/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PAT_H
#define _ASM_X86_PAT_H

#include <linux/types.h>
#include <asm/pgtable_types.h>

bool pat_enabled(void);
void pat_disable(const char *reason);
extern void pat_init(void);
extern void init_cache_modes(void);

extern int reserve_memtype(u64 start, u64 end,
		enum page_cache_mode req_pcm, enum page_cache_mode *ret_pcm);
extern int free_memtype(u64 start, u64 end);

extern int kernel_map_sync_memtype(u64 base, unsigned long size,
		enum page_cache_mode pcm);

int io_reserve_memtype(resource_size_t start, resource_size_t end,
			enum page_cache_mode *pcm);

void io_free_memtype(resource_size_t start, resource_size_t end);

bool pat_pfn_immune_to_uc_mtrr(unsigned long pfn);

#endif /* _ASM_X86_PAT_H */
