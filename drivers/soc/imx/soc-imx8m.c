// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include <linux/platform_device.h>
#include <linux/arm-smccc.h>
#include <linux/of.h>
#include <linux/clk.h>

#define REV_B1				0x21

#define IMX8MQ_SW_INFO_B1		0x40
#define IMX8MQ_SW_MAGIC_B1		0xff0055aa

#define IMX_SIP_GET_SOC_INFO		0xc2000006

#define OCOTP_UID_LOW			0x410
#define OCOTP_UID_HIGH			0x420

#define IMX8MP_OCOTP_UID_OFFSET		0x10
#define IMX8MP_OCOTP_UID_HIGH		0xE00

/* Same as ANADIG_DIGPROG_IMX7D */
#define ANADIG_DIGPROG_IMX8MM	0x800

struct imx8_soc_data {
	char *name;
	const char *ocotp_compatible;
	int (*soc_revision)(struct platform_device *pdev, u32 *socrev);
	int (*soc_uid)(struct platform_device *pdev, u64 *socuid);
};

struct imx8_soc_drvdata {
	void __iomem *ocotp_base;
	struct clk *clk;
};

#ifdef CONFIG_HAVE_ARM_SMCCC
static u32 imx8mq_soc_revision_from_atf(void)
{
	struct arm_smccc_res res;

	arm_smccc_smc(IMX_SIP_GET_SOC_INFO, 0, 0, 0, 0, 0, 0, 0, &res);

	if (res.a0 == SMCCC_RET_NOT_SUPPORTED)
		return 0;
	else
		return res.a0 & 0xff;
}
#else
static inline u32 imx8mq_soc_revision_from_atf(void) { return 0; };
#endif

static int imx8m_soc_uid(struct platform_device *pdev, u64 *socuid)
{
	struct imx8_soc_drvdata *drvdata = platform_get_drvdata(pdev);
	void __iomem *ocotp_base = drvdata->ocotp_base;

	*socuid = readl_relaxed(ocotp_base + OCOTP_UID_HIGH);
	*socuid <<= 32;
	*socuid |= readl_relaxed(ocotp_base + OCOTP_UID_LOW);

	return 0;
}

static int imx8mq_soc_revision(struct platform_device *pdev, u32 *socrev)
{
	struct imx8_soc_drvdata *drvdata = platform_get_drvdata(pdev);
	void __iomem *ocotp_base = drvdata->ocotp_base;
	u32 magic;
	u32 rev;

	/*
	 * SOC revision on older imx8mq is not available in fuses so query
	 * the value from ATF instead.
	 */
	rev = imx8mq_soc_revision_from_atf();
	if (!rev) {
		magic = readl_relaxed(ocotp_base + IMX8MQ_SW_INFO_B1);
		if (magic == IMX8MQ_SW_MAGIC_B1)
			rev = REV_B1;
	}

	*socrev = rev;

	return 0;
}

static int imx8mp_soc_uid(struct platform_device *pdev, u64 *socuid)
{
	struct imx8_soc_drvdata *drvdata = platform_get_drvdata(pdev);
	void __iomem *ocotp_base = drvdata->ocotp_base;

	socuid[0] = readl_relaxed(ocotp_base + OCOTP_UID_HIGH + IMX8MP_OCOTP_UID_OFFSET);
	socuid[0] <<= 32;
	socuid[0] |= readl_relaxed(ocotp_base + OCOTP_UID_LOW + IMX8MP_OCOTP_UID_OFFSET);

	socuid[1] = readl_relaxed(ocotp_base + IMX8MP_OCOTP_UID_HIGH + 0x10);
	socuid[1] <<= 32;
	socuid[1] |= readl_relaxed(ocotp_base + IMX8MP_OCOTP_UID_HIGH);

	return 0;
}

static int imx8mm_soc_revision(struct platform_device *pdev, u32 *socrev)
{
	struct device_node *np __free(device_node) =
		of_find_compatible_node(NULL, NULL, "fsl,imx8mm-anatop");
	void __iomem *anatop_base;

	if (!np)
		return -EINVAL;

	anatop_base = of_iomap(np, 0);
	if (!anatop_base)
		return -EINVAL;

	*socrev = readl_relaxed(anatop_base + ANADIG_DIGPROG_IMX8MM);

	iounmap(anatop_base);

	return 0;
}

static int imx8m_soc_prepare(struct platform_device *pdev, const char *ocotp_compatible)
{
	struct device_node *np __free(device_node) =
		of_find_compatible_node(NULL, NULL, ocotp_compatible);
	struct imx8_soc_drvdata *drvdata = platform_get_drvdata(pdev);
	int ret = 0;

	if (!np)
		return -EINVAL;

	drvdata->ocotp_base = of_iomap(np, 0);
	if (!drvdata->ocotp_base)
		return -EINVAL;

	drvdata->clk = of_clk_get_by_name(np, NULL);
	if (IS_ERR(drvdata->clk)) {
		ret = PTR_ERR(drvdata->clk);
		goto err_clk;
	}

	return clk_prepare_enable(drvdata->clk);

err_clk:
	iounmap(drvdata->ocotp_base);
	return ret;
}

static void imx8m_soc_unprepare(struct platform_device *pdev)
{
	struct imx8_soc_drvdata *drvdata = platform_get_drvdata(pdev);

	clk_disable_unprepare(drvdata->clk);
	clk_put(drvdata->clk);
	iounmap(drvdata->ocotp_base);
}

static const struct imx8_soc_data imx8mq_soc_data = {
	.name = "i.MX8MQ",
	.ocotp_compatible = "fsl,imx8mq-ocotp",
	.soc_revision = imx8mq_soc_revision,
	.soc_uid = imx8m_soc_uid,
};

static const struct imx8_soc_data imx8mm_soc_data = {
	.name = "i.MX8MM",
	.ocotp_compatible = "fsl,imx8mm-ocotp",
	.soc_revision = imx8mm_soc_revision,
	.soc_uid = imx8m_soc_uid,
};

static const struct imx8_soc_data imx8mn_soc_data = {
	.name = "i.MX8MN",
	.ocotp_compatible = "fsl,imx8mm-ocotp",
	.soc_revision = imx8mm_soc_revision,
	.soc_uid = imx8m_soc_uid,
};

static const struct imx8_soc_data imx8mp_soc_data = {
	.name = "i.MX8MP",
	.ocotp_compatible = "fsl,imx8mm-ocotp",
	.soc_revision = imx8mm_soc_revision,
	.soc_uid = imx8mp_soc_uid,
};

static __maybe_unused const struct of_device_id imx8_soc_match[] = {
	{ .compatible = "fsl,imx8mq", .data = &imx8mq_soc_data, },
	{ .compatible = "fsl,imx8mm", .data = &imx8mm_soc_data, },
	{ .compatible = "fsl,imx8mn", .data = &imx8mn_soc_data, },
	{ .compatible = "fsl,imx8mp", .data = &imx8mp_soc_data, },
	{ }
};

#define imx8_revision(dev, soc_rev) \
	(soc_rev) ? \
	devm_kasprintf((dev), GFP_KERNEL, "%d.%d", ((soc_rev) >> 4) & 0xf, (soc_rev) & 0xf) : \
	"unknown"

static void imx8m_unregister_soc(void *data)
{
	soc_device_unregister(data);
}

static void imx8m_unregister_cpufreq(void *data)
{
	platform_device_unregister(data);
}

static int imx8m_soc_probe(struct platform_device *pdev)
{
	struct soc_device_attribute *soc_dev_attr;
	struct platform_device *cpufreq_dev;
	const struct imx8_soc_data *data;
	struct imx8_soc_drvdata *drvdata;
	struct device *dev = &pdev->dev;
	const struct of_device_id *id;
	struct soc_device *soc_dev;
	u32 soc_rev = 0;
	u64 soc_uid[2] = {0, 0};
	int ret;

	soc_dev_attr = devm_kzalloc(dev, sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	platform_set_drvdata(pdev, drvdata);

	soc_dev_attr->family = "Freescale i.MX";

	ret = of_property_read_string(of_root, "model", &soc_dev_attr->machine);
	if (ret)
		return ret;

	id = of_match_node(imx8_soc_match, of_root);
	if (!id)
		return -ENODEV;

	data = id->data;
	if (data) {
		soc_dev_attr->soc_id = data->name;
		ret = imx8m_soc_prepare(pdev, data->ocotp_compatible);
		if (ret)
			return ret;

		if (data->soc_revision) {
			ret = data->soc_revision(pdev, &soc_rev);
			if (ret) {
				imx8m_soc_unprepare(pdev);
				return ret;
			}
		}
		if (data->soc_uid) {
			ret = data->soc_uid(pdev, soc_uid);
			if (ret) {
				imx8m_soc_unprepare(pdev);
				return ret;
			}
		}
		imx8m_soc_unprepare(pdev);
	}

	soc_dev_attr->revision = imx8_revision(dev, soc_rev);
	if (!soc_dev_attr->revision)
		return -ENOMEM;

	if (soc_uid[1])
		soc_dev_attr->serial_number = devm_kasprintf(dev, GFP_KERNEL, "%016llX%016llX",
							     soc_uid[1], soc_uid[0]);
	else
		soc_dev_attr->serial_number = devm_kasprintf(dev, GFP_KERNEL, "%016llX",
							     soc_uid[0]);
	if (!soc_dev_attr->serial_number)
		return -ENOMEM;

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev))
		return PTR_ERR(soc_dev);

	ret = devm_add_action(dev, imx8m_unregister_soc, soc_dev);
	if (ret)
		return ret;

	pr_info("SoC: %s revision %s\n", soc_dev_attr->soc_id,
		soc_dev_attr->revision);

	if (IS_ENABLED(CONFIG_ARM_IMX_CPUFREQ_DT)) {
		cpufreq_dev = platform_device_register_simple("imx-cpufreq-dt", -1, NULL, 0);
		if (IS_ERR(cpufreq_dev))
			return dev_err_probe(dev, PTR_ERR(cpufreq_dev),
					     "Failed to register imx-cpufreq-dev device\n");
		ret = devm_add_action(dev, imx8m_unregister_cpufreq, cpufreq_dev);
		if (ret)
			return ret;
	}

	return 0;
}

static struct platform_driver imx8m_soc_driver = {
	.probe = imx8m_soc_probe,
	.driver = {
		.name = "imx8m-soc",
	},
};

static int __init imx8_soc_init(void)
{
	struct platform_device *pdev;
	int ret;

	/* No match means this is non-i.MX8M hardware, do nothing. */
	if (!of_match_node(imx8_soc_match, of_root))
		return 0;

	ret = platform_driver_register(&imx8m_soc_driver);
	if (ret) {
		pr_err("Failed to register imx8m-soc platform driver: %d\n", ret);
		return ret;
	}

	pdev = platform_device_register_simple("imx8m-soc", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		pr_err("Failed to register imx8m-soc platform device: %ld\n", PTR_ERR(pdev));
		platform_driver_unregister(&imx8m_soc_driver);
		return PTR_ERR(pdev);
	}

	return 0;
}
device_initcall(imx8_soc_init);
MODULE_DESCRIPTION("NXP i.MX8M SoC driver");
MODULE_LICENSE("GPL");
