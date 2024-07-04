// SPDX-License-Identifier: GPL-2.0-only
/*
 * DDR Self-Refresh Power Down (SRPD) support for Broadcom STB SoCs
 *
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define REG_MEMC_CNTRLR_CONFIG		0x00
#define  CNTRLR_CONFIG_LPDDR4_SHIFT	5
#define  CNTRLR_CONFIG_MASK		0xf
#define REG_MEMC_SRPD_CFG_21		0x20
#define REG_MEMC_SRPD_CFG_20		0x34
#define REG_MEMC_SRPD_CFG_1x		0x3c
#define INACT_COUNT_SHIFT		0
#define INACT_COUNT_MASK		0xffff
#define SRPD_EN_SHIFT			16

struct brcmstb_memc_data {
	u32 srpd_offset;
};

struct brcmstb_memc {
	struct device *dev;
	void __iomem *ddr_ctrl;
	unsigned int timeout_cycles;
	u32 frequency;
	u32 srpd_offset;
};

static int brcmstb_memc_uses_lpddr4(struct brcmstb_memc *memc)
{
	void __iomem *config = memc->ddr_ctrl + REG_MEMC_CNTRLR_CONFIG;
	u32 reg;

	reg = readl_relaxed(config) & CNTRLR_CONFIG_MASK;

	return reg == CNTRLR_CONFIG_LPDDR4_SHIFT;
}

static int brcmstb_memc_srpd_config(struct brcmstb_memc *memc,
				    unsigned int cycles)
{
	void __iomem *cfg = memc->ddr_ctrl + memc->srpd_offset;
	u32 val;

	/* Max timeout supported in HW */
	if (cycles > INACT_COUNT_MASK)
		return -EINVAL;

	memc->timeout_cycles = cycles;

	val = (cycles << INACT_COUNT_SHIFT) & INACT_COUNT_MASK;
	if (cycles)
		val |= BIT(SRPD_EN_SHIFT);

	writel_relaxed(val, cfg);
	/* Ensure the write is committed to the controller */
	(void)readl_relaxed(cfg);

	return 0;
}

static ssize_t frequency_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct brcmstb_memc *memc = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", memc->frequency);
}

static ssize_t srpd_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct brcmstb_memc *memc = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", memc->timeout_cycles);
}

static ssize_t srpd_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct brcmstb_memc *memc = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	/*
	 * Cannot change the inactivity timeout on LPDDR4 chips because the
	 * dynamic tuning process will also get affected by the inactivity
	 * timeout, thus making it non functional.
	 */
	if (brcmstb_memc_uses_lpddr4(memc))
		return -EOPNOTSUPP;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;

	ret = brcmstb_memc_srpd_config(memc, val);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR_RO(frequency);
static DEVICE_ATTR_RW(srpd);

static struct attribute *dev_attrs[] = {
	&dev_attr_frequency.attr,
	&dev_attr_srpd.attr,
	NULL,
};

static struct attribute_group dev_attr_group = {
	.attrs = dev_attrs,
};

static int brcmstb_memc_probe(struct platform_device *pdev)
{
	const struct brcmstb_memc_data *memc_data;
	struct device *dev = &pdev->dev;
	struct brcmstb_memc *memc;
	int ret;

	memc = devm_kzalloc(dev, sizeof(*memc), GFP_KERNEL);
	if (!memc)
		return -ENOMEM;

	dev_set_drvdata(dev, memc);

	memc_data = device_get_match_data(dev);
	memc->srpd_offset = memc_data->srpd_offset;

	memc->ddr_ctrl = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(memc->ddr_ctrl))
		return PTR_ERR(memc->ddr_ctrl);

	of_property_read_u32(pdev->dev.of_node, "clock-frequency",
			     &memc->frequency);

	ret = sysfs_create_group(&dev->kobj, &dev_attr_group);
	if (ret)
		return ret;

	return 0;
}

static void brcmstb_memc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	sysfs_remove_group(&dev->kobj, &dev_attr_group);
}

enum brcmstb_memc_hwtype {
	BRCMSTB_MEMC_V21,
	BRCMSTB_MEMC_V20,
	BRCMSTB_MEMC_V1X,
};

static const struct brcmstb_memc_data brcmstb_memc_versions[] = {
	{ .srpd_offset = REG_MEMC_SRPD_CFG_21 },
	{ .srpd_offset = REG_MEMC_SRPD_CFG_20 },
	{ .srpd_offset = REG_MEMC_SRPD_CFG_1x },
};

static const struct of_device_id brcmstb_memc_of_match[] = {
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-b.1.x",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V1X]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-b.2.0",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V20]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-b.2.1",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-b.2.2",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-b.2.3",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-b.2.5",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-b.2.6",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-b.2.7",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-b.2.8",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-b.3.0",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-b.3.1",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-c.1.0",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-c.1.1",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-c.1.2",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-c.1.3",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-c.1.4",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V21]
	},
	/* default to the original offset */
	{
		.compatible = "brcm,brcmstb-memc-ddr",
		.data = &brcmstb_memc_versions[BRCMSTB_MEMC_V1X]
	},
	{}
};
MODULE_DEVICE_TABLE(of, brcmstb_memc_of_match);

static int brcmstb_memc_suspend(struct device *dev)
{
	struct brcmstb_memc *memc = dev_get_drvdata(dev);
	void __iomem *cfg = memc->ddr_ctrl + memc->srpd_offset;
	u32 val;

	if (memc->timeout_cycles == 0)
		return 0;

	/*
	 * Disable SRPD prior to suspending the system since that can
	 * cause issues with other memory clients managed by the ARM
	 * trusted firmware to access memory.
	 */
	val = readl_relaxed(cfg);
	val &= ~BIT(SRPD_EN_SHIFT);
	writel_relaxed(val, cfg);
	/* Ensure the write is committed to the controller */
	(void)readl_relaxed(cfg);

	return 0;
}

static int brcmstb_memc_resume(struct device *dev)
{
	struct brcmstb_memc *memc = dev_get_drvdata(dev);

	if (memc->timeout_cycles == 0)
		return 0;

	return brcmstb_memc_srpd_config(memc, memc->timeout_cycles);
}

static DEFINE_SIMPLE_DEV_PM_OPS(brcmstb_memc_pm_ops, brcmstb_memc_suspend,
				brcmstb_memc_resume);

static struct platform_driver brcmstb_memc_driver = {
	.probe = brcmstb_memc_probe,
	.remove_new = brcmstb_memc_remove,
	.driver = {
		.name		= "brcmstb_memc",
		.of_match_table	= brcmstb_memc_of_match,
		.pm		= pm_ptr(&brcmstb_memc_pm_ops),
	},
};
module_platform_driver(brcmstb_memc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("DDR SRPD driver for Broadcom STB chips");
