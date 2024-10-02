// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * leds-netxbig.c - Driver for the 2Big and 5Big Network series LEDs
 *
 * Copyright (C) 2010 LaCie
 *
 * Author: Simon Guinot <sguinot@lacie.com>
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/leds.h>
#include <linux/of.h>
#include <linux/of_platform.h>

struct netxbig_gpio_ext {
	struct gpio_desc **addr;
	int		num_addr;
	struct gpio_desc **data;
	int		num_data;
	struct gpio_desc *enable;
};

enum netxbig_led_mode {
	NETXBIG_LED_OFF,
	NETXBIG_LED_ON,
	NETXBIG_LED_SATA,
	NETXBIG_LED_TIMER1,
	NETXBIG_LED_TIMER2,
	NETXBIG_LED_MODE_NUM,
};

#define NETXBIG_LED_INVALID_MODE NETXBIG_LED_MODE_NUM

struct netxbig_led_timer {
	unsigned long		delay_on;
	unsigned long		delay_off;
	enum netxbig_led_mode	mode;
};

struct netxbig_led {
	const char	*name;
	const char	*default_trigger;
	int		mode_addr;
	int		*mode_val;
	int		bright_addr;
	int		bright_max;
};

struct netxbig_led_platform_data {
	struct netxbig_gpio_ext	*gpio_ext;
	struct netxbig_led_timer *timer;
	int			num_timer;
	struct netxbig_led	*leds;
	int			num_leds;
};

/*
 * GPIO extension bus.
 */

static DEFINE_SPINLOCK(gpio_ext_lock);

static void gpio_ext_set_addr(struct netxbig_gpio_ext *gpio_ext, int addr)
{
	int pin;

	for (pin = 0; pin < gpio_ext->num_addr; pin++)
		gpiod_set_value(gpio_ext->addr[pin], (addr >> pin) & 1);
}

static void gpio_ext_set_data(struct netxbig_gpio_ext *gpio_ext, int data)
{
	int pin;

	for (pin = 0; pin < gpio_ext->num_data; pin++)
		gpiod_set_value(gpio_ext->data[pin], (data >> pin) & 1);
}

static void gpio_ext_enable_select(struct netxbig_gpio_ext *gpio_ext)
{
	/* Enable select is done on the raising edge. */
	gpiod_set_value(gpio_ext->enable, 0);
	gpiod_set_value(gpio_ext->enable, 1);
}

static void gpio_ext_set_value(struct netxbig_gpio_ext *gpio_ext,
			       int addr, int value)
{
	unsigned long flags;

	spin_lock_irqsave(&gpio_ext_lock, flags);
	gpio_ext_set_addr(gpio_ext, addr);
	gpio_ext_set_data(gpio_ext, value);
	gpio_ext_enable_select(gpio_ext);
	spin_unlock_irqrestore(&gpio_ext_lock, flags);
}

/*
 * Class LED driver.
 */

struct netxbig_led_data {
	struct netxbig_gpio_ext	*gpio_ext;
	struct led_classdev	cdev;
	int			mode_addr;
	int			*mode_val;
	int			bright_addr;
	struct			netxbig_led_timer *timer;
	int			num_timer;
	enum netxbig_led_mode	mode;
	int			sata;
	spinlock_t		lock;
};

static int netxbig_led_get_timer_mode(enum netxbig_led_mode *mode,
				      unsigned long delay_on,
				      unsigned long delay_off,
				      struct netxbig_led_timer *timer,
				      int num_timer)
{
	int i;

	for (i = 0; i < num_timer; i++) {
		if (timer[i].delay_on == delay_on &&
		    timer[i].delay_off == delay_off) {
			*mode = timer[i].mode;
			return 0;
		}
	}
	return -EINVAL;
}

static int netxbig_led_blink_set(struct led_classdev *led_cdev,
				 unsigned long *delay_on,
				 unsigned long *delay_off)
{
	struct netxbig_led_data *led_dat =
		container_of(led_cdev, struct netxbig_led_data, cdev);
	enum netxbig_led_mode mode;
	int mode_val;
	int ret;

	/* Look for a LED mode with the requested timer frequency. */
	ret = netxbig_led_get_timer_mode(&mode, *delay_on, *delay_off,
					 led_dat->timer, led_dat->num_timer);
	if (ret < 0)
		return ret;

	mode_val = led_dat->mode_val[mode];
	if (mode_val == NETXBIG_LED_INVALID_MODE)
		return -EINVAL;

	spin_lock_irq(&led_dat->lock);

	gpio_ext_set_value(led_dat->gpio_ext, led_dat->mode_addr, mode_val);
	led_dat->mode = mode;

	spin_unlock_irq(&led_dat->lock);

	return 0;
}

static void netxbig_led_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	struct netxbig_led_data *led_dat =
		container_of(led_cdev, struct netxbig_led_data, cdev);
	enum netxbig_led_mode mode;
	int mode_val;
	int set_brightness = 1;
	unsigned long flags;

	spin_lock_irqsave(&led_dat->lock, flags);

	if (value == LED_OFF) {
		mode = NETXBIG_LED_OFF;
		set_brightness = 0;
	} else {
		if (led_dat->sata)
			mode = NETXBIG_LED_SATA;
		else if (led_dat->mode == NETXBIG_LED_OFF)
			mode = NETXBIG_LED_ON;
		else /* Keep 'timer' mode. */
			mode = led_dat->mode;
	}
	mode_val = led_dat->mode_val[mode];

	gpio_ext_set_value(led_dat->gpio_ext, led_dat->mode_addr, mode_val);
	led_dat->mode = mode;
	/*
	 * Note that the brightness register is shared between all the
	 * SATA LEDs. So, change the brightness setting for a single
	 * SATA LED will affect all the others.
	 */
	if (set_brightness)
		gpio_ext_set_value(led_dat->gpio_ext,
				   led_dat->bright_addr, value);

	spin_unlock_irqrestore(&led_dat->lock, flags);
}

static ssize_t sata_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buff, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct netxbig_led_data *led_dat =
		container_of(led_cdev, struct netxbig_led_data, cdev);
	unsigned long enable;
	enum netxbig_led_mode mode;
	int mode_val;
	int ret;

	ret = kstrtoul(buff, 10, &enable);
	if (ret < 0)
		return ret;

	enable = !!enable;

	spin_lock_irq(&led_dat->lock);

	if (led_dat->sata == enable) {
		ret = count;
		goto exit_unlock;
	}

	if (led_dat->mode != NETXBIG_LED_ON &&
	    led_dat->mode != NETXBIG_LED_SATA)
		mode = led_dat->mode; /* Keep modes 'off' and 'timer'. */
	else if (enable)
		mode = NETXBIG_LED_SATA;
	else
		mode = NETXBIG_LED_ON;

	mode_val = led_dat->mode_val[mode];
	if (mode_val == NETXBIG_LED_INVALID_MODE) {
		ret = -EINVAL;
		goto exit_unlock;
	}

	gpio_ext_set_value(led_dat->gpio_ext, led_dat->mode_addr, mode_val);
	led_dat->mode = mode;
	led_dat->sata = enable;

	ret = count;

exit_unlock:
	spin_unlock_irq(&led_dat->lock);

	return ret;
}

static ssize_t sata_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct netxbig_led_data *led_dat =
		container_of(led_cdev, struct netxbig_led_data, cdev);

	return sprintf(buf, "%d\n", led_dat->sata);
}

static DEVICE_ATTR_RW(sata);

static struct attribute *netxbig_led_attrs[] = {
	&dev_attr_sata.attr,
	NULL
};
ATTRIBUTE_GROUPS(netxbig_led);

static int create_netxbig_led(struct platform_device *pdev,
			      struct netxbig_led_platform_data *pdata,
			      struct netxbig_led_data *led_dat,
			      const struct netxbig_led *template)
{
	spin_lock_init(&led_dat->lock);
	led_dat->gpio_ext = pdata->gpio_ext;
	led_dat->cdev.name = template->name;
	led_dat->cdev.default_trigger = template->default_trigger;
	led_dat->cdev.blink_set = netxbig_led_blink_set;
	led_dat->cdev.brightness_set = netxbig_led_set;
	/*
	 * Because the GPIO extension bus don't allow to read registers
	 * value, there is no way to probe the LED initial state.
	 * So, the initial sysfs LED value for the "brightness" and "sata"
	 * attributes are inconsistent.
	 *
	 * Note that the initial LED state can't be reconfigured.
	 * The reason is that the LED behaviour must stay uniform during
	 * the whole boot process (bootloader+linux).
	 */
	led_dat->sata = 0;
	led_dat->cdev.brightness = LED_OFF;
	led_dat->cdev.max_brightness = template->bright_max;
	led_dat->cdev.flags |= LED_CORE_SUSPENDRESUME;
	led_dat->mode_addr = template->mode_addr;
	led_dat->mode_val = template->mode_val;
	led_dat->bright_addr = template->bright_addr;
	led_dat->timer = pdata->timer;
	led_dat->num_timer = pdata->num_timer;
	/*
	 * If available, expose the SATA activity blink capability through
	 * a "sata" sysfs attribute.
	 */
	if (led_dat->mode_val[NETXBIG_LED_SATA] != NETXBIG_LED_INVALID_MODE)
		led_dat->cdev.groups = netxbig_led_groups;

	return devm_led_classdev_register(&pdev->dev, &led_dat->cdev);
}

/**
 * netxbig_gpio_ext_remove() - Clean up GPIO extension data
 * @data: managed resource data to clean up
 *
 * Since we pick GPIO descriptors from another device than the device our
 * driver is probing to, we need to register a specific callback to free
 * these up using managed resources.
 */
static void netxbig_gpio_ext_remove(void *data)
{
	struct netxbig_gpio_ext *gpio_ext = data;
	int i;

	for (i = 0; i < gpio_ext->num_addr; i++)
		gpiod_put(gpio_ext->addr[i]);
	for (i = 0; i < gpio_ext->num_data; i++)
		gpiod_put(gpio_ext->data[i]);
	gpiod_put(gpio_ext->enable);
}

/**
 * netxbig_gpio_ext_get() - Obtain GPIO extension device data
 * @dev: main LED device
 * @gpio_ext_dev: the GPIO extension device
 * @gpio_ext: the data structure holding the GPIO extension data
 *
 * This function walks the subdevice that only contain GPIO line
 * handles in the device tree and obtains the GPIO descriptors from that
 * device.
 */
static int netxbig_gpio_ext_get(struct device *dev,
				struct device *gpio_ext_dev,
				struct netxbig_gpio_ext *gpio_ext)
{
	struct gpio_desc **addr, **data;
	int num_addr, num_data;
	struct gpio_desc *gpiod;
	int ret;
	int i;

	ret = gpiod_count(gpio_ext_dev, "addr");
	if (ret < 0) {
		dev_err(dev,
			"Failed to count GPIOs in DT property addr-gpios\n");
		return ret;
	}
	num_addr = ret;
	addr = devm_kcalloc(dev, num_addr, sizeof(*addr), GFP_KERNEL);
	if (!addr)
		return -ENOMEM;

	/*
	 * We cannot use devm_ managed resources with these GPIO descriptors
	 * since they are associated with the "GPIO extension device" which
	 * does not probe any driver. The device tree parser will however
	 * populate a platform device for it so we can anyway obtain the
	 * GPIO descriptors from the device.
	 */
	for (i = 0; i < num_addr; i++) {
		gpiod = gpiod_get_index(gpio_ext_dev, "addr", i,
					GPIOD_OUT_LOW);
		if (IS_ERR(gpiod))
			return PTR_ERR(gpiod);
		gpiod_set_consumer_name(gpiod, "GPIO extension addr");
		addr[i] = gpiod;
	}
	gpio_ext->addr = addr;
	gpio_ext->num_addr = num_addr;

	ret = gpiod_count(gpio_ext_dev, "data");
	if (ret < 0) {
		dev_err(dev,
			"Failed to count GPIOs in DT property data-gpios\n");
		return ret;
	}
	num_data = ret;
	data = devm_kcalloc(dev, num_data, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	for (i = 0; i < num_data; i++) {
		gpiod = gpiod_get_index(gpio_ext_dev, "data", i,
					GPIOD_OUT_LOW);
		if (IS_ERR(gpiod))
			return PTR_ERR(gpiod);
		gpiod_set_consumer_name(gpiod, "GPIO extension data");
		data[i] = gpiod;
	}
	gpio_ext->data = data;
	gpio_ext->num_data = num_data;

	gpiod = gpiod_get(gpio_ext_dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod)) {
		dev_err(dev,
			"Failed to get GPIO from DT property enable-gpio\n");
		return PTR_ERR(gpiod);
	}
	gpiod_set_consumer_name(gpiod, "GPIO extension enable");
	gpio_ext->enable = gpiod;

	return devm_add_action_or_reset(dev, netxbig_gpio_ext_remove, gpio_ext);
}

static int netxbig_leds_get_of_pdata(struct device *dev,
				     struct netxbig_led_platform_data *pdata)
{
	struct device_node *np = dev_of_node(dev);
	struct device_node *gpio_ext_np;
	struct platform_device *gpio_ext_pdev;
	struct device *gpio_ext_dev;
	struct netxbig_gpio_ext *gpio_ext;
	struct netxbig_led_timer *timers;
	struct netxbig_led *leds, *led;
	int num_timers;
	int num_leds = 0;
	int ret;
	int i;

	/* GPIO extension */
	gpio_ext_np = of_parse_phandle(np, "gpio-ext", 0);
	if (!gpio_ext_np) {
		dev_err(dev, "Failed to get DT handle gpio-ext\n");
		return -EINVAL;
	}
	gpio_ext_pdev = of_find_device_by_node(gpio_ext_np);
	if (!gpio_ext_pdev) {
		dev_err(dev, "Failed to find platform device for gpio-ext\n");
		return -ENODEV;
	}
	gpio_ext_dev = &gpio_ext_pdev->dev;

	gpio_ext = devm_kzalloc(dev, sizeof(*gpio_ext), GFP_KERNEL);
	if (!gpio_ext) {
		of_node_put(gpio_ext_np);
		ret = -ENOMEM;
		goto put_device;
	}
	ret = netxbig_gpio_ext_get(dev, gpio_ext_dev, gpio_ext);
	of_node_put(gpio_ext_np);
	if (ret)
		goto put_device;
	pdata->gpio_ext = gpio_ext;

	/* Timers (optional) */
	ret = of_property_count_u32_elems(np, "timers");
	if (ret > 0) {
		if (ret % 3) {
			ret = -EINVAL;
			goto put_device;
		}

		num_timers = ret / 3;
		timers = devm_kcalloc(dev, num_timers, sizeof(*timers),
				      GFP_KERNEL);
		if (!timers) {
			ret = -ENOMEM;
			goto put_device;
		}
		for (i = 0; i < num_timers; i++) {
			u32 tmp;

			of_property_read_u32_index(np, "timers", 3 * i,
						   &timers[i].mode);
			if (timers[i].mode >= NETXBIG_LED_MODE_NUM) {
				ret = -EINVAL;
				goto put_device;
			}
			of_property_read_u32_index(np, "timers",
						   3 * i + 1, &tmp);
			timers[i].delay_on = tmp;
			of_property_read_u32_index(np, "timers",
						   3 * i + 2, &tmp);
			timers[i].delay_off = tmp;
		}
		pdata->timer = timers;
		pdata->num_timer = num_timers;
	}

	/* LEDs */
	num_leds = of_get_available_child_count(np);
	if (!num_leds) {
		dev_err(dev, "No LED subnodes found in DT\n");
		ret = -ENODEV;
		goto put_device;
	}

	leds = devm_kcalloc(dev, num_leds, sizeof(*leds), GFP_KERNEL);
	if (!leds) {
		ret = -ENOMEM;
		goto put_device;
	}

	led = leds;
	for_each_available_child_of_node_scoped(np, child) {
		const char *string;
		int *mode_val;
		int num_modes;

		ret = of_property_read_u32(child, "mode-addr",
					   &led->mode_addr);
		if (ret)
			goto put_device;

		ret = of_property_read_u32(child, "bright-addr",
					   &led->bright_addr);
		if (ret)
			goto put_device;

		ret = of_property_read_u32(child, "max-brightness",
					   &led->bright_max);
		if (ret)
			goto put_device;

		mode_val =
			devm_kcalloc(dev,
				     NETXBIG_LED_MODE_NUM, sizeof(*mode_val),
				     GFP_KERNEL);
		if (!mode_val) {
			ret = -ENOMEM;
			goto put_device;
		}

		for (i = 0; i < NETXBIG_LED_MODE_NUM; i++)
			mode_val[i] = NETXBIG_LED_INVALID_MODE;

		ret = of_property_count_u32_elems(child, "mode-val");
		if (ret < 0 || ret % 2) {
			ret = -EINVAL;
			goto put_device;
		}
		num_modes = ret / 2;
		if (num_modes > NETXBIG_LED_MODE_NUM) {
			ret = -EINVAL;
			goto put_device;
		}

		for (i = 0; i < num_modes; i++) {
			int mode;
			int val;

			of_property_read_u32_index(child,
						   "mode-val", 2 * i, &mode);
			of_property_read_u32_index(child,
						   "mode-val", 2 * i + 1, &val);
			if (mode >= NETXBIG_LED_MODE_NUM) {
				ret = -EINVAL;
				goto put_device;
			}
			mode_val[mode] = val;
		}
		led->mode_val = mode_val;

		if (!of_property_read_string(child, "label", &string))
			led->name = string;
		else
			led->name = child->name;

		if (!of_property_read_string(child,
					     "linux,default-trigger", &string))
			led->default_trigger = string;

		led++;
	}

	pdata->leds = leds;
	pdata->num_leds = num_leds;

	return 0;

put_device:
	put_device(gpio_ext_dev);
	return ret;
}

static const struct of_device_id of_netxbig_leds_match[] = {
	{ .compatible = "lacie,netxbig-leds", },
	{},
};
MODULE_DEVICE_TABLE(of, of_netxbig_leds_match);

static int netxbig_led_probe(struct platform_device *pdev)
{
	struct netxbig_led_platform_data *pdata;
	struct netxbig_led_data *leds_data;
	int i;
	int ret;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	ret = netxbig_leds_get_of_pdata(&pdev->dev, pdata);
	if (ret)
		return ret;

	leds_data = devm_kcalloc(&pdev->dev,
				 pdata->num_leds, sizeof(*leds_data),
				 GFP_KERNEL);
	if (!leds_data)
		return -ENOMEM;

	for (i = 0; i < pdata->num_leds; i++) {
		ret = create_netxbig_led(pdev, pdata,
					 &leds_data[i], &pdata->leds[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static struct platform_driver netxbig_led_driver = {
	.probe		= netxbig_led_probe,
	.driver		= {
		.name		= "leds-netxbig",
		.of_match_table	= of_netxbig_leds_match,
	},
};

module_platform_driver(netxbig_led_driver);

MODULE_AUTHOR("Simon Guinot <sguinot@lacie.com>");
MODULE_DESCRIPTION("LED driver for LaCie xBig Network boards");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-netxbig");
