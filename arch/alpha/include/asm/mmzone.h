/*
 * Written by Kanoj Sarcar (kanoj@sgi.com) Aug 99
 * Adapted for the alpha wildfire architecture Jan 2001.
 */
#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#include <asm/smp.h>

struct bootmem_data_t; /* stupid forward decl. */

/*
 * Following are macros that are specific to this numa platform.
 */

extern pg_data_t node_data[];

#define alpha_pa_to_nid(pa)		\
        (alpha_mv.pa_to_nid 		\
	 ? alpha_mv.pa_to_nid(pa)	\
	 : (0))
#define node_mem_start(nid)		\
        (alpha_mv.node_mem_start 	\
	 ? alpha_mv.node_mem_start(nid) \
	 : (0UL))
#define node_mem_size(nid)		\
        (alpha_mv.node_mem_size 	\
	 ? alpha_mv.node_mem_size(nid) 	\
	 : ((nid) ? (0UL) : (~0UL)))

#define pa_to_nid(pa)		alpha_pa_to_nid(pa)
#define NODE_DATA(nid)		(&node_data[(nid)])

#define node_localnr(pfn, nid)	((pfn) - NODE_DATA(nid)->node_start_pfn)

#if 1
#define PLAT_NODE_DATA_LOCALNR(p, n)	\
	(((p) >> PAGE_SHIFT) - PLAT_NODE_DATA(n)->gendata.node_start_pfn)
#else
static inline unsigned long
PLAT_NODE_DATA_LOCALNR(unsigned long p, int n)
{
	unsigned long temp;
	temp = p >> PAGE_SHIFT;
	return temp - PLAT_NODE_DATA(n)->gendata.node_start_pfn;
}
#endif

#ifdef CONFIG_DISCONTIGMEM

/*
 * Following are macros that each numa implementation must define.
 */

/*
 * Given a kernel address, find the home node of the underlying memory.
 */
#define kvaddr_to_nid(kaddr)	pa_to_nid(__pa(kaddr))

/*
 * Given a kaddr, LOCAL_BASE_ADDR finds the owning node of the memory
 * and returns the kaddr corresponding to first physical page in the
 * node's mem_map.
 */
#define LOCAL_BASE_ADDR(kaddr)						  \
    ((unsigned long)__va(NODE_DATA(kvaddr_to_nid(kaddr))->node_start_pfn  \
			 << PAGE_SHIFT))

/* XXX: FIXME -- wli */
#define kern_addr_valid(kaddr)	(0)

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)

#define VALID_PAGE(page)	(((page) - mem_map) < max_mapnr)

#define pmd_page(pmd)		(pfn_to_page(pmd_val(pmd) >> 32))
#define pgd_page(pgd)		(pfn_to_page(pgd_val(pgd) >> 32))
#define pte_pfn(pte)		(pte_val(pte) >> 32)

#define mk_pte(page, pgprot)						     \
({								 	     \
	pte_t pte;                                                           \
	unsigned long pfn;                                                   \
									     \
	pfn = page_to_pfn(page) << 32; \
	pte_val(pte) = pfn | pgprot_val(pgprot);			     \
									     \
	pte;								     \
})

#define pte_page(x)							\
({									\
       	unsigned long kvirt;						\
	struct page * __xx;						\
									\
	kvirt = (unsigned long)__va(pte_val(x) >> (32-PAGE_SHIFT));	\
	__xx = virt_to_page(kvirt);					\
									\
	__xx;                                                           \
})

#define page_to_pa(page)						\
	(page_to_pfn(page) << PAGE_SHIFT)

#define pfn_to_nid(pfn)		pa_to_nid(((u64)(pfn) << PAGE_SHIFT))
#define pfn_valid(pfn)							\
	(((pfn) - node_start_pfn(pfn_to_nid(pfn))) <			\
	 node_spanned_pages(pfn_to_nid(pfn)))					\

#define virt_addr_valid(kaddr)	pfn_valid((__pa(kaddr) >> PAGE_SHIFT))

#endif /* CONFIG_DISCONTIGMEM */

#endif /* _ASM_MMZONE_H_ */
