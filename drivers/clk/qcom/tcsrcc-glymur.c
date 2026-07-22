// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/clk/qcom.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,glymur-tcsr.h>

static const char * const glymur_tcsr_tx0_rx5_regulators[] = {
	"vdda-refgen3-0p9",
	"vdda-refgen3-1p2",
	"vdda-qrefrx5-0p9",
	"vdda-qreftx0-0p9",
	"vdda-qreftx0-1p2",
};

static const char * const glymur_tcsr_tx1_rpt0_rx0_regulators[] = {
	"vdda-refgen4-0p9",
	"vdda-refgen4-1p2",
	"vdda-qreftx1-0p9",
	"vdda-qrefrpt0-0p9",
	"vdda-qrefrx0-0p9",
};

static const char * const glymur_tcsr_tx1_rpt01_rx1_regulators[] = {
	"vdda-refgen4-0p9",
	"vdda-refgen4-1p2",
	"vdda-qreftx1-0p9",
	"vdda-qrefrpt0-0p9",
	"vdda-qrefrpt1-0p9",
	"vdda-qrefrx1-0p9",
};

static const char * const glymur_tcsr_tx1_rpt012_rx2_regulators[] = {
	"vdda-refgen4-0p9",
	"vdda-refgen4-1p2",
	"vdda-qreftx1-0p9",
	"vdda-qrefrpt0-0p9",
	"vdda-qrefrpt1-0p9",
	"vdda-qrefrpt2-0p9",
	"vdda-qrefrx2-0p9",
};

static const char * const glymur_tcsr_tx1_rpt34_rx4_regulators[] = {
	"vdda-refgen4-0p9",
	"vdda-refgen4-1p2",
	"vdda-qreftx1-0p9",
	"vdda-qrefrpt3-0p9",
	"vdda-qrefrpt4-0p9",
	"vdda-qrefrx4-0p9",
};

static const struct regmap_config tcsr_cc_glymur_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x94,
	.fast_io = true,
};

static const struct qcom_clk_ref_desc * const tcsr_cc_glymur_clk_descs[] = {
	[TCSR_EDP_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_edp_clkref_en",
		.offset = 0x60,
		.regulator_names = glymur_tcsr_tx1_rpt0_rx0_regulators,
		.num_regulators = ARRAY_SIZE(glymur_tcsr_tx1_rpt0_rx0_regulators),
	},
	[TCSR_PCIE_1_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_pcie_1_clkref_en",
		.offset = 0x48,
		.regulator_names = glymur_tcsr_tx0_rx5_regulators,
		.num_regulators = ARRAY_SIZE(glymur_tcsr_tx0_rx5_regulators),
	},
	[TCSR_PCIE_2_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_pcie_2_clkref_en",
		.offset = 0x4c,
		.regulator_names = glymur_tcsr_tx1_rpt012_rx2_regulators,
		.num_regulators = ARRAY_SIZE(glymur_tcsr_tx1_rpt012_rx2_regulators),
	},
	[TCSR_PCIE_3_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_pcie_3_clkref_en",
		.offset = 0x54,
		.regulator_names = glymur_tcsr_tx1_rpt01_rx1_regulators,
		.num_regulators = ARRAY_SIZE(glymur_tcsr_tx1_rpt01_rx1_regulators),
	},
	[TCSR_PCIE_4_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_pcie_4_clkref_en",
		.offset = 0x58,
		.regulator_names = glymur_tcsr_tx1_rpt012_rx2_regulators,
		.num_regulators = ARRAY_SIZE(glymur_tcsr_tx1_rpt012_rx2_regulators),
	},
	[TCSR_USB2_1_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb2_1_clkref_en",
		.offset = 0x6c,
		.regulator_names = glymur_tcsr_tx1_rpt34_rx4_regulators,
		.num_regulators = ARRAY_SIZE(glymur_tcsr_tx1_rpt34_rx4_regulators),
	},
	[TCSR_USB2_2_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb2_2_clkref_en",
		.offset = 0x70,
		.regulator_names = glymur_tcsr_tx1_rpt01_rx1_regulators,
		.num_regulators = ARRAY_SIZE(glymur_tcsr_tx1_rpt01_rx1_regulators),
	},
	[TCSR_USB2_3_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb2_3_clkref_en",
		.offset = 0x74,
		.regulator_names = glymur_tcsr_tx1_rpt34_rx4_regulators,
		.num_regulators = ARRAY_SIZE(glymur_tcsr_tx1_rpt34_rx4_regulators),
	},
	[TCSR_USB2_4_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb2_4_clkref_en",
		.offset = 0x88,
		.regulator_names = glymur_tcsr_tx1_rpt34_rx4_regulators,
		.num_regulators = ARRAY_SIZE(glymur_tcsr_tx1_rpt34_rx4_regulators),
	},
	[TCSR_USB3_0_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb3_0_clkref_en",
		.offset = 0x64,
		.regulator_names = glymur_tcsr_tx1_rpt34_rx4_regulators,
		.num_regulators = ARRAY_SIZE(glymur_tcsr_tx1_rpt34_rx4_regulators),
	},
	[TCSR_USB3_1_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb3_1_clkref_en",
		.offset = 0x68,
		.regulator_names = glymur_tcsr_tx1_rpt34_rx4_regulators,
		.num_regulators = ARRAY_SIZE(glymur_tcsr_tx1_rpt34_rx4_regulators),
	},
	[TCSR_USB4_1_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb4_1_clkref_en",
		.offset = 0x44,
		.regulator_names = glymur_tcsr_tx0_rx5_regulators,
		.num_regulators = ARRAY_SIZE(glymur_tcsr_tx0_rx5_regulators),
	},
	[TCSR_USB4_2_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb4_2_clkref_en",
		.offset = 0x5c,
		.regulator_names = glymur_tcsr_tx1_rpt01_rx1_regulators,
		.num_regulators = ARRAY_SIZE(glymur_tcsr_tx1_rpt01_rx1_regulators),
	},
};

static const struct of_device_id tcsr_cc_glymur_match_table[] = {
	{ .compatible = "qcom,glymur-tcsr" },
	{ }
};
MODULE_DEVICE_TABLE(of, tcsr_cc_glymur_match_table);

static int tcsr_cc_glymur_probe(struct platform_device *pdev)
{
	return qcom_clk_ref_probe(pdev, &tcsr_cc_glymur_regmap_config,
				  tcsr_cc_glymur_clk_descs,
				  ARRAY_SIZE(tcsr_cc_glymur_clk_descs));
}

static struct platform_driver tcsr_cc_glymur_driver = {
	.probe = tcsr_cc_glymur_probe,
	.driver = {
		.name = "tcsrcc-glymur",
		.of_match_table = tcsr_cc_glymur_match_table,
	},
};

static int __init tcsr_cc_glymur_init(void)
{
	return platform_driver_register(&tcsr_cc_glymur_driver);
}
subsys_initcall(tcsr_cc_glymur_init);

static void __exit tcsr_cc_glymur_exit(void)
{
	platform_driver_unregister(&tcsr_cc_glymur_driver);
}
module_exit(tcsr_cc_glymur_exit);

MODULE_DESCRIPTION("QTI TCSRCC Glymur Driver");
MODULE_LICENSE("GPL");
