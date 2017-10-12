/*
 * handling kvm guest interrupts
 *
 * Copyright IBM Corp. 2008, 2015
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
#include <linux/vmalloc.h>
#include <asm/asm-offsets.h>
#include <asm/dis.h>
#include <linux/uaccess.h>
#include <asm/sclp.h>
#include <asm/isc.h>
#include <asm/gmap.h>
#include <asm/switch_to.h>
#include <asm/nmi.h>
#include "kvm-s390.h"
#include "gaccess.h"
#include "trace-s390.h"

#define PFAULT_INIT 0x0600
#define PFAULT_DONE 0x0680
#define VIRTIO_PARAM 0x0d00

/* handle external calls via sigp interpretation facility */
static int sca_ext_call_pending(struct kvm_vcpu *vcpu, int *src_id)
{
	int c, scn;

	if (!(atomic_read(&vcpu->arch.sie_block->cpuflags) & CPUSTAT_ECALL_PEND))
		return 0;

	BUG_ON(!kvm_s390_use_sca_entries());
	read_lock(&vcpu->kvm->arch.sca_lock);
	if (vcpu->kvm->arch.use_esca) {
		struct esca_block *sca = vcpu->kvm->arch.sca;
		union esca_sigp_ctrl sigp_ctrl =
			sca->cpu[vcpu->vcpu_id].sigp_ctrl;

		c = sigp_ctrl.c;
		scn = sigp_ctrl.scn;
	} else {
		struct bsca_block *sca = vcpu->kvm->arch.sca;
		union bsca_sigp_ctrl sigp_ctrl =
			sca->cpu[vcpu->vcpu_id].sigp_ctrl;

		c = sigp_ctrl.c;
		scn = sigp_ctrl.scn;
	}
	read_unlock(&vcpu->kvm->arch.sca_lock);

	if (src_id)
		*src_id = scn;

	return c;
}

static int sca_inject_ext_call(struct kvm_vcpu *vcpu, int src_id)
{
	int expect, rc;

	BUG_ON(!kvm_s390_use_sca_entries());
	read_lock(&vcpu->kvm->arch.sca_lock);
	if (vcpu->kvm->arch.use_esca) {
		struct esca_block *sca = vcpu->kvm->arch.sca;
		union esca_sigp_ctrl *sigp_ctrl =
			&(sca->cpu[vcpu->vcpu_id].sigp_ctrl);
		union esca_sigp_ctrl new_val = {0}, old_val = *sigp_ctrl;

		new_val.scn = src_id;
		new_val.c = 1;
		old_val.c = 0;

		expect = old_val.value;
		rc = cmpxchg(&sigp_ctrl->value, old_val.value, new_val.value);
	} else {
		struct bsca_block *sca = vcpu->kvm->arch.sca;
		union bsca_sigp_ctrl *sigp_ctrl =
			&(sca->cpu[vcpu->vcpu_id].sigp_ctrl);
		union bsca_sigp_ctrl new_val = {0}, old_val = *sigp_ctrl;

		new_val.scn = src_id;
		new_val.c = 1;
		old_val.c = 0;

		expect = old_val.value;
		rc = cmpxchg(&sigp_ctrl->value, old_val.value, new_val.value);
	}
	read_unlock(&vcpu->kvm->arch.sca_lock);

	if (rc != expect) {
		/* another external call is pending */
		return -EBUSY;
	}
	atomic_or(CPUSTAT_ECALL_PEND, &vcpu->arch.sie_block->cpuflags);
	return 0;
}

static void sca_clear_ext_call(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	int rc, expect;

	if (!kvm_s390_use_sca_entries())
		return;
	atomic_andnot(CPUSTAT_ECALL_PEND, li->cpuflags);
	read_lock(&vcpu->kvm->arch.sca_lock);
	if (vcpu->kvm->arch.use_esca) {
		struct esca_block *sca = vcpu->kvm->arch.sca;
		union esca_sigp_ctrl *sigp_ctrl =
			&(sca->cpu[vcpu->vcpu_id].sigp_ctrl);
		union esca_sigp_ctrl old = *sigp_ctrl;

		expect = old.value;
		rc = cmpxchg(&sigp_ctrl->value, old.value, 0);
	} else {
		struct bsca_block *sca = vcpu->kvm->arch.sca;
		union bsca_sigp_ctrl *sigp_ctrl =
			&(sca->cpu[vcpu->vcpu_id].sigp_ctrl);
		union bsca_sigp_ctrl old = *sigp_ctrl;

		expect = old.value;
		rc = cmpxchg(&sigp_ctrl->value, old.value, 0);
	}
	read_unlock(&vcpu->kvm->arch.sca_lock);
	WARN_ON(rc != expect); /* cannot clear? */
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
	return psw_extint_disabled(vcpu) &&
	       psw_ioint_disabled(vcpu) &&
	       psw_mchk_disabled(vcpu);
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

static int ckc_irq_pending(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.sie_block->ckc >= kvm_s390_get_tod_clock_fast(vcpu->kvm))
		return 0;
	return ckc_interrupts_enabled(vcpu);
}

static int cpu_timer_interrupts_enabled(struct kvm_vcpu *vcpu)
{
	return !psw_extint_disabled(vcpu) &&
	       (vcpu->arch.sie_block->gcr[0] & 0x400ul);
}

static int cpu_timer_irq_pending(struct kvm_vcpu *vcpu)
{
	if (!cpu_timer_interrupts_enabled(vcpu))
		return 0;
	return kvm_s390_get_cpu_timer(vcpu) >> 63;
}

static inline int is_ioirq(unsigned long irq_type)
{
	return ((irq_type >= IRQ_PEND_IO_ISC_0) &&
		(irq_type <= IRQ_PEND_IO_ISC_7));
}

static uint64_t isc_to_isc_bits(int isc)
{
	return (0x80 >> isc) << 24;
}

static inline u8 int_word_to_isc(u32 int_word)
{
	return (int_word & 0x38000000) >> 27;
}

static inline unsigned long pending_irqs(struct kvm_vcpu *vcpu)
{
	return vcpu->kvm->arch.float_int.pending_irqs |
	       vcpu->arch.local_int.pending_irqs;
}

static unsigned long disable_iscs(struct kvm_vcpu *vcpu,
				   unsigned long active_mask)
{
	int i;

	for (i = 0; i <= MAX_ISC; i++)
		if (!(vcpu->arch.sie_block->gcr[6] & isc_to_isc_bits(i)))
			active_mask &= ~(1UL << (IRQ_PEND_IO_ISC_0 + i));

	return active_mask;
}

static unsigned long deliverable_irqs(struct kvm_vcpu *vcpu)
{
	unsigned long active_mask;

	active_mask = pending_irqs(vcpu);
	if (!active_mask)
		return 0;

	if (psw_extint_disabled(vcpu))
		active_mask &= ~IRQ_PEND_EXT_MASK;
	if (psw_ioint_disabled(vcpu))
		active_mask &= ~IRQ_PEND_IO_MASK;
	else
		active_mask = disable_iscs(vcpu, active_mask);
	if (!(vcpu->arch.sie_block->gcr[0] & 0x2000ul))
		__clear_bit(IRQ_PEND_EXT_EXTERNAL, &active_mask);
	if (!(vcpu->arch.sie_block->gcr[0] & 0x4000ul))
		__clear_bit(IRQ_PEND_EXT_EMERGENCY, &active_mask);
	if (!(vcpu->arch.sie_block->gcr[0] & 0x800ul))
		__clear_bit(IRQ_PEND_EXT_CLOCK_COMP, &active_mask);
	if (!(vcpu->arch.sie_block->gcr[0] & 0x400ul))
		__clear_bit(IRQ_PEND_EXT_CPU_TIMER, &active_mask);
	if (!(vcpu->arch.sie_block->gcr[0] & 0x200ul))
		__clear_bit(IRQ_PEND_EXT_SERVICE, &active_mask);
	if (psw_mchk_disabled(vcpu))
		active_mask &= ~IRQ_PEND_MCHK_MASK;
	/*
	 * Check both floating and local interrupt's cr14 because
	 * bit IRQ_PEND_MCHK_REP could be set in both cases.
	 */
	if (!(vcpu->arch.sie_block->gcr[14] &
	   (vcpu->kvm->arch.float_int.mchk.cr14 |
	   vcpu->arch.local_int.irq.mchk.cr14)))
		__clear_bit(IRQ_PEND_MCHK_REP, &active_mask);

	/*
	 * STOP irqs will never be actively delivered. They are triggered via
	 * intercept requests and cleared when the stop intercept is performed.
	 */
	__clear_bit(IRQ_PEND_SIGP_STOP, &active_mask);

	return active_mask;
}

static void __set_cpu_idle(struct kvm_vcpu *vcpu)
{
	atomic_or(CPUSTAT_WAIT, &vcpu->arch.sie_block->cpuflags);
	set_bit(vcpu->vcpu_id, vcpu->arch.local_int.float_int->idle_mask);
}

static void __unset_cpu_idle(struct kvm_vcpu *vcpu)
{
	atomic_andnot(CPUSTAT_WAIT, &vcpu->arch.sie_block->cpuflags);
	clear_bit(vcpu->vcpu_id, vcpu->arch.local_int.float_int->idle_mask);
}

static void __reset_intercept_indicators(struct kvm_vcpu *vcpu)
{
	atomic_andnot(CPUSTAT_IO_INT | CPUSTAT_EXT_INT | CPUSTAT_STOP_INT,
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
	atomic_or(flag, &vcpu->arch.sie_block->cpuflags);
}

static void set_intercept_indicators_io(struct kvm_vcpu *vcpu)
{
	if (!(pending_irqs(vcpu) & IRQ_PEND_IO_MASK))
		return;
	else if (psw_ioint_disabled(vcpu))
		__set_cpuflag(vcpu, CPUSTAT_IO_INT);
	else
		vcpu->arch.sie_block->lctl |= LCTL_CR6;
}

static void set_intercept_indicators_ext(struct kvm_vcpu *vcpu)
{
	if (!(pending_irqs(vcpu) & IRQ_PEND_EXT_MASK))
		return;
	if (psw_extint_disabled(vcpu))
		__set_cpuflag(vcpu, CPUSTAT_EXT_INT);
	else
		vcpu->arch.sie_block->lctl |= LCTL_CR0;
}

static void set_intercept_indicators_mchk(struct kvm_vcpu *vcpu)
{
	if (!(pending_irqs(vcpu) & IRQ_PEND_MCHK_MASK))
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

/* Set interception request for non-deliverable interrupts */
static void set_intercept_indicators(struct kvm_vcpu *vcpu)
{
	set_intercept_indicators_io(vcpu);
	set_intercept_indicators_ext(vcpu);
	set_intercept_indicators_mchk(vcpu);
	set_intercept_indicators_stop(vcpu);
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

	VCPU_EVENT(vcpu, 4, "deliver: pfault init token 0x%llx",
		   ext.ext_params2);
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

static int __write_machine_check(struct kvm_vcpu *vcpu,
				 struct kvm_s390_mchk_info *mchk)
{
	unsigned long ext_sa_addr;
	unsigned long lc;
	freg_t fprs[NUM_FPRS];
	union mci mci;
	int rc;

	mci.val = mchk->mcic;
	/* take care of lazy register loading */
	save_fpu_regs();
	save_access_regs(vcpu->run->s.regs.acrs);
	if (MACHINE_HAS_GS && vcpu->arch.gs_enabled)
		save_gs_cb(current->thread.gs_cb);

	/* Extended save area */
	rc = read_guest_lc(vcpu, __LC_MCESAD, &ext_sa_addr,
			   sizeof(unsigned long));
	/* Only bits 0 through 63-LC are used for address formation */
	lc = ext_sa_addr & MCESA_LC_MASK;
	if (test_kvm_facility(vcpu->kvm, 133)) {
		switch (lc) {
		case 0:
		case 10:
			ext_sa_addr &= ~0x3ffUL;
			break;
		case 11:
			ext_sa_addr &= ~0x7ffUL;
			break;
		case 12:
			ext_sa_addr &= ~0xfffUL;
			break;
		default:
			ext_sa_addr = 0;
			break;
		}
	} else {
		ext_sa_addr &= ~0x3ffUL;
	}

	if (!rc && mci.vr && ext_sa_addr && test_kvm_facility(vcpu->kvm, 129)) {
		if (write_guest_abs(vcpu, ext_sa_addr, vcpu->run->s.regs.vrs,
				    512))
			mci.vr = 0;
	} else {
		mci.vr = 0;
	}
	if (!rc && mci.gs && ext_sa_addr && test_kvm_facility(vcpu->kvm, 133)
	    && (lc == 11 || lc == 12)) {
		if (write_guest_abs(vcpu, ext_sa_addr + 1024,
				    &vcpu->run->s.regs.gscb, 32))
			mci.gs = 0;
	} else {
		mci.gs = 0;
	}

	/* General interruption information */
	rc |= put_guest_lc(vcpu, 1, (u8 __user *) __LC_AR_MODE_ID);
	rc |= write_guest_lc(vcpu, __LC_MCK_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_MCK_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= put_guest_lc(vcpu, mci.val, (u64 __user *) __LC_MCCK_CODE);

	/* Register-save areas */
	if (MACHINE_HAS_VX) {
		convert_vx_to_fp(fprs, (__vector128 *) vcpu->run->s.regs.vrs);
		rc |= write_guest_lc(vcpu, __LC_FPREGS_SAVE_AREA, fprs, 128);
	} else {
		rc |= write_guest_lc(vcpu, __LC_FPREGS_SAVE_AREA,
				     vcpu->run->s.regs.fprs, 128);
	}
	rc |= write_guest_lc(vcpu, __LC_GPREGS_SAVE_AREA,
			     vcpu->run->s.regs.gprs, 128);
	rc |= put_guest_lc(vcpu, current->thread.fpu.fpc,
			   (u32 __user *) __LC_FP_CREG_SAVE_AREA);
	rc |= put_guest_lc(vcpu, vcpu->arch.sie_block->todpr,
			   (u32 __user *) __LC_TOD_PROGREG_SAVE_AREA);
	rc |= put_guest_lc(vcpu, kvm_s390_get_cpu_timer(vcpu),
			   (u64 __user *) __LC_CPU_TIMER_SAVE_AREA);
	rc |= put_guest_lc(vcpu, vcpu->arch.sie_block->ckc >> 8,
			   (u64 __user *) __LC_CLOCK_COMP_SAVE_AREA);
	rc |= write_guest_lc(vcpu, __LC_AREGS_SAVE_AREA,
			     &vcpu->run->s.regs.acrs, 64);
	rc |= write_guest_lc(vcpu, __LC_CREGS_SAVE_AREA,
			     &vcpu->arch.sie_block->gcr, 128);

	/* Extended interruption information */
	rc |= put_guest_lc(vcpu, mchk->ext_damage_code,
			   (u32 __user *) __LC_EXT_DAMAGE_CODE);
	rc |= put_guest_lc(vcpu, mchk->failing_storage_address,
			   (u64 __user *) __LC_MCCK_FAIL_STOR_ADDR);
	rc |= write_guest_lc(vcpu, __LC_PSW_SAVE_AREA, &mchk->fixed_logout,
			     sizeof(mchk->fixed_logout));
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_machine_check(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_mchk_info mchk = {};
	int deliver = 0;
	int rc = 0;

	spin_lock(&fi->lock);
	spin_lock(&li->lock);
	if (test_bit(IRQ_PEND_MCHK_EX, &li->pending_irqs) ||
	    test_bit(IRQ_PEND_MCHK_REP, &li->pending_irqs)) {
		/*
		 * If there was an exigent machine check pending, then any
		 * repressible machine checks that might have been pending
		 * are indicated along with it, so always clear bits for
		 * repressible and exigent interrupts
		 */
		mchk = li->irq.mchk;
		clear_bit(IRQ_PEND_MCHK_EX, &li->pending_irqs);
		clear_bit(IRQ_PEND_MCHK_REP, &li->pending_irqs);
		memset(&li->irq.mchk, 0, sizeof(mchk));
		deliver = 1;
	}
	/*
	 * We indicate floating repressible conditions along with
	 * other pending conditions. Channel Report Pending and Channel
	 * Subsystem damage are the only two and and are indicated by
	 * bits in mcic and masked in cr14.
	 */
	if (test_and_clear_bit(IRQ_PEND_MCHK_REP, &fi->pending_irqs)) {
		mchk.mcic |= fi->mchk.mcic;
		mchk.cr14 |= fi->mchk.cr14;
		memset(&fi->mchk, 0, sizeof(mchk));
		deliver = 1;
	}
	spin_unlock(&li->lock);
	spin_unlock(&fi->lock);

	if (deliver) {
		VCPU_EVENT(vcpu, 3, "deliver: machine check mcic 0x%llx",
			   mchk.mcic);
		trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id,
						 KVM_S390_MCHK,
						 mchk.cr14, mchk.mcic);
		rc = __write_machine_check(vcpu, &mchk);
	}
	return rc;
}

static int __must_check __deliver_restart(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	int rc;

	VCPU_EVENT(vcpu, 3, "%s", "deliver: cpu restart");
	vcpu->stat.deliver_restart_signal++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_RESTART, 0, 0);

	rc  = write_guest_lc(vcpu,
			     offsetof(struct lowcore, restart_old_psw),
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, offsetof(struct lowcore, restart_psw),
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

	VCPU_EVENT(vcpu, 4, "%s", "deliver: sigp emerg");
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

	VCPU_EVENT(vcpu, 4, "%s", "deliver: sigp ext call");
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
	int rc = 0, nullifying = false;
	u16 ilen;

	spin_lock(&li->lock);
	pgm_info = li->irq.pgm;
	clear_bit(IRQ_PEND_PROG, &li->pending_irqs);
	memset(&li->irq.pgm, 0, sizeof(pgm_info));
	spin_unlock(&li->lock);

	ilen = pgm_info.flags & KVM_S390_PGM_FLAGS_ILC_MASK;
	VCPU_EVENT(vcpu, 3, "deliver: program irq code 0x%x, ilen:%d",
		   pgm_info.code, ilen);
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
		nullifying = true;
		/* fall through */
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
		nullifying = true;
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
		nullifying = true;
		break;
	case PGM_MONITOR:
		rc = put_guest_lc(vcpu, pgm_info.mon_class_nr,
				  (u16 *)__LC_MON_CLASS_NR);
		rc |= put_guest_lc(vcpu, pgm_info.mon_code,
				   (u64 *)__LC_MON_CODE);
		break;
	case PGM_VECTOR_PROCESSING:
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
	case PGM_STACK_FULL:
	case PGM_STACK_EMPTY:
	case PGM_STACK_SPECIFICATION:
	case PGM_STACK_TYPE:
	case PGM_STACK_OPERATION:
	case PGM_TRACE_TABEL:
	case PGM_CRYPTO_OPERATION:
		nullifying = true;
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

	if (nullifying && !(pgm_info.flags & KVM_S390_PGM_FLAGS_NO_REWIND))
		kvm_s390_rewind_psw(vcpu, ilen);

	/* bit 1+2 of the target are the ilc, so we can directly use ilen */
	rc |= put_guest_lc(vcpu, ilen, (u16 *) __LC_PGM_ILC);
	rc |= put_guest_lc(vcpu, vcpu->arch.sie_block->gbea,
				 (u64 *) __LC_LAST_BREAK);
	rc |= put_guest_lc(vcpu, pgm_info.code,
			   (u16 *)__LC_PGM_INT_CODE);
	rc |= write_guest_lc(vcpu, __LC_PGM_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_PGM_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_service(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;
	struct kvm_s390_ext_info ext;
	int rc = 0;

	spin_lock(&fi->lock);
	if (!(test_bit(IRQ_PEND_EXT_SERVICE, &fi->pending_irqs))) {
		spin_unlock(&fi->lock);
		return 0;
	}
	ext = fi->srv_signal;
	memset(&fi->srv_signal, 0, sizeof(ext));
	clear_bit(IRQ_PEND_EXT_SERVICE, &fi->pending_irqs);
	spin_unlock(&fi->lock);

	VCPU_EVENT(vcpu, 4, "deliver: sclp parameter 0x%x",
		   ext.ext_params);
	vcpu->stat.deliver_service_signal++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_INT_SERVICE,
					 ext.ext_params, 0);

	rc  = put_guest_lc(vcpu, EXT_IRQ_SERVICE_SIG, (u16 *)__LC_EXT_INT_CODE);
	rc |= put_guest_lc(vcpu, 0, (u16 *)__LC_EXT_CPU_ADDR);
	rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= put_guest_lc(vcpu, ext.ext_params,
			   (u32 *)__LC_EXT_PARAMS);

	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_pfault_done(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;
	struct kvm_s390_interrupt_info *inti;
	int rc = 0;

	spin_lock(&fi->lock);
	inti = list_first_entry_or_null(&fi->lists[FIRQ_LIST_PFAULT],
					struct kvm_s390_interrupt_info,
					list);
	if (inti) {
		list_del(&inti->list);
		fi->counters[FIRQ_CNTR_PFAULT] -= 1;
	}
	if (list_empty(&fi->lists[FIRQ_LIST_PFAULT]))
		clear_bit(IRQ_PEND_PFAULT_DONE, &fi->pending_irqs);
	spin_unlock(&fi->lock);

	if (inti) {
		trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id,
						 KVM_S390_INT_PFAULT_DONE, 0,
						 inti->ext.ext_params2);
		VCPU_EVENT(vcpu, 4, "deliver: pfault done token 0x%llx",
			   inti->ext.ext_params2);

		rc  = put_guest_lc(vcpu, EXT_IRQ_CP_SERVICE,
				(u16 *)__LC_EXT_INT_CODE);
		rc |= put_guest_lc(vcpu, PFAULT_DONE,
				(u16 *)__LC_EXT_CPU_ADDR);
		rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
				&vcpu->arch.sie_block->gpsw,
				sizeof(psw_t));
		rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW,
				&vcpu->arch.sie_block->gpsw,
				sizeof(psw_t));
		rc |= put_guest_lc(vcpu, inti->ext.ext_params2,
				(u64 *)__LC_EXT_PARAMS2);
		kfree(inti);
	}
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_virtio(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;
	struct kvm_s390_interrupt_info *inti;
	int rc = 0;

	spin_lock(&fi->lock);
	inti = list_first_entry_or_null(&fi->lists[FIRQ_LIST_VIRTIO],
					struct kvm_s390_interrupt_info,
					list);
	if (inti) {
		VCPU_EVENT(vcpu, 4,
			   "deliver: virtio parm: 0x%x,parm64: 0x%llx",
			   inti->ext.ext_params, inti->ext.ext_params2);
		vcpu->stat.deliver_virtio_interrupt++;
		trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id,
				inti->type,
				inti->ext.ext_params,
				inti->ext.ext_params2);
		list_del(&inti->list);
		fi->counters[FIRQ_CNTR_VIRTIO] -= 1;
	}
	if (list_empty(&fi->lists[FIRQ_LIST_VIRTIO]))
		clear_bit(IRQ_PEND_VIRTIO, &fi->pending_irqs);
	spin_unlock(&fi->lock);

	if (inti) {
		rc  = put_guest_lc(vcpu, EXT_IRQ_CP_SERVICE,
				(u16 *)__LC_EXT_INT_CODE);
		rc |= put_guest_lc(vcpu, VIRTIO_PARAM,
				(u16 *)__LC_EXT_CPU_ADDR);
		rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
				&vcpu->arch.sie_block->gpsw,
				sizeof(psw_t));
		rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW,
				&vcpu->arch.sie_block->gpsw,
				sizeof(psw_t));
		rc |= put_guest_lc(vcpu, inti->ext.ext_params,
				(u32 *)__LC_EXT_PARAMS);
		rc |= put_guest_lc(vcpu, inti->ext.ext_params2,
				(u64 *)__LC_EXT_PARAMS2);
		kfree(inti);
	}
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_io(struct kvm_vcpu *vcpu,
				     unsigned long irq_type)
{
	struct list_head *isc_list;
	struct kvm_s390_float_interrupt *fi;
	struct kvm_s390_interrupt_info *inti = NULL;
	int rc = 0;

	fi = &vcpu->kvm->arch.float_int;

	spin_lock(&fi->lock);
	isc_list = &fi->lists[irq_type - IRQ_PEND_IO_ISC_0];
	inti = list_first_entry_or_null(isc_list,
					struct kvm_s390_interrupt_info,
					list);
	if (inti) {
		if (inti->type & KVM_S390_INT_IO_AI_MASK)
			VCPU_EVENT(vcpu, 4, "%s", "deliver: I/O (AI)");
		else
			VCPU_EVENT(vcpu, 4, "deliver: I/O %x ss %x schid %04x",
			inti->io.subchannel_id >> 8,
			inti->io.subchannel_id >> 1 & 0x3,
			inti->io.subchannel_nr);

		vcpu->stat.deliver_io_int++;
		trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id,
				inti->type,
				((__u32)inti->io.subchannel_id << 16) |
				inti->io.subchannel_nr,
				((__u64)inti->io.io_int_parm << 32) |
				inti->io.io_int_word);
		list_del(&inti->list);
		fi->counters[FIRQ_CNTR_IO] -= 1;
	}
	if (list_empty(isc_list))
		clear_bit(irq_type, &fi->pending_irqs);
	spin_unlock(&fi->lock);

	if (inti) {
		rc  = put_guest_lc(vcpu, inti->io.subchannel_id,
				(u16 *)__LC_SUBCHANNEL_ID);
		rc |= put_guest_lc(vcpu, inti->io.subchannel_nr,
				(u16 *)__LC_SUBCHANNEL_NR);
		rc |= put_guest_lc(vcpu, inti->io.io_int_parm,
				(u32 *)__LC_IO_INT_PARM);
		rc |= put_guest_lc(vcpu, inti->io.io_int_word,
				(u32 *)__LC_IO_INT_WORD);
		rc |= write_guest_lc(vcpu, __LC_IO_OLD_PSW,
				&vcpu->arch.sie_block->gpsw,
				sizeof(psw_t));
		rc |= read_guest_lc(vcpu, __LC_IO_NEW_PSW,
				&vcpu->arch.sie_block->gpsw,
				sizeof(psw_t));
		kfree(inti);
	}

	return rc ? -EFAULT : 0;
}

typedef int (*deliver_irq_t)(struct kvm_vcpu *vcpu);

static const deliver_irq_t deliver_irq_funcs[] = {
	[IRQ_PEND_MCHK_EX]        = __deliver_machine_check,
	[IRQ_PEND_MCHK_REP]       = __deliver_machine_check,
	[IRQ_PEND_PROG]           = __deliver_prog,
	[IRQ_PEND_EXT_EMERGENCY]  = __deliver_emergency_signal,
	[IRQ_PEND_EXT_EXTERNAL]   = __deliver_external_call,
	[IRQ_PEND_EXT_CLOCK_COMP] = __deliver_ckc,
	[IRQ_PEND_EXT_CPU_TIMER]  = __deliver_cpu_timer,
	[IRQ_PEND_RESTART]        = __deliver_restart,
	[IRQ_PEND_SET_PREFIX]     = __deliver_set_prefix,
	[IRQ_PEND_PFAULT_INIT]    = __deliver_pfault_init,
	[IRQ_PEND_EXT_SERVICE]    = __deliver_service,
	[IRQ_PEND_PFAULT_DONE]    = __deliver_pfault_done,
	[IRQ_PEND_VIRTIO]         = __deliver_virtio,
};

/* Check whether an external call is pending (deliverable or not) */
int kvm_s390_ext_call_pending(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	if (!sclp.has_sigpif)
		return test_bit(IRQ_PEND_EXT_EXTERNAL, &li->pending_irqs);

	return sca_ext_call_pending(vcpu, NULL);
}

int kvm_s390_vcpu_has_irq(struct kvm_vcpu *vcpu, int exclude_stop)
{
	if (deliverable_irqs(vcpu))
		return 1;

	if (kvm_cpu_has_pending_timer(vcpu))
		return 1;

	/* external call pending and deliverable */
	if (kvm_s390_ext_call_pending(vcpu) &&
	    !psw_extint_disabled(vcpu) &&
	    (vcpu->arch.sie_block->gcr[0] & 0x2000ul))
		return 1;

	if (!exclude_stop && kvm_s390_is_stop_irq_pending(vcpu))
		return 1;
	return 0;
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return ckc_irq_pending(vcpu) || cpu_timer_irq_pending(vcpu);
}

static u64 __calculate_sltime(struct kvm_vcpu *vcpu)
{
	u64 now, cputm, sltime = 0;

	if (ckc_interrupts_enabled(vcpu)) {
		now = kvm_s390_get_tod_clock_fast(vcpu->kvm);
		sltime = tod_to_ns(vcpu->arch.sie_block->ckc - now);
		/* already expired or overflow? */
		if (!sltime || vcpu->arch.sie_block->ckc <= now)
			return 0;
		if (cpu_timer_interrupts_enabled(vcpu)) {
			cputm = kvm_s390_get_cpu_timer(vcpu);
			/* already expired? */
			if (cputm >> 63)
				return 0;
			return min(sltime, tod_to_ns(cputm));
		}
	} else if (cpu_timer_interrupts_enabled(vcpu)) {
		sltime = kvm_s390_get_cpu_timer(vcpu);
		/* already expired? */
		if (sltime >> 63)
			return 0;
	}
	return sltime;
}

int kvm_s390_handle_wait(struct kvm_vcpu *vcpu)
{
	u64 sltime;

	vcpu->stat.exit_wait_state++;

	/* fast path */
	if (kvm_arch_vcpu_runnable(vcpu))
		return 0;

	if (psw_interrupts_disabled(vcpu)) {
		VCPU_EVENT(vcpu, 3, "%s", "disabled wait");
		return -EOPNOTSUPP; /* disabled wait */
	}

	if (!ckc_interrupts_enabled(vcpu) &&
	    !cpu_timer_interrupts_enabled(vcpu)) {
		VCPU_EVENT(vcpu, 3, "%s", "enabled wait w/o timer");
		__set_cpu_idle(vcpu);
		goto no_timer;
	}

	sltime = __calculate_sltime(vcpu);
	if (!sltime)
		return 0;

	__set_cpu_idle(vcpu);
	hrtimer_start(&vcpu->arch.ckc_timer, sltime, HRTIMER_MODE_REL);
	VCPU_EVENT(vcpu, 4, "enabled wait: %llu ns", sltime);
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
	/*
	 * We cannot move this into the if, as the CPU might be already
	 * in kvm_vcpu_block without having the waitqueue set (polling)
	 */
	vcpu->valid_wakeup = true;
	if (swait_active(&vcpu->wq)) {
		/*
		 * The vcpu gave up the cpu voluntarily, mark it as a good
		 * yield-candidate.
		 */
		vcpu->preempted = true;
		swake_up(&vcpu->wq);
		vcpu->stat.halt_wakeup++;
	}
	/*
	 * The VCPU might not be sleeping but is executing the VSIE. Let's
	 * kick it, so it leaves the SIE to process the request.
	 */
	kvm_s390_vsie_kick(vcpu);
}

enum hrtimer_restart kvm_s390_idle_wakeup(struct hrtimer *timer)
{
	struct kvm_vcpu *vcpu;
	u64 sltime;

	vcpu = container_of(timer, struct kvm_vcpu, arch.ckc_timer);
	sltime = __calculate_sltime(vcpu);

	/*
	 * If the monotonic clock runs faster than the tod clock we might be
	 * woken up too early and have to go back to sleep to avoid deadlocks.
	 */
	if (sltime && hrtimer_forward_now(timer, ns_to_ktime(sltime)))
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

	sca_clear_ext_call(vcpu);
}

int __must_check kvm_s390_deliver_pending_interrupts(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	deliver_irq_t func;
	int rc = 0;
	unsigned long irq_type;
	unsigned long irqs;

	__reset_intercept_indicators(vcpu);

	/* pending ckc conditions might have been invalidated */
	clear_bit(IRQ_PEND_EXT_CLOCK_COMP, &li->pending_irqs);
	if (ckc_irq_pending(vcpu))
		set_bit(IRQ_PEND_EXT_CLOCK_COMP, &li->pending_irqs);

	/* pending cpu timer conditions might have been invalidated */
	clear_bit(IRQ_PEND_EXT_CPU_TIMER, &li->pending_irqs);
	if (cpu_timer_irq_pending(vcpu))
		set_bit(IRQ_PEND_EXT_CPU_TIMER, &li->pending_irqs);

	while ((irqs = deliverable_irqs(vcpu)) && !rc) {
		/* bits are in the order of interrupt priority */
		irq_type = find_first_bit(&irqs, IRQ_PEND_COUNT);
		if (is_ioirq(irq_type)) {
			rc = __deliver_io(vcpu, irq_type);
		} else {
			func = deliver_irq_funcs[irq_type];
			if (!func) {
				WARN_ON_ONCE(func == NULL);
				clear_bit(irq_type, &li->pending_irqs);
				continue;
			}
			rc = func(vcpu);
		}
	}

	set_intercept_indicators(vcpu);

	return rc;
}

static int __inject_prog(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	VCPU_EVENT(vcpu, 3, "inject: program irq code 0x%x", irq->u.pgm.code);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_PROGRAM_INT,
				   irq->u.pgm.code, 0);

	if (!(irq->u.pgm.flags & KVM_S390_PGM_FLAGS_ILC_VALID)) {
		/* auto detection if no valid ILC was given */
		irq->u.pgm.flags &= ~KVM_S390_PGM_FLAGS_ILC_MASK;
		irq->u.pgm.flags |= kvm_s390_get_ilen(vcpu);
		irq->u.pgm.flags |= KVM_S390_PGM_FLAGS_ILC_VALID;
	}

	if (irq->u.pgm.code == PGM_PER) {
		li->irq.pgm.code |= PGM_PER;
		li->irq.pgm.flags = irq->u.pgm.flags;
		/* only modify PER related information */
		li->irq.pgm.per_address = irq->u.pgm.per_address;
		li->irq.pgm.per_code = irq->u.pgm.per_code;
		li->irq.pgm.per_atmid = irq->u.pgm.per_atmid;
		li->irq.pgm.per_access_id = irq->u.pgm.per_access_id;
	} else if (!(irq->u.pgm.code & PGM_PER)) {
		li->irq.pgm.code = (li->irq.pgm.code & PGM_PER) |
				   irq->u.pgm.code;
		li->irq.pgm.flags = irq->u.pgm.flags;
		/* only modify non-PER information */
		li->irq.pgm.trans_exc_code = irq->u.pgm.trans_exc_code;
		li->irq.pgm.mon_code = irq->u.pgm.mon_code;
		li->irq.pgm.data_exc_code = irq->u.pgm.data_exc_code;
		li->irq.pgm.mon_class_nr = irq->u.pgm.mon_class_nr;
		li->irq.pgm.exc_access_id = irq->u.pgm.exc_access_id;
		li->irq.pgm.op_access_id = irq->u.pgm.op_access_id;
	} else {
		li->irq.pgm = irq->u.pgm;
	}
	set_bit(IRQ_PEND_PROG, &li->pending_irqs);
	return 0;
}

static int __inject_pfault_init(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	VCPU_EVENT(vcpu, 4, "inject: pfault init parameter block at 0x%llx",
		   irq->u.ext.ext_params2);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_PFAULT_INIT,
				   irq->u.ext.ext_params,
				   irq->u.ext.ext_params2);

	li->irq.ext = irq->u.ext;
	set_bit(IRQ_PEND_PFAULT_INIT, &li->pending_irqs);
	atomic_or(CPUSTAT_EXT_INT, li->cpuflags);
	return 0;
}

static int __inject_extcall(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_extcall_info *extcall = &li->irq.extcall;
	uint16_t src_id = irq->u.extcall.code;

	VCPU_EVENT(vcpu, 4, "inject: external call source-cpu:%u",
		   src_id);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_EXTERNAL_CALL,
				   src_id, 0);

	/* sending vcpu invalid */
	if (kvm_get_vcpu_by_id(vcpu->kvm, src_id) == NULL)
		return -EINVAL;

	if (sclp.has_sigpif)
		return sca_inject_ext_call(vcpu, src_id);

	if (test_and_set_bit(IRQ_PEND_EXT_EXTERNAL, &li->pending_irqs))
		return -EBUSY;
	*extcall = irq->u.extcall;
	atomic_or(CPUSTAT_EXT_INT, li->cpuflags);
	return 0;
}

static int __inject_set_prefix(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_prefix_info *prefix = &li->irq.prefix;

	VCPU_EVENT(vcpu, 3, "inject: set prefix to %x",
		   irq->u.prefix.address);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_SIGP_SET_PREFIX,
				   irq->u.prefix.address, 0);

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

	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_SIGP_STOP, 0, 0);

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

	VCPU_EVENT(vcpu, 3, "%s", "inject: restart int");
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_RESTART, 0, 0);

	set_bit(IRQ_PEND_RESTART, &li->pending_irqs);
	return 0;
}

static int __inject_sigp_emergency(struct kvm_vcpu *vcpu,
				   struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	VCPU_EVENT(vcpu, 4, "inject: emergency from cpu %u",
		   irq->u.emerg.code);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_EMERGENCY,
				   irq->u.emerg.code, 0);

	/* sending vcpu invalid */
	if (kvm_get_vcpu_by_id(vcpu->kvm, irq->u.emerg.code) == NULL)
		return -EINVAL;

	set_bit(irq->u.emerg.code, li->sigp_emerg_pending);
	set_bit(IRQ_PEND_EXT_EMERGENCY, &li->pending_irqs);
	atomic_or(CPUSTAT_EXT_INT, li->cpuflags);
	return 0;
}

static int __inject_mchk(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_mchk_info *mchk = &li->irq.mchk;

	VCPU_EVENT(vcpu, 3, "inject: machine check mcic 0x%llx",
		   irq->u.mchk.mcic);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_MCHK, 0,
				   irq->u.mchk.mcic);

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

	VCPU_EVENT(vcpu, 3, "%s", "inject: clock comparator external");
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_CLOCK_COMP,
				   0, 0);

	set_bit(IRQ_PEND_EXT_CLOCK_COMP, &li->pending_irqs);
	atomic_or(CPUSTAT_EXT_INT, li->cpuflags);
	return 0;
}

static int __inject_cpu_timer(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	VCPU_EVENT(vcpu, 3, "%s", "inject: cpu timer external");
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_CPU_TIMER,
				   0, 0);

	set_bit(IRQ_PEND_EXT_CPU_TIMER, &li->pending_irqs);
	atomic_or(CPUSTAT_EXT_INT, li->cpuflags);
	return 0;
}

static struct kvm_s390_interrupt_info *get_io_int(struct kvm *kvm,
						  int isc, u32 schid)
{
	struct kvm_s390_float_interrupt *fi = &kvm->arch.float_int;
	struct list_head *isc_list = &fi->lists[FIRQ_LIST_IO_ISC_0 + isc];
	struct kvm_s390_interrupt_info *iter;
	u16 id = (schid & 0xffff0000U) >> 16;
	u16 nr = schid & 0x0000ffffU;

	spin_lock(&fi->lock);
	list_for_each_entry(iter, isc_list, list) {
		if (schid && (id != iter->io.subchannel_id ||
			      nr != iter->io.subchannel_nr))
			continue;
		/* found an appropriate entry */
		list_del_init(&iter->list);
		fi->counters[FIRQ_CNTR_IO] -= 1;
		if (list_empty(isc_list))
			clear_bit(IRQ_PEND_IO_ISC_0 + isc, &fi->pending_irqs);
		spin_unlock(&fi->lock);
		return iter;
	}
	spin_unlock(&fi->lock);
	return NULL;
}

/*
 * Dequeue and return an I/O interrupt matching any of the interruption
 * subclasses as designated by the isc mask in cr6 and the schid (if != 0).
 */
struct kvm_s390_interrupt_info *kvm_s390_get_io_int(struct kvm *kvm,
						    u64 isc_mask, u32 schid)
{
	struct kvm_s390_interrupt_info *inti = NULL;
	int isc;

	for (isc = 0; isc <= MAX_ISC && !inti; isc++) {
		if (isc_mask & isc_to_isc_bits(isc))
			inti = get_io_int(kvm, isc, schid);
	}
	return inti;
}

#define SCCB_MASK 0xFFFFFFF8
#define SCCB_EVENT_PENDING 0x3

static int __inject_service(struct kvm *kvm,
			     struct kvm_s390_interrupt_info *inti)
{
	struct kvm_s390_float_interrupt *fi = &kvm->arch.float_int;

	spin_lock(&fi->lock);
	fi->srv_signal.ext_params |= inti->ext.ext_params & SCCB_EVENT_PENDING;
	/*
	 * Early versions of the QEMU s390 bios will inject several
	 * service interrupts after another without handling a
	 * condition code indicating busy.
	 * We will silently ignore those superfluous sccb values.
	 * A future version of QEMU will take care of serialization
	 * of servc requests
	 */
	if (fi->srv_signal.ext_params & SCCB_MASK)
		goto out;
	fi->srv_signal.ext_params |= inti->ext.ext_params & SCCB_MASK;
	set_bit(IRQ_PEND_EXT_SERVICE, &fi->pending_irqs);
out:
	spin_unlock(&fi->lock);
	kfree(inti);
	return 0;
}

static int __inject_virtio(struct kvm *kvm,
			    struct kvm_s390_interrupt_info *inti)
{
	struct kvm_s390_float_interrupt *fi = &kvm->arch.float_int;

	spin_lock(&fi->lock);
	if (fi->counters[FIRQ_CNTR_VIRTIO] >= KVM_S390_MAX_VIRTIO_IRQS) {
		spin_unlock(&fi->lock);
		return -EBUSY;
	}
	fi->counters[FIRQ_CNTR_VIRTIO] += 1;
	list_add_tail(&inti->list, &fi->lists[FIRQ_LIST_VIRTIO]);
	set_bit(IRQ_PEND_VIRTIO, &fi->pending_irqs);
	spin_unlock(&fi->lock);
	return 0;
}

static int __inject_pfault_done(struct kvm *kvm,
				 struct kvm_s390_interrupt_info *inti)
{
	struct kvm_s390_float_interrupt *fi = &kvm->arch.float_int;

	spin_lock(&fi->lock);
	if (fi->counters[FIRQ_CNTR_PFAULT] >=
		(ASYNC_PF_PER_VCPU * KVM_MAX_VCPUS)) {
		spin_unlock(&fi->lock);
		return -EBUSY;
	}
	fi->counters[FIRQ_CNTR_PFAULT] += 1;
	list_add_tail(&inti->list, &fi->lists[FIRQ_LIST_PFAULT]);
	set_bit(IRQ_PEND_PFAULT_DONE, &fi->pending_irqs);
	spin_unlock(&fi->lock);
	return 0;
}

#define CR_PENDING_SUBCLASS 28
static int __inject_float_mchk(struct kvm *kvm,
				struct kvm_s390_interrupt_info *inti)
{
	struct kvm_s390_float_interrupt *fi = &kvm->arch.float_int;

	spin_lock(&fi->lock);
	fi->mchk.cr14 |= inti->mchk.cr14 & (1UL << CR_PENDING_SUBCLASS);
	fi->mchk.mcic |= inti->mchk.mcic;
	set_bit(IRQ_PEND_MCHK_REP, &fi->pending_irqs);
	spin_unlock(&fi->lock);
	kfree(inti);
	return 0;
}

static int __inject_io(struct kvm *kvm, struct kvm_s390_interrupt_info *inti)
{
	struct kvm_s390_float_interrupt *fi;
	struct list_head *list;
	int isc;

	fi = &kvm->arch.float_int;
	spin_lock(&fi->lock);
	if (fi->counters[FIRQ_CNTR_IO] >= KVM_S390_MAX_FLOAT_IRQS) {
		spin_unlock(&fi->lock);
		return -EBUSY;
	}
	fi->counters[FIRQ_CNTR_IO] += 1;

	if (inti->type & KVM_S390_INT_IO_AI_MASK)
		VM_EVENT(kvm, 4, "%s", "inject: I/O (AI)");
	else
		VM_EVENT(kvm, 4, "inject: I/O %x ss %x schid %04x",
			inti->io.subchannel_id >> 8,
			inti->io.subchannel_id >> 1 & 0x3,
			inti->io.subchannel_nr);
	isc = int_word_to_isc(inti->io.io_int_word);
	list = &fi->lists[FIRQ_LIST_IO_ISC_0 + isc];
	list_add_tail(&inti->list, list);
	set_bit(IRQ_PEND_IO_ISC_0 + isc, &fi->pending_irqs);
	spin_unlock(&fi->lock);
	return 0;
}

/*
 * Find a destination VCPU for a floating irq and kick it.
 */
static void __floating_irq_kick(struct kvm *kvm, u64 type)
{
	struct kvm_s390_float_interrupt *fi = &kvm->arch.float_int;
	struct kvm_s390_local_interrupt *li;
	struct kvm_vcpu *dst_vcpu;
	int sigcpu, online_vcpus, nr_tries = 0;

	online_vcpus = atomic_read(&kvm->online_vcpus);
	if (!online_vcpus)
		return;

	/* find idle VCPUs first, then round robin */
	sigcpu = find_first_bit(fi->idle_mask, online_vcpus);
	if (sigcpu == online_vcpus) {
		do {
			sigcpu = fi->next_rr_cpu;
			fi->next_rr_cpu = (fi->next_rr_cpu + 1) % online_vcpus;
			/* avoid endless loops if all vcpus are stopped */
			if (nr_tries++ >= online_vcpus)
				return;
		} while (is_vcpu_stopped(kvm_get_vcpu(kvm, sigcpu)));
	}
	dst_vcpu = kvm_get_vcpu(kvm, sigcpu);

	/* make the VCPU drop out of the SIE, or wake it up if sleeping */
	li = &dst_vcpu->arch.local_int;
	spin_lock(&li->lock);
	switch (type) {
	case KVM_S390_MCHK:
		atomic_or(CPUSTAT_STOP_INT, li->cpuflags);
		break;
	case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
		atomic_or(CPUSTAT_IO_INT, li->cpuflags);
		break;
	default:
		atomic_or(CPUSTAT_EXT_INT, li->cpuflags);
		break;
	}
	spin_unlock(&li->lock);
	kvm_s390_vcpu_wakeup(dst_vcpu);
}

static int __inject_vm(struct kvm *kvm, struct kvm_s390_interrupt_info *inti)
{
	u64 type = READ_ONCE(inti->type);
	int rc;

	switch (type) {
	case KVM_S390_MCHK:
		rc = __inject_float_mchk(kvm, inti);
		break;
	case KVM_S390_INT_VIRTIO:
		rc = __inject_virtio(kvm, inti);
		break;
	case KVM_S390_INT_SERVICE:
		rc = __inject_service(kvm, inti);
		break;
	case KVM_S390_INT_PFAULT_DONE:
		rc = __inject_pfault_done(kvm, inti);
		break;
	case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
		rc = __inject_io(kvm, inti);
		break;
	default:
		rc = -EINVAL;
	}
	if (rc)
		return rc;

	__floating_irq_kick(kvm, type);
	return 0;
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
		VM_EVENT(kvm, 4, "inject: sclp parm:%x", s390int->parm);
		inti->ext.ext_params = s390int->parm;
		break;
	case KVM_S390_INT_PFAULT_DONE:
		inti->ext.ext_params2 = s390int->parm64;
		break;
	case KVM_S390_MCHK:
		VM_EVENT(kvm, 3, "inject: machine check mcic 0x%llx",
			 s390int->parm64);
		inti->mchk.cr14 = s390int->parm; /* upper bits are not used */
		inti->mchk.mcic = s390int->parm64;
		break;
	case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
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

int kvm_s390_reinject_io_int(struct kvm *kvm,
			      struct kvm_s390_interrupt_info *inti)
{
	return __inject_vm(kvm, inti);
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

static int do_inject_vcpu(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	int rc;

	switch (irq->type) {
	case KVM_S390_PROGRAM_INT:
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

	return rc;
}

int kvm_s390_inject_vcpu(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	int rc;

	spin_lock(&li->lock);
	rc = do_inject_vcpu(vcpu, irq);
	spin_unlock(&li->lock);
	if (!rc)
		kvm_s390_vcpu_wakeup(vcpu);
	return rc;
}

static inline void clear_irq_list(struct list_head *_list)
{
	struct kvm_s390_interrupt_info *inti, *n;

	list_for_each_entry_safe(inti, n, _list, list) {
		list_del(&inti->list);
		kfree(inti);
	}
}

static void inti_to_irq(struct kvm_s390_interrupt_info *inti,
		       struct kvm_s390_irq *irq)
{
	irq->type = inti->type;
	switch (inti->type) {
	case KVM_S390_INT_PFAULT_INIT:
	case KVM_S390_INT_PFAULT_DONE:
	case KVM_S390_INT_VIRTIO:
		irq->u.ext = inti->ext;
		break;
	case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
		irq->u.io = inti->io;
		break;
	}
}

void kvm_s390_clear_float_irqs(struct kvm *kvm)
{
	struct kvm_s390_float_interrupt *fi = &kvm->arch.float_int;
	int i;

	spin_lock(&fi->lock);
	fi->pending_irqs = 0;
	memset(&fi->srv_signal, 0, sizeof(fi->srv_signal));
	memset(&fi->mchk, 0, sizeof(fi->mchk));
	for (i = 0; i < FIRQ_LIST_COUNT; i++)
		clear_irq_list(&fi->lists[i]);
	for (i = 0; i < FIRQ_MAX_COUNT; i++)
		fi->counters[i] = 0;
	spin_unlock(&fi->lock);
};

static int get_all_floating_irqs(struct kvm *kvm, u8 __user *usrbuf, u64 len)
{
	struct kvm_s390_interrupt_info *inti;
	struct kvm_s390_float_interrupt *fi;
	struct kvm_s390_irq *buf;
	struct kvm_s390_irq *irq;
	int max_irqs;
	int ret = 0;
	int n = 0;
	int i;

	if (len > KVM_S390_FLIC_MAX_BUFFER || len == 0)
		return -EINVAL;

	/*
	 * We are already using -ENOMEM to signal
	 * userspace it may retry with a bigger buffer,
	 * so we need to use something else for this case
	 */
	buf = vzalloc(len);
	if (!buf)
		return -ENOBUFS;

	max_irqs = len / sizeof(struct kvm_s390_irq);

	fi = &kvm->arch.float_int;
	spin_lock(&fi->lock);
	for (i = 0; i < FIRQ_LIST_COUNT; i++) {
		list_for_each_entry(inti, &fi->lists[i], list) {
			if (n == max_irqs) {
				/* signal userspace to try again */
				ret = -ENOMEM;
				goto out;
			}
			inti_to_irq(inti, &buf[n]);
			n++;
		}
	}
	if (test_bit(IRQ_PEND_EXT_SERVICE, &fi->pending_irqs)) {
		if (n == max_irqs) {
			/* signal userspace to try again */
			ret = -ENOMEM;
			goto out;
		}
		irq = (struct kvm_s390_irq *) &buf[n];
		irq->type = KVM_S390_INT_SERVICE;
		irq->u.ext = fi->srv_signal;
		n++;
	}
	if (test_bit(IRQ_PEND_MCHK_REP, &fi->pending_irqs)) {
		if (n == max_irqs) {
				/* signal userspace to try again */
				ret = -ENOMEM;
				goto out;
		}
		irq = (struct kvm_s390_irq *) &buf[n];
		irq->type = KVM_S390_MCHK;
		irq->u.mchk = fi->mchk;
		n++;
}

out:
	spin_unlock(&fi->lock);
	if (!ret && n > 0) {
		if (copy_to_user(usrbuf, buf, sizeof(struct kvm_s390_irq) * n))
			ret = -EFAULT;
	}
	vfree(buf);

	return ret < 0 ? ret : n;
}

static int flic_ais_mode_get_all(struct kvm *kvm, struct kvm_device_attr *attr)
{
	struct kvm_s390_float_interrupt *fi = &kvm->arch.float_int;
	struct kvm_s390_ais_all ais;

	if (attr->attr < sizeof(ais))
		return -EINVAL;

	if (!test_kvm_facility(kvm, 72))
		return -ENOTSUPP;

	mutex_lock(&fi->ais_lock);
	ais.simm = fi->simm;
	ais.nimm = fi->nimm;
	mutex_unlock(&fi->ais_lock);

	if (copy_to_user((void __user *)attr->addr, &ais, sizeof(ais)))
		return -EFAULT;

	return 0;
}

static int flic_get_attr(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	int r;

	switch (attr->group) {
	case KVM_DEV_FLIC_GET_ALL_IRQS:
		r = get_all_floating_irqs(dev->kvm, (u8 __user *) attr->addr,
					  attr->attr);
		break;
	case KVM_DEV_FLIC_AISM_ALL:
		r = flic_ais_mode_get_all(dev->kvm, attr);
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
	adapter->suppressible = (adapter_info.flags) &
				KVM_S390_ADAPTER_SUPPRESSIBLE;
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

static int clear_io_irq(struct kvm *kvm, struct kvm_device_attr *attr)

{
	const u64 isc_mask = 0xffUL << 24; /* all iscs set */
	u32 schid;

	if (attr->flags)
		return -EINVAL;
	if (attr->attr != sizeof(schid))
		return -EINVAL;
	if (copy_from_user(&schid, (void __user *) attr->addr, sizeof(schid)))
		return -EFAULT;
	kfree(kvm_s390_get_io_int(kvm, isc_mask, schid));
	/*
	 * If userspace is conforming to the architecture, we can have at most
	 * one pending I/O interrupt per subchannel, so this is effectively a
	 * clear all.
	 */
	return 0;
}

static int modify_ais_mode(struct kvm *kvm, struct kvm_device_attr *attr)
{
	struct kvm_s390_float_interrupt *fi = &kvm->arch.float_int;
	struct kvm_s390_ais_req req;
	int ret = 0;

	if (!test_kvm_facility(kvm, 72))
		return -ENOTSUPP;

	if (copy_from_user(&req, (void __user *)attr->addr, sizeof(req)))
		return -EFAULT;

	if (req.isc > MAX_ISC)
		return -EINVAL;

	trace_kvm_s390_modify_ais_mode(req.isc,
				       (fi->simm & AIS_MODE_MASK(req.isc)) ?
				       (fi->nimm & AIS_MODE_MASK(req.isc)) ?
				       2 : KVM_S390_AIS_MODE_SINGLE :
				       KVM_S390_AIS_MODE_ALL, req.mode);

	mutex_lock(&fi->ais_lock);
	switch (req.mode) {
	case KVM_S390_AIS_MODE_ALL:
		fi->simm &= ~AIS_MODE_MASK(req.isc);
		fi->nimm &= ~AIS_MODE_MASK(req.isc);
		break;
	case KVM_S390_AIS_MODE_SINGLE:
		fi->simm |= AIS_MODE_MASK(req.isc);
		fi->nimm &= ~AIS_MODE_MASK(req.isc);
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&fi->ais_lock);

	return ret;
}

static int kvm_s390_inject_airq(struct kvm *kvm,
				struct s390_io_adapter *adapter)
{
	struct kvm_s390_float_interrupt *fi = &kvm->arch.float_int;
	struct kvm_s390_interrupt s390int = {
		.type = KVM_S390_INT_IO(1, 0, 0, 0),
		.parm = 0,
		.parm64 = (adapter->isc << 27) | 0x80000000,
	};
	int ret = 0;

	if (!test_kvm_facility(kvm, 72) || !adapter->suppressible)
		return kvm_s390_inject_vm(kvm, &s390int);

	mutex_lock(&fi->ais_lock);
	if (fi->nimm & AIS_MODE_MASK(adapter->isc)) {
		trace_kvm_s390_airq_suppressed(adapter->id, adapter->isc);
		goto out;
	}

	ret = kvm_s390_inject_vm(kvm, &s390int);
	if (!ret && (fi->simm & AIS_MODE_MASK(adapter->isc))) {
		fi->nimm |= AIS_MODE_MASK(adapter->isc);
		trace_kvm_s390_modify_ais_mode(adapter->isc,
					       KVM_S390_AIS_MODE_SINGLE, 2);
	}
out:
	mutex_unlock(&fi->ais_lock);
	return ret;
}

static int flic_inject_airq(struct kvm *kvm, struct kvm_device_attr *attr)
{
	unsigned int id = attr->attr;
	struct s390_io_adapter *adapter = get_io_adapter(kvm, id);

	if (!adapter)
		return -EINVAL;

	return kvm_s390_inject_airq(kvm, adapter);
}

static int flic_ais_mode_set_all(struct kvm *kvm, struct kvm_device_attr *attr)
{
	struct kvm_s390_float_interrupt *fi = &kvm->arch.float_int;
	struct kvm_s390_ais_all ais;

	if (!test_kvm_facility(kvm, 72))
		return -ENOTSUPP;

	if (copy_from_user(&ais, (void __user *)attr->addr, sizeof(ais)))
		return -EFAULT;

	mutex_lock(&fi->ais_lock);
	fi->simm = ais.simm;
	fi->nimm = ais.nimm;
	mutex_unlock(&fi->ais_lock);

	return 0;
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
	case KVM_DEV_FLIC_CLEAR_IO_IRQ:
		r = clear_io_irq(dev->kvm, attr);
		break;
	case KVM_DEV_FLIC_AISM:
		r = modify_ais_mode(dev->kvm, attr);
		break;
	case KVM_DEV_FLIC_AIRQ_INJECT:
		r = flic_inject_airq(dev->kvm, attr);
		break;
	case KVM_DEV_FLIC_AISM_ALL:
		r = flic_ais_mode_set_all(dev->kvm, attr);
		break;
	default:
		r = -EINVAL;
	}

	return r;
}

static int flic_has_attr(struct kvm_device *dev,
			     struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_FLIC_GET_ALL_IRQS:
	case KVM_DEV_FLIC_ENQUEUE:
	case KVM_DEV_FLIC_CLEAR_IRQS:
	case KVM_DEV_FLIC_APF_ENABLE:
	case KVM_DEV_FLIC_APF_DISABLE_WAIT:
	case KVM_DEV_FLIC_ADAPTER_REGISTER:
	case KVM_DEV_FLIC_ADAPTER_MODIFY:
	case KVM_DEV_FLIC_CLEAR_IO_IRQ:
	case KVM_DEV_FLIC_AISM:
	case KVM_DEV_FLIC_AIRQ_INJECT:
	case KVM_DEV_FLIC_AISM_ALL:
		return 0;
	}
	return -ENXIO;
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
	.has_attr = flic_has_attr,
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
		ret = kvm_s390_inject_airq(kvm, adapter);
		if (ret == 0)
			ret = 1;
	}
	return ret;
}

/*
 * Inject the machine check to the guest.
 */
void kvm_s390_reinject_machine_check(struct kvm_vcpu *vcpu,
				     struct mcck_volatile_info *mcck_info)
{
	struct kvm_s390_interrupt_info inti;
	struct kvm_s390_irq irq;
	struct kvm_s390_mchk_info *mchk;
	union mci mci;
	__u64 cr14 = 0;         /* upper bits are not used */
	int rc;

	mci.val = mcck_info->mcic;
	if (mci.sr)
		cr14 |= CR14_RECOVERY_SUBMASK;
	if (mci.dg)
		cr14 |= CR14_DEGRADATION_SUBMASK;
	if (mci.w)
		cr14 |= CR14_WARNING_SUBMASK;

	mchk = mci.ck ? &inti.mchk : &irq.u.mchk;
	mchk->cr14 = cr14;
	mchk->mcic = mcck_info->mcic;
	mchk->ext_damage_code = mcck_info->ext_damage_code;
	mchk->failing_storage_address = mcck_info->failing_storage_address;
	if (mci.ck) {
		/* Inject the floating machine check */
		inti.type = KVM_S390_MCHK;
		rc = __inject_vm(vcpu->kvm, &inti);
	} else {
		/* Inject the machine check to specified vcpu */
		irq.type = KVM_S390_MCHK;
		rc = kvm_s390_inject_vcpu(vcpu, &irq);
	}
	WARN_ON_ONCE(rc);
}

int kvm_set_routing_entry(struct kvm *kvm,
			  struct kvm_kernel_irq_routing_entry *e,
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

int kvm_s390_set_irq_state(struct kvm_vcpu *vcpu, void __user *irqstate, int len)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_irq *buf;
	int r = 0;
	int n;

	buf = vmalloc(len);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user((void *) buf, irqstate, len)) {
		r = -EFAULT;
		goto out_free;
	}

	/*
	 * Don't allow setting the interrupt state
	 * when there are already interrupts pending
	 */
	spin_lock(&li->lock);
	if (li->pending_irqs) {
		r = -EBUSY;
		goto out_unlock;
	}

	for (n = 0; n < len / sizeof(*buf); n++) {
		r = do_inject_vcpu(vcpu, &buf[n]);
		if (r)
			break;
	}

out_unlock:
	spin_unlock(&li->lock);
out_free:
	vfree(buf);

	return r;
}

static void store_local_irq(struct kvm_s390_local_interrupt *li,
			    struct kvm_s390_irq *irq,
			    unsigned long irq_type)
{
	switch (irq_type) {
	case IRQ_PEND_MCHK_EX:
	case IRQ_PEND_MCHK_REP:
		irq->type = KVM_S390_MCHK;
		irq->u.mchk = li->irq.mchk;
		break;
	case IRQ_PEND_PROG:
		irq->type = KVM_S390_PROGRAM_INT;
		irq->u.pgm = li->irq.pgm;
		break;
	case IRQ_PEND_PFAULT_INIT:
		irq->type = KVM_S390_INT_PFAULT_INIT;
		irq->u.ext = li->irq.ext;
		break;
	case IRQ_PEND_EXT_EXTERNAL:
		irq->type = KVM_S390_INT_EXTERNAL_CALL;
		irq->u.extcall = li->irq.extcall;
		break;
	case IRQ_PEND_EXT_CLOCK_COMP:
		irq->type = KVM_S390_INT_CLOCK_COMP;
		break;
	case IRQ_PEND_EXT_CPU_TIMER:
		irq->type = KVM_S390_INT_CPU_TIMER;
		break;
	case IRQ_PEND_SIGP_STOP:
		irq->type = KVM_S390_SIGP_STOP;
		irq->u.stop = li->irq.stop;
		break;
	case IRQ_PEND_RESTART:
		irq->type = KVM_S390_RESTART;
		break;
	case IRQ_PEND_SET_PREFIX:
		irq->type = KVM_S390_SIGP_SET_PREFIX;
		irq->u.prefix = li->irq.prefix;
		break;
	}
}

int kvm_s390_get_irq_state(struct kvm_vcpu *vcpu, __u8 __user *buf, int len)
{
	int scn;
	unsigned long sigp_emerg_pending[BITS_TO_LONGS(KVM_MAX_VCPUS)];
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	unsigned long pending_irqs;
	struct kvm_s390_irq irq;
	unsigned long irq_type;
	int cpuaddr;
	int n = 0;

	spin_lock(&li->lock);
	pending_irqs = li->pending_irqs;
	memcpy(&sigp_emerg_pending, &li->sigp_emerg_pending,
	       sizeof(sigp_emerg_pending));
	spin_unlock(&li->lock);

	for_each_set_bit(irq_type, &pending_irqs, IRQ_PEND_COUNT) {
		memset(&irq, 0, sizeof(irq));
		if (irq_type == IRQ_PEND_EXT_EMERGENCY)
			continue;
		if (n + sizeof(irq) > len)
			return -ENOBUFS;
		store_local_irq(&vcpu->arch.local_int, &irq, irq_type);
		if (copy_to_user(&buf[n], &irq, sizeof(irq)))
			return -EFAULT;
		n += sizeof(irq);
	}

	if (test_bit(IRQ_PEND_EXT_EMERGENCY, &pending_irqs)) {
		for_each_set_bit(cpuaddr, sigp_emerg_pending, KVM_MAX_VCPUS) {
			memset(&irq, 0, sizeof(irq));
			if (n + sizeof(irq) > len)
				return -ENOBUFS;
			irq.type = KVM_S390_INT_EMERGENCY;
			irq.u.emerg.code = cpuaddr;
			if (copy_to_user(&buf[n], &irq, sizeof(irq)))
				return -EFAULT;
			n += sizeof(irq);
		}
	}

	if (sca_ext_call_pending(vcpu, &scn)) {
		if (n + sizeof(irq) > len)
			return -ENOBUFS;
		memset(&irq, 0, sizeof(irq));
		irq.type = KVM_S390_INT_EXTERNAL_CALL;
		irq.u.extcall.code = scn;
		if (copy_to_user(&buf[n], &irq, sizeof(irq)))
			return -EFAULT;
		n += sizeof(irq);
	}

	return n;
}
