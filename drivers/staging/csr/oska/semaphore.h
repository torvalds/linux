/*
 * OSKA Linux implementation -- semaphores
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_LINUX_SEMAPHORE_H
#define __OSKA_LINUX_SEMAPHORE_H

#include <linux/kernel.h>

#include <linux/kernel-compat.h>

typedef struct semaphore os_semaphore_t;

static inline void os_semaphore_init(os_semaphore_t *sem)
{
    sema_init(sem, 0);
}

static inline void os_semaphore_destroy(os_semaphore_t *sem)
{
}

static inline void os_semaphore_wait(os_semaphore_t *sem)
{
    down(sem);
}

/*
 * down_timeout() was added in 2.6.26 with the generic semaphore
 * implementation.  For now, only support it on recent kernels as
 * semaphores may be replaced by an event API that would be
 * implemented with wait_event(), and wait_event_timeout().
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)

static inline int os_semaphore_wait_timed(os_semaphore_t *sem,
                                           int time_ms)
{
    if (down_timeout(sem, msecs_to_jiffies(time_ms)) < 0) {
        return -ETIMEDOUT;
    }
    return 0;
}

#else

static inline int os_semaphore_wait_timed(os_semaphore_t *sem, int time_ms)
{
	unsigned long now = jiffies;
	do{
		if(!down_trylock(sem))
			return 0;
		msleep(1);
	} while(time_before(jiffies, now + msecs_to_jiffies(time_ms)));

	return -ETIMEDOUT;
}

#endif

static inline void os_semaphore_post(os_semaphore_t *sem)
{
    up(sem);
}

#endif /* __OSKA_LINUX_SEMAPHORE_H */
