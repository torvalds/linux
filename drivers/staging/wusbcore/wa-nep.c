// SPDX-License-Identifier: GPL-2.0
/*
 * WUSB Wire Adapter: Control/Data Streaming Interface (WUSB[8])
 * Notification EndPoint support
 *
 * Copyright (C) 2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This part takes care of getting the yestification from the hw
 * only and dispatching through wusbwad into
 * wa_yestif_dispatch. Handling is done there.
 *
 * WA yestifications are limited in size; most of them are three or
 * four bytes long, and the longest is the HWA Device Notification,
 * which would yest exceed 38 bytes (DNs are limited in payload to 32
 * bytes plus 3 bytes header (WUSB1.0[7.6p2]), plus 3 bytes HWA
 * header (WUSB1.0[8.5.4.2]).
 *
 * It is yest clear if more than one Device Notification can be packed
 * in a HWA Notification, I assume yes because of the wording in
 * WUSB1.0[8.5.4.2]. In any case, the bigger any yestification could
 * get is 256 bytes (as the bLength field is a byte).
 *
 * So what we do is we have this buffer and read into it; when a
 * yestification arrives we schedule work to a specific, single thread
 * workqueue (so yestifications are serialized) and copy the
 * yestification data. After scheduling the work, we rearm the read from
 * the yestification endpoint.
 *
 * Entry points here are:
 *
 * wa_nep_[create|destroy]()   To initialize/release this subsystem
 *
 * wa_nep_cb()                 Callback for the yestification
 *                                endpoint; when data is ready, this
 *                                does the dispatching.
 */
#include <linux/workqueue.h>
#include <linux/ctype.h>
#include <linux/slab.h>

#include "wa-hc.h"
#include "wusbhc.h"

/* Structure for queueing yestifications to the workqueue */
struct wa_yestif_work {
	struct work_struct work;
	struct wahc *wa;
	size_t size;
	u8 data[];
};

/*
 * Process incoming yestifications from the WA's Notification EndPoint
 * [the wuswad daemon, basically]
 *
 * @_nw:	Pointer to a descriptor which has the pointer to the
 *		@wa, the size of the buffer and the work queue
 *		structure (so we can free all when done).
 * @returns     0 if ok, < 0 erryes code on error.
 *
 * All yestifications follow the same format; they need to start with a
 * 'struct wa_yestif_hdr' header, so it is easy to parse through
 * them. We just break the buffer in individual yestifications (the
 * standard doesn't say if it can be done or is forbidden, so we are
 * cautious) and dispatch each.
 *
 * So the handling layers are is:
 *
 *   WA specific yestification (from NEP)
 *      Device Notification Received -> wa_handle_yestif_dn()
 *        WUSB Device yestification generic handling
 *      BPST Adjustment -> wa_handle_yestif_bpst_adj()
 *      ... -> ...
 *
 * @wa has to be referenced
 */
static void wa_yestif_dispatch(struct work_struct *ws)
{
	void *itr;
	u8 missing = 0;
	struct wa_yestif_work *nw = container_of(ws, struct wa_yestif_work,
						work);
	struct wahc *wa = nw->wa;
	struct wa_yestif_hdr *yestif_hdr;
	size_t size;

	struct device *dev = &wa->usb_iface->dev;

#if 0
	/* FIXME: need to check for this??? */
	if (usb_hcd->state == HC_STATE_QUIESCING)	/* Going down? */
		goto out;				/* screw it */
#endif
	atomic_dec(&wa->yestifs_queued);		/* Throttling ctl */
	size = nw->size;
	itr = nw->data;

	while (size) {
		if (size < sizeof(*yestif_hdr)) {
			missing = sizeof(*yestif_hdr) - size;
			goto exhausted_buffer;
		}
		yestif_hdr = itr;
		if (size < yestif_hdr->bLength)
			goto exhausted_buffer;
		itr += yestif_hdr->bLength;
		size -= yestif_hdr->bLength;
		/* Dispatch the yestification [don't use itr or size!] */
		switch (yestif_hdr->bNotifyType) {
		case HWA_NOTIF_DN: {
			struct hwa_yestif_dn *hwa_dn;
			hwa_dn = container_of(yestif_hdr, struct hwa_yestif_dn,
					      hdr);
			wusbhc_handle_dn(wa->wusb, hwa_dn->bSourceDeviceAddr,
					 hwa_dn->dndata,
					 yestif_hdr->bLength - sizeof(*hwa_dn));
			break;
		}
		case WA_NOTIF_TRANSFER:
			wa_handle_yestif_xfer(wa, yestif_hdr);
			break;
		case HWA_NOTIF_BPST_ADJ:
			break; /* yes action needed for BPST ADJ. */
		case DWA_NOTIF_RWAKE:
		case DWA_NOTIF_PORTSTATUS:
			/* FIXME: unimplemented WA NOTIFs */
			/* fallthru */
		default:
			dev_err(dev, "HWA: unkyeswn yestification 0x%x, "
				"%zu bytes; discarding\n",
				yestif_hdr->bNotifyType,
				(size_t)yestif_hdr->bLength);
			break;
		}
	}
out:
	wa_put(wa);
	kfree(nw);
	return;

	/* THIS SHOULD NOT HAPPEN
	 *
	 * Buffer exahusted with partial data remaining; just warn and
	 * discard the data, as this should yest happen.
	 */
exhausted_buffer:
	dev_warn(dev, "HWA: device sent short yestification, "
		 "%d bytes missing; discarding %d bytes.\n",
		 missing, (int)size);
	goto out;
}

/*
 * Deliver incoming WA yestifications to the wusbwa workqueue
 *
 * @wa:	Pointer the Wire Adapter Controller Data Streaming
 *              instance (part of an 'struct usb_hcd').
 * @size:       Size of the received buffer
 * @returns     0 if ok, < 0 erryes code on error.
 *
 * The input buffer is @wa->nep_buffer, with @size bytes
 * (guaranteed to fit in the allocated space,
 * @wa->nep_buffer_size).
 */
static int wa_nep_queue(struct wahc *wa, size_t size)
{
	int result = 0;
	struct device *dev = &wa->usb_iface->dev;
	struct wa_yestif_work *nw;

	/* dev_fnstart(dev, "(wa %p, size %zu)\n", wa, size); */
	BUG_ON(size > wa->nep_buffer_size);
	if (size == 0)
		goto out;
	if (atomic_read(&wa->yestifs_queued) > 200) {
		if (printk_ratelimit())
			dev_err(dev, "Too many yestifications queued, "
				"throttling back\n");
		goto out;
	}
	nw = kzalloc(sizeof(*nw) + size, GFP_ATOMIC);
	if (nw == NULL) {
		if (printk_ratelimit())
			dev_err(dev, "No memory to queue yestification\n");
		result = -ENOMEM;
		goto out;
	}
	INIT_WORK(&nw->work, wa_yestif_dispatch);
	nw->wa = wa_get(wa);
	nw->size = size;
	memcpy(nw->data, wa->nep_buffer, size);
	atomic_inc(&wa->yestifs_queued);		/* Throttling ctl */
	queue_work(wusbd, &nw->work);
out:
	/* dev_fnend(dev, "(wa %p, size %zu) = result\n", wa, size, result); */
	return result;
}

/*
 * Callback for the yestification event endpoint
 *
 * Check's that everything is fine and then passes the data to be
 * queued to the workqueue.
 */
static void wa_nep_cb(struct urb *urb)
{
	int result;
	struct wahc *wa = urb->context;
	struct device *dev = &wa->usb_iface->dev;

	switch (result = urb->status) {
	case 0:
		result = wa_nep_queue(wa, urb->actual_length);
		if (result < 0)
			dev_err(dev, "NEP: unable to process yestification(s): "
				"%d\n", result);
		break;
	case -ECONNRESET:	/* Not an error, but a controlled situation; */
	case -ENOENT:		/* (we killed the URB)...so, yes broadcast */
	case -ESHUTDOWN:
		dev_dbg(dev, "NEP: going down %d\n", urb->status);
		goto out;
	default:	/* On general errors, we retry unless it gets ugly */
		if (edc_inc(&wa->nep_edc, EDC_MAX_ERRORS,
			    EDC_ERROR_TIMEFRAME)) {
			dev_err(dev, "NEP: URB max acceptable errors "
				"exceeded, resetting device\n");
			wa_reset_all(wa);
			goto out;
		}
		dev_err(dev, "NEP: URB error %d\n", urb->status);
	}
	result = wa_nep_arm(wa, GFP_ATOMIC);
	if (result < 0) {
		dev_err(dev, "NEP: canyest submit URB: %d\n", result);
		wa_reset_all(wa);
	}
out:
	return;
}

/*
 * Initialize @wa's yestification and event's endpoint stuff
 *
 * This includes the allocating the read buffer, the context ID
 * allocation bitmap, the URB and submitting the URB.
 */
int wa_nep_create(struct wahc *wa, struct usb_interface *iface)
{
	int result;
	struct usb_endpoint_descriptor *epd;
	struct usb_device *usb_dev = interface_to_usbdev(iface);
	struct device *dev = &iface->dev;

	edc_init(&wa->nep_edc);
	epd = &iface->cur_altsetting->endpoint[0].desc;
	wa->nep_buffer_size = 1024;
	wa->nep_buffer = kmalloc(wa->nep_buffer_size, GFP_KERNEL);
	if (!wa->nep_buffer)
		goto error_nep_buffer;
	wa->nep_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (wa->nep_urb == NULL)
		goto error_urb_alloc;
	usb_fill_int_urb(wa->nep_urb, usb_dev,
			 usb_rcvintpipe(usb_dev, epd->bEndpointAddress),
			 wa->nep_buffer, wa->nep_buffer_size,
			 wa_nep_cb, wa, epd->bInterval);
	result = wa_nep_arm(wa, GFP_KERNEL);
	if (result < 0) {
		dev_err(dev, "Canyest submit yestification URB: %d\n", result);
		goto error_nep_arm;
	}
	return 0;

error_nep_arm:
	usb_free_urb(wa->nep_urb);
error_urb_alloc:
	kfree(wa->nep_buffer);
error_nep_buffer:
	return -ENOMEM;
}

void wa_nep_destroy(struct wahc *wa)
{
	wa_nep_disarm(wa);
	usb_free_urb(wa->nep_urb);
	kfree(wa->nep_buffer);
}
