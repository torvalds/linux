/* SPDX-License-Identifier: GPL-2.0 */
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
	union {
		unsigned long	sllp;	/* SLB L||LP (exact mask to use in slbmte) */
		unsigned long ap;	/* Ap encoding used by PowerISA 3.0 */
	};
};
extern struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT];

#endif /* __ASSEMBLY__ */

/* 64-bit classic hash table MMU */
#include <asm/book3s/64/mmu-hash.h>

#ifndef __ASSEMBLY__
/*
 * ISA 3.0 partition and process table entry format
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

/* Bits in patb0 field */
#define PATB_HR		(1UL << 63)
#define RPDB_MASK	0x0fffffffffffff00UL
#define RPDB_SHIFT	(1UL << 8)
#define RTS1_SHIFT	61		/* top 2 bits of radix tree size */
#define RTS1_MASK	(3UL << RTS1_SHIFT)
#define RTS2_SHIFT	5		/* bottom 3 bits of radix tree size */
#define RTS2_MASK	(7UL << RTS2_SHIFT)
#define RPDS_MASK	0x1f		/* root page dir. size field */

/* Bits in patb1 field */
#define PATB_GR		(1UL << 63)	/* guest uses radix; must match HR */
#define PRTS_MASK	0x1f		/* process table size field */
#define PRTB_MASK	0x0ffffffffffff000UL

/* Number of supported PID bits */
extern unsigned int mmu_pid_bits;

/* Base PID to allocate from */
extern unsigned int mmu_base_pid;

#define PRTB_SIZE_SHIFT	(mmu_pid_bits + 4)
#define PRTB_ENTRIES	(1ul << mmu_pid_bits)

/*
 * Power9 currently only support 64K partition table size.
 */
#define PATB_SIZE_SHIFT	16

typedef unsigned long mm_context_id_t;
struct spinlock;

/* Maximum possible number of NPUs in a system. */
#define NV_MAX_NPUS 8

typedef struct {
	mm_context_id_t id;
	u16 user_psize;		/* page size index */

	/* Number of bits in the mm_cpumask */
	atomic_t active_cpus;

	/* NPU NMMU context */
	struct npu_context *npu_context;

#ifdef CONFIG_PPC_MM_SLICES
	u64 low_slices_psize;	/* SLB page size encodings */
	unsigned char high_slices_psize[SLICE_ARRAY_SIZE];
	unsigned long addr_limit;
#else
	u16 sllp;		/* SLB page size encoding */
#endif
	unsigned long vdso_base;
#ifdef CONFIG_PPC_SUBPAGE_PROT
	struct subpage_prot_table spt;
#endif /* CONFIG_PPC_SUBPAGE_PROT */
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

/* MMU initialization */
void mmu_early_init_devtree(void);
void hash__early_init_devtree(void);
void radix__early_init_devtree(void);
extern void radix_init_native(void);
extern void hash__early_init_mmu(void);
extern void radix__early_init_mmu(void);
static inline void early_init_mmu(void)
{
	if (radix_enabled())
		return radix__early_init_mmu();
	return hash__early_init_mmu();
}
extern void hash__early_init_mmu_secondary(void);
extern void radix__early_init_mmu_secondary(void);
static inline void early_init_mmu_secondary(void)
{
	if (radix_enabled())
		return radix__early_init_mmu_secondary();
	return hash__early_init_mmu_secondary();
}

extern void hash__setup_initial_memory_limit(phys_addr_t first_memblock_base,
					 phys_addr_t first_memblock_size);
extern void radix__setup_initial_memory_limit(phys_addr_t first_memblock_base,
					 phys_addr_t first_memblock_size);
static inline void setup_initial_memory_limit(phys_addr_t first_memblock_base,
					      phys_addr_t first_memblock_size)
{
	if (early_radix_enabled())
		return radix__setup_initial_memory_limit(first_memblock_base,
						   first_memblock_size);
	return hash__setup_initial_memory_limit(first_memblock_base,
					   first_memblock_size);
}

extern int (*register_process_table)(unsigned long base, unsigned long page_size,
				     unsigned long tbl_size);

#ifdef CONFIG_PPC_PSERIES
extern void radix_init_pseries(void);
#else
static inline void radix_init_pseries(void) { };
#endif

#endif /* __ASSEMBLY__ */
#endif /* _ASM_POWERPC_BOOK3S_64_MMU_H_ */
