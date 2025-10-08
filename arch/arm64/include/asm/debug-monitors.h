/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_DEBUG_MONITORS_H
#define __ASM_DEBUG_MONITORS_H

#include <linux/errno.h>
#include <linux/types.h>
#include <asm/brk-imm.h>
#include <asm/esr.h>
#include <asm/insn.h>
#include <asm/ptrace.h>

/* Low-level stepping controls. */
#define DBG_SPSR_SS		(1 << 21)

#define	DBG_ESR_EVT(x)		(((x) >> 27) & 0x7)

/* AArch64 */
#define DBG_ESR_EVT_HWBP	0x0
#define DBG_ESR_EVT_HWSS	0x1
#define DBG_ESR_EVT_HWWP	0x2
#define DBG_ESR_EVT_BRK		0x6

/*
 * Break point instruction encoding
 */
#define BREAK_INSTR_SIZE		AARCH64_INSN_SIZE

#define AARCH64_BREAK_KGDB_DYN_DBG	\
	(AARCH64_BREAK_MON | (KGDB_DYN_DBG_BRK_IMM << 5))

#define CACHE_FLUSH_IS_SAFE		1

/* kprobes BRK opcodes with ESR encoding  */
#define BRK64_OPCODE_KPROBES	(AARCH64_BREAK_MON | (KPROBES_BRK_IMM << 5))
#define BRK64_OPCODE_KPROBES_SS	(AARCH64_BREAK_MON | (KPROBES_BRK_SS_IMM << 5))
/* uprobes BRK opcodes with ESR encoding  */
#define BRK64_OPCODE_UPROBES	(AARCH64_BREAK_MON | (UPROBES_BRK_IMM << 5))

/* AArch32 */
#define DBG_ESR_EVT_BKPT	0x4
#define DBG_ESR_EVT_VECC	0x5

#define AARCH32_BREAK_ARM	0x07f001f0
#define AARCH32_BREAK_THUMB	0xde01
#define AARCH32_BREAK_THUMB2_LO	0xf7f0
#define AARCH32_BREAK_THUMB2_HI	0xa000

#ifndef __ASSEMBLY__
struct task_struct;

#define DBG_ARCH_ID_RESERVED	0	/* In case of ptrace ABI updates. */

#define DBG_HOOK_HANDLED	0
#define DBG_HOOK_ERROR		1

u8 debug_monitors_arch(void);

enum dbg_active_el {
	DBG_ACTIVE_EL0 = 0,
	DBG_ACTIVE_EL1,
};

void enable_debug_monitors(enum dbg_active_el el);
void disable_debug_monitors(enum dbg_active_el el);

void user_rewind_single_step(struct task_struct *task);
void user_fastforward_single_step(struct task_struct *task);
void user_regs_reset_single_step(struct user_pt_regs *regs,
				 struct task_struct *task);

void kernel_enable_single_step(struct pt_regs *regs);
void kernel_disable_single_step(void);
int kernel_active_single_step(void);
void kernel_rewind_single_step(struct pt_regs *regs);
void kernel_fastforward_single_step(struct pt_regs *regs);

#ifdef CONFIG_HAVE_HW_BREAKPOINT
bool try_step_suspended_breakpoints(struct pt_regs *regs);
#else
static inline bool try_step_suspended_breakpoints(struct pt_regs *regs)
{
	return false;
}
#endif

bool try_handle_aarch32_break(struct pt_regs *regs);

#endif	/* __ASSEMBLY */
#endif	/* __ASM_DEBUG_MONITORS_H */
