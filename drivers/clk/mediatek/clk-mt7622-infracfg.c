// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Copyright (c) 2023 Collabora, Ltd.
 *               AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mt7622-clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "clk-cpumux.h"
#include "clk-gate.h"
#include "clk-mtk.h"
#include "reset.h"

#define GATE_INFRA(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &infra_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate_regs infra_cg_regs = {
	.set_ofs = 0x40,
	.clr_ofs = 0x44,
	.sta_ofs = 0x48,
};

static const char * const infra_mux1_parents[] = {
	"clkxtal",
	"armpll",
	"main_core_en",
	"armpll"
};

static const struct mtk_composite cpu_muxes[] = {
	MUX(CLK_INFRA_MUX1_SEL, "infra_mux1_sel", infra_mux1_parents, 0x000, 2, 2),
};

static const struct mtk_gate infra_clks[] = {
	GATE_INFRA(CLK_INFRA_DBGCLK_PD, "infra_dbgclk_pd", "axi_sel", 0),
	GATE_INFRA(CLK_INFRA_TRNG, "trng_ck", "axi_sel", 2),
	GATE_INFRA(CLK_INFRA_AUDIO_PD, "infra_audio_pd", "aud_intbus_sel", 5),
	GATE_INFRA(CLK_INFRA_IRRX_PD, "infra_irrx_pd", "irrx_sel", 16),
	GATE_INFRA(CLK_INFRA_APXGPT_PD, "infra_apxgpt_pd", "f10m_ref_sel", 18),
	GATE_INFRA(CLK_INFRA_PMIC_PD, "infra_pmic_pd", "pmicspi_sel", 22),
};

static u16 infrasys_rst_ofs[] = { 0x30 };

static const struct mtk_clk_rst_desc clk_rst_desc = {
	.version = MTK_RST_SIMPLE,
	.rst_bank_ofs = infrasys_rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(infrasys_rst_ofs),
};

static const struct of_device_id of_match_clk_mt7622_infracfg[] = {
	{ .compatible = "mediatek,mt7622-infracfg" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt7622_infracfg);

static int clk_mt7622_infracfg_probe(struct platform_device *pdev)
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

	ret = mtk_clk_register_gates(&pdev->dev, node, infra_clks,
				     ARRAY_SIZE(infra_clks), clk_data);
	if (ret)
		goto free_clk_data;

	ret = mtk_clk_register_cpumuxes(&pdev->dev, node, cpu_muxes,
					ARRAY_SIZE(cpu_muxes), clk_data);
	if (ret)
		goto unregister_gates;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (ret)
		goto unregister_cpumuxes;

	return 0;

unregister_cpumuxes:
	mtk_clk_unregister_cpumuxes(cpu_muxes, ARRAY_SIZE(cpu_muxes), clk_data);
unregister_gates:
	mtk_clk_unregister_gates(infra_clks, ARRAY_SIZE(infra_clks), clk_data);
free_clk_data:
	mtk_free_clk_data(clk_data);
	return ret;
}

static void clk_mt7622_infracfg_remove(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);

	of_clk_del_provider(node);
	mtk_clk_unregister_cpumuxes(cpu_muxes, ARRAY_SIZE(cpu_muxes), clk_data);
	mtk_clk_unregister_gates(infra_clks, ARRAY_SIZE(infra_clks), clk_data);
	mtk_free_clk_data(clk_data);
}

static struct platform_driver clk_mt7622_infracfg_drv = {
	.driver = {
		.name = "clk-mt7622-infracfg",
		.of_match_table = of_match_clk_mt7622_infracfg,
	},
	.probe = clk_mt7622_infracfg_probe,
	.remove_new = clk_mt7622_infracfg_remove,
};
module_platform_driver(clk_mt7622_infracfg_drv);

MODULE_DESCRIPTION("MediaTek MT7622 infracfg clocks driver");
MODULE_LICENSE("GPL");
