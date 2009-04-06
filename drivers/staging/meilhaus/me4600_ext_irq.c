/**
 * @file me4600_ext_irq.c
 *
 * @brief ME-4000 external interrupt subdevice instance.
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
#include <linux/io.h>
#include <linux/types.h>

#include "medefines.h"
#include "meinternal.h"
#include "meerror.h"

#include "medebug.h"
#include "meids.h"
#include "me4600_reg.h"
#include "me4600_ai_reg.h"
#include "me4600_ext_irq_reg.h"
#include "me4600_ext_irq.h"

/*
 * Defines
 */

/*
 * Functions
 */

static int me4600_ext_irq_io_irq_start(me_subdevice_t *subdevice,
				       struct file *filep,
				       int channel,
				       int irq_source,
				       int irq_edge, int irq_arg, int flags)
{
	me4600_ext_irq_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long cpu_flags;
	uint32_t tmp;

	PDEBUG("executed.\n");

	instance = (me4600_ext_irq_subdevice_t *) subdevice;

	if (flags & ~ME_IO_IRQ_START_DIO_BIT) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if ((irq_edge != ME_IRQ_EDGE_RISING)
	    && (irq_edge != ME_IRQ_EDGE_FALLING)
	    && (irq_edge != ME_IRQ_EDGE_ANY)
	    ) {
		PERROR("Invalid irq edge specified.\n");
		return ME_ERRNO_INVALID_IRQ_EDGE;
	}

	if (irq_source != ME_IRQ_SOURCE_DIO_LINE) {
		PERROR("Invalid irq source specified.\n");
		return ME_ERRNO_INVALID_IRQ_SOURCE;
	}

	if (channel) {
		PERROR("Invalid channel specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	tmp = 0x0;		//inl(instance->ext_irq_config_reg);

	if (irq_edge == ME_IRQ_EDGE_RISING) {
		//tmp &= ~ME4600_EXT_IRQ_CONFIG_MASK;
		//tmp |= ME4600_EXT_IRQ_CONFIG_MASK_RISING;
	} else if (irq_edge == ME_IRQ_EDGE_FALLING) {
		//tmp &= ~ME4600_EXT_IRQ_CONFIG_MASK;
		//tmp |= ME4600_EXT_IRQ_CONFIG_MASK_FALLING;
		tmp = ME4600_EXT_IRQ_CONFIG_MASK_FALLING;
	} else if (irq_edge == ME_IRQ_EDGE_ANY) {
		//tmp &= ~ME4600_EXT_IRQ_CONFIG_MASK;
		//tmp |= ME4600_EXT_IRQ_CONFIG_MASK_ANY;
		tmp = ME4600_EXT_IRQ_CONFIG_MASK_ANY;
	}

	outl(tmp, instance->ext_irq_config_reg);
	PDEBUG_REG("ext_irq_config_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->ext_irq_config_reg - instance->reg_base, tmp);

	spin_lock_irqsave(instance->ctrl_reg_lock, cpu_flags);
	tmp = inl(instance->ctrl_reg);
	tmp &= ~(ME4600_AI_CTRL_BIT_EX_IRQ | ME4600_AI_CTRL_BIT_EX_IRQ_RESET);
	tmp |= ME4600_AI_CTRL_BIT_EX_IRQ;
	outl(tmp, instance->ctrl_reg);
	spin_unlock_irqrestore(instance->ctrl_reg_lock, cpu_flags);
	instance->rised = 0;
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ext_irq_io_irq_wait(me_subdevice_t *subdevice,
				      struct file *filep,
				      int channel,
				      int *irq_count,
				      int *value, int time_out, int flags)
{
	me4600_ext_irq_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	long t = 0;
	unsigned long cpu_flags;

	PDEBUG("executed.\n");

	instance = (me4600_ext_irq_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (channel) {
		PERROR("Invalid channel specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
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
	*value = instance->value;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ext_irq_io_irq_stop(me_subdevice_t *subdevice,
				      struct file *filep,
				      int channel, int flags)
{
	me4600_ext_irq_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long cpu_flags;
	uint32_t tmp;

	PDEBUG("executed.\n");

	instance = (me4600_ext_irq_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (channel) {
		PERROR("Invalid channel specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	spin_lock(instance->ctrl_reg_lock);
	tmp = inl(instance->ctrl_reg);
	tmp &= ~(ME4600_AI_CTRL_BIT_EX_IRQ | ME4600_AI_CTRL_BIT_EX_IRQ_RESET);
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_regv outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);
	spin_unlock(instance->ctrl_reg_lock);
	instance->rised = -1;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	wake_up_interruptible_all(&instance->wait_queue);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ext_irq_io_reset_subdevice(me_subdevice_t *subdevice,
					     struct file *filep, int flags)
{
	me4600_ext_irq_subdevice_t *instance;
	unsigned long cpu_flags;
	uint32_t tmp;

	PDEBUG("executed.\n");

	instance = (me4600_ext_irq_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	spin_lock(instance->ctrl_reg_lock);
	tmp = inl(instance->ctrl_reg);
	tmp &= ~(ME4600_AI_CTRL_BIT_EX_IRQ | ME4600_AI_CTRL_BIT_EX_IRQ_RESET);
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_regv outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);
	spin_unlock(instance->ctrl_reg_lock);
	instance->rised = -1;
	instance->count = 0;
	outl(ME4600_EXT_IRQ_CONFIG_MASK_ANY, instance->ext_irq_config_reg);
	PDEBUG_REG("ext_irq_config_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->ext_irq_config_reg - instance->reg_base,
		   ME4600_EXT_IRQ_CONFIG_MASK_ANY);
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	wake_up_interruptible_all(&instance->wait_queue);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static void me4600_ext_irq_destructor(struct me_subdevice *subdevice)
{
	me4600_ext_irq_subdevice_t *instance;

	PDEBUG("executed.\n");
	instance = (me4600_ext_irq_subdevice_t *) subdevice;
	me_subdevice_deinit(&instance->base);
	free_irq(instance->irq, instance);
	kfree(instance);
}

static int me4600_ext_irq_query_number_channels(me_subdevice_t *subdevice,
						int *number)
{
	PDEBUG("executed.\n");
	*number = 1;
	return ME_ERRNO_SUCCESS;
}

static int me4600_ext_irq_query_subdevice_type(me_subdevice_t *subdevice,
					       int *type, int *subtype)
{
	PDEBUG("executed.\n");
	*type = ME_TYPE_EXT_IRQ;
	*subtype = ME_SUBTYPE_SINGLE;
	return ME_ERRNO_SUCCESS;
}

static int me4600_ext_irq_query_subdevice_caps(me_subdevice_t *subdevice,
					       int *caps)
{
	PDEBUG("executed.\n");
	*caps =
	    ME_CAPS_EXT_IRQ_EDGE_RISING | ME_CAPS_EXT_IRQ_EDGE_FALLING |
	    ME_CAPS_EXT_IRQ_EDGE_ANY;
	return ME_ERRNO_SUCCESS;
}

static irqreturn_t me4600_ext_irq_isr(int irq, void *dev_id)
{
	me4600_ext_irq_subdevice_t *instance;
	uint32_t ctrl;
	uint32_t irq_status;

	instance = (me4600_ext_irq_subdevice_t *) dev_id;

	if (irq != instance->irq) {
		PERROR("Incorrect interrupt num: %d.\n", irq);
		return IRQ_NONE;
	}

	irq_status = inl(instance->irq_status_reg);
	if (!(irq_status & ME4600_IRQ_STATUS_BIT_EX)) {
		PINFO("%ld Shared interrupt. %s(): irq_status_reg=0x%04X\n",
		      jiffies, __func__, irq_status);
		return IRQ_NONE;
	}

	PDEBUG("executed.\n");

	spin_lock(&instance->subdevice_lock);
	instance->rised = 1;
	instance->value = inl(instance->ext_irq_value_reg);
	instance->count++;

	spin_lock(instance->ctrl_reg_lock);
	ctrl = inl(instance->ctrl_reg);
	ctrl |= ME4600_AI_CTRL_BIT_EX_IRQ_RESET;
	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);
	ctrl &= ~ME4600_AI_CTRL_BIT_EX_IRQ_RESET;
	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);
	spin_unlock(instance->ctrl_reg_lock);

	spin_unlock(&instance->subdevice_lock);
	wake_up_interruptible_all(&instance->wait_queue);

	return IRQ_HANDLED;
}

me4600_ext_irq_subdevice_t *me4600_ext_irq_constructor(uint32_t reg_base,
						       int irq,
						       spinlock_t *
						       ctrl_reg_lock)
{
	me4600_ext_irq_subdevice_t *subdevice;
	int err;

	PDEBUG("executed.\n");

	/* Allocate memory for subdevice instance */
	subdevice = kmalloc(sizeof(me4600_ext_irq_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for subdevice instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me4600_ext_irq_subdevice_t));

	/* Initialize subdevice base class */
	err = me_subdevice_init(&subdevice->base);

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}
	// Initialize spin locks.
	spin_lock_init(&subdevice->subdevice_lock);

	subdevice->ctrl_reg_lock = ctrl_reg_lock;

	/* Initialize wait queue */
	init_waitqueue_head(&subdevice->wait_queue);

	/* Register interrupt */
	subdevice->irq = irq;

	if (request_irq(subdevice->irq, me4600_ext_irq_isr,
			IRQF_DISABLED | IRQF_SHARED,
			ME4600_NAME, subdevice)) {
		PERROR("Cannot register interrupt.\n");
		kfree(subdevice);
		return NULL;
	}
	PINFO("Registered irq=%d.\n", subdevice->irq);

	/* Initialize registers */
	subdevice->irq_status_reg = reg_base + ME4600_IRQ_STATUS_REG;
	subdevice->ctrl_reg = reg_base + ME4600_AI_CTRL_REG;
	subdevice->ext_irq_config_reg = reg_base + ME4600_EXT_IRQ_CONFIG_REG;
	subdevice->ext_irq_value_reg = reg_base + ME4600_EXT_IRQ_VALUE_REG;
#ifdef MEDEBUG_DEBUG_REG
	subdevice->reg_base = reg_base;
#endif

	/* Override base class methods. */
	subdevice->base.me_subdevice_destructor = me4600_ext_irq_destructor;
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me4600_ext_irq_io_reset_subdevice;
	subdevice->base.me_subdevice_io_irq_start = me4600_ext_irq_io_irq_start;
	subdevice->base.me_subdevice_io_irq_wait = me4600_ext_irq_io_irq_wait;
	subdevice->base.me_subdevice_io_irq_stop = me4600_ext_irq_io_irq_stop;
	subdevice->base.me_subdevice_query_number_channels =
	    me4600_ext_irq_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me4600_ext_irq_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me4600_ext_irq_query_subdevice_caps;

	subdevice->rised = 0;
	subdevice->count = 0;

	return subdevice;
}
