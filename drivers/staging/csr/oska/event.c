/*
 * Linux event functions.
 *
 * Copyright (C) 2009 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#include <linux/module.h>
#include <linux/sched.h>

#include "event.h"

void os_event_init(os_event_t *evt)
{
    init_waitqueue_head(&evt->wq);
    spin_lock_init(&evt->lock);
    evt->events = 0;
}
EXPORT_SYMBOL(os_event_init);

uint16_t os_event_wait(os_event_t *evt)
{
    uint16_t e;
    unsigned long flags;

    wait_event(evt->wq, evt->events != 0);

    spin_lock_irqsave(&evt->lock, flags);
    e = evt->events;
    evt->events &= ~e;
    spin_unlock_irqrestore(&evt->lock, flags);

    return e;
}
EXPORT_SYMBOL(os_event_wait);

uint16_t os_event_wait_interruptible(os_event_t *evt)
{
    uint16_t e;
    unsigned long flags;

    wait_event_interruptible(evt->wq, evt->events != 0);

    spin_lock_irqsave(&evt->lock, flags);
    e = evt->events;
    evt->events &= ~e;
    spin_unlock_irqrestore(&evt->lock, flags);

    return e;
}
EXPORT_SYMBOL(os_event_wait_interruptible);

uint16_t os_event_wait_timed(os_event_t *evt, unsigned timeout_ms)
{
    uint16_t e;
    unsigned long flags;

    wait_event_interruptible_timeout(evt->wq,
                                     evt->events != 0,
                                     msecs_to_jiffies(timeout_ms));

    spin_lock_irqsave(&evt->lock, flags);
    e = evt->events;
    evt->events &= ~e;
    spin_unlock_irqrestore(&evt->lock, flags);

    return e;
}
EXPORT_SYMBOL(os_event_wait_timed);

void os_event_raise(os_event_t *evt, uint16_t events)
{
    unsigned long flags;

    spin_lock_irqsave(&evt->lock, flags);
    evt->events |= events;
    spin_unlock_irqrestore(&evt->lock, flags);

    wake_up(&evt->wq);
}
EXPORT_SYMBOL(os_event_raise);
