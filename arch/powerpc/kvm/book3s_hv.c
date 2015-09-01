/*
 * Copyright 2011 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 * Copyright (C) 2009. SUSE Linux Products GmbH. All rights reserved.
 *
 * Authors:
 *    Paul Mackerras <paulus@au1.ibm.com>
 *    Alexander Graf <agraf@suse.de>
 *    Kevin Wolf <mail@kevin-wolf.de>
 *
 * Description: KVM functions specific to running on Book 3S
 * processors in hypervisor mode (specifically POWER7 and later).
 *
 * This file is derived from arch/powerpc/kvm/book3s.c,
 * by Alexander Graf <agraf@suse.de>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kvm_host.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <linux/page-flags.h>
#include <linux/srcu.h>
#include <linux/miscdevice.h>
#include <linux/debugfs.h>

#include <asm/reg.h>
#include <asm/cputable.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu_context.h>
#include <asm/lppaca.h>
#include <asm/processor.h>
#include <asm/cputhreads.h>
#include <asm/page.h>
#include <asm/hvcall.h>
#include <asm/switch_to.h>
#include <asm/smp.h>
#include <asm/dbell.h>
#include <linux/gfp.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/module.h>

#include "book3s.h"

#define CREATE_TRACE_POINTS
#include "trace_hv.h"

/* #define EXIT_DEBUG */
/* #define EXIT_DEBUG_SIMPLE */
/* #define EXIT_DEBUG_INT */

/* Used to indicate that a guest page fault needs to be handled */
#define RESUME_PAGE_FAULT	(RESUME_GUEST | RESUME_FLAG_ARCH1)

/* Used as a "null" value for timebase values */
#define TB_NIL	(~(u64)0)

static DECLARE_BITMAP(default_enabled_hcalls, MAX_HCALL_OPCODE/4 + 1);

#if defined(CONFIG_PPC_64K_PAGES)
#define MPP_BUFFER_ORDER	0
#elif defined(CONFIG_PPC_4K_PAGES)
#define MPP_BUFFER_ORDER	3
#endif


static void kvmppc_end_cede(struct kvm_vcpu *vcpu);
static int kvmppc_hv_setup_htab_rma(struct kvm_vcpu *vcpu);

static bool kvmppc_ipi_thread(int cpu)
{
	/* On POWER8 for IPIs to threads in the same core, use msgsnd */
	if (cpu_has_feature(CPU_FTR_ARCH_207S)) {
		preempt_disable();
		if (cpu_first_thread_sibling(cpu) ==
		    cpu_first_thread_sibling(smp_processor_id())) {
			unsigned long msg = PPC_DBELL_TYPE(PPC_DBELL_SERVER);
			msg |= cpu_thread_in_core(cpu);
			smp_mb();
			__asm__ __volatile__ (PPC_MSGSND(%0) : : "r" (msg));
			preempt_enable();
			return true;
		}
		preempt_enable();
	}

#if defined(CONFIG_PPC_ICP_NATIVE) && defined(CONFIG_SMP)
	if (cpu >= 0 && cpu < nr_cpu_ids && paca[cpu].kvm_hstate.xics_phys) {
		xics_wake_cpu(cpu);
		return true;
	}
#endif

	return false;
}

static void kvmppc_fast_vcpu_kick_hv(struct kvm_vcpu *vcpu)
{
	int cpu = vcpu->cpu;
	wait_queue_head_t *wqp;

	wqp = kvm_arch_vcpu_wq(vcpu);
	if (waitqueue_active(wqp)) {
		wake_up_interruptible(wqp);
		++vcpu->stat.halt_wakeup;
	}

	if (kvmppc_ipi_thread(cpu + vcpu->arch.ptid))
		return;

	/* CPU points to the first thread of the core */
	if (cpu >= 0 && cpu < nr_cpu_ids && cpu_online(cpu))
		smp_send_reschedule(cpu);
}

/*
 * We use the vcpu_load/put functions to measure stolen time.
 * Stolen time is counted as time when either the vcpu is able to
 * run as part of a virtual core, but the task running the vcore
 * is preempted or sleeping, or when the vcpu needs something done
 * in the kernel by the task running the vcpu, but that task is
 * preempted or sleeping.  Those two things have to be counted
 * separately, since one of the vcpu tasks will take on the job
 * of running the core, and the other vcpu tasks in the vcore will
 * sleep waiting for it to do that, but that sleep shouldn't count
 * as stolen time.
 *
 * Hence we accumulate stolen time when the vcpu can run as part of
 * a vcore using vc->stolen_tb, and the stolen time when the vcpu
 * needs its task to do other things in the kernel (for example,
 * service a page fault) in busy_stolen.  We don't accumulate
 * stolen time for a vcore when it is inactive, or for a vcpu
 * when it is in state RUNNING or NOTREADY.  NOTREADY is a bit of
 * a misnomer; it means that the vcpu task is not executing in
 * the KVM_VCPU_RUN ioctl, i.e. it is in userspace or elsewhere in
 * the kernel.  We don't have any way of dividing up that time
 * between time that the vcpu is genuinely stopped, time that
 * the task is actively working on behalf of the vcpu, and time
 * that the task is preempted, so we don't count any of it as
 * stolen.
 *
 * Updates to busy_stolen are protected by arch.tbacct_lock;
 * updates to vc->stolen_tb are protected by the vcore->stoltb_lock
 * lock.  The stolen times are measured in units of timebase ticks.
 * (Note that the != TB_NIL checks below are purely defensive;
 * they should never fail.)
 */

static void kvmppc_core_vcpu_load_hv(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	unsigned long flags;

	/*
	 * We can test vc->runner without taking the vcore lock,
	 * because only this task ever sets vc->runner to this
	 * vcpu, and once it is set to this vcpu, only this task
	 * ever sets it to NULL.
	 */
	if (vc->runner == vcpu && vc->vcore_state != VCORE_INACTIVE) {
		spin_lock_irqsave(&vc->stoltb_lock, flags);
		if (vc->preempt_tb != TB_NIL) {
			vc->stolen_tb += mftb() - vc->preempt_tb;
			vc->preempt_tb = TB_NIL;
		}
		spin_unlock_irqrestore(&vc->stoltb_lock, flags);
	}
	spin_lock_irqsave(&vcpu->arch.tbacct_lock, flags);
	if (vcpu->arch.state == KVMPPC_VCPU_BUSY_IN_HOST &&
	    vcpu->arch.busy_preempt != TB_NIL) {
		vcpu->arch.busy_stolen += mftb() - vcpu->arch.busy_preempt;
		vcpu->arch.busy_preempt = TB_NIL;
	}
	spin_unlock_irqrestore(&vcpu->arch.tbacct_lock, flags);
}

static void kvmppc_core_vcpu_put_hv(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	unsigned long flags;

	if (vc->runner == vcpu && vc->vcore_state != VCORE_INACTIVE) {
		spin_lock_irqsave(&vc->stoltb_lock, flags);
		vc->preempt_tb = mftb();
		spin_unlock_irqrestore(&vc->stoltb_lock, flags);
	}
	spin_lock_irqsave(&vcpu->arch.tbacct_lock, flags);
	if (vcpu->arch.state == KVMPPC_VCPU_BUSY_IN_HOST)
		vcpu->arch.busy_preempt = mftb();
	spin_unlock_irqrestore(&vcpu->arch.tbacct_lock, flags);
}

static void kvmppc_set_msr_hv(struct kvm_vcpu *vcpu, u64 msr)
{
	vcpu->arch.shregs.msr = msr;
	kvmppc_end_cede(vcpu);
}

void kvmppc_set_pvr_hv(struct kvm_vcpu *vcpu, u32 pvr)
{
	vcpu->arch.pvr = pvr;
}

int kvmppc_set_arch_compat(struct kvm_vcpu *vcpu, u32 arch_compat)
{
	unsigned long pcr = 0;
	struct kvmppc_vcore *vc = vcpu->arch.vcore;

	if (arch_compat) {
		switch (arch_compat) {
		case PVR_ARCH_205:
			/*
			 * If an arch bit is set in PCR, all the defined
			 * higher-order arch bits also have to be set.
			 */
			pcr = PCR_ARCH_206 | PCR_ARCH_205;
			break;
		case PVR_ARCH_206:
		case PVR_ARCH_206p:
			pcr = PCR_ARCH_206;
			break;
		case PVR_ARCH_207:
			break;
		default:
			return -EINVAL;
		}

		if (!cpu_has_feature(CPU_FTR_ARCH_207S)) {
			/* POWER7 can't emulate POWER8 */
			if (!(pcr & PCR_ARCH_206))
				return -EINVAL;
			pcr &= ~PCR_ARCH_206;
		}
	}

	spin_lock(&vc->lock);
	vc->arch_compat = arch_compat;
	vc->pcr = pcr;
	spin_unlock(&vc->lock);

	return 0;
}

void kvmppc_dump_regs(struct kvm_vcpu *vcpu)
{
	int r;

	pr_err("vcpu %p (%d):\n", vcpu, vcpu->vcpu_id);
	pr_err("pc  = %.16lx  msr = %.16llx  trap = %x\n",
	       vcpu->arch.pc, vcpu->arch.shregs.msr, vcpu->arch.trap);
	for (r = 0; r < 16; ++r)
		pr_err("r%2d = %.16lx  r%d = %.16lx\n",
		       r, kvmppc_get_gpr(vcpu, r),
		       r+16, kvmppc_get_gpr(vcpu, r+16));
	pr_err("ctr = %.16lx  lr  = %.16lx\n",
	       vcpu->arch.ctr, vcpu->arch.lr);
	pr_err("srr0 = %.16llx srr1 = %.16llx\n",
	       vcpu->arch.shregs.srr0, vcpu->arch.shregs.srr1);
	pr_err("sprg0 = %.16llx sprg1 = %.16llx\n",
	       vcpu->arch.shregs.sprg0, vcpu->arch.shregs.sprg1);
	pr_err("sprg2 = %.16llx sprg3 = %.16llx\n",
	       vcpu->arch.shregs.sprg2, vcpu->arch.shregs.sprg3);
	pr_err("cr = %.8x  xer = %.16lx  dsisr = %.8x\n",
	       vcpu->arch.cr, vcpu->arch.xer, vcpu->arch.shregs.dsisr);
	pr_err("dar = %.16llx\n", vcpu->arch.shregs.dar);
	pr_err("fault dar = %.16lx dsisr = %.8x\n",
	       vcpu->arch.fault_dar, vcpu->arch.fault_dsisr);
	pr_err("SLB (%d entries):\n", vcpu->arch.slb_max);
	for (r = 0; r < vcpu->arch.slb_max; ++r)
		pr_err("  ESID = %.16llx VSID = %.16llx\n",
		       vcpu->arch.slb[r].orige, vcpu->arch.slb[r].origv);
	pr_err("lpcr = %.16lx sdr1 = %.16lx last_inst = %.8x\n",
	       vcpu->arch.vcore->lpcr, vcpu->kvm->arch.sdr1,
	       vcpu->arch.last_inst);
}

struct kvm_vcpu *kvmppc_find_vcpu(struct kvm *kvm, int id)
{
	int r;
	struct kvm_vcpu *v, *ret = NULL;

	mutex_lock(&kvm->lock);
	kvm_for_each_vcpu(r, v, kvm) {
		if (v->vcpu_id == id) {
			ret = v;
			break;
		}
	}
	mutex_unlock(&kvm->lock);
	return ret;
}

static void init_vpa(struct kvm_vcpu *vcpu, struct lppaca *vpa)
{
	vpa->__old_status |= LPPACA_OLD_SHARED_PROC;
	vpa->yield_count = cpu_to_be32(1);
}

static int set_vpa(struct kvm_vcpu *vcpu, struct kvmppc_vpa *v,
		   unsigned long addr, unsigned long len)
{
	/* check address is cacheline aligned */
	if (addr & (L1_CACHE_BYTES - 1))
		return -EINVAL;
	spin_lock(&vcpu->arch.vpa_update_lock);
	if (v->next_gpa != addr || v->len != len) {
		v->next_gpa = addr;
		v->len = addr ? len : 0;
		v->update_pending = 1;
	}
	spin_unlock(&vcpu->arch.vpa_update_lock);
	return 0;
}

/* Length for a per-processor buffer is passed in at offset 4 in the buffer */
struct reg_vpa {
	u32 dummy;
	union {
		__be16 hword;
		__be32 word;
	} length;
};

static int vpa_is_registered(struct kvmppc_vpa *vpap)
{
	if (vpap->update_pending)
		return vpap->next_gpa != 0;
	return vpap->pinned_addr != NULL;
}

static unsigned long do_h_register_vpa(struct kvm_vcpu *vcpu,
				       unsigned long flags,
				       unsigned long vcpuid, unsigned long vpa)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long len, nb;
	void *va;
	struct kvm_vcpu *tvcpu;
	int err;
	int subfunc;
	struct kvmppc_vpa *vpap;

	tvcpu = kvmppc_find_vcpu(kvm, vcpuid);
	if (!tvcpu)
		return H_PARAMETER;

	subfunc = (flags >> H_VPA_FUNC_SHIFT) & H_VPA_FUNC_MASK;
	if (subfunc == H_VPA_REG_VPA || subfunc == H_VPA_REG_DTL ||
	    subfunc == H_VPA_REG_SLB) {
		/* Registering new area - address must be cache-line aligned */
		if ((vpa & (L1_CACHE_BYTES - 1)) || !vpa)
			return H_PARAMETER;

		/* convert logical addr to kernel addr and read length */
		va = kvmppc_pin_guest_page(kvm, vpa, &nb);
		if (va == NULL)
			return H_PARAMETER;
		if (subfunc == H_VPA_REG_VPA)
			len = be16_to_cpu(((struct reg_vpa *)va)->length.hword);
		else
			len = be32_to_cpu(((struct reg_vpa *)va)->length.word);
		kvmppc_unpin_guest_page(kvm, va, vpa, false);

		/* Check length */
		if (len > nb || len < sizeof(struct reg_vpa))
			return H_PARAMETER;
	} else {
		vpa = 0;
		len = 0;
	}

	err = H_PARAMETER;
	vpap = NULL;
	spin_lock(&tvcpu->arch.vpa_update_lock);

	switch (subfunc) {
	case H_VPA_REG_VPA:		/* register VPA */
		if (len < sizeof(struct lppaca))
			break;
		vpap = &tvcpu->arch.vpa;
		err = 0;
		break;

	case H_VPA_REG_DTL:		/* register DTL */
		if (len < sizeof(struct dtl_entry))
			break;
		len -= len % sizeof(struct dtl_entry);

		/* Check that they have previously registered a VPA */
		err = H_RESOURCE;
		if (!vpa_is_registered(&tvcpu->arch.vpa))
			break;

		vpap = &tvcpu->arch.dtl;
		err = 0;
		break;

	case H_VPA_REG_SLB:		/* register SLB shadow buffer */
		/* Check that they have previously registered a VPA */
		err = H_RESOURCE;
		if (!vpa_is_registered(&tvcpu->arch.vpa))
			break;

		vpap = &tvcpu->arch.slb_shadow;
		err = 0;
		break;

	case H_VPA_DEREG_VPA:		/* deregister VPA */
		/* Check they don't still have a DTL or SLB buf registered */
		err = H_RESOURCE;
		if (vpa_is_registered(&tvcpu->arch.dtl) ||
		    vpa_is_registered(&tvcpu->arch.slb_shadow))
			break;

		vpap = &tvcpu->arch.vpa;
		err = 0;
		break;

	case H_VPA_DEREG_DTL:		/* deregister DTL */
		vpap = &tvcpu->arch.dtl;
		err = 0;
		break;

	case H_VPA_DEREG_SLB:		/* deregister SLB shadow buffer */
		vpap = &tvcpu->arch.slb_shadow;
		err = 0;
		break;
	}

	if (vpap) {
		vpap->next_gpa = vpa;
		vpap->len = len;
		vpap->update_pending = 1;
	}

	spin_unlock(&tvcpu->arch.vpa_update_lock);

	return err;
}

static void kvmppc_update_vpa(struct kvm_vcpu *vcpu, struct kvmppc_vpa *vpap)
{
	struct kvm *kvm = vcpu->kvm;
	void *va;
	unsigned long nb;
	unsigned long gpa;

	/*
	 * We need to pin the page pointed to by vpap->next_gpa,
	 * but we can't call kvmppc_pin_guest_page under the lock
	 * as it does get_user_pages() and down_read().  So we
	 * have to drop the lock, pin the page, then get the lock
	 * again and check that a new area didn't get registered
	 * in the meantime.
	 */
	for (;;) {
		gpa = vpap->next_gpa;
		spin_unlock(&vcpu->arch.vpa_update_lock);
		va = NULL;
		nb = 0;
		if (gpa)
			va = kvmppc_pin_guest_page(kvm, gpa, &nb);
		spin_lock(&vcpu->arch.vpa_update_lock);
		if (gpa == vpap->next_gpa)
			break;
		/* sigh... unpin that one and try again */
		if (va)
			kvmppc_unpin_guest_page(kvm, va, gpa, false);
	}

	vpap->update_pending = 0;
	if (va && nb < vpap->len) {
		/*
		 * If it's now too short, it must be that userspace
		 * has changed the mappings underlying guest memory,
		 * so unregister the region.
		 */
		kvmppc_unpin_guest_page(kvm, va, gpa, false);
		va = NULL;
	}
	if (vpap->pinned_addr)
		kvmppc_unpin_guest_page(kvm, vpap->pinned_addr, vpap->gpa,
					vpap->dirty);
	vpap->gpa = gpa;
	vpap->pinned_addr = va;
	vpap->dirty = false;
	if (va)
		vpap->pinned_end = va + vpap->len;
}

static void kvmppc_update_vpas(struct kvm_vcpu *vcpu)
{
	if (!(vcpu->arch.vpa.update_pending ||
	      vcpu->arch.slb_shadow.update_pending ||
	      vcpu->arch.dtl.update_pending))
		return;

	spin_lock(&vcpu->arch.vpa_update_lock);
	if (vcpu->arch.vpa.update_pending) {
		kvmppc_update_vpa(vcpu, &vcpu->arch.vpa);
		if (vcpu->arch.vpa.pinned_addr)
			init_vpa(vcpu, vcpu->arch.vpa.pinned_addr);
	}
	if (vcpu->arch.dtl.update_pending) {
		kvmppc_update_vpa(vcpu, &vcpu->arch.dtl);
		vcpu->arch.dtl_ptr = vcpu->arch.dtl.pinned_addr;
		vcpu->arch.dtl_index = 0;
	}
	if (vcpu->arch.slb_shadow.update_pending)
		kvmppc_update_vpa(vcpu, &vcpu->arch.slb_shadow);
	spin_unlock(&vcpu->arch.vpa_update_lock);
}

/*
 * Return the accumulated stolen time for the vcore up until `now'.
 * The caller should hold the vcore lock.
 */
static u64 vcore_stolen_time(struct kvmppc_vcore *vc, u64 now)
{
	u64 p;
	unsigned long flags;

	spin_lock_irqsave(&vc->stoltb_lock, flags);
	p = vc->stolen_tb;
	if (vc->vcore_state != VCORE_INACTIVE &&
	    vc->preempt_tb != TB_NIL)
		p += now - vc->preempt_tb;
	spin_unlock_irqrestore(&vc->stoltb_lock, flags);
	return p;
}

static void kvmppc_create_dtl_entry(struct kvm_vcpu *vcpu,
				    struct kvmppc_vcore *vc)
{
	struct dtl_entry *dt;
	struct lppaca *vpa;
	unsigned long stolen;
	unsigned long core_stolen;
	u64 now;

	dt = vcpu->arch.dtl_ptr;
	vpa = vcpu->arch.vpa.pinned_addr;
	now = mftb();
	core_stolen = vcore_stolen_time(vc, now);
	stolen = core_stolen - vcpu->arch.stolen_logged;
	vcpu->arch.stolen_logged = core_stolen;
	spin_lock_irq(&vcpu->arch.tbacct_lock);
	stolen += vcpu->arch.busy_stolen;
	vcpu->arch.busy_stolen = 0;
	spin_unlock_irq(&vcpu->arch.tbacct_lock);
	if (!dt || !vpa)
		return;
	memset(dt, 0, sizeof(struct dtl_entry));
	dt->dispatch_reason = 7;
	dt->processor_id = cpu_to_be16(vc->pcpu + vcpu->arch.ptid);
	dt->timebase = cpu_to_be64(now + vc->tb_offset);
	dt->enqueue_to_dispatch_time = cpu_to_be32(stolen);
	dt->srr0 = cpu_to_be64(kvmppc_get_pc(vcpu));
	dt->srr1 = cpu_to_be64(vcpu->arch.shregs.msr);
	++dt;
	if (dt == vcpu->arch.dtl.pinned_end)
		dt = vcpu->arch.dtl.pinned_addr;
	vcpu->arch.dtl_ptr = dt;
	/* order writing *dt vs. writing vpa->dtl_idx */
	smp_wmb();
	vpa->dtl_idx = cpu_to_be64(++vcpu->arch.dtl_index);
	vcpu->arch.dtl.dirty = true;
}

static bool kvmppc_power8_compatible(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.vcore->arch_compat >= PVR_ARCH_207)
		return true;
	if ((!vcpu->arch.vcore->arch_compat) &&
	    cpu_has_feature(CPU_FTR_ARCH_207S))
		return true;
	return false;
}

static int kvmppc_h_set_mode(struct kvm_vcpu *vcpu, unsigned long mflags,
			     unsigned long resource, unsigned long value1,
			     unsigned long value2)
{
	switch (resource) {
	case H_SET_MODE_RESOURCE_SET_CIABR:
		if (!kvmppc_power8_compatible(vcpu))
			return H_P2;
		if (value2)
			return H_P4;
		if (mflags)
			return H_UNSUPPORTED_FLAG_START;
		/* Guests can't breakpoint the hypervisor */
		if ((value1 & CIABR_PRIV) == CIABR_PRIV_HYPER)
			return H_P3;
		vcpu->arch.ciabr  = value1;
		return H_SUCCESS;
	case H_SET_MODE_RESOURCE_SET_DAWR:
		if (!kvmppc_power8_compatible(vcpu))
			return H_P2;
		if (mflags)
			return H_UNSUPPORTED_FLAG_START;
		if (value2 & DABRX_HYP)
			return H_P4;
		vcpu->arch.dawr  = value1;
		vcpu->arch.dawrx = value2;
		return H_SUCCESS;
	default:
		return H_TOO_HARD;
	}
}

static int kvm_arch_vcpu_yield_to(struct kvm_vcpu *target)
{
	struct kvmppc_vcore *vcore = target->arch.vcore;

	/*
	 * We expect to have been called by the real mode handler
	 * (kvmppc_rm_h_confer()) which would have directly returned
	 * H_SUCCESS if the source vcore wasn't idle (e.g. if it may
	 * have useful work to do and should not confer) so we don't
	 * recheck that here.
	 */

	spin_lock(&vcore->lock);
	if (target->arch.state == KVMPPC_VCPU_RUNNABLE &&
	    vcore->vcore_state != VCORE_INACTIVE)
		target = vcore->runner;
	spin_unlock(&vcore->lock);

	return kvm_vcpu_yield_to(target);
}

static int kvmppc_get_yield_count(struct kvm_vcpu *vcpu)
{
	int yield_count = 0;
	struct lppaca *lppaca;

	spin_lock(&vcpu->arch.vpa_update_lock);
	lppaca = (struct lppaca *)vcpu->arch.vpa.pinned_addr;
	if (lppaca)
		yield_count = be32_to_cpu(lppaca->yield_count);
	spin_unlock(&vcpu->arch.vpa_update_lock);
	return yield_count;
}

int kvmppc_pseries_do_hcall(struct kvm_vcpu *vcpu)
{
	unsigned long req = kvmppc_get_gpr(vcpu, 3);
	unsigned long target, ret = H_SUCCESS;
	int yield_count;
	struct kvm_vcpu *tvcpu;
	int idx, rc;

	if (req <= MAX_HCALL_OPCODE &&
	    !test_bit(req/4, vcpu->kvm->arch.enabled_hcalls))
		return RESUME_HOST;

	switch (req) {
	case H_CEDE:
		break;
	case H_PROD:
		target = kvmppc_get_gpr(vcpu, 4);
		tvcpu = kvmppc_find_vcpu(vcpu->kvm, target);
		if (!tvcpu) {
			ret = H_PARAMETER;
			break;
		}
		tvcpu->arch.prodded = 1;
		smp_mb();
		if (vcpu->arch.ceded) {
			if (waitqueue_active(&vcpu->wq)) {
				wake_up_interruptible(&vcpu->wq);
				vcpu->stat.halt_wakeup++;
			}
		}
		break;
	case H_CONFER:
		target = kvmppc_get_gpr(vcpu, 4);
		if (target == -1)
			break;
		tvcpu = kvmppc_find_vcpu(vcpu->kvm, target);
		if (!tvcpu) {
			ret = H_PARAMETER;
			break;
		}
		yield_count = kvmppc_get_gpr(vcpu, 5);
		if (kvmppc_get_yield_count(tvcpu) != yield_count)
			break;
		kvm_arch_vcpu_yield_to(tvcpu);
		break;
	case H_REGISTER_VPA:
		ret = do_h_register_vpa(vcpu, kvmppc_get_gpr(vcpu, 4),
					kvmppc_get_gpr(vcpu, 5),
					kvmppc_get_gpr(vcpu, 6));
		break;
	case H_RTAS:
		if (list_empty(&vcpu->kvm->arch.rtas_tokens))
			return RESUME_HOST;

		idx = srcu_read_lock(&vcpu->kvm->srcu);
		rc = kvmppc_rtas_hcall(vcpu);
		srcu_read_unlock(&vcpu->kvm->srcu, idx);

		if (rc == -ENOENT)
			return RESUME_HOST;
		else if (rc == 0)
			break;

		/* Send the error out to userspace via KVM_RUN */
		return rc;
	case H_LOGICAL_CI_LOAD:
		ret = kvmppc_h_logical_ci_load(vcpu);
		if (ret == H_TOO_HARD)
			return RESUME_HOST;
		break;
	case H_LOGICAL_CI_STORE:
		ret = kvmppc_h_logical_ci_store(vcpu);
		if (ret == H_TOO_HARD)
			return RESUME_HOST;
		break;
	case H_SET_MODE:
		ret = kvmppc_h_set_mode(vcpu, kvmppc_get_gpr(vcpu, 4),
					kvmppc_get_gpr(vcpu, 5),
					kvmppc_get_gpr(vcpu, 6),
					kvmppc_get_gpr(vcpu, 7));
		if (ret == H_TOO_HARD)
			return RESUME_HOST;
		break;
	case H_XIRR:
	case H_CPPR:
	case H_EOI:
	case H_IPI:
	case H_IPOLL:
	case H_XIRR_X:
		if (kvmppc_xics_enabled(vcpu)) {
			ret = kvmppc_xics_hcall(vcpu, req);
			break;
		} /* fallthrough */
	default:
		return RESUME_HOST;
	}
	kvmppc_set_gpr(vcpu, 3, ret);
	vcpu->arch.hcall_needed = 0;
	return RESUME_GUEST;
}

static int kvmppc_hcall_impl_hv(unsigned long cmd)
{
	switch (cmd) {
	case H_CEDE:
	case H_PROD:
	case H_CONFER:
	case H_REGISTER_VPA:
	case H_SET_MODE:
	case H_LOGICAL_CI_LOAD:
	case H_LOGICAL_CI_STORE:
#ifdef CONFIG_KVM_XICS
	case H_XIRR:
	case H_CPPR:
	case H_EOI:
	case H_IPI:
	case H_IPOLL:
	case H_XIRR_X:
#endif
		return 1;
	}

	/* See if it's in the real-mode table */
	return kvmppc_hcall_impl_hv_realmode(cmd);
}

static int kvmppc_emulate_debug_inst(struct kvm_run *run,
					struct kvm_vcpu *vcpu)
{
	u32 last_inst;

	if (kvmppc_get_last_inst(vcpu, INST_GENERIC, &last_inst) !=
					EMULATE_DONE) {
		/*
		 * Fetch failed, so return to guest and
		 * try executing it again.
		 */
		return RESUME_GUEST;
	}

	if (last_inst == KVMPPC_INST_SW_BREAKPOINT) {
		run->exit_reason = KVM_EXIT_DEBUG;
		run->debug.arch.address = kvmppc_get_pc(vcpu);
		return RESUME_HOST;
	} else {
		kvmppc_core_queue_program(vcpu, SRR1_PROGILL);
		return RESUME_GUEST;
	}
}

static int kvmppc_handle_exit_hv(struct kvm_run *run, struct kvm_vcpu *vcpu,
				 struct task_struct *tsk)
{
	int r = RESUME_HOST;

	vcpu->stat.sum_exits++;

	run->exit_reason = KVM_EXIT_UNKNOWN;
	run->ready_for_interrupt_injection = 1;
	switch (vcpu->arch.trap) {
	/* We're good on these - the host merely wanted to get our attention */
	case BOOK3S_INTERRUPT_HV_DECREMENTER:
		vcpu->stat.dec_exits++;
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_EXTERNAL:
	case BOOK3S_INTERRUPT_H_DOORBELL:
		vcpu->stat.ext_intr_exits++;
		r = RESUME_GUEST;
		break;
	/* HMI is hypervisor interrupt and host has handled it. Resume guest.*/
	case BOOK3S_INTERRUPT_HMI:
	case BOOK3S_INTERRUPT_PERFMON:
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_MACHINE_CHECK:
		/*
		 * Deliver a machine check interrupt to the guest.
		 * We have to do this, even if the host has handled the
		 * machine check, because machine checks use SRR0/1 and
		 * the interrupt might have trashed guest state in them.
		 */
		kvmppc_book3s_queue_irqprio(vcpu,
					    BOOK3S_INTERRUPT_MACHINE_CHECK);
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_PROGRAM:
	{
		ulong flags;
		/*
		 * Normally program interrupts are delivered directly
		 * to the guest by the hardware, but we can get here
		 * as a result of a hypervisor emulation interrupt
		 * (e40) getting turned into a 700 by BML RTAS.
		 */
		flags = vcpu->arch.shregs.msr & 0x1f0000ull;
		kvmppc_core_queue_program(vcpu, flags);
		r = RESUME_GUEST;
		break;
	}
	case BOOK3S_INTERRUPT_SYSCALL:
	{
		/* hcall - punt to userspace */
		int i;

		/* hypercall with MSR_PR has already been handled in rmode,
		 * and never reaches here.
		 */

		run->papr_hcall.nr = kvmppc_get_gpr(vcpu, 3);
		for (i = 0; i < 9; ++i)
			run->papr_hcall.args[i] = kvmppc_get_gpr(vcpu, 4 + i);
		run->exit_reason = KVM_EXIT_PAPR_HCALL;
		vcpu->arch.hcall_needed = 1;
		r = RESUME_HOST;
		break;
	}
	/*
	 * We get these next two if the guest accesses a page which it thinks
	 * it has mapped but which is not actually present, either because
	 * it is for an emulated I/O device or because the corresonding
	 * host page has been paged out.  Any other HDSI/HISI interrupts
	 * have been handled already.
	 */
	case BOOK3S_INTERRUPT_H_DATA_STORAGE:
		r = RESUME_PAGE_FAULT;
		break;
	case BOOK3S_INTERRUPT_H_INST_STORAGE:
		vcpu->arch.fault_dar = kvmppc_get_pc(vcpu);
		vcpu->arch.fault_dsisr = 0;
		r = RESUME_PAGE_FAULT;
		break;
	/*
	 * This occurs if the guest executes an illegal instruction.
	 * If the guest debug is disabled, generate a program interrupt
	 * to the guest. If guest debug is enabled, we need to check
	 * whether the instruction is a software breakpoint instruction.
	 * Accordingly return to Guest or Host.
	 */
	case BOOK3S_INTERRUPT_H_EMUL_ASSIST:
		if (vcpu->arch.emul_inst != KVM_INST_FETCH_FAILED)
			vcpu->arch.last_inst = kvmppc_need_byteswap(vcpu) ?
				swab32(vcpu->arch.emul_inst) :
				vcpu->arch.emul_inst;
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_SW_BP) {
			r = kvmppc_emulate_debug_inst(run, vcpu);
		} else {
			kvmppc_core_queue_program(vcpu, SRR1_PROGILL);
			r = RESUME_GUEST;
		}
		break;
	/*
	 * This occurs if the guest (kernel or userspace), does something that
	 * is prohibited by HFSCR.  We just generate a program interrupt to
	 * the guest.
	 */
	case BOOK3S_INTERRUPT_H_FAC_UNAVAIL:
		kvmppc_core_queue_program(vcpu, SRR1_PROGILL);
		r = RESUME_GUEST;
		break;
	default:
		kvmppc_dump_regs(vcpu);
		printk(KERN_EMERG "trap=0x%x | pc=0x%lx | msr=0x%llx\n",
			vcpu->arch.trap, kvmppc_get_pc(vcpu),
			vcpu->arch.shregs.msr);
		run->hw.hardware_exit_reason = vcpu->arch.trap;
		r = RESUME_HOST;
		break;
	}

	return r;
}

static int kvm_arch_vcpu_ioctl_get_sregs_hv(struct kvm_vcpu *vcpu,
					    struct kvm_sregs *sregs)
{
	int i;

	memset(sregs, 0, sizeof(struct kvm_sregs));
	sregs->pvr = vcpu->arch.pvr;
	for (i = 0; i < vcpu->arch.slb_max; i++) {
		sregs->u.s.ppc64.slb[i].slbe = vcpu->arch.slb[i].orige;
		sregs->u.s.ppc64.slb[i].slbv = vcpu->arch.slb[i].origv;
	}

	return 0;
}

static int kvm_arch_vcpu_ioctl_set_sregs_hv(struct kvm_vcpu *vcpu,
					    struct kvm_sregs *sregs)
{
	int i, j;

	/* Only accept the same PVR as the host's, since we can't spoof it */
	if (sregs->pvr != vcpu->arch.pvr)
		return -EINVAL;

	j = 0;
	for (i = 0; i < vcpu->arch.slb_nr; i++) {
		if (sregs->u.s.ppc64.slb[i].slbe & SLB_ESID_V) {
			vcpu->arch.slb[j].orige = sregs->u.s.ppc64.slb[i].slbe;
			vcpu->arch.slb[j].origv = sregs->u.s.ppc64.slb[i].slbv;
			++j;
		}
	}
	vcpu->arch.slb_max = j;

	return 0;
}

static void kvmppc_set_lpcr(struct kvm_vcpu *vcpu, u64 new_lpcr,
		bool preserve_top32)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	u64 mask;

	mutex_lock(&kvm->lock);
	spin_lock(&vc->lock);
	/*
	 * If ILE (interrupt little-endian) has changed, update the
	 * MSR_LE bit in the intr_msr for each vcpu in this vcore.
	 */
	if ((new_lpcr & LPCR_ILE) != (vc->lpcr & LPCR_ILE)) {
		struct kvm_vcpu *vcpu;
		int i;

		kvm_for_each_vcpu(i, vcpu, kvm) {
			if (vcpu->arch.vcore != vc)
				continue;
			if (new_lpcr & LPCR_ILE)
				vcpu->arch.intr_msr |= MSR_LE;
			else
				vcpu->arch.intr_msr &= ~MSR_LE;
		}
	}

	/*
	 * Userspace can only modify DPFD (default prefetch depth),
	 * ILE (interrupt little-endian) and TC (translation control).
	 * On POWER8 userspace can also modify AIL (alt. interrupt loc.)
	 */
	mask = LPCR_DPFD | LPCR_ILE | LPCR_TC;
	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		mask |= LPCR_AIL;

	/* Broken 32-bit version of LPCR must not clear top bits */
	if (preserve_top32)
		mask &= 0xFFFFFFFF;
	vc->lpcr = (vc->lpcr & ~mask) | (new_lpcr & mask);
	spin_unlock(&vc->lock);
	mutex_unlock(&kvm->lock);
}

static int kvmppc_get_one_reg_hv(struct kvm_vcpu *vcpu, u64 id,
				 union kvmppc_one_reg *val)
{
	int r = 0;
	long int i;

	switch (id) {
	case KVM_REG_PPC_DEBUG_INST:
		*val = get_reg_val(id, KVMPPC_INST_SW_BREAKPOINT);
		break;
	case KVM_REG_PPC_HIOR:
		*val = get_reg_val(id, 0);
		break;
	case KVM_REG_PPC_DABR:
		*val = get_reg_val(id, vcpu->arch.dabr);
		break;
	case KVM_REG_PPC_DABRX:
		*val = get_reg_val(id, vcpu->arch.dabrx);
		break;
	case KVM_REG_PPC_DSCR:
		*val = get_reg_val(id, vcpu->arch.dscr);
		break;
	case KVM_REG_PPC_PURR:
		*val = get_reg_val(id, vcpu->arch.purr);
		break;
	case KVM_REG_PPC_SPURR:
		*val = get_reg_val(id, vcpu->arch.spurr);
		break;
	case KVM_REG_PPC_AMR:
		*val = get_reg_val(id, vcpu->arch.amr);
		break;
	case KVM_REG_PPC_UAMOR:
		*val = get_reg_val(id, vcpu->arch.uamor);
		break;
	case KVM_REG_PPC_MMCR0 ... KVM_REG_PPC_MMCRS:
		i = id - KVM_REG_PPC_MMCR0;
		*val = get_reg_val(id, vcpu->arch.mmcr[i]);
		break;
	case KVM_REG_PPC_PMC1 ... KVM_REG_PPC_PMC8:
		i = id - KVM_REG_PPC_PMC1;
		*val = get_reg_val(id, vcpu->arch.pmc[i]);
		break;
	case KVM_REG_PPC_SPMC1 ... KVM_REG_PPC_SPMC2:
		i = id - KVM_REG_PPC_SPMC1;
		*val = get_reg_val(id, vcpu->arch.spmc[i]);
		break;
	case KVM_REG_PPC_SIAR:
		*val = get_reg_val(id, vcpu->arch.siar);
		break;
	case KVM_REG_PPC_SDAR:
		*val = get_reg_val(id, vcpu->arch.sdar);
		break;
	case KVM_REG_PPC_SIER:
		*val = get_reg_val(id, vcpu->arch.sier);
		break;
	case KVM_REG_PPC_IAMR:
		*val = get_reg_val(id, vcpu->arch.iamr);
		break;
	case KVM_REG_PPC_PSPB:
		*val = get_reg_val(id, vcpu->arch.pspb);
		break;
	case KVM_REG_PPC_DPDES:
		*val = get_reg_val(id, vcpu->arch.vcore->dpdes);
		break;
	case KVM_REG_PPC_DAWR:
		*val = get_reg_val(id, vcpu->arch.dawr);
		break;
	case KVM_REG_PPC_DAWRX:
		*val = get_reg_val(id, vcpu->arch.dawrx);
		break;
	case KVM_REG_PPC_CIABR:
		*val = get_reg_val(id, vcpu->arch.ciabr);
		break;
	case KVM_REG_PPC_CSIGR:
		*val = get_reg_val(id, vcpu->arch.csigr);
		break;
	case KVM_REG_PPC_TACR:
		*val = get_reg_val(id, vcpu->arch.tacr);
		break;
	case KVM_REG_PPC_TCSCR:
		*val = get_reg_val(id, vcpu->arch.tcscr);
		break;
	case KVM_REG_PPC_PID:
		*val = get_reg_val(id, vcpu->arch.pid);
		break;
	case KVM_REG_PPC_ACOP:
		*val = get_reg_val(id, vcpu->arch.acop);
		break;
	case KVM_REG_PPC_WORT:
		*val = get_reg_val(id, vcpu->arch.wort);
		break;
	case KVM_REG_PPC_VPA_ADDR:
		spin_lock(&vcpu->arch.vpa_update_lock);
		*val = get_reg_val(id, vcpu->arch.vpa.next_gpa);
		spin_unlock(&vcpu->arch.vpa_update_lock);
		break;
	case KVM_REG_PPC_VPA_SLB:
		spin_lock(&vcpu->arch.vpa_update_lock);
		val->vpaval.addr = vcpu->arch.slb_shadow.next_gpa;
		val->vpaval.length = vcpu->arch.slb_shadow.len;
		spin_unlock(&vcpu->arch.vpa_update_lock);
		break;
	case KVM_REG_PPC_VPA_DTL:
		spin_lock(&vcpu->arch.vpa_update_lock);
		val->vpaval.addr = vcpu->arch.dtl.next_gpa;
		val->vpaval.length = vcpu->arch.dtl.len;
		spin_unlock(&vcpu->arch.vpa_update_lock);
		break;
	case KVM_REG_PPC_TB_OFFSET:
		*val = get_reg_val(id, vcpu->arch.vcore->tb_offset);
		break;
	case KVM_REG_PPC_LPCR:
	case KVM_REG_PPC_LPCR_64:
		*val = get_reg_val(id, vcpu->arch.vcore->lpcr);
		break;
	case KVM_REG_PPC_PPR:
		*val = get_reg_val(id, vcpu->arch.ppr);
		break;
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	case KVM_REG_PPC_TFHAR:
		*val = get_reg_val(id, vcpu->arch.tfhar);
		break;
	case KVM_REG_PPC_TFIAR:
		*val = get_reg_val(id, vcpu->arch.tfiar);
		break;
	case KVM_REG_PPC_TEXASR:
		*val = get_reg_val(id, vcpu->arch.texasr);
		break;
	case KVM_REG_PPC_TM_GPR0 ... KVM_REG_PPC_TM_GPR31:
		i = id - KVM_REG_PPC_TM_GPR0;
		*val = get_reg_val(id, vcpu->arch.gpr_tm[i]);
		break;
	case KVM_REG_PPC_TM_VSR0 ... KVM_REG_PPC_TM_VSR63:
	{
		int j;
		i = id - KVM_REG_PPC_TM_VSR0;
		if (i < 32)
			for (j = 0; j < TS_FPRWIDTH; j++)
				val->vsxval[j] = vcpu->arch.fp_tm.fpr[i][j];
		else {
			if (cpu_has_feature(CPU_FTR_ALTIVEC))
				val->vval = vcpu->arch.vr_tm.vr[i-32];
			else
				r = -ENXIO;
		}
		break;
	}
	case KVM_REG_PPC_TM_CR:
		*val = get_reg_val(id, vcpu->arch.cr_tm);
		break;
	case KVM_REG_PPC_TM_LR:
		*val = get_reg_val(id, vcpu->arch.lr_tm);
		break;
	case KVM_REG_PPC_TM_CTR:
		*val = get_reg_val(id, vcpu->arch.ctr_tm);
		break;
	case KVM_REG_PPC_TM_FPSCR:
		*val = get_reg_val(id, vcpu->arch.fp_tm.fpscr);
		break;
	case KVM_REG_PPC_TM_AMR:
		*val = get_reg_val(id, vcpu->arch.amr_tm);
		break;
	case KVM_REG_PPC_TM_PPR:
		*val = get_reg_val(id, vcpu->arch.ppr_tm);
		break;
	case KVM_REG_PPC_TM_VRSAVE:
		*val = get_reg_val(id, vcpu->arch.vrsave_tm);
		break;
	case KVM_REG_PPC_TM_VSCR:
		if (cpu_has_feature(CPU_FTR_ALTIVEC))
			*val = get_reg_val(id, vcpu->arch.vr_tm.vscr.u[3]);
		else
			r = -ENXIO;
		break;
	case KVM_REG_PPC_TM_DSCR:
		*val = get_reg_val(id, vcpu->arch.dscr_tm);
		break;
	case KVM_REG_PPC_TM_TAR:
		*val = get_reg_val(id, vcpu->arch.tar_tm);
		break;
#endif
	case KVM_REG_PPC_ARCH_COMPAT:
		*val = get_reg_val(id, vcpu->arch.vcore->arch_compat);
		break;
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

static int kvmppc_set_one_reg_hv(struct kvm_vcpu *vcpu, u64 id,
				 union kvmppc_one_reg *val)
{
	int r = 0;
	long int i;
	unsigned long addr, len;

	switch (id) {
	case KVM_REG_PPC_HIOR:
		/* Only allow this to be set to zero */
		if (set_reg_val(id, *val))
			r = -EINVAL;
		break;
	case KVM_REG_PPC_DABR:
		vcpu->arch.dabr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_DABRX:
		vcpu->arch.dabrx = set_reg_val(id, *val) & ~DABRX_HYP;
		break;
	case KVM_REG_PPC_DSCR:
		vcpu->arch.dscr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_PURR:
		vcpu->arch.purr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_SPURR:
		vcpu->arch.spurr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_AMR:
		vcpu->arch.amr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_UAMOR:
		vcpu->arch.uamor = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_MMCR0 ... KVM_REG_PPC_MMCRS:
		i = id - KVM_REG_PPC_MMCR0;
		vcpu->arch.mmcr[i] = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_PMC1 ... KVM_REG_PPC_PMC8:
		i = id - KVM_REG_PPC_PMC1;
		vcpu->arch.pmc[i] = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_SPMC1 ... KVM_REG_PPC_SPMC2:
		i = id - KVM_REG_PPC_SPMC1;
		vcpu->arch.spmc[i] = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_SIAR:
		vcpu->arch.siar = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_SDAR:
		vcpu->arch.sdar = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_SIER:
		vcpu->arch.sier = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_IAMR:
		vcpu->arch.iamr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_PSPB:
		vcpu->arch.pspb = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_DPDES:
		vcpu->arch.vcore->dpdes = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_DAWR:
		vcpu->arch.dawr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_DAWRX:
		vcpu->arch.dawrx = set_reg_val(id, *val) & ~DAWRX_HYP;
		break;
	case KVM_REG_PPC_CIABR:
		vcpu->arch.ciabr = set_reg_val(id, *val);
		/* Don't allow setting breakpoints in hypervisor code */
		if ((vcpu->arch.ciabr & CIABR_PRIV) == CIABR_PRIV_HYPER)
			vcpu->arch.ciabr &= ~CIABR_PRIV;	/* disable */
		break;
	case KVM_REG_PPC_CSIGR:
		vcpu->arch.csigr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TACR:
		vcpu->arch.tacr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TCSCR:
		vcpu->arch.tcscr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_PID:
		vcpu->arch.pid = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_ACOP:
		vcpu->arch.acop = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_WORT:
		vcpu->arch.wort = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_VPA_ADDR:
		addr = set_reg_val(id, *val);
		r = -EINVAL;
		if (!addr && (vcpu->arch.slb_shadow.next_gpa ||
			      vcpu->arch.dtl.next_gpa))
			break;
		r = set_vpa(vcpu, &vcpu->arch.vpa, addr, sizeof(struct lppaca));
		break;
	case KVM_REG_PPC_VPA_SLB:
		addr = val->vpaval.addr;
		len = val->vpaval.length;
		r = -EINVAL;
		if (addr && !vcpu->arch.vpa.next_gpa)
			break;
		r = set_vpa(vcpu, &vcpu->arch.slb_shadow, addr, len);
		break;
	case KVM_REG_PPC_VPA_DTL:
		addr = val->vpaval.addr;
		len = val->vpaval.length;
		r = -EINVAL;
		if (addr && (len < sizeof(struct dtl_entry) ||
			     !vcpu->arch.vpa.next_gpa))
			break;
		len -= len % sizeof(struct dtl_entry);
		r = set_vpa(vcpu, &vcpu->arch.dtl, addr, len);
		break;
	case KVM_REG_PPC_TB_OFFSET:
		/* round up to multiple of 2^24 */
		vcpu->arch.vcore->tb_offset =
			ALIGN(set_reg_val(id, *val), 1UL << 24);
		break;
	case KVM_REG_PPC_LPCR:
		kvmppc_set_lpcr(vcpu, set_reg_val(id, *val), true);
		break;
	case KVM_REG_PPC_LPCR_64:
		kvmppc_set_lpcr(vcpu, set_reg_val(id, *val), false);
		break;
	case KVM_REG_PPC_PPR:
		vcpu->arch.ppr = set_reg_val(id, *val);
		break;
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	case KVM_REG_PPC_TFHAR:
		vcpu->arch.tfhar = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TFIAR:
		vcpu->arch.tfiar = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TEXASR:
		vcpu->arch.texasr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_GPR0 ... KVM_REG_PPC_TM_GPR31:
		i = id - KVM_REG_PPC_TM_GPR0;
		vcpu->arch.gpr_tm[i] = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_VSR0 ... KVM_REG_PPC_TM_VSR63:
	{
		int j;
		i = id - KVM_REG_PPC_TM_VSR0;
		if (i < 32)
			for (j = 0; j < TS_FPRWIDTH; j++)
				vcpu->arch.fp_tm.fpr[i][j] = val->vsxval[j];
		else
			if (cpu_has_feature(CPU_FTR_ALTIVEC))
				vcpu->arch.vr_tm.vr[i-32] = val->vval;
			else
				r = -ENXIO;
		break;
	}
	case KVM_REG_PPC_TM_CR:
		vcpu->arch.cr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_LR:
		vcpu->arch.lr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_CTR:
		vcpu->arch.ctr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_FPSCR:
		vcpu->arch.fp_tm.fpscr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_AMR:
		vcpu->arch.amr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_PPR:
		vcpu->arch.ppr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_VRSAVE:
		vcpu->arch.vrsave_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_VSCR:
		if (cpu_has_feature(CPU_FTR_ALTIVEC))
			vcpu->arch.vr.vscr.u[3] = set_reg_val(id, *val);
		else
			r = - ENXIO;
		break;
	case KVM_REG_PPC_TM_DSCR:
		vcpu->arch.dscr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_TAR:
		vcpu->arch.tar_tm = set_reg_val(id, *val);
		break;
#endif
	case KVM_REG_PPC_ARCH_COMPAT:
		r = kvmppc_set_arch_compat(vcpu, set_reg_val(id, *val));
		break;
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

static struct kvmppc_vcore *kvmppc_vcore_create(struct kvm *kvm, int core)
{
	struct kvmppc_vcore *vcore;

	vcore = kzalloc(sizeof(struct kvmppc_vcore), GFP_KERNEL);

	if (vcore == NULL)
		return NULL;

	INIT_LIST_HEAD(&vcore->runnable_threads);
	spin_lock_init(&vcore->lock);
	spin_lock_init(&vcore->stoltb_lock);
	init_waitqueue_head(&vcore->wq);
	vcore->preempt_tb = TB_NIL;
	vcore->lpcr = kvm->arch.lpcr;
	vcore->first_vcpuid = core * threads_per_subcore;
	vcore->kvm = kvm;

	vcore->mpp_buffer_is_valid = false;

	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		vcore->mpp_buffer = (void *)__get_free_pages(
			GFP_KERNEL|__GFP_ZERO,
			MPP_BUFFER_ORDER);

	return vcore;
}

#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
static struct debugfs_timings_element {
	const char *name;
	size_t offset;
} timings[] = {
	{"rm_entry",	offsetof(struct kvm_vcpu, arch.rm_entry)},
	{"rm_intr",	offsetof(struct kvm_vcpu, arch.rm_intr)},
	{"rm_exit",	offsetof(struct kvm_vcpu, arch.rm_exit)},
	{"guest",	offsetof(struct kvm_vcpu, arch.guest_time)},
	{"cede",	offsetof(struct kvm_vcpu, arch.cede_time)},
};

#define N_TIMINGS	(sizeof(timings) / sizeof(timings[0]))

struct debugfs_timings_state {
	struct kvm_vcpu	*vcpu;
	unsigned int	buflen;
	char		buf[N_TIMINGS * 100];
};

static int debugfs_timings_open(struct inode *inode, struct file *file)
{
	struct kvm_vcpu *vcpu = inode->i_private;
	struct debugfs_timings_state *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	kvm_get_kvm(vcpu->kvm);
	p->vcpu = vcpu;
	file->private_data = p;

	return nonseekable_open(inode, file);
}

static int debugfs_timings_release(struct inode *inode, struct file *file)
{
	struct debugfs_timings_state *p = file->private_data;

	kvm_put_kvm(p->vcpu->kvm);
	kfree(p);
	return 0;
}

static ssize_t debugfs_timings_read(struct file *file, char __user *buf,
				    size_t len, loff_t *ppos)
{
	struct debugfs_timings_state *p = file->private_data;
	struct kvm_vcpu *vcpu = p->vcpu;
	char *s, *buf_end;
	struct kvmhv_tb_accumulator tb;
	u64 count;
	loff_t pos;
	ssize_t n;
	int i, loops;
	bool ok;

	if (!p->buflen) {
		s = p->buf;
		buf_end = s + sizeof(p->buf);
		for (i = 0; i < N_TIMINGS; ++i) {
			struct kvmhv_tb_accumulator *acc;

			acc = (struct kvmhv_tb_accumulator *)
				((unsigned long)vcpu + timings[i].offset);
			ok = false;
			for (loops = 0; loops < 1000; ++loops) {
				count = acc->seqcount;
				if (!(count & 1)) {
					smp_rmb();
					tb = *acc;
					smp_rmb();
					if (count == acc->seqcount) {
						ok = true;
						break;
					}
				}
				udelay(1);
			}
			if (!ok)
				snprintf(s, buf_end - s, "%s: stuck\n",
					timings[i].name);
			else
				snprintf(s, buf_end - s,
					"%s: %llu %llu %llu %llu\n",
					timings[i].name, count / 2,
					tb_to_ns(tb.tb_total),
					tb_to_ns(tb.tb_min),
					tb_to_ns(tb.tb_max));
			s += strlen(s);
		}
		p->buflen = s - p->buf;
	}

	pos = *ppos;
	if (pos >= p->buflen)
		return 0;
	if (len > p->buflen - pos)
		len = p->buflen - pos;
	n = copy_to_user(buf, p->buf + pos, len);
	if (n) {
		if (n == len)
			return -EFAULT;
		len -= n;
	}
	*ppos = pos + len;
	return len;
}

static ssize_t debugfs_timings_write(struct file *file, const char __user *buf,
				     size_t len, loff_t *ppos)
{
	return -EACCES;
}

static const struct file_operations debugfs_timings_ops = {
	.owner	 = THIS_MODULE,
	.open	 = debugfs_timings_open,
	.release = debugfs_timings_release,
	.read	 = debugfs_timings_read,
	.write	 = debugfs_timings_write,
	.llseek	 = generic_file_llseek,
};

/* Create a debugfs directory for the vcpu */
static void debugfs_vcpu_init(struct kvm_vcpu *vcpu, unsigned int id)
{
	char buf[16];
	struct kvm *kvm = vcpu->kvm;

	snprintf(buf, sizeof(buf), "vcpu%u", id);
	if (IS_ERR_OR_NULL(kvm->arch.debugfs_dir))
		return;
	vcpu->arch.debugfs_dir = debugfs_create_dir(buf, kvm->arch.debugfs_dir);
	if (IS_ERR_OR_NULL(vcpu->arch.debugfs_dir))
		return;
	vcpu->arch.debugfs_timings =
		debugfs_create_file("timings", 0444, vcpu->arch.debugfs_dir,
				    vcpu, &debugfs_timings_ops);
}

#else /* CONFIG_KVM_BOOK3S_HV_EXIT_TIMING */
static void debugfs_vcpu_init(struct kvm_vcpu *vcpu, unsigned int id)
{
}
#endif /* CONFIG_KVM_BOOK3S_HV_EXIT_TIMING */

static struct kvm_vcpu *kvmppc_core_vcpu_create_hv(struct kvm *kvm,
						   unsigned int id)
{
	struct kvm_vcpu *vcpu;
	int err = -EINVAL;
	int core;
	struct kvmppc_vcore *vcore;

	core = id / threads_per_subcore;
	if (core >= KVM_MAX_VCORES)
		goto out;

	err = -ENOMEM;
	vcpu = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL);
	if (!vcpu)
		goto out;

	err = kvm_vcpu_init(vcpu, kvm, id);
	if (err)
		goto free_vcpu;

	vcpu->arch.shared = &vcpu->arch.shregs;
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	/*
	 * The shared struct is never shared on HV,
	 * so we can always use host endianness
	 */
#ifdef __BIG_ENDIAN__
	vcpu->arch.shared_big_endian = true;
#else
	vcpu->arch.shared_big_endian = false;
#endif
#endif
	vcpu->arch.mmcr[0] = MMCR0_FC;
	vcpu->arch.ctrl = CTRL_RUNLATCH;
	/* default to host PVR, since we can't spoof it */
	kvmppc_set_pvr_hv(vcpu, mfspr(SPRN_PVR));
	spin_lock_init(&vcpu->arch.vpa_update_lock);
	spin_lock_init(&vcpu->arch.tbacct_lock);
	vcpu->arch.busy_preempt = TB_NIL;
	vcpu->arch.intr_msr = MSR_SF | MSR_ME;

	kvmppc_mmu_book3s_hv_init(vcpu);

	vcpu->arch.state = KVMPPC_VCPU_NOTREADY;

	init_waitqueue_head(&vcpu->arch.cpu_run);

	mutex_lock(&kvm->lock);
	vcore = kvm->arch.vcores[core];
	if (!vcore) {
		vcore = kvmppc_vcore_create(kvm, core);
		kvm->arch.vcores[core] = vcore;
		kvm->arch.online_vcores++;
	}
	mutex_unlock(&kvm->lock);

	if (!vcore)
		goto free_vcpu;

	spin_lock(&vcore->lock);
	++vcore->num_threads;
	spin_unlock(&vcore->lock);
	vcpu->arch.vcore = vcore;
	vcpu->arch.ptid = vcpu->vcpu_id - vcore->first_vcpuid;

	vcpu->arch.cpu_type = KVM_CPU_3S_64;
	kvmppc_sanity_check(vcpu);

	debugfs_vcpu_init(vcpu, id);

	return vcpu;

free_vcpu:
	kmem_cache_free(kvm_vcpu_cache, vcpu);
out:
	return ERR_PTR(err);
}

static void unpin_vpa(struct kvm *kvm, struct kvmppc_vpa *vpa)
{
	if (vpa->pinned_addr)
		kvmppc_unpin_guest_page(kvm, vpa->pinned_addr, vpa->gpa,
					vpa->dirty);
}

static void kvmppc_core_vcpu_free_hv(struct kvm_vcpu *vcpu)
{
	spin_lock(&vcpu->arch.vpa_update_lock);
	unpin_vpa(vcpu->kvm, &vcpu->arch.dtl);
	unpin_vpa(vcpu->kvm, &vcpu->arch.slb_shadow);
	unpin_vpa(vcpu->kvm, &vcpu->arch.vpa);
	spin_unlock(&vcpu->arch.vpa_update_lock);
	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, vcpu);
}

static int kvmppc_core_check_requests_hv(struct kvm_vcpu *vcpu)
{
	/* Indicate we want to get back into the guest */
	return 1;
}

static void kvmppc_set_timer(struct kvm_vcpu *vcpu)
{
	unsigned long dec_nsec, now;

	now = get_tb();
	if (now > vcpu->arch.dec_expires) {
		/* decrementer has already gone negative */
		kvmppc_core_queue_dec(vcpu);
		kvmppc_core_prepare_to_enter(vcpu);
		return;
	}
	dec_nsec = (vcpu->arch.dec_expires - now) * NSEC_PER_SEC
		   / tb_ticks_per_sec;
	hrtimer_start(&vcpu->arch.dec_timer, ktime_set(0, dec_nsec),
		      HRTIMER_MODE_REL);
	vcpu->arch.timer_running = 1;
}

static void kvmppc_end_cede(struct kvm_vcpu *vcpu)
{
	vcpu->arch.ceded = 0;
	if (vcpu->arch.timer_running) {
		hrtimer_try_to_cancel(&vcpu->arch.dec_timer);
		vcpu->arch.timer_running = 0;
	}
}

extern void __kvmppc_vcore_entry(void);

static void kvmppc_remove_runnable(struct kvmppc_vcore *vc,
				   struct kvm_vcpu *vcpu)
{
	u64 now;

	if (vcpu->arch.state != KVMPPC_VCPU_RUNNABLE)
		return;
	spin_lock_irq(&vcpu->arch.tbacct_lock);
	now = mftb();
	vcpu->arch.busy_stolen += vcore_stolen_time(vc, now) -
		vcpu->arch.stolen_logged;
	vcpu->arch.busy_preempt = now;
	vcpu->arch.state = KVMPPC_VCPU_BUSY_IN_HOST;
	spin_unlock_irq(&vcpu->arch.tbacct_lock);
	--vc->n_runnable;
	list_del(&vcpu->arch.run_list);
}

static int kvmppc_grab_hwthread(int cpu)
{
	struct paca_struct *tpaca;
	long timeout = 10000;

	tpaca = &paca[cpu];

	/* Ensure the thread won't go into the kernel if it wakes */
	tpaca->kvm_hstate.kvm_vcpu = NULL;
	tpaca->kvm_hstate.napping = 0;
	smp_wmb();
	tpaca->kvm_hstate.hwthread_req = 1;

	/*
	 * If the thread is already executing in the kernel (e.g. handling
	 * a stray interrupt), wait for it to get back to nap mode.
	 * The smp_mb() is to ensure that our setting of hwthread_req
	 * is visible before we look at hwthread_state, so if this
	 * races with the code at system_reset_pSeries and the thread
	 * misses our setting of hwthread_req, we are sure to see its
	 * setting of hwthread_state, and vice versa.
	 */
	smp_mb();
	while (tpaca->kvm_hstate.hwthread_state == KVM_HWTHREAD_IN_KERNEL) {
		if (--timeout <= 0) {
			pr_err("KVM: couldn't grab cpu %d\n", cpu);
			return -EBUSY;
		}
		udelay(1);
	}
	return 0;
}

static void kvmppc_release_hwthread(int cpu)
{
	struct paca_struct *tpaca;

	tpaca = &paca[cpu];
	tpaca->kvm_hstate.hwthread_req = 0;
	tpaca->kvm_hstate.kvm_vcpu = NULL;
}

static void kvmppc_start_thread(struct kvm_vcpu *vcpu)
{
	int cpu;
	struct paca_struct *tpaca;
	struct kvmppc_vcore *vc = vcpu->arch.vcore;

	if (vcpu->arch.timer_running) {
		hrtimer_try_to_cancel(&vcpu->arch.dec_timer);
		vcpu->arch.timer_running = 0;
	}
	cpu = vc->pcpu + vcpu->arch.ptid;
	tpaca = &paca[cpu];
	tpaca->kvm_hstate.kvm_vcore = vc;
	tpaca->kvm_hstate.ptid = vcpu->arch.ptid;
	vcpu->cpu = vc->pcpu;
	/* Order stores to hstate.kvm_vcore etc. before store to kvm_vcpu */
	smp_wmb();
	tpaca->kvm_hstate.kvm_vcpu = vcpu;
	if (cpu != smp_processor_id())
		kvmppc_ipi_thread(cpu);
}

static void kvmppc_wait_for_nap(void)
{
	int cpu = smp_processor_id();
	int i, loops;

	for (loops = 0; loops < 1000000; ++loops) {
		/*
		 * Check if all threads are finished.
		 * We set the vcpu pointer when starting a thread
		 * and the thread clears it when finished, so we look
		 * for any threads that still have a non-NULL vcpu ptr.
		 */
		for (i = 1; i < threads_per_subcore; ++i)
			if (paca[cpu + i].kvm_hstate.kvm_vcpu)
				break;
		if (i == threads_per_subcore) {
			HMT_medium();
			return;
		}
		HMT_low();
	}
	HMT_medium();
	for (i = 1; i < threads_per_subcore; ++i)
		if (paca[cpu + i].kvm_hstate.kvm_vcpu)
			pr_err("KVM: CPU %d seems to be stuck\n", cpu + i);
}

/*
 * Check that we are on thread 0 and that any other threads in
 * this core are off-line.  Then grab the threads so they can't
 * enter the kernel.
 */
static int on_primary_thread(void)
{
	int cpu = smp_processor_id();
	int thr;

	/* Are we on a primary subcore? */
	if (cpu_thread_in_subcore(cpu))
		return 0;

	thr = 0;
	while (++thr < threads_per_subcore)
		if (cpu_online(cpu + thr))
			return 0;

	/* Grab all hw threads so they can't go into the kernel */
	for (thr = 1; thr < threads_per_subcore; ++thr) {
		if (kvmppc_grab_hwthread(cpu + thr)) {
			/* Couldn't grab one; let the others go */
			do {
				kvmppc_release_hwthread(cpu + thr);
			} while (--thr > 0);
			return 0;
		}
	}
	return 1;
}

static void kvmppc_start_saving_l2_cache(struct kvmppc_vcore *vc)
{
	phys_addr_t phy_addr, mpp_addr;

	phy_addr = (phys_addr_t)virt_to_phys(vc->mpp_buffer);
	mpp_addr = phy_addr & PPC_MPPE_ADDRESS_MASK;

	mtspr(SPRN_MPPR, mpp_addr | PPC_MPPR_FETCH_ABORT);
	logmpp(mpp_addr | PPC_LOGMPP_LOG_L2);

	vc->mpp_buffer_is_valid = true;
}

static void kvmppc_start_restoring_l2_cache(const struct kvmppc_vcore *vc)
{
	phys_addr_t phy_addr, mpp_addr;

	phy_addr = virt_to_phys(vc->mpp_buffer);
	mpp_addr = phy_addr & PPC_MPPE_ADDRESS_MASK;

	/* We must abort any in-progress save operations to ensure
	 * the table is valid so that prefetch engine knows when to
	 * stop prefetching. */
	logmpp(mpp_addr | PPC_LOGMPP_LOG_ABORT);
	mtspr(SPRN_MPPR, mpp_addr | PPC_MPPR_FETCH_WHOLE_TABLE);
}

static void prepare_threads(struct kvmppc_vcore *vc)
{
	struct kvm_vcpu *vcpu, *vnext;

	list_for_each_entry_safe(vcpu, vnext, &vc->runnable_threads,
				 arch.run_list) {
		if (signal_pending(vcpu->arch.run_task))
			vcpu->arch.ret = -EINTR;
		else if (vcpu->arch.vpa.update_pending ||
			 vcpu->arch.slb_shadow.update_pending ||
			 vcpu->arch.dtl.update_pending)
			vcpu->arch.ret = RESUME_GUEST;
		else
			continue;
		kvmppc_remove_runnable(vc, vcpu);
		wake_up(&vcpu->arch.cpu_run);
	}
}

static void post_guest_process(struct kvmppc_vcore *vc)
{
	u64 now;
	long ret;
	struct kvm_vcpu *vcpu, *vnext;

	now = get_tb();
	list_for_each_entry_safe(vcpu, vnext, &vc->runnable_threads,
				 arch.run_list) {
		/* cancel pending dec exception if dec is positive */
		if (now < vcpu->arch.dec_expires &&
		    kvmppc_core_pending_dec(vcpu))
			kvmppc_core_dequeue_dec(vcpu);

		trace_kvm_guest_exit(vcpu);

		ret = RESUME_GUEST;
		if (vcpu->arch.trap)
			ret = kvmppc_handle_exit_hv(vcpu->arch.kvm_run, vcpu,
						    vcpu->arch.run_task);

		vcpu->arch.ret = ret;
		vcpu->arch.trap = 0;

		if (vcpu->arch.ceded) {
			if (!is_kvmppc_resume_guest(ret))
				kvmppc_end_cede(vcpu);
			else
				kvmppc_set_timer(vcpu);
		}
		if (!is_kvmppc_resume_guest(vcpu->arch.ret)) {
			kvmppc_remove_runnable(vc, vcpu);
			wake_up(&vcpu->arch.cpu_run);
		}
	}
}

/*
 * Run a set of guest threads on a physical core.
 * Called with vc->lock held.
 */
static noinline void kvmppc_run_core(struct kvmppc_vcore *vc)
{
	struct kvm_vcpu *vcpu, *vnext;
	int i;
	int srcu_idx;

	/*
	 * Remove from the list any threads that have a signal pending
	 * or need a VPA update done
	 */
	prepare_threads(vc);

	/* if the runner is no longer runnable, let the caller pick a new one */
	if (vc->runner->arch.state != KVMPPC_VCPU_RUNNABLE)
		return;

	/*
	 * Initialize *vc.
	 */
	vc->entry_exit_map = 0;
	vc->preempt_tb = TB_NIL;
	vc->in_guest = 0;
	vc->napping_threads = 0;
	vc->conferring_threads = 0;

	/*
	 * Make sure we are running on primary threads, and that secondary
	 * threads are offline.  Also check if the number of threads in this
	 * guest are greater than the current system threads per guest.
	 */
	if ((threads_per_core > 1) &&
	    ((vc->num_threads > threads_per_subcore) || !on_primary_thread())) {
		list_for_each_entry_safe(vcpu, vnext, &vc->runnable_threads,
					 arch.run_list) {
			vcpu->arch.ret = -EBUSY;
			kvmppc_remove_runnable(vc, vcpu);
			wake_up(&vcpu->arch.cpu_run);
		}
		goto out;
	}


	vc->pcpu = smp_processor_id();
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list) {
		kvmppc_start_thread(vcpu);
		kvmppc_create_dtl_entry(vcpu, vc);
		trace_kvm_guest_enter(vcpu);
	}

	/* Set this explicitly in case thread 0 doesn't have a vcpu */
	get_paca()->kvm_hstate.kvm_vcore = vc;
	get_paca()->kvm_hstate.ptid = 0;

	vc->vcore_state = VCORE_RUNNING;
	preempt_disable();

	trace_kvmppc_run_core(vc, 0);

	spin_unlock(&vc->lock);

	kvm_guest_enter();

	srcu_idx = srcu_read_lock(&vc->kvm->srcu);

	if (vc->mpp_buffer_is_valid)
		kvmppc_start_restoring_l2_cache(vc);

	__kvmppc_vcore_entry();

	spin_lock(&vc->lock);

	if (vc->mpp_buffer)
		kvmppc_start_saving_l2_cache(vc);

	/* disable sending of IPIs on virtual external irqs */
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list)
		vcpu->cpu = -1;
	/* wait for secondary threads to finish writing their state to memory */
	kvmppc_wait_for_nap();
	for (i = 0; i < threads_per_subcore; ++i)
		kvmppc_release_hwthread(vc->pcpu + i);
	/* prevent other vcpu threads from doing kvmppc_start_thread() now */
	vc->vcore_state = VCORE_EXITING;
	spin_unlock(&vc->lock);

	srcu_read_unlock(&vc->kvm->srcu, srcu_idx);

	/* make sure updates to secondary vcpu structs are visible now */
	smp_mb();
	kvm_guest_exit();

	preempt_enable();

	spin_lock(&vc->lock);
	post_guest_process(vc);

 out:
	vc->vcore_state = VCORE_INACTIVE;
	trace_kvmppc_run_core(vc, 1);
}

/*
 * Wait for some other vcpu thread to execute us, and
 * wake us up when we need to handle something in the host.
 */
static void kvmppc_wait_for_exec(struct kvm_vcpu *vcpu, int wait_state)
{
	DEFINE_WAIT(wait);

	prepare_to_wait(&vcpu->arch.cpu_run, &wait, wait_state);
	if (vcpu->arch.state == KVMPPC_VCPU_RUNNABLE)
		schedule();
	finish_wait(&vcpu->arch.cpu_run, &wait);
}

/*
 * All the vcpus in this vcore are idle, so wait for a decrementer
 * or external interrupt to one of the vcpus.  vc->lock is held.
 */
static void kvmppc_vcore_blocked(struct kvmppc_vcore *vc)
{
	struct kvm_vcpu *vcpu;
	int do_sleep = 1;

	DEFINE_WAIT(wait);

	prepare_to_wait(&vc->wq, &wait, TASK_INTERRUPTIBLE);

	/*
	 * Check one last time for pending exceptions and ceded state after
	 * we put ourselves on the wait queue
	 */
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list) {
		if (vcpu->arch.pending_exceptions || !vcpu->arch.ceded) {
			do_sleep = 0;
			break;
		}
	}

	if (!do_sleep) {
		finish_wait(&vc->wq, &wait);
		return;
	}

	vc->vcore_state = VCORE_SLEEPING;
	trace_kvmppc_vcore_blocked(vc, 0);
	spin_unlock(&vc->lock);
	schedule();
	finish_wait(&vc->wq, &wait);
	spin_lock(&vc->lock);
	vc->vcore_state = VCORE_INACTIVE;
	trace_kvmppc_vcore_blocked(vc, 1);
}

static int kvmppc_run_vcpu(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	int n_ceded;
	struct kvmppc_vcore *vc;
	struct kvm_vcpu *v, *vn;

	trace_kvmppc_run_vcpu_enter(vcpu);

	kvm_run->exit_reason = 0;
	vcpu->arch.ret = RESUME_GUEST;
	vcpu->arch.trap = 0;
	kvmppc_update_vpas(vcpu);

	/*
	 * Synchronize with other threads in this virtual core
	 */
	vc = vcpu->arch.vcore;
	spin_lock(&vc->lock);
	vcpu->arch.ceded = 0;
	vcpu->arch.run_task = current;
	vcpu->arch.kvm_run = kvm_run;
	vcpu->arch.stolen_logged = vcore_stolen_time(vc, mftb());
	vcpu->arch.state = KVMPPC_VCPU_RUNNABLE;
	vcpu->arch.busy_preempt = TB_NIL;
	list_add_tail(&vcpu->arch.run_list, &vc->runnable_threads);
	++vc->n_runnable;

	/*
	 * This happens the first time this is called for a vcpu.
	 * If the vcore is already running, we may be able to start
	 * this thread straight away and have it join in.
	 */
	if (!signal_pending(current)) {
		if (vc->vcore_state == VCORE_RUNNING && !VCORE_IS_EXITING(vc)) {
			kvmppc_create_dtl_entry(vcpu, vc);
			kvmppc_start_thread(vcpu);
			trace_kvm_guest_enter(vcpu);
		} else if (vc->vcore_state == VCORE_SLEEPING) {
			wake_up(&vc->wq);
		}

	}

	while (vcpu->arch.state == KVMPPC_VCPU_RUNNABLE &&
	       !signal_pending(current)) {
		if (vc->vcore_state != VCORE_INACTIVE) {
			spin_unlock(&vc->lock);
			kvmppc_wait_for_exec(vcpu, TASK_INTERRUPTIBLE);
			spin_lock(&vc->lock);
			continue;
		}
		list_for_each_entry_safe(v, vn, &vc->runnable_threads,
					 arch.run_list) {
			kvmppc_core_prepare_to_enter(v);
			if (signal_pending(v->arch.run_task)) {
				kvmppc_remove_runnable(vc, v);
				v->stat.signal_exits++;
				v->arch.kvm_run->exit_reason = KVM_EXIT_INTR;
				v->arch.ret = -EINTR;
				wake_up(&v->arch.cpu_run);
			}
		}
		if (!vc->n_runnable || vcpu->arch.state != KVMPPC_VCPU_RUNNABLE)
			break;
		n_ceded = 0;
		list_for_each_entry(v, &vc->runnable_threads, arch.run_list) {
			if (!v->arch.pending_exceptions)
				n_ceded += v->arch.ceded;
			else
				v->arch.ceded = 0;
		}
		vc->runner = vcpu;
		if (n_ceded == vc->n_runnable) {
			kvmppc_vcore_blocked(vc);
		} else if (need_resched()) {
			vc->vcore_state = VCORE_PREEMPT;
			/* Let something else run */
			cond_resched_lock(&vc->lock);
			vc->vcore_state = VCORE_INACTIVE;
		} else {
			kvmppc_run_core(vc);
		}
		vc->runner = NULL;
	}

	while (vcpu->arch.state == KVMPPC_VCPU_RUNNABLE &&
	       (vc->vcore_state == VCORE_RUNNING ||
		vc->vcore_state == VCORE_EXITING)) {
		spin_unlock(&vc->lock);
		kvmppc_wait_for_exec(vcpu, TASK_UNINTERRUPTIBLE);
		spin_lock(&vc->lock);
	}

	if (vcpu->arch.state == KVMPPC_VCPU_RUNNABLE) {
		kvmppc_remove_runnable(vc, vcpu);
		vcpu->stat.signal_exits++;
		kvm_run->exit_reason = KVM_EXIT_INTR;
		vcpu->arch.ret = -EINTR;
	}

	if (vc->n_runnable && vc->vcore_state == VCORE_INACTIVE) {
		/* Wake up some vcpu to run the core */
		v = list_first_entry(&vc->runnable_threads,
				     struct kvm_vcpu, arch.run_list);
		wake_up(&v->arch.cpu_run);
	}

	trace_kvmppc_run_vcpu_exit(vcpu, kvm_run);
	spin_unlock(&vc->lock);
	return vcpu->arch.ret;
}

static int kvmppc_vcpu_run_hv(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	int r;
	int srcu_idx;

	if (!vcpu->arch.sane) {
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		return -EINVAL;
	}

	kvmppc_core_prepare_to_enter(vcpu);

	/* No need to go into the guest when all we'll do is come back out */
	if (signal_pending(current)) {
		run->exit_reason = KVM_EXIT_INTR;
		return -EINTR;
	}

	atomic_inc(&vcpu->kvm->arch.vcpus_running);
	/* Order vcpus_running vs. hpte_setup_done, see kvmppc_alloc_reset_hpt */
	smp_mb();

	/* On the first time here, set up HTAB and VRMA */
	if (!vcpu->kvm->arch.hpte_setup_done) {
		r = kvmppc_hv_setup_htab_rma(vcpu);
		if (r)
			goto out;
	}

	flush_fp_to_thread(current);
	flush_altivec_to_thread(current);
	flush_vsx_to_thread(current);
	vcpu->arch.wqp = &vcpu->arch.vcore->wq;
	vcpu->arch.pgdir = current->mm->pgd;
	vcpu->arch.state = KVMPPC_VCPU_BUSY_IN_HOST;

	do {
		r = kvmppc_run_vcpu(run, vcpu);

		if (run->exit_reason == KVM_EXIT_PAPR_HCALL &&
		    !(vcpu->arch.shregs.msr & MSR_PR)) {
			trace_kvm_hcall_enter(vcpu);
			r = kvmppc_pseries_do_hcall(vcpu);
			trace_kvm_hcall_exit(vcpu, r);
			kvmppc_core_prepare_to_enter(vcpu);
		} else if (r == RESUME_PAGE_FAULT) {
			srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
			r = kvmppc_book3s_hv_page_fault(run, vcpu,
				vcpu->arch.fault_dar, vcpu->arch.fault_dsisr);
			srcu_read_unlock(&vcpu->kvm->srcu, srcu_idx);
		}
	} while (is_kvmppc_resume_guest(r));

 out:
	vcpu->arch.state = KVMPPC_VCPU_NOTREADY;
	atomic_dec(&vcpu->kvm->arch.vcpus_running);
	return r;
}

static void kvmppc_add_seg_page_size(struct kvm_ppc_one_seg_page_size **sps,
				     int linux_psize)
{
	struct mmu_psize_def *def = &mmu_psize_defs[linux_psize];

	if (!def->shift)
		return;
	(*sps)->page_shift = def->shift;
	(*sps)->slb_enc = def->sllp;
	(*sps)->enc[0].page_shift = def->shift;
	(*sps)->enc[0].pte_enc = def->penc[linux_psize];
	/*
	 * Add 16MB MPSS support if host supports it
	 */
	if (linux_psize != MMU_PAGE_16M && def->penc[MMU_PAGE_16M] != -1) {
		(*sps)->enc[1].page_shift = 24;
		(*sps)->enc[1].pte_enc = def->penc[MMU_PAGE_16M];
	}
	(*sps)++;
}

static int kvm_vm_ioctl_get_smmu_info_hv(struct kvm *kvm,
					 struct kvm_ppc_smmu_info *info)
{
	struct kvm_ppc_one_seg_page_size *sps;

	info->flags = KVM_PPC_PAGE_SIZES_REAL;
	if (mmu_has_feature(MMU_FTR_1T_SEGMENT))
		info->flags |= KVM_PPC_1T_SEGMENTS;
	info->slb_size = mmu_slb_size;

	/* We only support these sizes for now, and no muti-size segments */
	sps = &info->sps[0];
	kvmppc_add_seg_page_size(&sps, MMU_PAGE_4K);
	kvmppc_add_seg_page_size(&sps, MMU_PAGE_64K);
	kvmppc_add_seg_page_size(&sps, MMU_PAGE_16M);

	return 0;
}

/*
 * Get (and clear) the dirty memory log for a memory slot.
 */
static int kvm_vm_ioctl_get_dirty_log_hv(struct kvm *kvm,
					 struct kvm_dirty_log *log)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int r;
	unsigned long n;

	mutex_lock(&kvm->slots_lock);

	r = -EINVAL;
	if (log->slot >= KVM_USER_MEM_SLOTS)
		goto out;

	slots = kvm_memslots(kvm);
	memslot = id_to_memslot(slots, log->slot);
	r = -ENOENT;
	if (!memslot->dirty_bitmap)
		goto out;

	n = kvm_dirty_bitmap_bytes(memslot);
	memset(memslot->dirty_bitmap, 0, n);

	r = kvmppc_hv_get_dirty_log(kvm, memslot, memslot->dirty_bitmap);
	if (r)
		goto out;

	r = -EFAULT;
	if (copy_to_user(log->dirty_bitmap, memslot->dirty_bitmap, n))
		goto out;

	r = 0;
out:
	mutex_unlock(&kvm->slots_lock);
	return r;
}

static void kvmppc_core_free_memslot_hv(struct kvm_memory_slot *free,
					struct kvm_memory_slot *dont)
{
	if (!dont || free->arch.rmap != dont->arch.rmap) {
		vfree(free->arch.rmap);
		free->arch.rmap = NULL;
	}
}

static int kvmppc_core_create_memslot_hv(struct kvm_memory_slot *slot,
					 unsigned long npages)
{
	slot->arch.rmap = vzalloc(npages * sizeof(*slot->arch.rmap));
	if (!slot->arch.rmap)
		return -ENOMEM;

	return 0;
}

static int kvmppc_core_prepare_memory_region_hv(struct kvm *kvm,
					struct kvm_memory_slot *memslot,
					const struct kvm_userspace_memory_region *mem)
{
	return 0;
}

static void kvmppc_core_commit_memory_region_hv(struct kvm *kvm,
				const struct kvm_userspace_memory_region *mem,
				const struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new)
{
	unsigned long npages = mem->memory_size >> PAGE_SHIFT;
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;

	if (npages && old->npages) {
		/*
		 * If modifying a memslot, reset all the rmap dirty bits.
		 * If this is a new memslot, we don't need to do anything
		 * since the rmap array starts out as all zeroes,
		 * i.e. no pages are dirty.
		 */
		slots = kvm_memslots(kvm);
		memslot = id_to_memslot(slots, mem->slot);
		kvmppc_hv_get_dirty_log(kvm, memslot, NULL);
	}
}

/*
 * Update LPCR values in kvm->arch and in vcores.
 * Caller must hold kvm->lock.
 */
void kvmppc_update_lpcr(struct kvm *kvm, unsigned long lpcr, unsigned long mask)
{
	long int i;
	u32 cores_done = 0;

	if ((kvm->arch.lpcr & mask) == lpcr)
		return;

	kvm->arch.lpcr = (kvm->arch.lpcr & ~mask) | lpcr;

	for (i = 0; i < KVM_MAX_VCORES; ++i) {
		struct kvmppc_vcore *vc = kvm->arch.vcores[i];
		if (!vc)
			continue;
		spin_lock(&vc->lock);
		vc->lpcr = (vc->lpcr & ~mask) | lpcr;
		spin_unlock(&vc->lock);
		if (++cores_done >= kvm->arch.online_vcores)
			break;
	}
}

static void kvmppc_mmu_destroy_hv(struct kvm_vcpu *vcpu)
{
	return;
}

static int kvmppc_hv_setup_htab_rma(struct kvm_vcpu *vcpu)
{
	int err = 0;
	struct kvm *kvm = vcpu->kvm;
	unsigned long hva;
	struct kvm_memory_slot *memslot;
	struct vm_area_struct *vma;
	unsigned long lpcr = 0, senc;
	unsigned long psize, porder;
	int srcu_idx;

	mutex_lock(&kvm->lock);
	if (kvm->arch.hpte_setup_done)
		goto out;	/* another vcpu beat us to it */

	/* Allocate hashed page table (if not done already) and reset it */
	if (!kvm->arch.hpt_virt) {
		err = kvmppc_alloc_hpt(kvm, NULL);
		if (err) {
			pr_err("KVM: Couldn't alloc HPT\n");
			goto out;
		}
	}

	/* Look up the memslot for guest physical address 0 */
	srcu_idx = srcu_read_lock(&kvm->srcu);
	memslot = gfn_to_memslot(kvm, 0);

	/* We must have some memory at 0 by now */
	err = -EINVAL;
	if (!memslot || (memslot->flags & KVM_MEMSLOT_INVALID))
		goto out_srcu;

	/* Look up the VMA for the start of this memory slot */
	hva = memslot->userspace_addr;
	down_read(&current->mm->mmap_sem);
	vma = find_vma(current->mm, hva);
	if (!vma || vma->vm_start > hva || (vma->vm_flags & VM_IO))
		goto up_out;

	psize = vma_kernel_pagesize(vma);
	porder = __ilog2(psize);

	up_read(&current->mm->mmap_sem);

	/* We can handle 4k, 64k or 16M pages in the VRMA */
	err = -EINVAL;
	if (!(psize == 0x1000 || psize == 0x10000 ||
	      psize == 0x1000000))
		goto out_srcu;

	/* Update VRMASD field in the LPCR */
	senc = slb_pgsize_encoding(psize);
	kvm->arch.vrma_slb_v = senc | SLB_VSID_B_1T |
		(VRMA_VSID << SLB_VSID_SHIFT_1T);
	/* the -4 is to account for senc values starting at 0x10 */
	lpcr = senc << (LPCR_VRMASD_SH - 4);

	/* Create HPTEs in the hash page table for the VRMA */
	kvmppc_map_vrma(vcpu, memslot, porder);

	kvmppc_update_lpcr(kvm, lpcr, LPCR_VRMASD);

	/* Order updates to kvm->arch.lpcr etc. vs. hpte_setup_done */
	smp_wmb();
	kvm->arch.hpte_setup_done = 1;
	err = 0;
 out_srcu:
	srcu_read_unlock(&kvm->srcu, srcu_idx);
 out:
	mutex_unlock(&kvm->lock);
	return err;

 up_out:
	up_read(&current->mm->mmap_sem);
	goto out_srcu;
}

static int kvmppc_core_init_vm_hv(struct kvm *kvm)
{
	unsigned long lpcr, lpid;
	char buf[32];

	/* Allocate the guest's logical partition ID */

	lpid = kvmppc_alloc_lpid();
	if ((long)lpid < 0)
		return -ENOMEM;
	kvm->arch.lpid = lpid;

	/*
	 * Since we don't flush the TLB when tearing down a VM,
	 * and this lpid might have previously been used,
	 * make sure we flush on each core before running the new VM.
	 */
	cpumask_setall(&kvm->arch.need_tlb_flush);

	/* Start out with the default set of hcalls enabled */
	memcpy(kvm->arch.enabled_hcalls, default_enabled_hcalls,
	       sizeof(kvm->arch.enabled_hcalls));

	kvm->arch.host_sdr1 = mfspr(SPRN_SDR1);

	/* Init LPCR for virtual RMA mode */
	kvm->arch.host_lpid = mfspr(SPRN_LPID);
	kvm->arch.host_lpcr = lpcr = mfspr(SPRN_LPCR);
	lpcr &= LPCR_PECE | LPCR_LPES;
	lpcr |= (4UL << LPCR_DPFD_SH) | LPCR_HDICE |
		LPCR_VPM0 | LPCR_VPM1;
	kvm->arch.vrma_slb_v = SLB_VSID_B_1T |
		(VRMA_VSID << SLB_VSID_SHIFT_1T);
	/* On POWER8 turn on online bit to enable PURR/SPURR */
	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		lpcr |= LPCR_ONL;
	kvm->arch.lpcr = lpcr;

	/*
	 * Track that we now have a HV mode VM active. This blocks secondary
	 * CPU threads from coming online.
	 */
	kvm_hv_vm_activated();

	/*
	 * Create a debugfs directory for the VM
	 */
	snprintf(buf, sizeof(buf), "vm%d", current->pid);
	kvm->arch.debugfs_dir = debugfs_create_dir(buf, kvm_debugfs_dir);
	if (!IS_ERR_OR_NULL(kvm->arch.debugfs_dir))
		kvmppc_mmu_debugfs_init(kvm);

	return 0;
}

static void kvmppc_free_vcores(struct kvm *kvm)
{
	long int i;

	for (i = 0; i < KVM_MAX_VCORES; ++i) {
		if (kvm->arch.vcores[i] && kvm->arch.vcores[i]->mpp_buffer) {
			struct kvmppc_vcore *vc = kvm->arch.vcores[i];
			free_pages((unsigned long)vc->mpp_buffer,
				   MPP_BUFFER_ORDER);
		}
		kfree(kvm->arch.vcores[i]);
	}
	kvm->arch.online_vcores = 0;
}

static void kvmppc_core_destroy_vm_hv(struct kvm *kvm)
{
	debugfs_remove_recursive(kvm->arch.debugfs_dir);

	kvm_hv_vm_deactivated();

	kvmppc_free_vcores(kvm);

	kvmppc_free_hpt(kvm);
}

/* We don't need to emulate any privileged instructions or dcbz */
static int kvmppc_core_emulate_op_hv(struct kvm_run *run, struct kvm_vcpu *vcpu,
				     unsigned int inst, int *advance)
{
	return EMULATE_FAIL;
}

static int kvmppc_core_emulate_mtspr_hv(struct kvm_vcpu *vcpu, int sprn,
					ulong spr_val)
{
	return EMULATE_FAIL;
}

static int kvmppc_core_emulate_mfspr_hv(struct kvm_vcpu *vcpu, int sprn,
					ulong *spr_val)
{
	return EMULATE_FAIL;
}

static int kvmppc_core_check_processor_compat_hv(void)
{
	if (!cpu_has_feature(CPU_FTR_HVMODE) ||
	    !cpu_has_feature(CPU_FTR_ARCH_206))
		return -EIO;
	return 0;
}

static long kvm_arch_vm_ioctl_hv(struct file *filp,
				 unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm __maybe_unused = filp->private_data;
	void __user *argp = (void __user *)arg;
	long r;

	switch (ioctl) {

	case KVM_PPC_ALLOCATE_HTAB: {
		u32 htab_order;

		r = -EFAULT;
		if (get_user(htab_order, (u32 __user *)argp))
			break;
		r = kvmppc_alloc_reset_hpt(kvm, &htab_order);
		if (r)
			break;
		r = -EFAULT;
		if (put_user(htab_order, (u32 __user *)argp))
			break;
		r = 0;
		break;
	}

	case KVM_PPC_GET_HTAB_FD: {
		struct kvm_get_htab_fd ghf;

		r = -EFAULT;
		if (copy_from_user(&ghf, argp, sizeof(ghf)))
			break;
		r = kvm_vm_ioctl_get_htab_fd(kvm, &ghf);
		break;
	}

	default:
		r = -ENOTTY;
	}

	return r;
}

/*
 * List of hcall numbers to enable by default.
 * For compatibility with old userspace, we enable by default
 * all hcalls that were implemented before the hcall-enabling
 * facility was added.  Note this list should not include H_RTAS.
 */
static unsigned int default_hcall_list[] = {
	H_REMOVE,
	H_ENTER,
	H_READ,
	H_PROTECT,
	H_BULK_REMOVE,
	H_GET_TCE,
	H_PUT_TCE,
	H_SET_DABR,
	H_SET_XDABR,
	H_CEDE,
	H_PROD,
	H_CONFER,
	H_REGISTER_VPA,
#ifdef CONFIG_KVM_XICS
	H_EOI,
	H_CPPR,
	H_IPI,
	H_IPOLL,
	H_XIRR,
	H_XIRR_X,
#endif
	0
};

static void init_default_hcalls(void)
{
	int i;
	unsigned int hcall;

	for (i = 0; default_hcall_list[i]; ++i) {
		hcall = default_hcall_list[i];
		WARN_ON(!kvmppc_hcall_impl_hv(hcall));
		__set_bit(hcall / 4, default_enabled_hcalls);
	}
}

static struct kvmppc_ops kvm_ops_hv = {
	.get_sregs = kvm_arch_vcpu_ioctl_get_sregs_hv,
	.set_sregs = kvm_arch_vcpu_ioctl_set_sregs_hv,
	.get_one_reg = kvmppc_get_one_reg_hv,
	.set_one_reg = kvmppc_set_one_reg_hv,
	.vcpu_load   = kvmppc_core_vcpu_load_hv,
	.vcpu_put    = kvmppc_core_vcpu_put_hv,
	.set_msr     = kvmppc_set_msr_hv,
	.vcpu_run    = kvmppc_vcpu_run_hv,
	.vcpu_create = kvmppc_core_vcpu_create_hv,
	.vcpu_free   = kvmppc_core_vcpu_free_hv,
	.check_requests = kvmppc_core_check_requests_hv,
	.get_dirty_log  = kvm_vm_ioctl_get_dirty_log_hv,
	.flush_memslot  = kvmppc_core_flush_memslot_hv,
	.prepare_memory_region = kvmppc_core_prepare_memory_region_hv,
	.commit_memory_region  = kvmppc_core_commit_memory_region_hv,
	.unmap_hva = kvm_unmap_hva_hv,
	.unmap_hva_range = kvm_unmap_hva_range_hv,
	.age_hva  = kvm_age_hva_hv,
	.test_age_hva = kvm_test_age_hva_hv,
	.set_spte_hva = kvm_set_spte_hva_hv,
	.mmu_destroy  = kvmppc_mmu_destroy_hv,
	.free_memslot = kvmppc_core_free_memslot_hv,
	.create_memslot = kvmppc_core_create_memslot_hv,
	.init_vm =  kvmppc_core_init_vm_hv,
	.destroy_vm = kvmppc_core_destroy_vm_hv,
	.get_smmu_info = kvm_vm_ioctl_get_smmu_info_hv,
	.emulate_op = kvmppc_core_emulate_op_hv,
	.emulate_mtspr = kvmppc_core_emulate_mtspr_hv,
	.emulate_mfspr = kvmppc_core_emulate_mfspr_hv,
	.fast_vcpu_kick = kvmppc_fast_vcpu_kick_hv,
	.arch_vm_ioctl  = kvm_arch_vm_ioctl_hv,
	.hcall_implemented = kvmppc_hcall_impl_hv,
};

static int kvmppc_book3s_init_hv(void)
{
	int r;
	/*
	 * FIXME!! Do we need to check on all cpus ?
	 */
	r = kvmppc_core_check_processor_compat_hv();
	if (r < 0)
		return -ENODEV;

	kvm_ops_hv.owner = THIS_MODULE;
	kvmppc_hv_ops = &kvm_ops_hv;

	init_default_hcalls();

	r = kvmppc_mmu_hv_init();
	return r;
}

static void kvmppc_book3s_exit_hv(void)
{
	kvmppc_hv_ops = NULL;
}

module_init(kvmppc_book3s_init_hv);
module_exit(kvmppc_book3s_exit_hv);
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(KVM_MINOR);
MODULE_ALIAS("devname:kvm");
