#ifndef _ASM_ARM_MEMBLOCK_H
#define _ASM_ARM_MEMBLOCK_H

#ifdef CONFIG_MMU
extern phys_addr_t lowmem_end_addr;
#define MEMBLOCK_REAL_LIMIT	lowmem_end_addr
#else
#define MEMBLOCK_REAL_LIMIT	0
#endif

struct meminfo;
struct machine_desc;

extern void arm_memblock_init(struct meminfo *, struct machine_desc *);

#endif
