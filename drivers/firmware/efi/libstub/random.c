// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 Linaro Ltd;  <ard.biesheuvel@linaro.org>
 */

#include <linux/efi.h>
#include <asm/efi.h>

#include "efistub.h"

typedef union efi_rng_protocol efi_rng_protocol_t;

union efi_rng_protocol {
	struct {
		efi_status_t (__efiapi *get_info)(efi_rng_protocol_t *,
						  unsigned long *,
						  efi_guid_t *);
		efi_status_t (__efiapi *get_rng)(efi_rng_protocol_t *,
						 efi_guid_t *, unsigned long,
						 u8 *out);
	};
	struct {
		u32 get_info;
		u32 get_rng;
	} mixed_mode;
};

/**
 * efi_get_random_bytes() - fill a buffer with random bytes
 * @size:	size of the buffer
 * @out:	caller allocated buffer to receive the random bytes
 *
 * The call will fail if either the firmware does not implement the
 * EFI_RNG_PROTOCOL or there are not enough random bytes available to fill
 * the buffer.
 *
 * Return:	status code
 */
efi_status_t efi_get_random_bytes(unsigned long size, u8 *out)
{
	efi_guid_t rng_proto = EFI_RNG_PROTOCOL_GUID;
	efi_status_t status;
	efi_rng_protocol_t *rng = NULL;

	status = efi_bs_call(locate_protocol, &rng_proto, NULL, (void **)&rng);
	if (status != EFI_SUCCESS)
		return status;

	return efi_call_proto(rng, get_rng, NULL, size, out);
}

/**
 * efi_random_get_seed() - provide random seed as configuration table
 *
 * The EFI_RNG_PROTOCOL is used to read random bytes. These random bytes are
 * saved as a configuration table which can be used as entropy by the kernel
 * for the initialization of its pseudo random number generator.
 *
 * If the EFI_RNG_PROTOCOL is not available or there are not enough random bytes
 * available, the configuration table will not be installed and an error code
 * will be returned.
 *
 * Return:	status code
 */
efi_status_t efi_random_get_seed(void)
{
	efi_guid_t rng_proto = EFI_RNG_PROTOCOL_GUID;
	efi_guid_t rng_algo_raw = EFI_RNG_ALGORITHM_RAW;
	efi_guid_t rng_table_guid = LINUX_EFI_RANDOM_SEED_TABLE_GUID;
	efi_rng_protocol_t *rng = NULL;
	struct linux_efi_random_seed *seed = NULL;
	efi_status_t status;

	status = efi_bs_call(locate_protocol, &rng_proto, NULL, (void **)&rng);
	if (status != EFI_SUCCESS)
		return status;

	/*
	 * Use EFI_ACPI_RECLAIM_MEMORY here so that it is guaranteed that the
	 * allocation will survive a kexec reboot (although we refresh the seed
	 * beforehand)
	 */
	status = efi_bs_call(allocate_pool, EFI_ACPI_RECLAIM_MEMORY,
			     sizeof(*seed) + EFI_RANDOM_SEED_SIZE,
			     (void **)&seed);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_call_proto(rng, get_rng, &rng_algo_raw,
				 EFI_RANDOM_SEED_SIZE, seed->bits);

	if (status == EFI_UNSUPPORTED)
		/*
		 * Use whatever algorithm we have available if the raw algorithm
		 * is not implemented.
		 */
		status = efi_call_proto(rng, get_rng, NULL,
					EFI_RANDOM_SEED_SIZE, seed->bits);

	if (status != EFI_SUCCESS)
		goto err_freepool;

	seed->size = EFI_RANDOM_SEED_SIZE;
	status = efi_bs_call(install_configuration_table, &rng_table_guid, seed);
	if (status != EFI_SUCCESS)
		goto err_freepool;

	return EFI_SUCCESS;

err_freepool:
	efi_bs_call(free_pool, seed);
	return status;
}
