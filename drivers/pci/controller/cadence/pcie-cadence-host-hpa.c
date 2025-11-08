// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence PCIe host controller driver.
 *
 * Copyright (c) 2024, Cadence Design Systems
 * Author: Manikandan K Pillai <mpillai@cadence.com>
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/list_sort.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#include "pcie-cadence.h"
#include "pcie-cadence-host-common.h"

static u8 bar_aperture_mask[] = {
	[RP_BAR0] = 0x3F,
	[RP_BAR1] = 0x3F,
};

void __iomem *cdns_pci_hpa_map_bus(struct pci_bus *bus, unsigned int devfn,
				   int where)
{
	struct pci_host_bridge *bridge = pci_find_host_bridge(bus);
	struct cdns_pcie_rc *rc = pci_host_bridge_priv(bridge);
	struct cdns_pcie *pcie = &rc->pcie;
	unsigned int busn = bus->number;
	u32 addr0, desc0, desc1, ctrl0;
	u32 regval;

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

	/* Clear AXI link-down status */
	regval = cdns_pcie_hpa_readl(pcie, REG_BANK_AXI_SLAVE, CDNS_PCIE_HPA_AT_LINKDOWN);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE, CDNS_PCIE_HPA_AT_LINKDOWN,
			     (regval & ~GENMASK(0, 0)));

	/* Update Output registers for AXI region 0 */
	addr0 = CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0_NBITS(12) |
		CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0_DEVFN(devfn) |
		CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0_BUS(busn);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0(0), addr0);

	desc1 = cdns_pcie_hpa_readl(pcie, REG_BANK_AXI_SLAVE,
				    CDNS_PCIE_HPA_AT_OB_REGION_DESC1(0));
	desc1 &= ~CDNS_PCIE_HPA_AT_OB_REGION_DESC1_DEVFN_MASK;
	desc1 |= CDNS_PCIE_HPA_AT_OB_REGION_DESC1_DEVFN(0);
	ctrl0 = CDNS_PCIE_HPA_AT_OB_REGION_CTRL0_SUPPLY_BUS |
		CDNS_PCIE_HPA_AT_OB_REGION_CTRL0_SUPPLY_DEV_FN;

	if (busn == bridge->busnr + 1)
		desc0 = CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_CONF_TYPE0;
	else
		desc0 = CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_CONF_TYPE1;

	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_DESC0(0), desc0);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_DESC1(0), desc1);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_CTRL0(0), ctrl0);

	return rc->cfg_base + (where & 0xfff);
}

static struct pci_ops cdns_pcie_hpa_host_ops = {
	.map_bus	= cdns_pci_hpa_map_bus,
	.read		= pci_generic_config_read,
	.write		= pci_generic_config_write,
};

static void cdns_pcie_hpa_host_enable_ptm_response(struct cdns_pcie *pcie)
{
	u32 val;

	val = cdns_pcie_hpa_readl(pcie, REG_BANK_IP_REG, CDNS_PCIE_HPA_LM_PTM_CTRL);
	cdns_pcie_hpa_writel(pcie, REG_BANK_IP_REG, CDNS_PCIE_HPA_LM_PTM_CTRL,
			     val | CDNS_PCIE_HPA_LM_PTM_CTRL_PTMRSEN);
}

static int cdns_pcie_hpa_host_bar_ib_config(struct cdns_pcie_rc *rc,
					    enum cdns_pcie_rp_bar bar,
					    u64 cpu_addr, u64 size,
					    unsigned long flags)
{
	struct cdns_pcie *pcie = &rc->pcie;
	u32 addr0, addr1, aperture, value;

	if (!rc->avail_ib_bar[bar])
		return -ENODEV;

	rc->avail_ib_bar[bar] = false;

	aperture = ilog2(size);
	if (bar == RP_NO_BAR) {
		addr0 = CDNS_PCIE_HPA_AT_IB_RP_BAR_ADDR0_NBITS(aperture) |
			(lower_32_bits(cpu_addr) & GENMASK(31, 8));
		addr1 = upper_32_bits(cpu_addr);
	} else {
		addr0 = lower_32_bits(cpu_addr);
		addr1 = upper_32_bits(cpu_addr);
	}
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_MASTER,
			     CDNS_PCIE_HPA_AT_IB_RP_BAR_ADDR0(bar), addr0);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_MASTER,
			     CDNS_PCIE_HPA_AT_IB_RP_BAR_ADDR1(bar), addr1);

	if (bar == RP_NO_BAR)
		bar = (enum cdns_pcie_rp_bar)BAR_0;

	value = cdns_pcie_hpa_readl(pcie, REG_BANK_IP_CFG_CTRL_REG, CDNS_PCIE_HPA_LM_RC_BAR_CFG);
	value &= ~(HPA_LM_RC_BAR_CFG_CTRL_MEM_64BITS(bar) |
		   HPA_LM_RC_BAR_CFG_CTRL_PREF_MEM_64BITS(bar) |
		   HPA_LM_RC_BAR_CFG_CTRL_MEM_32BITS(bar) |
		   HPA_LM_RC_BAR_CFG_CTRL_PREF_MEM_32BITS(bar) |
		   HPA_LM_RC_BAR_CFG_APERTURE(bar, bar_aperture_mask[bar] + 7));
	if (size + cpu_addr >= SZ_4G) {
		value |= HPA_LM_RC_BAR_CFG_CTRL_MEM_64BITS(bar);
		if ((flags & IORESOURCE_PREFETCH))
			value |= HPA_LM_RC_BAR_CFG_CTRL_PREF_MEM_64BITS(bar);
	} else {
		value |= HPA_LM_RC_BAR_CFG_CTRL_MEM_32BITS(bar);
		if ((flags & IORESOURCE_PREFETCH))
			value |= HPA_LM_RC_BAR_CFG_CTRL_PREF_MEM_32BITS(bar);
	}

	value |= HPA_LM_RC_BAR_CFG_APERTURE(bar, aperture);
	cdns_pcie_hpa_writel(pcie, REG_BANK_IP_CFG_CTRL_REG, CDNS_PCIE_HPA_LM_RC_BAR_CFG, value);

	return 0;
}

static int cdns_pcie_hpa_host_init_root_port(struct cdns_pcie_rc *rc)
{
	struct cdns_pcie *pcie = &rc->pcie;
	u32 value, ctrl;

	/*
	 * Set the root port BAR configuration register:
	 * - disable both BAR0 and BAR1
	 * - enable Prefetchable Memory Base and Limit registers in type 1
	 *   config space (64 bits)
	 * - enable IO Base and Limit registers in type 1 config
	 *   space (32 bits)
	 */

	ctrl = CDNS_PCIE_HPA_LM_BAR_CFG_CTRL_DISABLED;
	value = CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR0_CTRL(ctrl) |
		CDNS_PCIE_HPA_LM_RC_BAR_CFG_BAR1_CTRL(ctrl) |
		CDNS_PCIE_HPA_LM_RC_BAR_CFG_PREFETCH_MEM_ENABLE |
		CDNS_PCIE_HPA_LM_RC_BAR_CFG_PREFETCH_MEM_64BITS |
		CDNS_PCIE_HPA_LM_RC_BAR_CFG_IO_ENABLE |
		CDNS_PCIE_HPA_LM_RC_BAR_CFG_IO_32BITS;
	cdns_pcie_hpa_writel(pcie, REG_BANK_IP_CFG_CTRL_REG,
			     CDNS_PCIE_HPA_LM_RC_BAR_CFG, value);

	if (rc->vendor_id != 0xffff)
		cdns_pcie_hpa_rp_writew(pcie, PCI_VENDOR_ID, rc->vendor_id);

	if (rc->device_id != 0xffff)
		cdns_pcie_hpa_rp_writew(pcie, PCI_DEVICE_ID, rc->device_id);

	cdns_pcie_hpa_rp_writeb(pcie, PCI_CLASS_REVISION, 0);
	cdns_pcie_hpa_rp_writeb(pcie, PCI_CLASS_PROG, 0);
	cdns_pcie_hpa_rp_writew(pcie, PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_PCI);

	/* Enable bus mastering */
	value = cdns_pcie_hpa_readl(pcie, REG_BANK_RP, PCI_COMMAND);
	value |= (PCI_COMMAND_MEMORY | PCI_COMMAND_IO | PCI_COMMAND_MASTER);
	cdns_pcie_hpa_writel(pcie, REG_BANK_RP, PCI_COMMAND, value);
	return 0;
}

static void cdns_pcie_hpa_create_region_for_cfg(struct cdns_pcie_rc *rc)
{
	struct cdns_pcie *pcie = &rc->pcie;
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(rc);
	struct resource *cfg_res = rc->cfg_res;
	struct resource_entry *entry;
	u64 cpu_addr = cfg_res->start;
	u32 addr0, addr1, desc1;
	int busnr = 0;

	entry = resource_list_first_type(&bridge->windows, IORESOURCE_BUS);
	if (entry)
		busnr = entry->res->start;

	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_TAG_MANAGEMENT, 0x01000000);
	/*
	 * Reserve region 0 for PCI configure space accesses:
	 * OB_REGION_PCI_ADDR0 and OB_REGION_DESC0 are updated dynamically by
	 * cdns_pci_map_bus(), other region registers are set here once for all
	 */
	desc1 = CDNS_PCIE_HPA_AT_OB_REGION_DESC1_BUS(busnr);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR1(0), 0x0);
	/* Type-1 CFG */
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_DESC0(0), 0x05000000);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_DESC1(0), desc1);

	addr0 = CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR0_NBITS(12) |
		(lower_32_bits(cpu_addr) & GENMASK(31, 8));
	addr1 = upper_32_bits(cpu_addr);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR0(0), addr0);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR1(0), addr1);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_CTRL0(0), 0x06000000);
}

static int cdns_pcie_hpa_host_init_address_translation(struct cdns_pcie_rc *rc)
{
	struct cdns_pcie *pcie = &rc->pcie;
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(rc);
	struct resource_entry *entry;
	int r = 0, busnr = 0;

	if (!rc->ecam_supported)
		cdns_pcie_hpa_create_region_for_cfg(rc);

	entry = resource_list_first_type(&bridge->windows, IORESOURCE_BUS);
	if (entry)
		busnr = entry->res->start;

	r++;
	if (pcie->msg_res) {
		cdns_pcie_hpa_set_outbound_region_for_normal_msg(pcie, busnr, 0, r,
								 pcie->msg_res->start);

		r++;
	}
	resource_list_for_each_entry(entry, &bridge->windows) {
		struct resource *res = entry->res;
		u64 pci_addr = res->start - entry->offset;

		if (resource_type(res) == IORESOURCE_IO)
			cdns_pcie_hpa_set_outbound_region(pcie, busnr, 0, r,
							  true,
							  pci_pio_to_address(res->start),
							  pci_addr,
							  resource_size(res));
		else
			cdns_pcie_hpa_set_outbound_region(pcie, busnr, 0, r,
							  false,
							  res->start,
							  pci_addr,
							  resource_size(res));

		r++;
	}

	if (rc->no_inbound_map)
		return 0;
	else
		return cdns_pcie_host_map_dma_ranges(rc, cdns_pcie_hpa_host_bar_ib_config);
}

static int cdns_pcie_hpa_host_init(struct cdns_pcie_rc *rc)
{
	int err;

	err = cdns_pcie_hpa_host_init_root_port(rc);
	if (err)
		return err;

	return cdns_pcie_hpa_host_init_address_translation(rc);
}

int cdns_pcie_hpa_host_link_setup(struct cdns_pcie_rc *rc)
{
	struct cdns_pcie *pcie = &rc->pcie;
	struct device *dev = rc->pcie.dev;
	int ret;

	if (rc->quirk_detect_quiet_flag)
		cdns_pcie_hpa_detect_quiet_min_delay_set(&rc->pcie);

	cdns_pcie_hpa_host_enable_ptm_response(pcie);

	ret = cdns_pcie_start_link(pcie);
	if (ret) {
		dev_err(dev, "Failed to start link\n");
		return ret;
	}

	ret = cdns_pcie_host_wait_for_link(pcie, cdns_pcie_hpa_link_up);
	if (ret)
		dev_dbg(dev, "PCIe link never came up\n");

	return ret;
}
EXPORT_SYMBOL_GPL(cdns_pcie_hpa_host_link_setup);

int cdns_pcie_hpa_host_setup(struct cdns_pcie_rc *rc)
{
	struct device *dev = rc->pcie.dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct pci_host_bridge *bridge;
	enum   cdns_pcie_rp_bar bar;
	struct cdns_pcie *pcie;
	struct resource *res;
	int    ret;

	bridge = pci_host_bridge_from_priv(rc);
	if (!bridge)
		return -ENOMEM;

	pcie = &rc->pcie;
	pcie->is_rc = true;

	if (!pcie->reg_base) {
		pcie->reg_base = devm_platform_ioremap_resource_byname(pdev, "reg");
		if (IS_ERR(pcie->reg_base)) {
			dev_err(dev, "missing \"reg\"\n");
			return PTR_ERR(pcie->reg_base);
		}
	}

	/* ECAM config space is remapped at glue layer */
	if (!rc->cfg_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg");
		rc->cfg_base = devm_pci_remap_cfg_resource(dev, res);
		if (IS_ERR(rc->cfg_base))
			return PTR_ERR(rc->cfg_base);
		rc->cfg_res = res;
	}

	/* Put EROM Bar aperture to 0 */
	cdns_pcie_hpa_writel(pcie, REG_BANK_IP_CFG_CTRL_REG, CDNS_PCIE_EROM, 0x0);

	ret = cdns_pcie_hpa_host_link_setup(rc);
	if (ret)
		return ret;

	for (bar = RP_BAR0; bar <= RP_NO_BAR; bar++)
		rc->avail_ib_bar[bar] = true;

	ret = cdns_pcie_hpa_host_init(rc);
	if (ret)
		return ret;

	if (!bridge->ops)
		bridge->ops = &cdns_pcie_hpa_host_ops;

	return pci_host_probe(bridge);
}
EXPORT_SYMBOL_GPL(cdns_pcie_hpa_host_setup);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cadence PCIe host controller driver");
