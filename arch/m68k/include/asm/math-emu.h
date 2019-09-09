/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_M68K_SETUP_H
#define _ASM_M68K_SETUP_H

#include <asm/setup.h>
#include <linux/linkage.h>

/* Status Register bits */

/* accrued exception bits */
#define FPSR_AEXC_INEX	3
#define FPSR_AEXC_DZ	4
#define FPSR_AEXC_UNFL	5
#define FPSR_AEXC_OVFL	6
#define FPSR_AEXC_IOP	7

/* exception status bits */
#define FPSR_EXC_INEX1	8
#define FPSR_EXC_INEX2	9
#define FPSR_EXC_DZ	10
#define FPSR_EXC_UNFL	11
#define FPSR_EXC_OVFL	12
#define FPSR_EXC_OPERR	13
#define FPSR_EXC_SNAN	14
#define FPSR_EXC_BSUN	15

/* quotient byte, assumes big-endian, of course */
#define FPSR_QUOTIENT(fpsr) (*((signed char *) &(fpsr) + 1))

/* condition code bits */
#define FPSR_CC_NAN	24
#define FPSR_CC_INF	25
#define FPSR_CC_Z	26
#define FPSR_CC_NEG	27


/* Control register bits */

/* rounding mode */
#define	FPCR_ROUND_RN	0		/* round to nearest/even */
#define FPCR_ROUND_RZ	1		/* round to zero */
#define FPCR_ROUND_RM	2		/* minus infinity */
#define FPCR_ROUND_RP	3		/* plus infinity */

/* rounding precision */
#define FPCR_PRECISION_X	0	/* long double */
#define FPCR_PRECISION_S	1	/* double */
#define FPCR_PRECISION_D	2	/* float */


/* Flags to select the debugging output */
#define PDECODE		0
#define PEXECUTE	1
#define PCONV		2
#define PNORM		3
#define PREGISTER	4
#define PINSTR		5
#define PUNIMPL		6
#define PMOVEM		7

#define PMDECODE	(1<<PDECODE)
#define PMEXECUTE	(1<<PEXECUTE)
#define PMCONV		(1<<PCONV)
#define PMNORM		(1<<PNORM)
#define PMREGISTER	(1<<PREGISTER)
#define PMINSTR		(1<<PINSTR)
#define PMUNIMPL	(1<<PUNIMPL)
#define PMMOVEM		(1<<PMOVEM)

#ifndef __ASSEMBLY__

#include <linux/kernel.h>
#include <linux/sched.h>

union fp_mant64 {
	unsigned long long m64;
	unsigned long m32[2];
};

union fp_mant128 {
	unsigned long long m64[2];
	unsigned long m32[4];
};

/* internal representation of extended fp numbers */
struct fp_ext {
	unsigned char lowmant;
	unsigned char sign;
	unsigned short exp;
	union fp_mant64 mant;
};

/* C representation of FPU registers */
/* NOTE: if you change this, you have to change the assembler offsets
   below and the size in <asm/fpu.h>, too */
struct fp_data {
	struct fp_ext fpreg[8];
	unsigned int fpcr;
	unsigned int fpsr;
	unsigned int fpiar;
	unsigned short prec;
	unsigned short rnd;
	struct fp_ext temp[2];
};

#ifdef FPU_EMU_DEBUG
extern unsigned int fp_debugprint;

#define dprint(bit, fmt, ...) ({			\
	if (fp_debugprint & (1 << (bit)))		\
		pr_info(fmt, ##__VA_ARGS__);		\
})
#else
#define dprint(bit, fmt, ...)	no_printk(fmt, ##__VA_ARGS__)
#endif

#define uprint(str) ({					\
	static int __count = 3;				\
							\
	if (__count > 0) {				\
		pr_err("You just hit an unimplemented "	\
		       "fpu instruction (%s)\n", str);	\
		pr_err("Please report this to ....\n");	\
		__count--;				\
	}						\
})

#define FPDATA		((struct fp_data *)current->thread.fp)

#else	/* __ASSEMBLY__ */

#define FPDATA		%a2

/* offsets from the base register to the floating point data in the task struct */
#define FPD_FPREG	(TASK_THREAD+THREAD_FPREG+0)
#define FPD_FPCR	(TASK_THREAD+THREAD_FPREG+96)
#define FPD_FPSR	(TASK_THREAD+THREAD_FPREG+100)
#define FPD_FPIAR	(TASK_THREAD+THREAD_FPREG+104)
#define FPD_PREC	(TASK_THREAD+THREAD_FPREG+108)
#define FPD_RND		(TASK_THREAD+THREAD_FPREG+110)
#define FPD_TEMPFP1	(TASK_THREAD+THREAD_FPREG+112)
#define FPD_TEMPFP2	(TASK_THREAD+THREAD_FPREG+124)
#define FPD_SIZEOF	(TASK_THREAD+THREAD_FPREG+136)

/* offsets on the stack to access saved registers,
 * these are only used during instruction decoding
 * where we always know how deep we're on the stack.
 */
#define FPS_DO		(PT_OFF_D0)
#define FPS_D1		(PT_OFF_D1)
#define FPS_D2		(PT_OFF_D2)
#define FPS_A0		(PT_OFF_A0)
#define FPS_A1		(PT_OFF_A1)
#define FPS_A2		(PT_OFF_A2)
#define FPS_SR		(PT_OFF_SR)
#define FPS_PC		(PT_OFF_PC)
#define FPS_EA		(PT_OFF_PC+6)
#define FPS_PC2		(PT_OFF_PC+10)

.macro	fp_get_fp_reg
	lea	(FPD_FPREG,FPDATA,%d0.w*4),%a0
	lea	(%a0,%d0.w*8),%a0
.endm

/* Macros used to get/put the current program counter.
 * 020/030 use a different stack frame then 040/060, for the
 * 040/060 the return pc points already to the next location,
 * so this only needs to be modified for jump instructions.
 */
.macro	fp_get_pc dest
	move.l	(FPS_PC+4,%sp),\dest
.endm

.macro	fp_put_pc src,jump=0
	move.l	\src,(FPS_PC+4,%sp)
.endm

.macro	fp_get_instr_data	f,s,dest,label
	getuser	\f,%sp@(FPS_PC+4)@(0),\dest,\label,%sp@(FPS_PC+4)
	addq.l	#\s,%sp@(FPS_PC+4)
.endm

.macro	fp_get_instr_word	dest,label,addr
	fp_get_instr_data	w,2,\dest,\label,\addr
.endm

.macro	fp_get_instr_long	dest,label,addr
	fp_get_instr_data	l,4,\dest,\label,\addr
.endm

/* These macros are used to read from/write to user space
 * on error we jump to the fixup section, load the fault
 * address into %a0 and jump to the exit.
 * (derived from <asm/uaccess.h>)
 */
.macro	getuser	size,src,dest,label,addr
|	printf	,"[\size<%08x]",1,\addr
.Lu1\@:	moves\size	\src,\dest

	.section .fixup,"ax"
	.even
.Lu2\@:	move.l	\addr,%a0
	jra	\label
	.previous

	.section __ex_table,"a"
	.align	4
	.long	.Lu1\@,.Lu2\@
	.previous
.endm

.macro	putuser	size,src,dest,label,addr
|	printf	,"[\size>%08x]",1,\addr
.Lu1\@:	moves\size	\src,\dest
.Lu2\@:

	.section .fixup,"ax"
	.even
.Lu3\@:	move.l	\addr,%a0
	jra	\label
	.previous

	.section __ex_table,"a"
	.align	4
	.long	.Lu1\@,.Lu3\@
	.long	.Lu2\@,.Lu3\@
	.previous
.endm

/* work around binutils idiocy */
old_gas=-1
.irp    gas_ident.x .x
old_gas=old_gas+1
.endr
.if !old_gas
.irp	m b,w,l
.macro	getuser.\m src,dest,label,addr
	getuser .\m,\src,\dest,\label,\addr
.endm
.macro	putuser.\m src,dest,label,addr
	putuser .\m,\src,\dest,\label,\addr
.endm
.endr
.endif

.macro	movestack	nr,arg1,arg2,arg3,arg4,arg5
	.if	\nr
	movestack	(\nr-1),\arg2,\arg3,\arg4,\arg5
	move.l	\arg1,-(%sp)
	.endif
.endm

.macro	printf	bit=-1,string,nr=0,arg1,arg2,arg3,arg4,arg5
#ifdef FPU_EMU_DEBUG
	.data
.Lpdata\@:
	.string	"\string"
	.previous

	movem.l	%d0/%d1/%a0/%a1,-(%sp)
	.if	\bit+1
#if 0
	moveq	#\bit,%d0
	andw	#7,%d0
	btst	%d0,fp_debugprint+((31-\bit)/8)
#else
	btst	#\bit,fp_debugprint+((31-\bit)/8)
#endif
	jeq	.Lpskip\@
	.endif
	movestack	\nr,\arg1,\arg2,\arg3,\arg4,\arg5
	pea	.Lpdata\@
	jsr	printk
	lea	((\nr+1)*4,%sp),%sp
.Lpskip\@:
	movem.l	(%sp)+,%d0/%d1/%a0/%a1
#endif
.endm

.macro	printx	bit,fp
#ifdef FPU_EMU_DEBUG
	movem.l	%d0/%a0,-(%sp)
	lea	\fp,%a0
#if 0
	moveq	#'+',%d0
	tst.w	(%a0)
	jeq	.Lx1\@
	moveq	#'-',%d0
.Lx1\@:	printf	\bit," %c",1,%d0
	move.l	(4,%a0),%d0
	bclr	#31,%d0
	jne	.Lx2\@
	printf	\bit,"0."
	jra	.Lx3\@
.Lx2\@:	printf	\bit,"1."
.Lx3\@:	printf	\bit,"%08x%08x",2,%d0,%a0@(8)
	move.w	(2,%a0),%d0
	ext.l	%d0
	printf	\bit,"E%04x",1,%d0
#else
	printf	\bit," %08x%08x%08x",3,%a0@,%a0@(4),%a0@(8)
#endif
	movem.l	(%sp)+,%d0/%a0
#endif
.endm

.macro	debug	instr,args
#ifdef FPU_EMU_DEBUG
	\instr	\args
#endif
.endm


#endif	/* __ASSEMBLY__ */

#endif	/* _ASM_M68K_SETUP_H */
