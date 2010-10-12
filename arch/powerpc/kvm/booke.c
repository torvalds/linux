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
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 *          Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
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
#include "timing.h"
#include <asm/cacheflush.h>

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
	{ NULL }
};

/* TODO: use vcpu_printf() */
void kvmppc_dump_vcpu(struct kvm_vcpu *vcpu)
{
	int i;

	printk("pc:   %08lx msr:  %08lx\n", vcpu->arch.pc, vcpu->arch.msr);
	printk("lr:   %08lx ctr:  %08lx\n", vcpu->arch.lr, vcpu->arch.ctr);
	printk("srr0: %08lx srr1: %08lx\n", vcpu->arch.srr0, vcpu->arch.srr1);

	printk("exceptions: %08lx\n", vcpu->arch.pending_exceptions);

	for (i = 0; i < 32; i += 4) {
		printk("gpr%02d: %08lx %08lx %08lx %08lx\n", i,
		       kvmppc_get_gpr(vcpu, i),
		       kvmppc_get_gpr(vcpu, i+1),
		       kvmppc_get_gpr(vcpu, i+2),
		       kvmppc_get_gpr(vcpu, i+3));
	}
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
	kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_EXTERNAL);
}

void kvmppc_core_dequeue_external(struct kvm_vcpu *vcpu,
                                  struct kvm_interrupt *irq)
{
	clear_bit(BOOKE_IRQPRIO_EXTERNAL, &vcpu->arch.pending_exceptions);
}

/* Deliver the interrupt of the corresponding priority, if possible. */
static int kvmppc_booke_irqprio_deliver(struct kvm_vcpu *vcpu,
                                        unsigned int priority)
{
	int allowed = 0;
	ulong uninitialized_var(msr_mask);
	bool update_esr = false, update_dear = false;

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
		msr_mask = MSR_CE|MSR_ME|MSR_DE;
		break;
	case BOOKE_IRQPRIO_CRITICAL:
	case BOOKE_IRQPRIO_WATCHDOG:
		allowed = vcpu->arch.msr & MSR_CE;
		msr_mask = MSR_ME;
		break;
	case BOOKE_IRQPRIO_MACHINE_CHECK:
		allowed = vcpu->arch.msr & MSR_ME;
		msr_mask = 0;
		break;
	case BOOKE_IRQPRIO_EXTERNAL:
	case BOOKE_IRQPRIO_DECREMENTER:
	case BOOKE_IRQPRIO_FIT:
		allowed = vcpu->arch.msr & MSR_EE;
		msr_mask = MSR_CE|MSR_ME|MSR_DE;
		break;
	case BOOKE_IRQPRIO_DEBUG:
		allowed = vcpu->arch.msr & MSR_DE;
		msr_mask = MSR_ME;
		break;
	}

	if (allowed) {
		vcpu->arch.srr0 = vcpu->arch.pc;
		vcpu->arch.srr1 = vcpu->arch.msr;
		vcpu->arch.pc = vcpu->arch.ivpr | vcpu->arch.ivor[priority];
		if (update_esr == true)
			vcpu->arch.esr = vcpu->arch.queued_esr;
		if (update_dear == true)
			vcpu->arch.dear = vcpu->arch.queued_dear;
		kvmppc_set_msr(vcpu, vcpu->arch.msr & msr_mask);

		clear_bit(priority, &vcpu->arch.pending_exceptions);
	}

	return allowed;
}

/* Check pending exceptions and deliver one, if possible. */
void kvmppc_core_deliver_interrupts(struct kvm_vcpu *vcpu)
{
	unsigned long *pending = &vcpu->arch.pending_exceptions;
	unsigned int priority;

	priority = __ffs(*pending);
	while (priority <= BOOKE_IRQPRIO_MAX) {
		if (kvmppc_booke_irqprio_deliver(vcpu, priority))
			break;

		priority = find_next_bit(pending,
		                         BITS_PER_BYTE * sizeof(*pending),
		                         priority + 1);
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
	enum emulation_result er;
	int r = RESUME_HOST;

	/* update before a new last_exit_type is rewritten */
	kvmppc_update_timing_stats(vcpu);

	local_irq_enable();

	run->exit_reason = KVM_EXIT_UNKNOWN;
	run->ready_for_interrupt_injection = 1;

	switch (exit_nr) {
	case BOOKE_INTERRUPT_MACHINE_CHECK:
		printk("MACHINE CHECK: %lx\n", mfspr(SPRN_MCSR));
		kvmppc_dump_vcpu(vcpu);
		r = RESUME_HOST;
		break;

	case BOOKE_INTERRUPT_EXTERNAL:
		kvmppc_account_exit(vcpu, EXT_INTR_EXITS);
		if (need_resched())
			cond_resched();
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_DECREMENTER:
		/* Since we switched IVPR back to the host's value, the host
		 * handled this interrupt the moment we enabled interrupts.
		 * Now we just offer it a chance to reschedule the guest. */
		kvmppc_account_exit(vcpu, DEC_EXITS);
		if (need_resched())
			cond_resched();
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_PROGRAM:
		if (vcpu->arch.msr & MSR_PR) {
			/* Program traps generated by user-level software must be handled
			 * by the guest kernel. */
			kvmppc_core_queue_program(vcpu, vcpu->arch.fault_esr);
			r = RESUME_GUEST;
			kvmppc_account_exit(vcpu, USR_PR_INST);
			break;
		}

		er = kvmppc_emulate_instruction(run, vcpu);
		switch (er) {
		case EMULATE_DONE:
			/* don't overwrite subtypes, just account kvm_stats */
			kvmppc_account_exit_stat(vcpu, EMULATED_INST_EXITS);
			/* Future optimization: only reload non-volatiles if
			 * they were actually modified by emulation. */
			r = RESUME_GUEST_NV;
			break;
		case EMULATE_DO_DCR:
			run->exit_reason = KVM_EXIT_DCR;
			r = RESUME_HOST;
			break;
		case EMULATE_FAIL:
			/* XXX Deliver Program interrupt to guest. */
			printk(KERN_CRIT "%s: emulation at %lx failed (%08x)\n",
			       __func__, vcpu->arch.pc, vcpu->arch.last_inst);
			/* For debugging, encode the failing instruction and
			 * report it to userspace. */
			run->hw.hardware_exit_reason = ~0ULL << 32;
			run->hw.hardware_exit_reason |= vcpu->arch.last_inst;
			r = RESUME_HOST;
			break;
		default:
			BUG();
		}
		break;

	case BOOKE_INTERRUPT_FP_UNAVAIL:
		kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_FP_UNAVAIL);
		kvmppc_account_exit(vcpu, FP_UNAVAIL);
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_SPE_UNAVAIL:
		kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_SPE_UNAVAIL);
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_SPE_FP_DATA:
		kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_SPE_FP_DATA);
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_SPE_FP_ROUND:
		kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_SPE_FP_ROUND);
		r = RESUME_GUEST;
		break;

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

	case BOOKE_INTERRUPT_SYSCALL:
		kvmppc_booke_queue_irqprio(vcpu, BOOKE_IRQPRIO_SYSCALL);
		kvmppc_account_exit(vcpu, SYSCALL_EXITS);
		r = RESUME_GUEST;
		break;

	case BOOKE_INTERRUPT_DTLB_MISS: {
		unsigned long eaddr = vcpu->arch.fault_dear;
		int gtlb_index;
		gpa_t gpaddr;
		gfn_t gfn;

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

	local_irq_disable();

	kvmppc_core_deliver_interrupts(vcpu);

	if (!(r & RESUME_HOST)) {
		/* To avoid clobbering exit_reason, only check for signals if
		 * we aren't already exiting to userspace for some other
		 * reason. */
		if (signal_pending(current)) {
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
	vcpu->arch.pc = 0;
	vcpu->arch.msr = 0;
	kvmppc_set_gpr(vcpu, 1, (16<<20) - 8); /* -8 for the callee-save LR slot */

	vcpu->arch.shadow_pid = 1;

	/* Eye-catching number so we know if the guest takes an interrupt
	 * before it's programmed its own IVPR. */
	vcpu->arch.ivpr = 0x55550000;

	kvmppc_init_timing_stats(vcpu);

	return kvmppc_core_vcpu_setup(vcpu);
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	regs->pc = vcpu->arch.pc;
	regs->cr = kvmppc_get_cr(vcpu);
	regs->ctr = vcpu->arch.ctr;
	regs->lr = vcpu->arch.lr;
	regs->xer = kvmppc_get_xer(vcpu);
	regs->msr = vcpu->arch.msr;
	regs->srr0 = vcpu->arch.srr0;
	regs->srr1 = vcpu->arch.srr1;
	regs->pid = vcpu->arch.pid;
	regs->sprg0 = vcpu->arch.sprg0;
	regs->sprg1 = vcpu->arch.sprg1;
	regs->sprg2 = vcpu->arch.sprg2;
	regs->sprg3 = vcpu->arch.sprg3;
	regs->sprg5 = vcpu->arch.sprg4;
	regs->sprg6 = vcpu->arch.sprg5;
	regs->sprg7 = vcpu->arch.sprg6;

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
	vcpu->arch.srr0 = regs->srr0;
	vcpu->arch.srr1 = regs->srr1;
	vcpu->arch.sprg0 = regs->sprg0;
	vcpu->arch.sprg1 = regs->sprg1;
	vcpu->arch.sprg2 = regs->sprg2;
	vcpu->arch.sprg3 = regs->sprg3;
	vcpu->arch.sprg5 = regs->sprg4;
	vcpu->arch.sprg6 = regs->sprg5;
	vcpu->arch.sprg7 = regs->sprg6;

	for (i = 0; i < ARRAY_SIZE(regs->gpr); i++)
		kvmppc_set_gpr(vcpu, i, regs->gpr[i]);

	return 0;
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
                                  struct kvm_sregs *sregs)
{
	return -ENOTSUPP;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
                                  struct kvm_sregs *sregs)
{
	return -ENOTSUPP;
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

int __init kvmppc_booke_init(void)
{
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

	return 0;
}

void __exit kvmppc_booke_exit(void)
{
	free_pages(kvmppc_booke_handlers, VCPU_SIZE_ORDER);
	kvm_exit();
}
