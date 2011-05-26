/* Blackfin KGDB header
 *
 * Copyright 2005-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_BLACKFIN_KGDB_H__
#define __ASM_BLACKFIN_KGDB_H__

#include <linux/ptrace.h>

/*
 * BUFMAX defines the maximum number of characters in inbound/outbound buffers.
 * At least NUMREGBYTES*2 are needed for register packets.
 * Longer buffer is needed to list all threads.
 */
#define BUFMAX 2048

/*
 * Note that this register image is different from
 * the register image that Linux produces at interrupt time.
 *
 * Linux's register image is defined by struct pt_regs in ptrace.h.
 */
enum regnames {
  /* Core Registers */
  BFIN_R0 = 0,
  BFIN_R1,
  BFIN_R2,
  BFIN_R3,
  BFIN_R4,
  BFIN_R5,
  BFIN_R6,
  BFIN_R7,
  BFIN_P0,
  BFIN_P1,
  BFIN_P2,
  BFIN_P3,
  BFIN_P4,
  BFIN_P5,
  BFIN_SP,
  BFIN_FP,
  BFIN_I0,
  BFIN_I1,
  BFIN_I2,
  BFIN_I3,
  BFIN_M0,
  BFIN_M1,
  BFIN_M2,
  BFIN_M3,
  BFIN_B0,
  BFIN_B1,
  BFIN_B2,
  BFIN_B3,
  BFIN_L0,
  BFIN_L1,
  BFIN_L2,
  BFIN_L3,
  BFIN_A0_DOT_X,
  BFIN_A0_DOT_W,
  BFIN_A1_DOT_X,
  BFIN_A1_DOT_W,
  BFIN_ASTAT,
  BFIN_RETS,
  BFIN_LC0,
  BFIN_LT0,
  BFIN_LB0,
  BFIN_LC1,
  BFIN_LT1,
  BFIN_LB1,
  BFIN_CYCLES,
  BFIN_CYCLES2,
  BFIN_USP,
  BFIN_SEQSTAT,
  BFIN_SYSCFG,
  BFIN_RETI,
  BFIN_RETX,
  BFIN_RETN,
  BFIN_RETE,

  /* Pseudo Registers */
  BFIN_PC,
  BFIN_CC,
  BFIN_EXTRA1,		/* Address of .text section.  */
  BFIN_EXTRA2,		/* Address of .data section.  */
  BFIN_EXTRA3,		/* Address of .bss section.  */
  BFIN_FDPIC_EXEC,
  BFIN_FDPIC_INTERP,

  /* MMRs */
  BFIN_IPEND,

  /* LAST ENTRY SHOULD NOT BE CHANGED.  */
  BFIN_NUM_REGS		/* The number of all registers.  */
};

/* Number of bytes of registers.  */
#define NUMREGBYTES BFIN_NUM_REGS*4

static inline void arch_kgdb_breakpoint(void)
{
	asm("EXCPT 2;");
}
#define BREAK_INSTR_SIZE	2
#ifdef CONFIG_SMP
# define CACHE_FLUSH_IS_SAFE	0
#else
# define CACHE_FLUSH_IS_SAFE	1
#endif
#define GDB_ADJUSTS_BREAK_OFFSET
#define HW_INST_WATCHPOINT_NUM	6
#define HW_WATCHPOINT_NUM	8
#define TYPE_INST_WATCHPOINT	0
#define TYPE_DATA_WATCHPOINT	1

/* Instruction watchpoint address control register bits mask */
#define WPPWR		0x1
#define WPIREN01	0x2
#define WPIRINV01	0x4
#define WPIAEN0		0x8
#define WPIAEN1		0x10
#define WPICNTEN0	0x20
#define WPICNTEN1	0x40
#define EMUSW0		0x80
#define EMUSW1		0x100
#define WPIREN23	0x200
#define WPIRINV23	0x400
#define WPIAEN2		0x800
#define WPIAEN3		0x1000
#define WPICNTEN2	0x2000
#define WPICNTEN3	0x4000
#define EMUSW2		0x8000
#define EMUSW3		0x10000
#define WPIREN45	0x20000
#define WPIRINV45	0x40000
#define WPIAEN4		0x80000
#define WPIAEN5		0x100000
#define WPICNTEN4	0x200000
#define WPICNTEN5	0x400000
#define EMUSW4		0x800000
#define EMUSW5		0x1000000
#define WPAND		0x2000000

/* Data watchpoint address control register bits mask */
#define WPDREN01	0x1
#define WPDRINV01	0x2
#define WPDAEN0		0x4
#define WPDAEN1		0x8
#define WPDCNTEN0	0x10
#define WPDCNTEN1	0x20

#define WPDSRC0		0xc0
#define WPDACC0_OFFSET	8
#define WPDSRC1		0xc00
#define WPDACC1_OFFSET	12

/* Watchpoint status register bits mask */
#define STATIA0		0x1
#define STATIA1		0x2
#define STATIA2		0x4
#define STATIA3		0x8
#define STATIA4		0x10
#define STATIA5		0x20
#define STATDA0		0x40
#define STATDA1		0x80

#endif
