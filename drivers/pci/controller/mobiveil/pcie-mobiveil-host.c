// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Mobiveil PCIe Host controller
 *
 * Copyright (c) 2018 Mobiveil Inc.
 * Copyright 2019-2020 NXP
 *
 * Author: Subrahmanya Lingappa <l.subrahmanya@mobiveil.co.in>
 *	   Hou Zhiqiang <Zhiqiang.Hou@nxp.com>
 */

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

#include "pcie-mobiveil.h"

static bool mobiveil_pcie_valid_device(struct pci_bus *bus, unsigned int devfn)
{
	struct mobiveil_pcie *pcie = bus->sysdata;
	struct mobiveil_root_port *rp = &pcie->rp;

	/* Only one device down on each root port */
	if ((bus->number == rp->root_bus_nr) && (devfn > 0))
		return false;

	/*
	 * Do not read more than one device on the bus directly
	 * attached to RC
	 */
	if ((bus->primary == rp->root_bus_nr) && (PCI_SLOT(devfn) > 0))
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
	struct mobiveil_root_port *rp = &pcie->rp;
	u32 value;

	if (!mobiveil_pcie_valid_device(bus, devfn))
		return NULL;

	/* RC config access */
	if (bus->number == rp->root_bus_nr)
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

	mobiveil_csr_writel(pcie, value, PAB_AXI_AMAP_PEX_WIN_L(WIN_NUM_0));

	return rp->config_axi_slave_base + where;
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
	struct mobiveil_root_port *rp = &pcie->rp;
	struct mobiveil_msi *msi = &rp->msi;
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
	val = mobiveil_csr_readl(pcie, PAB_INTP_AMBA_MISC_STAT);
	mask = mobiveil_csr_readl(pcie, PAB_INTP_AMBA_MISC_ENB);
	intr_status = val & mask;

	/* Handle INTx */
	if (intr_status & PAB_INTP_INTX_MASK) {
		shifted_status = mobiveil_csr_readl(pcie,
						    PAB_INTP_AMBA_MISC_STAT);
		shifted_status &= PAB_INTP_INTX_MASK;
		shifted_status >>= PAB_INTX_START;
		do {
			for_each_set_bit(bit, &shifted_status, PCI_NUM_INTX) {
				virq = irq_find_mapping(rp->intx_domain,
							bit + 1);
				if (virq)
					generic_handle_irq(virq);
				else
					dev_err_ratelimited(dev, "unexpected IRQ, INT%d\n",
							    bit);

				/* clear interrupt handled */
				mobiveil_csr_writel(pcie,
						    1 << (PAB_INTX_START + bit),
						    PAB_INTP_AMBA_MISC_STAT);
			}

			shifted_status = mobiveil_csr_readl(pcie,
							    PAB_INTP_AMBA_MISC_STAT);
			shifted_status &= PAB_INTP_INTX_MASK;
			shifted_status >>= PAB_INTX_START;
		} while (shifted_status != 0);
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
	mobiveil_csr_writel(pcie, intr_status, PAB_INTP_AMBA_MISC_STAT);
	chained_irq_exit(chip, desc);
}

static int mobiveil_pcie_parse_dt(struct mobiveil_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct platform_device *pdev = pcie->pdev;
	struct device_node *node = dev->of_node;
	struct mobiveil_root_port *rp = &pcie->rp;
	struct resource *res;

	/* map config resource */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "config_axi_slave");
	rp->config_axi_slave_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(rp->config_axi_slave_base))
		return PTR_ERR(rp->config_axi_slave_base);
	rp->ob_io_res = res;

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

	return 0;
}

static void mobiveil_pcie_enable_msi(struct mobiveil_pcie *pcie)
{
	phys_addr_t msg_addr = pcie->pcie_reg_base;
	struct mobiveil_msi *msi = &pcie->rp.msi;

	msi->num_of_vectors = PCI_NUM_MSI;
	msi->msi_pages_phys = (phys_addr_t)msg_addr;

	writel_relaxed(lower_32_bits(msg_addr),
		       pcie->apb_csr_base + MSI_BASE_LO_OFFSET);
	writel_relaxed(upper_32_bits(msg_addr),
		       pcie->apb_csr_base + MSI_BASE_HI_OFFSET);
	writel_relaxed(4096, pcie->apb_csr_base + MSI_SIZE_OFFSET);
	writel_relaxed(1, pcie->apb_csr_base + MSI_ENABLE_OFFSET);
}

int mobiveil_host_init(struct mobiveil_pcie *pcie, bool reinit)
{
	struct mobiveil_root_port *rp = &pcie->rp;
	struct pci_host_bridge *bridge = rp->bridge;
	u32 value, pab_ctrl, type;
	struct resource_entry *win;

	pcie->ib_wins_configured = 0;
	pcie->ob_wins_configured = 0;

	if (!reinit) {
		/* setup bus numbers */
		value = mobiveil_csr_readl(pcie, PCI_PRIMARY_BUS);
		value &= 0xff000000;
		value |= 0x00ff0100;
		mobiveil_csr_writel(pcie, value, PCI_PRIMARY_BUS);
	}

	/*
	 * program Bus Master Enable Bit in Command Register in PAB Config
	 * Space
	 */
	value = mobiveil_csr_readl(pcie, PCI_COMMAND);
	value |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	mobiveil_csr_writel(pcie, value, PCI_COMMAND);

	/*
	 * program PIO Enable Bit to 1 (and PEX PIO Enable to 1) in PAB_CTRL
	 * register
	 */
	pab_ctrl = mobiveil_csr_readl(pcie, PAB_CTRL);
	pab_ctrl |= (1 << AMBA_PIO_ENABLE_SHIFT) | (1 << PEX_PIO_ENABLE_SHIFT);
	mobiveil_csr_writel(pcie, pab_ctrl, PAB_CTRL);

	/*
	 * program PIO Enable Bit to 1 and Config Window Enable Bit to 1 in
	 * PAB_AXI_PIO_CTRL Register
	 */
	value = mobiveil_csr_readl(pcie, PAB_AXI_PIO_CTRL);
	value |= APIO_EN_MASK;
	mobiveil_csr_writel(pcie, value, PAB_AXI_PIO_CTRL);

	/* Enable PCIe PIO master */
	value = mobiveil_csr_readl(pcie, PAB_PEX_PIO_CTRL);
	value |= 1 << PIO_ENABLE_SHIFT;
	mobiveil_csr_writel(pcie, value, PAB_PEX_PIO_CTRL);

	/*
	 * we'll program one outbound window for config reads and
	 * another default inbound window for all the upstream traffic
	 * rest of the outbound windows will be configured according to
	 * the "ranges" field defined in device tree
	 */

	/* config outbound translation window */
	program_ob_windows(pcie, WIN_NUM_0, rp->ob_io_res->start, 0,
			   CFG_WINDOW_TYPE, resource_size(rp->ob_io_res));

	/* memory inbound translation window */
	program_ib_windows(pcie, WIN_NUM_0, 0, 0, MEM_WINDOW_TYPE, IB_WIN_SIZE);

	/* Get the I/O and memory ranges from DT */
	resource_list_for_each_entry(win, &bridge->windows) {
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
	value = mobiveil_csr_readl(pcie, PAB_INTP_AXI_PIO_CLASS);
	value &= 0xff;
	value |= (PCI_CLASS_BRIDGE_PCI << 16);
	mobiveil_csr_writel(pcie, value, PAB_INTP_AXI_PIO_CLASS);

	return 0;
}

static void mobiveil_mask_intx_irq(struct irq_data *data)
{
	struct irq_desc *desc = irq_to_desc(data->irq);
	struct mobiveil_pcie *pcie;
	struct mobiveil_root_port *rp;
	unsigned long flags;
	u32 mask, shifted_val;

	pcie = irq_desc_get_chip_data(desc);
	rp = &pcie->rp;
	mask = 1 << ((data->hwirq + PAB_INTX_START) - 1);
	raw_spin_lock_irqsave(&rp->intx_mask_lock, flags);
	shifted_val = mobiveil_csr_readl(pcie, PAB_INTP_AMBA_MISC_ENB);
	shifted_val &= ~mask;
	mobiveil_csr_writel(pcie, shifted_val, PAB_INTP_AMBA_MISC_ENB);
	raw_spin_unlock_irqrestore(&rp->intx_mask_lock, flags);
}

static void mobiveil_unmask_intx_irq(struct irq_data *data)
{
	struct irq_desc *desc = irq_to_desc(data->irq);
	struct mobiveil_pcie *pcie;
	struct mobiveil_root_port *rp;
	unsigned long flags;
	u32 shifted_val, mask;

	pcie = irq_desc_get_chip_data(desc);
	rp = &pcie->rp;
	mask = 1 << ((data->hwirq + PAB_INTX_START) - 1);
	raw_spin_lock_irqsave(&rp->intx_mask_lock, flags);
	shifted_val = mobiveil_csr_readl(pcie, PAB_INTP_AMBA_MISC_ENB);
	shifted_val |= mask;
	mobiveil_csr_writel(pcie, shifted_val, PAB_INTP_AMBA_MISC_ENB);
	raw_spin_unlock_irqrestore(&rp->intx_mask_lock, flags);
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
	struct mobiveil_msi *msi = &pcie->rp.msi;
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
	struct mobiveil_msi *msi = &pcie->rp.msi;

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
	struct mobiveil_msi *msi = &pcie->rp.msi;

	mutex_init(&msi->lock);
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
	struct mobiveil_root_port *rp = &pcie->rp;
	int ret;

	/* setup INTx */
	rp->intx_domain = irq_domain_add_linear(node, PCI_NUM_INTX,
						&intx_domain_ops, pcie);

	if (!rp->intx_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return -ENOMEM;
	}

	raw_spin_lock_init(&rp->intx_mask_lock);

	/* setup MSI */
	ret = mobiveil_allocate_msi_domains(pcie);
	if (ret)
		return ret;

	return 0;
}

static int mobiveil_pcie_integrated_interrupt_init(struct mobiveil_pcie *pcie)
{
	struct platform_device *pdev = pcie->pdev;
	struct device *dev = &pdev->dev;
	struct mobiveil_root_port *rp = &pcie->rp;
	struct resource *res;
	int ret;

	/* map MSI config resource */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "apb_csr");
	pcie->apb_csr_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(pcie->apb_csr_base))
		return PTR_ERR(pcie->apb_csr_base);

	/* setup MSI hardware registers */
	mobiveil_pcie_enable_msi(pcie);

	rp->irq = platform_get_irq(pdev, 0);
	if (rp->irq < 0) {
		dev_err(dev, "failed to map IRQ: %d\n", rp->irq);
		return rp->irq;
	}

	/* initialize the IRQ domains */
	ret = mobiveil_pcie_init_irq_domain(pcie);
	if (ret) {
		dev_err(dev, "Failed creating IRQ Domain\n");
		return ret;
	}

	irq_set_chained_handler_and_data(rp->irq, mobiveil_pcie_isr, pcie);

	/* Enable interrupts */
	mobiveil_csr_writel(pcie, (PAB_INTP_INTX_MASK | PAB_INTP_MSI_MASK),
			    PAB_INTP_AMBA_MISC_ENB);


	return 0;
}

static int mobiveil_pcie_interrupt_init(struct mobiveil_pcie *pcie)
{
	struct mobiveil_root_port *rp = &pcie->rp;

	if (rp->ops->interrupt_init)
		return rp->ops->interrupt_init(pcie);

	return mobiveil_pcie_integrated_interrupt_init(pcie);
}

static bool mobiveil_pcie_is_bridge(struct mobiveil_pcie *pcie)
{
	u32 header_type;

	header_type = mobiveil_csr_readb(pcie, PCI_HEADER_TYPE);
	header_type &= 0x7f;

	return header_type == PCI_HEADER_TYPE_BRIDGE;
}

int mobiveil_pcie_host_probe(struct mobiveil_pcie *pcie)
{
	struct mobiveil_root_port *rp = &pcie->rp;
	struct pci_host_bridge *bridge = rp->bridge;
	struct device *dev = &pcie->pdev->dev;
	struct pci_bus *bus;
	struct pci_bus *child;
	int ret;

	ret = mobiveil_pcie_parse_dt(pcie);
	if (ret) {
		dev_err(dev, "Parsing DT failed, ret: %x\n", ret);
		return ret;
	}

	if (!mobiveil_pcie_is_bridge(pcie))
		return -ENODEV;

	/* parse the host bridge base addresses from the device tree file */
	ret = pci_parse_request_of_pci_ranges(dev, &bridge->windows,
					      &bridge->dma_ranges, NULL);
	if (ret) {
		dev_err(dev, "Getting bridge resources failed\n");
		return ret;
	}

	/*
	 * configure all inbound and outbound windows and prepare the RC for
	 * config access
	 */
	ret = mobiveil_host_init(pcie, false);
	if (ret) {
		dev_err(dev, "Failed to initialize host\n");
		return ret;
	}

	ret = mobiveil_pcie_interrupt_init(pcie);
	if (ret) {
		dev_err(dev, "Interrupt init failed\n");
		return ret;
	}

	/* Initialize bridge */
	bridge->dev.parent = dev;
	bridge->sysdata = pcie;
	bridge->busnr = rp->root_bus_nr;
	bridge->ops = &mobiveil_pcie_ops;
	bridge->map_irq = of_irq_parse_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;

	ret = mobiveil_bringup_link(pcie);
	if (ret) {
		dev_info(dev, "link bring-up failed\n");
		return ret;
	}

	/* setup the kernel resources for the newly added PCIe root bus */
	ret = pci_scan_root_bus_bridge(bridge);
	if (ret)
		return ret;

	bus = bridge->bus;

	pci_assign_unassigned_bus_resources(bus);
	list_for_each_entry(child, &bus->children, node)
		pcie_bus_configure_settings(child);
	pci_bus_add_devices(bus);

	return 0;
}
