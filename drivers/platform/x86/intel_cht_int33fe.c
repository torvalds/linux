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
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#define EXPECTED_PTYPE		4

struct cht_int33fe_data {
	struct i2c_client *max17047;
	struct i2c_client *fusb302;
	struct i2c_client *pi3usb30532;
	/* Contain a list-head must be per device */
	struct device_connection connections[5];
};

/*
 * Grrr I severly dislike buggy BIOS-es. At least one BIOS enumerates
 * the max17047 both through the INT33FE ACPI device (it is right there
 * in the resources table) as well as through a separate MAX17047 device.
 *
 * These helpers are used to work around this by checking if an i2c-client
 * for the max17047 has already been registered.
 */
static int cht_int33fe_check_for_max17047(struct device *dev, void *data)
{
	struct i2c_client **max17047 = data;
	struct acpi_device *adev;
	const char *hid;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return 0;

	hid = acpi_device_hid(adev);

	/* The MAX17047 ACPI node doesn't have an UID, so we don't check that */
	if (strcmp(hid, "MAX17047"))
		return 0;

	*max17047 = to_i2c_client(dev);
	return 1;
}

static struct i2c_client *cht_int33fe_find_max17047(void)
{
	struct i2c_client *max17047 = NULL;

	i2c_for_each_dev(&max17047, cht_int33fe_check_for_max17047);
	return max17047;
}

static const char * const max17047_suppliers[] = { "bq24190-charger" };

static const struct property_entry max17047_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", max17047_suppliers),
	{ }
};

static const struct property_entry fusb302_props[] = {
	PROPERTY_ENTRY_STRING("fcs,extcon-name", "cht_wcove_pwrsrc"),
	PROPERTY_ENTRY_U32("fcs,max-sink-microvolt", 12000000),
	PROPERTY_ENTRY_U32("fcs,max-sink-microamp",   3000000),
	PROPERTY_ENTRY_U32("fcs,max-sink-microwatt", 36000000),
	{ }
};

static int cht_int33fe_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct i2c_board_info board_info;
	struct cht_int33fe_data *data;
	struct i2c_client *max17047;
	struct regulator *regulator;
	unsigned long long ptyp;
	acpi_status status;
	int fusb302_irq;
	int ret;

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

	/* Check presence of INT34D3 (hardware-rev 3) expected for ptype == 4 */
	if (!acpi_dev_present("INT34D3", "1", 3)) {
		dev_err(dev, "Error PTYPE == %d, but no INT34D3 device\n",
			EXPECTED_PTYPE);
		return -ENODEV;
	}

	/*
	 * We expect the WC PMIC to be paired with a TI bq24292i charger-IC.
	 * We check for the bq24292i vbus regulator here, this has 2 purposes:
	 * 1) The bq24292i allows charging with up to 12V, setting the fusb302's
	 *    max-snk voltage to 12V with another charger-IC is not good.
	 * 2) For the fusb302 driver to get the bq24292i vbus regulator, the
	 *    regulator-map, which is part of the bq24292i regulator_init_data,
	 *    must be registered before the fusb302 is instantiated, otherwise
	 *    it will end up with a dummy-regulator.
	 * Note "cht_wc_usb_typec_vbus" comes from the regulator_init_data
	 * which is defined in i2c-cht-wc.c from where the bq24292i i2c-client
	 * gets instantiated. We use regulator_get_optional here so that we
	 * don't end up getting a dummy-regulator ourselves.
	 */
	regulator = regulator_get_optional(dev, "cht_wc_usb_typec_vbus");
	if (IS_ERR(regulator)) {
		ret = PTR_ERR(regulator);
		return (ret == -ENODEV) ? -EPROBE_DEFER : ret;
	}
	regulator_put(regulator);

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

	/* Work around BIOS bug, see comment on cht_int33fe_find_max17047 */
	max17047 = cht_int33fe_find_max17047();
	if (max17047) {
		/* Pre-existing i2c-client for the max17047, add device-props */
		ret = device_add_properties(&max17047->dev, max17047_props);
		if (ret)
			return ret;
		/* And re-probe to get the new device-props applied. */
		ret = device_reprobe(&max17047->dev);
		if (ret)
			dev_warn(dev, "Reprobing max17047 error: %d\n", ret);
	} else {
		memset(&board_info, 0, sizeof(board_info));
		strlcpy(board_info.type, "max17047", I2C_NAME_SIZE);
		board_info.dev_name = "max17047";
		board_info.properties = max17047_props;
		data->max17047 = i2c_acpi_new_device(dev, 1, &board_info);
		if (!data->max17047)
			return -EPROBE_DEFER; /* Wait for i2c-adapter to load */
	}

	data->connections[0].endpoint[0] = "port0";
	data->connections[0].endpoint[1] = "i2c-pi3usb30532";
	data->connections[0].id = "typec-switch";
	data->connections[1].endpoint[0] = "port0";
	data->connections[1].endpoint[1] = "i2c-pi3usb30532";
	data->connections[1].id = "typec-mux";
	data->connections[2].endpoint[0] = "port0";
	data->connections[2].endpoint[1] = "i2c-pi3usb30532";
	data->connections[2].id = "idff01m01";
	data->connections[3].endpoint[0] = "i2c-fusb302";
	data->connections[3].endpoint[1] = "intel_xhci_usb_sw-role-switch";
	data->connections[3].id = "usb-role-switch";

	device_connections_add(data->connections);

	memset(&board_info, 0, sizeof(board_info));
	strlcpy(board_info.type, "typec_fusb302", I2C_NAME_SIZE);
	board_info.dev_name = "fusb302";
	board_info.properties = fusb302_props;
	board_info.irq = fusb302_irq;

	data->fusb302 = i2c_acpi_new_device(dev, 2, &board_info);
	if (!data->fusb302)
		goto out_unregister_max17047;

	memset(&board_info, 0, sizeof(board_info));
	board_info.dev_name = "pi3usb30532";
	strlcpy(board_info.type, "pi3usb30532", I2C_NAME_SIZE);

	data->pi3usb30532 = i2c_acpi_new_device(dev, 3, &board_info);
	if (!data->pi3usb30532)
		goto out_unregister_fusb302;

	platform_set_drvdata(pdev, data);

	return 0;

out_unregister_fusb302:
	i2c_unregister_device(data->fusb302);

out_unregister_max17047:
	if (data->max17047)
		i2c_unregister_device(data->max17047);

	device_connections_remove(data->connections);

	return -EPROBE_DEFER; /* Wait for the i2c-adapter to load */
}

static int cht_int33fe_remove(struct platform_device *pdev)
{
	struct cht_int33fe_data *data = platform_get_drvdata(pdev);

	i2c_unregister_device(data->pi3usb30532);
	i2c_unregister_device(data->fusb302);
	if (data->max17047)
		i2c_unregister_device(data->max17047);

	device_connections_remove(data->connections);

	return 0;
}

static const struct acpi_device_id cht_int33fe_acpi_ids[] = {
	{ "INT33FE", },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cht_int33fe_acpi_ids);

static struct platform_driver cht_int33fe_driver = {
	.driver	= {
		.name = "Intel Cherry Trail ACPI INT33FE driver",
		.acpi_match_table = ACPI_PTR(cht_int33fe_acpi_ids),
	},
	.probe = cht_int33fe_probe,
	.remove = cht_int33fe_remove,
};

module_platform_driver(cht_int33fe_driver);

MODULE_DESCRIPTION("Intel Cherry Trail ACPI INT33FE pseudo device driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
