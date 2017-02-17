/*
 * Copyright (c) 2012 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <media/v4l2-device.h>
#include <linux/atomisp_platform.h>

int vcm_power_up(struct v4l2_subdev *sd)
{
	const struct camera_af_platform_data *vcm_platform_data;

	vcm_platform_data = camera_get_af_platform_data();
	if (NULL == vcm_platform_data)
		return -ENODEV;
	/* Enable power */
	return vcm_platform_data->power_ctrl(sd, 1);
}

int vcm_power_down(struct v4l2_subdev *sd)
{
	const struct camera_af_platform_data *vcm_platform_data;

	vcm_platform_data = camera_get_af_platform_data();
	if (NULL == vcm_platform_data)
		return -ENODEV;
	return vcm_platform_data->power_ctrl(sd, 0);
}

