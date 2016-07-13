/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright SUSE Linux Products GmbH 2010
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#ifndef __ASM_KVM_BOOK3S_64_H__
#define __ASM_KVM_BOOK3S_64_H__

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
#define KVM_DEFAULT_HPT_ORDER	24	/* 16MB HPT by default */
#endif

#define VRMA_VSID	0x1ffffffUL	/* 1TB VSID reserved for VRMA */

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

static inline int __hpte_actual_psize(unsigned int lp, int psize)
{
	int i, shift;
	unsigned int mask;

	/* start from 1 ignoring MMU_PAGE_4K */
	for (i = 1; i < MMU_PAGE_COUNT; i++) {

		/* invalid penc */
		if (mmu_psize_defs[psize].penc[i] == -1)
			continue;
		/*
		 * encoding bits per actual page size
		 *        PTE LP     actual page size
		 *    rrrr rrrz		>=8KB
		 *    rrrr rrzz		>=16KB
		 *    rrrr rzzz		>=32KB
		 *    rrrr zzzz		>=64KB
		 * .......
		 */
		shift = mmu_psize_defs[i].shift - LP_SHIFT;
		if (shift > LP_BITS)
			shift = LP_BITS;
		mask = (1 << shift) - 1;
		if ((lp & mask) == mmu_psize_defs[psize].penc[i])
			return i;
	}
	return -1;
}

static inline unsigned long compute_tlbie_rb(unsigned long v, unsigned long r,
					     unsigned long pte_index)
{
	int b_psize = MMU_PAGE_4K, a_psize = MMU_PAGE_4K;
	unsigned int penc;
	unsigned long rb = 0, va_low, sllp;
	unsigned int lp = (r >> LP_SHIFT) & ((1 << LP_BITS) - 1);

	if (v & HPTE_V_LARGE) {
		for (b_psize = 0; b_psize < MMU_PAGE_COUNT; b_psize++) {

			/* valid entries have a shift value */
			if (!mmu_psize_defs[b_psize].shift)
				continue;

			a_psize = __hpte_actual_psize(lp, b_psize);
			if (a_psize != -1)
				break;
		}
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

	rb |= (v >> HPTE_V_SSIZE_SHIFT) << 8;	/*  B field */
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

	switch (b_psize) {
	case MMU_PAGE_4K:
		sllp = get_sllp_encoding(a_psize);
		rb |= sllp << 5;	/*  AP field */
		rb |= (va_low & 0x7ff) << 12;	/* remaining 11 bits of AVA */
		break;
	default:
	{
		int aval_shift;
		/*
		 * remaining bits of AVA/LP fields
		 * Also contain the rr bits of LP
		 */
		rb |= (va_low << mmu_psize_defs[b_psize].shift) & 0x7ff000;
		/*
		 * Now clear not needed LP bits based on actual psize
		 */
		rb &= ~((1ul << mmu_psize_defs[a_psize].shift) - 1);
		/*
		 * AVAL field 58..77 - base_page_shift bits of va
		 * we have space for 58..64 bits, Missing bits should
		 * be zero filled. +1 is to take care of L bit shift
		 */
		aval_shift = 64 - (77 - mmu_psize_defs[b_psize].shift) + 1;
		rb |= ((va_low << aval_shift) & 0xfe);

		rb |= 1;		/* L field */
		penc = mmu_psize_defs[b_psize].penc[a_psize];
		rb |= penc << 12;	/* LP field */
		break;
	}
	}
	rb |= (v >> 54) & 0x300;		/* B field */
	return rb;
}

static inline unsigned long __hpte_page_size(unsigned long h, unsigned long l,
					     bool is_base_size)
{

	int size, a_psize;
	/* Look at the 8 bit LP value */
	unsigned int lp = (l >> LP_SHIFT) & ((1 << LP_BITS) - 1);

	/* only handle 4k, 64k and 16M pages for now */
	if (!(h & HPTE_V_LARGE))
		return 1ul << 12;
	else {
		for (size = 0; size < MMU_PAGE_COUNT; size++) {
			/* valid entries have a shift value */
			if (!mmu_psize_defs[size].shift)
				continue;

			a_psize = __hpte_actual_psize(lp, size);
			if (a_psize != -1) {
				if (is_base_size)
					return 1ul << mmu_psize_defs[size].shift;
				return 1ul << mmu_psize_defs[a_psize].shift;
			}
		}

	}
	return 0;
}

static inline unsigned long hpte_page_size(unsigned long h, unsigned long l)
{
	return __hpte_page_size(h, l, 0);
}

static inline unsigned long hpte_base_page_size(unsigned long h, unsigned long l)
{
	return __hpte_page_size(h, l, 1);
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
		if (unlikely(!(pte_val(old_pte) & _PAGE_PRESENT)))
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
	return rcu_dereference_raw_notrace(kvm->memslots[0]);
}

extern void kvmppc_mmu_debugfs_init(struct kvm *kvm);

extern void kvmhv_rm_send_ipi(int cpu);

#endif /* CONFIG_KVM_BOOK3S_HV_POSSIBLE */

#endif /* __ASM_KVM_BOOK3S_64_H__ */
