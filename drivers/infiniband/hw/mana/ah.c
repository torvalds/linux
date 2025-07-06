// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Microsoft Corporation. All rights reserved.
 */

#include "mana_ib.h"

int mana_ib_create_ah(struct ib_ah *ibah, struct rdma_ah_init_attr *attr,
		      struct ib_udata *udata)
{
	struct mana_ib_dev *mdev = container_of(ibah->device, struct mana_ib_dev, ib_dev);
	struct mana_ib_ah *ah = container_of(ibah, struct mana_ib_ah, ibah);
	struct rdma_ah_attr *ah_attr = attr->ah_attr;
	const struct ib_global_route *grh;
	enum rdma_network_type ntype;

	if (ah_attr->type != RDMA_AH_ATTR_TYPE_ROCE ||
	    !(rdma_ah_get_ah_flags(ah_attr) & IB_AH_GRH))
		return -EINVAL;

	if (udata)
		return -EINVAL;

	ah->av = dma_pool_zalloc(mdev->av_pool, GFP_ATOMIC, &ah->dma_handle);
	if (!ah->av)
		return -ENOMEM;

	grh = rdma_ah_read_grh(ah_attr);
	ntype = rdma_gid_attr_network_type(grh->sgid_attr);

	copy_in_reverse(ah->av->dest_mac, ah_attr->roce.dmac, ETH_ALEN);
	ah->av->udp_src_port = rdma_flow_label_to_udp_sport(grh->flow_label);
	ah->av->hop_limit = grh->hop_limit;
	ah->av->dscp = (grh->traffic_class >> 2) & 0x3f;
	ah->av->is_ipv6 = (ntype == RDMA_NETWORK_IPV6);

	if (ah->av->is_ipv6) {
		copy_in_reverse(ah->av->dest_ip, grh->dgid.raw, 16);
		copy_in_reverse(ah->av->src_ip, grh->sgid_attr->gid.raw, 16);
	} else {
		ah->av->dest_ip[10] = 0xFF;
		ah->av->dest_ip[11] = 0xFF;
		copy_in_reverse(&ah->av->dest_ip[12], &grh->dgid.raw[12], 4);
		copy_in_reverse(&ah->av->src_ip[12], &grh->sgid_attr->gid.raw[12], 4);
	}

	return 0;
}

int mana_ib_destroy_ah(struct ib_ah *ibah, u32 flags)
{
	struct mana_ib_dev *mdev = container_of(ibah->device, struct mana_ib_dev, ib_dev);
	struct mana_ib_ah *ah = container_of(ibah, struct mana_ib_ah, ibah);

	dma_pool_free(mdev->av_pool, ah->av, ah->dma_handle);

	return 0;
}
