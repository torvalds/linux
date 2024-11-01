/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _RISCV_KERNEL_KPROBES_DECODE_INSN_H
#define _RISCV_KERNEL_KPROBES_DECODE_INSN_H

#include <asm/sections.h>
#include <asm/kprobes.h>

enum probe_insn {
	INSN_REJECTED,
	INSN_GOOD_NO_SLOT,
	INSN_GOOD,
};

enum probe_insn __kprobes
riscv_probe_decode_insn(probe_opcode_t *addr, struct arch_probe_insn *asi);

#endif /* _RISCV_KERNEL_KPROBES_DECODE_INSN_H */
