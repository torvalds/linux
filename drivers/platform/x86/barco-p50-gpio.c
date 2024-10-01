// SPDX-License-Identifier: GPL-2.0+

/*
 * Support for EC-connected GPIOs for identify
 * LED/button on Barco P50 board
 *
 * Copyright (C) 2021 Barco NV
 * Author: Santosh Kumar Yadav <santoshkumar.yadav@barco.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio_keys.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/input.h>


#define DRIVER_NAME		"barco-p50-gpio"

/* GPIO lines */
#define P50_GPIO_LINE_LED	0
#define P50_GPIO_LINE_BTN	1

/* GPIO IO Ports */
#define P50_GPIO_IO_PORT_BASE	0x299

#define P50_PORT_DATA		0x00
#define P50_PORT_CMD		0x01

#define P50_STATUS_OBF		0x01 /* EC output buffer full */
#define P50_STATUS_IBF		0x02 /* EC input buffer full */

#define P50_CMD_READ		0xa0
#define P50_CMD_WRITE		0x50

/* EC mailbox registers */
#define P50_MBOX_REG_CMD	0x00
#define P50_MBOX_REG_STATUS	0x01
#define P50_MBOX_REG_PARAM	0x02
#define P50_MBOX_REG_DATA	0x03

#define P50_MBOX_CMD_READ_GPIO	0x11
#define P50_MBOX_CMD_WRITE_GPIO	0x12
#define P50_MBOX_CMD_CLEAR	0xff

#define P50_MBOX_STATUS_SUCCESS	0x01

#define P50_MBOX_PARAM_LED	0x12
#define P50_MBOX_PARAM_BTN	0x13


struct p50_gpio {
	struct gpio_chip gc;
	struct mutex lock;
	unsigned long base;
	struct platform_device *leds_pdev;
	struct platform_device *keys_pdev;
};

static struct platform_device *gpio_pdev;

static int gpio_params[] = {
	[P50_GPIO_LINE_LED] = P50_MBOX_PARAM_LED,
	[P50_GPIO_LINE_BTN] = P50_MBOX_PARAM_BTN,
};

static const char * const gpio_names[] = {
	[P50_GPIO_LINE_LED] = "identify-led",
	[P50_GPIO_LINE_BTN] = "identify-button",
};


static struct gpiod_lookup_table p50_gpio_led_table = {
	.dev_id = "leds-gpio",
	.table = {
		GPIO_LOOKUP_IDX(DRIVER_NAME, P50_GPIO_LINE_LED, NULL, 0, GPIO_ACTIVE_HIGH),
		{}
	}
};

/* GPIO LEDs */
static struct gpio_led leds[] = {
	{ .name = "identify" }
};

static struct gpio_led_platform_data leds_pdata = {
	.num_leds = ARRAY_SIZE(leds),
	.leds = leds,
};

/* GPIO keyboard */
static struct gpio_keys_button buttons[] = {
	{
		.code = KEY_VENDOR,
		.gpio = P50_GPIO_LINE_BTN,
		.active_low = 1,
		.type = EV_KEY,
		.value = 1,
	},
};

static struct gpio_keys_platform_data keys_pdata = {
	.buttons = buttons,
	.nbuttons = ARRAY_SIZE(buttons),
	.poll_interval = 100,
	.rep = 0,
	.name = "identify",
};


/* low level access routines */

static int p50_wait_ec(struct p50_gpio *p50, int mask, int expected)
{
	int i, val;

	for (i = 0; i < 100; i++) {
		val = inb(p50->base + P50_PORT_CMD) & mask;
		if (val == expected)
			return 0;
		usleep_range(500, 2000);
	}

	dev_err(p50->gc.parent, "Timed out waiting for EC (0x%x)\n", val);
	return -ETIMEDOUT;
}


static int p50_read_mbox_reg(struct p50_gpio *p50, int reg)
{
	int ret;

	ret = p50_wait_ec(p50, P50_STATUS_IBF, 0);
	if (ret)
		return ret;

	/* clear output buffer flag, prevent unfinished commands */
	inb(p50->base + P50_PORT_DATA);

	/* cmd/address */
	outb(P50_CMD_READ | reg, p50->base + P50_PORT_CMD);

	ret = p50_wait_ec(p50, P50_STATUS_OBF, P50_STATUS_OBF);
	if (ret)
		return ret;

	return inb(p50->base + P50_PORT_DATA);
}

static int p50_write_mbox_reg(struct p50_gpio *p50, int reg, int val)
{
	int ret;

	ret = p50_wait_ec(p50, P50_STATUS_IBF, 0);
	if (ret)
		return ret;

	/* cmd/address */
	outb(P50_CMD_WRITE | reg, p50->base + P50_PORT_CMD);

	ret = p50_wait_ec(p50, P50_STATUS_IBF, 0);
	if (ret)
		return ret;

	/* data */
	outb(val, p50->base + P50_PORT_DATA);

	return 0;
}


/* mbox routines */

static int p50_wait_mbox_idle(struct p50_gpio *p50)
{
	int i, val;

	for (i = 0; i < 1000; i++) {
		val = p50_read_mbox_reg(p50, P50_MBOX_REG_CMD);
		/* cmd is 0 when idle */
		if (val <= 0)
			return val;

		usleep_range(500, 2000);
	}

	dev_err(p50->gc.parent,	"Timed out waiting for EC mbox idle (CMD: 0x%x)\n", val);

	return -ETIMEDOUT;
}

static int p50_send_mbox_cmd(struct p50_gpio *p50, int cmd, int param, int data)
{
	int ret;

	ret = p50_wait_mbox_idle(p50);
	if (ret)
		return ret;

	ret = p50_write_mbox_reg(p50, P50_MBOX_REG_DATA, data);
	if (ret)
		return ret;

	ret = p50_write_mbox_reg(p50, P50_MBOX_REG_PARAM, param);
	if (ret)
		return ret;

	ret = p50_write_mbox_reg(p50, P50_MBOX_REG_CMD, cmd);
	if (ret)
		return ret;

	ret = p50_wait_mbox_idle(p50);
	if (ret)
		return ret;

	ret = p50_read_mbox_reg(p50, P50_MBOX_REG_STATUS);
	if (ret < 0)
		return ret;

	if (ret == P50_MBOX_STATUS_SUCCESS)
		return 0;

	dev_err(p50->gc.parent,	"Mbox command failed (CMD=0x%x STAT=0x%x PARAM=0x%x DATA=0x%x)\n",
		cmd, ret, param, data);

	return -EIO;
}


/* gpio routines */

static int p50_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	switch (offset) {
	case P50_GPIO_LINE_BTN:
		return GPIO_LINE_DIRECTION_IN;

	case P50_GPIO_LINE_LED:
		return GPIO_LINE_DIRECTION_OUT;

	default:
		return -EINVAL;
	}
}

static int p50_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct p50_gpio *p50 = gpiochip_get_data(gc);
	int ret;

	mutex_lock(&p50->lock);

	ret = p50_send_mbox_cmd(p50, P50_MBOX_CMD_READ_GPIO, gpio_params[offset], 0);
	if (ret == 0)
		ret = p50_read_mbox_reg(p50, P50_MBOX_REG_DATA);

	mutex_unlock(&p50->lock);

	return ret;
}

static void p50_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct p50_gpio *p50 = gpiochip_get_data(gc);

	mutex_lock(&p50->lock);

	p50_send_mbox_cmd(p50, P50_MBOX_CMD_WRITE_GPIO, gpio_params[offset], value);

	mutex_unlock(&p50->lock);
}

static int p50_gpio_probe(struct platform_device *pdev)
{
	struct p50_gpio *p50;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res) {
		dev_err(&pdev->dev, "Cannot get I/O ports\n");
		return -ENODEV;
	}

	if (!devm_request_region(&pdev->dev, res->start, resource_size(res), pdev->name)) {
		dev_err(&pdev->dev, "Unable to reserve I/O region\n");
		return -EBUSY;
	}

	p50 = devm_kzalloc(&pdev->dev, sizeof(*p50), GFP_KERNEL);
	if (!p50)
		return -ENOMEM;

	platform_set_drvdata(pdev, p50);
	mutex_init(&p50->lock);
	p50->base = res->start;
	p50->gc.owner = THIS_MODULE;
	p50->gc.parent = &pdev->dev;
	p50->gc.label = dev_name(&pdev->dev);
	p50->gc.ngpio = ARRAY_SIZE(gpio_names);
	p50->gc.names = gpio_names;
	p50->gc.can_sleep = true;
	p50->gc.base = -1;
	p50->gc.get_direction = p50_gpio_get_direction;
	p50->gc.get = p50_gpio_get;
	p50->gc.set = p50_gpio_set;


	/* reset mbox */
	ret = p50_wait_mbox_idle(p50);
	if (ret)
		return ret;

	ret = p50_write_mbox_reg(p50, P50_MBOX_REG_CMD, P50_MBOX_CMD_CLEAR);
	if (ret)
		return ret;

	ret = p50_wait_mbox_idle(p50);
	if (ret)
		return ret;


	ret = devm_gpiochip_add_data(&pdev->dev, &p50->gc, p50);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip: %d\n", ret);
		return ret;
	}

	gpiod_add_lookup_table(&p50_gpio_led_table);

	p50->leds_pdev = platform_device_register_data(&pdev->dev,
		"leds-gpio", PLATFORM_DEVID_NONE, &leds_pdata, sizeof(leds_pdata));

	if (IS_ERR(p50->leds_pdev)) {
		ret = PTR_ERR(p50->leds_pdev);
		dev_err(&pdev->dev, "Could not register leds-gpio: %d\n", ret);
		goto err_leds;
	}

	/* gpio-keys-polled uses old-style gpio interface, pass the right identifier */
	buttons[0].gpio += p50->gc.base;

	p50->keys_pdev =
		platform_device_register_data(&pdev->dev, "gpio-keys-polled",
					      PLATFORM_DEVID_NONE,
					      &keys_pdata, sizeof(keys_pdata));

	if (IS_ERR(p50->keys_pdev)) {
		ret = PTR_ERR(p50->keys_pdev);
		dev_err(&pdev->dev, "Could not register gpio-keys-polled: %d\n", ret);
		goto err_keys;
	}

	return 0;

err_keys:
	platform_device_unregister(p50->leds_pdev);
err_leds:
	gpiod_remove_lookup_table(&p50_gpio_led_table);

	return ret;
}

static int p50_gpio_remove(struct platform_device *pdev)
{
	struct p50_gpio *p50 = platform_get_drvdata(pdev);

	platform_device_unregister(p50->keys_pdev);
	platform_device_unregister(p50->leds_pdev);

	gpiod_remove_lookup_table(&p50_gpio_led_table);

	return 0;
}

static struct platform_driver p50_gpio_driver = {
	.driver = {
		.name = DRIVER_NAME,
	},
	.probe = p50_gpio_probe,
	.remove = p50_gpio_remove,
};

/* Board setup */
static const struct dmi_system_id dmi_ids[] __initconst = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Barco"),
			DMI_EXACT_MATCH(DMI_PRODUCT_FAMILY, "P50")
		},
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, dmi_ids);

static int __init p50_module_init(void)
{
	struct resource res = DEFINE_RES_IO(P50_GPIO_IO_PORT_BASE, P50_PORT_CMD + 1);
	int ret;

	if (!dmi_first_match(dmi_ids))
		return -ENODEV;

	ret = platform_driver_register(&p50_gpio_driver);
	if (ret)
		return ret;

	gpio_pdev = platform_device_register_simple(DRIVER_NAME, PLATFORM_DEVID_NONE, &res, 1);
	if (IS_ERR(gpio_pdev)) {
		pr_err("failed registering %s: %ld\n", DRIVER_NAME, PTR_ERR(gpio_pdev));
		platform_driver_unregister(&p50_gpio_driver);
		return PTR_ERR(gpio_pdev);
	}

	return 0;
}

static void __exit p50_module_exit(void)
{
	platform_device_unregister(gpio_pdev);
	platform_driver_unregister(&p50_gpio_driver);
}

module_init(p50_module_init);
module_exit(p50_module_exit);

MODULE_AUTHOR("Santosh Kumar Yadav, Barco NV <santoshkumar.yadav@barco.com>");
MODULE_DESCRIPTION("Barco P50 identify GPIOs driver");
MODULE_LICENSE("GPL");
