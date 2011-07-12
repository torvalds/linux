#ifndef _X86_MEMBLOCK_H
#define _X86_MEMBLOCK_H

#define ARCH_DISCARD_MEMBLOCK

void memblock_x86_reserve_range(u64 start, u64 end, char *name);
void memblock_x86_free_range(u64 start, u64 end);

u64 memblock_x86_hole_size(u64 start, u64 end);
u64 memblock_x86_free_memory_in_range(u64 addr, u64 limit);
u64 memblock_x86_memory_in_range(u64 addr, u64 limit);

#endif
