/*
 * linux/arch/unicore32/include/asm/ptrace.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_PTRACE_H__
#define __UNICORE_PTRACE_H__

#define PTRACE_GET_THREAD_AREA	22

/*
 * PSR bits
 */
#define USER_MODE	0x00000010
#define REAL_MODE	0x00000011
#define INTR_MODE	0x00000012
#define PRIV_MODE	0x00000013
#define ABRT_MODE	0x00000017
#define EXTN_MODE	0x0000001b
#define SUSR_MODE	0x0000001f
#define MODE_MASK	0x0000001f
#define PSR_R_BIT	0x00000040
#define PSR_I_BIT	0x00000080
#define PSR_V_BIT	0x10000000
#define PSR_C_BIT	0x20000000
#define PSR_Z_BIT	0x40000000
#define PSR_S_BIT	0x80000000

/*
 * Groups of PSR bits
 */
#define PSR_f		0xff000000	/* Flags		*/
#define PSR_c		0x000000ff	/* Control		*/

#ifndef __ASSEMBLY__

/*
 * This struct defines the way the registers are stored on the
 * stack during a system call.  Note that sizeof(struct pt_regs)
 * has to be a multiple of 8.
 */
struct pt_regs {
	unsigned long uregs[34];
};

#define UCreg_asr		uregs[32]
#define UCreg_pc		uregs[31]
#define UCreg_lr		uregs[30]
#define UCreg_sp		uregs[29]
#define UCreg_ip		uregs[28]
#define UCreg_fp		uregs[27]
#define UCreg_26		uregs[26]
#define UCreg_25		uregs[25]
#define UCreg_24		uregs[24]
#define UCreg_23		uregs[23]
#define UCreg_22		uregs[22]
#define UCreg_21		uregs[21]
#define UCreg_20		uregs[20]
#define UCreg_19		uregs[19]
#define UCreg_18		uregs[18]
#define UCreg_17		uregs[17]
#define UCreg_16		uregs[16]
#define UCreg_15		uregs[15]
#define UCreg_14		uregs[14]
#define UCreg_13		uregs[13]
#define UCreg_12		uregs[12]
#define UCreg_11		uregs[11]
#define UCreg_10		uregs[10]
#define UCreg_09		uregs[9]
#define UCreg_08		uregs[8]
#define UCreg_07		uregs[7]
#define UCreg_06		uregs[6]
#define UCreg_05		uregs[5]
#define UCreg_04		uregs[4]
#define UCreg_03		uregs[3]
#define UCreg_02		uregs[2]
#define UCreg_01		uregs[1]
#define UCreg_00		uregs[0]
#define UCreg_ORIG_00		uregs[33]

#ifdef __KERNEL__

#define user_mode(regs)	\
	(processor_mode(regs) == USER_MODE)

#define processor_mode(regs) \
	((regs)->UCreg_asr & MODE_MASK)

#define interrupts_enabled(regs) \
	(!((regs)->UCreg_asr & PSR_I_BIT))

#define fast_interrupts_enabled(regs) \
	(!((regs)->UCreg_asr & PSR_R_BIT))

/* Are the current registers suitable for user mode?
 * (used to maintain security in signal handlers)
 */
static inline int valid_user_regs(struct pt_regs *regs)
{
	unsigned long mode = regs->UCreg_asr & MODE_MASK;

	/*
	 * Always clear the R (REAL) bits
	 */
	regs->UCreg_asr &= ~(PSR_R_BIT);

	if ((regs->UCreg_asr & PSR_I_BIT) == 0) {
		if (mode == USER_MODE)
			return 1;
	}

	/*
	 * Force ASR to something logical...
	 */
	regs->UCreg_asr &= PSR_f | USER_MODE;

	return 0;
}

#define instruction_pointer(regs)	((regs)->UCreg_pc)

#endif /* __KERNEL__ */

#endif /* __ASSEMBLY__ */

#endif

