// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DaVinci DA850 AHCI SATA platform driver
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include <linux/ahci_platform.h>
#include "ahci.h"

#define DRV_NAME		"ahci_da850"
#define HARDRESET_RETRIES	5

/* SATA PHY Control Register offset from AHCI base */
#define SATA_P0PHYCR_REG	0x178

#define SATA_PHY_MPY(x)		((x) << 0)
#define SATA_PHY_LOS(x)		((x) << 6)
#define SATA_PHY_RXCDR(x)	((x) << 10)
#define SATA_PHY_RXEQ(x)	((x) << 13)
#define SATA_PHY_TXSWING(x)	((x) << 19)
#define SATA_PHY_ENPLL(x)	((x) << 31)

static void da850_sata_init(struct device *dev, void __iomem *pwrdn_reg,
			    void __iomem *ahci_base, u32 mpy)
{
	unsigned int val;

	/* Enable SATA clock receiver */
	val = readl(pwrdn_reg);
	val &= ~BIT(0);
	writel(val, pwrdn_reg);

	val = SATA_PHY_MPY(mpy) | SATA_PHY_LOS(1) | SATA_PHY_RXCDR(4) |
	      SATA_PHY_RXEQ(1) | SATA_PHY_TXSWING(3) | SATA_PHY_ENPLL(1);

	writel(val, ahci_base + SATA_P0PHYCR_REG);
}

static u32 ahci_da850_calculate_mpy(unsigned long refclk_rate)
{
	u32 pll_output = 1500000000, needed;

	/*
	 * We need to determine the value of the multiplier (MPY) bits.
	 * In order to include the 12.5 multiplier we need to first divide
	 * the refclk rate by ten.
	 *
	 * __div64_32() turned out to be unreliable, sometimes returning
	 * false results.
	 */
	WARN((refclk_rate % 10) != 0, "refclk must be divisible by 10");
	needed = pll_output / (refclk_rate / 10);

	/*
	 * What we have now is (multiplier * 10).
	 *
	 * Let's determine the actual register value we need to write.
	 */

	switch (needed) {
	case 50:
		return 0x1;
	case 60:
		return 0x2;
	case 80:
		return 0x4;
	case 100:
		return 0x5;
	case 120:
		return 0x6;
	case 125:
		return 0x7;
	case 150:
		return 0x8;
	case 200:
		return 0x9;
	case 250:
		return 0xa;
	default:
		/*
		 * We should have divided evenly - if not, return an invalid
		 * value.
		 */
		return 0;
	}
}

static int ahci_da850_softreset(struct ata_link *link,
				unsigned int *class, unsigned long deadline)
{
	int pmp, ret;

	pmp = sata_srst_pmp(link);

	/*
	 * There's an issue with the SATA controller on da850 SoCs: if we
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

static int ahci_da850_hardreset(struct ata_link *link,
				unsigned int *class, unsigned long deadline)
{
	int ret, retry = HARDRESET_RETRIES;
	bool online;

	/*
	 * In order to correctly service the LCD controller of the da850 SoC,
	 * we increased the PLL0 frequency to 456MHz from the default 300MHz.
	 *
	 * This made the SATA controller unstable and the hardreset operation
	 * does not always succeed the first time. Before really giving up to
	 * bring up the link, retry the reset a couple times.
	 */
	do {
		ret = ahci_do_hardreset(link, class, deadline, &online);
		if (online)
			return ret;
	} while (retry--);

	return ret;
}

static struct ata_port_operations ahci_da850_port_ops = {
	.inherits = &ahci_platform_ops,
	.reset.softreset = ahci_da850_softreset,
	/*
	 * No need to override .pmp_softreset - it's only used for actual
	 * PMP-enabled ports.
	 */
	.reset.hardreset = ahci_da850_hardreset,
	.pmp_reset.hardreset = ahci_da850_hardreset,
};

static const struct ata_port_info ahci_da850_port_info = {
	.flags		= AHCI_FLAG_COMMON,
	.pio_mask	= ATA_PIO4,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &ahci_da850_port_ops,
};

static const struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT(DRV_NAME),
};

static int ahci_da850_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	void __iomem *pwrdn_reg;
	struct resource *res;
	u32 mpy;
	int rc;

	hpriv = ahci_platform_get_resources(pdev, 0);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	/*
	 * Internally ahci_platform_get_resources() calls the bulk clocks
	 * get method or falls back to using a single clk_get_optional().
	 * This AHCI SATA controller uses two clocks: functional clock
	 * with "fck" connection id and external reference clock with
	 * "refclk" id. If we haven't got all of them re-try the clocks
	 * getting procedure with the explicitly specified ids.
	 */
	if (hpriv->n_clks < 2) {
		hpriv->clks = devm_kcalloc(dev, 2, sizeof(*hpriv->clks), GFP_KERNEL);
		if (!hpriv->clks)
			return -ENOMEM;

		hpriv->clks[0].id = "fck";
		hpriv->clks[1].id = "refclk";
		hpriv->n_clks = 2;

		rc = devm_clk_bulk_get(dev, hpriv->n_clks, hpriv->clks);
		if (rc)
			return rc;
	}

	mpy = ahci_da850_calculate_mpy(clk_get_rate(hpriv->clks[1].clk));
	if (mpy == 0) {
		dev_err(dev, "invalid REFCLK multiplier value: 0x%x", mpy);
		return -EINVAL;
	}

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		rc = -ENODEV;
		goto disable_resources;
	}

	pwrdn_reg = devm_ioremap(dev, res->start, resource_size(res));
	if (!pwrdn_reg) {
		rc = -ENOMEM;
		goto disable_resources;
	}

	da850_sata_init(dev, pwrdn_reg, hpriv->mmio, mpy);

	rc = ahci_platform_init_host(pdev, hpriv, &ahci_da850_port_info,
				     &ahci_platform_sht);
	if (rc)
		goto disable_resources;

	return 0;
disable_resources:
	ahci_platform_disable_resources(hpriv);
	return rc;
}

static SIMPLE_DEV_PM_OPS(ahci_da850_pm_ops, ahci_platform_suspend,
			 ahci_platform_resume);

static const struct of_device_id ahci_da850_of_match[] = {
	{ .compatible = "ti,da850-ahci", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ahci_da850_of_match);

static struct platform_driver ahci_da850_driver = {
	.probe = ahci_da850_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ahci_da850_of_match,
		.pm = &ahci_da850_pm_ops,
	},
};
module_platform_driver(ahci_da850_driver);

MODULE_DESCRIPTION("DaVinci DA850 AHCI SATA platform driver");
MODULE_AUTHOR("Bartlomiej Zolnierkiewicz <b.zolnierkie@samsung.com>");
MODULE_LICENSE("GPL");
