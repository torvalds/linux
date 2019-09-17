// SPDX-License-Identifier: GPL-2.0
/*
 *    Alternative live-patching for parisc.
 *    Copyright (C) 2018 Helge Deller <deller@gmx.de>
 *
 */

#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/alternative.h>

#include <linux/module.h>

static int no_alternatives;
static int __init setup_no_alternatives(char *str)
{
	no_alternatives = 1;
	return 1;
}
__setup("no-alternatives", setup_no_alternatives);

void __init_or_module apply_alternatives(struct alt_instr *start,
		 struct alt_instr *end, const char *module_name)
{
	struct alt_instr *entry;
	int index = 0, applied = 0;
	int num_cpus = num_online_cpus();

	for (entry = start; entry < end; entry++, index++) {

		u32 *from, cond, replacement;
		s32 len;

		from = (u32 *)((ulong)&entry->orig_offset + entry->orig_offset);
		len = entry->len;
		cond = entry->cond;
		replacement = entry->replacement;

		WARN_ON(!cond);

		if (cond != ALT_COND_ALWAYS && no_alternatives)
			continue;

		pr_debug("Check %d: Cond 0x%x, Replace %02d instructions @ 0x%px with 0x%08x\n",
			index, cond, len, from, replacement);

		if ((cond & ALT_COND_NO_SMP) && (num_cpus != 1))
			continue;
		if ((cond & ALT_COND_NO_DCACHE) && (cache_info.dc_size != 0))
			continue;
		if ((cond & ALT_COND_NO_ICACHE) && (cache_info.ic_size != 0))
			continue;
		if ((cond & ALT_COND_RUN_ON_QEMU) && !running_on_qemu)
			continue;

		/*
		 * If the PDC_MODEL capabilities has Non-coherent IO-PDIR bit
		 * set (bit #61, big endian), we have to flush and sync every
		 * time IO-PDIR is changed in Ike/Astro.
		 */
		if ((cond & ALT_COND_NO_IOC_FDC) &&
			((boot_cpu_data.cpu_type <= pcxw_) ||
			 (boot_cpu_data.pdc.capabilities & PDC_MODEL_IOPDIR_FDC)))
			continue;

		/* Want to replace pdtlb by a pdtlb,l instruction? */
		if (replacement == INSN_PxTLB) {
			replacement = *from;
			if (boot_cpu_data.cpu_type >= pcxu) /* >= pa2.0 ? */
				replacement |= (1 << 10); /* set el bit */
		}

		/*
		 * Replace instruction with NOPs?
		 * For long distance insert a branch instruction instead.
		 */
		if (replacement == INSN_NOP && len > 1)
			replacement = 0xe8000002 + (len-2)*8; /* "b,n .+8" */

		pr_debug("ALTERNATIVE %3d: Cond %2x, Replace %2d instructions to 0x%08x @ 0x%px (%pS)\n",
			index, cond, len, replacement, from, from);

		if (len < 0) {
			/* Replace multiple instruction by new code */
			u32 *source;
			len = -len;
			source = (u32 *)((ulong)&entry->replacement + entry->replacement);
			memcpy(from, source, 4 * len);
		} else {
			/* Replace by one instruction */
			*from = replacement;
		}
		applied++;
	}

	pr_info("%s%salternatives: applied %d out of %d patches\n",
		module_name ? : "", module_name ? " " : "",
		applied, index);
}


void __init apply_alternatives_all(void)
{
	set_kernel_text_rw(1);

	apply_alternatives((struct alt_instr *) &__alt_instructions,
		(struct alt_instr *) &__alt_instructions_end, NULL);

	set_kernel_text_rw(0);
}
