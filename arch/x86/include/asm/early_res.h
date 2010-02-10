#ifndef _ASM_X86_EARLY_RES_H
#define _ASM_X86_EARLY_RES_H
#ifdef __KERNEL__

extern void reserve_early(u64 start, u64 end, char *name);
extern void reserve_early_overlap_ok(u64 start, u64 end, char *name);
extern void free_early(u64 start, u64 end);
extern void early_res_to_bootmem(u64 start, u64 end);

void reserve_early_without_check(u64 start, u64 end, char *name);
u64 find_early_area(u64 ei_start, u64 ei_last, u64 start, u64 end,
			 u64 size, u64 align);
u64 find_early_area_size(u64 ei_start, u64 ei_last, u64 start,
			 u64 *sizep, u64 align);
#include <linux/range.h>
int get_free_all_memory_range(struct range **rangep, int nodeid);

#endif /* __KERNEL__ */

#endif /* _ASM_X86_EARLY_RES_H */
