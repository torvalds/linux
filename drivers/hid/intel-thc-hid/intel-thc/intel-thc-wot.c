// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Intel Corporation */

#include <linux/acpi.h>
#include <linux/pm_wakeirq.h>

#include "intel-thc-dev.h"
#include "intel-thc-wot.h"

/**
 * thc_wot_config - Query and configure wake-on-touch feature
 * @thc_dev: Point to thc_device structure
 * @gpio_map: Point to ACPI GPIO resource mapping structure
 *
 * THC ACPI device only provides _CRS with GpioInt() resources, doesn't contain
 * _DSD to map this GPIO resource, so this function first registers wake GPIO
 * mapping manually, then queries wake-on-touch GPIO resource from ACPI,
 * if it exists and is wake-able, configure driver to enable it, otherwise,
 * return immediately.
 * This function will not return error as it doesn't impact major function.
 */
void thc_wot_config(struct thc_device *thc_dev, const struct acpi_gpio_mapping *gpio_map)
{
	struct acpi_device *adev;
	struct thc_wot *wot;
	int ret;

	if (!thc_dev)
		return;

	adev = ACPI_COMPANION(thc_dev->dev);
	if (!adev)
		return;

	wot = &thc_dev->wot;

	ret = acpi_dev_add_driver_gpios(adev, gpio_map);
	if (ret) {
		dev_warn(thc_dev->dev, "Can't add wake GPIO resource, ret = %d\n", ret);
		return;
	}

	wot->gpio_irq = acpi_dev_gpio_irq_wake_get_by(adev, "wake-on-touch", 0,
						      &wot->gpio_irq_wakeable);
	if (wot->gpio_irq <= 0) {
		dev_warn(thc_dev->dev, "Can't find wake GPIO resource\n");
		return;
	}

	if (!wot->gpio_irq_wakeable) {
		dev_warn(thc_dev->dev, "GPIO resource isn't wakeable\n");
		return;
	}

	ret = device_init_wakeup(thc_dev->dev, true);
	if (ret) {
		dev_warn(thc_dev->dev, "Failed to init wake up.\n");
		return;
	}

	ret = dev_pm_set_dedicated_wake_irq(thc_dev->dev, wot->gpio_irq);
	if (ret) {
		dev_warn(thc_dev->dev, "Failed to set wake up IRQ.\n");
		device_init_wakeup(thc_dev->dev, false);
	}
}
EXPORT_SYMBOL_NS_GPL(thc_wot_config, "INTEL_THC");

/**
 * thc_wot_unconfig - Unconfig wake-on-touch feature
 * @thc_dev: Point to thc_device structure
 *
 * Configure driver to disable wake-on-touch and release ACPI resource.
 */
void thc_wot_unconfig(struct thc_device *thc_dev)
{
	struct acpi_device *adev;

	if (!thc_dev)
		return;

	adev = ACPI_COMPANION(thc_dev->dev);
	if (!adev)
		return;

	if (thc_dev->wot.gpio_irq_wakeable)
		device_init_wakeup(thc_dev->dev, false);

	if (thc_dev->wot.gpio_irq > 0) {
		dev_pm_clear_wake_irq(thc_dev->dev);
		acpi_dev_remove_driver_gpios(adev);
	}
}
EXPORT_SYMBOL_NS_GPL(thc_wot_unconfig, "INTEL_THC");
