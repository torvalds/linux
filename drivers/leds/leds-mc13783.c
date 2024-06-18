// SPDX-License-Identifier: GPL-2.0-only
/*
 * LEDs driver for Freescale MC13783/MC13892/MC34708
 *
 * Copyright (C) 2010 Philippe Rétornaz
 *
 * Based on leds-da903x:
 * Copyright (C) 2008 Compulab, Ltd.
 *      Mike Rapoport <mike@compulab.co.il>
 *
 * Copyright (C) 2006-2008 Marvell International Ltd.
 *      Eric Miao <eric.miao@marvell.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/of.h>
#include <linux/mfd/mc13xxx.h>

struct mc13xxx_led_devtype {
	int	led_min;
	int	led_max;
	int	num_regs;
	u32	ledctrl_base;
};

struct mc13xxx_led {
	struct led_classdev	cdev;
	int			id;
	struct mc13xxx_leds	*leds;
};

struct mc13xxx_leds {
	struct mc13xxx			*master;
	struct mc13xxx_led_devtype	*devtype;
	int				num_leds;
	struct mc13xxx_led		*led;
};

static unsigned int mc13xxx_max_brightness(int id)
{
	if (id >= MC13783_LED_MD && id <= MC13783_LED_KP)
		return 0x0f;
	else if (id >= MC13783_LED_R1 && id <= MC13783_LED_B3)
		return 0x1f;

	return 0x3f;
}

static int mc13xxx_led_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	struct mc13xxx_led *led =
		container_of(led_cdev, struct mc13xxx_led, cdev);
	struct mc13xxx_leds *leds = led->leds;
	unsigned int reg, bank, off, shift;

	switch (led->id) {
	case MC13783_LED_MD:
	case MC13783_LED_AD:
	case MC13783_LED_KP:
		reg = 2;
		shift = 9 + (led->id - MC13783_LED_MD) * 4;
		break;
	case MC13783_LED_R1:
	case MC13783_LED_G1:
	case MC13783_LED_B1:
	case MC13783_LED_R2:
	case MC13783_LED_G2:
	case MC13783_LED_B2:
	case MC13783_LED_R3:
	case MC13783_LED_G3:
	case MC13783_LED_B3:
		off = led->id - MC13783_LED_R1;
		bank = off / 3;
		reg = 3 + bank;
		shift = (off - bank * 3) * 5 + 6;
		break;
	case MC13892_LED_MD:
	case MC13892_LED_AD:
	case MC13892_LED_KP:
		off = led->id - MC13892_LED_MD;
		reg = off / 2;
		shift = 3 + (off - reg * 2) * 12;
		break;
	case MC13892_LED_R:
	case MC13892_LED_G:
	case MC13892_LED_B:
		off = led->id - MC13892_LED_R;
		bank = off / 2;
		reg = 2 + bank;
		shift = (off - bank * 2) * 12 + 3;
		break;
	case MC34708_LED_R:
	case MC34708_LED_G:
		reg = 0;
		shift = 3 + (led->id - MC34708_LED_R) * 12;
		break;
	default:
		BUG();
	}

	return mc13xxx_reg_rmw(leds->master, leds->devtype->ledctrl_base + reg,
			mc13xxx_max_brightness(led->id) << shift,
			value << shift);
}

#ifdef CONFIG_OF
static struct mc13xxx_leds_platform_data __init *mc13xxx_led_probe_dt(
	struct platform_device *pdev)
{
	struct mc13xxx_leds *leds = platform_get_drvdata(pdev);
	struct mc13xxx_leds_platform_data *pdata;
	struct device_node *parent, *child;
	struct device *dev = &pdev->dev;
	int i = 0, ret = -ENODATA;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	parent = of_get_child_by_name(dev_of_node(dev->parent), "leds");
	if (!parent)
		goto out_node_put;

	ret = of_property_read_u32_array(parent, "led-control",
					 pdata->led_control,
					 leds->devtype->num_regs);
	if (ret)
		goto out_node_put;

	pdata->num_leds = of_get_available_child_count(parent);

	pdata->led = devm_kcalloc(dev, pdata->num_leds, sizeof(*pdata->led),
				  GFP_KERNEL);
	if (!pdata->led) {
		ret = -ENOMEM;
		goto out_node_put;
	}

	for_each_available_child_of_node(parent, child) {
		const char *str;
		u32 tmp;

		if (of_property_read_u32(child, "reg", &tmp))
			continue;
		pdata->led[i].id = leds->devtype->led_min + tmp;

		if (!of_property_read_string(child, "label", &str))
			pdata->led[i].name = str;
		if (!of_property_read_string(child, "linux,default-trigger",
					     &str))
			pdata->led[i].default_trigger = str;

		i++;
	}

	pdata->num_leds = i;
	ret = i > 0 ? 0 : -ENODATA;

out_node_put:
	of_node_put(parent);

	return ret ? ERR_PTR(ret) : pdata;
}
#else
static inline struct mc13xxx_leds_platform_data __init *mc13xxx_led_probe_dt(
	struct platform_device *pdev)
{
	return ERR_PTR(-ENOSYS);
}
#endif

static int __init mc13xxx_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mc13xxx_leds_platform_data *pdata = dev_get_platdata(dev);
	struct mc13xxx *mcdev = dev_get_drvdata(dev->parent);
	struct mc13xxx_led_devtype *devtype =
		(struct mc13xxx_led_devtype *)pdev->id_entry->driver_data;
	struct mc13xxx_leds *leds;
	int i, id, ret = -ENODATA;
	u32 init_led = 0;

	leds = devm_kzalloc(dev, sizeof(*leds), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	leds->devtype = devtype;
	leds->master = mcdev;
	platform_set_drvdata(pdev, leds);

	if (dev_of_node(dev->parent)) {
		pdata = mc13xxx_led_probe_dt(pdev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	} else if (!pdata)
		return -ENODATA;

	leds->num_leds = pdata->num_leds;

	if ((leds->num_leds < 1) ||
	    (leds->num_leds > (devtype->led_max - devtype->led_min + 1))) {
		dev_err(dev, "Invalid LED count %d\n", leds->num_leds);
		return -EINVAL;
	}

	leds->led = devm_kcalloc(dev, leds->num_leds, sizeof(*leds->led),
				 GFP_KERNEL);
	if (!leds->led)
		return -ENOMEM;

	for (i = 0; i < devtype->num_regs; i++) {
		ret = mc13xxx_reg_write(mcdev, leds->devtype->ledctrl_base + i,
					pdata->led_control[i]);
		if (ret)
			return ret;
	}

	for (i = 0; i < leds->num_leds; i++) {
		const char *name, *trig;

		ret = -EINVAL;

		id = pdata->led[i].id;
		name = pdata->led[i].name;
		trig = pdata->led[i].default_trigger;

		if ((id > devtype->led_max) || (id < devtype->led_min)) {
			dev_err(dev, "Invalid ID %i\n", id);
			break;
		}

		if (init_led & (1 << id)) {
			dev_warn(dev, "LED %i already initialized\n", id);
			break;
		}

		init_led |= 1 << id;
		leds->led[i].id = id;
		leds->led[i].leds = leds;
		leds->led[i].cdev.name = name;
		leds->led[i].cdev.default_trigger = trig;
		leds->led[i].cdev.flags = LED_CORE_SUSPENDRESUME;
		leds->led[i].cdev.brightness_set_blocking = mc13xxx_led_set;
		leds->led[i].cdev.max_brightness = mc13xxx_max_brightness(id);

		ret = led_classdev_register(dev->parent, &leds->led[i].cdev);
		if (ret) {
			dev_err(dev, "Failed to register LED %i\n", id);
			break;
		}
	}

	if (ret)
		while (--i >= 0)
			led_classdev_unregister(&leds->led[i].cdev);

	return ret;
}

static void mc13xxx_led_remove(struct platform_device *pdev)
{
	struct mc13xxx_leds *leds = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < leds->num_leds; i++)
		led_classdev_unregister(&leds->led[i].cdev);
}

static const struct mc13xxx_led_devtype mc13783_led_devtype = {
	.led_min	= MC13783_LED_MD,
	.led_max	= MC13783_LED_B3,
	.num_regs	= 6,
	.ledctrl_base	= 51,
};

static const struct mc13xxx_led_devtype mc13892_led_devtype = {
	.led_min	= MC13892_LED_MD,
	.led_max	= MC13892_LED_B,
	.num_regs	= 4,
	.ledctrl_base	= 51,
};

static const struct mc13xxx_led_devtype mc34708_led_devtype = {
	.led_min	= MC34708_LED_R,
	.led_max	= MC34708_LED_G,
	.num_regs	= 1,
	.ledctrl_base	= 54,
};

static const struct platform_device_id mc13xxx_led_id_table[] = {
	{ "mc13783-led", (kernel_ulong_t)&mc13783_led_devtype, },
	{ "mc13892-led", (kernel_ulong_t)&mc13892_led_devtype, },
	{ "mc34708-led", (kernel_ulong_t)&mc34708_led_devtype, },
	{ }
};
MODULE_DEVICE_TABLE(platform, mc13xxx_led_id_table);

static struct platform_driver mc13xxx_led_driver = {
	.driver	= {
		.name	= "mc13xxx-led",
	},
	.remove_new	= mc13xxx_led_remove,
	.id_table	= mc13xxx_led_id_table,
};
module_platform_driver_probe(mc13xxx_led_driver, mc13xxx_led_probe);

MODULE_DESCRIPTION("LEDs driver for Freescale MC13XXX PMIC");
MODULE_AUTHOR("Philippe Retornaz <philippe.retornaz@epfl.ch>");
MODULE_LICENSE("GPL");
