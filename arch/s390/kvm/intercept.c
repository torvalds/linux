/*
 * intercept.c - in-kernel handling for sie intercepts
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 */

#include <linux/kvm_host.h>
#include <linux/errno.h>
#include <linux/pagemap.h>

#include <asm/kvm_host.h>

#include "kvm-s390.h"

static int handle_noop(struct kvm_vcpu *vcpu)
{
	switch (vcpu->arch.sie_block->icptcode) {
	case 0x10:
		vcpu->stat.exit_external_request++;
		break;
	case 0x14:
		vcpu->stat.exit_external_interrupt++;
		break;
	default:
		break; /* nothing */
	}
	return 0;
}

static int handle_stop(struct kvm_vcpu *vcpu)
{
	vcpu->stat.exit_stop_request++;
	VCPU_EVENT(vcpu, 3, "%s", "cpu stopped");
	atomic_clear_mask(CPUSTAT_RUNNING, &vcpu->arch.sie_block->cpuflags);
	return -ENOTSUPP;
}

static int handle_validity(struct kvm_vcpu *vcpu)
{
	int viwhy = vcpu->arch.sie_block->ipb >> 16;
	vcpu->stat.exit_validity++;
	if (viwhy == 0x37) {
		fault_in_pages_writeable((char __user *)
					 vcpu->kvm->arch.guest_origin +
					 vcpu->arch.sie_block->prefix,
					 PAGE_SIZE);
		return 0;
	}
	VCPU_EVENT(vcpu, 2, "unhandled validity intercept code %d",
		   viwhy);
	return -ENOTSUPP;
}

static const intercept_handler_t intercept_funcs[0x48 >> 2] = {
	[0x00 >> 2] = handle_noop,
	[0x10 >> 2] = handle_noop,
	[0x14 >> 2] = handle_noop,
	[0x20 >> 2] = handle_validity,
	[0x28 >> 2] = handle_stop,
};

int kvm_handle_sie_intercept(struct kvm_vcpu *vcpu)
{
	intercept_handler_t func;
	u8 code = vcpu->arch.sie_block->icptcode;

	if (code & 3 || code > 0x48)
		return -ENOTSUPP;
	func = intercept_funcs[code >> 2];
	if (func)
		return func(vcpu);
	return -ENOTSUPP;
}
