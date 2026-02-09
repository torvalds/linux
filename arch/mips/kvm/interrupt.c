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

	for_each_set_bit(priority, pending_clr, MIPS_EXC_MAX + 1)
		kvm_mips_callbacks->irq_clear(vcpu, priority, cause);

	for_each_set_bit(priority, pending, MIPS_EXC_MAX + 1)
		kvm_mips_callbacks->irq_deliver(vcpu, priority, cause);
}

int kvm_mips_pending_timer(struct kvm_vcpu *vcpu)
{
	return test_bit(MIPS_EXC_INT_TIMER, &vcpu->arch.pending_exceptions);
}
