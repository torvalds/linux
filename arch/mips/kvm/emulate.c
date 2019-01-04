/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/MIPS: Instruction/Exception emulation
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/ktime.h>
#include <linux/kvm_host.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/memblock.h>
#include <linux/random.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/cacheops.h>
#include <asm/cpu-info.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/inst.h>

#undef CONFIG_MIPS_MT
#include <asm/r4kcache.h>
#define CONFIG_MIPS_MT

#include "interrupt.h"
#include "commpage.h"

#include "trace.h"

/*
 * Compute the return address and do emulate branch simulation, if required.
 * This function should be called only in branch delay slot active.
 */
static int kvm_compute_return_epc(struct kvm_vcpu *vcpu, unsigned long instpc,
				  unsigned long *out)
{
	unsigned int dspcontrol;
	union mips_instruction insn;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	long epc = instpc;
	long nextpc;
	int err;

	if (epc & 3) {
		kvm_err("%s: unaligned epc\n", __func__);
		return -EINVAL;
	}

	/* Read the instruction */
	err = kvm_get_badinstrp((u32 *)epc, vcpu, &insn.word);
	if (err)
		return err;

	switch (insn.i_format.opcode) {
		/* jr and jalr are in r_format format. */
	case spec_op:
		switch (insn.r_format.func) {
		case jalr_op:
			arch->gprs[insn.r_format.rd] = epc + 8;
			/* Fall through */
		case jr_op:
			nextpc = arch->gprs[insn.r_format.rs];
			break;
		default:
			return -EINVAL;
		}
		break;

		/*
		 * This group contains:
		 * bltz_op, bgez_op, bltzl_op, bgezl_op,
		 * bltzal_op, bgezal_op, bltzall_op, bgezall_op.
		 */
	case bcond_op:
		switch (insn.i_format.rt) {
		case bltz_op:
		case bltzl_op:
			if ((long)arch->gprs[insn.i_format.rs] < 0)
				epc = epc + 4 + (insn.i_format.simmediate << 2);
			else
				epc += 8;
			nextpc = epc;
			break;

		case bgez_op:
		case bgezl_op:
			if ((long)arch->gprs[insn.i_format.rs] >= 0)
				epc = epc + 4 + (insn.i_format.simmediate << 2);
			else
				epc += 8;
			nextpc = epc;
			break;

		case bltzal_op:
		case bltzall_op:
			arch->gprs[31] = epc + 8;
			if ((long)arch->gprs[insn.i_format.rs] < 0)
				epc = epc + 4 + (insn.i_format.simmediate << 2);
			else
				epc += 8;
			nextpc = epc;
			break;

		case bgezal_op:
		case bgezall_op:
			arch->gprs[31] = epc + 8;
			if ((long)arch->gprs[insn.i_format.rs] >= 0)
				epc = epc + 4 + (insn.i_format.simmediate << 2);
			else
				epc += 8;
			nextpc = epc;
			break;
		case bposge32_op:
			if (!cpu_has_dsp) {
				kvm_err("%s: DSP branch but not DSP ASE\n",
					__func__);
				return -EINVAL;
			}

			dspcontrol = rddsp(0x01);

			if (dspcontrol >= 32)
				epc = epc + 4 + (insn.i_format.simmediate << 2);
			else
				epc += 8;
			nextpc = epc;
			break;
		default:
			return -EINVAL;
		}
		break;

		/* These are unconditional and in j_format. */
	case jal_op:
		arch->gprs[31] = instpc + 8;
	case j_op:
		epc += 4;
		epc >>= 28;
		epc <<= 28;
		epc |= (insn.j_format.target << 2);
		nextpc = epc;
		break;

		/* These are conditional and in i_format. */
	case beq_op:
	case beql_op:
		if (arch->gprs[insn.i_format.rs] ==
		    arch->gprs[insn.i_format.rt])
			epc = epc + 4 + (insn.i_format.simmediate << 2);
		else
			epc += 8;
		nextpc = epc;
		break;

	case bne_op:
	case bnel_op:
		if (arch->gprs[insn.i_format.rs] !=
		    arch->gprs[insn.i_format.rt])
			epc = epc + 4 + (insn.i_format.simmediate << 2);
		else
			epc += 8;
		nextpc = epc;
		break;

	case blez_op:	/* POP06 */
#ifndef CONFIG_CPU_MIPSR6
	case blezl_op:	/* removed in R6 */
#endif
		if (insn.i_format.rt != 0)
			goto compact_branch;
		if ((long)arch->gprs[insn.i_format.rs] <= 0)
			epc = epc + 4 + (insn.i_format.simmediate << 2);
		else
			epc += 8;
		nextpc = epc;
		break;

	case bgtz_op:	/* POP07 */
#ifndef CONFIG_CPU_MIPSR6
	case bgtzl_op:	/* removed in R6 */
#endif
		if (insn.i_format.rt != 0)
			goto compact_branch;
		if ((long)arch->gprs[insn.i_format.rs] > 0)
			epc = epc + 4 + (insn.i_format.simmediate << 2);
		else
			epc += 8;
		nextpc = epc;
		break;

		/* And now the FPA/cp1 branch instructions. */
	case cop1_op:
		kvm_err("%s: unsupported cop1_op\n", __func__);
		return -EINVAL;

#ifdef CONFIG_CPU_MIPSR6
	/* R6 added the following compact branches with forbidden slots */
	case blezl_op:	/* POP26 */
	case bgtzl_op:	/* POP27 */
		/* only rt == 0 isn't compact branch */
		if (insn.i_format.rt != 0)
			goto compact_branch;
		return -EINVAL;
	case pop10_op:
	case pop30_op:
		/* only rs == rt == 0 is reserved, rest are compact branches */
		if (insn.i_format.rs != 0 || insn.i_format.rt != 0)
			goto compact_branch;
		return -EINVAL;
	case pop66_op:
	case pop76_op:
		/* only rs == 0 isn't compact branch */
		if (insn.i_format.rs != 0)
			goto compact_branch;
		return -EINVAL;
compact_branch:
		/*
		 * If we've hit an exception on the forbidden slot, then
		 * the branch must not have been taken.
		 */
		epc += 8;
		nextpc = epc;
		break;
#else
compact_branch:
		/* Fall through - Compact branches not supported before R6 */
#endif
	default:
		return -EINVAL;
	}

	*out = nextpc;
	return 0;
}

enum emulation_result update_pc(struct kvm_vcpu *vcpu, u32 cause)
{
	int err;

	if (cause & CAUSEF_BD) {
		err = kvm_compute_return_epc(vcpu, vcpu->arch.pc,
					     &vcpu->arch.pc);
		if (err)
			return EMULATE_FAIL;
	} else {
		vcpu->arch.pc += 4;
	}

	kvm_debug("update_pc(): New PC: %#lx\n", vcpu->arch.pc);

	return EMULATE_DONE;
}

/**
 * kvm_get_badinstr() - Get bad instruction encoding.
 * @opc:	Guest pointer to faulting instruction.
 * @vcpu:	KVM VCPU information.
 *
 * Gets the instruction encoding of the faulting instruction, using the saved
 * BadInstr register value if it exists, otherwise falling back to reading guest
 * memory at @opc.
 *
 * Returns:	The instruction encoding of the faulting instruction.
 */
int kvm_get_badinstr(u32 *opc, struct kvm_vcpu *vcpu, u32 *out)
{
	if (cpu_has_badinstr) {
		*out = vcpu->arch.host_cp0_badinstr;
		return 0;
	} else {
		return kvm_get_inst(opc, vcpu, out);
	}
}

/**
 * kvm_get_badinstrp() - Get bad prior instruction encoding.
 * @opc:	Guest pointer to prior faulting instruction.
 * @vcpu:	KVM VCPU information.
 *
 * Gets the instruction encoding of the prior faulting instruction (the branch
 * containing the delay slot which faulted), using the saved BadInstrP register
 * value if it exists, otherwise falling back to reading guest memory at @opc.
 *
 * Returns:	The instruction encoding of the prior faulting instruction.
 */
int kvm_get_badinstrp(u32 *opc, struct kvm_vcpu *vcpu, u32 *out)
{
	if (cpu_has_badinstrp) {
		*out = vcpu->arch.host_cp0_badinstrp;
		return 0;
	} else {
		return kvm_get_inst(opc, vcpu, out);
	}
}

/**
 * kvm_mips_count_disabled() - Find whether the CP0_Count timer is disabled.
 * @vcpu:	Virtual CPU.
 *
 * Returns:	1 if the CP0_Count timer is disabled by either the guest
 *		CP0_Cause.DC bit or the count_ctl.DC bit.
 *		0 otherwise (in which case CP0_Count timer is running).
 */
int kvm_mips_count_disabled(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;

	return	(vcpu->arch.count_ctl & KVM_REG_MIPS_COUNT_CTL_DC) ||
		(kvm_read_c0_guest_cause(cop0) & CAUSEF_DC);
}

/**
 * kvm_mips_ktime_to_count() - Scale ktime_t to a 32-bit count.
 *
 * Caches the dynamic nanosecond bias in vcpu->arch.count_dyn_bias.
 *
 * Assumes !kvm_mips_count_disabled(@vcpu) (guest CP0_Count timer is running).
 */
static u32 kvm_mips_ktime_to_count(struct kvm_vcpu *vcpu, ktime_t now)
{
	s64 now_ns, periods;
	u64 delta;

	now_ns = ktime_to_ns(now);
	delta = now_ns + vcpu->arch.count_dyn_bias;

	if (delta >= vcpu->arch.count_period) {
		/* If delta is out of safe range the bias needs adjusting */
		periods = div64_s64(now_ns, vcpu->arch.count_period);
		vcpu->arch.count_dyn_bias = -periods * vcpu->arch.count_period;
		/* Recalculate delta with new bias */
		delta = now_ns + vcpu->arch.count_dyn_bias;
	}

	/*
	 * We've ensured that:
	 *   delta < count_period
	 *
	 * Therefore the intermediate delta*count_hz will never overflow since
	 * at the boundary condition:
	 *   delta = count_period
	 *   delta = NSEC_PER_SEC * 2^32 / count_hz
	 *   delta * count_hz = NSEC_PER_SEC * 2^32
	 */
	return div_u64(delta * vcpu->arch.count_hz, NSEC_PER_SEC);
}

/**
 * kvm_mips_count_time() - Get effective current time.
 * @vcpu:	Virtual CPU.
 *
 * Get effective monotonic ktime. This is usually a straightforward ktime_get(),
 * except when the master disable bit is set in count_ctl, in which case it is
 * count_resume, i.e. the time that the count was disabled.
 *
 * Returns:	Effective monotonic ktime for CP0_Count.
 */
static inline ktime_t kvm_mips_count_time(struct kvm_vcpu *vcpu)
{
	if (unlikely(vcpu->arch.count_ctl & KVM_REG_MIPS_COUNT_CTL_DC))
		return vcpu->arch.count_resume;

	return ktime_get();
}

/**
 * kvm_mips_read_count_running() - Read the current count value as if running.
 * @vcpu:	Virtual CPU.
 * @now:	Kernel time to read CP0_Count at.
 *
 * Returns the current guest CP0_Count register at time @now and handles if the
 * timer interrupt is pending and hasn't been handled yet.
 *
 * Returns:	The current value of the guest CP0_Count register.
 */
static u32 kvm_mips_read_count_running(struct kvm_vcpu *vcpu, ktime_t now)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	ktime_t expires, threshold;
	u32 count, compare;
	int running;

	/* Calculate the biased and scaled guest CP0_Count */
	count = vcpu->arch.count_bias + kvm_mips_ktime_to_count(vcpu, now);
	compare = kvm_read_c0_guest_compare(cop0);

	/*
	 * Find whether CP0_Count has reached the closest timer interrupt. If
	 * not, we shouldn't inject it.
	 */
	if ((s32)(count - compare) < 0)
		return count;

	/*
	 * The CP0_Count we're going to return has already reached the closest
	 * timer interrupt. Quickly check if it really is a new interrupt by
	 * looking at whether the interval until the hrtimer expiry time is
	 * less than 1/4 of the timer period.
	 */
	expires = hrtimer_get_expires(&vcpu->arch.comparecount_timer);
	threshold = ktime_add_ns(now, vcpu->arch.count_period / 4);
	if (ktime_before(expires, threshold)) {
		/*
		 * Cancel it while we handle it so there's no chance of
		 * interference with the timeout handler.
		 */
		running = hrtimer_cancel(&vcpu->arch.comparecount_timer);

		/* Nothing should be waiting on the timeout */
		kvm_mips_callbacks->queue_timer_int(vcpu);

		/*
		 * Restart the timer if it was running based on the expiry time
		 * we read, so that we don't push it back 2 periods.
		 */
		if (running) {
			expires = ktime_add_ns(expires,
					       vcpu->arch.count_period);
			hrtimer_start(&vcpu->arch.comparecount_timer, expires,
				      HRTIMER_MODE_ABS);
		}
	}

	return count;
}

/**
 * kvm_mips_read_count() - Read the current count value.
 * @vcpu:	Virtual CPU.
 *
 * Read the current guest CP0_Count value, taking into account whether the timer
 * is stopped.
 *
 * Returns:	The current guest CP0_Count value.
 */
u32 kvm_mips_read_count(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;

	/* If count disabled just read static copy of count */
	if (kvm_mips_count_disabled(vcpu))
		return kvm_read_c0_guest_count(cop0);

	return kvm_mips_read_count_running(vcpu, ktime_get());
}

/**
 * kvm_mips_freeze_hrtimer() - Safely stop the hrtimer.
 * @vcpu:	Virtual CPU.
 * @count:	Output pointer for CP0_Count value at point of freeze.
 *
 * Freeze the hrtimer safely and return both the ktime and the CP0_Count value
 * at the point it was frozen. It is guaranteed that any pending interrupts at
 * the point it was frozen are handled, and none after that point.
 *
 * This is useful where the time/CP0_Count is needed in the calculation of the
 * new parameters.
 *
 * Assumes !kvm_mips_count_disabled(@vcpu) (guest CP0_Count timer is running).
 *
 * Returns:	The ktime at the point of freeze.
 */
ktime_t kvm_mips_freeze_hrtimer(struct kvm_vcpu *vcpu, u32 *count)
{
	ktime_t now;

	/* stop hrtimer before finding time */
	hrtimer_cancel(&vcpu->arch.comparecount_timer);
	now = ktime_get();

	/* find count at this point and handle pending hrtimer */
	*count = kvm_mips_read_count_running(vcpu, now);

	return now;
}

/**
 * kvm_mips_resume_hrtimer() - Resume hrtimer, updating expiry.
 * @vcpu:	Virtual CPU.
 * @now:	ktime at point of resume.
 * @count:	CP0_Count at point of resume.
 *
 * Resumes the timer and updates the timer expiry based on @now and @count.
 * This can be used in conjunction with kvm_mips_freeze_timer() when timer
 * parameters need to be changed.
 *
 * It is guaranteed that a timer interrupt immediately after resume will be
 * handled, but not if CP_Compare is exactly at @count. That case is already
 * handled by kvm_mips_freeze_timer().
 *
 * Assumes !kvm_mips_count_disabled(@vcpu) (guest CP0_Count timer is running).
 */
static void kvm_mips_resume_hrtimer(struct kvm_vcpu *vcpu,
				    ktime_t now, u32 count)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	u32 compare;
	u64 delta;
	ktime_t expire;

	/* Calculate timeout (wrap 0 to 2^32) */
	compare = kvm_read_c0_guest_compare(cop0);
	delta = (u64)(u32)(compare - count - 1) + 1;
	delta = div_u64(delta * NSEC_PER_SEC, vcpu->arch.count_hz);
	expire = ktime_add_ns(now, delta);

	/* Update hrtimer to use new timeout */
	hrtimer_cancel(&vcpu->arch.comparecount_timer);
	hrtimer_start(&vcpu->arch.comparecount_timer, expire, HRTIMER_MODE_ABS);
}

/**
 * kvm_mips_restore_hrtimer() - Restore hrtimer after a gap, updating expiry.
 * @vcpu:	Virtual CPU.
 * @before:	Time before Count was saved, lower bound of drift calculation.
 * @count:	CP0_Count at point of restore.
 * @min_drift:	Minimum amount of drift permitted before correction.
 *		Must be <= 0.
 *
 * Restores the timer from a particular @count, accounting for drift. This can
 * be used in conjunction with kvm_mips_freeze_timer() when a hardware timer is
 * to be used for a period of time, but the exact ktime corresponding to the
 * final Count that must be restored is not known.
 *
 * It is gauranteed that a timer interrupt immediately after restore will be
 * handled, but not if CP0_Compare is exactly at @count. That case should
 * already be handled when the hardware timer state is saved.
 *
 * Assumes !kvm_mips_count_disabled(@vcpu) (guest CP0_Count timer is not
 * stopped).
 *
 * Returns:	Amount of correction to count_bias due to drift.
 */
int kvm_mips_restore_hrtimer(struct kvm_vcpu *vcpu, ktime_t before,
			     u32 count, int min_drift)
{
	ktime_t now, count_time;
	u32 now_count, before_count;
	u64 delta;
	int drift, ret = 0;

	/* Calculate expected count at before */
	before_count = vcpu->arch.count_bias +
			kvm_mips_ktime_to_count(vcpu, before);

	/*
	 * Detect significantly negative drift, where count is lower than
	 * expected. Some negative drift is expected when hardware counter is
	 * set after kvm_mips_freeze_timer(), and it is harmless to allow the
	 * time to jump forwards a little, within reason. If the drift is too
	 * significant, adjust the bias to avoid a big Guest.CP0_Count jump.
	 */
	drift = count - before_count;
	if (drift < min_drift) {
		count_time = before;
		vcpu->arch.count_bias += drift;
		ret = drift;
		goto resume;
	}

	/* Calculate expected count right now */
	now = ktime_get();
	now_count = vcpu->arch.count_bias + kvm_mips_ktime_to_count(vcpu, now);

	/*
	 * Detect positive drift, where count is higher than expected, and
	 * adjust the bias to avoid guest time going backwards.
	 */
	drift = count - now_count;
	if (drift > 0) {
		count_time = now;
		vcpu->arch.count_bias += drift;
		ret = drift;
		goto resume;
	}

	/* Subtract nanosecond delta to find ktime when count was read */
	delta = (u64)(u32)(now_count - count);
	delta = div_u64(delta * NSEC_PER_SEC, vcpu->arch.count_hz);
	count_time = ktime_sub_ns(now, delta);

resume:
	/* Resume using the calculated ktime */
	kvm_mips_resume_hrtimer(vcpu, count_time, count);
	return ret;
}

/**
 * kvm_mips_write_count() - Modify the count and update timer.
 * @vcpu:	Virtual CPU.
 * @count:	Guest CP0_Count value to set.
 *
 * Sets the CP0_Count value and updates the timer accordingly.
 */
void kvm_mips_write_count(struct kvm_vcpu *vcpu, u32 count)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	ktime_t now;

	/* Calculate bias */
	now = kvm_mips_count_time(vcpu);
	vcpu->arch.count_bias = count - kvm_mips_ktime_to_count(vcpu, now);

	if (kvm_mips_count_disabled(vcpu))
		/* The timer's disabled, adjust the static count */
		kvm_write_c0_guest_count(cop0, count);
	else
		/* Update timeout */
		kvm_mips_resume_hrtimer(vcpu, now, count);
}

/**
 * kvm_mips_init_count() - Initialise timer.
 * @vcpu:	Virtual CPU.
 * @count_hz:	Frequency of timer.
 *
 * Initialise the timer to the specified frequency, zero it, and set it going if
 * it's enabled.
 */
void kvm_mips_init_count(struct kvm_vcpu *vcpu, unsigned long count_hz)
{
	vcpu->arch.count_hz = count_hz;
	vcpu->arch.count_period = div_u64((u64)NSEC_PER_SEC << 32, count_hz);
	vcpu->arch.count_dyn_bias = 0;

	/* Starting at 0 */
	kvm_mips_write_count(vcpu, 0);
}

/**
 * kvm_mips_set_count_hz() - Update the frequency of the timer.
 * @vcpu:	Virtual CPU.
 * @count_hz:	Frequency of CP0_Count timer in Hz.
 *
 * Change the frequency of the CP0_Count timer. This is done atomically so that
 * CP0_Count is continuous and no timer interrupt is lost.
 *
 * Returns:	-EINVAL if @count_hz is out of range.
 *		0 on success.
 */
int kvm_mips_set_count_hz(struct kvm_vcpu *vcpu, s64 count_hz)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	int dc;
	ktime_t now;
	u32 count;

	/* ensure the frequency is in a sensible range... */
	if (count_hz <= 0 || count_hz > NSEC_PER_SEC)
		return -EINVAL;
	/* ... and has actually changed */
	if (vcpu->arch.count_hz == count_hz)
		return 0;

	/* Safely freeze timer so we can keep it continuous */
	dc = kvm_mips_count_disabled(vcpu);
	if (dc) {
		now = kvm_mips_count_time(vcpu);
		count = kvm_read_c0_guest_count(cop0);
	} else {
		now = kvm_mips_freeze_hrtimer(vcpu, &count);
	}

	/* Update the frequency */
	vcpu->arch.count_hz = count_hz;
	vcpu->arch.count_period = div_u64((u64)NSEC_PER_SEC << 32, count_hz);
	vcpu->arch.count_dyn_bias = 0;

	/* Calculate adjusted bias so dynamic count is unchanged */
	vcpu->arch.count_bias = count - kvm_mips_ktime_to_count(vcpu, now);

	/* Update and resume hrtimer */
	if (!dc)
		kvm_mips_resume_hrtimer(vcpu, now, count);
	return 0;
}

/**
 * kvm_mips_write_compare() - Modify compare and update timer.
 * @vcpu:	Virtual CPU.
 * @compare:	New CP0_Compare value.
 * @ack:	Whether to acknowledge timer interrupt.
 *
 * Update CP0_Compare to a new value and update the timeout.
 * If @ack, atomically acknowledge any pending timer interrupt, otherwise ensure
 * any pending timer interrupt is preserved.
 */
void kvm_mips_write_compare(struct kvm_vcpu *vcpu, u32 compare, bool ack)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	int dc;
	u32 old_compare = kvm_read_c0_guest_compare(cop0);
	s32 delta = compare - old_compare;
	u32 cause;
	ktime_t now = ktime_set(0, 0); /* silence bogus GCC warning */
	u32 count;

	/* if unchanged, must just be an ack */
	if (old_compare == compare) {
		if (!ack)
			return;
		kvm_mips_callbacks->dequeue_timer_int(vcpu);
		kvm_write_c0_guest_compare(cop0, compare);
		return;
	}

	/*
	 * If guest CP0_Compare moves forward, CP0_GTOffset should be adjusted
	 * too to prevent guest CP0_Count hitting guest CP0_Compare.
	 *
	 * The new GTOffset corresponds to the new value of CP0_Compare, and is
	 * set prior to it being written into the guest context. We disable
	 * preemption until the new value is written to prevent restore of a
	 * GTOffset corresponding to the old CP0_Compare value.
	 */
	if (IS_ENABLED(CONFIG_KVM_MIPS_VZ) && delta > 0) {
		preempt_disable();
		write_c0_gtoffset(compare - read_c0_count());
		back_to_back_c0_hazard();
	}

	/* freeze_hrtimer() takes care of timer interrupts <= count */
	dc = kvm_mips_count_disabled(vcpu);
	if (!dc)
		now = kvm_mips_freeze_hrtimer(vcpu, &count);

	if (ack)
		kvm_mips_callbacks->dequeue_timer_int(vcpu);
	else if (IS_ENABLED(CONFIG_KVM_MIPS_VZ))
		/*
		 * With VZ, writing CP0_Compare acks (clears) CP0_Cause.TI, so
		 * preserve guest CP0_Cause.TI if we don't want to ack it.
		 */
		cause = kvm_read_c0_guest_cause(cop0);

	kvm_write_c0_guest_compare(cop0, compare);

	if (IS_ENABLED(CONFIG_KVM_MIPS_VZ)) {
		if (delta > 0)
			preempt_enable();

		back_to_back_c0_hazard();

		if (!ack && cause & CAUSEF_TI)
			kvm_write_c0_guest_cause(cop0, cause);
	}

	/* resume_hrtimer() takes care of timer interrupts > count */
	if (!dc)
		kvm_mips_resume_hrtimer(vcpu, now, count);

	/*
	 * If guest CP0_Compare is moving backward, we delay CP0_GTOffset change
	 * until after the new CP0_Compare is written, otherwise new guest
	 * CP0_Count could hit new guest CP0_Compare.
	 */
	if (IS_ENABLED(CONFIG_KVM_MIPS_VZ) && delta <= 0)
		write_c0_gtoffset(compare - read_c0_count());
}

/**
 * kvm_mips_count_disable() - Disable count.
 * @vcpu:	Virtual CPU.
 *
 * Disable the CP0_Count timer. A timer interrupt on or before the final stop
 * time will be handled but not after.
 *
 * Assumes CP0_Count was previously enabled but now Guest.CP0_Cause.DC or
 * count_ctl.DC has been set (count disabled).
 *
 * Returns:	The time that the timer was stopped.
 */
static ktime_t kvm_mips_count_disable(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	u32 count;
	ktime_t now;

	/* Stop hrtimer */
	hrtimer_cancel(&vcpu->arch.comparecount_timer);

	/* Set the static count from the dynamic count, handling pending TI */
	now = ktime_get();
	count = kvm_mips_read_count_running(vcpu, now);
	kvm_write_c0_guest_count(cop0, count);

	return now;
}

/**
 * kvm_mips_count_disable_cause() - Disable count using CP0_Cause.DC.
 * @vcpu:	Virtual CPU.
 *
 * Disable the CP0_Count timer and set CP0_Cause.DC. A timer interrupt on or
 * before the final stop time will be handled if the timer isn't disabled by
 * count_ctl.DC, but not after.
 *
 * Assumes CP0_Cause.DC is clear (count enabled).
 */
void kvm_mips_count_disable_cause(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;

	kvm_set_c0_guest_cause(cop0, CAUSEF_DC);
	if (!(vcpu->arch.count_ctl & KVM_REG_MIPS_COUNT_CTL_DC))
		kvm_mips_count_disable(vcpu);
}

/**
 * kvm_mips_count_enable_cause() - Enable count using CP0_Cause.DC.
 * @vcpu:	Virtual CPU.
 *
 * Enable the CP0_Count timer and clear CP0_Cause.DC. A timer interrupt after
 * the start time will be handled if the timer isn't disabled by count_ctl.DC,
 * potentially before even returning, so the caller should be careful with
 * ordering of CP0_Cause modifications so as not to lose it.
 *
 * Assumes CP0_Cause.DC is set (count disabled).
 */
void kvm_mips_count_enable_cause(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	u32 count;

	kvm_clear_c0_guest_cause(cop0, CAUSEF_DC);

	/*
	 * Set the dynamic count to match the static count.
	 * This starts the hrtimer if count_ctl.DC allows it.
	 * Otherwise it conveniently updates the biases.
	 */
	count = kvm_read_c0_guest_count(cop0);
	kvm_mips_write_count(vcpu, count);
}

/**
 * kvm_mips_set_count_ctl() - Update the count control KVM register.
 * @vcpu:	Virtual CPU.
 * @count_ctl:	Count control register new value.
 *
 * Set the count control KVM register. The timer is updated accordingly.
 *
 * Returns:	-EINVAL if reserved bits are set.
 *		0 on success.
 */
int kvm_mips_set_count_ctl(struct kvm_vcpu *vcpu, s64 count_ctl)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	s64 changed = count_ctl ^ vcpu->arch.count_ctl;
	s64 delta;
	ktime_t expire, now;
	u32 count, compare;

	/* Only allow defined bits to be changed */
	if (changed & ~(s64)(KVM_REG_MIPS_COUNT_CTL_DC))
		return -EINVAL;

	/* Apply new value */
	vcpu->arch.count_ctl = count_ctl;

	/* Master CP0_Count disable */
	if (changed & KVM_REG_MIPS_COUNT_CTL_DC) {
		/* Is CP0_Cause.DC already disabling CP0_Count? */
		if (kvm_read_c0_guest_cause(cop0) & CAUSEF_DC) {
			if (count_ctl & KVM_REG_MIPS_COUNT_CTL_DC)
				/* Just record the current time */
				vcpu->arch.count_resume = ktime_get();
		} else if (count_ctl & KVM_REG_MIPS_COUNT_CTL_DC) {
			/* disable timer and record current time */
			vcpu->arch.count_resume = kvm_mips_count_disable(vcpu);
		} else {
			/*
			 * Calculate timeout relative to static count at resume
			 * time (wrap 0 to 2^32).
			 */
			count = kvm_read_c0_guest_count(cop0);
			compare = kvm_read_c0_guest_compare(cop0);
			delta = (u64)(u32)(compare - count - 1) + 1;
			delta = div_u64(delta * NSEC_PER_SEC,
					vcpu->arch.count_hz);
			expire = ktime_add_ns(vcpu->arch.count_resume, delta);

			/* Handle pending interrupt */
			now = ktime_get();
			if (ktime_compare(now, expire) >= 0)
				/* Nothing should be waiting on the timeout */
				kvm_mips_callbacks->queue_timer_int(vcpu);

			/* Resume hrtimer without changing bias */
			count = kvm_mips_read_count_running(vcpu, now);
			kvm_mips_resume_hrtimer(vcpu, now, count);
		}
	}

	return 0;
}

/**
 * kvm_mips_set_count_resume() - Update the count resume KVM register.
 * @vcpu:		Virtual CPU.
 * @count_resume:	Count resume register new value.
 *
 * Set the count resume KVM register.
 *
 * Returns:	-EINVAL if out of valid range (0..now).
 *		0 on success.
 */
int kvm_mips_set_count_resume(struct kvm_vcpu *vcpu, s64 count_resume)
{
	/*
	 * It doesn't make sense for the resume time to be in the future, as it
	 * would be possible for the next interrupt to be more than a full
	 * period in the future.
	 */
	if (count_resume < 0 || count_resume > ktime_to_ns(ktime_get()))
		return -EINVAL;

	vcpu->arch.count_resume = ns_to_ktime(count_resume);
	return 0;
}

/**
 * kvm_mips_count_timeout() - Push timer forward on timeout.
 * @vcpu:	Virtual CPU.
 *
 * Handle an hrtimer event by push the hrtimer forward a period.
 *
 * Returns:	The hrtimer_restart value to return to the hrtimer subsystem.
 */
enum hrtimer_restart kvm_mips_count_timeout(struct kvm_vcpu *vcpu)
{
	/* Add the Count period to the current expiry time */
	hrtimer_add_expires_ns(&vcpu->arch.comparecount_timer,
			       vcpu->arch.count_period);
	return HRTIMER_RESTART;
}

enum emulation_result kvm_mips_emul_eret(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	enum emulation_result er = EMULATE_DONE;

	if (kvm_read_c0_guest_status(cop0) & ST0_ERL) {
		kvm_clear_c0_guest_status(cop0, ST0_ERL);
		vcpu->arch.pc = kvm_read_c0_guest_errorepc(cop0);
	} else if (kvm_read_c0_guest_status(cop0) & ST0_EXL) {
		kvm_debug("[%#lx] ERET to %#lx\n", vcpu->arch.pc,
			  kvm_read_c0_guest_epc(cop0));
		kvm_clear_c0_guest_status(cop0, ST0_EXL);
		vcpu->arch.pc = kvm_read_c0_guest_epc(cop0);

	} else {
		kvm_err("[%#lx] ERET when MIPS_SR_EXL|MIPS_SR_ERL == 0\n",
			vcpu->arch.pc);
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_emul_wait(struct kvm_vcpu *vcpu)
{
	kvm_debug("[%#lx] !!!WAIT!!! (%#lx)\n", vcpu->arch.pc,
		  vcpu->arch.pending_exceptions);

	++vcpu->stat.wait_exits;
	trace_kvm_exit(vcpu, KVM_TRACE_EXIT_WAIT);
	if (!vcpu->arch.pending_exceptions) {
		kvm_vz_lose_htimer(vcpu);
		vcpu->arch.wait = 1;
		kvm_vcpu_block(vcpu);

		/*
		 * We we are runnable, then definitely go off to user space to
		 * check if any I/O interrupts are pending.
		 */
		if (kvm_check_request(KVM_REQ_UNHALT, vcpu)) {
			kvm_clear_request(KVM_REQ_UNHALT, vcpu);
			vcpu->run->exit_reason = KVM_EXIT_IRQ_WINDOW_OPEN;
		}
	}

	return EMULATE_DONE;
}

static void kvm_mips_change_entryhi(struct kvm_vcpu *vcpu,
				    unsigned long entryhi)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct mm_struct *kern_mm = &vcpu->arch.guest_kernel_mm;
	int cpu, i;
	u32 nasid = entryhi & KVM_ENTRYHI_ASID;

	if (((kvm_read_c0_guest_entryhi(cop0) & KVM_ENTRYHI_ASID) != nasid)) {
		trace_kvm_asid_change(vcpu, kvm_read_c0_guest_entryhi(cop0) &
				      KVM_ENTRYHI_ASID, nasid);

		/*
		 * Flush entries from the GVA page tables.
		 * Guest user page table will get flushed lazily on re-entry to
		 * guest user if the guest ASID actually changes.
		 */
		kvm_mips_flush_gva_pt(kern_mm->pgd, KMF_KERN);

		/*
		 * Regenerate/invalidate kernel MMU context.
		 * The user MMU context will be regenerated lazily on re-entry
		 * to guest user if the guest ASID actually changes.
		 */
		preempt_disable();
		cpu = smp_processor_id();
		get_new_mmu_context(kern_mm, cpu);
		for_each_possible_cpu(i)
			if (i != cpu)
				cpu_context(i, kern_mm) = 0;
		preempt_enable();
	}
	kvm_write_c0_guest_entryhi(cop0, entryhi);
}

enum emulation_result kvm_mips_emul_tlbr(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_mips_tlb *tlb;
	unsigned long pc = vcpu->arch.pc;
	int index;

	index = kvm_read_c0_guest_index(cop0);
	if (index < 0 || index >= KVM_MIPS_GUEST_TLB_SIZE) {
		/* UNDEFINED */
		kvm_debug("[%#lx] TLBR Index %#x out of range\n", pc, index);
		index &= KVM_MIPS_GUEST_TLB_SIZE - 1;
	}

	tlb = &vcpu->arch.guest_tlb[index];
	kvm_write_c0_guest_pagemask(cop0, tlb->tlb_mask);
	kvm_write_c0_guest_entrylo0(cop0, tlb->tlb_lo[0]);
	kvm_write_c0_guest_entrylo1(cop0, tlb->tlb_lo[1]);
	kvm_mips_change_entryhi(vcpu, tlb->tlb_hi);

	return EMULATE_DONE;
}

/**
 * kvm_mips_invalidate_guest_tlb() - Indicates a change in guest MMU map.
 * @vcpu:	VCPU with changed mappings.
 * @tlb:	TLB entry being removed.
 *
 * This is called to indicate a single change in guest MMU mappings, so that we
 * can arrange TLB flushes on this and other CPUs.
 */
static void kvm_mips_invalidate_guest_tlb(struct kvm_vcpu *vcpu,
					  struct kvm_mips_tlb *tlb)
{
	struct mm_struct *kern_mm = &vcpu->arch.guest_kernel_mm;
	struct mm_struct *user_mm = &vcpu->arch.guest_user_mm;
	int cpu, i;
	bool user;

	/* No need to flush for entries which are already invalid */
	if (!((tlb->tlb_lo[0] | tlb->tlb_lo[1]) & ENTRYLO_V))
		return;
	/* Don't touch host kernel page tables or TLB mappings */
	if ((unsigned long)tlb->tlb_hi > 0x7fffffff)
		return;
	/* User address space doesn't need flushing for KSeg2/3 changes */
	user = tlb->tlb_hi < KVM_GUEST_KSEG0;

	preempt_disable();

	/* Invalidate page table entries */
	kvm_trap_emul_invalidate_gva(vcpu, tlb->tlb_hi & VPN2_MASK, user);

	/*
	 * Probe the shadow host TLB for the entry being overwritten, if one
	 * matches, invalidate it
	 */
	kvm_mips_host_tlb_inv(vcpu, tlb->tlb_hi, user, true);

	/* Invalidate the whole ASID on other CPUs */
	cpu = smp_processor_id();
	for_each_possible_cpu(i) {
		if (i == cpu)
			continue;
		if (user)
			cpu_context(i, user_mm) = 0;
		cpu_context(i, kern_mm) = 0;
	}

	preempt_enable();
}

/* Write Guest TLB Entry @ Index */
enum emulation_result kvm_mips_emul_tlbwi(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	int index = kvm_read_c0_guest_index(cop0);
	struct kvm_mips_tlb *tlb = NULL;
	unsigned long pc = vcpu->arch.pc;

	if (index < 0 || index >= KVM_MIPS_GUEST_TLB_SIZE) {
		kvm_debug("%s: illegal index: %d\n", __func__, index);
		kvm_debug("[%#lx] COP0_TLBWI [%d] (entryhi: %#lx, entrylo0: %#lx entrylo1: %#lx, mask: %#lx)\n",
			  pc, index, kvm_read_c0_guest_entryhi(cop0),
			  kvm_read_c0_guest_entrylo0(cop0),
			  kvm_read_c0_guest_entrylo1(cop0),
			  kvm_read_c0_guest_pagemask(cop0));
		index = (index & ~0x80000000) % KVM_MIPS_GUEST_TLB_SIZE;
	}

	tlb = &vcpu->arch.guest_tlb[index];

	kvm_mips_invalidate_guest_tlb(vcpu, tlb);

	tlb->tlb_mask = kvm_read_c0_guest_pagemask(cop0);
	tlb->tlb_hi = kvm_read_c0_guest_entryhi(cop0);
	tlb->tlb_lo[0] = kvm_read_c0_guest_entrylo0(cop0);
	tlb->tlb_lo[1] = kvm_read_c0_guest_entrylo1(cop0);

	kvm_debug("[%#lx] COP0_TLBWI [%d] (entryhi: %#lx, entrylo0: %#lx entrylo1: %#lx, mask: %#lx)\n",
		  pc, index, kvm_read_c0_guest_entryhi(cop0),
		  kvm_read_c0_guest_entrylo0(cop0),
		  kvm_read_c0_guest_entrylo1(cop0),
		  kvm_read_c0_guest_pagemask(cop0));

	return EMULATE_DONE;
}

/* Write Guest TLB Entry @ Random Index */
enum emulation_result kvm_mips_emul_tlbwr(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_mips_tlb *tlb = NULL;
	unsigned long pc = vcpu->arch.pc;
	int index;

	get_random_bytes(&index, sizeof(index));
	index &= (KVM_MIPS_GUEST_TLB_SIZE - 1);

	tlb = &vcpu->arch.guest_tlb[index];

	kvm_mips_invalidate_guest_tlb(vcpu, tlb);

	tlb->tlb_mask = kvm_read_c0_guest_pagemask(cop0);
	tlb->tlb_hi = kvm_read_c0_guest_entryhi(cop0);
	tlb->tlb_lo[0] = kvm_read_c0_guest_entrylo0(cop0);
	tlb->tlb_lo[1] = kvm_read_c0_guest_entrylo1(cop0);

	kvm_debug("[%#lx] COP0_TLBWR[%d] (entryhi: %#lx, entrylo0: %#lx entrylo1: %#lx)\n",
		  pc, index, kvm_read_c0_guest_entryhi(cop0),
		  kvm_read_c0_guest_entrylo0(cop0),
		  kvm_read_c0_guest_entrylo1(cop0));

	return EMULATE_DONE;
}

enum emulation_result kvm_mips_emul_tlbp(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	long entryhi = kvm_read_c0_guest_entryhi(cop0);
	unsigned long pc = vcpu->arch.pc;
	int index = -1;

	index = kvm_mips_guest_tlb_lookup(vcpu, entryhi);

	kvm_write_c0_guest_index(cop0, index);

	kvm_debug("[%#lx] COP0_TLBP (entryhi: %#lx), index: %d\n", pc, entryhi,
		  index);

	return EMULATE_DONE;
}

/**
 * kvm_mips_config1_wrmask() - Find mask of writable bits in guest Config1
 * @vcpu:	Virtual CPU.
 *
 * Finds the mask of bits which are writable in the guest's Config1 CP0
 * register, by userland (currently read-only to the guest).
 */
unsigned int kvm_mips_config1_wrmask(struct kvm_vcpu *vcpu)
{
	unsigned int mask = 0;

	/* Permit FPU to be present if FPU is supported */
	if (kvm_mips_guest_can_have_fpu(&vcpu->arch))
		mask |= MIPS_CONF1_FP;

	return mask;
}

/**
 * kvm_mips_config3_wrmask() - Find mask of writable bits in guest Config3
 * @vcpu:	Virtual CPU.
 *
 * Finds the mask of bits which are writable in the guest's Config3 CP0
 * register, by userland (currently read-only to the guest).
 */
unsigned int kvm_mips_config3_wrmask(struct kvm_vcpu *vcpu)
{
	/* Config4 and ULRI are optional */
	unsigned int mask = MIPS_CONF_M | MIPS_CONF3_ULRI;

	/* Permit MSA to be present if MSA is supported */
	if (kvm_mips_guest_can_have_msa(&vcpu->arch))
		mask |= MIPS_CONF3_MSA;

	return mask;
}

/**
 * kvm_mips_config4_wrmask() - Find mask of writable bits in guest Config4
 * @vcpu:	Virtual CPU.
 *
 * Finds the mask of bits which are writable in the guest's Config4 CP0
 * register, by userland (currently read-only to the guest).
 */
unsigned int kvm_mips_config4_wrmask(struct kvm_vcpu *vcpu)
{
	/* Config5 is optional */
	unsigned int mask = MIPS_CONF_M;

	/* KScrExist */
	mask |= 0xfc << MIPS_CONF4_KSCREXIST_SHIFT;

	return mask;
}

/**
 * kvm_mips_config5_wrmask() - Find mask of writable bits in guest Config5
 * @vcpu:	Virtual CPU.
 *
 * Finds the mask of bits which are writable in the guest's Config5 CP0
 * register, by the guest itself.
 */
unsigned int kvm_mips_config5_wrmask(struct kvm_vcpu *vcpu)
{
	unsigned int mask = 0;

	/* Permit MSAEn changes if MSA supported and enabled */
	if (kvm_mips_guest_has_msa(&vcpu->arch))
		mask |= MIPS_CONF5_MSAEN;

	/*
	 * Permit guest FPU mode changes if FPU is enabled and the relevant
	 * feature exists according to FIR register.
	 */
	if (kvm_mips_guest_has_fpu(&vcpu->arch)) {
		if (cpu_has_fre)
			mask |= MIPS_CONF5_FRE;
		/* We don't support UFR or UFE */
	}

	return mask;
}

enum emulation_result kvm_mips_emulate_CP0(union mips_instruction inst,
					   u32 *opc, u32 cause,
					   struct kvm_run *run,
					   struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	enum emulation_result er = EMULATE_DONE;
	u32 rt, rd, sel;
	unsigned long curr_pc;

	/*
	 * Update PC and hold onto current PC in case there is
	 * an error and we want to rollback the PC
	 */
	curr_pc = vcpu->arch.pc;
	er = update_pc(vcpu, cause);
	if (er == EMULATE_FAIL)
		return er;

	if (inst.co_format.co) {
		switch (inst.co_format.func) {
		case tlbr_op:	/*  Read indexed TLB entry  */
			er = kvm_mips_emul_tlbr(vcpu);
			break;
		case tlbwi_op:	/*  Write indexed  */
			er = kvm_mips_emul_tlbwi(vcpu);
			break;
		case tlbwr_op:	/*  Write random  */
			er = kvm_mips_emul_tlbwr(vcpu);
			break;
		case tlbp_op:	/* TLB Probe */
			er = kvm_mips_emul_tlbp(vcpu);
			break;
		case rfe_op:
			kvm_err("!!!COP0_RFE!!!\n");
			break;
		case eret_op:
			er = kvm_mips_emul_eret(vcpu);
			goto dont_update_pc;
		case wait_op:
			er = kvm_mips_emul_wait(vcpu);
			break;
		case hypcall_op:
			er = kvm_mips_emul_hypcall(vcpu, inst);
			break;
		}
	} else {
		rt = inst.c0r_format.rt;
		rd = inst.c0r_format.rd;
		sel = inst.c0r_format.sel;

		switch (inst.c0r_format.rs) {
		case mfc_op:
#ifdef CONFIG_KVM_MIPS_DEBUG_COP0_COUNTERS
			cop0->stat[rd][sel]++;
#endif
			/* Get reg */
			if ((rd == MIPS_CP0_COUNT) && (sel == 0)) {
				vcpu->arch.gprs[rt] =
				    (s32)kvm_mips_read_count(vcpu);
			} else if ((rd == MIPS_CP0_ERRCTL) && (sel == 0)) {
				vcpu->arch.gprs[rt] = 0x0;
#ifdef CONFIG_KVM_MIPS_DYN_TRANS
				kvm_mips_trans_mfc0(inst, opc, vcpu);
#endif
			} else {
				vcpu->arch.gprs[rt] = (s32)cop0->reg[rd][sel];

#ifdef CONFIG_KVM_MIPS_DYN_TRANS
				kvm_mips_trans_mfc0(inst, opc, vcpu);
#endif
			}

			trace_kvm_hwr(vcpu, KVM_TRACE_MFC0,
				      KVM_TRACE_COP0(rd, sel),
				      vcpu->arch.gprs[rt]);
			break;

		case dmfc_op:
			vcpu->arch.gprs[rt] = cop0->reg[rd][sel];

			trace_kvm_hwr(vcpu, KVM_TRACE_DMFC0,
				      KVM_TRACE_COP0(rd, sel),
				      vcpu->arch.gprs[rt]);
			break;

		case mtc_op:
#ifdef CONFIG_KVM_MIPS_DEBUG_COP0_COUNTERS
			cop0->stat[rd][sel]++;
#endif
			trace_kvm_hwr(vcpu, KVM_TRACE_MTC0,
				      KVM_TRACE_COP0(rd, sel),
				      vcpu->arch.gprs[rt]);

			if ((rd == MIPS_CP0_TLB_INDEX)
			    && (vcpu->arch.gprs[rt] >=
				KVM_MIPS_GUEST_TLB_SIZE)) {
				kvm_err("Invalid TLB Index: %ld",
					vcpu->arch.gprs[rt]);
				er = EMULATE_FAIL;
				break;
			}
			if ((rd == MIPS_CP0_PRID) && (sel == 1)) {
				/*
				 * Preserve core number, and keep the exception
				 * base in guest KSeg0.
				 */
				kvm_change_c0_guest_ebase(cop0, 0x1ffff000,
							  vcpu->arch.gprs[rt]);
			} else if (rd == MIPS_CP0_TLB_HI && sel == 0) {
				kvm_mips_change_entryhi(vcpu,
							vcpu->arch.gprs[rt]);
			}
			/* Are we writing to COUNT */
			else if ((rd == MIPS_CP0_COUNT) && (sel == 0)) {
				kvm_mips_write_count(vcpu, vcpu->arch.gprs[rt]);
				goto done;
			} else if ((rd == MIPS_CP0_COMPARE) && (sel == 0)) {
				/* If we are writing to COMPARE */
				/* Clear pending timer interrupt, if any */
				kvm_mips_write_compare(vcpu,
						       vcpu->arch.gprs[rt],
						       true);
			} else if ((rd == MIPS_CP0_STATUS) && (sel == 0)) {
				unsigned int old_val, val, change;

				old_val = kvm_read_c0_guest_status(cop0);
				val = vcpu->arch.gprs[rt];
				change = val ^ old_val;

				/* Make sure that the NMI bit is never set */
				val &= ~ST0_NMI;

				/*
				 * Don't allow CU1 or FR to be set unless FPU
				 * capability enabled and exists in guest
				 * configuration.
				 */
				if (!kvm_mips_guest_has_fpu(&vcpu->arch))
					val &= ~(ST0_CU1 | ST0_FR);

				/*
				 * Also don't allow FR to be set if host doesn't
				 * support it.
				 */
				if (!(current_cpu_data.fpu_id & MIPS_FPIR_F64))
					val &= ~ST0_FR;


				/* Handle changes in FPU mode */
				preempt_disable();

				/*
				 * FPU and Vector register state is made
				 * UNPREDICTABLE by a change of FR, so don't
				 * even bother saving it.
				 */
				if (change & ST0_FR)
					kvm_drop_fpu(vcpu);

				/*
				 * If MSA state is already live, it is undefined
				 * how it interacts with FR=0 FPU state, and we
				 * don't want to hit reserved instruction
				 * exceptions trying to save the MSA state later
				 * when CU=1 && FR=1, so play it safe and save
				 * it first.
				 */
				if (change & ST0_CU1 && !(val & ST0_FR) &&
				    vcpu->arch.aux_inuse & KVM_MIPS_AUX_MSA)
					kvm_lose_fpu(vcpu);

				/*
				 * Propagate CU1 (FPU enable) changes
				 * immediately if the FPU context is already
				 * loaded. When disabling we leave the context
				 * loaded so it can be quickly enabled again in
				 * the near future.
				 */
				if (change & ST0_CU1 &&
				    vcpu->arch.aux_inuse & KVM_MIPS_AUX_FPU)
					change_c0_status(ST0_CU1, val);

				preempt_enable();

				kvm_write_c0_guest_status(cop0, val);

#ifdef CONFIG_KVM_MIPS_DYN_TRANS
				/*
				 * If FPU present, we need CU1/FR bits to take
				 * effect fairly soon.
				 */
				if (!kvm_mips_guest_has_fpu(&vcpu->arch))
					kvm_mips_trans_mtc0(inst, opc, vcpu);
#endif
			} else if ((rd == MIPS_CP0_CONFIG) && (sel == 5)) {
				unsigned int old_val, val, change, wrmask;

				old_val = kvm_read_c0_guest_config5(cop0);
				val = vcpu->arch.gprs[rt];

				/* Only a few bits are writable in Config5 */
				wrmask = kvm_mips_config5_wrmask(vcpu);
				change = (val ^ old_val) & wrmask;
				val = old_val ^ change;


				/* Handle changes in FPU/MSA modes */
				preempt_disable();

				/*
				 * Propagate FRE changes immediately if the FPU
				 * context is already loaded.
				 */
				if (change & MIPS_CONF5_FRE &&
				    vcpu->arch.aux_inuse & KVM_MIPS_AUX_FPU)
					change_c0_config5(MIPS_CONF5_FRE, val);

				/*
				 * Propagate MSAEn changes immediately if the
				 * MSA context is already loaded. When disabling
				 * we leave the context loaded so it can be
				 * quickly enabled again in the near future.
				 */
				if (change & MIPS_CONF5_MSAEN &&
				    vcpu->arch.aux_inuse & KVM_MIPS_AUX_MSA)
					change_c0_config5(MIPS_CONF5_MSAEN,
							  val);

				preempt_enable();

				kvm_write_c0_guest_config5(cop0, val);
			} else if ((rd == MIPS_CP0_CAUSE) && (sel == 0)) {
				u32 old_cause, new_cause;

				old_cause = kvm_read_c0_guest_cause(cop0);
				new_cause = vcpu->arch.gprs[rt];
				/* Update R/W bits */
				kvm_change_c0_guest_cause(cop0, 0x08800300,
							  new_cause);
				/* DC bit enabling/disabling timer? */
				if ((old_cause ^ new_cause) & CAUSEF_DC) {
					if (new_cause & CAUSEF_DC)
						kvm_mips_count_disable_cause(vcpu);
					else
						kvm_mips_count_enable_cause(vcpu);
				}
			} else if ((rd == MIPS_CP0_HWRENA) && (sel == 0)) {
				u32 mask = MIPS_HWRENA_CPUNUM |
					   MIPS_HWRENA_SYNCISTEP |
					   MIPS_HWRENA_CC |
					   MIPS_HWRENA_CCRES;

				if (kvm_read_c0_guest_config3(cop0) &
				    MIPS_CONF3_ULRI)
					mask |= MIPS_HWRENA_ULR;
				cop0->reg[rd][sel] = vcpu->arch.gprs[rt] & mask;
			} else {
				cop0->reg[rd][sel] = vcpu->arch.gprs[rt];
#ifdef CONFIG_KVM_MIPS_DYN_TRANS
				kvm_mips_trans_mtc0(inst, opc, vcpu);
#endif
			}
			break;

		case dmtc_op:
			kvm_err("!!!!!!![%#lx]dmtc_op: rt: %d, rd: %d, sel: %d!!!!!!\n",
				vcpu->arch.pc, rt, rd, sel);
			trace_kvm_hwr(vcpu, KVM_TRACE_DMTC0,
				      KVM_TRACE_COP0(rd, sel),
				      vcpu->arch.gprs[rt]);
			er = EMULATE_FAIL;
			break;

		case mfmc0_op:
#ifdef KVM_MIPS_DEBUG_COP0_COUNTERS
			cop0->stat[MIPS_CP0_STATUS][0]++;
#endif
			if (rt != 0)
				vcpu->arch.gprs[rt] =
				    kvm_read_c0_guest_status(cop0);
			/* EI */
			if (inst.mfmc0_format.sc) {
				kvm_debug("[%#lx] mfmc0_op: EI\n",
					  vcpu->arch.pc);
				kvm_set_c0_guest_status(cop0, ST0_IE);
			} else {
				kvm_debug("[%#lx] mfmc0_op: DI\n",
					  vcpu->arch.pc);
				kvm_clear_c0_guest_status(cop0, ST0_IE);
			}

			break;

		case wrpgpr_op:
			{
				u32 css = cop0->reg[MIPS_CP0_STATUS][2] & 0xf;
				u32 pss =
				    (cop0->reg[MIPS_CP0_STATUS][2] >> 6) & 0xf;
				/*
				 * We don't support any shadow register sets, so
				 * SRSCtl[PSS] == SRSCtl[CSS] = 0
				 */
				if (css || pss) {
					er = EMULATE_FAIL;
					break;
				}
				kvm_debug("WRPGPR[%d][%d] = %#lx\n", pss, rd,
					  vcpu->arch.gprs[rt]);
				vcpu->arch.gprs[rd] = vcpu->arch.gprs[rt];
			}
			break;
		default:
			kvm_err("[%#lx]MachEmulateCP0: unsupported COP0, copz: 0x%x\n",
				vcpu->arch.pc, inst.c0r_format.rs);
			er = EMULATE_FAIL;
			break;
		}
	}

done:
	/* Rollback PC only if emulation was unsuccessful */
	if (er == EMULATE_FAIL)
		vcpu->arch.pc = curr_pc;

dont_update_pc:
	/*
	 * This is for special instructions whose emulation
	 * updates the PC, so do not overwrite the PC under
	 * any circumstances
	 */

	return er;
}

enum emulation_result kvm_mips_emulate_store(union mips_instruction inst,
					     u32 cause,
					     struct kvm_run *run,
					     struct kvm_vcpu *vcpu)
{
	enum emulation_result er;
	u32 rt;
	void *data = run->mmio.data;
	unsigned long curr_pc;

	/*
	 * Update PC and hold onto current PC in case there is
	 * an error and we want to rollback the PC
	 */
	curr_pc = vcpu->arch.pc;
	er = update_pc(vcpu, cause);
	if (er == EMULATE_FAIL)
		return er;

	rt = inst.i_format.rt;

	run->mmio.phys_addr = kvm_mips_callbacks->gva_to_gpa(
						vcpu->arch.host_cp0_badvaddr);
	if (run->mmio.phys_addr == KVM_INVALID_ADDR)
		goto out_fail;

	switch (inst.i_format.opcode) {
#if defined(CONFIG_64BIT) && defined(CONFIG_KVM_MIPS_VZ)
	case sd_op:
		run->mmio.len = 8;
		*(u64 *)data = vcpu->arch.gprs[rt];

		kvm_debug("[%#lx] OP_SD: eaddr: %#lx, gpr: %#lx, data: %#llx\n",
			  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
			  vcpu->arch.gprs[rt], *(u64 *)data);
		break;
#endif

	case sw_op:
		run->mmio.len = 4;
		*(u32 *)data = vcpu->arch.gprs[rt];

		kvm_debug("[%#lx] OP_SW: eaddr: %#lx, gpr: %#lx, data: %#x\n",
			  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
			  vcpu->arch.gprs[rt], *(u32 *)data);
		break;

	case sh_op:
		run->mmio.len = 2;
		*(u16 *)data = vcpu->arch.gprs[rt];

		kvm_debug("[%#lx] OP_SH: eaddr: %#lx, gpr: %#lx, data: %#x\n",
			  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
			  vcpu->arch.gprs[rt], *(u16 *)data);
		break;

	case sb_op:
		run->mmio.len = 1;
		*(u8 *)data = vcpu->arch.gprs[rt];

		kvm_debug("[%#lx] OP_SB: eaddr: %#lx, gpr: %#lx, data: %#x\n",
			  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
			  vcpu->arch.gprs[rt], *(u8 *)data);
		break;

	default:
		kvm_err("Store not yet supported (inst=0x%08x)\n",
			inst.word);
		goto out_fail;
	}

	run->mmio.is_write = 1;
	vcpu->mmio_needed = 1;
	vcpu->mmio_is_write = 1;
	return EMULATE_DO_MMIO;

out_fail:
	/* Rollback PC if emulation was unsuccessful */
	vcpu->arch.pc = curr_pc;
	return EMULATE_FAIL;
}

enum emulation_result kvm_mips_emulate_load(union mips_instruction inst,
					    u32 cause, struct kvm_run *run,
					    struct kvm_vcpu *vcpu)
{
	enum emulation_result er;
	unsigned long curr_pc;
	u32 op, rt;

	rt = inst.i_format.rt;
	op = inst.i_format.opcode;

	/*
	 * Find the resume PC now while we have safe and easy access to the
	 * prior branch instruction, and save it for
	 * kvm_mips_complete_mmio_load() to restore later.
	 */
	curr_pc = vcpu->arch.pc;
	er = update_pc(vcpu, cause);
	if (er == EMULATE_FAIL)
		return er;
	vcpu->arch.io_pc = vcpu->arch.pc;
	vcpu->arch.pc = curr_pc;

	vcpu->arch.io_gpr = rt;

	run->mmio.phys_addr = kvm_mips_callbacks->gva_to_gpa(
						vcpu->arch.host_cp0_badvaddr);
	if (run->mmio.phys_addr == KVM_INVALID_ADDR)
		return EMULATE_FAIL;

	vcpu->mmio_needed = 2;	/* signed */
	switch (op) {
#if defined(CONFIG_64BIT) && defined(CONFIG_KVM_MIPS_VZ)
	case ld_op:
		run->mmio.len = 8;
		break;

	case lwu_op:
		vcpu->mmio_needed = 1;	/* unsigned */
		/* fall through */
#endif
	case lw_op:
		run->mmio.len = 4;
		break;

	case lhu_op:
		vcpu->mmio_needed = 1;	/* unsigned */
		/* fall through */
	case lh_op:
		run->mmio.len = 2;
		break;

	case lbu_op:
		vcpu->mmio_needed = 1;	/* unsigned */
		/* fall through */
	case lb_op:
		run->mmio.len = 1;
		break;

	default:
		kvm_err("Load not yet supported (inst=0x%08x)\n",
			inst.word);
		vcpu->mmio_needed = 0;
		return EMULATE_FAIL;
	}

	run->mmio.is_write = 0;
	vcpu->mmio_is_write = 0;
	return EMULATE_DO_MMIO;
}

#ifndef CONFIG_KVM_MIPS_VZ
static enum emulation_result kvm_mips_guest_cache_op(int (*fn)(unsigned long),
						     unsigned long curr_pc,
						     unsigned long addr,
						     struct kvm_run *run,
						     struct kvm_vcpu *vcpu,
						     u32 cause)
{
	int err;

	for (;;) {
		/* Carefully attempt the cache operation */
		kvm_trap_emul_gva_lockless_begin(vcpu);
		err = fn(addr);
		kvm_trap_emul_gva_lockless_end(vcpu);

		if (likely(!err))
			return EMULATE_DONE;

		/*
		 * Try to handle the fault and retry, maybe we just raced with a
		 * GVA invalidation.
		 */
		switch (kvm_trap_emul_gva_fault(vcpu, addr, false)) {
		case KVM_MIPS_GVA:
		case KVM_MIPS_GPA:
			/* bad virtual or physical address */
			return EMULATE_FAIL;
		case KVM_MIPS_TLB:
			/* no matching guest TLB */
			vcpu->arch.host_cp0_badvaddr = addr;
			vcpu->arch.pc = curr_pc;
			kvm_mips_emulate_tlbmiss_ld(cause, NULL, run, vcpu);
			return EMULATE_EXCEPT;
		case KVM_MIPS_TLBINV:
			/* invalid matching guest TLB */
			vcpu->arch.host_cp0_badvaddr = addr;
			vcpu->arch.pc = curr_pc;
			kvm_mips_emulate_tlbinv_ld(cause, NULL, run, vcpu);
			return EMULATE_EXCEPT;
		default:
			break;
		};
	}
}

enum emulation_result kvm_mips_emulate_cache(union mips_instruction inst,
					     u32 *opc, u32 cause,
					     struct kvm_run *run,
					     struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
	u32 cache, op_inst, op, base;
	s16 offset;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	unsigned long va;
	unsigned long curr_pc;

	/*
	 * Update PC and hold onto current PC in case there is
	 * an error and we want to rollback the PC
	 */
	curr_pc = vcpu->arch.pc;
	er = update_pc(vcpu, cause);
	if (er == EMULATE_FAIL)
		return er;

	base = inst.i_format.rs;
	op_inst = inst.i_format.rt;
	if (cpu_has_mips_r6)
		offset = inst.spec3_format.simmediate;
	else
		offset = inst.i_format.simmediate;
	cache = op_inst & CacheOp_Cache;
	op = op_inst & CacheOp_Op;

	va = arch->gprs[base] + offset;

	kvm_debug("CACHE (cache: %#x, op: %#x, base[%d]: %#lx, offset: %#x\n",
		  cache, op, base, arch->gprs[base], offset);

	/*
	 * Treat INDEX_INV as a nop, basically issued by Linux on startup to
	 * invalidate the caches entirely by stepping through all the
	 * ways/indexes
	 */
	if (op == Index_Writeback_Inv) {
		kvm_debug("@ %#lx/%#lx CACHE (cache: %#x, op: %#x, base[%d]: %#lx, offset: %#x\n",
			  vcpu->arch.pc, vcpu->arch.gprs[31], cache, op, base,
			  arch->gprs[base], offset);

		if (cache == Cache_D) {
#ifdef CONFIG_CPU_R4K_CACHE_TLB
			r4k_blast_dcache();
#else
			switch (boot_cpu_type()) {
			case CPU_CAVIUM_OCTEON3:
				/* locally flush icache */
				local_flush_icache_range(0, 0);
				break;
			default:
				__flush_cache_all();
				break;
			}
#endif
		} else if (cache == Cache_I) {
#ifdef CONFIG_CPU_R4K_CACHE_TLB
			r4k_blast_icache();
#else
			switch (boot_cpu_type()) {
			case CPU_CAVIUM_OCTEON3:
				/* locally flush icache */
				local_flush_icache_range(0, 0);
				break;
			default:
				flush_icache_all();
				break;
			}
#endif
		} else {
			kvm_err("%s: unsupported CACHE INDEX operation\n",
				__func__);
			return EMULATE_FAIL;
		}

#ifdef CONFIG_KVM_MIPS_DYN_TRANS
		kvm_mips_trans_cache_index(inst, opc, vcpu);
#endif
		goto done;
	}

	/* XXXKYMA: Only a subset of cache ops are supported, used by Linux */
	if (op_inst == Hit_Writeback_Inv_D || op_inst == Hit_Invalidate_D) {
		/*
		 * Perform the dcache part of icache synchronisation on the
		 * guest's behalf.
		 */
		er = kvm_mips_guest_cache_op(protected_writeback_dcache_line,
					     curr_pc, va, run, vcpu, cause);
		if (er != EMULATE_DONE)
			goto done;
#ifdef CONFIG_KVM_MIPS_DYN_TRANS
		/*
		 * Replace the CACHE instruction, with a SYNCI, not the same,
		 * but avoids a trap
		 */
		kvm_mips_trans_cache_va(inst, opc, vcpu);
#endif
	} else if (op_inst == Hit_Invalidate_I) {
		/* Perform the icache synchronisation on the guest's behalf */
		er = kvm_mips_guest_cache_op(protected_writeback_dcache_line,
					     curr_pc, va, run, vcpu, cause);
		if (er != EMULATE_DONE)
			goto done;
		er = kvm_mips_guest_cache_op(protected_flush_icache_line,
					     curr_pc, va, run, vcpu, cause);
		if (er != EMULATE_DONE)
			goto done;

#ifdef CONFIG_KVM_MIPS_DYN_TRANS
		/* Replace the CACHE instruction, with a SYNCI */
		kvm_mips_trans_cache_va(inst, opc, vcpu);
#endif
	} else {
		kvm_err("NO-OP CACHE (cache: %#x, op: %#x, base[%d]: %#lx, offset: %#x\n",
			cache, op, base, arch->gprs[base], offset);
		er = EMULATE_FAIL;
	}

done:
	/* Rollback PC only if emulation was unsuccessful */
	if (er == EMULATE_FAIL)
		vcpu->arch.pc = curr_pc;
	/* Guest exception needs guest to resume */
	if (er == EMULATE_EXCEPT)
		er = EMULATE_DONE;

	return er;
}

enum emulation_result kvm_mips_emulate_inst(u32 cause, u32 *opc,
					    struct kvm_run *run,
					    struct kvm_vcpu *vcpu)
{
	union mips_instruction inst;
	enum emulation_result er = EMULATE_DONE;
	int err;

	/* Fetch the instruction. */
	if (cause & CAUSEF_BD)
		opc += 1;
	err = kvm_get_badinstr(opc, vcpu, &inst.word);
	if (err)
		return EMULATE_FAIL;

	switch (inst.r_format.opcode) {
	case cop0_op:
		er = kvm_mips_emulate_CP0(inst, opc, cause, run, vcpu);
		break;

#ifndef CONFIG_CPU_MIPSR6
	case cache_op:
		++vcpu->stat.cache_exits;
		trace_kvm_exit(vcpu, KVM_TRACE_EXIT_CACHE);
		er = kvm_mips_emulate_cache(inst, opc, cause, run, vcpu);
		break;
#else
	case spec3_op:
		switch (inst.spec3_format.func) {
		case cache6_op:
			++vcpu->stat.cache_exits;
			trace_kvm_exit(vcpu, KVM_TRACE_EXIT_CACHE);
			er = kvm_mips_emulate_cache(inst, opc, cause, run,
						    vcpu);
			break;
		default:
			goto unknown;
		};
		break;
unknown:
#endif

	default:
		kvm_err("Instruction emulation not supported (%p/%#x)\n", opc,
			inst.word);
		kvm_arch_vcpu_dump_regs(vcpu);
		er = EMULATE_FAIL;
		break;
	}

	return er;
}
#endif /* CONFIG_KVM_MIPS_VZ */

/**
 * kvm_mips_guest_exception_base() - Find guest exception vector base address.
 *
 * Returns:	The base address of the current guest exception vector, taking
 *		both Guest.CP0_Status.BEV and Guest.CP0_EBase into account.
 */
long kvm_mips_guest_exception_base(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;

	if (kvm_read_c0_guest_status(cop0) & ST0_BEV)
		return KVM_GUEST_CKSEG1ADDR(0x1fc00200);
	else
		return kvm_read_c0_guest_ebase(cop0) & MIPS_EBASE_BASE;
}

enum emulation_result kvm_mips_emulate_syscall(u32 cause,
					       u32 *opc,
					       struct kvm_run *run,
					       struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	enum emulation_result er = EMULATE_DONE;

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

		kvm_debug("Delivering SYSCALL @ pc %#lx\n", arch->pc);

		kvm_change_c0_guest_cause(cop0, (0xff),
					  (EXCCODE_SYS << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;

	} else {
		kvm_err("Trying to deliver SYSCALL when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_emulate_tlbmiss_ld(u32 cause,
						  u32 *opc,
						  struct kvm_run *run,
						  struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	unsigned long entryhi = (vcpu->arch.  host_cp0_badvaddr & VPN2_MASK) |
			(kvm_read_c0_guest_entryhi(cop0) & KVM_ENTRYHI_ASID);

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

		kvm_debug("[EXL == 0] delivering TLB MISS @ pc %#lx\n",
			  arch->pc);

		/* set pc to the exception entry point */
		arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x0;

	} else {
		kvm_debug("[EXL == 1] delivering TLB MISS @ pc %#lx\n",
			  arch->pc);

		arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;
	}

	kvm_change_c0_guest_cause(cop0, (0xff),
				  (EXCCODE_TLBL << CAUSEB_EXCCODE));

	/* setup badvaddr, context and entryhi registers for the guest */
	kvm_write_c0_guest_badvaddr(cop0, vcpu->arch.host_cp0_badvaddr);
	/* XXXKYMA: is the context register used by linux??? */
	kvm_write_c0_guest_entryhi(cop0, entryhi);

	return EMULATE_DONE;
}

enum emulation_result kvm_mips_emulate_tlbinv_ld(u32 cause,
						 u32 *opc,
						 struct kvm_run *run,
						 struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	unsigned long entryhi =
		(vcpu->arch.host_cp0_badvaddr & VPN2_MASK) |
		(kvm_read_c0_guest_entryhi(cop0) & KVM_ENTRYHI_ASID);

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

		kvm_debug("[EXL == 0] delivering TLB INV @ pc %#lx\n",
			  arch->pc);
	} else {
		kvm_debug("[EXL == 1] delivering TLB MISS @ pc %#lx\n",
			  arch->pc);
	}

	/* set pc to the exception entry point */
	arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;

	kvm_change_c0_guest_cause(cop0, (0xff),
				  (EXCCODE_TLBL << CAUSEB_EXCCODE));

	/* setup badvaddr, context and entryhi registers for the guest */
	kvm_write_c0_guest_badvaddr(cop0, vcpu->arch.host_cp0_badvaddr);
	/* XXXKYMA: is the context register used by linux??? */
	kvm_write_c0_guest_entryhi(cop0, entryhi);

	return EMULATE_DONE;
}

enum emulation_result kvm_mips_emulate_tlbmiss_st(u32 cause,
						  u32 *opc,
						  struct kvm_run *run,
						  struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	unsigned long entryhi = (vcpu->arch.host_cp0_badvaddr & VPN2_MASK) |
			(kvm_read_c0_guest_entryhi(cop0) & KVM_ENTRYHI_ASID);

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

		kvm_debug("[EXL == 0] Delivering TLB MISS @ pc %#lx\n",
			  arch->pc);

		/* Set PC to the exception entry point */
		arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x0;
	} else {
		kvm_debug("[EXL == 1] Delivering TLB MISS @ pc %#lx\n",
			  arch->pc);
		arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;
	}

	kvm_change_c0_guest_cause(cop0, (0xff),
				  (EXCCODE_TLBS << CAUSEB_EXCCODE));

	/* setup badvaddr, context and entryhi registers for the guest */
	kvm_write_c0_guest_badvaddr(cop0, vcpu->arch.host_cp0_badvaddr);
	/* XXXKYMA: is the context register used by linux??? */
	kvm_write_c0_guest_entryhi(cop0, entryhi);

	return EMULATE_DONE;
}

enum emulation_result kvm_mips_emulate_tlbinv_st(u32 cause,
						 u32 *opc,
						 struct kvm_run *run,
						 struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	unsigned long entryhi = (vcpu->arch.host_cp0_badvaddr & VPN2_MASK) |
		(kvm_read_c0_guest_entryhi(cop0) & KVM_ENTRYHI_ASID);

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

		kvm_debug("[EXL == 0] Delivering TLB MISS @ pc %#lx\n",
			  arch->pc);
	} else {
		kvm_debug("[EXL == 1] Delivering TLB MISS @ pc %#lx\n",
			  arch->pc);
	}

	/* Set PC to the exception entry point */
	arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;

	kvm_change_c0_guest_cause(cop0, (0xff),
				  (EXCCODE_TLBS << CAUSEB_EXCCODE));

	/* setup badvaddr, context and entryhi registers for the guest */
	kvm_write_c0_guest_badvaddr(cop0, vcpu->arch.host_cp0_badvaddr);
	/* XXXKYMA: is the context register used by linux??? */
	kvm_write_c0_guest_entryhi(cop0, entryhi);

	return EMULATE_DONE;
}

enum emulation_result kvm_mips_emulate_tlbmod(u32 cause,
					      u32 *opc,
					      struct kvm_run *run,
					      struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	unsigned long entryhi = (vcpu->arch.host_cp0_badvaddr & VPN2_MASK) |
			(kvm_read_c0_guest_entryhi(cop0) & KVM_ENTRYHI_ASID);
	struct kvm_vcpu_arch *arch = &vcpu->arch;

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

		kvm_debug("[EXL == 0] Delivering TLB MOD @ pc %#lx\n",
			  arch->pc);
	} else {
		kvm_debug("[EXL == 1] Delivering TLB MOD @ pc %#lx\n",
			  arch->pc);
	}

	arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;

	kvm_change_c0_guest_cause(cop0, (0xff),
				  (EXCCODE_MOD << CAUSEB_EXCCODE));

	/* setup badvaddr, context and entryhi registers for the guest */
	kvm_write_c0_guest_badvaddr(cop0, vcpu->arch.host_cp0_badvaddr);
	/* XXXKYMA: is the context register used by linux??? */
	kvm_write_c0_guest_entryhi(cop0, entryhi);

	return EMULATE_DONE;
}

enum emulation_result kvm_mips_emulate_fpu_exc(u32 cause,
					       u32 *opc,
					       struct kvm_run *run,
					       struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

	}

	arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;

	kvm_change_c0_guest_cause(cop0, (0xff),
				  (EXCCODE_CPU << CAUSEB_EXCCODE));
	kvm_change_c0_guest_cause(cop0, (CAUSEF_CE), (0x1 << CAUSEB_CE));

	return EMULATE_DONE;
}

enum emulation_result kvm_mips_emulate_ri_exc(u32 cause,
					      u32 *opc,
					      struct kvm_run *run,
					      struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	enum emulation_result er = EMULATE_DONE;

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

		kvm_debug("Delivering RI @ pc %#lx\n", arch->pc);

		kvm_change_c0_guest_cause(cop0, (0xff),
					  (EXCCODE_RI << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;

	} else {
		kvm_err("Trying to deliver RI when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_emulate_bp_exc(u32 cause,
					      u32 *opc,
					      struct kvm_run *run,
					      struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	enum emulation_result er = EMULATE_DONE;

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

		kvm_debug("Delivering BP @ pc %#lx\n", arch->pc);

		kvm_change_c0_guest_cause(cop0, (0xff),
					  (EXCCODE_BP << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;

	} else {
		kvm_err("Trying to deliver BP when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_emulate_trap_exc(u32 cause,
						u32 *opc,
						struct kvm_run *run,
						struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	enum emulation_result er = EMULATE_DONE;

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

		kvm_debug("Delivering TRAP @ pc %#lx\n", arch->pc);

		kvm_change_c0_guest_cause(cop0, (0xff),
					  (EXCCODE_TR << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;

	} else {
		kvm_err("Trying to deliver TRAP when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_emulate_msafpe_exc(u32 cause,
						  u32 *opc,
						  struct kvm_run *run,
						  struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	enum emulation_result er = EMULATE_DONE;

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

		kvm_debug("Delivering MSAFPE @ pc %#lx\n", arch->pc);

		kvm_change_c0_guest_cause(cop0, (0xff),
					  (EXCCODE_MSAFPE << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;

	} else {
		kvm_err("Trying to deliver MSAFPE when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_emulate_fpe_exc(u32 cause,
					       u32 *opc,
					       struct kvm_run *run,
					       struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	enum emulation_result er = EMULATE_DONE;

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

		kvm_debug("Delivering FPE @ pc %#lx\n", arch->pc);

		kvm_change_c0_guest_cause(cop0, (0xff),
					  (EXCCODE_FPE << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;

	} else {
		kvm_err("Trying to deliver FPE when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_emulate_msadis_exc(u32 cause,
						  u32 *opc,
						  struct kvm_run *run,
						  struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	enum emulation_result er = EMULATE_DONE;

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

		kvm_debug("Delivering MSADIS @ pc %#lx\n", arch->pc);

		kvm_change_c0_guest_cause(cop0, (0xff),
					  (EXCCODE_MSADIS << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;

	} else {
		kvm_err("Trying to deliver MSADIS when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_handle_ri(u32 cause, u32 *opc,
					 struct kvm_run *run,
					 struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	enum emulation_result er = EMULATE_DONE;
	unsigned long curr_pc;
	union mips_instruction inst;
	int err;

	/*
	 * Update PC and hold onto current PC in case there is
	 * an error and we want to rollback the PC
	 */
	curr_pc = vcpu->arch.pc;
	er = update_pc(vcpu, cause);
	if (er == EMULATE_FAIL)
		return er;

	/* Fetch the instruction. */
	if (cause & CAUSEF_BD)
		opc += 1;
	err = kvm_get_badinstr(opc, vcpu, &inst.word);
	if (err) {
		kvm_err("%s: Cannot get inst @ %p (%d)\n", __func__, opc, err);
		return EMULATE_FAIL;
	}

	if (inst.r_format.opcode == spec3_op &&
	    inst.r_format.func == rdhwr_op &&
	    inst.r_format.rs == 0 &&
	    (inst.r_format.re >> 3) == 0) {
		int usermode = !KVM_GUEST_KERNEL_MODE(vcpu);
		int rd = inst.r_format.rd;
		int rt = inst.r_format.rt;
		int sel = inst.r_format.re & 0x7;

		/* If usermode, check RDHWR rd is allowed by guest HWREna */
		if (usermode && !(kvm_read_c0_guest_hwrena(cop0) & BIT(rd))) {
			kvm_debug("RDHWR %#x disallowed by HWREna @ %p\n",
				  rd, opc);
			goto emulate_ri;
		}
		switch (rd) {
		case MIPS_HWR_CPUNUM:		/* CPU number */
			arch->gprs[rt] = vcpu->vcpu_id;
			break;
		case MIPS_HWR_SYNCISTEP:	/* SYNCI length */
			arch->gprs[rt] = min(current_cpu_data.dcache.linesz,
					     current_cpu_data.icache.linesz);
			break;
		case MIPS_HWR_CC:		/* Read count register */
			arch->gprs[rt] = (s32)kvm_mips_read_count(vcpu);
			break;
		case MIPS_HWR_CCRES:		/* Count register resolution */
			switch (current_cpu_data.cputype) {
			case CPU_20KC:
			case CPU_25KF:
				arch->gprs[rt] = 1;
				break;
			default:
				arch->gprs[rt] = 2;
			}
			break;
		case MIPS_HWR_ULR:		/* Read UserLocal register */
			arch->gprs[rt] = kvm_read_c0_guest_userlocal(cop0);
			break;

		default:
			kvm_debug("RDHWR %#x not supported @ %p\n", rd, opc);
			goto emulate_ri;
		}

		trace_kvm_hwr(vcpu, KVM_TRACE_RDHWR, KVM_TRACE_HWR(rd, sel),
			      vcpu->arch.gprs[rt]);
	} else {
		kvm_debug("Emulate RI not supported @ %p: %#x\n",
			  opc, inst.word);
		goto emulate_ri;
	}

	return EMULATE_DONE;

emulate_ri:
	/*
	 * Rollback PC (if in branch delay slot then the PC already points to
	 * branch target), and pass the RI exception to the guest OS.
	 */
	vcpu->arch.pc = curr_pc;
	return kvm_mips_emulate_ri_exc(cause, opc, run, vcpu);
}

enum emulation_result kvm_mips_complete_mmio_load(struct kvm_vcpu *vcpu,
						  struct kvm_run *run)
{
	unsigned long *gpr = &vcpu->arch.gprs[vcpu->arch.io_gpr];
	enum emulation_result er = EMULATE_DONE;

	if (run->mmio.len > sizeof(*gpr)) {
		kvm_err("Bad MMIO length: %d", run->mmio.len);
		er = EMULATE_FAIL;
		goto done;
	}

	/* Restore saved resume PC */
	vcpu->arch.pc = vcpu->arch.io_pc;

	switch (run->mmio.len) {
	case 8:
		*gpr = *(s64 *)run->mmio.data;
		break;

	case 4:
		if (vcpu->mmio_needed == 2)
			*gpr = *(s32 *)run->mmio.data;
		else
			*gpr = *(u32 *)run->mmio.data;
		break;

	case 2:
		if (vcpu->mmio_needed == 2)
			*gpr = *(s16 *) run->mmio.data;
		else
			*gpr = *(u16 *)run->mmio.data;

		break;
	case 1:
		if (vcpu->mmio_needed == 2)
			*gpr = *(s8 *) run->mmio.data;
		else
			*gpr = *(u8 *) run->mmio.data;
		break;
	}

done:
	return er;
}

static enum emulation_result kvm_mips_emulate_exc(u32 cause,
						  u32 *opc,
						  struct kvm_run *run,
						  struct kvm_vcpu *vcpu)
{
	u32 exccode = (cause >> CAUSEB_EXCCODE) & 0x1f;
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	enum emulation_result er = EMULATE_DONE;

	if ((kvm_read_c0_guest_status(cop0) & ST0_EXL) == 0) {
		/* save old pc */
		kvm_write_c0_guest_epc(cop0, arch->pc);
		kvm_set_c0_guest_status(cop0, ST0_EXL);

		if (cause & CAUSEF_BD)
			kvm_set_c0_guest_cause(cop0, CAUSEF_BD);
		else
			kvm_clear_c0_guest_cause(cop0, CAUSEF_BD);

		kvm_change_c0_guest_cause(cop0, (0xff),
					  (exccode << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = kvm_mips_guest_exception_base(vcpu) + 0x180;
		kvm_write_c0_guest_badvaddr(cop0, vcpu->arch.host_cp0_badvaddr);

		kvm_debug("Delivering EXC %d @ pc %#lx, badVaddr: %#lx\n",
			  exccode, kvm_read_c0_guest_epc(cop0),
			  kvm_read_c0_guest_badvaddr(cop0));
	} else {
		kvm_err("Trying to deliver EXC when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_check_privilege(u32 cause,
					       u32 *opc,
					       struct kvm_run *run,
					       struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
	u32 exccode = (cause >> CAUSEB_EXCCODE) & 0x1f;
	unsigned long badvaddr = vcpu->arch.host_cp0_badvaddr;

	int usermode = !KVM_GUEST_KERNEL_MODE(vcpu);

	if (usermode) {
		switch (exccode) {
		case EXCCODE_INT:
		case EXCCODE_SYS:
		case EXCCODE_BP:
		case EXCCODE_RI:
		case EXCCODE_TR:
		case EXCCODE_MSAFPE:
		case EXCCODE_FPE:
		case EXCCODE_MSADIS:
			break;

		case EXCCODE_CPU:
			if (((cause & CAUSEF_CE) >> CAUSEB_CE) == 0)
				er = EMULATE_PRIV_FAIL;
			break;

		case EXCCODE_MOD:
			break;

		case EXCCODE_TLBL:
			/*
			 * We we are accessing Guest kernel space, then send an
			 * address error exception to the guest
			 */
			if (badvaddr >= (unsigned long) KVM_GUEST_KSEG0) {
				kvm_debug("%s: LD MISS @ %#lx\n", __func__,
					  badvaddr);
				cause &= ~0xff;
				cause |= (EXCCODE_ADEL << CAUSEB_EXCCODE);
				er = EMULATE_PRIV_FAIL;
			}
			break;

		case EXCCODE_TLBS:
			/*
			 * We we are accessing Guest kernel space, then send an
			 * address error exception to the guest
			 */
			if (badvaddr >= (unsigned long) KVM_GUEST_KSEG0) {
				kvm_debug("%s: ST MISS @ %#lx\n", __func__,
					  badvaddr);
				cause &= ~0xff;
				cause |= (EXCCODE_ADES << CAUSEB_EXCCODE);
				er = EMULATE_PRIV_FAIL;
			}
			break;

		case EXCCODE_ADES:
			kvm_debug("%s: address error ST @ %#lx\n", __func__,
				  badvaddr);
			if ((badvaddr & PAGE_MASK) == KVM_GUEST_COMMPAGE_ADDR) {
				cause &= ~0xff;
				cause |= (EXCCODE_TLBS << CAUSEB_EXCCODE);
			}
			er = EMULATE_PRIV_FAIL;
			break;
		case EXCCODE_ADEL:
			kvm_debug("%s: address error LD @ %#lx\n", __func__,
				  badvaddr);
			if ((badvaddr & PAGE_MASK) == KVM_GUEST_COMMPAGE_ADDR) {
				cause &= ~0xff;
				cause |= (EXCCODE_TLBL << CAUSEB_EXCCODE);
			}
			er = EMULATE_PRIV_FAIL;
			break;
		default:
			er = EMULATE_PRIV_FAIL;
			break;
		}
	}

	if (er == EMULATE_PRIV_FAIL)
		kvm_mips_emulate_exc(cause, opc, run, vcpu);

	return er;
}

/*
 * User Address (UA) fault, this could happen if
 * (1) TLB entry not present/valid in both Guest and shadow host TLBs, in this
 *     case we pass on the fault to the guest kernel and let it handle it.
 * (2) TLB entry is present in the Guest TLB but not in the shadow, in this
 *     case we inject the TLB from the Guest TLB into the shadow host TLB
 */
enum emulation_result kvm_mips_handle_tlbmiss(u32 cause,
					      u32 *opc,
					      struct kvm_run *run,
					      struct kvm_vcpu *vcpu,
					      bool write_fault)
{
	enum emulation_result er = EMULATE_DONE;
	u32 exccode = (cause >> CAUSEB_EXCCODE) & 0x1f;
	unsigned long va = vcpu->arch.host_cp0_badvaddr;
	int index;

	kvm_debug("kvm_mips_handle_tlbmiss: badvaddr: %#lx\n",
		  vcpu->arch.host_cp0_badvaddr);

	/*
	 * KVM would not have got the exception if this entry was valid in the
	 * shadow host TLB. Check the Guest TLB, if the entry is not there then
	 * send the guest an exception. The guest exc handler should then inject
	 * an entry into the guest TLB.
	 */
	index = kvm_mips_guest_tlb_lookup(vcpu,
		      (va & VPN2_MASK) |
		      (kvm_read_c0_guest_entryhi(vcpu->arch.cop0) &
		       KVM_ENTRYHI_ASID));
	if (index < 0) {
		if (exccode == EXCCODE_TLBL) {
			er = kvm_mips_emulate_tlbmiss_ld(cause, opc, run, vcpu);
		} else if (exccode == EXCCODE_TLBS) {
			er = kvm_mips_emulate_tlbmiss_st(cause, opc, run, vcpu);
		} else {
			kvm_err("%s: invalid exc code: %d\n", __func__,
				exccode);
			er = EMULATE_FAIL;
		}
	} else {
		struct kvm_mips_tlb *tlb = &vcpu->arch.guest_tlb[index];

		/*
		 * Check if the entry is valid, if not then setup a TLB invalid
		 * exception to the guest
		 */
		if (!TLB_IS_VALID(*tlb, va)) {
			if (exccode == EXCCODE_TLBL) {
				er = kvm_mips_emulate_tlbinv_ld(cause, opc, run,
								vcpu);
			} else if (exccode == EXCCODE_TLBS) {
				er = kvm_mips_emulate_tlbinv_st(cause, opc, run,
								vcpu);
			} else {
				kvm_err("%s: invalid exc code: %d\n", __func__,
					exccode);
				er = EMULATE_FAIL;
			}
		} else {
			kvm_debug("Injecting hi: %#lx, lo0: %#lx, lo1: %#lx into shadow host TLB\n",
				  tlb->tlb_hi, tlb->tlb_lo[0], tlb->tlb_lo[1]);
			/*
			 * OK we have a Guest TLB entry, now inject it into the
			 * shadow host TLB
			 */
			if (kvm_mips_handle_mapped_seg_tlb_fault(vcpu, tlb, va,
								 write_fault)) {
				kvm_err("%s: handling mapped seg tlb fault for %lx, index: %u, vcpu: %p, ASID: %#lx\n",
					__func__, va, index, vcpu,
					read_c0_entryhi());
				er = EMULATE_FAIL;
			}
		}
	}

	return er;
}
