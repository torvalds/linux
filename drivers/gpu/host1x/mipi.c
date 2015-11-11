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
#define MIPI_CAL_CTRL_NOISE_FILTER(x)	(((x) & 0xf) << 26)
#define MIPI_CAL_CTRL_PRESCALE(x)	(((x) & 0x3) << 24)
#define MIPI_CAL_CTRL_CLKEN_OVR		(1 << 4)
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
#define MIPI_CAL_CONFIG_CSIF		0x0a
#define MIPI_CAL_CONFIG_DSIA		0x0e
#define MIPI_CAL_CONFIG_DSIB		0x0f
#define MIPI_CAL_CONFIG_DSIC		0x10
#define MIPI_CAL_CONFIG_DSID		0x11

#define MIPI_CAL_CONFIG_DSIA_CLK	0x19
#define MIPI_CAL_CONFIG_DSIB_CLK	0x1a
#define MIPI_CAL_CONFIG_CSIAB_CLK	0x1b
#define MIPI_CAL_CONFIG_DSIC_CLK	0x1c
#define MIPI_CAL_CONFIG_CSICD_CLK	0x1c
#define MIPI_CAL_CONFIG_DSID_CLK	0x1d
#define MIPI_CAL_CONFIG_CSIE_CLK	0x1d

/* for data and clock lanes */
#define MIPI_CAL_CONFIG_SELECT		(1 << 21)

/* for data lanes */
#define MIPI_CAL_CONFIG_HSPDOS(x)	(((x) & 0x1f) << 16)
#define MIPI_CAL_CONFIG_HSPUOS(x)	(((x) & 0x1f) <<  8)
#define MIPI_CAL_CONFIG_TERMOS(x)	(((x) & 0x1f) <<  0)

/* for clock lanes */
#define MIPI_CAL_CONFIG_HSCLKPDOSD(x)	(((x) & 0x1f) <<  8)
#define MIPI_CAL_CONFIG_HSCLKPUOSD(x)	(((x) & 0x1f) <<  0)

#define MIPI_CAL_BIAS_PAD_CFG0		0x16
#define MIPI_CAL_BIAS_PAD_PDVCLAMP	(1 << 1)
#define MIPI_CAL_BIAS_PAD_E_VCLAMP_REF	(1 << 0)

#define MIPI_CAL_BIAS_PAD_CFG1		0x17
#define MIPI_CAL_BIAS_PAD_DRV_DN_REF(x) (((x) & 0x7) << 16)
#define MIPI_CAL_BIAS_PAD_DRV_UP_REF(x) (((x) & 0x7) << 8)

#define MIPI_CAL_BIAS_PAD_CFG2		0x18
#define MIPI_CAL_BIAS_PAD_VCLAMP(x)	(((x) & 0x7) << 16)
#define MIPI_CAL_BIAS_PAD_VAUXP(x)	(((x) & 0x7) << 4)
#define MIPI_CAL_BIAS_PAD_PDVREG	(1 << 1)

struct tegra_mipi_pad {
	unsigned long data;
	unsigned long clk;
};

struct tegra_mipi_soc {
	bool has_clk_lane;
	const struct tegra_mipi_pad *pads;
	unsigned int num_pads;

	bool clock_enable_override;
	bool needs_vclamp_ref;

	/* bias pad configuration settings */
	u8 pad_drive_down_ref;
	u8 pad_drive_up_ref;

	u8 pad_vclamp_level;
	u8 pad_vauxp_level;

	/* calibration settings for data lanes */
	u8 hspdos;
	u8 hspuos;
	u8 termos;

	/* calibration settings for clock lanes */
	u8 hsclkpdos;
	u8 hsclkpuos;
};

struct tegra_mipi {
	const struct tegra_mipi_soc *soc;
	struct device *dev;
	void __iomem *regs;
	struct mutex lock;
	struct clk *clk;

	unsigned long usage_count;
};

struct tegra_mipi_device {
	struct platform_device *pdev;
	struct tegra_mipi *mipi;
	struct device *device;
	unsigned long pads;
};

static inline u32 tegra_mipi_readl(struct tegra_mipi *mipi,
				   unsigned long offset)
{
	return readl(mipi->regs + (offset << 2));
}

static inline void tegra_mipi_writel(struct tegra_mipi *mipi, u32 value,
				     unsigned long offset)
{
	writel(value, mipi->regs + (offset << 2));
}

static int tegra_mipi_power_up(struct tegra_mipi *mipi)
{
	u32 value;
	int err;

	err = clk_enable(mipi->clk);
	if (err < 0)
		return err;

	value = tegra_mipi_readl(mipi, MIPI_CAL_BIAS_PAD_CFG0);
	value &= ~MIPI_CAL_BIAS_PAD_PDVCLAMP;

	if (mipi->soc->needs_vclamp_ref)
		value |= MIPI_CAL_BIAS_PAD_E_VCLAMP_REF;

	tegra_mipi_writel(mipi, value, MIPI_CAL_BIAS_PAD_CFG0);

	value = tegra_mipi_readl(mipi, MIPI_CAL_BIAS_PAD_CFG2);
	value &= ~MIPI_CAL_BIAS_PAD_PDVREG;
	tegra_mipi_writel(mipi, value, MIPI_CAL_BIAS_PAD_CFG2);

	clk_disable(mipi->clk);

	return 0;
}

static int tegra_mipi_power_down(struct tegra_mipi *mipi)
{
	u32 value;
	int err;

	err = clk_enable(mipi->clk);
	if (err < 0)
		return err;

	/*
	 * The MIPI_CAL_BIAS_PAD_PDVREG controls a voltage regulator that
	 * supplies the DSI pads. This must be kept enabled until none of the
	 * DSI lanes are used anymore.
	 */
	value = tegra_mipi_readl(mipi, MIPI_CAL_BIAS_PAD_CFG2);
	value |= MIPI_CAL_BIAS_PAD_PDVREG;
	tegra_mipi_writel(mipi, value, MIPI_CAL_BIAS_PAD_CFG2);

	/*
	 * MIPI_CAL_BIAS_PAD_PDVCLAMP and MIPI_CAL_BIAS_PAD_E_VCLAMP_REF
	 * control a regulator that supplies current to the pre-driver logic.
	 * Powering down this regulator causes DSI to fail, so it must remain
	 * powered on until none of the DSI lanes are used anymore.
	 */
	value = tegra_mipi_readl(mipi, MIPI_CAL_BIAS_PAD_CFG0);

	if (mipi->soc->needs_vclamp_ref)
		value &= ~MIPI_CAL_BIAS_PAD_E_VCLAMP_REF;

	value |= MIPI_CAL_BIAS_PAD_PDVCLAMP;
	tegra_mipi_writel(mipi, value, MIPI_CAL_BIAS_PAD_CFG0);

	return 0;
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
		err = -ENOMEM;
		goto out;
	}

	dev->pdev = of_find_device_by_node(args.np);
	if (!dev->pdev) {
		err = -ENODEV;
		goto free;
	}

	dev->mipi = platform_get_drvdata(dev->pdev);
	if (!dev->mipi) {
		err = -EPROBE_DEFER;
		goto put;
	}

	of_node_put(args.np);

	dev->pads = args.args[0];
	dev->device = device;

	mutex_lock(&dev->mipi->lock);

	if (dev->mipi->usage_count++ == 0) {
		err = tegra_mipi_power_up(dev->mipi);
		if (err < 0) {
			dev_err(dev->mipi->dev,
				"failed to power up MIPI bricks: %d\n",
				err);
			return ERR_PTR(err);
		}
	}

	mutex_unlock(&dev->mipi->lock);

	return dev;

put:
	platform_device_put(dev->pdev);
free:
	kfree(dev);
out:
	of_node_put(args.np);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(tegra_mipi_request);

void tegra_mipi_free(struct tegra_mipi_device *device)
{
	int err;

	mutex_lock(&device->mipi->lock);

	if (--device->mipi->usage_count == 0) {
		err = tegra_mipi_power_down(device->mipi);
		if (err < 0) {
			/*
			 * Not much that can be done here, so an error message
			 * will have to do.
			 */
			dev_err(device->mipi->dev,
				"failed to power down MIPI bricks: %d\n",
				err);
		}
	}

	mutex_unlock(&device->mipi->lock);

	platform_device_put(device->pdev);
	kfree(device);
}
EXPORT_SYMBOL(tegra_mipi_free);

static int tegra_mipi_wait(struct tegra_mipi *mipi)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(250);
	u32 value;

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
	const struct tegra_mipi_soc *soc = device->mipi->soc;
	unsigned int i;
	u32 value;
	int err;

	err = clk_enable(device->mipi->clk);
	if (err < 0)
		return err;

	mutex_lock(&device->mipi->lock);

	value = MIPI_CAL_BIAS_PAD_DRV_DN_REF(soc->pad_drive_down_ref) |
		MIPI_CAL_BIAS_PAD_DRV_UP_REF(soc->pad_drive_up_ref);
	tegra_mipi_writel(device->mipi, value, MIPI_CAL_BIAS_PAD_CFG1);

	value = tegra_mipi_readl(device->mipi, MIPI_CAL_BIAS_PAD_CFG2);
	value &= ~MIPI_CAL_BIAS_PAD_VCLAMP(0x7);
	value &= ~MIPI_CAL_BIAS_PAD_VAUXP(0x7);
	value |= MIPI_CAL_BIAS_PAD_VCLAMP(soc->pad_vclamp_level);
	value |= MIPI_CAL_BIAS_PAD_VAUXP(soc->pad_vauxp_level);
	tegra_mipi_writel(device->mipi, value, MIPI_CAL_BIAS_PAD_CFG2);

	for (i = 0; i < soc->num_pads; i++) {
		u32 clk = 0, data = 0;

		if (device->pads & BIT(i)) {
			data = MIPI_CAL_CONFIG_SELECT |
			       MIPI_CAL_CONFIG_HSPDOS(soc->hspdos) |
			       MIPI_CAL_CONFIG_HSPUOS(soc->hspuos) |
			       MIPI_CAL_CONFIG_TERMOS(soc->termos);
			clk = MIPI_CAL_CONFIG_SELECT |
			      MIPI_CAL_CONFIG_HSCLKPDOSD(soc->hsclkpdos) |
			      MIPI_CAL_CONFIG_HSCLKPUOSD(soc->hsclkpuos);
		}

		tegra_mipi_writel(device->mipi, data, soc->pads[i].data);

		if (soc->has_clk_lane && soc->pads[i].clk != 0)
			tegra_mipi_writel(device->mipi, clk, soc->pads[i].clk);
	}

	value = tegra_mipi_readl(device->mipi, MIPI_CAL_CTRL);
	value &= ~MIPI_CAL_CTRL_NOISE_FILTER(0xf);
	value &= ~MIPI_CAL_CTRL_PRESCALE(0x3);
	value |= MIPI_CAL_CTRL_NOISE_FILTER(0xa);
	value |= MIPI_CAL_CTRL_PRESCALE(0x2);

	if (!soc->clock_enable_override)
		value &= ~MIPI_CAL_CTRL_CLKEN_OVR;
	else
		value |= MIPI_CAL_CTRL_CLKEN_OVR;

	tegra_mipi_writel(device->mipi, value, MIPI_CAL_CTRL);

	/* clear any pending status bits */
	value = tegra_mipi_readl(device->mipi, MIPI_CAL_STATUS);
	tegra_mipi_writel(device->mipi, value, MIPI_CAL_STATUS);

	value = tegra_mipi_readl(device->mipi, MIPI_CAL_CTRL);
	value |= MIPI_CAL_CTRL_START;
	tegra_mipi_writel(device->mipi, value, MIPI_CAL_CTRL);

	err = tegra_mipi_wait(device->mipi);

	mutex_unlock(&device->mipi->lock);
	clk_disable(device->mipi->clk);

	return err;
}
EXPORT_SYMBOL(tegra_mipi_calibrate);

static const struct tegra_mipi_pad tegra114_mipi_pads[] = {
	{ .data = MIPI_CAL_CONFIG_CSIA },
	{ .data = MIPI_CAL_CONFIG_CSIB },
	{ .data = MIPI_CAL_CONFIG_CSIC },
	{ .data = MIPI_CAL_CONFIG_CSID },
	{ .data = MIPI_CAL_CONFIG_CSIE },
	{ .data = MIPI_CAL_CONFIG_DSIA },
	{ .data = MIPI_CAL_CONFIG_DSIB },
	{ .data = MIPI_CAL_CONFIG_DSIC },
	{ .data = MIPI_CAL_CONFIG_DSID },
};

static const struct tegra_mipi_soc tegra114_mipi_soc = {
	.has_clk_lane = false,
	.pads = tegra114_mipi_pads,
	.num_pads = ARRAY_SIZE(tegra114_mipi_pads),
	.clock_enable_override = true,
	.needs_vclamp_ref = true,
	.pad_drive_down_ref = 0x2,
	.pad_drive_up_ref = 0x0,
	.pad_vclamp_level = 0x0,
	.pad_vauxp_level = 0x0,
	.hspdos = 0x0,
	.hspuos = 0x4,
	.termos = 0x5,
	.hsclkpdos = 0x0,
	.hsclkpuos = 0x4,
};

static const struct tegra_mipi_pad tegra124_mipi_pads[] = {
	{ .data = MIPI_CAL_CONFIG_CSIA, .clk = MIPI_CAL_CONFIG_CSIAB_CLK },
	{ .data = MIPI_CAL_CONFIG_CSIB, .clk = MIPI_CAL_CONFIG_CSIAB_CLK },
	{ .data = MIPI_CAL_CONFIG_CSIC, .clk = MIPI_CAL_CONFIG_CSICD_CLK },
	{ .data = MIPI_CAL_CONFIG_CSID, .clk = MIPI_CAL_CONFIG_CSICD_CLK },
	{ .data = MIPI_CAL_CONFIG_CSIE, .clk = MIPI_CAL_CONFIG_CSIE_CLK  },
	{ .data = MIPI_CAL_CONFIG_DSIA, .clk = MIPI_CAL_CONFIG_DSIA_CLK  },
	{ .data = MIPI_CAL_CONFIG_DSIB, .clk = MIPI_CAL_CONFIG_DSIB_CLK  },
};

static const struct tegra_mipi_soc tegra124_mipi_soc = {
	.has_clk_lane = true,
	.pads = tegra124_mipi_pads,
	.num_pads = ARRAY_SIZE(tegra124_mipi_pads),
	.clock_enable_override = true,
	.needs_vclamp_ref = true,
	.pad_drive_down_ref = 0x2,
	.pad_drive_up_ref = 0x0,
	.pad_vclamp_level = 0x0,
	.pad_vauxp_level = 0x0,
	.hspdos = 0x0,
	.hspuos = 0x0,
	.termos = 0x0,
	.hsclkpdos = 0x1,
	.hsclkpuos = 0x2,
};

static const struct tegra_mipi_soc tegra132_mipi_soc = {
	.has_clk_lane = true,
	.pads = tegra124_mipi_pads,
	.num_pads = ARRAY_SIZE(tegra124_mipi_pads),
	.clock_enable_override = false,
	.needs_vclamp_ref = false,
	.pad_drive_down_ref = 0x0,
	.pad_drive_up_ref = 0x3,
	.pad_vclamp_level = 0x0,
	.pad_vauxp_level = 0x0,
	.hspdos = 0x0,
	.hspuos = 0x0,
	.termos = 0x0,
	.hsclkpdos = 0x3,
	.hsclkpuos = 0x2,
};

static const struct tegra_mipi_pad tegra210_mipi_pads[] = {
	{ .data = MIPI_CAL_CONFIG_CSIA, .clk = 0 },
	{ .data = MIPI_CAL_CONFIG_CSIB, .clk = 0 },
	{ .data = MIPI_CAL_CONFIG_CSIC, .clk = 0 },
	{ .data = MIPI_CAL_CONFIG_CSID, .clk = 0 },
	{ .data = MIPI_CAL_CONFIG_CSIE, .clk = 0 },
	{ .data = MIPI_CAL_CONFIG_CSIF, .clk = 0 },
	{ .data = MIPI_CAL_CONFIG_DSIA, .clk = MIPI_CAL_CONFIG_DSIA_CLK },
	{ .data = MIPI_CAL_CONFIG_DSIB, .clk = MIPI_CAL_CONFIG_DSIB_CLK },
	{ .data = MIPI_CAL_CONFIG_DSIC, .clk = MIPI_CAL_CONFIG_DSIC_CLK },
	{ .data = MIPI_CAL_CONFIG_DSID, .clk = MIPI_CAL_CONFIG_DSID_CLK },
};

static const struct tegra_mipi_soc tegra210_mipi_soc = {
	.has_clk_lane = true,
	.pads = tegra210_mipi_pads,
	.num_pads = ARRAY_SIZE(tegra210_mipi_pads),
	.clock_enable_override = true,
	.needs_vclamp_ref = false,
	.pad_drive_down_ref = 0x0,
	.pad_drive_up_ref = 0x3,
	.pad_vclamp_level = 0x1,
	.pad_vauxp_level = 0x1,
	.hspdos = 0x0,
	.hspuos = 0x2,
	.termos = 0x0,
	.hsclkpdos = 0x0,
	.hsclkpuos = 0x2,
};

static const struct of_device_id tegra_mipi_of_match[] = {
	{ .compatible = "nvidia,tegra114-mipi", .data = &tegra114_mipi_soc },
	{ .compatible = "nvidia,tegra124-mipi", .data = &tegra124_mipi_soc },
	{ .compatible = "nvidia,tegra132-mipi", .data = &tegra132_mipi_soc },
	{ .compatible = "nvidia,tegra210-mipi", .data = &tegra210_mipi_soc },
	{ },
};

static int tegra_mipi_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct tegra_mipi *mipi;
	struct resource *res;
	int err;

	match = of_match_node(tegra_mipi_of_match, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	mipi = devm_kzalloc(&pdev->dev, sizeof(*mipi), GFP_KERNEL);
	if (!mipi)
		return -ENOMEM;

	mipi->soc = match->data;
	mipi->dev = &pdev->dev;

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

struct platform_driver tegra_mipi_driver = {
	.driver = {
		.name = "tegra-mipi",
		.of_match_table = tegra_mipi_of_match,
	},
	.probe = tegra_mipi_probe,
	.remove = tegra_mipi_remove,
};
