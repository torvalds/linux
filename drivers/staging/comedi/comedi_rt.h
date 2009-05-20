/*
    module/comedi_rt.h
    header file for real-time structures, variables, and constants

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef _COMEDI_RT_H
#define _COMEDI_RT_H

#ifndef _COMEDIDEV_H
#error comedi_rt.h should only be included by comedidev.h
#endif

#include <linux/kdev_t.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/delay.h>

#ifdef CONFIG_COMEDI_RT

#ifdef CONFIG_COMEDI_RTAI
#include <rtai.h>
#include <rtai_sched.h>
#include <rtai_version.h>
#endif
#ifdef CONFIG_COMEDI_RTL
#include <rtl_core.h>
#include <rtl_time.h>
/* #ifdef RTLINUX_VERSION_CODE */
#include <rtl_sync.h>
/* #endif */
#define rt_printk rtl_printf
#endif
#ifdef CONFIG_COMEDI_FUSION
#define rt_printk(format, args...) printk(format , ## args)
#endif /* CONFIG_COMEDI_FUSION */
#ifdef CONFIG_PRIORITY_IRQ
#define rt_printk printk
#endif

int comedi_request_irq(unsigned int irq, irqreturn_t(*handler) (int,
		void *PT_REGS_ARG), unsigned long flags, const char *device,
		struct comedi_device *dev_id);
void comedi_free_irq(unsigned int irq, struct comedi_device *dev_id);
void comedi_rt_init(void);
void comedi_rt_cleanup(void);
int comedi_switch_to_rt(struct comedi_device *dev);
void comedi_switch_to_non_rt(struct comedi_device *dev);
void comedi_rt_pend_wakeup(wait_queue_head_t *q);
extern int rt_pend_call(void (*func) (int arg1, void *arg2), int arg1,
	void *arg2);

#else

#define comedi_request_irq(a, b, c, d, e) request_irq(a, b, c, d, e)
#define comedi_free_irq(a, b) free_irq(a, b)
#define comedi_rt_init() do {} while (0)
#define comedi_rt_cleanup() do {} while (0)
#define comedi_switch_to_rt(a) (-1)
#define comedi_switch_to_non_rt(a) do {} while (0)
#define comedi_rt_pend_wakeup(a) do {} while (0)

#define rt_printk(format, args...)	printk(format, ##args)

#endif

/* Define a spin_lock_irqsave function that will work with rt or without.
 * Use inline functions instead of just macros to enforce some type checking.
 */
#define comedi_spin_lock_irqsave(lock_ptr, flags) \
	(flags = __comedi_spin_lock_irqsave(lock_ptr))

static inline unsigned long __comedi_spin_lock_irqsave(spinlock_t *lock_ptr)
{
	unsigned long flags;

#if defined(CONFIG_COMEDI_RTAI)
	flags = rt_spin_lock_irqsave(lock_ptr);

#elif defined(CONFIG_COMEDI_RTL)
	rtl_spin_lock_irqsave(lock_ptr, flags);

#elif defined(CONFIG_COMEDI_RTL_V1)
	rtl_spin_lock_irqsave(lock_ptr, flags);

#elif defined(CONFIG_COMEDI_FUSION)
	rthal_spin_lock_irqsave(lock_ptr, flags);
#else
	spin_lock_irqsave(lock_ptr, flags);

#endif

	return flags;
}

static inline void comedi_spin_unlock_irqrestore(spinlock_t *lock_ptr,
	unsigned long flags)
{

#if defined(CONFIG_COMEDI_RTAI)
	rt_spin_unlock_irqrestore(flags, lock_ptr);

#elif defined(CONFIG_COMEDI_RTL)
	rtl_spin_unlock_irqrestore(lock_ptr, flags);

#elif defined(CONFIG_COMEDI_RTL_V1)
	rtl_spin_unlock_irqrestore(lock_ptr, flags);
#elif defined(CONFIG_COMEDI_FUSION)
	rthal_spin_unlock_irqrestore(lock_ptr, flags);
#else
	spin_unlock_irqrestore(lock_ptr, flags);

#endif

}

/* define a RT safe udelay */
static inline void comedi_udelay(unsigned int usec)
{
#if defined(CONFIG_COMEDI_RTAI)
	static const int nanosec_per_usec = 1000;
	rt_busy_sleep(usec * nanosec_per_usec);
#elif defined(CONFIG_COMEDI_RTL)
	static const int nanosec_per_usec = 1000;
	rtl_delay(usec * nanosec_per_usec);
#else
	udelay(usec);
#endif
}

#endif
