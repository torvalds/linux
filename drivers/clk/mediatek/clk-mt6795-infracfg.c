// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mediatek,mt6795-clk.h>
#include <dt-bindings/reset/mediatek,mt6795-resets.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "clk-cpumux.h"
#include "clk-gate.h"
#include "clk-mtk.h"
#include "reset.h"

#define GATE_ICG(_id, _name, _parent, _shift)			\
		GATE_MTK(_id, _name, _parent, &infra_cg_regs,	\
			 _shift, &mtk_clk_gate_ops_no_setclr)

static const struct mtk_gate_regs infra_cg_regs = {
	.set_ofs = 0x0040,
	.clr_ofs = 0x0044,
	.sta_ofs = 0x0048,
};

static const char * const ca53_c0_parents[] = {
	"clk26m",
	"armca53pll",
	"mainpll",
	"univpll"
};

static const char * const ca53_c1_parents[] = {
	"clk26m",
	"armca53pll",
	"mainpll",
	"univpll"
};

static const struct mtk_composite cpu_muxes[] = {
	MUX(CLK_INFRA_CA53_C0_SEL, "infra_ca53_c0_sel", ca53_c0_parents, 0x00, 0, 2),
	MUX(CLK_INFRA_CA53_C1_SEL, "infra_ca53_c1_sel", ca53_c1_parents, 0x00, 2, 2),
};

static const struct mtk_gate infra_gates[] = {
	GATE_ICG(CLK_INFRA_DBGCLK, "infra_dbgclk", "axi_sel", 0),
	GATE_ICG(CLK_INFRA_SMI, "infra_smi", "mm_sel", 1),
	GATE_ICG(CLK_INFRA_AUDIO, "infra_audio", "aud_intbus_sel", 5),
	GATE_ICG(CLK_INFRA_GCE, "infra_gce", "axi_sel", 6),
	GATE_ICG(CLK_INFRA_L2C_SRAM, "infra_l2c_sram", "axi_sel", 7),
	GATE_ICG(CLK_INFRA_M4U, "infra_m4u", "mem_sel", 8),
	GATE_ICG(CLK_INFRA_MD1MCU, "infra_md1mcu", "clk26m", 9),
	GATE_ICG(CLK_INFRA_MD1BUS, "infra_md1bus", "axi_sel", 10),
	GATE_ICG(CLK_INFRA_MD1DBB, "infra_dbb", "axi_sel", 11),
	GATE_ICG(CLK_INFRA_DEVICE_APC, "infra_devapc", "clk26m", 12),
	GATE_ICG(CLK_INFRA_TRNG, "infra_trng", "axi_sel", 13),
	GATE_ICG(CLK_INFRA_MD1LTE, "infra_md1lte", "axi_sel", 14),
	GATE_ICG(CLK_INFRA_CPUM, "infra_cpum", "cpum_ck", 15),
	GATE_ICG(CLK_INFRA_KP, "infra_kp", "axi_sel", 16),
};

static u16 infra_ao_rst_ofs[] = { 0x30, 0x34 };

static u16 infra_ao_idx_map[] = {
	[MT6795_INFRA_RST0_SCPSYS_RST]    = 0 * RST_NR_PER_BANK + 5,
	[MT6795_INFRA_RST0_PMIC_WRAP_RST] = 0 * RST_NR_PER_BANK + 7,
	[MT6795_INFRA_RST1_MIPI_DSI_RST]  = 1 * RST_NR_PER_BANK + 4,
	[MT6795_INFRA_RST1_MIPI_CSI_RST]  = 1 * RST_NR_PER_BANK + 7,
	[MT6795_INFRA_RST1_MM_IOMMU_RST]  = 1 * RST_NR_PER_BANK + 15,
};

static const struct mtk_clk_rst_desc clk_rst_desc = {
	.version = MTK_RST_SET_CLR,
	.rst_bank_ofs = infra_ao_rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(infra_ao_rst_ofs),
	.rst_idx_map = infra_ao_idx_map,
	.rst_idx_map_nr = ARRAY_SIZE(infra_ao_idx_map),
};

static const struct of_device_id of_match_clk_mt6795_infracfg[] = {
	{ .compatible = "mediatek,mt6795-infracfg" },
	{ /* sentinel */ }
};

static int clk_mt6795_infracfg_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	void __iomem *base;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk_data = mtk_alloc_clk_data(CLK_INFRA_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	ret = mtk_register_reset_controller_with_dev(&pdev->dev, &clk_rst_desc);
	if (ret)
		goto free_clk_data;

	ret = mtk_clk_register_gates(node, infra_gates, ARRAY_SIZE(infra_gates), clk_data);
	if (ret)
		goto free_clk_data;

	ret = mtk_clk_register_cpumuxes(node, cpu_muxes, ARRAY_SIZE(cpu_muxes), clk_data);
	if (ret)
		goto unregister_gates;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (ret)
		goto unregister_cpumuxes;

	return 0;

unregister_cpumuxes:
	mtk_clk_unregister_cpumuxes(cpu_muxes, ARRAY_SIZE(cpu_muxes), clk_data);
unregister_gates:
	mtk_clk_unregister_gates(infra_gates, ARRAY_SIZE(infra_gates), clk_data);
free_clk_data:
	mtk_free_clk_data(clk_data);
	return ret;
}

static int clk_mt6795_infracfg_remove(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);

	of_clk_del_provider(node);
	mtk_clk_unregister_cpumuxes(cpu_muxes, ARRAY_SIZE(cpu_muxes), clk_data);
	mtk_clk_unregister_gates(infra_gates, ARRAY_SIZE(infra_gates), clk_data);
	mtk_free_clk_data(clk_data);

	return 0;
}

static struct platform_driver clk_mt6795_infracfg_drv = {
	.driver = {
		.name = "clk-mt6795-infracfg",
		.of_match_table = of_match_clk_mt6795_infracfg,
	},
	.probe = clk_mt6795_infracfg_probe,
	.remove = clk_mt6795_infracfg_remove,
};
module_platform_driver(clk_mt6795_infracfg_drv);

MODULE_DESCRIPTION("MediaTek MT6795 infracfg clocks driver");
MODULE_LICENSE("GPL");
