// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include "fbnic.h"

static void fbnic_hw_stat_rst32(struct fbnic_dev *fbd, u32 reg,
				struct fbnic_stat_counter *stat)
{
	/* We do not touch the "value" field here.
	 * It gets zeroed out on fbd structure allocation.
	 * After that we want it to grow continuously
	 * through device resets and power state changes.
	 */
	stat->u.old_reg_value_32 = rd32(fbd, reg);
}

static void fbnic_hw_stat_rd32(struct fbnic_dev *fbd, u32 reg,
			       struct fbnic_stat_counter *stat)
{
	u32 new_reg_value;

	new_reg_value = rd32(fbd, reg);
	stat->value += new_reg_value - stat->u.old_reg_value_32;
	stat->u.old_reg_value_32 = new_reg_value;
}

u64 fbnic_stat_rd64(struct fbnic_dev *fbd, u32 reg, u32 offset)
{
	u32 prev_upper, upper, lower, diff;

	prev_upper = rd32(fbd, reg + offset);
	lower = rd32(fbd, reg);
	upper = rd32(fbd, reg + offset);

	diff = upper - prev_upper;
	if (!diff)
		return ((u64)upper << 32) | lower;

	if (diff > 1)
		dev_warn_once(fbd->dev,
			      "Stats inconsistent, upper 32b of %#010x updating too quickly\n",
			      reg * 4);

	/* Return only the upper bits as we cannot guarantee
	 * the accuracy of the lower bits. We will add them in
	 * when the counter slows down enough that we can get
	 * a snapshot with both upper values being the same
	 * between reads.
	 */
	return ((u64)upper << 32);
}

static void fbnic_hw_stat_rst64(struct fbnic_dev *fbd, u32 reg, s32 offset,
				struct fbnic_stat_counter *stat)
{
	/* Record initial counter values and compute deltas from there to ensure
	 * stats start at 0 after reboot/reset. This avoids exposing absolute
	 * hardware counter values to userspace.
	 */
	stat->u.old_reg_value_64 = fbnic_stat_rd64(fbd, reg, offset);
}

static void fbnic_hw_stat_rd64(struct fbnic_dev *fbd, u32 reg, s32 offset,
			       struct fbnic_stat_counter *stat)
{
	u64 new_reg_value;

	new_reg_value = fbnic_stat_rd64(fbd, reg, offset);
	stat->value += new_reg_value - stat->u.old_reg_value_64;
	stat->u.old_reg_value_64 = new_reg_value;
}

static void fbnic_reset_rpc_stats(struct fbnic_dev *fbd,
				  struct fbnic_rpc_stats *rpc)
{
	fbnic_hw_stat_rst32(fbd,
			    FBNIC_RPC_CNTR_UNKN_ETYPE,
			    &rpc->unkn_etype);
	fbnic_hw_stat_rst32(fbd,
			    FBNIC_RPC_CNTR_UNKN_EXT_HDR,
			    &rpc->unkn_ext_hdr);
	fbnic_hw_stat_rst32(fbd, FBNIC_RPC_CNTR_IPV4_FRAG, &rpc->ipv4_frag);
	fbnic_hw_stat_rst32(fbd, FBNIC_RPC_CNTR_IPV6_FRAG, &rpc->ipv6_frag);
	fbnic_hw_stat_rst32(fbd, FBNIC_RPC_CNTR_IPV4_ESP, &rpc->ipv4_esp);
	fbnic_hw_stat_rst32(fbd, FBNIC_RPC_CNTR_IPV6_ESP, &rpc->ipv6_esp);
	fbnic_hw_stat_rst32(fbd, FBNIC_RPC_CNTR_TCP_OPT_ERR, &rpc->tcp_opt_err);
	fbnic_hw_stat_rst32(fbd,
			    FBNIC_RPC_CNTR_OUT_OF_HDR_ERR,
			    &rpc->out_of_hdr_err);
	fbnic_hw_stat_rst32(fbd,
			    FBNIC_RPC_CNTR_OVR_SIZE_ERR,
			    &rpc->ovr_size_err);
}

static void fbnic_get_rpc_stats32(struct fbnic_dev *fbd,
				  struct fbnic_rpc_stats *rpc)
{
	fbnic_hw_stat_rd32(fbd,
			   FBNIC_RPC_CNTR_UNKN_ETYPE,
			   &rpc->unkn_etype);
	fbnic_hw_stat_rd32(fbd,
			   FBNIC_RPC_CNTR_UNKN_EXT_HDR,
			   &rpc->unkn_ext_hdr);

	fbnic_hw_stat_rd32(fbd, FBNIC_RPC_CNTR_IPV4_FRAG, &rpc->ipv4_frag);
	fbnic_hw_stat_rd32(fbd, FBNIC_RPC_CNTR_IPV6_FRAG, &rpc->ipv6_frag);

	fbnic_hw_stat_rd32(fbd, FBNIC_RPC_CNTR_IPV4_ESP, &rpc->ipv4_esp);
	fbnic_hw_stat_rd32(fbd, FBNIC_RPC_CNTR_IPV6_ESP, &rpc->ipv6_esp);

	fbnic_hw_stat_rd32(fbd, FBNIC_RPC_CNTR_TCP_OPT_ERR, &rpc->tcp_opt_err);
	fbnic_hw_stat_rd32(fbd,
			   FBNIC_RPC_CNTR_OUT_OF_HDR_ERR,
			   &rpc->out_of_hdr_err);
	fbnic_hw_stat_rd32(fbd,
			   FBNIC_RPC_CNTR_OVR_SIZE_ERR,
			   &rpc->ovr_size_err);
}

static void fbnic_reset_pcie_stats_asic(struct fbnic_dev *fbd,
					struct fbnic_pcie_stats *pcie)
{
	fbnic_hw_stat_rst64(fbd,
			    FBNIC_PUL_USER_OB_RD_TLP_CNT_31_0,
			    1,
			    &pcie->ob_rd_tlp);
	fbnic_hw_stat_rst64(fbd,
			    FBNIC_PUL_USER_OB_RD_DWORD_CNT_31_0,
			    1,
			    &pcie->ob_rd_dword);
	fbnic_hw_stat_rst64(fbd,
			    FBNIC_PUL_USER_OB_CPL_TLP_CNT_31_0,
			    1,
			    &pcie->ob_cpl_tlp);
	fbnic_hw_stat_rst64(fbd,
			    FBNIC_PUL_USER_OB_CPL_DWORD_CNT_31_0,
			    1,
			    &pcie->ob_cpl_dword);
	fbnic_hw_stat_rst64(fbd,
			    FBNIC_PUL_USER_OB_WR_TLP_CNT_31_0,
			    1,
			    &pcie->ob_wr_tlp);
	fbnic_hw_stat_rst64(fbd,
			    FBNIC_PUL_USER_OB_WR_DWORD_CNT_31_0,
			    1,
			    &pcie->ob_wr_dword);

	fbnic_hw_stat_rst64(fbd,
			    FBNIC_PUL_USER_OB_RD_DBG_CNT_TAG_31_0,
			    1,
			    &pcie->ob_rd_no_tag);
	fbnic_hw_stat_rst64(fbd,
			    FBNIC_PUL_USER_OB_RD_DBG_CNT_CPL_CRED_31_0,
			    1,
			    &pcie->ob_rd_no_cpl_cred);
	fbnic_hw_stat_rst64(fbd,
			    FBNIC_PUL_USER_OB_RD_DBG_CNT_NP_CRED_31_0,
			    1,
			    &pcie->ob_rd_no_np_cred);
}

static void fbnic_get_pcie_stats_asic64(struct fbnic_dev *fbd,
					struct fbnic_pcie_stats *pcie)
{
	fbnic_hw_stat_rd64(fbd,
			   FBNIC_PUL_USER_OB_RD_TLP_CNT_31_0,
			   1,
			   &pcie->ob_rd_tlp);
	fbnic_hw_stat_rd64(fbd,
			   FBNIC_PUL_USER_OB_RD_DWORD_CNT_31_0,
			   1,
			   &pcie->ob_rd_dword);
	fbnic_hw_stat_rd64(fbd,
			   FBNIC_PUL_USER_OB_WR_TLP_CNT_31_0,
			   1,
			   &pcie->ob_wr_tlp);
	fbnic_hw_stat_rd64(fbd,
			   FBNIC_PUL_USER_OB_WR_DWORD_CNT_31_0,
			   1,
			   &pcie->ob_wr_dword);
	fbnic_hw_stat_rd64(fbd,
			   FBNIC_PUL_USER_OB_CPL_TLP_CNT_31_0,
			   1,
			   &pcie->ob_cpl_tlp);
	fbnic_hw_stat_rd64(fbd,
			   FBNIC_PUL_USER_OB_CPL_DWORD_CNT_31_0,
			   1,
			   &pcie->ob_cpl_dword);

	fbnic_hw_stat_rd64(fbd,
			   FBNIC_PUL_USER_OB_RD_DBG_CNT_TAG_31_0,
			   1,
			   &pcie->ob_rd_no_tag);
	fbnic_hw_stat_rd64(fbd,
			   FBNIC_PUL_USER_OB_RD_DBG_CNT_CPL_CRED_31_0,
			   1,
			   &pcie->ob_rd_no_cpl_cred);
	fbnic_hw_stat_rd64(fbd,
			   FBNIC_PUL_USER_OB_RD_DBG_CNT_NP_CRED_31_0,
			   1,
			   &pcie->ob_rd_no_np_cred);
}

void fbnic_reset_hw_stats(struct fbnic_dev *fbd)
{
	fbnic_reset_rpc_stats(fbd, &fbd->hw_stats.rpc);
	fbnic_reset_pcie_stats_asic(fbd, &fbd->hw_stats.pcie);
}

void fbnic_get_hw_stats32(struct fbnic_dev *fbd)
{
	fbnic_get_rpc_stats32(fbd, &fbd->hw_stats.rpc);
}

void fbnic_get_hw_stats(struct fbnic_dev *fbd)
{
	fbnic_get_hw_stats32(fbd);

	fbnic_get_pcie_stats_asic64(fbd, &fbd->hw_stats.pcie);
}
