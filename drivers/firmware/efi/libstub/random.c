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
	struct linux_efi_random_seed *prev_seed, *seed = NULL;
	int prev_seed_size = 0, seed_size = EFI_RANDOM_SEED_SIZE;
	efi_rng_protocol_t *rng = NULL;
	efi_status_t status;

	status = efi_bs_call(locate_protocol, &rng_proto, NULL, (void **)&rng);
	if (status != EFI_SUCCESS)
		return status;

	/*
	 * Check whether a seed was provided by a prior boot stage. In that
	 * case, instead of overwriting it, let's create a new buffer that can
	 * hold both, and concatenate the existing and the new seeds.
	 * Note that we should read the seed size with caution, in case the
	 * table got corrupted in memory somehow.
	 */
	prev_seed = get_efi_config_table(LINUX_EFI_RANDOM_SEED_TABLE_GUID);
	if (prev_seed && prev_seed->size <= 512U) {
		prev_seed_size = prev_seed->size;
		seed_size += prev_seed_size;
	}

	/*
	 * Use EFI_ACPI_RECLAIM_MEMORY here so that it is guaranteed that the
	 * allocation will survive a kexec reboot (although we refresh the seed
	 * beforehand)
	 */
	status = efi_bs_call(allocate_pool, EFI_ACPI_RECLAIM_MEMORY,
			     struct_size(seed, bits, seed_size),
			     (void **)&seed);
	if (status != EFI_SUCCESS) {
		efi_warn("Failed to allocate memory for RNG seed.\n");
		goto err_warn;
	}

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

	seed->size = seed_size;
	if (prev_seed_size)
		memcpy(seed->bits + EFI_RANDOM_SEED_SIZE, prev_seed->bits,
		       prev_seed_size);

	status = efi_bs_call(install_configuration_table, &rng_table_guid, seed);
	if (status != EFI_SUCCESS)
		goto err_freepool;

	if (prev_seed_size) {
		/* wipe and free the old seed if we managed to install the new one */
		memzero_explicit(prev_seed->bits, prev_seed_size);
		efi_bs_call(free_pool, prev_seed);
	}
	return EFI_SUCCESS;

err_freepool:
	memzero_explicit(seed, struct_size(seed, bits, seed_size));
	efi_bs_call(free_pool, seed);
	efi_warn("Failed to obtain seed from EFI_RNG_PROTOCOL\n");
err_warn:
	if (prev_seed)
		efi_warn("Retaining bootloader-supplied seed only");
	return status;
}
