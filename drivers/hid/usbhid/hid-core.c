/*
 *  USB HID support for Linux
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
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
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <asm/unaligned.h>
#include <asm/byteorder.h>
#include <linux/input.h>
#include <linux/wait.h>

#include <linux/usb.h>

#include <linux/hid.h>
#include <linux/hiddev.h>
#include <linux/hid-debug.h>
#include <linux/hidraw.h>
#include "usbhid.h"

/*
 * Version Information
 */

#define DRIVER_VERSION "v2.6"
#define DRIVER_AUTHOR "Andreas Gal, Vojtech Pavlik, Jiri Kosina"
#define DRIVER_DESC "USB HID core driver"
#define DRIVER_LICENSE "GPL"

static char *hid_types[] = {"Device", "Pointer", "Mouse", "Device", "Joystick",
				"Gamepad", "Keyboard", "Keypad", "Multi-Axis Controller"};
/*
 * Module parameters.
 */

static unsigned int hid_mousepoll_interval;
module_param_named(mousepoll, hid_mousepoll_interval, uint, 0644);
MODULE_PARM_DESC(mousepoll, "Polling interval of mice");

/* Quirks specified at module load time */
static char *quirks_param[MAX_USBHID_BOOT_QUIRKS] = { [ 0 ... (MAX_USBHID_BOOT_QUIRKS - 1) ] = NULL };
module_param_array_named(quirks, quirks_param, charp, NULL, 0444);
MODULE_PARM_DESC(quirks, "Add/modify USB HID quirks by specifying "
		" quirks=vendorID:productID:quirks"
		" where vendorID, productID, and quirks are all in"
		" 0x-prefixed hex");
static char *rdesc_quirks_param[MAX_USBHID_BOOT_QUIRKS] = { [ 0 ... (MAX_USBHID_BOOT_QUIRKS - 1) ] = NULL };
module_param_array_named(rdesc_quirks, rdesc_quirks_param, charp, NULL, 0444);
MODULE_PARM_DESC(rdesc_quirks, "Add/modify report descriptor quirks by specifying "
		" rdesc_quirks=vendorID:productID:rdesc_quirks"
		" where vendorID, productID, and rdesc_quirks are all in"
		" 0x-prefixed hex");
/*
 * Input submission and I/O error handler.
 */

static void hid_io_error(struct hid_device *hid);

/* Start up the input URB */
static int hid_start_in(struct hid_device *hid)
{
	unsigned long flags;
	int rc = 0;
	struct usbhid_device *usbhid = hid->driver_data;

	spin_lock_irqsave(&usbhid->inlock, flags);
	if (hid->open > 0 && !test_bit(HID_SUSPENDED, &usbhid->iofl) &&
			!test_bit(HID_DISCONNECTED, &usbhid->iofl) &&
			!test_and_set_bit(HID_IN_RUNNING, &usbhid->iofl)) {
		rc = usb_submit_urb(usbhid->urbin, GFP_ATOMIC);
		if (rc != 0)
			clear_bit(HID_IN_RUNNING, &usbhid->iofl);
	}
	spin_unlock_irqrestore(&usbhid->inlock, flags);
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
	int rc_lock, rc = 0;

	if (test_bit(HID_CLEAR_HALT, &usbhid->iofl)) {
		dev_dbg(&usbhid->intf->dev, "clear halt\n");
		rc = usb_clear_halt(hid_to_usb_dev(hid), usbhid->urbin->pipe);
		clear_bit(HID_CLEAR_HALT, &usbhid->iofl);
		hid_start_in(hid);
	}

	else if (test_bit(HID_RESET_PENDING, &usbhid->iofl)) {
		dev_dbg(&usbhid->intf->dev, "resetting device\n");
		rc = rc_lock = usb_lock_device_for_reset(hid_to_usb_dev(hid), usbhid->intf);
		if (rc_lock >= 0) {
			rc = usb_reset_composite_device(hid_to_usb_dev(hid), usbhid->intf);
			if (rc_lock)
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
		err_hid("can't reset device, %s-%s/input%d, status %d",
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

	spin_lock_irqsave(&usbhid->inlock, flags);

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
	spin_unlock_irqrestore(&usbhid->inlock, flags);
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
			usbhid->retry_delay = 0;
			hid_input_report(urb->context, HID_INPUT_REPORT,
					 urb->transfer_buffer,
					 urb->actual_length, 1);
			break;
		case -EPIPE:		/* stall */
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
			clear_bit(HID_IN_RUNNING, &usbhid->iofl);
			hid_io_error(hid);
			return;
		default:		/* error */
			warn("input irq status %d received", urb->status);
	}

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		clear_bit(HID_IN_RUNNING, &usbhid->iofl);
		if (status != -EPERM) {
			err_hid("can't resubmit intr, %s-%s/input%d, status %d",
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
	struct usbhid_device *usbhid = hid->driver_data;

	report = usbhid->out[usbhid->outtail];

	hid_output_report(report, usbhid->outbuf);
	usbhid->urbout->transfer_buffer_length = ((report->size - 1) >> 3) + 1 + (report->id > 0);
	usbhid->urbout->dev = hid_to_usb_dev(hid);

	dbg_hid("submitting out urb\n");

	if (usb_submit_urb(usbhid->urbout, GFP_ATOMIC)) {
		err_hid("usb_submit_urb(out) failed");
		return -1;
	}

	return 0;
}

static int hid_submit_ctrl(struct hid_device *hid)
{
	struct hid_report *report;
	unsigned char dir;
	int len;
	struct usbhid_device *usbhid = hid->driver_data;

	report = usbhid->ctrl[usbhid->ctrltail].report;
	dir = usbhid->ctrl[usbhid->ctrltail].dir;

	len = ((report->size - 1) >> 3) + 1 + (report->id > 0);
	if (dir == USB_DIR_OUT) {
		hid_output_report(report, usbhid->ctrlbuf);
		usbhid->urbctrl->pipe = usb_sndctrlpipe(hid_to_usb_dev(hid), 0);
		usbhid->urbctrl->transfer_buffer_length = len;
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
		err_hid("usb_submit_urb(ctrl) failed");
		return -1;
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
			warn("output irq status %d received", urb->status);
	}

	spin_lock_irqsave(&usbhid->outlock, flags);

	if (unplug)
		usbhid->outtail = usbhid->outhead;
	else
		usbhid->outtail = (usbhid->outtail + 1) & (HID_OUTPUT_FIFO_SIZE - 1);

	if (usbhid->outhead != usbhid->outtail) {
		if (hid_submit_out(hid)) {
			clear_bit(HID_OUT_RUNNING, &usbhid->iofl);
			wake_up(&usbhid->wait);
		}
		spin_unlock_irqrestore(&usbhid->outlock, flags);
		return;
	}

	clear_bit(HID_OUT_RUNNING, &usbhid->iofl);
	spin_unlock_irqrestore(&usbhid->outlock, flags);
	wake_up(&usbhid->wait);
}

/*
 * Control pipe completion handler.
 */

static void hid_ctrl(struct urb *urb)
{
	struct hid_device *hid = urb->context;
	struct usbhid_device *usbhid = hid->driver_data;
	unsigned long flags;
	int unplug = 0;

	spin_lock_irqsave(&usbhid->ctrllock, flags);

	switch (urb->status) {
		case 0:			/* success */
			if (usbhid->ctrl[usbhid->ctrltail].dir == USB_DIR_IN)
				hid_input_report(urb->context, usbhid->ctrl[usbhid->ctrltail].report->type,
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
			warn("ctrl urb status %d received", urb->status);
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
		spin_unlock_irqrestore(&usbhid->ctrllock, flags);
		return;
	}

	clear_bit(HID_CTRL_RUNNING, &usbhid->iofl);
	spin_unlock_irqrestore(&usbhid->ctrllock, flags);
	wake_up(&usbhid->wait);
}

void usbhid_submit_report(struct hid_device *hid, struct hid_report *report, unsigned char dir)
{
	int head;
	unsigned long flags;
	struct usbhid_device *usbhid = hid->driver_data;

	if ((hid->quirks & HID_QUIRK_NOGET) && dir == USB_DIR_IN)
		return;

	if (usbhid->urbout && dir == USB_DIR_OUT && report->type == HID_OUTPUT_REPORT) {

		spin_lock_irqsave(&usbhid->outlock, flags);

		if ((head = (usbhid->outhead + 1) & (HID_OUTPUT_FIFO_SIZE - 1)) == usbhid->outtail) {
			spin_unlock_irqrestore(&usbhid->outlock, flags);
			warn("output queue full");
			return;
		}

		usbhid->out[usbhid->outhead] = report;
		usbhid->outhead = head;

		if (!test_and_set_bit(HID_OUT_RUNNING, &usbhid->iofl))
			if (hid_submit_out(hid))
				clear_bit(HID_OUT_RUNNING, &usbhid->iofl);

		spin_unlock_irqrestore(&usbhid->outlock, flags);
		return;
	}

	spin_lock_irqsave(&usbhid->ctrllock, flags);

	if ((head = (usbhid->ctrlhead + 1) & (HID_CONTROL_FIFO_SIZE - 1)) == usbhid->ctrltail) {
		spin_unlock_irqrestore(&usbhid->ctrllock, flags);
		warn("control queue full");
		return;
	}

	usbhid->ctrl[usbhid->ctrlhead].report = report;
	usbhid->ctrl[usbhid->ctrlhead].dir = dir;
	usbhid->ctrlhead = head;

	if (!test_and_set_bit(HID_CTRL_RUNNING, &usbhid->iofl))
		if (hid_submit_ctrl(hid))
			clear_bit(HID_CTRL_RUNNING, &usbhid->iofl);

	spin_unlock_irqrestore(&usbhid->ctrllock, flags);
}

static int usb_hidinput_input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct hid_field *field;
	int offset;

	if (type == EV_FF)
		return input_ff_event(dev, type, code, value);

	if (type != EV_LED)
		return -1;

	if ((offset = hidinput_find_field(hid, type, code, &field)) == -1) {
		warn("event field not found");
		return -1;
	}

	hid_set_field(field, offset, value);
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

	if (!hid->open++) {
		res = usb_autopm_get_interface(usbhid->intf);
		if (res < 0) {
			hid->open--;
			return -EIO;
		}
	}
	if (hid_start_in(hid))
		hid_io_error(hid);
	return 0;
}

void usbhid_close(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	if (!--hid->open) {
		usb_kill_urb(usbhid->urbin);
		usb_autopm_put_interface(usbhid->intf);
	}
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
		warn("timeout initializing reports");
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

static void usbhid_set_leds(struct hid_device *hid)
{
	struct hid_field *field;
	int offset;

	if ((offset = hid_find_field_early(hid, HID_UP_LED, 0x01, &field)) != -1) {
		hid_set_field(field, offset, 0);
		usbhid_submit_report(hid, field->report, USB_DIR_OUT);
	}
}

/*
 * Traverse the supplied list of reports and find the longest
 */
static void hid_find_max_report(struct hid_device *hid, unsigned int type,
		unsigned int *max)
{
	struct hid_report *report;
	unsigned int size;

	list_for_each_entry(report, &hid->report_enum[type].report_list, list) {
		size = ((report->size - 1) >> 3) + 1;
		if (type == HID_INPUT_REPORT && hid->report_enum[type].numbered)
			size++;
		if (*max < size)
			*max = size;
	}
}

static int hid_alloc_buffers(struct usb_device *dev, struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	if (!(usbhid->inbuf = usb_buffer_alloc(dev, usbhid->bufsize, GFP_ATOMIC, &usbhid->inbuf_dma)))
		return -1;
	if (!(usbhid->outbuf = usb_buffer_alloc(dev, usbhid->bufsize, GFP_ATOMIC, &usbhid->outbuf_dma)))
		return -1;
	if (!(usbhid->cr = usb_buffer_alloc(dev, sizeof(*(usbhid->cr)), GFP_ATOMIC, &usbhid->cr_dma)))
		return -1;
	if (!(usbhid->ctrlbuf = usb_buffer_alloc(dev, usbhid->bufsize, GFP_ATOMIC, &usbhid->ctrlbuf_dma)))
		return -1;

	return 0;
}

static int usbhid_output_raw_report(struct hid_device *hid, __u8 *buf, size_t count)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usb_device *dev = hid_to_usb_dev(hid);
	struct usb_interface *intf = usbhid->intf;
	struct usb_host_interface *interface = intf->cur_altsetting;
	int ret;

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		HID_REQ_SET_REPORT,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		cpu_to_le16(((HID_OUTPUT_REPORT + 1) << 8) | *buf),
		interface->desc.bInterfaceNumber, buf + 1, count - 1,
		USB_CTRL_SET_TIMEOUT);

	/* count also the report id */
	if (ret > 0)
		ret++;

	return ret;
}

static void hid_free_buffers(struct usb_device *dev, struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	usb_buffer_free(dev, usbhid->bufsize, usbhid->inbuf, usbhid->inbuf_dma);
	usb_buffer_free(dev, usbhid->bufsize, usbhid->outbuf, usbhid->outbuf_dma);
	usb_buffer_free(dev, sizeof(*(usbhid->cr)), usbhid->cr, usbhid->cr_dma);
	usb_buffer_free(dev, usbhid->bufsize, usbhid->ctrlbuf, usbhid->ctrlbuf_dma);
}

/*
 * Sending HID_REQ_GET_REPORT changes the operation mode of the ps3 controller
 * to "operational".  Without this, the ps3 controller will not report any
 * events.
 */
static void hid_fixup_sony_ps3_controller(struct usb_device *dev, int ifnum)
{
	int result;
	char *buf = kmalloc(18, GFP_KERNEL);

	if (!buf)
		return;

	result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				 HID_REQ_GET_REPORT,
				 USB_DIR_IN | USB_TYPE_CLASS |
				 USB_RECIP_INTERFACE,
				 (3 << 8) | 0xf2, ifnum, buf, 17,
				 USB_CTRL_GET_TIMEOUT);

	if (result < 0)
		err_hid("%s failed: %d\n", __func__, result);

	kfree(buf);
}

static struct hid_device *usb_hid_configure(struct usb_interface *intf)
{
	struct usb_host_interface *interface = intf->cur_altsetting;
	struct usb_device *dev = interface_to_usbdev (intf);
	struct hid_descriptor *hdesc;
	struct hid_device *hid;
	u32 quirks = 0;
	unsigned int insize = 0, rsize = 0;
	char *rdesc;
	int n, len;
	struct usbhid_device *usbhid;

	quirks = usbhid_lookup_quirk(le16_to_cpu(dev->descriptor.idVendor),
			le16_to_cpu(dev->descriptor.idProduct));

	/* Many keyboards and mice don't like to be polled for reports,
	 * so we will always set the HID_QUIRK_NOGET flag for them. */
	if (interface->desc.bInterfaceSubClass == USB_INTERFACE_SUBCLASS_BOOT) {
		if (interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_KEYBOARD ||
			interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_MOUSE)
				quirks |= HID_QUIRK_NOGET;
	}

	if (quirks & HID_QUIRK_IGNORE)
		return NULL;

	if ((quirks & HID_QUIRK_IGNORE_MOUSE) &&
		(interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_MOUSE))
			return NULL;


	if (usb_get_extra_descriptor(interface, HID_DT_HID, &hdesc) &&
	    (!interface->desc.bNumEndpoints ||
	     usb_get_extra_descriptor(&interface->endpoint[0], HID_DT_HID, &hdesc))) {
		dbg_hid("class descriptor not present\n");
		return NULL;
	}

	for (n = 0; n < hdesc->bNumDescriptors; n++)
		if (hdesc->desc[n].bDescriptorType == HID_DT_REPORT)
			rsize = le16_to_cpu(hdesc->desc[n].wDescriptorLength);

	if (!rsize || rsize > HID_MAX_DESCRIPTOR_SIZE) {
		dbg_hid("weird size of report descriptor (%u)\n", rsize);
		return NULL;
	}

	if (!(rdesc = kmalloc(rsize, GFP_KERNEL))) {
		dbg_hid("couldn't allocate rdesc memory\n");
		return NULL;
	}

	hid_set_idle(dev, interface->desc.bInterfaceNumber, 0, 0);

	if ((n = hid_get_class_descriptor(dev, interface->desc.bInterfaceNumber, HID_DT_REPORT, rdesc, rsize)) < 0) {
		dbg_hid("reading report descriptor failed\n");
		kfree(rdesc);
		return NULL;
	}

	usbhid_fixup_report_descriptor(le16_to_cpu(dev->descriptor.idVendor),
			le16_to_cpu(dev->descriptor.idProduct), rdesc,
			rsize, rdesc_quirks_param);

	dbg_hid("report descriptor (size %u, read %d) = ", rsize, n);
	for (n = 0; n < rsize; n++)
		dbg_hid_line(" %02x", (unsigned char) rdesc[n]);
	dbg_hid_line("\n");

	if (!(hid = hid_parse_report(rdesc, n))) {
		dbg_hid("parsing report descriptor failed\n");
		kfree(rdesc);
		return NULL;
	}

	kfree(rdesc);
	hid->quirks = quirks;

	if (!(usbhid = kzalloc(sizeof(struct usbhid_device), GFP_KERNEL)))
		goto fail_no_usbhid;

	hid->driver_data = usbhid;
	usbhid->hid = hid;

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
		hid_free_buffers(dev, hid);
		goto fail;
	}

	hid->name[0] = 0;

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

	for (n = 0; n < interface->desc.bNumEndpoints; n++) {

		struct usb_endpoint_descriptor *endpoint;
		int pipe;
		int interval;

		endpoint = &interface->endpoint[n].desc;
		if ((endpoint->bmAttributes & 3) != 3)		/* Not an interrupt endpoint */
			continue;

		interval = endpoint->bInterval;

		/* Some vendors give fullspeed interval on highspeed devides */
		if (quirks & HID_QUIRK_FULLSPEED_INTERVAL  &&
		    dev->speed == USB_SPEED_HIGH) {
			interval = fls(endpoint->bInterval*8);
			printk(KERN_INFO "%s: Fixing fullspeed to highspeed interval: %d -> %d\n",
			       hid->name, endpoint->bInterval, interval);
		}

		/* Change the polling interval of mice. */
		if (hid->collection->usage == HID_GD_MOUSE && hid_mousepoll_interval > 0)
			interval = hid_mousepoll_interval;

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

	if (!usbhid->urbin) {
		err_hid("couldn't find an input interrupt endpoint");
		goto fail;
	}

	init_waitqueue_head(&usbhid->wait);
	INIT_WORK(&usbhid->reset_work, hid_reset);
	setup_timer(&usbhid->io_retry, hid_retry_timeout, (unsigned long) hid);

	spin_lock_init(&usbhid->inlock);
	spin_lock_init(&usbhid->outlock);
	spin_lock_init(&usbhid->ctrllock);

	hid->version = le16_to_cpu(hdesc->bcdHID);
	hid->country = hdesc->bCountryCode;
	hid->dev = &intf->dev;
	usbhid->intf = intf;
	usbhid->ifnum = interface->desc.bInterfaceNumber;

	hid->bus = BUS_USB;
	hid->vendor = le16_to_cpu(dev->descriptor.idVendor);
	hid->product = le16_to_cpu(dev->descriptor.idProduct);

	usb_make_path(dev, hid->phys, sizeof(hid->phys));
	strlcat(hid->phys, "/input", sizeof(hid->phys));
	len = strlen(hid->phys);
	if (len < sizeof(hid->phys) - 1)
		snprintf(hid->phys + len, sizeof(hid->phys) - len,
			 "%d", intf->altsetting[0].desc.bInterfaceNumber);

	if (usb_string(dev, dev->descriptor.iSerialNumber, hid->uniq, 64) <= 0)
		hid->uniq[0] = 0;

	usbhid->urbctrl = usb_alloc_urb(0, GFP_KERNEL);
	if (!usbhid->urbctrl)
		goto fail;

	usb_fill_control_urb(usbhid->urbctrl, dev, 0, (void *) usbhid->cr,
			     usbhid->ctrlbuf, 1, hid_ctrl, hid);
	usbhid->urbctrl->setup_dma = usbhid->cr_dma;
	usbhid->urbctrl->transfer_dma = usbhid->ctrlbuf_dma;
	usbhid->urbctrl->transfer_flags |= (URB_NO_TRANSFER_DMA_MAP | URB_NO_SETUP_DMA_MAP);
	hid->hidinput_input_event = usb_hidinput_input_event;
	hid->hid_open = usbhid_open;
	hid->hid_close = usbhid_close;
#ifdef CONFIG_USB_HIDDEV
	hid->hiddev_hid_event = hiddev_hid_event;
	hid->hiddev_report_event = hiddev_report_event;
#endif
	hid->hid_output_raw_report = usbhid_output_raw_report;
	return hid;

fail:
	usb_free_urb(usbhid->urbin);
	usb_free_urb(usbhid->urbout);
	usb_free_urb(usbhid->urbctrl);
	hid_free_buffers(dev, hid);
	kfree(usbhid);
fail_no_usbhid:
	hid_free_device(hid);

	return NULL;
}

static void hid_disconnect(struct usb_interface *intf)
{
	struct hid_device *hid = usb_get_intfdata (intf);
	struct usbhid_device *usbhid;

	if (!hid)
		return;

	usbhid = hid->driver_data;

	spin_lock_irq(&usbhid->inlock);	/* Sync with error handler */
	usb_set_intfdata(intf, NULL);
	set_bit(HID_DISCONNECTED, &usbhid->iofl);
	spin_unlock_irq(&usbhid->inlock);
	usb_kill_urb(usbhid->urbin);
	usb_kill_urb(usbhid->urbout);
	usb_kill_urb(usbhid->urbctrl);

	del_timer_sync(&usbhid->io_retry);
	cancel_work_sync(&usbhid->reset_work);

	if (hid->claimed & HID_CLAIMED_INPUT)
		hidinput_disconnect(hid);
	if (hid->claimed & HID_CLAIMED_HIDDEV)
		hiddev_disconnect(hid);
	if (hid->claimed & HID_CLAIMED_HIDRAW)
		hidraw_disconnect(hid);

	usb_free_urb(usbhid->urbin);
	usb_free_urb(usbhid->urbctrl);
	usb_free_urb(usbhid->urbout);

	hid_free_buffers(hid_to_usb_dev(hid), hid);
	kfree(usbhid);
	hid_free_device(hid);
}

static int hid_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct hid_device *hid;
	char path[64];
	int i;
	char *c;

	dbg_hid("HID probe called for ifnum %d\n",
			intf->altsetting->desc.bInterfaceNumber);

	if (!(hid = usb_hid_configure(intf)))
		return -ENODEV;

	usbhid_init_reports(hid);
	hid_dump_device(hid);
	if (hid->quirks & HID_QUIRK_RESET_LEDS)
		usbhid_set_leds(hid);

	if (!hidinput_connect(hid))
		hid->claimed |= HID_CLAIMED_INPUT;
	if (!hiddev_connect(hid))
		hid->claimed |= HID_CLAIMED_HIDDEV;
	if (!hidraw_connect(hid))
		hid->claimed |= HID_CLAIMED_HIDRAW;

	usb_set_intfdata(intf, hid);

	if (!hid->claimed) {
		printk ("HID device claimed by neither input, hiddev nor hidraw\n");
		hid_disconnect(intf);
		return -ENODEV;
	}

	if ((hid->claimed & HID_CLAIMED_INPUT))
		hid_ff_init(hid);

	if (hid->quirks & HID_QUIRK_SONY_PS3_CONTROLLER)
		hid_fixup_sony_ps3_controller(interface_to_usbdev(intf),
			intf->cur_altsetting->desc.bInterfaceNumber);

	printk(KERN_INFO);

	if (hid->claimed & HID_CLAIMED_INPUT)
		printk("input");
	if ((hid->claimed & HID_CLAIMED_INPUT) && ((hid->claimed & HID_CLAIMED_HIDDEV) ||
				hid->claimed & HID_CLAIMED_HIDRAW))
		printk(",");
	if (hid->claimed & HID_CLAIMED_HIDDEV)
		printk("hiddev%d", hid->minor);
	if ((hid->claimed & HID_CLAIMED_INPUT) && (hid->claimed & HID_CLAIMED_HIDDEV) &&
			(hid->claimed & HID_CLAIMED_HIDRAW))
		printk(",");
	if (hid->claimed & HID_CLAIMED_HIDRAW)
		printk("hidraw%d", ((struct hidraw*)hid->hidraw)->minor);

	c = "Device";
	for (i = 0; i < hid->maxcollection; i++) {
		if (hid->collection[i].type == HID_COLLECTION_APPLICATION &&
		    (hid->collection[i].usage & HID_USAGE_PAGE) == HID_UP_GENDESK &&
		    (hid->collection[i].usage & 0xffff) < ARRAY_SIZE(hid_types)) {
			c = hid_types[hid->collection[i].usage & 0xffff];
			break;
		}
	}

	usb_make_path(interface_to_usbdev(intf), path, 63);

	printk(": USB HID v%x.%02x %s [%s] on %s\n",
		hid->version >> 8, hid->version & 0xff, c, hid->name, path);

	return 0;
}

static int hid_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct hid_device *hid = usb_get_intfdata (intf);
	struct usbhid_device *usbhid = hid->driver_data;

	spin_lock_irq(&usbhid->inlock);	/* Sync with error handler */
	set_bit(HID_SUSPENDED, &usbhid->iofl);
	spin_unlock_irq(&usbhid->inlock);
	del_timer(&usbhid->io_retry);
	usb_kill_urb(usbhid->urbin);
	dev_dbg(&intf->dev, "suspend\n");
	return 0;
}

static int hid_resume(struct usb_interface *intf)
{
	struct hid_device *hid = usb_get_intfdata (intf);
	struct usbhid_device *usbhid = hid->driver_data;
	int status;

	clear_bit(HID_SUSPENDED, &usbhid->iofl);
	usbhid->retry_delay = 0;
	status = hid_start_in(hid);
	dev_dbg(&intf->dev, "resume status %d\n", status);
	return status;
}

/* Treat USB reset pretty much the same as suspend/resume */
static int hid_pre_reset(struct usb_interface *intf)
{
	/* FIXME: What if the interface is already suspended? */
	hid_suspend(intf, PMSG_ON);
	return 0;
}

/* Same routine used for post_reset and reset_resume */
static int hid_post_reset(struct usb_interface *intf)
{
	struct usb_device *dev = interface_to_usbdev (intf);

	hid_set_idle(dev, intf->cur_altsetting->desc.bInterfaceNumber, 0, 0);
	/* FIXME: Any more reinitialization needed? */

	return hid_resume(intf);
}

static struct usb_device_id hid_usb_ids [] = {
	{ .match_flags = USB_DEVICE_ID_MATCH_INT_CLASS,
		.bInterfaceClass = USB_INTERFACE_CLASS_HID },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, hid_usb_ids);

static struct usb_driver hid_driver = {
	.name =		"usbhid",
	.probe =	hid_probe,
	.disconnect =	hid_disconnect,
	.suspend =	hid_suspend,
	.resume =	hid_resume,
	.reset_resume =	hid_post_reset,
	.pre_reset =	hid_pre_reset,
	.post_reset =	hid_post_reset,
	.id_table =	hid_usb_ids,
	.supports_autosuspend = 1,
};

static int __init hid_init(void)
{
	int retval;
	retval = usbhid_quirks_init(quirks_param);
	if (retval)
		goto usbhid_quirks_init_fail;
	retval = hiddev_init();
	if (retval)
		goto hiddev_init_fail;
	retval = usb_register(&hid_driver);
	if (retval)
		goto usb_register_fail;
	info(DRIVER_VERSION ":" DRIVER_DESC);

	return 0;
usb_register_fail:
	hiddev_exit();
hiddev_init_fail:
	usbhid_quirks_exit();
usbhid_quirks_init_fail:
	return retval;
}

static void __exit hid_exit(void)
{
	usb_deregister(&hid_driver);
	hiddev_exit();
	usbhid_quirks_exit();
}

module_init(hid_init);
module_exit(hid_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);
