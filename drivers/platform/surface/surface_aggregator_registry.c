// SPDX-License-Identifier: GPL-2.0+
/*
 * Surface System Aggregator Module (SSAM) client device registry.
 *
 * Registry for non-platform/non-ACPI SSAM client devices, i.e. devices that
 * cannot be auto-detected. Provides device-hubs and performs instantiation
 * for these devices.
 *
 * Copyright (C) 2020-2022 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/types.h>

#include <linux/surface_aggregator/device.h>


/* -- Device registry. ------------------------------------------------------ */

/*
 * SSAM device names follow the SSAM module alias, meaning they are prefixed
 * with 'ssam:', followed by domain, category, target ID, instance ID, and
 * function, each encoded as two-digit hexadecimal, separated by ':'. In other
 * words, it follows the scheme
 *
 *      ssam:dd:cc:tt:ii:ff
 *
 * Where, 'dd', 'cc', 'tt', 'ii', and 'ff' are the two-digit hexadecimal
 * values mentioned above, respectively.
 */

/* Root node. */
static const struct software_node ssam_node_root = {
	.name = "ssam_platform_hub",
};

/* KIP device hub (connects keyboard cover devices on Surface Pro 8). */
static const struct software_node ssam_node_hub_kip = {
	.name = "ssam:00:00:01:0e:00",
	.parent = &ssam_node_root,
};

/* Base device hub (devices attached to Surface Book 3 base). */
static const struct software_node ssam_node_hub_base = {
	.name = "ssam:00:00:01:11:00",
	.parent = &ssam_node_root,
};

/* AC adapter. */
static const struct software_node ssam_node_bat_ac = {
	.name = "ssam:01:02:01:01:01",
	.parent = &ssam_node_root,
};

/* Primary battery. */
static const struct software_node ssam_node_bat_main = {
	.name = "ssam:01:02:01:01:00",
	.parent = &ssam_node_root,
};

/* Secondary battery (Surface Book 3). */
static const struct software_node ssam_node_bat_sb3base = {
	.name = "ssam:01:02:02:01:00",
	.parent = &ssam_node_hub_base,
};

/* Platform profile / performance-mode device without a fan. */
static const struct software_node ssam_node_tmp_perf_profile = {
	.name = "ssam:01:03:01:00:01",
	.parent = &ssam_node_root,
};

/* Platform profile / performance-mode device with a fan, such that
 * the fan controller profile can also be switched.
 */
static const struct property_entry ssam_node_tmp_perf_profile_has_fan[] = {
	PROPERTY_ENTRY_BOOL("has_fan"),
	{ }
};

static const struct software_node ssam_node_tmp_perf_profile_with_fan = {
	.name = "ssam:01:03:01:00:01",
	.parent = &ssam_node_root,
	.properties = ssam_node_tmp_perf_profile_has_fan,
};

/* Thermal sensors. */
static const struct software_node ssam_node_tmp_sensors = {
	.name = "ssam:01:03:01:00:02",
	.parent = &ssam_node_root,
};

/* Fan speed function. */
static const struct software_node ssam_node_fan_speed = {
	.name = "ssam:01:05:01:01:01",
	.parent = &ssam_node_root,
};

/* Tablet-mode switch via KIP subsystem. */
static const struct software_node ssam_node_kip_tablet_switch = {
	.name = "ssam:01:0e:01:00:01",
	.parent = &ssam_node_root,
};

/* DTX / detachment-system device (Surface Book 3). */
static const struct software_node ssam_node_bas_dtx = {
	.name = "ssam:01:11:01:00:00",
	.parent = &ssam_node_root,
};

/* HID keyboard (SAM, TID=1). */
static const struct software_node ssam_node_hid_sam_keyboard = {
	.name = "ssam:01:15:01:01:00",
	.parent = &ssam_node_root,
};

/* HID pen stash (SAM, TID=1; pen taken / stashed away evens). */
static const struct software_node ssam_node_hid_sam_penstash = {
	.name = "ssam:01:15:01:02:00",
	.parent = &ssam_node_root,
};

/* HID touchpad (SAM, TID=1). */
static const struct software_node ssam_node_hid_sam_touchpad = {
	.name = "ssam:01:15:01:03:00",
	.parent = &ssam_node_root,
};

/* HID device instance 6 (SAM, TID=1, HID sensor collection). */
static const struct software_node ssam_node_hid_sam_sensors = {
	.name = "ssam:01:15:01:06:00",
	.parent = &ssam_node_root,
};

/* HID device instance 7 (SAM, TID=1, UCM UCSI HID client). */
static const struct software_node ssam_node_hid_sam_ucm_ucsi = {
	.name = "ssam:01:15:01:07:00",
	.parent = &ssam_node_root,
};

/* HID system controls (SAM, TID=1). */
static const struct software_node ssam_node_hid_sam_sysctrl = {
	.name = "ssam:01:15:01:08:00",
	.parent = &ssam_node_root,
};

/* HID keyboard. */
static const struct software_node ssam_node_hid_main_keyboard = {
	.name = "ssam:01:15:02:01:00",
	.parent = &ssam_node_root,
};

/* HID touchpad. */
static const struct software_node ssam_node_hid_main_touchpad = {
	.name = "ssam:01:15:02:03:00",
	.parent = &ssam_node_root,
};

/* HID device instance 5 (unknown HID device). */
static const struct software_node ssam_node_hid_main_iid5 = {
	.name = "ssam:01:15:02:05:00",
	.parent = &ssam_node_root,
};

/* HID keyboard (base hub). */
static const struct software_node ssam_node_hid_base_keyboard = {
	.name = "ssam:01:15:02:01:00",
	.parent = &ssam_node_hub_base,
};

/* HID touchpad (base hub). */
static const struct software_node ssam_node_hid_base_touchpad = {
	.name = "ssam:01:15:02:03:00",
	.parent = &ssam_node_hub_base,
};

/* HID device instance 5 (unknown HID device, base hub). */
static const struct software_node ssam_node_hid_base_iid5 = {
	.name = "ssam:01:15:02:05:00",
	.parent = &ssam_node_hub_base,
};

/* HID device instance 6 (unknown HID device, base hub). */
static const struct software_node ssam_node_hid_base_iid6 = {
	.name = "ssam:01:15:02:06:00",
	.parent = &ssam_node_hub_base,
};

/* HID keyboard (KIP hub). */
static const struct software_node ssam_node_hid_kip_keyboard = {
	.name = "ssam:01:15:02:01:00",
	.parent = &ssam_node_hub_kip,
};

/* HID pen stash (KIP hub; pen taken / stashed away evens). */
static const struct software_node ssam_node_hid_kip_penstash = {
	.name = "ssam:01:15:02:02:00",
	.parent = &ssam_node_hub_kip,
};

/* HID touchpad (KIP hub). */
static const struct software_node ssam_node_hid_kip_touchpad = {
	.name = "ssam:01:15:02:03:00",
	.parent = &ssam_node_hub_kip,
};

/* HID device instance 5 (KIP hub, type-cover firmware update). */
static const struct software_node ssam_node_hid_kip_fwupd = {
	.name = "ssam:01:15:02:05:00",
	.parent = &ssam_node_hub_kip,
};

/* Tablet-mode switch via POS subsystem. */
static const struct software_node ssam_node_pos_tablet_switch = {
	.name = "ssam:01:26:01:00:01",
	.parent = &ssam_node_root,
};

/*
 * Devices for 5th- and 6th-generations models:
 * - Surface Book 2,
 * - Surface Laptop 1 and 2,
 * - Surface Pro 5 and 6.
 */
static const struct software_node *ssam_node_group_gen5[] = {
	&ssam_node_root,
	&ssam_node_tmp_perf_profile,
	NULL,
};

/* Devices for Surface Book 3. */
static const struct software_node *ssam_node_group_sb3[] = {
	&ssam_node_root,
	&ssam_node_hub_base,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_bat_sb3base,
	&ssam_node_tmp_perf_profile,
	&ssam_node_bas_dtx,
	&ssam_node_hid_base_keyboard,
	&ssam_node_hid_base_touchpad,
	&ssam_node_hid_base_iid5,
	&ssam_node_hid_base_iid6,
	NULL,
};

/* Devices for Surface Laptop 3 and 4. */
static const struct software_node *ssam_node_group_sl3[] = {
	&ssam_node_root,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_perf_profile,
	&ssam_node_hid_main_keyboard,
	&ssam_node_hid_main_touchpad,
	&ssam_node_hid_main_iid5,
	NULL,
};

/* Devices for Surface Laptop 5. */
static const struct software_node *ssam_node_group_sl5[] = {
	&ssam_node_root,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_perf_profile_with_fan,
	&ssam_node_tmp_sensors,
	&ssam_node_fan_speed,
	&ssam_node_hid_main_keyboard,
	&ssam_node_hid_main_touchpad,
	&ssam_node_hid_main_iid5,
	&ssam_node_hid_sam_ucm_ucsi,
	NULL,
};

/* Devices for Surface Laptop 6. */
static const struct software_node *ssam_node_group_sl6[] = {
	&ssam_node_root,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_perf_profile_with_fan,
	&ssam_node_tmp_sensors,
	&ssam_node_fan_speed,
	&ssam_node_hid_main_keyboard,
	&ssam_node_hid_main_touchpad,
	&ssam_node_hid_main_iid5,
	&ssam_node_hid_sam_sensors,
	&ssam_node_hid_sam_ucm_ucsi,
	NULL,
};

/* Devices for Surface Laptop 7. */
static const struct software_node *ssam_node_group_sl7[] = {
	&ssam_node_root,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_perf_profile_with_fan,
	&ssam_node_fan_speed,
	&ssam_node_hid_sam_keyboard,
	/* TODO: evaluate thermal sensors devices when we get a driver for that */
	NULL,
};

/* Devices for Surface Laptop Studio 1. */
static const struct software_node *ssam_node_group_sls1[] = {
	&ssam_node_root,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_perf_profile,
	&ssam_node_pos_tablet_switch,
	&ssam_node_hid_sam_keyboard,
	&ssam_node_hid_sam_penstash,
	&ssam_node_hid_sam_touchpad,
	&ssam_node_hid_sam_sensors,
	&ssam_node_hid_sam_ucm_ucsi,
	&ssam_node_hid_sam_sysctrl,
	NULL,
};

/* Devices for Surface Laptop Studio 2. */
static const struct software_node *ssam_node_group_sls2[] = {
	&ssam_node_root,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_perf_profile_with_fan,
	&ssam_node_tmp_sensors,
	&ssam_node_fan_speed,
	&ssam_node_pos_tablet_switch,
	&ssam_node_hid_sam_keyboard,
	&ssam_node_hid_sam_penstash,
	&ssam_node_hid_sam_sensors,
	&ssam_node_hid_sam_ucm_ucsi,
	NULL,
};

/* Devices for Surface Laptop Go. */
static const struct software_node *ssam_node_group_slg1[] = {
	&ssam_node_root,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_perf_profile,
	NULL,
};

/* Devices for Surface Pro 7 and Surface Pro 7+. */
static const struct software_node *ssam_node_group_sp7[] = {
	&ssam_node_root,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_perf_profile,
	NULL,
};

/* Devices for Surface Pro 8 */
static const struct software_node *ssam_node_group_sp8[] = {
	&ssam_node_root,
	&ssam_node_hub_kip,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_perf_profile,
	&ssam_node_kip_tablet_switch,
	&ssam_node_hid_kip_keyboard,
	&ssam_node_hid_kip_penstash,
	&ssam_node_hid_kip_touchpad,
	&ssam_node_hid_kip_fwupd,
	&ssam_node_hid_sam_sensors,
	&ssam_node_hid_sam_ucm_ucsi,
	NULL,
};

/* Devices for Surface Pro 9 and 10 */
static const struct software_node *ssam_node_group_sp9[] = {
	&ssam_node_root,
	&ssam_node_hub_kip,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_perf_profile_with_fan,
	&ssam_node_tmp_sensors,
	&ssam_node_fan_speed,
	&ssam_node_pos_tablet_switch,
	&ssam_node_hid_kip_keyboard,
	&ssam_node_hid_kip_penstash,
	&ssam_node_hid_kip_touchpad,
	&ssam_node_hid_kip_fwupd,
	&ssam_node_hid_sam_sensors,
	&ssam_node_hid_sam_ucm_ucsi,
	NULL,
};


/* -- SSAM platform/meta-hub driver. ---------------------------------------- */

static const struct acpi_device_id ssam_platform_hub_acpi_match[] = {
	/* Surface Pro 4, 5, and 6 (OMBR < 0x10) */
	{ "MSHW0081", (unsigned long)ssam_node_group_gen5 },

	/* Surface Pro 6 (OMBR >= 0x10) */
	{ "MSHW0111", (unsigned long)ssam_node_group_gen5 },

	/* Surface Pro 7 */
	{ "MSHW0116", (unsigned long)ssam_node_group_sp7 },

	/* Surface Pro 7+ */
	{ "MSHW0119", (unsigned long)ssam_node_group_sp7 },

	/* Surface Pro 8 */
	{ "MSHW0263", (unsigned long)ssam_node_group_sp8 },

	/* Surface Pro 9 */
	{ "MSHW0343", (unsigned long)ssam_node_group_sp9 },

	/* Surface Pro 10 */
	{ "MSHW0510", (unsigned long)ssam_node_group_sp9 },

	/* Surface Book 2 */
	{ "MSHW0107", (unsigned long)ssam_node_group_gen5 },

	/* Surface Book 3 */
	{ "MSHW0117", (unsigned long)ssam_node_group_sb3 },

	/* Surface Laptop 1 */
	{ "MSHW0086", (unsigned long)ssam_node_group_gen5 },

	/* Surface Laptop 2 */
	{ "MSHW0112", (unsigned long)ssam_node_group_gen5 },

	/* Surface Laptop 3 (13", Intel) */
	{ "MSHW0114", (unsigned long)ssam_node_group_sl3 },

	/* Surface Laptop 3 (15", AMD) and 4 (15", AMD) */
	{ "MSHW0110", (unsigned long)ssam_node_group_sl3 },

	/* Surface Laptop 4 (13", Intel) */
	{ "MSHW0250", (unsigned long)ssam_node_group_sl3 },

	/* Surface Laptop 5 */
	{ "MSHW0350", (unsigned long)ssam_node_group_sl5 },

	/* Surface Laptop 6 */
	{ "MSHW0530", (unsigned long)ssam_node_group_sl6 },

	/* Surface Laptop Go 1 */
	{ "MSHW0118", (unsigned long)ssam_node_group_slg1 },

	/* Surface Laptop Go 2 */
	{ "MSHW0290", (unsigned long)ssam_node_group_slg1 },

	/* Surface Laptop Go 3 */
	{ "MSHW0440", (unsigned long)ssam_node_group_slg1 },

	/* Surface Laptop Studio 1 */
	{ "MSHW0123", (unsigned long)ssam_node_group_sls1 },

	/* Surface Laptop Studio 2 */
	{ "MSHW0360", (unsigned long)ssam_node_group_sls2 },

	{ },
};
MODULE_DEVICE_TABLE(acpi, ssam_platform_hub_acpi_match);

static const struct of_device_id ssam_platform_hub_of_match[] __maybe_unused = {
	/* Surface Laptop 7 */
	{ .compatible = "microsoft,romulus13", (void *)ssam_node_group_sl7 },
	{ .compatible = "microsoft,romulus15", (void *)ssam_node_group_sl7 },
	{ },
};

static int ssam_platform_hub_probe(struct platform_device *pdev)
{
	const struct software_node **nodes;
	const struct of_device_id *match;
	struct device_node *fdt_root;
	struct ssam_controller *ctrl;
	struct fwnode_handle *root;
	int status;

	nodes = (const struct software_node **)acpi_device_get_match_data(&pdev->dev);
	if (!nodes) {
		fdt_root = of_find_node_by_path("/");
		if (!fdt_root)
			return -ENODEV;

		match = of_match_node(ssam_platform_hub_of_match, fdt_root);
		of_node_put(fdt_root);
		if (!match)
			return -ENODEV;

		nodes = (const struct software_node **)match->data;
		if (!nodes)
			return -ENODEV;
	}

	/*
	 * As we're adding the SSAM client devices as children under this device
	 * and not the SSAM controller, we need to add a device link to the
	 * controller to ensure that we remove all of our devices before the
	 * controller is removed. This also guarantees proper ordering for
	 * suspend/resume of the devices on this hub.
	 */
	ctrl = ssam_client_bind(&pdev->dev);
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl) == -ENODEV ? -EPROBE_DEFER : PTR_ERR(ctrl);

	status = software_node_register_node_group(nodes);
	if (status)
		return status;

	root = software_node_fwnode(&ssam_node_root);
	if (!root) {
		software_node_unregister_node_group(nodes);
		return -ENOENT;
	}

	set_secondary_fwnode(&pdev->dev, root);

	status = __ssam_register_clients(&pdev->dev, ctrl, root);
	if (status) {
		set_secondary_fwnode(&pdev->dev, NULL);
		software_node_unregister_node_group(nodes);
	}

	platform_set_drvdata(pdev, nodes);
	return status;
}

static void ssam_platform_hub_remove(struct platform_device *pdev)
{
	const struct software_node **nodes = platform_get_drvdata(pdev);

	ssam_remove_clients(&pdev->dev);
	set_secondary_fwnode(&pdev->dev, NULL);
	software_node_unregister_node_group(nodes);
}

static struct platform_driver ssam_platform_hub_driver = {
	.probe = ssam_platform_hub_probe,
	.remove_new = ssam_platform_hub_remove,
	.driver = {
		.name = "surface_aggregator_platform_hub",
		.acpi_match_table = ssam_platform_hub_acpi_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_platform_driver(ssam_platform_hub_driver);

MODULE_ALIAS("platform:surface_aggregator_platform_hub");
MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("Device-registry for Surface System Aggregator Module");
MODULE_LICENSE("GPL");
