// SPDX-License-Identifier: GPL-2.0

#include <linux/uaccess.h>
#include <asm/alternative.h>
#include <asm/facility.h>

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
		case ALT_TYPE_FACILITY_EARLY:
			replace = test_facility(a->data);
			break;
#ifndef __DECOMPRESSOR
		case ALT_TYPE_FACILITY:
			replace = __test_facility(a->data, alt_stfle_fac_list);
			break;
#endif
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
