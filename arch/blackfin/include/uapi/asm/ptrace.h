/*
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _UAPI_BFIN_PTRACE_H
#define _UAPI_BFIN_PTRACE_H

/*
 * GCC defines register number like this:
 * -----------------------------
 *       0 - 7 are data registers R0-R7
 *       8 - 15 are address registers P0-P7
 *      16 - 31 dsp registers I/B/L0 -- I/B/L3 & M0--M3
 *      32 - 33 A registers A0 & A1
 *      34 -    status register
 * -----------------------------
 *
 * We follows above, except:
 *      32-33 --- Low 32-bit of A0&1
 *      34-35 --- High 8-bit of A0&1
 */

#ifndef __ASSEMBLY__

struct task_struct;

/* this struct defines the way the registers are stored on the
   stack during a system call. */

struct pt_regs {
	long orig_pc;
	long ipend;
	long seqstat;
	long rete;
	long retn;
	long retx;
	long pc;		/* PC == RETI */
	long rets;
	long reserved;		/* Used as scratch during system calls */
	long astat;
	long lb1;
	long lb0;
	long lt1;
	long lt0;
	long lc1;
	long lc0;
	long a1w;
	long a1x;
	long a0w;
	long a0x;
	long b3;
	long b2;
	long b1;
	long b0;
	long l3;
	long l2;
	long l1;
	long l0;
	long m3;
	long m2;
	long m1;
	long m0;
	long i3;
	long i2;
	long i1;
	long i0;
	long usp;
	long fp;
	long p5;
	long p4;
	long p3;
	long p2;
	long p1;
	long p0;
	long r7;
	long r6;
	long r5;
	long r4;
	long r3;
	long r2;
	long r1;
	long r0;
	long orig_r0;
	long orig_p0;
	long syscfg;
};

/* Arbitrarily choose the same ptrace numbers as used by the Sparc code. */
#define PTRACE_GETREGS            12
#define PTRACE_SETREGS            13	/* ptrace signal  */

#define PTRACE_GETFDPIC           31	/* get the ELF fdpic loadmap address */
#define PTRACE_GETFDPIC_EXEC       0	/* [addr] request the executable loadmap */
#define PTRACE_GETFDPIC_INTERP     1	/* [addr] request the interpreter loadmap */

#define PS_S  (0x0002)


#endif				/* __ASSEMBLY__ */

/*
 * Offsets used by 'ptrace' system call interface.
 */

#define PT_R0 204
#define PT_R1 200
#define PT_R2 196
#define PT_R3 192
#define PT_R4 188
#define PT_R5 184
#define PT_R6 180
#define PT_R7 176
#define PT_P0 172
#define PT_P1 168
#define PT_P2 164
#define PT_P3 160
#define PT_P4 156
#define PT_P5 152
#define PT_FP 148
#define PT_USP 144
#define PT_I0 140
#define PT_I1 136
#define PT_I2 132
#define PT_I3 128
#define PT_M0 124
#define PT_M1 120
#define PT_M2 116
#define PT_M3 112
#define PT_L0 108
#define PT_L1 104
#define PT_L2 100
#define PT_L3 96
#define PT_B0 92
#define PT_B1 88
#define PT_B2 84
#define PT_B3 80
#define PT_A0X 76
#define PT_A0W 72
#define PT_A1X 68
#define PT_A1W 64
#define PT_LC0 60
#define PT_LC1 56
#define PT_LT0 52
#define PT_LT1 48
#define PT_LB0 44
#define PT_LB1 40
#define PT_ASTAT 36
#define PT_RESERVED 32
#define PT_RETS 28
#define PT_PC 24
#define PT_RETX 20
#define PT_RETN 16
#define PT_RETE 12
#define PT_SEQSTAT 8
#define PT_IPEND 4

#define PT_ORIG_R0 208
#define PT_ORIG_P0 212
#define PT_SYSCFG 216
#define PT_TEXT_ADDR 220
#define PT_TEXT_END_ADDR 224
#define PT_DATA_ADDR 228
#define PT_FDPIC_EXEC 232
#define PT_FDPIC_INTERP 236

#define PT_LAST_PSEUDO PT_FDPIC_INTERP

#endif /* _UAPI_BFIN_PTRACE_H */
