// SPDX-License-Identifier: GPL-2.0-only

#include <asm/csr.h>
#include <linux/processor.h>

#include "pi.h"

/*
 * To avoid rewriting code include asm/archrandom.h and create macros
 * for the functions that won't be included.
 */
#undef riscv_has_extension_unlikely
#define riscv_has_extension_likely(...) false
#undef pr_err_once
#define pr_err_once(...)

#include <asm/archrandom.h>

u64 get_kaslr_seed_zkr(const uintptr_t dtb_pa)
{
	unsigned long seed = 0;

	if (!fdt_early_match_extension_isa((const void *)dtb_pa, "zkr"))
		return 0;

	if (!csr_seed_long(&seed))
		return 0;

	return seed;
}
