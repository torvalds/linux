#ifndef _ASM_X86_NUMA_32_H
#define _ASM_X86_NUMA_32_H

extern int numa_off;

extern int pxm_to_nid(int pxm);

#ifdef CONFIG_NUMA
extern int __cpuinit numa_cpu_node(int cpu);
#else	/* CONFIG_NUMA */
static inline int numa_cpu_node(int cpu)		{ return NUMA_NO_NODE; }
#endif	/* CONFIG_NUMA */

#ifdef CONFIG_HIGHMEM
extern void set_highmem_pages_init(void);
#else
static inline void set_highmem_pages_init(void)
{
}
#endif

#endif /* _ASM_X86_NUMA_32_H */
