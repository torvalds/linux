/*
 * Copyright (c) 2016 Hisilicon Limited.
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

#include <linux/platform_device.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_cache.h>
#include "hns_roce_device.h"

#define HNS_ROCE_PORT_NUM_SHIFT		24
#define HNS_ROCE_VLAN_SL_BIT_MASK	7
#define HNS_ROCE_VLAN_SL_SHIFT		13

int hns_roce_create_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr,
		       u32 flags, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibah->device);
	const struct ib_gid_attr *gid_attr;
	struct device *dev = hr_dev->dev;
	struct hns_roce_ah *ah = to_hr_ah(ibah);
	u16 vlan_tag = 0xffff;
	const struct ib_global_route *grh = rdma_ah_read_grh(ah_attr);
	bool vlan_en = false;
	int ret;

	gid_attr = ah_attr->grh.sgid_attr;
	ret = rdma_read_gid_l2_fields(gid_attr, &vlan_tag, NULL);
	if (ret)
		return ret;

	/* Get mac address */
	memcpy(ah->av.mac, ah_attr->roce.dmac, ETH_ALEN);

	if (vlan_tag < VLAN_CFI_MASK) {
		vlan_en = true;
		vlan_tag |= (rdma_ah_get_sl(ah_attr) &
			     HNS_ROCE_VLAN_SL_BIT_MASK) <<
			     HNS_ROCE_VLAN_SL_SHIFT;
	}

	ah->av.port_pd = cpu_to_le32(to_hr_pd(ibah->pd)->pdn |
				     (rdma_ah_get_port_num(ah_attr) <<
				     HNS_ROCE_PORT_NUM_SHIFT));
	ah->av.gid_index = grh->sgid_index;
	ah->av.vlan = cpu_to_le16(vlan_tag);
	ah->av.vlan_en = vlan_en;
	dev_dbg(dev, "gid_index = 0x%x,vlan = 0x%x\n", ah->av.gid_index,
		ah->av.vlan);

	if (rdma_ah_get_static_rate(ah_attr))
		ah->av.stat_rate = IB_RATE_10_GBPS;

	memcpy(ah->av.dgid, grh->dgid.raw, HNS_ROCE_GID_SIZE);
	ah->av.sl_tclass_flowlabel = cpu_to_le32(rdma_ah_get_sl(ah_attr) <<
						 HNS_ROCE_SL_SHIFT);

	return 0;
}

int hns_roce_query_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr)
{
	struct hns_roce_ah *ah = to_hr_ah(ibah);

	memset(ah_attr, 0, sizeof(*ah_attr));

	rdma_ah_set_sl(ah_attr, (le32_to_cpu(ah->av.sl_tclass_flowlabel) >>
				 HNS_ROCE_SL_SHIFT));
	rdma_ah_set_port_num(ah_attr, (le32_to_cpu(ah->av.port_pd) >>
				       HNS_ROCE_PORT_NUM_SHIFT));
	rdma_ah_set_static_rate(ah_attr, ah->av.stat_rate);
	rdma_ah_set_grh(ah_attr, NULL,
			(le32_to_cpu(ah->av.sl_tclass_flowlabel) &
			 HNS_ROCE_FLOW_LABEL_MASK), ah->av.gid_index,
			ah->av.hop_limit,
			(le32_to_cpu(ah->av.sl_tclass_flowlabel) >>
			 HNS_ROCE_TCLASS_SHIFT));
	rdma_ah_set_dgid_raw(ah_attr, ah->av.dgid);

	return 0;
}

void hns_roce_destroy_ah(struct ib_ah *ah, u32 flags)
{
	return;
}
