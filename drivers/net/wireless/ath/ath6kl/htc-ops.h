/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef HTC_OPS_H
#define HTC_OPS_H

#include "htc.h"
#include "debug.h"

static inline void *ath6kl_htc_create(struct ath6kl *ar)
{
	return ar->htc_ops->create(ar);
}

static inline int ath6kl_htc_wait_target(struct htc_target *target)
{
	return target->dev->ar->htc_ops->wait_target(target);
}

static inline int ath6kl_htc_start(struct htc_target *target)
{
	return target->dev->ar->htc_ops->start(target);
}

static inline int ath6kl_htc_conn_service(struct htc_target *target,
					  struct htc_service_connect_req *req,
					  struct htc_service_connect_resp *resp)
{
	return target->dev->ar->htc_ops->conn_service(target, req, resp);
}

static inline int ath6kl_htc_tx(struct htc_target *target,
				struct htc_packet *packet)
{
	return target->dev->ar->htc_ops->tx(target, packet);
}

static inline void ath6kl_htc_stop(struct htc_target *target)
{
	return target->dev->ar->htc_ops->stop(target);
}

static inline void ath6kl_htc_cleanup(struct htc_target *target)
{
	return target->dev->ar->htc_ops->cleanup(target);
}

static inline void ath6kl_htc_flush_txep(struct htc_target *target,
					 enum htc_endpoint_id endpoint,
					 u16 tag)
{
	return target->dev->ar->htc_ops->flush_txep(target, endpoint, tag);
}

static inline void ath6kl_htc_flush_rx_buf(struct htc_target *target)
{
	return target->dev->ar->htc_ops->flush_rx_buf(target);
}

static inline void ath6kl_htc_activity_changed(struct htc_target *target,
					       enum htc_endpoint_id endpoint,
					       bool active)
{
	return target->dev->ar->htc_ops->activity_changed(target, endpoint,
							  active);
}

static inline int ath6kl_htc_get_rxbuf_num(struct htc_target *target,
					   enum htc_endpoint_id endpoint)
{
	return target->dev->ar->htc_ops->get_rxbuf_num(target, endpoint);
}

static inline int ath6kl_htc_add_rxbuf_multiple(struct htc_target *target,
						struct list_head *pktq)
{
	return target->dev->ar->htc_ops->add_rxbuf_multiple(target, pktq);
}

static inline int ath6kl_htc_credit_setup(struct htc_target *target,
					  struct ath6kl_htc_credit_info *info)
{
	return target->dev->ar->htc_ops->credit_setup(target, info);
}

static inline void ath6kl_htc_tx_complete(struct ath6kl *ar,
					  struct sk_buff *skb)
{
	ar->htc_ops->tx_complete(ar, skb);
}


static inline void ath6kl_htc_rx_complete(struct ath6kl *ar,
					  struct sk_buff *skb, u8 pipe)
{
	ar->htc_ops->rx_complete(ar, skb, pipe);
}


#endif
