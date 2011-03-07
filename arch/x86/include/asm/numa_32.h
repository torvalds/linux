#ifndef _ASM_X86_NUMA_32_H
#define _ASM_X86_NUMA_32_H

extern int numa_off;

extern int pxm_to_nid(int pxm);
extern void numa_remove_cpu(int cpu);

#ifdef CONFIG_HIGHMEM
extern void set_highmem_pages_init(void);
#else
static inline void set_highmem_pages_init(void)
{
}
#endif

#endif /* _ASM_X86_NUMA_32_H */
