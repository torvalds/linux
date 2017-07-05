/*
 * Copyright (c) 2015-2016 Quantenna Communications, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _QTN_FMAC_QLINK_UTIL_H_
#define _QTN_FMAC_QLINK_UTIL_H_

#include <linux/types.h>
#include <linux/skbuff.h>

#include "qlink.h"

static inline void qtnf_cmd_skb_put_action(struct sk_buff *skb, u16 action)
{
	__le16 *buf_ptr;

	buf_ptr = skb_put(skb, sizeof(action));
	*buf_ptr = cpu_to_le16(action);
}

static inline void
qtnf_cmd_skb_put_buffer(struct sk_buff *skb, const u8 *buf_src, size_t len)
{
	skb_put_data(skb, buf_src, len);
}

static inline void qtnf_cmd_skb_put_tlv_arr(struct sk_buff *skb,
					    u16 tlv_id, const u8 arr[],
					    size_t arr_len)
{
	struct qlink_tlv_hdr *hdr = skb_put(skb, sizeof(*hdr) + arr_len);

	hdr->type = cpu_to_le16(tlv_id);
	hdr->len = cpu_to_le16(arr_len);
	memcpy(hdr->val, arr, arr_len);
}

static inline void qtnf_cmd_skb_put_tlv_u8(struct sk_buff *skb, u16 tlv_id,
					   u8 value)
{
	struct qlink_tlv_hdr *hdr = skb_put(skb, sizeof(*hdr) + sizeof(value));

	hdr->type = cpu_to_le16(tlv_id);
	hdr->len = cpu_to_le16(sizeof(value));
	*hdr->val = value;
}

static inline void qtnf_cmd_skb_put_tlv_u16(struct sk_buff *skb,
					    u16 tlv_id, u16 value)
{
	struct qlink_tlv_hdr *hdr = skb_put(skb, sizeof(*hdr) + sizeof(value));
	__le16 tmp = cpu_to_le16(value);

	hdr->type = cpu_to_le16(tlv_id);
	hdr->len = cpu_to_le16(sizeof(value));
	memcpy(hdr->val, &tmp, sizeof(tmp));
}

u16 qlink_iface_type_mask_to_nl(u16 qlink_mask);
u8 qlink_chan_width_mask_to_nl(u16 qlink_mask);

#endif /* _QTN_FMAC_QLINK_UTIL_H_ */
