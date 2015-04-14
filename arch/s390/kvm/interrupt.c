/*
 * handling kvm guest interrupts
 *
 * Copyright IBM Corp. 2008,2014
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 */

#include <linux/interrupt.h>
#include <linux/kvm_host.h>
#include <linux/hrtimer.h>
#include <linux/mmu_context.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/bitmap.h>
#include <asm/asm-offsets.h>
#include <asm/uaccess.h>
#include <asm/sclp.h>
#include "kvm-s390.h"
#include "gaccess.h"
#include "trace-s390.h"

#define IOINT_SCHID_MASK 0x0000ffff
#define IOINT_SSID_MASK 0x00030000
#define IOINT_CSSID_MASK 0x03fc0000
#define IOINT_AI_MASK 0x04000000
#define PFAULT_INIT 0x0600
#define PFAULT_DONE 0x0680
#define VIRTIO_PARAM 0x0d00

static int is_ioint(u64 type)
{
	return ((type & 0xfffe0000u) != 0xfffe0000u);
}

int psw_extint_disabled(struct kvm_vcpu *vcpu)
{
	return !(vcpu->arch.sie_block->gpsw.mask & PSW_MASK_EXT);
}

static int psw_ioint_disabled(struct kvm_vcpu *vcpu)
{
	return !(vcpu->arch.sie_block->gpsw.mask & PSW_MASK_IO);
}

static int psw_mchk_disabled(struct kvm_vcpu *vcpu)
{
	return !(vcpu->arch.sie_block->gpsw.mask & PSW_MASK_MCHECK);
}

static int psw_interrupts_disabled(struct kvm_vcpu *vcpu)
{
	if ((vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PER) ||
	    (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_IO) ||
	    (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_EXT))
		return 0;
	return 1;
}

static int ckc_interrupts_enabled(struct kvm_vcpu *vcpu)
{
	if (psw_extint_disabled(vcpu) ||
	    !(vcpu->arch.sie_block->gcr[0] & 0x800ul))
		return 0;
	if (guestdbg_enabled(vcpu) && guestdbg_sstep_enabled(vcpu))
		/* No timer interrupts when single stepping */
		return 0;
	return 1;
}

static u64 int_word_to_isc_bits(u32 int_word)
{
	u8 isc = (int_word & 0x38000000) >> 27;

	return (0x80 >> isc) << 24;
}

static int __must_check __interrupt_is_deliverable(struct kvm_vcpu *vcpu,
				      struct kvm_s390_interrupt_info *inti)
{
	switch (inti->type) {
	case KVM_S390_INT_EXTERNAL_CALL:
		if (psw_extint_disabled(vcpu))
			return 0;
		if (vcpu->arch.sie_block->gcr[0] & 0x2000ul)
			return 1;
		return 0;
	case KVM_S390_INT_EMERGENCY:
		if (psw_extint_disabled(vcpu))
			return 0;
		if (vcpu->arch.sie_block->gcr[0] & 0x4000ul)
			return 1;
		return 0;
	case KVM_S390_INT_CLOCK_COMP:
		return ckc_interrupts_enabled(vcpu);
	case KVM_S390_INT_CPU_TIMER:
		if (psw_extint_disabled(vcpu))
			return 0;
		if (vcpu->arch.sie_block->gcr[0] & 0x400ul)
			return 1;
		return 0;
	case KVM_S390_INT_SERVICE:
	case KVM_S390_INT_PFAULT_INIT:
	case KVM_S390_INT_PFAULT_DONE:
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
	case KVM_S390_MCHK:
		if (psw_mchk_disabled(vcpu))
			return 0;
		if (vcpu->arch.sie_block->gcr[14] & inti->mchk.cr14)
			return 1;
		return 0;
	case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
		if (psw_ioint_disabled(vcpu))
			return 0;
		if (vcpu->arch.sie_block->gcr[6] &
		    int_word_to_isc_bits(inti->io.io_int_word))
			return 1;
		return 0;
	default:
		printk(KERN_WARNING "illegal interrupt type %llx\n",
		       inti->type);
		BUG();
	}
	return 0;
}

static inline unsigned long pending_local_irqs(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.local_int.pending_irqs;
}

static unsigned long deliverable_local_irqs(struct kvm_vcpu *vcpu)
{
	unsigned long active_mask = pending_local_irqs(vcpu);

	if (psw_extint_disabled(vcpu))
		active_mask &= ~IRQ_PEND_EXT_MASK;
	if (!(vcpu->arch.sie_block->gcr[0] & 0x2000ul))
		__clear_bit(IRQ_PEND_EXT_EXTERNAL, &active_mask);
	if (!(vcpu->arch.sie_block->gcr[0] & 0x4000ul))
		__clear_bit(IRQ_PEND_EXT_EMERGENCY, &active_mask);
	if (!(vcpu->arch.sie_block->gcr[0] & 0x800ul))
		__clear_bit(IRQ_PEND_EXT_CLOCK_COMP, &active_mask);
	if (!(vcpu->arch.sie_block->gcr[0] & 0x400ul))
		__clear_bit(IRQ_PEND_EXT_CPU_TIMER, &active_mask);
	if (psw_mchk_disabled(vcpu))
		active_mask &= ~IRQ_PEND_MCHK_MASK;

	/*
	 * STOP irqs will never be actively delivered. They are triggered via
	 * intercept requests and cleared when the stop intercept is performed.
	 */
	__clear_bit(IRQ_PEND_SIGP_STOP, &active_mask);

	return active_mask;
}

static void __set_cpu_idle(struct kvm_vcpu *vcpu)
{
	atomic_set_mask(CPUSTAT_WAIT, &vcpu->arch.sie_block->cpuflags);
	set_bit(vcpu->vcpu_id, vcpu->arch.local_int.float_int->idle_mask);
}

static void __unset_cpu_idle(struct kvm_vcpu *vcpu)
{
	atomic_clear_mask(CPUSTAT_WAIT, &vcpu->arch.sie_block->cpuflags);
	clear_bit(vcpu->vcpu_id, vcpu->arch.local_int.float_int->idle_mask);
}

static void __reset_intercept_indicators(struct kvm_vcpu *vcpu)
{
	atomic_clear_mask(CPUSTAT_IO_INT | CPUSTAT_EXT_INT | CPUSTAT_STOP_INT,
			  &vcpu->arch.sie_block->cpuflags);
	vcpu->arch.sie_block->lctl = 0x0000;
	vcpu->arch.sie_block->ictl &= ~(ICTL_LPSW | ICTL_STCTL | ICTL_PINT);

	if (guestdbg_enabled(vcpu)) {
		vcpu->arch.sie_block->lctl |= (LCTL_CR0 | LCTL_CR9 |
					       LCTL_CR10 | LCTL_CR11);
		vcpu->arch.sie_block->ictl |= (ICTL_STCTL | ICTL_PINT);
	}
}

static void __set_cpuflag(struct kvm_vcpu *vcpu, u32 flag)
{
	atomic_set_mask(flag, &vcpu->arch.sie_block->cpuflags);
}

static void set_intercept_indicators_ext(struct kvm_vcpu *vcpu)
{
	if (!(pending_local_irqs(vcpu) & IRQ_PEND_EXT_MASK))
		return;
	if (psw_extint_disabled(vcpu))
		__set_cpuflag(vcpu, CPUSTAT_EXT_INT);
	else
		vcpu->arch.sie_block->lctl |= LCTL_CR0;
}

static void set_intercept_indicators_mchk(struct kvm_vcpu *vcpu)
{
	if (!(pending_local_irqs(vcpu) & IRQ_PEND_MCHK_MASK))
		return;
	if (psw_mchk_disabled(vcpu))
		vcpu->arch.sie_block->ictl |= ICTL_LPSW;
	else
		vcpu->arch.sie_block->lctl |= LCTL_CR14;
}

static void set_intercept_indicators_stop(struct kvm_vcpu *vcpu)
{
	if (kvm_s390_is_stop_irq_pending(vcpu))
		__set_cpuflag(vcpu, CPUSTAT_STOP_INT);
}

/* Set interception request for non-deliverable local interrupts */
static void set_intercept_indicators_local(struct kvm_vcpu *vcpu)
{
	set_intercept_indicators_ext(vcpu);
	set_intercept_indicators_mchk(vcpu);
	set_intercept_indicators_stop(vcpu);
}

static void __set_intercept_indicator(struct kvm_vcpu *vcpu,
				      struct kvm_s390_interrupt_info *inti)
{
	switch (inti->type) {
	case KVM_S390_INT_SERVICE:
	case KVM_S390_INT_PFAULT_DONE:
	case KVM_S390_INT_VIRTIO:
		if (psw_extint_disabled(vcpu))
			__set_cpuflag(vcpu, CPUSTAT_EXT_INT);
		else
			vcpu->arch.sie_block->lctl |= LCTL_CR0;
		break;
	case KVM_S390_MCHK:
		if (psw_mchk_disabled(vcpu))
			vcpu->arch.sie_block->ictl |= ICTL_LPSW;
		else
			vcpu->arch.sie_block->lctl |= LCTL_CR14;
		break;
	case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
		if (psw_ioint_disabled(vcpu))
			__set_cpuflag(vcpu, CPUSTAT_IO_INT);
		else
			vcpu->arch.sie_block->lctl |= LCTL_CR6;
		break;
	default:
		BUG();
	}
}

static u16 get_ilc(struct kvm_vcpu *vcpu)
{
	const unsigned short table[] = { 2, 4, 4, 6 };

	switch (vcpu->arch.sie_block->icptcode) {
	case ICPT_INST:
	case ICPT_INSTPROGI:
	case ICPT_OPEREXC:
	case ICPT_PARTEXEC:
	case ICPT_IOINST:
		/* last instruction only stored for these icptcodes */
		return table[vcpu->arch.sie_block->ipa >> 14];
	case ICPT_PROGI:
		return vcpu->arch.sie_block->pgmilc;
	default:
		return 0;
	}
}

static int __must_check __deliver_cpu_timer(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	int rc;

	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_INT_CPU_TIMER,
					 0, 0);

	rc  = put_guest_lc(vcpu, EXT_IRQ_CPU_TIMER,
			   (u16 *)__LC_EXT_INT_CODE);
	rc |= put_guest_lc(vcpu, 0, (u16 *)__LC_EXT_CPU_ADDR);
	rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	clear_bit(IRQ_PEND_EXT_CPU_TIMER, &li->pending_irqs);
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_ckc(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	int rc;

	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_INT_CLOCK_COMP,
					 0, 0);

	rc  = put_guest_lc(vcpu, EXT_IRQ_CLK_COMP,
			   (u16 __user *)__LC_EXT_INT_CODE);
	rc |= put_guest_lc(vcpu, 0, (u16 *)__LC_EXT_CPU_ADDR);
	rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	clear_bit(IRQ_PEND_EXT_CLOCK_COMP, &li->pending_irqs);
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_pfault_init(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_ext_info ext;
	int rc;

	spin_lock(&li->lock);
	ext = li->irq.ext;
	clear_bit(IRQ_PEND_PFAULT_INIT, &li->pending_irqs);
	li->irq.ext.ext_params2 = 0;
	spin_unlock(&li->lock);

	VCPU_EVENT(vcpu, 4, "interrupt: pfault init parm:%x,parm64:%llx",
		   0, ext.ext_params2);
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id,
					 KVM_S390_INT_PFAULT_INIT,
					 0, ext.ext_params2);

	rc  = put_guest_lc(vcpu, EXT_IRQ_CP_SERVICE, (u16 *) __LC_EXT_INT_CODE);
	rc |= put_guest_lc(vcpu, PFAULT_INIT, (u16 *) __LC_EXT_CPU_ADDR);
	rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= put_guest_lc(vcpu, ext.ext_params2, (u64 *) __LC_EXT_PARAMS2);
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_machine_check(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_mchk_info mchk;
	int rc;

	spin_lock(&li->lock);
	mchk = li->irq.mchk;
	/*
	 * If there was an exigent machine check pending, then any repressible
	 * machine checks that might have been pending are indicated along
	 * with it, so always clear both bits
	 */
	clear_bit(IRQ_PEND_MCHK_EX, &li->pending_irqs);
	clear_bit(IRQ_PEND_MCHK_REP, &li->pending_irqs);
	memset(&li->irq.mchk, 0, sizeof(mchk));
	spin_unlock(&li->lock);

	VCPU_EVENT(vcpu, 4, "interrupt: machine check mcic=%llx",
		   mchk.mcic);
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_MCHK,
					 mchk.cr14, mchk.mcic);

	rc  = kvm_s390_vcpu_store_status(vcpu, KVM_S390_STORE_STATUS_PREFIXED);
	rc |= put_guest_lc(vcpu, mchk.mcic,
			   (u64 __user *) __LC_MCCK_CODE);
	rc |= put_guest_lc(vcpu, mchk.failing_storage_address,
			   (u64 __user *) __LC_MCCK_FAIL_STOR_ADDR);
	rc |= write_guest_lc(vcpu, __LC_PSW_SAVE_AREA,
			     &mchk.fixed_logout, sizeof(mchk.fixed_logout));
	rc |= write_guest_lc(vcpu, __LC_MCK_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_MCK_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_restart(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	int rc;

	VCPU_EVENT(vcpu, 4, "%s", "interrupt: cpu restart");
	vcpu->stat.deliver_restart_signal++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_RESTART, 0, 0);

	rc  = write_guest_lc(vcpu,
			     offsetof(struct _lowcore, restart_old_psw),
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, offsetof(struct _lowcore, restart_psw),
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	clear_bit(IRQ_PEND_RESTART, &li->pending_irqs);
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_set_prefix(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_prefix_info prefix;

	spin_lock(&li->lock);
	prefix = li->irq.prefix;
	li->irq.prefix.address = 0;
	clear_bit(IRQ_PEND_SET_PREFIX, &li->pending_irqs);
	spin_unlock(&li->lock);

	VCPU_EVENT(vcpu, 4, "interrupt: set prefix to %x", prefix.address);
	vcpu->stat.deliver_prefix_signal++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id,
					 KVM_S390_SIGP_SET_PREFIX,
					 prefix.address, 0);

	kvm_s390_set_prefix(vcpu, prefix.address);
	return 0;
}

static int __must_check __deliver_emergency_signal(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	int rc;
	int cpu_addr;

	spin_lock(&li->lock);
	cpu_addr = find_first_bit(li->sigp_emerg_pending, KVM_MAX_VCPUS);
	clear_bit(cpu_addr, li->sigp_emerg_pending);
	if (bitmap_empty(li->sigp_emerg_pending, KVM_MAX_VCPUS))
		clear_bit(IRQ_PEND_EXT_EMERGENCY, &li->pending_irqs);
	spin_unlock(&li->lock);

	VCPU_EVENT(vcpu, 4, "%s", "interrupt: sigp emerg");
	vcpu->stat.deliver_emergency_signal++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_INT_EMERGENCY,
					 cpu_addr, 0);

	rc  = put_guest_lc(vcpu, EXT_IRQ_EMERGENCY_SIG,
			   (u16 *)__LC_EXT_INT_CODE);
	rc |= put_guest_lc(vcpu, cpu_addr, (u16 *)__LC_EXT_CPU_ADDR);
	rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_external_call(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_extcall_info extcall;
	int rc;

	spin_lock(&li->lock);
	extcall = li->irq.extcall;
	li->irq.extcall.code = 0;
	clear_bit(IRQ_PEND_EXT_EXTERNAL, &li->pending_irqs);
	spin_unlock(&li->lock);

	VCPU_EVENT(vcpu, 4, "%s", "interrupt: sigp ext call");
	vcpu->stat.deliver_external_call++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id,
					 KVM_S390_INT_EXTERNAL_CALL,
					 extcall.code, 0);

	rc  = put_guest_lc(vcpu, EXT_IRQ_EXTERNAL_CALL,
			   (u16 *)__LC_EXT_INT_CODE);
	rc |= put_guest_lc(vcpu, extcall.code, (u16 *)__LC_EXT_CPU_ADDR);
	rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW, &vcpu->arch.sie_block->gpsw,
			    sizeof(psw_t));
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_prog(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_pgm_info pgm_info;
	int rc = 0;
	u16 ilc = get_ilc(vcpu);

	spin_lock(&li->lock);
	pgm_info = li->irq.pgm;
	clear_bit(IRQ_PEND_PROG, &li->pending_irqs);
	memset(&li->irq.pgm, 0, sizeof(pgm_info));
	spin_unlock(&li->lock);

	VCPU_EVENT(vcpu, 4, "interrupt: pgm check code:%x, ilc:%x",
		   pgm_info.code, ilc);
	vcpu->stat.deliver_program_int++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_PROGRAM_INT,
					 pgm_info.code, 0);

	switch (pgm_info.code & ~PGM_PER) {
	case PGM_AFX_TRANSLATION:
	case PGM_ASX_TRANSLATION:
	case PGM_EX_TRANSLATION:
	case PGM_LFX_TRANSLATION:
	case PGM_LSTE_SEQUENCE:
	case PGM_LSX_TRANSLATION:
	case PGM_LX_TRANSLATION:
	case PGM_PRIMARY_AUTHORITY:
	case PGM_SECONDARY_AUTHORITY:
	case PGM_SPACE_SWITCH:
		rc = put_guest_lc(vcpu, pgm_info.trans_exc_code,
				  (u64 *)__LC_TRANS_EXC_CODE);
		break;
	case PGM_ALEN_TRANSLATION:
	case PGM_ALE_SEQUENCE:
	case PGM_ASTE_INSTANCE:
	case PGM_ASTE_SEQUENCE:
	case PGM_ASTE_VALIDITY:
	case PGM_EXTENDED_AUTHORITY:
		rc = put_guest_lc(vcpu, pgm_info.exc_access_id,
				  (u8 *)__LC_EXC_ACCESS_ID);
		break;
	case PGM_ASCE_TYPE:
	case PGM_PAGE_TRANSLATION:
	case PGM_REGION_FIRST_TRANS:
	case PGM_REGION_SECOND_TRANS:
	case PGM_REGION_THIRD_TRANS:
	case PGM_SEGMENT_TRANSLATION:
		rc = put_guest_lc(vcpu, pgm_info.trans_exc_code,
				  (u64 *)__LC_TRANS_EXC_CODE);
		rc |= put_guest_lc(vcpu, pgm_info.exc_access_id,
				   (u8 *)__LC_EXC_ACCESS_ID);
		rc |= put_guest_lc(vcpu, pgm_info.op_access_id,
				   (u8 *)__LC_OP_ACCESS_ID);
		break;
	case PGM_MONITOR:
		rc = put_guest_lc(vcpu, pgm_info.mon_class_nr,
				  (u16 *)__LC_MON_CLASS_NR);
		rc |= put_guest_lc(vcpu, pgm_info.mon_code,
				   (u64 *)__LC_MON_CODE);
		break;
	case PGM_DATA:
		rc = put_guest_lc(vcpu, pgm_info.data_exc_code,
				  (u32 *)__LC_DATA_EXC_CODE);
		break;
	case PGM_PROTECTION:
		rc = put_guest_lc(vcpu, pgm_info.trans_exc_code,
				  (u64 *)__LC_TRANS_EXC_CODE);
		rc |= put_guest_lc(vcpu, pgm_info.exc_access_id,
				   (u8 *)__LC_EXC_ACCESS_ID);
		break;
	}

	if (pgm_info.code & PGM_PER) {
		rc |= put_guest_lc(vcpu, pgm_info.per_code,
				   (u8 *) __LC_PER_CODE);
		rc |= put_guest_lc(vcpu, pgm_info.per_atmid,
				   (u8 *)__LC_PER_ATMID);
		rc |= put_guest_lc(vcpu, pgm_info.per_address,
				   (u64 *) __LC_PER_ADDRESS);
		rc |= put_guest_lc(vcpu, pgm_info.per_access_id,
				   (u8 *) __LC_PER_ACCESS_ID);
	}

	rc |= put_guest_lc(vcpu, ilc, (u16 *) __LC_PGM_ILC);
	rc |= put_guest_lc(vcpu, pgm_info.code,
			   (u16 *)__LC_PGM_INT_CODE);
	rc |= write_guest_lc(vcpu, __LC_PGM_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_PGM_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_service(struct kvm_vcpu *vcpu,
					  struct kvm_s390_interrupt_info *inti)
{
	int rc;

	VCPU_EVENT(vcpu, 4, "interrupt: sclp parm:%x",
		   inti->ext.ext_params);
	vcpu->stat.deliver_service_signal++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, inti->type,
					 inti->ext.ext_params, 0);

	rc  = put_guest_lc(vcpu, EXT_IRQ_SERVICE_SIG, (u16 *)__LC_EXT_INT_CODE);
	rc |= put_guest_lc(vcpu, 0, (u16 *)__LC_EXT_CPU_ADDR);
	rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= put_guest_lc(vcpu, inti->ext.ext_params,
			   (u32 *)__LC_EXT_PARAMS);
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_pfault_done(struct kvm_vcpu *vcpu,
					   struct kvm_s390_interrupt_info *inti)
{
	int rc;

	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id,
					 KVM_S390_INT_PFAULT_DONE, 0,
					 inti->ext.ext_params2);

	rc  = put_guest_lc(vcpu, EXT_IRQ_CP_SERVICE, (u16 *)__LC_EXT_INT_CODE);
	rc |= put_guest_lc(vcpu, PFAULT_DONE, (u16 *)__LC_EXT_CPU_ADDR);
	rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= put_guest_lc(vcpu, inti->ext.ext_params2,
			   (u64 *)__LC_EXT_PARAMS2);
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_virtio(struct kvm_vcpu *vcpu,
					 struct kvm_s390_interrupt_info *inti)
{
	int rc;

	VCPU_EVENT(vcpu, 4, "interrupt: virtio parm:%x,parm64:%llx",
		   inti->ext.ext_params, inti->ext.ext_params2);
	vcpu->stat.deliver_virtio_interrupt++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, inti->type,
					 inti->ext.ext_params,
					 inti->ext.ext_params2);

	rc  = put_guest_lc(vcpu, EXT_IRQ_CP_SERVICE, (u16 *)__LC_EXT_INT_CODE);
	rc |= put_guest_lc(vcpu, VIRTIO_PARAM, (u16 *)__LC_EXT_CPU_ADDR);
	rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= put_guest_lc(vcpu, inti->ext.ext_params,
			   (u32 *)__LC_EXT_PARAMS);
	rc |= put_guest_lc(vcpu, inti->ext.ext_params2,
			   (u64 *)__LC_EXT_PARAMS2);
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_io(struct kvm_vcpu *vcpu,
				     struct kvm_s390_interrupt_info *inti)
{
	int rc;

	VCPU_EVENT(vcpu, 4, "interrupt: I/O %llx", inti->type);
	vcpu->stat.deliver_io_int++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, inti->type,
					 ((__u32)inti->io.subchannel_id << 16) |
						inti->io.subchannel_nr,
					 ((__u64)inti->io.io_int_parm << 32) |
						inti->io.io_int_word);

	rc  = put_guest_lc(vcpu, inti->io.subchannel_id,
			   (u16 *)__LC_SUBCHANNEL_ID);
	rc |= put_guest_lc(vcpu, inti->io.subchannel_nr,
			   (u16 *)__LC_SUBCHANNEL_NR);
	rc |= put_guest_lc(vcpu, inti->io.io_int_parm,
			   (u32 *)__LC_IO_INT_PARM);
	rc |= put_guest_lc(vcpu, inti->io.io_int_word,
			   (u32 *)__LC_IO_INT_WORD);
	rc |= write_guest_lc(vcpu, __LC_IO_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_IO_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_mchk_floating(struct kvm_vcpu *vcpu,
					   struct kvm_s390_interrupt_info *inti)
{
	struct kvm_s390_mchk_info *mchk = &inti->mchk;
	int rc;

	VCPU_EVENT(vcpu, 4, "interrupt: machine check mcic=%llx",
		   mchk->mcic);
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_MCHK,
					 mchk->cr14, mchk->mcic);

	rc  = kvm_s390_vcpu_store_status(vcpu, KVM_S390_STORE_STATUS_PREFIXED);
	rc |= put_guest_lc(vcpu, mchk->mcic,
			(u64 __user *) __LC_MCCK_CODE);
	rc |= put_guest_lc(vcpu, mchk->failing_storage_address,
			(u64 __user *) __LC_MCCK_FAIL_STOR_ADDR);
	rc |= write_guest_lc(vcpu, __LC_PSW_SAVE_AREA,
			     &mchk->fixed_logout, sizeof(mchk->fixed_logout));
	rc |= write_guest_lc(vcpu, __LC_MCK_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_MCK_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	return rc ? -EFAULT : 0;
}

typedef int (*deliver_irq_t)(struct kvm_vcpu *vcpu);

static const deliver_irq_t deliver_irq_funcs[] = {
	[IRQ_PEND_MCHK_EX]        = __deliver_machine_check,
	[IRQ_PEND_PROG]           = __deliver_prog,
	[IRQ_PEND_EXT_EMERGENCY]  = __deliver_emergency_signal,
	[IRQ_PEND_EXT_EXTERNAL]   = __deliver_external_call,
	[IRQ_PEND_EXT_CLOCK_COMP] = __deliver_ckc,
	[IRQ_PEND_EXT_CPU_TIMER]  = __deliver_cpu_timer,
	[IRQ_PEND_RESTART]        = __deliver_restart,
	[IRQ_PEND_SET_PREFIX]     = __deliver_set_prefix,
	[IRQ_PEND_PFAULT_INIT]    = __deliver_pfault_init,
};

static int __must_check __deliver_floating_interrupt(struct kvm_vcpu *vcpu,
					   struct kvm_s390_interrupt_info *inti)
{
	int rc;

	switch (inti->type) {
	case KVM_S390_INT_SERVICE:
		rc = __deliver_service(vcpu, inti);
		break;
	case KVM_S390_INT_PFAULT_DONE:
		rc = __deliver_pfault_done(vcpu, inti);
		break;
	case KVM_S390_INT_VIRTIO:
		rc = __deliver_virtio(vcpu, inti);
		break;
	case KVM_S390_MCHK:
		rc = __deliver_mchk_floating(vcpu, inti);
		break;
	case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
		rc = __deliver_io(vcpu, inti);
		break;
	default:
		BUG();
	}

	return rc;
}

/* Check whether an external call is pending (deliverable or not) */
int kvm_s390_ext_call_pending(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	uint8_t sigp_ctrl = vcpu->kvm->arch.sca->cpu[vcpu->vcpu_id].sigp_ctrl;

	if (!sclp_has_sigpif())
		return test_bit(IRQ_PEND_EXT_EXTERNAL, &li->pending_irqs);

	return (sigp_ctrl & SIGP_CTRL_C) &&
	       (atomic_read(&vcpu->arch.sie_block->cpuflags) & CPUSTAT_ECALL_PEND);
}

int kvm_s390_vcpu_has_irq(struct kvm_vcpu *vcpu, int exclude_stop)
{
	struct kvm_s390_float_interrupt *fi = vcpu->arch.local_int.float_int;
	struct kvm_s390_interrupt_info  *inti;
	int rc;

	rc = !!deliverable_local_irqs(vcpu);

	if ((!rc) && atomic_read(&fi->active)) {
		spin_lock(&fi->lock);
		list_for_each_entry(inti, &fi->list, list)
			if (__interrupt_is_deliverable(vcpu, inti)) {
				rc = 1;
				break;
			}
		spin_unlock(&fi->lock);
	}

	if (!rc && kvm_cpu_has_pending_timer(vcpu))
		rc = 1;

	/* external call pending and deliverable */
	if (!rc && kvm_s390_ext_call_pending(vcpu) &&
	    !psw_extint_disabled(vcpu) &&
	    (vcpu->arch.sie_block->gcr[0] & 0x2000ul))
		rc = 1;

	if (!rc && !exclude_stop && kvm_s390_is_stop_irq_pending(vcpu))
		rc = 1;

	return rc;
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	if (!(vcpu->arch.sie_block->ckc <
	      get_tod_clock_fast() + vcpu->arch.sie_block->epoch))
		return 0;
	if (!ckc_interrupts_enabled(vcpu))
		return 0;
	return 1;
}

int kvm_s390_handle_wait(struct kvm_vcpu *vcpu)
{
	u64 now, sltime;

	vcpu->stat.exit_wait_state++;

	/* fast path */
	if (kvm_cpu_has_pending_timer(vcpu) || kvm_arch_vcpu_runnable(vcpu))
		return 0;

	if (psw_interrupts_disabled(vcpu)) {
		VCPU_EVENT(vcpu, 3, "%s", "disabled wait");
		return -EOPNOTSUPP; /* disabled wait */
	}

	if (!ckc_interrupts_enabled(vcpu)) {
		VCPU_EVENT(vcpu, 3, "%s", "enabled wait w/o timer");
		__set_cpu_idle(vcpu);
		goto no_timer;
	}

	now = get_tod_clock_fast() + vcpu->arch.sie_block->epoch;
	sltime = tod_to_ns(vcpu->arch.sie_block->ckc - now);

	/* underflow */
	if (vcpu->arch.sie_block->ckc < now)
		return 0;

	__set_cpu_idle(vcpu);
	hrtimer_start(&vcpu->arch.ckc_timer, ktime_set (0, sltime) , HRTIMER_MODE_REL);
	VCPU_EVENT(vcpu, 5, "enabled wait via clock comparator: %llx ns", sltime);
no_timer:
	srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);
	kvm_vcpu_block(vcpu);
	__unset_cpu_idle(vcpu);
	vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);

	hrtimer_cancel(&vcpu->arch.ckc_timer);
	return 0;
}

void kvm_s390_vcpu_wakeup(struct kvm_vcpu *vcpu)
{
	if (waitqueue_active(&vcpu->wq)) {
		/*
		 * The vcpu gave up the cpu voluntarily, mark it as a good
		 * yield-candidate.
		 */
		vcpu->preempted = true;
		wake_up_interruptible(&vcpu->wq);
		vcpu->stat.halt_wakeup++;
	}
}

enum hrtimer_restart kvm_s390_idle_wakeup(struct hrtimer *timer)
{
	struct kvm_vcpu *vcpu;
	u64 now, sltime;

	vcpu = container_of(timer, struct kvm_vcpu, arch.ckc_timer);
	now = get_tod_clock_fast() + vcpu->arch.sie_block->epoch;
	sltime = tod_to_ns(vcpu->arch.sie_block->ckc - now);

	/*
	 * If the monotonic clock runs faster than the tod clock we might be
	 * woken up too early and have to go back to sleep to avoid deadlocks.
	 */
	if (vcpu->arch.sie_block->ckc > now &&
	    hrtimer_forward_now(timer, ns_to_ktime(sltime)))
		return HRTIMER_RESTART;
	kvm_s390_vcpu_wakeup(vcpu);
	return HRTIMER_NORESTART;
}

void kvm_s390_clear_local_irqs(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	spin_lock(&li->lock);
	li->pending_irqs = 0;
	bitmap_zero(li->sigp_emerg_pending, KVM_MAX_VCPUS);
	memset(&li->irq, 0, sizeof(li->irq));
	spin_unlock(&li->lock);

	/* clear pending external calls set by sigp interpretation facility */
	atomic_clear_mask(CPUSTAT_ECALL_PEND, li->cpuflags);
	vcpu->kvm->arch.sca->cpu[vcpu->vcpu_id].sigp_ctrl = 0;
}

int __must_check kvm_s390_deliver_pending_interrupts(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_float_interrupt *fi = vcpu->arch.local_int.float_int;
	struct kvm_s390_interrupt_info  *n, *inti = NULL;
	deliver_irq_t func;
	int deliver;
	int rc = 0;
	unsigned long irq_type;
	unsigned long deliverable_irqs;

	__reset_intercept_indicators(vcpu);

	/* pending ckc conditions might have been invalidated */
	clear_bit(IRQ_PEND_EXT_CLOCK_COMP, &li->pending_irqs);
	if (kvm_cpu_has_pending_timer(vcpu))
		set_bit(IRQ_PEND_EXT_CLOCK_COMP, &li->pending_irqs);

	do {
		deliverable_irqs = deliverable_local_irqs(vcpu);
		/* bits are in the order of interrupt priority */
		irq_type = find_first_bit(&deliverable_irqs, IRQ_PEND_COUNT);
		if (irq_type == IRQ_PEND_COUNT)
			break;
		func = deliver_irq_funcs[irq_type];
		if (!func) {
			WARN_ON_ONCE(func == NULL);
			clear_bit(irq_type, &li->pending_irqs);
			continue;
		}
		rc = func(vcpu);
	} while (!rc && irq_type != IRQ_PEND_COUNT);

	set_intercept_indicators_local(vcpu);

	if (!rc && atomic_read(&fi->active)) {
		do {
			deliver = 0;
			spin_lock(&fi->lock);
			list_for_each_entry_safe(inti, n, &fi->list, list) {
				if (__interrupt_is_deliverable(vcpu, inti)) {
					list_del(&inti->list);
					fi->irq_count--;
					deliver = 1;
					break;
				}
				__set_intercept_indicator(vcpu, inti);
			}
			if (list_empty(&fi->list))
				atomic_set(&fi->active, 0);
			spin_unlock(&fi->lock);
			if (deliver) {
				rc = __deliver_floating_interrupt(vcpu, inti);
				kfree(inti);
			}
		} while (!rc && deliver);
	}

	return rc;
}

static int __inject_prog(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	li->irq.pgm = irq->u.pgm;
	set_bit(IRQ_PEND_PROG, &li->pending_irqs);
	return 0;
}

int kvm_s390_inject_program_int(struct kvm_vcpu *vcpu, u16 code)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_irq irq;

	VCPU_EVENT(vcpu, 3, "inject: program check %d (from kernel)", code);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_PROGRAM_INT, code,
				   0, 1);
	spin_lock(&li->lock);
	irq.u.pgm.code = code;
	__inject_prog(vcpu, &irq);
	BUG_ON(waitqueue_active(li->wq));
	spin_unlock(&li->lock);
	return 0;
}

int kvm_s390_inject_prog_irq(struct kvm_vcpu *vcpu,
			     struct kvm_s390_pgm_info *pgm_info)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_irq irq;
	int rc;

	VCPU_EVENT(vcpu, 3, "inject: prog irq %d (from kernel)",
		   pgm_info->code);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_PROGRAM_INT,
				   pgm_info->code, 0, 1);
	spin_lock(&li->lock);
	irq.u.pgm = *pgm_info;
	rc = __inject_prog(vcpu, &irq);
	BUG_ON(waitqueue_active(li->wq));
	spin_unlock(&li->lock);
	return rc;
}

static int __inject_pfault_init(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	VCPU_EVENT(vcpu, 3, "inject: external irq params:%x, params2:%llx",
		   irq->u.ext.ext_params, irq->u.ext.ext_params2);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_PFAULT_INIT,
				   irq->u.ext.ext_params,
				   irq->u.ext.ext_params2, 2);

	li->irq.ext = irq->u.ext;
	set_bit(IRQ_PEND_PFAULT_INIT, &li->pending_irqs);
	atomic_set_mask(CPUSTAT_EXT_INT, li->cpuflags);
	return 0;
}

static int __inject_extcall_sigpif(struct kvm_vcpu *vcpu, uint16_t src_id)
{
	unsigned char new_val, old_val;
	uint8_t *sigp_ctrl = &vcpu->kvm->arch.sca->cpu[vcpu->vcpu_id].sigp_ctrl;

	new_val = SIGP_CTRL_C | (src_id & SIGP_CTRL_SCN_MASK);
	old_val = *sigp_ctrl & ~SIGP_CTRL_C;
	if (cmpxchg(sigp_ctrl, old_val, new_val) != old_val) {
		/* another external call is pending */
		return -EBUSY;
	}
	atomic_set_mask(CPUSTAT_ECALL_PEND, &vcpu->arch.sie_block->cpuflags);
	return 0;
}

static int __inject_extcall(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_extcall_info *extcall = &li->irq.extcall;
	uint16_t src_id = irq->u.extcall.code;

	VCPU_EVENT(vcpu, 3, "inject: external call source-cpu:%u",
		   src_id);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_EXTERNAL_CALL,
				   src_id, 0, 2);

	/* sending vcpu invalid */
	if (src_id >= KVM_MAX_VCPUS ||
	    kvm_get_vcpu(vcpu->kvm, src_id) == NULL)
		return -EINVAL;

	if (sclp_has_sigpif())
		return __inject_extcall_sigpif(vcpu, src_id);

	if (!test_and_set_bit(IRQ_PEND_EXT_EXTERNAL, &li->pending_irqs))
		return -EBUSY;
	*extcall = irq->u.extcall;
	atomic_set_mask(CPUSTAT_EXT_INT, li->cpuflags);
	return 0;
}

static int __inject_set_prefix(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_prefix_info *prefix = &li->irq.prefix;

	VCPU_EVENT(vcpu, 3, "inject: set prefix to %x (from user)",
		   irq->u.prefix.address);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_SIGP_SET_PREFIX,
				   irq->u.prefix.address, 0, 2);

	if (!is_vcpu_stopped(vcpu))
		return -EBUSY;

	*prefix = irq->u.prefix;
	set_bit(IRQ_PEND_SET_PREFIX, &li->pending_irqs);
	return 0;
}

#define KVM_S390_STOP_SUPP_FLAGS (KVM_S390_STOP_FLAG_STORE_STATUS)
static int __inject_sigp_stop(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_stop_info *stop = &li->irq.stop;
	int rc = 0;

	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_SIGP_STOP, 0, 0, 2);

	if (irq->u.stop.flags & ~KVM_S390_STOP_SUPP_FLAGS)
		return -EINVAL;

	if (is_vcpu_stopped(vcpu)) {
		if (irq->u.stop.flags & KVM_S390_STOP_FLAG_STORE_STATUS)
			rc = kvm_s390_store_status_unloaded(vcpu,
						KVM_S390_STORE_STATUS_NOADDR);
		return rc;
	}

	if (test_and_set_bit(IRQ_PEND_SIGP_STOP, &li->pending_irqs))
		return -EBUSY;
	stop->flags = irq->u.stop.flags;
	__set_cpuflag(vcpu, CPUSTAT_STOP_INT);
	return 0;
}

static int __inject_sigp_restart(struct kvm_vcpu *vcpu,
				 struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	VCPU_EVENT(vcpu, 3, "inject: restart type %llx", irq->type);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_RESTART, 0, 0, 2);

	set_bit(IRQ_PEND_RESTART, &li->pending_irqs);
	return 0;
}

static int __inject_sigp_emergency(struct kvm_vcpu *vcpu,
				   struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	VCPU_EVENT(vcpu, 3, "inject: emergency %u\n",
		   irq->u.emerg.code);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_EMERGENCY,
				   irq->u.emerg.code, 0, 2);

	set_bit(irq->u.emerg.code, li->sigp_emerg_pending);
	set_bit(IRQ_PEND_EXT_EMERGENCY, &li->pending_irqs);
	atomic_set_mask(CPUSTAT_EXT_INT, li->cpuflags);
	return 0;
}

static int __inject_mchk(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_mchk_info *mchk = &li->irq.mchk;

	VCPU_EVENT(vcpu, 5, "inject: machine check parm64:%llx",
		   irq->u.mchk.mcic);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_MCHK, 0,
				   irq->u.mchk.mcic, 2);

	/*
	 * Because repressible machine checks can be indicated along with
	 * exigent machine checks (PoP, Chapter 11, Interruption action)
	 * we need to combine cr14, mcic and external damage code.
	 * Failing storage address and the logout area should not be or'ed
	 * together, we just indicate the last occurrence of the corresponding
	 * machine check
	 */
	mchk->cr14 |= irq->u.mchk.cr14;
	mchk->mcic |= irq->u.mchk.mcic;
	mchk->ext_damage_code |= irq->u.mchk.ext_damage_code;
	mchk->failing_storage_address = irq->u.mchk.failing_storage_address;
	memcpy(&mchk->fixed_logout, &irq->u.mchk.fixed_logout,
	       sizeof(mchk->fixed_logout));
	if (mchk->mcic & MCHK_EX_MASK)
		set_bit(IRQ_PEND_MCHK_EX, &li->pending_irqs);
	else if (mchk->mcic & MCHK_REP_MASK)
		set_bit(IRQ_PEND_MCHK_REP,  &li->pending_irqs);
	return 0;
}

static int __inject_ckc(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	VCPU_EVENT(vcpu, 3, "inject: type %x", KVM_S390_INT_CLOCK_COMP);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_CLOCK_COMP,
				   0, 0, 2);

	set_bit(IRQ_PEND_EXT_CLOCK_COMP, &li->pending_irqs);
	atomic_set_mask(CPUSTAT_EXT_INT, li->cpuflags);
	return 0;
}

static int __inject_cpu_timer(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	VCPU_EVENT(vcpu, 3, "inject: type %x", KVM_S390_INT_CPU_TIMER);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_CPU_TIMER,
				   0, 0, 2);

	set_bit(IRQ_PEND_EXT_CPU_TIMER, &li->pending_irqs);
	atomic_set_mask(CPUSTAT_EXT_INT, li->cpuflags);
	return 0;
}


struct kvm_s390_interrupt_info *kvm_s390_get_io_int(struct kvm *kvm,
						    u64 cr6, u64 schid)
{
	struct kvm_s390_float_interrupt *fi;
	struct kvm_s390_interrupt_info *inti, *iter;

	if ((!schid && !cr6) || (schid && cr6))
		return NULL;
	fi = &kvm->arch.float_int;
	spin_lock(&fi->lock);
	inti = NULL;
	list_for_each_entry(iter, &fi->list, list) {
		if (!is_ioint(iter->type))
			continue;
		if (cr6 &&
		    ((cr6 & int_word_to_isc_bits(iter->io.io_int_word)) == 0))
			continue;
		if (schid) {
			if (((schid & 0x00000000ffff0000) >> 16) !=
			    iter->io.subchannel_id)
				continue;
			if ((schid & 0x000000000000ffff) !=
			    iter->io.subchannel_nr)
				continue;
		}
		inti = iter;
		break;
	}
	if (inti) {
		list_del_init(&inti->list);
		fi->irq_count--;
	}
	if (list_empty(&fi->list))
		atomic_set(&fi->active, 0);
	spin_unlock(&fi->lock);
	return inti;
}

static int __inject_vm(struct kvm *kvm, struct kvm_s390_interrupt_info *inti)
{
	struct kvm_s390_local_interrupt *li;
	struct kvm_s390_float_interrupt *fi;
	struct kvm_s390_interrupt_info *iter;
	struct kvm_vcpu *dst_vcpu = NULL;
	int sigcpu;
	int rc = 0;

	fi = &kvm->arch.float_int;
	spin_lock(&fi->lock);
	if (fi->irq_count >= KVM_S390_MAX_FLOAT_IRQS) {
		rc = -EINVAL;
		goto unlock_fi;
	}
	fi->irq_count++;
	if (!is_ioint(inti->type)) {
		list_add_tail(&inti->list, &fi->list);
	} else {
		u64 isc_bits = int_word_to_isc_bits(inti->io.io_int_word);

		/* Keep I/O interrupts sorted in isc order. */
		list_for_each_entry(iter, &fi->list, list) {
			if (!is_ioint(iter->type))
				continue;
			if (int_word_to_isc_bits(iter->io.io_int_word)
			    <= isc_bits)
				continue;
			break;
		}
		list_add_tail(&inti->list, &iter->list);
	}
	atomic_set(&fi->active, 1);
	if (atomic_read(&kvm->online_vcpus) == 0)
		goto unlock_fi;
	sigcpu = find_first_bit(fi->idle_mask, KVM_MAX_VCPUS);
	if (sigcpu == KVM_MAX_VCPUS) {
		do {
			sigcpu = fi->next_rr_cpu++;
			if (sigcpu == KVM_MAX_VCPUS)
				sigcpu = fi->next_rr_cpu = 0;
		} while (kvm_get_vcpu(kvm, sigcpu) == NULL);
	}
	dst_vcpu = kvm_get_vcpu(kvm, sigcpu);
	li = &dst_vcpu->arch.local_int;
	spin_lock(&li->lock);
	switch (inti->type) {
	case KVM_S390_MCHK:
		atomic_set_mask(CPUSTAT_STOP_INT, li->cpuflags);
		break;
	case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
		atomic_set_mask(CPUSTAT_IO_INT, li->cpuflags);
		break;
	default:
		atomic_set_mask(CPUSTAT_EXT_INT, li->cpuflags);
		break;
	}
	spin_unlock(&li->lock);
	kvm_s390_vcpu_wakeup(kvm_get_vcpu(kvm, sigcpu));
unlock_fi:
	spin_unlock(&fi->lock);
	return rc;
}

int kvm_s390_inject_vm(struct kvm *kvm,
		       struct kvm_s390_interrupt *s390int)
{
	struct kvm_s390_interrupt_info *inti;
	int rc;

	inti = kzalloc(sizeof(*inti), GFP_KERNEL);
	if (!inti)
		return -ENOMEM;

	inti->type = s390int->type;
	switch (inti->type) {
	case KVM_S390_INT_VIRTIO:
		VM_EVENT(kvm, 5, "inject: virtio parm:%x,parm64:%llx",
			 s390int->parm, s390int->parm64);
		inti->ext.ext_params = s390int->parm;
		inti->ext.ext_params2 = s390int->parm64;
		break;
	case KVM_S390_INT_SERVICE:
		VM_EVENT(kvm, 5, "inject: sclp parm:%x", s390int->parm);
		inti->ext.ext_params = s390int->parm;
		break;
	case KVM_S390_INT_PFAULT_DONE:
		inti->ext.ext_params2 = s390int->parm64;
		break;
	case KVM_S390_MCHK:
		VM_EVENT(kvm, 5, "inject: machine check parm64:%llx",
			 s390int->parm64);
		inti->mchk.cr14 = s390int->parm; /* upper bits are not used */
		inti->mchk.mcic = s390int->parm64;
		break;
	case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
		if (inti->type & IOINT_AI_MASK)
			VM_EVENT(kvm, 5, "%s", "inject: I/O (AI)");
		else
			VM_EVENT(kvm, 5, "inject: I/O css %x ss %x schid %04x",
				 s390int->type & IOINT_CSSID_MASK,
				 s390int->type & IOINT_SSID_MASK,
				 s390int->type & IOINT_SCHID_MASK);
		inti->io.subchannel_id = s390int->parm >> 16;
		inti->io.subchannel_nr = s390int->parm & 0x0000ffffu;
		inti->io.io_int_parm = s390int->parm64 >> 32;
		inti->io.io_int_word = s390int->parm64 & 0x00000000ffffffffull;
		break;
	default:
		kfree(inti);
		return -EINVAL;
	}
	trace_kvm_s390_inject_vm(s390int->type, s390int->parm, s390int->parm64,
				 2);

	rc = __inject_vm(kvm, inti);
	if (rc)
		kfree(inti);
	return rc;
}

void kvm_s390_reinject_io_int(struct kvm *kvm,
			      struct kvm_s390_interrupt_info *inti)
{
	__inject_vm(kvm, inti);
}

int s390int_to_s390irq(struct kvm_s390_interrupt *s390int,
		       struct kvm_s390_irq *irq)
{
	irq->type = s390int->type;
	switch (irq->type) {
	case KVM_S390_PROGRAM_INT:
		if (s390int->parm & 0xffff0000)
			return -EINVAL;
		irq->u.pgm.code = s390int->parm;
		break;
	case KVM_S390_SIGP_SET_PREFIX:
		irq->u.prefix.address = s390int->parm;
		break;
	case KVM_S390_SIGP_STOP:
		irq->u.stop.flags = s390int->parm;
		break;
	case KVM_S390_INT_EXTERNAL_CALL:
		if (s390int->parm & 0xffff0000)
			return -EINVAL;
		irq->u.extcall.code = s390int->parm;
		break;
	case KVM_S390_INT_EMERGENCY:
		if (s390int->parm & 0xffff0000)
			return -EINVAL;
		irq->u.emerg.code = s390int->parm;
		break;
	case KVM_S390_MCHK:
		irq->u.mchk.mcic = s390int->parm64;
		break;
	}
	return 0;
}

int kvm_s390_is_stop_irq_pending(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	return test_bit(IRQ_PEND_SIGP_STOP, &li->pending_irqs);
}

void kvm_s390_clear_stop_irq(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	spin_lock(&li->lock);
	li->irq.stop.flags = 0;
	clear_bit(IRQ_PEND_SIGP_STOP, &li->pending_irqs);
	spin_unlock(&li->lock);
}

int kvm_s390_inject_vcpu(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	int rc;

	spin_lock(&li->lock);
	switch (irq->type) {
	case KVM_S390_PROGRAM_INT:
		VCPU_EVENT(vcpu, 3, "inject: program check %d (from user)",
			   irq->u.pgm.code);
		rc = __inject_prog(vcpu, irq);
		break;
	case KVM_S390_SIGP_SET_PREFIX:
		rc = __inject_set_prefix(vcpu, irq);
		break;
	case KVM_S390_SIGP_STOP:
		rc = __inject_sigp_stop(vcpu, irq);
		break;
	case KVM_S390_RESTART:
		rc = __inject_sigp_restart(vcpu, irq);
		break;
	case KVM_S390_INT_CLOCK_COMP:
		rc = __inject_ckc(vcpu);
		break;
	case KVM_S390_INT_CPU_TIMER:
		rc = __inject_cpu_timer(vcpu);
		break;
	case KVM_S390_INT_EXTERNAL_CALL:
		rc = __inject_extcall(vcpu, irq);
		break;
	case KVM_S390_INT_EMERGENCY:
		rc = __inject_sigp_emergency(vcpu, irq);
		break;
	case KVM_S390_MCHK:
		rc = __inject_mchk(vcpu, irq);
		break;
	case KVM_S390_INT_PFAULT_INIT:
		rc = __inject_pfault_init(vcpu, irq);
		break;
	case KVM_S390_INT_VIRTIO:
	case KVM_S390_INT_SERVICE:
	case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
	default:
		rc = -EINVAL;
	}
	spin_unlock(&li->lock);
	if (!rc)
		kvm_s390_vcpu_wakeup(vcpu);
	return rc;
}

void kvm_s390_clear_float_irqs(struct kvm *kvm)
{
	struct kvm_s390_float_interrupt *fi;
	struct kvm_s390_interrupt_info	*n, *inti = NULL;

	fi = &kvm->arch.float_int;
	spin_lock(&fi->lock);
	list_for_each_entry_safe(inti, n, &fi->list, list) {
		list_del(&inti->list);
		kfree(inti);
	}
	fi->irq_count = 0;
	atomic_set(&fi->active, 0);
	spin_unlock(&fi->lock);
}

static inline int copy_irq_to_user(struct kvm_s390_interrupt_info *inti,
				   u8 *addr)
{
	struct kvm_s390_irq __user *uptr = (struct kvm_s390_irq __user *) addr;
	struct kvm_s390_irq irq = {0};

	irq.type = inti->type;
	switch (inti->type) {
	case KVM_S390_INT_PFAULT_INIT:
	case KVM_S390_INT_PFAULT_DONE:
	case KVM_S390_INT_VIRTIO:
	case KVM_S390_INT_SERVICE:
		irq.u.ext = inti->ext;
		break;
	case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
		irq.u.io = inti->io;
		break;
	case KVM_S390_MCHK:
		irq.u.mchk = inti->mchk;
		break;
	default:
		return -EINVAL;
	}

	if (copy_to_user(uptr, &irq, sizeof(irq)))
		return -EFAULT;

	return 0;
}

static int get_all_floating_irqs(struct kvm *kvm, __u8 *buf, __u64 len)
{
	struct kvm_s390_interrupt_info *inti;
	struct kvm_s390_float_interrupt *fi;
	int ret = 0;
	int n = 0;

	fi = &kvm->arch.float_int;
	spin_lock(&fi->lock);

	list_for_each_entry(inti, &fi->list, list) {
		if (len < sizeof(struct kvm_s390_irq)) {
			/* signal userspace to try again */
			ret = -ENOMEM;
			break;
		}
		ret = copy_irq_to_user(inti, buf);
		if (ret)
			break;
		buf += sizeof(struct kvm_s390_irq);
		len -= sizeof(struct kvm_s390_irq);
		n++;
	}

	spin_unlock(&fi->lock);

	return ret < 0 ? ret : n;
}

static int flic_get_attr(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	int r;

	switch (attr->group) {
	case KVM_DEV_FLIC_GET_ALL_IRQS:
		r = get_all_floating_irqs(dev->kvm, (u8 *) attr->addr,
					  attr->attr);
		break;
	default:
		r = -EINVAL;
	}

	return r;
}

static inline int copy_irq_from_user(struct kvm_s390_interrupt_info *inti,
				     u64 addr)
{
	struct kvm_s390_irq __user *uptr = (struct kvm_s390_irq __user *) addr;
	void *target = NULL;
	void __user *source;
	u64 size;

	if (get_user(inti->type, (u64 __user *)addr))
		return -EFAULT;

	switch (inti->type) {
	case KVM_S390_INT_PFAULT_INIT:
	case KVM_S390_INT_PFAULT_DONE:
	case KVM_S390_INT_VIRTIO:
	case KVM_S390_INT_SERVICE:
		target = (void *) &inti->ext;
		source = &uptr->u.ext;
		size = sizeof(inti->ext);
		break;
	case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
		target = (void *) &inti->io;
		source = &uptr->u.io;
		size = sizeof(inti->io);
		break;
	case KVM_S390_MCHK:
		target = (void *) &inti->mchk;
		source = &uptr->u.mchk;
		size = sizeof(inti->mchk);
		break;
	default:
		return -EINVAL;
	}

	if (copy_from_user(target, source, size))
		return -EFAULT;

	return 0;
}

static int enqueue_floating_irq(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	struct kvm_s390_interrupt_info *inti = NULL;
	int r = 0;
	int len = attr->attr;

	if (len % sizeof(struct kvm_s390_irq) != 0)
		return -EINVAL;
	else if (len > KVM_S390_FLIC_MAX_BUFFER)
		return -EINVAL;

	while (len >= sizeof(struct kvm_s390_irq)) {
		inti = kzalloc(sizeof(*inti), GFP_KERNEL);
		if (!inti)
			return -ENOMEM;

		r = copy_irq_from_user(inti, attr->addr);
		if (r) {
			kfree(inti);
			return r;
		}
		r = __inject_vm(dev->kvm, inti);
		if (r) {
			kfree(inti);
			return r;
		}
		len -= sizeof(struct kvm_s390_irq);
		attr->addr += sizeof(struct kvm_s390_irq);
	}

	return r;
}

static struct s390_io_adapter *get_io_adapter(struct kvm *kvm, unsigned int id)
{
	if (id >= MAX_S390_IO_ADAPTERS)
		return NULL;
	return kvm->arch.adapters[id];
}

static int register_io_adapter(struct kvm_device *dev,
			       struct kvm_device_attr *attr)
{
	struct s390_io_adapter *adapter;
	struct kvm_s390_io_adapter adapter_info;

	if (copy_from_user(&adapter_info,
			   (void __user *)attr->addr, sizeof(adapter_info)))
		return -EFAULT;

	if ((adapter_info.id >= MAX_S390_IO_ADAPTERS) ||
	    (dev->kvm->arch.adapters[adapter_info.id] != NULL))
		return -EINVAL;

	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter)
		return -ENOMEM;

	INIT_LIST_HEAD(&adapter->maps);
	init_rwsem(&adapter->maps_lock);
	atomic_set(&adapter->nr_maps, 0);
	adapter->id = adapter_info.id;
	adapter->isc = adapter_info.isc;
	adapter->maskable = adapter_info.maskable;
	adapter->masked = false;
	adapter->swap = adapter_info.swap;
	dev->kvm->arch.adapters[adapter->id] = adapter;

	return 0;
}

int kvm_s390_mask_adapter(struct kvm *kvm, unsigned int id, bool masked)
{
	int ret;
	struct s390_io_adapter *adapter = get_io_adapter(kvm, id);

	if (!adapter || !adapter->maskable)
		return -EINVAL;
	ret = adapter->masked;
	adapter->masked = masked;
	return ret;
}

static int kvm_s390_adapter_map(struct kvm *kvm, unsigned int id, __u64 addr)
{
	struct s390_io_adapter *adapter = get_io_adapter(kvm, id);
	struct s390_map_info *map;
	int ret;

	if (!adapter || !addr)
		return -EINVAL;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map) {
		ret = -ENOMEM;
		goto out;
	}
	INIT_LIST_HEAD(&map->list);
	map->guest_addr = addr;
	map->addr = gmap_translate(kvm->arch.gmap, addr);
	if (map->addr == -EFAULT) {
		ret = -EFAULT;
		goto out;
	}
	ret = get_user_pages_fast(map->addr, 1, 1, &map->page);
	if (ret < 0)
		goto out;
	BUG_ON(ret != 1);
	down_write(&adapter->maps_lock);
	if (atomic_inc_return(&adapter->nr_maps) < MAX_S390_ADAPTER_MAPS) {
		list_add_tail(&map->list, &adapter->maps);
		ret = 0;
	} else {
		put_page(map->page);
		ret = -EINVAL;
	}
	up_write(&adapter->maps_lock);
out:
	if (ret)
		kfree(map);
	return ret;
}

static int kvm_s390_adapter_unmap(struct kvm *kvm, unsigned int id, __u64 addr)
{
	struct s390_io_adapter *adapter = get_io_adapter(kvm, id);
	struct s390_map_info *map, *tmp;
	int found = 0;

	if (!adapter || !addr)
		return -EINVAL;

	down_write(&adapter->maps_lock);
	list_for_each_entry_safe(map, tmp, &adapter->maps, list) {
		if (map->guest_addr == addr) {
			found = 1;
			atomic_dec(&adapter->nr_maps);
			list_del(&map->list);
			put_page(map->page);
			kfree(map);
			break;
		}
	}
	up_write(&adapter->maps_lock);

	return found ? 0 : -EINVAL;
}

void kvm_s390_destroy_adapters(struct kvm *kvm)
{
	int i;
	struct s390_map_info *map, *tmp;

	for (i = 0; i < MAX_S390_IO_ADAPTERS; i++) {
		if (!kvm->arch.adapters[i])
			continue;
		list_for_each_entry_safe(map, tmp,
					 &kvm->arch.adapters[i]->maps, list) {
			list_del(&map->list);
			put_page(map->page);
			kfree(map);
		}
		kfree(kvm->arch.adapters[i]);
	}
}

static int modify_io_adapter(struct kvm_device *dev,
			     struct kvm_device_attr *attr)
{
	struct kvm_s390_io_adapter_req req;
	struct s390_io_adapter *adapter;
	int ret;

	if (copy_from_user(&req, (void __user *)attr->addr, sizeof(req)))
		return -EFAULT;

	adapter = get_io_adapter(dev->kvm, req.id);
	if (!adapter)
		return -EINVAL;
	switch (req.type) {
	case KVM_S390_IO_ADAPTER_MASK:
		ret = kvm_s390_mask_adapter(dev->kvm, req.id, req.mask);
		if (ret > 0)
			ret = 0;
		break;
	case KVM_S390_IO_ADAPTER_MAP:
		ret = kvm_s390_adapter_map(dev->kvm, req.id, req.addr);
		break;
	case KVM_S390_IO_ADAPTER_UNMAP:
		ret = kvm_s390_adapter_unmap(dev->kvm, req.id, req.addr);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int flic_set_attr(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	int r = 0;
	unsigned int i;
	struct kvm_vcpu *vcpu;

	switch (attr->group) {
	case KVM_DEV_FLIC_ENQUEUE:
		r = enqueue_floating_irq(dev, attr);
		break;
	case KVM_DEV_FLIC_CLEAR_IRQS:
		kvm_s390_clear_float_irqs(dev->kvm);
		break;
	case KVM_DEV_FLIC_APF_ENABLE:
		dev->kvm->arch.gmap->pfault_enabled = 1;
		break;
	case KVM_DEV_FLIC_APF_DISABLE_WAIT:
		dev->kvm->arch.gmap->pfault_enabled = 0;
		/*
		 * Make sure no async faults are in transition when
		 * clearing the queues. So we don't need to worry
		 * about late coming workers.
		 */
		synchronize_srcu(&dev->kvm->srcu);
		kvm_for_each_vcpu(i, vcpu, dev->kvm)
			kvm_clear_async_pf_completion_queue(vcpu);
		break;
	case KVM_DEV_FLIC_ADAPTER_REGISTER:
		r = register_io_adapter(dev, attr);
		break;
	case KVM_DEV_FLIC_ADAPTER_MODIFY:
		r = modify_io_adapter(dev, attr);
		break;
	default:
		r = -EINVAL;
	}

	return r;
}

static int flic_create(struct kvm_device *dev, u32 type)
{
	if (!dev)
		return -EINVAL;
	if (dev->kvm->arch.flic)
		return -EINVAL;
	dev->kvm->arch.flic = dev;
	return 0;
}

static void flic_destroy(struct kvm_device *dev)
{
	dev->kvm->arch.flic = NULL;
	kfree(dev);
}

/* s390 floating irq controller (flic) */
struct kvm_device_ops kvm_flic_ops = {
	.name = "kvm-flic",
	.get_attr = flic_get_attr,
	.set_attr = flic_set_attr,
	.create = flic_create,
	.destroy = flic_destroy,
};

static unsigned long get_ind_bit(__u64 addr, unsigned long bit_nr, bool swap)
{
	unsigned long bit;

	bit = bit_nr + (addr % PAGE_SIZE) * 8;

	return swap ? (bit ^ (BITS_PER_LONG - 1)) : bit;
}

static struct s390_map_info *get_map_info(struct s390_io_adapter *adapter,
					  u64 addr)
{
	struct s390_map_info *map;

	if (!adapter)
		return NULL;

	list_for_each_entry(map, &adapter->maps, list) {
		if (map->guest_addr == addr)
			return map;
	}
	return NULL;
}

static int adapter_indicators_set(struct kvm *kvm,
				  struct s390_io_adapter *adapter,
				  struct kvm_s390_adapter_int *adapter_int)
{
	unsigned long bit;
	int summary_set, idx;
	struct s390_map_info *info;
	void *map;

	info = get_map_info(adapter, adapter_int->ind_addr);
	if (!info)
		return -1;
	map = page_address(info->page);
	bit = get_ind_bit(info->addr, adapter_int->ind_offset, adapter->swap);
	set_bit(bit, map);
	idx = srcu_read_lock(&kvm->srcu);
	mark_page_dirty(kvm, info->guest_addr >> PAGE_SHIFT);
	set_page_dirty_lock(info->page);
	info = get_map_info(adapter, adapter_int->summary_addr);
	if (!info) {
		srcu_read_unlock(&kvm->srcu, idx);
		return -1;
	}
	map = page_address(info->page);
	bit = get_ind_bit(info->addr, adapter_int->summary_offset,
			  adapter->swap);
	summary_set = test_and_set_bit(bit, map);
	mark_page_dirty(kvm, info->guest_addr >> PAGE_SHIFT);
	set_page_dirty_lock(info->page);
	srcu_read_unlock(&kvm->srcu, idx);
	return summary_set ? 0 : 1;
}

/*
 * < 0 - not injected due to error
 * = 0 - coalesced, summary indicator already active
 * > 0 - injected interrupt
 */
static int set_adapter_int(struct kvm_kernel_irq_routing_entry *e,
			   struct kvm *kvm, int irq_source_id, int level,
			   bool line_status)
{
	int ret;
	struct s390_io_adapter *adapter;

	/* We're only interested in the 0->1 transition. */
	if (!level)
		return 0;
	adapter = get_io_adapter(kvm, e->adapter.adapter_id);
	if (!adapter)
		return -1;
	down_read(&adapter->maps_lock);
	ret = adapter_indicators_set(kvm, adapter, &e->adapter);
	up_read(&adapter->maps_lock);
	if ((ret > 0) && !adapter->masked) {
		struct kvm_s390_interrupt s390int = {
			.type = KVM_S390_INT_IO(1, 0, 0, 0),
			.parm = 0,
			.parm64 = (adapter->isc << 27) | 0x80000000,
		};
		ret = kvm_s390_inject_vm(kvm, &s390int);
		if (ret == 0)
			ret = 1;
	}
	return ret;
}

int kvm_set_routing_entry(struct kvm_kernel_irq_routing_entry *e,
			  const struct kvm_irq_routing_entry *ue)
{
	int ret;

	switch (ue->type) {
	case KVM_IRQ_ROUTING_S390_ADAPTER:
		e->set = set_adapter_int;
		e->adapter.summary_addr = ue->u.adapter.summary_addr;
		e->adapter.ind_addr = ue->u.adapter.ind_addr;
		e->adapter.summary_offset = ue->u.adapter.summary_offset;
		e->adapter.ind_offset = ue->u.adapter.ind_offset;
		e->adapter.adapter_id = ue->u.adapter.adapter_id;
		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

int kvm_set_msi(struct kvm_kernel_irq_routing_entry *e, struct kvm *kvm,
		int irq_source_id, int level, bool line_status)
{
	return -EINVAL;
}
