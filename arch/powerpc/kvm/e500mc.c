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
#include <linux/miscdevice.h>
#include <linux/module.h>

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

	preempt_disable();
	tag = PPC_DBELL_LPID(get_lpid(vcpu)) | vcpu->vcpu_id;
	mb();
	ppc_msgsnd(dbell_type, 0, tag);
	preempt_enable();
}

/* gtlbe must not be mapped by more than one host tlb entry */
void kvmppc_e500_tlbil_one(struct kvmppc_vcpu_e500 *vcpu_e500,
			   struct kvm_book3e_206_tlb_entry *gtlbe)
{
	unsigned int tid, ts;
	gva_t eaddr;
	u32 val;
	unsigned long flags;

	ts = get_tlb_ts(gtlbe);
	tid = get_tlb_tid(gtlbe);

	/* We search the host TLB to invalidate its shadow TLB entry */
	val = (tid << 16) | ts;
	eaddr = get_tlb_eaddr(gtlbe);

	local_irq_save(flags);

	mtspr(SPRN_MAS6, val);
	mtspr(SPRN_MAS5, MAS5_SGS | get_lpid(&vcpu_e500->vcpu));

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
	mtspr(SPRN_MAS5, MAS5_SGS | get_lpid(&vcpu_e500->vcpu));
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

/* We use two lpids per VM */
static DEFINE_PER_CPU(struct kvm_vcpu *[KVMPPC_NR_LPIDS], last_vcpu_of_lpid);

static void kvmppc_core_vcpu_load_e500mc(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	kvmppc_booke_vcpu_load(vcpu, cpu);

	mtspr(SPRN_LPID, get_lpid(vcpu));
	mtspr(SPRN_EPCR, vcpu->arch.shadow_epcr);
	mtspr(SPRN_GPIR, vcpu->vcpu_id);
	mtspr(SPRN_MSRP, vcpu->arch.shadow_msrp);
	vcpu->arch.eplc = EPC_EGS | (get_lpid(vcpu) << EPC_ELPID_SHIFT);
	vcpu->arch.epsc = vcpu->arch.eplc;
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

	if (vcpu->arch.oldpir != mfspr(SPRN_PIR) ||
	    __this_cpu_read(last_vcpu_of_lpid[get_lpid(vcpu)]) != vcpu) {
		kvmppc_e500_tlbil_all(vcpu_e500);
		__this_cpu_write(last_vcpu_of_lpid[get_lpid(vcpu)], vcpu);
	}
}

static void kvmppc_core_vcpu_put_e500mc(struct kvm_vcpu *vcpu)
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
#ifdef CONFIG_ALTIVEC
	/*
	 * Since guests have the privilege to enable AltiVec, we need AltiVec
	 * support in the host to save/restore their context.
	 * Don't use CPU_FTR_ALTIVEC to identify cores with AltiVec unit
	 * because it's cleared in the absence of CONFIG_ALTIVEC!
	 */
	else if (strcmp(cur_cpu_spec->cpu_name, "e6500") == 0)
		r = 0;
#endif
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
	vcpu->arch.shadow_msrp = MSRP_UCLEP | MSRP_PMMP;

	vcpu->arch.pvr = mfspr(SPRN_PVR);
	vcpu_e500->svr = mfspr(SPRN_SVR);

	vcpu->arch.cpu_type = KVM_CPU_E500MC;

	return 0;
}

static int kvmppc_core_get_sregs_e500mc(struct kvm_vcpu *vcpu,
					struct kvm_sregs *sregs)
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

	return kvmppc_get_sregs_ivor(vcpu, sregs);
}

static int kvmppc_core_set_sregs_e500mc(struct kvm_vcpu *vcpu,
					struct kvm_sregs *sregs)
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

static int kvmppc_get_one_reg_e500mc(struct kvm_vcpu *vcpu, u64 id,
			      union kvmppc_one_reg *val)
{
	int r = 0;

	switch (id) {
	case KVM_REG_PPC_SPRG9:
		*val = get_reg_val(id, vcpu->arch.sprg9);
		break;
	default:
		r = kvmppc_get_one_reg_e500_tlb(vcpu, id, val);
	}

	return r;
}

static int kvmppc_set_one_reg_e500mc(struct kvm_vcpu *vcpu, u64 id,
			      union kvmppc_one_reg *val)
{
	int r = 0;

	switch (id) {
	case KVM_REG_PPC_SPRG9:
		vcpu->arch.sprg9 = set_reg_val(id, *val);
		break;
	default:
		r = kvmppc_set_one_reg_e500_tlb(vcpu, id, val);
	}

	return r;
}

static struct kvm_vcpu *kvmppc_core_vcpu_create_e500mc(struct kvm *kvm,
						       unsigned int id)
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
	if (!vcpu->arch.shared) {
		err = -ENOMEM;
		goto uninit_tlb;
	}

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

static void kvmppc_core_vcpu_free_e500mc(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	free_page((unsigned long)vcpu->arch.shared);
	kvmppc_e500_tlb_uninit(vcpu_e500);
	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, vcpu_e500);
}

static int kvmppc_core_init_vm_e500mc(struct kvm *kvm)
{
	int lpid;

	lpid = kvmppc_alloc_lpid();
	if (lpid < 0)
		return lpid;

	/*
	 * Use two lpids per VM on cores with two threads like e6500. Use
	 * even numbers to speedup vcpu lpid computation with consecutive lpids
	 * per VM. vm1 will use lpids 2 and 3, vm2 lpids 4 and 5, and so on.
	 */
	if (threads_per_core == 2)
		lpid <<= 1;

	kvm->arch.lpid = lpid;
	return 0;
}

static void kvmppc_core_destroy_vm_e500mc(struct kvm *kvm)
{
	int lpid = kvm->arch.lpid;

	if (threads_per_core == 2)
		lpid >>= 1;

	kvmppc_free_lpid(lpid);
}

static struct kvmppc_ops kvm_ops_e500mc = {
	.get_sregs = kvmppc_core_get_sregs_e500mc,
	.set_sregs = kvmppc_core_set_sregs_e500mc,
	.get_one_reg = kvmppc_get_one_reg_e500mc,
	.set_one_reg = kvmppc_set_one_reg_e500mc,
	.vcpu_load   = kvmppc_core_vcpu_load_e500mc,
	.vcpu_put    = kvmppc_core_vcpu_put_e500mc,
	.vcpu_create = kvmppc_core_vcpu_create_e500mc,
	.vcpu_free   = kvmppc_core_vcpu_free_e500mc,
	.mmu_destroy  = kvmppc_mmu_destroy_e500,
	.init_vm = kvmppc_core_init_vm_e500mc,
	.destroy_vm = kvmppc_core_destroy_vm_e500mc,
	.emulate_op = kvmppc_core_emulate_op_e500,
	.emulate_mtspr = kvmppc_core_emulate_mtspr_e500,
	.emulate_mfspr = kvmppc_core_emulate_mfspr_e500,
};

static int __init kvmppc_e500mc_init(void)
{
	int r;

	r = kvmppc_booke_init();
	if (r)
		goto err_out;

	/*
	 * Use two lpids per VM on dual threaded processors like e6500
	 * to workarround the lack of tlb write conditional instruction.
	 * Expose half the number of available hardware lpids to the lpid
	 * allocator.
	 */
	kvmppc_init_lpid(KVMPPC_NR_LPIDS/threads_per_core);
	kvmppc_claim_lpid(0); /* host */

	r = kvm_init(NULL, sizeof(struct kvmppc_vcpu_e500), 0, THIS_MODULE);
	if (r)
		goto err_out;
	kvm_ops_e500mc.owner = THIS_MODULE;
	kvmppc_pr_ops = &kvm_ops_e500mc;

err_out:
	return r;
}

static void __exit kvmppc_e500mc_exit(void)
{
	kvmppc_pr_ops = NULL;
	kvmppc_booke_exit();
}

module_init(kvmppc_e500mc_init);
module_exit(kvmppc_e500mc_exit);
MODULE_ALIAS_MISCDEV(KVM_MINOR);
MODULE_ALIAS("devname:kvm");
