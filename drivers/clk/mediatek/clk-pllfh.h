/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Edward-JW Yang <edward-jw.yang@mediatek.com>
 */

#ifndef __CLK_PLLFH_H
#define __CLK_PLLFH_H

#include "clk-pll.h"

struct fh_pll_state {
	void __iomem *base;
	u32 fh_enable;
	u32 ssc_rate;
};

struct fh_pll_data {
	int pll_id;
	int fh_id;
	int fh_ver;
	u32 fhx_offset;
	u32 dds_mask;
	u32 slope0_value;
	u32 slope1_value;
	u32 sfstrx_en;
	u32 frddsx_en;
	u32 fhctlx_en;
	u32 tgl_org;
	u32 dvfs_tri;
	u32 pcwchg;
	u32 dt_val;
	u32 df_val;
	u32 updnlmt_shft;
	u32 msk_frddsx_dys;
	u32 msk_frddsx_dts;
};

struct mtk_pllfh_data {
	struct fh_pll_state state;
	const struct fh_pll_data data;
};

struct fh_pll_regs {
	void __iomem *reg_hp_en;
	void __iomem *reg_clk_con;
	void __iomem *reg_rst_con;
	void __iomem *reg_slope0;
	void __iomem *reg_slope1;
	void __iomem *reg_cfg;
	void __iomem *reg_updnlmt;
	void __iomem *reg_dds;
	void __iomem *reg_dvfs;
	void __iomem *reg_mon;
};

struct mtk_fh {
	struct mtk_clk_pll clk_pll;
	struct fh_pll_regs regs;
	struct mtk_pllfh_data *pllfh_data;
	const struct fh_operation *ops;
	spinlock_t *lock;
};

struct fh_operation {
	int (*hopping)(struct mtk_fh *fh, unsigned int new_dds,
		       unsigned int postdiv);
	int (*ssc_enable)(struct mtk_fh *fh, u32 rate);
};

int mtk_clk_register_pllfhs(struct device_node *node,
			    const struct mtk_pll_data *plls, int num_plls,
			    struct mtk_pllfh_data *pllfhs, int num_pllfhs,
			    struct clk_hw_onecell_data *clk_data);

void mtk_clk_unregister_pllfhs(const struct mtk_pll_data *plls, int num_plls,
			       struct mtk_pllfh_data *pllfhs, int num_fhs,
			       struct clk_hw_onecell_data *clk_data);

void fhctl_parse_dt(const u8 *compatible_node, struct mtk_pllfh_data *pllfhs,
		    int num_pllfhs);

#endif /* __CLK_PLLFH_H */
