// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#include <linux/gpio/machine.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/property.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/i2c.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/pcs/pcs-xpcs.h>
#include <linux/phylink.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_lib.h"
#include "../libwx/wx_hw.h"
#include "txgbe_type.h"
#include "txgbe_phy.h"
#include "txgbe_hw.h"

static int txgbe_swnodes_register(struct txgbe *txgbe)
{
	struct txgbe_nodes *nodes = &txgbe->nodes;
	struct pci_dev *pdev = txgbe->wx->pdev;
	struct software_node *swnodes;
	u32 id;

	id = pci_dev_id(pdev);

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

static int txgbe_pcs_read(struct mii_bus *bus, int addr, int devnum, int regnum)
{
	struct wx *wx  = bus->priv;
	u32 offset, val;

	if (addr)
		return -EOPNOTSUPP;

	offset = devnum << 16 | regnum;

	/* Set the LAN port indicator to IDA_ADDR */
	wr32(wx, TXGBE_XPCS_IDA_ADDR, offset);

	/* Read the data from IDA_DATA register */
	val = rd32(wx, TXGBE_XPCS_IDA_DATA);

	return (u16)val;
}

static int txgbe_pcs_write(struct mii_bus *bus, int addr, int devnum, int regnum, u16 val)
{
	struct wx *wx = bus->priv;
	u32 offset;

	if (addr)
		return -EOPNOTSUPP;

	offset = devnum << 16 | regnum;

	/* Set the LAN port indicator to IDA_ADDR */
	wr32(wx, TXGBE_XPCS_IDA_ADDR, offset);

	/* Write the data to IDA_DATA register */
	wr32(wx, TXGBE_XPCS_IDA_DATA, val);

	return 0;
}

static int txgbe_mdio_pcs_init(struct txgbe *txgbe)
{
	struct mii_bus *mii_bus;
	struct dw_xpcs *xpcs;
	struct pci_dev *pdev;
	struct wx *wx;
	int ret = 0;

	wx = txgbe->wx;
	pdev = wx->pdev;

	mii_bus = devm_mdiobus_alloc(&pdev->dev);
	if (!mii_bus)
		return -ENOMEM;

	mii_bus->name = "txgbe_pcs_mdio_bus";
	mii_bus->read_c45 = &txgbe_pcs_read;
	mii_bus->write_c45 = &txgbe_pcs_write;
	mii_bus->parent = &pdev->dev;
	mii_bus->phy_mask = ~0;
	mii_bus->priv = wx;
	snprintf(mii_bus->id, MII_BUS_ID_SIZE, "txgbe_pcs-%x",
		 pci_dev_id(pdev));

	ret = devm_mdiobus_register(&pdev->dev, mii_bus);
	if (ret)
		return ret;

	xpcs = xpcs_create_mdiodev(mii_bus, 0, PHY_INTERFACE_MODE_10GBASER);
	if (IS_ERR(xpcs))
		return PTR_ERR(xpcs);

	txgbe->xpcs = xpcs;

	return 0;
}

static struct phylink_pcs *txgbe_phylink_mac_select(struct phylink_config *config,
						    phy_interface_t interface)
{
	struct wx *wx = phylink_to_wx(config);
	struct txgbe *txgbe = wx->priv;

	if (interface == PHY_INTERFACE_MODE_10GBASER)
		return &txgbe->xpcs->pcs;

	return NULL;
}

static void txgbe_mac_config(struct phylink_config *config, unsigned int mode,
			     const struct phylink_link_state *state)
{
}

static void txgbe_mac_link_down(struct phylink_config *config,
				unsigned int mode, phy_interface_t interface)
{
	struct wx *wx = phylink_to_wx(config);

	wr32m(wx, WX_MAC_TX_CFG, WX_MAC_TX_CFG_TE, 0);
}

static void txgbe_mac_link_up(struct phylink_config *config,
			      struct phy_device *phy,
			      unsigned int mode, phy_interface_t interface,
			      int speed, int duplex,
			      bool tx_pause, bool rx_pause)
{
	struct wx *wx = phylink_to_wx(config);
	u32 txcfg, wdg;

	wx_fc_enable(wx, tx_pause, rx_pause);

	txcfg = rd32(wx, WX_MAC_TX_CFG);
	txcfg &= ~WX_MAC_TX_CFG_SPEED_MASK;

	switch (speed) {
	case SPEED_10000:
		txcfg |= WX_MAC_TX_CFG_SPEED_10G;
		break;
	case SPEED_1000:
	case SPEED_100:
	case SPEED_10:
		txcfg |= WX_MAC_TX_CFG_SPEED_1G;
		break;
	default:
		break;
	}

	wr32(wx, WX_MAC_TX_CFG, txcfg | WX_MAC_TX_CFG_TE);

	/* Re configure MAC Rx */
	wr32m(wx, WX_MAC_RX_CFG, WX_MAC_RX_CFG_RE, WX_MAC_RX_CFG_RE);
	wr32(wx, WX_MAC_PKT_FLT, WX_MAC_PKT_FLT_PR);
	wdg = rd32(wx, WX_MAC_WDG_TIMEOUT);
	wr32(wx, WX_MAC_WDG_TIMEOUT, wdg);
}

static int txgbe_mac_prepare(struct phylink_config *config, unsigned int mode,
			     phy_interface_t interface)
{
	struct wx *wx = phylink_to_wx(config);

	wr32m(wx, WX_MAC_TX_CFG, WX_MAC_TX_CFG_TE, 0);
	wr32m(wx, WX_MAC_RX_CFG, WX_MAC_RX_CFG_RE, 0);

	return txgbe_disable_sec_tx_path(wx);
}

static int txgbe_mac_finish(struct phylink_config *config, unsigned int mode,
			    phy_interface_t interface)
{
	struct wx *wx = phylink_to_wx(config);

	txgbe_enable_sec_tx_path(wx);
	wr32m(wx, WX_MAC_RX_CFG, WX_MAC_RX_CFG_RE, WX_MAC_RX_CFG_RE);

	return 0;
}

static const struct phylink_mac_ops txgbe_mac_ops = {
	.mac_select_pcs = txgbe_phylink_mac_select,
	.mac_prepare = txgbe_mac_prepare,
	.mac_finish = txgbe_mac_finish,
	.mac_config = txgbe_mac_config,
	.mac_link_down = txgbe_mac_link_down,
	.mac_link_up = txgbe_mac_link_up,
};

static int txgbe_phylink_init(struct txgbe *txgbe)
{
	struct fwnode_handle *fwnode = NULL;
	struct phylink_config *config;
	struct wx *wx = txgbe->wx;
	phy_interface_t phy_mode;
	struct phylink *phylink;

	config = &wx->phylink_config;
	config->dev = &wx->netdev->dev;
	config->type = PHYLINK_NETDEV;
	config->mac_capabilities = MAC_10000FD | MAC_1000FD | MAC_100FD |
				   MAC_SYM_PAUSE | MAC_ASYM_PAUSE;

	if (wx->media_type == sp_media_copper) {
		phy_mode = PHY_INTERFACE_MODE_XAUI;
		__set_bit(PHY_INTERFACE_MODE_XAUI, config->supported_interfaces);
	} else {
		phy_mode = PHY_INTERFACE_MODE_10GBASER;
		fwnode = software_node_fwnode(txgbe->nodes.group[SWNODE_PHYLINK]);
		__set_bit(PHY_INTERFACE_MODE_10GBASER, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_1000BASEX, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_SGMII, config->supported_interfaces);
	}

	phylink = phylink_create(config, fwnode, phy_mode, &txgbe_mac_ops);
	if (IS_ERR(phylink))
		return PTR_ERR(phylink);

	if (wx->phydev) {
		int ret;

		ret = phylink_connect_phy(phylink, wx->phydev);
		if (ret) {
			phylink_destroy(phylink);
			return ret;
		}
	}

	wx->phylink = phylink;

	return 0;
}

irqreturn_t txgbe_link_irq_handler(int irq, void *data)
{
	struct txgbe *txgbe = data;
	struct wx *wx = txgbe->wx;
	u32 status;
	bool up;

	status = rd32(wx, TXGBE_CFG_PORT_ST);
	up = !!(status & TXGBE_CFG_PORT_ST_LINK_UP);

	phylink_pcs_change(&txgbe->xpcs->pcs, up);

	return IRQ_HANDLED;
}

static int txgbe_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct wx *wx = gpiochip_get_data(chip);
	int val;

	val = rd32m(wx, WX_GPIO_EXT, BIT(offset));

	return !!(val & BIT(offset));
}

static int txgbe_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct wx *wx = gpiochip_get_data(chip);
	u32 val;

	val = rd32(wx, WX_GPIO_DDR);
	if (BIT(offset) & val)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int txgbe_gpio_direction_in(struct gpio_chip *chip, unsigned int offset)
{
	struct wx *wx = gpiochip_get_data(chip);
	unsigned long flags;

	raw_spin_lock_irqsave(&wx->gpio_lock, flags);
	wr32m(wx, WX_GPIO_DDR, BIT(offset), 0);
	raw_spin_unlock_irqrestore(&wx->gpio_lock, flags);

	return 0;
}

static int txgbe_gpio_direction_out(struct gpio_chip *chip, unsigned int offset,
				    int val)
{
	struct wx *wx = gpiochip_get_data(chip);
	unsigned long flags;
	u32 set;

	set = val ? BIT(offset) : 0;

	raw_spin_lock_irqsave(&wx->gpio_lock, flags);
	wr32m(wx, WX_GPIO_DR, BIT(offset), set);
	wr32m(wx, WX_GPIO_DDR, BIT(offset), BIT(offset));
	raw_spin_unlock_irqrestore(&wx->gpio_lock, flags);

	return 0;
}

static void txgbe_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct wx *wx = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&wx->gpio_lock, flags);
	wr32(wx, WX_GPIO_EOI, BIT(hwirq));
	raw_spin_unlock_irqrestore(&wx->gpio_lock, flags);
}

static void txgbe_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct wx *wx = gpiochip_get_data(gc);
	unsigned long flags;

	gpiochip_disable_irq(gc, hwirq);

	raw_spin_lock_irqsave(&wx->gpio_lock, flags);
	wr32m(wx, WX_GPIO_INTMASK, BIT(hwirq), BIT(hwirq));
	raw_spin_unlock_irqrestore(&wx->gpio_lock, flags);
}

static void txgbe_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct wx *wx = gpiochip_get_data(gc);
	unsigned long flags;

	gpiochip_enable_irq(gc, hwirq);

	raw_spin_lock_irqsave(&wx->gpio_lock, flags);
	wr32m(wx, WX_GPIO_INTMASK, BIT(hwirq), 0);
	raw_spin_unlock_irqrestore(&wx->gpio_lock, flags);
}

static void txgbe_toggle_trigger(struct gpio_chip *gc, unsigned int offset)
{
	struct wx *wx = gpiochip_get_data(gc);
	u32 pol, val;

	pol = rd32(wx, WX_GPIO_POLARITY);
	val = rd32(wx, WX_GPIO_EXT);

	if (val & BIT(offset))
		pol &= ~BIT(offset);
	else
		pol |= BIT(offset);

	wr32(wx, WX_GPIO_POLARITY, pol);
}

static int txgbe_gpio_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct wx *wx = gpiochip_get_data(gc);
	u32 level, polarity, mask;
	unsigned long flags;

	mask = BIT(hwirq);

	if (type & IRQ_TYPE_LEVEL_MASK) {
		level = 0;
		irq_set_handler_locked(d, handle_level_irq);
	} else {
		level = mask;
		irq_set_handler_locked(d, handle_edge_irq);
	}

	if (type == IRQ_TYPE_EDGE_RISING || type == IRQ_TYPE_LEVEL_HIGH)
		polarity = mask;
	else
		polarity = 0;

	raw_spin_lock_irqsave(&wx->gpio_lock, flags);

	wr32m(wx, WX_GPIO_INTEN, mask, mask);
	wr32m(wx, WX_GPIO_INTTYPE_LEVEL, mask, level);
	if (type == IRQ_TYPE_EDGE_BOTH)
		txgbe_toggle_trigger(gc, hwirq);
	else
		wr32m(wx, WX_GPIO_POLARITY, mask, polarity);

	raw_spin_unlock_irqrestore(&wx->gpio_lock, flags);

	return 0;
}

static const struct irq_chip txgbe_gpio_irq_chip = {
	.name = "txgbe-gpio-irq",
	.irq_ack = txgbe_gpio_irq_ack,
	.irq_mask = txgbe_gpio_irq_mask,
	.irq_unmask = txgbe_gpio_irq_unmask,
	.irq_set_type = txgbe_gpio_set_type,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

irqreturn_t txgbe_gpio_irq_handler(int irq, void *data)
{
	struct txgbe *txgbe = data;
	struct wx *wx = txgbe->wx;
	irq_hw_number_t hwirq;
	unsigned long gpioirq;
	struct gpio_chip *gc;
	unsigned long flags;

	gpioirq = rd32(wx, WX_GPIO_INTSTATUS);

	gc = txgbe->gpio;
	for_each_set_bit(hwirq, &gpioirq, gc->ngpio) {
		int gpio = irq_find_mapping(gc->irq.domain, hwirq);
		struct irq_data *d = irq_get_irq_data(gpio);
		u32 irq_type = irq_get_trigger_type(gpio);

		txgbe_gpio_irq_ack(d);
		handle_nested_irq(gpio);

		if ((irq_type & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH) {
			raw_spin_lock_irqsave(&wx->gpio_lock, flags);
			txgbe_toggle_trigger(gc, hwirq);
			raw_spin_unlock_irqrestore(&wx->gpio_lock, flags);
		}
	}

	return IRQ_HANDLED;
}

void txgbe_reinit_gpio_intr(struct wx *wx)
{
	struct txgbe *txgbe = wx->priv;
	irq_hw_number_t hwirq;
	unsigned long gpioirq;
	struct gpio_chip *gc;
	unsigned long flags;

	/* for gpio interrupt pending before irq enable */
	gpioirq = rd32(wx, WX_GPIO_INTSTATUS);

	gc = txgbe->gpio;
	for_each_set_bit(hwirq, &gpioirq, gc->ngpio) {
		int gpio = irq_find_mapping(gc->irq.domain, hwirq);
		struct irq_data *d = irq_get_irq_data(gpio);
		u32 irq_type = irq_get_trigger_type(gpio);

		txgbe_gpio_irq_ack(d);

		if ((irq_type & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH) {
			raw_spin_lock_irqsave(&wx->gpio_lock, flags);
			txgbe_toggle_trigger(gc, hwirq);
			raw_spin_unlock_irqrestore(&wx->gpio_lock, flags);
		}
	}
}

static int txgbe_gpio_init(struct txgbe *txgbe)
{
	struct gpio_irq_chip *girq;
	struct gpio_chip *gc;
	struct device *dev;
	struct wx *wx;
	int ret;

	wx = txgbe->wx;
	dev = &wx->pdev->dev;

	raw_spin_lock_init(&wx->gpio_lock);

	gc = devm_kzalloc(dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	gc->label = devm_kasprintf(dev, GFP_KERNEL, "txgbe_gpio-%x",
				   pci_dev_id(wx->pdev));
	if (!gc->label)
		return -ENOMEM;

	gc->base = -1;
	gc->ngpio = 6;
	gc->owner = THIS_MODULE;
	gc->parent = dev;
	gc->fwnode = software_node_fwnode(txgbe->nodes.group[SWNODE_GPIO]);
	gc->get = txgbe_gpio_get;
	gc->get_direction = txgbe_gpio_get_direction;
	gc->direction_input = txgbe_gpio_direction_in;
	gc->direction_output = txgbe_gpio_direction_out;

	girq = &gc->irq;
	gpio_irq_chip_set_chip(girq, &txgbe_gpio_irq_chip);
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;

	ret = devm_gpiochip_add_data(dev, gc, wx);
	if (ret)
		return ret;

	txgbe->gpio = gc;

	return 0;
}

static int txgbe_clock_register(struct txgbe *txgbe)
{
	struct pci_dev *pdev = txgbe->wx->pdev;
	struct clk_lookup *clock;
	char clk_name[32];
	struct clk *clk;

	snprintf(clk_name, sizeof(clk_name), "i2c_designware.%d",
		 pci_dev_id(pdev));

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
	info.id = pci_dev_id(pdev);

	info.res = &DEFINE_RES_IRQ(pdev->irq);
	info.num_res = 1;
	i2c_dev = platform_device_register_full(&info);
	if (IS_ERR(i2c_dev))
		return PTR_ERR(i2c_dev);

	txgbe->i2c_dev = i2c_dev;

	return 0;
}

static int txgbe_sfp_register(struct txgbe *txgbe)
{
	struct pci_dev *pdev = txgbe->wx->pdev;
	struct platform_device_info info = {};
	struct platform_device *sfp_dev;

	info.parent = &pdev->dev;
	info.fwnode = software_node_fwnode(txgbe->nodes.group[SWNODE_SFP]);
	info.name = "sfp";
	info.id = pci_dev_id(pdev);
	sfp_dev = platform_device_register_full(&info);
	if (IS_ERR(sfp_dev))
		return PTR_ERR(sfp_dev);

	txgbe->sfp_dev = sfp_dev;

	return 0;
}

static int txgbe_ext_phy_init(struct txgbe *txgbe)
{
	struct phy_device *phydev;
	struct mii_bus *mii_bus;
	struct pci_dev *pdev;
	struct wx *wx;
	int ret = 0;

	wx = txgbe->wx;
	pdev = wx->pdev;

	mii_bus = devm_mdiobus_alloc(&pdev->dev);
	if (!mii_bus)
		return -ENOMEM;

	mii_bus->name = "txgbe_mii_bus";
	mii_bus->read_c45 = &wx_phy_read_reg_mdi_c45;
	mii_bus->write_c45 = &wx_phy_write_reg_mdi_c45;
	mii_bus->parent = &pdev->dev;
	mii_bus->phy_mask = GENMASK(31, 1);
	mii_bus->priv = wx;
	snprintf(mii_bus->id, MII_BUS_ID_SIZE, "txgbe-%x",
		 (pdev->bus->number << 8) | pdev->devfn);

	ret = devm_mdiobus_register(&pdev->dev, mii_bus);
	if (ret) {
		wx_err(wx, "failed to register MDIO bus: %d\n", ret);
		return ret;
	}

	phydev = phy_find_first(mii_bus);
	if (!phydev) {
		wx_err(wx, "no PHY found\n");
		return -ENODEV;
	}

	phy_attached_info(phydev);

	wx->link = 0;
	wx->speed = 0;
	wx->duplex = 0;
	wx->phydev = phydev;

	ret = txgbe_phylink_init(txgbe);
	if (ret) {
		wx_err(wx, "failed to init phylink: %d\n", ret);
		return ret;
	}

	return 0;
}

int txgbe_init_phy(struct txgbe *txgbe)
{
	struct wx *wx = txgbe->wx;
	int ret;

	if (txgbe->wx->media_type == sp_media_copper)
		return txgbe_ext_phy_init(txgbe);

	ret = txgbe_swnodes_register(txgbe);
	if (ret) {
		wx_err(wx, "failed to register software nodes\n");
		return ret;
	}

	ret = txgbe_mdio_pcs_init(txgbe);
	if (ret) {
		wx_err(wx, "failed to init mdio pcs: %d\n", ret);
		goto err_unregister_swnode;
	}

	ret = txgbe_phylink_init(txgbe);
	if (ret) {
		wx_err(wx, "failed to init phylink\n");
		goto err_destroy_xpcs;
	}

	ret = txgbe_gpio_init(txgbe);
	if (ret) {
		wx_err(wx, "failed to init gpio\n");
		goto err_destroy_phylink;
	}

	ret = txgbe_clock_register(txgbe);
	if (ret) {
		wx_err(wx, "failed to register clock: %d\n", ret);
		goto err_destroy_phylink;
	}

	ret = txgbe_i2c_register(txgbe);
	if (ret) {
		wx_err(wx, "failed to init i2c interface: %d\n", ret);
		goto err_unregister_clk;
	}

	ret = txgbe_sfp_register(txgbe);
	if (ret) {
		wx_err(wx, "failed to register sfp\n");
		goto err_unregister_i2c;
	}

	return 0;

err_unregister_i2c:
	platform_device_unregister(txgbe->i2c_dev);
err_unregister_clk:
	clkdev_drop(txgbe->clock);
	clk_unregister(txgbe->clk);
err_destroy_phylink:
	phylink_destroy(wx->phylink);
err_destroy_xpcs:
	xpcs_destroy(txgbe->xpcs);
err_unregister_swnode:
	software_node_unregister_node_group(txgbe->nodes.group);

	return ret;
}

void txgbe_remove_phy(struct txgbe *txgbe)
{
	if (txgbe->wx->media_type == sp_media_copper) {
		phylink_disconnect_phy(txgbe->wx->phylink);
		phylink_destroy(txgbe->wx->phylink);
		return;
	}

	platform_device_unregister(txgbe->sfp_dev);
	platform_device_unregister(txgbe->i2c_dev);
	clkdev_drop(txgbe->clock);
	clk_unregister(txgbe->clk);
	phylink_destroy(txgbe->wx->phylink);
	xpcs_destroy(txgbe->xpcs);
	software_node_unregister_node_group(txgbe->nodes.group);
}
