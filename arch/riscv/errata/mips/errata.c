// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 MIPS.
 */

#include <linux/memory.h>
#include <linux/module.h>
#include <asm/text-patching.h>
#include <asm/alternative.h>
#include <asm/errata_list.h>
#include <asm/vendorid_list.h>
#include <asm/vendor_extensions.h>
#include <asm/vendor_extensions/mips.h>

static inline bool errata_probe_pause(void)
{
	if (!IS_ENABLED(CONFIG_ERRATA_MIPS_P8700_PAUSE_OPCODE))
		return false;

	if (!riscv_isa_vendor_extension_available(MIPS_VENDOR_ID, XMIPSEXECTL))
		return false;

	return true;
}

static u32 mips_errata_probe(void)
{
	u32 cpu_req_errata = 0;

	if (errata_probe_pause())
		cpu_req_errata |= BIT(ERRATA_MIPS_P8700_PAUSE_OPCODE);

	return cpu_req_errata;
}

void mips_errata_patch_func(struct alt_entry *begin, struct alt_entry *end,
			    unsigned long archid, unsigned long impid,
			    unsigned int stage)
{
	struct alt_entry *alt;
	u32 cpu_req_errata = mips_errata_probe();
	u32 tmp;

	BUILD_BUG_ON(ERRATA_MIPS_NUMBER >= RISCV_VENDOR_EXT_ALTERNATIVES_BASE);

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT)
		return;

	for (alt = begin; alt < end; alt++) {
		if (alt->vendor_id != MIPS_VENDOR_ID)
			continue;

		if (alt->patch_id >= ERRATA_MIPS_NUMBER) {
			WARN(1, "MIPS errata id:%d not in kernel errata list\n",
			     alt->patch_id);
			continue;
		}

		tmp = (1U << alt->patch_id);
		if (cpu_req_errata && tmp) {
			mutex_lock(&text_mutex);
			patch_text_nosync(ALT_OLD_PTR(alt), ALT_ALT_PTR(alt),
					  alt->alt_len);
			mutex_unlock(&text_mutex);
		}
	}
}
