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

/**
 * efi_get_system_table - Given a pointer to boot_params, retrieve the physical address
 *                        of the EFI system table.
 *
 * @bp:         pointer to boot_params
 *
 * Return: EFI system table address on success. On error, return 0.
 */
unsigned long efi_get_system_table(struct boot_params *bp)
{
	unsigned long sys_tbl_pa;
	struct efi_info *ei;
	enum efi_type et;

	/* Get systab from boot params. */
	ei = &bp->efi_info;
#ifdef CONFIG_X86_64
	sys_tbl_pa = ei->efi_systab | ((__u64)ei->efi_systab_hi << 32);
#else
	sys_tbl_pa = ei->efi_systab;
#endif
	if (!sys_tbl_pa) {
		debug_putstr("EFI system table not found.");
		return 0;
	}

	return sys_tbl_pa;
}

/**
 * efi_get_conf_table - Given a pointer to boot_params, locate and return the physical
 *                      address of EFI configuration table.
 *
 * @bp:                 pointer to boot_params
 * @cfg_tbl_pa:         location to store physical address of config table
 * @cfg_tbl_len:        location to store number of config table entries
 *
 * Return: 0 on success. On error, return params are left unchanged.
 */
int efi_get_conf_table(struct boot_params *bp, unsigned long *cfg_tbl_pa,
		       unsigned int *cfg_tbl_len)
{
	unsigned long sys_tbl_pa;
	enum efi_type et;
	int ret;

	if (!cfg_tbl_pa || !cfg_tbl_len)
		return -EINVAL;

	sys_tbl_pa = efi_get_system_table(bp);
	if (!sys_tbl_pa)
		return -EINVAL;

	/* Handle EFI bitness properly */
	et = efi_get_type(bp);
	if (et == EFI_TYPE_64) {
		efi_system_table_64_t *stbl = (efi_system_table_64_t *)sys_tbl_pa;

		*cfg_tbl_pa = stbl->tables;
		*cfg_tbl_len = stbl->nr_tables;
	} else if (et == EFI_TYPE_32) {
		efi_system_table_32_t *stbl = (efi_system_table_32_t *)sys_tbl_pa;

		*cfg_tbl_pa = stbl->tables;
		*cfg_tbl_len = stbl->nr_tables;
	} else {
		return -EINVAL;
	}

	return 0;
}
