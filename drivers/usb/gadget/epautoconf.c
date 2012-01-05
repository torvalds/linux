/*
 * epautoconf.c -- endpoint autoconfiguration for usb gadget drivers
 *
 * Copyright (C) 2004 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>

#include <linux/ctype.h>
#include <linux/string.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include "gadget_chips.h"


/* we must assign addresses for configurable endpoints (like net2280) */
static unsigned epnum;

// #define MANY_ENDPOINTS
#ifdef MANY_ENDPOINTS
/* more than 15 configurable endpoints */
static unsigned in_epnum;
#endif


/*
 * This should work with endpoints from controller drivers sharing the
 * same endpoint naming convention.  By example:
 *
 *	- ep1, ep2, ... address is fixed, not direction or type
 *	- ep1in, ep2out, ... address and direction are fixed, not type
 *	- ep1-bulk, ep2-bulk, ... address and type are fixed, not direction
 *	- ep1in-bulk, ep2out-iso, ... all three are fixed
 *	- ep-* ... no functionality restrictions
 *
 * Type suffixes are "-bulk", "-iso", or "-int".  Numbers are decimal.
 * Less common restrictions are implied by gadget_is_*().
 *
 * NOTE:  each endpoint is unidirectional, as specified by its USB
 * descriptor; and isn't specific to a configuration or altsetting.
 */
static int
ep_matches (
	struct usb_gadget		*gadget,
	struct usb_ep			*ep,
	struct usb_endpoint_descriptor	*desc,
	struct usb_ss_ep_comp_descriptor *ep_comp
)
{
	u8		type;
	const char	*tmp;
	u16		max;

	int		num_req_streams = 0;

	/* endpoint already claimed? */
	if (NULL != ep->driver_data)
		return 0;

	/* only support ep0 for portable CONTROL traffic */
	type = desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
	if (USB_ENDPOINT_XFER_CONTROL == type)
		return 0;

	/* some other naming convention */
	if ('e' != ep->name[0])
		return 0;

	/* type-restriction:  "-iso", "-bulk", or "-int".
	 * direction-restriction:  "in", "out".
	 */
	if ('-' != ep->name[2]) {
		tmp = strrchr (ep->name, '-');
		if (tmp) {
			switch (type) {
			case USB_ENDPOINT_XFER_INT:
				/* bulk endpoints handle interrupt transfers,
				 * except the toggle-quirky iso-synch kind
				 */
				if ('s' == tmp[2])	// == "-iso"
					return 0;
				/* for now, avoid PXA "interrupt-in";
				 * it's documented as never using DATA1.
				 */
				if (gadget_is_pxa (gadget)
						&& 'i' == tmp [1])
					return 0;
				break;
			case USB_ENDPOINT_XFER_BULK:
				if ('b' != tmp[1])	// != "-bulk"
					return 0;
				break;
			case USB_ENDPOINT_XFER_ISOC:
				if ('s' != tmp[2])	// != "-iso"
					return 0;
			}
		} else {
			tmp = ep->name + strlen (ep->name);
		}

		/* direction-restriction:  "..in-..", "out-.." */
		tmp--;
		if (!isdigit (*tmp)) {
			if (desc->bEndpointAddress & USB_DIR_IN) {
				if ('n' != *tmp)
					return 0;
			} else {
				if ('t' != *tmp)
					return 0;
			}
		}
	}

	/*
	 * Get the number of required streams from the EP companion
	 * descriptor and see if the EP matches it
	 */
	if (usb_endpoint_xfer_bulk(desc)) {
		if (ep_comp) {
			num_req_streams = ep_comp->bmAttributes & 0x1f;
			if (num_req_streams > ep->max_streams)
				return 0;
		}

	}

	/*
	 * If the protocol driver hasn't yet decided on wMaxPacketSize
	 * and wants to know the maximum possible, provide the info.
	 */
	if (desc->wMaxPacketSize == 0)
		desc->wMaxPacketSize = cpu_to_le16(ep->maxpacket);

	/* endpoint maxpacket size is an input parameter, except for bulk
	 * where it's an output parameter representing the full speed limit.
	 * the usb spec fixes high speed bulk maxpacket at 512 bytes.
	 */
	max = 0x7ff & usb_endpoint_maxp(desc);
	switch (type) {
	case USB_ENDPOINT_XFER_INT:
		/* INT:  limit 64 bytes full speed, 1024 high/super speed */
		if (!gadget->is_dualspeed && max > 64)
			return 0;
		/* FALLTHROUGH */

	case USB_ENDPOINT_XFER_ISOC:
		/* ISO:  limit 1023 bytes full speed, 1024 high/super speed */
		if (ep->maxpacket < max)
			return 0;
		if (!gadget->is_dualspeed && max > 1023)
			return 0;

		/* BOTH:  "high bandwidth" works only at high speed */
		if ((desc->wMaxPacketSize & cpu_to_le16(3<<11))) {
			if (!gadget->is_dualspeed)
				return 0;
			/* configure your hardware with enough buffering!! */
		}
		break;
	}

	/* MATCH!! */

	/* report address */
	desc->bEndpointAddress &= USB_DIR_IN;
	if (isdigit (ep->name [2])) {
		u8	num = simple_strtoul (&ep->name [2], NULL, 10);
		desc->bEndpointAddress |= num;
#ifdef	MANY_ENDPOINTS
	} else if (desc->bEndpointAddress & USB_DIR_IN) {
		if (++in_epnum > 15)
			return 0;
		desc->bEndpointAddress = USB_DIR_IN | in_epnum;
#endif
	} else {
		if (++epnum > 15)
			return 0;
		desc->bEndpointAddress |= epnum;
	}

	/* report (variable) full speed bulk maxpacket */
	if ((USB_ENDPOINT_XFER_BULK == type) && !ep_comp) {
		int size = ep->maxpacket;

		/* min() doesn't work on bitfields with gcc-3.5 */
		if (size > 64)
			size = 64;
		desc->wMaxPacketSize = cpu_to_le16(size);
	}
	ep->address = desc->bEndpointAddress;
	return 1;
}

static struct usb_ep *
find_ep (struct usb_gadget *gadget, const char *name)
{
	struct usb_ep	*ep;

	list_for_each_entry (ep, &gadget->ep_list, ep_list) {
		if (0 == strcmp (ep->name, name))
			return ep;
	}
	return NULL;
}

/**
 * usb_ep_autoconfig_ss() - choose an endpoint matching the ep
 * descriptor and ep companion descriptor
 * @gadget: The device to which the endpoint must belong.
 * @desc: Endpoint descriptor, with endpoint direction and transfer mode
 *    initialized.  For periodic transfers, the maximum packet
 *    size must also be initialized.  This is modified on
 *    success.
 * @ep_comp: Endpoint companion descriptor, with the required
 *    number of streams. Will be modified when the chosen EP
 *    supports a different number of streams.
 *
 * This routine replaces the usb_ep_autoconfig when needed
 * superspeed enhancments. If such enhancemnets are required,
 * the FD should call usb_ep_autoconfig_ss directly and provide
 * the additional ep_comp parameter.
 *
 * By choosing an endpoint to use with the specified descriptor,
 * this routine simplifies writing gadget drivers that work with
 * multiple USB device controllers.  The endpoint would be
 * passed later to usb_ep_enable(), along with some descriptor.
 *
 * That second descriptor won't always be the same as the first one.
 * For example, isochronous endpoints can be autoconfigured for high
 * bandwidth, and then used in several lower bandwidth altsettings.
 * Also, high and full speed descriptors will be different.
 *
 * Be sure to examine and test the results of autoconfiguration
 * on your hardware.  This code may not make the best choices
 * about how to use the USB controller, and it can't know all
 * the restrictions that may apply. Some combinations of driver
 * and hardware won't be able to autoconfigure.
 *
 * On success, this returns an un-claimed usb_ep, and modifies the endpoint
 * descriptor bEndpointAddress.  For bulk endpoints, the wMaxPacket value
 * is initialized as if the endpoint were used at full speed and
 * the bmAttribute field in the ep companion descriptor is
 * updated with the assigned number of streams if it is
 * different from the original value. To prevent the endpoint
 * from being returned by a later autoconfig call, claim it by
 * assigning ep->driver_data to some non-null value.
 *
 * On failure, this returns a null endpoint descriptor.
 */
struct usb_ep *usb_ep_autoconfig_ss(
	struct usb_gadget		*gadget,
	struct usb_endpoint_descriptor	*desc,
	struct usb_ss_ep_comp_descriptor *ep_comp
)
{
	struct usb_ep	*ep;
	u8		type;

	type = desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;

	/* First, apply chip-specific "best usage" knowledge.
	 * This might make a good usb_gadget_ops hook ...
	 */
	if (gadget_is_net2280 (gadget) && type == USB_ENDPOINT_XFER_INT) {
		/* ep-e, ep-f are PIO with only 64 byte fifos */
		ep = find_ep (gadget, "ep-e");
		if (ep && ep_matches(gadget, ep, desc, ep_comp))
			return ep;
		ep = find_ep (gadget, "ep-f");
		if (ep && ep_matches(gadget, ep, desc, ep_comp))
			return ep;

	} else if (gadget_is_goku (gadget)) {
		if (USB_ENDPOINT_XFER_INT == type) {
			/* single buffering is enough */
			ep = find_ep(gadget, "ep3-bulk");
			if (ep && ep_matches(gadget, ep, desc, ep_comp))
				return ep;
		} else if (USB_ENDPOINT_XFER_BULK == type
				&& (USB_DIR_IN & desc->bEndpointAddress)) {
			/* DMA may be available */
			ep = find_ep(gadget, "ep2-bulk");
			if (ep && ep_matches(gadget, ep, desc,
					      ep_comp))
				return ep;
		}

#ifdef CONFIG_BLACKFIN
	} else if (gadget_is_musbhdrc(gadget)) {
		if ((USB_ENDPOINT_XFER_BULK == type) ||
		    (USB_ENDPOINT_XFER_ISOC == type)) {
			if (USB_DIR_IN & desc->bEndpointAddress)
				ep = find_ep (gadget, "ep5in");
			else
				ep = find_ep (gadget, "ep6out");
		} else if (USB_ENDPOINT_XFER_INT == type) {
			if (USB_DIR_IN & desc->bEndpointAddress)
				ep = find_ep(gadget, "ep1in");
			else
				ep = find_ep(gadget, "ep2out");
		} else
			ep = NULL;
		if (ep && ep_matches(gadget, ep, desc, ep_comp))
			return ep;
#endif
	}

	/* Second, look at endpoints until an unclaimed one looks usable */
	list_for_each_entry (ep, &gadget->ep_list, ep_list) {
		if (ep_matches(gadget, ep, desc, ep_comp))
			return ep;
	}

	/* Fail */
	return NULL;
}

/**
 * usb_ep_autoconfig() - choose an endpoint matching the
 * descriptor
 * @gadget: The device to which the endpoint must belong.
 * @desc: Endpoint descriptor, with endpoint direction and transfer mode
 *	initialized.  For periodic transfers, the maximum packet
 *	size must also be initialized.  This is modified on success.
 *
 * By choosing an endpoint to use with the specified descriptor, this
 * routine simplifies writing gadget drivers that work with multiple
 * USB device controllers.  The endpoint would be passed later to
 * usb_ep_enable(), along with some descriptor.
 *
 * That second descriptor won't always be the same as the first one.
 * For example, isochronous endpoints can be autoconfigured for high
 * bandwidth, and then used in several lower bandwidth altsettings.
 * Also, high and full speed descriptors will be different.
 *
 * Be sure to examine and test the results of autoconfiguration on your
 * hardware.  This code may not make the best choices about how to use the
 * USB controller, and it can't know all the restrictions that may apply.
 * Some combinations of driver and hardware won't be able to autoconfigure.
 *
 * On success, this returns an un-claimed usb_ep, and modifies the endpoint
 * descriptor bEndpointAddress.  For bulk endpoints, the wMaxPacket value
 * is initialized as if the endpoint were used at full speed.  To prevent
 * the endpoint from being returned by a later autoconfig call, claim it
 * by assigning ep->driver_data to some non-null value.
 *
 * On failure, this returns a null endpoint descriptor.
 */
struct usb_ep *usb_ep_autoconfig(
	struct usb_gadget		*gadget,
	struct usb_endpoint_descriptor	*desc
)
{
	return usb_ep_autoconfig_ss(gadget, desc, NULL);
}


/**
 * usb_ep_autoconfig_reset - reset endpoint autoconfig state
 * @gadget: device for which autoconfig state will be reset
 *
 * Use this for devices where one configuration may need to assign
 * endpoint resources very differently from the next one.  It clears
 * state such as ep->driver_data and the record of assigned endpoints
 * used by usb_ep_autoconfig().
 */
void usb_ep_autoconfig_reset (struct usb_gadget *gadget)
{
	struct usb_ep	*ep;

	list_for_each_entry (ep, &gadget->ep_list, ep_list) {
		ep->driver_data = NULL;
	}
#ifdef	MANY_ENDPOINTS
	in_epnum = 0;
#endif
	epnum = 0;
}

