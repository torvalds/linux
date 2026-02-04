/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  KVM guest address space mapping code
 *
 *    Copyright IBM Corp. 2024, 2025
 *    Author(s): Claudio Imbrenda <imbrenda@linux.ibm.com>
 */

#ifndef __KVM_S390_DAT_H
#define __KVM_S390_DAT_H

#include <linux/radix-tree.h>
#include <linux/refcount.h>
#include <linux/io.h>
#include <linux/kvm_types.h>
#include <linux/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/dat-bits.h>

/*
 * Base address and length must be sent at the start of each block, therefore
 * it's cheaper to send some clean data, as long as it's less than the size of
 * two longs.
 */
#define KVM_S390_MAX_BIT_DISTANCE (2 * sizeof(void *))
/* For consistency */
#define KVM_S390_CMMA_SIZE_MAX ((u32)KVM_S390_SKEYS_MAX)

#define _ASCE(x) ((union asce) { .val = (x), })
#define NULL_ASCE _ASCE(0)

enum {
	_DAT_TOKEN_NONE = 0,
	_DAT_TOKEN_PIC,
};

#define _CRSTE_TOK(l, t, p) ((union crste) {	\
		.tok.i = 1,			\
		.tok.tt = (l),			\
		.tok.type = (t),		\
		.tok.par = (p)			\
	})
#define _CRSTE_PIC(l, p) _CRSTE_TOK(l, _DAT_TOKEN_PIC, p)

#define _CRSTE_HOLE(l) _CRSTE_PIC(l, PGM_ADDRESSING)
#define _CRSTE_EMPTY(l) _CRSTE_TOK(l, _DAT_TOKEN_NONE, 0)

#define _PMD_EMPTY _CRSTE_EMPTY(TABLE_TYPE_SEGMENT)

#define _PTE_TOK(t, p) ((union pte) { .tok.i = 1, .tok.type = (t), .tok.par = (p) })
#define _PTE_EMPTY _PTE_TOK(_DAT_TOKEN_NONE, 0)

/* This fake table type is used for page table walks (both for normal page tables and vSIE) */
#define TABLE_TYPE_PAGE_TABLE -1

enum dat_walk_flags {
	DAT_WALK_USES_SKEYS	= 0x40,
	DAT_WALK_CONTINUE	= 0x20,
	DAT_WALK_IGN_HOLES	= 0x10,
	DAT_WALK_SPLIT		= 0x08,
	DAT_WALK_ALLOC		= 0x04,
	DAT_WALK_ANY		= 0x02,
	DAT_WALK_LEAF		= 0x01,
	DAT_WALK_DEFAULT	= 0
};

#define DAT_WALK_SPLIT_ALLOC (DAT_WALK_SPLIT | DAT_WALK_ALLOC)
#define DAT_WALK_ALLOC_CONTINUE (DAT_WALK_CONTINUE | DAT_WALK_ALLOC)
#define DAT_WALK_LEAF_ALLOC (DAT_WALK_LEAF | DAT_WALK_ALLOC)

union pte {
	unsigned long val;
	union page_table_entry h;
	struct {
		unsigned long   :56; /* Hardware bits */
		unsigned long u : 1; /* Page unused */
		unsigned long s : 1; /* Special */
		unsigned long w : 1; /* Writable */
		unsigned long r : 1; /* Readable */
		unsigned long d : 1; /* Dirty */
		unsigned long y : 1; /* Young */
		unsigned long sd: 1; /* Soft dirty */
		unsigned long pr: 1; /* Present */
	} s;
	struct {
		unsigned char hwbytes[7];
		unsigned char swbyte;
	};
	union {
		struct {
			unsigned long type :16; /* Token type */
			unsigned long par  :16; /* Token parameter */
			unsigned long      :20;
			unsigned long      : 1; /* Must be 0 */
			unsigned long i    : 1; /* Must be 1 */
			unsigned long      : 2;
			unsigned long      : 7;
			unsigned long pr   : 1; /* Must be 0 */
		};
		struct {
			unsigned long token:32; /* Token and parameter */
			unsigned long      :32;
		};
	} tok;
};

/* Soft dirty, needed as macro for atomic operations on ptes */
#define _PAGE_SD 0x002

/* Needed as macro to perform atomic operations */
#define PGSTE_PCL_BIT		0x0080000000000000UL	/* PCL lock, HW bit */
#define PGSTE_CMMA_D_BIT	0x0000000000008000UL	/* CMMA dirty soft-bit */

enum pgste_gps_usage {
	PGSTE_GPS_USAGE_STABLE = 0,
	PGSTE_GPS_USAGE_UNUSED,
	PGSTE_GPS_USAGE_POT_VOLATILE,
	PGSTE_GPS_USAGE_VOLATILE,
};

union pgste {
	unsigned long val;
	struct {
		unsigned long acc          : 4;
		unsigned long fp           : 1;
		unsigned long              : 3;
		unsigned long pcl          : 1;
		unsigned long hr           : 1;
		unsigned long hc           : 1;
		unsigned long              : 2;
		unsigned long gr           : 1;
		unsigned long gc           : 1;
		unsigned long              : 1;
		unsigned long              :16; /* val16 */
		unsigned long zero         : 1;
		unsigned long nodat        : 1;
		unsigned long              : 4;
		unsigned long usage        : 2;
		unsigned long              : 8;
		unsigned long cmma_d       : 1; /* Dirty flag for CMMA bits */
		unsigned long prefix_notif : 1; /* Guest prefix invalidation notification */
		unsigned long vsie_notif   : 1; /* Referenced in a shadow table */
		unsigned long              : 5;
		unsigned long              : 8;
	};
	struct {
		unsigned short hwbytes0;
		unsigned short val16;	/* Used to store chunked values, see dat_{s,g}et_ptval() */
		unsigned short hwbytes4;
		unsigned char flags;	/* Maps to the software bits */
		unsigned char hwbyte7;
	} __packed;
};

union pmd {
	unsigned long val;
	union segment_table_entry h;
	struct {
		struct {
			unsigned long              :44; /* HW */
			unsigned long              : 3; /* Unused */
			unsigned long              : 1; /* HW */
			unsigned long w            : 1; /* Writable soft-bit */
			unsigned long r            : 1; /* Readable soft-bit */
			unsigned long d            : 1; /* Dirty */
			unsigned long y            : 1; /* Young */
			unsigned long prefix_notif : 1; /* Guest prefix invalidation notification */
			unsigned long              : 3; /* HW */
			unsigned long vsie_notif   : 1; /* Referenced in a shadow table */
			unsigned long              : 1; /* Unused */
			unsigned long              : 4; /* HW */
			unsigned long sd           : 1; /* Soft-Dirty */
			unsigned long pr           : 1; /* Present */
		} fc1;
	} s;
};

union pud {
	unsigned long val;
	union region3_table_entry h;
	struct {
		struct {
			unsigned long              :33; /* HW */
			unsigned long              :14; /* Unused */
			unsigned long              : 1; /* HW */
			unsigned long w            : 1; /* Writable soft-bit */
			unsigned long r            : 1; /* Readable soft-bit */
			unsigned long d            : 1; /* Dirty */
			unsigned long y            : 1; /* Young */
			unsigned long prefix_notif : 1; /* Guest prefix invalidation notification */
			unsigned long              : 3; /* HW */
			unsigned long vsie_notif   : 1; /* Referenced in a shadow table */
			unsigned long              : 1; /* Unused */
			unsigned long              : 4; /* HW */
			unsigned long sd           : 1; /* Soft-Dirty */
			unsigned long pr           : 1; /* Present */
		} fc1;
	} s;
};

union p4d {
	unsigned long val;
	union region2_table_entry h;
};

union pgd {
	unsigned long val;
	union region1_table_entry h;
};

union crste {
	unsigned long val;
	union {
		struct {
			unsigned long   :52;
			unsigned long   : 1;
			unsigned long fc: 1;
			unsigned long p : 1;
			unsigned long   : 1;
			unsigned long   : 2;
			unsigned long i : 1;
			unsigned long   : 1;
			unsigned long tt: 2;
			unsigned long   : 2;
		};
		struct {
			unsigned long to:52;
			unsigned long   : 1;
			unsigned long fc: 1;
			unsigned long p : 1;
			unsigned long   : 1;
			unsigned long tf: 2;
			unsigned long i : 1;
			unsigned long   : 1;
			unsigned long tt: 2;
			unsigned long tl: 2;
		} fc0;
		struct {
			unsigned long    :47;
			unsigned long av : 1; /* ACCF-Validity Control */
			unsigned long acc: 4; /* Access-Control Bits */
			unsigned long f  : 1; /* Fetch-Protection Bit */
			unsigned long fc : 1; /* Format-Control */
			unsigned long p  : 1; /* DAT-Protection Bit */
			unsigned long iep: 1; /* Instruction-Execution-Protection */
			unsigned long    : 2;
			unsigned long i  : 1; /* Segment-Invalid Bit */
			unsigned long cs : 1; /* Common-Segment Bit */
			unsigned long tt : 2; /* Table-Type Bits */
			unsigned long    : 2;
		} fc1;
	} h;
	struct {
		struct {
			unsigned long              :47;
			unsigned long              : 1; /* HW (should be 0) */
			unsigned long w            : 1; /* Writable */
			unsigned long r            : 1; /* Readable */
			unsigned long d            : 1; /* Dirty */
			unsigned long y            : 1; /* Young */
			unsigned long prefix_notif : 1; /* Guest prefix invalidation notification */
			unsigned long              : 3; /* HW */
			unsigned long vsie_notif   : 1; /* Referenced in a shadow table */
			unsigned long              : 1;
			unsigned long              : 4; /* HW */
			unsigned long sd           : 1; /* Soft-Dirty */
			unsigned long pr           : 1; /* Present */
		} fc1;
	} s;
	union {
		struct {
			unsigned long type :16; /* Token type */
			unsigned long par  :16; /* Token parameter */
			unsigned long      :26;
			unsigned long i    : 1; /* Must be 1 */
			unsigned long      : 1;
			unsigned long tt   : 2;
			unsigned long      : 1;
			unsigned long pr   : 1; /* Must be 0 */
		};
		struct {
			unsigned long token:32; /* Token and parameter */
			unsigned long      :32;
		};
	} tok;
	union pmd pmd;
	union pud pud;
	union p4d p4d;
	union pgd pgd;
};

union skey {
	unsigned char skey;
	struct {
		unsigned char acc :4;
		unsigned char fp  :1;
		unsigned char r   :1;
		unsigned char c   :1;
		unsigned char zero:1;
	};
};

static_assert(sizeof(union pgste) == sizeof(unsigned long));
static_assert(sizeof(union pte) == sizeof(unsigned long));
static_assert(sizeof(union pmd) == sizeof(unsigned long));
static_assert(sizeof(union pud) == sizeof(unsigned long));
static_assert(sizeof(union p4d) == sizeof(unsigned long));
static_assert(sizeof(union pgd) == sizeof(unsigned long));
static_assert(sizeof(union crste) == sizeof(unsigned long));
static_assert(sizeof(union skey) == sizeof(char));

struct segment_table {
	union pmd pmds[_CRST_ENTRIES];
};

struct region3_table {
	union pud puds[_CRST_ENTRIES];
};

struct region2_table {
	union p4d p4ds[_CRST_ENTRIES];
};

struct region1_table {
	union pgd pgds[_CRST_ENTRIES];
};

struct crst_table {
	union {
		union crste crstes[_CRST_ENTRIES];
		struct segment_table segment;
		struct region3_table region3;
		struct region2_table region2;
		struct region1_table region1;
	};
};

struct page_table {
	union pte ptes[_PAGE_ENTRIES];
	union pgste pgstes[_PAGE_ENTRIES];
};

static_assert(sizeof(struct crst_table) == _CRST_TABLE_SIZE);
static_assert(sizeof(struct page_table) == PAGE_SIZE);

struct dat_walk;

typedef long (*dat_walk_op)(union crste *crste, gfn_t gfn, gfn_t next, struct dat_walk *w);

struct dat_walk_ops {
	union {
		dat_walk_op crste_ops[4];
		struct {
			dat_walk_op pmd_entry;
			dat_walk_op pud_entry;
			dat_walk_op p4d_entry;
			dat_walk_op pgd_entry;
		};
	};
	long (*pte_entry)(union pte *pte, gfn_t gfn, gfn_t next, struct dat_walk *w);
};

struct dat_walk {
	const struct dat_walk_ops *ops;
	union crste *last;
	union pte *last_pte;
	union asce asce;
	gfn_t start;
	gfn_t end;
	int flags;
	void *priv;
};

struct ptval_param {
	unsigned char offset : 6;
	unsigned char len : 2;
};

/**
 * _pte() - Useful constructor for union pte
 * @pfn: the pfn this pte should point to.
 * @writable: whether the pte should be writable.
 * @dirty: whether the pte should be dirty.
 * @special: whether the pte should be marked as special
 *
 * The pte is also marked as young and present. If the pte is marked as dirty,
 * it gets marked as soft-dirty too. If the pte is not dirty, the hardware
 * protect bit is set (independently of the write softbit); this way proper
 * dirty tracking can be performed.
 *
 * Return: a union pte value.
 */
static inline union pte _pte(kvm_pfn_t pfn, bool writable, bool dirty, bool special)
{
	union pte res = { .val = PFN_PHYS(pfn) };

	res.h.p = !dirty;
	res.s.y = 1;
	res.s.pr = 1;
	res.s.w = writable;
	res.s.d = dirty;
	res.s.sd = dirty;
	res.s.s = special;
	return res;
}

static inline union crste _crste_fc0(kvm_pfn_t pfn, int tt)
{
	union crste res = { .val = PFN_PHYS(pfn) };

	res.h.tt = tt;
	res.h.fc0.tl = _REGION_ENTRY_LENGTH;
	res.h.fc0.tf = 0;
	return res;
}

/**
 * _crste() - Useful constructor for union crste with FC=1
 * @pfn: the pfn this pte should point to.
 * @tt: the table type
 * @writable: whether the pte should be writable.
 * @dirty: whether the pte should be dirty.
 *
 * The crste is also marked as young and present. If the crste is marked as
 * dirty, it gets marked as soft-dirty too. If the crste is not dirty, the
 * hardware protect bit is set (independently of the write softbit); this way
 * proper dirty tracking can be performed.
 *
 * Return: a union crste value.
 */
static inline union crste _crste_fc1(kvm_pfn_t pfn, int tt, bool writable, bool dirty)
{
	union crste res = { .val = PFN_PHYS(pfn) & _SEGMENT_MASK };

	res.h.tt = tt;
	res.h.p = !dirty;
	res.h.fc = 1;
	res.s.fc1.y = 1;
	res.s.fc1.pr = 1;
	res.s.fc1.w = writable;
	res.s.fc1.d = dirty;
	res.s.fc1.sd = dirty;
	return res;
}

union essa_state {
	unsigned char val;
	struct {
		unsigned char		: 2;
		unsigned char nodat	: 1;
		unsigned char exception	: 1;
		unsigned char usage	: 2;
		unsigned char content	: 2;
	};
};

/**
 * struct vsie_rmap - reverse mapping for shadow page table entries
 * @next: pointer to next rmap in the list
 * @r_gfn: virtual rmap address in the shadow guest address space
 */
struct vsie_rmap {
	struct vsie_rmap *next;
	union {
		unsigned long val;
		struct {
			long          level: 8;
			unsigned long      : 4;
			unsigned long r_gfn:52;
		};
	};
};

static_assert(sizeof(struct vsie_rmap) == 2 * sizeof(long));

#define KVM_S390_MMU_CACHE_N_CRSTS	6
#define KVM_S390_MMU_CACHE_N_PTS	2
#define KVM_S390_MMU_CACHE_N_RMAPS	16
struct kvm_s390_mmu_cache {
	void *crsts[KVM_S390_MMU_CACHE_N_CRSTS];
	void *pts[KVM_S390_MMU_CACHE_N_PTS];
	void *rmaps[KVM_S390_MMU_CACHE_N_RMAPS];
	short int n_crsts;
	short int n_pts;
	short int n_rmaps;
};

struct guest_fault {
	gfn_t gfn;		/* Guest frame */
	kvm_pfn_t pfn;		/* Host PFN */
	struct page *page;	/* Host page */
	union pte *ptep;	/* Used to resolve the fault, or NULL */
	union crste *crstep;	/* Used to resolve the fault, or NULL */
	bool writable;		/* Mapping is writable */
	bool write_attempt;	/* Write access attempted */
	bool attempt_pfault;	/* Attempt a pfault first */
	bool valid;		/* This entry contains valid data */
	void (*callback)(struct guest_fault *f);
	void *priv;
};

/*
 *	0	1	2	3	4	5	6	7
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 *  0	|				|	    PGT_ADDR		|
 *  8	|	 VMADDR		|					|
 * 16	|								|
 * 24	|								|
 */
#define MKPTVAL(o, l) ((struct ptval_param) { .offset = (o), .len = ((l) + 1) / 2 - 1})
#define PTVAL_PGT_ADDR	MKPTVAL(4, 8)
#define PTVAL_VMADDR	MKPTVAL(8, 6)

union pgste __must_check __dat_ptep_xchg(union pte *ptep, union pgste pgste, union pte new,
					 gfn_t gfn, union asce asce, bool uses_skeys);
bool dat_crstep_xchg_atomic(union crste *crstep, union crste old, union crste new, gfn_t gfn,
			    union asce asce);
void dat_crstep_xchg(union crste *crstep, union crste new, gfn_t gfn, union asce asce);

long _dat_walk_gfn_range(gfn_t start, gfn_t end, union asce asce,
			 const struct dat_walk_ops *ops, int flags, void *priv);

int dat_entry_walk(struct kvm_s390_mmu_cache *mc, gfn_t gfn, union asce asce, int flags,
		   int walk_level, union crste **last, union pte **ptepp);
void dat_free_level(struct crst_table *table, bool owns_ptes);
struct crst_table *dat_alloc_crst_sleepable(unsigned long init);
int dat_set_asce_limit(struct kvm_s390_mmu_cache *mc, union asce *asce, int newtype);
int dat_get_storage_key(union asce asce, gfn_t gfn, union skey *skey);
int dat_set_storage_key(struct kvm_s390_mmu_cache *mc, union asce asce, gfn_t gfn,
			union skey skey, bool nq);
int dat_cond_set_storage_key(struct kvm_s390_mmu_cache *mmc, union asce asce, gfn_t gfn,
			     union skey skey, union skey *oldkey, bool nq, bool mr, bool mc);
int dat_reset_reference_bit(union asce asce, gfn_t gfn);
long dat_reset_skeys(union asce asce, gfn_t start);

unsigned long dat_get_ptval(struct page_table *table, struct ptval_param param);
void dat_set_ptval(struct page_table *table, struct ptval_param param, unsigned long val);

int dat_set_slot(struct kvm_s390_mmu_cache *mc, union asce asce, gfn_t start, gfn_t end,
		 u16 type, u16 param);
int dat_set_prefix_notif_bit(union asce asce, gfn_t gfn);
bool dat_test_age_gfn(union asce asce, gfn_t start, gfn_t end);
int dat_link(struct kvm_s390_mmu_cache *mc, union asce asce, int level,
	     bool uses_skeys, struct guest_fault *f);

int dat_perform_essa(union asce asce, gfn_t gfn, int orc, union essa_state *state, bool *dirty);
long dat_reset_cmma(union asce asce, gfn_t start_gfn);
int dat_peek_cmma(gfn_t start, union asce asce, unsigned int *count, u8 *values);
int dat_get_cmma(union asce asce, gfn_t *start, unsigned int *count, u8 *values, atomic64_t *rem);
int dat_set_cmma_bits(struct kvm_s390_mmu_cache *mc, union asce asce, gfn_t gfn,
		      unsigned long count, unsigned long mask, const uint8_t *bits);

int kvm_s390_mmu_cache_topup(struct kvm_s390_mmu_cache *mc);

#define GFP_KVM_S390_MMU_CACHE (GFP_ATOMIC | __GFP_ACCOUNT | __GFP_NOWARN)

static inline struct page_table *kvm_s390_mmu_cache_alloc_pt(struct kvm_s390_mmu_cache *mc)
{
	if (mc->n_pts)
		return mc->pts[--mc->n_pts];
	return (void *)__get_free_page(GFP_KVM_S390_MMU_CACHE);
}

static inline struct crst_table *kvm_s390_mmu_cache_alloc_crst(struct kvm_s390_mmu_cache *mc)
{
	if (mc->n_crsts)
		return mc->crsts[--mc->n_crsts];
	return (void *)__get_free_pages(GFP_KVM_S390_MMU_CACHE | __GFP_COMP, CRST_ALLOC_ORDER);
}

static inline struct vsie_rmap *kvm_s390_mmu_cache_alloc_rmap(struct kvm_s390_mmu_cache *mc)
{
	if (mc->n_rmaps)
		return mc->rmaps[--mc->n_rmaps];
	return kzalloc(sizeof(struct vsie_rmap), GFP_KVM_S390_MMU_CACHE);
}

static inline struct crst_table *crste_table_start(union crste *crstep)
{
	return (struct crst_table *)ALIGN_DOWN((unsigned long)crstep, _CRST_TABLE_SIZE);
}

static inline struct page_table *pte_table_start(union pte *ptep)
{
	return (struct page_table *)ALIGN_DOWN((unsigned long)ptep, _PAGE_TABLE_SIZE);
}

static inline bool crdte_crste(union crste *crstep, union crste old, union crste new, gfn_t gfn,
			       union asce asce)
{
	unsigned long dtt = 0x10 | new.h.tt << 2;
	void *table = crste_table_start(crstep);

	return crdte(old.val, new.val, table, dtt, gfn_to_gpa(gfn), asce.val);
}

/**
 * idte_crste() - invalidate a crste entry using idte
 * @crstep: pointer to the crste to be invalidated
 * @gfn: a gfn mapped by the crste
 * @opt: options for the idte instruction
 * @asce: the asce
 * @local: whether the operation is cpu-local
 */
static __always_inline void idte_crste(union crste *crstep, gfn_t gfn, unsigned long opt,
				       union asce asce, int local)
{
	unsigned long table_origin = __pa(crste_table_start(crstep));
	unsigned long gaddr = gfn_to_gpa(gfn) & HPAGE_MASK;

	if (__builtin_constant_p(opt) && opt == 0) {
		/* flush without guest asce */
		asm volatile("idte	%[table_origin],0,%[gaddr],%[local]"
			: "+m" (*crstep)
			: [table_origin] "a" (table_origin), [gaddr] "a" (gaddr),
			  [local] "i" (local)
			: "cc");
	} else {
		/* flush with guest asce */
		asm volatile("idte %[table_origin],%[asce],%[gaddr_opt],%[local]"
			: "+m" (*crstep)
			: [table_origin] "a" (table_origin), [gaddr_opt] "a" (gaddr | opt),
			  [asce] "a" (asce.val), [local] "i" (local)
			: "cc");
	}
}

static inline void dat_init_pgstes(struct page_table *pt, unsigned long val)
{
	memset64((void *)pt->pgstes, val, PTRS_PER_PTE);
}

static inline void dat_init_page_table(struct page_table *pt, unsigned long ptes,
				       unsigned long pgstes)
{
	memset64((void *)pt->ptes, ptes, PTRS_PER_PTE);
	dat_init_pgstes(pt, pgstes);
}

static inline gfn_t asce_end(union asce asce)
{
	return 1ULL << ((asce.dt + 1) * 11 + _SEGMENT_SHIFT - PAGE_SHIFT);
}

#define _CRSTE(x) ((union crste) { .val = _Generic((x),	\
			union pgd : (x).val,		\
			union p4d : (x).val,		\
			union pud : (x).val,		\
			union pmd : (x).val,		\
			union crste : (x).val)})

#define _CRSTEP(x) ((union crste *)_Generic((*(x)),	\
				union pgd : (x),	\
				union p4d : (x),	\
				union pud : (x),	\
				union pmd : (x),	\
				union crste : (x)))

#define _CRSTP(x) ((struct crst_table *)_Generic((*(x)),	\
		struct crst_table : (x),			\
		struct segment_table : (x),			\
		struct region3_table : (x),			\
		struct region2_table : (x),			\
		struct region1_table : (x)))

static inline bool asce_contains_gfn(union asce asce, gfn_t gfn)
{
	return gfn < asce_end(asce);
}

static inline bool is_pmd(union crste crste)
{
	return crste.h.tt == TABLE_TYPE_SEGMENT;
}

static inline bool is_pud(union crste crste)
{
	return crste.h.tt == TABLE_TYPE_REGION3;
}

static inline bool is_p4d(union crste crste)
{
	return crste.h.tt == TABLE_TYPE_REGION2;
}

static inline bool is_pgd(union crste crste)
{
	return crste.h.tt == TABLE_TYPE_REGION1;
}

static inline phys_addr_t pmd_origin_large(union pmd pmd)
{
	return pmd.val & _SEGMENT_ENTRY_ORIGIN_LARGE;
}

static inline phys_addr_t pud_origin_large(union pud pud)
{
	return pud.val & _REGION3_ENTRY_ORIGIN_LARGE;
}

/**
 * crste_origin_large() - Return the large frame origin of a large crste
 * @crste: The crste whose origin is to be returned. Should be either a
 *         region-3 table entry or a segment table entry, in both cases with
 *         FC set to 1 (large pages).
 *
 * Return: The origin of the large frame pointed to by @crste, or -1 if the
 *         crste was not large (wrong table type, or FC==0)
 */
static inline phys_addr_t crste_origin_large(union crste crste)
{
	if (unlikely(!crste.h.fc || crste.h.tt > TABLE_TYPE_REGION3))
		return -1;
	if (is_pmd(crste))
		return pmd_origin_large(crste.pmd);
	return pud_origin_large(crste.pud);
}

#define crste_origin(x) (_Generic((x),				\
		union pmd : (x).val & _SEGMENT_ENTRY_ORIGIN,	\
		union pud : (x).val & _REGION_ENTRY_ORIGIN,	\
		union p4d : (x).val & _REGION_ENTRY_ORIGIN,	\
		union pgd : (x).val & _REGION_ENTRY_ORIGIN))

static inline unsigned long pte_origin(union pte pte)
{
	return pte.val & PAGE_MASK;
}

static inline bool pmd_prefix(union pmd pmd)
{
	return pmd.h.fc && pmd.s.fc1.prefix_notif;
}

static inline bool pud_prefix(union pud pud)
{
	return pud.h.fc && pud.s.fc1.prefix_notif;
}

static inline bool crste_leaf(union crste crste)
{
	return (crste.h.tt <= TABLE_TYPE_REGION3) && crste.h.fc;
}

static inline bool crste_prefix(union crste crste)
{
	return crste_leaf(crste) && crste.s.fc1.prefix_notif;
}

static inline bool crste_dirty(union crste crste)
{
	return crste_leaf(crste) && crste.s.fc1.d;
}

static inline union pgste *pgste_of(union pte *pte)
{
	return (union pgste *)(pte + _PAGE_ENTRIES);
}

static inline bool pte_hole(union pte pte)
{
	return pte.h.i && !pte.tok.pr && pte.tok.type != _DAT_TOKEN_NONE;
}

static inline bool _crste_hole(union crste crste)
{
	return crste.h.i && !crste.tok.pr && crste.tok.type != _DAT_TOKEN_NONE;
}

#define crste_hole(x) _crste_hole(_CRSTE(x))

static inline bool _crste_none(union crste crste)
{
	return crste.h.i && !crste.tok.pr && crste.tok.type == _DAT_TOKEN_NONE;
}

#define crste_none(x) _crste_none(_CRSTE(x))

static inline phys_addr_t large_pud_to_phys(union pud pud, gfn_t gfn)
{
	return pud_origin_large(pud) | (gfn_to_gpa(gfn) & ~_REGION3_MASK);
}

static inline phys_addr_t large_pmd_to_phys(union pmd pmd, gfn_t gfn)
{
	return pmd_origin_large(pmd) | (gfn_to_gpa(gfn) & ~_SEGMENT_MASK);
}

static inline phys_addr_t large_crste_to_phys(union crste crste, gfn_t gfn)
{
	if (unlikely(!crste.h.fc || crste.h.tt > TABLE_TYPE_REGION3))
		return -1;
	if (is_pmd(crste))
		return large_pmd_to_phys(crste.pmd, gfn);
	return large_pud_to_phys(crste.pud, gfn);
}

static inline bool cspg_crste(union crste *crstep, union crste old, union crste new)
{
	return cspg(&crstep->val, old.val, new.val);
}

static inline struct page_table *dereference_pmd(union pmd pmd)
{
	return phys_to_virt(crste_origin(pmd));
}

static inline struct segment_table *dereference_pud(union pud pud)
{
	return phys_to_virt(crste_origin(pud));
}

static inline struct region3_table *dereference_p4d(union p4d p4d)
{
	return phys_to_virt(crste_origin(p4d));
}

static inline struct region2_table *dereference_pgd(union pgd pgd)
{
	return phys_to_virt(crste_origin(pgd));
}

static inline struct crst_table *_dereference_crste(union crste crste)
{
	if (unlikely(is_pmd(crste)))
		return NULL;
	return phys_to_virt(crste_origin(crste.pud));
}

#define dereference_crste(x) (_Generic((x),			\
		union pud : _dereference_crste(_CRSTE(x)),	\
		union p4d : _dereference_crste(_CRSTE(x)),	\
		union pgd : _dereference_crste(_CRSTE(x)),	\
		union crste : _dereference_crste(_CRSTE(x))))

static inline struct crst_table *dereference_asce(union asce asce)
{
	return phys_to_virt(asce.val & _ASCE_ORIGIN);
}

static inline void asce_flush_tlb(union asce asce)
{
	__tlb_flush_idte(asce.val);
}

static inline bool pgste_get_trylock(union pte *ptep, union pgste *res)
{
	union pgste *pgstep = pgste_of(ptep);
	union pgste old_pgste;

	if (READ_ONCE(pgstep->val) & PGSTE_PCL_BIT)
		return false;
	old_pgste.val = __atomic64_or_barrier(PGSTE_PCL_BIT, &pgstep->val);
	if (old_pgste.pcl)
		return false;
	old_pgste.pcl = 1;
	*res = old_pgste;
	return true;
}

static inline union pgste pgste_get_lock(union pte *ptep)
{
	union pgste res;

	while (!pgste_get_trylock(ptep, &res))
		cpu_relax();
	return res;
}

static inline void pgste_set_unlock(union pte *ptep, union pgste pgste)
{
	pgste.pcl = 0;
	barrier();
	WRITE_ONCE(*pgste_of(ptep), pgste);
}

static inline void dat_ptep_xchg(union pte *ptep, union pte new, gfn_t gfn, union asce asce,
				 bool has_skeys)
{
	union pgste pgste;

	pgste = pgste_get_lock(ptep);
	pgste = __dat_ptep_xchg(ptep, pgste, new, gfn, asce, has_skeys);
	pgste_set_unlock(ptep, pgste);
}

static inline void dat_ptep_clear(union pte *ptep, gfn_t gfn, union asce asce, bool has_skeys)
{
	dat_ptep_xchg(ptep, _PTE_EMPTY, gfn, asce, has_skeys);
}

static inline void dat_free_pt(struct page_table *pt)
{
	free_page((unsigned long)pt);
}

static inline void _dat_free_crst(struct crst_table *table)
{
	free_pages((unsigned long)table, CRST_ALLOC_ORDER);
}

#define dat_free_crst(x) _dat_free_crst(_CRSTP(x))

static inline void kvm_s390_free_mmu_cache(struct kvm_s390_mmu_cache *mc)
{
	if (!mc)
		return;
	while (mc->n_pts)
		dat_free_pt(mc->pts[--mc->n_pts]);
	while (mc->n_crsts)
		_dat_free_crst(mc->crsts[--mc->n_crsts]);
	while (mc->n_rmaps)
		kfree(mc->rmaps[--mc->n_rmaps]);
	kfree(mc);
}

DEFINE_FREE(kvm_s390_mmu_cache, struct kvm_s390_mmu_cache *, if (_T) kvm_s390_free_mmu_cache(_T))

static inline struct kvm_s390_mmu_cache *kvm_s390_new_mmu_cache(void)
{
	struct kvm_s390_mmu_cache *mc __free(kvm_s390_mmu_cache) = NULL;

	mc = kzalloc(sizeof(*mc), GFP_KERNEL_ACCOUNT);
	if (mc && !kvm_s390_mmu_cache_topup(mc))
		return_ptr(mc);
	return NULL;
}

static inline bool dat_pmdp_xchg_atomic(union pmd *pmdp, union pmd old, union pmd new,
					gfn_t gfn, union asce asce)
{
	return dat_crstep_xchg_atomic(_CRSTEP(pmdp), _CRSTE(old), _CRSTE(new), gfn, asce);
}

static inline bool dat_pudp_xchg_atomic(union pud *pudp, union pud old, union pud new,
					gfn_t gfn, union asce asce)
{
	return dat_crstep_xchg_atomic(_CRSTEP(pudp), _CRSTE(old), _CRSTE(new), gfn, asce);
}

static inline void dat_crstep_clear(union crste *crstep, gfn_t gfn, union asce asce)
{
	union crste newcrste = _CRSTE_EMPTY(crstep->h.tt);

	dat_crstep_xchg(crstep, newcrste, gfn, asce);
}

static inline int get_level(union crste *crstep, union pte *ptep)
{
	return ptep ? TABLE_TYPE_PAGE_TABLE : crstep->h.tt;
}

static inline int dat_delete_slot(struct kvm_s390_mmu_cache *mc, union asce asce, gfn_t start,
				  unsigned long npages)
{
	return dat_set_slot(mc, asce, start, start + npages, _DAT_TOKEN_PIC, PGM_ADDRESSING);
}

static inline int dat_create_slot(struct kvm_s390_mmu_cache *mc, union asce asce, gfn_t start,
				  unsigned long npages)
{
	return dat_set_slot(mc, asce, start, start + npages, _DAT_TOKEN_NONE, 0);
}

static inline bool crste_is_ucas(union crste crste)
{
	return is_pmd(crste) && crste.h.i && crste.h.fc0.tl == 1 && crste.h.fc == 0;
}

#endif /* __KVM_S390_DAT_H */
