/*
 * Copyright (C) 2014 Hauke Mehrtens <hauke@hauke-m.de>
 * Copyright (C) 2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/mbus.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>

#include "pcie-iproc.h"

#define CLK_CONTROL_OFFSET           0x000
#define EP_PERST_SOURCE_SELECT_SHIFT 2
#define EP_PERST_SOURCE_SELECT       BIT(EP_PERST_SOURCE_SELECT_SHIFT)
#define EP_MODE_SURVIVE_PERST_SHIFT  1
#define EP_MODE_SURVIVE_PERST        BIT(EP_MODE_SURVIVE_PERST_SHIFT)
#define RC_PCIE_RST_OUTPUT_SHIFT     0
#define RC_PCIE_RST_OUTPUT           BIT(RC_PCIE_RST_OUTPUT_SHIFT)

#define CFG_IND_ADDR_OFFSET          0x120
#define CFG_IND_ADDR_MASK            0x00001ffc

#define CFG_IND_DATA_OFFSET          0x124

#define CFG_ADDR_OFFSET              0x1f8
#define CFG_ADDR_BUS_NUM_SHIFT       20
#define CFG_ADDR_BUS_NUM_MASK        0x0ff00000
#define CFG_ADDR_DEV_NUM_SHIFT       15
#define CFG_ADDR_DEV_NUM_MASK        0x000f8000
#define CFG_ADDR_FUNC_NUM_SHIFT      12
#define CFG_ADDR_FUNC_NUM_MASK       0x00007000
#define CFG_ADDR_REG_NUM_SHIFT       2
#define CFG_ADDR_REG_NUM_MASK        0x00000ffc
#define CFG_ADDR_CFG_TYPE_SHIFT      0
#define CFG_ADDR_CFG_TYPE_MASK       0x00000003

#define CFG_DATA_OFFSET              0x1fc

#define SYS_RC_INTX_EN               0x330
#define SYS_RC_INTX_MASK             0xf

#define PCIE_LINK_STATUS_OFFSET      0xf0c
#define PCIE_PHYLINKUP_SHIFT         3
#define PCIE_PHYLINKUP               BIT(PCIE_PHYLINKUP_SHIFT)
#define PCIE_DL_ACTIVE_SHIFT         2
#define PCIE_DL_ACTIVE               BIT(PCIE_DL_ACTIVE_SHIFT)

#define OARR_VALID_SHIFT             0
#define OARR_VALID                   BIT(OARR_VALID_SHIFT)
#define OARR_SIZE_CFG_SHIFT          1
#define OARR_SIZE_CFG                BIT(OARR_SIZE_CFG_SHIFT)

#define OARR_LO(window)              (0xd20 + (window) * 8)
#define OARR_HI(window)              (0xd24 + (window) * 8)
#define OMAP_LO(window)              (0xd40 + (window) * 8)
#define OMAP_HI(window)              (0xd44 + (window) * 8)

#define MAX_NUM_OB_WINDOWS           2

static inline struct iproc_pcie *iproc_data(struct pci_bus *bus)
{
	struct iproc_pcie *pcie;
#ifdef CONFIG_ARM
	struct pci_sys_data *sys = bus->sysdata;

	pcie = sys->private_data;
#else
	pcie = bus->sysdata;
#endif
	return pcie;
}

/**
 * Note access to the configuration registers are protected at the higher layer
 * by 'pci_lock' in drivers/pci/access.c
 */
static void __iomem *iproc_pcie_map_cfg_bus(struct pci_bus *bus,
					    unsigned int devfn,
					    int where)
{
	struct iproc_pcie *pcie = iproc_data(bus);
	unsigned slot = PCI_SLOT(devfn);
	unsigned fn = PCI_FUNC(devfn);
	unsigned busno = bus->number;
	u32 val;

	/* root complex access */
	if (busno == 0) {
		if (slot >= 1)
			return NULL;
		writel(where & CFG_IND_ADDR_MASK,
		       pcie->base + CFG_IND_ADDR_OFFSET);
		return (pcie->base + CFG_IND_DATA_OFFSET);
	}

	if (fn > 1)
		return NULL;

	/* EP device access */
	val = (busno << CFG_ADDR_BUS_NUM_SHIFT) |
		(slot << CFG_ADDR_DEV_NUM_SHIFT) |
		(fn << CFG_ADDR_FUNC_NUM_SHIFT) |
		(where & CFG_ADDR_REG_NUM_MASK) |
		(1 & CFG_ADDR_CFG_TYPE_MASK);
	writel(val, pcie->base + CFG_ADDR_OFFSET);

	return (pcie->base + CFG_DATA_OFFSET);
}

static struct pci_ops iproc_pcie_ops = {
	.map_bus = iproc_pcie_map_cfg_bus,
	.read = pci_generic_config_read32,
	.write = pci_generic_config_write32,
};

static void iproc_pcie_reset(struct iproc_pcie *pcie)
{
	u32 val;

	/*
	 * Select perst_b signal as reset source. Put the device into reset,
	 * and then bring it out of reset
	 */
	val = readl(pcie->base + CLK_CONTROL_OFFSET);
	val &= ~EP_PERST_SOURCE_SELECT & ~EP_MODE_SURVIVE_PERST &
		~RC_PCIE_RST_OUTPUT;
	writel(val, pcie->base + CLK_CONTROL_OFFSET);
	udelay(250);

	val |= RC_PCIE_RST_OUTPUT;
	writel(val, pcie->base + CLK_CONTROL_OFFSET);
	msleep(100);
}

static int iproc_pcie_check_link(struct iproc_pcie *pcie, struct pci_bus *bus)
{
	u8 hdr_type;
	u32 link_ctrl, class, val;
	u16 pos, link_status;
	bool link_is_active = false;

	val = readl(pcie->base + PCIE_LINK_STATUS_OFFSET);
	if (!(val & PCIE_PHYLINKUP) || !(val & PCIE_DL_ACTIVE)) {
		dev_err(pcie->dev, "PHY or data link is INACTIVE!\n");
		return -ENODEV;
	}

	/* make sure we are not in EP mode */
	pci_bus_read_config_byte(bus, 0, PCI_HEADER_TYPE, &hdr_type);
	if ((hdr_type & 0x7f) != PCI_HEADER_TYPE_BRIDGE) {
		dev_err(pcie->dev, "in EP mode, hdr=%#02x\n", hdr_type);
		return -EFAULT;
	}

	/* force class to PCI_CLASS_BRIDGE_PCI (0x0604) */
#define PCI_BRIDGE_CTRL_REG_OFFSET 0x43c
#define PCI_CLASS_BRIDGE_MASK      0xffff00
#define PCI_CLASS_BRIDGE_SHIFT     8
	pci_bus_read_config_dword(bus, 0, PCI_BRIDGE_CTRL_REG_OFFSET, &class);
	class &= ~PCI_CLASS_BRIDGE_MASK;
	class |= (PCI_CLASS_BRIDGE_PCI << PCI_CLASS_BRIDGE_SHIFT);
	pci_bus_write_config_dword(bus, 0, PCI_BRIDGE_CTRL_REG_OFFSET, class);

	/* check link status to see if link is active */
	pos = pci_bus_find_capability(bus, 0, PCI_CAP_ID_EXP);
	pci_bus_read_config_word(bus, 0, pos + PCI_EXP_LNKSTA, &link_status);
	if (link_status & PCI_EXP_LNKSTA_NLW)
		link_is_active = true;

	if (!link_is_active) {
		/* try GEN 1 link speed */
#define PCI_LINK_STATUS_CTRL_2_OFFSET 0x0dc
#define PCI_TARGET_LINK_SPEED_MASK    0xf
#define PCI_TARGET_LINK_SPEED_GEN2    0x2
#define PCI_TARGET_LINK_SPEED_GEN1    0x1
		pci_bus_read_config_dword(bus, 0,
					  PCI_LINK_STATUS_CTRL_2_OFFSET,
					  &link_ctrl);
		if ((link_ctrl & PCI_TARGET_LINK_SPEED_MASK) ==
		    PCI_TARGET_LINK_SPEED_GEN2) {
			link_ctrl &= ~PCI_TARGET_LINK_SPEED_MASK;
			link_ctrl |= PCI_TARGET_LINK_SPEED_GEN1;
			pci_bus_write_config_dword(bus, 0,
					   PCI_LINK_STATUS_CTRL_2_OFFSET,
					   link_ctrl);
			msleep(100);

			pos = pci_bus_find_capability(bus, 0, PCI_CAP_ID_EXP);
			pci_bus_read_config_word(bus, 0, pos + PCI_EXP_LNKSTA,
						 &link_status);
			if (link_status & PCI_EXP_LNKSTA_NLW)
				link_is_active = true;
		}
	}

	dev_info(pcie->dev, "link: %s\n", link_is_active ? "UP" : "DOWN");

	return link_is_active ? 0 : -ENODEV;
}

static void iproc_pcie_enable(struct iproc_pcie *pcie)
{
	writel(SYS_RC_INTX_MASK, pcie->base + SYS_RC_INTX_EN);
}

/**
 * Some iProc SoCs require the SW to configure the outbound address mapping
 *
 * Outbound address translation:
 *
 * iproc_pcie_address = axi_address - axi_offset
 * OARR = iproc_pcie_address
 * OMAP = pci_addr
 *
 * axi_addr -> iproc_pcie_address -> OARR -> OMAP -> pci_address
 */
static int iproc_pcie_setup_ob(struct iproc_pcie *pcie, u64 axi_addr,
			       u64 pci_addr, resource_size_t size)
{
	struct iproc_pcie_ob *ob = &pcie->ob;
	unsigned i;
	u64 max_size = (u64)ob->window_size * MAX_NUM_OB_WINDOWS;
	u64 remainder;

	if (size > max_size) {
		dev_err(pcie->dev,
			"res size 0x%pap exceeds max supported size 0x%llx\n",
			&size, max_size);
		return -EINVAL;
	}

	div64_u64_rem(size, ob->window_size, &remainder);
	if (remainder) {
		dev_err(pcie->dev,
			"res size %pap needs to be multiple of window size %pap\n",
			&size, &ob->window_size);
		return -EINVAL;
	}

	if (axi_addr < ob->axi_offset) {
		dev_err(pcie->dev,
			"axi address %pap less than offset %pap\n",
			&axi_addr, &ob->axi_offset);
		return -EINVAL;
	}

	/*
	 * Translate the AXI address to the internal address used by the iProc
	 * PCIe core before programming the OARR
	 */
	axi_addr -= ob->axi_offset;

	for (i = 0; i < MAX_NUM_OB_WINDOWS; i++) {
		writel(lower_32_bits(axi_addr) | OARR_VALID |
		       (ob->set_oarr_size ? 1 : 0), pcie->base + OARR_LO(i));
		writel(upper_32_bits(axi_addr), pcie->base + OARR_HI(i));
		writel(lower_32_bits(pci_addr), pcie->base + OMAP_LO(i));
		writel(upper_32_bits(pci_addr), pcie->base + OMAP_HI(i));

		size -= ob->window_size;
		if (size == 0)
			break;

		axi_addr += ob->window_size;
		pci_addr += ob->window_size;
	}

	return 0;
}

static int iproc_pcie_map_ranges(struct iproc_pcie *pcie,
				 struct list_head *resources)
{
	struct resource_entry *window;
	int ret;

	resource_list_for_each_entry(window, resources) {
		struct resource *res = window->res;
		u64 res_type = resource_type(res);

		switch (res_type) {
		case IORESOURCE_IO:
		case IORESOURCE_BUS:
			break;
		case IORESOURCE_MEM:
			ret = iproc_pcie_setup_ob(pcie, res->start,
						  res->start - window->offset,
						  resource_size(res));
			if (ret)
				return ret;
			break;
		default:
			dev_err(pcie->dev, "invalid resource %pR\n", res);
			return -EINVAL;
		}
	}

	return 0;
}

int iproc_pcie_setup(struct iproc_pcie *pcie, struct list_head *res)
{
	int ret;
	void *sysdata;
	struct pci_bus *bus;

	if (!pcie || !pcie->dev || !pcie->base)
		return -EINVAL;

	ret = phy_init(pcie->phy);
	if (ret) {
		dev_err(pcie->dev, "unable to initialize PCIe PHY\n");
		return ret;
	}

	ret = phy_power_on(pcie->phy);
	if (ret) {
		dev_err(pcie->dev, "unable to power on PCIe PHY\n");
		goto err_exit_phy;
	}

	iproc_pcie_reset(pcie);

	if (pcie->need_ob_cfg) {
		ret = iproc_pcie_map_ranges(pcie, res);
		if (ret) {
			dev_err(pcie->dev, "map failed\n");
			goto err_power_off_phy;
		}
	}

#ifdef CONFIG_ARM
	pcie->sysdata.private_data = pcie;
	sysdata = &pcie->sysdata;
#else
	sysdata = pcie;
#endif

	bus = pci_create_root_bus(pcie->dev, 0, &iproc_pcie_ops, sysdata, res);
	if (!bus) {
		dev_err(pcie->dev, "unable to create PCI root bus\n");
		ret = -ENOMEM;
		goto err_power_off_phy;
	}
	pcie->root_bus = bus;

	ret = iproc_pcie_check_link(pcie, bus);
	if (ret) {
		dev_err(pcie->dev, "no PCIe EP device detected\n");
		goto err_rm_root_bus;
	}

	iproc_pcie_enable(pcie);

	pci_scan_child_bus(bus);
	pci_assign_unassigned_bus_resources(bus);
	pci_fixup_irqs(pci_common_swizzle, pcie->map_irq);
	pci_bus_add_devices(bus);

	return 0;

err_rm_root_bus:
	pci_stop_root_bus(bus);
	pci_remove_root_bus(bus);

err_power_off_phy:
	phy_power_off(pcie->phy);
err_exit_phy:
	phy_exit(pcie->phy);
	return ret;
}
EXPORT_SYMBOL(iproc_pcie_setup);

int iproc_pcie_remove(struct iproc_pcie *pcie)
{
	pci_stop_root_bus(pcie->root_bus);
	pci_remove_root_bus(pcie->root_bus);

	phy_power_off(pcie->phy);
	phy_exit(pcie->phy);

	return 0;
}
EXPORT_SYMBOL(iproc_pcie_remove);

MODULE_AUTHOR("Ray Jui <rjui@broadcom.com>");
MODULE_DESCRIPTION("Broadcom iPROC PCIe common driver");
MODULE_LICENSE("GPL v2");
