/*
 * Copyright (C) 2016 Linaro Ltd;  <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/efi.h>
#include <asm/efi.h>

#include "efistub.h"

struct efi_rng_protocol {
	efi_status_t (*get_info)(struct efi_rng_protocol *,
				 unsigned long *, efi_guid_t *);
	efi_status_t (*get_rng)(struct efi_rng_protocol *,
				efi_guid_t *, unsigned long, u8 *out);
};

efi_status_t efi_get_random_bytes(efi_system_table_t *sys_table_arg,
				  unsigned long size, u8 *out)
{
	efi_guid_t rng_proto = EFI_RNG_PROTOCOL_GUID;
	efi_status_t status;
	struct efi_rng_protocol *rng;

	status = efi_call_early(locate_protocol, &rng_proto, NULL,
				(void **)&rng);
	if (status != EFI_SUCCESS)
		return status;

	return rng->get_rng(rng, NULL, size, out);
}
