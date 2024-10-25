/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_SPARSEMEM_H
#define _ASM_S390_SPARSEMEM_H

#define SECTION_SIZE_BITS	27
#define MAX_PHYSMEM_BITS	CONFIG_MAX_PHYSMEM_BITS

#ifdef CONFIG_NUMA
static inline int memory_add_physaddr_to_nid(u64 addr)
{
	return 0;
}
#define memory_add_physaddr_to_nid memory_add_physaddr_to_nid
#endif /* CONFIG_NUMA */

#endif /* _ASM_S390_SPARSEMEM_H */
