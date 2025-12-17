// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/arm-smccc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#define IMX_SIP_GET_SOC_INFO	0xc2000006
#define SOC_ID(x)		(((x) & 0xFF) ? ((x) & 0xFFFF) >> 4 : ((x) & 0xFFFF) >> 8)
#define SOC_REV_MAJOR(x)	((((x) >> 28) & 0xF) - 0x9)
#define SOC_REV_MINOR(x)	(((x) >> 24) & 0xF)

static int imx9_soc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct soc_device_attribute *attr;
	struct arm_smccc_res res;
	struct soc_device *sdev;
	u32 soc_id, rev_major, rev_minor;
	u64 uid127_64, uid63_0;
	int err;

	attr = devm_kzalloc(dev, sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	err = of_property_read_string(of_root, "model", &attr->machine);
	if (err)
		return dev_err_probe(dev, err, "%s: missing model property\n", __func__);

	attr->family = devm_kasprintf(dev, GFP_KERNEL, "Freescale i.MX");

	/*
	 * Retrieve the soc id, rev & uid info:
	 * res.a1[31:16]: soc revision;
	 * res.a1[15:0]: soc id;
	 * res.a2: uid[127:64];
	 * res.a3: uid[63:0];
	 */
	arm_smccc_smc(IMX_SIP_GET_SOC_INFO, 0, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0 != SMCCC_RET_SUCCESS)
		return dev_err_probe(dev, -EINVAL, "%s: SMC failed: 0x%lx\n", __func__, res.a0);

	soc_id = SOC_ID(res.a1);
	rev_major = SOC_REV_MAJOR(res.a1);
	rev_minor = SOC_REV_MINOR(res.a1);

	attr->soc_id = devm_kasprintf(dev, GFP_KERNEL, "i.MX%2x", soc_id);
	attr->revision = devm_kasprintf(dev, GFP_KERNEL, "%d.%d", rev_major, rev_minor);

	uid127_64 = res.a2;
	uid63_0 = res.a3;
	attr->serial_number = devm_kasprintf(dev, GFP_KERNEL, "%016llx%016llx", uid127_64, uid63_0);

	sdev = soc_device_register(attr);
	if (IS_ERR(sdev))
		return dev_err_probe(dev, PTR_ERR(sdev),
				     "%s failed to register SoC as a device\n", __func__);

	return 0;
}

static __maybe_unused const struct of_device_id imx9_soc_match[] = {
	{ .compatible = "fsl,imx93", },
	{ .compatible = "fsl,imx94", },
	{ .compatible = "fsl,imx95", },
	{ .compatible = "fsl,imx952", },
	{ }
};

#define IMX_SOC_DRIVER	"imx9-soc"

static struct platform_driver imx9_soc_driver = {
	.probe = imx9_soc_probe,
	.driver = {
		.name = IMX_SOC_DRIVER,
	},
};

static int __init imx9_soc_init(void)
{
	int ret;
	struct platform_device *pdev;

	/* No match means it is not an i.MX 9 series SoC, do nothing. */
	if (!of_match_node(imx9_soc_match, of_root))
		return 0;

	ret = platform_driver_register(&imx9_soc_driver);
	if (ret) {
		pr_err("failed to register imx9_soc platform driver: %d\n", ret);
		return ret;
	}

	pdev = platform_device_register_simple(IMX_SOC_DRIVER, -1, NULL, 0);
	if (IS_ERR(pdev)) {
		pr_err("failed to register imx9_soc platform device: %ld\n", PTR_ERR(pdev));
		platform_driver_unregister(&imx9_soc_driver);
		return PTR_ERR(pdev);
	}

	return 0;
}
device_initcall(imx9_soc_init);

MODULE_AUTHOR("NXP");
MODULE_DESCRIPTION("NXP i.MX9 SoC");
MODULE_LICENSE("GPL");
