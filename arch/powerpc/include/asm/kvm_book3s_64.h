/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright SUSE Linux Products GmbH 2010
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#ifndef __ASM_KVM_BOOK3S_64_H__
#define __ASM_KVM_BOOK3S_64_H__

#include <linux/string.h>
#include <asm/bitops.h>
#include <asm/book3s/64/mmu-hash.h>
#include <asm/cpu_has_feature.h>
#include <asm/ppc-opcode.h>
#include <asm/pte-walk.h>

#ifdef CONFIG_PPC_PSERIES
static inline bool kvmhv_on_pseries(void)
{
	return !cpu_has_feature(CPU_FTR_HVMODE);
}
#else
static inline bool kvmhv_on_pseries(void)
{
	return false;
}
#endif

/*
 * Structure for a nested guest, that is, for a guest that is managed by
 * one of our guests.
 */
struct kvm_nested_guest {
	struct kvm *l1_host;		/* L1 VM that owns this nested guest */
	int l1_lpid;			/* lpid L1 guest thinks this guest is */
	int shadow_lpid;		/* real lpid of this nested guest */
	pgd_t *shadow_pgtable;		/* our page table for this guest */
	u64 l1_gr_to_hr;		/* L1's addr of part'n-scoped table */
	u64 process_table;		/* process table entry for this guest */
	u64 hfscr;			/* HFSCR that the L1 requested for this nested guest */
	long refcnt;			/* number of pointers to this struct */
	struct mutex tlb_lock;		/* serialize page faults and tlbies */
	struct kvm_nested_guest *next;
	cpumask_t need_tlb_flush;
	cpumask_t cpu_in_guest;
	short prev_cpu[NR_CPUS];
	u8 radix;			/* is this nested guest radix */
};

/*
 * We define a nested rmap entry as a single 64-bit quantity
 * 0xFFF0000000000000	12-bit lpid field
 * 0x000FFFFFFFFFF000	40-bit guest 4k page frame number
 * 0x0000000000000001	1-bit  single entry flag
 */
#define RMAP_NESTED_LPID_MASK		0xFFF0000000000000UL
#define RMAP_NESTED_LPID_SHIFT		(52)
#define RMAP_NESTED_GPA_MASK		0x000FFFFFFFFFF000UL
#define RMAP_NESTED_IS_SINGLE_ENTRY	0x0000000000000001UL

/* Structure for a nested guest rmap entry */
struct rmap_nested {
	struct llist_node list;
	u64 rmap;
};

/*
 * for_each_nest_rmap_safe - iterate over the list of nested rmap entries
 *			     safe against removal of the list entry or NULL list
 * @pos:	a (struct rmap_nested *) to use as a loop cursor
 * @node:	pointer to the first entry
 *		NOTE: this can be NULL
 * @rmapp:	an (unsigned long *) in which to return the rmap entries on each
 *		iteration
 *		NOTE: this must point to already allocated memory
 *
 * The nested_rmap is a llist of (struct rmap_nested) entries pointed to by the
 * rmap entry in the memslot. The list is always terminated by a "single entry"
 * stored in the list element of the final entry of the llist. If there is ONLY
 * a single entry then this is itself in the rmap entry of the memslot, not a
 * llist head pointer.
 *
 * Note that the iterator below assumes that a nested rmap entry is always
 * non-zero.  This is true for our usage because the LPID field is always
 * non-zero (zero is reserved for the host).
 *
 * This should be used to iterate over the list of rmap_nested entries with
 * processing done on the u64 rmap value given by each iteration. This is safe
 * against removal of list entries and it is always safe to call free on (pos).
 *
 * e.g.
 * struct rmap_nested *cursor;
 * struct llist_node *first;
 * unsigned long rmap;
 * for_each_nest_rmap_safe(cursor, first, &rmap) {
 *	do_something(rmap);
 *	free(cursor);
 * }
 */
#define for_each_nest_rmap_safe(pos, node, rmapp)			       \
	for ((pos) = llist_entry((node), typeof(*(pos)), list);		       \
	     (node) &&							       \
	     (*(rmapp) = ((RMAP_NESTED_IS_SINGLE_ENTRY & ((u64) (node))) ?     \
			  ((u64) (node)) : ((pos)->rmap))) &&		       \
	     (((node) = ((RMAP_NESTED_IS_SINGLE_ENTRY & ((u64) (node))) ?      \
			 ((struct llist_node *) ((pos) = NULL)) :	       \
			 (pos)->list.next)), true);			       \
	     (pos) = llist_entry((node), typeof(*(pos)), list))

struct kvm_nested_guest *kvmhv_get_nested(struct kvm *kvm, int l1_lpid,
					  bool create);
void kvmhv_put_nested(struct kvm_nested_guest *gp);
int kvmhv_nested_next_lpid(struct kvm *kvm, int lpid);

/* Encoding of first parameter for H_TLB_INVALIDATE */
#define H_TLBIE_P1_ENC(ric, prs, r)	(___PPC_RIC(ric) | ___PPC_PRS(prs) | \
					 ___PPC_R(r))

/* Power architecture requires HPT is at least 256kiB, at most 64TiB */
#define PPC_MIN_HPT_ORDER	18
#define PPC_MAX_HPT_ORDER	46

#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
static inline struct kvmppc_book3s_shadow_vcpu *svcpu_get(struct kvm_vcpu *vcpu)
{
	preempt_disable();
	return &get_paca()->shadow_vcpu;
}

static inline void svcpu_put(struct kvmppc_book3s_shadow_vcpu *svcpu)
{
	preempt_enable();
}
#endif

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE

static inline bool kvm_is_radix(struct kvm *kvm)
{
	return kvm->arch.radix;
}

static inline bool kvmhv_vcpu_is_radix(struct kvm_vcpu *vcpu)
{
	bool radix;

	if (vcpu->arch.nested)
		radix = vcpu->arch.nested->radix;
	else
		radix = kvm_is_radix(vcpu->kvm);

	return radix;
}

int kvmhv_vcpu_entry_p9(struct kvm_vcpu *vcpu, u64 time_limit, unsigned long lpcr);

#define KVM_DEFAULT_HPT_ORDER	24	/* 16MB HPT by default */
#endif

/*
 * Invalid HDSISR value which is used to indicate when HW has not set the reg.
 * Used to work around an errata.
 */
#define HDSISR_CANARY	0x7fff

/*
 * We use a lock bit in HPTE dword 0 to synchronize updates and
 * accesses to each HPTE, and another bit to indicate non-present
 * HPTEs.
 */
#define HPTE_V_HVLOCK	0x40UL
#define HPTE_V_ABSENT	0x20UL

/*
 * We use this bit in the guest_rpte field of the revmap entry
 * to indicate a modified HPTE.
 */
#define HPTE_GR_MODIFIED	(1ul << 62)

/* These bits are reserved in the guest view of the HPTE */
#define HPTE_GR_RESERVED	HPTE_GR_MODIFIED

static inline long try_lock_hpte(__be64 *hpte, unsigned long bits)
{
	unsigned long tmp, old;
	__be64 be_lockbit, be_bits;

	/*
	 * We load/store in native endian, but the HTAB is in big endian. If
	 * we byte swap all data we apply on the PTE we're implicitly correct
	 * again.
	 */
	be_lockbit = cpu_to_be64(HPTE_V_HVLOCK);
	be_bits = cpu_to_be64(bits);

	asm volatile("	ldarx	%0,0,%2\n"
		     "	and.	%1,%0,%3\n"
		     "	bne	2f\n"
		     "	or	%0,%0,%4\n"
		     "  stdcx.	%0,0,%2\n"
		     "	beq+	2f\n"
		     "	mr	%1,%3\n"
		     "2:	isync"
		     : "=&r" (tmp), "=&r" (old)
		     : "r" (hpte), "r" (be_bits), "r" (be_lockbit)
		     : "cc", "memory");
	return old == 0;
}

static inline void unlock_hpte(__be64 *hpte, unsigned long hpte_v)
{
	hpte_v &= ~HPTE_V_HVLOCK;
	asm volatile(PPC_RELEASE_BARRIER "" : : : "memory");
	hpte[0] = cpu_to_be64(hpte_v);
}

/* Without barrier */
static inline void __unlock_hpte(__be64 *hpte, unsigned long hpte_v)
{
	hpte_v &= ~HPTE_V_HVLOCK;
	hpte[0] = cpu_to_be64(hpte_v);
}

/*
 * These functions encode knowledge of the POWER7/8/9 hardware
 * interpretations of the HPTE LP (large page size) field.
 */
static inline int kvmppc_hpte_page_shifts(unsigned long h, unsigned long l)
{
	unsigned int lphi;

	if (!(h & HPTE_V_LARGE))
		return 12;	/* 4kB */
	lphi = (l >> 16) & 0xf;
	switch ((l >> 12) & 0xf) {
	case 0:
		return !lphi ? 24 : 0;		/* 16MB */
		break;
	case 1:
		return 16;			/* 64kB */
		break;
	case 3:
		return !lphi ? 34 : 0;		/* 16GB */
		break;
	case 7:
		return (16 << 8) + 12;		/* 64kB in 4kB */
		break;
	case 8:
		if (!lphi)
			return (24 << 8) + 16;	/* 16MB in 64kkB */
		if (lphi == 3)
			return (24 << 8) + 12;	/* 16MB in 4kB */
		break;
	}
	return 0;
}

static inline int kvmppc_hpte_base_page_shift(unsigned long h, unsigned long l)
{
	return kvmppc_hpte_page_shifts(h, l) & 0xff;
}

static inline int kvmppc_hpte_actual_page_shift(unsigned long h, unsigned long l)
{
	int tmp = kvmppc_hpte_page_shifts(h, l);

	if (tmp >= 0x100)
		tmp >>= 8;
	return tmp;
}

static inline unsigned long kvmppc_actual_pgsz(unsigned long v, unsigned long r)
{
	int shift = kvmppc_hpte_actual_page_shift(v, r);

	if (shift)
		return 1ul << shift;
	return 0;
}

static inline int kvmppc_pgsize_lp_encoding(int base_shift, int actual_shift)
{
	switch (base_shift) {
	case 12:
		switch (actual_shift) {
		case 12:
			return 0;
		case 16:
			return 7;
		case 24:
			return 0x38;
		}
		break;
	case 16:
		switch (actual_shift) {
		case 16:
			return 1;
		case 24:
			return 8;
		}
		break;
	case 24:
		return 0;
	}
	return -1;
}

static inline unsigned long compute_tlbie_rb(unsigned long v, unsigned long r,
					     unsigned long pte_index)
{
	int a_pgshift, b_pgshift;
	unsigned long rb = 0, va_low, sllp;

	b_pgshift = a_pgshift = kvmppc_hpte_page_shifts(v, r);
	if (a_pgshift >= 0x100) {
		b_pgshift &= 0xff;
		a_pgshift >>= 8;
	}

	/*
	 * Ignore the top 14 bits of va
	 * v have top two bits covering segment size, hence move
	 * by 16 bits, Also clear the lower HPTE_V_AVPN_SHIFT (7) bits.
	 * AVA field in v also have the lower 23 bits ignored.
	 * For base page size 4K we need 14 .. 65 bits (so need to
	 * collect extra 11 bits)
	 * For others we need 14..14+i
	 */
	/* This covers 14..54 bits of va*/
	rb = (v & ~0x7fUL) << 16;		/* AVA field */

	/*
	 * AVA in v had cleared lower 23 bits. We need to derive
	 * that from pteg index
	 */
	va_low = pte_index >> 3;
	if (v & HPTE_V_SECONDARY)
		va_low = ~va_low;
	/*
	 * get the vpn bits from va_low using reverse of hashing.
	 * In v we have va with 23 bits dropped and then left shifted
	 * HPTE_V_AVPN_SHIFT (7) bits. Now to find vsid we need
	 * right shift it with (SID_SHIFT - (23 - 7))
	 */
	if (!(v & HPTE_V_1TB_SEG))
		va_low ^= v >> (SID_SHIFT - 16);
	else
		va_low ^= v >> (SID_SHIFT_1T - 16);
	va_low &= 0x7ff;

	if (b_pgshift <= 12) {
		if (a_pgshift > 12) {
			sllp = (a_pgshift == 16) ? 5 : 4;
			rb |= sllp << 5;	/*  AP field */
		}
		rb |= (va_low & 0x7ff) << 12;	/* remaining 11 bits of AVA */
	} else {
		int aval_shift;
		/*
		 * remaining bits of AVA/LP fields
		 * Also contain the rr bits of LP
		 */
		rb |= (va_low << b_pgshift) & 0x7ff000;
		/*
		 * Now clear not needed LP bits based on actual psize
		 */
		rb &= ~((1ul << a_pgshift) - 1);
		/*
		 * AVAL field 58..77 - base_page_shift bits of va
		 * we have space for 58..64 bits, Missing bits should
		 * be zero filled. +1 is to take care of L bit shift
		 */
		aval_shift = 64 - (77 - b_pgshift) + 1;
		rb |= ((va_low << aval_shift) & 0xfe);

		rb |= 1;		/* L field */
		rb |= r & 0xff000 & ((1ul << a_pgshift) - 1); /* LP field */
	}
	rb |= (v >> HPTE_V_SSIZE_SHIFT) << 8;	/* B field */
	return rb;
}

static inline unsigned long hpte_rpn(unsigned long ptel, unsigned long psize)
{
	return ((ptel & HPTE_R_RPN) & ~(psize - 1)) >> PAGE_SHIFT;
}

static inline int hpte_is_writable(unsigned long ptel)
{
	unsigned long pp = ptel & (HPTE_R_PP0 | HPTE_R_PP);

	return pp != PP_RXRX && pp != PP_RXXX;
}

static inline unsigned long hpte_make_readonly(unsigned long ptel)
{
	if ((ptel & HPTE_R_PP0) || (ptel & HPTE_R_PP) == PP_RWXX)
		ptel = (ptel & ~HPTE_R_PP) | PP_RXXX;
	else
		ptel |= PP_RXRX;
	return ptel;
}

static inline bool hpte_cache_flags_ok(unsigned long hptel, bool is_ci)
{
	unsigned int wimg = hptel & HPTE_R_WIMG;

	/* Handle SAO */
	if (wimg == (HPTE_R_W | HPTE_R_I | HPTE_R_M) &&
	    cpu_has_feature(CPU_FTR_ARCH_206))
		wimg = HPTE_R_M;

	if (!is_ci)
		return wimg == HPTE_R_M;
	/*
	 * if host is mapped cache inhibited, make sure hptel also have
	 * cache inhibited.
	 */
	if (wimg & HPTE_R_W) /* FIXME!! is this ok for all guest. ? */
		return false;
	return !!(wimg & HPTE_R_I);
}

/*
 * If it's present and writable, atomically set dirty and referenced bits and
 * return the PTE, otherwise return 0.
 */
static inline pte_t kvmppc_read_update_linux_pte(pte_t *ptep, int writing)
{
	pte_t old_pte, new_pte = __pte(0);

	while (1) {
		/*
		 * Make sure we don't reload from ptep
		 */
		old_pte = READ_ONCE(*ptep);
		/*
		 * wait until H_PAGE_BUSY is clear then set it atomically
		 */
		if (unlikely(pte_val(old_pte) & H_PAGE_BUSY)) {
			cpu_relax();
			continue;
		}
		/* If pte is not present return None */
		if (unlikely(!pte_present(old_pte)))
			return __pte(0);

		new_pte = pte_mkyoung(old_pte);
		if (writing && pte_write(old_pte))
			new_pte = pte_mkdirty(new_pte);

		if (pte_xchg(ptep, old_pte, new_pte))
			break;
	}
	return new_pte;
}

static inline bool hpte_read_permission(unsigned long pp, unsigned long key)
{
	if (key)
		return PP_RWRX <= pp && pp <= PP_RXRX;
	return true;
}

static inline bool hpte_write_permission(unsigned long pp, unsigned long key)
{
	if (key)
		return pp == PP_RWRW;
	return pp <= PP_RWRW;
}

static inline int hpte_get_skey_perm(unsigned long hpte_r, unsigned long amr)
{
	unsigned long skey;

	skey = ((hpte_r & HPTE_R_KEY_HI) >> 57) |
		((hpte_r & HPTE_R_KEY_LO) >> 9);
	return (amr >> (62 - 2 * skey)) & 3;
}

static inline void lock_rmap(unsigned long *rmap)
{
	do {
		while (test_bit(KVMPPC_RMAP_LOCK_BIT, rmap))
			cpu_relax();
	} while (test_and_set_bit_lock(KVMPPC_RMAP_LOCK_BIT, rmap));
}

static inline void unlock_rmap(unsigned long *rmap)
{
	__clear_bit_unlock(KVMPPC_RMAP_LOCK_BIT, rmap);
}

static inline bool slot_is_aligned(struct kvm_memory_slot *memslot,
				   unsigned long pagesize)
{
	unsigned long mask = (pagesize >> PAGE_SHIFT) - 1;

	if (pagesize <= PAGE_SIZE)
		return true;
	return !(memslot->base_gfn & mask) && !(memslot->npages & mask);
}

/*
 * This works for 4k, 64k and 16M pages on POWER7,
 * and 4k and 16M pages on PPC970.
 */
static inline unsigned long slb_pgsize_encoding(unsigned long psize)
{
	unsigned long senc = 0;

	if (psize > 0x1000) {
		senc = SLB_VSID_L;
		if (psize == 0x10000)
			senc |= SLB_VSID_LP_01;
	}
	return senc;
}

static inline int is_vrma_hpte(unsigned long hpte_v)
{
	return (hpte_v & ~0xffffffUL) ==
		(HPTE_V_1TB_SEG | (VRMA_VSID << (40 - 16)));
}

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
/*
 * Note modification of an HPTE; set the HPTE modified bit
 * if anyone is interested.
 */
static inline void note_hpte_modification(struct kvm *kvm,
					  struct revmap_entry *rev)
{
	if (atomic_read(&kvm->arch.hpte_mod_interest))
		rev->guest_rpte |= HPTE_GR_MODIFIED;
}

/*
 * Like kvm_memslots(), but for use in real mode when we can't do
 * any RCU stuff (since the secondary threads are offline from the
 * kernel's point of view), and we can't print anything.
 * Thus we use rcu_dereference_raw() rather than rcu_dereference_check().
 */
static inline struct kvm_memslots *kvm_memslots_raw(struct kvm *kvm)
{
	return rcu_dereference_raw_check(kvm->memslots[0]);
}

extern void kvmppc_mmu_debugfs_init(struct kvm *kvm);
extern void kvmhv_radix_debugfs_init(struct kvm *kvm);

extern void kvmhv_rm_send_ipi(int cpu);

static inline unsigned long kvmppc_hpt_npte(struct kvm_hpt_info *hpt)
{
	/* HPTEs are 2**4 bytes long */
	return 1UL << (hpt->order - 4);
}

static inline unsigned long kvmppc_hpt_mask(struct kvm_hpt_info *hpt)
{
	/* 128 (2**7) bytes in each HPTEG */
	return (1UL << (hpt->order - 7)) - 1;
}

/* Set bits in a dirty bitmap, which is in LE format */
static inline void set_dirty_bits(unsigned long *map, unsigned long i,
				  unsigned long npages)
{

	if (npages >= 8)
		memset((char *)map + i / 8, 0xff, npages / 8);
	else
		for (; npages; ++i, --npages)
			__set_bit_le(i, map);
}

static inline void set_dirty_bits_atomic(unsigned long *map, unsigned long i,
					 unsigned long npages)
{
	if (npages >= 8)
		memset((char *)map + i / 8, 0xff, npages / 8);
	else
		for (; npages; ++i, --npages)
			set_bit_le(i, map);
}

static inline u64 sanitize_msr(u64 msr)
{
	msr &= ~MSR_HV;
	msr |= MSR_ME;
	return msr;
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
static inline void copy_from_checkpoint(struct kvm_vcpu *vcpu)
{
	vcpu->arch.regs.ccr  = vcpu->arch.cr_tm;
	vcpu->arch.regs.xer = vcpu->arch.xer_tm;
	vcpu->arch.regs.link  = vcpu->arch.lr_tm;
	vcpu->arch.regs.ctr = vcpu->arch.ctr_tm;
	vcpu->arch.amr = vcpu->arch.amr_tm;
	vcpu->arch.ppr = vcpu->arch.ppr_tm;
	vcpu->arch.dscr = vcpu->arch.dscr_tm;
	vcpu->arch.tar = vcpu->arch.tar_tm;
	memcpy(vcpu->arch.regs.gpr, vcpu->arch.gpr_tm,
	       sizeof(vcpu->arch.regs.gpr));
	vcpu->arch.fp  = vcpu->arch.fp_tm;
	vcpu->arch.vr  = vcpu->arch.vr_tm;
	vcpu->arch.vrsave = vcpu->arch.vrsave_tm;
}

static inline void copy_to_checkpoint(struct kvm_vcpu *vcpu)
{
	vcpu->arch.cr_tm  = vcpu->arch.regs.ccr;
	vcpu->arch.xer_tm = vcpu->arch.regs.xer;
	vcpu->arch.lr_tm  = vcpu->arch.regs.link;
	vcpu->arch.ctr_tm = vcpu->arch.regs.ctr;
	vcpu->arch.amr_tm = vcpu->arch.amr;
	vcpu->arch.ppr_tm = vcpu->arch.ppr;
	vcpu->arch.dscr_tm = vcpu->arch.dscr;
	vcpu->arch.tar_tm = vcpu->arch.tar;
	memcpy(vcpu->arch.gpr_tm, vcpu->arch.regs.gpr,
	       sizeof(vcpu->arch.regs.gpr));
	vcpu->arch.fp_tm  = vcpu->arch.fp;
	vcpu->arch.vr_tm  = vcpu->arch.vr;
	vcpu->arch.vrsave_tm = vcpu->arch.vrsave;
}
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */

extern int kvmppc_create_pte(struct kvm *kvm, pgd_t *pgtable, pte_t pte,
			     unsigned long gpa, unsigned int level,
			     unsigned long mmu_seq, unsigned int lpid,
			     unsigned long *rmapp, struct rmap_nested **n_rmap);
extern void kvmhv_insert_nest_rmap(struct kvm *kvm, unsigned long *rmapp,
				   struct rmap_nested **n_rmap);
extern void kvmhv_update_nest_rmap_rc_list(struct kvm *kvm, unsigned long *rmapp,
					   unsigned long clr, unsigned long set,
					   unsigned long hpa, unsigned long nbytes);
extern void kvmhv_remove_nest_rmap_range(struct kvm *kvm,
				const struct kvm_memory_slot *memslot,
				unsigned long gpa, unsigned long hpa,
				unsigned long nbytes);

static inline pte_t *
find_kvm_secondary_pte_unlocked(struct kvm *kvm, unsigned long ea,
				unsigned *hshift)
{
	pte_t *pte;

	pte = __find_linux_pte(kvm->arch.pgtable, ea, NULL, hshift);
	return pte;
}

static inline pte_t *find_kvm_secondary_pte(struct kvm *kvm, unsigned long ea,
					    unsigned *hshift)
{
	pte_t *pte;

	VM_WARN(!spin_is_locked(&kvm->mmu_lock),
		"%s called with kvm mmu_lock not held \n", __func__);
	pte = __find_linux_pte(kvm->arch.pgtable, ea, NULL, hshift);

	return pte;
}

static inline pte_t *find_kvm_host_pte(struct kvm *kvm, unsigned long mmu_seq,
				       unsigned long ea, unsigned *hshift)
{
	pte_t *pte;

	VM_WARN(!spin_is_locked(&kvm->mmu_lock),
		"%s called with kvm mmu_lock not held \n", __func__);

	if (mmu_notifier_retry(kvm, mmu_seq))
		return NULL;

	pte = __find_linux_pte(kvm->mm->pgd, ea, NULL, hshift);

	return pte;
}

extern pte_t *find_kvm_nested_guest_pte(struct kvm *kvm, unsigned long lpid,
					unsigned long ea, unsigned *hshift);

#endif /* CONFIG_KVM_BOOK3S_HV_POSSIBLE */

#endif /* __ASM_KVM_BOOK3S_64_H__ */
