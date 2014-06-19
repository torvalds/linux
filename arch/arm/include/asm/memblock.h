#ifndef _ASM_ARM_MEMBLOCK_H
#define _ASM_ARM_MEMBLOCK_H

struct machine_desc;

void arm_memblock_init(const struct machine_desc *);
phys_addr_t arm_memblock_steal(phys_addr_t size, phys_addr_t align);

#endif
