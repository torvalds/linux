/******************************************************************************
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _ASM_IA64_PARAVIRT_PRIVOP_H
#define _ASM_IA64_PARAVIRT_PRIVOP_H

#ifdef CONFIG_PARAVIRT

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/kregs.h> /* for IA64_PSR_I */

/******************************************************************************
 * replacement of intrinsics operations.
 */

struct pv_cpu_ops {
	void (*fc)(void *addr);
	unsigned long (*thash)(unsigned long addr);
	unsigned long (*get_cpuid)(int index);
	unsigned long (*get_pmd)(int index);
	unsigned long (*getreg)(int reg);
	void (*setreg)(int reg, unsigned long val);
	void (*ptcga)(unsigned long addr, unsigned long size);
	unsigned long (*get_rr)(unsigned long index);
	void (*set_rr)(unsigned long index, unsigned long val);
	void (*set_rr0_to_rr4)(unsigned long val0, unsigned long val1,
			       unsigned long val2, unsigned long val3,
			       unsigned long val4);
	void (*ssm_i)(void);
	void (*rsm_i)(void);
	unsigned long (*get_psr_i)(void);
	void (*intrin_local_irq_restore)(unsigned long flags);
};

extern struct pv_cpu_ops pv_cpu_ops;

extern void ia64_native_setreg_func(int regnum, unsigned long val);
extern unsigned long ia64_native_getreg_func(int regnum);

/************************************************/
/* Instructions paravirtualized for performance */
/************************************************/

#ifndef ASM_SUPPORTED
#define paravirt_ssm_i()	pv_cpu_ops.ssm_i()
#define paravirt_rsm_i()	pv_cpu_ops.rsm_i()
#define __paravirt_getreg()	pv_cpu_ops.getreg()
#endif

/* mask for ia64_native_ssm/rsm() must be constant.("i" constraing).
 * static inline function doesn't satisfy it. */
#define paravirt_ssm(mask)			\
	do {					\
		if ((mask) == IA64_PSR_I)	\
			paravirt_ssm_i();	\
		else				\
			ia64_native_ssm(mask);	\
	} while (0)

#define paravirt_rsm(mask)			\
	do {					\
		if ((mask) == IA64_PSR_I)	\
			paravirt_rsm_i();	\
		else				\
			ia64_native_rsm(mask);	\
	} while (0)

/* returned ip value should be the one in the caller,
 * not in __paravirt_getreg() */
#define paravirt_getreg(reg)					\
	({							\
		unsigned long res;				\
		if ((reg) == _IA64_REG_IP)			\
			res = ia64_native_getreg(_IA64_REG_IP); \
		else						\
			res = __paravirt_getreg(reg);		\
		res;						\
	})

/******************************************************************************
 * replacement of hand written assembly codes.
 */
struct pv_cpu_asm_switch {
	unsigned long switch_to;
	unsigned long leave_syscall;
	unsigned long work_processed_syscall;
	unsigned long leave_kernel;
};
void paravirt_cpu_asm_init(const struct pv_cpu_asm_switch *cpu_asm_switch);

#endif /* __ASSEMBLY__ */

#define IA64_PARAVIRT_ASM_FUNC(name)	paravirt_ ## name

#else

/* fallback for native case */
#define IA64_PARAVIRT_ASM_FUNC(name)	ia64_native_ ## name

#endif /* CONFIG_PARAVIRT */

#if defined(CONFIG_PARAVIRT) && defined(ASM_SUPPORTED)
#define paravirt_dv_serialize_data()	ia64_dv_serialize_data()
#else
#define paravirt_dv_serialize_data()	/* nothing */
#endif

/* these routines utilize privilege-sensitive or performance-sensitive
 * privileged instructions so the code must be replaced with
 * paravirtualized versions */
#define ia64_switch_to			IA64_PARAVIRT_ASM_FUNC(switch_to)
#define ia64_leave_syscall		IA64_PARAVIRT_ASM_FUNC(leave_syscall)
#define ia64_work_processed_syscall	\
	IA64_PARAVIRT_ASM_FUNC(work_processed_syscall)
#define ia64_leave_kernel		IA64_PARAVIRT_ASM_FUNC(leave_kernel)


#if defined(CONFIG_PARAVIRT)
/******************************************************************************
 * binary patching infrastructure
 */
#define PARAVIRT_PATCH_TYPE_FC				1
#define PARAVIRT_PATCH_TYPE_THASH			2
#define PARAVIRT_PATCH_TYPE_GET_CPUID			3
#define PARAVIRT_PATCH_TYPE_GET_PMD			4
#define PARAVIRT_PATCH_TYPE_PTCGA			5
#define PARAVIRT_PATCH_TYPE_GET_RR			6
#define PARAVIRT_PATCH_TYPE_SET_RR			7
#define PARAVIRT_PATCH_TYPE_SET_RR0_TO_RR4		8
#define PARAVIRT_PATCH_TYPE_SSM_I			9
#define PARAVIRT_PATCH_TYPE_RSM_I			10
#define PARAVIRT_PATCH_TYPE_GET_PSR_I			11
#define PARAVIRT_PATCH_TYPE_INTRIN_LOCAL_IRQ_RESTORE	12

/* PARAVIRT_PATY_TYPE_[GS]ETREG + _IA64_REG_xxx */
#define PARAVIRT_PATCH_TYPE_GETREG			0x10000000
#define PARAVIRT_PATCH_TYPE_SETREG			0x20000000

/*
 * struct task_struct* (*ia64_switch_to)(void* next_task);
 * void *ia64_leave_syscall;
 * void *ia64_work_processed_syscall
 * void *ia64_leave_kernel;
 */

#define PARAVIRT_PATCH_TYPE_BR_START			0x30000000
#define PARAVIRT_PATCH_TYPE_BR_SWITCH_TO		\
	(PARAVIRT_PATCH_TYPE_BR_START + 0)
#define PARAVIRT_PATCH_TYPE_BR_LEAVE_SYSCALL		\
	(PARAVIRT_PATCH_TYPE_BR_START + 1)
#define PARAVIRT_PATCH_TYPE_BR_WORK_PROCESSED_SYSCALL	\
	(PARAVIRT_PATCH_TYPE_BR_START + 2)
#define PARAVIRT_PATCH_TYPE_BR_LEAVE_KERNEL		\
	(PARAVIRT_PATCH_TYPE_BR_START + 3)

#ifdef ASM_SUPPORTED
#include <asm/paravirt_patch.h>

/*
 * pv_cpu_ops calling stub.
 * normal function call convension can't be written by gcc
 * inline assembly.
 *
 * from the caller's point of view,
 * the following registers will be clobbered.
 * r2, r3
 * r8-r15
 * r16, r17
 * b6, b7
 * p6-p15
 * ar.ccv
 *
 * from the callee's point of view ,
 * the following registers can be used.
 * r2, r3: scratch
 * r8: scratch, input argument0 and return value
 * r0-r15: scratch, input argument1-5
 * b6: return pointer
 * b7: scratch
 * p6-p15: scratch
 * ar.ccv: scratch
 *
 * other registers must not be changed. especially
 * b0: rp: preserved. gcc ignores b0 in clobbered register.
 * r16: saved gp
 */
/* 5 bundles */
#define __PARAVIRT_BR							\
	";;\n"								\
	"{ .mlx\n"							\
	"nop 0\n"							\
	"movl r2 = %[op_addr]\n"/* get function pointer address */	\
	";;\n"								\
	"}\n"								\
	"1:\n"								\
	"{ .mii\n"							\
	"ld8 r2 = [r2]\n"	/* load function descriptor address */	\
	"mov r17 = ip\n"	/* get ip to calc return address */	\
	"mov r16 = gp\n"	/* save gp */				\
	";;\n"								\
	"}\n"								\
	"{ .mii\n"							\
	"ld8 r3 = [r2], 8\n"	/* load entry address */		\
	"adds r17 =  1f - 1b, r17\n"	/* calculate return address */	\
	";;\n"								\
	"mov b7 = r3\n"		/* set entry address */			\
	"}\n"								\
	"{ .mib\n"							\
	"ld8 gp = [r2]\n"	/* load gp value */			\
	"mov b6 = r17\n"	/* set return address */		\
	"br.cond.sptk.few b7\n"	/* intrinsics are very short isns */	\
	"}\n"								\
	"1:\n"								\
	"{ .mii\n"							\
	"mov gp = r16\n"	/* restore gp value */			\
	"nop 0\n"							\
	"nop 0\n"							\
	";;\n"								\
	"}\n"

#define PARAVIRT_OP(op)				\
	[op_addr] "i"(&pv_cpu_ops.op)

#define PARAVIRT_TYPE(type)			\
	PARAVIRT_PATCH_TYPE_ ## type

#define PARAVIRT_REG_CLOBBERS0					\
	"r2", "r3", /*"r8",*/ "r9", "r10", "r11", "r14",	\
		"r15", "r16", "r17"

#define PARAVIRT_REG_CLOBBERS1					\
	"r2","r3", /*"r8",*/ "r9", "r10", "r11", "r14",		\
		"r15", "r16", "r17"

#define PARAVIRT_REG_CLOBBERS2					\
	"r2", "r3", /*"r8", "r9",*/ "r10", "r11", "r14",	\
		"r15", "r16", "r17"

#define PARAVIRT_REG_CLOBBERS5					\
	"r2", "r3", /*"r8", "r9", "r10", "r11", "r14",*/	\
		"r15", "r16", "r17"

#define PARAVIRT_BR_CLOBBERS			\
	"b6", "b7"

#define PARAVIRT_PR_CLOBBERS						\
	"p6", "p7", "p8", "p9", "p10", "p11", "p12", "p13", "p14", "p15"

#define PARAVIRT_AR_CLOBBERS			\
	"ar.ccv"

#define PARAVIRT_CLOBBERS0			\
		PARAVIRT_REG_CLOBBERS0,		\
		PARAVIRT_BR_CLOBBERS,		\
		PARAVIRT_PR_CLOBBERS,		\
		PARAVIRT_AR_CLOBBERS,		\
		"memory"

#define PARAVIRT_CLOBBERS1			\
		PARAVIRT_REG_CLOBBERS1,		\
		PARAVIRT_BR_CLOBBERS,		\
		PARAVIRT_PR_CLOBBERS,		\
		PARAVIRT_AR_CLOBBERS,		\
		"memory"

#define PARAVIRT_CLOBBERS2			\
		PARAVIRT_REG_CLOBBERS2,		\
		PARAVIRT_BR_CLOBBERS,		\
		PARAVIRT_PR_CLOBBERS,		\
		PARAVIRT_AR_CLOBBERS,		\
		"memory"

#define PARAVIRT_CLOBBERS5			\
		PARAVIRT_REG_CLOBBERS5,		\
		PARAVIRT_BR_CLOBBERS,		\
		PARAVIRT_PR_CLOBBERS,		\
		PARAVIRT_AR_CLOBBERS,		\
		"memory"

#define PARAVIRT_BR0(op, type)					\
	register unsigned long ia64_clobber asm ("r8");		\
	asm volatile (paravirt_alt_bundle(__PARAVIRT_BR,	\
					  PARAVIRT_TYPE(type))	\
		      :	"=r"(ia64_clobber)			\
		      : PARAVIRT_OP(op)				\
		      : PARAVIRT_CLOBBERS0)

#define PARAVIRT_BR0_RET(op, type)				\
	register unsigned long ia64_intri_res asm ("r8");	\
	asm volatile (paravirt_alt_bundle(__PARAVIRT_BR,	\
					  PARAVIRT_TYPE(type))	\
		      : "=r"(ia64_intri_res)			\
		      : PARAVIRT_OP(op)				\
		      : PARAVIRT_CLOBBERS0)

#define PARAVIRT_BR1(op, type, arg1)				\
	register unsigned long __##arg1 asm ("r8") = arg1;	\
	register unsigned long ia64_clobber asm ("r8");		\
	asm volatile (paravirt_alt_bundle(__PARAVIRT_BR,	\
					  PARAVIRT_TYPE(type))	\
		      :	"=r"(ia64_clobber)			\
		      : PARAVIRT_OP(op), "0"(__##arg1)		\
		      : PARAVIRT_CLOBBERS1)

#define PARAVIRT_BR1_RET(op, type, arg1)			\
	register unsigned long ia64_intri_res asm ("r8");	\
	register unsigned long __##arg1 asm ("r8") = arg1;	\
	asm volatile (paravirt_alt_bundle(__PARAVIRT_BR,	\
					  PARAVIRT_TYPE(type))	\
		      : "=r"(ia64_intri_res)			\
		      : PARAVIRT_OP(op), "0"(__##arg1)		\
		      : PARAVIRT_CLOBBERS1)

#define PARAVIRT_BR1_VOID(op, type, arg1)			\
	register void *__##arg1 asm ("r8") = arg1;		\
	register unsigned long ia64_clobber asm ("r8");		\
	asm volatile (paravirt_alt_bundle(__PARAVIRT_BR,	\
					  PARAVIRT_TYPE(type))	\
		      :	"=r"(ia64_clobber)			\
		      : PARAVIRT_OP(op), "0"(__##arg1)		\
		      : PARAVIRT_CLOBBERS1)

#define PARAVIRT_BR2(op, type, arg1, arg2)				\
	register unsigned long __##arg1 asm ("r8") = arg1;		\
	register unsigned long __##arg2 asm ("r9") = arg2;		\
	register unsigned long ia64_clobber1 asm ("r8");		\
	register unsigned long ia64_clobber2 asm ("r9");		\
	asm volatile (paravirt_alt_bundle(__PARAVIRT_BR,		\
					  PARAVIRT_TYPE(type))		\
		      : "=r"(ia64_clobber1), "=r"(ia64_clobber2)	\
		      : PARAVIRT_OP(op), "0"(__##arg1), "1"(__##arg2)	\
		      : PARAVIRT_CLOBBERS2)


#define PARAVIRT_DEFINE_CPU_OP0(op, type)		\
	static inline void				\
	paravirt_ ## op (void)				\
	{						\
		PARAVIRT_BR0(op, type);			\
	}

#define PARAVIRT_DEFINE_CPU_OP0_RET(op, type)		\
	static inline unsigned long			\
	paravirt_ ## op (void)				\
	{						\
		PARAVIRT_BR0_RET(op, type);		\
		return ia64_intri_res;			\
	}

#define PARAVIRT_DEFINE_CPU_OP1_VOID(op, type)		\
	static inline void				\
	paravirt_ ## op (void *arg1)			\
	{						\
		PARAVIRT_BR1_VOID(op, type, arg1);	\
	}

#define PARAVIRT_DEFINE_CPU_OP1(op, type)		\
	static inline void				\
	paravirt_ ## op (unsigned long arg1)		\
	{						\
		PARAVIRT_BR1(op, type, arg1);		\
	}

#define PARAVIRT_DEFINE_CPU_OP1_RET(op, type)		\
	static inline unsigned long			\
	paravirt_ ## op (unsigned long arg1)		\
	{						\
		PARAVIRT_BR1_RET(op, type, arg1);	\
		return ia64_intri_res;			\
	}

#define PARAVIRT_DEFINE_CPU_OP2(op, type)		\
	static inline void				\
	paravirt_ ## op (unsigned long arg1,		\
			 unsigned long arg2)		\
	{						\
		PARAVIRT_BR2(op, type, arg1, arg2);	\
	}


PARAVIRT_DEFINE_CPU_OP1_VOID(fc, FC);
PARAVIRT_DEFINE_CPU_OP1_RET(thash, THASH)
PARAVIRT_DEFINE_CPU_OP1_RET(get_cpuid, GET_CPUID)
PARAVIRT_DEFINE_CPU_OP1_RET(get_pmd, GET_PMD)
PARAVIRT_DEFINE_CPU_OP2(ptcga, PTCGA)
PARAVIRT_DEFINE_CPU_OP1_RET(get_rr, GET_RR)
PARAVIRT_DEFINE_CPU_OP2(set_rr, SET_RR)
PARAVIRT_DEFINE_CPU_OP0(ssm_i, SSM_I)
PARAVIRT_DEFINE_CPU_OP0(rsm_i, RSM_I)
PARAVIRT_DEFINE_CPU_OP0_RET(get_psr_i, GET_PSR_I)
PARAVIRT_DEFINE_CPU_OP1(intrin_local_irq_restore, INTRIN_LOCAL_IRQ_RESTORE)

static inline void
paravirt_set_rr0_to_rr4(unsigned long val0, unsigned long val1,
			unsigned long val2, unsigned long val3,
			unsigned long val4)
{
	register unsigned long __val0 asm ("r8") = val0;
	register unsigned long __val1 asm ("r9") = val1;
	register unsigned long __val2 asm ("r10") = val2;
	register unsigned long __val3 asm ("r11") = val3;
	register unsigned long __val4 asm ("r14") = val4;

	register unsigned long ia64_clobber0 asm ("r8");
	register unsigned long ia64_clobber1 asm ("r9");
	register unsigned long ia64_clobber2 asm ("r10");
	register unsigned long ia64_clobber3 asm ("r11");
	register unsigned long ia64_clobber4 asm ("r14");

	asm volatile (paravirt_alt_bundle(__PARAVIRT_BR,
					  PARAVIRT_TYPE(SET_RR0_TO_RR4))
		      : "=r"(ia64_clobber0),
			"=r"(ia64_clobber1),
			"=r"(ia64_clobber2),
			"=r"(ia64_clobber3),
			"=r"(ia64_clobber4)
		      : PARAVIRT_OP(set_rr0_to_rr4),
			"0"(__val0), "1"(__val1), "2"(__val2),
			"3"(__val3), "4"(__val4)
		      : PARAVIRT_CLOBBERS5);
}

/* unsigned long paravirt_getreg(int reg) */
#define __paravirt_getreg(reg)						\
	({								\
		register unsigned long ia64_intri_res asm ("r8");	\
		register unsigned long __reg asm ("r8") = (reg);	\
									\
		BUILD_BUG_ON(!__builtin_constant_p(reg));		\
		asm volatile (paravirt_alt_bundle(__PARAVIRT_BR,	\
						  PARAVIRT_TYPE(GETREG) \
						  + (reg))		\
			      : "=r"(ia64_intri_res)			\
			      : PARAVIRT_OP(getreg), "0"(__reg)		\
			      : PARAVIRT_CLOBBERS1);			\
									\
		ia64_intri_res;						\
	})

/* void paravirt_setreg(int reg, unsigned long val) */
#define paravirt_setreg(reg, val)					\
	do {								\
		register unsigned long __val asm ("r8") = val;		\
		register unsigned long __reg asm ("r9") = reg;		\
		register unsigned long ia64_clobber1 asm ("r8");	\
		register unsigned long ia64_clobber2 asm ("r9");	\
									\
		BUILD_BUG_ON(!__builtin_constant_p(reg));		\
		asm volatile (paravirt_alt_bundle(__PARAVIRT_BR,	\
						  PARAVIRT_TYPE(SETREG) \
						  + (reg))		\
			      : "=r"(ia64_clobber1),			\
				"=r"(ia64_clobber2)			\
			      : PARAVIRT_OP(setreg),			\
				"1"(__reg), "0"(__val)			\
			      : PARAVIRT_CLOBBERS2);			\
	} while (0)

#endif /* ASM_SUPPORTED */
#endif /* CONFIG_PARAVIRT && ASM_SUPPOTED */

#endif /* _ASM_IA64_PARAVIRT_PRIVOP_H */
