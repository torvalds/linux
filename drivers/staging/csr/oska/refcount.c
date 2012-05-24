/*
 * OSKA generic implementation -- reference counting.
 *
 * Copyright (C) 2010 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#include "refcount.h"
#include "types.h"

void os_refcount_init(os_refcount_t *refcount, os_refcount_callback_f func, void *arg)
{
    os_spinlock_init(&refcount->lock);
    refcount->count = 1;
    refcount->func  = func;
    refcount->arg   = arg;
}

void os_refcount_destroy(os_refcount_t *refcount)
{
    os_spinlock_destroy(&refcount->lock);
}

void os_refcount_get(os_refcount_t *refcount)
{
    os_int_status_t istate;

    os_spinlock_lock_intsave(&refcount->lock, &istate);
    refcount->count++;
    os_spinlock_unlock_intrestore(&refcount->lock, &istate);
}

void os_refcount_put(os_refcount_t *refcount)
{
    bool is_zero;
    os_int_status_t istate;

    os_spinlock_lock_intsave(&refcount->lock, &istate);
    refcount->count--;
    is_zero = refcount->count == 0;
    os_spinlock_unlock_intrestore(&refcount->lock, &istate);

    if (is_zero) {
        refcount->func(refcount->arg);
    }
}
