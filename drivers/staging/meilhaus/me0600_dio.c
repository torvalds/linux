/**
 * @file me0600_dio.c
 *
 * @brief ME-630 digital input/output subdevice instance.
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
#include <linux/io.h>
#include <linux/types.h>

#include "medefines.h"
#include "meinternal.h"
#include "meerror.h"

#include "medebug.h"
#include "me0600_dio_reg.h"
#include "me0600_dio.h"

/*
 * Defines
 */

/*
 * Functions
 */

static int me0600_dio_io_reset_subdevice(struct me_subdevice *subdevice,
					 struct file *filep, int flags)
{
	me0600_dio_subdevice_t *instance;
	uint8_t mode;

	PDEBUG("executed.\n");

	instance = (me0600_dio_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ctrl_reg_lock);
	mode = inb(instance->ctrl_reg);
	mode &= ~(0x3 << (instance->dio_idx * 2));
	outb(mode, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, mode);
	spin_unlock(instance->ctrl_reg_lock);

	outb(0x00, instance->port_reg);
	PDEBUG_REG("port_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->port_reg - instance->reg_base, 0x00);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me0600_dio_io_single_config(me_subdevice_t *subdevice,
				       struct file *filep,
				       int channel,
				       int single_config,
				       int ref,
				       int trig_chan,
				       int trig_type, int trig_edge, int flags)
{
	me0600_dio_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint8_t mode;
	int size =
	    flags & (ME_IO_SINGLE_CONFIG_DIO_BIT | ME_IO_SINGLE_CONFIG_DIO_BYTE
		     | ME_IO_SINGLE_CONFIG_DIO_WORD |
		     ME_IO_SINGLE_CONFIG_DIO_DWORD);

	PDEBUG("executed.\n");

	instance = (me0600_dio_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ctrl_reg_lock);
	mode = inb(instance->ctrl_reg);
	switch (size) {
	case ME_IO_SINGLE_CONFIG_NO_FLAGS:
	case ME_IO_SINGLE_CONFIG_DIO_BYTE:
		if (channel == 0) {
			if (single_config == ME_SINGLE_CONFIG_DIO_INPUT) {
				mode &=
				    ~((ME0600_DIO_CONFIG_BIT_OUT_0) <<
				      (instance->dio_idx * 2));
			} else if (single_config == ME_SINGLE_CONFIG_DIO_OUTPUT) {
				mode &=
				    ~((ME0600_DIO_CONFIG_BIT_OUT_0) <<
				      (instance->dio_idx * 2));
				mode |=
				    ME0600_DIO_CONFIG_BIT_OUT_0 << (instance->
								    dio_idx *
								    2);
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
		PERROR("Invalid flags.\n");
		err = ME_ERRNO_INVALID_FLAGS;
	}

	if (!err) {
		outb(mode, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, mode);
	}
	spin_unlock(instance->ctrl_reg_lock);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me0600_dio_io_single_read(me_subdevice_t *subdevice,
				     struct file *filep,
				     int channel,
				     int *value, int time_out, int flags)
{
	me0600_dio_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint8_t mode;

	PDEBUG("executed.\n");

	instance = (me0600_dio_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ctrl_reg_lock);
	switch (flags) {
	case ME_IO_SINGLE_TYPE_DIO_BIT:
		if ((channel >= 0) && (channel < 8)) {
			mode =
			    inb(instance->
				ctrl_reg) & ((ME0600_DIO_CONFIG_BIT_OUT_0) <<
					     (instance->dio_idx * 2));

			if ((mode ==
			     (ME0600_DIO_CONFIG_BIT_OUT_0 <<
			      (instance->dio_idx * 2))) || !mode) {
				*value =
				    inb(instance->
					port_reg) & (0x0001 << channel);
			} else {
				PERROR("Port not in output or input mode.\n");
				err = ME_ERRNO_PREVIOUS_CONFIG;
			}
		} else {
			PERROR("Invalid bit number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}

		break;

	case ME_IO_SINGLE_NO_FLAGS:
	case ME_IO_SINGLE_TYPE_DIO_BYTE:
		if (channel == 0) {
			mode =
			    inb(instance->
				ctrl_reg) & ((ME0600_DIO_CONFIG_BIT_OUT_0) <<
					     (instance->dio_idx * 2));

			if ((mode ==
			     (ME0600_DIO_CONFIG_BIT_OUT_0 <<
			      (instance->dio_idx * 2))) || !mode) {
				*value = inb(instance->port_reg) & 0x00FF;
			} else {
				PERROR("Port not in output or input mode.\n");
				err = ME_ERRNO_PREVIOUS_CONFIG;
			}
		} else {
			PERROR("Invalid byte number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}

		break;

	default:
		PERROR("Invalid flags specified.\n");

		err = ME_ERRNO_INVALID_FLAGS;

		break;
	}
	spin_unlock(instance->ctrl_reg_lock);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me0600_dio_io_single_write(me_subdevice_t *subdevice,
				      struct file *filep,
				      int channel,
				      int value, int time_out, int flags)
{
	me0600_dio_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint8_t mode;
	uint8_t byte;

	PDEBUG("executed.\n");

	instance = (me0600_dio_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ctrl_reg_lock);
	switch (flags) {

	case ME_IO_SINGLE_TYPE_DIO_BIT:
		if ((channel >= 0) && (channel < 8)) {
			mode =
			    inb(instance->
				ctrl_reg) & ((ME0600_DIO_CONFIG_BIT_OUT_0) <<
					     (instance->dio_idx * 2));

			if (mode ==
			    (ME0600_DIO_CONFIG_BIT_OUT_0 <<
			     (instance->dio_idx * 2))) {
				byte = inb(instance->port_reg);

				if (value)
					byte |= 0x1 << channel;
				else
					byte &= ~(0x1 << channel);

				outb(byte, instance->port_reg);
			} else {
				PERROR("Port not in output or input mode.\n");
				err = ME_ERRNO_PREVIOUS_CONFIG;
			}
		} else {
			PERROR("Invalid bit number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}

		break;

	case ME_IO_SINGLE_NO_FLAGS:
	case ME_IO_SINGLE_TYPE_DIO_BYTE:
		if (channel == 0) {
			mode =
			    inb(instance->
				ctrl_reg) & ((ME0600_DIO_CONFIG_BIT_OUT_0) <<
					     (instance->dio_idx * 2));

			if (mode ==
			    (ME0600_DIO_CONFIG_BIT_OUT_0 <<
			     (instance->dio_idx * 2))) {
				outb(value, instance->port_reg);
			} else {
				PERROR("Port not in output or input mode.\n");
				err = ME_ERRNO_PREVIOUS_CONFIG;
			}
		} else {
			PERROR("Invalid byte number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}

		break;

	default:
		PERROR("Invalid flags specified.\n");

		err = ME_ERRNO_INVALID_FLAGS;

		break;
	}
	spin_unlock(instance->ctrl_reg_lock);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me0600_dio_query_number_channels(me_subdevice_t *subdevice,
					    int *number)
{
	PDEBUG("executed.\n");
	*number = 8;
	return ME_ERRNO_SUCCESS;
}

static int me0600_dio_query_subdevice_type(me_subdevice_t *subdevice,
					   int *type, int *subtype)
{
	PDEBUG("executed.\n");
	*type = ME_TYPE_DIO;
	*subtype = ME_SUBTYPE_SINGLE;
	return ME_ERRNO_SUCCESS;
}

static int me0600_dio_query_subdevice_caps(me_subdevice_t *subdevice,
					   int *caps)
{
	PDEBUG("executed.\n");
	*caps = ME_CAPS_DIO_DIR_BYTE;
	return ME_ERRNO_SUCCESS;
}

me0600_dio_subdevice_t *me0600_dio_constructor(uint32_t reg_base,
					       unsigned int dio_idx,
					       spinlock_t *ctrl_reg_lock)
{
	me0600_dio_subdevice_t *subdevice;
	int err;

	PDEBUG("executed.\n");

	/* Allocate memory for subdevice instance */
	subdevice = kmalloc(sizeof(me0600_dio_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for subdevice instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me0600_dio_subdevice_t));

	/* Initialize subdevice base class */
	err = me_subdevice_init(&subdevice->base);

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}
	/* Initialize spin locks. */
	spin_lock_init(&subdevice->subdevice_lock);

	subdevice->ctrl_reg_lock = ctrl_reg_lock;

	/* Save digital i/o index */
	subdevice->dio_idx = dio_idx;

	/* Save the subdevice index */
	subdevice->ctrl_reg = reg_base + ME0600_DIO_CONFIG_REG;
	subdevice->port_reg = reg_base + ME0600_DIO_PORT_REG + dio_idx;
#ifdef MEDEBUG_DEBUG_REG
	subdevice->reg_base = reg_base;
#endif

	/* Overload base class methods. */
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me0600_dio_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config =
	    me0600_dio_io_single_config;
	subdevice->base.me_subdevice_io_single_read = me0600_dio_io_single_read;
	subdevice->base.me_subdevice_io_single_write =
	    me0600_dio_io_single_write;
	subdevice->base.me_subdevice_query_number_channels =
	    me0600_dio_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me0600_dio_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me0600_dio_query_subdevice_caps;

	return subdevice;
}
