/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_irq.c
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#include <linux/slab.h>	/* For memory allocation */

#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "linux/interrupt.h"

typedef struct _mali_osk_irq_t_struct
{
	u32 irqnum;
	void *data;
	_mali_osk_irq_uhandler_t uhandler;
} mali_osk_irq_object_t;

typedef irqreturn_t (*irq_handler_func_t)(int, void *, struct pt_regs *);
static irqreturn_t irq_handler_upper_half (int port_name, void* dev_id ); /* , struct pt_regs *regs*/

_mali_osk_irq_t *_mali_osk_irq_init( u32 irqnum, _mali_osk_irq_uhandler_t uhandler, void *int_data, _mali_osk_irq_trigger_t trigger_func, _mali_osk_irq_ack_t ack_func, void *probe_data, const char *description )
{
	mali_osk_irq_object_t *irq_object;

	irq_object = kmalloc(sizeof(mali_osk_irq_object_t), GFP_KERNEL);
	if (NULL == irq_object)
	{
		return NULL;
	}

	if (-1 == irqnum)
	{
		/* Probe for IRQ */
		if ( (NULL != trigger_func) && (NULL != ack_func) )
		{
			unsigned long probe_count = 3;
			_mali_osk_errcode_t err;
			int irq;

			MALI_DEBUG_PRINT(2, ("Probing for irq\n"));

			do
			{
				unsigned long mask;

				mask = probe_irq_on();
				trigger_func(probe_data);

				_mali_osk_time_ubusydelay(5);

				irq = probe_irq_off(mask);
				err = ack_func(probe_data);
			}
			while (irq < 0 && (err == _MALI_OSK_ERR_OK) && probe_count--);

			if (irq < 0 || (_MALI_OSK_ERR_OK != err)) irqnum = -1;
			else irqnum = irq;
		}
		else irqnum = -1; /* no probe functions, fault */

		if (-1 != irqnum)
		{
			/* found an irq */
			MALI_DEBUG_PRINT(2, ("Found irq %d\n", irqnum));
		}
		else
		{
			MALI_DEBUG_PRINT(2, ("Probe for irq failed\n"));
		}
	}

	irq_object->irqnum = irqnum;
	irq_object->uhandler = uhandler;
	irq_object->data = int_data;

	if (-1 == irqnum)
	{
		MALI_DEBUG_PRINT(2, ("No IRQ for core '%s' found during probe\n", description));
		kfree(irq_object);
		return NULL;
	}

	if (0 != request_irq(irqnum, irq_handler_upper_half, IRQF_SHARED, description, irq_object))
	{
		MALI_DEBUG_PRINT(2, ("Unable to install IRQ handler for core '%s'\n", description));
		kfree(irq_object);
		return NULL;
	}

	return irq_object;
}

void _mali_osk_irq_term( _mali_osk_irq_t *irq )
{
	mali_osk_irq_object_t *irq_object = (mali_osk_irq_object_t *)irq;
	free_irq(irq_object->irqnum, irq_object);
	kfree(irq_object);
}


/** This function is called directly in interrupt context from the OS just after
 * the CPU get the hw-irq from mali, or other devices on the same IRQ-channel.
 * It is registered one of these function for each mali core. When an interrupt
 * arrives this function will be called equal times as registered mali cores.
 * That means that we only check one mali core in one function call, and the
 * core we check for each turn is given by the \a dev_id variable.
 * If we detect an pending interrupt on the given core, we mask the interrupt
 * out by settging the core's IRQ_MASK register to zero.
 * Then we schedule the mali_core_irq_handler_bottom_half to run as high priority
 * work queue job.
 */
static irqreturn_t irq_handler_upper_half (int port_name, void* dev_id ) /* , struct pt_regs *regs*/
{
	mali_osk_irq_object_t *irq_object = (mali_osk_irq_object_t *)dev_id;

	if (irq_object->uhandler(irq_object->data) == _MALI_OSK_ERR_OK)
	{
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

