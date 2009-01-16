/*
    comedi/rt.c
    comedi kernel module

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

#undef DEBUG

#define __NO_VERSION__
#include <linux/comedidev.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/io.h>

#include "rt_pend_tq.h"

#ifdef CONFIG_COMEDI_RTAI
#include <rtai.h>
#endif

#ifdef CONFIG_COMEDI_FUSION
#include <nucleus/asm/hal.h>
#endif

#ifdef CONFIG_COMEDI_RTL
#include <rtl_core.h>
#include <rtl_sync.h>
#endif

struct comedi_irq_struct {
	int rt;
	int irq;
	 irqreturn_t(*handler) (int irq, void *dev_id PT_REGS_ARG);
	unsigned long flags;
	const char *device;
	comedi_device *dev_id;
};

static int comedi_rt_get_irq(struct comedi_irq_struct *it);
static int comedi_rt_release_irq(struct comedi_irq_struct *it);

static struct comedi_irq_struct *comedi_irqs[NR_IRQS];

int comedi_request_irq(unsigned irq, irqreturn_t(*handler) (int,
		void *PT_REGS_ARG), unsigned long flags, const char *device,
	comedi_device * dev_id)
{
	struct comedi_irq_struct *it;
	int ret;
	/* null shared interrupt flag, since rt interrupt handlers do not
	 * support it, and this version of comedi_request_irq() is only
	 * called for kernels with rt support */
	unsigned long unshared_flags = flags & ~IRQF_SHARED;

	ret = request_irq(irq, handler, unshared_flags, device, dev_id);
	if (ret < 0) {
		// we failed, so fall back on allowing shared interrupt (which we won't ever make RT)
		if (flags & IRQF_SHARED) {
			rt_printk
				("comedi: cannot get unshared interrupt, will not use RT interrupts.\n");
			ret = request_irq(irq, handler, flags, device, dev_id);
		}
		if (ret < 0) {
			return ret;
		}
	} else {
		it = kzalloc(sizeof(struct comedi_irq_struct), GFP_KERNEL);
		if (!it)
			return -ENOMEM;

		it->handler = handler;
		it->irq = irq;
		it->dev_id = dev_id;
		it->device = device;
		it->flags = unshared_flags;
		comedi_irqs[irq] = it;
	}
	return 0;
}

void comedi_free_irq(unsigned int irq, comedi_device * dev_id)
{
	struct comedi_irq_struct *it;

	free_irq(irq, dev_id);

	it = comedi_irqs[irq];
	if (it == NULL)
		return;

	if (it->rt) {
		printk("real-time IRQ allocated at board removal (ignore)\n");
		comedi_rt_release_irq(it);
	}

	kfree(it);
	comedi_irqs[irq] = NULL;
}

int comedi_switch_to_rt(comedi_device * dev)
{
	struct comedi_irq_struct *it;
	unsigned long flags;

	it = comedi_irqs[dev->irq];
	/* drivers might not be using an interrupt for commands,
	   or we might not have been able to get an unshared irq */
	if (it == NULL)
		return -1;

	comedi_spin_lock_irqsave(&dev->spinlock, flags);

	if (!dev->rt)
		comedi_rt_get_irq(it);

	dev->rt++;
	it->rt = 1;

	comedi_spin_unlock_irqrestore(&dev->spinlock, flags);

	return 0;
}

void comedi_switch_to_non_rt(comedi_device * dev)
{
	struct comedi_irq_struct *it;
	unsigned long flags;

	it = comedi_irqs[dev->irq];
	if (it == NULL)
		return;

	comedi_spin_lock_irqsave(&dev->spinlock, flags);

	dev->rt--;
	if (!dev->rt)
		comedi_rt_release_irq(it);

	it->rt = 0;

	comedi_spin_unlock_irqrestore(&dev->spinlock, flags);
}

void wake_up_int_handler(int arg1, void *arg2)
{
	wake_up_interruptible((wait_queue_head_t *) arg2);
}

void comedi_rt_pend_wakeup(wait_queue_head_t * q)
{
	rt_pend_call(wake_up_int_handler, 0, q);
}

/* RTAI section */
#ifdef CONFIG_COMEDI_RTAI

#ifndef HAVE_RT_REQUEST_IRQ_WITH_ARG
#define DECLARE_VOID_IRQ(irq) \
static void handle_void_irq_ ## irq (void){ handle_void_irq(irq);}

static void handle_void_irq(int irq)
{
	struct comedi_irq_struct *it;

	it = comedi_irqs[irq];
	if (it == NULL) {
		rt_printk("comedi: null irq struct?\n");
		return;
	}
	it->handler(irq, it->dev_id PT_REGS_NULL);
	rt_enable_irq(irq);	//needed by rtai-adeos, seems like it shouldn't hurt earlier versions
}

DECLARE_VOID_IRQ(0);
DECLARE_VOID_IRQ(1);
DECLARE_VOID_IRQ(2);
DECLARE_VOID_IRQ(3);
DECLARE_VOID_IRQ(4);
DECLARE_VOID_IRQ(5);
DECLARE_VOID_IRQ(6);
DECLARE_VOID_IRQ(7);
DECLARE_VOID_IRQ(8);
DECLARE_VOID_IRQ(9);
DECLARE_VOID_IRQ(10);
DECLARE_VOID_IRQ(11);
DECLARE_VOID_IRQ(12);
DECLARE_VOID_IRQ(13);
DECLARE_VOID_IRQ(14);
DECLARE_VOID_IRQ(15);
DECLARE_VOID_IRQ(16);
DECLARE_VOID_IRQ(17);
DECLARE_VOID_IRQ(18);
DECLARE_VOID_IRQ(19);
DECLARE_VOID_IRQ(20);
DECLARE_VOID_IRQ(21);
DECLARE_VOID_IRQ(22);
DECLARE_VOID_IRQ(23);

typedef void (*V_FP_V) (void);
static V_FP_V handle_void_irq_ptrs[] = {
	handle_void_irq_0,
	handle_void_irq_1,
	handle_void_irq_2,
	handle_void_irq_3,
	handle_void_irq_4,
	handle_void_irq_5,
	handle_void_irq_6,
	handle_void_irq_7,
	handle_void_irq_8,
	handle_void_irq_9,
	handle_void_irq_10,
	handle_void_irq_11,
	handle_void_irq_12,
	handle_void_irq_13,
	handle_void_irq_14,
	handle_void_irq_15,
	handle_void_irq_16,
	handle_void_irq_17,
	handle_void_irq_18,
	handle_void_irq_19,
	handle_void_irq_20,
	handle_void_irq_21,
	handle_void_irq_22,
	handle_void_irq_23,
};

static int comedi_rt_get_irq(struct comedi_irq_struct *it)
{
	rt_request_global_irq(it->irq, handle_void_irq_ptrs[it->irq]);
	rt_startup_irq(it->irq);

	return 0;
}

static int comedi_rt_release_irq(struct comedi_irq_struct *it)
{
	rt_shutdown_irq(it->irq);
	rt_free_global_irq(it->irq);
	return 0;
}
#else

static int comedi_rt_get_irq(struct comedi_irq_struct *it)
{
	int ret;

	ret = rt_request_global_irq_arg(it->irq, it->handler, it->flags,
		it->device, it->dev_id);
	if (ret < 0) {
		rt_printk("rt_request_global_irq_arg() returned %d\n", ret);
		return ret;
	}
	rt_startup_irq(it->irq);

	return 0;
}

static int comedi_rt_release_irq(struct comedi_irq_struct *it)
{
	rt_shutdown_irq(it->irq);
	rt_free_global_irq(it->irq);
	return 0;
}
#endif

void comedi_rt_init(void)
{
	rt_mount_rtai();
	rt_pend_tq_init();
}

void comedi_rt_cleanup(void)
{
	rt_umount_rtai();
	rt_pend_tq_cleanup();
}

#endif

/* Fusion section */
#ifdef CONFIG_COMEDI_FUSION

static void fusion_handle_irq(unsigned int irq, void *cookie)
{
	struct comedi_irq_struct *it = cookie;

	it->handler(irq, it->dev_id PT_REGS_NULL);
	rthal_irq_enable(irq);
}

static int comedi_rt_get_irq(struct comedi_irq_struct *it)
{
	rthal_irq_request(it->irq, fusion_handle_irq, it);
	rthal_irq_enable(it->irq);
	return 0;
}

static int comedi_rt_release_irq(struct comedi_irq_struct *it)
{
	rthal_irq_disable(it->irq);
	rthal_irq_release(it->irq);
	return 0;
}

void comedi_rt_init(void)
{
	rt_pend_tq_init();
}

void comedi_rt_cleanup(void)
{
	rt_pend_tq_cleanup();
}

#endif /*CONFIG_COMEDI_FUSION */

/* RTLinux section */
#ifdef CONFIG_COMEDI_RTL

static unsigned int handle_rtl_irq(unsigned int irq PT_REGS_ARG)
{
	struct comedi_irq_struct *it;

	it = comedi_irqs[irq];
	if (it == NULL)
		return 0;
	it->handler(irq, it->dev_id PT_REGS_NULL);
	rtl_hard_enable_irq(irq);
	return 0;
}

static int comedi_rt_get_irq(struct comedi_irq_struct *it)
{
	rtl_request_global_irq(it->irq, handle_rtl_irq);
	return 0;
}

static int comedi_rt_release_irq(struct comedi_irq_struct *it)
{
	rtl_free_global_irq(it->irq);
	return 0;
}

void comedi_rt_init(void)
{
	rt_pend_tq_init();
}

void comedi_rt_cleanup(void)
{
	rt_pend_tq_cleanup();
}

#endif

#ifdef CONFIG_COMEDI_PIRQ
static int comedi_rt_get_irq(struct comedi_irq_struct *it)
{
	int ret;

	free_irq(it->irq, it->dev_id);
	ret = request_irq(it->irq, it->handler, it->flags | SA_PRIORITY,
		it->device, it->dev_id);

	return ret;
}

static int comedi_rt_release_irq(struct comedi_irq_struct *it)
{
	int ret;

	free_irq(it->irq, it->dev_id);
	ret = request_irq(it->irq, it->handler, it->flags,
		it->device, it->dev_id);

	return ret;
}

void comedi_rt_init(void)
{
	//rt_pend_tq_init();
}

void comedi_rt_cleanup(void)
{
	//rt_pend_tq_cleanup();
}
#endif
