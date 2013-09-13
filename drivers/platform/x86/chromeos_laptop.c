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

const char *i2c_adapter_names[] = {
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

static struct i2c_board_info __initdata cyapa_device = {
	I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
	.flags		= I2C_CLIENT_WAKE,
};

static struct i2c_board_info __initdata isl_als_device = {
	I2C_BOARD_INFO("isl29018", ISL_ALS_I2C_ADDR),
};

static struct i2c_board_info __initdata tsl2583_als_device = {
	I2C_BOARD_INFO("tsl2583", TAOS_ALS_I2C_ADDR),
};

static struct i2c_board_info __initdata tsl2563_als_device = {
	I2C_BOARD_INFO("tsl2563", TAOS_ALS_I2C_ADDR),
};

static struct mxt_platform_data atmel_224s_tp_platform_data = {
	.x_line			= 18,
	.y_line			= 12,
	.x_size			= 102*20,
	.y_size			= 68*20,
	.blen			= 0x80,	/* Gain setting is in upper 4 bits */
	.threshold		= 0x32,
	.voltage		= 0,	/* 3.3V */
	.orient			= MXT_VERTICAL_FLIP,
	.irqflags		= IRQF_TRIGGER_FALLING,
	.is_tp			= true,
	.key_map		= { KEY_RESERVED,
				    KEY_RESERVED,
				    KEY_RESERVED,
				    BTN_LEFT },
	.config			= NULL,
	.config_length		= 0,
};

static struct i2c_board_info __initdata atmel_224s_tp_device = {
	I2C_BOARD_INFO("atmel_mxt_tp", ATMEL_TP_I2C_ADDR),
	.platform_data = &atmel_224s_tp_platform_data,
	.flags		= I2C_CLIENT_WAKE,
};

static struct mxt_platform_data atmel_1664s_platform_data = {
	.x_line			= 32,
	.y_line			= 50,
	.x_size			= 1700,
	.y_size			= 2560,
	.blen			= 0x89,	/* Gain setting is in upper 4 bits */
	.threshold		= 0x28,
	.voltage		= 0,	/* 3.3V */
	.orient			= MXT_ROTATED_90_COUNTER,
	.irqflags		= IRQF_TRIGGER_FALLING,
	.is_tp			= false,
	.config			= NULL,
	.config_length		= 0,
};

static struct i2c_board_info __initdata atmel_1664s_device = {
	I2C_BOARD_INFO("atmel_mxt_ts", ATMEL_TS_I2C_ADDR),
	.platform_data = &atmel_1664s_platform_data,
	.flags		= I2C_CLIENT_WAKE,
};

static struct i2c_client __init *__add_probed_i2c_device(
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

static int __init __find_i2c_adap(struct device *dev, void *data)
{
	const char *name = data;
	static const char *prefix = "i2c-";
	struct i2c_adapter *adapter;
	if (strncmp(dev_name(dev), prefix, strlen(prefix)) != 0)
		return 0;
	adapter = to_i2c_adapter(dev);
	return (strncmp(adapter->name, name, strlen(name)) == 0);
}

static int __init find_i2c_adapter_num(enum i2c_adapter_type type)
{
	struct device *dev = NULL;
	struct i2c_adapter *adapter;
	const char *name = i2c_adapter_names[type];
	/* find the adapter by name */
	dev = bus_find_device(&i2c_bus_type, NULL, (void *)name,
			      __find_i2c_adap);
	if (!dev) {
		pr_err("%s: i2c adapter %s not found on system.\n", __func__,
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
static __init struct i2c_client *add_probed_i2c_device(
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
static __init struct i2c_client *add_i2c_device(const char *name,
						enum i2c_adapter_type type,
						struct i2c_board_info *info)
{
	const unsigned short addr_list[] = { info->addr, I2C_CLIENT_END };
	return __add_probed_i2c_device(name,
				       find_i2c_adapter_num(type),
				       info,
				       addr_list);
}


static struct i2c_client __init *add_smbus_device(const char *name,
						  struct i2c_board_info *info)
{
	return add_i2c_device(name, I2C_ADAPTER_SMBUS, info);
}

static int __init setup_cyapa_smbus_tp(const struct dmi_system_id *id)
{
	/* add cyapa touchpad on smbus */
	tp = add_smbus_device("trackpad", &cyapa_device);
	return 0;
}

static int __init setup_atmel_224s_tp(const struct dmi_system_id *id)
{
	const unsigned short addr_list[] = { ATMEL_TP_I2C_BL_ADDR,
					     ATMEL_TP_I2C_ADDR,
					     I2C_CLIENT_END };

	/* add atmel mxt touchpad on VGA DDC GMBus */
	tp = add_probed_i2c_device("trackpad", I2C_ADAPTER_VGADDC,
				   &atmel_224s_tp_device, addr_list);
	return 0;
}

static int __init setup_atmel_1664s_ts(const struct dmi_system_id *id)
{
	const unsigned short addr_list[] = { ATMEL_TS_I2C_BL_ADDR,
					     ATMEL_TS_I2C_ADDR,
					     I2C_CLIENT_END };

	/* add atmel mxt touch device on PANEL GMBus */
	ts = add_probed_i2c_device("touchscreen", I2C_ADAPTER_PANEL,
				   &atmel_1664s_device, addr_list);
	return 0;
}


static int __init setup_isl29018_als(const struct dmi_system_id *id)
{
	/* add isl29018 light sensor */
	als = add_smbus_device("lightsensor", &isl_als_device);
	return 0;
}

static int __init setup_isl29023_als(const struct dmi_system_id *id)
{
	/* add isl29023 light sensor on Panel GMBus */
	als = add_i2c_device("lightsensor", I2C_ADAPTER_PANEL,
			     &isl_als_device);
	return 0;
}

static int __init setup_tsl2583_als(const struct dmi_system_id *id)
{
	/* add tsl2583 light sensor on smbus */
	als = add_smbus_device(NULL, &tsl2583_als_device);
	return 0;
}

static int __init setup_tsl2563_als(const struct dmi_system_id *id)
{
	/* add tsl2563 light sensor on smbus */
	als = add_smbus_device(NULL, &tsl2563_als_device);
	return 0;
}

static struct dmi_system_id __initdata chromeos_laptop_dmi_table[] = {
	{
		.ident = "Samsung Series 5 550 - Touchpad",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Lumpy"),
		},
		.callback = setup_cyapa_smbus_tp,
	},
	{
		.ident = "Chromebook Pixel - Touchscreen",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Link"),
		},
		.callback = setup_atmel_1664s_ts,
	},
	{
		.ident = "Chromebook Pixel - Touchpad",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Link"),
		},
		.callback = setup_atmel_224s_tp,
	},
	{
		.ident = "Samsung Series 5 550 - Light Sensor",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Lumpy"),
		},
		.callback = setup_isl29018_als,
	},
	{
		.ident = "Chromebook Pixel - Light Sensor",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Link"),
		},
		.callback = setup_isl29023_als,
	},
	{
		.ident = "Acer C7 Chromebook - Touchpad",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Parrot"),
		},
		.callback = setup_cyapa_smbus_tp,
	},
	{
		.ident = "HP Pavilion 14 Chromebook - Touchpad",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Butterfly"),
		},
		.callback = setup_cyapa_smbus_tp,
	},
	{
		.ident = "Samsung Series 5 - Light Sensor",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Alex"),
		},
		.callback = setup_tsl2583_als,
	},
	{
		.ident = "Cr-48 - Light Sensor",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Mario"),
		},
		.callback = setup_tsl2563_als,
	},
	{
		.ident = "Acer AC700 - Light Sensor",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "ZGB"),
		},
		.callback = setup_tsl2563_als,
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, chromeos_laptop_dmi_table);

static int __init chromeos_laptop_init(void)
{
	if (!dmi_check_system(chromeos_laptop_dmi_table)) {
		pr_debug("%s unsupported system.\n", __func__);
		return -ENODEV;
	}
	return 0;
}

static void __exit chromeos_laptop_exit(void)
{
	if (als)
		i2c_unregister_device(als);
	if (tp)
		i2c_unregister_device(tp);
	if (ts)
		i2c_unregister_device(ts);
}

module_init(chromeos_laptop_init);
module_exit(chromeos_laptop_exit);

MODULE_DESCRIPTION("Chrome OS Laptop driver");
MODULE_AUTHOR("Benson Leung <bleung@chromium.org>");
MODULE_LICENSE("GPL");
