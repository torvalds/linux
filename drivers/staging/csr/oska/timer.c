/*
 * OSKA Linux implementation -- timers.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#include <linux/module.h>

#include "timer.h"

static void timer_func(unsigned long data)
{
    os_timer_t *timer = (os_timer_t *)data;

    timer->func(timer->arg);
}

void os_timer_init(os_timer_t *timer, os_timer_func_t func, void *arg)
{
    timer->func = func;
    timer->arg = arg;
    timer->timer.function = timer_func;
    timer->timer.data = (unsigned long)timer;
    init_timer(&timer->timer);
}
EXPORT_SYMBOL(os_timer_init);
