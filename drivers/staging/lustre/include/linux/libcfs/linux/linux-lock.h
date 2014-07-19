/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/linux/linux-lock.h
 *
 * Basic library routines.
 */

#ifndef __LIBCFS_LINUX_CFS_LOCK_H__
#define __LIBCFS_LINUX_CFS_LOCK_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <linux/libcfs/libcfs.h> instead
#endif


#include <linux/mutex.h>

/*
 * IMPORTANT !!!!!!!!
 *
 * All locks' declaration are not guaranteed to be initialized,
 * although some of them are initialized in Linux. All locks
 * declared by CFS_DECL_* should be initialized explicitly.
 */

/*
 * spin_lock "implementation" (use Linux kernel's primitives)
 *
 * - spin_lock_init(x)
 * - spin_lock(x)
 * - spin_lock_bh(x)
 * - spin_lock_bh_init(x)
 * - spin_unlock(x)
 * - spin_unlock_bh(x)
 * - spin_trylock(x)
 * - assert_spin_locked(x)
 *
 * - spin_lock_irq(x)
 * - spin_lock_irqsave(x, f)
 * - spin_unlock_irqrestore(x, f)
 * - read_lock_irqsave(lock, f)
 * - write_lock_irqsave(lock, f)
 * - write_unlock_irqrestore(lock, f)
 */

/*
 * spinlock "implementation"
 */




/*
 * rw_semaphore "implementation" (use Linux kernel's primitives)
 *
 * - sema_init(x)
 * - init_rwsem(x)
 * - down_read(x)
 * - up_read(x)
 * - down_write(x)
 * - up_write(x)
 */


#define fini_rwsem(s)		do {} while (0)


/*
 * rwlock_t "implementation" (use Linux kernel's primitives)
 *
 * - rwlock_init(x)
 * - read_lock(x)
 * - read_unlock(x)
 * - write_lock(x)
 * - write_unlock(x)
 * - write_lock_bh(x)
 * - write_unlock_bh(x)
 *
 * - RW_LOCK_UNLOCKED
 */


#ifndef DEFINE_RWLOCK
#define DEFINE_RWLOCK(lock)	rwlock_t lock = __RW_LOCK_UNLOCKED(lock)
#endif

/*
 * completion "implementation" (use Linux kernel's primitives)
 *
 * - DECLARE_COMPLETION(work)
 * - INIT_COMPLETION(c)
 * - COMPLETION_INITIALIZER(work)
 * - init_completion(c)
 * - complete(c)
 * - wait_for_completion(c)
 * - wait_for_completion_interruptible(c)
 * - fini_completion(c)
 */
#define fini_completion(c) do { } while (0)

/*
 * semaphore "implementation" (use Linux kernel's primitives)
 * - DEFINE_SEMAPHORE(name)
 * - sema_init(sem, val)
 * - up(sem)
 * - down(sem)
 * - down_interruptible(sem)
 * - down_trylock(sem)
 */

/*
 * mutex "implementation" (use Linux kernel's primitives)
 *
 * - DEFINE_MUTEX(name)
 * - mutex_init(x)
 * - mutex_lock(x)
 * - mutex_unlock(x)
 * - mutex_trylock(x)
 * - mutex_is_locked(x)
 * - mutex_destroy(x)
 */

#ifndef lockdep_set_class

/**************************************************************************
 *
 * Lockdep "implementation". Also see liblustre.h
 *
 **************************************************************************/

struct lock_class_key {
	;
};

#define lockdep_set_class(lock, key) \
	do { (void)sizeof(lock); (void)sizeof(key); } while (0)
/* This has to be a macro, so that `subclass' can be undefined in kernels
 * that do not support lockdep. */


static inline void lockdep_off(void)
{
}

static inline void lockdep_on(void)
{
}
#else

#endif /* lockdep_set_class */

#ifndef CONFIG_DEBUG_LOCK_ALLOC
#ifndef mutex_lock_nested
#define mutex_lock_nested(mutex, subclass) mutex_lock(mutex)
#endif

#ifndef spin_lock_nested
#define spin_lock_nested(lock, subclass) spin_lock(lock)
#endif

#ifndef down_read_nested
#define down_read_nested(lock, subclass) down_read(lock)
#endif

#ifndef down_write_nested
#define down_write_nested(lock, subclass) down_write(lock)
#endif
#endif /* CONFIG_DEBUG_LOCK_ALLOC */


#endif /* __LIBCFS_LINUX_CFS_LOCK_H__ */
