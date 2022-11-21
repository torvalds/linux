// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spmi.h>
#include <linux/regmap.h>
#include <linux/of_platform.h>

#define PMIC_REV2		0x101
#define PMIC_REV3		0x102
#define PMIC_REV4		0x103
#define PMIC_TYPE		0x104
#define PMIC_SUBTYPE		0x105

#define PMIC_TYPE_VALUE		0x51

#define COMMON_SUBTYPE		0x00
#define PM8941_SUBTYPE		0x01
#define PM8841_SUBTYPE		0x02
#define PM8019_SUBTYPE		0x03
#define PM8226_SUBTYPE		0x04
#define PM8110_SUBTYPE		0x05
#define PMA8084_SUBTYPE		0x06
#define PMI8962_SUBTYPE		0x07
#define PMD9635_SUBTYPE		0x08
#define PM8994_SUBTYPE		0x09
#define PMI8994_SUBTYPE		0x0a
#define PM8916_SUBTYPE		0x0b
#define PM8004_SUBTYPE		0x0c
#define PM8909_SUBTYPE		0x0d
#define PM8028_SUBTYPE		0x0e
#define PM8901_SUBTYPE		0x0f
#define PM8950_SUBTYPE		0x10
#define PMI8950_SUBTYPE		0x11
#define PM8998_SUBTYPE		0x14
#define PMI8998_SUBTYPE		0x15
#define PM8005_SUBTYPE		0x18
#define PM660L_SUBTYPE		0x1A
#define PM660_SUBTYPE		0x1B
#define PM8150_SUBTYPE		0x1E
#define PM8150L_SUBTYPE		0x1f
#define PM8150B_SUBTYPE		0x20
#define PMK8002_SUBTYPE		0x21
#define PM8009_SUBTYPE		0x24
#define PM8150C_SUBTYPE		0x26
#define SMB2351_SUBTYPE		0x29

static const struct of_device_id pmic_spmi_id_table[] = {
	{ .compatible = "qcom,pm660",     .data = (void *)PM660_SUBTYPE },
	{ .compatible = "qcom,pm660l",    .data = (void *)PM660L_SUBTYPE },
	{ .compatible = "qcom,pm8004",    .data = (void *)PM8004_SUBTYPE },
	{ .compatible = "qcom,pm8005",    .data = (void *)PM8005_SUBTYPE },
	{ .compatible = "qcom,pm8019",    .data = (void *)PM8019_SUBTYPE },
	{ .compatible = "qcom,pm8028",    .data = (void *)PM8028_SUBTYPE },
	{ .compatible = "qcom,pm8110",    .data = (void *)PM8110_SUBTYPE },
	{ .compatible = "qcom,pm8150",    .data = (void *)PM8150_SUBTYPE },
	{ .compatible = "qcom,pm8150b",   .data = (void *)PM8150B_SUBTYPE },
	{ .compatible = "qcom,pm8150c",   .data = (void *)PM8150C_SUBTYPE },
	{ .compatible = "qcom,pm8150l",   .data = (void *)PM8150L_SUBTYPE },
	{ .compatible = "qcom,pm8226",    .data = (void *)PM8226_SUBTYPE },
	{ .compatible = "qcom,pm8841",    .data = (void *)PM8841_SUBTYPE },
	{ .compatible = "qcom,pm8901",    .data = (void *)PM8901_SUBTYPE },
	{ .compatible = "qcom,pm8909",    .data = (void *)PM8909_SUBTYPE },
	{ .compatible = "qcom,pm8916",    .data = (void *)PM8916_SUBTYPE },
	{ .compatible = "qcom,pm8941",    .data = (void *)PM8941_SUBTYPE },
	{ .compatible = "qcom,pm8950",    .data = (void *)PM8950_SUBTYPE },
	{ .compatible = "qcom,pm8994",    .data = (void *)PM8994_SUBTYPE },
	{ .compatible = "qcom,pm8998",    .data = (void *)PM8998_SUBTYPE },
	{ .compatible = "qcom,pma8084",   .data = (void *)PMA8084_SUBTYPE },
	{ .compatible = "qcom,pmd9635",   .data = (void *)PMD9635_SUBTYPE },
	{ .compatible = "qcom,pmi8950",   .data = (void *)PMI8950_SUBTYPE },
	{ .compatible = "qcom,pmi8962",   .data = (void *)PMI8962_SUBTYPE },
	{ .compatible = "qcom,pmi8994",   .data = (void *)PMI8994_SUBTYPE },
	{ .compatible = "qcom,pmi8998",   .data = (void *)PMI8998_SUBTYPE },
	{ .compatible = "qcom,pmk8002",   .data = (void *)PMK8002_SUBTYPE },
	{ .compatible = "qcom,smb2351",   .data = (void *)SMB2351_SUBTYPE },
	{ .compatible = "qcom,spmi-pmic", .data = (void *)COMMON_SUBTYPE },
	{ }
};

static void pmic_spmi_show_revid(struct regmap *map, struct device *dev)
{
	unsigned int rev2, minor, major, type, subtype;
	const char *name = "unknown";
	int ret, i;

	ret = regmap_read(map, PMIC_TYPE, &type);
	if (ret < 0)
		return;

	if (type != PMIC_TYPE_VALUE)
		return;

	ret = regmap_read(map, PMIC_SUBTYPE, &subtype);
	if (ret < 0)
		return;

	for (i = 0; i < ARRAY_SIZE(pmic_spmi_id_table); i++) {
		if (subtype == (unsigned long)pmic_spmi_id_table[i].data)
			break;
	}

	if (i != ARRAY_SIZE(pmic_spmi_id_table))
		name = pmic_spmi_id_table[i].compatible;

	ret = regmap_read(map, PMIC_REV2, &rev2);
	if (ret < 0)
		return;

	ret = regmap_read(map, PMIC_REV3, &minor);
	if (ret < 0)
		return;

	ret = regmap_read(map, PMIC_REV4, &major);
	if (ret < 0)
		return;

	/*
	 * In early versions of PM8941 and PM8226, the major revision number
	 * started incrementing from 0 (eg 0 = v1.0, 1 = v2.0).
	 * Increment the major revision number here if the chip is an early
	 * version of PM8941 or PM8226.
	 */
	if ((subtype == PM8941_SUBTYPE || subtype == PM8226_SUBTYPE) &&
	    major < 0x02)
		major++;

	if (subtype == PM8110_SUBTYPE)
		minor = rev2;

	dev_dbg(dev, "%x: %s v%d.%d\n", subtype, name, major, minor);
}

static const struct regmap_config spmi_regmap_config = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0xffff,
	.fast_io	= true,
};

static int pmic_spmi_probe(struct spmi_device *sdev)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_spmi_ext(sdev, &spmi_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Only the first slave id for a PMIC contains this information */
	if (sdev->usid % 2 == 0)
		pmic_spmi_show_revid(regmap, &sdev->dev);

	return devm_of_platform_populate(&sdev->dev);
}

MODULE_DEVICE_TABLE(of, pmic_spmi_id_table);

static struct spmi_driver pmic_spmi_driver = {
	.probe = pmic_spmi_probe,
	.driver = {
		.name = "pmic-spmi",
		.of_match_table = pmic_spmi_id_table,
	},
};
module_spmi_driver(pmic_spmi_driver);

MODULE_DESCRIPTION("Qualcomm SPMI PMIC driver");
MODULE_ALIAS("spmi:spmi-pmic");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Josh Cartwright <joshc@codeaurora.org>");
MODULE_AUTHOR("Stanimir Varbanov <svarbanov@mm-sol.com>");
