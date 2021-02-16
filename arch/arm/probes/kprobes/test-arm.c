// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/kernel/kprobes-test-arm.c
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/system_info.h>
#include <asm/opcodes.h>
#include <asm/probes.h>

#include "test-core.h"


#define TEST_ISA "32"

#define TEST_ARM_TO_THUMB_INTERWORK_R(code1, reg, val, code2)	\
	TESTCASE_START(code1 #reg code2)			\
	TEST_ARG_REG(reg, val)					\
	TEST_ARG_REG(14, 99f)					\
	TEST_ARG_END("")					\
	"50:	nop			\n\t"			\
	"1:	"code1 #reg code2"	\n\t"			\
	"	bx	lr		\n\t"			\
	".thumb				\n\t"			\
	"3:	adr	lr, 2f		\n\t"			\
	"	bx	lr		\n\t"			\
	".arm				\n\t"			\
	"2:	nop			\n\t"			\
	TESTCASE_END

#define TEST_ARM_TO_THUMB_INTERWORK_P(code1, reg, val, code2)	\
	TESTCASE_START(code1 #reg code2)			\
	TEST_ARG_PTR(reg, val)					\
	TEST_ARG_REG(14, 99f)					\
	TEST_ARG_MEM(15, 3f+1)					\
	TEST_ARG_END("")					\
	"50:	nop			\n\t"			\
	"1:	"code1 #reg code2"	\n\t"			\
	"	bx	lr		\n\t"			\
	".thumb				\n\t"			\
	"3:	adr	lr, 2f		\n\t"			\
	"	bx	lr		\n\t"			\
	".arm				\n\t"			\
	"2:	nop			\n\t"			\
	TESTCASE_END


void kprobe_arm_test_cases(void)
{
	kprobe_test_flags = 0;

	TEST_GROUP("Data-processing (register), (register-shifted register), (immediate)")

#define _DATA_PROCESSING_DNM(op,s,val)						\
	TEST_RR(  op s "eq	r0,  r",1, VAL1,", r",2, val, "")		\
	TEST_RR(  op s "ne	r1,  r",1, VAL1,", r",2, val, ", lsl #3")	\
	TEST_RR(  op s "cs	r2,  r",3, VAL1,", r",2, val, ", lsr #4")	\
	TEST_RR(  op s "cc	r3,  r",3, VAL1,", r",2, val, ", asr #5")	\
	TEST_RR(  op s "mi	r4,  r",5, VAL1,", r",2, N(val),", asr #6")	\
	TEST_RR(  op s "pl	r5,  r",5, VAL1,", r",2, val, ", ror #7")	\
	TEST_RR(  op s "vs	r6,  r",7, VAL1,", r",2, val, ", rrx")		\
	TEST_R(   op s "vc	r6,  r",7, VAL1,", pc, lsl #3")			\
	TEST_R(   op s "vc	r6,  r",7, VAL1,", sp, lsr #4")			\
	TEST_R(   op s "vc	r6,  pc, r",7, VAL1,", asr #5")			\
	TEST_R(   op s "vc	r6,  sp, r",7, VAL1,", ror #6")			\
	TEST_RRR( op s "hi	r8,  r",9, VAL1,", r",14,val, ", lsl r",0, 3,"")\
	TEST_RRR( op s "ls	r9,  r",9, VAL1,", r",14,val, ", lsr r",7, 4,"")\
	TEST_RRR( op s "ge	r10, r",11,VAL1,", r",14,val, ", asr r",7, 5,"")\
	TEST_RRR( op s "lt	r11, r",11,VAL1,", r",14,N(val),", asr r",7, 6,"")\
	TEST_RR(  op s "gt	r12, r13"       ", r",14,val, ", ror r",14,7,"")\
	TEST_RR(  op s "le	r14, r",0, val, ", r13"       ", lsl r",14,8,"")\
	TEST_R(   op s "eq	r0,  r",11,VAL1,", #0xf5")			\
	TEST_R(   op s "ne	r11, r",0, VAL1,", #0xf5000000")		\
	TEST_R(   op s "	r7,  r",8, VAL2,", #0x000af000")		\
	TEST(     op s "	r4,  pc"        ", #0x00005a00")

#define DATA_PROCESSING_DNM(op,val)		\
	_DATA_PROCESSING_DNM(op,"",val)		\
	_DATA_PROCESSING_DNM(op,"s",val)

#define DATA_PROCESSING_NM(op,val)						\
	TEST_RR(  op "ne	r",1, VAL1,", r",2, val, "")			\
	TEST_RR(  op "eq	r",1, VAL1,", r",2, val, ", lsl #3")		\
	TEST_RR(  op "cc	r",3, VAL1,", r",2, val, ", lsr #4")		\
	TEST_RR(  op "cs	r",3, VAL1,", r",2, val, ", asr #5")		\
	TEST_RR(  op "pl	r",5, VAL1,", r",2, N(val),", asr #6")		\
	TEST_RR(  op "mi	r",5, VAL1,", r",2, val, ", ror #7")		\
	TEST_RR(  op "vc	r",7, VAL1,", r",2, val, ", rrx")		\
	TEST_R (  op "vs	r",7, VAL1,", pc, lsl #3")			\
	TEST_R (  op "vs	r",7, VAL1,", sp, lsr #4")			\
	TEST_R(   op "vs	pc, r",7, VAL1,", asr #5")			\
	TEST_R(   op "vs	sp, r",7, VAL1,", ror #6")			\
	TEST_RRR( op "ls	r",9, VAL1,", r",14,val, ", lsl r",0, 3,"")	\
	TEST_RRR( op "hi	r",9, VAL1,", r",14,val, ", lsr r",7, 4,"")	\
	TEST_RRR( op "lt	r",11,VAL1,", r",14,val, ", asr r",7, 5,"")	\
	TEST_RRR( op "ge	r",11,VAL1,", r",14,N(val),", asr r",7, 6,"")	\
	TEST_RR(  op "le	r13"       ", r",14,val, ", ror r",14,7,"")	\
	TEST_RR(  op "gt	r",0, val, ", r13"       ", lsl r",14,8,"")	\
	TEST_R(   op "eq	r",11,VAL1,", #0xf5")				\
	TEST_R(   op "ne	r",0, VAL1,", #0xf5000000")			\
	TEST_R(   op "	r",8, VAL2,", #0x000af000")

#define _DATA_PROCESSING_DM(op,s,val)					\
	TEST_R(   op s "eq	r0,  r",1, val, "")			\
	TEST_R(   op s "ne	r1,  r",1, val, ", lsl #3")		\
	TEST_R(   op s "cs	r2,  r",3, val, ", lsr #4")		\
	TEST_R(   op s "cc	r3,  r",3, val, ", asr #5")		\
	TEST_R(   op s "mi	r4,  r",5, N(val),", asr #6")		\
	TEST_R(   op s "pl	r5,  r",5, val, ", ror #7")		\
	TEST_R(   op s "vs	r6,  r",10,val, ", rrx")		\
	TEST(     op s "vs	r7,  pc, lsl #3")			\
	TEST(     op s "vs	r7,  sp, lsr #4")			\
	TEST_RR(  op s "vc	r8,  r",7, val, ", lsl r",0, 3,"")	\
	TEST_RR(  op s "hi	r9,  r",9, val, ", lsr r",7, 4,"")	\
	TEST_RR(  op s "ls	r10, r",9, val, ", asr r",7, 5,"")	\
	TEST_RR(  op s "ge	r11, r",11,N(val),", asr r",7, 6,"")	\
	TEST_RR(  op s "lt	r12, r",11,val, ", ror r",14,7,"")	\
	TEST_R(   op s "gt	r14, r13"       ", lsl r",14,8,"")	\
	TEST(     op s "eq	r0,  #0xf5")				\
	TEST(     op s "ne	r11, #0xf5000000")			\
	TEST(     op s "	r7,  #0x000af000")			\
	TEST(     op s "	r4,  #0x00005a00")

#define DATA_PROCESSING_DM(op,val)		\
	_DATA_PROCESSING_DM(op,"",val)		\
	_DATA_PROCESSING_DM(op,"s",val)

	DATA_PROCESSING_DNM("and",0xf00f00ff)
	DATA_PROCESSING_DNM("eor",0xf00f00ff)
	DATA_PROCESSING_DNM("sub",VAL2)
	DATA_PROCESSING_DNM("rsb",VAL2)
	DATA_PROCESSING_DNM("add",VAL2)
	DATA_PROCESSING_DNM("adc",VAL2)
	DATA_PROCESSING_DNM("sbc",VAL2)
	DATA_PROCESSING_DNM("rsc",VAL2)
	DATA_PROCESSING_NM("tst",0xf00f00ff)
	DATA_PROCESSING_NM("teq",0xf00f00ff)
	DATA_PROCESSING_NM("cmp",VAL2)
	DATA_PROCESSING_NM("cmn",VAL2)
	DATA_PROCESSING_DNM("orr",0xf00f00ff)
	DATA_PROCESSING_DM("mov",VAL2)
	DATA_PROCESSING_DNM("bic",0xf00f00ff)
	DATA_PROCESSING_DM("mvn",VAL2)

	TEST("mov	ip, sp") /* This has special case emulation code */

	TEST_SUPPORTED("mov	pc, #0x1000");
	TEST_SUPPORTED("mov	sp, #0x1000");
	TEST_SUPPORTED("cmp	pc, #0x1000");
	TEST_SUPPORTED("cmp	sp, #0x1000");

	/* Data-processing with PC and a shift count in a register */
	TEST_UNSUPPORTED(__inst_arm(0xe15c0f1e) "	@ cmp	r12, r14, asl pc")
	TEST_UNSUPPORTED(__inst_arm(0xe1a0cf1e) "	@ mov	r12, r14, asl pc")
	TEST_UNSUPPORTED(__inst_arm(0xe08caf1e) "	@ add	r10, r12, r14, asl pc")
	TEST_UNSUPPORTED(__inst_arm(0xe151021f) "	@ cmp	r1, pc, lsl r2")
	TEST_UNSUPPORTED(__inst_arm(0xe17f0211) "	@ cmn	pc, r1, lsl r2")
	TEST_UNSUPPORTED(__inst_arm(0xe1a0121f) "	@ mov	r1, pc, lsl r2")
	TEST_UNSUPPORTED(__inst_arm(0xe1a0f211) "	@ mov	pc, r1, lsl r2")
	TEST_UNSUPPORTED(__inst_arm(0xe042131f) "	@ sub	r1, r2, pc, lsl r3")
	TEST_UNSUPPORTED(__inst_arm(0xe1cf1312) "	@ bic	r1, pc, r2, lsl r3")
	TEST_UNSUPPORTED(__inst_arm(0xe081f312) "	@ add	pc, r1, r2, lsl r3")

	/* Data-processing with PC as a target and status registers updated */
	TEST_UNSUPPORTED("movs	pc, r1")
	TEST_UNSUPPORTED(__inst_arm(0xe1b0f211) "	@movs	pc, r1, lsl r2")
	TEST_UNSUPPORTED("movs	pc, #0x10000")
	TEST_UNSUPPORTED("adds	pc, lr, r1")
	TEST_UNSUPPORTED(__inst_arm(0xe09ef211) "	@adds	pc, lr, r1, lsl r2")
	TEST_UNSUPPORTED("adds	pc, lr, #4")

	/* Data-processing with SP as target */
	TEST("add	sp, sp, #16")
	TEST("sub	sp, sp, #8")
	TEST("bic	sp, sp, #0x20")
	TEST("orr	sp, sp, #0x20")
	TEST_PR( "add	sp, r",10,0,", r",11,4,"")
	TEST_PRR("add	sp, r",10,0,", r",11,4,", asl r",12,1,"")
	TEST_P(  "mov	sp, r",10,0,"")
	TEST_PR( "mov	sp, r",10,0,", asl r",12,0,"")

	/* Data-processing with PC as target */
	TEST_BF(   "add	pc, pc, #2f-1b-8")
	TEST_BF_R ("add	pc, pc, r",14,2f-1f-8,"")
	TEST_BF_R ("add	pc, r",14,2f-1f-8,", pc")
	TEST_BF_R ("mov	pc, r",0,2f,"")
	TEST_BF_R ("add	pc, pc, r",14,(2f-1f-8)*2,", asr #1")
	TEST_BB(   "sub	pc, pc, #1b-2b+8")
#if __LINUX_ARM_ARCH__ == 6 && !defined(CONFIG_CPU_V7)
	TEST_BB(   "sub	pc, pc, #1b-2b+8-2") /* UNPREDICTABLE before and after ARMv6 */
#endif
	TEST_BB_R( "sub	pc, pc, r",14, 1f-2f+8,"")
	TEST_BB_R( "rsb	pc, r",14,1f-2f+8,", pc")
	TEST_R(    "add	pc, pc, r",10,-2,", asl #1")
#ifdef CONFIG_THUMB2_KERNEL
	TEST_ARM_TO_THUMB_INTERWORK_R("add	pc, pc, r",0,3f-1f-8+1,"")
	TEST_ARM_TO_THUMB_INTERWORK_R("sub	pc, r",0,3f+8+1,", #8")
#endif
	TEST_GROUP("Miscellaneous instructions")

	TEST_RMASKED("mrs	r",0,~PSR_IGNORE_BITS,", cpsr")
	TEST_RMASKED("mrspl	r",7,~PSR_IGNORE_BITS,", cpsr")
	TEST_RMASKED("mrs	r",14,~PSR_IGNORE_BITS,", cpsr")
	TEST_UNSUPPORTED(__inst_arm(0xe10ff000) "	@ mrs r15, cpsr")
	TEST_UNSUPPORTED("mrs	r0, spsr")
	TEST_UNSUPPORTED("mrs	lr, spsr")

	TEST_UNSUPPORTED("msr	cpsr, r0")
	TEST_UNSUPPORTED("msr	cpsr_f, lr")
	TEST_UNSUPPORTED("msr	spsr, r0")

#if __LINUX_ARM_ARCH__ >= 5 || \
    (__LINUX_ARM_ARCH__ == 4 && !defined(CONFIG_CPU_32v4))
	TEST_BF_R("bx	r",0,2f,"")
	TEST_BB_R("bx	r",7,2f,"")
	TEST_BF_R("bxeq	r",14,2f,"")
#endif

#if __LINUX_ARM_ARCH__ >= 5
	TEST_R("clz	r0, r",0, 0x0,"")
	TEST_R("clzeq	r7, r",14,0x1,"")
	TEST_R("clz	lr, r",7, 0xffffffff,"")
	TEST(  "clz	r4, sp")
	TEST_UNSUPPORTED(__inst_arm(0x016fff10) "	@ clz pc, r0")
	TEST_UNSUPPORTED(__inst_arm(0x016f0f1f) "	@ clz r0, pc")

#if __LINUX_ARM_ARCH__ >= 6
	TEST_UNSUPPORTED("bxj	r0")
#endif

	TEST_BF_R("blx	r",0,2f,"")
	TEST_BB_R("blx	r",7,2f,"")
	TEST_BF_R("blxeq	r",14,2f,"")
	TEST_UNSUPPORTED(__inst_arm(0x0120003f) "	@ blx pc")

	TEST_RR(   "qadd	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(   "qaddvs	lr, r",9, VAL2,", r",8, VAL1,"")
	TEST_R(    "qadd	lr, r",9, VAL2,", r13")
	TEST_RR(   "qsub	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(   "qsubvs	lr, r",9, VAL2,", r",8, VAL1,"")
	TEST_R(    "qsub	lr, r",9, VAL2,", r13")
	TEST_RR(   "qdadd	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(   "qdaddvs	lr, r",9, VAL2,", r",8, VAL1,"")
	TEST_R(    "qdadd	lr, r",9, VAL2,", r13")
	TEST_RR(   "qdsub	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(   "qdsubvs	lr, r",9, VAL2,", r",8, VAL1,"")
	TEST_R(    "qdsub	lr, r",9, VAL2,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe101f050) "	@ qadd pc, r0, r1")
	TEST_UNSUPPORTED(__inst_arm(0xe121f050) "	@ qsub pc, r0, r1")
	TEST_UNSUPPORTED(__inst_arm(0xe141f050) "	@ qdadd pc, r0, r1")
	TEST_UNSUPPORTED(__inst_arm(0xe161f050) "	@ qdsub pc, r0, r1")
	TEST_UNSUPPORTED(__inst_arm(0xe16f2050) "	@ qdsub r2, r0, pc")
	TEST_UNSUPPORTED(__inst_arm(0xe161205f) "	@ qdsub r2, pc, r1")

	TEST_UNSUPPORTED("bkpt	0xffff")
	TEST_UNSUPPORTED("bkpt	0x0000")

	TEST_UNSUPPORTED(__inst_arm(0xe1600070) " @ smc #0")

	TEST_GROUP("Halfword multiply and multiply-accumulate")

	TEST_RRR(    "smlabb	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(    "smlabbge	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RR(     "smlabb	lr, r",1, VAL2,", r",2, VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe10f3281) " @ smlabb pc, r1, r2, r3")
	TEST_RRR(    "smlatb	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(    "smlatbge	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RR(     "smlatb	lr, r",1, VAL2,", r",2, VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe10f32a1) " @ smlatb pc, r1, r2, r3")
	TEST_RRR(    "smlabt	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(    "smlabtge	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RR(     "smlabt	lr, r",1, VAL2,", r",2, VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe10f32c1) " @ smlabt pc, r1, r2, r3")
	TEST_RRR(    "smlatt	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(    "smlattge	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RR(     "smlatt	lr, r",1, VAL2,", r",2, VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe10f32e1) " @ smlatt pc, r1, r2, r3")

	TEST_RRR(    "smlawb	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(    "smlawbge	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RR(     "smlawb	lr, r",1, VAL2,", r",2, VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe12f3281) " @ smlawb pc, r1, r2, r3")
	TEST_RRR(    "smlawt	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(    "smlawtge	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RR(     "smlawt	lr, r",1, VAL2,", r",2, VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe12f32c1) " @ smlawt pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe12032cf) " @ smlawt r0, pc, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe1203fc1) " @ smlawt r0, r1, pc, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe120f2c1) " @ smlawt r0, r1, r2, pc")

	TEST_RR(    "smulwb	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "smulwbge	r7, r",8, VAL3,", r",9, VAL1,"")
	TEST_R(     "smulwb	lr, r",1, VAL2,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe12f02a1) " @ smulwb pc, r1, r2")
	TEST_RR(    "smulwt	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "smulwtge	r7, r",8, VAL3,", r",9, VAL1,"")
	TEST_R(     "smulwt	lr, r",1, VAL2,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe12f02e1) " @ smulwt pc, r1, r2")

	TEST_RRRR(  "smlalbb	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR(  "smlalbble	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)
	TEST_RRR(   "smlalbb	r",14,VAL3,", r",7, VAL4,", r",5, VAL1,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe14f1382) " @ smlalbb pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe141f382) " @ smlalbb r1, pc, r2, r3")
	TEST_RRRR(  "smlaltb	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR(  "smlaltble	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)
	TEST_RRR(   "smlaltb	r",14,VAL3,", r",7, VAL4,", r",5, VAL1,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe14f13a2) " @ smlaltb pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe141f3a2) " @ smlaltb r1, pc, r2, r3")
	TEST_RRRR(  "smlalbt	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR(  "smlalbtle	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)
	TEST_RRR(   "smlalbt	r",14,VAL3,", r",7, VAL4,", r",5, VAL1,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe14f13c2) " @ smlalbt pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe141f3c2) " @ smlalbt r1, pc, r2, r3")
	TEST_RRRR(  "smlaltt	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR(  "smlalttle	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)
	TEST_RRR(   "smlaltt	r",14,VAL3,", r",7, VAL4,", r",5, VAL1,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe14f13e2) " @ smlalbb pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe140f3e2) " @ smlalbb r0, pc, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe14013ef) " @ smlalbb r0, r1, pc, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe1401fe2) " @ smlalbb r0, r1, r2, pc")

	TEST_RR(    "smulbb	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "smulbbge	r7, r",8, VAL3,", r",9, VAL1,"")
	TEST_R(     "smulbb	lr, r",1, VAL2,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe16f0281) " @ smulbb pc, r1, r2")
	TEST_RR(    "smultb	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "smultbge	r7, r",8, VAL3,", r",9, VAL1,"")
	TEST_R(     "smultb	lr, r",1, VAL2,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe16f02a1) " @ smultb pc, r1, r2")
	TEST_RR(    "smulbt	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "smulbtge	r7, r",8, VAL3,", r",9, VAL1,"")
	TEST_R(     "smulbt	lr, r",1, VAL2,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe16f02c1) " @ smultb pc, r1, r2")
	TEST_RR(    "smultt	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "smulttge	r7, r",8, VAL3,", r",9, VAL1,"")
	TEST_R(     "smultt	lr, r",1, VAL2,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe16f02e1) " @ smultt pc, r1, r2")
	TEST_UNSUPPORTED(__inst_arm(0xe16002ef) " @ smultt r0, pc, r2")
	TEST_UNSUPPORTED(__inst_arm(0xe1600fe1) " @ smultt r0, r1, pc")
#endif

	TEST_GROUP("Multiply and multiply-accumulate")

	TEST_RR(    "mul	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "mulls	r7, r",8, VAL2,", r",9, VAL2,"")
	TEST_R(     "mul	lr, r",4, VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe00f0291) " @ mul pc, r1, r2")
	TEST_UNSUPPORTED(__inst_arm(0xe000029f) " @ mul r0, pc, r2")
	TEST_UNSUPPORTED(__inst_arm(0xe0000f91) " @ mul r0, r1, pc")
	TEST_RR(    "muls	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "mulsls	r7, r",8, VAL2,", r",9, VAL2,"")
	TEST_R(     "muls	lr, r",4, VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe01f0291) " @ muls pc, r1, r2")

	TEST_RRR(    "mla	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(    "mlahi	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RR(     "mla	lr, r",1, VAL2,", r",2, VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe02f3291) " @ mla pc, r1, r2, r3")
	TEST_RRR(    "mlas	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(    "mlashi	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RR(     "mlas	lr, r",1, VAL2,", r",2, VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe03f3291) " @ mlas pc, r1, r2, r3")

#if __LINUX_ARM_ARCH__ >= 6
	TEST_RR(  "umaal	r0, r1, r",2, VAL1,", r",3, VAL2,"")
	TEST_RR(  "umaalls	r7, r8, r",9, VAL2,", r",10, VAL1,"")
	TEST_R(   "umaal	lr, r12, r",11,VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe041f392) " @ umaal pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe04f0392) " @ umaal r0, pc, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe0500090) " @ undef")
	TEST_UNSUPPORTED(__inst_arm(0xe05fff9f) " @ undef")
#endif

#if __LINUX_ARM_ARCH__ >= 7
	TEST_RRR(  "mls		r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(  "mlshi	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RR(   "mls		lr, r",1, VAL2,", r",2, VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe06f3291) " @ mls pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe060329f) " @ mls r0, pc, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe0603f91) " @ mls r0, r1, pc, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe060f291) " @ mls r0, r1, r2, pc")
#endif

	TEST_UNSUPPORTED(__inst_arm(0xe0700090) " @ undef")
	TEST_UNSUPPORTED(__inst_arm(0xe07fff9f) " @ undef")

	TEST_RR(  "umull	r0, r1, r",2, VAL1,", r",3, VAL2,"")
	TEST_RR(  "umullls	r7, r8, r",9, VAL2,", r",10, VAL1,"")
	TEST_R(   "umull	lr, r12, r",11,VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe081f392) " @ umull pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe08f1392) " @ umull r1, pc, r2, r3")
	TEST_RR(  "umulls	r0, r1, r",2, VAL1,", r",3, VAL2,"")
	TEST_RR(  "umullsls	r7, r8, r",9, VAL2,", r",10, VAL1,"")
	TEST_R(   "umulls	lr, r12, r",11,VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe091f392) " @ umulls pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe09f1392) " @ umulls r1, pc, r2, r3")

	TEST_RRRR(  "umlal	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR(  "umlalle	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)
	TEST_RRR(   "umlal	r",14,VAL3,", r",7, VAL4,", r",5, VAL1,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe0af1392) " @ umlal pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe0a1f392) " @ umlal r1, pc, r2, r3")
	TEST_RRRR(  "umlals	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR(  "umlalsle	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)
	TEST_RRR(   "umlals	r",14,VAL3,", r",7, VAL4,", r",5, VAL1,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe0bf1392) " @ umlals pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe0b1f392) " @ umlals r1, pc, r2, r3")

	TEST_RR(  "smull	r0, r1, r",2, VAL1,", r",3, VAL2,"")
	TEST_RR(  "smullls	r7, r8, r",9, VAL2,", r",10, VAL1,"")
	TEST_R(   "smull	lr, r12, r",11,VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe0c1f392) " @ smull pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe0cf1392) " @ smull r1, pc, r2, r3")
	TEST_RR(  "smulls	r0, r1, r",2, VAL1,", r",3, VAL2,"")
	TEST_RR(  "smullsls	r7, r8, r",9, VAL2,", r",10, VAL1,"")
	TEST_R(   "smulls	lr, r12, r",11,VAL3,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe0d1f392) " @ smulls pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe0df1392) " @ smulls r1, pc, r2, r3")

	TEST_RRRR(  "smlal	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR(  "smlalle	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)
	TEST_RRR(   "smlal	r",14,VAL3,", r",7, VAL4,", r",5, VAL1,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe0ef1392) " @ smlal pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe0e1f392) " @ smlal r1, pc, r2, r3")
	TEST_RRRR(  "smlals	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR(  "smlalsle	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)
	TEST_RRR(   "smlals	r",14,VAL3,", r",7, VAL4,", r",5, VAL1,", r13")
	TEST_UNSUPPORTED(__inst_arm(0xe0ff1392) " @ smlals pc, r1, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe0f0f392) " @ smlals r0, pc, r2, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe0f0139f) " @ smlals r0, r1, pc, r3")
	TEST_UNSUPPORTED(__inst_arm(0xe0f01f92) " @ smlals r0, r1, r2, pc")

	TEST_GROUP("Synchronization primitives")

#if __LINUX_ARM_ARCH__ < 6
	TEST_RP("swp	lr, r",7,VAL2,", [r",8,0,"]")
	TEST_R( "swpvs	r0, r",1,VAL1,", [sp]")
	TEST_RP("swp	sp, r",14,VAL2,", [r",12,13*4,"]")
#else
	TEST_UNSUPPORTED(__inst_arm(0xe108e097) " @ swp	lr, r7, [r8]")
	TEST_UNSUPPORTED(__inst_arm(0x610d0091) " @ swpvs	r0, r1, [sp]")
	TEST_UNSUPPORTED(__inst_arm(0xe10cd09e) " @ swp	sp, r14 [r12]")
#endif
	TEST_UNSUPPORTED(__inst_arm(0xe102f091) " @ swp pc, r1, [r2]")
	TEST_UNSUPPORTED(__inst_arm(0xe102009f) " @ swp r0, pc, [r2]")
	TEST_UNSUPPORTED(__inst_arm(0xe10f0091) " @ swp r0, r1, [pc]")
#if __LINUX_ARM_ARCH__ < 6
	TEST_RP("swpb	lr, r",7,VAL2,", [r",8,0,"]")
	TEST_R( "swpbvs	r0, r",1,VAL1,", [sp]")
#else
	TEST_UNSUPPORTED(__inst_arm(0xe148e097) " @ swpb	lr, r7, [r8]")
	TEST_UNSUPPORTED(__inst_arm(0x614d0091) " @ swpvsb	r0, r1, [sp]")
#endif
	TEST_UNSUPPORTED(__inst_arm(0xe142f091) " @ swpb pc, r1, [r2]")

	TEST_UNSUPPORTED(__inst_arm(0xe1100090)) /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe1200090)) /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe1300090)) /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe1500090)) /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe1600090)) /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe1700090)) /* Unallocated space */
#if __LINUX_ARM_ARCH__ >= 6
	TEST_UNSUPPORTED("ldrex	r2, [sp]")
#endif
#if (__LINUX_ARM_ARCH__ >= 7) || defined(CONFIG_CPU_32v6K)
	TEST_UNSUPPORTED("strexd	r0, r2, r3, [sp]")
	TEST_UNSUPPORTED("ldrexd	r2, r3, [sp]")
	TEST_UNSUPPORTED("strexb	r0, r2, [sp]")
	TEST_UNSUPPORTED("ldrexb	r2, [sp]")
	TEST_UNSUPPORTED("strexh	r0, r2, [sp]")
	TEST_UNSUPPORTED("ldrexh	r2, [sp]")
#endif
	TEST_GROUP("Extra load/store instructions")

	TEST_RPR(  "strh	r",0, VAL1,", [r",1, 48,", -r",2, 24,"]")
	TEST_RPR(  "strheq	r",14,VAL2,", [r",11,0, ", r",12, 48,"]")
	TEST_UNSUPPORTED(  "strheq	r14, [r13, r12]")
	TEST_UNSUPPORTED(  "strheq	r14, [r12, r13]")
	TEST_RPR(  "strh	r",1, VAL1,", [r",2, 24,", r",3,  48,"]!")
	TEST_RPR(  "strhne	r",12,VAL2,", [r",11,48,", -r",10,24,"]!")
	TEST_RPR(  "strh	r",2, VAL1,", [r",3, 24,"], r",4, 48,"")
	TEST_RPR(  "strh	r",10,VAL2,", [r",9, 48,"], -r",11,24,"")
	TEST_UNSUPPORTED(__inst_arm(0xe1afc0ba) "	@ strh r12, [pc, r10]!")
	TEST_UNSUPPORTED(__inst_arm(0xe089f0bb) "	@ strh pc, [r9], r11")
	TEST_UNSUPPORTED(__inst_arm(0xe089a0bf) "	@ strh r10, [r9], pc")

	TEST_PR(   "ldrh	r0, [r",0,  48,", -r",2, 24,"]")
	TEST_PR(   "ldrhcs	r14, [r",13,0, ", r",12, 48,"]")
	TEST_PR(   "ldrh	r1, [r",2,  24,", r",3,  48,"]!")
	TEST_PR(   "ldrhcc	r12, [r",11,48,", -r",10,24,"]!")
	TEST_PR(   "ldrh	r2, [r",3,  24,"], r",4, 48,"")
	TEST_PR(   "ldrh	r10, [r",9, 48,"], -r",11,24,"")
	TEST_UNSUPPORTED(__inst_arm(0xe1bfc0ba) "	@ ldrh r12, [pc, r10]!")
	TEST_UNSUPPORTED(__inst_arm(0xe099f0bb) "	@ ldrh pc, [r9], r11")
	TEST_UNSUPPORTED(__inst_arm(0xe099a0bf) "	@ ldrh r10, [r9], pc")

	TEST_RP(   "strh	r",0, VAL1,", [r",1, 24,", #-2]")
	TEST_RP(   "strhmi	r",14,VAL2,", [r",13,0, ", #2]")
	TEST_RP(   "strh	r",1, VAL1,", [r",2, 24,", #4]!")
	TEST_RP(   "strhpl	r",12,VAL2,", [r",11,24,", #-4]!")
	TEST_RP(   "strh	r",2, VAL1,", [r",3, 24,"], #48")
	TEST_RP(   "strh	r",10,VAL2,", [r",9, 64,"], #-48")
	TEST_RP(   "strh	r",3, VAL1,", [r",13,TEST_MEMORY_SIZE,", #-"__stringify(MAX_STACK_SIZE)"]!")
	TEST_UNSUPPORTED("strh r3, [r13, #-"__stringify(MAX_STACK_SIZE)"-8]!")
	TEST_RP(   "strh	r",4, VAL1,", [r",14,TEST_MEMORY_SIZE,", #-"__stringify(MAX_STACK_SIZE)"-8]!")
	TEST_UNSUPPORTED(__inst_arm(0xe1efc3b0) "	@ strh r12, [pc, #48]!")
	TEST_UNSUPPORTED(__inst_arm(0xe0c9f3b0) "	@ strh pc, [r9], #48")

	TEST_P(	   "ldrh	r0, [r",0,  24,", #-2]")
	TEST_P(	   "ldrhvs	r14, [r",13,0, ", #2]")
	TEST_P(	   "ldrh	r1, [r",2,  24,", #4]!")
	TEST_P(	   "ldrhvc	r12, [r",11,24,", #-4]!")
	TEST_P(	   "ldrh	r2, [r",3,  24,"], #48")
	TEST_P(	   "ldrh	r10, [r",9, 64,"], #-48")
	TEST(      "ldrh	r0, [pc, #0]")
	TEST_UNSUPPORTED(__inst_arm(0xe1ffc3b0) "	@ ldrh r12, [pc, #48]!")
	TEST_UNSUPPORTED(__inst_arm(0xe0d9f3b0) "	@ ldrh pc, [r9], #48")

	TEST_PR(   "ldrsb	r0, [r",0,  48,", -r",2, 24,"]")
	TEST_PR(   "ldrsbhi	r14, [r",13,0,", r",12,  48,"]")
	TEST_PR(   "ldrsb	r1, [r",2,  24,", r",3,  48,"]!")
	TEST_PR(   "ldrsbls	r12, [r",11,48,", -r",10,24,"]!")
	TEST_PR(   "ldrsb	r2, [r",3,  24,"], r",4, 48,"")
	TEST_PR(   "ldrsb	r10, [r",9, 48,"], -r",11,24,"")
	TEST_UNSUPPORTED(__inst_arm(0xe1bfc0da) "	@ ldrsb r12, [pc, r10]!")
	TEST_UNSUPPORTED(__inst_arm(0xe099f0db) "	@ ldrsb pc, [r9], r11")

	TEST_P(	   "ldrsb	r0, [r",0,  24,", #-1]")
	TEST_P(	   "ldrsbge	r14, [r",13,0, ", #1]")
	TEST_P(	   "ldrsb	r1, [r",2,  24,", #4]!")
	TEST_P(	   "ldrsblt	r12, [r",11,24,", #-4]!")
	TEST_P(	   "ldrsb	r2, [r",3,  24,"], #48")
	TEST_P(	   "ldrsb	r10, [r",9, 64,"], #-48")
	TEST(      "ldrsb	r0, [pc, #0]")
	TEST_UNSUPPORTED(__inst_arm(0xe1ffc3d0) "	@ ldrsb r12, [pc, #48]!")
	TEST_UNSUPPORTED(__inst_arm(0xe0d9f3d0) "	@ ldrsb pc, [r9], #48")

	TEST_PR(   "ldrsh	r0, [r",0,  48,", -r",2, 24,"]")
	TEST_PR(   "ldrshgt	r14, [r",13,0, ", r",12, 48,"]")
	TEST_PR(   "ldrsh	r1, [r",2,  24,", r",3,  48,"]!")
	TEST_PR(   "ldrshle	r12, [r",11,48,", -r",10,24,"]!")
	TEST_PR(   "ldrsh	r2, [r",3,  24,"], r",4, 48,"")
	TEST_PR(   "ldrsh	r10, [r",9, 48,"], -r",11,24,"")
	TEST_UNSUPPORTED(__inst_arm(0xe1bfc0fa) "	@ ldrsh r12, [pc, r10]!")
	TEST_UNSUPPORTED(__inst_arm(0xe099f0fb) "	@ ldrsh pc, [r9], r11")

	TEST_P(	   "ldrsh	r0, [r",0,  24,", #-1]")
	TEST_P(	   "ldrsheq	r14, [r",13,0 ,", #1]")
	TEST_P(	   "ldrsh	r1, [r",2,  24,", #4]!")
	TEST_P(	   "ldrshne	r12, [r",11,24,", #-4]!")
	TEST_P(	   "ldrsh	r2, [r",3,  24,"], #48")
	TEST_P(	   "ldrsh	r10, [r",9, 64,"], #-48")
	TEST(      "ldrsh	r0, [pc, #0]")
	TEST_UNSUPPORTED(__inst_arm(0xe1ffc3f0) "	@ ldrsh r12, [pc, #48]!")
	TEST_UNSUPPORTED(__inst_arm(0xe0d9f3f0) "	@ ldrsh pc, [r9], #48")

#if __LINUX_ARM_ARCH__ >= 7
	TEST_UNSUPPORTED("strht	r1, [r2], r3")
	TEST_UNSUPPORTED("ldrht	r1, [r2], r3")
	TEST_UNSUPPORTED("strht	r1, [r2], #48")
	TEST_UNSUPPORTED("ldrht	r1, [r2], #48")
	TEST_UNSUPPORTED("ldrsbt	r1, [r2], r3")
	TEST_UNSUPPORTED("ldrsbt	r1, [r2], #48")
	TEST_UNSUPPORTED("ldrsht	r1, [r2], r3")
	TEST_UNSUPPORTED("ldrsht	r1, [r2], #48")
#endif

#if __LINUX_ARM_ARCH__ >= 5
	TEST_RPR(  "strd	r",0, VAL1,", [r",1, 48,", -r",2,24,"]")
	TEST_RPR(  "strdcc	r",8, VAL2,", [r",11,0, ", r",12,48,"]")
	TEST_UNSUPPORTED(  "strdcc r8, [r13, r12]")
	TEST_UNSUPPORTED(  "strdcc r8, [r12, r13]")
	TEST_RPR(  "strd	r",4, VAL1,", [r",2, 24,", r",3, 48,"]!")
	TEST_RPR(  "strdcs	r",12,VAL2,", r13, [r",11,48,", -r",10,24,"]!")
	TEST_RPR(  "strd	r",2, VAL1,", r3, [r",5, 24,"], r",4,48,"")
	TEST_RPR(  "strd	r",10,VAL2,", r11, [r",9, 48,"], -r",7,24,"")
	TEST_UNSUPPORTED(__inst_arm(0xe1afc0fa) "	@ strd r12, [pc, r10]!")

	TEST_PR(   "ldrd	r0, [r",0, 48,", -r",2,24,"]")
	TEST_PR(   "ldrdmi	r8, [r",13,0, ", r",12,48,"]")
	TEST_PR(   "ldrd	r4, [r",2, 24,", r",3, 48,"]!")
	TEST_PR(   "ldrdpl	r6, [r",11,48,", -r",10,24,"]!")
	TEST_PR(   "ldrd	r2, r3, [r",5, 24,"], r",4,48,"")
	TEST_PR(   "ldrd	r10, r11, [r",9,48,"], -r",7,24,"")
	TEST_UNSUPPORTED(__inst_arm(0xe1afc0da) "	@ ldrd r12, [pc, r10]!")
	TEST_UNSUPPORTED(__inst_arm(0xe089f0db) "	@ ldrd pc, [r9], r11")
	TEST_UNSUPPORTED(__inst_arm(0xe089e0db) "	@ ldrd lr, [r9], r11")
	TEST_UNSUPPORTED(__inst_arm(0xe089c0df) "	@ ldrd r12, [r9], pc")

	TEST_RP(   "strd	r",0, VAL1,", [r",1, 24,", #-8]")
	TEST_RP(   "strdvs	r",8, VAL2,", [r",13,0, ", #8]")
	TEST_RP(   "strd	r",4, VAL1,", [r",2, 24,", #16]!")
	TEST_RP(   "strdvc	r",12,VAL2,", r13, [r",11,24,", #-16]!")
	TEST_RP(   "strd	r",2, VAL1,", [r",4, 24,"], #48")
	TEST_RP(   "strd	r",10,VAL2,", [r",9, 64,"], #-48")
	TEST_RP(   "strd	r",6, VAL1,", [r",13,TEST_MEMORY_SIZE,", #-"__stringify(MAX_STACK_SIZE)"]!")
	TEST_UNSUPPORTED("strd r6, [r13, #-"__stringify(MAX_STACK_SIZE)"-8]!")
	TEST_RP(   "strd	r",4, VAL1,", [r",12,TEST_MEMORY_SIZE,", #-"__stringify(MAX_STACK_SIZE)"-8]!")
	TEST_UNSUPPORTED(__inst_arm(0xe1efc3f0) "	@ strd r12, [pc, #48]!")

	TEST_P(	   "ldrd	r0, [r",0, 24,", #-8]")
	TEST_P(	   "ldrdhi	r8, [r",13,0, ", #8]")
	TEST_P(	   "ldrd	r4, [r",2, 24,", #16]!")
	TEST_P(	   "ldrdls	r6, [r",11,24,", #-16]!")
	TEST_P(	   "ldrd	r2, [r",5, 24,"], #48")
	TEST_P(	   "ldrd	r10, [r",9,6,"], #-48")
	TEST_UNSUPPORTED(__inst_arm(0xe1efc3d0) "	@ ldrd r12, [pc, #48]!")
	TEST_UNSUPPORTED(__inst_arm(0xe0c9f3d0) "	@ ldrd pc, [r9], #48")
	TEST_UNSUPPORTED(__inst_arm(0xe0c9e3d0) "	@ ldrd lr, [r9], #48")
#endif

	TEST_GROUP("Miscellaneous")

#if __LINUX_ARM_ARCH__ >= 7
	TEST("movw	r0, #0")
	TEST("movw	r0, #0xffff")
	TEST("movw	lr, #0xffff")
	TEST_UNSUPPORTED(__inst_arm(0xe300f000) "	@ movw pc, #0")
	TEST_R("movt	r",0, VAL1,", #0")
	TEST_R("movt	r",0, VAL2,", #0xffff")
	TEST_R("movt	r",14,VAL1,", #0xffff")
	TEST_UNSUPPORTED(__inst_arm(0xe340f000) "	@ movt pc, #0")
#endif

	TEST_UNSUPPORTED("msr	cpsr, 0x13")
	TEST_UNSUPPORTED("msr	cpsr_f, 0xf0000000")
	TEST_UNSUPPORTED("msr	spsr, 0x13")

#if __LINUX_ARM_ARCH__ >= 7
	TEST_SUPPORTED("yield")
	TEST("sev")
	TEST("nop")
	TEST("wfi")
	TEST_SUPPORTED("wfe")
	TEST_UNSUPPORTED("dbg #0")
#endif

	TEST_GROUP("Load/store word and unsigned byte")

#define LOAD_STORE(byte)							\
	TEST_RP( "str"byte"	r",0, VAL1,", [r",1, 24,", #-2]")		\
	TEST_RP( "str"byte"	r",14,VAL2,", [r",13,0, ", #2]")		\
	TEST_RP( "str"byte"	r",1, VAL1,", [r",2, 24,", #4]!")		\
	TEST_RP( "str"byte"	r",12,VAL2,", [r",11,24,", #-4]!")		\
	TEST_RP( "str"byte"	r",2, VAL1,", [r",3, 24,"], #48")		\
	TEST_RP( "str"byte"	r",10,VAL2,", [r",9, 64,"], #-48")		\
	TEST_RP( "str"byte"	r",3, VAL1,", [r",13,TEST_MEMORY_SIZE,", #-"__stringify(MAX_STACK_SIZE)"]!") \
	TEST_UNSUPPORTED("str"byte" r3, [r13, #-"__stringify(MAX_STACK_SIZE)"-8]!")				\
	TEST_RP( "str"byte"	r",4, VAL1,", [r",10,TEST_MEMORY_SIZE,", #-"__stringify(MAX_STACK_SIZE)"-8]!") \
	TEST_RPR("str"byte"	r",0, VAL1,", [r",1, 48,", -r",2, 24,"]")	\
	TEST_RPR("str"byte"	r",14,VAL2,", [r",11,0, ", r",12, 48,"]")	\
	TEST_UNSUPPORTED("str"byte" r14, [r13, r12]")				\
	TEST_UNSUPPORTED("str"byte" r14, [r12, r13]")				\
	TEST_RPR("str"byte"	r",1, VAL1,", [r",2, 24,", r",3,  48,"]!")	\
	TEST_RPR("str"byte"	r",12,VAL2,", [r",11,48,", -r",10,24,"]!")	\
	TEST_RPR("str"byte"	r",2, VAL1,", [r",3, 24,"], r",4, 48,"")	\
	TEST_RPR("str"byte"	r",10,VAL2,", [r",9, 48,"], -r",11,24,"")	\
	TEST_RPR("str"byte"	r",0, VAL1,", [r",1, 24,", r",2,  32,", asl #1]")\
	TEST_RPR("str"byte"	r",14,VAL2,", [r",11,0, ", r",12, 32,", lsr #2]")\
	TEST_UNSUPPORTED("str"byte"	r14, [r13, r12, lsr #2]")		\
	TEST_RPR("str"byte"	r",1, VAL1,", [r",2, 24,", r",3,  32,", asr #3]!")\
	TEST_RPR("str"byte"	r",12,VAL2,", [r",11,24,", r",10, 4,", ror #31]!")\
	TEST_P(  "ldr"byte"	r0, [r",0,  24,", #-2]")			\
	TEST_P(  "ldr"byte"	r14, [r",13,0, ", #2]")				\
	TEST_P(  "ldr"byte"	r1, [r",2,  24,", #4]!")			\
	TEST_P(  "ldr"byte"	r12, [r",11,24,", #-4]!")			\
	TEST_P(  "ldr"byte"	r2, [r",3,  24,"], #48")			\
	TEST_P(  "ldr"byte"	r10, [r",9, 64,"], #-48")			\
	TEST_PR( "ldr"byte"	r0, [r",0,  48,", -r",2, 24,"]")		\
	TEST_PR( "ldr"byte"	r14, [r",13,0, ", r",12, 48,"]")		\
	TEST_PR( "ldr"byte"	r1, [r",2,  24,", r",3, 48,"]!")		\
	TEST_PR( "ldr"byte"	r12, [r",11,48,", -r",10,24,"]!")		\
	TEST_PR( "ldr"byte"	r2, [r",3,  24,"], r",4, 48,"")			\
	TEST_PR( "ldr"byte"	r10, [r",9, 48,"], -r",11,24,"")		\
	TEST_PR( "ldr"byte"	r0, [r",0,  24,", r",2,  32,", asl #1]")	\
	TEST_PR( "ldr"byte"	r14, [r",13,0, ", r",12, 32,", lsr #2]")	\
	TEST_PR( "ldr"byte"	r1, [r",2,  24,", r",3,  32,", asr #3]!")	\
	TEST_PR( "ldr"byte"	r12, [r",11,24,", r",10, 4,", ror #31]!")	\
	TEST(    "ldr"byte"	r0, [pc, #0]")					\
	TEST_R(  "ldr"byte"	r12, [pc, r",14,0,"]")

	LOAD_STORE("")
	TEST_P(   "str	pc, [r",0,0,", #15*4]")
	TEST_UNSUPPORTED(   "str	pc, [sp, r2]")
	TEST_BF(  "ldr	pc, [sp, #15*4]")
	TEST_BF_R("ldr	pc, [sp, r",2,15*4,"]")

	TEST_P(   "str	sp, [r",0,0,", #13*4]")
	TEST_UNSUPPORTED(   "str	sp, [sp, r2]")
	TEST_BF(  "ldr	sp, [sp, #13*4]")
	TEST_BF_R("ldr	sp, [sp, r",2,13*4,"]")

#ifdef CONFIG_THUMB2_KERNEL
	TEST_ARM_TO_THUMB_INTERWORK_P("ldr	pc, [r",0,0,", #15*4]")
#endif
	TEST_UNSUPPORTED(__inst_arm(0xe5af6008) "	@ str r6, [pc, #8]!")
	TEST_UNSUPPORTED(__inst_arm(0xe7af6008) "	@ str r6, [pc, r8]!")
	TEST_UNSUPPORTED(__inst_arm(0xe5bf6008) "	@ ldr r6, [pc, #8]!")
	TEST_UNSUPPORTED(__inst_arm(0xe7bf6008) "	@ ldr r6, [pc, r8]!")
	TEST_UNSUPPORTED(__inst_arm(0xe788600f) "	@ str r6, [r8, pc]")
	TEST_UNSUPPORTED(__inst_arm(0xe798600f) "	@ ldr r6, [r8, pc]")

	LOAD_STORE("b")
	TEST_UNSUPPORTED(__inst_arm(0xe5f7f008) "	@ ldrb pc, [r7, #8]!")
	TEST_UNSUPPORTED(__inst_arm(0xe7f7f008) "	@ ldrb pc, [r7, r8]!")
	TEST_UNSUPPORTED(__inst_arm(0xe5ef6008) "	@ strb r6, [pc, #8]!")
	TEST_UNSUPPORTED(__inst_arm(0xe7ef6008) "	@ strb r6, [pc, r3]!")
	TEST_UNSUPPORTED(__inst_arm(0xe5ff6008) "	@ ldrb r6, [pc, #8]!")
	TEST_UNSUPPORTED(__inst_arm(0xe7ff6008) "	@ ldrb r6, [pc, r3]!")

	TEST_UNSUPPORTED("ldrt	r0, [r1], #4")
	TEST_UNSUPPORTED("ldrt	r1, [r2], r3")
	TEST_UNSUPPORTED("strt	r2, [r3], #4")
	TEST_UNSUPPORTED("strt	r3, [r4], r5")
	TEST_UNSUPPORTED("ldrbt	r4, [r5], #4")
	TEST_UNSUPPORTED("ldrbt	r5, [r6], r7")
	TEST_UNSUPPORTED("strbt	r6, [r7], #4")
	TEST_UNSUPPORTED("strbt	r7, [r8], r9")

#if __LINUX_ARM_ARCH__ >= 7
	TEST_GROUP("Parallel addition and subtraction, signed")

	TEST_UNSUPPORTED(__inst_arm(0xe6000010) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe60fffff) "") /* Unallocated space */

	TEST_RR(    "sadd16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "sadd16	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe61cff1a) "	@ sadd16	pc, r12, r10")
	TEST_RR(    "sasx	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "sasx	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe61cff3a) "	@ sasx	pc, r12, r10")
	TEST_RR(    "ssax	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "ssax	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe61cff5a) "	@ ssax	pc, r12, r10")
	TEST_RR(    "ssub16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "ssub16	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe61cff7a) "	@ ssub16	pc, r12, r10")
	TEST_RR(    "sadd8	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "sadd8	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe61cff9a) "	@ sadd8	pc, r12, r10")
	TEST_UNSUPPORTED(__inst_arm(0xe61000b0) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe61fffbf) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe61000d0) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe61fffdf) "") /* Unallocated space */
	TEST_RR(    "ssub8	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "ssub8	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe61cfffa) "	@ ssub8	pc, r12, r10")

	TEST_RR(    "qadd16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "qadd16	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe62cff1a) "	@ qadd16	pc, r12, r10")
	TEST_RR(    "qasx	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "qasx	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe62cff3a) "	@ qasx	pc, r12, r10")
	TEST_RR(    "qsax	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "qsax	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe62cff5a) "	@ qsax	pc, r12, r10")
	TEST_RR(    "qsub16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "qsub16	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe62cff7a) "	@ qsub16	pc, r12, r10")
	TEST_RR(    "qadd8	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "qadd8	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe62cff9a) "	@ qadd8	pc, r12, r10")
	TEST_UNSUPPORTED(__inst_arm(0xe62000b0) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe62fffbf) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe62000d0) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe62fffdf) "") /* Unallocated space */
	TEST_RR(    "qsub8	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "qsub8	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe62cfffa) "	@ qsub8	pc, r12, r10")

	TEST_RR(    "shadd16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "shadd16	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe63cff1a) "	@ shadd16	pc, r12, r10")
	TEST_RR(    "shasx	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "shasx	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe63cff3a) "	@ shasx	pc, r12, r10")
	TEST_RR(    "shsax	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "shsax	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe63cff5a) "	@ shsax	pc, r12, r10")
	TEST_RR(    "shsub16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "shsub16	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe63cff7a) "	@ shsub16	pc, r12, r10")
	TEST_RR(    "shadd8	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "shadd8	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe63cff9a) "	@ shadd8	pc, r12, r10")
	TEST_UNSUPPORTED(__inst_arm(0xe63000b0) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe63fffbf) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe63000d0) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe63fffdf) "") /* Unallocated space */
	TEST_RR(    "shsub8	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "shsub8	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe63cfffa) "	@ shsub8	pc, r12, r10")

	TEST_GROUP("Parallel addition and subtraction, unsigned")

	TEST_UNSUPPORTED(__inst_arm(0xe6400010) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe64fffff) "") /* Unallocated space */

	TEST_RR(    "uadd16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uadd16	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe65cff1a) "	@ uadd16	pc, r12, r10")
	TEST_RR(    "uasx	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uasx	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe65cff3a) "	@ uasx	pc, r12, r10")
	TEST_RR(    "usax	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "usax	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe65cff5a) "	@ usax	pc, r12, r10")
	TEST_RR(    "usub16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "usub16	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe65cff7a) "	@ usub16	pc, r12, r10")
	TEST_RR(    "uadd8	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uadd8	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe65cff9a) "	@ uadd8	pc, r12, r10")
	TEST_UNSUPPORTED(__inst_arm(0xe65000b0) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe65fffbf) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe65000d0) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe65fffdf) "") /* Unallocated space */
	TEST_RR(    "usub8	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "usub8	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe65cfffa) "	@ usub8	pc, r12, r10")

	TEST_RR(    "uqadd16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uqadd16	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe66cff1a) "	@ uqadd16	pc, r12, r10")
	TEST_RR(    "uqasx	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uqasx	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe66cff3a) "	@ uqasx	pc, r12, r10")
	TEST_RR(    "uqsax	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uqsax	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe66cff5a) "	@ uqsax	pc, r12, r10")
	TEST_RR(    "uqsub16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uqsub16	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe66cff7a) "	@ uqsub16	pc, r12, r10")
	TEST_RR(    "uqadd8	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uqadd8	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe66cff9a) "	@ uqadd8	pc, r12, r10")
	TEST_UNSUPPORTED(__inst_arm(0xe66000b0) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe66fffbf) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe66000d0) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe66fffdf) "") /* Unallocated space */
	TEST_RR(    "uqsub8	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uqsub8	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe66cfffa) "	@ uqsub8	pc, r12, r10")

	TEST_RR(    "uhadd16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uhadd16	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe67cff1a) "	@ uhadd16	pc, r12, r10")
	TEST_RR(    "uhasx	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uhasx	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe67cff3a) "	@ uhasx	pc, r12, r10")
	TEST_RR(    "uhsax	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uhsax	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe67cff5a) "	@ uhsax	pc, r12, r10")
	TEST_RR(    "uhsub16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uhsub16	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe67cff7a) "	@ uhsub16	pc, r12, r10")
	TEST_RR(    "uhadd8	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uhadd8	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe67cff9a) "	@ uhadd8	pc, r12, r10")
	TEST_UNSUPPORTED(__inst_arm(0xe67000b0) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe67fffbf) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe67000d0) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe67fffdf) "") /* Unallocated space */
	TEST_RR(    "uhsub8	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uhsub8	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe67cfffa) "	@ uhsub8	pc, r12, r10")
	TEST_UNSUPPORTED(__inst_arm(0xe67feffa) "	@ uhsub8	r14, pc, r10")
	TEST_UNSUPPORTED(__inst_arm(0xe67cefff) "	@ uhsub8	r14, r12, pc")
#endif /* __LINUX_ARM_ARCH__ >= 7 */

#if __LINUX_ARM_ARCH__ >= 6
	TEST_GROUP("Packing, unpacking, saturation, and reversal")

	TEST_RR(    "pkhbt	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "pkhbt	r14,r",12, HH1,", r",10,HH2,", lsl #2")
	TEST_UNSUPPORTED(__inst_arm(0xe68cf11a) "	@ pkhbt	pc, r12, r10, lsl #2")
	TEST_RR(    "pkhtb	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "pkhtb	r14,r",12, HH1,", r",10,HH2,", asr #2")
	TEST_UNSUPPORTED(__inst_arm(0xe68cf15a) "	@ pkhtb	pc, r12, r10, asr #2")
	TEST_UNSUPPORTED(__inst_arm(0xe68fe15a) "	@ pkhtb	r14, pc, r10, asr #2")
	TEST_UNSUPPORTED(__inst_arm(0xe68ce15f) "	@ pkhtb	r14, r12, pc, asr #2")
	TEST_UNSUPPORTED(__inst_arm(0xe6900010) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe69fffdf) "") /* Unallocated space */

	TEST_R(     "ssat	r0, #24, r",0,   VAL1,"")
	TEST_R(     "ssat	r14, #24, r",12, VAL2,"")
	TEST_R(     "ssat	r0, #24, r",0,   VAL1,", lsl #8")
	TEST_R(     "ssat	r14, #24, r",12, VAL2,", asr #8")
	TEST_UNSUPPORTED(__inst_arm(0xe6b7f01c) "	@ ssat	pc, #24, r12")

	TEST_R(     "usat	r0, #24, r",0,   VAL1,"")
	TEST_R(     "usat	r14, #24, r",12, VAL2,"")
	TEST_R(     "usat	r0, #24, r",0,   VAL1,", lsl #8")
	TEST_R(     "usat	r14, #24, r",12, VAL2,", asr #8")
	TEST_UNSUPPORTED(__inst_arm(0xe6f7f01c) "	@ usat	pc, #24, r12")

	TEST_RR(    "sxtab16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "sxtab16	r14,r",12, HH2,", r",10,HH1,", ror #8")
	TEST_R(     "sxtb16	r8, r",7,  HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe68cf47a) "	@ sxtab16	pc,r12, r10, ror #8")

	TEST_RR(    "sel	r0, r",0,  VAL1,", r",1, VAL2,"")
	TEST_RR(    "sel	r14, r",12,VAL1,", r",10, VAL2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe68cffba) "	@ sel	pc, r12, r10")
	TEST_UNSUPPORTED(__inst_arm(0xe68fefba) "	@ sel	r14, pc, r10")
	TEST_UNSUPPORTED(__inst_arm(0xe68cefbf) "	@ sel	r14, r12, pc")

	TEST_R(     "ssat16	r0, #12, r",0,   HH1,"")
	TEST_R(     "ssat16	r14, #12, r",12, HH2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe6abff3c) "	@ ssat16	pc, #12, r12")

	TEST_RR(    "sxtab	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "sxtab	r14,r",12, HH2,", r",10,HH1,", ror #8")
	TEST_R(     "sxtb	r8, r",7,  HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe6acf47a) "	@ sxtab	pc,r12, r10, ror #8")

	TEST_R(     "rev	r0, r",0,   VAL1,"")
	TEST_R(     "rev	r14, r",12, VAL2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe6bfff3c) "	@ rev	pc, r12")

	TEST_RR(    "sxtah	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "sxtah	r14,r",12, HH2,", r",10,HH1,", ror #8")
	TEST_R(     "sxth	r8, r",7,  HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe6bcf47a) "	@ sxtah	pc,r12, r10, ror #8")

	TEST_R(     "rev16	r0, r",0,   VAL1,"")
	TEST_R(     "rev16	r14, r",12, VAL2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe6bfffbc) "	@ rev16	pc, r12")

	TEST_RR(    "uxtab16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uxtab16	r14,r",12, HH2,", r",10,HH1,", ror #8")
	TEST_R(     "uxtb16	r8, r",7,  HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe6ccf47a) "	@ uxtab16	pc,r12, r10, ror #8")

	TEST_R(     "usat16	r0, #12, r",0,   HH1,"")
	TEST_R(     "usat16	r14, #12, r",12, HH2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe6ecff3c) "	@ usat16	pc, #12, r12")
	TEST_UNSUPPORTED(__inst_arm(0xe6ecef3f) "	@ usat16	r14, #12, pc")

	TEST_RR(    "uxtab	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uxtab	r14,r",12, HH2,", r",10,HH1,", ror #8")
	TEST_R(     "uxtb	r8, r",7,  HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe6ecf47a) "	@ uxtab	pc,r12, r10, ror #8")

#if __LINUX_ARM_ARCH__ >= 7
	TEST_R(     "rbit	r0, r",0,   VAL1,"")
	TEST_R(     "rbit	r14, r",12, VAL2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe6ffff3c) "	@ rbit	pc, r12")
#endif

	TEST_RR(    "uxtah	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uxtah	r14,r",12, HH2,", r",10,HH1,", ror #8")
	TEST_R(     "uxth	r8, r",7,  HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe6fff077) "	@ uxth	pc, r7")
	TEST_UNSUPPORTED(__inst_arm(0xe6ff807f) "	@ uxth	r8, pc")
	TEST_UNSUPPORTED(__inst_arm(0xe6fcf47a) "	@ uxtah	pc, r12, r10, ror #8")
	TEST_UNSUPPORTED(__inst_arm(0xe6fce47f) "	@ uxtah	r14, r12, pc, ror #8")

	TEST_R(     "revsh	r0, r",0,   VAL1,"")
	TEST_R(     "revsh	r14, r",12, VAL2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe6ffff3c) "	@ revsh	pc, r12")
	TEST_UNSUPPORTED(__inst_arm(0xe6ffef3f) "	@ revsh	r14, pc")

	TEST_UNSUPPORTED(__inst_arm(0xe6900070) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe69fff7f) "") /* Unallocated space */

	TEST_UNSUPPORTED(__inst_arm(0xe6d00070) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_arm(0xe6dfff7f) "") /* Unallocated space */
#endif /* __LINUX_ARM_ARCH__ >= 6 */

#if __LINUX_ARM_ARCH__ >= 6
	TEST_GROUP("Signed multiplies")

	TEST_RRR(   "smlad	r0, r",0,  HH1,", r",1, HH2,", r",2, VAL1,"")
	TEST_RRR(   "smlad	r14, r",12,HH2,", r",10,HH1,", r",8, VAL2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe70f8a1c) "	@ smlad	pc, r12, r10, r8")
	TEST_RRR(   "smladx	r0, r",0,  HH1,", r",1, HH2,", r",2, VAL1,"")
	TEST_RRR(   "smladx	r14, r",12,HH2,", r",10,HH1,", r",8, VAL2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe70f8a3c) "	@ smladx	pc, r12, r10, r8")

	TEST_RR(   "smuad	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(   "smuad	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe70ffa1c) "	@ smuad	pc, r12, r10")
	TEST_RR(   "smuadx	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(   "smuadx	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe70ffa3c) "	@ smuadx	pc, r12, r10")

	TEST_RRR(   "smlsd	r0, r",0,  HH1,", r",1, HH2,", r",2, VAL1,"")
	TEST_RRR(   "smlsd	r14, r",12,HH2,", r",10,HH1,", r",8, VAL2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe70f8a5c) "	@ smlsd	pc, r12, r10, r8")
	TEST_RRR(   "smlsdx	r0, r",0,  HH1,", r",1, HH2,", r",2, VAL1,"")
	TEST_RRR(   "smlsdx	r14, r",12,HH2,", r",10,HH1,", r",8, VAL2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe70f8a7c) "	@ smlsdx	pc, r12, r10, r8")

	TEST_RR(   "smusd	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(   "smusd	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe70ffa5c) "	@ smusd	pc, r12, r10")
	TEST_RR(   "smusdx	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(   "smusdx	r14, r",12,HH2,", r",10,HH1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe70ffa7c) "	@ smusdx	pc, r12, r10")

	TEST_RRRR( "smlald	r",0, VAL1,", r",1, VAL2, ", r",0, HH1,", r",1, HH2)
	TEST_RRRR( "smlald	r",11,VAL2,", r",10,VAL1, ", r",9, HH2,", r",8, HH1)
	TEST_UNSUPPORTED(__inst_arm(0xe74af819) "	@ smlald	pc, r10, r9, r8")
	TEST_UNSUPPORTED(__inst_arm(0xe74fb819) "	@ smlald	r11, pc, r9, r8")
	TEST_UNSUPPORTED(__inst_arm(0xe74ab81f) "	@ smlald	r11, r10, pc, r8")
	TEST_UNSUPPORTED(__inst_arm(0xe74abf19) "	@ smlald	r11, r10, r9, pc")

	TEST_RRRR( "smlaldx	r",0, VAL1,", r",1, VAL2, ", r",0, HH1,", r",1, HH2)
	TEST_RRRR( "smlaldx	r",11,VAL2,", r",10,VAL1, ", r",9, HH2,", r",8, HH1)
	TEST_UNSUPPORTED(__inst_arm(0xe74af839) "	@ smlaldx	pc, r10, r9, r8")
	TEST_UNSUPPORTED(__inst_arm(0xe74fb839) "	@ smlaldx	r11, pc, r9, r8")

	TEST_RRR(  "smmla	r0, r",0,  VAL1,", r",1, VAL2,", r",2, VAL1,"")
	TEST_RRR(  "smmla	r14, r",12,VAL2,", r",10,VAL1,", r",8, VAL2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe75f8a1c) "	@ smmla	pc, r12, r10, r8")
	TEST_RRR(  "smmlar	r0, r",0,  VAL1,", r",1, VAL2,", r",2, VAL1,"")
	TEST_RRR(  "smmlar	r14, r",12,VAL2,", r",10,VAL1,", r",8, VAL2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe75f8a3c) "	@ smmlar	pc, r12, r10, r8")

	TEST_RR(   "smmul	r0, r",0,  VAL1,", r",1, VAL2,"")
	TEST_RR(   "smmul	r14, r",12,VAL2,", r",10,VAL1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe75ffa1c) "	@ smmul	pc, r12, r10")
	TEST_RR(   "smmulr	r0, r",0,  VAL1,", r",1, VAL2,"")
	TEST_RR(   "smmulr	r14, r",12,VAL2,", r",10,VAL1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe75ffa3c) "	@ smmulr	pc, r12, r10")

	TEST_RRR(  "smmls	r0, r",0,  VAL1,", r",1, VAL2,", r",2, VAL1,"")
	TEST_RRR(  "smmls	r14, r",12,VAL2,", r",10,VAL1,", r",8, VAL2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe75f8adc) "	@ smmls	pc, r12, r10, r8")
	TEST_RRR(  "smmlsr	r0, r",0,  VAL1,", r",1, VAL2,", r",2, VAL1,"")
	TEST_RRR(  "smmlsr	r14, r",12,VAL2,", r",10,VAL1,", r",8, VAL2,"")
	TEST_UNSUPPORTED(__inst_arm(0xe75f8afc) "	@ smmlsr	pc, r12, r10, r8")
	TEST_UNSUPPORTED(__inst_arm(0xe75e8aff) "	@ smmlsr	r14, pc, r10, r8")
	TEST_UNSUPPORTED(__inst_arm(0xe75e8ffc) "	@ smmlsr	r14, r12, pc, r8")
	TEST_UNSUPPORTED(__inst_arm(0xe75efafc) "	@ smmlsr	r14, r12, r10, pc")

	TEST_RR(   "usad8	r0, r",0,  VAL1,", r",1, VAL2,"")
	TEST_RR(   "usad8	r14, r",12,VAL2,", r",10,VAL1,"")
	TEST_UNSUPPORTED(__inst_arm(0xe75ffa1c) "	@ usad8	pc, r12, r10")
	TEST_UNSUPPORTED(__inst_arm(0xe75efa1f) "	@ usad8	r14, pc, r10")
	TEST_UNSUPPORTED(__inst_arm(0xe75eff1c) "	@ usad8	r14, r12, pc")

	TEST_RRR(  "usada8	r0, r",0,  VAL1,", r",1, VAL2,", r",2, VAL3,"")
	TEST_RRR(  "usada8	r14, r",12,VAL2,", r",10,VAL1,", r",8, VAL3,"")
	TEST_UNSUPPORTED(__inst_arm(0xe78f8a1c) "	@ usada8	pc, r12, r10, r8")
	TEST_UNSUPPORTED(__inst_arm(0xe78e8a1f) "	@ usada8	r14, pc, r10, r8")
	TEST_UNSUPPORTED(__inst_arm(0xe78e8f1c) "	@ usada8	r14, r12, pc, r8")
#endif /* __LINUX_ARM_ARCH__ >= 6 */

#if __LINUX_ARM_ARCH__ >= 7
	TEST_GROUP("Bit Field")

	TEST_R(     "sbfx	r0, r",0  , VAL1,", #0, #31")
	TEST_R(     "sbfxeq	r14, r",12, VAL2,", #8, #16")
	TEST_R(     "sbfx	r4, r",10,  VAL1,", #16, #15")
	TEST_UNSUPPORTED(__inst_arm(0xe7aff45c) "	@ sbfx	pc, r12, #8, #16")

	TEST_R(     "ubfx	r0, r",0  , VAL1,", #0, #31")
	TEST_R(     "ubfxcs	r14, r",12, VAL2,", #8, #16")
	TEST_R(     "ubfx	r4, r",10,  VAL1,", #16, #15")
	TEST_UNSUPPORTED(__inst_arm(0xe7eff45c) "	@ ubfx	pc, r12, #8, #16")
	TEST_UNSUPPORTED(__inst_arm(0xe7efc45f) "	@ ubfx	r12, pc, #8, #16")

	TEST_R(     "bfc	r",0, VAL1,", #4, #20")
	TEST_R(     "bfcvs	r",14,VAL2,", #4, #20")
	TEST_R(     "bfc	r",7, VAL1,", #0, #31")
	TEST_R(     "bfc	r",8, VAL2,", #0, #31")
	TEST_UNSUPPORTED(__inst_arm(0xe7def01f) "	@ bfc	pc, #0, #31");

	TEST_RR(    "bfi	r",0, VAL1,", r",0  , VAL2,", #0, #31")
	TEST_RR(    "bfipl	r",12,VAL1,", r",14 , VAL2,", #4, #20")
	TEST_UNSUPPORTED(__inst_arm(0xe7d7f21e) "	@ bfi	pc, r14, #4, #20")

	TEST_UNSUPPORTED(__inst_arm(0x07f000f0) "")  /* Permanently UNDEFINED */
	TEST_UNSUPPORTED(__inst_arm(0x07ffffff) "")  /* Permanently UNDEFINED */
#endif /* __LINUX_ARM_ARCH__ >= 6 */

	TEST_GROUP("Branch, branch with link, and block data transfer")

	TEST_P(   "stmda	r",0, 16*4,", {r0}")
	TEST_P(   "stmdaeq	r",4, 16*4,", {r0-r15}")
	TEST_P(   "stmdane	r",8, 16*4,"!, {r8-r15}")
	TEST_P(   "stmda	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")
	TEST_P(   "stmda	r",13,0,   "!, {pc}")

	TEST_P(   "ldmda	r",0, 16*4,", {r0}")
	TEST_BF_P("ldmdacs	r",4, 15*4,", {r0-r15}")
	TEST_BF_P("ldmdacc	r",7, 15*4,"!, {r8-r15}")
	TEST_P(   "ldmda	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")
	TEST_BF_P("ldmda	r",14,15*4,"!, {pc}")

	TEST_P(   "stmia	r",0, 16*4,", {r0}")
	TEST_P(   "stmiami	r",4, 16*4,", {r0-r15}")
	TEST_P(   "stmiapl	r",8, 16*4,"!, {r8-r15}")
	TEST_P(   "stmia	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")
	TEST_P(   "stmia	r",14,0,   "!, {pc}")

	TEST_P(   "ldmia	r",0, 16*4,", {r0}")
	TEST_BF_P("ldmiavs	r",4, 0,   ", {r0-r15}")
	TEST_BF_P("ldmiavc	r",7, 8*4, "!, {r8-r15}")
	TEST_P(   "ldmia	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")
	TEST_BF_P("ldmia	r",14,15*4,"!, {pc}")

	TEST_P(   "stmdb	r",0, 16*4,", {r0}")
	TEST_P(   "stmdbhi	r",4, 16*4,", {r0-r15}")
	TEST_P(   "stmdbls	r",8, 16*4,"!, {r8-r15}")
	TEST_P(   "stmdb	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")
	TEST_P(   "stmdb	r",13,4,   "!, {pc}")

	TEST_P(   "ldmdb	r",0, 16*4,", {r0}")
	TEST_BF_P("ldmdbge	r",4, 16*4,", {r0-r15}")
	TEST_BF_P("ldmdblt	r",7, 16*4,"!, {r8-r15}")
	TEST_P(   "ldmdb	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")
	TEST_BF_P("ldmdb	r",14,16*4,"!, {pc}")

	TEST_P(   "stmib	r",0, 16*4,", {r0}")
	TEST_P(   "stmibgt	r",4, 16*4,", {r0-r15}")
	TEST_P(   "stmible	r",8, 16*4,"!, {r8-r15}")
	TEST_P(   "stmib	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")
	TEST_P(   "stmib	r",13,-4,  "!, {pc}")

	TEST_P(   "ldmib	r",0, 16*4,", {r0}")
	TEST_BF_P("ldmibeq	r",4, -4,", {r0-r15}")
	TEST_BF_P("ldmibne	r",7, 7*4,"!, {r8-r15}")
	TEST_P(   "ldmib	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")
	TEST_BF_P("ldmib	r",14,14*4,"!, {pc}")

	TEST_P(   "stmdb	r",13,16*4,"!, {r3-r12,lr}")
	TEST_P(	  "stmdbeq	r",13,16*4,"!, {r3-r12}")
	TEST_P(   "stmdbne	r",2, 16*4,", {r3-r12,lr}")
	TEST_P(   "stmdb	r",13,16*4,"!, {r2-r12,lr}")
	TEST_P(   "stmdb	r",0, 16*4,", {r0-r12}")
	TEST_P(   "stmdb	r",0, 16*4,", {r0-r12,lr}")

	TEST_BF_P("ldmia	r",13,5*4, "!, {r3-r12,pc}")
	TEST_P(	  "ldmiacc	r",13,5*4, "!, {r3-r12}")
	TEST_BF_P("ldmiacs	r",2, 5*4, "!, {r3-r12,pc}")
	TEST_BF_P("ldmia	r",13,4*4, "!, {r2-r12,pc}")
	TEST_P(   "ldmia	r",0, 16*4,", {r0-r12}")
	TEST_P(   "ldmia	r",0, 16*4,", {r0-r12,lr}")

#ifdef CONFIG_THUMB2_KERNEL
	TEST_ARM_TO_THUMB_INTERWORK_P("ldmplia	r",0,15*4,", {pc}")
	TEST_ARM_TO_THUMB_INTERWORK_P("ldmmiia	r",13,0,", {r0-r15}")
#endif
	TEST_BF("b	2f")
	TEST_BF("bl	2f")
	TEST_BB("b	2b")
	TEST_BB("bl	2b")

	TEST_BF("beq	2f")
	TEST_BF("bleq	2f")
	TEST_BB("bne	2b")
	TEST_BB("blne	2b")

	TEST_BF("bgt	2f")
	TEST_BF("blgt	2f")
	TEST_BB("blt	2b")
	TEST_BB("bllt	2b")

	TEST_GROUP("Supervisor Call, and coprocessor instructions")

	/*
	 * We can't really test these by executing them, so all
	 * we can do is check that probes are, or are not allowed.
	 * At the moment none are allowed...
	 */
#define TEST_COPROCESSOR(code) TEST_UNSUPPORTED(code)

#define COPROCESSOR_INSTRUCTIONS_ST_LD(two,cc)					\
	TEST_COPROCESSOR("stc"two"	p0, cr0, [r13, #4]")			\
	TEST_COPROCESSOR("stc"two"	p0, cr0, [r13, #-4]")			\
	TEST_COPROCESSOR("stc"two"	p0, cr0, [r13, #4]!")			\
	TEST_COPROCESSOR("stc"two"	p0, cr0, [r13, #-4]!")			\
	TEST_COPROCESSOR("stc"two"	p0, cr0, [r13], #4")			\
	TEST_COPROCESSOR("stc"two"	p0, cr0, [r13], #-4")			\
	TEST_COPROCESSOR("stc"two"	p0, cr0, [r13], {1}")			\
	TEST_COPROCESSOR("stc"two"l	p0, cr0, [r13, #4]")			\
	TEST_COPROCESSOR("stc"two"l	p0, cr0, [r13, #-4]")			\
	TEST_COPROCESSOR("stc"two"l	p0, cr0, [r13, #4]!")			\
	TEST_COPROCESSOR("stc"two"l	p0, cr0, [r13, #-4]!")			\
	TEST_COPROCESSOR("stc"two"l	p0, cr0, [r13], #4")			\
	TEST_COPROCESSOR("stc"two"l	p0, cr0, [r13], #-4")			\
	TEST_COPROCESSOR("stc"two"l	p0, cr0, [r13], {1}")			\
	TEST_COPROCESSOR("ldc"two"	p0, cr0, [r13, #4]")			\
	TEST_COPROCESSOR("ldc"two"	p0, cr0, [r13, #-4]")			\
	TEST_COPROCESSOR("ldc"two"	p0, cr0, [r13, #4]!")			\
	TEST_COPROCESSOR("ldc"two"	p0, cr0, [r13, #-4]!")			\
	TEST_COPROCESSOR("ldc"two"	p0, cr0, [r13], #4")			\
	TEST_COPROCESSOR("ldc"two"	p0, cr0, [r13], #-4")			\
	TEST_COPROCESSOR("ldc"two"	p0, cr0, [r13], {1}")			\
	TEST_COPROCESSOR("ldc"two"l	p0, cr0, [r13, #4]")			\
	TEST_COPROCESSOR("ldc"two"l	p0, cr0, [r13, #-4]")			\
	TEST_COPROCESSOR("ldc"two"l	p0, cr0, [r13, #4]!")			\
	TEST_COPROCESSOR("ldc"two"l	p0, cr0, [r13, #-4]!")			\
	TEST_COPROCESSOR("ldc"two"l	p0, cr0, [r13], #4")			\
	TEST_COPROCESSOR("ldc"two"l	p0, cr0, [r13], #-4")			\
	TEST_COPROCESSOR("ldc"two"l	p0, cr0, [r13], {1}")			\
										\
	TEST_COPROCESSOR( "stc"two"	p0, cr0, [r15, #4]")			\
	TEST_COPROCESSOR( "stc"two"	p0, cr0, [r15, #-4]")			\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##daf0001) "	@ stc"two"	0, cr0, [r15, #4]!")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##d2f0001) "	@ stc"two"	0, cr0, [r15, #-4]!")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##caf0001) "	@ stc"two"	0, cr0, [r15], #4")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##c2f0001) "	@ stc"two"	0, cr0, [r15], #-4")	\
	TEST_COPROCESSOR( "stc"two"	p0, cr0, [r15], {1}")			\
	TEST_COPROCESSOR( "stc"two"l	p0, cr0, [r15, #4]")			\
	TEST_COPROCESSOR( "stc"two"l	p0, cr0, [r15, #-4]")			\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##def0001) "	@ stc"two"l	0, cr0, [r15, #4]!")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##d6f0001) "	@ stc"two"l	0, cr0, [r15, #-4]!")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##cef0001) "	@ stc"two"l	0, cr0, [r15], #4")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##c6f0001) "	@ stc"two"l	0, cr0, [r15], #-4")	\
	TEST_COPROCESSOR( "stc"two"l	p0, cr0, [r15], {1}")			\
	TEST_COPROCESSOR( "ldc"two"	p0, cr0, [r15, #4]")			\
	TEST_COPROCESSOR( "ldc"two"	p0, cr0, [r15, #-4]")			\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##dbf0001) "	@ ldc"two"	0, cr0, [r15, #4]!")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##d3f0001) "	@ ldc"two"	0, cr0, [r15, #-4]!")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##cbf0001) "	@ ldc"two"	0, cr0, [r15], #4")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##c3f0001) "	@ ldc"two"	0, cr0, [r15], #-4")	\
	TEST_COPROCESSOR( "ldc"two"	p0, cr0, [r15], {1}")			\
	TEST_COPROCESSOR( "ldc"two"l	p0, cr0, [r15, #4]")			\
	TEST_COPROCESSOR( "ldc"two"l	p0, cr0, [r15, #-4]")			\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##dff0001) "	@ ldc"two"l	0, cr0, [r15, #4]!")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##d7f0001) "	@ ldc"two"l	0, cr0, [r15, #-4]!")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##cff0001) "	@ ldc"two"l	0, cr0, [r15], #4")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##c7f0001) "	@ ldc"two"l	0, cr0, [r15], #-4")	\
	TEST_COPROCESSOR( "ldc"two"l	p0, cr0, [r15], {1}")

#define COPROCESSOR_INSTRUCTIONS_MC_MR(two,cc)					\
										\
	TEST_COPROCESSOR( "mcrr"two"	p0, 15, r0, r14, cr0")			\
	TEST_COPROCESSOR( "mcrr"two"	p15, 0, r14, r0, cr15")			\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##c4f00f0) "	@ mcrr"two"	0, 15, r0, r15, cr0")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##c40ff0f) "	@ mcrr"two"	15, 0, r15, r0, cr15")	\
	TEST_COPROCESSOR( "mrrc"two"	p0, 15, r0, r14, cr0")			\
	TEST_COPROCESSOR( "mrrc"two"	p15, 0, r14, r0, cr15")			\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##c5f00f0) "	@ mrrc"two"	0, 15, r0, r15, cr0")	\
	TEST_UNSUPPORTED(__inst_arm(0x##cc##c50ff0f) "	@ mrrc"two"	15, 0, r15, r0, cr15")	\
	TEST_COPROCESSOR( "cdp"two"	p15, 15, cr15, cr15, cr15, 7")		\
	TEST_COPROCESSOR( "cdp"two"	p0, 0, cr0, cr0, cr0, 0")		\
	TEST_COPROCESSOR( "mcr"two"	p15, 7, r15, cr15, cr15, 7")		\
	TEST_COPROCESSOR( "mcr"two"	p0, 0, r0, cr0, cr0, 0")		\
	TEST_COPROCESSOR( "mrc"two"	p15, 7, r14, cr15, cr15, 7")		\
	TEST_COPROCESSOR( "mrc"two"	p0, 0, r0, cr0, cr0, 0")

	COPROCESSOR_INSTRUCTIONS_ST_LD("",e)
#if __LINUX_ARM_ARCH__ >= 5
	COPROCESSOR_INSTRUCTIONS_MC_MR("",e)
#endif
	TEST_UNSUPPORTED("svc	0")
	TEST_UNSUPPORTED("svc	0xffffff")

	TEST_UNSUPPORTED("svc	0")

	TEST_GROUP("Unconditional instruction")

#if __LINUX_ARM_ARCH__ >= 6
	TEST_UNSUPPORTED("srsda	sp, 0x13")
	TEST_UNSUPPORTED("srsdb	sp, 0x13")
	TEST_UNSUPPORTED("srsia	sp, 0x13")
	TEST_UNSUPPORTED("srsib	sp, 0x13")
	TEST_UNSUPPORTED("srsda	sp!, 0x13")
	TEST_UNSUPPORTED("srsdb	sp!, 0x13")
	TEST_UNSUPPORTED("srsia	sp!, 0x13")
	TEST_UNSUPPORTED("srsib	sp!, 0x13")

	TEST_UNSUPPORTED("rfeda	sp")
	TEST_UNSUPPORTED("rfedb	sp")
	TEST_UNSUPPORTED("rfeia	sp")
	TEST_UNSUPPORTED("rfeib	sp")
	TEST_UNSUPPORTED("rfeda	sp!")
	TEST_UNSUPPORTED("rfedb	sp!")
	TEST_UNSUPPORTED("rfeia	sp!")
	TEST_UNSUPPORTED("rfeib	sp!")
	TEST_UNSUPPORTED(__inst_arm(0xf81d0a00) "	@ rfeda	pc")
	TEST_UNSUPPORTED(__inst_arm(0xf91d0a00) "	@ rfedb	pc")
	TEST_UNSUPPORTED(__inst_arm(0xf89d0a00) "	@ rfeia	pc")
	TEST_UNSUPPORTED(__inst_arm(0xf99d0a00) "	@ rfeib	pc")
	TEST_UNSUPPORTED(__inst_arm(0xf83d0a00) "	@ rfeda	pc!")
	TEST_UNSUPPORTED(__inst_arm(0xf93d0a00) "	@ rfedb	pc!")
	TEST_UNSUPPORTED(__inst_arm(0xf8bd0a00) "	@ rfeia	pc!")
	TEST_UNSUPPORTED(__inst_arm(0xf9bd0a00) "	@ rfeib	pc!")
#endif /* __LINUX_ARM_ARCH__ >= 6 */

#if __LINUX_ARM_ARCH__ >= 6
	TEST_X(	"blx	__dummy_thumb_subroutine_even",
		".thumb				\n\t"
		".space 4			\n\t"
		".type __dummy_thumb_subroutine_even, %%function \n\t"
		"__dummy_thumb_subroutine_even:	\n\t"
		"mov	r0, pc			\n\t"
		"bx	lr			\n\t"
		".arm				\n\t"
	)
	TEST(	"blx	__dummy_thumb_subroutine_even")

	TEST_X(	"blx	__dummy_thumb_subroutine_odd",
		".thumb				\n\t"
		".space 2			\n\t"
		".type __dummy_thumb_subroutine_odd, %%function	\n\t"
		"__dummy_thumb_subroutine_odd:	\n\t"
		"mov	r0, pc			\n\t"
		"bx	lr			\n\t"
		".arm				\n\t"
	)
	TEST(	"blx	__dummy_thumb_subroutine_odd")
#endif /* __LINUX_ARM_ARCH__ >= 6 */

#if __LINUX_ARM_ARCH__ >= 5
	COPROCESSOR_INSTRUCTIONS_ST_LD("2",f)
#endif
#if __LINUX_ARM_ARCH__ >= 6
	COPROCESSOR_INSTRUCTIONS_MC_MR("2",f)
#endif

	TEST_GROUP("Miscellaneous instructions, memory hints, and Advanced SIMD instructions")

#if __LINUX_ARM_ARCH__ >= 6
	TEST_UNSUPPORTED("cps	0x13")
	TEST_UNSUPPORTED("cpsie	i")
	TEST_UNSUPPORTED("cpsid	i")
	TEST_UNSUPPORTED("cpsie	i,0x13")
	TEST_UNSUPPORTED("cpsid	i,0x13")
	TEST_UNSUPPORTED("setend	le")
	TEST_UNSUPPORTED("setend	be")
#endif

#if __LINUX_ARM_ARCH__ >= 7
	TEST_P("pli	[r",0,0b,", #16]")
	TEST(  "pli	[pc, #0]")
	TEST_RR("pli	[r",12,0b,", r",0, 16,"]")
	TEST_RR("pli	[r",0, 0b,", -r",12,16,", lsl #4]")
#endif

#if __LINUX_ARM_ARCH__ >= 5
	TEST_P("pld	[r",0,32,", #-16]")
	TEST(  "pld	[pc, #0]")
	TEST_PR("pld	[r",7, 24, ", r",0, 16,"]")
	TEST_PR("pld	[r",8, 24, ", -r",12,16,", lsl #4]")
#endif

#if __LINUX_ARM_ARCH__ >= 7
	TEST_SUPPORTED(  __inst_arm(0xf590f000) "	@ pldw [r0, #0]")
	TEST_SUPPORTED(  __inst_arm(0xf797f000) "	@ pldw	[r7, r0]")
	TEST_SUPPORTED(  __inst_arm(0xf798f18c) "	@ pldw	[r8, r12, lsl #3]");
#endif

#if __LINUX_ARM_ARCH__ >= 7
	TEST_UNSUPPORTED("clrex")
	TEST_UNSUPPORTED("dsb")
	TEST_UNSUPPORTED("dmb")
	TEST_UNSUPPORTED("isb")
#endif

	verbose("\n");
}

