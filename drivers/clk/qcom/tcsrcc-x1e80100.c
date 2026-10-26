// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/clk-provider.h>
#include <linux/clk/qcom.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,x1e80100-tcsr.h>

static const char * const x1e80100_tcsr_tx1_rpt0_rx0_regulators[] = {
	"vdda-refgen0-0p9",
	"vdda-refgen0-1p2",
	"vdda-qreftx1-0p9",
	"vdda-qreftx1-1p2",
	"vdda-qrefrpt0-0p9",
	"vdda-qrefrx0-0p9",
};

static const char * const x1e80100_tcsr_tx1_rpt1_rx1_regulators[] = {
	"vdda-refgen0-0p9",
	"vdda-refgen0-1p2",
	"vdda-qreftx1-0p9",
	"vdda-qreftx1-1p2",
	"vdda-qrefrpt1-0p9",
	"vdda-qrefrx1-0p9",
};

static const char * const x1e80100_tcsr_tx1_rpt12_rx2_regulators[] = {
	"vdda-refgen0-0p9",
	"vdda-refgen0-1p2",
	"vdda-qreftx1-0p9",
	"vdda-qreftx1-1p2",
	"vdda-qrefrpt1-0p9",
	"vdda-qrefrpt2-0p9",
	"vdda-qrefrx2-0p9",
};

static const char * const x1e80100_tcsr_tx0_rpt3_rx3_regulators[] = {
	"vdda-refgen2-0p9",
	"vdda-refgen2-1p2",
	"vdda-qreftx0-0p9",
	"vdda-qreftx0-1p2",
	"vdda-qrefrpt3-0p9",
	"vdda-qrefrx3-0p9",
};

static const char * const x1e80100_tcsr_tx0_rpt4_rx4_regulators[] = {
	"vdda-refgen2-0p9",
	"vdda-refgen2-1p2",
	"vdda-qreftx0-0p9",
	"vdda-qreftx0-1p2",
	"vdda-qrefrpt4-0p9",
	"vdda-qrefrx4-0p9",
};

static const struct regmap_config tcsr_cc_x1e80100_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x2f000,
	.fast_io = true,
};

static const struct qcom_clk_ref_desc * const tcsr_cc_x1e80100_clk_descs[] = {
	[TCSR_EDP_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_edp_clkref_en",
		.offset = 0x15130,
		.regulator_names = x1e80100_tcsr_tx0_rpt3_rx3_regulators,
		.num_regulators = ARRAY_SIZE(x1e80100_tcsr_tx0_rpt3_rx3_regulators),
	},
	[TCSR_PCIE_2L_4_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_pcie_2l_4_clkref_en",
		.offset = 0x15100,
		.regulator_names = x1e80100_tcsr_tx1_rpt1_rx1_regulators,
		.num_regulators = ARRAY_SIZE(x1e80100_tcsr_tx1_rpt1_rx1_regulators),
	},
	[TCSR_PCIE_2L_5_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_pcie_2l_5_clkref_en",
		.offset = 0x15104,
		.regulator_names = x1e80100_tcsr_tx1_rpt12_rx2_regulators,
		.num_regulators = ARRAY_SIZE(x1e80100_tcsr_tx1_rpt12_rx2_regulators),
	},
	[TCSR_PCIE_8L_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_pcie_8l_clkref_en",
		.offset = 0x15108,
		.regulator_names = x1e80100_tcsr_tx1_rpt0_rx0_regulators,
		.num_regulators = ARRAY_SIZE(x1e80100_tcsr_tx1_rpt0_rx0_regulators),
	},
	[TCSR_USB3_MP0_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb3_mp0_clkref_en",
		.offset = 0x1510c,
		.regulator_names = x1e80100_tcsr_tx1_rpt0_rx0_regulators,
		.num_regulators = ARRAY_SIZE(x1e80100_tcsr_tx1_rpt0_rx0_regulators),
	},
	[TCSR_USB3_MP1_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb3_mp1_clkref_en",
		.offset = 0x15110,
		.regulator_names = x1e80100_tcsr_tx1_rpt0_rx0_regulators,
		.num_regulators = ARRAY_SIZE(x1e80100_tcsr_tx1_rpt0_rx0_regulators),
	},
	[TCSR_USB2_1_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb2_1_clkref_en",
		.offset = 0x15114,
		.regulator_names = x1e80100_tcsr_tx0_rpt3_rx3_regulators,
		.num_regulators = ARRAY_SIZE(x1e80100_tcsr_tx0_rpt3_rx3_regulators),
	},
	[TCSR_UFS_PHY_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_ufs_phy_clkref_en",
		.offset = 0x15118,
		.regulator_names = x1e80100_tcsr_tx1_rpt12_rx2_regulators,
		.num_regulators = ARRAY_SIZE(x1e80100_tcsr_tx1_rpt12_rx2_regulators),
	},
	[TCSR_USB4_1_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb4_1_clkref_en",
		.offset = 0x15120,
		.regulator_names = x1e80100_tcsr_tx0_rpt4_rx4_regulators,
		.num_regulators = ARRAY_SIZE(x1e80100_tcsr_tx0_rpt4_rx4_regulators),
	},
	[TCSR_USB4_2_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb4_2_clkref_en",
		.offset = 0x15124,
		.regulator_names = x1e80100_tcsr_tx0_rpt3_rx3_regulators,
		.num_regulators = ARRAY_SIZE(x1e80100_tcsr_tx0_rpt3_rx3_regulators),
	},
	[TCSR_USB2_2_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_usb2_2_clkref_en",
		.offset = 0x15128,
		.regulator_names = x1e80100_tcsr_tx0_rpt3_rx3_regulators,
		.num_regulators = ARRAY_SIZE(x1e80100_tcsr_tx0_rpt3_rx3_regulators),
	},
	[TCSR_PCIE_4L_CLKREF_EN] = &(const struct qcom_clk_ref_desc) {
		.name = "tcsr_pcie_4l_clkref_en",
		.offset = 0x1512c,
		.regulator_names = x1e80100_tcsr_tx0_rpt4_rx4_regulators,
		.num_regulators = ARRAY_SIZE(x1e80100_tcsr_tx0_rpt4_rx4_regulators),
	},
};

static const struct of_device_id tcsr_cc_x1e80100_match_table[] = {
	{ .compatible = "qcom,x1e80100-tcsr" },
	{ }
};
MODULE_DEVICE_TABLE(of, tcsr_cc_x1e80100_match_table);

static int tcsr_cc_x1e80100_probe(struct platform_device *pdev)
{
	return qcom_clk_ref_probe(pdev, &tcsr_cc_x1e80100_regmap_config,
				  tcsr_cc_x1e80100_clk_descs,
				  ARRAY_SIZE(tcsr_cc_x1e80100_clk_descs));
}

static struct platform_driver tcsr_cc_x1e80100_driver = {
	.probe = tcsr_cc_x1e80100_probe,
	.driver = {
		.name = "tcsrcc-x1e80100",
		.of_match_table = tcsr_cc_x1e80100_match_table,
	},
};

subsys_platform_driver(tcsr_cc_x1e80100_driver);

MODULE_DESCRIPTION("QTI TCSR Clock Controller X1E80100 Driver");
MODULE_LICENSE("GPL");
