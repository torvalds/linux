/**
 * @file me8100_di.c
 *
 * @brief ME-8100 digital input subdevice instance.
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
#include <asm/io.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/version.h>

#include "medefines.h"
#include "meerror.h"

#include "meids.h"
#include "medebug.h"
#include "meplx_reg.h"
#include "me8100_reg.h"
#include "me8100_di_reg.h"
#include "me8100_di.h"

/*
 * Defines
 */

/*
 * Functions
 */

static int me8100_di_io_reset_subdevice(struct me_subdevice *subdevice,
					struct file *filep, int flags)
{
	me8100_di_subdevice_t *instance;
	unsigned short ctrl;
	unsigned long cpu_flags;

	PDEBUG("executed.\n");

	instance = (me8100_di_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	spin_lock(instance->ctrl_reg_lock);
	ctrl = inw(instance->ctrl_reg);
	ctrl &= ~(ME8100_DIO_CTRL_BIT_INTB_1 | ME8100_DIO_CTRL_BIT_INTB_0);
	outw(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);
	spin_unlock(instance->ctrl_reg_lock);

	outw(0, instance->mask_reg);
	PDEBUG_REG("mask_reg outw(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->mask_reg - instance->reg_base, 0);
	outw(0, instance->pattern_reg);
	PDEBUG_REG("pattern_reg outw(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->pattern_reg - instance->reg_base, 0);
	instance->rised = -1;
	instance->irq_count = 0;
	instance->filtering_flag = 0;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	outl(PLX_INTCSR_LOCAL_INT1_EN |
	     PLX_INTCSR_LOCAL_INT1_POL |
	     PLX_INTCSR_LOCAL_INT2_EN |
	     PLX_INTCSR_LOCAL_INT2_POL |
	     PLX_INTCSR_PCI_INT_EN, instance->irq_status_reg);
	PDEBUG_REG("plx:irq_status_reg outl(0x%lX)=0x%x\n",
		   instance->irq_status_reg,
		   PLX_INTCSR_LOCAL_INT1_EN | PLX_INTCSR_LOCAL_INT1_POL |
		   PLX_INTCSR_LOCAL_INT2_EN | PLX_INTCSR_LOCAL_INT2_POL |
		   PLX_INTCSR_PCI_INT_EN);

	wake_up_interruptible_all(&instance->wait_queue);
	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me8100_di_io_irq_start(me_subdevice_t * subdevice,
				  struct file *filep,
				  int channel,
				  int irq_source,
				  int irq_edge, int irq_arg, int flags)
{
	me8100_di_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint16_t ctrl;
	unsigned long cpu_flags;

	PDEBUG("executed.\n");

	instance = (me8100_di_subdevice_t *) subdevice;

	if (irq_source == ME_IRQ_SOURCE_DIO_PATTERN) {
		if (flags &
		    ~(ME_IO_IRQ_START_PATTERN_FILTERING |
		      ME_IO_IRQ_START_DIO_WORD)) {
			PERROR("Invalid flag specified.\n");
			return ME_ERRNO_INVALID_FLAGS;
		}

		if (irq_edge != ME_IRQ_EDGE_NOT_USED) {
			PERROR("Invalid irq edge specified.\n");
			return ME_ERRNO_INVALID_IRQ_EDGE;
		}
	} else if (irq_source == ME_IRQ_SOURCE_DIO_MASK) {
		if (flags &
		    ~(ME_IO_IRQ_START_EXTENDED_STATUS |
		      ME_IO_IRQ_START_DIO_WORD)) {
			PERROR("Invalid flag specified.\n");
			return ME_ERRNO_INVALID_FLAGS;
		}

		if (irq_edge != ME_IRQ_EDGE_ANY) {
			PERROR("Invalid irq edge specified.\n");
			return ME_ERRNO_INVALID_IRQ_EDGE;
		}

		if (!(irq_arg & 0xFFFF)) {
			PERROR("No mask specified.\n");
			return ME_ERRNO_INVALID_IRQ_ARG;
		}
	} else {
		PERROR("Invalid irq source specified.\n");
		return ME_ERRNO_INVALID_IRQ_SOURCE;
	}

	if (channel) {
		PERROR("Invalid channel specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	if (irq_source == ME_IRQ_SOURCE_DIO_PATTERN) {
		outw(irq_arg, instance->pattern_reg);
		instance->compare_value = irq_arg;
		instance->filtering_flag =
		    (flags & ME_IO_IRQ_START_PATTERN_FILTERING) ? 1 : 0;
	}
	if (irq_source == ME_IRQ_SOURCE_DIO_MASK) {
		outw(irq_arg, instance->mask_reg);
	}

	spin_lock(instance->ctrl_reg_lock);
	ctrl = inw(instance->ctrl_reg);
	ctrl |= ME8100_DIO_CTRL_BIT_INTB_0;
	if (irq_source == ME_IRQ_SOURCE_DIO_PATTERN) {
		ctrl &= ~ME8100_DIO_CTRL_BIT_INTB_1;
	}

	if (irq_source == ME_IRQ_SOURCE_DIO_MASK) {
		ctrl |= ME8100_DIO_CTRL_BIT_INTB_1;
	}
	outw(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outw(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);
	spin_unlock(instance->ctrl_reg_lock);

	instance->rised = 0;
	instance->status_value = 0;
	instance->status_value_edges = 0;
	instance->line_value = inw(instance->port_reg);
	instance->status_flag = flags & ME_IO_IRQ_START_EXTENDED_STATUS;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8100_di_io_irq_wait(me_subdevice_t * subdevice,
				 struct file *filep,
				 int channel,
				 int *irq_count,
				 int *value, int time_out, int flags)
{
	me8100_di_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	long t = 0;
	unsigned long cpu_flags;
	int count;

	PDEBUG("executed.\n");
	PDEVELOP("PID: %d.\n", current->pid);

	instance = (me8100_di_subdevice_t *) subdevice;

	if (flags &
	    ~(ME_IO_IRQ_WAIT_NORMAL_STATUS | ME_IO_IRQ_WAIT_EXTENDED_STATUS)) {
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
		count = instance->irq_count;

		if (time_out) {
			t = wait_event_interruptible_timeout(instance->
							     wait_queue,
							     ((count !=
							       instance->
							       irq_count)
							      || (instance->
								  rised < 0)),
							     t);
//                      t = wait_event_interruptible_timeout(instance->wait_queue, (instance->rised != 0), t);
			if (t == 0) {
				PERROR("Wait on interrupt timed out.\n");
				err = ME_ERRNO_TIMEOUT;
			}
		} else {
			wait_event_interruptible(instance->wait_queue,
						 ((count != instance->irq_count)
						  || (instance->rised < 0)));
//                      wait_event_interruptible(instance->wait_queue, (instance->rised != 0));
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
	*irq_count = instance->irq_count;
	if (!err) {
		if (flags & ME_IO_IRQ_WAIT_NORMAL_STATUS) {
			*value = instance->status_value;
		} else if (flags & ME_IO_IRQ_WAIT_EXTENDED_STATUS) {
			*value = instance->status_value_edges;
		} else {	// Use default
			if (!instance->status_flag) {
				*value = instance->status_value;
			} else {
				*value = instance->status_value_edges;
			}
		}
		instance->rised = 0;
/*
			instance->status_value = 0;
			instance->status_value_edges = 0;
*/
	} else {
		*value = 0;
	}
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8100_di_io_irq_stop(me_subdevice_t * subdevice,
				 struct file *filep, int channel, int flags)
{
	me8100_di_subdevice_t *instance;
	uint16_t ctrl;
	unsigned long cpu_flags;

	PDEBUG("executed.\n");

	instance = (me8100_di_subdevice_t *) subdevice;

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
	ctrl = inw(instance->ctrl_reg);
	ctrl &= ~(ME8100_DIO_CTRL_BIT_INTB_1 | ME8100_DIO_CTRL_BIT_INTB_0);
	outw(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outw(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);
	spin_unlock(instance->ctrl_reg_lock);
	instance->rised = -1;
	instance->status_value = 0;
	instance->status_value_edges = 0;
	instance->filtering_flag = 0;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	wake_up_interruptible_all(&instance->wait_queue);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me8100_di_io_single_config(me_subdevice_t * subdevice,
				      struct file *filep,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags)
{
	me8100_di_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed.\n");

	instance = (me8100_di_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);

	switch (flags) {
	case ME_IO_SINGLE_CONFIG_NO_FLAGS:
	case ME_IO_SINGLE_CONFIG_DIO_WORD:
		if (channel == 0) {
			if (single_config == ME_SINGLE_CONFIG_DIO_INPUT) {
			} else {
				PERROR
				    ("Invalid port configuration specified.\n");
				err = ME_ERRNO_INVALID_SINGLE_CONFIG;
			}
		} else {
			PERROR("Invalid channel number.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	default:
		PERROR("Invalid flags specified.\n");
		err = ME_ERRNO_INVALID_FLAGS;
	}

	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8100_di_io_single_read(me_subdevice_t * subdevice,
				    struct file *filep,
				    int channel,
				    int *value, int time_out, int flags)
{
	me8100_di_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed.\n");

	instance = (me8100_di_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);

	switch (flags) {

	case ME_IO_SINGLE_TYPE_DIO_BIT:
		if ((channel >= 0) && (channel < 16)) {
			*value = inw(instance->port_reg) & (0x1 << channel);
		} else {
			PERROR("Invalid bit number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_TYPE_DIO_BYTE:
		if (channel == 0) {
			*value = inw(instance->port_reg) & 0xFF;
		} else if (channel == 1) {
			*value = (inw(instance->port_reg) >> 8) & 0xFF;
		} else {
			PERROR("Invalid byte number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_NO_FLAGS:
	case ME_IO_SINGLE_TYPE_DIO_WORD:
		if (channel == 0) {
			*value = inw(instance->port_reg);
		} else {
			PERROR("Invalid word number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}

		break;

	default:
		PERROR("Invalid flags specified.\n");
		err = ME_ERRNO_INVALID_FLAGS;
	}

	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8100_di_query_number_channels(me_subdevice_t * subdevice,
					   int *number)
{
	PDEBUG("executed.\n");
	*number = 16;
	return ME_ERRNO_SUCCESS;
}

static int me8100_di_query_subdevice_type(me_subdevice_t * subdevice,
					  int *type, int *subtype)
{
	PDEBUG("executed.\n");
	*type = ME_TYPE_DI;
	*subtype = ME_SUBTYPE_SINGLE;
	return ME_ERRNO_SUCCESS;
}

static int me8100_di_query_subdevice_caps(me_subdevice_t * subdevice, int *caps)
{
	PDEBUG("executed.\n");
	*caps = ME_CAPS_DIO_BIT_PATTERN_IRQ | ME_CAPS_DIO_BIT_MASK_IRQ_EDGE_ANY;
	return ME_ERRNO_SUCCESS;
}

static void me8100_di_destructor(struct me_subdevice *subdevice)
{
	me8100_di_subdevice_t *instance;

	PDEBUG("executed.\n");

	instance = (me8100_di_subdevice_t *) subdevice;

	free_irq(instance->irq, (void *)instance);
	me_subdevice_deinit(&instance->base);
	kfree(instance);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
static irqreturn_t me8100_isr(int irq, void *dev_id)
#else
static irqreturn_t me8100_isr(int irq, void *dev_id, struct pt_regs *regs)
#endif
{
	me8100_di_subdevice_t *instance;
	uint32_t icsr;

	uint16_t irq_status;
	uint16_t line_value = 0;

	uint32_t status_val = 0;

	PDEBUG("executed.\n");

	instance = (me8100_di_subdevice_t *) dev_id;

	if (irq != instance->irq) {
		PERROR("Incorrect interrupt num: %d.\n", irq);
		return IRQ_NONE;
	}

	icsr = inl(instance->irq_status_reg);
	if (instance->di_idx == 0) {

		if ((icsr &
		     (PLX_INTCSR_LOCAL_INT1_STATE | PLX_INTCSR_PCI_INT_EN |
		      PLX_INTCSR_LOCAL_INT1_EN)) !=
		    (PLX_INTCSR_LOCAL_INT1_STATE | PLX_INTCSR_PCI_INT_EN |
		     PLX_INTCSR_LOCAL_INT1_EN)) {
			PINFO
			    ("%ld Shared interrupt. %s(): idx=0 plx:irq_status_reg=0x%04X\n",
			     jiffies, __func__, icsr);
			return IRQ_NONE;
		}
	} else if (instance->di_idx == 1) {
		if ((icsr &
		     (PLX_INTCSR_LOCAL_INT2_STATE | PLX_INTCSR_PCI_INT_EN |
		      PLX_INTCSR_LOCAL_INT2_EN)) !=
		    (PLX_INTCSR_LOCAL_INT2_STATE | PLX_INTCSR_PCI_INT_EN |
		     PLX_INTCSR_LOCAL_INT2_EN)) {
			PINFO
			    ("%ld Shared interrupt. %s(): idx=1 plx:irq_status_reg=0x%04X\n",
			     jiffies, __func__, icsr);
			return IRQ_NONE;
		}
	} else {
		PERROR("%s():Wrong interrupt idx=%d csr=0x%X.\n", __func__,
		       instance->di_idx, icsr);
		return IRQ_NONE;
	}

	PDEBUG("me8100_isr():Interrupt from idx=%d occured.\n",
	       instance->di_idx);
	spin_lock(&instance->subdevice_lock);
	inw(instance->irq_reset_reg);
	line_value = inw(instance->port_reg);

	irq_status = instance->line_value ^ line_value;

	// Make extended information.
	status_val |= (0x00FF & (~(uint16_t) instance->line_value & line_value)) << 16;	//Raise
	status_val |= (0x00FF & ((uint16_t) instance->line_value & ~line_value));	//Fall

	instance->line_value = line_value;

	if (instance->rised == 0) {
		instance->status_value = irq_status;
		instance->status_value_edges = status_val;
	} else {
		instance->status_value |= irq_status;
		instance->status_value_edges |= status_val;
	}

	if (instance->filtering_flag) {	// For compare mode only.
		if (instance->compare_value == instance->line_value) {
			instance->rised = 1;
			instance->irq_count++;
		}
	} else {
		instance->rised = 1;
		instance->irq_count++;
	}

	spin_unlock(&instance->subdevice_lock);
	wake_up_interruptible_all(&instance->wait_queue);

	return IRQ_HANDLED;
}

me8100_di_subdevice_t *me8100_di_constructor(uint32_t me8100_reg_base,
					     uint32_t plx_reg_base,
					     unsigned int di_idx,
					     int irq,
					     spinlock_t * ctrl_reg_lock)
{
	me8100_di_subdevice_t *subdevice;
	int err;

	PDEBUG("executed.\n");

	/* Allocate memory for subdevice instance */
	subdevice = kmalloc(sizeof(me8100_di_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for subdevice instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me8100_di_subdevice_t));

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

	/* Save the subdevice index. */
	subdevice->di_idx = di_idx;

	/* Initialize wait queue */
	init_waitqueue_head(&subdevice->wait_queue);

	/* Register interrupt service routine. */
	subdevice->irq = irq;
	err = request_irq(subdevice->irq, me8100_isr,
#ifdef IRQF_DISABLED
			  IRQF_DISABLED | IRQF_SHARED,
#else
			  SA_INTERRUPT | SA_SHIRQ,
#endif
			  ME8100_NAME, (void *)subdevice);

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}
	PINFO("Registered irq=%d.\n", subdevice->irq);

	/* Initialize the registers */
	subdevice->ctrl_reg =
	    me8100_reg_base + ME8100_CTRL_REG_A + di_idx * ME8100_REG_OFFSET;
	subdevice->port_reg =
	    me8100_reg_base + ME8100_DI_REG_A + di_idx * ME8100_REG_OFFSET;
	subdevice->mask_reg =
	    me8100_reg_base + ME8100_MASK_REG_A + di_idx * ME8100_REG_OFFSET;
	subdevice->pattern_reg =
	    me8100_reg_base + ME8100_PATTERN_REG_A + di_idx * ME8100_REG_OFFSET;
	subdevice->din_int_reg =
	    me8100_reg_base + ME8100_INT_DI_REG_A + di_idx * ME8100_REG_OFFSET;
	subdevice->irq_reset_reg =
	    me8100_reg_base + ME8100_RES_INT_REG_A + di_idx * ME8100_REG_OFFSET;
	subdevice->irq_status_reg = plx_reg_base + PLX_INTCSR;
#ifdef MEDEBUG_DEBUG_REG
	subdevice->reg_base = me8100_reg_base;
#endif

	/* Overload base class methods. */
	subdevice->base.me_subdevice_io_irq_start = me8100_di_io_irq_start;
	subdevice->base.me_subdevice_io_irq_wait = me8100_di_io_irq_wait;
	subdevice->base.me_subdevice_io_irq_stop = me8100_di_io_irq_stop;
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me8100_di_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config =
	    me8100_di_io_single_config;
	subdevice->base.me_subdevice_io_single_read = me8100_di_io_single_read;
	subdevice->base.me_subdevice_query_number_channels =
	    me8100_di_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me8100_di_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me8100_di_query_subdevice_caps;
	subdevice->base.me_subdevice_destructor = me8100_di_destructor;

	subdevice->rised = 0;
	subdevice->irq_count = 0;

	return subdevice;
}
