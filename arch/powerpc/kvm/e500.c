/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu, <yu.liu@freescale.com>
 *
 * Description:
 * This file is derived from arch/powerpc/kvm/44x.c,
 * by Hollis Blanchard <hollisb@us.ibm.com>.
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

#include "../mm/mmu_decl.h"
#include "booke.h"
#include "e500.h"

struct id {
	unsigned long val;
	struct id **pentry;
};

#define NUM_TIDS 256

/*
 * This table provide mappings from:
 * (guestAS,guestTID,guestPR) --> ID of physical cpu
 * guestAS	[0..1]
 * guestTID	[0..255]
 * guestPR	[0..1]
 * ID		[1..255]
 * Each vcpu keeps one vcpu_id_table.
 */
struct vcpu_id_table {
	struct id id[2][NUM_TIDS][2];
};

/*
 * This table provide reversed mappings of vcpu_id_table:
 * ID --> address of vcpu_id_table item.
 * Each physical core has one pcpu_id_table.
 */
struct pcpu_id_table {
	struct id *entry[NUM_TIDS];
};

static DEFINE_PER_CPU(struct pcpu_id_table, pcpu_sids);

/* This variable keeps last used shadow ID on local core.
 * The valid range of shadow ID is [1..255] */
static DEFINE_PER_CPU(unsigned long, pcpu_last_used_sid);

/*
 * Allocate a free shadow id and setup a valid sid mapping in given entry.
 * A mapping is only valid when vcpu_id_table and pcpu_id_table are match.
 *
 * The caller must have preemption disabled, and keep it that way until
 * it has finished with the returned shadow id (either written into the
 * TLB or arch.shadow_pid, or discarded).
 */
static inline int local_sid_setup_one(struct id *entry)
{
	unsigned long sid;
	int ret = -1;

	sid = ++(__get_cpu_var(pcpu_last_used_sid));
	if (sid < NUM_TIDS) {
		__get_cpu_var(pcpu_sids).entry[sid] = entry;
		entry->val = sid;
		entry->pentry = &__get_cpu_var(pcpu_sids).entry[sid];
		ret = sid;
	}

	/*
	 * If sid == NUM_TIDS, we've run out of sids.  We return -1, and
	 * the caller will invalidate everything and start over.
	 *
	 * sid > NUM_TIDS indicates a race, which we disable preemption to
	 * avoid.
	 */
	WARN_ON(sid > NUM_TIDS);

	return ret;
}

/*
 * Check if given entry contain a valid shadow id mapping.
 * An ID mapping is considered valid only if
 * both vcpu and pcpu know this mapping.
 *
 * The caller must have preemption disabled, and keep it that way until
 * it has finished with the returned shadow id (either written into the
 * TLB or arch.shadow_pid, or discarded).
 */
static inline int local_sid_lookup(struct id *entry)
{
	if (entry && entry->val != 0 &&
	    __get_cpu_var(pcpu_sids).entry[entry->val] == entry &&
	    entry->pentry == &__get_cpu_var(pcpu_sids).entry[entry->val])
		return entry->val;
	return -1;
}

/* Invalidate all id mappings on local core -- call with preempt disabled */
static inline void local_sid_destroy_all(void)
{
	__get_cpu_var(pcpu_last_used_sid) = 0;
	memset(&__get_cpu_var(pcpu_sids), 0, sizeof(__get_cpu_var(pcpu_sids)));
}

static void *kvmppc_e500_id_table_alloc(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	vcpu_e500->idt = kzalloc(sizeof(struct vcpu_id_table), GFP_KERNEL);
	return vcpu_e500->idt;
}

static void kvmppc_e500_id_table_free(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	kfree(vcpu_e500->idt);
	vcpu_e500->idt = NULL;
}

/* Map guest pid to shadow.
 * We use PID to keep shadow of current guest non-zero PID,
 * and use PID1 to keep shadow of guest zero PID.
 * So that guest tlbe with TID=0 can be accessed at any time */
static void kvmppc_e500_recalc_shadow_pid(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	preempt_disable();
	vcpu_e500->vcpu.arch.shadow_pid = kvmppc_e500_get_sid(vcpu_e500,
			get_cur_as(&vcpu_e500->vcpu),
			get_cur_pid(&vcpu_e500->vcpu),
			get_cur_pr(&vcpu_e500->vcpu), 1);
	vcpu_e500->vcpu.arch.shadow_pid1 = kvmppc_e500_get_sid(vcpu_e500,
			get_cur_as(&vcpu_e500->vcpu), 0,
			get_cur_pr(&vcpu_e500->vcpu), 1);
	preempt_enable();
}

/* Invalidate all mappings on vcpu */
static void kvmppc_e500_id_table_reset_all(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	memset(vcpu_e500->idt, 0, sizeof(struct vcpu_id_table));

	/* Update shadow pid when mappings are changed */
	kvmppc_e500_recalc_shadow_pid(vcpu_e500);
}

/* Invalidate one ID mapping on vcpu */
static inline void kvmppc_e500_id_table_reset_one(
			       struct kvmppc_vcpu_e500 *vcpu_e500,
			       int as, int pid, int pr)
{
	struct vcpu_id_table *idt = vcpu_e500->idt;

	BUG_ON(as >= 2);
	BUG_ON(pid >= NUM_TIDS);
	BUG_ON(pr >= 2);

	idt->id[as][pid][pr].val = 0;
	idt->id[as][pid][pr].pentry = NULL;

	/* Update shadow pid when mappings are changed */
	kvmppc_e500_recalc_shadow_pid(vcpu_e500);
}

/*
 * Map guest (vcpu,AS,ID,PR) to physical core shadow id.
 * This function first lookup if a valid mapping exists,
 * if not, then creates a new one.
 *
 * The caller must have preemption disabled, and keep it that way until
 * it has finished with the returned shadow id (either written into the
 * TLB or arch.shadow_pid, or discarded).
 */
unsigned int kvmppc_e500_get_sid(struct kvmppc_vcpu_e500 *vcpu_e500,
				 unsigned int as, unsigned int gid,
				 unsigned int pr, int avoid_recursion)
{
	struct vcpu_id_table *idt = vcpu_e500->idt;
	int sid;

	BUG_ON(as >= 2);
	BUG_ON(gid >= NUM_TIDS);
	BUG_ON(pr >= 2);

	sid = local_sid_lookup(&idt->id[as][gid][pr]);

	while (sid <= 0) {
		/* No mapping yet */
		sid = local_sid_setup_one(&idt->id[as][gid][pr]);
		if (sid <= 0) {
			_tlbil_all();
			local_sid_destroy_all();
		}

		/* Update shadow pid when mappings are changed */
		if (!avoid_recursion)
			kvmppc_e500_recalc_shadow_pid(vcpu_e500);
	}

	return sid;
}

unsigned int kvmppc_e500_get_tlb_stid(struct kvm_vcpu *vcpu,
				      struct kvm_book3e_206_tlb_entry *gtlbe)
{
	return kvmppc_e500_get_sid(to_e500(vcpu), get_tlb_ts(gtlbe),
				   get_tlb_tid(gtlbe), get_cur_pr(vcpu), 0);
}

void kvmppc_set_pid(struct kvm_vcpu *vcpu, u32 pid)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	if (vcpu->arch.pid != pid) {
		vcpu_e500->pid[0] = vcpu->arch.pid = pid;
		kvmppc_e500_recalc_shadow_pid(vcpu_e500);
	}
}

/* gtlbe must not be mapped by more than one host tlbe */
void kvmppc_e500_tlbil_one(struct kvmppc_vcpu_e500 *vcpu_e500,
                           struct kvm_book3e_206_tlb_entry *gtlbe)
{
	struct vcpu_id_table *idt = vcpu_e500->idt;
	unsigned int pr, tid, ts, pid;
	u32 val, eaddr;
	unsigned long flags;

	ts = get_tlb_ts(gtlbe);
	tid = get_tlb_tid(gtlbe);

	preempt_disable();

	/* One guest ID may be mapped to two shadow IDs */
	for (pr = 0; pr < 2; pr++) {
		/*
		 * The shadow PID can have a valid mapping on at most one
		 * host CPU.  In the common case, it will be valid on this
		 * CPU, in which case we do a local invalidation of the
		 * specific address.
		 *
		 * If the shadow PID is not valid on the current host CPU,
		 * we invalidate the entire shadow PID.
		 */
		pid = local_sid_lookup(&idt->id[ts][tid][pr]);
		if (pid <= 0) {
			kvmppc_e500_id_table_reset_one(vcpu_e500, ts, tid, pr);
			continue;
		}

		/*
		 * The guest is invalidating a 4K entry which is in a PID
		 * that has a valid shadow mapping on this host CPU.  We
		 * search host TLB to invalidate it's shadow TLB entry,
		 * similar to __tlbil_va except that we need to look in AS1.
		 */
		val = (pid << MAS6_SPID_SHIFT) | MAS6_SAS;
		eaddr = get_tlb_eaddr(gtlbe);

		local_irq_save(flags);

		mtspr(SPRN_MAS6, val);
		asm volatile("tlbsx 0, %[eaddr]" : : [eaddr] "r" (eaddr));
		val = mfspr(SPRN_MAS1);
		if (val & MAS1_VALID) {
			mtspr(SPRN_MAS1, val & ~MAS1_VALID);
			asm volatile("tlbwe");
		}

		local_irq_restore(flags);
	}

	preempt_enable();
}

void kvmppc_e500_tlbil_all(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	kvmppc_e500_id_table_reset_all(vcpu_e500);
}

void kvmppc_mmu_msr_notify(struct kvm_vcpu *vcpu, u32 old_msr)
{
	/* Recalc shadow pid since MSR changes */
	kvmppc_e500_recalc_shadow_pid(to_e500(vcpu));
}

void kvmppc_core_load_host_debugstate(struct kvm_vcpu *vcpu)
{
}

void kvmppc_core_load_guest_debugstate(struct kvm_vcpu *vcpu)
{
}

void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	kvmppc_booke_vcpu_load(vcpu, cpu);

	/* Shadow PID may be expired on local core */
	kvmppc_e500_recalc_shadow_pid(to_e500(vcpu));
}

void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_SPE
	if (vcpu->arch.shadow_msr & MSR_SPE)
		kvmppc_vcpu_disable_spe(vcpu);
#endif

	kvmppc_booke_vcpu_put(vcpu);
}

int kvmppc_core_check_processor_compat(void)
{
	int r;

	if (strcmp(cur_cpu_spec->cpu_name, "e500v2") == 0)
		r = 0;
	else
		r = -ENOTSUPP;

	return r;
}

static void kvmppc_e500_tlb_setup(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	struct kvm_book3e_206_tlb_entry *tlbe;

	/* Insert large initial mapping for guest. */
	tlbe = get_entry(vcpu_e500, 1, 0);
	tlbe->mas1 = MAS1_VALID | MAS1_TSIZE(BOOK3E_PAGESZ_256M);
	tlbe->mas2 = 0;
	tlbe->mas7_3 = E500_TLB_SUPER_PERM_MASK;

	/* 4K map for serial output. Used by kernel wrapper. */
	tlbe = get_entry(vcpu_e500, 1, 1);
	tlbe->mas1 = MAS1_VALID | MAS1_TSIZE(BOOK3E_PAGESZ_4K);
	tlbe->mas2 = (0xe0004500 & 0xFFFFF000) | MAS2_I | MAS2_G;
	tlbe->mas7_3 = (0xe0004500 & 0xFFFFF000) | E500_TLB_SUPER_PERM_MASK;
}

int kvmppc_core_vcpu_setup(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	kvmppc_e500_tlb_setup(vcpu_e500);

	/* Registers init */
	vcpu->arch.pvr = mfspr(SPRN_PVR);
	vcpu_e500->svr = mfspr(SPRN_SVR);

	vcpu->arch.cpu_type = KVM_CPU_E500V2;

	return 0;
}

void kvmppc_core_get_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	sregs->u.e.features |= KVM_SREGS_E_ARCH206_MMU | KVM_SREGS_E_SPE |
	                       KVM_SREGS_E_PM;
	sregs->u.e.impl_id = KVM_SREGS_E_IMPL_FSL;

	sregs->u.e.impl.fsl.features = 0;
	sregs->u.e.impl.fsl.svr = vcpu_e500->svr;
	sregs->u.e.impl.fsl.hid0 = vcpu_e500->hid0;
	sregs->u.e.impl.fsl.mcar = vcpu_e500->mcar;

	sregs->u.e.ivor_high[0] = vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_UNAVAIL];
	sregs->u.e.ivor_high[1] = vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_DATA];
	sregs->u.e.ivor_high[2] = vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_ROUND];
	sregs->u.e.ivor_high[3] =
		vcpu->arch.ivor[BOOKE_IRQPRIO_PERFORMANCE_MONITOR];

	kvmppc_get_sregs_ivor(vcpu, sregs);
	kvmppc_get_sregs_e500_tlb(vcpu, sregs);
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

	if (sregs->u.e.features & KVM_SREGS_E_SPE) {
		vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_UNAVAIL] =
			sregs->u.e.ivor_high[0];
		vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_DATA] =
			sregs->u.e.ivor_high[1];
		vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_ROUND] =
			sregs->u.e.ivor_high[2];
	}

	if (sregs->u.e.features & KVM_SREGS_E_PM) {
		vcpu->arch.ivor[BOOKE_IRQPRIO_PERFORMANCE_MONITOR] =
			sregs->u.e.ivor_high[3];
	}

	return kvmppc_set_sregs_ivor(vcpu, sregs);
}

int kvmppc_get_one_reg(struct kvm_vcpu *vcpu, u64 id,
			union kvmppc_one_reg *val)
{
	int r = kvmppc_get_one_reg_e500_tlb(vcpu, id, val);
	return r;
}

int kvmppc_set_one_reg(struct kvm_vcpu *vcpu, u64 id,
		       union kvmppc_one_reg *val)
{
	int r = kvmppc_get_one_reg_e500_tlb(vcpu, id, val);
	return r;
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
	err = kvm_vcpu_init(vcpu, kvm, id);
	if (err)
		goto free_vcpu;

	if (kvmppc_e500_id_table_alloc(vcpu_e500) == NULL)
		goto uninit_vcpu;

	err = kvmppc_e500_tlb_init(vcpu_e500);
	if (err)
		goto uninit_id;

	vcpu->arch.shared = (void*)__get_free_page(GFP_KERNEL|__GFP_ZERO);
	if (!vcpu->arch.shared)
		goto uninit_tlb;

	return vcpu;

uninit_tlb:
	kvmppc_e500_tlb_uninit(vcpu_e500);
uninit_id:
	kvmppc_e500_id_table_free(vcpu_e500);
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
	kvmppc_e500_id_table_free(vcpu_e500);
	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, vcpu_e500);
}

int kvmppc_core_init_vm(struct kvm *kvm)
{
	return 0;
}

void kvmppc_core_destroy_vm(struct kvm *kvm)
{
}

static int __init kvmppc_e500_init(void)
{
	int r, i;
	unsigned long ivor[3];
	/* Process remaining handlers above the generic first 16 */
	unsigned long *handler = &kvmppc_booke_handler_addr[16];
	unsigned long handler_len;
	unsigned long max_ivor = 0;

	r = kvmppc_core_check_processor_compat();
	if (r)
		return r;

	r = kvmppc_booke_init();
	if (r)
		return r;

	/* copy extra E500 exception handlers */
	ivor[0] = mfspr(SPRN_IVOR32);
	ivor[1] = mfspr(SPRN_IVOR33);
	ivor[2] = mfspr(SPRN_IVOR34);
	for (i = 0; i < 3; i++) {
		if (ivor[i] > ivor[max_ivor])
			max_ivor = i;

		handler_len = handler[i + 1] - handler[i];
		memcpy((void *)kvmppc_booke_handlers + ivor[i],
		       (void *)handler[i], handler_len);
	}
	handler_len = handler[max_ivor + 1] - handler[max_ivor];
	flush_icache_range(kvmppc_booke_handlers, kvmppc_booke_handlers +
			   ivor[max_ivor] + handler_len);

	return kvm_init(NULL, sizeof(struct kvmppc_vcpu_e500), 0, THIS_MODULE);
}

static void __exit kvmppc_e500_exit(void)
{
	kvmppc_booke_exit();
}

module_init(kvmppc_e500_init);
module_exit(kvmppc_e500_exit);
