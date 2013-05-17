/*
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * EXYNOS - SATA controller platform driver wrapper
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
#include <linux/of_irq.h>
#include <linux/of_address.h>

#include "ahci.h"
#include "sata_phy.h"

#define MHZ		(1000 * 1000)
#define DEFERED		1
#define NO_PORT		0

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
	int irq;
	unsigned int freq;
	struct sata_phy *phy[];
};

static int exynos_sata_parse_dt(struct device_node *np,
					struct exynos_sata *sata)
{
	if (!np)
		return -EINVAL;

	return of_property_read_u32(np, "samsung,sata-freq",
						&sata->freq);
}

static int exynos_sata_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ata_port_info pi = ahci_port_info;
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct ahci_host_priv *hpriv;
	struct exynos_sata *sata;
	struct ata_host *host;
	struct device_node *of_node_phy = NULL;
	static int flag = 0, port_init = NO_PORT;
	int n_ports, i, ret;

	sata = devm_kzalloc(dev, sizeof(*sata), GFP_KERNEL);
	if (!sata) {
		dev_err(dev, "can't alloc sata\n");
		return -ENOMEM;
	}

	hpriv = devm_kzalloc(dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv) {
		dev_err(dev, "can't alloc ahci_host_priv\n");
		return -ENOMEM;
	}

	hpriv->flags |= (unsigned long)pi.private_data;

	sata->irq = irq_of_parse_and_map(dev->of_node, 0);
	if (sata->irq <= 0) {
		dev_err(dev, "irq not specified\n");
		return -EINVAL;
	}

	hpriv->mmio = of_iomap(dev->of_node, 0);
	if (!hpriv->mmio) {
		dev_err(dev, "failed to map IO\n");
		return -ENOMEM;
	}

	ret = exynos_sata_parse_dt(dev->of_node, sata);
	if (ret < 0) {
		dev_err(dev, "failed to get frequency for sata ctrl\n");
		goto err_iomap;
	}

	sata->sclk = devm_clk_get(dev, "sclk_sata");
	if (IS_ERR(sata->sclk)) {
		dev_err(dev, "failed to get sclk_sata\n");
		ret = PTR_ERR(sata->sclk);
		goto err_iomap;
	}

	ret = clk_prepare_enable(sata->sclk);
	if (ret < 0) {
		dev_err(dev, "failed to enable source clk\n");
		goto err_iomap;
	}

	ret = clk_set_rate(sata->sclk, sata->freq * MHZ);
	if (ret < 0) {
		dev_err(dev, "failed to set clk frequency\n");
		goto err_clkstrt;
	}

	sata->clk = devm_clk_get(dev, "sata");
	if (IS_ERR(sata->clk)) {
		dev_err(dev, "failed to get sata clock\n");
		ret = PTR_ERR(sata->clk);
		goto err_clkstrt;
	}

	ret = clk_prepare_enable(sata->clk);
	if (ret < 0) {
		dev_err(dev, "failed to enable source clk\n");
		goto err_clkstrt;
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
		goto err_clken;
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
		of_node_phy = of_parse_phandle(dev->of_node,
						"samsung,exynos-sata-phy", i);
		if (!of_node_phy) {
			dev_err(dev,
				"phandle of phy not found for port %d\n", i);
			break;
		}

		sata->phy[i] = sata_get_phy(of_node_phy);
		if (IS_ERR(sata->phy[i])) {
			if (PTR_ERR(sata->phy[i]) == -EBUSY)
				continue;
			dev_err(dev,
				"failed to get sata phy for port %d\n", i);
			if (flag != DEFERED) {
				flag = DEFERED ;
				return -EPROBE_DEFER;
			} else
				continue;

		}
		/* Initialize the PHY */
		ret = sata_init_phy(sata->phy[i]);
		if (ret < 0) {
			if (ret == -EPROBE_DEFER) {
				if (flag != DEFERED) {
					flag = DEFERED ;
					sata_put_phy(sata->phy[i]);
					return -EPROBE_DEFER;
				} else {
					continue;
				}
			} else {
				dev_err(dev,
				"failed to initialize sata phy for port %d\n",
				i);
				sata_put_phy(sata->phy[i]);
			}

		}

		/* set enclosure management message type */
		if (ap->flags & ATA_FLAG_EM)
			ap->em_message_type = hpriv->em_msg_type;

		/* disabled/not-implemented port */
		if (!(hpriv->port_map & (1 << i)))
			ap->ops = &ata_dummy_port_ops;

		port_init++;
	}

	if (port_init == NO_PORT)
		goto err_initphy;

	ret = ahci_reset_controller(host);
	if (ret)
		goto err_rst;

	ahci_init_controller(host);
	ahci_print_info(host, "platform");

	ret = ata_host_activate(host, sata->irq, ahci_interrupt, IRQF_SHARED,
				&ahci_platform_sht);
	if (ret)
		goto err_rst;

	platform_set_drvdata(pdev, sata);

	return 0;

 err_rst:
	for (i = 0; i < host->n_ports; i++)
		sata_shutdown_phy(sata->phy[i]);

 err_initphy:
	for (i = 0; i < host->n_ports; i++)
		sata_put_phy(sata->phy[i]);

 err_clken:
	clk_disable_unprepare(sata->clk);

 err_clkstrt:
	clk_disable_unprepare(sata->sclk);

 err_iomap:
	iounmap(hpriv->mmio);

	return ret;
}

static int exynos_sata_remove(struct platform_device *pdev)
{
	unsigned int i;
	struct device *dev = &pdev->dev;
	struct ata_host *host = dev_get_drvdata(dev);
	struct exynos_sata *sata = platform_get_drvdata(pdev);
	struct ahci_host_priv *hpriv =
			(struct ahci_host_priv *)host->private_data;

	ata_host_detach(host);

	for (i = 0; i < host->n_ports; i++) {
		sata_shutdown_phy(sata->phy[i]);
		sata_put_phy(sata->phy[i]);
	}
	iounmap(hpriv->mmio);

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
		.name = "exynos-sata",
		.owner = THIS_MODULE,
		.of_match_table = ahci_of_match,
	},
};

module_platform_driver(exynos_sata_driver);

MODULE_DESCRIPTION("EXYNOS SATA DRIVER");
MODULE_AUTHOR("Vasanth Ananthan, <vasanth.a@samsung.com>");
MODULE_LICENSE("GPL");
