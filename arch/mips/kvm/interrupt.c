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

void kvm_mips_deliver_interrupts(struct kvm_vcpu *vcpu, u32 cause)
{
	unsigned long *pending = &vcpu->arch.pending_exceptions;
	unsigned long *pending_clr = &vcpu->arch.pending_exceptions_clr;
	unsigned int priority;

	if (!(*pending) && !(*pending_clr))
		return;

	priority = __ffs(*pending_clr);
	while (priority <= MIPS_EXC_MAX) {
		kvm_mips_callbacks->irq_clear(vcpu, priority, cause);

		priority = find_next_bit(pending_clr,
					 BITS_PER_BYTE * sizeof(*pending_clr),
					 priority + 1);
	}

	priority = __ffs(*pending);
	while (priority <= MIPS_EXC_MAX) {
		kvm_mips_callbacks->irq_deliver(vcpu, priority, cause);

		priority = find_next_bit(pending,
					 BITS_PER_BYTE * sizeof(*pending),
					 priority + 1);
	}

}

int kvm_mips_pending_timer(struct kvm_vcpu *vcpu)
{
	return test_bit(MIPS_EXC_INT_TIMER, &vcpu->arch.pending_exceptions);
}
