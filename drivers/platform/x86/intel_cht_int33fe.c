// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Cherry Trail ACPI INT33FE pseudo device driver
 *
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
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
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/pd.h>

#define EXPECTED_PTYPE		4

enum {
	INT33FE_NODE_FUSB302,
	INT33FE_NODE_MAX17047,
	INT33FE_NODE_PI3USB30532,
	INT33FE_NODE_DISPLAYPORT,
	INT33FE_NODE_ROLE_SWITCH,
	INT33FE_NODE_USB_CONNECTOR,
	INT33FE_NODE_MAX,
};

struct cht_int33fe_data {
	struct i2c_client *max17047;
	struct i2c_client *fusb302;
	struct i2c_client *pi3usb30532;

	struct fwnode_handle *dp;
	struct fwnode_handle *mux;
};

static const struct software_node nodes[];

static const struct software_node_ref_args pi3usb30532_ref = {
	&nodes[INT33FE_NODE_PI3USB30532]
};

static const struct software_node_ref_args dp_ref = {
	&nodes[INT33FE_NODE_DISPLAYPORT]
};

static struct software_node_ref_args mux_ref;

static const struct software_node_reference usb_connector_refs[] = {
	{ "orientation-switch", 1, &pi3usb30532_ref},
	{ "mode-switch", 1, &pi3usb30532_ref},
	{ "displayport", 1, &dp_ref},
	{ }
};

static const struct software_node_reference fusb302_refs[] = {
	{ "usb-role-switch", 1, &mux_ref},
	{ }
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

static const char * const max17047_suppliers[] = { "bq24190-charger" };

static const struct property_entry max17047_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", max17047_suppliers),
	{ }
};

static const struct property_entry fusb302_props[] = {
	PROPERTY_ENTRY_STRING("linux,extcon-name", "cht_wcove_pwrsrc"),
	{ }
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

static const struct property_entry usb_connector_props[] = {
	PROPERTY_ENTRY_STRING("data-role", "dual"),
	PROPERTY_ENTRY_STRING("power-role", "dual"),
	PROPERTY_ENTRY_STRING("try-power-role", "sink"),
	PROPERTY_ENTRY_U32_ARRAY("source-pdos", src_pdo),
	PROPERTY_ENTRY_U32_ARRAY("sink-pdos", snk_pdo),
	PROPERTY_ENTRY_U32("op-sink-microwatt", 2500000),
	{ }
};

static const struct software_node nodes[] = {
	{ "fusb302", NULL, fusb302_props, fusb302_refs },
	{ "max17047", NULL, max17047_props },
	{ "pi3usb30532" },
	{ "displayport" },
	{ "usb-role-switch" },
	{ "connector", &nodes[0], usb_connector_props, usb_connector_refs },
	{ }
};

static int cht_int33fe_setup_mux(struct cht_int33fe_data *data)
{
	struct fwnode_handle *fwnode;
	struct device *dev;
	struct device *p;

	fwnode = software_node_fwnode(&nodes[INT33FE_NODE_ROLE_SWITCH]);
	if (!fwnode)
		return -ENODEV;

	/* First finding the platform device */
	p = bus_find_device_by_name(&platform_bus_type, NULL,
				    "intel_xhci_usb_sw");
	if (!p)
		return -EPROBE_DEFER;

	/* Then the mux child device */
	dev = device_find_child_by_name(p, "intel_xhci_usb_sw-role-switch");
	put_device(p);
	if (!dev)
		return -EPROBE_DEFER;

	/* If there already is a node for the mux, using that one. */
	if (dev->fwnode)
		fwnode_remove_software_node(fwnode);
	else
		dev->fwnode = fwnode;

	data->mux = fwnode_handle_get(dev->fwnode);
	put_device(dev);
	mux_ref.node = to_software_node(data->mux);

	return 0;
}

static int cht_int33fe_setup_dp(struct cht_int33fe_data *data)
{
	struct fwnode_handle *fwnode;
	struct pci_dev *pdev;

	fwnode = software_node_fwnode(&nodes[INT33FE_NODE_DISPLAYPORT]);
	if (!fwnode)
		return -ENODEV;

	/* First let's find the GPU PCI device */
	pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, NULL);
	if (!pdev || pdev->vendor != PCI_VENDOR_ID_INTEL) {
		pci_dev_put(pdev);
		return -ENODEV;
	}

	/* Then the DP child device node */
	data->dp = device_get_named_child_node(&pdev->dev, "DD02");
	pci_dev_put(pdev);
	if (!data->dp)
		return -ENODEV;

	fwnode->secondary = ERR_PTR(-ENODEV);
	data->dp->secondary = fwnode;

	return 0;
}

static void cht_int33fe_remove_nodes(struct cht_int33fe_data *data)
{
	software_node_unregister_nodes(nodes);

	if (data->mux) {
		fwnode_handle_put(data->mux);
		mux_ref.node = NULL;
		data->mux = NULL;
	}

	if (data->dp) {
		data->dp->secondary = NULL;
		fwnode_handle_put(data->dp);
		data->dp = NULL;
	}
}

static int cht_int33fe_add_nodes(struct cht_int33fe_data *data)
{
	int ret;

	ret = software_node_register_nodes(nodes);
	if (ret)
		return ret;

	/* The devices that are not created in this driver need extra steps. */

	/*
	 * There is no ACPI device node for the USB role mux, so we need to find
	 * the mux device and assign our node directly to it. That means we
	 * depend on the mux driver. This function will return -PROBE_DEFER
	 * until the mux device is registered.
	 */
	ret = cht_int33fe_setup_mux(data);
	if (ret)
		goto err_remove_nodes;

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

	fwnode = software_node_fwnode(&nodes[INT33FE_NODE_MAX17047]);
	if (!fwnode)
		return -ENODEV;

	i2c_for_each_dev(&max17047, cht_int33fe_check_for_max17047);
	if (max17047) {
		/* Pre-existing i2c-client for the max17047, add device-props */
		fwnode->secondary = ERR_PTR(-ENODEV);
		max17047->dev.fwnode->secondary = fwnode;
		/* And re-probe to get the new device-props applied. */
		ret = device_reprobe(&max17047->dev);
		if (ret)
			dev_warn(dev, "Reprobing max17047 error: %d\n", ret);
		return 0;
	}

	memset(&board_info, 0, sizeof(board_info));
	strlcpy(board_info.type, "max17047", I2C_NAME_SIZE);
	board_info.dev_name = "max17047";
	board_info.fwnode = fwnode;
	data->max17047 = i2c_acpi_new_device(dev, 1, &board_info);

	return PTR_ERR_OR_ZERO(data->max17047);
}

static int cht_int33fe_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct i2c_board_info board_info;
	struct cht_int33fe_data *data;
	struct fwnode_handle *fwnode;
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

	ret = cht_int33fe_add_nodes(data);
	if (ret)
		return ret;

	/* Work around BIOS bug, see comment on cht_int33fe_check_for_max17047 */
	ret = cht_int33fe_register_max17047(dev, data);
	if (ret)
		goto out_remove_nodes;

	fwnode = software_node_fwnode(&nodes[INT33FE_NODE_FUSB302]);
	if (!fwnode) {
		ret = -ENODEV;
		goto out_unregister_max17047;
	}

	memset(&board_info, 0, sizeof(board_info));
	strlcpy(board_info.type, "typec_fusb302", I2C_NAME_SIZE);
	board_info.dev_name = "fusb302";
	board_info.fwnode = fwnode;
	board_info.irq = fusb302_irq;

	data->fusb302 = i2c_acpi_new_device(dev, 2, &board_info);
	if (IS_ERR(data->fusb302)) {
		ret = PTR_ERR(data->fusb302);
		goto out_unregister_max17047;
	}

	fwnode = software_node_fwnode(&nodes[INT33FE_NODE_PI3USB30532]);
	if (!fwnode) {
		ret = -ENODEV;
		goto out_unregister_fusb302;
	}

	memset(&board_info, 0, sizeof(board_info));
	board_info.dev_name = "pi3usb30532";
	board_info.fwnode = fwnode;
	strlcpy(board_info.type, "pi3usb30532", I2C_NAME_SIZE);

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
	i2c_unregister_device(data->max17047);

out_remove_nodes:
	cht_int33fe_remove_nodes(data);

	return ret;
}

static int cht_int33fe_remove(struct platform_device *pdev)
{
	struct cht_int33fe_data *data = platform_get_drvdata(pdev);

	i2c_unregister_device(data->pi3usb30532);
	i2c_unregister_device(data->fusb302);
	i2c_unregister_device(data->max17047);

	cht_int33fe_remove_nodes(data);

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
MODULE_LICENSE("GPL v2");
