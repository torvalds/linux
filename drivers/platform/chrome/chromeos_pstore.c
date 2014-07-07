/*
 *  chromeos_pstore.c - Driver to instantiate Chromebook ramoops device
 *
 *  Copyright (C) 2013 Google, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */

#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pstore_ram.h>

static struct dmi_system_id chromeos_pstore_dmi_table[] __initdata = {
	{
		/*
		 * Today all Chromebooks/boxes ship with GOOGLE as vendor and
		 * coreboot as bios vendor. No other systems with this
		 * combination are known to date.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_BIOS_VENDOR, "coreboot"),
		},
	},
	{
		/*
		 * The first Samsung Chromebox and Chromebook Series 5 550 use
		 * coreboot but with Samsung as the system vendor.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG"),
			DMI_MATCH(DMI_BIOS_VENDOR, "coreboot"),
		},
	},
	{
		/* x86-alex, the first Samsung Chromebook. */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alex"),
		},
	},
	{
		/* x86-mario, the Cr-48 pilot device from Google. */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IEC"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Mario"),
		},
	},
	{
		/* x86-zgb, the first Acer Chromebook. */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ACER"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ZGB"),
		},
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, chromeos_pstore_dmi_table);

/*
 * On x86 chromebooks/boxes, the firmware will keep the legacy VGA memory
 * range untouched across reboots, so we use that to store our pstore
 * contents for panic logs, etc.
 */
static struct ramoops_platform_data chromeos_ramoops_data = {
	.mem_size	= 0x100000,
	.mem_address	= 0xf00000,
	.record_size	= 0x20000,
	.console_size	= 0x20000,
	.ftrace_size	= 0x20000,
	.dump_oops	= 1,
};

static struct platform_device chromeos_ramoops = {
	.name = "ramoops",
	.dev = {
		.platform_data = &chromeos_ramoops_data,
	},
};

static int __init chromeos_pstore_init(void)
{
	if (dmi_check_system(chromeos_pstore_dmi_table))
		return platform_device_register(&chromeos_ramoops);

	return -ENODEV;
}

static void __exit chromeos_pstore_exit(void)
{
	platform_device_unregister(&chromeos_ramoops);
}

module_init(chromeos_pstore_init);
module_exit(chromeos_pstore_exit);

MODULE_DESCRIPTION("Chrome OS pstore module");
MODULE_LICENSE("GPL");
