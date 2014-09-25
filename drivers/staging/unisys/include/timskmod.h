/* timskmod.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __TIMSKMOD_H__
#define __TIMSKMOD_H__

#include <linux/version.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <asm/irq.h>
#include <linux/io.h>
#include <asm/dma.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/poll.h>
/* #define EXPORT_SYMTAB */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fcntl.h>
#include <linux/aio.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/seq_file.h>
#include <linux/mm.h>

/* #define DEBUG */
#ifndef BOOL
#define BOOL    int
#endif
#define FALSE   0
#define TRUE    1
#if !defined SUCCESS
#define SUCCESS 0
#endif
#define FAILURE (-1)
#define DRIVERNAMEMAX 50
#define MIN(a, b)     (((a) < (b)) ? (a) : (b))
#define MAX(a, b)     (((a) > (b)) ? (a) : (b))
#define STRUCTSEQUAL(x, y) (memcmp(&x, &y, sizeof(x)) == 0)
#ifndef HOSTADDRESS
#define HOSTADDRESS unsigned long long
#endif

typedef long VMMIO;  /**< Virtual MMIO address (returned from ioremap), which
    *   is a virtual address pointer to a memory-mapped region.
    *   These are declared as "long" instead of u32* to force you to
    *   use readb()/writeb()/memcpy_fromio()/etc to access them.
    *   (On x86 we could probably get away with treating them as
    *   pointers.)
    */
typedef long VMMIO8; /**< #VMMIO pointing to  8-bit data */
typedef long VMMIO16;/**< #VMMIO pointing to 16-bit data */
typedef long VMMIO32;/**< #VMMIO pointing to 32-bit data */

#define LOCKSEM(sem)                   down_interruptible(sem)
#define LOCKSEM_UNINTERRUPTIBLE(sem)   down(sem)
#define UNLOCKSEM(sem)                 up(sem)

/** lock read/write semaphore for reading.
    Note that all read/write semaphores are of the "uninterruptible" variety.
    @param sem (rw_semaphore *) points to semaphore to lock
 */
#define LOCKREADSEM(sem)               down_read(sem)

/** unlock read/write semaphore for reading.
    Note that all read/write semaphores are of the "uninterruptible" variety.
    @param sem (rw_semaphore *) points to semaphore to unlock
 */
#define UNLOCKREADSEM(sem)             up_read(sem)

/** lock read/write semaphore for writing.
    Note that all read/write semaphores are of the "uninterruptible" variety.
    @param sem (rw_semaphore *) points to semaphore to lock
 */
#define LOCKWRITESEM(sem)              down_write(sem)

/** unlock read/write semaphore for writing.
    Note that all read/write semaphores are of the "uninterruptible" variety.
    @param sem (rw_semaphore *) points to semaphore to unlock
 */
#define UNLOCKWRITESEM(sem)            up_write(sem)

#ifdef ENABLE_RETURN_TRACE
#define RETTRACE(x)                                            \
	do {						       \
		if (1) {				       \
			INFODRV("RET 0x%lx in %s",	       \
				(ulong)(x), __func__);     \
		}					   \
	} while (0)
#else
#define RETTRACE(x)
#endif

/** Try to evaulate the provided expression, and do a RETINT(x) iff
 *  the expression evaluates to < 0.
 *  @param x the expression to try
 */
#define ASSERT(cond)                                           \
	do { if (!(cond))                                      \
			HUHDRV("ASSERT failed - %s",	       \
			       __stringify(cond));	       \
	} while (0)

#define sizeofmember(TYPE, MEMBER) (sizeof(((TYPE *)0)->MEMBER))
/** "Covered quotient" function */
#define COVQ(v, d)  (((v) + (d) - 1) / (d))
#define SWAPPOINTERS(p1, p2)				\
	do {						\
		void *SWAPPOINTERS_TEMP = (void *)p1;	\
		(void *)(p1) = (void *)(p2);            \
		(void *)(p2) = SWAPPOINTERS_TEMP;	\
	} while (0)

/**
 *  @addtogroup driverlogging
 *  @{
 */

#define PRINTKDRV(fmt, args...) LOGINF(fmt, ## args)
#define TBDDRV(fmt, args...)    LOGERR(fmt, ## args)
#define HUHDRV(fmt, args...)    LOGERR(fmt, ## args)
#define ERRDRV(fmt, args...)    LOGERR(fmt, ## args)
#define WARNDRV(fmt, args...)   LOGWRN(fmt, ## args)
#define SECUREDRV(fmt, args...) LOGWRN(fmt, ## args)
#define INFODRV(fmt, args...)   LOGINF(fmt, ## args)
#define DEBUGDRV(fmt, args...)  DBGINF(fmt, ## args)

#define PRINTKDEV(devname, fmt, args...)  LOGINFDEV(devname, fmt, ## args)
#define TBDDEV(devname, fmt, args...)     LOGERRDEV(devname, fmt, ## args)
#define HUHDEV(devname, fmt, args...)     LOGERRDEV(devname, fmt, ## args)
#define ERRDEV(devname, fmt, args...)     LOGERRDEV(devname, fmt, ## args)
#define ERRDEVX(devno, fmt, args...)	  LOGERRDEVX(devno, fmt, ## args)
#define WARNDEV(devname, fmt, args...)    LOGWRNDEV(devname, fmt, ## args)
#define SECUREDEV(devname, fmt, args...)  LOGWRNDEV(devname, fmt, ## args)
#define INFODEV(devname, fmt, args...)    LOGINFDEV(devname, fmt, ## args)
#define INFODEVX(devno, fmt, args...)     LOGINFDEVX(devno, fmt, ## args)
#define DEBUGDEV(devname, fmt, args...)   DBGINFDEV(devname, fmt, ## args)


/* @} */

/** Verifies the consistency of your PRIVATEDEVICEDATA structure using
 *  conventional "signature" fields:
 *  <p>
 *  - sig1 should contain the size of the structure
 *  - sig2 should contain a pointer to the beginning of the structure
 */
#define DDLOOKSVALID(dd)                                 \
		((dd != NULL)                             &&	\
		 ((dd)->sig1 == sizeof(PRIVATEDEVICEDATA)) &&	\
		 ((dd)->sig2 == dd))

/** Verifies the consistency of your PRIVATEFILEDATA structure using
 *  conventional "signature" fields:
 *  <p>
 *  - sig1 should contain the size of the structure
 *  - sig2 should contain a pointer to the beginning of the structure
 */
#define FDLOOKSVALID(fd)                               \
	((fd != NULL)                           &&     \
	 ((fd)->sig1 == sizeof(PRIVATEFILEDATA)) &&    \
	 ((fd)->sig2 == fd))

/** Locks dd->lockDev if you havn't already locked it */
#define LOCKDEV(dd)                                                    \
	{                                                              \
		if (!lockedDev) {				       \
			spin_lock(&dd->lockDev);		       \
			lockedDev = TRUE;			       \
		}						       \
	}

/** Unlocks dd->lockDev if you previously locked it */
#define UNLOCKDEV(dd)                                                  \
	{                                                              \
		if (lockedDev) {				       \
			spin_unlock(&dd->lockDev);		       \
			lockedDev = FALSE;			       \
		}						       \
	}

/** Locks dd->lockDevISR if you havn't already locked it */
#define LOCKDEVISR(dd)                                                 \
	{                                                              \
		if (!lockedDevISR) {				       \
			spin_lock_irqsave(&dd->lockDevISR, flags);     \
			lockedDevISR = TRUE;			       \
		}						       \
	}

/** Unlocks dd->lockDevISR if you previously locked it */
#define UNLOCKDEVISR(dd)						\
	{								\
		if (lockedDevISR) {					\
			spin_unlock_irqrestore(&dd->lockDevISR, flags); \
			lockedDevISR = FALSE;				\
		}							\
	}

/** Locks LockGlobalISR if you havn't already locked it */
#define LOCKGLOBALISR                                                  \
	{                                                              \
		if (!lockedGlobalISR) {				       \
			spin_lock_irqsave(&LockGlobalISR, flags);      \
			lockedGlobalISR = TRUE;			       \
		}						       \
	}

/** Unlocks LockGlobalISR if you previously locked it */
#define UNLOCKGLOBALISR                                                \
	{                                                              \
		if (lockedGlobalISR) {				       \
			spin_unlock_irqrestore(&LockGlobalISR, flags); \
			lockedGlobalISR = FALSE;		       \
		}						       \
	}

/** Locks LockGlobal if you havn't already locked it */
#define LOCKGLOBAL                                                     \
	{                                                              \
		if (!lockedGlobal) {				       \
			spin_lock(&LockGlobal);			       \
			lockedGlobal = TRUE;			       \
		}						       \
	}

/** Unlocks LockGlobal if you previously locked it */
#define UNLOCKGLOBAL                                                   \
	{                                                              \
		if (lockedGlobal) {				       \
			spin_unlock(&LockGlobal);		       \
			lockedGlobal = FALSE;			       \
		}						       \
	}

/** Use this at the beginning of functions where you intend to
 *  use #LOCKDEV/#UNLOCKDEV, #LOCKDEVISR/#UNLOCKDEVISR,
 *  #LOCKGLOBAL/#UNLOCKGLOBAL, #LOCKGLOBALISR/#UNLOCKGLOBALISR.
 *
 *  Note that __attribute__((unused)) is how you tell GNU C to suppress
 *  any warning messages about the variable being unused.
 */
#define LOCKPREAMBLE							\
	ulong flags __attribute__((unused)) = 0;			\
	BOOL lockedDev __attribute__((unused)) = FALSE;			\
	BOOL lockedDevISR __attribute__((unused)) = FALSE;		\
	BOOL lockedGlobal __attribute__((unused)) = FALSE;		\
	BOOL lockedGlobalISR __attribute__((unused)) = FALSE



/** Sleep for an indicated number of seconds (for use in kernel mode).
 *  @param x the number of seconds to sleep.
 */
#define SLEEP(x)					     \
	do { current->state = TASK_INTERRUPTIBLE;	     \
		schedule_timeout((x)*HZ);		     \
	} while (0)

/** Sleep for an indicated number of jiffies (for use in kernel mode).
 *  @param x the number of jiffies to sleep.
 */
#define SLEEPJIFFIES(x)						    \
	do { current->state = TASK_INTERRUPTIBLE;		    \
		schedule_timeout(x);				    \
	} while (0)

#ifndef max
#define max(a, b) (((a) > (b)) ? (a):(b))
#endif

static inline struct cdev *cdev_alloc_init(struct module *owner,
					   const struct file_operations *fops)
{
	struct cdev *cdev = NULL;
	cdev = cdev_alloc();
	if (!cdev)
		return NULL;
	cdev->ops = fops;
	cdev->owner = owner;

	/* Note that the memory allocated for cdev will be deallocated
	 * when the usage count drops to 0, because it is controlled
	 * by a kobject of type ktype_cdev_dynamic.  (This
	 * deallocation could very well happen outside of our kernel
	 * module, like via the cdev_put in __fput() for example.)
	 */
	return cdev;
}

#include "timskmodutils.h"

#endif
