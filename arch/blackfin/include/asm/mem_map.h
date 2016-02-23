/*
 * Common Blackfin memory map
 *
 * Copyright 2004-2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#ifndef __BFIN_MEM_MAP_H__
#define __BFIN_MEM_MAP_H__

#include <mach/mem_map.h>

/* Every Blackfin so far has MMRs like this */
#ifndef COREMMR_BASE
# define COREMMR_BASE 0xFFE00000
#endif
#ifndef SYSMMR_BASE
# define SYSMMR_BASE  0xFFC00000
#endif

/* Every Blackfin so far has on-chip Scratch Pad SRAM like this */
#ifndef L1_SCRATCH_START
# define L1_SCRATCH_START  0xFFB00000
# define L1_SCRATCH_LENGTH 0x1000
#endif

/* Most parts lack on-chip L2 SRAM */
#ifndef L2_START
# define L2_START  0
# define L2_LENGTH 0
#endif

/* Most parts lack on-chip L1 ROM */
#ifndef L1_ROM_START
# define L1_ROM_START  0
# define L1_ROM_LENGTH 0
#endif

/* Allow wonky SMP ports to override this */
#ifndef GET_PDA_SAFE
# define GET_PDA_SAFE(preg) \
	preg.l = _cpu_pda; \
	preg.h = _cpu_pda;
# define GET_PDA(preg, dreg) GET_PDA_SAFE(preg)

# ifndef __ASSEMBLY__

static inline unsigned long get_l1_scratch_start_cpu(int cpu)
{
	return L1_SCRATCH_START;
}
static inline unsigned long get_l1_code_start_cpu(int cpu)
{
	return L1_CODE_START;
}
static inline unsigned long get_l1_data_a_start_cpu(int cpu)
{
	return L1_DATA_A_START;
}
static inline unsigned long get_l1_data_b_start_cpu(int cpu)
{
	return L1_DATA_B_START;
}
static inline unsigned long get_l1_scratch_start(void)
{
	return get_l1_scratch_start_cpu(0);
}
static inline unsigned long get_l1_code_start(void)
{
	return  get_l1_code_start_cpu(0);
}
static inline unsigned long get_l1_data_a_start(void)
{
	return get_l1_data_a_start_cpu(0);
}
static inline unsigned long get_l1_data_b_start(void)
{
	return get_l1_data_b_start_cpu(0);
}

# endif /* __ASSEMBLY__ */
#endif /* !GET_PDA_SAFE */

#endif
