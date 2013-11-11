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

#ifdef CONFIG_KVM_BOOK3S_PR
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

#define SPAPR_TCE_SHIFT		12

#ifdef CONFIG_KVM_BOOK3S_64_HV
#define KVM_DEFAULT_HPT_ORDER	24	/* 16MB HPT by default */
extern int kvm_hpt_order;		/* order of preallocated HPTs */
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

static inline long try_lock_hpte(unsigned long *hpte, unsigned long bits)
{
	unsigned long tmp, old;

	asm volatile("	ldarx	%0,0,%2\n"
		     "	and.	%1,%0,%3\n"
		     "	bne	2f\n"
		     "	ori	%0,%0,%4\n"
		     "  stdcx.	%0,0,%2\n"
		     "	beq+	2f\n"
		     "	mr	%1,%3\n"
		     "2:	isync"
		     : "=&r" (tmp), "=&r" (old)
		     : "r" (hpte), "r" (bits), "i" (HPTE_V_HVLOCK)
		     : "cc", "memory");
	return old == 0;
}

static inline unsigned long compute_tlbie_rb(unsigned long v, unsigned long r,
					     unsigned long pte_index)
{
	unsigned long rb, va_low;

	rb = (v & ~0x7fUL) << 16;		/* AVA field */
	va_low = pte_index >> 3;
	if (v & HPTE_V_SECONDARY)
		va_low = ~va_low;
	/* xor vsid from AVA */
	if (!(v & HPTE_V_1TB_SEG))
		va_low ^= v >> 12;
	else
		va_low ^= v >> 24;
	va_low &= 0x7ff;
	if (v & HPTE_V_LARGE) {
		rb |= 1;			/* L field */
		if (cpu_has_feature(CPU_FTR_ARCH_206) &&
		    (r & 0xff000)) {
			/* non-16MB large page, must be 64k */
			/* (masks depend on page size) */
			rb |= 0x1000;		/* page encoding in LP field */
			rb |= (va_low & 0x7f) << 16; /* 7b of VA in AVA/LP field */
			rb |= (va_low & 0xfe);	/* AVAL field (P7 doesn't seem to care) */
		}
	} else {
		/* 4kB page */
		rb |= (va_low & 0x7ff) << 12;	/* remaining 11b of VA */
	}
	rb |= (v >> 54) & 0x300;		/* B field */
	return rb;
}

static inline unsigned long hpte_page_size(unsigned long h, unsigned long l)
{
	/* only handle 4k, 64k and 16M pages for now */
	if (!(h & HPTE_V_LARGE))
		return 1ul << 12;		/* 4k page */
	if ((l & 0xf000) == 0x1000 && cpu_has_feature(CPU_FTR_ARCH_206))
		return 1ul << 16;		/* 64k page */
	if ((l & 0xff000) == 0)
		return 1ul << 24;		/* 16M page */
	return 0;				/* error */
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

static inline int hpte_cache_flags_ok(unsigned long ptel, unsigned long io_type)
{
	unsigned int wimg = ptel & HPTE_R_WIMG;

	/* Handle SAO */
	if (wimg == (HPTE_R_W | HPTE_R_I | HPTE_R_M) &&
	    cpu_has_feature(CPU_FTR_ARCH_206))
		wimg = HPTE_R_M;

	if (!io_type)
		return wimg == HPTE_R_M;

	return (wimg & (HPTE_R_W | HPTE_R_I)) == io_type;
}

/*
 * Lock and read a linux PTE.  If it's present and writable, atomically
 * set dirty and referenced bits and return the PTE, otherwise return 0.
 */
static inline pte_t kvmppc_read_update_linux_pte(pte_t *p, int writing)
{
	pte_t pte, tmp;

	/* wait until _PAGE_BUSY is clear then set it atomically */
	__asm__ __volatile__ (
		"1:	ldarx	%0,0,%3\n"
		"	andi.	%1,%0,%4\n"
		"	bne-	1b\n"
		"	ori	%1,%0,%4\n"
		"	stdcx.	%1,0,%3\n"
		"	bne-	1b"
		: "=&r" (pte), "=&r" (tmp), "=m" (*p)
		: "r" (p), "i" (_PAGE_BUSY)
		: "cc");

	if (pte_present(pte)) {
		pte = pte_mkyoung(pte);
		if (writing && pte_write(pte))
			pte = pte_mkdirty(pte);
	}

	*p = pte;	/* clears _PAGE_BUSY */

	return pte;
}

/* Return HPTE cache control bits corresponding to Linux pte bits */
static inline unsigned long hpte_cache_bits(unsigned long pte_val)
{
#if _PAGE_NO_CACHE == HPTE_R_I && _PAGE_WRITETHRU == HPTE_R_W
	return pte_val & (HPTE_R_W | HPTE_R_I);
#else
	return ((pte_val & _PAGE_NO_CACHE) ? HPTE_R_I : 0) +
		((pte_val & _PAGE_WRITETHRU) ? HPTE_R_W : 0);
#endif
}

static inline bool hpte_read_permission(unsigned long pp, unsigned long key)
{
	if (key)
		return PP_RWRX <= pp && pp <= PP_RXRX;
	return 1;
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
		return 1;
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

#ifdef CONFIG_KVM_BOOK3S_64_HV
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
#endif /* CONFIG_KVM_BOOK3S_64_HV */

#endif /* __ASM_KVM_BOOK3S_64_H__ */
