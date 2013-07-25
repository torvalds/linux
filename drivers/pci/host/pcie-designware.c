/*
 * PCIe host controller driver for Samsung EXYNOS SoCs
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

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/types.h>

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
	u8			controller;
	u8			root_bus_nr;
	void __iomem		*dbi_base;
	void __iomem		*elbi_base;
	void __iomem		*phy_base;
	void __iomem		*purple_base;
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
	struct clk		*clk;
	struct clk		*bus_clk;
	int			irq;
	int			reset_gpio;
};

/*
 * Exynos PCIe IP consists of Synopsys specific part and Exynos
 * specific part. Only core block is a Synopsys designware part;
 * other parts are Exynos specific.
 */

/* Synopsis specific PCIE configuration registers */
#define PCIE_PORT_LINK_CONTROL		0x710
#define PORT_LINK_MODE_MASK		(0x3f << 16)
#define PORT_LINK_MODE_4_LANES		(0x7 << 16)

#define PCIE_LINK_WIDTH_SPEED_CONTROL	0x80C
#define PORT_LOGIC_SPEED_CHANGE		(0x1 << 17)
#define PORT_LOGIC_LINK_WIDTH_MASK	(0x1ff << 8)
#define PORT_LOGIC_LINK_WIDTH_4_LANES	(0x7 << 8)

#define PCIE_MSI_ADDR_LO		0x820
#define PCIE_MSI_ADDR_HI		0x824
#define PCIE_MSI_INTR0_ENABLE		0x828
#define PCIE_MSI_INTR0_MASK		0x82C
#define PCIE_MSI_INTR0_STATUS		0x830

#define PCIE_ATU_VIEWPORT		0x900
#define PCIE_ATU_REGION_INBOUND		(0x1 << 31)
#define PCIE_ATU_REGION_OUTBOUND	(0x0 << 31)
#define PCIE_ATU_REGION_INDEX1		(0x1 << 0)
#define PCIE_ATU_REGION_INDEX0		(0x0 << 0)
#define PCIE_ATU_CR1			0x904
#define PCIE_ATU_TYPE_MEM		(0x0 << 0)
#define PCIE_ATU_TYPE_IO		(0x2 << 0)
#define PCIE_ATU_TYPE_CFG0		(0x4 << 0)
#define PCIE_ATU_TYPE_CFG1		(0x5 << 0)
#define PCIE_ATU_CR2			0x908
#define PCIE_ATU_ENABLE			(0x1 << 31)
#define PCIE_ATU_BAR_MODE_ENABLE	(0x1 << 30)
#define PCIE_ATU_LOWER_BASE		0x90C
#define PCIE_ATU_UPPER_BASE		0x910
#define PCIE_ATU_LIMIT			0x914
#define PCIE_ATU_LOWER_TARGET		0x918
#define PCIE_ATU_BUS(x)			(((x) & 0xff) << 24)
#define PCIE_ATU_DEV(x)			(((x) & 0x1f) << 19)
#define PCIE_ATU_FUNC(x)		(((x) & 0x7) << 16)
#define PCIE_ATU_UPPER_TARGET		0x91C

/* Exynos specific PCIE configuration registers */

/* PCIe ELBI registers */
#define PCIE_IRQ_PULSE			0x000
#define IRQ_INTA_ASSERT			(0x1 << 0)
#define IRQ_INTB_ASSERT			(0x1 << 2)
#define IRQ_INTC_ASSERT			(0x1 << 4)
#define IRQ_INTD_ASSERT			(0x1 << 6)
#define PCIE_IRQ_LEVEL			0x004
#define PCIE_IRQ_SPECIAL		0x008
#define PCIE_IRQ_EN_PULSE		0x00c
#define PCIE_IRQ_EN_LEVEL		0x010
#define PCIE_IRQ_EN_SPECIAL		0x014
#define PCIE_PWR_RESET			0x018
#define PCIE_CORE_RESET			0x01c
#define PCIE_CORE_RESET_ENABLE		(0x1 << 0)
#define PCIE_STICKY_RESET		0x020
#define PCIE_NONSTICKY_RESET		0x024
#define PCIE_APP_INIT_RESET		0x028
#define PCIE_APP_LTSSM_ENABLE		0x02c
#define PCIE_ELBI_RDLH_LINKUP		0x064
#define PCIE_ELBI_LTSSM_ENABLE		0x1
#define PCIE_ELBI_SLV_AWMISC		0x11c
#define PCIE_ELBI_SLV_ARMISC		0x120
#define PCIE_ELBI_SLV_DBI_ENABLE	(0x1 << 21)

/* PCIe Purple registers */
#define PCIE_PHY_GLOBAL_RESET		0x000
#define PCIE_PHY_COMMON_RESET		0x004
#define PCIE_PHY_CMN_REG		0x008
#define PCIE_PHY_MAC_RESET		0x00c
#define PCIE_PHY_PLL_LOCKED		0x010
#define PCIE_PHY_TRSVREG_RESET		0x020
#define PCIE_PHY_TRSV_RESET		0x024

/* PCIe PHY registers */
#define PCIE_PHY_IMPEDANCE		0x004
#define PCIE_PHY_PLL_DIV_0		0x008
#define PCIE_PHY_PLL_BIAS		0x00c
#define PCIE_PHY_DCC_FEEDBACK		0x014
#define PCIE_PHY_PLL_DIV_1		0x05c
#define PCIE_PHY_TRSV0_EMP_LVL		0x084
#define PCIE_PHY_TRSV0_DRV_LVL		0x088
#define PCIE_PHY_TRSV0_RXCDR		0x0ac
#define PCIE_PHY_TRSV0_LVCC		0x0dc
#define PCIE_PHY_TRSV1_EMP_LVL		0x144
#define PCIE_PHY_TRSV1_RXCDR		0x16c
#define PCIE_PHY_TRSV1_LVCC		0x19c
#define PCIE_PHY_TRSV2_EMP_LVL		0x204
#define PCIE_PHY_TRSV2_RXCDR		0x22c
#define PCIE_PHY_TRSV2_LVCC		0x25c
#define PCIE_PHY_TRSV3_EMP_LVL		0x2c4
#define PCIE_PHY_TRSV3_RXCDR		0x2ec
#define PCIE_PHY_TRSV3_LVCC		0x31c

static struct hw_pci exynos_pci;

static inline struct pcie_port *sys_to_pcie(struct pci_sys_data *sys)
{
	return sys->private_data;
}

static inline int cfg_read(void *addr, int where, int size, u32 *val)
{
	*val = readl(addr);

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 3))) & 0xffff;
	else if (size != 4)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}

static inline int cfg_write(void *addr, int where, int size, u32 val)
{
	if (size == 4)
		writel(val, addr);
	else if (size == 2)
		writew(val, addr + (where & 2));
	else if (size == 1)
		writeb(val, addr + (where & 3));
	else
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}

static void exynos_pcie_sideband_dbi_w_mode(struct pcie_port *pp, bool on)
{
	u32 val;

	if (on) {
		val = readl(pp->elbi_base + PCIE_ELBI_SLV_AWMISC);
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
		writel(val, pp->elbi_base + PCIE_ELBI_SLV_AWMISC);
	} else {
		val = readl(pp->elbi_base + PCIE_ELBI_SLV_AWMISC);
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
		writel(val, pp->elbi_base + PCIE_ELBI_SLV_AWMISC);
	}
}

static void exynos_pcie_sideband_dbi_r_mode(struct pcie_port *pp, bool on)
{
	u32 val;

	if (on) {
		val = readl(pp->elbi_base + PCIE_ELBI_SLV_ARMISC);
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
		writel(val, pp->elbi_base + PCIE_ELBI_SLV_ARMISC);
	} else {
		val = readl(pp->elbi_base + PCIE_ELBI_SLV_ARMISC);
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
		writel(val, pp->elbi_base + PCIE_ELBI_SLV_ARMISC);
	}
}

static inline void readl_rc(struct pcie_port *pp, void *dbi_base, u32 *val)
{
	exynos_pcie_sideband_dbi_r_mode(pp, true);
	*val = readl(dbi_base);
	exynos_pcie_sideband_dbi_r_mode(pp, false);
	return;
}

static inline void writel_rc(struct pcie_port *pp, u32 val, void *dbi_base)
{
	exynos_pcie_sideband_dbi_w_mode(pp, true);
	writel(val, dbi_base);
	exynos_pcie_sideband_dbi_w_mode(pp, false);
	return;
}

static int exynos_pcie_rd_own_conf(struct pcie_port *pp, int where, int size,
				u32 *val)
{
	int ret;

	exynos_pcie_sideband_dbi_r_mode(pp, true);
	ret = cfg_read(pp->dbi_base + (where & ~0x3), where, size, val);
	exynos_pcie_sideband_dbi_r_mode(pp, false);
	return ret;
}

static int exynos_pcie_wr_own_conf(struct pcie_port *pp, int where, int size,
				u32 val)
{
	int ret;

	exynos_pcie_sideband_dbi_w_mode(pp, true);
	ret = cfg_write(pp->dbi_base + (where & ~0x3), where, size, val);
	exynos_pcie_sideband_dbi_w_mode(pp, false);
	return ret;
}

static void exynos_pcie_prog_viewport_cfg0(struct pcie_port *pp, u32 busdev)
{
	u32 val;
	void __iomem *dbi_base = pp->dbi_base;

	/* Program viewport 0 : OUTBOUND : CFG0 */
	val = PCIE_ATU_REGION_OUTBOUND | PCIE_ATU_REGION_INDEX0;
	writel_rc(pp, val, dbi_base + PCIE_ATU_VIEWPORT);
	writel_rc(pp, pp->cfg0_base, dbi_base + PCIE_ATU_LOWER_BASE);
	writel_rc(pp, (pp->cfg0_base >> 32), dbi_base + PCIE_ATU_UPPER_BASE);
	writel_rc(pp, pp->cfg0_base + pp->config.cfg0_size - 1,
			dbi_base + PCIE_ATU_LIMIT);
	writel_rc(pp, busdev, dbi_base + PCIE_ATU_LOWER_TARGET);
	writel_rc(pp, 0, dbi_base + PCIE_ATU_UPPER_TARGET);
	writel_rc(pp, PCIE_ATU_TYPE_CFG0, dbi_base + PCIE_ATU_CR1);
	val = PCIE_ATU_ENABLE;
	writel_rc(pp, val, dbi_base + PCIE_ATU_CR2);
}

static void exynos_pcie_prog_viewport_cfg1(struct pcie_port *pp, u32 busdev)
{
	u32 val;
	void __iomem *dbi_base = pp->dbi_base;

	/* Program viewport 1 : OUTBOUND : CFG1 */
	val = PCIE_ATU_REGION_OUTBOUND | PCIE_ATU_REGION_INDEX1;
	writel_rc(pp, val, dbi_base + PCIE_ATU_VIEWPORT);
	writel_rc(pp, PCIE_ATU_TYPE_CFG1, dbi_base + PCIE_ATU_CR1);
	val = PCIE_ATU_ENABLE;
	writel_rc(pp, val, dbi_base + PCIE_ATU_CR2);
	writel_rc(pp, pp->cfg1_base, dbi_base + PCIE_ATU_LOWER_BASE);
	writel_rc(pp, (pp->cfg1_base >> 32), dbi_base + PCIE_ATU_UPPER_BASE);
	writel_rc(pp, pp->cfg1_base + pp->config.cfg1_size - 1,
			dbi_base + PCIE_ATU_LIMIT);
	writel_rc(pp, busdev, dbi_base + PCIE_ATU_LOWER_TARGET);
	writel_rc(pp, 0, dbi_base + PCIE_ATU_UPPER_TARGET);
}

static void exynos_pcie_prog_viewport_mem_outbound(struct pcie_port *pp)
{
	u32 val;
	void __iomem *dbi_base = pp->dbi_base;

	/* Program viewport 0 : OUTBOUND : MEM */
	val = PCIE_ATU_REGION_OUTBOUND | PCIE_ATU_REGION_INDEX0;
	writel_rc(pp, val, dbi_base + PCIE_ATU_VIEWPORT);
	writel_rc(pp, PCIE_ATU_TYPE_MEM, dbi_base + PCIE_ATU_CR1);
	val = PCIE_ATU_ENABLE;
	writel_rc(pp, val, dbi_base + PCIE_ATU_CR2);
	writel_rc(pp, pp->mem_base, dbi_base + PCIE_ATU_LOWER_BASE);
	writel_rc(pp, (pp->mem_base >> 32), dbi_base + PCIE_ATU_UPPER_BASE);
	writel_rc(pp, pp->mem_base + pp->config.mem_size - 1,
			dbi_base + PCIE_ATU_LIMIT);
	writel_rc(pp, pp->config.mem_bus_addr,
			dbi_base + PCIE_ATU_LOWER_TARGET);
	writel_rc(pp, upper_32_bits(pp->config.mem_bus_addr),
			dbi_base + PCIE_ATU_UPPER_TARGET);
}

static void exynos_pcie_prog_viewport_io_outbound(struct pcie_port *pp)
{
	u32 val;
	void __iomem *dbi_base = pp->dbi_base;

	/* Program viewport 1 : OUTBOUND : IO */
	val = PCIE_ATU_REGION_OUTBOUND | PCIE_ATU_REGION_INDEX1;
	writel_rc(pp, val, dbi_base + PCIE_ATU_VIEWPORT);
	writel_rc(pp, PCIE_ATU_TYPE_IO, dbi_base + PCIE_ATU_CR1);
	val = PCIE_ATU_ENABLE;
	writel_rc(pp, val, dbi_base + PCIE_ATU_CR2);
	writel_rc(pp, pp->io_base, dbi_base + PCIE_ATU_LOWER_BASE);
	writel_rc(pp, (pp->io_base >> 32), dbi_base + PCIE_ATU_UPPER_BASE);
	writel_rc(pp, pp->io_base + pp->config.io_size - 1,
			dbi_base + PCIE_ATU_LIMIT);
	writel_rc(pp, pp->config.io_bus_addr,
			dbi_base + PCIE_ATU_LOWER_TARGET);
	writel_rc(pp, upper_32_bits(pp->config.io_bus_addr),
			dbi_base + PCIE_ATU_UPPER_TARGET);
}

static int exynos_pcie_rd_other_conf(struct pcie_port *pp, struct pci_bus *bus,
		u32 devfn, int where, int size, u32 *val)
{
	int ret = PCIBIOS_SUCCESSFUL;
	u32 address, busdev;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));
	address = where & ~0x3;

	if (bus->parent->number == pp->root_bus_nr) {
		exynos_pcie_prog_viewport_cfg0(pp, busdev);
		ret = cfg_read(pp->va_cfg0_base + address, where, size, val);
		exynos_pcie_prog_viewport_mem_outbound(pp);
	} else {
		exynos_pcie_prog_viewport_cfg1(pp, busdev);
		ret = cfg_read(pp->va_cfg1_base + address, where, size, val);
		exynos_pcie_prog_viewport_io_outbound(pp);
	}

	return ret;
}

static int exynos_pcie_wr_other_conf(struct pcie_port *pp, struct pci_bus *bus,
		u32 devfn, int where, int size, u32 val)
{
	int ret = PCIBIOS_SUCCESSFUL;
	u32 address, busdev;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));
	address = where & ~0x3;

	if (bus->parent->number == pp->root_bus_nr) {
		exynos_pcie_prog_viewport_cfg0(pp, busdev);
		ret = cfg_write(pp->va_cfg0_base + address, where, size, val);
		exynos_pcie_prog_viewport_mem_outbound(pp);
	} else {
		exynos_pcie_prog_viewport_cfg1(pp, busdev);
		ret = cfg_write(pp->va_cfg1_base + address, where, size, val);
		exynos_pcie_prog_viewport_io_outbound(pp);
	}

	return ret;
}

static unsigned long global_io_offset;

static int exynos_pcie_setup(int nr, struct pci_sys_data *sys)
{
	struct pcie_port *pp;

	pp = sys_to_pcie(sys);

	if (!pp)
		return 0;

	if (global_io_offset < SZ_1M && pp->config.io_size > 0) {
		sys->io_offset = global_io_offset - pp->config.io_bus_addr;
		pci_ioremap_io(sys->io_offset, pp->io.start);
		global_io_offset += SZ_64K;
		pci_add_resource_offset(&sys->resources, &pp->io,
					sys->io_offset);
	}

	sys->mem_offset = pp->mem.start - pp->config.mem_bus_addr;
	pci_add_resource_offset(&sys->resources, &pp->mem, sys->mem_offset);

	return 1;
}

static int exynos_pcie_link_up(struct pcie_port *pp)
{
	u32 val = readl(pp->elbi_base + PCIE_ELBI_RDLH_LINKUP);

	if (val == PCIE_ELBI_LTSSM_ENABLE)
		return 1;

	return 0;
}

static int exynos_pcie_valid_config(struct pcie_port *pp,
				struct pci_bus *bus, int dev)
{
	/* If there is no link, then there is no device */
	if (bus->number != pp->root_bus_nr) {
		if (!exynos_pcie_link_up(pp))
			return 0;
	}

	/* access only one slot on each root port */
	if (bus->number == pp->root_bus_nr && dev > 0)
		return 0;

	/*
	 * do not read more than one device on the bus directly attached
	 * to RC's (Virtual Bridge's) DS side.
	 */
	if (bus->primary == pp->root_bus_nr && dev > 0)
		return 0;

	return 1;
}

static int exynos_pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
			int size, u32 *val)
{
	struct pcie_port *pp = sys_to_pcie(bus->sysdata);
	unsigned long flags;
	int ret;

	if (!pp) {
		BUG();
		return -EINVAL;
	}

	if (exynos_pcie_valid_config(pp, bus, PCI_SLOT(devfn)) == 0) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	spin_lock_irqsave(&pp->conf_lock, flags);
	if (bus->number != pp->root_bus_nr)
		ret = exynos_pcie_rd_other_conf(pp, bus, devfn,
						where, size, val);
	else
		ret = exynos_pcie_rd_own_conf(pp, where, size, val);
	spin_unlock_irqrestore(&pp->conf_lock, flags);

	return ret;
}

static int exynos_pcie_wr_conf(struct pci_bus *bus, u32 devfn,
			int where, int size, u32 val)
{
	struct pcie_port *pp = sys_to_pcie(bus->sysdata);
	unsigned long flags;
	int ret;

	if (!pp) {
		BUG();
		return -EINVAL;
	}

	if (exynos_pcie_valid_config(pp, bus, PCI_SLOT(devfn)) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	spin_lock_irqsave(&pp->conf_lock, flags);
	if (bus->number != pp->root_bus_nr)
		ret = exynos_pcie_wr_other_conf(pp, bus, devfn,
						where, size, val);
	else
		ret = exynos_pcie_wr_own_conf(pp, where, size, val);
	spin_unlock_irqrestore(&pp->conf_lock, flags);

	return ret;
}

static struct pci_ops exynos_pcie_ops = {
	.read = exynos_pcie_rd_conf,
	.write = exynos_pcie_wr_conf,
};

static struct pci_bus *exynos_pcie_scan_bus(int nr,
					struct pci_sys_data *sys)
{
	struct pci_bus *bus;
	struct pcie_port *pp = sys_to_pcie(sys);

	if (pp) {
		pp->root_bus_nr = sys->busnr;
		bus = pci_scan_root_bus(NULL, sys->busnr, &exynos_pcie_ops,
					sys, &sys->resources);
	} else {
		bus = NULL;
		BUG();
	}

	return bus;
}

static int exynos_pcie_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct pcie_port *pp = sys_to_pcie(dev->bus->sysdata);

	return pp->irq;
}

static struct hw_pci exynos_pci = {
	.setup		= exynos_pcie_setup,
	.scan		= exynos_pcie_scan_bus,
	.map_irq	= exynos_pcie_map_irq,
};

static void exynos_pcie_setup_rc(struct pcie_port *pp)
{
	struct pcie_port_info *config = &pp->config;
	void __iomem *dbi_base = pp->dbi_base;
	u32 val;
	u32 membase;
	u32 memlimit;

	/* set the number of lines as 4 */
	readl_rc(pp, dbi_base + PCIE_PORT_LINK_CONTROL, &val);
	val &= ~PORT_LINK_MODE_MASK;
	val |= PORT_LINK_MODE_4_LANES;
	writel_rc(pp, val, dbi_base + PCIE_PORT_LINK_CONTROL);

	/* set link width speed control register */
	readl_rc(pp, dbi_base + PCIE_LINK_WIDTH_SPEED_CONTROL, &val);
	val &= ~PORT_LOGIC_LINK_WIDTH_MASK;
	val |= PORT_LOGIC_LINK_WIDTH_4_LANES;
	writel_rc(pp, val, dbi_base + PCIE_LINK_WIDTH_SPEED_CONTROL);

	/* setup RC BARs */
	writel_rc(pp, 0x00000004, dbi_base + PCI_BASE_ADDRESS_0);
	writel_rc(pp, 0x00000004, dbi_base + PCI_BASE_ADDRESS_1);

	/* setup interrupt pins */
	readl_rc(pp, dbi_base + PCI_INTERRUPT_LINE, &val);
	val &= 0xffff00ff;
	val |= 0x00000100;
	writel_rc(pp, val, dbi_base + PCI_INTERRUPT_LINE);

	/* setup bus numbers */
	readl_rc(pp, dbi_base + PCI_PRIMARY_BUS, &val);
	val &= 0xff000000;
	val |= 0x00010100;
	writel_rc(pp, val, dbi_base + PCI_PRIMARY_BUS);

	/* setup memory base, memory limit */
	membase = ((u32)pp->mem_base & 0xfff00000) >> 16;
	memlimit = (config->mem_size + (u32)pp->mem_base) & 0xfff00000;
	val = memlimit | membase;
	writel_rc(pp, val, dbi_base + PCI_MEMORY_BASE);

	/* setup command register */
	readl_rc(pp, dbi_base + PCI_COMMAND, &val);
	val &= 0xffff0000;
	val |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_MASTER | PCI_COMMAND_SERR;
	writel_rc(pp, val, dbi_base + PCI_COMMAND);
}

static void exynos_pcie_assert_core_reset(struct pcie_port *pp)
{
	u32 val;
	void __iomem *elbi_base = pp->elbi_base;

	val = readl(elbi_base + PCIE_CORE_RESET);
	val &= ~PCIE_CORE_RESET_ENABLE;
	writel(val, elbi_base + PCIE_CORE_RESET);
	writel(0, elbi_base + PCIE_PWR_RESET);
	writel(0, elbi_base + PCIE_STICKY_RESET);
	writel(0, elbi_base + PCIE_NONSTICKY_RESET);
}

static void exynos_pcie_deassert_core_reset(struct pcie_port *pp)
{
	u32 val;
	void __iomem *elbi_base = pp->elbi_base;
	void __iomem *purple_base = pp->purple_base;

	val = readl(elbi_base + PCIE_CORE_RESET);
	val |= PCIE_CORE_RESET_ENABLE;
	writel(val, elbi_base + PCIE_CORE_RESET);
	writel(1, elbi_base + PCIE_STICKY_RESET);
	writel(1, elbi_base + PCIE_NONSTICKY_RESET);
	writel(1, elbi_base + PCIE_APP_INIT_RESET);
	writel(0, elbi_base + PCIE_APP_INIT_RESET);
	writel(1, purple_base + PCIE_PHY_MAC_RESET);
}

static void exynos_pcie_assert_phy_reset(struct pcie_port *pp)
{
	void __iomem *purple_base = pp->purple_base;

	writel(0, purple_base + PCIE_PHY_MAC_RESET);
	writel(1, purple_base + PCIE_PHY_GLOBAL_RESET);
}

static void exynos_pcie_deassert_phy_reset(struct pcie_port *pp)
{
	void __iomem *elbi_base = pp->elbi_base;
	void __iomem *purple_base = pp->purple_base;

	writel(0, purple_base + PCIE_PHY_GLOBAL_RESET);
	writel(1, elbi_base + PCIE_PWR_RESET);
	writel(0, purple_base + PCIE_PHY_COMMON_RESET);
	writel(0, purple_base + PCIE_PHY_CMN_REG);
	writel(0, purple_base + PCIE_PHY_TRSVREG_RESET);
	writel(0, purple_base + PCIE_PHY_TRSV_RESET);
}

static void exynos_pcie_init_phy(struct pcie_port *pp)
{
	void __iomem *phy_base = pp->phy_base;

	/* DCC feedback control off */
	writel(0x29, phy_base + PCIE_PHY_DCC_FEEDBACK);

	/* set TX/RX impedance */
	writel(0xd5, phy_base + PCIE_PHY_IMPEDANCE);

	/* set 50Mhz PHY clock */
	writel(0x14, phy_base + PCIE_PHY_PLL_DIV_0);
	writel(0x12, phy_base + PCIE_PHY_PLL_DIV_1);

	/* set TX Differential output for lane 0 */
	writel(0x7f, phy_base + PCIE_PHY_TRSV0_DRV_LVL);

	/* set TX Pre-emphasis Level Control for lane 0 to minimum */
	writel(0x0, phy_base + PCIE_PHY_TRSV0_EMP_LVL);

	/* set RX clock and data recovery bandwidth */
	writel(0xe7, phy_base + PCIE_PHY_PLL_BIAS);
	writel(0x82, phy_base + PCIE_PHY_TRSV0_RXCDR);
	writel(0x82, phy_base + PCIE_PHY_TRSV1_RXCDR);
	writel(0x82, phy_base + PCIE_PHY_TRSV2_RXCDR);
	writel(0x82, phy_base + PCIE_PHY_TRSV3_RXCDR);

	/* change TX Pre-emphasis Level Control for lanes */
	writel(0x39, phy_base + PCIE_PHY_TRSV0_EMP_LVL);
	writel(0x39, phy_base + PCIE_PHY_TRSV1_EMP_LVL);
	writel(0x39, phy_base + PCIE_PHY_TRSV2_EMP_LVL);
	writel(0x39, phy_base + PCIE_PHY_TRSV3_EMP_LVL);

	/* set LVCC */
	writel(0x20, phy_base + PCIE_PHY_TRSV0_LVCC);
	writel(0xa0, phy_base + PCIE_PHY_TRSV1_LVCC);
	writel(0xa0, phy_base + PCIE_PHY_TRSV2_LVCC);
	writel(0xa0, phy_base + PCIE_PHY_TRSV3_LVCC);
}

static void exynos_pcie_assert_reset(struct pcie_port *pp)
{
	if (pp->reset_gpio >= 0)
		devm_gpio_request_one(pp->dev, pp->reset_gpio,
				GPIOF_OUT_INIT_HIGH, "RESET");
	return;
}

static int exynos_pcie_establish_link(struct pcie_port *pp)
{
	u32 val;
	int count = 0;
	void __iomem *elbi_base = pp->elbi_base;
	void __iomem *purple_base = pp->purple_base;
	void __iomem *phy_base = pp->phy_base;

	if (exynos_pcie_link_up(pp)) {
		dev_err(pp->dev, "Link already up\n");
		return 0;
	}

	/* assert reset signals */
	exynos_pcie_assert_core_reset(pp);
	exynos_pcie_assert_phy_reset(pp);

	/* de-assert phy reset */
	exynos_pcie_deassert_phy_reset(pp);

	/* initialize phy */
	exynos_pcie_init_phy(pp);

	/* pulse for common reset */
	writel(1, purple_base + PCIE_PHY_COMMON_RESET);
	udelay(500);
	writel(0, purple_base + PCIE_PHY_COMMON_RESET);

	/* de-assert core reset */
	exynos_pcie_deassert_core_reset(pp);

	/* setup root complex */
	exynos_pcie_setup_rc(pp);

	/* assert reset signal */
	exynos_pcie_assert_reset(pp);

	/* assert LTSSM enable */
	writel(PCIE_ELBI_LTSSM_ENABLE, elbi_base + PCIE_APP_LTSSM_ENABLE);

	/* check if the link is up or not */
	while (!exynos_pcie_link_up(pp)) {
		mdelay(100);
		count++;
		if (count == 10) {
			while (readl(phy_base + PCIE_PHY_PLL_LOCKED) == 0) {
				val = readl(purple_base + PCIE_PHY_PLL_LOCKED);
				dev_info(pp->dev, "PLL Locked: 0x%x\n", val);
			}
			dev_err(pp->dev, "PCIe Link Fail\n");
			return -EINVAL;
		}
	}

	dev_info(pp->dev, "Link up\n");

	return 0;
}

static void exynos_pcie_clear_irq_pulse(struct pcie_port *pp)
{
	u32 val;
	void __iomem *elbi_base = pp->elbi_base;

	val = readl(elbi_base + PCIE_IRQ_PULSE);
	writel(val, elbi_base + PCIE_IRQ_PULSE);
	return;
}

static void exynos_pcie_enable_irq_pulse(struct pcie_port *pp)
{
	u32 val;
	void __iomem *elbi_base = pp->elbi_base;

	/* enable INTX interrupt */
	val = IRQ_INTA_ASSERT | IRQ_INTB_ASSERT |
		IRQ_INTC_ASSERT | IRQ_INTD_ASSERT,
	writel(val, elbi_base + PCIE_IRQ_EN_PULSE);
	return;
}

static irqreturn_t exynos_pcie_irq_handler(int irq, void *arg)
{
	struct pcie_port *pp = arg;

	exynos_pcie_clear_irq_pulse(pp);
	return IRQ_HANDLED;
}

static void exynos_pcie_enable_interrupts(struct pcie_port *pp)
{
	exynos_pcie_enable_irq_pulse(pp);
	return;
}

static void exynos_pcie_host_init(struct pcie_port *pp)
{
	struct pcie_port_info *config = &pp->config;
	u32 val;

	/* Keep first 64K for IO */
	pp->cfg0_base = pp->cfg.start;
	pp->cfg1_base = pp->cfg.start + config->cfg0_size;
	pp->io_base = pp->io.start;
	pp->mem_base = pp->mem.start;

	/* enable link */
	exynos_pcie_establish_link(pp);

	exynos_pcie_wr_own_conf(pp, PCI_BASE_ADDRESS_0, 4, 0);

	/* program correct class for RC */
	exynos_pcie_wr_own_conf(pp, PCI_CLASS_DEVICE, 2, PCI_CLASS_BRIDGE_PCI);

	exynos_pcie_rd_own_conf(pp, PCIE_LINK_WIDTH_SPEED_CONTROL, 4, &val);
	val |= PORT_LOGIC_SPEED_CHANGE;
	exynos_pcie_wr_own_conf(pp, PCIE_LINK_WIDTH_SPEED_CONTROL, 4, val);

	exynos_pcie_enable_interrupts(pp);
}

static int add_pcie_port(struct pcie_port *pp, struct platform_device *pdev)
{
	struct resource *elbi_base;
	struct resource *phy_base;
	struct resource *purple_base;
	int ret;

	elbi_base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!elbi_base) {
		dev_err(&pdev->dev, "couldn't get elbi base resource\n");
		return -EINVAL;
	}
	pp->elbi_base = devm_ioremap_resource(&pdev->dev, elbi_base);
	if (IS_ERR(pp->elbi_base))
		return PTR_ERR(pp->elbi_base);

	phy_base = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!phy_base) {
		dev_err(&pdev->dev, "couldn't get phy base resource\n");
		return -EINVAL;
	}
	pp->phy_base = devm_ioremap_resource(&pdev->dev, phy_base);
	if (IS_ERR(pp->phy_base))
		return PTR_ERR(pp->phy_base);

	purple_base = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (!purple_base) {
		dev_err(&pdev->dev, "couldn't get purple base resource\n");
		return -EINVAL;
	}
	pp->purple_base = devm_ioremap_resource(&pdev->dev, purple_base);
	if (IS_ERR(pp->purple_base))
		return PTR_ERR(pp->purple_base);

	pp->irq = platform_get_irq(pdev, 1);
	if (!pp->irq) {
		dev_err(&pdev->dev, "failed to get irq\n");
		return -ENODEV;
	}
	ret = devm_request_irq(&pdev->dev, pp->irq, exynos_pcie_irq_handler,
				IRQF_SHARED, "exynos-pcie", pp);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq\n");
		return ret;
	}

	pp->dbi_base = devm_ioremap(&pdev->dev, pp->cfg.start,
				resource_size(&pp->cfg));
	if (!pp->dbi_base) {
		dev_err(&pdev->dev, "error with ioremap\n");
		return -ENOMEM;
	}

	pp->root_bus_nr = -1;

	spin_lock_init(&pp->conf_lock);
	exynos_pcie_host_init(pp);
	pp->va_cfg0_base = devm_ioremap(&pdev->dev, pp->cfg0_base,
					pp->config.cfg0_size);
	if (!pp->va_cfg0_base) {
		dev_err(pp->dev, "error with ioremap in function\n");
		return -ENOMEM;
	}
	pp->va_cfg1_base = devm_ioremap(&pdev->dev, pp->cfg1_base,
					pp->config.cfg1_size);
	if (!pp->va_cfg1_base) {
		dev_err(pp->dev, "error with ioremap\n");
		return -ENOMEM;
	}

	return 0;
}

static int __init exynos_pcie_probe(struct platform_device *pdev)
{
	struct pcie_port *pp;
	struct device_node *np = pdev->dev.of_node;
	struct of_pci_range range;
	struct of_pci_range_parser parser;
	int ret;

	pp = devm_kzalloc(&pdev->dev, sizeof(*pp), GFP_KERNEL);
	if (!pp) {
		dev_err(&pdev->dev, "no memory for pcie port\n");
		return -ENOMEM;
	}

	pp->dev = &pdev->dev;

	if (of_pci_range_parser_init(&parser, np)) {
		dev_err(&pdev->dev, "missing ranges property\n");
		return -EINVAL;
	}

	/* Get the I/O and memory ranges from DT */
	for_each_of_pci_range(&parser, &range) {
		unsigned long restype = range.flags & IORESOURCE_TYPE_BITS;
		if (restype == IORESOURCE_IO) {
			of_pci_range_to_resource(&range, np, &pp->io);
			pp->io.name = "I/O";
			pp->io.start = max_t(resource_size_t,
					     PCIBIOS_MIN_IO,
					     range.pci_addr + global_io_offset);
			pp->io.end = min_t(resource_size_t,
					   IO_SPACE_LIMIT,
					   range.pci_addr + range.size
					   + global_io_offset);
			pp->config.io_size = resource_size(&pp->io);
			pp->config.io_bus_addr = range.pci_addr;
		}
		if (restype == IORESOURCE_MEM) {
			of_pci_range_to_resource(&range, np, &pp->mem);
			pp->mem.name = "MEM";
			pp->config.mem_size = resource_size(&pp->mem);
			pp->config.mem_bus_addr = range.pci_addr;
		}
		if (restype == 0) {
			of_pci_range_to_resource(&range, np, &pp->cfg);
			pp->config.cfg0_size = resource_size(&pp->cfg)/2;
			pp->config.cfg1_size = resource_size(&pp->cfg)/2;
		}
	}

	pp->reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);

	pp->clk = devm_clk_get(&pdev->dev, "pcie");
	if (IS_ERR(pp->clk)) {
		dev_err(&pdev->dev, "Failed to get pcie rc clock\n");
		return PTR_ERR(pp->clk);
	}
	ret = clk_prepare_enable(pp->clk);
	if (ret)
		return ret;

	pp->bus_clk = devm_clk_get(&pdev->dev, "pcie_bus");
	if (IS_ERR(pp->bus_clk)) {
		dev_err(&pdev->dev, "Failed to get pcie bus clock\n");
		ret = PTR_ERR(pp->bus_clk);
		goto fail_clk;
	}
	ret = clk_prepare_enable(pp->bus_clk);
	if (ret)
		goto fail_clk;

	ret = add_pcie_port(pp, pdev);
	if (ret < 0)
		goto fail_bus_clk;

	pp->controller = exynos_pci.nr_controllers;
	exynos_pci.nr_controllers = 1;
	exynos_pci.private_data = (void **)&pp;

	pci_common_init(&exynos_pci);
	pci_assign_unassigned_resources();
#ifdef CONFIG_PCI_DOMAINS
	exynos_pci.domain++;
#endif

	platform_set_drvdata(pdev, pp);
	return 0;

fail_bus_clk:
	clk_disable_unprepare(pp->bus_clk);
fail_clk:
	clk_disable_unprepare(pp->clk);
	return ret;
}

static int __exit exynos_pcie_remove(struct platform_device *pdev)
{
	struct pcie_port *pp = platform_get_drvdata(pdev);

	clk_disable_unprepare(pp->bus_clk);
	clk_disable_unprepare(pp->clk);

	return 0;
}

static const struct of_device_id exynos_pcie_of_match[] = {
	{ .compatible = "samsung,exynos5440-pcie", },
	{},
};
MODULE_DEVICE_TABLE(of, exynos_pcie_of_match);

static struct platform_driver exynos_pcie_driver = {
	.remove		= __exit_p(exynos_pcie_remove),
	.driver = {
		.name	= "exynos-pcie",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(exynos_pcie_of_match),
	},
};

static int exynos_pcie_abort(unsigned long addr, unsigned int fsr,
			struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);
	unsigned long instr = *(unsigned long *)pc;

	WARN_ONCE(1, "pcie abort\n");

	/*
	 * If the instruction being executed was a read,
	 * make it look like it read all-ones.
	 */
	if ((instr & 0x0c100000) == 0x04100000) {
		int reg = (instr >> 12) & 15;
		unsigned long val;

		if (instr & 0x00400000)
			val = 255;
		else
			val = -1;

		regs->uregs[reg] = val;
		regs->ARM_pc += 4;
		return 0;
	}

	if ((instr & 0x0e100090) == 0x00100090) {
		int reg = (instr >> 12) & 15;

		regs->uregs[reg] = -1;
		regs->ARM_pc += 4;
		return 0;
	}

	return 1;
}

/* Exynos PCIe driver does not allow module unload */

static int __init pcie_init(void)
{
	hook_fault_code(16 + 6, exynos_pcie_abort, SIGBUS, 0,
			"imprecise external abort");

	platform_driver_probe(&exynos_pcie_driver, exynos_pcie_probe);

	return 0;
}
subsys_initcall(pcie_init);

MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_DESCRIPTION("Samsung PCIe host controller driver");
MODULE_LICENSE("GPL v2");
