/*
 * Freescale QorIQ AHCI SATA platform driver
 *
 * Copyright 2015 Freescale, Inc.
 *   Tang Yuantian <Yuantian.Tang@freescale.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/ahci_platform.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include "ahci.h"

#define DRV_NAME "ahci-qoriq"

/* port register definition */
#define PORT_PHY1	0xA8
#define PORT_PHY2	0xAC
#define PORT_PHY3	0xB0
#define PORT_PHY4	0xB4
#define PORT_PHY5	0xB8
#define PORT_TRANS	0xC8

/* port register default value */
#define AHCI_PORT_PHY_1_CFG	0xa003fffe
#define AHCI_PORT_TRANS_CFG	0x08000029

/* for ls1021a */
#define LS1021A_PORT_PHY2	0x28183414
#define LS1021A_PORT_PHY3	0x0e080e06
#define LS1021A_PORT_PHY4	0x064a080b
#define LS1021A_PORT_PHY5	0x2aa86470

#define SATA_ECC_DISABLE	0x00020000

/* for ls1043a */
#define LS1043A_PORT_PHY2	0x28184d1f
#define LS1043A_PORT_PHY3	0x0e081509

enum ahci_qoriq_type {
	AHCI_LS1021A,
	AHCI_LS1043A,
	AHCI_LS2080A,
};

struct ahci_qoriq_priv {
	struct ccsr_ahci *reg_base;
	enum ahci_qoriq_type type;
	void __iomem *ecc_addr;
};

static const struct of_device_id ahci_qoriq_of_match[] = {
	{ .compatible = "fsl,ls1021a-ahci", .data = (void *)AHCI_LS1021A},
	{ .compatible = "fsl,ls1043a-ahci", .data = (void *)AHCI_LS1043A},
	{ .compatible = "fsl,ls2080a-ahci", .data = (void *)AHCI_LS2080A},
	{},
};
MODULE_DEVICE_TABLE(of, ahci_qoriq_of_match);

static int ahci_qoriq_hardreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline)
{
	const unsigned long *timing = sata_ehc_deb_timing(&link->eh_context);
	void __iomem *port_mmio = ahci_port_base(link->ap);
	u32 px_cmd, px_is, px_val;
	struct ata_port *ap = link->ap;
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_qoriq_priv *qoriq_priv = hpriv->plat_data;
	u8 *d2h_fis = pp->rx_fis + RX_FIS_D2H_REG;
	struct ata_taskfile tf;
	bool online;
	int rc;
	bool ls1021a_workaround = (qoriq_priv->type == AHCI_LS1021A);

	DPRINTK("ENTER\n");

	ahci_stop_engine(ap);

	/*
	 * There is a errata on ls1021a Rev1.0 and Rev2.0 which is:
	 * A-009042: The device detection initialization sequence
	 * mistakenly resets some registers.
	 *
	 * Workaround for this is:
	 * The software should read and store PxCMD and PxIS values
	 * before issuing the device detection initialization sequence.
	 * After the sequence is complete, software should restore the
	 * PxCMD and PxIS with the stored values.
	 */
	if (ls1021a_workaround) {
		px_cmd = readl(port_mmio + PORT_CMD);
		px_is = readl(port_mmio + PORT_IRQ_STAT);
	}

	/* clear D2H reception area to properly wait for D2H FIS */
	ata_tf_init(link->device, &tf);
	tf.command = ATA_BUSY;
	ata_tf_to_fis(&tf, 0, 0, d2h_fis);

	rc = sata_link_hardreset(link, timing, deadline, &online,
				 ahci_check_ready);

	/* restore the PxCMD and PxIS on ls1021 */
	if (ls1021a_workaround) {
		px_val = readl(port_mmio + PORT_CMD);
		if (px_val != px_cmd)
			writel(px_cmd, port_mmio + PORT_CMD);

		px_val = readl(port_mmio + PORT_IRQ_STAT);
		if (px_val != px_is)
			writel(px_is, port_mmio + PORT_IRQ_STAT);
	}

	hpriv->start_engine(ap);

	if (online)
		*class = ahci_dev_classify(ap);

	DPRINTK("EXIT, rc=%d, class=%u\n", rc, *class);
	return rc;
}

static struct ata_port_operations ahci_qoriq_ops = {
	.inherits	= &ahci_ops,
	.hardreset	= ahci_qoriq_hardreset,
};

static struct ata_port_info ahci_qoriq_port_info = {
	.flags		= AHCI_FLAG_COMMON | ATA_FLAG_NCQ,
	.pio_mask	= ATA_PIO4,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &ahci_qoriq_ops,
};

static struct scsi_host_template ahci_qoriq_sht = {
	AHCI_SHT(DRV_NAME),
};

static int ahci_qoriq_phy_init(struct ahci_host_priv *hpriv)
{
	struct ahci_qoriq_priv *qpriv = hpriv->plat_data;
	void __iomem *reg_base = hpriv->mmio;

	switch (qpriv->type) {
	case AHCI_LS1021A:
		writel(SATA_ECC_DISABLE, qpriv->ecc_addr);
		writel(AHCI_PORT_PHY_1_CFG, reg_base + PORT_PHY1);
		writel(LS1021A_PORT_PHY2, reg_base + PORT_PHY2);
		writel(LS1021A_PORT_PHY3, reg_base + PORT_PHY3);
		writel(LS1021A_PORT_PHY4, reg_base + PORT_PHY4);
		writel(LS1021A_PORT_PHY5, reg_base + PORT_PHY5);
		writel(AHCI_PORT_TRANS_CFG, reg_base + PORT_TRANS);
		break;

	case AHCI_LS1043A:
		writel(AHCI_PORT_PHY_1_CFG, reg_base + PORT_PHY1);
		writel(LS1043A_PORT_PHY2, reg_base + PORT_PHY2);
		writel(LS1043A_PORT_PHY3, reg_base + PORT_PHY3);
		writel(AHCI_PORT_TRANS_CFG, reg_base + PORT_TRANS);
		break;

	case AHCI_LS2080A:
		writel(AHCI_PORT_PHY_1_CFG, reg_base + PORT_PHY1);
		writel(AHCI_PORT_TRANS_CFG, reg_base + PORT_TRANS);
		break;
	}

	return 0;
}

static int ahci_qoriq_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	struct ahci_qoriq_priv *qoriq_priv;
	const struct of_device_id *of_id;
	struct resource *res;
	int rc;

	hpriv = ahci_platform_get_resources(pdev);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	of_id = of_match_node(ahci_qoriq_of_match, np);
	if (!of_id)
		return -ENODEV;

	qoriq_priv = devm_kzalloc(dev, sizeof(*qoriq_priv), GFP_KERNEL);
	if (!qoriq_priv)
		return -ENOMEM;

	qoriq_priv->type = (enum ahci_qoriq_type)of_id->data;

	if (qoriq_priv->type == AHCI_LS1021A) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
				"sata-ecc");
		qoriq_priv->ecc_addr = devm_ioremap_resource(dev, res);
		if (IS_ERR(qoriq_priv->ecc_addr))
			return PTR_ERR(qoriq_priv->ecc_addr);
	}

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	hpriv->plat_data = qoriq_priv;
	rc = ahci_qoriq_phy_init(hpriv);
	if (rc)
		goto disable_resources;

	/* Workaround for ls2080a */
	if (qoriq_priv->type == AHCI_LS2080A) {
		hpriv->flags |= AHCI_HFLAG_NO_NCQ;
		ahci_qoriq_port_info.flags &= ~ATA_FLAG_NCQ;
	}

	rc = ahci_platform_init_host(pdev, hpriv, &ahci_qoriq_port_info,
				     &ahci_qoriq_sht);
	if (rc)
		goto disable_resources;

	return 0;

disable_resources:
	ahci_platform_disable_resources(hpriv);

	return rc;
}

#ifdef CONFIG_PM_SLEEP
static int ahci_qoriq_resume(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int rc;

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	rc = ahci_qoriq_phy_init(hpriv);
	if (rc)
		goto disable_resources;

	rc = ahci_platform_resume_host(dev);
	if (rc)
		goto disable_resources;

	/* We resumed so update PM runtime state */
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;

disable_resources:
	ahci_platform_disable_resources(hpriv);

	return rc;
}
#endif

static SIMPLE_DEV_PM_OPS(ahci_qoriq_pm_ops, ahci_platform_suspend,
			 ahci_qoriq_resume);

static struct platform_driver ahci_qoriq_driver = {
	.probe = ahci_qoriq_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ahci_qoriq_of_match,
		.pm = &ahci_qoriq_pm_ops,
	},
};
module_platform_driver(ahci_qoriq_driver);

MODULE_DESCRIPTION("Freescale QorIQ AHCI SATA platform driver");
MODULE_AUTHOR("Tang Yuantian <Yuantian.Tang@freescale.com>");
MODULE_LICENSE("GPL");
