/*
 * Wireless USB Host Controller
 * Root Hub operations
 *
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
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
 *
 * We fake a root hub that has fake ports (as many as simultaneous
 * devices the Wireless USB Host Controller can deal with). For each
 * port we keep an state in @wusbhc->port[index] identical to the one
 * specified in the USB2.0[ch11] spec and some extra device
 * information that complements the one in 'struct usb_device' (as
 * this lacs a hcpriv pointer).
 *
 * Note this is common to WHCI and HWA host controllers.
 *
 * Through here we enable most of the state changes that the USB stack
 * will use to connect or disconnect devices. We need to do some
 * forced adaptation of Wireless USB device states vs. wired:
 *
 *        USB:                 WUSB:
 *
 * Port   Powered-off          port slot n/a
 *        Powered-on           port slot available
 *        Disconnected         port slot available
 *        Connected            port slot assigned device
 *        		       device sent DN_Connect
 *                             device was authenticated
 *        Enabled              device is authenticated, transitioned
 *                             from unauth -> auth -> default address
 *                             -> enabled
 *        Reset                disconnect
 *        Disable              disconnect
 *
 * This maps the standard USB port states with the WUSB device states
 * so we can fake ports without having to modify the USB stack.
 *
 * FIXME: this process will change in the future
 *
 *
 * ENTRY POINTS
 *
 * Our entry points into here are, as in hcd.c, the USB stack root hub
 * ops defined in the usb_hcd struct:
 *
 * wusbhc_rh_status_data()	Provide hub and port status data bitmap
 *
 * wusbhc_rh_control()          Execution of all the major requests
 *                              you can do to a hub (Set|Clear
 *                              features, get descriptors, status, etc).
 *
 * wusbhc_rh_[suspend|resume]() That
 *
 * wusbhc_rh_start_port_reset() ??? unimplemented
 */
#include <linux/slab.h>
#include <linux/export.h>
#include "wusbhc.h"

/*
 * Reset a fake port
 *
 * Using a Reset Device IE is too heavyweight as it causes the device
 * to enter the UnConnected state and leave the cluster, this can mean
 * that when the device reconnects it is connected to a different fake
 * port.
 *
 * Instead, reset authenticated devices with a SetAddress(0), followed
 * by a SetAddresss(AuthAddr).
 *
 * For unauthenticated devices just pretend to reset but do nothing.
 * If the device initialization continues to fail it will eventually
 * time out after TrustTimeout and enter the UnConnected state.
 *
 * @wusbhc is assumed referenced and @wusbhc->mutex unlocked.
 *
 * Supposedly we are the only thread accesing @wusbhc->port; in any
 * case, maybe we should move the mutex locking from
 * wusbhc_devconnect_auth() to here.
 *
 * @port_idx refers to the wusbhc's port index, not the USB port number
 */
static int wusbhc_rh_port_reset(struct wusbhc *wusbhc, u8 port_idx)
{
	int result = 0;
	struct wusb_port *port = wusb_port_by_idx(wusbhc, port_idx);
	struct wusb_dev *wusb_dev = port->wusb_dev;

	if (wusb_dev == NULL)
		return -ENOTCONN;

	port->status |= USB_PORT_STAT_RESET;
	port->change |= USB_PORT_STAT_C_RESET;

	if (wusb_dev->addr & WUSB_DEV_ADDR_UNAUTH)
		result = 0;
	else
		result = wusb_dev_update_address(wusbhc, wusb_dev);

	port->status &= ~USB_PORT_STAT_RESET;
	port->status |= USB_PORT_STAT_ENABLE;
	port->change |= USB_PORT_STAT_C_RESET | USB_PORT_STAT_C_ENABLE;	

	return result;
}

/*
 * Return the hub change status bitmap
 *
 * The bits in the change status bitmap are cleared when a
 * ClearPortFeature request is issued (USB2.0[11.12.3,11.12.4].
 *
 * @wusbhc is assumed referenced and @wusbhc->mutex unlocked.
 *
 * WARNING!! This gets called from atomic context; we cannot get the
 *           mutex--the only race condition we can find is some bit
 *           changing just after we copy it, which shouldn't be too
 *           big of a problem [and we can't make it an spinlock
 *           because other parts need to take it and sleep] .
 *
 *           @usb_hcd is refcounted, so it won't disappear under us
 *           and before killing a host, the polling of the root hub
 *           would be stopped anyway.
 */
int wusbhc_rh_status_data(struct usb_hcd *usb_hcd, char *_buf)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	size_t cnt, size, bits_set = 0;

	/* WE DON'T LOCK, see comment */
	/* round up to bytes.  Hub bit is bit 0 so add 1. */
	size = DIV_ROUND_UP(wusbhc->ports_max + 1, 8);

	/* clear the output buffer. */
	memset(_buf, 0, size);
	/* set the bit for each changed port. */
	for (cnt = 0; cnt < wusbhc->ports_max; cnt++) {

		if (wusb_port_by_idx(wusbhc, cnt)->change) {
			const int bitpos = cnt+1;

			_buf[bitpos/8] |= (1 << (bitpos % 8));
			bits_set++;
		}
	}

	return bits_set ? size : 0;
}
EXPORT_SYMBOL_GPL(wusbhc_rh_status_data);

/*
 * Return the hub's descriptor
 *
 * NOTE: almost cut and paste from ehci-hub.c
 *
 * @wusbhc is assumed referenced and @wusbhc->mutex unlocked
 */
static int wusbhc_rh_get_hub_descr(struct wusbhc *wusbhc, u16 wValue,
				   u16 wIndex,
				   struct usb_hub_descriptor *descr,
				   u16 wLength)
{
	u16 temp = 1 + (wusbhc->ports_max / 8);
	u8 length = 7 + 2 * temp;

	if (wLength < length)
		return -ENOSPC;
	descr->bDescLength = 7 + 2 * temp;
	descr->bDescriptorType = USB_DT_HUB; /* HUB type */
	descr->bNbrPorts = wusbhc->ports_max;
	descr->wHubCharacteristics = cpu_to_le16(
		HUB_CHAR_COMMON_LPSM	/* All ports power at once */
		| 0x00			/* not part of compound device */
		| HUB_CHAR_NO_OCPM	/* No overcurrent protection */
		| 0x00			/* 8 FS think time FIXME ?? */
		| 0x00);		/* No port indicators */
	descr->bPwrOn2PwrGood = 0;
	descr->bHubContrCurrent = 0;
	/* two bitmaps:  ports removable, and usb 1.0 legacy PortPwrCtrlMask */
	memset(&descr->u.hs.DeviceRemovable[0], 0, temp);
	memset(&descr->u.hs.DeviceRemovable[temp], 0xff, temp);
	return 0;
}

/*
 * Clear a hub feature
 *
 * @wusbhc is assumed referenced and @wusbhc->mutex unlocked.
 *
 * Nothing to do, so no locking needed ;)
 */
static int wusbhc_rh_clear_hub_feat(struct wusbhc *wusbhc, u16 feature)
{
	int result;

	switch (feature) {
	case C_HUB_LOCAL_POWER:
		/* FIXME: maybe plug bit 0 to the power input status,
		 * if any?
		 * see wusbhc_rh_get_hub_status() */
	case C_HUB_OVER_CURRENT:
		result = 0;
		break;
	default:
		result = -EPIPE;
	}
	return result;
}

/*
 * Return hub status (it is always zero...)
 *
 * @wusbhc is assumed referenced and @wusbhc->mutex unlocked.
 *
 * Nothing to do, so no locking needed ;)
 */
static int wusbhc_rh_get_hub_status(struct wusbhc *wusbhc, u32 *buf,
				    u16 wLength)
{
	/* FIXME: maybe plug bit 0 to the power input status (if any)? */
	*buf = 0;
	return 0;
}

/*
 * Set a port feature
 *
 * @wusbhc is assumed referenced and @wusbhc->mutex unlocked.
 */
static int wusbhc_rh_set_port_feat(struct wusbhc *wusbhc, u16 feature,
				   u8 selector, u8 port_idx)
{
	struct device *dev = wusbhc->dev;

	if (port_idx > wusbhc->ports_max)
		return -EINVAL;

	switch (feature) {
		/* According to USB2.0[11.24.2.13]p2, these features
		 * are not required to be implemented. */
	case USB_PORT_FEAT_C_OVER_CURRENT:
	case USB_PORT_FEAT_C_ENABLE:
	case USB_PORT_FEAT_C_SUSPEND:
	case USB_PORT_FEAT_C_CONNECTION:
	case USB_PORT_FEAT_C_RESET:
		return 0;
	case USB_PORT_FEAT_POWER:
		/* No such thing, but we fake it works */
		mutex_lock(&wusbhc->mutex);
		wusb_port_by_idx(wusbhc, port_idx)->status |= USB_PORT_STAT_POWER;
		mutex_unlock(&wusbhc->mutex);
		return 0;
	case USB_PORT_FEAT_RESET:
		return wusbhc_rh_port_reset(wusbhc, port_idx);
	case USB_PORT_FEAT_ENABLE:
	case USB_PORT_FEAT_SUSPEND:
		dev_err(dev, "(port_idx %d) set feat %d/%d UNIMPLEMENTED\n",
			port_idx, feature, selector);
		return -ENOSYS;
	default:
		dev_err(dev, "(port_idx %d) set feat %d/%d UNKNOWN\n",
			port_idx, feature, selector);
		return -EPIPE;
	}

	return 0;
}

/*
 * Clear a port feature...
 *
 * @wusbhc is assumed referenced and @wusbhc->mutex unlocked.
 */
static int wusbhc_rh_clear_port_feat(struct wusbhc *wusbhc, u16 feature,
				     u8 selector, u8 port_idx)
{
	int result = 0;
	struct device *dev = wusbhc->dev;

	if (port_idx > wusbhc->ports_max)
		return -EINVAL;

	mutex_lock(&wusbhc->mutex);
	switch (feature) {
	case USB_PORT_FEAT_POWER:	/* fake port always on */
		/* According to USB2.0[11.24.2.7.1.4], no need to implement? */
	case USB_PORT_FEAT_C_OVER_CURRENT:
		break;
	case USB_PORT_FEAT_C_RESET:
		wusb_port_by_idx(wusbhc, port_idx)->change &= ~USB_PORT_STAT_C_RESET;
		break;
	case USB_PORT_FEAT_C_CONNECTION:
		wusb_port_by_idx(wusbhc, port_idx)->change &= ~USB_PORT_STAT_C_CONNECTION;
		break;
	case USB_PORT_FEAT_ENABLE:
		__wusbhc_dev_disable(wusbhc, port_idx);
		break;
	case USB_PORT_FEAT_C_ENABLE:
		wusb_port_by_idx(wusbhc, port_idx)->change &= ~USB_PORT_STAT_C_ENABLE;
		break;
	case USB_PORT_FEAT_SUSPEND:
	case USB_PORT_FEAT_C_SUSPEND:
		dev_err(dev, "(port_idx %d) Clear feat %d/%d UNIMPLEMENTED\n",
			port_idx, feature, selector);
		result = -ENOSYS;
		break;
	default:
		dev_err(dev, "(port_idx %d) Clear feat %d/%d UNKNOWN\n",
			port_idx, feature, selector);
		result = -EPIPE;
		break;
	}
	mutex_unlock(&wusbhc->mutex);

	return result;
}

/*
 * Return the port's status
 *
 * @wusbhc is assumed referenced and @wusbhc->mutex unlocked.
 */
static int wusbhc_rh_get_port_status(struct wusbhc *wusbhc, u16 port_idx,
				     u32 *_buf, u16 wLength)
{
	__le16 *buf = (__le16 *)_buf;

	if (port_idx > wusbhc->ports_max)
		return -EINVAL;

	mutex_lock(&wusbhc->mutex);
	buf[0] = cpu_to_le16(wusb_port_by_idx(wusbhc, port_idx)->status);
	buf[1] = cpu_to_le16(wusb_port_by_idx(wusbhc, port_idx)->change);
	mutex_unlock(&wusbhc->mutex);

	return 0;
}

/*
 * Entry point for Root Hub operations
 *
 * @wusbhc is assumed referenced and @wusbhc->mutex unlocked.
 */
int wusbhc_rh_control(struct usb_hcd *usb_hcd, u16 reqntype, u16 wValue,
		      u16 wIndex, char *buf, u16 wLength)
{
	int result = -ENOSYS;
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);

	switch (reqntype) {
	case GetHubDescriptor:
		result = wusbhc_rh_get_hub_descr(
			wusbhc, wValue, wIndex,
			(struct usb_hub_descriptor *) buf, wLength);
		break;
	case ClearHubFeature:
		result = wusbhc_rh_clear_hub_feat(wusbhc, wValue);
		break;
	case GetHubStatus:
		result = wusbhc_rh_get_hub_status(wusbhc, (u32 *)buf, wLength);
		break;

	case SetPortFeature:
		result = wusbhc_rh_set_port_feat(wusbhc, wValue, wIndex >> 8,
						 (wIndex & 0xff) - 1);
		break;
	case ClearPortFeature:
		result = wusbhc_rh_clear_port_feat(wusbhc, wValue, wIndex >> 8,
						   (wIndex & 0xff) - 1);
		break;
	case GetPortStatus:
		result = wusbhc_rh_get_port_status(wusbhc, wIndex - 1,
						   (u32 *)buf, wLength);
		break;

	case SetHubFeature:
	default:
		dev_err(wusbhc->dev, "%s (%p [%p], %x, %x, %x, %p, %x) "
			"UNIMPLEMENTED\n", __func__, usb_hcd, wusbhc, reqntype,
			wValue, wIndex, buf, wLength);
		/* dump_stack(); */
		result = -ENOSYS;
	}
	return result;
}
EXPORT_SYMBOL_GPL(wusbhc_rh_control);

int wusbhc_rh_start_port_reset(struct usb_hcd *usb_hcd, unsigned port_idx)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	dev_err(wusbhc->dev, "%s (%p [%p], port_idx %u) UNIMPLEMENTED\n",
		__func__, usb_hcd, wusbhc, port_idx);
	WARN_ON(1);
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(wusbhc_rh_start_port_reset);

static void wusb_port_init(struct wusb_port *port)
{
	port->status |= USB_PORT_STAT_HIGH_SPEED;
}

/*
 * Alloc fake port specific fields and status.
 */
int wusbhc_rh_create(struct wusbhc *wusbhc)
{
	int result = -ENOMEM;
	size_t port_size, itr;
	port_size = wusbhc->ports_max * sizeof(wusbhc->port[0]);
	wusbhc->port = kzalloc(port_size, GFP_KERNEL);
	if (wusbhc->port == NULL)
		goto error_port_alloc;
	for (itr = 0; itr < wusbhc->ports_max; itr++)
		wusb_port_init(&wusbhc->port[itr]);
	result = 0;
error_port_alloc:
	return result;
}

void wusbhc_rh_destroy(struct wusbhc *wusbhc)
{
	kfree(wusbhc->port);
}
