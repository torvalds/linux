// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <asm/alternative.h>
#include <asm/cacheflush.h>
#include <asm/errata_list.h>
#include <asm/patch.h>
#include <asm/vendorid_list.h>

static bool errata_probe_pbmt(unsigned int stage,
			      unsigned long arch_id, unsigned long impid)
{
	if (!IS_ENABLED(CONFIG_ERRATA_THEAD_PBMT))
		return false;

	if (arch_id != 0 || impid != 0)
		return false;

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT ||
	    stage == RISCV_ALTERNATIVES_MODULE)
		return true;

	return false;
}

static bool errata_probe_cmo(unsigned int stage,
			     unsigned long arch_id, unsigned long impid)
{
	if (!IS_ENABLED(CONFIG_ERRATA_THEAD_CMO))
		return false;

	if (arch_id != 0 || impid != 0)
		return false;

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT)
		return false;

	riscv_cbom_block_size = L1_CACHE_BYTES;
	riscv_noncoherent_supported();
	return true;
}

static bool errata_probe_pmu(unsigned int stage,
			     unsigned long arch_id, unsigned long impid)
{
	if (!IS_ENABLED(CONFIG_ERRATA_THEAD_PMU))
		return false;

	/* target-c9xx cores report arch_id and impid as 0 */
	if (arch_id != 0 || impid != 0)
		return false;

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT)
		return false;

	return true;
}

static u32 thead_errata_probe(unsigned int stage,
			      unsigned long archid, unsigned long impid)
{
	u32 cpu_req_errata = 0;

	if (errata_probe_pbmt(stage, archid, impid))
		cpu_req_errata |= BIT(ERRATA_THEAD_PBMT);

	if (errata_probe_cmo(stage, archid, impid))
		cpu_req_errata |= BIT(ERRATA_THEAD_CMO);

	if (errata_probe_pmu(stage, archid, impid))
		cpu_req_errata |= BIT(ERRATA_THEAD_PMU);

	return cpu_req_errata;
}

void __init_or_module thead_errata_patch_func(struct alt_entry *begin, struct alt_entry *end,
					      unsigned long archid, unsigned long impid,
					      unsigned int stage)
{
	struct alt_entry *alt;
	u32 cpu_req_errata = thead_errata_probe(stage, archid, impid);
	u32 tmp;

	for (alt = begin; alt < end; alt++) {
		if (alt->vendor_id != THEAD_VENDOR_ID)
			continue;
		if (alt->errata_id >= ERRATA_THEAD_NUMBER)
			continue;

		tmp = (1U << alt->errata_id);
		if (cpu_req_errata & tmp) {
			/* On vm-alternatives, the mmu isn't running yet */
			if (stage == RISCV_ALTERNATIVES_EARLY_BOOT)
				memcpy((void *)__pa_symbol(alt->old_ptr),
				       (void *)__pa_symbol(alt->alt_ptr), alt->alt_len);
			else
				patch_text_nosync(alt->old_ptr, alt->alt_ptr, alt->alt_len);
		}
	}

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT)
		local_flush_icache_all();
}
