/*
 * Copyright (C) 2010,2012 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Varun Sethi, <varun.sethi@freescale.com>
 *
 * Description:
 * This file is derived from arch/powerpc/kvm/e500.c,
 * by Yu Liu <yu.liu@freescale.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kvm_host.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/export.h>

#include <asm/reg.h>
#include <asm/cputable.h>
#include <asm/tlbflush.h>
#include <asm/kvm_ppc.h>
#include <asm/dbell.h>

#include "booke.h"
#include "e500.h"

void kvmppc_set_pending_interrupt(struct kvm_vcpu *vcpu, enum int_class type)
{
	enum ppc_dbell dbell_type;
	unsigned long tag;

	switch (type) {
	case INT_CLASS_NONCRIT:
		dbell_type = PPC_G_DBELL;
		break;
	case INT_CLASS_CRIT:
		dbell_type = PPC_G_DBELL_CRIT;
		break;
	case INT_CLASS_MC:
		dbell_type = PPC_G_DBELL_MC;
		break;
	default:
		WARN_ONCE(1, "%s: unknown int type %d\n", __func__, type);
		return;
	}


	tag = PPC_DBELL_LPID(vcpu->kvm->arch.lpid) | vcpu->vcpu_id;
	mb();
	ppc_msgsnd(dbell_type, 0, tag);
}

/* gtlbe must not be mapped by more than one host tlb entry */
void kvmppc_e500_tlbil_one(struct kvmppc_vcpu_e500 *vcpu_e500,
			   struct kvm_book3e_206_tlb_entry *gtlbe)
{
	unsigned int tid, ts;
	u32 val, eaddr, lpid;
	unsigned long flags;

	ts = get_tlb_ts(gtlbe);
	tid = get_tlb_tid(gtlbe);
	lpid = vcpu_e500->vcpu.kvm->arch.lpid;

	/* We search the host TLB to invalidate its shadow TLB entry */
	val = (tid << 16) | ts;
	eaddr = get_tlb_eaddr(gtlbe);

	local_irq_save(flags);

	mtspr(SPRN_MAS6, val);
	mtspr(SPRN_MAS5, MAS5_SGS | lpid);

	asm volatile("tlbsx 0, %[eaddr]\n" : : [eaddr] "r" (eaddr));
	val = mfspr(SPRN_MAS1);
	if (val & MAS1_VALID) {
		mtspr(SPRN_MAS1, val & ~MAS1_VALID);
		asm volatile("tlbwe");
	}
	mtspr(SPRN_MAS5, 0);
	/* NOTE: tlbsx also updates mas8, so clear it for host tlbwe */
	mtspr(SPRN_MAS8, 0);
	isync();

	local_irq_restore(flags);
}

void kvmppc_e500_tlbil_all(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	unsigned long flags;

	local_irq_save(flags);
	mtspr(SPRN_MAS5, MAS5_SGS | vcpu_e500->vcpu.kvm->arch.lpid);
	asm volatile("tlbilxlpid");
	mtspr(SPRN_MAS5, 0);
	local_irq_restore(flags);
}

void kvmppc_set_pid(struct kvm_vcpu *vcpu, u32 pid)
{
	vcpu->arch.pid = pid;
}

void kvmppc_mmu_msr_notify(struct kvm_vcpu *vcpu, u32 old_msr)
{
}

void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	kvmppc_booke_vcpu_load(vcpu, cpu);

	mtspr(SPRN_LPID, vcpu->kvm->arch.lpid);
	mtspr(SPRN_EPCR, vcpu->arch.shadow_epcr);
	mtspr(SPRN_GPIR, vcpu->vcpu_id);
	mtspr(SPRN_MSRP, vcpu->arch.shadow_msrp);
	mtspr(SPRN_EPLC, vcpu->arch.eplc);
	mtspr(SPRN_EPSC, vcpu->arch.epsc);

	mtspr(SPRN_GIVPR, vcpu->arch.ivpr);
	mtspr(SPRN_GIVOR2, vcpu->arch.ivor[BOOKE_IRQPRIO_DATA_STORAGE]);
	mtspr(SPRN_GIVOR8, vcpu->arch.ivor[BOOKE_IRQPRIO_SYSCALL]);
	mtspr(SPRN_GSPRG0, (unsigned long)vcpu->arch.shared->sprg0);
	mtspr(SPRN_GSPRG1, (unsigned long)vcpu->arch.shared->sprg1);
	mtspr(SPRN_GSPRG2, (unsigned long)vcpu->arch.shared->sprg2);
	mtspr(SPRN_GSPRG3, (unsigned long)vcpu->arch.shared->sprg3);

	mtspr(SPRN_GSRR0, vcpu->arch.shared->srr0);
	mtspr(SPRN_GSRR1, vcpu->arch.shared->srr1);

	mtspr(SPRN_GEPR, vcpu->arch.epr);
	mtspr(SPRN_GDEAR, vcpu->arch.shared->dar);
	mtspr(SPRN_GESR, vcpu->arch.shared->esr);

	if (vcpu->arch.oldpir != mfspr(SPRN_PIR))
		kvmppc_e500_tlbil_all(vcpu_e500);

	kvmppc_load_guest_fp(vcpu);
}

void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu)
{
	vcpu->arch.eplc = mfspr(SPRN_EPLC);
	vcpu->arch.epsc = mfspr(SPRN_EPSC);

	vcpu->arch.shared->sprg0 = mfspr(SPRN_GSPRG0);
	vcpu->arch.shared->sprg1 = mfspr(SPRN_GSPRG1);
	vcpu->arch.shared->sprg2 = mfspr(SPRN_GSPRG2);
	vcpu->arch.shared->sprg3 = mfspr(SPRN_GSPRG3);

	vcpu->arch.shared->srr0 = mfspr(SPRN_GSRR0);
	vcpu->arch.shared->srr1 = mfspr(SPRN_GSRR1);

	vcpu->arch.epr = mfspr(SPRN_GEPR);
	vcpu->arch.shared->dar = mfspr(SPRN_GDEAR);
	vcpu->arch.shared->esr = mfspr(SPRN_GESR);

	vcpu->arch.oldpir = mfspr(SPRN_PIR);

	kvmppc_booke_vcpu_put(vcpu);
}

int kvmppc_core_check_processor_compat(void)
{
	int r;

	if (strcmp(cur_cpu_spec->cpu_name, "e500mc") == 0)
		r = 0;
	else if (strcmp(cur_cpu_spec->cpu_name, "e5500") == 0)
		r = 0;
	else
		r = -ENOTSUPP;

	return r;
}

int kvmppc_core_vcpu_setup(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	vcpu->arch.shadow_epcr = SPRN_EPCR_DSIGS | SPRN_EPCR_DGTMI | \
				 SPRN_EPCR_DUVD;
#ifdef CONFIG_64BIT
	vcpu->arch.shadow_epcr |= SPRN_EPCR_ICM;
#endif
	vcpu->arch.shadow_msrp = MSRP_UCLEP | MSRP_DEP | MSRP_PMMP;
	vcpu->arch.eplc = EPC_EGS | (vcpu->kvm->arch.lpid << EPC_ELPID_SHIFT);
	vcpu->arch.epsc = vcpu->arch.eplc;

	vcpu->arch.pvr = mfspr(SPRN_PVR);
	vcpu_e500->svr = mfspr(SPRN_SVR);

	vcpu->arch.cpu_type = KVM_CPU_E500MC;

	return 0;
}

void kvmppc_core_get_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	sregs->u.e.features |= KVM_SREGS_E_ARCH206_MMU | KVM_SREGS_E_PM |
			       KVM_SREGS_E_PC;
	sregs->u.e.impl_id = KVM_SREGS_E_IMPL_FSL;

	sregs->u.e.impl.fsl.features = 0;
	sregs->u.e.impl.fsl.svr = vcpu_e500->svr;
	sregs->u.e.impl.fsl.hid0 = vcpu_e500->hid0;
	sregs->u.e.impl.fsl.mcar = vcpu_e500->mcar;

	kvmppc_get_sregs_e500_tlb(vcpu, sregs);

	sregs->u.e.ivor_high[3] =
		vcpu->arch.ivor[BOOKE_IRQPRIO_PERFORMANCE_MONITOR];
	sregs->u.e.ivor_high[4] = vcpu->arch.ivor[BOOKE_IRQPRIO_DBELL];
	sregs->u.e.ivor_high[5] = vcpu->arch.ivor[BOOKE_IRQPRIO_DBELL_CRIT];

	kvmppc_get_sregs_ivor(vcpu, sregs);
}

int kvmppc_core_set_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int ret;

	if (sregs->u.e.impl_id == KVM_SREGS_E_IMPL_FSL) {
		vcpu_e500->svr = sregs->u.e.impl.fsl.svr;
		vcpu_e500->hid0 = sregs->u.e.impl.fsl.hid0;
		vcpu_e500->mcar = sregs->u.e.impl.fsl.mcar;
	}

	ret = kvmppc_set_sregs_e500_tlb(vcpu, sregs);
	if (ret < 0)
		return ret;

	if (!(sregs->u.e.features & KVM_SREGS_E_IVOR))
		return 0;

	if (sregs->u.e.features & KVM_SREGS_E_PM) {
		vcpu->arch.ivor[BOOKE_IRQPRIO_PERFORMANCE_MONITOR] =
			sregs->u.e.ivor_high[3];
	}

	if (sregs->u.e.features & KVM_SREGS_E_PC) {
		vcpu->arch.ivor[BOOKE_IRQPRIO_DBELL] =
			sregs->u.e.ivor_high[4];
		vcpu->arch.ivor[BOOKE_IRQPRIO_DBELL_CRIT] =
			sregs->u.e.ivor_high[5];
	}

	return kvmppc_set_sregs_ivor(vcpu, sregs);
}

struct kvm_vcpu *kvmppc_core_vcpu_create(struct kvm *kvm, unsigned int id)
{
	struct kvmppc_vcpu_e500 *vcpu_e500;
	struct kvm_vcpu *vcpu;
	int err;

	vcpu_e500 = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL);
	if (!vcpu_e500) {
		err = -ENOMEM;
		goto out;
	}
	vcpu = &vcpu_e500->vcpu;

	/* Invalid PIR value -- this LPID dosn't have valid state on any cpu */
	vcpu->arch.oldpir = 0xffffffff;

	err = kvm_vcpu_init(vcpu, kvm, id);
	if (err)
		goto free_vcpu;

	err = kvmppc_e500_tlb_init(vcpu_e500);
	if (err)
		goto uninit_vcpu;

	vcpu->arch.shared = (void *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
	if (!vcpu->arch.shared)
		goto uninit_tlb;

	return vcpu;

uninit_tlb:
	kvmppc_e500_tlb_uninit(vcpu_e500);
uninit_vcpu:
	kvm_vcpu_uninit(vcpu);

free_vcpu:
	kmem_cache_free(kvm_vcpu_cache, vcpu_e500);
out:
	return ERR_PTR(err);
}

void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	free_page((unsigned long)vcpu->arch.shared);
	kvmppc_e500_tlb_uninit(vcpu_e500);
	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, vcpu_e500);
}

int kvmppc_core_init_vm(struct kvm *kvm)
{
	int lpid;

	lpid = kvmppc_alloc_lpid();
	if (lpid < 0)
		return lpid;

	kvm->arch.lpid = lpid;
	return 0;
}

void kvmppc_core_destroy_vm(struct kvm *kvm)
{
	kvmppc_free_lpid(kvm->arch.lpid);
}

static int __init kvmppc_e500mc_init(void)
{
	int r;

	r = kvmppc_booke_init();
	if (r)
		return r;

	kvmppc_init_lpid(64);
	kvmppc_claim_lpid(0); /* host */

	return kvm_init(NULL, sizeof(struct kvmppc_vcpu_e500), 0, THIS_MODULE);
}

static void __exit kvmppc_e500mc_exit(void)
{
	kvmppc_booke_exit();
}

module_init(kvmppc_e500mc_init);
module_exit(kvmppc_e500mc_exit);
