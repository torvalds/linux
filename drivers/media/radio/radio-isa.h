/*
 * Framework for ISA radio drivers.
 * This takes care of all the V4L2 scaffolding, allowing the ISA drivers
 * to concentrate on the actual hardware operation.
 *
 * Copyright (C) 2012 Hans Verkuil <hans.verkuil@cisco.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef _RADIO_ISA_H_
#define _RADIO_ISA_H_

#include <linux/isa.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

struct radio_isa_driver;
struct radio_isa_ops;

/* Core structure for radio ISA cards */
struct radio_isa_card {
	const struct radio_isa_driver *drv;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler hdl;
	struct video_device vdev;
	struct mutex lock;
	const struct radio_isa_ops *ops;
	struct {	/* mute/volume cluster */
		struct v4l2_ctrl *mute;
		struct v4l2_ctrl *volume;
	};
	/* I/O port */
	int io;

	/* Card is in stereo audio mode */
	bool stereo;
	/* Current frequency */
	u32 freq;
};

struct radio_isa_ops {
	/* Allocate and initialize a radio_isa_card struct */
	struct radio_isa_card *(*alloc)(void);
	/* Probe whether a card is present at the given port */
	bool (*probe)(struct radio_isa_card *isa, int io);
	/* Special card initialization can be done here, this is called after
	 * the standard controls are registered, but before they are setup,
	 * thus allowing drivers to add their own controls here. */
	int (*init)(struct radio_isa_card *isa);
	/* Set mute and volume. */
	int (*s_mute_volume)(struct radio_isa_card *isa, bool mute, int volume);
	/* Set frequency */
	int (*s_frequency)(struct radio_isa_card *isa, u32 freq);
	/* Set stereo/mono audio mode */
	int (*s_stereo)(struct radio_isa_card *isa, bool stereo);
	/* Get rxsubchans value for VIDIOC_G_TUNER */
	u32 (*g_rxsubchans)(struct radio_isa_card *isa);
	/* Get the signal strength for VIDIOC_G_TUNER */
	u32 (*g_signal)(struct radio_isa_card *isa);
};

/* Top level structure needed to instantiate the cards */
struct radio_isa_driver {
	struct isa_driver driver;
	const struct radio_isa_ops *ops;
	/* The module_param_array with the specified I/O ports */
	int *io_params;
	/* The module_param_array with the radio_nr values */
	int *radio_nr_params;
	/* Whether we should probe for possible cards */
	bool probe;
	/* The list of possible I/O ports */
	const int *io_ports;
	/* The size of that list */
	int num_of_io_ports;
	/* The region size to request */
	unsigned region_size;
	/* The name of the card */
	const char *card;
	/* Card can capture stereo audio */
	bool has_stereo;
	/* The maximum volume for the volume control. If 0, then there
	   is no volume control possible. */
	int max_volume;
};

int radio_isa_match(struct device *pdev, unsigned int dev);
int radio_isa_probe(struct device *pdev, unsigned int dev);
int radio_isa_remove(struct device *pdev, unsigned int dev);

#endif
