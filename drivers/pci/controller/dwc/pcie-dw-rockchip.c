// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Rockchip SoCs.
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 *		http://www.rock-chips.com
 *
 * Author: Simon Xue <xxm@rock-chips.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/hw_bitfield.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "../../pci.h"
#include "pcie-designware.h"

/*
 * The upper 16 bits of PCIE_CLIENT_CONFIG are a write
 * mask for the lower 16 bits.
 */

#define to_rockchip_pcie(x) dev_get_drvdata((x)->dev)

/* General Control Register */
#define PCIE_CLIENT_GENERAL_CON		0x0
#define  PCIE_CLIENT_MODE_MASK		GENMASK(7, 4)
#define  PCIE_CLIENT_MODE_EP		0x0UL
#define  PCIE_CLIENT_MODE_RC		0x4UL
#define  PCIE_CLIENT_SET_MODE(x)	FIELD_PREP_WM16(PCIE_CLIENT_MODE_MASK, (x))
#define  PCIE_CLIENT_LD_RQ_RST_GRT	FIELD_PREP_WM16(BIT(3), 1)
#define  PCIE_CLIENT_ENABLE_LTSSM	FIELD_PREP_WM16(BIT(2), 1)
#define  PCIE_CLIENT_DISABLE_LTSSM	FIELD_PREP_WM16(BIT(2), 0)

/* Interrupt Status Register Related to Legacy Interrupt */
#define PCIE_CLIENT_INTR_STATUS_LEGACY	0x8

/* Interrupt Status Register Related to Miscellaneous Operation */
#define PCIE_CLIENT_INTR_STATUS_MISC	0x10
#define  PCIE_RDLH_LINK_UP_CHGED	BIT(1)
#define  PCIE_LINK_REQ_RST_NOT_INT	BIT(2)

/* Interrupt Mask Register Related to Legacy Interrupt */
#define PCIE_CLIENT_INTR_MASK_LEGACY	0x1c
#define  PCIE_INTR_MASK			GENMASK(7, 0)
#define  PCIE_INTR_CLAMP(_x)		((BIT((_x)) & PCIE_INTR_MASK))
#define  PCIE_INTR_LEGACY_MASK(x)	(PCIE_INTR_CLAMP((x)) | \
					 (PCIE_INTR_CLAMP((x)) << 16))
#define  PCIE_INTR_LEGACY_UNMASK(x)	(PCIE_INTR_CLAMP((x)) << 16)

/* Interrupt Mask Register Related to Miscellaneous Operation */
#define PCIE_CLIENT_INTR_MASK_MISC	0x24

/* Hot Reset Control Register */
#define PCIE_CLIENT_HOT_RESET_CTRL	0x180
#define  PCIE_LTSSM_APP_DLY2_EN		BIT(1)
#define  PCIE_LTSSM_APP_DLY2_DONE	BIT(3)
#define  PCIE_LTSSM_ENABLE_ENHANCE	BIT(4)

/* LTSSM Status Register */
#define PCIE_CLIENT_LTSSM_STATUS	0x300
#define  PCIE_LINKUP			0x3
#define  PCIE_LINKUP_MASK		GENMASK(17, 16)
#define  PCIE_LTSSM_STATUS_MASK		GENMASK(5, 0)

struct rockchip_pcie {
	struct dw_pcie pci;
	void __iomem *apb_base;
	struct phy *phy;
	struct clk_bulk_data *clks;
	unsigned int clk_cnt;
	struct reset_control *rst;
	struct gpio_desc *rst_gpio;
	struct regulator *vpcie3v3;
	struct irq_domain *irq_domain;
	const struct rockchip_pcie_of_data *data;
};

struct rockchip_pcie_of_data {
	enum dw_pcie_device_mode mode;
	const struct pci_epc_features *epc_features;
};

static int rockchip_pcie_readl_apb(struct rockchip_pcie *rockchip, u32 reg)
{
	return readl_relaxed(rockchip->apb_base + reg);
}

static void rockchip_pcie_writel_apb(struct rockchip_pcie *rockchip, u32 val,
				     u32 reg)
{
	writel_relaxed(val, rockchip->apb_base + reg);
}

static void rockchip_pcie_intx_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct rockchip_pcie *rockchip = irq_desc_get_handler_data(desc);
	unsigned long reg, hwirq;

	chained_irq_enter(chip, desc);

	reg = rockchip_pcie_readl_apb(rockchip, PCIE_CLIENT_INTR_STATUS_LEGACY);

	for_each_set_bit(hwirq, &reg, 4)
		generic_handle_domain_irq(rockchip->irq_domain, hwirq);

	chained_irq_exit(chip, desc);
}

static void rockchip_intx_mask(struct irq_data *data)
{
	rockchip_pcie_writel_apb(irq_data_get_irq_chip_data(data),
				 PCIE_INTR_LEGACY_MASK(data->hwirq),
				 PCIE_CLIENT_INTR_MASK_LEGACY);
};

static void rockchip_intx_unmask(struct irq_data *data)
{
	rockchip_pcie_writel_apb(irq_data_get_irq_chip_data(data),
				 PCIE_INTR_LEGACY_UNMASK(data->hwirq),
				 PCIE_CLIENT_INTR_MASK_LEGACY);
};

static struct irq_chip rockchip_intx_irq_chip = {
	.name			= "INTx",
	.irq_mask		= rockchip_intx_mask,
	.irq_unmask		= rockchip_intx_unmask,
	.flags			= IRQCHIP_SKIP_SET_WAKE | IRQCHIP_MASK_ON_SUSPEND,
};

static int rockchip_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
				  irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &rockchip_intx_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = rockchip_pcie_intx_map,
};

static int rockchip_pcie_init_irq_domain(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->pci.dev;
	struct device_node *intc;

	intc = of_get_child_by_name(dev->of_node, "legacy-interrupt-controller");
	if (!intc) {
		dev_err(dev, "missing child interrupt-controller node\n");
		return -EINVAL;
	}

	rockchip->irq_domain = irq_domain_create_linear(of_fwnode_handle(intc), PCI_NUM_INTX,
							&intx_domain_ops, rockchip);
	of_node_put(intc);
	if (!rockchip->irq_domain) {
		dev_err(dev, "failed to get a INTx IRQ domain\n");
		return -EINVAL;
	}

	return 0;
}

static u32 rockchip_pcie_get_ltssm(struct rockchip_pcie *rockchip)
{
	return rockchip_pcie_readl_apb(rockchip, PCIE_CLIENT_LTSSM_STATUS);
}

static void rockchip_pcie_enable_ltssm(struct rockchip_pcie *rockchip)
{
	rockchip_pcie_writel_apb(rockchip, PCIE_CLIENT_ENABLE_LTSSM,
				 PCIE_CLIENT_GENERAL_CON);
}

static void rockchip_pcie_disable_ltssm(struct rockchip_pcie *rockchip)
{
	rockchip_pcie_writel_apb(rockchip, PCIE_CLIENT_DISABLE_LTSSM,
				 PCIE_CLIENT_GENERAL_CON);
}

static bool rockchip_pcie_link_up(struct dw_pcie *pci)
{
	struct rockchip_pcie *rockchip = to_rockchip_pcie(pci);
	u32 val = rockchip_pcie_get_ltssm(rockchip);

	return FIELD_GET(PCIE_LINKUP_MASK, val) == PCIE_LINKUP;
}

static void rockchip_pcie_enable_l0s(struct dw_pcie *pci)
{
	u32 cap, lnkcap;

	/* Enable L0S capability for all SoCs */
	cap = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	if (cap) {
		lnkcap = dw_pcie_readl_dbi(pci, cap + PCI_EXP_LNKCAP);
		lnkcap |= PCI_EXP_LNKCAP_ASPM_L0S;
		dw_pcie_dbi_ro_wr_en(pci);
		dw_pcie_writel_dbi(pci, cap + PCI_EXP_LNKCAP, lnkcap);
		dw_pcie_dbi_ro_wr_dis(pci);
	}
}

static int rockchip_pcie_start_link(struct dw_pcie *pci)
{
	struct rockchip_pcie *rockchip = to_rockchip_pcie(pci);

	/* Reset device */
	gpiod_set_value_cansleep(rockchip->rst_gpio, 0);

	rockchip_pcie_enable_ltssm(rockchip);

	/*
	 * PCIe requires the refclk to be stable for 100µs prior to releasing
	 * PERST. See table 2-4 in section 2.6.2 AC Specifications of the PCI
	 * Express Card Electromechanical Specification, 1.1. However, we don't
	 * know if the refclk is coming from RC's PHY or external OSC. If it's
	 * from RC, so enabling LTSSM is the just right place to release #PERST.
	 * We need more extra time as before, rather than setting just
	 * 100us as we don't know how long should the device need to reset.
	 */
	msleep(PCIE_T_PVPERL_MS);
	gpiod_set_value_cansleep(rockchip->rst_gpio, 1);

	return 0;
}

static void rockchip_pcie_stop_link(struct dw_pcie *pci)
{
	struct rockchip_pcie *rockchip = to_rockchip_pcie(pci);

	rockchip_pcie_disable_ltssm(rockchip);
}

static int rockchip_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct rockchip_pcie *rockchip = to_rockchip_pcie(pci);
	struct device *dev = rockchip->pci.dev;
	int irq, ret;

	irq = of_irq_get_byname(dev->of_node, "legacy");
	if (irq < 0)
		return irq;

	ret = rockchip_pcie_init_irq_domain(rockchip);
	if (ret < 0)
		dev_err(dev, "failed to init irq domain\n");

	irq_set_chained_handler_and_data(irq, rockchip_pcie_intx_handler,
					 rockchip);

	rockchip_pcie_enable_l0s(pci);

	return 0;
}

static const struct dw_pcie_host_ops rockchip_pcie_host_ops = {
	.init = rockchip_pcie_host_init,
};

/*
 * ATS does not work on RK3588 when running in EP mode.
 *
 * After the host has enabled ATS on the EP side, it will send an IOTLB
 * invalidation request to the EP side. However, the RK3588 will never send
 * a completion back and eventually the host will print an IOTLB_INV_TIMEOUT
 * error, and the EP will not be operational. If we hide the ATS capability,
 * things work as expected.
 */
static void rockchip_pcie_ep_hide_broken_ats_cap_rk3588(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct device *dev = pci->dev;

	/* Only hide the ATS capability for RK3588 running in EP mode. */
	if (!of_device_is_compatible(dev->of_node, "rockchip,rk3588-pcie-ep"))
		return;

	if (dw_pcie_ep_hide_ext_capability(pci, PCI_EXT_CAP_ID_SECPCI,
					   PCI_EXT_CAP_ID_ATS))
		dev_err(dev, "failed to hide ATS capability\n");
}

static void rockchip_pcie_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar;

	rockchip_pcie_enable_l0s(pci);
	rockchip_pcie_ep_hide_broken_ats_cap_rk3588(ep);

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		dw_pcie_ep_reset_bar(pci, bar);
};

static int rockchip_pcie_raise_irq(struct dw_pcie_ep *ep, u8 func_no,
				   unsigned int type, u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	switch (type) {
	case PCI_IRQ_INTX:
		return dw_pcie_ep_raise_intx_irq(ep, func_no);
	case PCI_IRQ_MSI:
		return dw_pcie_ep_raise_msi_irq(ep, func_no, interrupt_num);
	case PCI_IRQ_MSIX:
		return dw_pcie_ep_raise_msix_irq(ep, func_no, interrupt_num);
	default:
		dev_err(pci->dev, "UNKNOWN IRQ type\n");
	}

	return 0;
}

static const struct pci_epc_features rockchip_pcie_epc_features_rk3568 = {
	.linkup_notifier = true,
	.msi_capable = true,
	.msix_capable = true,
	.align = SZ_64K,
	.bar[BAR_0] = { .type = BAR_RESIZABLE, },
	.bar[BAR_1] = { .type = BAR_RESIZABLE, },
	.bar[BAR_2] = { .type = BAR_RESIZABLE, },
	.bar[BAR_3] = { .type = BAR_RESIZABLE, },
	.bar[BAR_4] = { .type = BAR_RESIZABLE, },
	.bar[BAR_5] = { .type = BAR_RESIZABLE, },
};

/*
 * BAR4 on rk3588 exposes the ATU Port Logic Structure to the host regardless of
 * iATU settings for BAR4. This means that BAR4 cannot be used by an EPF driver,
 * so mark it as RESERVED. (rockchip_pcie_ep_init() will disable all BARs by
 * default.) If the host could write to BAR4, the iATU settings (for all other
 * BARs) would be overwritten, resulting in (all other BARs) no longer working.
 */
static const struct pci_epc_features rockchip_pcie_epc_features_rk3588 = {
	.linkup_notifier = true,
	.msi_capable = true,
	.msix_capable = true,
	.align = SZ_64K,
	.bar[BAR_0] = { .type = BAR_RESIZABLE, },
	.bar[BAR_1] = { .type = BAR_RESIZABLE, },
	.bar[BAR_2] = { .type = BAR_RESIZABLE, },
	.bar[BAR_3] = { .type = BAR_RESIZABLE, },
	.bar[BAR_4] = { .type = BAR_RESERVED, },
	.bar[BAR_5] = { .type = BAR_RESIZABLE, },
};

static const struct pci_epc_features *
rockchip_pcie_get_features(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct rockchip_pcie *rockchip = to_rockchip_pcie(pci);

	return rockchip->data->epc_features;
}

static const struct dw_pcie_ep_ops rockchip_pcie_ep_ops = {
	.init = rockchip_pcie_ep_init,
	.raise_irq = rockchip_pcie_raise_irq,
	.get_features = rockchip_pcie_get_features,
};

static int rockchip_pcie_clk_init(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->pci.dev;
	int ret;

	ret = devm_clk_bulk_get_all(dev, &rockchip->clks);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get clocks\n");

	rockchip->clk_cnt = ret;

	ret = clk_bulk_prepare_enable(rockchip->clk_cnt, rockchip->clks);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable clocks\n");

	return 0;
}

static int rockchip_pcie_resource_get(struct platform_device *pdev,
				      struct rockchip_pcie *rockchip)
{
	rockchip->apb_base = devm_platform_ioremap_resource_byname(pdev, "apb");
	if (IS_ERR(rockchip->apb_base))
		return dev_err_probe(&pdev->dev, PTR_ERR(rockchip->apb_base),
				     "failed to map apb registers\n");

	rockchip->rst_gpio = devm_gpiod_get_optional(&pdev->dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(rockchip->rst_gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(rockchip->rst_gpio),
				     "failed to get reset gpio\n");

	rockchip->rst = devm_reset_control_array_get_exclusive(&pdev->dev);
	if (IS_ERR(rockchip->rst))
		return dev_err_probe(&pdev->dev, PTR_ERR(rockchip->rst),
				     "failed to get reset lines\n");

	return 0;
}

static int rockchip_pcie_phy_init(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->pci.dev;
	int ret;

	rockchip->phy = devm_phy_get(dev, "pcie-phy");
	if (IS_ERR(rockchip->phy))
		return dev_err_probe(dev, PTR_ERR(rockchip->phy),
				     "missing PHY\n");

	ret = phy_init(rockchip->phy);
	if (ret < 0)
		return ret;

	ret = phy_power_on(rockchip->phy);
	if (ret)
		phy_exit(rockchip->phy);

	return ret;
}

static void rockchip_pcie_phy_deinit(struct rockchip_pcie *rockchip)
{
	phy_power_off(rockchip->phy);
	phy_exit(rockchip->phy);
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.link_up = rockchip_pcie_link_up,
	.start_link = rockchip_pcie_start_link,
	.stop_link = rockchip_pcie_stop_link,
};

static irqreturn_t rockchip_pcie_rc_sys_irq_thread(int irq, void *arg)
{
	struct rockchip_pcie *rockchip = arg;
	struct dw_pcie *pci = &rockchip->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = pci->dev;
	u32 reg;

	reg = rockchip_pcie_readl_apb(rockchip, PCIE_CLIENT_INTR_STATUS_MISC);
	rockchip_pcie_writel_apb(rockchip, reg, PCIE_CLIENT_INTR_STATUS_MISC);

	dev_dbg(dev, "PCIE_CLIENT_INTR_STATUS_MISC: %#x\n", reg);
	dev_dbg(dev, "LTSSM_STATUS: %#x\n", rockchip_pcie_get_ltssm(rockchip));

	if (reg & PCIE_RDLH_LINK_UP_CHGED) {
		if (rockchip_pcie_link_up(pci)) {
			msleep(PCIE_RESET_CONFIG_WAIT_MS);
			dev_dbg(dev, "Received Link up event. Starting enumeration!\n");
			/* Rescan the bus to enumerate endpoint devices */
			pci_lock_rescan_remove();
			pci_rescan_bus(pp->bridge->bus);
			pci_unlock_rescan_remove();
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t rockchip_pcie_ep_sys_irq_thread(int irq, void *arg)
{
	struct rockchip_pcie *rockchip = arg;
	struct dw_pcie *pci = &rockchip->pci;
	struct device *dev = pci->dev;
	u32 reg, val;

	reg = rockchip_pcie_readl_apb(rockchip, PCIE_CLIENT_INTR_STATUS_MISC);
	rockchip_pcie_writel_apb(rockchip, reg, PCIE_CLIENT_INTR_STATUS_MISC);

	dev_dbg(dev, "PCIE_CLIENT_INTR_STATUS_MISC: %#x\n", reg);
	dev_dbg(dev, "LTSSM_STATUS: %#x\n", rockchip_pcie_get_ltssm(rockchip));

	if (reg & PCIE_LINK_REQ_RST_NOT_INT) {
		dev_dbg(dev, "hot reset or link-down reset\n");
		dw_pcie_ep_linkdown(&pci->ep);
		/* Stop delaying link training. */
		val = FIELD_PREP_WM16(PCIE_LTSSM_APP_DLY2_DONE, 1);
		rockchip_pcie_writel_apb(rockchip, val,
					 PCIE_CLIENT_HOT_RESET_CTRL);
	}

	if (reg & PCIE_RDLH_LINK_UP_CHGED) {
		if (rockchip_pcie_link_up(pci)) {
			dev_dbg(dev, "link up\n");
			dw_pcie_ep_linkup(&pci->ep);
		}
	}

	return IRQ_HANDLED;
}

static int rockchip_pcie_configure_rc(struct platform_device *pdev,
				      struct rockchip_pcie *rockchip)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie_rp *pp;
	int irq, ret;
	u32 val;

	if (!IS_ENABLED(CONFIG_PCIE_ROCKCHIP_DW_HOST))
		return -ENODEV;

	irq = platform_get_irq_byname(pdev, "sys");
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq, NULL,
					rockchip_pcie_rc_sys_irq_thread,
					IRQF_ONESHOT, "pcie-sys-rc", rockchip);
	if (ret) {
		dev_err(dev, "failed to request PCIe sys IRQ\n");
		return ret;
	}

	/* LTSSM enable control mode */
	val = FIELD_PREP_WM16(PCIE_LTSSM_ENABLE_ENHANCE, 1);
	rockchip_pcie_writel_apb(rockchip, val, PCIE_CLIENT_HOT_RESET_CTRL);

	rockchip_pcie_writel_apb(rockchip,
				 PCIE_CLIENT_SET_MODE(PCIE_CLIENT_MODE_RC),
				 PCIE_CLIENT_GENERAL_CON);

	pp = &rockchip->pci.pp;
	pp->ops = &rockchip_pcie_host_ops;
	pp->use_linkup_irq = true;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	/* unmask DLL up/down indicator */
	val = FIELD_PREP_WM16(PCIE_RDLH_LINK_UP_CHGED, 0);
	rockchip_pcie_writel_apb(rockchip, val, PCIE_CLIENT_INTR_MASK_MISC);

	return ret;
}

static int rockchip_pcie_configure_ep(struct platform_device *pdev,
				      struct rockchip_pcie *rockchip)
{
	struct device *dev = &pdev->dev;
	int irq, ret;
	u32 val;

	if (!IS_ENABLED(CONFIG_PCIE_ROCKCHIP_DW_EP))
		return -ENODEV;

	irq = platform_get_irq_byname(pdev, "sys");
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq, NULL,
					rockchip_pcie_ep_sys_irq_thread,
					IRQF_ONESHOT, "pcie-sys-ep", rockchip);
	if (ret) {
		dev_err(dev, "failed to request PCIe sys IRQ\n");
		return ret;
	}

	/*
	 * LTSSM enable control mode, and automatically delay link training on
	 * hot reset/link-down reset.
	 */
	val = FIELD_PREP_WM16(PCIE_LTSSM_ENABLE_ENHANCE, 1) |
	      FIELD_PREP_WM16(PCIE_LTSSM_APP_DLY2_EN, 1);
	rockchip_pcie_writel_apb(rockchip, val, PCIE_CLIENT_HOT_RESET_CTRL);

	rockchip_pcie_writel_apb(rockchip,
				 PCIE_CLIENT_SET_MODE(PCIE_CLIENT_MODE_EP),
				 PCIE_CLIENT_GENERAL_CON);

	rockchip->pci.ep.ops = &rockchip_pcie_ep_ops;
	rockchip->pci.ep.page_size = SZ_64K;

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));

	ret = dw_pcie_ep_init(&rockchip->pci.ep);
	if (ret) {
		dev_err(dev, "failed to initialize endpoint\n");
		return ret;
	}

	ret = dw_pcie_ep_init_registers(&rockchip->pci.ep);
	if (ret) {
		dev_err(dev, "failed to initialize DWC endpoint registers\n");
		dw_pcie_ep_deinit(&rockchip->pci.ep);
		return ret;
	}

	pci_epc_init_notify(rockchip->pci.ep.epc);

	/* unmask DLL up/down indicator and hot reset/link-down reset */
	val = FIELD_PREP_WM16(PCIE_RDLH_LINK_UP_CHGED, 0) |
	      FIELD_PREP_WM16(PCIE_LINK_REQ_RST_NOT_INT, 0);
	rockchip_pcie_writel_apb(rockchip, val, PCIE_CLIENT_INTR_MASK_MISC);

	return ret;
}

static int rockchip_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_pcie *rockchip;
	const struct rockchip_pcie_of_data *data;
	int ret;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	rockchip = devm_kzalloc(dev, sizeof(*rockchip), GFP_KERNEL);
	if (!rockchip)
		return -ENOMEM;

	platform_set_drvdata(pdev, rockchip);

	rockchip->pci.dev = dev;
	rockchip->pci.ops = &dw_pcie_ops;
	rockchip->data = data;

	/* Default N_FTS value (210) is broken, override it to 255 */
	rockchip->pci.n_fts[0] = 255; /* Gen1 */
	rockchip->pci.n_fts[1] = 255; /* Gen2+ */

	ret = rockchip_pcie_resource_get(pdev, rockchip);
	if (ret)
		return ret;

	ret = reset_control_assert(rockchip->rst);
	if (ret)
		return ret;

	/* DON'T MOVE ME: must be enable before PHY init */
	rockchip->vpcie3v3 = devm_regulator_get_optional(dev, "vpcie3v3");
	if (IS_ERR(rockchip->vpcie3v3)) {
		if (PTR_ERR(rockchip->vpcie3v3) != -ENODEV)
			return dev_err_probe(dev, PTR_ERR(rockchip->vpcie3v3),
					"failed to get vpcie3v3 regulator\n");
		rockchip->vpcie3v3 = NULL;
	} else {
		ret = regulator_enable(rockchip->vpcie3v3);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to enable vpcie3v3 regulator\n");
	}

	ret = rockchip_pcie_phy_init(rockchip);
	if (ret)
		goto disable_regulator;

	ret = reset_control_deassert(rockchip->rst);
	if (ret)
		goto deinit_phy;

	ret = rockchip_pcie_clk_init(rockchip);
	if (ret)
		goto deinit_phy;

	switch (data->mode) {
	case DW_PCIE_RC_TYPE:
		ret = rockchip_pcie_configure_rc(pdev, rockchip);
		if (ret)
			goto deinit_clk;
		break;
	case DW_PCIE_EP_TYPE:
		ret = rockchip_pcie_configure_ep(pdev, rockchip);
		if (ret)
			goto deinit_clk;
		break;
	default:
		dev_err(dev, "INVALID device type %d\n", data->mode);
		ret = -EINVAL;
		goto deinit_clk;
	}

	return 0;

deinit_clk:
	clk_bulk_disable_unprepare(rockchip->clk_cnt, rockchip->clks);
deinit_phy:
	rockchip_pcie_phy_deinit(rockchip);
disable_regulator:
	if (rockchip->vpcie3v3)
		regulator_disable(rockchip->vpcie3v3);

	return ret;
}

static const struct rockchip_pcie_of_data rockchip_pcie_rc_of_data_rk3568 = {
	.mode = DW_PCIE_RC_TYPE,
};

static const struct rockchip_pcie_of_data rockchip_pcie_ep_of_data_rk3568 = {
	.mode = DW_PCIE_EP_TYPE,
	.epc_features = &rockchip_pcie_epc_features_rk3568,
};

static const struct rockchip_pcie_of_data rockchip_pcie_ep_of_data_rk3588 = {
	.mode = DW_PCIE_EP_TYPE,
	.epc_features = &rockchip_pcie_epc_features_rk3588,
};

static const struct of_device_id rockchip_pcie_of_match[] = {
	{
		.compatible = "rockchip,rk3568-pcie",
		.data = &rockchip_pcie_rc_of_data_rk3568,
	},
	{
		.compatible = "rockchip,rk3568-pcie-ep",
		.data = &rockchip_pcie_ep_of_data_rk3568,
	},
	{
		.compatible = "rockchip,rk3588-pcie-ep",
		.data = &rockchip_pcie_ep_of_data_rk3588,
	},
	{},
};

static struct platform_driver rockchip_pcie_driver = {
	.driver = {
		.name	= "rockchip-dw-pcie",
		.of_match_table = rockchip_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = rockchip_pcie_probe,
};
builtin_platform_driver(rockchip_pcie_driver);
