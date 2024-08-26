// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2022 Google LLC
// Author: Ard Biesheuvel <ardb@google.com>

#include <linux/efi.h>

#include "efistub.h"

typedef union efi_smbios_protocol efi_smbios_protocol_t;

union efi_smbios_protocol {
	struct {
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
	struct {
		u32 add;
		u32 update_string;
		u32 remove;
		u32 get_next;

		u8 major_version;
		u8 minor_version;
	} mixed_mode;
};

const struct efi_smbios_record *efi_get_smbios_record(u8 type)
{
	struct efi_smbios_record *record;
	efi_smbios_protocol_t *smbios;
	efi_status_t status;
	u16 handle = 0xfffe;

	status = efi_bs_call(locate_protocol, &EFI_SMBIOS_PROTOCOL_GUID, NULL,
			     (void **)&smbios) ?:
		 efi_call_proto(smbios, get_next, &handle, &type, &record, NULL);
	if (status != EFI_SUCCESS)
		return NULL;
	return record;
}

const u8 *__efi_get_smbios_string(const struct efi_smbios_record *record,
				  const u8 *offset)
{
	const u8 *strtable;

	if (!record)
		return NULL;

	strtable = (u8 *)record + record->length;
	for (int i = 1; i < *offset; i++) {
		int len = strlen(strtable);

		if (!len)
			return NULL;
		strtable += len + 1;
	}
	return strtable;
}
