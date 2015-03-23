/*
 * guest access functions
 *
 * Copyright IBM Corp. 2014
 *
 */

#include <linux/vmalloc.h>
#include <linux/err.h>
#include <asm/pgtable.h>
#include "kvm-s390.h"
#include "gaccess.h"
#include <asm/switch_to.h>

union asce {
	unsigned long val;
	struct {
		unsigned long origin : 52; /* Region- or Segment-Table Origin */
		unsigned long	 : 2;
		unsigned long g  : 1; /* Subspace Group Control */
		unsigned long p  : 1; /* Private Space Control */
		unsigned long s  : 1; /* Storage-Alteration-Event Control */
		unsigned long x  : 1; /* Space-Switch-Event Control */
		unsigned long r  : 1; /* Real-Space Control */
		unsigned long	 : 1;
		unsigned long dt : 2; /* Designation-Type Control */
		unsigned long tl : 2; /* Region- or Segment-Table Length */
	};
};

enum {
	ASCE_TYPE_SEGMENT = 0,
	ASCE_TYPE_REGION3 = 1,
	ASCE_TYPE_REGION2 = 2,
	ASCE_TYPE_REGION1 = 3
};

union region1_table_entry {
	unsigned long val;
	struct {
		unsigned long rto: 52;/* Region-Table Origin */
		unsigned long	 : 2;
		unsigned long p  : 1; /* DAT-Protection Bit */
		unsigned long	 : 1;
		unsigned long tf : 2; /* Region-Second-Table Offset */
		unsigned long i  : 1; /* Region-Invalid Bit */
		unsigned long	 : 1;
		unsigned long tt : 2; /* Table-Type Bits */
		unsigned long tl : 2; /* Region-Second-Table Length */
	};
};

union region2_table_entry {
	unsigned long val;
	struct {
		unsigned long rto: 52;/* Region-Table Origin */
		unsigned long	 : 2;
		unsigned long p  : 1; /* DAT-Protection Bit */
		unsigned long	 : 1;
		unsigned long tf : 2; /* Region-Third-Table Offset */
		unsigned long i  : 1; /* Region-Invalid Bit */
		unsigned long	 : 1;
		unsigned long tt : 2; /* Table-Type Bits */
		unsigned long tl : 2; /* Region-Third-Table Length */
	};
};

struct region3_table_entry_fc0 {
	unsigned long sto: 52;/* Segment-Table Origin */
	unsigned long	 : 1;
	unsigned long fc : 1; /* Format-Control */
	unsigned long p  : 1; /* DAT-Protection Bit */
	unsigned long	 : 1;
	unsigned long tf : 2; /* Segment-Table Offset */
	unsigned long i  : 1; /* Region-Invalid Bit */
	unsigned long cr : 1; /* Common-Region Bit */
	unsigned long tt : 2; /* Table-Type Bits */
	unsigned long tl : 2; /* Segment-Table Length */
};

struct region3_table_entry_fc1 {
	unsigned long rfaa : 33; /* Region-Frame Absolute Address */
	unsigned long	 : 14;
	unsigned long av : 1; /* ACCF-Validity Control */
	unsigned long acc: 4; /* Access-Control Bits */
	unsigned long f  : 1; /* Fetch-Protection Bit */
	unsigned long fc : 1; /* Format-Control */
	unsigned long p  : 1; /* DAT-Protection Bit */
	unsigned long co : 1; /* Change-Recording Override */
	unsigned long	 : 2;
	unsigned long i  : 1; /* Region-Invalid Bit */
	unsigned long cr : 1; /* Common-Region Bit */
	unsigned long tt : 2; /* Table-Type Bits */
	unsigned long	 : 2;
};

union region3_table_entry {
	unsigned long val;
	struct region3_table_entry_fc0 fc0;
	struct region3_table_entry_fc1 fc1;
	struct {
		unsigned long	 : 53;
		unsigned long fc : 1; /* Format-Control */
		unsigned long	 : 4;
		unsigned long i  : 1; /* Region-Invalid Bit */
		unsigned long cr : 1; /* Common-Region Bit */
		unsigned long tt : 2; /* Table-Type Bits */
		unsigned long	 : 2;
	};
};

struct segment_entry_fc0 {
	unsigned long pto: 53;/* Page-Table Origin */
	unsigned long fc : 1; /* Format-Control */
	unsigned long p  : 1; /* DAT-Protection Bit */
	unsigned long	 : 3;
	unsigned long i  : 1; /* Segment-Invalid Bit */
	unsigned long cs : 1; /* Common-Segment Bit */
	unsigned long tt : 2; /* Table-Type Bits */
	unsigned long	 : 2;
};

struct segment_entry_fc1 {
	unsigned long sfaa : 44; /* Segment-Frame Absolute Address */
	unsigned long	 : 3;
	unsigned long av : 1; /* ACCF-Validity Control */
	unsigned long acc: 4; /* Access-Control Bits */
	unsigned long f  : 1; /* Fetch-Protection Bit */
	unsigned long fc : 1; /* Format-Control */
	unsigned long p  : 1; /* DAT-Protection Bit */
	unsigned long co : 1; /* Change-Recording Override */
	unsigned long	 : 2;
	unsigned long i  : 1; /* Segment-Invalid Bit */
	unsigned long cs : 1; /* Common-Segment Bit */
	unsigned long tt : 2; /* Table-Type Bits */
	unsigned long	 : 2;
};

union segment_table_entry {
	unsigned long val;
	struct segment_entry_fc0 fc0;
	struct segment_entry_fc1 fc1;
	struct {
		unsigned long	 : 53;
		unsigned long fc : 1; /* Format-Control */
		unsigned long	 : 4;
		unsigned long i  : 1; /* Segment-Invalid Bit */
		unsigned long cs : 1; /* Common-Segment Bit */
		unsigned long tt : 2; /* Table-Type Bits */
		unsigned long	 : 2;
	};
};

enum {
	TABLE_TYPE_SEGMENT = 0,
	TABLE_TYPE_REGION3 = 1,
	TABLE_TYPE_REGION2 = 2,
	TABLE_TYPE_REGION1 = 3
};

union page_table_entry {
	unsigned long val;
	struct {
		unsigned long pfra : 52; /* Page-Frame Real Address */
		unsigned long z  : 1; /* Zero Bit */
		unsigned long i  : 1; /* Page-Invalid Bit */
		unsigned long p  : 1; /* DAT-Protection Bit */
		unsigned long co : 1; /* Change-Recording Override */
		unsigned long	 : 8;
	};
};

/*
 * vaddress union in order to easily decode a virtual address into its
 * region first index, region second index etc. parts.
 */
union vaddress {
	unsigned long addr;
	struct {
		unsigned long rfx : 11;
		unsigned long rsx : 11;
		unsigned long rtx : 11;
		unsigned long sx  : 11;
		unsigned long px  : 8;
		unsigned long bx  : 12;
	};
	struct {
		unsigned long rfx01 : 2;
		unsigned long	    : 9;
		unsigned long rsx01 : 2;
		unsigned long	    : 9;
		unsigned long rtx01 : 2;
		unsigned long	    : 9;
		unsigned long sx01  : 2;
		unsigned long	    : 29;
	};
};

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
} __packed;

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
} __packed;

int ipte_lock_held(struct kvm_vcpu *vcpu)
{
	union ipte_control *ic = &vcpu->kvm->arch.sca->ipte_control;

	if (vcpu->arch.sie_block->eca & 1)
		return ic->kh != 0;
	return vcpu->kvm->arch.ipte_lock_count != 0;
}

static void ipte_lock_simple(struct kvm_vcpu *vcpu)
{
	union ipte_control old, new, *ic;

	mutex_lock(&vcpu->kvm->arch.ipte_mutex);
	vcpu->kvm->arch.ipte_lock_count++;
	if (vcpu->kvm->arch.ipte_lock_count > 1)
		goto out;
	ic = &vcpu->kvm->arch.sca->ipte_control;
	do {
		old = READ_ONCE(*ic);
		while (old.k) {
			cond_resched();
			old = READ_ONCE(*ic);
		}
		new = old;
		new.k = 1;
	} while (cmpxchg(&ic->val, old.val, new.val) != old.val);
out:
	mutex_unlock(&vcpu->kvm->arch.ipte_mutex);
}

static void ipte_unlock_simple(struct kvm_vcpu *vcpu)
{
	union ipte_control old, new, *ic;

	mutex_lock(&vcpu->kvm->arch.ipte_mutex);
	vcpu->kvm->arch.ipte_lock_count--;
	if (vcpu->kvm->arch.ipte_lock_count)
		goto out;
	ic = &vcpu->kvm->arch.sca->ipte_control;
	do {
		old = READ_ONCE(*ic);
		new = old;
		new.k = 0;
	} while (cmpxchg(&ic->val, old.val, new.val) != old.val);
	wake_up(&vcpu->kvm->arch.ipte_wq);
out:
	mutex_unlock(&vcpu->kvm->arch.ipte_mutex);
}

static void ipte_lock_siif(struct kvm_vcpu *vcpu)
{
	union ipte_control old, new, *ic;

	ic = &vcpu->kvm->arch.sca->ipte_control;
	do {
		old = READ_ONCE(*ic);
		while (old.kg) {
			cond_resched();
			old = READ_ONCE(*ic);
		}
		new = old;
		new.k = 1;
		new.kh++;
	} while (cmpxchg(&ic->val, old.val, new.val) != old.val);
}

static void ipte_unlock_siif(struct kvm_vcpu *vcpu)
{
	union ipte_control old, new, *ic;

	ic = &vcpu->kvm->arch.sca->ipte_control;
	do {
		old = READ_ONCE(*ic);
		new = old;
		new.kh--;
		if (!new.kh)
			new.k = 0;
	} while (cmpxchg(&ic->val, old.val, new.val) != old.val);
	if (!new.kh)
		wake_up(&vcpu->kvm->arch.ipte_wq);
}

void ipte_lock(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.sie_block->eca & 1)
		ipte_lock_siif(vcpu);
	else
		ipte_lock_simple(vcpu);
}

void ipte_unlock(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.sie_block->eca & 1)
		ipte_unlock_siif(vcpu);
	else
		ipte_unlock_simple(vcpu);
}

static int ar_translation(struct kvm_vcpu *vcpu, union asce *asce, ar_t ar,
			  int write)
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

	if (ale.fo == 1 && write)
		return PGM_PROTECTION;

	asce->val = aste.asce;
	return 0;
}

struct trans_exc_code_bits {
	unsigned long addr : 52; /* Translation-exception Address */
	unsigned long fsi  : 2;  /* Access Exception Fetch/Store Indication */
	unsigned long	   : 6;
	unsigned long b60  : 1;
	unsigned long b61  : 1;
	unsigned long as   : 2;  /* ASCE Identifier */
};

enum {
	FSI_UNKNOWN = 0, /* Unknown wether fetch or store */
	FSI_STORE   = 1, /* Exception was due to store operation */
	FSI_FETCH   = 2  /* Exception was due to fetch operation */
};

static int get_vcpu_asce(struct kvm_vcpu *vcpu, union asce *asce,
			 ar_t ar, int write)
{
	int rc;
	psw_t *psw = &vcpu->arch.sie_block->gpsw;
	struct kvm_s390_pgm_info *pgm = &vcpu->arch.pgm;
	struct trans_exc_code_bits *tec_bits;

	memset(pgm, 0, sizeof(*pgm));
	tec_bits = (struct trans_exc_code_bits *)&pgm->trans_exc_code;
	tec_bits->fsi = write ? FSI_STORE : FSI_FETCH;
	tec_bits->as = psw_bits(*psw).as;

	if (!psw_bits(*psw).t) {
		asce->val = 0;
		asce->r = 1;
		return 0;
	}

	switch (psw_bits(vcpu->arch.sie_block->gpsw).as) {
	case PSW_AS_PRIMARY:
		asce->val = vcpu->arch.sie_block->gcr[1];
		return 0;
	case PSW_AS_SECONDARY:
		asce->val = vcpu->arch.sie_block->gcr[7];
		return 0;
	case PSW_AS_HOME:
		asce->val = vcpu->arch.sie_block->gcr[13];
		return 0;
	case PSW_AS_ACCREG:
		rc = ar_translation(vcpu, asce, ar, write);
		switch (rc) {
		case PGM_ALEN_TRANSLATION:
		case PGM_ALE_SEQUENCE:
		case PGM_ASTE_VALIDITY:
		case PGM_ASTE_SEQUENCE:
		case PGM_EXTENDED_AUTHORITY:
			vcpu->arch.pgm.exc_access_id = ar;
			break;
		case PGM_PROTECTION:
			tec_bits->b60 = 1;
			tec_bits->b61 = 1;
			break;
		}
		if (rc > 0)
			pgm->code = rc;
		return rc;
	}
	return 0;
}

static int deref_table(struct kvm *kvm, unsigned long gpa, unsigned long *val)
{
	return kvm_read_guest(kvm, gpa, val, sizeof(*val));
}

/**
 * guest_translate - translate a guest virtual into a guest absolute address
 * @vcpu: virtual cpu
 * @gva: guest virtual address
 * @gpa: points to where guest physical (absolute) address should be stored
 * @asce: effective asce
 * @write: indicates if access is a write access
 *
 * Translate a guest virtual address into a guest absolute address by means
 * of dynamic address translation as specified by the architecture.
 * If the resulting absolute address is not available in the configuration
 * an addressing exception is indicated and @gpa will not be changed.
 *
 * Returns: - zero on success; @gpa contains the resulting absolute address
 *	    - a negative value if guest access failed due to e.g. broken
 *	      guest mapping
 *	    - a positve value if an access exception happened. In this case
 *	      the returned value is the program interruption code as defined
 *	      by the architecture
 */
static unsigned long guest_translate(struct kvm_vcpu *vcpu, unsigned long gva,
				     unsigned long *gpa, const union asce asce,
				     int write)
{
	union vaddress vaddr = {.addr = gva};
	union raddress raddr = {.addr = gva};
	union page_table_entry pte;
	int dat_protection = 0;
	union ctlreg0 ctlreg0;
	unsigned long ptr;
	int edat1, edat2;

	ctlreg0.val = vcpu->arch.sie_block->gcr[0];
	edat1 = ctlreg0.edat && test_kvm_facility(vcpu->kvm, 8);
	edat2 = edat1 && test_kvm_facility(vcpu->kvm, 78);
	if (asce.r)
		goto real_address;
	ptr = asce.origin * 4096;
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

		if (kvm_is_error_gpa(vcpu->kvm, ptr))
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
		ptr = rfte.rto * 4096 + vaddr.rsx * 8;
	}
		/* fallthrough */
	case ASCE_TYPE_REGION2: {
		union region2_table_entry rste;

		if (kvm_is_error_gpa(vcpu->kvm, ptr))
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
		ptr = rste.rto * 4096 + vaddr.rtx * 8;
	}
		/* fallthrough */
	case ASCE_TYPE_REGION3: {
		union region3_table_entry rtte;

		if (kvm_is_error_gpa(vcpu->kvm, ptr))
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
			raddr.rfaa = rtte.fc1.rfaa;
			goto absolute_address;
		}
		if (vaddr.sx01 < rtte.fc0.tf)
			return PGM_SEGMENT_TRANSLATION;
		if (vaddr.sx01 > rtte.fc0.tl)
			return PGM_SEGMENT_TRANSLATION;
		if (edat1)
			dat_protection |= rtte.fc0.p;
		ptr = rtte.fc0.sto * 4096 + vaddr.sx * 8;
	}
		/* fallthrough */
	case ASCE_TYPE_SEGMENT: {
		union segment_table_entry ste;

		if (kvm_is_error_gpa(vcpu->kvm, ptr))
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
			raddr.sfaa = ste.fc1.sfaa;
			goto absolute_address;
		}
		dat_protection |= ste.fc0.p;
		ptr = ste.fc0.pto * 2048 + vaddr.px * 8;
	}
	}
	if (kvm_is_error_gpa(vcpu->kvm, ptr))
		return PGM_ADDRESSING;
	if (deref_table(vcpu->kvm, ptr, &pte.val))
		return -EFAULT;
	if (pte.i)
		return PGM_PAGE_TRANSLATION;
	if (pte.z)
		return PGM_TRANSLATION_SPEC;
	if (pte.co && !edat1)
		return PGM_TRANSLATION_SPEC;
	dat_protection |= pte.p;
	raddr.pfra = pte.pfra;
real_address:
	raddr.addr = kvm_s390_real_to_abs(vcpu, raddr.addr);
absolute_address:
	if (write && dat_protection)
		return PGM_PROTECTION;
	if (kvm_is_error_gpa(vcpu->kvm, raddr.addr))
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
	if (psw_bits(*psw).t && asce.p)
		return 0;
	return 1;
}

static int guest_page_range(struct kvm_vcpu *vcpu, unsigned long ga,
			    unsigned long *pages, unsigned long nr_pages,
			    const union asce asce, int write)
{
	struct kvm_s390_pgm_info *pgm = &vcpu->arch.pgm;
	psw_t *psw = &vcpu->arch.sie_block->gpsw;
	struct trans_exc_code_bits *tec_bits;
	int lap_enabled, rc;

	tec_bits = (struct trans_exc_code_bits *)&pgm->trans_exc_code;
	lap_enabled = low_address_protection_enabled(vcpu, asce);
	while (nr_pages) {
		ga = kvm_s390_logical_to_effective(vcpu, ga);
		tec_bits->addr = ga >> PAGE_SHIFT;
		if (write && lap_enabled && is_low_address(ga)) {
			pgm->code = PGM_PROTECTION;
			return pgm->code;
		}
		ga &= PAGE_MASK;
		if (psw_bits(*psw).t) {
			rc = guest_translate(vcpu, ga, pages, asce, write);
			if (rc < 0)
				return rc;
			if (rc == PGM_PROTECTION)
				tec_bits->b61 = 1;
			if (rc)
				pgm->code = rc;
		} else {
			*pages = kvm_s390_real_to_abs(vcpu, ga);
			if (kvm_is_error_gpa(vcpu->kvm, *pages))
				pgm->code = PGM_ADDRESSING;
		}
		if (pgm->code)
			return pgm->code;
		ga += PAGE_SIZE;
		pages++;
		nr_pages--;
	}
	return 0;
}

int access_guest(struct kvm_vcpu *vcpu, unsigned long ga, ar_t ar, void *data,
		 unsigned long len, int write)
{
	psw_t *psw = &vcpu->arch.sie_block->gpsw;
	unsigned long _len, nr_pages, gpa, idx;
	unsigned long pages_array[2];
	unsigned long *pages;
	int need_ipte_lock;
	union asce asce;
	int rc;

	if (!len)
		return 0;
	rc = get_vcpu_asce(vcpu, &asce, ar, write);
	if (rc)
		return rc;
	nr_pages = (((ga & ~PAGE_MASK) + len - 1) >> PAGE_SHIFT) + 1;
	pages = pages_array;
	if (nr_pages > ARRAY_SIZE(pages_array))
		pages = vmalloc(nr_pages * sizeof(unsigned long));
	if (!pages)
		return -ENOMEM;
	need_ipte_lock = psw_bits(*psw).t && !asce.r;
	if (need_ipte_lock)
		ipte_lock(vcpu);
	rc = guest_page_range(vcpu, ga, pages, nr_pages, asce, write);
	for (idx = 0; idx < nr_pages && !rc; idx++) {
		gpa = *(pages + idx) + (ga & ~PAGE_MASK);
		_len = min(PAGE_SIZE - (gpa & ~PAGE_MASK), len);
		if (write)
			rc = kvm_write_guest(vcpu->kvm, gpa, data, _len);
		else
			rc = kvm_read_guest(vcpu->kvm, gpa, data, _len);
		len -= _len;
		ga += _len;
		data += _len;
	}
	if (need_ipte_lock)
		ipte_unlock(vcpu);
	if (nr_pages > ARRAY_SIZE(pages_array))
		vfree(pages);
	return rc;
}

int access_guest_real(struct kvm_vcpu *vcpu, unsigned long gra,
		      void *data, unsigned long len, int write)
{
	unsigned long _len, gpa;
	int rc = 0;

	while (len && !rc) {
		gpa = kvm_s390_real_to_abs(vcpu, gra);
		_len = min(PAGE_SIZE - (gpa & ~PAGE_MASK), len);
		if (write)
			rc = write_guest_abs(vcpu, gpa, data, _len);
		else
			rc = read_guest_abs(vcpu, gpa, data, _len);
		len -= _len;
		gra += _len;
		data += _len;
	}
	return rc;
}

/**
 * guest_translate_address - translate guest logical into guest absolute address
 *
 * Parameter semantics are the same as the ones from guest_translate.
 * The memory contents at the guest address are not changed.
 *
 * Note: The IPTE lock is not taken during this function, so the caller
 * has to take care of this.
 */
int guest_translate_address(struct kvm_vcpu *vcpu, unsigned long gva, ar_t ar,
			    unsigned long *gpa, int write)
{
	struct kvm_s390_pgm_info *pgm = &vcpu->arch.pgm;
	psw_t *psw = &vcpu->arch.sie_block->gpsw;
	struct trans_exc_code_bits *tec;
	union asce asce;
	int rc;

	gva = kvm_s390_logical_to_effective(vcpu, gva);
	tec = (struct trans_exc_code_bits *)&pgm->trans_exc_code;
	rc = get_vcpu_asce(vcpu, &asce, ar, write);
	tec->addr = gva >> PAGE_SHIFT;
	if (rc)
		return rc;
	if (is_low_address(gva) && low_address_protection_enabled(vcpu, asce)) {
		if (write) {
			rc = pgm->code = PGM_PROTECTION;
			return rc;
		}
	}

	if (psw_bits(*psw).t && !asce.r) {	/* Use DAT? */
		rc = guest_translate(vcpu, gva, gpa, asce, write);
		if (rc > 0) {
			if (rc == PGM_PROTECTION)
				tec->b61 = 1;
			pgm->code = rc;
		}
	} else {
		rc = 0;
		*gpa = kvm_s390_real_to_abs(vcpu, gva);
		if (kvm_is_error_gpa(vcpu->kvm, *gpa))
			rc = pgm->code = PGM_ADDRESSING;
	}

	return rc;
}

/**
 * check_gva_range - test a range of guest virtual addresses for accessibility
 */
int check_gva_range(struct kvm_vcpu *vcpu, unsigned long gva, ar_t ar,
		    unsigned long length, int is_write)
{
	unsigned long gpa;
	unsigned long currlen;
	int rc = 0;

	ipte_lock(vcpu);
	while (length > 0 && !rc) {
		currlen = min(length, PAGE_SIZE - (gva % PAGE_SIZE));
		rc = guest_translate_address(vcpu, gva, ar, &gpa, is_write);
		gva += currlen;
		length -= currlen;
	}
	ipte_unlock(vcpu);

	return rc;
}

/**
 * kvm_s390_check_low_addr_prot_real - check for low-address protection
 * @gra: Guest real address
 *
 * Checks whether an address is subject to low-address protection and set
 * up vcpu->arch.pgm accordingly if necessary.
 *
 * Return: 0 if no protection exception, or PGM_PROTECTION if protected.
 */
int kvm_s390_check_low_addr_prot_real(struct kvm_vcpu *vcpu, unsigned long gra)
{
	struct kvm_s390_pgm_info *pgm = &vcpu->arch.pgm;
	psw_t *psw = &vcpu->arch.sie_block->gpsw;
	struct trans_exc_code_bits *tec_bits;
	union ctlreg0 ctlreg0 = {.val = vcpu->arch.sie_block->gcr[0]};

	if (!ctlreg0.lap || !is_low_address(gra))
		return 0;

	memset(pgm, 0, sizeof(*pgm));
	tec_bits = (struct trans_exc_code_bits *)&pgm->trans_exc_code;
	tec_bits->fsi = FSI_STORE;
	tec_bits->as = psw_bits(*psw).as;
	tec_bits->addr = gra >> PAGE_SHIFT;
	pgm->code = PGM_PROTECTION;

	return pgm->code;
}
