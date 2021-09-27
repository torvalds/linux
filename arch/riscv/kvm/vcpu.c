// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/kvm_host.h>
#include <asm/csr.h>
#include <asm/delay.h>
#include <asm/hwcap.h>

const struct _kvm_stats_desc kvm_vcpu_stats_desc[] = {
	KVM_GENERIC_VCPU_STATS(),
	STATS_DESC_COUNTER(VCPU, ecall_exit_stat),
	STATS_DESC_COUNTER(VCPU, wfi_exit_stat),
	STATS_DESC_COUNTER(VCPU, mmio_exit_user),
	STATS_DESC_COUNTER(VCPU, mmio_exit_kernel),
	STATS_DESC_COUNTER(VCPU, exits)
};

const struct kvm_stats_header kvm_vcpu_stats_header = {
	.name_size = KVM_STATS_NAME_SIZE,
	.num_desc = ARRAY_SIZE(kvm_vcpu_stats_desc),
	.id_offset = sizeof(struct kvm_stats_header),
	.desc_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE,
	.data_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE +
		       sizeof(kvm_vcpu_stats_desc),
};

#define KVM_RISCV_ISA_ALLOWED	(riscv_isa_extension_mask(a) | \
				 riscv_isa_extension_mask(c) | \
				 riscv_isa_extension_mask(d) | \
				 riscv_isa_extension_mask(f) | \
				 riscv_isa_extension_mask(i) | \
				 riscv_isa_extension_mask(m) | \
				 riscv_isa_extension_mask(s) | \
				 riscv_isa_extension_mask(u))

static void kvm_riscv_reset_vcpu(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;
	struct kvm_vcpu_csr *reset_csr = &vcpu->arch.guest_reset_csr;
	struct kvm_cpu_context *cntx = &vcpu->arch.guest_context;
	struct kvm_cpu_context *reset_cntx = &vcpu->arch.guest_reset_context;

	memcpy(csr, reset_csr, sizeof(*csr));

	memcpy(cntx, reset_cntx, sizeof(*cntx));

	WRITE_ONCE(vcpu->arch.irqs_pending, 0);
	WRITE_ONCE(vcpu->arch.irqs_pending_mask, 0);
}

int kvm_arch_vcpu_precreate(struct kvm *kvm, unsigned int id)
{
	return 0;
}

int kvm_arch_vcpu_create(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *cntx;

	/* Mark this VCPU never ran */
	vcpu->arch.ran_atleast_once = false;

	/* Setup ISA features available to VCPU */
	vcpu->arch.isa = riscv_isa_extension_base(NULL) & KVM_RISCV_ISA_ALLOWED;

	/* Setup reset state of shadow SSTATUS and HSTATUS CSRs */
	cntx = &vcpu->arch.guest_reset_context;
	cntx->sstatus = SR_SPP | SR_SPIE;
	cntx->hstatus = 0;
	cntx->hstatus |= HSTATUS_VTW;
	cntx->hstatus |= HSTATUS_SPVP;
	cntx->hstatus |= HSTATUS_SPV;

	/* Reset VCPU */
	kvm_riscv_reset_vcpu(vcpu);

	return 0;
}

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	/* Flush the pages pre-allocated for Stage2 page table mappings */
	kvm_riscv_stage2_flush_cache(vcpu);
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return kvm_riscv_vcpu_has_interrupts(vcpu, 1UL << IRQ_VS_TIMER);
}

void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu)
{
}

void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu)
{
}

int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	return (kvm_riscv_vcpu_has_interrupts(vcpu, -1UL) &&
		!vcpu->arch.power_off && !vcpu->arch.pause);
}

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_exiting_guest_mode(vcpu) == IN_GUEST_MODE;
}

bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.guest_context.sstatus & SR_SPP) ? true : false;
}

vm_fault_t kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

long kvm_arch_vcpu_async_ioctl(struct file *filp,
			       unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;

	if (ioctl == KVM_INTERRUPT) {
		struct kvm_interrupt irq;

		if (copy_from_user(&irq, argp, sizeof(irq)))
			return -EFAULT;

		if (irq.irq == KVM_INTERRUPT_SET)
			return kvm_riscv_vcpu_set_interrupt(vcpu, IRQ_VS_EXT);
		else
			return kvm_riscv_vcpu_unset_interrupt(vcpu, IRQ_VS_EXT);
	}

	return -ENOIOCTLCMD;
}

long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg)
{
	/* TODO: */
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
				  struct kvm_translation *tr)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	return -EINVAL;
}

void kvm_riscv_vcpu_flush_interrupts(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;
	unsigned long mask, val;

	if (READ_ONCE(vcpu->arch.irqs_pending_mask)) {
		mask = xchg_acquire(&vcpu->arch.irqs_pending_mask, 0);
		val = READ_ONCE(vcpu->arch.irqs_pending) & mask;

		csr->hvip &= ~mask;
		csr->hvip |= val;
	}
}

void kvm_riscv_vcpu_sync_interrupts(struct kvm_vcpu *vcpu)
{
	unsigned long hvip;
	struct kvm_vcpu_arch *v = &vcpu->arch;
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;

	/* Read current HVIP and VSIE CSRs */
	csr->vsie = csr_read(CSR_VSIE);

	/* Sync-up HVIP.VSSIP bit changes does by Guest */
	hvip = csr_read(CSR_HVIP);
	if ((csr->hvip ^ hvip) & (1UL << IRQ_VS_SOFT)) {
		if (hvip & (1UL << IRQ_VS_SOFT)) {
			if (!test_and_set_bit(IRQ_VS_SOFT,
					      &v->irqs_pending_mask))
				set_bit(IRQ_VS_SOFT, &v->irqs_pending);
		} else {
			if (!test_and_set_bit(IRQ_VS_SOFT,
					      &v->irqs_pending_mask))
				clear_bit(IRQ_VS_SOFT, &v->irqs_pending);
		}
	}
}

int kvm_riscv_vcpu_set_interrupt(struct kvm_vcpu *vcpu, unsigned int irq)
{
	if (irq != IRQ_VS_SOFT &&
	    irq != IRQ_VS_TIMER &&
	    irq != IRQ_VS_EXT)
		return -EINVAL;

	set_bit(irq, &vcpu->arch.irqs_pending);
	smp_mb__before_atomic();
	set_bit(irq, &vcpu->arch.irqs_pending_mask);

	kvm_vcpu_kick(vcpu);

	return 0;
}

int kvm_riscv_vcpu_unset_interrupt(struct kvm_vcpu *vcpu, unsigned int irq)
{
	if (irq != IRQ_VS_SOFT &&
	    irq != IRQ_VS_TIMER &&
	    irq != IRQ_VS_EXT)
		return -EINVAL;

	clear_bit(irq, &vcpu->arch.irqs_pending);
	smp_mb__before_atomic();
	set_bit(irq, &vcpu->arch.irqs_pending_mask);

	return 0;
}

bool kvm_riscv_vcpu_has_interrupts(struct kvm_vcpu *vcpu, unsigned long mask)
{
	unsigned long ie = ((vcpu->arch.guest_csr.vsie & VSIP_VALID_MASK)
			    << VSIP_TO_HVIP_SHIFT) & mask;

	return (READ_ONCE(vcpu->arch.irqs_pending) & ie) ? true : false;
}

void kvm_riscv_vcpu_power_off(struct kvm_vcpu *vcpu)
{
	vcpu->arch.power_off = true;
	kvm_make_request(KVM_REQ_SLEEP, vcpu);
	kvm_vcpu_kick(vcpu);
}

void kvm_riscv_vcpu_power_on(struct kvm_vcpu *vcpu)
{
	vcpu->arch.power_off = false;
	kvm_vcpu_wake_up(vcpu);
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	if (vcpu->arch.power_off)
		mp_state->mp_state = KVM_MP_STATE_STOPPED;
	else
		mp_state->mp_state = KVM_MP_STATE_RUNNABLE;

	return 0;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	int ret = 0;

	switch (mp_state->mp_state) {
	case KVM_MP_STATE_RUNNABLE:
		vcpu->arch.power_off = false;
		break;
	case KVM_MP_STATE_STOPPED:
		kvm_riscv_vcpu_power_off(vcpu);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg)
{
	/* TODO; To be implemented later. */
	return -EINVAL;
}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	/* TODO: */

	kvm_riscv_stage2_update_hgatp(vcpu);
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	/* TODO: */
}

static void kvm_riscv_check_vcpu_requests(struct kvm_vcpu *vcpu)
{
	struct rcuwait *wait = kvm_arch_vcpu_get_wait(vcpu);

	if (kvm_request_pending(vcpu)) {
		if (kvm_check_request(KVM_REQ_SLEEP, vcpu)) {
			rcuwait_wait_event(wait,
				(!vcpu->arch.power_off) && (!vcpu->arch.pause),
				TASK_INTERRUPTIBLE);

			if (vcpu->arch.power_off || vcpu->arch.pause) {
				/*
				 * Awaken to handle a signal, request to
				 * sleep again later.
				 */
				kvm_make_request(KVM_REQ_SLEEP, vcpu);
			}
		}

		if (kvm_check_request(KVM_REQ_VCPU_RESET, vcpu))
			kvm_riscv_reset_vcpu(vcpu);
	}
}

static void kvm_riscv_update_hvip(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;

	csr_write(CSR_HVIP, csr->hvip);
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	int ret;
	struct kvm_cpu_trap trap;
	struct kvm_run *run = vcpu->run;

	/* Mark this VCPU ran at least once */
	vcpu->arch.ran_atleast_once = true;

	vcpu->arch.srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);

	/* Process MMIO value returned from user-space */
	if (run->exit_reason == KVM_EXIT_MMIO) {
		ret = kvm_riscv_vcpu_mmio_return(vcpu, vcpu->run);
		if (ret) {
			srcu_read_unlock(&vcpu->kvm->srcu, vcpu->arch.srcu_idx);
			return ret;
		}
	}

	if (run->immediate_exit) {
		srcu_read_unlock(&vcpu->kvm->srcu, vcpu->arch.srcu_idx);
		return -EINTR;
	}

	vcpu_load(vcpu);

	kvm_sigset_activate(vcpu);

	ret = 1;
	run->exit_reason = KVM_EXIT_UNKNOWN;
	while (ret > 0) {
		/* Check conditions before entering the guest */
		cond_resched();

		kvm_riscv_check_vcpu_requests(vcpu);

		preempt_disable();

		local_irq_disable();

		/*
		 * Exit if we have a signal pending so that we can deliver
		 * the signal to user space.
		 */
		if (signal_pending(current)) {
			ret = -EINTR;
			run->exit_reason = KVM_EXIT_INTR;
		}

		/*
		 * Ensure we set mode to IN_GUEST_MODE after we disable
		 * interrupts and before the final VCPU requests check.
		 * See the comment in kvm_vcpu_exiting_guest_mode() and
		 * Documentation/virtual/kvm/vcpu-requests.rst
		 */
		vcpu->mode = IN_GUEST_MODE;

		srcu_read_unlock(&vcpu->kvm->srcu, vcpu->arch.srcu_idx);
		smp_mb__after_srcu_read_unlock();

		/*
		 * We might have got VCPU interrupts updated asynchronously
		 * so update it in HW.
		 */
		kvm_riscv_vcpu_flush_interrupts(vcpu);

		/* Update HVIP CSR for current CPU */
		kvm_riscv_update_hvip(vcpu);

		if (ret <= 0 ||
		    kvm_request_pending(vcpu)) {
			vcpu->mode = OUTSIDE_GUEST_MODE;
			local_irq_enable();
			preempt_enable();
			vcpu->arch.srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
			continue;
		}

		guest_enter_irqoff();

		__kvm_riscv_switch_to(&vcpu->arch);

		vcpu->mode = OUTSIDE_GUEST_MODE;
		vcpu->stat.exits++;

		/*
		 * Save SCAUSE, STVAL, HTVAL, and HTINST because we might
		 * get an interrupt between __kvm_riscv_switch_to() and
		 * local_irq_enable() which can potentially change CSRs.
		 */
		trap.sepc = vcpu->arch.guest_context.sepc;
		trap.scause = csr_read(CSR_SCAUSE);
		trap.stval = csr_read(CSR_STVAL);
		trap.htval = csr_read(CSR_HTVAL);
		trap.htinst = csr_read(CSR_HTINST);

		/* Syncup interrupts state with HW */
		kvm_riscv_vcpu_sync_interrupts(vcpu);

		/*
		 * We may have taken a host interrupt in VS/VU-mode (i.e.
		 * while executing the guest). This interrupt is still
		 * pending, as we haven't serviced it yet!
		 *
		 * We're now back in HS-mode with interrupts disabled
		 * so enabling the interrupts now will have the effect
		 * of taking the interrupt again, in HS-mode this time.
		 */
		local_irq_enable();

		/*
		 * We do local_irq_enable() before calling guest_exit() so
		 * that if a timer interrupt hits while running the guest
		 * we account that tick as being spent in the guest. We
		 * enable preemption after calling guest_exit() so that if
		 * we get preempted we make sure ticks after that is not
		 * counted as guest time.
		 */
		guest_exit();

		preempt_enable();

		vcpu->arch.srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);

		ret = kvm_riscv_vcpu_exit(vcpu, run, &trap);
	}

	kvm_sigset_deactivate(vcpu);

	vcpu_put(vcpu);

	srcu_read_unlock(&vcpu->kvm->srcu, vcpu->arch.srcu_idx);

	return ret;
}
