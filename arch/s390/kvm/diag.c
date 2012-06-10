/*
 * diag.c - handling diagnose instructions
 *
 * Copyright IBM Corp. 2008,2011
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 */

#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include "kvm-s390.h"

static int diag_release_pages(struct kvm_vcpu *vcpu)
{
	unsigned long start, end;
	unsigned long prefix  = vcpu->arch.sie_block->prefix;

	start = vcpu->run->s.regs.gprs[(vcpu->arch.sie_block->ipa & 0xf0) >> 4];
	end = vcpu->run->s.regs.gprs[vcpu->arch.sie_block->ipa & 0xf] + 4096;

	if (start & ~PAGE_MASK || end & ~PAGE_MASK || start > end
	    || start < 2 * PAGE_SIZE)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	VCPU_EVENT(vcpu, 5, "diag release pages %lX %lX", start, end);
	vcpu->stat.diagnose_10++;

	/* we checked for start > end above */
	if (end < prefix || start >= prefix + 2 * PAGE_SIZE) {
		gmap_discard(start, end, vcpu->arch.gmap);
	} else {
		if (start < prefix)
			gmap_discard(start, prefix, vcpu->arch.gmap);
		if (end >= prefix)
			gmap_discard(prefix + 2 * PAGE_SIZE,
				     end, vcpu->arch.gmap);
	}
	return 0;
}

static int __diag_time_slice_end(struct kvm_vcpu *vcpu)
{
	VCPU_EVENT(vcpu, 5, "%s", "diag time slice end");
	vcpu->stat.diagnose_44++;
	kvm_vcpu_on_spin(vcpu);
	return 0;
}

static int __diag_time_slice_end_directed(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_vcpu *tcpu;
	int tid;
	int i;

	tid = vcpu->run->s.regs.gprs[(vcpu->arch.sie_block->ipa & 0xf0) >> 4];
	vcpu->stat.diagnose_9c++;
	VCPU_EVENT(vcpu, 5, "diag time slice end directed to %d", tid);

	if (tid == vcpu->vcpu_id)
		return 0;

	kvm_for_each_vcpu(i, tcpu, kvm)
		if (tcpu->vcpu_id == tid) {
			kvm_vcpu_yield_to(tcpu);
			break;
		}

	return 0;
}

static int __diag_ipl_functions(struct kvm_vcpu *vcpu)
{
	unsigned int reg = vcpu->arch.sie_block->ipa & 0xf;
	unsigned long subcode = vcpu->run->s.regs.gprs[reg] & 0xffff;

	VCPU_EVENT(vcpu, 5, "diag ipl functions, subcode %lx", subcode);
	switch (subcode) {
	case 3:
		vcpu->run->s390_reset_flags = KVM_S390_RESET_CLEAR;
		break;
	case 4:
		vcpu->run->s390_reset_flags = 0;
		break;
	default:
		return -EOPNOTSUPP;
	}

	atomic_set_mask(CPUSTAT_STOPPED, &vcpu->arch.sie_block->cpuflags);
	vcpu->run->s390_reset_flags |= KVM_S390_RESET_SUBSYSTEM;
	vcpu->run->s390_reset_flags |= KVM_S390_RESET_IPL;
	vcpu->run->s390_reset_flags |= KVM_S390_RESET_CPU_INIT;
	vcpu->run->exit_reason = KVM_EXIT_S390_RESET;
	VCPU_EVENT(vcpu, 3, "requesting userspace resets %llx",
	  vcpu->run->s390_reset_flags);
	return -EREMOTE;
}

int kvm_s390_handle_diag(struct kvm_vcpu *vcpu)
{
	int code = (vcpu->arch.sie_block->ipb & 0xfff0000) >> 16;

	switch (code) {
	case 0x10:
		return diag_release_pages(vcpu);
	case 0x44:
		return __diag_time_slice_end(vcpu);
	case 0x9c:
		return __diag_time_slice_end_directed(vcpu);
	case 0x308:
		return __diag_ipl_functions(vcpu);
	default:
		return -EOPNOTSUPP;
	}
}
