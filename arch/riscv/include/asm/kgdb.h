/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_KGDB_H_
#define __ASM_KGDB_H_

#include <linux/build_bug.h>

#ifdef __KERNEL__

#define GDB_SIZEOF_REG sizeof(unsigned long)

#define DBG_MAX_REG_NUM 36
#define NUMREGBYTES (DBG_MAX_REG_NUM * GDB_SIZEOF_REG)
#define CACHE_FLUSH_IS_SAFE     1
#define BUFMAX                  2048
static_assert(BUFMAX > NUMREGBYTES,
	      "As per KGDB documentation, BUFMAX must be larger than NUMREGBYTES");
#ifdef CONFIG_RISCV_ISA_C
#define BREAK_INSTR_SIZE	2
#else
#define BREAK_INSTR_SIZE	4
#endif

#ifndef	__ASSEMBLER__

void arch_kgdb_breakpoint(void);
extern unsigned long kgdb_compiled_break;

#endif /* !__ASSEMBLER__ */

#define DBG_REG_ZERO "zero"
#define DBG_REG_RA "ra"
#define DBG_REG_SP "sp"
#define DBG_REG_GP "gp"
#define DBG_REG_TP "tp"
#define DBG_REG_T0 "t0"
#define DBG_REG_T1 "t1"
#define DBG_REG_T2 "t2"
#define DBG_REG_FP "fp"
#define DBG_REG_S1 "s1"
#define DBG_REG_A0 "a0"
#define DBG_REG_A1 "a1"
#define DBG_REG_A2 "a2"
#define DBG_REG_A3 "a3"
#define DBG_REG_A4 "a4"
#define DBG_REG_A5 "a5"
#define DBG_REG_A6 "a6"
#define DBG_REG_A7 "a7"
#define DBG_REG_S2 "s2"
#define DBG_REG_S3 "s3"
#define DBG_REG_S4 "s4"
#define DBG_REG_S5 "s5"
#define DBG_REG_S6 "s6"
#define DBG_REG_S7 "s7"
#define DBG_REG_S8 "s8"
#define DBG_REG_S9 "s9"
#define DBG_REG_S10 "s10"
#define DBG_REG_S11 "s11"
#define DBG_REG_T3 "t3"
#define DBG_REG_T4 "t4"
#define DBG_REG_T5 "t5"
#define DBG_REG_T6 "t6"
#define DBG_REG_EPC "pc"
#define DBG_REG_STATUS "sstatus"
#define DBG_REG_BADADDR "stval"
#define DBG_REG_CAUSE "scause"

#define DBG_REG_ZERO_OFF 0
#define DBG_REG_RA_OFF 1
#define DBG_REG_SP_OFF 2
#define DBG_REG_GP_OFF 3
#define DBG_REG_TP_OFF 4
#define DBG_REG_T0_OFF 5
#define DBG_REG_T1_OFF 6
#define DBG_REG_T2_OFF 7
#define DBG_REG_FP_OFF 8
#define DBG_REG_S1_OFF 9
#define DBG_REG_A0_OFF 10
#define DBG_REG_A1_OFF 11
#define DBG_REG_A2_OFF 12
#define DBG_REG_A3_OFF 13
#define DBG_REG_A4_OFF 14
#define DBG_REG_A5_OFF 15
#define DBG_REG_A6_OFF 16
#define DBG_REG_A7_OFF 17
#define DBG_REG_S2_OFF 18
#define DBG_REG_S3_OFF 19
#define DBG_REG_S4_OFF 20
#define DBG_REG_S5_OFF 21
#define DBG_REG_S6_OFF 22
#define DBG_REG_S7_OFF 23
#define DBG_REG_S8_OFF 24
#define DBG_REG_S9_OFF 25
#define DBG_REG_S10_OFF 26
#define DBG_REG_S11_OFF 27
#define DBG_REG_T3_OFF 28
#define DBG_REG_T4_OFF 29
#define DBG_REG_T5_OFF 30
#define DBG_REG_T6_OFF 31
#define DBG_REG_EPC_OFF 32
#define DBG_REG_STATUS_OFF 33
#define DBG_REG_BADADDR_OFF 34
#define DBG_REG_CAUSE_OFF 35
/* NOTE: increase DBG_MAX_REG_NUM if you add more values here. */

extern const char riscv_gdb_stub_feature[64];

#define kgdb_arch_gdb_stub_feature riscv_gdb_stub_feature

#endif
#endif
