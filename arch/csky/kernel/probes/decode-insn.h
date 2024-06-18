/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __CSKY_KERNEL_KPROBES_DECODE_INSN_H
#define __CSKY_KERNEL_KPROBES_DECODE_INSN_H

#include <asm/sections.h>
#include <asm/kprobes.h>

enum probe_insn {
	INSN_REJECTED,
	INSN_GOOD_NO_SLOT,
	INSN_GOOD,
};

#define is_insn32(insn) ((insn & 0xc000) == 0xc000)

enum probe_insn __kprobes
csky_probe_decode_insn(probe_opcode_t *addr, struct arch_probe_insn *asi);

#endif /* __CSKY_KERNEL_KPROBES_DECODE_INSN_H */
