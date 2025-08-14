// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include <linux/soc/qcom/ubwc.h>

static const struct qcom_ubwc_cfg_data no_ubwc_data = {
	/* no UBWC, no HBB */
};

static const struct qcom_ubwc_cfg_data msm8937_data = {
	.ubwc_enc_version = UBWC_1_0,
	.ubwc_dec_version = UBWC_1_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL1 |
			UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.highest_bank_bit = 14,
};

static const struct qcom_ubwc_cfg_data msm8998_data = {
	.ubwc_enc_version = UBWC_1_0,
	.ubwc_dec_version = UBWC_1_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL1 |
			UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.highest_bank_bit = 15,
};

static const struct qcom_ubwc_cfg_data qcm2290_data = {
	/* no UBWC */
	.highest_bank_bit = 15,
};

static const struct qcom_ubwc_cfg_data sa8775p_data = {
	.ubwc_enc_version = UBWC_4_0,
	.ubwc_dec_version = UBWC_4_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL3,
	.ubwc_bank_spread = true,
	.highest_bank_bit = 13,
	.macrotile_mode = true,
};

static const struct qcom_ubwc_cfg_data sar2130p_data = {
	.ubwc_enc_version = UBWC_3_0, /* 4.0.2 in hw */
	.ubwc_dec_version = UBWC_4_3,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.ubwc_bank_spread = true,
	.highest_bank_bit = 13,
	.macrotile_mode = true,
};

static const struct qcom_ubwc_cfg_data sc7180_data = {
	.ubwc_enc_version = UBWC_2_0,
	.ubwc_dec_version = UBWC_2_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.ubwc_bank_spread = true,
	.highest_bank_bit = 14,
};

static const struct qcom_ubwc_cfg_data sc7280_data = {
	.ubwc_enc_version = UBWC_3_0,
	.ubwc_dec_version = UBWC_4_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.ubwc_bank_spread = true,
	.highest_bank_bit = 14,
	.macrotile_mode = true,
};

static const struct qcom_ubwc_cfg_data sc8180x_data = {
	.ubwc_enc_version = UBWC_3_0,
	.ubwc_dec_version = UBWC_3_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.highest_bank_bit = 16,
	.macrotile_mode = true,
};

static const struct qcom_ubwc_cfg_data sc8280xp_data = {
	.ubwc_enc_version = UBWC_4_0,
	.ubwc_dec_version = UBWC_4_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.ubwc_bank_spread = true,
	.highest_bank_bit = 16,
	.macrotile_mode = true,
};

static const struct qcom_ubwc_cfg_data sdm670_data = {
	.ubwc_enc_version = UBWC_2_0,
	.ubwc_dec_version = UBWC_2_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.highest_bank_bit = 14,
};

static const struct qcom_ubwc_cfg_data sdm845_data = {
	.ubwc_enc_version = UBWC_2_0,
	.ubwc_dec_version = UBWC_2_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.highest_bank_bit = 15,
};

static const struct qcom_ubwc_cfg_data sm6115_data = {
	.ubwc_enc_version = UBWC_1_0,
	.ubwc_dec_version = UBWC_2_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL1 |
			UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.ubwc_bank_spread = true,
	.highest_bank_bit = 14,
};

static const struct qcom_ubwc_cfg_data sm6125_data = {
	.ubwc_enc_version = UBWC_1_0,
	.ubwc_dec_version = UBWC_3_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL1 |
			UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.highest_bank_bit = 14,
};

static const struct qcom_ubwc_cfg_data sm6150_data = {
	.ubwc_enc_version = UBWC_2_0,
	.ubwc_dec_version = UBWC_2_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.highest_bank_bit = 14,
};

static const struct qcom_ubwc_cfg_data sm6350_data = {
	.ubwc_enc_version = UBWC_2_0,
	.ubwc_dec_version = UBWC_2_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.ubwc_bank_spread = true,
	.highest_bank_bit = 14,
};

static const struct qcom_ubwc_cfg_data sm7150_data = {
	.ubwc_enc_version = UBWC_2_0,
	.ubwc_dec_version = UBWC_2_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.highest_bank_bit = 14,
};

static const struct qcom_ubwc_cfg_data sm8150_data = {
	.ubwc_enc_version = UBWC_3_0,
	.ubwc_dec_version = UBWC_3_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.highest_bank_bit = 15,
};

static const struct qcom_ubwc_cfg_data sm8250_data = {
	.ubwc_enc_version = UBWC_4_0,
	.ubwc_dec_version = UBWC_4_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.ubwc_bank_spread = true,
	/* TODO: highest_bank_bit = 15 for LP_DDR4 */
	.highest_bank_bit = 16,
	.macrotile_mode = true,
};

static const struct qcom_ubwc_cfg_data sm8350_data = {
	.ubwc_enc_version = UBWC_4_0,
	.ubwc_dec_version = UBWC_4_0,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.ubwc_bank_spread = true,
	/* TODO: highest_bank_bit = 15 for LP_DDR4 */
	.highest_bank_bit = 16,
	.macrotile_mode = true,
};

static const struct qcom_ubwc_cfg_data sm8550_data = {
	.ubwc_enc_version = UBWC_4_0,
	.ubwc_dec_version = UBWC_4_3,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.ubwc_bank_spread = true,
	/* TODO: highest_bank_bit = 15 for LP_DDR4 */
	.highest_bank_bit = 16,
	.macrotile_mode = true,
};

static const struct qcom_ubwc_cfg_data sm8750_data = {
	.ubwc_enc_version = UBWC_5_0,
	.ubwc_dec_version = UBWC_5_0,
	.ubwc_swizzle = 6,
	.ubwc_bank_spread = true,
	/* TODO: highest_bank_bit = 15 for LP_DDR4 */
	.highest_bank_bit = 16,
	.macrotile_mode = true,
};

static const struct qcom_ubwc_cfg_data x1e80100_data = {
	.ubwc_enc_version = UBWC_4_0,
	.ubwc_dec_version = UBWC_4_3,
	.ubwc_swizzle = UBWC_SWIZZLE_ENABLE_LVL2 |
			UBWC_SWIZZLE_ENABLE_LVL3,
	.ubwc_bank_spread = true,
	/* TODO: highest_bank_bit = 15 for LP_DDR4 */
	.highest_bank_bit = 16,
	.macrotile_mode = true,
};

static const struct of_device_id qcom_ubwc_configs[] __maybe_unused = {
	{ .compatible = "qcom,apq8016", .data = &no_ubwc_data },
	{ .compatible = "qcom,apq8026", .data = &no_ubwc_data },
	{ .compatible = "qcom,apq8074", .data = &no_ubwc_data },
	{ .compatible = "qcom,apq8096", .data = &msm8998_data },
	{ .compatible = "qcom,msm8226", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8916", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8917", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8937", .data = &msm8937_data },
	{ .compatible = "qcom,msm8929", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8939", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8953", .data = &msm8937_data },
	{ .compatible = "qcom,msm8956", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8974", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8976", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8996", .data = &msm8998_data },
	{ .compatible = "qcom,msm8998", .data = &msm8998_data },
	{ .compatible = "qcom,qcm2290", .data = &qcm2290_data, },
	{ .compatible = "qcom,qcm6490", .data = &sc7280_data, },
	{ .compatible = "qcom,sa8155p", .data = &sm8150_data, },
	{ .compatible = "qcom,sa8540p", .data = &sc8280xp_data, },
	{ .compatible = "qcom,sa8775p", .data = &sa8775p_data, },
	{ .compatible = "qcom,sar2130p", .data = &sar2130p_data },
	{ .compatible = "qcom,sc7180", .data = &sc7180_data },
	{ .compatible = "qcom,sc7280", .data = &sc7280_data, },
	{ .compatible = "qcom,sc8180x", .data = &sc8180x_data, },
	{ .compatible = "qcom,sc8280xp", .data = &sc8280xp_data, },
	{ .compatible = "qcom,sda660", .data = &msm8937_data },
	{ .compatible = "qcom,sdm450", .data = &msm8937_data },
	{ .compatible = "qcom,sdm630", .data = &msm8937_data },
	{ .compatible = "qcom,sdm632", .data = &msm8937_data },
	{ .compatible = "qcom,sdm636", .data = &msm8937_data },
	{ .compatible = "qcom,sdm660", .data = &msm8937_data },
	{ .compatible = "qcom,sdm670", .data = &sdm670_data, },
	{ .compatible = "qcom,sdm845", .data = &sdm845_data, },
	{ .compatible = "qcom,sm4250", .data = &sm6115_data, },
	{ .compatible = "qcom,sm6115", .data = &sm6115_data, },
	{ .compatible = "qcom,sm6125", .data = &sm6125_data, },
	{ .compatible = "qcom,sm6150", .data = &sm6150_data, },
	{ .compatible = "qcom,sm6350", .data = &sm6350_data, },
	{ .compatible = "qcom,sm6375", .data = &sm6350_data, },
	{ .compatible = "qcom,sm7125", .data = &sc7180_data },
	{ .compatible = "qcom,sm7150", .data = &sm7150_data, },
	{ .compatible = "qcom,sm7225", .data = &sm6350_data, },
	{ .compatible = "qcom,sm7325", .data = &sc7280_data, },
	{ .compatible = "qcom,sm8150", .data = &sm8150_data, },
	{ .compatible = "qcom,sm8250", .data = &sm8250_data, },
	{ .compatible = "qcom,sm8350", .data = &sm8350_data, },
	{ .compatible = "qcom,sm8450", .data = &sm8350_data, },
	{ .compatible = "qcom,sm8550", .data = &sm8550_data, },
	{ .compatible = "qcom,sm8650", .data = &sm8550_data, },
	{ .compatible = "qcom,sm8750", .data = &sm8750_data, },
	{ .compatible = "qcom,x1e80100", .data = &x1e80100_data, },
	{ .compatible = "qcom,x1p42100", .data = &x1e80100_data, },
	{ }
};

const struct qcom_ubwc_cfg_data *qcom_ubwc_config_get_data(void)
{
	const struct of_device_id *match;
	struct device_node *root;

	root = of_find_node_by_path("/");
	if (!root)
		return ERR_PTR(-ENODEV);

	match = of_match_node(qcom_ubwc_configs, root);
	of_node_put(root);
	if (!match) {
		pr_err("Couldn't find UBWC config data for this platform!\n");
		return ERR_PTR(-EINVAL);
	}

	return match->data;
}
EXPORT_SYMBOL_GPL(qcom_ubwc_config_get_data);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("UBWC config database for QTI SoCs");
