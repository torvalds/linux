/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_SPARSEMEM_H
#define _ASM_IA64_SPARSEMEM_H

#ifdef CONFIG_SPARSEMEM
#include <asm/page.h>
/*
 * SECTION_SIZE_BITS            2^N: how big each section will be
 * MAX_PHYSMEM_BITS             2^N: how much memory we can have in that space
 */

#define SECTION_SIZE_BITS	(30)
#define MAX_PHYSMEM_BITS	(50)
#ifdef CONFIG_ARCH_FORCE_MAX_ORDER
#if ((CONFIG_ARCH_FORCE_MAX_ORDER - 1 + PAGE_SHIFT) > SECTION_SIZE_BITS)
#undef SECTION_SIZE_BITS
#define SECTION_SIZE_BITS (CONFIG_ARCH_FORCE_MAX_ORDER - 1 + PAGE_SHIFT)
#endif
#endif

#endif /* CONFIG_SPARSEMEM */

#ifdef CONFIG_MEMORY_HOTPLUG
int memory_add_physaddr_to_nid(u64 addr);
#define memory_add_physaddr_to_nid memory_add_physaddr_to_nid
#endif

#endif /* _ASM_IA64_SPARSEMEM_H */
