/**
 * @file me1000_dio.c
 *
 * @brief ME-1000 DIO subdevice instance.
 * @note Copyright (C) 2006 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 * @author Krzysztof Gantzke	(k.gantzke@meilhaus.de)
 */

/*
 * Copyright (C) 2006 Meilhaus Electronic GmbH (support@meilhaus.de)
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

#include "me1000_dio_reg.h"
#include "me1000_dio.h"

/*
 * Defines
 */
#define ME1000_DIO_MAGIC_NUMBER	0x1000	/**< The magic number of the class structure. */

/*
 * Functions
 */

static int me1000_dio_io_reset_subdevice(struct me_subdevice *subdevice,
					 struct file *filep, int flags)
{
	me1000_dio_subdevice_t *instance;
	uint32_t tmp;

	PDEBUG("executed.\n");

	instance = (me1000_dio_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ctrl_reg_lock);
	tmp = inl(instance->ctrl_reg);
	tmp &= ~(0x1 << instance->dio_idx);
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);
	spin_unlock(instance->ctrl_reg_lock);

	outl(0x00000000, instance->port_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, 0);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me1000_dio_io_single_config(struct me_subdevice *subdevice,
				       struct file *filep,
				       int channel,
				       int single_config,
				       int ref,
				       int trig_chan,
				       int trig_type, int trig_edge, int flags)
{
	me1000_dio_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	int ctrl;
	int size =
	    flags & (ME_IO_SINGLE_CONFIG_DIO_BIT | ME_IO_SINGLE_CONFIG_DIO_BYTE
		     | ME_IO_SINGLE_CONFIG_DIO_WORD |
		     ME_IO_SINGLE_CONFIG_DIO_DWORD);

	PDEBUG("executed.\n");

	instance = (me1000_dio_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ctrl_reg_lock);
	ctrl = inl(instance->ctrl_reg);

	switch (size) {
	case ME_IO_SINGLE_CONFIG_NO_FLAGS:
	case ME_IO_SINGLE_CONFIG_DIO_DWORD:
		if (channel == 0) {
			if (single_config == ME_SINGLE_CONFIG_DIO_INPUT) {
				ctrl &= ~(0x1 << instance->dio_idx);
			} else if (single_config == ME_SINGLE_CONFIG_DIO_OUTPUT) {
				ctrl |= 0x1 << instance->dio_idx;
			} else {
				PERROR("Invalid port direction.\n");
				err = ME_ERRNO_INVALID_SINGLE_CONFIG;
			}
		} else {
			PERROR("Invalid channel number.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	default:
		PERROR("Invalid flags.\n");
		err = ME_ERRNO_INVALID_FLAGS;
	}

	if (!err) {
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);
	}
	spin_unlock(instance->ctrl_reg_lock);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me1000_dio_io_single_read(struct me_subdevice *subdevice,
				     struct file *filep,
				     int channel,
				     int *value, int time_out, int flags)
{
	me1000_dio_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed.\n");

	instance = (me1000_dio_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	switch (flags) {
	case ME_IO_SINGLE_TYPE_DIO_BIT:
		if ((channel >= 0) && (channel < 32)) {
			*value = inl(instance->port_reg) & (0x1 << channel);
		} else {
			PERROR("Invalid bit number.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_TYPE_DIO_BYTE:
		if ((channel >= 0) && (channel < 4)) {
			*value =
			    (inl(instance->port_reg) >> (channel * 8)) & 0xFF;
		} else {
			PERROR("Invalid byte number.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_TYPE_DIO_WORD:
		if ((channel >= 0) && (channel < 2)) {
			*value =
			    (inl(instance->port_reg) >> (channel * 16)) &
			    0xFFFF;
		} else {
			PERROR("Invalid word number.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_NO_FLAGS:
	case ME_IO_SINGLE_TYPE_DIO_DWORD:
		if (channel == 0) {
			*value = inl(instance->port_reg);
		} else {
			PERROR("Invalid dword number.\n");
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

static int me1000_dio_io_single_write(struct me_subdevice *subdevice,
				      struct file *filep,
				      int channel,
				      int value, int time_out, int flags)
{
	me1000_dio_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint32_t config;
	uint32_t state;

	PDEBUG("executed.\n");

	instance = (me1000_dio_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ctrl_reg_lock);
	config = inl(instance->ctrl_reg) & (0x1 << instance->dio_idx);
	switch (flags) {
	case ME_IO_SINGLE_TYPE_DIO_BIT:
		if ((channel >= 0) && (channel < 32)) {
			if (config) {
				state = inl(instance->port_reg);
				state =
				    value ? (state | (0x1 << channel)) : (state
									  &
									  ~(0x1
									    <<
									    channel));
				outl(state, instance->port_reg);
				PDEBUG_REG("port_reg outl(0x%lX+0x%lX)=0x%x\n",
					   instance->reg_base,
					   instance->port_reg -
					   instance->reg_base, state);
			} else {
				PERROR("Port is not in output mode.\n");
				err = ME_ERRNO_PREVIOUS_CONFIG;
			}
		} else {
			PERROR("Invalid bit number.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_TYPE_DIO_BYTE:
		if ((channel >= 0) && (channel < 4)) {
			if (config) {
				state = inl(instance->port_reg);
				state &= ~(0xFF << (channel * 8));
				state |= (value & 0xFF) << (channel * 8);
				outl(state, instance->port_reg);
				PDEBUG_REG("port_reg outl(0x%lX+0x%lX)=0x%x\n",
					   instance->reg_base,
					   instance->port_reg -
					   instance->reg_base, state);
			} else {
				PERROR("Port is not in output mode.\n");
				err = ME_ERRNO_PREVIOUS_CONFIG;
			}
		} else {
			PERROR("Invalid byte number.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_TYPE_DIO_WORD:
		if ((channel >= 0) && (channel < 2)) {
			if (config) {
				state = inl(instance->port_reg);
				state &= ~(0xFFFF << (channel * 16));
				state |= (value & 0xFFFF) << (channel * 16);
				outl(state, instance->port_reg);
				PDEBUG_REG("port_reg outl(0x%lX+0x%lX)=0x%x\n",
					   instance->reg_base,
					   instance->port_reg -
					   instance->reg_base, state);
			} else {
				PERROR("Port is not in output mode.\n");
				err = ME_ERRNO_PREVIOUS_CONFIG;
			}
		} else {
			PERROR("Invalid word number.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_NO_FLAGS:
	case ME_IO_SINGLE_TYPE_DIO_DWORD:
		if (channel == 0) {
			if (config) {
				outl(value, instance->port_reg);
				PDEBUG_REG("port_reg outl(0x%lX+0x%lX)=0x%x\n",
					   instance->reg_base,
					   instance->port_reg -
					   instance->reg_base, value);
			} else {
				PERROR("Port is not in output mode.\n");
				err = ME_ERRNO_PREVIOUS_CONFIG;
			}
		} else {
			PERROR("Invalid dword number.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	default:
		PERROR("Invalid flags specified.\n");
		err = ME_ERRNO_INVALID_FLAGS;
	}
	spin_unlock(instance->ctrl_reg_lock);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me1000_dio_query_number_channels(struct me_subdevice *subdevice,
					    int *number)
{
	PDEBUG("executed.\n");
	*number = ME1000_DIO_NUMBER_CHANNELS;
	return ME_ERRNO_SUCCESS;
}

static int me1000_dio_query_subdevice_type(struct me_subdevice *subdevice,
					   int *type, int *subtype)
{
	PDEBUG("executed.\n");
	*type = ME_TYPE_DIO;
	*subtype = ME_SUBTYPE_SINGLE;
	return ME_ERRNO_SUCCESS;
}

static int me1000_dio_query_subdevice_caps(struct me_subdevice *subdevice,
					   int *caps)
{
	me1000_dio_subdevice_t *instance;

	PDEBUG("executed.\n");

	instance = (me1000_dio_subdevice_t *) subdevice;

	*caps = ME_CAPS_DIO_DIR_DWORD;

	return ME_ERRNO_SUCCESS;
}

me1000_dio_subdevice_t *me1000_dio_constructor(uint32_t reg_base,
					       unsigned int dio_idx,
					       spinlock_t * ctrl_reg_lock)
{
	me1000_dio_subdevice_t *subdevice;
	int err;

	PDEBUG("executed.\n");

	/* Allocate memory for subdevice instance */
	subdevice = kmalloc(sizeof(me1000_dio_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for ME-1000 DIO instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me1000_dio_subdevice_t));

	/* Check if counter index is out of range */

	if (dio_idx >= ME1000_DIO_NUMBER_PORTS) {
		PERROR("DIO index is out of range.\n");
		kfree(subdevice);
		return NULL;
	}

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

	/* Save the DIO index */
	subdevice->dio_idx = dio_idx;

	/* Initialize registers. */
#ifdef MEDEBUG_DEBUG_REG
	subdevice->reg_base = reg_base;
#endif
	subdevice->ctrl_reg = reg_base + ME1000_PORT_MODE;
	subdevice->port_reg =
	    reg_base + ME1000_PORT + (dio_idx * ME1000_PORT_STEP);

	/* Override base class methods. */
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me1000_dio_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config =
	    me1000_dio_io_single_config;
	subdevice->base.me_subdevice_io_single_read = me1000_dio_io_single_read;
	subdevice->base.me_subdevice_io_single_write =
	    me1000_dio_io_single_write;
	subdevice->base.me_subdevice_query_number_channels =
	    me1000_dio_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me1000_dio_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me1000_dio_query_subdevice_caps;

	return subdevice;
}
