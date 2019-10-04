// SPDX-License-Identifier: GPL-2.0
/*
 * WUSB Wire Adapter
 * Data transfer and URB enqueing
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * How transfers work: get a buffer, break it up in segments (segment
 * size is a multiple of the maxpacket size). For each segment issue a
 * segment request (struct wa_xfer_*), then send the data buffer if
 * out or nothing if in (all over the DTO endpoint).
 *
 * For each submitted segment request, a notification will come over
 * the NEP endpoint and a transfer result (struct xfer_result) will
 * arrive in the DTI URB. Read it, get the xfer ID, see if there is
 * data coming (inbound transfer), schedule a read and handle it.
 *
 * Sounds simple, it is a pain to implement.
 *
 *
 * ENTRY POINTS
 *
 *   FIXME
 *
 * LIFE CYCLE / STATE DIAGRAM
 *
 *   FIXME
 *
 * THIS CODE IS DISGUSTING
 *
 *   Warned you are; it's my second try and still not happy with it.
 *
 * NOTES:
 *
 *   - No iso
 *
 *   - Supports DMA xfers, control, bulk and maybe interrupt
 *
 *   - Does not recycle unused rpipes
 *
 *     An rpipe is assigned to an endpoint the first time it is used,
 *     and then it's there, assigned, until the endpoint is disabled
 *     (destroyed [{h,d}wahc_op_ep_disable()]. The assignment of the
 *     rpipe to the endpoint is done under the wa->rpipe_sem semaphore
 *     (should be a mutex).
 *
 *     Two methods it could be done:
 *
 *     (a) set up a timer every time an rpipe's use count drops to 1
 *         (which means unused) or when a transfer ends. Reset the
 *         timer when a xfer is queued. If the timer expires, release
 *         the rpipe [see rpipe_ep_disable()].
 *
 *     (b) when looking for free rpipes to attach [rpipe_get_by_ep()],
 *         when none are found go over the list, check their endpoint
 *         and their activity record (if no last-xfer-done-ts in the
 *         last x seconds) take it
 *
 *     However, due to the fact that we have a set of limited
 *     resources (max-segments-at-the-same-time per xfer,
 *     xfers-per-ripe, blocks-per-rpipe, rpipes-per-host), at the end
 *     we are going to have to rebuild all this based on an scheduler,
 *     to where we have a list of transactions to do and based on the
 *     availability of the different required components (blocks,
 *     rpipes, segment slots, etc), we go scheduling them. Painful.
 */
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/hash.h>
#include <linux/ratelimit.h>
#include <linux/export.h>
#include <linux/scatterlist.h>

#include "wa-hc.h"
#include "wusbhc.h"

enum {
	/* [WUSB] section 8.3.3 allocates 7 bits for the segment index. */
	WA_SEGS_MAX = 128,
};

enum wa_seg_status {
	WA_SEG_NOTREADY,
	WA_SEG_READY,
	WA_SEG_DELAYED,
	WA_SEG_SUBMITTED,
	WA_SEG_PENDING,
	WA_SEG_DTI_PENDING,
	WA_SEG_DONE,
	WA_SEG_ERROR,
	WA_SEG_ABORTED,
};

static void wa_xfer_delayed_run(struct wa_rpipe *);
static int __wa_xfer_delayed_run(struct wa_rpipe *rpipe, int *dto_waiting);

/*
 * Life cycle governed by 'struct urb' (the refcount of the struct is
 * that of the 'struct urb' and usb_free_urb() would free the whole
 * struct).
 */
struct wa_seg {
	struct urb tr_urb;		/* transfer request urb. */
	struct urb *isoc_pack_desc_urb;	/* for isoc packet descriptor. */
	struct urb *dto_urb;		/* for data output. */
	struct list_head list_node;	/* for rpipe->req_list */
	struct wa_xfer *xfer;		/* out xfer */
	u8 index;			/* which segment we are */
	int isoc_frame_count;	/* number of isoc frames in this segment. */
	int isoc_frame_offset;	/* starting frame offset in the xfer URB. */
	/* Isoc frame that the current transfer buffer corresponds to. */
	int isoc_frame_index;
	int isoc_size;	/* size of all isoc frames sent by this seg. */
	enum wa_seg_status status;
	ssize_t result;			/* bytes xfered or error */
	struct wa_xfer_hdr xfer_hdr;
};

static inline void wa_seg_init(struct wa_seg *seg)
{
	usb_init_urb(&seg->tr_urb);

	/* set the remaining memory to 0. */
	memset(((void *)seg) + sizeof(seg->tr_urb), 0,
		sizeof(*seg) - sizeof(seg->tr_urb));
}

/*
 * Protected by xfer->lock
 *
 */
struct wa_xfer {
	struct kref refcnt;
	struct list_head list_node;
	spinlock_t lock;
	u32 id;

	struct wahc *wa;		/* Wire adapter we are plugged to */
	struct usb_host_endpoint *ep;
	struct urb *urb;		/* URB we are transferring for */
	struct wa_seg **seg;		/* transfer segments */
	u8 segs, segs_submitted, segs_done;
	unsigned is_inbound:1;
	unsigned is_dma:1;
	size_t seg_size;
	int result;

	gfp_t gfp;			/* allocation mask */

	struct wusb_dev *wusb_dev;	/* for activity timestamps */
};

static void __wa_populate_dto_urb_isoc(struct wa_xfer *xfer,
	struct wa_seg *seg, int curr_iso_frame);
static void wa_complete_remaining_xfer_segs(struct wa_xfer *xfer,
		int starting_index, enum wa_seg_status status);

static inline void wa_xfer_init(struct wa_xfer *xfer)
{
	kref_init(&xfer->refcnt);
	INIT_LIST_HEAD(&xfer->list_node);
	spin_lock_init(&xfer->lock);
}

/*
 * Destroy a transfer structure
 *
 * Note that freeing xfer->seg[cnt]->tr_urb will free the containing
 * xfer->seg[cnt] memory that was allocated by __wa_xfer_setup_segs.
 */
static void wa_xfer_destroy(struct kref *_xfer)
{
	struct wa_xfer *xfer = container_of(_xfer, struct wa_xfer, refcnt);
	if (xfer->seg) {
		unsigned cnt;
		for (cnt = 0; cnt < xfer->segs; cnt++) {
			struct wa_seg *seg = xfer->seg[cnt];
			if (seg) {
				usb_free_urb(seg->isoc_pack_desc_urb);
				if (seg->dto_urb) {
					kfree(seg->dto_urb->sg);
					usb_free_urb(seg->dto_urb);
				}
				usb_free_urb(&seg->tr_urb);
			}
		}
		kfree(xfer->seg);
	}
	kfree(xfer);
}

static void wa_xfer_get(struct wa_xfer *xfer)
{
	kref_get(&xfer->refcnt);
}

static void wa_xfer_put(struct wa_xfer *xfer)
{
	kref_put(&xfer->refcnt, wa_xfer_destroy);
}

/*
 * Try to get exclusive access to the DTO endpoint resource.  Return true
 * if successful.
 */
static inline int __wa_dto_try_get(struct wahc *wa)
{
	return (test_and_set_bit(0, &wa->dto_in_use) == 0);
}

/* Release the DTO endpoint resource. */
static inline void __wa_dto_put(struct wahc *wa)
{
	clear_bit_unlock(0, &wa->dto_in_use);
}

/* Service RPIPEs that are waiting on the DTO resource. */
static void wa_check_for_delayed_rpipes(struct wahc *wa)
{
	unsigned long flags;
	int dto_waiting = 0;
	struct wa_rpipe *rpipe;

	spin_lock_irqsave(&wa->rpipe_lock, flags);
	while (!list_empty(&wa->rpipe_delayed_list) && !dto_waiting) {
		rpipe = list_first_entry(&wa->rpipe_delayed_list,
				struct wa_rpipe, list_node);
		__wa_xfer_delayed_run(rpipe, &dto_waiting);
		/* remove this RPIPE from the list if it is not waiting. */
		if (!dto_waiting) {
			pr_debug("%s: RPIPE %d serviced and removed from delayed list.\n",
				__func__,
				le16_to_cpu(rpipe->descr.wRPipeIndex));
			list_del_init(&rpipe->list_node);
		}
	}
	spin_unlock_irqrestore(&wa->rpipe_lock, flags);
}

/* add this RPIPE to the end of the delayed RPIPE list. */
static void wa_add_delayed_rpipe(struct wahc *wa, struct wa_rpipe *rpipe)
{
	unsigned long flags;

	spin_lock_irqsave(&wa->rpipe_lock, flags);
	/* add rpipe to the list if it is not already on it. */
	if (list_empty(&rpipe->list_node)) {
		pr_debug("%s: adding RPIPE %d to the delayed list.\n",
			__func__, le16_to_cpu(rpipe->descr.wRPipeIndex));
		list_add_tail(&rpipe->list_node, &wa->rpipe_delayed_list);
	}
	spin_unlock_irqrestore(&wa->rpipe_lock, flags);
}

/*
 * xfer is referenced
 *
 * xfer->lock has to be unlocked
 *
 * We take xfer->lock for setting the result; this is a barrier
 * against drivers/usb/core/hcd.c:unlink1() being called after we call
 * usb_hcd_giveback_urb() and wa_urb_dequeue() trying to get a
 * reference to the transfer.
 */
static void wa_xfer_giveback(struct wa_xfer *xfer)
{
	unsigned long flags;

	spin_lock_irqsave(&xfer->wa->xfer_list_lock, flags);
	list_del_init(&xfer->list_node);
	usb_hcd_unlink_urb_from_ep(&(xfer->wa->wusb->usb_hcd), xfer->urb);
	spin_unlock_irqrestore(&xfer->wa->xfer_list_lock, flags);
	/* FIXME: segmentation broken -- kills DWA */
	wusbhc_giveback_urb(xfer->wa->wusb, xfer->urb, xfer->result);
	wa_put(xfer->wa);
	wa_xfer_put(xfer);
}

/*
 * xfer is referenced
 *
 * xfer->lock has to be unlocked
 */
static void wa_xfer_completion(struct wa_xfer *xfer)
{
	if (xfer->wusb_dev)
		wusb_dev_put(xfer->wusb_dev);
	rpipe_put(xfer->ep->hcpriv);
	wa_xfer_giveback(xfer);
}

/*
 * Initialize a transfer's ID
 *
 * We need to use a sequential number; if we use the pointer or the
 * hash of the pointer, it can repeat over sequential transfers and
 * then it will confuse the HWA....wonder why in hell they put a 32
 * bit handle in there then.
 */
static void wa_xfer_id_init(struct wa_xfer *xfer)
{
	xfer->id = atomic_add_return(1, &xfer->wa->xfer_id_count);
}

/* Return the xfer's ID. */
static inline u32 wa_xfer_id(struct wa_xfer *xfer)
{
	return xfer->id;
}

/* Return the xfer's ID in transport format (little endian). */
static inline __le32 wa_xfer_id_le32(struct wa_xfer *xfer)
{
	return cpu_to_le32(xfer->id);
}

/*
 * If transfer is done, wrap it up and return true
 *
 * xfer->lock has to be locked
 */
static unsigned __wa_xfer_is_done(struct wa_xfer *xfer)
{
	struct device *dev = &xfer->wa->usb_iface->dev;
	unsigned result, cnt;
	struct wa_seg *seg;
	struct urb *urb = xfer->urb;
	unsigned found_short = 0;

	result = xfer->segs_done == xfer->segs_submitted;
	if (result == 0)
		goto out;
	urb->actual_length = 0;
	for (cnt = 0; cnt < xfer->segs; cnt++) {
		seg = xfer->seg[cnt];
		switch (seg->status) {
		case WA_SEG_DONE:
			if (found_short && seg->result > 0) {
				dev_dbg(dev, "xfer %p ID %08X#%u: bad short segments (%zu)\n",
					xfer, wa_xfer_id(xfer), cnt,
					seg->result);
				urb->status = -EINVAL;
				goto out;
			}
			urb->actual_length += seg->result;
			if (!(usb_pipeisoc(xfer->urb->pipe))
				&& seg->result < xfer->seg_size
			    && cnt != xfer->segs-1)
				found_short = 1;
			dev_dbg(dev, "xfer %p ID %08X#%u: DONE short %d "
				"result %zu urb->actual_length %d\n",
				xfer, wa_xfer_id(xfer), seg->index, found_short,
				seg->result, urb->actual_length);
			break;
		case WA_SEG_ERROR:
			xfer->result = seg->result;
			dev_dbg(dev, "xfer %p ID %08X#%u: ERROR result %zi(0x%08zX)\n",
				xfer, wa_xfer_id(xfer), seg->index, seg->result,
				seg->result);
			goto out;
		case WA_SEG_ABORTED:
			xfer->result = seg->result;
			dev_dbg(dev, "xfer %p ID %08X#%u: ABORTED result %zi(0x%08zX)\n",
				xfer, wa_xfer_id(xfer), seg->index, seg->result,
				seg->result);
			goto out;
		default:
			dev_warn(dev, "xfer %p ID %08X#%u: is_done bad state %d\n",
				 xfer, wa_xfer_id(xfer), cnt, seg->status);
			xfer->result = -EINVAL;
			goto out;
		}
	}
	xfer->result = 0;
out:
	return result;
}

/*
 * Mark the given segment as done.  Return true if this completes the xfer.
 * This should only be called for segs that have been submitted to an RPIPE.
 * Delayed segs are not marked as submitted so they do not need to be marked
 * as done when cleaning up.
 *
 * xfer->lock has to be locked
 */
static unsigned __wa_xfer_mark_seg_as_done(struct wa_xfer *xfer,
	struct wa_seg *seg, enum wa_seg_status status)
{
	seg->status = status;
	xfer->segs_done++;

	/* check for done. */
	return __wa_xfer_is_done(xfer);
}

/*
 * Search for a transfer list ID on the HCD's URB list
 *
 * For 32 bit architectures, we use the pointer itself; for 64 bits, a
 * 32-bit hash of the pointer.
 *
 * @returns NULL if not found.
 */
static struct wa_xfer *wa_xfer_get_by_id(struct wahc *wa, u32 id)
{
	unsigned long flags;
	struct wa_xfer *xfer_itr;
	spin_lock_irqsave(&wa->xfer_list_lock, flags);
	list_for_each_entry(xfer_itr, &wa->xfer_list, list_node) {
		if (id == xfer_itr->id) {
			wa_xfer_get(xfer_itr);
			goto out;
		}
	}
	xfer_itr = NULL;
out:
	spin_unlock_irqrestore(&wa->xfer_list_lock, flags);
	return xfer_itr;
}

struct wa_xfer_abort_buffer {
	struct urb urb;
	struct wahc *wa;
	struct wa_xfer_abort cmd;
};

static void __wa_xfer_abort_cb(struct urb *urb)
{
	struct wa_xfer_abort_buffer *b = urb->context;
	struct wahc *wa = b->wa;

	/*
	 * If the abort request URB failed, then the HWA did not get the abort
	 * command.  Forcibly clean up the xfer without waiting for a Transfer
	 * Result from the HWA.
	 */
	if (urb->status < 0) {
		struct wa_xfer *xfer;
		struct device *dev = &wa->usb_iface->dev;

		xfer = wa_xfer_get_by_id(wa, le32_to_cpu(b->cmd.dwTransferID));
		dev_err(dev, "%s: Transfer Abort request failed. result: %d\n",
			__func__, urb->status);
		if (xfer) {
			unsigned long flags;
			int done, seg_index = 0;
			struct wa_rpipe *rpipe = xfer->ep->hcpriv;

			dev_err(dev, "%s: cleaning up xfer %p ID 0x%08X.\n",
				__func__, xfer, wa_xfer_id(xfer));
			spin_lock_irqsave(&xfer->lock, flags);
			/* skip done segs. */
			while (seg_index < xfer->segs) {
				struct wa_seg *seg = xfer->seg[seg_index];

				if ((seg->status == WA_SEG_DONE) ||
					(seg->status == WA_SEG_ERROR)) {
					++seg_index;
				} else {
					break;
				}
			}
			/* mark remaining segs as aborted. */
			wa_complete_remaining_xfer_segs(xfer, seg_index,
				WA_SEG_ABORTED);
			done = __wa_xfer_is_done(xfer);
			spin_unlock_irqrestore(&xfer->lock, flags);
			if (done)
				wa_xfer_completion(xfer);
			wa_xfer_delayed_run(rpipe);
			wa_xfer_put(xfer);
		} else {
			dev_err(dev, "%s: xfer ID 0x%08X already gone.\n",
				 __func__, le32_to_cpu(b->cmd.dwTransferID));
		}
	}

	wa_put(wa);	/* taken in __wa_xfer_abort */
	usb_put_urb(&b->urb);
}

/*
 * Aborts an ongoing transaction
 *
 * Assumes the transfer is referenced and locked and in a submitted
 * state (mainly that there is an endpoint/rpipe assigned).
 *
 * The callback (see above) does nothing but freeing up the data by
 * putting the URB. Because the URB is allocated at the head of the
 * struct, the whole space we allocated is kfreed. *
 */
static int __wa_xfer_abort(struct wa_xfer *xfer)
{
	int result = -ENOMEM;
	struct device *dev = &xfer->wa->usb_iface->dev;
	struct wa_xfer_abort_buffer *b;
	struct wa_rpipe *rpipe = xfer->ep->hcpriv;

	b = kmalloc(sizeof(*b), GFP_ATOMIC);
	if (b == NULL)
		goto error_kmalloc;
	b->cmd.bLength =  sizeof(b->cmd);
	b->cmd.bRequestType = WA_XFER_ABORT;
	b->cmd.wRPipe = rpipe->descr.wRPipeIndex;
	b->cmd.dwTransferID = wa_xfer_id_le32(xfer);
	b->wa = wa_get(xfer->wa);

	usb_init_urb(&b->urb);
	usb_fill_bulk_urb(&b->urb, xfer->wa->usb_dev,
		usb_sndbulkpipe(xfer->wa->usb_dev,
				xfer->wa->dto_epd->bEndpointAddress),
		&b->cmd, sizeof(b->cmd), __wa_xfer_abort_cb, b);
	result = usb_submit_urb(&b->urb, GFP_ATOMIC);
	if (result < 0)
		goto error_submit;
	return result;				/* callback frees! */


error_submit:
	wa_put(xfer->wa);
	if (printk_ratelimit())
		dev_err(dev, "xfer %p: Can't submit abort request: %d\n",
			xfer, result);
	kfree(b);
error_kmalloc:
	return result;

}

/*
 * Calculate the number of isoc frames starting from isoc_frame_offset
 * that will fit a in transfer segment.
 */
static int __wa_seg_calculate_isoc_frame_count(struct wa_xfer *xfer,
	int isoc_frame_offset, int *total_size)
{
	int segment_size = 0, frame_count = 0;
	int index = isoc_frame_offset;
	struct usb_iso_packet_descriptor *iso_frame_desc =
		xfer->urb->iso_frame_desc;

	while ((index < xfer->urb->number_of_packets)
		&& ((segment_size + iso_frame_desc[index].length)
				<= xfer->seg_size)) {
		/*
		 * For Alereon HWA devices, only include an isoc frame in an
		 * out segment if it is physically contiguous with the previous
		 * frame.  This is required because those devices expect
		 * the isoc frames to be sent as a single USB transaction as
		 * opposed to one transaction per frame with standard HWA.
		 */
		if ((xfer->wa->quirks & WUSB_QUIRK_ALEREON_HWA_CONCAT_ISOC)
			&& (xfer->is_inbound == 0)
			&& (index > isoc_frame_offset)
			&& ((iso_frame_desc[index - 1].offset +
				iso_frame_desc[index - 1].length) !=
				iso_frame_desc[index].offset))
			break;

		/* this frame fits. count it. */
		++frame_count;
		segment_size += iso_frame_desc[index].length;

		/* move to the next isoc frame. */
		++index;
	}

	*total_size = segment_size;
	return frame_count;
}

/*
 *
 * @returns < 0 on error, transfer segment request size if ok
 */
static ssize_t __wa_xfer_setup_sizes(struct wa_xfer *xfer,
				     enum wa_xfer_type *pxfer_type)
{
	ssize_t result;
	struct device *dev = &xfer->wa->usb_iface->dev;
	size_t maxpktsize;
	struct urb *urb = xfer->urb;
	struct wa_rpipe *rpipe = xfer->ep->hcpriv;

	switch (rpipe->descr.bmAttribute & 0x3) {
	case USB_ENDPOINT_XFER_CONTROL:
		*pxfer_type = WA_XFER_TYPE_CTL;
		result = sizeof(struct wa_xfer_ctl);
		break;
	case USB_ENDPOINT_XFER_INT:
	case USB_ENDPOINT_XFER_BULK:
		*pxfer_type = WA_XFER_TYPE_BI;
		result = sizeof(struct wa_xfer_bi);
		break;
	case USB_ENDPOINT_XFER_ISOC:
		*pxfer_type = WA_XFER_TYPE_ISO;
		result = sizeof(struct wa_xfer_hwaiso);
		break;
	default:
		/* never happens */
		BUG();
		result = -EINVAL;	/* shut gcc up */
	}
	xfer->is_inbound = urb->pipe & USB_DIR_IN ? 1 : 0;
	xfer->is_dma = urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP ? 1 : 0;

	maxpktsize = le16_to_cpu(rpipe->descr.wMaxPacketSize);
	xfer->seg_size = le16_to_cpu(rpipe->descr.wBlocks)
		* 1 << (xfer->wa->wa_descr->bRPipeBlockSize - 1);
	/* Compute the segment size and make sure it is a multiple of
	 * the maxpktsize (WUSB1.0[8.3.3.1])...not really too much of
	 * a check (FIXME) */
	if (xfer->seg_size < maxpktsize) {
		dev_err(dev,
			"HW BUG? seg_size %zu smaller than maxpktsize %zu\n",
			xfer->seg_size, maxpktsize);
		result = -EINVAL;
		goto error;
	}
	xfer->seg_size = (xfer->seg_size / maxpktsize) * maxpktsize;
	if ((rpipe->descr.bmAttribute & 0x3) == USB_ENDPOINT_XFER_ISOC) {
		int index = 0;

		xfer->segs = 0;
		/*
		 * loop over urb->number_of_packets to determine how many
		 * xfer segments will be needed to send the isoc frames.
		 */
		while (index < urb->number_of_packets) {
			int seg_size; /* don't care. */
			index += __wa_seg_calculate_isoc_frame_count(xfer,
					index, &seg_size);
			++xfer->segs;
		}
	} else {
		xfer->segs = DIV_ROUND_UP(urb->transfer_buffer_length,
						xfer->seg_size);
		if (xfer->segs == 0 && *pxfer_type == WA_XFER_TYPE_CTL)
			xfer->segs = 1;
	}

	if (xfer->segs > WA_SEGS_MAX) {
		dev_err(dev, "BUG? oops, number of segments %zu bigger than %d\n",
			(urb->transfer_buffer_length/xfer->seg_size),
			WA_SEGS_MAX);
		result = -EINVAL;
		goto error;
	}
error:
	return result;
}

static void __wa_setup_isoc_packet_descr(
		struct wa_xfer_packet_info_hwaiso *packet_desc,
		struct wa_xfer *xfer,
		struct wa_seg *seg) {
	struct usb_iso_packet_descriptor *iso_frame_desc =
		xfer->urb->iso_frame_desc;
	int frame_index;

	/* populate isoc packet descriptor. */
	packet_desc->bPacketType = WA_XFER_ISO_PACKET_INFO;
	packet_desc->wLength = cpu_to_le16(struct_size(packet_desc,
					   PacketLength,
					   seg->isoc_frame_count));
	for (frame_index = 0; frame_index < seg->isoc_frame_count;
		++frame_index) {
		int offset_index = frame_index + seg->isoc_frame_offset;
		packet_desc->PacketLength[frame_index] =
			cpu_to_le16(iso_frame_desc[offset_index].length);
	}
}


/* Fill in the common request header and xfer-type specific data. */
static void __wa_xfer_setup_hdr0(struct wa_xfer *xfer,
				 struct wa_xfer_hdr *xfer_hdr0,
				 enum wa_xfer_type xfer_type,
				 size_t xfer_hdr_size)
{
	struct wa_rpipe *rpipe = xfer->ep->hcpriv;
	struct wa_seg *seg = xfer->seg[0];

	xfer_hdr0 = &seg->xfer_hdr;
	xfer_hdr0->bLength = xfer_hdr_size;
	xfer_hdr0->bRequestType = xfer_type;
	xfer_hdr0->wRPipe = rpipe->descr.wRPipeIndex;
	xfer_hdr0->dwTransferID = wa_xfer_id_le32(xfer);
	xfer_hdr0->bTransferSegment = 0;
	switch (xfer_type) {
	case WA_XFER_TYPE_CTL: {
		struct wa_xfer_ctl *xfer_ctl =
			container_of(xfer_hdr0, struct wa_xfer_ctl, hdr);
		xfer_ctl->bmAttribute = xfer->is_inbound ? 1 : 0;
		memcpy(&xfer_ctl->baSetupData, xfer->urb->setup_packet,
		       sizeof(xfer_ctl->baSetupData));
		break;
	}
	case WA_XFER_TYPE_BI:
		break;
	case WA_XFER_TYPE_ISO: {
		struct wa_xfer_hwaiso *xfer_iso =
			container_of(xfer_hdr0, struct wa_xfer_hwaiso, hdr);
		struct wa_xfer_packet_info_hwaiso *packet_desc =
			((void *)xfer_iso) + xfer_hdr_size;

		/* populate the isoc section of the transfer request. */
		xfer_iso->dwNumOfPackets = cpu_to_le32(seg->isoc_frame_count);
		/* populate isoc packet descriptor. */
		__wa_setup_isoc_packet_descr(packet_desc, xfer, seg);
		break;
	}
	default:
		BUG();
	};
}

/*
 * Callback for the OUT data phase of the segment request
 *
 * Check wa_seg_tr_cb(); most comments also apply here because this
 * function does almost the same thing and they work closely
 * together.
 *
 * If the seg request has failed but this DTO phase has succeeded,
 * wa_seg_tr_cb() has already failed the segment and moved the
 * status to WA_SEG_ERROR, so this will go through 'case 0' and
 * effectively do nothing.
 */
static void wa_seg_dto_cb(struct urb *urb)
{
	struct wa_seg *seg = urb->context;
	struct wa_xfer *xfer = seg->xfer;
	struct wahc *wa;
	struct device *dev;
	struct wa_rpipe *rpipe;
	unsigned long flags;
	unsigned rpipe_ready = 0;
	int data_send_done = 1, release_dto = 0, holding_dto = 0;
	u8 done = 0;
	int result;

	/* free the sg if it was used. */
	kfree(urb->sg);
	urb->sg = NULL;

	spin_lock_irqsave(&xfer->lock, flags);
	wa = xfer->wa;
	dev = &wa->usb_iface->dev;
	if (usb_pipeisoc(xfer->urb->pipe)) {
		/* Alereon HWA sends all isoc frames in a single transfer. */
		if (wa->quirks & WUSB_QUIRK_ALEREON_HWA_CONCAT_ISOC)
			seg->isoc_frame_index += seg->isoc_frame_count;
		else
			seg->isoc_frame_index += 1;
		if (seg->isoc_frame_index < seg->isoc_frame_count) {
			data_send_done = 0;
			holding_dto = 1; /* checked in error cases. */
			/*
			 * if this is the last isoc frame of the segment, we
			 * can release DTO after sending this frame.
			 */
			if ((seg->isoc_frame_index + 1) >=
				seg->isoc_frame_count)
				release_dto = 1;
		}
		dev_dbg(dev, "xfer 0x%08X#%u: isoc frame = %d, holding_dto = %d, release_dto = %d.\n",
			wa_xfer_id(xfer), seg->index, seg->isoc_frame_index,
			holding_dto, release_dto);
	}
	spin_unlock_irqrestore(&xfer->lock, flags);

	switch (urb->status) {
	case 0:
		spin_lock_irqsave(&xfer->lock, flags);
		seg->result += urb->actual_length;
		if (data_send_done) {
			dev_dbg(dev, "xfer 0x%08X#%u: data out done (%zu bytes)\n",
				wa_xfer_id(xfer), seg->index, seg->result);
			if (seg->status < WA_SEG_PENDING)
				seg->status = WA_SEG_PENDING;
		} else {
			/* should only hit this for isoc xfers. */
			/*
			 * Populate the dto URB with the next isoc frame buffer,
			 * send the URB and release DTO if we no longer need it.
			 */
			 __wa_populate_dto_urb_isoc(xfer, seg,
				seg->isoc_frame_offset + seg->isoc_frame_index);

			/* resubmit the URB with the next isoc frame. */
			/* take a ref on resubmit. */
			wa_xfer_get(xfer);
			result = usb_submit_urb(seg->dto_urb, GFP_ATOMIC);
			if (result < 0) {
				dev_err(dev, "xfer 0x%08X#%u: DTO submit failed: %d\n",
				       wa_xfer_id(xfer), seg->index, result);
				spin_unlock_irqrestore(&xfer->lock, flags);
				goto error_dto_submit;
			}
		}
		spin_unlock_irqrestore(&xfer->lock, flags);
		if (release_dto) {
			__wa_dto_put(wa);
			wa_check_for_delayed_rpipes(wa);
		}
		break;
	case -ECONNRESET:	/* URB unlinked; no need to do anything */
	case -ENOENT:		/* as it was done by the who unlinked us */
		if (holding_dto) {
			__wa_dto_put(wa);
			wa_check_for_delayed_rpipes(wa);
		}
		break;
	default:		/* Other errors ... */
		dev_err(dev, "xfer 0x%08X#%u: data out error %d\n",
			wa_xfer_id(xfer), seg->index, urb->status);
		goto error_default;
	}

	/* taken when this URB was submitted. */
	wa_xfer_put(xfer);
	return;

error_dto_submit:
	/* taken on resubmit attempt. */
	wa_xfer_put(xfer);
error_default:
	spin_lock_irqsave(&xfer->lock, flags);
	rpipe = xfer->ep->hcpriv;
	if (edc_inc(&wa->nep_edc, EDC_MAX_ERRORS,
		    EDC_ERROR_TIMEFRAME)){
		dev_err(dev, "DTO: URB max acceptable errors exceeded, resetting device\n");
		wa_reset_all(wa);
	}
	if (seg->status != WA_SEG_ERROR) {
		seg->result = urb->status;
		__wa_xfer_abort(xfer);
		rpipe_ready = rpipe_avail_inc(rpipe);
		done = __wa_xfer_mark_seg_as_done(xfer, seg, WA_SEG_ERROR);
	}
	spin_unlock_irqrestore(&xfer->lock, flags);
	if (holding_dto) {
		__wa_dto_put(wa);
		wa_check_for_delayed_rpipes(wa);
	}
	if (done)
		wa_xfer_completion(xfer);
	if (rpipe_ready)
		wa_xfer_delayed_run(rpipe);
	/* taken when this URB was submitted. */
	wa_xfer_put(xfer);
}

/*
 * Callback for the isoc packet descriptor phase of the segment request
 *
 * Check wa_seg_tr_cb(); most comments also apply here because this
 * function does almost the same thing and they work closely
 * together.
 *
 * If the seg request has failed but this phase has succeeded,
 * wa_seg_tr_cb() has already failed the segment and moved the
 * status to WA_SEG_ERROR, so this will go through 'case 0' and
 * effectively do nothing.
 */
static void wa_seg_iso_pack_desc_cb(struct urb *urb)
{
	struct wa_seg *seg = urb->context;
	struct wa_xfer *xfer = seg->xfer;
	struct wahc *wa;
	struct device *dev;
	struct wa_rpipe *rpipe;
	unsigned long flags;
	unsigned rpipe_ready = 0;
	u8 done = 0;

	switch (urb->status) {
	case 0:
		spin_lock_irqsave(&xfer->lock, flags);
		wa = xfer->wa;
		dev = &wa->usb_iface->dev;
		dev_dbg(dev, "iso xfer %08X#%u: packet descriptor done\n",
			wa_xfer_id(xfer), seg->index);
		if (xfer->is_inbound && seg->status < WA_SEG_PENDING)
			seg->status = WA_SEG_PENDING;
		spin_unlock_irqrestore(&xfer->lock, flags);
		break;
	case -ECONNRESET:	/* URB unlinked; no need to do anything */
	case -ENOENT:		/* as it was done by the who unlinked us */
		break;
	default:		/* Other errors ... */
		spin_lock_irqsave(&xfer->lock, flags);
		wa = xfer->wa;
		dev = &wa->usb_iface->dev;
		rpipe = xfer->ep->hcpriv;
		pr_err_ratelimited("iso xfer %08X#%u: packet descriptor error %d\n",
				wa_xfer_id(xfer), seg->index, urb->status);
		if (edc_inc(&wa->nep_edc, EDC_MAX_ERRORS,
			    EDC_ERROR_TIMEFRAME)){
			dev_err(dev, "iso xfer: URB max acceptable errors exceeded, resetting device\n");
			wa_reset_all(wa);
		}
		if (seg->status != WA_SEG_ERROR) {
			usb_unlink_urb(seg->dto_urb);
			seg->result = urb->status;
			__wa_xfer_abort(xfer);
			rpipe_ready = rpipe_avail_inc(rpipe);
			done = __wa_xfer_mark_seg_as_done(xfer, seg,
					WA_SEG_ERROR);
		}
		spin_unlock_irqrestore(&xfer->lock, flags);
		if (done)
			wa_xfer_completion(xfer);
		if (rpipe_ready)
			wa_xfer_delayed_run(rpipe);
	}
	/* taken when this URB was submitted. */
	wa_xfer_put(xfer);
}

/*
 * Callback for the segment request
 *
 * If successful transition state (unless already transitioned or
 * outbound transfer); otherwise, take a note of the error, mark this
 * segment done and try completion.
 *
 * Note we don't access until we are sure that the transfer hasn't
 * been cancelled (ECONNRESET, ENOENT), which could mean that
 * seg->xfer could be already gone.
 *
 * We have to check before setting the status to WA_SEG_PENDING
 * because sometimes the xfer result callback arrives before this
 * callback (geeeeeeze), so it might happen that we are already in
 * another state. As well, we don't set it if the transfer is not inbound,
 * as in that case, wa_seg_dto_cb will do it when the OUT data phase
 * finishes.
 */
static void wa_seg_tr_cb(struct urb *urb)
{
	struct wa_seg *seg = urb->context;
	struct wa_xfer *xfer = seg->xfer;
	struct wahc *wa;
	struct device *dev;
	struct wa_rpipe *rpipe;
	unsigned long flags;
	unsigned rpipe_ready;
	u8 done = 0;

	switch (urb->status) {
	case 0:
		spin_lock_irqsave(&xfer->lock, flags);
		wa = xfer->wa;
		dev = &wa->usb_iface->dev;
		dev_dbg(dev, "xfer %p ID 0x%08X#%u: request done\n",
			xfer, wa_xfer_id(xfer), seg->index);
		if (xfer->is_inbound &&
			seg->status < WA_SEG_PENDING &&
			!(usb_pipeisoc(xfer->urb->pipe)))
			seg->status = WA_SEG_PENDING;
		spin_unlock_irqrestore(&xfer->lock, flags);
		break;
	case -ECONNRESET:	/* URB unlinked; no need to do anything */
	case -ENOENT:		/* as it was done by the who unlinked us */
		break;
	default:		/* Other errors ... */
		spin_lock_irqsave(&xfer->lock, flags);
		wa = xfer->wa;
		dev = &wa->usb_iface->dev;
		rpipe = xfer->ep->hcpriv;
		if (printk_ratelimit())
			dev_err(dev, "xfer %p ID 0x%08X#%u: request error %d\n",
				xfer, wa_xfer_id(xfer), seg->index,
				urb->status);
		if (edc_inc(&wa->nep_edc, EDC_MAX_ERRORS,
			    EDC_ERROR_TIMEFRAME)){
			dev_err(dev, "DTO: URB max acceptable errors "
				"exceeded, resetting device\n");
			wa_reset_all(wa);
		}
		usb_unlink_urb(seg->isoc_pack_desc_urb);
		usb_unlink_urb(seg->dto_urb);
		seg->result = urb->status;
		__wa_xfer_abort(xfer);
		rpipe_ready = rpipe_avail_inc(rpipe);
		done = __wa_xfer_mark_seg_as_done(xfer, seg, WA_SEG_ERROR);
		spin_unlock_irqrestore(&xfer->lock, flags);
		if (done)
			wa_xfer_completion(xfer);
		if (rpipe_ready)
			wa_xfer_delayed_run(rpipe);
	}
	/* taken when this URB was submitted. */
	wa_xfer_put(xfer);
}

/*
 * Allocate an SG list to store bytes_to_transfer bytes and copy the
 * subset of the in_sg that matches the buffer subset
 * we are about to transfer.
 */
static struct scatterlist *wa_xfer_create_subset_sg(struct scatterlist *in_sg,
	const unsigned int bytes_transferred,
	const unsigned int bytes_to_transfer, int *out_num_sgs)
{
	struct scatterlist *out_sg;
	unsigned int bytes_processed = 0, offset_into_current_page_data = 0,
		nents;
	struct scatterlist *current_xfer_sg = in_sg;
	struct scatterlist *current_seg_sg, *last_seg_sg;

	/* skip previously transferred pages. */
	while ((current_xfer_sg) &&
			(bytes_processed < bytes_transferred)) {
		bytes_processed += current_xfer_sg->length;

		/* advance the sg if current segment starts on or past the
			next page. */
		if (bytes_processed <= bytes_transferred)
			current_xfer_sg = sg_next(current_xfer_sg);
	}

	/* the data for the current segment starts in current_xfer_sg.
		calculate the offset. */
	if (bytes_processed > bytes_transferred) {
		offset_into_current_page_data = current_xfer_sg->length -
			(bytes_processed - bytes_transferred);
	}

	/* calculate the number of pages needed by this segment. */
	nents = DIV_ROUND_UP((bytes_to_transfer +
		offset_into_current_page_data +
		current_xfer_sg->offset),
		PAGE_SIZE);

	out_sg = kmalloc((sizeof(struct scatterlist) * nents), GFP_ATOMIC);
	if (out_sg) {
		sg_init_table(out_sg, nents);

		/* copy the portion of the incoming SG that correlates to the
		 * data to be transferred by this segment to the segment SG. */
		last_seg_sg = current_seg_sg = out_sg;
		bytes_processed = 0;

		/* reset nents and calculate the actual number of sg entries
			needed. */
		nents = 0;
		while ((bytes_processed < bytes_to_transfer) &&
				current_seg_sg && current_xfer_sg) {
			unsigned int page_len = min((current_xfer_sg->length -
				offset_into_current_page_data),
				(bytes_to_transfer - bytes_processed));

			sg_set_page(current_seg_sg, sg_page(current_xfer_sg),
				page_len,
				current_xfer_sg->offset +
				offset_into_current_page_data);

			bytes_processed += page_len;

			last_seg_sg = current_seg_sg;
			current_seg_sg = sg_next(current_seg_sg);
			current_xfer_sg = sg_next(current_xfer_sg);

			/* only the first page may require additional offset. */
			offset_into_current_page_data = 0;
			nents++;
		}

		/* update num_sgs and terminate the list since we may have
		 *  concatenated pages. */
		sg_mark_end(last_seg_sg);
		*out_num_sgs = nents;
	}

	return out_sg;
}

/*
 * Populate DMA buffer info for the isoc dto urb.
 */
static void __wa_populate_dto_urb_isoc(struct wa_xfer *xfer,
	struct wa_seg *seg, int curr_iso_frame)
{
	seg->dto_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	seg->dto_urb->sg = NULL;
	seg->dto_urb->num_sgs = 0;
	/* dto urb buffer address pulled from iso_frame_desc. */
	seg->dto_urb->transfer_dma = xfer->urb->transfer_dma +
		xfer->urb->iso_frame_desc[curr_iso_frame].offset;
	/* The Alereon HWA sends a single URB with all isoc segs. */
	if (xfer->wa->quirks & WUSB_QUIRK_ALEREON_HWA_CONCAT_ISOC)
		seg->dto_urb->transfer_buffer_length = seg->isoc_size;
	else
		seg->dto_urb->transfer_buffer_length =
			xfer->urb->iso_frame_desc[curr_iso_frame].length;
}

/*
 * Populate buffer ptr and size, DMA buffer or SG list for the dto urb.
 */
static int __wa_populate_dto_urb(struct wa_xfer *xfer,
	struct wa_seg *seg, size_t buf_itr_offset, size_t buf_itr_size)
{
	int result = 0;

	if (xfer->is_dma) {
		seg->dto_urb->transfer_dma =
			xfer->urb->transfer_dma + buf_itr_offset;
		seg->dto_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		seg->dto_urb->sg = NULL;
		seg->dto_urb->num_sgs = 0;
	} else {
		/* do buffer or SG processing. */
		seg->dto_urb->transfer_flags &=
			~URB_NO_TRANSFER_DMA_MAP;
		/* this should always be 0 before a resubmit. */
		seg->dto_urb->num_mapped_sgs = 0;

		if (xfer->urb->transfer_buffer) {
			seg->dto_urb->transfer_buffer =
				xfer->urb->transfer_buffer +
				buf_itr_offset;
			seg->dto_urb->sg = NULL;
			seg->dto_urb->num_sgs = 0;
		} else {
			seg->dto_urb->transfer_buffer = NULL;

			/*
			 * allocate an SG list to store seg_size bytes
			 * and copy the subset of the xfer->urb->sg that
			 * matches the buffer subset we are about to
			 * read.
			 */
			seg->dto_urb->sg = wa_xfer_create_subset_sg(
				xfer->urb->sg,
				buf_itr_offset, buf_itr_size,
				&(seg->dto_urb->num_sgs));
			if (!(seg->dto_urb->sg))
				result = -ENOMEM;
		}
	}
	seg->dto_urb->transfer_buffer_length = buf_itr_size;

	return result;
}

/*
 * Allocate the segs array and initialize each of them
 *
 * The segments are freed by wa_xfer_destroy() when the xfer use count
 * drops to zero; however, because each segment is given the same life
 * cycle as the USB URB it contains, it is actually freed by
 * usb_put_urb() on the contained USB URB (twisted, eh?).
 */
static int __wa_xfer_setup_segs(struct wa_xfer *xfer, size_t xfer_hdr_size)
{
	int result, cnt, isoc_frame_offset = 0;
	size_t alloc_size = sizeof(*xfer->seg[0])
		- sizeof(xfer->seg[0]->xfer_hdr) + xfer_hdr_size;
	struct usb_device *usb_dev = xfer->wa->usb_dev;
	const struct usb_endpoint_descriptor *dto_epd = xfer->wa->dto_epd;
	struct wa_seg *seg;
	size_t buf_itr, buf_size, buf_itr_size;

	result = -ENOMEM;
	xfer->seg = kcalloc(xfer->segs, sizeof(xfer->seg[0]), GFP_ATOMIC);
	if (xfer->seg == NULL)
		goto error_segs_kzalloc;
	buf_itr = 0;
	buf_size = xfer->urb->transfer_buffer_length;
	for (cnt = 0; cnt < xfer->segs; cnt++) {
		size_t iso_pkt_descr_size = 0;
		int seg_isoc_frame_count = 0, seg_isoc_size = 0;

		/*
		 * Adjust the size of the segment object to contain space for
		 * the isoc packet descriptor buffer.
		 */
		if (usb_pipeisoc(xfer->urb->pipe)) {
			seg_isoc_frame_count =
				__wa_seg_calculate_isoc_frame_count(xfer,
					isoc_frame_offset, &seg_isoc_size);

			iso_pkt_descr_size =
				sizeof(struct wa_xfer_packet_info_hwaiso) +
				(seg_isoc_frame_count * sizeof(__le16));
		}
		result = -ENOMEM;
		seg = xfer->seg[cnt] = kmalloc(alloc_size + iso_pkt_descr_size,
						GFP_ATOMIC);
		if (seg == NULL)
			goto error_seg_kmalloc;
		wa_seg_init(seg);
		seg->xfer = xfer;
		seg->index = cnt;
		usb_fill_bulk_urb(&seg->tr_urb, usb_dev,
				  usb_sndbulkpipe(usb_dev,
						  dto_epd->bEndpointAddress),
				  &seg->xfer_hdr, xfer_hdr_size,
				  wa_seg_tr_cb, seg);
		buf_itr_size = min(buf_size, xfer->seg_size);

		if (usb_pipeisoc(xfer->urb->pipe)) {
			seg->isoc_frame_count = seg_isoc_frame_count;
			seg->isoc_frame_offset = isoc_frame_offset;
			seg->isoc_size = seg_isoc_size;
			/* iso packet descriptor. */
			seg->isoc_pack_desc_urb =
					usb_alloc_urb(0, GFP_ATOMIC);
			if (seg->isoc_pack_desc_urb == NULL)
				goto error_iso_pack_desc_alloc;
			/*
			 * The buffer for the isoc packet descriptor starts
			 * after the transfer request header in the
			 * segment object memory buffer.
			 */
			usb_fill_bulk_urb(
				seg->isoc_pack_desc_urb, usb_dev,
				usb_sndbulkpipe(usb_dev,
					dto_epd->bEndpointAddress),
				(void *)(&seg->xfer_hdr) +
					xfer_hdr_size,
				iso_pkt_descr_size,
				wa_seg_iso_pack_desc_cb, seg);

			/* adjust starting frame offset for next seg. */
			isoc_frame_offset += seg_isoc_frame_count;
		}

		if (xfer->is_inbound == 0 && buf_size > 0) {
			/* outbound data. */
			seg->dto_urb = usb_alloc_urb(0, GFP_ATOMIC);
			if (seg->dto_urb == NULL)
				goto error_dto_alloc;
			usb_fill_bulk_urb(
				seg->dto_urb, usb_dev,
				usb_sndbulkpipe(usb_dev,
						dto_epd->bEndpointAddress),
				NULL, 0, wa_seg_dto_cb, seg);

			if (usb_pipeisoc(xfer->urb->pipe)) {
				/*
				 * Fill in the xfer buffer information for the
				 * first isoc frame.  Subsequent frames in this
				 * segment will be filled in and sent from the
				 * DTO completion routine, if needed.
				 */
				__wa_populate_dto_urb_isoc(xfer, seg,
					seg->isoc_frame_offset);
			} else {
				/* fill in the xfer buffer information. */
				result = __wa_populate_dto_urb(xfer, seg,
							buf_itr, buf_itr_size);
				if (result < 0)
					goto error_seg_outbound_populate;

				buf_itr += buf_itr_size;
				buf_size -= buf_itr_size;
			}
		}
		seg->status = WA_SEG_READY;
	}
	return 0;

	/*
	 * Free the memory for the current segment which failed to init.
	 * Use the fact that cnt is left at were it failed.  The remaining
	 * segments will be cleaned up by wa_xfer_destroy.
	 */
error_seg_outbound_populate:
	usb_free_urb(xfer->seg[cnt]->dto_urb);
error_dto_alloc:
	usb_free_urb(xfer->seg[cnt]->isoc_pack_desc_urb);
error_iso_pack_desc_alloc:
	kfree(xfer->seg[cnt]);
	xfer->seg[cnt] = NULL;
error_seg_kmalloc:
error_segs_kzalloc:
	return result;
}

/*
 * Allocates all the stuff needed to submit a transfer
 *
 * Breaks the whole data buffer in a list of segments, each one has a
 * structure allocated to it and linked in xfer->seg[index]
 *
 * FIXME: merge setup_segs() and the last part of this function, no
 *        need to do two for loops when we could run everything in a
 *        single one
 */
static int __wa_xfer_setup(struct wa_xfer *xfer, struct urb *urb)
{
	int result;
	struct device *dev = &xfer->wa->usb_iface->dev;
	enum wa_xfer_type xfer_type = 0; /* shut up GCC */
	size_t xfer_hdr_size, cnt, transfer_size;
	struct wa_xfer_hdr *xfer_hdr0, *xfer_hdr;

	result = __wa_xfer_setup_sizes(xfer, &xfer_type);
	if (result < 0)
		goto error_setup_sizes;
	xfer_hdr_size = result;
	result = __wa_xfer_setup_segs(xfer, xfer_hdr_size);
	if (result < 0) {
		dev_err(dev, "xfer %p: Failed to allocate %d segments: %d\n",
			xfer, xfer->segs, result);
		goto error_setup_segs;
	}
	/* Fill the first header */
	xfer_hdr0 = &xfer->seg[0]->xfer_hdr;
	wa_xfer_id_init(xfer);
	__wa_xfer_setup_hdr0(xfer, xfer_hdr0, xfer_type, xfer_hdr_size);

	/* Fill remaining headers */
	xfer_hdr = xfer_hdr0;
	if (xfer_type == WA_XFER_TYPE_ISO) {
		xfer_hdr0->dwTransferLength =
			cpu_to_le32(xfer->seg[0]->isoc_size);
		for (cnt = 1; cnt < xfer->segs; cnt++) {
			struct wa_xfer_packet_info_hwaiso *packet_desc;
			struct wa_seg *seg = xfer->seg[cnt];
			struct wa_xfer_hwaiso *xfer_iso;

			xfer_hdr = &seg->xfer_hdr;
			xfer_iso = container_of(xfer_hdr,
						struct wa_xfer_hwaiso, hdr);
			packet_desc = ((void *)xfer_hdr) + xfer_hdr_size;
			/*
			 * Copy values from the 0th header. Segment specific
			 * values are set below.
			 */
			memcpy(xfer_hdr, xfer_hdr0, xfer_hdr_size);
			xfer_hdr->bTransferSegment = cnt;
			xfer_hdr->dwTransferLength =
				cpu_to_le32(seg->isoc_size);
			xfer_iso->dwNumOfPackets =
					cpu_to_le32(seg->isoc_frame_count);
			__wa_setup_isoc_packet_descr(packet_desc, xfer, seg);
			seg->status = WA_SEG_READY;
		}
	} else {
		transfer_size = urb->transfer_buffer_length;
		xfer_hdr0->dwTransferLength = transfer_size > xfer->seg_size ?
			cpu_to_le32(xfer->seg_size) :
			cpu_to_le32(transfer_size);
		transfer_size -=  xfer->seg_size;
		for (cnt = 1; cnt < xfer->segs; cnt++) {
			xfer_hdr = &xfer->seg[cnt]->xfer_hdr;
			memcpy(xfer_hdr, xfer_hdr0, xfer_hdr_size);
			xfer_hdr->bTransferSegment = cnt;
			xfer_hdr->dwTransferLength =
				transfer_size > xfer->seg_size ?
					cpu_to_le32(xfer->seg_size)
					: cpu_to_le32(transfer_size);
			xfer->seg[cnt]->status = WA_SEG_READY;
			transfer_size -=  xfer->seg_size;
		}
	}
	xfer_hdr->bTransferSegment |= 0x80;	/* this is the last segment */
	result = 0;
error_setup_segs:
error_setup_sizes:
	return result;
}

/*
 *
 *
 * rpipe->seg_lock is held!
 */
static int __wa_seg_submit(struct wa_rpipe *rpipe, struct wa_xfer *xfer,
			   struct wa_seg *seg, int *dto_done)
{
	int result;

	/* default to done unless we encounter a multi-frame isoc segment. */
	*dto_done = 1;

	/*
	 * Take a ref for each segment urb so the xfer cannot disappear until
	 * all of the callbacks run.
	 */
	wa_xfer_get(xfer);
	/* submit the transfer request. */
	seg->status = WA_SEG_SUBMITTED;
	result = usb_submit_urb(&seg->tr_urb, GFP_ATOMIC);
	if (result < 0) {
		pr_err("%s: xfer %p#%u: REQ submit failed: %d\n",
		       __func__, xfer, seg->index, result);
		wa_xfer_put(xfer);
		goto error_tr_submit;
	}
	/* submit the isoc packet descriptor if present. */
	if (seg->isoc_pack_desc_urb) {
		wa_xfer_get(xfer);
		result = usb_submit_urb(seg->isoc_pack_desc_urb, GFP_ATOMIC);
		seg->isoc_frame_index = 0;
		if (result < 0) {
			pr_err("%s: xfer %p#%u: ISO packet descriptor submit failed: %d\n",
			       __func__, xfer, seg->index, result);
			wa_xfer_put(xfer);
			goto error_iso_pack_desc_submit;
		}
	}
	/* submit the out data if this is an out request. */
	if (seg->dto_urb) {
		struct wahc *wa = xfer->wa;
		wa_xfer_get(xfer);
		result = usb_submit_urb(seg->dto_urb, GFP_ATOMIC);
		if (result < 0) {
			pr_err("%s: xfer %p#%u: DTO submit failed: %d\n",
			       __func__, xfer, seg->index, result);
			wa_xfer_put(xfer);
			goto error_dto_submit;
		}
		/*
		 * If this segment contains more than one isoc frame, hold
		 * onto the dto resource until we send all frames.
		 * Only applies to non-Alereon devices.
		 */
		if (((wa->quirks & WUSB_QUIRK_ALEREON_HWA_CONCAT_ISOC) == 0)
			&& (seg->isoc_frame_count > 1))
			*dto_done = 0;
	}
	rpipe_avail_dec(rpipe);
	return 0;

error_dto_submit:
	usb_unlink_urb(seg->isoc_pack_desc_urb);
error_iso_pack_desc_submit:
	usb_unlink_urb(&seg->tr_urb);
error_tr_submit:
	seg->status = WA_SEG_ERROR;
	seg->result = result;
	*dto_done = 1;
	return result;
}

/*
 * Execute more queued request segments until the maximum concurrent allowed.
 * Return true if the DTO resource was acquired and released.
 *
 * The ugly unlock/lock sequence on the error path is needed as the
 * xfer->lock normally nests the seg_lock and not viceversa.
 */
static int __wa_xfer_delayed_run(struct wa_rpipe *rpipe, int *dto_waiting)
{
	int result, dto_acquired = 0, dto_done = 0;
	struct device *dev = &rpipe->wa->usb_iface->dev;
	struct wa_seg *seg;
	struct wa_xfer *xfer;
	unsigned long flags;

	*dto_waiting = 0;

	spin_lock_irqsave(&rpipe->seg_lock, flags);
	while (atomic_read(&rpipe->segs_available) > 0
	      && !list_empty(&rpipe->seg_list)
	      && (dto_acquired = __wa_dto_try_get(rpipe->wa))) {
		seg = list_first_entry(&(rpipe->seg_list), struct wa_seg,
				 list_node);
		list_del(&seg->list_node);
		xfer = seg->xfer;
		/*
		 * Get a reference to the xfer in case the callbacks for the
		 * URBs submitted by __wa_seg_submit attempt to complete
		 * the xfer before this function completes.
		 */
		wa_xfer_get(xfer);
		result = __wa_seg_submit(rpipe, xfer, seg, &dto_done);
		/* release the dto resource if this RPIPE is done with it. */
		if (dto_done)
			__wa_dto_put(rpipe->wa);
		dev_dbg(dev, "xfer %p ID %08X#%u submitted from delayed [%d segments available] %d\n",
			xfer, wa_xfer_id(xfer), seg->index,
			atomic_read(&rpipe->segs_available), result);
		if (unlikely(result < 0)) {
			int done;

			spin_unlock_irqrestore(&rpipe->seg_lock, flags);
			spin_lock_irqsave(&xfer->lock, flags);
			__wa_xfer_abort(xfer);
			/*
			 * This seg was marked as submitted when it was put on
			 * the RPIPE seg_list.  Mark it done.
			 */
			xfer->segs_done++;
			done = __wa_xfer_is_done(xfer);
			spin_unlock_irqrestore(&xfer->lock, flags);
			if (done)
				wa_xfer_completion(xfer);
			spin_lock_irqsave(&rpipe->seg_lock, flags);
		}
		wa_xfer_put(xfer);
	}
	/*
	 * Mark this RPIPE as waiting if dto was not acquired, there are
	 * delayed segs and no active transfers to wake us up later.
	 */
	if (!dto_acquired && !list_empty(&rpipe->seg_list)
		&& (atomic_read(&rpipe->segs_available) ==
			le16_to_cpu(rpipe->descr.wRequests)))
		*dto_waiting = 1;

	spin_unlock_irqrestore(&rpipe->seg_lock, flags);

	return dto_done;
}

static void wa_xfer_delayed_run(struct wa_rpipe *rpipe)
{
	int dto_waiting;
	int dto_done = __wa_xfer_delayed_run(rpipe, &dto_waiting);

	/*
	 * If this RPIPE is waiting on the DTO resource, add it to the tail of
	 * the waiting list.
	 * Otherwise, if the WA DTO resource was acquired and released by
	 *  __wa_xfer_delayed_run, another RPIPE may have attempted to acquire
	 * DTO and failed during that time.  Check the delayed list and process
	 * any waiters.  Start searching from the next RPIPE index.
	 */
	if (dto_waiting)
		wa_add_delayed_rpipe(rpipe->wa, rpipe);
	else if (dto_done)
		wa_check_for_delayed_rpipes(rpipe->wa);
}

/*
 *
 * xfer->lock is taken
 *
 * On failure submitting we just stop submitting and return error;
 * wa_urb_enqueue_b() will execute the completion path
 */
static int __wa_xfer_submit(struct wa_xfer *xfer)
{
	int result, dto_acquired = 0, dto_done = 0, dto_waiting = 0;
	struct wahc *wa = xfer->wa;
	struct device *dev = &wa->usb_iface->dev;
	unsigned cnt;
	struct wa_seg *seg;
	unsigned long flags;
	struct wa_rpipe *rpipe = xfer->ep->hcpriv;
	size_t maxrequests = le16_to_cpu(rpipe->descr.wRequests);
	u8 available;
	u8 empty;

	spin_lock_irqsave(&wa->xfer_list_lock, flags);
	list_add_tail(&xfer->list_node, &wa->xfer_list);
	spin_unlock_irqrestore(&wa->xfer_list_lock, flags);

	BUG_ON(atomic_read(&rpipe->segs_available) > maxrequests);
	result = 0;
	spin_lock_irqsave(&rpipe->seg_lock, flags);
	for (cnt = 0; cnt < xfer->segs; cnt++) {
		int delay_seg = 1;

		available = atomic_read(&rpipe->segs_available);
		empty = list_empty(&rpipe->seg_list);
		seg = xfer->seg[cnt];
		if (available && empty) {
			/*
			 * Only attempt to acquire DTO if we have a segment
			 * to send.
			 */
			dto_acquired = __wa_dto_try_get(rpipe->wa);
			if (dto_acquired) {
				delay_seg = 0;
				result = __wa_seg_submit(rpipe, xfer, seg,
							&dto_done);
				dev_dbg(dev, "xfer %p ID 0x%08X#%u: available %u empty %u submitted\n",
					xfer, wa_xfer_id(xfer), cnt, available,
					empty);
				if (dto_done)
					__wa_dto_put(rpipe->wa);

				if (result < 0) {
					__wa_xfer_abort(xfer);
					goto error_seg_submit;
				}
			}
		}

		if (delay_seg) {
			dev_dbg(dev, "xfer %p ID 0x%08X#%u: available %u empty %u delayed\n",
				xfer, wa_xfer_id(xfer), cnt, available,  empty);
			seg->status = WA_SEG_DELAYED;
			list_add_tail(&seg->list_node, &rpipe->seg_list);
		}
		xfer->segs_submitted++;
	}
error_seg_submit:
	/*
	 * Mark this RPIPE as waiting if dto was not acquired, there are
	 * delayed segs and no active transfers to wake us up later.
	 */
	if (!dto_acquired && !list_empty(&rpipe->seg_list)
		&& (atomic_read(&rpipe->segs_available) ==
			le16_to_cpu(rpipe->descr.wRequests)))
		dto_waiting = 1;
	spin_unlock_irqrestore(&rpipe->seg_lock, flags);

	if (dto_waiting)
		wa_add_delayed_rpipe(rpipe->wa, rpipe);
	else if (dto_done)
		wa_check_for_delayed_rpipes(rpipe->wa);

	return result;
}

/*
 * Second part of a URB/transfer enqueuement
 *
 * Assumes this comes from wa_urb_enqueue() [maybe through
 * wa_urb_enqueue_run()]. At this point:
 *
 * xfer->wa	filled and refcounted
 * xfer->ep	filled with rpipe refcounted if
 *              delayed == 0
 * xfer->urb 	filled and refcounted (this is the case when called
 *              from wa_urb_enqueue() as we come from usb_submit_urb()
 *              and when called by wa_urb_enqueue_run(), as we took an
 *              extra ref dropped by _run() after we return).
 * xfer->gfp	filled
 *
 * If we fail at __wa_xfer_submit(), then we just check if we are done
 * and if so, we run the completion procedure. However, if we are not
 * yet done, we do nothing and wait for the completion handlers from
 * the submitted URBs or from the xfer-result path to kick in. If xfer
 * result never kicks in, the xfer will timeout from the USB code and
 * dequeue() will be called.
 */
static int wa_urb_enqueue_b(struct wa_xfer *xfer)
{
	int result;
	unsigned long flags;
	struct urb *urb = xfer->urb;
	struct wahc *wa = xfer->wa;
	struct wusbhc *wusbhc = wa->wusb;
	struct wusb_dev *wusb_dev;
	unsigned done;

	result = rpipe_get_by_ep(wa, xfer->ep, urb, xfer->gfp);
	if (result < 0) {
		pr_err("%s: error_rpipe_get\n", __func__);
		goto error_rpipe_get;
	}
	result = -ENODEV;
	/* FIXME: segmentation broken -- kills DWA */
	mutex_lock(&wusbhc->mutex);		/* get a WUSB dev */
	if (urb->dev == NULL) {
		mutex_unlock(&wusbhc->mutex);
		pr_err("%s: error usb dev gone\n", __func__);
		goto error_dev_gone;
	}
	wusb_dev = __wusb_dev_get_by_usb_dev(wusbhc, urb->dev);
	if (wusb_dev == NULL) {
		mutex_unlock(&wusbhc->mutex);
		dev_err(&(urb->dev->dev), "%s: error wusb dev gone\n",
			__func__);
		goto error_dev_gone;
	}
	mutex_unlock(&wusbhc->mutex);

	spin_lock_irqsave(&xfer->lock, flags);
	xfer->wusb_dev = wusb_dev;
	result = urb->status;
	if (urb->status != -EINPROGRESS) {
		dev_err(&(urb->dev->dev), "%s: error_dequeued\n", __func__);
		goto error_dequeued;
	}

	result = __wa_xfer_setup(xfer, urb);
	if (result < 0) {
		dev_err(&(urb->dev->dev), "%s: error_xfer_setup\n", __func__);
		goto error_xfer_setup;
	}
	/*
	 * Get a xfer reference since __wa_xfer_submit starts asynchronous
	 * operations that may try to complete the xfer before this function
	 * exits.
	 */
	wa_xfer_get(xfer);
	result = __wa_xfer_submit(xfer);
	if (result < 0) {
		dev_err(&(urb->dev->dev), "%s: error_xfer_submit\n", __func__);
		goto error_xfer_submit;
	}
	spin_unlock_irqrestore(&xfer->lock, flags);
	wa_xfer_put(xfer);
	return 0;

	/*
	 * this is basically wa_xfer_completion() broken up wa_xfer_giveback()
	 * does a wa_xfer_put() that will call wa_xfer_destroy() and undo
	 * setup().
	 */
error_xfer_setup:
error_dequeued:
	spin_unlock_irqrestore(&xfer->lock, flags);
	/* FIXME: segmentation broken, kills DWA */
	if (wusb_dev)
		wusb_dev_put(wusb_dev);
error_dev_gone:
	rpipe_put(xfer->ep->hcpriv);
error_rpipe_get:
	xfer->result = result;
	return result;

error_xfer_submit:
	done = __wa_xfer_is_done(xfer);
	xfer->result = result;
	spin_unlock_irqrestore(&xfer->lock, flags);
	if (done)
		wa_xfer_completion(xfer);
	wa_xfer_put(xfer);
	/* return success since the completion routine will run. */
	return 0;
}

/*
 * Execute the delayed transfers in the Wire Adapter @wa
 *
 * We need to be careful here, as dequeue() could be called in the
 * middle.  That's why we do the whole thing under the
 * wa->xfer_list_lock. If dequeue() jumps in, it first locks xfer->lock
 * and then checks the list -- so as we would be acquiring in inverse
 * order, we move the delayed list to a separate list while locked and then
 * submit them without the list lock held.
 */
void wa_urb_enqueue_run(struct work_struct *ws)
{
	struct wahc *wa = container_of(ws, struct wahc, xfer_enqueue_work);
	struct wa_xfer *xfer, *next;
	struct urb *urb;
	LIST_HEAD(tmp_list);

	/* Create a copy of the wa->xfer_delayed_list while holding the lock */
	spin_lock_irq(&wa->xfer_list_lock);
	list_cut_position(&tmp_list, &wa->xfer_delayed_list,
			wa->xfer_delayed_list.prev);
	spin_unlock_irq(&wa->xfer_list_lock);

	/*
	 * enqueue from temp list without list lock held since wa_urb_enqueue_b
	 * can take xfer->lock as well as lock mutexes.
	 */
	list_for_each_entry_safe(xfer, next, &tmp_list, list_node) {
		list_del_init(&xfer->list_node);

		urb = xfer->urb;
		if (wa_urb_enqueue_b(xfer) < 0)
			wa_xfer_giveback(xfer);
		usb_put_urb(urb);	/* taken when queuing */
	}
}
EXPORT_SYMBOL_GPL(wa_urb_enqueue_run);

/*
 * Process the errored transfers on the Wire Adapter outside of interrupt.
 */
void wa_process_errored_transfers_run(struct work_struct *ws)
{
	struct wahc *wa = container_of(ws, struct wahc, xfer_error_work);
	struct wa_xfer *xfer, *next;
	LIST_HEAD(tmp_list);

	pr_info("%s: Run delayed STALL processing.\n", __func__);

	/* Create a copy of the wa->xfer_errored_list while holding the lock */
	spin_lock_irq(&wa->xfer_list_lock);
	list_cut_position(&tmp_list, &wa->xfer_errored_list,
			wa->xfer_errored_list.prev);
	spin_unlock_irq(&wa->xfer_list_lock);

	/*
	 * run rpipe_clear_feature_stalled from temp list without list lock
	 * held.
	 */
	list_for_each_entry_safe(xfer, next, &tmp_list, list_node) {
		struct usb_host_endpoint *ep;
		unsigned long flags;
		struct wa_rpipe *rpipe;

		spin_lock_irqsave(&xfer->lock, flags);
		ep = xfer->ep;
		rpipe = ep->hcpriv;
		spin_unlock_irqrestore(&xfer->lock, flags);

		/* clear RPIPE feature stalled without holding a lock. */
		rpipe_clear_feature_stalled(wa, ep);

		/* complete the xfer. This removes it from the tmp list. */
		wa_xfer_completion(xfer);

		/* check for work. */
		wa_xfer_delayed_run(rpipe);
	}
}
EXPORT_SYMBOL_GPL(wa_process_errored_transfers_run);

/*
 * Submit a transfer to the Wire Adapter in a delayed way
 *
 * The process of enqueuing involves possible sleeps() [see
 * enqueue_b(), for the rpipe_get() and the mutex_lock()]. If we are
 * in an atomic section, we defer the enqueue_b() call--else we call direct.
 *
 * @urb: We own a reference to it done by the HCI Linux USB stack that
 *       will be given up by calling usb_hcd_giveback_urb() or by
 *       returning error from this function -> ergo we don't have to
 *       refcount it.
 */
int wa_urb_enqueue(struct wahc *wa, struct usb_host_endpoint *ep,
		   struct urb *urb, gfp_t gfp)
{
	int result;
	struct device *dev = &wa->usb_iface->dev;
	struct wa_xfer *xfer;
	unsigned long my_flags;
	unsigned cant_sleep = irqs_disabled() | in_atomic();

	if ((urb->transfer_buffer == NULL)
	    && (urb->sg == NULL)
	    && !(urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP)
	    && urb->transfer_buffer_length != 0) {
		dev_err(dev, "BUG? urb %p: NULL xfer buffer & NODMA\n", urb);
		dump_stack();
	}

	spin_lock_irqsave(&wa->xfer_list_lock, my_flags);
	result = usb_hcd_link_urb_to_ep(&(wa->wusb->usb_hcd), urb);
	spin_unlock_irqrestore(&wa->xfer_list_lock, my_flags);
	if (result < 0)
		goto error_link_urb;

	result = -ENOMEM;
	xfer = kzalloc(sizeof(*xfer), gfp);
	if (xfer == NULL)
		goto error_kmalloc;

	result = -ENOENT;
	if (urb->status != -EINPROGRESS)	/* cancelled */
		goto error_dequeued;		/* before starting? */
	wa_xfer_init(xfer);
	xfer->wa = wa_get(wa);
	xfer->urb = urb;
	xfer->gfp = gfp;
	xfer->ep = ep;
	urb->hcpriv = xfer;

	dev_dbg(dev, "xfer %p urb %p pipe 0x%02x [%d bytes] %s %s %s\n",
		xfer, urb, urb->pipe, urb->transfer_buffer_length,
		urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP ? "dma" : "nodma",
		urb->pipe & USB_DIR_IN ? "inbound" : "outbound",
		cant_sleep ? "deferred" : "inline");

	if (cant_sleep) {
		usb_get_urb(urb);
		spin_lock_irqsave(&wa->xfer_list_lock, my_flags);
		list_add_tail(&xfer->list_node, &wa->xfer_delayed_list);
		spin_unlock_irqrestore(&wa->xfer_list_lock, my_flags);
		queue_work(wusbd, &wa->xfer_enqueue_work);
	} else {
		result = wa_urb_enqueue_b(xfer);
		if (result < 0) {
			/*
			 * URB submit/enqueue failed.  Clean up, return an
			 * error and do not run the callback.  This avoids
			 * an infinite submit/complete loop.
			 */
			dev_err(dev, "%s: URB enqueue failed: %d\n",
			   __func__, result);
			wa_put(xfer->wa);
			wa_xfer_put(xfer);
			spin_lock_irqsave(&wa->xfer_list_lock, my_flags);
			usb_hcd_unlink_urb_from_ep(&(wa->wusb->usb_hcd), urb);
			spin_unlock_irqrestore(&wa->xfer_list_lock, my_flags);
			return result;
		}
	}
	return 0;

error_dequeued:
	kfree(xfer);
error_kmalloc:
	spin_lock_irqsave(&wa->xfer_list_lock, my_flags);
	usb_hcd_unlink_urb_from_ep(&(wa->wusb->usb_hcd), urb);
	spin_unlock_irqrestore(&wa->xfer_list_lock, my_flags);
error_link_urb:
	return result;
}
EXPORT_SYMBOL_GPL(wa_urb_enqueue);

/*
 * Dequeue a URB and make sure uwb_hcd_giveback_urb() [completion
 * handler] is called.
 *
 * Until a transfer goes successfully through wa_urb_enqueue() it
 * needs to be dequeued with completion calling; when stuck in delayed
 * or before wa_xfer_setup() is called, we need to do completion.
 *
 *  not setup  If there is no hcpriv yet, that means that that enqueue
 *             still had no time to set the xfer up. Because
 *             urb->status should be other than -EINPROGRESS,
 *             enqueue() will catch that and bail out.
 *
 * If the transfer has gone through setup, we just need to clean it
 * up. If it has gone through submit(), we have to abort it [with an
 * asynch request] and then make sure we cancel each segment.
 *
 */
int wa_urb_dequeue(struct wahc *wa, struct urb *urb, int status)
{
	unsigned long flags;
	struct wa_xfer *xfer;
	struct wa_seg *seg;
	struct wa_rpipe *rpipe;
	unsigned cnt, done = 0, xfer_abort_pending;
	unsigned rpipe_ready = 0;
	int result;

	/* check if it is safe to unlink. */
	spin_lock_irqsave(&wa->xfer_list_lock, flags);
	result = usb_hcd_check_unlink_urb(&(wa->wusb->usb_hcd), urb, status);
	if ((result == 0) && urb->hcpriv) {
		/*
		 * Get a xfer ref to prevent a race with wa_xfer_giveback
		 * cleaning up the xfer while we are working with it.
		 */
		wa_xfer_get(urb->hcpriv);
	}
	spin_unlock_irqrestore(&wa->xfer_list_lock, flags);
	if (result)
		return result;

	xfer = urb->hcpriv;
	if (xfer == NULL)
		return -ENOENT;
	spin_lock_irqsave(&xfer->lock, flags);
	pr_debug("%s: DEQUEUE xfer id 0x%08X\n", __func__, wa_xfer_id(xfer));
	rpipe = xfer->ep->hcpriv;
	if (rpipe == NULL) {
		pr_debug("%s: xfer %p id 0x%08X has no RPIPE.  %s",
			__func__, xfer, wa_xfer_id(xfer),
			"Probably already aborted.\n" );
		result = -ENOENT;
		goto out_unlock;
	}
	/*
	 * Check for done to avoid racing with wa_xfer_giveback and completing
	 * twice.
	 */
	if (__wa_xfer_is_done(xfer)) {
		pr_debug("%s: xfer %p id 0x%08X already done.\n", __func__,
			xfer, wa_xfer_id(xfer));
		result = -ENOENT;
		goto out_unlock;
	}
	/* Check the delayed list -> if there, release and complete */
	spin_lock(&wa->xfer_list_lock);
	if (!list_empty(&xfer->list_node) && xfer->seg == NULL)
		goto dequeue_delayed;
	spin_unlock(&wa->xfer_list_lock);
	if (xfer->seg == NULL)  	/* still hasn't reached */
		goto out_unlock;	/* setup(), enqueue_b() completes */
	/* Ok, the xfer is in flight already, it's been setup and submitted.*/
	xfer_abort_pending = __wa_xfer_abort(xfer) >= 0;
	/*
	 * grab the rpipe->seg_lock here to prevent racing with
	 * __wa_xfer_delayed_run.
	 */
	spin_lock(&rpipe->seg_lock);
	for (cnt = 0; cnt < xfer->segs; cnt++) {
		seg = xfer->seg[cnt];
		pr_debug("%s: xfer id 0x%08X#%d status = %d\n",
			__func__, wa_xfer_id(xfer), cnt, seg->status);
		switch (seg->status) {
		case WA_SEG_NOTREADY:
		case WA_SEG_READY:
			printk(KERN_ERR "xfer %p#%u: dequeue bad state %u\n",
			       xfer, cnt, seg->status);
			WARN_ON(1);
			break;
		case WA_SEG_DELAYED:
			/*
			 * delete from rpipe delayed list.  If no segments on
			 * this xfer have been submitted, __wa_xfer_is_done will
			 * trigger a giveback below.  Otherwise, the submitted
			 * segments will be completed in the DTI interrupt.
			 */
			seg->status = WA_SEG_ABORTED;
			seg->result = -ENOENT;
			list_del(&seg->list_node);
			xfer->segs_done++;
			break;
		case WA_SEG_DONE:
		case WA_SEG_ERROR:
		case WA_SEG_ABORTED:
			break;
			/*
			 * The buf_in data for a segment in the
			 * WA_SEG_DTI_PENDING state is actively being read.
			 * Let wa_buf_in_cb handle it since it will be called
			 * and will increment xfer->segs_done.  Cleaning up
			 * here could cause wa_buf_in_cb to access the xfer
			 * after it has been completed/freed.
			 */
		case WA_SEG_DTI_PENDING:
			break;
			/*
			 * In the states below, the HWA device already knows
			 * about the transfer.  If an abort request was sent,
			 * allow the HWA to process it and wait for the
			 * results.  Otherwise, the DTI state and seg completed
			 * counts can get out of sync.
			 */
		case WA_SEG_SUBMITTED:
		case WA_SEG_PENDING:
			/*
			 * Check if the abort was successfully sent.  This could
			 * be false if the HWA has been removed but we haven't
			 * gotten the disconnect notification yet.
			 */
			if (!xfer_abort_pending) {
				seg->status = WA_SEG_ABORTED;
				rpipe_ready = rpipe_avail_inc(rpipe);
				xfer->segs_done++;
			}
			break;
		}
	}
	spin_unlock(&rpipe->seg_lock);
	xfer->result = urb->status;	/* -ENOENT or -ECONNRESET */
	done = __wa_xfer_is_done(xfer);
	spin_unlock_irqrestore(&xfer->lock, flags);
	if (done)
		wa_xfer_completion(xfer);
	if (rpipe_ready)
		wa_xfer_delayed_run(rpipe);
	wa_xfer_put(xfer);
	return result;

out_unlock:
	spin_unlock_irqrestore(&xfer->lock, flags);
	wa_xfer_put(xfer);
	return result;

dequeue_delayed:
	list_del_init(&xfer->list_node);
	spin_unlock(&wa->xfer_list_lock);
	xfer->result = urb->status;
	spin_unlock_irqrestore(&xfer->lock, flags);
	wa_xfer_giveback(xfer);
	wa_xfer_put(xfer);
	usb_put_urb(urb);		/* we got a ref in enqueue() */
	return 0;
}
EXPORT_SYMBOL_GPL(wa_urb_dequeue);

/*
 * Translation from WA status codes (WUSB1.0 Table 8.15) to errno
 * codes
 *
 * Positive errno values are internal inconsistencies and should be
 * flagged louder. Negative are to be passed up to the user in the
 * normal way.
 *
 * @status: USB WA status code -- high two bits are stripped.
 */
static int wa_xfer_status_to_errno(u8 status)
{
	int errno;
	u8 real_status = status;
	static int xlat[] = {
		[WA_XFER_STATUS_SUCCESS] = 		0,
		[WA_XFER_STATUS_HALTED] = 		-EPIPE,
		[WA_XFER_STATUS_DATA_BUFFER_ERROR] = 	-ENOBUFS,
		[WA_XFER_STATUS_BABBLE] = 		-EOVERFLOW,
		[WA_XFER_RESERVED] = 			EINVAL,
		[WA_XFER_STATUS_NOT_FOUND] =		0,
		[WA_XFER_STATUS_INSUFFICIENT_RESOURCE] = -ENOMEM,
		[WA_XFER_STATUS_TRANSACTION_ERROR] = 	-EILSEQ,
		[WA_XFER_STATUS_ABORTED] =		-ENOENT,
		[WA_XFER_STATUS_RPIPE_NOT_READY] = 	EINVAL,
		[WA_XFER_INVALID_FORMAT] = 		EINVAL,
		[WA_XFER_UNEXPECTED_SEGMENT_NUMBER] = 	EINVAL,
		[WA_XFER_STATUS_RPIPE_TYPE_MISMATCH] = 	EINVAL,
	};
	status &= 0x3f;

	if (status == 0)
		return 0;
	if (status >= ARRAY_SIZE(xlat)) {
		printk_ratelimited(KERN_ERR "%s(): BUG? "
			       "Unknown WA transfer status 0x%02x\n",
			       __func__, real_status);
		return -EINVAL;
	}
	errno = xlat[status];
	if (unlikely(errno > 0)) {
		printk_ratelimited(KERN_ERR "%s(): BUG? "
			       "Inconsistent WA status: 0x%02x\n",
			       __func__, real_status);
		errno = -errno;
	}
	return errno;
}

/*
 * If a last segment flag and/or a transfer result error is encountered,
 * no other segment transfer results will be returned from the device.
 * Mark the remaining submitted or pending xfers as completed so that
 * the xfer will complete cleanly.
 *
 * xfer->lock must be held
 *
 */
static void wa_complete_remaining_xfer_segs(struct wa_xfer *xfer,
		int starting_index, enum wa_seg_status status)
{
	int index;
	struct wa_rpipe *rpipe = xfer->ep->hcpriv;

	for (index = starting_index; index < xfer->segs_submitted; index++) {
		struct wa_seg *current_seg = xfer->seg[index];

		BUG_ON(current_seg == NULL);

		switch (current_seg->status) {
		case WA_SEG_SUBMITTED:
		case WA_SEG_PENDING:
		case WA_SEG_DTI_PENDING:
			rpipe_avail_inc(rpipe);
		/*
		 * do not increment RPIPE avail for the WA_SEG_DELAYED case
		 * since it has not been submitted to the RPIPE.
		 */
		/* fall through */
		case WA_SEG_DELAYED:
			xfer->segs_done++;
			current_seg->status = status;
			break;
		case WA_SEG_ABORTED:
			break;
		default:
			WARN(1, "%s: xfer 0x%08X#%d. bad seg status = %d\n",
				__func__, wa_xfer_id(xfer), index,
				current_seg->status);
			break;
		}
	}
}

/* Populate the given urb based on the current isoc transfer state. */
static int __wa_populate_buf_in_urb_isoc(struct wahc *wa,
	struct urb *buf_in_urb, struct wa_xfer *xfer, struct wa_seg *seg)
{
	int urb_start_frame = seg->isoc_frame_index + seg->isoc_frame_offset;
	int seg_index, total_len = 0, urb_frame_index = urb_start_frame;
	struct usb_iso_packet_descriptor *iso_frame_desc =
						xfer->urb->iso_frame_desc;
	const int dti_packet_size = usb_endpoint_maxp(wa->dti_epd);
	int next_frame_contiguous;
	struct usb_iso_packet_descriptor *iso_frame;

	BUG_ON(buf_in_urb->status == -EINPROGRESS);

	/*
	 * If the current frame actual_length is contiguous with the next frame
	 * and actual_length is a multiple of the DTI endpoint max packet size,
	 * combine the current frame with the next frame in a single URB.  This
	 * reduces the number of URBs that must be submitted in that case.
	 */
	seg_index = seg->isoc_frame_index;
	do {
		next_frame_contiguous = 0;

		iso_frame = &iso_frame_desc[urb_frame_index];
		total_len += iso_frame->actual_length;
		++urb_frame_index;
		++seg_index;

		if (seg_index < seg->isoc_frame_count) {
			struct usb_iso_packet_descriptor *next_iso_frame;

			next_iso_frame = &iso_frame_desc[urb_frame_index];

			if ((iso_frame->offset + iso_frame->actual_length) ==
				next_iso_frame->offset)
				next_frame_contiguous = 1;
		}
	} while (next_frame_contiguous
			&& ((iso_frame->actual_length % dti_packet_size) == 0));

	/* this should always be 0 before a resubmit. */
	buf_in_urb->num_mapped_sgs	= 0;
	buf_in_urb->transfer_dma = xfer->urb->transfer_dma +
		iso_frame_desc[urb_start_frame].offset;
	buf_in_urb->transfer_buffer_length = total_len;
	buf_in_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	buf_in_urb->transfer_buffer = NULL;
	buf_in_urb->sg = NULL;
	buf_in_urb->num_sgs = 0;
	buf_in_urb->context = seg;

	/* return the number of frames included in this URB. */
	return seg_index - seg->isoc_frame_index;
}

/* Populate the given urb based on the current transfer state. */
static int wa_populate_buf_in_urb(struct urb *buf_in_urb, struct wa_xfer *xfer,
	unsigned int seg_idx, unsigned int bytes_transferred)
{
	int result = 0;
	struct wa_seg *seg = xfer->seg[seg_idx];

	BUG_ON(buf_in_urb->status == -EINPROGRESS);
	/* this should always be 0 before a resubmit. */
	buf_in_urb->num_mapped_sgs	= 0;

	if (xfer->is_dma) {
		buf_in_urb->transfer_dma = xfer->urb->transfer_dma
			+ (seg_idx * xfer->seg_size);
		buf_in_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		buf_in_urb->transfer_buffer = NULL;
		buf_in_urb->sg = NULL;
		buf_in_urb->num_sgs = 0;
	} else {
		/* do buffer or SG processing. */
		buf_in_urb->transfer_flags &= ~URB_NO_TRANSFER_DMA_MAP;

		if (xfer->urb->transfer_buffer) {
			buf_in_urb->transfer_buffer =
				xfer->urb->transfer_buffer
				+ (seg_idx * xfer->seg_size);
			buf_in_urb->sg = NULL;
			buf_in_urb->num_sgs = 0;
		} else {
			/* allocate an SG list to store seg_size bytes
				and copy the subset of the xfer->urb->sg
				that matches the buffer subset we are
				about to read. */
			buf_in_urb->sg = wa_xfer_create_subset_sg(
				xfer->urb->sg,
				seg_idx * xfer->seg_size,
				bytes_transferred,
				&(buf_in_urb->num_sgs));

			if (!(buf_in_urb->sg)) {
				buf_in_urb->num_sgs	= 0;
				result = -ENOMEM;
			}
			buf_in_urb->transfer_buffer = NULL;
		}
	}
	buf_in_urb->transfer_buffer_length = bytes_transferred;
	buf_in_urb->context = seg;

	return result;
}

/*
 * Process a xfer result completion message
 *
 * inbound transfers: need to schedule a buf_in_urb read
 *
 * FIXME: this function needs to be broken up in parts
 */
static void wa_xfer_result_chew(struct wahc *wa, struct wa_xfer *xfer,
		struct wa_xfer_result *xfer_result)
{
	int result;
	struct device *dev = &wa->usb_iface->dev;
	unsigned long flags;
	unsigned int seg_idx;
	struct wa_seg *seg;
	struct wa_rpipe *rpipe;
	unsigned done = 0;
	u8 usb_status;
	unsigned rpipe_ready = 0;
	unsigned bytes_transferred = le32_to_cpu(xfer_result->dwTransferLength);
	struct urb *buf_in_urb = &(wa->buf_in_urbs[0]);

	spin_lock_irqsave(&xfer->lock, flags);
	seg_idx = xfer_result->bTransferSegment & 0x7f;
	if (unlikely(seg_idx >= xfer->segs))
		goto error_bad_seg;
	seg = xfer->seg[seg_idx];
	rpipe = xfer->ep->hcpriv;
	usb_status = xfer_result->bTransferStatus;
	dev_dbg(dev, "xfer %p ID 0x%08X#%u: bTransferStatus 0x%02x (seg status %u)\n",
		xfer, wa_xfer_id(xfer), seg_idx, usb_status, seg->status);
	if (seg->status == WA_SEG_ABORTED
	    || seg->status == WA_SEG_ERROR)	/* already handled */
		goto segment_aborted;
	if (seg->status == WA_SEG_SUBMITTED)	/* ops, got here */
		seg->status = WA_SEG_PENDING;	/* before wa_seg{_dto}_cb() */
	if (seg->status != WA_SEG_PENDING) {
		if (printk_ratelimit())
			dev_err(dev, "xfer %p#%u: Bad segment state %u\n",
				xfer, seg_idx, seg->status);
		seg->status = WA_SEG_PENDING;	/* workaround/"fix" it */
	}
	if (usb_status & 0x80) {
		seg->result = wa_xfer_status_to_errno(usb_status);
		dev_err(dev, "DTI: xfer %p 0x%08X:#%u failed (0x%02x)\n",
			xfer, xfer->id, seg->index, usb_status);
		seg->status = ((usb_status & 0x7F) == WA_XFER_STATUS_ABORTED) ?
			WA_SEG_ABORTED : WA_SEG_ERROR;
		goto error_complete;
	}
	/* FIXME: we ignore warnings, tally them for stats */
	if (usb_status & 0x40) 		/* Warning?... */
		usb_status = 0;		/* ... pass */
	/*
	 * If the last segment bit is set, complete the remaining segments.
	 * When the current segment is completed, either in wa_buf_in_cb for
	 * transfers with data or below for no data, the xfer will complete.
	 */
	if (xfer_result->bTransferSegment & 0x80)
		wa_complete_remaining_xfer_segs(xfer, seg->index + 1,
			WA_SEG_DONE);
	if (usb_pipeisoc(xfer->urb->pipe)
		&& (le32_to_cpu(xfer_result->dwNumOfPackets) > 0)) {
		/* set up WA state to read the isoc packet status next. */
		wa->dti_isoc_xfer_in_progress = wa_xfer_id(xfer);
		wa->dti_isoc_xfer_seg = seg_idx;
		wa->dti_state = WA_DTI_ISOC_PACKET_STATUS_PENDING;
	} else if (xfer->is_inbound && !usb_pipeisoc(xfer->urb->pipe)
			&& (bytes_transferred > 0)) {
		/* IN data phase: read to buffer */
		seg->status = WA_SEG_DTI_PENDING;
		result = wa_populate_buf_in_urb(buf_in_urb, xfer, seg_idx,
			bytes_transferred);
		if (result < 0)
			goto error_buf_in_populate;
		++(wa->active_buf_in_urbs);
		result = usb_submit_urb(buf_in_urb, GFP_ATOMIC);
		if (result < 0) {
			--(wa->active_buf_in_urbs);
			goto error_submit_buf_in;
		}
	} else {
		/* OUT data phase or no data, complete it -- */
		seg->result = bytes_transferred;
		rpipe_ready = rpipe_avail_inc(rpipe);
		done = __wa_xfer_mark_seg_as_done(xfer, seg, WA_SEG_DONE);
	}
	spin_unlock_irqrestore(&xfer->lock, flags);
	if (done)
		wa_xfer_completion(xfer);
	if (rpipe_ready)
		wa_xfer_delayed_run(rpipe);
	return;

error_submit_buf_in:
	if (edc_inc(&wa->dti_edc, EDC_MAX_ERRORS, EDC_ERROR_TIMEFRAME)) {
		dev_err(dev, "DTI: URB max acceptable errors "
			"exceeded, resetting device\n");
		wa_reset_all(wa);
	}
	if (printk_ratelimit())
		dev_err(dev, "xfer %p#%u: can't submit DTI data phase: %d\n",
			xfer, seg_idx, result);
	seg->result = result;
	kfree(buf_in_urb->sg);
	buf_in_urb->sg = NULL;
error_buf_in_populate:
	__wa_xfer_abort(xfer);
	seg->status = WA_SEG_ERROR;
error_complete:
	xfer->segs_done++;
	rpipe_ready = rpipe_avail_inc(rpipe);
	wa_complete_remaining_xfer_segs(xfer, seg->index + 1, seg->status);
	done = __wa_xfer_is_done(xfer);
	/*
	 * queue work item to clear STALL for control endpoints.
	 * Otherwise, let endpoint_reset take care of it.
	 */
	if (((usb_status & 0x3f) == WA_XFER_STATUS_HALTED) &&
		usb_endpoint_xfer_control(&xfer->ep->desc) &&
		done) {

		dev_info(dev, "Control EP stall.  Queue delayed work.\n");
		spin_lock(&wa->xfer_list_lock);
		/* move xfer from xfer_list to xfer_errored_list. */
		list_move_tail(&xfer->list_node, &wa->xfer_errored_list);
		spin_unlock(&wa->xfer_list_lock);
		spin_unlock_irqrestore(&xfer->lock, flags);
		queue_work(wusbd, &wa->xfer_error_work);
	} else {
		spin_unlock_irqrestore(&xfer->lock, flags);
		if (done)
			wa_xfer_completion(xfer);
		if (rpipe_ready)
			wa_xfer_delayed_run(rpipe);
	}

	return;

error_bad_seg:
	spin_unlock_irqrestore(&xfer->lock, flags);
	wa_urb_dequeue(wa, xfer->urb, -ENOENT);
	if (printk_ratelimit())
		dev_err(dev, "xfer %p#%u: bad segment\n", xfer, seg_idx);
	if (edc_inc(&wa->dti_edc, EDC_MAX_ERRORS, EDC_ERROR_TIMEFRAME)) {
		dev_err(dev, "DTI: URB max acceptable errors "
			"exceeded, resetting device\n");
		wa_reset_all(wa);
	}
	return;

segment_aborted:
	/* nothing to do, as the aborter did the completion */
	spin_unlock_irqrestore(&xfer->lock, flags);
}

/*
 * Process a isochronous packet status message
 *
 * inbound transfers: need to schedule a buf_in_urb read
 */
static int wa_process_iso_packet_status(struct wahc *wa, struct urb *urb)
{
	struct device *dev = &wa->usb_iface->dev;
	struct wa_xfer_packet_status_hwaiso *packet_status;
	struct wa_xfer_packet_status_len_hwaiso *status_array;
	struct wa_xfer *xfer;
	unsigned long flags;
	struct wa_seg *seg;
	struct wa_rpipe *rpipe;
	unsigned done = 0, dti_busy = 0, data_frame_count = 0, seg_index;
	unsigned first_frame_index = 0, rpipe_ready = 0;
	size_t expected_size;

	/* We have a xfer result buffer; check it */
	dev_dbg(dev, "DTI: isoc packet status %d bytes at %p\n",
		urb->actual_length, urb->transfer_buffer);
	packet_status = (struct wa_xfer_packet_status_hwaiso *)(wa->dti_buf);
	if (packet_status->bPacketType != WA_XFER_ISO_PACKET_STATUS) {
		dev_err(dev, "DTI Error: isoc packet status--bad type 0x%02x\n",
			packet_status->bPacketType);
		goto error_parse_buffer;
	}
	xfer = wa_xfer_get_by_id(wa, wa->dti_isoc_xfer_in_progress);
	if (xfer == NULL) {
		dev_err(dev, "DTI Error: isoc packet status--unknown xfer 0x%08x\n",
			wa->dti_isoc_xfer_in_progress);
		goto error_parse_buffer;
	}
	spin_lock_irqsave(&xfer->lock, flags);
	if (unlikely(wa->dti_isoc_xfer_seg >= xfer->segs))
		goto error_bad_seg;
	seg = xfer->seg[wa->dti_isoc_xfer_seg];
	rpipe = xfer->ep->hcpriv;
	expected_size = struct_size(packet_status, PacketStatus,
				    seg->isoc_frame_count);
	if (urb->actual_length != expected_size) {
		dev_err(dev, "DTI Error: isoc packet status--bad urb length (%d bytes vs %zu needed)\n",
			urb->actual_length, expected_size);
		goto error_bad_seg;
	}
	if (le16_to_cpu(packet_status->wLength) != expected_size) {
		dev_err(dev, "DTI Error: isoc packet status--bad length %u\n",
			le16_to_cpu(packet_status->wLength));
		goto error_bad_seg;
	}
	/* write isoc packet status and lengths back to the xfer urb. */
	status_array = packet_status->PacketStatus;
	xfer->urb->start_frame =
		wa->wusb->usb_hcd.driver->get_frame_number(&wa->wusb->usb_hcd);
	for (seg_index = 0; seg_index < seg->isoc_frame_count; ++seg_index) {
		struct usb_iso_packet_descriptor *iso_frame_desc =
			xfer->urb->iso_frame_desc;
		const int xfer_frame_index =
			seg->isoc_frame_offset + seg_index;

		iso_frame_desc[xfer_frame_index].status =
			wa_xfer_status_to_errno(
			le16_to_cpu(status_array[seg_index].PacketStatus));
		iso_frame_desc[xfer_frame_index].actual_length =
			le16_to_cpu(status_array[seg_index].PacketLength);
		/* track the number of frames successfully transferred. */
		if (iso_frame_desc[xfer_frame_index].actual_length > 0) {
			/* save the starting frame index for buf_in_urb. */
			if (!data_frame_count)
				first_frame_index = seg_index;
			++data_frame_count;
		}
	}

	if (xfer->is_inbound && data_frame_count) {
		int result, total_frames_read = 0, urb_index = 0;
		struct urb *buf_in_urb;

		/* IN data phase: read to buffer */
		seg->status = WA_SEG_DTI_PENDING;

		/* start with the first frame with data. */
		seg->isoc_frame_index = first_frame_index;
		/* submit up to WA_MAX_BUF_IN_URBS read URBs. */
		do {
			int urb_frame_index, urb_frame_count;
			struct usb_iso_packet_descriptor *iso_frame_desc;

			buf_in_urb = &(wa->buf_in_urbs[urb_index]);
			urb_frame_count = __wa_populate_buf_in_urb_isoc(wa,
				buf_in_urb, xfer, seg);
			/* advance frame index to start of next read URB. */
			seg->isoc_frame_index += urb_frame_count;
			total_frames_read += urb_frame_count;

			++(wa->active_buf_in_urbs);
			result = usb_submit_urb(buf_in_urb, GFP_ATOMIC);

			/* skip 0-byte frames. */
			urb_frame_index =
				seg->isoc_frame_offset + seg->isoc_frame_index;
			iso_frame_desc =
				&(xfer->urb->iso_frame_desc[urb_frame_index]);
			while ((seg->isoc_frame_index <
						seg->isoc_frame_count) &&
				 (iso_frame_desc->actual_length == 0)) {
				++(seg->isoc_frame_index);
				++iso_frame_desc;
			}
			++urb_index;

		} while ((result == 0) && (urb_index < WA_MAX_BUF_IN_URBS)
				&& (seg->isoc_frame_index <
						seg->isoc_frame_count));

		if (result < 0) {
			--(wa->active_buf_in_urbs);
			dev_err(dev, "DTI Error: Could not submit buf in URB (%d)",
				result);
			wa_reset_all(wa);
		} else if (data_frame_count > total_frames_read)
			/* If we need to read more frames, set DTI busy. */
			dti_busy = 1;
	} else {
		/* OUT transfer or no more IN data, complete it -- */
		rpipe_ready = rpipe_avail_inc(rpipe);
		done = __wa_xfer_mark_seg_as_done(xfer, seg, WA_SEG_DONE);
	}
	spin_unlock_irqrestore(&xfer->lock, flags);
	if (dti_busy)
		wa->dti_state = WA_DTI_BUF_IN_DATA_PENDING;
	else
		wa->dti_state = WA_DTI_TRANSFER_RESULT_PENDING;
	if (done)
		wa_xfer_completion(xfer);
	if (rpipe_ready)
		wa_xfer_delayed_run(rpipe);
	wa_xfer_put(xfer);
	return dti_busy;

error_bad_seg:
	spin_unlock_irqrestore(&xfer->lock, flags);
	wa_xfer_put(xfer);
error_parse_buffer:
	return dti_busy;
}

/*
 * Callback for the IN data phase
 *
 * If successful transition state; otherwise, take a note of the
 * error, mark this segment done and try completion.
 *
 * Note we don't access until we are sure that the transfer hasn't
 * been cancelled (ECONNRESET, ENOENT), which could mean that
 * seg->xfer could be already gone.
 */
static void wa_buf_in_cb(struct urb *urb)
{
	struct wa_seg *seg = urb->context;
	struct wa_xfer *xfer = seg->xfer;
	struct wahc *wa;
	struct device *dev;
	struct wa_rpipe *rpipe;
	unsigned rpipe_ready = 0, isoc_data_frame_count = 0;
	unsigned long flags;
	int resubmit_dti = 0, active_buf_in_urbs;
	u8 done = 0;

	/* free the sg if it was used. */
	kfree(urb->sg);
	urb->sg = NULL;

	spin_lock_irqsave(&xfer->lock, flags);
	wa = xfer->wa;
	dev = &wa->usb_iface->dev;
	--(wa->active_buf_in_urbs);
	active_buf_in_urbs = wa->active_buf_in_urbs;
	rpipe = xfer->ep->hcpriv;

	if (usb_pipeisoc(xfer->urb->pipe)) {
		struct usb_iso_packet_descriptor *iso_frame_desc =
			xfer->urb->iso_frame_desc;
		int	seg_index;

		/*
		 * Find the next isoc frame with data and count how many
		 * frames with data remain.
		 */
		seg_index = seg->isoc_frame_index;
		while (seg_index < seg->isoc_frame_count) {
			const int urb_frame_index =
				seg->isoc_frame_offset + seg_index;

			if (iso_frame_desc[urb_frame_index].actual_length > 0) {
				/* save the index of the next frame with data */
				if (!isoc_data_frame_count)
					seg->isoc_frame_index = seg_index;
				++isoc_data_frame_count;
			}
			++seg_index;
		}
	}
	spin_unlock_irqrestore(&xfer->lock, flags);

	switch (urb->status) {
	case 0:
		spin_lock_irqsave(&xfer->lock, flags);

		seg->result += urb->actual_length;
		if (isoc_data_frame_count > 0) {
			int result, urb_frame_count;

			/* submit a read URB for the next frame with data. */
			urb_frame_count = __wa_populate_buf_in_urb_isoc(wa, urb,
				 xfer, seg);
			/* advance index to start of next read URB. */
			seg->isoc_frame_index += urb_frame_count;
			++(wa->active_buf_in_urbs);
			result = usb_submit_urb(urb, GFP_ATOMIC);
			if (result < 0) {
				--(wa->active_buf_in_urbs);
				dev_err(dev, "DTI Error: Could not submit buf in URB (%d)",
					result);
				wa_reset_all(wa);
			}
			/*
			 * If we are in this callback and
			 * isoc_data_frame_count > 0, it means that the dti_urb
			 * submission was delayed in wa_dti_cb.  Once
			 * we submit the last buf_in_urb, we can submit the
			 * delayed dti_urb.
			 */
			  resubmit_dti = (isoc_data_frame_count ==
							urb_frame_count);
		} else if (active_buf_in_urbs == 0) {
			dev_dbg(dev,
				"xfer %p 0x%08X#%u: data in done (%zu bytes)\n",
				xfer, wa_xfer_id(xfer), seg->index,
				seg->result);
			rpipe_ready = rpipe_avail_inc(rpipe);
			done = __wa_xfer_mark_seg_as_done(xfer, seg,
					WA_SEG_DONE);
		}
		spin_unlock_irqrestore(&xfer->lock, flags);
		if (done)
			wa_xfer_completion(xfer);
		if (rpipe_ready)
			wa_xfer_delayed_run(rpipe);
		break;
	case -ECONNRESET:	/* URB unlinked; no need to do anything */
	case -ENOENT:		/* as it was done by the who unlinked us */
		break;
	default:		/* Other errors ... */
		/*
		 * Error on data buf read.  Only resubmit DTI if it hasn't
		 * already been done by previously hitting this error or by a
		 * successful completion of the previous buf_in_urb.
		 */
		resubmit_dti = wa->dti_state != WA_DTI_TRANSFER_RESULT_PENDING;
		spin_lock_irqsave(&xfer->lock, flags);
		if (printk_ratelimit())
			dev_err(dev, "xfer %p 0x%08X#%u: data in error %d\n",
				xfer, wa_xfer_id(xfer), seg->index,
				urb->status);
		if (edc_inc(&wa->nep_edc, EDC_MAX_ERRORS,
			    EDC_ERROR_TIMEFRAME)){
			dev_err(dev, "DTO: URB max acceptable errors "
				"exceeded, resetting device\n");
			wa_reset_all(wa);
		}
		seg->result = urb->status;
		rpipe_ready = rpipe_avail_inc(rpipe);
		if (active_buf_in_urbs == 0)
			done = __wa_xfer_mark_seg_as_done(xfer, seg,
				WA_SEG_ERROR);
		else
			__wa_xfer_abort(xfer);
		spin_unlock_irqrestore(&xfer->lock, flags);
		if (done)
			wa_xfer_completion(xfer);
		if (rpipe_ready)
			wa_xfer_delayed_run(rpipe);
	}

	if (resubmit_dti) {
		int result;

		wa->dti_state = WA_DTI_TRANSFER_RESULT_PENDING;

		result = usb_submit_urb(wa->dti_urb, GFP_ATOMIC);
		if (result < 0) {
			dev_err(dev, "DTI Error: Could not submit DTI URB (%d)\n",
				result);
			wa_reset_all(wa);
		}
	}
}

/*
 * Handle an incoming transfer result buffer
 *
 * Given a transfer result buffer, it completes the transfer (possibly
 * scheduling and buffer in read) and then resubmits the DTI URB for a
 * new transfer result read.
 *
 *
 * The xfer_result DTI URB state machine
 *
 * States: OFF | RXR (Read-Xfer-Result) | RBI (Read-Buffer-In)
 *
 * We start in OFF mode, the first xfer_result notification [through
 * wa_handle_notif_xfer()] moves us to RXR by posting the DTI-URB to
 * read.
 *
 * We receive a buffer -- if it is not a xfer_result, we complain and
 * repost the DTI-URB. If it is a xfer_result then do the xfer seg
 * request accounting. If it is an IN segment, we move to RBI and post
 * a BUF-IN-URB to the right buffer. The BUF-IN-URB callback will
 * repost the DTI-URB and move to RXR state. if there was no IN
 * segment, it will repost the DTI-URB.
 *
 * We go back to OFF when we detect a ENOENT or ESHUTDOWN (or too many
 * errors) in the URBs.
 */
static void wa_dti_cb(struct urb *urb)
{
	int result, dti_busy = 0;
	struct wahc *wa = urb->context;
	struct device *dev = &wa->usb_iface->dev;
	u32 xfer_id;
	u8 usb_status;

	BUG_ON(wa->dti_urb != urb);
	switch (wa->dti_urb->status) {
	case 0:
		if (wa->dti_state == WA_DTI_TRANSFER_RESULT_PENDING) {
			struct wa_xfer_result *xfer_result;
			struct wa_xfer *xfer;

			/* We have a xfer result buffer; check it */
			dev_dbg(dev, "DTI: xfer result %d bytes at %p\n",
				urb->actual_length, urb->transfer_buffer);
			if (urb->actual_length != sizeof(*xfer_result)) {
				dev_err(dev, "DTI Error: xfer result--bad size xfer result (%d bytes vs %zu needed)\n",
					urb->actual_length,
					sizeof(*xfer_result));
				break;
			}
			xfer_result = (struct wa_xfer_result *)(wa->dti_buf);
			if (xfer_result->hdr.bLength != sizeof(*xfer_result)) {
				dev_err(dev, "DTI Error: xfer result--bad header length %u\n",
					xfer_result->hdr.bLength);
				break;
			}
			if (xfer_result->hdr.bNotifyType != WA_XFER_RESULT) {
				dev_err(dev, "DTI Error: xfer result--bad header type 0x%02x\n",
					xfer_result->hdr.bNotifyType);
				break;
			}
			xfer_id = le32_to_cpu(xfer_result->dwTransferID);
			usb_status = xfer_result->bTransferStatus & 0x3f;
			if (usb_status == WA_XFER_STATUS_NOT_FOUND) {
				/* taken care of already */
				dev_dbg(dev, "%s: xfer 0x%08X#%u not found.\n",
					__func__, xfer_id,
					xfer_result->bTransferSegment & 0x7f);
				break;
			}
			xfer = wa_xfer_get_by_id(wa, xfer_id);
			if (xfer == NULL) {
				/* FIXME: transaction not found. */
				dev_err(dev, "DTI Error: xfer result--unknown xfer 0x%08x (status 0x%02x)\n",
					xfer_id, usb_status);
				break;
			}
			wa_xfer_result_chew(wa, xfer, xfer_result);
			wa_xfer_put(xfer);
		} else if (wa->dti_state == WA_DTI_ISOC_PACKET_STATUS_PENDING) {
			dti_busy = wa_process_iso_packet_status(wa, urb);
		} else {
			dev_err(dev, "DTI Error: unexpected EP state = %d\n",
				wa->dti_state);
		}
		break;
	case -ENOENT:		/* (we killed the URB)...so, no broadcast */
	case -ESHUTDOWN:	/* going away! */
		dev_dbg(dev, "DTI: going down! %d\n", urb->status);
		goto out;
	default:
		/* Unknown error */
		if (edc_inc(&wa->dti_edc, EDC_MAX_ERRORS,
			    EDC_ERROR_TIMEFRAME)) {
			dev_err(dev, "DTI: URB max acceptable errors "
				"exceeded, resetting device\n");
			wa_reset_all(wa);
			goto out;
		}
		if (printk_ratelimit())
			dev_err(dev, "DTI: URB error %d\n", urb->status);
		break;
	}

	/* Resubmit the DTI URB if we are not busy processing isoc in frames. */
	if (!dti_busy) {
		result = usb_submit_urb(wa->dti_urb, GFP_ATOMIC);
		if (result < 0) {
			dev_err(dev, "DTI Error: Could not submit DTI URB (%d)\n",
				result);
			wa_reset_all(wa);
		}
	}
out:
	return;
}

/*
 * Initialize the DTI URB for reading transfer result notifications and also
 * the buffer-in URB, for reading buffers. Then we just submit the DTI URB.
 */
int wa_dti_start(struct wahc *wa)
{
	const struct usb_endpoint_descriptor *dti_epd = wa->dti_epd;
	struct device *dev = &wa->usb_iface->dev;
	int result = -ENOMEM, index;

	if (wa->dti_urb != NULL)	/* DTI URB already started */
		goto out;

	wa->dti_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (wa->dti_urb == NULL)
		goto error_dti_urb_alloc;
	usb_fill_bulk_urb(
		wa->dti_urb, wa->usb_dev,
		usb_rcvbulkpipe(wa->usb_dev, 0x80 | dti_epd->bEndpointAddress),
		wa->dti_buf, wa->dti_buf_size,
		wa_dti_cb, wa);

	/* init the buf in URBs */
	for (index = 0; index < WA_MAX_BUF_IN_URBS; ++index) {
		usb_fill_bulk_urb(
			&(wa->buf_in_urbs[index]), wa->usb_dev,
			usb_rcvbulkpipe(wa->usb_dev,
				0x80 | dti_epd->bEndpointAddress),
			NULL, 0, wa_buf_in_cb, wa);
	}
	result = usb_submit_urb(wa->dti_urb, GFP_KERNEL);
	if (result < 0) {
		dev_err(dev, "DTI Error: Could not submit DTI URB (%d) resetting\n",
			result);
		goto error_dti_urb_submit;
	}
out:
	return 0;

error_dti_urb_submit:
	usb_put_urb(wa->dti_urb);
	wa->dti_urb = NULL;
error_dti_urb_alloc:
	return result;
}
EXPORT_SYMBOL_GPL(wa_dti_start);
/*
 * Transfer complete notification
 *
 * Called from the notif.c code. We get a notification on EP2 saying
 * that some endpoint has some transfer result data available. We are
 * about to read it.
 *
 * To speed up things, we always have a URB reading the DTI URB; we
 * don't really set it up and start it until the first xfer complete
 * notification arrives, which is what we do here.
 *
 * Follow up in wa_dti_cb(), as that's where the whole state
 * machine starts.
 *
 * @wa shall be referenced
 */
void wa_handle_notif_xfer(struct wahc *wa, struct wa_notif_hdr *notif_hdr)
{
	struct device *dev = &wa->usb_iface->dev;
	struct wa_notif_xfer *notif_xfer;
	const struct usb_endpoint_descriptor *dti_epd = wa->dti_epd;

	notif_xfer = container_of(notif_hdr, struct wa_notif_xfer, hdr);
	BUG_ON(notif_hdr->bNotifyType != WA_NOTIF_TRANSFER);

	if ((0x80 | notif_xfer->bEndpoint) != dti_epd->bEndpointAddress) {
		/* FIXME: hardcoded limitation, adapt */
		dev_err(dev, "BUG: DTI ep is %u, not %u (hack me)\n",
			notif_xfer->bEndpoint, dti_epd->bEndpointAddress);
		goto error;
	}

	/* attempt to start the DTI ep processing. */
	if (wa_dti_start(wa) < 0)
		goto error;

	return;

error:
	wa_reset_all(wa);
}
