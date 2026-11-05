// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 David Yang
 */

#include <linux/uleds.h>

#include "chip.h"
#include "leds.h"
#include "smi.h"

#define to_yt921x_led(led_cdev) \
	container_of_const((led_cdev), struct yt921x_led, cdev)
#define to_yt921x_port(led) ((led)->port)

static u32 yt921x_led_regaddr(struct yt921x_priv *priv, int port, int group)
{
	switch (group) {
	case 0:
	default:
		return YT921X_LED0_PORTn(port);
	case 1:
		return YT921X_LED1_PORTn(port);
	case 2:
		return YT921X_LED2_PORTn(port);
	}
}

static int
yt921x_led_force_set(struct yt921x_priv *priv, int port, int group, bool on)
{
	struct yt921x_port *pp = &priv->ports[port];
	u32 ctrl;
	u32 mask;

	/* No reversion - LED is sacrificed in case of IO error */
	pp->led_duty_mask &= ~BIT(group);
	pp->led_cycle_mask &= ~BIT(group);

	mask = YT921X_LED2_PORT_FORCEn_M(group);
	ctrl = on ? YT921X_LED2_PORT_FORCEn_ON(group) :
	       YT921X_LED2_PORT_FORCEn_OFF(group);
	return yt921x_reg_update_bits(priv, YT921X_LED2_PORTn(port), mask,
				      ctrl);
}

static int
yt921x_led_blink_select(const struct yt921x_priv *priv, unsigned long on,
			unsigned long off, unsigned short *cyclep,
			unsigned char *dutyp)
{
	static const unsigned char dutys[] = {
		YT921X_LED_DUTY(1, 6),
		YT921X_LED_DUTY(1, 4),
		YT921X_LED_DUTY(1, 3),
		YT921X_LED_DUTY(1, 2),
	};
	unsigned int cycle_upper;
	unsigned int cycle_req;
	unsigned int duty_req;
	unsigned int cycle;
	unsigned int duty;

	cycle = YT921X_LED_BLINK_MAX;
	cycle_upper = cycle * 11585 / 8192 + 1;  /* M_SQRT2 * cycle */
	if (check_add_overflow(on, off, &cycle_req) || cycle_req >= cycle_upper)
		return -EOPNOTSUPP;

	for (; cycle > YT921X_LED_BLINK_MIN; cycle_upper >>= 1, cycle >>= 1)
		if (cycle_req >= cycle_upper >> 1)
			break;
	*cyclep = cycle;

	duty_req = DIV_ROUND_CLOSEST(YT921X_LED_DUTY_DENOM *
				     (on > off ? off : on), cycle_req);
	for (unsigned int i = ARRAY_SIZE(dutys) - 1;; i--)
		if (i == 0 || duty_req >= (dutys[i - 1] + dutys[i]) / 2) {
			duty = dutys[i];
			break;
		}
	if (on > off)
		duty = YT921X_LED_DUTY_DENOM - duty;
	*dutyp = duty;

	return 0;
}

static int
yt921x_led_blink_set(struct yt921x_priv *priv, int port, int group,
		     unsigned long *onp, unsigned long *offp)
{
	struct yt921x_port *pp = &priv->ports[port];
	unsigned short cycle;
	unsigned char duty;
	bool use_cycle;
	u32 ctrl;
	u32 mask;
	u32 val;
	int res;

	if (!*onp && !*offp) {
		cycle = YT921X_LED_BLINK_DEF;
		duty = (pp->led_duty_mask & ~BIT(group)) ? pp->led_duty :
		       YT921X_LED_DUTY(1, 2);

		use_cycle = false;
	} else if (!*onp || !*offp) {
		return yt921x_led_force_set(priv, port, group, *onp);
	} else {
		res = yt921x_led_blink_select(priv, *onp, *offp, &cycle, &duty);
		if (res)
			return res;

		use_cycle = cycle < YT921X_LED_BLINK_DEF;

		if (use_cycle && cycle != pp->led_cycle &&
		    (pp->led_cycle_mask & ~BIT(group)))
			return -EOPNOTSUPP;
		if (duty != pp->led_duty && (pp->led_duty_mask & ~BIT(group)))
			return -EOPNOTSUPP;
	}

	pp->led_cycle_mask &= ~BIT(group);
	pp->led_duty_mask &= ~BIT(group);

	/* The chip seems to jam a while if changing duty directly */
	res = yt921x_reg_read(priv, YT921X_LED2_PORTn(port), &val);
	if (res)
		return res;

	ctrl = val & ~YT921X_LED2_PORT_FORCEn_M(group);
	ctrl |= YT921X_LED2_PORT_FORCEn_OFF(group);
	if (val != ctrl) {
		res = yt921x_reg_write(priv, YT921X_LED2_PORTn(port), ctrl);
		if (res)
			return res;
	}

	mask = YT921X_LED1_PORT_BLINK_DUTY_M | YT921X_LED1_PORT_BLINK_DUTY_COMP;
	switch (duty >= YT921X_LED_DUTY(1, 2) ? duty :
		YT921X_LED_DUTY_DENOM - duty) {
	default:
		duty = YT921X_LED_DUTY(1, 2);
		fallthrough;
	case YT921X_LED_DUTY(1, 2):
		ctrl = YT921X_LED1_PORT_BLINK_DUTY_1_2;
		break;
	case YT921X_LED_DUTY(2, 3):
		ctrl = YT921X_LED1_PORT_BLINK_DUTY_2_3;
		break;
	case YT921X_LED_DUTY(3, 4):
		ctrl = YT921X_LED1_PORT_BLINK_DUTY_3_4;
		break;
	case YT921X_LED_DUTY(5, 6):
		ctrl = YT921X_LED1_PORT_BLINK_DUTY_5_6;
		break;
	}
	if (duty < YT921X_LED_DUTY(1, 2))
		ctrl |= YT921X_LED1_PORT_BLINK_DUTY_COMP;
	if (use_cycle) {
		mask |= YT921X_LED1_PORT_OTHER_BLINK_M;
		ctrl |= YT921X_LED1_PORT_OTHER_BLINK(9 - __fls(cycle));
	}
	res = yt921x_reg_update_bits(priv, YT921X_LED1_PORTn(port), mask, ctrl);
	if (res)
		return res;

	ctrl = val & ~(YT921X_LED2_PORT_FORCEn_M(group) |
		       YT921X_LED2_PORT_FORCE_BLINKn_M(group));
	ctrl |= YT921X_LED2_PORT_FORCEn_BLINK(group);
	if (use_cycle)
		ctrl |= YT921X_LED2_PORT_FORCE_BLINKn_OTHER(group);
	else
		ctrl |= YT921X_LED2_PORT_FORCE_BLINKn(group, __fls(cycle) - 9);
	res = yt921x_reg_write(priv, YT921X_LED2_PORTn(port), ctrl);
	if (res)
		return res;

	if (use_cycle) {
		pp->led_cycle_mask |= BIT(group);
		pp->led_cycle = cycle;
	}
	pp->led_duty_mask |= BIT(group);
	pp->led_duty = duty;

	*onp = DIV_ROUND_CLOSEST(duty * cycle, YT921X_LED_DUTY_DENOM);
	*offp = cycle - *onp;
	return 0;
}

struct yt921x_led_trigger_map {
	unsigned long flags;
	u32 mask;
};

static const struct yt921x_led_trigger_map yt921x_led_trigger_maps[] = {
	{BIT(TRIGGER_NETDEV_LINK),
	 YT921X_LEDx_PORT_ACT_DUPLEX_HALF | YT921X_LEDx_PORT_ACT_DUPLEX_FULL},
	{BIT(TRIGGER_NETDEV_LINK_10), YT921X_LEDx_PORT_ACT_10M},
	{BIT(TRIGGER_NETDEV_LINK_100), YT921X_LEDx_PORT_ACT_100M},
	{BIT(TRIGGER_NETDEV_LINK_1000), YT921X_LEDx_PORT_ACT_1000M},
	{BIT(TRIGGER_NETDEV_HALF_DUPLEX), YT921X_LEDx_PORT_ACT_DUPLEX_HALF},
	{BIT(TRIGGER_NETDEV_FULL_DUPLEX), YT921X_LEDx_PORT_ACT_DUPLEX_FULL},
	{BIT(TRIGGER_NETDEV_TX), YT921X_LEDx_PORT_ACT_TX_BLINK},
	{BIT(TRIGGER_NETDEV_RX), YT921X_LEDx_PORT_ACT_RX_BLINK},
	{BIT(TRIGGER_NETDEV_TX_ERR) | BIT(TRIGGER_NETDEV_RX_ERR),
	 YT921X_LEDx_PORT_ACT_COLLISION_BLINK},
};

static bool
yt921x_led_trigger_is_supported(const struct yt921x_priv *priv, int port,
				int group, unsigned long flags)
{
	if (!flags)
		return true;

	for (unsigned int i = 0; i < ARRAY_SIZE(yt921x_led_trigger_maps); i++) {
		const struct yt921x_led_trigger_map *map = &yt921x_led_trigger_maps[i];

		if ((flags & map->flags) == map->flags) {
			flags &= ~map->flags;
			if (!flags)
				return true;
		}
	}

	return false;
}

static int
yt921x_led_trigger_get(struct yt921x_priv *priv, int port, int group,
		       unsigned long *flagsp)
{
	u32 addr;
	u32 val;
	int res;

	res = yt921x_reg_read(priv, YT921X_LED2_PORTn(port), &val);
	if (res)
		return res;

	if ((val & YT921X_LED2_PORT_FORCEn_M(group)) !=
	    YT921X_LED2_PORT_FORCEn_DONTCARE(group)) {
		*flagsp = 0;
		return 0;
	}

	if (group != 2) {
		addr = yt921x_led_regaddr(priv, port, group);
		res = yt921x_reg_read(priv, addr, &val);
		if (res)
			return res;
	}

	*flagsp = 0;
	for (unsigned int i = 0; i < ARRAY_SIZE(yt921x_led_trigger_maps); i++) {
		const struct yt921x_led_trigger_map *map = &yt921x_led_trigger_maps[i];

		if ((val & map->mask) == map->mask)
			*flagsp |= map->flags;
	}

	return 0;
}

static int
yt921x_led_trigger_set(struct yt921x_priv *priv, int port, int group,
		       unsigned long flags)
{
	struct yt921x_port *pp = &priv->ports[port];
	u32 addr;
	u32 ctrl;
	u32 mask;
	int res;

	ctrl = 0;
	for (unsigned int i = 0; i < ARRAY_SIZE(yt921x_led_trigger_maps); i++) {
		const struct yt921x_led_trigger_map *map = &yt921x_led_trigger_maps[i];

		if ((flags & map->flags) == map->flags) {
			flags &= ~map->flags;
			ctrl |= map->mask;
			if (!flags)
				break;
		}
	}
	if (flags)
		return -EOPNOTSUPP;

	pp->led_duty_mask &= ~BIT(group);
	pp->led_cycle_mask &= ~BIT(group);

	mask = !group ? YT921X_LED0_PORT_ACT_M : YT921X_LEDx_PORT_ACT_M;
	if (group == 2) {
		mask |= YT921X_LED2_PORT_FORCEn_M(group);
		ctrl |= YT921X_LED2_PORT_FORCEn_DONTCARE(group);
	}
	addr = yt921x_led_regaddr(priv, port, group);
	res = yt921x_reg_update_bits(priv, addr, mask, ctrl);
	if (res)
		return res;

	if (group != 2) {
		mask = YT921X_LED2_PORT_FORCEn_M(group);
		ctrl = YT921X_LED2_PORT_FORCEn_DONTCARE(group);
		res = yt921x_reg_update_bits(priv, YT921X_LED2_PORTn(port),
					     mask, ctrl);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_cled_brightness_set_blocking(struct led_classdev *led_cdev,
				    enum led_brightness brightness)
{
	struct yt921x_led *led = to_yt921x_led(led_cdev);
	struct yt921x_port *pp = to_yt921x_port(led);
	struct yt921x_priv *priv = yt921x_port_to_priv(pp);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_led_force_set(priv, pp->index, led->group, brightness);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_cled_blink_set(struct led_classdev *led_cdev, unsigned long *delay_on,
		      unsigned long *delay_off)
{
	struct yt921x_led *led = to_yt921x_led(led_cdev);
	struct yt921x_port *pp = to_yt921x_port(led);
	struct yt921x_priv *priv = yt921x_port_to_priv(pp);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_led_blink_set(priv, pp->index, led->group, delay_on,
				   delay_off);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static struct device __maybe_unused *
yt921x_cled_hw_control_get_device(struct led_classdev *led_cdev)
{
	struct yt921x_led *led = to_yt921x_led(led_cdev);
	struct yt921x_port *pp = to_yt921x_port(led);
	struct yt921x_priv *priv = yt921x_port_to_priv(pp);
	struct dsa_port *dp;

	dp = dsa_to_port(&priv->ds, pp->index);
	if (!dp)
		return NULL;

	if (dsa_port_is_user(dp))
		return !dp->user ? NULL : &dp->user->dev;
	if (dsa_port_is_cpu(dp))
		return !dp->conduit ? NULL : &dp->conduit->dev;

	return NULL;
}

static int __maybe_unused
yt921x_cled_hw_control_is_supported(struct led_classdev *led_cdev,
				    unsigned long flags)
{
	struct yt921x_led *led = to_yt921x_led(led_cdev);
	struct yt921x_port *pp = to_yt921x_port(led);
	struct yt921x_priv *priv = yt921x_port_to_priv(pp);

	if (yt921x_led_trigger_is_supported(priv, pp->index, led->group, flags))
		return 0;
	return -EOPNOTSUPP;
}

static int __maybe_unused
yt921x_cled_hw_control_get(struct led_classdev *led_cdev, unsigned long *flagsp)
{
	struct yt921x_led *led = to_yt921x_led(led_cdev);
	struct yt921x_port *pp = to_yt921x_port(led);
	struct yt921x_priv *priv = yt921x_port_to_priv(pp);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_led_trigger_get(priv, pp->index, led->group, flagsp);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int __maybe_unused
yt921x_cled_hw_control_set(struct led_classdev *led_cdev, unsigned long flags)
{
	struct yt921x_led *led = to_yt921x_led(led_cdev);
	struct yt921x_port *pp = to_yt921x_port(led);
	struct yt921x_priv *priv = yt921x_port_to_priv(pp);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_led_trigger_set(priv, pp->index, led->group, flags);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_led_setup(struct yt921x_priv *priv, int port,
		 struct fwnode_handle *fwnode)
{
	struct device *dev = yt921x_priv_to_device(priv);
	struct yt921x_port *pp = &priv->ports[port];
	struct led_init_data init_data;
	struct led_classdev *led_cdev;
	char name[LED_MAX_NAME_SIZE];
	enum led_default_state state;
	struct yt921x_led *led;
	int polarity_orig;
	bool force_high;
	bool force_low;
	u32 led2_orig;
	int polarity;
	u32 group;
	u32 mask;
	u32 ctrl;
	u32 val;
	bool on;
	int res;
	int ret;

	res = fwnode_property_read_u32(fwnode, "reg", &group);
	if (res) {
		dev_err(dev, "Missing LED reg for %pfw\n", fwnode);
		return res;
	}
	if (group >= YT921X_LED_GROUP_NUM) {
		dev_err(dev, "Invalid LED reg %u for port %d\n", group, port);
		return -EINVAL;
	}
	if (pp->leds[group]) {
		res = -EEXIST;
		goto err;
	}

	force_high = fwnode_property_read_bool(fwnode, "active-high");
	force_low = fwnode_property_read_bool(fwnode, "active-low");
	if (force_high && force_low) {
		dev_err(dev, "Duplicate polarities for LED %02d:%02u\n",
			port, group);
		return -EINVAL;
	}
	polarity = force_high ? 1 : force_low ? -1 : 0;

	led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
	if (!led) {
		res = -ENOMEM;
		goto err;
	}
	pp->leds[group] = led;

	led->port = pp;
	led->group = group;

	state = led_init_default_state_get(fwnode);

	mutex_lock(&priv->reg_lock);

	/* Inversion is internal - force on will give low logic.
	 * In the rest of the file, treat LEDs as if active-low.
	 */
	polarity_orig = 0;
	if (polarity) {
		res = yt921x_reg_read(priv, YT921X_LED_PAR_INV, &val);
		if (res)
			goto err_inv;

		mask = YT921X_LED_PAR_INV_INVnm(group, port);
		ctrl = polarity > 0 ? val | mask : val & ~mask;
		if (ctrl != val) {
			res = yt921x_reg_write(priv, YT921X_LED_PAR_INV, ctrl);
			if (res)
				goto err_inv;

			polarity_orig = -polarity;
		}
	}

	led2_orig = U32_MAX;
	res = yt921x_reg_read(priv, YT921X_LED2_PORTn(port), &val);
	if (res)
		goto err_led2;
	mask = YT921X_LED2_PORT_FORCEn_M(group);

	switch (state) {
	case LEDS_DEFSTATE_OFF:
	case LEDS_DEFSTATE_ON:
	default:
		on = state == LEDS_DEFSTATE_ON;
		ctrl = (val & ~mask) |
		       (on ? YT921X_LED2_PORT_FORCEn_ON(group) :
			YT921X_LED2_PORT_FORCEn_OFF(group));
		if (ctrl != val) {
			res = yt921x_reg_write(priv, YT921X_LED2_PORTn(port),
					       ctrl);
			if (res)
				goto err_led2;

			led2_orig = val & mask;
		}
		break;
	case LEDS_DEFSTATE_KEEP:
		on = (val & mask) == YT921X_LED2_PORT_FORCEn_ON(group);
		break;
	}

	mutex_unlock(&priv->reg_lock);

	led_cdev = &led->cdev;
	led_cdev->brightness = on;
	led_cdev->max_brightness = 1;
	led_cdev->flags = LED_RETAIN_AT_SHUTDOWN;
	led_cdev->brightness_set_blocking = yt921x_cled_brightness_set_blocking;
	led_cdev->blink_set = yt921x_cled_blink_set;
#ifdef CONFIG_LEDS_TRIGGERS
	led_cdev->hw_control_trigger = "netdev";
	led_cdev->hw_control_get_device = yt921x_cled_hw_control_get_device;
	led_cdev->hw_control_is_supported = yt921x_cled_hw_control_is_supported;
	led_cdev->hw_control_get = yt921x_cled_hw_control_get;
	led_cdev->hw_control_set = yt921x_cled_hw_control_set;
#endif

	snprintf(name, sizeof(name), YT921X_NAME "-%u:%02d:%02u",
		 priv->ds.index, port, group);
	init_data = (typeof(init_data)){
		.fwnode = fwnode,
		.default_label = ":port",
		.devicename = name,
		.devname_mandatory = true,
	};
	res = devm_led_classdev_register_ext(dev, led_cdev, &init_data);
	if (res)
		goto revoke;

	return 0;

revoke:
	mutex_lock(&priv->reg_lock);
	if (led2_orig != U32_MAX) {
		ret = yt921x_reg_update_bits(priv, YT921X_LED2_PORTn(port),
					     YT921X_LED2_PORT_FORCEn_M(group),
					     led2_orig);
		if (ret)
			dev_err(dev,
				"Failed to restore %s for LED %02d:%02u: %d\n",
				"LED2_PORT", port, group, ret);
	}
err_led2:
	if (polarity_orig) {
		ret = yt921x_reg_toggle_bits(priv, YT921X_LED_PAR_INV,
					     YT921X_LED_PAR_INV_INVnm(group, port),
					     polarity_orig > 0);
		if (ret)
			dev_err(dev,
				"Failed to restore %s for LED %02d:%02u: %d\n",
				"LED_PAR_INV", port, group, ret);
	}
err_inv:
	mutex_unlock(&priv->reg_lock);

	pp->leds[group] = NULL;
	devm_kfree(dev, led);

err:
	dev_err(dev, "Failed to initialize LED %02d:%02u: %d\n",
		port, group, res);
	return res;
}

static void yt921x_leds_remove_port(struct yt921x_priv *priv, int port)
{
	struct device *dev = yt921x_priv_to_device(priv);
	struct yt921x_port *pp = &priv->ports[port];

	for (int group = 0; group < YT921X_LED_GROUP_NUM; group++) {
		struct yt921x_led *led = pp->leds[group];

		if (led) {
			devm_led_classdev_unregister(dev, &led->cdev);
			pp->leds[group] = NULL;
			devm_kfree(dev, led);
		}
	}

	pp->led_duty_mask = 0;
	pp->led_cycle_mask = 0;
}

static int yt921x_leds_setup_port(struct yt921x_priv *priv, int port)
{
	struct device *dev = yt921x_priv_to_device(priv);
	struct dsa_switch *ds = &priv->ds;
	struct device_node *leds_np;
	struct dsa_port *dp;

	dp = dsa_to_port(ds, port);
	leds_np = of_get_child_by_name(dp->dn, "leds");
	if (!leds_np)
		return 0;

	if (port >= YT921X_LED_PORT_NUM) {
		dev_err(dev, "Cannot configure LEDs for port %d\n", port);
		of_node_put(leds_np);
		return -EINVAL;
	}

	for_each_child_of_node_scoped(leds_np, led_np) {
		yt921x_led_setup(priv, port, of_fwnode_handle(led_np));
		/* Allow partial configuration: LEDs are optional */
	}

	of_node_put(leds_np);
	return 0;
}

void yt921x_leds_remove(struct yt921x_priv *priv)
{
	for (int port = 0; port < YT921X_LED_PORT_NUM; port++)
		yt921x_leds_remove_port(priv, port);
}

int yt921x_leds_setup(struct yt921x_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;
	struct dsa_port *dp;

	/* LEDs are always enabled. There is no way to disable them altogether
	 * (as far as I know).
	 */

	dsa_switch_for_each_port(dp, ds) {
		int port = dp->index;

		if (!dp->dn)
			continue;

		yt921x_leds_setup_port(priv, port);
		/* Allow partial configuration: LEDs are optional */
	}

	return 0;
}
