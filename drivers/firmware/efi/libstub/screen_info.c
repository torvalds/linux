// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <linux/screen_info.h>

#include <asm/efi.h>

#include "efistub.h"

/*
 * There are two ways of populating the core kernel's struct screen_info via the stub:
 * - using a configuration table, like below, which relies on the EFI init code
 *   to locate the table and copy the contents;
 * - by linking directly to the core kernel's copy of the global symbol.
 *
 * The latter is preferred because it makes the EFIFB earlycon available very
 * early, but it only works if the EFI stub is part of the core kernel image
 * itself. The zboot decompressor can only use the configuration table
 * approach.
 */

static efi_guid_t screen_info_guid = LINUX_EFI_SCREEN_INFO_TABLE_GUID;

struct screen_info *__alloc_screen_info(void)
{
	struct screen_info *si;
	efi_status_t status;

	status = efi_bs_call(allocate_pool, EFI_ACPI_RECLAIM_MEMORY,
			     sizeof(*si), (void **)&si);

	if (status != EFI_SUCCESS)
		return NULL;

	status = efi_bs_call(install_configuration_table,
			     &screen_info_guid, si);
	if (status == EFI_SUCCESS)
		return si;

	efi_bs_call(free_pool, si);
	return NULL;
}

void free_screen_info(struct screen_info *si)
{
	if (!si)
		return;

	efi_bs_call(install_configuration_table, &screen_info_guid, NULL);
	efi_bs_call(free_pool, si);
}
