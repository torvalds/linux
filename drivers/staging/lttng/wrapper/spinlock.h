#ifndef _LTTNG_WRAPPER_SPINLOCK_H
#define _LTTNG_WRAPPER_SPINLOCK_H

/*
 * wrapper/spinlock.h
 *
 * Copyright (C) 2011-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))

#include <linux/string.h>

#define raw_spin_lock_init(lock)					\
	do {								\
		raw_spinlock_t __lock = __RAW_SPIN_LOCK_UNLOCKED;	\
		memcpy(lock, &__lock, sizeof(lock));			\
	} while (0)

#define raw_spin_is_locked(lock)	__raw_spin_is_locked(lock)

#define wrapper_desc_spin_lock(lock)	spin_lock(lock)
#define wrapper_desc_spin_unlock(lock)	spin_unlock(lock)

#else

#define wrapper_desc_spin_lock(lock)	raw_spin_lock(lock)
#define wrapper_desc_spin_unlock(lock)	raw_spin_unlock(lock)

#endif
#endif /* _LTTNG_WRAPPER_SPINLOCK_H */
