/*
    comedi_fc.h

    This is a place for code driver writers wish to share between
    two or more drivers. These functions are meant to be used only
    by drivers, they are NOT part of the kcomedilib API!

    Author:  Frank Mori Hess <fmhess@users.sourceforge.net>
    Copyright (C) 2002 Frank Mori Hess

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

************************************************************************/

#ifndef _COMEDI_FC_H
#define _COMEDI_FC_H

#include "../comedidev.h"

/* Writes an array of data points to comedi's buffer */
extern unsigned int cfc_write_array_to_buffer(struct comedi_subdevice *subd,
					      void *data,
					      unsigned int num_bytes);

static inline unsigned int cfc_write_to_buffer(struct comedi_subdevice *subd,
					       short data)
{
	return cfc_write_array_to_buffer(subd, &data, sizeof(data));
};

static inline unsigned int cfc_write_long_to_buffer(struct comedi_subdevice
						    *subd, unsigned int data)
{
	return cfc_write_array_to_buffer(subd, &data, sizeof(data));
};

extern unsigned int cfc_read_array_from_buffer(struct comedi_subdevice *subd,
					       void *data,
					       unsigned int num_bytes);

extern unsigned int cfc_handle_events(struct comedi_device *dev,
				      struct comedi_subdevice *subd);

static inline unsigned int cfc_bytes_per_scan(struct comedi_subdevice *subd)
{
	int num_samples;
	int bits_per_sample;

	switch (subd->type) {
	case COMEDI_SUBD_DI:
	case COMEDI_SUBD_DO:
	case COMEDI_SUBD_DIO:
		bits_per_sample = 8 * bytes_per_sample(subd);
		num_samples = (subd->async->cmd.chanlist_len +
			       bits_per_sample - 1) / bits_per_sample;
		break;
	default:
		num_samples = subd->async->cmd.chanlist_len;
		break;
	}
	return num_samples * bytes_per_sample(subd);
}

/**
 * cfc_check_trigger_src() - trivially validate a comedi_cmd trigger source
 * @src: pointer to the trigger source to validate
 * @flags: bitmask of valid TRIG_* for the trigger
 *
 * This is used in "step 1" of the do_cmdtest functions of comedi drivers
 * to vaildate the comedi_cmd triggers. The mask of the @src against the
 * @flags allows the userspace comedilib to pass all the comedi_cmd
 * triggers as TRIG_ANY and get back a bitmask of the valid trigger sources.
 */
static inline int cfc_check_trigger_src(unsigned int *src, unsigned int flags)
{
	unsigned int orig_src = *src;

	*src = orig_src & flags;
	if (*src == TRIG_INVALID || *src != orig_src)
		return -EINVAL;
	return 0;
}

/**
 * cfc_check_trigger_is_unique() - make sure a trigger source is unique
 * @src: the trigger source to check
 */
static inline int cfc_check_trigger_is_unique(unsigned int src)
{
	/* this test is true if more than one _src bit is set */
	if ((src & (src - 1)) != 0)
		return -EINVAL;
	return 0;
}

/**
 * cfc_check_trigger_arg_is() - trivially validate a trigger argument
 * @arg: pointer to the trigger arg to validate
 * @val: the value the argument should be
 */
static inline int cfc_check_trigger_arg_is(unsigned int *arg, unsigned int val)
{
	if (*arg != val) {
		*arg = val;
		return -EINVAL;
	}
	return 0;
}

/**
 * cfc_check_trigger_arg_min() - trivially validate a trigger argument
 * @arg: pointer to the trigger arg to validate
 * @val: the minimum value the argument should be
 */
static inline int cfc_check_trigger_arg_min(unsigned int *arg,
					    unsigned int val)
{
	if (*arg < val) {
		*arg = val;
		return -EINVAL;
	}
	return 0;
}

/**
 * cfc_check_trigger_arg_max() - trivially validate a trigger argument
 * @arg: pointer to the trigger arg to validate
 * @val: the maximum value the argument should be
 */
static inline int cfc_check_trigger_arg_max(unsigned int *arg,
					    unsigned int val)
{
	if (*arg > val) {
		*arg = val;
		return -EINVAL;
	}
	return 0;
}

#endif /* _COMEDI_FC_H */
