/*
 * Synopsys Designware PCIe host controller driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

struct pcie_port_info {
	u32		cfg0_size;
	u32		cfg1_size;
	u32		io_size;
	u32		mem_size;
	phys_addr_t	io_bus_addr;
	phys_addr_t	mem_bus_addr;
};

struct pcie_port {
	struct device		*dev;
	u8			root_bus_nr;
	void __iomem		*dbi_base;
	u64			cfg0_base;
	void __iomem		*va_cfg0_base;
	u64			cfg1_base;
	void __iomem		*va_cfg1_base;
	u64			io_base;
	u64			mem_base;
	spinlock_t		conf_lock;
	struct resource		cfg;
	struct resource		io;
	struct resource		mem;
	struct pcie_port_info	config;
	int			irq;
	u32			lanes;
	struct pcie_host_ops	*ops;
};

struct pcie_host_ops {
	void (*readl_rc)(struct pcie_port *pp,
			void __iomem *dbi_base, u32 *val);
	void (*writel_rc)(struct pcie_port *pp,
			u32 val, void __iomem *dbi_base);
	int (*rd_own_conf)(struct pcie_port *pp, int where, int size, u32 *val);
	int (*wr_own_conf)(struct pcie_port *pp, int where, int size, u32 val);
	int (*link_up)(struct pcie_port *pp);
	void (*host_init)(struct pcie_port *pp);
};

extern unsigned long global_io_offset;

int cfg_read(void __iomem *addr, int where, int size, u32 *val);
int cfg_write(void __iomem *addr, int where, int size, u32 val);
int dw_pcie_wr_own_conf(struct pcie_port *pp, int where, int size, u32 val);
int dw_pcie_rd_own_conf(struct pcie_port *pp, int where, int size, u32 *val);
int dw_pcie_link_up(struct pcie_port *pp);
void dw_pcie_setup_rc(struct pcie_port *pp);
int dw_pcie_host_init(struct pcie_port *pp);
int dw_pcie_setup(int nr, struct pci_sys_data *sys);
struct pci_bus *dw_pcie_scan_bus(int nr, struct pci_sys_data *sys);
int dw_pcie_map_irq(const struct pci_dev *dev, u8 slot, u8 pin);
