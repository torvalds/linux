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

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/resource.h>
#include <linux/types.h>

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

#define	PCIECTRL_DRA7XX_CONF_DEVICE_CMD			0x0104
#define	LTSSM_EN					0x1

#define	PCIECTRL_DRA7XX_CONF_PHY_CS			0x010C
#define	LINK_UP						BIT(16)
#define	DRA7XX_CPU_TO_BUS_ADDR				0x0FFFFFFF

#define EXP_CAP_ID_OFFSET				0x70

struct dra7xx_pcie {
	struct pcie_port	pp;
	void __iomem		*base;		/* DT ti_conf */
	int			phy_count;	/* DT phy-names count */
	struct phy		**phy;
	int			link_gen;
};

#define to_dra7xx_pcie(x)	container_of((x), struct dra7xx_pcie, pp)

static inline u32 dra7xx_pcie_readl(struct dra7xx_pcie *pcie, u32 offset)
{
	return readl(pcie->base + offset);
}

static inline void dra7xx_pcie_writel(struct dra7xx_pcie *pcie, u32 offset,
				      u32 value)
{
	writel(value, pcie->base + offset);
}

static int dra7xx_pcie_link_up(struct pcie_port *pp)
{
	struct dra7xx_pcie *dra7xx = to_dra7xx_pcie(pp);
	u32 reg = dra7xx_pcie_readl(dra7xx, PCIECTRL_DRA7XX_CONF_PHY_CS);

	return !!(reg & LINK_UP);
}

static int dra7xx_pcie_establish_link(struct dra7xx_pcie *dra7xx)
{
	struct pcie_port *pp = &dra7xx->pp;
	struct device *dev = pp->dev;
	u32 reg;
	u32 exp_cap_off = EXP_CAP_ID_OFFSET;

	if (dw_pcie_link_up(pp)) {
		dev_err(dev, "link is already up\n");
		return 0;
	}

	if (dra7xx->link_gen == 1) {
		dw_pcie_cfg_read(pp->dbi_base + exp_cap_off + PCI_EXP_LNKCAP,
				 4, &reg);
		if ((reg & PCI_EXP_LNKCAP_SLS) != PCI_EXP_LNKCAP_SLS_2_5GB) {
			reg &= ~((u32)PCI_EXP_LNKCAP_SLS);
			reg |= PCI_EXP_LNKCAP_SLS_2_5GB;
			dw_pcie_cfg_write(pp->dbi_base + exp_cap_off +
					  PCI_EXP_LNKCAP, 4, reg);
		}

		dw_pcie_cfg_read(pp->dbi_base + exp_cap_off + PCI_EXP_LNKCTL2,
				 2, &reg);
		if ((reg & PCI_EXP_LNKCAP_SLS) != PCI_EXP_LNKCAP_SLS_2_5GB) {
			reg &= ~((u32)PCI_EXP_LNKCAP_SLS);
			reg |= PCI_EXP_LNKCAP_SLS_2_5GB;
			dw_pcie_cfg_write(pp->dbi_base + exp_cap_off +
					  PCI_EXP_LNKCTL2, 2, reg);
		}
	}

	reg = dra7xx_pcie_readl(dra7xx, PCIECTRL_DRA7XX_CONF_DEVICE_CMD);
	reg |= LTSSM_EN;
	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_DEVICE_CMD, reg);

	return dw_pcie_wait_for_link(pp);
}

static void dra7xx_pcie_enable_interrupts(struct dra7xx_pcie *dra7xx)
{
	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_IRQSTATUS_MAIN,
			   ~INTERRUPTS);
	dra7xx_pcie_writel(dra7xx,
			   PCIECTRL_DRA7XX_CONF_IRQENABLE_SET_MAIN, INTERRUPTS);
	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_IRQSTATUS_MSI,
			   ~LEG_EP_INTERRUPTS & ~MSI);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		dra7xx_pcie_writel(dra7xx,
				   PCIECTRL_DRA7XX_CONF_IRQENABLE_SET_MSI, MSI);
	else
		dra7xx_pcie_writel(dra7xx,
				   PCIECTRL_DRA7XX_CONF_IRQENABLE_SET_MSI,
				   LEG_EP_INTERRUPTS);
}

static void dra7xx_pcie_host_init(struct pcie_port *pp)
{
	struct dra7xx_pcie *dra7xx = to_dra7xx_pcie(pp);

	pp->io_base &= DRA7XX_CPU_TO_BUS_ADDR;
	pp->mem_base &= DRA7XX_CPU_TO_BUS_ADDR;
	pp->cfg0_base &= DRA7XX_CPU_TO_BUS_ADDR;
	pp->cfg1_base &= DRA7XX_CPU_TO_BUS_ADDR;

	dw_pcie_setup_rc(pp);

	dra7xx_pcie_establish_link(dra7xx);
	if (IS_ENABLED(CONFIG_PCI_MSI))
		dw_pcie_msi_init(pp);
	dra7xx_pcie_enable_interrupts(dra7xx);
}

static struct pcie_host_ops dra7xx_pcie_host_ops = {
	.link_up = dra7xx_pcie_link_up,
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
	struct device *dev = pp->dev;
	struct device_node *node = dev->of_node;
	struct device_node *pcie_intc_node =  of_get_next_child(node, NULL);

	if (!pcie_intc_node) {
		dev_err(dev, "No PCIe Intc node found\n");
		return -ENODEV;
	}

	pp->irq_domain = irq_domain_add_linear(pcie_intc_node, 4,
					       &intx_domain_ops, pp);
	if (!pp->irq_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return -ENODEV;
	}

	return 0;
}

static irqreturn_t dra7xx_pcie_msi_irq_handler(int irq, void *arg)
{
	struct dra7xx_pcie *dra7xx = arg;
	struct pcie_port *pp = &dra7xx->pp;
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
		generic_handle_irq(irq_find_mapping(pp->irq_domain, ffs(reg)));
		break;
	}

	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_IRQSTATUS_MSI, reg);

	return IRQ_HANDLED;
}


static irqreturn_t dra7xx_pcie_irq_handler(int irq, void *arg)
{
	struct dra7xx_pcie *dra7xx = arg;
	struct device *dev = dra7xx->pp.dev;
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

	if (reg & LINK_UP_EVT)
		dev_dbg(dev, "Link-up state change\n");

	if (reg & CFG_BME_EVT)
		dev_dbg(dev, "CFG 'Bus Master Enable' change\n");

	if (reg & CFG_MSE_EVT)
		dev_dbg(dev, "CFG 'Memory Space Enable' change\n");

	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_IRQSTATUS_MAIN, reg);

	return IRQ_HANDLED;
}

static int __init dra7xx_add_pcie_port(struct dra7xx_pcie *dra7xx,
				       struct platform_device *pdev)
{
	int ret;
	struct pcie_port *pp = &dra7xx->pp;
	struct device *dev = pp->dev;
	struct resource *res;

	pp->irq = platform_get_irq(pdev, 1);
	if (pp->irq < 0) {
		dev_err(dev, "missing IRQ resource\n");
		return -EINVAL;
	}

	ret = devm_request_irq(dev, pp->irq, dra7xx_pcie_msi_irq_handler,
			       IRQF_SHARED | IRQF_NO_THREAD,
			       "dra7-pcie-msi",	dra7xx);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}

	if (!IS_ENABLED(CONFIG_PCI_MSI)) {
		ret = dra7xx_pcie_init_irq_domain(pp);
		if (ret < 0)
			return ret;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rc_dbics");
	pp->dbi_base = devm_ioremap(dev, res->start, resource_size(res));
	if (!pp->dbi_base)
		return -ENOMEM;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int __init dra7xx_pcie_probe(struct platform_device *pdev)
{
	u32 reg;
	int ret;
	int irq;
	int i;
	int phy_count;
	struct phy **phy;
	void __iomem *base;
	struct resource *res;
	struct dra7xx_pcie *dra7xx;
	struct pcie_port *pp;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	char name[10];
	struct gpio_desc *reset;

	dra7xx = devm_kzalloc(dev, sizeof(*dra7xx), GFP_KERNEL);
	if (!dra7xx)
		return -ENOMEM;

	pp = &dra7xx->pp;
	pp->dev = dev;
	pp->ops = &dra7xx_pcie_host_ops;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "missing IRQ resource\n");
		return -EINVAL;
	}

	ret = devm_request_irq(dev, irq, dra7xx_pcie_irq_handler,
			       IRQF_SHARED, "dra7xx-pcie-main", dra7xx);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
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

	for (i = 0; i < phy_count; i++) {
		snprintf(name, sizeof(name), "pcie-phy%d", i);
		phy[i] = devm_phy_get(dev, name);
		if (IS_ERR(phy[i]))
			return PTR_ERR(phy[i]);

		ret = phy_init(phy[i]);
		if (ret < 0)
			goto err_phy;

		ret = phy_power_on(phy[i]);
		if (ret < 0) {
			phy_exit(phy[i]);
			goto err_phy;
		}
	}

	dra7xx->base = base;
	dra7xx->phy = phy;
	dra7xx->phy_count = phy_count;

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

	ret = dra7xx_add_pcie_port(dra7xx, pdev);
	if (ret < 0)
		goto err_gpio;

	platform_set_drvdata(pdev, dra7xx);
	return 0;

err_gpio:
	pm_runtime_put(dev);

err_get_sync:
	pm_runtime_disable(dev);

err_phy:
	while (--i >= 0) {
		phy_power_off(phy[i]);
		phy_exit(phy[i]);
	}

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int dra7xx_pcie_suspend(struct device *dev)
{
	struct dra7xx_pcie *dra7xx = dev_get_drvdata(dev);
	struct pcie_port *pp = &dra7xx->pp;
	u32 val;

	/* clear MSE */
	val = dw_pcie_readl_rc(pp, PCI_COMMAND);
	val &= ~PCI_COMMAND_MEMORY;
	dw_pcie_writel_rc(pp, PCI_COMMAND, val);

	return 0;
}

static int dra7xx_pcie_resume(struct device *dev)
{
	struct dra7xx_pcie *dra7xx = dev_get_drvdata(dev);
	struct pcie_port *pp = &dra7xx->pp;
	u32 val;

	/* set MSE */
	val = dw_pcie_readl_rc(pp, PCI_COMMAND);
	val |= PCI_COMMAND_MEMORY;
	dw_pcie_writel_rc(pp, PCI_COMMAND, val);

	return 0;
}

static int dra7xx_pcie_suspend_noirq(struct device *dev)
{
	struct dra7xx_pcie *dra7xx = dev_get_drvdata(dev);
	int count = dra7xx->phy_count;

	while (count--) {
		phy_power_off(dra7xx->phy[count]);
		phy_exit(dra7xx->phy[count]);
	}

	return 0;
}

static int dra7xx_pcie_resume_noirq(struct device *dev)
{
	struct dra7xx_pcie *dra7xx = dev_get_drvdata(dev);
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
#endif

static const struct dev_pm_ops dra7xx_pcie_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dra7xx_pcie_suspend, dra7xx_pcie_resume)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(dra7xx_pcie_suspend_noirq,
				      dra7xx_pcie_resume_noirq)
};

static const struct of_device_id of_dra7xx_pcie_match[] = {
	{ .compatible = "ti,dra7-pcie", },
	{},
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
