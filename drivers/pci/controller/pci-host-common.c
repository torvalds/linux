// SPDX-License-Identifier: GPL-2.0
/*
 * Common library for PCI host controller drivers
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci-ecam.h>
#include <linux/platform_device.h>

#include "pci-host-common.h"

/**
 * pci_host_common_delete_ports - Cleanup function for port list
 * @data: Pointer to the port list head
 */
void pci_host_common_delete_ports(void *data)
{
	struct list_head *ports = data;
	struct pci_host_perst *perst, *tmp_perst;
	struct pci_host_port *port, *tmp_port;

	list_for_each_entry_safe(port, tmp_port, ports, list) {
		list_for_each_entry_safe(perst, tmp_perst, &port->perst, list)
			list_del(&perst->list);
		list_del(&port->list);
	}
}
EXPORT_SYMBOL_GPL(pci_host_common_delete_ports);

/**
 * pci_host_common_parse_perst - Parse PERST# from all nodes, depth first
 * @dev: Device pointer
 * @port: PCI host port
 * @np: Device tree node to start parsing from
 *
 * Recursively parse PERST# GPIO from all PCIe bridge nodes starting from
 * @np in a depth-first manner.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int pci_host_common_parse_perst(struct device *dev,
				       struct pci_host_port *port,
				       struct device_node *np)
{
	struct pci_host_perst *perst;
	struct gpio_desc *reset;
	int ret;

	if (!of_property_present(np, "reset-gpios"))
		goto parse_child_node;

	reset = devm_fwnode_gpiod_get(dev, of_fwnode_handle(np), "reset",
				      GPIOD_ASIS, "PERST#");
	if (IS_ERR(reset)) {
		/*
		 * FIXME: GPIOLIB currently supports exclusive GPIO access only.
		 * Non exclusive access is broken. But shared PERST# requires
		 * non-exclusive access. So once GPIOLIB properly supports it,
		 * implement it here.
		 */
		if (PTR_ERR(reset) == -EBUSY)
			dev_err(dev, "Shared PERST# is not supported\n");

		return PTR_ERR(reset);
	}

	perst = devm_kzalloc(dev, sizeof(*perst), GFP_KERNEL);
	if (!perst)
		return -ENOMEM;

	INIT_LIST_HEAD(&perst->list);
	perst->desc = reset;
	list_add_tail(&perst->list, &port->perst);

parse_child_node:
	for_each_available_child_of_node_scoped(np, child) {
		ret = pci_host_common_parse_perst(dev, port, child);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * pci_host_common_parse_port - Parse a single Root Port node
 * @dev: Device pointer
 * @bridge: PCI host bridge
 * @node: Device tree node of the Root Port
 *
 * Parse Root Port properties from the device tree.  Currently it only
 * handles the PERST# GPIO (including PERST# GPIOs from all PCIe bridge
 * nodes under this Root Port), which is optional.
 *
 * NOTE: This helper fetches resources (like PERST# GPIO) optionally.  If a
 * controller driver has a hard dependency on certain resources (PHY,
 * clocks, regulators, etc.), those resources MUST be modeled correctly in
 * the DT binding and validated in DTS. This helper cannot enforce such
 * dependencies and the driver may fail to operate if required resources
 * are missing.
 *
 * Return: 0 on success, -ENODEV if PERST# found in RC node (legacy binding
 * should be used), Other negative error codes on failure.
 */
static int pci_host_common_parse_port(struct device *dev,
				      struct pci_host_bridge *bridge,
				      struct device_node *node)
{
	struct pci_host_port *port;
	int ret;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	INIT_LIST_HEAD(&port->perst);

	ret = pci_host_common_parse_perst(dev, port, node);
	if (ret)
		return ret;

	/*
	 * 1. PERST# found in RP or its child nodes - list is not empty,
	 *    continue
	 *
	 * 2. PERST# not found in RP/children, but found in RC node -
	 *    return -ENODEV to fallback legacy binding
	 *
	 * 3. PERST# not found anywhere - list is empty, continue (optional
	 *    PERST#)
	 */
	if (list_empty(&port->perst)) {
		if (of_property_present(dev->of_node, "reset-gpios") ||
		    of_property_present(dev->of_node, "reset-gpio"))
			return -ENODEV;
	}

	INIT_LIST_HEAD(&port->list);
	list_add_tail(&port->list, &bridge->ports);

	return 0;
}

/**
 * pci_host_common_parse_ports - Parse Root Port nodes from device tree
 * @dev: Device pointer
 * @bridge: PCI host bridge
 *
 * Iterate through child nodes of the host bridge and parse Root Port
 * properties (currently only reset GPIOs).
 *
 * Return: 0 on success, -ENODEV if no ports found or PERST# found in RC
 * node (legacy binding should be used), Other negative error codes on
 * failure.
 */
int pci_host_common_parse_ports(struct device *dev, struct pci_host_bridge *bridge)
{
	int ret = -ENODEV;

	for_each_available_child_of_node_scoped(dev->of_node, of_port) {
		if (!of_node_is_type(of_port, "pci"))
			continue;
		ret = pci_host_common_parse_port(dev, bridge, of_port);
		if (ret)
			goto err_cleanup;
	}

	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, pci_host_common_delete_ports,
					&bridge->ports);

err_cleanup:
	pci_host_common_delete_ports(&bridge->ports);
	return ret;
}
EXPORT_SYMBOL_GPL(pci_host_common_parse_ports);

#define PCI_HOST_D3COLD_ALLOWED        BIT(0)
#define PCI_HOST_PME_D3COLD_CAPABLE    BIT(1)


static void gen_pci_unmap_cfg(void *ptr)
{
	pci_ecam_free((struct pci_config_window *)ptr);
}

struct pci_config_window *pci_host_common_ecam_create(struct device *dev,
		struct pci_host_bridge *bridge, const struct pci_ecam_ops *ops)
{
	int err;
	struct resource cfgres;
	struct resource_entry *bus;
	struct pci_config_window *cfg;

	err = of_address_to_resource(dev->of_node, 0, &cfgres);
	if (err) {
		dev_err(dev, "missing or malformed \"reg\" property\n");
		return ERR_PTR(err);
	}

	bus = resource_list_first_type(&bridge->windows, IORESOURCE_BUS);
	if (!bus)
		return ERR_PTR(-ENODEV);

	cfg = pci_ecam_create(dev, &cfgres, bus->res, ops);
	if (IS_ERR(cfg))
		return cfg;

	err = devm_add_action_or_reset(dev, gen_pci_unmap_cfg, cfg);
	if (err)
		return ERR_PTR(err);

	return cfg;
}
EXPORT_SYMBOL_GPL(pci_host_common_ecam_create);

int pci_host_common_init(struct platform_device *pdev,
			 struct pci_host_bridge *bridge,
			 const struct pci_ecam_ops *ops)
{
	struct device *dev = &pdev->dev;
	struct pci_config_window *cfg;

	of_pci_check_probe_only();

	platform_set_drvdata(pdev, bridge);

	/* Parse and map our Configuration Space windows */
	cfg = pci_host_common_ecam_create(dev, bridge, ops);
	if (IS_ERR(cfg))
		return PTR_ERR(cfg);

	/* Do not reassign bus numbers if probe only */
	if (!pci_has_flag(PCI_PROBE_ONLY))
		pci_add_flags(PCI_REASSIGN_ALL_BUS);

	bridge->sysdata = cfg;
	bridge->ops = (struct pci_ops *)&ops->pci_ops;
	bridge->enable_device = ops->enable_device;
	bridge->disable_device = ops->disable_device;
	bridge->msi_domain = true;

	return pci_host_probe(bridge);
}
EXPORT_SYMBOL_GPL(pci_host_common_init);

int pci_host_common_probe(struct platform_device *pdev)
{
	const struct pci_ecam_ops *ops;
	struct pci_host_bridge *bridge;

	ops = of_device_get_match_data(&pdev->dev);
	if (!ops)
		return -ENODEV;

	bridge = devm_pci_alloc_host_bridge(&pdev->dev, 0);
	if (!bridge)
		return -ENOMEM;

	return pci_host_common_init(pdev, bridge, ops);
}
EXPORT_SYMBOL_GPL(pci_host_common_probe);

void pci_host_common_remove(struct platform_device *pdev)
{
	struct pci_host_bridge *bridge = platform_get_drvdata(pdev);

	pci_lock_rescan_remove();
	pci_stop_root_bus(bridge->bus);
	pci_remove_root_bus(bridge->bus);
	pci_unlock_rescan_remove();
}
EXPORT_SYMBOL_GPL(pci_host_common_remove);

static int __pci_host_common_d3cold_possible(struct pci_dev *pdev,
					     void *userdata)
{
	u32 *flags = userdata;

	if (!pdev->dev.driver && !pci_is_enabled(pdev))
		return 0;

	if (pdev->current_state != PCI_D3hot)
		goto exit;

	if (device_may_wakeup(&pdev->dev)) {
		if (!pci_pme_capable(pdev, PCI_D3cold))
			goto exit;
		else
			*flags |= PCI_HOST_PME_D3COLD_CAPABLE;
	}

	return 0;

exit:
	*flags &= ~PCI_HOST_D3COLD_ALLOWED;

	return -EOPNOTSUPP;
}

/**
 * pci_host_common_d3cold_possible - Determine whether the host bridge can
 *				     transition the devices into D3cold.
 *
 * @bridge: PCI host bridge to check
 * @pme_capable: Pointer to update if there is any device capable of generating
 *		 PME from D3cold.
 *
 * Walk downstream PCIe endpoint devices and determine whether the host bridge
 * is permitted to transition the devices into D3cold.
 *
 * Devices under host bridge can enter D3cold only if all active PCIe
 * endpoints are in PCI_D3hot and any wakeup-enabled endpoint is capable of
 * generating PME from D3cold.  Inactive endpoints are ignored.
 *
 * The @pme_capable output allows PCIe controller drivers to apply
 * platform-specific handling to preserve wakeup functionality.
 *
 * Return: %true if the host bridge may enter D3cold, otherwise %false.
 */
bool pci_host_common_d3cold_possible(struct pci_host_bridge *bridge,
				     bool *pme_capable)
{
	u32 flags = PCI_HOST_D3COLD_ALLOWED;

	pci_walk_bus(bridge->bus, __pci_host_common_d3cold_possible, &flags);

	*pme_capable = !!(flags & PCI_HOST_PME_D3COLD_CAPABLE);

	return !!(flags & PCI_HOST_D3COLD_ALLOWED);
}
EXPORT_SYMBOL_GPL(pci_host_common_d3cold_possible);

MODULE_DESCRIPTION("Common library for PCI host controller drivers");
MODULE_LICENSE("GPL v2");
