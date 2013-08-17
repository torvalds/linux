/*
 * BF561 memory map
 *
 * Copyright 2004-2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#ifndef __BFIN_MACH_MEM_MAP_H__
#define __BFIN_MACH_MEM_MAP_H__

#ifndef __BFIN_MEM_MAP_H__
# error "do not include mach/mem_map.h directly -- use asm/mem_map.h"
#endif

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

/*
 * If we are in SMP mode, then the cache settings of Core B will match
 * the settings of Core A.  If we aren't, then we assume Core B is not
 * using any cache.  This allows the rest of the kernel to work with
 * the core in either mode as we are only loading user code into it and
 * it is the user's problem to make sure they aren't doing something
 * stupid there.
 *
 * Note that we treat the L1 code region as a contiguous blob to make
 * the rest of the kernel simpler.  Easier to check one region than a
 * bunch of small ones.  Again, possible misbehavior here is the fault
 * of the user -- don't try to use memory that doesn't exist.
 */
#ifdef CONFIG_SMP
# define COREB_L1_CODE_LENGTH     L1_CODE_LENGTH
# define COREB_L1_DATA_A_LENGTH   L1_DATA_A_LENGTH
# define COREB_L1_DATA_B_LENGTH   L1_DATA_B_LENGTH
#else
# define COREB_L1_CODE_LENGTH     0x14000
# define COREB_L1_DATA_A_LENGTH   0x8000
# define COREB_L1_DATA_B_LENGTH   0x8000
#endif

/* Level 2 Memory */
#define L2_START		0xFEB00000
#define L2_LENGTH		0x20000

/* Scratch Pad Memory */

#define COREA_L1_SCRATCH_START	0xFFB00000
#define COREB_L1_SCRATCH_START	0xFF700000

#ifdef CONFIG_SMP

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
# define GET_PDA_SAFE(preg)		\
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

# define GET_PDA(preg, dreg)		\
	preg.l = lo(DSPID);		\
	preg.h = hi(DSPID);		\
	dreg = [preg];			\
	preg.l = _cpu_pda;		\
	preg.h = _cpu_pda;		\
	cc = bittst(dreg, 0);		\
	if !cc jump 1f;			\
	preg = [preg];			\
1:					\

# define GET_CPUID(preg, dreg)		\
	preg.l = lo(DSPID);		\
	preg.h = hi(DSPID);		\
	dreg = [preg];			\
	dreg = ROT dreg BY -1;		\
	dreg = CC;

# ifndef __ASSEMBLY__

#  include <asm/processor.h>

static inline unsigned long get_l1_scratch_start_cpu(int cpu)
{
	return cpu ? COREB_L1_SCRATCH_START : COREA_L1_SCRATCH_START;
}
static inline unsigned long get_l1_code_start_cpu(int cpu)
{
	return cpu ? COREB_L1_CODE_START : COREA_L1_CODE_START;
}
static inline unsigned long get_l1_data_a_start_cpu(int cpu)
{
	return cpu ? COREB_L1_DATA_A_START : COREA_L1_DATA_A_START;
}
static inline unsigned long get_l1_data_b_start_cpu(int cpu)
{
	return cpu ? COREB_L1_DATA_B_START : COREA_L1_DATA_B_START;
}

static inline unsigned long get_l1_scratch_start(void)
{
	return get_l1_scratch_start_cpu(blackfin_core_id());
}
static inline unsigned long get_l1_code_start(void)
{
	return get_l1_code_start_cpu(blackfin_core_id());
}
static inline unsigned long get_l1_data_a_start(void)
{
	return get_l1_data_a_start_cpu(blackfin_core_id());
}
static inline unsigned long get_l1_data_b_start(void)
{
	return get_l1_data_b_start_cpu(blackfin_core_id());
}

# endif /* __ASSEMBLY__ */
#endif /* CONFIG_SMP */

#endif
