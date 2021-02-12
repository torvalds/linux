// SPDX-License-Identifier: GPL-2.0+
/*
 * Surface System Aggregator Module (SSAM) client device registry.
 *
 * Registry for non-platform/non-ACPI SSAM client devices, i.e. devices that
 * cannot be auto-detected. Provides device-hubs and performs instantiation
 * for these devices.
 *
 * Copyright (C) 2020-2021 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#include <linux/surface_aggregator/controller.h>
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

/* Devices for Surface Book 2. */
static const struct software_node *ssam_node_group_sb2[] = {
	&ssam_node_root,
	NULL,
};

/* Devices for Surface Book 3. */
static const struct software_node *ssam_node_group_sb3[] = {
	&ssam_node_root,
	NULL,
};

/* Devices for Surface Laptop 1. */
static const struct software_node *ssam_node_group_sl1[] = {
	&ssam_node_root,
	NULL,
};

/* Devices for Surface Laptop 2. */
static const struct software_node *ssam_node_group_sl2[] = {
	&ssam_node_root,
	NULL,
};

/* Devices for Surface Laptop 3. */
static const struct software_node *ssam_node_group_sl3[] = {
	&ssam_node_root,
	NULL,
};

/* Devices for Surface Laptop Go. */
static const struct software_node *ssam_node_group_slg1[] = {
	&ssam_node_root,
	NULL,
};

/* Devices for Surface Pro 5. */
static const struct software_node *ssam_node_group_sp5[] = {
	&ssam_node_root,
	NULL,
};

/* Devices for Surface Pro 6. */
static const struct software_node *ssam_node_group_sp6[] = {
	&ssam_node_root,
	NULL,
};

/* Devices for Surface Pro 7. */
static const struct software_node *ssam_node_group_sp7[] = {
	&ssam_node_root,
	NULL,
};


/* -- Device registry helper functions. ------------------------------------- */

static int ssam_uid_from_string(const char *str, struct ssam_device_uid *uid)
{
	u8 d, tc, tid, iid, fn;
	int n;

	n = sscanf(str, "ssam:%hhx:%hhx:%hhx:%hhx:%hhx", &d, &tc, &tid, &iid, &fn);
	if (n != 5)
		return -EINVAL;

	uid->domain = d;
	uid->category = tc;
	uid->target = tid;
	uid->instance = iid;
	uid->function = fn;

	return 0;
}

static int ssam_hub_remove_devices_fn(struct device *dev, void *data)
{
	if (!is_ssam_device(dev))
		return 0;

	ssam_device_remove(to_ssam_device(dev));
	return 0;
}

static void ssam_hub_remove_devices(struct device *parent)
{
	device_for_each_child_reverse(parent, NULL, ssam_hub_remove_devices_fn);
}

static int ssam_hub_add_device(struct device *parent, struct ssam_controller *ctrl,
			       struct fwnode_handle *node)
{
	struct ssam_device_uid uid;
	struct ssam_device *sdev;
	int status;

	status = ssam_uid_from_string(fwnode_get_name(node), &uid);
	if (status)
		return status;

	sdev = ssam_device_alloc(ctrl, uid);
	if (!sdev)
		return -ENOMEM;

	sdev->dev.parent = parent;
	sdev->dev.fwnode = node;

	status = ssam_device_add(sdev);
	if (status)
		ssam_device_put(sdev);

	return status;
}

static int ssam_hub_add_devices(struct device *parent, struct ssam_controller *ctrl,
				struct fwnode_handle *node)
{
	struct fwnode_handle *child;
	int status;

	fwnode_for_each_child_node(node, child) {
		/*
		 * Try to add the device specified in the firmware node. If
		 * this fails with -EINVAL, the node does not specify any SSAM
		 * device, so ignore it and continue with the next one.
		 */

		status = ssam_hub_add_device(parent, ctrl, child);
		if (status && status != -EINVAL)
			goto err;
	}

	return 0;
err:
	ssam_hub_remove_devices(parent);
	return status;
}


/* -- SSAM platform/meta-hub driver. ---------------------------------------- */

static const struct acpi_device_id ssam_platform_hub_match[] = {
	/* Surface Pro 4, 5, and 6 (OMBR < 0x10) */
	{ "MSHW0081", (unsigned long)ssam_node_group_sp5 },

	/* Surface Pro 6 (OMBR >= 0x10) */
	{ "MSHW0111", (unsigned long)ssam_node_group_sp6 },

	/* Surface Pro 7 */
	{ "MSHW0116", (unsigned long)ssam_node_group_sp7 },

	/* Surface Book 2 */
	{ "MSHW0107", (unsigned long)ssam_node_group_sb2 },

	/* Surface Book 3 */
	{ "MSHW0117", (unsigned long)ssam_node_group_sb3 },

	/* Surface Laptop 1 */
	{ "MSHW0086", (unsigned long)ssam_node_group_sl1 },

	/* Surface Laptop 2 */
	{ "MSHW0112", (unsigned long)ssam_node_group_sl2 },

	/* Surface Laptop 3 (13", Intel) */
	{ "MSHW0114", (unsigned long)ssam_node_group_sl3 },

	/* Surface Laptop 3 (15", AMD) */
	{ "MSHW0110", (unsigned long)ssam_node_group_sl3 },

	/* Surface Laptop Go 1 */
	{ "MSHW0118", (unsigned long)ssam_node_group_slg1 },

	{ },
};
MODULE_DEVICE_TABLE(acpi, ssam_platform_hub_match);

static int ssam_platform_hub_probe(struct platform_device *pdev)
{
	const struct software_node **nodes;
	struct ssam_controller *ctrl;
	struct fwnode_handle *root;
	int status;

	nodes = (const struct software_node **)acpi_device_get_match_data(&pdev->dev);
	if (!nodes)
		return -ENODEV;

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

	status = ssam_hub_add_devices(&pdev->dev, ctrl, root);
	if (status) {
		set_secondary_fwnode(&pdev->dev, NULL);
		software_node_unregister_node_group(nodes);
	}

	platform_set_drvdata(pdev, nodes);
	return status;
}

static int ssam_platform_hub_remove(struct platform_device *pdev)
{
	const struct software_node **nodes = platform_get_drvdata(pdev);

	ssam_hub_remove_devices(&pdev->dev);
	set_secondary_fwnode(&pdev->dev, NULL);
	software_node_unregister_node_group(nodes);
	return 0;
}

static struct platform_driver ssam_platform_hub_driver = {
	.probe = ssam_platform_hub_probe,
	.remove = ssam_platform_hub_remove,
	.driver = {
		.name = "surface_aggregator_platform_hub",
		.acpi_match_table = ssam_platform_hub_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_platform_driver(ssam_platform_hub_driver);

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("Device-registry for Surface System Aggregator Module");
MODULE_LICENSE("GPL");
