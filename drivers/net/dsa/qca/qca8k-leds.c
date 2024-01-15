// SPDX-License-Identifier: GPL-2.0
#include <linux/property.h>
#include <linux/regmap.h>
#include <net/dsa.h>

#include "qca8k.h"
#include "qca8k_leds.h"

static u32 qca8k_phy_to_port(int phy)
{
	/* Internal PHY 0 has port at index 1.
	 * Internal PHY 1 has port at index 2.
	 * Internal PHY 2 has port at index 3.
	 * Internal PHY 3 has port at index 4.
	 * Internal PHY 4 has port at index 5.
	 */

	return phy + 1;
}

static int
qca8k_get_enable_led_reg(int port_num, int led_num, struct qca8k_led_pattern_en *reg_info)
{
	switch (port_num) {
	case 0:
		reg_info->reg = QCA8K_LED_CTRL_REG(led_num);
		reg_info->shift = QCA8K_LED_PHY0123_CONTROL_RULE_SHIFT;
		break;
	case 1:
	case 2:
	case 3:
		/* Port 123 are controlled on a different reg */
		reg_info->reg = QCA8K_LED_CTRL3_REG;
		reg_info->shift = QCA8K_LED_PHY123_PATTERN_EN_SHIFT(port_num, led_num);
		break;
	case 4:
		reg_info->reg = QCA8K_LED_CTRL_REG(led_num);
		reg_info->shift = QCA8K_LED_PHY4_CONTROL_RULE_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
qca8k_get_control_led_reg(int port_num, int led_num, struct qca8k_led_pattern_en *reg_info)
{
	reg_info->reg = QCA8K_LED_CTRL_REG(led_num);

	/* 6 total control rule:
	 * 3 control rules for phy0-3 that applies to all their leds
	 * 3 control rules for phy4
	 */
	if (port_num == 4)
		reg_info->shift = QCA8K_LED_PHY4_CONTROL_RULE_SHIFT;
	else
		reg_info->shift = QCA8K_LED_PHY0123_CONTROL_RULE_SHIFT;

	return 0;
}

static int
qca8k_parse_netdev(unsigned long rules, u32 *offload_trigger)
{
	/* Parsing specific to netdev trigger */
	if (test_bit(TRIGGER_NETDEV_TX, &rules))
		*offload_trigger |= QCA8K_LED_TX_BLINK_MASK;
	if (test_bit(TRIGGER_NETDEV_RX, &rules))
		*offload_trigger |= QCA8K_LED_RX_BLINK_MASK;
	if (test_bit(TRIGGER_NETDEV_LINK_10, &rules))
		*offload_trigger |= QCA8K_LED_LINK_10M_EN_MASK;
	if (test_bit(TRIGGER_NETDEV_LINK_100, &rules))
		*offload_trigger |= QCA8K_LED_LINK_100M_EN_MASK;
	if (test_bit(TRIGGER_NETDEV_LINK_1000, &rules))
		*offload_trigger |= QCA8K_LED_LINK_1000M_EN_MASK;
	if (test_bit(TRIGGER_NETDEV_HALF_DUPLEX, &rules))
		*offload_trigger |= QCA8K_LED_HALF_DUPLEX_MASK;
	if (test_bit(TRIGGER_NETDEV_FULL_DUPLEX, &rules))
		*offload_trigger |= QCA8K_LED_FULL_DUPLEX_MASK;

	if (rules && !*offload_trigger)
		return -EOPNOTSUPP;

	/* Enable some default rule by default to the requested mode:
	 * - Blink at 4Hz by default
	 */
	*offload_trigger |= QCA8K_LED_BLINK_4HZ;

	return 0;
}

static int
qca8k_led_brightness_set(struct qca8k_led *led,
			 enum led_brightness brightness)
{
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;
	u32 mask, val;

	qca8k_get_enable_led_reg(led->port_num, led->led_num, &reg_info);

	val = QCA8K_LED_ALWAYS_OFF;
	if (brightness)
		val = QCA8K_LED_ALWAYS_ON;

	/* HW regs to control brightness is special and port 1-2-3
	 * are placed in a different reg.
	 *
	 * To control port 0 brightness:
	 * - the 2 bit (15, 14) of:
	 *   - QCA8K_LED_CTRL0_REG for led1
	 *   - QCA8K_LED_CTRL1_REG for led2
	 *   - QCA8K_LED_CTRL2_REG for led3
	 *
	 * To control port 4:
	 * - the 2 bit (31, 30) of:
	 *   - QCA8K_LED_CTRL0_REG for led1
	 *   - QCA8K_LED_CTRL1_REG for led2
	 *   - QCA8K_LED_CTRL2_REG for led3
	 *
	 * To control port 1:
	 *   - the 2 bit at (9, 8) of QCA8K_LED_CTRL3_REG are used for led1
	 *   - the 2 bit at (11, 10) of QCA8K_LED_CTRL3_REG are used for led2
	 *   - the 2 bit at (13, 12) of QCA8K_LED_CTRL3_REG are used for led3
	 *
	 * To control port 2:
	 *   - the 2 bit at (15, 14) of QCA8K_LED_CTRL3_REG are used for led1
	 *   - the 2 bit at (17, 16) of QCA8K_LED_CTRL3_REG are used for led2
	 *   - the 2 bit at (19, 18) of QCA8K_LED_CTRL3_REG are used for led3
	 *
	 * To control port 3:
	 *   - the 2 bit at (21, 20) of QCA8K_LED_CTRL3_REG are used for led1
	 *   - the 2 bit at (23, 22) of QCA8K_LED_CTRL3_REG are used for led2
	 *   - the 2 bit at (25, 24) of QCA8K_LED_CTRL3_REG are used for led3
	 *
	 * To abstract this and have less code, we use the port and led numm
	 * to calculate the shift and the correct reg due to this problem of
	 * not having a 1:1 map of LED with the regs.
	 */
	if (led->port_num == 0 || led->port_num == 4) {
		mask = QCA8K_LED_PATTERN_EN_MASK;
		val <<= QCA8K_LED_PATTERN_EN_SHIFT;
	} else {
		mask = QCA8K_LED_PHY123_PATTERN_EN_MASK;
	}

	return regmap_update_bits(priv->regmap, reg_info.reg,
				  mask << reg_info.shift,
				  val << reg_info.shift);
}

static int
qca8k_cled_brightness_set_blocking(struct led_classdev *ldev,
				   enum led_brightness brightness)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);

	return qca8k_led_brightness_set(led, brightness);
}

static enum led_brightness
qca8k_led_brightness_get(struct qca8k_led *led)
{
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;
	u32 val;
	int ret;

	qca8k_get_enable_led_reg(led->port_num, led->led_num, &reg_info);

	ret = regmap_read(priv->regmap, reg_info.reg, &val);
	if (ret)
		return 0;

	val >>= reg_info.shift;

	if (led->port_num == 0 || led->port_num == 4) {
		val &= QCA8K_LED_PATTERN_EN_MASK;
		val >>= QCA8K_LED_PATTERN_EN_SHIFT;
	} else {
		val &= QCA8K_LED_PHY123_PATTERN_EN_MASK;
	}

	/* Assume brightness ON only when the LED is set to always ON */
	return val == QCA8K_LED_ALWAYS_ON;
}

static int
qca8k_cled_blink_set(struct led_classdev *ldev,
		     unsigned long *delay_on,
		     unsigned long *delay_off)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);
	u32 mask, val = QCA8K_LED_ALWAYS_BLINK_4HZ;
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;

	if (*delay_on == 0 && *delay_off == 0) {
		*delay_on = 125;
		*delay_off = 125;
	}

	if (*delay_on != 125 || *delay_off != 125) {
		/* The hardware only supports blinking at 4Hz. Fall back
		 * to software implementation in other cases.
		 */
		return -EINVAL;
	}

	qca8k_get_enable_led_reg(led->port_num, led->led_num, &reg_info);

	if (led->port_num == 0 || led->port_num == 4) {
		mask = QCA8K_LED_PATTERN_EN_MASK;
		val <<= QCA8K_LED_PATTERN_EN_SHIFT;
	} else {
		mask = QCA8K_LED_PHY123_PATTERN_EN_MASK;
	}

	regmap_update_bits(priv->regmap, reg_info.reg, mask << reg_info.shift,
			   val << reg_info.shift);

	return 0;
}

static int
qca8k_cled_trigger_offload(struct led_classdev *ldev, bool enable)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);

	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;
	u32 mask, val = QCA8K_LED_ALWAYS_OFF;

	qca8k_get_enable_led_reg(led->port_num, led->led_num, &reg_info);

	if (enable)
		val = QCA8K_LED_RULE_CONTROLLED;

	if (led->port_num == 0 || led->port_num == 4) {
		mask = QCA8K_LED_PATTERN_EN_MASK;
		val <<= QCA8K_LED_PATTERN_EN_SHIFT;
	} else {
		mask = QCA8K_LED_PHY123_PATTERN_EN_MASK;
	}

	return regmap_update_bits(priv->regmap, reg_info.reg, mask << reg_info.shift,
				  val << reg_info.shift);
}

static bool
qca8k_cled_hw_control_status(struct led_classdev *ldev)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);

	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;
	u32 val;

	qca8k_get_enable_led_reg(led->port_num, led->led_num, &reg_info);

	regmap_read(priv->regmap, reg_info.reg, &val);

	val >>= reg_info.shift;

	if (led->port_num == 0 || led->port_num == 4) {
		val &= QCA8K_LED_PATTERN_EN_MASK;
		val >>= QCA8K_LED_PATTERN_EN_SHIFT;
	} else {
		val &= QCA8K_LED_PHY123_PATTERN_EN_MASK;
	}

	return val == QCA8K_LED_RULE_CONTROLLED;
}

static int
qca8k_cled_hw_control_is_supported(struct led_classdev *ldev, unsigned long rules)
{
	u32 offload_trigger = 0;

	return qca8k_parse_netdev(rules, &offload_trigger);
}

static int
qca8k_cled_hw_control_set(struct led_classdev *ldev, unsigned long rules)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;
	u32 offload_trigger = 0;
	int ret;

	ret = qca8k_parse_netdev(rules, &offload_trigger);
	if (ret)
		return ret;

	ret = qca8k_cled_trigger_offload(ldev, true);
	if (ret)
		return ret;

	qca8k_get_control_led_reg(led->port_num, led->led_num, &reg_info);

	return regmap_update_bits(priv->regmap, reg_info.reg,
				  QCA8K_LED_RULE_MASK << reg_info.shift,
				  offload_trigger << reg_info.shift);
}

static int
qca8k_cled_hw_control_get(struct led_classdev *ldev, unsigned long *rules)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;
	u32 val;
	int ret;

	/* With hw control not active return err */
	if (!qca8k_cled_hw_control_status(ldev))
		return -EINVAL;

	qca8k_get_control_led_reg(led->port_num, led->led_num, &reg_info);

	ret = regmap_read(priv->regmap, reg_info.reg, &val);
	if (ret)
		return ret;

	val >>= reg_info.shift;
	val &= QCA8K_LED_RULE_MASK;

	/* Parsing specific to netdev trigger */
	if (val & QCA8K_LED_TX_BLINK_MASK)
		set_bit(TRIGGER_NETDEV_TX, rules);
	if (val & QCA8K_LED_RX_BLINK_MASK)
		set_bit(TRIGGER_NETDEV_RX, rules);
	if (val & QCA8K_LED_LINK_10M_EN_MASK)
		set_bit(TRIGGER_NETDEV_LINK_10, rules);
	if (val & QCA8K_LED_LINK_100M_EN_MASK)
		set_bit(TRIGGER_NETDEV_LINK_100, rules);
	if (val & QCA8K_LED_LINK_1000M_EN_MASK)
		set_bit(TRIGGER_NETDEV_LINK_1000, rules);
	if (val & QCA8K_LED_HALF_DUPLEX_MASK)
		set_bit(TRIGGER_NETDEV_HALF_DUPLEX, rules);
	if (val & QCA8K_LED_FULL_DUPLEX_MASK)
		set_bit(TRIGGER_NETDEV_FULL_DUPLEX, rules);

	return 0;
}

static struct device *qca8k_cled_hw_control_get_device(struct led_classdev *ldev)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);
	struct qca8k_priv *priv = led->priv;
	struct dsa_port *dp;

	dp = dsa_to_port(priv->ds, qca8k_phy_to_port(led->port_num));
	if (!dp)
		return NULL;
	if (dp->user)
		return &dp->user->dev;
	return NULL;
}

static int
qca8k_parse_port_leds(struct qca8k_priv *priv, struct fwnode_handle *port, int port_num)
{
	struct fwnode_handle *led = NULL, *leds = NULL;
	struct led_init_data init_data = { };
	struct dsa_switch *ds = priv->ds;
	enum led_default_state state;
	struct qca8k_led *port_led;
	int led_num, led_index;
	int ret;

	leds = fwnode_get_named_child_node(port, "leds");
	if (!leds) {
		dev_dbg(priv->dev, "No Leds node specified in device tree for port %d!\n",
			port_num);
		return 0;
	}

	fwnode_for_each_child_node(leds, led) {
		/* Reg represent the led number of the port.
		 * Each port can have at most 3 leds attached
		 * Commonly:
		 * 1. is gigabit led
		 * 2. is mbit led
		 * 3. additional status led
		 */
		if (fwnode_property_read_u32(led, "reg", &led_num))
			continue;

		if (led_num >= QCA8K_LED_PORT_COUNT) {
			dev_warn(priv->dev, "Invalid LED reg %d defined for port %d",
				 led_num, port_num);
			continue;
		}

		led_index = QCA8K_LED_PORT_INDEX(port_num, led_num);

		port_led = &priv->ports_led[led_index];
		port_led->port_num = port_num;
		port_led->led_num = led_num;
		port_led->priv = priv;

		state = led_init_default_state_get(led);
		switch (state) {
		case LEDS_DEFSTATE_ON:
			port_led->cdev.brightness = 1;
			qca8k_led_brightness_set(port_led, 1);
			break;
		case LEDS_DEFSTATE_KEEP:
			port_led->cdev.brightness =
					qca8k_led_brightness_get(port_led);
			break;
		default:
			port_led->cdev.brightness = 0;
			qca8k_led_brightness_set(port_led, 0);
		}

		port_led->cdev.max_brightness = 1;
		port_led->cdev.brightness_set_blocking = qca8k_cled_brightness_set_blocking;
		port_led->cdev.blink_set = qca8k_cled_blink_set;
		port_led->cdev.hw_control_is_supported = qca8k_cled_hw_control_is_supported;
		port_led->cdev.hw_control_set = qca8k_cled_hw_control_set;
		port_led->cdev.hw_control_get = qca8k_cled_hw_control_get;
		port_led->cdev.hw_control_get_device = qca8k_cled_hw_control_get_device;
		port_led->cdev.hw_control_trigger = "netdev";
		init_data.default_label = ":port";
		init_data.fwnode = led;
		init_data.devname_mandatory = true;
		init_data.devicename = kasprintf(GFP_KERNEL, "%s:0%d", ds->user_mii_bus->id,
						 port_num);
		if (!init_data.devicename)
			return -ENOMEM;

		ret = devm_led_classdev_register_ext(priv->dev, &port_led->cdev, &init_data);
		if (ret)
			dev_warn(priv->dev, "Failed to init LED %d for port %d", led_num, port_num);

		kfree(init_data.devicename);
	}

	return 0;
}

int
qca8k_setup_led_ctrl(struct qca8k_priv *priv)
{
	struct fwnode_handle *ports, *port;
	int port_num;
	int ret;

	ports = device_get_named_child_node(priv->dev, "ports");
	if (!ports) {
		dev_info(priv->dev, "No ports node specified in device tree!");
		return 0;
	}

	fwnode_for_each_child_node(ports, port) {
		if (fwnode_property_read_u32(port, "reg", &port_num))
			continue;

		/* Skip checking for CPU port 0 and CPU port 6 as not supported */
		if (port_num == 0 || port_num == 6)
			continue;

		/* Each port can have at most 3 different leds attached.
		 * Switch port starts from 0 to 6, but port 0 and 6 are CPU
		 * port. The port index needs to be decreased by one to identify
		 * the correct port for LED setup.
		 */
		ret = qca8k_parse_port_leds(priv, port, qca8k_port_to_phy(port_num));
		if (ret)
			return ret;
	}

	return 0;
}
