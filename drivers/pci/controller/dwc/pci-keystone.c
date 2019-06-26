// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Texas Instruments Keystone SoCs
 *
 * Copyright (C) 2013-2014 Texas Instruments., Ltd.
 *		http://www.ti.com
 *
 * Author: Murali Karicheri <m-karicheri2@ti.com>
 * Implementation based on pci-exynos.c and pcie-designware.c
 */

#include <linux/clk.h>
#include <linux/delay.h>
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
#define SPACE0_REMOTE_CFG_OFFSET	0x1000
#define OB_OFFSET_INDEX(n)		(0x200 + (8 * (n)))
#define OB_OFFSET_HI(n)			(0x204 + (8 * (n)))
#define OB_ENABLEN			BIT(0)
#define OB_WIN_SIZE			8	/* 8MB */

/* IRQ register defines */
#define IRQ_EOI				0x050
#define IRQ_STATUS			0x184
#define IRQ_ENABLE_SET			0x188
#define IRQ_ENABLE_CLR			0x18c

#define MSI_IRQ				0x054
#define MSI0_IRQ_STATUS			0x104
#define MSI0_IRQ_ENABLE_SET		0x108
#define MSI0_IRQ_ENABLE_CLR		0x10c
#define IRQ_STATUS			0x184
#define MSI_IRQ_OFFSET			4

#define ERR_IRQ_STATUS			0x1c4
#define ERR_IRQ_ENABLE_SET		0x1c8
#define ERR_AER				BIT(5)	/* ECRC error */
#define ERR_AXI				BIT(4)	/* AXI tag lookup fatal error */
#define ERR_CORR			BIT(3)	/* Correctable error */
#define ERR_NONFATAL			BIT(2)	/* Non-fatal error */
#define ERR_FATAL			BIT(1)	/* Fatal error */
#define ERR_SYS				BIT(0)	/* System error */
#define ERR_IRQ_ALL			(ERR_AER | ERR_AXI | ERR_CORR | \
					 ERR_NONFATAL | ERR_FATAL | ERR_SYS)

#define MAX_MSI_HOST_IRQS		8
/* PCIE controller device IDs */
#define PCIE_RC_K2HK			0xb008
#define PCIE_RC_K2E			0xb009
#define PCIE_RC_K2L			0xb00a
#define PCIE_RC_K2G			0xb00b

#define to_keystone_pcie(x)		dev_get_drvdata((x)->dev)

struct keystone_pcie {
	struct dw_pcie		*pci;
	/* PCI Device ID */
	u32			device_id;
	int			num_legacy_host_irqs;
	int			legacy_host_irqs[PCI_NUM_INTX];
	struct			device_node *legacy_intc_np;

	int			num_msi_host_irqs;
	int			msi_host_irqs[MAX_MSI_HOST_IRQS];
	int			num_lanes;
	u32			num_viewport;
	struct phy		**phy;
	struct device_link	**link;
	struct			device_node *msi_intc_np;
	struct irq_domain	*legacy_irq_domain;
	struct device_node	*np;

	int error_irq;

	/* Application register space */
	void __iomem		*va_app_base;	/* DT 1st resource */
	struct resource		app;
};

static inline void update_reg_offset_bit_pos(u32 offset, u32 *reg_offset,
					     u32 *bit_pos)
{
	*reg_offset = offset % 8;
	*bit_pos = offset >> 3;
}

static phys_addr_t ks_pcie_get_msi_addr(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);

	return ks_pcie->app.start + MSI_IRQ;
}

static u32 ks_pcie_app_readl(struct keystone_pcie *ks_pcie, u32 offset)
{
	return readl(ks_pcie->va_app_base + offset);
}

static void ks_pcie_app_writel(struct keystone_pcie *ks_pcie, u32 offset,
			       u32 val)
{
	writel(val, ks_pcie->va_app_base + offset);
}

static void ks_pcie_handle_msi_irq(struct keystone_pcie *ks_pcie, int offset)
{
	struct dw_pcie *pci = ks_pcie->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = pci->dev;
	u32 pending, vector;
	int src, virq;

	pending = ks_pcie_app_readl(ks_pcie, MSI0_IRQ_STATUS + (offset << 4));

	/*
	 * MSI0 status bit 0-3 shows vectors 0, 8, 16, 24, MSI1 status bit
	 * shows 1, 9, 17, 25 and so forth
	 */
	for (src = 0; src < 4; src++) {
		if (BIT(src) & pending) {
			vector = offset + (src << 3);
			virq = irq_linear_revmap(pp->irq_domain, vector);
			dev_dbg(dev, "irq: bit %d, vector %d, virq %d\n",
				src, vector, virq);
			generic_handle_irq(virq);
		}
	}
}

static void ks_pcie_msi_irq_ack(int irq, struct pcie_port *pp)
{
	u32 reg_offset, bit_pos;
	struct keystone_pcie *ks_pcie;
	struct dw_pcie *pci;

	pci = to_dw_pcie_from_pp(pp);
	ks_pcie = to_keystone_pcie(pci);
	update_reg_offset_bit_pos(irq, &reg_offset, &bit_pos);

	ks_pcie_app_writel(ks_pcie, MSI0_IRQ_STATUS + (reg_offset << 4),
			   BIT(bit_pos));
	ks_pcie_app_writel(ks_pcie, IRQ_EOI, reg_offset + MSI_IRQ_OFFSET);
}

static void ks_pcie_msi_set_irq(struct pcie_port *pp, int irq)
{
	u32 reg_offset, bit_pos;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);

	update_reg_offset_bit_pos(irq, &reg_offset, &bit_pos);
	ks_pcie_app_writel(ks_pcie, MSI0_IRQ_ENABLE_SET + (reg_offset << 4),
			   BIT(bit_pos));
}

static void ks_pcie_msi_clear_irq(struct pcie_port *pp, int irq)
{
	u32 reg_offset, bit_pos;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);

	update_reg_offset_bit_pos(irq, &reg_offset, &bit_pos);
	ks_pcie_app_writel(ks_pcie, MSI0_IRQ_ENABLE_CLR + (reg_offset << 4),
			   BIT(bit_pos));
}

static int ks_pcie_msi_host_init(struct pcie_port *pp)
{
	return dw_pcie_allocate_domains(pp);
}

static void ks_pcie_enable_legacy_irqs(struct keystone_pcie *ks_pcie)
{
	int i;

	for (i = 0; i < PCI_NUM_INTX; i++)
		ks_pcie_app_writel(ks_pcie, IRQ_ENABLE_SET + (i << 4), 0x1);
}

static void ks_pcie_handle_legacy_irq(struct keystone_pcie *ks_pcie,
				      int offset)
{
	struct dw_pcie *pci = ks_pcie->pci;
	struct device *dev = pci->dev;
	u32 pending;
	int virq;

	pending = ks_pcie_app_readl(ks_pcie, IRQ_STATUS + (offset << 4));

	if (BIT(0) & pending) {
		virq = irq_linear_revmap(ks_pcie->legacy_irq_domain, offset);
		dev_dbg(dev, ": irq: irq_offset %d, virq %d\n", offset, virq);
		generic_handle_irq(virq);
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

	if (reg & ERR_AXI)
		dev_err(dev, "AXI tag lookup fatal Error\n");

	if (reg & ERR_AER)
		dev_err(dev, "ECRC Error\n");

	ks_pcie_app_writel(ks_pcie, ERR_IRQ_STATUS, reg);

	return IRQ_HANDLED;
}

static void ks_pcie_ack_legacy_irq(struct irq_data *d)
{
}

static void ks_pcie_mask_legacy_irq(struct irq_data *d)
{
}

static void ks_pcie_unmask_legacy_irq(struct irq_data *d)
{
}

static struct irq_chip ks_pcie_legacy_irq_chip = {
	.name = "Keystone-PCI-Legacy-IRQ",
	.irq_ack = ks_pcie_ack_legacy_irq,
	.irq_mask = ks_pcie_mask_legacy_irq,
	.irq_unmask = ks_pcie_unmask_legacy_irq,
};

static int ks_pcie_init_legacy_irq_map(struct irq_domain *d,
				       unsigned int irq,
				       irq_hw_number_t hw_irq)
{
	irq_set_chip_and_handler(irq, &ks_pcie_legacy_irq_chip,
				 handle_level_irq);
	irq_set_chip_data(irq, d->host_data);

	return 0;
}

static const struct irq_domain_ops ks_pcie_legacy_irq_domain_ops = {
	.map = ks_pcie_init_legacy_irq_map,
	.xlate = irq_domain_xlate_onetwocell,
};

/**
 * ks_pcie_set_dbi_mode() - Set DBI mode to access overlaid BAR mask
 * registers
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
	struct pcie_port *pp = &pci->pp;
	u64 start = pp->mem->start;
	u64 end = pp->mem->end;
	int i;

	/* Disable BARs for inbound access */
	ks_pcie_set_dbi_mode(ks_pcie);
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0);
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_1, 0);
	ks_pcie_clear_dbi_mode(ks_pcie);

	val = ilog2(OB_WIN_SIZE);
	ks_pcie_app_writel(ks_pcie, OB_SIZE, val);

	/* Using Direct 1:1 mapping of RC <-> PCI memory space */
	for (i = 0; i < num_viewport && (start < end); i++) {
		ks_pcie_app_writel(ks_pcie, OB_OFFSET_INDEX(i),
				   lower_32_bits(start) | OB_ENABLEN);
		ks_pcie_app_writel(ks_pcie, OB_OFFSET_HI(i),
				   upper_32_bits(start));
		start += OB_WIN_SIZE;
	}

	val = ks_pcie_app_readl(ks_pcie, CMD_STATUS);
	val |= OB_XLAT_EN_VAL;
	ks_pcie_app_writel(ks_pcie, CMD_STATUS, val);
}

static int ks_pcie_rd_other_conf(struct pcie_port *pp, struct pci_bus *bus,
				 unsigned int devfn, int where, int size,
				 u32 *val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);
	u32 reg;

	reg = CFG_BUS(bus->number) | CFG_DEVICE(PCI_SLOT(devfn)) |
		CFG_FUNC(PCI_FUNC(devfn));
	if (bus->parent->number != pp->root_bus_nr)
		reg |= CFG_TYPE1;
	ks_pcie_app_writel(ks_pcie, CFG_SETUP, reg);

	return dw_pcie_read(pp->va_cfg0_base + where, size, val);
}

static int ks_pcie_wr_other_conf(struct pcie_port *pp, struct pci_bus *bus,
				 unsigned int devfn, int where, int size,
				 u32 val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);
	u32 reg;

	reg = CFG_BUS(bus->number) | CFG_DEVICE(PCI_SLOT(devfn)) |
		CFG_FUNC(PCI_FUNC(devfn));
	if (bus->parent->number != pp->root_bus_nr)
		reg |= CFG_TYPE1;
	ks_pcie_app_writel(ks_pcie, CFG_SETUP, reg);

	return dw_pcie_write(pp->va_cfg0_base + where, size, val);
}

/**
 * ks_pcie_v3_65_scan_bus() - keystone scan_bus post initialization
 *
 * This sets BAR0 to enable inbound access for MSI_IRQ register
 */
static void ks_pcie_v3_65_scan_bus(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);

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
}

/**
 * ks_pcie_link_up() - Check if link up
 */
static int ks_pcie_link_up(struct dw_pcie *pci)
{
	u32 val;

	val = dw_pcie_readl_dbi(pci, PCIE_PORT_DEBUG0);
	val &= PORT_LOGIC_LTSSM_STATE_MASK;
	return (val == PORT_LOGIC_LTSSM_STATE_L0);
}

static void ks_pcie_initiate_link_train(struct keystone_pcie *ks_pcie)
{
	u32 val;

	/* Disable Link training */
	val = ks_pcie_app_readl(ks_pcie, CMD_STATUS);
	val &= ~LTSSM_EN_VAL;
	ks_pcie_app_writel(ks_pcie, CMD_STATUS, LTSSM_EN_VAL | val);

	/* Initiate Link Training */
	val = ks_pcie_app_readl(ks_pcie, CMD_STATUS);
	ks_pcie_app_writel(ks_pcie, CMD_STATUS, LTSSM_EN_VAL | val);
}

/**
 * ks_pcie_dw_host_init() - initialize host for v3_65 dw hardware
 *
 * Ioremap the register resources, initialize legacy irq domain
 * and call dw_pcie_v3_65_host_init() API to initialize the Keystone
 * PCI host controller.
 */
static int __init ks_pcie_dw_host_init(struct keystone_pcie *ks_pcie)
{
	struct dw_pcie *pci = ks_pcie->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = pci->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;

	/* Index 0 is the config reg. space address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pci->dbi_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	/*
	 * We set these same and is used in pcie rd/wr_other_conf
	 * functions
	 */
	pp->va_cfg0_base = pci->dbi_base + SPACE0_REMOTE_CFG_OFFSET;
	pp->va_cfg1_base = pp->va_cfg0_base;

	/* Index 1 is the application reg. space address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	ks_pcie->va_app_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ks_pcie->va_app_base))
		return PTR_ERR(ks_pcie->va_app_base);

	ks_pcie->app = *res;

	/* Create legacy IRQ domain */
	ks_pcie->legacy_irq_domain =
			irq_domain_add_linear(ks_pcie->legacy_intc_np,
					      PCI_NUM_INTX,
					      &ks_pcie_legacy_irq_domain_ops,
					      NULL);
	if (!ks_pcie->legacy_irq_domain) {
		dev_err(dev, "Failed to add irq domain for legacy irqs\n");
		return -EINVAL;
	}

	return dw_pcie_host_init(pp);
}

static void ks_pcie_quirk(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->bus;
	struct pci_dev *bridge;
	static const struct pci_device_id rc_pci_devids[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCIE_RC_K2HK),
		 .class = PCI_CLASS_BRIDGE_PCI << 8, .class_mask = ~0, },
		{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCIE_RC_K2E),
		 .class = PCI_CLASS_BRIDGE_PCI << 8, .class_mask = ~0, },
		{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCIE_RC_K2L),
		 .class = PCI_CLASS_BRIDGE_PCI << 8, .class_mask = ~0, },
		{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCIE_RC_K2G),
		 .class = PCI_CLASS_BRIDGE_PCI << 8, .class_mask = ~0, },
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

static int ks_pcie_establish_link(struct keystone_pcie *ks_pcie)
{
	struct dw_pcie *pci = ks_pcie->pci;
	struct device *dev = pci->dev;

	if (dw_pcie_link_up(pci)) {
		dev_info(dev, "Link already up\n");
		return 0;
	}

	ks_pcie_initiate_link_train(ks_pcie);

	/* check if the link is up or not */
	if (!dw_pcie_wait_for_link(pci))
		return 0;

	dev_err(dev, "phy link never came up\n");
	return -ETIMEDOUT;
}

static void ks_pcie_msi_irq_handler(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);
	struct keystone_pcie *ks_pcie = irq_desc_get_handler_data(desc);
	u32 offset = irq - ks_pcie->msi_host_irqs[0];
	struct dw_pcie *pci = ks_pcie->pci;
	struct device *dev = pci->dev;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	dev_dbg(dev, "%s, irq %d\n", __func__, irq);

	/*
	 * The chained irq handler installation would have replaced normal
	 * interrupt driver handler so we need to take care of mask/unmask and
	 * ack operation.
	 */
	chained_irq_enter(chip, desc);
	ks_pcie_handle_msi_irq(ks_pcie, offset);
	chained_irq_exit(chip, desc);
}

/**
 * ks_pcie_legacy_irq_handler() - Handle legacy interrupt
 * @irq: IRQ line for legacy interrupts
 * @desc: Pointer to irq descriptor
 *
 * Traverse through pending legacy interrupts and invoke handler for each. Also
 * takes care of interrupt controller level mask/ack operation.
 */
static void ks_pcie_legacy_irq_handler(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);
	struct keystone_pcie *ks_pcie = irq_desc_get_handler_data(desc);
	struct dw_pcie *pci = ks_pcie->pci;
	struct device *dev = pci->dev;
	u32 irq_offset = irq - ks_pcie->legacy_host_irqs[0];
	struct irq_chip *chip = irq_desc_get_chip(desc);

	dev_dbg(dev, ": Handling legacy irq %d\n", irq);

	/*
	 * The chained irq handler installation would have replaced normal
	 * interrupt driver handler so we need to take care of mask/unmask and
	 * ack operation.
	 */
	chained_irq_enter(chip, desc);
	ks_pcie_handle_legacy_irq(ks_pcie, irq_offset);
	chained_irq_exit(chip, desc);
}

static int ks_pcie_get_irq_controller_info(struct keystone_pcie *ks_pcie,
					   char *controller, int *num_irqs)
{
	int temp, max_host_irqs, legacy = 1, *host_irqs;
	struct device *dev = ks_pcie->pci->dev;
	struct device_node *np_pcie = dev->of_node, **np_temp;

	if (!strcmp(controller, "msi-interrupt-controller"))
		legacy = 0;

	if (legacy) {
		np_temp = &ks_pcie->legacy_intc_np;
		max_host_irqs = PCI_NUM_INTX;
		host_irqs = &ks_pcie->legacy_host_irqs[0];
	} else {
		np_temp = &ks_pcie->msi_intc_np;
		max_host_irqs = MAX_MSI_HOST_IRQS;
		host_irqs =  &ks_pcie->msi_host_irqs[0];
	}

	/* interrupt controller is in a child node */
	*np_temp = of_get_child_by_name(np_pcie, controller);
	if (!(*np_temp)) {
		dev_err(dev, "Node for %s is absent\n", controller);
		return -EINVAL;
	}

	temp = of_irq_count(*np_temp);
	if (!temp) {
		dev_err(dev, "No IRQ entries in %s\n", controller);
		of_node_put(*np_temp);
		return -EINVAL;
	}

	if (temp > max_host_irqs)
		dev_warn(dev, "Too many %s interrupts defined %u\n",
			(legacy ? "legacy" : "MSI"), temp);

	/*
	 * support upto max_host_irqs. In dt from index 0 to 3 (legacy) or 0 to
	 * 7 (MSI)
	 */
	for (temp = 0; temp < max_host_irqs; temp++) {
		host_irqs[temp] = irq_of_parse_and_map(*np_temp, temp);
		if (!host_irqs[temp])
			break;
	}

	of_node_put(*np_temp);

	if (temp) {
		*num_irqs = temp;
		return 0;
	}

	return -EINVAL;
}

static void ks_pcie_setup_interrupts(struct keystone_pcie *ks_pcie)
{
	int i;

	/* Legacy IRQ */
	for (i = 0; i < ks_pcie->num_legacy_host_irqs; i++) {
		irq_set_chained_handler_and_data(ks_pcie->legacy_host_irqs[i],
						 ks_pcie_legacy_irq_handler,
						 ks_pcie);
	}
	ks_pcie_enable_legacy_irqs(ks_pcie);

	/* MSI IRQ */
	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		for (i = 0; i < ks_pcie->num_msi_host_irqs; i++) {
			irq_set_chained_handler_and_data(ks_pcie->msi_host_irqs[i],
							 ks_pcie_msi_irq_handler,
							 ks_pcie);
		}
	}

	if (ks_pcie->error_irq > 0)
		ks_pcie_enable_error_irq(ks_pcie);
}

/*
 * When a PCI device does not exist during config cycles, keystone host gets a
 * bus error instead of returning 0xffffffff. This handler always returns 0
 * for this kind of faults.
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

static int __init ks_pcie_init_id(struct keystone_pcie *ks_pcie)
{
	int ret;
	unsigned int id;
	struct regmap *devctrl_regs;
	struct dw_pcie *pci = ks_pcie->pci;
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;

	devctrl_regs = syscon_regmap_lookup_by_phandle(np, "ti,syscon-pcie-id");
	if (IS_ERR(devctrl_regs))
		return PTR_ERR(devctrl_regs);

	ret = regmap_read(devctrl_regs, 0, &id);
	if (ret)
		return ret;

	dw_pcie_writew_dbi(pci, PCI_VENDOR_ID, id & PCIE_VENDORID_MASK);
	dw_pcie_writew_dbi(pci, PCI_DEVICE_ID, id >> PCIE_DEVICEID_SHIFT);

	return 0;
}

static int __init ks_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct keystone_pcie *ks_pcie = to_keystone_pcie(pci);
	int ret;

	dw_pcie_setup_rc(pp);

	ks_pcie_establish_link(ks_pcie);
	ks_pcie_setup_rc_app_regs(ks_pcie);
	ks_pcie_setup_interrupts(ks_pcie);
	writew(PCI_IO_RANGE_TYPE_32 | (PCI_IO_RANGE_TYPE_32 << 8),
			pci->dbi_base + PCI_IO_BASE);

	ret = ks_pcie_init_id(ks_pcie);
	if (ret < 0)
		return ret;

	/*
	 * PCIe access errors that result into OCP errors are caught by ARM as
	 * "External aborts"
	 */
	hook_fault_code(17, ks_pcie_fault, SIGBUS, 0,
			"Asynchronous external abort");

	return 0;
}

static const struct dw_pcie_host_ops ks_pcie_host_ops = {
	.rd_other_conf = ks_pcie_rd_other_conf,
	.wr_other_conf = ks_pcie_wr_other_conf,
	.host_init = ks_pcie_host_init,
	.msi_set_irq = ks_pcie_msi_set_irq,
	.msi_clear_irq = ks_pcie_msi_clear_irq,
	.get_msi_addr = ks_pcie_get_msi_addr,
	.msi_host_init = ks_pcie_msi_host_init,
	.msi_irq_ack = ks_pcie_msi_irq_ack,
	.scan_bus = ks_pcie_v3_65_scan_bus,
};

static irqreturn_t ks_pcie_err_irq_handler(int irq, void *priv)
{
	struct keystone_pcie *ks_pcie = priv;

	return ks_pcie_handle_error_irq(ks_pcie);
}

static int __init ks_pcie_add_pcie_port(struct keystone_pcie *ks_pcie,
					struct platform_device *pdev)
{
	struct dw_pcie *pci = ks_pcie->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	ret = ks_pcie_get_irq_controller_info(ks_pcie,
					"legacy-interrupt-controller",
					&ks_pcie->num_legacy_host_irqs);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		ret = ks_pcie_get_irq_controller_info(ks_pcie,
						"msi-interrupt-controller",
						&ks_pcie->num_msi_host_irqs);
		if (ret)
			return ret;
	}

	/*
	 * Index 0 is the platform interrupt for error interrupt
	 * from RC.  This is optional.
	 */
	ks_pcie->error_irq = irq_of_parse_and_map(ks_pcie->np, 0);
	if (ks_pcie->error_irq <= 0)
		dev_info(dev, "no error IRQ defined\n");
	else {
		ret = request_irq(ks_pcie->error_irq, ks_pcie_err_irq_handler,
				  IRQF_SHARED, "pcie-error-irq", ks_pcie);
		if (ret < 0) {
			dev_err(dev, "failed to request error IRQ %d\n",
				ks_pcie->error_irq);
			return ret;
		}
	}

	pp->ops = &ks_pcie_host_ops;
	ret = ks_pcie_dw_host_init(ks_pcie);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id ks_pcie_of_match[] = {
	{
		.type = "pci",
		.compatible = "ti,keystone-pcie",
	},
	{ },
};

static const struct dw_pcie_ops ks_pcie_dw_pcie_ops = {
	.link_up = ks_pcie_link_up,
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

static int __init ks_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct dw_pcie *pci;
	struct keystone_pcie *ks_pcie;
	struct device_link **link;
	u32 num_viewport;
	struct phy **phy;
	u32 num_lanes;
	char name[10];
	int ret;
	int i;

	ks_pcie = devm_kzalloc(dev, sizeof(*ks_pcie), GFP_KERNEL);
	if (!ks_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &ks_pcie_dw_pcie_ops;

	ret = of_property_read_u32(np, "num-viewport", &num_viewport);
	if (ret < 0) {
		dev_err(dev, "unable to read *num-viewport* property\n");
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
	ks_pcie->num_viewport = num_viewport;
	ks_pcie->phy = phy;

	ret = ks_pcie_enable_phy(ks_pcie);
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

	ret = ks_pcie_add_pcie_port(ks_pcie, pdev);
	if (ret < 0)
		goto err_get_sync;

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

static int __exit ks_pcie_remove(struct platform_device *pdev)
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

	return 0;
}

static struct platform_driver ks_pcie_driver __refdata = {
	.probe  = ks_pcie_probe,
	.remove = __exit_p(ks_pcie_remove),
	.driver = {
		.name	= "keystone-pcie",
		.of_match_table = of_match_ptr(ks_pcie_of_match),
	},
};
builtin_platform_driver(ks_pcie_driver);
