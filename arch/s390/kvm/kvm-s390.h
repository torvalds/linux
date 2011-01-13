/*
 * kvm_s390.h -  definition for kvm on s390
 *
 * Copyright IBM Corp. 2008,2009
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 *               Christian Ehrhardt <ehrhardt@de.ibm.com>
 */

#ifndef ARCH_S390_KVM_S390_H
#define ARCH_S390_KVM_S390_H

#include <linux/hrtimer.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>

/* The current code can have up to 256 pages for virtio */
#define VIRTIODESCSPACE (256ul * 4096ul)

typedef int (*intercept_handler_t)(struct kvm_vcpu *vcpu);

/* negativ values are error codes, positive values for internal conditions */
#define SIE_INTERCEPT_RERUNVCPU		(1<<0)
int kvm_handle_sie_intercept(struct kvm_vcpu *vcpu);

#define VM_EVENT(d_kvm, d_loglevel, d_string, d_args...)\
do { \
	debug_sprintf_event(d_kvm->arch.dbf, d_loglevel, d_string "\n", \
	  d_args); \
} while (0)

#define VCPU_EVENT(d_vcpu, d_loglevel, d_string, d_args...)\
do { \
	debug_sprintf_event(d_vcpu->kvm->arch.dbf, d_loglevel, \
	  "%02d[%016lx-%016lx]: " d_string "\n", d_vcpu->vcpu_id, \
	  d_vcpu->arch.sie_block->gpsw.mask, d_vcpu->arch.sie_block->gpsw.addr,\
	  d_args); \
} while (0)

static inline int __cpu_is_stopped(struct kvm_vcpu *vcpu)
{
	return atomic_read(&vcpu->arch.sie_block->cpuflags) & CPUSTAT_STOP_INT;
}

int kvm_s390_handle_wait(struct kvm_vcpu *vcpu);
enum hrtimer_restart kvm_s390_idle_wakeup(struct hrtimer *timer);
void kvm_s390_tasklet(unsigned long parm);
void kvm_s390_deliver_pending_interrupts(struct kvm_vcpu *vcpu);
int kvm_s390_inject_vm(struct kvm *kvm,
		struct kvm_s390_interrupt *s390int);
int kvm_s390_inject_vcpu(struct kvm_vcpu *vcpu,
		struct kvm_s390_interrupt *s390int);
int kvm_s390_inject_program_int(struct kvm_vcpu *vcpu, u16 code);
int kvm_s390_inject_sigp_stop(struct kvm_vcpu *vcpu, int action);

static inline long kvm_s390_vcpu_get_memsize(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.sie_block->gmslm
		- vcpu->arch.sie_block->gmsor
		- VIRTIODESCSPACE + 1ul;
}

static inline void kvm_s390_vcpu_set_mem(struct kvm_vcpu *vcpu)
{
	int idx;
	struct kvm_memory_slot *mem;
	struct kvm_memslots *memslots;

	idx = srcu_read_lock(&vcpu->kvm->srcu);
	memslots = kvm_memslots(vcpu->kvm);

	mem = &memslots->memslots[0];

	vcpu->arch.sie_block->gmsor = mem->userspace_addr;
	vcpu->arch.sie_block->gmslm =
		mem->userspace_addr +
		(mem->npages << PAGE_SHIFT) +
		VIRTIODESCSPACE - 1ul;

	srcu_read_unlock(&vcpu->kvm->srcu, idx);
}

/* implemented in priv.c */
int kvm_s390_handle_b2(struct kvm_vcpu *vcpu);

/* implemented in sigp.c */
int kvm_s390_handle_sigp(struct kvm_vcpu *vcpu);

/* implemented in kvm-s390.c */
int kvm_s390_vcpu_store_status(struct kvm_vcpu *vcpu,
				 unsigned long addr);
/* implemented in diag.c */
int kvm_s390_handle_diag(struct kvm_vcpu *vcpu);

#endif
