/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_ARCREGS_H
#define _ASM_ARC_ARCREGS_H

#ifdef __KERNEL__

/* Build Configuration Registers */
#define ARC_REG_VECBASE_BCR	0x68

/* status32 Bits Positions */
#define STATUS_H_BIT		0	/* CPU Halted */
#define STATUS_E1_BIT		1	/* Int 1 enable */
#define STATUS_E2_BIT		2	/* Int 2 enable */
#define STATUS_A1_BIT		3	/* Int 1 active */
#define STATUS_A2_BIT		4	/* Int 2 active */
#define STATUS_AE_BIT		5	/* Exception active */
#define STATUS_DE_BIT		6	/* PC is in delay slot */
#define STATUS_U_BIT		7	/* User/Kernel mode */
#define STATUS_L_BIT		12	/* Loop inhibit */

/* These masks correspond to the status word(STATUS_32) bits */
#define STATUS_H_MASK		(1<<STATUS_H_BIT)
#define STATUS_E1_MASK		(1<<STATUS_E1_BIT)
#define STATUS_E2_MASK		(1<<STATUS_E2_BIT)
#define STATUS_A1_MASK		(1<<STATUS_A1_BIT)
#define STATUS_A2_MASK		(1<<STATUS_A2_BIT)
#define STATUS_AE_MASK		(1<<STATUS_AE_BIT)
#define STATUS_DE_MASK		(1<<STATUS_DE_BIT)
#define STATUS_U_MASK		(1<<STATUS_U_BIT)
#define STATUS_L_MASK		(1<<STATUS_L_BIT)

/* Auxiliary registers */
#define AUX_IDENTITY		4
#define AUX_INTR_VEC_BASE	0x25
#define AUX_IRQ_LEV		0x200	/* IRQ Priority: L1 or L2 */
#define AUX_IRQ_HINT		0x201	/* For generating Soft Interrupts */
#define AUX_IRQ_LV12		0x43	/* interrupt level register */

#define AUX_IENABLE		0x40c
#define AUX_ITRIGGER		0x40d
#define AUX_IPULSE		0x415

/* Timer related Aux registers */
#define ARC_REG_TIMER0_LIMIT	0x23	/* timer 0 limit */
#define ARC_REG_TIMER0_CTRL	0x22	/* timer 0 control */
#define ARC_REG_TIMER0_CNT	0x21	/* timer 0 count */
#define ARC_REG_TIMER1_LIMIT	0x102	/* timer 1 limit */
#define ARC_REG_TIMER1_CTRL	0x101	/* timer 1 control */
#define ARC_REG_TIMER1_CNT	0x100	/* timer 1 count */

#define TIMER_CTRL_IE		(1 << 0) /* Interupt when Count reachs limit */
#define TIMER_CTRL_NH		(1 << 1) /* Count only when CPU NOT halted */

/*
 * Floating Pt Registers
 * Status regs are read-only (build-time) so need not be saved/restored
 */
#define ARC_AUX_FP_STAT         0x300
#define ARC_AUX_DPFP_1L         0x301
#define ARC_AUX_DPFP_1H         0x302
#define ARC_AUX_DPFP_2L         0x303
#define ARC_AUX_DPFP_2H         0x304
#define ARC_AUX_DPFP_STAT       0x305

#ifndef __ASSEMBLY__

/*
 ******************************************************************
 *      Inline ASM macros to read/write AUX Regs
 *      Essentially invocation of lr/sr insns from "C"
 */

#if 1

#define read_aux_reg(reg)	__builtin_arc_lr(reg)

/* gcc builtin sr needs reg param to be long immediate */
#define write_aux_reg(reg_immed, val)		\
		__builtin_arc_sr((unsigned int)val, reg_immed)

#else

#define read_aux_reg(reg)		\
({					\
	unsigned int __ret;		\
	__asm__ __volatile__(		\
	"	lr    %0, [%1]"		\
	: "=r"(__ret)			\
	: "i"(reg));			\
	__ret;				\
})

/*
 * Aux Reg address is specified as long immediate by caller
 * e.g.
 *    write_aux_reg(0x69, some_val);
 * This generates tightest code.
 */
#define write_aux_reg(reg_imm, val)	\
({					\
	__asm__ __volatile__(		\
	"	sr   %0, [%1]	\n"	\
	:				\
	: "ir"(val), "i"(reg_imm));	\
})

/*
 * Aux Reg address is specified in a variable
 *  * e.g.
 *      reg_num = 0x69
 *      write_aux_reg2(reg_num, some_val);
 * This has to generate glue code to load the reg num from
 *  memory to a reg hence not recommended.
 */
#define write_aux_reg2(reg_in_var, val)		\
({						\
	unsigned int tmp;			\
						\
	__asm__ __volatile__(			\
	"	ld   %0, [%2]	\n\t"		\
	"	sr   %1, [%0]	\n\t"		\
	: "=&r"(tmp)				\
	: "r"(val), "memory"(&reg_in_var));	\
})

#endif

#ifdef CONFIG_ARC_FPU_SAVE_RESTORE
/* These DPFP regs need to be saved/restored across ctx-sw */
struct arc_fpu {
	struct {
		unsigned int l, h;
	} aux_dpfp[2];
};
#endif

#endif /* __ASEMBLY__ */

#endif /* __KERNEL__ */

#endif /* _ASM_ARC_ARCREGS_H */
