// SPDX-License-Identifier: GPL-2.0
/*
 * Helpers for early access to EFI configuration table.
 *
 * Originally derived from arch/x86/boot/compressed/acpi.c
 */

#include "misc.h"
#include <linux/efi.h>
#include <asm/efi.h>

/**
 * efi_get_type - Given a pointer to boot_params, determine the type of EFI environment.
 *
 * @bp:         pointer to boot_params
 *
 * Return: EFI_TYPE_{32,64} for valid EFI environments, EFI_TYPE_NONE otherwise.
 */
enum efi_type efi_get_type(struct boot_params *bp)
{
	struct efi_info *ei;
	enum efi_type et;
	const char *sig;

	ei = &bp->efi_info;
	sig = (char *)&ei->efi_loader_signature;

	if (!strncmp(sig, EFI64_LOADER_SIGNATURE, 4)) {
		et = EFI_TYPE_64;
	} else if (!strncmp(sig, EFI32_LOADER_SIGNATURE, 4)) {
		et = EFI_TYPE_32;
	} else {
		debug_putstr("No EFI environment detected.\n");
		et = EFI_TYPE_NONE;
	}

#ifndef CONFIG_X86_64
	/*
	 * Existing callers like acpi.c treat this case as an indicator to
	 * fall-through to non-EFI, rather than an error, so maintain that
	 * functionality here as well.
	 */
	if (ei->efi_systab_hi || ei->efi_memmap_hi) {
		debug_putstr("EFI system table is located above 4GB and cannot be accessed.\n");
		et = EFI_TYPE_NONE;
	}
#endif

	return et;
}
