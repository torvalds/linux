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

/*
 * Segment table
 */

#define STE_ESID_V	0x80
#define STE_ESID_KS	0x20
#define STE_ESID_KP	0x10
#define STE_ESID_N	0x08

#define STE_VSID_SHIFT	12

/* Location of cpu0's segment table */
#define STAB0_PAGE	0x8
#define STAB0_OFFSET	(STAB0_PAGE << 12)
#define STAB0_PHYS_ADDR	(STAB0_OFFSET + PHYSICAL_START)

#ifndef __ASSEMBLY__
extern char initial_stab[];
#endif /* ! __ASSEMBLY */

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
/* pp0 will always be 0 for linux     */
#define PP_RWXX	0	/* Supervisor read/write, User none */
#define PP_RWRX 1	/* Supervisor read/write, User read */
#define PP_RWRW 2	/* Supervisor read/write, User read/write */
#define PP_RXRX 3	/* Supervisor read,       User read */

#ifndef __ASSEMBLY__

struct hash_pte {
	unsigned long v;
	unsigned long r;
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
	unsigned int	penc;	/* HPTE encoding */
	unsigned int	tlbiel;	/* tlbiel supported for that page size */
	unsigned long	avpnm;	/* bits to mask out in AVPN in the HPTE */
	unsigned long	sllp;	/* SLB L||LP (exact mask to use in slbmte) */
};

#endif /* __ASSEMBLY__ */

/*
 * Segment sizes.
 * These are the values used by hardware in the B field of
 * SLB entries and the first dword of MMU hashtable entries.
 * The B field is 2 bits; the values 2 and 3 are unused and reserved.
 */
#define MMU_SEGSIZE_256M	0
#define MMU_SEGSIZE_1T		1


#ifndef __ASSEMBLY__

/*
 * The current system page and segment sizes
 */
extern struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT];
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
 * This function sets the AVPN and L fields of the HPTE  appropriately
 * for the page size
 */
static inline unsigned long hpte_encode_v(unsigned long va, int psize,
					  int ssize)
{
	unsigned long v;
	v = (va >> 23) & ~(mmu_psize_defs[psize].avpnm);
	v <<= HPTE_V_AVPN_SHIFT;
	if (psize != MMU_PAGE_4K)
		v |= HPTE_V_LARGE;
	v |= ((unsigned long) ssize) << HPTE_V_SSIZE_SHIFT;
	return v;
}

/*
 * This function sets the ARPN, and LP fields of the HPTE appropriately
 * for the page size. We assume the pa is already "clean" that is properly
 * aligned for the requested page size
 */
static inline unsigned long hpte_encode_r(unsigned long pa, int psize)
{
	unsigned long r;

	/* A 4K page needs no special encoding */
	if (psize == MMU_PAGE_4K)
		return pa & HPTE_R_RPN;
	else {
		unsigned int penc = mmu_psize_defs[psize].penc;
		unsigned int shift = mmu_psize_defs[psize].shift;
		return (pa & ~((1ul << shift) - 1)) | (penc << 12);
	}
	return r;
}

/*
 * Build a VA given VSID, EA and segment size
 */
static inline unsigned long hpt_va(unsigned long ea, unsigned long vsid,
				   int ssize)
{
	if (ssize == MMU_SEGSIZE_256M)
		return (vsid << 28) | (ea & 0xfffffffUL);
	return (vsid << 40) | (ea & 0xffffffffffUL);
}

/*
 * This hashes a virtual address
 */

static inline unsigned long hpt_hash(unsigned long va, unsigned int shift,
				     int ssize)
{
	unsigned long hash, vsid;

	if (ssize == MMU_SEGSIZE_256M) {
		hash = (va >> 28) ^ ((va & 0x0fffffffUL) >> shift);
	} else {
		vsid = va >> 40;
		hash = vsid ^ (vsid << 25) ^ ((va & 0xffffffffffUL) >> shift);
	}
	return hash & 0x7fffffffffUL;
}

extern int __hash_page_4K(unsigned long ea, unsigned long access,
			  unsigned long vsid, pte_t *ptep, unsigned long trap,
			  unsigned int local, int ssize, int subpage_prot);
extern int __hash_page_64K(unsigned long ea, unsigned long access,
			   unsigned long vsid, pte_t *ptep, unsigned long trap,
			   unsigned int local, int ssize);
struct mm_struct;
unsigned int hash_page_do_lazy_icache(unsigned int pp, pte_t pte, int trap);
extern int hash_page(unsigned long ea, unsigned long access, unsigned long trap);
int __hash_page_huge(unsigned long ea, unsigned long access, unsigned long vsid,
		     pte_t *ptep, unsigned long trap, int local, int ssize,
		     unsigned int shift, unsigned int mmu_psize);
extern void hash_failure_debug(unsigned long ea, unsigned long access,
			       unsigned long vsid, unsigned long trap,
			       int ssize, int psize, unsigned long pte);
extern int htab_bolt_mapping(unsigned long vstart, unsigned long vend,
			     unsigned long pstart, unsigned long prot,
			     int psize, int ssize);
extern void add_gpage(unsigned long addr, unsigned long page_size,
			  unsigned long number_of_pages);
extern void demote_segment_4k(struct mm_struct *mm, unsigned long addr);

extern void hpte_init_native(void);
extern void hpte_init_lpar(void);
extern void hpte_init_iSeries(void);
extern void hpte_init_beat(void);
extern void hpte_init_beat_v3(void);

extern void stabs_alloc(void);
extern void slb_initialize(void);
extern void slb_flush_and_rebolt(void);
extern void stab_initialize(unsigned long stab);

extern void slb_vmalloc_update(void);
extern void slb_set_size(u16 size);
#endif /* __ASSEMBLY__ */

/*
 * VSID allocation
 *
 * We first generate a 36-bit "proto-VSID".  For kernel addresses this
 * is equal to the ESID, for user addresses it is:
 *	(context << 15) | (esid & 0x7fff)
 *
 * The two forms are distinguishable because the top bit is 0 for user
 * addresses, whereas the top two bits are 1 for kernel addresses.
 * Proto-VSIDs with the top two bits equal to 0b10 are reserved for
 * now.
 *
 * The proto-VSIDs are then scrambled into real VSIDs with the
 * multiplicative hash:
 *
 *	VSID = (proto-VSID * VSID_MULTIPLIER) % VSID_MODULUS
 *	where	VSID_MULTIPLIER = 268435399 = 0xFFFFFC7
 *		VSID_MODULUS = 2^36-1 = 0xFFFFFFFFF
 *
 * This scramble is only well defined for proto-VSIDs below
 * 0xFFFFFFFFF, so both proto-VSID and actual VSID 0xFFFFFFFFF are
 * reserved.  VSID_MULTIPLIER is prime, so in particular it is
 * co-prime to VSID_MODULUS, making this a 1:1 scrambling function.
 * Because the modulus is 2^n-1 we can compute it efficiently without
 * a divide or extra multiply (see below).
 *
 * This scheme has several advantages over older methods:
 *
 * 	- We have VSIDs allocated for every kernel address
 * (i.e. everything above 0xC000000000000000), except the very top
 * segment, which simplifies several things.
 *
 * 	- We allow for 15 significant bits of ESID and 20 bits of
 * context for user addresses.  i.e. 8T (43 bits) of address space for
 * up to 1M contexts (although the page table structure and context
 * allocation will need changes to take advantage of this).
 *
 * 	- The scramble function gives robust scattering in the hash
 * table (at least based on some initial results).  The previous
 * method was more susceptible to pathological cases giving excessive
 * hash collisions.
 */
/*
 * WARNING - If you change these you must make sure the asm
 * implementations in slb_allocate (slb_low.S), do_stab_bolted
 * (head.S) and ASM_VSID_SCRAMBLE (below) are changed accordingly.
 *
 * You'll also need to change the precomputed VSID values in head.S
 * which are used by the iSeries firmware.
 */

#define VSID_MULTIPLIER_256M	ASM_CONST(200730139)	/* 28-bit prime */
#define VSID_BITS_256M		36
#define VSID_MODULUS_256M	((1UL<<VSID_BITS_256M)-1)

#define VSID_MULTIPLIER_1T	ASM_CONST(12538073)	/* 24-bit prime */
#define VSID_BITS_1T		24
#define VSID_MODULUS_1T		((1UL<<VSID_BITS_1T)-1)

#define CONTEXT_BITS		19
#define USER_ESID_BITS		16
#define USER_ESID_BITS_1T	4

#define USER_VSID_RANGE	(1UL << (USER_ESID_BITS + SID_SHIFT))

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
	/* Now, r3 == VSID (mod 2^36-1), and lies between 0 and		\
	 * 2^36-1+2^28-1.  That in particular means that if r3 >=	\
	 * 2^36-1, then r3+1 has the 2^36 bit set.  So, if r3+1 has	\
	 * the bit clear, r3 already has the answer we want, if it	\
	 * doesn't, the answer is the low 36 bits of r3+1.  So in all	\
	 * cases the answer is the low 36 bits of (r3 + ((r3+1) >> 36))*/\
	addi	rx,rt,1;						\
	srdi	rx,rx,VSID_BITS_##size;	/* extract 2^VSID_BITS bit */	\
	add	rt,rt,rx


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
	unsigned int **protptrs[2];
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
	u64 high_slices_psize;  /* 4 bits per slice for now */
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

/* This is only valid for addresses >= PAGE_OFFSET */
static inline unsigned long get_kernel_vsid(unsigned long ea, int ssize)
{
	if (ssize == MMU_SEGSIZE_256M)
		return vsid_scramble(ea >> SID_SHIFT, 256M);
	return vsid_scramble(ea >> SID_SHIFT_1T, 1T);
}

/* Returns the segment size indicator for a user address */
static inline int user_segment_size(unsigned long addr)
{
	/* Use 1T segments if possible for addresses >= 1T */
	if (addr >= (1UL << SID_SHIFT_1T))
		return mmu_highuser_ssize;
	return MMU_SEGSIZE_256M;
}

/* This is only valid for user addresses (which are below 2^44) */
static inline unsigned long get_vsid(unsigned long context, unsigned long ea,
				     int ssize)
{
	if (ssize == MMU_SEGSIZE_256M)
		return vsid_scramble((context << USER_ESID_BITS)
				     | (ea >> SID_SHIFT), 256M);
	return vsid_scramble((context << USER_ESID_BITS_1T)
			     | (ea >> SID_SHIFT_1T), 1T);
}

/*
 * This is only used on legacy iSeries in lparmap.c,
 * hence the 256MB segment assumption.
 */
#define VSID_SCRAMBLE(pvsid)	(((pvsid) * VSID_MULTIPLIER_256M) %	\
				 VSID_MODULUS_256M)
#define KERNEL_VSID(ea)		VSID_SCRAMBLE(GET_ESID(ea))

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_MMU_HASH64_H_ */
