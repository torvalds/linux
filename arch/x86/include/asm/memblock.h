#ifndef _X86_MEMBLOCK_H
#define _X86_MEMBLOCK_H

void memblock_x86_reserve_range(u64 start, u64 end, char *name);
void memblock_x86_free_range(u64 start, u64 end);

#endif
