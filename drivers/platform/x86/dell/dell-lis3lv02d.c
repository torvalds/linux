// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lis3lv02d i2c-client instantiation for ACPI SMO88xx devices without I2C resources.
 *
 *  Copyright (C) 2024 Hans de Goede <hansg@kernel.org>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device/bus.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include "dell-smo8800-ids.h"

#define LIS3_WHO_AM_I 0x0f

#define DELL_LIS3LV02D_DMI_ENTRY(product_name, i2c_addr)                 \
	{                                                                \
		.matches = {                                             \
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Dell Inc."),    \
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, product_name), \
		},                                                       \
		.driver_data = (void *)(uintptr_t)(i2c_addr),            \
	}

/*
 * Accelerometer's I2C address is not specified in DMI nor ACPI,
 * so it is needed to define mapping table based on DMI product names.
 */
static const struct dmi_system_id lis3lv02d_devices[] __initconst = {
	/*
	 * Dell platform team told us that these Latitude devices have
	 * ST microelectronics accelerometer at I2C address 0x29.
	 */
	DELL_LIS3LV02D_DMI_ENTRY("Latitude E5250",     0x29),
	DELL_LIS3LV02D_DMI_ENTRY("Latitude E5450",     0x29),
	DELL_LIS3LV02D_DMI_ENTRY("Latitude E5550",     0x29),
	DELL_LIS3LV02D_DMI_ENTRY("Latitude E6440",     0x29),
	DELL_LIS3LV02D_DMI_ENTRY("Latitude E6440 ATG", 0x29),
	DELL_LIS3LV02D_DMI_ENTRY("Latitude E6540",     0x29),
	/*
	 * Additional individual entries were added after verification.
	 */
	DELL_LIS3LV02D_DMI_ENTRY("Latitude 5480",      0x29),
	DELL_LIS3LV02D_DMI_ENTRY("Latitude 5500",      0x29),
	DELL_LIS3LV02D_DMI_ENTRY("Latitude E6330",     0x29),
	DELL_LIS3LV02D_DMI_ENTRY("Latitude E6430",     0x29),
	DELL_LIS3LV02D_DMI_ENTRY("Precision 3540",     0x29),
	DELL_LIS3LV02D_DMI_ENTRY("Precision M6800",    0x29),
	DELL_LIS3LV02D_DMI_ENTRY("Vostro V131",        0x1d),
	DELL_LIS3LV02D_DMI_ENTRY("Vostro 5568",        0x29),
	DELL_LIS3LV02D_DMI_ENTRY("XPS 15 7590",        0x29),
	DELL_LIS3LV02D_DMI_ENTRY("XPS 15 9550",        0x29),
	{ }
};

static u8 i2c_addr;
static struct i2c_client *i2c_dev;
static bool notifier_registered;

static bool probe_i2c_addr;
module_param(probe_i2c_addr, bool, 0444);
MODULE_PARM_DESC(probe_i2c_addr, "Probe the i801 I2C bus for the accelerometer on models where the address is unknown, this may be dangerous.");

static int detect_lis3lv02d(struct i2c_adapter *adap, unsigned short addr)
{
	union i2c_smbus_data smbus_data;
	int err;

	dev_info(&adap->dev, "Probing for lis3lv02d on address 0x%02x\n", addr);

	err = i2c_smbus_xfer(adap, addr, 0, I2C_SMBUS_READ, LIS3_WHO_AM_I,
			     I2C_SMBUS_BYTE_DATA, &smbus_data);
	if (err < 0)
		return 0; /* Not found */

	/* valid who-am-i values are from drivers/misc/lis3lv02d/lis3lv02d.c */
	switch (smbus_data.byte) {
	case 0x32:
	case 0x33:
	case 0x3a:
	case 0x3b:
		break;
	default:
		dev_warn(&adap->dev, "Unknown who-am-i register value 0x%02x\n",
			 smbus_data.byte);
		return 0; /* Not found */
	}

	dev_info(&adap->dev,
		 "Detected lis3lv02d on address 0x%02x, please report this upstream to platform-driver-x86@vger.kernel.org so that a quirk can be added\n",
		 addr);

	return 1; /* Found */
}

static bool i2c_adapter_is_main_i801(struct i2c_adapter *adap)
{
	/*
	 * Only match the main I801 adapter and reject secondary adapters
	 * which names start with "SMBus I801 IDF adapter".
	 */
	return strstarts(adap->name, "SMBus I801 adapter");
}

static int find_i801(struct device *dev, void *data)
{
	struct i2c_adapter *adap, **adap_ret = data;

	adap = i2c_verify_adapter(dev);
	if (!adap)
		return 0;

	if (!i2c_adapter_is_main_i801(adap))
		return 0;

	*adap_ret = i2c_get_adapter(adap->nr);
	return 1;
}

static void instantiate_i2c_client(struct work_struct *work)
{
	struct i2c_board_info info = { };
	struct i2c_adapter *adap = NULL;

	if (i2c_dev)
		return;

	/*
	 * bus_for_each_dev() and not i2c_for_each_dev() to avoid
	 * a deadlock when find_i801() calls i2c_get_adapter().
	 */
	bus_for_each_dev(&i2c_bus_type, NULL, &adap, find_i801);
	if (!adap)
		return;

	strscpy(info.type, "lis3lv02d", I2C_NAME_SIZE);

	if (i2c_addr) {
		info.addr = i2c_addr;
		i2c_dev = i2c_new_client_device(adap, &info);
	} else {
		/* First try address 0x29 (most used) and then try 0x1d */
		static const unsigned short addr_list[] = { 0x29, 0x1d, I2C_CLIENT_END };

		i2c_dev = i2c_new_scanned_device(adap, &info, addr_list, detect_lis3lv02d);
	}

	if (IS_ERR(i2c_dev)) {
		dev_err(&adap->dev, "error %ld registering i2c_client\n", PTR_ERR(i2c_dev));
		i2c_dev = NULL;
	} else {
		dev_dbg(&adap->dev, "registered lis3lv02d on address 0x%02x\n", info.addr);
	}

	i2c_put_adapter(adap);
}
static DECLARE_WORK(i2c_work, instantiate_i2c_client);

static int i2c_bus_notify(struct notifier_block *nb, unsigned long action, void *data)
{
	struct device *dev = data;
	struct i2c_client *client;
	struct i2c_adapter *adap;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		adap = i2c_verify_adapter(dev);
		if (!adap)
			break;

		if (i2c_adapter_is_main_i801(adap))
			queue_work(system_long_wq, &i2c_work);
		break;
	case BUS_NOTIFY_REMOVED_DEVICE:
		client = i2c_verify_client(dev);
		if (!client)
			break;

		if (i2c_dev == client) {
			dev_dbg(&client->adapter->dev, "lis3lv02d i2c_client removed\n");
			i2c_dev = NULL;
		}
		break;
	default:
		break;
	}

	return 0;
}
static struct notifier_block i2c_nb = { .notifier_call = i2c_bus_notify };

static int __init match_acpi_device_ids(struct device *dev, const void *data)
{
	return acpi_match_device(data, dev) ? 1 : 0;
}

static int __init dell_lis3lv02d_init(void)
{
	const struct dmi_system_id *lis3lv02d_dmi_id;
	struct device *dev;
	int err;

	/*
	 * First check for a matching platform_device. This protects against
	 * SMO88xx ACPI fwnodes which actually do have an I2C resource, which
	 * will already have an i2c_client instantiated (not a platform_device).
	 */
	dev = bus_find_device(&platform_bus_type, NULL, smo8800_ids, match_acpi_device_ids);
	if (!dev) {
		pr_debug("No SMO88xx platform-device found\n");
		return 0;
	}
	put_device(dev);

	lis3lv02d_dmi_id = dmi_first_match(lis3lv02d_devices);
	if (!lis3lv02d_dmi_id && !probe_i2c_addr) {
		pr_warn("accelerometer is present on SMBus but its address is unknown, skipping registration\n");
		pr_info("Pass dell_lis3lv02d.probe_i2c_addr=1 on the kernel command line to probe, this may be dangerous!\n");
		return 0;
	}

	if (lis3lv02d_dmi_id)
		i2c_addr = (long)lis3lv02d_dmi_id->driver_data;

	/*
	 * Register i2c-bus notifier + queue initial scan for lis3lv02d
	 * i2c_client instantiation.
	 */
	err = bus_register_notifier(&i2c_bus_type, &i2c_nb);
	if (err)
		return err;

	notifier_registered = true;

	queue_work(system_long_wq, &i2c_work);
	return 0;
}
module_init(dell_lis3lv02d_init);

static void __exit dell_lis3lv02d_module_exit(void)
{
	if (!notifier_registered)
		return;

	bus_unregister_notifier(&i2c_bus_type, &i2c_nb);
	cancel_work_sync(&i2c_work);
	i2c_unregister_device(i2c_dev);
}
module_exit(dell_lis3lv02d_module_exit);

MODULE_DESCRIPTION("lis3lv02d i2c-client instantiation for ACPI SMO88xx devices");
MODULE_AUTHOR("Hans de Goede <hansg@kernel.org>");
MODULE_LICENSE("GPL");
