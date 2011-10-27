/*
 *  USB HID support for Linux
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2007-2008 Oliver Neukum
 *  Copyright (c) 2006-2010 Jiri Kosina
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <asm/unaligned.h>
#include <asm/byteorder.h>
#include <linux/input.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <linux/usb.h>

#include <linux/hid.h>
#include <linux/hiddev.h>
#include <linux/hid-debug.h>
#include <linux/hidraw.h>
#include "usbhid.h"

/*
 * Version Information
 */

#define DRIVER_DESC "USB HID core driver"
#define DRIVER_LICENSE "GPL"

/*
 * Module parameters.
 */

static unsigned int hid_mousepoll_interval;
module_param_named(mousepoll, hid_mousepoll_interval, uint, 0644);
MODULE_PARM_DESC(mousepoll, "Polling interval of mice");

static unsigned int ignoreled;
module_param_named(ignoreled, ignoreled, uint, 0644);
MODULE_PARM_DESC(ignoreled, "Autosuspend with active leds");

/* Quirks specified at module load time */
static char *quirks_param[MAX_USBHID_BOOT_QUIRKS] = { [ 0 ... (MAX_USBHID_BOOT_QUIRKS - 1) ] = NULL };
module_param_array_named(quirks, quirks_param, charp, NULL, 0444);
MODULE_PARM_DESC(quirks, "Add/modify USB HID quirks by specifying "
		" quirks=vendorID:productID:quirks"
		" where vendorID, productID, and quirks are all in"
		" 0x-prefixed hex");
/*
 * Input submission and I/O error handler.
 */
static DEFINE_MUTEX(hid_open_mut);

static void hid_io_error(struct hid_device *hid);
static int hid_submit_out(struct hid_device *hid);
static int hid_submit_ctrl(struct hid_device *hid);
static void hid_cancel_delayed_stuff(struct usbhid_device *usbhid);

/* Start up the input URB */
static int hid_start_in(struct hid_device *hid)
{
	unsigned long flags;
	int rc = 0;
	struct usbhid_device *usbhid = hid->driver_data;

	spin_lock_irqsave(&usbhid->lock, flags);
	if (hid->open > 0 &&
			!test_bit(HID_DISCONNECTED, &usbhid->iofl) &&
			!test_bit(HID_REPORTED_IDLE, &usbhid->iofl) &&
			!test_and_set_bit(HID_IN_RUNNING, &usbhid->iofl)) {
		rc = usb_submit_urb(usbhid->urbin, GFP_ATOMIC);
		if (rc != 0)
			clear_bit(HID_IN_RUNNING, &usbhid->iofl);
	}
	spin_unlock_irqrestore(&usbhid->lock, flags);
	return rc;
}

/* I/O retry timer routine */
static void hid_retry_timeout(unsigned long _hid)
{
	struct hid_device *hid = (struct hid_device *) _hid;
	struct usbhid_device *usbhid = hid->driver_data;

	dev_dbg(&usbhid->intf->dev, "retrying intr urb\n");
	if (hid_start_in(hid))
		hid_io_error(hid);
}

/* Workqueue routine to reset the device or clear a halt */
static void hid_reset(struct work_struct *work)
{
	struct usbhid_device *usbhid =
		container_of(work, struct usbhid_device, reset_work);
	struct hid_device *hid = usbhid->hid;
	int rc = 0;

	if (test_bit(HID_CLEAR_HALT, &usbhid->iofl)) {
		dev_dbg(&usbhid->intf->dev, "clear halt\n");
		rc = usb_clear_halt(hid_to_usb_dev(hid), usbhid->urbin->pipe);
		clear_bit(HID_CLEAR_HALT, &usbhid->iofl);
		hid_start_in(hid);
	}

	else if (test_bit(HID_RESET_PENDING, &usbhid->iofl)) {
		dev_dbg(&usbhid->intf->dev, "resetting device\n");
		rc = usb_lock_device_for_reset(hid_to_usb_dev(hid), usbhid->intf);
		if (rc == 0) {
			rc = usb_reset_device(hid_to_usb_dev(hid));
			usb_unlock_device(hid_to_usb_dev(hid));
		}
		clear_bit(HID_RESET_PENDING, &usbhid->iofl);
	}

	switch (rc) {
	case 0:
		if (!test_bit(HID_IN_RUNNING, &usbhid->iofl))
			hid_io_error(hid);
		break;
	default:
		hid_err(hid, "can't reset device, %s-%s/input%d, status %d\n",
			hid_to_usb_dev(hid)->bus->bus_name,
			hid_to_usb_dev(hid)->devpath,
			usbhid->ifnum, rc);
		/* FALLTHROUGH */
	case -EHOSTUNREACH:
	case -ENODEV:
	case -EINTR:
		break;
	}
}

/* Main I/O error handler */
static void hid_io_error(struct hid_device *hid)
{
	unsigned long flags;
	struct usbhid_device *usbhid = hid->driver_data;

	spin_lock_irqsave(&usbhid->lock, flags);

	/* Stop when disconnected */
	if (test_bit(HID_DISCONNECTED, &usbhid->iofl))
		goto done;

	/* If it has been a while since the last error, we'll assume
	 * this a brand new error and reset the retry timeout. */
	if (time_after(jiffies, usbhid->stop_retry + HZ/2))
		usbhid->retry_delay = 0;

	/* When an error occurs, retry at increasing intervals */
	if (usbhid->retry_delay == 0) {
		usbhid->retry_delay = 13;	/* Then 26, 52, 104, 104, ... */
		usbhid->stop_retry = jiffies + msecs_to_jiffies(1000);
	} else if (usbhid->retry_delay < 100)
		usbhid->retry_delay *= 2;

	if (time_after(jiffies, usbhid->stop_retry)) {

		/* Retries failed, so do a port reset */
		if (!test_and_set_bit(HID_RESET_PENDING, &usbhid->iofl)) {
			schedule_work(&usbhid->reset_work);
			goto done;
		}
	}

	mod_timer(&usbhid->io_retry,
			jiffies + msecs_to_jiffies(usbhid->retry_delay));
done:
	spin_unlock_irqrestore(&usbhid->lock, flags);
}

static void usbhid_mark_busy(struct usbhid_device *usbhid)
{
	struct usb_interface *intf = usbhid->intf;

	usb_mark_last_busy(interface_to_usbdev(intf));
}

static int usbhid_restart_out_queue(struct usbhid_device *usbhid)
{
	struct hid_device *hid = usb_get_intfdata(usbhid->intf);
	int kicked;

	if (!hid)
		return 0;

	if ((kicked = (usbhid->outhead != usbhid->outtail))) {
		dbg("Kicking head %d tail %d", usbhid->outhead, usbhid->outtail);
		if (hid_submit_out(hid)) {
			clear_bit(HID_OUT_RUNNING, &usbhid->iofl);
			wake_up(&usbhid->wait);
		}
	}
	return kicked;
}

static int usbhid_restart_ctrl_queue(struct usbhid_device *usbhid)
{
	struct hid_device *hid = usb_get_intfdata(usbhid->intf);
	int kicked;

	WARN_ON(hid == NULL);
	if (!hid)
		return 0;

	if ((kicked = (usbhid->ctrlhead != usbhid->ctrltail))) {
		dbg("Kicking head %d tail %d", usbhid->ctrlhead, usbhid->ctrltail);
		if (hid_submit_ctrl(hid)) {
			clear_bit(HID_CTRL_RUNNING, &usbhid->iofl);
			wake_up(&usbhid->wait);
		}
	}
	return kicked;
}

/*
 * Input interrupt completion handler.
 */

static void hid_irq_in(struct urb *urb)
{
	struct hid_device	*hid = urb->context;
	struct usbhid_device 	*usbhid = hid->driver_data;
	int			status;

	switch (urb->status) {
	case 0:			/* success */
		usbhid_mark_busy(usbhid);
		usbhid->retry_delay = 0;
		hid_input_report(urb->context, HID_INPUT_REPORT,
				 urb->transfer_buffer,
				 urb->actual_length, 1);
		/*
		 * autosuspend refused while keys are pressed
		 * because most keyboards don't wake up when
		 * a key is released
		 */
		if (hid_check_keys_pressed(hid))
			set_bit(HID_KEYS_PRESSED, &usbhid->iofl);
		else
			clear_bit(HID_KEYS_PRESSED, &usbhid->iofl);
		break;
	case -EPIPE:		/* stall */
		usbhid_mark_busy(usbhid);
		clear_bit(HID_IN_RUNNING, &usbhid->iofl);
		set_bit(HID_CLEAR_HALT, &usbhid->iofl);
		schedule_work(&usbhid->reset_work);
		return;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:	/* unplug */
		clear_bit(HID_IN_RUNNING, &usbhid->iofl);
		return;
	case -EILSEQ:		/* protocol error or unplug */
	case -EPROTO:		/* protocol error or unplug */
	case -ETIME:		/* protocol error or unplug */
	case -ETIMEDOUT:	/* Should never happen, but... */
		usbhid_mark_busy(usbhid);
		clear_bit(HID_IN_RUNNING, &usbhid->iofl);
		hid_io_error(hid);
		return;
	default:		/* error */
		hid_warn(urb->dev, "input irq status %d received\n",
			 urb->status);
	}

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		clear_bit(HID_IN_RUNNING, &usbhid->iofl);
		if (status != -EPERM) {
			hid_err(hid, "can't resubmit intr, %s-%s/input%d, status %d\n",
				hid_to_usb_dev(hid)->bus->bus_name,
				hid_to_usb_dev(hid)->devpath,
				usbhid->ifnum, status);
			hid_io_error(hid);
		}
	}
}

static int hid_submit_out(struct hid_device *hid)
{
	struct hid_report *report;
	char *raw_report;
	struct usbhid_device *usbhid = hid->driver_data;
	int r;

	report = usbhid->out[usbhid->outtail].report;
	raw_report = usbhid->out[usbhid->outtail].raw_report;

	r = usb_autopm_get_interface_async(usbhid->intf);
	if (r < 0)
		return -1;

	/*
	 * if the device hasn't been woken, we leave the output
	 * to resume()
	 */
	if (!test_bit(HID_REPORTED_IDLE, &usbhid->iofl)) {
		usbhid->urbout->transfer_buffer_length = ((report->size - 1) >> 3) + 1 + (report->id > 0);
		usbhid->urbout->dev = hid_to_usb_dev(hid);
		memcpy(usbhid->outbuf, raw_report, usbhid->urbout->transfer_buffer_length);
		kfree(raw_report);

		dbg_hid("submitting out urb\n");

		if (usb_submit_urb(usbhid->urbout, GFP_ATOMIC)) {
			hid_err(hid, "usb_submit_urb(out) failed\n");
			usb_autopm_put_interface_async(usbhid->intf);
			return -1;
		}
		usbhid->last_out = jiffies;
	}

	return 0;
}

static int hid_submit_ctrl(struct hid_device *hid)
{
	struct hid_report *report;
	unsigned char dir;
	char *raw_report;
	int len, r;
	struct usbhid_device *usbhid = hid->driver_data;

	report = usbhid->ctrl[usbhid->ctrltail].report;
	raw_report = usbhid->ctrl[usbhid->ctrltail].raw_report;
	dir = usbhid->ctrl[usbhid->ctrltail].dir;

	r = usb_autopm_get_interface_async(usbhid->intf);
	if (r < 0)
		return -1;
	if (!test_bit(HID_REPORTED_IDLE, &usbhid->iofl)) {
		len = ((report->size - 1) >> 3) + 1 + (report->id > 0);
		if (dir == USB_DIR_OUT) {
			usbhid->urbctrl->pipe = usb_sndctrlpipe(hid_to_usb_dev(hid), 0);
			usbhid->urbctrl->transfer_buffer_length = len;
			memcpy(usbhid->ctrlbuf, raw_report, len);
			kfree(raw_report);
		} else {
			int maxpacket, padlen;

			usbhid->urbctrl->pipe = usb_rcvctrlpipe(hid_to_usb_dev(hid), 0);
			maxpacket = usb_maxpacket(hid_to_usb_dev(hid), usbhid->urbctrl->pipe, 0);
			if (maxpacket > 0) {
				padlen = DIV_ROUND_UP(len, maxpacket);
				padlen *= maxpacket;
				if (padlen > usbhid->bufsize)
					padlen = usbhid->bufsize;
			} else
				padlen = 0;
			usbhid->urbctrl->transfer_buffer_length = padlen;
		}
		usbhid->urbctrl->dev = hid_to_usb_dev(hid);

		usbhid->cr->bRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE | dir;
		usbhid->cr->bRequest = (dir == USB_DIR_OUT) ? HID_REQ_SET_REPORT : HID_REQ_GET_REPORT;
		usbhid->cr->wValue = cpu_to_le16(((report->type + 1) << 8) | report->id);
		usbhid->cr->wIndex = cpu_to_le16(usbhid->ifnum);
		usbhid->cr->wLength = cpu_to_le16(len);

		dbg_hid("submitting ctrl urb: %s wValue=0x%04x wIndex=0x%04x wLength=%u\n",
			usbhid->cr->bRequest == HID_REQ_SET_REPORT ? "Set_Report" : "Get_Report",
			usbhid->cr->wValue, usbhid->cr->wIndex, usbhid->cr->wLength);

		if (usb_submit_urb(usbhid->urbctrl, GFP_ATOMIC)) {
			usb_autopm_put_interface_async(usbhid->intf);
			hid_err(hid, "usb_submit_urb(ctrl) failed\n");
			return -1;
		}
		usbhid->last_ctrl = jiffies;
	}

	return 0;
}

/*
 * Output interrupt completion handler.
 */

static void hid_irq_out(struct urb *urb)
{
	struct hid_device *hid = urb->context;
	struct usbhid_device *usbhid = hid->driver_data;
	unsigned long flags;
	int unplug = 0;

	switch (urb->status) {
	case 0:			/* success */
		break;
	case -ESHUTDOWN:	/* unplug */
		unplug = 1;
	case -EILSEQ:		/* protocol error or unplug */
	case -EPROTO:		/* protocol error or unplug */
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
		break;
	default:		/* error */
		hid_warn(urb->dev, "output irq status %d received\n",
			 urb->status);
	}

	spin_lock_irqsave(&usbhid->lock, flags);

	if (unplug)
		usbhid->outtail = usbhid->outhead;
	else
		usbhid->outtail = (usbhid->outtail + 1) & (HID_OUTPUT_FIFO_SIZE - 1);

	if (usbhid->outhead != usbhid->outtail) {
		if (hid_submit_out(hid)) {
			clear_bit(HID_OUT_RUNNING, &usbhid->iofl);
			wake_up(&usbhid->wait);
		}
		spin_unlock_irqrestore(&usbhid->lock, flags);
		return;
	}

	clear_bit(HID_OUT_RUNNING, &usbhid->iofl);
	spin_unlock_irqrestore(&usbhid->lock, flags);
	usb_autopm_put_interface_async(usbhid->intf);
	wake_up(&usbhid->wait);
}

/*
 * Control pipe completion handler.
 */

static void hid_ctrl(struct urb *urb)
{
	struct hid_device *hid = urb->context;
	struct usbhid_device *usbhid = hid->driver_data;
	int unplug = 0, status = urb->status;

	spin_lock(&usbhid->lock);

	switch (status) {
	case 0:			/* success */
		if (usbhid->ctrl[usbhid->ctrltail].dir == USB_DIR_IN)
			hid_input_report(urb->context,
				usbhid->ctrl[usbhid->ctrltail].report->type,
				urb->transfer_buffer, urb->actual_length, 0);
		break;
	case -ESHUTDOWN:	/* unplug */
		unplug = 1;
	case -EILSEQ:		/* protocol error or unplug */
	case -EPROTO:		/* protocol error or unplug */
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -EPIPE:		/* report not available */
		break;
	default:		/* error */
		hid_warn(urb->dev, "ctrl urb status %d received\n", status);
	}

	if (unplug)
		usbhid->ctrltail = usbhid->ctrlhead;
	else
		usbhid->ctrltail = (usbhid->ctrltail + 1) & (HID_CONTROL_FIFO_SIZE - 1);

	if (usbhid->ctrlhead != usbhid->ctrltail) {
		if (hid_submit_ctrl(hid)) {
			clear_bit(HID_CTRL_RUNNING, &usbhid->iofl);
			wake_up(&usbhid->wait);
		}
		spin_unlock(&usbhid->lock);
		usb_autopm_put_interface_async(usbhid->intf);
		return;
	}

	clear_bit(HID_CTRL_RUNNING, &usbhid->iofl);
	spin_unlock(&usbhid->lock);
	usb_autopm_put_interface_async(usbhid->intf);
	wake_up(&usbhid->wait);
}

static void __usbhid_submit_report(struct hid_device *hid, struct hid_report *report,
				   unsigned char dir)
{
	int head;
	struct usbhid_device *usbhid = hid->driver_data;
	int len = ((report->size - 1) >> 3) + 1 + (report->id > 0);

	if ((hid->quirks & HID_QUIRK_NOGET) && dir == USB_DIR_IN)
		return;

	if (usbhid->urbout && dir == USB_DIR_OUT && report->type == HID_OUTPUT_REPORT) {
		if ((head = (usbhid->outhead + 1) & (HID_OUTPUT_FIFO_SIZE - 1)) == usbhid->outtail) {
			hid_warn(hid, "output queue full\n");
			return;
		}

		usbhid->out[usbhid->outhead].raw_report = kmalloc(len, GFP_ATOMIC);
		if (!usbhid->out[usbhid->outhead].raw_report) {
			hid_warn(hid, "output queueing failed\n");
			return;
		}
		hid_output_report(report, usbhid->out[usbhid->outhead].raw_report);
		usbhid->out[usbhid->outhead].report = report;
		usbhid->outhead = head;

		if (!test_and_set_bit(HID_OUT_RUNNING, &usbhid->iofl)) {
			if (hid_submit_out(hid))
				clear_bit(HID_OUT_RUNNING, &usbhid->iofl);
		} else {
			/*
			 * the queue is known to run
			 * but an earlier request may be stuck
			 * we may need to time out
			 * no race because this is called under
			 * spinlock
			 */
			if (time_after(jiffies, usbhid->last_out + HZ * 5))
				usb_unlink_urb(usbhid->urbout);
		}
		return;
	}

	if ((head = (usbhid->ctrlhead + 1) & (HID_CONTROL_FIFO_SIZE - 1)) == usbhid->ctrltail) {
		hid_warn(hid, "control queue full\n");
		return;
	}

	if (dir == USB_DIR_OUT) {
		usbhid->ctrl[usbhid->ctrlhead].raw_report = kmalloc(len, GFP_ATOMIC);
		if (!usbhid->ctrl[usbhid->ctrlhead].raw_report) {
			hid_warn(hid, "control queueing failed\n");
			return;
		}
		hid_output_report(report, usbhid->ctrl[usbhid->ctrlhead].raw_report);
	}
	usbhid->ctrl[usbhid->ctrlhead].report = report;
	usbhid->ctrl[usbhid->ctrlhead].dir = dir;
	usbhid->ctrlhead = head;

	if (!test_and_set_bit(HID_CTRL_RUNNING, &usbhid->iofl)) {
		if (hid_submit_ctrl(hid))
			clear_bit(HID_CTRL_RUNNING, &usbhid->iofl);
	} else {
		/*
		 * the queue is known to run
		 * but an earlier request may be stuck
		 * we may need to time out
		 * no race because this is called under
		 * spinlock
		 */
		if (time_after(jiffies, usbhid->last_ctrl + HZ * 5))
			usb_unlink_urb(usbhid->urbctrl);
	}
}

void usbhid_submit_report(struct hid_device *hid, struct hid_report *report, unsigned char dir)
{
	struct usbhid_device *usbhid = hid->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&usbhid->lock, flags);
	__usbhid_submit_report(hid, report, dir);
	spin_unlock_irqrestore(&usbhid->lock, flags);
}
EXPORT_SYMBOL_GPL(usbhid_submit_report);

static int usb_hidinput_input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct usbhid_device *usbhid = hid->driver_data;
	struct hid_field *field;
	unsigned long flags;
	int offset;

	if (type == EV_FF)
		return input_ff_event(dev, type, code, value);

	if (type != EV_LED)
		return -1;

	if ((offset = hidinput_find_field(hid, type, code, &field)) == -1) {
		hid_warn(dev, "event field not found\n");
		return -1;
	}

	hid_set_field(field, offset, value);
	if (value) {
		spin_lock_irqsave(&usbhid->lock, flags);
		usbhid->ledcount++;
		spin_unlock_irqrestore(&usbhid->lock, flags);
	} else {
		spin_lock_irqsave(&usbhid->lock, flags);
		usbhid->ledcount--;
		spin_unlock_irqrestore(&usbhid->lock, flags);
	}
	usbhid_submit_report(hid, field->report, USB_DIR_OUT);

	return 0;
}

int usbhid_wait_io(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	if (!wait_event_timeout(usbhid->wait,
				(!test_bit(HID_CTRL_RUNNING, &usbhid->iofl) &&
				!test_bit(HID_OUT_RUNNING, &usbhid->iofl)),
					10*HZ)) {
		dbg_hid("timeout waiting for ctrl or out queue to clear\n");
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(usbhid_wait_io);

static int hid_set_idle(struct usb_device *dev, int ifnum, int report, int idle)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		HID_REQ_SET_IDLE, USB_TYPE_CLASS | USB_RECIP_INTERFACE, (idle << 8) | report,
		ifnum, NULL, 0, USB_CTRL_SET_TIMEOUT);
}

static int hid_get_class_descriptor(struct usb_device *dev, int ifnum,
		unsigned char type, void *buf, int size)
{
	int result, retries = 4;

	memset(buf, 0, size);

	do {
		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				USB_REQ_GET_DESCRIPTOR, USB_RECIP_INTERFACE | USB_DIR_IN,
				(type << 8), ifnum, buf, size, USB_CTRL_GET_TIMEOUT);
		retries--;
	} while (result < size && retries);
	return result;
}

int usbhid_open(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	int res;

	mutex_lock(&hid_open_mut);
	if (!hid->open++) {
		res = usb_autopm_get_interface(usbhid->intf);
		/* the device must be awake to reliably request remote wakeup */
		if (res < 0) {
			hid->open--;
			mutex_unlock(&hid_open_mut);
			return -EIO;
		}
		usbhid->intf->needs_remote_wakeup = 1;
		if (hid_start_in(hid))
			hid_io_error(hid);
 
		usb_autopm_put_interface(usbhid->intf);
	}
	mutex_unlock(&hid_open_mut);
	return 0;
}

void usbhid_close(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	mutex_lock(&hid_open_mut);

	/* protecting hid->open to make sure we don't restart
	 * data acquistion due to a resumption we no longer
	 * care about
	 */
	spin_lock_irq(&usbhid->lock);
	if (!--hid->open) {
		spin_unlock_irq(&usbhid->lock);
		hid_cancel_delayed_stuff(usbhid);
		usb_kill_urb(usbhid->urbin);
		usbhid->intf->needs_remote_wakeup = 0;
	} else {
		spin_unlock_irq(&usbhid->lock);
	}
	mutex_unlock(&hid_open_mut);
}

/*
 * Initialize all reports
 */

void usbhid_init_reports(struct hid_device *hid)
{
	struct hid_report *report;
	struct usbhid_device *usbhid = hid->driver_data;
	int err, ret;

	list_for_each_entry(report, &hid->report_enum[HID_INPUT_REPORT].report_list, list)
		usbhid_submit_report(hid, report, USB_DIR_IN);

	list_for_each_entry(report, &hid->report_enum[HID_FEATURE_REPORT].report_list, list)
		usbhid_submit_report(hid, report, USB_DIR_IN);

	err = 0;
	ret = usbhid_wait_io(hid);
	while (ret) {
		err |= ret;
		if (test_bit(HID_CTRL_RUNNING, &usbhid->iofl))
			usb_kill_urb(usbhid->urbctrl);
		if (test_bit(HID_OUT_RUNNING, &usbhid->iofl))
			usb_kill_urb(usbhid->urbout);
		ret = usbhid_wait_io(hid);
	}

	if (err)
		hid_warn(hid, "timeout initializing reports\n");
}

/*
 * Reset LEDs which BIOS might have left on. For now, just NumLock (0x01).
 */
static int hid_find_field_early(struct hid_device *hid, unsigned int page,
    unsigned int hid_code, struct hid_field **pfield)
{
	struct hid_report *report;
	struct hid_field *field;
	struct hid_usage *usage;
	int i, j;

	list_for_each_entry(report, &hid->report_enum[HID_OUTPUT_REPORT].report_list, list) {
		for (i = 0; i < report->maxfield; i++) {
			field = report->field[i];
			for (j = 0; j < field->maxusage; j++) {
				usage = &field->usage[j];
				if ((usage->hid & HID_USAGE_PAGE) == page &&
				    (usage->hid & 0xFFFF) == hid_code) {
					*pfield = field;
					return j;
				}
			}
		}
	}
	return -1;
}

void usbhid_set_leds(struct hid_device *hid)
{
	struct hid_field *field;
	int offset;

	if ((offset = hid_find_field_early(hid, HID_UP_LED, 0x01, &field)) != -1) {
		hid_set_field(field, offset, 0);
		usbhid_submit_report(hid, field->report, USB_DIR_OUT);
	}
}
EXPORT_SYMBOL_GPL(usbhid_set_leds);

/*
 * Traverse the supplied list of reports and find the longest
 */
static void hid_find_max_report(struct hid_device *hid, unsigned int type,
		unsigned int *max)
{
	struct hid_report *report;
	unsigned int size;

	list_for_each_entry(report, &hid->report_enum[type].report_list, list) {
		size = ((report->size - 1) >> 3) + 1 + hid->report_enum[type].numbered;
		if (*max < size)
			*max = size;
	}
}

static int hid_alloc_buffers(struct usb_device *dev, struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	usbhid->inbuf = usb_alloc_coherent(dev, usbhid->bufsize, GFP_KERNEL,
			&usbhid->inbuf_dma);
	usbhid->outbuf = usb_alloc_coherent(dev, usbhid->bufsize, GFP_KERNEL,
			&usbhid->outbuf_dma);
	usbhid->cr = kmalloc(sizeof(*usbhid->cr), GFP_KERNEL);
	usbhid->ctrlbuf = usb_alloc_coherent(dev, usbhid->bufsize, GFP_KERNEL,
			&usbhid->ctrlbuf_dma);
	if (!usbhid->inbuf || !usbhid->outbuf || !usbhid->cr ||
			!usbhid->ctrlbuf)
		return -1;

	return 0;
}

static int usbhid_get_raw_report(struct hid_device *hid,
		unsigned char report_number, __u8 *buf, size_t count,
		unsigned char report_type)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usb_device *dev = hid_to_usb_dev(hid);
	struct usb_interface *intf = usbhid->intf;
	struct usb_host_interface *interface = intf->cur_altsetting;
	int skipped_report_id = 0;
	int ret;

	/* Byte 0 is the report number. Report data starts at byte 1.*/
	buf[0] = report_number;
	if (report_number == 0x0) {
		/* Offset the return buffer by 1, so that the report ID
		   will remain in byte 0. */
		buf++;
		count--;
		skipped_report_id = 1;
	}
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		HID_REQ_GET_REPORT,
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		((report_type + 1) << 8) | report_number,
		interface->desc.bInterfaceNumber, buf, count,
		USB_CTRL_SET_TIMEOUT);

	/* count also the report id */
	if (ret > 0 && skipped_report_id)
		ret++;

	return ret;
}

static int usbhid_output_raw_report(struct hid_device *hid, __u8 *buf, size_t count,
		unsigned char report_type)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usb_device *dev = hid_to_usb_dev(hid);
	struct usb_interface *intf = usbhid->intf;
	struct usb_host_interface *interface = intf->cur_altsetting;
	int ret;

	if (usbhid->urbout && report_type != HID_FEATURE_REPORT) {
		int actual_length;
		int skipped_report_id = 0;

		if (buf[0] == 0x0) {
			/* Don't send the Report ID */
			buf++;
			count--;
			skipped_report_id = 1;
		}
		ret = usb_interrupt_msg(dev, usbhid->urbout->pipe,
			buf, count, &actual_length,
			USB_CTRL_SET_TIMEOUT);
		/* return the number of bytes transferred */
		if (ret == 0) {
			ret = actual_length;
			/* count also the report id */
			if (skipped_report_id)
				ret++;
		}
	} else {
		int skipped_report_id = 0;
		int report_id = buf[0];
		if (buf[0] == 0x0) {
			/* Don't send the Report ID */
			buf++;
			count--;
			skipped_report_id = 1;
		}
		ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			HID_REQ_SET_REPORT,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			((report_type + 1) << 8) | report_id,
			interface->desc.bInterfaceNumber, buf, count,
			USB_CTRL_SET_TIMEOUT);
		/* count also the report id, if this was a numbered report. */
		if (ret > 0 && skipped_report_id)
			ret++;
	}

	return ret;
}

static void usbhid_restart_queues(struct usbhid_device *usbhid)
{
	if (usbhid->urbout)
		usbhid_restart_out_queue(usbhid);
	usbhid_restart_ctrl_queue(usbhid);
}

static void hid_free_buffers(struct usb_device *dev, struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	usb_free_coherent(dev, usbhid->bufsize, usbhid->inbuf, usbhid->inbuf_dma);
	usb_free_coherent(dev, usbhid->bufsize, usbhid->outbuf, usbhid->outbuf_dma);
	kfree(usbhid->cr);
	usb_free_coherent(dev, usbhid->bufsize, usbhid->ctrlbuf, usbhid->ctrlbuf_dma);
}

static int usbhid_parse(struct hid_device *hid)
{
	struct usb_interface *intf = to_usb_interface(hid->dev.parent);
	struct usb_host_interface *interface = intf->cur_altsetting;
	struct usb_device *dev = interface_to_usbdev (intf);
	struct hid_descriptor *hdesc;
	u32 quirks = 0;
	unsigned int rsize = 0;
	char *rdesc;
	int ret, n;

	quirks = usbhid_lookup_quirk(le16_to_cpu(dev->descriptor.idVendor),
			le16_to_cpu(dev->descriptor.idProduct));

	if (quirks & HID_QUIRK_IGNORE)
		return -ENODEV;

	/* Many keyboards and mice don't like to be polled for reports,
	 * so we will always set the HID_QUIRK_NOGET flag for them. */
	if (interface->desc.bInterfaceSubClass == USB_INTERFACE_SUBCLASS_BOOT) {
		if (interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_KEYBOARD ||
			interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_MOUSE)
				quirks |= HID_QUIRK_NOGET;
	}

	if (usb_get_extra_descriptor(interface, HID_DT_HID, &hdesc) &&
	    (!interface->desc.bNumEndpoints ||
	     usb_get_extra_descriptor(&interface->endpoint[0], HID_DT_HID, &hdesc))) {
		dbg_hid("class descriptor not present\n");
		return -ENODEV;
	}

	hid->version = le16_to_cpu(hdesc->bcdHID);
	hid->country = hdesc->bCountryCode;

	for (n = 0; n < hdesc->bNumDescriptors; n++)
		if (hdesc->desc[n].bDescriptorType == HID_DT_REPORT)
			rsize = le16_to_cpu(hdesc->desc[n].wDescriptorLength);

	if (!rsize || rsize > HID_MAX_DESCRIPTOR_SIZE) {
		dbg_hid("weird size of report descriptor (%u)\n", rsize);
		return -EINVAL;
	}

	if (!(rdesc = kmalloc(rsize, GFP_KERNEL))) {
		dbg_hid("couldn't allocate rdesc memory\n");
		return -ENOMEM;
	}

	hid_set_idle(dev, interface->desc.bInterfaceNumber, 0, 0);

	ret = hid_get_class_descriptor(dev, interface->desc.bInterfaceNumber,
			HID_DT_REPORT, rdesc, rsize);
	if (ret < 0) {
		dbg_hid("reading report descriptor failed\n");
		kfree(rdesc);
		goto err;
	}

	ret = hid_parse_report(hid, rdesc, rsize);
	kfree(rdesc);
	if (ret) {
		dbg_hid("parsing report descriptor failed\n");
		goto err;
	}

	hid->quirks |= quirks;

	return 0;
err:
	return ret;
}

static int usbhid_start(struct hid_device *hid)
{
	struct usb_interface *intf = to_usb_interface(hid->dev.parent);
	struct usb_host_interface *interface = intf->cur_altsetting;
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usbhid_device *usbhid = hid->driver_data;
	unsigned int n, insize = 0;
	int ret;

	clear_bit(HID_DISCONNECTED, &usbhid->iofl);

	usbhid->bufsize = HID_MIN_BUFFER_SIZE;
	hid_find_max_report(hid, HID_INPUT_REPORT, &usbhid->bufsize);
	hid_find_max_report(hid, HID_OUTPUT_REPORT, &usbhid->bufsize);
	hid_find_max_report(hid, HID_FEATURE_REPORT, &usbhid->bufsize);

	if (usbhid->bufsize > HID_MAX_BUFFER_SIZE)
		usbhid->bufsize = HID_MAX_BUFFER_SIZE;

	hid_find_max_report(hid, HID_INPUT_REPORT, &insize);

	if (insize > HID_MAX_BUFFER_SIZE)
		insize = HID_MAX_BUFFER_SIZE;

	if (hid_alloc_buffers(dev, hid)) {
		ret = -ENOMEM;
		goto fail;
	}

	for (n = 0; n < interface->desc.bNumEndpoints; n++) {
		struct usb_endpoint_descriptor *endpoint;
		int pipe;
		int interval;

		endpoint = &interface->endpoint[n].desc;
		if (!usb_endpoint_xfer_int(endpoint))
			continue;

		interval = endpoint->bInterval;

		/* Some vendors give fullspeed interval on highspeed devides */
		if (hid->quirks & HID_QUIRK_FULLSPEED_INTERVAL &&
		    dev->speed == USB_SPEED_HIGH) {
			interval = fls(endpoint->bInterval*8);
			printk(KERN_INFO "%s: Fixing fullspeed to highspeed interval: %d -> %d\n",
			       hid->name, endpoint->bInterval, interval);
		}

		/* Change the polling interval of mice. */
		if (hid->collection->usage == HID_GD_MOUSE && hid_mousepoll_interval > 0)
			interval = hid_mousepoll_interval;

		ret = -ENOMEM;
		if (usb_endpoint_dir_in(endpoint)) {
			if (usbhid->urbin)
				continue;
			if (!(usbhid->urbin = usb_alloc_urb(0, GFP_KERNEL)))
				goto fail;
			pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
			usb_fill_int_urb(usbhid->urbin, dev, pipe, usbhid->inbuf, insize,
					 hid_irq_in, hid, interval);
			usbhid->urbin->transfer_dma = usbhid->inbuf_dma;
			usbhid->urbin->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		} else {
			if (usbhid->urbout)
				continue;
			if (!(usbhid->urbout = usb_alloc_urb(0, GFP_KERNEL)))
				goto fail;
			pipe = usb_sndintpipe(dev, endpoint->bEndpointAddress);
			usb_fill_int_urb(usbhid->urbout, dev, pipe, usbhid->outbuf, 0,
					 hid_irq_out, hid, interval);
			usbhid->urbout->transfer_dma = usbhid->outbuf_dma;
			usbhid->urbout->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		}
	}

	usbhid->urbctrl = usb_alloc_urb(0, GFP_KERNEL);
	if (!usbhid->urbctrl) {
		ret = -ENOMEM;
		goto fail;
	}

	usb_fill_control_urb(usbhid->urbctrl, dev, 0, (void *) usbhid->cr,
			     usbhid->ctrlbuf, 1, hid_ctrl, hid);
	usbhid->urbctrl->transfer_dma = usbhid->ctrlbuf_dma;
	usbhid->urbctrl->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	if (!(hid->quirks & HID_QUIRK_NO_INIT_REPORTS))
		usbhid_init_reports(hid);

	set_bit(HID_STARTED, &usbhid->iofl);

	/* Some keyboards don't work until their LEDs have been set.
	 * Since BIOSes do set the LEDs, it must be safe for any device
	 * that supports the keyboard boot protocol.
	 * In addition, enable remote wakeup by default for all keyboard
	 * devices supporting the boot protocol.
	 */
	if (interface->desc.bInterfaceSubClass == USB_INTERFACE_SUBCLASS_BOOT &&
			interface->desc.bInterfaceProtocol ==
				USB_INTERFACE_PROTOCOL_KEYBOARD) {
		usbhid_set_leds(hid);
		device_set_wakeup_enable(&dev->dev, 1);
	}
	return 0;

fail:
	usb_free_urb(usbhid->urbin);
	usb_free_urb(usbhid->urbout);
	usb_free_urb(usbhid->urbctrl);
	usbhid->urbin = NULL;
	usbhid->urbout = NULL;
	usbhid->urbctrl = NULL;
	hid_free_buffers(dev, hid);
	return ret;
}

static void usbhid_stop(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	if (WARN_ON(!usbhid))
		return;

	clear_bit(HID_STARTED, &usbhid->iofl);
	spin_lock_irq(&usbhid->lock);	/* Sync with error handler */
	set_bit(HID_DISCONNECTED, &usbhid->iofl);
	spin_unlock_irq(&usbhid->lock);
	usb_kill_urb(usbhid->urbin);
	usb_kill_urb(usbhid->urbout);
	usb_kill_urb(usbhid->urbctrl);

	hid_cancel_delayed_stuff(usbhid);

	hid->claimed = 0;

	usb_free_urb(usbhid->urbin);
	usb_free_urb(usbhid->urbctrl);
	usb_free_urb(usbhid->urbout);
	usbhid->urbin = NULL; /* don't mess up next start */
	usbhid->urbctrl = NULL;
	usbhid->urbout = NULL;

	hid_free_buffers(hid_to_usb_dev(hid), hid);
}

static int usbhid_power(struct hid_device *hid, int lvl)
{
	int r = 0;

	switch (lvl) {
	case PM_HINT_FULLON:
		r = usbhid_get_power(hid);
		break;
	case PM_HINT_NORMAL:
		usbhid_put_power(hid);
		break;
	}
	return r;
}

static struct hid_ll_driver usb_hid_driver = {
	.parse = usbhid_parse,
	.start = usbhid_start,
	.stop = usbhid_stop,
	.open = usbhid_open,
	.close = usbhid_close,
	.power = usbhid_power,
	.hidinput_input_event = usb_hidinput_input_event,
};

static int usbhid_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_host_interface *interface = intf->cur_altsetting;
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usbhid_device *usbhid;
	struct hid_device *hid;
	unsigned int n, has_in = 0;
	size_t len;
	int ret;

	dbg_hid("HID probe called for ifnum %d\n",
			intf->altsetting->desc.bInterfaceNumber);

	for (n = 0; n < interface->desc.bNumEndpoints; n++)
		if (usb_endpoint_is_int_in(&interface->endpoint[n].desc))
			has_in++;
	if (!has_in) {
		hid_err(intf, "couldn't find an input interrupt endpoint\n");
		return -ENODEV;
	}

	hid = hid_allocate_device();
	if (IS_ERR(hid))
		return PTR_ERR(hid);

	usb_set_intfdata(intf, hid);
	hid->ll_driver = &usb_hid_driver;
	hid->hid_get_raw_report = usbhid_get_raw_report;
	hid->hid_output_raw_report = usbhid_output_raw_report;
	hid->ff_init = hid_pidff_init;
#ifdef CONFIG_USB_HIDDEV
	hid->hiddev_connect = hiddev_connect;
	hid->hiddev_disconnect = hiddev_disconnect;
	hid->hiddev_hid_event = hiddev_hid_event;
	hid->hiddev_report_event = hiddev_report_event;
#endif
	hid->dev.parent = &intf->dev;
	hid->bus = BUS_USB;
	hid->vendor = le16_to_cpu(dev->descriptor.idVendor);
	hid->product = le16_to_cpu(dev->descriptor.idProduct);
	hid->name[0] = 0;
	hid->quirks = usbhid_lookup_quirk(hid->vendor, hid->product);
	if (intf->cur_altsetting->desc.bInterfaceProtocol ==
			USB_INTERFACE_PROTOCOL_MOUSE)
		hid->type = HID_TYPE_USBMOUSE;
	else if (intf->cur_altsetting->desc.bInterfaceProtocol == 0)
		hid->type = HID_TYPE_USBNONE;

	if (dev->manufacturer)
		strlcpy(hid->name, dev->manufacturer, sizeof(hid->name));

	if (dev->product) {
		if (dev->manufacturer)
			strlcat(hid->name, " ", sizeof(hid->name));
		strlcat(hid->name, dev->product, sizeof(hid->name));
	}

	if (!strlen(hid->name))
		snprintf(hid->name, sizeof(hid->name), "HID %04x:%04x",
			 le16_to_cpu(dev->descriptor.idVendor),
			 le16_to_cpu(dev->descriptor.idProduct));

	usb_make_path(dev, hid->phys, sizeof(hid->phys));
	strlcat(hid->phys, "/input", sizeof(hid->phys));
	len = strlen(hid->phys);
	if (len < sizeof(hid->phys) - 1)
		snprintf(hid->phys + len, sizeof(hid->phys) - len,
			 "%d", intf->altsetting[0].desc.bInterfaceNumber);

	if (usb_string(dev, dev->descriptor.iSerialNumber, hid->uniq, 64) <= 0)
		hid->uniq[0] = 0;

	usbhid = kzalloc(sizeof(*usbhid), GFP_KERNEL);
	if (usbhid == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	hid->driver_data = usbhid;
	usbhid->hid = hid;
	usbhid->intf = intf;
	usbhid->ifnum = interface->desc.bInterfaceNumber;

	init_waitqueue_head(&usbhid->wait);
	INIT_WORK(&usbhid->reset_work, hid_reset);
	setup_timer(&usbhid->io_retry, hid_retry_timeout, (unsigned long) hid);
	spin_lock_init(&usbhid->lock);

	ret = hid_add_device(hid);
	if (ret) {
		if (ret != -ENODEV)
			hid_err(intf, "can't add hid device: %d\n", ret);
		goto err_free;
	}

	return 0;
err_free:
	kfree(usbhid);
err:
	hid_destroy_device(hid);
	return ret;
}

static void usbhid_disconnect(struct usb_interface *intf)
{
	struct hid_device *hid = usb_get_intfdata(intf);
	struct usbhid_device *usbhid;

	if (WARN_ON(!hid))
		return;

	usbhid = hid->driver_data;
	hid_destroy_device(hid);
	kfree(usbhid);
}

static void hid_cancel_delayed_stuff(struct usbhid_device *usbhid)
{
	del_timer_sync(&usbhid->io_retry);
	cancel_work_sync(&usbhid->reset_work);
}

static void hid_cease_io(struct usbhid_device *usbhid)
{
	del_timer(&usbhid->io_retry);
	usb_kill_urb(usbhid->urbin);
	usb_kill_urb(usbhid->urbctrl);
	usb_kill_urb(usbhid->urbout);
}

/* Treat USB reset pretty much the same as suspend/resume */
static int hid_pre_reset(struct usb_interface *intf)
{
	struct hid_device *hid = usb_get_intfdata(intf);
	struct usbhid_device *usbhid = hid->driver_data;

	spin_lock_irq(&usbhid->lock);
	set_bit(HID_RESET_PENDING, &usbhid->iofl);
	spin_unlock_irq(&usbhid->lock);
	hid_cease_io(usbhid);

	return 0;
}

/* Same routine used for post_reset and reset_resume */
static int hid_post_reset(struct usb_interface *intf)
{
	struct usb_device *dev = interface_to_usbdev (intf);
	struct hid_device *hid = usb_get_intfdata(intf);
	struct usbhid_device *usbhid = hid->driver_data;
	int status;

	spin_lock_irq(&usbhid->lock);
	clear_bit(HID_RESET_PENDING, &usbhid->iofl);
	spin_unlock_irq(&usbhid->lock);
	hid_set_idle(dev, intf->cur_altsetting->desc.bInterfaceNumber, 0, 0);
	status = hid_start_in(hid);
	if (status < 0)
		hid_io_error(hid);
	usbhid_restart_queues(usbhid);

	return 0;
}

int usbhid_get_power(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	return usb_autopm_get_interface(usbhid->intf);
}

void usbhid_put_power(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	usb_autopm_put_interface(usbhid->intf);
}


#ifdef CONFIG_PM
static int hid_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct hid_device *hid = usb_get_intfdata(intf);
	struct usbhid_device *usbhid = hid->driver_data;
	int status;

	if (message.event & PM_EVENT_AUTO) {
		spin_lock_irq(&usbhid->lock);	/* Sync with error handler */
		if (!test_bit(HID_RESET_PENDING, &usbhid->iofl)
		    && !test_bit(HID_CLEAR_HALT, &usbhid->iofl)
		    && !test_bit(HID_OUT_RUNNING, &usbhid->iofl)
		    && !test_bit(HID_CTRL_RUNNING, &usbhid->iofl)
		    && !test_bit(HID_KEYS_PRESSED, &usbhid->iofl)
		    && (!usbhid->ledcount || ignoreled))
		{
			set_bit(HID_REPORTED_IDLE, &usbhid->iofl);
			spin_unlock_irq(&usbhid->lock);
			if (hid->driver && hid->driver->suspend) {
				status = hid->driver->suspend(hid, message);
				if (status < 0)
					return status;
			}
		} else {
			usbhid_mark_busy(usbhid);
			spin_unlock_irq(&usbhid->lock);
			return -EBUSY;
		}

	} else {
		if (hid->driver && hid->driver->suspend) {
			status = hid->driver->suspend(hid, message);
			if (status < 0)
				return status;
		}
		spin_lock_irq(&usbhid->lock);
		set_bit(HID_REPORTED_IDLE, &usbhid->iofl);
		spin_unlock_irq(&usbhid->lock);
		if (usbhid_wait_io(hid) < 0)
			return -EIO;
	}

	if (!ignoreled && (message.event & PM_EVENT_AUTO)) {
		spin_lock_irq(&usbhid->lock);
		if (test_bit(HID_LED_ON, &usbhid->iofl)) {
			spin_unlock_irq(&usbhid->lock);
			usbhid_mark_busy(usbhid);
			return -EBUSY;
		}
		spin_unlock_irq(&usbhid->lock);
	}

	hid_cancel_delayed_stuff(usbhid);
	hid_cease_io(usbhid);

	if ((message.event & PM_EVENT_AUTO) &&
			test_bit(HID_KEYS_PRESSED, &usbhid->iofl)) {
		/* lost race against keypresses */
		status = hid_start_in(hid);
		if (status < 0)
			hid_io_error(hid);
		usbhid_mark_busy(usbhid);
		return -EBUSY;
	}
	dev_dbg(&intf->dev, "suspend\n");
	return 0;
}

static int hid_resume(struct usb_interface *intf)
{
	struct hid_device *hid = usb_get_intfdata (intf);
	struct usbhid_device *usbhid = hid->driver_data;
	int status;

	if (!test_bit(HID_STARTED, &usbhid->iofl))
		return 0;

	clear_bit(HID_REPORTED_IDLE, &usbhid->iofl);
	usbhid_mark_busy(usbhid);

	if (test_bit(HID_CLEAR_HALT, &usbhid->iofl) ||
	    test_bit(HID_RESET_PENDING, &usbhid->iofl))
		schedule_work(&usbhid->reset_work);
	usbhid->retry_delay = 0;
	status = hid_start_in(hid);
	if (status < 0)
		hid_io_error(hid);
	usbhid_restart_queues(usbhid);

	if (status >= 0 && hid->driver && hid->driver->resume) {
		int ret = hid->driver->resume(hid);
		if (ret < 0)
			status = ret;
	}
	dev_dbg(&intf->dev, "resume status %d\n", status);
	return 0;
}

static int hid_reset_resume(struct usb_interface *intf)
{
	struct hid_device *hid = usb_get_intfdata(intf);
	struct usbhid_device *usbhid = hid->driver_data;
	int status;

	clear_bit(HID_REPORTED_IDLE, &usbhid->iofl);
	status = hid_post_reset(intf);
	if (status >= 0 && hid->driver && hid->driver->reset_resume) {
		int ret = hid->driver->reset_resume(hid);
		if (ret < 0)
			status = ret;
	}
	return status;
}

#endif /* CONFIG_PM */

static const struct usb_device_id hid_usb_ids[] = {
	{ .match_flags = USB_DEVICE_ID_MATCH_INT_CLASS,
		.bInterfaceClass = USB_INTERFACE_CLASS_HID },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, hid_usb_ids);

static struct usb_driver hid_driver = {
	.name =		"usbhid",
	.probe =	usbhid_probe,
	.disconnect =	usbhid_disconnect,
#ifdef CONFIG_PM
	.suspend =	hid_suspend,
	.resume =	hid_resume,
	.reset_resume =	hid_reset_resume,
#endif
	.pre_reset =	hid_pre_reset,
	.post_reset =	hid_post_reset,
	.id_table =	hid_usb_ids,
	.supports_autosuspend = 1,
};

static const struct hid_device_id hid_usb_table[] = {
	{ HID_USB_DEVICE(HID_ANY_ID, HID_ANY_ID) },
	{ }
};

struct usb_interface *usbhid_find_interface(int minor)
{
	return usb_find_interface(&hid_driver, minor);
}

static struct hid_driver hid_usb_driver = {
	.name = "generic-usb",
	.id_table = hid_usb_table,
};

static int __init hid_init(void)
{
	int retval = -ENOMEM;

	retval = hid_register_driver(&hid_usb_driver);
	if (retval)
		goto hid_register_fail;
	retval = usbhid_quirks_init(quirks_param);
	if (retval)
		goto usbhid_quirks_init_fail;
	retval = usb_register(&hid_driver);
	if (retval)
		goto usb_register_fail;
	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_DESC "\n");

	return 0;
usb_register_fail:
	usbhid_quirks_exit();
usbhid_quirks_init_fail:
	hid_unregister_driver(&hid_usb_driver);
hid_register_fail:
	return retval;
}

static void __exit hid_exit(void)
{
	usb_deregister(&hid_driver);
	usbhid_quirks_exit();
	hid_unregister_driver(&hid_usb_driver);
}

module_init(hid_init);
module_exit(hid_exit);

MODULE_AUTHOR("Andreas Gal");
MODULE_AUTHOR("Vojtech Pavlik");
MODULE_AUTHOR("Jiri Kosina");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);
