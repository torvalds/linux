// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include "fbnic.h"

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
	fbnic_reset_pcie_stats_asic(fbd, &fbd->hw_stats.pcie);
}

void fbnic_get_hw_stats(struct fbnic_dev *fbd)
{
	fbnic_get_pcie_stats_asic64(fbd, &fbd->hw_stats.pcie);
}
