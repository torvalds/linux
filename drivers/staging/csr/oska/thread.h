/*
 * OSKA Linux implementation -- threading
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_LINUX_THREAD_H
#define __OSKA_LINUX_THREAD_H

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19)
#include <linux/freezer.h>
#endif
#include "event.h"

struct os_thread_lx {
    void (*func)(void *);
    void *arg;
    struct task_struct *task;
    int stop;
};

typedef struct os_thread_lx os_thread_t;

int os_thread_create(os_thread_t *thread, const char *name,
                     void (*func)(void *), void *arg);
void os_thread_stop(os_thread_t *thread, os_event_t *evt);
int os_thread_should_stop(os_thread_t *thread);

static inline void os_try_suspend_thread(os_thread_t *thread)
{
    try_to_freeze();
}

#endif /* __OSKA_LINUX_THREAD_H */
