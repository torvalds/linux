// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Copyright (c) 2022 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mt8173-clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "clk-cpumux.h"
#include "clk-gate.h"
#include "clk-mtk.h"
#include "reset.h"

#define GATE_ICG(_id, _name, _parent, _shift)			\
		GATE_MTK(_id, _name, _parent, &infra_cg_regs,	\
			 _shift, &mtk_clk_gate_ops_setclr)

static struct clk_hw_onecell_data *infra_clk_data;

static const struct mtk_gate_regs infra_cg_regs = {
	.set_ofs = 0x0040,
	.clr_ofs = 0x0044,
	.sta_ofs = 0x0048,
};

static const char * const ca53_parents[] __initconst = {
	"clk26m",
	"armca7pll",
	"mainpll",
	"univpll"
};

static const char * const ca72_parents[] __initconst = {
	"clk26m",
	"armca15pll",
	"mainpll",
	"univpll"
};

static const struct mtk_composite cpu_muxes[] = {
	MUX(CLK_INFRA_CA53SEL, "infra_ca53_sel", ca53_parents, 0x0000, 0, 2),
	MUX(CLK_INFRA_CA72SEL, "infra_ca72_sel", ca72_parents, 0x0000, 2, 2),
};

static const struct mtk_fixed_factor infra_early_divs[] = {
	FACTOR(CLK_INFRA_CLK_13M, "clk13m", "clk26m", 1, 2),
};

static const struct mtk_gate infra_gates[] = {
	GATE_ICG(CLK_INFRA_DBGCLK, "infra_dbgclk", "axi_sel", 0),
	GATE_ICG(CLK_INFRA_SMI, "infra_smi", "mm_sel", 1),
	GATE_ICG(CLK_INFRA_AUDIO, "infra_audio", "aud_intbus_sel", 5),
	GATE_ICG(CLK_INFRA_GCE, "infra_gce", "axi_sel", 6),
	GATE_ICG(CLK_INFRA_L2C_SRAM, "infra_l2c_sram", "axi_sel", 7),
	GATE_ICG(CLK_INFRA_M4U, "infra_m4u", "mem_sel", 8),
	GATE_ICG(CLK_INFRA_CPUM, "infra_cpum", "cpum_ck", 15),
	GATE_ICG(CLK_INFRA_KP, "infra_kp", "axi_sel", 16),
	GATE_ICG(CLK_INFRA_CEC, "infra_cec", "clk26m", 18),
	GATE_ICG(CLK_INFRA_PMICSPI, "infra_pmicspi", "pmicspi_sel", 22),
	GATE_ICG(CLK_INFRA_PMICWRAP, "infra_pmicwrap", "axi_sel", 23),
};

static u16 infrasys_rst_ofs[] = { 0x30, 0x34 };

static const struct mtk_clk_rst_desc clk_rst_desc = {
	.version = MTK_RST_SIMPLE,
	.rst_bank_ofs = infrasys_rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(infrasys_rst_ofs),
};

static const struct of_device_id of_match_clk_mt8173_infracfg[] = {
	{ .compatible = "mediatek,mt8173-infracfg" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8173_infracfg);

static void clk_mt8173_infra_init_early(struct device_node *node)
{
	int i;

	infra_clk_data = mtk_alloc_clk_data(CLK_INFRA_NR_CLK);
	if (!infra_clk_data)
		return;

	for (i = 0; i < CLK_INFRA_NR_CLK; i++)
		infra_clk_data->hws[i] = ERR_PTR(-EPROBE_DEFER);

	mtk_clk_register_factors(infra_early_divs,
				 ARRAY_SIZE(infra_early_divs), infra_clk_data);

	of_clk_add_hw_provider(node, of_clk_hw_onecell_get, infra_clk_data);
}
CLK_OF_DECLARE_DRIVER(mtk_infrasys, "mediatek,mt8173-infracfg",
		      clk_mt8173_infra_init_early);

static int clk_mt8173_infracfg_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	int r;

	r = mtk_clk_register_gates(&pdev->dev, node, infra_gates,
				   ARRAY_SIZE(infra_gates), infra_clk_data);
	if (r)
		return r;

	r = mtk_clk_register_cpumuxes(&pdev->dev, node, cpu_muxes,
				      ARRAY_SIZE(cpu_muxes), infra_clk_data);
	if (r)
		goto unregister_gates;

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, infra_clk_data);
	if (r)
		goto unregister_cpumuxes;

	r = mtk_register_reset_controller_with_dev(&pdev->dev, &clk_rst_desc);
	if (r)
		goto unregister_clk_hw;

	return 0;

unregister_clk_hw:
	of_clk_del_provider(node);
unregister_cpumuxes:
	mtk_clk_unregister_cpumuxes(cpu_muxes, ARRAY_SIZE(cpu_muxes), infra_clk_data);
unregister_gates:
	mtk_clk_unregister_gates(infra_gates, ARRAY_SIZE(infra_gates), infra_clk_data);
	return r;
}

static void clk_mt8173_infracfg_remove(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);

	of_clk_del_provider(node);
	mtk_clk_unregister_cpumuxes(cpu_muxes, ARRAY_SIZE(cpu_muxes), clk_data);
	mtk_clk_unregister_gates(infra_gates, ARRAY_SIZE(infra_gates), clk_data);
	mtk_free_clk_data(clk_data);
}

static struct platform_driver clk_mt8173_infracfg_drv = {
	.driver = {
		.name = "clk-mt8173-infracfg",
		.of_match_table = of_match_clk_mt8173_infracfg,
	},
	.probe = clk_mt8173_infracfg_probe,
	.remove_new = clk_mt8173_infracfg_remove,
};
module_platform_driver(clk_mt8173_infracfg_drv);

MODULE_DESCRIPTION("MediaTek MT8173 infracfg clocks driver");
MODULE_LICENSE("GPL");
