/**
 * @file me8200_di.c
 *
 * @brief ME-8200 digital input subdevice instance.
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

///Includes
#include <linux/module.h>

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/interrupt.h>

#include "medefines.h"
#include "meerror.h"

#include "meids.h"
#include "medebug.h"
#include "me8200_reg.h"
#include "me8200_di_reg.h"
#include "me8200_di.h"

/// Defines
static void me8200_di_destructor(struct me_subdevice *subdevice);
static int me8200_di_io_irq_start(me_subdevice_t *subdevice,
				  struct file *filep,
				  int channel,
				  int irq_source,
				  int irq_edge, int irq_arg, int flags);
static int me8200_di_io_irq_wait(me_subdevice_t *subdevice,
				 struct file *filep,
				 int channel,
				 int *irq_count,
				 int *value, int time_out, int flags);
static int me8200_di_io_irq_stop(me_subdevice_t *subdevice,
				 struct file *filep, int channel, int flags);
static int me8200_di_io_single_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags);
static int me8200_di_io_single_read(me_subdevice_t *subdevice,
				    struct file *filep,
				    int channel,
				    int *value, int time_out, int flags);
static int me8200_di_io_reset_subdevice(struct me_subdevice *subdevice,
					struct file *filep, int flags);
static int me8200_di_query_number_channels(me_subdevice_t *subdevice,
					   int *number);
static int me8200_di_query_subdevice_type(me_subdevice_t *subdevice,
					  int *type, int *subtype);
static int me8200_di_query_subdevice_caps(me_subdevice_t *subdevice,
					  int *caps);
static irqreturn_t me8200_isr(int irq, void *dev_id);
static irqreturn_t me8200_isr_EX(int irq, void *dev_id);
static void me8200_di_check_version(me8200_di_subdevice_t *instance,
				    unsigned long addr);

///Functions
static int me8200_di_io_irq_start(me_subdevice_t *subdevice,
				  struct file *filep,
				  int channel,
				  int irq_source,
				  int irq_edge, int irq_arg, int flags)
{
	me8200_di_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	volatile uint8_t tmp;
	unsigned long status;

	PDEBUG("executed.\n");

	instance = (me8200_di_subdevice_t *) subdevice;

	if (irq_source == ME_IRQ_SOURCE_DIO_PATTERN) {
		if (flags &
		    ~(ME_IO_IRQ_START_PATTERN_FILTERING |
		      ME_IO_IRQ_START_DIO_BYTE)) {
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
		      ME_IO_IRQ_START_DIO_BYTE)) {
			PERROR("Invalid flag specified.\n");
			return ME_ERRNO_INVALID_FLAGS;
		}

		if ((irq_edge != ME_IRQ_EDGE_RISING)
		    && (irq_edge != ME_IRQ_EDGE_FALLING)
		    && (irq_edge != ME_IRQ_EDGE_ANY)) {
			PERROR("Invalid irq edge specified.\n");
			return ME_ERRNO_INVALID_IRQ_EDGE;
		}

		if (!(irq_arg & 0xFF)) {
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

	spin_lock_irqsave(&instance->subdevice_lock, status);
	if (irq_source == ME_IRQ_SOURCE_DIO_PATTERN) {
		outb(irq_arg, instance->compare_reg);
		PDEBUG_REG("compare_reg outb(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->compare_reg - instance->reg_base, irq_arg);
		outb(0xFF, instance->mask_reg);
		PDEBUG_REG("mask_reg outb(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->mask_reg - instance->reg_base, 0xff);
		instance->compare_value = irq_arg;
		instance->filtering_flag =
		    (flags & ME_IO_IRQ_START_PATTERN_FILTERING) ? 1 : 0;
	}
	if (irq_source == ME_IRQ_SOURCE_DIO_MASK) {
		outb(irq_arg, instance->mask_reg);
		PDEBUG_REG("mask_reg outb(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->mask_reg - instance->reg_base, irq_arg);
		instance->filtering_flag = 0;
	}

	spin_lock(instance->irq_mode_lock);
	tmp = inb(instance->irq_mode_reg);
	tmp &=
	    ~(ME8200_IRQ_MODE_MASK <<
	      (ME8200_IRQ_MODE_DI_SHIFT * instance->di_idx));
	if (irq_source == ME_IRQ_SOURCE_DIO_PATTERN) {
		tmp |=
		    ME8200_IRQ_MODE_MASK_COMPARE << (ME8200_IRQ_MODE_DI_SHIFT *
						     instance->di_idx);
	}

	if (irq_source == ME_IRQ_SOURCE_DIO_MASK) {
		tmp |=
		    ME8200_IRQ_MODE_MASK_MASK << (ME8200_IRQ_MODE_DI_SHIFT *
						  instance->di_idx);
	}
	outb(tmp, instance->irq_mode_reg);
	PDEBUG_REG("irq_mode_reg outb(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->irq_mode_reg - instance->reg_base, tmp);
	spin_unlock(instance->irq_mode_lock);

	spin_lock(instance->irq_ctrl_lock);
	tmp = inb(instance->irq_ctrl_reg);
	tmp |=
	    (ME8200_DI_IRQ_CTRL_BIT_CLEAR <<
	     (ME8200_DI_IRQ_CTRL_SHIFT * instance->di_idx));
	tmp |=
	    ME8200_DI_IRQ_CTRL_BIT_ENABLE << (ME8200_DI_IRQ_CTRL_SHIFT *
					      instance->di_idx);

	if (irq_source == ME_IRQ_SOURCE_DIO_MASK) {
		tmp &=
		    ~(ME8200_DI_IRQ_CTRL_MASK_EDGE <<
		      (ME8200_DI_IRQ_CTRL_SHIFT * instance->di_idx));
		if (irq_edge == ME_IRQ_EDGE_RISING) {
			tmp |=
			    ME8200_DI_IRQ_CTRL_MASK_EDGE_RISING <<
			    (ME8200_DI_IRQ_CTRL_SHIFT * instance->di_idx);
		} else if (irq_edge == ME_IRQ_EDGE_FALLING) {
			tmp |=
			    ME8200_DI_IRQ_CTRL_MASK_EDGE_FALLING <<
			    (ME8200_DI_IRQ_CTRL_SHIFT * instance->di_idx);
		} else if (irq_edge == ME_IRQ_EDGE_ANY) {
			tmp |=
			    ME8200_DI_IRQ_CTRL_MASK_EDGE_ANY <<
			    (ME8200_DI_IRQ_CTRL_SHIFT * instance->di_idx);
		}
	}
	outb(tmp, instance->irq_ctrl_reg);
	PDEBUG_REG("irq_ctrl_reg outb(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->irq_ctrl_reg - instance->reg_base, tmp);
	tmp &=
	    ~(ME8200_DI_IRQ_CTRL_BIT_CLEAR <<
	      (ME8200_DI_IRQ_CTRL_SHIFT * instance->di_idx));
	outb(tmp, instance->irq_ctrl_reg);
	PDEBUG_REG("irq_ctrl_reg outb(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->irq_ctrl_reg - instance->reg_base, tmp);

	instance->line_value = inb(instance->port_reg);
	spin_unlock(instance->irq_ctrl_lock);

	instance->rised = 0;
	instance->status_value = 0;
	instance->status_value_edges = 0;
	instance->status_flag = flags & ME_IO_IRQ_START_EXTENDED_STATUS;
	spin_unlock_irqrestore(&instance->subdevice_lock, status);
	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8200_di_io_irq_wait(me_subdevice_t *subdevice,
				 struct file *filep,
				 int channel,
				 int *irq_count,
				 int *value, int time_out, int flags)
{
	me8200_di_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	long t = 0;
	unsigned long cpu_flags;
	int count;

	PDEBUG("executed.\n");
	PDEVELOP("PID: %d.\n", current->pid);

	instance = (me8200_di_subdevice_t *) subdevice;

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
		count = instance->count;

		if (time_out) {
			t = wait_event_interruptible_timeout(instance->
							     wait_queue,
							     ((count !=
							       instance->count)
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
						 ((count != instance->count)
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
	*irq_count = instance->count;
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

static int me8200_di_io_irq_stop(me_subdevice_t *subdevice,
				 struct file *filep, int channel, int flags)
{
	me8200_di_subdevice_t *instance;
	uint8_t tmp;
	unsigned long status;

	PDEBUG("executed.\n");

	instance = (me8200_di_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (channel) {
		PERROR("Invalid channel specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	ME_SUBDEVICE_ENTER spin_lock_irqsave(&instance->subdevice_lock, status);
	spin_lock(instance->irq_ctrl_lock);
	tmp = inb(instance->irq_ctrl_reg);
	tmp |=
	    (ME8200_DI_IRQ_CTRL_BIT_ENABLE <<
	     (ME8200_DI_IRQ_CTRL_SHIFT * instance->di_idx));
	outb(tmp, instance->irq_ctrl_reg);
	PDEBUG_REG("irq_ctrl_reg outb(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->irq_ctrl_reg - instance->reg_base, tmp);
	tmp &=
	    ~(ME8200_DI_IRQ_CTRL_BIT_ENABLE <<
	      (ME8200_DI_IRQ_CTRL_SHIFT * instance->di_idx));
	tmp |=
	    (ME8200_DI_IRQ_CTRL_BIT_CLEAR <<
	     (ME8200_DI_IRQ_CTRL_SHIFT * instance->di_idx));
//                      tmp &= ~(ME8200_DI_IRQ_CTRL_BIT_CLEAR << (ME8200_DI_IRQ_CTRL_SHIFT * instance->di_idx));
	outb(tmp, instance->irq_ctrl_reg);
	PDEBUG_REG("irq_ctrl_reg outb(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->irq_ctrl_reg - instance->reg_base, tmp);
	spin_unlock(instance->irq_ctrl_lock);

	instance->rised = -1;
	instance->status_value = 0;
	instance->status_value_edges = 0;
	instance->filtering_flag = 0;
	spin_unlock_irqrestore(&instance->subdevice_lock, status);
	wake_up_interruptible_all(&instance->wait_queue);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me8200_di_io_single_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags)
{
	me8200_di_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long status;

	PDEBUG("executed.\n");

	instance = (me8200_di_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, status);

	switch (flags) {
	case ME_IO_SINGLE_CONFIG_NO_FLAGS:
	case ME_IO_SINGLE_CONFIG_DIO_BYTE:
		if (channel == 0) {
			if (single_config == ME_SINGLE_CONFIG_DIO_INPUT) {
			} else {
				PERROR("Invalid port direction specified.\n");
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

	spin_unlock_irqrestore(&instance->subdevice_lock, status);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8200_di_io_single_read(me_subdevice_t *subdevice,
				    struct file *filep,
				    int channel,
				    int *value, int time_out, int flags)
{
	me8200_di_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long status;

	PDEBUG("executed.\n");

	instance = (me8200_di_subdevice_t *) subdevice;

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
			PERROR("Invalid channel number.\n");
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

static int me8200_di_io_reset_subdevice(struct me_subdevice *subdevice,
					struct file *filep, int flags)
{
	me8200_di_subdevice_t *instance = (me8200_di_subdevice_t *) subdevice;

	PDEBUG("executed.\n");

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	instance->count = 0;
	return me8200_di_io_irq_stop(subdevice, filep, 0, 0);
}

static int me8200_di_query_number_channels(me_subdevice_t *subdevice,
					   int *number)
{
	PDEBUG("executed.\n");
	*number = 8;
	return ME_ERRNO_SUCCESS;
}

static int me8200_di_query_subdevice_type(me_subdevice_t *subdevice,
					  int *type, int *subtype)
{
	PDEBUG("executed.\n");
	*type = ME_TYPE_DI;
	*subtype = ME_SUBTYPE_SINGLE;
	return ME_ERRNO_SUCCESS;
}

static int me8200_di_query_subdevice_caps(me_subdevice_t *subdevice, int *caps)
{
	PDEBUG("executed.\n");
	*caps =
	    ME_CAPS_DIO_BIT_PATTERN_IRQ |
	    ME_CAPS_DIO_BIT_MASK_IRQ_EDGE_RISING |
	    ME_CAPS_DIO_BIT_MASK_IRQ_EDGE_FALLING |
	    ME_CAPS_DIO_BIT_MASK_IRQ_EDGE_ANY;
	return ME_ERRNO_SUCCESS;
}

static irqreturn_t me8200_isr(int irq, void *dev_id)
{
	me8200_di_subdevice_t *instance;
	uint8_t ctrl;
	uint8_t irq_status;
	uint8_t line_value = 0;
	uint8_t line_status = 0;
	uint32_t status_val = 0;

	instance = (me8200_di_subdevice_t *) dev_id;

	if (irq != instance->irq) {
		PERROR("Incorrect interrupt num: %d.\n", irq);
		return IRQ_NONE;
	}

	irq_status = inb(instance->irq_status_reg);
	if (!irq_status) {
		PINFO
		    ("%ld Shared interrupt. %s(): idx=%d irq_status_reg=0x%04X\n",
		     jiffies, __func__, instance->di_idx, irq_status);
		return IRQ_NONE;
	}

	PDEBUG("executed.\n");

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->irq_ctrl_lock);
	ctrl = inb(instance->irq_ctrl_reg);
	ctrl |=
	    ME8200_DI_IRQ_CTRL_BIT_CLEAR << (ME8200_DI_IRQ_CTRL_SHIFT *
					     instance->di_idx);
	outb(ctrl, instance->irq_ctrl_reg);
	PDEBUG_REG("irq_ctrl_reg outb(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->irq_ctrl_reg - instance->reg_base, ctrl);
	ctrl &=
	    ~(ME8200_DI_IRQ_CTRL_BIT_CLEAR <<
	      (ME8200_DI_IRQ_CTRL_SHIFT * instance->di_idx));
	outb(ctrl, instance->irq_ctrl_reg);
	PDEBUG_REG("irq_ctrl_reg outb(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->irq_ctrl_reg - instance->reg_base, ctrl);

	line_value = inb(instance->port_reg);
	spin_unlock(instance->irq_ctrl_lock);

	line_status = ((uint8_t) instance->line_value ^ line_value);

	// Make extended information.
	status_val |= (0x00FF & (~(uint8_t) instance->line_value & line_value)) << 16;	//Raise
	status_val |= (0x00FF & ((uint8_t) instance->line_value & ~line_value));	//Fall

	instance->line_value = (int)line_value;

	if (instance->rised == 0) {
		instance->status_value = irq_status | line_status;
		instance->status_value_edges = status_val;
	} else {
		instance->status_value |= irq_status | line_status;
		instance->status_value_edges |= status_val;
	}

	if (instance->filtering_flag) {	// For compare mode only.
		if (instance->compare_value == instance->line_value) {
			instance->rised = 1;
			instance->count++;
		}
	} else {
		instance->rised = 1;
		instance->count++;
	}
	spin_unlock(&instance->subdevice_lock);

	spin_unlock(&instance->subdevice_lock);

	wake_up_interruptible_all(&instance->wait_queue);

	return IRQ_HANDLED;
}

static irqreturn_t me8200_isr_EX(int irq, void *dev_id)
{
	me8200_di_subdevice_t *instance;
	uint8_t irq_status = 0;
	uint16_t irq_status_EX = 0;
	uint32_t status_val = 0;
	int i, j;

	instance = (me8200_di_subdevice_t *) dev_id;

	if (irq != instance->irq) {
		PERROR("Incorrect interrupt num: %d.\n", irq);
		return IRQ_NONE;
	}

	PDEBUG("executed.\n");

	//Reset latches. Copy status to extended registers.
	irq_status = inb(instance->irq_status_reg);
	PDEBUG_REG("idx=%d irq_status_reg=0x%02X\n", instance->di_idx,
		   irq_status);

	if (!irq_status) {
		PINFO
		    ("%ld Shared interrupt. %s(): idx=%d irq_status_reg=0x%04X\n",
		     jiffies, __func__, instance->di_idx, irq_status);
		return IRQ_NONE;
	}

	irq_status_EX = inb(instance->irq_status_low_reg);
	irq_status_EX |= (inb(instance->irq_status_high_reg) << 8);

	PDEVELOP("EXTENDED REG: 0x%04x\n", irq_status_EX);
	instance->line_value = inb(instance->port_reg);

	// Format extended information.
	for (i = 0, j = 0; i < 8; i++, j += 2) {
		status_val |= ((0x01 << j) & irq_status_EX) >> (j - i);	//Fall
		status_val |= ((0x01 << (j + 1)) & irq_status_EX) << (15 - j + i);	//Raise
	}

	spin_lock(&instance->subdevice_lock);
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
			instance->count++;
		}
	} else {
		instance->rised = 1;
		instance->count++;
	}
	spin_unlock(&instance->subdevice_lock);

	wake_up_interruptible_all(&instance->wait_queue);

	return IRQ_HANDLED;
}

static void me8200_di_destructor(struct me_subdevice *subdevice)
{
	me8200_di_subdevice_t *instance;

	PDEBUG("executed.\n");

	instance = (me8200_di_subdevice_t *) subdevice;

	free_irq(instance->irq, (void *)instance);
	me_subdevice_deinit(&instance->base);
	kfree(instance);
}

me8200_di_subdevice_t *me8200_di_constructor(uint32_t me8200_regbase,
					     unsigned int di_idx,
					     int irq,
					     spinlock_t *irq_ctrl_lock,
					     spinlock_t *irq_mode_lock)
{
	me8200_di_subdevice_t *subdevice;
	int err;

	PDEBUG("executed.\n");

	/* Allocate memory for subdevice instance */
	subdevice = kmalloc(sizeof(me8200_di_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for subdevice instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me8200_di_subdevice_t));

	/* Initialize subdevice base class */
	err = me_subdevice_init(&subdevice->base);

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}
	// Check firmware version.
	me8200_di_check_version(subdevice,
				me8200_regbase + ME8200_FIRMWARE_VERSION_REG);

	// Initialize spin locks.
	spin_lock_init(&subdevice->subdevice_lock);

	subdevice->irq_ctrl_lock = irq_ctrl_lock;
	subdevice->irq_mode_lock = irq_mode_lock;

	/* Save the subdevice index. */
	subdevice->di_idx = di_idx;

	/* Initialize registers */
	if (di_idx == 0) {
		subdevice->port_reg = me8200_regbase + ME8200_DI_PORT_0_REG;
		subdevice->mask_reg = me8200_regbase + ME8200_DI_MASK_0_REG;
		subdevice->compare_reg =
		    me8200_regbase + ME8200_DI_COMPARE_0_REG;
		subdevice->irq_status_reg =
		    me8200_regbase + ME8200_DI_CHANGE_0_REG;

		subdevice->irq_status_low_reg =
		    me8200_regbase + ME8200_DI_EXTEND_CHANGE_0_LOW_REG;
		subdevice->irq_status_high_reg =
		    me8200_regbase + ME8200_DI_EXTEND_CHANGE_0_HIGH_REG;
	} else if (di_idx == 1) {
		subdevice->port_reg = me8200_regbase + ME8200_DI_PORT_1_REG;
		subdevice->mask_reg = me8200_regbase + ME8200_DI_MASK_1_REG;
		subdevice->compare_reg =
		    me8200_regbase + ME8200_DI_COMPARE_1_REG;
		subdevice->irq_status_reg =
		    me8200_regbase + ME8200_DI_CHANGE_1_REG;

		subdevice->irq_status_low_reg =
		    me8200_regbase + ME8200_DI_EXTEND_CHANGE_1_LOW_REG;
		subdevice->irq_status_high_reg =
		    me8200_regbase + ME8200_DI_EXTEND_CHANGE_1_HIGH_REG;
	} else {
		PERROR("Wrong subdevice idx=%d.\n", di_idx);
		kfree(subdevice);
		return NULL;
	}
	subdevice->irq_ctrl_reg = me8200_regbase + ME8200_DI_IRQ_CTRL_REG;
	subdevice->irq_mode_reg = me8200_regbase + ME8200_IRQ_MODE_REG;
#ifdef MEDEBUG_DEBUG_REG
	subdevice->reg_base = me8200_regbase;
#endif

	/* Initialize wait queue */
	init_waitqueue_head(&subdevice->wait_queue);

	/* Overload base class methods. */
	subdevice->base.me_subdevice_io_irq_start = me8200_di_io_irq_start;
	subdevice->base.me_subdevice_io_irq_wait = me8200_di_io_irq_wait;
	subdevice->base.me_subdevice_io_irq_stop = me8200_di_io_irq_stop;
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me8200_di_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config =
	    me8200_di_io_single_config;
	subdevice->base.me_subdevice_io_single_read = me8200_di_io_single_read;
	subdevice->base.me_subdevice_query_number_channels =
	    me8200_di_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me8200_di_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me8200_di_query_subdevice_caps;
	subdevice->base.me_subdevice_destructor = me8200_di_destructor;

	subdevice->rised = 0;
	subdevice->count = 0;

	/* Register interrupt service routine. */
	subdevice->irq = irq;
	if (subdevice->version > 0) {	// NEW
		err = request_irq(subdevice->irq, me8200_isr_EX,
				  IRQF_DISABLED | IRQF_SHARED,
				  ME8200_NAME, (void *)subdevice);
	} else {		//OLD
		err = request_irq(subdevice->irq, me8200_isr,
				  IRQF_DISABLED | IRQF_SHARED,
				  ME8200_NAME, (void *)subdevice);
	}

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}
	PDEBUG("Registred irq=%d.\n", subdevice->irq);

	return subdevice;
}

static void me8200_di_check_version(me8200_di_subdevice_t *instance,
				    unsigned long addr)
{

	PDEBUG("executed.\n");
	instance->version = 0x000000FF & inb(addr);
	PDEVELOP("me8200 firmware version: %d\n", instance->version);

	/// @note Fix for wrong values in this registry.
	if ((instance->version < 0x7) || (instance->version > 0x1F))
		instance->version = 0x0;
}
