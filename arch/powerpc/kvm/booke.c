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
 * Copyright IBM Corp. 2007
 * Copyright 2010-2011 Freescale Semiconductor, Inc.
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 *          Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 *          Scott Wood <scottwood@freescale.com>
 *          Varun Sethi <varun.sethi@freescale.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>

#include <asm/cputable.h>
#include <asm/uaccess.h>
#include <asm/kvm_ppc.h>
#include <asm/cacheflush.h>
#include <asm/dbell.h>
#include <asm/hw_irq.h>
#include <asm/irq.h>

#include "timing.h"
#include "booke.h"

unsigned long kvmppc_booke_handlers;

#define VM_STAT(x) offsetof(struct kvm, stat.x), KVM_STAT_VM
#define VCPU_STAT(x) offsetof(struct kvm_vcpu, stat.x), KVM_STAT_VCPU

struct kvm_stats_debugfs_item debugfs_entries[] = {
	{ "mmio",       VCPU_STAT(mmio_exits) },
	{ "dcr",        VCPU_STAT(dcr_exits) },
	{ "sig",        VCPU_STAT(signal_exits) },
	{ "itlb_r",     VCPU_STAT(itlb_real_miss_exits) },
	{ "itlb_v",     VCPU_STAT(itlb_virt_miss_exits) },
	{ "dtlb_r",     VCPU_STAT(dtlb_real_miss_exits) },
	{ "dtlb_v",     VCPU_STAT(dtlb_virt_miss_exits) },
	{ "sysc",       VCPU_STAT(syscall_exits) },
	{ "isi",        VCPU_STAT(isi_exits) },
	{ "dsi",        VCPU_STAT(dsi_exits) },
	{ "inst_emu",   VCPU_STAT(emulated_inst_exits) },
	{ "dec",        VCPU_STAT(dec_exits) },
	{ "ext_intr",   VCPU_STAT(ext_intr_exits) },
	{ "halt_wakeup", VCPU_STAT(halt_wakeup) },
	{ "doorbell", VCPU_STAT(dbell_exits) },
	{ "guest doorbell", VCPU_STAT(gdbell_exits) },
	{ NULL }
};

/* TODO: use vcpu_printf() */
void kvmppc_dump_vcpu(struct kvm_vcpu *vcpu)
{
	int i;

	printk("pc:   %08lx msr:  %08llx\n", vcpu->arch.pc, vcpu->arch.shared->msr);
	printk("lr:   %08lx ctr:  %08lx\n", vcpu->arch.lr, vcpu->arch.ctr);
	printk("srr0: %08llx srr1: %08llx\n", vcpu->arch.shared->srr0,
					    vcpu->arch.shared->srr1);

	printk("exceptions: %08lx\n", vcpu->arch.pending_exceptions);

	for (i = 0; i < 32; i += 4) {
		printk("gpr%02d: %08lx %08lx %08lx %08lx\n", i,
		       kvmppc_get_gpr(vcpu, i),
		       kvmppc_get_gpr(vcpu, i+1),
		       kvmppc_get_gpr(vcpu, i+2),
		       kvmppc_get_gpr(vcpu, i+3));
	}
}

#ifdef CONFIG_SPE
void kvmppc_vcpu_disable_spe(struct kvm_vcpu *vcpu)
{
	preempt_disable();
	enable_kernel_spe();
	kvmppc_save_guest_spe(vcpu);
	vcpu->arch.shadow_msr &= ~MSR_SPE;
	preempt_enable();
}

static void kvmppc_vcpu_enable_spe(struct kvm_vcpu *vcpu)
{
	preempt_disable();
	enable_kernel_spe();
	kvmppc_load_guest_spe(vcpu);
	vcpu->arch.shadow_msr |= MSR_SPE;
	preempt_enable();
}

static void kvmppc_vcpu_sync_spe(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.shared->msr & MSR_SPE) {
		if (!(vcpu->arch.shadow_msr & MSR_SPE))
			kvmppc_vcpu_enable_spe(vcpu);
	} else if (vcpu->arch.shadow_msr & MSR_SPE) {
		kvmppc_vcpu_disable_spe(vcpu);
	}
}
#else
static void kvmppc_vcpu_sync_spe(struct kvm_vcpu *vcpu)
{
}
#endif

/*
 * Helper function for "full" MSR writes.  No need to call this if only
 * EE/CE/ME/DE/RI are changing.
 */
void kvmppc_set_msr(struct kvm_vcpu *vcpu, u32 new_msr)
{
	u32 old_msr = vcpu->arch.shared->msr;

#ifdef CONFIG_KVM_BOOKE_HV
	new_msr |= MSR_GS;
#endif

	vcpu->arch.shared->msr = new_msr;

	kvmppc_mmu_msr_notify(vcpu, old_msr);
	kvmppc_vcpu_sync_spe(vcpu);
}

static void kvmppc_booke_queue_irqprio(struct kvm_vcpu *vcpu,
                                       unsigned int priority)
{
	set_bit(priority, &vcpu->arch.pending_exceptions);
}

static void kvmppc_core_queue_dtlb_miss(struct kvm_vcpu *vcpu,
                                        ulong dear_flags, ulong esr_flags)
{
	vcpu->arch.queued_dear = dear_flags;
	vcpu->arch.queued_esr = esr_flags;
	kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_DTLB_MISS);
}

static void kvmppc_core_queue_data_storage(struct kvm_vcpu *vcpu,
                                           ulong dear_flags, ulong esr_flags)
{
	vcpu->arch.queued_dear = dear_flags;
	vcpu->arch.queued_esr = esr_flags;
	kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_DATA_STORAGE);
}

static void kvmppc_core_queue_inst_storage(struct kvm_vcpu *vcpu,
                                           ulong esr_flags)
{
	vcpu->arch.queued_esr = esr_flags;
	kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_INST_STORAGE);
}

void kvmppc_core_queue_program(struct kvm_vcpu *vcpu, ulong esr_flags)
{
	vcpu->arch.queued_esr = esr_flags;
	kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_PROGRAM);
}

void kvmppc_core_queue_dec(struct kvm_vcpu *vcpu)
{
	kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_DECREMENTER);
}

int kvmppc_core_pending_dec(struct kvm_vcpu *vcpu)
{
	return test_bit(BOOKE_IRQPRIO_DECREMENTER, &vcpu->arch.pending_exceptions);
}

void kvmppc_core_dequeue_dec(struct kvm_vcpu *vcpu)
{
	clear_bit(BOOKE_IRQPRIO_DECREMENTER, &vcpu->arch.pending_exceptions);
}

void kvmppc_core_queue_external(struct kvm_vcpu *vcpu,
                                struct kvm_interrupt *irq)
{
	unsigned int prio = BOOKE_IRQPRIO_EXTERNAL;

	if (irq->irq == KVM_INTERRUPT_SET_LEVEL)
		prio = BOOKE_IRQPRIO_EXTERNAL_LEVEL;

	kvmppc_booke_queue_irqprio(vcpu, prio);
}

void kvmppc_core_dequeue_external(struct kvm_vcpu *vcpu,
                                  struct kvm_interrupt *irq)
{
	clear_bit(BOOKE_IRQPRIO_EXTERNAL, &vcpu->arch.pending_exceptions);
	clear_bit(BOOKE_IRQPRIO_EXTERNAL_LEVEL, &vcpu->arch.pending_exceptions);
}

static void set_guest_srr(struct kvm_vcpu *vcpu, unsigned long srr0, u32 srr1)
{
#ifdef CONFIG_KVM_BOOKE_HV
	mtspr(SPRN_GSRR0, srr0);
	mtspr(SPRN_GSRR1, srr1);
#else
	vcpu->arch.shared->srr0 = srr0;
	vcpu->arch.shared->srr1 = srr1;
#endif
}

static void set_guest_csrr(struct kvm_vcpu *vcpu, unsigned long srr0, u32 srr1)
{
	vcpu->arch.csrr0 = srr0;
	vcpu->arch.csrr1 = srr1;
}

static void set_guest_dsrr(struct kvm_vcpu *vcpu, unsigned long srr0, u32 srr1)
{
	if (cpu_has_feature(CPU_FTR_DEBUG_LVL_EXC)) {
		vcpu->arch.dsrr0 = srr0;
		vcpu->arch.dsrr1 = srr1;
	} else {
		set_guest_csrr(vcpu, srr0, srr1);
	}
}

static void set_guest_mcsrr(struct kvm_vcpu *vcpu, unsigned long srr0, u32 srr1)
{
	vcpu->arch.mcsrr0 = srr0;
	vcpu->arch.mcsrr1 = srr1;
}

static unsigned long get_guest_dear(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_KVM_BOOKE_HV
	return mfspr(SPRN_GDEAR);
#else
	return vcpu->arch.shared->dar;
#endif
}

static void set_guest_dear(struct kvm_vcpu *vcpu, unsigned long dear)
{
#ifdef CONFIG_KVM_BOOKE_HV
	mtspr(SPRN_GDEAR, dear);
#else
	vcpu->arch.shared->dar = dear;
#endif
}

static unsigned long get_guest_esr(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_KVM_BOOKE_HV
	return mfspr(SPRN_GESR);
#else
	return vcpu->arch.shared->esr;
#endif
}

static void set_guest_esr(struct kvm_vcpu *vcpu, u32 esr)
{
#ifdef CONFIG_KVM_BOOKE_HV
	mtspr(SPRN_GESR, esr);
#else
	vcpu->arch.shared->esr = esr;
#endif
}

/* Deliver the interrupt of the corresponding priority, if possible. */
static int kvmppc_booke_irqprio_deliver(struct kvm_vcpu *vcpu,
                                        unsigned int priority)
{
	int allowed = 0;
	ulong msr_mask = 0;
	bool update_esr = false, update_dear = false;
	ulong crit_raw = vcpu->arch.shared->critical;
	ulong crit_r1 = kvmppc_get_gpr(vcpu, 1);
	bool crit;
	bool keep_irq = false;
	enum int_class int_class;

	/* Truncate crit indicators in 32 bit mode */
	if (!(vcpu->arch.shared->msr & MSR_SF)) {
		crit_raw &= 0xffffffff;
		crit_r1 &= 0xffffffff;
	}

	/* Critical section when crit == r1 */
	crit = (crit_raw == crit_r1);
	/* ... and we're in supervisor mode */
	crit = crit && !(vcpu->arch.shared->msr & MSR_PR);

	if (priority == BOOKE_IRQPRIO_EXTERNAL_LEVEL) {
		priority = BOOKE_IRQPRIO_EXTERNAL;
		keep_irq = true;
	}

	switch (priority) {
	case BOOKE_IRQPRIO_DTLB_MISS:
	case BOOKE_IRQPRIO_DATA_STORAGE:
		update_dear = true;
		/* fall through */
	case BOOKE_IRQPRIO_INST_STORAGE:
	case BOOKE_IRQPRIO_PROGRAM:
		update_esr = true;
		/* fall through */
	case BOOKE_IRQPRIO_ITLB_MISS:
	case BOOKE_IRQPRIO_SYSCALL:
	case BOOKE_IRQPRIO_FP_UNAVAIL:
	case BOOKE_IRQPRIO_SPE_UNAVAIL:
	case BOOKE_IRQPRIO_SPE_FP_DATA:
	case BOOKE_IRQPRIO_SPE_FP_ROUND:
	case BOOKE_IRQPRIO_AP_UNAVAIL:
	case BOOKE_IRQPRIO_ALIGNMENT:
		allowed = 1;
		msr_mask = MSR_CE | MSR_ME | MSR_DE;
		int_class = INT_CLASS_NONCRIT;
		break;
	case BOOKE_IRQPRIO_CRITICAL:
	case BOOKE_IRQPRIO_DBELL_CRIT:
		allowed = vcpu->arch.shared->msr & MSR_CE;
		allowed = allowed && !crit;
		msr_mask = MSR_ME;
		int_class = INT_CLASS_CRIT;
		break;
	case BOOKE_IRQPRIO_MACHINE_CHECK:
		allowed = vcpu->arch.shared->msr & MSR_ME;
		allowed = allowed && !crit;
		int_class = INT_CLASS_MC;
		break;
	case BOOKE_IRQPRIO_DECREMENTER:
	case BOOKE_IRQPRIO_FIT:
		keep_irq = true;
		/* fall through */
	case BOOKE_IRQPRIO_EXTERNAL:
	case BOOKE_IRQPRIO_DBELL:
		allowed = vcpu->arch.shared->msr & MSR_EE;
		allowed = allowed && !crit;
		msr_mask = MSR_CE | MSR_ME | MSR_DE;
		int_class = INT_CLASS_NONCRIT;
		break;
	case BOOKE_IRQPRIO_DEBUG:
		allowed = vcpu->arch.shared->msr & MSR_DE;
		allowed = allowed && !crit;
		msr_mask = MSR_ME;
		int_class = INT_CLASS_CRIT;
		break;
	}

	if (allowed) {
		switch (int_class) {
		case INT_CLASS_NONCRIT:
			set_guest_srr(vcpu, vcpu->arch.pc,
				      vcpu->arch.shared->msr);
			break;
		case INT_CLASS_CRIT:
			set_guest_csrr(vcpu, vcpu->arch.pc,
				       vcpu->arch.shared->msr);
			break;
		case INT_CLASS_DBG:
			set_guest_dsrr(vcpu, vcpu->arch.pc,
				       vcpu->arch.shared->msr);
			break;
		case INT_CLASS_MC:
			set_guest_mcsrr(vcpu, vcpu->arch.pc,
					vcpu->arch.shared->msr);
			break;
		}

		vcpu->arch.pc = vcpu->arch.ivpr | vcpu->arch.ivor[priority];
		if (update_esr == true)
			set_guest_esr(vcpu, vcpu->arch.queued_esr);
		if (update_dear == true)
			set_guest_dear(vcpu, vcpu->arch.queued_dear);
		kvmppc_set_msr(vcpu, vcpu->arch.shared->msr & msr_mask);

		if (!keep_irq)
			clear_bit(priority, &vcpu->arch.pending_exceptions);
	}

#ifdef CONFIG_KVM_BOOKE_HV
	/*
	 * If an interrupt is pending but masked, raise a guest doorbell
	 * so that we are notified when the guest enables the relevant
	 * MSR bit.
	 */
	if (vcpu->arch.pending_exceptions & BOOKE_IRQMASK_EE)
		kvmppc_set_pending_interrupt(vcpu, INT_CLASS_NONCRIT);
	if (vcpu->arch.pending_exceptions & BOOKE_IRQMASK_CE)
		kvmppc_set_pending_interrupt(vcpu, INT_CLASS_CRIT);
	if (vcpu->arch.pending_exceptions & BOOKE_IRQPRIO_MACHINE_CHECK)
		kvmppc_set_pending_interrupt(vcpu, INT_CLASS_MC);
#endif

	return allowed;
}

static void update_timer_ints(struct kvm_vcpu *vcpu)
{
	if ((vcpu->arch.tcr & TCR_DIE) && (vcpu->arch.tsr & TSR_DIS))
		kvmppc_core_queue_dec(vcpu);
	else
		kvmppc_core_dequeue_dec(vcpu);
}

static void kvmppc_core_check_exceptions(struct kvm_vcpu *vcpu)
{
	unsigned long *pending = &vcpu->arch.pending_exceptions;
	unsigned int priority;

	if (vcpu->requests) {
		if (kvm_check_request(KVM_REQ_PENDING_TIMER, vcpu)) {
			smp_mb();
			update_timer_ints(vcpu);
		}
	}

	priority = __ffs(*pending);
	while (priority < BOOKE_IRQPRIO_MAX) {
		if (kvmppc_booke_irqprio_deliver(vcpu, priority))
			break;

		priority = find_next_bit(pending,
		                         BITS_PER_BYTE * sizeof(*pending),
		                         priority + 1);
	}

	/* Tell the guest about our interrupt status */
	vcpu->arch.shared->int_pending = !!*pending;
}

/* Check pending exceptions and deliver one, if possible. */
int kvmppc_core_prepare_to_enter(struct kvm_vcpu *vcpu)
{
	int r = 0;
	WARN_ON_ONCE(!irqs_disabled());

	kvmppc_core_check_exceptions(vcpu);

	if (vcpu->arch.shared->msr & MSR_WE) {
		local_irq_enable();
		kvm_vcpu_block(vcpu);
		clear_bit(KVM_REQ_UNHALT, &vcpu->requests);
		local_irq_disable();

		kvmppc_set_exit_type(vcpu, EMULATED_MTMSRWE_EXITS);
		r = 1;
	};

	return r;
}

/*
 * Common checks before entering the guest world.  Call with interrupts
 * disabled.
 *
 * returns !0 if a signal is pending and check_signal is true
 */
static int kvmppc_prepare_to_enter(struct kvm_vcpu *vcpu)
{
	int r = 0;

	WARN_ON_ONCE(!irqs_disabled());
	while (true) {
		if (need_resched()) {
			local_irq_enable();
			cond_resched();
			local_irq_disable();
			continue;
		}

		if (signal_pending(current)) {
			r = 1;
			break;
		}

		if (kvmppc_core_prepare_to_enter(vcpu)) {
			/* interrupts got enabled in between, so we
			   are back at square 1 */
			continue;
		}

		break;
	}

	return r;
}

int kvmppc_vcpu_run(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	int ret;
#ifdef CONFIG_PPC_FPU
	unsigned int fpscr;
	int fpexc_mode;
	u64 fpr[32];
#endif

	if (!vcpu->arch.sane) {
		kvm_run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		return -EINVAL;
	}

	local_irq_disable();
	if (kvmppc_prepare_to_enter(vcpu)) {
		kvm_run->exit_reason = KVM_EXIT_INTR;
		ret = -EINTR;
		goto out;
	}

	kvm_guest_enter();

#ifdef CONFIG_PPC_FPU
	/* Save userspace FPU state in stack */
	enable_kernel_fp();
	memcpy(fpr, current->thread.fpr, sizeof(current->thread.fpr));
	fpscr = current->thread.fpscr.val;
	fpexc_mode = current->thread.fpexc_mode;

	/* Restore guest FPU state to thread */
	memcpy(current->thread.fpr, vcpu->arch.fpr, sizeof(vcpu->arch.fpr));
	current->thread.fpscr.val = vcpu->arch.fpscr;

	/*
	 * Since we can't trap on MSR_FP in GS-mode, we consider the guest
	 * as always using the FPU.  Kernel usage of FP (via
	 * enable_kernel_fp()) in this thread must not occur while
	 * vcpu->fpu_active is set.
	 */
	vcpu->fpu_active = 1;

	kvmppc_load_guest_fp(vcpu);
#endif

	ret = __kvmppc_vcpu_run(kvm_run, vcpu);

#ifdef CONFIG_PPC_FPU
	kvmppc_save_guest_fp(vcpu);

	vcpu->fpu_active = 0;

	/* Save guest FPU state from thread */
	memcpy(vcpu->arch.fpr, current->thread.fpr, sizeof(vcpu->arch.fpr));
	vcpu->arch.fpscr = current->thread.fpscr.val;

	/* Restore userspace FPU state from stack */
	memcpy(current->thread.fpr, fpr, sizeof(current->thread.fpr));
	current->thread.fpscr.val = fpscr;
	current->thread.fpexc_mode = fpexc_mode;
#endif

	kvm_guest_exit();

out:
	local_irq_enable();
	return ret;
}

static int emulation_exit(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	enum emulation_result er;

	er = kvmppc_emulate_instruction(run, vcpu);
	switch (er) {
	case EMULATE_DONE:
		/* don't overwrite subtypes, just account kvm_stats */
		kvmppc_account_exit_stat(vcpu, EMULATED_INST_EXITS);
		/* Future optimization: only reload non-volatiles if
		 * they were actually modified by emulation. */
		return RESUME_GUEST_NV;

	case EMULATE_DO_DCR:
		run->exit_reason = KVM_EXIT_DCR;
		return RESUME_HOST;

	case EMULATE_FAIL:
		printk(KERN_CRIT "%s: emulation at %lx failed (%08x)\n",
		       __func__, vcpu->arch.pc, vcpu->arch.last_inst);
		/* For debugging, encode the failing instruction and
		 * report it to userspace. */
		run->hw.hardware_exit_reason = ~0ULL << 32;
		run->hw.hardware_exit_reason |= vcpu->arch.last_inst;
		kvmppc_core_queue_program(vcpu, ESR_PIL);
		return RESUME_HOST;

	default:
		BUG();
	}
}

static void kvmppc_fill_pt_regs(struct pt_regs *regs)
{
	ulong r1, ip, msr, lr;

	asm("mr %0, 1" : "=r"(r1));
	asm("mflr %0" : "=r"(lr));
	asm("mfmsr %0" : "=r"(msr));
	asm("bl 1f; 1: mflr %0" : "=r"(ip));

	memset(regs, 0, sizeof(*regs));
	regs->gpr[1] = r1;
	regs->nip = ip;
	regs->msr = msr;
	regs->link = lr;
}

static void kvmppc_restart_interrupt(struct kvm_vcpu *vcpu,
				     unsigned int exit_nr)
{
	struct pt_regs regs;

	switch (exit_nr) {
	case BOOKE_INTERRUPT_EXTERNAL:
		kvmppc_fill_pt_regs(&regs);
		do_IRQ(&regs);
		break;
	case BOOKE_INTERRUPT_DECREMENTER:
		kvmppc_fill_pt_regs(&regs);
		timer_interrupt(&regs);
		break;
#if defined(CONFIG_PPC_FSL_BOOK3E) || defined(CONFIG_PPC_BOOK3E_64)
	case BOOKE_INTERRUPT_DOORBELL:
		kvmppc_fill_pt_regs(&regs);
		doorbell_exception(&regs);
		break;
#endif
	case BOOKE_INTERRUPT_MACHINE_CHECK:
		/* FIXME */
		break;
	case BOOKE_INTERRUPT_PERFORMANCE_MONITOR:
		kvmppc_fill_pt_regs(&regs);
		performance_monitor_exception(&regs);
		break;
	}
}

/**
 * kvmppc_handle_exit
 *
 * Return value is in the form (errcode<<2 | RESUME_FLAG_HOST | RESUME_FLAG_NV)
 */
int kvmppc_handle_exit(struct kvm_run *run, struct kvm_vcpu *vcpu,
                       unsigned int exit_nr)
{
	int r = RESUME_HOST;

	/* update before a new last_exit_type is rewritten */
	kvmppc_update_timing_stats(vcpu);

	/* restart interrupts if they were meant for the host */
	kvmppc_restart_interrupt(vcpu, exit_nr);

	local_irq_enable();

	run->exit_reason = KVM_EXIT_UNKNOWN;
	run->ready_for_interrupt_injection = 1;

	switch (exit_nr) {
	case BOOKE_INTERRUPT_MACHINE_CHECK:
		printk("MACHINE CHECK: %lx\n", mfspr(SPRN_MCSR));
		kvmppc_dump_vcpu(vcpu);
		/* For debugging, send invalid exit reason to user space */
		run->hw.hardware_exit_reason = ~1ULL << 32;
		run->hw.hardware_exit_reason |= mfspr(SPRN_MCSR);
		r = RESUME_HOST;
		break;

	case BOOKE_INTERRUPT_EXTERNAL:
		kvmppc_account_exit(vcpu, EXT_INTR_EXITS);
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_DECREMENTER:
		kvmppc_account_exit(vcpu, DEC_EXITS);
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_DOORBELL:
		kvmppc_account_exit(vcpu, DBELL_EXITS);
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_GUEST_DBELL_CRIT:
		kvmppc_account_exit(vcpu, GDBELL_EXITS);

		/*
		 * We are here because there is a pending guest interrupt
		 * which could not be delivered as MSR_CE or MSR_ME was not
		 * set.  Once we break from here we will retry delivery.
		 */
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_GUEST_DBELL:
		kvmppc_account_exit(vcpu, GDBELL_EXITS);

		/*
		 * We are here because there is a pending guest interrupt
		 * which could not be delivered as MSR_EE was not set.  Once
		 * we break from here we will retry delivery.
		 */
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_PERFORMANCE_MONITOR:
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_HV_PRIV:
		r = emulation_exit(run, vcpu);
		break;

	case BOOKE_INTERRUPT_PROGRAM:
		if (vcpu->arch.shared->msr & (MSR_PR | MSR_GS)) {
			/*
			 * Program traps generated by user-level software must
			 * be handled by the guest kernel.
			 *
			 * In GS mode, hypervisor privileged instructions trap
			 * on BOOKE_INTERRUPT_HV_PRIV, not here, so these are
			 * actual program interrupts, handled by the guest.
			 */
			kvmppc_core_queue_program(vcpu, vcpu->arch.fault_esr);
			r = RESUME_GUEST;
			kvmppc_account_exit(vcpu, USR_PR_INST);
			break;
		}

		r = emulation_exit(run, vcpu);
		break;

	case BOOKE_INTERRUPT_FP_UNAVAIL:
		kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_FP_UNAVAIL);
		kvmppc_account_exit(vcpu, FP_UNAVAIL);
		r = RESUME_GUEST;
		break;

#ifdef CONFIG_SPE
	case BOOKE_INTERRUPT_SPE_UNAVAIL: {
		if (vcpu->arch.shared->msr & MSR_SPE)
			kvmppc_vcpu_enable_spe(vcpu);
		else
			kvmppc_booke_queue_irqprio(vcpu,
						   BOOKE_IRQPRIO_SPE_UNAVAIL);
		r = RESUME_GUEST;
		break;
	}

	case BOOKE_INTERRUPT_SPE_FP_DATA:
		kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_SPE_FP_DATA);
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_SPE_FP_ROUND:
		kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_SPE_FP_ROUND);
		r = RESUME_GUEST;
		break;
#else
	case BOOKE_INTERRUPT_SPE_UNAVAIL:
		/*
		 * Guest wants SPE, but host kernel doesn't support it.  Send
		 * an "unimplemented operation" program check to the guest.
		 */
		kvmppc_core_queue_program(vcpu, ESR_PUO | ESR_SPV);
		r = RESUME_GUEST;
		break;

	/*
	 * These really should never happen without CONFIG_SPE,
	 * as we should never enable the real MSR[SPE] in the guest.
	 */
	case BOOKE_INTERRUPT_SPE_FP_DATA:
	case BOOKE_INTERRUPT_SPE_FP_ROUND:
		printk(KERN_CRIT "%s: unexpected SPE interrupt %u at %08lx\n",
		       __func__, exit_nr, vcpu->arch.pc);
		run->hw.hardware_exit_reason = exit_nr;
		r = RESUME_HOST;
		break;
#endif

	case BOOKE_INTERRUPT_DATA_STORAGE:
		kvmppc_core_queue_data_storage(vcpu, vcpu->arch.fault_dear,
		                               vcpu->arch.fault_esr);
		kvmppc_account_exit(vcpu, DSI_EXITS);
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_INST_STORAGE:
		kvmppc_core_queue_inst_storage(vcpu, vcpu->arch.fault_esr);
		kvmppc_account_exit(vcpu, ISI_EXITS);
		r = RESUME_GUEST;
		break;

#ifdef CONFIG_KVM_BOOKE_HV
	case BOOKE_INTERRUPT_HV_SYSCALL:
		if (!(vcpu->arch.shared->msr & MSR_PR)) {
			kvmppc_set_gpr(vcpu, 3, kvmppc_kvm_pv(vcpu));
		} else {
			/*
			 * hcall from guest userspace -- send privileged
			 * instruction program check.
			 */
			kvmppc_core_queue_program(vcpu, ESR_PPR);
		}

		r = RESUME_GUEST;
		break;
#else
	case BOOKE_INTERRUPT_SYSCALL:
		if (!(vcpu->arch.shared->msr & MSR_PR) &&
		    (((u32)kvmppc_get_gpr(vcpu, 0)) == KVM_SC_MAGIC_R0)) {
			/* KVM PV hypercalls */
			kvmppc_set_gpr(vcpu, 3, kvmppc_kvm_pv(vcpu));
			r = RESUME_GUEST;
		} else {
			/* Guest syscalls */
			kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_SYSCALL);
		}
		kvmppc_account_exit(vcpu, SYSCALL_EXITS);
		r = RESUME_GUEST;
		break;
#endif

	case BOOKE_INTERRUPT_DTLB_MISS: {
		unsigned long eaddr = vcpu->arch.fault_dear;
		int gtlb_index;
		gpa_t gpaddr;
		gfn_t gfn;

#ifdef CONFIG_KVM_E500V2
		if (!(vcpu->arch.shared->msr & MSR_PR) &&
		    (eaddr & PAGE_MASK) == vcpu->arch.magic_page_ea) {
			kvmppc_map_magic(vcpu);
			kvmppc_account_exit(vcpu, DTLB_VIRT_MISS_EXITS);
			r = RESUME_GUEST;

			break;
		}
#endif

		/* Check the guest TLB. */
		gtlb_index = kvmppc_mmu_dtlb_index(vcpu, eaddr);
		if (gtlb_index < 0) {
			/* The guest didn't have a mapping for it. */
			kvmppc_core_queue_dtlb_miss(vcpu,
			                            vcpu->arch.fault_dear,
			                            vcpu->arch.fault_esr);
			kvmppc_mmu_dtlb_miss(vcpu);
			kvmppc_account_exit(vcpu, DTLB_REAL_MISS_EXITS);
			r = RESUME_GUEST;
			break;
		}

		gpaddr = kvmppc_mmu_xlate(vcpu, gtlb_index, eaddr);
		gfn = gpaddr >> PAGE_SHIFT;

		if (kvm_is_visible_gfn(vcpu->kvm, gfn)) {
			/* The guest TLB had a mapping, but the shadow TLB
			 * didn't, and it is RAM. This could be because:
			 * a) the entry is mapping the host kernel, or
			 * b) the guest used a large mapping which we're faking
			 * Either way, we need to satisfy the fault without
			 * invoking the guest. */
			kvmppc_mmu_map(vcpu, eaddr, gpaddr, gtlb_index);
			kvmppc_account_exit(vcpu, DTLB_VIRT_MISS_EXITS);
			r = RESUME_GUEST;
		} else {
			/* Guest has mapped and accessed a page which is not
			 * actually RAM. */
			vcpu->arch.paddr_accessed = gpaddr;
			vcpu->arch.vaddr_accessed = eaddr;
			r = kvmppc_emulate_mmio(run, vcpu);
			kvmppc_account_exit(vcpu, MMIO_EXITS);
		}

		break;
	}

	case BOOKE_INTERRUPT_ITLB_MISS: {
		unsigned long eaddr = vcpu->arch.pc;
		gpa_t gpaddr;
		gfn_t gfn;
		int gtlb_index;

		r = RESUME_GUEST;

		/* Check the guest TLB. */
		gtlb_index = kvmppc_mmu_itlb_index(vcpu, eaddr);
		if (gtlb_index < 0) {
			/* The guest didn't have a mapping for it. */
			kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_ITLB_MISS);
			kvmppc_mmu_itlb_miss(vcpu);
			kvmppc_account_exit(vcpu, ITLB_REAL_MISS_EXITS);
			break;
		}

		kvmppc_account_exit(vcpu, ITLB_VIRT_MISS_EXITS);

		gpaddr = kvmppc_mmu_xlate(vcpu, gtlb_index, eaddr);
		gfn = gpaddr >> PAGE_SHIFT;

		if (kvm_is_visible_gfn(vcpu->kvm, gfn)) {
			/* The guest TLB had a mapping, but the shadow TLB
			 * didn't. This could be because:
			 * a) the entry is mapping the host kernel, or
			 * b) the guest used a large mapping which we're faking
			 * Either way, we need to satisfy the fault without
			 * invoking the guest. */
			kvmppc_mmu_map(vcpu, eaddr, gpaddr, gtlb_index);
		} else {
			/* Guest mapped and leaped at non-RAM! */
			kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_MACHINE_CHECK);
		}

		break;
	}

	case BOOKE_INTERRUPT_DEBUG: {
		u32 dbsr;

		vcpu->arch.pc = mfspr(SPRN_CSRR0);

		/* clear IAC events in DBSR register */
		dbsr = mfspr(SPRN_DBSR);
		dbsr &= DBSR_IAC1 | DBSR_IAC2 | DBSR_IAC3 | DBSR_IAC4;
		mtspr(SPRN_DBSR, dbsr);

		run->exit_reason = KVM_EXIT_DEBUG;
		kvmppc_account_exit(vcpu, DEBUG_EXITS);
		r = RESUME_HOST;
		break;
	}

	default:
		printk(KERN_EMERG "exit_nr %d\n", exit_nr);
		BUG();
	}

	/*
	 * To avoid clobbering exit_reason, only check for signals if we
	 * aren't already exiting to userspace for some other reason.
	 */
	if (!(r & RESUME_HOST)) {
		local_irq_disable();
		if (kvmppc_prepare_to_enter(vcpu)) {
			run->exit_reason = KVM_EXIT_INTR;
			r = (-EINTR << 2) | RESUME_HOST | (r & RESUME_FLAG_NV);
			kvmppc_account_exit(vcpu, SIGNAL_EXITS);
		}
	}

	return r;
}

/* Initial guest state: 16MB mapping 0 -> 0, PC = 0, MSR = 0, R1 = 16MB */
int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu)
{
	int i;
	int r;

	vcpu->arch.pc = 0;
	vcpu->arch.shared->pir = vcpu->vcpu_id;
	kvmppc_set_gpr(vcpu, 1, (16<<20) - 8); /* -8 for the callee-save LR slot */
	kvmppc_set_msr(vcpu, 0);

#ifndef CONFIG_KVM_BOOKE_HV
	vcpu->arch.shadow_msr = MSR_USER | MSR_DE | MSR_IS | MSR_DS;
	vcpu->arch.shadow_pid = 1;
	vcpu->arch.shared->msr = 0;
#endif

	/* Eye-catching numbers so we know if the guest takes an interrupt
	 * before it's programmed its own IVPR/IVORs. */
	vcpu->arch.ivpr = 0x55550000;
	for (i = 0; i < BOOKE_IRQPRIO_MAX; i++)
		vcpu->arch.ivor[i] = 0x7700 | i * 4;

	kvmppc_init_timing_stats(vcpu);

	r = kvmppc_core_vcpu_setup(vcpu);
	kvmppc_sanity_check(vcpu);
	return r;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	regs->pc = vcpu->arch.pc;
	regs->cr = kvmppc_get_cr(vcpu);
	regs->ctr = vcpu->arch.ctr;
	regs->lr = vcpu->arch.lr;
	regs->xer = kvmppc_get_xer(vcpu);
	regs->msr = vcpu->arch.shared->msr;
	regs->srr0 = vcpu->arch.shared->srr0;
	regs->srr1 = vcpu->arch.shared->srr1;
	regs->pid = vcpu->arch.pid;
	regs->sprg0 = vcpu->arch.shared->sprg0;
	regs->sprg1 = vcpu->arch.shared->sprg1;
	regs->sprg2 = vcpu->arch.shared->sprg2;
	regs->sprg3 = vcpu->arch.shared->sprg3;
	regs->sprg4 = vcpu->arch.shared->sprg4;
	regs->sprg5 = vcpu->arch.shared->sprg5;
	regs->sprg6 = vcpu->arch.shared->sprg6;
	regs->sprg7 = vcpu->arch.shared->sprg7;

	for (i = 0; i < ARRAY_SIZE(regs->gpr); i++)
		regs->gpr[i] = kvmppc_get_gpr(vcpu, i);

	return 0;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	vcpu->arch.pc = regs->pc;
	kvmppc_set_cr(vcpu, regs->cr);
	vcpu->arch.ctr = regs->ctr;
	vcpu->arch.lr = regs->lr;
	kvmppc_set_xer(vcpu, regs->xer);
	kvmppc_set_msr(vcpu, regs->msr);
	vcpu->arch.shared->srr0 = regs->srr0;
	vcpu->arch.shared->srr1 = regs->srr1;
	kvmppc_set_pid(vcpu, regs->pid);
	vcpu->arch.shared->sprg0 = regs->sprg0;
	vcpu->arch.shared->sprg1 = regs->sprg1;
	vcpu->arch.shared->sprg2 = regs->sprg2;
	vcpu->arch.shared->sprg3 = regs->sprg3;
	vcpu->arch.shared->sprg4 = regs->sprg4;
	vcpu->arch.shared->sprg5 = regs->sprg5;
	vcpu->arch.shared->sprg6 = regs->sprg6;
	vcpu->arch.shared->sprg7 = regs->sprg7;

	for (i = 0; i < ARRAY_SIZE(regs->gpr); i++)
		kvmppc_set_gpr(vcpu, i, regs->gpr[i]);

	return 0;
}

static void get_sregs_base(struct kvm_vcpu *vcpu,
                           struct kvm_sregs *sregs)
{
	u64 tb = get_tb();

	sregs->u.e.features |= KVM_SREGS_E_BASE;

	sregs->u.e.csrr0 = vcpu->arch.csrr0;
	sregs->u.e.csrr1 = vcpu->arch.csrr1;
	sregs->u.e.mcsr = vcpu->arch.mcsr;
	sregs->u.e.esr = get_guest_esr(vcpu);
	sregs->u.e.dear = get_guest_dear(vcpu);
	sregs->u.e.tsr = vcpu->arch.tsr;
	sregs->u.e.tcr = vcpu->arch.tcr;
	sregs->u.e.dec = kvmppc_get_dec(vcpu, tb);
	sregs->u.e.tb = tb;
	sregs->u.e.vrsave = vcpu->arch.vrsave;
}

static int set_sregs_base(struct kvm_vcpu *vcpu,
                          struct kvm_sregs *sregs)
{
	if (!(sregs->u.e.features & KVM_SREGS_E_BASE))
		return 0;

	vcpu->arch.csrr0 = sregs->u.e.csrr0;
	vcpu->arch.csrr1 = sregs->u.e.csrr1;
	vcpu->arch.mcsr = sregs->u.e.mcsr;
	set_guest_esr(vcpu, sregs->u.e.esr);
	set_guest_dear(vcpu, sregs->u.e.dear);
	vcpu->arch.vrsave = sregs->u.e.vrsave;
	kvmppc_set_tcr(vcpu, sregs->u.e.tcr);

	if (sregs->u.e.update_special & KVM_SREGS_E_UPDATE_DEC) {
		vcpu->arch.dec = sregs->u.e.dec;
		kvmppc_emulate_dec(vcpu);
	}

	if (sregs->u.e.update_special & KVM_SREGS_E_UPDATE_TSR) {
		vcpu->arch.tsr = sregs->u.e.tsr;
		update_timer_ints(vcpu);
	}

	return 0;
}

static void get_sregs_arch206(struct kvm_vcpu *vcpu,
                              struct kvm_sregs *sregs)
{
	sregs->u.e.features |= KVM_SREGS_E_ARCH206;

	sregs->u.e.pir = vcpu->vcpu_id;
	sregs->u.e.mcsrr0 = vcpu->arch.mcsrr0;
	sregs->u.e.mcsrr1 = vcpu->arch.mcsrr1;
	sregs->u.e.decar = vcpu->arch.decar;
	sregs->u.e.ivpr = vcpu->arch.ivpr;
}

static int set_sregs_arch206(struct kvm_vcpu *vcpu,
                             struct kvm_sregs *sregs)
{
	if (!(sregs->u.e.features & KVM_SREGS_E_ARCH206))
		return 0;

	if (sregs->u.e.pir != vcpu->vcpu_id)
		return -EINVAL;

	vcpu->arch.mcsrr0 = sregs->u.e.mcsrr0;
	vcpu->arch.mcsrr1 = sregs->u.e.mcsrr1;
	vcpu->arch.decar = sregs->u.e.decar;
	vcpu->arch.ivpr = sregs->u.e.ivpr;

	return 0;
}

void kvmppc_get_sregs_ivor(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	sregs->u.e.features |= KVM_SREGS_E_IVOR;

	sregs->u.e.ivor_low[0] = vcpu->arch.ivor[BOOKE_IRQPRIO_CRITICAL];
	sregs->u.e.ivor_low[1] = vcpu->arch.ivor[BOOKE_IRQPRIO_MACHINE_CHECK];
	sregs->u.e.ivor_low[2] = vcpu->arch.ivor[BOOKE_IRQPRIO_DATA_STORAGE];
	sregs->u.e.ivor_low[3] = vcpu->arch.ivor[BOOKE_IRQPRIO_INST_STORAGE];
	sregs->u.e.ivor_low[4] = vcpu->arch.ivor[BOOKE_IRQPRIO_EXTERNAL];
	sregs->u.e.ivor_low[5] = vcpu->arch.ivor[BOOKE_IRQPRIO_ALIGNMENT];
	sregs->u.e.ivor_low[6] = vcpu->arch.ivor[BOOKE_IRQPRIO_PROGRAM];
	sregs->u.e.ivor_low[7] = vcpu->arch.ivor[BOOKE_IRQPRIO_FP_UNAVAIL];
	sregs->u.e.ivor_low[8] = vcpu->arch.ivor[BOOKE_IRQPRIO_SYSCALL];
	sregs->u.e.ivor_low[9] = vcpu->arch.ivor[BOOKE_IRQPRIO_AP_UNAVAIL];
	sregs->u.e.ivor_low[10] = vcpu->arch.ivor[BOOKE_IRQPRIO_DECREMENTER];
	sregs->u.e.ivor_low[11] = vcpu->arch.ivor[BOOKE_IRQPRIO_FIT];
	sregs->u.e.ivor_low[12] = vcpu->arch.ivor[BOOKE_IRQPRIO_WATCHDOG];
	sregs->u.e.ivor_low[13] = vcpu->arch.ivor[BOOKE_IRQPRIO_DTLB_MISS];
	sregs->u.e.ivor_low[14] = vcpu->arch.ivor[BOOKE_IRQPRIO_ITLB_MISS];
	sregs->u.e.ivor_low[15] = vcpu->arch.ivor[BOOKE_IRQPRIO_DEBUG];
}

int kvmppc_set_sregs_ivor(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	if (!(sregs->u.e.features & KVM_SREGS_E_IVOR))
		return 0;

	vcpu->arch.ivor[BOOKE_IRQPRIO_CRITICAL] = sregs->u.e.ivor_low[0];
	vcpu->arch.ivor[BOOKE_IRQPRIO_MACHINE_CHECK] = sregs->u.e.ivor_low[1];
	vcpu->arch.ivor[BOOKE_IRQPRIO_DATA_STORAGE] = sregs->u.e.ivor_low[2];
	vcpu->arch.ivor[BOOKE_IRQPRIO_INST_STORAGE] = sregs->u.e.ivor_low[3];
	vcpu->arch.ivor[BOOKE_IRQPRIO_EXTERNAL] = sregs->u.e.ivor_low[4];
	vcpu->arch.ivor[BOOKE_IRQPRIO_ALIGNMENT] = sregs->u.e.ivor_low[5];
	vcpu->arch.ivor[BOOKE_IRQPRIO_PROGRAM] = sregs->u.e.ivor_low[6];
	vcpu->arch.ivor[BOOKE_IRQPRIO_FP_UNAVAIL] = sregs->u.e.ivor_low[7];
	vcpu->arch.ivor[BOOKE_IRQPRIO_SYSCALL] = sregs->u.e.ivor_low[8];
	vcpu->arch.ivor[BOOKE_IRQPRIO_AP_UNAVAIL] = sregs->u.e.ivor_low[9];
	vcpu->arch.ivor[BOOKE_IRQPRIO_DECREMENTER] = sregs->u.e.ivor_low[10];
	vcpu->arch.ivor[BOOKE_IRQPRIO_FIT] = sregs->u.e.ivor_low[11];
	vcpu->arch.ivor[BOOKE_IRQPRIO_WATCHDOG] = sregs->u.e.ivor_low[12];
	vcpu->arch.ivor[BOOKE_IRQPRIO_DTLB_MISS] = sregs->u.e.ivor_low[13];
	vcpu->arch.ivor[BOOKE_IRQPRIO_ITLB_MISS] = sregs->u.e.ivor_low[14];
	vcpu->arch.ivor[BOOKE_IRQPRIO_DEBUG] = sregs->u.e.ivor_low[15];

	return 0;
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
                                  struct kvm_sregs *sregs)
{
	sregs->pvr = vcpu->arch.pvr;

	get_sregs_base(vcpu, sregs);
	get_sregs_arch206(vcpu, sregs);
	kvmppc_core_get_sregs(vcpu, sregs);
	return 0;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
                                  struct kvm_sregs *sregs)
{
	int ret;

	if (vcpu->arch.pvr != sregs->pvr)
		return -EINVAL;

	ret = set_sregs_base(vcpu, sregs);
	if (ret < 0)
		return ret;

	ret = set_sregs_arch206(vcpu, sregs);
	if (ret < 0)
		return ret;

	return kvmppc_core_set_sregs(vcpu, sregs);
}

int kvm_vcpu_ioctl_get_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg)
{
	return -EINVAL;
}

int kvm_vcpu_ioctl_set_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -ENOTSUPP;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -ENOTSUPP;
}

int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
                                  struct kvm_translation *tr)
{
	int r;

	r = kvmppc_core_vcpu_translate(vcpu, tr);
	return r;
}

int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm, struct kvm_dirty_log *log)
{
	return -ENOTSUPP;
}

int kvmppc_core_prepare_memory_region(struct kvm *kvm,
				      struct kvm_userspace_memory_region *mem)
{
	return 0;
}

void kvmppc_core_commit_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem)
{
}

void kvmppc_set_tcr(struct kvm_vcpu *vcpu, u32 new_tcr)
{
	vcpu->arch.tcr = new_tcr;
	update_timer_ints(vcpu);
}

void kvmppc_set_tsr_bits(struct kvm_vcpu *vcpu, u32 tsr_bits)
{
	set_bits(tsr_bits, &vcpu->arch.tsr);
	smp_wmb();
	kvm_make_request(KVM_REQ_PENDING_TIMER, vcpu);
	kvm_vcpu_kick(vcpu);
}

void kvmppc_clr_tsr_bits(struct kvm_vcpu *vcpu, u32 tsr_bits)
{
	clear_bits(tsr_bits, &vcpu->arch.tsr);
	update_timer_ints(vcpu);
}

void kvmppc_decrementer_func(unsigned long data)
{
	struct kvm_vcpu *vcpu = (struct kvm_vcpu *)data;

	kvmppc_set_tsr_bits(vcpu, TSR_DIS);
}

void kvmppc_booke_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	current->thread.kvm_vcpu = vcpu;
}

void kvmppc_booke_vcpu_put(struct kvm_vcpu *vcpu)
{
	current->thread.kvm_vcpu = NULL;
}

int __init kvmppc_booke_init(void)
{
#ifndef CONFIG_KVM_BOOKE_HV
	unsigned long ivor[16];
	unsigned long max_ivor = 0;
	int i;

	/* We install our own exception handlers by hijacking IVPR. IVPR must
	 * be 16-bit aligned, so we need a 64KB allocation. */
	kvmppc_booke_handlers = __get_free_pages(GFP_KERNEL | __GFP_ZERO,
	                                         VCPU_SIZE_ORDER);
	if (!kvmppc_booke_handlers)
		return -ENOMEM;

	/* XXX make sure our handlers are smaller than Linux's */

	/* Copy our interrupt handlers to match host IVORs. That way we don't
	 * have to swap the IVORs on every guest/host transition. */
	ivor[0] = mfspr(SPRN_IVOR0);
	ivor[1] = mfspr(SPRN_IVOR1);
	ivor[2] = mfspr(SPRN_IVOR2);
	ivor[3] = mfspr(SPRN_IVOR3);
	ivor[4] = mfspr(SPRN_IVOR4);
	ivor[5] = mfspr(SPRN_IVOR5);
	ivor[6] = mfspr(SPRN_IVOR6);
	ivor[7] = mfspr(SPRN_IVOR7);
	ivor[8] = mfspr(SPRN_IVOR8);
	ivor[9] = mfspr(SPRN_IVOR9);
	ivor[10] = mfspr(SPRN_IVOR10);
	ivor[11] = mfspr(SPRN_IVOR11);
	ivor[12] = mfspr(SPRN_IVOR12);
	ivor[13] = mfspr(SPRN_IVOR13);
	ivor[14] = mfspr(SPRN_IVOR14);
	ivor[15] = mfspr(SPRN_IVOR15);

	for (i = 0; i < 16; i++) {
		if (ivor[i] > max_ivor)
			max_ivor = ivor[i];

		memcpy((void *)kvmppc_booke_handlers + ivor[i],
		       kvmppc_handlers_start + i * kvmppc_handler_len,
		       kvmppc_handler_len);
	}
	flush_icache_range(kvmppc_booke_handlers,
	                   kvmppc_booke_handlers + max_ivor + kvmppc_handler_len);
#endif /* !BOOKE_HV */
	return 0;
}

void __exit kvmppc_booke_exit(void)
{
	free_pages(kvmppc_booke_handlers, VCPU_SIZE_ORDER);
	kvm_exit();
}
