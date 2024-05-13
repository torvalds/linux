// SPDX-License-Identifier: GPL-2.0
/*
 * pci-j721e - PCIe controller driver for TI's J721E SoCs
 *
 * Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include "../../pci.h"
#include "pcie-cadence.h"

#define ENABLE_REG_SYS_2	0x108
#define STATUS_REG_SYS_2	0x508
#define STATUS_CLR_REG_SYS_2	0x708
#define LINK_DOWN		BIT(1)
#define J7200_LINK_DOWN		BIT(10)

#define J721E_PCIE_USER_CMD_STATUS	0x4
#define LINK_TRAINING_ENABLE		BIT(0)

#define J721E_PCIE_USER_LINKSTATUS	0x14
#define LINK_STATUS			GENMASK(1, 0)

enum link_status {
	NO_RECEIVERS_DETECTED,
	LINK_TRAINING_IN_PROGRESS,
	LINK_UP_DL_IN_PROGRESS,
	LINK_UP_DL_COMPLETED,
};

#define J721E_MODE_RC			BIT(7)
#define LANE_COUNT_MASK			BIT(8)
#define LANE_COUNT(n)			((n) << 8)

#define GENERATION_SEL_MASK		GENMASK(1, 0)

#define MAX_LANES			2

struct j721e_pcie {
	struct cdns_pcie	*cdns_pcie;
	struct clk		*refclk;
	u32			mode;
	u32			num_lanes;
	void __iomem		*user_cfg_base;
	void __iomem		*intd_cfg_base;
	u32			linkdown_irq_regfield;
};

enum j721e_pcie_mode {
	PCI_MODE_RC,
	PCI_MODE_EP,
};

struct j721e_pcie_data {
	enum j721e_pcie_mode	mode;
	unsigned int		quirk_retrain_flag:1;
	unsigned int		quirk_detect_quiet_flag:1;
	unsigned int		quirk_disable_flr:1;
	u32			linkdown_irq_regfield;
	unsigned int		byte_access_allowed:1;
};

static inline u32 j721e_pcie_user_readl(struct j721e_pcie *pcie, u32 offset)
{
	return readl(pcie->user_cfg_base + offset);
}

static inline void j721e_pcie_user_writel(struct j721e_pcie *pcie, u32 offset,
					  u32 value)
{
	writel(value, pcie->user_cfg_base + offset);
}

static inline u32 j721e_pcie_intd_readl(struct j721e_pcie *pcie, u32 offset)
{
	return readl(pcie->intd_cfg_base + offset);
}

static inline void j721e_pcie_intd_writel(struct j721e_pcie *pcie, u32 offset,
					  u32 value)
{
	writel(value, pcie->intd_cfg_base + offset);
}

static irqreturn_t j721e_pcie_link_irq_handler(int irq, void *priv)
{
	struct j721e_pcie *pcie = priv;
	struct device *dev = pcie->cdns_pcie->dev;
	u32 reg;

	reg = j721e_pcie_intd_readl(pcie, STATUS_REG_SYS_2);
	if (!(reg & pcie->linkdown_irq_regfield))
		return IRQ_NONE;

	dev_err(dev, "LINK DOWN!\n");

	j721e_pcie_intd_writel(pcie, STATUS_CLR_REG_SYS_2, pcie->linkdown_irq_regfield);
	return IRQ_HANDLED;
}

static void j721e_pcie_config_link_irq(struct j721e_pcie *pcie)
{
	u32 reg;

	reg = j721e_pcie_intd_readl(pcie, ENABLE_REG_SYS_2);
	reg |= pcie->linkdown_irq_regfield;
	j721e_pcie_intd_writel(pcie, ENABLE_REG_SYS_2, reg);
}

static int j721e_pcie_start_link(struct cdns_pcie *cdns_pcie)
{
	struct j721e_pcie *pcie = dev_get_drvdata(cdns_pcie->dev);
	u32 reg;

	reg = j721e_pcie_user_readl(pcie, J721E_PCIE_USER_CMD_STATUS);
	reg |= LINK_TRAINING_ENABLE;
	j721e_pcie_user_writel(pcie, J721E_PCIE_USER_CMD_STATUS, reg);

	return 0;
}

static void j721e_pcie_stop_link(struct cdns_pcie *cdns_pcie)
{
	struct j721e_pcie *pcie = dev_get_drvdata(cdns_pcie->dev);
	u32 reg;

	reg = j721e_pcie_user_readl(pcie, J721E_PCIE_USER_CMD_STATUS);
	reg &= ~LINK_TRAINING_ENABLE;
	j721e_pcie_user_writel(pcie, J721E_PCIE_USER_CMD_STATUS, reg);
}

static bool j721e_pcie_link_up(struct cdns_pcie *cdns_pcie)
{
	struct j721e_pcie *pcie = dev_get_drvdata(cdns_pcie->dev);
	u32 reg;

	reg = j721e_pcie_user_readl(pcie, J721E_PCIE_USER_LINKSTATUS);
	reg &= LINK_STATUS;
	if (reg == LINK_UP_DL_COMPLETED)
		return true;

	return false;
}

static const struct cdns_pcie_ops j721e_pcie_ops = {
	.start_link = j721e_pcie_start_link,
	.stop_link = j721e_pcie_stop_link,
	.link_up = j721e_pcie_link_up,
};

static int j721e_pcie_set_mode(struct j721e_pcie *pcie, struct regmap *syscon,
			       unsigned int offset)
{
	struct device *dev = pcie->cdns_pcie->dev;
	u32 mask = J721E_MODE_RC;
	u32 mode = pcie->mode;
	u32 val = 0;
	int ret = 0;

	if (mode == PCI_MODE_RC)
		val = J721E_MODE_RC;

	ret = regmap_update_bits(syscon, offset, mask, val);
	if (ret)
		dev_err(dev, "failed to set pcie mode\n");

	return ret;
}

static int j721e_pcie_set_link_speed(struct j721e_pcie *pcie,
				     struct regmap *syscon, unsigned int offset)
{
	struct device *dev = pcie->cdns_pcie->dev;
	struct device_node *np = dev->of_node;
	int link_speed;
	u32 val = 0;
	int ret;

	link_speed = of_pci_get_max_link_speed(np);
	if (link_speed < 2)
		link_speed = 2;

	val = link_speed - 1;
	ret = regmap_update_bits(syscon, offset, GENERATION_SEL_MASK, val);
	if (ret)
		dev_err(dev, "failed to set link speed\n");

	return ret;
}

static int j721e_pcie_set_lane_count(struct j721e_pcie *pcie,
				     struct regmap *syscon, unsigned int offset)
{
	struct device *dev = pcie->cdns_pcie->dev;
	u32 lanes = pcie->num_lanes;
	u32 val = 0;
	int ret;

	val = LANE_COUNT(lanes - 1);
	ret = regmap_update_bits(syscon, offset, LANE_COUNT_MASK, val);
	if (ret)
		dev_err(dev, "failed to set link count\n");

	return ret;
}

static int j721e_pcie_ctrl_init(struct j721e_pcie *pcie)
{
	struct device *dev = pcie->cdns_pcie->dev;
	struct device_node *node = dev->of_node;
	struct of_phandle_args args;
	unsigned int offset = 0;
	struct regmap *syscon;
	int ret;

	syscon = syscon_regmap_lookup_by_phandle(node, "ti,syscon-pcie-ctrl");
	if (IS_ERR(syscon)) {
		dev_err(dev, "Unable to get ti,syscon-pcie-ctrl regmap\n");
		return PTR_ERR(syscon);
	}

	/* Do not error out to maintain old DT compatibility */
	ret = of_parse_phandle_with_fixed_args(node, "ti,syscon-pcie-ctrl", 1,
					       0, &args);
	if (!ret)
		offset = args.args[0];

	ret = j721e_pcie_set_mode(pcie, syscon, offset);
	if (ret < 0) {
		dev_err(dev, "Failed to set pci mode\n");
		return ret;
	}

	ret = j721e_pcie_set_link_speed(pcie, syscon, offset);
	if (ret < 0) {
		dev_err(dev, "Failed to set link speed\n");
		return ret;
	}

	ret = j721e_pcie_set_lane_count(pcie, syscon, offset);
	if (ret < 0) {
		dev_err(dev, "Failed to set num-lanes\n");
		return ret;
	}

	return 0;
}

static int cdns_ti_pcie_config_read(struct pci_bus *bus, unsigned int devfn,
				    int where, int size, u32 *value)
{
	if (pci_is_root_bus(bus))
		return pci_generic_config_read32(bus, devfn, where, size,
						 value);

	return pci_generic_config_read(bus, devfn, where, size, value);
}

static int cdns_ti_pcie_config_write(struct pci_bus *bus, unsigned int devfn,
				     int where, int size, u32 value)
{
	if (pci_is_root_bus(bus))
		return pci_generic_config_write32(bus, devfn, where, size,
						  value);

	return pci_generic_config_write(bus, devfn, where, size, value);
}

static struct pci_ops cdns_ti_pcie_host_ops = {
	.map_bus	= cdns_pci_map_bus,
	.read		= cdns_ti_pcie_config_read,
	.write		= cdns_ti_pcie_config_write,
};

static const struct j721e_pcie_data j721e_pcie_rc_data = {
	.mode = PCI_MODE_RC,
	.quirk_retrain_flag = true,
	.byte_access_allowed = false,
	.linkdown_irq_regfield = LINK_DOWN,
};

static const struct j721e_pcie_data j721e_pcie_ep_data = {
	.mode = PCI_MODE_EP,
	.linkdown_irq_regfield = LINK_DOWN,
};

static const struct j721e_pcie_data j7200_pcie_rc_data = {
	.mode = PCI_MODE_RC,
	.quirk_detect_quiet_flag = true,
	.linkdown_irq_regfield = J7200_LINK_DOWN,
	.byte_access_allowed = true,
};

static const struct j721e_pcie_data j7200_pcie_ep_data = {
	.mode = PCI_MODE_EP,
	.quirk_detect_quiet_flag = true,
	.quirk_disable_flr = true,
};

static const struct j721e_pcie_data am64_pcie_rc_data = {
	.mode = PCI_MODE_RC,
	.linkdown_irq_regfield = J7200_LINK_DOWN,
	.byte_access_allowed = true,
};

static const struct j721e_pcie_data am64_pcie_ep_data = {
	.mode = PCI_MODE_EP,
	.linkdown_irq_regfield = J7200_LINK_DOWN,
};

static const struct of_device_id of_j721e_pcie_match[] = {
	{
		.compatible = "ti,j721e-pcie-host",
		.data = &j721e_pcie_rc_data,
	},
	{
		.compatible = "ti,j721e-pcie-ep",
		.data = &j721e_pcie_ep_data,
	},
	{
		.compatible = "ti,j7200-pcie-host",
		.data = &j7200_pcie_rc_data,
	},
	{
		.compatible = "ti,j7200-pcie-ep",
		.data = &j7200_pcie_ep_data,
	},
	{
		.compatible = "ti,am64-pcie-host",
		.data = &am64_pcie_rc_data,
	},
	{
		.compatible = "ti,am64-pcie-ep",
		.data = &am64_pcie_ep_data,
	},
	{},
};

static int j721e_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct pci_host_bridge *bridge;
	const struct j721e_pcie_data *data;
	struct cdns_pcie *cdns_pcie;
	struct j721e_pcie *pcie;
	struct cdns_pcie_rc *rc = NULL;
	struct cdns_pcie_ep *ep = NULL;
	struct gpio_desc *gpiod;
	void __iomem *base;
	struct clk *clk;
	u32 num_lanes;
	u32 mode;
	int ret;
	int irq;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	mode = (u32)data->mode;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	switch (mode) {
	case PCI_MODE_RC:
		if (!IS_ENABLED(CONFIG_PCIE_CADENCE_HOST))
			return -ENODEV;

		bridge = devm_pci_alloc_host_bridge(dev, sizeof(*rc));
		if (!bridge)
			return -ENOMEM;

		if (!data->byte_access_allowed)
			bridge->ops = &cdns_ti_pcie_host_ops;
		rc = pci_host_bridge_priv(bridge);
		rc->quirk_retrain_flag = data->quirk_retrain_flag;
		rc->quirk_detect_quiet_flag = data->quirk_detect_quiet_flag;

		cdns_pcie = &rc->pcie;
		cdns_pcie->dev = dev;
		cdns_pcie->ops = &j721e_pcie_ops;
		pcie->cdns_pcie = cdns_pcie;
		break;
	case PCI_MODE_EP:
		if (!IS_ENABLED(CONFIG_PCIE_CADENCE_EP))
			return -ENODEV;

		ep = devm_kzalloc(dev, sizeof(*ep), GFP_KERNEL);
		if (!ep)
			return -ENOMEM;

		ep->quirk_detect_quiet_flag = data->quirk_detect_quiet_flag;
		ep->quirk_disable_flr = data->quirk_disable_flr;

		cdns_pcie = &ep->pcie;
		cdns_pcie->dev = dev;
		cdns_pcie->ops = &j721e_pcie_ops;
		pcie->cdns_pcie = cdns_pcie;
		break;
	default:
		dev_err(dev, "INVALID device type %d\n", mode);
		return 0;
	}

	pcie->mode = mode;
	pcie->linkdown_irq_regfield = data->linkdown_irq_regfield;

	base = devm_platform_ioremap_resource_byname(pdev, "intd_cfg");
	if (IS_ERR(base))
		return PTR_ERR(base);
	pcie->intd_cfg_base = base;

	base = devm_platform_ioremap_resource_byname(pdev, "user_cfg");
	if (IS_ERR(base))
		return PTR_ERR(base);
	pcie->user_cfg_base = base;

	ret = of_property_read_u32(node, "num-lanes", &num_lanes);
	if (ret || num_lanes > MAX_LANES)
		num_lanes = 1;
	pcie->num_lanes = num_lanes;

	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48)))
		return -EINVAL;

	irq = platform_get_irq_byname(pdev, "link_state");
	if (irq < 0)
		return irq;

	dev_set_drvdata(dev, pcie);
	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_get_sync failed\n");
		goto err_get_sync;
	}

	ret = j721e_pcie_ctrl_init(pcie);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_get_sync failed\n");
		goto err_get_sync;
	}

	ret = devm_request_irq(dev, irq, j721e_pcie_link_irq_handler, 0,
			       "j721e-pcie-link-down-irq", pcie);
	if (ret < 0) {
		dev_err(dev, "failed to request link state IRQ %d\n", irq);
		goto err_get_sync;
	}

	j721e_pcie_config_link_irq(pcie);

	switch (mode) {
	case PCI_MODE_RC:
		gpiod = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
		if (IS_ERR(gpiod)) {
			ret = PTR_ERR(gpiod);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Failed to get reset GPIO\n");
			goto err_get_sync;
		}

		ret = cdns_pcie_init_phy(dev, cdns_pcie);
		if (ret) {
			dev_err(dev, "Failed to init phy\n");
			goto err_get_sync;
		}

		clk = devm_clk_get_optional(dev, "pcie_refclk");
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			dev_err(dev, "failed to get pcie_refclk\n");
			goto err_pcie_setup;
		}

		ret = clk_prepare_enable(clk);
		if (ret) {
			dev_err(dev, "failed to enable pcie_refclk\n");
			goto err_pcie_setup;
		}
		pcie->refclk = clk;

		/*
		 * "Power Sequencing and Reset Signal Timings" table in
		 * PCI EXPRESS CARD ELECTROMECHANICAL SPECIFICATION, REV. 3.0
		 * indicates PERST# should be deasserted after minimum of 100us
		 * once REFCLK is stable. The REFCLK to the connector in RC
		 * mode is selected while enabling the PHY. So deassert PERST#
		 * after 100 us.
		 */
		if (gpiod) {
			usleep_range(100, 200);
			gpiod_set_value_cansleep(gpiod, 1);
		}

		ret = cdns_pcie_host_setup(rc);
		if (ret < 0) {
			clk_disable_unprepare(pcie->refclk);
			goto err_pcie_setup;
		}

		break;
	case PCI_MODE_EP:
		ret = cdns_pcie_init_phy(dev, cdns_pcie);
		if (ret) {
			dev_err(dev, "Failed to init phy\n");
			goto err_get_sync;
		}

		ret = cdns_pcie_ep_setup(ep);
		if (ret < 0)
			goto err_pcie_setup;

		break;
	}

	return 0;

err_pcie_setup:
	cdns_pcie_disable_phy(cdns_pcie);

err_get_sync:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return ret;
}

static void j721e_pcie_remove(struct platform_device *pdev)
{
	struct j721e_pcie *pcie = platform_get_drvdata(pdev);
	struct cdns_pcie *cdns_pcie = pcie->cdns_pcie;
	struct device *dev = &pdev->dev;

	clk_disable_unprepare(pcie->refclk);
	cdns_pcie_disable_phy(cdns_pcie);
	pm_runtime_put(dev);
	pm_runtime_disable(dev);
}

static struct platform_driver j721e_pcie_driver = {
	.probe  = j721e_pcie_probe,
	.remove_new = j721e_pcie_remove,
	.driver = {
		.name	= "j721e-pcie",
		.of_match_table = of_j721e_pcie_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(j721e_pcie_driver);
