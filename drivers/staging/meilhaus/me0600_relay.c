/**
 * @file me0600_relay.c
 *
 * @brief ME-630 relay subdevice instance.
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
#include "me0600_relay_reg.h"
#include "me0600_relay.h"

/*
 * Defines
 */

/*
 * Functions
 */

static int me0600_relay_io_reset_subdevice(struct me_subdevice *subdevice,
					   struct file *filep, int flags)
{
	me0600_relay_subdevice_t *instance;

	PDEBUG("executed.\n");

	instance = (me0600_relay_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	outb(0x0, instance->port_0_reg);
	PDEBUG_REG("port_0_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->port_0_reg - instance->reg_base, 0);
	outb(0x0, instance->port_1_reg);
	PDEBUG_REG("port_1_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->port_1_reg - instance->reg_base, 0);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me0600_relay_io_single_config(me_subdevice_t *subdevice,
					 struct file *filep,
					 int channel,
					 int single_config,
					 int ref,
					 int trig_chan,
					 int trig_type,
					 int trig_edge, int flags)
{
	me0600_relay_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed.\n");

	instance = (me0600_relay_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);

	switch (flags) {
	case ME_IO_SINGLE_CONFIG_NO_FLAGS:
	case ME_IO_SINGLE_CONFIG_DIO_WORD:
		if (channel == 0) {
			if (single_config != ME_SINGLE_CONFIG_DIO_OUTPUT) {
				PERROR("Invalid word direction specified.\n");
				err = ME_ERRNO_INVALID_SINGLE_CONFIG;
			}
		} else {
			PERROR("Invalid channel specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}

		break;

	default:
		PERROR("Invalid flags specified.\n");

		err = ME_ERRNO_INVALID_FLAGS;

		break;
	}

	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me0600_relay_io_single_read(me_subdevice_t *subdevice,
				       struct file *filep,
				       int channel,
				       int *value, int time_out, int flags)
{
	me0600_relay_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed.\n");

	instance = (me0600_relay_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);

	switch (flags) {

	case ME_IO_SINGLE_TYPE_DIO_BIT:
		if ((channel >= 0) && (channel < 8)) {
			*value = inb(instance->port_0_reg) & (0x1 << channel);
		} else if ((channel >= 8) && (channel < 16)) {
			*value =
			    inb(instance->port_1_reg) & (0x1 << (channel - 8));
		} else {
			PERROR("Invalid bit number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}

		break;

	case ME_IO_SINGLE_TYPE_DIO_BYTE:
		if (channel == 0) {
			*value = inb(instance->port_0_reg);
		} else if (channel == 1) {
			*value = inb(instance->port_1_reg);
		} else {
			PERROR("Invalid byte number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}

		break;

	case ME_IO_SINGLE_NO_FLAGS:
	case ME_IO_SINGLE_TYPE_DIO_WORD:
		if (channel == 0) {
			*value = (uint32_t) inb(instance->port_1_reg) << 8;
			*value |= inb(instance->port_0_reg);
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

static int me0600_relay_io_single_write(me_subdevice_t *subdevice,
					struct file *filep,
					int channel,
					int value, int time_out, int flags)
{
	me0600_relay_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint8_t state;

	PDEBUG("executed.\n");

	instance = (me0600_relay_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);

	switch (flags) {
	case ME_IO_SINGLE_TYPE_DIO_BIT:
		if ((channel >= 0) && (channel < 8)) {
			state = inb(instance->port_0_reg);
			state =
			    value ? (state | (0x1 << channel)) : (state &
								  ~(0x1 <<
								    channel));
			outb(state, instance->port_0_reg);
		} else if ((channel >= 8) && (channel < 16)) {
			state = inb(instance->port_1_reg);
			state =
			    value ? (state | (0x1 << (channel - 8))) : (state &
									~(0x1 <<
									  (channel
									   -
									   8)));
			outb(state, instance->port_1_reg);
		} else {
			PERROR("Invalid bit number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_TYPE_DIO_BYTE:
		if (channel == 0) {
			outb(value, instance->port_0_reg);
		} else if (channel == 1) {
			outb(value, instance->port_1_reg);
		} else {
			PERROR("Invalid byte number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_NO_FLAGS:
	case ME_IO_SINGLE_TYPE_DIO_WORD:
		if (channel == 0) {
			outb(value, instance->port_0_reg);
			outb(value >> 8, instance->port_1_reg);
		} else {
			PERROR("Invalid word number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	default:
		PERROR("Invalid flags specified.\n");
		err = ME_ERRNO_INVALID_FLAGS;
		break;
	}

	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me0600_relay_query_number_channels(me_subdevice_t *subdevice,
					      int *number)
{
	PDEBUG("executed.\n");
	*number = 16;
	return ME_ERRNO_SUCCESS;
}

static int me0600_relay_query_subdevice_type(me_subdevice_t *subdevice,
					     int *type, int *subtype)
{
	PDEBUG("executed.\n");
	*type = ME_TYPE_DO;
	*subtype = ME_SUBTYPE_SINGLE;
	return ME_ERRNO_SUCCESS;
}

static int me0600_relay_query_subdevice_caps(me_subdevice_t *subdevice,
					     int *caps)
{
	PDEBUG("executed.\n");
	*caps = 0;
	return ME_ERRNO_SUCCESS;
}

me0600_relay_subdevice_t *me0600_relay_constructor(uint32_t reg_base)
{
	me0600_relay_subdevice_t *subdevice;
	int err;

	PDEBUG("executed.\n");

	/* Allocate memory for subdevice instance */
	subdevice = kmalloc(sizeof(me0600_relay_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for subdevice instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me0600_relay_subdevice_t));

	/* Initialize subdevice base class */
	err = me_subdevice_init(&subdevice->base);

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}
	// Initialize spin locks.
	spin_lock_init(&subdevice->subdevice_lock);

	/* Save the subdevice index */
	subdevice->port_0_reg = reg_base + ME0600_RELAIS_0_REG;
	subdevice->port_1_reg = reg_base + ME0600_RELAIS_1_REG;
#ifdef MEDEBUG_DEBUG_REG
	subdevice->reg_base = reg_base;
#endif

	/* Overload base class methods. */
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me0600_relay_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config =
	    me0600_relay_io_single_config;
	subdevice->base.me_subdevice_io_single_read =
	    me0600_relay_io_single_read;
	subdevice->base.me_subdevice_io_single_write =
	    me0600_relay_io_single_write;
	subdevice->base.me_subdevice_query_number_channels =
	    me0600_relay_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me0600_relay_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me0600_relay_query_subdevice_caps;

	return subdevice;
}
