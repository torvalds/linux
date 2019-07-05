// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Mobiveil PCIe Host controller
 *
 * Copyright (c) 2018 Mobiveil Inc.
 * Author: Subrahmanya Lingappa <l.subrahmanya@mobiveil.co.in>
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "../pci.h"

/* register offsets and bit positions */

/*
 * translation tables are grouped into windows, each window registers are
 * grouped into blocks of 4 or 16 registers each
 */
#define PAB_REG_BLOCK_SIZE		16
#define PAB_EXT_REG_BLOCK_SIZE		4

#define PAB_REG_ADDR(offset, win)	\
	(offset + (win * PAB_REG_BLOCK_SIZE))
#define PAB_EXT_REG_ADDR(offset, win)	\
	(offset + (win * PAB_EXT_REG_BLOCK_SIZE))

#define LTSSM_STATUS			0x0404
#define  LTSSM_STATUS_L0_MASK		0x3f
#define  LTSSM_STATUS_L0		0x2d

#define PAB_CTRL			0x0808
#define  AMBA_PIO_ENABLE_SHIFT		0
#define  PEX_PIO_ENABLE_SHIFT		1
#define  PAGE_SEL_SHIFT			13
#define  PAGE_SEL_MASK			0x3f
#define  PAGE_LO_MASK			0x3ff
#define  PAGE_SEL_OFFSET_SHIFT		10

#define PAB_AXI_PIO_CTRL		0x0840
#define  APIO_EN_MASK			0xf

#define PAB_PEX_PIO_CTRL		0x08c0
#define  PIO_ENABLE_SHIFT		0

#define PAB_INTP_AMBA_MISC_ENB		0x0b0c
#define PAB_INTP_AMBA_MISC_STAT		0x0b1c
#define  PAB_INTP_INTX_MASK		0x01e0
#define  PAB_INTP_MSI_MASK		0x8

#define PAB_AXI_AMAP_CTRL(win)		PAB_REG_ADDR(0x0ba0, win)
#define  WIN_ENABLE_SHIFT		0
#define  WIN_TYPE_SHIFT			1
#define  WIN_TYPE_MASK			0x3
#define  WIN_SIZE_MASK			0xfffffc00

#define PAB_EXT_AXI_AMAP_SIZE(win)	PAB_EXT_REG_ADDR(0xbaf0, win)

#define PAB_EXT_AXI_AMAP_AXI_WIN(win)	PAB_EXT_REG_ADDR(0x80a0, win)
#define PAB_AXI_AMAP_AXI_WIN(win)	PAB_REG_ADDR(0x0ba4, win)
#define  AXI_WINDOW_ALIGN_MASK		3

#define PAB_AXI_AMAP_PEX_WIN_L(win)	PAB_REG_ADDR(0x0ba8, win)
#define  PAB_BUS_SHIFT			24
#define  PAB_DEVICE_SHIFT		19
#define  PAB_FUNCTION_SHIFT		16

#define PAB_AXI_AMAP_PEX_WIN_H(win)	PAB_REG_ADDR(0x0bac, win)
#define PAB_INTP_AXI_PIO_CLASS		0x474

#define PAB_PEX_AMAP_CTRL(win)		PAB_REG_ADDR(0x4ba0, win)
#define  AMAP_CTRL_EN_SHIFT		0
#define  AMAP_CTRL_TYPE_SHIFT		1
#define  AMAP_CTRL_TYPE_MASK		3

#define PAB_EXT_PEX_AMAP_SIZEN(win)	PAB_EXT_REG_ADDR(0xbef0, win)
#define PAB_PEX_AMAP_AXI_WIN(win)	PAB_REG_ADDR(0x4ba4, win)
#define PAB_PEX_AMAP_PEX_WIN_L(win)	PAB_REG_ADDR(0x4ba8, win)
#define PAB_PEX_AMAP_PEX_WIN_H(win)	PAB_REG_ADDR(0x4bac, win)

/* starting offset of INTX bits in status register */
#define PAB_INTX_START			5

/* supported number of MSI interrupts */
#define PCI_NUM_MSI			16

/* MSI registers */
#define MSI_BASE_LO_OFFSET		0x04
#define MSI_BASE_HI_OFFSET		0x08
#define MSI_SIZE_OFFSET			0x0c
#define MSI_ENABLE_OFFSET		0x14
#define MSI_STATUS_OFFSET		0x18
#define MSI_DATA_OFFSET			0x20
#define MSI_ADDR_L_OFFSET		0x24
#define MSI_ADDR_H_OFFSET		0x28

/* outbound and inbound window definitions */
#define WIN_NUM_0			0
#define WIN_NUM_1			1
#define CFG_WINDOW_TYPE			0
#define IO_WINDOW_TYPE			1
#define MEM_WINDOW_TYPE			2
#define IB_WIN_SIZE			((u64)256 * 1024 * 1024 * 1024)
#define MAX_PIO_WINDOWS			8

/* Parameters for the waiting for link up routine */
#define LINK_WAIT_MAX_RETRIES		10
#define LINK_WAIT_MIN			90000
#define LINK_WAIT_MAX			100000

#define PAGED_ADDR_BNDRY		0xc00
#define OFFSET_TO_PAGE_ADDR(off)	\
	((off & PAGE_LO_MASK) | PAGED_ADDR_BNDRY)
#define OFFSET_TO_PAGE_IDX(off)		\
	((off >> PAGE_SEL_OFFSET_SHIFT) & PAGE_SEL_MASK)

struct mobiveil_msi {			/* MSI information */
	struct mutex lock;		/* protect bitmap variable */
	struct irq_domain *msi_domain;
	struct irq_domain *dev_domain;
	phys_addr_t msi_pages_phys;
	int num_of_vectors;
	DECLARE_BITMAP(msi_irq_in_use, PCI_NUM_MSI);
};

struct mobiveil_pcie {
	struct platform_device *pdev;
	struct list_head resources;
	void __iomem *config_axi_slave_base;	/* endpoint config base */
	void __iomem *csr_axi_slave_base;	/* root port config base */
	void __iomem *apb_csr_base;	/* MSI register base */
	phys_addr_t pcie_reg_base;	/* Physical PCIe Controller Base */
	struct irq_domain *intx_domain;
	raw_spinlock_t intx_mask_lock;
	int irq;
	int apio_wins;
	int ppio_wins;
	int ob_wins_configured;		/* configured outbound windows */
	int ib_wins_configured;		/* configured inbound windows */
	struct resource *ob_io_res;
	char root_bus_nr;
	struct mobiveil_msi msi;
};

/*
 * mobiveil_pcie_sel_page - routine to access paged register
 *
 * Registers whose address greater than PAGED_ADDR_BNDRY (0xc00) are paged,
 * for this scheme to work extracted higher 6 bits of the offset will be
 * written to pg_sel field of PAB_CTRL register and rest of the lower 10
 * bits enabled with PAGED_ADDR_BNDRY are used as offset of the register.
 */
static void mobiveil_pcie_sel_page(struct mobiveil_pcie *pcie, u8 pg_idx)
{
	u32 val;

	val = readl(pcie->csr_axi_slave_base + PAB_CTRL);
	val &= ~(PAGE_SEL_MASK << PAGE_SEL_SHIFT);
	val |= (pg_idx & PAGE_SEL_MASK) << PAGE_SEL_SHIFT;

	writel(val, pcie->csr_axi_slave_base + PAB_CTRL);
}

static void *mobiveil_pcie_comp_addr(struct mobiveil_pcie *pcie, u32 off)
{
	if (off < PAGED_ADDR_BNDRY) {
		/* For directly accessed registers, clear the pg_sel field */
		mobiveil_pcie_sel_page(pcie, 0);
		return pcie->csr_axi_slave_base + off;
	}

	mobiveil_pcie_sel_page(pcie, OFFSET_TO_PAGE_IDX(off));
	return pcie->csr_axi_slave_base + OFFSET_TO_PAGE_ADDR(off);
}

static int mobiveil_pcie_read(void __iomem *addr, int size, u32 *val)
{
	if ((uintptr_t)addr & (size - 1)) {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	switch (size) {
	case 4:
		*val = readl(addr);
		break;
	case 2:
		*val = readw(addr);
		break;
	case 1:
		*val = readb(addr);
		break;
	default:
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int mobiveil_pcie_write(void __iomem *addr, int size, u32 val)
{
	if ((uintptr_t)addr & (size - 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	switch (size) {
	case 4:
		writel(val, addr);
		break;
	case 2:
		writew(val, addr);
		break;
	case 1:
		writeb(val, addr);
		break;
	default:
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	return PCIBIOS_SUCCESSFUL;
}

static u32 csr_read(struct mobiveil_pcie *pcie, u32 off, size_t size)
{
	void *addr;
	u32 val;
	int ret;

	addr = mobiveil_pcie_comp_addr(pcie, off);

	ret = mobiveil_pcie_read(addr, size, &val);
	if (ret)
		dev_err(&pcie->pdev->dev, "read CSR address failed\n");

	return val;
}

static void csr_write(struct mobiveil_pcie *pcie, u32 val, u32 off, size_t size)
{
	void *addr;
	int ret;

	addr = mobiveil_pcie_comp_addr(pcie, off);

	ret = mobiveil_pcie_write(addr, size, val);
	if (ret)
		dev_err(&pcie->pdev->dev, "write CSR address failed\n");
}

static u32 csr_readl(struct mobiveil_pcie *pcie, u32 off)
{
	return csr_read(pcie, off, 0x4);
}

static void csr_writel(struct mobiveil_pcie *pcie, u32 val, u32 off)
{
	csr_write(pcie, val, off, 0x4);
}

static bool mobiveil_pcie_link_up(struct mobiveil_pcie *pcie)
{
	return (csr_readl(pcie, LTSSM_STATUS) &
		LTSSM_STATUS_L0_MASK) == LTSSM_STATUS_L0;
}

static bool mobiveil_pcie_valid_device(struct pci_bus *bus, unsigned int devfn)
{
	struct mobiveil_pcie *pcie = bus->sysdata;

	/* Only one device down on each root port */
	if ((bus->number == pcie->root_bus_nr) && (devfn > 0))
		return false;

	/*
	 * Do not read more than one device on the bus directly
	 * attached to RC
	 */
	if ((bus->primary == pcie->root_bus_nr) && (PCI_SLOT(devfn) > 0))
		return false;

	return true;
}

/*
 * mobiveil_pcie_map_bus - routine to get the configuration base of either
 * root port or endpoint
 */
static void __iomem *mobiveil_pcie_map_bus(struct pci_bus *bus,
					   unsigned int devfn, int where)
{
	struct mobiveil_pcie *pcie = bus->sysdata;
	u32 value;

	if (!mobiveil_pcie_valid_device(bus, devfn))
		return NULL;

	/* RC config access */
	if (bus->number == pcie->root_bus_nr)
		return pcie->csr_axi_slave_base + where;

	/*
	 * EP config access (in Config/APIO space)
	 * Program PEX Address base (31..16 bits) with appropriate value
	 * (BDF) in PAB_AXI_AMAP_PEX_WIN_L0 Register.
	 * Relies on pci_lock serialization
	 */
	value = bus->number << PAB_BUS_SHIFT |
		PCI_SLOT(devfn) << PAB_DEVICE_SHIFT |
		PCI_FUNC(devfn) << PAB_FUNCTION_SHIFT;

	csr_writel(pcie, value, PAB_AXI_AMAP_PEX_WIN_L(WIN_NUM_0));

	return pcie->config_axi_slave_base + where;
}

static struct pci_ops mobiveil_pcie_ops = {
	.map_bus = mobiveil_pcie_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static void mobiveil_pcie_isr(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct mobiveil_pcie *pcie = irq_desc_get_handler_data(desc);
	struct device *dev = &pcie->pdev->dev;
	struct mobiveil_msi *msi = &pcie->msi;
	u32 msi_data, msi_addr_lo, msi_addr_hi;
	u32 intr_status, msi_status;
	unsigned long shifted_status;
	u32 bit, virq, val, mask;

	/*
	 * The core provides a single interrupt for both INTx/MSI messages.
	 * So we'll read both INTx and MSI status
	 */

	chained_irq_enter(chip, desc);

	/* read INTx status */
	val = csr_readl(pcie, PAB_INTP_AMBA_MISC_STAT);
	mask = csr_readl(pcie, PAB_INTP_AMBA_MISC_ENB);
	intr_status = val & mask;

	/* Handle INTx */
	if (intr_status & PAB_INTP_INTX_MASK) {
		shifted_status = csr_readl(pcie, PAB_INTP_AMBA_MISC_STAT) >>
					   PAB_INTX_START;
		do {
			for_each_set_bit(bit, &shifted_status, PCI_NUM_INTX) {
				virq = irq_find_mapping(pcie->intx_domain,
							bit + 1);
				if (virq)
					generic_handle_irq(virq);
				else
					dev_err_ratelimited(dev, "unexpected IRQ, INT%d\n",
							    bit);

				/* clear interrupt */
				csr_writel(pcie,
					   shifted_status << PAB_INTX_START,
					   PAB_INTP_AMBA_MISC_STAT);
			}
		} while ((shifted_status >> PAB_INTX_START) != 0);
	}

	/* read extra MSI status register */
	msi_status = readl_relaxed(pcie->apb_csr_base + MSI_STATUS_OFFSET);

	/* handle MSI interrupts */
	while (msi_status & 1) {
		msi_data = readl_relaxed(pcie->apb_csr_base + MSI_DATA_OFFSET);

		/*
		 * MSI_STATUS_OFFSET register gets updated to zero
		 * once we pop not only the MSI data but also address
		 * from MSI hardware FIFO. So keeping these following
		 * two dummy reads.
		 */
		msi_addr_lo = readl_relaxed(pcie->apb_csr_base +
					    MSI_ADDR_L_OFFSET);
		msi_addr_hi = readl_relaxed(pcie->apb_csr_base +
					    MSI_ADDR_H_OFFSET);
		dev_dbg(dev, "MSI registers, data: %08x, addr: %08x:%08x\n",
			msi_data, msi_addr_hi, msi_addr_lo);

		virq = irq_find_mapping(msi->dev_domain, msi_data);
		if (virq)
			generic_handle_irq(virq);

		msi_status = readl_relaxed(pcie->apb_csr_base +
					   MSI_STATUS_OFFSET);
	}

	/* Clear the interrupt status */
	csr_writel(pcie, intr_status, PAB_INTP_AMBA_MISC_STAT);
	chained_irq_exit(chip, desc);
}

static int mobiveil_pcie_parse_dt(struct mobiveil_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct platform_device *pdev = pcie->pdev;
	struct device_node *node = dev->of_node;
	struct resource *res;

	/* map config resource */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "config_axi_slave");
	pcie->config_axi_slave_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(pcie->config_axi_slave_base))
		return PTR_ERR(pcie->config_axi_slave_base);
	pcie->ob_io_res = res;

	/* map csr resource */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "csr_axi_slave");
	pcie->csr_axi_slave_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(pcie->csr_axi_slave_base))
		return PTR_ERR(pcie->csr_axi_slave_base);
	pcie->pcie_reg_base = res->start;

	/* map MSI config resource */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "apb_csr");
	pcie->apb_csr_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(pcie->apb_csr_base))
		return PTR_ERR(pcie->apb_csr_base);

	/* read the number of windows requested */
	if (of_property_read_u32(node, "apio-wins", &pcie->apio_wins))
		pcie->apio_wins = MAX_PIO_WINDOWS;

	if (of_property_read_u32(node, "ppio-wins", &pcie->ppio_wins))
		pcie->ppio_wins = MAX_PIO_WINDOWS;

	pcie->irq = platform_get_irq(pdev, 0);
	if (pcie->irq <= 0) {
		dev_err(dev, "failed to map IRQ: %d\n", pcie->irq);
		return -ENODEV;
	}

	return 0;
}

static void program_ib_windows(struct mobiveil_pcie *pcie, int win_num,
			       int pci_addr, u32 type, u64 size)
{
	u32 value;
	u64 size64 = ~(size - 1);

	if (win_num >= pcie->ppio_wins) {
		dev_err(&pcie->pdev->dev,
			"ERROR: max inbound windows reached !\n");
		return;
	}

	value = csr_readl(pcie, PAB_PEX_PIO_CTRL);
	value |= 1 << PIO_ENABLE_SHIFT;
	csr_writel(pcie, value, PAB_PEX_PIO_CTRL);

	value = csr_readl(pcie, PAB_PEX_AMAP_CTRL(win_num));
	value &= ~(AMAP_CTRL_TYPE_MASK << AMAP_CTRL_TYPE_SHIFT | WIN_SIZE_MASK);
	value |= type << AMAP_CTRL_TYPE_SHIFT | 1 << AMAP_CTRL_EN_SHIFT |
		 (lower_32_bits(size64) & WIN_SIZE_MASK);
	csr_writel(pcie, value, PAB_PEX_AMAP_CTRL(win_num));

	csr_writel(pcie, upper_32_bits(size64),
		   PAB_EXT_PEX_AMAP_SIZEN(win_num));

	csr_writel(pcie, pci_addr, PAB_PEX_AMAP_AXI_WIN(win_num));

	csr_writel(pcie, pci_addr, PAB_PEX_AMAP_PEX_WIN_L(win_num));
	csr_writel(pcie, 0, PAB_PEX_AMAP_PEX_WIN_H(win_num));
	pcie->ib_wins_configured++;
}

/*
 * routine to program the outbound windows
 */
static void program_ob_windows(struct mobiveil_pcie *pcie, int win_num,
			       u64 cpu_addr, u64 pci_addr, u32 type, u64 size)
{
	u32 value;
	u64 size64 = ~(size - 1);

	if (win_num >= pcie->apio_wins) {
		dev_err(&pcie->pdev->dev,
			"ERROR: max outbound windows reached !\n");
		return;
	}

	/*
	 * program Enable Bit to 1, Type Bit to (00) base 2, AXI Window Size Bit
	 * to 4 KB in PAB_AXI_AMAP_CTRL register
	 */
	value = csr_readl(pcie, PAB_AXI_AMAP_CTRL(win_num));
	value &= ~(WIN_TYPE_MASK << WIN_TYPE_SHIFT | WIN_SIZE_MASK);
	value |= 1 << WIN_ENABLE_SHIFT | type << WIN_TYPE_SHIFT |
		 (lower_32_bits(size64) & WIN_SIZE_MASK);
	csr_writel(pcie, value, PAB_AXI_AMAP_CTRL(win_num));

	csr_writel(pcie, upper_32_bits(size64), PAB_EXT_AXI_AMAP_SIZE(win_num));

	/*
	 * program AXI window base with appropriate value in
	 * PAB_AXI_AMAP_AXI_WIN0 register
	 */
	csr_writel(pcie, lower_32_bits(cpu_addr) & (~AXI_WINDOW_ALIGN_MASK),
		   PAB_AXI_AMAP_AXI_WIN(win_num));
	csr_writel(pcie, upper_32_bits(cpu_addr),
		   PAB_EXT_AXI_AMAP_AXI_WIN(win_num));

	csr_writel(pcie, lower_32_bits(pci_addr),
		   PAB_AXI_AMAP_PEX_WIN_L(win_num));
	csr_writel(pcie, upper_32_bits(pci_addr),
		   PAB_AXI_AMAP_PEX_WIN_H(win_num));

	pcie->ob_wins_configured++;
}

static int mobiveil_bringup_link(struct mobiveil_pcie *pcie)
{
	int retries;

	/* check if the link is up or not */
	for (retries = 0; retries < LINK_WAIT_MAX_RETRIES; retries++) {
		if (mobiveil_pcie_link_up(pcie))
			return 0;

		usleep_range(LINK_WAIT_MIN, LINK_WAIT_MAX);
	}

	dev_err(&pcie->pdev->dev, "link never came up\n");

	return -ETIMEDOUT;
}

static void mobiveil_pcie_enable_msi(struct mobiveil_pcie *pcie)
{
	phys_addr_t msg_addr = pcie->pcie_reg_base;
	struct mobiveil_msi *msi = &pcie->msi;

	pcie->msi.num_of_vectors = PCI_NUM_MSI;
	msi->msi_pages_phys = (phys_addr_t)msg_addr;

	writel_relaxed(lower_32_bits(msg_addr),
		       pcie->apb_csr_base + MSI_BASE_LO_OFFSET);
	writel_relaxed(upper_32_bits(msg_addr),
		       pcie->apb_csr_base + MSI_BASE_HI_OFFSET);
	writel_relaxed(4096, pcie->apb_csr_base + MSI_SIZE_OFFSET);
	writel_relaxed(1, pcie->apb_csr_base + MSI_ENABLE_OFFSET);
}

static int mobiveil_host_init(struct mobiveil_pcie *pcie)
{
	u32 value, pab_ctrl, type;
	struct resource_entry *win;

	/* setup bus numbers */
	value = csr_readl(pcie, PCI_PRIMARY_BUS);
	value &= 0xff000000;
	value |= 0x00ff0100;
	csr_writel(pcie, value, PCI_PRIMARY_BUS);

	/*
	 * program Bus Master Enable Bit in Command Register in PAB Config
	 * Space
	 */
	value = csr_readl(pcie, PCI_COMMAND);
	value |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	csr_writel(pcie, value, PCI_COMMAND);

	/*
	 * program PIO Enable Bit to 1 (and PEX PIO Enable to 1) in PAB_CTRL
	 * register
	 */
	pab_ctrl = csr_readl(pcie, PAB_CTRL);
	pab_ctrl |= (1 << AMBA_PIO_ENABLE_SHIFT) | (1 << PEX_PIO_ENABLE_SHIFT);
	csr_writel(pcie, pab_ctrl, PAB_CTRL);

	csr_writel(pcie, (PAB_INTP_INTX_MASK | PAB_INTP_MSI_MASK),
		   PAB_INTP_AMBA_MISC_ENB);

	/*
	 * program PIO Enable Bit to 1 and Config Window Enable Bit to 1 in
	 * PAB_AXI_PIO_CTRL Register
	 */
	value = csr_readl(pcie, PAB_AXI_PIO_CTRL);
	value |= APIO_EN_MASK;
	csr_writel(pcie, value, PAB_AXI_PIO_CTRL);

	/*
	 * we'll program one outbound window for config reads and
	 * another default inbound window for all the upstream traffic
	 * rest of the outbound windows will be configured according to
	 * the "ranges" field defined in device tree
	 */

	/* config outbound translation window */
	program_ob_windows(pcie, WIN_NUM_0, pcie->ob_io_res->start, 0,
			   CFG_WINDOW_TYPE, resource_size(pcie->ob_io_res));

	/* memory inbound translation window */
	program_ib_windows(pcie, WIN_NUM_0, 0, MEM_WINDOW_TYPE, IB_WIN_SIZE);

	/* Get the I/O and memory ranges from DT */
	resource_list_for_each_entry(win, &pcie->resources) {
		if (resource_type(win->res) == IORESOURCE_MEM)
			type = MEM_WINDOW_TYPE;
		else if (resource_type(win->res) == IORESOURCE_IO)
			type = IO_WINDOW_TYPE;
		else
			continue;

		/* configure outbound translation window */
		program_ob_windows(pcie, pcie->ob_wins_configured,
				   win->res->start,
				   win->res->start - win->offset,
				   type, resource_size(win->res));
	}

	/* fixup for PCIe class register */
	value = csr_readl(pcie, PAB_INTP_AXI_PIO_CLASS);
	value &= 0xff;
	value |= (PCI_CLASS_BRIDGE_PCI << 16);
	csr_writel(pcie, value, PAB_INTP_AXI_PIO_CLASS);

	/* setup MSI hardware registers */
	mobiveil_pcie_enable_msi(pcie);

	return 0;
}

static void mobiveil_mask_intx_irq(struct irq_data *data)
{
	struct irq_desc *desc = irq_to_desc(data->irq);
	struct mobiveil_pcie *pcie;
	unsigned long flags;
	u32 mask, shifted_val;

	pcie = irq_desc_get_chip_data(desc);
	mask = 1 << ((data->hwirq + PAB_INTX_START) - 1);
	raw_spin_lock_irqsave(&pcie->intx_mask_lock, flags);
	shifted_val = csr_readl(pcie, PAB_INTP_AMBA_MISC_ENB);
	shifted_val &= ~mask;
	csr_writel(pcie, shifted_val, PAB_INTP_AMBA_MISC_ENB);
	raw_spin_unlock_irqrestore(&pcie->intx_mask_lock, flags);
}

static void mobiveil_unmask_intx_irq(struct irq_data *data)
{
	struct irq_desc *desc = irq_to_desc(data->irq);
	struct mobiveil_pcie *pcie;
	unsigned long flags;
	u32 shifted_val, mask;

	pcie = irq_desc_get_chip_data(desc);
	mask = 1 << ((data->hwirq + PAB_INTX_START) - 1);
	raw_spin_lock_irqsave(&pcie->intx_mask_lock, flags);
	shifted_val = csr_readl(pcie, PAB_INTP_AMBA_MISC_ENB);
	shifted_val |= mask;
	csr_writel(pcie, shifted_val, PAB_INTP_AMBA_MISC_ENB);
	raw_spin_unlock_irqrestore(&pcie->intx_mask_lock, flags);
}

static struct irq_chip intx_irq_chip = {
	.name = "mobiveil_pcie:intx",
	.irq_enable = mobiveil_unmask_intx_irq,
	.irq_disable = mobiveil_mask_intx_irq,
	.irq_mask = mobiveil_mask_intx_irq,
	.irq_unmask = mobiveil_unmask_intx_irq,
};

/* routine to setup the INTx related data */
static int mobiveil_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
				  irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &intx_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

/* INTx domain operations structure */
static const struct irq_domain_ops intx_domain_ops = {
	.map = mobiveil_pcie_intx_map,
};

static struct irq_chip mobiveil_msi_irq_chip = {
	.name = "Mobiveil PCIe MSI",
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static struct msi_domain_info mobiveil_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_PCI_MSIX),
	.chip	= &mobiveil_msi_irq_chip,
};

static void mobiveil_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct mobiveil_pcie *pcie = irq_data_get_irq_chip_data(data);
	phys_addr_t addr = pcie->pcie_reg_base + (data->hwirq * sizeof(int));

	msg->address_lo = lower_32_bits(addr);
	msg->address_hi = upper_32_bits(addr);
	msg->data = data->hwirq;

	dev_dbg(&pcie->pdev->dev, "msi#%d address_hi %#x address_lo %#x\n",
		(int)data->hwirq, msg->address_hi, msg->address_lo);
}

static int mobiveil_msi_set_affinity(struct irq_data *irq_data,
				     const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static struct irq_chip mobiveil_msi_bottom_irq_chip = {
	.name			= "Mobiveil MSI",
	.irq_compose_msi_msg	= mobiveil_compose_msi_msg,
	.irq_set_affinity	= mobiveil_msi_set_affinity,
};

static int mobiveil_irq_msi_domain_alloc(struct irq_domain *domain,
					 unsigned int virq,
					 unsigned int nr_irqs, void *args)
{
	struct mobiveil_pcie *pcie = domain->host_data;
	struct mobiveil_msi *msi = &pcie->msi;
	unsigned long bit;

	WARN_ON(nr_irqs != 1);
	mutex_lock(&msi->lock);

	bit = find_first_zero_bit(msi->msi_irq_in_use, msi->num_of_vectors);
	if (bit >= msi->num_of_vectors) {
		mutex_unlock(&msi->lock);
		return -ENOSPC;
	}

	set_bit(bit, msi->msi_irq_in_use);

	mutex_unlock(&msi->lock);

	irq_domain_set_info(domain, virq, bit, &mobiveil_msi_bottom_irq_chip,
			    domain->host_data, handle_level_irq, NULL, NULL);
	return 0;
}

static void mobiveil_irq_msi_domain_free(struct irq_domain *domain,
					 unsigned int virq,
					 unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct mobiveil_pcie *pcie = irq_data_get_irq_chip_data(d);
	struct mobiveil_msi *msi = &pcie->msi;

	mutex_lock(&msi->lock);

	if (!test_bit(d->hwirq, msi->msi_irq_in_use))
		dev_err(&pcie->pdev->dev, "trying to free unused MSI#%lu\n",
			d->hwirq);
	else
		__clear_bit(d->hwirq, msi->msi_irq_in_use);

	mutex_unlock(&msi->lock);
}
static const struct irq_domain_ops msi_domain_ops = {
	.alloc	= mobiveil_irq_msi_domain_alloc,
	.free	= mobiveil_irq_msi_domain_free,
};

static int mobiveil_allocate_msi_domains(struct mobiveil_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct fwnode_handle *fwnode = of_node_to_fwnode(dev->of_node);
	struct mobiveil_msi *msi = &pcie->msi;

	mutex_init(&pcie->msi.lock);
	msi->dev_domain = irq_domain_add_linear(NULL, msi->num_of_vectors,
						&msi_domain_ops, pcie);
	if (!msi->dev_domain) {
		dev_err(dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	msi->msi_domain = pci_msi_create_irq_domain(fwnode,
						    &mobiveil_msi_domain_info,
						    msi->dev_domain);
	if (!msi->msi_domain) {
		dev_err(dev, "failed to create MSI domain\n");
		irq_domain_remove(msi->dev_domain);
		return -ENOMEM;
	}

	return 0;
}

static int mobiveil_pcie_init_irq_domain(struct mobiveil_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct device_node *node = dev->of_node;
	int ret;

	/* setup INTx */
	pcie->intx_domain = irq_domain_add_linear(node, PCI_NUM_INTX,
						  &intx_domain_ops, pcie);

	if (!pcie->intx_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return -ENOMEM;
	}

	raw_spin_lock_init(&pcie->intx_mask_lock);

	/* setup MSI */
	ret = mobiveil_allocate_msi_domains(pcie);
	if (ret)
		return ret;

	return 0;
}

static int mobiveil_pcie_probe(struct platform_device *pdev)
{
	struct mobiveil_pcie *pcie;
	struct pci_bus *bus;
	struct pci_bus *child;
	struct pci_host_bridge *bridge;
	struct device *dev = &pdev->dev;
	resource_size_t iobase;
	int ret;

	/* allocate the PCIe port */
	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!bridge)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(bridge);

	pcie->pdev = pdev;

	ret = mobiveil_pcie_parse_dt(pcie);
	if (ret) {
		dev_err(dev, "Parsing DT failed, ret: %x\n", ret);
		return ret;
	}

	INIT_LIST_HEAD(&pcie->resources);

	/* parse the host bridge base addresses from the device tree file */
	ret = devm_of_pci_get_host_bridge_resources(dev, 0, 0xff,
						    &pcie->resources, &iobase);
	if (ret) {
		dev_err(dev, "Getting bridge resources failed\n");
		return ret;
	}

	/*
	 * configure all inbound and outbound windows and prepare the RC for
	 * config access
	 */
	ret = mobiveil_host_init(pcie);
	if (ret) {
		dev_err(dev, "Failed to initialize host\n");
		goto error;
	}

	/* initialize the IRQ domains */
	ret = mobiveil_pcie_init_irq_domain(pcie);
	if (ret) {
		dev_err(dev, "Failed creating IRQ Domain\n");
		goto error;
	}

	irq_set_chained_handler_and_data(pcie->irq, mobiveil_pcie_isr, pcie);

	ret = devm_request_pci_bus_resources(dev, &pcie->resources);
	if (ret)
		goto error;

	/* Initialize bridge */
	list_splice_init(&pcie->resources, &bridge->windows);
	bridge->dev.parent = dev;
	bridge->sysdata = pcie;
	bridge->busnr = pcie->root_bus_nr;
	bridge->ops = &mobiveil_pcie_ops;
	bridge->map_irq = of_irq_parse_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;

	ret = mobiveil_bringup_link(pcie);
	if (ret) {
		dev_info(dev, "link bring-up failed\n");
		goto error;
	}

	/* setup the kernel resources for the newly added PCIe root bus */
	ret = pci_scan_root_bus_bridge(bridge);
	if (ret)
		goto error;

	bus = bridge->bus;

	pci_assign_unassigned_bus_resources(bus);
	list_for_each_entry(child, &bus->children, node)
		pcie_bus_configure_settings(child);
	pci_bus_add_devices(bus);

	return 0;
error:
	pci_free_resource_list(&pcie->resources);
	return ret;
}

static const struct of_device_id mobiveil_pcie_of_match[] = {
	{.compatible = "mbvl,gpex40-pcie",},
	{},
};

MODULE_DEVICE_TABLE(of, mobiveil_pcie_of_match);

static struct platform_driver mobiveil_pcie_driver = {
	.probe = mobiveil_pcie_probe,
	.driver = {
		.name = "mobiveil-pcie",
		.of_match_table = mobiveil_pcie_of_match,
		.suppress_bind_attrs = true,
	},
};

builtin_platform_driver(mobiveil_pcie_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Mobiveil PCIe host controller driver");
MODULE_AUTHOR("Subrahmanya Lingappa <l.subrahmanya@mobiveil.co.in>");
