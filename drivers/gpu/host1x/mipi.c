// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 NVIDIA Corporation
 * Copyright (C) 2025 Svyatoslav Ryhel <clamor95@gmail.com>
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/tegra-mipi-cal.h>

/* only need to support one provider */
static struct {
	struct device_node *np;
	const struct tegra_mipi_ops *ops;
} provider;

/**
 * tegra_mipi_enable - Enable the Tegra MIPI calibration device.
 * @device: Handle to the Tegra MIPI calibration device.
 *
 * This calls the enable sequence for the Tegra MIPI calibration device.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int tegra_mipi_enable(struct tegra_mipi_device *device)
{
	if (device->ops->enable)
		return device->ops->enable(device);

	return 0;
}
EXPORT_SYMBOL(tegra_mipi_enable);

/**
 * tegra_mipi_disable - Disable the Tegra MIPI calibration device.
 * @device: Handle to the Tegra MIPI calibration device.
 *
 * This calls the disable sequence for the Tegra MIPI calibration device.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int tegra_mipi_disable(struct tegra_mipi_device *device)
{
	if (device->ops->disable)
		return device->ops->disable(device);

	return 0;
}
EXPORT_SYMBOL(tegra_mipi_disable);

/**
 * tegra_mipi_start_calibration - Start the Tegra MIPI calibration sequence.
 * @device: Handle to the Tegra MIPI calibration device.
 *
 * This initiates the calibration of CSI/DSI interfaces via the Tegra MIPI
 * calibration device.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int tegra_mipi_start_calibration(struct tegra_mipi_device *device)
{
	if (device->ops->start_calibration)
		return device->ops->start_calibration(device);

	return 0;
}
EXPORT_SYMBOL(tegra_mipi_start_calibration);

/**
 * tegra_mipi_finish_calibration - Finish the Tegra MIPI calibration sequence.
 * @device: Handle to the Tegra MIPI calibration device.
 *
 * This completes the calibration of CSI/DSI interfaces via the Tegra MIPI
 * calibration device.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int tegra_mipi_finish_calibration(struct tegra_mipi_device *device)
{
	if (device->ops->finish_calibration)
		return device->ops->finish_calibration(device);

	return 0;
}
EXPORT_SYMBOL(tegra_mipi_finish_calibration);

/**
 * tegra_mipi_request - Request a Tegra MIPI calibration device.
 * @device: Handle of the device requesting the MIPI calibration function.
 * @np: Device node pointer of the device requesting the MIPI calibration
 *	function.
 *
 * This function requests a reference to a Tegra MIPI calibration device.
 *
 * Returns a pointer to the Tegra MIPI calibration device on success,
 * or an ERR_PTR-encoded error code on failure.
 */
struct tegra_mipi_device *tegra_mipi_request(struct device *device,
					     struct device_node *np)
{
	struct tegra_mipi_device *mipidev;
	struct of_phandle_args args;
	int err;

	err = of_parse_phandle_with_args(np, "nvidia,mipi-calibrate",
					 "#nvidia,mipi-calibrate-cells", 0,
					 &args);
	if (err < 0)
		return ERR_PTR(err);

	if (provider.np != args.np)
		return ERR_PTR(-ENODEV);

	mipidev = kzalloc_obj(*mipidev);
	if (!mipidev) {
		err = -ENOMEM;
		goto out;
	}

	mipidev->pdev = of_find_device_by_node(args.np);
	if (!mipidev->pdev) {
		err = -ENODEV;
		goto free;
	}

	of_node_put(args.np);

	mipidev->ops = provider.ops;
	mipidev->pads = args.args[0];

	return mipidev;

free:
	kfree(mipidev);
out:
	of_node_put(args.np);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(tegra_mipi_request);

/**
 * tegra_mipi_free - Free a Tegra MIPI calibration device.
 * @mipidev: Handle to the Tegra MIPI calibration device.
 *
 * This function releases a reference to a Tegra MIPI calibration device
 * previously requested by tegra_mipi_request().
 */
void tegra_mipi_free(struct tegra_mipi_device *mipidev)
{
	platform_device_put(mipidev->pdev);
	kfree(mipidev);
}
EXPORT_SYMBOL(tegra_mipi_free);

static void tegra_mipi_remove_provider(void *data)
{
	provider.np = NULL;
	provider.ops = NULL;
}

/**
 * devm_tegra_mipi_add_provider - Managed registration of a Tegra MIPI
 *				  calibration function provider.
 * @device: Handle to the device providing the MIPI calibration function.
 * @np: Device node pointer of the device providing the MIPI calibration
 *	function.
 * @ops: Operations supported by the MIPI calibration device.
 *
 * This registers a device that provides MIPI calibration functions.
 * For Tegra20 and Tegra30, this is the CSI block, while Tegra114 and
 * newer SoC generations have a dedicated hardware block for these
 * functions.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int devm_tegra_mipi_add_provider(struct device *device, struct device_node *np,
				 const struct tegra_mipi_ops *ops)
{
	if (provider.np)
		return -EBUSY;

	provider.np = np;
	provider.ops = ops;

	return devm_add_action_or_reset(device, tegra_mipi_remove_provider, NULL);
}
EXPORT_SYMBOL(devm_tegra_mipi_add_provider);
