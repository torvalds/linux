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

/* Same as ANADIG_DIGPROG_IMX7D */
#define ANADIG_DIGPROG_IMX8MM	0x800

struct imx8_soc_data {
	char *name;
	int (*soc_revision)(u32 *socrev, u64 *socuid);
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

static int imx8mq_soc_revision(u32 *socrev, u64 *socuid)
{
	struct device_node *np __free(device_node) =
		of_find_compatible_node(NULL, NULL, "fsl,imx8mq-ocotp");
	void __iomem *ocotp_base;
	u32 magic;
	u32 rev;
	struct clk *clk;
	int ret;

	if (!np)
		return -EINVAL;

	ocotp_base = of_iomap(np, 0);
	if (!ocotp_base)
		return -EINVAL;

	clk = of_clk_get_by_name(np, NULL);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto err_clk;
	}

	clk_prepare_enable(clk);

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

	*socuid = readl_relaxed(ocotp_base + OCOTP_UID_HIGH);
	*socuid <<= 32;
	*socuid |= readl_relaxed(ocotp_base + OCOTP_UID_LOW);

	*socrev = rev;

	clk_disable_unprepare(clk);
	clk_put(clk);
	iounmap(ocotp_base);

	return 0;

err_clk:
	iounmap(ocotp_base);
	return ret;
}

static int imx8mm_soc_uid(u64 *socuid)
{
	struct device_node *np __free(device_node) =
		of_find_compatible_node(NULL, NULL, "fsl,imx8mm-ocotp");
	void __iomem *ocotp_base;
	struct clk *clk;
	int ret = 0;
	u32 offset = of_machine_is_compatible("fsl,imx8mp") ?
		     IMX8MP_OCOTP_UID_OFFSET : 0;

	if (!np)
		return -EINVAL;

	ocotp_base = of_iomap(np, 0);
	if (!ocotp_base)
		return -EINVAL;

	clk = of_clk_get_by_name(np, NULL);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto err_clk;
	}

	clk_prepare_enable(clk);

	*socuid = readl_relaxed(ocotp_base + OCOTP_UID_HIGH + offset);
	*socuid <<= 32;
	*socuid |= readl_relaxed(ocotp_base + OCOTP_UID_LOW + offset);

	clk_disable_unprepare(clk);
	clk_put(clk);

err_clk:
	iounmap(ocotp_base);
	return ret;
}

static int imx8mm_soc_revision(u32 *socrev, u64 *socuid)
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

	return imx8mm_soc_uid(socuid);
}

static const struct imx8_soc_data imx8mq_soc_data = {
	.name = "i.MX8MQ",
	.soc_revision = imx8mq_soc_revision,
};

static const struct imx8_soc_data imx8mm_soc_data = {
	.name = "i.MX8MM",
	.soc_revision = imx8mm_soc_revision,
};

static const struct imx8_soc_data imx8mn_soc_data = {
	.name = "i.MX8MN",
	.soc_revision = imx8mm_soc_revision,
};

static const struct imx8_soc_data imx8mp_soc_data = {
	.name = "i.MX8MP",
	.soc_revision = imx8mm_soc_revision,
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

static int imx8m_soc_probe(struct platform_device *pdev)
{
	struct soc_device_attribute *soc_dev_attr;
	const struct imx8_soc_data *data;
	struct device *dev = &pdev->dev;
	const struct of_device_id *id;
	struct soc_device *soc_dev;
	u32 soc_rev = 0;
	u64 soc_uid = 0;
	int ret;

	soc_dev_attr = devm_kzalloc(dev, sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

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
		if (data->soc_revision) {
			ret = data->soc_revision(&soc_rev, &soc_uid);
			if (ret)
				return ret;
		}
	}

	soc_dev_attr->revision = imx8_revision(dev, soc_rev);
	if (!soc_dev_attr->revision)
		return -ENOMEM;

	soc_dev_attr->serial_number = devm_kasprintf(dev, GFP_KERNEL, "%016llX", soc_uid);
	if (!soc_dev_attr->serial_number)
		return -ENOMEM;

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev))
		return PTR_ERR(soc_dev);

	pr_info("SoC: %s revision %s\n", soc_dev_attr->soc_id,
		soc_dev_attr->revision);

	if (IS_ENABLED(CONFIG_ARM_IMX_CPUFREQ_DT))
		platform_device_register_simple("imx-cpufreq-dt", -1, NULL, 0);

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
