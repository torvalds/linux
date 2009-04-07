#ifndef _ASM_X86_PAT_H
#define _ASM_X86_PAT_H

#include <linux/types.h>
#include <asm/pgtable_types.h>

#ifdef CONFIG_X86_PAT
extern int pat_enabled;
#else
static const int pat_enabled;
#endif

extern void pat_init(void);

extern int reserve_memtype(u64 start, u64 end,
		unsigned long req_type, unsigned long *ret_type);
extern int free_memtype(u64 start, u64 end);

extern int kernel_map_sync_memtype(u64 base, unsigned long size,
		unsigned long flag);
extern void map_devmem(unsigned long pfn, unsigned long size,
		       struct pgprot vma_prot);
extern void unmap_devmem(unsigned long pfn, unsigned long size,
			 struct pgprot vma_prot);

#endif /* _ASM_X86_PAT_H */
