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

#define get_efi_var(name, vendor, ...) \
	efi_call_runtime(get_variable, \
			 (efi_char16_t *)(name), (efi_guid_t *)(vendor), \
			 __VA_ARGS__)

#define set_efi_var(name, vendor, ...) \
	efi_call_runtime(set_variable, \
			 (efi_char16_t *)(name), (efi_guid_t *)(vendor), \
			 __VA_ARGS__)

/*
 * Enable reboot attack mitigation. This requests that the firmware clear the
 * RAM on next reboot before proceeding with boot, ensuring that any secrets
 * are cleared. If userland has ensured that all secrets have been removed
 * from RAM before reboot it can simply reset this variable.
 */
void efi_enable_reset_attack_mitigation(efi_system_table_t *sys_table_arg)
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

static void efi_retrieve_tpm2_eventlog_1_2(efi_system_table_t *sys_table_arg)
{
	efi_guid_t tcg2_guid = EFI_TCG2_PROTOCOL_GUID;
	efi_guid_t linux_eventlog_guid = LINUX_EFI_TPM_EVENT_LOG_GUID;
	efi_status_t status;
	efi_physical_addr_t log_location = 0, log_last_entry = 0;
	struct linux_efi_tpm_eventlog *log_tbl = NULL;
	unsigned long first_entry_addr, last_entry_addr;
	size_t log_size, last_entry_size;
	efi_bool_t truncated;
	void *tcg2_protocol = NULL;

	status = efi_call_early(locate_protocol, &tcg2_guid, NULL,
				&tcg2_protocol);
	if (status != EFI_SUCCESS)
		return;

	status = efi_call_proto(efi_tcg2_protocol, get_event_log, tcg2_protocol,
				EFI_TCG2_EVENT_LOG_FORMAT_TCG_1_2,
				&log_location, &log_last_entry, &truncated);
	if (status != EFI_SUCCESS)
		return;

	if (!log_location)
		return;
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
		last_entry_size = sizeof(struct tcpa_event) +
			((struct tcpa_event *) last_entry_addr)->event_size;
		log_size = log_last_entry - log_location + last_entry_size;
	}

	/* Allocate space for the logs and copy them. */
	status = efi_call_early(allocate_pool, EFI_LOADER_DATA,
				sizeof(*log_tbl) + log_size,
				(void **) &log_tbl);

	if (status != EFI_SUCCESS) {
		efi_printk(sys_table_arg,
			   "Unable to allocate memory for event log\n");
		return;
	}

	memset(log_tbl, 0, sizeof(*log_tbl) + log_size);
	log_tbl->size = log_size;
	log_tbl->version = EFI_TCG2_EVENT_LOG_FORMAT_TCG_1_2;
	memcpy(log_tbl->log, (void *) first_entry_addr, log_size);

	status = efi_call_early(install_configuration_table,
				&linux_eventlog_guid, log_tbl);
	if (status != EFI_SUCCESS)
		goto err_free;
	return;

err_free:
	efi_call_early(free_pool, log_tbl);
}

void efi_retrieve_tpm2_eventlog(efi_system_table_t *sys_table_arg)
{
	/* Only try to retrieve the logs in 1.2 format. */
	efi_retrieve_tpm2_eventlog_1_2(sys_table_arg);
}
