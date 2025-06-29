// SPDX-License-Identifier: GPL-2.0
/* Author: Hans de Goede <hansg@kernel.org> */

#include <linux/dmi.h>
#include <linux/platform_data/x86/int3472.h>

static const struct int3472_discrete_quirks lenovo_miix_510_quirks = {
	.avdd_second_sensor = "i2c-OVTI2680:00",
};

const struct dmi_system_id skl_int3472_discrete_quirks[] = {
	{
		/* Lenovo Miix 510-12IKB */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "MIIX 510-12IKB"),
		},
		.driver_data = (void *)&lenovo_miix_510_quirks,
	},
	{ }
};
