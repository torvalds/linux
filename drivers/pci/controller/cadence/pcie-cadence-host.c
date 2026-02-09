// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Cadence
// Cadence PCIe host controller driver.
// Author: Cyrille Pitchen <cyrille.pitchen@free-electrons.com>

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list_sort.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/platform_device.h>

#include "pcie-cadence.h"
#include "pcie-cadence-host-common.h"

static u8 bar_aperture_mask[] = {
	[RP_BAR0] = 0x1F,
	[RP_BAR1] = 0xF,
};

void __iomem *cdns_pci_map_bus(struct pci_bus *bus, unsigned int devfn,
			       int where)
{
	struct pci_host_bridge *bridge = pci_find_host_bridge(bus);
	struct cdns_pcie_rc *rc = pci_host_bridge_priv(bridge);
	struct cdns_pcie *pcie = &rc->pcie;
	unsigned int busn = bus->number;
	u32 addr0, desc0;

	if (pci_is_root_bus(bus)) {
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
	if (busn == bridge->busnr + 1)
		desc0 |= CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_CONF_TYPE0;
	else
		desc0 |= CDNS_PCIE_AT_OB_REGION_DESC0_TYPE_CONF_TYPE1;
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_OB_REGION_DESC0(0), desc0);

	return rc->cfg_base + (where & 0xfff);
}
EXPORT_SYMBOL_GPL(cdns_pci_map_bus);

static struct pci_ops cdns_pcie_host_ops = {
	.map_bus	= cdns_pci_map_bus,
	.read		= pci_generic_config_read,
	.write		= pci_generic_config_write,
};

static void cdns_pcie_host_disable_ptm_response(struct cdns_pcie *pcie)
{
	u32 val;

	val = cdns_pcie_readl(pcie, CDNS_PCIE_LM_PTM_CTRL);
	cdns_pcie_writel(pcie, CDNS_PCIE_LM_PTM_CTRL, val & ~CDNS_PCIE_LM_TPM_CTRL_PTMRSEN);
}

static void cdns_pcie_host_enable_ptm_response(struct cdns_pcie *pcie)
{
	u32 val;

	val = cdns_pcie_readl(pcie, CDNS_PCIE_LM_PTM_CTRL);
	cdns_pcie_writel(pcie, CDNS_PCIE_LM_PTM_CTRL, val | CDNS_PCIE_LM_TPM_CTRL_PTMRSEN);
}

static void cdns_pcie_host_deinit_root_port(struct cdns_pcie_rc *rc)
{
	struct cdns_pcie *pcie = &rc->pcie;
	u32 value, ctrl;

	cdns_pcie_rp_writew(pcie, PCI_CLASS_DEVICE, 0xffff);
	cdns_pcie_rp_writeb(pcie, PCI_CLASS_PROG, 0xff);
	cdns_pcie_rp_writeb(pcie, PCI_CLASS_REVISION, 0xff);
	cdns_pcie_writel(pcie, CDNS_PCIE_LM_ID, 0xffffffff);
	cdns_pcie_rp_writew(pcie, PCI_DEVICE_ID, 0xffff);
	ctrl = CDNS_PCIE_LM_BAR_CFG_CTRL_DISABLED;
	value = ~(CDNS_PCIE_LM_RC_BAR_CFG_BAR0_CTRL(ctrl) |
		CDNS_PCIE_LM_RC_BAR_CFG_BAR1_CTRL(ctrl) |
		CDNS_PCIE_LM_RC_BAR_CFG_PREFETCH_MEM_ENABLE |
		CDNS_PCIE_LM_RC_BAR_CFG_PREFETCH_MEM_64BITS |
		CDNS_PCIE_LM_RC_BAR_CFG_IO_ENABLE |
		CDNS_PCIE_LM_RC_BAR_CFG_IO_32BITS);
	cdns_pcie_writel(pcie, CDNS_PCIE_LM_RC_BAR_CFG, value);
}

static int cdns_pcie_host_init_root_port(struct cdns_pcie_rc *rc)
{
	struct cdns_pcie *pcie = &rc->pcie;
	u32 value, ctrl;
	u32 id;

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
	if (rc->vendor_id != 0xffff) {
		id = CDNS_PCIE_LM_ID_VENDOR(rc->vendor_id) |
			CDNS_PCIE_LM_ID_SUBSYS(rc->vendor_id);
		cdns_pcie_writel(pcie, CDNS_PCIE_LM_ID, id);
	}

	if (rc->device_id != 0xffff)
		cdns_pcie_rp_writew(pcie, PCI_DEVICE_ID, rc->device_id);

	cdns_pcie_rp_writeb(pcie, PCI_CLASS_REVISION, 0);
	cdns_pcie_rp_writeb(pcie, PCI_CLASS_PROG, 0);
	cdns_pcie_rp_writew(pcie, PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_PCI);

	return 0;
}

int cdns_pcie_host_bar_ib_config(struct cdns_pcie_rc *rc,
				 enum cdns_pcie_rp_bar bar,
				 u64 cpu_addr,
				 u64 size,
				 unsigned long flags)
{
	struct cdns_pcie *pcie = &rc->pcie;
	u32 addr0, addr1, aperture, value;

	if (!rc->avail_ib_bar[bar])
		return -EBUSY;

	rc->avail_ib_bar[bar] = false;

	aperture = ilog2(size);
	addr0 = CDNS_PCIE_AT_IB_RP_BAR_ADDR0_NBITS(aperture) |
		(lower_32_bits(cpu_addr) & GENMASK(31, 8));
	addr1 = upper_32_bits(cpu_addr);
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_IB_RP_BAR_ADDR0(bar), addr0);
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_IB_RP_BAR_ADDR1(bar), addr1);

	if (bar == RP_NO_BAR)
		return 0;

	value = cdns_pcie_readl(pcie, CDNS_PCIE_LM_RC_BAR_CFG);
	value &= ~(LM_RC_BAR_CFG_CTRL_MEM_64BITS(bar) |
		   LM_RC_BAR_CFG_CTRL_PREF_MEM_64BITS(bar) |
		   LM_RC_BAR_CFG_CTRL_MEM_32BITS(bar) |
		   LM_RC_BAR_CFG_CTRL_PREF_MEM_32BITS(bar) |
		   LM_RC_BAR_CFG_APERTURE(bar, bar_aperture_mask[bar] + 2));
	if (size + cpu_addr >= SZ_4G) {
		if (!(flags & IORESOURCE_PREFETCH))
			value |= LM_RC_BAR_CFG_CTRL_MEM_64BITS(bar);
		value |= LM_RC_BAR_CFG_CTRL_PREF_MEM_64BITS(bar);
	} else {
		if (!(flags & IORESOURCE_PREFETCH))
			value |= LM_RC_BAR_CFG_CTRL_MEM_32BITS(bar);
		value |= LM_RC_BAR_CFG_CTRL_PREF_MEM_32BITS(bar);
	}

	value |= LM_RC_BAR_CFG_APERTURE(bar, aperture);
	cdns_pcie_writel(pcie, CDNS_PCIE_LM_RC_BAR_CFG, value);

	return 0;
}

static void cdns_pcie_host_unmap_dma_ranges(struct cdns_pcie_rc *rc)
{
	struct cdns_pcie *pcie = &rc->pcie;
	enum cdns_pcie_rp_bar bar;
	u32 value;

	/* Reset inbound configuration for all BARs which were being used */
	for (bar = RP_BAR0; bar <= RP_NO_BAR; bar++) {
		if (rc->avail_ib_bar[bar])
			continue;

		cdns_pcie_writel(pcie, CDNS_PCIE_AT_IB_RP_BAR_ADDR0(bar), 0);
		cdns_pcie_writel(pcie, CDNS_PCIE_AT_IB_RP_BAR_ADDR1(bar), 0);

		if (bar == RP_NO_BAR)
			continue;

		value = ~(LM_RC_BAR_CFG_CTRL_MEM_64BITS(bar) |
			  LM_RC_BAR_CFG_CTRL_PREF_MEM_64BITS(bar) |
			  LM_RC_BAR_CFG_CTRL_MEM_32BITS(bar) |
			  LM_RC_BAR_CFG_CTRL_PREF_MEM_32BITS(bar) |
			  LM_RC_BAR_CFG_APERTURE(bar, bar_aperture_mask[bar] + 2));
		cdns_pcie_writel(pcie, CDNS_PCIE_LM_RC_BAR_CFG, value);
	}
}

static void cdns_pcie_host_deinit_address_translation(struct cdns_pcie_rc *rc)
{
	struct cdns_pcie *pcie = &rc->pcie;
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(rc);
	struct resource_entry *entry;
	int r;

	cdns_pcie_host_unmap_dma_ranges(rc);

	/*
	 * Reset outbound region 0 which was reserved for configuration space
	 * accesses.
	 */
	cdns_pcie_reset_outbound_region(pcie, 0);

	/* Reset rest of the outbound regions */
	r = 1;
	resource_list_for_each_entry(entry, &bridge->windows) {
		cdns_pcie_reset_outbound_region(pcie, r);
		r++;
	}
}

static int cdns_pcie_host_init_address_translation(struct cdns_pcie_rc *rc)
{
	struct cdns_pcie *pcie = &rc->pcie;
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(rc);
	struct resource *cfg_res = rc->cfg_res;
	struct resource_entry *entry;
	u64 cpu_addr = cfg_res->start;
	u32 addr0, addr1, desc1;
	int r, busnr = 0;

	entry = resource_list_first_type(&bridge->windows, IORESOURCE_BUS);
	if (entry)
		busnr = entry->res->start;

	/*
	 * Reserve region 0 for PCI configure space accesses:
	 * OB_REGION_PCI_ADDR0 and OB_REGION_DESC0 are updated dynamically by
	 * cdns_pci_map_bus(), other region registers are set here once for all.
	 */
	addr1 = 0; /* Should be programmed to zero. */
	desc1 = CDNS_PCIE_AT_OB_REGION_DESC1_BUS(busnr);
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_OB_REGION_PCI_ADDR1(0), addr1);
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_OB_REGION_DESC1(0), desc1);

	if (pcie->ops && pcie->ops->cpu_addr_fixup)
		cpu_addr = pcie->ops->cpu_addr_fixup(pcie, cpu_addr);

	addr0 = CDNS_PCIE_AT_OB_REGION_CPU_ADDR0_NBITS(12) |
		(lower_32_bits(cpu_addr) & GENMASK(31, 8));
	addr1 = upper_32_bits(cpu_addr);
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_OB_REGION_CPU_ADDR0(0), addr0);
	cdns_pcie_writel(pcie, CDNS_PCIE_AT_OB_REGION_CPU_ADDR1(0), addr1);

	r = 1;
	resource_list_for_each_entry(entry, &bridge->windows) {
		struct resource *res = entry->res;
		u64 pci_addr = res->start - entry->offset;

		if (resource_type(res) == IORESOURCE_IO)
			cdns_pcie_set_outbound_region(pcie, busnr, 0, r,
						      true,
						      pci_pio_to_address(res->start),
						      pci_addr,
						      resource_size(res));
		else
			cdns_pcie_set_outbound_region(pcie, busnr, 0, r,
						      false,
						      res->start,
						      pci_addr,
						      resource_size(res));

		r++;
	}

	return cdns_pcie_host_map_dma_ranges(rc, cdns_pcie_host_bar_ib_config);
}

static void cdns_pcie_host_deinit(struct cdns_pcie_rc *rc)
{
	cdns_pcie_host_deinit_address_translation(rc);
	cdns_pcie_host_deinit_root_port(rc);
}

int cdns_pcie_host_init(struct cdns_pcie_rc *rc)
{
	int err;

	err = cdns_pcie_host_init_root_port(rc);
	if (err)
		return err;

	return cdns_pcie_host_init_address_translation(rc);
}
EXPORT_SYMBOL_GPL(cdns_pcie_host_init);

static void cdns_pcie_host_link_disable(struct cdns_pcie_rc *rc)
{
	struct cdns_pcie *pcie = &rc->pcie;

	cdns_pcie_stop_link(pcie);
	cdns_pcie_host_disable_ptm_response(pcie);
}

int cdns_pcie_host_link_setup(struct cdns_pcie_rc *rc)
{
	struct cdns_pcie *pcie = &rc->pcie;
	struct device *dev = rc->pcie.dev;
	int ret;

	if (rc->quirk_detect_quiet_flag)
		cdns_pcie_detect_quiet_min_delay_set(&rc->pcie);

	cdns_pcie_host_enable_ptm_response(pcie);

	ret = cdns_pcie_start_link(pcie);
	if (ret) {
		dev_err(dev, "Failed to start link\n");
		return ret;
	}

	ret = cdns_pcie_host_start_link(rc, cdns_pcie_link_up);
	if (ret)
		dev_dbg(dev, "PCIe link never came up\n");

	return 0;
}
EXPORT_SYMBOL_GPL(cdns_pcie_host_link_setup);

void cdns_pcie_host_disable(struct cdns_pcie_rc *rc)
{
	struct pci_host_bridge *bridge;

	bridge = pci_host_bridge_from_priv(rc);
	pci_stop_root_bus(bridge->bus);
	pci_remove_root_bus(bridge->bus);

	cdns_pcie_host_deinit(rc);
	cdns_pcie_host_link_disable(rc);
}
EXPORT_SYMBOL_GPL(cdns_pcie_host_disable);

int cdns_pcie_host_setup(struct cdns_pcie_rc *rc)
{
	struct device *dev = rc->pcie.dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *np = dev->of_node;
	struct pci_host_bridge *bridge;
	enum cdns_pcie_rp_bar bar;
	struct cdns_pcie *pcie;
	struct resource *res;
	int ret;

	bridge = pci_host_bridge_from_priv(rc);
	if (!bridge)
		return -ENOMEM;

	pcie = &rc->pcie;
	pcie->is_rc = true;

	rc->vendor_id = 0xffff;
	of_property_read_u32(np, "vendor-id", &rc->vendor_id);

	rc->device_id = 0xffff;
	of_property_read_u32(np, "device-id", &rc->device_id);

	pcie->reg_base = devm_platform_ioremap_resource_byname(pdev, "reg");
	if (IS_ERR(pcie->reg_base)) {
		dev_err(dev, "missing \"reg\"\n");
		return PTR_ERR(pcie->reg_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg");
	rc->cfg_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(rc->cfg_base))
		return PTR_ERR(rc->cfg_base);
	rc->cfg_res = res;

	ret = cdns_pcie_host_link_setup(rc);
	if (ret)
		return ret;

	for (bar = RP_BAR0; bar <= RP_NO_BAR; bar++)
		rc->avail_ib_bar[bar] = true;

	ret = cdns_pcie_host_init(rc);
	if (ret)
		return ret;

	if (!bridge->ops)
		bridge->ops = &cdns_pcie_host_ops;

	return pci_host_probe(bridge);
}
EXPORT_SYMBOL_GPL(cdns_pcie_host_setup);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cadence PCIe host controller driver");
MODULE_AUTHOR("Cyrille Pitchen <cyrille.pitchen@free-electrons.com>");
