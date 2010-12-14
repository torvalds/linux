/*
 *  tm6000-input.c - driver for TM5600/TM6000/TM6010 USB video capture devices
 *
 *  Copyright (C) 2010 Stefan Ringel <stefan.ringel@arcor.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <linux/input.h>
#include <linux/usb.h>

#include <media/ir-core.h>
#include <media/ir-common.h>

#include "tm6000.h"
#include "tm6000-regs.h"

static unsigned int ir_debug;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug, "enable debug message [IR]");

static unsigned int enable_ir = 1;
module_param(enable_ir, int, 0644);
MODULE_PARM_DESC(enable_ir, "enable ir (default is enable)");

#undef dprintk

#define dprintk(fmt, arg...) \
	if (ir_debug) { \
		printk(KERN_DEBUG "%s/ir: " fmt, ir->name , ## arg); \
	}

struct tm6000_ir_poll_result {
	u16 rc_data;
};

struct tm6000_IR {
	struct tm6000_core	*dev;
	struct ir_input_dev	*input;
	struct ir_input_state	ir;
	char			name[32];
	char			phys[32];

	/* poll expernal decoder */
	int			polling;
	struct delayed_work	work;
	u8			wait:1;
	u8			key:1;
	struct urb		*int_urb;
	u8			*urb_data;

	int (*get_key) (struct tm6000_IR *, struct tm6000_ir_poll_result *);

	/* IR device properties */
	struct ir_dev_props	props;
};


void tm6000_ir_wait(struct tm6000_core *dev, u8 state)
{
	struct tm6000_IR *ir = dev->ir;

	if (!dev->ir)
		return;

	if (state)
		ir->wait = 1;
	else
		ir->wait = 0;
}


static int tm6000_ir_config(struct tm6000_IR *ir)
{
	struct tm6000_core *dev = ir->dev;
	u8 buf[10];
	int rc;

	/* hack */
	buf[0] = 0xff;
	buf[1] = 0xff;
	buf[2] = 0xf2;
	buf[3] = 0x2b;
	buf[4] = 0x20;
	buf[5] = 0x35;
	buf[6] = 0x60;
	buf[7] = 0x04;
	buf[8] = 0xc0;
	buf[9] = 0x08;

	rc = tm6000_read_write_usb(dev, USB_DIR_OUT | USB_TYPE_VENDOR |
		USB_RECIP_DEVICE, REQ_00_SET_IR_VALUE, 0, 0, buf, 0x0a);
	msleep(100);

	if (rc < 0) {
		printk(KERN_INFO "IR configuration failed");
		return rc;
	}
	return 0;
}

static void tm6000_ir_urb_received(struct urb *urb)
{
	struct tm6000_core *dev = urb->context;
	struct tm6000_IR *ir = dev->ir;
	int rc;

	if (urb->status != 0)
		printk(KERN_INFO "not ready\n");
	else if (urb->actual_length > 0) {
		memcpy(ir->urb_data, urb->transfer_buffer, urb->actual_length);

		dprintk("data %02x %02x %02x %02x\n", ir->urb_data[0],
			ir->urb_data[1], ir->urb_data[2], ir->urb_data[3]);

		ir->key = 1;
	}

	rc = usb_submit_urb(urb, GFP_ATOMIC);
}

static int default_polling_getkey(struct tm6000_IR *ir,
				struct tm6000_ir_poll_result *poll_result)
{
	struct tm6000_core *dev = ir->dev;
	int rc;
	u8 buf[2];

	if (ir->wait && !&dev->int_in)
		return 0;

	if (&dev->int_in) {
		if (ir->ir.ir_type == IR_TYPE_RC5)
			poll_result->rc_data = ir->urb_data[0];
		else
			poll_result->rc_data = ir->urb_data[0] | ir->urb_data[1] << 8;
	} else {
		tm6000_set_reg(dev, REQ_04_EN_DISABLE_MCU_INT, 2, 0);
		msleep(10);
		tm6000_set_reg(dev, REQ_04_EN_DISABLE_MCU_INT, 2, 1);
		msleep(10);

		if (ir->ir.ir_type == IR_TYPE_RC5) {
			rc = tm6000_read_write_usb(dev, USB_DIR_IN |
				USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				REQ_02_GET_IR_CODE, 0, 0, buf, 1);

			msleep(10);

			dprintk("read data=%02x\n", buf[0]);
			if (rc < 0)
				return rc;

			poll_result->rc_data = buf[0];
		} else {
			rc = tm6000_read_write_usb(dev, USB_DIR_IN |
				USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				REQ_02_GET_IR_CODE, 0, 0, buf, 2);

			msleep(10);

			dprintk("read data=%04x\n", buf[0] | buf[1] << 8);
			if (rc < 0)
				return rc;

			poll_result->rc_data = buf[0] | buf[1] << 8;
		}
		if ((poll_result->rc_data & 0x00ff) != 0xff)
			ir->key = 1;
	}
	return 0;
}

static void tm6000_ir_handle_key(struct tm6000_IR *ir)
{
	int result;
	struct tm6000_ir_poll_result poll_result;

	/* read the registers containing the IR status */
	result = ir->get_key(ir, &poll_result);
	if (result < 0) {
		printk(KERN_INFO "ir->get_key() failed %d\n", result);
		return;
	}

	dprintk("ir->get_key result data=%04x\n", poll_result.rc_data);

	if (ir->key) {
		ir_input_keydown(ir->input->input_dev, &ir->ir,
				(u32)poll_result.rc_data);

		ir_input_nokey(ir->input->input_dev, &ir->ir);
		ir->key = 0;
	}
	return;
}

static void tm6000_ir_work(struct work_struct *work)
{
	struct tm6000_IR *ir = container_of(work, struct tm6000_IR, work.work);

	tm6000_ir_handle_key(ir);
	schedule_delayed_work(&ir->work, msecs_to_jiffies(ir->polling));
}

static int tm6000_ir_start(void *priv)
{
	struct tm6000_IR *ir = priv;

	INIT_DELAYED_WORK(&ir->work, tm6000_ir_work);
	schedule_delayed_work(&ir->work, 0);

	return 0;
}

static void tm6000_ir_stop(void *priv)
{
	struct tm6000_IR *ir = priv;

	cancel_delayed_work_sync(&ir->work);
}

int tm6000_ir_change_protocol(void *priv, u64 ir_type)
{
	struct tm6000_IR *ir = priv;

	ir->get_key = default_polling_getkey;

	tm6000_ir_config(ir);
	/* TODO */
	return 0;
}

int tm6000_ir_init(struct tm6000_core *dev)
{
	struct tm6000_IR *ir;
	struct ir_input_dev *ir_input_dev;
	int err = -ENOMEM;
	int pipe, size, rc;

	if (!enable_ir)
		return -ENODEV;

	if (!dev->caps.has_remote)
		return 0;

	if (!dev->ir_codes)
		return 0;

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	ir_input_dev = kzalloc(sizeof(*ir_input_dev), GFP_KERNEL);
	ir_input_dev->input_dev = input_allocate_device();
	if (!ir || !ir_input_dev || !ir_input_dev->input_dev)
		goto err_out_free;

	/* record handles to ourself */
	ir->dev = dev;
	dev->ir = ir;

	ir->input = ir_input_dev;

	/* input einrichten */
	ir->props.allowed_protos = IR_TYPE_RC5 | IR_TYPE_NEC;
	ir->props.priv = ir;
	ir->props.change_protocol = tm6000_ir_change_protocol;
	ir->props.open = tm6000_ir_start;
	ir->props.close = tm6000_ir_stop;
	ir->props.driver_type = RC_DRIVER_SCANCODE;

	ir->polling = 50;

	snprintf(ir->name, sizeof(ir->name), "tm5600/60x0 IR (%s)",
						dev->name);

	usb_make_path(dev->udev, ir->phys, sizeof(ir->phys));
	strlcat(ir->phys, "/input0", sizeof(ir->phys));

	tm6000_ir_change_protocol(ir, IR_TYPE_UNKNOWN);
	err = ir_input_init(ir_input_dev->input_dev, &ir->ir, IR_TYPE_OTHER);
	if (err < 0)
		goto err_out_free;

	ir_input_dev->input_dev->name = ir->name;
	ir_input_dev->input_dev->phys = ir->phys;
	ir_input_dev->input_dev->id.bustype = BUS_USB;
	ir_input_dev->input_dev->id.version = 1;
	ir_input_dev->input_dev->id.vendor = le16_to_cpu(dev->udev->descriptor.idVendor);
	ir_input_dev->input_dev->id.product = le16_to_cpu(dev->udev->descriptor.idProduct);

	ir_input_dev->input_dev->dev.parent = &dev->udev->dev;

	if (&dev->int_in) {
		dprintk("IR over int\n");

		ir->int_urb = usb_alloc_urb(0, GFP_KERNEL);

		pipe = usb_rcvintpipe(dev->udev,
			dev->int_in.endp->desc.bEndpointAddress
			& USB_ENDPOINT_NUMBER_MASK);

		size = usb_maxpacket(dev->udev, pipe, usb_pipeout(pipe));
		dprintk("IR max size: %d\n", size);

		ir->int_urb->transfer_buffer = kzalloc(size, GFP_KERNEL);
		if (ir->int_urb->transfer_buffer == NULL) {
			usb_free_urb(ir->int_urb);
			goto err_out_stop;
		}
		dprintk("int interval: %d\n", dev->int_in.endp->desc.bInterval);
		usb_fill_int_urb(ir->int_urb, dev->udev, pipe,
			ir->int_urb->transfer_buffer, size,
			tm6000_ir_urb_received, dev,
			dev->int_in.endp->desc.bInterval);
		rc = usb_submit_urb(ir->int_urb, GFP_KERNEL);
		if (rc) {
			kfree(ir->int_urb->transfer_buffer);
			usb_free_urb(ir->int_urb);
			err = rc;
			goto err_out_stop;
		}
		ir->urb_data = kzalloc(size, GFP_KERNEL);
	}

	/* ir register */
	err = ir_input_register(ir->input->input_dev, dev->ir_codes,
		&ir->props, "tm6000");
	if (err)
		goto err_out_stop;

	return 0;

err_out_stop:
	dev->ir = NULL;
err_out_free:
	kfree(ir_input_dev);
	kfree(ir);
	return err;
}

int tm6000_ir_fini(struct tm6000_core *dev)
{
	struct tm6000_IR *ir = dev->ir;

	/* skip detach on non attached board */

	if (!ir)
		return 0;

	ir_input_unregister(ir->input->input_dev);

	if (ir->int_urb) {
		usb_kill_urb(ir->int_urb);
		kfree(ir->int_urb->transfer_buffer);
		usb_free_urb(ir->int_urb);
		ir->int_urb = NULL;
		kfree(ir->urb_data);
		ir->urb_data = NULL;
	}

	kfree(ir->input);
	ir->input = NULL;
	kfree(ir);
	dev->ir = NULL;

	return 0;
}
