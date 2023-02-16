// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013, 2014 Linaro Ltd;  <roy.franz@linaro.org>
 *
 * This file implements the EFI boot stub for the arm64 kernel.
 * Adapted from ARM version by Mark Salter <msalter@redhat.com>
 */


#include <linux/efi.h>
#include <asm/efi.h>
#include <asm/memory.h>
#include <asm/sysreg.h>

#include "efistub.h"

static bool system_needs_vamap(void)
{
	const u8 *type1_family = efi_get_smbios_string(1, family);

	/*
	 * Ampere eMAG, Altra, and Altra Max machines crash in SetTime() if
	 * SetVirtualAddressMap() has not been called prior.
	 */
	if (!type1_family || (
	    strcmp(type1_family, "eMAG") &&
	    strcmp(type1_family, "Altra") &&
	    strcmp(type1_family, "Altra Max")))
		return false;

	efi_warn("Working around broken SetVirtualAddressMap()\n");
	return true;
}

efi_status_t check_platform_features(void)
{
	u64 tg;

	/*
	 * If we have 48 bits of VA space for TTBR0 mappings, we can map the
	 * UEFI runtime regions 1:1 and so calling SetVirtualAddressMap() is
	 * unnecessary.
	 */
	if (VA_BITS_MIN >= 48 && !system_needs_vamap())
		efi_novamap = true;

	/* UEFI mandates support for 4 KB granularity, no need to check */
	if (IS_ENABLED(CONFIG_ARM64_4K_PAGES))
		return EFI_SUCCESS;

	tg = (read_cpuid(ID_AA64MMFR0_EL1) >> ID_AA64MMFR0_EL1_TGRAN_SHIFT) & 0xf;
	if (tg < ID_AA64MMFR0_EL1_TGRAN_SUPPORTED_MIN || tg > ID_AA64MMFR0_EL1_TGRAN_SUPPORTED_MAX) {
		if (IS_ENABLED(CONFIG_ARM64_64K_PAGES))
			efi_err("This 64 KB granular kernel is not supported by your CPU\n");
		else
			efi_err("This 16 KB granular kernel is not supported by your CPU\n");
		return EFI_UNSUPPORTED;
	}
	return EFI_SUCCESS;
}

void efi_cache_sync_image(unsigned long image_base,
			  unsigned long alloc_size,
			  unsigned long code_size)
{
	u32 ctr = read_cpuid_effective_cachetype();
	u64 lsize = 4 << cpuid_feature_extract_unsigned_field(ctr,
						CTR_EL0_DminLine_SHIFT);

	do {
		asm("dc civac, %0" :: "r"(image_base));
		image_base += lsize;
		alloc_size -= lsize;
	} while (alloc_size >= lsize);

	asm("ic ialluis");
	dsb(ish);
	isb();
}
