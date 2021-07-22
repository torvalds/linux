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
			fallthrough;
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
		fallthrough;
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
		WARN_ONCE(1, "CPU doesn't have BadInstr register\n");
		return -EINVAL;
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
		WARN_ONCE(1, "CPU doesn't have BadInstrp register\n");
		return -EINVAL;
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
	if (delta > 0) {
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
	else
		/*
		 * With VZ, writing CP0_Compare acks (clears) CP0_Cause.TI, so
		 * preserve guest CP0_Cause.TI if we don't want to ack it.
		 */
		cause = kvm_read_c0_guest_cause(cop0);

	kvm_write_c0_guest_compare(cop0, compare);

	if (delta > 0)
		preempt_enable();

	back_to_back_c0_hazard();

	if (!ack && cause & CAUSEF_TI)
		kvm_write_c0_guest_cause(cop0, cause);

	/* resume_hrtimer() takes care of timer interrupts > count */
	if (!dc)
		kvm_mips_resume_hrtimer(vcpu, now, count);

	/*
	 * If guest CP0_Compare is moving backward, we delay CP0_GTOffset change
	 * until after the new CP0_Compare is written, otherwise new guest
	 * CP0_Count could hit new guest CP0_Compare.
	 */
	if (delta <= 0)
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

enum emulation_result kvm_mips_emulate_store(union mips_instruction inst,
					     u32 cause,
					     struct kvm_vcpu *vcpu)
{
	int r;
	enum emulation_result er;
	u32 rt;
	struct kvm_run *run = vcpu->run;
	void *data = run->mmio.data;
	unsigned int imme;
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
#if defined(CONFIG_64BIT)
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

	case swl_op:
		run->mmio.phys_addr = kvm_mips_callbacks->gva_to_gpa(
					vcpu->arch.host_cp0_badvaddr) & (~0x3);
		run->mmio.len = 4;
		imme = vcpu->arch.host_cp0_badvaddr & 0x3;
		switch (imme) {
		case 0:
			*(u32 *)data = ((*(u32 *)data) & 0xffffff00) |
					(vcpu->arch.gprs[rt] >> 24);
			break;
		case 1:
			*(u32 *)data = ((*(u32 *)data) & 0xffff0000) |
					(vcpu->arch.gprs[rt] >> 16);
			break;
		case 2:
			*(u32 *)data = ((*(u32 *)data) & 0xff000000) |
					(vcpu->arch.gprs[rt] >> 8);
			break;
		case 3:
			*(u32 *)data = vcpu->arch.gprs[rt];
			break;
		default:
			break;
		}

		kvm_debug("[%#lx] OP_SWL: eaddr: %#lx, gpr: %#lx, data: %#x\n",
			  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
			  vcpu->arch.gprs[rt], *(u32 *)data);
		break;

	case swr_op:
		run->mmio.phys_addr = kvm_mips_callbacks->gva_to_gpa(
					vcpu->arch.host_cp0_badvaddr) & (~0x3);
		run->mmio.len = 4;
		imme = vcpu->arch.host_cp0_badvaddr & 0x3;
		switch (imme) {
		case 0:
			*(u32 *)data = vcpu->arch.gprs[rt];
			break;
		case 1:
			*(u32 *)data = ((*(u32 *)data) & 0xff) |
					(vcpu->arch.gprs[rt] << 8);
			break;
		case 2:
			*(u32 *)data = ((*(u32 *)data) & 0xffff) |
					(vcpu->arch.gprs[rt] << 16);
			break;
		case 3:
			*(u32 *)data = ((*(u32 *)data) & 0xffffff) |
					(vcpu->arch.gprs[rt] << 24);
			break;
		default:
			break;
		}

		kvm_debug("[%#lx] OP_SWR: eaddr: %#lx, gpr: %#lx, data: %#x\n",
			  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
			  vcpu->arch.gprs[rt], *(u32 *)data);
		break;

#if defined(CONFIG_64BIT)
	case sdl_op:
		run->mmio.phys_addr = kvm_mips_callbacks->gva_to_gpa(
					vcpu->arch.host_cp0_badvaddr) & (~0x7);

		run->mmio.len = 8;
		imme = vcpu->arch.host_cp0_badvaddr & 0x7;
		switch (imme) {
		case 0:
			*(u64 *)data = ((*(u64 *)data) & 0xffffffffffffff00) |
					((vcpu->arch.gprs[rt] >> 56) & 0xff);
			break;
		case 1:
			*(u64 *)data = ((*(u64 *)data) & 0xffffffffffff0000) |
					((vcpu->arch.gprs[rt] >> 48) & 0xffff);
			break;
		case 2:
			*(u64 *)data = ((*(u64 *)data) & 0xffffffffff000000) |
					((vcpu->arch.gprs[rt] >> 40) & 0xffffff);
			break;
		case 3:
			*(u64 *)data = ((*(u64 *)data) & 0xffffffff00000000) |
					((vcpu->arch.gprs[rt] >> 32) & 0xffffffff);
			break;
		case 4:
			*(u64 *)data = ((*(u64 *)data) & 0xffffff0000000000) |
					((vcpu->arch.gprs[rt] >> 24) & 0xffffffffff);
			break;
		case 5:
			*(u64 *)data = ((*(u64 *)data) & 0xffff000000000000) |
					((vcpu->arch.gprs[rt] >> 16) & 0xffffffffffff);
			break;
		case 6:
			*(u64 *)data = ((*(u64 *)data) & 0xff00000000000000) |
					((vcpu->arch.gprs[rt] >> 8) & 0xffffffffffffff);
			break;
		case 7:
			*(u64 *)data = vcpu->arch.gprs[rt];
			break;
		default:
			break;
		}

		kvm_debug("[%#lx] OP_SDL: eaddr: %#lx, gpr: %#lx, data: %llx\n",
			  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
			  vcpu->arch.gprs[rt], *(u64 *)data);
		break;

	case sdr_op:
		run->mmio.phys_addr = kvm_mips_callbacks->gva_to_gpa(
					vcpu->arch.host_cp0_badvaddr) & (~0x7);

		run->mmio.len = 8;
		imme = vcpu->arch.host_cp0_badvaddr & 0x7;
		switch (imme) {
		case 0:
			*(u64 *)data = vcpu->arch.gprs[rt];
			break;
		case 1:
			*(u64 *)data = ((*(u64 *)data) & 0xff) |
					(vcpu->arch.gprs[rt] << 8);
			break;
		case 2:
			*(u64 *)data = ((*(u64 *)data) & 0xffff) |
					(vcpu->arch.gprs[rt] << 16);
			break;
		case 3:
			*(u64 *)data = ((*(u64 *)data) & 0xffffff) |
					(vcpu->arch.gprs[rt] << 24);
			break;
		case 4:
			*(u64 *)data = ((*(u64 *)data) & 0xffffffff) |
					(vcpu->arch.gprs[rt] << 32);
			break;
		case 5:
			*(u64 *)data = ((*(u64 *)data) & 0xffffffffff) |
					(vcpu->arch.gprs[rt] << 40);
			break;
		case 6:
			*(u64 *)data = ((*(u64 *)data) & 0xffffffffffff) |
					(vcpu->arch.gprs[rt] << 48);
			break;
		case 7:
			*(u64 *)data = ((*(u64 *)data) & 0xffffffffffffff) |
					(vcpu->arch.gprs[rt] << 56);
			break;
		default:
			break;
		}

		kvm_debug("[%#lx] OP_SDR: eaddr: %#lx, gpr: %#lx, data: %llx\n",
			  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
			  vcpu->arch.gprs[rt], *(u64 *)data);
		break;
#endif

#ifdef CONFIG_CPU_LOONGSON64
	case sdc2_op:
		rt = inst.loongson3_lsdc2_format.rt;
		switch (inst.loongson3_lsdc2_format.opcode1) {
		/*
		 * Loongson-3 overridden sdc2 instructions.
		 * opcode1              instruction
		 *   0x0          gssbx: store 1 bytes from GPR
		 *   0x1          gsshx: store 2 bytes from GPR
		 *   0x2          gsswx: store 4 bytes from GPR
		 *   0x3          gssdx: store 8 bytes from GPR
		 */
		case 0x0:
			run->mmio.len = 1;
			*(u8 *)data = vcpu->arch.gprs[rt];

			kvm_debug("[%#lx] OP_GSSBX: eaddr: %#lx, gpr: %#lx, data: %#x\n",
				  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
				  vcpu->arch.gprs[rt], *(u8 *)data);
			break;
		case 0x1:
			run->mmio.len = 2;
			*(u16 *)data = vcpu->arch.gprs[rt];

			kvm_debug("[%#lx] OP_GSSSHX: eaddr: %#lx, gpr: %#lx, data: %#x\n",
				  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
				  vcpu->arch.gprs[rt], *(u16 *)data);
			break;
		case 0x2:
			run->mmio.len = 4;
			*(u32 *)data = vcpu->arch.gprs[rt];

			kvm_debug("[%#lx] OP_GSSWX: eaddr: %#lx, gpr: %#lx, data: %#x\n",
				  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
				  vcpu->arch.gprs[rt], *(u32 *)data);
			break;
		case 0x3:
			run->mmio.len = 8;
			*(u64 *)data = vcpu->arch.gprs[rt];

			kvm_debug("[%#lx] OP_GSSDX: eaddr: %#lx, gpr: %#lx, data: %#llx\n",
				  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
				  vcpu->arch.gprs[rt], *(u64 *)data);
			break;
		default:
			kvm_err("Godson Extended GS-Store not yet supported (inst=0x%08x)\n",
				inst.word);
			break;
		}
		break;
#endif
	default:
		kvm_err("Store not yet supported (inst=0x%08x)\n",
			inst.word);
		goto out_fail;
	}

	vcpu->mmio_needed = 1;
	run->mmio.is_write = 1;
	vcpu->mmio_is_write = 1;

	r = kvm_io_bus_write(vcpu, KVM_MMIO_BUS,
			run->mmio.phys_addr, run->mmio.len, data);

	if (!r) {
		vcpu->mmio_needed = 0;
		return EMULATE_DONE;
	}

	return EMULATE_DO_MMIO;

out_fail:
	/* Rollback PC if emulation was unsuccessful */
	vcpu->arch.pc = curr_pc;
	return EMULATE_FAIL;
}

enum emulation_result kvm_mips_emulate_load(union mips_instruction inst,
					    u32 cause, struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	int r;
	enum emulation_result er;
	unsigned long curr_pc;
	u32 op, rt;
	unsigned int imme;

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
#if defined(CONFIG_64BIT)
	case ld_op:
		run->mmio.len = 8;
		break;

	case lwu_op:
		vcpu->mmio_needed = 1;	/* unsigned */
		fallthrough;
#endif
	case lw_op:
		run->mmio.len = 4;
		break;

	case lhu_op:
		vcpu->mmio_needed = 1;	/* unsigned */
		fallthrough;
	case lh_op:
		run->mmio.len = 2;
		break;

	case lbu_op:
		vcpu->mmio_needed = 1;	/* unsigned */
		fallthrough;
	case lb_op:
		run->mmio.len = 1;
		break;

	case lwl_op:
		run->mmio.phys_addr = kvm_mips_callbacks->gva_to_gpa(
					vcpu->arch.host_cp0_badvaddr) & (~0x3);

		run->mmio.len = 4;
		imme = vcpu->arch.host_cp0_badvaddr & 0x3;
		switch (imme) {
		case 0:
			vcpu->mmio_needed = 3;	/* 1 byte */
			break;
		case 1:
			vcpu->mmio_needed = 4;	/* 2 bytes */
			break;
		case 2:
			vcpu->mmio_needed = 5;	/* 3 bytes */
			break;
		case 3:
			vcpu->mmio_needed = 6;	/* 4 bytes */
			break;
		default:
			break;
		}
		break;

	case lwr_op:
		run->mmio.phys_addr = kvm_mips_callbacks->gva_to_gpa(
					vcpu->arch.host_cp0_badvaddr) & (~0x3);

		run->mmio.len = 4;
		imme = vcpu->arch.host_cp0_badvaddr & 0x3;
		switch (imme) {
		case 0:
			vcpu->mmio_needed = 7;	/* 4 bytes */
			break;
		case 1:
			vcpu->mmio_needed = 8;	/* 3 bytes */
			break;
		case 2:
			vcpu->mmio_needed = 9;	/* 2 bytes */
			break;
		case 3:
			vcpu->mmio_needed = 10;	/* 1 byte */
			break;
		default:
			break;
		}
		break;

#if defined(CONFIG_64BIT)
	case ldl_op:
		run->mmio.phys_addr = kvm_mips_callbacks->gva_to_gpa(
					vcpu->arch.host_cp0_badvaddr) & (~0x7);

		run->mmio.len = 8;
		imme = vcpu->arch.host_cp0_badvaddr & 0x7;
		switch (imme) {
		case 0:
			vcpu->mmio_needed = 11;	/* 1 byte */
			break;
		case 1:
			vcpu->mmio_needed = 12;	/* 2 bytes */
			break;
		case 2:
			vcpu->mmio_needed = 13;	/* 3 bytes */
			break;
		case 3:
			vcpu->mmio_needed = 14;	/* 4 bytes */
			break;
		case 4:
			vcpu->mmio_needed = 15;	/* 5 bytes */
			break;
		case 5:
			vcpu->mmio_needed = 16;	/* 6 bytes */
			break;
		case 6:
			vcpu->mmio_needed = 17;	/* 7 bytes */
			break;
		case 7:
			vcpu->mmio_needed = 18;	/* 8 bytes */
			break;
		default:
			break;
		}
		break;

	case ldr_op:
		run->mmio.phys_addr = kvm_mips_callbacks->gva_to_gpa(
					vcpu->arch.host_cp0_badvaddr) & (~0x7);

		run->mmio.len = 8;
		imme = vcpu->arch.host_cp0_badvaddr & 0x7;
		switch (imme) {
		case 0:
			vcpu->mmio_needed = 19;	/* 8 bytes */
			break;
		case 1:
			vcpu->mmio_needed = 20;	/* 7 bytes */
			break;
		case 2:
			vcpu->mmio_needed = 21;	/* 6 bytes */
			break;
		case 3:
			vcpu->mmio_needed = 22;	/* 5 bytes */
			break;
		case 4:
			vcpu->mmio_needed = 23;	/* 4 bytes */
			break;
		case 5:
			vcpu->mmio_needed = 24;	/* 3 bytes */
			break;
		case 6:
			vcpu->mmio_needed = 25;	/* 2 bytes */
			break;
		case 7:
			vcpu->mmio_needed = 26;	/* 1 byte */
			break;
		default:
			break;
		}
		break;
#endif

#ifdef CONFIG_CPU_LOONGSON64
	case ldc2_op:
		rt = inst.loongson3_lsdc2_format.rt;
		switch (inst.loongson3_lsdc2_format.opcode1) {
		/*
		 * Loongson-3 overridden ldc2 instructions.
		 * opcode1              instruction
		 *   0x0          gslbx: store 1 bytes from GPR
		 *   0x1          gslhx: store 2 bytes from GPR
		 *   0x2          gslwx: store 4 bytes from GPR
		 *   0x3          gsldx: store 8 bytes from GPR
		 */
		case 0x0:
			run->mmio.len = 1;
			vcpu->mmio_needed = 27;	/* signed */
			break;
		case 0x1:
			run->mmio.len = 2;
			vcpu->mmio_needed = 28;	/* signed */
			break;
		case 0x2:
			run->mmio.len = 4;
			vcpu->mmio_needed = 29;	/* signed */
			break;
		case 0x3:
			run->mmio.len = 8;
			vcpu->mmio_needed = 30;	/* signed */
			break;
		default:
			kvm_err("Godson Extended GS-Load for float not yet supported (inst=0x%08x)\n",
				inst.word);
			break;
		}
		break;
#endif

	default:
		kvm_err("Load not yet supported (inst=0x%08x)\n",
			inst.word);
		vcpu->mmio_needed = 0;
		return EMULATE_FAIL;
	}

	run->mmio.is_write = 0;
	vcpu->mmio_is_write = 0;

	r = kvm_io_bus_read(vcpu, KVM_MMIO_BUS,
			run->mmio.phys_addr, run->mmio.len, run->mmio.data);

	if (!r) {
		kvm_mips_complete_mmio_load(vcpu);
		vcpu->mmio_needed = 0;
		return EMULATE_DONE;
	}

	return EMULATE_DO_MMIO;
}

enum emulation_result kvm_mips_complete_mmio_load(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
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
		switch (vcpu->mmio_needed) {
		case 11:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffffffffffffff) |
				(((*(s64 *)run->mmio.data) & 0xff) << 56);
			break;
		case 12:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffffffffffff) |
				(((*(s64 *)run->mmio.data) & 0xffff) << 48);
			break;
		case 13:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffffffffff) |
				(((*(s64 *)run->mmio.data) & 0xffffff) << 40);
			break;
		case 14:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffffffff) |
				(((*(s64 *)run->mmio.data) & 0xffffffff) << 32);
			break;
		case 15:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffffff) |
				(((*(s64 *)run->mmio.data) & 0xffffffffff) << 24);
			break;
		case 16:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffff) |
				(((*(s64 *)run->mmio.data) & 0xffffffffffff) << 16);
			break;
		case 17:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xff) |
				(((*(s64 *)run->mmio.data) & 0xffffffffffffff) << 8);
			break;
		case 18:
		case 19:
			*gpr = *(s64 *)run->mmio.data;
			break;
		case 20:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xff00000000000000) |
				((((*(s64 *)run->mmio.data)) >> 8) & 0xffffffffffffff);
			break;
		case 21:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffff000000000000) |
				((((*(s64 *)run->mmio.data)) >> 16) & 0xffffffffffff);
			break;
		case 22:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffffff0000000000) |
				((((*(s64 *)run->mmio.data)) >> 24) & 0xffffffffff);
			break;
		case 23:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffffffff00000000) |
				((((*(s64 *)run->mmio.data)) >> 32) & 0xffffffff);
			break;
		case 24:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffffffffff000000) |
				((((*(s64 *)run->mmio.data)) >> 40) & 0xffffff);
			break;
		case 25:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffffffffffff0000) |
				((((*(s64 *)run->mmio.data)) >> 48) & 0xffff);
			break;
		case 26:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffffffffffffff00) |
				((((*(s64 *)run->mmio.data)) >> 56) & 0xff);
			break;
		default:
			*gpr = *(s64 *)run->mmio.data;
		}
		break;

	case 4:
		switch (vcpu->mmio_needed) {
		case 1:
			*gpr = *(u32 *)run->mmio.data;
			break;
		case 2:
			*gpr = *(s32 *)run->mmio.data;
			break;
		case 3:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffffff) |
				(((*(s32 *)run->mmio.data) & 0xff) << 24);
			break;
		case 4:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffff) |
				(((*(s32 *)run->mmio.data) & 0xffff) << 16);
			break;
		case 5:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xff) |
				(((*(s32 *)run->mmio.data) & 0xffffff) << 8);
			break;
		case 6:
		case 7:
			*gpr = *(s32 *)run->mmio.data;
			break;
		case 8:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xff000000) |
				((((*(s32 *)run->mmio.data)) >> 8) & 0xffffff);
			break;
		case 9:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffff0000) |
				((((*(s32 *)run->mmio.data)) >> 16) & 0xffff);
			break;
		case 10:
			*gpr = (vcpu->arch.gprs[vcpu->arch.io_gpr] & 0xffffff00) |
				((((*(s32 *)run->mmio.data)) >> 24) & 0xff);
			break;
		default:
			*gpr = *(s32 *)run->mmio.data;
		}
		break;

	case 2:
		if (vcpu->mmio_needed == 1)
			*gpr = *(u16 *)run->mmio.data;
		else
			*gpr = *(s16 *)run->mmio.data;

		break;
	case 1:
		if (vcpu->mmio_needed == 1)
			*gpr = *(u8 *)run->mmio.data;
		else
			*gpr = *(s8 *)run->mmio.data;
		break;
	}

done:
	return er;
}
