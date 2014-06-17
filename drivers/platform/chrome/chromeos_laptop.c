/*
 *  chromeos_laptop.c - Driver to instantiate Chromebook i2c/smbus devices.
 *
 *  Author : Benson Leung <bleung@chromium.org>
 *
 *  Copyright (C) 2012 Google, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/i2c/atmel_mxt_ts.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define ATMEL_TP_I2C_ADDR	0x4b
#define ATMEL_TP_I2C_BL_ADDR	0x25
#define ATMEL_TS_I2C_ADDR	0x4a
#define ATMEL_TS_I2C_BL_ADDR	0x26
#define CYAPA_TP_I2C_ADDR	0x67
#define ISL_ALS_I2C_ADDR	0x44
#define TAOS_ALS_I2C_ADDR	0x29

static struct i2c_client *als;
static struct i2c_client *tp;
static struct i2c_client *ts;

static const char *i2c_adapter_names[] = {
	"SMBus I801 adapter",
	"i915 gmbus vga",
	"i915 gmbus panel",
};

/* Keep this enum consistent with i2c_adapter_names */
enum i2c_adapter_type {
	I2C_ADAPTER_SMBUS = 0,
	I2C_ADAPTER_VGADDC,
	I2C_ADAPTER_PANEL,
};

struct i2c_peripheral {
	int (*add)(enum i2c_adapter_type type);
	enum i2c_adapter_type type;
};

#define MAX_I2C_PERIPHERALS 3

struct chromeos_laptop {
	struct i2c_peripheral i2c_peripherals[MAX_I2C_PERIPHERALS];
};

static struct chromeos_laptop *cros_laptop;

static struct i2c_board_info cyapa_device = {
	I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
	.flags		= I2C_CLIENT_WAKE,
};

static struct i2c_board_info isl_als_device = {
	I2C_BOARD_INFO("isl29018", ISL_ALS_I2C_ADDR),
};

static struct i2c_board_info tsl2583_als_device = {
	I2C_BOARD_INFO("tsl2583", TAOS_ALS_I2C_ADDR),
};

static struct i2c_board_info tsl2563_als_device = {
	I2C_BOARD_INFO("tsl2563", TAOS_ALS_I2C_ADDR),
};

static int mxt_t19_keys[] = {
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	BTN_LEFT
};

static struct mxt_platform_data atmel_224s_tp_platform_data = {
	.irqflags		= IRQF_TRIGGER_FALLING,
	.t19_num_keys		= ARRAY_SIZE(mxt_t19_keys),
	.t19_keymap		= mxt_t19_keys,
	.config			= NULL,
	.config_length		= 0,
};

static struct i2c_board_info atmel_224s_tp_device = {
	I2C_BOARD_INFO("atmel_mxt_tp", ATMEL_TP_I2C_ADDR),
	.platform_data = &atmel_224s_tp_platform_data,
	.flags		= I2C_CLIENT_WAKE,
};

static struct mxt_platform_data atmel_1664s_platform_data = {
	.irqflags		= IRQF_TRIGGER_FALLING,
	.config			= NULL,
	.config_length		= 0,
};

static struct i2c_board_info atmel_1664s_device = {
	I2C_BOARD_INFO("atmel_mxt_ts", ATMEL_TS_I2C_ADDR),
	.platform_data = &atmel_1664s_platform_data,
	.flags		= I2C_CLIENT_WAKE,
};

static struct i2c_client *__add_probed_i2c_device(
		const char *name,
		int bus,
		struct i2c_board_info *info,
		const unsigned short *addrs)
{
	const struct dmi_device *dmi_dev;
	const struct dmi_dev_onboard *dev_data;
	struct i2c_adapter *adapter;
	struct i2c_client *client;

	if (bus < 0)
		return NULL;
	/*
	 * If a name is specified, look for irq platform information stashed
	 * in DMI_DEV_TYPE_DEV_ONBOARD by the Chrome OS custom system firmware.
	 */
	if (name) {
		dmi_dev = dmi_find_device(DMI_DEV_TYPE_DEV_ONBOARD, name, NULL);
		if (!dmi_dev) {
			pr_err("%s failed to dmi find device %s.\n",
			       __func__,
			       name);
			return NULL;
		}
		dev_data = (struct dmi_dev_onboard *)dmi_dev->device_data;
		if (!dev_data) {
			pr_err("%s failed to get data from dmi for %s.\n",
			       __func__, name);
			return NULL;
		}
		info->irq = dev_data->instance;
	}

	adapter = i2c_get_adapter(bus);
	if (!adapter) {
		pr_err("%s failed to get i2c adapter %d.\n", __func__, bus);
		return NULL;
	}

	/* add the i2c device */
	client = i2c_new_probed_device(adapter, info, addrs, NULL);
	if (!client)
		pr_err("%s failed to register device %d-%02x\n",
		       __func__, bus, info->addr);
	else
		pr_debug("%s added i2c device %d-%02x\n",
			 __func__, bus, info->addr);

	i2c_put_adapter(adapter);
	return client;
}

static int __find_i2c_adap(struct device *dev, void *data)
{
	const char *name = data;
	static const char *prefix = "i2c-";
	struct i2c_adapter *adapter;
	if (strncmp(dev_name(dev), prefix, strlen(prefix)) != 0)
		return 0;
	adapter = to_i2c_adapter(dev);
	return (strncmp(adapter->name, name, strlen(name)) == 0);
}

static int find_i2c_adapter_num(enum i2c_adapter_type type)
{
	struct device *dev = NULL;
	struct i2c_adapter *adapter;
	const char *name = i2c_adapter_names[type];
	/* find the adapter by name */
	dev = bus_find_device(&i2c_bus_type, NULL, (void *)name,
			      __find_i2c_adap);
	if (!dev) {
		/* Adapters may appear later. Deferred probing will retry */
		pr_notice("%s: i2c adapter %s not found on system.\n", __func__,
			  name);
		return -ENODEV;
	}
	adapter = to_i2c_adapter(dev);
	return adapter->nr;
}

/*
 * Takes a list of addresses in addrs as such :
 * { addr1, ... , addrn, I2C_CLIENT_END };
 * add_probed_i2c_device will use i2c_new_probed_device
 * and probe for devices at all of the addresses listed.
 * Returns NULL if no devices found.
 * See Documentation/i2c/instantiating-devices for more information.
 */
static struct i2c_client *add_probed_i2c_device(
		const char *name,
		enum i2c_adapter_type type,
		struct i2c_board_info *info,
		const unsigned short *addrs)
{
	return __add_probed_i2c_device(name,
				       find_i2c_adapter_num(type),
				       info,
				       addrs);
}

/*
 * Probes for a device at a single address, the one provided by
 * info->addr.
 * Returns NULL if no device found.
 */
static struct i2c_client *add_i2c_device(const char *name,
						enum i2c_adapter_type type,
						struct i2c_board_info *info)
{
	const unsigned short addr_list[] = { info->addr, I2C_CLIENT_END };
	return __add_probed_i2c_device(name,
				       find_i2c_adapter_num(type),
				       info,
				       addr_list);
}

static int setup_cyapa_tp(enum i2c_adapter_type type)
{
	if (tp)
		return 0;

	/* add cyapa touchpad */
	tp = add_i2c_device("trackpad", type, &cyapa_device);
	return (!tp) ? -EAGAIN : 0;
}

static int setup_atmel_224s_tp(enum i2c_adapter_type type)
{
	const unsigned short addr_list[] = { ATMEL_TP_I2C_BL_ADDR,
					     ATMEL_TP_I2C_ADDR,
					     I2C_CLIENT_END };
	if (tp)
		return 0;

	/* add atmel mxt touchpad */
	tp = add_probed_i2c_device("trackpad", type,
				   &atmel_224s_tp_device, addr_list);
	return (!tp) ? -EAGAIN : 0;
}

static int setup_atmel_1664s_ts(enum i2c_adapter_type type)
{
	const unsigned short addr_list[] = { ATMEL_TS_I2C_BL_ADDR,
					     ATMEL_TS_I2C_ADDR,
					     I2C_CLIENT_END };
	if (ts)
		return 0;

	/* add atmel mxt touch device */
	ts = add_probed_i2c_device("touchscreen", type,
				   &atmel_1664s_device, addr_list);
	return (!ts) ? -EAGAIN : 0;
}

static int setup_isl29018_als(enum i2c_adapter_type type)
{
	if (als)
		return 0;

	/* add isl29018 light sensor */
	als = add_i2c_device("lightsensor", type, &isl_als_device);
	return (!als) ? -EAGAIN : 0;
}

static int setup_tsl2583_als(enum i2c_adapter_type type)
{
	if (als)
		return 0;

	/* add tsl2583 light sensor */
	als = add_i2c_device(NULL, type, &tsl2583_als_device);
	return (!als) ? -EAGAIN : 0;
}

static int setup_tsl2563_als(enum i2c_adapter_type type)
{
	if (als)
		return 0;

	/* add tsl2563 light sensor */
	als = add_i2c_device(NULL, type, &tsl2563_als_device);
	return (!als) ? -EAGAIN : 0;
}

static int __init chromeos_laptop_dmi_matched(const struct dmi_system_id *id)
{
	cros_laptop = (void *)id->driver_data;
	pr_debug("DMI Matched %s.\n", id->ident);

	/* Indicate to dmi_scan that processing is done. */
	return 1;
}

static int chromeos_laptop_probe(struct platform_device *pdev)
{
	int i;
	int ret = 0;

	for (i = 0; i < MAX_I2C_PERIPHERALS; i++) {
		struct i2c_peripheral *i2c_dev;

		i2c_dev = &cros_laptop->i2c_peripherals[i];

		/* No more peripherals. */
		if (i2c_dev->add == NULL)
			break;

		/* Add the device. Set -EPROBE_DEFER on any failure */
		if (i2c_dev->add(i2c_dev->type))
			ret = -EPROBE_DEFER;
	}

	return ret;
}

static struct chromeos_laptop samsung_series_5_550 = {
	.i2c_peripherals = {
		/* Touchpad. */
		{ .add = setup_cyapa_tp, I2C_ADAPTER_SMBUS },
		/* Light Sensor. */
		{ .add = setup_isl29018_als, I2C_ADAPTER_SMBUS },
	},
};

static struct chromeos_laptop samsung_series_5 = {
	.i2c_peripherals = {
		/* Light Sensor. */
		{ .add = setup_tsl2583_als, I2C_ADAPTER_SMBUS },
	},
};

static struct chromeos_laptop chromebook_pixel = {
	.i2c_peripherals = {
		/* Touch Screen. */
		{ .add = setup_atmel_1664s_ts, I2C_ADAPTER_PANEL },
		/* Touchpad. */
		{ .add = setup_atmel_224s_tp, I2C_ADAPTER_VGADDC },
		/* Light Sensor. */
		{ .add = setup_isl29018_als, I2C_ADAPTER_PANEL },
	},
};

static struct chromeos_laptop acer_c7_chromebook = {
	.i2c_peripherals = {
		/* Touchpad. */
		{ .add = setup_cyapa_tp, I2C_ADAPTER_SMBUS },
	},
};

static struct chromeos_laptop acer_ac700 = {
	.i2c_peripherals = {
		/* Light Sensor. */
		{ .add = setup_tsl2563_als, I2C_ADAPTER_SMBUS },
	},
};

static struct chromeos_laptop hp_pavilion_14_chromebook = {
	.i2c_peripherals = {
		/* Touchpad. */
		{ .add = setup_cyapa_tp, I2C_ADAPTER_SMBUS },
	},
};

static struct chromeos_laptop cr48 = {
	.i2c_peripherals = {
		/* Light Sensor. */
		{ .add = setup_tsl2563_als, I2C_ADAPTER_SMBUS },
	},
};

#define _CBDD(board_) \
	.callback = chromeos_laptop_dmi_matched, \
	.driver_data = (void *)&board_

static struct dmi_system_id chromeos_laptop_dmi_table[] __initdata = {
	{
		.ident = "Samsung Series 5 550",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Lumpy"),
		},
		_CBDD(samsung_series_5_550),
	},
	{
		.ident = "Samsung Series 5",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Alex"),
		},
		_CBDD(samsung_series_5),
	},
	{
		.ident = "Chromebook Pixel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Link"),
		},
		_CBDD(chromebook_pixel),
	},
	{
		.ident = "Acer C7 Chromebook",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Parrot"),
		},
		_CBDD(acer_c7_chromebook),
	},
	{
		.ident = "Acer AC700",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "ZGB"),
		},
		_CBDD(acer_ac700),
	},
	{
		.ident = "HP Pavilion 14 Chromebook",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Butterfly"),
		},
		_CBDD(hp_pavilion_14_chromebook),
	},
	{
		.ident = "Cr-48",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Mario"),
		},
		_CBDD(cr48),
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, chromeos_laptop_dmi_table);

static struct platform_device *cros_platform_device;

static struct platform_driver cros_platform_driver = {
	.driver = {
		.name = "chromeos_laptop",
		.owner = THIS_MODULE,
	},
	.probe = chromeos_laptop_probe,
};

static int __init chromeos_laptop_init(void)
{
	int ret;
	if (!dmi_check_system(chromeos_laptop_dmi_table)) {
		pr_debug("%s unsupported system.\n", __func__);
		return -ENODEV;
	}

	ret = platform_driver_register(&cros_platform_driver);
	if (ret)
		return ret;

	cros_platform_device = platform_device_alloc("chromeos_laptop", -1);
	if (!cros_platform_device) {
		ret = -ENOMEM;
		goto fail_platform_device1;
	}

	ret = platform_device_add(cros_platform_device);
	if (ret)
		goto fail_platform_device2;

	return 0;

fail_platform_device2:
	platform_device_put(cros_platform_device);
fail_platform_device1:
	platform_driver_unregister(&cros_platform_driver);
	return ret;
}

static void __exit chromeos_laptop_exit(void)
{
	if (als)
		i2c_unregister_device(als);
	if (tp)
		i2c_unregister_device(tp);
	if (ts)
		i2c_unregister_device(ts);

	platform_device_unregister(cros_platform_device);
	platform_driver_unregister(&cros_platform_driver);
}

module_init(chromeos_laptop_init);
module_exit(chromeos_laptop_exit);

MODULE_DESCRIPTION("Chrome OS Laptop driver");
MODULE_AUTHOR("Benson Leung <bleung@chromium.org>");
MODULE_LICENSE("GPL");
