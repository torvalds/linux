/*
 * Linux thread functions.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#include <linux/module.h>

#include "thread.h"

static int thread_func(void *data)
{
    os_thread_t *thread = data;

    thread->func(thread->arg);

    /*
     * kthread_stop() cannot handle the thread exiting while
     * kthread_should_stop() is false, so sleep until kthread_stop()
     * wakes us up.
     */
    set_current_state(TASK_INTERRUPTIBLE);
    if (!kthread_should_stop())
        schedule();

    return 0;
}

int os_thread_create(os_thread_t *thread, const char *name, void (*func)(void *), void *arg)
{
    thread->func = func;
    thread->arg = arg;

    thread->stop = 0;

    thread->task = kthread_run(thread_func, thread, name);
    if (IS_ERR(thread->task)) {
        return PTR_ERR(thread->task);
    }
    return 0;
}
EXPORT_SYMBOL(os_thread_create);

void os_thread_stop(os_thread_t *thread, os_event_t *evt)
{
    /*
     * Stop flag must be set before the event is raised so
     * kthread_should_stop() cannot be used.
     */
    thread->stop = 1;

    if (evt) {
        os_event_raise(evt, ~0);
    }

    kthread_stop(thread->task);
}
EXPORT_SYMBOL(os_thread_stop);

int os_thread_should_stop(os_thread_t *thread)
{
    return thread->stop;
}
EXPORT_SYMBOL(os_thread_should_stop);
