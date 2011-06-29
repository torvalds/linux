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
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/cpumask.h>

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
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>

/* #define EXIT_DEBUG */
/* #define EXIT_DEBUG_SIMPLE */
/* #define EXIT_DEBUG_INT */

void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	local_paca->kvm_hstate.kvm_vcpu = vcpu;
}

void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu)
{
}

void kvmppc_vcpu_block(struct kvm_vcpu *vcpu)
{
	u64 now;
	unsigned long dec_nsec;

	now = get_tb();
	if (now >= vcpu->arch.dec_expires && !kvmppc_core_pending_dec(vcpu))
		kvmppc_core_queue_dec(vcpu);
	if (vcpu->arch.pending_exceptions)
		return;
	if (vcpu->arch.dec_expires != ~(u64)0) {
		dec_nsec = (vcpu->arch.dec_expires - now) * NSEC_PER_SEC /
			tb_ticks_per_sec;
		hrtimer_start(&vcpu->arch.dec_timer, ktime_set(0, dec_nsec),
			      HRTIMER_MODE_REL);
	}

	kvm_vcpu_block(vcpu);
	vcpu->stat.halt_wakeup++;

	if (vcpu->arch.dec_expires != ~(u64)0)
		hrtimer_try_to_cancel(&vcpu->arch.dec_timer);
}

void kvmppc_set_msr(struct kvm_vcpu *vcpu, u64 msr)
{
	vcpu->arch.shregs.msr = msr;
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
	       vcpu->arch.lpcr, vcpu->kvm->arch.sdr1,
	       vcpu->arch.last_inst);
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
	 * We get these next two if the guest does a bad real-mode access,
	 * as we have enabled VRMA (virtualized real mode area) mode in the
	 * LPCR.  We just generate an appropriate DSI/ISI to the guest.
	 */
	case BOOK3S_INTERRUPT_H_DATA_STORAGE:
		vcpu->arch.shregs.dsisr = vcpu->arch.fault_dsisr;
		vcpu->arch.shregs.dar = vcpu->arch.fault_dar;
		kvmppc_inject_interrupt(vcpu, BOOK3S_INTERRUPT_DATA_STORAGE, 0);
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_H_INST_STORAGE:
		kvmppc_inject_interrupt(vcpu, BOOK3S_INTERRUPT_INST_STORAGE,
					0x08000000);
		r = RESUME_GUEST;
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


	if (!(r & RESUME_HOST)) {
		/* To avoid clobbering exit_reason, only check for signals if
		 * we aren't already exiting to userspace for some other
		 * reason. */
		if (signal_pending(tsk)) {
			vcpu->stat.signal_exits++;
			run->exit_reason = KVM_EXIT_INTR;
			r = -EINTR;
		} else {
			kvmppc_core_deliver_interrupts(vcpu);
		}
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

int kvmppc_core_check_processor_compat(void)
{
	if (cpu_has_feature(CPU_FTR_HVMODE_206))
		return 0;
	return -EIO;
}

struct kvm_vcpu *kvmppc_core_vcpu_create(struct kvm *kvm, unsigned int id)
{
	struct kvm_vcpu *vcpu;
	int err = -ENOMEM;
	unsigned long lpcr;

	vcpu = kzalloc(sizeof(struct kvm_vcpu), GFP_KERNEL);
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

	lpcr = kvm->arch.host_lpcr & (LPCR_PECE | LPCR_LPES);
	lpcr |= LPCR_VPM0 | LPCR_VRMA_L | (4UL << LPCR_DPFD_SH) | LPCR_HDICE;
	vcpu->arch.lpcr = lpcr;

	kvmppc_mmu_book3s_hv_init(vcpu);

	return vcpu;

free_vcpu:
	kfree(vcpu);
out:
	return ERR_PTR(err);
}

void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu)
{
	kvm_vcpu_uninit(vcpu);
	kfree(vcpu);
}

extern int __kvmppc_vcore_entry(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu);

int kvmppc_vcpu_run(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	u64 now;

	if (signal_pending(current)) {
		run->exit_reason = KVM_EXIT_INTR;
		return -EINTR;
	}

	flush_fp_to_thread(current);
	flush_altivec_to_thread(current);
	flush_vsx_to_thread(current);
	preempt_disable();

	/*
	 * Make sure we are running on thread 0, and that
	 * secondary threads are offline.
	 * XXX we should also block attempts to bring any
	 * secondary threads online.
	 */
	if (threads_per_core > 1) {
		int cpu = smp_processor_id();
		int thr = cpu_thread_in_core(cpu);

		if (thr)
			goto out;
		while (++thr < threads_per_core)
			if (cpu_online(cpu + thr))
				goto out;
	}

	kvm_guest_enter();

	__kvmppc_vcore_entry(NULL, vcpu);

	kvm_guest_exit();

	preempt_enable();
	kvm_resched(vcpu);

	now = get_tb();
	/* cancel pending dec exception if dec is positive */
	if (now < vcpu->arch.dec_expires && kvmppc_core_pending_dec(vcpu))
		kvmppc_core_dequeue_dec(vcpu);

	return kvmppc_handle_exit(run, vcpu, current);

 out:
	preempt_enable();
	return -EBUSY;
}

int kvmppc_core_prepare_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem)
{
	if (mem->guest_phys_addr == 0 && mem->memory_size != 0)
		return kvmppc_prepare_vrma(kvm, mem);
	return 0;
}

void kvmppc_core_commit_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem)
{
	if (mem->guest_phys_addr == 0 && mem->memory_size != 0)
		kvmppc_map_vrma(kvm, mem);
}

int kvmppc_core_init_vm(struct kvm *kvm)
{
	long r;

	/* Allocate hashed page table */
	r = kvmppc_alloc_hpt(kvm);

	return r;
}

void kvmppc_core_destroy_vm(struct kvm *kvm)
{
	kvmppc_free_hpt(kvm);
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

int kvmppc_core_emulate_mtspr(struct kvm_vcpu *vcpu, int sprn, int rs)
{
	return EMULATE_FAIL;
}

int kvmppc_core_emulate_mfspr(struct kvm_vcpu *vcpu, int sprn, int rt)
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
