// SPDX-License-Identifier: GPL-2.0+
/*
 * PCIe host controller driver for StarFive JH7110 Soc.
 *
 * Copyright (C) 2023 StarFive Technology Co., Ltd.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include "../../pci.h"

#include "pcie-plda.h"

#define PCIE_FUNC_NUM			4

/* system control */
#define STG_SYSCON_PCIE0_BASE			0x48
#define STG_SYSCON_PCIE1_BASE			0x1f8

#define STG_SYSCON_AR_OFFSET			0x78
#define STG_SYSCON_AXI4_SLVL_AR_MASK		GENMASK(22, 8)
#define STG_SYSCON_AXI4_SLVL_PHY_AR(x)		FIELD_PREP(GENMASK(20, 17), x)
#define STG_SYSCON_AW_OFFSET			0x7c
#define STG_SYSCON_AXI4_SLVL_AW_MASK		GENMASK(14, 0)
#define STG_SYSCON_AXI4_SLVL_PHY_AW(x)		FIELD_PREP(GENMASK(12, 9), x)
#define STG_SYSCON_CLKREQ			BIT(22)
#define STG_SYSCON_CKREF_SRC_MASK		GENMASK(19, 18)
#define STG_SYSCON_RP_NEP_OFFSET		0xe8
#define STG_SYSCON_K_RP_NEP			BIT(8)
#define STG_SYSCON_LNKSTA_OFFSET		0x170
#define DATA_LINK_ACTIVE			BIT(5)

/* Parameters for the waiting for link up routine */
#define LINK_WAIT_MAX_RETRIES	10
#define LINK_WAIT_USLEEP_MIN	90000
#define LINK_WAIT_USLEEP_MAX	100000

struct starfive_jh7110_pcie {
	struct plda_pcie_rp plda;
	struct reset_control *resets;
	struct clk_bulk_data *clks;
	struct regmap *reg_syscon;
	struct gpio_desc *power_gpio;
	struct gpio_desc *reset_gpio;
	struct phy *phy;

	unsigned int stg_pcie_base;
	int num_clks;
};

/*
 * JH7110 PCIe port BAR0/1 can be configured as 64-bit prefetchable memory
 * space. PCIe read and write requests targeting BAR0/1 are routed to so called
 * 'Bridge Configuration space' in PLDA IP datasheet, which contains the bridge
 * internal registers, such as interrupt, DMA and ATU registers...
 * JH7110 can access the Bridge Configuration space by local bus, and don`t
 * want the bridge internal registers accessed by the DMA from EP devices.
 * Thus, they are unimplemented and should be hidden here.
 */
static bool starfive_pcie_hide_rc_bar(struct pci_bus *bus, unsigned int devfn,
				      int offset)
{
	if (pci_is_root_bus(bus) && !devfn &&
	    (offset == PCI_BASE_ADDRESS_0 || offset == PCI_BASE_ADDRESS_1))
		return true;

	return false;
}

static int starfive_pcie_config_write(struct pci_bus *bus, unsigned int devfn,
				      int where, int size, u32 value)
{
	if (starfive_pcie_hide_rc_bar(bus, devfn, where))
		return PCIBIOS_SUCCESSFUL;

	return pci_generic_config_write(bus, devfn, where, size, value);
}

static int starfive_pcie_config_read(struct pci_bus *bus, unsigned int devfn,
				     int where, int size, u32 *value)
{
	if (starfive_pcie_hide_rc_bar(bus, devfn, where)) {
		*value = 0;
		return PCIBIOS_SUCCESSFUL;
	}

	return pci_generic_config_read(bus, devfn, where, size, value);
}

static int starfive_pcie_parse_dt(struct starfive_jh7110_pcie *pcie,
				  struct device *dev)
{
	int domain_nr;

	pcie->num_clks = devm_clk_bulk_get_all(dev, &pcie->clks);
	if (pcie->num_clks < 0)
		return dev_err_probe(dev, pcie->num_clks,
				     "failed to get pcie clocks\n");

	pcie->resets = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(pcie->resets))
		return dev_err_probe(dev, PTR_ERR(pcie->resets),
				     "failed to get pcie resets");

	pcie->reg_syscon =
		syscon_regmap_lookup_by_phandle(dev->of_node,
						"starfive,stg-syscon");

	if (IS_ERR(pcie->reg_syscon))
		return dev_err_probe(dev, PTR_ERR(pcie->reg_syscon),
				     "failed to parse starfive,stg-syscon\n");

	pcie->phy = devm_phy_optional_get(dev, NULL);
	if (IS_ERR(pcie->phy))
		return dev_err_probe(dev, PTR_ERR(pcie->phy),
				     "failed to get pcie phy\n");

	/*
	 * The PCIe domain numbers are set to be static in JH7110 DTS.
	 * As the STG system controller defines different bases in PCIe RP0 &
	 * RP1, we use them to identify which controller is doing the hardware
	 * initialization.
	 */
	domain_nr = of_get_pci_domain_nr(dev->of_node);

	if (domain_nr < 0 || domain_nr > 1)
		return dev_err_probe(dev, -ENODEV,
				     "failed to get valid pcie domain\n");

	if (domain_nr == 0)
		pcie->stg_pcie_base = STG_SYSCON_PCIE0_BASE;
	else
		pcie->stg_pcie_base = STG_SYSCON_PCIE1_BASE;

	pcie->reset_gpio = devm_gpiod_get_optional(dev, "perst",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(pcie->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(pcie->reset_gpio),
				     "failed to get perst-gpio\n");

	pcie->power_gpio = devm_gpiod_get_optional(dev, "enable",
						   GPIOD_OUT_LOW);
	if (IS_ERR(pcie->power_gpio))
		return dev_err_probe(dev, PTR_ERR(pcie->power_gpio),
				     "failed to get power-gpio\n");

	return 0;
}

static struct pci_ops starfive_pcie_ops = {
	.map_bus	= plda_pcie_map_bus,
	.read           = starfive_pcie_config_read,
	.write          = starfive_pcie_config_write,
};

static int starfive_pcie_clk_rst_init(struct starfive_jh7110_pcie *pcie)
{
	struct device *dev = pcie->plda.dev;
	int ret;

	ret = clk_bulk_prepare_enable(pcie->num_clks, pcie->clks);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable clocks\n");

	ret = reset_control_deassert(pcie->resets);
	if (ret) {
		clk_bulk_disable_unprepare(pcie->num_clks, pcie->clks);
		dev_err_probe(dev, ret, "failed to deassert resets\n");
	}

	return ret;
}

static void starfive_pcie_clk_rst_deinit(struct starfive_jh7110_pcie *pcie)
{
	reset_control_assert(pcie->resets);
	clk_bulk_disable_unprepare(pcie->num_clks, pcie->clks);
}

static bool starfive_pcie_link_up(struct plda_pcie_rp *plda)
{
	struct starfive_jh7110_pcie *pcie =
		container_of(plda, struct starfive_jh7110_pcie, plda);
	int ret;
	u32 stg_reg_val;

	ret = regmap_read(pcie->reg_syscon,
			  pcie->stg_pcie_base + STG_SYSCON_LNKSTA_OFFSET,
			  &stg_reg_val);
	if (ret) {
		dev_err(pcie->plda.dev, "failed to read link status\n");
		return false;
	}

	return !!(stg_reg_val & DATA_LINK_ACTIVE);
}

static int starfive_pcie_host_wait_for_link(struct starfive_jh7110_pcie *pcie)
{
	int retries;

	/* Check if the link is up or not */
	for (retries = 0; retries < LINK_WAIT_MAX_RETRIES; retries++) {
		if (starfive_pcie_link_up(&pcie->plda)) {
			dev_info(pcie->plda.dev, "port link up\n");
			return 0;
		}
		usleep_range(LINK_WAIT_USLEEP_MIN, LINK_WAIT_USLEEP_MAX);
	}

	return -ETIMEDOUT;
}

static int starfive_pcie_enable_phy(struct device *dev,
				    struct starfive_jh7110_pcie *pcie)
{
	int ret;

	if (!pcie->phy)
		return 0;

	ret = phy_init(pcie->phy);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to initialize pcie phy\n");

	ret = phy_set_mode(pcie->phy, PHY_MODE_PCIE);
	if (ret) {
		dev_err_probe(dev, ret, "failed to set pcie mode\n");
		goto err_phy_on;
	}

	ret = phy_power_on(pcie->phy);
	if (ret) {
		dev_err_probe(dev, ret, "failed to power on pcie phy\n");
		goto err_phy_on;
	}

	return 0;

err_phy_on:
	phy_exit(pcie->phy);
	return ret;
}

static void starfive_pcie_disable_phy(struct starfive_jh7110_pcie *pcie)
{
	phy_power_off(pcie->phy);
	phy_exit(pcie->phy);
}

static void starfive_pcie_host_deinit(struct plda_pcie_rp *plda)
{
	struct starfive_jh7110_pcie *pcie =
		container_of(plda, struct starfive_jh7110_pcie, plda);

	starfive_pcie_clk_rst_deinit(pcie);
	if (pcie->power_gpio)
		gpiod_set_value_cansleep(pcie->power_gpio, 0);
	starfive_pcie_disable_phy(pcie);
}

static int starfive_pcie_host_init(struct plda_pcie_rp *plda)
{
	struct starfive_jh7110_pcie *pcie =
		container_of(plda, struct starfive_jh7110_pcie, plda);
	struct device *dev = plda->dev;
	int ret;
	int i;

	ret = starfive_pcie_enable_phy(dev, pcie);
	if (ret)
		return ret;

	regmap_update_bits(pcie->reg_syscon,
			   pcie->stg_pcie_base + STG_SYSCON_RP_NEP_OFFSET,
			   STG_SYSCON_K_RP_NEP, STG_SYSCON_K_RP_NEP);

	regmap_update_bits(pcie->reg_syscon,
			   pcie->stg_pcie_base + STG_SYSCON_AW_OFFSET,
			   STG_SYSCON_CKREF_SRC_MASK,
			   FIELD_PREP(STG_SYSCON_CKREF_SRC_MASK, 2));

	regmap_update_bits(pcie->reg_syscon,
			   pcie->stg_pcie_base + STG_SYSCON_AW_OFFSET,
			   STG_SYSCON_CLKREQ, STG_SYSCON_CLKREQ);

	ret = starfive_pcie_clk_rst_init(pcie);
	if (ret)
		return ret;

	if (pcie->power_gpio)
		gpiod_set_value_cansleep(pcie->power_gpio, 1);

	if (pcie->reset_gpio)
		gpiod_set_value_cansleep(pcie->reset_gpio, 1);

	/* Disable physical functions except #0 */
	for (i = 1; i < PCIE_FUNC_NUM; i++) {
		regmap_update_bits(pcie->reg_syscon,
				   pcie->stg_pcie_base + STG_SYSCON_AR_OFFSET,
				   STG_SYSCON_AXI4_SLVL_AR_MASK,
				   STG_SYSCON_AXI4_SLVL_PHY_AR(i));

		regmap_update_bits(pcie->reg_syscon,
				   pcie->stg_pcie_base + STG_SYSCON_AW_OFFSET,
				   STG_SYSCON_AXI4_SLVL_AW_MASK,
				   STG_SYSCON_AXI4_SLVL_PHY_AW(i));

		plda_pcie_disable_func(plda);
	}

	regmap_update_bits(pcie->reg_syscon,
			   pcie->stg_pcie_base + STG_SYSCON_AR_OFFSET,
			   STG_SYSCON_AXI4_SLVL_AR_MASK, 0);
	regmap_update_bits(pcie->reg_syscon,
			   pcie->stg_pcie_base + STG_SYSCON_AW_OFFSET,
			   STG_SYSCON_AXI4_SLVL_AW_MASK, 0);

	plda_pcie_enable_root_port(plda);
	plda_pcie_write_rc_bar(plda, 0);

	/* PCIe PCI Standard Configuration Identification Settings. */
	plda_pcie_set_standard_class(plda);

	/*
	 * The LTR message receiving is enabled by the register "PCIe Message
	 * Reception" as default, but the forward id & addr are uninitialized.
	 * If we do not disable LTR message forwarding here, or set a legal
	 * forwarding address, the kernel will get stuck.
	 * To workaround, disable the LTR message forwarding here before using
	 * this feature.
	 */
	plda_pcie_disable_ltr(plda);

	/*
	 * Enable the prefetchable memory window 64-bit addressing in JH7110.
	 * The 64-bits prefetchable address translation configurations in ATU
	 * can be work after enable the register setting below.
	 */
	plda_pcie_set_pref_win_64bit(plda);

	/*
	 * Ensure that PERST has been asserted for at least 100 ms,
	 * the sleep value is T_PVPERL from PCIe CEM spec r2.0 (Table 2-4)
	 */
	msleep(100);
	if (pcie->reset_gpio)
		gpiod_set_value_cansleep(pcie->reset_gpio, 0);

	/*
	 * With a Downstream Port (<=5GT/s), software must wait a minimum
	 * of 100ms following exit from a conventional reset before
	 * sending a configuration request to the device.
	 */
	msleep(PCIE_RESET_CONFIG_DEVICE_WAIT_MS);

	if (starfive_pcie_host_wait_for_link(pcie))
		dev_info(dev, "port link down\n");

	return 0;
}

static const struct plda_pcie_host_ops sf_host_ops = {
	.host_init = starfive_pcie_host_init,
	.host_deinit = starfive_pcie_host_deinit,
};

static const struct plda_event stf_pcie_event = {
	.intx_event = EVENT_PM_MSI_INT_INTX,
	.msi_event  = EVENT_PM_MSI_INT_MSI
};

static int starfive_pcie_probe(struct platform_device *pdev)
{
	struct starfive_jh7110_pcie *pcie;
	struct device *dev = &pdev->dev;
	struct plda_pcie_rp *plda;
	int ret;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	plda = &pcie->plda;
	plda->dev = dev;

	ret = starfive_pcie_parse_dt(pcie, dev);
	if (ret)
		return ret;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	plda->host_ops = &sf_host_ops;
	plda->num_events = PLDA_MAX_EVENT_NUM;
	/* mask doorbell event */
	plda->events_bitmap = GENMASK(PLDA_INT_EVENT_NUM - 1, 0)
			     & ~BIT(PLDA_AXI_DOORBELL)
			     & ~BIT(PLDA_PCIE_DOORBELL);
	plda->events_bitmap <<= PLDA_NUM_DMA_EVENTS;
	ret = plda_pcie_host_init(&pcie->plda, &starfive_pcie_ops,
				  &stf_pcie_event);
	if (ret) {
		pm_runtime_put_sync(&pdev->dev);
		pm_runtime_disable(&pdev->dev);
		return ret;
	}

	platform_set_drvdata(pdev, pcie);

	return 0;
}

static void starfive_pcie_remove(struct platform_device *pdev)
{
	struct starfive_jh7110_pcie *pcie = platform_get_drvdata(pdev);

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	plda_pcie_host_deinit(&pcie->plda);
	platform_set_drvdata(pdev, NULL);
}

static int starfive_pcie_suspend_noirq(struct device *dev)
{
	struct starfive_jh7110_pcie *pcie = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(pcie->num_clks, pcie->clks);
	starfive_pcie_disable_phy(pcie);

	return 0;
}

static int starfive_pcie_resume_noirq(struct device *dev)
{
	struct starfive_jh7110_pcie *pcie = dev_get_drvdata(dev);
	int ret;

	ret = starfive_pcie_enable_phy(dev, pcie);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(pcie->num_clks, pcie->clks);
	if (ret) {
		dev_err(dev, "failed to enable clocks\n");
		starfive_pcie_disable_phy(pcie);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops starfive_pcie_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(starfive_pcie_suspend_noirq,
				  starfive_pcie_resume_noirq)
};

static const struct of_device_id starfive_pcie_of_match[] = {
	{ .compatible = "starfive,jh7110-pcie", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, starfive_pcie_of_match);

static struct platform_driver starfive_pcie_driver = {
	.driver = {
		.name = "pcie-starfive",
		.of_match_table = of_match_ptr(starfive_pcie_of_match),
		.pm = pm_sleep_ptr(&starfive_pcie_pm_ops),
	},
	.probe = starfive_pcie_probe,
	.remove_new = starfive_pcie_remove,
};
module_platform_driver(starfive_pcie_driver);

MODULE_DESCRIPTION("StarFive JH7110 PCIe host driver");
MODULE_LICENSE("GPL v2");
