// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare PCIe host controller driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci_regs.h>
#include <linux/platform_device.h>

#include "../../pci.h"
#include "pcie-designware.h"

static struct pci_ops dw_pcie_ops;

static int dw_pcie_rd_own_conf(struct pcie_port *pp, int where, int size,
			       u32 *val)
{
	struct dw_pcie *pci;

	if (pp->ops->rd_own_conf)
		return pp->ops->rd_own_conf(pp, where, size, val);

	pci = to_dw_pcie_from_pp(pp);
	return dw_pcie_read(pci->dbi_base + where, size, val);
}

static int dw_pcie_wr_own_conf(struct pcie_port *pp, int where, int size,
			       u32 val)
{
	struct dw_pcie *pci;

	if (pp->ops->wr_own_conf)
		return pp->ops->wr_own_conf(pp, where, size, val);

	pci = to_dw_pcie_from_pp(pp);
	return dw_pcie_write(pci->dbi_base + where, size, val);
}

static void dw_msi_ack_irq(struct irq_data *d)
{
	irq_chip_ack_parent(d);
}

static void dw_msi_mask_irq(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void dw_msi_unmask_irq(struct irq_data *d)
{
	pci_msi_unmask_irq(d);
	irq_chip_unmask_parent(d);
}

static struct irq_chip dw_pcie_msi_irq_chip = {
	.name = "PCI-MSI",
	.irq_ack = dw_msi_ack_irq,
	.irq_mask = dw_msi_mask_irq,
	.irq_unmask = dw_msi_unmask_irq,
};

static struct msi_domain_info dw_pcie_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_PCI_MSIX | MSI_FLAG_MULTI_PCI_MSI),
	.chip	= &dw_pcie_msi_irq_chip,
};

/* MSI int handler */
irqreturn_t dw_handle_msi_irq(struct pcie_port *pp)
{
	int i, pos, irq;
	u32 val, num_ctrls;
	irqreturn_t ret = IRQ_NONE;

	num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

	for (i = 0; i < num_ctrls; i++) {
		dw_pcie_rd_own_conf(pp, PCIE_MSI_INTR0_STATUS +
					(i * MSI_REG_CTRL_BLOCK_SIZE),
				    4, &val);
		if (!val)
			continue;

		ret = IRQ_HANDLED;
		pos = 0;
		while ((pos = find_next_bit((unsigned long *) &val,
					    MAX_MSI_IRQS_PER_CTRL,
					    pos)) != MAX_MSI_IRQS_PER_CTRL) {
			irq = irq_find_mapping(pp->irq_domain,
					       (i * MAX_MSI_IRQS_PER_CTRL) +
					       pos);
			generic_handle_irq(irq);
			pos++;
		}
	}

	return ret;
}

/* Chained MSI interrupt service routine */
static void dw_chained_msi_isr(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct pcie_port *pp;

	chained_irq_enter(chip, desc);

	pp = irq_desc_get_handler_data(desc);
	dw_handle_msi_irq(pp);

	chained_irq_exit(chip, desc);
}

static void dw_pci_setup_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	struct pcie_port *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	u64 msi_target;

	msi_target = (u64)pp->msi_data;

	msg->address_lo = lower_32_bits(msi_target);
	msg->address_hi = upper_32_bits(msi_target);

	msg->data = d->hwirq;

	dev_dbg(pci->dev, "msi#%d address_hi %#x address_lo %#x\n",
		(int)d->hwirq, msg->address_hi, msg->address_lo);
}

static int dw_pci_msi_set_affinity(struct irq_data *d,
				   const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static void dw_pci_bottom_mask(struct irq_data *d)
{
	struct pcie_port *pp = irq_data_get_irq_chip_data(d);
	unsigned int res, bit, ctrl;
	unsigned long flags;

	raw_spin_lock_irqsave(&pp->lock, flags);

	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	pp->irq_mask[ctrl] |= BIT(bit);
	dw_pcie_wr_own_conf(pp, PCIE_MSI_INTR0_MASK + res, 4,
			    pp->irq_mask[ctrl]);

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static void dw_pci_bottom_unmask(struct irq_data *d)
{
	struct pcie_port *pp = irq_data_get_irq_chip_data(d);
	unsigned int res, bit, ctrl;
	unsigned long flags;

	raw_spin_lock_irqsave(&pp->lock, flags);

	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	pp->irq_mask[ctrl] &= ~BIT(bit);
	dw_pcie_wr_own_conf(pp, PCIE_MSI_INTR0_MASK + res, 4,
			    pp->irq_mask[ctrl]);

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static void dw_pci_bottom_ack(struct irq_data *d)
{
	struct pcie_port *pp  = irq_data_get_irq_chip_data(d);
	unsigned int res, bit, ctrl;

	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	dw_pcie_wr_own_conf(pp, PCIE_MSI_INTR0_STATUS + res, 4, BIT(bit));
}

static struct irq_chip dw_pci_msi_bottom_irq_chip = {
	.name = "DWPCI-MSI",
	.irq_ack = dw_pci_bottom_ack,
	.irq_compose_msi_msg = dw_pci_setup_msi_msg,
	.irq_set_affinity = dw_pci_msi_set_affinity,
	.irq_mask = dw_pci_bottom_mask,
	.irq_unmask = dw_pci_bottom_unmask,
};

static int dw_pcie_irq_domain_alloc(struct irq_domain *domain,
				    unsigned int virq, unsigned int nr_irqs,
				    void *args)
{
	struct pcie_port *pp = domain->host_data;
	unsigned long flags;
	u32 i;
	int bit;

	raw_spin_lock_irqsave(&pp->lock, flags);

	bit = bitmap_find_free_region(pp->msi_irq_in_use, pp->num_vectors,
				      order_base_2(nr_irqs));

	raw_spin_unlock_irqrestore(&pp->lock, flags);

	if (bit < 0)
		return -ENOSPC;

	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_info(domain, virq + i, bit + i,
				    pp->msi_irq_chip,
				    pp, handle_edge_irq,
				    NULL, NULL);

	return 0;
}

static void dw_pcie_irq_domain_free(struct irq_domain *domain,
				    unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct pcie_port *pp = irq_data_get_irq_chip_data(d);
	unsigned long flags;

	raw_spin_lock_irqsave(&pp->lock, flags);

	bitmap_release_region(pp->msi_irq_in_use, d->hwirq,
			      order_base_2(nr_irqs));

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static const struct irq_domain_ops dw_pcie_msi_domain_ops = {
	.alloc	= dw_pcie_irq_domain_alloc,
	.free	= dw_pcie_irq_domain_free,
};

int dw_pcie_allocate_domains(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct fwnode_handle *fwnode = of_node_to_fwnode(pci->dev->of_node);

	pp->irq_domain = irq_domain_create_linear(fwnode, pp->num_vectors,
					       &dw_pcie_msi_domain_ops, pp);
	if (!pp->irq_domain) {
		dev_err(pci->dev, "Failed to create IRQ domain\n");
		return -ENOMEM;
	}

	pp->msi_domain = pci_msi_create_irq_domain(fwnode,
						   &dw_pcie_msi_domain_info,
						   pp->irq_domain);
	if (!pp->msi_domain) {
		dev_err(pci->dev, "Failed to create MSI domain\n");
		irq_domain_remove(pp->irq_domain);
		return -ENOMEM;
	}

	return 0;
}

void dw_pcie_free_msi(struct pcie_port *pp)
{
	if (pp->msi_irq) {
		irq_set_chained_handler(pp->msi_irq, NULL);
		irq_set_handler_data(pp->msi_irq, NULL);
	}

	irq_domain_remove(pp->msi_domain);
	irq_domain_remove(pp->irq_domain);

	if (pp->msi_page)
		__free_page(pp->msi_page);
}

void dw_pcie_msi_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	u64 msi_target;

	pp->msi_page = alloc_page(GFP_KERNEL);
	pp->msi_data = dma_map_page(dev, pp->msi_page, 0, PAGE_SIZE,
				    DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, pp->msi_data)) {
		dev_err(dev, "Failed to map MSI data\n");
		__free_page(pp->msi_page);
		pp->msi_page = NULL;
		return;
	}
	msi_target = (u64)pp->msi_data;

	/* Program the msi_data */
	dw_pcie_wr_own_conf(pp, PCIE_MSI_ADDR_LO, 4,
			    lower_32_bits(msi_target));
	dw_pcie_wr_own_conf(pp, PCIE_MSI_ADDR_HI, 4,
			    upper_32_bits(msi_target));
}
EXPORT_SYMBOL_GPL(dw_pcie_msi_init);

int dw_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource_entry *win, *tmp;
	struct pci_bus *child;
	struct pci_host_bridge *bridge;
	struct resource *cfg_res;
	u32 hdr_type;
	int ret;

	raw_spin_lock_init(&pci->pp.lock);

	cfg_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config");
	if (cfg_res) {
		pp->cfg0_size = resource_size(cfg_res) >> 1;
		pp->cfg1_size = resource_size(cfg_res) >> 1;
		pp->cfg0_base = cfg_res->start;
		pp->cfg1_base = cfg_res->start + pp->cfg0_size;
	} else if (!pp->va_cfg0_base) {
		dev_err(dev, "Missing *config* reg space\n");
	}

	bridge = devm_pci_alloc_host_bridge(dev, 0);
	if (!bridge)
		return -ENOMEM;

	ret = devm_of_pci_get_host_bridge_resources(dev, 0, 0xff,
					&bridge->windows, &pp->io_base);
	if (ret)
		return ret;

	ret = devm_request_pci_bus_resources(dev, &bridge->windows);
	if (ret)
		return ret;

	/* Get the I/O and memory ranges from DT */
	resource_list_for_each_entry_safe(win, tmp, &bridge->windows) {
		switch (resource_type(win->res)) {
		case IORESOURCE_IO:
			ret = devm_pci_remap_iospace(dev, win->res,
						     pp->io_base);
			if (ret) {
				dev_warn(dev, "Error %d: failed to map resource %pR\n",
					 ret, win->res);
				resource_list_destroy_entry(win);
			} else {
				pp->io = win->res;
				pp->io->name = "I/O";
				pp->io_size = resource_size(pp->io);
				pp->io_bus_addr = pp->io->start - win->offset;
			}
			break;
		case IORESOURCE_MEM:
			pp->mem = win->res;
			pp->mem->name = "MEM";
			pp->mem_size = resource_size(pp->mem);
			pp->mem_bus_addr = pp->mem->start - win->offset;
			break;
		case 0:
			pp->cfg = win->res;
			pp->cfg0_size = resource_size(pp->cfg) >> 1;
			pp->cfg1_size = resource_size(pp->cfg) >> 1;
			pp->cfg0_base = pp->cfg->start;
			pp->cfg1_base = pp->cfg->start + pp->cfg0_size;
			break;
		case IORESOURCE_BUS:
			pp->busn = win->res;
			break;
		}
	}

	if (!pci->dbi_base) {
		pci->dbi_base = devm_pci_remap_cfgspace(dev,
						pp->cfg->start,
						resource_size(pp->cfg));
		if (!pci->dbi_base) {
			dev_err(dev, "Error with ioremap\n");
			return -ENOMEM;
		}
	}

	pp->mem_base = pp->mem->start;

	if (!pp->va_cfg0_base) {
		pp->va_cfg0_base = devm_pci_remap_cfgspace(dev,
					pp->cfg0_base, pp->cfg0_size);
		if (!pp->va_cfg0_base) {
			dev_err(dev, "Error with ioremap in function\n");
			return -ENOMEM;
		}
	}

	if (!pp->va_cfg1_base) {
		pp->va_cfg1_base = devm_pci_remap_cfgspace(dev,
						pp->cfg1_base,
						pp->cfg1_size);
		if (!pp->va_cfg1_base) {
			dev_err(dev, "Error with ioremap\n");
			return -ENOMEM;
		}
	}

	ret = of_property_read_u32(np, "num-viewport", &pci->num_viewport);
	if (ret)
		pci->num_viewport = 2;

	if (pci_msi_enabled()) {
		/*
		 * If a specific SoC driver needs to change the
		 * default number of vectors, it needs to implement
		 * the set_num_vectors callback.
		 */
		if (!pp->ops->set_num_vectors) {
			pp->num_vectors = MSI_DEF_NUM_VECTORS;
		} else {
			pp->ops->set_num_vectors(pp);

			if (pp->num_vectors > MAX_MSI_IRQS ||
			    pp->num_vectors == 0) {
				dev_err(dev,
					"Invalid number of vectors\n");
				return -EINVAL;
			}
		}

		if (!pp->ops->msi_host_init) {
			pp->msi_irq_chip = &dw_pci_msi_bottom_irq_chip;

			ret = dw_pcie_allocate_domains(pp);
			if (ret)
				return ret;

			if (pp->msi_irq)
				irq_set_chained_handler_and_data(pp->msi_irq,
							    dw_chained_msi_isr,
							    pp);
		} else {
			ret = pp->ops->msi_host_init(pp);
			if (ret < 0)
				return ret;
		}
	}

	if (pp->ops->host_init) {
		ret = pp->ops->host_init(pp);
		if (ret)
			goto err_free_msi;
	}

	ret = dw_pcie_rd_own_conf(pp, PCI_HEADER_TYPE, 1, &hdr_type);
	if (ret != PCIBIOS_SUCCESSFUL) {
		dev_err(pci->dev, "Failed reading PCI_HEADER_TYPE cfg space reg (ret: 0x%x)\n",
			ret);
		ret = pcibios_err_to_errno(ret);
		goto err_free_msi;
	}
	if (hdr_type != PCI_HEADER_TYPE_BRIDGE) {
		dev_err(pci->dev,
			"PCIe controller is not set to bridge type (hdr_type: 0x%x)!\n",
			hdr_type);
		ret = -EIO;
		goto err_free_msi;
	}

	pp->root_bus_nr = pp->busn->start;

	bridge->dev.parent = dev;
	bridge->sysdata = pp;
	bridge->busnr = pp->root_bus_nr;
	bridge->ops = &dw_pcie_ops;
	bridge->map_irq = of_irq_parse_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;

	ret = pci_scan_root_bus_bridge(bridge);
	if (ret)
		goto err_free_msi;

	pp->root_bus = bridge->bus;

	if (pp->ops->scan_bus)
		pp->ops->scan_bus(pp);

	pci_bus_size_bridges(pp->root_bus);
	pci_bus_assign_resources(pp->root_bus);

	list_for_each_entry(child, &pp->root_bus->children, node)
		pcie_bus_configure_settings(child);

	pci_bus_add_devices(pp->root_bus);
	return 0;

err_free_msi:
	if (pci_msi_enabled() && !pp->ops->msi_host_init)
		dw_pcie_free_msi(pp);
	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie_host_init);

void dw_pcie_host_deinit(struct pcie_port *pp)
{
	pci_stop_root_bus(pp->root_bus);
	pci_remove_root_bus(pp->root_bus);
	if (pci_msi_enabled() && !pp->ops->msi_host_init)
		dw_pcie_free_msi(pp);
}
EXPORT_SYMBOL_GPL(dw_pcie_host_deinit);

static int dw_pcie_access_other_conf(struct pcie_port *pp, struct pci_bus *bus,
				     u32 devfn, int where, int size, u32 *val,
				     bool write)
{
	int ret, type;
	u32 busdev, cfg_size;
	u64 cpu_addr;
	void __iomem *va_cfg_base;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (bus->parent->number == pp->root_bus_nr) {
		type = PCIE_ATU_TYPE_CFG0;
		cpu_addr = pp->cfg0_base;
		cfg_size = pp->cfg0_size;
		va_cfg_base = pp->va_cfg0_base;
	} else {
		type = PCIE_ATU_TYPE_CFG1;
		cpu_addr = pp->cfg1_base;
		cfg_size = pp->cfg1_size;
		va_cfg_base = pp->va_cfg1_base;
	}

	dw_pcie_prog_outbound_atu(pci, PCIE_ATU_REGION_INDEX1,
				  type, cpu_addr,
				  busdev, cfg_size);
	if (write)
		ret = dw_pcie_write(va_cfg_base + where, size, *val);
	else
		ret = dw_pcie_read(va_cfg_base + where, size, val);

	if (pci->num_viewport <= 2)
		dw_pcie_prog_outbound_atu(pci, PCIE_ATU_REGION_INDEX1,
					  PCIE_ATU_TYPE_IO, pp->io_base,
					  pp->io_bus_addr, pp->io_size);

	return ret;
}

static int dw_pcie_rd_other_conf(struct pcie_port *pp, struct pci_bus *bus,
				 u32 devfn, int where, int size, u32 *val)
{
	if (pp->ops->rd_other_conf)
		return pp->ops->rd_other_conf(pp, bus, devfn, where,
					      size, val);

	return dw_pcie_access_other_conf(pp, bus, devfn, where, size, val,
					 false);
}

static int dw_pcie_wr_other_conf(struct pcie_port *pp, struct pci_bus *bus,
				 u32 devfn, int where, int size, u32 val)
{
	if (pp->ops->wr_other_conf)
		return pp->ops->wr_other_conf(pp, bus, devfn, where,
					      size, val);

	return dw_pcie_access_other_conf(pp, bus, devfn, where, size, &val,
					 true);
}

static int dw_pcie_valid_device(struct pcie_port *pp, struct pci_bus *bus,
				int dev)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	/* If there is no link, then there is no device */
	if (bus->number != pp->root_bus_nr) {
		if (!dw_pcie_link_up(pci))
			return 0;
	}

	/* Access only one slot on each root port */
	if (bus->number == pp->root_bus_nr && dev > 0)
		return 0;

	return 1;
}

static int dw_pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
			   int size, u32 *val)
{
	struct pcie_port *pp = bus->sysdata;

	if (!dw_pcie_valid_device(pp, bus, PCI_SLOT(devfn))) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (bus->number == pp->root_bus_nr)
		return dw_pcie_rd_own_conf(pp, where, size, val);

	return dw_pcie_rd_other_conf(pp, bus, devfn, where, size, val);
}

static int dw_pcie_wr_conf(struct pci_bus *bus, u32 devfn,
			   int where, int size, u32 val)
{
	struct pcie_port *pp = bus->sysdata;

	if (!dw_pcie_valid_device(pp, bus, PCI_SLOT(devfn)))
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (bus->number == pp->root_bus_nr)
		return dw_pcie_wr_own_conf(pp, where, size, val);

	return dw_pcie_wr_other_conf(pp, bus, devfn, where, size, val);
}

static struct pci_ops dw_pcie_ops = {
	.read = dw_pcie_rd_conf,
	.write = dw_pcie_wr_conf,
};

void dw_pcie_setup_rc(struct pcie_port *pp)
{
	u32 val, ctrl, num_ctrls;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	/*
	 * Enable DBI read-only registers for writing/updating configuration.
	 * Write permission gets disabled towards the end of this function.
	 */
	dw_pcie_dbi_ro_wr_en(pci);

	dw_pcie_setup(pci);

	if (!pp->ops->msi_host_init) {
		num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

		/* Initialize IRQ Status array */
		for (ctrl = 0; ctrl < num_ctrls; ctrl++) {
			pp->irq_mask[ctrl] = ~0;
			dw_pcie_wr_own_conf(pp, PCIE_MSI_INTR0_MASK +
					    (ctrl * MSI_REG_CTRL_BLOCK_SIZE),
					    4, pp->irq_mask[ctrl]);
			dw_pcie_wr_own_conf(pp, PCIE_MSI_INTR0_ENABLE +
					    (ctrl * MSI_REG_CTRL_BLOCK_SIZE),
					    4, ~0);
		}
	}

	/* Setup RC BARs */
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0x00000004);
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_1, 0x00000000);

	/* Setup interrupt pins */
	val = dw_pcie_readl_dbi(pci, PCI_INTERRUPT_LINE);
	val &= 0xffff00ff;
	val |= 0x00000100;
	dw_pcie_writel_dbi(pci, PCI_INTERRUPT_LINE, val);

	/* Setup bus numbers */
	val = dw_pcie_readl_dbi(pci, PCI_PRIMARY_BUS);
	val &= 0xff000000;
	val |= 0x00ff0100;
	dw_pcie_writel_dbi(pci, PCI_PRIMARY_BUS, val);

	/* Setup command register */
	val = dw_pcie_readl_dbi(pci, PCI_COMMAND);
	val &= 0xffff0000;
	val |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_MASTER | PCI_COMMAND_SERR;
	dw_pcie_writel_dbi(pci, PCI_COMMAND, val);

	/*
	 * If the platform provides ->rd_other_conf, it means the platform
	 * uses its own address translation component rather than ATU, so
	 * we should not program the ATU here.
	 */
	if (!pp->ops->rd_other_conf) {
		dw_pcie_prog_outbound_atu(pci, PCIE_ATU_REGION_INDEX0,
					  PCIE_ATU_TYPE_MEM, pp->mem_base,
					  pp->mem_bus_addr, pp->mem_size);
		if (pci->num_viewport > 2)
			dw_pcie_prog_outbound_atu(pci, PCIE_ATU_REGION_INDEX2,
						  PCIE_ATU_TYPE_IO, pp->io_base,
						  pp->io_bus_addr, pp->io_size);
	}

	dw_pcie_wr_own_conf(pp, PCI_BASE_ADDRESS_0, 4, 0);

	/* Program correct class for RC */
	dw_pcie_wr_own_conf(pp, PCI_CLASS_DEVICE, 2, PCI_CLASS_BRIDGE_PCI);

	dw_pcie_rd_own_conf(pp, PCIE_LINK_WIDTH_SPEED_CONTROL, 4, &val);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_wr_own_conf(pp, PCIE_LINK_WIDTH_SPEED_CONTROL, 4, val);

	dw_pcie_dbi_ro_wr_dis(pci);
}
EXPORT_SYMBOL_GPL(dw_pcie_setup_rc);
