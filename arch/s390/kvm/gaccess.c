// SPDX-License-Identifier: GPL-2.0
/*
 * guest access functions
 *
 * Copyright IBM Corp. 2014
 *
 */

#include <linux/vmalloc.h>
#include <linux/mm_types.h>
#include <linux/err.h>
#include <linux/pgtable.h>
#include <linux/bitfield.h>
#include <linux/kvm_host.h>
#include <linux/kvm_types.h>
#include <asm/diag.h>
#include <asm/access-regs.h>
#include <asm/fault.h>
#include <asm/dat-bits.h>
#include "kvm-s390.h"
#include "dat.h"
#include "gmap.h"
#include "gaccess.h"
#include "faultin.h"

#define GMAP_SHADOW_FAKE_TABLE 1ULL

union dat_table_entry {
	unsigned long val;
	union region1_table_entry pgd;
	union region2_table_entry p4d;
	union region3_table_entry pud;
	union segment_table_entry pmd;
	union page_table_entry pte;
};

#define WALK_N_ENTRIES 7
#define LEVEL_MEM -2
struct pgtwalk {
	struct guest_fault raw_entries[WALK_N_ENTRIES];
	gpa_t last_addr;
	int level;
	bool p;
};

static inline struct guest_fault *get_entries(struct pgtwalk *w)
{
	return w->raw_entries - LEVEL_MEM;
}

/*
 * raddress union which will contain the result (real or absolute address)
 * after a page table walk. The rfaa, sfaa and pfra members are used to
 * simply assign them the value of a region, segment or page table entry.
 */
union raddress {
	unsigned long addr;
	unsigned long rfaa : 33; /* Region-Frame Absolute Address */
	unsigned long sfaa : 44; /* Segment-Frame Absolute Address */
	unsigned long pfra : 52; /* Page-Frame Real Address */
};

union alet {
	u32 val;
	struct {
		u32 reserved : 7;
		u32 p        : 1;
		u32 alesn    : 8;
		u32 alen     : 16;
	};
};

union ald {
	u32 val;
	struct {
		u32     : 1;
		u32 alo : 24;
		u32 all : 7;
	};
};

struct ale {
	unsigned long i      : 1; /* ALEN-Invalid Bit */
	unsigned long        : 5;
	unsigned long fo     : 1; /* Fetch-Only Bit */
	unsigned long p      : 1; /* Private Bit */
	unsigned long alesn  : 8; /* Access-List-Entry Sequence Number */
	unsigned long aleax  : 16; /* Access-List-Entry Authorization Index */
	unsigned long        : 32;
	unsigned long        : 1;
	unsigned long asteo  : 25; /* ASN-Second-Table-Entry Origin */
	unsigned long        : 6;
	unsigned long astesn : 32; /* ASTE Sequence Number */
};

struct aste {
	unsigned long i      : 1; /* ASX-Invalid Bit */
	unsigned long ato    : 29; /* Authority-Table Origin */
	unsigned long        : 1;
	unsigned long b      : 1; /* Base-Space Bit */
	unsigned long ax     : 16; /* Authorization Index */
	unsigned long atl    : 12; /* Authority-Table Length */
	unsigned long        : 2;
	unsigned long ca     : 1; /* Controlled-ASN Bit */
	unsigned long ra     : 1; /* Reusable-ASN Bit */
	unsigned long asce   : 64; /* Address-Space-Control Element */
	unsigned long ald    : 32;
	unsigned long astesn : 32;
	/* .. more fields there */
};

union oac {
	unsigned int val;
	struct {
		struct {
			unsigned short key : 4;
			unsigned short     : 4;
			unsigned short as  : 2;
			unsigned short     : 4;
			unsigned short k   : 1;
			unsigned short a   : 1;
		} oac1;
		struct {
			unsigned short key : 4;
			unsigned short     : 4;
			unsigned short as  : 2;
			unsigned short     : 4;
			unsigned short k   : 1;
			unsigned short a   : 1;
		} oac2;
	};
};

int ipte_lock_held(struct kvm *kvm)
{
	if (sclp.has_siif)
		return kvm->arch.sca->ipte_control.kh != 0;

	return kvm->arch.ipte_lock_count != 0;
}

static void ipte_lock_simple(struct kvm *kvm)
{
	union ipte_control old, new, *ic;

	mutex_lock(&kvm->arch.ipte_mutex);
	kvm->arch.ipte_lock_count++;
	if (kvm->arch.ipte_lock_count > 1)
		goto out;
retry:
	ic = &kvm->arch.sca->ipte_control;
	old = READ_ONCE(*ic);
	do {
		if (old.k) {
			cond_resched();
			goto retry;
		}
		new = old;
		new.k = 1;
	} while (!try_cmpxchg(&ic->val, &old.val, new.val));
out:
	mutex_unlock(&kvm->arch.ipte_mutex);
}

static void ipte_unlock_simple(struct kvm *kvm)
{
	union ipte_control old, new, *ic;

	mutex_lock(&kvm->arch.ipte_mutex);
	kvm->arch.ipte_lock_count--;
	if (kvm->arch.ipte_lock_count)
		goto out;
	ic = &kvm->arch.sca->ipte_control;
	old = READ_ONCE(*ic);
	do {
		new = old;
		new.k = 0;
	} while (!try_cmpxchg(&ic->val, &old.val, new.val));
	wake_up(&kvm->arch.ipte_wq);
out:
	mutex_unlock(&kvm->arch.ipte_mutex);
}

static void ipte_lock_siif(struct kvm *kvm)
{
	union ipte_control old, new, *ic;

retry:
	ic = &kvm->arch.sca->ipte_control;
	old = READ_ONCE(*ic);
	do {
		if (old.kg) {
			cond_resched();
			goto retry;
		}
		new = old;
		new.k = 1;
		new.kh++;
	} while (!try_cmpxchg(&ic->val, &old.val, new.val));
}

static void ipte_unlock_siif(struct kvm *kvm)
{
	union ipte_control old, new, *ic;

	ic = &kvm->arch.sca->ipte_control;
	old = READ_ONCE(*ic);
	do {
		new = old;
		new.kh--;
		if (!new.kh)
			new.k = 0;
	} while (!try_cmpxchg(&ic->val, &old.val, new.val));
	if (!new.kh)
		wake_up(&kvm->arch.ipte_wq);
}

void ipte_lock(struct kvm *kvm)
{
	if (sclp.has_siif)
		ipte_lock_siif(kvm);
	else
		ipte_lock_simple(kvm);
}

void ipte_unlock(struct kvm *kvm)
{
	if (sclp.has_siif)
		ipte_unlock_siif(kvm);
	else
		ipte_unlock_simple(kvm);
}

static int ar_translation(struct kvm_vcpu *vcpu, union asce *asce, u8 ar,
			  enum gacc_mode mode)
{
	union alet alet;
	struct ale ale;
	struct aste aste;
	unsigned long ald_addr, authority_table_addr;
	union ald ald;
	int eax, rc;
	u8 authority_table;

	if (ar >= NUM_ACRS)
		return -EINVAL;

	if (vcpu->arch.acrs_loaded)
		save_access_regs(vcpu->run->s.regs.acrs);
	alet.val = vcpu->run->s.regs.acrs[ar];

	if (ar == 0 || alet.val == 0) {
		asce->val = vcpu->arch.sie_block->gcr[1];
		return 0;
	} else if (alet.val == 1) {
		asce->val = vcpu->arch.sie_block->gcr[7];
		return 0;
	}

	if (alet.reserved)
		return PGM_ALET_SPECIFICATION;

	if (alet.p)
		ald_addr = vcpu->arch.sie_block->gcr[5];
	else
		ald_addr = vcpu->arch.sie_block->gcr[2];
	ald_addr &= 0x7fffffc0;

	rc = read_guest_real(vcpu, ald_addr + 16, &ald.val, sizeof(union ald));
	if (rc)
		return rc;

	if (alet.alen / 8 > ald.all)
		return PGM_ALEN_TRANSLATION;

	if (0x7fffffff - ald.alo * 128 < alet.alen * 16)
		return PGM_ADDRESSING;

	rc = read_guest_real(vcpu, ald.alo * 128 + alet.alen * 16, &ale,
			     sizeof(struct ale));
	if (rc)
		return rc;

	if (ale.i == 1)
		return PGM_ALEN_TRANSLATION;
	if (ale.alesn != alet.alesn)
		return PGM_ALE_SEQUENCE;

	rc = read_guest_real(vcpu, ale.asteo * 64, &aste, sizeof(struct aste));
	if (rc)
		return rc;

	if (aste.i)
		return PGM_ASTE_VALIDITY;
	if (aste.astesn != ale.astesn)
		return PGM_ASTE_SEQUENCE;

	if (ale.p == 1) {
		eax = (vcpu->arch.sie_block->gcr[8] >> 16) & 0xffff;
		if (ale.aleax != eax) {
			if (eax / 16 > aste.atl)
				return PGM_EXTENDED_AUTHORITY;

			authority_table_addr = aste.ato * 4 + eax / 4;

			rc = read_guest_real(vcpu, authority_table_addr,
					     &authority_table,
					     sizeof(u8));
			if (rc)
				return rc;

			if ((authority_table & (0x40 >> ((eax & 3) * 2))) == 0)
				return PGM_EXTENDED_AUTHORITY;
		}
	}

	if (ale.fo == 1 && mode == GACC_STORE)
		return PGM_PROTECTION;

	asce->val = aste.asce;
	return 0;
}

enum prot_type {
	PROT_TYPE_LA   = 0,
	PROT_TYPE_KEYC = 1,
	PROT_TYPE_ALC  = 2,
	PROT_TYPE_DAT  = 3,
	PROT_TYPE_IEP  = 4,
	/* Dummy value for passing an initialized value when code != PGM_PROTECTION */
	PROT_TYPE_DUMMY,
};

static int trans_exc_ending(struct kvm_vcpu *vcpu, int code, unsigned long gva, u8 ar,
			    enum gacc_mode mode, enum prot_type prot, bool terminate)
{
	struct kvm_s390_pgm_info *pgm = &vcpu->arch.pgm;
	union teid *teid;

	memset(pgm, 0, sizeof(*pgm));
	pgm->code = code;
	teid = (union teid *)&pgm->trans_exc_code;

	switch (code) {
	case PGM_PROTECTION:
		switch (prot) {
		case PROT_TYPE_DUMMY:
			/* We should never get here, acts like termination */
			WARN_ON_ONCE(1);
			break;
		case PROT_TYPE_IEP:
			teid->b61 = 1;
			fallthrough;
		case PROT_TYPE_LA:
			teid->b56 = 1;
			break;
		case PROT_TYPE_KEYC:
			teid->b60 = 1;
			break;
		case PROT_TYPE_ALC:
			teid->b60 = 1;
			fallthrough;
		case PROT_TYPE_DAT:
			teid->b61 = 1;
			break;
		}
		if (terminate) {
			teid->b56 = 0;
			teid->b60 = 0;
			teid->b61 = 0;
		}
		fallthrough;
	case PGM_ASCE_TYPE:
	case PGM_PAGE_TRANSLATION:
	case PGM_REGION_FIRST_TRANS:
	case PGM_REGION_SECOND_TRANS:
	case PGM_REGION_THIRD_TRANS:
	case PGM_SEGMENT_TRANSLATION:
		/*
		 * op_access_id only applies to MOVE_PAGE -> set bit 61
		 * exc_access_id has to be set to 0 for some instructions. Both
		 * cases have to be handled by the caller.
		 */
		teid->addr = gva >> PAGE_SHIFT;
		teid->fsi = mode == GACC_STORE ? TEID_FSI_STORE : TEID_FSI_FETCH;
		teid->as = psw_bits(vcpu->arch.sie_block->gpsw).as;
		fallthrough;
	case PGM_ALEN_TRANSLATION:
	case PGM_ALE_SEQUENCE:
	case PGM_ASTE_VALIDITY:
	case PGM_ASTE_SEQUENCE:
	case PGM_EXTENDED_AUTHORITY:
		/*
		 * We can always store exc_access_id, as it is
		 * undefined for non-ar cases. It is undefined for
		 * most DAT protection exceptions.
		 */
		pgm->exc_access_id = ar;
		break;
	}
	return code;
}

static int trans_exc(struct kvm_vcpu *vcpu, int code, unsigned long gva, u8 ar,
		     enum gacc_mode mode, enum prot_type prot)
{
	return trans_exc_ending(vcpu, code, gva, ar, mode, prot, false);
}

static int get_vcpu_asce(struct kvm_vcpu *vcpu, union asce *asce,
			 unsigned long ga, u8 ar, enum gacc_mode mode)
{
	int rc;
	struct psw_bits psw = psw_bits(vcpu->arch.sie_block->gpsw);

	if (!psw.dat) {
		asce->val = 0;
		asce->r = 1;
		return 0;
	}

	if ((mode == GACC_IFETCH) && (psw.as != PSW_BITS_AS_HOME))
		psw.as = PSW_BITS_AS_PRIMARY;

	switch (psw.as) {
	case PSW_BITS_AS_PRIMARY:
		asce->val = vcpu->arch.sie_block->gcr[1];
		return 0;
	case PSW_BITS_AS_SECONDARY:
		asce->val = vcpu->arch.sie_block->gcr[7];
		return 0;
	case PSW_BITS_AS_HOME:
		asce->val = vcpu->arch.sie_block->gcr[13];
		return 0;
	case PSW_BITS_AS_ACCREG:
		rc = ar_translation(vcpu, asce, ar, mode);
		if (rc > 0)
			return trans_exc(vcpu, rc, ga, ar, mode, PROT_TYPE_ALC);
		return rc;
	}
	return 0;
}

static int deref_table(struct kvm *kvm, unsigned long gpa, unsigned long *val)
{
	return kvm_read_guest(kvm, gpa, val, sizeof(*val));
}

/**
 * guest_translate_gva() - translate a guest virtual into a guest absolute address
 * @vcpu: virtual cpu
 * @gva: guest virtual address
 * @gpa: points to where guest physical (absolute) address should be stored
 * @asce: effective asce
 * @mode: indicates the access mode to be used
 * @prot: returns the type for protection exceptions
 *
 * Translate a guest virtual address into a guest absolute address by means
 * of dynamic address translation as specified by the architecture.
 * If the resulting absolute address is not available in the configuration
 * an addressing exception is indicated and @gpa will not be changed.
 *
 * Returns: - zero on success; @gpa contains the resulting absolute address
 *	    - a negative value if guest access failed due to e.g. broken
 *	      guest mapping
 *	    - a positive value if an access exception happened. In this case
 *	      the returned value is the program interruption code as defined
 *	      by the architecture
 */
static unsigned long guest_translate_gva(struct kvm_vcpu *vcpu, unsigned long gva,
					 unsigned long *gpa, const union asce asce,
					 enum gacc_mode mode, enum prot_type *prot)
{
	union vaddress vaddr = {.addr = gva};
	union raddress raddr = {.addr = gva};
	union page_table_entry pte;
	int dat_protection = 0;
	int iep_protection = 0;
	union ctlreg0 ctlreg0;
	unsigned long ptr;
	int edat1, edat2, iep;

	ctlreg0.val = vcpu->arch.sie_block->gcr[0];
	edat1 = ctlreg0.edat && test_kvm_facility(vcpu->kvm, 8);
	edat2 = edat1 && test_kvm_facility(vcpu->kvm, 78);
	iep = ctlreg0.iep && test_kvm_facility(vcpu->kvm, 130);
	if (asce.r)
		goto real_address;
	ptr = asce.rsto * PAGE_SIZE;
	switch (asce.dt) {
	case ASCE_TYPE_REGION1:
		if (vaddr.rfx01 > asce.tl)
			return PGM_REGION_FIRST_TRANS;
		ptr += vaddr.rfx * 8;
		break;
	case ASCE_TYPE_REGION2:
		if (vaddr.rfx)
			return PGM_ASCE_TYPE;
		if (vaddr.rsx01 > asce.tl)
			return PGM_REGION_SECOND_TRANS;
		ptr += vaddr.rsx * 8;
		break;
	case ASCE_TYPE_REGION3:
		if (vaddr.rfx || vaddr.rsx)
			return PGM_ASCE_TYPE;
		if (vaddr.rtx01 > asce.tl)
			return PGM_REGION_THIRD_TRANS;
		ptr += vaddr.rtx * 8;
		break;
	case ASCE_TYPE_SEGMENT:
		if (vaddr.rfx || vaddr.rsx || vaddr.rtx)
			return PGM_ASCE_TYPE;
		if (vaddr.sx01 > asce.tl)
			return PGM_SEGMENT_TRANSLATION;
		ptr += vaddr.sx * 8;
		break;
	}
	switch (asce.dt) {
	case ASCE_TYPE_REGION1:	{
		union region1_table_entry rfte;

		if (!kvm_is_gpa_in_memslot(vcpu->kvm, ptr))
			return PGM_ADDRESSING;
		if (deref_table(vcpu->kvm, ptr, &rfte.val))
			return -EFAULT;
		if (rfte.i)
			return PGM_REGION_FIRST_TRANS;
		if (rfte.tt != TABLE_TYPE_REGION1)
			return PGM_TRANSLATION_SPEC;
		if (vaddr.rsx01 < rfte.tf || vaddr.rsx01 > rfte.tl)
			return PGM_REGION_SECOND_TRANS;
		if (edat1)
			dat_protection |= rfte.p;
		ptr = rfte.rto * PAGE_SIZE + vaddr.rsx * 8;
	}
		fallthrough;
	case ASCE_TYPE_REGION2: {
		union region2_table_entry rste;

		if (!kvm_is_gpa_in_memslot(vcpu->kvm, ptr))
			return PGM_ADDRESSING;
		if (deref_table(vcpu->kvm, ptr, &rste.val))
			return -EFAULT;
		if (rste.i)
			return PGM_REGION_SECOND_TRANS;
		if (rste.tt != TABLE_TYPE_REGION2)
			return PGM_TRANSLATION_SPEC;
		if (vaddr.rtx01 < rste.tf || vaddr.rtx01 > rste.tl)
			return PGM_REGION_THIRD_TRANS;
		if (edat1)
			dat_protection |= rste.p;
		ptr = rste.rto * PAGE_SIZE + vaddr.rtx * 8;
	}
		fallthrough;
	case ASCE_TYPE_REGION3: {
		union region3_table_entry rtte;

		if (!kvm_is_gpa_in_memslot(vcpu->kvm, ptr))
			return PGM_ADDRESSING;
		if (deref_table(vcpu->kvm, ptr, &rtte.val))
			return -EFAULT;
		if (rtte.i)
			return PGM_REGION_THIRD_TRANS;
		if (rtte.tt != TABLE_TYPE_REGION3)
			return PGM_TRANSLATION_SPEC;
		if (rtte.cr && asce.p && edat2)
			return PGM_TRANSLATION_SPEC;
		if (rtte.fc && edat2) {
			dat_protection |= rtte.fc1.p;
			iep_protection = rtte.fc1.iep;
			raddr.rfaa = rtte.fc1.rfaa;
			goto absolute_address;
		}
		if (vaddr.sx01 < rtte.fc0.tf)
			return PGM_SEGMENT_TRANSLATION;
		if (vaddr.sx01 > rtte.fc0.tl)
			return PGM_SEGMENT_TRANSLATION;
		if (edat1)
			dat_protection |= rtte.fc0.p;
		ptr = rtte.fc0.sto * PAGE_SIZE + vaddr.sx * 8;
	}
		fallthrough;
	case ASCE_TYPE_SEGMENT: {
		union segment_table_entry ste;

		if (!kvm_is_gpa_in_memslot(vcpu->kvm, ptr))
			return PGM_ADDRESSING;
		if (deref_table(vcpu->kvm, ptr, &ste.val))
			return -EFAULT;
		if (ste.i)
			return PGM_SEGMENT_TRANSLATION;
		if (ste.tt != TABLE_TYPE_SEGMENT)
			return PGM_TRANSLATION_SPEC;
		if (ste.cs && asce.p)
			return PGM_TRANSLATION_SPEC;
		if (ste.fc && edat1) {
			dat_protection |= ste.fc1.p;
			iep_protection = ste.fc1.iep;
			raddr.sfaa = ste.fc1.sfaa;
			goto absolute_address;
		}
		dat_protection |= ste.fc0.p;
		ptr = ste.fc0.pto * (PAGE_SIZE / 2) + vaddr.px * 8;
	}
	}
	if (!kvm_is_gpa_in_memslot(vcpu->kvm, ptr))
		return PGM_ADDRESSING;
	if (deref_table(vcpu->kvm, ptr, &pte.val))
		return -EFAULT;
	if (pte.i)
		return PGM_PAGE_TRANSLATION;
	if (pte.z)
		return PGM_TRANSLATION_SPEC;
	dat_protection |= pte.p;
	iep_protection = pte.iep;
	raddr.pfra = pte.pfra;
real_address:
	raddr.addr = kvm_s390_real_to_abs(vcpu, raddr.addr);
absolute_address:
	if (mode == GACC_STORE && dat_protection) {
		*prot = PROT_TYPE_DAT;
		return PGM_PROTECTION;
	}
	if (mode == GACC_IFETCH && iep_protection && iep) {
		*prot = PROT_TYPE_IEP;
		return PGM_PROTECTION;
	}
	if (!kvm_is_gpa_in_memslot(vcpu->kvm, raddr.addr))
		return PGM_ADDRESSING;
	*gpa = raddr.addr;
	return 0;
}

static inline int is_low_address(unsigned long ga)
{
	/* Check for address ranges 0..511 and 4096..4607 */
	return (ga & ~0x11fful) == 0;
}

static int low_address_protection_enabled(struct kvm_vcpu *vcpu,
					  const union asce asce)
{
	union ctlreg0 ctlreg0 = {.val = vcpu->arch.sie_block->gcr[0]};
	psw_t *psw = &vcpu->arch.sie_block->gpsw;

	if (!ctlreg0.lap)
		return 0;
	if (psw_bits(*psw).dat && asce.p)
		return 0;
	return 1;
}

static int vm_check_access_key_gpa(struct kvm *kvm, u8 access_key,
				   enum gacc_mode mode, gpa_t gpa)
{
	union skey storage_key;
	int r;

	scoped_guard(read_lock, &kvm->mmu_lock)
		r = dat_get_storage_key(kvm->arch.gmap->asce, gpa_to_gfn(gpa), &storage_key);
	if (r)
		return r;
	if (access_key == 0 || storage_key.acc == access_key)
		return 0;
	if ((mode == GACC_FETCH || mode == GACC_IFETCH) && !storage_key.fp)
		return 0;
	return PGM_PROTECTION;
}

static bool fetch_prot_override_applicable(struct kvm_vcpu *vcpu, enum gacc_mode mode,
					   union asce asce)
{
	psw_t *psw = &vcpu->arch.sie_block->gpsw;
	unsigned long override;

	if (mode == GACC_FETCH || mode == GACC_IFETCH) {
		/* check if fetch protection override enabled */
		override = vcpu->arch.sie_block->gcr[0];
		override &= CR0_FETCH_PROTECTION_OVERRIDE;
		/* not applicable if subject to DAT && private space */
		override = override && !(psw_bits(*psw).dat && asce.p);
		return override;
	}
	return false;
}

static bool fetch_prot_override_applies(unsigned long ga, unsigned int len)
{
	return ga < 2048 && ga + len <= 2048;
}

static bool storage_prot_override_applicable(struct kvm_vcpu *vcpu)
{
	/* check if storage protection override enabled */
	return vcpu->arch.sie_block->gcr[0] & CR0_STORAGE_PROTECTION_OVERRIDE;
}

static bool storage_prot_override_applies(u8 access_control)
{
	/* matches special storage protection override key (9) -> allow */
	return access_control == PAGE_SPO_ACC;
}

static int vcpu_check_access_key_gpa(struct kvm_vcpu *vcpu, u8 access_key,
				     enum gacc_mode mode, union asce asce, gpa_t gpa,
				     unsigned long ga, unsigned int len)
{
	union skey storage_key;
	int r;

	/* access key 0 matches any storage key -> allow */
	if (access_key == 0)
		return 0;
	/*
	 * caller needs to ensure that gfn is accessible, so we can
	 * assume that this cannot fail
	 */
	scoped_guard(read_lock, &vcpu->kvm->mmu_lock)
		r = dat_get_storage_key(vcpu->arch.gmap->asce, gpa_to_gfn(gpa), &storage_key);
	if (r)
		return r;
	/* access key matches storage key -> allow */
	if (storage_key.acc == access_key)
		return 0;
	if (mode == GACC_FETCH || mode == GACC_IFETCH) {
		/* it is a fetch and fetch protection is off -> allow */
		if (!storage_key.fp)
			return 0;
		if (fetch_prot_override_applicable(vcpu, mode, asce) &&
		    fetch_prot_override_applies(ga, len))
			return 0;
	}
	if (storage_prot_override_applicable(vcpu) &&
	    storage_prot_override_applies(storage_key.acc))
		return 0;
	return PGM_PROTECTION;
}

/**
 * guest_range_to_gpas() - Calculate guest physical addresses of page fragments
 * covering a logical range
 * @vcpu: virtual cpu
 * @ga: guest address, start of range
 * @ar: access register
 * @gpas: output argument, may be NULL
 * @len: length of range in bytes
 * @asce: address-space-control element to use for translation
 * @mode: access mode
 * @access_key: access key to mach the range's storage keys against
 *
 * Translate a logical range to a series of guest absolute addresses,
 * such that the concatenation of page fragments starting at each gpa make up
 * the whole range.
 * The translation is performed as if done by the cpu for the given @asce, @ar,
 * @mode and state of the @vcpu.
 * If the translation causes an exception, its program interruption code is
 * returned and the &struct kvm_s390_pgm_info pgm member of @vcpu is modified
 * such that a subsequent call to kvm_s390_inject_prog_vcpu() will inject
 * a correct exception into the guest.
 * The resulting gpas are stored into @gpas, unless it is NULL.
 *
 * Note: All fragments except the first one start at the beginning of a page.
 *	 When deriving the boundaries of a fragment from a gpa, all but the last
 *	 fragment end at the end of the page.
 *
 * Return:
 * * 0		- success
 * * <0		- translation could not be performed, for example if  guest
 *		  memory could not be accessed
 * * >0		- an access exception occurred. In this case the returned value
 *		  is the program interruption code and the contents of pgm may
 *		  be used to inject an exception into the guest.
 */
static int guest_range_to_gpas(struct kvm_vcpu *vcpu, unsigned long ga, u8 ar,
			       unsigned long *gpas, unsigned long len,
			       const union asce asce, enum gacc_mode mode,
			       u8 access_key)
{
	psw_t *psw = &vcpu->arch.sie_block->gpsw;
	unsigned int offset = offset_in_page(ga);
	unsigned int fragment_len;
	int lap_enabled, rc = 0;
	enum prot_type prot;
	unsigned long gpa;

	lap_enabled = low_address_protection_enabled(vcpu, asce);
	while (min(PAGE_SIZE - offset, len) > 0) {
		fragment_len = min(PAGE_SIZE - offset, len);
		ga = kvm_s390_logical_to_effective(vcpu, ga);
		if (mode == GACC_STORE && lap_enabled && is_low_address(ga))
			return trans_exc(vcpu, PGM_PROTECTION, ga, ar, mode,
					 PROT_TYPE_LA);
		if (psw_bits(*psw).dat) {
			rc = guest_translate_gva(vcpu, ga, &gpa, asce, mode, &prot);
			if (rc < 0)
				return rc;
		} else {
			gpa = kvm_s390_real_to_abs(vcpu, ga);
			if (!kvm_is_gpa_in_memslot(vcpu->kvm, gpa)) {
				rc = PGM_ADDRESSING;
				prot = PROT_TYPE_DUMMY;
			}
		}
		if (rc)
			return trans_exc(vcpu, rc, ga, ar, mode, prot);
		rc = vcpu_check_access_key_gpa(vcpu, access_key, mode, asce, gpa, ga, fragment_len);
		if (rc)
			return trans_exc(vcpu, rc, ga, ar, mode, PROT_TYPE_KEYC);
		if (gpas)
			*gpas++ = gpa;
		offset = 0;
		ga += fragment_len;
		len -= fragment_len;
	}
	return 0;
}

static int access_guest_page_gpa(struct kvm *kvm, enum gacc_mode mode, gpa_t gpa,
				 void *data, unsigned int len)
{
	const unsigned int offset = offset_in_page(gpa);
	const gfn_t gfn = gpa_to_gfn(gpa);
	int rc;

	if (!gfn_to_memslot(kvm, gfn))
		return PGM_ADDRESSING;
	if (mode == GACC_STORE)
		rc = kvm_write_guest_page(kvm, gfn, data, offset, len);
	else
		rc = kvm_read_guest_page(kvm, gfn, data, offset, len);
	return rc;
}

static int mvcos_key(void *to, const void *from, unsigned long size, u8 dst_key, u8 src_key)
{
	union oac spec = {
		.oac1.key = dst_key,
		.oac1.k = !!dst_key,
		.oac2.key = src_key,
		.oac2.k = !!src_key,
	};
	int exception = PGM_PROTECTION;

	asm_inline volatile(
		"       lr      %%r0,%[spec]\n"
		"0:     mvcos   %[to],%[from],%[size]\n"
		"1:     lhi     %[exc],0\n"
		"2:\n"
		EX_TABLE(0b, 2b)
		EX_TABLE(1b, 2b)
		: [size] "+d" (size), [to] "=Q" (*(char *)to), [exc] "+d" (exception)
		: [spec] "d" (spec.val), [from] "Q" (*(const char *)from)
		: "memory", "cc", "0");
	return exception;
}

struct acc_page_key_context {
	void *data;
	int exception;
	unsigned short offset;
	unsigned short len;
	bool store;
	u8 access_key;
};

static void _access_guest_page_with_key_gpa(struct guest_fault *f)
{
	struct acc_page_key_context *context = f->priv;
	void *ptr;
	int r;

	ptr = __va(PFN_PHYS(f->pfn) | context->offset);

	if (context->store)
		r = mvcos_key(ptr, context->data, context->len, context->access_key, 0);
	else
		r = mvcos_key(context->data, ptr, context->len, 0, context->access_key);

	context->exception = r;
}

static int access_guest_page_with_key_gpa(struct kvm *kvm, enum gacc_mode mode, gpa_t gpa,
					  void *data, unsigned int len, u8 acc)
{
	struct acc_page_key_context context = {
		.offset = offset_in_page(gpa),
		.len = len,
		.data = data,
		.access_key = acc,
		.store = mode == GACC_STORE,
	};
	struct guest_fault fault = {
		.gfn = gpa_to_gfn(gpa),
		.priv = &context,
		.write_attempt = mode == GACC_STORE,
		.callback = _access_guest_page_with_key_gpa,
	};
	int rc;

	if (KVM_BUG_ON((len + context.offset) > PAGE_SIZE, kvm))
		return -EINVAL;

	rc = kvm_s390_faultin_gfn(NULL, kvm, &fault);
	if (rc)
		return rc;
	return context.exception;
}

int access_guest_abs_with_key(struct kvm *kvm, gpa_t gpa, void *data,
			      unsigned long len, enum gacc_mode mode, u8 access_key)
{
	int offset = offset_in_page(gpa);
	int fragment_len;
	int rc;

	while (min(PAGE_SIZE - offset, len) > 0) {
		fragment_len = min(PAGE_SIZE - offset, len);
		rc = access_guest_page_with_key_gpa(kvm, mode, gpa, data, fragment_len, access_key);
		if (rc)
			return rc;
		offset = 0;
		len -= fragment_len;
		data += fragment_len;
		gpa += fragment_len;
	}
	return 0;
}

int access_guest_with_key(struct kvm_vcpu *vcpu, unsigned long ga, u8 ar,
			  void *data, unsigned long len, enum gacc_mode mode,
			  u8 access_key)
{
	psw_t *psw = &vcpu->arch.sie_block->gpsw;
	unsigned long nr_pages, idx;
	unsigned long gpa_array[2];
	unsigned int fragment_len;
	unsigned long *gpas;
	enum prot_type prot;
	int need_ipte_lock;
	union asce asce;
	bool try_storage_prot_override;
	bool try_fetch_prot_override;
	int rc;

	if (!len)
		return 0;
	ga = kvm_s390_logical_to_effective(vcpu, ga);
	rc = get_vcpu_asce(vcpu, &asce, ga, ar, mode);
	if (rc)
		return rc;
	nr_pages = (((ga & ~PAGE_MASK) + len - 1) >> PAGE_SHIFT) + 1;
	gpas = gpa_array;
	if (nr_pages > ARRAY_SIZE(gpa_array))
		gpas = vmalloc(array_size(nr_pages, sizeof(unsigned long)));
	if (!gpas)
		return -ENOMEM;
	try_fetch_prot_override = fetch_prot_override_applicable(vcpu, mode, asce);
	try_storage_prot_override = storage_prot_override_applicable(vcpu);
	need_ipte_lock = psw_bits(*psw).dat && !asce.r;
	if (need_ipte_lock)
		ipte_lock(vcpu->kvm);
	/*
	 * Since we do the access further down ultimately via a move instruction
	 * that does key checking and returns an error in case of a protection
	 * violation, we don't need to do the check during address translation.
	 * Skip it by passing access key 0, which matches any storage key,
	 * obviating the need for any further checks. As a result the check is
	 * handled entirely in hardware on access, we only need to take care to
	 * forego key protection checking if fetch protection override applies or
	 * retry with the special key 9 in case of storage protection override.
	 */
	rc = guest_range_to_gpas(vcpu, ga, ar, gpas, len, asce, mode, 0);
	if (rc)
		goto out_unlock;
	for (idx = 0; idx < nr_pages; idx++) {
		fragment_len = min(PAGE_SIZE - offset_in_page(gpas[idx]), len);
		if (try_fetch_prot_override && fetch_prot_override_applies(ga, fragment_len)) {
			rc = access_guest_page_gpa(vcpu->kvm, mode, gpas[idx], data, fragment_len);
		} else {
			rc = access_guest_page_with_key_gpa(vcpu->kvm, mode, gpas[idx],
							    data, fragment_len, access_key);
		}
		if (rc == PGM_PROTECTION && try_storage_prot_override)
			rc = access_guest_page_with_key_gpa(vcpu->kvm, mode, gpas[idx],
							    data, fragment_len, PAGE_SPO_ACC);
		if (rc)
			break;
		len -= fragment_len;
		data += fragment_len;
		ga = kvm_s390_logical_to_effective(vcpu, ga + fragment_len);
	}
	if (rc > 0) {
		bool terminate = (mode == GACC_STORE) && (idx > 0);

		if (rc == PGM_PROTECTION)
			prot = PROT_TYPE_KEYC;
		else
			prot = PROT_TYPE_DUMMY;
		rc = trans_exc_ending(vcpu, rc, ga, ar, mode, prot, terminate);
	}
out_unlock:
	if (need_ipte_lock)
		ipte_unlock(vcpu->kvm);
	if (nr_pages > ARRAY_SIZE(gpa_array))
		vfree(gpas);
	return rc;
}

int access_guest_real(struct kvm_vcpu *vcpu, unsigned long gra,
		      void *data, unsigned long len, enum gacc_mode mode)
{
	unsigned int fragment_len;
	unsigned long gpa;
	int rc = 0;

	while (len && !rc) {
		gpa = kvm_s390_real_to_abs(vcpu, gra);
		fragment_len = min(PAGE_SIZE - offset_in_page(gpa), len);
		rc = access_guest_page_gpa(vcpu->kvm, mode, gpa, data, fragment_len);
		len -= fragment_len;
		gra += fragment_len;
		data += fragment_len;
	}
	if (rc > 0)
		vcpu->arch.pgm.code = rc;
	return rc;
}

/**
 * __cmpxchg_with_key() - Perform cmpxchg, honoring storage keys.
 * @ptr: Address of value to compare to *@old and exchange with
 *       @new. Must be aligned to @size.
 * @old: Old value. Compared to the content pointed to by @ptr in order to
 *       determine if the exchange occurs. The old value read from *@ptr is
 *       written here.
 * @new: New value to place at *@ptr.
 * @size: Size of the operation in bytes, may only be a power of two up to 16.
 * @access_key: Access key to use for checking storage key protection.
 *
 * Perform a cmpxchg on guest memory, honoring storage key protection.
 * @access_key alone determines how key checking is performed, neither
 * storage-protection-override nor fetch-protection-override apply.
 * In case of an exception *@uval is set to zero.
 *
 * Return:
 * * %0: cmpxchg executed successfully
 * * %1: cmpxchg executed unsuccessfully
 * * %PGM_PROTECTION: an exception happened when trying to access *@ptr
 * * %-EAGAIN: maxed out number of retries (byte and short only)
 * * %-EINVAL: invalid value for @size
 */
static int __cmpxchg_with_key(union kvm_s390_quad *ptr, union kvm_s390_quad *old,
			      union kvm_s390_quad new, int size, u8 access_key)
{
	union kvm_s390_quad tmp = { .sixteen = 0 };
	int rc;

	/*
	 * The cmpxchg_key macro depends on the type of "old", so we need
	 * a case for each valid length and get some code duplication as long
	 * as we don't introduce a new macro.
	 */
	switch (size) {
	case 1:
		rc = __cmpxchg_key1(&ptr->one, &tmp.one, old->one, new.one, access_key);
		break;
	case 2:
		rc = __cmpxchg_key2(&ptr->two, &tmp.two, old->two, new.two, access_key);
		break;
	case 4:
		rc = __cmpxchg_key4(&ptr->four, &tmp.four, old->four, new.four, access_key);
		break;
	case 8:
		rc = __cmpxchg_key8(&ptr->eight, &tmp.eight, old->eight, new.eight, access_key);
		break;
	case 16:
		rc = __cmpxchg_key16(&ptr->sixteen, &tmp.sixteen, old->sixteen, new.sixteen,
				     access_key);
		break;
	default:
		return -EINVAL;
	}
	if (!rc && memcmp(&tmp, old, size))
		rc = 1;
	*old = tmp;
	/*
	 * Assume that the fault is caused by protection, either key protection
	 * or user page write protection.
	 */
	if (rc == -EFAULT)
		rc = PGM_PROTECTION;
	return rc;
}

struct cmpxchg_key_context {
	union kvm_s390_quad new;
	union kvm_s390_quad *old;
	int exception;
	unsigned short offset;
	u8 access_key;
	u8 len;
};

static void _cmpxchg_guest_abs_with_key(struct guest_fault *f)
{
	struct cmpxchg_key_context *context = f->priv;

	context->exception = __cmpxchg_with_key(__va(PFN_PHYS(f->pfn) | context->offset),
						context->old, context->new, context->len,
						context->access_key);
}

/**
 * cmpxchg_guest_abs_with_key() - Perform cmpxchg on guest absolute address.
 * @kvm: Virtual machine instance.
 * @gpa: Absolute guest address of the location to be changed.
 * @len: Operand length of the cmpxchg, required: 1 <= len <= 16. Providing a
 *       non power of two will result in failure.
 * @old: Pointer to old value. If the location at @gpa contains this value,
 *       the exchange will succeed. After calling cmpxchg_guest_abs_with_key()
 *       *@old contains the value at @gpa before the attempt to
 *       exchange the value.
 * @new: The value to place at @gpa.
 * @acc: The access key to use for the guest access.
 * @success: output value indicating if an exchange occurred.
 *
 * Atomically exchange the value at @gpa by @new, if it contains *@old.
 * Honors storage keys.
 *
 * Return: * 0: successful exchange
 *         * >0: a program interruption code indicating the reason cmpxchg could
 *               not be attempted
 *         * -EINVAL: address misaligned or len not power of two
 *         * -EAGAIN: transient failure (len 1 or 2)
 *         * -EOPNOTSUPP: read-only memslot (should never occur)
 */
int cmpxchg_guest_abs_with_key(struct kvm *kvm, gpa_t gpa, int len, union kvm_s390_quad *old,
			       union kvm_s390_quad new, u8 acc, bool *success)
{
	struct cmpxchg_key_context context = {
		.old = old,
		.new = new,
		.offset = offset_in_page(gpa),
		.len = len,
		.access_key = acc,
	};
	struct guest_fault fault = {
		.gfn = gpa_to_gfn(gpa),
		.priv = &context,
		.write_attempt = true,
		.callback = _cmpxchg_guest_abs_with_key,
	};
	int rc;

	lockdep_assert_held(&kvm->srcu);

	if (len > 16 || !IS_ALIGNED(gpa, len))
		return -EINVAL;

	rc = kvm_s390_faultin_gfn(NULL, kvm, &fault);
	if (rc)
		return rc;
	*success = !context.exception;
	if (context.exception == 1)
		return 0;
	return context.exception;
}

/**
 * guest_translate_address_with_key - translate guest logical into guest absolute address
 * @vcpu: virtual cpu
 * @gva: Guest virtual address
 * @ar: Access register
 * @gpa: Guest physical address
 * @mode: Translation access mode
 * @access_key: access key to mach the storage key with
 *
 * Parameter semantics are the same as the ones from guest_translate.
 * The memory contents at the guest address are not changed.
 *
 * Note: The IPTE lock is not taken during this function, so the caller
 * has to take care of this.
 */
int guest_translate_address_with_key(struct kvm_vcpu *vcpu, unsigned long gva, u8 ar,
				     unsigned long *gpa, enum gacc_mode mode,
				     u8 access_key)
{
	union asce asce;
	int rc;

	gva = kvm_s390_logical_to_effective(vcpu, gva);
	rc = get_vcpu_asce(vcpu, &asce, gva, ar, mode);
	if (rc)
		return rc;
	return guest_range_to_gpas(vcpu, gva, ar, gpa, 1, asce, mode,
				   access_key);
}

/**
 * check_gva_range - test a range of guest virtual addresses for accessibility
 * @vcpu: virtual cpu
 * @gva: Guest virtual address
 * @ar: Access register
 * @length: Length of test range
 * @mode: Translation access mode
 * @access_key: access key to mach the storage keys with
 */
int check_gva_range(struct kvm_vcpu *vcpu, unsigned long gva, u8 ar,
		    unsigned long length, enum gacc_mode mode, u8 access_key)
{
	union asce asce;
	int rc = 0;

	rc = get_vcpu_asce(vcpu, &asce, gva, ar, mode);
	if (rc)
		return rc;
	ipte_lock(vcpu->kvm);
	rc = guest_range_to_gpas(vcpu, gva, ar, NULL, length, asce, mode,
				 access_key);
	ipte_unlock(vcpu->kvm);

	return rc;
}

/**
 * check_gpa_range - test a range of guest physical addresses for accessibility
 * @kvm: virtual machine instance
 * @gpa: guest physical address
 * @length: length of test range
 * @mode: access mode to test, relevant for storage keys
 * @access_key: access key to mach the storage keys with
 */
int check_gpa_range(struct kvm *kvm, unsigned long gpa, unsigned long length,
		    enum gacc_mode mode, u8 access_key)
{
	unsigned int fragment_len;
	int rc = 0;

	while (length && !rc) {
		fragment_len = min(PAGE_SIZE - offset_in_page(gpa), length);
		rc = vm_check_access_key_gpa(kvm, access_key, mode, gpa);
		length -= fragment_len;
		gpa += fragment_len;
	}
	return rc;
}

/**
 * kvm_s390_check_low_addr_prot_real - check for low-address protection
 * @vcpu: virtual cpu
 * @gra: Guest real address
 *
 * Checks whether an address is subject to low-address protection and set
 * up vcpu->arch.pgm accordingly if necessary.
 *
 * Return: 0 if no protection exception, or PGM_PROTECTION if protected.
 */
int kvm_s390_check_low_addr_prot_real(struct kvm_vcpu *vcpu, unsigned long gra)
{
	union ctlreg0 ctlreg0 = {.val = vcpu->arch.sie_block->gcr[0]};

	if (!ctlreg0.lap || !is_low_address(gra))
		return 0;
	return trans_exc(vcpu, PGM_PROTECTION, gra, 0, GACC_STORE, PROT_TYPE_LA);
}

/**
 * walk_guest_tables() - Walk the guest page table and pin the dat tables.
 * @sg: Pointer to the shadow guest address space structure.
 * @saddr: Faulting address in the shadow gmap.
 * @w: Will be filled with information on the pinned pages.
 * @wr: Wndicates a write access if true.
 *
 * Return:
 * * %0 in case of success,
 * * a PIC code > 0 in case the address translation fails
 * * an error code < 0 if other errors happen in the host
 */
static int walk_guest_tables(struct gmap *sg, unsigned long saddr, struct pgtwalk *w, bool wr)
{
	struct gmap *parent = sg->parent;
	struct guest_fault *entries;
	union dat_table_entry table;
	union vaddress vaddr;
	unsigned long ptr;
	struct kvm *kvm;
	union asce asce;
	int rc;

	if (!parent)
		return -EAGAIN;
	kvm = parent->kvm;
	WARN_ON(!kvm);
	asce = sg->guest_asce;
	entries = get_entries(w);

	w->level = LEVEL_MEM;
	w->last_addr = saddr;
	if (asce.r)
		return kvm_s390_get_guest_page(kvm, entries + LEVEL_MEM, gpa_to_gfn(saddr), false);

	vaddr.addr = saddr;
	ptr = asce.rsto * PAGE_SIZE;

	if (!asce_contains_gfn(asce, gpa_to_gfn(saddr)))
		return PGM_ASCE_TYPE;
	switch (asce.dt) {
	case ASCE_TYPE_REGION1:
		if (vaddr.rfx01 > asce.tl)
			return PGM_REGION_FIRST_TRANS;
		break;
	case ASCE_TYPE_REGION2:
		if (vaddr.rsx01 > asce.tl)
			return PGM_REGION_SECOND_TRANS;
		break;
	case ASCE_TYPE_REGION3:
		if (vaddr.rtx01 > asce.tl)
			return PGM_REGION_THIRD_TRANS;
		break;
	case ASCE_TYPE_SEGMENT:
		if (vaddr.sx01 > asce.tl)
			return PGM_SEGMENT_TRANSLATION;
		break;
	}

	w->level = asce.dt;
	switch (asce.dt) {
	case ASCE_TYPE_REGION1:
		w->last_addr = ptr + vaddr.rfx * 8;
		rc = kvm_s390_get_guest_page_and_read_gpa(kvm, entries + w->level,
							  w->last_addr, &table.val);
		if (rc)
			return rc;
		if (table.pgd.i)
			return PGM_REGION_FIRST_TRANS;
		if (table.pgd.tt != TABLE_TYPE_REGION1)
			return PGM_TRANSLATION_SPEC;
		if (vaddr.rsx01 < table.pgd.tf || vaddr.rsx01 > table.pgd.tl)
			return PGM_REGION_SECOND_TRANS;
		if (sg->edat_level >= 1)
			w->p |= table.pgd.p;
		ptr = table.pgd.rto * PAGE_SIZE;
		w->level--;
		fallthrough;
	case ASCE_TYPE_REGION2:
		w->last_addr = ptr + vaddr.rsx * 8;
		rc = kvm_s390_get_guest_page_and_read_gpa(kvm, entries + w->level,
							  w->last_addr, &table.val);
		if (rc)
			return rc;
		if (table.p4d.i)
			return PGM_REGION_SECOND_TRANS;
		if (table.p4d.tt != TABLE_TYPE_REGION2)
			return PGM_TRANSLATION_SPEC;
		if (vaddr.rtx01 < table.p4d.tf || vaddr.rtx01 > table.p4d.tl)
			return PGM_REGION_THIRD_TRANS;
		if (sg->edat_level >= 1)
			w->p |= table.p4d.p;
		ptr = table.p4d.rto * PAGE_SIZE;
		w->level--;
		fallthrough;
	case ASCE_TYPE_REGION3:
		w->last_addr = ptr + vaddr.rtx * 8;
		rc = kvm_s390_get_guest_page_and_read_gpa(kvm, entries + w->level,
							  w->last_addr, &table.val);
		if (rc)
			return rc;
		if (table.pud.i)
			return PGM_REGION_THIRD_TRANS;
		if (table.pud.tt != TABLE_TYPE_REGION3)
			return PGM_TRANSLATION_SPEC;
		if (table.pud.cr && asce.p && sg->edat_level >= 2)
			return PGM_TRANSLATION_SPEC;
		if (sg->edat_level >= 1)
			w->p |= table.pud.p;
		if (table.pud.fc && sg->edat_level >= 2) {
			table.val = u64_replace_bits(table.val, saddr, ~_REGION3_MASK);
			goto edat_applies;
		}
		if (vaddr.sx01 < table.pud.fc0.tf || vaddr.sx01 > table.pud.fc0.tl)
			return PGM_SEGMENT_TRANSLATION;
		ptr = table.pud.fc0.sto * PAGE_SIZE;
		w->level--;
		fallthrough;
	case ASCE_TYPE_SEGMENT:
		w->last_addr = ptr + vaddr.sx * 8;
		rc = kvm_s390_get_guest_page_and_read_gpa(kvm, entries + w->level,
							  w->last_addr, &table.val);
		if (rc)
			return rc;
		if (table.pmd.i)
			return PGM_SEGMENT_TRANSLATION;
		if (table.pmd.tt != TABLE_TYPE_SEGMENT)
			return PGM_TRANSLATION_SPEC;
		if (table.pmd.cs && asce.p)
			return PGM_TRANSLATION_SPEC;
		w->p |= table.pmd.p;
		if (table.pmd.fc && sg->edat_level >= 1) {
			table.val = u64_replace_bits(table.val, saddr, ~_SEGMENT_MASK);
			goto edat_applies;
		}
		ptr = table.pmd.fc0.pto * (PAGE_SIZE / 2);
		w->level--;
	}
	w->last_addr = ptr + vaddr.px * 8;
	rc = kvm_s390_get_guest_page_and_read_gpa(kvm, entries + w->level,
						  w->last_addr, &table.val);
	if (rc)
		return rc;
	if (table.pte.i)
		return PGM_PAGE_TRANSLATION;
	if (table.pte.z)
		return PGM_TRANSLATION_SPEC;
	w->p |= table.pte.p;
edat_applies:
	if (wr && w->p)
		return PGM_PROTECTION;

	return kvm_s390_get_guest_page(kvm, entries + LEVEL_MEM, table.pte.pfra, wr);
}

static int _do_shadow_pte(struct gmap *sg, gpa_t raddr, union pte *ptep_h, union pte *ptep,
			  struct guest_fault *f, bool p)
{
	union pgste pgste;
	union pte newpte;
	int rc;

	lockdep_assert_held(&sg->kvm->mmu_lock);
	lockdep_assert_held(&sg->parent->children_lock);

	scoped_guard(spinlock, &sg->host_to_rmap_lock)
		rc = gmap_insert_rmap(sg, f->gfn, gpa_to_gfn(raddr), TABLE_TYPE_PAGE_TABLE);
	if (rc)
		return rc;

	pgste = pgste_get_lock(ptep_h);
	newpte = _pte(f->pfn, f->writable, !p, 0);
	newpte.s.d |= ptep->s.d;
	newpte.s.sd |= ptep->s.sd;
	newpte.h.p &= ptep->h.p;
	pgste = _gmap_ptep_xchg(sg->parent, ptep_h, newpte, pgste, f->gfn, false);
	pgste.vsie_notif = 1;
	pgste_set_unlock(ptep_h, pgste);

	newpte = _pte(f->pfn, 0, !p, 0);
	pgste = pgste_get_lock(ptep);
	pgste = __dat_ptep_xchg(ptep, pgste, newpte, gpa_to_gfn(raddr), sg->asce, uses_skeys(sg));
	pgste_set_unlock(ptep, pgste);

	return 0;
}

static int _do_shadow_crste(struct gmap *sg, gpa_t raddr, union crste *host, union crste *table,
			    struct guest_fault *f, bool p)
{
	union crste newcrste;
	gfn_t gfn;
	int rc;

	lockdep_assert_held(&sg->kvm->mmu_lock);
	lockdep_assert_held(&sg->parent->children_lock);

	gfn = f->gfn & gpa_to_gfn(is_pmd(*table) ? _SEGMENT_MASK : _REGION3_MASK);
	scoped_guard(spinlock, &sg->host_to_rmap_lock)
		rc = gmap_insert_rmap(sg, gfn, gpa_to_gfn(raddr), host->h.tt);
	if (rc)
		return rc;

	newcrste = _crste_fc1(f->pfn, host->h.tt, f->writable, !p);
	newcrste.s.fc1.d |= host->s.fc1.d;
	newcrste.s.fc1.sd |= host->s.fc1.sd;
	newcrste.h.p &= host->h.p;
	newcrste.s.fc1.vsie_notif = 1;
	newcrste.s.fc1.prefix_notif = host->s.fc1.prefix_notif;
	_gmap_crstep_xchg(sg->parent, host, newcrste, f->gfn, false);

	newcrste = _crste_fc1(f->pfn, host->h.tt, 0, !p);
	dat_crstep_xchg(table, newcrste, gpa_to_gfn(raddr), sg->asce);
	return 0;
}

static int _gaccess_do_shadow(struct kvm_s390_mmu_cache *mc, struct gmap *sg,
			      unsigned long saddr, struct pgtwalk *w)
{
	struct guest_fault *entries;
	int flags, i, hl, gl, l, rc;
	union crste *table, *host;
	union pte *ptep, *ptep_h;

	lockdep_assert_held(&sg->kvm->mmu_lock);
	lockdep_assert_held(&sg->parent->children_lock);

	entries = get_entries(w);
	ptep_h = NULL;
	ptep = NULL;

	rc = dat_entry_walk(NULL, gpa_to_gfn(saddr), sg->asce, DAT_WALK_ANY, TABLE_TYPE_PAGE_TABLE,
			    &table, &ptep);
	if (rc)
		return rc;

	/* A race occourred. The shadow mapping is already valid, nothing to do */
	if ((ptep && !ptep->h.i) || (!ptep && crste_leaf(*table)))
		return 0;

	gl = get_level(table, ptep);

	/*
	 * Skip levels that are already protected. For each level, protect
	 * only the page containing the entry, not the whole table.
	 */
	for (i = gl ; i >= w->level; i--) {
		rc = gmap_protect_rmap(mc, sg, entries[i - 1].gfn, gpa_to_gfn(saddr),
				       entries[i - 1].pfn, i, entries[i - 1].writable);
		if (rc)
			return rc;
	}

	rc = dat_entry_walk(NULL, entries[LEVEL_MEM].gfn, sg->parent->asce, DAT_WALK_LEAF,
			    TABLE_TYPE_PAGE_TABLE, &host, &ptep_h);
	if (rc)
		return rc;

	hl = get_level(host, ptep_h);
	/* Get the smallest granularity */
	l = min3(gl, hl, w->level);

	flags = DAT_WALK_SPLIT_ALLOC | (uses_skeys(sg->parent) ? DAT_WALK_USES_SKEYS : 0);
	/* If necessary, create the shadow mapping */
	if (l < gl) {
		rc = dat_entry_walk(mc, gpa_to_gfn(saddr), sg->asce, flags, l, &table, &ptep);
		if (rc)
			return rc;
	}
	if (l < hl) {
		rc = dat_entry_walk(mc, entries[LEVEL_MEM].gfn, sg->parent->asce,
				    flags, l, &host, &ptep_h);
		if (rc)
			return rc;
	}

	if (KVM_BUG_ON(l > TABLE_TYPE_REGION3, sg->kvm))
		return -EFAULT;
	if (l == TABLE_TYPE_PAGE_TABLE)
		return _do_shadow_pte(sg, saddr, ptep_h, ptep, entries + LEVEL_MEM, w->p);
	return _do_shadow_crste(sg, saddr, host, table, entries + LEVEL_MEM, w->p);
}

static inline int _gaccess_shadow_fault(struct kvm_vcpu *vcpu, struct gmap *sg, gpa_t saddr,
					unsigned long seq, struct pgtwalk *walk)
{
	struct gmap *parent;
	int rc;

	if (kvm_s390_array_needs_retry_unsafe(vcpu->kvm, seq, walk->raw_entries))
		return -EAGAIN;
again:
	rc = kvm_s390_mmu_cache_topup(vcpu->arch.mc);
	if (rc)
		return rc;
	scoped_guard(read_lock, &vcpu->kvm->mmu_lock) {
		if (kvm_s390_array_needs_retry_safe(vcpu->kvm, seq, walk->raw_entries))
			return -EAGAIN;
		parent = READ_ONCE(sg->parent);
		if (!parent)
			return -EAGAIN;
		scoped_guard(spinlock, &parent->children_lock) {
			if (READ_ONCE(sg->parent) != parent)
				return -EAGAIN;
			rc = _gaccess_do_shadow(vcpu->arch.mc, sg, saddr, walk);
		}
		if (rc == -ENOMEM)
			goto again;
		if (!rc)
			kvm_s390_release_faultin_array(vcpu->kvm, walk->raw_entries, false);
	}
	return rc;
}

/**
 * __gaccess_shadow_fault() - Handle fault on a shadow page table.
 * @vcpu: Virtual cpu that triggered the action.
 * @sg: The shadow guest address space structure.
 * @saddr: Faulting address in the shadow gmap.
 * @datptr: Will contain the address of the faulting DAT table entry, or of
 *	    the valid leaf, plus some flags.
 * @wr: Whether this is a write access.
 *
 * Return:
 * * %0 if the shadow fault was successfully resolved
 * * > 0 (pgm exception code) on exceptions while faulting
 * * %-EAGAIN if the caller can retry immediately
 * * %-EFAULT when accessing invalid guest addresses
 * * %-ENOMEM if out of memory
 */
static int __gaccess_shadow_fault(struct kvm_vcpu *vcpu, struct gmap *sg, gpa_t saddr,
				  union mvpg_pei *datptr, bool wr)
{
	struct pgtwalk walk = {	.p = false, };
	unsigned long seq;
	int rc;

	seq = vcpu->kvm->mmu_invalidate_seq;
	/* Pairs with the smp_wmb() in kvm_mmu_invalidate_end(). */
	smp_rmb();

	rc = walk_guest_tables(sg, saddr, &walk, wr);
	if (datptr) {
		datptr->val = walk.last_addr;
		datptr->dat_prot = wr && walk.p;
		datptr->not_pte = walk.level > TABLE_TYPE_PAGE_TABLE;
		datptr->real = sg->guest_asce.r;
	}
	if (!rc)
		rc = _gaccess_shadow_fault(vcpu, sg, saddr, seq, &walk);
	if (rc)
		kvm_s390_release_faultin_array(vcpu->kvm, walk.raw_entries, true);
	return rc;
}

int gaccess_shadow_fault(struct kvm_vcpu *vcpu, struct gmap *sg, gpa_t saddr,
			 union mvpg_pei *datptr, bool wr)
{
	int rc;

	if (KVM_BUG_ON(!test_bit(GMAP_FLAG_SHADOW, &sg->flags), vcpu->kvm))
		return -EFAULT;

	rc = kvm_s390_mmu_cache_topup(vcpu->arch.mc);
	if (rc)
		return rc;

	ipte_lock(vcpu->kvm);
	rc = __gaccess_shadow_fault(vcpu, sg, saddr, datptr, wr || sg->guest_asce.r);
	ipte_unlock(vcpu->kvm);

	return rc;
}
