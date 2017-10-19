/* Core kernel secure boot support.
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/efi.h>
#include <linux/kernel.h>
#include <linux/printk.h>

/*
 * Decide what to do when UEFI secure boot mode is enabled.
 */
void __init efi_set_secure_boot(enum efi_secureboot_mode mode)
{
	if (efi_enabled(EFI_BOOT)) {
		switch (mode) {
		case efi_secureboot_mode_disabled:
			pr_info("Secure boot disabled\n");
			break;
		case efi_secureboot_mode_enabled:
			set_bit(EFI_SECURE_BOOT, &efi.flags);
			pr_info("Secure boot enabled\n");
			break;
		default:
			pr_warning("Secure boot could not be determined (mode %u)\n",
				   mode);
			break;
		}
	}
}
