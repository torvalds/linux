// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *
 * Floating-point emulation code
 *  Copyright (C) 2001 Hewlett-Packard (Paul Bame) <bame@debian.org>
 */
/*
 *  linux/arch/math-emu/driver.c.c
 *
 *	decodes and dispatches unimplemented FPU instructions
 *
 *  Copyright (C) 1999, 2000  Philipp Rumpf <prumpf@tux.org>
 *  Copyright (C) 2001	      Hewlett-Packard <bame@debian.org>
 */

#include <linux/sched/signal.h>

#include "float.h"
#include "math-emu.h"


#define fptpos 31
#define fpr1pos 10
#define extru(r,pos,len) (((r) >> (31-(pos))) & (( 1 << (len)) - 1))

#define FPUDEBUG 0

/* Format of the floating-point exception registers. */
struct exc_reg {
	unsigned int exception : 6;
	unsigned int ei : 26;
};

/* Macros for grabbing bits of the instruction format from the 'ei'
   field above. */
/* Major opcode 0c and 0e */
#define FP0CE_UID(i) (((i) >> 6) & 3)
#define FP0CE_CLASS(i) (((i) >> 9) & 3)
#define FP0CE_SUBOP(i) (((i) >> 13) & 7)
#define FP0CE_SUBOP1(i) (((i) >> 15) & 7) /* Class 1 subopcode */
#define FP0C_FORMAT(i) (((i) >> 11) & 3)
#define FP0E_FORMAT(i) (((i) >> 11) & 1)

/* Major opcode 0c, uid 2 (performance monitoring) */
#define FPPM_SUBOP(i) (((i) >> 9) & 0x1f)

/* Major opcode 2e (fused operations).   */
#define FP2E_SUBOP(i)  (((i) >> 5) & 1)
#define FP2E_FORMAT(i) (((i) >> 11) & 1)

/* Major opcode 26 (FMPYSUB) */
/* Major opcode 06 (FMPYADD) */
#define FPx6_FORMAT(i) ((i) & 0x1f)

/* Flags and enable bits of the status word. */
#define FPSW_FLAGS(w) ((w) >> 27)
#define FPSW_ENABLE(w) ((w) & 0x1f)
#define FPSW_V (1<<4)
#define FPSW_Z (1<<3)
#define FPSW_O (1<<2)
#define FPSW_U (1<<1)
#define FPSW_I (1<<0)

/* Handle a floating point exception.  Return zero if the faulting
   instruction can be completed successfully. */
int
handle_fpe(struct pt_regs *regs)
{
	extern void printbinary(unsigned long x, int nbits);
	unsigned int orig_sw, sw;
	int signalcode;
	/* need an intermediate copy of float regs because FPU emulation
	 * code expects an artificial last entry which contains zero
	 *
	 * also, the passed in fr registers contain one word that defines
	 * the fpu type. the fpu type information is constructed 
	 * inside the emulation code
	 */
	__u64 frcopy[36];

	memcpy(frcopy, regs->fr, sizeof regs->fr);
	frcopy[32] = 0;

	memcpy(&orig_sw, frcopy, sizeof(orig_sw));

	if (FPUDEBUG) {
		printk(KERN_DEBUG "FP VZOUICxxxxCQCQCQCQCQCRMxxTDVZOUI ->\n   ");
		printbinary(orig_sw, 32);
		printk(KERN_DEBUG "\n");
	}

	signalcode = decode_fpu(frcopy, 0x666);

	/* Status word = FR0L. */
	memcpy(&sw, frcopy, sizeof(sw));
	if (FPUDEBUG) {
		printk(KERN_DEBUG "VZOUICxxxxCQCQCQCQCQCRMxxTDVZOUI decode_fpu returns %d|0x%x\n",
			signalcode >> 24, signalcode & 0xffffff);
		printbinary(sw, 32);
		printk(KERN_DEBUG "\n");
	}

	memcpy(regs->fr, frcopy, sizeof regs->fr);
	if (signalcode != 0) {
	    force_sig_fault(signalcode >> 24, signalcode & 0xffffff,
			    (void __user *) regs->iaoq[0]);
	    return -1;
	}

	return signalcode ? -1 : 0;
}
