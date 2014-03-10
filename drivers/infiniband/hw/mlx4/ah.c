/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <rdma/ib_addr.h>
#include <rdma/ib_cache.h>

#include <linux/slab.h>
#include <linux/inet.h>
#include <linux/string.h>

#include "mlx4_ib.h"

static struct ib_ah *create_ib_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr,
				  struct mlx4_ib_ah *ah)
{
	struct mlx4_dev *dev = to_mdev(pd->device)->dev;

	ah->av.ib.port_pd = cpu_to_be32(to_mpd(pd)->pdn | (ah_attr->port_num << 24));
	ah->av.ib.g_slid  = ah_attr->src_path_bits;
	if (ah_attr->ah_flags & IB_AH_GRH) {
		ah->av.ib.g_slid   |= 0x80;
		ah->av.ib.gid_index = ah_attr->grh.sgid_index;
		ah->av.ib.hop_limit = ah_attr->grh.hop_limit;
		ah->av.ib.sl_tclass_flowlabel |=
			cpu_to_be32((ah_attr->grh.traffic_class << 20) |
				    ah_attr->grh.flow_label);
		memcpy(ah->av.ib.dgid, ah_attr->grh.dgid.raw, 16);
	}

	ah->av.ib.dlid    = cpu_to_be16(ah_attr->dlid);
	if (ah_attr->static_rate) {
		ah->av.ib.stat_rate = ah_attr->static_rate + MLX4_STAT_RATE_OFFSET;
		while (ah->av.ib.stat_rate > IB_RATE_2_5_GBPS + MLX4_STAT_RATE_OFFSET &&
		       !(1 << ah->av.ib.stat_rate & dev->caps.stat_rate_support))
			--ah->av.ib.stat_rate;
	}
	ah->av.ib.sl_tclass_flowlabel = cpu_to_be32(ah_attr->sl << 28);

	return &ah->ibah;
}

static struct ib_ah *create_iboe_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr,
				    struct mlx4_ib_ah *ah)
{
	struct mlx4_ib_dev *ibdev = to_mdev(pd->device);
	struct mlx4_dev *dev = ibdev->dev;
	int is_mcast;
	struct in6_addr in6;
	u16 vlan_tag;

	memcpy(&in6, ah_attr->grh.dgid.raw, sizeof(in6));
	if (rdma_is_multicast_addr(&in6)) {
		is_mcast = 1;
		rdma_get_mcast_mac(&in6, ah->av.eth.mac);
	} else {
		memcpy(ah->av.eth.mac, ah_attr->dmac, ETH_ALEN);
	}
	vlan_tag = ah_attr->vlan_id;
	if (vlan_tag < 0x1000)
		vlan_tag |= (ah_attr->sl & 7) << 13;
	ah->av.eth.port_pd = cpu_to_be32(to_mpd(pd)->pdn | (ah_attr->port_num << 24));
	ah->av.eth.gid_index = ah_attr->grh.sgid_index;
	ah->av.eth.vlan = cpu_to_be16(vlan_tag);
	if (ah_attr->static_rate) {
		ah->av.eth.stat_rate = ah_attr->static_rate + MLX4_STAT_RATE_OFFSET;
		while (ah->av.eth.stat_rate > IB_RATE_2_5_GBPS + MLX4_STAT_RATE_OFFSET &&
		       !(1 << ah->av.eth.stat_rate & dev->caps.stat_rate_support))
			--ah->av.eth.stat_rate;
	}

	/*
	 * HW requires multicast LID so we just choose one.
	 */
	if (is_mcast)
		ah->av.ib.dlid = cpu_to_be16(0xc000);

	memcpy(ah->av.eth.dgid, ah_attr->grh.dgid.raw, 16);
	ah->av.eth.sl_tclass_flowlabel = cpu_to_be32(ah_attr->sl << 29);

	return &ah->ibah;
}

struct ib_ah *mlx4_ib_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr)
{
	struct mlx4_ib_ah *ah;
	struct ib_ah *ret;

	ah = kzalloc(sizeof *ah, GFP_ATOMIC);
	if (!ah)
		return ERR_PTR(-ENOMEM);

	if (rdma_port_get_link_layer(pd->device, ah_attr->port_num) == IB_LINK_LAYER_ETHERNET) {
		if (!(ah_attr->ah_flags & IB_AH_GRH)) {
			ret = ERR_PTR(-EINVAL);
		} else {
			/*
			 * TBD: need to handle the case when we get
			 * called in an atomic context and there we
			 * might sleep.  We don't expect this
			 * currently since we're working with link
			 * local addresses which we can translate
			 * without going to sleep.
			 */
			ret = create_iboe_ah(pd, ah_attr, ah);
		}

		if (IS_ERR(ret))
			kfree(ah);

		return ret;
	} else
		return create_ib_ah(pd, ah_attr, ah); /* never fails */
}

int mlx4_ib_query_ah(struct ib_ah *ibah, struct ib_ah_attr *ah_attr)
{
	struct mlx4_ib_ah *ah = to_mah(ibah);
	enum rdma_link_layer ll;

	memset(ah_attr, 0, sizeof *ah_attr);
	ah_attr->sl = be32_to_cpu(ah->av.ib.sl_tclass_flowlabel) >> 28;
	ah_attr->port_num = be32_to_cpu(ah->av.ib.port_pd) >> 24;
	ll = rdma_port_get_link_layer(ibah->device, ah_attr->port_num);
	ah_attr->dlid = ll == IB_LINK_LAYER_INFINIBAND ? be16_to_cpu(ah->av.ib.dlid) : 0;
	if (ah->av.ib.stat_rate)
		ah_attr->static_rate = ah->av.ib.stat_rate - MLX4_STAT_RATE_OFFSET;
	ah_attr->src_path_bits = ah->av.ib.g_slid & 0x7F;

	if (mlx4_ib_ah_grh_present(ah)) {
		ah_attr->ah_flags = IB_AH_GRH;

		ah_attr->grh.traffic_class =
			be32_to_cpu(ah->av.ib.sl_tclass_flowlabel) >> 20;
		ah_attr->grh.flow_label =
			be32_to_cpu(ah->av.ib.sl_tclass_flowlabel) & 0xfffff;
		ah_attr->grh.hop_limit  = ah->av.ib.hop_limit;
		ah_attr->grh.sgid_index = ah->av.ib.gid_index;
		memcpy(ah_attr->grh.dgid.raw, ah->av.ib.dgid, 16);
	}

	return 0;
}

int mlx4_ib_destroy_ah(struct ib_ah *ah)
{
	kfree(to_mah(ah));
	return 0;
}
