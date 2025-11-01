// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics STM32MP25 PCIe endpoint driver.
 *
 * Copyright (C) 2025 STMicroelectronics
 * Author: Christian Bruel <christian.bruel@foss.st.com>
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include "pcie-designware.h"
#include "pcie-stm32.h"

struct stm32_pcie {
	struct dw_pcie pci;
	struct regmap *regmap;
	struct reset_control *rst;
	struct phy *phy;
	struct clk *clk;
	struct gpio_desc *perst_gpio;
	unsigned int perst_irq;
};

static void stm32_pcie_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		dw_pcie_ep_reset_bar(pci, bar);
}

static int stm32_pcie_enable_link(struct dw_pcie *pci)
{
	struct stm32_pcie *stm32_pcie = to_stm32_pcie(pci);

	regmap_update_bits(stm32_pcie->regmap, SYSCFG_PCIECR,
			   STM32MP25_PCIECR_LTSSM_EN,
			   STM32MP25_PCIECR_LTSSM_EN);

	return dw_pcie_wait_for_link(pci);
}

static void stm32_pcie_disable_link(struct dw_pcie *pci)
{
	struct stm32_pcie *stm32_pcie = to_stm32_pcie(pci);

	regmap_update_bits(stm32_pcie->regmap, SYSCFG_PCIECR, STM32MP25_PCIECR_LTSSM_EN, 0);
}

static int stm32_pcie_start_link(struct dw_pcie *pci)
{
	struct stm32_pcie *stm32_pcie = to_stm32_pcie(pci);
	int ret;

	dev_dbg(pci->dev, "Enable link\n");

	ret = stm32_pcie_enable_link(pci);
	if (ret) {
		dev_err(pci->dev, "PCIe cannot establish link: %d\n", ret);
		return ret;
	}

	enable_irq(stm32_pcie->perst_irq);

	return 0;
}

static void stm32_pcie_stop_link(struct dw_pcie *pci)
{
	struct stm32_pcie *stm32_pcie = to_stm32_pcie(pci);

	dev_dbg(pci->dev, "Disable link\n");

	disable_irq(stm32_pcie->perst_irq);

	stm32_pcie_disable_link(pci);
}

static int stm32_pcie_raise_irq(struct dw_pcie_ep *ep, u8 func_no,
				unsigned int type, u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	switch (type) {
	case PCI_IRQ_INTX:
		return dw_pcie_ep_raise_intx_irq(ep, func_no);
	case PCI_IRQ_MSI:
		return dw_pcie_ep_raise_msi_irq(ep, func_no, interrupt_num);
	default:
		dev_err(pci->dev, "UNKNOWN IRQ type\n");
		return -EINVAL;
	}
}

static const struct pci_epc_features stm32_pcie_epc_features = {
	.msi_capable = true,
	.align = SZ_64K,
};

static const struct pci_epc_features*
stm32_pcie_get_features(struct dw_pcie_ep *ep)
{
	return &stm32_pcie_epc_features;
}

static const struct dw_pcie_ep_ops stm32_pcie_ep_ops = {
	.init = stm32_pcie_ep_init,
	.raise_irq = stm32_pcie_raise_irq,
	.get_features = stm32_pcie_get_features,
};

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = stm32_pcie_start_link,
	.stop_link = stm32_pcie_stop_link,
};

static int stm32_pcie_enable_resources(struct stm32_pcie *stm32_pcie)
{
	int ret;

	ret = phy_init(stm32_pcie->phy);
	if (ret)
		return ret;

	ret = clk_prepare_enable(stm32_pcie->clk);
	if (ret)
		phy_exit(stm32_pcie->phy);

	return ret;
}

static void stm32_pcie_disable_resources(struct stm32_pcie *stm32_pcie)
{
	clk_disable_unprepare(stm32_pcie->clk);

	phy_exit(stm32_pcie->phy);
}

static void stm32_pcie_perst_assert(struct dw_pcie *pci)
{
	struct stm32_pcie *stm32_pcie = to_stm32_pcie(pci);
	struct dw_pcie_ep *ep = &stm32_pcie->pci.ep;
	struct device *dev = pci->dev;

	dev_dbg(dev, "PERST asserted by host\n");

	pci_epc_deinit_notify(ep->epc);

	stm32_pcie_disable_resources(stm32_pcie);

	pm_runtime_put_sync(dev);
}

static void stm32_pcie_perst_deassert(struct dw_pcie *pci)
{
	struct stm32_pcie *stm32_pcie = to_stm32_pcie(pci);
	struct device *dev = pci->dev;
	struct dw_pcie_ep *ep = &pci->ep;
	int ret;

	dev_dbg(dev, "PERST de-asserted by host\n");

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to resume runtime PM: %d\n", ret);
		return;
	}

	ret = stm32_pcie_enable_resources(stm32_pcie);
	if (ret) {
		dev_err(dev, "Failed to enable resources: %d\n", ret);
		goto err_pm_put_sync;
	}

	/*
	 * Reprogram the configuration space registers here because the DBI
	 * registers were reset by the PHY RCC during phy_init().
	 */
	ret = dw_pcie_ep_init_registers(ep);
	if (ret) {
		dev_err(dev, "Failed to complete initialization: %d\n", ret);
		goto err_disable_resources;
	}

	pci_epc_init_notify(ep->epc);

	return;

err_disable_resources:
	stm32_pcie_disable_resources(stm32_pcie);

err_pm_put_sync:
	pm_runtime_put_sync(dev);
}

static irqreturn_t stm32_pcie_ep_perst_irq_thread(int irq, void *data)
{
	struct stm32_pcie *stm32_pcie = data;
	struct dw_pcie *pci = &stm32_pcie->pci;
	u32 perst;

	perst = gpiod_get_value(stm32_pcie->perst_gpio);
	if (perst)
		stm32_pcie_perst_assert(pci);
	else
		stm32_pcie_perst_deassert(pci);

	irq_set_irq_type(gpiod_to_irq(stm32_pcie->perst_gpio),
			 (perst ? IRQF_TRIGGER_HIGH : IRQF_TRIGGER_LOW));

	return IRQ_HANDLED;
}

static int stm32_add_pcie_ep(struct stm32_pcie *stm32_pcie,
			     struct platform_device *pdev)
{
	struct dw_pcie_ep *ep = &stm32_pcie->pci.ep;
	struct device *dev = &pdev->dev;
	int ret;

	ret = regmap_update_bits(stm32_pcie->regmap, SYSCFG_PCIECR,
				 STM32MP25_PCIECR_TYPE_MASK,
				 STM32MP25_PCIECR_EP);
	if (ret)
		return ret;

	reset_control_assert(stm32_pcie->rst);
	reset_control_deassert(stm32_pcie->rst);

	ep->ops = &stm32_pcie_ep_ops;

	ret = dw_pcie_ep_init(ep);
	if (ret) {
		dev_err(dev, "Failed to initialize ep: %d\n", ret);
		return ret;
	}

	ret = stm32_pcie_enable_resources(stm32_pcie);
	if (ret) {
		dev_err(dev, "Failed to enable resources: %d\n", ret);
		dw_pcie_ep_deinit(ep);
		return ret;
	}

	return 0;
}

static int stm32_pcie_probe(struct platform_device *pdev)
{
	struct stm32_pcie *stm32_pcie;
	struct device *dev = &pdev->dev;
	int ret;

	stm32_pcie = devm_kzalloc(dev, sizeof(*stm32_pcie), GFP_KERNEL);
	if (!stm32_pcie)
		return -ENOMEM;

	stm32_pcie->pci.dev = dev;
	stm32_pcie->pci.ops = &dw_pcie_ops;

	stm32_pcie->regmap = syscon_regmap_lookup_by_compatible("st,stm32mp25-syscfg");
	if (IS_ERR(stm32_pcie->regmap))
		return dev_err_probe(dev, PTR_ERR(stm32_pcie->regmap),
				     "No syscfg specified\n");

	stm32_pcie->phy = devm_phy_get(dev, NULL);
	if (IS_ERR(stm32_pcie->phy))
		return dev_err_probe(dev, PTR_ERR(stm32_pcie->phy),
				     "failed to get pcie-phy\n");

	stm32_pcie->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(stm32_pcie->clk))
		return dev_err_probe(dev, PTR_ERR(stm32_pcie->clk),
				     "Failed to get PCIe clock source\n");

	stm32_pcie->rst = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(stm32_pcie->rst))
		return dev_err_probe(dev, PTR_ERR(stm32_pcie->rst),
				     "Failed to get PCIe reset\n");

	stm32_pcie->perst_gpio = devm_gpiod_get(dev, "reset", GPIOD_IN);
	if (IS_ERR(stm32_pcie->perst_gpio))
		return dev_err_probe(dev, PTR_ERR(stm32_pcie->perst_gpio),
				     "Failed to get reset GPIO\n");

	ret = phy_set_mode(stm32_pcie->phy, PHY_MODE_PCIE);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, stm32_pcie);

	pm_runtime_get_noresume(dev);

	ret = devm_pm_runtime_enable(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(&pdev->dev);
		return dev_err_probe(dev, ret, "Failed to enable runtime PM\n");
	}

	stm32_pcie->perst_irq = gpiod_to_irq(stm32_pcie->perst_gpio);

	/* Will be enabled in start_link when device is initialized. */
	irq_set_status_flags(stm32_pcie->perst_irq, IRQ_NOAUTOEN);

	ret = devm_request_threaded_irq(dev, stm32_pcie->perst_irq, NULL,
					stm32_pcie_ep_perst_irq_thread,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"perst_irq", stm32_pcie);
	if (ret) {
		pm_runtime_put_noidle(&pdev->dev);
		return dev_err_probe(dev, ret, "Failed to request PERST IRQ\n");
	}

	ret = stm32_add_pcie_ep(stm32_pcie, pdev);
	if (ret)
		pm_runtime_put_noidle(&pdev->dev);

	return ret;
}

static void stm32_pcie_remove(struct platform_device *pdev)
{
	struct stm32_pcie *stm32_pcie = platform_get_drvdata(pdev);
	struct dw_pcie *pci = &stm32_pcie->pci;
	struct dw_pcie_ep *ep = &pci->ep;

	dw_pcie_stop_link(pci);

	pci_epc_deinit_notify(ep->epc);
	dw_pcie_ep_deinit(ep);

	stm32_pcie_disable_resources(stm32_pcie);

	pm_runtime_put_sync(&pdev->dev);
}

static const struct of_device_id stm32_pcie_ep_of_match[] = {
	{ .compatible = "st,stm32mp25-pcie-ep" },
	{},
};

static struct platform_driver stm32_pcie_ep_driver = {
	.probe = stm32_pcie_probe,
	.remove = stm32_pcie_remove,
	.driver = {
		.name = "stm32-ep-pcie",
		.of_match_table = stm32_pcie_ep_of_match,
	},
};

module_platform_driver(stm32_pcie_ep_driver);

MODULE_AUTHOR("Christian Bruel <christian.bruel@foss.st.com>");
MODULE_DESCRIPTION("STM32MP25 PCIe Endpoint Controller driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, stm32_pcie_ep_of_match);
