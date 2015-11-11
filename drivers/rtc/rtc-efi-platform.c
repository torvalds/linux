/*
 * Moved from arch/ia64/kernel/time.c
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 *	David Mosberger <davidm@hpl.hp.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 * Copyright (C) 1999-2000 VA Linux Systems
 * Copyright (C) 1999-2000 Walt Drummond <drummond@valinux.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/platform_device.h>

static struct platform_device rtc_efi_dev = {
	.name = "rtc-efi",
	.id = -1,
};

static int __init rtc_init(void)
{
	if (efi_enabled(EFI_RUNTIME_SERVICES))
		if (platform_device_register(&rtc_efi_dev) < 0)
			pr_err("unable to register rtc device...\n");

	/* not necessarily an error */
	return 0;
}
module_init(rtc_init);
