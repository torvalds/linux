// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Texas Instruments Keystone SoCs
 *
 * Copyright (C) 2013-2014 Texas Instruments., Ltd.
 *		https://www.ti.com
 *
 * Author: Murali Karicheri <m-karicheri2@ti.com>
 * Implementation based on pci-exynos.c and pcie-designware.c
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/syscon.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/resource.h>
#include <linux/signal.h>

#include "../../pci.h"
#include "pcie-designware.h"

#define PCIE_VENDORID_MASK	0xffff
#define PCIE_DEVICEID_SHIFT	16

/* Application registers */
#define CMD_STATUS			0x004
#define LTSSM_EN_VAL		        BIT(0)
#define OB_XLAT_EN_VAL		        BIT(1)
#define DBI_CS2				BIT(5)

#define CFG_SETUP			0x008
#define CFG_BUS(x)			(((x) & 0xff) << 16)
#define CFG_DEVICE(x)			(((x) & 0x1f) << 8)
#define CFG_FUNC(x)			((x) & 0x7)
#define CFG_TYPE1			BIT(24)

#define OB_SIZE				0x030
#define OB_OFFSET_INDEX(n)		(0x200 + (8 * (n)))
#define OB_OFFSET_HI(n)			(0x204 + (8 * (n)))
#define OB_ENABLEN			BIT(0)
#define OB_WIN_SIZE			8	/* 8MB */

#define PCIE_LEGACY_IRQ_ENABLE_SET(n)	(0x188 + (0x10 * ((n) - 1)))
#define PCIE_LEGACY_IRQ_ENABLE_CLR(n)	(0x18c + (0x10 * ((n) - 1)))
#define PCIE_EP_IRQ_SET			0x64
#define PCIE_EP_IRQ_CLR			0x68
#define INT_ENABLE			BIT(0)

/* IRQ register defines */
#define IRQ_EOI				0x050

#define MSI_IRQ				0x054
#define MSI_IRQ_STATUS(n)		(0x104 + ((n) << 4))
#define MSI_IRQ_ENABLE_SET(n)		(0x108 + ((n) << 4))
#define MSI_IRQ_ENABLE_CLR(n)		(0x10c + ((n) << 4))
#define MSI_IRQ_OFFSET			4

#define IRQ_STATUS(n)			(0x184 + ((n) << 4))
#define IRQ_ENABLE_SET(n)		(0x188 + ((n) << 4))
#define INTx_EN				BIT(0)

#define ERR_IRQ_STATUS			0x1c4
#define ERR_IRQ_ENABLE_SET		0x1c8
#define ERR_AER				BIT(5)	/* ECRC error */
#define AM6_ERR_AER			BIT(4)	/* AM6 ECRC error */
#define ERR_AXI				BIT(4)	/* AXI tag lookup fatal error */
#define ERR_CORR			BIT(3)	/* Correctable error */
#define ERR_NONFATAL			BIT(2)	/* Non-fatal error */
#define ERR_FATAL			BIT(1)	/* Fatal error */
#define ERR_SYS				BIT(0)	/* System error */
#define ERR_IRQ_ALL			(ERR_AER | ERR_AXI | ERR_CORR | \
					 ERR_NONFATAL | ERR_FATAL | ERR_SYS)

/* PCIE controller device IDs */
#define PCIE_RC_K2HK			0xb008
#define PCIE_RC_K2E			0xb009
#define PCIE_RC_K2L			0xb00a
#define PCIE_RC_K2G			0xb00b

#define KS_PCIE_DEV_TYPE_MASK		(0x3 << 1)
#define KS_PCIE_DEV_TYPE(mode)		((mode) << 1)

#define EP				0x0
#define LEG_EP				0x1
#define RC				0x2

#define KS_PCIE_SYSCLOCKOUTEN		BIT(0)

#define AM654_PCIE_DEV_TYPE_MASK	0x3
#define AM654_WIN_SIZE			SZ_64K

#define APP_ADDR_SPACE_0		(16 * SZ_1K)

#define to_keystone_pcie(x)		dev_get_drvdata((x)->dev)

struct ks_pcie_of_data {
	enum dw_pcie_device_mode mode;
	const struct dw_pcie_host_ops *host_ops;
	const struct dw_pcie_ep_ops *ep_ops;
	u32 version;
};

struct keystone_pcie {
	struct dw_pcie		*pci;
	/* PCI Device ID */
	u32			device_id;
	int			intx_host_irqs[PCI_NUM_INTX];

	int			msi_host_irq;
	int			num_lanes;
	u32			num_viewport;
	struct phy		**phy;
	struct device_link	**link;
	struct			device_node *msi_intc_np;
	struct irq_domain	*intx_irq_domain;
	struct device_node	*np;

	/* Application register space */
	void __iomem		*va_app_base;	/* DT 1st resource */
	struct resource		app;
	bool			is_am6;
};

static u32 ks_pcie_app_readl(struct keystone_pcie *ks_pcie, u32 offset)
{
	return readl(ks_pcie->va_app_base + offset);
}

static void ks_pcie_app_writel(struct keystone_pcie *ks_pcie, u32 offset,
			       u32 val)
{
	writel(val, ks_pcie->va_app_base + offset);
}

static void ks_pcie_msi_irq_ack(struct irq_data *data)
{
	struct dw_pcie_rp *pp  = irq_data_get_irq_chip_data(data);
	struct keystone_pcie *ks_pcie;
	u32 irq = data->hwirq;
	struct dw_pcie *pci;
	u32 reg_offset;
	u32 bit_pos;

	pci = to_dw_pcie_from_pp(pp);
	ks_pcie = to_keystone_pcie(pci);

	reg_offset = irq % 8;
	bit_pos = irq >> 3;

	ks_pcie_app_writel(ks_pcie, MSI_IRQ_STATUS(reg_offset),
			   BIT(bit_pos));
	ks_pcie_app_writel(ks_pcie, IRQ_EOI, reg_offset + MSI_IRQ_OFFSET);
}

static void ks_pcie_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct dw_pcie_rp *pp = irq_data_get_irq_chip_data(data);
	struct keystone_pcie *ks_pcie;
	struct dw_pcie *pci;
	u64 msi_target;

	pci = to_dw_pcie_from_pp(pp);
	ks_pcie = to_keystone_pcie(pci);

	msi_target = ks_pcie->app.start + MSI_IRQ;
	msg->address_lo = lower_32_bits(msi_target);
	msg->address_hi = upper_32_bits(msi_target);
	msg->data = data->hwirq;

	dev_dbg(pci->dev, "msi#%d address_hi %#x address_lo %#x\n",
		(int)data->hwirq, msg->address_hi, msg->address_lo);
}

static int ks_pcie_msi_set_affinity(struct irq_data *irq_data,
				    const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static void ks_pcie_msi_mask(struct irq_data *data)
{
	struct dw_pcie_rp *pp = irq_data_get_irq_chip_data(data);
	struct keystone_pcie *ks_pcie;
	u32 irq = data->hwirq;
	struct dw_pcie *pci;
	unsigned long flags;
	u32 reg_offset;
	u32 bit_pos;

	raw_spin_lock_irqsave(&pp->lock, flags);

	pci = to_dw_pcie_from_pp(pp);
	ks_pcie = to_keystone_pcie(pci);

	reg_offset = irq % 8;
	bit_pos = irq >> 3;

	ks_pcie_app_writel(ks_pcie, MSI_IRQ_ENABLE_CLR(reg_offset),
			   BIT(bit_pos));

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static void ks_pcie_msi_unmask(struct irq_data *data)
{
	struct dw_pcie_rp *pp = irq_data_get_irq_chip_data(data);
	struct keystone_pcie *ks_pcie;
	u32 irq = data->hwirq;
	struct dw_pcie *pci;
	unsigned long flags;
	u32 reg_offset;
	u32 bit_pos;

	raw_spin_lock_irqsave(&pp->lock, flags);

	pci = to_dw_pcie_from_pp(pp);
	ks_pcie = to_keystone_pcie(pci);

	reg_offset = irq % 8;
	bit_pos = irq >> 3;

	ks_pcie_app_writel(ks_pcie, MSI_IRQ_ENABLE_SET(reg_offset),
			   BIT(bit_pos));

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static struct irq_chip ks_pcie_msi_irq_chip = {
	.name = "KEYSTONE-PCI-MSI",
	.irq_ack = ks_pcie_msi_irq_ack,
	.irq_compose_msi_msg = ks_pcie_compose_msi_msg,
	.irq_set_affinity = ks_pcie_msi_set_affinity,
	.irq_mask = ks_pcie_msi_mask,
	.irq_unmask = ks_pcie_msi_unmask,
};

static int ks_pcie_msi_host_init(struct dw_pcie_rp *pp)
{
	pp->msi_irq_chip = &ks_pcie_msi_irq_chip;
	return dw_pcie_allocate_domains(pp);
}

static void ks_pcie_handle_intx_irq(struct keystone_pcie *ks_pcie,
				    int offset)
{
	struct dw_pcie *pci = ks_pcie->pci;
	struct device *dev = pci->dev;
	u32 pending;

	pending = ks_pcie_app_readl(ks_pcie, IRQ_STATUS(offset));

	if (BIT(0) & pending) {
		dev_dbg(dev, ": irq: irq_offset %d", offset);
		generic_handle_domain_irq(ks_pcie->intx_irq_domain, offset);
	}

	/* EOI the INTx interrupt */
	ks_pcie_app_writel(ks_pcie, IRQ_EOI, offset);
}

static void ks_pcie_enable_error_irq(struct keystone_pcie *ks_pcie)
{
	ks_pcie_app_writel(ks_pcie, ERR_IRQ_ENABLE_SET, ERR_IRQ_ALL);
}

static irqreturn_t ks_pcie_handle_error_irq(struct keystone_pcie *ks_pcie)
{
	u32 reg;
	struct device *dev = ks_pcie->pci->dev;

	reg = ks_pcie_app_readl(ks_pcie, ERR_IRQ_STATUS);
	if (!reg)
		return IRQ_NONE;

	if (reg & ERR_SYS)
		dev_err(dev, "System Error\n");

	if (reg & ERR_FATAL)
		dev_err(dev, "Fatal Error\n");

	if (reg & ERR_NONFATAL)
		dev_dbg(dev, "Non Fatal Error\n");

	if (reg & ERR_CORR)
		dev_dbg(dev, "Correctable Error\n");

	if (!ks_pcie->is_am6 && (reg & ERR_AXI))
		dev_err(dev, "AXI tag lookup fatal Error\n");

	if (reg & ERR_AER || (ks_pcie->is_am6 && (reg & AM6_ERR_AER)))
		dev_err(dev, "ECRC Error\n");

	ks_pcie_app_writel(ks_pcie, ERR_IRQ_STATUS, reg);

	return IRQ_HANDLED;
}

static void ks_pcie_ack_intx_irq(struct irq_data *d)
{
}

static void ks_pcie_mask_intx_irq(struct irq_data *d)
{
}

static void ks_pcie_unmask_intx_irq(struct irq_data *d)
{
}

static struct irq_chip ks_pcie_intx_irq_chip = {
	.name = "Keystone-PCI-INTX-IRQ",
	.irq_ack = ks_pcie_ack_intx_irq,
	.irq_mask = ks_pcie_mask_intx_irq,
	.irq_unmask = ks_pcie_unmask_intx_irq,
};

static int ks_pcie_init_intx_irq_map(struct irq_domain *d,
				     unsigned int irq, irq_hw_number_t hw_irq)
{
	irq_set_chip_and_handler(irq, &ks_pcie_intx_irq_chip,
				 handle_level_irq);
	irq_set_chip_data(irq, d->host_data);

	return 0;
}

static const struct irq_domain_ops ks_pcie_intx_irq_domain_ops = {
	.map = ks_pcie_init_intx_irq_map,
	.xlate = irq_domain_xlate_onetwocell,
};

/**
 * ks_pcie_set_dbi_mode() - Set DBI mode to access overlaid BAR mask registers
 * @ks_pcie: A pointer to the keystone_pcie structure which holds the KeyStone
 *	     PCIe host controller driver information.
 *
 * Since modification of dbi_cs2 involves different clock domain, read the
 * status back to ensure the transition is complete.
 */
static void ks_pcie_set_dbi_mode(struct keystone_pcie *ks_pcie)
{
	u32 val;

	val = ks_pcie_app_readl(ks_pcie, CMD_STATUS);
	val |= DBI_CS2;
	ks_pcie_app_writel(ks_pcie, CMD_STATUS, val);

	do {
		val = ks_pcie_app_readl(ks_pcie, CMD_STATUS);
	} while (!(val & DBI_CS2));
}

/**
 * ks_pcie_clear_dbi_mode() - Disable DBI mode
 * @ks_pcie: A pointer to the keystone_pcie structure which holds the KeyStone
 *	     PCIe host controller driver information.
 *
 * Since modification of dbi_cs2 involves different clock domain, read the
 * status back to ensure the transition is complete.
 */
static void ks_pcie_clear_dbi_mode(struct keystone_pcie *ks_pcie)
{
	u32 val;

	val = ks_pcie_app_readl(ks_pcie, CMD_STATUS);
	val &= ~DBI_CS2;
	ks_pcie_app_writel(ks_pcie, CMD_STATUS, val);

	do {
		val = ks_pcie_app_readl(ks_pcie, CMD_STATUS);
	} while (val & DBI_CS2);
}

static void ks_pcie_setup_rc_app_regs(struct keystone_pcie *ks_pcie)
{
	u32 val;
	u32 num_viewport = ks_pcie->num_viewport;
	struct dw_pcie *pci = ks_pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	u64 start, end;
	struct resource *mem;
	int i;

	mem = resource_list_first_type(&pp->bridge->windows, IORESOURCE_MEM)->res;
	start = mem->start;
	end = mem->end;

	/* Disable BARs for inbound access */
	ks_pcie_set_dbi_mode(ks_pcie);
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0);
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_1, 0);
	ks_pcie_clear_dbi_mode(ks_pcie);

	if (ks_pcie->is_am6)
		return;

	val = ilog2(OB_WIN_SIZE);
	ks_pcie_app_writel(ks_pcie, OB_SIZE, val);

	/* Using Direct 1:1 mapping of RC <-> PCI memory space */
	for (i = 0; i < num_viewport && (start < end); i++) {
		ks_pcie_app_writel(ks_pcie, OB_OFFSET_INDEX(i),
				   lower_32_bits(start) | OB_ENABLEN);
		ks_pcie_app_writel(ks_pcie, OB_OFFSET_HI(i),
				   upper_32_bits(start));
		start += OB_WIN_SIZE * SZ_1M;
	}

	val = ks_pcie_app_readl(ks_pcie, CMD_STATUS);
	val |= OB_XLAT_EN_VAL;
	ks_pcie_app_writel(ks_pcie, CMD_STATUS, val);
}

static void __iomem *ks_pcie_other_map_bus(struct pci_bus *bus,
					   unsigned int devfn, int where)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);
	u32 reg;

	reg = CFG_BUS(bus->number) | CFG_DEVICE(PCI_SLOT(devfn)) |
		CFG_FUNC(PCI_FUNC(devfn));
	if (!pci_is_root_bus(bus->parent))
		reg |= CFG_TYPE1;
	ks_pcie_app_writel(ks_pcie, CFG_SETUP, reg);

	return pp->va_cfg0_base + where;
}

static struct pci_ops ks_child_pcie_ops = {
	.map_bus = ks_pcie_other_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
};

/**
 * ks_pcie_v3_65_add_bus() - keystone add_bus post initialization
 * @bus: A pointer to the PCI bus structure.
 *
 * This sets BAR0 to enable inbound access for MSI_IRQ register
 */
static int ks_pcie_v3_65_add_bus(struct pci_bus *bus)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);

	if (!pci_is_root_bus(bus))
		return 0;

	/* Configure and set up BAR0 */
	ks_pcie_set_dbi_mode(ks_pcie);

	/* Enable BAR0 */
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, 1);
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, SZ_4K - 1);

	ks_pcie_clear_dbi_mode(ks_pcie);

	 /*
	  * For BAR0, just setting bus address for inbound writes (MSI) should
	  * be sufficient.  Use physical address to avoid any conflicts.
	  */
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, ks_pcie->app.start);

	return 0;
}

static struct pci_ops ks_pcie_ops = {
	.map_bus = dw_pcie_own_conf_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
	.add_bus = ks_pcie_v3_65_add_bus,
};

/**
 * ks_pcie_link_up() - Check if link up
 * @pci: A pointer to the dw_pcie structure which holds the DesignWare PCIe host
 *	 controller driver information.
 */
static int ks_pcie_link_up(struct dw_pcie *pci)
{
	u32 val;

	val = dw_pcie_readl_dbi(pci, PCIE_PORT_DEBUG0);
	val &= PORT_LOGIC_LTSSM_STATE_MASK;
	return (val == PORT_LOGIC_LTSSM_STATE_L0);
}

static void ks_pcie_stop_link(struct dw_pcie *pci)
{
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);
	u32 val;

	/* Disable Link training */
	val = ks_pcie_app_readl(ks_pcie, CMD_STATUS);
	val &= ~LTSSM_EN_VAL;
	ks_pcie_app_writel(ks_pcie, CMD_STATUS, val);
}

static int ks_pcie_start_link(struct dw_pcie *pci)
{
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);
	u32 val;

	/* Initiate Link Training */
	val = ks_pcie_app_readl(ks_pcie, CMD_STATUS);
	ks_pcie_app_writel(ks_pcie, CMD_STATUS, LTSSM_EN_VAL | val);

	return 0;
}

static void ks_pcie_quirk(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->bus;
	struct pci_dev *bridge;
	static const struct pci_device_id rc_pci_devids[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCIE_RC_K2HK),
		 .class = PCI_CLASS_BRIDGE_PCI_NORMAL, .class_mask = ~0, },
		{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCIE_RC_K2E),
		 .class = PCI_CLASS_BRIDGE_PCI_NORMAL, .class_mask = ~0, },
		{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCIE_RC_K2L),
		 .class = PCI_CLASS_BRIDGE_PCI_NORMAL, .class_mask = ~0, },
		{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCIE_RC_K2G),
		 .class = PCI_CLASS_BRIDGE_PCI_NORMAL, .class_mask = ~0, },
		{ 0, },
	};

	if (pci_is_root_bus(bus))
		bridge = dev;

	/* look for the host bridge */
	while (!pci_is_root_bus(bus)) {
		bridge = bus->self;
		bus = bus->parent;
	}

	if (!bridge)
		return;

	/*
	 * Keystone PCI controller has a h/w limitation of
	 * 256 bytes maximum read request size.  It can't handle
	 * anything higher than this.  So force this limit on
	 * all downstream devices.
	 */
	if (pci_match_id(rc_pci_devids, bridge)) {
		if (pcie_get_readrq(dev) > 256) {
			dev_info(&dev->dev, "limiting MRRS to 256\n");
			pcie_set_readrq(dev, 256);
		}
	}
}
DECLARE_PCI_FIXUP_ENABLE(PCI_ANY_ID, PCI_ANY_ID, ks_pcie_quirk);

static void ks_pcie_msi_irq_handler(struct irq_desc *desc)
{
	unsigned int irq = desc->irq_data.hwirq;
	struct keystone_pcie *ks_pcie = irq_desc_get_handler_data(desc);
	u32 offset = irq - ks_pcie->msi_host_irq;
	struct dw_pcie *pci = ks_pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = pci->dev;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u32 vector, reg, pos;

	dev_dbg(dev, "%s, irq %d\n", __func__, irq);

	/*
	 * The chained irq handler installation would have replaced normal
	 * interrupt driver handler so we need to take care of mask/unmask and
	 * ack operation.
	 */
	chained_irq_enter(chip, desc);

	reg = ks_pcie_app_readl(ks_pcie, MSI_IRQ_STATUS(offset));
	/*
	 * MSI0 status bit 0-3 shows vectors 0, 8, 16, 24, MSI1 status bit
	 * shows 1, 9, 17, 25 and so forth
	 */
	for (pos = 0; pos < 4; pos++) {
		if (!(reg & BIT(pos)))
			continue;

		vector = offset + (pos << 3);
		dev_dbg(dev, "irq: bit %d, vector %d\n", pos, vector);
		generic_handle_domain_irq(pp->irq_domain, vector);
	}

	chained_irq_exit(chip, desc);
}

/**
 * ks_pcie_intx_irq_handler() - Handle INTX interrupt
 * @desc: Pointer to irq descriptor
 *
 * Traverse through pending INTX interrupts and invoke handler for each. Also
 * takes care of interrupt controller level mask/ack operation.
 */
static void ks_pcie_intx_irq_handler(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);
	struct keystone_pcie *ks_pcie = irq_desc_get_handler_data(desc);
	struct dw_pcie *pci = ks_pcie->pci;
	struct device *dev = pci->dev;
	u32 irq_offset = irq - ks_pcie->intx_host_irqs[0];
	struct irq_chip *chip = irq_desc_get_chip(desc);

	dev_dbg(dev, ": Handling INTX irq %d\n", irq);

	/*
	 * The chained irq handler installation would have replaced normal
	 * interrupt driver handler so we need to take care of mask/unmask and
	 * ack operation.
	 */
	chained_irq_enter(chip, desc);
	ks_pcie_handle_intx_irq(ks_pcie, irq_offset);
	chained_irq_exit(chip, desc);
}

static int ks_pcie_config_msi_irq(struct keystone_pcie *ks_pcie)
{
	struct device *dev = ks_pcie->pci->dev;
	struct device_node *np = ks_pcie->np;
	struct device_node *intc_np;
	struct irq_data *irq_data;
	int irq_count, irq, ret, i;

	if (!IS_ENABLED(CONFIG_PCI_MSI))
		return 0;

	intc_np = of_get_child_by_name(np, "msi-interrupt-controller");
	if (!intc_np) {
		if (ks_pcie->is_am6)
			return 0;
		dev_warn(dev, "msi-interrupt-controller node is absent\n");
		return -EINVAL;
	}

	irq_count = of_irq_count(intc_np);
	if (!irq_count) {
		dev_err(dev, "No IRQ entries in msi-interrupt-controller\n");
		ret = -EINVAL;
		goto err;
	}

	for (i = 0; i < irq_count; i++) {
		irq = irq_of_parse_and_map(intc_np, i);
		if (!irq) {
			ret = -EINVAL;
			goto err;
		}

		if (!ks_pcie->msi_host_irq) {
			irq_data = irq_get_irq_data(irq);
			if (!irq_data) {
				ret = -EINVAL;
				goto err;
			}
			ks_pcie->msi_host_irq = irq_data->hwirq;
		}

		irq_set_chained_handler_and_data(irq, ks_pcie_msi_irq_handler,
						 ks_pcie);
	}

	of_node_put(intc_np);
	return 0;

err:
	of_node_put(intc_np);
	return ret;
}

static int ks_pcie_config_intx_irq(struct keystone_pcie *ks_pcie)
{
	struct device *dev = ks_pcie->pci->dev;
	struct irq_domain *intx_irq_domain;
	struct device_node *np = ks_pcie->np;
	struct device_node *intc_np;
	int irq_count, irq, ret = 0, i;

	intc_np = of_get_child_by_name(np, "legacy-interrupt-controller");
	if (!intc_np) {
		/*
		 * Since INTX interrupts are modeled as edge-interrupts in
		 * AM6, keep it disabled for now.
		 */
		if (ks_pcie->is_am6)
			return 0;
		dev_warn(dev, "legacy-interrupt-controller node is absent\n");
		return -EINVAL;
	}

	irq_count = of_irq_count(intc_np);
	if (!irq_count) {
		dev_err(dev, "No IRQ entries in legacy-interrupt-controller\n");
		ret = -EINVAL;
		goto err;
	}

	for (i = 0; i < irq_count; i++) {
		irq = irq_of_parse_and_map(intc_np, i);
		if (!irq) {
			ret = -EINVAL;
			goto err;
		}
		ks_pcie->intx_host_irqs[i] = irq;

		irq_set_chained_handler_and_data(irq,
						 ks_pcie_intx_irq_handler,
						 ks_pcie);
	}

	intx_irq_domain = irq_domain_add_linear(intc_np, PCI_NUM_INTX,
					&ks_pcie_intx_irq_domain_ops, NULL);
	if (!intx_irq_domain) {
		dev_err(dev, "Failed to add irq domain for INTX irqs\n");
		ret = -EINVAL;
		goto err;
	}
	ks_pcie->intx_irq_domain = intx_irq_domain;

	for (i = 0; i < PCI_NUM_INTX; i++)
		ks_pcie_app_writel(ks_pcie, IRQ_ENABLE_SET(i), INTx_EN);

err:
	of_node_put(intc_np);
	return ret;
}

#ifdef CONFIG_ARM
/*
 * When a PCI device does not exist during config cycles, keystone host
 * gets a bus error instead of returning 0xffffffff (PCI_ERROR_RESPONSE).
 * This handler always returns 0 for this kind of fault.
 */
static int ks_pcie_fault(unsigned long addr, unsigned int fsr,
			 struct pt_regs *regs)
{
	unsigned long instr = *(unsigned long *) instruction_pointer(regs);

	if ((instr & 0x0e100090) == 0x00100090) {
		int reg = (instr >> 12) & 15;

		regs->uregs[reg] = -1;
		regs->ARM_pc += 4;
	}

	return 0;
}
#endif

static int __init ks_pcie_init_id(struct keystone_pcie *ks_pcie)
{
	int ret;
	unsigned int id;
	struct regmap *devctrl_regs;
	struct dw_pcie *pci = ks_pcie->pci;
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;
	struct of_phandle_args args;
	unsigned int offset = 0;

	devctrl_regs = syscon_regmap_lookup_by_phandle(np, "ti,syscon-pcie-id");
	if (IS_ERR(devctrl_regs))
		return PTR_ERR(devctrl_regs);

	/* Do not error out to maintain old DT compatibility */
	ret = of_parse_phandle_with_fixed_args(np, "ti,syscon-pcie-id", 1, 0, &args);
	if (!ret)
		offset = args.args[0];

	ret = regmap_read(devctrl_regs, offset, &id);
	if (ret)
		return ret;

	dw_pcie_dbi_ro_wr_en(pci);
	dw_pcie_writew_dbi(pci, PCI_VENDOR_ID, id & PCIE_VENDORID_MASK);
	dw_pcie_writew_dbi(pci, PCI_DEVICE_ID, id >> PCIE_DEVICEID_SHIFT);
	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}

static int __init ks_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);
	int ret;

	pp->bridge->ops = &ks_pcie_ops;
	if (!ks_pcie->is_am6)
		pp->bridge->child_ops = &ks_child_pcie_ops;

	ret = ks_pcie_config_intx_irq(ks_pcie);
	if (ret)
		return ret;

	ret = ks_pcie_config_msi_irq(ks_pcie);
	if (ret)
		return ret;

	ks_pcie_stop_link(pci);
	ks_pcie_setup_rc_app_regs(ks_pcie);
	writew(PCI_IO_RANGE_TYPE_32 | (PCI_IO_RANGE_TYPE_32 << 8),
			pci->dbi_base + PCI_IO_BASE);

	ret = ks_pcie_init_id(ks_pcie);
	if (ret < 0)
		return ret;

#ifdef CONFIG_ARM
	/*
	 * PCIe access errors that result into OCP errors are caught by ARM as
	 * "External aborts"
	 */
	hook_fault_code(17, ks_pcie_fault, SIGBUS, 0,
			"Asynchronous external abort");
#endif

	return 0;
}

static const struct dw_pcie_host_ops ks_pcie_host_ops = {
	.init = ks_pcie_host_init,
	.msi_init = ks_pcie_msi_host_init,
};

static const struct dw_pcie_host_ops ks_pcie_am654_host_ops = {
	.init = ks_pcie_host_init,
};

static irqreturn_t ks_pcie_err_irq_handler(int irq, void *priv)
{
	struct keystone_pcie *ks_pcie = priv;

	return ks_pcie_handle_error_irq(ks_pcie);
}

static void ks_pcie_am654_write_dbi2(struct dw_pcie *pci, void __iomem *base,
				     u32 reg, size_t size, u32 val)
{
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);

	ks_pcie_set_dbi_mode(ks_pcie);
	dw_pcie_write(base + reg, size, val);
	ks_pcie_clear_dbi_mode(ks_pcie);
}

static const struct dw_pcie_ops ks_pcie_dw_pcie_ops = {
	.start_link = ks_pcie_start_link,
	.stop_link = ks_pcie_stop_link,
	.link_up = ks_pcie_link_up,
	.write_dbi2 = ks_pcie_am654_write_dbi2,
};

static void ks_pcie_am654_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	int flags;

	ep->page_size = AM654_WIN_SIZE;
	flags = PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_32;
	dw_pcie_writel_dbi2(pci, PCI_BASE_ADDRESS_0, APP_ADDR_SPACE_0 - 1);
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, flags);
}

static void ks_pcie_am654_raise_intx_irq(struct keystone_pcie *ks_pcie)
{
	struct dw_pcie *pci = ks_pcie->pci;
	u8 int_pin;

	int_pin = dw_pcie_readb_dbi(pci, PCI_INTERRUPT_PIN);
	if (int_pin == 0 || int_pin > 4)
		return;

	ks_pcie_app_writel(ks_pcie, PCIE_LEGACY_IRQ_ENABLE_SET(int_pin),
			   INT_ENABLE);
	ks_pcie_app_writel(ks_pcie, PCIE_EP_IRQ_SET, INT_ENABLE);
	mdelay(1);
	ks_pcie_app_writel(ks_pcie, PCIE_EP_IRQ_CLR, INT_ENABLE);
	ks_pcie_app_writel(ks_pcie, PCIE_LEGACY_IRQ_ENABLE_CLR(int_pin),
			   INT_ENABLE);
}

static int ks_pcie_am654_raise_irq(struct dw_pcie_ep *ep, u8 func_no,
				   unsigned int type, u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);

	switch (type) {
	case PCI_IRQ_INTX:
		ks_pcie_am654_raise_intx_irq(ks_pcie);
		break;
	case PCI_IRQ_MSI:
		dw_pcie_ep_raise_msi_irq(ep, func_no, interrupt_num);
		break;
	case PCI_IRQ_MSIX:
		dw_pcie_ep_raise_msix_irq(ep, func_no, interrupt_num);
		break;
	default:
		dev_err(pci->dev, "UNKNOWN IRQ type\n");
		return -EINVAL;
	}

	return 0;
}

static const struct pci_epc_features ks_pcie_am654_epc_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = true,
	.bar[BAR_0] = { .type = BAR_RESERVED, },
	.bar[BAR_1] = { .type = BAR_RESERVED, },
	.bar[BAR_2] = { .type = BAR_FIXED, .fixed_size = SZ_1M, },
	.bar[BAR_3] = { .type = BAR_FIXED, .fixed_size = SZ_64K, },
	.bar[BAR_4] = { .type = BAR_FIXED, .fixed_size = 256, },
	.bar[BAR_5] = { .type = BAR_FIXED, .fixed_size = SZ_1M, },
	.align = SZ_1M,
};

static const struct pci_epc_features*
ks_pcie_am654_get_features(struct dw_pcie_ep *ep)
{
	return &ks_pcie_am654_epc_features;
}

static const struct dw_pcie_ep_ops ks_pcie_am654_ep_ops = {
	.init = ks_pcie_am654_ep_init,
	.raise_irq = ks_pcie_am654_raise_irq,
	.get_features = &ks_pcie_am654_get_features,
};

static void ks_pcie_disable_phy(struct keystone_pcie *ks_pcie)
{
	int num_lanes = ks_pcie->num_lanes;

	while (num_lanes--) {
		phy_power_off(ks_pcie->phy[num_lanes]);
		phy_exit(ks_pcie->phy[num_lanes]);
	}
}

static int ks_pcie_enable_phy(struct keystone_pcie *ks_pcie)
{
	int i;
	int ret;
	int num_lanes = ks_pcie->num_lanes;

	for (i = 0; i < num_lanes; i++) {
		ret = phy_reset(ks_pcie->phy[i]);
		if (ret < 0)
			goto err_phy;

		ret = phy_init(ks_pcie->phy[i]);
		if (ret < 0)
			goto err_phy;

		ret = phy_power_on(ks_pcie->phy[i]);
		if (ret < 0) {
			phy_exit(ks_pcie->phy[i]);
			goto err_phy;
		}
	}

	return 0;

err_phy:
	while (--i >= 0) {
		phy_power_off(ks_pcie->phy[i]);
		phy_exit(ks_pcie->phy[i]);
	}

	return ret;
}

static int ks_pcie_set_mode(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct of_phandle_args args;
	unsigned int offset = 0;
	struct regmap *syscon;
	u32 val;
	u32 mask;
	int ret = 0;

	syscon = syscon_regmap_lookup_by_phandle(np, "ti,syscon-pcie-mode");
	if (IS_ERR(syscon))
		return 0;

	/* Do not error out to maintain old DT compatibility */
	ret = of_parse_phandle_with_fixed_args(np, "ti,syscon-pcie-mode", 1, 0, &args);
	if (!ret)
		offset = args.args[0];

	mask = KS_PCIE_DEV_TYPE_MASK | KS_PCIE_SYSCLOCKOUTEN;
	val = KS_PCIE_DEV_TYPE(RC) | KS_PCIE_SYSCLOCKOUTEN;

	ret = regmap_update_bits(syscon, offset, mask, val);
	if (ret) {
		dev_err(dev, "failed to set pcie mode\n");
		return ret;
	}

	return 0;
}

static int ks_pcie_am654_set_mode(struct device *dev,
				  enum dw_pcie_device_mode mode)
{
	struct device_node *np = dev->of_node;
	struct of_phandle_args args;
	unsigned int offset = 0;
	struct regmap *syscon;
	u32 val;
	u32 mask;
	int ret = 0;

	syscon = syscon_regmap_lookup_by_phandle(np, "ti,syscon-pcie-mode");
	if (IS_ERR(syscon))
		return 0;

	/* Do not error out to maintain old DT compatibility */
	ret = of_parse_phandle_with_fixed_args(np, "ti,syscon-pcie-mode", 1, 0, &args);
	if (!ret)
		offset = args.args[0];

	mask = AM654_PCIE_DEV_TYPE_MASK;

	switch (mode) {
	case DW_PCIE_RC_TYPE:
		val = RC;
		break;
	case DW_PCIE_EP_TYPE:
		val = EP;
		break;
	default:
		dev_err(dev, "INVALID device type %d\n", mode);
		return -EINVAL;
	}

	ret = regmap_update_bits(syscon, offset, mask, val);
	if (ret) {
		dev_err(dev, "failed to set pcie mode\n");
		return ret;
	}

	return 0;
}

static const struct ks_pcie_of_data ks_pcie_rc_of_data = {
	.host_ops = &ks_pcie_host_ops,
	.version = DW_PCIE_VER_365A,
};

static const struct ks_pcie_of_data ks_pcie_am654_rc_of_data = {
	.host_ops = &ks_pcie_am654_host_ops,
	.mode = DW_PCIE_RC_TYPE,
	.version = DW_PCIE_VER_490A,
};

static const struct ks_pcie_of_data ks_pcie_am654_ep_of_data = {
	.ep_ops = &ks_pcie_am654_ep_ops,
	.mode = DW_PCIE_EP_TYPE,
	.version = DW_PCIE_VER_490A,
};

static const struct of_device_id ks_pcie_of_match[] = {
	{
		.type = "pci",
		.data = &ks_pcie_rc_of_data,
		.compatible = "ti,keystone-pcie",
	},
	{
		.data = &ks_pcie_am654_rc_of_data,
		.compatible = "ti,am654-pcie-rc",
	},
	{
		.data = &ks_pcie_am654_ep_of_data,
		.compatible = "ti,am654-pcie-ep",
	},
	{ },
};

static int ks_pcie_probe(struct platform_device *pdev)
{
	const struct dw_pcie_host_ops *host_ops;
	const struct dw_pcie_ep_ops *ep_ops;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct ks_pcie_of_data *data;
	enum dw_pcie_device_mode mode;
	struct dw_pcie *pci;
	struct keystone_pcie *ks_pcie;
	struct device_link **link;
	struct gpio_desc *gpiod;
	struct resource *res;
	void __iomem *base;
	u32 num_viewport;
	struct phy **phy;
	u32 num_lanes;
	char name[10];
	u32 version;
	int ret;
	int irq;
	int i;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	version = data->version;
	host_ops = data->host_ops;
	ep_ops = data->ep_ops;
	mode = data->mode;

	ks_pcie = devm_kzalloc(dev, sizeof(*ks_pcie), GFP_KERNEL);
	if (!ks_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "app");
	ks_pcie->va_app_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ks_pcie->va_app_base))
		return PTR_ERR(ks_pcie->va_app_base);

	ks_pcie->app = *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbics");
	base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	if (of_device_is_compatible(np, "ti,am654-pcie-rc"))
		ks_pcie->is_am6 = true;

	pci->dbi_base = base;
	pci->dbi_base2 = base;
	pci->dev = dev;
	pci->ops = &ks_pcie_dw_pcie_ops;
	pci->version = version;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = request_irq(irq, ks_pcie_err_irq_handler, IRQF_SHARED,
			  "ks-pcie-error-irq", ks_pcie);
	if (ret < 0) {
		dev_err(dev, "failed to request error IRQ %d\n",
			irq);
		return ret;
	}

	ret = of_property_read_u32(np, "num-lanes", &num_lanes);
	if (ret)
		num_lanes = 1;

	phy = devm_kzalloc(dev, sizeof(*phy) * num_lanes, GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	link = devm_kzalloc(dev, sizeof(*link) * num_lanes, GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	for (i = 0; i < num_lanes; i++) {
		snprintf(name, sizeof(name), "pcie-phy%d", i);
		phy[i] = devm_phy_optional_get(dev, name);
		if (IS_ERR(phy[i])) {
			ret = PTR_ERR(phy[i]);
			goto err_link;
		}

		if (!phy[i])
			continue;

		link[i] = device_link_add(dev, &phy[i]->dev, DL_FLAG_STATELESS);
		if (!link[i]) {
			ret = -EINVAL;
			goto err_link;
		}
	}

	ks_pcie->np = np;
	ks_pcie->pci = pci;
	ks_pcie->link = link;
	ks_pcie->num_lanes = num_lanes;
	ks_pcie->phy = phy;

	gpiod = devm_gpiod_get_optional(dev, "reset",
					GPIOD_OUT_LOW);
	if (IS_ERR(gpiod)) {
		ret = PTR_ERR(gpiod);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get reset GPIO\n");
		goto err_link;
	}

	/* Obtain references to the PHYs */
	for (i = 0; i < num_lanes; i++)
		phy_pm_runtime_get_sync(ks_pcie->phy[i]);

	ret = ks_pcie_enable_phy(ks_pcie);

	/* Release references to the PHYs */
	for (i = 0; i < num_lanes; i++)
		phy_pm_runtime_put_sync(ks_pcie->phy[i]);

	if (ret) {
		dev_err(dev, "failed to enable phy\n");
		goto err_link;
	}

	platform_set_drvdata(pdev, ks_pcie);
	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_get_sync failed\n");
		goto err_get_sync;
	}

	if (dw_pcie_ver_is_ge(pci, 480A))
		ret = ks_pcie_am654_set_mode(dev, mode);
	else
		ret = ks_pcie_set_mode(dev);
	if (ret < 0)
		goto err_get_sync;

	switch (mode) {
	case DW_PCIE_RC_TYPE:
		if (!IS_ENABLED(CONFIG_PCI_KEYSTONE_HOST)) {
			ret = -ENODEV;
			goto err_get_sync;
		}

		ret = of_property_read_u32(np, "num-viewport", &num_viewport);
		if (ret < 0) {
			dev_err(dev, "unable to read *num-viewport* property\n");
			goto err_get_sync;
		}

		/*
		 * "Power Sequencing and Reset Signal Timings" table in
		 * PCI EXPRESS CARD ELECTROMECHANICAL SPECIFICATION, REV. 2.0
		 * indicates PERST# should be deasserted after minimum of 100us
		 * once REFCLK is stable. The REFCLK to the connector in RC
		 * mode is selected while enabling the PHY. So deassert PERST#
		 * after 100 us.
		 */
		if (gpiod) {
			usleep_range(100, 200);
			gpiod_set_value_cansleep(gpiod, 1);
		}

		ks_pcie->num_viewport = num_viewport;
		pci->pp.ops = host_ops;
		ret = dw_pcie_host_init(&pci->pp);
		if (ret < 0)
			goto err_get_sync;
		break;
	case DW_PCIE_EP_TYPE:
		if (!IS_ENABLED(CONFIG_PCI_KEYSTONE_EP)) {
			ret = -ENODEV;
			goto err_get_sync;
		}

		pci->ep.ops = ep_ops;
		ret = dw_pcie_ep_init(&pci->ep);
		if (ret < 0)
			goto err_get_sync;
		break;
	default:
		dev_err(dev, "INVALID device type %d\n", mode);
	}

	ks_pcie_enable_error_irq(ks_pcie);

	return 0;

err_get_sync:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);
	ks_pcie_disable_phy(ks_pcie);

err_link:
	while (--i >= 0 && link[i])
		device_link_del(link[i]);

	return ret;
}

static void ks_pcie_remove(struct platform_device *pdev)
{
	struct keystone_pcie *ks_pcie = platform_get_drvdata(pdev);
	struct device_link **link = ks_pcie->link;
	int num_lanes = ks_pcie->num_lanes;
	struct device *dev = &pdev->dev;

	pm_runtime_put(dev);
	pm_runtime_disable(dev);
	ks_pcie_disable_phy(ks_pcie);
	while (num_lanes--)
		device_link_del(link[num_lanes]);
}

static struct platform_driver ks_pcie_driver = {
	.probe  = ks_pcie_probe,
	.remove_new = ks_pcie_remove,
	.driver = {
		.name	= "keystone-pcie",
		.of_match_table = ks_pcie_of_match,
	},
};
builtin_platform_driver(ks_pcie_driver);
