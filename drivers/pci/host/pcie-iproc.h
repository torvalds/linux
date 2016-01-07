/*
 * Copyright (C) 2014-2015 Broadcom Corporation
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

#ifndef _PCIE_IPROC_H
#define _PCIE_IPROC_H

/**
 * iProc PCIe interface type
 *
 * PAXB is the wrapper used in root complex that can be connected to an
 * external endpoint device.
 *
 * PAXC is the wrapper used in root complex dedicated for internal emulated
 * endpoint devices.
 */
enum iproc_pcie_type {
	IPROC_PCIE_PAXB = 0,
	IPROC_PCIE_PAXC,
};

/**
 * iProc PCIe outbound mapping
 * @set_oarr_size: indicates the OARR size bit needs to be set
 * @axi_offset: offset from the AXI address to the internal address used by
 * the iProc PCIe core
 * @window_size: outbound window size
 */
struct iproc_pcie_ob {
	bool set_oarr_size;
	resource_size_t axi_offset;
	resource_size_t window_size;
};

struct iproc_msi;

/**
 * iProc PCIe device
 *
 * @dev: pointer to device data structure
 * @type: iProc PCIe interface type
 * @reg_offsets: register offsets
 * @base: PCIe host controller I/O register base
 * @base_addr: PCIe host controller register base physical address
 * @sysdata: Per PCI controller data (ARM-specific)
 * @root_bus: pointer to root bus
 * @phy: optional PHY device that controls the Serdes
 * @map_irq: function callback to map interrupts
 * @need_ob_cfg: indicates SW needs to configure the outbound mapping window
 * @ob: outbound mapping parameters
 * @msi: MSI data
 */
struct iproc_pcie {
	struct device *dev;
	enum iproc_pcie_type type;
	const u16 *reg_offsets;
	void __iomem *base;
	phys_addr_t base_addr;
#ifdef CONFIG_ARM
	struct pci_sys_data sysdata;
#endif
	struct pci_bus *root_bus;
	struct phy *phy;
	int (*map_irq)(const struct pci_dev *, u8, u8);
	bool need_ob_cfg;
	struct iproc_pcie_ob ob;
	struct iproc_msi *msi;
};

int iproc_pcie_setup(struct iproc_pcie *pcie, struct list_head *res);
int iproc_pcie_remove(struct iproc_pcie *pcie);

#ifdef CONFIG_PCIE_IPROC_MSI
int iproc_msi_init(struct iproc_pcie *pcie, struct device_node *node);
void iproc_msi_exit(struct iproc_pcie *pcie);
#else
static inline int iproc_msi_init(struct iproc_pcie *pcie,
				 struct device_node *node)
{
	return -ENODEV;
}
static inline void iproc_msi_exit(struct iproc_pcie *pcie)
{
}
#endif

#endif /* _PCIE_IPROC_H */
