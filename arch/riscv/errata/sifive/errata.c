// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Sifive.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/bug.h>
#include <asm/patch.h>
#include <asm/alternative.h>
#include <asm/vendorid_list.h>
#include <asm/errata_list.h>

struct errata_info_t {
	char name[ERRATA_STRING_LENGTH_MAX];
	bool (*check_func)(unsigned long  arch_id, unsigned long impid);
};

static u32 __init sifive_errata_probe(unsigned long archid, unsigned long impid)
{
	int idx;
	u32 cpu_req_errata = 0;

	for (idx = 0; idx < ERRATA_SIFIVE_NUMBER; idx++)
		if (errata_list[idx].check_func(archid, impid))
			cpu_req_errata |= (1U << idx);

	return cpu_req_errata;
}

static void __init warn_miss_errata(u32 miss_errata)
{
	int i;

	pr_warn("----------------------------------------------------------------\n");
	pr_warn("WARNING: Missing the following errata may cause potential issues\n");
	for (i = 0; i < ERRATA_SIFIVE_NUMBER; i++)
		if (miss_errata & 0x1 << i)
			pr_warn("\tSiFive Errata[%d]:%s\n", i, errata_list[i].name);
	pr_warn("Please enable the corresponding Kconfig to apply them\n");
	pr_warn("----------------------------------------------------------------\n");
}

void __init sifive_errata_patch_func(struct alt_entry *begin, struct alt_entry *end,
				     unsigned long archid, unsigned long impid)
{
	struct alt_entry *alt;
	u32 cpu_req_errata = sifive_errata_probe(archid, impid);
	u32 cpu_apply_errata = 0;
	u32 tmp;

	for (alt = begin; alt < end; alt++) {
		if (alt->vendor_id != SIFIVE_VENDOR_ID)
			continue;
		if (alt->errata_id >= ERRATA_SIFIVE_NUMBER) {
			WARN(1, "This errata id:%d is not in kernel errata list", alt->errata_id);
			continue;
		}

		tmp = (1U << alt->errata_id);
		if (cpu_req_errata & tmp) {
			patch_text_nosync(alt->old_ptr, alt->alt_ptr, alt->alt_len);
			cpu_apply_errata |= tmp;
		}
	}
	if (cpu_apply_errata != cpu_req_errata)
		warn_miss_errata(cpu_req_errata - cpu_apply_errata);
}
