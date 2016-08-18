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
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/bootmem.h>
#include <linux/random.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/cpu-info.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/inst.h>

#undef CONFIG_MIPS_MT
#include <asm/r4kcache.h>
#define CONFIG_MIPS_MT

#include "opcode.h"
#include "interrupt.h"
#include "commpage.h"

#include "trace.h"

/*
 * Compute the return address and do emulate branch simulation, if required.
 * This function should be called only in branch delay slot active.
 */
unsigned long kvm_compute_return_epc(struct kvm_vcpu *vcpu,
	unsigned long instpc)
{
	unsigned int dspcontrol;
	union mips_instruction insn;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	long epc = instpc;
	long nextpc = KVM_INVALID_INST;

	if (epc & 3)
		goto unaligned;

	/* Read the instruction */
	insn.word = kvm_get_inst((uint32_t *) epc, vcpu);

	if (insn.word == KVM_INVALID_INST)
		return KVM_INVALID_INST;

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
			if (!cpu_has_dsp)
				goto sigill;

			dspcontrol = rddsp(0x01);

			if (dspcontrol >= 32)
				epc = epc + 4 + (insn.i_format.simmediate << 2);
			else
				epc += 8;
			nextpc = epc;
			break;
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

	case blez_op:		/* not really i_format */
	case blezl_op:
		/* rt field assumed to be zero */
		if ((long)arch->gprs[insn.i_format.rs] <= 0)
			epc = epc + 4 + (insn.i_format.simmediate << 2);
		else
			epc += 8;
		nextpc = epc;
		break;

	case bgtz_op:
	case bgtzl_op:
		/* rt field assumed to be zero */
		if ((long)arch->gprs[insn.i_format.rs] > 0)
			epc = epc + 4 + (insn.i_format.simmediate << 2);
		else
			epc += 8;
		nextpc = epc;
		break;

		/* And now the FPA/cp1 branch instructions. */
	case cop1_op:
		kvm_err("%s: unsupported cop1_op\n", __func__);
		break;
	}

	return nextpc;

unaligned:
	kvm_err("%s: unaligned epc\n", __func__);
	return nextpc;

sigill:
	kvm_err("%s: DSP branch but not DSP ASE\n", __func__);
	return nextpc;
}

enum emulation_result update_pc(struct kvm_vcpu *vcpu, uint32_t cause)
{
	unsigned long branch_pc;
	enum emulation_result er = EMULATE_DONE;

	if (cause & CAUSEF_BD) {
		branch_pc = kvm_compute_return_epc(vcpu, vcpu->arch.pc);
		if (branch_pc == KVM_INVALID_INST) {
			er = EMULATE_FAIL;
		} else {
			vcpu->arch.pc = branch_pc;
			kvm_debug("BD update_pc(): New PC: %#lx\n",
				  vcpu->arch.pc);
		}
	} else
		vcpu->arch.pc += 4;

	kvm_debug("update_pc(): New PC: %#lx\n", vcpu->arch.pc);

	return er;
}

/**
 * kvm_mips_count_disabled() - Find whether the CP0_Count timer is disabled.
 * @vcpu:	Virtual CPU.
 *
 * Returns:	1 if the CP0_Count timer is disabled by either the guest
 *		CP0_Cause.DC bit or the count_ctl.DC bit.
 *		0 otherwise (in which case CP0_Count timer is running).
 */
static inline int kvm_mips_count_disabled(struct kvm_vcpu *vcpu)
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
static uint32_t kvm_mips_ktime_to_count(struct kvm_vcpu *vcpu, ktime_t now)
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
static uint32_t kvm_mips_read_count_running(struct kvm_vcpu *vcpu, ktime_t now)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	ktime_t expires, threshold;
	uint32_t count, compare;
	int running;

	/* Calculate the biased and scaled guest CP0_Count */
	count = vcpu->arch.count_bias + kvm_mips_ktime_to_count(vcpu, now);
	compare = kvm_read_c0_guest_compare(cop0);

	/*
	 * Find whether CP0_Count has reached the closest timer interrupt. If
	 * not, we shouldn't inject it.
	 */
	if ((int32_t)(count - compare) < 0)
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
uint32_t kvm_mips_read_count(struct kvm_vcpu *vcpu)
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
static ktime_t kvm_mips_freeze_hrtimer(struct kvm_vcpu *vcpu,
				       uint32_t *count)
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
				    ktime_t now, uint32_t count)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	uint32_t compare;
	u64 delta;
	ktime_t expire;

	/* Calculate timeout (wrap 0 to 2^32) */
	compare = kvm_read_c0_guest_compare(cop0);
	delta = (u64)(uint32_t)(compare - count - 1) + 1;
	delta = div_u64(delta * NSEC_PER_SEC, vcpu->arch.count_hz);
	expire = ktime_add_ns(now, delta);

	/* Update hrtimer to use new timeout */
	hrtimer_cancel(&vcpu->arch.comparecount_timer);
	hrtimer_start(&vcpu->arch.comparecount_timer, expire, HRTIMER_MODE_ABS);
}

/**
 * kvm_mips_write_count() - Modify the count and update timer.
 * @vcpu:	Virtual CPU.
 * @count:	Guest CP0_Count value to set.
 *
 * Sets the CP0_Count value and updates the timer accordingly.
 */
void kvm_mips_write_count(struct kvm_vcpu *vcpu, uint32_t count)
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
 *
 * Initialise the timer to a sensible frequency, namely 100MHz, zero it, and set
 * it going if it's enabled.
 */
void kvm_mips_init_count(struct kvm_vcpu *vcpu)
{
	/* 100 MHz */
	vcpu->arch.count_hz = 100*1000*1000;
	vcpu->arch.count_period = div_u64((u64)NSEC_PER_SEC << 32,
					  vcpu->arch.count_hz);
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
void kvm_mips_write_compare(struct kvm_vcpu *vcpu, uint32_t compare, bool ack)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	int dc;
	u32 old_compare = kvm_read_c0_guest_compare(cop0);
	ktime_t now;
	uint32_t count;

	/* if unchanged, must just be an ack */
	if (old_compare == compare) {
		if (!ack)
			return;
		kvm_mips_callbacks->dequeue_timer_int(vcpu);
		kvm_write_c0_guest_compare(cop0, compare);
		return;
	}

	/* freeze_hrtimer() takes care of timer interrupts <= count */
	dc = kvm_mips_count_disabled(vcpu);
	if (!dc)
		now = kvm_mips_freeze_hrtimer(vcpu, &count);

	if (ack)
		kvm_mips_callbacks->dequeue_timer_int(vcpu);

	kvm_write_c0_guest_compare(cop0, compare);

	/* resume_hrtimer() takes care of timer interrupts > count */
	if (!dc)
		kvm_mips_resume_hrtimer(vcpu, now, count);
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
	uint32_t count;
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
	uint32_t count;

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
	uint32_t count, compare;

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
			delta = (u64)(uint32_t)(compare - count - 1) + 1;
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

	if (kvm_read_c0_guest_status(cop0) & ST0_EXL) {
		kvm_debug("[%#lx] ERET to %#lx\n", vcpu->arch.pc,
			  kvm_read_c0_guest_epc(cop0));
		kvm_clear_c0_guest_status(cop0, ST0_EXL);
		vcpu->arch.pc = kvm_read_c0_guest_epc(cop0);

	} else if (kvm_read_c0_guest_status(cop0) & ST0_ERL) {
		kvm_clear_c0_guest_status(cop0, ST0_ERL);
		vcpu->arch.pc = kvm_read_c0_guest_errorepc(cop0);
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
	trace_kvm_exit(vcpu, WAIT_EXITS);
	if (!vcpu->arch.pending_exceptions) {
		vcpu->arch.wait = 1;
		kvm_vcpu_block(vcpu);

		/*
		 * We we are runnable, then definitely go off to user space to
		 * check if any I/O interrupts are pending.
		 */
		if (kvm_check_request(KVM_REQ_UNHALT, vcpu)) {
			clear_bit(KVM_REQ_UNHALT, &vcpu->requests);
			vcpu->run->exit_reason = KVM_EXIT_IRQ_WINDOW_OPEN;
		}
	}

	return EMULATE_DONE;
}

/*
 * XXXKYMA: Linux doesn't seem to use TLBR, return EMULATE_FAIL for now so that
 * we can catch this, if things ever change
 */
enum emulation_result kvm_mips_emul_tlbr(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	uint32_t pc = vcpu->arch.pc;

	kvm_err("[%#x] COP0_TLBR [%ld]\n", pc, kvm_read_c0_guest_index(cop0));
	return EMULATE_FAIL;
}

/* Write Guest TLB Entry @ Index */
enum emulation_result kvm_mips_emul_tlbwi(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	int index = kvm_read_c0_guest_index(cop0);
	struct kvm_mips_tlb *tlb = NULL;
	uint32_t pc = vcpu->arch.pc;

	if (index < 0 || index >= KVM_MIPS_GUEST_TLB_SIZE) {
		kvm_debug("%s: illegal index: %d\n", __func__, index);
		kvm_debug("[%#x] COP0_TLBWI [%d] (entryhi: %#lx, entrylo0: %#lx entrylo1: %#lx, mask: %#lx)\n",
			  pc, index, kvm_read_c0_guest_entryhi(cop0),
			  kvm_read_c0_guest_entrylo0(cop0),
			  kvm_read_c0_guest_entrylo1(cop0),
			  kvm_read_c0_guest_pagemask(cop0));
		index = (index & ~0x80000000) % KVM_MIPS_GUEST_TLB_SIZE;
	}

	tlb = &vcpu->arch.guest_tlb[index];
	/*
	 * Probe the shadow host TLB for the entry being overwritten, if one
	 * matches, invalidate it
	 */
	kvm_mips_host_tlb_inv(vcpu, tlb->tlb_hi);

	tlb->tlb_mask = kvm_read_c0_guest_pagemask(cop0);
	tlb->tlb_hi = kvm_read_c0_guest_entryhi(cop0);
	tlb->tlb_lo0 = kvm_read_c0_guest_entrylo0(cop0);
	tlb->tlb_lo1 = kvm_read_c0_guest_entrylo1(cop0);

	kvm_debug("[%#x] COP0_TLBWI [%d] (entryhi: %#lx, entrylo0: %#lx entrylo1: %#lx, mask: %#lx)\n",
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
	uint32_t pc = vcpu->arch.pc;
	int index;

	get_random_bytes(&index, sizeof(index));
	index &= (KVM_MIPS_GUEST_TLB_SIZE - 1);

	tlb = &vcpu->arch.guest_tlb[index];

	/*
	 * Probe the shadow host TLB for the entry being overwritten, if one
	 * matches, invalidate it
	 */
	kvm_mips_host_tlb_inv(vcpu, tlb->tlb_hi);

	tlb->tlb_mask = kvm_read_c0_guest_pagemask(cop0);
	tlb->tlb_hi = kvm_read_c0_guest_entryhi(cop0);
	tlb->tlb_lo0 = kvm_read_c0_guest_entrylo0(cop0);
	tlb->tlb_lo1 = kvm_read_c0_guest_entrylo1(cop0);

	kvm_debug("[%#x] COP0_TLBWR[%d] (entryhi: %#lx, entrylo0: %#lx entrylo1: %#lx)\n",
		  pc, index, kvm_read_c0_guest_entryhi(cop0),
		  kvm_read_c0_guest_entrylo0(cop0),
		  kvm_read_c0_guest_entrylo1(cop0));

	return EMULATE_DONE;
}

enum emulation_result kvm_mips_emul_tlbp(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	long entryhi = kvm_read_c0_guest_entryhi(cop0);
	uint32_t pc = vcpu->arch.pc;
	int index = -1;

	index = kvm_mips_guest_tlb_lookup(vcpu, entryhi);

	kvm_write_c0_guest_index(cop0, index);

	kvm_debug("[%#x] COP0_TLBP (entryhi: %#lx), index: %d\n", pc, entryhi,
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
	/* Config4 is optional */
	unsigned int mask = MIPS_CONF_M;

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
	return MIPS_CONF_M;
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

enum emulation_result kvm_mips_emulate_CP0(uint32_t inst, uint32_t *opc,
					   uint32_t cause, struct kvm_run *run,
					   struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	enum emulation_result er = EMULATE_DONE;
	int32_t rt, rd, copz, sel, co_bit, op;
	uint32_t pc = vcpu->arch.pc;
	unsigned long curr_pc;

	/*
	 * Update PC and hold onto current PC in case there is
	 * an error and we want to rollback the PC
	 */
	curr_pc = vcpu->arch.pc;
	er = update_pc(vcpu, cause);
	if (er == EMULATE_FAIL)
		return er;

	copz = (inst >> 21) & 0x1f;
	rt = (inst >> 16) & 0x1f;
	rd = (inst >> 11) & 0x1f;
	sel = inst & 0x7;
	co_bit = (inst >> 25) & 1;

	if (co_bit) {
		op = (inst) & 0xff;

		switch (op) {
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
			break;
		case wait_op:
			er = kvm_mips_emul_wait(vcpu);
			break;
		}
	} else {
		switch (copz) {
		case mfc_op:
#ifdef CONFIG_KVM_MIPS_DEBUG_COP0_COUNTERS
			cop0->stat[rd][sel]++;
#endif
			/* Get reg */
			if ((rd == MIPS_CP0_COUNT) && (sel == 0)) {
				vcpu->arch.gprs[rt] = kvm_mips_read_count(vcpu);
			} else if ((rd == MIPS_CP0_ERRCTL) && (sel == 0)) {
				vcpu->arch.gprs[rt] = 0x0;
#ifdef CONFIG_KVM_MIPS_DYN_TRANS
				kvm_mips_trans_mfc0(inst, opc, vcpu);
#endif
			} else {
				vcpu->arch.gprs[rt] = cop0->reg[rd][sel];

#ifdef CONFIG_KVM_MIPS_DYN_TRANS
				kvm_mips_trans_mfc0(inst, opc, vcpu);
#endif
			}

			kvm_debug
			    ("[%#x] MFCz[%d][%d], vcpu->arch.gprs[%d]: %#lx\n",
			     pc, rd, sel, rt, vcpu->arch.gprs[rt]);

			break;

		case dmfc_op:
			vcpu->arch.gprs[rt] = cop0->reg[rd][sel];
			break;

		case mtc_op:
#ifdef CONFIG_KVM_MIPS_DEBUG_COP0_COUNTERS
			cop0->stat[rd][sel]++;
#endif
			if ((rd == MIPS_CP0_TLB_INDEX)
			    && (vcpu->arch.gprs[rt] >=
				KVM_MIPS_GUEST_TLB_SIZE)) {
				kvm_err("Invalid TLB Index: %ld",
					vcpu->arch.gprs[rt]);
				er = EMULATE_FAIL;
				break;
			}
#define C0_EBASE_CORE_MASK 0xff
			if ((rd == MIPS_CP0_PRID) && (sel == 1)) {
				/* Preserve CORE number */
				kvm_change_c0_guest_ebase(cop0,
							  ~(C0_EBASE_CORE_MASK),
							  vcpu->arch.gprs[rt]);
				kvm_err("MTCz, cop0->reg[EBASE]: %#lx\n",
					kvm_read_c0_guest_ebase(cop0));
			} else if (rd == MIPS_CP0_TLB_HI && sel == 0) {
				uint32_t nasid =
					vcpu->arch.gprs[rt] & ASID_MASK;
				if ((KSEGX(vcpu->arch.gprs[rt]) != CKSEG0) &&
				    ((kvm_read_c0_guest_entryhi(cop0) &
				      ASID_MASK) != nasid)) {
					kvm_debug("MTCz, change ASID from %#lx to %#lx\n",
						kvm_read_c0_guest_entryhi(cop0)
						& ASID_MASK,
						vcpu->arch.gprs[rt]
						& ASID_MASK);

					/* Blow away the shadow host TLBs */
					kvm_mips_flush_host_tlb(1);
				}
				kvm_write_c0_guest_entryhi(cop0,
							   vcpu->arch.gprs[rt]);
			}
			/* Are we writing to COUNT */
			else if ((rd == MIPS_CP0_COUNT) && (sel == 0)) {
				kvm_mips_write_count(vcpu, vcpu->arch.gprs[rt]);
				goto done;
			} else if ((rd == MIPS_CP0_COMPARE) && (sel == 0)) {
				kvm_debug("[%#x] MTCz, COMPARE %#lx <- %#lx\n",
					  pc, kvm_read_c0_guest_compare(cop0),
					  vcpu->arch.gprs[rt]);

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
				    vcpu->arch.fpu_inuse & KVM_MIPS_FPU_MSA)
					kvm_lose_fpu(vcpu);

				/*
				 * Propagate CU1 (FPU enable) changes
				 * immediately if the FPU context is already
				 * loaded. When disabling we leave the context
				 * loaded so it can be quickly enabled again in
				 * the near future.
				 */
				if (change & ST0_CU1 &&
				    vcpu->arch.fpu_inuse & KVM_MIPS_FPU_FPU)
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
				    vcpu->arch.fpu_inuse & KVM_MIPS_FPU_FPU)
					change_c0_config5(MIPS_CONF5_FRE, val);

				/*
				 * Propagate MSAEn changes immediately if the
				 * MSA context is already loaded. When disabling
				 * we leave the context loaded so it can be
				 * quickly enabled again in the near future.
				 */
				if (change & MIPS_CONF5_MSAEN &&
				    vcpu->arch.fpu_inuse & KVM_MIPS_FPU_MSA)
					change_c0_config5(MIPS_CONF5_MSAEN,
							  val);

				preempt_enable();

				kvm_write_c0_guest_config5(cop0, val);
			} else if ((rd == MIPS_CP0_CAUSE) && (sel == 0)) {
				uint32_t old_cause, new_cause;

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
			} else {
				cop0->reg[rd][sel] = vcpu->arch.gprs[rt];
#ifdef CONFIG_KVM_MIPS_DYN_TRANS
				kvm_mips_trans_mtc0(inst, opc, vcpu);
#endif
			}

			kvm_debug("[%#x] MTCz, cop0->reg[%d][%d]: %#lx\n", pc,
				  rd, sel, cop0->reg[rd][sel]);
			break;

		case dmtc_op:
			kvm_err("!!!!!!![%#lx]dmtc_op: rt: %d, rd: %d, sel: %d!!!!!!\n",
				vcpu->arch.pc, rt, rd, sel);
			er = EMULATE_FAIL;
			break;

		case mfmcz_op:
#ifdef KVM_MIPS_DEBUG_COP0_COUNTERS
			cop0->stat[MIPS_CP0_STATUS][0]++;
#endif
			if (rt != 0) {
				vcpu->arch.gprs[rt] =
				    kvm_read_c0_guest_status(cop0);
			}
			/* EI */
			if (inst & 0x20) {
				kvm_debug("[%#lx] mfmcz_op: EI\n",
					  vcpu->arch.pc);
				kvm_set_c0_guest_status(cop0, ST0_IE);
			} else {
				kvm_debug("[%#lx] mfmcz_op: DI\n",
					  vcpu->arch.pc);
				kvm_clear_c0_guest_status(cop0, ST0_IE);
			}

			break;

		case wrpgpr_op:
			{
				uint32_t css =
				    cop0->reg[MIPS_CP0_STATUS][2] & 0xf;
				uint32_t pss =
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
				vcpu->arch.pc, copz);
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

enum emulation_result kvm_mips_emulate_store(uint32_t inst, uint32_t cause,
					     struct kvm_run *run,
					     struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DO_MMIO;
	int32_t op, base, rt, offset;
	uint32_t bytes;
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

	rt = (inst >> 16) & 0x1f;
	base = (inst >> 21) & 0x1f;
	offset = inst & 0xffff;
	op = (inst >> 26) & 0x3f;

	switch (op) {
	case sb_op:
		bytes = 1;
		if (bytes > sizeof(run->mmio.data)) {
			kvm_err("%s: bad MMIO length: %d\n", __func__,
			       run->mmio.len);
		}
		run->mmio.phys_addr =
		    kvm_mips_callbacks->gva_to_gpa(vcpu->arch.
						   host_cp0_badvaddr);
		if (run->mmio.phys_addr == KVM_INVALID_ADDR) {
			er = EMULATE_FAIL;
			break;
		}
		run->mmio.len = bytes;
		run->mmio.is_write = 1;
		vcpu->mmio_needed = 1;
		vcpu->mmio_is_write = 1;
		*(u8 *) data = vcpu->arch.gprs[rt];
		kvm_debug("OP_SB: eaddr: %#lx, gpr: %#lx, data: %#x\n",
			  vcpu->arch.host_cp0_badvaddr, vcpu->arch.gprs[rt],
			  *(uint8_t *) data);

		break;

	case sw_op:
		bytes = 4;
		if (bytes > sizeof(run->mmio.data)) {
			kvm_err("%s: bad MMIO length: %d\n", __func__,
			       run->mmio.len);
		}
		run->mmio.phys_addr =
		    kvm_mips_callbacks->gva_to_gpa(vcpu->arch.
						   host_cp0_badvaddr);
		if (run->mmio.phys_addr == KVM_INVALID_ADDR) {
			er = EMULATE_FAIL;
			break;
		}

		run->mmio.len = bytes;
		run->mmio.is_write = 1;
		vcpu->mmio_needed = 1;
		vcpu->mmio_is_write = 1;
		*(uint32_t *) data = vcpu->arch.gprs[rt];

		kvm_debug("[%#lx] OP_SW: eaddr: %#lx, gpr: %#lx, data: %#x\n",
			  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
			  vcpu->arch.gprs[rt], *(uint32_t *) data);
		break;

	case sh_op:
		bytes = 2;
		if (bytes > sizeof(run->mmio.data)) {
			kvm_err("%s: bad MMIO length: %d\n", __func__,
			       run->mmio.len);
		}
		run->mmio.phys_addr =
		    kvm_mips_callbacks->gva_to_gpa(vcpu->arch.
						   host_cp0_badvaddr);
		if (run->mmio.phys_addr == KVM_INVALID_ADDR) {
			er = EMULATE_FAIL;
			break;
		}

		run->mmio.len = bytes;
		run->mmio.is_write = 1;
		vcpu->mmio_needed = 1;
		vcpu->mmio_is_write = 1;
		*(uint16_t *) data = vcpu->arch.gprs[rt];

		kvm_debug("[%#lx] OP_SH: eaddr: %#lx, gpr: %#lx, data: %#x\n",
			  vcpu->arch.pc, vcpu->arch.host_cp0_badvaddr,
			  vcpu->arch.gprs[rt], *(uint32_t *) data);
		break;

	default:
		kvm_err("Store not yet supported");
		er = EMULATE_FAIL;
		break;
	}

	/* Rollback PC if emulation was unsuccessful */
	if (er == EMULATE_FAIL)
		vcpu->arch.pc = curr_pc;

	return er;
}

enum emulation_result kvm_mips_emulate_load(uint32_t inst, uint32_t cause,
					    struct kvm_run *run,
					    struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DO_MMIO;
	int32_t op, base, rt, offset;
	uint32_t bytes;

	rt = (inst >> 16) & 0x1f;
	base = (inst >> 21) & 0x1f;
	offset = inst & 0xffff;
	op = (inst >> 26) & 0x3f;

	vcpu->arch.pending_load_cause = cause;
	vcpu->arch.io_gpr = rt;

	switch (op) {
	case lw_op:
		bytes = 4;
		if (bytes > sizeof(run->mmio.data)) {
			kvm_err("%s: bad MMIO length: %d\n", __func__,
			       run->mmio.len);
			er = EMULATE_FAIL;
			break;
		}
		run->mmio.phys_addr =
		    kvm_mips_callbacks->gva_to_gpa(vcpu->arch.
						   host_cp0_badvaddr);
		if (run->mmio.phys_addr == KVM_INVALID_ADDR) {
			er = EMULATE_FAIL;
			break;
		}

		run->mmio.len = bytes;
		run->mmio.is_write = 0;
		vcpu->mmio_needed = 1;
		vcpu->mmio_is_write = 0;
		break;

	case lh_op:
	case lhu_op:
		bytes = 2;
		if (bytes > sizeof(run->mmio.data)) {
			kvm_err("%s: bad MMIO length: %d\n", __func__,
			       run->mmio.len);
			er = EMULATE_FAIL;
			break;
		}
		run->mmio.phys_addr =
		    kvm_mips_callbacks->gva_to_gpa(vcpu->arch.
						   host_cp0_badvaddr);
		if (run->mmio.phys_addr == KVM_INVALID_ADDR) {
			er = EMULATE_FAIL;
			break;
		}

		run->mmio.len = bytes;
		run->mmio.is_write = 0;
		vcpu->mmio_needed = 1;
		vcpu->mmio_is_write = 0;

		if (op == lh_op)
			vcpu->mmio_needed = 2;
		else
			vcpu->mmio_needed = 1;

		break;

	case lbu_op:
	case lb_op:
		bytes = 1;
		if (bytes > sizeof(run->mmio.data)) {
			kvm_err("%s: bad MMIO length: %d\n", __func__,
			       run->mmio.len);
			er = EMULATE_FAIL;
			break;
		}
		run->mmio.phys_addr =
		    kvm_mips_callbacks->gva_to_gpa(vcpu->arch.
						   host_cp0_badvaddr);
		if (run->mmio.phys_addr == KVM_INVALID_ADDR) {
			er = EMULATE_FAIL;
			break;
		}

		run->mmio.len = bytes;
		run->mmio.is_write = 0;
		vcpu->mmio_is_write = 0;

		if (op == lb_op)
			vcpu->mmio_needed = 2;
		else
			vcpu->mmio_needed = 1;

		break;

	default:
		kvm_err("Load not yet supported");
		er = EMULATE_FAIL;
		break;
	}

	return er;
}

int kvm_mips_sync_icache(unsigned long va, struct kvm_vcpu *vcpu)
{
	unsigned long offset = (va & ~PAGE_MASK);
	struct kvm *kvm = vcpu->kvm;
	unsigned long pa;
	gfn_t gfn;
	pfn_t pfn;

	gfn = va >> PAGE_SHIFT;

	if (gfn >= kvm->arch.guest_pmap_npages) {
		kvm_err("%s: Invalid gfn: %#llx\n", __func__, gfn);
		kvm_mips_dump_host_tlbs();
		kvm_arch_vcpu_dump_regs(vcpu);
		return -1;
	}
	pfn = kvm->arch.guest_pmap[gfn];
	pa = (pfn << PAGE_SHIFT) | offset;

	kvm_debug("%s: va: %#lx, unmapped: %#x\n", __func__, va,
		  CKSEG0ADDR(pa));

	local_flush_icache_range(CKSEG0ADDR(pa), 32);
	return 0;
}

#define MIPS_CACHE_OP_INDEX_INV         0x0
#define MIPS_CACHE_OP_INDEX_LD_TAG      0x1
#define MIPS_CACHE_OP_INDEX_ST_TAG      0x2
#define MIPS_CACHE_OP_IMP               0x3
#define MIPS_CACHE_OP_HIT_INV           0x4
#define MIPS_CACHE_OP_FILL_WB_INV       0x5
#define MIPS_CACHE_OP_HIT_HB            0x6
#define MIPS_CACHE_OP_FETCH_LOCK        0x7

#define MIPS_CACHE_ICACHE               0x0
#define MIPS_CACHE_DCACHE               0x1
#define MIPS_CACHE_SEC                  0x3

enum emulation_result kvm_mips_emulate_cache(uint32_t inst, uint32_t *opc,
					     uint32_t cause,
					     struct kvm_run *run,
					     struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	enum emulation_result er = EMULATE_DONE;
	int32_t offset, cache, op_inst, op, base;
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

	base = (inst >> 21) & 0x1f;
	op_inst = (inst >> 16) & 0x1f;
	offset = (int16_t)inst;
	cache = (inst >> 16) & 0x3;
	op = (inst >> 18) & 0x7;

	va = arch->gprs[base] + offset;

	kvm_debug("CACHE (cache: %#x, op: %#x, base[%d]: %#lx, offset: %#x\n",
		  cache, op, base, arch->gprs[base], offset);

	/*
	 * Treat INDEX_INV as a nop, basically issued by Linux on startup to
	 * invalidate the caches entirely by stepping through all the
	 * ways/indexes
	 */
	if (op == MIPS_CACHE_OP_INDEX_INV) {
		kvm_debug("@ %#lx/%#lx CACHE (cache: %#x, op: %#x, base[%d]: %#lx, offset: %#x\n",
			  vcpu->arch.pc, vcpu->arch.gprs[31], cache, op, base,
			  arch->gprs[base], offset);

		if (cache == MIPS_CACHE_DCACHE)
			r4k_blast_dcache();
		else if (cache == MIPS_CACHE_ICACHE)
			r4k_blast_icache();
		else {
			kvm_err("%s: unsupported CACHE INDEX operation\n",
				__func__);
			return EMULATE_FAIL;
		}

#ifdef CONFIG_KVM_MIPS_DYN_TRANS
		kvm_mips_trans_cache_index(inst, opc, vcpu);
#endif
		goto done;
	}

	preempt_disable();
	if (KVM_GUEST_KSEGX(va) == KVM_GUEST_KSEG0) {
		if (kvm_mips_host_tlb_lookup(vcpu, va) < 0 &&
		    kvm_mips_handle_kseg0_tlb_fault(va, vcpu)) {
			kvm_err("%s: handling mapped kseg0 tlb fault for %lx, vcpu: %p, ASID: %#lx\n",
				__func__, va, vcpu, read_c0_entryhi());
			er = EMULATE_FAIL;
			preempt_enable();
			goto done;
		}
	} else if ((KVM_GUEST_KSEGX(va) < KVM_GUEST_KSEG0) ||
		   KVM_GUEST_KSEGX(va) == KVM_GUEST_KSEG23) {
		int index;

		/* If an entry already exists then skip */
		if (kvm_mips_host_tlb_lookup(vcpu, va) >= 0)
			goto skip_fault;

		/*
		 * If address not in the guest TLB, then give the guest a fault,
		 * the resulting handler will do the right thing
		 */
		index = kvm_mips_guest_tlb_lookup(vcpu, (va & VPN2_MASK) |
						  (kvm_read_c0_guest_entryhi
						   (cop0) & ASID_MASK));

		if (index < 0) {
			vcpu->arch.host_cp0_entryhi = (va & VPN2_MASK);
			vcpu->arch.host_cp0_badvaddr = va;
			er = kvm_mips_emulate_tlbmiss_ld(cause, NULL, run,
							 vcpu);
			preempt_enable();
			goto dont_update_pc;
		} else {
			struct kvm_mips_tlb *tlb = &vcpu->arch.guest_tlb[index];
			/*
			 * Check if the entry is valid, if not then setup a TLB
			 * invalid exception to the guest
			 */
			if (!TLB_IS_VALID(*tlb, va)) {
				er = kvm_mips_emulate_tlbinv_ld(cause, NULL,
								run, vcpu);
				preempt_enable();
				goto dont_update_pc;
			}
			/*
			 * We fault an entry from the guest tlb to the
			 * shadow host TLB
			 */
			if (kvm_mips_handle_mapped_seg_tlb_fault(vcpu, tlb,
								 NULL, NULL)) {
				kvm_err("%s: handling mapped seg tlb fault for %lx, index: %u, vcpu: %p, ASID: %#lx\n",
					__func__, va, index, vcpu,
					read_c0_entryhi());
				er = EMULATE_FAIL;
				preempt_enable();
				goto done;
			}
		}
	} else {
		kvm_err("INVALID CACHE INDEX/ADDRESS (cache: %#x, op: %#x, base[%d]: %#lx, offset: %#x\n",
			cache, op, base, arch->gprs[base], offset);
		er = EMULATE_FAIL;
		preempt_enable();
		goto dont_update_pc;

	}

skip_fault:
	/* XXXKYMA: Only a subset of cache ops are supported, used by Linux */
	if (cache == MIPS_CACHE_DCACHE
	    && (op == MIPS_CACHE_OP_FILL_WB_INV
		|| op == MIPS_CACHE_OP_HIT_INV)) {
		flush_dcache_line(va);

#ifdef CONFIG_KVM_MIPS_DYN_TRANS
		/*
		 * Replace the CACHE instruction, with a SYNCI, not the same,
		 * but avoids a trap
		 */
		kvm_mips_trans_cache_va(inst, opc, vcpu);
#endif
	} else if (op == MIPS_CACHE_OP_HIT_INV && cache == MIPS_CACHE_ICACHE) {
		flush_dcache_line(va);
		flush_icache_line(va);

#ifdef CONFIG_KVM_MIPS_DYN_TRANS
		/* Replace the CACHE instruction, with a SYNCI */
		kvm_mips_trans_cache_va(inst, opc, vcpu);
#endif
	} else {
		kvm_err("NO-OP CACHE (cache: %#x, op: %#x, base[%d]: %#lx, offset: %#x\n",
			cache, op, base, arch->gprs[base], offset);
		er = EMULATE_FAIL;
		preempt_enable();
		goto dont_update_pc;
	}

	preempt_enable();

dont_update_pc:
	/* Rollback PC */
	vcpu->arch.pc = curr_pc;
done:
	return er;
}

enum emulation_result kvm_mips_emulate_inst(unsigned long cause, uint32_t *opc,
					    struct kvm_run *run,
					    struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
	uint32_t inst;

	/* Fetch the instruction. */
	if (cause & CAUSEF_BD)
		opc += 1;

	inst = kvm_get_inst(opc, vcpu);

	switch (((union mips_instruction)inst).r_format.opcode) {
	case cop0_op:
		er = kvm_mips_emulate_CP0(inst, opc, cause, run, vcpu);
		break;
	case sb_op:
	case sh_op:
	case sw_op:
		er = kvm_mips_emulate_store(inst, cause, run, vcpu);
		break;
	case lb_op:
	case lbu_op:
	case lhu_op:
	case lh_op:
	case lw_op:
		er = kvm_mips_emulate_load(inst, cause, run, vcpu);
		break;

	case cache_op:
		++vcpu->stat.cache_exits;
		trace_kvm_exit(vcpu, CACHE_EXITS);
		er = kvm_mips_emulate_cache(inst, opc, cause, run, vcpu);
		break;

	default:
		kvm_err("Instruction emulation not supported (%p/%#x)\n", opc,
			inst);
		kvm_arch_vcpu_dump_regs(vcpu);
		er = EMULATE_FAIL;
		break;
	}

	return er;
}

enum emulation_result kvm_mips_emulate_syscall(unsigned long cause,
					       uint32_t *opc,
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
					  (T_SYSCALL << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = KVM_GUEST_KSEG0 + 0x180;

	} else {
		kvm_err("Trying to deliver SYSCALL when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_emulate_tlbmiss_ld(unsigned long cause,
						  uint32_t *opc,
						  struct kvm_run *run,
						  struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	unsigned long entryhi = (vcpu->arch.  host_cp0_badvaddr & VPN2_MASK) |
				(kvm_read_c0_guest_entryhi(cop0) & ASID_MASK);

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
		arch->pc = KVM_GUEST_KSEG0 + 0x0;

	} else {
		kvm_debug("[EXL == 1] delivering TLB MISS @ pc %#lx\n",
			  arch->pc);

		arch->pc = KVM_GUEST_KSEG0 + 0x180;
	}

	kvm_change_c0_guest_cause(cop0, (0xff),
				  (T_TLB_LD_MISS << CAUSEB_EXCCODE));

	/* setup badvaddr, context and entryhi registers for the guest */
	kvm_write_c0_guest_badvaddr(cop0, vcpu->arch.host_cp0_badvaddr);
	/* XXXKYMA: is the context register used by linux??? */
	kvm_write_c0_guest_entryhi(cop0, entryhi);
	/* Blow away the shadow host TLBs */
	kvm_mips_flush_host_tlb(1);

	return EMULATE_DONE;
}

enum emulation_result kvm_mips_emulate_tlbinv_ld(unsigned long cause,
						 uint32_t *opc,
						 struct kvm_run *run,
						 struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	unsigned long entryhi =
		(vcpu->arch.host_cp0_badvaddr & VPN2_MASK) |
		(kvm_read_c0_guest_entryhi(cop0) & ASID_MASK);

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

		/* set pc to the exception entry point */
		arch->pc = KVM_GUEST_KSEG0 + 0x180;

	} else {
		kvm_debug("[EXL == 1] delivering TLB MISS @ pc %#lx\n",
			  arch->pc);
		arch->pc = KVM_GUEST_KSEG0 + 0x180;
	}

	kvm_change_c0_guest_cause(cop0, (0xff),
				  (T_TLB_LD_MISS << CAUSEB_EXCCODE));

	/* setup badvaddr, context and entryhi registers for the guest */
	kvm_write_c0_guest_badvaddr(cop0, vcpu->arch.host_cp0_badvaddr);
	/* XXXKYMA: is the context register used by linux??? */
	kvm_write_c0_guest_entryhi(cop0, entryhi);
	/* Blow away the shadow host TLBs */
	kvm_mips_flush_host_tlb(1);

	return EMULATE_DONE;
}

enum emulation_result kvm_mips_emulate_tlbmiss_st(unsigned long cause,
						  uint32_t *opc,
						  struct kvm_run *run,
						  struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	unsigned long entryhi = (vcpu->arch.host_cp0_badvaddr & VPN2_MASK) |
				(kvm_read_c0_guest_entryhi(cop0) & ASID_MASK);

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
		arch->pc = KVM_GUEST_KSEG0 + 0x0;
	} else {
		kvm_debug("[EXL == 1] Delivering TLB MISS @ pc %#lx\n",
			  arch->pc);
		arch->pc = KVM_GUEST_KSEG0 + 0x180;
	}

	kvm_change_c0_guest_cause(cop0, (0xff),
				  (T_TLB_ST_MISS << CAUSEB_EXCCODE));

	/* setup badvaddr, context and entryhi registers for the guest */
	kvm_write_c0_guest_badvaddr(cop0, vcpu->arch.host_cp0_badvaddr);
	/* XXXKYMA: is the context register used by linux??? */
	kvm_write_c0_guest_entryhi(cop0, entryhi);
	/* Blow away the shadow host TLBs */
	kvm_mips_flush_host_tlb(1);

	return EMULATE_DONE;
}

enum emulation_result kvm_mips_emulate_tlbinv_st(unsigned long cause,
						 uint32_t *opc,
						 struct kvm_run *run,
						 struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	unsigned long entryhi = (vcpu->arch.host_cp0_badvaddr & VPN2_MASK) |
		(kvm_read_c0_guest_entryhi(cop0) & ASID_MASK);

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
		arch->pc = KVM_GUEST_KSEG0 + 0x180;
	} else {
		kvm_debug("[EXL == 1] Delivering TLB MISS @ pc %#lx\n",
			  arch->pc);
		arch->pc = KVM_GUEST_KSEG0 + 0x180;
	}

	kvm_change_c0_guest_cause(cop0, (0xff),
				  (T_TLB_ST_MISS << CAUSEB_EXCCODE));

	/* setup badvaddr, context and entryhi registers for the guest */
	kvm_write_c0_guest_badvaddr(cop0, vcpu->arch.host_cp0_badvaddr);
	/* XXXKYMA: is the context register used by linux??? */
	kvm_write_c0_guest_entryhi(cop0, entryhi);
	/* Blow away the shadow host TLBs */
	kvm_mips_flush_host_tlb(1);

	return EMULATE_DONE;
}

/* TLBMOD: store into address matching TLB with Dirty bit off */
enum emulation_result kvm_mips_handle_tlbmod(unsigned long cause, uint32_t *opc,
					     struct kvm_run *run,
					     struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
#ifdef DEBUG
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	unsigned long entryhi = (vcpu->arch.host_cp0_badvaddr & VPN2_MASK) |
				(kvm_read_c0_guest_entryhi(cop0) & ASID_MASK);
	int index;

	/* If address not in the guest TLB, then we are in trouble */
	index = kvm_mips_guest_tlb_lookup(vcpu, entryhi);
	if (index < 0) {
		/* XXXKYMA Invalidate and retry */
		kvm_mips_host_tlb_inv(vcpu, vcpu->arch.host_cp0_badvaddr);
		kvm_err("%s: host got TLBMOD for %#lx but entry not present in Guest TLB\n",
		     __func__, entryhi);
		kvm_mips_dump_guest_tlbs(vcpu);
		kvm_mips_dump_host_tlbs();
		return EMULATE_FAIL;
	}
#endif

	er = kvm_mips_emulate_tlbmod(cause, opc, run, vcpu);
	return er;
}

enum emulation_result kvm_mips_emulate_tlbmod(unsigned long cause,
					      uint32_t *opc,
					      struct kvm_run *run,
					      struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	unsigned long entryhi = (vcpu->arch.host_cp0_badvaddr & VPN2_MASK) |
				(kvm_read_c0_guest_entryhi(cop0) & ASID_MASK);
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

		arch->pc = KVM_GUEST_KSEG0 + 0x180;
	} else {
		kvm_debug("[EXL == 1] Delivering TLB MOD @ pc %#lx\n",
			  arch->pc);
		arch->pc = KVM_GUEST_KSEG0 + 0x180;
	}

	kvm_change_c0_guest_cause(cop0, (0xff), (T_TLB_MOD << CAUSEB_EXCCODE));

	/* setup badvaddr, context and entryhi registers for the guest */
	kvm_write_c0_guest_badvaddr(cop0, vcpu->arch.host_cp0_badvaddr);
	/* XXXKYMA: is the context register used by linux??? */
	kvm_write_c0_guest_entryhi(cop0, entryhi);
	/* Blow away the shadow host TLBs */
	kvm_mips_flush_host_tlb(1);

	return EMULATE_DONE;
}

enum emulation_result kvm_mips_emulate_fpu_exc(unsigned long cause,
					       uint32_t *opc,
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

	arch->pc = KVM_GUEST_KSEG0 + 0x180;

	kvm_change_c0_guest_cause(cop0, (0xff),
				  (T_COP_UNUSABLE << CAUSEB_EXCCODE));
	kvm_change_c0_guest_cause(cop0, (CAUSEF_CE), (0x1 << CAUSEB_CE));

	return EMULATE_DONE;
}

enum emulation_result kvm_mips_emulate_ri_exc(unsigned long cause,
					      uint32_t *opc,
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
					  (T_RES_INST << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = KVM_GUEST_KSEG0 + 0x180;

	} else {
		kvm_err("Trying to deliver RI when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_emulate_bp_exc(unsigned long cause,
					      uint32_t *opc,
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
					  (T_BREAK << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = KVM_GUEST_KSEG0 + 0x180;

	} else {
		kvm_err("Trying to deliver BP when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_emulate_trap_exc(unsigned long cause,
						uint32_t *opc,
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
					  (T_TRAP << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = KVM_GUEST_KSEG0 + 0x180;

	} else {
		kvm_err("Trying to deliver TRAP when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_emulate_msafpe_exc(unsigned long cause,
						  uint32_t *opc,
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
					  (T_MSAFPE << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = KVM_GUEST_KSEG0 + 0x180;

	} else {
		kvm_err("Trying to deliver MSAFPE when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_emulate_fpe_exc(unsigned long cause,
					       uint32_t *opc,
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
					  (T_FPE << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = KVM_GUEST_KSEG0 + 0x180;

	} else {
		kvm_err("Trying to deliver FPE when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

enum emulation_result kvm_mips_emulate_msadis_exc(unsigned long cause,
						  uint32_t *opc,
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
					  (T_MSADIS << CAUSEB_EXCCODE));

		/* Set PC to the exception entry point */
		arch->pc = KVM_GUEST_KSEG0 + 0x180;

	} else {
		kvm_err("Trying to deliver MSADIS when EXL is already set\n");
		er = EMULATE_FAIL;
	}

	return er;
}

/* ll/sc, rdhwr, sync emulation */

#define OPCODE 0xfc000000
#define BASE   0x03e00000
#define RT     0x001f0000
#define OFFSET 0x0000ffff
#define LL     0xc0000000
#define SC     0xe0000000
#define SPEC0  0x00000000
#define SPEC3  0x7c000000
#define RD     0x0000f800
#define FUNC   0x0000003f
#define SYNC   0x0000000f
#define RDHWR  0x0000003b

enum emulation_result kvm_mips_handle_ri(unsigned long cause, uint32_t *opc,
					 struct kvm_run *run,
					 struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	enum emulation_result er = EMULATE_DONE;
	unsigned long curr_pc;
	uint32_t inst;

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

	inst = kvm_get_inst(opc, vcpu);

	if (inst == KVM_INVALID_INST) {
		kvm_err("%s: Cannot get inst @ %p\n", __func__, opc);
		return EMULATE_FAIL;
	}

	if ((inst & OPCODE) == SPEC3 && (inst & FUNC) == RDHWR) {
		int usermode = !KVM_GUEST_KERNEL_MODE(vcpu);
		int rd = (inst & RD) >> 11;
		int rt = (inst & RT) >> 16;
		/* If usermode, check RDHWR rd is allowed by guest HWREna */
		if (usermode && !(kvm_read_c0_guest_hwrena(cop0) & BIT(rd))) {
			kvm_debug("RDHWR %#x disallowed by HWREna @ %p\n",
				  rd, opc);
			goto emulate_ri;
		}
		switch (rd) {
		case 0:	/* CPU number */
			arch->gprs[rt] = 0;
			break;
		case 1:	/* SYNCI length */
			arch->gprs[rt] = min(current_cpu_data.dcache.linesz,
					     current_cpu_data.icache.linesz);
			break;
		case 2:	/* Read count register */
			arch->gprs[rt] = kvm_mips_read_count(vcpu);
			break;
		case 3:	/* Count register resolution */
			switch (current_cpu_data.cputype) {
			case CPU_20KC:
			case CPU_25KF:
				arch->gprs[rt] = 1;
				break;
			default:
				arch->gprs[rt] = 2;
			}
			break;
		case 29:
			arch->gprs[rt] = kvm_read_c0_guest_userlocal(cop0);
			break;

		default:
			kvm_debug("RDHWR %#x not supported @ %p\n", rd, opc);
			goto emulate_ri;
		}
	} else {
		kvm_debug("Emulate RI not supported @ %p: %#x\n", opc, inst);
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

	er = update_pc(vcpu, vcpu->arch.pending_load_cause);
	if (er == EMULATE_FAIL)
		return er;

	switch (run->mmio.len) {
	case 4:
		*gpr = *(int32_t *) run->mmio.data;
		break;

	case 2:
		if (vcpu->mmio_needed == 2)
			*gpr = *(int16_t *) run->mmio.data;
		else
			*gpr = *(uint16_t *)run->mmio.data;

		break;
	case 1:
		if (vcpu->mmio_needed == 2)
			*gpr = *(int8_t *) run->mmio.data;
		else
			*gpr = *(u8 *) run->mmio.data;
		break;
	}

	if (vcpu->arch.pending_load_cause & CAUSEF_BD)
		kvm_debug("[%#lx] Completing %d byte BD Load to gpr %d (0x%08lx) type %d\n",
			  vcpu->arch.pc, run->mmio.len, vcpu->arch.io_gpr, *gpr,
			  vcpu->mmio_needed);

done:
	return er;
}

static enum emulation_result kvm_mips_emulate_exc(unsigned long cause,
						  uint32_t *opc,
						  struct kvm_run *run,
						  struct kvm_vcpu *vcpu)
{
	uint32_t exccode = (cause >> CAUSEB_EXCCODE) & 0x1f;
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
		arch->pc = KVM_GUEST_KSEG0 + 0x180;
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

enum emulation_result kvm_mips_check_privilege(unsigned long cause,
					       uint32_t *opc,
					       struct kvm_run *run,
					       struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
	uint32_t exccode = (cause >> CAUSEB_EXCCODE) & 0x1f;
	unsigned long badvaddr = vcpu->arch.host_cp0_badvaddr;

	int usermode = !KVM_GUEST_KERNEL_MODE(vcpu);

	if (usermode) {
		switch (exccode) {
		case T_INT:
		case T_SYSCALL:
		case T_BREAK:
		case T_RES_INST:
		case T_TRAP:
		case T_MSAFPE:
		case T_FPE:
		case T_MSADIS:
			break;

		case T_COP_UNUSABLE:
			if (((cause & CAUSEF_CE) >> CAUSEB_CE) == 0)
				er = EMULATE_PRIV_FAIL;
			break;

		case T_TLB_MOD:
			break;

		case T_TLB_LD_MISS:
			/*
			 * We we are accessing Guest kernel space, then send an
			 * address error exception to the guest
			 */
			if (badvaddr >= (unsigned long) KVM_GUEST_KSEG0) {
				kvm_debug("%s: LD MISS @ %#lx\n", __func__,
					  badvaddr);
				cause &= ~0xff;
				cause |= (T_ADDR_ERR_LD << CAUSEB_EXCCODE);
				er = EMULATE_PRIV_FAIL;
			}
			break;

		case T_TLB_ST_MISS:
			/*
			 * We we are accessing Guest kernel space, then send an
			 * address error exception to the guest
			 */
			if (badvaddr >= (unsigned long) KVM_GUEST_KSEG0) {
				kvm_debug("%s: ST MISS @ %#lx\n", __func__,
					  badvaddr);
				cause &= ~0xff;
				cause |= (T_ADDR_ERR_ST << CAUSEB_EXCCODE);
				er = EMULATE_PRIV_FAIL;
			}
			break;

		case T_ADDR_ERR_ST:
			kvm_debug("%s: address error ST @ %#lx\n", __func__,
				  badvaddr);
			if ((badvaddr & PAGE_MASK) == KVM_GUEST_COMMPAGE_ADDR) {
				cause &= ~0xff;
				cause |= (T_TLB_ST_MISS << CAUSEB_EXCCODE);
			}
			er = EMULATE_PRIV_FAIL;
			break;
		case T_ADDR_ERR_LD:
			kvm_debug("%s: address error LD @ %#lx\n", __func__,
				  badvaddr);
			if ((badvaddr & PAGE_MASK) == KVM_GUEST_COMMPAGE_ADDR) {
				cause &= ~0xff;
				cause |= (T_TLB_LD_MISS << CAUSEB_EXCCODE);
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
enum emulation_result kvm_mips_handle_tlbmiss(unsigned long cause,
					      uint32_t *opc,
					      struct kvm_run *run,
					      struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
	uint32_t exccode = (cause >> CAUSEB_EXCCODE) & 0x1f;
	unsigned long va = vcpu->arch.host_cp0_badvaddr;
	int index;

	kvm_debug("kvm_mips_handle_tlbmiss: badvaddr: %#lx, entryhi: %#lx\n",
		  vcpu->arch.host_cp0_badvaddr, vcpu->arch.host_cp0_entryhi);

	/*
	 * KVM would not have got the exception if this entry was valid in the
	 * shadow host TLB. Check the Guest TLB, if the entry is not there then
	 * send the guest an exception. The guest exc handler should then inject
	 * an entry into the guest TLB.
	 */
	index = kvm_mips_guest_tlb_lookup(vcpu,
					  (va & VPN2_MASK) |
					  (kvm_read_c0_guest_entryhi
					   (vcpu->arch.cop0) & ASID_MASK));
	if (index < 0) {
		if (exccode == T_TLB_LD_MISS) {
			er = kvm_mips_emulate_tlbmiss_ld(cause, opc, run, vcpu);
		} else if (exccode == T_TLB_ST_MISS) {
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
			if (exccode == T_TLB_LD_MISS) {
				er = kvm_mips_emulate_tlbinv_ld(cause, opc, run,
								vcpu);
			} else if (exccode == T_TLB_ST_MISS) {
				er = kvm_mips_emulate_tlbinv_st(cause, opc, run,
								vcpu);
			} else {
				kvm_err("%s: invalid exc code: %d\n", __func__,
					exccode);
				er = EMULATE_FAIL;
			}
		} else {
			kvm_debug("Injecting hi: %#lx, lo0: %#lx, lo1: %#lx into shadow host TLB\n",
				  tlb->tlb_hi, tlb->tlb_lo0, tlb->tlb_lo1);
			/*
			 * OK we have a Guest TLB entry, now inject it into the
			 * shadow host TLB
			 */
			if (kvm_mips_handle_mapped_seg_tlb_fault(vcpu, tlb,
								 NULL, NULL)) {
				kvm_err("%s: handling mapped seg tlb fault for %lx, index: %u, vcpu: %p, ASID: %#lx\n",
					__func__, va, index, vcpu,
					read_c0_entryhi());
				er = EMULATE_FAIL;
			}
		}
	}

	return er;
}
