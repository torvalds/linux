/*
 *
 *  $Id$
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include "pvrusb2-i2c-core.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-debug.h"
#include "pvrusb2-i2c-cmd-v4l2.h"
#include "pvrusb2-audio.h"
#include "pvrusb2-tuner.h"
#include "pvrusb2-video-v4l.h"
#include "pvrusb2-cx2584x-v4l.h"
#include "pvrusb2-wm8775.h"

#define trace_i2c(...) pvr2_trace(PVR2_TRACE_I2C,__VA_ARGS__)

#define OP_STANDARD 0
#define OP_BCSH 1
#define OP_VOLUME 2
#define OP_FREQ 3
#define OP_AUDIORATE 4
#define OP_SIZE 5
#define OP_LOG 6

static const struct pvr2_i2c_op * const ops[] = {
	[OP_STANDARD] = &pvr2_i2c_op_v4l2_standard,
	[OP_BCSH] = &pvr2_i2c_op_v4l2_bcsh,
	[OP_VOLUME] = &pvr2_i2c_op_v4l2_volume,
	[OP_FREQ] = &pvr2_i2c_op_v4l2_frequency,
	[OP_SIZE] = &pvr2_i2c_op_v4l2_size,
	[OP_LOG] = &pvr2_i2c_op_v4l2_log,
};

void pvr2_i2c_probe(struct pvr2_hdw *hdw,struct pvr2_i2c_client *cp)
{
	int id;
	id = cp->client->driver->id;
	cp->ctl_mask = ((1 << OP_STANDARD) |
			(1 << OP_BCSH) |
			(1 << OP_VOLUME) |
			(1 << OP_FREQ) |
			(1 << OP_SIZE) |
			(1 << OP_LOG));

	if (id == I2C_DRIVERID_MSP3400) {
		if (pvr2_i2c_msp3400_setup(hdw,cp)) {
			return;
		}
	}
	if (id == I2C_DRIVERID_TUNER) {
		if (pvr2_i2c_tuner_setup(hdw,cp)) {
			return;
		}
	}
	if (id == I2C_DRIVERID_CX25840) {
		if (pvr2_i2c_cx2584x_v4l_setup(hdw,cp)) {
			return;
		}
	}
	if (id == I2C_DRIVERID_WM8775) {
		if (pvr2_i2c_wm8775_setup(hdw,cp)) {
			return;
		}
	}
	if (id == I2C_DRIVERID_SAA711X) {
		if (pvr2_i2c_decoder_v4l_setup(hdw,cp)) {
			return;
		}
	}
}


const struct pvr2_i2c_op *pvr2_i2c_get_op(unsigned int idx)
{
	if (idx >= ARRAY_SIZE(ops))
		return NULL;
	return ops[idx];
}


/*
  Stuff for Emacs to see, in order to encourage consistent editing style:
  *** Local Variables: ***
  *** mode: c ***
  *** fill-column: 75 ***
  *** tab-width: 8 ***
  *** c-basic-offset: 8 ***
  *** End: ***
  */
