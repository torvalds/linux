/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file mali_osk_irq.c
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#include <linux/slab.h> /* For memory allocation */
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include "mali_osk.h"
#include "mali_kernel_common.h"

typedef struct _mali_osk_irq_t_struct {
	u32 irqnum;
	void *data;
	_mali_osk_irq_uhandler_t uhandler;
} mali_osk_irq_object_t;

typedef irqreturn_t (*irq_handler_func_t)(int, void *, struct pt_regs *);
static irqreturn_t irq_handler_upper_half(int port_name, void *dev_id);   /* , struct pt_regs *regs*/

#if defined(DEBUG)

struct test_interrupt_data {
	_mali_osk_irq_ack_t ack_func;
	void *probe_data;
	mali_bool interrupt_received;
	wait_queue_head_t wq;
};

static irqreturn_t test_interrupt_upper_half(int port_name, void *dev_id)
{
	irqreturn_t ret = IRQ_NONE;
	struct test_interrupt_data *data = (struct test_interrupt_data *)dev_id;

	if (_MALI_OSK_ERR_OK == data->ack_func(data->probe_data)) {
		data->interrupt_received = MALI_TRUE;
		wake_up(&data->wq);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static _mali_osk_errcode_t test_interrupt(u32 irqnum,
		_mali_osk_irq_trigger_t trigger_func,
		_mali_osk_irq_ack_t ack_func,
		void *probe_data,
		const char *description)
{
	unsigned long irq_flags = 0;
	struct test_interrupt_data data = {
		.ack_func = ack_func,
		.probe_data = probe_data,
		.interrupt_received = MALI_FALSE,
	};

#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	irq_flags |= IRQF_SHARED;
#endif /* defined(CONFIG_MALI_SHARED_INTERRUPTS) */

	if (0 != request_irq(irqnum, test_interrupt_upper_half, irq_flags, description, &data)) {
		MALI_DEBUG_PRINT(2, ("Unable to install test IRQ handler for core '%s'\n", description));
		return _MALI_OSK_ERR_FAULT;
	}

	init_waitqueue_head(&data.wq);

	trigger_func(probe_data);
	wait_event_timeout(data.wq, data.interrupt_received, 100);

	free_irq(irqnum, &data);

	if (data.interrupt_received) {
		MALI_DEBUG_PRINT(3, ("%s: Interrupt test OK\n", description));
		return _MALI_OSK_ERR_OK;
	} else {
		MALI_PRINT_ERROR(("%s: Failed interrupt test on %u\n", description, irqnum));
		return _MALI_OSK_ERR_FAULT;
	}
}

#endif /* defined(DEBUG) */

_mali_osk_irq_t *_mali_osk_irq_init(u32 irqnum, _mali_osk_irq_uhandler_t uhandler, void *int_data, _mali_osk_irq_trigger_t trigger_func, _mali_osk_irq_ack_t ack_func, void *probe_data, const char *description)
{
	mali_osk_irq_object_t *irq_object;
	unsigned long irq_flags = 0;

#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	irq_flags |= IRQF_SHARED;
#endif /* defined(CONFIG_MALI_SHARED_INTERRUPTS) */

	irq_object = kmalloc(sizeof(mali_osk_irq_object_t), GFP_KERNEL);
	if (NULL == irq_object) {
		return NULL;
	}

	if (-1 == irqnum) {
		/* Probe for IRQ */
		if ((NULL != trigger_func) && (NULL != ack_func)) {
			unsigned long probe_count = 3;
			_mali_osk_errcode_t err;
			int irq;

			MALI_DEBUG_PRINT(2, ("Probing for irq\n"));

			do {
				unsigned long mask;

				mask = probe_irq_on();
				trigger_func(probe_data);

				_mali_osk_time_ubusydelay(5);

				irq = probe_irq_off(mask);
				err = ack_func(probe_data);
			} while (irq < 0 && (err == _MALI_OSK_ERR_OK) && probe_count--);

			if (irq < 0 || (_MALI_OSK_ERR_OK != err)) irqnum = -1;
			else irqnum = irq;
		} else irqnum = -1; /* no probe functions, fault */

		if (-1 != irqnum) {
			/* found an irq */
			MALI_DEBUG_PRINT(2, ("Found irq %d\n", irqnum));
		} else {
			MALI_DEBUG_PRINT(2, ("Probe for irq failed\n"));
		}
	}

	irq_object->irqnum = irqnum;
	irq_object->uhandler = uhandler;
	irq_object->data = int_data;

	if (-1 == irqnum) {
		MALI_DEBUG_PRINT(2, ("No IRQ for core '%s' found during probe\n", description));
		kfree(irq_object);
		return NULL;
	}

#if defined(DEBUG)
	/* Verify that the configured interrupt settings are working */
	if (_MALI_OSK_ERR_OK != test_interrupt(irqnum, trigger_func, ack_func, probe_data, description)) {
		MALI_DEBUG_PRINT(2, ("Test of IRQ handler for core '%s' failed\n", description));
		kfree(irq_object);
		return NULL;
	}
#endif

	if (0 != request_irq(irqnum, irq_handler_upper_half, irq_flags, description, irq_object)) {
		MALI_DEBUG_PRINT(2, ("Unable to install IRQ handler for core '%s'\n", description));
		kfree(irq_object);
		return NULL;
	}

	return irq_object;
}

void _mali_osk_irq_term(_mali_osk_irq_t *irq)
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
static irqreturn_t irq_handler_upper_half(int port_name, void *dev_id)   /* , struct pt_regs *regs*/
{
	irqreturn_t ret = IRQ_NONE;
	mali_osk_irq_object_t *irq_object = (mali_osk_irq_object_t *)dev_id;

	if (_MALI_OSK_ERR_OK == irq_object->uhandler(irq_object->data)) {
		ret = IRQ_HANDLED;
	}

	return ret;
}
