// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Google, Inc.
 *     Thiebaud Weksteen <tweek@google.com>
 */

#define TPM_MEMREMAP(start, size) early_memremap(start, size)
#define TPM_MEMUNMAP(start, size) early_memunmap(start, size)

#include <asm/early_ioremap.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/tpm_eventlog.h>

int efi_tpm_final_log_size;
EXPORT_SYMBOL(efi_tpm_final_log_size);

static int tpm2_calc_event_log_size(void *data, int count, void *size_info)
{
	struct tcg_pcr_event2_head *header;
	int event_size, size = 0;

	while (count > 0) {
		header = data + size;
		event_size = __calc_tpm2_event_size(header, size_info, true);
		if (event_size == 0)
			return -1;
		size += event_size;
		count--;
	}

	return size;
}

/*
 * Reserve the memory associated with the TPM Event Log configuration table.
 */
int __init efi_tpm_eventlog_init(void)
{
	struct linux_efi_tpm_eventlog *log_tbl;
	struct efi_tcg2_final_events_table *final_tbl;
	int tbl_size;
	int ret = 0;

	if (efi.tpm_log == EFI_INVALID_TABLE_ADDR) {
		/*
		 * We can't calculate the size of the final events without the
		 * first entry in the TPM log, so bail here.
		 */
		return 0;
	}

	log_tbl = early_memremap(efi.tpm_log, sizeof(*log_tbl));
	if (!log_tbl) {
		pr_err("Failed to map TPM Event Log table @ 0x%lx\n",
		       efi.tpm_log);
		efi.tpm_log = EFI_INVALID_TABLE_ADDR;
		return -ENOMEM;
	}

	tbl_size = sizeof(*log_tbl) + log_tbl->size;
	memblock_reserve(efi.tpm_log, tbl_size);

	if (efi.tpm_final_log == EFI_INVALID_TABLE_ADDR)
		goto out;

	final_tbl = early_memremap(efi.tpm_final_log, sizeof(*final_tbl));

	if (!final_tbl) {
		pr_err("Failed to map TPM Final Event Log table @ 0x%lx\n",
		       efi.tpm_final_log);
		efi.tpm_final_log = EFI_INVALID_TABLE_ADDR;
		ret = -ENOMEM;
		goto out;
	}

	tbl_size = 0;
	if (final_tbl->nr_events != 0) {
		void *events = (void *)efi.tpm_final_log
				+ sizeof(final_tbl->version)
				+ sizeof(final_tbl->nr_events);

		tbl_size = tpm2_calc_event_log_size(events,
						    final_tbl->nr_events,
						    log_tbl->log);
	}

	if (tbl_size < 0) {
		pr_err(FW_BUG "Failed to parse event in TPM Final Events Log\n");
		ret = -EINVAL;
		goto out_calc;
	}

	memblock_reserve((unsigned long)final_tbl,
			 tbl_size + sizeof(*final_tbl));
	efi_tpm_final_log_size = tbl_size;

out_calc:
	early_memunmap(final_tbl, sizeof(*final_tbl));
out:
	early_memunmap(log_tbl, sizeof(*log_tbl));
	return ret;
}

