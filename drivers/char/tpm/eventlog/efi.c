// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Google
 *
 * Authors:
 *      Thiebaud Weksteen <tweek@google.com>
 */

#include <linux/efi.h>
#include <linux/tpm_eventlog.h>

#include "../tpm.h"
#include "common.h"

/* read binary bios log from EFI configuration table */
int tpm_read_log_efi(struct tpm_chip *chip)
{

	struct linux_efi_tpm_eventlog *log_tbl;
	struct tpm_bios_log *log;
	u32 log_size;
	u8 tpm_log_version;

	if (!(chip->flags & TPM_CHIP_FLAG_TPM2))
		return -ENODEV;

	if (efi.tpm_log == EFI_INVALID_TABLE_ADDR)
		return -ENODEV;

	log = &chip->log;

	log_tbl = memremap(efi.tpm_log, sizeof(*log_tbl), MEMREMAP_WB);
	if (!log_tbl) {
		pr_err("Could not map UEFI TPM log table !\n");
		return -ENOMEM;
	}

	log_size = log_tbl->size;
	memunmap(log_tbl);

	log_tbl = memremap(efi.tpm_log, sizeof(*log_tbl) + log_size,
			   MEMREMAP_WB);
	if (!log_tbl) {
		pr_err("Could not map UEFI TPM log table payload!\n");
		return -ENOMEM;
	}

	/* malloc EventLog space */
	log->bios_event_log = kmemdup(log_tbl->log, log_size, GFP_KERNEL);
	if (!log->bios_event_log)
		goto err_memunmap;
	log->bios_event_log_end = log->bios_event_log + log_size;

	tpm_log_version = log_tbl->version;
	memunmap(log_tbl);
	return tpm_log_version;

err_memunmap:
	memunmap(log_tbl);
	return -ENOMEM;
}
