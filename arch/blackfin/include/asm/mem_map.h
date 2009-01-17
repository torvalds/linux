/*
 * mem_map.h
 * Common header file for blackfin family of processors.
 *
 */

#ifndef _MEM_MAP_H_
#define _MEM_MAP_H_

#include <mach/mem_map.h>

#ifndef __ASSEMBLY__

#ifdef CONFIG_SMP
static inline ulong get_l1_scratch_start_cpu(int cpu)
{
	return (cpu) ? COREB_L1_SCRATCH_START : COREA_L1_SCRATCH_START;
}
static inline ulong get_l1_code_start_cpu(int cpu)
{
	return (cpu) ? COREB_L1_CODE_START : COREA_L1_CODE_START;
}
static inline ulong get_l1_data_a_start_cpu(int cpu)
{
	return (cpu) ? COREB_L1_DATA_A_START : COREA_L1_DATA_A_START;
}
static inline ulong get_l1_data_b_start_cpu(int cpu)
{
	return (cpu) ? COREB_L1_DATA_B_START : COREA_L1_DATA_B_START;
}

static inline ulong get_l1_scratch_start(void)
{
	return get_l1_scratch_start_cpu(blackfin_core_id());
}
static inline ulong get_l1_code_start(void)
{
	return get_l1_code_start_cpu(blackfin_core_id());
}
static inline ulong get_l1_data_a_start(void)
{
	return get_l1_data_a_start_cpu(blackfin_core_id());
}
static inline ulong get_l1_data_b_start(void)
{
	return get_l1_data_b_start_cpu(blackfin_core_id());
}

#else /* !CONFIG_SMP */

static inline ulong get_l1_scratch_start_cpu(int cpu)
{
	return L1_SCRATCH_START;
}
static inline ulong get_l1_code_start_cpu(int cpu)
{
	return L1_CODE_START;
}
static inline ulong get_l1_data_a_start_cpu(int cpu)
{
	return L1_DATA_A_START;
}
static inline ulong get_l1_data_b_start_cpu(int cpu)
{
	return L1_DATA_B_START;
}
static inline ulong get_l1_scratch_start(void)
{
	return get_l1_scratch_start_cpu(0);
}
static inline ulong get_l1_code_start(void)
{
	return  get_l1_code_start_cpu(0);
}
static inline ulong get_l1_data_a_start(void)
{
	return get_l1_data_a_start_cpu(0);
}
static inline ulong get_l1_data_b_start(void)
{
	return get_l1_data_b_start_cpu(0);
}

#endif /* CONFIG_SMP */
#endif /* __ASSEMBLY__ */

#endif				/* _MEM_MAP_H_ */
