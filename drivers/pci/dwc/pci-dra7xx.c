/*
 * pcie-dra7xx - PCIe controller driver for TI DRA7xx SoCs
 *
 * Copyright (C) 2013-2014 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/resource.h>
#include <linux/types.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pcie-designware.h"

/* PCIe controller wrapper DRA7XX configuration registers */

#define	PCIECTRL_DRA7XX_CONF_IRQSTATUS_MAIN		0x0024
#define	PCIECTRL_DRA7XX_CONF_IRQENABLE_SET_MAIN		0x0028
#define	ERR_SYS						BIT(0)
#define	ERR_FATAL					BIT(1)
#define	ERR_NONFATAL					BIT(2)
#define	ERR_COR						BIT(3)
#define	ERR_AXI						BIT(4)
#define	ERR_ECRC					BIT(5)
#define	PME_TURN_OFF					BIT(8)
#define	PME_TO_ACK					BIT(9)
#define	PM_PME						BIT(10)
#define	LINK_REQ_RST					BIT(11)
#define	LINK_UP_EVT					BIT(12)
#define	CFG_BME_EVT					BIT(13)
#define	CFG_MSE_EVT					BIT(14)
#define	INTERRUPTS (ERR_SYS | ERR_FATAL | ERR_NONFATAL | ERR_COR | ERR_AXI | \
			ERR_ECRC | PME_TURN_OFF | PME_TO_ACK | PM_PME | \
			LINK_REQ_RST | LINK_UP_EVT | CFG_BME_EVT | CFG_MSE_EVT)

#define	PCIECTRL_DRA7XX_CONF_IRQSTATUS_MSI		0x0034
#define	PCIECTRL_DRA7XX_CONF_IRQENABLE_SET_MSI		0x0038
#define	INTA						BIT(0)
#define	INTB						BIT(1)
#define	INTC						BIT(2)
#define	INTD						BIT(3)
#define	MSI						BIT(4)
#define	LEG_EP_INTERRUPTS (INTA | INTB | INTC | INTD)

#define	PCIECTRL_TI_CONF_DEVICE_TYPE			0x0100
#define	DEVICE_TYPE_EP					0x0
#define	DEVICE_TYPE_LEG_EP				0x1
#define	DEVICE_TYPE_RC					0x4

#define	PCIECTRL_DRA7XX_CONF_DEVICE_CMD			0x0104
#define	LTSSM_EN					0x1

#define	PCIECTRL_DRA7XX_CONF_PHY_CS			0x010C
#define	LINK_UP						BIT(16)
#define	DRA7XX_CPU_TO_BUS_ADDR				0x0FFFFFFF

#define EXP_CAP_ID_OFFSET				0x70

#define	PCIECTRL_TI_CONF_INTX_ASSERT			0x0124
#define	PCIECTRL_TI_CONF_INTX_DEASSERT			0x0128

#define	PCIECTRL_TI_CONF_MSI_XMT			0x012c
#define MSI_REQ_GRANT					BIT(0)
#define MSI_VECTOR_SHIFT				7

struct dra7xx_pcie {
	struct dw_pcie		*pci;
	void __iomem		*base;		/* DT ti_conf */
	int			phy_count;	/* DT phy-names count */
	struct phy		**phy;
	int			link_gen;
	struct irq_domain	*irq_domain;
	enum dw_pcie_device_mode mode;
};

struct dra7xx_pcie_of_data {
	enum dw_pcie_device_mode mode;
};

#define to_dra7xx_pcie(x)	dev_get_drvdata((x)->dev)

static inline u32 dra7xx_pcie_readl(struct dra7xx_pcie *pcie, u32 offset)
{
	return readl(pcie->base + offset);
}

static inline void dra7xx_pcie_writel(struct dra7xx_pcie *pcie, u32 offset,
				      u32 value)
{
	writel(value, pcie->base + offset);
}

static u64 dra7xx_pcie_cpu_addr_fixup(u64 pci_addr)
{
	return pci_addr & DRA7XX_CPU_TO_BUS_ADDR;
}

static int dra7xx_pcie_link_up(struct dw_pcie *pci)
{
	struct dra7xx_pcie *dra7xx = to_dra7xx_pcie(pci);
	u32 reg = dra7xx_pcie_readl(dra7xx, PCIECTRL_DRA7XX_CONF_PHY_CS);

	return !!(reg & LINK_UP);
}

static void dra7xx_pcie_stop_link(struct dw_pcie *pci)
{
	struct dra7xx_pcie *dra7xx = to_dra7xx_pcie(pci);
	u32 reg;

	reg = dra7xx_pcie_readl(dra7xx, PCIECTRL_DRA7XX_CONF_DEVICE_CMD);
	reg &= ~LTSSM_EN;
	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_DEVICE_CMD, reg);
}

static int dra7xx_pcie_establish_link(struct dw_pcie *pci)
{
	struct dra7xx_pcie *dra7xx = to_dra7xx_pcie(pci);
	struct device *dev = pci->dev;
	u32 reg;
	u32 exp_cap_off = EXP_CAP_ID_OFFSET;

	if (dw_pcie_link_up(pci)) {
		dev_err(dev, "link is already up\n");
		return 0;
	}

	if (dra7xx->link_gen == 1) {
		dw_pcie_read(pci->dbi_base + exp_cap_off + PCI_EXP_LNKCAP,
			     4, &reg);
		if ((reg & PCI_EXP_LNKCAP_SLS) != PCI_EXP_LNKCAP_SLS_2_5GB) {
			reg &= ~((u32)PCI_EXP_LNKCAP_SLS);
			reg |= PCI_EXP_LNKCAP_SLS_2_5GB;
			dw_pcie_write(pci->dbi_base + exp_cap_off +
				      PCI_EXP_LNKCAP, 4, reg);
		}

		dw_pcie_read(pci->dbi_base + exp_cap_off + PCI_EXP_LNKCTL2,
			     2, &reg);
		if ((reg & PCI_EXP_LNKCAP_SLS) != PCI_EXP_LNKCAP_SLS_2_5GB) {
			reg &= ~((u32)PCI_EXP_LNKCAP_SLS);
			reg |= PCI_EXP_LNKCAP_SLS_2_5GB;
			dw_pcie_write(pci->dbi_base + exp_cap_off +
				      PCI_EXP_LNKCTL2, 2, reg);
		}
	}

	reg = dra7xx_pcie_readl(dra7xx, PCIECTRL_DRA7XX_CONF_DEVICE_CMD);
	reg |= LTSSM_EN;
	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_DEVICE_CMD, reg);

	return 0;
}

static void dra7xx_pcie_enable_msi_interrupts(struct dra7xx_pcie *dra7xx)
{
	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_IRQSTATUS_MSI,
			   LEG_EP_INTERRUPTS | MSI);

	dra7xx_pcie_writel(dra7xx,
			   PCIECTRL_DRA7XX_CONF_IRQENABLE_SET_MSI,
			   MSI | LEG_EP_INTERRUPTS);
}

static void dra7xx_pcie_enable_wrapper_interrupts(struct dra7xx_pcie *dra7xx)
{
	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_IRQSTATUS_MAIN,
			   INTERRUPTS);
	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_IRQENABLE_SET_MAIN,
			   INTERRUPTS);
}

static void dra7xx_pcie_enable_interrupts(struct dra7xx_pcie *dra7xx)
{
	dra7xx_pcie_enable_wrapper_interrupts(dra7xx);
	dra7xx_pcie_enable_msi_interrupts(dra7xx);
}

static int dra7xx_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct dra7xx_pcie *dra7xx = to_dra7xx_pcie(pci);

	dw_pcie_setup_rc(pp);

	dra7xx_pcie_establish_link(pci);
	dw_pcie_wait_for_link(pci);
	dw_pcie_msi_init(pp);
	dra7xx_pcie_enable_interrupts(dra7xx);

	return 0;
}

static const struct dw_pcie_host_ops dra7xx_pcie_host_ops = {
	.host_init = dra7xx_pcie_host_init,
};

static int dra7xx_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
				irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = dra7xx_pcie_intx_map,
};

static int dra7xx_pcie_init_irq_domain(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct dra7xx_pcie *dra7xx = to_dra7xx_pcie(pci);
	struct device_node *node = dev->of_node;
	struct device_node *pcie_intc_node =  of_get_next_child(node, NULL);

	if (!pcie_intc_node) {
		dev_err(dev, "No PCIe Intc node found\n");
		return -ENODEV;
	}

	dra7xx->irq_domain = irq_domain_add_linear(pcie_intc_node, PCI_NUM_INTX,
						   &intx_domain_ops, pp);
	if (!dra7xx->irq_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return -ENODEV;
	}

	return 0;
}

static irqreturn_t dra7xx_pcie_msi_irq_handler(int irq, void *arg)
{
	struct dra7xx_pcie *dra7xx = arg;
	struct dw_pcie *pci = dra7xx->pci;
	struct pcie_port *pp = &pci->pp;
	u32 reg;

	reg = dra7xx_pcie_readl(dra7xx, PCIECTRL_DRA7XX_CONF_IRQSTATUS_MSI);

	switch (reg) {
	case MSI:
		dw_handle_msi_irq(pp);
		break;
	case INTA:
	case INTB:
	case INTC:
	case INTD:
		generic_handle_irq(irq_find_mapping(dra7xx->irq_domain,
						    ffs(reg)));
		break;
	}

	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_IRQSTATUS_MSI, reg);

	return IRQ_HANDLED;
}

static irqreturn_t dra7xx_pcie_irq_handler(int irq, void *arg)
{
	struct dra7xx_pcie *dra7xx = arg;
	struct dw_pcie *pci = dra7xx->pci;
	struct device *dev = pci->dev;
	struct dw_pcie_ep *ep = &pci->ep;
	u32 reg;

	reg = dra7xx_pcie_readl(dra7xx, PCIECTRL_DRA7XX_CONF_IRQSTATUS_MAIN);

	if (reg & ERR_SYS)
		dev_dbg(dev, "System Error\n");

	if (reg & ERR_FATAL)
		dev_dbg(dev, "Fatal Error\n");

	if (reg & ERR_NONFATAL)
		dev_dbg(dev, "Non Fatal Error\n");

	if (reg & ERR_COR)
		dev_dbg(dev, "Correctable Error\n");

	if (reg & ERR_AXI)
		dev_dbg(dev, "AXI tag lookup fatal Error\n");

	if (reg & ERR_ECRC)
		dev_dbg(dev, "ECRC Error\n");

	if (reg & PME_TURN_OFF)
		dev_dbg(dev,
			"Power Management Event Turn-Off message received\n");

	if (reg & PME_TO_ACK)
		dev_dbg(dev,
			"Power Management Turn-Off Ack message received\n");

	if (reg & PM_PME)
		dev_dbg(dev, "PM Power Management Event message received\n");

	if (reg & LINK_REQ_RST)
		dev_dbg(dev, "Link Request Reset\n");

	if (reg & LINK_UP_EVT) {
		if (dra7xx->mode == DW_PCIE_EP_TYPE)
			dw_pcie_ep_linkup(ep);
		dev_dbg(dev, "Link-up state change\n");
	}

	if (reg & CFG_BME_EVT)
		dev_dbg(dev, "CFG 'Bus Master Enable' change\n");

	if (reg & CFG_MSE_EVT)
		dev_dbg(dev, "CFG 'Memory Space Enable' change\n");

	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_IRQSTATUS_MAIN, reg);

	return IRQ_HANDLED;
}

static void dw_pcie_ep_reset_bar(struct dw_pcie *pci, enum pci_barno bar)
{
	u32 reg;

	reg = PCI_BASE_ADDRESS_0 + (4 * bar);
	dw_pcie_writel_dbi2(pci, reg, 0x0);
	dw_pcie_writel_dbi(pci, reg, 0x0);
}

static void dra7xx_pcie_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct dra7xx_pcie *dra7xx = to_dra7xx_pcie(pci);
	enum pci_barno bar;

	for (bar = BAR_0; bar <= BAR_5; bar++)
		dw_pcie_ep_reset_bar(pci, bar);

	dra7xx_pcie_enable_wrapper_interrupts(dra7xx);
}

static void dra7xx_pcie_raise_legacy_irq(struct dra7xx_pcie *dra7xx)
{
	dra7xx_pcie_writel(dra7xx, PCIECTRL_TI_CONF_INTX_ASSERT, 0x1);
	mdelay(1);
	dra7xx_pcie_writel(dra7xx, PCIECTRL_TI_CONF_INTX_DEASSERT, 0x1);
}

static void dra7xx_pcie_raise_msi_irq(struct dra7xx_pcie *dra7xx,
				      u8 interrupt_num)
{
	u32 reg;

	reg = (interrupt_num - 1) << MSI_VECTOR_SHIFT;
	reg |= MSI_REQ_GRANT;
	dra7xx_pcie_writel(dra7xx, PCIECTRL_TI_CONF_MSI_XMT, reg);
}

static int dra7xx_pcie_raise_irq(struct dw_pcie_ep *ep,
				 enum pci_epc_irq_type type, u8 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct dra7xx_pcie *dra7xx = to_dra7xx_pcie(pci);

	switch (type) {
	case PCI_EPC_IRQ_LEGACY:
		dra7xx_pcie_raise_legacy_irq(dra7xx);
		break;
	case PCI_EPC_IRQ_MSI:
		dra7xx_pcie_raise_msi_irq(dra7xx, interrupt_num);
		break;
	default:
		dev_err(pci->dev, "UNKNOWN IRQ type\n");
	}

	return 0;
}

static struct dw_pcie_ep_ops pcie_ep_ops = {
	.ep_init = dra7xx_pcie_ep_init,
	.raise_irq = dra7xx_pcie_raise_irq,
};

static int __init dra7xx_add_pcie_ep(struct dra7xx_pcie *dra7xx,
				     struct platform_device *pdev)
{
	int ret;
	struct dw_pcie_ep *ep;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci = dra7xx->pci;

	ep = &pci->ep;
	ep->ops = &pcie_ep_ops;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ep_dbics");
	pci->dbi_base = devm_ioremap(dev, res->start, resource_size(res));
	if (!pci->dbi_base)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ep_dbics2");
	pci->dbi_base2 = devm_ioremap(dev, res->start, resource_size(res));
	if (!pci->dbi_base2)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "addr_space");
	if (!res)
		return -EINVAL;

	ep->phys_base = res->start;
	ep->addr_size = resource_size(res);

	ret = dw_pcie_ep_init(ep);
	if (ret) {
		dev_err(dev, "failed to initialize endpoint\n");
		return ret;
	}

	return 0;
}

static int __init dra7xx_add_pcie_port(struct dra7xx_pcie *dra7xx,
				       struct platform_device *pdev)
{
	int ret;
	struct dw_pcie *pci = dra7xx->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = pci->dev;
	struct resource *res;

	pp->irq = platform_get_irq(pdev, 1);
	if (pp->irq < 0) {
		dev_err(dev, "missing IRQ resource\n");
		return pp->irq;
	}

	ret = devm_request_irq(dev, pp->irq, dra7xx_pcie_msi_irq_handler,
			       IRQF_SHARED | IRQF_NO_THREAD,
			       "dra7-pcie-msi",	dra7xx);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}

	ret = dra7xx_pcie_init_irq_domain(pp);
	if (ret < 0)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rc_dbics");
	pci->dbi_base = devm_ioremap(dev, res->start, resource_size(res));
	if (!pci->dbi_base)
		return -ENOMEM;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.cpu_addr_fixup = dra7xx_pcie_cpu_addr_fixup,
	.start_link = dra7xx_pcie_establish_link,
	.stop_link = dra7xx_pcie_stop_link,
	.link_up = dra7xx_pcie_link_up,
};

static void dra7xx_pcie_disable_phy(struct dra7xx_pcie *dra7xx)
{
	int phy_count = dra7xx->phy_count;

	while (phy_count--) {
		phy_power_off(dra7xx->phy[phy_count]);
		phy_exit(dra7xx->phy[phy_count]);
	}
}

static int dra7xx_pcie_enable_phy(struct dra7xx_pcie *dra7xx)
{
	int phy_count = dra7xx->phy_count;
	int ret;
	int i;

	for (i = 0; i < phy_count; i++) {
		ret = phy_init(dra7xx->phy[i]);
		if (ret < 0)
			goto err_phy;

		ret = phy_power_on(dra7xx->phy[i]);
		if (ret < 0) {
			phy_exit(dra7xx->phy[i]);
			goto err_phy;
		}
	}

	return 0;

err_phy:
	while (--i >= 0) {
		phy_power_off(dra7xx->phy[i]);
		phy_exit(dra7xx->phy[i]);
	}

	return ret;
}

static const struct dra7xx_pcie_of_data dra7xx_pcie_rc_of_data = {
	.mode = DW_PCIE_RC_TYPE,
};

static const struct dra7xx_pcie_of_data dra7xx_pcie_ep_of_data = {
	.mode = DW_PCIE_EP_TYPE,
};

static const struct of_device_id of_dra7xx_pcie_match[] = {
	{
		.compatible = "ti,dra7-pcie",
		.data = &dra7xx_pcie_rc_of_data,
	},
	{
		.compatible = "ti,dra7-pcie-ep",
		.data = &dra7xx_pcie_ep_of_data,
	},
	{},
};

/*
 * dra7xx_pcie_unaligned_memaccess: workaround for AM572x/AM571x Errata i870
 * @dra7xx: the dra7xx device where the workaround should be applied
 *
 * Access to the PCIe slave port that are not 32-bit aligned will result
 * in incorrect mapping to TLP Address and Byte enable fields. Therefore,
 * byte and half-word accesses are not possible to byte offset 0x1, 0x2, or
 * 0x3.
 *
 * To avoid this issue set PCIE_SS1_AXI2OCP_LEGACY_MODE_ENABLE to 1.
 */
static int dra7xx_pcie_unaligned_memaccess(struct device *dev)
{
	int ret;
	struct device_node *np = dev->of_node;
	struct of_phandle_args args;
	struct regmap *regmap;

	regmap = syscon_regmap_lookup_by_phandle(np,
						 "ti,syscon-unaligned-access");
	if (IS_ERR(regmap)) {
		dev_dbg(dev, "can't get ti,syscon-unaligned-access\n");
		return -EINVAL;
	}

	ret = of_parse_phandle_with_fixed_args(np, "ti,syscon-unaligned-access",
					       2, 0, &args);
	if (ret) {
		dev_err(dev, "failed to parse ti,syscon-unaligned-access\n");
		return ret;
	}

	ret = regmap_update_bits(regmap, args.args[0], args.args[1],
				 args.args[1]);
	if (ret)
		dev_err(dev, "failed to enable unaligned access\n");

	of_node_put(args.np);

	return ret;
}

static int __init dra7xx_pcie_probe(struct platform_device *pdev)
{
	u32 reg;
	int ret;
	int irq;
	int i;
	int phy_count;
	struct phy **phy;
	struct device_link **link;
	void __iomem *base;
	struct resource *res;
	struct dw_pcie *pci;
	struct pcie_port *pp;
	struct dra7xx_pcie *dra7xx;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	char name[10];
	struct gpio_desc *reset;
	const struct of_device_id *match;
	const struct dra7xx_pcie_of_data *data;
	enum dw_pcie_device_mode mode;

	match = of_match_device(of_match_ptr(of_dra7xx_pcie_match), dev);
	if (!match)
		return -EINVAL;

	data = (struct dra7xx_pcie_of_data *)match->data;
	mode = (enum dw_pcie_device_mode)data->mode;

	dra7xx = devm_kzalloc(dev, sizeof(*dra7xx), GFP_KERNEL);
	if (!dra7xx)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;

	pp = &pci->pp;
	pp->ops = &dra7xx_pcie_host_ops;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "missing IRQ resource: %d\n", irq);
		return irq;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ti_conf");
	base = devm_ioremap_nocache(dev, res->start, resource_size(res));
	if (!base)
		return -ENOMEM;

	phy_count = of_property_count_strings(np, "phy-names");
	if (phy_count < 0) {
		dev_err(dev, "unable to find the strings\n");
		return phy_count;
	}

	phy = devm_kzalloc(dev, sizeof(*phy) * phy_count, GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	link = devm_kzalloc(dev, sizeof(*link) * phy_count, GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	for (i = 0; i < phy_count; i++) {
		snprintf(name, sizeof(name), "pcie-phy%d", i);
		phy[i] = devm_phy_get(dev, name);
		if (IS_ERR(phy[i]))
			return PTR_ERR(phy[i]);

		link[i] = device_link_add(dev, &phy[i]->dev, DL_FLAG_STATELESS);
		if (!link[i]) {
			ret = -EINVAL;
			goto err_link;
		}
	}

	dra7xx->base = base;
	dra7xx->phy = phy;
	dra7xx->pci = pci;
	dra7xx->phy_count = phy_count;

	ret = dra7xx_pcie_enable_phy(dra7xx);
	if (ret) {
		dev_err(dev, "failed to enable phy\n");
		return ret;
	}

	platform_set_drvdata(pdev, dra7xx);

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_get_sync failed\n");
		goto err_get_sync;
	}

	reset = devm_gpiod_get_optional(dev, NULL, GPIOD_OUT_HIGH);
	if (IS_ERR(reset)) {
		ret = PTR_ERR(reset);
		dev_err(&pdev->dev, "gpio request failed, ret %d\n", ret);
		goto err_gpio;
	}

	reg = dra7xx_pcie_readl(dra7xx, PCIECTRL_DRA7XX_CONF_DEVICE_CMD);
	reg &= ~LTSSM_EN;
	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_DEVICE_CMD, reg);

	dra7xx->link_gen = of_pci_get_max_link_speed(np);
	if (dra7xx->link_gen < 0 || dra7xx->link_gen > 2)
		dra7xx->link_gen = 2;

	switch (mode) {
	case DW_PCIE_RC_TYPE:
		dra7xx_pcie_writel(dra7xx, PCIECTRL_TI_CONF_DEVICE_TYPE,
				   DEVICE_TYPE_RC);

		ret = dra7xx_pcie_unaligned_memaccess(dev);
		if (ret)
			dev_err(dev, "WA for Errata i870 not applied\n");

		ret = dra7xx_add_pcie_port(dra7xx, pdev);
		if (ret < 0)
			goto err_gpio;
		break;
	case DW_PCIE_EP_TYPE:
		dra7xx_pcie_writel(dra7xx, PCIECTRL_TI_CONF_DEVICE_TYPE,
				   DEVICE_TYPE_EP);

		ret = dra7xx_pcie_unaligned_memaccess(dev);
		if (ret)
			goto err_gpio;

		ret = dra7xx_add_pcie_ep(dra7xx, pdev);
		if (ret < 0)
			goto err_gpio;
		break;
	default:
		dev_err(dev, "INVALID device type %d\n", mode);
	}
	dra7xx->mode = mode;

	ret = devm_request_irq(dev, irq, dra7xx_pcie_irq_handler,
			       IRQF_SHARED, "dra7xx-pcie-main", dra7xx);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		goto err_gpio;
	}

	return 0;

err_gpio:
	pm_runtime_put(dev);

err_get_sync:
	pm_runtime_disable(dev);
	dra7xx_pcie_disable_phy(dra7xx);

err_link:
	while (--i >= 0)
		device_link_del(link[i]);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int dra7xx_pcie_suspend(struct device *dev)
{
	struct dra7xx_pcie *dra7xx = dev_get_drvdata(dev);
	struct dw_pcie *pci = dra7xx->pci;
	u32 val;

	if (dra7xx->mode != DW_PCIE_RC_TYPE)
		return 0;

	/* clear MSE */
	val = dw_pcie_readl_dbi(pci, PCI_COMMAND);
	val &= ~PCI_COMMAND_MEMORY;
	dw_pcie_writel_dbi(pci, PCI_COMMAND, val);

	return 0;
}

static int dra7xx_pcie_resume(struct device *dev)
{
	struct dra7xx_pcie *dra7xx = dev_get_drvdata(dev);
	struct dw_pcie *pci = dra7xx->pci;
	u32 val;

	if (dra7xx->mode != DW_PCIE_RC_TYPE)
		return 0;

	/* set MSE */
	val = dw_pcie_readl_dbi(pci, PCI_COMMAND);
	val |= PCI_COMMAND_MEMORY;
	dw_pcie_writel_dbi(pci, PCI_COMMAND, val);

	return 0;
}

static int dra7xx_pcie_suspend_noirq(struct device *dev)
{
	struct dra7xx_pcie *dra7xx = dev_get_drvdata(dev);

	dra7xx_pcie_disable_phy(dra7xx);

	return 0;
}

static int dra7xx_pcie_resume_noirq(struct device *dev)
{
	struct dra7xx_pcie *dra7xx = dev_get_drvdata(dev);
	int ret;

	ret = dra7xx_pcie_enable_phy(dra7xx);
	if (ret) {
		dev_err(dev, "failed to enable phy\n");
		return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops dra7xx_pcie_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dra7xx_pcie_suspend, dra7xx_pcie_resume)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(dra7xx_pcie_suspend_noirq,
				      dra7xx_pcie_resume_noirq)
};

static struct platform_driver dra7xx_pcie_driver = {
	.driver = {
		.name	= "dra7-pcie",
		.of_match_table = of_dra7xx_pcie_match,
		.suppress_bind_attrs = true,
		.pm	= &dra7xx_pcie_pm_ops,
	},
};
builtin_platform_driver_probe(dra7xx_pcie_driver, dra7xx_pcie_probe);
