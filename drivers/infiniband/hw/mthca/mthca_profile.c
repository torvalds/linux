/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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
 *
 * $Id: mthca_profile.c 1349 2004-12-16 21:09:43Z roland $
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "mthca_profile.h"

enum {
	MTHCA_RES_QP,
	MTHCA_RES_EEC,
	MTHCA_RES_SRQ,
	MTHCA_RES_CQ,
	MTHCA_RES_EQP,
	MTHCA_RES_EEEC,
	MTHCA_RES_EQ,
	MTHCA_RES_RDB,
	MTHCA_RES_MCG,
	MTHCA_RES_MPT,
	MTHCA_RES_MTT,
	MTHCA_RES_UAR,
	MTHCA_RES_UDAV,
	MTHCA_RES_UARC,
	MTHCA_RES_NUM
};

enum {
	MTHCA_NUM_EQS = 32,
	MTHCA_NUM_PDS = 1 << 15
};

u64 mthca_make_profile(struct mthca_dev *dev,
		       struct mthca_profile *request,
		       struct mthca_dev_lim *dev_lim,
		       struct mthca_init_hca_param *init_hca)
{
	struct mthca_resource {
		u64 size;
		u64 start;
		int type;
		int num;
		int log_num;
	};

	u64 mem_base, mem_avail;
	u64 total_size = 0;
	struct mthca_resource *profile;
	struct mthca_resource tmp;
	int i, j;

	profile = kzalloc(MTHCA_RES_NUM * sizeof *profile, GFP_KERNEL);
	if (!profile)
		return -ENOMEM;

	profile[MTHCA_RES_QP].size   = dev_lim->qpc_entry_sz;
	profile[MTHCA_RES_EEC].size  = dev_lim->eec_entry_sz;
	profile[MTHCA_RES_SRQ].size  = dev_lim->srq_entry_sz;
	profile[MTHCA_RES_CQ].size   = dev_lim->cqc_entry_sz;
	profile[MTHCA_RES_EQP].size  = dev_lim->eqpc_entry_sz;
	profile[MTHCA_RES_EEEC].size = dev_lim->eeec_entry_sz;
	profile[MTHCA_RES_EQ].size   = dev_lim->eqc_entry_sz;
	profile[MTHCA_RES_RDB].size  = MTHCA_RDB_ENTRY_SIZE;
	profile[MTHCA_RES_MCG].size  = MTHCA_MGM_ENTRY_SIZE;
	profile[MTHCA_RES_MPT].size  = dev_lim->mpt_entry_sz;
	profile[MTHCA_RES_MTT].size  = MTHCA_MTT_SEG_SIZE;
	profile[MTHCA_RES_UAR].size  = dev_lim->uar_scratch_entry_sz;
	profile[MTHCA_RES_UDAV].size = MTHCA_AV_SIZE;
	profile[MTHCA_RES_UARC].size = request->uarc_size;

	profile[MTHCA_RES_QP].num    = request->num_qp;
	profile[MTHCA_RES_SRQ].num   = request->num_srq;
	profile[MTHCA_RES_EQP].num   = request->num_qp;
	profile[MTHCA_RES_RDB].num   = request->num_qp * request->rdb_per_qp;
	profile[MTHCA_RES_CQ].num    = request->num_cq;
	profile[MTHCA_RES_EQ].num    = MTHCA_NUM_EQS;
	profile[MTHCA_RES_MCG].num   = request->num_mcg;
	profile[MTHCA_RES_MPT].num   = request->num_mpt;
	profile[MTHCA_RES_MTT].num   = request->num_mtt;
	profile[MTHCA_RES_UAR].num   = request->num_uar;
	profile[MTHCA_RES_UARC].num  = request->num_uar;
	profile[MTHCA_RES_UDAV].num  = request->num_udav;

	for (i = 0; i < MTHCA_RES_NUM; ++i) {
		profile[i].type     = i;
		profile[i].log_num  = max(ffs(profile[i].num) - 1, 0);
		profile[i].size    *= profile[i].num;
		if (mthca_is_memfree(dev))
			profile[i].size = max(profile[i].size, (u64) PAGE_SIZE);
	}

	if (mthca_is_memfree(dev)) {
		mem_base  = 0;
		mem_avail = dev_lim->hca.arbel.max_icm_sz;
	} else {
		mem_base  = dev->ddr_start;
		mem_avail = dev->fw.tavor.fw_start - dev->ddr_start;
	}

	/*
	 * Sort the resources in decreasing order of size.  Since they
	 * all have sizes that are powers of 2, we'll be able to keep
	 * resources aligned to their size and pack them without gaps
	 * using the sorted order.
	 */
	for (i = MTHCA_RES_NUM; i > 0; --i)
		for (j = 1; j < i; ++j) {
			if (profile[j].size > profile[j - 1].size) {
				tmp            = profile[j];
				profile[j]     = profile[j - 1];
				profile[j - 1] = tmp;
			}
		}

	for (i = 0; i < MTHCA_RES_NUM; ++i) {
		if (profile[i].size) {
			profile[i].start = mem_base + total_size;
			total_size      += profile[i].size;
		}
		if (total_size > mem_avail) {
			mthca_err(dev, "Profile requires 0x%llx bytes; "
				  "won't fit in 0x%llx bytes of context memory.\n",
				  (unsigned long long) total_size,
				  (unsigned long long) mem_avail);
			kfree(profile);
			return -ENOMEM;
		}

		if (profile[i].size)
			mthca_dbg(dev, "profile[%2d]--%2d/%2d @ 0x%16llx "
				  "(size 0x%8llx)\n",
				  i, profile[i].type, profile[i].log_num,
				  (unsigned long long) profile[i].start,
				  (unsigned long long) profile[i].size);
	}

	if (mthca_is_memfree(dev))
		mthca_dbg(dev, "HCA context memory: reserving %d KB\n",
			  (int) (total_size >> 10));
	else
		mthca_dbg(dev, "HCA memory: allocated %d KB/%d KB (%d KB free)\n",
			  (int) (total_size >> 10), (int) (mem_avail >> 10),
			  (int) ((mem_avail - total_size) >> 10));

	for (i = 0; i < MTHCA_RES_NUM; ++i) {
		switch (profile[i].type) {
		case MTHCA_RES_QP:
			dev->limits.num_qps   = profile[i].num;
			init_hca->qpc_base    = profile[i].start;
			init_hca->log_num_qps = profile[i].log_num;
			break;
		case MTHCA_RES_EEC:
			dev->limits.num_eecs   = profile[i].num;
			init_hca->eec_base     = profile[i].start;
			init_hca->log_num_eecs = profile[i].log_num;
			break;
		case MTHCA_RES_SRQ:
			dev->limits.num_srqs   = profile[i].num;
			init_hca->srqc_base    = profile[i].start;
			init_hca->log_num_srqs = profile[i].log_num;
			break;
		case MTHCA_RES_CQ:
			dev->limits.num_cqs   = profile[i].num;
			init_hca->cqc_base    = profile[i].start;
			init_hca->log_num_cqs = profile[i].log_num;
			break;
		case MTHCA_RES_EQP:
			init_hca->eqpc_base = profile[i].start;
			break;
		case MTHCA_RES_EEEC:
			init_hca->eeec_base = profile[i].start;
			break;
		case MTHCA_RES_EQ:
			dev->limits.num_eqs   = profile[i].num;
			init_hca->eqc_base    = profile[i].start;
			init_hca->log_num_eqs = profile[i].log_num;
			break;
		case MTHCA_RES_RDB:
			for (dev->qp_table.rdb_shift = 0;
			     request->num_qp << dev->qp_table.rdb_shift < profile[i].num;
			     ++dev->qp_table.rdb_shift)
				; /* nothing */
			dev->qp_table.rdb_base    = (u32) profile[i].start;
			init_hca->rdb_base        = profile[i].start;
			break;
		case MTHCA_RES_MCG:
			dev->limits.num_mgms      = profile[i].num >> 1;
			dev->limits.num_amgms     = profile[i].num >> 1;
			init_hca->mc_base         = profile[i].start;
			init_hca->log_mc_entry_sz = ffs(MTHCA_MGM_ENTRY_SIZE) - 1;
			init_hca->log_mc_table_sz = profile[i].log_num;
			init_hca->mc_hash_sz      = 1 << (profile[i].log_num - 1);
			break;
		case MTHCA_RES_MPT:
			dev->limits.num_mpts   = profile[i].num;
			dev->mr_table.mpt_base = profile[i].start;
			init_hca->mpt_base     = profile[i].start;
			init_hca->log_mpt_sz   = profile[i].log_num;
			break;
		case MTHCA_RES_MTT:
			dev->limits.num_mtt_segs = profile[i].num;
			dev->mr_table.mtt_base   = profile[i].start;
			init_hca->mtt_base       = profile[i].start;
			init_hca->mtt_seg_sz     = ffs(MTHCA_MTT_SEG_SIZE) - 7;
			break;
		case MTHCA_RES_UAR:
			dev->limits.num_uars       = profile[i].num;
			init_hca->uar_scratch_base = profile[i].start;
			break;
		case MTHCA_RES_UDAV:
			dev->av_table.ddr_av_base = profile[i].start;
			dev->av_table.num_ddr_avs = profile[i].num;
			break;
		case MTHCA_RES_UARC:
			dev->uar_table.uarc_size = request->uarc_size;
			dev->uar_table.uarc_base = profile[i].start;
			init_hca->uarc_base   	 = profile[i].start;
			init_hca->log_uarc_sz 	 = ffs(request->uarc_size) - 13;
			init_hca->log_uar_sz  	 = ffs(request->num_uar) - 1;
			break;
		default:
			break;
		}
	}

	/*
	 * PDs don't take any HCA memory, but we assign them as part
	 * of the HCA profile anyway.
	 */
	dev->limits.num_pds = MTHCA_NUM_PDS;

	if (dev->mthca_flags & MTHCA_FLAG_SINAI_OPT &&
	    init_hca->log_mpt_sz > 23) {
		mthca_warn(dev, "MPT table too large (requested size 2^%d >= 2^24)\n",
			   init_hca->log_mpt_sz);
		mthca_warn(dev, "Disabling memory key throughput optimization.\n");
		dev->mthca_flags &= ~MTHCA_FLAG_SINAI_OPT;
	}

	/*
	 * For Tavor, FMRs use ioremapped PCI memory. For 32 bit
	 * systems it may use too much vmalloc space to map all MTT
	 * memory, so we reserve some MTTs for FMR access, taking them
	 * out of the MR pool. They don't use additional memory, but
	 * we assign them as part of the HCA profile anyway.
	 */
	if (mthca_is_memfree(dev))
		dev->limits.fmr_reserved_mtts = 0;
	else
		dev->limits.fmr_reserved_mtts = request->fmr_reserved_mtts;

	kfree(profile);
	return total_size;
}
