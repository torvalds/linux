// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * Authors:
 * Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/acpi.h>
#include <drm/drm_privacy_screen_machine.h>

#ifdef CONFIG_X86
static struct drm_privacy_screen_lookup arch_lookup;

struct arch_init_data {
	struct drm_privacy_screen_lookup lookup;
	bool (*detect)(void);
};

#if IS_ENABLED(CONFIG_THINKPAD_ACPI)
static acpi_status __init acpi_set_handle(acpi_handle handle, u32 level,
					  void *context, void **return_value)
{
	*(acpi_handle *)return_value = handle;
	return AE_CTRL_TERMINATE;
}

static bool __init detect_thinkpad_privacy_screen(void)
{
	union acpi_object obj = { .type = ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { .count = 1, .pointer = &obj, };
	acpi_handle ec_handle = NULL;
	unsigned long long output;
	acpi_status status;

	if (acpi_disabled)
		return false;

	/* Get embedded-controller handle */
	status = acpi_get_devices("PNP0C09", acpi_set_handle, NULL, &ec_handle);
	if (ACPI_FAILURE(status) || !ec_handle)
		return false;

	/* And call the privacy-screen get-status method */
	status = acpi_evaluate_integer(ec_handle, "HKEY.GSSS", &args, &output);
	if (ACPI_FAILURE(status))
		return false;

	return (output & 0x10000) ? true : false;
}
#endif

static const struct arch_init_data arch_init_data[] __initconst = {
#if IS_ENABLED(CONFIG_THINKPAD_ACPI)
	{
		.lookup = {
			.dev_id = NULL,
			.con_id = NULL,
			.provider = "privacy_screen-thinkpad_acpi",
		},
		.detect = detect_thinkpad_privacy_screen,
	},
#endif
};

void __init drm_privacy_screen_lookup_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(arch_init_data); i++) {
		if (!arch_init_data[i].detect())
			continue;

		pr_info("Found '%s' privacy-screen provider\n",
			arch_init_data[i].lookup.provider);

		/* Make a copy because arch_init_data is __initconst */
		arch_lookup = arch_init_data[i].lookup;
		drm_privacy_screen_lookup_add(&arch_lookup);
		break;
	}
}

void drm_privacy_screen_lookup_exit(void)
{
	if (arch_lookup.provider)
		drm_privacy_screen_lookup_remove(&arch_lookup);
}
#endif /* ifdef CONFIG_X86 */
