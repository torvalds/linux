// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare PCIe host controller driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		https://www.samsung.com
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
static struct pci_ops dw_child_pcie_ops;

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
	unsigned long val;
	u32 status, num_ctrls;
	irqreturn_t ret = IRQ_NONE;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

	for (i = 0; i < num_ctrls; i++) {
		status = dw_pcie_readl_dbi(pci, PCIE_MSI_INTR0_STATUS +
					   (i * MSI_REG_CTRL_BLOCK_SIZE));
		if (!status)
			continue;

		ret = IRQ_HANDLED;
		val = status;
		pos = 0;
		while ((pos = find_next_bit(&val, MAX_MSI_IRQS_PER_CTRL,
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
EXPORT_SYMBOL_GPL(dw_handle_msi_irq);

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
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	unsigned int res, bit, ctrl;
	unsigned long flags;

	raw_spin_lock_irqsave(&pp->lock, flags);

	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	pp->irq_mask[ctrl] |= BIT(bit);
	dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_MASK + res, pp->irq_mask[ctrl]);

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static void dw_pci_bottom_unmask(struct irq_data *d)
{
	struct pcie_port *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	unsigned int res, bit, ctrl;
	unsigned long flags;

	raw_spin_lock_irqsave(&pp->lock, flags);

	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	pp->irq_mask[ctrl] &= ~BIT(bit);
	dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_MASK + res, pp->irq_mask[ctrl]);

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static void dw_pci_bottom_ack(struct irq_data *d)
{
	struct pcie_port *pp  = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	unsigned int res, bit, ctrl;

	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_STATUS + res, BIT(bit));
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
	struct pcie_port *pp = domain->host_data;
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

	irq_domain_update_bus_token(pp->irq_domain, DOMAIN_BUS_NEXUS);

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

	if (pp->msi_data) {
		struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
		struct device *dev = pci->dev;

		dma_unmap_single_attrs(dev, pp->msi_data, sizeof(pp->msi_msg),
				       DMA_FROM_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
	}
}

void dw_pcie_msi_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	u64 msi_target = (u64)pp->msi_data;

	if (!IS_ENABLED(CONFIG_PCI_MSI))
		return;

	/* Program the msi_data */
	dw_pcie_writel_dbi(pci, PCIE_MSI_ADDR_LO, lower_32_bits(msi_target));
	dw_pcie_writel_dbi(pci, PCIE_MSI_ADDR_HI, upper_32_bits(msi_target));
}
EXPORT_SYMBOL_GPL(dw_pcie_msi_init);

int dw_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource_entry *win;
	struct pci_host_bridge *bridge;
	struct resource *cfg_res;
	int ret;

	raw_spin_lock_init(&pci->pp.lock);

	cfg_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config");
	if (cfg_res) {
		pp->cfg0_size = resource_size(cfg_res);
		pp->cfg0_base = cfg_res->start;
	} else if (!pp->va_cfg0_base) {
		dev_err(dev, "Missing *config* reg space\n");
	}

	bridge = devm_pci_alloc_host_bridge(dev, 0);
	if (!bridge)
		return -ENOMEM;

	pp->bridge = bridge;

	/* Get the I/O and memory ranges from DT */
	resource_list_for_each_entry(win, &bridge->windows) {
		switch (resource_type(win->res)) {
		case IORESOURCE_IO:
			pp->io_size = resource_size(win->res);
			pp->io_bus_addr = win->res->start - win->offset;
			pp->io_base = pci_pio_to_address(win->res->start);
			break;
		case 0:
			dev_err(dev, "Missing *config* reg space\n");
			pp->cfg0_size = resource_size(win->res);
			pp->cfg0_base = win->res->start;
			if (!pci->dbi_base) {
				pci->dbi_base = devm_pci_remap_cfgspace(dev,
								pp->cfg0_base,
								pp->cfg0_size);
				if (!pci->dbi_base) {
					dev_err(dev, "Error with ioremap\n");
					return -ENOMEM;
				}
			}
			break;
		}
	}

	if (!pp->va_cfg0_base) {
		pp->va_cfg0_base = devm_pci_remap_cfgspace(dev,
					pp->cfg0_base, pp->cfg0_size);
		if (!pp->va_cfg0_base) {
			dev_err(dev, "Error with ioremap in function\n");
			return -ENOMEM;
		}
	}

	ret = of_property_read_u32(np, "num-viewport", &pci->num_viewport);
	if (ret)
		pci->num_viewport = 2;

	if (pci->link_gen < 1)
		pci->link_gen = of_pci_get_max_link_speed(np);

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

			pp->msi_data = dma_map_single_attrs(pci->dev, &pp->msi_msg,
						      sizeof(pp->msi_msg),
						      DMA_FROM_DEVICE,
						      DMA_ATTR_SKIP_CPU_SYNC);
			if (dma_mapping_error(pci->dev, pp->msi_data)) {
				dev_err(pci->dev, "Failed to map MSI data\n");
				pp->msi_data = 0;
				goto err_free_msi;
			}
		} else {
			ret = pp->ops->msi_host_init(pp);
			if (ret < 0)
				return ret;
		}
	}

	/* Set default bus ops */
	bridge->ops = &dw_pcie_ops;
	bridge->child_ops = &dw_child_pcie_ops;

	if (pp->ops->host_init) {
		ret = pp->ops->host_init(pp);
		if (ret)
			goto err_free_msi;
	}

	bridge->sysdata = pp;

	ret = pci_host_probe(bridge);
	if (!ret)
		return 0;

err_free_msi:
	if (pci_msi_enabled() && !pp->ops->msi_host_init)
		dw_pcie_free_msi(pp);
	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie_host_init);

void dw_pcie_host_deinit(struct pcie_port *pp)
{
	pci_stop_root_bus(pp->bridge->bus);
	pci_remove_root_bus(pp->bridge->bus);
	if (pci_msi_enabled() && !pp->ops->msi_host_init)
		dw_pcie_free_msi(pp);
}
EXPORT_SYMBOL_GPL(dw_pcie_host_deinit);

static void __iomem *dw_pcie_other_conf_map_bus(struct pci_bus *bus,
						unsigned int devfn, int where)
{
	int type;
	u32 busdev;
	struct pcie_port *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	/*
	 * Checking whether the link is up here is a last line of defense
	 * against platforms that forward errors on the system bus as
	 * SError upon PCI configuration transactions issued when the link
	 * is down. This check is racy by definition and does not stop
	 * the system from triggering an SError if the link goes down
	 * after this check is performed.
	 */
	if (!dw_pcie_link_up(pci))
		return NULL;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (pci_is_root_bus(bus->parent))
		type = PCIE_ATU_TYPE_CFG0;
	else
		type = PCIE_ATU_TYPE_CFG1;


	dw_pcie_prog_outbound_atu(pci, 0, type, pp->cfg0_base, busdev, pp->cfg0_size);

	return pp->va_cfg0_base + where;
}

static int dw_pcie_rd_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *val)
{
	int ret;
	struct pcie_port *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	ret = pci_generic_config_read(bus, devfn, where, size, val);

	if (!ret && (pci->iatu_unroll_enabled & DWC_IATU_IOCFG_SHARED))
		dw_pcie_prog_outbound_atu(pci, 0, PCIE_ATU_TYPE_IO, pp->io_base,
					  pp->io_bus_addr, pp->io_size);

	return ret;
}

static int dw_pcie_wr_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	int ret;
	struct pcie_port *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	ret = pci_generic_config_write(bus, devfn, where, size, val);

	if (!ret && (pci->iatu_unroll_enabled & DWC_IATU_IOCFG_SHARED))
		dw_pcie_prog_outbound_atu(pci, 0, PCIE_ATU_TYPE_IO, pp->io_base,
					  pp->io_bus_addr, pp->io_size);

	return ret;
}

static struct pci_ops dw_child_pcie_ops = {
	.map_bus = dw_pcie_other_conf_map_bus,
	.read = dw_pcie_rd_other_conf,
	.write = dw_pcie_wr_other_conf,
};

void __iomem *dw_pcie_own_conf_map_bus(struct pci_bus *bus, unsigned int devfn, int where)
{
	struct pcie_port *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	if (PCI_SLOT(devfn) > 0)
		return NULL;

	return pci->dbi_base + where;
}
EXPORT_SYMBOL_GPL(dw_pcie_own_conf_map_bus);

static struct pci_ops dw_pcie_ops = {
	.map_bus = dw_pcie_own_conf_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
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

	if (pci_msi_enabled() && !pp->ops->msi_host_init) {
		num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

		/* Initialize IRQ Status array */
		for (ctrl = 0; ctrl < num_ctrls; ctrl++) {
			pp->irq_mask[ctrl] = ~0;
			dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_MASK +
					    (ctrl * MSI_REG_CTRL_BLOCK_SIZE),
					    pp->irq_mask[ctrl]);
			dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_ENABLE +
					    (ctrl * MSI_REG_CTRL_BLOCK_SIZE),
					    ~0);
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
	 * If the platform provides its own child bus config accesses, it means
	 * the platform uses its own address translation component rather than
	 * ATU, so we should not program the ATU here.
	 */
	if (pp->bridge->child_ops == &dw_child_pcie_ops) {
		int atu_idx = 0;
		struct resource_entry *entry;

		/* Get last memory resource entry */
		resource_list_for_each_entry(entry, &pp->bridge->windows) {
			if (resource_type(entry->res) != IORESOURCE_MEM)
				continue;

			if (pci->num_viewport <= ++atu_idx)
				break;

			dw_pcie_prog_outbound_atu(pci, atu_idx,
						  PCIE_ATU_TYPE_MEM, entry->res->start,
						  entry->res->start - entry->offset,
						  resource_size(entry->res));
		}

		if (pp->io_size) {
			if (pci->num_viewport > ++atu_idx)
				dw_pcie_prog_outbound_atu(pci, atu_idx,
							  PCIE_ATU_TYPE_IO, pp->io_base,
							  pp->io_bus_addr, pp->io_size);
			else
				pci->iatu_unroll_enabled |= DWC_IATU_IOCFG_SHARED;
		}

		if (pci->num_viewport <= atu_idx)
			dev_warn(pci->dev, "Resources exceed number of ATU entries (%d)",
				 pci->num_viewport);
	}

	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0);

	/* Program correct class for RC */
	dw_pcie_writew_dbi(pci, PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_PCI);

	val = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	dw_pcie_dbi_ro_wr_dis(pci);
}
EXPORT_SYMBOL_GPL(dw_pcie_setup_rc);
