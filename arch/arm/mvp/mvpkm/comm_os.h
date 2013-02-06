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
 *  @file
 *
 *  @brief Cross-platform base type definitions and function declarations.
 *         Includes OS-specific base type definitions and function declarations.
 */

#ifndef	_COMM_OS_H_
#define	_COMM_OS_H_

/* For-ever timeout constant (in milliseconds). */
#define COMM_OS_4EVER_TO ((unsigned long long)(~0UL >> 1))

/* Condition function prototype. Returns 1: true, 0: false, < 0: error code. */
typedef int (*CommOSWaitConditionFunc)(void *arg1, void *arg2);

/* Dispatch function prototype. Called by input (dispatch) kernel threads. */
typedef unsigned int (*CommOSDispatchFunc)(void);

/* Module initialization and exit callback functions. */
extern int  (*commOSModInit)(void *args);
extern void (*commOSModExit)(void);

/* Macro to assign Init and Exit callbacks. */
#define COMM_OS_MOD_INIT(init, exit)        \
   int (*commOSModInit)(void *args) = init; \
   void (*commOSModExit)(void) = exit


/*
 * OS-specific implementations must provide the following:
 *    1. Types:
 *       CommOSAtomic
 *       CommOSSpinlock
 *       CommOSMutex
 *       CommOSWaitQueue
 *       CommOSWork
 *       CommOSWorkFunc
 *       CommOSList
 *       CommOSModule
 *       struct kvec
 *
 *    2. Definition, initializers:
 *       CommOSSpinlock_Define()
 *
 *    3. Functions:
 *       void CommOS_Debug(const char *format, ...);
 *       void CommOS_Log(const char *format, ...);
 *       void CommOS_WriteAtomic(CommOSAtomic *atomic, int val);
 *       int CommOS_ReadAtomic(CommOSAtomic *atomic);
 *       int CommOS_AddReturnAtomic(CommOSAtomic *atomic, int val);
 *       int CommOS_SubReturnAtomic(CommOSAtomic *atomic, int val);
 *       void CommOS_SpinlockInit(CommOSSpinlock *lock);
 *       void CommOS_SpinLockBH(CommOSSpinlock *lock);
 *       int CommOS_SpinTrylockBH(CommOSSpinlock *lock);
 *       void CommOS_SpinUnlockBH(CommOSSpinlock *lock);
 *       void CommOS_SpinLock(CommOSSpinlock *lock);
 *       int CommOS_SpinTrylock(CommOSSpinlock *lock);
 *       void CommOS_SpinUnlock(CommOSSpinlock *lock);
 *       void CommOS_MutexInit(CommOSMutex *mutex);
 *       void CommOS_MutexLock(CommOSMutex *mutex);
 *       int CommOS_MutexLockUninterruptible(CommOSMutex *mutex);
 *       int CommOS_MutexTrylock(CommOSMutex *mutex);
 *       void CommOS_MutexUnlock(CommOSMutex *mutex);
 *       void CommOS_WaitQueueInit(CommOSWaitQueue *wq);
 *       CommOS_DoWait(CommOSWaitQueue *wq,
 *                     CommOSWaitConditionFunc cond,
 *                     void *condArg1,
 *                     void *condArg2,
 *                     unsigned long long *timeoutMillis,
 *                     int interruptible);
 *       int CommOS_Wait(CommOSWaitQueue *wq,
 *                       CommOSWaitConditionFunc func,
 *                       void *funcArg1,
 *                       void *funcArg2,
 *                       unsigned long long *timeoutMillis);
 *       int CommOS_WaitUninterruptible(CommOSWaitQueue *wq,
 *                                      CommOSWaitConditionFunc func,
 *                                      void *funcArg1,
 *                                      void *funcArg2,
 *                                      unsigned long long *timeoutMillis);
 *       void CommOS_WakeUp(CommOSWaitQueue *wq);
 *       void *CommOS_KmallocNoSleep(unsigned int size);
 *       void *CommOS_Kmalloc(unsigned int size);
 *       void CommOS_Kfree(void *arg);
 *       void CommOS_Yield(void);
 *       unsigned long long CommOS_GetCurrentMillis(void);
 *       void CommOS_ListInit(CommOSList *list);
 *       int CommOS_ListEmpty(CommOSList *list);
 *       void CommOS_ListAdd(CommOSList *list, CommOSList *listElem);
 *       void CommOS_ListAddTail(CommOSList *list, CommOSList *listElem);
 *       void int CommOS_ListDel(CommOSList *listElem);
 *       Macros:
 *          CommOS_ListForEach(*list, *item, itemListFieldName);
 *          CommOS_ListForEachSafe(*list, *item, *tmp, itemListFieldName);
 *       void CommOS_ListSplice(CommOSList *list, CommOSList *listToAdd);
 *       void CommOS_ListSpliceTail(CommOSList *list, CommOSList *listToAdd);
 *       CommOSModule CommOS_ModuleSelf(void);
 *       int CommOS_ModuleGet(CommOSModule module);
 *       void CommOS_ModulePut(CommOSModule module);
 *       void CommOS_MemBarrier(void);
 *
 *    These cannot be defined here: a) non-pointer type definitions need size
 *    information, and b) functions may or may not be inlined, or macros may
 *    be used instead.
 */


#ifdef __linux__
#include "comm_os_linux.h"
#else
#error "Unsupported OS"
#endif

/* Functions to start and stop the dispatch and aio kernel threads. */
void CommOS_StopIO(void);
void CommOS_ScheduleDisp(void);
void CommOS_InitWork(CommOSWork *work, CommOSWorkFunc func);
int CommOS_ScheduleAIOWork(CommOSWork *work);
void CommOS_FlushAIOWork(CommOSWork *work);

int
CommOS_StartIO(const char *dispatchTaskName,
               CommOSDispatchFunc dispatchHandler,
               unsigned int interval,
               unsigned int maxCycles,
               const char *aioTaskName);


#endif  /* _COMM_OS_H_ */
