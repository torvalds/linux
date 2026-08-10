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
#include <linux/soc/qcom/smem.h>

static const struct qcom_ubwc_cfg_data no_ubwc_data = {
	/* no UBWC, no HBB */
};

static const struct qcom_ubwc_cfg_data ubwc_0_0_hbb15 = {
	/* no UBWC */
	.highest_bank_bit = 15,
};

static const struct qcom_ubwc_cfg_data ubwc_1_0_hbb14 = {
	.ubwc_enc_version = UBWC_1_0,
	.highest_bank_bit = 14,
};

static const struct qcom_ubwc_cfg_data ubwc_1_0_hbb15 = {
	.ubwc_enc_version = UBWC_1_0,
	.highest_bank_bit = 15,
};

static const struct qcom_ubwc_cfg_data ubwc_2_0_hbb14 = {
	.ubwc_enc_version = UBWC_2_0,
	.highest_bank_bit = 14,
};

static const struct qcom_ubwc_cfg_data ubwc_2_0_hbb15 = {
	.ubwc_enc_version = UBWC_2_0,
	.highest_bank_bit = 15,
};

static const struct qcom_ubwc_cfg_data ubwc_3_0_hbb15 = {
	.ubwc_enc_version = UBWC_3_0,
	.highest_bank_bit = 15,
};

static const struct qcom_ubwc_cfg_data ubwc_3_1_hbb13 = {
	.ubwc_enc_version = UBWC_3_1,
	.highest_bank_bit = 13,
};

static const struct qcom_ubwc_cfg_data ubwc_3_1_hbb14  = {
	.ubwc_enc_version = UBWC_3_1,
	.highest_bank_bit = 14,
};

static const struct qcom_ubwc_cfg_data ubwc_3_1_hbb16  = {
	.ubwc_enc_version = UBWC_3_1,
	.highest_bank_bit = 16,
};

static const struct qcom_ubwc_cfg_data ubwc_4_0_hbb16 = {
	.ubwc_enc_version = UBWC_4_0,
	.highest_bank_bit = 16,
};

static const struct qcom_ubwc_cfg_data ubwc_5_0_hbb15 = {
	.ubwc_enc_version = UBWC_5_0,
	/* TODO: highest_bank_bit = 14 for LP_DDR4 */
	.highest_bank_bit = 15,
};

static const struct qcom_ubwc_cfg_data ubwc_5_0_hbb16 = {
	.ubwc_enc_version = UBWC_5_0,
	.highest_bank_bit = 16,
};

static const struct qcom_ubwc_cfg_data ubwc_6_0_hbb16 = {
	.ubwc_enc_version = UBWC_6_0,
	.highest_bank_bit = 16,
};

static const struct qcom_ubwc_cfg_data sa8775p_data = {
	.ubwc_enc_version = UBWC_4_0,
	.flags = UBWC_FLAG_DISABLE_SWIZZLE_LVL2,
	.highest_bank_bit = 13,
};

static const struct qcom_ubwc_cfg_data glymur_data = {
	.ubwc_enc_version = UBWC_5_0,
	.flags = UBWC_FLAG_DISABLE_SWIZZLE_LVL2 |
		 UBWC_FLAG_DISABLE_SWIZZLE_LVL3,
	/* TODO: highest_bank_bit = 15 for LP_DDR4 */
	.highest_bank_bit = 16,
};

static const struct qcom_ubwc_cfg_data milos_data = {
	.ubwc_enc_version = UBWC_4_0,
	.flags = UBWC_SWIZZLE_ENABLE_LVL2 |
		 UBWC_SWIZZLE_ENABLE_LVL3,
	/* TODO: highest_bank_bit = 14 for LP_DDR4 */
	.highest_bank_bit = 15,
};

static const struct of_device_id qcom_ubwc_configs[] __maybe_unused = {
	{ .compatible = "qcom,apq8016", .data = &no_ubwc_data },
	{ .compatible = "qcom,apq8026", .data = &no_ubwc_data },
	{ .compatible = "qcom,apq8074", .data = &no_ubwc_data },
	{ .compatible = "qcom,apq8096", .data = &ubwc_1_0_hbb15 },
	{ .compatible = "qcom,eliza", .data = &ubwc_5_0_hbb15 },
	{ .compatible = "qcom,glymur", .data = &glymur_data},
	{ .compatible = "qcom,kaanapali", .data = &ubwc_6_0_hbb16 },
	{ .compatible = "qcom,mahua", .data = &glymur_data },
	{ .compatible = "qcom,milos", .data = &milos_data },
	{ .compatible = "qcom,msm8226", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8916", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8917", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8937", .data = &ubwc_1_0_hbb14 },
	{ .compatible = "qcom,msm8929", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8939", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8953", .data = &ubwc_1_0_hbb14 },
	{ .compatible = "qcom,msm8956", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8974", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8976", .data = &no_ubwc_data },
	{ .compatible = "qcom,msm8996", .data = &ubwc_1_0_hbb15 },
	{ .compatible = "qcom,msm8998", .data = &ubwc_1_0_hbb15 },
	{ .compatible = "qcom,qcm2290", .data = &ubwc_0_0_hbb15, },
	{ .compatible = "qcom,qcm6490", .data = &ubwc_3_1_hbb14, },
	{ .compatible = "qcom,qcs8300", .data = &ubwc_4_0_hbb16, },
	{ .compatible = "qcom,sa8155p", .data = &ubwc_3_0_hbb15, },
	{ .compatible = "qcom,sa8540p", .data = &ubwc_4_0_hbb16, },
	{ .compatible = "qcom,sa8775p", .data = &sa8775p_data, },
	{ .compatible = "qcom,sar2130p", .data = &ubwc_3_1_hbb13 },
	{ .compatible = "qcom,sc7180", .data = &ubwc_2_0_hbb14, },
	{ .compatible = "qcom,sc7280", .data = &ubwc_3_1_hbb14, },
	{ .compatible = "qcom,sc8180x", .data = &ubwc_3_1_hbb16, },
	{ .compatible = "qcom,sc8280xp", .data = &ubwc_4_0_hbb16, },
	{ .compatible = "qcom,sda660", .data = &ubwc_1_0_hbb14 },
	{ .compatible = "qcom,sdm450", .data = &ubwc_1_0_hbb14 },
	{ .compatible = "qcom,sdm630", .data = &ubwc_1_0_hbb14 },
	{ .compatible = "qcom,sdm632", .data = &ubwc_1_0_hbb14 },
	{ .compatible = "qcom,sdm636", .data = &ubwc_1_0_hbb14 },
	{ .compatible = "qcom,sdm660", .data = &ubwc_1_0_hbb14 },
	{ .compatible = "qcom,sdm670", .data = &ubwc_2_0_hbb14, },
	{ .compatible = "qcom,sdm845", .data = &ubwc_2_0_hbb15, },
	{ .compatible = "qcom,shikra", .data = &ubwc_0_0_hbb15, },
	{ .compatible = "qcom,sm4250", .data = &ubwc_1_0_hbb14, },
	{ .compatible = "qcom,sm6115", .data = &ubwc_1_0_hbb14, },
	{ .compatible = "qcom,sm6125", .data = &ubwc_1_0_hbb14, },
	{ .compatible = "qcom,sm6150", .data = &ubwc_2_0_hbb14, },
	{ .compatible = "qcom,sm6350", .data = &ubwc_2_0_hbb14, },
	{ .compatible = "qcom,sm6375", .data = &ubwc_2_0_hbb14, },
	{ .compatible = "qcom,sm7125", .data = &ubwc_2_0_hbb14, },
	{ .compatible = "qcom,sm7150", .data = &ubwc_2_0_hbb14, },
	{ .compatible = "qcom,sm7225", .data = &ubwc_2_0_hbb14, },
	{ .compatible = "qcom,sm7325", .data = &ubwc_3_1_hbb14, },
	{ .compatible = "qcom,sm8150", .data = &ubwc_3_0_hbb15, },
	{ .compatible = "qcom,sm8250", .data = &ubwc_4_0_hbb16, },
	{ .compatible = "qcom,sm8350", .data = &ubwc_4_0_hbb16, },
	{ .compatible = "qcom,sm8450", .data = &ubwc_4_0_hbb16, },
	{ .compatible = "qcom,sm8550", .data = &ubwc_4_0_hbb16, },
	{ .compatible = "qcom,sm8650", .data = &ubwc_4_0_hbb16, },
	{ .compatible = "qcom,sm8750", .data = &ubwc_5_0_hbb16, },
	{ .compatible = "qcom,x1e80100", .data = &ubwc_4_0_hbb16, },
	{ .compatible = "qcom,x1p42100", .data = &ubwc_4_0_hbb16, },
	{ }
};

static struct qcom_ubwc_cfg_data *cfg;
static DEFINE_MUTEX(cfg_mutex);
/**
 * qcom_ubwc_config_get_data() - Retrieve UBWC data for the platform.
 *
 * Return: Pointer to valid struct qcom_ubwc_cfg_data on success, negative
 * errno on failure. Note that this may return EPROBE_DEFER.
 */
const struct qcom_ubwc_cfg_data *qcom_ubwc_config_get_data(void)
{
	const struct qcom_ubwc_cfg_data *data;
	int hbb;

	guard(mutex)(&cfg_mutex);
	if (cfg)
		return cfg;

	if (!qcom_smem_is_available())
		return ERR_PTR(-EPROBE_DEFER);

	data = of_machine_get_match_data(qcom_ubwc_configs);
	if (!data) {
		pr_err("Couldn't find UBWC config data for this platform!\n");
		return ERR_PTR(-EINVAL);
	}

	hbb = qcom_smem_dram_get_hbb();
	if (hbb == -ENODATA) {
		/* Lack of HBB data is OK - it was only introduced later */
		return data;
	} else if (hbb < 0) {
		pr_err("Couldn't get HBB data from SMEM: %d\n", hbb);
		return ERR_PTR(hbb);
	}

	cfg = kmemdup(data, sizeof(*data), GFP_KERNEL);
	if (!cfg)
		return ERR_PTR(-ENOMEM);

	cfg->highest_bank_bit = hbb;

	return cfg;
}
EXPORT_SYMBOL_GPL(qcom_ubwc_config_get_data);

static void __exit ubwc_config_exit(void)
{
	kfree(cfg);
}
module_exit(ubwc_config_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("UBWC config database for QTI SoCs");
