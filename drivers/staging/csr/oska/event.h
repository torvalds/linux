/*
 * OSKA Linux implementation -- events
 *
 * Copyright (C) 2009 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_LINUX_EVENT_H
#define __OSKA_LINUX_EVENT_H

#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/spinlock.h>

typedef struct {
    wait_queue_head_t wq;
    spinlock_t lock;
    uint16_t events;
} os_event_t;

void os_event_init(os_event_t *evt);

static inline void os_event_destroy(os_event_t *evt)
{
}

uint16_t os_event_wait(os_event_t *evt);
uint16_t os_event_wait_interruptible(os_event_t *evt);
uint16_t os_event_wait_timed(os_event_t *evt, unsigned timeout_ms);
void os_event_raise(os_event_t *evt, uint16_t events);

#endif /* #ifndef __OSKA_LINUX_EVENT_H */
