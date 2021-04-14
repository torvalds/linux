// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The Netronix embedded controller is a microcontroller found in some
 * e-book readers designed by the original design manufacturer Netronix, Inc.
 * It contains RTC, battery monitoring, system power management, and PWM
 * functionality.
 *
 * This driver implements register access, version detection, and system
 * power-off/reset.
 *
 * Copyright 2020 Jonathan Neuschäfer <j.neuschaefer@gmx.net>
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/ntxec.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <asm/unaligned.h>

#define NTXEC_REG_VERSION	0x00
#define NTXEC_REG_POWEROFF	0x50
#define NTXEC_REG_POWERKEEP	0x70
#define NTXEC_REG_RESET		0x90

#define NTXEC_POWEROFF_VALUE	0x0100
#define NTXEC_POWERKEEP_VALUE	0x0800
#define NTXEC_RESET_VALUE	0xff00

static struct i2c_client *poweroff_restart_client;

static void ntxec_poweroff(void)
{
	int res;
	u8 buf[3] = { NTXEC_REG_POWEROFF };
	struct i2c_msg msgs[] = {
		{
			.addr = poweroff_restart_client->addr,
			.flags = 0,
			.len = sizeof(buf),
			.buf = buf,
		},
	};

	put_unaligned_be16(NTXEC_POWEROFF_VALUE, buf + 1);

	res = i2c_transfer(poweroff_restart_client->adapter, msgs, ARRAY_SIZE(msgs));
	if (res < 0)
		dev_warn(&poweroff_restart_client->dev,
			 "Failed to power off (err = %d)\n", res);

	/*
	 * The time from the register write until the host CPU is powered off
	 * has been observed to be about 2.5 to 3 seconds. Sleep long enough to
	 * safely avoid returning from the poweroff handler.
	 */
	msleep(5000);
}

static int ntxec_restart(struct notifier_block *nb,
			 unsigned long action, void *data)
{
	int res;
	u8 buf[3] = { NTXEC_REG_RESET };
	/*
	 * NOTE: The lower half of the reset value is not sent, because sending
	 * it causes an I2C error. (The reset handler in the downstream driver
	 * does send the full two-byte value, but doesn't check the result).
	 */
	struct i2c_msg msgs[] = {
		{
			.addr = poweroff_restart_client->addr,
			.flags = 0,
			.len = sizeof(buf) - 1,
			.buf = buf,
		},
	};

	put_unaligned_be16(NTXEC_RESET_VALUE, buf + 1);

	res = i2c_transfer(poweroff_restart_client->adapter, msgs, ARRAY_SIZE(msgs));
	if (res < 0)
		dev_warn(&poweroff_restart_client->dev,
			 "Failed to restart (err = %d)\n", res);

	return NOTIFY_DONE;
}

static struct notifier_block ntxec_restart_handler = {
	.notifier_call = ntxec_restart,
	.priority = 128,
};

static const struct regmap_config regmap_config = {
	.name = "ntxec",
	.reg_bits = 8,
	.val_bits = 16,
	.cache_type = REGCACHE_NONE,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

static const struct mfd_cell ntxec_subdevices[] = {
	{ .name = "ntxec-rtc" },
	{ .name = "ntxec-pwm" },
};

static int ntxec_probe(struct i2c_client *client)
{
	struct ntxec *ec;
	unsigned int version;
	int res;

	ec = devm_kmalloc(&client->dev, sizeof(*ec), GFP_KERNEL);
	if (!ec)
		return -ENOMEM;

	ec->dev = &client->dev;

	ec->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(ec->regmap)) {
		dev_err(ec->dev, "Failed to set up regmap for device\n");
		return PTR_ERR(ec->regmap);
	}

	/* Determine the firmware version */
	res = regmap_read(ec->regmap, NTXEC_REG_VERSION, &version);
	if (res < 0) {
		dev_err(ec->dev, "Failed to read firmware version number\n");
		return res;
	}

	/* Bail out if we encounter an unknown firmware version */
	switch (version) {
	case NTXEC_VERSION_KOBO_AURA:
		break;
	default:
		dev_err(ec->dev,
			"Netronix embedded controller version %04x is not supported.\n",
			version);
		return -ENODEV;
	}

	dev_info(ec->dev,
		 "Netronix embedded controller version %04x detected.\n", version);

	if (of_device_is_system_power_controller(ec->dev->of_node)) {
		/*
		 * Set the 'powerkeep' bit. This is necessary on some boards
		 * in order to keep the system running.
		 */
		res = regmap_write(ec->regmap, NTXEC_REG_POWERKEEP,
				   NTXEC_POWERKEEP_VALUE);
		if (res < 0)
			return res;

		if (poweroff_restart_client)
			/*
			 * Another instance of the driver already took
			 * poweroff/restart duties.
			 */
			dev_err(ec->dev, "poweroff_restart_client already assigned\n");
		else
			poweroff_restart_client = client;

		if (pm_power_off)
			/* Another driver already registered a poweroff handler. */
			dev_err(ec->dev, "pm_power_off already assigned\n");
		else
			pm_power_off = ntxec_poweroff;

		res = register_restart_handler(&ntxec_restart_handler);
		if (res)
			dev_err(ec->dev,
				"Failed to register restart handler: %d\n", res);
	}

	i2c_set_clientdata(client, ec);

	res = devm_mfd_add_devices(ec->dev, PLATFORM_DEVID_NONE, ntxec_subdevices,
				   ARRAY_SIZE(ntxec_subdevices), NULL, 0, NULL);
	if (res)
		dev_err(ec->dev, "Failed to add subdevices: %d\n", res);

	return res;
}

static int ntxec_remove(struct i2c_client *client)
{
	if (client == poweroff_restart_client) {
		poweroff_restart_client = NULL;
		pm_power_off = NULL;
		unregister_restart_handler(&ntxec_restart_handler);
	}

	return 0;
}

static const struct of_device_id of_ntxec_match_table[] = {
	{ .compatible = "netronix,ntxec", },
	{}
};
MODULE_DEVICE_TABLE(of, of_ntxec_match_table);

static struct i2c_driver ntxec_driver = {
	.driver = {
		.name = "ntxec",
		.of_match_table = of_ntxec_match_table,
	},
	.probe_new = ntxec_probe,
	.remove = ntxec_remove,
};
module_i2c_driver(ntxec_driver);

MODULE_AUTHOR("Jonathan Neuschäfer <j.neuschaefer@gmx.net>");
MODULE_DESCRIPTION("Core driver for Netronix EC");
MODULE_LICENSE("GPL");
