/*
 * dm355evm_msp.c - driver for MSP430 firmware on DM355EVM board
 *
 * Copyright (C) 2008 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/i2c.h>
#include <linux/i2c/dm355evm_msp.h>


/*
 * The DM355 is a DaVinci chip with video support but no C64+ DSP.  Its
 * EVM board has an MSP430 programmed with firmware for various board
 * support functions.  This driver exposes some of them directly, and
 * supports other drivers (e.g. RTC, input) for more complex access.
 *
 * Because this firmware is entirely board-specific, this file embeds
 * knowledge that would be passed as platform_data in a generic driver.
 *
 * This driver was tested with firmware revision A4.
 */

#if defined(CONFIG_INPUT_DM355EVM) || defined(CONFIG_INPUT_DM355EVM_MODULE)
#define msp_has_keyboard()	true
#else
#define msp_has_keyboard()	false
#endif

#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
#define msp_has_leds()		true
#else
#define msp_has_leds()		false
#endif

#if defined(CONFIG_RTC_DRV_DM355EVM) || defined(CONFIG_RTC_DRV_DM355EVM_MODULE)
#define msp_has_rtc()		true
#else
#define msp_has_rtc()		false
#endif

#if defined(CONFIG_VIDEO_TVP514X) || defined(CONFIG_VIDEO_TVP514X_MODULE)
#define msp_has_tvp()		true
#else
#define msp_has_tvp()		false
#endif


/*----------------------------------------------------------------------*/

/* REVISIT for paranoia's sake, retry reads/writes on error */

static struct i2c_client *msp430;

/**
 * dm355evm_msp_write - Writes a register in dm355evm_msp
 * @value: the value to be written
 * @reg: register address
 *
 * Returns result of operation - 0 is success, else negative errno
 */
int dm355evm_msp_write(u8 value, u8 reg)
{
	return i2c_smbus_write_byte_data(msp430, reg, value);
}
EXPORT_SYMBOL(dm355evm_msp_write);

/**
 * dm355evm_msp_read - Reads a register from dm355evm_msp
 * @reg: register address
 *
 * Returns result of operation - value, or negative errno
 */
int dm355evm_msp_read(u8 reg)
{
	return i2c_smbus_read_byte_data(msp430, reg);
}
EXPORT_SYMBOL(dm355evm_msp_read);

/*----------------------------------------------------------------------*/

/*
 * Many of the msp430 pins are just used as fixed-direction GPIOs.
 * We could export a few more of them this way, if we wanted.
 */
#define MSP_GPIO(bit,reg)	((DM355EVM_MSP_ ## reg) << 3 | (bit))

static const u8 msp_gpios[] = {
	/* eight leds */
	MSP_GPIO(0, LED), MSP_GPIO(1, LED),
	MSP_GPIO(2, LED), MSP_GPIO(3, LED),
	MSP_GPIO(4, LED), MSP_GPIO(5, LED),
	MSP_GPIO(6, LED), MSP_GPIO(7, LED),
	/* SW6 and the NTSC/nPAL jumper */
	MSP_GPIO(0, SWITCH1), MSP_GPIO(1, SWITCH1),
	MSP_GPIO(2, SWITCH1), MSP_GPIO(3, SWITCH1),
	MSP_GPIO(4, SWITCH1),
	/* switches on MMC/SD sockets */
	/*
	 * Note: EVMDM355_ECP_VA4.pdf suggests that Bit 2 and 4 should be
	 * checked for card detection. However on the EVM bit 1 and 3 gives
	 * this status, for 0 and 1 instance respectively. The pdf also
	 * suggests that Bit 1 and 3 should be checked for write protection.
	 * However on the EVM bit 2 and 4 gives this status,for 0 and 1
	 * instance respectively.
	 */
	MSP_GPIO(2, SDMMC), MSP_GPIO(1, SDMMC),	/* mmc0 WP, nCD */
	MSP_GPIO(4, SDMMC), MSP_GPIO(3, SDMMC),	/* mmc1 WP, nCD */
};

#define MSP_GPIO_REG(offset)	(msp_gpios[(offset)] >> 3)
#define MSP_GPIO_MASK(offset)	BIT(msp_gpios[(offset)] & 0x07)

static int msp_gpio_in(struct gpio_chip *chip, unsigned offset)
{
	switch (MSP_GPIO_REG(offset)) {
	case DM355EVM_MSP_SWITCH1:
	case DM355EVM_MSP_SWITCH2:
	case DM355EVM_MSP_SDMMC:
		return 0;
	default:
		return -EINVAL;
	}
}

static u8 msp_led_cache;

static int msp_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	int reg, status;

	reg = MSP_GPIO_REG(offset);
	status = dm355evm_msp_read(reg);
	if (status < 0)
		return status;
	if (reg == DM355EVM_MSP_LED)
		msp_led_cache = status;
	return status & MSP_GPIO_MASK(offset);
}

static int msp_gpio_out(struct gpio_chip *chip, unsigned offset, int value)
{
	int mask, bits;

	/* NOTE:  there are some other signals that could be
	 * packaged as output GPIOs, but they aren't as useful
	 * as the LEDs ... so for now we don't.
	 */
	if (MSP_GPIO_REG(offset) != DM355EVM_MSP_LED)
		return -EINVAL;

	mask = MSP_GPIO_MASK(offset);
	bits = msp_led_cache;

	bits &= ~mask;
	if (value)
		bits |= mask;
	msp_led_cache = bits;

	return dm355evm_msp_write(bits, DM355EVM_MSP_LED);
}

static void msp_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	msp_gpio_out(chip, offset, value);
}

static struct gpio_chip dm355evm_msp_gpio = {
	.label			= "dm355evm_msp",
	.owner			= THIS_MODULE,
	.direction_input	= msp_gpio_in,
	.get			= msp_gpio_get,
	.direction_output	= msp_gpio_out,
	.set			= msp_gpio_set,
	.base			= -EINVAL,		/* dynamic assignment */
	.ngpio			= ARRAY_SIZE(msp_gpios),
	.can_sleep		= true,
};

/*----------------------------------------------------------------------*/

static struct device *add_child(struct i2c_client *client, const char *name,
		void *pdata, unsigned pdata_len,
		bool can_wakeup, int irq)
{
	struct platform_device	*pdev;
	int			status;

	pdev = platform_device_alloc(name, -1);
	if (!pdev) {
		dev_dbg(&client->dev, "can't alloc dev\n");
		status = -ENOMEM;
		goto err;
	}

	device_init_wakeup(&pdev->dev, can_wakeup);
	pdev->dev.parent = &client->dev;

	if (pdata) {
		status = platform_device_add_data(pdev, pdata, pdata_len);
		if (status < 0) {
			dev_dbg(&pdev->dev, "can't add platform_data\n");
			goto err;
		}
	}

	if (irq) {
		struct resource r = {
			.start = irq,
			.flags = IORESOURCE_IRQ,
		};

		status = platform_device_add_resources(pdev, &r, 1);
		if (status < 0) {
			dev_dbg(&pdev->dev, "can't add irq\n");
			goto err;
		}
	}

	status = platform_device_add(pdev);

err:
	if (status < 0) {
		platform_device_put(pdev);
		dev_err(&client->dev, "can't add %s dev\n", name);
		return ERR_PTR(status);
	}
	return &pdev->dev;
}

static int add_children(struct i2c_client *client)
{
	static const struct {
		int offset;
		char *label;
	} config_inputs[] = {
		/* 8 == right after the LEDs */
		{ 8 + 0, "sw6_1", },
		{ 8 + 1, "sw6_2", },
		{ 8 + 2, "sw6_3", },
		{ 8 + 3, "sw6_4", },
		{ 8 + 4, "NTSC/nPAL", },
	};

	struct device	*child;
	int		status;
	int		i;

	/* GPIO-ish stuff */
	dm355evm_msp_gpio.dev = &client->dev;
	status = gpiochip_add(&dm355evm_msp_gpio);
	if (status < 0)
		return status;

	/* LED output */
	if (msp_has_leds()) {
#define GPIO_LED(l)	.name = l, .active_low = true
		static struct gpio_led evm_leds[] = {
			{ GPIO_LED("dm355evm::ds14"),
				.default_trigger = "heartbeat", },
			{ GPIO_LED("dm355evm::ds15"),
				.default_trigger = "mmc0", },
			{ GPIO_LED("dm355evm::ds16"),
				/* could also be a CE-ATA drive */
				.default_trigger = "mmc1", },
			{ GPIO_LED("dm355evm::ds17"),
				.default_trigger = "nand-disk", },
			{ GPIO_LED("dm355evm::ds18"), },
			{ GPIO_LED("dm355evm::ds19"), },
			{ GPIO_LED("dm355evm::ds20"), },
			{ GPIO_LED("dm355evm::ds21"), },
		};
#undef GPIO_LED

		struct gpio_led_platform_data evm_led_data = {
			.num_leds	= ARRAY_SIZE(evm_leds),
			.leds		= evm_leds,
		};

		for (i = 0; i < ARRAY_SIZE(evm_leds); i++)
			evm_leds[i].gpio = i + dm355evm_msp_gpio.base;

		/* NOTE:  these are the only fully programmable LEDs
		 * on the board, since GPIO-61/ds22 (and many signals
		 * going to DC7) must be used for AEMIF address lines
		 * unless the top 1 GB of NAND is unused...
		 */
		child = add_child(client, "leds-gpio",
				&evm_led_data, sizeof(evm_led_data),
				false, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	/* configuration inputs */
	for (i = 0; i < ARRAY_SIZE(config_inputs); i++) {
		int gpio = dm355evm_msp_gpio.base + config_inputs[i].offset;

		gpio_request_one(gpio, GPIOF_IN, config_inputs[i].label);

		/* make it easy for userspace to see these */
		gpio_export(gpio, false);
	}

	/* MMC/SD inputs -- right after the last config input */
	if (dev_get_platdata(&client->dev)) {
		void (*mmcsd_setup)(unsigned) = dev_get_platdata(&client->dev);

		mmcsd_setup(dm355evm_msp_gpio.base + 8 + 5);
	}

	/* RTC is a 32 bit counter, no alarm */
	if (msp_has_rtc()) {
		child = add_child(client, "rtc-dm355evm",
				NULL, 0, false, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	/* input from buttons and IR remote (uses the IRQ) */
	if (msp_has_keyboard()) {
		child = add_child(client, "dm355evm_keys",
				NULL, 0, true, client->irq);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	return 0;
}

/*----------------------------------------------------------------------*/

static void dm355evm_command(unsigned command)
{
	int status;

	status = dm355evm_msp_write(command, DM355EVM_MSP_COMMAND);
	if (status < 0)
		dev_err(&msp430->dev, "command %d failure %d\n",
				command, status);
}

static void dm355evm_power_off(void)
{
	dm355evm_command(MSP_COMMAND_POWEROFF);
}

static int dm355evm_msp_remove(struct i2c_client *client)
{
	pm_power_off = NULL;
	msp430 = NULL;
	return 0;
}

static int
dm355evm_msp_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int		status;
	const char	*video = msp_has_tvp() ? "TVP5146" : "imager";

	if (msp430)
		return -EBUSY;
	msp430 = client;

	/* display revision status; doubles as sanity check */
	status = dm355evm_msp_read(DM355EVM_MSP_FIRMREV);
	if (status < 0)
		goto fail;
	dev_info(&client->dev, "firmware v.%02X, %s as video-in\n",
			status, video);

	/* mux video input:  either tvp5146 or some external imager */
	status = dm355evm_msp_write(msp_has_tvp() ? 0 : MSP_VIDEO_IMAGER,
			DM355EVM_MSP_VIDEO_IN);
	if (status < 0)
		dev_warn(&client->dev, "error %d muxing %s as video-in\n",
			status, video);

	/* init LED cache, and turn off the LEDs */
	msp_led_cache = 0xff;
	dm355evm_msp_write(msp_led_cache, DM355EVM_MSP_LED);

	/* export capabilities we support */
	status = add_children(client);
	if (status < 0)
		goto fail;

	/* PM hookup */
	pm_power_off = dm355evm_power_off;

	return 0;

fail:
	/* FIXME remove children ... */
	dm355evm_msp_remove(client);
	return status;
}

static const struct i2c_device_id dm355evm_msp_ids[] = {
	{ "dm355evm_msp", 0 },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(i2c, dm355evm_msp_ids);

static struct i2c_driver dm355evm_msp_driver = {
	.driver.name	= "dm355evm_msp",
	.id_table	= dm355evm_msp_ids,
	.probe		= dm355evm_msp_probe,
	.remove		= dm355evm_msp_remove,
};

static int __init dm355evm_msp_init(void)
{
	return i2c_add_driver(&dm355evm_msp_driver);
}
subsys_initcall(dm355evm_msp_init);

static void __exit dm355evm_msp_exit(void)
{
	i2c_del_driver(&dm355evm_msp_driver);
}
module_exit(dm355evm_msp_exit);

MODULE_DESCRIPTION("Interface to MSP430 firmware on DM355EVM");
MODULE_LICENSE("GPL");
