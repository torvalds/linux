// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mediatek,mt6735-infracfg.h>
#include <dt-bindings/reset/mediatek,mt6735-infracfg.h>

#define INFRA_RST0			0x30
#define INFRA_GLOBALCON_PDN0		0x40
#define INFRA_PDN1			0x44
#define INFRA_PDN_STA			0x48

#define RST_NR_PER_BANK			32

static struct mtk_gate_regs infra_cg_regs = {
	.set_ofs = INFRA_GLOBALCON_PDN0,
	.clr_ofs = INFRA_PDN1,
	.sta_ofs = INFRA_PDN_STA,
};

static const struct mtk_gate infracfg_gates[] = {
	GATE_MTK(CLK_INFRA_DBG, "dbg", "axi_sel", &infra_cg_regs, 0, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_GCE, "gce", "axi_sel", &infra_cg_regs, 1, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_TRBG, "trbg", "axi_sel", &infra_cg_regs, 2, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_CPUM, "cpum", "axi_sel", &infra_cg_regs, 3, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_DEVAPC, "devapc", "axi_sel", &infra_cg_regs, 4, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_AUDIO, "audio", "aud_intbus_sel", &infra_cg_regs, 5, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_GCPU, "gcpu", "axi_sel", &infra_cg_regs, 6, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_L2C_SRAM, "l2csram", "axi_sel", &infra_cg_regs, 7, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_M4U, "m4u", "axi_sel", &infra_cg_regs, 8, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_CLDMA, "cldma", "axi_sel", &infra_cg_regs, 12, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_CONNMCU_BUS, "connmcu_bus", "axi_sel", &infra_cg_regs, 15, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_KP, "kp", "axi_sel", &infra_cg_regs, 16, &mtk_clk_gate_ops_setclr),
	GATE_MTK_FLAGS(CLK_INFRA_APXGPT, "apxgpt", "axi_sel", &infra_cg_regs, 18, &mtk_clk_gate_ops_setclr, CLK_IS_CRITICAL),
	GATE_MTK(CLK_INFRA_SEJ, "sej", "axi_sel", &infra_cg_regs, 19, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_CCIF0_AP, "ccif0ap", "axi_sel", &infra_cg_regs, 20, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_CCIF1_AP, "ccif1ap", "axi_sel", &infra_cg_regs, 21, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_PMIC_SPI, "pmicspi", "pmicspi_sel", &infra_cg_regs, 22, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_INFRA_PMIC_WRAP, "pmicwrap", "axi_sel", &infra_cg_regs, 23, &mtk_clk_gate_ops_setclr)
};

static u16 infracfg_rst_bank_ofs[] = { INFRA_RST0 };

static u16 infracfg_rst_idx_map[] = {
	[MT6735_INFRA_RST0_EMI_REG]		= 0 * RST_NR_PER_BANK + 0,
	[MT6735_INFRA_RST0_DRAMC0_AO]		= 0 * RST_NR_PER_BANK + 1,
	[MT6735_INFRA_RST0_AP_CIRQ_EINT]	= 0 * RST_NR_PER_BANK + 3,
	[MT6735_INFRA_RST0_APXGPT]		= 0 * RST_NR_PER_BANK + 4,
	[MT6735_INFRA_RST0_SCPSYS]		= 0 * RST_NR_PER_BANK + 5,
	[MT6735_INFRA_RST0_KP]			= 0 * RST_NR_PER_BANK + 6,
	[MT6735_INFRA_RST0_PMIC_WRAP]		= 0 * RST_NR_PER_BANK + 7,
	[MT6735_INFRA_RST0_CLDMA_AO_TOP]	= 0 * RST_NR_PER_BANK + 8,
	[MT6735_INFRA_RST0_USBSIF_TOP]		= 0 * RST_NR_PER_BANK + 9,
	[MT6735_INFRA_RST0_EMI]			= 0 * RST_NR_PER_BANK + 16,
	[MT6735_INFRA_RST0_CCIF]		= 0 * RST_NR_PER_BANK + 17,
	[MT6735_INFRA_RST0_DRAMC0]		= 0 * RST_NR_PER_BANK + 18,
	[MT6735_INFRA_RST0_EMI_AO_REG]		= 0 * RST_NR_PER_BANK + 19,
	[MT6735_INFRA_RST0_CCIF_AO]		= 0 * RST_NR_PER_BANK + 20,
	[MT6735_INFRA_RST0_TRNG]		= 0 * RST_NR_PER_BANK + 21,
	[MT6735_INFRA_RST0_SYS_CIRQ]		= 0 * RST_NR_PER_BANK + 22,
	[MT6735_INFRA_RST0_GCE]			= 0 * RST_NR_PER_BANK + 23,
	[MT6735_INFRA_RST0_M4U]			= 0 * RST_NR_PER_BANK + 24,
	[MT6735_INFRA_RST0_CCIF1]		= 0 * RST_NR_PER_BANK + 25,
	[MT6735_INFRA_RST0_CLDMA_TOP_PD]	= 0 * RST_NR_PER_BANK + 26
};

static const struct mtk_clk_rst_desc infracfg_resets = {
	.version = MTK_RST_SIMPLE,
	.rst_bank_ofs = infracfg_rst_bank_ofs,
	.rst_bank_nr = ARRAY_SIZE(infracfg_rst_bank_ofs),
	.rst_idx_map = infracfg_rst_idx_map,
	.rst_idx_map_nr = ARRAY_SIZE(infracfg_rst_idx_map)
};

static const struct mtk_clk_desc infracfg_clks = {
	.clks = infracfg_gates,
	.num_clks = ARRAY_SIZE(infracfg_gates),

	.rst_desc = &infracfg_resets
};

static const struct of_device_id of_match_mt6735_infracfg[] = {
	{ .compatible = "mediatek,mt6735-infracfg", .data = &infracfg_clks },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_mt6735_infracfg);

static struct platform_driver clk_mt6735_infracfg = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6735-infracfg",
		.of_match_table = of_match_mt6735_infracfg,
	},
};
module_platform_driver(clk_mt6735_infracfg);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("MediaTek MT6735 infracfg clock and reset driver");
MODULE_LICENSE("GPL");
