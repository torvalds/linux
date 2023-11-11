/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_RISCV_PROBES_H
#define _ASM_RISCV_PROBES_H

typedef u32 probe_opcode_t;
typedef bool (probes_handler_t) (u32 opcode, unsigned long addr, struct pt_regs *);

/* architecture specific copy of original instruction */
struct arch_probe_insn {
	probe_opcode_t *insn;
	probes_handler_t *handler;
	/* restore address after simulation */
	unsigned long restore;
};

#ifdef CONFIG_KPROBES
typedef u32 kprobe_opcode_t;
struct arch_specific_insn {
	struct arch_probe_insn api;
};
#endif

#endif /* _ASM_RISCV_PROBES_H */
