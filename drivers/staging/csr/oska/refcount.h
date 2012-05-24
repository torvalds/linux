/*
 * Operating system kernel abstraction -- reference counting.
 *
 * Copyright (C) 2010 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_REFCOUNT_H
#define __OSKA_REFCOUNT_H

#include "spinlock.h"

/**
 * @defgroup refcount Reference Counting
 *
 * A reference count is an atomic counter.  A callback function is
 * called whenever the count reaches zero.
 *
 * A generic implementation is provided that is suitable for all
 * platforms that support the spinlock API in <oska/spinlock.h> (see
 * \ref spinlock).
 */

typedef void (*os_refcount_callback_f)(void *arg);

struct __os_refcount_impl {
    unsigned count;
    os_spinlock_t lock;
    os_refcount_callback_f func;
    void *arg;
};

/**
 * A reference count object.
 *
 * @ingroup refcount
 */
typedef struct __os_refcount_impl os_refcount_t;

/**
 * Initialize a reference count to 1.
 *
 * Initialized reference counts must be destroyed by calling
 * os_refcount_destroy().
 *
 * @param refcount the reference count.
 * @param func     the function which will be called when the
 *                 reference count reaches 0.
 * @param arg      an argument to pass to func.
 *
 * @ingroup refcount
 */
void os_refcount_init(os_refcount_t *refcount, os_refcount_callback_f func, void *arg);

/**
 * Destroy a reference count object.
 *
 * @param refcount the reference count.
 *
 * @ingroup refcount
 */
void os_refcount_destroy(os_refcount_t *refcount);

/**
 * Atomically increase the reference count by 1.
 *
 * @param refcount the reference count.
 *
 * @ingroup refcount
 */
void os_refcount_get(os_refcount_t *refcount);

/**
 * Atomically decrease the reference count by 1.
 *
 * The callback function passed to the call to os_refcount_init() is
 * called if the count was decreased to zero.
 *
 * @param refcount the reference count.
 *
 * @ingroup refcount
 */
void os_refcount_put(os_refcount_t *refcount);

#endif /* #ifndef __OSKA_REFCOUNT_H */
