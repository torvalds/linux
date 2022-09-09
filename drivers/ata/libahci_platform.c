// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AHCI SATA platform library
 *
 * Copyright 2004-2005  Red Hat, Inc.
 *   Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2010  MontaVista Software, LLC.
 *   Anton Vorontsov <avorontsov@ru.mvista.com>
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
#include <linux/of_platform.h>
#include <linux/reset.h>
#include "ahci.h"

static void ahci_host_stop(struct ata_host *host);

struct ata_port_operations ahci_platform_ops = {
	.inherits	= &ahci_ops,
	.host_stop	= ahci_host_stop,
};
EXPORT_SYMBOL_GPL(ahci_platform_ops);

/**
 * ahci_platform_enable_phys - Enable PHYs
 * @hpriv: host private area to store config values
 *
 * This function enables all the PHYs found in hpriv->phys, if any.
 * If a PHY fails to be enabled, it disables all the PHYs already
 * enabled in reverse order and returns an error.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_enable_phys(struct ahci_host_priv *hpriv)
{
	int rc, i;

	for (i = 0; i < hpriv->nports; i++) {
		rc = phy_init(hpriv->phys[i]);
		if (rc)
			goto disable_phys;

		rc = phy_set_mode(hpriv->phys[i], PHY_MODE_SATA);
		if (rc) {
			phy_exit(hpriv->phys[i]);
			goto disable_phys;
		}

		rc = phy_power_on(hpriv->phys[i]);
		if (rc) {
			phy_exit(hpriv->phys[i]);
			goto disable_phys;
		}
	}

	return 0;

disable_phys:
	while (--i >= 0) {
		phy_power_off(hpriv->phys[i]);
		phy_exit(hpriv->phys[i]);
	}
	return rc;
}
EXPORT_SYMBOL_GPL(ahci_platform_enable_phys);

/**
 * ahci_platform_disable_phys - Disable PHYs
 * @hpriv: host private area to store config values
 *
 * This function disables all PHYs found in hpriv->phys.
 */
void ahci_platform_disable_phys(struct ahci_host_priv *hpriv)
{
	int i;

	for (i = 0; i < hpriv->nports; i++) {
		phy_power_off(hpriv->phys[i]);
		phy_exit(hpriv->phys[i]);
	}
}
EXPORT_SYMBOL_GPL(ahci_platform_disable_phys);

/**
 * ahci_platform_find_clk - Find platform clock
 * @hpriv: host private area to store config values
 * @con_id: clock connection ID
 *
 * This function returns a pointer to the clock descriptor of the clock with
 * the passed ID.
 *
 * RETURNS:
 * Pointer to the clock descriptor on success otherwise NULL
 */
struct clk *ahci_platform_find_clk(struct ahci_host_priv *hpriv, const char *con_id)
{
	int i;

	for (i = 0; i < hpriv->n_clks; i++) {
		if (!strcmp(hpriv->clks[i].id, con_id))
			return hpriv->clks[i].clk;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(ahci_platform_find_clk);

/**
 * ahci_platform_enable_clks - Enable platform clocks
 * @hpriv: host private area to store config values
 *
 * This function enables all the clks found for the AHCI device.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_enable_clks(struct ahci_host_priv *hpriv)
{
	return clk_bulk_prepare_enable(hpriv->n_clks, hpriv->clks);
}
EXPORT_SYMBOL_GPL(ahci_platform_enable_clks);

/**
 * ahci_platform_disable_clks - Disable platform clocks
 * @hpriv: host private area to store config values
 *
 * This function disables all the clocks enabled before
 * (bulk-clocks-disable function is supposed to do that in reverse
 * from the enabling procedure order).
 */
void ahci_platform_disable_clks(struct ahci_host_priv *hpriv)
{
	clk_bulk_disable_unprepare(hpriv->n_clks, hpriv->clks);
}
EXPORT_SYMBOL_GPL(ahci_platform_disable_clks);

/**
 * ahci_platform_deassert_rsts - Deassert/trigger platform resets
 * @hpriv: host private area to store config values
 *
 * This function deasserts or triggers all the reset lines found for
 * the AHCI device.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_deassert_rsts(struct ahci_host_priv *hpriv)
{
	if (hpriv->f_rsts & AHCI_PLATFORM_RST_TRIGGER)
		return reset_control_reset(hpriv->rsts);

	return reset_control_deassert(hpriv->rsts);
}
EXPORT_SYMBOL_GPL(ahci_platform_deassert_rsts);

/**
 * ahci_platform_assert_rsts - Assert/rearm platform resets
 * @hpriv: host private area to store config values
 *
 * This function asserts or rearms (for self-deasserting resets) all
 * the reset controls found for the AHCI device.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_assert_rsts(struct ahci_host_priv *hpriv)
{
	if (hpriv->f_rsts & AHCI_PLATFORM_RST_TRIGGER)
		return reset_control_rearm(hpriv->rsts);

	return reset_control_assert(hpriv->rsts);
}
EXPORT_SYMBOL_GPL(ahci_platform_assert_rsts);

/**
 * ahci_platform_enable_regulators - Enable regulators
 * @hpriv: host private area to store config values
 *
 * This function enables all the regulators found in controller and
 * hpriv->target_pwrs, if any.  If a regulator fails to be enabled, it
 * disables all the regulators already enabled in reverse order and
 * returns an error.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_enable_regulators(struct ahci_host_priv *hpriv)
{
	int rc, i;

	rc = regulator_enable(hpriv->ahci_regulator);
	if (rc)
		return rc;

	rc = regulator_enable(hpriv->phy_regulator);
	if (rc)
		goto disable_ahci_pwrs;

	for (i = 0; i < hpriv->nports; i++) {
		if (!hpriv->target_pwrs[i])
			continue;

		rc = regulator_enable(hpriv->target_pwrs[i]);
		if (rc)
			goto disable_target_pwrs;
	}

	return 0;

disable_target_pwrs:
	while (--i >= 0)
		if (hpriv->target_pwrs[i])
			regulator_disable(hpriv->target_pwrs[i]);

	regulator_disable(hpriv->phy_regulator);
disable_ahci_pwrs:
	regulator_disable(hpriv->ahci_regulator);
	return rc;
}
EXPORT_SYMBOL_GPL(ahci_platform_enable_regulators);

/**
 * ahci_platform_disable_regulators - Disable regulators
 * @hpriv: host private area to store config values
 *
 * This function disables all regulators found in hpriv->target_pwrs and
 * AHCI controller.
 */
void ahci_platform_disable_regulators(struct ahci_host_priv *hpriv)
{
	int i;

	for (i = 0; i < hpriv->nports; i++) {
		if (!hpriv->target_pwrs[i])
			continue;
		regulator_disable(hpriv->target_pwrs[i]);
	}

	regulator_disable(hpriv->ahci_regulator);
	regulator_disable(hpriv->phy_regulator);
}
EXPORT_SYMBOL_GPL(ahci_platform_disable_regulators);
/**
 * ahci_platform_enable_resources - Enable platform resources
 * @hpriv: host private area to store config values
 *
 * This function enables all ahci_platform managed resources in the
 * following order:
 * 1) Regulator
 * 2) Clocks (through ahci_platform_enable_clks)
 * 3) Resets
 * 4) Phys
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

	rc = ahci_platform_enable_regulators(hpriv);
	if (rc)
		return rc;

	rc = ahci_platform_enable_clks(hpriv);
	if (rc)
		goto disable_regulator;

	rc = ahci_platform_deassert_rsts(hpriv);
	if (rc)
		goto disable_clks;

	rc = ahci_platform_enable_phys(hpriv);
	if (rc)
		goto disable_rsts;

	return 0;

disable_rsts:
	ahci_platform_assert_rsts(hpriv);

disable_clks:
	ahci_platform_disable_clks(hpriv);

disable_regulator:
	ahci_platform_disable_regulators(hpriv);

	return rc;
}
EXPORT_SYMBOL_GPL(ahci_platform_enable_resources);

/**
 * ahci_platform_disable_resources - Disable platform resources
 * @hpriv: host private area to store config values
 *
 * This function disables all ahci_platform managed resources in the
 * following order:
 * 1) Phys
 * 2) Resets
 * 3) Clocks (through ahci_platform_disable_clks)
 * 4) Regulator
 */
void ahci_platform_disable_resources(struct ahci_host_priv *hpriv)
{
	ahci_platform_disable_phys(hpriv);

	ahci_platform_assert_rsts(hpriv);

	ahci_platform_disable_clks(hpriv);

	ahci_platform_disable_regulators(hpriv);
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

	/*
	 * The regulators are tied to child node device and not to the
	 * SATA device itself. So we can't use devm for automatically
	 * releasing them. We have to do it manually here.
	 */
	for (c = 0; c < hpriv->nports; c++)
		if (hpriv->target_pwrs && hpriv->target_pwrs[c])
			regulator_put(hpriv->target_pwrs[c]);

	kfree(hpriv->target_pwrs);
}

static int ahci_platform_get_phy(struct ahci_host_priv *hpriv, u32 port,
				struct device *dev, struct device_node *node)
{
	int rc;

	hpriv->phys[port] = devm_of_phy_get(dev, node, NULL);

	if (!IS_ERR(hpriv->phys[port]))
		return 0;

	rc = PTR_ERR(hpriv->phys[port]);
	switch (rc) {
	case -ENOSYS:
		/* No PHY support. Check if PHY is required. */
		if (of_find_property(node, "phys", NULL)) {
			dev_err(dev,
				"couldn't get PHY in node %pOFn: ENOSYS\n",
				node);
			break;
		}
		fallthrough;
	case -ENODEV:
		/* continue normally */
		hpriv->phys[port] = NULL;
		rc = 0;
		break;
	case -EPROBE_DEFER:
		/* Do not complain yet */
		break;

	default:
		dev_err(dev,
			"couldn't get PHY in node %pOFn: %d\n",
			node, rc);

		break;
	}

	return rc;
}

static int ahci_platform_get_regulator(struct ahci_host_priv *hpriv, u32 port,
				struct device *dev)
{
	struct regulator *target_pwr;
	int rc = 0;

	target_pwr = regulator_get(dev, "target");

	if (!IS_ERR(target_pwr))
		hpriv->target_pwrs[port] = target_pwr;
	else
		rc = PTR_ERR(target_pwr);

	return rc;
}

static int ahci_platform_get_firmware(struct ahci_host_priv *hpriv,
				      struct device *dev)
{
	struct device_node *child;
	u32 port;

	if (!of_property_read_u32(dev->of_node, "hba-cap", &hpriv->saved_cap))
		hpriv->saved_cap &= (HOST_CAP_SSS | HOST_CAP_MPS);

	of_property_read_u32(dev->of_node,
			     "ports-implemented", &hpriv->saved_port_map);

	for_each_child_of_node(dev->of_node, child) {
		if (!of_device_is_available(child))
			continue;

		if (of_property_read_u32(child, "reg", &port)) {
			of_node_put(child);
			return -EINVAL;
		}

		if (!of_property_read_u32(child, "hba-port-cap", &hpriv->saved_port_cap[port]))
			hpriv->saved_port_cap[port] &= PORT_CMD_CAP;
	}

	return 0;
}

/**
 * ahci_platform_get_resources - Get platform resources
 * @pdev: platform device to get resources for
 * @flags: bitmap representing the resource to get
 *
 * This function allocates an ahci_host_priv struct, and gets the following
 * resources, storing a reference to them inside the returned struct:
 *
 * 1) mmio registers (IORESOURCE_MEM 0, mandatory)
 * 2) regulator for controlling the targets power (optional)
 *    regulator for controlling the AHCI controller (optional)
 * 3) all clocks specified in the devicetree node, or a single
 *    clock for non-OF platforms (optional)
 * 4) resets, if flags has AHCI_PLATFORM_GET_RESETS (optional)
 * 5) phys (optional)
 *
 * RETURNS:
 * The allocated ahci_host_priv on success, otherwise an ERR_PTR value
 */
struct ahci_host_priv *ahci_platform_get_resources(struct platform_device *pdev,
						   unsigned int flags)
{
	int child_nodes, rc = -ENOMEM, enabled_ports = 0;
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	struct device_node *child;
	u32 mask_port_map = 0;

	if (!devres_open_group(dev, NULL, GFP_KERNEL))
		return ERR_PTR(-ENOMEM);

	hpriv = devres_alloc(ahci_platform_put_resources, sizeof(*hpriv),
			     GFP_KERNEL);
	if (!hpriv)
		goto err_out;

	devres_add(dev, hpriv);

	/*
	 * If the DT provided an "ahci" named resource, use it. Otherwise,
	 * fallback to using the default first resource for the device node.
	 */
	if (platform_get_resource_byname(pdev, IORESOURCE_MEM, "ahci"))
		hpriv->mmio = devm_platform_ioremap_resource_byname(pdev, "ahci");
	else
		hpriv->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hpriv->mmio)) {
		rc = PTR_ERR(hpriv->mmio);
		goto err_out;
	}

	/*
	 * Bulk clocks getting procedure can fail to find any clock due to
	 * running on a non-OF platform or due to the clocks being defined in
	 * bypass of the DT firmware (like da850, spear13xx). In that case we
	 * fallback to getting a single clock source right from the dev clocks
	 * list.
	 */
	rc = devm_clk_bulk_get_all(dev, &hpriv->clks);
	if (rc < 0)
		goto err_out;

	if (rc > 0) {
		/* Got clocks in bulk */
		hpriv->n_clks = rc;
	} else {
		/*
		 * No clock bulk found: fallback to manually getting
		 * the optional clock.
		 */
		hpriv->clks = devm_kzalloc(dev, sizeof(*hpriv->clks), GFP_KERNEL);
		if (!hpriv->clks) {
			rc = -ENOMEM;
			goto err_out;
		}
		hpriv->clks->clk = devm_clk_get_optional(dev, NULL);
		if (IS_ERR(hpriv->clks->clk)) {
			rc = PTR_ERR(hpriv->clks->clk);
			goto err_out;
		} else if (hpriv->clks->clk) {
			hpriv->clks->id = "ahci";
			hpriv->n_clks = 1;
		}
	}

	hpriv->ahci_regulator = devm_regulator_get(dev, "ahci");
	if (IS_ERR(hpriv->ahci_regulator)) {
		rc = PTR_ERR(hpriv->ahci_regulator);
		if (rc != 0)
			goto err_out;
	}

	hpriv->phy_regulator = devm_regulator_get(dev, "phy");
	if (IS_ERR(hpriv->phy_regulator)) {
		rc = PTR_ERR(hpriv->phy_regulator);
		goto err_out;
	}

	if (flags & AHCI_PLATFORM_GET_RESETS) {
		hpriv->rsts = devm_reset_control_array_get_optional_shared(dev);
		if (IS_ERR(hpriv->rsts)) {
			rc = PTR_ERR(hpriv->rsts);
			goto err_out;
		}

		hpriv->f_rsts = flags & AHCI_PLATFORM_RST_TRIGGER;
	}

	/*
	 * Too many sub-nodes most likely means having something wrong with
	 * the firmware.
	 */
	child_nodes = of_get_child_count(dev->of_node);
	if (child_nodes > AHCI_MAX_PORTS) {
		rc = -EINVAL;
		goto err_out;
	}

	/*
	 * If no sub-node was found, we still need to set nports to
	 * one in order to be able to use the
	 * ahci_platform_[en|dis]able_[phys|regulators] functions.
	 */
	if (child_nodes)
		hpriv->nports = child_nodes;
	else
		hpriv->nports = 1;

	hpriv->phys = devm_kcalloc(dev, hpriv->nports, sizeof(*hpriv->phys), GFP_KERNEL);
	if (!hpriv->phys) {
		rc = -ENOMEM;
		goto err_out;
	}
	/*
	 * We cannot use devm_ here, since ahci_platform_put_resources() uses
	 * target_pwrs after devm_ have freed memory
	 */
	hpriv->target_pwrs = kcalloc(hpriv->nports, sizeof(*hpriv->target_pwrs), GFP_KERNEL);
	if (!hpriv->target_pwrs) {
		rc = -ENOMEM;
		goto err_out;
	}

	if (child_nodes) {
		for_each_child_of_node(dev->of_node, child) {
			u32 port;
			struct platform_device *port_dev __maybe_unused;

			if (!of_device_is_available(child))
				continue;

			if (of_property_read_u32(child, "reg", &port)) {
				rc = -EINVAL;
				of_node_put(child);
				goto err_out;
			}

			if (port >= hpriv->nports) {
				dev_warn(dev, "invalid port number %d\n", port);
				continue;
			}
			mask_port_map |= BIT(port);

#ifdef CONFIG_OF_ADDRESS
			of_platform_device_create(child, NULL, NULL);

			port_dev = of_find_device_by_node(child);

			if (port_dev) {
				rc = ahci_platform_get_regulator(hpriv, port,
								&port_dev->dev);
				if (rc == -EPROBE_DEFER) {
					of_node_put(child);
					goto err_out;
				}
			}
#endif

			rc = ahci_platform_get_phy(hpriv, port, dev, child);
			if (rc) {
				of_node_put(child);
				goto err_out;
			}

			enabled_ports++;
		}
		if (!enabled_ports) {
			dev_warn(dev, "No port enabled\n");
			rc = -ENODEV;
			goto err_out;
		}

		if (!hpriv->mask_port_map)
			hpriv->mask_port_map = mask_port_map;
	} else {
		/*
		 * If no sub-node was found, keep this for device tree
		 * compatibility
		 */
		rc = ahci_platform_get_phy(hpriv, 0, dev, dev->of_node);
		if (rc)
			goto err_out;

		rc = ahci_platform_get_regulator(hpriv, 0, dev);
		if (rc == -EPROBE_DEFER)
			goto err_out;
	}

	/*
	 * Retrieve firmware-specific flags which then will be used to set
	 * the HW-init fields of HBA and its ports
	 */
	rc = ahci_platform_get_firmware(hpriv, dev);
	if (rc)
		goto err_out;

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
 * @sht: scsi_host_template to use when registering
 *
 * This function does all the usual steps needed to bring up an
 * ahci-platform host, note any necessary resources (ie clks, phys, etc.)
 * must be initialized / enabled before calling this.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_init_host(struct platform_device *pdev,
			    struct ahci_host_priv *hpriv,
			    const struct ata_port_info *pi_template,
			    struct scsi_host_template *sht)
{
	struct device *dev = &pdev->dev;
	struct ata_port_info pi = *pi_template;
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct ata_host *host;
	int i, irq, n_ports, rc;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	if (!irq)
		return -EINVAL;

	hpriv->irq = irq;

	/* prepare host */
	pi.private_data = (void *)(unsigned long)hpriv->flags;

	ahci_save_initial_config(dev, hpriv);

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

	if (hpriv->cap & HOST_CAP_64) {
		rc = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(64));
		if (rc) {
			dev_err(dev, "Failed to enable 64-bit DMA.\n");
			return rc;
		}
	}

	rc = ahci_reset_controller(host);
	if (rc)
		return rc;

	ahci_init_controller(host);
	ahci_print_info(host, "platform");

	return ahci_host_activate(host, sht);
}
EXPORT_SYMBOL_GPL(ahci_platform_init_host);

static void ahci_host_stop(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;

	ahci_platform_disable_resources(hpriv);
}

/**
 * ahci_platform_shutdown - Disable interrupts and stop DMA for host ports
 * @pdev: platform device pointer for the host
 *
 * This function is called during system shutdown and performs the minimal
 * deconfiguration required to ensure that an ahci_platform host cannot
 * corrupt or otherwise interfere with a new kernel being started with kexec.
 */
void ahci_platform_shutdown(struct platform_device *pdev)
{
	struct ata_host *host = platform_get_drvdata(pdev);
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->mmio;
	int i;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		/* Disable port interrupts */
		if (ap->ops->freeze)
			ap->ops->freeze(ap);

		/* Stop the port DMA engines */
		if (ap->ops->port_stop)
			ap->ops->port_stop(ap);
	}

	/* Disable and clear host interrupts */
	writel(readl(mmio + HOST_CTL) & ~HOST_IRQ_EN, mmio + HOST_CTL);
	readl(mmio + HOST_CTL); /* flush */
	writel(GENMASK(host->n_ports, 0), mmio + HOST_IRQ_STAT);
}
EXPORT_SYMBOL_GPL(ahci_platform_shutdown);

#ifdef CONFIG_PM_SLEEP
/**
 * ahci_platform_suspend_host - Suspend an ahci-platform host
 * @dev: device pointer for the host
 *
 * This function does all the usual steps needed to suspend an
 * ahci-platform host, note any necessary resources (ie clks, phys, etc.)
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

	if (hpriv->flags & AHCI_HFLAG_SUSPEND_PHYS)
		ahci_platform_disable_phys(hpriv);

	ata_host_suspend(host, PMSG_SUSPEND);
	return 0;
}
EXPORT_SYMBOL_GPL(ahci_platform_suspend_host);

/**
 * ahci_platform_resume_host - Resume an ahci-platform host
 * @dev: device pointer for the host
 *
 * This function does all the usual steps needed to resume an ahci-platform
 * host, note any necessary resources (ie clks, phys, etc.)  must be
 * initialized / enabled before calling this.
 *
 * RETURNS:
 * 0 on success otherwise a negative error code
 */
int ahci_platform_resume_host(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int rc;

	if (dev->power.power_state.event == PM_EVENT_SUSPEND) {
		rc = ahci_reset_controller(host);
		if (rc)
			return rc;

		ahci_init_controller(host);
	}

	if (hpriv->flags & AHCI_HFLAG_SUSPEND_PHYS)
		ahci_platform_enable_phys(hpriv);

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
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int rc;

	rc = ahci_platform_suspend_host(dev);
	if (rc)
		return rc;

	ahci_platform_disable_resources(hpriv);

	return 0;
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
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int rc;

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

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
