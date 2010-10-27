/*
 * WUSB Wire Adapter: WLP interface
 * Driver for the Linux Network stack.
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
 * i1480u's RX handling is simple. i1480u will send the received
 * network packets broken up in fragments; 1 to N fragments make a
 * packet, we assemble them together and deliver the packet with netif_rx().
 *
 * Beacuse each USB transfer is a *single* fragment (except when the
 * transfer contains a first fragment), each URB called thus
 * back contains one or two fragments. So we queue N URBs, each with its own
 * fragment buffer. When a URB is done, we process it (adding to the
 * current skb from the fragment buffer until complete). Once
 * processed, we requeue the URB. There is always a bunch of URBs
 * ready to take data, so the intergap should be minimal.
 *
 * An URB's transfer buffer is the data field of a socket buffer. This
 * reduces copying as data can be passed directly to network layer. If a
 * complete packet or 1st fragment is received the URB's transfer buffer is
 * taken away from it and used to send data to the network layer. In this
 * case a new transfer buffer is allocated to the URB before being requeued.
 * If a "NEXT" or "LAST" fragment is received, the fragment contents is
 * appended to the RX packet under construction and the transfer buffer
 * is reused. To be able to use this buffer to assemble complete packets
 * we set each buffer's size to that of the MAX ethernet packet that can
 * be received. There is thus room for improvement in memory usage.
 *
 * When the max tx fragment size increases, we should be able to read
 * data into the skbs directly with very simple code.
 *
 * ROADMAP:
 *
 *   ENTRY POINTS:
 *
 *     i1480u_rx_setup(): setup RX context [from i1480u_open()]
 *
 *     i1480u_rx_release(): release RX context [from i1480u_stop()]
 *
 *     i1480u_rx_cb(): called when the RX USB URB receives a
 *                     packet. It removes the header and pushes it up
 *                     the Linux netdev stack with netif_rx().
 *
 *       i1480u_rx_buffer()
 *         i1480u_drop() and i1480u_fix()
 *         i1480u_skb_deliver
 *
 */

#include <linux/gfp.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include "i1480u-wlp.h"

/*
 * Setup the RX context
 *
 * Each URB is provided with a transfer_buffer that is the data field
 * of a new socket buffer.
 */
int i1480u_rx_setup(struct i1480u *i1480u)
{
	int result, cnt;
	struct device *dev = &i1480u->usb_iface->dev;
	struct net_device *net_dev = i1480u->net_dev;
	struct usb_endpoint_descriptor *epd;
	struct sk_buff *skb;

	/* Alloc RX stuff */
	i1480u->rx_skb = NULL;	/* not in process of receiving packet */
	result = -ENOMEM;
	epd = &i1480u->usb_iface->cur_altsetting->endpoint[1].desc;
	for (cnt = 0; cnt < i1480u_RX_BUFS; cnt++) {
		struct i1480u_rx_buf *rx_buf = &i1480u->rx_buf[cnt];
		rx_buf->i1480u = i1480u;
		skb = dev_alloc_skb(i1480u_MAX_RX_PKT_SIZE);
		if (!skb) {
			dev_err(dev,
				"RX: cannot allocate RX buffer %d\n", cnt);
			result = -ENOMEM;
			goto error;
		}
		skb->dev = net_dev;
		skb->ip_summed = CHECKSUM_NONE;
		skb_reserve(skb, 2);
		rx_buf->data = skb;
		rx_buf->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (unlikely(rx_buf->urb == NULL)) {
			dev_err(dev, "RX: cannot allocate URB %d\n", cnt);
			result = -ENOMEM;
			goto error;
		}
		usb_fill_bulk_urb(rx_buf->urb, i1480u->usb_dev,
			  usb_rcvbulkpipe(i1480u->usb_dev, epd->bEndpointAddress),
			  rx_buf->data->data, i1480u_MAX_RX_PKT_SIZE - 2,
			  i1480u_rx_cb, rx_buf);
		result = usb_submit_urb(rx_buf->urb, GFP_NOIO);
		if (unlikely(result < 0)) {
			dev_err(dev, "RX: cannot submit URB %d: %d\n",
				cnt, result);
			goto error;
		}
	}
	return 0;

error:
	i1480u_rx_release(i1480u);
	return result;
}


/* Release resources associated to the rx context */
void i1480u_rx_release(struct i1480u *i1480u)
{
	int cnt;
	for (cnt = 0; cnt < i1480u_RX_BUFS; cnt++) {
		if (i1480u->rx_buf[cnt].data)
			dev_kfree_skb(i1480u->rx_buf[cnt].data);
		if (i1480u->rx_buf[cnt].urb) {
			usb_kill_urb(i1480u->rx_buf[cnt].urb);
			usb_free_urb(i1480u->rx_buf[cnt].urb);
		}
	}
	if (i1480u->rx_skb != NULL)
		dev_kfree_skb(i1480u->rx_skb);
}

static
void i1480u_rx_unlink_urbs(struct i1480u *i1480u)
{
	int cnt;
	for (cnt = 0; cnt < i1480u_RX_BUFS; cnt++) {
		if (i1480u->rx_buf[cnt].urb)
			usb_unlink_urb(i1480u->rx_buf[cnt].urb);
	}
}

/* Fix an out-of-sequence packet */
#define i1480u_fix(i1480u, msg...)			\
do {							\
	if (printk_ratelimit())				\
		dev_err(&i1480u->usb_iface->dev, msg);	\
	dev_kfree_skb_irq(i1480u->rx_skb);		\
	i1480u->rx_skb = NULL;				\
	i1480u->rx_untd_pkt_size = 0;			\
} while (0)


/* Drop an out-of-sequence packet */
#define i1480u_drop(i1480u, msg...)			\
do {							\
	if (printk_ratelimit())				\
		dev_err(&i1480u->usb_iface->dev, msg);	\
	i1480u->net_dev->stats.rx_dropped++;			\
} while (0)




/* Finalizes setting up the SKB and delivers it
 *
 * We first pass the incoming frame to WLP substack for verification. It
 * may also be a WLP association frame in which case WLP will take over the
 * processing. If WLP does not take it over it will still verify it, if the
 * frame is invalid the skb will be freed by WLP and we will not continue
 * parsing.
 * */
static
void i1480u_skb_deliver(struct i1480u *i1480u)
{
	int should_parse;
	struct net_device *net_dev = i1480u->net_dev;
	struct device *dev = &i1480u->usb_iface->dev;

	should_parse = wlp_receive_frame(dev, &i1480u->wlp, i1480u->rx_skb,
					 &i1480u->rx_srcaddr);
	if (!should_parse)
		goto out;
	i1480u->rx_skb->protocol = eth_type_trans(i1480u->rx_skb, net_dev);
	net_dev->stats.rx_packets++;
	net_dev->stats.rx_bytes += i1480u->rx_untd_pkt_size;

	netif_rx(i1480u->rx_skb);		/* deliver */
out:
	i1480u->rx_skb = NULL;
	i1480u->rx_untd_pkt_size = 0;
}


/*
 * Process a buffer of data received from the USB RX endpoint
 *
 * First fragment arrives with next or last fragment. All other fragments
 * arrive alone.
 *
 * /me hates long functions.
 */
static
void i1480u_rx_buffer(struct i1480u_rx_buf *rx_buf)
{
	unsigned pkt_completed = 0;	/* !0 when we got all pkt fragments */
	size_t untd_hdr_size, untd_frg_size;
	size_t i1480u_hdr_size;
	struct wlp_rx_hdr *i1480u_hdr = NULL;

	struct i1480u *i1480u = rx_buf->i1480u;
	struct sk_buff *skb = rx_buf->data;
	int size_left = rx_buf->urb->actual_length;
	void *ptr = rx_buf->urb->transfer_buffer; /* also rx_buf->data->data */
	struct untd_hdr *untd_hdr;

	struct net_device *net_dev = i1480u->net_dev;
	struct device *dev = &i1480u->usb_iface->dev;
	struct sk_buff *new_skb;

#if 0
	dev_fnstart(dev,
		    "(i1480u %p ptr %p size_left %zu)\n", i1480u, ptr, size_left);
	dev_err(dev, "RX packet, %zu bytes\n", size_left);
	dump_bytes(dev, ptr, size_left);
#endif
	i1480u_hdr_size = sizeof(struct wlp_rx_hdr);

	while (size_left > 0) {
		if (pkt_completed) {
			i1480u_drop(i1480u, "RX: fragment follows completed"
					 "packet in same buffer. Dropping\n");
			break;
		}
		untd_hdr = ptr;
		if (size_left < sizeof(*untd_hdr)) {	/*  Check the UNTD header */
			i1480u_drop(i1480u, "RX: short UNTD header! Dropping\n");
			goto out;
		}
		if (unlikely(untd_hdr_rx_tx(untd_hdr) == 0)) {	/* Paranoia: TX set? */
			i1480u_drop(i1480u, "RX: TX bit set! Dropping\n");
			goto out;
		}
		switch (untd_hdr_type(untd_hdr)) {	/* Check the UNTD header type */
		case i1480u_PKT_FRAG_1ST: {
			struct untd_hdr_1st *untd_hdr_1st = (void *) untd_hdr;
			dev_dbg(dev, "1st fragment\n");
			untd_hdr_size = sizeof(struct untd_hdr_1st);
			if (i1480u->rx_skb != NULL)
				i1480u_fix(i1480u, "RX: 1st fragment out of "
					"sequence! Fixing\n");
			if (size_left < untd_hdr_size + i1480u_hdr_size) {
				i1480u_drop(i1480u, "RX: short 1st fragment! "
					"Dropping\n");
				goto out;
			}
			i1480u->rx_untd_pkt_size = le16_to_cpu(untd_hdr->len)
						 - i1480u_hdr_size;
			untd_frg_size = le16_to_cpu(untd_hdr_1st->fragment_len);
			if (size_left < untd_hdr_size + untd_frg_size) {
				i1480u_drop(i1480u,
					    "RX: short payload! Dropping\n");
				goto out;
			}
			i1480u->rx_skb = skb;
			i1480u_hdr = (void *) untd_hdr_1st + untd_hdr_size;
			i1480u->rx_srcaddr = i1480u_hdr->srcaddr;
			skb_put(i1480u->rx_skb, untd_hdr_size + untd_frg_size);
			skb_pull(i1480u->rx_skb, untd_hdr_size + i1480u_hdr_size);
			stats_add_sample(&i1480u->lqe_stats, (s8) i1480u_hdr->LQI - 7);
			stats_add_sample(&i1480u->rssi_stats, i1480u_hdr->RSSI + 18);
			rx_buf->data = NULL; /* need to create new buffer */
			break;
		}
		case i1480u_PKT_FRAG_NXT: {
			dev_dbg(dev, "nxt fragment\n");
			untd_hdr_size = sizeof(struct untd_hdr_rst);
			if (i1480u->rx_skb == NULL) {
				i1480u_drop(i1480u, "RX: next fragment out of "
					    "sequence! Dropping\n");
				goto out;
			}
			if (size_left < untd_hdr_size) {
				i1480u_drop(i1480u, "RX: short NXT fragment! "
					    "Dropping\n");
				goto out;
			}
			untd_frg_size = le16_to_cpu(untd_hdr->len);
			if (size_left < untd_hdr_size + untd_frg_size) {
				i1480u_drop(i1480u,
					    "RX: short payload! Dropping\n");
				goto out;
			}
			memmove(skb_put(i1480u->rx_skb, untd_frg_size),
					ptr + untd_hdr_size, untd_frg_size);
			break;
		}
		case i1480u_PKT_FRAG_LST: {
			dev_dbg(dev, "Lst fragment\n");
			untd_hdr_size = sizeof(struct untd_hdr_rst);
			if (i1480u->rx_skb == NULL) {
				i1480u_drop(i1480u, "RX: last fragment out of "
					    "sequence! Dropping\n");
				goto out;
			}
			if (size_left < untd_hdr_size) {
				i1480u_drop(i1480u, "RX: short LST fragment! "
					    "Dropping\n");
				goto out;
			}
			untd_frg_size = le16_to_cpu(untd_hdr->len);
			if (size_left < untd_frg_size + untd_hdr_size) {
				i1480u_drop(i1480u,
					    "RX: short payload! Dropping\n");
				goto out;
			}
			memmove(skb_put(i1480u->rx_skb, untd_frg_size),
					ptr + untd_hdr_size, untd_frg_size);
			pkt_completed = 1;
			break;
		}
		case i1480u_PKT_FRAG_CMP: {
			dev_dbg(dev, "cmp fragment\n");
			untd_hdr_size = sizeof(struct untd_hdr_cmp);
			if (i1480u->rx_skb != NULL)
				i1480u_fix(i1480u, "RX: fix out-of-sequence CMP"
					   " fragment!\n");
			if (size_left < untd_hdr_size + i1480u_hdr_size) {
				i1480u_drop(i1480u, "RX: short CMP fragment! "
					    "Dropping\n");
				goto out;
			}
			i1480u->rx_untd_pkt_size = le16_to_cpu(untd_hdr->len);
			untd_frg_size = i1480u->rx_untd_pkt_size;
			if (size_left < i1480u->rx_untd_pkt_size + untd_hdr_size) {
				i1480u_drop(i1480u,
					    "RX: short payload! Dropping\n");
				goto out;
			}
			i1480u->rx_skb = skb;
			i1480u_hdr = (void *) untd_hdr + untd_hdr_size;
			i1480u->rx_srcaddr = i1480u_hdr->srcaddr;
			stats_add_sample(&i1480u->lqe_stats, (s8) i1480u_hdr->LQI - 7);
			stats_add_sample(&i1480u->rssi_stats, i1480u_hdr->RSSI + 18);
			skb_put(i1480u->rx_skb, untd_hdr_size + i1480u->rx_untd_pkt_size);
			skb_pull(i1480u->rx_skb, untd_hdr_size + i1480u_hdr_size);
			rx_buf->data = NULL;	/* for hand off skb to network stack */
			pkt_completed = 1;
			i1480u->rx_untd_pkt_size -= i1480u_hdr_size; /* accurate stat */
			break;
		}
		default:
			i1480u_drop(i1480u, "RX: unknown packet type %u! "
				    "Dropping\n", untd_hdr_type(untd_hdr));
			goto out;
		}
		size_left -= untd_hdr_size + untd_frg_size;
		if (size_left > 0)
			ptr += untd_hdr_size + untd_frg_size;
	}
	if (pkt_completed)
		i1480u_skb_deliver(i1480u);
out:
	/* recreate needed RX buffers*/
	if (rx_buf->data == NULL) {
		/* buffer is being used to receive packet, create new */
		new_skb = dev_alloc_skb(i1480u_MAX_RX_PKT_SIZE);
		if (!new_skb) {
			if (printk_ratelimit())
				dev_err(dev,
				"RX: cannot allocate RX buffer\n");
		} else {
			new_skb->dev = net_dev;
			new_skb->ip_summed = CHECKSUM_NONE;
			skb_reserve(new_skb, 2);
			rx_buf->data = new_skb;
		}
	}
	return;
}


/*
 * Called when an RX URB has finished receiving or has found some kind
 * of error condition.
 *
 * LIMITATIONS:
 *
 *  - We read USB-transfers, each transfer contains a SINGLE fragment
 *    (can contain a complete packet, or a 1st, next, or last fragment
 *    of a packet).
 *    Looks like a transfer can contain more than one fragment (07/18/06)
 *
 *  - Each transfer buffer is the size of the maximum packet size (minus
 *    headroom), i1480u_MAX_PKT_SIZE - 2
 *
 *  - We always read the full USB-transfer, no partials.
 *
 *  - Each transfer is read directly into a skb. This skb will be used to
 *    send data to the upper layers if it is the first fragment or a complete
 *    packet. In the other cases the data will be copied from the skb to
 *    another skb that is being prepared for the upper layers from a prev
 *    first fragment.
 *
 * It is simply too much of a pain. Gosh, there should be a unified
 * SG infrastructure for *everything* [so that I could declare a SG
 * buffer, pass it to USB for receiving, append some space to it if
 * I wish, receive more until I have the whole chunk, adapt
 * pointers on each fragment to remove hardware headers and then
 * attach that to an skbuff and netif_rx()].
 */
void i1480u_rx_cb(struct urb *urb)
{
	int result;
	int do_parse_buffer = 1;
	struct i1480u_rx_buf *rx_buf = urb->context;
	struct i1480u *i1480u = rx_buf->i1480u;
	struct device *dev = &i1480u->usb_iface->dev;
	unsigned long flags;
	u8 rx_buf_idx = rx_buf - i1480u->rx_buf;

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:	/* Not an error, but a controlled situation; */
	case -ENOENT:		/* (we killed the URB)...so, no broadcast */
	case -ESHUTDOWN:	/* going away! */
		dev_err(dev, "RX URB[%u]: goind down %d\n",
			rx_buf_idx, urb->status);
		goto error;
	default:
		dev_err(dev, "RX URB[%u]: unknown status %d\n",
			rx_buf_idx, urb->status);
		if (edc_inc(&i1480u->rx_errors, EDC_MAX_ERRORS,
					EDC_ERROR_TIMEFRAME)) {
			dev_err(dev, "RX: max acceptable errors exceeded,"
					" resetting device.\n");
			i1480u_rx_unlink_urbs(i1480u);
			wlp_reset_all(&i1480u->wlp);
			goto error;
		}
		do_parse_buffer = 0;
		break;
	}
	spin_lock_irqsave(&i1480u->lock, flags);
	/* chew the data fragments, extract network packets */
	if (do_parse_buffer) {
		i1480u_rx_buffer(rx_buf);
		if (rx_buf->data) {
			rx_buf->urb->transfer_buffer = rx_buf->data->data;
			result = usb_submit_urb(rx_buf->urb, GFP_ATOMIC);
			if (result < 0) {
				dev_err(dev, "RX URB[%u]: cannot submit %d\n",
					rx_buf_idx, result);
			}
		}
	}
	spin_unlock_irqrestore(&i1480u->lock, flags);
error:
	return;
}

