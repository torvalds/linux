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

struct errata_info {
	char name[ERRATA_STRING_LENGTH_MAX];
	bool (*check_func)(unsigned long arch_id, unsigned long impid);
	unsigned int stage;
};

static bool errata_mt_check_func(unsigned long  arch_id, unsigned long impid)
{
	if (arch_id != 0 || impid != 0)
		return false;
	return true;
}

static const struct errata_info errata_list[ERRATA_THEAD_NUMBER] = {
	{
		.name = "memory-types",
		.stage = RISCV_ALTERNATIVES_EARLY_BOOT,
		.check_func = errata_mt_check_func
	},
};

static u32 thead_errata_probe(unsigned int stage, unsigned long archid, unsigned long impid)
{
	const struct errata_info *info;
	u32 cpu_req_errata = 0;
	int idx;

	for (idx = 0; idx < ERRATA_THEAD_NUMBER; idx++) {
		info = &errata_list[idx];

		if ((stage == RISCV_ALTERNATIVES_MODULE ||
		     info->stage == stage) && info->check_func(archid, impid))
			cpu_req_errata |= (1U << idx);
	}

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
