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
#include <linux/module.h>

#define CYAPA_TP_I2C_ADDR	0x67
#define ISL_ALS_I2C_ADDR	0x44

static struct i2c_client *als;
static struct i2c_client *tp;

const char *i2c_adapter_names[] = {
	"SMBus I801 adapter",
};

/* Keep this enum consistent with i2c_adapter_names */
enum i2c_adapter_type {
	I2C_ADAPTER_SMBUS = 0,
};

static struct i2c_board_info __initdata cyapa_device = {
	I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
	.flags		= I2C_CLIENT_WAKE,
};

static struct i2c_board_info __initdata isl_als_device = {
	I2C_BOARD_INFO("isl29018", ISL_ALS_I2C_ADDR),
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
 * Probes for a device at a single address, the one provided by
 * info->addr.
 * Returns NULL if no device found.
 */
static struct i2c_client __init *add_smbus_device(const char *name,
						  struct i2c_board_info *info)
{
	const unsigned short addr_list[] = { info->addr, I2C_CLIENT_END };
	return __add_probed_i2c_device(name,
				       find_i2c_adapter_num(I2C_ADAPTER_SMBUS),
				       info,
				       addr_list);
}

static int __init setup_lumpy_tp(const struct dmi_system_id *id)
{
	/* add cyapa touchpad on smbus */
	tp = add_smbus_device("trackpad", &cyapa_device);
	return 0;
}

static int __init setup_isl29018_als(const struct dmi_system_id *id)
{
	/* add isl29018 light sensor */
	als = add_smbus_device("lightsensor", &isl_als_device);
	return 0;
}

static struct dmi_system_id __initdata chromeos_laptop_dmi_table[] = {
	{
		.ident = "Samsung Series 5 550 - Touchpad",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Lumpy"),
		},
		.callback = setup_lumpy_tp,
	},
	{
		.ident = "Samsung Series 5 550 - Light Sensor",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Lumpy"),
		},
		.callback = setup_isl29018_als,
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
}

module_init(chromeos_laptop_init);
module_exit(chromeos_laptop_exit);

MODULE_DESCRIPTION("Chrome OS Laptop driver");
MODULE_AUTHOR("Benson Leung <bleung@chromium.org>");
MODULE_LICENSE("GPL");
