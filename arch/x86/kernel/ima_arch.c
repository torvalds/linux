/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 IBM Corporation
 */
#include <linux/efi.h>
#include <linux/ima.h>

extern struct boot_params boot_params;

bool arch_ima_get_secureboot(void)
{
	if (efi_enabled(EFI_BOOT) &&
		(boot_params.secure_boot == efi_secureboot_mode_enabled))
		return true;
	else
		return false;
}
