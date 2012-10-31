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

#include <asm/reg.h>
#include <asm/cputable.h>
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
#include <linux/gfp.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>

/* #define EXIT_DEBUG */
/* #define EXIT_DEBUG_SIMPLE */
/* #define EXIT_DEBUG_INT */

/* Used to indicate that a guest page fault needs to be handled */
#define RESUME_PAGE_FAULT	(RESUME_GUEST | RESUME_FLAG_ARCH1)

/* Used as a "null" value for timebase values */
#define TB_NIL	(~(u64)0)

static void kvmppc_end_cede(struct kvm_vcpu *vcpu);
static int kvmppc_hv_setup_htab_rma(struct kvm_vcpu *vcpu);

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
 * updates to vc->stolen_tb are protected by the arch.tbacct_lock
 * of the vcpu that has taken responsibility for running the vcore
 * (i.e. vc->runner).  The stolen times are measured in units of
 * timebase ticks.  (Note that the != TB_NIL checks below are
 * purely defensive; they should never fail.)
 */

void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;

	spin_lock(&vcpu->arch.tbacct_lock);
	if (vc->runner == vcpu && vc->vcore_state != VCORE_INACTIVE &&
	    vc->preempt_tb != TB_NIL) {
		vc->stolen_tb += mftb() - vc->preempt_tb;
		vc->preempt_tb = TB_NIL;
	}
	if (vcpu->arch.state == KVMPPC_VCPU_BUSY_IN_HOST &&
	    vcpu->arch.busy_preempt != TB_NIL) {
		vcpu->arch.busy_stolen += mftb() - vcpu->arch.busy_preempt;
		vcpu->arch.busy_preempt = TB_NIL;
	}
	spin_unlock(&vcpu->arch.tbacct_lock);
}

void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;

	spin_lock(&vcpu->arch.tbacct_lock);
	if (vc->runner == vcpu && vc->vcore_state != VCORE_INACTIVE)
		vc->preempt_tb = mftb();
	if (vcpu->arch.state == KVMPPC_VCPU_BUSY_IN_HOST)
		vcpu->arch.busy_preempt = mftb();
	spin_unlock(&vcpu->arch.tbacct_lock);
}

void kvmppc_set_msr(struct kvm_vcpu *vcpu, u64 msr)
{
	vcpu->arch.shregs.msr = msr;
	kvmppc_end_cede(vcpu);
}

void kvmppc_set_pvr(struct kvm_vcpu *vcpu, u32 pvr)
{
	vcpu->arch.pvr = pvr;
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
	       vcpu->kvm->arch.lpcr, vcpu->kvm->arch.sdr1,
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
	vpa->shared_proc = 1;
	vpa->yield_count = 1;
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
		u16 hword;
		u32 word;
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
			len = ((struct reg_vpa *)va)->length.hword;
		else
			len = ((struct reg_vpa *)va)->length.word;
		kvmppc_unpin_guest_page(kvm, va);

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
			va = kvmppc_pin_guest_page(kvm, vpap->next_gpa, &nb);
		spin_lock(&vcpu->arch.vpa_update_lock);
		if (gpa == vpap->next_gpa)
			break;
		/* sigh... unpin that one and try again */
		if (va)
			kvmppc_unpin_guest_page(kvm, va);
	}

	vpap->update_pending = 0;
	if (va && nb < vpap->len) {
		/*
		 * If it's now too short, it must be that userspace
		 * has changed the mappings underlying guest memory,
		 * so unregister the region.
		 */
		kvmppc_unpin_guest_page(kvm, va);
		va = NULL;
	}
	if (vpap->pinned_addr)
		kvmppc_unpin_guest_page(kvm, vpap->pinned_addr);
	vpap->pinned_addr = va;
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

	/*
	 * If we are the task running the vcore, then since we hold
	 * the vcore lock, we can't be preempted, so stolen_tb/preempt_tb
	 * can't be updated, so we don't need the tbacct_lock.
	 * If the vcore is inactive, it can't become active (since we
	 * hold the vcore lock), so the vcpu load/put functions won't
	 * update stolen_tb/preempt_tb, and we don't need tbacct_lock.
	 */
	if (vc->vcore_state != VCORE_INACTIVE &&
	    vc->runner->arch.run_task != current) {
		spin_lock(&vc->runner->arch.tbacct_lock);
		p = vc->stolen_tb;
		if (vc->preempt_tb != TB_NIL)
			p += now - vc->preempt_tb;
		spin_unlock(&vc->runner->arch.tbacct_lock);
	} else {
		p = vc->stolen_tb;
	}
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
	spin_lock(&vcpu->arch.tbacct_lock);
	stolen += vcpu->arch.busy_stolen;
	vcpu->arch.busy_stolen = 0;
	spin_unlock(&vcpu->arch.tbacct_lock);
	if (!dt || !vpa)
		return;
	memset(dt, 0, sizeof(struct dtl_entry));
	dt->dispatch_reason = 7;
	dt->processor_id = vc->pcpu + vcpu->arch.ptid;
	dt->timebase = now;
	dt->enqueue_to_dispatch_time = stolen;
	dt->srr0 = kvmppc_get_pc(vcpu);
	dt->srr1 = vcpu->arch.shregs.msr;
	++dt;
	if (dt == vcpu->arch.dtl.pinned_end)
		dt = vcpu->arch.dtl.pinned_addr;
	vcpu->arch.dtl_ptr = dt;
	/* order writing *dt vs. writing vpa->dtl_idx */
	smp_wmb();
	vpa->dtl_idx = ++vcpu->arch.dtl_index;
}

int kvmppc_pseries_do_hcall(struct kvm_vcpu *vcpu)
{
	unsigned long req = kvmppc_get_gpr(vcpu, 3);
	unsigned long target, ret = H_SUCCESS;
	struct kvm_vcpu *tvcpu;
	int idx;

	switch (req) {
	case H_ENTER:
		idx = srcu_read_lock(&vcpu->kvm->srcu);
		ret = kvmppc_virtmode_h_enter(vcpu, kvmppc_get_gpr(vcpu, 4),
					      kvmppc_get_gpr(vcpu, 5),
					      kvmppc_get_gpr(vcpu, 6),
					      kvmppc_get_gpr(vcpu, 7));
		srcu_read_unlock(&vcpu->kvm->srcu, idx);
		break;
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
		break;
	case H_REGISTER_VPA:
		ret = do_h_register_vpa(vcpu, kvmppc_get_gpr(vcpu, 4),
					kvmppc_get_gpr(vcpu, 5),
					kvmppc_get_gpr(vcpu, 6));
		break;
	default:
		return RESUME_HOST;
	}
	kvmppc_set_gpr(vcpu, 3, ret);
	vcpu->arch.hcall_needed = 0;
	return RESUME_GUEST;
}

static int kvmppc_handle_exit(struct kvm_run *run, struct kvm_vcpu *vcpu,
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
		vcpu->stat.ext_intr_exits++;
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_PERFMON:
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

		if (vcpu->arch.shregs.msr & MSR_PR) {
			/* sc 1 from userspace - reflect to guest syscall */
			kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_SYSCALL);
			r = RESUME_GUEST;
			break;
		}
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
	 * We just generate a program interrupt to the guest, since
	 * we don't emulate any guest instructions at this stage.
	 */
	case BOOK3S_INTERRUPT_H_EMUL_ASSIST:
		kvmppc_core_queue_program(vcpu, 0x80000);
		r = RESUME_GUEST;
		break;
	default:
		kvmppc_dump_regs(vcpu);
		printk(KERN_EMERG "trap=0x%x | pc=0x%lx | msr=0x%llx\n",
			vcpu->arch.trap, kvmppc_get_pc(vcpu),
			vcpu->arch.shregs.msr);
		r = RESUME_HOST;
		BUG();
		break;
	}

	return r;
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
                                  struct kvm_sregs *sregs)
{
	int i;

	sregs->pvr = vcpu->arch.pvr;

	memset(sregs, 0, sizeof(struct kvm_sregs));
	for (i = 0; i < vcpu->arch.slb_max; i++) {
		sregs->u.s.ppc64.slb[i].slbe = vcpu->arch.slb[i].orige;
		sregs->u.s.ppc64.slb[i].slbv = vcpu->arch.slb[i].origv;
	}

	return 0;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
                                  struct kvm_sregs *sregs)
{
	int i, j;

	kvmppc_set_pvr(vcpu, sregs->pvr);

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

int kvmppc_get_one_reg(struct kvm_vcpu *vcpu, u64 id, union kvmppc_one_reg *val)
{
	int r = 0;
	long int i;

	switch (id) {
	case KVM_REG_PPC_HIOR:
		*val = get_reg_val(id, 0);
		break;
	case KVM_REG_PPC_DABR:
		*val = get_reg_val(id, vcpu->arch.dabr);
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
	case KVM_REG_PPC_MMCR0 ... KVM_REG_PPC_MMCRA:
		i = id - KVM_REG_PPC_MMCR0;
		*val = get_reg_val(id, vcpu->arch.mmcr[i]);
		break;
	case KVM_REG_PPC_PMC1 ... KVM_REG_PPC_PMC8:
		i = id - KVM_REG_PPC_PMC1;
		*val = get_reg_val(id, vcpu->arch.pmc[i]);
		break;
#ifdef CONFIG_VSX
	case KVM_REG_PPC_FPR0 ... KVM_REG_PPC_FPR31:
		if (cpu_has_feature(CPU_FTR_VSX)) {
			/* VSX => FP reg i is stored in arch.vsr[2*i] */
			long int i = id - KVM_REG_PPC_FPR0;
			*val = get_reg_val(id, vcpu->arch.vsr[2 * i]);
		} else {
			/* let generic code handle it */
			r = -EINVAL;
		}
		break;
	case KVM_REG_PPC_VSR0 ... KVM_REG_PPC_VSR31:
		if (cpu_has_feature(CPU_FTR_VSX)) {
			long int i = id - KVM_REG_PPC_VSR0;
			val->vsxval[0] = vcpu->arch.vsr[2 * i];
			val->vsxval[1] = vcpu->arch.vsr[2 * i + 1];
		} else {
			r = -ENXIO;
		}
		break;
#endif /* CONFIG_VSX */
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
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

int kvmppc_set_one_reg(struct kvm_vcpu *vcpu, u64 id, union kvmppc_one_reg *val)
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
	case KVM_REG_PPC_MMCR0 ... KVM_REG_PPC_MMCRA:
		i = id - KVM_REG_PPC_MMCR0;
		vcpu->arch.mmcr[i] = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_PMC1 ... KVM_REG_PPC_PMC8:
		i = id - KVM_REG_PPC_PMC1;
		vcpu->arch.pmc[i] = set_reg_val(id, *val);
		break;
#ifdef CONFIG_VSX
	case KVM_REG_PPC_FPR0 ... KVM_REG_PPC_FPR31:
		if (cpu_has_feature(CPU_FTR_VSX)) {
			/* VSX => FP reg i is stored in arch.vsr[2*i] */
			long int i = id - KVM_REG_PPC_FPR0;
			vcpu->arch.vsr[2 * i] = set_reg_val(id, *val);
		} else {
			/* let generic code handle it */
			r = -EINVAL;
		}
		break;
	case KVM_REG_PPC_VSR0 ... KVM_REG_PPC_VSR31:
		if (cpu_has_feature(CPU_FTR_VSX)) {
			long int i = id - KVM_REG_PPC_VSR0;
			vcpu->arch.vsr[2 * i] = val->vsxval[0];
			vcpu->arch.vsr[2 * i + 1] = val->vsxval[1];
		} else {
			r = -ENXIO;
		}
		break;
#endif /* CONFIG_VSX */
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
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

int kvmppc_core_check_processor_compat(void)
{
	if (cpu_has_feature(CPU_FTR_HVMODE))
		return 0;
	return -EIO;
}

struct kvm_vcpu *kvmppc_core_vcpu_create(struct kvm *kvm, unsigned int id)
{
	struct kvm_vcpu *vcpu;
	int err = -EINVAL;
	int core;
	struct kvmppc_vcore *vcore;

	core = id / threads_per_core;
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
	vcpu->arch.last_cpu = -1;
	vcpu->arch.mmcr[0] = MMCR0_FC;
	vcpu->arch.ctrl = CTRL_RUNLATCH;
	/* default to host PVR, since we can't spoof it */
	vcpu->arch.pvr = mfspr(SPRN_PVR);
	kvmppc_set_pvr(vcpu, vcpu->arch.pvr);
	spin_lock_init(&vcpu->arch.vpa_update_lock);
	spin_lock_init(&vcpu->arch.tbacct_lock);
	vcpu->arch.busy_preempt = TB_NIL;

	kvmppc_mmu_book3s_hv_init(vcpu);

	vcpu->arch.state = KVMPPC_VCPU_NOTREADY;

	init_waitqueue_head(&vcpu->arch.cpu_run);

	mutex_lock(&kvm->lock);
	vcore = kvm->arch.vcores[core];
	if (!vcore) {
		vcore = kzalloc(sizeof(struct kvmppc_vcore), GFP_KERNEL);
		if (vcore) {
			INIT_LIST_HEAD(&vcore->runnable_threads);
			spin_lock_init(&vcore->lock);
			init_waitqueue_head(&vcore->wq);
			vcore->preempt_tb = TB_NIL;
		}
		kvm->arch.vcores[core] = vcore;
	}
	mutex_unlock(&kvm->lock);

	if (!vcore)
		goto free_vcpu;

	spin_lock(&vcore->lock);
	++vcore->num_threads;
	spin_unlock(&vcore->lock);
	vcpu->arch.vcore = vcore;

	vcpu->arch.cpu_type = KVM_CPU_3S_64;
	kvmppc_sanity_check(vcpu);

	return vcpu;

free_vcpu:
	kmem_cache_free(kvm_vcpu_cache, vcpu);
out:
	return ERR_PTR(err);
}

void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu)
{
	spin_lock(&vcpu->arch.vpa_update_lock);
	if (vcpu->arch.dtl.pinned_addr)
		kvmppc_unpin_guest_page(vcpu->kvm, vcpu->arch.dtl.pinned_addr);
	if (vcpu->arch.slb_shadow.pinned_addr)
		kvmppc_unpin_guest_page(vcpu->kvm, vcpu->arch.slb_shadow.pinned_addr);
	if (vcpu->arch.vpa.pinned_addr)
		kvmppc_unpin_guest_page(vcpu->kvm, vcpu->arch.vpa.pinned_addr);
	spin_unlock(&vcpu->arch.vpa_update_lock);
	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, vcpu);
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

extern int __kvmppc_vcore_entry(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu);
extern void xics_wake_cpu(int cpu);

static void kvmppc_remove_runnable(struct kvmppc_vcore *vc,
				   struct kvm_vcpu *vcpu)
{
	u64 now;

	if (vcpu->arch.state != KVMPPC_VCPU_RUNNABLE)
		return;
	spin_lock(&vcpu->arch.tbacct_lock);
	now = mftb();
	vcpu->arch.busy_stolen += vcore_stolen_time(vc, now) -
		vcpu->arch.stolen_logged;
	vcpu->arch.busy_preempt = now;
	vcpu->arch.state = KVMPPC_VCPU_BUSY_IN_HOST;
	spin_unlock(&vcpu->arch.tbacct_lock);
	--vc->n_runnable;
	list_del(&vcpu->arch.run_list);
}

static int kvmppc_grab_hwthread(int cpu)
{
	struct paca_struct *tpaca;
	long timeout = 1000;

	tpaca = &paca[cpu];

	/* Ensure the thread won't go into the kernel if it wakes */
	tpaca->kvm_hstate.hwthread_req = 1;
	tpaca->kvm_hstate.kvm_vcpu = NULL;

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
	tpaca->kvm_hstate.kvm_vcpu = vcpu;
	tpaca->kvm_hstate.kvm_vcore = vc;
	tpaca->kvm_hstate.napping = 0;
	vcpu->cpu = vc->pcpu;
	smp_wmb();
#if defined(CONFIG_PPC_ICP_NATIVE) && defined(CONFIG_SMP)
	if (vcpu->arch.ptid) {
		xics_wake_cpu(cpu);
		++vc->n_woken;
	}
#endif
}

static void kvmppc_wait_for_nap(struct kvmppc_vcore *vc)
{
	int i;

	HMT_low();
	i = 0;
	while (vc->nap_count < vc->n_woken) {
		if (++i >= 1000000) {
			pr_err("kvmppc_wait_for_nap timeout %d %d\n",
			       vc->nap_count, vc->n_woken);
			break;
		}
		cpu_relax();
	}
	HMT_medium();
}

/*
 * Check that we are on thread 0 and that any other threads in
 * this core are off-line.  Then grab the threads so they can't
 * enter the kernel.
 */
static int on_primary_thread(void)
{
	int cpu = smp_processor_id();
	int thr = cpu_thread_in_core(cpu);

	if (thr)
		return 0;
	while (++thr < threads_per_core)
		if (cpu_online(cpu + thr))
			return 0;

	/* Grab all hw threads so they can't go into the kernel */
	for (thr = 1; thr < threads_per_core; ++thr) {
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

/*
 * Run a set of guest threads on a physical core.
 * Called with vc->lock held.
 */
static void kvmppc_run_core(struct kvmppc_vcore *vc)
{
	struct kvm_vcpu *vcpu, *vcpu0, *vnext;
	long ret;
	u64 now;
	int ptid, i, need_vpa_update;
	int srcu_idx;
	struct kvm_vcpu *vcpus_to_update[threads_per_core];

	/* don't start if any threads have a signal pending */
	need_vpa_update = 0;
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list) {
		if (signal_pending(vcpu->arch.run_task))
			return;
		if (vcpu->arch.vpa.update_pending ||
		    vcpu->arch.slb_shadow.update_pending ||
		    vcpu->arch.dtl.update_pending)
			vcpus_to_update[need_vpa_update++] = vcpu;
	}

	/*
	 * Initialize *vc, in particular vc->vcore_state, so we can
	 * drop the vcore lock if necessary.
	 */
	vc->n_woken = 0;
	vc->nap_count = 0;
	vc->entry_exit_count = 0;
	vc->vcore_state = VCORE_STARTING;
	vc->in_guest = 0;
	vc->napping_threads = 0;

	/*
	 * Updating any of the vpas requires calling kvmppc_pin_guest_page,
	 * which can't be called with any spinlocks held.
	 */
	if (need_vpa_update) {
		spin_unlock(&vc->lock);
		for (i = 0; i < need_vpa_update; ++i)
			kvmppc_update_vpas(vcpus_to_update[i]);
		spin_lock(&vc->lock);
	}

	/*
	 * Assign physical thread IDs, first to non-ceded vcpus
	 * and then to ceded ones.
	 */
	ptid = 0;
	vcpu0 = NULL;
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list) {
		if (!vcpu->arch.ceded) {
			if (!ptid)
				vcpu0 = vcpu;
			vcpu->arch.ptid = ptid++;
		}
	}
	if (!vcpu0)
		goto out;	/* nothing to run; should never happen */
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list)
		if (vcpu->arch.ceded)
			vcpu->arch.ptid = ptid++;

	/*
	 * Make sure we are running on thread 0, and that
	 * secondary threads are offline.
	 */
	if (threads_per_core > 1 && !on_primary_thread()) {
		list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list)
			vcpu->arch.ret = -EBUSY;
		goto out;
	}

	vc->pcpu = smp_processor_id();
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list) {
		kvmppc_start_thread(vcpu);
		kvmppc_create_dtl_entry(vcpu, vc);
	}

	vc->vcore_state = VCORE_RUNNING;
	preempt_disable();
	spin_unlock(&vc->lock);

	kvm_guest_enter();

	srcu_idx = srcu_read_lock(&vcpu0->kvm->srcu);

	__kvmppc_vcore_entry(NULL, vcpu0);

	spin_lock(&vc->lock);
	/* disable sending of IPIs on virtual external irqs */
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list)
		vcpu->cpu = -1;
	/* wait for secondary threads to finish writing their state to memory */
	if (vc->nap_count < vc->n_woken)
		kvmppc_wait_for_nap(vc);
	for (i = 0; i < threads_per_core; ++i)
		kvmppc_release_hwthread(vc->pcpu + i);
	/* prevent other vcpu threads from doing kvmppc_start_thread() now */
	vc->vcore_state = VCORE_EXITING;
	spin_unlock(&vc->lock);

	srcu_read_unlock(&vcpu0->kvm->srcu, srcu_idx);

	/* make sure updates to secondary vcpu structs are visible now */
	smp_mb();
	kvm_guest_exit();

	preempt_enable();
	kvm_resched(vcpu);

	spin_lock(&vc->lock);
	now = get_tb();
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list) {
		/* cancel pending dec exception if dec is positive */
		if (now < vcpu->arch.dec_expires &&
		    kvmppc_core_pending_dec(vcpu))
			kvmppc_core_dequeue_dec(vcpu);

		ret = RESUME_GUEST;
		if (vcpu->arch.trap)
			ret = kvmppc_handle_exit(vcpu->arch.kvm_run, vcpu,
						 vcpu->arch.run_task);

		vcpu->arch.ret = ret;
		vcpu->arch.trap = 0;

		if (vcpu->arch.ceded) {
			if (ret != RESUME_GUEST)
				kvmppc_end_cede(vcpu);
			else
				kvmppc_set_timer(vcpu);
		}
	}

 out:
	vc->vcore_state = VCORE_INACTIVE;
	list_for_each_entry_safe(vcpu, vnext, &vc->runnable_threads,
				 arch.run_list) {
		if (vcpu->arch.ret != RESUME_GUEST) {
			kvmppc_remove_runnable(vc, vcpu);
			wake_up(&vcpu->arch.cpu_run);
		}
	}
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
	DEFINE_WAIT(wait);

	prepare_to_wait(&vc->wq, &wait, TASK_INTERRUPTIBLE);
	vc->vcore_state = VCORE_SLEEPING;
	spin_unlock(&vc->lock);
	schedule();
	finish_wait(&vc->wq, &wait);
	spin_lock(&vc->lock);
	vc->vcore_state = VCORE_INACTIVE;
}

static int kvmppc_run_vcpu(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	int n_ceded;
	struct kvmppc_vcore *vc;
	struct kvm_vcpu *v, *vn;

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
		if (vc->vcore_state == VCORE_RUNNING &&
		    VCORE_EXIT_COUNT(vc) == 0) {
			vcpu->arch.ptid = vc->n_runnable - 1;
			kvmppc_create_dtl_entry(vcpu, vc);
			kvmppc_start_thread(vcpu);
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
		vc->runner = vcpu;
		n_ceded = 0;
		list_for_each_entry(v, &vc->runnable_threads, arch.run_list)
			if (!v->arch.pending_exceptions)
				n_ceded += v->arch.ceded;
		if (n_ceded == vc->n_runnable)
			kvmppc_vcore_blocked(vc);
		else
			kvmppc_run_core(vc);
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

	spin_unlock(&vc->lock);
	return vcpu->arch.ret;
}

int kvmppc_vcpu_run(struct kvm_run *run, struct kvm_vcpu *vcpu)
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
	/* Order vcpus_running vs. rma_setup_done, see kvmppc_alloc_reset_hpt */
	smp_mb();

	/* On the first time here, set up HTAB and VRMA or RMA */
	if (!vcpu->kvm->arch.rma_setup_done) {
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
			r = kvmppc_pseries_do_hcall(vcpu);
			kvmppc_core_prepare_to_enter(vcpu);
		} else if (r == RESUME_PAGE_FAULT) {
			srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
			r = kvmppc_book3s_hv_page_fault(run, vcpu,
				vcpu->arch.fault_dar, vcpu->arch.fault_dsisr);
			srcu_read_unlock(&vcpu->kvm->srcu, srcu_idx);
		}
	} while (r == RESUME_GUEST);

 out:
	vcpu->arch.state = KVMPPC_VCPU_NOTREADY;
	atomic_dec(&vcpu->kvm->arch.vcpus_running);
	return r;
}


/* Work out RMLS (real mode limit selector) field value for a given RMA size.
   Assumes POWER7 or PPC970. */
static inline int lpcr_rmls(unsigned long rma_size)
{
	switch (rma_size) {
	case 32ul << 20:	/* 32 MB */
		if (cpu_has_feature(CPU_FTR_ARCH_206))
			return 8;	/* only supported on POWER7 */
		return -1;
	case 64ul << 20:	/* 64 MB */
		return 3;
	case 128ul << 20:	/* 128 MB */
		return 7;
	case 256ul << 20:	/* 256 MB */
		return 4;
	case 1ul << 30:		/* 1 GB */
		return 2;
	case 16ul << 30:	/* 16 GB */
		return 1;
	case 256ul << 30:	/* 256 GB */
		return 0;
	default:
		return -1;
	}
}

static int kvm_rma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct kvmppc_linear_info *ri = vma->vm_file->private_data;
	struct page *page;

	if (vmf->pgoff >= ri->npages)
		return VM_FAULT_SIGBUS;

	page = pfn_to_page(ri->base_pfn + vmf->pgoff);
	get_page(page);
	vmf->page = page;
	return 0;
}

static const struct vm_operations_struct kvm_rma_vm_ops = {
	.fault = kvm_rma_fault,
};

static int kvm_rma_mmap(struct file *file, struct vm_area_struct *vma)
{
	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_ops = &kvm_rma_vm_ops;
	return 0;
}

static int kvm_rma_release(struct inode *inode, struct file *filp)
{
	struct kvmppc_linear_info *ri = filp->private_data;

	kvm_release_rma(ri);
	return 0;
}

static struct file_operations kvm_rma_fops = {
	.mmap           = kvm_rma_mmap,
	.release	= kvm_rma_release,
};

long kvm_vm_ioctl_allocate_rma(struct kvm *kvm, struct kvm_allocate_rma *ret)
{
	struct kvmppc_linear_info *ri;
	long fd;

	ri = kvm_alloc_rma();
	if (!ri)
		return -ENOMEM;

	fd = anon_inode_getfd("kvm-rma", &kvm_rma_fops, ri, O_RDWR);
	if (fd < 0)
		kvm_release_rma(ri);

	ret->rma_size = ri->npages << PAGE_SHIFT;
	return fd;
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
	(*sps)->enc[0].pte_enc = def->penc;
	(*sps)++;
}

int kvm_vm_ioctl_get_smmu_info(struct kvm *kvm, struct kvm_ppc_smmu_info *info)
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
int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm, struct kvm_dirty_log *log)
{
	struct kvm_memory_slot *memslot;
	int r;
	unsigned long n;

	mutex_lock(&kvm->slots_lock);

	r = -EINVAL;
	if (log->slot >= KVM_MEMORY_SLOTS)
		goto out;

	memslot = id_to_memslot(kvm->memslots, log->slot);
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

static unsigned long slb_pgsize_encoding(unsigned long psize)
{
	unsigned long senc = 0;

	if (psize > 0x1000) {
		senc = SLB_VSID_L;
		if (psize == 0x10000)
			senc |= SLB_VSID_LP_01;
	}
	return senc;
}

static void unpin_slot(struct kvm_memory_slot *memslot)
{
	unsigned long *physp;
	unsigned long j, npages, pfn;
	struct page *page;

	physp = memslot->arch.slot_phys;
	npages = memslot->npages;
	if (!physp)
		return;
	for (j = 0; j < npages; j++) {
		if (!(physp[j] & KVMPPC_GOT_PAGE))
			continue;
		pfn = physp[j] >> PAGE_SHIFT;
		page = pfn_to_page(pfn);
		SetPageDirty(page);
		put_page(page);
	}
}

void kvmppc_core_free_memslot(struct kvm_memory_slot *free,
			      struct kvm_memory_slot *dont)
{
	if (!dont || free->arch.rmap != dont->arch.rmap) {
		vfree(free->arch.rmap);
		free->arch.rmap = NULL;
	}
	if (!dont || free->arch.slot_phys != dont->arch.slot_phys) {
		unpin_slot(free);
		vfree(free->arch.slot_phys);
		free->arch.slot_phys = NULL;
	}
}

int kvmppc_core_create_memslot(struct kvm_memory_slot *slot,
			       unsigned long npages)
{
	slot->arch.rmap = vzalloc(npages * sizeof(*slot->arch.rmap));
	if (!slot->arch.rmap)
		return -ENOMEM;
	slot->arch.slot_phys = NULL;

	return 0;
}

int kvmppc_core_prepare_memory_region(struct kvm *kvm,
				      struct kvm_memory_slot *memslot,
				      struct kvm_userspace_memory_region *mem)
{
	unsigned long *phys;

	/* Allocate a slot_phys array if needed */
	phys = memslot->arch.slot_phys;
	if (!kvm->arch.using_mmu_notifiers && !phys && memslot->npages) {
		phys = vzalloc(memslot->npages * sizeof(unsigned long));
		if (!phys)
			return -ENOMEM;
		memslot->arch.slot_phys = phys;
	}

	return 0;
}

void kvmppc_core_commit_memory_region(struct kvm *kvm,
				      struct kvm_userspace_memory_region *mem,
				      struct kvm_memory_slot old)
{
	unsigned long npages = mem->memory_size >> PAGE_SHIFT;
	struct kvm_memory_slot *memslot;

	if (npages && old.npages) {
		/*
		 * If modifying a memslot, reset all the rmap dirty bits.
		 * If this is a new memslot, we don't need to do anything
		 * since the rmap array starts out as all zeroes,
		 * i.e. no pages are dirty.
		 */
		memslot = id_to_memslot(kvm->memslots, mem->slot);
		kvmppc_hv_get_dirty_log(kvm, memslot, NULL);
	}
}

static int kvmppc_hv_setup_htab_rma(struct kvm_vcpu *vcpu)
{
	int err = 0;
	struct kvm *kvm = vcpu->kvm;
	struct kvmppc_linear_info *ri = NULL;
	unsigned long hva;
	struct kvm_memory_slot *memslot;
	struct vm_area_struct *vma;
	unsigned long lpcr, senc;
	unsigned long psize, porder;
	unsigned long rma_size;
	unsigned long rmls;
	unsigned long *physp;
	unsigned long i, npages;
	int srcu_idx;

	mutex_lock(&kvm->lock);
	if (kvm->arch.rma_setup_done)
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

	/* Is this one of our preallocated RMAs? */
	if (vma->vm_file && vma->vm_file->f_op == &kvm_rma_fops &&
	    hva == vma->vm_start)
		ri = vma->vm_file->private_data;

	up_read(&current->mm->mmap_sem);

	if (!ri) {
		/* On POWER7, use VRMA; on PPC970, give up */
		err = -EPERM;
		if (cpu_has_feature(CPU_FTR_ARCH_201)) {
			pr_err("KVM: CPU requires an RMO\n");
			goto out_srcu;
		}

		/* We can handle 4k, 64k or 16M pages in the VRMA */
		err = -EINVAL;
		if (!(psize == 0x1000 || psize == 0x10000 ||
		      psize == 0x1000000))
			goto out_srcu;

		/* Update VRMASD field in the LPCR */
		senc = slb_pgsize_encoding(psize);
		kvm->arch.vrma_slb_v = senc | SLB_VSID_B_1T |
			(VRMA_VSID << SLB_VSID_SHIFT_1T);
		lpcr = kvm->arch.lpcr & ~LPCR_VRMASD;
		lpcr |= senc << (LPCR_VRMASD_SH - 4);
		kvm->arch.lpcr = lpcr;

		/* Create HPTEs in the hash page table for the VRMA */
		kvmppc_map_vrma(vcpu, memslot, porder);

	} else {
		/* Set up to use an RMO region */
		rma_size = ri->npages;
		if (rma_size > memslot->npages)
			rma_size = memslot->npages;
		rma_size <<= PAGE_SHIFT;
		rmls = lpcr_rmls(rma_size);
		err = -EINVAL;
		if (rmls < 0) {
			pr_err("KVM: Can't use RMA of 0x%lx bytes\n", rma_size);
			goto out_srcu;
		}
		atomic_inc(&ri->use_count);
		kvm->arch.rma = ri;

		/* Update LPCR and RMOR */
		lpcr = kvm->arch.lpcr;
		if (cpu_has_feature(CPU_FTR_ARCH_201)) {
			/* PPC970; insert RMLS value (split field) in HID4 */
			lpcr &= ~((1ul << HID4_RMLS0_SH) |
				  (3ul << HID4_RMLS2_SH));
			lpcr |= ((rmls >> 2) << HID4_RMLS0_SH) |
				((rmls & 3) << HID4_RMLS2_SH);
			/* RMOR is also in HID4 */
			lpcr |= ((ri->base_pfn >> (26 - PAGE_SHIFT)) & 0xffff)
				<< HID4_RMOR_SH;
		} else {
			/* POWER7 */
			lpcr &= ~(LPCR_VPM0 | LPCR_VRMA_L);
			lpcr |= rmls << LPCR_RMLS_SH;
			kvm->arch.rmor = kvm->arch.rma->base_pfn << PAGE_SHIFT;
		}
		kvm->arch.lpcr = lpcr;
		pr_info("KVM: Using RMO at %lx size %lx (LPCR = %lx)\n",
			ri->base_pfn << PAGE_SHIFT, rma_size, lpcr);

		/* Initialize phys addrs of pages in RMO */
		npages = ri->npages;
		porder = __ilog2(npages);
		physp = memslot->arch.slot_phys;
		if (physp) {
			if (npages > memslot->npages)
				npages = memslot->npages;
			spin_lock(&kvm->arch.slot_phys_lock);
			for (i = 0; i < npages; ++i)
				physp[i] = ((ri->base_pfn + i) << PAGE_SHIFT) +
					porder;
			spin_unlock(&kvm->arch.slot_phys_lock);
		}
	}

	/* Order updates to kvm->arch.lpcr etc. vs. rma_setup_done */
	smp_wmb();
	kvm->arch.rma_setup_done = 1;
	err = 0;
 out_srcu:
	srcu_read_unlock(&kvm->srcu, srcu_idx);
 out:
	mutex_unlock(&kvm->lock);
	return err;

 up_out:
	up_read(&current->mm->mmap_sem);
	goto out;
}

int kvmppc_core_init_vm(struct kvm *kvm)
{
	unsigned long lpcr, lpid;

	/* Allocate the guest's logical partition ID */

	lpid = kvmppc_alloc_lpid();
	if (lpid < 0)
		return -ENOMEM;
	kvm->arch.lpid = lpid;

	INIT_LIST_HEAD(&kvm->arch.spapr_tce_tables);

	kvm->arch.rma = NULL;

	kvm->arch.host_sdr1 = mfspr(SPRN_SDR1);

	if (cpu_has_feature(CPU_FTR_ARCH_201)) {
		/* PPC970; HID4 is effectively the LPCR */
		kvm->arch.host_lpid = 0;
		kvm->arch.host_lpcr = lpcr = mfspr(SPRN_HID4);
		lpcr &= ~((3 << HID4_LPID1_SH) | (0xful << HID4_LPID5_SH));
		lpcr |= ((lpid >> 4) << HID4_LPID1_SH) |
			((lpid & 0xf) << HID4_LPID5_SH);
	} else {
		/* POWER7; init LPCR for virtual RMA mode */
		kvm->arch.host_lpid = mfspr(SPRN_LPID);
		kvm->arch.host_lpcr = lpcr = mfspr(SPRN_LPCR);
		lpcr &= LPCR_PECE | LPCR_LPES;
		lpcr |= (4UL << LPCR_DPFD_SH) | LPCR_HDICE |
			LPCR_VPM0 | LPCR_VPM1;
		kvm->arch.vrma_slb_v = SLB_VSID_B_1T |
			(VRMA_VSID << SLB_VSID_SHIFT_1T);
	}
	kvm->arch.lpcr = lpcr;

	kvm->arch.using_mmu_notifiers = !!cpu_has_feature(CPU_FTR_ARCH_206);
	spin_lock_init(&kvm->arch.slot_phys_lock);

	/*
	 * Don't allow secondary CPU threads to come online
	 * while any KVM VMs exist.
	 */
	inhibit_secondary_onlining();

	return 0;
}

void kvmppc_core_destroy_vm(struct kvm *kvm)
{
	uninhibit_secondary_onlining();

	if (kvm->arch.rma) {
		kvm_release_rma(kvm->arch.rma);
		kvm->arch.rma = NULL;
	}

	kvmppc_free_hpt(kvm);
	WARN_ON(!list_empty(&kvm->arch.spapr_tce_tables));
}

/* These are stubs for now */
void kvmppc_mmu_pte_pflush(struct kvm_vcpu *vcpu, ulong pa_start, ulong pa_end)
{
}

/* We don't need to emulate any privileged instructions or dcbz */
int kvmppc_core_emulate_op(struct kvm_run *run, struct kvm_vcpu *vcpu,
                           unsigned int inst, int *advance)
{
	return EMULATE_FAIL;
}

int kvmppc_core_emulate_mtspr(struct kvm_vcpu *vcpu, int sprn, ulong spr_val)
{
	return EMULATE_FAIL;
}

int kvmppc_core_emulate_mfspr(struct kvm_vcpu *vcpu, int sprn, ulong *spr_val)
{
	return EMULATE_FAIL;
}

static int kvmppc_book3s_hv_init(void)
{
	int r;

	r = kvm_init(NULL, sizeof(struct kvm_vcpu), 0, THIS_MODULE);

	if (r)
		return r;

	r = kvmppc_mmu_hv_init();

	return r;
}

static void kvmppc_book3s_hv_exit(void)
{
	kvm_exit();
}

module_init(kvmppc_book3s_hv_init);
module_exit(kvmppc_book3s_hv_exit);
