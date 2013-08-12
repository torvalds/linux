/*
 * Copyright Altera Corporation (C) 2013.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Credit:
 * Walter Goossens
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>

#define DRV_NAME	"altera_sysid"

struct altera_sysid {
	void __iomem		*regs;
};

/* System ID Registers*/
#define SYSID_REG_ID		(0x0)
#define SYSID_REG_TIMESTAMP	(0x4)

static ssize_t altera_sysid_show_id(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct altera_sysid *sysid = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", readl(sysid->regs + SYSID_REG_ID));
}

static ssize_t altera_sysid_show_timestamp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned int reg;
	struct tm timestamp;
	struct altera_sysid *sysid = dev_get_drvdata(dev);

	reg = readl(sysid->regs + SYSID_REG_TIMESTAMP);

	time_to_tm(reg, 0, &timestamp);

	return sprintf(buf, "%u (%u-%u-%u %u:%u:%u UTC)\n", reg,
		(unsigned int)(timestamp.tm_year + 1900),
		timestamp.tm_mon + 1, timestamp.tm_mday, timestamp.tm_hour,
		timestamp.tm_min, timestamp.tm_sec);
}

static DEVICE_ATTR(id, S_IRUGO, altera_sysid_show_id, NULL);
static DEVICE_ATTR(timestamp, S_IRUGO, altera_sysid_show_timestamp, NULL);

static struct attribute *altera_sysid_attrs[] = {
	&dev_attr_id.attr,
	&dev_attr_timestamp.attr,
	NULL,
};

struct attribute_group altera_sysid_attr_group = {
	.name = "sysid",
	.attrs = altera_sysid_attrs,
};

static int altera_sysid_probe(struct platform_device *pdev)
{
	struct altera_sysid *sysid;
	struct resource	*regs;

	sysid = devm_kzalloc(&pdev->dev, sizeof(struct altera_sysid),
		GFP_KERNEL);
	if (!sysid)
		return -ENOMEM;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;

	sysid->regs = devm_request_and_ioremap(&pdev->dev, regs);
	if (!sysid->regs)
		return -ENOMEM;

	platform_set_drvdata(pdev, sysid);

	return sysfs_create_group(&pdev->dev.kobj, &altera_sysid_attr_group);
}

static int altera_sysid_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &altera_sysid_attr_group);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id altera_sysid_match[] = {
	{ .compatible = "altr,sysid-1.0" },
	{ /* Sentinel */ }
};

MODULE_DEVICE_TABLE(of, altera_sysid_match);

static struct platform_driver altera_sysid_platform_driver = {
	.driver = {
		.name		= DRV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(altera_sysid_match),
	},
	.remove			= altera_sysid_remove,
};

static int __init altera_sysid_init(void)
{
	return platform_driver_probe(&altera_sysid_platform_driver,
		altera_sysid_probe);
}

static void __exit altera_sysid_exit(void)
{
	platform_driver_unregister(&altera_sysid_platform_driver);
}

module_init(altera_sysid_init);
module_exit(altera_sysid_exit);

MODULE_AUTHOR("Ley Foon Tan <lftan@altera.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Altera System ID driver");
MODULE_ALIAS("platform:" DRV_NAME);
