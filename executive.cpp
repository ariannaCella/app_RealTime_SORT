#include <cassert>
#include <iostream>

#define VERBOSE

#include "executive.h"

Executive::Executive(size_t num_tasks, unsigned int frame_length, unsigned int unit_duration)
	: p_tasks(num_tasks), frame_length(frame_length), unit_time(unit_duration)
{
}

void Executive::set_periodic_task(size_t task_id, std::function<void()> periodic_task, unsigned int wcet)
{
	assert(task_id < p_tasks.size()); // Fallisce in caso di task_id non corretto (fuori range)
	p_tasks[task_id].function = periodic_task;
	p_tasks[task_id].wcet = wcet;
}

void Executive::set_aperiodic_task(std::function<void()> aperiodic_task, unsigned int wcet)
{
 	ap_task.function = aperiodic_task;
 	ap_task.wcet = wcet;
}
		
void Executive::add_frame(std::vector<size_t> frame)
{
	for (auto & id: frame)
		assert(id < p_tasks.size()); // Fallisce in caso di task_id non corretto (fuori range)
	
	frames.push_back(frame);
}

void Executive::start()
{
        rt::affinity aff("1");
        rt::priority prio(rt::priority::rt_min);
	for (size_t id = 0; id < p_tasks.size(); ++id)
	{
		assert(p_tasks[id].function); // Fallisce se set_periodic_task() non e' stato invocato per questo id
		
		p_tasks[id].thread = std::thread(&Executive::task_function, std::ref(p_tasks[id])); 
		
		rt::set_affinity(p_tasks[id].thread , aff);
		rt::set_priority(p_tasks[id].thread, prio);
		
	}
	
	assert(ap_task.function); // Fallisce se set_aperiodic_task() non e' stato invocato
	ap_task.state = task_data::IDLE;
	ap_task.thread = std::thread(&Executive::task_function, std::ref(ap_task));
	rt::set_affinity(ap_task.thread , aff);
	rt::set_priority(ap_task.thread, prio);
	
	exec_thread = std::thread(&Executive::exec_function, this); //gli passa se stesso
	rt::set_affinity(exec_thread , aff);
	prio = rt::priority::rt_max;
	rt::set_priority(exec_thread, prio);
}
	
void Executive::wait()
{
	exec_thread.join();
	ap_task.thread.join();
	
	for (auto & pt: p_tasks)
		pt.thread.join();
}

void Executive::ap_task_request()
{
	std::unique_lock<std::mutex> lock(ap_task.mutex);
	std::cout << "Sono il job aperiodico e mi hanno invocato" << std::endl;
	AP_request = true;
}

void Executive::task_function(Executive::task_data & task)
{
	while(true){
	  {
	        std::unique_lock<std::mutex> lock(task.mutex);
	        if (task.inMiss == true) {
	            task.inMiss = false;
	            std::cout << "Task in miss è riuscito a terminare" << std::endl;
	        }
                task.state = task_data::IDLE;
                
	        while(task.state!=task_data::PENDING){
	            task.cond.wait(lock);
	        }
	        task.state=task_data::RUNNING;
	  }
	  
          // Esegui il task
          task.function();
          // Dopo l'esecuzione, ritorna in stato IDLE
        }
}

void Executive::exec_function()
{
	size_t frame_id = 0;
	auto timer_frame = std::chrono::steady_clock::now();
	
	while (true)
	{
#ifdef VERBOSE
		std::cout << "*** Frame n." << frame_id << (frame_id == 0 ? " ******" : "") << std::endl;
#endif
                /*CONTROLLO APERIODICI*/
		{
		    std::unique_lock<std::mutex> lock(ap_task.mutex);
		    if (AP_request == true){
		            AP_request = false;
		            if (ap_task.state == task_data::IDLE) {
	                            ap_task.state = task_data::PENDING;
	                            std::cout << "Accetto il job aperiodico" << std::endl;
	                            ap_task.cond.notify_one();
		            }
		            else {
		                    std::cerr << "*** RIFIUTO JOB APERIODICO (Deadline miss aperiodico) ***" << std::endl;
		            }
		    }
		}
		

		/* Rilascio dei task periodici del frame corrente ... */
		rt::priority prio(rt::priority::rt_max-1);
		for (auto id_task : frames[frame_id]) {
                        std::unique_lock<std::mutex> lock(p_tasks[id_task].mutex);
                        if(p_tasks[id_task].state == task_data::IDLE){
                                p_tasks[id_task].state=task_data::PENDING;
                                rt::set_priority(p_tasks[id_task].thread, prio);
                                --prio;
                                p_tasks[id_task].cond.notify_one();
                        }
		}
	      
		
		/* Attesa fino al prossimo inizio frame ... */  
		timer_frame += std::chrono::milliseconds(frame_length * unit_time);  
		std::this_thread::sleep_until(timer_frame);
		
		
		/* Controllo delle deadline ... */
		 for (size_t id_task : frames[frame_id]) {
		      std::unique_lock<std::mutex> lock(p_tasks[id_task].mutex);
                      if (p_tasks[id_task].state != task_data::IDLE){
                              if (p_tasks[id_task].state == task_data::PENDING){
                                        std::cerr << "Deadline miss for task : task " << id_task << " cancellato perchè in PENDING" << std::endl;
                                        p_tasks[id_task].state=task_data::IDLE;
                              }
                              else if(p_tasks[id_task].inMiss == false) {
                                        std::cerr << "Deadline miss for task " << id_task << std::endl;
                                        rt::set_priority(p_tasks[id_task].thread, rt::priority::rt_min);
                                        p_tasks[id_task].inMiss = true;
                              }
                      }
                } 
		
		if (++frame_id == frames.size())
			frame_id = 0;
	}
}

/*AGGIUNGERE FUNZIONALITA' DI PORRE IN ESECUZIONE ANCHE UN TASK APERIODICO, DEVE USARE IDLE. AGGIUNGERE FUNZIONALITA' NELLA CLASSE PUBBLICA FORNENDO METODO CHE PUO' ESSERE INVOCATO PER ANDARLO AD ESEGUIRE A PARTIRE 
IO HO RICHIESTA CHE VIENE PRESA AL FRAME SUCCESSIVO 
DEVE FARE TUTTO EXECUTIVE IN PRATICA, SERVE FUNZIONE CHE DICA CHE C'È QUALCOSA DA FARE 
MODIFICA AL MATERIALE ALLA TRACCIA
*/
