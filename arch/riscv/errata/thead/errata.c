// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <asm/alternative.h>
#include <asm/cacheflush.h>
#include <asm/cpufeature.h>
#include <asm/errata_list.h>
#include <asm/hwprobe.h>
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

	if (stage == RISCV_ALTERNATIVES_BOOT) {
		riscv_cbom_block_size = L1_CACHE_BYTES;
		riscv_noncoherent_supported();
	}

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

void thead_errata_patch_func(struct alt_entry *begin, struct alt_entry *end,
			     unsigned long archid, unsigned long impid,
			     unsigned int stage)
{
	struct alt_entry *alt;
	u32 cpu_req_errata = thead_errata_probe(stage, archid, impid);
	u32 tmp;
	void *oldptr, *altptr;

	for (alt = begin; alt < end; alt++) {
		if (alt->vendor_id != THEAD_VENDOR_ID)
			continue;
		if (alt->patch_id >= ERRATA_THEAD_NUMBER)
			continue;

		tmp = (1U << alt->patch_id);
		if (cpu_req_errata & tmp) {
			oldptr = ALT_OLD_PTR(alt);
			altptr = ALT_ALT_PTR(alt);

			/* On vm-alternatives, the mmu isn't running yet */
			if (stage == RISCV_ALTERNATIVES_EARLY_BOOT) {
				memcpy(oldptr, altptr, alt->alt_len);
			} else {
				mutex_lock(&text_mutex);
				patch_text_nosync(oldptr, altptr, alt->alt_len);
				mutex_unlock(&text_mutex);
			}
		}
	}

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT)
		local_flush_icache_all();
}
