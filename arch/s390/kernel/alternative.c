// SPDX-License-Identifier: GPL-2.0

#ifndef pr_fmt
#define pr_fmt(fmt)	"alt: " fmt
#endif

#include <linux/uaccess.h>
#include <linux/printk.h>
#include <asm/nospec-branch.h>
#include <asm/abs_lowcore.h>
#include <asm/alternative.h>
#include <asm/facility.h>
#include <asm/sections.h>
#include <asm/machine.h>

#ifndef a_debug
#define a_debug		pr_debug
#endif

#ifndef __kernel_va
#define __kernel_va(x)	(void *)(x)
#endif

unsigned long __bootdata_preserved(machine_features[1]);

struct alt_debug {
	unsigned long facilities[MAX_FACILITY_BIT / BITS_PER_LONG];
	unsigned long mfeatures[MAX_MFEATURE_BIT / BITS_PER_LONG];
	int spec;
};

static struct alt_debug __bootdata_preserved(alt_debug);

static void alternative_dump(u8 *old, u8 *new, unsigned int len, unsigned int type, unsigned int data)
{
	char oinsn[33], ninsn[33];
	unsigned long kptr;
	unsigned int pos;

	for (pos = 0; pos < len && 2 * pos < sizeof(oinsn) - 3; pos++)
		hex_byte_pack(&oinsn[2 * pos], old[pos]);
	oinsn[2 * pos] = 0;
	for (pos = 0; pos < len && 2 * pos < sizeof(ninsn) - 3; pos++)
		hex_byte_pack(&ninsn[2 * pos], new[pos]);
	ninsn[2 * pos] = 0;
	kptr = (unsigned long)__kernel_va(old);
	a_debug("[%d/%3d] %016lx: %s -> %s\n", type, data, kptr, oinsn, ninsn);
}

void __apply_alternatives(struct alt_instr *start, struct alt_instr *end, unsigned int ctx)
{
	struct alt_debug *d;
	struct alt_instr *a;
	bool debug, replace;
	u8 *old, *new;

	/*
	 * The scan order should be from start to end. A later scanned
	 * alternative code can overwrite previously scanned alternative code.
	 */
	d = &alt_debug;
	for (a = start; a < end; a++) {
		if (!(a->ctx & ctx))
			continue;
		switch (a->type) {
		case ALT_TYPE_FACILITY:
			replace = test_facility(a->data);
			debug = __test_facility(a->data, d->facilities);
			break;
		case ALT_TYPE_FEATURE:
			replace = test_machine_feature(a->data);
			debug = __test_machine_feature(a->data, d->mfeatures);
			break;
		case ALT_TYPE_SPEC:
			replace = nobp_enabled();
			debug = d->spec;
			break;
		default:
			replace = false;
			debug = false;
		}
		if (!replace)
			continue;
		old = (u8 *)&a->instr_offset + a->instr_offset;
		new = (u8 *)&a->repl_offset + a->repl_offset;
		if (debug)
			alternative_dump(old, new, a->instrlen, a->type, a->data);
		s390_kernel_write(old, new, a->instrlen);
	}
}
