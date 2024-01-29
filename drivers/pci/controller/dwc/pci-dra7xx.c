// SPDX-License-Identifier: GPL-2.0
/*
 * pcie-dra7xx - PCIe controller driver for TI DRA7xx SoCs
 *
 * Copyright (C) 2013-2014 Texas Instruments Incorporated - https://www.ti.com
 *
 * Authors: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
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
#include <linux/gpio/consumer.h>

#include "../../pci.h"
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

#define	PCIECTRL_TI_CONF_INTX_ASSERT			0x0124
#define	PCIECTRL_TI_CONF_INTX_DEASSERT			0x0128

#define	PCIECTRL_TI_CONF_MSI_XMT			0x012c
#define MSI_REQ_GRANT					BIT(0)
#define MSI_VECTOR_SHIFT				7

#define PCIE_1LANE_2LANE_SELECTION			BIT(13)
#define PCIE_B1C0_MODE_SEL				BIT(2)
#define PCIE_B0_B1_TSYNCEN				BIT(0)

struct dra7xx_pcie {
	struct dw_pcie		*pci;
	void __iomem		*base;		/* DT ti_conf */
	int			phy_count;	/* DT phy-names count */
	struct phy		**phy;
	struct irq_domain	*irq_domain;
	struct clk              *clk;
	enum dw_pcie_device_mode mode;
};

struct dra7xx_pcie_of_data {
	enum dw_pcie_device_mode mode;
	u32 b1co_mode_sel_mask;
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

static u64 dra7xx_pcie_cpu_addr_fixup(struct dw_pcie *pci, u64 pci_addr)
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

	if (dw_pcie_link_up(pci)) {
		dev_err(dev, "link is already up\n");
		return 0;
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

static int dra7xx_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct dra7xx_pcie *dra7xx = to_dra7xx_pcie(pci);

	dra7xx_pcie_enable_interrupts(dra7xx);

	return 0;
}

static int dra7xx_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
				irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = dra7xx_pcie_intx_map,
	.xlate = pci_irqd_intx_xlate,
};

static int dra7xx_pcie_handle_msi(struct dw_pcie_rp *pp, int index)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	unsigned long val;
	int pos;

	val = dw_pcie_readl_dbi(pci, PCIE_MSI_INTR0_STATUS +
				   (index * MSI_REG_CTRL_BLOCK_SIZE));
	if (!val)
		return 0;

	pos = find_first_bit(&val, MAX_MSI_IRQS_PER_CTRL);
	while (pos != MAX_MSI_IRQS_PER_CTRL) {
		generic_handle_domain_irq(pp->irq_domain,
					  (index * MAX_MSI_IRQS_PER_CTRL) + pos);
		pos++;
		pos = find_next_bit(&val, MAX_MSI_IRQS_PER_CTRL, pos);
	}

	return 1;
}

static void dra7xx_pcie_handle_msi_irq(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	int ret, i, count, num_ctrls;

	num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

	/**
	 * Need to make sure all MSI status bits read 0 before exiting.
	 * Else, new MSI IRQs are not registered by the wrapper. Have an
	 * upperbound for the loop and exit the IRQ in case of IRQ flood
	 * to avoid locking up system in interrupt context.
	 */
	count = 0;
	do {
		ret = 0;

		for (i = 0; i < num_ctrls; i++)
			ret |= dra7xx_pcie_handle_msi(pp, i);
		count++;
	} while (ret && count <= 1000);

	if (count > 1000)
		dev_warn_ratelimited(pci->dev,
				     "Too many MSI IRQs to handle\n");
}

static void dra7xx_pcie_msi_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct dra7xx_pcie *dra7xx;
	struct dw_pcie_rp *pp;
	struct dw_pcie *pci;
	unsigned long reg;
	u32 bit;

	chained_irq_enter(chip, desc);

	pp = irq_desc_get_handler_data(desc);
	pci = to_dw_pcie_from_pp(pp);
	dra7xx = to_dra7xx_pcie(pci);

	reg = dra7xx_pcie_readl(dra7xx, PCIECTRL_DRA7XX_CONF_IRQSTATUS_MSI);
	dra7xx_pcie_writel(dra7xx, PCIECTRL_DRA7XX_CONF_IRQSTATUS_MSI, reg);

	switch (reg) {
	case MSI:
		dra7xx_pcie_handle_msi_irq(pp);
		break;
	case INTA:
	case INTB:
	case INTC:
	case INTD:
		for_each_set_bit(bit, &reg, PCI_NUM_INTX)
			generic_handle_domain_irq(dra7xx->irq_domain, bit);
		break;
	}

	chained_irq_exit(chip, desc);
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

static int dra7xx_pcie_init_irq_domain(struct dw_pcie_rp *pp)
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

	irq_set_chained_handler_and_data(pp->irq, dra7xx_pcie_msi_irq_handler,
					 pp);
	dra7xx->irq_domain = irq_domain_add_linear(pcie_intc_node, PCI_NUM_INTX,
						   &intx_domain_ops, pp);
	of_node_put(pcie_intc_node);
	if (!dra7xx->irq_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return -ENODEV;
	}

	return 0;
}

static const struct dw_pcie_host_ops dra7xx_pcie_host_ops = {
	.init = dra7xx_pcie_host_init,
};

static void dra7xx_pcie_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct dra7xx_pcie *dra7xx = to_dra7xx_pcie(pci);
	enum pci_barno bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		dw_pcie_ep_reset_bar(pci, bar);

	dra7xx_pcie_enable_wrapper_interrupts(dra7xx);
}

static void dra7xx_pcie_raise_intx_irq(struct dra7xx_pcie *dra7xx)
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

static int dra7xx_pcie_raise_irq(struct dw_pcie_ep *ep, u8 func_no,
				 unsigned int type, u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct dra7xx_pcie *dra7xx = to_dra7xx_pcie(pci);

	switch (type) {
	case PCI_IRQ_INTX:
		dra7xx_pcie_raise_intx_irq(dra7xx);
		break;
	case PCI_IRQ_MSI:
		dra7xx_pcie_raise_msi_irq(dra7xx, interrupt_num);
		break;
	default:
		dev_err(pci->dev, "UNKNOWN IRQ type\n");
	}

	return 0;
}

static const struct pci_epc_features dra7xx_pcie_epc_features = {
	.linkup_notifier = true,
	.msi_capable = true,
	.msix_capable = false,
};

static const struct pci_epc_features*
dra7xx_pcie_get_features(struct dw_pcie_ep *ep)
{
	return &dra7xx_pcie_epc_features;
}

static const struct dw_pcie_ep_ops pcie_ep_ops = {
	.init = dra7xx_pcie_ep_init,
	.raise_irq = dra7xx_pcie_raise_irq,
	.get_features = dra7xx_pcie_get_features,
};

static int dra7xx_add_pcie_ep(struct dra7xx_pcie *dra7xx,
			      struct platform_device *pdev)
{
	int ret;
	struct dw_pcie_ep *ep;
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci = dra7xx->pci;

	ep = &pci->ep;
	ep->ops = &pcie_ep_ops;

	pci->dbi_base = devm_platform_ioremap_resource_byname(pdev, "ep_dbics");
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	pci->dbi_base2 =
		devm_platform_ioremap_resource_byname(pdev, "ep_dbics2");
	if (IS_ERR(pci->dbi_base2))
		return PTR_ERR(pci->dbi_base2);

	ret = dw_pcie_ep_init(ep);
	if (ret) {
		dev_err(dev, "failed to initialize endpoint\n");
		return ret;
	}

	return 0;
}

static int dra7xx_add_pcie_port(struct dra7xx_pcie *dra7xx,
				struct platform_device *pdev)
{
	int ret;
	struct dw_pcie *pci = dra7xx->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = pci->dev;

	pp->irq = platform_get_irq(pdev, 1);
	if (pp->irq < 0)
		return pp->irq;

	/* MSI IRQ is muxed */
	pp->msi_irq[0] = -ENODEV;

	ret = dra7xx_pcie_init_irq_domain(pp);
	if (ret < 0)
		return ret;

	pci->dbi_base = devm_platform_ioremap_resource_byname(pdev, "rc_dbics");
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	pp->ops = &dra7xx_pcie_host_ops;

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
		ret = phy_set_mode(dra7xx->phy[i], PHY_MODE_PCIE);
		if (ret < 0)
			goto err_phy;

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

static const struct dra7xx_pcie_of_data dra746_pcie_rc_of_data = {
	.b1co_mode_sel_mask = BIT(2),
	.mode = DW_PCIE_RC_TYPE,
};

static const struct dra7xx_pcie_of_data dra726_pcie_rc_of_data = {
	.b1co_mode_sel_mask = GENMASK(3, 2),
	.mode = DW_PCIE_RC_TYPE,
};

static const struct dra7xx_pcie_of_data dra746_pcie_ep_of_data = {
	.b1co_mode_sel_mask = BIT(2),
	.mode = DW_PCIE_EP_TYPE,
};

static const struct dra7xx_pcie_of_data dra726_pcie_ep_of_data = {
	.b1co_mode_sel_mask = GENMASK(3, 2),
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
	{
		.compatible = "ti,dra746-pcie-rc",
		.data = &dra746_pcie_rc_of_data,
	},
	{
		.compatible = "ti,dra726-pcie-rc",
		.data = &dra726_pcie_rc_of_data,
	},
	{
		.compatible = "ti,dra746-pcie-ep",
		.data = &dra746_pcie_ep_of_data,
	},
	{
		.compatible = "ti,dra726-pcie-ep",
		.data = &dra726_pcie_ep_of_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, of_dra7xx_pcie_match);

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

static int dra7xx_pcie_configure_two_lane(struct device *dev,
					  u32 b1co_mode_sel_mask)
{
	struct device_node *np = dev->of_node;
	struct regmap *pcie_syscon;
	unsigned int pcie_reg;
	u32 mask;
	u32 val;

	pcie_syscon = syscon_regmap_lookup_by_phandle(np, "ti,syscon-lane-sel");
	if (IS_ERR(pcie_syscon)) {
		dev_err(dev, "unable to get ti,syscon-lane-sel\n");
		return -EINVAL;
	}

	if (of_property_read_u32_index(np, "ti,syscon-lane-sel", 1,
				       &pcie_reg)) {
		dev_err(dev, "couldn't get lane selection reg offset\n");
		return -EINVAL;
	}

	mask = b1co_mode_sel_mask | PCIE_B0_B1_TSYNCEN;
	val = PCIE_B1C0_MODE_SEL | PCIE_B0_B1_TSYNCEN;
	regmap_update_bits(pcie_syscon, pcie_reg, mask, val);

	return 0;
}

static int dra7xx_pcie_probe(struct platform_device *pdev)
{
	u32 reg;
	int ret;
	int irq;
	int i;
	int phy_count;
	struct phy **phy;
	struct device_link **link;
	void __iomem *base;
	struct dw_pcie *pci;
	struct dra7xx_pcie *dra7xx;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	char name[10];
	struct gpio_desc *reset;
	const struct dra7xx_pcie_of_data *data;
	enum dw_pcie_device_mode mode;
	u32 b1co_mode_sel_mask;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	mode = (enum dw_pcie_device_mode)data->mode;
	b1co_mode_sel_mask = data->b1co_mode_sel_mask;

	dra7xx = devm_kzalloc(dev, sizeof(*dra7xx), GFP_KERNEL);
	if (!dra7xx)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	base = devm_platform_ioremap_resource_byname(pdev, "ti_conf");
	if (IS_ERR(base))
		return PTR_ERR(base);

	phy_count = of_property_count_strings(np, "phy-names");
	if (phy_count < 0) {
		dev_err(dev, "unable to find the strings\n");
		return phy_count;
	}

	phy = devm_kcalloc(dev, phy_count, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	link = devm_kcalloc(dev, phy_count, sizeof(*link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	dra7xx->clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(dra7xx->clk))
		return dev_err_probe(dev, PTR_ERR(dra7xx->clk),
				     "clock request failed");

	ret = clk_prepare_enable(dra7xx->clk);
	if (ret)
		return ret;

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

	if (phy_count == 2) {
		ret = dra7xx_pcie_configure_two_lane(dev, b1co_mode_sel_mask);
		if (ret < 0)
			dra7xx->phy_count = 1; /* Fallback to x1 lane mode */
	}

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

	switch (mode) {
	case DW_PCIE_RC_TYPE:
		if (!IS_ENABLED(CONFIG_PCI_DRA7XX_HOST)) {
			ret = -ENODEV;
			goto err_gpio;
		}

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
		if (!IS_ENABLED(CONFIG_PCI_DRA7XX_EP)) {
			ret = -ENODEV;
			goto err_gpio;
		}

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

	ret = devm_request_threaded_irq(dev, irq, NULL, dra7xx_pcie_irq_handler,
			       IRQF_SHARED, "dra7xx-pcie-main", dra7xx);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		goto err_gpio;
	}

	return 0;

err_gpio:
err_get_sync:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);
	dra7xx_pcie_disable_phy(dra7xx);

err_link:
	while (--i >= 0)
		device_link_del(link[i]);

	return ret;
}

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

static void dra7xx_pcie_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dra7xx_pcie *dra7xx = dev_get_drvdata(dev);
	int ret;

	dra7xx_pcie_stop_link(dra7xx->pci);

	ret = pm_runtime_put_sync(dev);
	if (ret < 0)
		dev_dbg(dev, "pm_runtime_put_sync failed\n");

	pm_runtime_disable(dev);
	dra7xx_pcie_disable_phy(dra7xx);

	clk_disable_unprepare(dra7xx->clk);
}

static const struct dev_pm_ops dra7xx_pcie_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(dra7xx_pcie_suspend, dra7xx_pcie_resume)
	NOIRQ_SYSTEM_SLEEP_PM_OPS(dra7xx_pcie_suspend_noirq,
				  dra7xx_pcie_resume_noirq)
};

static struct platform_driver dra7xx_pcie_driver = {
	.probe = dra7xx_pcie_probe,
	.driver = {
		.name	= "dra7-pcie",
		.of_match_table = of_dra7xx_pcie_match,
		.suppress_bind_attrs = true,
		.pm	= &dra7xx_pcie_pm_ops,
	},
	.shutdown = dra7xx_pcie_shutdown,
};
module_platform_driver(dra7xx_pcie_driver);

MODULE_AUTHOR("Kishon Vijay Abraham I <kishon@ti.com>");
MODULE_DESCRIPTION("PCIe controller driver for TI DRA7xx SoCs");
MODULE_LICENSE("GPL v2");
