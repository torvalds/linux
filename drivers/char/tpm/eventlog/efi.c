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

	struct efi_tcg2_final_events_table *final_tbl = NULL;
	int final_events_log_size = efi_tpm_final_log_size;
	struct linux_efi_tpm_eventlog *log_tbl;
	struct tpm_bios_log *log;
	u32 log_size;
	u8 tpm_log_version;
	void *tmp;
	int ret;

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

	if (!log_size) {
		pr_warn("UEFI TPM log area empty\n");
		return -EIO;
	}

	log_tbl = memremap(efi.tpm_log, sizeof(*log_tbl) + log_size,
			   MEMREMAP_WB);
	if (!log_tbl) {
		pr_err("Could not map UEFI TPM log table payload!\n");
		return -ENOMEM;
	}

	/* malloc EventLog space */
	log->bios_event_log = kmemdup(log_tbl->log, log_size, GFP_KERNEL);
	if (!log->bios_event_log) {
		ret = -ENOMEM;
		goto out;
	}

	log->bios_event_log_end = log->bios_event_log + log_size;
	tpm_log_version = log_tbl->version;

	ret = tpm_log_version;

	if (efi.tpm_final_log == EFI_INVALID_TABLE_ADDR ||
	    final_events_log_size == 0 ||
	    tpm_log_version != EFI_TCG2_EVENT_LOG_FORMAT_TCG_2)
		goto out;

	final_tbl = memremap(efi.tpm_final_log,
			     sizeof(*final_tbl) + final_events_log_size,
			     MEMREMAP_WB);
	if (!final_tbl) {
		pr_err("Could not map UEFI TPM final log\n");
		kfree(log->bios_event_log);
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * The 'final events log' size excludes the 'final events preboot log'
	 * at its beginning.
	 */
	final_events_log_size -= log_tbl->final_events_preboot_size;

	/*
	 * Allocate memory for the 'combined log' where we will append the
	 * 'final events log' to.
	 */
	tmp = krealloc(log->bios_event_log,
		       log_size + final_events_log_size,
		       GFP_KERNEL);
	if (!tmp) {
		kfree(log->bios_event_log);
		ret = -ENOMEM;
		goto out;
	}

	log->bios_event_log = tmp;

	/*
	 * Append any of the 'final events log' that didn't also end up in the
	 * 'main log'. Events can be logged in both if events are generated
	 * between GetEventLog() and ExitBootServices().
	 */
	memcpy((void *)log->bios_event_log + log_size,
	       final_tbl->events + log_tbl->final_events_preboot_size,
	       final_events_log_size);
	/*
	 * The size of the 'combined log' is the size of the 'main log' plus
	 * the size of the 'final events log'.
	 */
	log->bios_event_log_end = log->bios_event_log +
		log_size + final_events_log_size;

out:
	memunmap(final_tbl);
	memunmap(log_tbl);
	return ret;
}
