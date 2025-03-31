// SPDX-License-Identifier: GPL-2.0
/*
 * handling kvm guest interrupts
 *
 * Copyright IBM Corp. 2008, 2020
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 */

#define KMSG_COMPONENT "kvm-s390"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/interrupt.h>
#include <linux/kvm_host.h>
#include <linux/hrtimer.h>
#include <linux/mmu_context.h>
#include <linux/nospec.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/bitmap.h>
#include <linux/vmalloc.h>
#include <asm/access-regs.h>
#include <asm/asm-offsets.h>
#include <asm/dis.h>
#include <linux/uaccess.h>
#include <asm/sclp.h>
#include <asm/isc.h>
#include <asm/gmap.h>
#include <asm/nmi.h>
#include <asm/airq.h>
#include <asm/tpi.h>
#include "kvm-s390.h"
#include "gaccess.h"
#include "trace-s390.h"
#include "pci.h"

#define PFAULT_INIT 0x0600
#define PFAULT_DONE 0x0680
#define VIRTIO_PARAM 0x0d00

static struct kvm_s390_gib *gib;

/* handle external calls via sigp interpretation facility */
static int sca_ext_call_pending(struct kvm_vcpu *vcpu, int *src_id)
{
	int c, scn;

	if (!kvm_s390_test_cpuflags(vcpu, CPUSTAT_ECALL_PEND))
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
		union esca_sigp_ctrl new_val = {0}, old_val;

		old_val = READ_ONCE(*sigp_ctrl);
		new_val.scn = src_id;
		new_val.c = 1;
		old_val.c = 0;

		expect = old_val.value;
		rc = cmpxchg(&sigp_ctrl->value, old_val.value, new_val.value);
	} else {
		struct bsca_block *sca = vcpu->kvm->arch.sca;
		union bsca_sigp_ctrl *sigp_ctrl =
			&(sca->cpu[vcpu->vcpu_id].sigp_ctrl);
		union bsca_sigp_ctrl new_val = {0}, old_val;

		old_val = READ_ONCE(*sigp_ctrl);
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
	kvm_s390_set_cpuflags(vcpu, CPUSTAT_ECALL_PEND);
	return 0;
}

static void sca_clear_ext_call(struct kvm_vcpu *vcpu)
{
	if (!kvm_s390_use_sca_entries())
		return;
	kvm_s390_clear_cpuflags(vcpu, CPUSTAT_ECALL_PEND);
	read_lock(&vcpu->kvm->arch.sca_lock);
	if (vcpu->kvm->arch.use_esca) {
		struct esca_block *sca = vcpu->kvm->arch.sca;
		union esca_sigp_ctrl *sigp_ctrl =
			&(sca->cpu[vcpu->vcpu_id].sigp_ctrl);

		WRITE_ONCE(sigp_ctrl->value, 0);
	} else {
		struct bsca_block *sca = vcpu->kvm->arch.sca;
		union bsca_sigp_ctrl *sigp_ctrl =
			&(sca->cpu[vcpu->vcpu_id].sigp_ctrl);

		WRITE_ONCE(sigp_ctrl->value, 0);
	}
	read_unlock(&vcpu->kvm->arch.sca_lock);
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
	    !(vcpu->arch.sie_block->gcr[0] & CR0_CLOCK_COMPARATOR_SUBMASK))
		return 0;
	if (guestdbg_enabled(vcpu) && guestdbg_sstep_enabled(vcpu))
		/* No timer interrupts when single stepping */
		return 0;
	return 1;
}

static int ckc_irq_pending(struct kvm_vcpu *vcpu)
{
	const u64 now = kvm_s390_get_tod_clock_fast(vcpu->kvm);
	const u64 ckc = vcpu->arch.sie_block->ckc;

	if (vcpu->arch.sie_block->gcr[0] & CR0_CLOCK_COMPARATOR_SIGN) {
		if ((s64)ckc >= (s64)now)
			return 0;
	} else if (ckc >= now) {
		return 0;
	}
	return ckc_interrupts_enabled(vcpu);
}

static int cpu_timer_interrupts_enabled(struct kvm_vcpu *vcpu)
{
	return !psw_extint_disabled(vcpu) &&
	       (vcpu->arch.sie_block->gcr[0] & CR0_CPU_TIMER_SUBMASK);
}

static int cpu_timer_irq_pending(struct kvm_vcpu *vcpu)
{
	if (!cpu_timer_interrupts_enabled(vcpu))
		return 0;
	return kvm_s390_get_cpu_timer(vcpu) >> 63;
}

static uint64_t isc_to_isc_bits(int isc)
{
	return (0x80 >> isc) << 24;
}

static inline u32 isc_to_int_word(u8 isc)
{
	return ((u32)isc << 27) | 0x80000000;
}

static inline u8 int_word_to_isc(u32 int_word)
{
	return (int_word & 0x38000000) >> 27;
}

/*
 * To use atomic bitmap functions, we have to provide a bitmap address
 * that is u64 aligned. However, the ipm might be u32 aligned.
 * Therefore, we logically start the bitmap at the very beginning of the
 * struct and fixup the bit number.
 */
#define IPM_BIT_OFFSET (offsetof(struct kvm_s390_gisa, ipm) * BITS_PER_BYTE)

/**
 * gisa_set_iam - change the GISA interruption alert mask
 *
 * @gisa: gisa to operate on
 * @iam: new IAM value to use
 *
 * Change the IAM atomically with the next alert address and the IPM
 * of the GISA if the GISA is not part of the GIB alert list. All three
 * fields are located in the first long word of the GISA.
 *
 * Returns: 0 on success
 *          -EBUSY in case the gisa is part of the alert list
 */
static inline int gisa_set_iam(struct kvm_s390_gisa *gisa, u8 iam)
{
	u64 word, _word;

	word = READ_ONCE(gisa->u64.word[0]);
	do {
		if ((u64)gisa != word >> 32)
			return -EBUSY;
		_word = (word & ~0xffUL) | iam;
	} while (!try_cmpxchg(&gisa->u64.word[0], &word, _word));

	return 0;
}

/**
 * gisa_clear_ipm - clear the GISA interruption pending mask
 *
 * @gisa: gisa to operate on
 *
 * Clear the IPM atomically with the next alert address and the IAM
 * of the GISA unconditionally. All three fields are located in the
 * first long word of the GISA.
 */
static inline void gisa_clear_ipm(struct kvm_s390_gisa *gisa)
{
	u64 word, _word;

	word = READ_ONCE(gisa->u64.word[0]);
	do {
		_word = word & ~(0xffUL << 24);
	} while (!try_cmpxchg(&gisa->u64.word[0], &word, _word));
}

/**
 * gisa_get_ipm_or_restore_iam - return IPM or restore GISA IAM
 *
 * @gi: gisa interrupt struct to work on
 *
 * Atomically restores the interruption alert mask if none of the
 * relevant ISCs are pending and return the IPM.
 *
 * Returns: the relevant pending ISCs
 */
static inline u8 gisa_get_ipm_or_restore_iam(struct kvm_s390_gisa_interrupt *gi)
{
	u8 pending_mask, alert_mask;
	u64 word, _word;

	word = READ_ONCE(gi->origin->u64.word[0]);
	do {
		alert_mask = READ_ONCE(gi->alert.mask);
		pending_mask = (u8)(word >> 24) & alert_mask;
		if (pending_mask)
			return pending_mask;
		_word = (word & ~0xffUL) | alert_mask;
	} while (!try_cmpxchg(&gi->origin->u64.word[0], &word, _word));

	return 0;
}

static inline void gisa_set_ipm_gisc(struct kvm_s390_gisa *gisa, u32 gisc)
{
	set_bit_inv(IPM_BIT_OFFSET + gisc, (unsigned long *) gisa);
}

static inline u8 gisa_get_ipm(struct kvm_s390_gisa *gisa)
{
	return READ_ONCE(gisa->ipm);
}

static inline int gisa_tac_ipm_gisc(struct kvm_s390_gisa *gisa, u32 gisc)
{
	return test_and_clear_bit_inv(IPM_BIT_OFFSET + gisc, (unsigned long *) gisa);
}

static inline unsigned long pending_irqs_no_gisa(struct kvm_vcpu *vcpu)
{
	unsigned long pending = vcpu->kvm->arch.float_int.pending_irqs |
				vcpu->arch.local_int.pending_irqs;

	pending &= ~vcpu->kvm->arch.float_int.masked_irqs;
	return pending;
}

static inline unsigned long pending_irqs(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_gisa_interrupt *gi = &vcpu->kvm->arch.gisa_int;
	unsigned long pending_mask;

	pending_mask = pending_irqs_no_gisa(vcpu);
	if (gi->origin)
		pending_mask |= gisa_get_ipm(gi->origin) << IRQ_PEND_IO_ISC_7;
	return pending_mask;
}

static inline int isc_to_irq_type(unsigned long isc)
{
	return IRQ_PEND_IO_ISC_0 - isc;
}

static inline int irq_type_to_isc(unsigned long irq_type)
{
	return IRQ_PEND_IO_ISC_0 - irq_type;
}

static unsigned long disable_iscs(struct kvm_vcpu *vcpu,
				   unsigned long active_mask)
{
	int i;

	for (i = 0; i <= MAX_ISC; i++)
		if (!(vcpu->arch.sie_block->gcr[6] & isc_to_isc_bits(i)))
			active_mask &= ~(1UL << (isc_to_irq_type(i)));

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
	if (!(vcpu->arch.sie_block->gcr[0] & CR0_EXTERNAL_CALL_SUBMASK))
		__clear_bit(IRQ_PEND_EXT_EXTERNAL, &active_mask);
	if (!(vcpu->arch.sie_block->gcr[0] & CR0_EMERGENCY_SIGNAL_SUBMASK))
		__clear_bit(IRQ_PEND_EXT_EMERGENCY, &active_mask);
	if (!(vcpu->arch.sie_block->gcr[0] & CR0_CLOCK_COMPARATOR_SUBMASK))
		__clear_bit(IRQ_PEND_EXT_CLOCK_COMP, &active_mask);
	if (!(vcpu->arch.sie_block->gcr[0] & CR0_CPU_TIMER_SUBMASK))
		__clear_bit(IRQ_PEND_EXT_CPU_TIMER, &active_mask);
	if (!(vcpu->arch.sie_block->gcr[0] & CR0_SERVICE_SIGNAL_SUBMASK)) {
		__clear_bit(IRQ_PEND_EXT_SERVICE, &active_mask);
		__clear_bit(IRQ_PEND_EXT_SERVICE_EV, &active_mask);
	}
	if (psw_mchk_disabled(vcpu))
		active_mask &= ~IRQ_PEND_MCHK_MASK;
	/* PV guest cpus can have a single interruption injected at a time. */
	if (kvm_s390_pv_cpu_get_handle(vcpu) &&
	    vcpu->arch.sie_block->iictl != IICTL_CODE_NONE)
		active_mask &= ~(IRQ_PEND_EXT_II_MASK |
				 IRQ_PEND_IO_MASK |
				 IRQ_PEND_MCHK_MASK);
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
	kvm_s390_set_cpuflags(vcpu, CPUSTAT_WAIT);
	set_bit(vcpu->vcpu_idx, vcpu->kvm->arch.idle_mask);
}

static void __unset_cpu_idle(struct kvm_vcpu *vcpu)
{
	kvm_s390_clear_cpuflags(vcpu, CPUSTAT_WAIT);
	clear_bit(vcpu->vcpu_idx, vcpu->kvm->arch.idle_mask);
}

static void __reset_intercept_indicators(struct kvm_vcpu *vcpu)
{
	kvm_s390_clear_cpuflags(vcpu, CPUSTAT_IO_INT | CPUSTAT_EXT_INT |
				      CPUSTAT_STOP_INT);
	vcpu->arch.sie_block->lctl = 0x0000;
	vcpu->arch.sie_block->ictl &= ~(ICTL_LPSW | ICTL_STCTL | ICTL_PINT);

	if (guestdbg_enabled(vcpu)) {
		vcpu->arch.sie_block->lctl |= (LCTL_CR0 | LCTL_CR9 |
					       LCTL_CR10 | LCTL_CR11);
		vcpu->arch.sie_block->ictl |= (ICTL_STCTL | ICTL_PINT);
	}
}

static void set_intercept_indicators_io(struct kvm_vcpu *vcpu)
{
	if (!(pending_irqs_no_gisa(vcpu) & IRQ_PEND_IO_MASK))
		return;
	if (psw_ioint_disabled(vcpu))
		kvm_s390_set_cpuflags(vcpu, CPUSTAT_IO_INT);
	else
		vcpu->arch.sie_block->lctl |= LCTL_CR6;
}

static void set_intercept_indicators_ext(struct kvm_vcpu *vcpu)
{
	if (!(pending_irqs_no_gisa(vcpu) & IRQ_PEND_EXT_MASK))
		return;
	if (psw_extint_disabled(vcpu))
		kvm_s390_set_cpuflags(vcpu, CPUSTAT_EXT_INT);
	else
		vcpu->arch.sie_block->lctl |= LCTL_CR0;
}

static void set_intercept_indicators_mchk(struct kvm_vcpu *vcpu)
{
	if (!(pending_irqs_no_gisa(vcpu) & IRQ_PEND_MCHK_MASK))
		return;
	if (psw_mchk_disabled(vcpu))
		vcpu->arch.sie_block->ictl |= ICTL_LPSW;
	else
		vcpu->arch.sie_block->lctl |= LCTL_CR14;
}

static void set_intercept_indicators_stop(struct kvm_vcpu *vcpu)
{
	if (kvm_s390_is_stop_irq_pending(vcpu))
		kvm_s390_set_cpuflags(vcpu, CPUSTAT_STOP_INT);
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
	int rc = 0;

	vcpu->stat.deliver_cputm++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_INT_CPU_TIMER,
					 0, 0);
	if (kvm_s390_pv_cpu_is_protected(vcpu)) {
		vcpu->arch.sie_block->iictl = IICTL_CODE_EXT;
		vcpu->arch.sie_block->eic = EXT_IRQ_CPU_TIMER;
	} else {
		rc  = put_guest_lc(vcpu, EXT_IRQ_CPU_TIMER,
				   (u16 *)__LC_EXT_INT_CODE);
		rc |= put_guest_lc(vcpu, 0, (u16 *)__LC_EXT_CPU_ADDR);
		rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
				     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
		rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW,
				    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	}
	clear_bit(IRQ_PEND_EXT_CPU_TIMER, &li->pending_irqs);
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_ckc(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	int rc = 0;

	vcpu->stat.deliver_ckc++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_INT_CLOCK_COMP,
					 0, 0);
	if (kvm_s390_pv_cpu_is_protected(vcpu)) {
		vcpu->arch.sie_block->iictl = IICTL_CODE_EXT;
		vcpu->arch.sie_block->eic = EXT_IRQ_CLK_COMP;
	} else {
		rc  = put_guest_lc(vcpu, EXT_IRQ_CLK_COMP,
				   (u16 __user *)__LC_EXT_INT_CODE);
		rc |= put_guest_lc(vcpu, 0, (u16 *)__LC_EXT_CPU_ADDR);
		rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
				     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
		rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW,
				    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	}
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

	/*
	 * All other possible payload for a machine check (e.g. the register
	 * contents in the save area) will be handled by the ultravisor, as
	 * the hypervisor does not not have the needed information for
	 * protected guests.
	 */
	if (kvm_s390_pv_cpu_is_protected(vcpu)) {
		vcpu->arch.sie_block->iictl = IICTL_CODE_MCHK;
		vcpu->arch.sie_block->mcic = mchk->mcic;
		vcpu->arch.sie_block->faddr = mchk->failing_storage_address;
		vcpu->arch.sie_block->edc = mchk->ext_damage_code;
		return 0;
	}

	mci.val = mchk->mcic;
	/* take care of lazy register loading */
	kvm_s390_fpu_store(vcpu->run);
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
	if (cpu_has_vx()) {
		convert_vx_to_fp(fprs, (__vector128 *) vcpu->run->s.regs.vrs);
		rc |= write_guest_lc(vcpu, __LC_FPREGS_SAVE_AREA, fprs, 128);
	} else {
		rc |= write_guest_lc(vcpu, __LC_FPREGS_SAVE_AREA,
				     vcpu->run->s.regs.fprs, 128);
	}
	rc |= write_guest_lc(vcpu, __LC_GPREGS_SAVE_AREA,
			     vcpu->run->s.regs.gprs, 128);
	rc |= put_guest_lc(vcpu, vcpu->run->s.regs.fpc,
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
	 * Subsystem damage are the only two and are indicated by
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
		vcpu->stat.deliver_machine_check++;
		rc = __write_machine_check(vcpu, &mchk);
	}
	return rc;
}

static int __must_check __deliver_restart(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	int rc = 0;

	VCPU_EVENT(vcpu, 3, "%s", "deliver: cpu restart");
	vcpu->stat.deliver_restart_signal++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_RESTART, 0, 0);

	if (kvm_s390_pv_cpu_is_protected(vcpu)) {
		vcpu->arch.sie_block->iictl = IICTL_CODE_RESTART;
	} else {
		rc  = write_guest_lc(vcpu,
				     offsetof(struct lowcore, restart_old_psw),
				     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
		rc |= read_guest_lc(vcpu, offsetof(struct lowcore, restart_psw),
				    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	}
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
	if (kvm_s390_pv_cpu_is_protected(vcpu)) {
		vcpu->arch.sie_block->iictl = IICTL_CODE_EXT;
		vcpu->arch.sie_block->eic = EXT_IRQ_EMERGENCY_SIG;
		vcpu->arch.sie_block->extcpuaddr = cpu_addr;
		return 0;
	}

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
	if (kvm_s390_pv_cpu_is_protected(vcpu)) {
		vcpu->arch.sie_block->iictl = IICTL_CODE_EXT;
		vcpu->arch.sie_block->eic = EXT_IRQ_EXTERNAL_CALL;
		vcpu->arch.sie_block->extcpuaddr = extcall.code;
		return 0;
	}

	rc  = put_guest_lc(vcpu, EXT_IRQ_EXTERNAL_CALL,
			   (u16 *)__LC_EXT_INT_CODE);
	rc |= put_guest_lc(vcpu, extcall.code, (u16 *)__LC_EXT_CPU_ADDR);
	rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW, &vcpu->arch.sie_block->gpsw,
			    sizeof(psw_t));
	return rc ? -EFAULT : 0;
}

static int __deliver_prog_pv(struct kvm_vcpu *vcpu, u16 code)
{
	switch (code) {
	case PGM_SPECIFICATION:
		vcpu->arch.sie_block->iictl = IICTL_CODE_SPECIFICATION;
		break;
	case PGM_OPERAND:
		vcpu->arch.sie_block->iictl = IICTL_CODE_OPERAND;
		break;
	default:
		return -EINVAL;
	}
	return 0;
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
	vcpu->stat.deliver_program++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_PROGRAM_INT,
					 pgm_info.code, 0);

	/* PER is handled by the ultravisor */
	if (kvm_s390_pv_cpu_is_protected(vcpu))
		return __deliver_prog_pv(vcpu, pgm_info.code & ~PGM_PER);

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
		fallthrough;
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
				 (u64 *) __LC_PGM_LAST_BREAK);
	rc |= put_guest_lc(vcpu, pgm_info.code,
			   (u16 *)__LC_PGM_INT_CODE);
	rc |= write_guest_lc(vcpu, __LC_PGM_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_PGM_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	return rc ? -EFAULT : 0;
}

#define SCCB_MASK 0xFFFFFFF8
#define SCCB_EVENT_PENDING 0x3

static int write_sclp(struct kvm_vcpu *vcpu, u32 parm)
{
	int rc;

	if (kvm_s390_pv_cpu_get_handle(vcpu)) {
		vcpu->arch.sie_block->iictl = IICTL_CODE_EXT;
		vcpu->arch.sie_block->eic = EXT_IRQ_SERVICE_SIG;
		vcpu->arch.sie_block->eiparams = parm;
		return 0;
	}

	rc  = put_guest_lc(vcpu, EXT_IRQ_SERVICE_SIG, (u16 *)__LC_EXT_INT_CODE);
	rc |= put_guest_lc(vcpu, 0, (u16 *)__LC_EXT_CPU_ADDR);
	rc |= write_guest_lc(vcpu, __LC_EXT_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_EXT_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw, sizeof(psw_t));
	rc |= put_guest_lc(vcpu, parm,
			   (u32 *)__LC_EXT_PARAMS);

	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_service(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;
	struct kvm_s390_ext_info ext;

	spin_lock(&fi->lock);
	if (test_bit(IRQ_PEND_EXT_SERVICE, &fi->masked_irqs) ||
	    !(test_bit(IRQ_PEND_EXT_SERVICE, &fi->pending_irqs))) {
		spin_unlock(&fi->lock);
		return 0;
	}
	ext = fi->srv_signal;
	memset(&fi->srv_signal, 0, sizeof(ext));
	clear_bit(IRQ_PEND_EXT_SERVICE, &fi->pending_irqs);
	clear_bit(IRQ_PEND_EXT_SERVICE_EV, &fi->pending_irqs);
	if (kvm_s390_pv_cpu_is_protected(vcpu))
		set_bit(IRQ_PEND_EXT_SERVICE, &fi->masked_irqs);
	spin_unlock(&fi->lock);

	VCPU_EVENT(vcpu, 4, "deliver: sclp parameter 0x%x",
		   ext.ext_params);
	vcpu->stat.deliver_service_signal++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_INT_SERVICE,
					 ext.ext_params, 0);

	return write_sclp(vcpu, ext.ext_params);
}

static int __must_check __deliver_service_ev(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;
	struct kvm_s390_ext_info ext;

	spin_lock(&fi->lock);
	if (!(test_bit(IRQ_PEND_EXT_SERVICE_EV, &fi->pending_irqs))) {
		spin_unlock(&fi->lock);
		return 0;
	}
	ext = fi->srv_signal;
	/* only clear the event bits */
	fi->srv_signal.ext_params &= ~SCCB_EVENT_PENDING;
	clear_bit(IRQ_PEND_EXT_SERVICE_EV, &fi->pending_irqs);
	spin_unlock(&fi->lock);

	VCPU_EVENT(vcpu, 4, "%s", "deliver: sclp parameter event");
	vcpu->stat.deliver_service_signal++;
	trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id, KVM_S390_INT_SERVICE,
					 ext.ext_params, 0);

	return write_sclp(vcpu, ext.ext_params & SCCB_EVENT_PENDING);
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
		vcpu->stat.deliver_virtio++;
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

static int __do_deliver_io(struct kvm_vcpu *vcpu, struct kvm_s390_io_info *io)
{
	int rc;

	if (kvm_s390_pv_cpu_is_protected(vcpu)) {
		vcpu->arch.sie_block->iictl = IICTL_CODE_IO;
		vcpu->arch.sie_block->subchannel_id = io->subchannel_id;
		vcpu->arch.sie_block->subchannel_nr = io->subchannel_nr;
		vcpu->arch.sie_block->io_int_parm = io->io_int_parm;
		vcpu->arch.sie_block->io_int_word = io->io_int_word;
		return 0;
	}

	rc  = put_guest_lc(vcpu, io->subchannel_id, (u16 *)__LC_SUBCHANNEL_ID);
	rc |= put_guest_lc(vcpu, io->subchannel_nr, (u16 *)__LC_SUBCHANNEL_NR);
	rc |= put_guest_lc(vcpu, io->io_int_parm, (u32 *)__LC_IO_INT_PARM);
	rc |= put_guest_lc(vcpu, io->io_int_word, (u32 *)__LC_IO_INT_WORD);
	rc |= write_guest_lc(vcpu, __LC_IO_OLD_PSW,
			     &vcpu->arch.sie_block->gpsw,
			     sizeof(psw_t));
	rc |= read_guest_lc(vcpu, __LC_IO_NEW_PSW,
			    &vcpu->arch.sie_block->gpsw,
			    sizeof(psw_t));
	return rc ? -EFAULT : 0;
}

static int __must_check __deliver_io(struct kvm_vcpu *vcpu,
				     unsigned long irq_type)
{
	struct list_head *isc_list;
	struct kvm_s390_float_interrupt *fi;
	struct kvm_s390_gisa_interrupt *gi = &vcpu->kvm->arch.gisa_int;
	struct kvm_s390_interrupt_info *inti = NULL;
	struct kvm_s390_io_info io;
	u32 isc;
	int rc = 0;

	fi = &vcpu->kvm->arch.float_int;

	spin_lock(&fi->lock);
	isc = irq_type_to_isc(irq_type);
	isc_list = &fi->lists[isc];
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

		vcpu->stat.deliver_io++;
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
		rc = __do_deliver_io(vcpu, &(inti->io));
		kfree(inti);
		goto out;
	}

	if (gi->origin && gisa_tac_ipm_gisc(gi->origin, isc)) {
		/*
		 * in case an adapter interrupt was not delivered
		 * in SIE context KVM will handle the delivery
		 */
		VCPU_EVENT(vcpu, 4, "%s isc %u", "deliver: I/O (AI/gisa)", isc);
		memset(&io, 0, sizeof(io));
		io.io_int_word = isc_to_int_word(isc);
		vcpu->stat.deliver_io++;
		trace_kvm_s390_deliver_interrupt(vcpu->vcpu_id,
			KVM_S390_INT_IO(1, 0, 0, 0),
			((__u32)io.subchannel_id << 16) |
			io.subchannel_nr,
			((__u64)io.io_int_parm << 32) |
			io.io_int_word);
		rc = __do_deliver_io(vcpu, &io);
	}
out:
	return rc;
}

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
	    (vcpu->arch.sie_block->gcr[0] & CR0_EXTERNAL_CALL_SUBMASK))
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
	const u64 now = kvm_s390_get_tod_clock_fast(vcpu->kvm);
	const u64 ckc = vcpu->arch.sie_block->ckc;
	u64 cputm, sltime = 0;

	if (ckc_interrupts_enabled(vcpu)) {
		if (vcpu->arch.sie_block->gcr[0] & CR0_CLOCK_COMPARATOR_SIGN) {
			if ((s64)now < (s64)ckc)
				sltime = tod_to_ns((s64)ckc - (s64)now);
		} else if (now < ckc) {
			sltime = tod_to_ns(ckc - now);
		}
		/* already expired */
		if (!sltime)
			return 0;
		if (cpu_timer_interrupts_enabled(vcpu)) {
			cputm = kvm_s390_get_cpu_timer(vcpu);
			/* already expired? */
			if (cputm >> 63)
				return 0;
			return min_t(u64, sltime, tod_to_ns(cputm));
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
	struct kvm_s390_gisa_interrupt *gi = &vcpu->kvm->arch.gisa_int;
	u64 sltime;

	vcpu->stat.exit_wait_state++;

	/* fast path */
	if (kvm_arch_vcpu_runnable(vcpu))
		return 0;

	if (psw_interrupts_disabled(vcpu)) {
		VCPU_EVENT(vcpu, 3, "%s", "disabled wait");
		return -EOPNOTSUPP; /* disabled wait */
	}

	if (gi->origin &&
	    (gisa_get_ipm_or_restore_iam(gi) &
	     vcpu->arch.sie_block->gcr[6] >> 24))
		return 0;

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
	kvm_vcpu_srcu_read_unlock(vcpu);
	kvm_vcpu_halt(vcpu);
	vcpu->valid_wakeup = false;
	__unset_cpu_idle(vcpu);
	kvm_vcpu_srcu_read_lock(vcpu);

	hrtimer_cancel(&vcpu->arch.ckc_timer);
	return 0;
}

void kvm_s390_vcpu_wakeup(struct kvm_vcpu *vcpu)
{
	vcpu->valid_wakeup = true;
	kvm_vcpu_wake_up(vcpu);

	/*
	 * The VCPU might not be sleeping but rather executing VSIE. Let's
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
	int rc = 0;
	bool delivered = false;
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
		/* bits are in the reverse order of interrupt priority */
		irq_type = find_last_bit(&irqs, IRQ_PEND_COUNT);
		switch (irq_type) {
		case IRQ_PEND_IO_ISC_0:
		case IRQ_PEND_IO_ISC_1:
		case IRQ_PEND_IO_ISC_2:
		case IRQ_PEND_IO_ISC_3:
		case IRQ_PEND_IO_ISC_4:
		case IRQ_PEND_IO_ISC_5:
		case IRQ_PEND_IO_ISC_6:
		case IRQ_PEND_IO_ISC_7:
			rc = __deliver_io(vcpu, irq_type);
			break;
		case IRQ_PEND_MCHK_EX:
		case IRQ_PEND_MCHK_REP:
			rc = __deliver_machine_check(vcpu);
			break;
		case IRQ_PEND_PROG:
			rc = __deliver_prog(vcpu);
			break;
		case IRQ_PEND_EXT_EMERGENCY:
			rc = __deliver_emergency_signal(vcpu);
			break;
		case IRQ_PEND_EXT_EXTERNAL:
			rc = __deliver_external_call(vcpu);
			break;
		case IRQ_PEND_EXT_CLOCK_COMP:
			rc = __deliver_ckc(vcpu);
			break;
		case IRQ_PEND_EXT_CPU_TIMER:
			rc = __deliver_cpu_timer(vcpu);
			break;
		case IRQ_PEND_RESTART:
			rc = __deliver_restart(vcpu);
			break;
		case IRQ_PEND_SET_PREFIX:
			rc = __deliver_set_prefix(vcpu);
			break;
		case IRQ_PEND_PFAULT_INIT:
			rc = __deliver_pfault_init(vcpu);
			break;
		case IRQ_PEND_EXT_SERVICE:
			rc = __deliver_service(vcpu);
			break;
		case IRQ_PEND_EXT_SERVICE_EV:
			rc = __deliver_service_ev(vcpu);
			break;
		case IRQ_PEND_PFAULT_DONE:
			rc = __deliver_pfault_done(vcpu);
			break;
		case IRQ_PEND_VIRTIO:
			rc = __deliver_virtio(vcpu);
			break;
		default:
			WARN_ONCE(1, "Unknown pending irq type %ld", irq_type);
			clear_bit(irq_type, &li->pending_irqs);
		}
		delivered |= !rc;
	}

	/*
	 * We delivered at least one interrupt and modified the PC. Force a
	 * singlestep event now.
	 */
	if (delivered && guestdbg_sstep_enabled(vcpu)) {
		struct kvm_debug_exit_arch *debug_exit = &vcpu->run->debug.arch;

		debug_exit->addr = vcpu->arch.sie_block->gpsw.addr;
		debug_exit->type = KVM_SINGLESTEP;
		vcpu->guest_debug |= KVM_GUESTDBG_EXIT_PENDING;
	}

	set_intercept_indicators(vcpu);

	return rc;
}

static int __inject_prog(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	vcpu->stat.inject_program++;
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

	vcpu->stat.inject_pfault_init++;
	VCPU_EVENT(vcpu, 4, "inject: pfault init parameter block at 0x%llx",
		   irq->u.ext.ext_params2);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_PFAULT_INIT,
				   irq->u.ext.ext_params,
				   irq->u.ext.ext_params2);

	li->irq.ext = irq->u.ext;
	set_bit(IRQ_PEND_PFAULT_INIT, &li->pending_irqs);
	kvm_s390_set_cpuflags(vcpu, CPUSTAT_EXT_INT);
	return 0;
}

static int __inject_extcall(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_extcall_info *extcall = &li->irq.extcall;
	uint16_t src_id = irq->u.extcall.code;

	vcpu->stat.inject_external_call++;
	VCPU_EVENT(vcpu, 4, "inject: external call source-cpu:%u",
		   src_id);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_EXTERNAL_CALL,
				   src_id, 0);

	/* sending vcpu invalid */
	if (kvm_get_vcpu_by_id(vcpu->kvm, src_id) == NULL)
		return -EINVAL;

	if (sclp.has_sigpif && !kvm_s390_pv_cpu_get_handle(vcpu))
		return sca_inject_ext_call(vcpu, src_id);

	if (test_and_set_bit(IRQ_PEND_EXT_EXTERNAL, &li->pending_irqs))
		return -EBUSY;
	*extcall = irq->u.extcall;
	kvm_s390_set_cpuflags(vcpu, CPUSTAT_EXT_INT);
	return 0;
}

static int __inject_set_prefix(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_prefix_info *prefix = &li->irq.prefix;

	vcpu->stat.inject_set_prefix++;
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

	vcpu->stat.inject_stop_signal++;
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
	kvm_s390_set_cpuflags(vcpu, CPUSTAT_STOP_INT);
	return 0;
}

static int __inject_sigp_restart(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	vcpu->stat.inject_restart++;
	VCPU_EVENT(vcpu, 3, "%s", "inject: restart int");
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_RESTART, 0, 0);

	set_bit(IRQ_PEND_RESTART, &li->pending_irqs);
	return 0;
}

static int __inject_sigp_emergency(struct kvm_vcpu *vcpu,
				   struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	vcpu->stat.inject_emergency_signal++;
	VCPU_EVENT(vcpu, 4, "inject: emergency from cpu %u",
		   irq->u.emerg.code);
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_EMERGENCY,
				   irq->u.emerg.code, 0);

	/* sending vcpu invalid */
	if (kvm_get_vcpu_by_id(vcpu->kvm, irq->u.emerg.code) == NULL)
		return -EINVAL;

	set_bit(irq->u.emerg.code, li->sigp_emerg_pending);
	set_bit(IRQ_PEND_EXT_EMERGENCY, &li->pending_irqs);
	kvm_s390_set_cpuflags(vcpu, CPUSTAT_EXT_INT);
	return 0;
}

static int __inject_mchk(struct kvm_vcpu *vcpu, struct kvm_s390_irq *irq)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	struct kvm_s390_mchk_info *mchk = &li->irq.mchk;

	vcpu->stat.inject_mchk++;
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

	vcpu->stat.inject_ckc++;
	VCPU_EVENT(vcpu, 3, "%s", "inject: clock comparator external");
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_CLOCK_COMP,
				   0, 0);

	set_bit(IRQ_PEND_EXT_CLOCK_COMP, &li->pending_irqs);
	kvm_s390_set_cpuflags(vcpu, CPUSTAT_EXT_INT);
	return 0;
}

static int __inject_cpu_timer(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	vcpu->stat.inject_cputm++;
	VCPU_EVENT(vcpu, 3, "%s", "inject: cpu timer external");
	trace_kvm_s390_inject_vcpu(vcpu->vcpu_id, KVM_S390_INT_CPU_TIMER,
				   0, 0);

	set_bit(IRQ_PEND_EXT_CPU_TIMER, &li->pending_irqs);
	kvm_s390_set_cpuflags(vcpu, CPUSTAT_EXT_INT);
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
			clear_bit(isc_to_irq_type(isc), &fi->pending_irqs);
		spin_unlock(&fi->lock);
		return iter;
	}
	spin_unlock(&fi->lock);
	return NULL;
}

static struct kvm_s390_interrupt_info *get_top_io_int(struct kvm *kvm,
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

static int get_top_gisa_isc(struct kvm *kvm, u64 isc_mask, u32 schid)
{
	struct kvm_s390_gisa_interrupt *gi = &kvm->arch.gisa_int;
	unsigned long active_mask;
	int isc;

	if (schid)
		goto out;
	if (!gi->origin)
		goto out;

	active_mask = (isc_mask & gisa_get_ipm(gi->origin) << 24) << 32;
	while (active_mask) {
		isc = __fls(active_mask) ^ (BITS_PER_LONG - 1);
		if (gisa_tac_ipm_gisc(gi->origin, isc))
			return isc;
		clear_bit_inv(isc, &active_mask);
	}
out:
	return -EINVAL;
}

/*
 * Dequeue and return an I/O interrupt matching any of the interruption
 * subclasses as designated by the isc mask in cr6 and the schid (if != 0).
 * Take into account the interrupts pending in the interrupt list and in GISA.
 *
 * Note that for a guest that does not enable I/O interrupts
 * but relies on TPI, a flood of classic interrupts may starve
 * out adapter interrupts on the same isc. Linux does not do
 * that, and it is possible to work around the issue by configuring
 * different iscs for classic and adapter interrupts in the guest,
 * but we may want to revisit this in the future.
 */
struct kvm_s390_interrupt_info *kvm_s390_get_io_int(struct kvm *kvm,
						    u64 isc_mask, u32 schid)
{
	struct kvm_s390_gisa_interrupt *gi = &kvm->arch.gisa_int;
	struct kvm_s390_interrupt_info *inti, *tmp_inti;
	int isc;

	inti = get_top_io_int(kvm, isc_mask, schid);

	isc = get_top_gisa_isc(kvm, isc_mask, schid);
	if (isc < 0)
		/* no AI in GISA */
		goto out;

	if (!inti)
		/* AI in GISA but no classical IO int */
		goto gisa_out;

	/* both types of interrupts present */
	if (int_word_to_isc(inti->io.io_int_word) <= isc) {
		/* classical IO int with higher priority */
		gisa_set_ipm_gisc(gi->origin, isc);
		goto out;
	}
gisa_out:
	tmp_inti = kzalloc(sizeof(*inti), GFP_KERNEL_ACCOUNT);
	if (tmp_inti) {
		tmp_inti->type = KVM_S390_INT_IO(1, 0, 0, 0);
		tmp_inti->io.io_int_word = isc_to_int_word(isc);
		if (inti)
			kvm_s390_reinject_io_int(kvm, inti);
		inti = tmp_inti;
	} else
		gisa_set_ipm_gisc(gi->origin, isc);
out:
	return inti;
}

static int __inject_service(struct kvm *kvm,
			     struct kvm_s390_interrupt_info *inti)
{
	struct kvm_s390_float_interrupt *fi = &kvm->arch.float_int;

	kvm->stat.inject_service_signal++;
	spin_lock(&fi->lock);
	fi->srv_signal.ext_params |= inti->ext.ext_params & SCCB_EVENT_PENDING;

	/* We always allow events, track them separately from the sccb ints */
	if (fi->srv_signal.ext_params & SCCB_EVENT_PENDING)
		set_bit(IRQ_PEND_EXT_SERVICE_EV, &fi->pending_irqs);

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

	kvm->stat.inject_virtio++;
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

	kvm->stat.inject_pfault_done++;
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

	kvm->stat.inject_float_mchk++;
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
	struct kvm_s390_gisa_interrupt *gi = &kvm->arch.gisa_int;
	struct kvm_s390_float_interrupt *fi;
	struct list_head *list;
	int isc;

	kvm->stat.inject_io++;
	isc = int_word_to_isc(inti->io.io_int_word);

	/*
	 * We do not use the lock checking variant as this is just a
	 * performance optimization and we do not hold the lock here.
	 * This is ok as the code will pick interrupts from both "lists"
	 * for delivery.
	 */
	if (gi->origin && inti->type & KVM_S390_INT_IO_AI_MASK) {
		VM_EVENT(kvm, 4, "%s isc %1u", "inject: I/O (AI/gisa)", isc);
		gisa_set_ipm_gisc(gi->origin, isc);
		kfree(inti);
		return 0;
	}

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
	list = &fi->lists[FIRQ_LIST_IO_ISC_0 + isc];
	list_add_tail(&inti->list, list);
	set_bit(isc_to_irq_type(isc), &fi->pending_irqs);
	spin_unlock(&fi->lock);
	return 0;
}

/*
 * Find a destination VCPU for a floating irq and kick it.
 */
static void __floating_irq_kick(struct kvm *kvm, u64 type)
{
	struct kvm_vcpu *dst_vcpu;
	int sigcpu, online_vcpus, nr_tries = 0;

	online_vcpus = atomic_read(&kvm->online_vcpus);
	if (!online_vcpus)
		return;

	/* find idle VCPUs first, then round robin */
	sigcpu = find_first_bit(kvm->arch.idle_mask, online_vcpus);
	if (sigcpu == online_vcpus) {
		do {
			sigcpu = kvm->arch.float_int.next_rr_cpu++;
			kvm->arch.float_int.next_rr_cpu %= online_vcpus;
			/* avoid endless loops if all vcpus are stopped */
			if (nr_tries++ >= online_vcpus)
				return;
		} while (is_vcpu_stopped(kvm_get_vcpu(kvm, sigcpu)));
	}
	dst_vcpu = kvm_get_vcpu(kvm, sigcpu);

	/* make the VCPU drop out of the SIE, or wake it up if sleeping */
	switch (type) {
	case KVM_S390_MCHK:
		kvm_s390_set_cpuflags(dst_vcpu, CPUSTAT_STOP_INT);
		break;
	case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
		if (!(type & KVM_S390_INT_IO_AI_MASK &&
		      kvm->arch.gisa_int.origin) ||
		      kvm_s390_pv_cpu_get_handle(dst_vcpu))
			kvm_s390_set_cpuflags(dst_vcpu, CPUSTAT_IO_INT);
		break;
	default:
		kvm_s390_set_cpuflags(dst_vcpu, CPUSTAT_EXT_INT);
		break;
	}
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

	inti = kzalloc(sizeof(*inti), GFP_KERNEL_ACCOUNT);
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
	case KVM_S390_INT_PFAULT_INIT:
		irq->u.ext.ext_params = s390int->parm;
		irq->u.ext.ext_params2 = s390int->parm64;
		break;
	case KVM_S390_RESTART:
	case KVM_S390_INT_CLOCK_COMP:
	case KVM_S390_INT_CPU_TIMER:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int kvm_s390_is_stop_irq_pending(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	return test_bit(IRQ_PEND_SIGP_STOP, &li->pending_irqs);
}

int kvm_s390_is_restart_irq_pending(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;

	return test_bit(IRQ_PEND_RESTART, &li->pending_irqs);
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
		rc = __inject_sigp_restart(vcpu);
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

	mutex_lock(&kvm->lock);
	if (!kvm_s390_pv_is_protected(kvm))
		fi->masked_irqs = 0;
	mutex_unlock(&kvm->lock);
	spin_lock(&fi->lock);
	fi->pending_irqs = 0;
	memset(&fi->srv_signal, 0, sizeof(fi->srv_signal));
	memset(&fi->mchk, 0, sizeof(fi->mchk));
	for (i = 0; i < FIRQ_LIST_COUNT; i++)
		clear_irq_list(&fi->lists[i]);
	for (i = 0; i < FIRQ_MAX_COUNT; i++)
		fi->counters[i] = 0;
	spin_unlock(&fi->lock);
	kvm_s390_gisa_clear(kvm);
};

static int get_all_floating_irqs(struct kvm *kvm, u8 __user *usrbuf, u64 len)
{
	struct kvm_s390_gisa_interrupt *gi = &kvm->arch.gisa_int;
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

	if (gi->origin && gisa_get_ipm(gi->origin)) {
		for (i = 0; i <= MAX_ISC; i++) {
			if (n == max_irqs) {
				/* signal userspace to try again */
				ret = -ENOMEM;
				goto out_nolock;
			}
			if (gisa_tac_ipm_gisc(gi->origin, i)) {
				irq = (struct kvm_s390_irq *) &buf[n];
				irq->type = KVM_S390_INT_IO(1, 0, 0, 0);
				irq->u.io.io_int_word = isc_to_int_word(i);
				n++;
			}
		}
	}
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
	if (test_bit(IRQ_PEND_EXT_SERVICE, &fi->pending_irqs) ||
	    test_bit(IRQ_PEND_EXT_SERVICE_EV, &fi->pending_irqs)) {
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
out_nolock:
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
		return -EOPNOTSUPP;

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
		inti = kzalloc(sizeof(*inti), GFP_KERNEL_ACCOUNT);
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
	id = array_index_nospec(id, MAX_S390_IO_ADAPTERS);
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

	if (adapter_info.id >= MAX_S390_IO_ADAPTERS)
		return -EINVAL;

	adapter_info.id = array_index_nospec(adapter_info.id,
					     MAX_S390_IO_ADAPTERS);

	if (dev->kvm->arch.adapters[adapter_info.id] != NULL)
		return -EINVAL;

	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL_ACCOUNT);
	if (!adapter)
		return -ENOMEM;

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

void kvm_s390_destroy_adapters(struct kvm *kvm)
{
	int i;

	for (i = 0; i < MAX_S390_IO_ADAPTERS; i++)
		kfree(kvm->arch.adapters[i]);
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
	/*
	 * The following operations are no longer needed and therefore no-ops.
	 * The gpa to hva translation is done when an IRQ route is set up. The
	 * set_irq code uses get_user_pages_remote() to do the actual write.
	 */
	case KVM_S390_IO_ADAPTER_MAP:
	case KVM_S390_IO_ADAPTER_UNMAP:
		ret = 0;
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
	if (!schid)
		return -EINVAL;
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
		return -EOPNOTSUPP;

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
		.parm64 = isc_to_int_word(adapter->isc),
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
		return -EOPNOTSUPP;

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
	unsigned long i;
	struct kvm_vcpu *vcpu;

	switch (attr->group) {
	case KVM_DEV_FLIC_ENQUEUE:
		r = enqueue_floating_irq(dev, attr);
		break;
	case KVM_DEV_FLIC_CLEAR_IRQS:
		kvm_s390_clear_float_irqs(dev->kvm);
		break;
	case KVM_DEV_FLIC_APF_ENABLE:
		if (kvm_is_ucontrol(dev->kvm))
			return -EINVAL;
		dev->kvm->arch.gmap->pfault_enabled = 1;
		break;
	case KVM_DEV_FLIC_APF_DISABLE_WAIT:
		if (kvm_is_ucontrol(dev->kvm))
			return -EINVAL;
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

static struct page *get_map_page(struct kvm *kvm, u64 uaddr)
{
	struct page *page = NULL;

	mmap_read_lock(kvm->mm);
	get_user_pages_remote(kvm->mm, uaddr, 1, FOLL_WRITE,
			      &page, NULL);
	mmap_read_unlock(kvm->mm);
	return page;
}

static int adapter_indicators_set(struct kvm *kvm,
				  struct s390_io_adapter *adapter,
				  struct kvm_s390_adapter_int *adapter_int)
{
	unsigned long bit;
	int summary_set, idx;
	struct page *ind_page, *summary_page;
	void *map;

	ind_page = get_map_page(kvm, adapter_int->ind_addr);
	if (!ind_page)
		return -1;
	summary_page = get_map_page(kvm, adapter_int->summary_addr);
	if (!summary_page) {
		put_page(ind_page);
		return -1;
	}

	idx = srcu_read_lock(&kvm->srcu);
	map = page_address(ind_page);
	bit = get_ind_bit(adapter_int->ind_addr,
			  adapter_int->ind_offset, adapter->swap);
	set_bit(bit, map);
	mark_page_dirty(kvm, adapter_int->ind_addr >> PAGE_SHIFT);
	set_page_dirty_lock(ind_page);
	map = page_address(summary_page);
	bit = get_ind_bit(adapter_int->summary_addr,
			  adapter_int->summary_offset, adapter->swap);
	summary_set = test_and_set_bit(bit, map);
	mark_page_dirty(kvm, adapter_int->summary_addr >> PAGE_SHIFT);
	set_page_dirty_lock(summary_page);
	srcu_read_unlock(&kvm->srcu, idx);

	put_page(ind_page);
	put_page(summary_page);
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
	ret = adapter_indicators_set(kvm, adapter, &e->adapter);
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
	u64 uaddr_s, uaddr_i;
	int idx;

	switch (ue->type) {
	/* we store the userspace addresses instead of the guest addresses */
	case KVM_IRQ_ROUTING_S390_ADAPTER:
		if (kvm_is_ucontrol(kvm))
			return -EINVAL;
		e->set = set_adapter_int;

		idx = srcu_read_lock(&kvm->srcu);
		uaddr_s = gpa_to_hva(kvm, ue->u.adapter.summary_addr);
		uaddr_i = gpa_to_hva(kvm, ue->u.adapter.ind_addr);
		srcu_read_unlock(&kvm->srcu, idx);

		if (kvm_is_error_hva(uaddr_s) || kvm_is_error_hva(uaddr_i))
			return -EFAULT;
		e->adapter.summary_addr = uaddr_s;
		e->adapter.ind_addr = uaddr_i;
		e->adapter.summary_offset = ue->u.adapter.summary_offset;
		e->adapter.ind_offset = ue->u.adapter.ind_offset;
		e->adapter.adapter_id = ue->u.adapter.adapter_id;
		return 0;
	default:
		return -EINVAL;
	}
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
	DECLARE_BITMAP(sigp_emerg_pending, KVM_MAX_VCPUS);
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

static void __airqs_kick_single_vcpu(struct kvm *kvm, u8 deliverable_mask)
{
	int vcpu_idx, online_vcpus = atomic_read(&kvm->online_vcpus);
	struct kvm_s390_gisa_interrupt *gi = &kvm->arch.gisa_int;
	struct kvm_vcpu *vcpu;
	u8 vcpu_isc_mask;

	for_each_set_bit(vcpu_idx, kvm->arch.idle_mask, online_vcpus) {
		vcpu = kvm_get_vcpu(kvm, vcpu_idx);
		if (psw_ioint_disabled(vcpu))
			continue;
		vcpu_isc_mask = (u8)(vcpu->arch.sie_block->gcr[6] >> 24);
		if (deliverable_mask & vcpu_isc_mask) {
			/* lately kicked but not yet running */
			if (test_and_set_bit(vcpu_idx, gi->kicked_mask))
				return;
			kvm_s390_vcpu_wakeup(vcpu);
			return;
		}
	}
}

static enum hrtimer_restart gisa_vcpu_kicker(struct hrtimer *timer)
{
	struct kvm_s390_gisa_interrupt *gi =
		container_of(timer, struct kvm_s390_gisa_interrupt, timer);
	struct kvm *kvm =
		container_of(gi->origin, struct sie_page2, gisa)->kvm;
	u8 pending_mask;

	pending_mask = gisa_get_ipm_or_restore_iam(gi);
	if (pending_mask) {
		__airqs_kick_single_vcpu(kvm, pending_mask);
		hrtimer_forward_now(timer, ns_to_ktime(gi->expires));
		return HRTIMER_RESTART;
	}

	return HRTIMER_NORESTART;
}

#define NULL_GISA_ADDR 0x00000000UL
#define NONE_GISA_ADDR 0x00000001UL
#define GISA_ADDR_MASK 0xfffff000UL

static void process_gib_alert_list(void)
{
	struct kvm_s390_gisa_interrupt *gi;
	u32 final, gisa_phys, origin = 0UL;
	struct kvm_s390_gisa *gisa;
	struct kvm *kvm;

	do {
		/*
		 * If the NONE_GISA_ADDR is still stored in the alert list
		 * origin, we will leave the outer loop. No further GISA has
		 * been added to the alert list by millicode while processing
		 * the current alert list.
		 */
		final = (origin & NONE_GISA_ADDR);
		/*
		 * Cut off the alert list and store the NONE_GISA_ADDR in the
		 * alert list origin to avoid further GAL interruptions.
		 * A new alert list can be build up by millicode in parallel
		 * for guests not in the yet cut-off alert list. When in the
		 * final loop, store the NULL_GISA_ADDR instead. This will re-
		 * enable GAL interruptions on the host again.
		 */
		origin = xchg(&gib->alert_list_origin,
			      (!final) ? NONE_GISA_ADDR : NULL_GISA_ADDR);
		/*
		 * Loop through the just cut-off alert list and start the
		 * gisa timers to kick idle vcpus to consume the pending
		 * interruptions asap.
		 */
		while (origin & GISA_ADDR_MASK) {
			gisa_phys = origin;
			gisa = phys_to_virt(gisa_phys);
			origin = gisa->next_alert;
			gisa->next_alert = gisa_phys;
			kvm = container_of(gisa, struct sie_page2, gisa)->kvm;
			gi = &kvm->arch.gisa_int;
			if (hrtimer_active(&gi->timer))
				hrtimer_cancel(&gi->timer);
			hrtimer_start(&gi->timer, 0, HRTIMER_MODE_REL);
		}
	} while (!final);

}

void kvm_s390_gisa_clear(struct kvm *kvm)
{
	struct kvm_s390_gisa_interrupt *gi = &kvm->arch.gisa_int;

	if (!gi->origin)
		return;
	gisa_clear_ipm(gi->origin);
	VM_EVENT(kvm, 3, "gisa 0x%p cleared", gi->origin);
}

void kvm_s390_gisa_init(struct kvm *kvm)
{
	struct kvm_s390_gisa_interrupt *gi = &kvm->arch.gisa_int;

	if (!css_general_characteristics.aiv)
		return;
	gi->origin = &kvm->arch.sie_page2->gisa;
	gi->alert.mask = 0;
	spin_lock_init(&gi->alert.ref_lock);
	gi->expires = 50 * 1000; /* 50 usec */
	hrtimer_init(&gi->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	gi->timer.function = gisa_vcpu_kicker;
	memset(gi->origin, 0, sizeof(struct kvm_s390_gisa));
	gi->origin->next_alert = (u32)virt_to_phys(gi->origin);
	VM_EVENT(kvm, 3, "gisa 0x%p initialized", gi->origin);
}

void kvm_s390_gisa_enable(struct kvm *kvm)
{
	struct kvm_s390_gisa_interrupt *gi = &kvm->arch.gisa_int;
	struct kvm_vcpu *vcpu;
	unsigned long i;
	u32 gisa_desc;

	if (gi->origin)
		return;
	kvm_s390_gisa_init(kvm);
	gisa_desc = kvm_s390_get_gisa_desc(kvm);
	if (!gisa_desc)
		return;
	kvm_for_each_vcpu(i, vcpu, kvm) {
		mutex_lock(&vcpu->mutex);
		vcpu->arch.sie_block->gd = gisa_desc;
		vcpu->arch.sie_block->eca |= ECA_AIV;
		VCPU_EVENT(vcpu, 3, "AIV gisa format-%u enabled for cpu %03u",
			   vcpu->arch.sie_block->gd & 0x3, vcpu->vcpu_id);
		mutex_unlock(&vcpu->mutex);
	}
}

void kvm_s390_gisa_destroy(struct kvm *kvm)
{
	struct kvm_s390_gisa_interrupt *gi = &kvm->arch.gisa_int;
	struct kvm_s390_gisa *gisa = gi->origin;

	if (!gi->origin)
		return;
	WARN(gi->alert.mask != 0x00,
	     "unexpected non zero alert.mask 0x%02x",
	     gi->alert.mask);
	gi->alert.mask = 0x00;
	if (gisa_set_iam(gi->origin, gi->alert.mask))
		process_gib_alert_list();
	hrtimer_cancel(&gi->timer);
	gi->origin = NULL;
	VM_EVENT(kvm, 3, "gisa 0x%p destroyed", gisa);
}

void kvm_s390_gisa_disable(struct kvm *kvm)
{
	struct kvm_s390_gisa_interrupt *gi = &kvm->arch.gisa_int;
	struct kvm_vcpu *vcpu;
	unsigned long i;

	if (!gi->origin)
		return;
	kvm_for_each_vcpu(i, vcpu, kvm) {
		mutex_lock(&vcpu->mutex);
		vcpu->arch.sie_block->eca &= ~ECA_AIV;
		vcpu->arch.sie_block->gd = 0U;
		mutex_unlock(&vcpu->mutex);
		VCPU_EVENT(vcpu, 3, "AIV disabled for cpu %03u", vcpu->vcpu_id);
	}
	kvm_s390_gisa_destroy(kvm);
}

/**
 * kvm_s390_gisc_register - register a guest ISC
 *
 * @kvm:  the kernel vm to work with
 * @gisc: the guest interruption sub class to register
 *
 * The function extends the vm specific alert mask to use.
 * The effective IAM mask in the GISA is updated as well
 * in case the GISA is not part of the GIB alert list.
 * It will be updated latest when the IAM gets restored
 * by gisa_get_ipm_or_restore_iam().
 *
 * Returns: the nonspecific ISC (NISC) the gib alert mechanism
 *          has registered with the channel subsystem.
 *          -ENODEV in case the vm uses no GISA
 *          -ERANGE in case the guest ISC is invalid
 */
int kvm_s390_gisc_register(struct kvm *kvm, u32 gisc)
{
	struct kvm_s390_gisa_interrupt *gi = &kvm->arch.gisa_int;

	if (!gi->origin)
		return -ENODEV;
	if (gisc > MAX_ISC)
		return -ERANGE;

	spin_lock(&gi->alert.ref_lock);
	gi->alert.ref_count[gisc]++;
	if (gi->alert.ref_count[gisc] == 1) {
		gi->alert.mask |= 0x80 >> gisc;
		gisa_set_iam(gi->origin, gi->alert.mask);
	}
	spin_unlock(&gi->alert.ref_lock);

	return gib->nisc;
}
EXPORT_SYMBOL_GPL(kvm_s390_gisc_register);

/**
 * kvm_s390_gisc_unregister - unregister a guest ISC
 *
 * @kvm:  the kernel vm to work with
 * @gisc: the guest interruption sub class to register
 *
 * The function reduces the vm specific alert mask to use.
 * The effective IAM mask in the GISA is updated as well
 * in case the GISA is not part of the GIB alert list.
 * It will be updated latest when the IAM gets restored
 * by gisa_get_ipm_or_restore_iam().
 *
 * Returns: the nonspecific ISC (NISC) the gib alert mechanism
 *          has registered with the channel subsystem.
 *          -ENODEV in case the vm uses no GISA
 *          -ERANGE in case the guest ISC is invalid
 *          -EINVAL in case the guest ISC is not registered
 */
int kvm_s390_gisc_unregister(struct kvm *kvm, u32 gisc)
{
	struct kvm_s390_gisa_interrupt *gi = &kvm->arch.gisa_int;
	int rc = 0;

	if (!gi->origin)
		return -ENODEV;
	if (gisc > MAX_ISC)
		return -ERANGE;

	spin_lock(&gi->alert.ref_lock);
	if (gi->alert.ref_count[gisc] == 0) {
		rc = -EINVAL;
		goto out;
	}
	gi->alert.ref_count[gisc]--;
	if (gi->alert.ref_count[gisc] == 0) {
		gi->alert.mask &= ~(0x80 >> gisc);
		gisa_set_iam(gi->origin, gi->alert.mask);
	}
out:
	spin_unlock(&gi->alert.ref_lock);

	return rc;
}
EXPORT_SYMBOL_GPL(kvm_s390_gisc_unregister);

static void aen_host_forward(unsigned long si)
{
	struct kvm_s390_gisa_interrupt *gi;
	struct zpci_gaite *gaite;
	struct kvm *kvm;

	gaite = (struct zpci_gaite *)aift->gait +
		(si * sizeof(struct zpci_gaite));
	if (gaite->count == 0)
		return;
	if (gaite->aisb != 0)
		set_bit_inv(gaite->aisbo, phys_to_virt(gaite->aisb));

	kvm = kvm_s390_pci_si_to_kvm(aift, si);
	if (!kvm)
		return;
	gi = &kvm->arch.gisa_int;

	if (!(gi->origin->g1.simm & AIS_MODE_MASK(gaite->gisc)) ||
	    !(gi->origin->g1.nimm & AIS_MODE_MASK(gaite->gisc))) {
		gisa_set_ipm_gisc(gi->origin, gaite->gisc);
		if (hrtimer_active(&gi->timer))
			hrtimer_cancel(&gi->timer);
		hrtimer_start(&gi->timer, 0, HRTIMER_MODE_REL);
		kvm->stat.aen_forward++;
	}
}

static void aen_process_gait(u8 isc)
{
	bool found = false, first = true;
	union zpci_sic_iib iib = {{0}};
	unsigned long si, flags;

	spin_lock_irqsave(&aift->gait_lock, flags);

	if (!aift->gait) {
		spin_unlock_irqrestore(&aift->gait_lock, flags);
		return;
	}

	for (si = 0;;) {
		/* Scan adapter summary indicator bit vector */
		si = airq_iv_scan(aift->sbv, si, airq_iv_end(aift->sbv));
		if (si == -1UL) {
			if (first || found) {
				/* Re-enable interrupts. */
				zpci_set_irq_ctrl(SIC_IRQ_MODE_SINGLE, isc,
						  &iib);
				first = found = false;
			} else {
				/* Interrupts on and all bits processed */
				break;
			}
			found = false;
			si = 0;
			/* Scan again after re-enabling interrupts */
			continue;
		}
		found = true;
		aen_host_forward(si);
	}

	spin_unlock_irqrestore(&aift->gait_lock, flags);
}

static void gib_alert_irq_handler(struct airq_struct *airq,
				  struct tpi_info *tpi_info)
{
	struct tpi_adapter_info *info = (struct tpi_adapter_info *)tpi_info;

	inc_irq_stat(IRQIO_GAL);

	if ((info->forward || info->error) &&
	    IS_ENABLED(CONFIG_VFIO_PCI_ZDEV_KVM)) {
		aen_process_gait(info->isc);
		if (info->aism != 0)
			process_gib_alert_list();
	} else {
		process_gib_alert_list();
	}
}

static struct airq_struct gib_alert_irq = {
	.handler = gib_alert_irq_handler,
};

void kvm_s390_gib_destroy(void)
{
	if (!gib)
		return;
	if (kvm_s390_pci_interp_allowed() && aift) {
		mutex_lock(&aift->aift_lock);
		kvm_s390_pci_aen_exit();
		mutex_unlock(&aift->aift_lock);
	}
	chsc_sgib(0);
	unregister_adapter_interrupt(&gib_alert_irq);
	free_page((unsigned long)gib);
	gib = NULL;
}

int __init kvm_s390_gib_init(u8 nisc)
{
	u32 gib_origin;
	int rc = 0;

	if (!css_general_characteristics.aiv) {
		KVM_EVENT(3, "%s", "gib not initialized, no AIV facility");
		goto out;
	}

	gib = (struct kvm_s390_gib *)get_zeroed_page(GFP_KERNEL_ACCOUNT | GFP_DMA);
	if (!gib) {
		rc = -ENOMEM;
		goto out;
	}

	gib_alert_irq.isc = nisc;
	if (register_adapter_interrupt(&gib_alert_irq)) {
		pr_err("Registering the GIB alert interruption handler failed\n");
		rc = -EIO;
		goto out_free_gib;
	}
	/* adapter interrupts used for AP (applicable here) don't use the LSI */
	*gib_alert_irq.lsi_ptr = 0xff;

	gib->nisc = nisc;
	gib_origin = virt_to_phys(gib);
	if (chsc_sgib(gib_origin)) {
		pr_err("Associating the GIB with the AIV facility failed\n");
		free_page((unsigned long)gib);
		gib = NULL;
		rc = -EIO;
		goto out_unreg_gal;
	}

	if (kvm_s390_pci_interp_allowed()) {
		if (kvm_s390_pci_aen_init(nisc)) {
			pr_err("Initializing AEN for PCI failed\n");
			rc = -EIO;
			goto out_unreg_gal;
		}
	}

	KVM_EVENT(3, "gib 0x%p (nisc=%d) initialized", gib, gib->nisc);
	goto out;

out_unreg_gal:
	unregister_adapter_interrupt(&gib_alert_irq);
out_free_gib:
	free_page((unsigned long)gib);
	gib = NULL;
out:
	return rc;
}
