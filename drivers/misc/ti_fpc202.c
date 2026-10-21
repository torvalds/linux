// SPDX-License-Identifier: GPL-2.0-only
/*
 * ti_fpc202.c - FPC202 Dual Port Controller driver
 *
 * Copyright (C) 2024 Bootlin
 *
 */

#include <linux/cleanup.h>
#include <linux/device/devres.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/i2c-atr.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/math.h>
#include <linux/types.h>

#define FPC202_NUM_PORTS 2
#define FPC202_ALIASES_PER_PORT 2

/*
 * GPIO: port mapping
 *
 * 0: P0_S0_IN_A
 * 1: P0_S1_IN_A
 * 2: P1_S0_IN_A
 * 3: P1_S1_IN_A
 * 4: P0_S0_IN_B
 * ...
 * 8: P0_S0_IN_C
 * ...
 * 12: P0_S0_OUT_A
 * ...
 * 16: P0_S0_OUT_B
 * ...
 * 19: P1_S1_OUT_B
 *
 * Ports with optional LED control:
 *
 * 20: P0_S0_OUT_C (P0_S0_LED1)
 * ...
 * 23: P1_S1_OUT_C (P1_S1_LED1)
 * 24: P0_S0_OUT_D (P0_S0_LED2
 * ...
 * 27: P1_S1_OUT_D (P1_S1_LED2)
 *
 */

#define FPC202_GPIO_COUNT 28
#define FPC202_GPIO_P0_S0_IN_B  4
#define FPC202_GPIO_P0_S0_OUT_A 12
#define FPC202_GPIO_P0_S0_OUT_C 20
#define FPC202_GPIO_P0_S0_OUT_D 24

#define FPC202_REG_IN_A_INT    0x6
#define FPC202_REG_IN_C_IN_B   0x7
#define FPC202_REG_OUT_A_OUT_B 0x8
#define FPC202_REG_OUT_C_OUT_D 0x9

#define FPC202_REG_OUT_A_OUT_B_VAL 0xa

#define FPC202_LED_COUNT 8

/* There are four LED GPIO mode registers which manage two GPIOs each. */
#define FPC202_REG_LED_MODE(offset) (0x1a + 0x20 * ((offset) % 4))

/* LED1 GPIOs (*_OUT_C) are configured in bits 1:0, LED2 GPIOs (*_OUT_D) in bits 3:2. */
#define FPC202_LED_MODE_SHIFT(offset) ((offset) < FPC202_GPIO_P0_S0_OUT_D ? 0 : 2)
#define FPC202_LED_MODE_MASK(offset) (GENMASK(1, 0) << FPC202_LED_MODE_SHIFT(offset))

/* There is one PWM control register for each GPIO LED */
#define FPC202_REG_LED_PWM(offset) \
	(((offset) < FPC202_GPIO_P0_S0_OUT_D ? 0x14 : 0x15) + 0x20 * ((offset) % 4))

/* There are two blink delay registers (on/off time) for each GPIO LED */
#define FPC202_REG_LED_BLINK_ON(offset) \
	(((offset) < FPC202_GPIO_P0_S0_OUT_D ? 0x16 : 0x18) + 0x20 * ((offset) % 4))
#define FPC202_REG_LED_BLINK_OFF(offset) (FPC202_REG_LED_BLINK_ON(offset) + 1)

/* The actual hardware precision is 2.5ms but since the LED API doesn't handle sub-millisecond
 * timesteps this is rounded up to 5ms
 */
#define FPC202_LED_BLINK_PRECISION 5UL

#define FPC202_LED_MAX_BRIGHTNESS 255

#define FPC202_REG_MOD_DEV(port, dev) (0xb4 + ((port) * 4) + (dev))
#define FPC202_REG_AUX_DEV(port, dev) (0xb6 + ((port) * 4) + (dev))

/*
 * The FPC202 doesn't support turning off address translation on a single port.
 * So just set an invalid I2C address as the translation target when no client
 * address is attached.
 */
#define FPC202_REG_DEV_INVALID 0

/* Even aliases are assigned to device 0 and odd aliases to device 1 */
#define fpc202_dev_num_from_alias(alias) ((alias) % 2)

enum fpc202_led_mode {
	FPC202_LED_MODE_OFF = 0,
	FPC202_LED_MODE_ON = 1,
	FPC202_LED_MODE_PWM = 2,
	FPC202_LED_MODE_BLINK = 3,
};

struct fpc202_led {
	int offset;
	struct led_classdev led_cdev;
	struct fpc202_priv *priv;
	struct gpio_desc *gpio;
	enum fpc202_led_mode mode;
};

struct fpc202_priv {
	struct i2c_client *client;
	struct i2c_atr *atr;
	struct gpio_desc *en_gpio;
	struct gpio_chip gpio;
	struct fpc202_led leds[FPC202_LED_COUNT];

	/* Lock REG_MOD/AUX_DEV and addr_caches during attach/detach */
	struct mutex reg_dev_lock;

	/* Lock LED mode select register during accesses */
	struct mutex led_mode_lock;

	/* Cached device addresses for both ports and their devices */
	u8 addr_caches[2][2];

	/* Keep track of which ports were probed */
	DECLARE_BITMAP(probed_ports, FPC202_NUM_PORTS);
};

static void fpc202_fill_alias_table(struct i2c_client *client, u16 *aliases, int port_id)
{
	u16 first_alias;
	int i;

	/*
	 * There is a predefined list of aliases for each FPC202 I2C
	 * self-address.  This allows daisy-chained FPC202 units to
	 * automatically take on different sets of aliases.
	 * Each port of an FPC202 unit is assigned two aliases from this list.
	 */
	first_alias = 0x10 + 4 * port_id + 8 * ((u16)client->addr - 2);

	for (i = 0; i < FPC202_ALIASES_PER_PORT; i++)
		aliases[i] = first_alias + i;
}

static int fpc202_gpio_get_dir(int offset)
{
	return offset < FPC202_GPIO_P0_S0_OUT_A ? GPIO_LINE_DIRECTION_IN : GPIO_LINE_DIRECTION_OUT;
}

static int fpc202_gpio_has_led_caps(int offset)
{
	return offset >= FPC202_GPIO_P0_S0_OUT_C;
}

static int fpc202_read(struct fpc202_priv *priv, u8 reg)
{
	int val;

	val = i2c_smbus_read_byte_data(priv->client, reg);
	return val;
}

static int fpc202_write(struct fpc202_priv *priv, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(priv->client, reg, value);
}

static void fpc202_set_enable(struct fpc202_priv *priv, int enable)
{
	if (!priv->en_gpio)
		return;

	gpiod_set_value(priv->en_gpio, enable);
}

static int fpc202_led_mode_write(struct fpc202_priv *priv,
				 int offset,
				 enum fpc202_led_mode mode)
{
	u8 val, reg = FPC202_REG_LED_MODE(offset);
	int ret;

	guard(mutex)(&priv->led_mode_lock);

	ret = fpc202_read(priv, reg);
	if (ret < 0) {
		dev_err(&priv->client->dev, "failed to read LED mode %d! err %d\n",
			offset, ret);
		return ret;
	}

	val = (u8)ret & ~FPC202_LED_MODE_MASK(offset);
	val |= mode << FPC202_LED_MODE_SHIFT(offset);

	return fpc202_write(priv, reg, val);
}

static int fpc202_led_mode_set(struct fpc202_led *led, enum fpc202_led_mode mode)
{
	struct fpc202_priv *priv = led->priv;

	led->mode = mode;

	return fpc202_led_mode_write(priv, led->offset, mode);
}

static int fpc202_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct fpc202_priv *priv = gpiochip_get_data(chip);
	int ret;
	u8 val;

	if (fpc202_gpio_has_led_caps(offset)) {
		ret = fpc202_led_mode_write(priv, offset,
					    value ? FPC202_LED_MODE_ON : FPC202_LED_MODE_OFF);
		if (ret < 0)
			dev_err(&priv->client->dev, "Failed to set GPIO %d LED mode! err %d\n",
				offset, ret);

		return ret;
	}

	ret = fpc202_read(priv, FPC202_REG_OUT_A_OUT_B_VAL);
	if (ret < 0) {
		dev_err(&priv->client->dev, "Failed to set GPIO %d value! err %d\n", offset, ret);
		return ret;
	}

	val = (u8)ret;

	if (value)
		val |= BIT(offset - FPC202_GPIO_P0_S0_OUT_A);
	else
		val &= ~BIT(offset - FPC202_GPIO_P0_S0_OUT_A);

	return fpc202_write(priv, FPC202_REG_OUT_A_OUT_B_VAL, val);
}

static int fpc202_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct fpc202_priv *priv = gpiochip_get_data(chip);
	u8 reg, bit;
	int ret;

	if (offset < FPC202_GPIO_P0_S0_IN_B) {
		reg = FPC202_REG_IN_A_INT;
		bit = BIT(4 + offset);
	} else if (offset < FPC202_GPIO_P0_S0_OUT_A) {
		reg = FPC202_REG_IN_C_IN_B;
		bit = BIT(offset - FPC202_GPIO_P0_S0_IN_B);
	} else if (!fpc202_gpio_has_led_caps(offset)) {
		reg = FPC202_REG_OUT_A_OUT_B_VAL;
		bit = BIT(offset - FPC202_GPIO_P0_S0_OUT_A);
	} else {
		return -EOPNOTSUPP;
	}

	ret = fpc202_read(priv, reg);
	if (ret < 0)
		return ret;

	return !!(((u8)ret) & bit);
}

static int fpc202_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	if (fpc202_gpio_get_dir(offset) == GPIO_LINE_DIRECTION_OUT)
		return -EINVAL;

	return 0;
}

static int fpc202_gpio_direction_output(struct gpio_chip *chip, unsigned int offset,
					int value)
{
	struct fpc202_priv *priv = gpiochip_get_data(chip);
	u8 reg, val, bit;
	int ret;

	if (fpc202_gpio_get_dir(offset) == GPIO_LINE_DIRECTION_IN)
		return -EINVAL;

	fpc202_gpio_set(chip, offset, value);

	if (fpc202_gpio_has_led_caps(offset)) {
		reg = FPC202_REG_OUT_C_OUT_D;
		bit = BIT(offset - FPC202_GPIO_P0_S0_OUT_C);
	} else {
		reg = FPC202_REG_OUT_A_OUT_B;
		bit = BIT(offset - FPC202_GPIO_P0_S0_OUT_A);
	}

	ret = fpc202_read(priv, reg);
	if (ret < 0)
		return ret;

	val = (u8)ret | bit;

	return fpc202_write(priv, reg, val);
}

/*
 * Set the translation table entry associated with a port and device number.
 *
 * Each downstream port of the FPC202 has two fixed aliases corresponding to
 * device numbers 0 and 1. If one of these aliases is found in an incoming I2C
 * transfer, it will be translated to the address given by the corresponding
 * translation table entry.
 */
static int fpc202_write_dev_addr(struct fpc202_priv *priv, u32 port_id, int dev_num, u16 addr)
{
	int ret, reg_mod, reg_aux;
	u8 val;

	guard(mutex)(&priv->reg_dev_lock);

	reg_mod = FPC202_REG_MOD_DEV(port_id, dev_num);
	reg_aux = FPC202_REG_AUX_DEV(port_id, dev_num);
	val = addr & 0x7f;

	ret = fpc202_write(priv, reg_mod, val);
	if (ret)
		return ret;

	/*
	 * The FPC202 datasheet is unclear about the role of the AUX registers.
	 * Empirically, writing to them as well seems to be necessary for
	 * address translation to function properly.
	 */
	ret = fpc202_write(priv, reg_aux, val);

	priv->addr_caches[port_id][dev_num] = val;

	return ret;
}

static int fpc202_attach_addr(struct i2c_atr *atr, u32 chan_id,
			      u16 addr, u16 alias)
{
	struct fpc202_priv *priv = i2c_atr_get_driver_data(atr);

	dev_dbg(&priv->client->dev, "attaching address 0x%02x to alias 0x%02x\n", addr, alias);

	return fpc202_write_dev_addr(priv, chan_id, fpc202_dev_num_from_alias(alias), addr);
}

static void fpc202_detach_addr(struct i2c_atr *atr, u32 chan_id,
			       u16 addr)
{
	struct fpc202_priv *priv = i2c_atr_get_driver_data(atr);
	int dev_num, val;

	for (dev_num = 0; dev_num < 2; dev_num++) {
		mutex_lock(&priv->reg_dev_lock);

		val = priv->addr_caches[chan_id][dev_num];

		mutex_unlock(&priv->reg_dev_lock);

		if (val == (addr & 0x7f)) {
			fpc202_write_dev_addr(priv, chan_id, dev_num, FPC202_REG_DEV_INVALID);
			return;
		}
	}
}

static const struct i2c_atr_ops fpc202_atr_ops = {
	.attach_addr = fpc202_attach_addr,
	.detach_addr = fpc202_detach_addr,
};

static struct fpc202_led *fpc202_cdev_to_led(struct led_classdev *cdev)
{
	return container_of(cdev, struct fpc202_led, led_cdev);
}

static struct fpc202_led *fpc202_led_get(struct fpc202_priv *priv, int offset)
{
	return &priv->leds[offset - FPC202_GPIO_P0_S0_OUT_C];
}

static int fpc202_led_blink_set(struct led_classdev *cdev,
				unsigned long *delay_on,
				unsigned long *delay_off)
{
	struct fpc202_led *led = fpc202_cdev_to_led(cdev);
	struct fpc202_priv *priv = led->priv;
	unsigned long val;
	int ret;

	if (*delay_on == 0 && *delay_off == 0) {
		*delay_on = 250;
		*delay_off = 250;
	} else {
		if (*delay_on % FPC202_LED_BLINK_PRECISION)
			*delay_on = roundup(*delay_on, FPC202_LED_BLINK_PRECISION);

		if (*delay_off % FPC202_LED_BLINK_PRECISION)
			*delay_off = roundup(*delay_off, FPC202_LED_BLINK_PRECISION);
	}

	/* Multiply the duration by two, since the actual precision is 2.5ms not 5ms*/
	val = 2 * (*delay_on / FPC202_LED_BLINK_PRECISION);
	if (val > 255) {
		val = 255;
		*delay_on = (val / 2) * FPC202_LED_BLINK_PRECISION;
	}

	ret = fpc202_write(priv, FPC202_REG_LED_BLINK_ON(led->offset), val);
	if (ret) {
		dev_err(&priv->client->dev,
			"Failed to set blink on duration for LED %d, err %d\n",
			led->offset, ret);
		return ret;
	}

	val = 2 * (*delay_off / FPC202_LED_BLINK_PRECISION);
	if (val > 255) {
		val = 255;
		*delay_off = (val / 2) * FPC202_LED_BLINK_PRECISION;
	}

	ret = fpc202_write(priv, FPC202_REG_LED_BLINK_OFF(led->offset), val);
	if (ret) {
		dev_err(&priv->client->dev,
			"Failed to set blink off duration for LED %d, err %d\n",
			led->offset, ret);
		return ret;
	}

	return fpc202_led_mode_set(led, FPC202_LED_MODE_BLINK);
}

static enum led_brightness fpc202_led_brightness_get(struct led_classdev *cdev)
{
	struct fpc202_led *led = fpc202_cdev_to_led(cdev);

	if (led->mode == FPC202_LED_MODE_OFF)
		return LED_OFF;

	return LED_ON;
}

static int fpc202_led_brightness_set(struct led_classdev *cdev,
				     enum led_brightness brightness)
{
	struct fpc202_led *led = fpc202_cdev_to_led(cdev);
	struct fpc202_priv *priv = led->priv;
	int ret;

	if (!brightness)
		return fpc202_led_mode_set(led, FPC202_LED_MODE_OFF);

	if (led->mode != FPC202_LED_MODE_BLINK) {
		if (brightness == FPC202_LED_MAX_BRIGHTNESS)
			return fpc202_led_mode_set(led, FPC202_LED_MODE_ON);

		ret = fpc202_led_mode_set(led, FPC202_LED_MODE_PWM);
		if (ret) {
			dev_err(&priv->client->dev, "Failed to set LED %d mode, err %d\n",
				led->offset, ret);
			return ret;
		}
	}

	return fpc202_write(priv, FPC202_REG_LED_PWM(led->offset), brightness);
}

static int fpc202_register_led(struct fpc202_priv *priv, int offset,
			       struct device_node *led_handle)
{
	struct fpc202_led *led = fpc202_led_get(priv, offset);
	struct device *dev = &priv->client->dev;
	struct led_init_data init_data = { };
	int ret = 0;

	led->priv = priv;
	led->offset = offset;
	led->led_cdev.max_brightness = FPC202_LED_MAX_BRIGHTNESS;
	led->led_cdev.brightness_set_blocking = fpc202_led_brightness_set;
	led->led_cdev.brightness_get = fpc202_led_brightness_get;
	led->led_cdev.blink_set = fpc202_led_blink_set;

	init_data.fwnode = of_fwnode_handle(led_handle);
	init_data.default_label = NULL;
	init_data.devicename = NULL;
	init_data.devname_mandatory = false;

	ret = fpc202_led_mode_set(led, FPC202_LED_MODE_OFF);
	if (ret) {
		dev_err(dev, "Failed to set LED %d mode, err %d\n", offset, ret);
		return ret;
	}

	ret = devm_led_classdev_register_ext(dev, &led->led_cdev, &init_data);
	if (ret) {
		dev_err(dev, "Failed to register LED %d cdev, err %d\n", offset, ret);
		return ret;
	}

	/* Claim corresponding GPIO line so that it cannot be interfered with */
	led->gpio = gpiochip_request_own_desc(&priv->gpio, offset, led->led_cdev.name,
					      GPIO_ACTIVE_HIGH, GPIOD_ASIS);
	if (IS_ERR(led->gpio)) {
		ret = PTR_ERR(led->gpio);
		dev_err(dev, "Failed to register LED %d cdev, err %d\n", offset, ret);
	}

	return ret;
}

static int fpc202_register_leds(struct fpc202_priv *priv)
{
	struct device *dev = &priv->client->dev;
	int offset, ret = 0;

	if (!devres_open_group(dev, fpc202_register_leds, GFP_KERNEL))
		return -ENOMEM;

	for_each_child_of_node_scoped(dev->of_node, led_handle) {
		ret = of_property_read_u32(led_handle, "reg", &offset);
		if (ret) {
			dev_err(dev, "Failed to read 'reg' property of child node, err %d\n", ret);
			return ret;
		}

		if (offset < FPC202_GPIO_P0_S0_OUT_C || offset > FPC202_GPIO_COUNT)
			continue;

		ret = fpc202_register_led(priv, offset, led_handle);
		if (ret) {
			dev_err(dev, "Failed to register LED %d, err %d\n", offset,
				ret);
			goto free_own_gpios;
		}
	}

	devres_close_group(dev, fpc202_register_leds);

	return 0;

free_own_gpios:
	for (offset = 0; offset < FPC202_LED_COUNT; offset++)
		if (priv->leds[offset].gpio)
			gpiochip_free_own_desc(priv->leds[offset].gpio);
	return ret;
}

static int fpc202_probe_port(struct fpc202_priv *priv, struct device_node *i2c_handle, int port_id)
{
	u16 aliases[FPC202_ALIASES_PER_PORT] = { };
	struct device *dev = &priv->client->dev;
	struct i2c_atr_adap_desc desc = { };
	int ret = 0;

	desc.chan_id = port_id;
	desc.parent = dev;
	desc.bus_handle = of_fwnode_handle(i2c_handle);
	desc.num_aliases = FPC202_ALIASES_PER_PORT;

	fpc202_fill_alias_table(priv->client, aliases, port_id);
	desc.aliases = aliases;

	ret = i2c_atr_add_adapter(priv->atr, &desc);
	if (ret)
		return ret;

	set_bit(port_id, priv->probed_ports);

	ret = fpc202_write_dev_addr(priv, port_id, 0, FPC202_REG_DEV_INVALID);
	if (ret)
		return ret;

	return fpc202_write_dev_addr(priv, port_id, 1, FPC202_REG_DEV_INVALID);
}

static void fpc202_remove_port(struct fpc202_priv *priv, int port_id)
{
	i2c_atr_del_adapter(priv->atr, port_id);
	clear_bit(port_id, priv->probed_ports);
}

static int fpc202_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct fpc202_priv *priv;
	int ret, port_id, led_id;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->reg_dev_lock);
	mutex_init(&priv->led_mode_lock);

	priv->client = client;
	i2c_set_clientdata(client, priv);

	priv->en_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->en_gpio)) {
		ret = PTR_ERR(priv->en_gpio);
		dev_err(dev, "failed to fetch enable GPIO! err %d\n", ret);
		goto destroy_mutex;
	}

	priv->gpio.label = "gpio-fpc202";
	priv->gpio.base = -1;
	priv->gpio.direction_input = fpc202_gpio_direction_input;
	priv->gpio.direction_output = fpc202_gpio_direction_output;
	priv->gpio.set = fpc202_gpio_set;
	priv->gpio.get = fpc202_gpio_get;
	priv->gpio.ngpio = FPC202_GPIO_COUNT;
	priv->gpio.parent = dev;
	priv->gpio.owner = THIS_MODULE;

	ret = gpiochip_add_data(&priv->gpio, priv);
	if (ret) {
		priv->gpio.parent = NULL;
		dev_err(dev, "failed to add gpiochip err %d\n", ret);
		goto disable_gpio;
	}

	priv->atr = i2c_atr_new(client->adapter, dev, &fpc202_atr_ops, 2, 0);
	if (IS_ERR(priv->atr)) {
		ret = PTR_ERR(priv->atr);
		dev_err(dev, "failed to create i2c atr err %d\n", ret);
		goto disable_gpio;
	}

	i2c_atr_set_driver_data(priv->atr, priv);

	ret = fpc202_register_leds(priv);
	if (ret) {
		dev_err(dev, "Failed to register LEDs, err %d\n", ret);
		goto delete_atr;
	}

	bitmap_zero(priv->probed_ports, FPC202_NUM_PORTS);

	for_each_child_of_node_scoped(dev->of_node, i2c_handle) {
		ret = of_property_read_u32(i2c_handle, "reg", &port_id);
		if (ret) {
			if (ret == -EINVAL)
				continue;

			dev_err(dev, "failed to read 'reg' property of child node, err %d\n", ret);
			goto unregister_chans;
		}

		if (port_id >= FPC202_NUM_PORTS)
			continue;

		ret = fpc202_probe_port(priv, i2c_handle, port_id);
		if (ret) {
			dev_err(dev, "Failed to probe port %d, err %d\n", port_id, ret);
			goto unregister_chans;
		}
	}

	goto out;

unregister_chans:
	for_each_set_bit(port_id, priv->probed_ports, FPC202_NUM_PORTS)
		fpc202_remove_port(priv, port_id);

	for (led_id = 0; led_id < FPC202_LED_COUNT; led_id++)
		if (priv->leds[led_id].gpio)
			gpiochip_free_own_desc(priv->leds[led_id].gpio);

	devres_release_group(&client->dev, fpc202_register_leds);
delete_atr:
	i2c_atr_delete(priv->atr);
disable_gpio:
	fpc202_set_enable(priv, 0);
	gpiochip_remove(&priv->gpio);
destroy_mutex:
	mutex_destroy(&priv->led_mode_lock);
	mutex_destroy(&priv->reg_dev_lock);
out:
	return ret;
}

static void fpc202_remove(struct i2c_client *client)
{
	struct fpc202_priv *priv = i2c_get_clientdata(client);
	int port_id, led_id;

	for_each_set_bit(port_id, priv->probed_ports, FPC202_NUM_PORTS)
		fpc202_remove_port(priv, port_id);

	for (led_id = 0; led_id < FPC202_LED_COUNT; led_id++)
		if (priv->leds[led_id].gpio)
			gpiochip_free_own_desc(priv->leds[led_id].gpio);

	/* Release led devices early so that blink handlers don't trigger. */
	devres_release_group(&client->dev, fpc202_register_leds);

	mutex_destroy(&priv->led_mode_lock);
	mutex_destroy(&priv->reg_dev_lock);

	i2c_atr_delete(priv->atr);

	fpc202_set_enable(priv, 0);
	gpiochip_remove(&priv->gpio);
}

static const struct of_device_id fpc202_of_match[] = {
	{ .compatible = "ti,fpc202" },
	{ }
};
MODULE_DEVICE_TABLE(of, fpc202_of_match);

static struct i2c_driver fpc202_driver = {
	.driver = {
		.name = "fpc202",
		.of_match_table = fpc202_of_match,
	},
	.probe = fpc202_probe,
	.remove = fpc202_remove,
};

module_i2c_driver(fpc202_driver);

MODULE_AUTHOR("Romain Gantois <romain.gantois@bootlin.com>");
MODULE_DESCRIPTION("TI FPC202 Dual Port Controller driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("I2C_ATR");
