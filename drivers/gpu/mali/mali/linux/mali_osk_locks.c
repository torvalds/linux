/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_locks.c
 * Implemenation of the OS abstraction layer for the kernel device driver
 */

/* needed to detect kernel version specific code */
#include <linux/version.h>

#include <linux/spinlock.h>
#include <linux/rwsem.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
#include <linux/semaphore.h>
#else /* pre 2.6.26 the file was in the arch specific location */
#include <asm/semaphore.h>
#endif

#include <linux/slab.h>
#include "mali_osk.h"
#include "mali_kernel_common.h"

/* These are all the locks we implement: */
typedef enum
{
	_MALI_OSK_INTERNAL_LOCKTYPE_SPIN,            /* Mutex, implicitly non-interruptable, use spin_lock/spin_unlock */
	_MALI_OSK_INTERNAL_LOCKTYPE_SPIN_IRQ,        /* Mutex, IRQ version of spinlock, use spin_lock_irqsave/spin_unlock_irqrestore */
	_MALI_OSK_INTERNAL_LOCKTYPE_MUTEX,           /* Interruptable, use up()/down_interruptable() */
	_MALI_OSK_INTERNAL_LOCKTYPE_MUTEX_NONINT,    /* Non-Interruptable, use up()/down() */
	_MALI_OSK_INTERNAL_LOCKTYPE_MUTEX_NONINT_RW, /* Non-interruptable, Reader/Writer, use {up,down}{read,write}() */

	/* Linux supports, but we do not support:
	 * Non-Interruptable Reader/Writer spinlock mutexes - RW optimization will be switched off
	 */

	/* Linux does not support:
	 * One-locks, of any sort - no optimization for this fact will be made.
	 */

} _mali_osk_internal_locktype;

struct _mali_osk_lock_t_struct
{
    _mali_osk_internal_locktype type;
	unsigned long flags;
    union
    {
        spinlock_t spinlock;
        struct semaphore sema;
        struct rw_semaphore rw_sema;
    } obj;
	MALI_DEBUG_CODE(
				  /** original flags for debug checking */
				  _mali_osk_lock_flags_t orig_flags;

				  /* id of the thread currently holding this lock, 0 if no
				   * threads hold it. */
				  u32 owner;
				  /* number of owners this lock currently has (can be > 1 if
				   * taken in R/O mode. */
				  u32 nOwners;
				  /* what mode the lock was taken in */
				  _mali_osk_lock_mode_t mode;
	); /* MALI_DEBUG_CODE */
};

_mali_osk_lock_t *_mali_osk_lock_init( _mali_osk_lock_flags_t flags, u32 initial, u32 order )
{
    _mali_osk_lock_t *lock = NULL;

	/* Validate parameters: */
	/* Flags acceptable */
	MALI_DEBUG_ASSERT( 0 == ( flags & ~(_MALI_OSK_LOCKFLAG_SPINLOCK
                                      | _MALI_OSK_LOCKFLAG_SPINLOCK_IRQ
                                      | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE
                                      | _MALI_OSK_LOCKFLAG_READERWRITER
                                      | _MALI_OSK_LOCKFLAG_ORDERED
                                      | _MALI_OSK_LOCKFLAG_ONELOCK )) );
	/* Spinlocks are always non-interruptable */
	MALI_DEBUG_ASSERT( (((flags & _MALI_OSK_LOCKFLAG_SPINLOCK) || (flags & _MALI_OSK_LOCKFLAG_SPINLOCK_IRQ)) && (flags & _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE))
					 || !(flags & _MALI_OSK_LOCKFLAG_SPINLOCK));
	/* Parameter initial SBZ - for future expansion */
	MALI_DEBUG_ASSERT( 0 == initial );

	lock = kmalloc(sizeof(_mali_osk_lock_t), GFP_KERNEL);

	if ( NULL == lock )
	{
		return lock;
	}

	/* Determine type of mutex: */
    /* defaults to interruptable mutex if no flags are specified */

	if ( (flags & _MALI_OSK_LOCKFLAG_SPINLOCK) )
	{
		/* Non-interruptable Spinlocks override all others */
		lock->type = _MALI_OSK_INTERNAL_LOCKTYPE_SPIN;
		spin_lock_init( &lock->obj.spinlock );
	}
	else if ( (flags & _MALI_OSK_LOCKFLAG_SPINLOCK_IRQ ) )
	{
		lock->type = _MALI_OSK_INTERNAL_LOCKTYPE_SPIN_IRQ;
		lock->flags = 0;
		spin_lock_init( &lock->obj.spinlock );
	}
	else if ( (flags & _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE)
			  && (flags & _MALI_OSK_LOCKFLAG_READERWRITER) )
	{
		lock->type = _MALI_OSK_INTERNAL_LOCKTYPE_MUTEX_NONINT_RW;
		init_rwsem( &lock->obj.rw_sema );
	}
	else
	{
		/* Usual mutex types */
		if ( (flags & _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE) )
		{
			lock->type = _MALI_OSK_INTERNAL_LOCKTYPE_MUTEX_NONINT;
		}
		else
		{
			lock->type = _MALI_OSK_INTERNAL_LOCKTYPE_MUTEX;
		}

		/* Initially unlocked */
		sema_init( &lock->obj.sema, 1 );
	}

#ifdef DEBUG
	/* Debug tracking of flags */
	lock->orig_flags = flags;

	/* Debug tracking of lock owner */
	lock->owner = 0;
	lock->nOwners = 0;
#endif /* DEBUG */

    return lock;
}

#ifdef DEBUG
u32 _mali_osk_lock_get_owner( _mali_osk_lock_t *lock )
{
	return lock->owner;
}

u32 _mali_osk_lock_get_number_owners( _mali_osk_lock_t *lock )
{
	return lock->nOwners;
}

u32 _mali_osk_lock_get_mode( _mali_osk_lock_t *lock )
{
	return lock->mode;
}
#endif /* DEBUG */

_mali_osk_errcode_t _mali_osk_lock_wait( _mali_osk_lock_t *lock, _mali_osk_lock_mode_t mode)
{
    _mali_osk_errcode_t err = _MALI_OSK_ERR_OK;

	/* Parameter validation */
	MALI_DEBUG_ASSERT_POINTER( lock );

	MALI_DEBUG_ASSERT( _MALI_OSK_LOCKMODE_RW == mode
					 || _MALI_OSK_LOCKMODE_RO == mode );

	/* Only allow RO locks when the initial object was a Reader/Writer lock
	 * Since information is lost on the internal locktype, we use the original
	 * information, which is only stored when built for DEBUG */
	MALI_DEBUG_ASSERT( _MALI_OSK_LOCKMODE_RW == mode
					 || (_MALI_OSK_LOCKMODE_RO == mode && (_MALI_OSK_LOCKFLAG_READERWRITER & lock->orig_flags)) );

	switch ( lock->type )
	{
	case _MALI_OSK_INTERNAL_LOCKTYPE_SPIN:
		spin_lock(&lock->obj.spinlock);
		break;
	case _MALI_OSK_INTERNAL_LOCKTYPE_SPIN_IRQ:
		spin_lock_irqsave(&lock->obj.spinlock, lock->flags);
		break;

	case _MALI_OSK_INTERNAL_LOCKTYPE_MUTEX:
		if ( down_interruptible(&lock->obj.sema) )
		{
			MALI_PRINT_ERROR(("Can not lock mutex\n"));
			err = _MALI_OSK_ERR_RESTARTSYSCALL;
		}
		break;

	case _MALI_OSK_INTERNAL_LOCKTYPE_MUTEX_NONINT:
		down(&lock->obj.sema);
		break;

	case _MALI_OSK_INTERNAL_LOCKTYPE_MUTEX_NONINT_RW:
		if (mode == _MALI_OSK_LOCKMODE_RO)
        {
            down_read(&lock->obj.rw_sema);
        }
        else
        {
            down_write(&lock->obj.rw_sema);
        }
		break;

	default:
		/* Reaching here indicates a programming error, so you will not get here
		 * on non-DEBUG builds */
		MALI_DEBUG_PRINT_ERROR( ("Invalid internal lock type: %.8X", lock->type ) );
		break;
	}

#ifdef DEBUG
	/* This thread is now the owner of this lock */
	if (_MALI_OSK_ERR_OK == err)
	{
		if (mode == _MALI_OSK_LOCKMODE_RW)
		{
			/*MALI_DEBUG_ASSERT(0 == lock->owner);*/
			if (0 != lock->owner)
			{
				printk(KERN_ERR "%d: ERROR: Lock %p already has owner %d\n", _mali_osk_get_tid(), lock, lock->owner);
				dump_stack();
			}
			lock->owner = _mali_osk_get_tid();
			lock->mode = mode;
			++lock->nOwners;
		}
		else /* mode == _MALI_OSK_LOCKMODE_RO */
		{
			lock->owner |= _mali_osk_get_tid();
			lock->mode = mode;
			++lock->nOwners;
		}
	}
#endif

    return err;
}

void _mali_osk_lock_signal( _mali_osk_lock_t *lock, _mali_osk_lock_mode_t mode )
{
	/* Parameter validation */
	MALI_DEBUG_ASSERT_POINTER( lock );

	MALI_DEBUG_ASSERT( _MALI_OSK_LOCKMODE_RW == mode
					 || _MALI_OSK_LOCKMODE_RO == mode );

	/* Only allow RO locks when the initial object was a Reader/Writer lock
	 * Since information is lost on the internal locktype, we use the original
	 * information, which is only stored when built for DEBUG */
	MALI_DEBUG_ASSERT( _MALI_OSK_LOCKMODE_RW == mode
					 || (_MALI_OSK_LOCKMODE_RO == mode && (_MALI_OSK_LOCKFLAG_READERWRITER & lock->orig_flags)) );

#ifdef DEBUG
	/* make sure the thread releasing the lock actually was the owner */
	if (mode == _MALI_OSK_LOCKMODE_RW)
	{
		/*MALI_DEBUG_ASSERT(_mali_osk_get_tid() == lock->owner);*/
		if (_mali_osk_get_tid() != lock->owner)
		{
			printk(KERN_ERR "%d: ERROR: Lock %p owner was %d\n", _mali_osk_get_tid(), lock, lock->owner);
			dump_stack();
		}
		/* This lock now has no owner */
		lock->owner = 0;
		--lock->nOwners;
	}
	else /* mode == _MALI_OSK_LOCKMODE_RO */
	{
		if ((_mali_osk_get_tid() & lock->owner) != _mali_osk_get_tid())
		{
			printk(KERN_ERR "%d: ERROR: Not an owner of %p lock.\n", _mali_osk_get_tid(), lock);
			dump_stack();
		}

		/* if this is the last thread holding this lock in R/O mode, set owner
		 * back to 0 */
		if (0 == --lock->nOwners)
		{
			lock->owner = 0;
		}
	}
#endif /* DEBUG */

	switch ( lock->type )
	{
	case _MALI_OSK_INTERNAL_LOCKTYPE_SPIN:
		spin_unlock(&lock->obj.spinlock);
		break;
	case _MALI_OSK_INTERNAL_LOCKTYPE_SPIN_IRQ:
		spin_unlock_irqrestore(&lock->obj.spinlock, lock->flags);
		break;

	case _MALI_OSK_INTERNAL_LOCKTYPE_MUTEX:
		/* FALLTHROUGH */
	case _MALI_OSK_INTERNAL_LOCKTYPE_MUTEX_NONINT:
		up(&lock->obj.sema);
		break;

	case _MALI_OSK_INTERNAL_LOCKTYPE_MUTEX_NONINT_RW:
		if (mode == _MALI_OSK_LOCKMODE_RO)
        {
            up_read(&lock->obj.rw_sema);
        }
        else
        {
            up_write(&lock->obj.rw_sema);
        }
		break;

	default:
		/* Reaching here indicates a programming error, so you will not get here
		 * on non-DEBUG builds */
		MALI_DEBUG_PRINT_ERROR( ("Invalid internal lock type: %.8X", lock->type ) );
		break;
	}
}

void _mali_osk_lock_term( _mali_osk_lock_t *lock )
{
	/* Parameter validation  */
	MALI_DEBUG_ASSERT_POINTER( lock );

	/* Linux requires no explicit termination of spinlocks, semaphores, or rw_semaphores */
    kfree(lock);
}
