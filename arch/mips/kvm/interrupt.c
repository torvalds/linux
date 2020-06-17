/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/MIPS: Interrupt delivery
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/memblock.h>
#include <asm/page.h>
#include <asm/cacheflush.h>

#include <linux/kvm_host.h>

#include "interrupt.h"

void kvm_mips_queue_irq(struct kvm_vcpu *vcpu, unsigned int priority)
{
	set_bit(priority, &vcpu->arch.pending_exceptions);
}

void kvm_mips_dequeue_irq(struct kvm_vcpu *vcpu, unsigned int priority)
{
	clear_bit(priority, &vcpu->arch.pending_exceptions);
}

void kvm_mips_queue_timer_int_cb(struct kvm_vcpu *vcpu)
{
	/*
	 * Cause bits to reflect the pending timer interrupt,
	 * the EXC code will be set when we are actually
	 * delivering the interrupt:
	 */
	kvm_set_c0_guest_cause(vcpu->arch.cop0, (C_IRQ5 | C_TI));

	/* Queue up an INT exception for the core */
	kvm_mips_queue_irq(vcpu, MIPS_EXC_INT_TIMER);

}

void kvm_mips_dequeue_timer_int_cb(struct kvm_vcpu *vcpu)
{
	kvm_clear_c0_guest_cause(vcpu->arch.cop0, (C_IRQ5 | C_TI));
	kvm_mips_dequeue_irq(vcpu, MIPS_EXC_INT_TIMER);
}

void kvm_mips_queue_io_int_cb(struct kvm_vcpu *vcpu,
			      struct kvm_mips_interrupt *irq)
{
	int intr = (int)irq->irq;

	/*
	 * Cause bits to reflect the pending IO interrupt,
	 * the EXC code will be set when we are actually
	 * delivering the interrupt:
	 */
	kvm_set_c0_guest_cause(vcpu->arch.cop0, 1 << (intr + 8));
	kvm_mips_queue_irq(vcpu, kvm_irq_to_priority(intr));
}

void kvm_mips_dequeue_io_int_cb(struct kvm_vcpu *vcpu,
				struct kvm_mips_interrupt *irq)
{
	int intr = (int)irq->irq;

	kvm_clear_c0_guest_cause(vcpu->arch.cop0, 1 << (-intr + 8));
	kvm_mips_dequeue_irq(vcpu, kvm_irq_to_priority(-intr));
}

/* Deliver the interrupt of the corresponding priority, if possible. */
int kvm_mips_irq_deliver_cb(struct kvm_vcpu *vcpu, unsigned int priority,
			    u32 cause)
{
	int allowed = 0;
	u32 exccode, ie;

	struct kvm_vcpu_arch *arch = &vcpu->arch;
	struct mips_coproc *cop0 = vcpu->arch.cop0;

	if (priority == MIPS_EXC_MAX)
		return 0;

	ie = 1 << (kvm_priority_to_irq[priority] + 8);
	if ((kvm_read_c0_guest_status(cop0) & ST0_IE)
	    && (!(kvm_read_c0_guest_status(cop0) & (ST0_EXL | ST0_ERL)))
	    && (kvm_read_c0_guest_status(cop0) & ie)) {
		allowed = 1;
		exccode = EXCCODE_INT;
	}

	/* Are we allowed to deliver the interrupt ??? */
	if (allowed) {
		if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
			/* save old pc */
			kvm_write_c0_guest_epc(cop0, arch->pc);
			kvm_set_c0_guest_status(cop0, ST0_EXL);

			if (cause & CAUSEF_BD)
				kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
			else
				kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

			kvm_debug("Delivering INT @ pc %#lx\n", arch->pc);

		} else
			kvm_err("Trying to deliver interrupt when EXL is already set\n");

		kvm_change_c0_guest_cause(cop0, CAUSEF_EXCCODE,
					  (exccode << CAUSEB_EXCCODE));

		/* XXXSL Set PC to the interrupt exception entry point */
		arch->pc = kvm_mips_guest_exception_base(vcpu);
		if (kvm_read_c0_guest_cause(cop0) & CAUSEF_IV)
			arch->pc += 0x200;
		else
			arch->pc += 0x180;

		clear_bit(priority, &vcpu->arch.pending_exceptions);
	}

	return allowed;
}

int kvm_mips_irq_clear_cb(struct kvm_vcpu *vcpu, unsigned int priority,
			  u32 cause)
{
	return 1;
}

void kvm_mips_deliver_interrupts(struct kvm_vcpu *vcpu, u32 cause)
{
	unsigned long *pending = &vcpu->arch.pending_exceptions;
	unsigned long *pending_clr = &vcpu->arch.pending_exceptions_clr;
	unsigned int priority;

	if (!(*pending) && !(*pending_clr))
		return;

	priority = __ffs(*pending_clr);
	while (priority <= MIPS_EXC_MAX) {
		if (kvm_mips_callbacks->irq_clear(vcpu, priority, cause)) {
			if (!KVM_MIPS_IRQ_CLEAR_ALL_AT_ONCE)
				break;
		}

		priority = find_next_bit(pending_clr,
					 BITS_PER_BYTE * sizeof(*pending_clr),
					 priority + 1);
	}

	priority = __ffs(*pending);
	while (priority <= MIPS_EXC_MAX) {
		if (kvm_mips_callbacks->irq_deliver(vcpu, priority, cause)) {
			if (!KVM_MIPS_IRQ_DELIVER_ALL_AT_ONCE)
				break;
		}

		priority = find_next_bit(pending,
					 BITS_PER_BYTE * sizeof(*pending),
					 priority + 1);
	}

}

int kvm_mips_pending_timer(struct kvm_vcpu *vcpu)
{
	return test_bit(MIPS_EXC_INT_TIMER, &vcpu->arch.pending_exceptions);
}
