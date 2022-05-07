// SPDX-License-Identifier: GPL-2.0+

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <asm/sections.h>

#include "decode-insn.h"
#include "simulate-insn.h"

/* Return:
 *   INSN_REJECTED     If instruction is one not allowed to kprobe,
 *   INSN_GOOD_NO_SLOT If instruction is supported but doesn't use its slot.
 */
enum probe_insn __kprobes
csky_probe_decode_insn(probe_opcode_t *addr, struct arch_probe_insn *api)
{
	probe_opcode_t insn = le32_to_cpu(*addr);

	CSKY_INSN_SET_SIMULATE(br16,		insn);
	CSKY_INSN_SET_SIMULATE(bt16,		insn);
	CSKY_INSN_SET_SIMULATE(bf16,		insn);
	CSKY_INSN_SET_SIMULATE(jmp16,		insn);
	CSKY_INSN_SET_SIMULATE(jsr16,		insn);
	CSKY_INSN_SET_SIMULATE(lrw16,		insn);
	CSKY_INSN_SET_SIMULATE(pop16,		insn);

	CSKY_INSN_SET_SIMULATE(br32,		insn);
	CSKY_INSN_SET_SIMULATE(bt32,		insn);
	CSKY_INSN_SET_SIMULATE(bf32,		insn);
	CSKY_INSN_SET_SIMULATE(jmp32,		insn);
	CSKY_INSN_SET_SIMULATE(jsr32,		insn);
	CSKY_INSN_SET_SIMULATE(lrw32,		insn);
	CSKY_INSN_SET_SIMULATE(pop32,		insn);

	CSKY_INSN_SET_SIMULATE(bez32,		insn);
	CSKY_INSN_SET_SIMULATE(bnez32,		insn);
	CSKY_INSN_SET_SIMULATE(bnezad32,	insn);
	CSKY_INSN_SET_SIMULATE(bhsz32,		insn);
	CSKY_INSN_SET_SIMULATE(bhz32,		insn);
	CSKY_INSN_SET_SIMULATE(blsz32,		insn);
	CSKY_INSN_SET_SIMULATE(blz32,		insn);
	CSKY_INSN_SET_SIMULATE(bsr32,		insn);
	CSKY_INSN_SET_SIMULATE(jmpi32,		insn);
	CSKY_INSN_SET_SIMULATE(jsri32,		insn);

	return INSN_GOOD;
}
