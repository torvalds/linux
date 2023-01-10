/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MEMTYPE_H
#define _ASM_X86_MEMTYPE_H

#include <linux/types.h>
#include <asm/pgtable_types.h>

extern bool pat_enabled(void);
extern void pat_bp_init(void);
extern void pat_cpu_init(void);

extern int memtype_reserve(u64 start, u64 end,
		enum page_cache_mode req_pcm, enum page_cache_mode *ret_pcm);
extern int memtype_free(u64 start, u64 end);

extern int memtype_kernel_map_sync(u64 base, unsigned long size,
		enum page_cache_mode pcm);

extern int memtype_reserve_io(resource_size_t start, resource_size_t end,
			enum page_cache_mode *pcm);

extern void memtype_free_io(resource_size_t start, resource_size_t end);

extern bool pat_pfn_immune_to_uc_mtrr(unsigned long pfn);

bool x86_has_pat_wp(void);
enum page_cache_mode pgprot2cachemode(pgprot_t pgprot);

#endif /* _ASM_X86_MEMTYPE_H */
