/*
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * EXYNOS - SATA controller driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include <linux/ahci_platform.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "ahci.h"
#include "sata_phy.h"

#define MHZ            (1000 * 1000)

static const struct ata_port_info ahci_port_info = {
	.flags = AHCI_FLAG_COMMON,
	.pio_mask = ATA_PIO4,
	.udma_mask = ATA_UDMA6,
	.port_ops = &ahci_ops,
};

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT("ahci_platform"),
};

struct exynos_sata {
	struct clk *sclk;
	struct clk *clk;
	struct sata_phy *phy;
	int irq;
	unsigned int freq;
};

static void exynos_sata_parse_dt(struct device_node *np,
					struct exynos_sata *sata)
{
	if (!np)
		return;

	of_property_read_u32(np, "samsung,sata-freq", &sata->freq);
}

static int exynos_sata_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ata_port_info pi = ahci_port_info;
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct ahci_host_priv *hpriv;
	struct exynos_sata *sata;
	struct ata_host *host;
	struct resource *mem;
	int n_ports, i, ret;

	sata = devm_kzalloc(dev, sizeof(*sata), GFP_KERNEL);
	if (!sata) {
		dev_err(dev, "can't alloc sata\n");
		return -EINVAL;
	}

	hpriv = devm_kzalloc(dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv) {
		dev_err(dev, "can't alloc ahci_host_priv\n");
		ret = -ENOMEM;
		goto err1;
	}

	hpriv->flags |= (unsigned long)pi.private_data;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(dev, "no mmio space\n");
		ret = -EINVAL;
		goto err2;
	}

	sata->irq = platform_get_irq(pdev, 0);
	if (sata->irq <= 0) {
		dev_err(dev, "no irq\n");
		ret = -EINVAL;
		goto err2;
	}

	hpriv->mmio = devm_ioremap(dev, mem->start, resource_size(mem));
	if (!hpriv->mmio) {
		dev_err(dev, "can't map %pR\n", mem);
		ret = -ENOMEM;
		goto err2;
	}

	exynos_sata_parse_dt(dev->of_node, sata);

	sata->sclk = devm_clk_get(dev, "sclk_sata");
	if (IS_ERR(sata->sclk)) {
		dev_err(dev, "failed to get sclk_sata\n");
		ret = PTR_ERR(sata->sclk);
		goto err3;
	}
	clk_enable(sata->sclk);

	clk_set_rate(sata->sclk, sata->freq * MHZ);

	sata->clk = devm_clk_get(dev, "sata");
	if (IS_ERR(sata->clk)) {
		dev_err(dev, "failed to get sata clock\n");
		ret = PTR_ERR(sata->clk);
		goto err4;
	}
	clk_enable(sata->clk);

	/*  Get a gen 3 PHY controller */

	sata->phy = sata_get_phy(SATA_PHY_GENERATION3);
	if (!sata->phy) {
		dev_err(dev, "failed to get sata phy\n");
		ret = -EPROBE_DEFER;
		goto err5;
	}

	/* Initialize the controller */

	ret = sata_init_phy(sata->phy);
	if (ret < 0) {
		dev_err(dev, "failed to initialize sata phy\n");
		goto err6;
	}

	ahci_save_initial_config(dev, hpriv, 0, 0);

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
		ret = -ENOMEM;
		goto err7;
	}

	host->private_data = hpriv;

	if (!(hpriv->cap & HOST_CAP_SSS) || ahci_ignore_sss)
		host->flags |= ATA_HOST_PARALLEL_SCAN;
	else
		pr_info(KERN_INFO
		       "ahci: SSS flag set, parallel bus scan disabled\n");

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

	ret = ahci_reset_controller(host);
	if (ret)
		goto err7;

	ahci_init_controller(host);
	ahci_print_info(host, "platform");

	ret = ata_host_activate(host, sata->irq, ahci_interrupt, IRQF_SHARED,
				&ahci_platform_sht);
	if (ret)
		goto err7;

	platform_set_drvdata(pdev, sata);

	return 0;

 err7:
	sata_shutdown_phy(sata->phy);

 err6:
	sata_put_phy(sata->phy);

 err5:
	clk_disable(sata->clk);
	devm_clk_put(dev, sata->clk);

 err4:
	clk_disable(sata->sclk);
	devm_clk_put(dev, sata->sclk);

 err3:
	devm_iounmap(dev, hpriv->mmio);

 err2:
	devm_kfree(dev, hpriv);

 err1:
	devm_kfree(dev, sata);

	return ret;
}

static int exynos_sata_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ata_host *host = dev_get_drvdata(dev);
	struct exynos_sata *sata = platform_get_drvdata(pdev);

	ata_host_detach(host);

	sata_shutdown_phy(sata->phy);
	sata_put_phy(sata->phy);

	return 0;
}

static const struct of_device_id ahci_of_match[] = {
	{ .compatible = "samsung,exynos5-sata-ahci", },
};

MODULE_DEVICE_TABLE(of, ahci_of_match);

static struct platform_driver exynos_sata_driver = {
	.probe	= exynos_sata_probe,
	.remove = exynos_sata_remove,
	.driver = {
		.name = "ahci-sata",
		.owner = THIS_MODULE,
		.of_match_table = ahci_of_match,
	},
};

module_platform_driver(exynos_sata_driver);

MODULE_DESCRIPTION("EXYNOS SATA DRIVER");
MODULE_AUTHOR("Vasanth Ananthan, <vasanth.a@samsung.com>");
MODULE_LICENSE("GPL");
