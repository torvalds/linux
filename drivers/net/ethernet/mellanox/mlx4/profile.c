/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2006, 2007 Cisco Systems, Inc. All rights reserved.
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

#include "mlx4.h"
#include "fw.h"

enum {
	MLX4_RES_QP,
	MLX4_RES_RDMARC,
	MLX4_RES_ALTC,
	MLX4_RES_AUXC,
	MLX4_RES_SRQ,
	MLX4_RES_CQ,
	MLX4_RES_EQ,
	MLX4_RES_DMPT,
	MLX4_RES_CMPT,
	MLX4_RES_MTT,
	MLX4_RES_MCG,
	MLX4_RES_NUM
};

static const char *res_name[] = {
	[MLX4_RES_QP]		= "QP",
	[MLX4_RES_RDMARC]	= "RDMARC",
	[MLX4_RES_ALTC]		= "ALTC",
	[MLX4_RES_AUXC]		= "AUXC",
	[MLX4_RES_SRQ]		= "SRQ",
	[MLX4_RES_CQ]		= "CQ",
	[MLX4_RES_EQ]		= "EQ",
	[MLX4_RES_DMPT]		= "DMPT",
	[MLX4_RES_CMPT]		= "CMPT",
	[MLX4_RES_MTT]		= "MTT",
	[MLX4_RES_MCG]		= "MCG",
};

u64 mlx4_make_profile(struct mlx4_dev *dev,
		      struct mlx4_profile *request,
		      struct mlx4_dev_cap *dev_cap,
		      struct mlx4_init_hca_param *init_hca)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource {
		u64 size;
		u64 start;
		int type;
		u32 num;
		int log_num;
	};

	u64 total_size = 0;
	struct mlx4_resource *profile;
	struct sysinfo si;
	int i, j;

	profile = kcalloc(MLX4_RES_NUM, sizeof(*profile), GFP_KERNEL);
	if (!profile)
		return -ENOMEM;

	/*
	 * We want to scale the number of MTTs with the size of the
	 * system memory, since it makes sense to register a lot of
	 * memory on a system with a lot of memory.  As a heuristic,
	 * make sure we have enough MTTs to cover twice the system
	 * memory (with PAGE_SIZE entries).
	 *
	 * This number has to be a power of two and fit into 32 bits
	 * due to device limitations, so cap this at 2^31 as well.
	 * That limits us to 8TB of memory registration per HCA with
	 * 4KB pages, which is probably OK for the next few months.
	 */
	si_meminfo(&si);
	request->num_mtt =
		roundup_pow_of_two(max_t(unsigned, request->num_mtt,
					 min(1UL << (31 - log_mtts_per_seg),
					     (si.totalram << 1) >> log_mtts_per_seg)));


	profile[MLX4_RES_QP].size     = dev_cap->qpc_entry_sz;
	profile[MLX4_RES_RDMARC].size = dev_cap->rdmarc_entry_sz;
	profile[MLX4_RES_ALTC].size   = dev_cap->altc_entry_sz;
	profile[MLX4_RES_AUXC].size   = dev_cap->aux_entry_sz;
	profile[MLX4_RES_SRQ].size    = dev_cap->srq_entry_sz;
	profile[MLX4_RES_CQ].size     = dev_cap->cqc_entry_sz;
	profile[MLX4_RES_EQ].size     = dev_cap->eqc_entry_sz;
	profile[MLX4_RES_DMPT].size   = dev_cap->dmpt_entry_sz;
	profile[MLX4_RES_CMPT].size   = dev_cap->cmpt_entry_sz;
	profile[MLX4_RES_MTT].size    = dev_cap->mtt_entry_sz;
	profile[MLX4_RES_MCG].size    = mlx4_get_mgm_entry_size(dev);

	profile[MLX4_RES_QP].num      = request->num_qp;
	profile[MLX4_RES_RDMARC].num  = request->num_qp * request->rdmarc_per_qp;
	profile[MLX4_RES_ALTC].num    = request->num_qp;
	profile[MLX4_RES_AUXC].num    = request->num_qp;
	profile[MLX4_RES_SRQ].num     = request->num_srq;
	profile[MLX4_RES_CQ].num      = request->num_cq;
	profile[MLX4_RES_EQ].num = mlx4_is_mfunc(dev) ? dev->phys_caps.num_phys_eqs :
					min_t(unsigned, dev_cap->max_eqs, MAX_MSIX);
	profile[MLX4_RES_DMPT].num    = request->num_mpt;
	profile[MLX4_RES_CMPT].num    = MLX4_NUM_CMPTS;
	profile[MLX4_RES_MTT].num     = request->num_mtt * (1 << log_mtts_per_seg);
	profile[MLX4_RES_MCG].num     = request->num_mcg;

	for (i = 0; i < MLX4_RES_NUM; ++i) {
		profile[i].type     = i;
		profile[i].num      = roundup_pow_of_two(profile[i].num);
		profile[i].log_num  = ilog2(profile[i].num);
		profile[i].size    *= profile[i].num;
		profile[i].size     = max(profile[i].size, (u64) PAGE_SIZE);
	}

	/*
	 * Sort the resources in decreasing order of size.  Since they
	 * all have sizes that are powers of 2, we'll be able to keep
	 * resources aligned to their size and pack them without gaps
	 * using the sorted order.
	 */
	for (i = MLX4_RES_NUM; i > 0; --i)
		for (j = 1; j < i; ++j) {
			if (profile[j].size > profile[j - 1].size)
				swap(profile[j], profile[j - 1]);
		}

	for (i = 0; i < MLX4_RES_NUM; ++i) {
		if (profile[i].size) {
			profile[i].start = total_size;
			total_size	+= profile[i].size;
		}

		if (total_size > dev_cap->max_icm_sz) {
			mlx4_err(dev, "Profile requires 0x%llx bytes; won't fit in 0x%llx bytes of context memory\n",
				 (unsigned long long) total_size,
				 (unsigned long long) dev_cap->max_icm_sz);
			kfree(profile);
			return -ENOMEM;
		}

		if (profile[i].size)
			mlx4_dbg(dev, "  profile[%2d] (%6s): 2^%02d entries @ 0x%10llx, size 0x%10llx\n",
				 i, res_name[profile[i].type],
				 profile[i].log_num,
				 (unsigned long long) profile[i].start,
				 (unsigned long long) profile[i].size);
	}

	mlx4_dbg(dev, "HCA context memory: reserving %d KB\n",
		 (int) (total_size >> 10));

	for (i = 0; i < MLX4_RES_NUM; ++i) {
		switch (profile[i].type) {
		case MLX4_RES_QP:
			dev->caps.num_qps     = profile[i].num;
			init_hca->qpc_base    = profile[i].start;
			init_hca->log_num_qps = profile[i].log_num;
			break;
		case MLX4_RES_RDMARC:
			for (priv->qp_table.rdmarc_shift = 0;
			     request->num_qp << priv->qp_table.rdmarc_shift < profile[i].num;
			     ++priv->qp_table.rdmarc_shift)
				; /* nothing */
			dev->caps.max_qp_dest_rdma = 1 << priv->qp_table.rdmarc_shift;
			priv->qp_table.rdmarc_base   = (u32) profile[i].start;
			init_hca->rdmarc_base	     = profile[i].start;
			init_hca->log_rd_per_qp	     = priv->qp_table.rdmarc_shift;
			break;
		case MLX4_RES_ALTC:
			init_hca->altc_base = profile[i].start;
			break;
		case MLX4_RES_AUXC:
			init_hca->auxc_base = profile[i].start;
			break;
		case MLX4_RES_SRQ:
			dev->caps.num_srqs     = profile[i].num;
			init_hca->srqc_base    = profile[i].start;
			init_hca->log_num_srqs = profile[i].log_num;
			break;
		case MLX4_RES_CQ:
			dev->caps.num_cqs     = profile[i].num;
			init_hca->cqc_base    = profile[i].start;
			init_hca->log_num_cqs = profile[i].log_num;
			break;
		case MLX4_RES_EQ:
			if (dev_cap->flags2 & MLX4_DEV_CAP_FLAG2_SYS_EQS) {
				init_hca->log_num_eqs = 0x1f;
				init_hca->eqc_base    = profile[i].start;
				init_hca->num_sys_eqs = dev_cap->num_sys_eqs;
			} else {
				dev->caps.num_eqs     = roundup_pow_of_two(
								min_t(unsigned,
								      dev_cap->max_eqs,
								      MAX_MSIX));
				init_hca->eqc_base    = profile[i].start;
				init_hca->log_num_eqs = ilog2(dev->caps.num_eqs);
			}
			break;
		case MLX4_RES_DMPT:
			dev->caps.num_mpts	= profile[i].num;
			priv->mr_table.mpt_base = profile[i].start;
			init_hca->dmpt_base	= profile[i].start;
			init_hca->log_mpt_sz	= profile[i].log_num;
			break;
		case MLX4_RES_CMPT:
			init_hca->cmpt_base	 = profile[i].start;
			break;
		case MLX4_RES_MTT:
			dev->caps.num_mtts	 = profile[i].num;
			priv->mr_table.mtt_base	 = profile[i].start;
			init_hca->mtt_base	 = profile[i].start;
			break;
		case MLX4_RES_MCG:
			init_hca->mc_base	  = profile[i].start;
			init_hca->log_mc_entry_sz =
					ilog2(mlx4_get_mgm_entry_size(dev));
			init_hca->log_mc_table_sz = profile[i].log_num;
			if (dev->caps.steering_mode ==
			    MLX4_STEERING_MODE_DEVICE_MANAGED) {
				dev->caps.num_mgms = profile[i].num;
			} else {
				init_hca->log_mc_hash_sz =
						profile[i].log_num - 1;
				dev->caps.num_mgms = profile[i].num >> 1;
				dev->caps.num_amgms = profile[i].num >> 1;
			}
			break;
		default:
			break;
		}
	}

	/*
	 * PDs don't take any HCA memory, but we assign them as part
	 * of the HCA profile anyway.
	 */
	dev->caps.num_pds = MLX4_NUM_PDS;

	kfree(profile);
	return total_size;
}
