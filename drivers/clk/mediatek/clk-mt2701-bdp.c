// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Shunli Wang <shunli.wang@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt2701-clk.h>

static const struct mtk_gate_regs bdp0_cg_regs = {
	.set_ofs = 0x0104,
	.clr_ofs = 0x0108,
	.sta_ofs = 0x0100,
};

static const struct mtk_gate_regs bdp1_cg_regs = {
	.set_ofs = 0x0114,
	.clr_ofs = 0x0118,
	.sta_ofs = 0x0110,
};

#define GATE_BDP0(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &bdp0_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

#define GATE_BDP1(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &bdp1_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate bdp_clks[] = {
	GATE_BDP0(CLK_BDP_BRG_BA, "brg_baclk", "mm_sel", 0),
	GATE_BDP0(CLK_BDP_BRG_DRAM, "brg_dram", "mm_sel", 1),
	GATE_BDP0(CLK_BDP_LARB_DRAM, "larb_dram", "mm_sel", 2),
	GATE_BDP0(CLK_BDP_WR_VDI_PXL, "wr_vdi_pxl", "hdmi_0_deep340m", 3),
	GATE_BDP0(CLK_BDP_WR_VDI_DRAM, "wr_vdi_dram", "mm_sel", 4),
	GATE_BDP0(CLK_BDP_WR_B, "wr_bclk", "mm_sel", 5),
	GATE_BDP0(CLK_BDP_DGI_IN, "dgi_in", "dpi1_sel", 6),
	GATE_BDP0(CLK_BDP_DGI_OUT, "dgi_out", "dpi1_sel", 7),
	GATE_BDP0(CLK_BDP_FMT_MAST_27, "fmt_mast_27", "dpi1_sel", 8),
	GATE_BDP0(CLK_BDP_FMT_B, "fmt_bclk", "mm_sel", 9),
	GATE_BDP0(CLK_BDP_OSD_B, "osd_bclk", "mm_sel", 10),
	GATE_BDP0(CLK_BDP_OSD_DRAM, "osd_dram", "mm_sel", 11),
	GATE_BDP0(CLK_BDP_OSD_AGENT, "osd_agent", "osd_sel", 12),
	GATE_BDP0(CLK_BDP_OSD_PXL, "osd_pxl", "dpi1_sel", 13),
	GATE_BDP0(CLK_BDP_RLE_B, "rle_bclk", "mm_sel", 14),
	GATE_BDP0(CLK_BDP_RLE_AGENT, "rle_agent", "mm_sel", 15),
	GATE_BDP0(CLK_BDP_RLE_DRAM, "rle_dram", "mm_sel", 16),
	GATE_BDP0(CLK_BDP_F27M, "f27m", "di_sel", 17),
	GATE_BDP0(CLK_BDP_F27M_VDOUT, "f27m_vdout", "di_sel", 18),
	GATE_BDP0(CLK_BDP_F27_74_74, "f27_74_74", "di_sel", 19),
	GATE_BDP0(CLK_BDP_F2FS, "f2fs", "di_sel", 20),
	GATE_BDP0(CLK_BDP_F2FS74_148, "f2fs74_148", "di_sel", 21),
	GATE_BDP0(CLK_BDP_FB, "fbclk", "mm_sel", 22),
	GATE_BDP0(CLK_BDP_VDO_DRAM, "vdo_dram", "mm_sel", 23),
	GATE_BDP0(CLK_BDP_VDO_2FS, "vdo_2fs", "di_sel", 24),
	GATE_BDP0(CLK_BDP_VDO_B, "vdo_bclk", "mm_sel", 25),
	GATE_BDP0(CLK_BDP_WR_DI_PXL, "wr_di_pxl", "di_sel", 26),
	GATE_BDP0(CLK_BDP_WR_DI_DRAM, "wr_di_dram", "mm_sel", 27),
	GATE_BDP0(CLK_BDP_WR_DI_B, "wr_di_bclk", "mm_sel", 28),
	GATE_BDP0(CLK_BDP_NR_PXL, "nr_pxl", "nr_sel", 29),
	GATE_BDP0(CLK_BDP_NR_DRAM, "nr_dram", "mm_sel", 30),
	GATE_BDP0(CLK_BDP_NR_B, "nr_bclk", "mm_sel", 31),
	GATE_BDP1(CLK_BDP_RX_F, "rx_fclk", "hadds2_fbclk", 0),
	GATE_BDP1(CLK_BDP_RX_X, "rx_xclk", "clk26m", 1),
	GATE_BDP1(CLK_BDP_RXPDT, "rxpdtclk", "hdmi_0_pix340m", 2),
	GATE_BDP1(CLK_BDP_RX_CSCL_N, "rx_cscl_n", "clk26m", 3),
	GATE_BDP1(CLK_BDP_RX_CSCL, "rx_cscl", "clk26m", 4),
	GATE_BDP1(CLK_BDP_RX_DDCSCL_N, "rx_ddcscl_n", "hdmi_scl_rx", 5),
	GATE_BDP1(CLK_BDP_RX_DDCSCL, "rx_ddcscl", "hdmi_scl_rx", 6),
	GATE_BDP1(CLK_BDP_RX_VCO, "rx_vcoclk", "hadds2pll_294m", 7),
	GATE_BDP1(CLK_BDP_RX_DP, "rx_dpclk", "hdmi_0_pll340m", 8),
	GATE_BDP1(CLK_BDP_RX_P, "rx_pclk", "hdmi_0_pll340m", 9),
	GATE_BDP1(CLK_BDP_RX_M, "rx_mclk", "hadds2pll_294m", 10),
	GATE_BDP1(CLK_BDP_RX_PLL, "rx_pllclk", "hdmi_0_pix340m", 11),
	GATE_BDP1(CLK_BDP_BRG_RT_B, "brg_rt_bclk", "mm_sel", 12),
	GATE_BDP1(CLK_BDP_BRG_RT_DRAM, "brg_rt_dram", "mm_sel", 13),
	GATE_BDP1(CLK_BDP_LARBRT_DRAM, "larbrt_dram", "mm_sel", 14),
	GATE_BDP1(CLK_BDP_TMDS_SYN, "tmds_syn", "hdmi_0_pll340m", 15),
	GATE_BDP1(CLK_BDP_HDMI_MON, "hdmi_mon", "hdmi_0_pll340m", 16),
};

static const struct mtk_clk_desc bdp_desc = {
	.clks = bdp_clks,
	.num_clks = ARRAY_SIZE(bdp_clks),
};

static const struct of_device_id of_match_clk_mt2701_bdp[] = {
	{
		.compatible = "mediatek,mt2701-bdpsys",
		.data = &bdp_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt2701_bdp_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt2701-bdp",
		.of_match_table = of_match_clk_mt2701_bdp,
	},
};

builtin_platform_driver(clk_mt2701_bdp_drv);
