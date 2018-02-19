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
#include <linux/mlx4/driver.h>

#include "mlx4_ib.h"

static struct ib_ah *create_ib_ah(struct ib_pd *pd,
				  struct rdma_ah_attr *ah_attr,
				  struct mlx4_ib_ah *ah)
{
	struct mlx4_dev *dev = to_mdev(pd->device)->dev;

	ah->av.ib.port_pd = cpu_to_be32(to_mpd(pd)->pdn |
			    (rdma_ah_get_port_num(ah_attr) << 24));
	ah->av.ib.g_slid  = rdma_ah_get_path_bits(ah_attr);
	ah->av.ib.sl_tclass_flowlabel =
			cpu_to_be32(rdma_ah_get_sl(ah_attr) << 28);
	if (rdma_ah_get_ah_flags(ah_attr) & IB_AH_GRH) {
		const struct ib_global_route *grh = rdma_ah_read_grh(ah_attr);

		ah->av.ib.g_slid   |= 0x80;
		ah->av.ib.gid_index = grh->sgid_index;
		ah->av.ib.hop_limit = grh->hop_limit;
		ah->av.ib.sl_tclass_flowlabel |=
			cpu_to_be32((grh->traffic_class << 20) |
				    grh->flow_label);
		memcpy(ah->av.ib.dgid, grh->dgid.raw, 16);
	}

	ah->av.ib.dlid = cpu_to_be16(rdma_ah_get_dlid(ah_attr));
	if (rdma_ah_get_static_rate(ah_attr)) {
		u8 static_rate = rdma_ah_get_static_rate(ah_attr) +
					MLX4_STAT_RATE_OFFSET;

		while (static_rate > IB_RATE_2_5_GBPS + MLX4_STAT_RATE_OFFSET &&
		       !(1 << static_rate & dev->caps.stat_rate_support))
			--static_rate;
		ah->av.ib.stat_rate = static_rate;
	}

	return &ah->ibah;
}

static struct ib_ah *create_iboe_ah(struct ib_pd *pd,
				    struct rdma_ah_attr *ah_attr,
				    struct mlx4_ib_ah *ah)
{
	struct mlx4_ib_dev *ibdev = to_mdev(pd->device);
	struct mlx4_dev *dev = ibdev->dev;
	int is_mcast = 0;
	struct in6_addr in6;
	u16 vlan_tag = 0xffff;
	union ib_gid sgid;
	struct ib_gid_attr gid_attr;
	const struct ib_global_route *grh = rdma_ah_read_grh(ah_attr);
	int ret;

	memcpy(&in6, grh->dgid.raw, sizeof(in6));
	if (rdma_is_multicast_addr(&in6))
		is_mcast = 1;

	memcpy(ah->av.eth.mac, ah_attr->roce.dmac, ETH_ALEN);
	ret = ib_get_cached_gid(pd->device, rdma_ah_get_port_num(ah_attr),
				grh->sgid_index, &sgid, &gid_attr);
	if (ret)
		return ERR_PTR(ret);
	eth_zero_addr(ah->av.eth.s_mac);
	if (gid_attr.ndev) {
		if (is_vlan_dev(gid_attr.ndev))
			vlan_tag = vlan_dev_vlan_id(gid_attr.ndev);
		memcpy(ah->av.eth.s_mac, gid_attr.ndev->dev_addr, ETH_ALEN);
		dev_put(gid_attr.ndev);
	}
	if (vlan_tag < 0x1000)
		vlan_tag |= (rdma_ah_get_sl(ah_attr) & 7) << 13;
	ah->av.eth.port_pd = cpu_to_be32(to_mpd(pd)->pdn |
					 (rdma_ah_get_port_num(ah_attr) << 24));
	ret = mlx4_ib_gid_index_to_real_index(ibdev,
					      rdma_ah_get_port_num(ah_attr),
					      grh->sgid_index);
	if (ret < 0)
		return ERR_PTR(ret);
	ah->av.eth.gid_index = ret;
	ah->av.eth.vlan = cpu_to_be16(vlan_tag);
	ah->av.eth.hop_limit = grh->hop_limit;
	if (rdma_ah_get_static_rate(ah_attr)) {
		ah->av.eth.stat_rate = rdma_ah_get_static_rate(ah_attr) +
					MLX4_STAT_RATE_OFFSET;
		while (ah->av.eth.stat_rate > IB_RATE_2_5_GBPS + MLX4_STAT_RATE_OFFSET &&
		       !(1 << ah->av.eth.stat_rate & dev->caps.stat_rate_support))
			--ah->av.eth.stat_rate;
	}
	ah->av.eth.sl_tclass_flowlabel |=
			cpu_to_be32((grh->traffic_class << 20) |
				    grh->flow_label);
	/*
	 * HW requires multicast LID so we just choose one.
	 */
	if (is_mcast)
		ah->av.ib.dlid = cpu_to_be16(0xc000);

	memcpy(ah->av.eth.dgid, grh->dgid.raw, 16);
	ah->av.eth.sl_tclass_flowlabel |= cpu_to_be32(rdma_ah_get_sl(ah_attr)
						      << 29);
	return &ah->ibah;
}

struct ib_ah *mlx4_ib_create_ah(struct ib_pd *pd, struct rdma_ah_attr *ah_attr,
				struct ib_udata *udata)

{
	struct mlx4_ib_ah *ah;
	struct ib_ah *ret;

	ah = kzalloc(sizeof *ah, GFP_ATOMIC);
	if (!ah)
		return ERR_PTR(-ENOMEM);

	if (ah_attr->type == RDMA_AH_ATTR_TYPE_ROCE) {
		if (!(rdma_ah_get_ah_flags(ah_attr) & IB_AH_GRH)) {
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

int mlx4_ib_query_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr)
{
	struct mlx4_ib_ah *ah = to_mah(ibah);
	int port_num = be32_to_cpu(ah->av.ib.port_pd) >> 24;

	memset(ah_attr, 0, sizeof *ah_attr);
	ah_attr->type = ibah->type;

	if (ah_attr->type == RDMA_AH_ATTR_TYPE_ROCE) {
		rdma_ah_set_dlid(ah_attr, 0);
		rdma_ah_set_sl(ah_attr,
			       be32_to_cpu(ah->av.eth.sl_tclass_flowlabel)
			       >> 29);
	} else {
		rdma_ah_set_dlid(ah_attr, be16_to_cpu(ah->av.ib.dlid));
		rdma_ah_set_sl(ah_attr,
			       be32_to_cpu(ah->av.ib.sl_tclass_flowlabel)
			       >> 28);
	}

	rdma_ah_set_port_num(ah_attr, port_num);
	if (ah->av.ib.stat_rate)
		rdma_ah_set_static_rate(ah_attr,
					ah->av.ib.stat_rate -
					MLX4_STAT_RATE_OFFSET);
	rdma_ah_set_path_bits(ah_attr, ah->av.ib.g_slid & 0x7F);
	if (mlx4_ib_ah_grh_present(ah)) {
		u32 tc_fl = be32_to_cpu(ah->av.ib.sl_tclass_flowlabel);

		rdma_ah_set_grh(ah_attr, NULL,
				tc_fl & 0xfffff, ah->av.ib.gid_index,
				ah->av.ib.hop_limit,
				tc_fl >> 20);
		rdma_ah_set_dgid_raw(ah_attr, ah->av.ib.dgid);
	}

	return 0;
}

int mlx4_ib_destroy_ah(struct ib_ah *ah)
{
	kfree(to_mah(ah));
	return 0;
}
