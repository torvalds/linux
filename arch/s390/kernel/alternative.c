// SPDX-License-Identifier: GPL-2.0

#include <linux/uaccess.h>
#include <asm/nospec-branch.h>
#include <asm/abs_lowcore.h>
#include <asm/alternative.h>
#include <asm/facility.h>
#include <asm/sections.h>
#include <asm/machine.h>

unsigned long __bootdata_preserved(machine_features[1]);

void __apply_alternatives(struct alt_instr *start, struct alt_instr *end, unsigned int ctx)
{
	u8 *instr, *replacement;
	struct alt_instr *a;
	bool replace;

	/*
	 * The scan order should be from start to end. A later scanned
	 * alternative code can overwrite previously scanned alternative code.
	 */
	for (a = start; a < end; a++) {
		if (!(a->ctx & ctx))
			continue;
		switch (a->type) {
		case ALT_TYPE_FACILITY:
			replace = test_facility(a->data);
			break;
		case ALT_TYPE_FEATURE:
			replace = test_machine_feature(a->data);
			break;
		case ALT_TYPE_SPEC:
			replace = nobp_enabled();
			break;
		default:
			replace = false;
		}
		if (!replace)
			continue;
		instr = (u8 *)&a->instr_offset + a->instr_offset;
		replacement = (u8 *)&a->repl_offset + a->repl_offset;
		s390_kernel_write(instr, replacement, a->instrlen);
	}
}
