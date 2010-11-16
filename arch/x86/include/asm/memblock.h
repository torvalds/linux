#ifndef _X86_MEMBLOCK_H
#define _X86_MEMBLOCK_H

#define ARCH_DISCARD_MEMBLOCK

u64 memblock_x86_find_in_range_size(u64 start, u64 *sizep, u64 align);
void memblock_x86_to_bootmem(u64 start, u64 end);

void memblock_x86_reserve_range(u64 start, u64 end, char *name);
void memblock_x86_free_range(u64 start, u64 end);
struct range;
int __get_free_all_memory_range(struct range **range, int nodeid,
			 unsigned long start_pfn, unsigned long end_pfn);
int get_free_all_memory_range(struct range **rangep, int nodeid);

void memblock_x86_register_active_regions(int nid, unsigned long start_pfn,
					 unsigned long last_pfn);
u64 memblock_x86_hole_size(u64 start, u64 end);
u64 memblock_x86_find_in_range_node(int nid, u64 start, u64 end, u64 size, u64 align);
u64 memblock_x86_free_memory_in_range(u64 addr, u64 limit);
u64 memblock_x86_memory_in_range(u64 addr, u64 limit);

#endif
