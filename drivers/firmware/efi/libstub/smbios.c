// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2022 Google LLC
// Author: Ard Biesheuvel <ardb@google.com>

#include <linux/efi.h>

#include "efistub.h"

typedef struct efi_smbios_protocol efi_smbios_protocol_t;

struct efi_smbios_protocol {
	efi_status_t (__efiapi *add)(efi_smbios_protocol_t *, efi_handle_t,
				     u16 *, struct efi_smbios_record *);
	efi_status_t (__efiapi *update_string)(efi_smbios_protocol_t *, u16 *,
					       unsigned long *, u8 *);
	efi_status_t (__efiapi *remove)(efi_smbios_protocol_t *, u16);
	efi_status_t (__efiapi *get_next)(efi_smbios_protocol_t *, u16 *, u8 *,
					  struct efi_smbios_record **,
					  efi_handle_t *);

	u8 major_version;
	u8 minor_version;
};

const u8 *__efi_get_smbios_string(u8 type, int offset, int recsize)
{
	struct efi_smbios_record *record;
	efi_smbios_protocol_t *smbios;
	efi_status_t status;
	u16 handle = 0xfffe;
	const u8 *strtable;

	status = efi_bs_call(locate_protocol, &EFI_SMBIOS_PROTOCOL_GUID, NULL,
			     (void **)&smbios) ?:
		 efi_call_proto(smbios, get_next, &handle, &type, &record, NULL);
	if (status != EFI_SUCCESS)
		return NULL;

	strtable = (u8 *)record + record->length;
	for (int i = 1; i < ((u8 *)record)[offset]; i++) {
		int len = strlen(strtable);

		if (!len)
			return NULL;
		strtable += len + 1;
	}
	return strtable;
}
