// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Weiyi Lu <weiyi.lu@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt2712-clk.h>

static const struct mtk_gate_regs bdp_cg_regs = {
	.set_ofs = 0x100,
	.clr_ofs = 0x100,
	.sta_ofs = 0x100,
};

#define GATE_BDP(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &bdp_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

static const struct mtk_gate bdp_clks[] = {
	GATE_BDP(CLK_BDP_BRIDGE_B, "bdp_bridge_b", "mm_sel", 0),
	GATE_BDP(CLK_BDP_BRIDGE_DRAM, "bdp_bridge_d", "mm_sel", 1),
	GATE_BDP(CLK_BDP_LARB_DRAM, "bdp_larb_d", "mm_sel", 2),
	GATE_BDP(CLK_BDP_WR_CHANNEL_VDI_PXL, "bdp_vdi_pxl", "tvd_sel", 3),
	GATE_BDP(CLK_BDP_WR_CHANNEL_VDI_DRAM, "bdp_vdi_d", "mm_sel", 4),
	GATE_BDP(CLK_BDP_WR_CHANNEL_VDI_B, "bdp_vdi_b", "mm_sel", 5),
	GATE_BDP(CLK_BDP_MT_B, "bdp_fmt_b", "mm_sel", 9),
	GATE_BDP(CLK_BDP_DISPFMT_27M, "bdp_27m", "di_sel", 10),
	GATE_BDP(CLK_BDP_DISPFMT_27M_VDOUT, "bdp_27m_vdout", "di_sel", 11),
	GATE_BDP(CLK_BDP_DISPFMT_27_74_74, "bdp_27_74_74", "di_sel", 12),
	GATE_BDP(CLK_BDP_DISPFMT_2FS, "bdp_2fs", "di_sel", 13),
	GATE_BDP(CLK_BDP_DISPFMT_2FS_2FS74_148, "bdp_2fs74_148", "di_sel", 14),
	GATE_BDP(CLK_BDP_DISPFMT_B, "bdp_b", "mm_sel", 15),
	GATE_BDP(CLK_BDP_VDO_DRAM, "bdp_vdo_d", "mm_sel", 16),
	GATE_BDP(CLK_BDP_VDO_2FS, "bdp_vdo_2fs", "di_sel", 17),
	GATE_BDP(CLK_BDP_VDO_B, "bdp_vdo_b", "mm_sel", 18),
	GATE_BDP(CLK_BDP_WR_CHANNEL_DI_PXL, "bdp_di_pxl", "di_sel", 19),
	GATE_BDP(CLK_BDP_WR_CHANNEL_DI_DRAM, "bdp_di_d", "mm_sel", 20),
	GATE_BDP(CLK_BDP_WR_CHANNEL_DI_B, "bdp_di_b", "mm_sel", 21),
	GATE_BDP(CLK_BDP_NR_AGENT, "bdp_nr_agent", "nr_sel", 22),
	GATE_BDP(CLK_BDP_NR_DRAM, "bdp_nr_d", "mm_sel", 23),
	GATE_BDP(CLK_BDP_NR_B, "bdp_nr_b", "mm_sel", 24),
	GATE_BDP(CLK_BDP_BRIDGE_RT_B, "bdp_bridge_rt_b", "mm_sel", 25),
	GATE_BDP(CLK_BDP_BRIDGE_RT_DRAM, "bdp_bridge_rt_d", "mm_sel", 26),
	GATE_BDP(CLK_BDP_LARB_RT_DRAM, "bdp_larb_rt_d", "mm_sel", 27),
	GATE_BDP(CLK_BDP_TVD_TDC, "bdp_tvd_tdc", "mm_sel", 28),
	GATE_BDP(CLK_BDP_TVD_54, "bdp_tvd_clk_54", "tvd_sel", 29),
	GATE_BDP(CLK_BDP_TVD_CBUS, "bdp_tvd_cbus", "mm_sel", 30),
};

static const struct mtk_clk_desc bdp_desc = {
	.clks = bdp_clks,
	.num_clks = ARRAY_SIZE(bdp_clks),
};

static const struct of_device_id of_match_clk_mt2712_bdp[] = {
	{
		.compatible = "mediatek,mt2712-bdpsys",
		.data = &bdp_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt2712_bdp);

static struct platform_driver clk_mt2712_bdp_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt2712-bdp",
		.of_match_table = of_match_clk_mt2712_bdp,
	},
};
module_platform_driver(clk_mt2712_bdp_drv);

MODULE_DESCRIPTION("MediaTek MT2712 BDP clocks driver");
MODULE_LICENSE("GPL");
