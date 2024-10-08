/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm64/include/asm/probes.h
 *
 * Copyright (C) 2013 Linaro Limited
 */
#ifndef _ARM_PROBES_H
#define _ARM_PROBES_H

#include <asm/insn.h>

typedef void (probes_handler_t) (u32 opcode, long addr, struct pt_regs *);

struct arch_probe_insn {
	probes_handler_t *handler;
};
#ifdef CONFIG_KPROBES
typedef __le32 kprobe_opcode_t;
struct arch_specific_insn {
	struct arch_probe_insn api;
	kprobe_opcode_t *xol_insn;
	/* restore address after step xol */
	unsigned long xol_restore;
};
#endif

#endif
