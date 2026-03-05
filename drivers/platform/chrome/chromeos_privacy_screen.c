// SPDX-License-Identifier: GPL-2.0

/*
 *  ChromeOS Privacy Screen support
 *
 * Copyright (C) 2022 Google LLC
 *
 * This is the Chromeos privacy screen provider, present on certain chromebooks,
 * represented by a GOOG0010 device in the ACPI. This ACPI device, if present,
 * will cause the i915 drm driver to probe defer until this driver registers
 * the privacy-screen.
 */

#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <drm/drm_privacy_screen_driver.h>

/*
 * The DSM (Device Specific Method) constants below are the agreed API with
 * the firmware team, on how to control privacy screen using ACPI methods.
 */
#define PRIV_SCRN_DSM_REVID		1	/* DSM version */
#define PRIV_SCRN_DSM_FN_GET_STATUS	1	/* Get privacy screen status */
#define PRIV_SCRN_DSM_FN_ENABLE		2	/* Enable privacy screen */
#define PRIV_SCRN_DSM_FN_DISABLE	3	/* Disable privacy screen */

static const guid_t chromeos_privacy_screen_dsm_guid =
		    GUID_INIT(0xc7033113, 0x8720, 0x4ceb,
			      0x90, 0x90, 0x9d, 0x52, 0xb3, 0xe5, 0x2d, 0x73);

static void
chromeos_privacy_screen_get_hw_state(struct drm_privacy_screen
				     *drm_privacy_screen)
{
	union acpi_object *obj;
	struct device *privacy_screen =
		drm_privacy_screen_get_drvdata(drm_privacy_screen);
	acpi_handle handle = ACPI_HANDLE(privacy_screen);

	obj = acpi_evaluate_dsm(handle, &chromeos_privacy_screen_dsm_guid,
				PRIV_SCRN_DSM_REVID,
				PRIV_SCRN_DSM_FN_GET_STATUS, NULL);
	if (!obj) {
		dev_err(privacy_screen,
			"_DSM failed to get privacy-screen state\n");
		return;
	}

	if (obj->type != ACPI_TYPE_INTEGER)
		dev_err(privacy_screen,
			"Bad _DSM to get privacy-screen state\n");
	else if (obj->integer.value == 1)
		drm_privacy_screen->hw_state = drm_privacy_screen->sw_state =
			PRIVACY_SCREEN_ENABLED;
	else
		drm_privacy_screen->hw_state = drm_privacy_screen->sw_state =
			PRIVACY_SCREEN_DISABLED;

	ACPI_FREE(obj);
}

static int
chromeos_privacy_screen_set_sw_state(struct drm_privacy_screen
				     *drm_privacy_screen,
				     enum drm_privacy_screen_status state)
{
	union acpi_object *obj = NULL;
	struct device *privacy_screen =
		drm_privacy_screen_get_drvdata(drm_privacy_screen);
	acpi_handle handle = ACPI_HANDLE(privacy_screen);

	if (state == PRIVACY_SCREEN_DISABLED) {
		obj = acpi_evaluate_dsm(handle,
					&chromeos_privacy_screen_dsm_guid,
					PRIV_SCRN_DSM_REVID,
					PRIV_SCRN_DSM_FN_DISABLE, NULL);
	} else if (state == PRIVACY_SCREEN_ENABLED) {
		obj = acpi_evaluate_dsm(handle,
					&chromeos_privacy_screen_dsm_guid,
					PRIV_SCRN_DSM_REVID,
					PRIV_SCRN_DSM_FN_ENABLE, NULL);
	} else {
		dev_err(privacy_screen,
			"Bad attempt to set privacy-screen status to %u\n",
			state);
		return -EINVAL;
	}

	if (!obj) {
		dev_err(privacy_screen,
			"_DSM failed to set privacy-screen state\n");
		return -EIO;
	}

	drm_privacy_screen->hw_state = drm_privacy_screen->sw_state = state;
	ACPI_FREE(obj);
	return 0;
}

static const struct drm_privacy_screen_ops chromeos_privacy_screen_ops = {
	.get_hw_state = chromeos_privacy_screen_get_hw_state,
	.set_sw_state = chromeos_privacy_screen_set_sw_state,
};

static int chromeos_privacy_screen_probe(struct platform_device *pdev)
{
	struct drm_privacy_screen *drm_privacy_screen =
		drm_privacy_screen_register(&pdev->dev,
					    &chromeos_privacy_screen_ops,
					    &pdev->dev);

	if (IS_ERR(drm_privacy_screen)) {
		dev_err(&pdev->dev, "Error registering privacy-screen\n");
		return PTR_ERR(drm_privacy_screen);
	}

	platform_set_drvdata(pdev, drm_privacy_screen);
	dev_info(&pdev->dev, "registered privacy-screen '%s'\n",
		 dev_name(&drm_privacy_screen->dev));

	return 0;
}

static void chromeos_privacy_screen_remove(struct platform_device *pdev)
{
	drm_privacy_screen_unregister(platform_get_drvdata(pdev));
}

static const struct acpi_device_id chromeos_privacy_screen_device_ids[] = {
	{"GOOG0010", 0}, /* Google's electronic privacy screen for eDP-1 */
	{}
};
MODULE_DEVICE_TABLE(acpi, chromeos_privacy_screen_device_ids);

static struct platform_driver chromeos_privacy_screen_driver = {
	.probe = chromeos_privacy_screen_probe,
	.remove = chromeos_privacy_screen_remove,
	.driver = {
		.name = "chromeos_privacy_screen_driver",
		.acpi_match_table = chromeos_privacy_screen_device_ids,
	},
};

module_platform_driver(chromeos_privacy_screen_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ChromeOS ACPI Privacy Screen driver");
MODULE_AUTHOR("Rajat Jain <rajatja@google.com>");
