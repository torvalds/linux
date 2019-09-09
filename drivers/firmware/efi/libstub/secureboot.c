// SPDX-License-Identifier: GPL-2.0
/*
 * Secure boot handling.
 *
 * Copyright (C) 2013,2014 Linaro Limited
 *     Roy Franz <roy.franz@linaro.org
 * Copyright (C) 2013 Red Hat, Inc.
 *     Mark Salter <msalter@redhat.com>
 */
#include <linux/efi.h>
#include <asm/efi.h>

#include "efistub.h"

/* BIOS variables */
static const efi_guid_t efi_variable_guid = EFI_GLOBAL_VARIABLE_GUID;
static const efi_char16_t efi_SecureBoot_name[] = L"SecureBoot";
static const efi_char16_t efi_SetupMode_name[] = L"SetupMode";

/* SHIM variables */
static const efi_guid_t shim_guid = EFI_SHIM_LOCK_GUID;
static const efi_char16_t shim_MokSBState_name[] = L"MokSBState";

#define get_efi_var(name, vendor, ...) \
	efi_call_runtime(get_variable, \
			 (efi_char16_t *)(name), (efi_guid_t *)(vendor), \
			 __VA_ARGS__);

/*
 * Determine whether we're in secure boot mode.
 *
 * Please keep the logic in sync with
 * arch/x86/xen/efi.c:xen_efi_get_secureboot().
 */
enum efi_secureboot_mode efi_get_secureboot(efi_system_table_t *sys_table_arg)
{
	u32 attr;
	u8 secboot, setupmode, moksbstate;
	unsigned long size;
	efi_status_t status;

	size = sizeof(secboot);
	status = get_efi_var(efi_SecureBoot_name, &efi_variable_guid,
			     NULL, &size, &secboot);
	if (status == EFI_NOT_FOUND)
		return efi_secureboot_mode_disabled;
	if (status != EFI_SUCCESS)
		goto out_efi_err;

	size = sizeof(setupmode);
	status = get_efi_var(efi_SetupMode_name, &efi_variable_guid,
			     NULL, &size, &setupmode);
	if (status != EFI_SUCCESS)
		goto out_efi_err;

	if (secboot == 0 || setupmode == 1)
		return efi_secureboot_mode_disabled;

	/*
	 * See if a user has put the shim into insecure mode. If so, and if the
	 * variable doesn't have the runtime attribute set, we might as well
	 * honor that.
	 */
	size = sizeof(moksbstate);
	status = get_efi_var(shim_MokSBState_name, &shim_guid,
			     &attr, &size, &moksbstate);

	/* If it fails, we don't care why. Default to secure */
	if (status != EFI_SUCCESS)
		goto secure_boot_enabled;
	if (!(attr & EFI_VARIABLE_RUNTIME_ACCESS) && moksbstate == 1)
		return efi_secureboot_mode_disabled;

secure_boot_enabled:
	pr_efi(sys_table_arg, "UEFI Secure Boot is enabled.\n");
	return efi_secureboot_mode_enabled;

out_efi_err:
	pr_efi_err(sys_table_arg, "Could not determine UEFI Secure Boot status.\n");
	return efi_secureboot_mode_unknown;
}
