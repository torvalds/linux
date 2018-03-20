// SPDX-License-Identifier: GPL-2.0+
// Driver to instantiate Chromebook i2c/smbus devices.
//
// Copyright (C) 2012 Google, Inc.
// Author: Benson Leung <bleung@chromium.org>

#define pr_fmt(fmt)		KBUILD_MODNAME ": " fmt

#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/platform_data/atmel_mxt_ts.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define ATMEL_TP_I2C_ADDR	0x4b
#define ATMEL_TP_I2C_BL_ADDR	0x25
#define ATMEL_TS_I2C_ADDR	0x4a
#define ATMEL_TS_I2C_BL_ADDR	0x26
#define CYAPA_TP_I2C_ADDR	0x67
#define ELAN_TP_I2C_ADDR	0x15
#define ISL_ALS_I2C_ADDR	0x44
#define TAOS_ALS_I2C_ADDR	0x29

#define MAX_I2C_DEVICE_DEFERRALS	5

static const char *i2c_adapter_names[] = {
	"SMBus I801 adapter",
	"i915 gmbus vga",
	"i915 gmbus panel",
	"Synopsys DesignWare I2C adapter",
	"Synopsys DesignWare I2C adapter",
};

/* Keep this enum consistent with i2c_adapter_names */
enum i2c_adapter_type {
	I2C_ADAPTER_SMBUS = 0,
	I2C_ADAPTER_VGADDC,
	I2C_ADAPTER_PANEL,
	I2C_ADAPTER_DESIGNWARE_0,
	I2C_ADAPTER_DESIGNWARE_1,
};

enum i2c_peripheral_state {
	UNPROBED = 0,
	PROBED,
	TIMEDOUT,
	FAILED,
};

struct i2c_peripheral {
	struct i2c_board_info board_info;
	unsigned short alt_addr;
	const char *dmi_name;
	enum i2c_adapter_type type;

	enum i2c_peripheral_state state;
	struct i2c_client *client;
	int tries;
};

#define MAX_I2C_PERIPHERALS 4

struct chromeos_laptop {
	struct i2c_peripheral i2c_peripherals[MAX_I2C_PERIPHERALS];
};

static struct chromeos_laptop *cros_laptop;

static int chromeos_laptop_get_irq_from_dmi(const char *dmi_name)
{
	const struct dmi_device *dmi_dev;
	const struct dmi_dev_onboard *dev_data;

	dmi_dev = dmi_find_device(DMI_DEV_TYPE_DEV_ONBOARD, dmi_name, NULL);
	if (!dmi_dev) {
		pr_err("failed to find DMI device '%s'\n", dmi_name);
		return -ENOENT;
	}

	dev_data = dmi_dev->device_data;
	if (!dev_data) {
		pr_err("failed to get data from DMI for '%s'\n", dmi_name);
		return -EINVAL;
	}

	return dev_data->instance;
}

static struct i2c_client *
chromes_laptop_instantiate_i2c_device(int bus,
				      struct i2c_board_info *info,
				      unsigned short alt_addr)
{
	struct i2c_adapter *adapter;
	struct i2c_client *client = NULL;
	const unsigned short addr_list[] = { info->addr, I2C_CLIENT_END };

	adapter = i2c_get_adapter(bus);
	if (!adapter) {
		pr_err("failed to get i2c adapter %d\n", bus);
		return NULL;
	}

	/*
	 * Add the i2c device. If we can't detect it at the primary
	 * address we scan secondary addresses. In any case the client
	 * structure gets assigned primary address.
	 */
	client = i2c_new_probed_device(adapter, info, addr_list, NULL);
	if (!client && alt_addr) {
		struct i2c_board_info dummy_info = {
			I2C_BOARD_INFO("dummy", info->addr),
		};
		const unsigned short alt_addr_list[] = {
			alt_addr, I2C_CLIENT_END
		};
		struct i2c_client *dummy;

		dummy = i2c_new_probed_device(adapter, &dummy_info,
					      alt_addr_list, NULL);
		if (dummy) {
			pr_debug("%d-%02x is probed at %02x\n",
				 bus, info->addr, dummy->addr);
			i2c_unregister_device(dummy);
			client = i2c_new_device(adapter, info);
		}
	}

	if (!client)
		pr_notice("failed to register device %d-%02x\n",
			  bus, info->addr);
	else
		pr_debug("added i2c device %d-%02x\n", bus, info->addr);

	i2c_put_adapter(adapter);
	return client;
}

struct i2c_lookup {
	const char *name;
	int instance;
	int n;
};

static int __find_i2c_adap(struct device *dev, void *data)
{
	struct i2c_lookup *lookup = data;
	static const char *prefix = "i2c-";
	struct i2c_adapter *adapter;

	if (strncmp(dev_name(dev), prefix, strlen(prefix)) != 0)
		return 0;
	adapter = to_i2c_adapter(dev);
	if (strncmp(adapter->name, lookup->name, strlen(lookup->name)) == 0 &&
	    lookup->n++ == lookup->instance)
		return 1;
	return 0;
}

static int find_i2c_adapter_num(enum i2c_adapter_type type)
{
	struct device *dev = NULL;
	struct i2c_adapter *adapter;
	struct i2c_lookup lookup;

	memset(&lookup, 0, sizeof(lookup));
	lookup.name = i2c_adapter_names[type];
	lookup.instance = (type == I2C_ADAPTER_DESIGNWARE_1) ? 1 : 0;

	/* find the adapter by name */
	dev = bus_find_device(&i2c_bus_type, NULL, &lookup, __find_i2c_adap);
	if (!dev) {
		/* Adapters may appear later. Deferred probing will retry */
		pr_notice("i2c adapter %s not found on system.\n",
			  lookup.name);
		return -ENODEV;
	}
	adapter = to_i2c_adapter(dev);
	return adapter->nr;
}

static int chromeos_laptop_add_peripheral(struct i2c_peripheral *i2c_dev)
{
	struct i2c_client *client;
	int bus;
	int irq;

	/*
	 * Check that the i2c adapter is present.
	 * -EPROBE_DEFER if missing as the adapter may appear much
	 * later.
	 */
	bus = find_i2c_adapter_num(i2c_dev->type);
	if (bus < 0)
		return bus == -ENODEV ? -EPROBE_DEFER : bus;

	if (i2c_dev->dmi_name) {
		irq = chromeos_laptop_get_irq_from_dmi(i2c_dev->dmi_name);
		if (irq < 0) {
			i2c_dev->state = FAILED;
			return irq;
		}

		i2c_dev->board_info.irq = irq;
	}

	client = chromes_laptop_instantiate_i2c_device(bus,
						       &i2c_dev->board_info,
						       i2c_dev->alt_addr);
	if (!client) {
		/*
		 * Set -EPROBE_DEFER a limited num of times
		 * if device is not successfully added.
		 */
		if (++i2c_dev->tries < MAX_I2C_DEVICE_DEFERRALS) {
			return -EPROBE_DEFER;
		} else {
			/* Ran out of tries. */
			pr_notice("ran out of tries for device.\n");
			i2c_dev->state = TIMEDOUT;
			return -EIO;
		}
	}

	i2c_dev->client = client;
	i2c_dev->state = PROBED;

	return 0;
}

static int __init chromeos_laptop_dmi_matched(const struct dmi_system_id *id)
{
	cros_laptop = (void *)id->driver_data;
	pr_debug("DMI Matched %s\n", id->ident);

	/* Indicate to dmi_scan that processing is done. */
	return 1;
}

static int chromeos_laptop_probe(struct platform_device *pdev)
{
	struct i2c_peripheral *i2c_dev;
	int i;
	int ret = 0;

	for (i = 0; i < MAX_I2C_PERIPHERALS; i++) {
		i2c_dev = &cros_laptop->i2c_peripherals[i];

		/* No more peripherals. */
		if (!i2c_dev->board_info.addr)
			break;

		if (i2c_dev->state != UNPROBED)
			continue;

		if (chromeos_laptop_add_peripheral(i2c_dev) == -EPROBE_DEFER)
			ret = -EPROBE_DEFER;
	}

	return ret;
}

static struct chromeos_laptop samsung_series_5_550 = {
	.i2c_peripherals = {
		/* Touchpad. */
		{
			.board_info	= {
				I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
				.flags		= I2C_CLIENT_WAKE,
			},
			.dmi_name	= "trackpad",
			.type		= I2C_ADAPTER_SMBUS,
		},
		/* Light Sensor. */
		{
			.board_info	= {
				I2C_BOARD_INFO("isl29018", ISL_ALS_I2C_ADDR),
			},
			.dmi_name	= "lightsensor",
			.type		= I2C_ADAPTER_SMBUS,
		},
	},
};

static struct chromeos_laptop samsung_series_5 = {
	.i2c_peripherals = {
		/* Light Sensor. */
		{
			.board_info	= {
				I2C_BOARD_INFO("tsl2583", TAOS_ALS_I2C_ADDR),
			},
			.type		= I2C_ADAPTER_SMBUS,
		},
	},
};

static struct mxt_platform_data atmel_1664s_platform_data = {
	.irqflags		= IRQF_TRIGGER_FALLING,
};

static int chromebook_pixel_tp_keys[] = {
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	BTN_LEFT
};

static struct mxt_platform_data chromebook_pixel_tp_platform_data = {
	.irqflags		= IRQF_TRIGGER_FALLING,
	.t19_num_keys		= ARRAY_SIZE(chromebook_pixel_tp_keys),
	.t19_keymap		= chromebook_pixel_tp_keys,
};

static struct chromeos_laptop chromebook_pixel = {
	.i2c_peripherals = {
		/* Touch Screen. */
		{
			.board_info	= {
				I2C_BOARD_INFO("atmel_mxt_ts",
						ATMEL_TS_I2C_ADDR),
				.platform_data	= &atmel_1664s_platform_data,
				.flags		= I2C_CLIENT_WAKE,
			},
			.dmi_name	= "touchscreen",
			.type		= I2C_ADAPTER_PANEL,
			.alt_addr	= ATMEL_TS_I2C_BL_ADDR,
		},
		/* Touchpad. */
		{
			.board_info	= {
				I2C_BOARD_INFO("atmel_mxt_tp",
						ATMEL_TP_I2C_ADDR),
				.platform_data	=
					&chromebook_pixel_tp_platform_data,
				.flags		= I2C_CLIENT_WAKE,
			},
			.dmi_name	= "trackpad",
			.type		= I2C_ADAPTER_VGADDC,
			.alt_addr	= ATMEL_TP_I2C_BL_ADDR,
		},
		/* Light Sensor. */
		{
			.board_info	= {
				I2C_BOARD_INFO("isl29018", ISL_ALS_I2C_ADDR),
			},
			.dmi_name	= "lightsensor",
			.type		= I2C_ADAPTER_PANEL,
		},
	},
};

static struct chromeos_laptop hp_chromebook_14 = {
	.i2c_peripherals = {
		/* Touchpad. */
		{
			.board_info	= {
				I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
				.flags		= I2C_CLIENT_WAKE,
			},
			.dmi_name	= "trackpad",
			.type		= I2C_ADAPTER_DESIGNWARE_0,
		},
	},
};

static struct chromeos_laptop dell_chromebook_11 = {
	.i2c_peripherals = {
		/* Touchpad. */
		{
			.board_info	= {
				I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
				.flags		= I2C_CLIENT_WAKE,
			},
			.dmi_name	= "trackpad",
			.type		= I2C_ADAPTER_DESIGNWARE_0,
		},
		/* Elan Touchpad option. */
		{
			.board_info	= {
				I2C_BOARD_INFO("elan_i2c", ELAN_TP_I2C_ADDR),
				.flags		= I2C_CLIENT_WAKE,
			},
			.dmi_name	= "trackpad",
			.type		= I2C_ADAPTER_DESIGNWARE_0,
		},
	},
};

static struct chromeos_laptop toshiba_cb35 = {
	.i2c_peripherals = {
		/* Touchpad. */
		{
			.board_info	= {
				I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
				.flags		= I2C_CLIENT_WAKE,
			},
			.dmi_name	= "trackpad",
			.type		= I2C_ADAPTER_DESIGNWARE_0,
		},
	},
};

static struct chromeos_laptop acer_c7_chromebook = {
	.i2c_peripherals = {
		/* Touchpad. */
		{
			.board_info	= {
				I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
				.flags		= I2C_CLIENT_WAKE,
			},
			.dmi_name	= "trackpad",
			.type		= I2C_ADAPTER_SMBUS,
		},
	},
};

static struct chromeos_laptop acer_ac700 = {
	.i2c_peripherals = {
		/* Light Sensor. */
		{
			.board_info	= {
				I2C_BOARD_INFO("tsl2583", TAOS_ALS_I2C_ADDR),
			},
			.type		= I2C_ADAPTER_SMBUS,
		},
	},
};

static struct chromeos_laptop acer_c720 = {
	.i2c_peripherals = {
		/* Touchscreen. */
		{
			.board_info	= {
				I2C_BOARD_INFO("atmel_mxt_ts",
						ATMEL_TS_I2C_ADDR),
				.platform_data	= &atmel_1664s_platform_data,
				.flags		= I2C_CLIENT_WAKE,
			},
			.dmi_name	= "touchscreen",
			.type		= I2C_ADAPTER_DESIGNWARE_1,
			.alt_addr	= ATMEL_TS_I2C_BL_ADDR,
		},
		/* Touchpad. */
		{
			.board_info	= {
				I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
				.flags		= I2C_CLIENT_WAKE,
			},
			.dmi_name	= "trackpad",
			.type		= I2C_ADAPTER_DESIGNWARE_0,
		},
		/* Elan Touchpad option. */
		{
			.board_info	= {
				I2C_BOARD_INFO("elan_i2c", ELAN_TP_I2C_ADDR),
				.flags		= I2C_CLIENT_WAKE,
			},
			.dmi_name	= "trackpad",
			.type		= I2C_ADAPTER_DESIGNWARE_0,
		},
		/* Light Sensor. */
		{
			.board_info	= {
				I2C_BOARD_INFO("isl29018", ISL_ALS_I2C_ADDR),
			},
			.dmi_name	= "lightsensor",
			.type		= I2C_ADAPTER_DESIGNWARE_1,
		},
	},
};

static struct chromeos_laptop hp_pavilion_14_chromebook = {
	.i2c_peripherals = {
		/* Touchpad. */
		{
			.board_info	= {
				I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
				.flags		= I2C_CLIENT_WAKE,
			},
			.dmi_name	= "trackpad",
			.type		= I2C_ADAPTER_SMBUS,
		},
	},
};

static struct chromeos_laptop cr48 = {
	.i2c_peripherals = {
		/* Light Sensor. */
		{
			.board_info	= {
				I2C_BOARD_INFO("tsl2563", TAOS_ALS_I2C_ADDR),
			},
			.type		= I2C_ADAPTER_SMBUS,
		},
	},
};

#define _CBDD(board_) \
	.callback = chromeos_laptop_dmi_matched, \
	.driver_data = (void *)&board_

static const struct dmi_system_id chromeos_laptop_dmi_table[] __initconst = {
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
		.ident = "Wolf",
		.matches = {
			DMI_MATCH(DMI_BIOS_VENDOR, "coreboot"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Wolf"),
		},
		_CBDD(dell_chromebook_11),
	},
	{
		.ident = "HP Chromebook 14",
		.matches = {
			DMI_MATCH(DMI_BIOS_VENDOR, "coreboot"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Falco"),
		},
		_CBDD(hp_chromebook_14),
	},
	{
		.ident = "Toshiba CB35",
		.matches = {
			DMI_MATCH(DMI_BIOS_VENDOR, "coreboot"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Leon"),
		},
		_CBDD(toshiba_cb35),
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
		.ident = "Acer C720",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Peppy"),
		},
		_CBDD(acer_c720),
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
	},
	.probe = chromeos_laptop_probe,
};

static int __init chromeos_laptop_init(void)
{
	int ret;

	if (!dmi_check_system(chromeos_laptop_dmi_table)) {
		pr_debug("unsupported system\n");
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
	struct i2c_peripheral *i2c_dev;
	int i;

	platform_device_unregister(cros_platform_device);
	platform_driver_unregister(&cros_platform_driver);

	for (i = 0; i < MAX_I2C_PERIPHERALS; i++) {
		i2c_dev = &cros_laptop->i2c_peripherals[i];

		/* No more peripherals */
		if (!i2c_dev->board_info.type)
			break;

		if (i2c_dev->state == PROBED)
			i2c_unregister_device(i2c_dev->client);
	}
}

module_init(chromeos_laptop_init);
module_exit(chromeos_laptop_exit);

MODULE_DESCRIPTION("Chrome OS Laptop driver");
MODULE_AUTHOR("Benson Leung <bleung@chromium.org>");
MODULE_LICENSE("GPL");
