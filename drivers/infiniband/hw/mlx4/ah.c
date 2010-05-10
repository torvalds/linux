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

#include <linux/slab.h>

#include "mlx4_ib.h"

struct ib_ah *mlx4_ib_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr)
{
	struct mlx4_dev *dev = to_mdev(pd->device)->dev;
	struct mlx4_ib_ah *ah;

	ah = kmalloc(sizeof *ah, GFP_ATOMIC);
	if (!ah)
		return ERR_PTR(-ENOMEM);

	memset(&ah->av, 0, sizeof ah->av);

	ah->av.port_pd = cpu_to_be32(to_mpd(pd)->pdn | (ah_attr->port_num << 24));
	ah->av.g_slid  = ah_attr->src_path_bits;
	ah->av.dlid    = cpu_to_be16(ah_attr->dlid);
	if (ah_attr->static_rate) {
		ah->av.stat_rate = ah_attr->static_rate + MLX4_STAT_RATE_OFFSET;
		while (ah->av.stat_rate > IB_RATE_2_5_GBPS + MLX4_STAT_RATE_OFFSET &&
		       !(1 << ah->av.stat_rate & dev->caps.stat_rate_support))
			--ah->av.stat_rate;
	}
	ah->av.sl_tclass_flowlabel = cpu_to_be32(ah_attr->sl << 28);
	if (ah_attr->ah_flags & IB_AH_GRH) {
		ah->av.g_slid   |= 0x80;
		ah->av.gid_index = ah_attr->grh.sgid_index;
		ah->av.hop_limit = ah_attr->grh.hop_limit;
		ah->av.sl_tclass_flowlabel |=
			cpu_to_be32((ah_attr->grh.traffic_class << 20) |
				    ah_attr->grh.flow_label);
		memcpy(ah->av.dgid, ah_attr->grh.dgid.raw, 16);
	}

	return &ah->ibah;
}

int mlx4_ib_query_ah(struct ib_ah *ibah, struct ib_ah_attr *ah_attr)
{
	struct mlx4_ib_ah *ah = to_mah(ibah);

	memset(ah_attr, 0, sizeof *ah_attr);
	ah_attr->dlid	       = be16_to_cpu(ah->av.dlid);
	ah_attr->sl	       = be32_to_cpu(ah->av.sl_tclass_flowlabel) >> 28;
	ah_attr->port_num      = be32_to_cpu(ah->av.port_pd) >> 24;
	if (ah->av.stat_rate)
		ah_attr->static_rate = ah->av.stat_rate - MLX4_STAT_RATE_OFFSET;
	ah_attr->src_path_bits = ah->av.g_slid & 0x7F;

	if (mlx4_ib_ah_grh_present(ah)) {
		ah_attr->ah_flags = IB_AH_GRH;

		ah_attr->grh.traffic_class =
			be32_to_cpu(ah->av.sl_tclass_flowlabel) >> 20;
		ah_attr->grh.flow_label =
			be32_to_cpu(ah->av.sl_tclass_flowlabel) & 0xfffff;
		ah_attr->grh.hop_limit  = ah->av.hop_limit;
		ah_attr->grh.sgid_index = ah->av.gid_index;
		memcpy(ah_attr->grh.dgid.raw, ah->av.dgid, 16);
	}

	return 0;
}

int mlx4_ib_destroy_ah(struct ib_ah *ah)
{
	kfree(to_mah(ah));
	return 0;
}
