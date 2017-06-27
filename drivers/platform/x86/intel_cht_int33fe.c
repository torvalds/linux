/*
 * Intel Cherry Trail ACPI INT33FE pseudo device driver
 *
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Some Intel Cherry Trail based device which ship with Windows 10, have
 * this weird INT33FE ACPI device with a CRS table with 4 I2cSerialBusV2
 * resources, for 4 different chips attached to various i2c busses:
 * 1. The Whiskey Cove pmic, which is also described by the INT34D3 ACPI device
 * 2. Maxim MAX17047 Fuel Gauge Controller
 * 3. FUSB302 USB Type-C Controller
 * 4. PI3USB30532 USB switch
 *
 * So this driver is a stub / pseudo driver whose only purpose is to
 * instantiate i2c-clients for chips 2 - 4, so that standard i2c drivers
 * for these chips can bind to the them.
 */

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>

#define EXPECTED_PTYPE		4

struct cht_int33fe_data {
	struct i2c_client *max17047;
	struct i2c_client *fusb302;
	struct i2c_client *pi3usb30532;
};

static int cht_int33fe_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct i2c_board_info board_info;
	struct cht_int33fe_data *data;
	unsigned long long ptyp;
	acpi_status status;
	int fusb302_irq;

	status = acpi_evaluate_integer(ACPI_HANDLE(dev), "PTYP", NULL, &ptyp);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "Error getting PTYPE\n");
		return -ENODEV;
	}

	/*
	 * The same ACPI HID is used for different configurations check PTYP
	 * to ensure that we are dealing with the expected config.
	 */
	if (ptyp != EXPECTED_PTYPE)
		return -ENODEV;

	/* The FUSB302 uses the irq at index 1 and is the only irq user */
	fusb302_irq = acpi_dev_gpio_irq_get(ACPI_COMPANION(dev), 1);
	if (fusb302_irq < 0) {
		if (fusb302_irq != -EPROBE_DEFER)
			dev_err(dev, "Error getting FUSB302 irq\n");
		return fusb302_irq;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memset(&board_info, 0, sizeof(board_info));
	strlcpy(board_info.type, "max17047", I2C_NAME_SIZE);

	data->max17047 = i2c_acpi_new_device(dev, 1, &board_info);
	if (!data->max17047)
		return -EPROBE_DEFER; /* Wait for the i2c-adapter to load */

	memset(&board_info, 0, sizeof(board_info));
	strlcpy(board_info.type, "fusb302", I2C_NAME_SIZE);
	board_info.irq = fusb302_irq;

	data->fusb302 = i2c_acpi_new_device(dev, 2, &board_info);
	if (!data->fusb302)
		goto out_unregister_max17047;

	memset(&board_info, 0, sizeof(board_info));
	strlcpy(board_info.type, "pi3usb30532", I2C_NAME_SIZE);

	data->pi3usb30532 = i2c_acpi_new_device(dev, 3, &board_info);
	if (!data->pi3usb30532)
		goto out_unregister_fusb302;

	i2c_set_clientdata(client, data);

	return 0;

out_unregister_fusb302:
	i2c_unregister_device(data->fusb302);

out_unregister_max17047:
	i2c_unregister_device(data->max17047);

	return -EPROBE_DEFER; /* Wait for the i2c-adapter to load */
}

static int cht_int33fe_remove(struct i2c_client *i2c)
{
	struct cht_int33fe_data *data = i2c_get_clientdata(i2c);

	i2c_unregister_device(data->pi3usb30532);
	i2c_unregister_device(data->fusb302);
	i2c_unregister_device(data->max17047);

	return 0;
}

static const struct i2c_device_id cht_int33fe_i2c_id[] = {
	{ }
};
MODULE_DEVICE_TABLE(i2c, cht_int33fe_i2c_id);

static const struct acpi_device_id cht_int33fe_acpi_ids[] = {
	{ "INT33FE", },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cht_int33fe_acpi_ids);

static struct i2c_driver cht_int33fe_driver = {
	.driver	= {
		.name = "Intel Cherry Trail ACPI INT33FE driver",
		.acpi_match_table = ACPI_PTR(cht_int33fe_acpi_ids),
	},
	.probe_new = cht_int33fe_probe,
	.remove = cht_int33fe_remove,
	.id_table = cht_int33fe_i2c_id,
	.disable_i2c_core_irq_mapping = true,
};

module_i2c_driver(cht_int33fe_driver);

MODULE_DESCRIPTION("Intel Cherry Trail ACPI INT33FE pseudo device driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
