/*
 * AHCI SATA platform library
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
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include <linux/ahci_platform.h>
#include <linux/phy/phy.h>
#include <linux/pm_runtime.h>
#include "ahci.h"

static void ahci_host_stop(struct ata_host *host);

struct ata_port_operations ahci_platform_ops = {
	.inherits	= &ahci_ops,
	.host_stop	= ahci_host_stop,
};
EXPORT_SYMBOL_GPL(ahci_platform_ops);

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT("ahci_platform"),
};

/**
 * ahci_platform_enable_clks - Enable platform clocks
 * @hpriv: host private area to store config values
 *
 * This function enables all the clks found in hpriv->clks, starting at
 * index 0. If any clk fails to enable it disables all the clks already
 * enabled in reverse order, and then returns an error.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_enable_clks(struct ahci_host_priv *hpriv)
{
	int c, rc;

	for (c = 0; c < AHCI_MAX_CLKS && hpriv->clks[c]; c++) {
		rc = clk_prepare_enable(hpriv->clks[c]);
		if (rc)
			goto disable_unprepare_clk;
	}
	return 0;

disable_unprepare_clk:
	while (--c >= 0)
		clk_disable_unprepare(hpriv->clks[c]);
	return rc;
}
EXPORT_SYMBOL_GPL(ahci_platform_enable_clks);

/**
 * ahci_platform_disable_clks - Disable platform clocks
 * @hpriv: host private area to store config values
 *
 * This function disables all the clks found in hpriv->clks, in reverse
 * order of ahci_platform_enable_clks (starting at the end of the array).
 */
void ahci_platform_disable_clks(struct ahci_host_priv *hpriv)
{
	int c;

	for (c = AHCI_MAX_CLKS - 1; c >= 0; c--)
		if (hpriv->clks[c])
			clk_disable_unprepare(hpriv->clks[c]);
}
EXPORT_SYMBOL_GPL(ahci_platform_disable_clks);

/**
 * ahci_platform_enable_resources - Enable platform resources
 * @hpriv: host private area to store config values
 *
 * This function enables all ahci_platform managed resources in the
 * following order:
 * 1) Regulator
 * 2) Clocks (through ahci_platform_enable_clks)
 * 3) Phy
 *
 * If resource enabling fails at any point the previous enabled resources
 * are disabled in reverse order.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_enable_resources(struct ahci_host_priv *hpriv)
{
	int rc;

	if (hpriv->target_pwr) {
		rc = regulator_enable(hpriv->target_pwr);
		if (rc)
			return rc;
	}

	rc = ahci_platform_enable_clks(hpriv);
	if (rc)
		goto disable_regulator;

	if (hpriv->phy) {
		rc = phy_init(hpriv->phy);
		if (rc)
			goto disable_clks;

		rc = phy_power_on(hpriv->phy);
		if (rc) {
			phy_exit(hpriv->phy);
			goto disable_clks;
		}
	}

	return 0;

disable_clks:
	ahci_platform_disable_clks(hpriv);

disable_regulator:
	if (hpriv->target_pwr)
		regulator_disable(hpriv->target_pwr);
	return rc;
}
EXPORT_SYMBOL_GPL(ahci_platform_enable_resources);

/**
 * ahci_platform_disable_resources - Disable platform resources
 * @hpriv: host private area to store config values
 *
 * This function disables all ahci_platform managed resources in the
 * following order:
 * 1) Phy
 * 2) Clocks (through ahci_platform_disable_clks)
 * 3) Regulator
 */
void ahci_platform_disable_resources(struct ahci_host_priv *hpriv)
{
	if (hpriv->phy) {
		phy_power_off(hpriv->phy);
		phy_exit(hpriv->phy);
	}

	ahci_platform_disable_clks(hpriv);

	if (hpriv->target_pwr)
		regulator_disable(hpriv->target_pwr);
}
EXPORT_SYMBOL_GPL(ahci_platform_disable_resources);

static void ahci_platform_put_resources(struct device *dev, void *res)
{
	struct ahci_host_priv *hpriv = res;
	int c;

	if (hpriv->got_runtime_pm) {
		pm_runtime_put_sync(dev);
		pm_runtime_disable(dev);
	}

	for (c = 0; c < AHCI_MAX_CLKS && hpriv->clks[c]; c++)
		clk_put(hpriv->clks[c]);
}

/**
 * ahci_platform_get_resources - Get platform resources
 * @pdev: platform device to get resources for
 *
 * This function allocates an ahci_host_priv struct, and gets the following
 * resources, storing a reference to them inside the returned struct:
 *
 * 1) mmio registers (IORESOURCE_MEM 0, mandatory)
 * 2) regulator for controlling the targets power (optional)
 * 3) 0 - AHCI_MAX_CLKS clocks, as specified in the devs devicetree node,
 *    or for non devicetree enabled platforms a single clock
 *	4) phy (optional)
 *
 * RETURNS:
 * The allocated ahci_host_priv on success, otherwise an ERR_PTR value
 */
struct ahci_host_priv *ahci_platform_get_resources(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	struct clk *clk;
	int i, rc = -ENOMEM;

	if (!devres_open_group(dev, NULL, GFP_KERNEL))
		return ERR_PTR(-ENOMEM);

	hpriv = devres_alloc(ahci_platform_put_resources, sizeof(*hpriv),
			     GFP_KERNEL);
	if (!hpriv)
		goto err_out;

	devres_add(dev, hpriv);

	hpriv->mmio = devm_ioremap_resource(dev,
			      platform_get_resource(pdev, IORESOURCE_MEM, 0));
	if (IS_ERR(hpriv->mmio)) {
		dev_err(dev, "no mmio space\n");
		rc = PTR_ERR(hpriv->mmio);
		goto err_out;
	}

	hpriv->target_pwr = devm_regulator_get_optional(dev, "target");
	if (IS_ERR(hpriv->target_pwr)) {
		rc = PTR_ERR(hpriv->target_pwr);
		if (rc == -EPROBE_DEFER)
			goto err_out;
		hpriv->target_pwr = NULL;
	}

	for (i = 0; i < AHCI_MAX_CLKS; i++) {
		/*
		 * For now we must use clk_get(dev, NULL) for the first clock,
		 * because some platforms (da850, spear13xx) are not yet
		 * converted to use devicetree for clocks.  For new platforms
		 * this is equivalent to of_clk_get(dev->of_node, 0).
		 */
		if (i == 0)
			clk = clk_get(dev, NULL);
		else
			clk = of_clk_get(dev->of_node, i);

		if (IS_ERR(clk)) {
			rc = PTR_ERR(clk);
			if (rc == -EPROBE_DEFER)
				goto err_out;
			break;
		}
		hpriv->clks[i] = clk;
	}

	hpriv->phy = devm_phy_get(dev, "sata-phy");
	if (IS_ERR(hpriv->phy)) {
		rc = PTR_ERR(hpriv->phy);
		switch (rc) {
		case -ENODEV:
		case -ENOSYS:
			/* continue normally */
			hpriv->phy = NULL;
			break;

		case -EPROBE_DEFER:
			goto err_out;

		default:
			dev_err(dev, "couldn't get sata-phy\n");
			goto err_out;
		}
	}

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);
	hpriv->got_runtime_pm = true;

	devres_remove_group(dev, NULL);
	return hpriv;

err_out:
	devres_release_group(dev, NULL);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(ahci_platform_get_resources);

/**
 * ahci_platform_init_host - Bring up an ahci-platform host
 * @pdev: platform device pointer for the host
 * @hpriv: ahci-host private data for the host
 * @pi_template: template for the ata_port_info to use
 * @force_port_map: param passed to ahci_save_initial_config
 * @mask_port_map: param passed to ahci_save_initial_config
 *
 * This function does all the usual steps needed to bring up an
 * ahci-platform host, note any necessary resources (ie clks, phy, etc.)
 * must be initialized / enabled before calling this.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_init_host(struct platform_device *pdev,
			    struct ahci_host_priv *hpriv,
			    const struct ata_port_info *pi_template,
			    unsigned int force_port_map,
			    unsigned int mask_port_map)
{
	struct device *dev = &pdev->dev;
	struct ata_port_info pi = *pi_template;
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct ata_host *host;
	int i, irq, n_ports, rc;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(dev, "no irq\n");
		return -EINVAL;
	}

	/* prepare host */
	hpriv->flags |= (unsigned long)pi.private_data;

	ahci_save_initial_config(dev, hpriv, force_port_map, mask_port_map);

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
	if (!host)
		return -ENOMEM;

	host->private_data = hpriv;

	if (!(hpriv->cap & HOST_CAP_SSS) || ahci_ignore_sss)
		host->flags |= ATA_HOST_PARALLEL_SCAN;
	else
		dev_info(dev, "SSS flag set, parallel bus scan disabled\n");

	if (pi.flags & ATA_FLAG_EM)
		ahci_reset_em(host);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		ata_port_desc(ap, "mmio %pR",
			      platform_get_resource(pdev, IORESOURCE_MEM, 0));
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
		return rc;

	ahci_init_controller(host);
	ahci_print_info(host, "platform");

	return ata_host_activate(host, irq, ahci_interrupt, IRQF_SHARED,
				 &ahci_platform_sht);
}
EXPORT_SYMBOL_GPL(ahci_platform_init_host);

static void ahci_host_stop(struct ata_host *host)
{
	struct device *dev = host->dev;
	struct ahci_platform_data *pdata = dev_get_platdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;

	if (pdata && pdata->exit)
		pdata->exit(dev);

	ahci_platform_disable_resources(hpriv);
}

#ifdef CONFIG_PM_SLEEP
/**
 * ahci_platform_suspend_host - Suspend an ahci-platform host
 * @dev: device pointer for the host
 *
 * This function does all the usual steps needed to suspend an
 * ahci-platform host, note any necessary resources (ie clks, phy, etc.)
 * must be disabled after calling this.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_suspend_host(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->mmio;
	u32 ctl;

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

	return ata_host_suspend(host, PMSG_SUSPEND);
}
EXPORT_SYMBOL_GPL(ahci_platform_suspend_host);

/**
 * ahci_platform_resume_host - Resume an ahci-platform host
 * @dev: device pointer for the host
 *
 * This function does all the usual steps needed to resume an ahci-platform
 * host, note any necessary resources (ie clks, phy, etc.)  must be
 * initialized / enabled before calling this.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_resume_host(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	int rc;

	if (dev->power.power_state.event == PM_EVENT_SUSPEND) {
		rc = ahci_reset_controller(host);
		if (rc)
			return rc;

		ahci_init_controller(host);
	}

	ata_host_resume(host);

	return 0;
}
EXPORT_SYMBOL_GPL(ahci_platform_resume_host);

/**
 * ahci_platform_suspend - Suspend an ahci-platform device
 * @dev: the platform device to suspend
 *
 * This function suspends the host associated with the device, followed by
 * disabling all the resources of the device.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_suspend(struct device *dev)
{
	struct ahci_platform_data *pdata = dev_get_platdata(dev);
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int rc;

	rc = ahci_platform_suspend_host(dev);
	if (rc)
		return rc;

	if (pdata && pdata->suspend) {
		rc = pdata->suspend(dev);
		if (rc)
			goto resume_host;
	}

	ahci_platform_disable_resources(hpriv);

	return 0;

resume_host:
	ahci_platform_resume_host(dev);
	return rc;
}
EXPORT_SYMBOL_GPL(ahci_platform_suspend);

/**
 * ahci_platform_resume - Resume an ahci-platform device
 * @dev: the platform device to resume
 *
 * This function enables all the resources of the device followed by
 * resuming the host associated with the device.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_resume(struct device *dev)
{
	struct ahci_platform_data *pdata = dev_get_platdata(dev);
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int rc;

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	if (pdata && pdata->resume) {
		rc = pdata->resume(dev);
		if (rc)
			goto disable_resources;
	}

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
EXPORT_SYMBOL_GPL(ahci_platform_resume);
#endif

MODULE_DESCRIPTION("AHCI SATA platform library");
MODULE_AUTHOR("Anton Vorontsov <avorontsov@ru.mvista.com>");
MODULE_LICENSE("GPL");
