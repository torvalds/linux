// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <asm/alternative.h>
#include <asm/facility.h>
#include <asm/nospec-branch.h>

void __init_or_module apply_alternatives(struct alt_instr *start,
					 struct alt_instr *end)
{
	struct alt_instr *a;
	u8 *instr, *replacement;

	/*
	 * The scan order should be from start to end. A later scanned
	 * alternative code can overwrite previously scanned alternative code.
	 */
	for (a = start; a < end; a++) {
		instr = (u8 *)&a->instr_offset + a->instr_offset;
		replacement = (u8 *)&a->repl_offset + a->repl_offset;

		if (!__test_facility(a->feature, alt_stfle_fac_list))
			continue;
		s390_kernel_write(instr, replacement, a->instrlen);
	}
}

extern struct alt_instr __alt_instructions[], __alt_instructions_end[];
void __init apply_alternative_instructions(void)
{
	apply_alternatives(__alt_instructions, __alt_instructions_end);
}
