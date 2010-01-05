/*
 * WiMedia Logical Link Control Protocol (WLP)
 * Message exchange infrastructure
 *
 * Copyright (C) 2007 Intel Corporation
 * Reinette Chatre <reinette.chatre@intel.com>
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
 * FIXME: Docs
 *
 */

#include <linux/etherdevice.h>
#include <linux/wlp.h>

#include "wlp-internal.h"

/*
 * Direct incoming association msg to correct parsing routine
 *
 * We only expect D1, E1, C1, C3 messages as new. All other incoming
 * association messages should form part of an established session that is
 * handled elsewhere.
 * The handling of these messages often require calling sleeping functions
 * - this cannot be done in interrupt context. We use the kernel's
 * workqueue to handle these messages.
 */
static
void wlp_direct_assoc_frame(struct wlp *wlp, struct sk_buff *skb,
			   struct uwb_dev_addr *src)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct wlp_frame_assoc *assoc = (void *) skb->data;
	struct wlp_assoc_frame_ctx *frame_ctx;

	frame_ctx = kmalloc(sizeof(*frame_ctx), GFP_ATOMIC);
	if (frame_ctx == NULL) {
		dev_err(dev, "WLP: Unable to allocate memory for association "
			"frame handling.\n");
		kfree_skb(skb);
		return;
	}
	frame_ctx->wlp = wlp;
	frame_ctx->skb = skb;
	frame_ctx->src = *src;
	switch (assoc->type) {
	case WLP_ASSOC_D1:
		INIT_WORK(&frame_ctx->ws, wlp_handle_d1_frame);
		schedule_work(&frame_ctx->ws);
		break;
	case WLP_ASSOC_E1:
		kfree_skb(skb); /* Temporary until we handle it */
		kfree(frame_ctx); /* Temporary until we handle it */
		break;
	case WLP_ASSOC_C1:
		INIT_WORK(&frame_ctx->ws, wlp_handle_c1_frame);
		schedule_work(&frame_ctx->ws);
		break;
	case WLP_ASSOC_C3:
		INIT_WORK(&frame_ctx->ws, wlp_handle_c3_frame);
		schedule_work(&frame_ctx->ws);
		break;
	default:
		dev_err(dev, "Received unexpected association frame. "
			"Type = %d \n", assoc->type);
		kfree_skb(skb);
		kfree(frame_ctx);
		break;
	}
}

/*
 * Process incoming association frame
 *
 * Although it could be possible to deal with some incoming association
 * messages without creating a new session we are keeping things simple. We
 * do not accept new association messages if there is a session in progress
 * and the messages do not belong to that session.
 *
 * If an association message arrives that causes the creation of a session
 * (WLP_ASSOC_E1) while we are in the process of creating a session then we
 * rely on the neighbor mutex to protect the data. That is, the new session
 * will not be started until the previous is completed.
 */
static
void wlp_receive_assoc_frame(struct wlp *wlp, struct sk_buff *skb,
			     struct uwb_dev_addr *src)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	struct wlp_frame_assoc *assoc = (void *) skb->data;
	struct wlp_session *session = wlp->session;
	u8 version;

	if (wlp_get_version(wlp, &assoc->version, &version,
			    sizeof(assoc->version)) < 0)
		goto error;
	if (version != WLP_VERSION) {
		dev_err(dev, "Unsupported WLP version in association "
			"message.\n");
		goto error;
	}
	if (session != NULL) {
		/* Function that created this session is still holding the
		 * &wlp->mutex to protect this session. */
		if (assoc->type == session->exp_message ||
		    assoc->type == WLP_ASSOC_F0) {
			if (!memcmp(&session->neighbor_addr, src,
				   sizeof(*src))) {
				session->data = skb;
				(session->cb)(wlp);
			} else {
				dev_err(dev, "Received expected message from "
					"unexpected source.  Expected message "
					"%d or F0 from %02x:%02x, but received "
					"it from %02x:%02x. Dropping.\n",
					session->exp_message,
					session->neighbor_addr.data[1],
					session->neighbor_addr.data[0],
					src->data[1], src->data[0]);
				goto error;
			}
		} else {
			dev_err(dev, "Association already in progress. "
				"Dropping.\n");
			goto error;
		}
	} else {
		wlp_direct_assoc_frame(wlp, skb, src);
	}
	return;
error:
	kfree_skb(skb);
}

/*
 * Verify incoming frame is from connected neighbor, prep to pass to WLP client
 *
 * Verification proceeds according to WLP 0.99 [7.3.1]. The source address
 * is used to determine which neighbor is sending the frame and the WSS tag
 * is used to know to which WSS the frame belongs (we only support one WSS
 * so this test is straight forward).
 * With the WSS found we need to ensure that we are connected before
 * allowing the exchange of data frames.
 */
static
int wlp_verify_prep_rx_frame(struct wlp *wlp, struct sk_buff *skb,
			     struct uwb_dev_addr *src)
{
	struct device *dev = &wlp->rc->uwb_dev.dev;
	int result = -EINVAL;
	struct wlp_eda_node eda_entry;
	struct wlp_frame_std_abbrv_hdr *hdr = (void *) skb->data;

	/*verify*/
	result = wlp_copy_eda_node(&wlp->eda, src, &eda_entry);
	if (result < 0) {
		if (printk_ratelimit())
			dev_err(dev, "WLP: Incoming frame is from unknown "
				"neighbor %02x:%02x.\n", src->data[1],
				src->data[0]);
		goto out;
	}
	if (hdr->tag != eda_entry.tag) {
		if (printk_ratelimit())
			dev_err(dev, "WLP: Tag of incoming frame from "
				"%02x:%02x does not match expected tag. "
				"Received 0x%02x, expected 0x%02x. \n",
				src->data[1], src->data[0], hdr->tag,
				eda_entry.tag);
		result = -EINVAL;
		goto out;
	}
	if (eda_entry.state != WLP_WSS_CONNECTED) {
		if (printk_ratelimit())
			dev_err(dev, "WLP: Incoming frame from "
				"%02x:%02x does is not from connected WSS.\n",
				src->data[1], src->data[0]);
		result = -EINVAL;
		goto out;
	}
	/*prep*/
	skb_pull(skb, sizeof(*hdr));
out:
	return result;
}

/*
 * Receive a WLP frame from device
 *
 * @returns: 1 if calling function should free the skb
 *           0 if it successfully handled skb and freed it
 *           0 if error occured, will free skb in this case
 */
int wlp_receive_frame(struct device *dev, struct wlp *wlp, struct sk_buff *skb,
		      struct uwb_dev_addr *src)
{
	unsigned len = skb->len;
	void *ptr = skb->data;
	struct wlp_frame_hdr *hdr;
	int result = 0;

	if (len < sizeof(*hdr)) {
		dev_err(dev, "Not enough data to parse WLP header.\n");
		result = -EINVAL;
		goto out;
	}
	hdr = ptr;
	if (le16_to_cpu(hdr->mux_hdr) != WLP_PROTOCOL_ID) {
		dev_err(dev, "Not a WLP frame type.\n");
		result = -EINVAL;
		goto out;
	}
	switch (hdr->type) {
	case WLP_FRAME_STANDARD:
		if (len < sizeof(struct wlp_frame_std_abbrv_hdr)) {
			dev_err(dev, "Not enough data to parse Standard "
				"WLP header.\n");
			goto out;
		}
		result = wlp_verify_prep_rx_frame(wlp, skb, src);
		if (result < 0) {
			if (printk_ratelimit())
				dev_err(dev, "WLP: Verification of frame "
					"from neighbor %02x:%02x failed.\n",
					src->data[1], src->data[0]);
			goto out;
		}
		result = 1;
		break;
	case WLP_FRAME_ABBREVIATED:
		dev_err(dev, "Abbreviated frame received. FIXME?\n");
		kfree_skb(skb);
		break;
	case WLP_FRAME_CONTROL:
		dev_err(dev, "Control frame received. FIXME?\n");
		kfree_skb(skb);
		break;
	case WLP_FRAME_ASSOCIATION:
		if (len < sizeof(struct wlp_frame_assoc)) {
			dev_err(dev, "Not enough data to parse Association "
				"WLP header.\n");
			goto out;
		}
		wlp_receive_assoc_frame(wlp, skb, src);
		break;
	default:
		dev_err(dev, "Invalid frame received.\n");
		result = -EINVAL;
		break;
	}
out:
	if (result < 0) {
		kfree_skb(skb);
		result = 0;
	}
	return result;
}
EXPORT_SYMBOL_GPL(wlp_receive_frame);


/*
 * Verify frame from network stack, prepare for further transmission
 *
 * @skb:   the socket buffer that needs to be prepared for transmission (it
 *         is in need of a WLP header). If this is a broadcast frame we take
 *         over the entire transmission.
 *         If it is a unicast the WSS connection should already be established
 *         and transmission will be done by the calling function.
 * @dst:   On return this will contain the device address to which the
 *         frame is destined.
 * @returns: 0 on success no tx : WLP header successfully applied to skb buffer,
 *                                calling function can proceed with tx
 *           1 on success with tx : WLP will take over transmission of this
 *                                  frame
 *           <0 on error
 *
 * The network stack (WLP client) is attempting to transmit a frame. We can
 * only transmit data if a local WSS is at least active (connection will be
 * done here if this is a broadcast frame and neighbor also has the WSS
 * active).
 *
 * The frame can be either broadcast or unicast. Broadcast in a WSS is
 * supported via multicast, but we don't support multicast yet (until
 * devices start to support MAB IEs). If a broadcast frame needs to be
 * transmitted it is treated as a unicast frame to each neighbor. In this
 * case the WLP takes over transmission of the skb and returns 1
 * to the caller to indicate so. Also, in this case, if a neighbor has the
 * same WSS activated but is not connected then the WSS connection will be
 * done at this time. The neighbor's virtual address will be learned at
 * this time.
 *
 * The destination address in a unicast frame is the virtual address of the
 * neighbor. This address only becomes known when a WSS connection is
 * established. We thus rely on a broadcast frame to trigger the setup of
 * WSS connections to all neighbors before we are able to send unicast
 * frames to them. This seems reasonable as IP would usually use ARP first
 * before any unicast frames are sent.
 *
 * If we are already connected to the neighbor (neighbor's virtual address
 * is known) we just prepare the WLP header and the caller will continue to
 * send the frame.
 *
 * A failure in this function usually indicates something that cannot be
 * fixed automatically. So, if this function fails (@return < 0) the calling
 * function should not retry to send the frame as it will very likely keep
 * failing.
 *
 */
int wlp_prepare_tx_frame(struct device *dev, struct wlp *wlp,
			 struct sk_buff *skb, struct uwb_dev_addr *dst)
{
	int result = -EINVAL;
	struct ethhdr *eth_hdr = (void *) skb->data;

	if (is_multicast_ether_addr(eth_hdr->h_dest)) {
		result = wlp_eda_for_each(&wlp->eda, wlp_wss_send_copy, skb);
		if (result < 0) {
			if (printk_ratelimit())
				dev_err(dev, "Unable to handle broadcast "
					"frame from WLP client.\n");
			goto out;
		}
		dev_kfree_skb_irq(skb);
		result = 1;
		/* Frame will be transmitted by WLP. */
	} else {
		result = wlp_eda_for_virtual(&wlp->eda, eth_hdr->h_dest, dst,
					     wlp_wss_prep_hdr, skb);
		if (unlikely(result < 0)) {
			if (printk_ratelimit())
				dev_err(dev, "Unable to prepare "
					"skb for transmission. \n");
			goto out;
		}
	}
out:
	return result;
}
EXPORT_SYMBOL_GPL(wlp_prepare_tx_frame);
