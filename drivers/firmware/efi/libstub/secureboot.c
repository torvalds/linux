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

/* SHIM variables */
static const efi_guid_t shim_guid = EFI_SHIM_LOCK_GUID;
static const efi_char16_t shim_MokSBState_name[] = L"MokSBState";

static efi_status_t get_var(efi_char16_t *name, efi_guid_t *vendor, u32 *attr,
			    unsigned long *data_size, void *data)
{
	return get_efi_var(name, vendor, attr, data_size, data);
}

/*
 * Determine whether we're in secure boot mode.
 */
enum efi_secureboot_mode efi_get_secureboot(void)
{
	u32 attr;
	unsigned long size;
	enum efi_secureboot_mode mode;
	efi_status_t status;
	u8 moksbstate;

	mode = efi_get_secureboot_mode(get_var);
	if (mode == efi_secureboot_mode_unknown) {
		efi_err("Could not determine UEFI Secure Boot status.\n");
		return efi_secureboot_mode_unknown;
	}
	if (mode != efi_secureboot_mode_enabled)
		return mode;

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
	efi_info("UEFI Secure Boot is enabled.\n");
	return efi_secureboot_mode_enabled;
}
