/*
 * AHCI SATA platform driver
 *
 * Copyright 2004-2005  Red Hat, Inc.
 *   Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2010  MontaVista Software, LLC.
 *   Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include <linux/ahci_platform.h>
#include "ahci.h"

static void ahci_host_stop(struct ata_host *host);

enum ahci_type {
	AHCI,		/* standard platform ahci */
	IMX53_AHCI,	/* ahci on i.mx53 */
	STRICT_AHCI,	/* delayed DMA engine start */
};

static struct platform_device_id ahci_devtype[] = {
	{
		.name = "ahci",
		.driver_data = AHCI,
	}, {
		.name = "imx53-ahci",
		.driver_data = IMX53_AHCI,
	}, {
		.name = "strict-ahci",
		.driver_data = STRICT_AHCI,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, ahci_devtype);

static struct ata_port_operations ahci_platform_ops = {
	.inherits	= &ahci_ops,
	.host_stop	= ahci_host_stop,
};

static struct ata_port_operations ahci_platform_retry_srst_ops = {
	.inherits	= &ahci_pmp_retry_srst_ops,
	.host_stop	= ahci_host_stop,
};

static const struct ata_port_info ahci_port_info[] = {
	/* by features */
	[AHCI] = {
		.flags		= AHCI_FLAG_COMMON,
		.pio_mask	= ATA_PIO4,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &ahci_platform_ops,
	},
	[IMX53_AHCI] = {
		.flags		= AHCI_FLAG_COMMON,
		.pio_mask	= ATA_PIO4,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &ahci_platform_retry_srst_ops,
	},
	[STRICT_AHCI] = {
		AHCI_HFLAGS	(AHCI_HFLAG_DELAY_ENGINE),
		.flags		= AHCI_FLAG_COMMON,
		.pio_mask	= ATA_PIO4,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &ahci_platform_ops,
	},
};

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT("ahci_platform"),
};

static int ahci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ahci_platform_data *pdata = dev_get_platdata(dev);
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct ata_port_info pi = ahci_port_info[id ? id->driver_data : 0];
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct ahci_host_priv *hpriv;
	struct ata_host *host;
	struct resource *mem;
	int irq;
	int n_ports;
	int i;
	int rc;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(dev, "no mmio space\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(dev, "no irq\n");
		return -EINVAL;
	}

	if (pdata && pdata->ata_port_info)
		pi = *pdata->ata_port_info;

	hpriv = devm_kzalloc(dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv) {
		dev_err(dev, "can't alloc ahci_host_priv\n");
		return -ENOMEM;
	}

	hpriv->flags |= (unsigned long)pi.private_data;

	hpriv->mmio = devm_ioremap(dev, mem->start, resource_size(mem));
	if (!hpriv->mmio) {
		dev_err(dev, "can't map %pR\n", mem);
		return -ENOMEM;
	}

	hpriv->clk = clk_get(dev, NULL);
	if (IS_ERR(hpriv->clk)) {
		dev_err(dev, "can't get clock\n");
	} else {
		rc = clk_prepare_enable(hpriv->clk);
		if (rc) {
			dev_err(dev, "clock prepare enable failed");
			goto free_clk;
		}
	}

	/*
	 * Some platforms might need to prepare for mmio region access,
	 * which could be done in the following init call. So, the mmio
	 * region shouldn't be accessed before init (if provided) has
	 * returned successfully.
	 */
	if (pdata && pdata->init) {
		rc = pdata->init(dev, hpriv->mmio);
		if (rc)
			goto disable_unprepare_clk;
	}

	ahci_save_initial_config(dev, hpriv,
		pdata ? pdata->force_port_map : 0,
		pdata ? pdata->mask_port_map  : 0);

	/* prepare host */
	if (hpriv->cap & HOST_CAP_NCQ)
		pi.flags |= ATA_FLAG_NCQ;

	if (hpriv->cap & HOST_CAP_PMP)
		pi.flags |= ATA_FLAG_PMP;

	ahci_set_em_messages(hpriv, &pi);

	/* CAP.NP sometimes indicate the index of the last enabled
	 * port, at other times, that of the last possible port, so
	 * determining the maximum port number requires looking at
	 * both CAP.NP and port_map.
	 */
	n_ports = max(ahci_nr_ports(hpriv->cap), fls(hpriv->port_map));

	host = ata_host_alloc_pinfo(dev, ppi, n_ports);
	if (!host) {
		rc = -ENOMEM;
		goto pdata_exit;
	}

	host->private_data = hpriv;

	if (!(hpriv->cap & HOST_CAP_SSS) || ahci_ignore_sss)
		host->flags |= ATA_HOST_PARALLEL_SCAN;
	else
		dev_info(dev, "SSS flag set, parallel bus scan disabled\n");

	if (pi.flags & ATA_FLAG_EM)
		ahci_reset_em(host);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		ata_port_desc(ap, "mmio %pR", mem);
		ata_port_desc(ap, "port 0x%x", 0x100 + ap->port_no * 0x80);

		/* set enclosure management message type */
		if (ap->flags & ATA_FLAG_EM)
			ap->em_message_type = hpriv->em_msg_type;

		/* disabled/not-implemented port */
		if (!(hpriv->port_map & (1 << i)))
			ap->ops = &ata_dummy_port_ops;
	}

	rc = ahci_reset_controller(host);
	if (rc)
		goto pdata_exit;

	ahci_init_controller(host);
	ahci_print_info(host, "platform");

	rc = ata_host_activate(host, irq, ahci_interrupt, IRQF_SHARED,
			       &ahci_platform_sht);
	if (rc)
		goto pdata_exit;

	return 0;
pdata_exit:
	if (pdata && pdata->exit)
		pdata->exit(dev);
disable_unprepare_clk:
	if (!IS_ERR(hpriv->clk))
		clk_disable_unprepare(hpriv->clk);
free_clk:
	if (!IS_ERR(hpriv->clk))
		clk_put(hpriv->clk);
	return rc;
}

static void ahci_host_stop(struct ata_host *host)
{
	struct device *dev = host->dev;
	struct ahci_platform_data *pdata = dev_get_platdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;

	if (pdata && pdata->exit)
		pdata->exit(dev);

	if (!IS_ERR(hpriv->clk)) {
		clk_disable_unprepare(hpriv->clk);
		clk_put(hpriv->clk);
	}
}

#ifdef CONFIG_PM_SLEEP
static int ahci_suspend(struct device *dev)
{
	struct ahci_platform_data *pdata = dev_get_platdata(dev);
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->mmio;
	u32 ctl;
	int rc;

	if (hpriv->flags & AHCI_HFLAG_NO_SUSPEND) {
		dev_err(dev, "firmware update required for suspend/resume\n");
		return -EIO;
	}

	/*
	 * AHCI spec rev1.1 section 8.3.3:
	 * Software must disable interrupts prior to requesting a
	 * transition of the HBA to D3 state.
	 */
	ctl = readl(mmio + HOST_CTL);
	ctl &= ~HOST_IRQ_EN;
	writel(ctl, mmio + HOST_CTL);
	readl(mmio + HOST_CTL); /* flush */

	rc = ata_host_suspend(host, PMSG_SUSPEND);
	if (rc)
		return rc;

	if (pdata && pdata->suspend)
		return pdata->suspend(dev);

	if (!IS_ERR(hpriv->clk))
		clk_disable_unprepare(hpriv->clk);

	return 0;
}

static int ahci_resume(struct device *dev)
{
	struct ahci_platform_data *pdata = dev_get_platdata(dev);
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int rc;

	if (!IS_ERR(hpriv->clk)) {
		rc = clk_prepare_enable(hpriv->clk);
		if (rc) {
			dev_err(dev, "clock prepare enable failed");
			return rc;
		}
	}

	if (pdata && pdata->resume) {
		rc = pdata->resume(dev);
		if (rc)
			goto disable_unprepare_clk;
	}

	if (dev->power.power_state.event == PM_EVENT_SUSPEND) {
		rc = ahci_reset_controller(host);
		if (rc)
			goto disable_unprepare_clk;

		ahci_init_controller(host);
	}

	ata_host_resume(host);

	return 0;

disable_unprepare_clk:
	if (!IS_ERR(hpriv->clk))
		clk_disable_unprepare(hpriv->clk);

	return rc;
}
#endif

static SIMPLE_DEV_PM_OPS(ahci_pm_ops, ahci_suspend, ahci_resume);

static const struct of_device_id ahci_of_match[] = {
	{ .compatible = "snps,spear-ahci", },
	{ .compatible = "snps,exynos5440-ahci", },
	{},
};
MODULE_DEVICE_TABLE(of, ahci_of_match);

static struct platform_driver ahci_driver = {
	.probe = ahci_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = "ahci",
		.owner = THIS_MODULE,
		.of_match_table = ahci_of_match,
		.pm = &ahci_pm_ops,
	},
	.id_table	= ahci_devtype,
};
module_platform_driver(ahci_driver);

MODULE_DESCRIPTION("AHCI SATA platform driver");
MODULE_AUTHOR("Anton Vorontsov <avorontsov@ru.mvista.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ahci");
