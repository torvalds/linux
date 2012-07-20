/*
 * OSKA Linux implementation -- mutexes
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_LINUX_MUTEX_H
#define __OSKA_LINUX_MUTEX_H

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>


/* Real mutexes were only added to 2.6.16 so use semaphores
   instead. */
typedef struct semaphore os_mutex_t;

static inline void os_mutex_init(os_mutex_t *mutex)
{
    //init_MUTEX(mutex);
    sema_init(mutex, 1);
}

static inline void os_mutex_destroy(os_mutex_t *mutex)
{
    /* no op */
}

static inline void os_mutex_lock(os_mutex_t *mutex)
{
    down(mutex);
}

static inline void os_mutex_unlock(os_mutex_t *mutex)
{
    up(mutex);
}

#endif /* __OSKA_LINUX_MUTEX_H */
