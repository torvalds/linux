// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#include <linux/gpio/property.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/i2c.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "../libwx/wx_type.h"
#include "txgbe_type.h"
#include "txgbe_phy.h"

static int txgbe_swnodes_register(struct txgbe *txgbe)
{
	struct txgbe_nodes *nodes = &txgbe->nodes;
	struct pci_dev *pdev = txgbe->wx->pdev;
	struct software_node *swnodes;
	u32 id;

	id = (pdev->bus->number << 8) | pdev->devfn;

	snprintf(nodes->gpio_name, sizeof(nodes->gpio_name), "txgbe_gpio-%x", id);
	snprintf(nodes->i2c_name, sizeof(nodes->i2c_name), "txgbe_i2c-%x", id);
	snprintf(nodes->sfp_name, sizeof(nodes->sfp_name), "txgbe_sfp-%x", id);
	snprintf(nodes->phylink_name, sizeof(nodes->phylink_name), "txgbe_phylink-%x", id);

	swnodes = nodes->swnodes;

	/* GPIO 0: tx fault
	 * GPIO 1: tx disable
	 * GPIO 2: sfp module absent
	 * GPIO 3: rx signal lost
	 * GPIO 4: rate select, 1G(0) 10G(1)
	 * GPIO 5: rate select, 1G(0) 10G(1)
	 */
	nodes->gpio_props[0] = PROPERTY_ENTRY_STRING("pinctrl-names", "default");
	swnodes[SWNODE_GPIO] = NODE_PROP(nodes->gpio_name, nodes->gpio_props);
	nodes->gpio0_ref[0] = SOFTWARE_NODE_REFERENCE(&swnodes[SWNODE_GPIO], 0, GPIO_ACTIVE_HIGH);
	nodes->gpio1_ref[0] = SOFTWARE_NODE_REFERENCE(&swnodes[SWNODE_GPIO], 1, GPIO_ACTIVE_HIGH);
	nodes->gpio2_ref[0] = SOFTWARE_NODE_REFERENCE(&swnodes[SWNODE_GPIO], 2, GPIO_ACTIVE_LOW);
	nodes->gpio3_ref[0] = SOFTWARE_NODE_REFERENCE(&swnodes[SWNODE_GPIO], 3, GPIO_ACTIVE_HIGH);
	nodes->gpio4_ref[0] = SOFTWARE_NODE_REFERENCE(&swnodes[SWNODE_GPIO], 4, GPIO_ACTIVE_HIGH);
	nodes->gpio5_ref[0] = SOFTWARE_NODE_REFERENCE(&swnodes[SWNODE_GPIO], 5, GPIO_ACTIVE_HIGH);

	nodes->i2c_props[0] = PROPERTY_ENTRY_STRING("compatible", "snps,designware-i2c");
	nodes->i2c_props[1] = PROPERTY_ENTRY_BOOL("wx,i2c-snps-model");
	nodes->i2c_props[2] = PROPERTY_ENTRY_U32("clock-frequency", I2C_MAX_STANDARD_MODE_FREQ);
	swnodes[SWNODE_I2C] = NODE_PROP(nodes->i2c_name, nodes->i2c_props);
	nodes->i2c_ref[0] = SOFTWARE_NODE_REFERENCE(&swnodes[SWNODE_I2C]);

	nodes->sfp_props[0] = PROPERTY_ENTRY_STRING("compatible", "sff,sfp");
	nodes->sfp_props[1] = PROPERTY_ENTRY_REF_ARRAY("i2c-bus", nodes->i2c_ref);
	nodes->sfp_props[2] = PROPERTY_ENTRY_REF_ARRAY("tx-fault-gpios", nodes->gpio0_ref);
	nodes->sfp_props[3] = PROPERTY_ENTRY_REF_ARRAY("tx-disable-gpios", nodes->gpio1_ref);
	nodes->sfp_props[4] = PROPERTY_ENTRY_REF_ARRAY("mod-def0-gpios", nodes->gpio2_ref);
	nodes->sfp_props[5] = PROPERTY_ENTRY_REF_ARRAY("los-gpios", nodes->gpio3_ref);
	nodes->sfp_props[6] = PROPERTY_ENTRY_REF_ARRAY("rate-select1-gpios", nodes->gpio4_ref);
	nodes->sfp_props[7] = PROPERTY_ENTRY_REF_ARRAY("rate-select0-gpios", nodes->gpio5_ref);
	swnodes[SWNODE_SFP] = NODE_PROP(nodes->sfp_name, nodes->sfp_props);
	nodes->sfp_ref[0] = SOFTWARE_NODE_REFERENCE(&swnodes[SWNODE_SFP]);

	nodes->phylink_props[0] = PROPERTY_ENTRY_STRING("managed", "in-band-status");
	nodes->phylink_props[1] = PROPERTY_ENTRY_REF_ARRAY("sfp", nodes->sfp_ref);
	swnodes[SWNODE_PHYLINK] = NODE_PROP(nodes->phylink_name, nodes->phylink_props);

	nodes->group[SWNODE_GPIO] = &swnodes[SWNODE_GPIO];
	nodes->group[SWNODE_I2C] = &swnodes[SWNODE_I2C];
	nodes->group[SWNODE_SFP] = &swnodes[SWNODE_SFP];
	nodes->group[SWNODE_PHYLINK] = &swnodes[SWNODE_PHYLINK];

	return software_node_register_node_group(nodes->group);
}

static int txgbe_clock_register(struct txgbe *txgbe)
{
	struct pci_dev *pdev = txgbe->wx->pdev;
	struct clk_lookup *clock;
	char clk_name[32];
	struct clk *clk;

	snprintf(clk_name, sizeof(clk_name), "i2c_designware.%d",
		 (pdev->bus->number << 8) | pdev->devfn);

	clk = clk_register_fixed_rate(NULL, clk_name, NULL, 0, 156250000);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	clock = clkdev_create(clk, NULL, clk_name);
	if (!clock) {
		clk_unregister(clk);
		return -ENOMEM;
	}

	txgbe->clk = clk;
	txgbe->clock = clock;

	return 0;
}

static int txgbe_i2c_read(void *context, unsigned int reg, unsigned int *val)
{
	struct wx *wx = context;

	*val = rd32(wx, reg + TXGBE_I2C_BASE);

	return 0;
}

static int txgbe_i2c_write(void *context, unsigned int reg, unsigned int val)
{
	struct wx *wx = context;

	wr32(wx, reg + TXGBE_I2C_BASE, val);

	return 0;
}

static const struct regmap_config i2c_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_read = txgbe_i2c_read,
	.reg_write = txgbe_i2c_write,
	.fast_io = true,
};

static int txgbe_i2c_register(struct txgbe *txgbe)
{
	struct platform_device_info info = {};
	struct platform_device *i2c_dev;
	struct regmap *i2c_regmap;
	struct pci_dev *pdev;
	struct wx *wx;

	wx = txgbe->wx;
	pdev = wx->pdev;
	i2c_regmap = devm_regmap_init(&pdev->dev, NULL, wx, &i2c_regmap_config);
	if (IS_ERR(i2c_regmap)) {
		wx_err(wx, "failed to init I2C regmap\n");
		return PTR_ERR(i2c_regmap);
	}

	info.parent = &pdev->dev;
	info.fwnode = software_node_fwnode(txgbe->nodes.group[SWNODE_I2C]);
	info.name = "i2c_designware";
	info.id = (pdev->bus->number << 8) | pdev->devfn;

	info.res = &DEFINE_RES_IRQ(pdev->irq);
	info.num_res = 1;
	i2c_dev = platform_device_register_full(&info);
	if (IS_ERR(i2c_dev))
		return PTR_ERR(i2c_dev);

	txgbe->i2c_dev = i2c_dev;

	return 0;
}

int txgbe_init_phy(struct txgbe *txgbe)
{
	int ret;

	ret = txgbe_swnodes_register(txgbe);
	if (ret) {
		wx_err(txgbe->wx, "failed to register software nodes\n");
		return ret;
	}

	ret = txgbe_clock_register(txgbe);
	if (ret) {
		wx_err(txgbe->wx, "failed to register clock: %d\n", ret);
		goto err_unregister_swnode;
	}

	ret = txgbe_i2c_register(txgbe);
	if (ret) {
		wx_err(txgbe->wx, "failed to init i2c interface: %d\n", ret);
		goto err_unregister_clk;
	}

	return 0;

err_unregister_clk:
	clkdev_drop(txgbe->clock);
	clk_unregister(txgbe->clk);
err_unregister_swnode:
	software_node_unregister_node_group(txgbe->nodes.group);

	return ret;
}

void txgbe_remove_phy(struct txgbe *txgbe)
{
	platform_device_unregister(txgbe->i2c_dev);
	clkdev_drop(txgbe->clock);
	clk_unregister(txgbe->clk);
	software_node_unregister_node_group(txgbe->nodes.group);
}
