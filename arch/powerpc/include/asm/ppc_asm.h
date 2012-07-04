/*
 * Copyright (C) 1995-1999 Gary Thomas, Paul Mackerras, Cort Dougan.
 */
#ifndef _ASM_POWERPC_PPC_ASM_H
#define _ASM_POWERPC_PPC_ASM_H

#include <linux/init.h>
#include <linux/stringify.h>
#include <asm/asm-compat.h>
#include <asm/processor.h>
#include <asm/ppc-opcode.h>
#include <asm/firmware.h>

#ifndef __ASSEMBLY__
#error __FILE__ should only be used in assembler files
#else

#define SZL			(BITS_PER_LONG/8)

/*
 * Stuff for accurate CPU time accounting.
 * These macros handle transitions between user and system state
 * in exception entry and exit and accumulate time to the
 * user_time and system_time fields in the paca.
 */

#ifndef CONFIG_VIRT_CPU_ACCOUNTING
#define ACCOUNT_CPU_USER_ENTRY(ra, rb)
#define ACCOUNT_CPU_USER_EXIT(ra, rb)
#define ACCOUNT_STOLEN_TIME
#else
#define ACCOUNT_CPU_USER_ENTRY(ra, rb)					\
	beq	2f;			/* if from kernel mode */	\
	MFTB(ra);			/* get timebase */		\
	ld	rb,PACA_STARTTIME_USER(r13);				\
	std	ra,PACA_STARTTIME(r13);					\
	subf	rb,rb,ra;		/* subtract start value */	\
	ld	ra,PACA_USER_TIME(r13);					\
	add	ra,ra,rb;		/* add on to user time */	\
	std	ra,PACA_USER_TIME(r13);					\
2:

#define ACCOUNT_CPU_USER_EXIT(ra, rb)					\
	MFTB(ra);			/* get timebase */		\
	ld	rb,PACA_STARTTIME(r13);					\
	std	ra,PACA_STARTTIME_USER(r13);				\
	subf	rb,rb,ra;		/* subtract start value */	\
	ld	ra,PACA_SYSTEM_TIME(r13);				\
	add	ra,ra,rb;		/* add on to system time */	\
	std	ra,PACA_SYSTEM_TIME(r13)

#ifdef CONFIG_PPC_SPLPAR
#define ACCOUNT_STOLEN_TIME						\
BEGIN_FW_FTR_SECTION;							\
	beq	33f;							\
	/* from user - see if there are any DTL entries to process */	\
	ld	r10,PACALPPACAPTR(r13);	/* get ptr to VPA */		\
	ld	r11,PACA_DTL_RIDX(r13);	/* get log read index */	\
	ld	r10,LPPACA_DTLIDX(r10);	/* get log write index */	\
	cmpd	cr1,r11,r10;						\
	beq+	cr1,33f;						\
	bl	.accumulate_stolen_time;				\
	ld	r12,_MSR(r1);						\
	andi.	r10,r12,MSR_PR;		/* Restore cr0 (coming from user) */ \
33:									\
END_FW_FTR_SECTION_IFSET(FW_FEATURE_SPLPAR)

#else  /* CONFIG_PPC_SPLPAR */
#define ACCOUNT_STOLEN_TIME

#endif /* CONFIG_PPC_SPLPAR */

#endif /* CONFIG_VIRT_CPU_ACCOUNTING */

/*
 * Macros for storing registers into and loading registers from
 * exception frames.
 */
#ifdef __powerpc64__
#define SAVE_GPR(n, base)	std	n,GPR0+8*(n)(base)
#define REST_GPR(n, base)	ld	n,GPR0+8*(n)(base)
#define SAVE_NVGPRS(base)	SAVE_8GPRS(14, base); SAVE_10GPRS(22, base)
#define REST_NVGPRS(base)	REST_8GPRS(14, base); REST_10GPRS(22, base)
#else
#define SAVE_GPR(n, base)	stw	n,GPR0+4*(n)(base)
#define REST_GPR(n, base)	lwz	n,GPR0+4*(n)(base)
#define SAVE_NVGPRS(base)	SAVE_GPR(13, base); SAVE_8GPRS(14, base); \
				SAVE_10GPRS(22, base)
#define REST_NVGPRS(base)	REST_GPR(13, base); REST_8GPRS(14, base); \
				REST_10GPRS(22, base)
#endif

#define SAVE_2GPRS(n, base)	SAVE_GPR(n, base); SAVE_GPR(n+1, base)
#define SAVE_4GPRS(n, base)	SAVE_2GPRS(n, base); SAVE_2GPRS(n+2, base)
#define SAVE_8GPRS(n, base)	SAVE_4GPRS(n, base); SAVE_4GPRS(n+4, base)
#define SAVE_10GPRS(n, base)	SAVE_8GPRS(n, base); SAVE_2GPRS(n+8, base)
#define REST_2GPRS(n, base)	REST_GPR(n, base); REST_GPR(n+1, base)
#define REST_4GPRS(n, base)	REST_2GPRS(n, base); REST_2GPRS(n+2, base)
#define REST_8GPRS(n, base)	REST_4GPRS(n, base); REST_4GPRS(n+4, base)
#define REST_10GPRS(n, base)	REST_8GPRS(n, base); REST_2GPRS(n+8, base)

#define SAVE_FPR(n, base)	stfd	n,THREAD_FPR0+8*TS_FPRWIDTH*(n)(base)
#define SAVE_2FPRS(n, base)	SAVE_FPR(n, base); SAVE_FPR(n+1, base)
#define SAVE_4FPRS(n, base)	SAVE_2FPRS(n, base); SAVE_2FPRS(n+2, base)
#define SAVE_8FPRS(n, base)	SAVE_4FPRS(n, base); SAVE_4FPRS(n+4, base)
#define SAVE_16FPRS(n, base)	SAVE_8FPRS(n, base); SAVE_8FPRS(n+8, base)
#define SAVE_32FPRS(n, base)	SAVE_16FPRS(n, base); SAVE_16FPRS(n+16, base)
#define REST_FPR(n, base)	lfd	n,THREAD_FPR0+8*TS_FPRWIDTH*(n)(base)
#define REST_2FPRS(n, base)	REST_FPR(n, base); REST_FPR(n+1, base)
#define REST_4FPRS(n, base)	REST_2FPRS(n, base); REST_2FPRS(n+2, base)
#define REST_8FPRS(n, base)	REST_4FPRS(n, base); REST_4FPRS(n+4, base)
#define REST_16FPRS(n, base)	REST_8FPRS(n, base); REST_8FPRS(n+8, base)
#define REST_32FPRS(n, base)	REST_16FPRS(n, base); REST_16FPRS(n+16, base)

#define SAVE_VR(n,b,base)	li b,THREAD_VR0+(16*(n));  stvx n,base,b
#define SAVE_2VRS(n,b,base)	SAVE_VR(n,b,base); SAVE_VR(n+1,b,base)
#define SAVE_4VRS(n,b,base)	SAVE_2VRS(n,b,base); SAVE_2VRS(n+2,b,base)
#define SAVE_8VRS(n,b,base)	SAVE_4VRS(n,b,base); SAVE_4VRS(n+4,b,base)
#define SAVE_16VRS(n,b,base)	SAVE_8VRS(n,b,base); SAVE_8VRS(n+8,b,base)
#define SAVE_32VRS(n,b,base)	SAVE_16VRS(n,b,base); SAVE_16VRS(n+16,b,base)
#define REST_VR(n,b,base)	li b,THREAD_VR0+(16*(n)); lvx n,base,b
#define REST_2VRS(n,b,base)	REST_VR(n,b,base); REST_VR(n+1,b,base)
#define REST_4VRS(n,b,base)	REST_2VRS(n,b,base); REST_2VRS(n+2,b,base)
#define REST_8VRS(n,b,base)	REST_4VRS(n,b,base); REST_4VRS(n+4,b,base)
#define REST_16VRS(n,b,base)	REST_8VRS(n,b,base); REST_8VRS(n+8,b,base)
#define REST_32VRS(n,b,base)	REST_16VRS(n,b,base); REST_16VRS(n+16,b,base)

/* Save the lower 32 VSRs in the thread VSR region */
#define SAVE_VSR(n,b,base)	li b,THREAD_VSR0+(16*(n));  STXVD2X(n,base,b)
#define SAVE_2VSRS(n,b,base)	SAVE_VSR(n,b,base); SAVE_VSR(n+1,b,base)
#define SAVE_4VSRS(n,b,base)	SAVE_2VSRS(n,b,base); SAVE_2VSRS(n+2,b,base)
#define SAVE_8VSRS(n,b,base)	SAVE_4VSRS(n,b,base); SAVE_4VSRS(n+4,b,base)
#define SAVE_16VSRS(n,b,base)	SAVE_8VSRS(n,b,base); SAVE_8VSRS(n+8,b,base)
#define SAVE_32VSRS(n,b,base)	SAVE_16VSRS(n,b,base); SAVE_16VSRS(n+16,b,base)
#define REST_VSR(n,b,base)	li b,THREAD_VSR0+(16*(n)); LXVD2X(n,base,b)
#define REST_2VSRS(n,b,base)	REST_VSR(n,b,base); REST_VSR(n+1,b,base)
#define REST_4VSRS(n,b,base)	REST_2VSRS(n,b,base); REST_2VSRS(n+2,b,base)
#define REST_8VSRS(n,b,base)	REST_4VSRS(n,b,base); REST_4VSRS(n+4,b,base)
#define REST_16VSRS(n,b,base)	REST_8VSRS(n,b,base); REST_8VSRS(n+8,b,base)
#define REST_32VSRS(n,b,base)	REST_16VSRS(n,b,base); REST_16VSRS(n+16,b,base)
/* Save the upper 32 VSRs (32-63) in the thread VSX region (0-31) */
#define SAVE_VSRU(n,b,base)	li b,THREAD_VR0+(16*(n));  STXVD2X(n+32,base,b)
#define SAVE_2VSRSU(n,b,base)	SAVE_VSRU(n,b,base); SAVE_VSRU(n+1,b,base)
#define SAVE_4VSRSU(n,b,base)	SAVE_2VSRSU(n,b,base); SAVE_2VSRSU(n+2,b,base)
#define SAVE_8VSRSU(n,b,base)	SAVE_4VSRSU(n,b,base); SAVE_4VSRSU(n+4,b,base)
#define SAVE_16VSRSU(n,b,base)	SAVE_8VSRSU(n,b,base); SAVE_8VSRSU(n+8,b,base)
#define SAVE_32VSRSU(n,b,base)	SAVE_16VSRSU(n,b,base); SAVE_16VSRSU(n+16,b,base)
#define REST_VSRU(n,b,base)	li b,THREAD_VR0+(16*(n)); LXVD2X(n+32,base,b)
#define REST_2VSRSU(n,b,base)	REST_VSRU(n,b,base); REST_VSRU(n+1,b,base)
#define REST_4VSRSU(n,b,base)	REST_2VSRSU(n,b,base); REST_2VSRSU(n+2,b,base)
#define REST_8VSRSU(n,b,base)	REST_4VSRSU(n,b,base); REST_4VSRSU(n+4,b,base)
#define REST_16VSRSU(n,b,base)	REST_8VSRSU(n,b,base); REST_8VSRSU(n+8,b,base)
#define REST_32VSRSU(n,b,base)	REST_16VSRSU(n,b,base); REST_16VSRSU(n+16,b,base)

/*
 * b = base register for addressing, o = base offset from register of 1st EVR
 * n = first EVR, s = scratch
 */
#define SAVE_EVR(n,s,b,o)	evmergehi s,s,n; stw s,o+4*(n)(b)
#define SAVE_2EVRS(n,s,b,o)	SAVE_EVR(n,s,b,o); SAVE_EVR(n+1,s,b,o)
#define SAVE_4EVRS(n,s,b,o)	SAVE_2EVRS(n,s,b,o); SAVE_2EVRS(n+2,s,b,o)
#define SAVE_8EVRS(n,s,b,o)	SAVE_4EVRS(n,s,b,o); SAVE_4EVRS(n+4,s,b,o)
#define SAVE_16EVRS(n,s,b,o)	SAVE_8EVRS(n,s,b,o); SAVE_8EVRS(n+8,s,b,o)
#define SAVE_32EVRS(n,s,b,o)	SAVE_16EVRS(n,s,b,o); SAVE_16EVRS(n+16,s,b,o)
#define REST_EVR(n,s,b,o)	lwz s,o+4*(n)(b); evmergelo n,s,n
#define REST_2EVRS(n,s,b,o)	REST_EVR(n,s,b,o); REST_EVR(n+1,s,b,o)
#define REST_4EVRS(n,s,b,o)	REST_2EVRS(n,s,b,o); REST_2EVRS(n+2,s,b,o)
#define REST_8EVRS(n,s,b,o)	REST_4EVRS(n,s,b,o); REST_4EVRS(n+4,s,b,o)
#define REST_16EVRS(n,s,b,o)	REST_8EVRS(n,s,b,o); REST_8EVRS(n+8,s,b,o)
#define REST_32EVRS(n,s,b,o)	REST_16EVRS(n,s,b,o); REST_16EVRS(n+16,s,b,o)

/* Macros to adjust thread priority for hardware multithreading */
#define HMT_VERY_LOW	or	31,31,31	# very low priority
#define HMT_LOW		or	1,1,1
#define HMT_MEDIUM_LOW  or	6,6,6		# medium low priority
#define HMT_MEDIUM	or	2,2,2
#define HMT_MEDIUM_HIGH or	5,5,5		# medium high priority
#define HMT_HIGH	or	3,3,3
#define HMT_EXTRA_HIGH	or	7,7,7		# power7 only

#ifdef CONFIG_PPC64
#define ULONG_SIZE 	8
#else
#define ULONG_SIZE	4
#endif
#define VCPU_GPR(n)	(VCPU_GPRS + (n * ULONG_SIZE))

#ifdef __KERNEL__
#ifdef CONFIG_PPC64

#define STACKFRAMESIZE 256
#define STK_REG(i)     (112 + ((i)-14)*8)

#define STK_PARAM(i)	(48 + ((i)-3)*8)

#define XGLUE(a,b) a##b
#define GLUE(a,b) XGLUE(a,b)

#define _GLOBAL(name) \
	.section ".text"; \
	.align 2 ; \
	.globl name; \
	.globl GLUE(.,name); \
	.section ".opd","aw"; \
name: \
	.quad GLUE(.,name); \
	.quad .TOC.@tocbase; \
	.quad 0; \
	.previous; \
	.type GLUE(.,name),@function; \
GLUE(.,name):

#define _INIT_GLOBAL(name) \
	__REF; \
	.align 2 ; \
	.globl name; \
	.globl GLUE(.,name); \
	.section ".opd","aw"; \
name: \
	.quad GLUE(.,name); \
	.quad .TOC.@tocbase; \
	.quad 0; \
	.previous; \
	.type GLUE(.,name),@function; \
GLUE(.,name):

#define _KPROBE(name) \
	.section ".kprobes.text","a"; \
	.align 2 ; \
	.globl name; \
	.globl GLUE(.,name); \
	.section ".opd","aw"; \
name: \
	.quad GLUE(.,name); \
	.quad .TOC.@tocbase; \
	.quad 0; \
	.previous; \
	.type GLUE(.,name),@function; \
GLUE(.,name):

#define _STATIC(name) \
	.section ".text"; \
	.align 2 ; \
	.section ".opd","aw"; \
name: \
	.quad GLUE(.,name); \
	.quad .TOC.@tocbase; \
	.quad 0; \
	.previous; \
	.type GLUE(.,name),@function; \
GLUE(.,name):

#define _INIT_STATIC(name) \
	__REF; \
	.align 2 ; \
	.section ".opd","aw"; \
name: \
	.quad GLUE(.,name); \
	.quad .TOC.@tocbase; \
	.quad 0; \
	.previous; \
	.type GLUE(.,name),@function; \
GLUE(.,name):

#else /* 32-bit */

#define _ENTRY(n)	\
	.globl n;	\
n:

#define _GLOBAL(n)	\
	.text;		\
	.stabs __stringify(n:F-1),N_FUN,0,0,n;\
	.globl n;	\
n:

#define _KPROBE(n)	\
	.section ".kprobes.text","a";	\
	.globl	n;	\
n:

#endif

/* 
 * LOAD_REG_IMMEDIATE(rn, expr)
 *   Loads the value of the constant expression 'expr' into register 'rn'
 *   using immediate instructions only.  Use this when it's important not
 *   to reference other data (i.e. on ppc64 when the TOC pointer is not
 *   valid) and when 'expr' is a constant or absolute address.
 *
 * LOAD_REG_ADDR(rn, name)
 *   Loads the address of label 'name' into register 'rn'.  Use this when
 *   you don't particularly need immediate instructions only, but you need
 *   the whole address in one register (e.g. it's a structure address and
 *   you want to access various offsets within it).  On ppc32 this is
 *   identical to LOAD_REG_IMMEDIATE.
 *
 * LOAD_REG_ADDRBASE(rn, name)
 * ADDROFF(name)
 *   LOAD_REG_ADDRBASE loads part of the address of label 'name' into
 *   register 'rn'.  ADDROFF(name) returns the remainder of the address as
 *   a constant expression.  ADDROFF(name) is a signed expression < 16 bits
 *   in size, so is suitable for use directly as an offset in load and store
 *   instructions.  Use this when loading/storing a single word or less as:
 *      LOAD_REG_ADDRBASE(rX, name)
 *      ld	rY,ADDROFF(name)(rX)
 */
#ifdef __powerpc64__
#define LOAD_REG_IMMEDIATE(reg,expr)		\
	lis     reg,(expr)@highest;		\
	ori     reg,reg,(expr)@higher;	\
	rldicr  reg,reg,32,31;		\
	oris    reg,reg,(expr)@h;		\
	ori     reg,reg,(expr)@l;

#define LOAD_REG_ADDR(reg,name)			\
	ld	reg,name@got(r2)

#define LOAD_REG_ADDRBASE(reg,name)	LOAD_REG_ADDR(reg,name)
#define ADDROFF(name)			0

/* offsets for stack frame layout */
#define LRSAVE	16

#else /* 32-bit */

#define LOAD_REG_IMMEDIATE(reg,expr)		\
	lis	reg,(expr)@ha;		\
	addi	reg,reg,(expr)@l;

#define LOAD_REG_ADDR(reg,name)		LOAD_REG_IMMEDIATE(reg, name)

#define LOAD_REG_ADDRBASE(reg, name)	lis	reg,name@ha
#define ADDROFF(name)			name@l

/* offsets for stack frame layout */
#define LRSAVE	4

#endif

/* various errata or part fixups */
#ifdef CONFIG_PPC601_SYNC_FIX
#define SYNC				\
BEGIN_FTR_SECTION			\
	sync;				\
	isync;				\
END_FTR_SECTION_IFSET(CPU_FTR_601)
#define SYNC_601			\
BEGIN_FTR_SECTION			\
	sync;				\
END_FTR_SECTION_IFSET(CPU_FTR_601)
#define ISYNC_601			\
BEGIN_FTR_SECTION			\
	isync;				\
END_FTR_SECTION_IFSET(CPU_FTR_601)
#else
#define	SYNC
#define SYNC_601
#define ISYNC_601
#endif

#ifdef CONFIG_PPC_CELL
#define MFTB(dest)			\
90:	mftb  dest;			\
BEGIN_FTR_SECTION_NESTED(96);		\
	cmpwi dest,0;			\
	beq-  90b;			\
END_FTR_SECTION_NESTED(CPU_FTR_CELL_TB_BUG, CPU_FTR_CELL_TB_BUG, 96)
#else
#define MFTB(dest)			mftb dest
#endif

#ifndef CONFIG_SMP
#define TLBSYNC
#else /* CONFIG_SMP */
/* tlbsync is not implemented on 601 */
#define TLBSYNC				\
BEGIN_FTR_SECTION			\
	tlbsync;			\
	sync;				\
END_FTR_SECTION_IFCLR(CPU_FTR_601)
#endif

#ifdef CONFIG_PPC64
#define MTOCRF(FXM, RS)			\
	BEGIN_FTR_SECTION_NESTED(848);	\
	mtcrf	(FXM), (RS);		\
	FTR_SECTION_ELSE_NESTED(848);	\
	mtocrf (FXM), (RS);		\
	ALT_FTR_SECTION_END_NESTED_IFCLR(CPU_FTR_NOEXECUTE, 848)
#endif

/*
 * This instruction is not implemented on the PPC 603 or 601; however, on
 * the 403GCX and 405GP tlbia IS defined and tlbie is not.
 * All of these instructions exist in the 8xx, they have magical powers,
 * and they must be used.
 */

#if !defined(CONFIG_4xx) && !defined(CONFIG_8xx)
#define tlbia					\
	li	r4,1024;			\
	mtctr	r4;				\
	lis	r4,KERNELBASE@h;		\
0:	tlbie	r4;				\
	addi	r4,r4,0x1000;			\
	bdnz	0b
#endif


#ifdef CONFIG_IBM440EP_ERR42
#define PPC440EP_ERR42 isync
#else
#define PPC440EP_ERR42
#endif

/*
 * toreal/fromreal/tophys/tovirt macros. 32-bit BookE makes them
 * keep the address intact to be compatible with code shared with
 * 32-bit classic.
 *
 * On the other hand, I find it useful to have them behave as expected
 * by their name (ie always do the addition) on 64-bit BookE
 */
#if defined(CONFIG_BOOKE) && !defined(CONFIG_PPC64)
#define toreal(rd)
#define fromreal(rd)

/*
 * We use addis to ensure compatibility with the "classic" ppc versions of
 * these macros, which use rs = 0 to get the tophys offset in rd, rather than
 * converting the address in r0, and so this version has to do that too
 * (i.e. set register rd to 0 when rs == 0).
 */
#define tophys(rd,rs)				\
	addis	rd,rs,0

#define tovirt(rd,rs)				\
	addis	rd,rs,0

#elif defined(CONFIG_PPC64)
#define toreal(rd)		/* we can access c000... in real mode */
#define fromreal(rd)

#define tophys(rd,rs)                           \
	clrldi	rd,rs,2

#define tovirt(rd,rs)                           \
	rotldi	rd,rs,16;			\
	ori	rd,rd,((KERNELBASE>>48)&0xFFFF);\
	rotldi	rd,rd,48
#else
/*
 * On APUS (Amiga PowerPC cpu upgrade board), we don't know the
 * physical base address of RAM at compile time.
 */
#define toreal(rd)	tophys(rd,rd)
#define fromreal(rd)	tovirt(rd,rd)

#define tophys(rd,rs)				\
0:	addis	rd,rs,-PAGE_OFFSET@h;		\
	.section ".vtop_fixup","aw";		\
	.align  1;				\
	.long   0b;				\
	.previous

#define tovirt(rd,rs)				\
0:	addis	rd,rs,PAGE_OFFSET@h;		\
	.section ".ptov_fixup","aw";		\
	.align  1;				\
	.long   0b;				\
	.previous
#endif

#ifdef CONFIG_PPC_BOOK3S_64
#define RFI		rfid
#define MTMSRD(r)	mtmsrd	r
#define MTMSR_EERI(reg)	mtmsrd	reg,1
#else
#define FIX_SRR1(ra, rb)
#ifndef CONFIG_40x
#define	RFI		rfi
#else
#define RFI		rfi; b .	/* Prevent prefetch past rfi */
#endif
#define MTMSRD(r)	mtmsr	r
#define MTMSR_EERI(reg)	mtmsr	reg
#define CLR_TOP32(r)
#endif

#endif /* __KERNEL__ */

/* The boring bits... */

/* Condition Register Bit Fields */

#define	cr0	0
#define	cr1	1
#define	cr2	2
#define	cr3	3
#define	cr4	4
#define	cr5	5
#define	cr6	6
#define	cr7	7


/*
 * General Purpose Registers (GPRs)
 *
 * The lower case r0-r31 should be used in preference to the upper
 * case R0-R31 as they provide more error checking in the assembler.
 * Use R0-31 only when really nessesary.
 */

#define	r0	%r0
#define	r1	%r1
#define	r2	%r2
#define	r3	%r3
#define	r4	%r4
#define	r5	%r5
#define	r6	%r6
#define	r7	%r7
#define	r8	%r8
#define	r9	%r9
#define	r10	%r10
#define	r11	%r11
#define	r12	%r12
#define	r13	%r13
#define	r14	%r14
#define	r15	%r15
#define	r16	%r16
#define	r17	%r17
#define	r18	%r18
#define	r19	%r19
#define	r20	%r20
#define	r21	%r21
#define	r22	%r22
#define	r23	%r23
#define	r24	%r24
#define	r25	%r25
#define	r26	%r26
#define	r27	%r27
#define	r28	%r28
#define	r29	%r29
#define	r30	%r30
#define	r31	%r31


/* Floating Point Registers (FPRs) */

#define	fr0	0
#define	fr1	1
#define	fr2	2
#define	fr3	3
#define	fr4	4
#define	fr5	5
#define	fr6	6
#define	fr7	7
#define	fr8	8
#define	fr9	9
#define	fr10	10
#define	fr11	11
#define	fr12	12
#define	fr13	13
#define	fr14	14
#define	fr15	15
#define	fr16	16
#define	fr17	17
#define	fr18	18
#define	fr19	19
#define	fr20	20
#define	fr21	21
#define	fr22	22
#define	fr23	23
#define	fr24	24
#define	fr25	25
#define	fr26	26
#define	fr27	27
#define	fr28	28
#define	fr29	29
#define	fr30	30
#define	fr31	31

/* AltiVec Registers (VPRs) */

#define	vr0	0
#define	vr1	1
#define	vr2	2
#define	vr3	3
#define	vr4	4
#define	vr5	5
#define	vr6	6
#define	vr7	7
#define	vr8	8
#define	vr9	9
#define	vr10	10
#define	vr11	11
#define	vr12	12
#define	vr13	13
#define	vr14	14
#define	vr15	15
#define	vr16	16
#define	vr17	17
#define	vr18	18
#define	vr19	19
#define	vr20	20
#define	vr21	21
#define	vr22	22
#define	vr23	23
#define	vr24	24
#define	vr25	25
#define	vr26	26
#define	vr27	27
#define	vr28	28
#define	vr29	29
#define	vr30	30
#define	vr31	31

/* VSX Registers (VSRs) */

#define	vsr0	0
#define	vsr1	1
#define	vsr2	2
#define	vsr3	3
#define	vsr4	4
#define	vsr5	5
#define	vsr6	6
#define	vsr7	7
#define	vsr8	8
#define	vsr9	9
#define	vsr10	10
#define	vsr11	11
#define	vsr12	12
#define	vsr13	13
#define	vsr14	14
#define	vsr15	15
#define	vsr16	16
#define	vsr17	17
#define	vsr18	18
#define	vsr19	19
#define	vsr20	20
#define	vsr21	21
#define	vsr22	22
#define	vsr23	23
#define	vsr24	24
#define	vsr25	25
#define	vsr26	26
#define	vsr27	27
#define	vsr28	28
#define	vsr29	29
#define	vsr30	30
#define	vsr31	31
#define	vsr32	32
#define	vsr33	33
#define	vsr34	34
#define	vsr35	35
#define	vsr36	36
#define	vsr37	37
#define	vsr38	38
#define	vsr39	39
#define	vsr40	40
#define	vsr41	41
#define	vsr42	42
#define	vsr43	43
#define	vsr44	44
#define	vsr45	45
#define	vsr46	46
#define	vsr47	47
#define	vsr48	48
#define	vsr49	49
#define	vsr50	50
#define	vsr51	51
#define	vsr52	52
#define	vsr53	53
#define	vsr54	54
#define	vsr55	55
#define	vsr56	56
#define	vsr57	57
#define	vsr58	58
#define	vsr59	59
#define	vsr60	60
#define	vsr61	61
#define	vsr62	62
#define	vsr63	63

/* SPE Registers (EVPRs) */

#define	evr0	0
#define	evr1	1
#define	evr2	2
#define	evr3	3
#define	evr4	4
#define	evr5	5
#define	evr6	6
#define	evr7	7
#define	evr8	8
#define	evr9	9
#define	evr10	10
#define	evr11	11
#define	evr12	12
#define	evr13	13
#define	evr14	14
#define	evr15	15
#define	evr16	16
#define	evr17	17
#define	evr18	18
#define	evr19	19
#define	evr20	20
#define	evr21	21
#define	evr22	22
#define	evr23	23
#define	evr24	24
#define	evr25	25
#define	evr26	26
#define	evr27	27
#define	evr28	28
#define	evr29	29
#define	evr30	30
#define	evr31	31

/* some stab codes */
#define N_FUN	36
#define N_RSYM	64
#define N_SLINE	68
#define N_SO	100

#endif /*  __ASSEMBLY__ */

#endif /* _ASM_POWERPC_PPC_ASM_H */
