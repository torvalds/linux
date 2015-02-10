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

static int __sigp_sense(struct kvm_vcpu *vcpu, struct kvm_vcpu *dst_vcpu,
			u64 *reg)
{
	struct kvm_s390_local_interrupt *li;
	int cpuflags;
	int rc;

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

	VCPU_EVENT(vcpu, 4, "sensed status of cpu %x rc %x", dst_vcpu->vcpu_id,
		   rc);
	return rc;
}

static int __inject_sigp_emergency(struct kvm_vcpu *vcpu,
				    struct kvm_vcpu *dst_vcpu)
{
	struct kvm_s390_irq irq = {
		.type = KVM_S390_INT_EMERGENCY,
		.u.emerg.code = vcpu->vcpu_id,
	};
	int rc = 0;

	rc = kvm_s390_inject_vcpu(dst_vcpu, &irq);
	if (!rc)
		VCPU_EVENT(vcpu, 4, "sent sigp emerg to cpu %x",
			   dst_vcpu->vcpu_id);

	return rc ? rc : SIGP_CC_ORDER_CODE_ACCEPTED;
}

static int __sigp_emergency(struct kvm_vcpu *vcpu, struct kvm_vcpu *dst_vcpu)
{
	return __inject_sigp_emergency(vcpu, dst_vcpu);
}

static int __sigp_conditional_emergency(struct kvm_vcpu *vcpu,
					struct kvm_vcpu *dst_vcpu,
					u16 asn, u64 *reg)
{
	const u64 psw_int_mask = PSW_MASK_IO | PSW_MASK_EXT;
	u16 p_asn, s_asn;
	psw_t *psw;
	u32 flags;

	flags = atomic_read(&dst_vcpu->arch.sie_block->cpuflags);
	psw = &dst_vcpu->arch.sie_block->gpsw;
	p_asn = dst_vcpu->arch.sie_block->gcr[4] & 0xffff;  /* Primary ASN */
	s_asn = dst_vcpu->arch.sie_block->gcr[3] & 0xffff;  /* Secondary ASN */

	/* Inject the emergency signal? */
	if (!(flags & CPUSTAT_STOPPED)
	    || (psw->mask & psw_int_mask) != psw_int_mask
	    || ((flags & CPUSTAT_WAIT) && psw->addr != 0)
	    || (!(flags & CPUSTAT_WAIT) && (asn == p_asn || asn == s_asn))) {
		return __inject_sigp_emergency(vcpu, dst_vcpu);
	} else {
		*reg &= 0xffffffff00000000UL;
		*reg |= SIGP_STATUS_INCORRECT_STATE;
		return SIGP_CC_STATUS_STORED;
	}
}

static int __sigp_external_call(struct kvm_vcpu *vcpu,
				struct kvm_vcpu *dst_vcpu)
{
	struct kvm_s390_irq irq = {
		.type = KVM_S390_INT_EXTERNAL_CALL,
		.u.extcall.code = vcpu->vcpu_id,
	};
	int rc;

	rc = kvm_s390_inject_vcpu(dst_vcpu, &irq);
	if (!rc)
		VCPU_EVENT(vcpu, 4, "sent sigp ext call to cpu %x",
			   dst_vcpu->vcpu_id);

	return rc ? rc : SIGP_CC_ORDER_CODE_ACCEPTED;
}

static int __inject_sigp_stop(struct kvm_vcpu *dst_vcpu, int action)
{
	struct kvm_s390_local_interrupt *li = &dst_vcpu->arch.local_int;
	int rc = SIGP_CC_ORDER_CODE_ACCEPTED;

	spin_lock(&li->lock);
	if (li->action_bits & ACTION_STOP_ON_STOP) {
		/* another SIGP STOP is pending */
		rc = SIGP_CC_BUSY;
		goto out;
	}
	if ((atomic_read(li->cpuflags) & CPUSTAT_STOPPED)) {
		if ((action & ACTION_STORE_ON_STOP) != 0)
			rc = -ESHUTDOWN;
		goto out;
	}
	set_bit(IRQ_PEND_SIGP_STOP, &li->pending_irqs);
	li->action_bits |= action;
	atomic_set_mask(CPUSTAT_STOP_INT, li->cpuflags);
	kvm_s390_vcpu_wakeup(dst_vcpu);
out:
	spin_unlock(&li->lock);

	return rc;
}

static int __sigp_stop(struct kvm_vcpu *vcpu, struct kvm_vcpu *dst_vcpu)
{
	int rc;

	rc = __inject_sigp_stop(dst_vcpu, ACTION_STOP_ON_STOP);
	VCPU_EVENT(vcpu, 4, "sent sigp stop to cpu %x", dst_vcpu->vcpu_id);

	return rc;
}

static int __sigp_stop_and_store_status(struct kvm_vcpu *vcpu,
					struct kvm_vcpu *dst_vcpu, u64 *reg)
{
	int rc;

	rc = __inject_sigp_stop(dst_vcpu, ACTION_STOP_ON_STOP |
					      ACTION_STORE_ON_STOP);
	VCPU_EVENT(vcpu, 4, "sent sigp stop and store status to cpu %x",
		   dst_vcpu->vcpu_id);

	if (rc == -ESHUTDOWN) {
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

static int __sigp_set_prefix(struct kvm_vcpu *vcpu, struct kvm_vcpu *dst_vcpu,
			     u32 address, u64 *reg)
{
	struct kvm_s390_local_interrupt *li;
	int rc;

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

	spin_lock(&li->lock);
	/* cpu must be in stopped state */
	if (!(atomic_read(li->cpuflags) & CPUSTAT_STOPPED)) {
		*reg &= 0xffffffff00000000UL;
		*reg |= SIGP_STATUS_INCORRECT_STATE;
		rc = SIGP_CC_STATUS_STORED;
		goto out_li;
	}

	li->irq.prefix.address = address;
	set_bit(IRQ_PEND_SET_PREFIX, &li->pending_irqs);
	kvm_s390_vcpu_wakeup(dst_vcpu);
	rc = SIGP_CC_ORDER_CODE_ACCEPTED;

	VCPU_EVENT(vcpu, 4, "set prefix of cpu %02x to %x", dst_vcpu->vcpu_id,
		   address);
out_li:
	spin_unlock(&li->lock);
	return rc;
}

static int __sigp_store_status_at_addr(struct kvm_vcpu *vcpu,
				       struct kvm_vcpu *dst_vcpu,
				       u32 addr, u64 *reg)
{
	int flags;
	int rc;

	spin_lock(&dst_vcpu->arch.local_int.lock);
	flags = atomic_read(dst_vcpu->arch.local_int.cpuflags);
	spin_unlock(&dst_vcpu->arch.local_int.lock);
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

static int __sigp_sense_running(struct kvm_vcpu *vcpu,
				struct kvm_vcpu *dst_vcpu, u64 *reg)
{
	struct kvm_s390_local_interrupt *li;
	int rc;

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

	VCPU_EVENT(vcpu, 4, "sensed running status of cpu %x rc %x",
		   dst_vcpu->vcpu_id, rc);

	return rc;
}

static int __prepare_sigp_re_start(struct kvm_vcpu *vcpu,
				   struct kvm_vcpu *dst_vcpu, u8 order_code)
{
	struct kvm_s390_local_interrupt *li = &dst_vcpu->arch.local_int;
	/* handle (RE)START in user space */
	int rc = -EOPNOTSUPP;

	spin_lock(&li->lock);
	if (li->action_bits & ACTION_STOP_ON_STOP)
		rc = SIGP_CC_BUSY;
	spin_unlock(&li->lock);

	return rc;
}

static int __prepare_sigp_cpu_reset(struct kvm_vcpu *vcpu,
				    struct kvm_vcpu *dst_vcpu, u8 order_code)
{
	/* handle (INITIAL) CPU RESET in user space */
	return -EOPNOTSUPP;
}

static int __prepare_sigp_unknown(struct kvm_vcpu *vcpu,
				  struct kvm_vcpu *dst_vcpu)
{
	/* handle unknown orders in user space */
	return -EOPNOTSUPP;
}

static int handle_sigp_dst(struct kvm_vcpu *vcpu, u8 order_code,
			   u16 cpu_addr, u32 parameter, u64 *status_reg)
{
	int rc;
	struct kvm_vcpu *dst_vcpu;

	if (cpu_addr >= KVM_MAX_VCPUS)
		return SIGP_CC_NOT_OPERATIONAL;

	dst_vcpu = kvm_get_vcpu(vcpu->kvm, cpu_addr);
	if (!dst_vcpu)
		return SIGP_CC_NOT_OPERATIONAL;

	switch (order_code) {
	case SIGP_SENSE:
		vcpu->stat.instruction_sigp_sense++;
		rc = __sigp_sense(vcpu, dst_vcpu, status_reg);
		break;
	case SIGP_EXTERNAL_CALL:
		vcpu->stat.instruction_sigp_external_call++;
		rc = __sigp_external_call(vcpu, dst_vcpu);
		break;
	case SIGP_EMERGENCY_SIGNAL:
		vcpu->stat.instruction_sigp_emergency++;
		rc = __sigp_emergency(vcpu, dst_vcpu);
		break;
	case SIGP_STOP:
		vcpu->stat.instruction_sigp_stop++;
		rc = __sigp_stop(vcpu, dst_vcpu);
		break;
	case SIGP_STOP_AND_STORE_STATUS:
		vcpu->stat.instruction_sigp_stop_store_status++;
		rc = __sigp_stop_and_store_status(vcpu, dst_vcpu, status_reg);
		break;
	case SIGP_STORE_STATUS_AT_ADDRESS:
		vcpu->stat.instruction_sigp_store_status++;
		rc = __sigp_store_status_at_addr(vcpu, dst_vcpu, parameter,
						 status_reg);
		break;
	case SIGP_SET_PREFIX:
		vcpu->stat.instruction_sigp_prefix++;
		rc = __sigp_set_prefix(vcpu, dst_vcpu, parameter, status_reg);
		break;
	case SIGP_COND_EMERGENCY_SIGNAL:
		vcpu->stat.instruction_sigp_cond_emergency++;
		rc = __sigp_conditional_emergency(vcpu, dst_vcpu, parameter,
						  status_reg);
		break;
	case SIGP_SENSE_RUNNING:
		vcpu->stat.instruction_sigp_sense_running++;
		rc = __sigp_sense_running(vcpu, dst_vcpu, status_reg);
		break;
	case SIGP_START:
		vcpu->stat.instruction_sigp_start++;
		rc = __prepare_sigp_re_start(vcpu, dst_vcpu, order_code);
		break;
	case SIGP_RESTART:
		vcpu->stat.instruction_sigp_restart++;
		rc = __prepare_sigp_re_start(vcpu, dst_vcpu, order_code);
		break;
	case SIGP_INITIAL_CPU_RESET:
		vcpu->stat.instruction_sigp_init_cpu_reset++;
		rc = __prepare_sigp_cpu_reset(vcpu, dst_vcpu, order_code);
		break;
	case SIGP_CPU_RESET:
		vcpu->stat.instruction_sigp_cpu_reset++;
		rc = __prepare_sigp_cpu_reset(vcpu, dst_vcpu, order_code);
		break;
	default:
		vcpu->stat.instruction_sigp_unknown++;
		rc = __prepare_sigp_unknown(vcpu, dst_vcpu);
	}

	if (rc == -EOPNOTSUPP)
		VCPU_EVENT(vcpu, 4,
			   "sigp order %u -> cpu %x: handled in user space",
			   order_code, dst_vcpu->vcpu_id);

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
	case SIGP_SET_ARCHITECTURE:
		vcpu->stat.instruction_sigp_arch++;
		rc = __sigp_set_arch(vcpu, parameter);
		break;
	default:
		rc = handle_sigp_dst(vcpu, order_code, cpu_addr,
				     parameter,
				     &vcpu->run->s.regs.gprs[r1]);
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

		kvm_s390_vcpu_wakeup(dest_vcpu);
		kvm_s390_set_psw_cc(vcpu, SIGP_CC_ORDER_CODE_ACCEPTED);
		return 0;
	}

	return -EOPNOTSUPP;
}
