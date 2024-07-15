// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2023 Google LLC
// Authors: Ard Biesheuvel <ardb@google.com>
//          Peter Collingbourne <pcc@google.com>

#include <linux/elf.h>
#include <linux/init.h>
#include <linux/types.h>

#include "pi.h"

extern const Elf64_Rela rela_start[], rela_end[];
extern const u64 relr_start[], relr_end[];

void __init relocate_kernel(u64 offset)
{
	u64 *place = NULL;

	for (const Elf64_Rela *rela = rela_start; rela < rela_end; rela++) {
		if (ELF64_R_TYPE(rela->r_info) != R_AARCH64_RELATIVE)
			continue;
		*(u64 *)(rela->r_offset + offset) = rela->r_addend + offset;
	}

	if (!IS_ENABLED(CONFIG_RELR) || !offset)
		return;

	/*
	 * Apply RELR relocations.
	 *
	 * RELR is a compressed format for storing relative relocations. The
	 * encoded sequence of entries looks like:
	 * [ AAAAAAAA BBBBBBB1 BBBBBBB1 ... AAAAAAAA BBBBBB1 ... ]
	 *
	 * i.e. start with an address, followed by any number of bitmaps. The
	 * address entry encodes 1 relocation. The subsequent bitmap entries
	 * encode up to 63 relocations each, at subsequent offsets following
	 * the last address entry.
	 *
	 * The bitmap entries must have 1 in the least significant bit. The
	 * assumption here is that an address cannot have 1 in lsb. Odd
	 * addresses are not supported. Any odd addresses are stored in the
	 * RELA section, which is handled above.
	 *
	 * With the exception of the least significant bit, each bit in the
	 * bitmap corresponds with a machine word that follows the base address
	 * word, and the bit value indicates whether or not a relocation needs
	 * to be applied to it. The second least significant bit represents the
	 * machine word immediately following the initial address, and each bit
	 * that follows represents the next word, in linear order. As such, a
	 * single bitmap can encode up to 63 relocations in a 64-bit object.
	 */
	for (const u64 *relr = relr_start; relr < relr_end; relr++) {
		if ((*relr & 1) == 0) {
			place = (u64 *)(*relr + offset);
			*place++ += offset;
		} else {
			for (u64 *p = place, r = *relr >> 1; r; p++, r >>= 1)
				if (r & 1)
					*p += offset;
			place += 63;
		}
	}
}
