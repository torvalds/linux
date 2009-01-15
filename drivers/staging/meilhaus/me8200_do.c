/**
 * @file me8200_do.c
 *
 * @brief ME-8200 digital output subdevice instance.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 * @author Krzysztof Gantzke	(k.gantzke@meilhaus.de)
 */

/*
 * Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __KERNEL__
#  define __KERNEL__
#endif

/*
 * Includes
 */
#include <linux/module.h>

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <linux/types.h>
#include <linux/version.h>

#include "medefines.h"
#include "meinternal.h"
#include "meerror.h"

#include "meids.h"
#include "medebug.h"
#include "me8200_reg.h"
#include "me8200_do_reg.h"
#include "me8200_do.h"

/*
 * Defines
 */

/*
 * Functions
 */

static int me8200_do_io_irq_start(me_subdevice_t * subdevice,
				  struct file *filep,
				  int channel,
				  int irq_source,
				  int irq_edge, int irq_arg, int flags)
{
	me8200_do_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint8_t tmp;
	unsigned long status;

	if (flags & ~ME_IO_IRQ_START_DIO_BYTE) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (channel != 0) {
		PERROR("Invalid channel specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	if (irq_source != ME_IRQ_SOURCE_DIO_OVER_TEMP) {
		PERROR("Invalid interrupt source specified.\n");
		return ME_ERRNO_INVALID_IRQ_SOURCE;
	}

	PDEBUG("executed.\n");

	instance = (me8200_do_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, status);
	spin_lock(instance->irq_mode_lock);
	tmp = inb(instance->irq_ctrl_reg);
	tmp |=
	    ME8200_IRQ_MODE_BIT_ENABLE_POWER << (ME8200_IRQ_MODE_POWER_SHIFT *
						 instance->do_idx);
	outb(tmp, instance->irq_ctrl_reg);
	PDEBUG_REG("irq_ctrl_reg outb(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->irq_ctrl_reg - instance->reg_base, tmp);
	spin_unlock(instance->irq_mode_lock);
	instance->rised = 0;
	spin_unlock_irqrestore(&instance->subdevice_lock, status);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8200_do_io_irq_wait(me_subdevice_t * subdevice,
				 struct file *filep,
				 int channel,
				 int *irq_count,
				 int *value, int time_out, int flags)
{
	me8200_do_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	long t = 0;
	unsigned long cpu_flags;

	PDEBUG("executed.\n");

	instance = (me8200_do_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (time_out < 0) {
		PERROR("Invalid time_out specified.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	if (time_out) {
		t = (time_out * HZ) / 1000;

		if (t == 0)
			t = 1;
	}

	ME_SUBDEVICE_ENTER;

	if (instance->rised <= 0) {
		instance->rised = 0;

		if (time_out) {
			t = wait_event_interruptible_timeout(instance->
							     wait_queue,
							     (instance->rised !=
							      0), t);

			if (t == 0) {
				PERROR
				    ("Wait on external interrupt timed out.\n");
				err = ME_ERRNO_TIMEOUT;
			}
		} else {
			wait_event_interruptible(instance->wait_queue,
						 (instance->rised != 0));
		}

		if (instance->rised < 0) {
			PERROR("Wait on interrupt aborted by user.\n");
			err = ME_ERRNO_CANCELLED;
		}
	}

	if (signal_pending(current)) {
		PERROR("Wait on external interrupt aborted by signal.\n");
		err = ME_ERRNO_SIGNAL;
	}

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	instance->rised = 0;
	*irq_count = instance->count;
	*value = 0;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8200_do_io_irq_stop(me_subdevice_t * subdevice,
				 struct file *filep, int channel, int flags)
{
	me8200_do_subdevice_t *instance;
	uint8_t tmp;
	unsigned long cpu_flags;

	PDEBUG("executed.\n");

	instance = (me8200_do_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	spin_lock(instance->irq_mode_lock);
	tmp = inb(instance->irq_ctrl_reg);
	tmp &=
	    ~(ME8200_IRQ_MODE_BIT_ENABLE_POWER <<
	      (ME8200_IRQ_MODE_POWER_SHIFT * instance->do_idx));
	outb(tmp, instance->irq_ctrl_reg);
	PDEBUG_REG("irq_ctrl_reg outb(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->irq_ctrl_reg - instance->reg_base, tmp);
	spin_unlock(instance->irq_mode_lock);
	instance->rised = -1;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	wake_up_interruptible_all(&instance->wait_queue);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me8200_do_io_reset_subdevice(struct me_subdevice *subdevice,
					struct file *filep, int flags)
{
	me8200_do_subdevice_t *instance;
	unsigned long cpu_flags;
	uint8_t tmp;

	PDEBUG("executed.\n");

	instance = (me8200_do_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	outb(0x00, instance->port_reg);
	PDEBUG_REG("port_reg outb(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->port_reg - instance->reg_base, 0x00);
	spin_lock(instance->irq_mode_lock);
	tmp = inb(instance->irq_ctrl_reg);
	tmp &=
	    ~(ME8200_IRQ_MODE_BIT_ENABLE_POWER <<
	      (ME8200_IRQ_MODE_POWER_SHIFT * instance->do_idx));
	outb(tmp, instance->irq_ctrl_reg);
	PDEBUG_REG("irq_ctrl_reg outb(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->irq_ctrl_reg - instance->reg_base, tmp);
	spin_unlock(instance->irq_mode_lock);
	instance->rised = -1;
	instance->count = 0;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	wake_up_interruptible_all(&instance->wait_queue);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me8200_do_io_single_config(me_subdevice_t * subdevice,
				      struct file *filep,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags)
{
	me8200_do_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long status;

	PDEBUG("executed.\n");

	instance = (me8200_do_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, status);
	switch (flags) {
	case ME_IO_SINGLE_CONFIG_NO_FLAGS:
	case ME_IO_SINGLE_CONFIG_DIO_BYTE:
		if (channel == 0) {
			if (single_config == ME_SINGLE_CONFIG_DIO_OUTPUT) {
			} else {
				PERROR("Invalid byte direction specified.\n");
				err = ME_ERRNO_INVALID_SINGLE_CONFIG;
			}
		} else {
			PERROR("Invalid byte specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	default:
		PERROR("Invalid flags specified.\n");
		err = ME_ERRNO_INVALID_FLAGS;
	}
	spin_unlock_irqrestore(&instance->subdevice_lock, status);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8200_do_io_single_read(me_subdevice_t * subdevice,
				    struct file *filep,
				    int channel,
				    int *value, int time_out, int flags)
{
	me8200_do_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long status;

	PDEBUG("executed.\n");

	instance = (me8200_do_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, status);
	switch (flags) {
	case ME_IO_SINGLE_TYPE_DIO_BIT:
		if ((channel >= 0) && (channel < 8)) {
			*value = inb(instance->port_reg) & (0x1 << channel);
		} else {
			PERROR("Invalid bit number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_NO_FLAGS:
	case ME_IO_SINGLE_TYPE_DIO_BYTE:
		if (channel == 0) {
			*value = inb(instance->port_reg);
		} else {
			PERROR("Invalid byte number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	default:
		PERROR("Invalid flags specified.\n");
		err = ME_ERRNO_INVALID_FLAGS;
	}
	spin_unlock_irqrestore(&instance->subdevice_lock, status);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8200_do_io_single_write(me_subdevice_t * subdevice,
				     struct file *filep,
				     int channel,
				     int value, int time_out, int flags)
{
	me8200_do_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint8_t state;
	unsigned long status;

	PDEBUG("executed.\n");

	instance = (me8200_do_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, status);
	switch (flags) {
	case ME_IO_SINGLE_TYPE_DIO_BIT:
		if ((channel >= 0) && (channel < 8)) {
			state = inb(instance->port_reg);
			state =
			    value ? (state | (0x1 << channel)) : (state &
								  ~(0x1 <<
								    channel));
			outb(state, instance->port_reg);
			PDEBUG_REG("port_reg outb(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->port_reg - instance->reg_base,
				   state);
		} else {
			PERROR("Invalid bit number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_NO_FLAGS:
	case ME_IO_SINGLE_TYPE_DIO_BYTE:
		if (channel == 0) {
			outb(value, instance->port_reg);
			PDEBUG_REG("port_reg outb(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->port_reg - instance->reg_base,
				   value);
		} else {
			PERROR("Invalid byte number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	default:
		PERROR("Invalid flags specified.\n");
		err = ME_ERRNO_INVALID_FLAGS;
	}
	spin_unlock_irqrestore(&instance->subdevice_lock, status);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8200_do_query_number_channels(me_subdevice_t * subdevice,
					   int *number)
{
	PDEBUG("executed.\n");
	*number = 8;
	return ME_ERRNO_SUCCESS;
}

static int me8200_do_query_subdevice_type(me_subdevice_t * subdevice,
					  int *type, int *subtype)
{
	PDEBUG("executed.\n");
	*type = ME_TYPE_DO;
	*subtype = ME_SUBTYPE_SINGLE;
	return ME_ERRNO_SUCCESS;
}

static int me8200_do_query_subdevice_caps(me_subdevice_t * subdevice, int *caps)
{
	PDEBUG("executed.\n");
	*caps = ME_CAPS_DIO_OVER_TEMP_IRQ;
	return ME_ERRNO_SUCCESS;
}

static void me8200_do_destructor(struct me_subdevice *subdevice)
{
	me8200_do_subdevice_t *instance;

	PDEBUG("executed.\n");

	instance = (me8200_do_subdevice_t *) subdevice;

	free_irq(instance->irq, (void *)instance);
	me_subdevice_deinit(&instance->base);
	kfree(instance);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
static irqreturn_t me8200_do_isr(int irq, void *dev_id)
#else
static irqreturn_t me8200_do_isr(int irq, void *dev_id, struct pt_regs *regs)
#endif
{
	me8200_do_subdevice_t *instance;
	uint16_t ctrl;
	uint8_t irq_status;

	instance = (me8200_do_subdevice_t *) dev_id;

	if (irq != instance->irq) {
		PERROR("Incorrect interrupt num: %d.\n", irq);
		return IRQ_NONE;
	}

	irq_status = inb(instance->irq_status_reg);
	if (!
	    (irq_status &
	     (ME8200_DO_IRQ_STATUS_BIT_ACTIVE << instance->do_idx))) {
		PINFO
		    ("%ld Shared interrupt. %s(): idx=%d irq_status_reg=0x%04X\n",
		     jiffies, __func__, instance->do_idx, irq_status);
		return IRQ_NONE;
	}

	PDEBUG("executed.\n");

	spin_lock(&instance->subdevice_lock);
	instance->rised = 1;
	instance->count++;

	spin_lock(instance->irq_mode_lock);
	ctrl = inw(instance->irq_ctrl_reg);
	ctrl |= ME8200_IRQ_MODE_BIT_CLEAR_POWER << instance->do_idx;
	outw(ctrl, instance->irq_ctrl_reg);
	PDEBUG_REG("irq_ctrl_reg outw(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->irq_ctrl_reg - instance->reg_base, ctrl);
	ctrl &= ~(ME8200_IRQ_MODE_BIT_CLEAR_POWER << instance->do_idx);
	outw(ctrl, instance->irq_ctrl_reg);
	PDEBUG_REG("irq_ctrl_reg outw(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->irq_ctrl_reg - instance->reg_base, ctrl);
	spin_unlock(instance->irq_mode_lock);
	spin_unlock(&instance->subdevice_lock);
	wake_up_interruptible_all(&instance->wait_queue);

	return IRQ_HANDLED;
}

me8200_do_subdevice_t *me8200_do_constructor(uint32_t reg_base,
					     unsigned int do_idx,
					     int irq,
					     spinlock_t * irq_mode_lock)
{
	me8200_do_subdevice_t *subdevice;
	int err;

	PDEBUG("executed.\n");

	/* Allocate memory for subdevice instance */
	subdevice = kmalloc(sizeof(me8200_do_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for subdevice instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me8200_do_subdevice_t));

	/* Initialize subdevice base class */
	err = me_subdevice_init(&subdevice->base);

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}
	// Initialize spin locks.
	spin_lock_init(&subdevice->subdevice_lock);

	subdevice->irq_mode_lock = irq_mode_lock;

	/* Save the index of the digital output */
	subdevice->do_idx = do_idx;
	subdevice->irq = irq;

	/* Initialize the registers */
	if (do_idx == 0) {
		subdevice->port_reg = reg_base + ME8200_DO_PORT_0_REG;
	} else if (do_idx == 1) {
		subdevice->port_reg = reg_base + ME8200_DO_PORT_1_REG;
	} else {
		PERROR("Wrong subdevice idx=%d.\n", do_idx);
		kfree(subdevice);
		return NULL;
	}
	subdevice->irq_ctrl_reg = reg_base + ME8200_IRQ_MODE_REG;
	subdevice->irq_status_reg = reg_base + ME8200_DO_IRQ_STATUS_REG;
#ifdef MEDEBUG_DEBUG_REG
	subdevice->reg_base = reg_base;
#endif

	/* Initialize the wait queue */
	init_waitqueue_head(&subdevice->wait_queue);

	/* Request the interrupt line */
	err = request_irq(irq, me8200_do_isr,
#ifdef IRQF_DISABLED
			  IRQF_DISABLED | IRQF_SHARED,
#else
			  SA_INTERRUPT | SA_SHIRQ,
#endif
			  ME8200_NAME, (void *)subdevice);

	if (err) {
		PERROR("Cannot get interrupt line.\n");
		kfree(subdevice);
		return NULL;
	}
	PINFO("Registered irq=%d.\n", irq);

	/* Overload base class methods. */
	subdevice->base.me_subdevice_io_irq_start = me8200_do_io_irq_start;
	subdevice->base.me_subdevice_io_irq_wait = me8200_do_io_irq_wait;
	subdevice->base.me_subdevice_io_irq_stop = me8200_do_io_irq_stop;
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me8200_do_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config =
	    me8200_do_io_single_config;
	subdevice->base.me_subdevice_io_single_read = me8200_do_io_single_read;
	subdevice->base.me_subdevice_io_single_write =
	    me8200_do_io_single_write;
	subdevice->base.me_subdevice_query_number_channels =
	    me8200_do_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me8200_do_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me8200_do_query_subdevice_caps;
	subdevice->base.me_subdevice_destructor = me8200_do_destructor;

	subdevice->rised = 0;
	subdevice->count = 0;

	return subdevice;
}
