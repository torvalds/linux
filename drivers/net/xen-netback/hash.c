/*
 * Copyright (c) 2016 Citrix Systems Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Softare Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#define XEN_NETIF_DEFINE_TOEPLITZ

#include "common.h"
#include <linux/vmalloc.h>
#include <linux/rculist.h>

static void xenvif_del_hash(struct rcu_head *rcu)
{
	struct xenvif_hash_cache_entry *entry;

	entry = container_of(rcu, struct xenvif_hash_cache_entry, rcu);

	kfree(entry);
}

static void xenvif_add_hash(struct xenvif *vif, const u8 *tag,
			    unsigned int len, u32 val)
{
	struct xenvif_hash_cache_entry *new, *entry, *oldest;
	unsigned long flags;
	bool found;

	new = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!new)
		return;

	memcpy(new->tag, tag, len);
	new->len = len;
	new->val = val;

	spin_lock_irqsave(&vif->hash.cache.lock, flags);

	found = false;
	oldest = NULL;
	list_for_each_entry_rcu(entry, &vif->hash.cache.list, link) {
		/* Make sure we don't add duplicate entries */
		if (entry->len == len &&
		    memcmp(entry->tag, tag, len) == 0)
			found = true;
		if (!oldest || entry->seq < oldest->seq)
			oldest = entry;
	}

	if (!found) {
		new->seq = atomic_inc_return(&vif->hash.cache.seq);
		list_add_rcu(&new->link, &vif->hash.cache.list);

		if (++vif->hash.cache.count > xenvif_hash_cache_size) {
			list_del_rcu(&oldest->link);
			vif->hash.cache.count--;
			call_rcu(&oldest->rcu, xenvif_del_hash);
		}
	}

	spin_unlock_irqrestore(&vif->hash.cache.lock, flags);

	if (found)
		kfree(new);
}

static u32 xenvif_new_hash(struct xenvif *vif, const u8 *data,
			   unsigned int len)
{
	u32 val;

	val = xen_netif_toeplitz_hash(vif->hash.key,
				      sizeof(vif->hash.key),
				      data, len);

	if (xenvif_hash_cache_size != 0)
		xenvif_add_hash(vif, data, len, val);

	return val;
}

static void xenvif_flush_hash(struct xenvif *vif)
{
	struct xenvif_hash_cache_entry *entry;
	unsigned long flags;

	if (xenvif_hash_cache_size == 0)
		return;

	spin_lock_irqsave(&vif->hash.cache.lock, flags);

	list_for_each_entry_rcu(entry, &vif->hash.cache.list, link) {
		list_del_rcu(&entry->link);
		vif->hash.cache.count--;
		call_rcu(&entry->rcu, xenvif_del_hash);
	}

	spin_unlock_irqrestore(&vif->hash.cache.lock, flags);
}

static u32 xenvif_find_hash(struct xenvif *vif, const u8 *data,
			    unsigned int len)
{
	struct xenvif_hash_cache_entry *entry;
	u32 val;
	bool found;

	if (len >= XEN_NETBK_HASH_TAG_SIZE)
		return 0;

	if (xenvif_hash_cache_size == 0)
		return xenvif_new_hash(vif, data, len);

	rcu_read_lock();

	found = false;

	list_for_each_entry_rcu(entry, &vif->hash.cache.list, link) {
		if (entry->len == len &&
		    memcmp(entry->tag, data, len) == 0) {
			val = entry->val;
			entry->seq = atomic_inc_return(&vif->hash.cache.seq);
			found = true;
			break;
		}
	}

	rcu_read_unlock();

	if (!found)
		val = xenvif_new_hash(vif, data, len);

	return val;
}

void xenvif_set_skb_hash(struct xenvif *vif, struct sk_buff *skb)
{
	struct flow_keys flow;
	u32 hash = 0;
	enum pkt_hash_types type = PKT_HASH_TYPE_NONE;
	u32 flags = vif->hash.flags;
	bool has_tcp_hdr;

	/* Quick rejection test: If the network protocol doesn't
	 * correspond to any enabled hash type then there's no point
	 * in parsing the packet header.
	 */
	switch (skb->protocol) {
	case htons(ETH_P_IP):
		if (flags & (XEN_NETIF_CTRL_HASH_TYPE_IPV4_TCP |
			     XEN_NETIF_CTRL_HASH_TYPE_IPV4))
			break;

		goto done;

	case htons(ETH_P_IPV6):
		if (flags & (XEN_NETIF_CTRL_HASH_TYPE_IPV6_TCP |
			     XEN_NETIF_CTRL_HASH_TYPE_IPV6))
			break;

		goto done;

	default:
		goto done;
	}

	memset(&flow, 0, sizeof(flow));
	if (!skb_flow_dissect_flow_keys(skb, &flow, 0))
		goto done;

	has_tcp_hdr = (flow.basic.ip_proto == IPPROTO_TCP) &&
		      !(flow.control.flags & FLOW_DIS_IS_FRAGMENT);

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		if (has_tcp_hdr &&
		    (flags & XEN_NETIF_CTRL_HASH_TYPE_IPV4_TCP)) {
			u8 data[12];

			memcpy(&data[0], &flow.addrs.v4addrs.src, 4);
			memcpy(&data[4], &flow.addrs.v4addrs.dst, 4);
			memcpy(&data[8], &flow.ports.src, 2);
			memcpy(&data[10], &flow.ports.dst, 2);

			hash = xenvif_find_hash(vif, data, sizeof(data));
			type = PKT_HASH_TYPE_L4;
		} else if (flags & XEN_NETIF_CTRL_HASH_TYPE_IPV4) {
			u8 data[8];

			memcpy(&data[0], &flow.addrs.v4addrs.src, 4);
			memcpy(&data[4], &flow.addrs.v4addrs.dst, 4);

			hash = xenvif_find_hash(vif, data, sizeof(data));
			type = PKT_HASH_TYPE_L3;
		}

		break;

	case htons(ETH_P_IPV6):
		if (has_tcp_hdr &&
		    (flags & XEN_NETIF_CTRL_HASH_TYPE_IPV6_TCP)) {
			u8 data[36];

			memcpy(&data[0], &flow.addrs.v6addrs.src, 16);
			memcpy(&data[16], &flow.addrs.v6addrs.dst, 16);
			memcpy(&data[32], &flow.ports.src, 2);
			memcpy(&data[34], &flow.ports.dst, 2);

			hash = xenvif_find_hash(vif, data, sizeof(data));
			type = PKT_HASH_TYPE_L4;
		} else if (flags & XEN_NETIF_CTRL_HASH_TYPE_IPV6) {
			u8 data[32];

			memcpy(&data[0], &flow.addrs.v6addrs.src, 16);
			memcpy(&data[16], &flow.addrs.v6addrs.dst, 16);

			hash = xenvif_find_hash(vif, data, sizeof(data));
			type = PKT_HASH_TYPE_L3;
		}

		break;
	}

done:
	if (type == PKT_HASH_TYPE_NONE)
		skb_clear_hash(skb);
	else
		__skb_set_sw_hash(skb, hash, type == PKT_HASH_TYPE_L4);
}

u32 xenvif_set_hash_alg(struct xenvif *vif, u32 alg)
{
	switch (alg) {
	case XEN_NETIF_CTRL_HASH_ALGORITHM_NONE:
	case XEN_NETIF_CTRL_HASH_ALGORITHM_TOEPLITZ:
		break;

	default:
		return XEN_NETIF_CTRL_STATUS_INVALID_PARAMETER;
	}

	vif->hash.alg = alg;

	return XEN_NETIF_CTRL_STATUS_SUCCESS;
}

u32 xenvif_get_hash_flags(struct xenvif *vif, u32 *flags)
{
	if (vif->hash.alg == XEN_NETIF_CTRL_HASH_ALGORITHM_NONE)
		return XEN_NETIF_CTRL_STATUS_NOT_SUPPORTED;

	*flags = XEN_NETIF_CTRL_HASH_TYPE_IPV4 |
		 XEN_NETIF_CTRL_HASH_TYPE_IPV4_TCP |
		 XEN_NETIF_CTRL_HASH_TYPE_IPV6 |
		 XEN_NETIF_CTRL_HASH_TYPE_IPV6_TCP;

	return XEN_NETIF_CTRL_STATUS_SUCCESS;
}

u32 xenvif_set_hash_flags(struct xenvif *vif, u32 flags)
{
	if (flags & ~(XEN_NETIF_CTRL_HASH_TYPE_IPV4 |
		      XEN_NETIF_CTRL_HASH_TYPE_IPV4_TCP |
		      XEN_NETIF_CTRL_HASH_TYPE_IPV6 |
		      XEN_NETIF_CTRL_HASH_TYPE_IPV6_TCP))
		return XEN_NETIF_CTRL_STATUS_INVALID_PARAMETER;

	if (vif->hash.alg == XEN_NETIF_CTRL_HASH_ALGORITHM_NONE)
		return XEN_NETIF_CTRL_STATUS_INVALID_PARAMETER;

	vif->hash.flags = flags;

	return XEN_NETIF_CTRL_STATUS_SUCCESS;
}

u32 xenvif_set_hash_key(struct xenvif *vif, u32 gref, u32 len)
{
	u8 *key = vif->hash.key;
	struct gnttab_copy copy_op = {
		.source.u.ref = gref,
		.source.domid = vif->domid,
		.dest.u.gmfn = virt_to_gfn(key),
		.dest.domid = DOMID_SELF,
		.dest.offset = xen_offset_in_page(key),
		.len = len,
		.flags = GNTCOPY_source_gref
	};

	if (len > XEN_NETBK_MAX_HASH_KEY_SIZE)
		return XEN_NETIF_CTRL_STATUS_INVALID_PARAMETER;

	if (len != 0) {
		gnttab_batch_copy(&copy_op, 1);

		if (copy_op.status != GNTST_okay)
			return XEN_NETIF_CTRL_STATUS_INVALID_PARAMETER;
	}

	/* Clear any remaining key octets */
	if (len < XEN_NETBK_MAX_HASH_KEY_SIZE)
		memset(key + len, 0, XEN_NETBK_MAX_HASH_KEY_SIZE - len);

	xenvif_flush_hash(vif);

	return XEN_NETIF_CTRL_STATUS_SUCCESS;
}

u32 xenvif_set_hash_mapping_size(struct xenvif *vif, u32 size)
{
	if (size > XEN_NETBK_MAX_HASH_MAPPING_SIZE)
		return XEN_NETIF_CTRL_STATUS_INVALID_PARAMETER;

	vif->hash.size = size;
	memset(vif->hash.mapping, 0, sizeof(u32) * size);

	return XEN_NETIF_CTRL_STATUS_SUCCESS;
}

u32 xenvif_set_hash_mapping(struct xenvif *vif, u32 gref, u32 len,
			    u32 off)
{
	u32 *mapping = &vif->hash.mapping[off];
	struct gnttab_copy copy_op = {
		.source.u.ref = gref,
		.source.domid = vif->domid,
		.dest.u.gmfn = virt_to_gfn(mapping),
		.dest.domid = DOMID_SELF,
		.dest.offset = xen_offset_in_page(mapping),
		.len = len * sizeof(u32),
		.flags = GNTCOPY_source_gref
	};

	if ((off + len > vif->hash.size) || copy_op.len > XEN_PAGE_SIZE)
		return XEN_NETIF_CTRL_STATUS_INVALID_PARAMETER;

	while (len-- != 0)
		if (mapping[off++] >= vif->num_queues)
			return XEN_NETIF_CTRL_STATUS_INVALID_PARAMETER;

	if (len != 0) {
		gnttab_batch_copy(&copy_op, 1);

		if (copy_op.status != GNTST_okay)
			return XEN_NETIF_CTRL_STATUS_INVALID_PARAMETER;
	}

	return XEN_NETIF_CTRL_STATUS_SUCCESS;
}

void xenvif_init_hash(struct xenvif *vif)
{
	if (xenvif_hash_cache_size == 0)
		return;

	spin_lock_init(&vif->hash.cache.lock);
	INIT_LIST_HEAD(&vif->hash.cache.list);
}

void xenvif_deinit_hash(struct xenvif *vif)
{
	xenvif_flush_hash(vif);
}
