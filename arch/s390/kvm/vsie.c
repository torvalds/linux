/*
 * kvm nested virtualization support for s390x
 *
 * Copyright IBM Corp. 2016
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): David Hildenbrand <dahi@linux.vnet.ibm.com>
 */
#include <linux/vmalloc.h>
#include <linux/kvm_host.h>
#include <linux/bug.h>
#include <linux/list.h>
#include <linux/bitmap.h>
#include <asm/gmap.h>
#include <asm/mmu_context.h>
#include <asm/sclp.h>
#include <asm/nmi.h>
#include <asm/dis.h>
#include "kvm-s390.h"
#include "gaccess.h"

struct vsie_page {
	struct kvm_s390_sie_block scb_s;	/* 0x0000 */
	/* the pinned originial scb */
	struct kvm_s390_sie_block *scb_o;	/* 0x0200 */
	/* the shadow gmap in use by the vsie_page */
	struct gmap *gmap;			/* 0x0208 */
	/* address of the last reported fault to guest2 */
	unsigned long fault_addr;		/* 0x0210 */
	__u8 reserved[0x0700 - 0x0218];		/* 0x0218 */
	struct kvm_s390_crypto_cb crycb;	/* 0x0700 */
	__u8 fac[S390_ARCH_FAC_LIST_SIZE_BYTE];	/* 0x0800 */
} __packed;

/* trigger a validity icpt for the given scb */
static int set_validity_icpt(struct kvm_s390_sie_block *scb,
			     __u16 reason_code)
{
	scb->ipa = 0x1000;
	scb->ipb = ((__u32) reason_code) << 16;
	scb->icptcode = ICPT_VALIDITY;
	return 1;
}

/* mark the prefix as unmapped, this will block the VSIE */
static void prefix_unmapped(struct vsie_page *vsie_page)
{
	atomic_or(PROG_REQUEST, &vsie_page->scb_s.prog20);
}

/* mark the prefix as unmapped and wait until the VSIE has been left */
static void prefix_unmapped_sync(struct vsie_page *vsie_page)
{
	prefix_unmapped(vsie_page);
	if (vsie_page->scb_s.prog0c & PROG_IN_SIE)
		atomic_or(CPUSTAT_STOP_INT, &vsie_page->scb_s.cpuflags);
	while (vsie_page->scb_s.prog0c & PROG_IN_SIE)
		cpu_relax();
}

/* mark the prefix as mapped, this will allow the VSIE to run */
static void prefix_mapped(struct vsie_page *vsie_page)
{
	atomic_andnot(PROG_REQUEST, &vsie_page->scb_s.prog20);
}

/* test if the prefix is mapped into the gmap shadow */
static int prefix_is_mapped(struct vsie_page *vsie_page)
{
	return !(atomic_read(&vsie_page->scb_s.prog20) & PROG_REQUEST);
}

/* copy the updated intervention request bits into the shadow scb */
static void update_intervention_requests(struct vsie_page *vsie_page)
{
	const int bits = CPUSTAT_STOP_INT | CPUSTAT_IO_INT | CPUSTAT_EXT_INT;
	int cpuflags;

	cpuflags = atomic_read(&vsie_page->scb_o->cpuflags);
	atomic_andnot(bits, &vsie_page->scb_s.cpuflags);
	atomic_or(cpuflags & bits, &vsie_page->scb_s.cpuflags);
}

/* shadow (filter and validate) the cpuflags  */
static int prepare_cpuflags(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page)
{
	struct kvm_s390_sie_block *scb_s = &vsie_page->scb_s;
	struct kvm_s390_sie_block *scb_o = vsie_page->scb_o;
	int newflags, cpuflags = atomic_read(&scb_o->cpuflags);

	/* we don't allow ESA/390 guests */
	if (!(cpuflags & CPUSTAT_ZARCH))
		return set_validity_icpt(scb_s, 0x0001U);

	if (cpuflags & (CPUSTAT_RRF | CPUSTAT_MCDS))
		return set_validity_icpt(scb_s, 0x0001U);
	else if (cpuflags & (CPUSTAT_SLSV | CPUSTAT_SLSR))
		return set_validity_icpt(scb_s, 0x0007U);

	/* intervention requests will be set later */
	newflags = CPUSTAT_ZARCH;
	if (cpuflags & CPUSTAT_GED && test_kvm_facility(vcpu->kvm, 8))
		newflags |= CPUSTAT_GED;
	if (cpuflags & CPUSTAT_GED2 && test_kvm_facility(vcpu->kvm, 78)) {
		if (cpuflags & CPUSTAT_GED)
			return set_validity_icpt(scb_s, 0x0001U);
		newflags |= CPUSTAT_GED2;
	}
	if (test_kvm_cpu_feat(vcpu->kvm, KVM_S390_VM_CPU_FEAT_GPERE))
		newflags |= cpuflags & CPUSTAT_P;
	if (test_kvm_cpu_feat(vcpu->kvm, KVM_S390_VM_CPU_FEAT_GSLS))
		newflags |= cpuflags & CPUSTAT_SM;
	if (test_kvm_cpu_feat(vcpu->kvm, KVM_S390_VM_CPU_FEAT_IBS))
		newflags |= cpuflags & CPUSTAT_IBS;

	atomic_set(&scb_s->cpuflags, newflags);
	return 0;
}

/*
 * Create a shadow copy of the crycb block and setup key wrapping, if
 * requested for guest 3 and enabled for guest 2.
 *
 * We only accept format-1 (no AP in g2), but convert it into format-2
 * There is nothing to do for format-0.
 *
 * Returns: - 0 if shadowed or nothing to do
 *          - > 0 if control has to be given to guest 2
 */
static int shadow_crycb(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page)
{
	struct kvm_s390_sie_block *scb_s = &vsie_page->scb_s;
	struct kvm_s390_sie_block *scb_o = vsie_page->scb_o;
	u32 crycb_addr = scb_o->crycbd & 0x7ffffff8U;
	unsigned long *b1, *b2;
	u8 ecb3_flags;

	scb_s->crycbd = 0;
	if (!(scb_o->crycbd & vcpu->arch.sie_block->crycbd & CRYCB_FORMAT1))
		return 0;
	/* format-1 is supported with message-security-assist extension 3 */
	if (!test_kvm_facility(vcpu->kvm, 76))
		return 0;
	/* we may only allow it if enabled for guest 2 */
	ecb3_flags = scb_o->ecb3 & vcpu->arch.sie_block->ecb3 &
		     (ECB3_AES | ECB3_DEA);
	if (!ecb3_flags)
		return 0;

	if ((crycb_addr & PAGE_MASK) != ((crycb_addr + 128) & PAGE_MASK))
		return set_validity_icpt(scb_s, 0x003CU);
	else if (!crycb_addr)
		return set_validity_icpt(scb_s, 0x0039U);

	/* copy only the wrapping keys */
	if (read_guest_real(vcpu, crycb_addr + 72, &vsie_page->crycb, 56))
		return set_validity_icpt(scb_s, 0x0035U);

	scb_s->ecb3 |= ecb3_flags;
	scb_s->crycbd = ((__u32)(__u64) &vsie_page->crycb) | CRYCB_FORMAT1 |
			CRYCB_FORMAT2;

	/* xor both blocks in one run */
	b1 = (unsigned long *) vsie_page->crycb.dea_wrapping_key_mask;
	b2 = (unsigned long *)
			    vcpu->kvm->arch.crypto.crycb->dea_wrapping_key_mask;
	/* as 56%8 == 0, bitmap_xor won't overwrite any data */
	bitmap_xor(b1, b1, b2, BITS_PER_BYTE * 56);
	return 0;
}

/* shadow (round up/down) the ibc to avoid validity icpt */
static void prepare_ibc(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page)
{
	struct kvm_s390_sie_block *scb_s = &vsie_page->scb_s;
	struct kvm_s390_sie_block *scb_o = vsie_page->scb_o;
	__u64 min_ibc = (sclp.ibc >> 16) & 0x0fffU;

	scb_s->ibc = 0;
	/* ibc installed in g2 and requested for g3 */
	if (vcpu->kvm->arch.model.ibc && (scb_o->ibc & 0x0fffU)) {
		scb_s->ibc = scb_o->ibc & 0x0fffU;
		/* takte care of the minimum ibc level of the machine */
		if (scb_s->ibc < min_ibc)
			scb_s->ibc = min_ibc;
		/* take care of the maximum ibc level set for the guest */
		if (scb_s->ibc > vcpu->kvm->arch.model.ibc)
			scb_s->ibc = vcpu->kvm->arch.model.ibc;
	}
}

/* unshadow the scb, copying parameters back to the real scb */
static void unshadow_scb(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page)
{
	struct kvm_s390_sie_block *scb_s = &vsie_page->scb_s;
	struct kvm_s390_sie_block *scb_o = vsie_page->scb_o;

	/* interception */
	scb_o->icptcode = scb_s->icptcode;
	scb_o->icptstatus = scb_s->icptstatus;
	scb_o->ipa = scb_s->ipa;
	scb_o->ipb = scb_s->ipb;
	scb_o->gbea = scb_s->gbea;

	/* timer */
	scb_o->cputm = scb_s->cputm;
	scb_o->ckc = scb_s->ckc;
	scb_o->todpr = scb_s->todpr;

	/* guest state */
	scb_o->gpsw = scb_s->gpsw;
	scb_o->gg14 = scb_s->gg14;
	scb_o->gg15 = scb_s->gg15;
	memcpy(scb_o->gcr, scb_s->gcr, 128);
	scb_o->pp = scb_s->pp;

	/* interrupt intercept */
	switch (scb_s->icptcode) {
	case ICPT_PROGI:
	case ICPT_INSTPROGI:
	case ICPT_EXTINT:
		memcpy((void *)((u64)scb_o + 0xc0),
		       (void *)((u64)scb_s + 0xc0), 0xf0 - 0xc0);
		break;
	case ICPT_PARTEXEC:
		/* MVPG only */
		memcpy((void *)((u64)scb_o + 0xc0),
		       (void *)((u64)scb_s + 0xc0), 0xd0 - 0xc0);
		break;
	}

	if (scb_s->ihcpu != 0xffffU)
		scb_o->ihcpu = scb_s->ihcpu;
}

/*
 * Setup the shadow scb by copying and checking the relevant parts of the g2
 * provided scb.
 *
 * Returns: - 0 if the scb has been shadowed
 *          - > 0 if control has to be given to guest 2
 */
static int shadow_scb(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page)
{
	struct kvm_s390_sie_block *scb_o = vsie_page->scb_o;
	struct kvm_s390_sie_block *scb_s = &vsie_page->scb_s;
	bool had_tx = scb_s->ecb & 0x10U;
	unsigned long new_mso = 0;
	int rc;

	/* make sure we don't have any leftovers when reusing the scb */
	scb_s->icptcode = 0;
	scb_s->eca = 0;
	scb_s->ecb = 0;
	scb_s->ecb2 = 0;
	scb_s->ecb3 = 0;
	scb_s->ecd = 0;
	scb_s->fac = 0;

	rc = prepare_cpuflags(vcpu, vsie_page);
	if (rc)
		goto out;

	/* timer */
	scb_s->cputm = scb_o->cputm;
	scb_s->ckc = scb_o->ckc;
	scb_s->todpr = scb_o->todpr;
	scb_s->epoch = scb_o->epoch;

	/* guest state */
	scb_s->gpsw = scb_o->gpsw;
	scb_s->gg14 = scb_o->gg14;
	scb_s->gg15 = scb_o->gg15;
	memcpy(scb_s->gcr, scb_o->gcr, 128);
	scb_s->pp = scb_o->pp;

	/* interception / execution handling */
	scb_s->gbea = scb_o->gbea;
	scb_s->lctl = scb_o->lctl;
	scb_s->svcc = scb_o->svcc;
	scb_s->ictl = scb_o->ictl;
	/*
	 * SKEY handling functions can't deal with false setting of PTE invalid
	 * bits. Therefore we cannot provide interpretation and would later
	 * have to provide own emulation handlers.
	 */
	scb_s->ictl |= ICTL_ISKE | ICTL_SSKE | ICTL_RRBE;
	scb_s->icpua = scb_o->icpua;

	if (!(atomic_read(&scb_s->cpuflags) & CPUSTAT_SM))
		new_mso = scb_o->mso & 0xfffffffffff00000UL;
	/* if the hva of the prefix changes, we have to remap the prefix */
	if (scb_s->mso != new_mso || scb_s->prefix != scb_o->prefix)
		prefix_unmapped(vsie_page);
	 /* SIE will do mso/msl validity and exception checks for us */
	scb_s->msl = scb_o->msl & 0xfffffffffff00000UL;
	scb_s->mso = new_mso;
	scb_s->prefix = scb_o->prefix;

	/* We have to definetly flush the tlb if this scb never ran */
	if (scb_s->ihcpu != 0xffffU)
		scb_s->ihcpu = scb_o->ihcpu;

	/* MVPG and Protection Exception Interpretation are always available */
	scb_s->eca |= scb_o->eca & 0x01002000U;
	/* Host-protection-interruption introduced with ESOP */
	if (test_kvm_cpu_feat(vcpu->kvm, KVM_S390_VM_CPU_FEAT_ESOP))
		scb_s->ecb |= scb_o->ecb & 0x02U;
	/* transactional execution */
	if (test_kvm_facility(vcpu->kvm, 73)) {
		/* remap the prefix is tx is toggled on */
		if ((scb_o->ecb & 0x10U) && !had_tx)
			prefix_unmapped(vsie_page);
		scb_s->ecb |= scb_o->ecb & 0x10U;
	}
	/* SIMD */
	if (test_kvm_facility(vcpu->kvm, 129)) {
		scb_s->eca |= scb_o->eca & 0x00020000U;
		scb_s->ecd |= scb_o->ecd & 0x20000000U;
	}
	/* Run-time-Instrumentation */
	if (test_kvm_facility(vcpu->kvm, 64))
		scb_s->ecb3 |= scb_o->ecb3 & 0x01U;
	if (test_kvm_cpu_feat(vcpu->kvm, KVM_S390_VM_CPU_FEAT_SIIF))
		scb_s->eca |= scb_o->eca & 0x00000001U;
	if (test_kvm_cpu_feat(vcpu->kvm, KVM_S390_VM_CPU_FEAT_IB))
		scb_s->eca |= scb_o->eca & 0x40000000U;
	if (test_kvm_cpu_feat(vcpu->kvm, KVM_S390_VM_CPU_FEAT_CEI))
		scb_s->eca |= scb_o->eca & 0x80000000U;

	prepare_ibc(vcpu, vsie_page);
	rc = shadow_crycb(vcpu, vsie_page);
out:
	if (rc)
		unshadow_scb(vcpu, vsie_page);
	return rc;
}

void kvm_s390_vsie_gmap_notifier(struct gmap *gmap, unsigned long start,
				 unsigned long end)
{
	struct kvm *kvm = gmap->private;
	struct vsie_page *cur;
	unsigned long prefix;
	struct page *page;
	int i;

	if (!gmap_is_shadow(gmap))
		return;
	if (start >= 1UL << 31)
		/* We are only interested in prefix pages */
		return;

	/*
	 * Only new shadow blocks are added to the list during runtime,
	 * therefore we can safely reference them all the time.
	 */
	for (i = 0; i < kvm->arch.vsie.page_count; i++) {
		page = READ_ONCE(kvm->arch.vsie.pages[i]);
		if (!page)
			continue;
		cur = page_to_virt(page);
		if (READ_ONCE(cur->gmap) != gmap)
			continue;
		prefix = cur->scb_s.prefix << GUEST_PREFIX_SHIFT;
		/* with mso/msl, the prefix lies at an offset */
		prefix += cur->scb_s.mso;
		if (prefix <= end && start <= prefix + 2 * PAGE_SIZE - 1)
			prefix_unmapped_sync(cur);
	}
}

/*
 * Map the first prefix page and if tx is enabled also the second prefix page.
 *
 * The prefix will be protected, a gmap notifier will inform about unmaps.
 * The shadow scb must not be executed until the prefix is remapped, this is
 * guaranteed by properly handling PROG_REQUEST.
 *
 * Returns: - 0 on if successfully mapped or already mapped
 *          - > 0 if control has to be given to guest 2
 *          - -EAGAIN if the caller can retry immediately
 *          - -ENOMEM if out of memory
 */
static int map_prefix(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page)
{
	struct kvm_s390_sie_block *scb_s = &vsie_page->scb_s;
	u64 prefix = scb_s->prefix << GUEST_PREFIX_SHIFT;
	int rc;

	if (prefix_is_mapped(vsie_page))
		return 0;

	/* mark it as mapped so we can catch any concurrent unmappers */
	prefix_mapped(vsie_page);

	/* with mso/msl, the prefix lies at offset *mso* */
	prefix += scb_s->mso;

	rc = kvm_s390_shadow_fault(vcpu, vsie_page->gmap, prefix);
	if (!rc && (scb_s->ecb & 0x10U))
		rc = kvm_s390_shadow_fault(vcpu, vsie_page->gmap,
					   prefix + PAGE_SIZE);
	/*
	 * We don't have to mprotect, we will be called for all unshadows.
	 * SIE will detect if protection applies and trigger a validity.
	 */
	if (rc)
		prefix_unmapped(vsie_page);
	if (rc > 0 || rc == -EFAULT)
		rc = set_validity_icpt(scb_s, 0x0037U);
	return rc;
}

/*
 * Pin the guest page given by gpa and set hpa to the pinned host address.
 * Will always be pinned writable.
 *
 * Returns: - 0 on success
 *          - -EINVAL if the gpa is not valid guest storage
 *          - -ENOMEM if out of memory
 */
static int pin_guest_page(struct kvm *kvm, gpa_t gpa, hpa_t *hpa)
{
	struct page *page;
	hva_t hva;
	int rc;

	hva = gfn_to_hva(kvm, gpa_to_gfn(gpa));
	if (kvm_is_error_hva(hva))
		return -EINVAL;
	rc = get_user_pages_fast(hva, 1, 1, &page);
	if (rc < 0)
		return rc;
	else if (rc != 1)
		return -ENOMEM;
	*hpa = (hpa_t) page_to_virt(page) + (gpa & ~PAGE_MASK);
	return 0;
}

/* Unpins a page previously pinned via pin_guest_page, marking it as dirty. */
static void unpin_guest_page(struct kvm *kvm, gpa_t gpa, hpa_t hpa)
{
	struct page *page;

	page = virt_to_page(hpa);
	set_page_dirty_lock(page);
	put_page(page);
	/* mark the page always as dirty for migration */
	mark_page_dirty(kvm, gpa_to_gfn(gpa));
}

/* unpin all blocks previously pinned by pin_blocks(), marking them dirty */
static void unpin_blocks(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page)
{
	struct kvm_s390_sie_block *scb_o = vsie_page->scb_o;
	struct kvm_s390_sie_block *scb_s = &vsie_page->scb_s;
	hpa_t hpa;
	gpa_t gpa;

	hpa = (u64) scb_s->scaoh << 32 | scb_s->scaol;
	if (hpa) {
		gpa = scb_o->scaol & ~0xfUL;
		if (test_kvm_cpu_feat(vcpu->kvm, KVM_S390_VM_CPU_FEAT_64BSCAO))
			gpa |= (u64) scb_o->scaoh << 32;
		unpin_guest_page(vcpu->kvm, gpa, hpa);
		scb_s->scaol = 0;
		scb_s->scaoh = 0;
	}

	hpa = scb_s->itdba;
	if (hpa) {
		gpa = scb_o->itdba & ~0xffUL;
		unpin_guest_page(vcpu->kvm, gpa, hpa);
		scb_s->itdba = 0;
	}

	hpa = scb_s->gvrd;
	if (hpa) {
		gpa = scb_o->gvrd & ~0x1ffUL;
		unpin_guest_page(vcpu->kvm, gpa, hpa);
		scb_s->gvrd = 0;
	}

	hpa = scb_s->riccbd;
	if (hpa) {
		gpa = scb_o->riccbd & ~0x3fUL;
		unpin_guest_page(vcpu->kvm, gpa, hpa);
		scb_s->riccbd = 0;
	}
}

/*
 * Instead of shadowing some blocks, we can simply forward them because the
 * addresses in the scb are 64 bit long.
 *
 * This works as long as the data lies in one page. If blocks ever exceed one
 * page, we have to fall back to shadowing.
 *
 * As we reuse the sca, the vcpu pointers contained in it are invalid. We must
 * therefore not enable any facilities that access these pointers (e.g. SIGPIF).
 *
 * Returns: - 0 if all blocks were pinned.
 *          - > 0 if control has to be given to guest 2
 *          - -ENOMEM if out of memory
 */
static int pin_blocks(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page)
{
	struct kvm_s390_sie_block *scb_o = vsie_page->scb_o;
	struct kvm_s390_sie_block *scb_s = &vsie_page->scb_s;
	hpa_t hpa;
	gpa_t gpa;
	int rc = 0;

	gpa = scb_o->scaol & ~0xfUL;
	if (test_kvm_cpu_feat(vcpu->kvm, KVM_S390_VM_CPU_FEAT_64BSCAO))
		gpa |= (u64) scb_o->scaoh << 32;
	if (gpa) {
		if (!(gpa & ~0x1fffUL))
			rc = set_validity_icpt(scb_s, 0x0038U);
		else if ((gpa & ~0x1fffUL) == kvm_s390_get_prefix(vcpu))
			rc = set_validity_icpt(scb_s, 0x0011U);
		else if ((gpa & PAGE_MASK) !=
			 ((gpa + sizeof(struct bsca_block) - 1) & PAGE_MASK))
			rc = set_validity_icpt(scb_s, 0x003bU);
		if (!rc) {
			rc = pin_guest_page(vcpu->kvm, gpa, &hpa);
			if (rc == -EINVAL)
				rc = set_validity_icpt(scb_s, 0x0034U);
		}
		if (rc)
			goto unpin;
		scb_s->scaoh = (u32)((u64)hpa >> 32);
		scb_s->scaol = (u32)(u64)hpa;
	}

	gpa = scb_o->itdba & ~0xffUL;
	if (gpa && (scb_s->ecb & 0x10U)) {
		if (!(gpa & ~0x1fffU)) {
			rc = set_validity_icpt(scb_s, 0x0080U);
			goto unpin;
		}
		/* 256 bytes cannot cross page boundaries */
		rc = pin_guest_page(vcpu->kvm, gpa, &hpa);
		if (rc == -EINVAL)
			rc = set_validity_icpt(scb_s, 0x0080U);
		if (rc)
			goto unpin;
		scb_s->itdba = hpa;
	}

	gpa = scb_o->gvrd & ~0x1ffUL;
	if (gpa && (scb_s->eca & 0x00020000U) &&
	    !(scb_s->ecd & 0x20000000U)) {
		if (!(gpa & ~0x1fffUL)) {
			rc = set_validity_icpt(scb_s, 0x1310U);
			goto unpin;
		}
		/*
		 * 512 bytes vector registers cannot cross page boundaries
		 * if this block gets bigger, we have to shadow it.
		 */
		rc = pin_guest_page(vcpu->kvm, gpa, &hpa);
		if (rc == -EINVAL)
			rc = set_validity_icpt(scb_s, 0x1310U);
		if (rc)
			goto unpin;
		scb_s->gvrd = hpa;
	}

	gpa = scb_o->riccbd & ~0x3fUL;
	if (gpa && (scb_s->ecb3 & 0x01U)) {
		if (!(gpa & ~0x1fffUL)) {
			rc = set_validity_icpt(scb_s, 0x0043U);
			goto unpin;
		}
		/* 64 bytes cannot cross page boundaries */
		rc = pin_guest_page(vcpu->kvm, gpa, &hpa);
		if (rc == -EINVAL)
			rc = set_validity_icpt(scb_s, 0x0043U);
		/* Validity 0x0044 will be checked by SIE */
		if (rc)
			goto unpin;
		scb_s->riccbd = hpa;
	}
	return 0;
unpin:
	unpin_blocks(vcpu, vsie_page);
	return rc;
}

/* unpin the scb provided by guest 2, marking it as dirty */
static void unpin_scb(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page,
		      gpa_t gpa)
{
	hpa_t hpa = (hpa_t) vsie_page->scb_o;

	if (hpa)
		unpin_guest_page(vcpu->kvm, gpa, hpa);
	vsie_page->scb_o = NULL;
}

/*
 * Pin the scb at gpa provided by guest 2 at vsie_page->scb_o.
 *
 * Returns: - 0 if the scb was pinned.
 *          - > 0 if control has to be given to guest 2
 *          - -ENOMEM if out of memory
 */
static int pin_scb(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page,
		   gpa_t gpa)
{
	hpa_t hpa;
	int rc;

	rc = pin_guest_page(vcpu->kvm, gpa, &hpa);
	if (rc == -EINVAL) {
		rc = kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		if (!rc)
			rc = 1;
	}
	if (!rc)
		vsie_page->scb_o = (struct kvm_s390_sie_block *) hpa;
	return rc;
}

/*
 * Inject a fault into guest 2.
 *
 * Returns: - > 0 if control has to be given to guest 2
 *            < 0 if an error occurred during injection.
 */
static int inject_fault(struct kvm_vcpu *vcpu, __u16 code, __u64 vaddr,
			bool write_flag)
{
	struct kvm_s390_pgm_info pgm = {
		.code = code,
		.trans_exc_code =
			/* 0-51: virtual address */
			(vaddr & 0xfffffffffffff000UL) |
			/* 52-53: store / fetch */
			(((unsigned int) !write_flag) + 1) << 10,
			/* 62-63: asce id (alway primary == 0) */
		.exc_access_id = 0, /* always primary */
		.op_access_id = 0, /* not MVPG */
	};
	int rc;

	if (code == PGM_PROTECTION)
		pgm.trans_exc_code |= 0x4UL;

	rc = kvm_s390_inject_prog_irq(vcpu, &pgm);
	return rc ? rc : 1;
}

/*
 * Handle a fault during vsie execution on a gmap shadow.
 *
 * Returns: - 0 if the fault was resolved
 *          - > 0 if control has to be given to guest 2
 *          - < 0 if an error occurred
 */
static int handle_fault(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page)
{
	int rc;

	if (current->thread.gmap_int_code == PGM_PROTECTION)
		/* we can directly forward all protection exceptions */
		return inject_fault(vcpu, PGM_PROTECTION,
				    current->thread.gmap_addr, 1);

	rc = kvm_s390_shadow_fault(vcpu, vsie_page->gmap,
				   current->thread.gmap_addr);
	if (rc > 0) {
		rc = inject_fault(vcpu, rc,
				  current->thread.gmap_addr,
				  current->thread.gmap_write_flag);
		if (rc >= 0)
			vsie_page->fault_addr = current->thread.gmap_addr;
	}
	return rc;
}

/*
 * Retry the previous fault that required guest 2 intervention. This avoids
 * one superfluous SIE re-entry and direct exit.
 *
 * Will ignore any errors. The next SIE fault will do proper fault handling.
 */
static void handle_last_fault(struct kvm_vcpu *vcpu,
			      struct vsie_page *vsie_page)
{
	if (vsie_page->fault_addr)
		kvm_s390_shadow_fault(vcpu, vsie_page->gmap,
				      vsie_page->fault_addr);
	vsie_page->fault_addr = 0;
}

static inline void clear_vsie_icpt(struct vsie_page *vsie_page)
{
	vsie_page->scb_s.icptcode = 0;
}

/* rewind the psw and clear the vsie icpt, so we can retry execution */
static void retry_vsie_icpt(struct vsie_page *vsie_page)
{
	struct kvm_s390_sie_block *scb_s = &vsie_page->scb_s;
	int ilen = insn_length(scb_s->ipa >> 8);

	/* take care of EXECUTE instructions */
	if (scb_s->icptstatus & 1) {
		ilen = (scb_s->icptstatus >> 4) & 0x6;
		if (!ilen)
			ilen = 4;
	}
	scb_s->gpsw.addr = __rewind_psw(scb_s->gpsw, ilen);
	clear_vsie_icpt(vsie_page);
}

/*
 * Try to shadow + enable the guest 2 provided facility list.
 * Retry instruction execution if enabled for and provided by guest 2.
 *
 * Returns: - 0 if handled (retry or guest 2 icpt)
 *          - > 0 if control has to be given to guest 2
 */
static int handle_stfle(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page)
{
	struct kvm_s390_sie_block *scb_s = &vsie_page->scb_s;
	__u32 fac = vsie_page->scb_o->fac & 0x7ffffff8U;

	if (fac && test_kvm_facility(vcpu->kvm, 7)) {
		retry_vsie_icpt(vsie_page);
		if (read_guest_real(vcpu, fac, &vsie_page->fac,
				    sizeof(vsie_page->fac)))
			return set_validity_icpt(scb_s, 0x1090U);
		scb_s->fac = (__u32)(__u64) &vsie_page->fac;
	}
	return 0;
}

/*
 * Run the vsie on a shadow scb and a shadow gmap, without any further
 * sanity checks, handling SIE faults.
 *
 * Returns: - 0 everything went fine
 *          - > 0 if control has to be given to guest 2
 *          - < 0 if an error occurred
 */
static int do_vsie_run(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page)
{
	struct kvm_s390_sie_block *scb_s = &vsie_page->scb_s;
	struct kvm_s390_sie_block *scb_o = vsie_page->scb_o;
	int rc;

	handle_last_fault(vcpu, vsie_page);

	if (need_resched())
		schedule();
	if (test_cpu_flag(CIF_MCCK_PENDING))
		s390_handle_mcck();

	srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);
	local_irq_disable();
	guest_enter_irqoff();
	local_irq_enable();

	rc = sie64a(scb_s, vcpu->run->s.regs.gprs);

	local_irq_disable();
	guest_exit_irqoff();
	local_irq_enable();
	vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);

	if (rc > 0)
		rc = 0; /* we could still have an icpt */
	else if (rc == -EFAULT)
		return handle_fault(vcpu, vsie_page);

	switch (scb_s->icptcode) {
	case ICPT_INST:
		if (scb_s->ipa == 0xb2b0)
			rc = handle_stfle(vcpu, vsie_page);
		break;
	case ICPT_STOP:
		/* stop not requested by g2 - must have been a kick */
		if (!(atomic_read(&scb_o->cpuflags) & CPUSTAT_STOP_INT))
			clear_vsie_icpt(vsie_page);
		break;
	case ICPT_VALIDITY:
		if ((scb_s->ipa & 0xf000) != 0xf000)
			scb_s->ipa += 0x1000;
		break;
	}
	return rc;
}

static void release_gmap_shadow(struct vsie_page *vsie_page)
{
	if (vsie_page->gmap)
		gmap_put(vsie_page->gmap);
	WRITE_ONCE(vsie_page->gmap, NULL);
	prefix_unmapped(vsie_page);
}

static int acquire_gmap_shadow(struct kvm_vcpu *vcpu,
			       struct vsie_page *vsie_page)
{
	unsigned long asce;
	union ctlreg0 cr0;
	struct gmap *gmap;
	int edat;

	asce = vcpu->arch.sie_block->gcr[1];
	cr0.val = vcpu->arch.sie_block->gcr[0];
	edat = cr0.edat && test_kvm_facility(vcpu->kvm, 8);
	edat += edat && test_kvm_facility(vcpu->kvm, 78);

	/*
	 * ASCE or EDAT could have changed since last icpt, or the gmap
	 * we're holding has been unshadowed. If the gmap is still valid,
	 * we can safely reuse it.
	 */
	if (vsie_page->gmap && gmap_shadow_valid(vsie_page->gmap, asce, edat))
		return 0;

	/* release the old shadow - if any, and mark the prefix as unmapped */
	release_gmap_shadow(vsie_page);
	gmap = gmap_shadow(vcpu->arch.gmap, asce, edat);
	if (IS_ERR(gmap))
		return PTR_ERR(gmap);
	gmap->private = vcpu->kvm;
	WRITE_ONCE(vsie_page->gmap, gmap);
	return 0;
}

/*
 * Register the shadow scb at the VCPU, e.g. for kicking out of vsie.
 */
static void register_shadow_scb(struct kvm_vcpu *vcpu,
				struct vsie_page *vsie_page)
{
	struct kvm_s390_sie_block *scb_s = &vsie_page->scb_s;

	WRITE_ONCE(vcpu->arch.vsie_block, &vsie_page->scb_s);
	/*
	 * External calls have to lead to a kick of the vcpu and
	 * therefore the vsie -> Simulate Wait state.
	 */
	atomic_or(CPUSTAT_WAIT, &vcpu->arch.sie_block->cpuflags);
	/*
	 * We have to adjust the g3 epoch by the g2 epoch. The epoch will
	 * automatically be adjusted on tod clock changes via kvm_sync_clock.
	 */
	preempt_disable();
	scb_s->epoch += vcpu->kvm->arch.epoch;
	preempt_enable();
}

/*
 * Unregister a shadow scb from a VCPU.
 */
static void unregister_shadow_scb(struct kvm_vcpu *vcpu)
{
	atomic_andnot(CPUSTAT_WAIT, &vcpu->arch.sie_block->cpuflags);
	WRITE_ONCE(vcpu->arch.vsie_block, NULL);
}

/*
 * Run the vsie on a shadowed scb, managing the gmap shadow, handling
 * prefix pages and faults.
 *
 * Returns: - 0 if no errors occurred
 *          - > 0 if control has to be given to guest 2
 *          - -ENOMEM if out of memory
 */
static int vsie_run(struct kvm_vcpu *vcpu, struct vsie_page *vsie_page)
{
	struct kvm_s390_sie_block *scb_s = &vsie_page->scb_s;
	int rc = 0;

	while (1) {
		rc = acquire_gmap_shadow(vcpu, vsie_page);
		if (!rc)
			rc = map_prefix(vcpu, vsie_page);
		if (!rc) {
			gmap_enable(vsie_page->gmap);
			update_intervention_requests(vsie_page);
			rc = do_vsie_run(vcpu, vsie_page);
			gmap_enable(vcpu->arch.gmap);
		}
		atomic_andnot(PROG_BLOCK_SIE, &scb_s->prog20);

		if (rc == -EAGAIN)
			rc = 0;
		if (rc || scb_s->icptcode || signal_pending(current) ||
		    kvm_s390_vcpu_has_irq(vcpu, 0))
			break;
	};

	if (rc == -EFAULT) {
		/*
		 * Addressing exceptions are always presentes as intercepts.
		 * As addressing exceptions are suppressing and our guest 3 PSW
		 * points at the responsible instruction, we have to
		 * forward the PSW and set the ilc. If we can't read guest 3
		 * instruction, we can use an arbitrary ilc. Let's always use
		 * ilen = 4 for now, so we can avoid reading in guest 3 virtual
		 * memory. (we could also fake the shadow so the hardware
		 * handles it).
		 */
		scb_s->icptcode = ICPT_PROGI;
		scb_s->iprcc = PGM_ADDRESSING;
		scb_s->pgmilc = 4;
		scb_s->gpsw.addr = __rewind_psw(scb_s->gpsw, 4);
	}
	return rc;
}

/*
 * Get or create a vsie page for a scb address.
 *
 * Returns: - address of a vsie page (cached or new one)
 *          - NULL if the same scb address is already used by another VCPU
 *          - ERR_PTR(-ENOMEM) if out of memory
 */
static struct vsie_page *get_vsie_page(struct kvm *kvm, unsigned long addr)
{
	struct vsie_page *vsie_page;
	struct page *page;
	int nr_vcpus;

	rcu_read_lock();
	page = radix_tree_lookup(&kvm->arch.vsie.addr_to_page, addr >> 9);
	rcu_read_unlock();
	if (page) {
		if (page_ref_inc_return(page) == 2)
			return page_to_virt(page);
		page_ref_dec(page);
	}

	/*
	 * We want at least #online_vcpus shadows, so every VCPU can execute
	 * the VSIE in parallel.
	 */
	nr_vcpus = atomic_read(&kvm->online_vcpus);

	mutex_lock(&kvm->arch.vsie.mutex);
	if (kvm->arch.vsie.page_count < nr_vcpus) {
		page = alloc_page(GFP_KERNEL | __GFP_ZERO | GFP_DMA);
		if (!page) {
			mutex_unlock(&kvm->arch.vsie.mutex);
			return ERR_PTR(-ENOMEM);
		}
		page_ref_inc(page);
		kvm->arch.vsie.pages[kvm->arch.vsie.page_count] = page;
		kvm->arch.vsie.page_count++;
	} else {
		/* reuse an existing entry that belongs to nobody */
		while (true) {
			page = kvm->arch.vsie.pages[kvm->arch.vsie.next];
			if (page_ref_inc_return(page) == 2)
				break;
			page_ref_dec(page);
			kvm->arch.vsie.next++;
			kvm->arch.vsie.next %= nr_vcpus;
		}
		radix_tree_delete(&kvm->arch.vsie.addr_to_page, page->index >> 9);
	}
	page->index = addr;
	/* double use of the same address */
	if (radix_tree_insert(&kvm->arch.vsie.addr_to_page, addr >> 9, page)) {
		page_ref_dec(page);
		mutex_unlock(&kvm->arch.vsie.mutex);
		return NULL;
	}
	mutex_unlock(&kvm->arch.vsie.mutex);

	vsie_page = page_to_virt(page);
	memset(&vsie_page->scb_s, 0, sizeof(struct kvm_s390_sie_block));
	release_gmap_shadow(vsie_page);
	vsie_page->fault_addr = 0;
	vsie_page->scb_s.ihcpu = 0xffffU;
	return vsie_page;
}

/* put a vsie page acquired via get_vsie_page */
static void put_vsie_page(struct kvm *kvm, struct vsie_page *vsie_page)
{
	struct page *page = pfn_to_page(__pa(vsie_page) >> PAGE_SHIFT);

	page_ref_dec(page);
}

int kvm_s390_handle_vsie(struct kvm_vcpu *vcpu)
{
	struct vsie_page *vsie_page;
	unsigned long scb_addr;
	int rc;

	vcpu->stat.instruction_sie++;
	if (!test_kvm_cpu_feat(vcpu->kvm, KVM_S390_VM_CPU_FEAT_SIEF2))
		return -EOPNOTSUPP;
	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	BUILD_BUG_ON(sizeof(struct vsie_page) != 4096);
	scb_addr = kvm_s390_get_base_disp_s(vcpu, NULL);

	/* 512 byte alignment */
	if (unlikely(scb_addr & 0x1ffUL))
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	if (signal_pending(current) || kvm_s390_vcpu_has_irq(vcpu, 0))
		return 0;

	vsie_page = get_vsie_page(vcpu->kvm, scb_addr);
	if (IS_ERR(vsie_page))
		return PTR_ERR(vsie_page);
	else if (!vsie_page)
		/* double use of sie control block - simply do nothing */
		return 0;

	rc = pin_scb(vcpu, vsie_page, scb_addr);
	if (rc)
		goto out_put;
	rc = shadow_scb(vcpu, vsie_page);
	if (rc)
		goto out_unpin_scb;
	rc = pin_blocks(vcpu, vsie_page);
	if (rc)
		goto out_unshadow;
	register_shadow_scb(vcpu, vsie_page);
	rc = vsie_run(vcpu, vsie_page);
	unregister_shadow_scb(vcpu);
	unpin_blocks(vcpu, vsie_page);
out_unshadow:
	unshadow_scb(vcpu, vsie_page);
out_unpin_scb:
	unpin_scb(vcpu, vsie_page, scb_addr);
out_put:
	put_vsie_page(vcpu->kvm, vsie_page);

	return rc < 0 ? rc : 0;
}

/* Init the vsie data structures. To be called when a vm is initialized. */
void kvm_s390_vsie_init(struct kvm *kvm)
{
	mutex_init(&kvm->arch.vsie.mutex);
	INIT_RADIX_TREE(&kvm->arch.vsie.addr_to_page, GFP_KERNEL);
}

/* Destroy the vsie data structures. To be called when a vm is destroyed. */
void kvm_s390_vsie_destroy(struct kvm *kvm)
{
	struct vsie_page *vsie_page;
	struct page *page;
	int i;

	mutex_lock(&kvm->arch.vsie.mutex);
	for (i = 0; i < kvm->arch.vsie.page_count; i++) {
		page = kvm->arch.vsie.pages[i];
		kvm->arch.vsie.pages[i] = NULL;
		vsie_page = page_to_virt(page);
		release_gmap_shadow(vsie_page);
		/* free the radix tree entry */
		radix_tree_delete(&kvm->arch.vsie.addr_to_page, page->index >> 9);
		__free_page(page);
	}
	kvm->arch.vsie.page_count = 0;
	mutex_unlock(&kvm->arch.vsie.mutex);
}

void kvm_s390_vsie_kick(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_sie_block *scb = READ_ONCE(vcpu->arch.vsie_block);

	/*
	 * Even if the VCPU lets go of the shadow sie block reference, it is
	 * still valid in the cache. So we can safely kick it.
	 */
	if (scb) {
		atomic_or(PROG_BLOCK_SIE, &scb->prog20);
		if (scb->prog0c & PROG_IN_SIE)
			atomic_or(CPUSTAT_STOP_INT, &scb->cpuflags);
	}
}
