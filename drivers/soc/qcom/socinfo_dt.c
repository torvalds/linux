// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sys_soc.h>
#include <linux/types.h>
#include <linux/of.h>

/*
 * SoC version type with major number in the upper 16 bits and minor
 * number in the lower 16 bits.
 */
#define SOCINFO_MAJOR(ver) (((ver) >> 16) & 0xffff)
#define SOCINFO_MINOR(ver) ((ver) & 0xffff)

static struct soc_device *soc_dev;
static struct platform_device *soc_pdev;

static int qcom_dt_socinfo_probe(struct platform_device *pdev)
{
	struct soc_device_attribute *attr;
	struct device_node *root;
	u32 val[2], soc_id, revision;
	int ret;

	attr = devm_kzalloc(&pdev->dev, sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	root = of_find_node_by_path("/");
	ret = of_property_read_u32_array(root, "qcom,msm-id", val, 2);
	if (ret) {
		dev_err(&pdev->dev, "qcom,msm-id error\n");
		return ret;
	}
	attr->machine = of_get_property(root, "qcom,msm-name", NULL);
	of_node_put(root);

	soc_id = le32_to_cpu(val[0]);
	revision = le32_to_cpu(val[1]);
	attr->family = "Snapdragon";
	attr->soc_id = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%d", soc_id);
	attr->revision = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%u.%u",
			SOCINFO_MAJOR(revision), SOCINFO_MINOR(revision));

	soc_dev = soc_device_register(attr);
	return PTR_ERR_OR_ZERO(soc_dev);
}

static int qcom_dt_socinfo_remove(struct platform_device *pdev)
{
	soc_device_unregister(soc_dev);

	return 0;
}

static struct platform_driver qcom_dt_socinfo_driver = {
	.probe = qcom_dt_socinfo_probe,
	.remove = qcom_dt_socinfo_remove,
	.driver  = {
		.name = "qcom-dt-socinfo",
	},
};

static int __init qcom_dt_socinfo_init(void)
{
	int ret;

	ret = platform_driver_register(&qcom_dt_socinfo_driver);
	if (ret)
		return ret;

	soc_pdev = platform_device_register_simple("qcom-dt-socinfo", -1, NULL, 0);
	if (IS_ERR(soc_pdev)) {
		platform_driver_unregister(&qcom_dt_socinfo_driver);
		return PTR_ERR(soc_pdev);
	}

	return 0;
}
module_init(qcom_dt_socinfo_init);

static void __exit qcom_dt_socinfo_exit(void)
{
	platform_device_unregister(soc_pdev);
	platform_driver_unregister(&qcom_dt_socinfo_driver);
}
module_exit(qcom_dt_socinfo_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. DT based SoCinfo driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:qcom-dt-socinfo");
