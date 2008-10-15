#ifndef _SPARC_KGDB_H
#define _SPARC_KGDB_H

#ifdef CONFIG_SPARC32
#define BUFMAX			2048
#else
#define BUFMAX			4096
#endif

enum regnames {
	GDB_G0, GDB_G1, GDB_G2, GDB_G3, GDB_G4, GDB_G5, GDB_G6, GDB_G7,
	GDB_O0, GDB_O1, GDB_O2, GDB_O3, GDB_O4, GDB_O5, GDB_SP, GDB_O7,
	GDB_L0, GDB_L1, GDB_L2, GDB_L3, GDB_L4, GDB_L5, GDB_L6, GDB_L7,
	GDB_I0, GDB_I1, GDB_I2, GDB_I3, GDB_I4, GDB_I5, GDB_FP, GDB_I7,
	GDB_F0,
	GDB_F31 = GDB_F0 + 31,
#ifdef CONFIG_SPARC32
	GDB_Y, GDB_PSR, GDB_WIM, GDB_TBR, GDB_PC, GDB_NPC,
	GDB_FSR, GDB_CSR,
#else
	GDB_F32 = GDB_F0 + 32,
	GDB_F62 = GDB_F32 + 15,
	GDB_PC, GDB_NPC, GDB_STATE, GDB_FSR, GDB_FPRS, GDB_Y,
#endif
};

#ifdef CONFIG_SPARC32
#define NUMREGBYTES		((GDB_CSR + 1) * 4)
#else
#define NUMREGBYTES		((GDB_Y + 1) * 8)
#endif

extern void arch_kgdb_breakpoint(void);

#define BREAK_INSTR_SIZE	4
#define CACHE_FLUSH_IS_SAFE	1

#endif /* _SPARC_KGDB_H */
