#ifndef _ASM_X86_NUMA_32_H
#define _ASM_X86_NUMA_32_H

extern int pxm_to_nid(int pxm);
extern void numa_remove_cpu(int cpu);

#ifdef CONFIG_NUMA
extern void set_highmem_pages_init(void);
#endif

#endif /* _ASM_X86_NUMA_32_H */
