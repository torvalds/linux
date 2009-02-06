/**
 * @file me8255.c
 *
 * @brief 8255 subdevice instance.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
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

#include "me8255_reg.h"
#include "me8255.h"

/*
 * Defines
 */

/*
 * Functions
 */

static uint8_t get_mode_from_mirror(uint32_t mirror)
{
	PDEBUG("executed.\n");

	if (mirror & ME8255_PORT_0_OUTPUT) {
		if (mirror & ME8255_PORT_1_OUTPUT) {
			if (mirror & ME8255_PORT_2_OUTPUT) {
				return ME8255_MODE_OOO;
			} else {
				return ME8255_MODE_IOO;
			}
		} else {
			if (mirror & ME8255_PORT_2_OUTPUT) {
				return ME8255_MODE_OIO;
			} else {
				return ME8255_MODE_IIO;
			}
		}
	} else {
		if (mirror & ME8255_PORT_1_OUTPUT) {
			if (mirror & ME8255_PORT_2_OUTPUT) {
				return ME8255_MODE_OOI;
			} else {
				return ME8255_MODE_IOI;
			}
		} else {
			if (mirror & ME8255_PORT_2_OUTPUT) {
				return ME8255_MODE_OII;
			} else {
				return ME8255_MODE_III;
			}
		}
	}
}

static int me8255_io_reset_subdevice(struct me_subdevice *subdevice,
				     struct file *filep, int flags)
{
	me8255_subdevice_t *instance;

	PDEBUG("executed.\n");

	instance = (me8255_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ctrl_reg_lock);
	*instance->ctrl_reg_mirror &=
	    ~(ME8255_PORT_0_OUTPUT << instance->dio_idx);
	outb(get_mode_from_mirror(*instance->ctrl_reg_mirror),
	     instance->ctrl_reg);
	spin_unlock(instance->ctrl_reg_lock);

	outb(0, instance->port_reg);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me8255_io_single_config(struct me_subdevice *subdevice,
				   struct file *filep,
				   int channel,
				   int single_config,
				   int ref,
				   int trig_chan,
				   int trig_type, int trig_edge, int flags)
{
	me8255_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed.\n");

	instance = (me8255_subdevice_t *) subdevice;

	if (flags & ~ME_IO_SINGLE_CONFIG_DIO_BYTE) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (channel) {
		PERROR("Invalid channel.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	if (single_config == ME_SINGLE_CONFIG_DIO_INPUT) {
		spin_lock(instance->ctrl_reg_lock);
		*instance->ctrl_reg_mirror &=
		    ~(ME8255_PORT_0_OUTPUT << instance->dio_idx);
		outb(get_mode_from_mirror(*instance->ctrl_reg_mirror),
		     instance->ctrl_reg);
		spin_unlock(instance->ctrl_reg_lock);
	} else if (single_config == ME_SINGLE_CONFIG_DIO_OUTPUT) {
		spin_lock(instance->ctrl_reg_lock);
		*instance->ctrl_reg_mirror |=
		    (ME8255_PORT_0_OUTPUT << instance->dio_idx);
		outb(get_mode_from_mirror(*instance->ctrl_reg_mirror),
		     instance->ctrl_reg);
		spin_unlock(instance->ctrl_reg_lock);
	} else {
		PERROR("Invalid port direction.\n");
		err = ME_ERRNO_INVALID_SINGLE_CONFIG;
	}
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me8255_io_single_read(struct me_subdevice *subdevice,
				 struct file *filep,
				 int channel,
				 int *value, int time_out, int flags)
{
	me8255_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed.\n");

	instance = (me8255_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	switch (flags) {
	case ME_IO_SINGLE_TYPE_DIO_BIT:
		if ((channel >= 0) && (channel < 8)) {
			*value = inb(instance->port_reg) & (0x1 << channel);
		} else {
			PERROR("Invalid bit number.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_NO_FLAGS:
	case ME_IO_SINGLE_TYPE_DIO_BYTE:
		if (channel == 0) {
			*value = inb(instance->port_reg);
		} else {
			PERROR("Invalid byte number.\n");
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

static int me8255_io_single_write(struct me_subdevice *subdevice,
				  struct file *filep,
				  int channel,
				  int value, int time_out, int flags)
{
	me8255_subdevice_t *instance;
	uint8_t byte;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed.\n");

	instance = (me8255_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	switch (flags) {
	case ME_IO_SINGLE_TYPE_DIO_BIT:
		if ((channel >= 0) && (channel < 8)) {
			if (*instance->
			    ctrl_reg_mirror & (ME8255_PORT_0_OUTPUT <<
					       instance->dio_idx)) {
				byte = inb(instance->port_reg);

				if (value)
					byte |= 0x1 << channel;
				else
					byte &= ~(0x1 << channel);

				outb(byte, instance->port_reg);
			} else {
				PERROR("Port not in output mode.\n");
				err = ME_ERRNO_PREVIOUS_CONFIG;
			}
		} else {
			PERROR("Invalid bit number.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
		}
		break;

	case ME_IO_SINGLE_NO_FLAGS:
	case ME_IO_SINGLE_TYPE_DIO_BYTE:
		if (channel == 0) {
			if (*instance->
			    ctrl_reg_mirror & (ME8255_PORT_0_OUTPUT <<
					       instance->dio_idx)) {
				outb(value, instance->port_reg);
			} else {
				PERROR("Port not in output mode.\n");
				err = ME_ERRNO_PREVIOUS_CONFIG;
			}
		} else {
			PERROR("Invalid byte number.\n");
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

static int me8255_query_number_channels(struct me_subdevice *subdevice,
					int *number)
{
	PDEBUG("executed.\n");
	*number = ME8255_NUMBER_CHANNELS;
	return ME_ERRNO_SUCCESS;
}

static int me8255_query_subdevice_type(struct me_subdevice *subdevice,
				       int *type, int *subtype)
{
	PDEBUG("executed.\n");
	*type = ME_TYPE_DIO;
	*subtype = ME_SUBTYPE_SINGLE;
	return ME_ERRNO_SUCCESS;
}

static int me8255_query_subdevice_caps(struct me_subdevice *subdevice,
				       int *caps)
{
	PDEBUG("executed.\n");
	*caps = ME_CAPS_DIO_DIR_BYTE;
	return ME_ERRNO_SUCCESS;
}

me8255_subdevice_t *me8255_constructor(uint32_t device_id,
				       uint32_t reg_base,
				       unsigned int me8255_idx,
				       unsigned int dio_idx,
				       int *ctrl_reg_mirror,
				       spinlock_t * ctrl_reg_lock)
{
	me8255_subdevice_t *subdevice;
	int err;

	PDEBUG("executed.\n");

	/* Allocate memory for subdevice instance */
	subdevice = kmalloc(sizeof(me8255_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for 8255 instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me8255_subdevice_t));

	/* Check if counter index is out of range */

	if (dio_idx > 2) {
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

	/* Save the pointer to global port settings */
	subdevice->ctrl_reg_mirror = ctrl_reg_mirror;

	/* Save type of Meilhaus device */
	subdevice->device_id = device_id;

	/* Save the indices */
	subdevice->me8255_idx = me8255_idx;
	subdevice->dio_idx = dio_idx;

	/* Do device specific initialization */
	switch (device_id) {
	case PCI_DEVICE_ID_MEILHAUS_ME1400:
	case PCI_DEVICE_ID_MEILHAUS_ME14E0:

	case PCI_DEVICE_ID_MEILHAUS_ME140A:
	case PCI_DEVICE_ID_MEILHAUS_ME14EA:
		/* Check if 8255 index is out of range */
		if (me8255_idx > 0) {
			PERROR("8255 index is out of range.\n");
			me_subdevice_deinit(&subdevice->base);
			kfree(subdevice);
			return NULL;
		}

	case PCI_DEVICE_ID_MEILHAUS_ME140B:	/* Fall through */
	case PCI_DEVICE_ID_MEILHAUS_ME14EB:
		/* Check if 8255 index is out of range */
		if (me8255_idx > 1) {
			PERROR("8255 index is out of range.\n");
			me_subdevice_deinit(&subdevice->base);
			kfree(subdevice);
			return NULL;
		}

		/* Get the registers */
		if (me8255_idx == 0) {
			subdevice->ctrl_reg = reg_base + ME1400AB_PORT_A_CTRL;
			subdevice->port_reg =
			    reg_base + ME1400AB_PORT_A_0 + dio_idx;
		} else if (me8255_idx == 1) {
			subdevice->ctrl_reg = reg_base + ME1400AB_PORT_B_CTRL;
			subdevice->port_reg =
			    reg_base + ME1400AB_PORT_B_0 + dio_idx;
		}

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140C:
		/* Check if 8255 index is out of range */
		if (me8255_idx > 0) {
			PERROR("8255 index is out of range.\n");
			me_subdevice_deinit(&subdevice->base);
			kfree(subdevice);
			return NULL;
		}

	case PCI_DEVICE_ID_MEILHAUS_ME140D:	/* Fall through */
		/* Check if 8255 index is out of range */
		if (me8255_idx > 1) {
			PERROR("8255 index is out of range.\n");
			me_subdevice_deinit(&subdevice->base);
			kfree(subdevice);
			return NULL;
		}

		/* Get the registers */
		if (me8255_idx == 0) {
			subdevice->ctrl_reg = reg_base + ME1400CD_PORT_A_CTRL;
			subdevice->port_reg =
			    reg_base + ME1400CD_PORT_A_0 + dio_idx;
		} else if (me8255_idx == 1) {
			subdevice->ctrl_reg = reg_base + ME1400CD_PORT_B_CTRL;
			subdevice->port_reg =
			    reg_base + ME1400CD_PORT_B_0 + dio_idx;
		}

		break;

	default:
		PERROR("Unknown device type. dev ID: 0x%04x\n", device_id);

		me_subdevice_deinit(&subdevice->base);

		kfree(subdevice);

		return NULL;
	}

	/* Overload subdevice base class methods. */
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me8255_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config = me8255_io_single_config;
	subdevice->base.me_subdevice_io_single_read = me8255_io_single_read;
	subdevice->base.me_subdevice_io_single_write = me8255_io_single_write;
	subdevice->base.me_subdevice_query_number_channels =
	    me8255_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me8255_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me8255_query_subdevice_caps;

	return subdevice;
}
