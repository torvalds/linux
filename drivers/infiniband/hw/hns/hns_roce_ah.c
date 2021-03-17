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
#include <linux/pci.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_cache.h>
#include "hns_roce_device.h"

static inline u16 get_ah_udp_sport(const struct rdma_ah_attr *ah_attr)
{
	u32 fl = ah_attr->grh.flow_label;
	u16 sport;

	if (!fl)
		sport = get_random_u32() %
			(IB_ROCE_UDP_ENCAP_VALID_PORT_MAX + 1 -
			 IB_ROCE_UDP_ENCAP_VALID_PORT_MIN) +
			IB_ROCE_UDP_ENCAP_VALID_PORT_MIN;
	else
		sport = rdma_flow_label_to_udp_sport(fl);

	return sport;
}

int hns_roce_create_ah(struct ib_ah *ibah, struct rdma_ah_init_attr *init_attr,
		       struct ib_udata *udata)
{
	struct rdma_ah_attr *ah_attr = init_attr->ah_attr;
	const struct ib_global_route *grh = rdma_ah_read_grh(ah_attr);
	struct hns_roce_dev *hr_dev = to_hr_dev(ibah->device);
	struct hns_roce_ah *ah = to_hr_ah(ibah);
	int ret = 0;

	ah->av.port = rdma_ah_get_port_num(ah_attr);
	ah->av.gid_index = grh->sgid_index;

	if (rdma_ah_get_static_rate(ah_attr))
		ah->av.stat_rate = IB_RATE_10_GBPS;

	ah->av.hop_limit = grh->hop_limit;
	ah->av.flowlabel = grh->flow_label;
	ah->av.udp_sport = get_ah_udp_sport(ah_attr);
	ah->av.sl = rdma_ah_get_sl(ah_attr);
	ah->av.tclass = get_tclass(grh);

	memcpy(ah->av.dgid, grh->dgid.raw, HNS_ROCE_GID_SIZE);
	memcpy(ah->av.mac, ah_attr->roce.dmac, ETH_ALEN);

	/* HIP08 needs to record vlan info in Address Vector */
	if (hr_dev->pci_dev->revision <= PCI_REVISION_ID_HIP08) {
		ret = rdma_read_gid_l2_fields(ah_attr->grh.sgid_attr,
					      &ah->av.vlan_id, NULL);
		if (ret)
			return ret;

		ah->av.vlan_en = ah->av.vlan_id < VLAN_N_VID;
	}

	return ret;
}

int hns_roce_query_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr)
{
	struct hns_roce_ah *ah = to_hr_ah(ibah);

	memset(ah_attr, 0, sizeof(*ah_attr));

	rdma_ah_set_sl(ah_attr, ah->av.sl);
	rdma_ah_set_port_num(ah_attr, ah->av.port);
	rdma_ah_set_static_rate(ah_attr, ah->av.stat_rate);
	rdma_ah_set_grh(ah_attr, NULL, ah->av.flowlabel,
			ah->av.gid_index, ah->av.hop_limit, ah->av.tclass);
	rdma_ah_set_dgid_raw(ah_attr, ah->av.dgid);

	return 0;
}
