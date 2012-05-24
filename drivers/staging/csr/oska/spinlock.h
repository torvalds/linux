/*
 * OSKA Linux implementation -- spinlocks
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_LINUX_SPINLOCK_H
#define __OSKA_LINUX_SPINLOCK_H

#include <linux/kernel.h>
#include <linux/spinlock.h>

typedef spinlock_t os_spinlock_t;
typedef unsigned long os_int_status_t;

static inline void os_spinlock_init(os_spinlock_t *lock)
{
    spinlock_t *l = (spinlock_t *)lock;
    spin_lock_init(l);
}

static inline void os_spinlock_destroy(os_spinlock_t *lock)
{
    /* no op */
}

static inline void os_spinlock_lock_intsave(os_spinlock_t *lock,
					    os_int_status_t *int_state)
{
    spinlock_t *l = (spinlock_t *)lock;
    spin_lock_irqsave(l, *int_state);
}

static inline void os_spinlock_unlock_intrestore(os_spinlock_t *lock,
						 os_int_status_t *int_state)
{
    spinlock_t *l = (spinlock_t *)lock;
    spin_unlock_irqrestore(l, *int_state);
}

#endif /* #ifndef __OSKA_LINUX_SPINLOCK_H */
