/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Guest Communications
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

/**
 *  @file
 *
 *  @brief Linux-specific functions/types.
 */

#include "comm_os.h"

#define DISPATCH_MAX_CYCLES 8192

/* Type definitions  */

typedef struct workqueue_struct CommOSWorkQueue;


/* Static data */

static volatile int running;
static int numCpus;
static CommOSWorkQueue *dispatchWQ;
static CommOSDispatchFunc dispatch;
static CommOSWork dispatchWorksNow[NR_CPUS];
static CommOSWork dispatchWorks[NR_CPUS];
static unsigned int dispatchInterval = 1;
static unsigned int dispatchMaxCycles = 2048;
static CommOSWorkQueue *aioWQ;


/**
 *  @brief Initializes a workqueue consisting of per-cpu kernel threads.
 *  @param name workqueue name
 *  @return workqueue handle if successful, NULL otherwise
 */

static inline CommOSWorkQueue *
CreateWorkqueue(const char *name)
{
   return create_workqueue(name);
}


/**
 *  @brief Destroys a workqueue and stops its threads.
 *  @param[in,out] wq workqueue to destroy.
 *  @return workqueue handle is successful, NULL otherwise.
 */

static inline void
DestroyWorkqueue(CommOSWorkQueue *wq)
{
   destroy_workqueue(wq);
}


/**
 *  @brief Force execution of a work item.
 *  @param[in,out] work work item to dequeue.
 */

static inline void
FlushDelayedWork(CommOSWork *work)
{
   flush_delayed_work(work);
}


/**
 *  @brief Enqueue a work item to a workqueue for execution on a given cpu
 *      and after the specified interval.
 *  @param cpu cpu number. If negative, work item is enqueued on current cpu.
 *  @param[in,out] wq target work queue.
 *  @param[in,out] work work item to enqueue.
 *  @param jif delay interval.
 *  @return zero if successful, non-zero otherwise.
 */

static inline int
QueueDelayedWorkOn(int cpu,
                   CommOSWorkQueue *wq,
                   CommOSWork *work,
                   unsigned long jif)
{
   if (cpu < 0) {
      return !queue_delayed_work(wq, work, jif) ? -1 : 0;
   } else {
      return !queue_delayed_work_on(cpu, wq, work, jif) ? -1 : 0;
   }
}


/**
 *  @brief Enqueues a work item to a workqueue for execution on the current cpu
 *      and after the specified interval.
 *  @param[in,out] wq target work queue.
 *  @param[in,out] work work item to enqueue.
 *  @param jif delay interval.
 *  @return zero if successful, non-zero otherwise.
 */

static inline int
QueueDelayedWork(CommOSWorkQueue *wq,
                 CommOSWork *work,
                 unsigned long jif)
{
   return QueueDelayedWorkOn(-1, wq, work, jif);
}


/**
 *  @brief Cancels a queued delayed work item and synchronizes with its
 *      completion.
 *  @param[in,out] work work item to cancel
 */

static inline void
WaitForDelayedWork(CommOSWork *work)
{
   cancel_delayed_work_sync(work);
}


/**
 *  @brief Discards work items queued to the specified workqueue.
 *  @param[in,out] wq work queue to flush.
 */

static inline void
FlushWorkqueue(CommOSWorkQueue *wq)
{
   flush_workqueue(wq);
}


/**
 *  @brief Schedules dispatcher threads for immediate execution.
 */

void
CommOS_ScheduleDisp(void)
{
   CommOSWork *work = &dispatchWorksNow[get_cpu()];

   put_cpu();
   if (running) {
      QueueDelayedWork(dispatchWQ, work, 0);
   }
}


/**
 *  @brief Default delayed work callback function implementation.
 *      Calls the input function specified at initialization.
 *  @param[in,out] work work item.
 */

static void
DispatchWrapper(CommOSWork *work)
{
   unsigned int misses;

   for (misses = 0; running && (misses < dispatchMaxCycles); ) {
      /* We run for at most dispatchMaxCycles worth of channel no-ops. */

      if (!dispatch()) {
         /* No useful work was done, on any of the channels. */

         misses++;
         if ((misses % 32) == 0) {
            CommOS_Yield();
         }
      } else {
         misses = 0;
      }
   }

   if (running &&
       (work >= &dispatchWorks[0]) &&
       (work <= &dispatchWorks[NR_CPUS - 1])) {
      /*
       * If still running _and_ this was a regular, time-based run, then
       * re-arm the timer.
       */

      QueueDelayedWork(dispatchWQ, work, dispatchInterval);
   }
}


/**
 *  @brief Initializes work item with specified callback function.
 *  @param[in,out] work work queue to initialize.
 *  @param func work item to initialize the queue with.
 */

void
CommOS_InitWork(CommOSWork *work,
                CommOSWorkFunc func)
{
   INIT_DELAYED_WORK(work, (work_func_t)func);
}


/**
 *  @brief Flush execution of a work item
 *  @param{in,out] work work item to dequeue
 */
void
CommOS_FlushAIOWork(CommOSWork *work)
{
   if (aioWQ && work) {
      FlushDelayedWork(work);
   }
}


/**
 *  @brief Queue a work item to the AIO workqueue.
 *  @param[in,out] work work item to enqueue.
 *  @return zero if work enqueued, non-zero otherwise.
 */

int
CommOS_ScheduleAIOWork(CommOSWork *work)
{
   if (running && aioWQ && work) {
      return QueueDelayedWork(aioWQ, work, 0);
   }
   return -1;
}


/**
 *  @brief Initializes the base IO system.
 *  @param dispatchTaskName dispatch thread(s) name.
 *  @param dispatchFunc dispatch function.
 *  @param intervalMillis periodic interval in milliseconds to call dispatch.
 *         The floor is 1 jiffy, regardless of how small intervalMillis is
 *  @param maxCycles number of cycles to do adaptive polling before scheduling.
 *         The maximum number of cycles is DISPATCH_MAX_CYCLES.
 *  @param aioTaskName AIO thread(s) name. If NULL, AIO threads aren't started.
 *  @return zero is successful, -1 otherwise.
 *  @sideeffects Dispatch threads, and if applicable, AIO threads are started.
 */

int
CommOS_StartIO(const char *dispatchTaskName,    // IN
               CommOSDispatchFunc dispatchFunc, // IN
               unsigned int intervalMillis,     // IN
               unsigned int maxCycles,          // IN
               const char *aioTaskName)         // IN
{
   int rc;
   int cpu;

   if (running) {
      CommOS_Debug(("%s: I/O tasks already running.\n", __FUNCTION__));
      return 0;
   }

   /*
    * OK, let's test the handler against NULL. Though, the whole concept
    * of checking for NULL pointers, outside cases where NULL is meaningful
    * to the implementation, is relatively useless: garbage, random pointers
    * rarely happen to be all-zeros.
    */

   if (!dispatchFunc) {
      CommOS_Log(("%s: a NULL Dispatch handler was passed.\n", __FUNCTION__));
      return -1;
   }
   dispatch = dispatchFunc;

   if (intervalMillis == 0) {
      intervalMillis = 4;
   }
   if ((dispatchInterval = msecs_to_jiffies(intervalMillis)) < 1) {
      dispatchInterval = 1;
   }
   if (maxCycles > DISPATCH_MAX_CYCLES) {
      dispatchMaxCycles = DISPATCH_MAX_CYCLES;
   } else if (maxCycles > 0) {
      dispatchMaxCycles = maxCycles;
   }
   CommOS_Debug(("%s: Interval millis %u (jif:%u).\n", __FUNCTION__,
                 intervalMillis, dispatchInterval));
   CommOS_Debug(("%s: Max cycles %u.\n", __FUNCTION__, dispatchMaxCycles));

   numCpus = num_present_cpus();
   dispatchWQ = CreateWorkqueue(dispatchTaskName);
   if (!dispatchWQ) {
      CommOS_Log(("%s: Couldn't create %s task(s).\n", __FUNCTION__,
                  dispatchTaskName));
      return -1;
   }

   if (aioTaskName) {
      aioWQ = CreateWorkqueue(aioTaskName);
      if (!aioWQ) {
         CommOS_Log(("%s: Couldn't create %s task(s).\n", __FUNCTION__,
                     aioTaskName));
         DestroyWorkqueue(dispatchWQ);
         return -1;
      }
   } else {
      aioWQ = NULL;
   }

   running = 1;
   for (cpu = 0; cpu < numCpus; cpu++) {
      CommOS_InitWork(&dispatchWorksNow[cpu], DispatchWrapper);
      CommOS_InitWork(&dispatchWorks[cpu], DispatchWrapper);
      rc = QueueDelayedWorkOn(cpu, dispatchWQ,
                              &dispatchWorks[cpu],
                              dispatchInterval);
      if (rc != 0) {
         CommOS_StopIO();
         return -1;
      }
   }
   CommOS_Log(("%s: Created I/O task(s) successfully.\n", __FUNCTION__));
   return 0;
}


/**
 *  @brief Stops the base IO system.
 *  @sideeffects Dispatch threads, and if applicable, AIO threads are stopped.
 */

void
CommOS_StopIO(void)
{
   int cpu;

   if (running) {
      running = 0;
      if (aioWQ) {
         FlushWorkqueue(aioWQ);
         DestroyWorkqueue(aioWQ);
         aioWQ = NULL;
      }
      FlushWorkqueue(dispatchWQ);
      for (cpu = 0; cpu < numCpus; cpu++) {
         WaitForDelayedWork(&dispatchWorksNow[cpu]);
         WaitForDelayedWork(&dispatchWorks[cpu]);
      }
      DestroyWorkqueue(dispatchWQ);
      dispatchWQ = NULL;
      CommOS_Log(("%s: I/O tasks stopped.\n", __FUNCTION__));
   }
}
