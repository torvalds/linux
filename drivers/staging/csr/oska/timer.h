/*
 * OSKA Linux implementation -- timers.
 *
 * Copyright (C) 2009 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_LINUX_TIMER_H
#define __OSKA_LINUX_TIMER_H

#include <linux/kernel.h>
#include <linux/timer.h>

typedef void (*os_timer_func_t)(void *arg);

typedef struct {
    os_timer_func_t func;
    void *arg;
    struct timer_list timer;
} os_timer_t;

void os_timer_init(os_timer_t *timer, os_timer_func_t func, void *arg);

static inline void os_timer_destroy(os_timer_t *timer)
{
    del_timer_sync(&timer->timer);
}

static inline void os_timer_set(os_timer_t *timer, unsigned long expires_ms)
{
    mod_timer(&timer->timer, jiffies + msecs_to_jiffies(expires_ms));
}

static inline void os_timer_cancel(os_timer_t *timer)
{
    del_timer(&timer->timer);
}

#endif /* #ifndef __OSKA_LINUX_TIMER_H */
