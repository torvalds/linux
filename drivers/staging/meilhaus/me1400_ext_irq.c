/**
 * @file me1400_ext_irq.c
 *
 * @brief ME-1400 external interrupt subdevice instance.
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
#include <asm/io.h>
#include <linux/types.h>
#include <linux/interrupt.h>

#include "medefines.h"
#include "meinternal.h"
#include "meerror.h"
#include "medebug.h"
#include "meids.h"

#include "me1400_ext_irq.h"
#include "me1400_ext_irq_reg.h"

/*
 * Defines
 */
#define ME1400_EXT_IRQ_MAGIC_NUMBER	0x1401	/**< The magic number of the class structure. */
#define ME1400_EXT_IRQ_NUMBER_CHANNELS 1	/**< One channel per counter. */

/*
 * Functions
 */

static int me1400_ext_irq_io_irq_start(struct me_subdevice *subdevice,
				       struct file *filep,
				       int channel,
				       int irq_source,
				       int irq_edge, int irq_arg, int flags)
{
	me1400_ext_irq_subdevice_t *instance;
	unsigned long cpu_flags;
	uint8_t tmp;

	PDEBUG("executed.\n");

	instance = (me1400_ext_irq_subdevice_t *) subdevice;

	if (flags & ~ME_IO_IRQ_START_DIO_BIT) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (channel) {
		PERROR("Invalid channel.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	if (irq_source != ME_IRQ_SOURCE_DIO_LINE) {
		PERROR("Invalid irq source.\n");
		return ME_ERRNO_INVALID_IRQ_SOURCE;
	}

	if (irq_edge != ME_IRQ_EDGE_RISING) {
		PERROR("Invalid irq edge.\n");
		return ME_ERRNO_INVALID_IRQ_EDGE;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);

	spin_lock(instance->clk_src_reg_lock);
//                      // Enable IRQ on PLX
//                      tmp = inb(instance->plx_intcs_reg) | (PLX_LOCAL_INT1_EN | PLX_LOCAL_INT1_POL | PLX_PCI_INT_EN);
//                      outb(tmp, instance->plx_intcs_reg);
//                      PDEBUG_REG("ctrl_reg outb(PLX:0x%lX)=0x%x\n", instance->plx_intcs_reg, tmp);

	// Enable IRQ
	switch (instance->device_id) {
	case PCI_DEVICE_ID_MEILHAUS_ME140C:
	case PCI_DEVICE_ID_MEILHAUS_ME140D:
		tmp = inb(instance->ctrl_reg);
		tmp |= ME1400CD_EXT_IRQ_CLK_EN;
		outb(tmp, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, tmp);
		break;

	default:
		outb(ME1400AB_EXT_IRQ_IRQ_EN, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base,
			   ME1400AB_EXT_IRQ_IRQ_EN);
		break;
	}
	spin_unlock(instance->clk_src_reg_lock);
	instance->rised = 0;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me1400_ext_irq_io_irq_wait(struct me_subdevice *subdevice,
				      struct file *filep,
				      int channel,
				      int *irq_count,
				      int *value, int time_out, int flags)
{
	me1400_ext_irq_subdevice_t *instance;
	unsigned long cpu_flags;
	long t = 0;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed.\n");

	instance = (me1400_ext_irq_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (channel) {
		PERROR("Invalid channel.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	if (time_out < 0) {
		PERROR("Invalid time out.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	if (time_out) {
		/* Convert to ticks */
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

static int me1400_ext_irq_io_irq_stop(struct me_subdevice *subdevice,
				      struct file *filep,
				      int channel, int flags)
{
	me1400_ext_irq_subdevice_t *instance;
	unsigned long cpu_flags;
	uint8_t tmp;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed.\n");

	instance = (me1400_ext_irq_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (channel) {
		PERROR("Invalid channel.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	spin_lock(instance->clk_src_reg_lock);
//                      // Disable IRQ on PLX
//                      tmp = inb(instance->plx_intcs_reg) & ( ~(PLX_LOCAL_INT1_EN | PLX_LOCAL_INT1_POL | PLX_PCI_INT_EN));
//                      outb(tmp, instance->plx_intcs_reg);
//                      PDEBUG_REG("ctrl_reg outb(PLX:0x%lX)=0x%x\n", instance->plx_intcs_reg, tmp);

	switch (instance->device_id) {
	case PCI_DEVICE_ID_MEILHAUS_ME140C:
	case PCI_DEVICE_ID_MEILHAUS_ME140D:
		tmp = inb(instance->ctrl_reg);
		tmp &= ~ME1400CD_EXT_IRQ_CLK_EN;
		outb(tmp, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, tmp);

		break;

	default:
		outb(0x00, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, 0x00);
		break;
	}
	spin_unlock(instance->clk_src_reg_lock);
	instance->rised = -1;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	wake_up_interruptible_all(&instance->wait_queue);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me1400_ext_irq_io_reset_subdevice(struct me_subdevice *subdevice,
					     struct file *filep, int flags)
{
	me1400_ext_irq_subdevice_t *instance =
	    (me1400_ext_irq_subdevice_t *) subdevice;

	PDEBUG("executed.\n");

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	instance->n = 0;
	return me1400_ext_irq_io_irq_stop(subdevice, filep, 0, flags);
}

static int me1400_ext_irq_query_number_channels(struct me_subdevice *subdevice,
						int *number)
{
	PDEBUG("executed.\n");
	*number = ME1400_EXT_IRQ_NUMBER_CHANNELS;
	return ME_ERRNO_SUCCESS;
}

static int me1400_ext_irq_query_subdevice_type(struct me_subdevice *subdevice,
					       int *type, int *subtype)
{
	PDEBUG("executed.\n");
	*type = ME_TYPE_EXT_IRQ;
	*subtype = ME_SUBTYPE_SINGLE;
	return ME_ERRNO_SUCCESS;
}

static int me1400_ext_irq_query_subdevice_caps(struct me_subdevice *subdevice,
					       int *caps)
{
	PDEBUG("executed.\n");
	*caps = ME_CAPS_EXT_IRQ_EDGE_RISING;
	return ME_ERRNO_SUCCESS;
}

static int me1400_ext_irq_query_subdevice_caps_args(struct me_subdevice
						    *subdevice, int cap,
						    int *args, int count)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
static irqreturn_t me1400_ext_irq_isr(int irq, void *dev_id)
#else
static irqreturn_t me1400_ext_irq_isr(int irq, void *dev_id,
				      struct pt_regs *regs)
#endif
{
	me1400_ext_irq_subdevice_t *instance;
	uint32_t status;
	uint8_t tmp;

	instance = (me1400_ext_irq_subdevice_t *) dev_id;

	if (irq != instance->irq) {
		PERROR("Incorrect interrupt num: %d.\n", irq);
		return IRQ_NONE;
	}

	spin_lock(&instance->subdevice_lock);
	status = inl(instance->plx_intcs_reg);
//              if (!((status & PLX_LOCAL_INT1_STATE) && (status & PLX_LOCAL_INT1_EN) && (status & PLX_PCI_INT_EN)))
	if ((status &
	     (PLX_LOCAL_INT1_STATE | PLX_LOCAL_INT1_EN | PLX_PCI_INT_EN)) !=
	    (PLX_LOCAL_INT1_STATE | PLX_LOCAL_INT1_EN | PLX_PCI_INT_EN)) {
		spin_unlock(&instance->subdevice_lock);
		PINFO("%ld Shared interrupt. %s(): irq_status_reg=0x%04X\n",
		      jiffies, __func__, status);
		return IRQ_NONE;
	}

	inl(instance->ctrl_reg);

	PDEBUG("executed.\n");

	instance->n++;
	instance->rised = 1;

	switch (instance->device_id) {

	case PCI_DEVICE_ID_MEILHAUS_ME140C:
	case PCI_DEVICE_ID_MEILHAUS_ME140D:
		spin_lock(instance->clk_src_reg_lock);
		tmp = inb(instance->ctrl_reg);
		tmp &= ~ME1400CD_EXT_IRQ_CLK_EN;
		outb(tmp, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outb(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, tmp);
		tmp |= ME1400CD_EXT_IRQ_CLK_EN;
		outb(tmp, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outb(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, tmp);
		spin_unlock(instance->clk_src_reg_lock);

		break;

	default:
		outb(0, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outb(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, 0);
		outb(ME1400AB_EXT_IRQ_IRQ_EN, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outb(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base,
			   ME1400AB_EXT_IRQ_IRQ_EN);
		break;
	}

	spin_unlock(&instance->subdevice_lock);
	wake_up_interruptible_all(&instance->wait_queue);

	return IRQ_HANDLED;
}

static void me1400_ext_irq_destructor(struct me_subdevice *subdevice)
{
	me1400_ext_irq_subdevice_t *instance;
	uint8_t tmp;

	PDEBUG("executed.\n");

	instance = (me1400_ext_irq_subdevice_t *) subdevice;

	// Disable IRQ on PLX
	tmp =
	    inb(instance->
		plx_intcs_reg) & (~(PLX_LOCAL_INT1_EN | PLX_LOCAL_INT1_POL |
				    PLX_PCI_INT_EN));
	outb(tmp, instance->plx_intcs_reg);
	PDEBUG_REG("ctrl_reg outb(plx:0x%lX)=0x%x\n", instance->plx_intcs_reg,
		   tmp);

	free_irq(instance->irq, (void *)instance);
	me_subdevice_deinit(&instance->base);
	kfree(instance);
}

me1400_ext_irq_subdevice_t *me1400_ext_irq_constructor(uint32_t device_id,
						       uint32_t plx_reg_base,
						       uint32_t me1400_reg_base,
						       spinlock_t *
						       clk_src_reg_lock,
						       int irq)
{
	me1400_ext_irq_subdevice_t *subdevice;
	int err;
	uint8_t tmp;

	PDEBUG("executed.\n");

	/* Allocate memory for subdevice instance */
	subdevice = kmalloc(sizeof(me1400_ext_irq_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for 1400_ext_irq instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me1400_ext_irq_subdevice_t));

	/* Initialize subdevice base class */
	err = me_subdevice_init(&subdevice->base);

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}
	// Initialize spin locks.
	spin_lock_init(&subdevice->subdevice_lock);
	subdevice->clk_src_reg_lock = clk_src_reg_lock;

	/* Initialize wait queue */
	init_waitqueue_head(&subdevice->wait_queue);

	subdevice->irq = irq;

	err = request_irq(irq, me1400_ext_irq_isr,
#ifdef IRQF_DISABLED
			  IRQF_DISABLED | IRQF_SHARED,
#else
			  SA_INTERRUPT | SA_SHIRQ,
#endif
			  ME1400_NAME, (void *)subdevice);

	if (err) {
		PERROR("Can't get irq.\n");
		me_subdevice_deinit(&subdevice->base);
		kfree(subdevice);
		return NULL;
	}
	PINFO("Registered irq=%d.\n", subdevice->irq);

	/* Initialize registers */
	subdevice->plx_intcs_reg = plx_reg_base + PLX_INTCSR_REG;
	subdevice->ctrl_reg = me1400_reg_base + ME1400AB_EXT_IRQ_CTRL_REG;
#ifdef MEDEBUG_DEBUG_REG
	subdevice->reg_base = me1400_reg_base;
#endif

	// Enable IRQ on PLX
	tmp =
	    inb(subdevice->
		plx_intcs_reg) | (PLX_LOCAL_INT1_EN | PLX_LOCAL_INT1_POL |
				  PLX_PCI_INT_EN);
	outb(tmp, subdevice->plx_intcs_reg);
	PDEBUG_REG("ctrl_reg outb(Pplx:0x%lX)=0x%x\n", subdevice->plx_intcs_reg,
		   tmp);

	/* Initialize the subdevice methods */
	subdevice->base.me_subdevice_io_irq_start = me1400_ext_irq_io_irq_start;
	subdevice->base.me_subdevice_io_irq_wait = me1400_ext_irq_io_irq_wait;
	subdevice->base.me_subdevice_io_irq_stop = me1400_ext_irq_io_irq_stop;
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me1400_ext_irq_io_reset_subdevice;
	subdevice->base.me_subdevice_query_number_channels =
	    me1400_ext_irq_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me1400_ext_irq_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me1400_ext_irq_query_subdevice_caps;
	subdevice->base.me_subdevice_query_subdevice_caps_args =
	    me1400_ext_irq_query_subdevice_caps_args;
	subdevice->base.me_subdevice_destructor = me1400_ext_irq_destructor;

	subdevice->rised = 0;
	subdevice->n = 0;

	return subdevice;
}
