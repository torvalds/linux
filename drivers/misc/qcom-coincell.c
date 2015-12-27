/* Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2015, Sony Mobile Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

struct qcom_coincell {
	struct device	*dev;
	struct regmap	*regmap;
	u32		base_addr;
};

#define QCOM_COINCELL_REG_RSET		0x44
#define QCOM_COINCELL_REG_VSET		0x45
#define QCOM_COINCELL_REG_ENABLE	0x46

#define QCOM_COINCELL_ENABLE		BIT(7)

static const int qcom_rset_map[] = { 2100, 1700, 1200, 800 };
static const int qcom_vset_map[] = { 2500, 3200, 3100, 3000 };
/* NOTE: for pm8921 and others, voltage of 2500 is 16 (10000b), not 0 */

/* if enable==0, rset and vset are ignored */
static int qcom_coincell_chgr_config(struct qcom_coincell *chgr, int rset,
				     int vset, bool enable)
{
	int i, j, rc;

	/* if disabling, just do that and skip other operations */
	if (!enable)
		return regmap_write(chgr->regmap,
			  chgr->base_addr + QCOM_COINCELL_REG_ENABLE, 0);

	/* find index for current-limiting resistor */
	for (i = 0; i < ARRAY_SIZE(qcom_rset_map); i++)
		if (rset == qcom_rset_map[i])
			break;

	if (i >= ARRAY_SIZE(qcom_rset_map)) {
		dev_err(chgr->dev, "invalid rset-ohms value %d\n", rset);
		return -EINVAL;
	}

	/* find index for charge voltage */
	for (j = 0; j < ARRAY_SIZE(qcom_vset_map); j++)
		if (vset == qcom_vset_map[j])
			break;

	if (j >= ARRAY_SIZE(qcom_vset_map)) {
		dev_err(chgr->dev, "invalid vset-millivolts value %d\n", vset);
		return -EINVAL;
	}

	rc = regmap_write(chgr->regmap,
			  chgr->base_addr + QCOM_COINCELL_REG_RSET, i);
	if (rc) {
		/*
		 * This is mainly to flag a bad base_addr (reg) from dts.
		 * Other failures writing to the registers should be
		 * extremely rare, or indicative of problems that
		 * should be reported elsewhere (eg. spmi failure).
		 */
		dev_err(chgr->dev, "could not write to RSET register\n");
		return rc;
	}

	rc = regmap_write(chgr->regmap,
		chgr->base_addr + QCOM_COINCELL_REG_VSET, j);
	if (rc)
		return rc;

	/* set 'enable' register */
	return regmap_write(chgr->regmap,
			    chgr->base_addr + QCOM_COINCELL_REG_ENABLE,
			    QCOM_COINCELL_ENABLE);
}

static int qcom_coincell_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct qcom_coincell chgr;
	u32 rset, vset;
	bool enable;
	int rc;

	chgr.dev = &pdev->dev;

	chgr.regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chgr.regmap) {
		dev_err(chgr.dev, "Unable to get regmap\n");
		return -EINVAL;
	}

	rc = of_property_read_u32(node, "reg", &chgr.base_addr);
	if (rc)
		return rc;

	enable = !of_property_read_bool(node, "qcom,charger-disable");

	if (enable) {
		rc = of_property_read_u32(node, "qcom,rset-ohms", &rset);
		if (rc) {
			dev_err(chgr.dev,
				"can't find 'qcom,rset-ohms' in DT block");
			return rc;
		}

		rc = of_property_read_u32(node, "qcom,vset-millivolts", &vset);
		if (rc) {
			dev_err(chgr.dev,
			    "can't find 'qcom,vset-millivolts' in DT block");
			return rc;
		}
	}

	return qcom_coincell_chgr_config(&chgr, rset, vset, enable);
}

static const struct of_device_id qcom_coincell_match_table[] = {
	{ .compatible = "qcom,pm8941-coincell", },
	{}
};

MODULE_DEVICE_TABLE(of, qcom_coincell_match_table);

static struct platform_driver qcom_coincell_driver = {
	.driver	= {
		.name		= "qcom-spmi-coincell",
		.of_match_table	= qcom_coincell_match_table,
	},
	.probe		= qcom_coincell_probe,
};

module_platform_driver(qcom_coincell_driver);

MODULE_DESCRIPTION("Qualcomm PMIC coincell charger driver");
MODULE_LICENSE("GPL v2");
