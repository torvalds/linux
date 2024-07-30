// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Cherry Trail ACPI INT33FE pseudo device driver
 *
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 *
 * Some Intel Cherry Trail based device which ship with Windows 10, have
 * this weird INT33FE ACPI device with a CRS table with 4 I2cSerialBusV2
 * resources, for 4 different chips attached to various I²C buses:
 * 1. The Whiskey Cove PMIC, which is also described by the INT34D3 ACPI device
 * 2. Maxim MAX17047 Fuel Gauge Controller
 * 3. FUSB302 USB Type-C Controller
 * 4. PI3USB30532 USB switch
 *
 * So this driver is a stub / pseudo driver whose only purpose is to
 * instantiate I²C clients for chips 2 - 4, so that standard I²C drivers
 * for these chips can bind to the them.
 */

#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/pd.h>

struct cht_int33fe_data {
	struct i2c_client *battery_fg;
	struct i2c_client *fusb302;
	struct i2c_client *pi3usb30532;
	struct fwnode_handle *dp;
};

/*
 * Grrr, I severely dislike buggy BIOS-es. At least one BIOS enumerates
 * the max17047 both through the INT33FE ACPI device (it is right there
 * in the resources table) as well as through a separate MAX17047 device.
 *
 * These helpers are used to work around this by checking if an I²C client
 * for the max17047 has already been registered.
 */
static int cht_int33fe_check_for_max17047(struct device *dev, void *data)
{
	struct i2c_client **max17047 = data;
	struct acpi_device *adev;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return 0;

	/* The MAX17047 ACPI node doesn't have an UID, so we don't check that */
	if (!acpi_dev_hid_uid_match(adev, "MAX17047", NULL))
		return 0;

	*max17047 = to_i2c_client(dev);
	return 1;
}

static const char * const max17047_suppliers[] = { "bq24190-charger" };

static const struct property_entry max17047_properties[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", max17047_suppliers),
	{ }
};

static const struct software_node max17047_node = {
	.name = "max17047",
	.properties = max17047_properties,
};

/*
 * We are not using inline property here because those are constant,
 * and we need to adjust this one at runtime to point to real
 * software node.
 */
static struct software_node_ref_args fusb302_mux_refs[] = {
	{ .node = NULL },
};

static const struct property_entry fusb302_properties[] = {
	PROPERTY_ENTRY_STRING("linux,extcon-name", "cht_wcove_pwrsrc"),
	PROPERTY_ENTRY_REF_ARRAY("usb-role-switch", fusb302_mux_refs),
	{ }
};

static const struct software_node fusb302_node = {
	.name = "fusb302",
	.properties = fusb302_properties,
};

#define PDO_FIXED_FLAGS \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | PDO_FIXED_USB_COMM)

static const u32 src_pdo[] = {
	PDO_FIXED(5000, 1500, PDO_FIXED_FLAGS),
};

static const u32 snk_pdo[] = {
	PDO_FIXED(5000, 400, PDO_FIXED_FLAGS),
	PDO_VAR(5000, 12000, 3000),
};

static const struct software_node pi3usb30532_node = {
	.name = "pi3usb30532",
};

static const struct software_node displayport_node = {
	.name = "displayport",
};

static const struct property_entry usb_connector_properties[] = {
	PROPERTY_ENTRY_STRING("data-role", "dual"),
	PROPERTY_ENTRY_STRING("power-role", "dual"),
	PROPERTY_ENTRY_STRING("try-power-role", "sink"),
	PROPERTY_ENTRY_U32_ARRAY("source-pdos", src_pdo),
	PROPERTY_ENTRY_U32_ARRAY("sink-pdos", snk_pdo),
	PROPERTY_ENTRY_U32("op-sink-microwatt", 2500000),
	PROPERTY_ENTRY_REF("orientation-switch", &pi3usb30532_node),
	PROPERTY_ENTRY_REF("mode-switch", &pi3usb30532_node),
	PROPERTY_ENTRY_REF("displayport", &displayport_node),
	{ }
};

static const struct software_node usb_connector_node = {
	.name = "connector",
	.parent = &fusb302_node,
	.properties = usb_connector_properties,
};

static const struct software_node altmodes_node = {
	.name = "altmodes",
	.parent = &usb_connector_node,
};

static const struct property_entry dp_altmode_properties[] = {
	PROPERTY_ENTRY_U16("svid", 0xff01),
	PROPERTY_ENTRY_U32("vdo", 0x0c0086),
	{ }
};

static const struct software_node dp_altmode_node = {
	.name = "displayport-altmode",
	.parent = &altmodes_node,
	.properties = dp_altmode_properties,
};

static const struct software_node *node_group[] = {
	&fusb302_node,
	&max17047_node,
	&pi3usb30532_node,
	&displayport_node,
	&usb_connector_node,
	&altmodes_node,
	&dp_altmode_node,
	NULL
};

static int cht_int33fe_setup_dp(struct cht_int33fe_data *data)
{
	struct fwnode_handle *fwnode;
	struct pci_dev *pdev;

	fwnode = software_node_fwnode(&displayport_node);
	if (!fwnode)
		return -ENODEV;

	/* First let's find the GPU PCI device */
	pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, NULL);
	if (!pdev || pdev->vendor != PCI_VENDOR_ID_INTEL) {
		pci_dev_put(pdev);
		return -ENODEV;
	}

	/* Then the DP-2 child device node */
	data->dp = device_get_named_child_node(&pdev->dev, "DD04");
	pci_dev_put(pdev);
	if (!data->dp)
		return -ENODEV;

	fwnode->secondary = ERR_PTR(-ENODEV);
	data->dp->secondary = fwnode;

	return 0;
}

static void cht_int33fe_remove_nodes(struct cht_int33fe_data *data)
{
	software_node_unregister_node_group(node_group);

	if (fusb302_mux_refs[0].node) {
		fwnode_handle_put(software_node_fwnode(fusb302_mux_refs[0].node));
		fusb302_mux_refs[0].node = NULL;
	}

	if (data->dp) {
		data->dp->secondary = NULL;
		fwnode_handle_put(data->dp);
		data->dp = NULL;
	}
}

static int cht_int33fe_add_nodes(struct cht_int33fe_data *data)
{
	const struct software_node *mux_ref_node;
	int ret;

	/*
	 * There is no ACPI device node for the USB role mux, so we need to wait
	 * until the mux driver has created software node for the mux device.
	 * It means we depend on the mux driver. This function will return
	 * -EPROBE_DEFER until the mux device is registered.
	 */
	mux_ref_node = software_node_find_by_name(NULL, "intel-xhci-usb-sw");
	if (!mux_ref_node)
		return -EPROBE_DEFER;

	/*
	 * Update node used in "usb-role-switch" property. Note that we
	 * rely on software_node_register_node_group() to use the original
	 * instance of properties instead of copying them.
	 */
	fusb302_mux_refs[0].node = mux_ref_node;

	ret = software_node_register_node_group(node_group);
	if (ret)
		return ret;

	/* The devices that are not created in this driver need extra steps. */

	/*
	 * The DP connector does have ACPI device node. In this case we can just
	 * find that ACPI node and assign our node as the secondary node to it.
	 */
	ret = cht_int33fe_setup_dp(data);
	if (ret)
		goto err_remove_nodes;

	return 0;

err_remove_nodes:
	cht_int33fe_remove_nodes(data);

	return ret;
}

static int
cht_int33fe_register_max17047(struct device *dev, struct cht_int33fe_data *data)
{
	struct i2c_client *max17047 = NULL;
	struct i2c_board_info board_info;
	struct fwnode_handle *fwnode;
	int ret;

	fwnode = software_node_fwnode(&max17047_node);
	if (!fwnode)
		return -ENODEV;

	i2c_for_each_dev(&max17047, cht_int33fe_check_for_max17047);
	if (max17047) {
		/* Pre-existing I²C client for the max17047, add device properties */
		set_secondary_fwnode(&max17047->dev, fwnode);
		/* And re-probe to get the new device properties applied */
		ret = device_reprobe(&max17047->dev);
		if (ret)
			dev_warn(dev, "Reprobing max17047 error: %d\n", ret);
		return 0;
	}

	memset(&board_info, 0, sizeof(board_info));
	strscpy(board_info.type, "max17047");
	board_info.dev_name = "max17047";
	board_info.fwnode = fwnode;
	data->battery_fg = i2c_acpi_new_device(dev, 1, &board_info);

	return PTR_ERR_OR_ZERO(data->battery_fg);
}

static const struct dmi_system_id cht_int33fe_typec_ids[] = {
	{
		/*
		 * GPD win / GPD pocket mini laptops
		 *
		 * This DMI match may not seem unique, but it is. In the 67000+
		 * DMI decode dumps from linux-hardware.org only 116 have
		 * board_vendor set to "AMI Corporation" and of those 116 only
		 * the GPD win's and pocket's board_name is "Default string".
		 */
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "Default string"),
			DMI_EXACT_MATCH(DMI_BOARD_SERIAL, "Default string"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Default string"),
		},
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, cht_int33fe_typec_ids);

static int cht_int33fe_typec_probe(struct platform_device *pdev)
{
	struct i2c_board_info board_info;
	struct device *dev = &pdev->dev;
	struct cht_int33fe_data *data;
	struct fwnode_handle *fwnode;
	struct regulator *regulator;
	int fusb302_irq;
	int ret;

	if (!dmi_check_system(cht_int33fe_typec_ids))
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

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
	 * which is defined in i2c-cht-wc.c from where the bq24292i I²C client
	 * gets instantiated. We use regulator_get_optional here so that we
	 * don't end up getting a dummy-regulator ourselves.
	 */
	regulator = regulator_get_optional(dev, "cht_wc_usb_typec_vbus");
	if (IS_ERR(regulator)) {
		ret = PTR_ERR(regulator);
		return (ret == -ENODEV) ? -EPROBE_DEFER : ret;
	}
	regulator_put(regulator);

	/* The FUSB302 uses the IRQ at index 1 and is the only IRQ user */
	fusb302_irq = acpi_dev_gpio_irq_get(ACPI_COMPANION(dev), 1);
	if (fusb302_irq < 0) {
		if (fusb302_irq != -EPROBE_DEFER)
			dev_err(dev, "Error getting FUSB302 irq\n");
		return fusb302_irq;
	}

	ret = cht_int33fe_add_nodes(data);
	if (ret)
		return ret;

	/* Work around BIOS bug, see comment on cht_int33fe_check_for_max17047() */
	ret = cht_int33fe_register_max17047(dev, data);
	if (ret)
		goto out_remove_nodes;

	fwnode = software_node_fwnode(&fusb302_node);
	if (!fwnode) {
		ret = -ENODEV;
		goto out_unregister_max17047;
	}

	memset(&board_info, 0, sizeof(board_info));
	strscpy(board_info.type, "typec_fusb302");
	board_info.dev_name = "fusb302";
	board_info.fwnode = fwnode;
	board_info.irq = fusb302_irq;

	data->fusb302 = i2c_acpi_new_device(dev, 2, &board_info);
	if (IS_ERR(data->fusb302)) {
		ret = PTR_ERR(data->fusb302);
		goto out_unregister_max17047;
	}

	fwnode = software_node_fwnode(&pi3usb30532_node);
	if (!fwnode) {
		ret = -ENODEV;
		goto out_unregister_fusb302;
	}

	memset(&board_info, 0, sizeof(board_info));
	board_info.dev_name = "pi3usb30532";
	board_info.fwnode = fwnode;
	strscpy(board_info.type, "pi3usb30532");

	data->pi3usb30532 = i2c_acpi_new_device(dev, 3, &board_info);
	if (IS_ERR(data->pi3usb30532)) {
		ret = PTR_ERR(data->pi3usb30532);
		goto out_unregister_fusb302;
	}

	platform_set_drvdata(pdev, data);

	return 0;

out_unregister_fusb302:
	i2c_unregister_device(data->fusb302);

out_unregister_max17047:
	i2c_unregister_device(data->battery_fg);

out_remove_nodes:
	cht_int33fe_remove_nodes(data);

	return ret;
}

static void cht_int33fe_typec_remove(struct platform_device *pdev)
{
	struct cht_int33fe_data *data = platform_get_drvdata(pdev);

	i2c_unregister_device(data->pi3usb30532);
	i2c_unregister_device(data->fusb302);
	i2c_unregister_device(data->battery_fg);

	cht_int33fe_remove_nodes(data);
}

static const struct acpi_device_id cht_int33fe_acpi_ids[] = {
	{ "INT33FE", },
	{ }
};

static struct platform_driver cht_int33fe_typec_driver = {
	.driver	= {
		.name = "Intel Cherry Trail ACPI INT33FE Type-C driver",
		.acpi_match_table = ACPI_PTR(cht_int33fe_acpi_ids),
	},
	.probe = cht_int33fe_typec_probe,
	.remove_new = cht_int33fe_typec_remove,
};

module_platform_driver(cht_int33fe_typec_driver);

MODULE_DESCRIPTION("Intel Cherry Trail ACPI INT33FE Type-C pseudo device driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL v2");
