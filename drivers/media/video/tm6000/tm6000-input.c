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

#include <media/rc-core.h>

#include "tm6000.h"
#include "tm6000-regs.h"

static unsigned int ir_debug;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug, "enable debug message [IR]");

static unsigned int enable_ir = 1;
module_param(enable_ir, int, 0644);
MODULE_PARM_DESC(enable_ir, "enable ir (default is enable)");

/* number of 50ms for ON-OFF-ON power led */
/* show IR activity */
#define PWLED_OFF 2

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
	struct rc_dev		*rc;
	char			name[32];
	char			phys[32];

	/* poll expernal decoder */
	int			polling;
	struct delayed_work	work;
	u8			wait:1;
	u8			key:1;
	u8			pwled:1;
	u8			pwledcnt;
	u16			key_addr;
	struct urb		*int_urb;
	u8			*urb_data;

	int (*get_key) (struct tm6000_IR *, struct tm6000_ir_poll_result *);

	/* IR device properties */
	u64			rc_type;
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

	switch (ir->rc_type) {
	case RC_TYPE_NEC:
		/* Setup IR decoder for NEC standard 12MHz system clock */
		/* IR_LEADER_CNT = 0.9ms             */
		tm6000_set_reg(dev, TM6010_REQ07_RD8_IR_LEADER1, 0xaa);
		tm6000_set_reg(dev, TM6010_REQ07_RD8_IR_LEADER0, 0x30);
		/* IR_PULSE_CNT = 0.7ms              */
		tm6000_set_reg(dev, TM6010_REQ07_RD8_IR_PULSE_CNT1, 0x20);
		tm6000_set_reg(dev, TM6010_REQ07_RD8_IR_PULSE_CNT0, 0xd0);
		/* Remote WAKEUP = enable */
		tm6000_set_reg(dev, TM6010_REQ07_RE5_REMOTE_WAKEUP, 0xfe);
		/* IR_WKUP_SEL = Low byte in decoded IR data */
		tm6000_set_reg(dev, TM6010_REQ07_RD8_IR_WAKEUP_SEL, 0xff);
		/* IR_WKU_ADD code */
		tm6000_set_reg(dev, TM6010_REQ07_RD8_IR_WAKEUP_ADD, 0xff);
		tm6000_flash_led(dev, 0);
		msleep(100);
		tm6000_flash_led(dev, 1);
		break;
	default:
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
		break;
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
		switch (ir->rc_type) {
		case RC_TYPE_RC5:
			poll_result->rc_data = ir->urb_data[0];
			break;
		case RC_TYPE_NEC:
			if (ir->urb_data[1] == ((ir->key_addr >> 8) & 0xff)) {
				poll_result->rc_data = ir->urb_data[0]
							| ir->urb_data[1] << 8;
			}
			break;
		default:
			poll_result->rc_data = ir->urb_data[0]
					| ir->urb_data[1] << 8;
			break;
		}
	} else {
		tm6000_set_reg(dev, REQ_04_EN_DISABLE_MCU_INT, 2, 0);
		msleep(10);
		tm6000_set_reg(dev, REQ_04_EN_DISABLE_MCU_INT, 2, 1);
		msleep(10);

		if (ir->rc_type == RC_TYPE_RC5) {
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
	struct tm6000_core *dev = ir->dev;
	int result;
	struct tm6000_ir_poll_result poll_result;

	/* read the registers containing the IR status */
	result = ir->get_key(ir, &poll_result);
	if (result < 0) {
		printk(KERN_INFO "ir->get_key() failed %d\n", result);
		return;
	}

	dprintk("ir->get_key result data=%04x\n", poll_result.rc_data);

	if (ir->pwled) {
		if (ir->pwledcnt >= PWLED_OFF) {
			ir->pwled = 0;
			ir->pwledcnt = 0;
			tm6000_flash_led(dev, 1);
		} else
			ir->pwledcnt += 1;
	}

	if (ir->key) {
		rc_keydown(ir->rc, poll_result.rc_data, 0);
		ir->key = 0;
		ir->pwled = 1;
		ir->pwledcnt = 0;
		tm6000_flash_led(dev, 0);
	}
	return;
}

static void tm6000_ir_work(struct work_struct *work)
{
	struct tm6000_IR *ir = container_of(work, struct tm6000_IR, work.work);

	tm6000_ir_handle_key(ir);
	schedule_delayed_work(&ir->work, msecs_to_jiffies(ir->polling));
}

static int tm6000_ir_start(struct rc_dev *rc)
{
	struct tm6000_IR *ir = rc->priv;

	INIT_DELAYED_WORK(&ir->work, tm6000_ir_work);
	schedule_delayed_work(&ir->work, 0);

	return 0;
}

static void tm6000_ir_stop(struct rc_dev *rc)
{
	struct tm6000_IR *ir = rc->priv;

	cancel_delayed_work_sync(&ir->work);
}

static int tm6000_ir_change_protocol(struct rc_dev *rc, u64 rc_type)
{
	struct tm6000_IR *ir = rc->priv;

	if (!ir)
		return 0;

	if ((rc->rc_map.scan) && (rc_type == RC_TYPE_NEC))
		ir->key_addr = ((rc->rc_map.scan[0].scancode >> 8) & 0xffff);

	ir->get_key = default_polling_getkey;
	ir->rc_type = rc_type;

	tm6000_ir_config(ir);
	/* TODO */
	return 0;
}

int tm6000_ir_int_start(struct tm6000_core *dev)
{
	struct tm6000_IR *ir = dev->ir;
	int pipe, size;
	int err = -ENOMEM;


	if (!ir)
		return -ENODEV;

	ir->int_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ir->int_urb)
		return -ENOMEM;

	pipe = usb_rcvintpipe(dev->udev,
		dev->int_in.endp->desc.bEndpointAddress
		& USB_ENDPOINT_NUMBER_MASK);

	size = usb_maxpacket(dev->udev, pipe, usb_pipeout(pipe));
	dprintk("IR max size: %d\n", size);

	ir->int_urb->transfer_buffer = kzalloc(size, GFP_KERNEL);
	if (ir->int_urb->transfer_buffer == NULL) {
		usb_free_urb(ir->int_urb);
		return err;
	}
	dprintk("int interval: %d\n", dev->int_in.endp->desc.bInterval);
	usb_fill_int_urb(ir->int_urb, dev->udev, pipe,
		ir->int_urb->transfer_buffer, size,
		tm6000_ir_urb_received, dev,
		dev->int_in.endp->desc.bInterval);
	err = usb_submit_urb(ir->int_urb, GFP_KERNEL);
	if (err) {
		kfree(ir->int_urb->transfer_buffer);
		usb_free_urb(ir->int_urb);
		return err;
	}
	ir->urb_data = kzalloc(size, GFP_KERNEL);

	return 0;
}

void tm6000_ir_int_stop(struct tm6000_core *dev)
{
	struct tm6000_IR *ir = dev->ir;

	if (!ir)
		return;

	usb_kill_urb(ir->int_urb);
	kfree(ir->int_urb->transfer_buffer);
	usb_free_urb(ir->int_urb);
	ir->int_urb = NULL;
	kfree(ir->urb_data);
	ir->urb_data = NULL;
}

int tm6000_ir_init(struct tm6000_core *dev)
{
	struct tm6000_IR *ir;
	struct rc_dev *rc;
	int err = -ENOMEM;

	if (!enable_ir)
		return -ENODEV;

	if (!dev->caps.has_remote)
		return 0;

	if (!dev->ir_codes)
		return 0;

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	rc = rc_allocate_device();
	if (!ir || !rc)
		goto out;

	/* record handles to ourself */
	ir->dev = dev;
	dev->ir = ir;
	ir->rc = rc;

	/* input einrichten */
	rc->allowed_protos = RC_TYPE_RC5 | RC_TYPE_NEC;
	rc->priv = ir;
	rc->change_protocol = tm6000_ir_change_protocol;
	rc->open = tm6000_ir_start;
	rc->close = tm6000_ir_stop;
	rc->driver_type = RC_DRIVER_SCANCODE;

	ir->polling = 50;
	ir->pwled = 0;
	ir->pwledcnt = 0;


	snprintf(ir->name, sizeof(ir->name), "tm5600/60x0 IR (%s)",
						dev->name);

	usb_make_path(dev->udev, ir->phys, sizeof(ir->phys));
	strlcat(ir->phys, "/input0", sizeof(ir->phys));

	tm6000_ir_change_protocol(rc, RC_TYPE_UNKNOWN);

	rc->input_name = ir->name;
	rc->input_phys = ir->phys;
	rc->input_id.bustype = BUS_USB;
	rc->input_id.version = 1;
	rc->input_id.vendor = le16_to_cpu(dev->udev->descriptor.idVendor);
	rc->input_id.product = le16_to_cpu(dev->udev->descriptor.idProduct);
	rc->map_name = dev->ir_codes;
	rc->driver_name = "tm6000";
	rc->dev.parent = &dev->udev->dev;

	if (&dev->int_in) {
		dprintk("IR over int\n");

		err = tm6000_ir_int_start(dev);

		if (err)
			goto out;
	}

	/* ir register */
	err = rc_register_device(rc);
	if (err)
		goto out;

	return 0;

out:
	dev->ir = NULL;
	rc_free_device(rc);
	kfree(ir);
	return err;
}

int tm6000_ir_fini(struct tm6000_core *dev)
{
	struct tm6000_IR *ir = dev->ir;

	/* skip detach on non attached board */

	if (!ir)
		return 0;

	rc_unregister_device(ir->rc);

	if (ir->int_urb)
		tm6000_ir_int_stop(dev);

	kfree(ir);
	dev->ir = NULL;

	return 0;
}
