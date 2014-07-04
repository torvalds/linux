/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file mali_osk_locks.h
 * Defines OS abstraction of lock and mutex
 */
#ifndef _MALI_OSK_LOCKS_H
#define _MALI_OSK_LOCKS_H

#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>

#include <linux/slab.h>

#include "mali_osk_types.h"

#ifdef _cplusplus
extern "C" {
#endif

	/* When DEBUG is enabled, this struct will be used to track owner, mode and order checking */
#ifdef DEBUG
	struct _mali_osk_lock_debug_s {
		u32 owner;
		_mali_osk_lock_flags_t orig_flags;
		_mali_osk_lock_order_t order;
		struct _mali_osk_lock_debug_s *next;
	};
#endif

	/* Anstraction of spinlock_t */
	struct _mali_osk_spinlock_s {
#ifdef DEBUG
		struct _mali_osk_lock_debug_s checker;
#endif
		spinlock_t spinlock;
	};

	/* Abstration of spinlock_t and lock flag which is used to store register's state before locking */
	struct _mali_osk_spinlock_irq_s {
#ifdef DEBUG
		struct _mali_osk_lock_debug_s checker;
#endif

		spinlock_t spinlock;
		unsigned long flags;
	};

	/* Abstraction of rw_semaphore in OS */
	struct _mali_osk_mutex_rw_s {
#ifdef DEBUG
		struct _mali_osk_lock_debug_s checker;
		_mali_osk_lock_mode_t mode;
#endif

		struct rw_semaphore rw_sema;
	};

	/* Mutex and mutex_interruptible functions share the same osk mutex struct */
	struct _mali_osk_mutex_s {
#ifdef DEBUG
		struct _mali_osk_lock_debug_s checker;
#endif
		struct mutex mutex;
	};

#ifdef DEBUG
	/** @brief _mali_osk_locks_debug_init/add/remove() functions are declared when DEBUG is enabled and
	 * defined in file mali_osk_locks.c. When LOCK_ORDER_CHECKING is enabled, calling these functions when we
	 * init/lock/unlock a lock/mutex, we could track lock order of a given tid. */
	void _mali_osk_locks_debug_init(struct _mali_osk_lock_debug_s *checker, _mali_osk_lock_flags_t flags, _mali_osk_lock_order_t order);
	void _mali_osk_locks_debug_add(struct _mali_osk_lock_debug_s *checker);
	void _mali_osk_locks_debug_remove(struct _mali_osk_lock_debug_s *checker);

	/** @brief This function can return a given lock's owner when DEBUG	is enabled. */
	static inline u32 _mali_osk_lock_get_owner(struct _mali_osk_lock_debug_s *lock)
	{
		return lock->owner;
	}
#else
#define _mali_osk_locks_debug_init(x, y, z) do {} while (0)
#define _mali_osk_locks_debug_add(x) do {} while (0)
#define _mali_osk_locks_debug_remove(x) do {} while (0)
#endif

	/** @brief Before use _mali_osk_spin_lock, init function should be used to allocate memory and initial spinlock*/
	static inline _mali_osk_spinlock_t *_mali_osk_spinlock_init(_mali_osk_lock_flags_t flags, _mali_osk_lock_order_t order)
	{
		_mali_osk_spinlock_t *lock = NULL;

		lock = kmalloc(sizeof(_mali_osk_spinlock_t), GFP_KERNEL);
		if (NULL == lock) {
			return NULL;
		}
		spin_lock_init(&lock->spinlock);
		_mali_osk_locks_debug_init((struct _mali_osk_lock_debug_s *)lock, flags, order);
		return lock;
	}

	/** @brief Lock a spinlock */
	static inline void  _mali_osk_spinlock_lock(_mali_osk_spinlock_t *lock)
	{
		BUG_ON(NULL == lock);
		spin_lock(&lock->spinlock);
		_mali_osk_locks_debug_add((struct _mali_osk_lock_debug_s *)lock);
	}

	/** @brief Unlock a spinlock */
	static inline void _mali_osk_spinlock_unlock(_mali_osk_spinlock_t *lock)
	{
		BUG_ON(NULL == lock);
		_mali_osk_locks_debug_remove((struct _mali_osk_lock_debug_s *)lock);
		spin_unlock(&lock->spinlock);
	}

	/** @brief Free a memory block which the argument lock pointed to and its type must be
	 * _mali_osk_spinlock_t *. */
	static inline void _mali_osk_spinlock_term(_mali_osk_spinlock_t *lock)
	{
		/* Parameter validation  */
		BUG_ON(NULL == lock);

		/* Linux requires no explicit termination of spinlocks, semaphores, or rw_semaphores */
		kfree(lock);
	}

	/** @brief Before _mali_osk_spinlock_irq_lock/unlock/term() is called, init function should be
	 * called to initial spinlock and flags in struct _mali_osk_spinlock_irq_t. */
	static inline _mali_osk_spinlock_irq_t *_mali_osk_spinlock_irq_init(_mali_osk_lock_flags_t flags, _mali_osk_lock_order_t order)
	{
		_mali_osk_spinlock_irq_t *lock = NULL;
		lock = kmalloc(sizeof(_mali_osk_spinlock_irq_t), GFP_KERNEL);

		if (NULL == lock) {
			return NULL;
		}

		lock->flags = 0;
		spin_lock_init(&lock->spinlock);
		_mali_osk_locks_debug_init((struct _mali_osk_lock_debug_s *)lock, flags, order);
		return lock;
	}

	/** @brief Lock spinlock and save the register's state */
	static inline void _mali_osk_spinlock_irq_lock(_mali_osk_spinlock_irq_t *lock)
	{
		unsigned long tmp_flags;

		BUG_ON(NULL == lock);
		spin_lock_irqsave(&lock->spinlock, tmp_flags);
		lock->flags = tmp_flags;
		_mali_osk_locks_debug_add((struct _mali_osk_lock_debug_s *)lock);
	}

	/** @brief Unlock spinlock with saved register's state */
	static inline void _mali_osk_spinlock_irq_unlock(_mali_osk_spinlock_irq_t *lock)
	{
		BUG_ON(NULL == lock);
		_mali_osk_locks_debug_remove((struct _mali_osk_lock_debug_s *)lock);
		spin_unlock_irqrestore(&lock->spinlock, lock->flags);
	}

	/** @brief Destroy a given memory block which lock pointed to, and the lock type must be
	 * _mali_osk_spinlock_irq_t *. */
	static inline void _mali_osk_spinlock_irq_term(_mali_osk_spinlock_irq_t *lock)
	{
		/* Parameter validation  */
		BUG_ON(NULL == lock);

		/* Linux requires no explicit termination of spinlocks, semaphores, or rw_semaphores */
		kfree(lock);
	}

	/** @brief Before _mali_osk_mutex_rw_wait/signal/term() is called, we should call
	 * _mali_osk_mutex_rw_init() to kmalloc a memory block and initial part of elements in it. */
	static inline _mali_osk_mutex_rw_t *_mali_osk_mutex_rw_init(_mali_osk_lock_flags_t flags, _mali_osk_lock_order_t order)
	{
		_mali_osk_mutex_rw_t *lock = NULL;

		lock = kmalloc(sizeof(_mali_osk_mutex_rw_t), GFP_KERNEL);

		if (NULL == lock) {
			return NULL;
		}

		init_rwsem(&lock->rw_sema);
		_mali_osk_locks_debug_init((struct _mali_osk_lock_debug_s *)lock, flags, order);
		return lock;
	}

	/** @brief When call _mali_osk_mutex_rw_wait/signal() functions, the second argument mode
	 * should be assigned with value _MALI_OSK_LOCKMODE_RO or _MALI_OSK_LOCKMODE_RW */
	static inline void _mali_osk_mutex_rw_wait(_mali_osk_mutex_rw_t *lock, _mali_osk_lock_mode_t mode)
	{
		BUG_ON(NULL == lock);
		BUG_ON(!(_MALI_OSK_LOCKMODE_RO == mode || _MALI_OSK_LOCKMODE_RW == mode));

		if (mode == _MALI_OSK_LOCKMODE_RO) {
			down_read(&lock->rw_sema);
		} else {
			down_write(&lock->rw_sema);
		}

#ifdef DEBUG
		if (mode == _MALI_OSK_LOCKMODE_RW) {
			lock->mode = mode;
		} else { /* mode == _MALI_OSK_LOCKMODE_RO */
			lock->mode = mode;
		}
		_mali_osk_locks_debug_add((struct _mali_osk_lock_debug_s *)lock);
#endif
	}

	/** @brief Up lock->rw_sema with up_read/write() accordinf argument mode's value. */
	static inline void  _mali_osk_mutex_rw_signal(_mali_osk_mutex_rw_t *lock, _mali_osk_lock_mode_t mode)
	{
		BUG_ON(NULL == lock);
		BUG_ON(!(_MALI_OSK_LOCKMODE_RO == mode || _MALI_OSK_LOCKMODE_RW == mode));
#ifdef DEBUG
		/* make sure the thread releasing the lock actually was the owner */
		if (mode == _MALI_OSK_LOCKMODE_RW) {
			_mali_osk_locks_debug_remove((struct _mali_osk_lock_debug_s *)lock);
			/* This lock now has no owner */
			lock->checker.owner = 0;
		}
#endif

		if (mode == _MALI_OSK_LOCKMODE_RO) {
			up_read(&lock->rw_sema);
		} else {
			up_write(&lock->rw_sema);
		}
	}

	/** @brief Free a given memory block which lock pointed to and its type must be
	 * _mali_sok_mutex_rw_t *. */
	static inline void _mali_osk_mutex_rw_term(_mali_osk_mutex_rw_t *lock)
	{
		/* Parameter validation  */
		BUG_ON(NULL == lock);

		/* Linux requires no explicit termination of spinlocks, semaphores, or rw_semaphores */
		kfree(lock);
	}

	/** @brief Mutex & mutex_interruptible share the same init and term function, because they have the
	 * same osk mutex struct, and the difference between them is which locking function they use */
	static inline _mali_osk_mutex_t *_mali_osk_mutex_init(_mali_osk_lock_flags_t flags, _mali_osk_lock_order_t order)
	{
		_mali_osk_mutex_t *lock = NULL;

		lock = kmalloc(sizeof(_mali_osk_mutex_t), GFP_KERNEL);

		if (NULL == lock) {
			return NULL;
		}
		mutex_init(&lock->mutex);

		_mali_osk_locks_debug_init((struct _mali_osk_lock_debug_s *)lock, flags, order);
		return lock;
	}

	/** @brief  Lock the lock->mutex with mutex_lock_interruptible function */
	static inline _mali_osk_errcode_t _mali_osk_mutex_wait_interruptible(_mali_osk_mutex_t *lock)
	{
		_mali_osk_errcode_t err = _MALI_OSK_ERR_OK;

		BUG_ON(NULL == lock);

		if (mutex_lock_interruptible(&lock->mutex)) {
			printk(KERN_WARNING "Mali: Can not lock mutex\n");
			err = _MALI_OSK_ERR_RESTARTSYSCALL;
		}

		_mali_osk_locks_debug_add((struct _mali_osk_lock_debug_s *)lock);
		return err;
	}

	/** @brief Unlock the lock->mutex which is locked with mutex_lock_interruptible() function. */
	static inline void _mali_osk_mutex_signal_interruptible(_mali_osk_mutex_t *lock)
	{
		BUG_ON(NULL == lock);
		_mali_osk_locks_debug_remove((struct _mali_osk_lock_debug_s *)lock);
		mutex_unlock(&lock->mutex);
	}

	/** @brief Lock the lock->mutex just with mutex_lock() function which could not be interruptted. */
	static inline void _mali_osk_mutex_wait(_mali_osk_mutex_t *lock)
	{
		BUG_ON(NULL == lock);
		mutex_lock(&lock->mutex);
		_mali_osk_locks_debug_add((struct _mali_osk_lock_debug_s *)lock);
	}

	/** @brief Unlock the lock->mutex which is locked with mutex_lock() function. */
	static inline void _mali_osk_mutex_signal(_mali_osk_mutex_t *lock)
	{
		BUG_ON(NULL == lock);
		_mali_osk_locks_debug_remove((struct _mali_osk_lock_debug_s *)lock);
		mutex_unlock(&lock->mutex);
	}

	/** @brief Free a given memory block which lock point. */
	static inline void _mali_osk_mutex_term(_mali_osk_mutex_t *lock)
	{
		/* Parameter validation  */
		BUG_ON(NULL == lock);

		/* Linux requires no explicit termination of spinlocks, semaphores, or rw_semaphores */
		kfree(lock);
	}

#ifdef _cplusplus
}
#endif

#endif
