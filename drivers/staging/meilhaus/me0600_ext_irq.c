/**
 * @file me0600_ext_irq.c
 *
 * @brief ME-630 external interrupt subdevice instance.
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
#include <linux/version.h>
#include <linux/module.h>

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/interrupt.h>

#include "medefines.h"
#include "meinternal.h"
#include "meerror.h"
#include "meids.h"
#include "medebug.h"

#include "meplx_reg.h"
#include "me0600_ext_irq_reg.h"
#include "me0600_ext_irq.h"

/*
 * Functions
 */

static int me0600_ext_irq_io_irq_start(struct me_subdevice *subdevice,
				       struct file *filep,
				       int channel,
				       int irq_source,
				       int irq_edge, int irq_arg, int flags)
{
	me0600_ext_irq_subdevice_t *instance;
	uint32_t tmp;
	unsigned long cpu_flags;

	PDEBUG("executed.\n");

	instance = (me0600_ext_irq_subdevice_t *) subdevice;

	if (flags & ~ME_IO_IRQ_START_DIO_BIT) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (instance->lintno > 1) {
		PERROR("Wrong idx=%d.\n", instance->lintno);
		return ME_ERRNO_INVALID_SUBDEVICE;
	}

	if (channel) {
		PERROR("Invalid channel specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	if (irq_source != ME_IRQ_SOURCE_DIO_LINE) {
		PERROR("Invalid irq source specified.\n");
		return ME_ERRNO_INVALID_IRQ_SOURCE;
	}

	if (irq_edge != ME_IRQ_EDGE_RISING) {
		PERROR("Invalid irq edge specified.\n");
		return ME_ERRNO_INVALID_IRQ_EDGE;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	spin_lock(instance->intcsr_lock);
	tmp = inl(instance->intcsr);
	switch (instance->lintno) {
	case 0:
		tmp |=
		    PLX_INTCSR_LOCAL_INT1_EN | PLX_INTCSR_LOCAL_INT1_POL |
		    PLX_INTCSR_PCI_INT_EN;
		break;
	case 1:
		tmp |=
		    PLX_INTCSR_LOCAL_INT2_EN | PLX_INTCSR_LOCAL_INT2_POL |
		    PLX_INTCSR_PCI_INT_EN;
		break;
	}
	outl(tmp, instance->intcsr);
	PDEBUG_REG("intcsr outl(plx:0x%X)=0x%x\n", instance->intcsr, tmp);
	spin_unlock(instance->intcsr_lock);
	instance->rised = 0;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me0600_ext_irq_io_irq_wait(struct me_subdevice *subdevice,
				      struct file *filep,
				      int channel,
				      int *irq_count,
				      int *value, int time_out, int flags)
{
	me0600_ext_irq_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	long t = 0;
	unsigned long cpu_flags;

	PDEBUG("executed.\n");

	instance = (me0600_ext_irq_subdevice_t *) subdevice;

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
				PERROR("Wait on interrupt timed out.\n");
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
		PERROR("Wait on interrupt aborted by signal.\n");
		err = ME_ERRNO_SIGNAL;
	}

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	instance->rised = 0;
	*irq_count = instance->n;
	*value = 1;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me0600_ext_irq_io_irq_stop(struct me_subdevice *subdevice,
				      struct file *filep,
				      int channel, int flags)
{
	me0600_ext_irq_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint32_t tmp;
	unsigned long cpu_flags;

	PDEBUG("executed.\n");

	instance = (me0600_ext_irq_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (instance->lintno > 1) {
		PERROR("Wrong idx=%d.\n", instance->lintno);
		return ME_ERRNO_INVALID_SUBDEVICE;
	}

	if (channel) {
		PERROR("Invalid channel specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	spin_lock(instance->intcsr_lock);
	tmp = inl(instance->intcsr);
	switch (instance->lintno) {
	case 0:
		tmp &= ~PLX_INTCSR_LOCAL_INT1_EN;
		break;
	case 1:
		tmp &= ~PLX_INTCSR_LOCAL_INT2_EN;
		break;
	}
	outl(tmp, instance->intcsr);
	PDEBUG_REG("intcsr outl(plx:0x%X)=0x%x\n", instance->intcsr, tmp);
	spin_unlock(instance->intcsr_lock);
	instance->rised = -1;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	wake_up_interruptible_all(&instance->wait_queue);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me0600_ext_irq_io_reset_subdevice(struct me_subdevice *subdevice,
					     struct file *filep, int flags)
{
	me0600_ext_irq_subdevice_t *instance;
	uint32_t tmp;
	unsigned long cpu_flags;

	PDEBUG("executed.\n");

	instance = (me0600_ext_irq_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	spin_lock(instance->intcsr_lock);
	tmp = inl(instance->intcsr);
	switch (instance->lintno) {
	case 0:
		tmp |= PLX_INTCSR_LOCAL_INT1_POL | PLX_INTCSR_PCI_INT_EN;
		tmp &= ~PLX_INTCSR_LOCAL_INT1_EN;
		break;
	case 1:
		tmp |= PLX_INTCSR_LOCAL_INT2_POL | PLX_INTCSR_PCI_INT_EN;
		tmp &= ~PLX_INTCSR_LOCAL_INT2_EN;
		break;
	}
	outl(tmp, instance->intcsr);
	PDEBUG_REG("intcsr outl(plx:0x%X)=0x%x\n", instance->intcsr, tmp);
	spin_unlock(instance->intcsr_lock);

	instance->rised = -1;
	instance->n = 0;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	wake_up_interruptible_all(&instance->wait_queue);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me0600_ext_irq_query_number_channels(struct me_subdevice *subdevice,
						int *number)
{
	PDEBUG("executed.\n");
	*number = 1;
	return ME_ERRNO_SUCCESS;
}

static int me0600_ext_irq_query_subdevice_type(struct me_subdevice *subdevice,
					       int *type, int *subtype)
{
	PDEBUG("executed.\n");
	*type = ME_TYPE_EXT_IRQ;
	*subtype = ME_SUBTYPE_SINGLE;
	return ME_ERRNO_SUCCESS;
}

static int me0600_ext_irq_query_subdevice_caps(struct me_subdevice *subdevice,
					       int *caps)
{
	PDEBUG("executed.\n");
	*caps = ME_CAPS_EXT_IRQ_EDGE_RISING;
	return ME_ERRNO_SUCCESS;
}

static void me0600_ext_irq_destructor(struct me_subdevice *subdevice)
{
	me0600_ext_irq_subdevice_t *instance;

	PDEBUG("executed.\n");

	instance = (me0600_ext_irq_subdevice_t *) subdevice;

	free_irq(instance->irq, (void *)instance);
	me_subdevice_deinit(&instance->base);
	kfree(instance);
}

static irqreturn_t me0600_isr(int irq, void *dev_id)
{
	me0600_ext_irq_subdevice_t *instance;
	uint32_t status;
	uint32_t mask = PLX_INTCSR_PCI_INT_EN;
	irqreturn_t ret = IRQ_HANDLED;

	instance = (me0600_ext_irq_subdevice_t *) dev_id;

	if (irq != instance->irq) {
		PERROR("Incorrect interrupt num: %d.\n", irq);
		return IRQ_NONE;
	}

	PDEBUG("executed.\n");

	if (instance->lintno > 1) {
		PERROR_CRITICAL
		    ("%s():Wrong subdevice index=%d plx:irq_status_reg=0x%04X.\n",
		     __func__, instance->lintno, inl(instance->intcsr));
		return IRQ_NONE;
	}

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->intcsr_lock);
	status = inl(instance->intcsr);
	switch (instance->lintno) {
	case 0:
		mask |= PLX_INTCSR_LOCAL_INT1_STATE | PLX_INTCSR_LOCAL_INT1_EN;
		break;
	case 1:
		mask |= PLX_INTCSR_LOCAL_INT2_STATE | PLX_INTCSR_LOCAL_INT2_EN;
		break;
	}

	if ((status & mask) == mask) {
		instance->rised = 1;
		instance->n++;
		inb(instance->reset_reg);
		PDEBUG("Interrupt detected.\n");
	} else {
		PINFO
		    ("%ld Shared interrupt. %s(): idx=0 plx:irq_status_reg=0x%04X\n",
		     jiffies, __func__, status);
		ret = IRQ_NONE;
	}
	spin_unlock(instance->intcsr_lock);
	spin_unlock(&instance->subdevice_lock);

	wake_up_interruptible_all(&instance->wait_queue);

	return ret;
}

me0600_ext_irq_subdevice_t *me0600_ext_irq_constructor(uint32_t plx_reg_base,
						       uint32_t me0600_reg_base,
						       spinlock_t *intcsr_lock,
						       unsigned ext_irq_idx,
						       int irq)
{
	me0600_ext_irq_subdevice_t *subdevice;
	int err;

	PDEBUG("executed.\n");

	/* Allocate memory for subdevice instance */
	subdevice = kmalloc(sizeof(me0600_ext_irq_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for 630_ext_irq instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me0600_ext_irq_subdevice_t));

	/* Initialize subdevice base class */
	err = me_subdevice_init(&subdevice->base);

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}
	// Initialize spin locks.
	spin_lock_init(&subdevice->subdevice_lock);

	subdevice->intcsr_lock = intcsr_lock;

	/* Initialize wait queue */
	init_waitqueue_head(&subdevice->wait_queue);

	subdevice->lintno = ext_irq_idx;

	/* Request interrupt line */
	subdevice->irq = irq;

	err = request_irq(subdevice->irq, me0600_isr,
#ifdef IRQF_DISABLED
			  IRQF_DISABLED | IRQF_SHARED,
#else
			  SA_INTERRUPT | SA_SHIRQ,
#endif
			  ME0600_NAME, (void *)subdevice);

	if (err) {
		PERROR("Cannot get interrupt line.\n");
		kfree(subdevice);
		return NULL;
	}
	PINFO("Registered irq=%d.\n", subdevice->irq);

	/* Initialize registers */
	subdevice->intcsr = plx_reg_base + PLX_INTCSR;
	subdevice->reset_reg =
	    me0600_reg_base + ME0600_INT_0_RESET_REG + ext_irq_idx;

	/* Initialize the subdevice methods */
	subdevice->base.me_subdevice_io_irq_start = me0600_ext_irq_io_irq_start;
	subdevice->base.me_subdevice_io_irq_wait = me0600_ext_irq_io_irq_wait;
	subdevice->base.me_subdevice_io_irq_stop = me0600_ext_irq_io_irq_stop;
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me0600_ext_irq_io_reset_subdevice;
	subdevice->base.me_subdevice_query_number_channels =
	    me0600_ext_irq_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me0600_ext_irq_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me0600_ext_irq_query_subdevice_caps;
	subdevice->base.me_subdevice_destructor = me0600_ext_irq_destructor;

	subdevice->rised = 0;
	subdevice->n = 0;

	return subdevice;
}
