/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contributors:
 *      Manjunath Hadli <manjunath.hadli@ti.com>
 *      Prabhakar Lad <prabhakar.lad@ti.com>
 */

#ifndef _VPFE_H
#define _VPFE_H

#ifdef __KERNEL__
#include <linux/v4l2-subdev.h>
#include <linux/clk.h>
#include <linux/i2c.h>

#include <media/davinci/vpfe_types.h>

#define CAPTURE_DRV_NAME	"vpfe-capture"

struct vpfe_route {
	__u32 input;
	__u32 output;
};

enum vpfe_subdev_id {
	VPFE_SUBDEV_TVP5146 = 1,
	VPFE_SUBDEV_MT9T031 = 2,
	VPFE_SUBDEV_TVP7002 = 3,
	VPFE_SUBDEV_MT9P031 = 4,
};

struct vpfe_ext_subdev_info {
	/* v4l2 subdev */
	struct v4l2_subdev *subdev;
	/* Sub device module name */
	char module_name[32];
	/* Sub device group id */
	int grp_id;
	/* Number of inputs supported */
	int num_inputs;
	/* inputs available at the sub device */
	struct v4l2_input *inputs;
	/* Sub dev routing information for each input */
	struct vpfe_route *routes;
	/* ccdc bus/interface configuration */
	struct vpfe_hw_if_param ccdc_if_params;
	/* i2c subdevice board info */
	struct i2c_board_info board_info;
	/* Is this a camera sub device ? */
	unsigned is_camera:1;
	/* check if sub dev supports routing */
	unsigned can_route:1;
	/* registered ? */
	unsigned registered:1;
};

struct vpfe_config {
	/* Number of sub devices connected to vpfe */
	int num_subdevs;
	/* information about each subdev */
	struct vpfe_ext_subdev_info *sub_devs;
	/* evm card info */
	char *card_name;
	/* setup function for the input path */
	int (*setup_input)(enum vpfe_subdev_id id);
	/* number of clocks */
	int num_clocks;
	/* clocks used for vpfe capture */
	char *clocks[];
};
#endif
#endif
