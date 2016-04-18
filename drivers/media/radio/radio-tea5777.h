#ifndef __RADIO_TEA5777_H
#define __RADIO_TEA5777_H

/*
 *   v4l2 driver for TEA5777 Philips AM/FM radio tuner chips
 *
 *	Copyright (c) 2012 Hans de Goede <hdegoede@redhat.com>
 *
 *   Based on the ALSA driver for TEA5757/5759 Philips AM/FM radio tuner chips:
 *
 *	Copyright (c) 2004 Jaroslav Kysela <perex@perex.cz>
 *	Copyright (c) 2012 Hans de Goede <hdegoede@redhat.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>

#define TEA575X_FMIF	10700
#define TEA575X_AMIF	  450

struct radio_tea5777;

struct radio_tea5777_ops {
	/*
	 * Write the 6 bytes large write register of the tea5777
	 *
	 * val represents the 6 write registers, with byte 1 from the
	 * datasheet being the most significant byte (so byte 5 of the u64),
	 * and byte 6 from the datasheet being the least significant byte.
	 *
	 * returns 0 on success.
	 */
	int (*write_reg)(struct radio_tea5777 *tea, u64 val);
	/*
	 * Read the 3 bytes large read register of the tea5777
	 *
	 * The read value gets returned in val, akin to write_reg, byte 1 from
	 * the datasheet is stored as the most significant byte (so byte 2 of
	 * the u32), and byte 3 from the datasheet gets stored as the least
	 * significant byte (iow byte 0 of the u32).
	 *
	 * returns 0 on success.
	 */
	int (*read_reg)(struct radio_tea5777 *tea, u32 *val);
};

struct radio_tea5777 {
	struct v4l2_device *v4l2_dev;
	struct v4l2_file_operations fops;
	struct video_device vd;		/* video device */
	bool has_am;			/* Device can tune to AM freqs */
	bool write_before_read;		/* must write before read quirk */
	bool needs_write;		/* for write before read quirk */
	u32 band;			/* current band */
	u32 freq;			/* current frequency */
	u32 audmode;			/* last set audmode */
	u32 seek_rangelow;		/* current hwseek limits */
	u32 seek_rangehigh;
	u32 read_reg;
	u64 write_reg;
	struct mutex mutex;
	const struct radio_tea5777_ops *ops;
	void *private_data;
	u8 card[32];
	u8 bus_info[32];
	struct v4l2_ctrl_handler ctrl_handler;
};

int radio_tea5777_init(struct radio_tea5777 *tea, struct module *owner);
void radio_tea5777_exit(struct radio_tea5777 *tea);
int radio_tea5777_set_freq(struct radio_tea5777 *tea);

#endif /* __RADIO_TEA5777_H */
