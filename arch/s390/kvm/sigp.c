/*
 * handling interprocessor communication
 *
 * Copyright IBM Corp. 2008, 2013
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 *               Christian Ehrhardt <ehrhardt@de.ibm.com>
 */

#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/slab.h>
#include <asm/sigp.h>
#include "gaccess.h"
#include "kvm-s390.h"
#include "trace.h"

static int __sigp_sense(struct kvm_vcpu *vcpu, u16 cpu_addr,
			u64 *reg)
{
	struct kvm_s390_local_interrupt *li;
	struct kvm_vcpu *dst_vcpu = NULL;
	int cpuflags;
	int rc;

	if (cpu_addr >= KVM_MAX_VCPUS)
		return SIGP_CC_NOT_OPERATIONAL;

	dst_vcpu = kvm_get_vcpu(vcpu->kvm, cpu_addr);
	if (!dst_vcpu)
		return SIGP_CC_NOT_OPERATIONAL;
	li = &dst_vcpu->arch.local_int;

	cpuflags = atomic_read(li->cpuflags);
	if (!(cpuflags & (CPUSTAT_ECALL_PEND | CPUSTAT_STOPPED)))
		rc = SIGP_CC_ORDER_CODE_ACCEPTED;
	else {
		*reg &= 0xffffffff00000000UL;
		if (cpuflags & CPUSTAT_ECALL_PEND)
			*reg |= SIGP_STATUS_EXT_CALL_PENDING;
		if (cpuflags & CPUSTAT_STOPPED)
			*reg |= SIGP_STATUS_STOPPED;
		rc = SIGP_CC_STATUS_STORED;
	}

	VCPU_EVENT(vcpu, 4, "sensed status of cpu %x rc %x", cpu_addr, rc);
	return rc;
}

static int __sigp_emergency(struct kvm_vcpu *vcpu, u16 cpu_addr)
{
	struct kvm_s390_interrupt s390int = {
		.type = KVM_S390_INT_EMERGENCY,
		.parm = vcpu->vcpu_id,
	};
	struct kvm_vcpu *dst_vcpu = NULL;
	int rc = 0;

	if (cpu_addr < KVM_MAX_VCPUS)
		dst_vcpu = kvm_get_vcpu(vcpu->kvm, cpu_addr);
	if (!dst_vcpu)
		return SIGP_CC_NOT_OPERATIONAL;

	rc = kvm_s390_inject_vcpu(dst_vcpu, &s390int);
	if (!rc)
		VCPU_EVENT(vcpu, 4, "sent sigp emerg to cpu %x", cpu_addr);

	return rc ? rc : SIGP_CC_ORDER_CODE_ACCEPTED;
}

static int __sigp_conditional_emergency(struct kvm_vcpu *vcpu, u16 cpu_addr,
					u16 asn, u64 *reg)
{
	struct kvm_vcpu *dst_vcpu = NULL;
	const u64 psw_int_mask = PSW_MASK_IO | PSW_MASK_EXT;
	u16 p_asn, s_asn;
	psw_t *psw;
	u32 flags;

	if (cpu_addr < KVM_MAX_VCPUS)
		dst_vcpu = kvm_get_vcpu(vcpu->kvm, cpu_addr);
	if (!dst_vcpu)
		return SIGP_CC_NOT_OPERATIONAL;
	flags = atomic_read(&dst_vcpu->arch.sie_block->cpuflags);
	psw = &dst_vcpu->arch.sie_block->gpsw;
	p_asn = dst_vcpu->arch.sie_block->gcr[4] & 0xffff;  /* Primary ASN */
	s_asn = dst_vcpu->arch.sie_block->gcr[3] & 0xffff;  /* Secondary ASN */

	/* Deliver the emergency signal? */
	if (!(flags & CPUSTAT_STOPPED)
	    || (psw->mask & psw_int_mask) != psw_int_mask
	    || ((flags & CPUSTAT_WAIT) && psw->addr != 0)
	    || (!(flags & CPUSTAT_WAIT) && (asn == p_asn || asn == s_asn))) {
		return __sigp_emergency(vcpu, cpu_addr);
	} else {
		*reg &= 0xffffffff00000000UL;
		*reg |= SIGP_STATUS_INCORRECT_STATE;
		return SIGP_CC_STATUS_STORED;
	}
}

static int __sigp_external_call(struct kvm_vcpu *vcpu, u16 cpu_addr)
{
	struct kvm_s390_interrupt s390int = {
		.type = KVM_S390_INT_EXTERNAL_CALL,
		.parm = vcpu->vcpu_id,
	};
	struct kvm_vcpu *dst_vcpu = NULL;
	int rc;

	if (cpu_addr < KVM_MAX_VCPUS)
		dst_vcpu = kvm_get_vcpu(vcpu->kvm, cpu_addr);
	if (!dst_vcpu)
		return SIGP_CC_NOT_OPERATIONAL;

	rc = kvm_s390_inject_vcpu(dst_vcpu, &s390int);
	if (!rc)
		VCPU_EVENT(vcpu, 4, "sent sigp ext call to cpu %x", cpu_addr);

	return rc ? rc : SIGP_CC_ORDER_CODE_ACCEPTED;
}

static int __inject_sigp_stop(struct kvm_s390_local_interrupt *li, int action)
{
	struct kvm_s390_interrupt_info *inti;
	int rc = SIGP_CC_ORDER_CODE_ACCEPTED;

	inti = kzalloc(sizeof(*inti), GFP_ATOMIC);
	if (!inti)
		return -ENOMEM;
	inti->type = KVM_S390_SIGP_STOP;

	spin_lock_bh(&li->lock);
	if (li->action_bits & ACTION_STOP_ON_STOP) {
		/* another SIGP STOP is pending */
		rc = SIGP_CC_BUSY;
		goto out;
	}
	if ((atomic_read(li->cpuflags) & CPUSTAT_STOPPED)) {
		kfree(inti);
		if ((action & ACTION_STORE_ON_STOP) != 0)
			rc = -ESHUTDOWN;
		goto out;
	}
	list_add_tail(&inti->list, &li->list);
	atomic_set(&li->active, 1);
	li->action_bits |= action;
	atomic_set_mask(CPUSTAT_STOP_INT, li->cpuflags);
	if (waitqueue_active(li->wq))
		wake_up_interruptible(li->wq);
out:
	spin_unlock_bh(&li->lock);

	return rc;
}

static int __sigp_stop(struct kvm_vcpu *vcpu, u16 cpu_addr, int action)
{
	struct kvm_s390_local_interrupt *li;
	struct kvm_vcpu *dst_vcpu = NULL;
	int rc;

	if (cpu_addr >= KVM_MAX_VCPUS)
		return SIGP_CC_NOT_OPERATIONAL;

	dst_vcpu = kvm_get_vcpu(vcpu->kvm, cpu_addr);
	if (!dst_vcpu)
		return SIGP_CC_NOT_OPERATIONAL;
	li = &dst_vcpu->arch.local_int;

	rc = __inject_sigp_stop(li, action);

	VCPU_EVENT(vcpu, 4, "sent sigp stop to cpu %x", cpu_addr);

	if ((action & ACTION_STORE_ON_STOP) != 0 && rc == -ESHUTDOWN) {
		/* If the CPU has already been stopped, we still have
		 * to save the status when doing stop-and-store. This
		 * has to be done after unlocking all spinlocks. */
		rc = kvm_s390_store_status_unloaded(dst_vcpu,
						KVM_S390_STORE_STATUS_NOADDR);
	}

	return rc;
}

static int __sigp_set_arch(struct kvm_vcpu *vcpu, u32 parameter)
{
	int rc;
	unsigned int i;
	struct kvm_vcpu *v;

	switch (parameter & 0xff) {
	case 0:
		rc = SIGP_CC_NOT_OPERATIONAL;
		break;
	case 1:
	case 2:
		kvm_for_each_vcpu(i, v, vcpu->kvm) {
			v->arch.pfault_token = KVM_S390_PFAULT_TOKEN_INVALID;
			kvm_clear_async_pf_completion_queue(v);
		}

		rc = SIGP_CC_ORDER_CODE_ACCEPTED;
		break;
	default:
		rc = -EOPNOTSUPP;
	}
	return rc;
}

static int __sigp_set_prefix(struct kvm_vcpu *vcpu, u16 cpu_addr, u32 address,
			     u64 *reg)
{
	struct kvm_s390_local_interrupt *li;
	struct kvm_vcpu *dst_vcpu = NULL;
	struct kvm_s390_interrupt_info *inti;
	int rc;

	if (cpu_addr < KVM_MAX_VCPUS)
		dst_vcpu = kvm_get_vcpu(vcpu->kvm, cpu_addr);
	if (!dst_vcpu)
		return SIGP_CC_NOT_OPERATIONAL;
	li = &dst_vcpu->arch.local_int;

	/*
	 * Make sure the new value is valid memory. We only need to check the
	 * first page, since address is 8k aligned and memory pieces are always
	 * at least 1MB aligned and have at least a size of 1MB.
	 */
	address &= 0x7fffe000u;
	if (kvm_is_error_gpa(vcpu->kvm, address)) {
		*reg &= 0xffffffff00000000UL;
		*reg |= SIGP_STATUS_INVALID_PARAMETER;
		return SIGP_CC_STATUS_STORED;
	}

	inti = kzalloc(sizeof(*inti), GFP_KERNEL);
	if (!inti)
		return SIGP_CC_BUSY;

	spin_lock_bh(&li->lock);
	/* cpu must be in stopped state */
	if (!(atomic_read(li->cpuflags) & CPUSTAT_STOPPED)) {
		*reg &= 0xffffffff00000000UL;
		*reg |= SIGP_STATUS_INCORRECT_STATE;
		rc = SIGP_CC_STATUS_STORED;
		kfree(inti);
		goto out_li;
	}

	inti->type = KVM_S390_SIGP_SET_PREFIX;
	inti->prefix.address = address;

	list_add_tail(&inti->list, &li->list);
	atomic_set(&li->active, 1);
	if (waitqueue_active(li->wq))
		wake_up_interruptible(li->wq);
	rc = SIGP_CC_ORDER_CODE_ACCEPTED;

	VCPU_EVENT(vcpu, 4, "set prefix of cpu %02x to %x", cpu_addr, address);
out_li:
	spin_unlock_bh(&li->lock);
	return rc;
}

static int __sigp_store_status_at_addr(struct kvm_vcpu *vcpu, u16 cpu_id,
					u32 addr, u64 *reg)
{
	struct kvm_vcpu *dst_vcpu = NULL;
	int flags;
	int rc;

	if (cpu_id < KVM_MAX_VCPUS)
		dst_vcpu = kvm_get_vcpu(vcpu->kvm, cpu_id);
	if (!dst_vcpu)
		return SIGP_CC_NOT_OPERATIONAL;

	spin_lock_bh(&dst_vcpu->arch.local_int.lock);
	flags = atomic_read(dst_vcpu->arch.local_int.cpuflags);
	spin_unlock_bh(&dst_vcpu->arch.local_int.lock);
	if (!(flags & CPUSTAT_STOPPED)) {
		*reg &= 0xffffffff00000000UL;
		*reg |= SIGP_STATUS_INCORRECT_STATE;
		return SIGP_CC_STATUS_STORED;
	}

	addr &= 0x7ffffe00;
	rc = kvm_s390_store_status_unloaded(dst_vcpu, addr);
	if (rc == -EFAULT) {
		*reg &= 0xffffffff00000000UL;
		*reg |= SIGP_STATUS_INVALID_PARAMETER;
		rc = SIGP_CC_STATUS_STORED;
	}
	return rc;
}

static int __sigp_sense_running(struct kvm_vcpu *vcpu, u16 cpu_addr,
				u64 *reg)
{
	struct kvm_s390_local_interrupt *li;
	struct kvm_vcpu *dst_vcpu = NULL;
	int rc;

	if (cpu_addr >= KVM_MAX_VCPUS)
		return SIGP_CC_NOT_OPERATIONAL;

	dst_vcpu = kvm_get_vcpu(vcpu->kvm, cpu_addr);
	if (!dst_vcpu)
		return SIGP_CC_NOT_OPERATIONAL;
	li = &dst_vcpu->arch.local_int;
	if (atomic_read(li->cpuflags) & CPUSTAT_RUNNING) {
		/* running */
		rc = SIGP_CC_ORDER_CODE_ACCEPTED;
	} else {
		/* not running */
		*reg &= 0xffffffff00000000UL;
		*reg |= SIGP_STATUS_NOT_RUNNING;
		rc = SIGP_CC_STATUS_STORED;
	}

	VCPU_EVENT(vcpu, 4, "sensed running status of cpu %x rc %x", cpu_addr,
		   rc);

	return rc;
}

/* Test whether the destination CPU is available and not busy */
static int sigp_check_callable(struct kvm_vcpu *vcpu, u16 cpu_addr)
{
	struct kvm_s390_local_interrupt *li;
	int rc = SIGP_CC_ORDER_CODE_ACCEPTED;
	struct kvm_vcpu *dst_vcpu = NULL;

	if (cpu_addr >= KVM_MAX_VCPUS)
		return SIGP_CC_NOT_OPERATIONAL;

	dst_vcpu = kvm_get_vcpu(vcpu->kvm, cpu_addr);
	if (!dst_vcpu)
		return SIGP_CC_NOT_OPERATIONAL;
	li = &dst_vcpu->arch.local_int;
	spin_lock_bh(&li->lock);
	if (li->action_bits & ACTION_STOP_ON_STOP)
		rc = SIGP_CC_BUSY;
	spin_unlock_bh(&li->lock);

	return rc;
}

int kvm_s390_handle_sigp(struct kvm_vcpu *vcpu)
{
	int r1 = (vcpu->arch.sie_block->ipa & 0x00f0) >> 4;
	int r3 = vcpu->arch.sie_block->ipa & 0x000f;
	u32 parameter;
	u16 cpu_addr = vcpu->run->s.regs.gprs[r3];
	u8 order_code;
	int rc;

	/* sigp in userspace can exit */
	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	order_code = kvm_s390_get_base_disp_rs(vcpu);

	if (r1 % 2)
		parameter = vcpu->run->s.regs.gprs[r1];
	else
		parameter = vcpu->run->s.regs.gprs[r1 + 1];

	trace_kvm_s390_handle_sigp(vcpu, order_code, cpu_addr, parameter);
	switch (order_code) {
	case SIGP_SENSE:
		vcpu->stat.instruction_sigp_sense++;
		rc = __sigp_sense(vcpu, cpu_addr,
				  &vcpu->run->s.regs.gprs[r1]);
		break;
	case SIGP_EXTERNAL_CALL:
		vcpu->stat.instruction_sigp_external_call++;
		rc = __sigp_external_call(vcpu, cpu_addr);
		break;
	case SIGP_EMERGENCY_SIGNAL:
		vcpu->stat.instruction_sigp_emergency++;
		rc = __sigp_emergency(vcpu, cpu_addr);
		break;
	case SIGP_STOP:
		vcpu->stat.instruction_sigp_stop++;
		rc = __sigp_stop(vcpu, cpu_addr, ACTION_STOP_ON_STOP);
		break;
	case SIGP_STOP_AND_STORE_STATUS:
		vcpu->stat.instruction_sigp_stop++;
		rc = __sigp_stop(vcpu, cpu_addr, ACTION_STORE_ON_STOP |
						 ACTION_STOP_ON_STOP);
		break;
	case SIGP_STORE_STATUS_AT_ADDRESS:
		rc = __sigp_store_status_at_addr(vcpu, cpu_addr, parameter,
						 &vcpu->run->s.regs.gprs[r1]);
		break;
	case SIGP_SET_ARCHITECTURE:
		vcpu->stat.instruction_sigp_arch++;
		rc = __sigp_set_arch(vcpu, parameter);
		break;
	case SIGP_SET_PREFIX:
		vcpu->stat.instruction_sigp_prefix++;
		rc = __sigp_set_prefix(vcpu, cpu_addr, parameter,
				       &vcpu->run->s.regs.gprs[r1]);
		break;
	case SIGP_COND_EMERGENCY_SIGNAL:
		rc = __sigp_conditional_emergency(vcpu, cpu_addr, parameter,
						  &vcpu->run->s.regs.gprs[r1]);
		break;
	case SIGP_SENSE_RUNNING:
		vcpu->stat.instruction_sigp_sense_running++;
		rc = __sigp_sense_running(vcpu, cpu_addr,
					  &vcpu->run->s.regs.gprs[r1]);
		break;
	case SIGP_START:
		rc = sigp_check_callable(vcpu, cpu_addr);
		if (rc == SIGP_CC_ORDER_CODE_ACCEPTED)
			rc = -EOPNOTSUPP;    /* Handle START in user space */
		break;
	case SIGP_RESTART:
		vcpu->stat.instruction_sigp_restart++;
		rc = sigp_check_callable(vcpu, cpu_addr);
		if (rc == SIGP_CC_ORDER_CODE_ACCEPTED) {
			VCPU_EVENT(vcpu, 4,
				   "sigp restart %x to handle userspace",
				   cpu_addr);
			/* user space must know about restart */
			rc = -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (rc < 0)
		return rc;

	kvm_s390_set_psw_cc(vcpu, rc);
	return 0;
}

/*
 * Handle SIGP partial execution interception.
 *
 * This interception will occur at the source cpu when a source cpu sends an
 * external call to a target cpu and the target cpu has the WAIT bit set in
 * its cpuflags. Interception will occurr after the interrupt indicator bits at
 * the target cpu have been set. All error cases will lead to instruction
 * interception, therefore nothing is to be checked or prepared.
 */
int kvm_s390_handle_sigp_pei(struct kvm_vcpu *vcpu)
{
	int r3 = vcpu->arch.sie_block->ipa & 0x000f;
	u16 cpu_addr = vcpu->run->s.regs.gprs[r3];
	struct kvm_vcpu *dest_vcpu;
	u8 order_code = kvm_s390_get_base_disp_rs(vcpu);

	trace_kvm_s390_handle_sigp_pei(vcpu, order_code, cpu_addr);

	if (order_code == SIGP_EXTERNAL_CALL) {
		dest_vcpu = kvm_get_vcpu(vcpu->kvm, cpu_addr);
		BUG_ON(dest_vcpu == NULL);

		spin_lock_bh(&dest_vcpu->arch.local_int.lock);
		if (waitqueue_active(&dest_vcpu->wq))
			wake_up_interruptible(&dest_vcpu->wq);
		dest_vcpu->preempted = true;
		spin_unlock_bh(&dest_vcpu->arch.local_int.lock);

		kvm_s390_set_psw_cc(vcpu, SIGP_CC_ORDER_CODE_ACCEPTED);
		return 0;
	}

	return -EOPNOTSUPP;
}
