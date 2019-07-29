// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Cadence
// Cadence PCIe host controller driver.
// Author: Cyrille Pitchen <cyrille.pitchen@free-electrons.com>

#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "pcie-cadence.h"

/**
 * struct cdns_pcie_rc - private data for this PCIe Root Complex driver
 * @pcie: Cadence PCIe controller
 * @dev: pointer to PCIe device
 * @cfg_res: start/end offsets in the physical system memory to map PCI
 *           configuration space accesses
 * @bus_range: first/last buses behind the PCIe host controller
 * @cfg_base: IO mapped window to access the PCI configuration space of a
 *            single function at a time
 * @max_regions: maximum number of regions supported by the hardware
 * @no_bar_nbits: Number of bits to keep for inbound (PCIe -> CPU) address
 *                translation (nbits sets into the "no BAR match" register)
 * @vendor_id: PCI vendor ID
 * @device_id: PCI device ID
 */
struct cdns_pcie_rc {
	struct cdns_pcie	pcie;
	struct device		*dev;
	struct resource		*cfg_res;
	struct resource		*bus_range;
	void __iomem		*cfg_base;
	u32			max_regions;
	u32			no_bar_nbits;
	u16			vendor_id;
	u16			device_id;
};

static void __iomem *cdns_pci_map_bus(struct pci_bus *bus, unsigned int devfn,
				      int where)
{
	struct pci_host_bridge *bridge = pci_find_host_bridge(bus);
	struct cdns_pcie_rc *rc = pci_host_bridge_priv(bridge);
	struct cdns_pcie *pcie = &rc->pcie;
	unsigned int busn = bus->number;
	u32 addr0, desc0;

	if (busn == rc->bus_range->start) {
		/*
		 * Only the root port (devfn == 0) is connected to this bus.
		 * All other PCI devices are behind some bridge hence on another
		 * bus.
		 */
		if (devfn)
			return NULL;

		return pcie->reg_base + (where & 0xfff);
	}
	/* Check that the link is up */
	if (!(cdns_pcie_readl(pcie, CDNS_PCIE_LM_BASE) & 0x1))
		return NULL;
	/* Clear AXI link-down status */
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_LINKDOWN, 0x0);

	/* Update Output registers for AXI region 0. */
	addr0 = CDNS_PCIE_AT_OB_REGION_PCI_ADDR0_NBITS(12) |
		CDNS_PCIE_AT_OB_REGION_PCI_ADDR0_DEVFN(devfn) |
		CDNS_PCIE_AT_OB_REGION_PCI_ADDR0_BUS(busn);
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_OB_REGION_PCI_ADDR0(0), addr0);

	/* Configuration Type 0 or Type 1 access. */
	desc0 = CDNS_PCIE_AT_OB_REGION_DESC0_HARDCODED_RID |
		CDNS_PCIE_AT_OB_REGION_DESC0_DEVFN(0);
	/*
	 * The bus number was already set once for all in desc1 by
	 * cdns_pcie_host_init_address_translation().
	 */
	if (busn == rc->bus_range->start + 1)
		desc0 |= CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_CONF_TYPE0;
	else
		desc0 |= CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_CONF_TYPE1;
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_OB_REGION_DESC0(0), desc0);

	return rc->cfg_base + (where & 0xfff);
}

static struct pci_ops cdns_pcie_host_ops = {
	.map_bus	= cdns_pci_map_bus,
	.read		= pci_generic_config_read,
	.write		= pci_generic_config_write,
};

static const struct of_device_id cdns_pcie_host_of_match[] = {
	{ .compatible = "cdns,cdns-pcie-host" },

	{ },
};

static int cdns_pcie_host_init_root_port(struct cdns_pcie_rc *rc)
{
	struct cdns_pcie *pcie = &rc->pcie;
	u32 value, ctrl;

	/*
	 * Set the root complex BAR configuration register:
	 * - disable both BAR0 and BAR1.
	 * - enable Prefetchable Memory Base and Limit registers in type 1
	 *   config space (64 bits).
	 * - enable IO Base and Limit registers in type 1 config
	 *   space (32 bits).
	 */
	ctrl = CDNS_PCIE_LM_BAR_CFG_CTRL_DISABLED;
	value = CDNS_PCIE_LM_RC_BAR_CFG_BAR0_CTRL(ctrl) |
		CDNS_PCIE_LM_RC_BAR_CFG_BAR1_CTRL(ctrl) |
		CDNS_PCIE_LM_RC_BAR_CFG_PREFETCH_MEM_ENABLE |
		CDNS_PCIE_LM_RC_BAR_CFG_PREFETCH_MEM_64BITS |
		CDNS_PCIE_LM_RC_BAR_CFG_IO_ENABLE |
		CDNS_PCIE_LM_RC_BAR_CFG_IO_32BITS;
	cdns_pcie_writel(pcie, CDNS_PCIE_LM_RC_BAR_CFG, value);

	/* Set root port configuration space */
	if (rc->vendor_id != 0xffff)
		cdns_pcie_rp_writew(pcie, PCI_VENDOR_ID, rc->vendor_id);
	if (rc->device_id != 0xffff)
		cdns_pcie_rp_writew(pcie, PCI_DEVICE_ID, rc->device_id);

	cdns_pcie_rp_writeb(pcie, PCI_CLASS_REVISION, 0);
	cdns_pcie_rp_writeb(pcie, PCI_CLASS_PROG, 0);
	cdns_pcie_rp_writew(pcie, PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_PCI);

	return 0;
}

static int cdns_pcie_host_init_address_translation(struct cdns_pcie_rc *rc)
{
	struct cdns_pcie *pcie = &rc->pcie;
	struct resource *cfg_res = rc->cfg_res;
	struct resource *mem_res = pcie->mem_res;
	struct resource *bus_range = rc->bus_range;
	struct device *dev = rc->dev;
	struct device_node *np = dev->of_node;
	struct of_pci_range_parser parser;
	struct of_pci_range range;
	u32 addr0, addr1, desc1;
	u64 cpu_addr;
	int r, err;

	/*
	 * Reserve region 0 for PCI configure space accesses:
	 * OB_REGION_PCI_ADDR0 and OB_REGION_DESC0 are updated dynamically by
	 * cdns_pci_map_bus(), other region registers are set here once for all.
	 */
	addr1 = 0; /* Should be programmed to zero. */
	desc1 = CDNS_PCIE_AT_OB_REGION_DESC1_BUS(bus_range->start);
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_OB_REGION_PCI_ADDR1(0), addr1);
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_OB_REGION_DESC1(0), desc1);

	cpu_addr = cfg_res->start - mem_res->start;
	addr0 = CDNS_PCIE_AT_OB_REGION_CPU_ADDR0_NBITS(12) |
		(lower_32_bits(cpu_addr) & GENMASK(31, 8));
	addr1 = upper_32_bits(cpu_addr);
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_OB_REGION_CPU_ADDR0(0), addr0);
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_OB_REGION_CPU_ADDR1(0), addr1);

	err = of_pci_range_parser_init(&parser, np);
	if (err)
		return err;

	r = 1;
	for_each_of_pci_range(&parser, &range) {
		bool is_io;

		if (r >= rc->max_regions)
			break;

		if ((range.flags & IORESOURCE_TYPE_BITS) == IORESOURCE_MEM)
			is_io = false;
		else if ((range.flags & IORESOURCE_TYPE_BITS) == IORESOURCE_IO)
			is_io = true;
		else
			continue;

		cdns_pcie_set_outbound_region(pcie, 0, r, is_io,
					      range.cpu_addr,
					      range.pci_addr,
					      range.size);
		r++;
	}

	/*
	 * Set Root Port no BAR match Inbound Translation registers:
	 * needed for MSI and DMA.
	 * Root Port BAR0 and BAR1 are disabled, hence no need to set their
	 * inbound translation registers.
	 */
	addr0 = CDNS_PCIE_AT_IB_RP_BAR_ADDR0_NBITS(rc->no_bar_nbits);
	addr1 = 0;
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_IB_RP_BAR_ADDR0(RP_NO_BAR), addr0);
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_IB_RP_BAR_ADDR1(RP_NO_BAR), addr1);

	return 0;
}

static int cdns_pcie_host_init(struct device *dev,
			       struct list_head *resources,
			       struct cdns_pcie_rc *rc)
{
	struct resource *bus_range = NULL;
	int err;

	/* Parse our PCI ranges and request their resources */
	err = pci_parse_request_of_pci_ranges(dev, resources, &bus_range);
	if (err)
		return err;

	rc->bus_range = bus_range;
	rc->pcie.bus = bus_range->start;

	err = cdns_pcie_host_init_root_port(rc);
	if (err)
		goto err_out;

	err = cdns_pcie_host_init_address_translation(rc);
	if (err)
		goto err_out;

	return 0;

 err_out:
	pci_free_resource_list(resources);
	return err;
}

static int cdns_pcie_host_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct pci_host_bridge *bridge;
	struct list_head resources;
	struct cdns_pcie_rc *rc;
	struct cdns_pcie *pcie;
	struct resource *res;
	int ret;
	int phy_count;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*rc));
	if (!bridge)
		return -ENOMEM;

	rc = pci_host_bridge_priv(bridge);
	rc->dev = dev;

	pcie = &rc->pcie;
	pcie->is_rc = true;

	rc->max_regions = 32;
	of_property_read_u32(np, "cdns,max-outbound-regions", &rc->max_regions);

	rc->no_bar_nbits = 32;
	of_property_read_u32(np, "cdns,no-bar-match-nbits", &rc->no_bar_nbits);

	rc->vendor_id = 0xffff;
	of_property_read_u16(np, "vendor-id", &rc->vendor_id);

	rc->device_id = 0xffff;
	of_property_read_u16(np, "device-id", &rc->device_id);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "reg");
	pcie->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->reg_base)) {
		dev_err(dev, "missing \"reg\"\n");
		return PTR_ERR(pcie->reg_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg");
	rc->cfg_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(rc->cfg_base)) {
		dev_err(dev, "missing \"cfg\"\n");
		return PTR_ERR(rc->cfg_base);
	}
	rc->cfg_res = res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mem");
	if (!res) {
		dev_err(dev, "missing \"mem\"\n");
		return -EINVAL;
	}
	pcie->mem_res = res;

	ret = cdns_pcie_init_phy(dev, pcie);
	if (ret) {
		dev_err(dev, "failed to init phy\n");
		return ret;
	}
	platform_set_drvdata(pdev, pcie);

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_get_sync() failed\n");
		goto err_get_sync;
	}

	ret = cdns_pcie_host_init(dev, &resources, rc);
	if (ret)
		goto err_init;

	list_splice_init(&resources, &bridge->windows);
	bridge->dev.parent = dev;
	bridge->busnr = pcie->bus;
	bridge->ops = &cdns_pcie_host_ops;
	bridge->map_irq = of_irq_parse_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;

	ret = pci_host_probe(bridge);
	if (ret < 0)
		goto err_host_probe;

	return 0;

 err_host_probe:
	pci_free_resource_list(&resources);

 err_init:
	pm_runtime_put_sync(dev);

 err_get_sync:
	pm_runtime_disable(dev);
	cdns_pcie_disable_phy(pcie);
	phy_count = pcie->phy_count;
	while (phy_count--)
		device_link_del(pcie->link[phy_count]);

	return ret;
}

static void cdns_pcie_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cdns_pcie *pcie = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_put_sync(dev);
	if (ret < 0)
		dev_dbg(dev, "pm_runtime_put_sync failed\n");

	pm_runtime_disable(dev);
	cdns_pcie_disable_phy(pcie);
}

static struct platform_driver cdns_pcie_host_driver = {
	.driver = {
		.name = "cdns-pcie-host",
		.of_match_table = cdns_pcie_host_of_match,
		.pm	= &cdns_pcie_pm_ops,
	},
	.probe = cdns_pcie_host_probe,
	.shutdown = cdns_pcie_shutdown,
};
builtin_platform_driver(cdns_pcie_host_driver);
