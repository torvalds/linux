// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/probes/kprobes/test-thumb.c
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/opcodes.h>
#include <asm/probes.h>

#include "test-core.h"


#define TEST_ISA "16"

#define DONT_TEST_IN_ITBLOCK(tests)			\
	kprobe_test_flags |= TEST_FLAG_NO_ITBLOCK;	\
	tests						\
	kprobe_test_flags &= ~TEST_FLAG_NO_ITBLOCK;

#define CONDITION_INSTRUCTIONS(cc_pos, tests)		\
	kprobe_test_cc_position = cc_pos;		\
	DONT_TEST_IN_ITBLOCK(tests)			\
	kprobe_test_cc_position = 0;

#define TEST_ITBLOCK(code)				\
	kprobe_test_flags |= TEST_FLAG_FULL_ITBLOCK;	\
	TESTCASE_START(code)				\
	TEST_ARG_END("")				\
	"50:	nop			\n\t"		\
	"1:	"code"			\n\t"		\
	"	mov r1, #0x11		\n\t"		\
	"	mov r2, #0x22		\n\t"		\
	"	mov r3, #0x33		\n\t"		\
	"2:	nop			\n\t"		\
	TESTCASE_END					\
	kprobe_test_flags &= ~TEST_FLAG_FULL_ITBLOCK;

#define TEST_THUMB_TO_ARM_INTERWORK_P(code1, reg, val, code2)	\
	TESTCASE_START(code1 #reg code2)			\
	TEST_ARG_PTR(reg, val)					\
	TEST_ARG_REG(14, 99f+1)					\
	TEST_ARG_MEM(15, 3f)					\
	TEST_ARG_END("")					\
	"	nop			\n\t" /* To align 1f */	\
	"50:	nop			\n\t"			\
	"1:	"code1 #reg code2"	\n\t"			\
	"	bx	lr		\n\t"			\
	".arm				\n\t"			\
	"3:	adr	lr, 2f+1	\n\t"			\
	"	bx	lr		\n\t"			\
	".thumb				\n\t"			\
	"2:	nop			\n\t"			\
	TESTCASE_END


void kprobe_thumb16_test_cases(void)
{
	kprobe_test_flags = TEST_FLAG_NARROW_INSTR;

	TEST_GROUP("Shift (immediate), add, subtract, move, and compare")

	TEST_R(    "lsls	r7, r",0,VAL1,", #5")
	TEST_R(    "lsls	r0, r",7,VAL2,", #11")
	TEST_R(    "lsrs	r7, r",0,VAL1,", #5")
	TEST_R(    "lsrs	r0, r",7,VAL2,", #11")
	TEST_R(    "asrs	r7, r",0,VAL1,", #5")
	TEST_R(    "asrs	r0, r",7,VAL2,", #11")
	TEST_RR(   "adds	r2, r",0,VAL1,", r",7,VAL2,"")
	TEST_RR(   "adds	r5, r",7,VAL2,", r",0,VAL2,"")
	TEST_RR(   "subs	r2, r",0,VAL1,", r",7,VAL2,"")
	TEST_RR(   "subs	r5, r",7,VAL2,", r",0,VAL2,"")
	TEST_R(    "adds	r7, r",0,VAL1,", #5")
	TEST_R(    "adds	r0, r",7,VAL2,", #2")
	TEST_R(    "subs	r7, r",0,VAL1,", #5")
	TEST_R(    "subs	r0, r",7,VAL2,", #2")
	TEST(      "movs.n	r0, #0x5f")
	TEST(      "movs.n	r7, #0xa0")
	TEST_R(    "cmp.n	r",0,0x5e, ", #0x5f")
	TEST_R(    "cmp.n	r",5,0x15f,", #0x5f")
	TEST_R(    "cmp.n	r",7,0xa0, ", #0xa0")
	TEST_R(    "adds.n	r",0,VAL1,", #0x5f")
	TEST_R(    "adds.n	r",7,VAL2,", #0xa0")
	TEST_R(    "subs.n	r",0,VAL1,", #0x5f")
	TEST_R(    "subs.n	r",7,VAL2,", #0xa0")

	TEST_GROUP("16-bit Thumb data-processing instructions")

#define DATA_PROCESSING16(op,val)			\
	TEST_RR(   op"	r",0,VAL1,", r",7,val,"")	\
	TEST_RR(   op"	r",7,VAL2,", r",0,val,"")

	DATA_PROCESSING16("ands",0xf00f00ff)
	DATA_PROCESSING16("eors",0xf00f00ff)
	DATA_PROCESSING16("lsls",11)
	DATA_PROCESSING16("lsrs",11)
	DATA_PROCESSING16("asrs",11)
	DATA_PROCESSING16("adcs",VAL2)
	DATA_PROCESSING16("sbcs",VAL2)
	DATA_PROCESSING16("rors",11)
	DATA_PROCESSING16("tst",0xf00f00ff)
	TEST_R("rsbs	r",0,VAL1,", #0")
	TEST_R("rsbs	r",7,VAL2,", #0")
	DATA_PROCESSING16("cmp",0xf00f00ff)
	DATA_PROCESSING16("cmn",0xf00f00ff)
	DATA_PROCESSING16("orrs",0xf00f00ff)
	DATA_PROCESSING16("muls",VAL2)
	DATA_PROCESSING16("bics",0xf00f00ff)
	DATA_PROCESSING16("mvns",VAL2)

	TEST_GROUP("Special data instructions and branch and exchange")

	TEST_RR(  "add	r",0, VAL1,", r",7,VAL2,"")
	TEST_RR(  "add	r",3, VAL2,", r",8,VAL3,"")
	TEST_RR(  "add	r",8, VAL3,", r",0,VAL1,"")
	TEST_R(   "add	sp"        ", r",8,-8,  "")
	TEST_R(   "add	r",14,VAL1,", pc")
	TEST_BF_R("add	pc"        ", r",0,2f-1f-8,"")
	TEST_UNSUPPORTED(__inst_thumb16(0x44ff) "	@ add pc, pc")

	TEST_RR(  "cmp	r",3,VAL1,", r",8,VAL2,"")
	TEST_RR(  "cmp	r",8,VAL2,", r",0,VAL1,"")
	TEST_R(   "cmp	sp"       ", r",8,-8,  "")

	TEST_R(   "mov	r0, r",7,VAL2,"")
	TEST_R(   "mov	r3, r",8,VAL3,"")
	TEST_R(   "mov	r8, r",0,VAL1,"")
	TEST_P(   "mov	sp, r",8,-8,  "")
	TEST(     "mov	lr, pc")
	TEST_BF_R("mov	pc, r",0,2f,  "")

	TEST_BF_R("bx	r",0, 2f+1,"")
	TEST_BF_R("bx	r",14,2f+1,"")
	TESTCASE_START("bx	pc")
		TEST_ARG_REG(14, 99f+1)
		TEST_ARG_END("")
		"	nop			\n\t" /* To align the bx pc*/
		"50:	nop			\n\t"
		"1:	bx	pc		\n\t"
		"	bx	lr		\n\t"
		".arm				\n\t"
		"	adr	lr, 2f+1	\n\t"
		"	bx	lr		\n\t"
		".thumb				\n\t"
		"2:	nop			\n\t"
	TESTCASE_END

	TEST_BF_R("blx	r",0, 2f+1,"")
	TEST_BB_R("blx	r",14,2f+1,"")
	TEST_UNSUPPORTED(__inst_thumb16(0x47f8) "	@ blx pc")

	TEST_GROUP("Load from Literal Pool")

	TEST_X( "ldr	r0, 3f",
		".align					\n\t"
		"3:	.word	"__stringify(VAL1))
	TEST_X( "ldr	r7, 3f",
		".space 128				\n\t"
		".align					\n\t"
		"3:	.word	"__stringify(VAL2))

	TEST_GROUP("16-bit Thumb Load/store instructions")

	TEST_RPR("str	r",0, VAL1,", [r",1, 24,", r",2,  48,"]")
	TEST_RPR("str	r",7, VAL2,", [r",6, 24,", r",5,  48,"]")
	TEST_RPR("strh	r",0, VAL1,", [r",1, 24,", r",2,  48,"]")
	TEST_RPR("strh	r",7, VAL2,", [r",6, 24,", r",5,  48,"]")
	TEST_RPR("strb	r",0, VAL1,", [r",1, 24,", r",2,  48,"]")
	TEST_RPR("strb	r",7, VAL2,", [r",6, 24,", r",5,  48,"]")
	TEST_PR( "ldrsb	r0, [r",1, 24,", r",2,  48,"]")
	TEST_PR( "ldrsb	r7, [r",6, 24,", r",5,  50,"]")
	TEST_PR( "ldr	r0, [r",1, 24,", r",2,  48,"]")
	TEST_PR( "ldr	r7, [r",6, 24,", r",5,  48,"]")
	TEST_PR( "ldrh	r0, [r",1, 24,", r",2,  48,"]")
	TEST_PR( "ldrh	r7, [r",6, 24,", r",5,  50,"]")
	TEST_PR( "ldrb	r0, [r",1, 24,", r",2,  48,"]")
	TEST_PR( "ldrb	r7, [r",6, 24,", r",5,  50,"]")
	TEST_PR( "ldrsh	r0, [r",1, 24,", r",2,  48,"]")
	TEST_PR( "ldrsh	r7, [r",6, 24,", r",5,  50,"]")

	TEST_RP("str	r",0, VAL1,", [r",1, 24,", #120]")
	TEST_RP("str	r",7, VAL2,", [r",6, 24,", #120]")
	TEST_P( "ldr	r0, [r",1, 24,", #120]")
	TEST_P( "ldr	r7, [r",6, 24,", #120]")
	TEST_RP("strb	r",0, VAL1,", [r",1, 24,", #30]")
	TEST_RP("strb	r",7, VAL2,", [r",6, 24,", #30]")
	TEST_P( "ldrb	r0, [r",1, 24,", #30]")
	TEST_P( "ldrb	r7, [r",6, 24,", #30]")
	TEST_RP("strh	r",0, VAL1,", [r",1, 24,", #60]")
	TEST_RP("strh	r",7, VAL2,", [r",6, 24,", #60]")
	TEST_P( "ldrh	r0, [r",1, 24,", #60]")
	TEST_P( "ldrh	r7, [r",6, 24,", #60]")

	TEST_R( "str	r",0, VAL1,", [sp, #0]")
	TEST_R( "str	r",7, VAL2,", [sp, #160]")
	TEST(   "ldr	r0, [sp, #0]")
	TEST(   "ldr	r7, [sp, #160]")

	TEST_RP("str	r",0, VAL1,", [r",0, 24,"]")
	TEST_P( "ldr	r0, [r",0, 24,"]")

	TEST_GROUP("Generate PC-/SP-relative address")

	TEST("add	r0, pc, #4")
	TEST("add	r7, pc, #1020")
	TEST("add	r0, sp, #4")
	TEST("add	r7, sp, #1020")

	TEST_GROUP("Miscellaneous 16-bit instructions")

	TEST_UNSUPPORTED( "cpsie	i")
	TEST_UNSUPPORTED( "cpsid	i")
	TEST_UNSUPPORTED( "setend	le")
	TEST_UNSUPPORTED( "setend	be")

	TEST("add	sp, #"__stringify(TEST_MEMORY_SIZE)) /* Assumes TEST_MEMORY_SIZE < 0x400 */
	TEST("sub	sp, #0x7f*4")

DONT_TEST_IN_ITBLOCK(
	TEST_BF_R(  "cbnz	r",0,0, ", 2f")
	TEST_BF_R(  "cbz	r",2,-1,", 2f")
	TEST_BF_RX( "cbnz	r",4,1, ", 2f", SPACE_0x20)
	TEST_BF_RX( "cbz	r",7,0, ", 2f", SPACE_0x40)
)
	TEST_R("sxth	r0, r",7, HH1,"")
	TEST_R("sxth	r7, r",0, HH2,"")
	TEST_R("sxtb	r0, r",7, HH1,"")
	TEST_R("sxtb	r7, r",0, HH2,"")
	TEST_R("uxth	r0, r",7, HH1,"")
	TEST_R("uxth	r7, r",0, HH2,"")
	TEST_R("uxtb	r0, r",7, HH1,"")
	TEST_R("uxtb	r7, r",0, HH2,"")
	TEST_R("rev	r0, r",7, VAL1,"")
	TEST_R("rev	r7, r",0, VAL2,"")
	TEST_R("rev16	r0, r",7, VAL1,"")
	TEST_R("rev16	r7, r",0, VAL2,"")
	TEST_UNSUPPORTED(__inst_thumb16(0xba80) "")
	TEST_UNSUPPORTED(__inst_thumb16(0xbabf) "")
	TEST_R("revsh	r0, r",7, VAL1,"")
	TEST_R("revsh	r7, r",0, VAL2,"")

#define TEST_POPPC(code, offset)	\
	TESTCASE_START(code)		\
	TEST_ARG_PTR(13, offset)	\
	TEST_ARG_END("")		\
	TEST_BRANCH_F(code)		\
	TESTCASE_END

	TEST("push	{r0}")
	TEST("push	{r7}")
	TEST("push	{r14}")
	TEST("push	{r0-r7,r14}")
	TEST("push	{r0,r2,r4,r6,r14}")
	TEST("push	{r1,r3,r5,r7}")
	TEST("pop	{r0}")
	TEST("pop	{r7}")
	TEST("pop	{r0,r2,r4,r6}")
	TEST_POPPC("pop	{pc}",15*4)
	TEST_POPPC("pop	{r0-r7,pc}",7*4)
	TEST_POPPC("pop	{r1,r3,r5,r7,pc}",11*4)
	TEST_THUMB_TO_ARM_INTERWORK_P("pop	{pc}	@ ",13,15*4,"")
	TEST_THUMB_TO_ARM_INTERWORK_P("pop	{r0-r7,pc}	@ ",13,7*4,"")

	TEST_UNSUPPORTED("bkpt.n	0")
	TEST_UNSUPPORTED("bkpt.n	255")

	TEST_SUPPORTED("yield")
	TEST("sev")
	TEST("nop")
	TEST("wfi")
	TEST_SUPPORTED("wfe")
	TEST_UNSUPPORTED(__inst_thumb16(0xbf50) "") /* Unassigned hints */
	TEST_UNSUPPORTED(__inst_thumb16(0xbff0) "") /* Unassigned hints */

#define TEST_IT(code, code2)			\
	TESTCASE_START(code)			\
	TEST_ARG_END("")			\
	"50:	nop			\n\t"	\
	"1:	"code"			\n\t"	\
	"	"code2"			\n\t"	\
	"2:	nop			\n\t"	\
	TESTCASE_END

DONT_TEST_IN_ITBLOCK(
	TEST_IT("it	eq","moveq r0,#0")
	TEST_IT("it	vc","movvc r0,#0")
	TEST_IT("it	le","movle r0,#0")
	TEST_IT("ite	eq","moveq r0,#0\n\t  movne r1,#1")
	TEST_IT("itet	vc","movvc r0,#0\n\t  movvs r1,#1\n\t  movvc r2,#2")
	TEST_IT("itete	le","movle r0,#0\n\t  movgt r1,#1\n\t  movle r2,#2\n\t  movgt r3,#3")
	TEST_IT("itttt	le","movle r0,#0\n\t  movle r1,#1\n\t  movle r2,#2\n\t  movle r3,#3")
	TEST_IT("iteee	le","movle r0,#0\n\t  movgt r1,#1\n\t  movgt r2,#2\n\t  movgt r3,#3")
)

	TEST_GROUP("Load and store multiple")

	TEST_P("ldmia	r",4, 16*4,"!, {r0,r7}")
	TEST_P("ldmia	r",7, 16*4,"!, {r0-r6}")
	TEST_P("stmia	r",4, 16*4,"!, {r0,r7}")
	TEST_P("stmia	r",0, 16*4,"!, {r0-r7}")

	TEST_GROUP("Conditional branch and Supervisor Call instructions")

CONDITION_INSTRUCTIONS(8,
	TEST_BF("beq	2f")
	TEST_BB("bne	2b")
	TEST_BF("bgt	2f")
	TEST_BB("blt	2b")
)
	TEST_UNSUPPORTED(__inst_thumb16(0xde00) "")
	TEST_UNSUPPORTED(__inst_thumb16(0xdeff) "")
	TEST_UNSUPPORTED("svc	#0x00")
	TEST_UNSUPPORTED("svc	#0xff")

	TEST_GROUP("Unconditional branch")

	TEST_BF(  "b	2f")
	TEST_BB(  "b	2b")
	TEST_BF_X("b	2f", SPACE_0x400)
	TEST_BB_X("b	2b", SPACE_0x400)

	TEST_GROUP("Testing instructions in IT blocks")

	TEST_ITBLOCK("subs.n r0, r0")

	verbose("\n");
}


void kprobe_thumb32_test_cases(void)
{
	kprobe_test_flags = 0;

	TEST_GROUP("Load/store multiple")

	TEST_UNSUPPORTED("rfedb	sp")
	TEST_UNSUPPORTED("rfeia	sp")
	TEST_UNSUPPORTED("rfedb	sp!")
	TEST_UNSUPPORTED("rfeia	sp!")

	TEST_P(   "stmia	r",0, 16*4,", {r0,r8}")
	TEST_P(   "stmia	r",4, 16*4,", {r0-r12,r14}")
	TEST_P(   "stmia	r",7, 16*4,"!, {r8-r12,r14}")
	TEST_P(   "stmia	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")

	TEST_P(   "ldmia	r",0, 16*4,", {r0,r8}")
	TEST_P(   "ldmia	r",4, 0,   ", {r0-r12,r14}")
	TEST_BF_P("ldmia	r",5, 8*4, "!, {r6-r12,r15}")
	TEST_P(   "ldmia	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")
	TEST_BF_P("ldmia	r",14,14*4,"!, {r4,pc}")

	TEST_P(   "stmdb	r",0, 16*4,", {r0,r8}")
	TEST_P(   "stmdb	r",4, 16*4,", {r0-r12,r14}")
	TEST_P(   "stmdb	r",5, 16*4,"!, {r8-r12,r14}")
	TEST_P(   "stmdb	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")

	TEST_P(   "ldmdb	r",0, 16*4,", {r0,r8}")
	TEST_P(   "ldmdb	r",4, 16*4,", {r0-r12,r14}")
	TEST_BF_P("ldmdb	r",5, 16*4,"!, {r6-r12,r15}")
	TEST_P(   "ldmdb	r",12,16*4,"!, {r1,r3,r5,r7,r8-r11,r14}")
	TEST_BF_P("ldmdb	r",14,16*4,"!, {r4,pc}")

	TEST_P(   "stmdb	r",13,16*4,"!, {r3-r12,lr}")
	TEST_P(	  "stmdb	r",13,16*4,"!, {r3-r12}")
	TEST_P(   "stmdb	r",2, 16*4,", {r3-r12,lr}")
	TEST_P(   "stmdb	r",13,16*4,"!, {r2-r12,lr}")
	TEST_P(   "stmdb	r",0, 16*4,", {r0-r12}")
	TEST_P(   "stmdb	r",0, 16*4,", {r0-r12,lr}")

	TEST_BF_P("ldmia	r",13,5*4, "!, {r3-r12,pc}")
	TEST_P(	  "ldmia	r",13,5*4, "!, {r3-r12}")
	TEST_BF_P("ldmia	r",2, 5*4, "!, {r3-r12,pc}")
	TEST_BF_P("ldmia	r",13,4*4, "!, {r2-r12,pc}")
	TEST_P(   "ldmia	r",0, 16*4,", {r0-r12}")
	TEST_P(   "ldmia	r",0, 16*4,", {r0-r12,lr}")

	TEST_THUMB_TO_ARM_INTERWORK_P("ldmia	r",0,14*4,", {r12,pc}")
	TEST_THUMB_TO_ARM_INTERWORK_P("ldmia	r",13,2*4,", {r0-r12,pc}")

	TEST_UNSUPPORTED(__inst_thumb32(0xe88f0101) "	@ stmia	pc, {r0,r8}")
	TEST_UNSUPPORTED(__inst_thumb32(0xe92f5f00) "	@ stmdb	pc!, {r8-r12,r14}")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8bdc000) "	@ ldmia	r13!, {r14,pc}")
	TEST_UNSUPPORTED(__inst_thumb32(0xe93ec000) "	@ ldmdb	r14!, {r14,pc}")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8a73f00) "	@ stmia	r7!, {r8-r12,sp}")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8a79f00) "	@ stmia	r7!, {r8-r12,pc}")
	TEST_UNSUPPORTED(__inst_thumb32(0xe93e2010) "	@ ldmdb	r14!, {r4,sp}")

	TEST_GROUP("Load/store double or exclusive, table branch")

	TEST_P(  "ldrd	r0, r1, [r",1, 24,", #-16]")
	TEST(    "ldrd	r12, r14, [sp, #16]")
	TEST_P(  "ldrd	r1, r0, [r",7, 24,", #-16]!")
	TEST(    "ldrd	r14, r12, [sp, #16]!")
	TEST_P(  "ldrd	r1, r0, [r",7, 24,"], #16")
	TEST(    "ldrd	r7, r8, [sp], #-16")

	TEST_X( "ldrd	r12, r14, 3f",
		".align 3				\n\t"
		"3:	.word	"__stringify(VAL1)"	\n\t"
		"	.word	"__stringify(VAL2))

	TEST_UNSUPPORTED(__inst_thumb32(0xe9ffec04) "	@ ldrd	r14, r12, [pc, #16]!")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8ffec04) "	@ ldrd	r14, r12, [pc], #16")
	TEST_UNSUPPORTED(__inst_thumb32(0xe9d4d800) "	@ ldrd	sp, r8, [r4]")
	TEST_UNSUPPORTED(__inst_thumb32(0xe9d4f800) "	@ ldrd	pc, r8, [r4]")
	TEST_UNSUPPORTED(__inst_thumb32(0xe9d47d00) "	@ ldrd	r7, sp, [r4]")
	TEST_UNSUPPORTED(__inst_thumb32(0xe9d47f00) "	@ ldrd	r7, pc, [r4]")

	TEST_RRP("strd	r",0, VAL1,", r",1, VAL2,", [r",1, 24,", #-16]")
	TEST_RR( "strd	r",12,VAL2,", r",14,VAL1,", [sp, #16]")
	TEST_RRP("strd	r",1, VAL1,", r",0, VAL2,", [r",7, 24,", #-16]!")
	TEST_RR( "strd	r",14,VAL2,", r",12,VAL1,", [sp, #16]!")
	TEST_RRP("strd	r",1, VAL1,", r",0, VAL2,", [r",7, 24,"], #16")
	TEST_RR( "strd	r",7, VAL2,", r",8, VAL1,", [sp], #-16")
	TEST_RRP("strd	r",6, VAL1,", r",7, VAL2,", [r",13, TEST_MEMORY_SIZE,", #-"__stringify(MAX_STACK_SIZE)"]!")
	TEST_UNSUPPORTED("strd r6, r7, [r13, #-"__stringify(MAX_STACK_SIZE)"-8]!")
	TEST_RRP("strd	r",4, VAL1,", r",5, VAL2,", [r",14, TEST_MEMORY_SIZE,", #-"__stringify(MAX_STACK_SIZE)"-8]!")
	TEST_UNSUPPORTED(__inst_thumb32(0xe9efec04) "	@ strd	r14, r12, [pc, #16]!")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8efec04) "	@ strd	r14, r12, [pc], #16")

	TEST_RX("tbb	[pc, r",0, (9f-(1f+4)),"]",
		"9:			\n\t"
		".byte	(2f-1b-4)>>1	\n\t"
		".byte	(3f-1b-4)>>1	\n\t"
		"3:	mvn	r0, r0	\n\t"
		"2:	nop		\n\t")

	TEST_RX("tbb	[pc, r",4, (9f-(1f+4)+1),"]",
		"9:			\n\t"
		".byte	(2f-1b-4)>>1	\n\t"
		".byte	(3f-1b-4)>>1	\n\t"
		"3:	mvn	r0, r0	\n\t"
		"2:	nop		\n\t")

	TEST_RRX("tbb	[r",1,9f,", r",2,0,"]",
		"9:			\n\t"
		".byte	(2f-1b-4)>>1	\n\t"
		".byte	(3f-1b-4)>>1	\n\t"
		"3:	mvn	r0, r0	\n\t"
		"2:	nop		\n\t")

	TEST_RX("tbh	[pc, r",7, (9f-(1f+4))>>1,"]",
		"9:			\n\t"
		".short	(2f-1b-4)>>1	\n\t"
		".short	(3f-1b-4)>>1	\n\t"
		"3:	mvn	r0, r0	\n\t"
		"2:	nop		\n\t")

	TEST_RX("tbh	[pc, r",12, ((9f-(1f+4))>>1)+1,"]",
		"9:			\n\t"
		".short	(2f-1b-4)>>1	\n\t"
		".short	(3f-1b-4)>>1	\n\t"
		"3:	mvn	r0, r0	\n\t"
		"2:	nop		\n\t")

	TEST_RRX("tbh	[r",1,9f, ", r",14,1,"]",
		"9:			\n\t"
		".short	(2f-1b-4)>>1	\n\t"
		".short	(3f-1b-4)>>1	\n\t"
		"3:	mvn	r0, r0	\n\t"
		"2:	nop		\n\t")

	TEST_UNSUPPORTED(__inst_thumb32(0xe8d1f01f) "	@ tbh [r1, pc]")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8d1f01d) "	@ tbh [r1, sp]")
	TEST_UNSUPPORTED(__inst_thumb32(0xe8ddf012) "	@ tbh [sp, r2]")

	TEST_UNSUPPORTED("strexb	r0, r1, [r2]")
	TEST_UNSUPPORTED("strexh	r0, r1, [r2]")
	TEST_UNSUPPORTED("strexd	r0, r1, [r2]")
	TEST_UNSUPPORTED("ldrexb	r0, [r1]")
	TEST_UNSUPPORTED("ldrexh	r0, [r1]")
	TEST_UNSUPPORTED("ldrexd	r0, [r1]")

	TEST_GROUP("Data-processing (shifted register) and (modified immediate)")

#define _DATA_PROCESSING32_DNM(op,s,val)					\
	TEST_RR(op s".w	r0,  r",1, VAL1,", r",2, val, "")			\
	TEST_RR(op s"	r1,  r",1, VAL1,", r",2, val, ", lsl #3")		\
	TEST_RR(op s"	r2,  r",3, VAL1,", r",2, val, ", lsr #4")		\
	TEST_RR(op s"	r3,  r",3, VAL1,", r",2, val, ", asr #5")		\
	TEST_RR(op s"	r4,  r",5, VAL1,", r",2, N(val),", asr #6")		\
	TEST_RR(op s"	r5,  r",5, VAL1,", r",2, val, ", ror #7")		\
	TEST_RR(op s"	r8,  r",9, VAL1,", r",10,val, ", rrx")			\
	TEST_R( op s"	r0,  r",11,VAL1,", #0x00010001")			\
	TEST_R( op s"	r11, r",0, VAL1,", #0xf5000000")			\
	TEST_R( op s"	r7,  r",8, VAL2,", #0x000af000")

#define DATA_PROCESSING32_DNM(op,val)		\
	_DATA_PROCESSING32_DNM(op,"",val)	\
	_DATA_PROCESSING32_DNM(op,"s",val)

#define DATA_PROCESSING32_NM(op,val)					\
	TEST_RR(op".w	r",1, VAL1,", r",2, val, "")			\
	TEST_RR(op"	r",1, VAL1,", r",2, val, ", lsl #3")		\
	TEST_RR(op"	r",3, VAL1,", r",2, val, ", lsr #4")		\
	TEST_RR(op"	r",3, VAL1,", r",2, val, ", asr #5")		\
	TEST_RR(op"	r",5, VAL1,", r",2, N(val),", asr #6")		\
	TEST_RR(op"	r",5, VAL1,", r",2, val, ", ror #7")		\
	TEST_RR(op"	r",9, VAL1,", r",10,val, ", rrx")		\
	TEST_R( op"	r",11,VAL1,", #0x00010001")			\
	TEST_R( op"	r",0, VAL1,", #0xf5000000")			\
	TEST_R( op"	r",8, VAL2,", #0x000af000")

#define _DATA_PROCESSING32_DM(op,s,val)				\
	TEST_R( op s".w	r0,  r",14, val, "")			\
	TEST_R( op s"	r1,  r",12, val, ", lsl #3")		\
	TEST_R( op s"	r2,  r",11, val, ", lsr #4")		\
	TEST_R( op s"	r3,  r",10, val, ", asr #5")		\
	TEST_R( op s"	r4,  r",9, N(val),", asr #6")		\
	TEST_R( op s"	r5,  r",8, val, ", ror #7")		\
	TEST_R( op s"	r8,  r",7,val, ", rrx")			\
	TEST(   op s"	r0,  #0x00010001")			\
	TEST(   op s"	r11, #0xf5000000")			\
	TEST(   op s"	r7,  #0x000af000")			\
	TEST(   op s"	r4,  #0x00005a00")

#define DATA_PROCESSING32_DM(op,val)		\
	_DATA_PROCESSING32_DM(op,"",val)	\
	_DATA_PROCESSING32_DM(op,"s",val)

	DATA_PROCESSING32_DNM("and",0xf00f00ff)
	DATA_PROCESSING32_NM("tst",0xf00f00ff)
	DATA_PROCESSING32_DNM("bic",0xf00f00ff)
	DATA_PROCESSING32_DNM("orr",0xf00f00ff)
	DATA_PROCESSING32_DM("mov",VAL2)
	DATA_PROCESSING32_DNM("orn",0xf00f00ff)
	DATA_PROCESSING32_DM("mvn",VAL2)
	DATA_PROCESSING32_DNM("eor",0xf00f00ff)
	DATA_PROCESSING32_NM("teq",0xf00f00ff)
	DATA_PROCESSING32_DNM("add",VAL2)
	DATA_PROCESSING32_NM("cmn",VAL2)
	DATA_PROCESSING32_DNM("adc",VAL2)
	DATA_PROCESSING32_DNM("sbc",VAL2)
	DATA_PROCESSING32_DNM("sub",VAL2)
	DATA_PROCESSING32_NM("cmp",VAL2)
	DATA_PROCESSING32_DNM("rsb",VAL2)

	TEST_RR("pkhbt	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR("pkhbt	r14,r",12, HH1,", r",10,HH2,", lsl #2")
	TEST_RR("pkhtb	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR("pkhtb	r14,r",12, HH1,", r",10,HH2,", asr #2")

	TEST_UNSUPPORTED(__inst_thumb32(0xea170f0d) "	@ tst.w r7, sp")
	TEST_UNSUPPORTED(__inst_thumb32(0xea170f0f) "	@ tst.w r7, pc")
	TEST_UNSUPPORTED(__inst_thumb32(0xea1d0f07) "	@ tst.w sp, r7")
	TEST_UNSUPPORTED(__inst_thumb32(0xea1f0f07) "	@ tst.w pc, r7")
	TEST_UNSUPPORTED(__inst_thumb32(0xf01d1f08) "	@ tst sp, #0x00080008")
	TEST_UNSUPPORTED(__inst_thumb32(0xf01f1f08) "	@ tst pc, #0x00080008")

	TEST_UNSUPPORTED(__inst_thumb32(0xea970f0d) "	@ teq.w r7, sp")
	TEST_UNSUPPORTED(__inst_thumb32(0xea970f0f) "	@ teq.w r7, pc")
	TEST_UNSUPPORTED(__inst_thumb32(0xea9d0f07) "	@ teq.w sp, r7")
	TEST_UNSUPPORTED(__inst_thumb32(0xea9f0f07) "	@ teq.w pc, r7")
	TEST_UNSUPPORTED(__inst_thumb32(0xf09d1f08) "	@ tst sp, #0x00080008")
	TEST_UNSUPPORTED(__inst_thumb32(0xf09f1f08) "	@ tst pc, #0x00080008")

	TEST_UNSUPPORTED(__inst_thumb32(0xeb170f0d) "	@ cmn.w r7, sp")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb170f0f) "	@ cmn.w r7, pc")
	TEST_P("cmn.w	sp, r",7,0,"")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb1f0f07) "	@ cmn.w pc, r7")
	TEST(  "cmn	sp, #0x00080008")
	TEST_UNSUPPORTED(__inst_thumb32(0xf11f1f08) "	@ cmn pc, #0x00080008")

	TEST_UNSUPPORTED(__inst_thumb32(0xebb70f0d) "	@ cmp.w r7, sp")
	TEST_UNSUPPORTED(__inst_thumb32(0xebb70f0f) "	@ cmp.w r7, pc")
	TEST_P("cmp.w	sp, r",7,0,"")
	TEST_UNSUPPORTED(__inst_thumb32(0xebbf0f07) "	@ cmp.w pc, r7")
	TEST(  "cmp	sp, #0x00080008")
	TEST_UNSUPPORTED(__inst_thumb32(0xf1bf1f08) "	@ cmp pc, #0x00080008")

	TEST_UNSUPPORTED(__inst_thumb32(0xea5f070d) "	@ movs.w r7, sp")
	TEST_UNSUPPORTED(__inst_thumb32(0xea5f070f) "	@ movs.w r7, pc")
	TEST_UNSUPPORTED(__inst_thumb32(0xea5f0d07) "	@ movs.w sp, r7")
	TEST_UNSUPPORTED(__inst_thumb32(0xea4f0f07) "	@ mov.w  pc, r7")
	TEST_UNSUPPORTED(__inst_thumb32(0xf04f1d08) "	@ mov sp, #0x00080008")
	TEST_UNSUPPORTED(__inst_thumb32(0xf04f1f08) "	@ mov pc, #0x00080008")

	TEST_R("add.w	r0, sp, r",1, 4,"")
	TEST_R("adds	r0, sp, r",1, 4,", asl #3")
	TEST_R("add	r0, sp, r",1, 4,", asl #4")
	TEST_R("add	r0, sp, r",1, 16,", ror #1")
	TEST_R("add.w	sp, sp, r",1, 4,"")
	TEST_R("add	sp, sp, r",1, 4,", asl #3")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb0d1d01) "	@ add sp, sp, r1, asl #4")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb0d0d71) "	@ add sp, sp, r1, ror #1")
	TEST(  "add.w	r0, sp, #24")
	TEST(  "add.w	sp, sp, #24")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb0d0f01) "	@ add pc, sp, r1")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb0d000f) "	@ add r0, sp, pc")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb0d000d) "	@ add r0, sp, sp")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb0d0d0f) "	@ add sp, sp, pc")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb0d0d0d) "	@ add sp, sp, sp")

	TEST_R("sub.w	r0, sp, r",1, 4,"")
	TEST_R("subs	r0, sp, r",1, 4,", asl #3")
	TEST_R("sub	r0, sp, r",1, 4,", asl #4")
	TEST_R("sub	r0, sp, r",1, 16,", ror #1")
	TEST_R("sub.w	sp, sp, r",1, 4,"")
	TEST_R("sub	sp, sp, r",1, 4,", asl #3")
	TEST_UNSUPPORTED(__inst_thumb32(0xebad1d01) "	@ sub sp, sp, r1, asl #4")
	TEST_UNSUPPORTED(__inst_thumb32(0xebad0d71) "	@ sub sp, sp, r1, ror #1")
	TEST_UNSUPPORTED(__inst_thumb32(0xebad0f01) "	@ sub pc, sp, r1")
	TEST(  "sub.w	r0, sp, #24")
	TEST(  "sub.w	sp, sp, #24")

	TEST_UNSUPPORTED(__inst_thumb32(0xea02010f) "	@ and r1, r2, pc")
	TEST_UNSUPPORTED(__inst_thumb32(0xea0f0103) "	@ and r1, pc, r3")
	TEST_UNSUPPORTED(__inst_thumb32(0xea020f03) "	@ and pc, r2, r3")
	TEST_UNSUPPORTED(__inst_thumb32(0xea02010d) "	@ and r1, r2, sp")
	TEST_UNSUPPORTED(__inst_thumb32(0xea0d0103) "	@ and r1, sp, r3")
	TEST_UNSUPPORTED(__inst_thumb32(0xea020d03) "	@ and sp, r2, r3")
	TEST_UNSUPPORTED(__inst_thumb32(0xf00d1108) "	@ and r1, sp, #0x00080008")
	TEST_UNSUPPORTED(__inst_thumb32(0xf00f1108) "	@ and r1, pc, #0x00080008")
	TEST_UNSUPPORTED(__inst_thumb32(0xf0021d08) "	@ and sp, r8, #0x00080008")
	TEST_UNSUPPORTED(__inst_thumb32(0xf0021f08) "	@ and pc, r8, #0x00080008")

	TEST_UNSUPPORTED(__inst_thumb32(0xeb02010f) "	@ add r1, r2, pc")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb0f0103) "	@ add r1, pc, r3")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb020f03) "	@ add pc, r2, r3")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb02010d) "	@ add r1, r2, sp")
	TEST_SUPPORTED(  __inst_thumb32(0xeb0d0103) "	@ add r1, sp, r3")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb020d03) "	@ add sp, r2, r3")
	TEST_SUPPORTED(  __inst_thumb32(0xf10d1108) "	@ add r1, sp, #0x00080008")
	TEST_UNSUPPORTED(__inst_thumb32(0xf10d1f08) "	@ add pc, sp, #0x00080008")
	TEST_UNSUPPORTED(__inst_thumb32(0xf10f1108) "	@ add r1, pc, #0x00080008")
	TEST_UNSUPPORTED(__inst_thumb32(0xf1021d08) "	@ add sp, r8, #0x00080008")
	TEST_UNSUPPORTED(__inst_thumb32(0xf1021f08) "	@ add pc, r8, #0x00080008")

	TEST_UNSUPPORTED(__inst_thumb32(0xeaa00000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xeaf00000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb200000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xeb800000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xebe00000) "")

	TEST_UNSUPPORTED(__inst_thumb32(0xf0a00000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xf0c00000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xf0f00000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xf1200000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xf1800000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xf1e00000) "")

	TEST_GROUP("Coprocessor instructions")

	TEST_UNSUPPORTED(__inst_thumb32(0xec000000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xeff00000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xfc000000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xfff00000) "")

	TEST_GROUP("Data-processing (plain binary immediate)")

	TEST_R("addw	r0,  r",1, VAL1,", #0x123")
	TEST(  "addw	r14, sp, #0xf5a")
	TEST(  "addw	sp, sp, #0x20")
	TEST(  "addw	r7,  pc, #0x888")
	TEST_UNSUPPORTED(__inst_thumb32(0xf20f1f20) "	@ addw pc, pc, #0x120")
	TEST_UNSUPPORTED(__inst_thumb32(0xf20d1f20) "	@ addw pc, sp, #0x120")
	TEST_UNSUPPORTED(__inst_thumb32(0xf20f1d20) "	@ addw sp, pc, #0x120")
	TEST_UNSUPPORTED(__inst_thumb32(0xf2001d20) "	@ addw sp, r0, #0x120")

	TEST_R("subw	r0,  r",1, VAL1,", #0x123")
	TEST(  "subw	r14, sp, #0xf5a")
	TEST(  "subw	sp, sp, #0x20")
	TEST(  "subw	r7,  pc, #0x888")
	TEST_UNSUPPORTED(__inst_thumb32(0xf2af1f20) "	@ subw pc, pc, #0x120")
	TEST_UNSUPPORTED(__inst_thumb32(0xf2ad1f20) "	@ subw pc, sp, #0x120")
	TEST_UNSUPPORTED(__inst_thumb32(0xf2af1d20) "	@ subw sp, pc, #0x120")
	TEST_UNSUPPORTED(__inst_thumb32(0xf2a01d20) "	@ subw sp, r0, #0x120")

	TEST("movw	r0, #0")
	TEST("movw	r0, #0xffff")
	TEST("movw	lr, #0xffff")
	TEST_UNSUPPORTED(__inst_thumb32(0xf2400d00) "	@ movw sp, #0")
	TEST_UNSUPPORTED(__inst_thumb32(0xf2400f00) "	@ movw pc, #0")

	TEST_R("movt	r",0, VAL1,", #0")
	TEST_R("movt	r",0, VAL2,", #0xffff")
	TEST_R("movt	r",14,VAL1,", #0xffff")
	TEST_UNSUPPORTED(__inst_thumb32(0xf2c00d00) "	@ movt sp, #0")
	TEST_UNSUPPORTED(__inst_thumb32(0xf2c00f00) "	@ movt pc, #0")

	TEST_R(     "ssat	r0, #24, r",0,   VAL1,"")
	TEST_R(     "ssat	r14, #24, r",12, VAL2,"")
	TEST_R(     "ssat	r0, #24, r",0,   VAL1,", lsl #8")
	TEST_R(     "ssat	r14, #24, r",12, VAL2,", asr #8")
	TEST_UNSUPPORTED(__inst_thumb32(0xf30c0d17) "	@ ssat	sp, #24, r12")
	TEST_UNSUPPORTED(__inst_thumb32(0xf30c0f17) "	@ ssat	pc, #24, r12")
	TEST_UNSUPPORTED(__inst_thumb32(0xf30d0c17) "	@ ssat	r12, #24, sp")
	TEST_UNSUPPORTED(__inst_thumb32(0xf30f0c17) "	@ ssat	r12, #24, pc")

	TEST_R(     "usat	r0, #24, r",0,   VAL1,"")
	TEST_R(     "usat	r14, #24, r",12, VAL2,"")
	TEST_R(     "usat	r0, #24, r",0,   VAL1,", lsl #8")
	TEST_R(     "usat	r14, #24, r",12, VAL2,", asr #8")
	TEST_UNSUPPORTED(__inst_thumb32(0xf38c0d17) "	@ usat	sp, #24, r12")
	TEST_UNSUPPORTED(__inst_thumb32(0xf38c0f17) "	@ usat	pc, #24, r12")
	TEST_UNSUPPORTED(__inst_thumb32(0xf38d0c17) "	@ usat	r12, #24, sp")
	TEST_UNSUPPORTED(__inst_thumb32(0xf38f0c17) "	@ usat	r12, #24, pc")

	TEST_R(     "ssat16	r0, #12, r",0,   HH1,"")
	TEST_R(     "ssat16	r14, #12, r",12, HH2,"")
	TEST_UNSUPPORTED(__inst_thumb32(0xf32c0d0b) "	@ ssat16	sp, #12, r12")
	TEST_UNSUPPORTED(__inst_thumb32(0xf32c0f0b) "	@ ssat16	pc, #12, r12")
	TEST_UNSUPPORTED(__inst_thumb32(0xf32d0c0b) "	@ ssat16	r12, #12, sp")
	TEST_UNSUPPORTED(__inst_thumb32(0xf32f0c0b) "	@ ssat16	r12, #12, pc")

	TEST_R(     "usat16	r0, #12, r",0,   HH1,"")
	TEST_R(     "usat16	r14, #12, r",12, HH2,"")
	TEST_UNSUPPORTED(__inst_thumb32(0xf3ac0d0b) "	@ usat16	sp, #12, r12")
	TEST_UNSUPPORTED(__inst_thumb32(0xf3ac0f0b) "	@ usat16	pc, #12, r12")
	TEST_UNSUPPORTED(__inst_thumb32(0xf3ad0c0b) "	@ usat16	r12, #12, sp")
	TEST_UNSUPPORTED(__inst_thumb32(0xf3af0c0b) "	@ usat16	r12, #12, pc")

	TEST_R(     "sbfx	r0, r",0  , VAL1,", #0, #31")
	TEST_R(     "sbfx	r14, r",12, VAL2,", #8, #16")
	TEST_R(     "sbfx	r4, r",10,  VAL1,", #16, #15")
	TEST_UNSUPPORTED(__inst_thumb32(0xf34c2d0f) "	@ sbfx	sp, r12, #8, #16")
	TEST_UNSUPPORTED(__inst_thumb32(0xf34c2f0f) "	@ sbfx	pc, r12, #8, #16")
	TEST_UNSUPPORTED(__inst_thumb32(0xf34d2c0f) "	@ sbfx	r12, sp, #8, #16")
	TEST_UNSUPPORTED(__inst_thumb32(0xf34f2c0f) "	@ sbfx	r12, pc, #8, #16")

	TEST_R(     "ubfx	r0, r",0  , VAL1,", #0, #31")
	TEST_R(     "ubfx	r14, r",12, VAL2,", #8, #16")
	TEST_R(     "ubfx	r4, r",10,  VAL1,", #16, #15")
	TEST_UNSUPPORTED(__inst_thumb32(0xf3cc2d0f) "	@ ubfx	sp, r12, #8, #16")
	TEST_UNSUPPORTED(__inst_thumb32(0xf3cc2f0f) "	@ ubfx	pc, r12, #8, #16")
	TEST_UNSUPPORTED(__inst_thumb32(0xf3cd2c0f) "	@ ubfx	r12, sp, #8, #16")
	TEST_UNSUPPORTED(__inst_thumb32(0xf3cf2c0f) "	@ ubfx	r12, pc, #8, #16")

	TEST_R(     "bfc	r",0, VAL1,", #4, #20")
	TEST_R(     "bfc	r",14,VAL2,", #4, #20")
	TEST_R(     "bfc	r",7, VAL1,", #0, #31")
	TEST_R(     "bfc	r",8, VAL2,", #0, #31")
	TEST_UNSUPPORTED(__inst_thumb32(0xf36f0d1e) "	@ bfc	sp, #0, #31")
	TEST_UNSUPPORTED(__inst_thumb32(0xf36f0f1e) "	@ bfc	pc, #0, #31")

	TEST_RR(    "bfi	r",0, VAL1,", r",0  , VAL2,", #0, #31")
	TEST_RR(    "bfi	r",12,VAL1,", r",14 , VAL2,", #4, #20")
	TEST_UNSUPPORTED(__inst_thumb32(0xf36e1d17) "	@ bfi	sp, r14, #4, #20")
	TEST_UNSUPPORTED(__inst_thumb32(0xf36e1f17) "	@ bfi	pc, r14, #4, #20")
	TEST_UNSUPPORTED(__inst_thumb32(0xf36d1e17) "	@ bfi	r14, sp, #4, #20")

	TEST_GROUP("Branches and miscellaneous control")

CONDITION_INSTRUCTIONS(22,
	TEST_BF("beq.w	2f")
	TEST_BB("bne.w	2b")
	TEST_BF("bgt.w	2f")
	TEST_BB("blt.w	2b")
	TEST_BF_X("bpl.w	2f", SPACE_0x1000)
)

	TEST_UNSUPPORTED("msr	cpsr, r0")
	TEST_UNSUPPORTED("msr	cpsr_f, r1")
	TEST_UNSUPPORTED("msr	spsr, r2")

	TEST_UNSUPPORTED("cpsie.w	i")
	TEST_UNSUPPORTED("cpsid.w	i")
	TEST_UNSUPPORTED("cps	0x13")

	TEST_SUPPORTED("yield.w")
	TEST("sev.w")
	TEST("nop.w")
	TEST("wfi.w")
	TEST_SUPPORTED("wfe.w")
	TEST_UNSUPPORTED("dbg.w	#0")

	TEST_UNSUPPORTED("clrex")
	TEST_UNSUPPORTED("dsb")
	TEST_UNSUPPORTED("dmb")
	TEST_UNSUPPORTED("isb")

	TEST_UNSUPPORTED("bxj	r0")

	TEST_UNSUPPORTED("subs	pc, lr, #4")

	TEST_RMASKED("mrs	r",0,~PSR_IGNORE_BITS,", cpsr")
	TEST_RMASKED("mrs	r",14,~PSR_IGNORE_BITS,", cpsr")
	TEST_UNSUPPORTED(__inst_thumb32(0xf3ef8d00) "	@ mrs	sp, spsr")
	TEST_UNSUPPORTED(__inst_thumb32(0xf3ef8f00) "	@ mrs	pc, spsr")
	TEST_UNSUPPORTED("mrs	r0, spsr")
	TEST_UNSUPPORTED("mrs	lr, spsr")

	TEST_UNSUPPORTED(__inst_thumb32(0xf7f08000) " @ smc #0")

	TEST_UNSUPPORTED(__inst_thumb32(0xf7f0a000) " @ undefeined")

	TEST_BF(  "b.w	2f")
	TEST_BB(  "b.w	2b")
	TEST_BF_X("b.w	2f", SPACE_0x1000)

	TEST_BF(  "bl.w	2f")
	TEST_BB(  "bl.w	2b")
	TEST_BB_X("bl.w	2b", SPACE_0x1000)

	TEST_X(	"blx	__dummy_arm_subroutine",
		".arm				\n\t"
		".align				\n\t"
		".type __dummy_arm_subroutine, %%function \n\t"
		"__dummy_arm_subroutine:	\n\t"
		"mov	r0, pc			\n\t"
		"bx	lr			\n\t"
		".thumb				\n\t"
	)
	TEST(	"blx	__dummy_arm_subroutine")

	TEST_GROUP("Store single data item")

#define SINGLE_STORE(size)							\
	TEST_RP( "str"size"	r",0, VAL1,", [r",11,-1024,", #1024]")		\
	TEST_RP( "str"size"	r",14,VAL2,", [r",1, -1024,", #1080]")		\
	TEST_RP( "str"size"	r",0, VAL1,", [r",11,256,  ", #-120]")		\
	TEST_RP( "str"size"	r",14,VAL2,", [r",1, 256,  ", #-128]")		\
	TEST_RP( "str"size"	r",0, VAL1,", [r",11,24,  "], #120")		\
	TEST_RP( "str"size"	r",14,VAL2,", [r",1, 24,  "], #128")		\
	TEST_RP( "str"size"	r",0, VAL1,", [r",11,24,  "], #-120")		\
	TEST_RP( "str"size"	r",14,VAL2,", [r",1, 24,  "], #-128")		\
	TEST_RP( "str"size"	r",0, VAL1,", [r",11,24,   ", #120]!")		\
	TEST_RP( "str"size"	r",14,VAL2,", [r",1, 24,   ", #128]!")		\
	TEST_RP( "str"size"	r",0, VAL1,", [r",11,256,  ", #-120]!")		\
	TEST_RP( "str"size"	r",14,VAL2,", [r",1, 256,  ", #-128]!")		\
	TEST_RPR("str"size".w	r",0, VAL1,", [r",1, 0,", r",2, 4,"]")		\
	TEST_RPR("str"size"	r",14,VAL2,", [r",10,0,", r",11,4,", lsl #1]")	\
	TEST_UNSUPPORTED("str"size"	r0, [r13, r1]")				\
	TEST_R(  "str"size".w	r",7, VAL1,", [sp, #24]")			\
	TEST_RP( "str"size".w	r",0, VAL2,", [r",0,0, "]")			\
	TEST_RP( "str"size"	r",6, VAL1,", [r",13, TEST_MEMORY_SIZE,", #-"__stringify(MAX_STACK_SIZE)"]!") \
	TEST_UNSUPPORTED("str"size"	r6, [r13, #-"__stringify(MAX_STACK_SIZE)"-8]!")			\
	TEST_RP( "str"size"	r",4, VAL2,", [r",12, TEST_MEMORY_SIZE,", #-"__stringify(MAX_STACK_SIZE)"-8]!") \
	TEST_UNSUPPORTED("str"size"t	r0, [r1, #4]")

	SINGLE_STORE("b")
	SINGLE_STORE("h")
	SINGLE_STORE("")

	TEST_UNSUPPORTED(__inst_thumb32(0xf801000d) "	@ strb	r0, [r1, r13]")
	TEST_UNSUPPORTED(__inst_thumb32(0xf821000d) "	@ strh	r0, [r1, r13]")
	TEST_UNSUPPORTED(__inst_thumb32(0xf841000d) "	@ str	r0, [r1, r13]")

	TEST("str	sp, [sp]")
	TEST_UNSUPPORTED(__inst_thumb32(0xf8cfe000) "	@ str	r14, [pc]")
	TEST_UNSUPPORTED(__inst_thumb32(0xf8cef000) "	@ str	pc, [r14]")

	TEST_GROUP("Advanced SIMD element or structure load/store instructions")

	TEST_UNSUPPORTED(__inst_thumb32(0xf9000000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xf92fffff) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xf9800000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xf9efffff) "")

	TEST_GROUP("Load single data item and memory hints")

#define SINGLE_LOAD(size)						\
	TEST_P( "ldr"size"	r0, [r",11,-1024, ", #1024]")		\
	TEST_P( "ldr"size"	r14, [r",1, -1024,", #1080]")		\
	TEST_P( "ldr"size"	r0, [r",11,256,   ", #-120]")		\
	TEST_P( "ldr"size"	r14, [r",1, 256,  ", #-128]")		\
	TEST_P( "ldr"size"	r0, [r",11,24,   "], #120")		\
	TEST_P( "ldr"size"	r14, [r",1, 24,  "], #128")		\
	TEST_P( "ldr"size"	r0, [r",11,24,   "], #-120")		\
	TEST_P( "ldr"size"	r14, [r",1,24,   "], #-128")		\
	TEST_P( "ldr"size"	r0, [r",11,24,    ", #120]!")		\
	TEST_P( "ldr"size"	r14, [r",1, 24,   ", #128]!")		\
	TEST_P( "ldr"size"	r0, [r",11,256,   ", #-120]!")		\
	TEST_P( "ldr"size"	r14, [r",1, 256,  ", #-128]!")		\
	TEST_PR("ldr"size".w	r0, [r",1, 0,", r",2, 4,"]")		\
	TEST_PR("ldr"size"	r14, [r",10,0,", r",11,4,", lsl #1]")	\
	TEST_X( "ldr"size".w	r0, 3f",				\
		".align 3				\n\t"		\
		"3:	.word	"__stringify(VAL1))			\
	TEST_X( "ldr"size".w	r14, 3f",				\
		".align 3				\n\t"		\
		"3:	.word	"__stringify(VAL2))			\
	TEST(   "ldr"size".w	r7, 3b")				\
	TEST(   "ldr"size".w	r7, [sp, #24]")				\
	TEST_P( "ldr"size".w	r0, [r",0,0, "]")			\
	TEST_UNSUPPORTED("ldr"size"t	r0, [r1, #4]")

	SINGLE_LOAD("b")
	SINGLE_LOAD("sb")
	SINGLE_LOAD("h")
	SINGLE_LOAD("sh")
	SINGLE_LOAD("")

	TEST_BF_P("ldr	pc, [r",14, 15*4,"]")
	TEST_P(   "ldr	sp, [r",14, 13*4,"]")
	TEST_BF_R("ldr	pc, [sp, r",14, 15*4,"]")
	TEST_R(   "ldr	sp, [sp, r",14, 13*4,"]")
	TEST_THUMB_TO_ARM_INTERWORK_P("ldr	pc, [r",0,0,", #15*4]")
	TEST_SUPPORTED("ldr	sp, 99f")
	TEST_SUPPORTED("ldr	pc, 99f")

	TEST_UNSUPPORTED(__inst_thumb32(0xf854700d) "	@ ldr	r7, [r4, sp]")
	TEST_UNSUPPORTED(__inst_thumb32(0xf854700f) "	@ ldr	r7, [r4, pc]")
	TEST_UNSUPPORTED(__inst_thumb32(0xf814700d) "	@ ldrb	r7, [r4, sp]")
	TEST_UNSUPPORTED(__inst_thumb32(0xf814700f) "	@ ldrb	r7, [r4, pc]")
	TEST_UNSUPPORTED(__inst_thumb32(0xf89fd004) "	@ ldrb	sp, 99f")
	TEST_UNSUPPORTED(__inst_thumb32(0xf814d008) "	@ ldrb	sp, [r4, r8]")
	TEST_UNSUPPORTED(__inst_thumb32(0xf894d000) "	@ ldrb	sp, [r4]")

	TEST_UNSUPPORTED(__inst_thumb32(0xf8600000) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_thumb32(0xf9ffffff) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_thumb32(0xf9500000) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_thumb32(0xf95fffff) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_thumb32(0xf8000800) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_thumb32(0xf97ffaff) "") /* Unallocated space */

	TEST(   "pli	[pc, #4]")
	TEST(   "pli	[pc, #-4]")
	TEST(   "pld	[pc, #4]")
	TEST(   "pld	[pc, #-4]")

	TEST_P( "pld	[r",0,-1024,", #1024]")
	TEST(   __inst_thumb32(0xf8b0f400) "	@ pldw	[r0, #1024]")
	TEST_P( "pli	[r",4, 0b,", #1024]")
	TEST_P( "pld	[r",7, 120,", #-120]")
	TEST(   __inst_thumb32(0xf837fc78) "	@ pldw	[r7, #-120]")
	TEST_P( "pli	[r",11,120,", #-120]")
	TEST(   "pld	[sp, #0]")

	TEST_PR("pld	[r",7, 24, ", r",0, 16,"]")
	TEST_PR("pld	[r",8, 24, ", r",12,16,", lsl #3]")
	TEST_SUPPORTED(__inst_thumb32(0xf837f000) "	@ pldw	[r7, r0]")
	TEST_SUPPORTED(__inst_thumb32(0xf838f03c) "	@ pldw	[r8, r12, lsl #3]");
	TEST_RR("pli	[r",12,0b,", r",0, 16,"]")
	TEST_RR("pli	[r",0, 0b,", r",12,16,", lsl #3]")
	TEST_R( "pld	[sp, r",1, 16,"]")
	TEST_UNSUPPORTED(__inst_thumb32(0xf817f00d) "  @pld	[r7, sp]")
	TEST_UNSUPPORTED(__inst_thumb32(0xf817f00f) "  @pld	[r7, pc]")

	TEST_GROUP("Data-processing (register)")

#define SHIFTS32(op)					\
	TEST_RR(op"	r0,  r",1, VAL1,", r",2, 3, "")	\
	TEST_RR(op"	r14, r",12,VAL2,", r",11,10,"")

	SHIFTS32("lsl")
	SHIFTS32("lsls")
	SHIFTS32("lsr")
	SHIFTS32("lsrs")
	SHIFTS32("asr")
	SHIFTS32("asrs")
	SHIFTS32("ror")
	SHIFTS32("rors")

	TEST_UNSUPPORTED(__inst_thumb32(0xfa01ff02) "	@ lsl	pc, r1, r2")
	TEST_UNSUPPORTED(__inst_thumb32(0xfa01fd02) "	@ lsl	sp, r1, r2")
	TEST_UNSUPPORTED(__inst_thumb32(0xfa0ff002) "	@ lsl	r0, pc, r2")
	TEST_UNSUPPORTED(__inst_thumb32(0xfa0df002) "	@ lsl	r0, sp, r2")
	TEST_UNSUPPORTED(__inst_thumb32(0xfa01f00f) "	@ lsl	r0, r1, pc")
	TEST_UNSUPPORTED(__inst_thumb32(0xfa01f00d) "	@ lsl	r0, r1, sp")

	TEST_RR(    "sxtah	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "sxtah	r14,r",12, HH2,", r",10,HH1,", ror #8")
	TEST_R(     "sxth	r8, r",7,  HH1,"")

	TEST_UNSUPPORTED(__inst_thumb32(0xfa0fff87) "	@ sxth	pc, r7");
	TEST_UNSUPPORTED(__inst_thumb32(0xfa0ffd87) "	@ sxth	sp, r7");
	TEST_UNSUPPORTED(__inst_thumb32(0xfa0ff88f) "	@ sxth	r8, pc");
	TEST_UNSUPPORTED(__inst_thumb32(0xfa0ff88d) "	@ sxth	r8, sp");

	TEST_RR(    "uxtah	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uxtah	r14,r",12, HH2,", r",10,HH1,", ror #8")
	TEST_R(     "uxth	r8, r",7,  HH1,"")

	TEST_RR(    "sxtab16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "sxtab16	r14,r",12, HH2,", r",10,HH1,", ror #8")
	TEST_R(     "sxtb16	r8, r",7,  HH1,"")

	TEST_RR(    "uxtab16	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uxtab16	r14,r",12, HH2,", r",10,HH1,", ror #8")
	TEST_R(     "uxtb16	r8, r",7,  HH1,"")

	TEST_RR(    "sxtab	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "sxtab	r14,r",12, HH2,", r",10,HH1,", ror #8")
	TEST_R(     "sxtb	r8, r",7,  HH1,"")

	TEST_RR(    "uxtab	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "uxtab	r14,r",12, HH2,", r",10,HH1,", ror #8")
	TEST_R(     "uxtb	r8, r",7,  HH1,"")

	TEST_UNSUPPORTED(__inst_thumb32(0xfa6000f0) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xfa7fffff) "")

#define PARALLEL_ADD_SUB(op)					\
	TEST_RR(  op"add16	r0, r",0,  HH1,", r",1, HH2,"")	\
	TEST_RR(  op"add16	r14, r",12,HH2,", r",10,HH1,"")	\
	TEST_RR(  op"asx	r0, r",0,  HH1,", r",1, HH2,"")	\
	TEST_RR(  op"asx	r14, r",12,HH2,", r",10,HH1,"")	\
	TEST_RR(  op"sax	r0, r",0,  HH1,", r",1, HH2,"")	\
	TEST_RR(  op"sax	r14, r",12,HH2,", r",10,HH1,"")	\
	TEST_RR(  op"sub16	r0, r",0,  HH1,", r",1, HH2,"")	\
	TEST_RR(  op"sub16	r14, r",12,HH2,", r",10,HH1,"")	\
	TEST_RR(  op"add8	r0, r",0,  HH1,", r",1, HH2,"")	\
	TEST_RR(  op"add8	r14, r",12,HH2,", r",10,HH1,"")	\
	TEST_RR(  op"sub8	r0, r",0,  HH1,", r",1, HH2,"")	\
	TEST_RR(  op"sub8	r14, r",12,HH2,", r",10,HH1,"")

	TEST_GROUP("Parallel addition and subtraction, signed")

	PARALLEL_ADD_SUB("s")
	PARALLEL_ADD_SUB("q")
	PARALLEL_ADD_SUB("sh")

	TEST_GROUP("Parallel addition and subtraction, unsigned")

	PARALLEL_ADD_SUB("u")
	PARALLEL_ADD_SUB("uq")
	PARALLEL_ADD_SUB("uh")

	TEST_GROUP("Miscellaneous operations")

	TEST_RR("qadd	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR("qadd	lr, r",9, VAL2,", r",8, VAL1,"")
	TEST_RR("qsub	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR("qsub	lr, r",9, VAL2,", r",8, VAL1,"")
	TEST_RR("qdadd	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR("qdadd	lr, r",9, VAL2,", r",8, VAL1,"")
	TEST_RR("qdsub	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR("qdsub	lr, r",9, VAL2,", r",8, VAL1,"")

	TEST_R("rev.w	r0, r",0,   VAL1,"")
	TEST_R("rev	r14, r",12, VAL2,"")
	TEST_R("rev16.w	r0, r",0,   VAL1,"")
	TEST_R("rev16	r14, r",12, VAL2,"")
	TEST_R("rbit	r0, r",0,   VAL1,"")
	TEST_R("rbit	r14, r",12, VAL2,"")
	TEST_R("revsh.w	r0, r",0,   VAL1,"")
	TEST_R("revsh	r14, r",12, VAL2,"")

	TEST_UNSUPPORTED(__inst_thumb32(0xfa9cff8c) "	@ rev	pc, r12");
	TEST_UNSUPPORTED(__inst_thumb32(0xfa9cfd8c) "	@ rev	sp, r12");
	TEST_UNSUPPORTED(__inst_thumb32(0xfa9ffe8f) "	@ rev	r14, pc");
	TEST_UNSUPPORTED(__inst_thumb32(0xfa9dfe8d) "	@ rev	r14, sp");

	TEST_RR("sel	r0, r",0,  VAL1,", r",1, VAL2,"")
	TEST_RR("sel	r14, r",12,VAL1,", r",10, VAL2,"")

	TEST_R("clz	r0, r",0, 0x0,"")
	TEST_R("clz	r7, r",14,0x1,"")
	TEST_R("clz	lr, r",7, 0xffffffff,"")

	TEST_UNSUPPORTED(__inst_thumb32(0xfa80f030) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_thumb32(0xfaffff7f) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_thumb32(0xfab0f000) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_thumb32(0xfaffff7f) "") /* Unallocated space */

	TEST_GROUP("Multiply, multiply accumulate, and absolute difference operations")

	TEST_RR(    "mul	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "mul	r7, r",8, VAL2,", r",9, VAL2,"")
	TEST_UNSUPPORTED(__inst_thumb32(0xfb08ff09) "	@ mul	pc, r8, r9")
	TEST_UNSUPPORTED(__inst_thumb32(0xfb08fd09) "	@ mul	sp, r8, r9")
	TEST_UNSUPPORTED(__inst_thumb32(0xfb0ff709) "	@ mul	r7, pc, r9")
	TEST_UNSUPPORTED(__inst_thumb32(0xfb0df709) "	@ mul	r7, sp, r9")
	TEST_UNSUPPORTED(__inst_thumb32(0xfb08f70f) "	@ mul	r7, r8, pc")
	TEST_UNSUPPORTED(__inst_thumb32(0xfb08f70d) "	@ mul	r7, r8, sp")

	TEST_RRR(   "mla	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(   "mla	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_UNSUPPORTED(__inst_thumb32(0xfb08af09) "	@ mla	pc, r8, r9, r10");
	TEST_UNSUPPORTED(__inst_thumb32(0xfb08ad09) "	@ mla	sp, r8, r9, r10");
	TEST_UNSUPPORTED(__inst_thumb32(0xfb0fa709) "	@ mla	r7, pc, r9, r10");
	TEST_UNSUPPORTED(__inst_thumb32(0xfb0da709) "	@ mla	r7, sp, r9, r10");
	TEST_UNSUPPORTED(__inst_thumb32(0xfb08a70f) "	@ mla	r7, r8, pc, r10");
	TEST_UNSUPPORTED(__inst_thumb32(0xfb08a70d) "	@ mla	r7, r8, sp, r10");
	TEST_UNSUPPORTED(__inst_thumb32(0xfb08d709) "	@ mla	r7, r8, r9, sp");

	TEST_RRR(   "mls	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(   "mls	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")

	TEST_RRR(   "smlabb	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(   "smlabb	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RRR(   "smlatb	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(   "smlatb	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RRR(   "smlabt	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(   "smlabt	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RRR(   "smlatt	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(   "smlatt	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RR(    "smulbb	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "smulbb	r7, r",8, VAL3,", r",9, VAL1,"")
	TEST_RR(    "smultb	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "smultb	r7, r",8, VAL3,", r",9, VAL1,"")
	TEST_RR(    "smulbt	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "smulbt	r7, r",8, VAL3,", r",9, VAL1,"")
	TEST_RR(    "smultt	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "smultt	r7, r",8, VAL3,", r",9, VAL1,"")

	TEST_RRR(   "smlad	r0, r",0,  HH1,", r",1, HH2,", r",2, VAL1,"")
	TEST_RRR(   "smlad	r14, r",12,HH2,", r",10,HH1,", r",8, VAL2,"")
	TEST_RRR(   "smladx	r0, r",0,  HH1,", r",1, HH2,", r",2, VAL1,"")
	TEST_RRR(   "smladx	r14, r",12,HH2,", r",10,HH1,", r",8, VAL2,"")
	TEST_RR(    "smuad	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "smuad	r14, r",12,HH2,", r",10,HH1,"")
	TEST_RR(    "smuadx	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "smuadx	r14, r",12,HH2,", r",10,HH1,"")

	TEST_RRR(   "smlawb	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(   "smlawb	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RRR(   "smlawt	r0, r",1, VAL1,", r",2, VAL2,", r",3,  VAL3,"")
	TEST_RRR(   "smlawt	r7, r",8, VAL3,", r",9, VAL1,", r",10, VAL2,"")
	TEST_RR(    "smulwb	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "smulwb	r7, r",8, VAL3,", r",9, VAL1,"")
	TEST_RR(    "smulwt	r0, r",1, VAL1,", r",2, VAL2,"")
	TEST_RR(    "smulwt	r7, r",8, VAL3,", r",9, VAL1,"")

	TEST_RRR(   "smlsd	r0, r",0,  HH1,", r",1, HH2,", r",2, VAL1,"")
	TEST_RRR(   "smlsd	r14, r",12,HH2,", r",10,HH1,", r",8, VAL2,"")
	TEST_RRR(   "smlsdx	r0, r",0,  HH1,", r",1, HH2,", r",2, VAL1,"")
	TEST_RRR(   "smlsdx	r14, r",12,HH2,", r",10,HH1,", r",8, VAL2,"")
	TEST_RR(    "smusd	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "smusd	r14, r",12,HH2,", r",10,HH1,"")
	TEST_RR(    "smusdx	r0, r",0,  HH1,", r",1, HH2,"")
	TEST_RR(    "smusdx	r14, r",12,HH2,", r",10,HH1,"")

	TEST_RRR(   "smmla	r0, r",0,  VAL1,", r",1, VAL2,", r",2, VAL1,"")
	TEST_RRR(   "smmla	r14, r",12,VAL2,", r",10,VAL1,", r",8, VAL2,"")
	TEST_RRR(   "smmlar	r0, r",0,  VAL1,", r",1, VAL2,", r",2, VAL1,"")
	TEST_RRR(   "smmlar	r14, r",12,VAL2,", r",10,VAL1,", r",8, VAL2,"")
	TEST_RR(    "smmul	r0, r",0,  VAL1,", r",1, VAL2,"")
	TEST_RR(    "smmul	r14, r",12,VAL2,", r",10,VAL1,"")
	TEST_RR(    "smmulr	r0, r",0,  VAL1,", r",1, VAL2,"")
	TEST_RR(    "smmulr	r14, r",12,VAL2,", r",10,VAL1,"")

	TEST_RRR(   "smmls	r0, r",0,  VAL1,", r",1, VAL2,", r",2, VAL1,"")
	TEST_RRR(   "smmls	r14, r",12,VAL2,", r",10,VAL1,", r",8, VAL2,"")
	TEST_RRR(   "smmlsr	r0, r",0,  VAL1,", r",1, VAL2,", r",2, VAL1,"")
	TEST_RRR(   "smmlsr	r14, r",12,VAL2,", r",10,VAL1,", r",8, VAL2,"")

	TEST_RRR(   "usada8	r0, r",0,  VAL1,", r",1, VAL2,", r",2, VAL3,"")
	TEST_RRR(   "usada8	r14, r",12,VAL2,", r",10,VAL1,", r",8, VAL3,"")
	TEST_RR(    "usad8	r0, r",0,  VAL1,", r",1, VAL2,"")
	TEST_RR(    "usad8	r14, r",12,VAL2,", r",10,VAL1,"")

	TEST_UNSUPPORTED(__inst_thumb32(0xfb00f010) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_thumb32(0xfb0fff1f) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_thumb32(0xfb70f010) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_thumb32(0xfb7fff1f) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_thumb32(0xfb700010) "") /* Unallocated space */
	TEST_UNSUPPORTED(__inst_thumb32(0xfb7fff1f) "") /* Unallocated space */

	TEST_GROUP("Long multiply, long multiply accumulate, and divide")

	TEST_RR(   "smull	r0, r1, r",2, VAL1,", r",3, VAL2,"")
	TEST_RR(   "smull	r7, r8, r",9, VAL2,", r",10, VAL1,"")
	TEST_UNSUPPORTED(__inst_thumb32(0xfb89f80a) "	@ smull	pc, r8, r9, r10");
	TEST_UNSUPPORTED(__inst_thumb32(0xfb89d80a) "	@ smull	sp, r8, r9, r10");
	TEST_UNSUPPORTED(__inst_thumb32(0xfb897f0a) "	@ smull	r7, pc, r9, r10");
	TEST_UNSUPPORTED(__inst_thumb32(0xfb897d0a) "	@ smull	r7, sp, r9, r10");
	TEST_UNSUPPORTED(__inst_thumb32(0xfb8f780a) "	@ smull	r7, r8, pc, r10");
	TEST_UNSUPPORTED(__inst_thumb32(0xfb8d780a) "	@ smull	r7, r8, sp, r10");
	TEST_UNSUPPORTED(__inst_thumb32(0xfb89780f) "	@ smull	r7, r8, r9, pc");
	TEST_UNSUPPORTED(__inst_thumb32(0xfb89780d) "	@ smull	r7, r8, r9, sp");

	TEST_RR(   "umull	r0, r1, r",2, VAL1,", r",3, VAL2,"")
	TEST_RR(   "umull	r7, r8, r",9, VAL2,", r",10, VAL1,"")

	TEST_RRRR( "smlal	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR( "smlal	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)

	TEST_RRRR( "smlalbb	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR( "smlalbb	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)
	TEST_RRRR( "smlalbt	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR( "smlalbt	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)
	TEST_RRRR( "smlaltb	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR( "smlaltb	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)
	TEST_RRRR( "smlaltt	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR( "smlaltt	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)

	TEST_RRRR( "smlald	r",0, VAL1,", r",1, VAL2, ", r",0, HH1,", r",1, HH2)
	TEST_RRRR( "smlald	r",11,VAL2,", r",10,VAL1, ", r",9, HH2,", r",8, HH1)
	TEST_RRRR( "smlaldx	r",0, VAL1,", r",1, VAL2, ", r",0, HH1,", r",1, HH2)
	TEST_RRRR( "smlaldx	r",11,VAL2,", r",10,VAL1, ", r",9, HH2,", r",8, HH1)

	TEST_RRRR( "smlsld	r",0, VAL1,", r",1, VAL2, ", r",0, HH1,", r",1, HH2)
	TEST_RRRR( "smlsld	r",11,VAL2,", r",10,VAL1, ", r",9, HH2,", r",8, HH1)
	TEST_RRRR( "smlsldx	r",0, VAL1,", r",1, VAL2, ", r",0, HH1,", r",1, HH2)
	TEST_RRRR( "smlsldx	r",11,VAL2,", r",10,VAL1, ", r",9, HH2,", r",8, HH1)

	TEST_RRRR( "umlal	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR( "umlal	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)
	TEST_RRRR( "umaal	r",0, VAL1,", r",1, VAL2,", r",2, VAL3,", r",3, VAL4)
	TEST_RRRR( "umaal	r",8, VAL4,", r",9, VAL1,", r",10,VAL2,", r",11,VAL3)

	TEST_GROUP("Coprocessor instructions")

	TEST_UNSUPPORTED(__inst_thumb32(0xfc000000) "")
	TEST_UNSUPPORTED(__inst_thumb32(0xffffffff) "")

	TEST_GROUP("Testing instructions in IT blocks")

	TEST_ITBLOCK("sub.w	r0, r0")

	verbose("\n");
}

