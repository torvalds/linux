/*
 * c67x00-hcd.c: Cypress C67X00 USB Host Controller Driver
 *
 * Copyright (C) 2006-2008 Barco N.V.
 *    Derived from the Cypress cy7c67200/300 ezusb linux driver and
 *    based on multiple host controller drivers inside the linux kernel.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301  USA.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/usb.h>

#include "c67x00.h"
#include "c67x00-hcd.h"

/* --------------------------------------------------------------------------
 * Root Hub Support
 */

static __u8 c67x00_hub_des[] = {
	0x09,			/*  __u8  bLength; */
	0x29,			/*  __u8  bDescriptorType; Hub-descriptor */
	0x02,			/*  __u8  bNbrPorts; */
	0x00,			/* __u16  wHubCharacteristics; */
	0x00,			/*   (per-port OC, no power switching) */
	0x32,			/*  __u8  bPwrOn2pwrGood; 2ms */
	0x00,			/*  __u8  bHubContrCurrent; 0 mA */
	0x00,			/*  __u8  DeviceRemovable; ** 7 Ports max ** */
	0xff,			/*  __u8  PortPwrCtrlMask; ** 7 ports max ** */
};

static void c67x00_hub_reset_host_port(struct c67x00_sie *sie, int port)
{
	struct c67x00_hcd *c67x00 = sie->private_data;
	unsigned long flags;

	c67x00_ll_husb_reset(sie, port);

	spin_lock_irqsave(&c67x00->lock, flags);
	c67x00_ll_husb_reset_port(sie, port);
	spin_unlock_irqrestore(&c67x00->lock, flags);

	c67x00_ll_set_husb_eot(sie->dev, DEFAULT_EOT);
}

static int c67x00_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct c67x00_hcd *c67x00 = hcd_to_c67x00_hcd(hcd);
	struct c67x00_sie *sie = c67x00->sie;
	u16 status;
	int i;

	*buf = 0;
	status = c67x00_ll_usb_get_status(sie);
	for (i = 0; i < C67X00_PORTS; i++)
		if (status & PORT_CONNECT_CHANGE(i))
			*buf |= (1 << i);

	/* bit 0 denotes hub change, b1..n port change */
	*buf <<= 1;

	return !!*buf;
}

static int c67x00_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
			      u16 wIndex, char *buf, u16 wLength)
{
	struct c67x00_hcd *c67x00 = hcd_to_c67x00_hcd(hcd);
	struct c67x00_sie *sie = c67x00->sie;
	u16 status, usb_status;
	int len = 0;
	unsigned int port = wIndex-1;
	u16 wPortChange, wPortStatus;

	switch (typeReq) {

	case GetHubStatus:
		*(__le32 *) buf = cpu_to_le32(0);
		len = 4;		/* hub power */
		break;

	case GetPortStatus:
		if (wIndex > C67X00_PORTS)
			return -EPIPE;

		status = c67x00_ll_usb_get_status(sie);
		usb_status = c67x00_ll_get_usb_ctl(sie);

		wPortChange = 0;
		if (status & PORT_CONNECT_CHANGE(port))
			wPortChange |= USB_PORT_STAT_C_CONNECTION;

		wPortStatus = USB_PORT_STAT_POWER;
		if (!(status & PORT_SE0_STATUS(port)))
			wPortStatus |= USB_PORT_STAT_CONNECTION;
		if (usb_status & LOW_SPEED_PORT(port)) {
			wPortStatus |= USB_PORT_STAT_LOW_SPEED;
			c67x00->low_speed_ports |= (1 << port);
		} else
			c67x00->low_speed_ports &= ~(1 << port);

		if (usb_status & SOF_EOP_EN(port))
			wPortStatus |= USB_PORT_STAT_ENABLE;

		*(__le16 *) buf = cpu_to_le16(wPortStatus);
		*(__le16 *) (buf + 2) = cpu_to_le16(wPortChange);
		len = 4;
		break;

	case SetHubFeature:	/* We don't implement these */
	case ClearHubFeature:
		switch (wValue) {
		case C_HUB_OVER_CURRENT:
		case C_HUB_LOCAL_POWER:
			len = 0;
			break;

		default:
			return -EPIPE;
		}
		break;

	case SetPortFeature:
		if (wIndex > C67X00_PORTS)
			return -EPIPE;

		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			dev_dbg(c67x00_hcd_dev(c67x00),
				"SetPortFeature %d (SUSPEND)\n", port);
			len = 0;
			break;

		case USB_PORT_FEAT_RESET:
			c67x00_hub_reset_host_port(sie, port);
			len = 0;
			break;

		case USB_PORT_FEAT_POWER:
			/* Power always enabled */
			len = 0;
			break;

		default:
			dev_dbg(c67x00_hcd_dev(c67x00),
				"%s: SetPortFeature %d (0x%04x) Error!\n",
				__func__, port, wValue);
			return -EPIPE;
		}
		break;

	case ClearPortFeature:
		if (wIndex > C67X00_PORTS)
			return -EPIPE;

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			/* Reset the port so that the c67x00 also notices the
			 * disconnect */
			c67x00_hub_reset_host_port(sie, port);
			len = 0;
			break;

		case USB_PORT_FEAT_C_ENABLE:
			dev_dbg(c67x00_hcd_dev(c67x00),
				"ClearPortFeature (%d): C_ENABLE\n", port);
			len = 0;
			break;

		case USB_PORT_FEAT_SUSPEND:
			dev_dbg(c67x00_hcd_dev(c67x00),
				"ClearPortFeature (%d): SUSPEND\n", port);
			len = 0;
			break;

		case USB_PORT_FEAT_C_SUSPEND:
			dev_dbg(c67x00_hcd_dev(c67x00),
				"ClearPortFeature (%d): C_SUSPEND\n", port);
			len = 0;
			break;

		case USB_PORT_FEAT_POWER:
			dev_dbg(c67x00_hcd_dev(c67x00),
				"ClearPortFeature (%d): POWER\n", port);
			return -EPIPE;

		case USB_PORT_FEAT_C_CONNECTION:
			c67x00_ll_usb_clear_status(sie,
						   PORT_CONNECT_CHANGE(port));
			len = 0;
			break;

		case USB_PORT_FEAT_C_OVER_CURRENT:
			dev_dbg(c67x00_hcd_dev(c67x00),
				"ClearPortFeature (%d): OVER_CURRENT\n", port);
			len = 0;
			break;

		case USB_PORT_FEAT_C_RESET:
			dev_dbg(c67x00_hcd_dev(c67x00),
				"ClearPortFeature (%d): C_RESET\n", port);
			len = 0;
			break;

		default:
			dev_dbg(c67x00_hcd_dev(c67x00),
				"%s: ClearPortFeature %d (0x%04x) Error!\n",
				__func__, port, wValue);
			return -EPIPE;
		}
		break;

	case GetHubDescriptor:
		len = min_t(unsigned int, sizeof(c67x00_hub_des), wLength);
		memcpy(buf, c67x00_hub_des, len);
		break;

	default:
		dev_dbg(c67x00_hcd_dev(c67x00), "%s: unknown\n", __func__);
		return -EPIPE;
	}

	return 0;
}

/* ---------------------------------------------------------------------
 * Main part of host controller driver
 */

/**
 * c67x00_hcd_irq
 *
 * This function is called from the interrupt handler in c67x00-drv.c
 */
static void c67x00_hcd_irq(struct c67x00_sie *sie, u16 int_status, u16 msg)
{
	struct c67x00_hcd *c67x00 = sie->private_data;
	struct usb_hcd *hcd = c67x00_hcd_to_hcd(c67x00);

	/* Handle sie message flags */
	if (msg) {
		if (msg & HUSB_TDListDone)
			c67x00_sched_kick(c67x00);
		else
			dev_warn(c67x00_hcd_dev(c67x00),
				 "Unknown SIE msg flag(s): 0x%04x\n", msg);
	}

	if (unlikely(hcd->state == HC_STATE_HALT))
		return;

	if (!HCD_HW_ACCESSIBLE(hcd))
		return;

	/* Handle Start of frame events */
	if (int_status & SOFEOP_FLG(sie->sie_num)) {
		c67x00_ll_usb_clear_status(sie, SOF_EOP_IRQ_FLG);
		c67x00_sched_kick(c67x00);
	}
}

/**
 * c67x00_hcd_start: Host controller start hook
 */
static int c67x00_hcd_start(struct usb_hcd *hcd)
{
	hcd->uses_new_polling = 1;
	hcd->state = HC_STATE_RUNNING;
	set_bit(HCD_FLAG_POLL_RH, &hcd->flags);

	return 0;
}

/**
 * c67x00_hcd_stop: Host controller stop hook
 */
static void c67x00_hcd_stop(struct usb_hcd *hcd)
{
	/* Nothing to do */
}

static int c67x00_hcd_get_frame(struct usb_hcd *hcd)
{
	struct c67x00_hcd *c67x00 = hcd_to_c67x00_hcd(hcd);
	u16 temp_val;

	dev_dbg(c67x00_hcd_dev(c67x00), "%s\n", __func__);
	temp_val = c67x00_ll_husb_get_frame(c67x00->sie);
	temp_val &= HOST_FRAME_MASK;
	return temp_val ? (temp_val - 1) : HOST_FRAME_MASK;
}

static struct hc_driver c67x00_hc_driver = {
	.description	= "c67x00-hcd",
	.product_desc	= "Cypress C67X00 Host Controller",
	.hcd_priv_size	= sizeof(struct c67x00_hcd),
	.flags		= HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start		= c67x00_hcd_start,
	.stop		= c67x00_hcd_stop,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue	= c67x00_urb_enqueue,
	.urb_dequeue	= c67x00_urb_dequeue,
	.endpoint_disable = c67x00_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number = c67x00_hcd_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = c67x00_hub_status_data,
	.hub_control	= c67x00_hub_control,
};

/* ---------------------------------------------------------------------
 * Setup/Teardown routines
 */

int c67x00_hcd_probe(struct c67x00_sie *sie)
{
	struct c67x00_hcd *c67x00;
	struct usb_hcd *hcd;
	unsigned long flags;
	int retval;

	if (usb_disabled())
		return -ENODEV;

	hcd = usb_create_hcd(&c67x00_hc_driver, sie_dev(sie), "c67x00_sie");
	if (!hcd) {
		retval = -ENOMEM;
		goto err0;
	}
	c67x00 = hcd_to_c67x00_hcd(hcd);

	spin_lock_init(&c67x00->lock);
	c67x00->sie = sie;

	INIT_LIST_HEAD(&c67x00->list[PIPE_ISOCHRONOUS]);
	INIT_LIST_HEAD(&c67x00->list[PIPE_INTERRUPT]);
	INIT_LIST_HEAD(&c67x00->list[PIPE_CONTROL]);
	INIT_LIST_HEAD(&c67x00->list[PIPE_BULK]);
	c67x00->urb_count = 0;
	INIT_LIST_HEAD(&c67x00->td_list);
	c67x00->td_base_addr = CY_HCD_BUF_ADDR + SIE_TD_OFFSET(sie->sie_num);
	c67x00->buf_base_addr = CY_HCD_BUF_ADDR + SIE_BUF_OFFSET(sie->sie_num);
	c67x00->max_frame_bw = MAX_FRAME_BW_STD;

	c67x00_ll_husb_init_host_port(sie);

	init_completion(&c67x00->endpoint_disable);
	retval = c67x00_sched_start_scheduler(c67x00);
	if (retval)
		goto err1;

	retval = usb_add_hcd(hcd, 0, 0);
	if (retval) {
		dev_dbg(sie_dev(sie), "%s: usb_add_hcd returned %d\n",
			__func__, retval);
		goto err2;
	}

	device_wakeup_enable(hcd->self.controller);

	spin_lock_irqsave(&sie->lock, flags);
	sie->private_data = c67x00;
	sie->irq = c67x00_hcd_irq;
	spin_unlock_irqrestore(&sie->lock, flags);

	return retval;

 err2:
	c67x00_sched_stop_scheduler(c67x00);
 err1:
	usb_put_hcd(hcd);
 err0:
	return retval;
}

/* may be called with controller, bus, and devices active */
void c67x00_hcd_remove(struct c67x00_sie *sie)
{
	struct c67x00_hcd *c67x00 = sie->private_data;
	struct usb_hcd *hcd = c67x00_hcd_to_hcd(c67x00);

	c67x00_sched_stop_scheduler(c67x00);
	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);
}
