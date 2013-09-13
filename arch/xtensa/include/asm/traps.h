/*
 * arch/xtensa/include/asm/traps.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 Tensilica Inc.
 */
#ifndef _XTENSA_TRAPS_H
#define _XTENSA_TRAPS_H

#include <asm/ptrace.h>

/*
 * handler must be either of the following:
 *  void (*)(struct pt_regs *regs);
 *  void (*)(struct pt_regs *regs, unsigned long exccause);
 */
extern void * __init trap_set_handler(int cause, void *handler);
extern void do_unhandled(struct pt_regs *regs, unsigned long exccause);

static inline void spill_registers(void)
{

	__asm__ __volatile__ (
		"movi	a14, "__stringify((1 << PS_EXCM_BIT) | LOCKLEVEL)"\n\t"
		"mov	a12, a0\n\t"
		"rsr	a13, sar\n\t"
		"xsr	a14, ps\n\t"
		"movi	a0, _spill_registers\n\t"
		"rsync\n\t"
		"callx0 a0\n\t"
		"mov	a0, a12\n\t"
		"wsr	a13, sar\n\t"
		"wsr	a14, ps\n\t"
		: :
#if defined(CONFIG_FRAME_POINTER)
		: "a2", "a3", "a4",       "a11", "a12", "a13", "a14", "a15",
#else
		: "a2", "a3", "a4", "a7", "a11", "a12", "a13", "a14", "a15",
#endif
		  "memory");
}

#endif /* _XTENSA_TRAPS_H */
