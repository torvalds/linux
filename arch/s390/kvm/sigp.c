/*
 * sigp.c - handlinge interprocessor communication
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

#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/slab.h>
#include "gaccess.h"
#include "kvm-s390.h"

/* sigp order codes */
#define SIGP_SENSE             0x01
#define SIGP_EXTERNAL_CALL     0x02
#define SIGP_EMERGENCY         0x03
#define SIGP_START             0x04
#define SIGP_STOP              0x05
#define SIGP_RESTART           0x06
#define SIGP_STOP_STORE_STATUS 0x09
#define SIGP_INITIAL_CPU_RESET 0x0b
#define SIGP_CPU_RESET         0x0c
#define SIGP_SET_PREFIX        0x0d
#define SIGP_STORE_STATUS_ADDR 0x0e
#define SIGP_SET_ARCH          0x12
#define SIGP_SENSE_RUNNING     0x15

/* cpu status bits */
#define SIGP_STAT_EQUIPMENT_CHECK   0x80000000UL
#define SIGP_STAT_NOT_RUNNING	    0x00000400UL
#define SIGP_STAT_INCORRECT_STATE   0x00000200UL
#define SIGP_STAT_INVALID_PARAMETER 0x00000100UL
#define SIGP_STAT_EXT_CALL_PENDING  0x00000080UL
#define SIGP_STAT_STOPPED           0x00000040UL
#define SIGP_STAT_OPERATOR_INTERV   0x00000020UL
#define SIGP_STAT_CHECK_STOP        0x00000010UL
#define SIGP_STAT_INOPERATIVE       0x00000004UL
#define SIGP_STAT_INVALID_ORDER     0x00000002UL
#define SIGP_STAT_RECEIVER_CHECK    0x00000001UL


static int __sigp_sense(struct kvm_vcpu *vcpu, u16 cpu_addr,
			u64 *reg)
{
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;
	int rc;

	if (cpu_addr >= KVM_MAX_VCPUS)
		return 3; /* not operational */

	spin_lock(&fi->lock);
	if (fi->local_int[cpu_addr] == NULL)
		rc = 3; /* not operational */
	else if (!(atomic_read(fi->local_int[cpu_addr]->cpuflags)
		  & CPUSTAT_STOPPED)) {
		*reg &= 0xffffffff00000000UL;
		rc = 1; /* status stored */
	} else {
		*reg &= 0xffffffff00000000UL;
		*reg |= SIGP_STAT_STOPPED;
		rc = 1; /* status stored */
	}
	spin_unlock(&fi->lock);

	VCPU_EVENT(vcpu, 4, "sensed status of cpu %x rc %x", cpu_addr, rc);
	return rc;
}

static int __sigp_emergency(struct kvm_vcpu *vcpu, u16 cpu_addr)
{
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;
	struct kvm_s390_local_interrupt *li;
	struct kvm_s390_interrupt_info *inti;
	int rc;

	if (cpu_addr >= KVM_MAX_VCPUS)
		return 3; /* not operational */

	inti = kzalloc(sizeof(*inti), GFP_KERNEL);
	if (!inti)
		return -ENOMEM;

	inti->type = KVM_S390_INT_EMERGENCY;
	inti->emerg.code = vcpu->vcpu_id;

	spin_lock(&fi->lock);
	li = fi->local_int[cpu_addr];
	if (li == NULL) {
		rc = 3; /* not operational */
		kfree(inti);
		goto unlock;
	}
	spin_lock_bh(&li->lock);
	list_add_tail(&inti->list, &li->list);
	atomic_set(&li->active, 1);
	atomic_set_mask(CPUSTAT_EXT_INT, li->cpuflags);
	if (waitqueue_active(&li->wq))
		wake_up_interruptible(&li->wq);
	spin_unlock_bh(&li->lock);
	rc = 0; /* order accepted */
	VCPU_EVENT(vcpu, 4, "sent sigp emerg to cpu %x", cpu_addr);
unlock:
	spin_unlock(&fi->lock);
	return rc;
}

static int __sigp_external_call(struct kvm_vcpu *vcpu, u16 cpu_addr)
{
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;
	struct kvm_s390_local_interrupt *li;
	struct kvm_s390_interrupt_info *inti;
	int rc;

	if (cpu_addr >= KVM_MAX_VCPUS)
		return 3; /* not operational */

	inti = kzalloc(sizeof(*inti), GFP_KERNEL);
	if (!inti)
		return -ENOMEM;

	inti->type = KVM_S390_INT_EXTERNAL_CALL;
	inti->extcall.code = vcpu->vcpu_id;

	spin_lock(&fi->lock);
	li = fi->local_int[cpu_addr];
	if (li == NULL) {
		rc = 3; /* not operational */
		kfree(inti);
		goto unlock;
	}
	spin_lock_bh(&li->lock);
	list_add_tail(&inti->list, &li->list);
	atomic_set(&li->active, 1);
	atomic_set_mask(CPUSTAT_EXT_INT, li->cpuflags);
	if (waitqueue_active(&li->wq))
		wake_up_interruptible(&li->wq);
	spin_unlock_bh(&li->lock);
	rc = 0; /* order accepted */
	VCPU_EVENT(vcpu, 4, "sent sigp ext call to cpu %x", cpu_addr);
unlock:
	spin_unlock(&fi->lock);
	return rc;
}

static int __inject_sigp_stop(struct kvm_s390_local_interrupt *li, int action)
{
	struct kvm_s390_interrupt_info *inti;

	inti = kzalloc(sizeof(*inti), GFP_ATOMIC);
	if (!inti)
		return -ENOMEM;
	inti->type = KVM_S390_SIGP_STOP;

	spin_lock_bh(&li->lock);
	if ((atomic_read(li->cpuflags) & CPUSTAT_STOPPED))
		goto out;
	list_add_tail(&inti->list, &li->list);
	atomic_set(&li->active, 1);
	atomic_set_mask(CPUSTAT_STOP_INT, li->cpuflags);
	li->action_bits |= action;
	if (waitqueue_active(&li->wq))
		wake_up_interruptible(&li->wq);
out:
	spin_unlock_bh(&li->lock);

	return 0; /* order accepted */
}

static int __sigp_stop(struct kvm_vcpu *vcpu, u16 cpu_addr, int action)
{
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;
	struct kvm_s390_local_interrupt *li;
	int rc;

	if (cpu_addr >= KVM_MAX_VCPUS)
		return 3; /* not operational */

	spin_lock(&fi->lock);
	li = fi->local_int[cpu_addr];
	if (li == NULL) {
		rc = 3; /* not operational */
		goto unlock;
	}

	rc = __inject_sigp_stop(li, action);

unlock:
	spin_unlock(&fi->lock);
	VCPU_EVENT(vcpu, 4, "sent sigp stop to cpu %x", cpu_addr);
	return rc;
}

int kvm_s390_inject_sigp_stop(struct kvm_vcpu *vcpu, int action)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	return __inject_sigp_stop(li, action);
}

static int __sigp_set_arch(struct kvm_vcpu *vcpu, u32 parameter)
{
	int rc;

	switch (parameter & 0xff) {
	case 0:
		rc = 3; /* not operational */
		break;
	case 1:
	case 2:
		rc = 0; /* order accepted */
		break;
	default:
		rc = -EOPNOTSUPP;
	}
	return rc;
}

static int __sigp_set_prefix(struct kvm_vcpu *vcpu, u16 cpu_addr, u32 address,
			     u64 *reg)
{
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;
	struct kvm_s390_local_interrupt *li = NULL;
	struct kvm_s390_interrupt_info *inti;
	int rc;
	u8 tmp;

	/* make sure that the new value is valid memory */
	address = address & 0x7fffe000u;
	if (copy_from_guest_absolute(vcpu, &tmp, address, 1) ||
	   copy_from_guest_absolute(vcpu, &tmp, address + PAGE_SIZE, 1)) {
		*reg |= SIGP_STAT_INVALID_PARAMETER;
		return 1; /* invalid parameter */
	}

	inti = kzalloc(sizeof(*inti), GFP_KERNEL);
	if (!inti)
		return 2; /* busy */

	spin_lock(&fi->lock);
	if (cpu_addr < KVM_MAX_VCPUS)
		li = fi->local_int[cpu_addr];

	if (li == NULL) {
		rc = 1; /* incorrect state */
		*reg &= SIGP_STAT_INCORRECT_STATE;
		kfree(inti);
		goto out_fi;
	}

	spin_lock_bh(&li->lock);
	/* cpu must be in stopped state */
	if (!(atomic_read(li->cpuflags) & CPUSTAT_STOPPED)) {
		rc = 1; /* incorrect state */
		*reg &= SIGP_STAT_INCORRECT_STATE;
		kfree(inti);
		goto out_li;
	}

	inti->type = KVM_S390_SIGP_SET_PREFIX;
	inti->prefix.address = address;

	list_add_tail(&inti->list, &li->list);
	atomic_set(&li->active, 1);
	if (waitqueue_active(&li->wq))
		wake_up_interruptible(&li->wq);
	rc = 0; /* order accepted */

	VCPU_EVENT(vcpu, 4, "set prefix of cpu %02x to %x", cpu_addr, address);
out_li:
	spin_unlock_bh(&li->lock);
out_fi:
	spin_unlock(&fi->lock);
	return rc;
}

static int __sigp_sense_running(struct kvm_vcpu *vcpu, u16 cpu_addr,
				u64 *reg)
{
	int rc;
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;

	if (cpu_addr >= KVM_MAX_VCPUS)
		return 3; /* not operational */

	spin_lock(&fi->lock);
	if (fi->local_int[cpu_addr] == NULL)
		rc = 3; /* not operational */
	else {
		if (atomic_read(fi->local_int[cpu_addr]->cpuflags)
		    & CPUSTAT_RUNNING) {
			/* running */
			rc = 1;
		} else {
			/* not running */
			*reg &= 0xffffffff00000000UL;
			*reg |= SIGP_STAT_NOT_RUNNING;
			rc = 0;
		}
	}
	spin_unlock(&fi->lock);

	VCPU_EVENT(vcpu, 4, "sensed running status of cpu %x rc %x", cpu_addr,
		   rc);

	return rc;
}

static int __sigp_restart(struct kvm_vcpu *vcpu, u16 cpu_addr)
{
	int rc = 0;
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;
	struct kvm_s390_local_interrupt *li;

	if (cpu_addr >= KVM_MAX_VCPUS)
		return 3; /* not operational */

	spin_lock(&fi->lock);
	li = fi->local_int[cpu_addr];
	if (li == NULL) {
		rc = 3; /* not operational */
		goto out;
	}

	spin_lock_bh(&li->lock);
	if (li->action_bits & ACTION_STOP_ON_STOP)
		rc = 2; /* busy */
	else
		VCPU_EVENT(vcpu, 4, "sigp restart %x to handle userspace",
			cpu_addr);
	spin_unlock_bh(&li->lock);
out:
	spin_unlock(&fi->lock);
	return rc;
}

int kvm_s390_handle_sigp(struct kvm_vcpu *vcpu)
{
	int r1 = (vcpu->arch.sie_block->ipa & 0x00f0) >> 4;
	int r3 = vcpu->arch.sie_block->ipa & 0x000f;
	int base2 = vcpu->arch.sie_block->ipb >> 28;
	int disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16);
	u32 parameter;
	u16 cpu_addr = vcpu->run->s.regs.gprs[r3];
	u8 order_code;
	int rc;

	/* sigp in userspace can exit */
	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu,
						   PGM_PRIVILEGED_OPERATION);

	order_code = disp2;
	if (base2)
		order_code += vcpu->run->s.regs.gprs[base2];

	if (r1 % 2)
		parameter = vcpu->run->s.regs.gprs[r1];
	else
		parameter = vcpu->run->s.regs.gprs[r1 + 1];

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
	case SIGP_EMERGENCY:
		vcpu->stat.instruction_sigp_emergency++;
		rc = __sigp_emergency(vcpu, cpu_addr);
		break;
	case SIGP_STOP:
		vcpu->stat.instruction_sigp_stop++;
		rc = __sigp_stop(vcpu, cpu_addr, ACTION_STOP_ON_STOP);
		break;
	case SIGP_STOP_STORE_STATUS:
		vcpu->stat.instruction_sigp_stop++;
		rc = __sigp_stop(vcpu, cpu_addr, ACTION_STORE_ON_STOP |
						 ACTION_STOP_ON_STOP);
		break;
	case SIGP_SET_ARCH:
		vcpu->stat.instruction_sigp_arch++;
		rc = __sigp_set_arch(vcpu, parameter);
		break;
	case SIGP_SET_PREFIX:
		vcpu->stat.instruction_sigp_prefix++;
		rc = __sigp_set_prefix(vcpu, cpu_addr, parameter,
				       &vcpu->run->s.regs.gprs[r1]);
		break;
	case SIGP_SENSE_RUNNING:
		vcpu->stat.instruction_sigp_sense_running++;
		rc = __sigp_sense_running(vcpu, cpu_addr,
					  &vcpu->run->s.regs.gprs[r1]);
		break;
	case SIGP_RESTART:
		vcpu->stat.instruction_sigp_restart++;
		rc = __sigp_restart(vcpu, cpu_addr);
		if (rc == 2) /* busy */
			break;
		/* user space must know about restart */
	default:
		return -EOPNOTSUPP;
	}

	if (rc < 0)
		return rc;

	vcpu->arch.sie_block->gpsw.mask &= ~(3ul << 44);
	vcpu->arch.sie_block->gpsw.mask |= (rc & 3ul) << 44;
	return 0;
}
