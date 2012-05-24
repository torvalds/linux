/*
 * OSKA Linux implementation -- timing
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_LINUX_TIME_H
#define __OSKA_LINUX_TIME_H

#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

static inline unsigned long os_current_time_ms(void)
{
    return jiffies_to_msecs(jiffies);
}

static inline void os_sleep_ms(unsigned ms)
{
    msleep_interruptible(ms);
}

static inline void os_delay_us(unsigned us)
{
    udelay(us);
}

#define os_time_after(a, b) time_after((a), (b))

#endif /* __OSKA_LINUX_TIME_H */
