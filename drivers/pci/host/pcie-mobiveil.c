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
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* register offsets and bit positions */

/*
 * translation tables are grouped into windows, each window registers are
 * grouped into blocks of 4 or 16 registers each
 */
#define PAB_REG_BLOCK_SIZE	16
#define PAB_EXT_REG_BLOCK_SIZE	4

#define PAB_REG_ADDR(offset, win) (offset + (win * PAB_REG_BLOCK_SIZE))
#define PAB_EXT_REG_ADDR(offset, win) (offset + (win * PAB_EXT_REG_BLOCK_SIZE))

#define LTSSM_STATUS		0x0404
#define  LTSSM_STATUS_L0_MASK	0x3f
#define  LTSSM_STATUS_L0	0x2d

#define PAB_CTRL		0x0808
#define  AMBA_PIO_ENABLE_SHIFT	0
#define  PEX_PIO_ENABLE_SHIFT	1
#define  PAGE_SEL_SHIFT	13
#define  PAGE_SEL_MASK		0x3f
#define  PAGE_LO_MASK		0x3ff
#define  PAGE_SEL_EN		0xc00
#define  PAGE_SEL_OFFSET_SHIFT	10

#define PAB_AXI_PIO_CTRL	0x0840
#define  APIO_EN_MASK		0xf

#define PAB_PEX_PIO_CTRL	0x08c0
#define  PIO_ENABLE_SHIFT	0

#define PAB_INTP_AMBA_MISC_ENB		0x0b0c
#define PAB_INTP_AMBA_MISC_STAT	0x0b1c
#define  PAB_INTP_INTX_MASK		0x01e0

#define PAB_AXI_AMAP_CTRL(win)	PAB_REG_ADDR(0x0ba0, win)
#define  WIN_ENABLE_SHIFT	0
#define  WIN_TYPE_SHIFT	1

#define PAB_EXT_AXI_AMAP_SIZE(win)	PAB_EXT_REG_ADDR(0xbaf0, win)

#define PAB_AXI_AMAP_AXI_WIN(win)	PAB_REG_ADDR(0x0ba4, win)
#define  AXI_WINDOW_ALIGN_MASK		3

#define PAB_AXI_AMAP_PEX_WIN_L(win)	PAB_REG_ADDR(0x0ba8, win)
#define  PAB_BUS_SHIFT		24
#define  PAB_DEVICE_SHIFT	19
#define  PAB_FUNCTION_SHIFT	16

#define PAB_AXI_AMAP_PEX_WIN_H(win)	PAB_REG_ADDR(0x0bac, win)
#define PAB_INTP_AXI_PIO_CLASS		0x474

#define PAB_PEX_AMAP_CTRL(win)	PAB_REG_ADDR(0x4ba0, win)
#define  AMAP_CTRL_EN_SHIFT	0
#define  AMAP_CTRL_TYPE_SHIFT	1

#define PAB_EXT_PEX_AMAP_SIZEN(win)	PAB_EXT_REG_ADDR(0xbef0, win)
#define PAB_PEX_AMAP_AXI_WIN(win)	PAB_REG_ADDR(0x4ba4, win)
#define PAB_PEX_AMAP_PEX_WIN_L(win)	PAB_REG_ADDR(0x4ba8, win)
#define PAB_PEX_AMAP_PEX_WIN_H(win)	PAB_REG_ADDR(0x4bac, win)

/* starting offset of INTX bits in status register */
#define PAB_INTX_START	5

/* outbound and inbound window definitions */
#define WIN_NUM_0		0
#define WIN_NUM_1		1
#define CFG_WINDOW_TYPE	0
#define IO_WINDOW_TYPE		1
#define MEM_WINDOW_TYPE	2
#define IB_WIN_SIZE		(256 * 1024 * 1024 * 1024)
#define MAX_PIO_WINDOWS	8

/* Parameters for the waiting for link up routine */
#define LINK_WAIT_MAX_RETRIES	10
#define LINK_WAIT_MIN	90000
#define LINK_WAIT_MAX	100000

struct mobiveil_pcie {
	struct platform_device *pdev;
	struct list_head resources;
	void __iomem *config_axi_slave_base;	/* endpoint config base */
	void __iomem *csr_axi_slave_base;	/* root port config base */
	void __iomem *pcie_reg_base;	/* Physical PCIe Controller Base */
	struct irq_domain *intx_domain;
	raw_spinlock_t intx_mask_lock;
	int irq;
	int apio_wins;
	int ppio_wins;
	int ob_wins_configured;		/* configured outbound windows */
	int ib_wins_configured;		/* configured inbound windows */
	struct resource *ob_io_res;
	char root_bus_nr;
};

static inline void csr_writel(struct mobiveil_pcie *pcie, const u32 value,
		const u32 reg)
{
	writel_relaxed(value, pcie->csr_axi_slave_base + reg);
}

static inline u32 csr_readl(struct mobiveil_pcie *pcie, const u32 reg)
{
	return readl_relaxed(pcie->csr_axi_slave_base + reg);
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
	if ((bus->primary == pcie->root_bus_nr) && (devfn > 0))
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

	if (!mobiveil_pcie_valid_device(bus, devfn))
		return NULL;

	if (bus->number == pcie->root_bus_nr) {
		/* RC config access */
		return pcie->csr_axi_slave_base + where;
	}

	/*
	 * EP config access (in Config/APIO space)
	 * Program PEX Address base (31..16 bits) with appropriate value
	 * (BDF) in PAB_AXI_AMAP_PEX_WIN_L0 Register.
	 * Relies on pci_lock serialization
	 */
	csr_writel(pcie, bus->number << PAB_BUS_SHIFT |
			PCI_SLOT(devfn) << PAB_DEVICE_SHIFT |
			PCI_FUNC(devfn) << PAB_FUNCTION_SHIFT,
			PAB_AXI_AMAP_PEX_WIN_L(WIN_NUM_0));
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
	u32 intr_status;
	unsigned long shifted_status;
	u32 bit, virq, val, mask;

	/*
	 * The core provides interrupt for INTx.
	 * So we'll read INTx status.
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
					dev_err_ratelimited(dev,
						"unexpected IRQ, INT%d\n", bit);

				/* clear interrupt */
				csr_writel(pcie,
					shifted_status << PAB_INTX_START,
					PAB_INTP_AMBA_MISC_STAT);
			}
		} while ((shifted_status >> PAB_INTX_START) != 0);
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
	const char *type;

	type = of_get_property(node, "device_type", NULL);
	if (!type || strcmp(type, "pci")) {
		dev_err(dev, "invalid \"device_type\" %s\n", type);
		return -EINVAL;
	}

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

	irq_set_chained_handler_and_data(pcie->irq, mobiveil_pcie_isr, pcie);

	return 0;
}

/*
 * select_paged_register - routine to access paged register of root complex
 *
 * registers of RC are paged, for this scheme to work
 * extracted higher 6 bits of the offset will be written to pg_sel
 * field of PAB_CTRL register and rest of the lower 10 bits enabled with
 * PAGE_SEL_EN are used as offset of the register.
 */
static void select_paged_register(struct mobiveil_pcie *pcie, u32 offset)
{
	int pab_ctrl_dw, pg_sel;

	/* clear pg_sel field */
	pab_ctrl_dw = csr_readl(pcie, PAB_CTRL);
	pab_ctrl_dw = (pab_ctrl_dw & ~(PAGE_SEL_MASK << PAGE_SEL_SHIFT));

	/* set pg_sel field */
	pg_sel = (offset >> PAGE_SEL_OFFSET_SHIFT) & PAGE_SEL_MASK;
	pab_ctrl_dw |= ((pg_sel << PAGE_SEL_SHIFT));
	csr_writel(pcie, pab_ctrl_dw, PAB_CTRL);
}

static void write_paged_register(struct mobiveil_pcie *pcie,
		u32 val, u32 offset)
{
	u32 off = (offset & PAGE_LO_MASK) | PAGE_SEL_EN;

	select_paged_register(pcie, offset);
	csr_writel(pcie, val, off);
}

static u32 read_paged_register(struct mobiveil_pcie *pcie, u32 offset)
{
	u32 off = (offset & PAGE_LO_MASK) | PAGE_SEL_EN;

	select_paged_register(pcie, offset);
	return csr_readl(pcie, off);
}

static void program_ib_windows(struct mobiveil_pcie *pcie, int win_num,
		int pci_addr, u32 type, u64 size)
{
	int pio_ctrl_val;
	int amap_ctrl_dw;
	u64 size64 = ~(size - 1);

	if ((pcie->ib_wins_configured + 1) > pcie->ppio_wins) {
		dev_err(&pcie->pdev->dev,
			"ERROR: max inbound windows reached !\n");
		return;
	}

	pio_ctrl_val = csr_readl(pcie, PAB_PEX_PIO_CTRL);
	csr_writel(pcie,
		pio_ctrl_val | (1 << PIO_ENABLE_SHIFT), PAB_PEX_PIO_CTRL);
	amap_ctrl_dw = read_paged_register(pcie, PAB_PEX_AMAP_CTRL(win_num));
	amap_ctrl_dw = (amap_ctrl_dw | (type << AMAP_CTRL_TYPE_SHIFT));
	amap_ctrl_dw = (amap_ctrl_dw | (1 << AMAP_CTRL_EN_SHIFT));

	write_paged_register(pcie, amap_ctrl_dw | lower_32_bits(size64),
				PAB_PEX_AMAP_CTRL(win_num));

	write_paged_register(pcie, upper_32_bits(size64),
				PAB_EXT_PEX_AMAP_SIZEN(win_num));

	write_paged_register(pcie, pci_addr, PAB_PEX_AMAP_AXI_WIN(win_num));
	write_paged_register(pcie, pci_addr, PAB_PEX_AMAP_PEX_WIN_L(win_num));
	write_paged_register(pcie, 0, PAB_PEX_AMAP_PEX_WIN_H(win_num));
}

/*
 * routine to program the outbound windows
 */
static void program_ob_windows(struct mobiveil_pcie *pcie, int win_num,
		u64 cpu_addr, u64 pci_addr, u32 config_io_bit, u64 size)
{

	u32 value, type;
	u64 size64 = ~(size - 1);

	if ((pcie->ob_wins_configured + 1) > pcie->apio_wins) {
		dev_err(&pcie->pdev->dev,
			"ERROR: max outbound windows reached !\n");
		return;
	}

	/*
	 * program Enable Bit to 1, Type Bit to (00) base 2, AXI Window Size Bit
	 * to 4 KB in PAB_AXI_AMAP_CTRL register
	 */
	type = config_io_bit;
	value = csr_readl(pcie, PAB_AXI_AMAP_CTRL(win_num));
	csr_writel(pcie, 1 << WIN_ENABLE_SHIFT | type << WIN_TYPE_SHIFT |
			lower_32_bits(size64), PAB_AXI_AMAP_CTRL(win_num));

	write_paged_register(pcie, upper_32_bits(size64),
				PAB_EXT_AXI_AMAP_SIZE(win_num));

	/*
	 * program AXI window base with appropriate value in
	 * PAB_AXI_AMAP_AXI_WIN0 register
	 */
	value = csr_readl(pcie, PAB_AXI_AMAP_AXI_WIN(win_num));
	csr_writel(pcie, cpu_addr & (~AXI_WINDOW_ALIGN_MASK),
			PAB_AXI_AMAP_AXI_WIN(win_num));

	value = csr_readl(pcie, PAB_AXI_AMAP_PEX_WIN_H(win_num));

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

static int mobiveil_host_init(struct mobiveil_pcie *pcie)
{
	u32 value, pab_ctrl, type = 0;
	int err;
	struct resource_entry *win, *tmp;

	err = mobiveil_bringup_link(pcie);
	if (err) {
		dev_info(&pcie->pdev->dev, "link bring-up failed\n");
		return err;
	}

	/*
	 * program Bus Master Enable Bit in Command Register in PAB Config
	 * Space
	 */
	value = csr_readl(pcie, PCI_COMMAND);
	csr_writel(pcie, value | PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_MASTER, PCI_COMMAND);

	/*
	 * program PIO Enable Bit to 1 (and PEX PIO Enable to 1) in PAB_CTRL
	 * register
	 */
	pab_ctrl = csr_readl(pcie, PAB_CTRL);
	csr_writel(pcie, pab_ctrl | (1 << AMBA_PIO_ENABLE_SHIFT) |
		(1 << PEX_PIO_ENABLE_SHIFT), PAB_CTRL);

	csr_writel(pcie, PAB_INTP_INTX_MASK, PAB_INTP_AMBA_MISC_ENB);

	/*
	 * program PIO Enable Bit to 1 and Config Window Enable Bit to 1 in
	 * PAB_AXI_PIO_CTRL Register
	 */
	value = csr_readl(pcie, PAB_AXI_PIO_CTRL);
	csr_writel(pcie, value | APIO_EN_MASK, PAB_AXI_PIO_CTRL);

	/*
	 * we'll program one outbound window for config reads and
	 * another default inbound window for all the upstream traffic
	 * rest of the outbound windows will be configured according to
	 * the "ranges" field defined in device tree
	 */

	/* config outbound translation window */
	program_ob_windows(pcie, pcie->ob_wins_configured,
			pcie->ob_io_res->start, 0, CFG_WINDOW_TYPE,
			resource_size(pcie->ob_io_res));

	/* memory inbound translation window */
	program_ib_windows(pcie, WIN_NUM_1, 0, MEM_WINDOW_TYPE, IB_WIN_SIZE);

	/* Get the I/O and memory ranges from DT */
	resource_list_for_each_entry_safe(win, tmp, &pcie->resources) {
		type = 0;
		if (resource_type(win->res) == IORESOURCE_MEM)
			type = MEM_WINDOW_TYPE;
		if (resource_type(win->res) == IORESOURCE_IO)
			type = IO_WINDOW_TYPE;
		if (type) {
			/* configure outbound translation window */
			program_ob_windows(pcie, pcie->ob_wins_configured,
				win->res->start, 0, type,
				resource_size(win->res));
		}
	}

	return err;
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
	csr_writel(pcie, (shifted_val & (~mask)), PAB_INTP_AMBA_MISC_ENB);
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
	csr_writel(pcie, (shifted_val | mask), PAB_INTP_AMBA_MISC_ENB);
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

static int mobiveil_pcie_init_irq_domain(struct mobiveil_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct device_node *node = dev->of_node;
	int ret;

	/* setup INTx */
	pcie->intx_domain = irq_domain_add_linear(node,
				PCI_NUM_INTX, &intx_domain_ops, pcie);

	if (!pcie->intx_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return -ENODEV;
	}

	raw_spin_lock_init(&pcie->intx_mask_lock);

	return 0;
}

static int mobiveil_pcie_probe(struct platform_device *pdev)
{
	struct mobiveil_pcie *pcie;
	struct pci_bus *bus;
	struct pci_bus *child;
	struct pci_host_bridge *bridge;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	resource_size_t iobase;
	int ret;

	/* allocate the PCIe port */
	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!bridge)
		return -ENODEV;

	pcie = pci_host_bridge_priv(bridge);
	if (!pcie)
		return -ENOMEM;

	pcie->pdev = pdev;

	ret = mobiveil_pcie_parse_dt(pcie);
	if (ret) {
		dev_err(dev, "Parsing DT failed, ret: %x\n", ret);
		return ret;
	}

	INIT_LIST_HEAD(&pcie->resources);

	/* parse the host bridge base addresses from the device tree file */
	ret = of_pci_get_host_bridge_resources(node, 0, 0xff,
			&pcie->resources, &iobase);
	if (ret) {
		dev_err(dev, "Getting bridge resources failed\n");
		return -ENOMEM;
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

	/* fixup for PCIe class register */
	csr_writel(pcie, 0x060402ab, PAB_INTP_AXI_PIO_CLASS);

	/* initialize the IRQ domains */
	ret = mobiveil_pcie_init_irq_domain(pcie);
	if (ret) {
		dev_err(dev, "Failed creating IRQ Domain\n");
		goto error;
	}

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
