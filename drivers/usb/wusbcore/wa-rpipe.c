/*
 * WUSB Wire Adapter
 * rpipe management
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
 * FIXME: docs
 *
 * RPIPE
 *
 *   Targetted at different downstream endpoints
 *
 *   Descriptor: use to config the remote pipe.
 *
 *   The number of blocks could be dynamic (wBlocks in descriptor is
 *   0)--need to schedule them then.
 *
 * Each bit in wa->rpipe_bm represents if an rpipe is being used or
 * not. Rpipes are represented with a 'struct wa_rpipe' that is
 * attached to the hcpriv member of a 'struct usb_host_endpoint'.
 *
 * When you need to xfer data to an endpoint, you get an rpipe for it
 * with wa_ep_rpipe_get(), which gives you a reference to the rpipe
 * and keeps a single one (the first one) with the endpoint. When you
 * are done transferring, you drop that reference. At the end the
 * rpipe is always allocated and bound to the endpoint. There it might
 * be recycled when not used.
 *
 * Addresses:
 *
 *  We use a 1:1 mapping mechanism between port address (0 based
 *  index, actually) and the address. The USB stack knows about this.
 *
 *  USB Stack port number    4 (1 based)
 *  WUSB code port index     3 (0 based)
 *  USB Addresss             5 (2 based -- 0 is for default, 1 for root hub)
 *
 *  Now, because we don't use the concept as default address exactly
 *  like the (wired) USB code does, we need to kind of skip it. So we
 *  never take addresses from the urb->pipe, but from the
 *  urb->dev->devnum, to make sure that we always have the right
 *  destination address.
 */
#include <linux/init.h>
#include <asm/atomic.h>
#include <linux/bitmap.h>

#include "wusbhc.h"
#include "wa-hc.h"

static int __rpipe_get_descr(struct wahc *wa,
			     struct usb_rpipe_descriptor *descr, u16 index)
{
	ssize_t result;
	struct device *dev = &wa->usb_iface->dev;

	/* Get the RPIPE descriptor -- we cannot use the usb_get_descriptor()
	 * function because the arguments are different.
	 */
	result = usb_control_msg(
		wa->usb_dev, usb_rcvctrlpipe(wa->usb_dev, 0),
		USB_REQ_GET_DESCRIPTOR,
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_RPIPE,
		USB_DT_RPIPE<<8, index, descr, sizeof(*descr),
		1000 /* FIXME: arbitrary */);
	if (result < 0) {
		dev_err(dev, "rpipe %u: get descriptor failed: %d\n",
			index, (int)result);
		goto error;
	}
	if (result < sizeof(*descr)) {
		dev_err(dev, "rpipe %u: got short descriptor "
			"(%zd vs %zd bytes needed)\n",
			index, result, sizeof(*descr));
		result = -EINVAL;
		goto error;
	}
	result = 0;

error:
	return result;
}

/*
 *
 * The descriptor is assumed to be properly initialized (ie: you got
 * it through __rpipe_get_descr()).
 */
static int __rpipe_set_descr(struct wahc *wa,
			     struct usb_rpipe_descriptor *descr, u16 index)
{
	ssize_t result;
	struct device *dev = &wa->usb_iface->dev;

	/* we cannot use the usb_get_descriptor() function because the
	 * arguments are different.
	 */
	result = usb_control_msg(
		wa->usb_dev, usb_sndctrlpipe(wa->usb_dev, 0),
		USB_REQ_SET_DESCRIPTOR,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_RPIPE,
		USB_DT_RPIPE<<8, index, descr, sizeof(*descr),
		HZ / 10);
	if (result < 0) {
		dev_err(dev, "rpipe %u: set descriptor failed: %d\n",
			index, (int)result);
		goto error;
	}
	if (result < sizeof(*descr)) {
		dev_err(dev, "rpipe %u: sent short descriptor "
			"(%zd vs %zd bytes required)\n",
			index, result, sizeof(*descr));
		result = -EINVAL;
		goto error;
	}
	result = 0;

error:
	return result;

}

static void rpipe_init(struct wa_rpipe *rpipe)
{
	kref_init(&rpipe->refcnt);
	spin_lock_init(&rpipe->seg_lock);
	INIT_LIST_HEAD(&rpipe->seg_list);
}

static unsigned rpipe_get_idx(struct wahc *wa, unsigned rpipe_idx)
{
	unsigned long flags;

	spin_lock_irqsave(&wa->rpipe_bm_lock, flags);
	rpipe_idx = find_next_zero_bit(wa->rpipe_bm, wa->rpipes, rpipe_idx);
	if (rpipe_idx < wa->rpipes)
		set_bit(rpipe_idx, wa->rpipe_bm);
	spin_unlock_irqrestore(&wa->rpipe_bm_lock, flags);

	return rpipe_idx;
}

static void rpipe_put_idx(struct wahc *wa, unsigned rpipe_idx)
{
	unsigned long flags;

	spin_lock_irqsave(&wa->rpipe_bm_lock, flags);
	clear_bit(rpipe_idx, wa->rpipe_bm);
	spin_unlock_irqrestore(&wa->rpipe_bm_lock, flags);
}

void rpipe_destroy(struct kref *_rpipe)
{
	struct wa_rpipe *rpipe = container_of(_rpipe, struct wa_rpipe, refcnt);
	u8 index = le16_to_cpu(rpipe->descr.wRPipeIndex);

	if (rpipe->ep)
		rpipe->ep->hcpriv = NULL;
	rpipe_put_idx(rpipe->wa, index);
	wa_put(rpipe->wa);
	kfree(rpipe);
}
EXPORT_SYMBOL_GPL(rpipe_destroy);

/*
 * Locate an idle rpipe, create an structure for it and return it
 *
 * @wa 	  is referenced and unlocked
 * @crs   enum rpipe_attr, required endpoint characteristics
 *
 * The rpipe can be used only sequentially (not in parallel).
 *
 * The rpipe is moved into the "ready" state.
 */
static int rpipe_get_idle(struct wa_rpipe **prpipe, struct wahc *wa, u8 crs,
			  gfp_t gfp)
{
	int result;
	unsigned rpipe_idx;
	struct wa_rpipe *rpipe;
	struct device *dev = &wa->usb_iface->dev;

	rpipe = kzalloc(sizeof(*rpipe), gfp);
	if (rpipe == NULL)
		return -ENOMEM;
	rpipe_init(rpipe);

	/* Look for an idle pipe */
	for (rpipe_idx = 0; rpipe_idx < wa->rpipes; rpipe_idx++) {
		rpipe_idx = rpipe_get_idx(wa, rpipe_idx);
		if (rpipe_idx >= wa->rpipes)	/* no more pipes :( */
			break;
		result =  __rpipe_get_descr(wa, &rpipe->descr, rpipe_idx);
		if (result < 0)
			dev_err(dev, "Can't get descriptor for rpipe %u: %d\n",
				rpipe_idx, result);
		else if ((rpipe->descr.bmCharacteristics & crs) != 0)
			goto found;
		rpipe_put_idx(wa, rpipe_idx);
	}
	*prpipe = NULL;
	kfree(rpipe);
	return -ENXIO;

found:
	set_bit(rpipe_idx, wa->rpipe_bm);
	rpipe->wa = wa_get(wa);
	*prpipe = rpipe;
	return 0;
}

static int __rpipe_reset(struct wahc *wa, unsigned index)
{
	int result;
	struct device *dev = &wa->usb_iface->dev;

	result = usb_control_msg(
		wa->usb_dev, usb_sndctrlpipe(wa->usb_dev, 0),
		USB_REQ_RPIPE_RESET,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_RPIPE,
		0, index, NULL, 0, 1000 /* FIXME: arbitrary */);
	if (result < 0)
		dev_err(dev, "rpipe %u: reset failed: %d\n",
			index, result);
	return result;
}

/*
 * Fake companion descriptor for ep0
 *
 * See WUSB1.0[7.4.4], most of this is zero for bulk/int/ctl
 */
static struct usb_wireless_ep_comp_descriptor epc0 = {
	.bLength = sizeof(epc0),
	.bDescriptorType = USB_DT_WIRELESS_ENDPOINT_COMP,
/*	.bMaxBurst = 1, */
	.bMaxSequence = 31,
};

/*
 * Look for EP companion descriptor
 *
 * Get there, look for Inara in the endpoint's extra descriptors
 */
static struct usb_wireless_ep_comp_descriptor *rpipe_epc_find(
		struct device *dev, struct usb_host_endpoint *ep)
{
	void *itr;
	size_t itr_size;
	struct usb_descriptor_header *hdr;
	struct usb_wireless_ep_comp_descriptor *epcd;

	if (ep->desc.bEndpointAddress == 0) {
		epcd = &epc0;
		goto out;
	}
	itr = ep->extra;
	itr_size = ep->extralen;
	epcd = NULL;
	while (itr_size > 0) {
		if (itr_size < sizeof(*hdr)) {
			dev_err(dev, "HW Bug? ep 0x%02x: extra descriptors "
				"at offset %zu: only %zu bytes left\n",
				ep->desc.bEndpointAddress,
				itr - (void *) ep->extra, itr_size);
			break;
		}
		hdr = itr;
		if (hdr->bDescriptorType == USB_DT_WIRELESS_ENDPOINT_COMP) {
			epcd = itr;
			break;
		}
		if (hdr->bLength > itr_size) {
			dev_err(dev, "HW Bug? ep 0x%02x: extra descriptor "
				"at offset %zu (type 0x%02x) "
				"length %d but only %zu bytes left\n",
				ep->desc.bEndpointAddress,
				itr - (void *) ep->extra, hdr->bDescriptorType,
				hdr->bLength, itr_size);
			break;
		}
		itr += hdr->bLength;
		itr_size -= hdr->bDescriptorType;
	}
out:
	return epcd;
}

/*
 * Aim an rpipe to its device & endpoint destination
 *
 * Make sure we change the address to unauthenticathed if the device
 * is WUSB and it is not authenticated.
 */
static int rpipe_aim(struct wa_rpipe *rpipe, struct wahc *wa,
		     struct usb_host_endpoint *ep, struct urb *urb, gfp_t gfp)
{
	int result = -ENOMSG;	/* better code for lack of companion? */
	struct device *dev = &wa->usb_iface->dev;
	struct usb_device *usb_dev = urb->dev;
	struct usb_wireless_ep_comp_descriptor *epcd;
	u8 unauth;

	epcd = rpipe_epc_find(dev, ep);
	if (epcd == NULL) {
		dev_err(dev, "ep 0x%02x: can't find companion descriptor\n",
			ep->desc.bEndpointAddress);
		goto error;
	}
	unauth = usb_dev->wusb && !usb_dev->authenticated ? 0x80 : 0;
	__rpipe_reset(wa, le16_to_cpu(rpipe->descr.wRPipeIndex));
	atomic_set(&rpipe->segs_available, le16_to_cpu(rpipe->descr.wRequests));
	/* FIXME: block allocation system; request with queuing and timeout */
	/* FIXME: compute so seg_size > ep->maxpktsize */
	rpipe->descr.wBlocks = cpu_to_le16(16);		/* given */
	/* ep0 maxpktsize is 0x200 (WUSB1.0[4.8.1]) */
	rpipe->descr.wMaxPacketSize = cpu_to_le16(ep->desc.wMaxPacketSize);
	rpipe->descr.bHSHubAddress = 0;			/* reserved: zero */
	rpipe->descr.bHSHubPort = wusb_port_no_to_idx(urb->dev->portnum);
	/* FIXME: use maximum speed as supported or recommended by device */
	rpipe->descr.bSpeed = usb_pipeendpoint(urb->pipe) == 0 ?
		UWB_PHY_RATE_53 : UWB_PHY_RATE_200;

	dev_dbg(dev, "addr %u (0x%02x) rpipe #%u ep# %u speed %d\n",
		urb->dev->devnum, urb->dev->devnum | unauth,
		le16_to_cpu(rpipe->descr.wRPipeIndex),
		usb_pipeendpoint(urb->pipe), rpipe->descr.bSpeed);

	/* see security.c:wusb_update_address() */
	if (unlikely(urb->dev->devnum == 0x80))
		rpipe->descr.bDeviceAddress = 0;
	else
		rpipe->descr.bDeviceAddress = urb->dev->devnum | unauth;
	rpipe->descr.bEndpointAddress = ep->desc.bEndpointAddress;
	/* FIXME: bDataSequence */
	rpipe->descr.bDataSequence = 0;
	/* FIXME: dwCurrentWindow */
	rpipe->descr.dwCurrentWindow = cpu_to_le32(1);
	/* FIXME: bMaxDataSequence */
	rpipe->descr.bMaxDataSequence = epcd->bMaxSequence - 1;
	rpipe->descr.bInterval = ep->desc.bInterval;
	/* FIXME: bOverTheAirInterval */
	rpipe->descr.bOverTheAirInterval = 0;	/* 0 if not isoc */
	/* FIXME: xmit power & preamble blah blah */
	rpipe->descr.bmAttribute = ep->desc.bmAttributes & 0x03;
	/* rpipe->descr.bmCharacteristics RO */
	/* FIXME: bmRetryOptions */
	rpipe->descr.bmRetryOptions = 15;
	/* FIXME: use for assessing link quality? */
	rpipe->descr.wNumTransactionErrors = 0;
	result = __rpipe_set_descr(wa, &rpipe->descr,
				   le16_to_cpu(rpipe->descr.wRPipeIndex));
	if (result < 0) {
		dev_err(dev, "Cannot aim rpipe: %d\n", result);
		goto error;
	}
	result = 0;
error:
	return result;
}

/*
 * Check an aimed rpipe to make sure it points to where we want
 *
 * We use bit 19 of the Linux USB pipe bitmap for unauth vs auth
 * space; when it is like that, we or 0x80 to make an unauth address.
 */
static int rpipe_check_aim(const struct wa_rpipe *rpipe, const struct wahc *wa,
			   const struct usb_host_endpoint *ep,
			   const struct urb *urb, gfp_t gfp)
{
	int result = 0;		/* better code for lack of companion? */
	struct device *dev = &wa->usb_iface->dev;
	struct usb_device *usb_dev = urb->dev;
	u8 unauth = (usb_dev->wusb && !usb_dev->authenticated) ? 0x80 : 0;
	u8 portnum = wusb_port_no_to_idx(urb->dev->portnum);

#define AIM_CHECK(rdf, val, text)					\
	do {								\
		if (rpipe->descr.rdf != (val)) {			\
			dev_err(dev,					\
				"rpipe aim discrepancy: " #rdf " " text "\n", \
				rpipe->descr.rdf, (val));		\
			result = -EINVAL;				\
			WARN_ON(1);					\
		}							\
	} while (0)
	AIM_CHECK(wMaxPacketSize, cpu_to_le16(ep->desc.wMaxPacketSize),
		  "(%u vs %u)");
	AIM_CHECK(bHSHubPort, portnum, "(%u vs %u)");
	AIM_CHECK(bSpeed, usb_pipeendpoint(urb->pipe) == 0 ?
			UWB_PHY_RATE_53 : UWB_PHY_RATE_200,
		  "(%u vs %u)");
	AIM_CHECK(bDeviceAddress, urb->dev->devnum | unauth, "(%u vs %u)");
	AIM_CHECK(bEndpointAddress, ep->desc.bEndpointAddress, "(%u vs %u)");
	AIM_CHECK(bInterval, ep->desc.bInterval, "(%u vs %u)");
	AIM_CHECK(bmAttribute, ep->desc.bmAttributes & 0x03, "(%u vs %u)");
#undef AIM_CHECK
	return result;
}

#ifndef CONFIG_BUG
#define CONFIG_BUG 0
#endif

/*
 * Make sure there is an rpipe allocated for an endpoint
 *
 * If already allocated, we just refcount it; if not, we get an
 * idle one, aim it to the right location and take it.
 *
 * Attaches to ep->hcpriv and rpipe->ep to ep.
 */
int rpipe_get_by_ep(struct wahc *wa, struct usb_host_endpoint *ep,
		    struct urb *urb, gfp_t gfp)
{
	int result = 0;
	struct device *dev = &wa->usb_iface->dev;
	struct wa_rpipe *rpipe;
	u8 eptype;

	mutex_lock(&wa->rpipe_mutex);
	rpipe = ep->hcpriv;
	if (rpipe != NULL) {
		if (CONFIG_BUG == 1) {
			result = rpipe_check_aim(rpipe, wa, ep, urb, gfp);
			if (result < 0)
				goto error;
		}
		__rpipe_get(rpipe);
		dev_dbg(dev, "ep 0x%02x: reusing rpipe %u\n",
			ep->desc.bEndpointAddress,
			le16_to_cpu(rpipe->descr.wRPipeIndex));
	} else {
		/* hmm, assign idle rpipe, aim it */
		result = -ENOBUFS;
		eptype = ep->desc.bmAttributes & 0x03;
		result = rpipe_get_idle(&rpipe, wa, 1 << eptype, gfp);
		if (result < 0)
			goto error;
		result = rpipe_aim(rpipe, wa, ep, urb, gfp);
		if (result < 0) {
			rpipe_put(rpipe);
			goto error;
		}
		ep->hcpriv = rpipe;
		rpipe->ep = ep;
		__rpipe_get(rpipe);	/* for caching into ep->hcpriv */
		dev_dbg(dev, "ep 0x%02x: using rpipe %u\n",
			ep->desc.bEndpointAddress,
			le16_to_cpu(rpipe->descr.wRPipeIndex));
	}
error:
	mutex_unlock(&wa->rpipe_mutex);
	return result;
}

/*
 * Allocate the bitmap for each rpipe.
 */
int wa_rpipes_create(struct wahc *wa)
{
	wa->rpipes = wa->wa_descr->wNumRPipes;
	wa->rpipe_bm = kzalloc(BITS_TO_LONGS(wa->rpipes)*sizeof(unsigned long),
			       GFP_KERNEL);
	if (wa->rpipe_bm == NULL)
		return -ENOMEM;
	return 0;
}

void wa_rpipes_destroy(struct wahc *wa)
{
	struct device *dev = &wa->usb_iface->dev;

	if (!bitmap_empty(wa->rpipe_bm, wa->rpipes)) {
		char buf[256];
		WARN_ON(1);
		bitmap_scnprintf(buf, sizeof(buf), wa->rpipe_bm, wa->rpipes);
		dev_err(dev, "BUG: pipes not released on exit: %s\n", buf);
	}
	kfree(wa->rpipe_bm);
}

/*
 * Release resources allocated for an endpoint
 *
 * If there is an associated rpipe to this endpoint, Abort any pending
 * transfers and put it. If the rpipe ends up being destroyed,
 * __rpipe_destroy() will cleanup ep->hcpriv.
 *
 * This is called before calling hcd->stop(), so you don't need to do
 * anything else in there.
 */
void rpipe_ep_disable(struct wahc *wa, struct usb_host_endpoint *ep)
{
	struct wa_rpipe *rpipe;

	mutex_lock(&wa->rpipe_mutex);
	rpipe = ep->hcpriv;
	if (rpipe != NULL) {
		u16 index = le16_to_cpu(rpipe->descr.wRPipeIndex);

		usb_control_msg(
			wa->usb_dev, usb_rcvctrlpipe(wa->usb_dev, 0),
			USB_REQ_RPIPE_ABORT,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_RPIPE,
			0, index, NULL, 0, 1000 /* FIXME: arbitrary */);
		rpipe_put(rpipe);
	}
	mutex_unlock(&wa->rpipe_mutex);
}
EXPORT_SYMBOL_GPL(rpipe_ep_disable);
