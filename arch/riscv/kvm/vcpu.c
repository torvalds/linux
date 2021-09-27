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

int kvm_arch_vcpu_precreate(struct kvm *kvm, unsigned int id)
{
	return 0;
}

int kvm_arch_vcpu_create(struct kvm_vcpu *vcpu)
{
	/* TODO: */
	return 0;
}

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
}

int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu)
{
	/* TODO: */
	return 0;
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	/* TODO: */
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	/* TODO: */
	return 0;
}

void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu)
{
}

void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu)
{
}

int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	/* TODO: */
	return 0;
}

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	/* TODO: */
	return 0;
}

bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu)
{
	/* TODO: */
	return false;
}

vm_fault_t kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

long kvm_arch_vcpu_async_ioctl(struct file *filp,
			       unsigned int ioctl, unsigned long arg)
{
	/* TODO; */
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

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	/* TODO: */
	return 0;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	/* TODO: */
	return 0;
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
	/* TODO: */
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	int ret;
	struct kvm_cpu_trap trap;
	struct kvm_run *run = vcpu->run;

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
		trap.sepc = 0;
		trap.scause = csr_read(CSR_SCAUSE);
		trap.stval = csr_read(CSR_STVAL);
		trap.htval = csr_read(CSR_HTVAL);
		trap.htinst = csr_read(CSR_HTINST);

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
