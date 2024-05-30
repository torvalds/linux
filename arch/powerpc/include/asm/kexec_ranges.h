/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_POWERPC_KEXEC_RANGES_H
#define _ASM_POWERPC_KEXEC_RANGES_H

#define MEM_RANGE_CHUNK_SZ		2048	/* Memory ranges size chunk */

void sort_memory_ranges(struct crash_mem *mrngs, bool merge);
struct crash_mem *realloc_mem_ranges(struct crash_mem **mem_ranges);
int add_mem_range(struct crash_mem **mem_ranges, u64 base, u64 size);
int remove_mem_range(struct crash_mem **mem_ranges, u64 base, u64 size);
int get_exclude_memory_ranges(struct crash_mem **mem_ranges);
int get_reserved_memory_ranges(struct crash_mem **mem_ranges);
int get_crash_memory_ranges(struct crash_mem **mem_ranges);
int get_usable_memory_ranges(struct crash_mem **mem_ranges);
#endif /* _ASM_POWERPC_KEXEC_RANGES_H */
