// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 MIPS.
 */

#include <linux/memory.h>
#include <linux/module.h>
#include <asm/text-patching.h>
#include <asm/alternative.h>
#include <asm/cacheflush.h>
#include <asm/errata_list.h>
#include <asm/vendorid_list.h>
#include <asm/vendor_extensions.h>
#include <asm/vendor_extensions/mips.h>

static inline bool errata_probe_pause(unsigned int stage)
{
	if (!IS_ENABLED(CONFIG_ERRATA_MIPS_P8700_PAUSE_OPCODE))
		return false;

	if (!riscv_isa_vendor_extension_available(MIPS_VENDOR_ID, XMIPSEXECTL))
		return false;

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT)
		return false;

	return true;
}

static inline bool errata_probe_wfi(unsigned int stage)
{
	if (!IS_ENABLED(CONFIG_ERRATA_MIPS_P8700_WFI))
		return false;

	if (!riscv_isa_vendor_extension_available(MIPS_VENDOR_ID, XMIPSEXECTL))
		return false;

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT)
		return false;

	return true;
}

static u32 mips_errata_probe(unsigned int stage)
{
	u32 cpu_req_errata = 0;

	if (errata_probe_pause(stage))
		cpu_req_errata |= BIT(ERRATA_MIPS_P8700_PAUSE_OPCODE);

	if (errata_probe_wfi(stage))
		cpu_req_errata |= BIT(ERRATA_MIPS_P8700_WFI);

	return cpu_req_errata;
}

void mips_errata_patch_func(struct alt_entry *begin, struct alt_entry *end,
			    unsigned long archid, unsigned long impid,
			    unsigned int stage)
{
	struct alt_entry *alt;
	u32 cpu_req_errata = mips_errata_probe(stage);
	u32 tmp;
	void *oldptr, *altptr;

	BUILD_BUG_ON(ERRATA_MIPS_NUMBER >= RISCV_VENDOR_EXT_ALTERNATIVES_BASE);

	for (alt = begin; alt < end; alt++) {
		if (alt->vendor_id != MIPS_VENDOR_ID)
			continue;

		if (alt->patch_id >= ERRATA_MIPS_NUMBER)
			continue;

		tmp = (1U << alt->patch_id);
		if (cpu_req_errata & tmp) {
			oldptr = ALT_OLD_PTR(alt);
			altptr = ALT_ALT_PTR(alt);

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
