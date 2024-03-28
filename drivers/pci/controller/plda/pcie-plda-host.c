// SPDX-License-Identifier: GPL-2.0
/*
 * PLDA PCIe XpressRich host controller driver
 *
 * Copyright (C) 2023 Microchip Co. Ltd
 *		      StarFive Co. Ltd
 *
 * Author: Daire McNamara <daire.mcnamara@microchip.com>
 */

#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/pci_regs.h>
#include <linux/pci-ecam.h>

#include "pcie-plda.h"

void __iomem *plda_pcie_map_bus(struct pci_bus *bus, unsigned int devfn,
				int where)
{
	struct plda_pcie_rp *pcie = bus->sysdata;

	return pcie->config_base + PCIE_ECAM_OFFSET(bus->number, devfn, where);
}
EXPORT_SYMBOL_GPL(plda_pcie_map_bus);

static void plda_handle_msi(struct irq_desc *desc)
{
	struct plda_pcie_rp *port = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct device *dev = port->dev;
	struct plda_msi *msi = &port->msi;
	void __iomem *bridge_base_addr = port->bridge_addr;
	unsigned long status;
	u32 bit;
	int ret;

	chained_irq_enter(chip, desc);

	status = readl_relaxed(bridge_base_addr + ISTATUS_LOCAL);
	if (status & PM_MSI_INT_MSI_MASK) {
		writel_relaxed(status & PM_MSI_INT_MSI_MASK,
			       bridge_base_addr + ISTATUS_LOCAL);
		status = readl_relaxed(bridge_base_addr + ISTATUS_MSI);
		for_each_set_bit(bit, &status, msi->num_vectors) {
			ret = generic_handle_domain_irq(msi->dev_domain, bit);
			if (ret)
				dev_err_ratelimited(dev, "bad MSI IRQ %d\n",
						    bit);
		}
	}

	chained_irq_exit(chip, desc);
}

static void plda_msi_bottom_irq_ack(struct irq_data *data)
{
	struct plda_pcie_rp *port = irq_data_get_irq_chip_data(data);
	void __iomem *bridge_base_addr = port->bridge_addr;
	u32 bitpos = data->hwirq;

	writel_relaxed(BIT(bitpos), bridge_base_addr + ISTATUS_MSI);
}

static void plda_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct plda_pcie_rp *port = irq_data_get_irq_chip_data(data);
	phys_addr_t addr = port->msi.vector_phy;

	msg->address_lo = lower_32_bits(addr);
	msg->address_hi = upper_32_bits(addr);
	msg->data = data->hwirq;

	dev_dbg(port->dev, "msi#%x address_hi %#x address_lo %#x\n",
		(int)data->hwirq, msg->address_hi, msg->address_lo);
}

static int plda_msi_set_affinity(struct irq_data *irq_data,
				 const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static struct irq_chip plda_msi_bottom_irq_chip = {
	.name = "PLDA MSI",
	.irq_ack = plda_msi_bottom_irq_ack,
	.irq_compose_msi_msg = plda_compose_msi_msg,
	.irq_set_affinity = plda_msi_set_affinity,
};

static int plda_irq_msi_domain_alloc(struct irq_domain *domain,
				     unsigned int virq,
				     unsigned int nr_irqs,
				     void *args)
{
	struct plda_pcie_rp *port = domain->host_data;
	struct plda_msi *msi = &port->msi;
	unsigned long bit;

	mutex_lock(&msi->lock);
	bit = find_first_zero_bit(msi->used, msi->num_vectors);
	if (bit >= msi->num_vectors) {
		mutex_unlock(&msi->lock);
		return -ENOSPC;
	}

	set_bit(bit, msi->used);

	irq_domain_set_info(domain, virq, bit, &plda_msi_bottom_irq_chip,
			    domain->host_data, handle_edge_irq, NULL, NULL);

	mutex_unlock(&msi->lock);

	return 0;
}

static void plda_irq_msi_domain_free(struct irq_domain *domain,
				     unsigned int virq,
				     unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct plda_pcie_rp *port = irq_data_get_irq_chip_data(d);
	struct plda_msi *msi = &port->msi;

	mutex_lock(&msi->lock);

	if (test_bit(d->hwirq, msi->used))
		__clear_bit(d->hwirq, msi->used);
	else
		dev_err(port->dev, "trying to free unused MSI%lu\n", d->hwirq);

	mutex_unlock(&msi->lock);
}

static const struct irq_domain_ops msi_domain_ops = {
	.alloc	= plda_irq_msi_domain_alloc,
	.free	= plda_irq_msi_domain_free,
};

static struct irq_chip plda_msi_irq_chip = {
	.name = "PLDA PCIe MSI",
	.irq_ack = irq_chip_ack_parent,
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static struct msi_domain_info plda_msi_domain_info = {
	.flags = (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		  MSI_FLAG_PCI_MSIX),
	.chip = &plda_msi_irq_chip,
};

static int plda_allocate_msi_domains(struct plda_pcie_rp *port)
{
	struct device *dev = port->dev;
	struct fwnode_handle *fwnode = of_node_to_fwnode(dev->of_node);
	struct plda_msi *msi = &port->msi;

	mutex_init(&port->msi.lock);

	msi->dev_domain = irq_domain_add_linear(NULL, msi->num_vectors,
						&msi_domain_ops, port);
	if (!msi->dev_domain) {
		dev_err(dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	msi->msi_domain = pci_msi_create_irq_domain(fwnode,
						    &plda_msi_domain_info,
						    msi->dev_domain);
	if (!msi->msi_domain) {
		dev_err(dev, "failed to create MSI domain\n");
		irq_domain_remove(msi->dev_domain);
		return -ENOMEM;
	}

	return 0;
}

static void plda_handle_intx(struct irq_desc *desc)
{
	struct plda_pcie_rp *port = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct device *dev = port->dev;
	void __iomem *bridge_base_addr = port->bridge_addr;
	unsigned long status;
	u32 bit;
	int ret;

	chained_irq_enter(chip, desc);

	status = readl_relaxed(bridge_base_addr + ISTATUS_LOCAL);
	if (status & PM_MSI_INT_INTX_MASK) {
		status &= PM_MSI_INT_INTX_MASK;
		status >>= PM_MSI_INT_INTX_SHIFT;
		for_each_set_bit(bit, &status, PCI_NUM_INTX) {
			ret = generic_handle_domain_irq(port->intx_domain, bit);
			if (ret)
				dev_err_ratelimited(dev, "bad INTx IRQ %d\n",
						    bit);
		}
	}

	chained_irq_exit(chip, desc);
}

static void plda_ack_intx_irq(struct irq_data *data)
{
	struct plda_pcie_rp *port = irq_data_get_irq_chip_data(data);
	void __iomem *bridge_base_addr = port->bridge_addr;
	u32 mask = BIT(data->hwirq + PM_MSI_INT_INTX_SHIFT);

	writel_relaxed(mask, bridge_base_addr + ISTATUS_LOCAL);
}

static void plda_mask_intx_irq(struct irq_data *data)
{
	struct plda_pcie_rp *port = irq_data_get_irq_chip_data(data);
	void __iomem *bridge_base_addr = port->bridge_addr;
	unsigned long flags;
	u32 mask = BIT(data->hwirq + PM_MSI_INT_INTX_SHIFT);
	u32 val;

	raw_spin_lock_irqsave(&port->lock, flags);
	val = readl_relaxed(bridge_base_addr + IMASK_LOCAL);
	val &= ~mask;
	writel_relaxed(val, bridge_base_addr + IMASK_LOCAL);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static void plda_unmask_intx_irq(struct irq_data *data)
{
	struct plda_pcie_rp *port = irq_data_get_irq_chip_data(data);
	void __iomem *bridge_base_addr = port->bridge_addr;
	unsigned long flags;
	u32 mask = BIT(data->hwirq + PM_MSI_INT_INTX_SHIFT);
	u32 val;

	raw_spin_lock_irqsave(&port->lock, flags);
	val = readl_relaxed(bridge_base_addr + IMASK_LOCAL);
	val |= mask;
	writel_relaxed(val, bridge_base_addr + IMASK_LOCAL);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static struct irq_chip plda_intx_irq_chip = {
	.name = "PLDA PCIe INTx",
	.irq_ack = plda_ack_intx_irq,
	.irq_mask = plda_mask_intx_irq,
	.irq_unmask = plda_unmask_intx_irq,
};

static int plda_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
			      irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &plda_intx_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = plda_pcie_intx_map,
};

static u32 plda_get_events(struct plda_pcie_rp *port)
{
	u32 events, val, origin;

	origin = readl_relaxed(port->bridge_addr + ISTATUS_LOCAL);

	/* MSI event and sys events */
	val = (origin & SYS_AND_MSI_MASK) >> PM_MSI_INT_MSI_SHIFT;
	events = val << (PM_MSI_INT_MSI_SHIFT - PCI_NUM_INTX + 1);

	/* INTx events */
	if (origin & PM_MSI_INT_INTX_MASK)
		events |= BIT(PM_MSI_INT_INTX_SHIFT);

	/* remains are same with register */
	events |= origin & GENMASK(P_ATR_EVT_DOORBELL_SHIFT, 0);

	return events;
}

static irqreturn_t plda_event_handler(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static void plda_handle_event(struct irq_desc *desc)
{
	struct plda_pcie_rp *port = irq_desc_get_handler_data(desc);
	unsigned long events;
	u32 bit;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	events = port->event_ops->get_events(port);

	events &= port->events_bitmap;
	for_each_set_bit(bit, &events, port->num_events)
		generic_handle_domain_irq(port->event_domain, bit);

	chained_irq_exit(chip, desc);
}

static u32 plda_hwirq_to_mask(int hwirq)
{
	u32 mask;

	/* hwirq 23 - 0 are the same with register */
	if (hwirq < EVENT_PM_MSI_INT_INTX)
		mask = BIT(hwirq);
	else if (hwirq == EVENT_PM_MSI_INT_INTX)
		mask = PM_MSI_INT_INTX_MASK;
	else
		mask = BIT(hwirq + PCI_NUM_INTX - 1);

	return mask;
}

static void plda_ack_event_irq(struct irq_data *data)
{
	struct plda_pcie_rp *port = irq_data_get_irq_chip_data(data);

	writel_relaxed(plda_hwirq_to_mask(data->hwirq),
		       port->bridge_addr + ISTATUS_LOCAL);
}

static void plda_mask_event_irq(struct irq_data *data)
{
	struct plda_pcie_rp *port = irq_data_get_irq_chip_data(data);
	u32 mask, val;

	mask = plda_hwirq_to_mask(data->hwirq);

	raw_spin_lock(&port->lock);
	val = readl_relaxed(port->bridge_addr + IMASK_LOCAL);
	val &= ~mask;
	writel_relaxed(val, port->bridge_addr + IMASK_LOCAL);
	raw_spin_unlock(&port->lock);
}

static void plda_unmask_event_irq(struct irq_data *data)
{
	struct plda_pcie_rp *port = irq_data_get_irq_chip_data(data);
	u32 mask, val;

	mask = plda_hwirq_to_mask(data->hwirq);

	raw_spin_lock(&port->lock);
	val = readl_relaxed(port->bridge_addr + IMASK_LOCAL);
	val |= mask;
	writel_relaxed(val, port->bridge_addr + IMASK_LOCAL);
	raw_spin_unlock(&port->lock);
}

static struct irq_chip plda_event_irq_chip = {
	.name = "PLDA PCIe EVENT",
	.irq_ack = plda_ack_event_irq,
	.irq_mask = plda_mask_event_irq,
	.irq_unmask = plda_unmask_event_irq,
};

static const struct plda_event_ops plda_event_ops = {
	.get_events = plda_get_events,
};

static int plda_pcie_event_map(struct irq_domain *domain, unsigned int irq,
			       irq_hw_number_t hwirq)
{
	struct plda_pcie_rp *port = (void *)domain->host_data;

	irq_set_chip_and_handler(irq, port->event_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops plda_event_domain_ops = {
	.map = plda_pcie_event_map,
};

static int plda_pcie_init_irq_domains(struct plda_pcie_rp *port)
{
	struct device *dev = port->dev;
	struct device_node *node = dev->of_node;
	struct device_node *pcie_intc_node;

	/* Setup INTx */
	pcie_intc_node = of_get_next_child(node, NULL);
	if (!pcie_intc_node) {
		dev_err(dev, "failed to find PCIe Intc node\n");
		return -EINVAL;
	}

	port->event_domain = irq_domain_add_linear(pcie_intc_node,
						   port->num_events,
						   &plda_event_domain_ops,
						   port);
	if (!port->event_domain) {
		dev_err(dev, "failed to get event domain\n");
		of_node_put(pcie_intc_node);
		return -ENOMEM;
	}

	irq_domain_update_bus_token(port->event_domain, DOMAIN_BUS_NEXUS);

	port->intx_domain = irq_domain_add_linear(pcie_intc_node, PCI_NUM_INTX,
						  &intx_domain_ops, port);
	if (!port->intx_domain) {
		dev_err(dev, "failed to get an INTx IRQ domain\n");
		of_node_put(pcie_intc_node);
		return -ENOMEM;
	}

	irq_domain_update_bus_token(port->intx_domain, DOMAIN_BUS_WIRED);

	of_node_put(pcie_intc_node);
	raw_spin_lock_init(&port->lock);

	return plda_allocate_msi_domains(port);
}

int plda_init_interrupts(struct platform_device *pdev,
			 struct plda_pcie_rp *port,
			 const struct plda_event *event)
{
	struct device *dev = &pdev->dev;
	int event_irq, ret;
	u32 i;

	if (!port->event_ops)
		port->event_ops = &plda_event_ops;

	if (!port->event_irq_chip)
		port->event_irq_chip = &plda_event_irq_chip;

	ret = plda_pcie_init_irq_domains(port);
	if (ret) {
		dev_err(dev, "failed creating IRQ domains\n");
		return ret;
	}

	port->irq = platform_get_irq(pdev, 0);
	if (port->irq < 0)
		return -ENODEV;

	for_each_set_bit(i, &port->events_bitmap, port->num_events) {
		event_irq = irq_create_mapping(port->event_domain, i);
		if (!event_irq) {
			dev_err(dev, "failed to map hwirq %d\n", i);
			return -ENXIO;
		}

		if (event->request_event_irq)
			ret = event->request_event_irq(port, event_irq, i);
		else
			ret = devm_request_irq(dev, event_irq,
					       plda_event_handler,
					       0, NULL, port);

		if (ret) {
			dev_err(dev, "failed to request IRQ %d\n", event_irq);
			return ret;
		}
	}

	port->intx_irq = irq_create_mapping(port->event_domain,
					    event->intx_event);
	if (!port->intx_irq) {
		dev_err(dev, "failed to map INTx interrupt\n");
		return -ENXIO;
	}

	/* Plug the INTx chained handler */
	irq_set_chained_handler_and_data(port->intx_irq, plda_handle_intx, port);

	port->msi_irq = irq_create_mapping(port->event_domain,
					   event->msi_event);
	if (!port->msi_irq)
		return -ENXIO;

	/* Plug the MSI chained handler */
	irq_set_chained_handler_and_data(port->msi_irq, plda_handle_msi, port);

	/* Plug the main event chained handler */
	irq_set_chained_handler_and_data(port->irq, plda_handle_event, port);

	return 0;
}
EXPORT_SYMBOL_GPL(plda_init_interrupts);

void plda_pcie_setup_window(void __iomem *bridge_base_addr, u32 index,
			    phys_addr_t axi_addr, phys_addr_t pci_addr,
			    size_t size)
{
	u32 atr_sz = ilog2(size) - 1;
	u32 val;

	if (index == 0)
		val = PCIE_CONFIG_INTERFACE;
	else
		val = PCIE_TX_RX_INTERFACE;

	writel(val, bridge_base_addr + (index * ATR_ENTRY_SIZE) +
	       ATR0_AXI4_SLV0_TRSL_PARAM);

	val = lower_32_bits(axi_addr) | (atr_sz << ATR_SIZE_SHIFT) |
			    ATR_IMPL_ENABLE;
	writel(val, bridge_base_addr + (index * ATR_ENTRY_SIZE) +
	       ATR0_AXI4_SLV0_SRCADDR_PARAM);

	val = upper_32_bits(axi_addr);
	writel(val, bridge_base_addr + (index * ATR_ENTRY_SIZE) +
	       ATR0_AXI4_SLV0_SRC_ADDR);

	val = lower_32_bits(pci_addr);
	writel(val, bridge_base_addr + (index * ATR_ENTRY_SIZE) +
	       ATR0_AXI4_SLV0_TRSL_ADDR_LSB);

	val = upper_32_bits(pci_addr);
	writel(val, bridge_base_addr + (index * ATR_ENTRY_SIZE) +
	       ATR0_AXI4_SLV0_TRSL_ADDR_UDW);

	val = readl(bridge_base_addr + ATR0_PCIE_WIN0_SRCADDR_PARAM);
	val |= (ATR0_PCIE_ATR_SIZE << ATR0_PCIE_ATR_SIZE_SHIFT);
	writel(val, bridge_base_addr + ATR0_PCIE_WIN0_SRCADDR_PARAM);
	writel(0, bridge_base_addr + ATR0_PCIE_WIN0_SRC_ADDR);
}
EXPORT_SYMBOL_GPL(plda_pcie_setup_window);

int plda_pcie_setup_iomems(struct pci_host_bridge *bridge,
			   struct plda_pcie_rp *port)
{
	void __iomem *bridge_base_addr = port->bridge_addr;
	struct resource_entry *entry;
	u64 pci_addr;
	u32 index = 1;

	resource_list_for_each_entry(entry, &bridge->windows) {
		if (resource_type(entry->res) == IORESOURCE_MEM) {
			pci_addr = entry->res->start - entry->offset;
			plda_pcie_setup_window(bridge_base_addr, index,
					       entry->res->start, pci_addr,
					       resource_size(entry->res));
			index++;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(plda_pcie_setup_iomems);

static void plda_pcie_irq_domain_deinit(struct plda_pcie_rp *pcie)
{
	irq_set_chained_handler_and_data(pcie->irq, NULL, NULL);
	irq_set_chained_handler_and_data(pcie->msi_irq, NULL, NULL);
	irq_set_chained_handler_and_data(pcie->intx_irq, NULL, NULL);

	irq_domain_remove(pcie->msi.msi_domain);
	irq_domain_remove(pcie->msi.dev_domain);

	irq_domain_remove(pcie->intx_domain);
	irq_domain_remove(pcie->event_domain);
}

int plda_pcie_host_init(struct plda_pcie_rp *port, struct pci_ops *ops,
			const struct plda_event *plda_event)
{
	struct device *dev = port->dev;
	struct pci_host_bridge *bridge;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *cfg_res;
	int ret;

	pdev = to_platform_device(dev);

	port->bridge_addr =
		devm_platform_ioremap_resource_byname(pdev, "apb");

	if (IS_ERR(port->bridge_addr))
		return dev_err_probe(dev, PTR_ERR(port->bridge_addr),
				     "failed to map reg memory\n");

	cfg_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg");
	if (!cfg_res)
		return dev_err_probe(dev, -ENODEV,
				     "failed to get config memory\n");

	port->config_base = devm_ioremap_resource(dev, cfg_res);
	if (IS_ERR(port->config_base))
		return dev_err_probe(dev, PTR_ERR(port->config_base),
				     "failed to map config memory\n");

	bridge = devm_pci_alloc_host_bridge(dev, 0);
	if (!bridge)
		return dev_err_probe(dev, -ENOMEM,
				     "failed to alloc bridge\n");

	if (port->host_ops && port->host_ops->host_init) {
		ret = port->host_ops->host_init(port);
		if (ret)
			return ret;
	}

	port->bridge = bridge;
	plda_pcie_setup_window(port->bridge_addr, 0, cfg_res->start, 0,
			       resource_size(cfg_res));
	plda_pcie_setup_iomems(bridge, port);
	plda_set_default_msi(&port->msi);
	ret = plda_init_interrupts(pdev, port, plda_event);
	if (ret)
		goto err_host;

	/* Set default bus ops */
	bridge->ops = ops;
	bridge->sysdata = port;

	ret = pci_host_probe(bridge);
	if (ret < 0) {
		dev_err_probe(dev, ret, "failed to probe pci host\n");
		goto err_probe;
	}

	return ret;

err_probe:
	plda_pcie_irq_domain_deinit(port);
err_host:
	if (port->host_ops && port->host_ops->host_deinit)
		port->host_ops->host_deinit(port);

	return ret;
}
EXPORT_SYMBOL_GPL(plda_pcie_host_init);

void plda_pcie_host_deinit(struct plda_pcie_rp *port)
{
	pci_stop_root_bus(port->bridge->bus);
	pci_remove_root_bus(port->bridge->bus);

	plda_pcie_irq_domain_deinit(port);

	if (port->host_ops && port->host_ops->host_deinit)
		port->host_ops->host_deinit(port);
}
EXPORT_SYMBOL_GPL(plda_pcie_host_deinit);
