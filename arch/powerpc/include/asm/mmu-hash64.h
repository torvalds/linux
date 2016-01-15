#ifndef _ASM_POWERPC_MMU_HASH64_H_
#define _ASM_POWERPC_MMU_HASH64_H_
/*
 * PowerPC64 memory management structures
 *
 * Dave Engebretsen & Mike Corrigan <{engebret|mikejc}@us.ibm.com>
 *   PPC64 rework.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/asm-compat.h>
#include <asm/page.h>
#include <asm/bug.h>

/*
 * This is necessary to get the definition of PGTABLE_RANGE which we
 * need for various slices related matters. Note that this isn't the
 * complete pgtable.h but only a portion of it.
 */
#include <asm/book3s/64/pgtable.h>
#include <asm/bug.h>
#include <asm/processor.h>

/*
 * SLB
 */

#define SLB_NUM_BOLTED		3
#define SLB_CACHE_ENTRIES	8
#define SLB_MIN_SIZE		32

/* Bits in the SLB ESID word */
#define SLB_ESID_V		ASM_CONST(0x0000000008000000) /* valid */

/* Bits in the SLB VSID word */
#define SLB_VSID_SHIFT		12
#define SLB_VSID_SHIFT_1T	24
#define SLB_VSID_SSIZE_SHIFT	62
#define SLB_VSID_B		ASM_CONST(0xc000000000000000)
#define SLB_VSID_B_256M		ASM_CONST(0x0000000000000000)
#define SLB_VSID_B_1T		ASM_CONST(0x4000000000000000)
#define SLB_VSID_KS		ASM_CONST(0x0000000000000800)
#define SLB_VSID_KP		ASM_CONST(0x0000000000000400)
#define SLB_VSID_N		ASM_CONST(0x0000000000000200) /* no-execute */
#define SLB_VSID_L		ASM_CONST(0x0000000000000100)
#define SLB_VSID_C		ASM_CONST(0x0000000000000080) /* class */
#define SLB_VSID_LP		ASM_CONST(0x0000000000000030)
#define SLB_VSID_LP_00		ASM_CONST(0x0000000000000000)
#define SLB_VSID_LP_01		ASM_CONST(0x0000000000000010)
#define SLB_VSID_LP_10		ASM_CONST(0x0000000000000020)
#define SLB_VSID_LP_11		ASM_CONST(0x0000000000000030)
#define SLB_VSID_LLP		(SLB_VSID_L|SLB_VSID_LP)

#define SLB_VSID_KERNEL		(SLB_VSID_KP)
#define SLB_VSID_USER		(SLB_VSID_KP|SLB_VSID_KS|SLB_VSID_C)

#define SLBIE_C			(0x08000000)
#define SLBIE_SSIZE_SHIFT	25

/*
 * Hash table
 */

#define HPTES_PER_GROUP 8

#define HPTE_V_SSIZE_SHIFT	62
#define HPTE_V_AVPN_SHIFT	7
#define HPTE_V_AVPN		ASM_CONST(0x3fffffffffffff80)
#define HPTE_V_AVPN_VAL(x)	(((x) & HPTE_V_AVPN) >> HPTE_V_AVPN_SHIFT)
#define HPTE_V_COMPARE(x,y)	(!(((x) ^ (y)) & 0xffffffffffffff80UL))
#define HPTE_V_BOLTED		ASM_CONST(0x0000000000000010)
#define HPTE_V_LOCK		ASM_CONST(0x0000000000000008)
#define HPTE_V_LARGE		ASM_CONST(0x0000000000000004)
#define HPTE_V_SECONDARY	ASM_CONST(0x0000000000000002)
#define HPTE_V_VALID		ASM_CONST(0x0000000000000001)

#define HPTE_R_PP0		ASM_CONST(0x8000000000000000)
#define HPTE_R_TS		ASM_CONST(0x4000000000000000)
#define HPTE_R_KEY_HI		ASM_CONST(0x3000000000000000)
#define HPTE_R_RPN_SHIFT	12
#define HPTE_R_RPN		ASM_CONST(0x0ffffffffffff000)
#define HPTE_R_PP		ASM_CONST(0x0000000000000003)
#define HPTE_R_N		ASM_CONST(0x0000000000000004)
#define HPTE_R_G		ASM_CONST(0x0000000000000008)
#define HPTE_R_M		ASM_CONST(0x0000000000000010)
#define HPTE_R_I		ASM_CONST(0x0000000000000020)
#define HPTE_R_W		ASM_CONST(0x0000000000000040)
#define HPTE_R_WIMG		ASM_CONST(0x0000000000000078)
#define HPTE_R_C		ASM_CONST(0x0000000000000080)
#define HPTE_R_R		ASM_CONST(0x0000000000000100)
#define HPTE_R_KEY_LO		ASM_CONST(0x0000000000000e00)

#define HPTE_V_1TB_SEG		ASM_CONST(0x4000000000000000)
#define HPTE_V_VRMA_MASK	ASM_CONST(0x4001ffffff000000)

/* Values for PP (assumes Ks=0, Kp=1) */
#define PP_RWXX	0	/* Supervisor read/write, User none */
#define PP_RWRX 1	/* Supervisor read/write, User read */
#define PP_RWRW 2	/* Supervisor read/write, User read/write */
#define PP_RXRX 3	/* Supervisor read,       User read */
#define PP_RXXX	(HPTE_R_PP0 | 2)	/* Supervisor read, user none */

/* Fields for tlbiel instruction in architecture 2.06 */
#define TLBIEL_INVAL_SEL_MASK	0xc00	/* invalidation selector */
#define  TLBIEL_INVAL_PAGE	0x000	/* invalidate a single page */
#define  TLBIEL_INVAL_SET_LPID	0x800	/* invalidate a set for current LPID */
#define  TLBIEL_INVAL_SET	0xc00	/* invalidate a set for all LPIDs */
#define TLBIEL_INVAL_SET_MASK	0xfff000	/* set number to inval. */
#define TLBIEL_INVAL_SET_SHIFT	12

#define POWER7_TLB_SETS		128	/* # sets in POWER7 TLB */
#define POWER8_TLB_SETS		512	/* # sets in POWER8 TLB */

#ifndef __ASSEMBLY__

struct hash_pte {
	__be64 v;
	__be64 r;
};

extern struct hash_pte *htab_address;
extern unsigned long htab_size_bytes;
extern unsigned long htab_hash_mask;

/*
 * Page size definition
 *
 *    shift : is the "PAGE_SHIFT" value for that page size
 *    sllp  : is a bit mask with the value of SLB L || LP to be or'ed
 *            directly to a slbmte "vsid" value
 *    penc  : is the HPTE encoding mask for the "LP" field:
 *
 */
struct mmu_psize_def
{
	unsigned int	shift;	/* number of bits */
	int		penc[MMU_PAGE_COUNT];	/* HPTE encoding */
	unsigned int	tlbiel;	/* tlbiel supported for that page size */
	unsigned long	avpnm;	/* bits to mask out in AVPN in the HPTE */
	unsigned long	sllp;	/* SLB L||LP (exact mask to use in slbmte) */
};
extern struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT];

static inline int shift_to_mmu_psize(unsigned int shift)
{
	int psize;

	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize)
		if (mmu_psize_defs[psize].shift == shift)
			return psize;
	return -1;
}

static inline unsigned int mmu_psize_to_shift(unsigned int mmu_psize)
{
	if (mmu_psize_defs[mmu_psize].shift)
		return mmu_psize_defs[mmu_psize].shift;
	BUG();
}

#endif /* __ASSEMBLY__ */

/*
 * Segment sizes.
 * These are the values used by hardware in the B field of
 * SLB entries and the first dword of MMU hashtable entries.
 * The B field is 2 bits; the values 2 and 3 are unused and reserved.
 */
#define MMU_SEGSIZE_256M	0
#define MMU_SEGSIZE_1T		1

/*
 * encode page number shift.
 * in order to fit the 78 bit va in a 64 bit variable we shift the va by
 * 12 bits. This enable us to address upto 76 bit va.
 * For hpt hash from a va we can ignore the page size bits of va and for
 * hpte encoding we ignore up to 23 bits of va. So ignoring lower 12 bits ensure
 * we work in all cases including 4k page size.
 */
#define VPN_SHIFT	12

/*
 * HPTE Large Page (LP) details
 */
#define LP_SHIFT	12
#define LP_BITS		8
#define LP_MASK(i)	((0xFF >> (i)) << LP_SHIFT)

#ifndef __ASSEMBLY__

static inline int slb_vsid_shift(int ssize)
{
	if (ssize == MMU_SEGSIZE_256M)
		return SLB_VSID_SHIFT;
	return SLB_VSID_SHIFT_1T;
}

static inline int segment_shift(int ssize)
{
	if (ssize == MMU_SEGSIZE_256M)
		return SID_SHIFT;
	return SID_SHIFT_1T;
}

/*
 * The current system page and segment sizes
 */
extern int mmu_linear_psize;
extern int mmu_virtual_psize;
extern int mmu_vmalloc_psize;
extern int mmu_vmemmap_psize;
extern int mmu_io_psize;
extern int mmu_kernel_ssize;
extern int mmu_highuser_ssize;
extern u16 mmu_slb_size;
extern unsigned long tce_alloc_start, tce_alloc_end;

/*
 * If the processor supports 64k normal pages but not 64k cache
 * inhibited pages, we have to be prepared to switch processes
 * to use 4k pages when they create cache-inhibited mappings.
 * If this is the case, mmu_ci_restrictions will be set to 1.
 */
extern int mmu_ci_restrictions;

/*
 * This computes the AVPN and B fields of the first dword of a HPTE,
 * for use when we want to match an existing PTE.  The bottom 7 bits
 * of the returned value are zero.
 */
static inline unsigned long hpte_encode_avpn(unsigned long vpn, int psize,
					     int ssize)
{
	unsigned long v;
	/*
	 * The AVA field omits the low-order 23 bits of the 78 bits VA.
	 * These bits are not needed in the PTE, because the
	 * low-order b of these bits are part of the byte offset
	 * into the virtual page and, if b < 23, the high-order
	 * 23-b of these bits are always used in selecting the
	 * PTEGs to be searched
	 */
	v = (vpn >> (23 - VPN_SHIFT)) & ~(mmu_psize_defs[psize].avpnm);
	v <<= HPTE_V_AVPN_SHIFT;
	v |= ((unsigned long) ssize) << HPTE_V_SSIZE_SHIFT;
	return v;
}

/*
 * This function sets the AVPN and L fields of the HPTE  appropriately
 * using the base page size and actual page size.
 */
static inline unsigned long hpte_encode_v(unsigned long vpn, int base_psize,
					  int actual_psize, int ssize)
{
	unsigned long v;
	v = hpte_encode_avpn(vpn, base_psize, ssize);
	if (actual_psize != MMU_PAGE_4K)
		v |= HPTE_V_LARGE;
	return v;
}

/*
 * This function sets the ARPN, and LP fields of the HPTE appropriately
 * for the page size. We assume the pa is already "clean" that is properly
 * aligned for the requested page size
 */
static inline unsigned long hpte_encode_r(unsigned long pa, int base_psize,
					  int actual_psize)
{
	/* A 4K page needs no special encoding */
	if (actual_psize == MMU_PAGE_4K)
		return pa & HPTE_R_RPN;
	else {
		unsigned int penc = mmu_psize_defs[base_psize].penc[actual_psize];
		unsigned int shift = mmu_psize_defs[actual_psize].shift;
		return (pa & ~((1ul << shift) - 1)) | (penc << LP_SHIFT);
	}
}

/*
 * Build a VPN_SHIFT bit shifted va given VSID, EA and segment size.
 */
static inline unsigned long hpt_vpn(unsigned long ea,
				    unsigned long vsid, int ssize)
{
	unsigned long mask;
	int s_shift = segment_shift(ssize);

	mask = (1ul << (s_shift - VPN_SHIFT)) - 1;
	return (vsid << (s_shift - VPN_SHIFT)) | ((ea >> VPN_SHIFT) & mask);
}

/*
 * This hashes a virtual address
 */
static inline unsigned long hpt_hash(unsigned long vpn,
				     unsigned int shift, int ssize)
{
	int mask;
	unsigned long hash, vsid;

	/* VPN_SHIFT can be atmost 12 */
	if (ssize == MMU_SEGSIZE_256M) {
		mask = (1ul << (SID_SHIFT - VPN_SHIFT)) - 1;
		hash = (vpn >> (SID_SHIFT - VPN_SHIFT)) ^
			((vpn & mask) >> (shift - VPN_SHIFT));
	} else {
		mask = (1ul << (SID_SHIFT_1T - VPN_SHIFT)) - 1;
		vsid = vpn >> (SID_SHIFT_1T - VPN_SHIFT);
		hash = vsid ^ (vsid << 25) ^
			((vpn & mask) >> (shift - VPN_SHIFT)) ;
	}
	return hash & 0x7fffffffffUL;
}

#define HPTE_LOCAL_UPDATE	0x1
#define HPTE_NOHPTE_UPDATE	0x2

extern int __hash_page_4K(unsigned long ea, unsigned long access,
			  unsigned long vsid, pte_t *ptep, unsigned long trap,
			  unsigned long flags, int ssize, int subpage_prot);
extern int __hash_page_64K(unsigned long ea, unsigned long access,
			   unsigned long vsid, pte_t *ptep, unsigned long trap,
			   unsigned long flags, int ssize);
struct mm_struct;
unsigned int hash_page_do_lazy_icache(unsigned int pp, pte_t pte, int trap);
extern int hash_page_mm(struct mm_struct *mm, unsigned long ea,
			unsigned long access, unsigned long trap,
			unsigned long flags);
extern int hash_page(unsigned long ea, unsigned long access, unsigned long trap,
		     unsigned long dsisr);
int __hash_page_huge(unsigned long ea, unsigned long access, unsigned long vsid,
		     pte_t *ptep, unsigned long trap, unsigned long flags,
		     int ssize, unsigned int shift, unsigned int mmu_psize);
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
extern int __hash_page_thp(unsigned long ea, unsigned long access,
			   unsigned long vsid, pmd_t *pmdp, unsigned long trap,
			   unsigned long flags, int ssize, unsigned int psize);
#else
static inline int __hash_page_thp(unsigned long ea, unsigned long access,
				  unsigned long vsid, pmd_t *pmdp,
				  unsigned long trap, unsigned long flags,
				  int ssize, unsigned int psize)
{
	BUG();
	return -1;
}
#endif
extern void hash_failure_debug(unsigned long ea, unsigned long access,
			       unsigned long vsid, unsigned long trap,
			       int ssize, int psize, int lpsize,
			       unsigned long pte);
extern int htab_bolt_mapping(unsigned long vstart, unsigned long vend,
			     unsigned long pstart, unsigned long prot,
			     int psize, int ssize);
int htab_remove_mapping(unsigned long vstart, unsigned long vend,
			int psize, int ssize);
extern void add_gpage(u64 addr, u64 page_size, unsigned long number_of_pages);
extern void demote_segment_4k(struct mm_struct *mm, unsigned long addr);

extern void hpte_init_native(void);
extern void hpte_init_lpar(void);
extern void hpte_init_beat(void);
extern void hpte_init_beat_v3(void);

extern void slb_initialize(void);
extern void slb_flush_and_rebolt(void);

extern void slb_vmalloc_update(void);
extern void slb_set_size(u16 size);
#endif /* __ASSEMBLY__ */

/*
 * VSID allocation (256MB segment)
 *
 * We first generate a 37-bit "proto-VSID". Proto-VSIDs are generated
 * from mmu context id and effective segment id of the address.
 *
 * For user processes max context id is limited to ((1ul << 19) - 5)
 * for kernel space, we use the top 4 context ids to map address as below
 * NOTE: each context only support 64TB now.
 * 0x7fffc -  [ 0xc000000000000000 - 0xc0003fffffffffff ]
 * 0x7fffd -  [ 0xd000000000000000 - 0xd0003fffffffffff ]
 * 0x7fffe -  [ 0xe000000000000000 - 0xe0003fffffffffff ]
 * 0x7ffff -  [ 0xf000000000000000 - 0xf0003fffffffffff ]
 *
 * The proto-VSIDs are then scrambled into real VSIDs with the
 * multiplicative hash:
 *
 *	VSID = (proto-VSID * VSID_MULTIPLIER) % VSID_MODULUS
 *
 * VSID_MULTIPLIER is prime, so in particular it is
 * co-prime to VSID_MODULUS, making this a 1:1 scrambling function.
 * Because the modulus is 2^n-1 we can compute it efficiently without
 * a divide or extra multiply (see below). The scramble function gives
 * robust scattering in the hash table (at least based on some initial
 * results).
 *
 * We also consider VSID 0 special. We use VSID 0 for slb entries mapping
 * bad address. This enables us to consolidate bad address handling in
 * hash_page.
 *
 * We also need to avoid the last segment of the last context, because that
 * would give a protovsid of 0x1fffffffff. That will result in a VSID 0
 * because of the modulo operation in vsid scramble. But the vmemmap
 * (which is what uses region 0xf) will never be close to 64TB in size
 * (it's 56 bytes per page of system memory).
 */

#define CONTEXT_BITS		19
#define ESID_BITS		18
#define ESID_BITS_1T		6

/*
 * 256MB segment
 * The proto-VSID space has 2^(CONTEX_BITS + ESID_BITS) - 1 segments
 * available for user + kernel mapping. The top 4 contexts are used for
 * kernel mapping. Each segment contains 2^28 bytes. Each
 * context maps 2^46 bytes (64TB) so we can support 2^19-1 contexts
 * (19 == 37 + 28 - 46).
 */
#define MAX_USER_CONTEXT	((ASM_CONST(1) << CONTEXT_BITS) - 5)

/*
 * This should be computed such that protovosid * vsid_mulitplier
 * doesn't overflow 64 bits. It should also be co-prime to vsid_modulus
 */
#define VSID_MULTIPLIER_256M	ASM_CONST(12538073)	/* 24-bit prime */
#define VSID_BITS_256M		(CONTEXT_BITS + ESID_BITS)
#define VSID_MODULUS_256M	((1UL<<VSID_BITS_256M)-1)

#define VSID_MULTIPLIER_1T	ASM_CONST(12538073)	/* 24-bit prime */
#define VSID_BITS_1T		(CONTEXT_BITS + ESID_BITS_1T)
#define VSID_MODULUS_1T		((1UL<<VSID_BITS_1T)-1)


#define USER_VSID_RANGE	(1UL << (ESID_BITS + SID_SHIFT))

/*
 * This macro generates asm code to compute the VSID scramble
 * function.  Used in slb_allocate() and do_stab_bolted.  The function
 * computed is: (protovsid*VSID_MULTIPLIER) % VSID_MODULUS
 *
 *	rt = register continaing the proto-VSID and into which the
 *		VSID will be stored
 *	rx = scratch register (clobbered)
 *
 * 	- rt and rx must be different registers
 * 	- The answer will end up in the low VSID_BITS bits of rt.  The higher
 * 	  bits may contain other garbage, so you may need to mask the
 * 	  result.
 */
#define ASM_VSID_SCRAMBLE(rt, rx, size)					\
	lis	rx,VSID_MULTIPLIER_##size@h;				\
	ori	rx,rx,VSID_MULTIPLIER_##size@l;				\
	mulld	rt,rt,rx;		/* rt = rt * MULTIPLIER */	\
									\
	srdi	rx,rt,VSID_BITS_##size;					\
	clrldi	rt,rt,(64-VSID_BITS_##size);				\
	add	rt,rt,rx;		/* add high and low bits */	\
	/* NOTE: explanation based on VSID_BITS_##size = 36		\
	 * Now, r3 == VSID (mod 2^36-1), and lies between 0 and		\
	 * 2^36-1+2^28-1.  That in particular means that if r3 >=	\
	 * 2^36-1, then r3+1 has the 2^36 bit set.  So, if r3+1 has	\
	 * the bit clear, r3 already has the answer we want, if it	\
	 * doesn't, the answer is the low 36 bits of r3+1.  So in all	\
	 * cases the answer is the low 36 bits of (r3 + ((r3+1) >> 36))*/\
	addi	rx,rt,1;						\
	srdi	rx,rx,VSID_BITS_##size;	/* extract 2^VSID_BITS bit */	\
	add	rt,rt,rx

/* 4 bits per slice and we have one slice per 1TB */
#define SLICE_ARRAY_SIZE  (PGTABLE_RANGE >> 41)

#ifndef __ASSEMBLY__

#ifdef CONFIG_PPC_SUBPAGE_PROT
/*
 * For the sub-page protection option, we extend the PGD with one of
 * these.  Basically we have a 3-level tree, with the top level being
 * the protptrs array.  To optimize speed and memory consumption when
 * only addresses < 4GB are being protected, pointers to the first
 * four pages of sub-page protection words are stored in the low_prot
 * array.
 * Each page of sub-page protection words protects 1GB (4 bytes
 * protects 64k).  For the 3-level tree, each page of pointers then
 * protects 8TB.
 */
struct subpage_prot_table {
	unsigned long maxaddr;	/* only addresses < this are protected */
	unsigned int **protptrs[(TASK_SIZE_USER64 >> 43)];
	unsigned int *low_prot[4];
};

#define SBP_L1_BITS		(PAGE_SHIFT - 2)
#define SBP_L2_BITS		(PAGE_SHIFT - 3)
#define SBP_L1_COUNT		(1 << SBP_L1_BITS)
#define SBP_L2_COUNT		(1 << SBP_L2_BITS)
#define SBP_L2_SHIFT		(PAGE_SHIFT + SBP_L1_BITS)
#define SBP_L3_SHIFT		(SBP_L2_SHIFT + SBP_L2_BITS)

extern void subpage_prot_free(struct mm_struct *mm);
extern void subpage_prot_init_new_context(struct mm_struct *mm);
#else
static inline void subpage_prot_free(struct mm_struct *mm) {}
static inline void subpage_prot_init_new_context(struct mm_struct *mm) { }
#endif /* CONFIG_PPC_SUBPAGE_PROT */

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


#if 0
/*
 * The code below is equivalent to this function for arguments
 * < 2^VSID_BITS, which is all this should ever be called
 * with.  However gcc is not clever enough to compute the
 * modulus (2^n-1) without a second multiply.
 */
#define vsid_scramble(protovsid, size) \
	((((protovsid) * VSID_MULTIPLIER_##size) % VSID_MODULUS_##size))

#else /* 1 */
#define vsid_scramble(protovsid, size) \
	({								 \
		unsigned long x;					 \
		x = (protovsid) * VSID_MULTIPLIER_##size;		 \
		x = (x >> VSID_BITS_##size) + (x & VSID_MODULUS_##size); \
		(x + ((x+1) >> VSID_BITS_##size)) & VSID_MODULUS_##size; \
	})
#endif /* 1 */

/* Returns the segment size indicator for a user address */
static inline int user_segment_size(unsigned long addr)
{
	/* Use 1T segments if possible for addresses >= 1T */
	if (addr >= (1UL << SID_SHIFT_1T))
		return mmu_highuser_ssize;
	return MMU_SEGSIZE_256M;
}

static inline unsigned long get_vsid(unsigned long context, unsigned long ea,
				     int ssize)
{
	/*
	 * Bad address. We return VSID 0 for that
	 */
	if ((ea & ~REGION_MASK) >= PGTABLE_RANGE)
		return 0;

	if (ssize == MMU_SEGSIZE_256M)
		return vsid_scramble((context << ESID_BITS)
				     | (ea >> SID_SHIFT), 256M);
	return vsid_scramble((context << ESID_BITS_1T)
			     | (ea >> SID_SHIFT_1T), 1T);
}

/*
 * This is only valid for addresses >= PAGE_OFFSET
 *
 * For kernel space, we use the top 4 context ids to map address as below
 * 0x7fffc -  [ 0xc000000000000000 - 0xc0003fffffffffff ]
 * 0x7fffd -  [ 0xd000000000000000 - 0xd0003fffffffffff ]
 * 0x7fffe -  [ 0xe000000000000000 - 0xe0003fffffffffff ]
 * 0x7ffff -  [ 0xf000000000000000 - 0xf0003fffffffffff ]
 */
static inline unsigned long get_kernel_vsid(unsigned long ea, int ssize)
{
	unsigned long context;

	/*
	 * kernel take the top 4 context from the available range
	 */
	context = (MAX_USER_CONTEXT) + ((ea >> 60) - 0xc) + 1;
	return get_vsid(context, ea, ssize);
}
#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_MMU_HASH64_H_ */
