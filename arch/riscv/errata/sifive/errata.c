// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Sifive.
 */

#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/bug.h>
#include <asm/text-patching.h>
#include <asm/alternative.h>
#include <asm/vendorid_list.h>
#include <asm/errata_list.h>
#include <asm/vendor_extensions.h>

struct errata_info_t {
	char name[32];
	bool (*check_func)(unsigned long  arch_id, unsigned long impid);
};

static bool errata_cip_453_check_func(unsigned long  arch_id, unsigned long impid)
{
	/*
	 * Affected cores:
	 * Architecture ID: 0x8000000000000007
	 * Implement ID: 0x20181004 <= impid <= 0x20191105
	 */
	if (arch_id != 0x8000000000000007 ||
	    (impid < 0x20181004 || impid > 0x20191105))
		return false;
	return true;
}

static bool errata_cip_1200_check_func(unsigned long  arch_id, unsigned long impid)
{
	/*
	 * Affected cores:
	 * Architecture ID: 0x8000000000000007 or 0x1
	 * Implement ID: mimpid[23:0] <= 0x200630 and mimpid != 0x01200626
	 */
	if (arch_id != 0x8000000000000007 && arch_id != 0x1)
		return false;
	if ((impid & 0xffffff) > 0x200630 || impid == 0x1200626)
		return false;

#ifdef CONFIG_MMU
	tlb_flush_all_threshold = 0;
#endif

	return true;
}

static struct errata_info_t errata_list[ERRATA_SIFIVE_NUMBER] = {
	{
		.name = "cip-453",
		.check_func = errata_cip_453_check_func
	},
	{
		.name = "cip-1200",
		.check_func = errata_cip_1200_check_func
	},
};

static u32 __init_or_module sifive_errata_probe(unsigned long archid,
						unsigned long impid)
{
	int idx;
	u32 cpu_req_errata = 0;

	for (idx = 0; idx < ERRATA_SIFIVE_NUMBER; idx++)
		if (errata_list[idx].check_func(archid, impid))
			cpu_req_errata |= (1U << idx);

	return cpu_req_errata;
}

static void __init_or_module warn_miss_errata(u32 miss_errata)
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

void sifive_errata_patch_func(struct alt_entry *begin, struct alt_entry *end,
			      unsigned long archid, unsigned long impid,
			      unsigned int stage)
{
	struct alt_entry *alt;
	u32 cpu_req_errata;
	u32 cpu_apply_errata = 0;
	u32 tmp;

	BUILD_BUG_ON(ERRATA_SIFIVE_NUMBER >= RISCV_VENDOR_EXT_ALTERNATIVES_BASE);

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT)
		return;

	cpu_req_errata = sifive_errata_probe(archid, impid);

	for (alt = begin; alt < end; alt++) {
		if (alt->vendor_id != SIFIVE_VENDOR_ID)
			continue;
		if (alt->patch_id >= ERRATA_SIFIVE_NUMBER) {
			WARN(1, "This errata id:%d is not in kernel errata list", alt->patch_id);
			continue;
		}

		tmp = (1U << alt->patch_id);
		if (cpu_req_errata & tmp) {
			mutex_lock(&text_mutex);
			patch_text_nosync(ALT_OLD_PTR(alt), ALT_ALT_PTR(alt),
					  alt->alt_len);
			mutex_unlock(&text_mutex);
			cpu_apply_errata |= tmp;
		}
	}
	if (stage != RISCV_ALTERNATIVES_MODULE &&
	    cpu_apply_errata != cpu_req_errata)
		warn_miss_errata(cpu_req_errata - cpu_apply_errata);
}
