// SPDX-License-Identifier: GPL-2.0
/*
 * TPM handling.
 *
 * Copyright (C) 2016 CoreOS, Inc
 * Copyright (C) 2017 Google, Inc.
 *     Matthew Garrett <mjg59@google.com>
 *     Thiebaud Weksteen <tweek@google.com>
 */
#include <linux/efi.h>
#include <linux/tpm_eventlog.h>
#include <asm/efi.h>

#include "efistub.h"

#ifdef CONFIG_RESET_ATTACK_MITIGATION
static const efi_char16_t efi_MemoryOverWriteRequest_name[] =
	L"MemoryOverwriteRequestControl";

#define MEMORY_ONLY_RESET_CONTROL_GUID \
	EFI_GUID(0xe20939be, 0x32d4, 0x41be, 0xa1, 0x50, 0x89, 0x7f, 0x85, 0xd4, 0x98, 0x29)

/*
 * Enable reboot attack mitigation. This requests that the firmware clear the
 * RAM on next reboot before proceeding with boot, ensuring that any secrets
 * are cleared. If userland has ensured that all secrets have been removed
 * from RAM before reboot it can simply reset this variable.
 */
void efi_enable_reset_attack_mitigation(void)
{
	u8 val = 1;
	efi_guid_t var_guid = MEMORY_ONLY_RESET_CONTROL_GUID;
	efi_status_t status;
	unsigned long datasize = 0;

	status = get_efi_var(efi_MemoryOverWriteRequest_name, &var_guid,
			     NULL, &datasize, NULL);

	if (status == EFI_NOT_FOUND)
		return;

	set_efi_var(efi_MemoryOverWriteRequest_name, &var_guid,
		    EFI_VARIABLE_NON_VOLATILE |
		    EFI_VARIABLE_BOOTSERVICE_ACCESS |
		    EFI_VARIABLE_RUNTIME_ACCESS, sizeof(val), &val);
}

#endif

void efi_retrieve_tpm2_eventlog(void)
{
	efi_guid_t tcg2_guid = EFI_TCG2_PROTOCOL_GUID;
	efi_guid_t linux_eventlog_guid = LINUX_EFI_TPM_EVENT_LOG_GUID;
	efi_status_t status;
	efi_physical_addr_t log_location = 0, log_last_entry = 0;
	struct linux_efi_tpm_eventlog *log_tbl = NULL;
	struct efi_tcg2_final_events_table *final_events_table = NULL;
	unsigned long first_entry_addr, last_entry_addr;
	size_t log_size, last_entry_size;
	efi_bool_t truncated;
	int version = EFI_TCG2_EVENT_LOG_FORMAT_TCG_2;
	efi_tcg2_protocol_t *tcg2_protocol = NULL;
	int final_events_size = 0;

	status = efi_bs_call(locate_protocol, &tcg2_guid, NULL,
			     (void **)&tcg2_protocol);
	if (status != EFI_SUCCESS)
		return;

	status = efi_call_proto(tcg2_protocol, get_event_log, version,
				&log_location, &log_last_entry, &truncated);

	if (status != EFI_SUCCESS || !log_location) {
		version = EFI_TCG2_EVENT_LOG_FORMAT_TCG_1_2;
		status = efi_call_proto(tcg2_protocol, get_event_log, version,
					&log_location, &log_last_entry,
					&truncated);
		if (status != EFI_SUCCESS || !log_location)
			return;

	}

	first_entry_addr = (unsigned long) log_location;

	/*
	 * We populate the EFI table even if the logs are empty.
	 */
	if (!log_last_entry) {
		log_size = 0;
	} else {
		last_entry_addr = (unsigned long) log_last_entry;
		/*
		 * get_event_log only returns the address of the last entry.
		 * We need to calculate its size to deduce the full size of
		 * the logs.
		 */
		if (version == EFI_TCG2_EVENT_LOG_FORMAT_TCG_2) {
			/*
			 * The TCG2 log format has variable length entries,
			 * and the information to decode the hash algorithms
			 * back into a size is contained in the first entry -
			 * pass a pointer to the final entry (to calculate its
			 * size) and the first entry (so we know how long each
			 * digest is)
			 */
			last_entry_size =
				__calc_tpm2_event_size((void *)last_entry_addr,
						    (void *)(long)log_location,
						    false);
		} else {
			last_entry_size = sizeof(struct tcpa_event) +
			   ((struct tcpa_event *) last_entry_addr)->event_size;
		}
		log_size = log_last_entry - log_location + last_entry_size;
	}

	/* Allocate space for the logs and copy them. */
	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA,
			     sizeof(*log_tbl) + log_size, (void **)&log_tbl);

	if (status != EFI_SUCCESS) {
		efi_printk("Unable to allocate memory for event log\n");
		return;
	}

	/*
	 * Figure out whether any events have already been logged to the
	 * final events structure, and if so how much space they take up
	 */
	if (version == EFI_TCG2_EVENT_LOG_FORMAT_TCG_2)
		final_events_table = get_efi_config_table(LINUX_EFI_TPM_FINAL_LOG_GUID);
	if (final_events_table && final_events_table->nr_events) {
		struct tcg_pcr_event2_head *header;
		int offset;
		void *data;
		int event_size;
		int i = final_events_table->nr_events;

		data = (void *)final_events_table;
		offset = sizeof(final_events_table->version) +
			sizeof(final_events_table->nr_events);

		while (i > 0) {
			header = data + offset + final_events_size;
			event_size = __calc_tpm2_event_size(header,
						   (void *)(long)log_location,
						   false);
			final_events_size += event_size;
			i--;
		}
	}

	memset(log_tbl, 0, sizeof(*log_tbl) + log_size);
	log_tbl->size = log_size;
	log_tbl->final_events_preboot_size = final_events_size;
	log_tbl->version = version;
	memcpy(log_tbl->log, (void *) first_entry_addr, log_size);

	status = efi_bs_call(install_configuration_table,
			     &linux_eventlog_guid, log_tbl);
	if (status != EFI_SUCCESS)
		goto err_free;
	return;

err_free:
	efi_bs_call(free_pool, log_tbl);
}
