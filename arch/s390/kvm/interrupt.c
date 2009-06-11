/*
 * interrupt.c - handling kvm guest interrupts
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 */

#include <asm/lowcore.h>
#include <asm/uaccess.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/kvm_host.h>
#include <linux/signal.h>
#include "kvm-s390.h"
#include "gaccess.h"

static int psw_extint_disabled(struct kvm_vcpu *vcpu)
{
	return !(vcpu->arch.sie_block->gpsw.mask & PSW_MASK_EXT);
}

static int psw_interrupts_disabled(struct kvm_vcpu *vcpu)
{
	if ((vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PER) ||
	    (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_IO) ||
	    (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_EXT))
		return 0;
	return 1;
}

static int __interrupt_is_deliverable(struct kvm_vcpu *vcpu,
				      struct kvm_s390_interrupt_info *inti)
{
	switch (inti->type) {
	case KVM_S390_INT_EMERGENCY:
		if (psw_extint_disabled(vcpu))
			return 0;
		if (vcpu->arch.sie_block->gcr[0] & 0x4000ul)
			return 1;
		return 0;
	case KVM_S390_INT_SERVICE:
		if (psw_extint_disabled(vcpu))
			return 0;
		if (vcpu->arch.sie_block->gcr[0] & 0x200ul)
			return 1;
		return 0;
	case KVM_S390_INT_VIRTIO:
		if (psw_extint_disabled(vcpu))
			return 0;
		if (vcpu->arch.sie_block->gcr[0] & 0x200ul)
			return 1;
		return 0;
	case KVM_S390_PROGRAM_INT:
	case KVM_S390_SIGP_STOP:
	case KVM_S390_SIGP_SET_PREFIX:
	case KVM_S390_RESTART:
		return 1;
	default:
		BUG();
	}
	return 0;
}

static void __set_cpu_idle(struct kvm_vcpu *vcpu)
{
	BUG_ON(vcpu->vcpu_id > KVM_MAX_VCPUS - 1);
	atomic_set_mask(CPUSTAT_WAIT, &vcpu->arch.sie_block->cpuflags);
	set_bit(vcpu->vcpu_id, vcpu->arch.local_int.float_int->idle_mask);
}

static void __unset_cpu_idle(struct kvm_vcpu *vcpu)
{
	BUG_ON(vcpu->vcpu_id > KVM_MAX_VCPUS - 1);
	atomic_clear_mask(CPUSTAT_WAIT, &vcpu->arch.sie_block->cpuflags);
	clear_bit(vcpu->vcpu_id, vcpu->arch.local_int.float_int->idle_mask);
}

static void __reset_intercept_indicators(struct kvm_vcpu *vcpu)
{
	atomic_clear_mask(CPUSTAT_ECALL_PEND |
		CPUSTAT_IO_INT | CPUSTAT_EXT_INT | CPUSTAT_STOP_INT,
		&vcpu->arch.sie_block->cpuflags);
	vcpu->arch.sie_block->lctl = 0x0000;
}

static void __set_cpuflag(struct kvm_vcpu *vcpu, u32 flag)
{
	atomic_set_mask(flag, &vcpu->arch.sie_block->cpuflags);
}

static void __set_intercept_indicator(struct kvm_vcpu *vcpu,
				      struct kvm_s390_interrupt_info *inti)
{
	switch (inti->type) {
	case KVM_S390_INT_EMERGENCY:
	case KVM_S390_INT_SERVICE:
	case KVM_S390_INT_VIRTIO:
		if (psw_extint_disabled(vcpu))
			__set_cpuflag(vcpu, CPUSTAT_EXT_INT);
		else
			vcpu->arch.sie_block->lctl |= LCTL_CR0;
		break;
	case KVM_S390_SIGP_STOP:
		__set_cpuflag(vcpu, CPUSTAT_STOP_INT);
		break;
	default:
		BUG();
	}
}

static void __do_deliver_interrupt(struct kvm_vcpu *vcpu,
				   struct kvm_s390_interrupt_info *inti)
{
	const unsigned short table[] = { 2, 4, 4, 6 };
	int rc, exception = 0;

	switch (inti->type) {
	case KVM_S390_INT_EMERGENCY:
		VCPU_EVENT(vcpu, 4, "%s", "interrupt: sigp emerg");
		vcpu->stat.deliver_emergency_signal++;
		rc = put_guest_u16(vcpu, __LC_EXT_INT_CODE, 0x1201);
		if (rc == -EFAULT)
			exception = 1;

		rc = copy_to_guest(vcpu, __LC_EXT_OLD_PSW,
			 &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
		if (rc == -EFAULT)
			exception = 1;

		rc = copy_from_guest(vcpu, &vcpu->arch.sie_block->gpsw,
			__LC_EXT_NEW_PSW, sizeof(psw_t));
		if (rc == -EFAULT)
			exception = 1;
		break;

	case KVM_S390_INT_SERVICE:
		VCPU_EVENT(vcpu, 4, "interrupt: sclp parm:%x",
			   inti->ext.ext_params);
		vcpu->stat.deliver_service_signal++;
		rc = put_guest_u16(vcpu, __LC_EXT_INT_CODE, 0x2401);
		if (rc == -EFAULT)
			exception = 1;

		rc = copy_to_guest(vcpu, __LC_EXT_OLD_PSW,
			 &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
		if (rc == -EFAULT)
			exception = 1;

		rc = copy_from_guest(vcpu, &vcpu->arch.sie_block->gpsw,
			__LC_EXT_NEW_PSW, sizeof(psw_t));
		if (rc == -EFAULT)
			exception = 1;

		rc = put_guest_u32(vcpu, __LC_EXT_PARAMS, inti->ext.ext_params);
		if (rc == -EFAULT)
			exception = 1;
		break;

	case KVM_S390_INT_VIRTIO:
		VCPU_EVENT(vcpu, 4, "interrupt: virtio parm:%x,parm64:%llx",
			   inti->ext.ext_params, inti->ext.ext_params2);
		vcpu->stat.deliver_virtio_interrupt++;
		rc = put_guest_u16(vcpu, __LC_EXT_INT_CODE, 0x2603);
		if (rc == -EFAULT)
			exception = 1;

		rc = put_guest_u16(vcpu, __LC_CPU_ADDRESS, 0x0d00);
		if (rc == -EFAULT)
			exception = 1;

		rc = copy_to_guest(vcpu, __LC_EXT_OLD_PSW,
			 &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
		if (rc == -EFAULT)
			exception = 1;

		rc = copy_from_guest(vcpu, &vcpu->arch.sie_block->gpsw,
			__LC_EXT_NEW_PSW, sizeof(psw_t));
		if (rc == -EFAULT)
			exception = 1;

		rc = put_guest_u32(vcpu, __LC_EXT_PARAMS, inti->ext.ext_params);
		if (rc == -EFAULT)
			exception = 1;

		rc = put_guest_u64(vcpu, __LC_PFAULT_INTPARM,
			inti->ext.ext_params2);
		if (rc == -EFAULT)
			exception = 1;
		break;

	case KVM_S390_SIGP_STOP:
		VCPU_EVENT(vcpu, 4, "%s", "interrupt: cpu stop");
		vcpu->stat.deliver_stop_signal++;
		__set_intercept_indicator(vcpu, inti);
		break;

	case KVM_S390_SIGP_SET_PREFIX:
		VCPU_EVENT(vcpu, 4, "interrupt: set prefix to %x",
			   inti->prefix.address);
		vcpu->stat.deliver_prefix_signal++;
		vcpu->arch.sie_block->prefix = inti->prefix.address;
		vcpu->arch.sie_block->ihcpu = 0xffff;
		break;

	case KVM_S390_RESTART:
		VCPU_EVENT(vcpu, 4, "%s", "interrupt: cpu restart");
		vcpu->stat.deliver_restart_signal++;
		rc = copy_to_guest(vcpu, offsetof(struct _lowcore,
		  restart_old_psw), &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
		if (rc == -EFAULT)
			exception = 1;

		rc = copy_from_guest(vcpu, &vcpu->arch.sie_block->gpsw,
			offsetof(struct _lowcore, restart_psw), sizeof(psw_t));
		if (rc == -EFAULT)
			exception = 1;
		break;

	case KVM_S390_PROGRAM_INT:
		VCPU_EVENT(vcpu, 4, "interrupt: pgm check code:%x, ilc:%x",
			   inti->pgm.code,
			   table[vcpu->arch.sie_block->ipa >> 14]);
		vcpu->stat.deliver_program_int++;
		rc = put_guest_u16(vcpu, __LC_PGM_INT_CODE, inti->pgm.code);
		if (rc == -EFAULT)
			exception = 1;

		rc = put_guest_u16(vcpu, __LC_PGM_ILC,
			table[vcpu->arch.sie_block->ipa >> 14]);
		if (rc == -EFAULT)
			exception = 1;

		rc = copy_to_guest(vcpu, __LC_PGM_OLD_PSW,
			 &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
		if (rc == -EFAULT)
			exception = 1;

		rc = copy_from_guest(vcpu, &vcpu->arch.sie_block->gpsw,
			__LC_PGM_NEW_PSW, sizeof(psw_t));
		if (rc == -EFAULT)
			exception = 1;
		break;

	default:
		BUG();
	}
	if (exception) {
		printk("kvm: The guest lowcore is not mapped during interrupt "
			"delivery, killing userspace\n");
		do_exit(SIGKILL);
	}
}

static int __try_deliver_ckc_interrupt(struct kvm_vcpu *vcpu)
{
	int rc, exception = 0;

	if (psw_extint_disabled(vcpu))
		return 0;
	if (!(vcpu->arch.sie_block->gcr[0] & 0x800ul))
		return 0;
	rc = put_guest_u16(vcpu, __LC_EXT_INT_CODE, 0x1004);
	if (rc == -EFAULT)
		exception = 1;
	rc = copy_to_guest(vcpu, __LC_EXT_OLD_PSW,
		 &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	if (rc == -EFAULT)
		exception = 1;
	rc = copy_from_guest(vcpu, &vcpu->arch.sie_block->gpsw,
		__LC_EXT_NEW_PSW, sizeof(psw_t));
	if (rc == -EFAULT)
		exception = 1;
	if (exception) {
		printk("kvm: The guest lowcore is not mapped during interrupt "
			"delivery, killing userspace\n");
		do_exit(SIGKILL);
	}
	return 1;
}

int kvm_cpu_has_interrupt(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_float_interrupt *fi = vcpu->arch.local_int.float_int;
	struct kvm_s390_interrupt_info  *inti;
	int rc = 0;

	if (atomic_read(&li->active)) {
		spin_lock_bh(&li->lock);
		list_for_each_entry(inti, &li->list, list)
			if (__interrupt_is_deliverable(vcpu, inti)) {
				rc = 1;
				break;
			}
		spin_unlock_bh(&li->lock);
	}

	if ((!rc) && atomic_read(&fi->active)) {
		spin_lock(&fi->lock);
		list_for_each_entry(inti, &fi->list, list)
			if (__interrupt_is_deliverable(vcpu, inti)) {
				rc = 1;
				break;
			}
		spin_unlock(&fi->lock);
	}

	if ((!rc) && (vcpu->arch.sie_block->ckc <
		get_clock() + vcpu->arch.sie_block->epoch)) {
		if ((!psw_extint_disabled(vcpu)) &&
			(vcpu->arch.sie_block->gcr[0] & 0x800ul))
			rc = 1;
	}

	return rc;
}

int kvm_arch_interrupt_allowed(struct kvm_vcpu *vcpu)
{
	/* do real check here */
	return 1;
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return 0;
}

int kvm_s390_handle_wait(struct kvm_vcpu *vcpu)
{
	u64 now, sltime;
	DECLARE_WAITQUEUE(wait, current);

	vcpu->stat.exit_wait_state++;
	if (kvm_cpu_has_interrupt(vcpu))
		return 0;

	__set_cpu_idle(vcpu);
	spin_lock_bh(&vcpu->arch.local_int.lock);
	vcpu->arch.local_int.timer_due = 0;
	spin_unlock_bh(&vcpu->arch.local_int.lock);

	if (psw_interrupts_disabled(vcpu)) {
		VCPU_EVENT(vcpu, 3, "%s", "disabled wait");
		__unset_cpu_idle(vcpu);
		return -ENOTSUPP; /* disabled wait */
	}

	if (psw_extint_disabled(vcpu) ||
	    (!(vcpu->arch.sie_block->gcr[0] & 0x800ul))) {
		VCPU_EVENT(vcpu, 3, "%s", "enabled wait w/o timer");
		goto no_timer;
	}

	now = get_clock() + vcpu->arch.sie_block->epoch;
	if (vcpu->arch.sie_block->ckc < now) {
		__unset_cpu_idle(vcpu);
		return 0;
	}

	sltime = ((vcpu->arch.sie_block->ckc - now)*125)>>9;

	hrtimer_start(&vcpu->arch.ckc_timer, ktime_set (0, sltime) , HRTIMER_MODE_REL);
	VCPU_EVENT(vcpu, 5, "enabled wait via clock comparator: %llx ns", sltime);
no_timer:
	spin_lock(&vcpu->arch.local_int.float_int->lock);
	spin_lock_bh(&vcpu->arch.local_int.lock);
	add_wait_queue(&vcpu->arch.local_int.wq, &wait);
	while (list_empty(&vcpu->arch.local_int.list) &&
		list_empty(&vcpu->arch.local_int.float_int->list) &&
		(!vcpu->arch.local_int.timer_due) &&
		!signal_pending(current)) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_bh(&vcpu->arch.local_int.lock);
		spin_unlock(&vcpu->arch.local_int.float_int->lock);
		vcpu_put(vcpu);
		schedule();
		vcpu_load(vcpu);
		spin_lock(&vcpu->arch.local_int.float_int->lock);
		spin_lock_bh(&vcpu->arch.local_int.lock);
	}
	__unset_cpu_idle(vcpu);
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&vcpu->wq, &wait);
	spin_unlock_bh(&vcpu->arch.local_int.lock);
	spin_unlock(&vcpu->arch.local_int.float_int->lock);
	hrtimer_try_to_cancel(&vcpu->arch.ckc_timer);
	return 0;
}

void kvm_s390_tasklet(unsigned long parm)
{
	struct kvm_vcpu *vcpu = (struct kvm_vcpu *) parm;

	spin_lock(&vcpu->arch.local_int.lock);
	vcpu->arch.local_int.timer_due = 1;
	if (waitqueue_active(&vcpu->arch.local_int.wq))
		wake_up_interruptible(&vcpu->arch.local_int.wq);
	spin_unlock(&vcpu->arch.local_int.lock);
}

/*
 * low level hrtimer wake routine. Because this runs in hardirq context
 * we schedule a tasklet to do the real work.
 */
enum hrtimer_restart kvm_s390_idle_wakeup(struct hrtimer *timer)
{
	struct kvm_vcpu *vcpu;

	vcpu = container_of(timer, struct kvm_vcpu, arch.ckc_timer);
	tasklet_schedule(&vcpu->arch.tasklet);

	return HRTIMER_NORESTART;
}

void kvm_s390_deliver_pending_interrupts(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_float_interrupt *fi = vcpu->arch.local_int.float_int;
	struct kvm_s390_interrupt_info  *n, *inti = NULL;
	int deliver;

	__reset_intercept_indicators(vcpu);
	if (atomic_read(&li->active)) {
		do {
			deliver = 0;
			spin_lock_bh(&li->lock);
			list_for_each_entry_safe(inti, n, &li->list, list) {
				if (__interrupt_is_deliverable(vcpu, inti)) {
					list_del(&inti->list);
					deliver = 1;
					break;
				}
				__set_intercept_indicator(vcpu, inti);
			}
			if (list_empty(&li->list))
				atomic_set(&li->active, 0);
			spin_unlock_bh(&li->lock);
			if (deliver) {
				__do_deliver_interrupt(vcpu, inti);
				kfree(inti);
			}
		} while (deliver);
	}

	if ((vcpu->arch.sie_block->ckc <
		get_clock() + vcpu->arch.sie_block->epoch))
		__try_deliver_ckc_interrupt(vcpu);

	if (atomic_read(&fi->active)) {
		do {
			deliver = 0;
			spin_lock(&fi->lock);
			list_for_each_entry_safe(inti, n, &fi->list, list) {
				if (__interrupt_is_deliverable(vcpu, inti)) {
					list_del(&inti->list);
					deliver = 1;
					break;
				}
				__set_intercept_indicator(vcpu, inti);
			}
			if (list_empty(&fi->list))
				atomic_set(&fi->active, 0);
			spin_unlock(&fi->lock);
			if (deliver) {
				__do_deliver_interrupt(vcpu, inti);
				kfree(inti);
			}
		} while (deliver);
	}
}

int kvm_s390_inject_program_int(struct kvm_vcpu *vcpu, u16 code)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_interrupt_info *inti;

	inti = kzalloc(sizeof(*inti), GFP_KERNEL);
	if (!inti)
		return -ENOMEM;

	inti->type = KVM_S390_PROGRAM_INT;;
	inti->pgm.code = code;

	VCPU_EVENT(vcpu, 3, "inject: program check %d (from kernel)", code);
	spin_lock_bh(&li->lock);
	list_add(&inti->list, &li->list);
	atomic_set(&li->active, 1);
	BUG_ON(waitqueue_active(&li->wq));
	spin_unlock_bh(&li->lock);
	return 0;
}

int kvm_s390_inject_vm(struct kvm *kvm,
		       struct kvm_s390_interrupt *s390int)
{
	struct kvm_s390_local_interrupt *li;
	struct kvm_s390_float_interrupt *fi;
	struct kvm_s390_interrupt_info *inti;
	int sigcpu;

	inti = kzalloc(sizeof(*inti), GFP_KERNEL);
	if (!inti)
		return -ENOMEM;

	switch (s390int->type) {
	case KVM_S390_INT_VIRTIO:
		VM_EVENT(kvm, 5, "inject: virtio parm:%x,parm64:%llx",
			 s390int->parm, s390int->parm64);
		inti->type = s390int->type;
		inti->ext.ext_params = s390int->parm;
		inti->ext.ext_params2 = s390int->parm64;
		break;
	case KVM_S390_INT_SERVICE:
		VM_EVENT(kvm, 5, "inject: sclp parm:%x", s390int->parm);
		inti->type = s390int->type;
		inti->ext.ext_params = s390int->parm;
		break;
	case KVM_S390_PROGRAM_INT:
	case KVM_S390_SIGP_STOP:
	case KVM_S390_INT_EMERGENCY:
	default:
		kfree(inti);
		return -EINVAL;
	}

	mutex_lock(&kvm->lock);
	fi = &kvm->arch.float_int;
	spin_lock(&fi->lock);
	list_add_tail(&inti->list, &fi->list);
	atomic_set(&fi->active, 1);
	sigcpu = find_first_bit(fi->idle_mask, KVM_MAX_VCPUS);
	if (sigcpu == KVM_MAX_VCPUS) {
		do {
			sigcpu = fi->next_rr_cpu++;
			if (sigcpu == KVM_MAX_VCPUS)
				sigcpu = fi->next_rr_cpu = 0;
		} while (fi->local_int[sigcpu] == NULL);
	}
	li = fi->local_int[sigcpu];
	spin_lock_bh(&li->lock);
	atomic_set_mask(CPUSTAT_EXT_INT, li->cpuflags);
	if (waitqueue_active(&li->wq))
		wake_up_interruptible(&li->wq);
	spin_unlock_bh(&li->lock);
	spin_unlock(&fi->lock);
	mutex_unlock(&kvm->lock);
	return 0;
}

int kvm_s390_inject_vcpu(struct kvm_vcpu *vcpu,
			 struct kvm_s390_interrupt *s390int)
{
	struct kvm_s390_local_interrupt *li;
	struct kvm_s390_interrupt_info *inti;

	inti = kzalloc(sizeof(*inti), GFP_KERNEL);
	if (!inti)
		return -ENOMEM;

	switch (s390int->type) {
	case KVM_S390_PROGRAM_INT:
		if (s390int->parm & 0xffff0000) {
			kfree(inti);
			return -EINVAL;
		}
		inti->type = s390int->type;
		inti->pgm.code = s390int->parm;
		VCPU_EVENT(vcpu, 3, "inject: program check %d (from user)",
			   s390int->parm);
		break;
	case KVM_S390_SIGP_SET_PREFIX:
		inti->prefix.address = s390int->parm;
		inti->type = s390int->type;
		VCPU_EVENT(vcpu, 3, "inject: set prefix to %x (from user)",
			   s390int->parm);
		break;
	case KVM_S390_SIGP_STOP:
	case KVM_S390_RESTART:
	case KVM_S390_INT_EMERGENCY:
		VCPU_EVENT(vcpu, 3, "inject: type %x", s390int->type);
		inti->type = s390int->type;
		break;
	case KVM_S390_INT_VIRTIO:
	case KVM_S390_INT_SERVICE:
	default:
		kfree(inti);
		return -EINVAL;
	}

	mutex_lock(&vcpu->kvm->lock);
	li = &vcpu->arch.local_int;
	spin_lock_bh(&li->lock);
	if (inti->type == KVM_S390_PROGRAM_INT)
		list_add(&inti->list, &li->list);
	else
		list_add_tail(&inti->list, &li->list);
	atomic_set(&li->active, 1);
	if (inti->type == KVM_S390_SIGP_STOP)
		li->action_bits |= ACTION_STOP_ON_STOP;
	atomic_set_mask(CPUSTAT_EXT_INT, li->cpuflags);
	if (waitqueue_active(&li->wq))
		wake_up_interruptible(&vcpu->arch.local_int.wq);
	spin_unlock_bh(&li->lock);
	mutex_unlock(&vcpu->kvm->lock);
	return 0;
}
