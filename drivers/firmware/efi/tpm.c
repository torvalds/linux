/*
 * Copyright (C) 2017 Google, Inc.
 *     Thiebaud Weksteen <tweek@google.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/efi.h>
#include <linux/init.h>
#include <linux/memblock.h>

#include <asm/early_ioremap.h>

/*
 * Reserve the memory associated with the TPM Event Log configuration table.
 */
int __init efi_tpm_eventlog_init(void)
{
	struct linux_efi_tpm_eventlog *log_tbl;
	unsigned int tbl_size;

	if (efi.tpm_log == EFI_INVALID_TABLE_ADDR)
		return 0;

	log_tbl = early_memremap(efi.tpm_log, sizeof(*log_tbl));
	if (!log_tbl) {
		pr_err("Failed to map TPM Event Log table @ 0x%lx\n",
			efi.tpm_log);
		efi.tpm_log = EFI_INVALID_TABLE_ADDR;
		return -ENOMEM;
	}

	tbl_size = sizeof(*log_tbl) + log_tbl->size;
	memblock_reserve(efi.tpm_log, tbl_size);
	early_memunmap(log_tbl, sizeof(*log_tbl));
	return 0;
}

