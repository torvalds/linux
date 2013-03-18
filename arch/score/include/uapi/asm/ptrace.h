#ifndef _UAPI_ASM_SCORE_PTRACE_H
#define _UAPI_ASM_SCORE_PTRACE_H

#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13

#define PC		32
#define CONDITION	33
#define ECR		34
#define EMA		35
#define CEH		36
#define CEL		37
#define COUNTER		38
#define LDCR		39
#define STCR		40
#define PSR		41

#define SINGLESTEP16_INSN	0x7006
#define SINGLESTEP32_INSN	0x840C8000
#define BREAKPOINT16_INSN	0x7002		/* work on SPG300 */
#define BREAKPOINT32_INSN	0x84048000	/* work on SPG300 */

/* Define instruction mask */
#define INSN32_MASK	0x80008000

#define J32	0x88008000	/* 1_00010_0000000000_1_000000000000000 */
#define J32M	0xFC008000	/* 1_11111_0000000000_1_000000000000000 */

#define B32	0x90008000	/* 1_00100_0000000000_1_000000000000000 */
#define B32M	0xFC008000
#define BL32	0x90008001	/* 1_00100_0000000000_1_000000000000001 */
#define BL32M	B32
#define BR32	0x80008008	/* 1_00000_0000000000_1_00000000_000100_0 */
#define BR32M	0xFFE0807E
#define BRL32	0x80008009	/* 1_00000_0000000000_1_00000000_000100_1 */
#define BRL32M	BR32M

#define B32_SET	(J32 | B32 | BL32 | BR32 | BRL32)

#define J16	0x3000		/* 0_011_....... */
#define J16M	0xF000
#define B16	0x4000		/* 0_100_....... */
#define B16M	0xF000
#define BR16	0x0004		/* 0_000.......0100 */
#define BR16M	0xF00F
#define B16_SET (J16 | B16 | BR16)


/*
 * This struct defines the way the registers are stored on the stack during a
 * system call/exception. As usual the registers k0/k1 aren't being saved.
 */
struct pt_regs {
	unsigned long pad0[6];	/* stack arguments */
	unsigned long orig_r4;
	unsigned long orig_r7;
	long is_syscall;

	unsigned long regs[32];

	unsigned long cel;
	unsigned long ceh;

	unsigned long sr0;	/* cnt */
	unsigned long sr1;	/* lcr */
	unsigned long sr2;	/* scr */

	unsigned long cp0_epc;
	unsigned long cp0_ema;
	unsigned long cp0_psr;
	unsigned long cp0_ecr;
	unsigned long cp0_condition;
};


#endif /* _UAPI_ASM_SCORE_PTRACE_H */
