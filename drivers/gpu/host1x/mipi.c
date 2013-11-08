/*
 * Copyright (C) 2013 NVIDIA Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/host1x.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "dev.h"

#define MIPI_CAL_CTRL			0x00
#define MIPI_CAL_CTRL_START		(1 << 0)

#define MIPI_CAL_AUTOCAL_CTRL		0x01

#define MIPI_CAL_STATUS			0x02
#define MIPI_CAL_STATUS_DONE		(1 << 16)
#define MIPI_CAL_STATUS_ACTIVE		(1 <<  0)

#define MIPI_CAL_CONFIG_CSIA		0x05
#define MIPI_CAL_CONFIG_CSIB		0x06
#define MIPI_CAL_CONFIG_CSIC		0x07
#define MIPI_CAL_CONFIG_CSID		0x08
#define MIPI_CAL_CONFIG_CSIE		0x09
#define MIPI_CAL_CONFIG_DSIA		0x0e
#define MIPI_CAL_CONFIG_DSIB		0x0f
#define MIPI_CAL_CONFIG_DSIC		0x10
#define MIPI_CAL_CONFIG_DSID		0x11

#define MIPI_CAL_CONFIG_SELECT		(1 << 21)
#define MIPI_CAL_CONFIG_HSPDOS(x)	(((x) & 0x1f) << 16)
#define MIPI_CAL_CONFIG_HSPUOS(x)	(((x) & 0x1f) <<  8)
#define MIPI_CAL_CONFIG_TERMOS(x)	(((x) & 0x1f) <<  0)

#define MIPI_CAL_BIAS_PAD_CFG0		0x16
#define MIPI_CAL_BIAS_PAD_PDVCLAMP	(1 << 1)
#define MIPI_CAL_BIAS_PAD_E_VCLAMP_REF	(1 << 0)

#define MIPI_CAL_BIAS_PAD_CFG1		0x17

#define MIPI_CAL_BIAS_PAD_CFG2		0x18
#define MIPI_CAL_BIAS_PAD_PDVREG	(1 << 1)

static const struct module {
	unsigned long reg;
} modules[] = {
	{ .reg = MIPI_CAL_CONFIG_CSIA },
	{ .reg = MIPI_CAL_CONFIG_CSIB },
	{ .reg = MIPI_CAL_CONFIG_CSIC },
	{ .reg = MIPI_CAL_CONFIG_CSID },
	{ .reg = MIPI_CAL_CONFIG_CSIE },
	{ .reg = MIPI_CAL_CONFIG_DSIA },
	{ .reg = MIPI_CAL_CONFIG_DSIB },
	{ .reg = MIPI_CAL_CONFIG_DSIC },
	{ .reg = MIPI_CAL_CONFIG_DSID },
};

struct tegra_mipi {
	void __iomem *regs;
	struct mutex lock;
	struct clk *clk;
};

struct tegra_mipi_device {
	struct platform_device *pdev;
	struct tegra_mipi *mipi;
	struct device *device;
	unsigned long pads;
};

static inline unsigned long tegra_mipi_readl(struct tegra_mipi *mipi,
					     unsigned long reg)
{
	return readl(mipi->regs + (reg << 2));
}

static inline void tegra_mipi_writel(struct tegra_mipi *mipi,
				     unsigned long value, unsigned long reg)
{
	writel(value, mipi->regs + (reg << 2));
}

struct tegra_mipi_device *tegra_mipi_request(struct device *device)
{
	struct device_node *np = device->of_node;
	struct tegra_mipi_device *dev;
	struct of_phandle_args args;
	int err;

	err = of_parse_phandle_with_args(np, "nvidia,mipi-calibrate",
					 "#nvidia,mipi-calibrate-cells", 0,
					 &args);
	if (err < 0)
		return ERR_PTR(err);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		of_node_put(args.np);
		err = -ENOMEM;
		goto out;
	}

	dev->pdev = of_find_device_by_node(args.np);
	if (!dev->pdev) {
		of_node_put(args.np);
		err = -ENODEV;
		goto free;
	}

	of_node_put(args.np);

	dev->mipi = platform_get_drvdata(dev->pdev);
	if (!dev->mipi) {
		err = -EPROBE_DEFER;
		goto pdev_put;
	}

	dev->pads = args.args[0];
	dev->device = device;

	return dev;

pdev_put:
	platform_device_put(dev->pdev);
free:
	kfree(dev);
out:
	return ERR_PTR(err);
}
EXPORT_SYMBOL(tegra_mipi_request);

void tegra_mipi_free(struct tegra_mipi_device *device)
{
	platform_device_put(device->pdev);
	kfree(device);
}
EXPORT_SYMBOL(tegra_mipi_free);

static int tegra_mipi_wait(struct tegra_mipi *mipi)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(250);
	unsigned long value;

	while (time_before(jiffies, timeout)) {
		value = tegra_mipi_readl(mipi, MIPI_CAL_STATUS);
		if ((value & MIPI_CAL_STATUS_ACTIVE) == 0 &&
		    (value & MIPI_CAL_STATUS_DONE) != 0)
			return 0;

		usleep_range(10, 50);
	}

	return -ETIMEDOUT;
}

int tegra_mipi_calibrate(struct tegra_mipi_device *device)
{
	unsigned long value;
	unsigned int i;
	int err;

	err = clk_enable(device->mipi->clk);
	if (err < 0)
		return err;

	mutex_lock(&device->mipi->lock);

	value = tegra_mipi_readl(device->mipi, MIPI_CAL_BIAS_PAD_CFG0);
	value &= ~MIPI_CAL_BIAS_PAD_PDVCLAMP;
	value |= MIPI_CAL_BIAS_PAD_E_VCLAMP_REF;
	tegra_mipi_writel(device->mipi, value, MIPI_CAL_BIAS_PAD_CFG0);

	value = tegra_mipi_readl(device->mipi, MIPI_CAL_BIAS_PAD_CFG2);
	value &= ~MIPI_CAL_BIAS_PAD_PDVREG;
	tegra_mipi_writel(device->mipi, value, MIPI_CAL_BIAS_PAD_CFG2);

	for (i = 0; i < ARRAY_SIZE(modules); i++) {
		if (device->pads & BIT(i))
			value = MIPI_CAL_CONFIG_SELECT |
				MIPI_CAL_CONFIG_HSPDOS(0) |
				MIPI_CAL_CONFIG_HSPUOS(4) |
				MIPI_CAL_CONFIG_TERMOS(5);
		else
			value = 0;

		tegra_mipi_writel(device->mipi, value, modules[i].reg);
	}

	tegra_mipi_writel(device->mipi, MIPI_CAL_CTRL_START, MIPI_CAL_CTRL);

	err = tegra_mipi_wait(device->mipi);

	mutex_unlock(&device->mipi->lock);
	clk_disable(device->mipi->clk);

	return err;
}
EXPORT_SYMBOL(tegra_mipi_calibrate);

static int tegra_mipi_probe(struct platform_device *pdev)
{
	struct tegra_mipi *mipi;
	struct resource *res;
	int err;

	mipi = devm_kzalloc(&pdev->dev, sizeof(*mipi), GFP_KERNEL);
	if (!mipi)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mipi->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mipi->regs))
		return PTR_ERR(mipi->regs);

	mutex_init(&mipi->lock);

	mipi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mipi->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(mipi->clk);
	}

	err = clk_prepare(mipi->clk);
	if (err < 0)
		return err;

	platform_set_drvdata(pdev, mipi);

	return 0;
}

static int tegra_mipi_remove(struct platform_device *pdev)
{
	struct tegra_mipi *mipi = platform_get_drvdata(pdev);

	clk_unprepare(mipi->clk);

	return 0;
}

static struct of_device_id tegra_mipi_of_match[] = {
	{ .compatible = "nvidia,tegra114-mipi", },
	{ },
};

struct platform_driver tegra_mipi_driver = {
	.driver = {
		.name = "tegra-mipi",
		.of_match_table = tegra_mipi_of_match,
	},
	.probe = tegra_mipi_probe,
	.remove = tegra_mipi_remove,
};
