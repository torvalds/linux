/**
 * @file me8100_do.c
 *
 * @brief ME-8100 digital output subdevice instance.
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

#include "medefines.h"
#include "meinternal.h"
#include "meerror.h"

#include "medebug.h"
#include "me8100_reg.h"
#include "me8100_do_reg.h"
#include "me8100_do.h"

/*
 * Defines
 */

/*
 * Functions
 */

static int me8100_do_io_reset_subdevice(struct me_subdevice *subdevice,
					struct file *filep, int flags)
{
	me8100_do_subdevice_t *instance;
	uint16_t ctrl;

	PDEBUG("executed.\n");

	instance = (me8100_do_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ctrl_reg_lock);
	ctrl = inw(instance->ctrl_reg);
	ctrl &= ME8100_DIO_CTRL_BIT_INTB_1 | ME8100_DIO_CTRL_BIT_INTB_0;
	outw(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outw(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);
	spin_unlock(instance->ctrl_reg_lock);
	outw(0, instance->port_reg);
	instance->port_reg_mirror = 0;
	PDEBUG_REG("port_reg outw(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->port_reg - instance->reg_base, 0);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me8100_do_io_single_config(me_subdevice_t * subdevice,
				      struct file *filep,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags)
{
	me8100_do_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	int config;

	PDEBUG("executed.\n");

	instance = (me8100_do_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ctrl_reg_lock);
	config = inw(instance->ctrl_reg);
	switch (flags) {
	case ME_IO_SINGLE_CONFIG_NO_FLAGS:
	case ME_IO_SINGLE_CONFIG_DIO_WORD:
		if (channel == 0) {
			if (single_config ==
			    ME_SINGLE_CONFIG_DIO_HIGH_IMPEDANCE) {
				config &= ~(ME8100_DIO_CTRL_BIT_ENABLE_DIO);
			} else if (single_config == ME_SINGLE_CONFIG_DIO_SINK) {
				config |= ME8100_DIO_CTRL_BIT_ENABLE_DIO;
				config &= ~ME8100_DIO_CTRL_BIT_SOURCE;
			} else if (single_config == ME_SINGLE_CONFIG_DIO_SOURCE) {
				config |=
				    ME8100_DIO_CTRL_BIT_ENABLE_DIO |
				    ME8100_DIO_CTRL_BIT_SOURCE;
			} else {
				PERROR
				    ("Invalid port configuration specified.\n");
				err = ME_ERRNO_INVALID_SINGLE_CONFIG;
			}
		} else {
			PERROR("Invalid word number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	default:
		PERROR("Invalid flags specified.\n");
		err = ME_ERRNO_INVALID_FLAGS;
	}

	if (!err) {
		outw(config, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, config);
	}

	spin_unlock(instance->ctrl_reg_lock);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8100_do_io_single_read(me_subdevice_t * subdevice,
				    struct file *filep,
				    int channel,
				    int *value, int time_out, int flags)
{
	me8100_do_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed.\n");

	instance = (me8100_do_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	switch (flags) {
	case ME_IO_SINGLE_TYPE_DIO_BIT:
		if ((channel >= 0) && (channel < 16)) {
			*value = instance->port_reg_mirror & (0x1 << channel);
		} else {
			PERROR("Invalid bit number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_TYPE_DIO_BYTE:
		if (channel == 0) {
			*value = instance->port_reg_mirror & 0xFF;
		} else if (channel == 1) {
			*value = (instance->port_reg_mirror >> 8) & 0xFF;
		} else {
			PERROR("Invalid byte number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_NO_FLAGS:
	case ME_IO_SINGLE_TYPE_DIO_WORD:
		if (channel == 0) {
			*value = instance->port_reg_mirror;
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

static int me8100_do_io_single_write(me_subdevice_t * subdevice,
				     struct file *filep,
				     int channel,
				     int value, int time_out, int flags)
{
	me8100_do_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed.\n");

	instance = (me8100_do_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	switch (flags) {
	case ME_IO_SINGLE_TYPE_DIO_BIT:
		if ((channel >= 0) && (channel < 16)) {
			instance->port_reg_mirror =
			    value ? (instance->
				     port_reg_mirror | (0x1 << channel))
			    : (instance->port_reg_mirror & ~(0x1 << channel));
			outw(instance->port_reg_mirror, instance->port_reg);
			PDEBUG_REG("port_reg outw(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->port_reg - instance->reg_base,
				   instance->port_reg_mirror);
		} else {
			PERROR("Invalid bit number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_TYPE_DIO_BYTE:
		if (channel == 0) {
			instance->port_reg_mirror &= ~0xFF;
			instance->port_reg_mirror |= value & 0xFF;
			outw(instance->port_reg_mirror, instance->port_reg);
			PDEBUG_REG("port_reg outw(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->port_reg - instance->reg_base,
				   instance->port_reg_mirror);
		} else if (channel == 1) {
			instance->port_reg_mirror &= ~0xFF00;
			instance->port_reg_mirror |= (value << 8) & 0xFF00;
			outw(instance->port_reg_mirror, instance->port_reg);
			PDEBUG_REG("port_reg outw(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->port_reg - instance->reg_base,
				   instance->port_reg_mirror);
		} else {
			PERROR("Invalid byte number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_NO_FLAGS:
	case ME_IO_SINGLE_TYPE_DIO_WORD:
		if (channel == 0) {
			instance->port_reg_mirror = value;
			outw(value, instance->port_reg);
			PDEBUG_REG("port_reg outw(0x%lX+0x%lX)=0x%x\n",
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
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8100_do_query_number_channels(me_subdevice_t * subdevice,
					   int *number)
{
	PDEBUG("executed.\n");
	*number = 16;
	return ME_ERRNO_SUCCESS;
}

static int me8100_do_query_subdevice_type(me_subdevice_t * subdevice,
					  int *type, int *subtype)
{
	PDEBUG("executed.\n");
	*type = ME_TYPE_DO;
	*subtype = ME_SUBTYPE_SINGLE;
	return ME_ERRNO_SUCCESS;
}

static int me8100_do_query_subdevice_caps(me_subdevice_t * subdevice, int *caps)
{
	PDEBUG("executed.\n");
	*caps = ME_CAPS_DIO_SINK_SOURCE;
	return ME_ERRNO_SUCCESS;
}

me8100_do_subdevice_t *me8100_do_constructor(uint32_t reg_base,
					     unsigned int do_idx,
					     spinlock_t * ctrl_reg_lock)
{
	me8100_do_subdevice_t *subdevice;
	int err;

	PDEBUG("executed.\n");

	/* Allocate memory for subdevice instance */
	subdevice = kmalloc(sizeof(me8100_do_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for subdevice instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me8100_do_subdevice_t));

	/* Initialize subdevice base class */
	err = me_subdevice_init(&subdevice->base);

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}

	/* Initialize registers */
	if (do_idx == 0) {
		subdevice->port_reg = reg_base + ME8100_DO_REG_A;
		subdevice->ctrl_reg = reg_base + ME8100_CTRL_REG_A;
	} else if (do_idx == 1) {
		subdevice->port_reg = reg_base + ME8100_DO_REG_B;
		subdevice->ctrl_reg = reg_base + ME8100_CTRL_REG_B;
	} else {
		PERROR("Wrong subdevice idx=%d.\n", do_idx);
		kfree(subdevice);
		return NULL;
	}
#ifdef MEDEBUG_DEBUG_REG
	subdevice->reg_base = reg_base;
#endif

	// Initialize spin locks.
	spin_lock_init(&subdevice->subdevice_lock);
	subdevice->ctrl_reg_lock = ctrl_reg_lock;

	/* Save the subdevice index */
	subdevice->do_idx = do_idx;

	/* Overload base class methods. */
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me8100_do_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config =
	    me8100_do_io_single_config;
	subdevice->base.me_subdevice_io_single_read = me8100_do_io_single_read;
	subdevice->base.me_subdevice_io_single_write =
	    me8100_do_io_single_write;
	subdevice->base.me_subdevice_query_number_channels =
	    me8100_do_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me8100_do_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me8100_do_query_subdevice_caps;

	return subdevice;
}
