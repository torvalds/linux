/*
 * DaVinci DA850 AHCI SATA platform driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include <linux/ahci_platform.h>
#include "ahci.h"

#define DRV_NAME "ahci_da850"

/* SATA PHY Control Register offset from AHCI base */
#define SATA_P0PHYCR_REG	0x178

#define SATA_PHY_MPY(x)		((x) << 0)
#define SATA_PHY_LOS(x)		((x) << 6)
#define SATA_PHY_RXCDR(x)	((x) << 10)
#define SATA_PHY_RXEQ(x)	((x) << 13)
#define SATA_PHY_TXSWING(x)	((x) << 19)
#define SATA_PHY_ENPLL(x)	((x) << 31)

/*
 * The multiplier needed for 1.5GHz PLL output.
 *
 * NOTE: This is currently hardcoded to be suitable for 100MHz crystal
 * frequency (which is used by DA850 EVM board) and may need to be changed
 * if you would like to use this driver on some other board.
 */
#define DA850_SATA_CLK_MULTIPLIER	7

static void da850_sata_init(struct device *dev, void __iomem *pwrdn_reg,
			    void __iomem *ahci_base)
{
	unsigned int val;

	/* Enable SATA clock receiver */
	val = readl(pwrdn_reg);
	val &= ~BIT(0);
	writel(val, pwrdn_reg);

	val = SATA_PHY_MPY(DA850_SATA_CLK_MULTIPLIER + 1) | SATA_PHY_LOS(1) |
	      SATA_PHY_RXCDR(4) | SATA_PHY_RXEQ(1) | SATA_PHY_TXSWING(3) |
	      SATA_PHY_ENPLL(1);

	writel(val, ahci_base + SATA_P0PHYCR_REG);
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

static struct ata_port_operations ahci_da850_port_ops = {
	.inherits = &ahci_platform_ops,
	.softreset = ahci_da850_softreset,
	/*
	 * No need to override .pmp_softreset - it's only used for actual
	 * PMP-enabled ports.
	 */
};

static const struct ata_port_info ahci_da850_port_info = {
	.flags		= AHCI_FLAG_COMMON,
	.pio_mask	= ATA_PIO4,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &ahci_da850_port_ops,
};

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT(DRV_NAME),
};

static int ahci_da850_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	struct resource *res;
	void __iomem *pwrdn_reg;
	int rc;

	hpriv = ahci_platform_get_resources(pdev);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		goto disable_resources;

	pwrdn_reg = devm_ioremap(dev, res->start, resource_size(res));
	if (!pwrdn_reg)
		goto disable_resources;

	da850_sata_init(dev, pwrdn_reg, hpriv->mmio);

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

static struct platform_driver ahci_da850_driver = {
	.probe = ahci_da850_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = DRV_NAME,
		.pm = &ahci_da850_pm_ops,
	},
};
module_platform_driver(ahci_da850_driver);

MODULE_DESCRIPTION("DaVinci DA850 AHCI SATA platform driver");
MODULE_AUTHOR("Bartlomiej Zolnierkiewicz <b.zolnierkie@samsung.com>");
MODULE_LICENSE("GPL");
