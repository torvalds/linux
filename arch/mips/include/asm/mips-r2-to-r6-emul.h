/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2014 Imagination Technologies Ltd.
 * Author: Markos Chandras <markos.chandras@imgtec.com>
 */

#ifndef __ASM_MIPS_R2_TO_R6_EMUL_H
#define __ASM_MIPS_R2_TO_R6_EMUL_H

struct mips_r2_emulator_stats {
	u64 movs;
	u64 hilo;
	u64 muls;
	u64 divs;
	u64 dsps;
	u64 bops;
	u64 traps;
	u64 fpus;
	u64 loads;
	u64 stores;
	u64 llsc;
	u64 dsemul;
};

struct mips_r2br_emulator_stats {
	u64 jrs;
	u64 bltzl;
	u64 bgezl;
	u64 bltzll;
	u64 bgezll;
	u64 bltzall;
	u64 bgezall;
	u64 bltzal;
	u64 bgezal;
	u64 beql;
	u64 bnel;
	u64 blezl;
	u64 bgtzl;
};

#ifdef CONFIG_DEBUG_FS

#define MIPS_R2_STATS(M)						\
do {									\
	u32 nir;							\
	int err;							\
									\
	preempt_disable();						\
	__this_cpu_inc(mipsr2emustats.M);				\
	err = __get_user(nir, (u32 __user *)regs->cp0_epc);		\
	if (!err) {							\
		if (nir == BREAK_MATH)					\
			__this_cpu_inc(mipsr2bdemustats.M);		\
	}								\
	preempt_enable();						\
} while (0)

#define MIPS_R2BR_STATS(M)					\
do {								\
	preempt_disable();					\
	__this_cpu_inc(mipsr2bremustats.M);			\
	preempt_enable();					\
} while (0)

#else

#define MIPS_R2_STATS(M)          do { } while (0)
#define MIPS_R2BR_STATS(M)        do { } while (0)

#endif /* CONFIG_DEBUG_FS */

struct r2_decoder_table {
	u32     mask;
	u32     code;
	int     (*func)(struct pt_regs *regs, u32 inst);
};


extern void do_trap_or_bp(struct pt_regs *regs, unsigned int code,
			  const char *str);

#ifndef CONFIG_MIPSR2_TO_R6_EMULATOR
static int mipsr2_emulation;
static __maybe_unused int mipsr2_decoder(struct pt_regs *regs, u32 inst) { return 0; };
#else
/* MIPS R2 Emulator ON/OFF */
extern int mipsr2_emulation;
extern int mipsr2_decoder(struct pt_regs *regs, u32 inst);
#endif /* CONFIG_MIPSR2_TO_R6_EMULATOR */

#define NO_R6EMU	(cpu_has_mips_r6 && !mipsr2_emulation)

#endif /* __ASM_MIPS_R2_TO_R6_EMUL_H */
