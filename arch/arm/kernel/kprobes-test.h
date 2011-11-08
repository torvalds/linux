/*
 * arch/arm/kernel/kprobes-test.h
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define VERBOSE 0 /* Set to '1' for more logging of test cases */

#ifdef CONFIG_THUMB2_KERNEL
#define NORMAL_ISA "16"
#else
#define NORMAL_ISA "32"
#endif


/* Flags used in kprobe_test_flags */
#define TEST_FLAG_NO_ITBLOCK	(1<<0)
#define TEST_FLAG_FULL_ITBLOCK	(1<<1)
#define TEST_FLAG_NARROW_INSTR	(1<<2)

extern int kprobe_test_flags;
extern int kprobe_test_cc_position;


#define TEST_MEMORY_SIZE 256


/*
 * Test case structures.
 *
 * The arguments given to test cases can be one of three types.
 *
 *   ARG_TYPE_REG
 *	Load a register with the given value.
 *
 *   ARG_TYPE_PTR
 *	Load a register with a pointer into the stack buffer (SP + given value).
 *
 *   ARG_TYPE_MEM
 *	Store the given value into the stack buffer at [SP+index].
 *
 */

#define	ARG_TYPE_END	0
#define	ARG_TYPE_REG	1
#define	ARG_TYPE_PTR	2
#define	ARG_TYPE_MEM	3

#define ARG_FLAG_UNSUPPORTED	0x01
#define ARG_FLAG_SUPPORTED	0x02
#define ARG_FLAG_THUMB		0x10	/* Must be 16 so TEST_ISA can be used */
#define ARG_FLAG_ARM		0x20	/* Must be 32 so TEST_ISA can be used */

struct test_arg {
	u8	type;		/* ARG_TYPE_x */
	u8	_padding[7];
};

struct test_arg_regptr {
	u8	type;		/* ARG_TYPE_REG or ARG_TYPE_PTR */
	u8	reg;
	u8	_padding[2];
	u32	val;
};

struct test_arg_mem {
	u8	type;		/* ARG_TYPE_MEM */
	u8	index;
	u8	_padding[2];
	u32	val;
};

struct test_arg_end {
	u8	type;		/* ARG_TYPE_END */
	u8	flags;		/* ARG_FLAG_x */
	u16	code_offset;
	u16	branch_offset;
	u16	end_offset;
};


/*
 * Building blocks for test cases.
 *
 * Each test case is wrapped between TESTCASE_START and TESTCASE_END.
 *
 * To specify arguments for a test case the TEST_ARG_{REG,PTR,MEM} macros are
 * used followed by a terminating TEST_ARG_END.
 *
 * After this, the instruction to be tested is defined with TEST_INSTRUCTION.
 * Or for branches, TEST_BRANCH_B and TEST_BRANCH_F (branch forwards/backwards).
 *
 * Some specific test cases may make use of other custom constructs.
 */

#if VERBOSE
#define verbose(fmt, ...) pr_info(fmt, ##__VA_ARGS__)
#else
#define verbose(fmt, ...)
#endif

#define TEST_GROUP(title)					\
	verbose("\n");						\
	verbose(title"\n");					\
	verbose("---------------------------------------------------------\n");

#define TESTCASE_START(title)					\
	__asm__ __volatile__ (					\
	"bl	__kprobes_test_case_start		\n\t"	\
	/* don't use .asciz here as 'title' may be */		\
	/* multiple strings to be concatenated.  */		\
	".ascii "#title"				\n\t"	\
	".byte	0					\n\t"	\
	".align	2					\n\t"

#define	TEST_ARG_REG(reg, val)					\
	".byte	"__stringify(ARG_TYPE_REG)"		\n\t"	\
	".byte	"#reg"					\n\t"	\
	".short	0					\n\t"	\
	".word	"#val"					\n\t"

#define	TEST_ARG_PTR(reg, val)					\
	".byte	"__stringify(ARG_TYPE_PTR)"		\n\t"	\
	".byte	"#reg"					\n\t"	\
	".short	0					\n\t"	\
	".word	"#val"					\n\t"

#define	TEST_ARG_MEM(index, val)				\
	".byte	"__stringify(ARG_TYPE_MEM)"		\n\t"	\
	".byte	"#index"				\n\t"	\
	".short	0					\n\t"	\
	".word	"#val"					\n\t"

#define	TEST_ARG_END(flags)					\
	".byte	"__stringify(ARG_TYPE_END)"		\n\t"	\
	".byte	"TEST_ISA flags"			\n\t"	\
	".short	50f-0f					\n\t"	\
	".short	2f-0f					\n\t"	\
	".short	99f-0f					\n\t"	\
	".code "TEST_ISA"				\n\t"	\
	"0:						\n\t"

#define TEST_INSTRUCTION(instruction)				\
	"50:	nop					\n\t"	\
	"1:	"instruction"				\n\t"	\
	"	nop					\n\t"

#define TEST_BRANCH_F(instruction, xtra_dist)			\
	TEST_INSTRUCTION(instruction)				\
	".if "#xtra_dist"				\n\t"	\
	"	b	99f				\n\t"	\
	".space "#xtra_dist"				\n\t"	\
	".endif						\n\t"	\
	"	b	99f				\n\t"	\
	"2:	nop					\n\t"

#define TEST_BRANCH_B(instruction, xtra_dist)			\
	"	b	50f				\n\t"	\
	"	b	99f				\n\t"	\
	"2:	nop					\n\t"	\
	"	b	99f				\n\t"	\
	".if "#xtra_dist"				\n\t"	\
	".space "#xtra_dist"				\n\t"	\
	".endif						\n\t"	\
	TEST_INSTRUCTION(instruction)

#define TESTCASE_END						\
	"2:						\n\t"	\
	"99:						\n\t"	\
	"	bl __kprobes_test_case_end_"TEST_ISA"	\n\t"	\
	".code "NORMAL_ISA"				\n\t"	\
	: :							\
	: "r0", "r1", "r2", "r3", "ip", "lr", "memory", "cc"	\
	);


/*
 * Macros to define test cases.
 *
 * Those of the form TEST_{R,P,M}* can be used to define test cases
 * which take combinations of the three basic types of arguments. E.g.
 *
 *   TEST_R	One register argument
 *   TEST_RR	Two register arguments
 *   TEST_RPR	A register, a pointer, then a register argument
 *
 * For testing instructions which may branch, there are macros TEST_BF_*
 * and TEST_BB_* for branching forwards and backwards.
 *
 * TEST_SUPPORTED and TEST_UNSUPPORTED don't cause the code to be executed,
 * the just verify that a kprobe is or is not allowed on the given instruction.
 */

#define TEST(code)				\
	TESTCASE_START(code)			\
	TEST_ARG_END("")			\
	TEST_INSTRUCTION(code)			\
	TESTCASE_END

#define TEST_UNSUPPORTED(code)					\
	TESTCASE_START(code)					\
	TEST_ARG_END("|"__stringify(ARG_FLAG_UNSUPPORTED))	\
	TEST_INSTRUCTION(code)					\
	TESTCASE_END

#define TEST_SUPPORTED(code)					\
	TESTCASE_START(code)					\
	TEST_ARG_END("|"__stringify(ARG_FLAG_SUPPORTED))	\
	TEST_INSTRUCTION(code)					\
	TESTCASE_END

#define TEST_R(code1, reg, val, code2)			\
	TESTCASE_START(code1 #reg code2)		\
	TEST_ARG_REG(reg, val)				\
	TEST_ARG_END("")				\
	TEST_INSTRUCTION(code1 #reg code2)		\
	TESTCASE_END

#define TEST_RR(code1, reg1, val1, code2, reg2, val2, code3)	\
	TESTCASE_START(code1 #reg1 code2 #reg2 code3)		\
	TEST_ARG_REG(reg1, val1)				\
	TEST_ARG_REG(reg2, val2)				\
	TEST_ARG_END("")					\
	TEST_INSTRUCTION(code1 #reg1 code2 #reg2 code3)		\
	TESTCASE_END

#define TEST_RRR(code1, reg1, val1, code2, reg2, val2, code3, reg3, val3, code4)\
	TESTCASE_START(code1 #reg1 code2 #reg2 code3 #reg3 code4)		\
	TEST_ARG_REG(reg1, val1)						\
	TEST_ARG_REG(reg2, val2)						\
	TEST_ARG_REG(reg3, val3)						\
	TEST_ARG_END("")							\
	TEST_INSTRUCTION(code1 #reg1 code2 #reg2 code3 #reg3 code4)		\
	TESTCASE_END

#define TEST_RRRR(code1, reg1, val1, code2, reg2, val2, code3, reg3, val3, code4, reg4, val4)	\
	TESTCASE_START(code1 #reg1 code2 #reg2 code3 #reg3 code4 #reg4)		\
	TEST_ARG_REG(reg1, val1)						\
	TEST_ARG_REG(reg2, val2)						\
	TEST_ARG_REG(reg3, val3)						\
	TEST_ARG_REG(reg4, val4)						\
	TEST_ARG_END("")							\
	TEST_INSTRUCTION(code1 #reg1 code2 #reg2 code3 #reg3 code4 #reg4)	\
	TESTCASE_END

#define TEST_P(code1, reg1, val1, code2)	\
	TESTCASE_START(code1 #reg1 code2)	\
	TEST_ARG_PTR(reg1, val1)		\
	TEST_ARG_END("")			\
	TEST_INSTRUCTION(code1 #reg1 code2)	\
	TESTCASE_END

#define TEST_PR(code1, reg1, val1, code2, reg2, val2, code3)	\
	TESTCASE_START(code1 #reg1 code2 #reg2 code3)		\
	TEST_ARG_PTR(reg1, val1)				\
	TEST_ARG_REG(reg2, val2)				\
	TEST_ARG_END("")					\
	TEST_INSTRUCTION(code1 #reg1 code2 #reg2 code3)		\
	TESTCASE_END

#define TEST_RP(code1, reg1, val1, code2, reg2, val2, code3)	\
	TESTCASE_START(code1 #reg1 code2 #reg2 code3)		\
	TEST_ARG_REG(reg1, val1)				\
	TEST_ARG_PTR(reg2, val2)				\
	TEST_ARG_END("")					\
	TEST_INSTRUCTION(code1 #reg1 code2 #reg2 code3)		\
	TESTCASE_END

#define TEST_PRR(code1, reg1, val1, code2, reg2, val2, code3, reg3, val3, code4)\
	TESTCASE_START(code1 #reg1 code2 #reg2 code3 #reg3 code4)		\
	TEST_ARG_PTR(reg1, val1)						\
	TEST_ARG_REG(reg2, val2)						\
	TEST_ARG_REG(reg3, val3)						\
	TEST_ARG_END("")							\
	TEST_INSTRUCTION(code1 #reg1 code2 #reg2 code3 #reg3 code4)		\
	TESTCASE_END

#define TEST_RPR(code1, reg1, val1, code2, reg2, val2, code3, reg3, val3, code4)\
	TESTCASE_START(code1 #reg1 code2 #reg2 code3 #reg3 code4)		\
	TEST_ARG_REG(reg1, val1)						\
	TEST_ARG_PTR(reg2, val2)						\
	TEST_ARG_REG(reg3, val3)						\
	TEST_ARG_END("")							\
	TEST_INSTRUCTION(code1 #reg1 code2 #reg2 code3 #reg3 code4)		\
	TESTCASE_END

#define TEST_RRP(code1, reg1, val1, code2, reg2, val2, code3, reg3, val3, code4)\
	TESTCASE_START(code1 #reg1 code2 #reg2 code3 #reg3 code4)		\
	TEST_ARG_REG(reg1, val1)						\
	TEST_ARG_REG(reg2, val2)						\
	TEST_ARG_PTR(reg3, val3)						\
	TEST_ARG_END("")							\
	TEST_INSTRUCTION(code1 #reg1 code2 #reg2 code3 #reg3 code4)		\
	TESTCASE_END

#define TEST_BF_P(code1, reg1, val1, code2)	\
	TESTCASE_START(code1 #reg1 code2)	\
	TEST_ARG_PTR(reg1, val1)		\
	TEST_ARG_END("")			\
	TEST_BRANCH_F(code1 #reg1 code2, 0)	\
	TESTCASE_END

#define TEST_BF_X(code, xtra_dist)		\
	TESTCASE_START(code)			\
	TEST_ARG_END("")			\
	TEST_BRANCH_F(code, xtra_dist)		\
	TESTCASE_END

#define TEST_BB_X(code, xtra_dist)		\
	TESTCASE_START(code)			\
	TEST_ARG_END("")			\
	TEST_BRANCH_B(code, xtra_dist)		\
	TESTCASE_END

#define TEST_BF_RX(code1, reg, val, code2, xtra_dist)	\
	TESTCASE_START(code1 #reg code2)		\
	TEST_ARG_REG(reg, val)				\
	TEST_ARG_END("")				\
	TEST_BRANCH_F(code1 #reg code2, xtra_dist)	\
	TESTCASE_END

#define TEST_BB_RX(code1, reg, val, code2, xtra_dist)	\
	TESTCASE_START(code1 #reg code2)		\
	TEST_ARG_REG(reg, val)				\
	TEST_ARG_END("")				\
	TEST_BRANCH_B(code1 #reg code2, xtra_dist)	\
	TESTCASE_END

#define TEST_BF(code)	TEST_BF_X(code, 0)
#define TEST_BB(code)	TEST_BB_X(code, 0)

#define TEST_BF_R(code1, reg, val, code2) TEST_BF_RX(code1, reg, val, code2, 0)
#define TEST_BB_R(code1, reg, val, code2) TEST_BB_RX(code1, reg, val, code2, 0)

#define TEST_BF_RR(code1, reg1, val1, code2, reg2, val2, code3)	\
	TESTCASE_START(code1 #reg1 code2 #reg2 code3)		\
	TEST_ARG_REG(reg1, val1)				\
	TEST_ARG_REG(reg2, val2)				\
	TEST_ARG_END("")					\
	TEST_BRANCH_F(code1 #reg1 code2 #reg2 code3, 0)		\
	TESTCASE_END

#define TEST_X(code, codex)			\
	TESTCASE_START(code)			\
	TEST_ARG_END("")			\
	TEST_INSTRUCTION(code)			\
	"	b	99f		\n\t"	\
	"	"codex"			\n\t"	\
	TESTCASE_END

#define TEST_RX(code1, reg, val, code2, codex)		\
	TESTCASE_START(code1 #reg code2)		\
	TEST_ARG_REG(reg, val)				\
	TEST_ARG_END("")				\
	TEST_INSTRUCTION(code1 __stringify(reg) code2)	\
	"	b	99f		\n\t"		\
	"	"codex"			\n\t"		\
	TESTCASE_END

#define TEST_RRX(code1, reg1, val1, code2, reg2, val2, code3, codex)		\
	TESTCASE_START(code1 #reg1 code2 #reg2 code3)				\
	TEST_ARG_REG(reg1, val1)						\
	TEST_ARG_REG(reg2, val2)						\
	TEST_ARG_END("")							\
	TEST_INSTRUCTION(code1 __stringify(reg1) code2 __stringify(reg2) code3)	\
	"	b	99f		\n\t"					\
	"	"codex"			\n\t"					\
	TESTCASE_END


/* Various values used in test cases... */
#define N(val)	(val ^ 0xffffffff)
#define VAL1	0x12345678
#define VAL2	N(VAL1)
#define VAL3	0xa5f801
#define VAL4	N(VAL3)
#define VALM	0x456789ab
#define VALR	0xdeaddead
#define HH1	0x0123fecb
#define HH2	0xa9874567


#ifdef CONFIG_THUMB2_KERNEL
void kprobe_thumb16_test_cases(void);
void kprobe_thumb32_test_cases(void);
#else
void kprobe_arm_test_cases(void);
#endif
