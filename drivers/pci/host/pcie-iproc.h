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
	IPROC_PCIE_PAXB_BCMA = 0,
	IPROC_PCIE_PAXB,
	IPROC_PCIE_PAXB_V2,
	IPROC_PCIE_PAXC,
	IPROC_PCIE_PAXC_V2,
};

/**
 * iProc PCIe outbound mapping
 * @axi_offset: offset from the AXI address to the internal address used by
 * the iProc PCIe core
 * @nr_windows: total number of supported outbound mapping windows
 */
struct iproc_pcie_ob {
	resource_size_t axi_offset;
	unsigned int nr_windows;
};

/**
 * iProc PCIe inbound mapping
 * @nr_regions: total number of supported inbound mapping regions
 */
struct iproc_pcie_ib {
	unsigned int nr_regions;
};

struct iproc_pcie_ob_map;
struct iproc_pcie_ib_map;
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
 * @ep_is_internal: indicates an internal emulated endpoint device is connected
 * @has_apb_err_disable: indicates the controller can be configured to prevent
 * unsupported request from being forwarded as an APB bus error
 *
 * @need_ob_cfg: indicates SW needs to configure the outbound mapping window
 * @ob: outbound mapping related parameters
 * @ob_map: outbound mapping related parameters specific to the controller
 *
 * @ib: inbound mapping related parameters
 * @ib_map: outbound mapping region related parameters
 *
 * @need_msi_steer: indicates additional configuration of the iProc PCIe
 * controller is required to steer MSI writes to external interrupt controller
 * @msi: MSI data
 */
struct iproc_pcie {
	struct device *dev;
	enum iproc_pcie_type type;
	u16 *reg_offsets;
	void __iomem *base;
	phys_addr_t base_addr;
#ifdef CONFIG_ARM
	struct pci_sys_data sysdata;
#endif
	struct pci_bus *root_bus;
	struct phy *phy;
	int (*map_irq)(const struct pci_dev *, u8, u8);
	bool ep_is_internal;
	bool has_apb_err_disable;

	bool need_ob_cfg;
	struct iproc_pcie_ob ob;
	const struct iproc_pcie_ob_map *ob_map;

	struct iproc_pcie_ib ib;
	const struct iproc_pcie_ib_map *ib_map;

	bool need_msi_steer;
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
