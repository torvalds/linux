/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#ifndef _ASM_LOONGARCH_KGDB_H
#define _ASM_LOONGARCH_KGDB_H

#define GDB_SIZEOF_REG		sizeof(u64)

/* gdb remote procotol expects the following register layout. */

/*
 * General purpose registers:
 *     r0-r31: 64 bit
 *     orig_a0: 64 bit
 *     pc : 64 bit
 *     csr_badvaddr: 64 bit
 */
#define DBG_PT_REGS_BASE	0
#define DBG_PT_REGS_NUM		35
#define DBG_PT_REGS_END		(DBG_PT_REGS_BASE + DBG_PT_REGS_NUM - 1)

/*
 * Floating point registers:
 *     f0-f31: 64 bit
 */
#define DBG_FPR_BASE		(DBG_PT_REGS_END + 1)
#define DBG_FPR_NUM		32
#define DBG_FPR_END		(DBG_FPR_BASE + DBG_FPR_NUM - 1)

/*
 * Condition Flag registers:
 *     fcc0-fcc8: 8 bit
 */
#define DBG_FCC_BASE		(DBG_FPR_END + 1)
#define DBG_FCC_NUM		8
#define DBG_FCC_END		(DBG_FCC_BASE + DBG_FCC_NUM - 1)

/*
 * Floating-point Control and Status registers:
 *     fcsr: 32 bit
 */
#define DBG_FCSR_NUM		1
#define DBG_FCSR		(DBG_FCC_END + 1)

#define DBG_MAX_REG_NUM		(DBG_FCSR + 1)

/*
 * Size of I/O buffer for gdb packet.
 * considering to hold all register contents, size is set
 */
#define BUFMAX			2048

/*
 * Number of bytes required for gdb_regs buffer.
 * PT_REGS and FPR: 8 bytes; FCSR: 4 bytes; FCC: 1 bytes.
 * GDB fails to connect for size beyond this with error
 * "'g' packet reply is too long"
 */
#define NUMREGBYTES		((DBG_PT_REGS_NUM + DBG_FPR_NUM) * GDB_SIZEOF_REG + DBG_FCC_NUM * 1 + DBG_FCSR_NUM * 4)

#define BREAK_INSTR_SIZE	4
#define CACHE_FLUSH_IS_SAFE	0

/* Register numbers of various important registers. */
enum dbg_loongarch_regnum {
	DBG_LOONGARCH_ZERO = 0,
	DBG_LOONGARCH_RA,
	DBG_LOONGARCH_TP,
	DBG_LOONGARCH_SP,
	DBG_LOONGARCH_A0,
	DBG_LOONGARCH_FP = 22,
	DBG_LOONGARCH_S0,
	DBG_LOONGARCH_S1,
	DBG_LOONGARCH_S2,
	DBG_LOONGARCH_S3,
	DBG_LOONGARCH_S4,
	DBG_LOONGARCH_S5,
	DBG_LOONGARCH_S6,
	DBG_LOONGARCH_S7,
	DBG_LOONGARCH_S8,
	DBG_LOONGARCH_ORIG_A0,
	DBG_LOONGARCH_PC,
	DBG_LOONGARCH_BADV
};

void kgdb_breakinst(void);
void arch_kgdb_breakpoint(void);

#ifdef CONFIG_KGDB
bool kgdb_breakpoint_handler(struct pt_regs *regs);
#else /* !CONFIG_KGDB */
static inline bool kgdb_breakpoint_handler(struct pt_regs *regs) { return false; }
#endif /* CONFIG_KGDB */

#endif /* __ASM_KGDB_H_ */
