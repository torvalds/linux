// SPDX-License-Identifier: GPL-2.0

#include <linux/bitops.h>
#include <linux/regmap.h>
#include <net/dsa.h>
#include "rtl83xx.h"
#include "rtl8366rb.h"

static inline u32 rtl8366rb_led_group_port_mask(u8 led_group, u8 port)
{
	switch (led_group) {
	case 0:
		return FIELD_PREP(RTL8366RB_LED_0_X_CTRL_MASK, BIT(port));
	case 1:
		return FIELD_PREP(RTL8366RB_LED_0_X_CTRL_MASK, BIT(port));
	case 2:
		return FIELD_PREP(RTL8366RB_LED_0_X_CTRL_MASK, BIT(port));
	case 3:
		return FIELD_PREP(RTL8366RB_LED_0_X_CTRL_MASK, BIT(port));
	default:
		return 0;
	}
}

static int rb8366rb_get_port_led(struct rtl8366rb_led *led)
{
	struct realtek_priv *priv = led->priv;
	u8 led_group = led->led_group;
	u8 port_num = led->port_num;
	int ret;
	u32 val;

	ret = regmap_read(priv->map, RTL8366RB_LED_X_X_CTRL_REG(led_group),
			  &val);
	if (ret) {
		dev_err(priv->dev, "error reading LED on port %d group %d\n",
			led_group, port_num);
		return ret;
	}

	return !!(val & rtl8366rb_led_group_port_mask(led_group, port_num));
}

static int rb8366rb_set_port_led(struct rtl8366rb_led *led, bool enable)
{
	struct realtek_priv *priv = led->priv;
	u8 led_group = led->led_group;
	u8 port_num = led->port_num;
	int ret;

	ret = regmap_update_bits(priv->map,
				 RTL8366RB_LED_X_X_CTRL_REG(led_group),
				 rtl8366rb_led_group_port_mask(led_group,
							       port_num),
				 enable ? 0xffff : 0);
	if (ret) {
		dev_err(priv->dev, "error updating LED on port %d group %d\n",
			led_group, port_num);
		return ret;
	}

	/* Change the LED group to manual controlled LEDs if required */
	ret = rb8366rb_set_ledgroup_mode(priv, led_group,
					 RTL8366RB_LEDGROUP_FORCE);

	if (ret) {
		dev_err(priv->dev, "error updating LED GROUP group %d\n",
			led_group);
		return ret;
	}

	return 0;
}

static int
rtl8366rb_cled_brightness_set_blocking(struct led_classdev *ldev,
				       enum led_brightness brightness)
{
	struct rtl8366rb_led *led = container_of(ldev, struct rtl8366rb_led,
						 cdev);

	return rb8366rb_set_port_led(led, brightness == LED_ON);
}

static int rtl8366rb_setup_led(struct realtek_priv *priv, struct dsa_port *dp,
			       struct fwnode_handle *led_fwnode)
{
	struct rtl8366rb *rb = priv->chip_data;
	struct led_init_data init_data = { };
	enum led_default_state state;
	struct rtl8366rb_led *led;
	u32 led_group;
	int ret;

	ret = fwnode_property_read_u32(led_fwnode, "reg", &led_group);
	if (ret)
		return ret;

	if (led_group >= RTL8366RB_NUM_LEDGROUPS) {
		dev_warn(priv->dev, "Invalid LED reg %d defined for port %d",
			 led_group, dp->index);
		return -EINVAL;
	}

	led = &rb->leds[dp->index][led_group];
	led->port_num = dp->index;
	led->led_group = led_group;
	led->priv = priv;

	state = led_init_default_state_get(led_fwnode);
	switch (state) {
	case LEDS_DEFSTATE_ON:
		led->cdev.brightness = 1;
		rb8366rb_set_port_led(led, 1);
		break;
	case LEDS_DEFSTATE_KEEP:
		led->cdev.brightness =
			rb8366rb_get_port_led(led);
		break;
	case LEDS_DEFSTATE_OFF:
	default:
		led->cdev.brightness = 0;
		rb8366rb_set_port_led(led, 0);
	}

	led->cdev.max_brightness = 1;
	led->cdev.brightness_set_blocking =
		rtl8366rb_cled_brightness_set_blocking;
	init_data.fwnode = led_fwnode;
	init_data.devname_mandatory = true;

	init_data.devicename = kasprintf(GFP_KERNEL, "Realtek-%d:0%d:%d",
					 dp->ds->index, dp->index, led_group);
	if (!init_data.devicename)
		return -ENOMEM;

	ret = devm_led_classdev_register_ext(priv->dev, &led->cdev, &init_data);
	if (ret) {
		dev_warn(priv->dev, "Failed to init LED %d for port %d",
			 led_group, dp->index);
		return ret;
	}

	return 0;
}

int rtl8366rb_setup_leds(struct realtek_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;
	struct device_node *leds_np;
	struct dsa_port *dp;
	int ret = 0;

	dsa_switch_for_each_port(dp, ds) {
		if (!dp->dn)
			continue;

		leds_np = of_get_child_by_name(dp->dn, "leds");
		if (!leds_np) {
			dev_dbg(priv->dev, "No leds defined for port %d",
				dp->index);
			continue;
		}

		for_each_child_of_node_scoped(leds_np, led_np) {
			ret = rtl8366rb_setup_led(priv, dp,
						  of_fwnode_handle(led_np));
			if (ret)
				break;
		}

		of_node_put(leds_np);
		if (ret)
			return ret;
	}
	return 0;
}
