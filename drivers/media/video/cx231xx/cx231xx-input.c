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
	u8	cmd;

		/* poll IR chip */
	if (1 != i2c_master_recv(ir->c, &cmd, 1))
		return -EIO;

	/* it seems that 0xFE indicates that a button is still hold
	   down, while 0xff indicates that no button is hold
	   down. 0xfe sequences are sometimes interrupted by 0xFF */

	if (cmd == 0xff)
		return 0;

	dev_dbg(&ir->input->dev, "scancode = %02x\n", cmd);

	*ir_key = cmd;
	*ir_raw = cmd;
	return 1;
}

int cx231xx_ir_init(struct cx231xx *dev)
{
	struct input_dev *input_dev;
	struct i2c_board_info info;
	int rc;
	u8 ir_i2c_bus;

	dev_dbg(&dev->udev->dev, "%s\n", __func__);

	/* Only initialize if a rc keycode map is defined */
	if (!cx231xx_boards[dev->model].rc_map)
		return -ENODEV;

	input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	request_module("ir-kbd-i2c");

	memset(&info, 0, sizeof(struct i2c_board_info));
	memset(&dev->ir.init_data, 0, sizeof(dev->ir.init_data));

	dev->ir.input_dev = input_dev;
	dev->ir.init_data.name = cx231xx_boards[dev->model].name;
	dev->ir.props.priv = dev;
	dev->ir.props.allowed_protos = IR_TYPE_NEC;
	snprintf(dev->ir.name, sizeof(dev->ir.name),
		 "cx231xx IR (%s)", cx231xx_boards[dev->model].name);
	usb_make_path(dev->udev, dev->ir.phys, sizeof(dev->ir.phys));
	strlcat(dev->ir.phys, "/input0", sizeof(dev->ir.phys));

	strlcpy(info.type, "ir_video", I2C_NAME_SIZE);
	info.platform_data = &dev->ir.init_data;

	input_dev->name = dev->ir.name;
	input_dev->phys = dev->ir.phys;
	input_dev->dev.parent = &dev->udev->dev;
	input_dev->id.bustype = BUS_USB;
	input_dev->id.version = 1;
	input_dev->id.vendor = le16_to_cpu(dev->udev->descriptor.idVendor);
	input_dev->id.product = le16_to_cpu(dev->udev->descriptor.idProduct);

	/*
	 * Board-dependent values
	 *
	 * For now, there's just one type of hardware design using
	 * an i2c device.
	 */
	dev->ir.init_data.get_key = get_key_isdbt;
	dev->ir.init_data.ir_codes = cx231xx_boards[dev->model].rc_map;
	/* The i2c micro-controller only outputs the cmd part of NEC protocol */
	dev->ir.props.scanmask = 0xff;
	info.addr = 0x30;

	rc = ir_input_register(input_dev, cx231xx_boards[dev->model].rc_map,
			       &dev->ir.props, MODULE_NAME);
	if (rc < 0)
		return rc;

	/* Load and bind ir-kbd-i2c */
	ir_i2c_bus = cx231xx_boards[dev->model].ir_i2c_master;
	i2c_new_device(&dev->i2c_bus[ir_i2c_bus].i2c_adap, &info);

	return rc;
}

void cx231xx_ir_exit(struct cx231xx *dev)
{
	if (dev->ir.input_dev) {
		ir_input_unregister(dev->ir.input_dev);
		dev->ir.input_dev = NULL;
	}
}
