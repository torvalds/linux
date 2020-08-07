/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_POWERPC_KEXEC_RANGES_H
#define _ASM_POWERPC_KEXEC_RANGES_H

#define MEM_RANGE_CHUNK_SZ		2048	/* Memory ranges size chunk */

void sort_memory_ranges(struct crash_mem *mrngs, bool merge);
struct crash_mem *realloc_mem_ranges(struct crash_mem **mem_ranges);
int add_mem_range(struct crash_mem **mem_ranges, u64 base, u64 size);
int add_tce_mem_ranges(struct crash_mem **mem_ranges);
int add_initrd_mem_range(struct crash_mem **mem_ranges);
#ifdef CONFIG_PPC_BOOK3S_64
int add_htab_mem_range(struct crash_mem **mem_ranges);
#else
static inline int add_htab_mem_range(struct crash_mem **mem_ranges)
{
	return 0;
}
#endif
int add_kernel_mem_range(struct crash_mem **mem_ranges);
int add_rtas_mem_range(struct crash_mem **mem_ranges);
int add_opal_mem_range(struct crash_mem **mem_ranges);
int add_reserved_mem_ranges(struct crash_mem **mem_ranges);

#endif /* _ASM_POWERPC_KEXEC_RANGES_H */
