/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PA-RISC KGDB support
 *
 * Copyright (c) 2019 Sven Schnelle <svens@stackframe.org>
 *
 */

#ifndef __PARISC_KGDB_H__
#define __PARISC_KGDB_H__

#define BREAK_INSTR_SIZE		4
#define PARISC_KGDB_COMPILED_BREAK_INSN	0x3ffc01f
#define PARISC_KGDB_BREAK_INSN		0x3ffa01f


#define NUMREGBYTES			sizeof(struct parisc_gdb_regs)
#define BUFMAX				4096

#define KGDB_MAX_BREAKPOINTS		40

#define CACHE_FLUSH_IS_SAFE		1

#ifndef __ASSEMBLY__

static inline void arch_kgdb_breakpoint(void)
{
	asm(".word %0" : : "i"(PARISC_KGDB_COMPILED_BREAK_INSN) : "memory");
}

struct parisc_gdb_regs {
	unsigned long gpr[32];
	unsigned long sar;
	unsigned long iaoq_f;
	unsigned long iasq_f;
	unsigned long iaoq_b;
	unsigned long iasq_b;
	unsigned long eiem;
	unsigned long iir;
	unsigned long isr;
	unsigned long ior;
	unsigned long ipsw;
	unsigned long __unused0;
	unsigned long sr4;
	unsigned long sr0;
	unsigned long sr1;
	unsigned long sr2;
	unsigned long sr3;
	unsigned long sr5;
	unsigned long sr6;
	unsigned long sr7;
	unsigned long cr0;
	unsigned long pid1;
	unsigned long pid2;
	unsigned long scrccr;
	unsigned long pid3;
	unsigned long pid4;
	unsigned long cr24;
	unsigned long cr25;
	unsigned long cr26;
	unsigned long cr27;
	unsigned long cr28;
	unsigned long cr29;
	unsigned long cr30;

	u64 fr[32];
};

#endif
#endif
