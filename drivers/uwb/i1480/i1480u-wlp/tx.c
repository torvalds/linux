/*
 * WUSB Wire Adapter: WLP interface
 * Deal with TX (massaging data to transmit, handling it)
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
 * Transmission engine. Get an skb, create from that a WLP transmit
 * context, add a WLP TX header (which we keep prefilled in the
 * device's instance), fill out the target-specific fields and
 * fire it.
 *
 * ROADMAP:
 *
 *   Entry points:
 *
 *     i1480u_tx_release(): called by i1480u_disconnect() to release
 *                          pending tx contexts.
 *
 *     i1480u_tx_cb(): callback for TX contexts (USB URBs)
 *       i1480u_tx_destroy():
 *
 *     i1480u_tx_timeout(): called for timeout handling from the
 *                          network stack.
 *
 *     i1480u_hard_start_xmit(): called for transmitting an skb from
 *                               the network stack. Will interact with WLP
 *                               substack to verify and prepare frame.
 *       i1480u_xmit_frame(): actual transmission on hardware
 *
 *         i1480u_tx_create()	    Creates TX context
 *            i1480u_tx_create_1()    For packets in 1 fragment
 *            i1480u_tx_create_n()    For packets in >1 fragments
 *
 * TODO:
 *
 * - FIXME: rewrite using usb_sg_*(), add asynch support to
 *          usb_sg_*(). It might not make too much sense as most of
 *          the times the MTU will be smaller than one page...
 */

#include "i1480u-wlp.h"
#define D_LOCAL 5
#include <linux/uwb/debug.h>

enum {
	/* This is only for Next and Last TX packets */
	i1480u_MAX_PL_SIZE = i1480u_MAX_FRG_SIZE
		- sizeof(struct untd_hdr_rst),
};

/** Free resources allocated to a i1480u tx context. */
static
void i1480u_tx_free(struct i1480u_tx *wtx)
{
	kfree(wtx->buf);
	if (wtx->skb)
		dev_kfree_skb_irq(wtx->skb);
	usb_free_urb(wtx->urb);
	kfree(wtx);
}

static
void i1480u_tx_destroy(struct i1480u *i1480u, struct i1480u_tx *wtx)
{
	unsigned long flags;
	spin_lock_irqsave(&i1480u->tx_list_lock, flags);	/* not active any more */
	list_del(&wtx->list_node);
	i1480u_tx_free(wtx);
	spin_unlock_irqrestore(&i1480u->tx_list_lock, flags);
}

static
void i1480u_tx_unlink_urbs(struct i1480u *i1480u)
{
	unsigned long flags;
	struct i1480u_tx *wtx, *next;

	spin_lock_irqsave(&i1480u->tx_list_lock, flags);
	list_for_each_entry_safe(wtx, next, &i1480u->tx_list, list_node) {
		usb_unlink_urb(wtx->urb);
	}
	spin_unlock_irqrestore(&i1480u->tx_list_lock, flags);
}


/**
 * Callback for a completed tx USB URB.
 *
 * TODO:
 *
 * - FIXME: recover errors more gracefully
 * - FIXME: handle NAKs (I dont think they come here) for flow ctl
 */
static
void i1480u_tx_cb(struct urb *urb)
{
	struct i1480u_tx *wtx = urb->context;
	struct i1480u *i1480u = wtx->i1480u;
	struct net_device *net_dev = i1480u->net_dev;
	struct device *dev = &i1480u->usb_iface->dev;
	unsigned long flags;

	switch (urb->status) {
	case 0:
		spin_lock_irqsave(&i1480u->lock, flags);
		i1480u->stats.tx_packets++;
		i1480u->stats.tx_bytes += urb->actual_length;
		spin_unlock_irqrestore(&i1480u->lock, flags);
		break;
	case -ECONNRESET:	/* Not an error, but a controlled situation; */
	case -ENOENT:		/* (we killed the URB)...so, no broadcast */
		dev_dbg(dev, "notif endp: reset/noent %d\n", urb->status);
		netif_stop_queue(net_dev);
		break;
	case -ESHUTDOWN:	/* going away! */
		dev_dbg(dev, "notif endp: down %d\n", urb->status);
		netif_stop_queue(net_dev);
		break;
	default:
		dev_err(dev, "TX: unknown URB status %d\n", urb->status);
		if (edc_inc(&i1480u->tx_errors, EDC_MAX_ERRORS,
					EDC_ERROR_TIMEFRAME)) {
			dev_err(dev, "TX: max acceptable errors exceeded."
					"Reset device.\n");
			netif_stop_queue(net_dev);
			i1480u_tx_unlink_urbs(i1480u);
			wlp_reset_all(&i1480u->wlp);
		}
		break;
	}
	i1480u_tx_destroy(i1480u, wtx);
	if (atomic_dec_return(&i1480u->tx_inflight.count)
	    <= i1480u->tx_inflight.threshold
	    && netif_queue_stopped(net_dev)
	    && i1480u->tx_inflight.threshold != 0) {
		if (d_test(2) && printk_ratelimit())
			d_printf(2, dev, "Restart queue. \n");
		netif_start_queue(net_dev);
		atomic_inc(&i1480u->tx_inflight.restart_count);
	}
	return;
}


/**
 * Given a buffer that doesn't fit in a single fragment, create an
 * scatter/gather structure for delivery to the USB pipe.
 *
 * Implements functionality of i1480u_tx_create().
 *
 * @wtx:	tx descriptor
 * @skb:	skb to send
 * @gfp_mask:	gfp allocation mask
 * @returns:    Pointer to @wtx if ok, NULL on error.
 *
 * Sorry, TOO LONG a function, but breaking it up is kind of hard
 *
 * This will break the buffer in chunks smaller than
 * i1480u_MAX_FRG_SIZE (including the header) and add proper headers
 * to each:
 *
 *   1st header           \
 *   i1480 tx header      |  fragment 1
 *   fragment data        /
 *   nxt header           \  fragment 2
 *   fragment data        /
 *   ..
 *   ..
 *   last header          \  fragment 3
 *   last fragment data   /
 *
 * This does not fill the i1480 TX header, it is left up to the
 * caller to do that; you can get it from @wtx->wlp_tx_hdr.
 *
 * This function consumes the skb unless there is an error.
 */
static
int i1480u_tx_create_n(struct i1480u_tx *wtx, struct sk_buff *skb,
		       gfp_t gfp_mask)
{
	int result;
	void *pl;
	size_t pl_size;

	void *pl_itr, *buf_itr;
	size_t pl_size_left, frgs, pl_size_1st, frg_pl_size = 0;
	struct untd_hdr_1st *untd_hdr_1st;
	struct wlp_tx_hdr *wlp_tx_hdr;
	struct untd_hdr_rst *untd_hdr_rst;

	wtx->skb = NULL;
	pl = skb->data;
	pl_itr = pl;
	pl_size = skb->len;
	pl_size_left = pl_size;	/* payload size */
	/* First fragment; fits as much as i1480u_MAX_FRG_SIZE minus
	 * the headers */
	pl_size_1st = i1480u_MAX_FRG_SIZE
		- sizeof(struct untd_hdr_1st) - sizeof(struct wlp_tx_hdr);
	BUG_ON(pl_size_1st > pl_size);
	pl_size_left -= pl_size_1st;
	/* The rest have an smaller header (no i1480 TX header). We
	 * need to break up the payload in blocks smaller than
	 * i1480u_MAX_PL_SIZE (payload excluding header). */
	frgs = (pl_size_left + i1480u_MAX_PL_SIZE - 1) / i1480u_MAX_PL_SIZE;
	/* Allocate space for the new buffer. In this new buffer we'll
	 * place the headers followed by the data fragment, headers,
	 * data fragments, etc..
	 */
	result = -ENOMEM;
	wtx->buf_size = sizeof(*untd_hdr_1st)
		+ sizeof(*wlp_tx_hdr)
		+ frgs * sizeof(*untd_hdr_rst)
		+ pl_size;
	wtx->buf = kmalloc(wtx->buf_size, gfp_mask);
	if (wtx->buf == NULL)
		goto error_buf_alloc;

	buf_itr = wtx->buf;		/* We got the space, let's fill it up */
	/* Fill 1st fragment */
	untd_hdr_1st = buf_itr;
	buf_itr += sizeof(*untd_hdr_1st);
	untd_hdr_set_type(&untd_hdr_1st->hdr, i1480u_PKT_FRAG_1ST);
	untd_hdr_set_rx_tx(&untd_hdr_1st->hdr, 0);
	untd_hdr_1st->hdr.len = cpu_to_le16(pl_size + sizeof(*wlp_tx_hdr));
	untd_hdr_1st->fragment_len =
		cpu_to_le16(pl_size_1st + sizeof(*wlp_tx_hdr));
	memset(untd_hdr_1st->padding, 0, sizeof(untd_hdr_1st->padding));
	/* Set up i1480 header info */
	wlp_tx_hdr = wtx->wlp_tx_hdr = buf_itr;
	buf_itr += sizeof(*wlp_tx_hdr);
	/* Copy the first fragment */
	memcpy(buf_itr, pl_itr, pl_size_1st);
	pl_itr += pl_size_1st;
	buf_itr += pl_size_1st;

	/* Now do each remaining fragment */
	result = -EINVAL;
	while (pl_size_left > 0) {
		d_printf(5, NULL, "ITR HDR: pl_size_left %zu buf_itr %zu\n",
			 pl_size_left, buf_itr - wtx->buf);
		if (buf_itr + sizeof(*untd_hdr_rst) - wtx->buf
		    > wtx->buf_size) {
			printk(KERN_ERR "BUG: no space for header\n");
			goto error_bug;
		}
		d_printf(5, NULL, "ITR HDR 2: pl_size_left %zu buf_itr %zu\n",
			 pl_size_left, buf_itr - wtx->buf);
		untd_hdr_rst = buf_itr;
		buf_itr += sizeof(*untd_hdr_rst);
		if (pl_size_left > i1480u_MAX_PL_SIZE) {
			frg_pl_size = i1480u_MAX_PL_SIZE;
			untd_hdr_set_type(&untd_hdr_rst->hdr, i1480u_PKT_FRAG_NXT);
		} else {
			frg_pl_size = pl_size_left;
			untd_hdr_set_type(&untd_hdr_rst->hdr, i1480u_PKT_FRAG_LST);
		}
		d_printf(5, NULL,
			 "ITR PL: pl_size_left %zu buf_itr %zu frg_pl_size %zu\n",
			 pl_size_left, buf_itr - wtx->buf, frg_pl_size);
		untd_hdr_set_rx_tx(&untd_hdr_rst->hdr, 0);
		untd_hdr_rst->hdr.len = cpu_to_le16(frg_pl_size);
		untd_hdr_rst->padding = 0;
		if (buf_itr + frg_pl_size - wtx->buf
		    > wtx->buf_size) {
			printk(KERN_ERR "BUG: no space for payload\n");
			goto error_bug;
		}
		memcpy(buf_itr, pl_itr, frg_pl_size);
		buf_itr += frg_pl_size;
		pl_itr += frg_pl_size;
		pl_size_left -= frg_pl_size;
		d_printf(5, NULL,
			 "ITR PL 2: pl_size_left %zu buf_itr %zu frg_pl_size %zu\n",
			 pl_size_left, buf_itr - wtx->buf, frg_pl_size);
	}
	dev_kfree_skb_irq(skb);
	return 0;

error_bug:
	printk(KERN_ERR
	       "BUG: skb %u bytes\n"
	       "BUG: frg_pl_size %zd i1480u_MAX_FRG_SIZE %u\n"
	       "BUG: buf_itr %zu buf_size %zu pl_size_left %zu\n",
	       skb->len,
	       frg_pl_size, i1480u_MAX_FRG_SIZE,
	       buf_itr - wtx->buf, wtx->buf_size, pl_size_left);

	kfree(wtx->buf);
error_buf_alloc:
	return result;
}


/**
 * Given a buffer that fits in a single fragment, fill out a @wtx
 * struct for transmitting it down the USB pipe.
 *
 * Uses the fact that we have space reserved in front of the skbuff
 * for hardware headers :]
 *
 * This does not fill the i1480 TX header, it is left up to the
 * caller to do that; you can get it from @wtx->wlp_tx_hdr.
 *
 * @pl:		pointer to payload data
 * @pl_size:    size of the payuload
 *
 * This function does not consume the @skb.
 */
static
int i1480u_tx_create_1(struct i1480u_tx *wtx, struct sk_buff *skb,
		       gfp_t gfp_mask)
{
	struct untd_hdr_cmp *untd_hdr_cmp;
	struct wlp_tx_hdr *wlp_tx_hdr;

	wtx->buf = NULL;
	wtx->skb = skb;
	BUG_ON(skb_headroom(skb) < sizeof(*wlp_tx_hdr));
	wlp_tx_hdr = (void *) __skb_push(skb, sizeof(*wlp_tx_hdr));
	wtx->wlp_tx_hdr = wlp_tx_hdr;
	BUG_ON(skb_headroom(skb) < sizeof(*untd_hdr_cmp));
	untd_hdr_cmp = (void *) __skb_push(skb, sizeof(*untd_hdr_cmp));

	untd_hdr_set_type(&untd_hdr_cmp->hdr, i1480u_PKT_FRAG_CMP);
	untd_hdr_set_rx_tx(&untd_hdr_cmp->hdr, 0);
	untd_hdr_cmp->hdr.len = cpu_to_le16(skb->len - sizeof(*untd_hdr_cmp));
	untd_hdr_cmp->padding = 0;
	return 0;
}


/**
 * Given a skb to transmit, massage it to become palatable for the TX pipe
 *
 * This will break the buffer in chunks smaller than
 * i1480u_MAX_FRG_SIZE and add proper headers to each.
 *
 *   1st header           \
 *   i1480 tx header      |  fragment 1
 *   fragment data        /
 *   nxt header           \  fragment 2
 *   fragment data        /
 *   ..
 *   ..
 *   last header          \  fragment 3
 *   last fragment data   /
 *
 * Each fragment will be always smaller or equal to i1480u_MAX_FRG_SIZE.
 *
 * If the first fragment is smaller than i1480u_MAX_FRG_SIZE, then the
 * following is composed:
 *
 *   complete header      \
 *   i1480 tx header      | single fragment
 *   packet data          /
 *
 * We were going to use s/g support, but because the interface is
 * synch and at the end there is plenty of overhead to do it, it
 * didn't seem that worth for data that is going to be smaller than
 * one page.
 */
static
struct i1480u_tx *i1480u_tx_create(struct i1480u *i1480u,
				   struct sk_buff *skb, gfp_t gfp_mask)
{
	int result;
	struct usb_endpoint_descriptor *epd;
	int usb_pipe;
	unsigned long flags;

	struct i1480u_tx *wtx;
	const size_t pl_max_size =
		i1480u_MAX_FRG_SIZE - sizeof(struct untd_hdr_cmp)
		- sizeof(struct wlp_tx_hdr);

	wtx = kmalloc(sizeof(*wtx), gfp_mask);
	if (wtx == NULL)
		goto error_wtx_alloc;
	wtx->urb = usb_alloc_urb(0, gfp_mask);
	if (wtx->urb == NULL)
		goto error_urb_alloc;
	epd = &i1480u->usb_iface->cur_altsetting->endpoint[2].desc;
	usb_pipe = usb_sndbulkpipe(i1480u->usb_dev, epd->bEndpointAddress);
	/* Fits in a single complete packet or need to split? */
	if (skb->len > pl_max_size) {
		result = i1480u_tx_create_n(wtx, skb, gfp_mask);
		if (result < 0)
			goto error_create;
		usb_fill_bulk_urb(wtx->urb, i1480u->usb_dev, usb_pipe,
				  wtx->buf, wtx->buf_size, i1480u_tx_cb, wtx);
	} else {
		result = i1480u_tx_create_1(wtx, skb, gfp_mask);
		if (result < 0)
			goto error_create;
		usb_fill_bulk_urb(wtx->urb, i1480u->usb_dev, usb_pipe,
				  skb->data, skb->len, i1480u_tx_cb, wtx);
	}
	spin_lock_irqsave(&i1480u->tx_list_lock, flags);
	list_add(&wtx->list_node, &i1480u->tx_list);
	spin_unlock_irqrestore(&i1480u->tx_list_lock, flags);
	return wtx;

error_create:
	kfree(wtx->urb);
error_urb_alloc:
	kfree(wtx);
error_wtx_alloc:
	return NULL;
}

/**
 * Actual fragmentation and transmission of frame
 *
 * @wlp:  WLP substack data structure
 * @skb:  To be transmitted
 * @dst:  Device address of destination
 * @returns: 0 on success, <0 on failure
 *
 * This function can also be called directly (not just from
 * hard_start_xmit), so we also check here if the interface is up before
 * taking sending anything.
 */
int i1480u_xmit_frame(struct wlp *wlp, struct sk_buff *skb,
		      struct uwb_dev_addr *dst)
{
	int result = -ENXIO;
	struct i1480u *i1480u = container_of(wlp, struct i1480u, wlp);
	struct device *dev = &i1480u->usb_iface->dev;
	struct net_device *net_dev = i1480u->net_dev;
	struct i1480u_tx *wtx;
	struct wlp_tx_hdr *wlp_tx_hdr;
	static unsigned char dev_bcast[2] = { 0xff, 0xff };
#if 0
	int lockup = 50;
#endif

	d_fnstart(6, dev, "(skb %p (%u), net_dev %p)\n", skb, skb->len,
		  net_dev);
	BUG_ON(i1480u->wlp.rc == NULL);
	if ((net_dev->flags & IFF_UP) == 0)
		goto out;
	result = -EBUSY;
	if (atomic_read(&i1480u->tx_inflight.count) >= i1480u->tx_inflight.max) {
		if (d_test(2) && printk_ratelimit())
			d_printf(2, dev, "Max frames in flight "
				 "stopping queue.\n");
		netif_stop_queue(net_dev);
		goto error_max_inflight;
	}
	result = -ENOMEM;
	wtx = i1480u_tx_create(i1480u, skb, GFP_ATOMIC);
	if (unlikely(wtx == NULL)) {
		if (printk_ratelimit())
			dev_err(dev, "TX: no memory for WLP TX URB,"
				"dropping packet (in flight %d)\n",
				atomic_read(&i1480u->tx_inflight.count));
		netif_stop_queue(net_dev);
		goto error_wtx_alloc;
	}
	wtx->i1480u = i1480u;
	/* Fill out the i1480 header; @i1480u->def_tx_hdr read without
	 * locking. We do so because they are kind of orthogonal to
	 * each other (and thus not changed in an atomic batch).
	 * The ETH header is right after the WLP TX header. */
	wlp_tx_hdr = wtx->wlp_tx_hdr;
	*wlp_tx_hdr = i1480u->options.def_tx_hdr;
	wlp_tx_hdr->dstaddr = *dst;
	if (!memcmp(&wlp_tx_hdr->dstaddr, dev_bcast, sizeof(dev_bcast))
	    && (wlp_tx_hdr_delivery_id_type(wlp_tx_hdr) & WLP_DRP)) {
		/*Broadcast message directed to DRP host. Send as best effort
		 * on PCA. */
		wlp_tx_hdr_set_delivery_id_type(wlp_tx_hdr, i1480u->options.pca_base_priority);
	}

#if 0
	dev_info(dev, "TX delivering skb -> USB, %zu bytes\n", skb->len);
	dump_bytes(dev, skb->data, skb->len > 72 ? 72 : skb->len);
#endif
#if 0
	/* simulates a device lockup after every lockup# packets */
	if (lockup && ((i1480u->stats.tx_packets + 1) % lockup) == 0) {
		/* Simulate a dropped transmit interrupt */
		net_dev->trans_start = jiffies;
		netif_stop_queue(net_dev);
		dev_err(dev, "Simulate lockup at %ld\n", jiffies);
		return result;
	}
#endif

	result = usb_submit_urb(wtx->urb, GFP_ATOMIC);		/* Go baby */
	if (result < 0) {
		dev_err(dev, "TX: cannot submit URB: %d\n", result);
		/* We leave the freeing of skb to calling function */
		wtx->skb = NULL;
		goto error_tx_urb_submit;
	}
	atomic_inc(&i1480u->tx_inflight.count);
	net_dev->trans_start = jiffies;
	d_fnend(6, dev, "(skb %p (%u), net_dev %p) = %d\n", skb, skb->len,
		net_dev, result);
	return result;

error_tx_urb_submit:
	i1480u_tx_destroy(i1480u, wtx);
error_wtx_alloc:
error_max_inflight:
out:
	d_fnend(6, dev, "(skb %p (%u), net_dev %p) = %d\n", skb, skb->len,
		net_dev, result);
	return result;
}


/**
 * Transmit an skb  Called when an skbuf has to be transmitted
 *
 * The skb is first passed to WLP substack to ensure this is a valid
 * frame. If valid the device address of destination will be filled and
 * the WLP header prepended to the skb. If this step fails we fake sending
 * the frame, if we return an error the network stack will just keep trying.
 *
 * Broadcast frames inside a WSS needs to be treated special as multicast is
 * not supported. A broadcast frame is sent as unicast to each member of the
 * WSS - this is done by the WLP substack when it finds a broadcast frame.
 * So, we test if the WLP substack took over the skb and only transmit it
 * if it has not (been taken over).
 *
 * @net_dev->xmit_lock is held
 */
int i1480u_hard_start_xmit(struct sk_buff *skb, struct net_device *net_dev)
{
	int result;
	struct i1480u *i1480u = netdev_priv(net_dev);
	struct device *dev = &i1480u->usb_iface->dev;
	struct uwb_dev_addr dst;

	d_fnstart(6, dev, "(skb %p (%u), net_dev %p)\n", skb, skb->len,
		  net_dev);
	BUG_ON(i1480u->wlp.rc == NULL);
	if ((net_dev->flags & IFF_UP) == 0)
		goto error;
	result = wlp_prepare_tx_frame(dev, &i1480u->wlp, skb, &dst);
	if (result < 0) {
		dev_err(dev, "WLP verification of TX frame failed (%d). "
			"Dropping packet.\n", result);
		goto error;
	} else if (result == 1) {
		d_printf(6, dev, "WLP will transmit frame. \n");
		/* trans_start time will be set when WLP actually transmits
		 * the frame */
		goto out;
	}
	d_printf(6, dev, "Transmitting frame. \n");
	result = i1480u_xmit_frame(&i1480u->wlp, skb, &dst);
	if (result < 0) {
		dev_err(dev, "Frame TX failed (%d).\n", result);
		goto error;
	}
	d_fnend(6, dev, "(skb %p (%u), net_dev %p) = %d\n", skb, skb->len,
		net_dev, result);
	return NETDEV_TX_OK;
error:
	dev_kfree_skb_any(skb);
	i1480u->stats.tx_dropped++;
out:
	d_fnend(6, dev, "(skb %p (%u), net_dev %p) = %d\n", skb, skb->len,
		net_dev, result);
	return NETDEV_TX_OK;
}


/**
 * Called when a pkt transmission doesn't complete in a reasonable period
 * Device reset may sleep - do it outside of interrupt context (delayed)
 */
void i1480u_tx_timeout(struct net_device *net_dev)
{
	struct i1480u *i1480u = netdev_priv(net_dev);

	wlp_reset_all(&i1480u->wlp);
}


void i1480u_tx_release(struct i1480u *i1480u)
{
	unsigned long flags;
	struct i1480u_tx *wtx, *next;
	int count = 0, empty;

	spin_lock_irqsave(&i1480u->tx_list_lock, flags);
	list_for_each_entry_safe(wtx, next, &i1480u->tx_list, list_node) {
		count++;
		usb_unlink_urb(wtx->urb);
	}
	spin_unlock_irqrestore(&i1480u->tx_list_lock, flags);
	count = count*10; /* i1480ut 200ms per unlinked urb (intervals of 20ms) */
	/*
	 * We don't like this sollution too much (dirty as it is), but
	 * it is cheaper than putting a refcount on each i1480u_tx and
	 * i1480uting for all of them to go away...
	 *
	 * Called when no more packets can be added to tx_list
	 * so can i1480ut for it to be empty.
	 */
	while (1) {
		spin_lock_irqsave(&i1480u->tx_list_lock, flags);
		empty = list_empty(&i1480u->tx_list);
		spin_unlock_irqrestore(&i1480u->tx_list_lock, flags);
		if (empty)
			break;
		count--;
		BUG_ON(count == 0);
		msleep(20);
	}
}
