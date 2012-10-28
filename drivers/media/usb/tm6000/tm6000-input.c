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
MODULE_PARM_DESC(ir_debug, "debug message level");

static unsigned int enable_ir = 1;
module_param(enable_ir, int, 0644);
MODULE_PARM_DESC(enable_ir, "enable ir (default is enable)");

static unsigned int ir_clock_mhz = 12;
module_param(ir_clock_mhz, int, 0644);
MODULE_PARM_DESC(enable_ir, "ir clock, in MHz");

#define URB_SUBMIT_DELAY	100	/* ms - Delay to submit an URB request on retrial and init */
#define URB_INT_LED_DELAY	100	/* ms - Delay to turn led on again on int mode */

#undef dprintk

#define dprintk(level, fmt, arg...) do {\
	if (ir_debug >= level) \
		printk(KERN_DEBUG "%s/ir: " fmt, ir->name , ## arg); \
	} while (0)

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
	u8			pwled:2;
	u8			submit_urb:1;
	u16			key_addr;
	struct urb		*int_urb;

	/* IR device properties */
	u64			rc_type;
};

void tm6000_ir_wait(struct tm6000_core *dev, u8 state)
{
	struct tm6000_IR *ir = dev->ir;

	if (!dev->ir)
		return;

	dprintk(2, "%s: %i\n",__func__, ir->wait);

	if (state)
		ir->wait = 1;
	else
		ir->wait = 0;
}

static int tm6000_ir_config(struct tm6000_IR *ir)
{
	struct tm6000_core *dev = ir->dev;
	u32 pulse = 0, leader = 0;

	dprintk(2, "%s\n",__func__);

	/*
	 * The IR decoder supports RC-5 or NEC, with a configurable timing.
	 * The timing configuration there is not that accurate, as it uses
	 * approximate values. The NEC spec mentions a 562.5 unit period,
	 * and RC-5 uses a 888.8 period.
	 * Currently, driver assumes a clock provided by a 12 MHz XTAL, but
	 * a modprobe parameter can adjust it.
	 * Adjustments are required for other timings.
	 * It seems that the 900ms timing for NEC is used to detect a RC-5
	 * IR, in order to discard such decoding
	 */

	switch (ir->rc_type) {
	case RC_TYPE_NEC:
		leader = 900;	/* ms */
		pulse  = 700;	/* ms - the actual value would be 562 */
		break;
	default:
	case RC_TYPE_RC5:
		leader = 900;	/* ms - from the NEC decoding */
		pulse  = 1780;	/* ms - The actual value would be 1776 */
		break;
	}

	pulse = ir_clock_mhz * pulse;
	leader = ir_clock_mhz * leader;
	if (ir->rc_type == RC_TYPE_NEC)
		leader = leader | 0x8000;

	dprintk(2, "%s: %s, %d MHz, leader = 0x%04x, pulse = 0x%06x \n",
		__func__,
		(ir->rc_type == RC_TYPE_NEC) ? "NEC" : "RC-5",
		ir_clock_mhz, leader, pulse);

	/* Remote WAKEUP = enable, normal mode, from IR decoder output */
	tm6000_set_reg(dev, TM6010_REQ07_RE5_REMOTE_WAKEUP, 0xfe);

	/* Enable IR reception on non-busrt mode */
	tm6000_set_reg(dev, TM6010_REQ07_RD8_IR, 0x2f);

	/* IR_WKUP_SEL = Low byte in decoded IR data */
	tm6000_set_reg(dev, TM6010_REQ07_RDA_IR_WAKEUP_SEL, 0xff);
	/* IR_WKU_ADD code */
	tm6000_set_reg(dev, TM6010_REQ07_RDB_IR_WAKEUP_ADD, 0xff);

	tm6000_set_reg(dev, TM6010_REQ07_RDC_IR_LEADER1, leader >> 8);
	tm6000_set_reg(dev, TM6010_REQ07_RDD_IR_LEADER0, leader);

	tm6000_set_reg(dev, TM6010_REQ07_RDE_IR_PULSE_CNT1, pulse >> 8);
	tm6000_set_reg(dev, TM6010_REQ07_RDF_IR_PULSE_CNT0, pulse);

	if (!ir->polling)
		tm6000_set_reg(dev, REQ_04_EN_DISABLE_MCU_INT, 2, 0);
	else
		tm6000_set_reg(dev, REQ_04_EN_DISABLE_MCU_INT, 2, 1);
	msleep(10);

	/* Shows that IR is working via the LED */
	tm6000_flash_led(dev, 0);
	msleep(100);
	tm6000_flash_led(dev, 1);
	ir->pwled = 1;

	return 0;
}

static void tm6000_ir_urb_received(struct urb *urb)
{
	struct tm6000_core *dev = urb->context;
	struct tm6000_IR *ir = dev->ir;
	struct tm6000_ir_poll_result poll_result;
	char *buf;

	dprintk(2, "%s\n",__func__);
	if (urb->status < 0 || urb->actual_length <= 0) {
		printk(KERN_INFO "tm6000: IR URB failure: status: %i, length %i\n",
		       urb->status, urb->actual_length);
		ir->submit_urb = 1;
		schedule_delayed_work(&ir->work, msecs_to_jiffies(URB_SUBMIT_DELAY));
		return;
	}
	buf = urb->transfer_buffer;

	if (ir_debug)
		print_hex_dump(KERN_DEBUG, "tm6000: IR data: ",
			       DUMP_PREFIX_OFFSET,16, 1,
			       buf, urb->actual_length, false);

	poll_result.rc_data = buf[0];
	if (urb->actual_length > 1)
		poll_result.rc_data |= buf[1] << 8;

	dprintk(1, "%s, scancode: 0x%04x\n",__func__, poll_result.rc_data);
	rc_keydown(ir->rc, poll_result.rc_data, 0);

	usb_submit_urb(urb, GFP_ATOMIC);
	/*
	 * Flash the led. We can't do it here, as it is running on IRQ context.
	 * So, use the scheduler to do it, in a few ms.
	 */
	ir->pwled = 2;
	schedule_delayed_work(&ir->work, msecs_to_jiffies(10));
}

static void tm6000_ir_handle_key(struct work_struct *work)
{
	struct tm6000_IR *ir = container_of(work, struct tm6000_IR, work.work);
	struct tm6000_core *dev = ir->dev;
	struct tm6000_ir_poll_result poll_result;
	int rc;
	u8 buf[2];

	if (ir->wait)
		return;

	dprintk(3, "%s\n",__func__);

	rc = tm6000_read_write_usb(dev, USB_DIR_IN |
		USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		REQ_02_GET_IR_CODE, 0, 0, buf, 2);
	if (rc < 0)
		return;

	if (rc > 1)
		poll_result.rc_data = buf[0] | buf[1] << 8;
	else
		poll_result.rc_data = buf[0];

	/* Check if something was read */
	if ((poll_result.rc_data & 0xff) == 0xff) {
		if (!ir->pwled) {
			tm6000_flash_led(dev, 1);
			ir->pwled = 1;
		}
		return;
	}

	dprintk(1, "%s, scancode: 0x%04x\n",__func__, poll_result.rc_data);
	rc_keydown(ir->rc, poll_result.rc_data, 0);
	tm6000_flash_led(dev, 0);
	ir->pwled = 0;

	/* Re-schedule polling */
	schedule_delayed_work(&ir->work, msecs_to_jiffies(ir->polling));
}

static void tm6000_ir_int_work(struct work_struct *work)
{
	struct tm6000_IR *ir = container_of(work, struct tm6000_IR, work.work);
	struct tm6000_core *dev = ir->dev;
	int rc;

	dprintk(3, "%s, submit_urb = %d, pwled = %d\n",__func__, ir->submit_urb,
		ir->pwled);

	if (ir->submit_urb) {
		dprintk(3, "Resubmit urb\n");
		tm6000_set_reg(dev, REQ_04_EN_DISABLE_MCU_INT, 2, 0);

		rc = usb_submit_urb(ir->int_urb, GFP_ATOMIC);
		if (rc < 0) {
			printk(KERN_ERR "tm6000: Can't submit an IR interrupt. Error %i\n",
			       rc);
			/* Retry in 100 ms */
			schedule_delayed_work(&ir->work, msecs_to_jiffies(URB_SUBMIT_DELAY));
			return;
		}
		ir->submit_urb = 0;
	}

	/* Led is enabled only if USB submit doesn't fail */
	if (ir->pwled == 2) {
		tm6000_flash_led(dev, 0);
		ir->pwled = 0;
		schedule_delayed_work(&ir->work, msecs_to_jiffies(URB_INT_LED_DELAY));
	} else if (!ir->pwled) {
		tm6000_flash_led(dev, 1);
		ir->pwled = 1;
	}
}

static int tm6000_ir_start(struct rc_dev *rc)
{
	struct tm6000_IR *ir = rc->priv;

	dprintk(2, "%s\n",__func__);

	schedule_delayed_work(&ir->work, 0);

	return 0;
}

static void tm6000_ir_stop(struct rc_dev *rc)
{
	struct tm6000_IR *ir = rc->priv;

	dprintk(2, "%s\n",__func__);

	cancel_delayed_work_sync(&ir->work);
}

static int tm6000_ir_change_protocol(struct rc_dev *rc, u64 rc_type)
{
	struct tm6000_IR *ir = rc->priv;

	if (!ir)
		return 0;

	dprintk(2, "%s\n",__func__);

	if ((rc->rc_map.scan) && (rc_type == RC_TYPE_NEC))
		ir->key_addr = ((rc->rc_map.scan[0].scancode >> 8) & 0xffff);

	ir->rc_type = rc_type;

	tm6000_ir_config(ir);
	/* TODO */
	return 0;
}

static int __tm6000_ir_int_start(struct rc_dev *rc)
{
	struct tm6000_IR *ir = rc->priv;
	struct tm6000_core *dev;
	int pipe, size;
	int err = -ENOMEM;

	if (!ir)
		return -ENODEV;
	dev = ir->dev;

	dprintk(2, "%s\n",__func__);

	ir->int_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!ir->int_urb)
		return -ENOMEM;

	pipe = usb_rcvintpipe(dev->udev,
		dev->int_in.endp->desc.bEndpointAddress
		& USB_ENDPOINT_NUMBER_MASK);

	size = usb_maxpacket(dev->udev, pipe, usb_pipeout(pipe));
	dprintk(1, "IR max size: %d\n", size);

	ir->int_urb->transfer_buffer = kzalloc(size, GFP_ATOMIC);
	if (ir->int_urb->transfer_buffer == NULL) {
		usb_free_urb(ir->int_urb);
		return err;
	}
	dprintk(1, "int interval: %d\n", dev->int_in.endp->desc.bInterval);

	usb_fill_int_urb(ir->int_urb, dev->udev, pipe,
		ir->int_urb->transfer_buffer, size,
		tm6000_ir_urb_received, dev,
		dev->int_in.endp->desc.bInterval);

	ir->submit_urb = 1;
	schedule_delayed_work(&ir->work, msecs_to_jiffies(URB_SUBMIT_DELAY));

	return 0;
}

static void __tm6000_ir_int_stop(struct rc_dev *rc)
{
	struct tm6000_IR *ir = rc->priv;

	if (!ir || !ir->int_urb)
		return;

	dprintk(2, "%s\n",__func__);

	usb_kill_urb(ir->int_urb);
	kfree(ir->int_urb->transfer_buffer);
	usb_free_urb(ir->int_urb);
	ir->int_urb = NULL;
}

int tm6000_ir_int_start(struct tm6000_core *dev)
{
	struct tm6000_IR *ir = dev->ir;

	if (!ir)
		return 0;

	return __tm6000_ir_int_start(ir->rc);
}

void tm6000_ir_int_stop(struct tm6000_core *dev)
{
	struct tm6000_IR *ir = dev->ir;

	if (!ir || !ir->rc)
		return;

	__tm6000_ir_int_stop(ir->rc);
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

	ir = kzalloc(sizeof(*ir), GFP_ATOMIC);
	rc = rc_allocate_device();
	if (!ir || !rc)
		goto out;

	dprintk(2, "%s\n", __func__);

	/* record handles to ourself */
	ir->dev = dev;
	dev->ir = ir;
	ir->rc = rc;

	/* input setup */
	rc->allowed_protos = RC_TYPE_RC5 | RC_TYPE_NEC;
	/* Neded, in order to support NEC remotes with 24 or 32 bits */
	rc->scanmask = 0xffff;
	rc->priv = ir;
	rc->change_protocol = tm6000_ir_change_protocol;
	if (dev->int_in.endp) {
		rc->open    = __tm6000_ir_int_start;
		rc->close   = __tm6000_ir_int_stop;
		INIT_DELAYED_WORK(&ir->work, tm6000_ir_int_work);
	} else {
		rc->open  = tm6000_ir_start;
		rc->close = tm6000_ir_stop;
		ir->polling = 50;
		INIT_DELAYED_WORK(&ir->work, tm6000_ir_handle_key);
	}
	rc->driver_type = RC_DRIVER_SCANCODE;

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

	dprintk(2, "%s\n",__func__);

	if (!ir->polling)
		__tm6000_ir_int_stop(ir->rc);

	tm6000_ir_stop(ir->rc);

	/* Turn off the led */
	tm6000_flash_led(dev, 0);
	ir->pwled = 0;

	rc_unregister_device(ir->rc);

	kfree(ir);
	dev->ir = NULL;

	return 0;
}
