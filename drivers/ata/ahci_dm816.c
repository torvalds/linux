// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DaVinci DM816 AHCI SATA platform driver
 *
 * Copyright (C) 2017 BayLibre SAS
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include <linux/ahci_platform.h>

#include "ahci.h"

#define AHCI_DM816_DRV_NAME		"ahci-dm816"

#define AHCI_DM816_PHY_ENPLL(x)		((x) << 0)
#define AHCI_DM816_PHY_MPY(x)		((x) << 1)
#define AHCI_DM816_PHY_LOS(x)		((x) << 12)
#define AHCI_DM816_PHY_RXCDR(x)		((x) << 13)
#define AHCI_DM816_PHY_RXEQ(x)		((x) << 16)
#define AHCI_DM816_PHY_TXSWING(x)	((x) << 23)

#define AHCI_DM816_P0PHYCR_REG		0x178
#define AHCI_DM816_P1PHYCR_REG		0x1f8

#define AHCI_DM816_PLL_OUT		1500000000LU

static const unsigned long pll_mpy_table[] = {
	  400,  500,  600,  800,  825, 1000, 1200,
	 1250, 1500, 1600, 1650, 2000, 2200, 2500
};

static int ahci_dm816_get_mpy_bits(unsigned long refclk_rate)
{
	unsigned long pll_multiplier;
	int i;

	/*
	 * We need to determine the value of the multiplier (MPY) bits.
	 * In order to include the 8.25 multiplier we need to first divide
	 * the refclk rate by 100.
	 */
	pll_multiplier = AHCI_DM816_PLL_OUT / (refclk_rate / 100);

	for (i = 0; i < ARRAY_SIZE(pll_mpy_table); i++) {
		if (pll_mpy_table[i] == pll_multiplier)
			return i;
	}

	/*
	 * We should have divided evenly - if not, return an invalid
	 * value.
	 */
	return -1;
}

static int ahci_dm816_phy_init(struct ahci_host_priv *hpriv, struct device *dev)
{
	unsigned long refclk_rate;
	int mpy;
	u32 val;

	/*
	 * We should have been supplied two clocks: the functional and
	 * keep-alive clock and the external reference clock. We need the
	 * rate of the latter to calculate the correct value of MPY bits.
	 */
	if (hpriv->n_clks < 2) {
		dev_err(dev, "reference clock not supplied\n");
		return -EINVAL;
	}

	refclk_rate = clk_get_rate(hpriv->clks[1].clk);
	if ((refclk_rate % 100) != 0) {
		dev_err(dev, "reference clock rate must be divisible by 100\n");
		return -EINVAL;
	}

	mpy = ahci_dm816_get_mpy_bits(refclk_rate);
	if (mpy < 0) {
		dev_err(dev, "can't calculate the MPY bits value\n");
		return -EINVAL;
	}

	/* Enable the PHY and configure the first HBA port. */
	val = AHCI_DM816_PHY_MPY(mpy) | AHCI_DM816_PHY_LOS(1) |
	      AHCI_DM816_PHY_RXCDR(4) | AHCI_DM816_PHY_RXEQ(1) |
	      AHCI_DM816_PHY_TXSWING(3) | AHCI_DM816_PHY_ENPLL(1);
	writel(val, hpriv->mmio + AHCI_DM816_P0PHYCR_REG);

	/* Configure the second HBA port. */
	val = AHCI_DM816_PHY_LOS(1) | AHCI_DM816_PHY_RXCDR(4) |
	      AHCI_DM816_PHY_RXEQ(1) | AHCI_DM816_PHY_TXSWING(3);
	writel(val, hpriv->mmio + AHCI_DM816_P1PHYCR_REG);

	return 0;
}

static int ahci_dm816_softreset(struct ata_link *link,
				unsigned int *class, unsigned long deadline)
{
	int pmp, ret;

	pmp = sata_srst_pmp(link);

	/*
	 * There's an issue with the SATA controller on DM816 SoC: if we
	 * enable Port Multiplier support, but the drive is connected directly
	 * to the board, it can't be detected. As a workaround: if PMP is
	 * enabled, we first call ahci_do_softreset() and pass it the result of
	 * sata_srst_pmp(). If this call fails, we retry with pmp = 0.
	 */
	ret = ahci_do_softreset(link, class, pmp, deadline, ahci_check_ready);
	if (pmp && ret == -EBUSY)
		return ahci_do_softreset(link, class, 0,
					 deadline, ahci_check_ready);

	return ret;
}

static struct ata_port_operations ahci_dm816_port_ops = {
	.inherits = &ahci_platform_ops,
	.reset.softreset = ahci_dm816_softreset,
};

static const struct ata_port_info ahci_dm816_port_info = {
	.flags		= AHCI_FLAG_COMMON,
	.pio_mask	= ATA_PIO4,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &ahci_dm816_port_ops,
};

static const struct scsi_host_template ahci_dm816_platform_sht = {
	AHCI_SHT(AHCI_DM816_DRV_NAME),
};

static int ahci_dm816_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	int rc;

	hpriv = ahci_platform_get_resources(pdev, 0);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	rc = ahci_dm816_phy_init(hpriv, dev);
	if (rc)
		goto disable_resources;

	rc = ahci_platform_init_host(pdev, hpriv,
				     &ahci_dm816_port_info,
				     &ahci_dm816_platform_sht);
	if (rc)
		goto disable_resources;

	return 0;

disable_resources:
	ahci_platform_disable_resources(hpriv);

	return rc;
}

static SIMPLE_DEV_PM_OPS(ahci_dm816_pm_ops,
			 ahci_platform_suspend,
			 ahci_platform_resume);

static const struct of_device_id ahci_dm816_of_match[] = {
	{ .compatible = "ti,dm816-ahci", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ahci_dm816_of_match);

static struct platform_driver ahci_dm816_driver = {
	.probe = ahci_dm816_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = AHCI_DM816_DRV_NAME,
		.of_match_table = ahci_dm816_of_match,
		.pm = &ahci_dm816_pm_ops,
	},
};
module_platform_driver(ahci_dm816_driver);

MODULE_DESCRIPTION("DaVinci DM816 AHCI SATA platform driver");
MODULE_AUTHOR("Bartosz Golaszewski <bgolaszewski@baylibre.com>");
MODULE_LICENSE("GPL");
