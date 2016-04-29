#ifndef _ASM_POWERPC_BOOK3S_64_MMU_H_
#define _ASM_POWERPC_BOOK3S_64_MMU_H_

#ifndef __ASSEMBLY__
/*
 * Page size definition
 *
 *    shift : is the "PAGE_SHIFT" value for that page size
 *    sllp  : is a bit mask with the value of SLB L || LP to be or'ed
 *            directly to a slbmte "vsid" value
 *    penc  : is the HPTE encoding mask for the "LP" field:
 *
 */
struct mmu_psize_def {
	unsigned int	shift;	/* number of bits */
	int		penc[MMU_PAGE_COUNT];	/* HPTE encoding */
	unsigned int	tlbiel;	/* tlbiel supported for that page size */
	unsigned long	avpnm;	/* bits to mask out in AVPN in the HPTE */
	unsigned long	sllp;	/* SLB L||LP (exact mask to use in slbmte) */
};
extern struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT];
#endif /* __ASSEMBLY__ */

/* 64-bit classic hash table MMU */
#include <asm/book3s/64/mmu-hash.h>

#ifndef __ASSEMBLY__
/*
 * ISA 3.0 partiton and process table entry format
 */
struct prtb_entry {
	__be64 prtb0;
	__be64 prtb1;
};
extern struct prtb_entry *process_tb;

struct patb_entry {
	__be64 patb0;
	__be64 patb1;
};
extern struct patb_entry *partition_tb;

#define PATB_HR		(1UL << 63)
#define PATB_GR		(1UL << 63)
#define RPDB_MASK	0x0ffffffffffff00fUL
#define RPDB_SHIFT	(1UL << 8)
/*
 * Limit process table to PAGE_SIZE table. This
 * also limit the max pid we can support.
 * MAX_USER_CONTEXT * 16 bytes of space.
 */
#define PRTB_SIZE_SHIFT	(CONTEXT_BITS + 4)
/*
 * Power9 currently only support 64K partition table size.
 */
#define PATB_SIZE_SHIFT	16

typedef unsigned long mm_context_id_t;
struct spinlock;

typedef struct {
	mm_context_id_t id;
	u16 user_psize;		/* page size index */

#ifdef CONFIG_PPC_MM_SLICES
	u64 low_slices_psize;	/* SLB page size encodings */
	unsigned char high_slices_psize[SLICE_ARRAY_SIZE];
#else
	u16 sllp;		/* SLB page size encoding */
#endif
	unsigned long vdso_base;
#ifdef CONFIG_PPC_SUBPAGE_PROT
	struct subpage_prot_table spt;
#endif /* CONFIG_PPC_SUBPAGE_PROT */
#ifdef CONFIG_PPC_ICSWX
	struct spinlock *cop_lockp; /* guard acop and cop_pid */
	unsigned long acop;	/* mask of enabled coprocessor types */
	unsigned int cop_pid;	/* pid value used with coprocessors */
#endif /* CONFIG_PPC_ICSWX */
#ifdef CONFIG_PPC_64K_PAGES
	/* for 4K PTE fragment support */
	void *pte_frag;
#endif
#ifdef CONFIG_SPAPR_TCE_IOMMU
	struct list_head iommu_group_mem_list;
#endif
} mm_context_t;

/*
 * The current system page and segment sizes
 */
extern int mmu_linear_psize;
extern int mmu_virtual_psize;
extern int mmu_vmalloc_psize;
extern int mmu_vmemmap_psize;
extern int mmu_io_psize;

#endif /* __ASSEMBLY__ */
#endif /* _ASM_POWERPC_BOOK3S_64_MMU_H_ */
