// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe host controller driver for Xilinx XDMA PCIe Bridge
 *
 * Copyright (C) 2023 Xilinx, Inc. All rights reserved.
 */
#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>

#include "../pci.h"
#include "pcie-xilinx-common.h"

/* Register definitions */
#define XILINX_PCIE_DMA_REG_IDR			0x00000138
#define XILINX_PCIE_DMA_REG_IMR			0x0000013c
#define XILINX_PCIE_DMA_REG_PSCR		0x00000144
#define XILINX_PCIE_DMA_REG_RPSC		0x00000148
#define XILINX_PCIE_DMA_REG_MSIBASE1		0x0000014c
#define XILINX_PCIE_DMA_REG_MSIBASE2		0x00000150
#define XILINX_PCIE_DMA_REG_RPEFR		0x00000154
#define XILINX_PCIE_DMA_REG_IDRN		0x00000160
#define XILINX_PCIE_DMA_REG_IDRN_MASK		0x00000164
#define XILINX_PCIE_DMA_REG_MSI_LOW		0x00000170
#define XILINX_PCIE_DMA_REG_MSI_HI		0x00000174
#define XILINX_PCIE_DMA_REG_MSI_LOW_MASK	0x00000178
#define XILINX_PCIE_DMA_REG_MSI_HI_MASK		0x0000017c

#define IMR(x) BIT(XILINX_PCIE_INTR_ ##x)

#define XILINX_PCIE_INTR_IMR_ALL_MASK	\
	(					\
		IMR(LINK_DOWN)		|	\
		IMR(HOT_RESET)		|	\
		IMR(CFG_TIMEOUT)	|	\
		IMR(CORRECTABLE)	|	\
		IMR(NONFATAL)		|	\
		IMR(FATAL)		|	\
		IMR(INTX)		|	\
		IMR(MSI)		|	\
		IMR(SLV_UNSUPP)		|	\
		IMR(SLV_UNEXP)		|	\
		IMR(SLV_COMPL)		|	\
		IMR(SLV_ERRP)		|	\
		IMR(SLV_CMPABT)		|	\
		IMR(SLV_ILLBUR)		|	\
		IMR(MST_DECERR)		|	\
		IMR(MST_SLVERR)		|	\
	)

#define XILINX_PCIE_DMA_IMR_ALL_MASK	0x0ff30fe9
#define XILINX_PCIE_DMA_IDR_ALL_MASK	0xffffffff
#define XILINX_PCIE_DMA_IDRN_MASK	GENMASK(19, 16)

/* Root Port Error Register definitions */
#define XILINX_PCIE_DMA_RPEFR_ERR_VALID	BIT(18)
#define XILINX_PCIE_DMA_RPEFR_REQ_ID	GENMASK(15, 0)
#define XILINX_PCIE_DMA_RPEFR_ALL_MASK	0xffffffff

/* Root Port Interrupt Register definitions */
#define XILINX_PCIE_DMA_IDRN_SHIFT	16

/* Root Port Status/control Register definitions */
#define XILINX_PCIE_DMA_REG_RPSC_BEN	BIT(0)

/* Phy Status/Control Register definitions */
#define XILINX_PCIE_DMA_REG_PSCR_LNKUP	BIT(11)

/* Number of MSI IRQs */
#define XILINX_NUM_MSI_IRQS	64

struct xilinx_msi {
	struct irq_domain	*msi_domain;
	unsigned long		*bitmap;
	struct irq_domain	*dev_domain;
	struct mutex		lock;		/* Protect bitmap variable */
	int			irq_msi0;
	int			irq_msi1;
};

/**
 * struct pl_dma_pcie - PCIe port information
 * @dev: Device pointer
 * @reg_base: IO Mapped Register Base
 * @irq: Interrupt number
 * @cfg: Holds mappings of config space window
 * @phys_reg_base: Physical address of reg base
 * @intx_domain: Legacy IRQ domain pointer
 * @pldma_domain: PL DMA IRQ domain pointer
 * @resources: Bus Resources
 * @msi: MSI information
 * @intx_irq: INTx error interrupt number
 * @lock: Lock protecting shared register access
 */
struct pl_dma_pcie {
	struct device			*dev;
	void __iomem			*reg_base;
	int				irq;
	struct pci_config_window	*cfg;
	phys_addr_t			phys_reg_base;
	struct irq_domain		*intx_domain;
	struct irq_domain		*pldma_domain;
	struct list_head		resources;
	struct xilinx_msi		msi;
	int				intx_irq;
	raw_spinlock_t			lock;
};

static inline u32 pcie_read(struct pl_dma_pcie *port, u32 reg)
{
	return readl(port->reg_base + reg);
}

static inline void pcie_write(struct pl_dma_pcie *port, u32 val, u32 reg)
{
	writel(val, port->reg_base + reg);
}

static inline bool xilinx_pl_dma_pcie_link_up(struct pl_dma_pcie *port)
{
	return (pcie_read(port, XILINX_PCIE_DMA_REG_PSCR) &
		XILINX_PCIE_DMA_REG_PSCR_LNKUP) ? true : false;
}

static void xilinx_pl_dma_pcie_clear_err_interrupts(struct pl_dma_pcie *port)
{
	unsigned long val = pcie_read(port, XILINX_PCIE_DMA_REG_RPEFR);

	if (val & XILINX_PCIE_DMA_RPEFR_ERR_VALID) {
		dev_dbg(port->dev, "Requester ID %lu\n",
			val & XILINX_PCIE_DMA_RPEFR_REQ_ID);
		pcie_write(port, XILINX_PCIE_DMA_RPEFR_ALL_MASK,
			   XILINX_PCIE_DMA_REG_RPEFR);
	}
}

static bool xilinx_pl_dma_pcie_valid_device(struct pci_bus *bus,
					    unsigned int devfn)
{
	struct pl_dma_pcie *port = bus->sysdata;

	if (!pci_is_root_bus(bus)) {
		/*
		 * Checking whether the link is up is the last line of
		 * defense, and this check is inherently racy by definition.
		 * Sending a PIO request to a downstream device when the link is
		 * down causes an unrecoverable error, and a reset of the entire
		 * PCIe controller will be needed. We can reduce the likelihood
		 * of that unrecoverable error by checking whether the link is
		 * up, but we can't completely prevent it because the link may
		 * go down between the link-up check and the PIO request.
		 */
		if (!xilinx_pl_dma_pcie_link_up(port))
			return false;
	} else if (devfn > 0)
		/* Only one device down on each root port */
		return false;

	return true;
}

static void __iomem *xilinx_pl_dma_pcie_map_bus(struct pci_bus *bus,
						unsigned int devfn, int where)
{
	struct pl_dma_pcie *port = bus->sysdata;

	if (!xilinx_pl_dma_pcie_valid_device(bus, devfn))
		return NULL;

	return port->reg_base + PCIE_ECAM_OFFSET(bus->number, devfn, where);
}

/* PCIe operations */
static struct pci_ecam_ops xilinx_pl_dma_pcie_ops = {
	.pci_ops = {
		.map_bus = xilinx_pl_dma_pcie_map_bus,
		.read	= pci_generic_config_read,
		.write	= pci_generic_config_write,
	}
};

static void xilinx_pl_dma_pcie_enable_msi(struct pl_dma_pcie *port)
{
	phys_addr_t msi_addr = port->phys_reg_base;

	pcie_write(port, upper_32_bits(msi_addr), XILINX_PCIE_DMA_REG_MSIBASE1);
	pcie_write(port, lower_32_bits(msi_addr), XILINX_PCIE_DMA_REG_MSIBASE2);
}

static void xilinx_mask_intx_irq(struct irq_data *data)
{
	struct pl_dma_pcie *port = irq_data_get_irq_chip_data(data);
	unsigned long flags;
	u32 mask, val;

	mask = BIT(data->hwirq + XILINX_PCIE_DMA_IDRN_SHIFT);
	raw_spin_lock_irqsave(&port->lock, flags);
	val = pcie_read(port, XILINX_PCIE_DMA_REG_IDRN_MASK);
	pcie_write(port, (val & (~mask)), XILINX_PCIE_DMA_REG_IDRN_MASK);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static void xilinx_unmask_intx_irq(struct irq_data *data)
{
	struct pl_dma_pcie *port = irq_data_get_irq_chip_data(data);
	unsigned long flags;
	u32 mask, val;

	mask = BIT(data->hwirq + XILINX_PCIE_DMA_IDRN_SHIFT);
	raw_spin_lock_irqsave(&port->lock, flags);
	val = pcie_read(port, XILINX_PCIE_DMA_REG_IDRN_MASK);
	pcie_write(port, (val | mask), XILINX_PCIE_DMA_REG_IDRN_MASK);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static struct irq_chip xilinx_leg_irq_chip = {
	.name		= "pl_dma:INTx",
	.irq_mask	= xilinx_mask_intx_irq,
	.irq_unmask	= xilinx_unmask_intx_irq,
};

static int xilinx_pl_dma_pcie_intx_map(struct irq_domain *domain,
				       unsigned int irq, irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &xilinx_leg_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);
	irq_set_status_flags(irq, IRQ_LEVEL);

	return 0;
}

/* INTx IRQ Domain operations */
static const struct irq_domain_ops intx_domain_ops = {
	.map = xilinx_pl_dma_pcie_intx_map,
};

static irqreturn_t xilinx_pl_dma_pcie_msi_handler_high(int irq, void *args)
{
	struct xilinx_msi *msi;
	unsigned long status;
	u32 bit, virq;
	struct pl_dma_pcie *port = args;

	msi = &port->msi;

	while ((status = pcie_read(port, XILINX_PCIE_DMA_REG_MSI_HI)) != 0) {
		for_each_set_bit(bit, &status, 32) {
			pcie_write(port, 1 << bit, XILINX_PCIE_DMA_REG_MSI_HI);
			bit = bit + 32;
			virq = irq_find_mapping(msi->dev_domain, bit);
			if (virq)
				generic_handle_irq(virq);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t xilinx_pl_dma_pcie_msi_handler_low(int irq, void *args)
{
	struct pl_dma_pcie *port = args;
	struct xilinx_msi *msi;
	unsigned long status;
	u32 bit, virq;

	msi = &port->msi;

	while ((status = pcie_read(port, XILINX_PCIE_DMA_REG_MSI_LOW)) != 0) {
		for_each_set_bit(bit, &status, 32) {
			pcie_write(port, 1 << bit, XILINX_PCIE_DMA_REG_MSI_LOW);
			virq = irq_find_mapping(msi->dev_domain, bit);
			if (virq)
				generic_handle_irq(virq);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t xilinx_pl_dma_pcie_event_flow(int irq, void *args)
{
	struct pl_dma_pcie *port = args;
	unsigned long val;
	int i;

	val = pcie_read(port, XILINX_PCIE_DMA_REG_IDR);
	val &= pcie_read(port, XILINX_PCIE_DMA_REG_IMR);
	for_each_set_bit(i, &val, 32)
		generic_handle_domain_irq(port->pldma_domain, i);

	pcie_write(port, val, XILINX_PCIE_DMA_REG_IDR);

	return IRQ_HANDLED;
}

#define _IC(x, s)                              \
	[XILINX_PCIE_INTR_ ## x] = { __stringify(x), s }

static const struct {
	const char	*sym;
	const char	*str;
} intr_cause[32] = {
	_IC(LINK_DOWN,		"Link Down"),
	_IC(HOT_RESET,		"Hot reset"),
	_IC(CFG_TIMEOUT,	"ECAM access timeout"),
	_IC(CORRECTABLE,	"Correctable error message"),
	_IC(NONFATAL,		"Non fatal error message"),
	_IC(FATAL,		"Fatal error message"),
	_IC(SLV_UNSUPP,		"Slave unsupported request"),
	_IC(SLV_UNEXP,		"Slave unexpected completion"),
	_IC(SLV_COMPL,		"Slave completion timeout"),
	_IC(SLV_ERRP,		"Slave Error Poison"),
	_IC(SLV_CMPABT,		"Slave Completer Abort"),
	_IC(SLV_ILLBUR,		"Slave Illegal Burst"),
	_IC(MST_DECERR,		"Master decode error"),
	_IC(MST_SLVERR,		"Master slave error"),
};

static irqreturn_t xilinx_pl_dma_pcie_intr_handler(int irq, void *dev_id)
{
	struct pl_dma_pcie *port = (struct pl_dma_pcie *)dev_id;
	struct device *dev = port->dev;
	struct irq_data *d;

	d = irq_domain_get_irq_data(port->pldma_domain, irq);
	switch (d->hwirq) {
	case XILINX_PCIE_INTR_CORRECTABLE:
	case XILINX_PCIE_INTR_NONFATAL:
	case XILINX_PCIE_INTR_FATAL:
		xilinx_pl_dma_pcie_clear_err_interrupts(port);
		fallthrough;

	default:
		if (intr_cause[d->hwirq].str)
			dev_warn(dev, "%s\n", intr_cause[d->hwirq].str);
		else
			dev_warn(dev, "Unknown IRQ %ld\n", d->hwirq);
	}

	return IRQ_HANDLED;
}

static struct irq_chip xilinx_msi_irq_chip = {
	.name = "pl_dma:PCIe MSI",
	.irq_enable = pci_msi_unmask_irq,
	.irq_disable = pci_msi_mask_irq,
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static struct msi_domain_info xilinx_msi_domain_info = {
	.flags = (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		MSI_FLAG_MULTI_PCI_MSI),
	.chip = &xilinx_msi_irq_chip,
};

static void xilinx_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct pl_dma_pcie *pcie = irq_data_get_irq_chip_data(data);
	phys_addr_t msi_addr = pcie->phys_reg_base;

	msg->address_lo = lower_32_bits(msi_addr);
	msg->address_hi = upper_32_bits(msi_addr);
	msg->data = data->hwirq;
}

static int xilinx_msi_set_affinity(struct irq_data *irq_data,
				   const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static struct irq_chip xilinx_irq_chip = {
	.name = "pl_dma:MSI",
	.irq_compose_msi_msg = xilinx_compose_msi_msg,
	.irq_set_affinity = xilinx_msi_set_affinity,
};

static int xilinx_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs, void *args)
{
	struct pl_dma_pcie *pcie = domain->host_data;
	struct xilinx_msi *msi = &pcie->msi;
	int bit, i;

	mutex_lock(&msi->lock);
	bit = bitmap_find_free_region(msi->bitmap, XILINX_NUM_MSI_IRQS,
				      get_count_order(nr_irqs));
	if (bit < 0) {
		mutex_unlock(&msi->lock);
		return -ENOSPC;
	}

	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, bit + i, &xilinx_irq_chip,
				    domain->host_data, handle_simple_irq,
				    NULL, NULL);
	}
	mutex_unlock(&msi->lock);

	return 0;
}

static void xilinx_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs)
{
	struct irq_data *data = irq_domain_get_irq_data(domain, virq);
	struct pl_dma_pcie *pcie = irq_data_get_irq_chip_data(data);
	struct xilinx_msi *msi = &pcie->msi;

	mutex_lock(&msi->lock);
	bitmap_release_region(msi->bitmap, data->hwirq,
			      get_count_order(nr_irqs));
	mutex_unlock(&msi->lock);
}

static const struct irq_domain_ops dev_msi_domain_ops = {
	.alloc	= xilinx_irq_domain_alloc,
	.free	= xilinx_irq_domain_free,
};

static void xilinx_pl_dma_pcie_free_irq_domains(struct pl_dma_pcie *port)
{
	struct xilinx_msi *msi = &port->msi;

	if (port->intx_domain) {
		irq_domain_remove(port->intx_domain);
		port->intx_domain = NULL;
	}

	if (msi->dev_domain) {
		irq_domain_remove(msi->dev_domain);
		msi->dev_domain = NULL;
	}

	if (msi->msi_domain) {
		irq_domain_remove(msi->msi_domain);
		msi->msi_domain = NULL;
	}
}

static int xilinx_pl_dma_pcie_init_msi_irq_domain(struct pl_dma_pcie *port)
{
	struct device *dev = port->dev;
	struct xilinx_msi *msi = &port->msi;
	int size = BITS_TO_LONGS(XILINX_NUM_MSI_IRQS) * sizeof(long);
	struct fwnode_handle *fwnode = of_node_to_fwnode(port->dev->of_node);

	msi->dev_domain = irq_domain_add_linear(NULL, XILINX_NUM_MSI_IRQS,
						&dev_msi_domain_ops, port);
	if (!msi->dev_domain)
		goto out;

	msi->msi_domain = pci_msi_create_irq_domain(fwnode,
						    &xilinx_msi_domain_info,
						    msi->dev_domain);
	if (!msi->msi_domain)
		goto out;

	mutex_init(&msi->lock);
	msi->bitmap = kzalloc(size, GFP_KERNEL);
	if (!msi->bitmap)
		goto out;

	raw_spin_lock_init(&port->lock);
	xilinx_pl_dma_pcie_enable_msi(port);

	return 0;

out:
	xilinx_pl_dma_pcie_free_irq_domains(port);
	dev_err(dev, "Failed to allocate MSI IRQ domains\n");

	return -ENOMEM;
}

/*
 * INTx error interrupts are Xilinx controller specific interrupt, used to
 * notify user about errors such as cfg timeout, slave unsupported requests,
 * fatal and non fatal error etc.
 */

static irqreturn_t xilinx_pl_dma_pcie_intx_flow(int irq, void *args)
{
	unsigned long val;
	int i;
	struct pl_dma_pcie *port = args;

	val = FIELD_GET(XILINX_PCIE_DMA_IDRN_MASK,
			pcie_read(port, XILINX_PCIE_DMA_REG_IDRN));

	for_each_set_bit(i, &val, PCI_NUM_INTX)
		generic_handle_domain_irq(port->intx_domain, i);
	return IRQ_HANDLED;
}

static void xilinx_pl_dma_pcie_mask_event_irq(struct irq_data *d)
{
	struct pl_dma_pcie *port = irq_data_get_irq_chip_data(d);
	u32 val;

	raw_spin_lock(&port->lock);
	val = pcie_read(port, XILINX_PCIE_DMA_REG_IMR);
	val &= ~BIT(d->hwirq);
	pcie_write(port, val, XILINX_PCIE_DMA_REG_IMR);
	raw_spin_unlock(&port->lock);
}

static void xilinx_pl_dma_pcie_unmask_event_irq(struct irq_data *d)
{
	struct pl_dma_pcie *port = irq_data_get_irq_chip_data(d);
	u32 val;

	raw_spin_lock(&port->lock);
	val = pcie_read(port, XILINX_PCIE_DMA_REG_IMR);
	val |= BIT(d->hwirq);
	pcie_write(port, val, XILINX_PCIE_DMA_REG_IMR);
	raw_spin_unlock(&port->lock);
}

static struct irq_chip xilinx_pl_dma_pcie_event_irq_chip = {
	.name		= "pl_dma:RC-Event",
	.irq_mask	= xilinx_pl_dma_pcie_mask_event_irq,
	.irq_unmask	= xilinx_pl_dma_pcie_unmask_event_irq,
};

static int xilinx_pl_dma_pcie_event_map(struct irq_domain *domain,
					unsigned int irq, irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &xilinx_pl_dma_pcie_event_irq_chip,
				 handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);
	irq_set_status_flags(irq, IRQ_LEVEL);

	return 0;
}

static const struct irq_domain_ops event_domain_ops = {
	.map = xilinx_pl_dma_pcie_event_map,
};

/**
 * xilinx_pl_dma_pcie_init_irq_domain - Initialize IRQ domain
 * @port: PCIe port information
 *
 * Return: '0' on success and error value on failure.
 */
static int xilinx_pl_dma_pcie_init_irq_domain(struct pl_dma_pcie *port)
{
	struct device *dev = port->dev;
	struct device_node *node = dev->of_node;
	struct device_node *pcie_intc_node;
	int ret;

	/* Setup INTx */
	pcie_intc_node = of_get_child_by_name(node, "interrupt-controller");
	if (!pcie_intc_node) {
		dev_err(dev, "No PCIe Intc node found\n");
		return -EINVAL;
	}

	port->pldma_domain = irq_domain_add_linear(pcie_intc_node, 32,
						   &event_domain_ops, port);
	if (!port->pldma_domain)
		return -ENOMEM;

	irq_domain_update_bus_token(port->pldma_domain, DOMAIN_BUS_NEXUS);

	port->intx_domain = irq_domain_add_linear(pcie_intc_node, PCI_NUM_INTX,
						  &intx_domain_ops, port);
	if (!port->intx_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return PTR_ERR(port->intx_domain);
	}

	irq_domain_update_bus_token(port->intx_domain, DOMAIN_BUS_WIRED);

	ret = xilinx_pl_dma_pcie_init_msi_irq_domain(port);
	if (ret != 0) {
		irq_domain_remove(port->intx_domain);
		return -ENOMEM;
	}

	of_node_put(pcie_intc_node);
	raw_spin_lock_init(&port->lock);

	return 0;
}

static int xilinx_pl_dma_pcie_setup_irq(struct pl_dma_pcie *port)
{
	struct device *dev = port->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int i, irq, err;

	port->irq = platform_get_irq(pdev, 0);
	if (port->irq < 0)
		return port->irq;

	for (i = 0; i < ARRAY_SIZE(intr_cause); i++) {
		int err;

		if (!intr_cause[i].str)
			continue;

		irq = irq_create_mapping(port->pldma_domain, i);
		if (!irq) {
			dev_err(dev, "Failed to map interrupt\n");
			return -ENXIO;
		}

		err = devm_request_irq(dev, irq,
				       xilinx_pl_dma_pcie_intr_handler,
				       IRQF_SHARED | IRQF_NO_THREAD,
				       intr_cause[i].sym, port);
		if (err) {
			dev_err(dev, "Failed to request IRQ %d\n", irq);
			return err;
		}
	}

	port->intx_irq = irq_create_mapping(port->pldma_domain,
					    XILINX_PCIE_INTR_INTX);
	if (!port->intx_irq) {
		dev_err(dev, "Failed to map INTx interrupt\n");
		return -ENXIO;
	}

	err = devm_request_irq(dev, port->intx_irq, xilinx_pl_dma_pcie_intx_flow,
			       IRQF_SHARED | IRQF_NO_THREAD, NULL, port);
	if (err) {
		dev_err(dev, "Failed to request INTx IRQ %d\n", irq);
		return err;
	}

	err = devm_request_irq(dev, port->irq, xilinx_pl_dma_pcie_event_flow,
			       IRQF_SHARED | IRQF_NO_THREAD, NULL, port);
	if (err) {
		dev_err(dev, "Failed to request event IRQ %d\n", irq);
		return err;
	}

	return 0;
}

static void xilinx_pl_dma_pcie_init_port(struct pl_dma_pcie *port)
{
	if (xilinx_pl_dma_pcie_link_up(port))
		dev_info(port->dev, "PCIe Link is UP\n");
	else
		dev_info(port->dev, "PCIe Link is DOWN\n");

	/* Disable all interrupts */
	pcie_write(port, ~XILINX_PCIE_DMA_IDR_ALL_MASK,
		   XILINX_PCIE_DMA_REG_IMR);

	/* Clear pending interrupts */
	pcie_write(port, pcie_read(port, XILINX_PCIE_DMA_REG_IDR) &
		   XILINX_PCIE_DMA_IMR_ALL_MASK,
		   XILINX_PCIE_DMA_REG_IDR);

	/* Needed for MSI DECODE MODE */
	pcie_write(port, XILINX_PCIE_DMA_IDR_ALL_MASK,
		   XILINX_PCIE_DMA_REG_MSI_LOW_MASK);
	pcie_write(port, XILINX_PCIE_DMA_IDR_ALL_MASK,
		   XILINX_PCIE_DMA_REG_MSI_HI_MASK);

	/* Set the Bridge enable bit */
	pcie_write(port, pcie_read(port, XILINX_PCIE_DMA_REG_RPSC) |
		   XILINX_PCIE_DMA_REG_RPSC_BEN,
		   XILINX_PCIE_DMA_REG_RPSC);
}

static int xilinx_request_msi_irq(struct pl_dma_pcie *port)
{
	struct device *dev = port->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	port->msi.irq_msi0 = platform_get_irq_byname(pdev, "msi0");
	if (port->msi.irq_msi0 <= 0)
		return port->msi.irq_msi0;

	ret = devm_request_irq(dev, port->msi.irq_msi0, xilinx_pl_dma_pcie_msi_handler_low,
			       IRQF_SHARED | IRQF_NO_THREAD, "xlnx-pcie-dma-pl",
			       port);
	if (ret) {
		dev_err(dev, "Failed to register interrupt\n");
		return ret;
	}

	port->msi.irq_msi1 = platform_get_irq_byname(pdev, "msi1");
	if (port->msi.irq_msi1 <= 0)
		return port->msi.irq_msi1;

	ret = devm_request_irq(dev, port->msi.irq_msi1, xilinx_pl_dma_pcie_msi_handler_high,
			       IRQF_SHARED | IRQF_NO_THREAD, "xlnx-pcie-dma-pl",
			       port);
	if (ret) {
		dev_err(dev, "Failed to register interrupt\n");
		return ret;
	}

	return 0;
}

static int xilinx_pl_dma_pcie_parse_dt(struct pl_dma_pcie *port,
				       struct resource *bus_range)
{
	struct device *dev = port->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;
	int err;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Missing \"reg\" property\n");
		return -ENXIO;
	}
	port->phys_reg_base = res->start;

	port->cfg = pci_ecam_create(dev, res, bus_range, &xilinx_pl_dma_pcie_ops);
	if (IS_ERR(port->cfg))
		return PTR_ERR(port->cfg);

	port->reg_base = port->cfg->win;

	err = xilinx_request_msi_irq(port);
	if (err) {
		pci_ecam_free(port->cfg);
		return err;
	}

	return 0;
}

static int xilinx_pl_dma_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pl_dma_pcie *port;
	struct pci_host_bridge *bridge;
	struct resource_entry *bus;
	int err;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*port));
	if (!bridge)
		return -ENODEV;

	port = pci_host_bridge_priv(bridge);

	port->dev = dev;

	bus = resource_list_first_type(&bridge->windows, IORESOURCE_BUS);
	if (!bus)
		return -ENODEV;

	err = xilinx_pl_dma_pcie_parse_dt(port, bus->res);
	if (err) {
		dev_err(dev, "Parsing DT failed\n");
		return err;
	}

	xilinx_pl_dma_pcie_init_port(port);

	err = xilinx_pl_dma_pcie_init_irq_domain(port);
	if (err)
		goto err_irq_domain;

	err = xilinx_pl_dma_pcie_setup_irq(port);

	bridge->sysdata = port;
	bridge->ops = &xilinx_pl_dma_pcie_ops.pci_ops;

	err = pci_host_probe(bridge);
	if (err < 0)
		goto err_host_bridge;

	return 0;

err_host_bridge:
	xilinx_pl_dma_pcie_free_irq_domains(port);

err_irq_domain:
	pci_ecam_free(port->cfg);
	return err;
}

static const struct of_device_id xilinx_pl_dma_pcie_of_match[] = {
	{
		.compatible = "xlnx,xdma-host-3.00",
	},
	{}
};

static struct platform_driver xilinx_pl_dma_pcie_driver = {
	.driver = {
		.name = "xilinx-xdma-pcie",
		.of_match_table = xilinx_pl_dma_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = xilinx_pl_dma_pcie_probe,
};

builtin_platform_driver(xilinx_pl_dma_pcie_driver);
