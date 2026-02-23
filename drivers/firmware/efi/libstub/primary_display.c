// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <linux/sysfb.h>

#include <asm/efi.h>

#include "efistub.h"

/*
 * There are two ways of populating the core kernel's sysfb_primary_display
 * via the stub:
 *
 *   - using a configuration table, which relies on the EFI init code to
 *     locate the table and copy the contents; or
 *
 *   - by linking directly to the core kernel's copy of the global symbol.
 *
 * The latter is preferred because it makes the EFIFB earlycon available very
 * early, but it only works if the EFI stub is part of the core kernel image
 * itself. The zboot decompressor can only use the configuration table
 * approach.
 */

static efi_guid_t primary_display_guid = LINUX_EFI_PRIMARY_DISPLAY_TABLE_GUID;

struct sysfb_display_info *__alloc_primary_display(void)
{
	struct sysfb_display_info *dpy;
	efi_status_t status;

	status = efi_bs_call(allocate_pool, EFI_ACPI_RECLAIM_MEMORY,
			     sizeof(*dpy), (void **)&dpy);

	if (status != EFI_SUCCESS)
		return NULL;

	memset(dpy, 0, sizeof(*dpy));

	status = efi_bs_call(install_configuration_table,
			     &primary_display_guid, dpy);
	if (status == EFI_SUCCESS)
		return dpy;

	efi_bs_call(free_pool, dpy);
	return NULL;
}

void free_primary_display(struct sysfb_display_info *dpy)
{
	if (!dpy)
		return;

	efi_bs_call(install_configuration_table, &primary_display_guid, NULL);
	efi_bs_call(free_pool, dpy);
}
