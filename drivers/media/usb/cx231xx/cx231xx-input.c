/*
 *   cx231xx IR glue driver
 *
 *   Copyright (C) 2010 Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 *   Polaris (cx231xx) has its support for IR's with a design close to MCE.
 *   however, a few designs are using an external I2C chip for IR, instead
 *   of using the one provided by the chip.
 *   This driver provides support for those extra devices
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 */

#include "cx231xx.h"
#include <linux/usb.h>
#include <linux/slab.h>

#define MODULE_NAME "cx231xx-input"

static int get_key_isdbt(struct IR_i2c *ir, u32 *ir_key,
			 u32 *ir_raw)
{
	int	rc;
	u8	cmd, scancode;

	dev_dbg(&ir->rc->input_dev->dev, "%s\n", __func__);

		/* poll IR chip */
	rc = i2c_master_recv(ir->c, &cmd, 1);
	if (rc < 0)
		return rc;
	if (rc != 1)
		return -EIO;

	/* it seems that 0xFE indicates that a button is still hold
	   down, while 0xff indicates that no button is hold
	   down. 0xfe sequences are sometimes interrupted by 0xFF */

	if (cmd == 0xff)
		return 0;

	scancode =
		 ((cmd & 0x01) ? 0x80 : 0) |
		 ((cmd & 0x02) ? 0x40 : 0) |
		 ((cmd & 0x04) ? 0x20 : 0) |
		 ((cmd & 0x08) ? 0x10 : 0) |
		 ((cmd & 0x10) ? 0x08 : 0) |
		 ((cmd & 0x20) ? 0x04 : 0) |
		 ((cmd & 0x40) ? 0x02 : 0) |
		 ((cmd & 0x80) ? 0x01 : 0);

	dev_dbg(&ir->rc->input_dev->dev, "cmd %02x, scan = %02x\n",
		cmd, scancode);

	*ir_key = scancode;
	*ir_raw = scancode;
	return 1;
}

int cx231xx_ir_init(struct cx231xx *dev)
{
	struct i2c_board_info info;
	u8 ir_i2c_bus;

	dev_dbg(&dev->udev->dev, "%s\n", __func__);

	/* Only initialize if a rc keycode map is defined */
	if (!cx231xx_boards[dev->model].rc_map_name)
		return -ENODEV;

	request_module("ir-kbd-i2c");

	memset(&info, 0, sizeof(struct i2c_board_info));
	memset(&dev->init_data, 0, sizeof(dev->init_data));
	dev->init_data.rc_dev = rc_allocate_device();
	if (!dev->init_data.rc_dev)
		return -ENOMEM;

	dev->init_data.name = cx231xx_boards[dev->model].name;

	strlcpy(info.type, "ir_video", I2C_NAME_SIZE);
	info.platform_data = &dev->init_data;

	/*
	 * Board-dependent values
	 *
	 * For now, there's just one type of hardware design using
	 * an i2c device.
	 */
	dev->init_data.get_key = get_key_isdbt;
	dev->init_data.ir_codes = cx231xx_boards[dev->model].rc_map_name;
	/* The i2c micro-controller only outputs the cmd part of NEC protocol */
	dev->init_data.rc_dev->scanmask = 0xff;
	dev->init_data.rc_dev->driver_name = "cx231xx";
	dev->init_data.type = RC_BIT_NEC;
	info.addr = 0x30;

	/* Load and bind ir-kbd-i2c */
	ir_i2c_bus = cx231xx_boards[dev->model].ir_i2c_master;
	dev_dbg(&dev->udev->dev, "Trying to bind ir at bus %d, addr 0x%02x\n",
		ir_i2c_bus, info.addr);
	dev->ir_i2c_client = i2c_new_device(&dev->i2c_bus[ir_i2c_bus].i2c_adap, &info);

	return 0;
}

void cx231xx_ir_exit(struct cx231xx *dev)
{
	if (dev->ir_i2c_client)
		i2c_unregister_device(dev->ir_i2c_client);
	dev->ir_i2c_client = NULL;
}
