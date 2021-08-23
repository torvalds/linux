// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek PCIe host controller driver.
 *
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *	   Honghui Zhang <honghui.zhang@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/msi.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "../pci.h"

/* PCIe shared registers */
#define PCIE_SYS_CFG		0x00
#define PCIE_INT_ENABLE		0x0c
#define PCIE_CFG_ADDR		0x20
#define PCIE_CFG_DATA		0x24

/* PCIe per port registers */
#define PCIE_BAR0_SETUP		0x10
#define PCIE_CLASS		0x34
#define PCIE_LINK_STATUS	0x50

#define PCIE_PORT_INT_EN(x)	BIT(20 + (x))
#define PCIE_PORT_PERST(x)	BIT(1 + (x))
#define PCIE_PORT_LINKUP	BIT(0)
#define PCIE_BAR_MAP_MAX	GENMASK(31, 16)

#define PCIE_BAR_ENABLE		BIT(0)
#define PCIE_REVISION_ID	BIT(0)
#define PCIE_CLASS_CODE		(0x60400 << 8)
#define PCIE_CONF_REG(regn)	(((regn) & GENMASK(7, 2)) | \
				((((regn) >> 8) & GENMASK(3, 0)) << 24))
#define PCIE_CONF_FUN(fun)	(((fun) << 8) & GENMASK(10, 8))
#define PCIE_CONF_DEV(dev)	(((dev) << 11) & GENMASK(15, 11))
#define PCIE_CONF_BUS(bus)	(((bus) << 16) & GENMASK(23, 16))
#define PCIE_CONF_ADDR(regn, fun, dev, bus) \
	(PCIE_CONF_REG(regn) | PCIE_CONF_FUN(fun) | \
	 PCIE_CONF_DEV(dev) | PCIE_CONF_BUS(bus))

/* MediaTek specific configuration registers */
#define PCIE_FTS_NUM		0x70c
#define PCIE_FTS_NUM_MASK	GENMASK(15, 8)
#define PCIE_FTS_NUM_L0(x)	((x) & 0xff << 8)

#define PCIE_FC_CREDIT		0x73c
#define PCIE_FC_CREDIT_MASK	(GENMASK(31, 31) | GENMASK(28, 16))
#define PCIE_FC_CREDIT_VAL(x)	((x) << 16)

/* PCIe V2 share registers */
#define PCIE_SYS_CFG_V2		0x0
#define PCIE_CSR_LTSSM_EN(x)	BIT(0 + (x) * 8)
#define PCIE_CSR_ASPM_L1_EN(x)	BIT(1 + (x) * 8)

/* PCIe V2 per-port registers */
#define PCIE_MSI_VECTOR		0x0c0

#define PCIE_CONF_VEND_ID	0x100
#define PCIE_CONF_DEVICE_ID	0x102
#define PCIE_CONF_CLASS_ID	0x106

#define PCIE_INT_MASK		0x420
#define INTX_MASK		GENMASK(19, 16)
#define INTX_SHIFT		16
#define PCIE_INT_STATUS		0x424
#define MSI_STATUS		BIT(23)
#define PCIE_IMSI_STATUS	0x42c
#define PCIE_IMSI_ADDR		0x430
#define MSI_MASK		BIT(23)
#define MTK_MSI_IRQS_NUM	32

#define PCIE_AHB_TRANS_BASE0_L	0x438
#define PCIE_AHB_TRANS_BASE0_H	0x43c
#define AHB2PCIE_SIZE(x)	((x) & GENMASK(4, 0))
#define PCIE_AXI_WINDOW0	0x448
#define WIN_ENABLE		BIT(7)
/*
 * Define PCIe to AHB window size as 2^33 to support max 8GB address space
 * translate, support least 4GB DRAM size access from EP DMA(physical DRAM
 * start from 0x40000000).
 */
#define PCIE2AHB_SIZE	0x21

/* PCIe V2 configuration transaction header */
#define PCIE_CFG_HEADER0	0x460
#define PCIE_CFG_HEADER1	0x464
#define PCIE_CFG_HEADER2	0x468
#define PCIE_CFG_WDATA		0x470
#define PCIE_APP_TLP_REQ	0x488
#define PCIE_CFG_RDATA		0x48c
#define APP_CFG_REQ		BIT(0)
#define APP_CPL_STATUS		GENMASK(7, 5)

#define CFG_WRRD_TYPE_0		4
#define CFG_WR_FMT		2
#define CFG_RD_FMT		0

#define CFG_DW0_LENGTH(length)	((length) & GENMASK(9, 0))
#define CFG_DW0_TYPE(type)	(((type) << 24) & GENMASK(28, 24))
#define CFG_DW0_FMT(fmt)	(((fmt) << 29) & GENMASK(31, 29))
#define CFG_DW2_REGN(regn)	((regn) & GENMASK(11, 2))
#define CFG_DW2_FUN(fun)	(((fun) << 16) & GENMASK(18, 16))
#define CFG_DW2_DEV(dev)	(((dev) << 19) & GENMASK(23, 19))
#define CFG_DW2_BUS(bus)	(((bus) << 24) & GENMASK(31, 24))
#define CFG_HEADER_DW0(type, fmt) \
	(CFG_DW0_LENGTH(1) | CFG_DW0_TYPE(type) | CFG_DW0_FMT(fmt))
#define CFG_HEADER_DW1(where, size) \
	(GENMASK(((size) - 1), 0) << ((where) & 0x3))
#define CFG_HEADER_DW2(regn, fun, dev, bus) \
	(CFG_DW2_REGN(regn) | CFG_DW2_FUN(fun) | \
	CFG_DW2_DEV(dev) | CFG_DW2_BUS(bus))

#define PCIE_RST_CTRL		0x510
#define PCIE_PHY_RSTB		BIT(0)
#define PCIE_PIPE_SRSTB		BIT(1)
#define PCIE_MAC_SRSTB		BIT(2)
#define PCIE_CRSTB		BIT(3)
#define PCIE_PERSTB		BIT(8)
#define PCIE_LINKDOWN_RST_EN	GENMASK(15, 13)
#define PCIE_LINK_STATUS_V2	0x804
#define PCIE_PORT_LINKUP_V2	BIT(10)

struct mtk_pcie_port;

/**
 * struct mtk_pcie_soc - differentiate between host generations
 * @need_fix_class_id: whether this host's class ID needed to be fixed or not
 * @need_fix_device_id: whether this host's device ID needed to be fixed or not
 * @no_msi: Bridge has no MSI support, and relies on an external block
 * @device_id: device ID which this host need to be fixed
 * @ops: pointer to configuration access functions
 * @startup: pointer to controller setting functions
 * @setup_irq: pointer to initialize IRQ functions
 */
struct mtk_pcie_soc {
	bool need_fix_class_id;
	bool need_fix_device_id;
	bool no_msi;
	unsigned int device_id;
	struct pci_ops *ops;
	int (*startup)(struct mtk_pcie_port *port);
	int (*setup_irq)(struct mtk_pcie_port *port, struct device_node *node);
};

/**
 * struct mtk_pcie_port - PCIe port information
 * @base: IO mapped register base
 * @list: port list
 * @pcie: pointer to PCIe host info
 * @reset: pointer to port reset control
 * @sys_ck: pointer to transaction/data link layer clock
 * @ahb_ck: pointer to AHB slave interface operating clock for CSR access
 *          and RC initiated MMIO access
 * @axi_ck: pointer to application layer MMIO channel operating clock
 * @aux_ck: pointer to pe2_mac_bridge and pe2_mac_core operating clock
 *          when pcie_mac_ck/pcie_pipe_ck is turned off
 * @obff_ck: pointer to OBFF functional block operating clock
 * @pipe_ck: pointer to LTSSM and PHY/MAC layer operating clock
 * @phy: pointer to PHY control block
 * @slot: port slot
 * @irq: GIC irq
 * @irq_domain: legacy INTx IRQ domain
 * @inner_domain: inner IRQ domain
 * @msi_domain: MSI IRQ domain
 * @lock: protect the msi_irq_in_use bitmap
 * @msi_irq_in_use: bit map for assigned MSI IRQ
 */
struct mtk_pcie_port {
	void __iomem *base;
	struct list_head list;
	struct mtk_pcie *pcie;
	struct reset_control *reset;
	struct clk *sys_ck;
	struct clk *ahb_ck;
	struct clk *axi_ck;
	struct clk *aux_ck;
	struct clk *obff_ck;
	struct clk *pipe_ck;
	struct phy *phy;
	u32 slot;
	int irq;
	struct irq_domain *irq_domain;
	struct irq_domain *inner_domain;
	struct irq_domain *msi_domain;
	struct mutex lock;
	DECLARE_BITMAP(msi_irq_in_use, MTK_MSI_IRQS_NUM);
};

/**
 * struct mtk_pcie - PCIe host information
 * @dev: pointer to PCIe device
 * @base: IO mapped register base
 * @cfg: IO mapped register map for PCIe config
 * @free_ck: free-run reference clock
 * @mem: non-prefetchable memory resource
 * @ports: pointer to PCIe port information
 * @soc: pointer to SoC-dependent operations
 */
struct mtk_pcie {
	struct device *dev;
	void __iomem *base;
	struct regmap *cfg;
	struct clk *free_ck;

	struct list_head ports;
	const struct mtk_pcie_soc *soc;
};

static void mtk_pcie_subsys_powerdown(struct mtk_pcie *pcie)
{
	struct device *dev = pcie->dev;

	clk_disable_unprepare(pcie->free_ck);

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
}

static void mtk_pcie_port_free(struct mtk_pcie_port *port)
{
	struct mtk_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;

	devm_iounmap(dev, port->base);
	list_del(&port->list);
	devm_kfree(dev, port);
}

static void mtk_pcie_put_resources(struct mtk_pcie *pcie)
{
	struct mtk_pcie_port *port, *tmp;

	list_for_each_entry_safe(port, tmp, &pcie->ports, list) {
		phy_power_off(port->phy);
		phy_exit(port->phy);
		clk_disable_unprepare(port->pipe_ck);
		clk_disable_unprepare(port->obff_ck);
		clk_disable_unprepare(port->axi_ck);
		clk_disable_unprepare(port->aux_ck);
		clk_disable_unprepare(port->ahb_ck);
		clk_disable_unprepare(port->sys_ck);
		mtk_pcie_port_free(port);
	}

	mtk_pcie_subsys_powerdown(pcie);
}

static int mtk_pcie_check_cfg_cpld(struct mtk_pcie_port *port)
{
	u32 val;
	int err;

	err = readl_poll_timeout_atomic(port->base + PCIE_APP_TLP_REQ, val,
					!(val & APP_CFG_REQ), 10,
					100 * USEC_PER_MSEC);
	if (err)
		return PCIBIOS_SET_FAILED;

	if (readl(port->base + PCIE_APP_TLP_REQ) & APP_CPL_STATUS)
		return PCIBIOS_SET_FAILED;

	return PCIBIOS_SUCCESSFUL;
}

static int mtk_pcie_hw_rd_cfg(struct mtk_pcie_port *port, u32 bus, u32 devfn,
			      int where, int size, u32 *val)
{
	u32 tmp;

	/* Write PCIe configuration transaction header for Cfgrd */
	writel(CFG_HEADER_DW0(CFG_WRRD_TYPE_0, CFG_RD_FMT),
	       port->base + PCIE_CFG_HEADER0);
	writel(CFG_HEADER_DW1(where, size), port->base + PCIE_CFG_HEADER1);
	writel(CFG_HEADER_DW2(where, PCI_FUNC(devfn), PCI_SLOT(devfn), bus),
	       port->base + PCIE_CFG_HEADER2);

	/* Trigger h/w to transmit Cfgrd TLP */
	tmp = readl(port->base + PCIE_APP_TLP_REQ);
	tmp |= APP_CFG_REQ;
	writel(tmp, port->base + PCIE_APP_TLP_REQ);

	/* Check completion status */
	if (mtk_pcie_check_cfg_cpld(port))
		return PCIBIOS_SET_FAILED;

	/* Read cpld payload of Cfgrd */
	*val = readl(port->base + PCIE_CFG_RDATA);

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 3))) & 0xffff;

	return PCIBIOS_SUCCESSFUL;
}

static int mtk_pcie_hw_wr_cfg(struct mtk_pcie_port *port, u32 bus, u32 devfn,
			      int where, int size, u32 val)
{
	/* Write PCIe configuration transaction header for Cfgwr */
	writel(CFG_HEADER_DW0(CFG_WRRD_TYPE_0, CFG_WR_FMT),
	       port->base + PCIE_CFG_HEADER0);
	writel(CFG_HEADER_DW1(where, size), port->base + PCIE_CFG_HEADER1);
	writel(CFG_HEADER_DW2(where, PCI_FUNC(devfn), PCI_SLOT(devfn), bus),
	       port->base + PCIE_CFG_HEADER2);

	/* Write Cfgwr data */
	val = val << 8 * (where & 3);
	writel(val, port->base + PCIE_CFG_WDATA);

	/* Trigger h/w to transmit Cfgwr TLP */
	val = readl(port->base + PCIE_APP_TLP_REQ);
	val |= APP_CFG_REQ;
	writel(val, port->base + PCIE_APP_TLP_REQ);

	/* Check completion status */
	return mtk_pcie_check_cfg_cpld(port);
}

static struct mtk_pcie_port *mtk_pcie_find_port(struct pci_bus *bus,
						unsigned int devfn)
{
	struct mtk_pcie *pcie = bus->sysdata;
	struct mtk_pcie_port *port;
	struct pci_dev *dev = NULL;

	/*
	 * Walk the bus hierarchy to get the devfn value
	 * of the port in the root bus.
	 */
	while (bus && bus->number) {
		dev = bus->self;
		bus = dev->bus;
		devfn = dev->devfn;
	}

	list_for_each_entry(port, &pcie->ports, list)
		if (port->slot == PCI_SLOT(devfn))
			return port;

	return NULL;
}

static int mtk_pcie_config_read(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *val)
{
	struct mtk_pcie_port *port;
	u32 bn = bus->number;
	int ret;

	port = mtk_pcie_find_port(bus, devfn);
	if (!port) {
		*val = ~0;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	ret = mtk_pcie_hw_rd_cfg(port, bn, devfn, where, size, val);
	if (ret)
		*val = ~0;

	return ret;
}

static int mtk_pcie_config_write(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	struct mtk_pcie_port *port;
	u32 bn = bus->number;

	port = mtk_pcie_find_port(bus, devfn);
	if (!port)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return mtk_pcie_hw_wr_cfg(port, bn, devfn, where, size, val);
}

static struct pci_ops mtk_pcie_ops_v2 = {
	.read  = mtk_pcie_config_read,
	.write = mtk_pcie_config_write,
};

static void mtk_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct mtk_pcie_port *port = irq_data_get_irq_chip_data(data);
	phys_addr_t addr;

	/* MT2712/MT7622 only support 32-bit MSI addresses */
	addr = virt_to_phys(port->base + PCIE_MSI_VECTOR);
	msg->address_hi = 0;
	msg->address_lo = lower_32_bits(addr);

	msg->data = data->hwirq;

	dev_dbg(port->pcie->dev, "msi#%d address_hi %#x address_lo %#x\n",
		(int)data->hwirq, msg->address_hi, msg->address_lo);
}

static int mtk_msi_set_affinity(struct irq_data *irq_data,
				const struct cpumask *mask, bool force)
{
	 return -EINVAL;
}

static void mtk_msi_ack_irq(struct irq_data *data)
{
	struct mtk_pcie_port *port = irq_data_get_irq_chip_data(data);
	u32 hwirq = data->hwirq;

	writel(1 << hwirq, port->base + PCIE_IMSI_STATUS);
}

static struct irq_chip mtk_msi_bottom_irq_chip = {
	.name			= "MTK MSI",
	.irq_compose_msi_msg	= mtk_compose_msi_msg,
	.irq_set_affinity	= mtk_msi_set_affinity,
	.irq_ack		= mtk_msi_ack_irq,
};

static int mtk_pcie_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				     unsigned int nr_irqs, void *args)
{
	struct mtk_pcie_port *port = domain->host_data;
	unsigned long bit;

	WARN_ON(nr_irqs != 1);
	mutex_lock(&port->lock);

	bit = find_first_zero_bit(port->msi_irq_in_use, MTK_MSI_IRQS_NUM);
	if (bit >= MTK_MSI_IRQS_NUM) {
		mutex_unlock(&port->lock);
		return -ENOSPC;
	}

	__set_bit(bit, port->msi_irq_in_use);

	mutex_unlock(&port->lock);

	irq_domain_set_info(domain, virq, bit, &mtk_msi_bottom_irq_chip,
			    domain->host_data, handle_edge_irq,
			    NULL, NULL);

	return 0;
}

static void mtk_pcie_irq_domain_free(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct mtk_pcie_port *port = irq_data_get_irq_chip_data(d);

	mutex_lock(&port->lock);

	if (!test_bit(d->hwirq, port->msi_irq_in_use))
		dev_err(port->pcie->dev, "trying to free unused MSI#%lu\n",
			d->hwirq);
	else
		__clear_bit(d->hwirq, port->msi_irq_in_use);

	mutex_unlock(&port->lock);

	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
}

static const struct irq_domain_ops msi_domain_ops = {
	.alloc	= mtk_pcie_irq_domain_alloc,
	.free	= mtk_pcie_irq_domain_free,
};

static struct irq_chip mtk_msi_irq_chip = {
	.name		= "MTK PCIe MSI",
	.irq_ack	= irq_chip_ack_parent,
	.irq_mask	= pci_msi_mask_irq,
	.irq_unmask	= pci_msi_unmask_irq,
};

static struct msi_domain_info mtk_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_PCI_MSIX),
	.chip	= &mtk_msi_irq_chip,
};

static int mtk_pcie_allocate_msi_domains(struct mtk_pcie_port *port)
{
	struct fwnode_handle *fwnode = of_node_to_fwnode(port->pcie->dev->of_node);

	mutex_init(&port->lock);

	port->inner_domain = irq_domain_create_linear(fwnode, MTK_MSI_IRQS_NUM,
						      &msi_domain_ops, port);
	if (!port->inner_domain) {
		dev_err(port->pcie->dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	port->msi_domain = pci_msi_create_irq_domain(fwnode, &mtk_msi_domain_info,
						     port->inner_domain);
	if (!port->msi_domain) {
		dev_err(port->pcie->dev, "failed to create MSI domain\n");
		irq_domain_remove(port->inner_domain);
		return -ENOMEM;
	}

	return 0;
}

static void mtk_pcie_enable_msi(struct mtk_pcie_port *port)
{
	u32 val;
	phys_addr_t msg_addr;

	msg_addr = virt_to_phys(port->base + PCIE_MSI_VECTOR);
	val = lower_32_bits(msg_addr);
	writel(val, port->base + PCIE_IMSI_ADDR);

	val = readl(port->base + PCIE_INT_MASK);
	val &= ~MSI_MASK;
	writel(val, port->base + PCIE_INT_MASK);
}

static void mtk_pcie_irq_teardown(struct mtk_pcie *pcie)
{
	struct mtk_pcie_port *port, *tmp;

	list_for_each_entry_safe(port, tmp, &pcie->ports, list) {
		irq_set_chained_handler_and_data(port->irq, NULL, NULL);

		if (port->irq_domain)
			irq_domain_remove(port->irq_domain);

		if (IS_ENABLED(CONFIG_PCI_MSI)) {
			if (port->msi_domain)
				irq_domain_remove(port->msi_domain);
			if (port->inner_domain)
				irq_domain_remove(port->inner_domain);
		}

		irq_dispose_mapping(port->irq);
	}
}

static int mtk_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = mtk_pcie_intx_map,
};

static int mtk_pcie_init_irq_domain(struct mtk_pcie_port *port,
				    struct device_node *node)
{
	struct device *dev = port->pcie->dev;
	struct device_node *pcie_intc_node;
	int ret;

	/* Setup INTx */
	pcie_intc_node = of_get_next_child(node, NULL);
	if (!pcie_intc_node) {
		dev_err(dev, "no PCIe Intc node found\n");
		return -ENODEV;
	}

	port->irq_domain = irq_domain_add_linear(pcie_intc_node, PCI_NUM_INTX,
						 &intx_domain_ops, port);
	of_node_put(pcie_intc_node);
	if (!port->irq_domain) {
		dev_err(dev, "failed to get INTx IRQ domain\n");
		return -ENODEV;
	}

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		ret = mtk_pcie_allocate_msi_domains(port);
		if (ret)
			return ret;
	}

	return 0;
}

static void mtk_pcie_intr_handler(struct irq_desc *desc)
{
	struct mtk_pcie_port *port = irq_desc_get_handler_data(desc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	unsigned long status;
	u32 virq;
	u32 bit = INTX_SHIFT;

	chained_irq_enter(irqchip, desc);

	status = readl(port->base + PCIE_INT_STATUS);
	if (status & INTX_MASK) {
		for_each_set_bit_from(bit, &status, PCI_NUM_INTX + INTX_SHIFT) {
			/* Clear the INTx */
			writel(1 << bit, port->base + PCIE_INT_STATUS);
			virq = irq_find_mapping(port->irq_domain,
						bit - INTX_SHIFT);
			generic_handle_irq(virq);
		}
	}

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		if (status & MSI_STATUS){
			unsigned long imsi_status;

			while ((imsi_status = readl(port->base + PCIE_IMSI_STATUS))) {
				for_each_set_bit(bit, &imsi_status, MTK_MSI_IRQS_NUM) {
					virq = irq_find_mapping(port->inner_domain, bit);
					generic_handle_irq(virq);
				}
			}
			/* Clear MSI interrupt status */
			writel(MSI_STATUS, port->base + PCIE_INT_STATUS);
		}
	}

	chained_irq_exit(irqchip, desc);
}

static int mtk_pcie_setup_irq(struct mtk_pcie_port *port,
			      struct device_node *node)
{
	struct mtk_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int err;

	err = mtk_pcie_init_irq_domain(port, node);
	if (err) {
		dev_err(dev, "failed to init PCIe IRQ domain\n");
		return err;
	}

	if (of_find_property(dev->of_node, "interrupt-names", NULL))
		port->irq = platform_get_irq_byname(pdev, "pcie_irq");
	else
		port->irq = platform_get_irq(pdev, port->slot);

	if (port->irq < 0)
		return port->irq;

	irq_set_chained_handler_and_data(port->irq,
					 mtk_pcie_intr_handler, port);

	return 0;
}

static int mtk_pcie_startup_port_v2(struct mtk_pcie_port *port)
{
	struct mtk_pcie *pcie = port->pcie;
	struct pci_host_bridge *host = pci_host_bridge_from_priv(pcie);
	struct resource *mem = NULL;
	struct resource_entry *entry;
	const struct mtk_pcie_soc *soc = port->pcie->soc;
	u32 val;
	int err;

	entry = resource_list_first_type(&host->windows, IORESOURCE_MEM);
	if (entry)
		mem = entry->res;
	if (!mem)
		return -EINVAL;

	/* MT7622 platforms need to enable LTSSM and ASPM from PCIe subsys */
	if (pcie->base) {
		val = readl(pcie->base + PCIE_SYS_CFG_V2);
		val |= PCIE_CSR_LTSSM_EN(port->slot) |
		       PCIE_CSR_ASPM_L1_EN(port->slot);
		writel(val, pcie->base + PCIE_SYS_CFG_V2);
	} else if (pcie->cfg) {
		val = PCIE_CSR_LTSSM_EN(port->slot) |
		      PCIE_CSR_ASPM_L1_EN(port->slot);
		regmap_update_bits(pcie->cfg, PCIE_SYS_CFG_V2, val, val);
	}

	/* Assert all reset signals */
	writel(0, port->base + PCIE_RST_CTRL);

	/*
	 * Enable PCIe link down reset, if link status changed from link up to
	 * link down, this will reset MAC control registers and configuration
	 * space.
	 */
	writel(PCIE_LINKDOWN_RST_EN, port->base + PCIE_RST_CTRL);

	/* De-assert PHY, PE, PIPE, MAC and configuration reset	*/
	val = readl(port->base + PCIE_RST_CTRL);
	val |= PCIE_PHY_RSTB | PCIE_PERSTB | PCIE_PIPE_SRSTB |
	       PCIE_MAC_SRSTB | PCIE_CRSTB;
	writel(val, port->base + PCIE_RST_CTRL);

	/* Set up vendor ID and class code */
	if (soc->need_fix_class_id) {
		val = PCI_VENDOR_ID_MEDIATEK;
		writew(val, port->base + PCIE_CONF_VEND_ID);

		val = PCI_CLASS_BRIDGE_PCI;
		writew(val, port->base + PCIE_CONF_CLASS_ID);
	}

	if (soc->need_fix_device_id)
		writew(soc->device_id, port->base + PCIE_CONF_DEVICE_ID);

	/* 100ms timeout value should be enough for Gen1/2 training */
	err = readl_poll_timeout(port->base + PCIE_LINK_STATUS_V2, val,
				 !!(val & PCIE_PORT_LINKUP_V2), 20,
				 100 * USEC_PER_MSEC);
	if (err)
		return -ETIMEDOUT;

	/* Set INTx mask */
	val = readl(port->base + PCIE_INT_MASK);
	val &= ~INTX_MASK;
	writel(val, port->base + PCIE_INT_MASK);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		mtk_pcie_enable_msi(port);

	/* Set AHB to PCIe translation windows */
	val = lower_32_bits(mem->start) |
	      AHB2PCIE_SIZE(fls(resource_size(mem)));
	writel(val, port->base + PCIE_AHB_TRANS_BASE0_L);

	val = upper_32_bits(mem->start);
	writel(val, port->base + PCIE_AHB_TRANS_BASE0_H);

	/* Set PCIe to AXI translation memory space.*/
	val = PCIE2AHB_SIZE | WIN_ENABLE;
	writel(val, port->base + PCIE_AXI_WINDOW0);

	return 0;
}

static void __iomem *mtk_pcie_map_bus(struct pci_bus *bus,
				      unsigned int devfn, int where)
{
	struct mtk_pcie *pcie = bus->sysdata;

	writel(PCIE_CONF_ADDR(where, PCI_FUNC(devfn), PCI_SLOT(devfn),
			      bus->number), pcie->base + PCIE_CFG_ADDR);

	return pcie->base + PCIE_CFG_DATA + (where & 3);
}

static struct pci_ops mtk_pcie_ops = {
	.map_bus = mtk_pcie_map_bus,
	.read  = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static int mtk_pcie_startup_port(struct mtk_pcie_port *port)
{
	struct mtk_pcie *pcie = port->pcie;
	u32 func = PCI_FUNC(port->slot);
	u32 slot = PCI_SLOT(port->slot << 3);
	u32 val;
	int err;

	/* assert port PERST_N */
	val = readl(pcie->base + PCIE_SYS_CFG);
	val |= PCIE_PORT_PERST(port->slot);
	writel(val, pcie->base + PCIE_SYS_CFG);

	/* de-assert port PERST_N */
	val = readl(pcie->base + PCIE_SYS_CFG);
	val &= ~PCIE_PORT_PERST(port->slot);
	writel(val, pcie->base + PCIE_SYS_CFG);

	/* 100ms timeout value should be enough for Gen1/2 training */
	err = readl_poll_timeout(port->base + PCIE_LINK_STATUS, val,
				 !!(val & PCIE_PORT_LINKUP), 20,
				 100 * USEC_PER_MSEC);
	if (err)
		return -ETIMEDOUT;

	/* enable interrupt */
	val = readl(pcie->base + PCIE_INT_ENABLE);
	val |= PCIE_PORT_INT_EN(port->slot);
	writel(val, pcie->base + PCIE_INT_ENABLE);

	/* map to all DDR region. We need to set it before cfg operation. */
	writel(PCIE_BAR_MAP_MAX | PCIE_BAR_ENABLE,
	       port->base + PCIE_BAR0_SETUP);

	/* configure class code and revision ID */
	writel(PCIE_CLASS_CODE | PCIE_REVISION_ID, port->base + PCIE_CLASS);

	/* configure FC credit */
	writel(PCIE_CONF_ADDR(PCIE_FC_CREDIT, func, slot, 0),
	       pcie->base + PCIE_CFG_ADDR);
	val = readl(pcie->base + PCIE_CFG_DATA);
	val &= ~PCIE_FC_CREDIT_MASK;
	val |= PCIE_FC_CREDIT_VAL(0x806c);
	writel(PCIE_CONF_ADDR(PCIE_FC_CREDIT, func, slot, 0),
	       pcie->base + PCIE_CFG_ADDR);
	writel(val, pcie->base + PCIE_CFG_DATA);

	/* configure RC FTS number to 250 when it leaves L0s */
	writel(PCIE_CONF_ADDR(PCIE_FTS_NUM, func, slot, 0),
	       pcie->base + PCIE_CFG_ADDR);
	val = readl(pcie->base + PCIE_CFG_DATA);
	val &= ~PCIE_FTS_NUM_MASK;
	val |= PCIE_FTS_NUM_L0(0x50);
	writel(PCIE_CONF_ADDR(PCIE_FTS_NUM, func, slot, 0),
	       pcie->base + PCIE_CFG_ADDR);
	writel(val, pcie->base + PCIE_CFG_DATA);

	return 0;
}

static void mtk_pcie_enable_port(struct mtk_pcie_port *port)
{
	struct mtk_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;
	int err;

	err = clk_prepare_enable(port->sys_ck);
	if (err) {
		dev_err(dev, "failed to enable sys_ck%d clock\n", port->slot);
		goto err_sys_clk;
	}

	err = clk_prepare_enable(port->ahb_ck);
	if (err) {
		dev_err(dev, "failed to enable ahb_ck%d\n", port->slot);
		goto err_ahb_clk;
	}

	err = clk_prepare_enable(port->aux_ck);
	if (err) {
		dev_err(dev, "failed to enable aux_ck%d\n", port->slot);
		goto err_aux_clk;
	}

	err = clk_prepare_enable(port->axi_ck);
	if (err) {
		dev_err(dev, "failed to enable axi_ck%d\n", port->slot);
		goto err_axi_clk;
	}

	err = clk_prepare_enable(port->obff_ck);
	if (err) {
		dev_err(dev, "failed to enable obff_ck%d\n", port->slot);
		goto err_obff_clk;
	}

	err = clk_prepare_enable(port->pipe_ck);
	if (err) {
		dev_err(dev, "failed to enable pipe_ck%d\n", port->slot);
		goto err_pipe_clk;
	}

	reset_control_assert(port->reset);
	reset_control_deassert(port->reset);

	err = phy_init(port->phy);
	if (err) {
		dev_err(dev, "failed to initialize port%d phy\n", port->slot);
		goto err_phy_init;
	}

	err = phy_power_on(port->phy);
	if (err) {
		dev_err(dev, "failed to power on port%d phy\n", port->slot);
		goto err_phy_on;
	}

	if (!pcie->soc->startup(port))
		return;

	dev_info(dev, "Port%d link down\n", port->slot);

	phy_power_off(port->phy);
err_phy_on:
	phy_exit(port->phy);
err_phy_init:
	clk_disable_unprepare(port->pipe_ck);
err_pipe_clk:
	clk_disable_unprepare(port->obff_ck);
err_obff_clk:
	clk_disable_unprepare(port->axi_ck);
err_axi_clk:
	clk_disable_unprepare(port->aux_ck);
err_aux_clk:
	clk_disable_unprepare(port->ahb_ck);
err_ahb_clk:
	clk_disable_unprepare(port->sys_ck);
err_sys_clk:
	mtk_pcie_port_free(port);
}

static int mtk_pcie_parse_port(struct mtk_pcie *pcie,
			       struct device_node *node,
			       int slot)
{
	struct mtk_pcie_port *port;
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	char name[10];
	int err;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	snprintf(name, sizeof(name), "port%d", slot);
	port->base = devm_platform_ioremap_resource_byname(pdev, name);
	if (IS_ERR(port->base)) {
		dev_err(dev, "failed to map port%d base\n", slot);
		return PTR_ERR(port->base);
	}

	snprintf(name, sizeof(name), "sys_ck%d", slot);
	port->sys_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->sys_ck)) {
		dev_err(dev, "failed to get sys_ck%d clock\n", slot);
		return PTR_ERR(port->sys_ck);
	}

	/* sys_ck might be divided into the following parts in some chips */
	snprintf(name, sizeof(name), "ahb_ck%d", slot);
	port->ahb_ck = devm_clk_get_optional(dev, name);
	if (IS_ERR(port->ahb_ck))
		return PTR_ERR(port->ahb_ck);

	snprintf(name, sizeof(name), "axi_ck%d", slot);
	port->axi_ck = devm_clk_get_optional(dev, name);
	if (IS_ERR(port->axi_ck))
		return PTR_ERR(port->axi_ck);

	snprintf(name, sizeof(name), "aux_ck%d", slot);
	port->aux_ck = devm_clk_get_optional(dev, name);
	if (IS_ERR(port->aux_ck))
		return PTR_ERR(port->aux_ck);

	snprintf(name, sizeof(name), "obff_ck%d", slot);
	port->obff_ck = devm_clk_get_optional(dev, name);
	if (IS_ERR(port->obff_ck))
		return PTR_ERR(port->obff_ck);

	snprintf(name, sizeof(name), "pipe_ck%d", slot);
	port->pipe_ck = devm_clk_get_optional(dev, name);
	if (IS_ERR(port->pipe_ck))
		return PTR_ERR(port->pipe_ck);

	snprintf(name, sizeof(name), "pcie-rst%d", slot);
	port->reset = devm_reset_control_get_optional_exclusive(dev, name);
	if (PTR_ERR(port->reset) == -EPROBE_DEFER)
		return PTR_ERR(port->reset);

	/* some platforms may use default PHY setting */
	snprintf(name, sizeof(name), "pcie-phy%d", slot);
	port->phy = devm_phy_optional_get(dev, name);
	if (IS_ERR(port->phy))
		return PTR_ERR(port->phy);

	port->slot = slot;
	port->pcie = pcie;

	if (pcie->soc->setup_irq) {
		err = pcie->soc->setup_irq(port, node);
		if (err)
			return err;
	}

	INIT_LIST_HEAD(&port->list);
	list_add_tail(&port->list, &pcie->ports);

	return 0;
}

static int mtk_pcie_subsys_powerup(struct mtk_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *regs;
	struct device_node *cfg_node;
	int err;

	/* get shared registers, which are optional */
	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "subsys");
	if (regs) {
		pcie->base = devm_ioremap_resource(dev, regs);
		if (IS_ERR(pcie->base))
			return PTR_ERR(pcie->base);
	}

	cfg_node = of_find_compatible_node(NULL, NULL,
					   "mediatek,generic-pciecfg");
	if (cfg_node) {
		pcie->cfg = syscon_node_to_regmap(cfg_node);
		if (IS_ERR(pcie->cfg))
			return PTR_ERR(pcie->cfg);
	}

	pcie->free_ck = devm_clk_get(dev, "free_ck");
	if (IS_ERR(pcie->free_ck)) {
		if (PTR_ERR(pcie->free_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		pcie->free_ck = NULL;
	}

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	/* enable top level clock */
	err = clk_prepare_enable(pcie->free_ck);
	if (err) {
		dev_err(dev, "failed to enable free_ck\n");
		goto err_free_ck;
	}

	return 0;

err_free_ck:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return err;
}

static int mtk_pcie_setup(struct mtk_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *node = dev->of_node, *child;
	struct mtk_pcie_port *port, *tmp;
	int err;

	for_each_available_child_of_node(node, child) {
		int slot;

		err = of_pci_get_devfn(child);
		if (err < 0) {
			dev_err(dev, "failed to parse devfn: %d\n", err);
			goto error_put_node;
		}

		slot = PCI_SLOT(err);

		err = mtk_pcie_parse_port(pcie, child, slot);
		if (err)
			goto error_put_node;
	}

	err = mtk_pcie_subsys_powerup(pcie);
	if (err)
		return err;

	/* enable each port, and then check link status */
	list_for_each_entry_safe(port, tmp, &pcie->ports, list)
		mtk_pcie_enable_port(port);

	/* power down PCIe subsys if slots are all empty (link down) */
	if (list_empty(&pcie->ports))
		mtk_pcie_subsys_powerdown(pcie);

	return 0;
error_put_node:
	of_node_put(child);
	return err;
}

static int mtk_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_pcie *pcie;
	struct pci_host_bridge *host;
	int err;

	host = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!host)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(host);

	pcie->dev = dev;
	pcie->soc = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, pcie);
	INIT_LIST_HEAD(&pcie->ports);

	err = mtk_pcie_setup(pcie);
	if (err)
		return err;

	host->ops = pcie->soc->ops;
	host->sysdata = pcie;
	host->msi_domain = pcie->soc->no_msi;

	err = pci_host_probe(host);
	if (err)
		goto put_resources;

	return 0;

put_resources:
	if (!list_empty(&pcie->ports))
		mtk_pcie_put_resources(pcie);

	return err;
}


static void mtk_pcie_free_resources(struct mtk_pcie *pcie)
{
	struct pci_host_bridge *host = pci_host_bridge_from_priv(pcie);
	struct list_head *windows = &host->windows;

	pci_free_resource_list(windows);
}

static int mtk_pcie_remove(struct platform_device *pdev)
{
	struct mtk_pcie *pcie = platform_get_drvdata(pdev);
	struct pci_host_bridge *host = pci_host_bridge_from_priv(pcie);

	pci_stop_root_bus(host->bus);
	pci_remove_root_bus(host->bus);
	mtk_pcie_free_resources(pcie);

	mtk_pcie_irq_teardown(pcie);

	mtk_pcie_put_resources(pcie);

	return 0;
}

static int __maybe_unused mtk_pcie_suspend_noirq(struct device *dev)
{
	struct mtk_pcie *pcie = dev_get_drvdata(dev);
	struct mtk_pcie_port *port;

	if (list_empty(&pcie->ports))
		return 0;

	list_for_each_entry(port, &pcie->ports, list) {
		clk_disable_unprepare(port->pipe_ck);
		clk_disable_unprepare(port->obff_ck);
		clk_disable_unprepare(port->axi_ck);
		clk_disable_unprepare(port->aux_ck);
		clk_disable_unprepare(port->ahb_ck);
		clk_disable_unprepare(port->sys_ck);
		phy_power_off(port->phy);
		phy_exit(port->phy);
	}

	clk_disable_unprepare(pcie->free_ck);

	return 0;
}

static int __maybe_unused mtk_pcie_resume_noirq(struct device *dev)
{
	struct mtk_pcie *pcie = dev_get_drvdata(dev);
	struct mtk_pcie_port *port, *tmp;

	if (list_empty(&pcie->ports))
		return 0;

	clk_prepare_enable(pcie->free_ck);

	list_for_each_entry_safe(port, tmp, &pcie->ports, list)
		mtk_pcie_enable_port(port);

	/* In case of EP was removed while system suspend. */
	if (list_empty(&pcie->ports))
		clk_disable_unprepare(pcie->free_ck);

	return 0;
}

static const struct dev_pm_ops mtk_pcie_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(mtk_pcie_suspend_noirq,
				      mtk_pcie_resume_noirq)
};

static const struct mtk_pcie_soc mtk_pcie_soc_v1 = {
	.no_msi = true,
	.ops = &mtk_pcie_ops,
	.startup = mtk_pcie_startup_port,
};

static const struct mtk_pcie_soc mtk_pcie_soc_mt2712 = {
	.ops = &mtk_pcie_ops_v2,
	.startup = mtk_pcie_startup_port_v2,
	.setup_irq = mtk_pcie_setup_irq,
};

static const struct mtk_pcie_soc mtk_pcie_soc_mt7622 = {
	.need_fix_class_id = true,
	.ops = &mtk_pcie_ops_v2,
	.startup = mtk_pcie_startup_port_v2,
	.setup_irq = mtk_pcie_setup_irq,
};

static const struct mtk_pcie_soc mtk_pcie_soc_mt7629 = {
	.need_fix_class_id = true,
	.need_fix_device_id = true,
	.device_id = PCI_DEVICE_ID_MEDIATEK_7629,
	.ops = &mtk_pcie_ops_v2,
	.startup = mtk_pcie_startup_port_v2,
	.setup_irq = mtk_pcie_setup_irq,
};

static const struct of_device_id mtk_pcie_ids[] = {
	{ .compatible = "mediatek,mt2701-pcie", .data = &mtk_pcie_soc_v1 },
	{ .compatible = "mediatek,mt7623-pcie", .data = &mtk_pcie_soc_v1 },
	{ .compatible = "mediatek,mt2712-pcie", .data = &mtk_pcie_soc_mt2712 },
	{ .compatible = "mediatek,mt7622-pcie", .data = &mtk_pcie_soc_mt7622 },
	{ .compatible = "mediatek,mt7629-pcie", .data = &mtk_pcie_soc_mt7629 },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_pcie_ids);

static struct platform_driver mtk_pcie_driver = {
	.probe = mtk_pcie_probe,
	.remove = mtk_pcie_remove,
	.driver = {
		.name = "mtk-pcie",
		.of_match_table = mtk_pcie_ids,
		.suppress_bind_attrs = true,
		.pm = &mtk_pcie_pm_ops,
	},
};
module_platform_driver(mtk_pcie_driver);
MODULE_LICENSE("GPL v2");
