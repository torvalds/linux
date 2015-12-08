/*
 * AHCI glue platform driver for Marvell EBU SOCs
 *
 * Copyright (C) 2014 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 * Marcin Wojtas <mw@semihalf.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/ahci_platform.h>
#include <linux/kernel.h>
#include <linux/mbus.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "ahci.h"

#define DRV_NAME "ahci-mvebu"

#define AHCI_VENDOR_SPECIFIC_0_ADDR  0xa0
#define AHCI_VENDOR_SPECIFIC_0_DATA  0xa4

#define AHCI_WINDOW_CTRL(win)	(0x60 + ((win) << 4))
#define AHCI_WINDOW_BASE(win)	(0x64 + ((win) << 4))
#define AHCI_WINDOW_SIZE(win)	(0x68 + ((win) << 4))

static void ahci_mvebu_mbus_config(struct ahci_host_priv *hpriv,
				   const struct mbus_dram_target_info *dram)
{
	int i;

	for (i = 0; i < 4; i++) {
		writel(0, hpriv->mmio + AHCI_WINDOW_CTRL(i));
		writel(0, hpriv->mmio + AHCI_WINDOW_BASE(i));
		writel(0, hpriv->mmio + AHCI_WINDOW_SIZE(i));
	}

	for (i = 0; i < dram->num_cs; i++) {
		const struct mbus_dram_window *cs = dram->cs + i;

		writel((cs->mbus_attr << 8) |
		       (dram->mbus_dram_target_id << 4) | 1,
		       hpriv->mmio + AHCI_WINDOW_CTRL(i));
		writel(cs->base >> 16, hpriv->mmio + AHCI_WINDOW_BASE(i));
		writel(((cs->size - 1) & 0xffff0000),
		       hpriv->mmio + AHCI_WINDOW_SIZE(i));
	}
}

static void ahci_mvebu_regret_option(struct ahci_host_priv *hpriv)
{
	/*
	 * Enable the regret bit to allow the SATA unit to regret a
	 * request that didn't receive an acknowlegde and avoid a
	 * deadlock
	 */
	writel(0x4, hpriv->mmio + AHCI_VENDOR_SPECIFIC_0_ADDR);
	writel(0x80, hpriv->mmio + AHCI_VENDOR_SPECIFIC_0_DATA);
}

#ifdef CONFIG_PM_SLEEP
static int ahci_mvebu_suspend(struct platform_device *pdev, pm_message_t state)
{
	return ahci_platform_suspend_host(&pdev->dev);
}

static int ahci_mvebu_resume(struct platform_device *pdev)
{
	struct ata_host *host = platform_get_drvdata(pdev);
	struct ahci_host_priv *hpriv = host->private_data;
	const struct mbus_dram_target_info *dram;

	dram = mv_mbus_dram_info();
	if (dram)
		ahci_mvebu_mbus_config(hpriv, dram);

	ahci_mvebu_regret_option(hpriv);

	return ahci_platform_resume_host(&pdev->dev);
}
#else
#define ahci_mvebu_suspend NULL
#define ahci_mvebu_resume NULL
#endif

static const struct ata_port_info ahci_mvebu_port_info = {
	.flags	   = AHCI_FLAG_COMMON,
	.pio_mask  = ATA_PIO4,
	.udma_mask = ATA_UDMA6,
	.port_ops  = &ahci_platform_ops,
};

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT(DRV_NAME),
};

static int ahci_mvebu_probe(struct platform_device *pdev)
{
	struct ahci_host_priv *hpriv;
	const struct mbus_dram_target_info *dram;
	int rc;

	hpriv = ahci_platform_get_resources(pdev);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	dram = mv_mbus_dram_info();
	if (!dram)
		return -ENODEV;

	ahci_mvebu_mbus_config(hpriv, dram);
	ahci_mvebu_regret_option(hpriv);

	rc = ahci_platform_init_host(pdev, hpriv, &ahci_mvebu_port_info,
				     &ahci_platform_sht);
	if (rc)
		goto disable_resources;

	return 0;

disable_resources:
	ahci_platform_disable_resources(hpriv);
	return rc;
}

static const struct of_device_id ahci_mvebu_of_match[] = {
	{ .compatible = "marvell,armada-380-ahci", },
	{ },
};
MODULE_DEVICE_TABLE(of, ahci_mvebu_of_match);

/*
 * We currently don't provide power management related operations,
 * since there is no suspend/resume support at the platform level for
 * Armada 38x for the moment.
 */
static struct platform_driver ahci_mvebu_driver = {
	.probe = ahci_mvebu_probe,
	.remove = ata_platform_remove_one,
	.suspend = ahci_mvebu_suspend,
	.resume = ahci_mvebu_resume,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ahci_mvebu_of_match,
	},
};
module_platform_driver(ahci_mvebu_driver);

MODULE_DESCRIPTION("Marvell EBU AHCI SATA driver");
MODULE_AUTHOR("Thomas Petazzoni <thomas.petazzoni@free-electrons.com>, Marcin Wojtas <mw@semihalf.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ahci_mvebu");
