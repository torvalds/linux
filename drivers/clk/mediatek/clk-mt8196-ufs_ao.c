// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 MediaTek Inc.
 *                    Guangjie Song <guangjie.song@mediatek.com>
 * Copyright (c) 2025 Collabora Ltd.
 *                    Laura Nao <laura.nao@collabora.com>
 */
#include <dt-bindings/clock/mediatek,mt8196-clock.h>
#include <dt-bindings/reset/mediatek,mt8196-resets.h>

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

#define MT8196_UFSAO_RST0_SET_OFFSET	0x48
#define MT8196_UFSAO_RST1_SET_OFFSET	0x148

static const struct mtk_gate_regs ufsao0_cg_regs = {
	.set_ofs = 0x108,
	.clr_ofs = 0x10c,
	.sta_ofs = 0x104,
};

static const struct mtk_gate_regs ufsao1_cg_regs = {
	.set_ofs = 0x8,
	.clr_ofs = 0xc,
	.sta_ofs = 0x4,
};

#define GATE_UFSAO0(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ufsao0_cg_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_setclr,	\
	}

#define GATE_UFSAO1(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ufsao1_cg_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_setclr,	\
	}

static const struct mtk_gate ufsao_clks[] = {
	/* UFSAO0 */
	GATE_UFSAO0(CLK_UFSAO_UFSHCI_UFS, "ufsao_ufshci_ufs", "ufs", 0),
	GATE_UFSAO0(CLK_UFSAO_UFSHCI_AES, "ufsao_ufshci_aes", "aes_ufsfde", 1),
	/* UFSAO1 */
	GATE_UFSAO1(CLK_UFSAO_UNIPRO_TX_SYM, "ufsao_unipro_tx_sym", "clk26m", 0),
	GATE_UFSAO1(CLK_UFSAO_UNIPRO_RX_SYM0, "ufsao_unipro_rx_sym0", "clk26m", 1),
	GATE_UFSAO1(CLK_UFSAO_UNIPRO_RX_SYM1, "ufsao_unipro_rx_sym1", "clk26m", 2),
	GATE_UFSAO1(CLK_UFSAO_UNIPRO_SYS, "ufsao_unipro_sys", "ufs", 3),
	GATE_UFSAO1(CLK_UFSAO_UNIPRO_SAP, "ufsao_unipro_sap", "clk26m", 4),
	GATE_UFSAO1(CLK_UFSAO_PHY_SAP, "ufsao_phy_sap", "clk26m", 8),
};

static u16 ufsao_rst_ofs[] = {
	MT8196_UFSAO_RST0_SET_OFFSET,
	MT8196_UFSAO_RST1_SET_OFFSET
};

static u16 ufsao_rst_idx_map[] = {
	[MT8196_UFSAO_RST0_UFS_MPHY] = 8,
	[MT8196_UFSAO_RST1_UFS_UNIPRO] = 1 * RST_NR_PER_BANK + 0,
	[MT8196_UFSAO_RST1_UFS_CRYPTO] = 1 * RST_NR_PER_BANK + 1,
	[MT8196_UFSAO_RST1_UFSHCI] = 1 * RST_NR_PER_BANK + 2,
};

static const struct mtk_clk_rst_desc ufsao_rst_desc = {
	.version = MTK_RST_SET_CLR,
	.rst_bank_ofs = ufsao_rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(ufsao_rst_ofs),
	.rst_idx_map = ufsao_rst_idx_map,
	.rst_idx_map_nr = ARRAY_SIZE(ufsao_rst_idx_map),
};

static const struct mtk_clk_desc ufsao_mcd = {
	.clks = ufsao_clks,
	.num_clks = ARRAY_SIZE(ufsao_clks),
	.rst_desc = &ufsao_rst_desc,
};

static const struct of_device_id of_match_clk_mt8196_ufs_ao[] = {
	{ .compatible = "mediatek,mt8196-ufscfg-ao", .data = &ufsao_mcd },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8196_ufs_ao);

static struct platform_driver clk_mt8196_ufs_ao_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8196-ufs-ao",
		.of_match_table = of_match_clk_mt8196_ufs_ao,
	},
};

module_platform_driver(clk_mt8196_ufs_ao_drv);
MODULE_DESCRIPTION("MediaTek MT8196 ufs_ao clocks driver");
MODULE_LICENSE("GPL");
