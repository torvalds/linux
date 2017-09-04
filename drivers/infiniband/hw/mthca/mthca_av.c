/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
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

#include <linux/string.h>
#include <linux/slab.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_cache.h>

#include "mthca_dev.h"

enum {
      MTHCA_RATE_TAVOR_FULL   = 0,
      MTHCA_RATE_TAVOR_1X     = 1,
      MTHCA_RATE_TAVOR_4X     = 2,
      MTHCA_RATE_TAVOR_1X_DDR = 3
};

enum {
      MTHCA_RATE_MEMFREE_FULL    = 0,
      MTHCA_RATE_MEMFREE_QUARTER = 1,
      MTHCA_RATE_MEMFREE_EIGHTH  = 2,
      MTHCA_RATE_MEMFREE_HALF    = 3
};

struct mthca_av {
	__be32 port_pd;
	u8     reserved1;
	u8     g_slid;
	__be16 dlid;
	u8     reserved2;
	u8     gid_index;
	u8     msg_sr;
	u8     hop_limit;
	__be32 sl_tclass_flowlabel;
	__be32 dgid[4];
};

static enum ib_rate memfree_rate_to_ib(u8 mthca_rate, u8 port_rate)
{
	switch (mthca_rate) {
	case MTHCA_RATE_MEMFREE_EIGHTH:
		return mult_to_ib_rate(port_rate >> 3);
	case MTHCA_RATE_MEMFREE_QUARTER:
		return mult_to_ib_rate(port_rate >> 2);
	case MTHCA_RATE_MEMFREE_HALF:
		return mult_to_ib_rate(port_rate >> 1);
	case MTHCA_RATE_MEMFREE_FULL:
	default:
		return mult_to_ib_rate(port_rate);
	}
}

static enum ib_rate tavor_rate_to_ib(u8 mthca_rate, u8 port_rate)
{
	switch (mthca_rate) {
	case MTHCA_RATE_TAVOR_1X:     return IB_RATE_2_5_GBPS;
	case MTHCA_RATE_TAVOR_1X_DDR: return IB_RATE_5_GBPS;
	case MTHCA_RATE_TAVOR_4X:     return IB_RATE_10_GBPS;
	default:		      return mult_to_ib_rate(port_rate);
	}
}

enum ib_rate mthca_rate_to_ib(struct mthca_dev *dev, u8 mthca_rate, u8 port)
{
	if (mthca_is_memfree(dev)) {
		/* Handle old Arbel FW */
		if (dev->limits.stat_rate_support == 0x3 && mthca_rate)
			return IB_RATE_2_5_GBPS;

		return memfree_rate_to_ib(mthca_rate, dev->rate[port - 1]);
	} else
		return tavor_rate_to_ib(mthca_rate, dev->rate[port - 1]);
}

static u8 ib_rate_to_memfree(u8 req_rate, u8 cur_rate)
{
	if (cur_rate <= req_rate)
		return 0;

	/*
	 * Inter-packet delay (IPD) to get from rate X down to a rate
	 * no more than Y is (X - 1) / Y.
	 */
	switch ((cur_rate - 1) / req_rate) {
	case 0:	 return MTHCA_RATE_MEMFREE_FULL;
	case 1:	 return MTHCA_RATE_MEMFREE_HALF;
	case 2:	 /* fall through */
	case 3:	 return MTHCA_RATE_MEMFREE_QUARTER;
	default: return MTHCA_RATE_MEMFREE_EIGHTH;
	}
}

static u8 ib_rate_to_tavor(u8 static_rate)
{
	switch (static_rate) {
	case IB_RATE_2_5_GBPS: return MTHCA_RATE_TAVOR_1X;
	case IB_RATE_5_GBPS:   return MTHCA_RATE_TAVOR_1X_DDR;
	case IB_RATE_10_GBPS:  return MTHCA_RATE_TAVOR_4X;
	default:	       return MTHCA_RATE_TAVOR_FULL;
	}
}

u8 mthca_get_rate(struct mthca_dev *dev, int static_rate, u8 port)
{
	u8 rate;

	if (!static_rate || ib_rate_to_mult(static_rate) >= dev->rate[port - 1])
		return 0;

	if (mthca_is_memfree(dev))
		rate = ib_rate_to_memfree(ib_rate_to_mult(static_rate),
					  dev->rate[port - 1]);
	else
		rate = ib_rate_to_tavor(static_rate);

	if (!(dev->limits.stat_rate_support & (1 << rate)))
		rate = 1;

	return rate;
}

int mthca_create_ah(struct mthca_dev *dev,
		    struct mthca_pd *pd,
		    struct rdma_ah_attr *ah_attr,
		    struct mthca_ah *ah)
{
	u32 index = -1;
	struct mthca_av *av = NULL;

	ah->type = MTHCA_AH_PCI_POOL;

	if (mthca_is_memfree(dev)) {
		ah->av   = kmalloc(sizeof *ah->av, GFP_ATOMIC);
		if (!ah->av)
			return -ENOMEM;

		ah->type = MTHCA_AH_KMALLOC;
		av       = ah->av;
	} else if (!atomic_read(&pd->sqp_count) &&
		 !(dev->mthca_flags & MTHCA_FLAG_DDR_HIDDEN)) {
		index = mthca_alloc(&dev->av_table.alloc);

		/* fall back to allocate in host memory */
		if (index == -1)
			goto on_hca_fail;

		av = kmalloc(sizeof *av, GFP_ATOMIC);
		if (!av)
			goto on_hca_fail;

		ah->type = MTHCA_AH_ON_HCA;
		ah->avdma  = dev->av_table.ddr_av_base +
			index * MTHCA_AV_SIZE;
	}

on_hca_fail:
	if (ah->type == MTHCA_AH_PCI_POOL) {
		ah->av = dma_pool_zalloc(dev->av_table.pool,
					 GFP_ATOMIC, &ah->avdma);
		if (!ah->av)
			return -ENOMEM;

		av = ah->av;
	}

	ah->key = pd->ntmr.ibmr.lkey;

	av->port_pd = cpu_to_be32(pd->pd_num |
				  (rdma_ah_get_port_num(ah_attr) << 24));
	av->g_slid  = rdma_ah_get_path_bits(ah_attr);
	av->dlid    = cpu_to_be16(rdma_ah_get_dlid(ah_attr));
	av->msg_sr  = (3 << 4) | /* 2K message */
		mthca_get_rate(dev, rdma_ah_get_static_rate(ah_attr),
			       rdma_ah_get_port_num(ah_attr));
	av->sl_tclass_flowlabel = cpu_to_be32(rdma_ah_get_sl(ah_attr) << 28);
	if (rdma_ah_get_ah_flags(ah_attr) & IB_AH_GRH) {
		const struct ib_global_route *grh = rdma_ah_read_grh(ah_attr);

		av->g_slid |= 0x80;
		av->gid_index = (rdma_ah_get_port_num(ah_attr) - 1) *
				  dev->limits.gid_table_len +
				  grh->sgid_index;
		av->hop_limit = grh->hop_limit;
		av->sl_tclass_flowlabel |=
			cpu_to_be32((grh->traffic_class << 20) |
				    grh->flow_label);
		memcpy(av->dgid, grh->dgid.raw, 16);
	} else {
		/* Arbel workaround -- low byte of GID must be 2 */
		av->dgid[3] = cpu_to_be32(2);
	}

	if (0) {
		int j;

		mthca_dbg(dev, "Created UDAV at %p/%08lx:\n",
			  av, (unsigned long) ah->avdma);
		for (j = 0; j < 8; ++j)
			printk(KERN_DEBUG "  [%2x] %08x\n",
			       j * 4, be32_to_cpu(((__be32 *) av)[j]));
	}

	if (ah->type == MTHCA_AH_ON_HCA) {
		memcpy_toio(dev->av_table.av_map + index * MTHCA_AV_SIZE,
			    av, MTHCA_AV_SIZE);
		kfree(av);
	}

	return 0;
}

int mthca_destroy_ah(struct mthca_dev *dev, struct mthca_ah *ah)
{
	switch (ah->type) {
	case MTHCA_AH_ON_HCA:
		mthca_free(&dev->av_table.alloc,
			   (ah->avdma - dev->av_table.ddr_av_base) /
			   MTHCA_AV_SIZE);
		break;

	case MTHCA_AH_PCI_POOL:
		dma_pool_free(dev->av_table.pool, ah->av, ah->avdma);
		break;

	case MTHCA_AH_KMALLOC:
		kfree(ah->av);
		break;
	}

	return 0;
}

int mthca_ah_grh_present(struct mthca_ah *ah)
{
	return !!(ah->av->g_slid & 0x80);
}

int mthca_read_ah(struct mthca_dev *dev, struct mthca_ah *ah,
		  struct ib_ud_header *header)
{
	if (ah->type == MTHCA_AH_ON_HCA)
		return -EINVAL;

	header->lrh.service_level   = be32_to_cpu(ah->av->sl_tclass_flowlabel) >> 28;
	header->lrh.destination_lid = ah->av->dlid;
	header->lrh.source_lid      = cpu_to_be16(ah->av->g_slid & 0x7f);
	if (mthca_ah_grh_present(ah)) {
		header->grh.traffic_class =
			(be32_to_cpu(ah->av->sl_tclass_flowlabel) >> 20) & 0xff;
		header->grh.flow_label    =
			ah->av->sl_tclass_flowlabel & cpu_to_be32(0xfffff);
		header->grh.hop_limit     = ah->av->hop_limit;
		ib_get_cached_gid(&dev->ib_dev,
				  be32_to_cpu(ah->av->port_pd) >> 24,
				  ah->av->gid_index % dev->limits.gid_table_len,
				  &header->grh.source_gid, NULL);
		memcpy(header->grh.destination_gid.raw,
		       ah->av->dgid, 16);
	}

	return 0;
}

int mthca_ah_query(struct ib_ah *ibah, struct rdma_ah_attr *attr)
{
	struct mthca_ah *ah   = to_mah(ibah);
	struct mthca_dev *dev = to_mdev(ibah->device);
	u8 port_num = be32_to_cpu(ah->av->port_pd) >> 24;

	/* Only implement for MAD and memfree ah for now. */
	if (ah->type == MTHCA_AH_ON_HCA)
		return -ENOSYS;

	memset(attr, 0, sizeof *attr);
	attr->type = ibah->type;
	rdma_ah_set_dlid(attr, be16_to_cpu(ah->av->dlid));
	rdma_ah_set_sl(attr, be32_to_cpu(ah->av->sl_tclass_flowlabel) >> 28);
	rdma_ah_set_port_num(attr, port_num);
	rdma_ah_set_static_rate(attr,
				mthca_rate_to_ib(dev, ah->av->msg_sr & 0x7,
						 port_num));
	rdma_ah_set_path_bits(attr, ah->av->g_slid & 0x7F);
	if (mthca_ah_grh_present(ah)) {
		u32 tc_fl = be32_to_cpu(ah->av->sl_tclass_flowlabel);

		rdma_ah_set_grh(attr, NULL,
				tc_fl & 0xfffff,
				ah->av->gid_index &
				(dev->limits.gid_table_len - 1),
				ah->av->hop_limit,
				(tc_fl >> 20) & 0xff);
		rdma_ah_set_dgid_raw(attr, ah->av->dgid);
	}

	return 0;
}

int mthca_init_av_table(struct mthca_dev *dev)
{
	int err;

	if (mthca_is_memfree(dev))
		return 0;

	err = mthca_alloc_init(&dev->av_table.alloc,
			       dev->av_table.num_ddr_avs,
			       dev->av_table.num_ddr_avs - 1,
			       0);
	if (err)
		return err;

	dev->av_table.pool = dma_pool_create("mthca_av", &dev->pdev->dev,
					     MTHCA_AV_SIZE,
					     MTHCA_AV_SIZE, 0);
	if (!dev->av_table.pool)
		goto out_free_alloc;

	if (!(dev->mthca_flags & MTHCA_FLAG_DDR_HIDDEN)) {
		dev->av_table.av_map = ioremap(pci_resource_start(dev->pdev, 4) +
					       dev->av_table.ddr_av_base -
					       dev->ddr_start,
					       dev->av_table.num_ddr_avs *
					       MTHCA_AV_SIZE);
		if (!dev->av_table.av_map)
			goto out_free_pool;
	} else
		dev->av_table.av_map = NULL;

	return 0;

 out_free_pool:
	dma_pool_destroy(dev->av_table.pool);

 out_free_alloc:
	mthca_alloc_cleanup(&dev->av_table.alloc);
	return -ENOMEM;
}

void mthca_cleanup_av_table(struct mthca_dev *dev)
{
	if (mthca_is_memfree(dev))
		return;

	if (dev->av_table.av_map)
		iounmap(dev->av_table.av_map);
	dma_pool_destroy(dev->av_table.pool);
	mthca_alloc_cleanup(&dev->av_table.alloc);
}
