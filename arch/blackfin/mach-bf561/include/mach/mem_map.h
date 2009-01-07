/*
 * Memory MAP
 * Common header file for blackfin BF561 of processors.
 */

#ifndef _MEM_MAP_561_H_
#define _MEM_MAP_561_H_

#define COREMMR_BASE           0xFFE00000	 /* Core MMRs */
#define SYSMMR_BASE            0xFFC00000	 /* System MMRs */

/* Async Memory Banks */
#define ASYNC_BANK3_BASE	0x2C000000	 /* Async Bank 3 */
#define ASYNC_BANK3_SIZE	0x04000000	/* 64M */
#define ASYNC_BANK2_BASE	0x28000000	 /* Async Bank 2 */
#define ASYNC_BANK2_SIZE	0x04000000	/* 64M */
#define ASYNC_BANK1_BASE	0x24000000	 /* Async Bank 1 */
#define ASYNC_BANK1_SIZE	0x04000000	/* 64M */
#define ASYNC_BANK0_BASE	0x20000000	 /* Async Bank 0 */
#define ASYNC_BANK0_SIZE	0x04000000	/* 64M */

/* Boot ROM Memory */

#define BOOT_ROM_START		0xEF000000
#define BOOT_ROM_LENGTH		0x800

/* Level 1 Memory */

#ifdef CONFIG_BFIN_ICACHE
#define BFIN_ICACHESIZE	(16*1024)
#else
#define BFIN_ICACHESIZE	(0*1024)
#endif

/* Memory Map for ADSP-BF561 processors */

#ifdef CONFIG_BF561
#define COREA_L1_CODE_START       0xFFA00000
#define COREA_L1_DATA_A_START     0xFF800000
#define COREA_L1_DATA_B_START     0xFF900000
#define COREB_L1_CODE_START       0xFF600000
#define COREB_L1_DATA_A_START     0xFF400000
#define COREB_L1_DATA_B_START     0xFF500000

#define L1_CODE_START       COREA_L1_CODE_START
#define L1_DATA_A_START     COREA_L1_DATA_A_START
#define L1_DATA_B_START     COREA_L1_DATA_B_START

#define L1_CODE_LENGTH      0x4000

#ifdef CONFIG_BFIN_DCACHE

#ifdef CONFIG_BFIN_DCACHE_BANKA
#define DMEM_CNTR (ACACHE_BSRAM | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      (0x8000 - 0x4000)
#define L1_DATA_B_LENGTH      0x8000
#define BFIN_DCACHESIZE	(16*1024)
#define BFIN_DSUPBANKS	1
#else
#define DMEM_CNTR (ACACHE_BCACHE | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      (0x8000 - 0x4000)
#define L1_DATA_B_LENGTH      (0x8000 - 0x4000)
#define BFIN_DCACHESIZE	(32*1024)
#define BFIN_DSUPBANKS	2
#endif

#else
#define DMEM_CNTR (ASRAM_BSRAM | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      0x8000
#define L1_DATA_B_LENGTH      0x8000
#define BFIN_DCACHESIZE	(0*1024)
#define BFIN_DSUPBANKS	0
#endif /*CONFIG_BFIN_DCACHE*/
#endif

/* Level 2 Memory */
#define L2_START		0xFEB00000
#define L2_LENGTH		0x20000

/* Scratch Pad Memory */

#define COREA_L1_SCRATCH_START	0xFFB00000
#define COREB_L1_SCRATCH_START	0xFF700000

#define L1_SCRATCH_START	COREA_L1_SCRATCH_START
#define L1_SCRATCH_LENGTH	0x1000

#ifdef __ASSEMBLY__

/*
 * The following macros both return the address of the PDA for the
 * current core.
 *
 * In its first safe (and hairy) form, the macro neither clobbers any
 * register aside of the output Preg, nor uses the stack, since it
 * could be called with an invalid stack pointer, or the current stack
 * space being uncovered by any CPLB (e.g. early exception handling).
 *
 * The constraints on the second form are a bit relaxed, and the code
 * is allowed to use the specified Dreg for determining the PDA
 * address to be returned into Preg.
 */
#ifdef CONFIG_SMP
#define GET_PDA_SAFE(preg)		\
	preg.l = lo(DSPID);		\
	preg.h = hi(DSPID);		\
	preg = [preg];			\
	preg = preg << 2;		\
	preg = preg << 2;		\
	preg = preg << 2;		\
	preg = preg << 2;		\
	preg = preg << 2;		\
	preg = preg << 2;		\
	preg = preg << 2;		\
	preg = preg << 2;		\
	preg = preg << 2;		\
	preg = preg << 2;		\
	preg = preg << 2;		\
	preg = preg << 2;		\
	if cc jump 2f;			\
	cc = preg == 0x0;		\
	preg.l = _cpu_pda;		\
	preg.h = _cpu_pda;		\
	if !cc jump 3f;			\
1:					\
	/* preg = 0x0; */		\
	cc = !cc; /* restore cc to 0 */	\
	jump 4f;			\
2:					\
	cc = preg == 0x0;		\
	preg.l = _cpu_pda;		\
	preg.h = _cpu_pda;		\
	if cc jump 4f;			\
	/* preg = 0x1000000; */		\
	cc = !cc; /* restore cc to 1 */	\
3:					\
	preg = [preg];			\
4:

#define GET_PDA(preg, dreg)		\
	preg.l = lo(DSPID);		\
	preg.h = hi(DSPID);		\
	dreg = [preg];			\
	preg.l = _cpu_pda;		\
	preg.h = _cpu_pda;		\
	cc = bittst(dreg, 0);		\
	if !cc jump 1f;			\
	preg = [preg];			\
1:					\

#define GET_CPUID(preg, dreg)		\
	preg.l = lo(DSPID);		\
	preg.h = hi(DSPID);		\
	dreg = [preg];			\
	dreg = ROT dreg BY -1;		\
	dreg = CC;

#else
#define GET_PDA_SAFE(preg)		\
	preg.l = _cpu_pda;		\
	preg.h = _cpu_pda;

#define GET_PDA(preg, dreg)	GET_PDA_SAFE(preg)
#endif /* CONFIG_SMP */

#endif /* __ASSEMBLY__ */

#endif				/* _MEM_MAP_533_H_ */
