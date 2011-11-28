#ifndef _LTT_WRAPPER_SPINLOCK_H
#define _LTT_WRAPPER_SPINLOCK_H

/*
 * Copyright (C) 2011 Mathieu Desnoyers (mathieu.desnoyers@efficios.com)
 *
 * Dual LGPL v2.1/GPL v2 license.
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


#endif
#endif /* _LTT_WRAPPER_SPINLOCK_H */
