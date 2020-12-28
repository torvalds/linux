/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm64/include/asm/probes.h
 *
 * Copyright (C) 2013 Linaro Limited
 */
#ifndef _ARM_PROBES_H
#define _ARM_PROBES_H

#include <asm/insn.h>

typedef u32 probe_opcode_t;
typedef void (probes_handler_t) (u32 opcode, long addr, struct pt_regs *);

/* architecture specific copy of original instruction */
struct arch_probe_insn {
	probe_opcode_t *insn;
	pstate_check_t *pstate_cc;
	probes_handler_t *handler;
	/* restore address after step xol */
	unsigned long restore;
};
#ifdef CONFIG_KPROBES
typedef u32 kprobe_opcode_t;
struct arch_specific_insn {
	struct arch_probe_insn api;
};
#endif

#endif
