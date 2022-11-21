/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_BOOK3S_64_MMU_HASH_H_
#define _ASM_POWERPC_BOOK3S_64_MMU_HASH_H_
/*
 * PowerPC64 memory management structures
 *
 * Dave Engebretsen & Mike Corrigan <{engebret|mikejc}@us.ibm.com>
 *   PPC64 rework.
 */

#include <asm/page.h>
#include <asm/bug.h>
#include <asm/asm-const.h>

/*
 * This is necessary to get the definition of PGTABLE_RANGE which we
 * need for various slices related matters. Note that this isn't the
 * complete pgtable.h but only a portion of it.
 */
#include <asm/book3s/64/pgtable.h>
#include <asm/task_size_64.h>
#include <asm/cpu_has_feature.h>

/*
 * SLB
 */

#define SLB_NUM_BOLTED		2
#define SLB_CACHE_ENTRIES	8
#define SLB_MIN_SIZE		32

/* Bits in the SLB ESID word */
#define SLB_ESID_V		ASM_CONST(0x0000000008000000) /* valid */

/* Bits in the SLB VSID word */
#define SLB_VSID_SHIFT		12
#define SLB_VSID_SHIFT_256M	SLB_VSID_SHIFT
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
#define HPTE_V_COMMON_BITS	ASM_CONST(0x000fffffffffffff)
#define HPTE_V_AVPN		ASM_CONST(0x3fffffffffffff80)
#define HPTE_V_AVPN_3_0		ASM_CONST(0x000fffffffffff80)
#define HPTE_V_AVPN_VAL(x)	(((x) & HPTE_V_AVPN) >> HPTE_V_AVPN_SHIFT)
#define HPTE_V_COMPARE(x,y)	(!(((x) ^ (y)) & 0xffffffffffffff80UL))
#define HPTE_V_BOLTED		ASM_CONST(0x0000000000000010)
#define HPTE_V_LOCK		ASM_CONST(0x0000000000000008)
#define HPTE_V_LARGE		ASM_CONST(0x0000000000000004)
#define HPTE_V_SECONDARY	ASM_CONST(0x0000000000000002)
#define HPTE_V_VALID		ASM_CONST(0x0000000000000001)

/*
 * ISA 3.0 has a different HPTE format.
 */
#define HPTE_R_3_0_SSIZE_SHIFT	58
#define HPTE_R_3_0_SSIZE_MASK	(3ull << HPTE_R_3_0_SSIZE_SHIFT)
#define HPTE_R_PP0		ASM_CONST(0x8000000000000000)
#define HPTE_R_TS		ASM_CONST(0x4000000000000000)
#define HPTE_R_KEY_HI		ASM_CONST(0x3000000000000000)
#define HPTE_R_KEY_BIT4		ASM_CONST(0x2000000000000000)
#define HPTE_R_KEY_BIT3		ASM_CONST(0x1000000000000000)
#define HPTE_R_RPN_SHIFT	12
#define HPTE_R_RPN		ASM_CONST(0x0ffffffffffff000)
#define HPTE_R_RPN_3_0		ASM_CONST(0x01fffffffffff000)
#define HPTE_R_PP		ASM_CONST(0x0000000000000003)
#define HPTE_R_PPP		ASM_CONST(0x8000000000000003)
#define HPTE_R_N		ASM_CONST(0x0000000000000004)
#define HPTE_R_G		ASM_CONST(0x0000000000000008)
#define HPTE_R_M		ASM_CONST(0x0000000000000010)
#define HPTE_R_I		ASM_CONST(0x0000000000000020)
#define HPTE_R_W		ASM_CONST(0x0000000000000040)
#define HPTE_R_WIMG		ASM_CONST(0x0000000000000078)
#define HPTE_R_C		ASM_CONST(0x0000000000000080)
#define HPTE_R_R		ASM_CONST(0x0000000000000100)
#define HPTE_R_KEY_LO		ASM_CONST(0x0000000000000e00)
#define HPTE_R_KEY_BIT2		ASM_CONST(0x0000000000000800)
#define HPTE_R_KEY_BIT1		ASM_CONST(0x0000000000000400)
#define HPTE_R_KEY_BIT0		ASM_CONST(0x0000000000000200)
#define HPTE_R_KEY		(HPTE_R_KEY_LO | HPTE_R_KEY_HI)

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
#define POWER9_TLB_SETS_HASH	256	/* # sets in POWER9 TLB Hash mode */
#define POWER9_TLB_SETS_RADIX	128	/* # sets in POWER9 TLB Radix mode */

#ifndef __ASSEMBLY__

struct mmu_hash_ops {
	void            (*hpte_invalidate)(unsigned long slot,
					   unsigned long vpn,
					   int bpsize, int apsize,
					   int ssize, int local);
	long		(*hpte_updatepp)(unsigned long slot,
					 unsigned long newpp,
					 unsigned long vpn,
					 int bpsize, int apsize,
					 int ssize, unsigned long flags);
	void            (*hpte_updateboltedpp)(unsigned long newpp,
					       unsigned long ea,
					       int psize, int ssize);
	long		(*hpte_insert)(unsigned long hpte_group,
				       unsigned long vpn,
				       unsigned long prpn,
				       unsigned long rflags,
				       unsigned long vflags,
				       int psize, int apsize,
				       int ssize);
	long		(*hpte_remove)(unsigned long hpte_group);
	int             (*hpte_removebolted)(unsigned long ea,
					     int psize, int ssize);
	void		(*flush_hash_range)(unsigned long number, int local);
	void		(*hugepage_invalidate)(unsigned long vsid,
					       unsigned long addr,
					       unsigned char *hpte_slot_array,
					       int psize, int ssize, int local);
	int		(*resize_hpt)(unsigned long shift);
	/*
	 * Special for kexec.
	 * To be called in real mode with interrupts disabled. No locks are
	 * taken as such, concurrent access on pre POWER5 hardware could result
	 * in a deadlock.
	 * The linear mapping is destroyed as well.
	 */
	void		(*hpte_clear_all)(void);
};
extern struct mmu_hash_ops mmu_hash_ops;

struct hash_pte {
	__be64 v;
	__be64 r;
};

extern struct hash_pte *htab_address;
extern unsigned long htab_size_bytes;
extern unsigned long htab_hash_mask;


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

static inline unsigned int ap_to_shift(unsigned long ap)
{
	int psize;

	for (psize = 0; psize < MMU_PAGE_COUNT; psize++) {
		if (mmu_psize_defs[psize].ap == ap)
			return mmu_psize_defs[psize].shift;
	}

	return -1;
}

static inline unsigned long get_sllp_encoding(int psize)
{
	unsigned long sllp;

	sllp = ((mmu_psize_defs[psize].sllp & SLB_VSID_L) >> 6) |
		((mmu_psize_defs[psize].sllp & SLB_VSID_LP) >> 4);
	return sllp;
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
 * This array is indexed by the LP field of the HPTE second dword.
 * Since this field may contain some RPN bits, some entries are
 * replicated so that we get the same value irrespective of RPN.
 * The top 4 bits are the page size index (MMU_PAGE_*) for the
 * actual page size, the bottom 4 bits are the base page size.
 */
extern u8 hpte_page_sizes[1 << LP_BITS];

static inline unsigned long __hpte_page_size(unsigned long h, unsigned long l,
					     bool is_base_size)
{
	unsigned int i, lp;

	if (!(h & HPTE_V_LARGE))
		return 1ul << 12;

	/* Look at the 8 bit LP value */
	lp = (l >> LP_SHIFT) & ((1 << LP_BITS) - 1);
	i = hpte_page_sizes[lp];
	if (!i)
		return 0;
	if (!is_base_size)
		i >>= 4;
	return 1ul << mmu_psize_defs[i & 0xf].shift;
}

static inline unsigned long hpte_page_size(unsigned long h, unsigned long l)
{
	return __hpte_page_size(h, l, 0);
}

static inline unsigned long hpte_base_page_size(unsigned long h, unsigned long l)
{
	return __hpte_page_size(h, l, 1);
}

/*
 * The current system page and segment sizes
 */
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
 * ISA v3.0 defines a new HPTE format, which differs from the old
 * format in having smaller AVPN and ARPN fields, and the B field
 * in the second dword instead of the first.
 */
static inline unsigned long hpte_old_to_new_v(unsigned long v)
{
	/* trim AVPN, drop B */
	return v & HPTE_V_COMMON_BITS;
}

static inline unsigned long hpte_old_to_new_r(unsigned long v, unsigned long r)
{
	/* move B field from 1st to 2nd dword, trim ARPN */
	return (r & ~HPTE_R_3_0_SSIZE_MASK) |
		(((v) >> HPTE_V_SSIZE_SHIFT) << HPTE_R_3_0_SSIZE_SHIFT);
}

static inline unsigned long hpte_new_to_old_v(unsigned long v, unsigned long r)
{
	/* insert B field */
	return (v & HPTE_V_COMMON_BITS) |
		((r & HPTE_R_3_0_SSIZE_MASK) <<
		 (HPTE_V_SSIZE_SHIFT - HPTE_R_3_0_SSIZE_SHIFT));
}

static inline unsigned long hpte_new_to_old_r(unsigned long r)
{
	/* clear out B field */
	return r & ~HPTE_R_3_0_SSIZE_MASK;
}

static inline unsigned long hpte_get_old_v(struct hash_pte *hptep)
{
	unsigned long hpte_v;

	hpte_v = be64_to_cpu(hptep->v);
	if (cpu_has_feature(CPU_FTR_ARCH_300))
		hpte_v = hpte_new_to_old_v(hpte_v, be64_to_cpu(hptep->r));
	return hpte_v;
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
	unsigned long mask;
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
#define HPTE_USE_KERNEL_KEY	0x4

long hpte_insert_repeating(unsigned long hash, unsigned long vpn, unsigned long pa,
			   unsigned long rlags, unsigned long vflags, int psize, int ssize);
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
void low_hash_fault(struct pt_regs *regs, unsigned long address, int rc);
int __hash_page(unsigned long trap, unsigned long ea, unsigned long dsisr, unsigned long msr);
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
extern void pseries_add_gpage(u64 addr, u64 page_size, unsigned long number_of_pages);
extern void demote_segment_4k(struct mm_struct *mm, unsigned long addr);

extern void hash__setup_new_exec(void);

#ifdef CONFIG_PPC_PSERIES
void hpte_init_pseries(void);
#else
static inline void hpte_init_pseries(void) { }
#endif

extern void hpte_init_native(void);

struct slb_entry {
	u64	esid;
	u64	vsid;
};

extern void slb_initialize(void);
void slb_flush_and_restore_bolted(void);
void slb_flush_all_realmode(void);
void __slb_restore_bolted_realmode(void);
void slb_restore_bolted_realmode(void);
void slb_save_contents(struct slb_entry *slb_ptr);
void slb_dump_contents(struct slb_entry *slb_ptr);

extern void slb_vmalloc_update(void);
void preload_new_slb_context(unsigned long start, unsigned long sp);

#ifdef CONFIG_PPC_64S_HASH_MMU
void slb_set_size(u16 size);
#else
static inline void slb_set_size(u16 size) { }
#endif

#endif /* __ASSEMBLY__ */

/*
 * VSID allocation (256MB segment)
 *
 * We first generate a 37-bit "proto-VSID". Proto-VSIDs are generated
 * from mmu context id and effective segment id of the address.
 *
 * For user processes max context id is limited to MAX_USER_CONTEXT.
 * more details in get_user_context
 *
 * For kernel space get_kernel_context
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
 * We use VSID 0 to indicate an invalid VSID. The means we can't use context id
 * 0, because a context id of 0 and an EA of 0 gives a proto-VSID of 0, which
 * will produce a VSID of 0.
 *
 * We also need to avoid the last segment of the last context, because that
 * would give a protovsid of 0x1fffffffff. That will result in a VSID 0
 * because of the modulo operation in vsid scramble.
 */

/*
 * Max Va bits we support as of now is 68 bits. We want 19 bit
 * context ID.
 * Restrictions:
 * GPU has restrictions of not able to access beyond 128TB
 * (47 bit effective address). We also cannot do more than 20bit PID.
 * For p4 and p5 which can only do 65 bit VA, we restrict our CONTEXT_BITS
 * to 16 bits (ie, we can only have 2^16 pids at the same time).
 */
#define VA_BITS			68
#define CONTEXT_BITS		19
#define ESID_BITS		(VA_BITS - (SID_SHIFT + CONTEXT_BITS))
#define ESID_BITS_1T		(VA_BITS - (SID_SHIFT_1T + CONTEXT_BITS))

#define ESID_BITS_MASK		((1 << ESID_BITS) - 1)
#define ESID_BITS_1T_MASK	((1 << ESID_BITS_1T) - 1)

/*
 * Now certain config support MAX_PHYSMEM more than 512TB. Hence we will need
 * to use more than one context for linear mapping the kernel.
 * For vmalloc and memmap, we use just one context with 512TB. With 64 byte
 * struct page size, we need ony 32 TB in memmap for 2PB (51 bits (MAX_PHYSMEM_BITS)).
 */
#if (H_MAX_PHYSMEM_BITS > MAX_EA_BITS_PER_CONTEXT)
#define MAX_KERNEL_CTX_CNT	(1UL << (H_MAX_PHYSMEM_BITS - MAX_EA_BITS_PER_CONTEXT))
#else
#define MAX_KERNEL_CTX_CNT	1
#endif

#define MAX_VMALLOC_CTX_CNT	1
#define MAX_IO_CTX_CNT		1
#define MAX_VMEMMAP_CTX_CNT	1

/*
 * 256MB segment
 * The proto-VSID space has 2^(CONTEX_BITS + ESID_BITS) - 1 segments
 * available for user + kernel mapping. VSID 0 is reserved as invalid, contexts
 * 1-4 are used for kernel mapping. Each segment contains 2^28 bytes. Each
 * context maps 2^49 bytes (512TB).
 *
 * We also need to avoid the last segment of the last context, because that
 * would give a protovsid of 0x1fffffffff. That will result in a VSID 0
 * because of the modulo operation in vsid scramble.
 *
 */
#define MAX_USER_CONTEXT	((ASM_CONST(1) << CONTEXT_BITS) - 2)

// The + 2 accounts for INVALID_REGION and 1 more to avoid overlap with kernel
#define MIN_USER_CONTEXT	(MAX_KERNEL_CTX_CNT + MAX_VMALLOC_CTX_CNT + \
				 MAX_IO_CTX_CNT + MAX_VMEMMAP_CTX_CNT + 2)

/*
 * For platforms that support on 65bit VA we limit the context bits
 */
#define MAX_USER_CONTEXT_65BIT_VA ((ASM_CONST(1) << (65 - (SID_SHIFT + ESID_BITS))) - 2)

/*
 * This should be computed such that protovosid * vsid_mulitplier
 * doesn't overflow 64 bits. The vsid_mutliplier should also be
 * co-prime to vsid_modulus. We also need to make sure that number
 * of bits in multiplied result (dividend) is less than twice the number of
 * protovsid bits for our modulus optmization to work.
 *
 * The below table shows the current values used.
 * |-------+------------+----------------------+------------+-------------------|
 * |       | Prime Bits | proto VSID_BITS_65VA | Total Bits | 2* prot VSID_BITS |
 * |-------+------------+----------------------+------------+-------------------|
 * | 1T    |         24 |                   25 |         49 |                50 |
 * |-------+------------+----------------------+------------+-------------------|
 * | 256MB |         24 |                   37 |         61 |                74 |
 * |-------+------------+----------------------+------------+-------------------|
 *
 * |-------+------------+----------------------+------------+--------------------|
 * |       | Prime Bits | proto VSID_BITS_68VA | Total Bits | 2* proto VSID_BITS |
 * |-------+------------+----------------------+------------+--------------------|
 * | 1T    |         24 |                   28 |         52 |                 56 |
 * |-------+------------+----------------------+------------+--------------------|
 * | 256MB |         24 |                   40 |         64 |                 80 |
 * |-------+------------+----------------------+------------+--------------------|
 *
 */
#define VSID_MULTIPLIER_256M	ASM_CONST(12538073)	/* 24-bit prime */
#define VSID_BITS_256M		(VA_BITS - SID_SHIFT)
#define VSID_BITS_65_256M	(65 - SID_SHIFT)
/*
 * Modular multiplicative inverse of VSID_MULTIPLIER under modulo VSID_MODULUS
 */
#define VSID_MULINV_256M	ASM_CONST(665548017062)

#define VSID_MULTIPLIER_1T	ASM_CONST(12538073)	/* 24-bit prime */
#define VSID_BITS_1T		(VA_BITS - SID_SHIFT_1T)
#define VSID_BITS_65_1T		(65 - SID_SHIFT_1T)
#define VSID_MULINV_1T		ASM_CONST(209034062)

/* 1TB VSID reserved for VRMA */
#define VRMA_VSID	0x1ffffffUL
#define USER_VSID_RANGE	(1UL << (ESID_BITS + SID_SHIFT))

/* 4 bits per slice and we have one slice per 1TB */
#define SLICE_ARRAY_SIZE	(H_PGTABLE_RANGE >> 41)
#define LOW_SLICE_ARRAY_SZ	(BITS_PER_LONG / BITS_PER_BYTE)
#define TASK_SLICE_ARRAY_SZ(x)	((x)->hash_context->slb_addr_limit >> 41)
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
#else
static inline void subpage_prot_free(struct mm_struct *mm) {}
#endif /* CONFIG_PPC_SUBPAGE_PROT */

/*
 * One bit per slice. We have lower slices which cover 256MB segments
 * upto 4G range. That gets us 16 low slices. For the rest we track slices
 * in 1TB size.
 */
struct slice_mask {
	u64 low_slices;
	DECLARE_BITMAP(high_slices, SLICE_NUM_HIGH);
};

struct hash_mm_context {
	u16 user_psize; /* page size index */

	/* SLB page size encodings*/
	unsigned char low_slices_psize[LOW_SLICE_ARRAY_SZ];
	unsigned char high_slices_psize[SLICE_ARRAY_SIZE];
	unsigned long slb_addr_limit;
#ifdef CONFIG_PPC_64K_PAGES
	struct slice_mask mask_64k;
#endif
	struct slice_mask mask_4k;
#ifdef CONFIG_HUGETLB_PAGE
	struct slice_mask mask_16m;
	struct slice_mask mask_16g;
#endif

#ifdef CONFIG_PPC_SUBPAGE_PROT
	struct subpage_prot_table *spt;
#endif /* CONFIG_PPC_SUBPAGE_PROT */
};

#if 0
/*
 * The code below is equivalent to this function for arguments
 * < 2^VSID_BITS, which is all this should ever be called
 * with.  However gcc is not clever enough to compute the
 * modulus (2^n-1) without a second multiply.
 */
#define vsid_scramble(protovsid, size) \
	((((protovsid) * VSID_MULTIPLIER_##size) % VSID_MODULUS_##size))

/* simplified form avoiding mod operation */
#define vsid_scramble(protovsid, size) \
	({								 \
		unsigned long x;					 \
		x = (protovsid) * VSID_MULTIPLIER_##size;		 \
		x = (x >> VSID_BITS_##size) + (x & VSID_MODULUS_##size); \
		(x + ((x+1) >> VSID_BITS_##size)) & VSID_MODULUS_##size; \
	})

#else /* 1 */
static inline unsigned long vsid_scramble(unsigned long protovsid,
				  unsigned long vsid_multiplier, int vsid_bits)
{
	unsigned long vsid;
	unsigned long vsid_modulus = ((1UL << vsid_bits) - 1);
	/*
	 * We have same multipler for both 256 and 1T segements now
	 */
	vsid = protovsid * vsid_multiplier;
	vsid = (vsid >> vsid_bits) + (vsid & vsid_modulus);
	return (vsid + ((vsid + 1) >> vsid_bits)) & vsid_modulus;
}

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
	unsigned long va_bits = VA_BITS;
	unsigned long vsid_bits;
	unsigned long protovsid;

	/*
	 * Bad address. We return VSID 0 for that
	 */
	if ((ea & EA_MASK)  >= H_PGTABLE_RANGE)
		return 0;

	if (!mmu_has_feature(MMU_FTR_68_BIT_VA))
		va_bits = 65;

	if (ssize == MMU_SEGSIZE_256M) {
		vsid_bits = va_bits - SID_SHIFT;
		protovsid = (context << ESID_BITS) |
			((ea >> SID_SHIFT) & ESID_BITS_MASK);
		return vsid_scramble(protovsid, VSID_MULTIPLIER_256M, vsid_bits);
	}
	/* 1T segment */
	vsid_bits = va_bits - SID_SHIFT_1T;
	protovsid = (context << ESID_BITS_1T) |
		((ea >> SID_SHIFT_1T) & ESID_BITS_1T_MASK);
	return vsid_scramble(protovsid, VSID_MULTIPLIER_1T, vsid_bits);
}

/*
 * For kernel space, we use context ids as
 * below. Range is 512TB per context.
 *
 * 0x00001 -  [ 0xc000000000000000 - 0xc001ffffffffffff]
 * 0x00002 -  [ 0xc002000000000000 - 0xc003ffffffffffff]
 * 0x00003 -  [ 0xc004000000000000 - 0xc005ffffffffffff]
 * 0x00004 -  [ 0xc006000000000000 - 0xc007ffffffffffff]
 *
 * vmap, IO, vmemap
 *
 * 0x00005 -  [ 0xc008000000000000 - 0xc009ffffffffffff]
 * 0x00006 -  [ 0xc00a000000000000 - 0xc00bffffffffffff]
 * 0x00007 -  [ 0xc00c000000000000 - 0xc00dffffffffffff]
 *
 */
static inline unsigned long get_kernel_context(unsigned long ea)
{
	unsigned long region_id = get_region_id(ea);
	unsigned long ctx;
	/*
	 * Depending on Kernel config, kernel region can have one context
	 * or more.
	 */
	if (region_id == LINEAR_MAP_REGION_ID) {
		/*
		 * We already verified ea to be not beyond the addr limit.
		 */
		ctx =  1 + ((ea & EA_MASK) >> MAX_EA_BITS_PER_CONTEXT);
	} else
		ctx = region_id + MAX_KERNEL_CTX_CNT - 1;
	return ctx;
}

/*
 * This is only valid for addresses >= PAGE_OFFSET
 */
static inline unsigned long get_kernel_vsid(unsigned long ea, int ssize)
{
	unsigned long context;

	if (!is_kernel_addr(ea))
		return 0;

	context = get_kernel_context(ea);
	return get_vsid(context, ea, ssize);
}

unsigned htab_shift_for_mem_size(unsigned long mem_size);

enum slb_index {
	LINEAR_INDEX	= 0, /* Kernel linear map  (0xc000000000000000) */
	KSTACK_INDEX	= 1, /* Kernel stack map */
};

#define slb_esid_mask(ssize)	\
	(((ssize) == MMU_SEGSIZE_256M) ? ESID_MASK : ESID_MASK_1T)

static inline unsigned long mk_esid_data(unsigned long ea, int ssize,
					 enum slb_index index)
{
	return (ea & slb_esid_mask(ssize)) | SLB_ESID_V | index;
}

static inline unsigned long __mk_vsid_data(unsigned long vsid, int ssize,
					   unsigned long flags)
{
	return (vsid << slb_vsid_shift(ssize)) | flags |
		((unsigned long)ssize << SLB_VSID_SSIZE_SHIFT);
}

static inline unsigned long mk_vsid_data(unsigned long ea, int ssize,
					 unsigned long flags)
{
	return __mk_vsid_data(get_kernel_vsid(ea, ssize), ssize, flags);
}

#endif /* __ASSEMBLY__ */
#endif /* _ASM_POWERPC_BOOK3S_64_MMU_HASH_H_ */
