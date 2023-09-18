// SPDX-License-Identifier: GPL-2.0-only
/*
 * Technologic Systems TS-73xx SBC FPGA loader
 *
 * Copyright (C) 2016 Florian Fainelli <f.fainelli@gmail.com>
 *
 * FPGA Manager Driver for the on-board Altera Cyclone II FPGA found on
 * TS-7300, heavily based on load_fpga.c in their vendor tree.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/iopoll.h>
#include <linux/fpga/fpga-mgr.h>

#define TS73XX_FPGA_DATA_REG		0
#define TS73XX_FPGA_CONFIG_REG		1

#define TS73XX_FPGA_WRITE_DONE		0x1
#define TS73XX_FPGA_WRITE_DONE_TIMEOUT	1000	/* us */
#define TS73XX_FPGA_RESET		0x2
#define TS73XX_FPGA_RESET_LOW_DELAY	30	/* us */
#define TS73XX_FPGA_RESET_HIGH_DELAY	80	/* us */
#define TS73XX_FPGA_LOAD_OK		0x4
#define TS73XX_FPGA_CONFIG_LOAD		0x8

struct ts73xx_fpga_priv {
	void __iomem	*io_base;
	struct device	*dev;
};

static int ts73xx_fpga_write_init(struct fpga_manager *mgr,
				  struct fpga_image_info *info,
				  const char *buf, size_t count)
{
	struct ts73xx_fpga_priv *priv = mgr->priv;

	/* Reset the FPGA */
	writeb(0, priv->io_base + TS73XX_FPGA_CONFIG_REG);
	udelay(TS73XX_FPGA_RESET_LOW_DELAY);
	writeb(TS73XX_FPGA_RESET, priv->io_base + TS73XX_FPGA_CONFIG_REG);
	udelay(TS73XX_FPGA_RESET_HIGH_DELAY);

	return 0;
}

static int ts73xx_fpga_write(struct fpga_manager *mgr, const char *buf,
			     size_t count)
{
	struct ts73xx_fpga_priv *priv = mgr->priv;
	size_t i = 0;
	int ret;
	u8 reg;

	while (count--) {
		ret = readb_poll_timeout(priv->io_base + TS73XX_FPGA_CONFIG_REG,
					 reg, !(reg & TS73XX_FPGA_WRITE_DONE),
					 1, TS73XX_FPGA_WRITE_DONE_TIMEOUT);
		if (ret < 0)
			return ret;

		writeb(buf[i], priv->io_base + TS73XX_FPGA_DATA_REG);
		i++;
	}

	return 0;
}

static int ts73xx_fpga_write_complete(struct fpga_manager *mgr,
				      struct fpga_image_info *info)
{
	struct ts73xx_fpga_priv *priv = mgr->priv;
	u8 reg;

	usleep_range(1000, 2000);
	reg = readb(priv->io_base + TS73XX_FPGA_CONFIG_REG);
	reg |= TS73XX_FPGA_CONFIG_LOAD;
	writeb(reg, priv->io_base + TS73XX_FPGA_CONFIG_REG);

	usleep_range(1000, 2000);
	reg = readb(priv->io_base + TS73XX_FPGA_CONFIG_REG);
	reg &= ~TS73XX_FPGA_CONFIG_LOAD;
	writeb(reg, priv->io_base + TS73XX_FPGA_CONFIG_REG);

	reg = readb(priv->io_base + TS73XX_FPGA_CONFIG_REG);
	if ((reg & TS73XX_FPGA_LOAD_OK) != TS73XX_FPGA_LOAD_OK)
		return -ETIMEDOUT;

	return 0;
}

static const struct fpga_manager_ops ts73xx_fpga_ops = {
	.write_init	= ts73xx_fpga_write_init,
	.write		= ts73xx_fpga_write,
	.write_complete	= ts73xx_fpga_write_complete,
};

static int ts73xx_fpga_probe(struct platform_device *pdev)
{
	struct device *kdev = &pdev->dev;
	struct ts73xx_fpga_priv *priv;
	struct fpga_manager *mgr;

	priv = devm_kzalloc(kdev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = kdev;

	priv->io_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->io_base))
		return PTR_ERR(priv->io_base);

	mgr = devm_fpga_mgr_register(kdev, "TS-73xx FPGA Manager",
				     &ts73xx_fpga_ops, priv);
	return PTR_ERR_OR_ZERO(mgr);
}

static struct platform_driver ts73xx_fpga_driver = {
	.driver	= {
		.name	= "ts73xx-fpga-mgr",
	},
	.probe	= ts73xx_fpga_probe,
};
module_platform_driver(ts73xx_fpga_driver);

MODULE_AUTHOR("Florian Fainelli <f.fainelli@gmail.com>");
MODULE_DESCRIPTION("TS-73xx FPGA Manager driver");
MODULE_LICENSE("GPL v2");
