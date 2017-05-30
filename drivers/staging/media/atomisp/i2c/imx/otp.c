/*
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
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
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>
#include <media/v4l2-device.h>

void *dummy_otp_read(struct v4l2_subdev *sd, u8 dev_addr,
	u32 start_addr, u32 size)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 *buf;

	buf = devm_kzalloc(&client->dev, size, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	return buf;
}
