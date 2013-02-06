/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Hypervisor Support
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
 * @file
 *
 * @brief The host kernel mutex functions.  These mutexes can be located in
 *        shared address space with the monitor.
 */

#include <linux/kernel.h>

#include <asm/string.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/hardirq.h>

#include "mvp.h"

#include "arm_inline.h"
#include "coproc_defs.h"
#include "mutex_kernel.h"

#define POLL_IN_PROGRESS_FLAG (1<<(30-MUTEX_CVAR_MAX))

#define INITWAITQ(waitQ) do {                         \
   init_waitqueue_head((wait_queue_head_t *)(waitQ)); \
} while (0)

#define WAKEUPALL(waitQ) do {                  \
   wake_up_all((wait_queue_head_t *)(waitQ));  \
} while (0)

#define WAKEUPONE(waitQ) do {                  \
   wake_up((wait_queue_head_t *)(waitQ));      \
} while (0)

/**
 * @brief initialize mutex
 * @param[in,out] mutex mutex to initialize
 */
void
Mutex_Init(Mutex *mutex)
{
   wait_queue_head_t *wq;
   int i;

   wq = kcalloc(MUTEX_CVAR_MAX + 1, sizeof(wait_queue_head_t), 0);
   FATAL_IF(wq == NULL);

   memset(mutex, 0, sizeof *mutex);
   mutex->mtxHKVA = (HKVA)mutex;
   mutex->lockWaitQ = (HKVA)&wq[0];
   INITWAITQ(mutex->lockWaitQ);
   for (i = 0; i < MUTEX_CVAR_MAX; i ++) {
      mutex->cvarWaitQs[i] = (HKVA)&wq[i + 1];
      INITWAITQ(mutex->cvarWaitQs[i]);
   }
}

/**
 * @brief Check if it is ok to sleep
 * @param file the file of the caller code
 * @param line the line number of the caller code
 */
static void
MutexCheckSleep(const char *file, int line)
{
#ifdef MVP_DEVEL
   static unsigned long prev_jiffy;        /* ratelimiting: 1/s */

#ifdef CONFIG_PREEMPT
   if (preemptible() && !irqs_disabled()) {
      return;
   }
#else
   if (!irqs_disabled()) {
      return;
   }
#endif
   if (time_before(jiffies, prev_jiffy + HZ) && prev_jiffy) {
      return;
   }
   prev_jiffy = jiffies;
   printk(KERN_ERR
          "BUG: sleeping function called from invalid context at %s:%d\n",
          file, line);
   printk(KERN_ERR
          "irqs_disabled(): %d, preemtible(): %d, pid: %d, name: %s\n",
          irqs_disabled(),
          preemptible(),
          current->pid, current->comm);
   dump_stack();
#endif
}

/**
 * @brief destroy mutex
 * @param[in,out] mutex mutex to destroy
 */
void
Mutex_Destroy(Mutex *mutex)
{
   kfree((void*)mutex->lockWaitQ);
}

/**
 * @brief Lock the mutex.  Also does a data barrier after locking so the
 *        locking is complete before any shared data is accessed.
 * @param[in,out] mutex which mutex to lock
 * @param         mode  mutex lock mode
 * @param file the file of the caller code
 * @param line the line number of the code that called this function
 * @return rc = 0: mutex now locked by caller<br>
 *             < 0: interrupted
 */
int
Mutex_LockLine(Mutex *mutex, MutexMode mode, const char *file, int line)
{
   Mutex_State newState, oldState;

   MutexCheckSleep(file, line);

   /*
    * If uncontended, just set new lock state and return success status.
    * If contended, mark state saying there is a waiting thread to wake.
    */
   do {
lock_start:
      /*
       * Get current state and calculate what new state would be.
       * New state adds 1 for shared and 0xFFFF for exclusive.
       * If the 16 bit field overflows, there is contention.
       */
      oldState.state = ATOMIC_GETO(mutex->state);
      newState.mode  = oldState.mode + mode;
      newState.blck  = oldState.blck;

      /*
       * So we are saying there is no contention if new state
       * indicates no overflow.
       *
       * On fairness: The test here allows a new-comer thread to grab
       * the lock even if there is a blocked thread. For example 2
       * threads repeatedly obtaining shared access can starve a third
       * wishing to obtain an exclusive lock. Currently this is only a
       * hypothetical situation as mksck use exclusive lock only and
       * the code never has more than 2 threads using the same mutex.
       */
      if ((uint32)newState.mode >= (uint32)mode) {
         if (!ATOMIC_SETIF(mutex->state, newState.state, oldState.state)) {
            goto lock_start;
         }
         DMB();
         mutex->line    = line;
         mutex->lineUnl = -1;
         return 0;
      }

      /*
       * There is contention, so increment the number of blocking threads.
       */
      newState.mode = oldState.mode;
      newState.blck = oldState.blck + 1;
   } while (!ATOMIC_SETIF(mutex->state, newState.state, oldState.state));

   /*
    * Statistics...
    */
   ATOMIC_ADDV(mutex->blocked, 1);

   /*
    * Mutex is contended, state has been updated to say there is a blocking
    * thread.
    *
    * So now we block till someone wakes us up.
    */
   do {
      DEFINE_WAIT(waiter);

      /*
       * This will make sure we catch any wakes done after we check the lock
       * state again.
       */
      prepare_to_wait((wait_queue_head_t *)mutex->lockWaitQ,
                      &waiter,
                      TASK_INTERRUPTIBLE);

      /*
       * Now that we will catch wakes, check the lock state again.  If now
       * uncontended, mark it locked, abandon the wait and return success.
       */

set_new_state:
      /*
       * Same as the original check for contention above, except that we
       * must decrement the number of waiting threads by one
       * if we are successful in locking the mutex.
       */
      oldState.state = ATOMIC_GETO(mutex->state);
      newState.mode  = oldState.mode + mode;
      newState.blck  = oldState.blck - 1;
      ASSERT(oldState.blck);

      if ((uint32)newState.mode >= (uint32)mode) {
         if (!ATOMIC_SETIF(mutex->state, newState.state, oldState.state)) {
            goto set_new_state;
         }
         /*
          * Mutex is no longer contended and we were able to lock it.
          */
         finish_wait((wait_queue_head_t *)mutex->lockWaitQ, &waiter);
         DMB();
         mutex->line    = line;
         mutex->lineUnl = -1;
         return 0;
      }

      /*
       * Wait for a wake that happens any time after prepare_to_wait()
       * returned.
       */
      WARN(!schedule_timeout(10*HZ), "Mutex_Lock: soft lockup - stuck for 10s!\n");
      finish_wait((wait_queue_head_t *)mutex->lockWaitQ, &waiter);
   } while (!signal_pending(current));

   /*
    * We aren't waiting anymore, so decrement the number of waiting threads.
    */
   do {
      oldState.state = ATOMIC_GETO(mutex->state);
      newState.mode  = oldState.mode;
      newState.blck  = oldState.blck - 1;

      ASSERT(oldState.blck);

   } while (!ATOMIC_SETIF(mutex->state, newState.state, oldState.state));

   return -ERESTARTSYS;
}


/**
 * @brief Unlock the mutex.  Also does a data barrier before unlocking so any
 *        modifications made before the lock gets released will be completed
 *        before the lock is released.
 * @param mutex as passed to Mutex_Lock()
 * @param mode  as passed to Mutex_Lock()
 * @param line the line number of the code that called this function
 */
void
Mutex_UnlockLine(Mutex *mutex, MutexMode mode, int line)
{
   Mutex_State newState, oldState;

   DMB();
   do {
      oldState.state = ATOMIC_GETO(mutex->state);
      newState.mode  = oldState.mode - mode;
      newState.blck  = oldState.blck;
      mutex->lineUnl = line;

      ASSERT(oldState.mode >= mode);
   } while (!ATOMIC_SETIF(mutex->state, newState.state, oldState.state));

   /*
    * If another thread was blocked, then wake it up.
    */
   if (oldState.blck) {
      if (mode == MutexModeSH) {
         WAKEUPONE(mutex->lockWaitQ);
      } else {
         WAKEUPALL(mutex->lockWaitQ);
      }
   }
}


/**
 * @brief Unlock the mutex and sleep.  Also does a data barrier before
 *        unlocking so any modifications made before the lock gets released
 *        will be completed before the lock is released.
 * @param mutex as passed to Mutex_Lock()
 * @param mode  as passed to Mutex_Lock()
 * @param cvi   which condition variable to sleep on
 * @param file the file of the caller code
 * @param line the line number of the caller code
 * @return rc = 0: successfully waited<br>
 *            < 0: error waiting
 */
int
Mutex_UnlSleepLine(Mutex *mutex, MutexMode mode, uint32 cvi, const char *file, int line)
{
   return Mutex_UnlSleepTestLine(mutex, mode, cvi, NULL, 0, file, line);
}

/**
 * @brief Unlock the mutex and sleep.  Also does a data barrier before
 *        unlocking so any modifications made before the lock gets released
 *        will be completed before the lock is released.
 * @param mutex as passed to Mutex_Lock()
 * @param mode  as passed to Mutex_Lock()
 * @param cvi   which condition variable to sleep on
 * @param test  sleep only if null or pointed atomic value mismatches mask
 * @param mask  bitfield to check test against before sleeping
 * @param file the file of the caller code
 * @param line the line number of the caller code
 * @return rc = 0: successfully waited<br>
 *            < 0: error waiting
 */
int
Mutex_UnlSleepTestLine(Mutex *mutex, MutexMode mode, uint32 cvi, AtmUInt32 *test, uint32 mask, const char *file, int line)
{
   DEFINE_WAIT(waiter);

   MutexCheckSleep(file, line);

   ASSERT(cvi < MUTEX_CVAR_MAX);

   /*
    * Tell anyone who might try to wake us that they need to actually call
    * WAKEUP***().
    */
   ATOMIC_ADDV(mutex->waiters, 1);

   /*
    * Be sure to catch any wake that comes along just after we unlock the mutex
    * but before we call schedule().
    */
   prepare_to_wait_exclusive((wait_queue_head_t *)mutex->cvarWaitQs[cvi],
                   &waiter,
                   TASK_INTERRUPTIBLE);

   /*
    * Release the mutex, someone can wake us up now.
    * They will see mutex->waiters non-zero so will actually do the wake.
    */
   Mutex_Unlock(mutex, mode);

   /*
    * Wait to be woken or interrupted.
    */
   if (test == NULL || (ATOMIC_GETO(*test) & mask) == 0) {
      schedule();
   }
   finish_wait((wait_queue_head_t *)mutex->cvarWaitQs[cvi], &waiter);

   /*
    * Done waiting, don't need a wake any more.
    */
   ATOMIC_SUBV(mutex->waiters, 1);

   /*
    * If interrupted, return error status.
    */
   if (signal_pending(current)) {
      return -ERESTARTSYS;
   }

   /*
    * Wait completed, return success status.
    */
   return 0;
}


/**
 * @brief Unlock the mutex and prepare to sleep on a kernel polling table
 *        given as anonymous parameters for poll_wait
 * @param mutex as passed to Mutex_Lock()
 * @param mode  as passed to Mutex_Lock()
 * @param cvi   which condition variable to sleep on
 * @param filp  which file to poll_wait upon
 * @param wait  which poll_table to poll_wait upon
 */
void
Mutex_UnlPoll(Mutex *mutex, MutexMode mode, uint32 cvi, void *filp, void *wait)
{
   ASSERT(cvi < MUTEX_CVAR_MAX);

   /* poll_wait is done with mutex locked to prevent any wake that comes and
    * defer them just after we unlock the mutex but before kernel polling
    * tables are used
    * Note that the kernel is probably avoiding an exclusive wait in that case
    * and also increments the usage for the file given in filp
    */
   poll_wait(filp, (wait_queue_head_t *)mutex->cvarWaitQs[cvi], wait);

   /*
    * Tell anyone who might try to wake us that they need to actually call
    * WAKEUP***(). This is done in putting ourselves in a "noisy" mode since
    * there is no guaranty that we would really sleep, or if we would be
    * wakening the sleeping thread with that socket or condition. This is
    * done using a POLL_IN_PROGRESS_FLAG, but unfortunately it has to be
    * a per-cvi flag, in case we would poll independently on different cvi
    */
   DMB();
   ATOMIC_ORO(mutex->waiters, (POLL_IN_PROGRESS_FLAG << cvi));

   /*
    * Release the mutex, someone can wake us up now.
    * They will see mutex->waiters non-zero so will actually do the wake.
    */
   Mutex_Unlock(mutex, mode);
}


/**
 * @brief Unlock the semaphore and wake sleeping threads.  Also does a data
 *        barrier before unlocking so any modifications made before the lock
 *        gets released will be completed before the lock is released.
 * @param mutex as passed to Mutex_Lock()
 * @param mode  as passed to Mutex_Lock()
 * @param cvi   which condition variable to signal
 * @param all   false: wake a single thread<br>
 *              true: wake all threads
 */
void
Mutex_UnlWake(Mutex *mutex, MutexMode mode, uint32 cvi, _Bool all)
{
   Mutex_Unlock(mutex, mode);
   Mutex_CondSig(mutex, cvi, all);
}


/**
 * @brief Signal condition variable, ie, wake up anyone waiting.
 * @param mutex mutex that holds the condition variable
 * @param cvi   which condition variable to signal
 * @param all   false: wake a single thread<br>
 *              true: wake all threads
 */
void
Mutex_CondSig(Mutex *mutex, uint32 cvi, _Bool all)
{
   uint32 waiters;

   ASSERT(cvi < MUTEX_CVAR_MAX);

   waiters = ATOMIC_GETO(mutex->waiters);
   if (waiters != 0) {
      /* Cleanup the effects of Mutex_UnlPoll() but only when it is SMP safe,
       * considering that atomic and wakeup operations should also do memory
       * barriers accordingly. This is mandatory otherwise rare SMP races are
       * even possible, since Mutex_CondSig is called with the associated mutex
       * unlocked, and that does not prevent from select() to run parallel !
       */
      if ((waiters >= POLL_IN_PROGRESS_FLAG) &&
          !waitqueue_active((wait_queue_head_t *)mutex->cvarWaitQs[cvi])) {
         ATOMIC_ANDO(mutex->waiters, ~(POLL_IN_PROGRESS_FLAG << cvi));
      }
      DMB();

      if (all) {
         WAKEUPALL(mutex->cvarWaitQs[cvi]);
      } else {
         WAKEUPONE(mutex->cvarWaitQs[cvi]);
      }
   }
}
