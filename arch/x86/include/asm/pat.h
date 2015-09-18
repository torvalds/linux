#ifndef _ASM_X86_PAT_H
#define _ASM_X86_PAT_H

#include <linux/types.h>
#include <asm/pgtable_types.h>

bool pat_enabled(void);
extern void pat_init(void);
void pat_init_cache_modes(u64);

extern int reserve_memtype(u64 start, u64 end,
		enum page_cache_mode req_pcm, enum page_cache_mode *ret_pcm);
extern int free_memtype(u64 start, u64 end);

extern int kernel_map_sync_memtype(u64 base, unsigned long size,
		enum page_cache_mode pcm);

int io_reserve_memtype(resource_size_t start, resource_size_t end,
			enum page_cache_mode *pcm);

void io_free_memtype(resource_size_t start, resource_size_t end);

#endif /* _ASM_X86_PAT_H */
