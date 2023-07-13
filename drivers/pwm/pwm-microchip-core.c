// SPDX-License-Identifier: GPL-2.0
/*
 * corePWM driver for Microchip "soft" FPGA IP cores.
 *
 * Copyright (c) 2021-2023 Microchip Corporation. All rights reserved.
 * Author: Conor Dooley <conor.dooley@microchip.com>
 * Documentation:
 * https://www.microsemi.com/document-portal/doc_download/1245275-corepwm-hb
 *
 * Limitations:
 * - If the IP block is configured without "shadow registers", all register
 *   writes will take effect immediately, causing glitches on the output.
 *   If shadow registers *are* enabled, setting the "SYNC_UPDATE" register
 *   notifies the core that it needs to update the registers defining the
 *   waveform from the contents of the "shadow registers". Otherwise, changes
 *   will take effective immediately, even for those channels.
 *   As setting the period/duty cycle takes 4 register writes, there is a window
 *   in which this races against the start of a new period.
 * - The IP block has no concept of a duty cycle, only rising/falling edges of
 *   the waveform. Unfortunately, if the rising & falling edges registers have
 *   the same value written to them the IP block will do whichever of a rising
 *   or a falling edge is possible. I.E. a 50% waveform at twice the requested
 *   period. Therefore to get a 0% waveform, the output is set the max high/low
 *   time depending on polarity.
 *   If the duty cycle is 0%, and the requested period is less than the
 *   available period resolution, this will manifest as a ~100% waveform (with
 *   some output glitches) rather than 50%.
 * - The PWM period is set for the whole IP block not per channel. The driver
 *   will only change the period if no other PWM output is enabled.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#define MCHPCOREPWM_PRESCALE_MAX	0xff
#define MCHPCOREPWM_PERIOD_STEPS_MAX	0xfe
#define MCHPCOREPWM_PERIOD_MAX		0xff00

#define MCHPCOREPWM_PRESCALE	0x00
#define MCHPCOREPWM_PERIOD	0x04
#define MCHPCOREPWM_EN(i)	(0x08 + 0x04 * (i)) /* 0x08, 0x0c */
#define MCHPCOREPWM_POSEDGE(i)	(0x10 + 0x08 * (i)) /* 0x10, 0x18, ..., 0x88 */
#define MCHPCOREPWM_NEGEDGE(i)	(0x14 + 0x08 * (i)) /* 0x14, 0x1c, ..., 0x8c */
#define MCHPCOREPWM_SYNC_UPD	0xe4
#define MCHPCOREPWM_TIMEOUT_MS	100u

struct mchp_core_pwm_chip {
	struct pwm_chip chip;
	struct clk *clk;
	void __iomem *base;
	struct mutex lock; /* protects the shared period */
	ktime_t update_timestamp;
	u32 sync_update_mask;
	u16 channel_enabled;
};

static inline struct mchp_core_pwm_chip *to_mchp_core_pwm(struct pwm_chip *chip)
{
	return container_of(chip, struct mchp_core_pwm_chip, chip);
}

static void mchp_core_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm,
				 bool enable, u64 period)
{
	struct mchp_core_pwm_chip *mchp_core_pwm = to_mchp_core_pwm(chip);
	u8 channel_enable, reg_offset, shift;

	/*
	 * There are two adjacent 8 bit control regs, the lower reg controls
	 * 0-7 and the upper reg 8-15. Check if the pwm is in the upper reg
	 * and if so, offset by the bus width.
	 */
	reg_offset = MCHPCOREPWM_EN(pwm->hwpwm >> 3);
	shift = pwm->hwpwm & 7;

	channel_enable = readb_relaxed(mchp_core_pwm->base + reg_offset);
	channel_enable &= ~(1 << shift);
	channel_enable |= (enable << shift);

	writel_relaxed(channel_enable, mchp_core_pwm->base + reg_offset);
	mchp_core_pwm->channel_enabled &= ~BIT(pwm->hwpwm);
	mchp_core_pwm->channel_enabled |= enable << pwm->hwpwm;

	/*
	 * The updated values will not appear on the bus until they have been
	 * applied to the waveform at the beginning of the next period.
	 * This is a NO-OP if the channel does not have shadow registers.
	 */
	if (mchp_core_pwm->sync_update_mask & (1 << pwm->hwpwm))
		mchp_core_pwm->update_timestamp = ktime_add_ns(ktime_get(), period);
}

static void mchp_core_pwm_wait_for_sync_update(struct mchp_core_pwm_chip *mchp_core_pwm,
					       unsigned int channel)
{
	/*
	 * If a shadow register is used for this PWM channel, and iff there is
	 * a pending update to the waveform, we must wait for it to be applied
	 * before attempting to read its state. Reading the registers yields
	 * the currently implemented settings & the new ones are only readable
	 * once the current period has ended.
	 */

	if (mchp_core_pwm->sync_update_mask & (1 << channel)) {
		ktime_t current_time = ktime_get();
		s64 remaining_ns;
		u32 delay_us;

		remaining_ns = ktime_to_ns(ktime_sub(mchp_core_pwm->update_timestamp,
						     current_time));

		/*
		 * If the update has gone through, don't bother waiting for
		 * obvious reasons. Otherwise wait around for an appropriate
		 * amount of time for the update to go through.
		 */
		if (remaining_ns <= 0)
			return;

		delay_us = DIV_ROUND_UP_ULL(remaining_ns, NSEC_PER_USEC);
		fsleep(delay_us);
	}
}

static u64 mchp_core_pwm_calc_duty(const struct pwm_state *state, u64 clk_rate,
				   u8 prescale, u8 period_steps)
{
	u64 duty_steps, tmp;

	/*
	 * Calculate the duty cycle in multiples of the prescaled period:
	 * duty_steps = duty_in_ns / step_in_ns
	 * step_in_ns = (prescale * NSEC_PER_SEC) / clk_rate
	 * The code below is rearranged slightly to only divide once.
	 */
	tmp = (((u64)prescale) + 1) * NSEC_PER_SEC;
	duty_steps = mul_u64_u64_div_u64(state->duty_cycle, clk_rate, tmp);

	return duty_steps;
}

static void mchp_core_pwm_apply_duty(struct pwm_chip *chip, struct pwm_device *pwm,
				     const struct pwm_state *state, u64 duty_steps,
				     u16 period_steps)
{
	struct mchp_core_pwm_chip *mchp_core_pwm = to_mchp_core_pwm(chip);
	u8 posedge, negedge;
	u8 first_edge = 0, second_edge = duty_steps;

	/*
	 * Setting posedge == negedge doesn't yield a constant output,
	 * so that's an unsuitable setting to model duty_steps = 0.
	 * In that case set the unwanted edge to a value that never
	 * triggers.
	 */
	if (duty_steps == 0)
		first_edge = period_steps + 1;

	if (state->polarity == PWM_POLARITY_INVERSED) {
		negedge = first_edge;
		posedge = second_edge;
	} else {
		posedge = first_edge;
		negedge = second_edge;
	}

	/*
	 * Set the sync bit which ensures that periods that already started are
	 * completed unaltered. At each counter reset event the values are
	 * updated from the shadow registers.
	 */
	writel_relaxed(posedge, mchp_core_pwm->base + MCHPCOREPWM_POSEDGE(pwm->hwpwm));
	writel_relaxed(negedge, mchp_core_pwm->base + MCHPCOREPWM_NEGEDGE(pwm->hwpwm));
}

static int mchp_core_pwm_calc_period(const struct pwm_state *state, unsigned long clk_rate,
				     u16 *prescale, u16 *period_steps)
{
	u64 tmp;

	/*
	 * Calculate the period cycles and prescale values.
	 * The registers are each 8 bits wide & multiplied to compute the period
	 * using the formula:
	 *           (prescale + 1) * (period_steps + 1)
	 * period = -------------------------------------
	 *                      clk_rate
	 * so the maximum period that can be generated is 0x10000 times the
	 * period of the input clock.
	 * However, due to the design of the "hardware", it is not possible to
	 * attain a 100% duty cycle if the full range of period_steps is used.
	 * Therefore period_steps is restricted to 0xfe and the maximum multiple
	 * of the clock period attainable is (0xff + 1) * (0xfe + 1) = 0xff00
	 *
	 * The prescale and period_steps registers operate similarly to
	 * CLK_DIVIDER_ONE_BASED, where the value used by the hardware is that
	 * in the register plus one.
	 * It's therefore not possible to set a period lower than 1/clk_rate, so
	 * if tmp is 0, abort. Without aborting, we will set a period that is
	 * greater than that requested and, more importantly, will trigger the
	 * neg-/pos-edge issue described in the limitations.
	 */
	tmp = mul_u64_u64_div_u64(state->period, clk_rate, NSEC_PER_SEC);
	if (tmp >= MCHPCOREPWM_PERIOD_MAX) {
		*prescale = MCHPCOREPWM_PRESCALE_MAX;
		*period_steps = MCHPCOREPWM_PERIOD_STEPS_MAX;

		return 0;
	}

	/*
	 * There are multiple strategies that could be used to choose the
	 * prescale & period_steps values.
	 * Here the idea is to pick values so that the selection of duty cycles
	 * is as finegrain as possible, while also keeping the period less than
	 * that requested.
	 *
	 * A simple way to satisfy the first condition is to always set
	 * period_steps to its maximum value. This neatly also satisfies the
	 * second condition too, since using the maximum value of period_steps
	 * to calculate prescale actually calculates its upper bound.
	 * Integer division will ensure a round down, so the period will thereby
	 * always be less than that requested.
	 *
	 * The downside of this approach is a significant degree of inaccuracy,
	 * especially as tmp approaches integer multiples of
	 * MCHPCOREPWM_PERIOD_STEPS_MAX.
	 *
	 * As we must produce a period less than that requested, and for the
	 * sake of creating a simple algorithm, disallow small values of tmp
	 * that would need special handling.
	 */
	if (tmp < MCHPCOREPWM_PERIOD_STEPS_MAX + 1)
		return -EINVAL;

	/*
	 * This "optimal" value for prescale is be calculated using the maximum
	 * permitted value of period_steps, 0xfe.
	 *
	 *                period * clk_rate
	 * prescale = ------------------------- - 1
	 *            NSEC_PER_SEC * (0xfe + 1)
	 *
	 *
	 *  period * clk_rate
	 * ------------------- was precomputed as `tmp`
	 *    NSEC_PER_SEC
	 */
	*prescale = ((u16)tmp) / (MCHPCOREPWM_PERIOD_STEPS_MAX + 1) - 1;

	/*
	 * period_steps can be computed from prescale:
	 *                      period * clk_rate
	 * period_steps = ----------------------------- - 1
	 *                NSEC_PER_SEC * (prescale + 1)
	 *
	 * However, in this approximation, we simply use the maximum value that
	 * was used to compute prescale.
	 */
	*period_steps = MCHPCOREPWM_PERIOD_STEPS_MAX;

	return 0;
}

static int mchp_core_pwm_apply_locked(struct pwm_chip *chip, struct pwm_device *pwm,
				      const struct pwm_state *state)
{
	struct mchp_core_pwm_chip *mchp_core_pwm = to_mchp_core_pwm(chip);
	bool period_locked;
	unsigned long clk_rate;
	u64 duty_steps;
	u16 prescale, period_steps;
	int ret;

	if (!state->enabled) {
		mchp_core_pwm_enable(chip, pwm, false, pwm->state.period);
		return 0;
	}

	/*
	 * If clk_rate is too big, the following multiplication might overflow.
	 * However this is implausible, as the fabric of current FPGAs cannot
	 * provide clocks at a rate high enough.
	 */
	clk_rate = clk_get_rate(mchp_core_pwm->clk);
	if (clk_rate >= NSEC_PER_SEC)
		return -EINVAL;

	ret = mchp_core_pwm_calc_period(state, clk_rate, &prescale, &period_steps);
	if (ret)
		return ret;

	/*
	 * If the only thing that has changed is the duty cycle or the polarity,
	 * we can shortcut the calculations and just compute/apply the new duty
	 * cycle pos & neg edges
	 * As all the channels share the same period, do not allow it to be
	 * changed if any other channels are enabled.
	 * If the period is locked, it may not be possible to use a period
	 * less than that requested. In that case, we just abort.
	 */
	period_locked = mchp_core_pwm->channel_enabled & ~(1 << pwm->hwpwm);

	if (period_locked) {
		u16 hw_prescale;
		u16 hw_period_steps;

		hw_prescale = readb_relaxed(mchp_core_pwm->base + MCHPCOREPWM_PRESCALE);
		hw_period_steps = readb_relaxed(mchp_core_pwm->base + MCHPCOREPWM_PERIOD);

		if ((period_steps + 1) * (prescale + 1) <
		    (hw_period_steps + 1) * (hw_prescale + 1))
			return -EINVAL;

		/*
		 * It is possible that something could have set the period_steps
		 * register to 0xff, which would prevent us from setting a 100%
		 * or 0% relative duty cycle, as explained above in
		 * mchp_core_pwm_calc_period().
		 * The period is locked and we cannot change this, so we abort.
		 */
		if (hw_period_steps == MCHPCOREPWM_PERIOD_STEPS_MAX)
			return -EINVAL;

		prescale = hw_prescale;
		period_steps = hw_period_steps;
	}

	duty_steps = mchp_core_pwm_calc_duty(state, clk_rate, prescale, period_steps);

	/*
	 * Because the period is not per channel, it is possible that the
	 * requested duty cycle is longer than the period, in which case cap it
	 * to the period, IOW a 100% duty cycle.
	 */
	if (duty_steps > period_steps)
		duty_steps = period_steps + 1;

	if (!period_locked) {
		writel_relaxed(prescale, mchp_core_pwm->base + MCHPCOREPWM_PRESCALE);
		writel_relaxed(period_steps, mchp_core_pwm->base + MCHPCOREPWM_PERIOD);
	}

	mchp_core_pwm_apply_duty(chip, pwm, state, duty_steps, period_steps);

	mchp_core_pwm_enable(chip, pwm, true, pwm->state.period);

	return 0;
}

static int mchp_core_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			       const struct pwm_state *state)
{
	struct mchp_core_pwm_chip *mchp_core_pwm = to_mchp_core_pwm(chip);
	int ret;

	mutex_lock(&mchp_core_pwm->lock);

	mchp_core_pwm_wait_for_sync_update(mchp_core_pwm, pwm->hwpwm);

	ret = mchp_core_pwm_apply_locked(chip, pwm, state);

	mutex_unlock(&mchp_core_pwm->lock);

	return ret;
}

static int mchp_core_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				   struct pwm_state *state)
{
	struct mchp_core_pwm_chip *mchp_core_pwm = to_mchp_core_pwm(chip);
	u64 rate;
	u16 prescale, period_steps;
	u8 duty_steps, posedge, negedge;

	mutex_lock(&mchp_core_pwm->lock);

	mchp_core_pwm_wait_for_sync_update(mchp_core_pwm, pwm->hwpwm);

	if (mchp_core_pwm->channel_enabled & (1 << pwm->hwpwm))
		state->enabled = true;
	else
		state->enabled = false;

	rate = clk_get_rate(mchp_core_pwm->clk);

	/*
	 * Calculating the period:
	 * The registers are each 8 bits wide & multiplied to compute the period
	 * using the formula:
	 *           (prescale + 1) * (period_steps + 1)
	 * period = -------------------------------------
	 *                      clk_rate
	 *
	 * Note:
	 * The prescale and period_steps registers operate similarly to
	 * CLK_DIVIDER_ONE_BASED, where the value used by the hardware is that
	 * in the register plus one.
	 */
	prescale = readb_relaxed(mchp_core_pwm->base + MCHPCOREPWM_PRESCALE);
	period_steps = readb_relaxed(mchp_core_pwm->base + MCHPCOREPWM_PERIOD);

	state->period = (period_steps + 1) * (prescale + 1);
	state->period *= NSEC_PER_SEC;
	state->period = DIV64_U64_ROUND_UP(state->period, rate);

	posedge = readb_relaxed(mchp_core_pwm->base + MCHPCOREPWM_POSEDGE(pwm->hwpwm));
	negedge = readb_relaxed(mchp_core_pwm->base + MCHPCOREPWM_NEGEDGE(pwm->hwpwm));

	mutex_unlock(&mchp_core_pwm->lock);

	if (negedge == posedge) {
		state->duty_cycle = state->period;
		state->period *= 2;
	} else {
		duty_steps = abs((s16)posedge - (s16)negedge);
		state->duty_cycle = duty_steps * (prescale + 1) * NSEC_PER_SEC;
		state->duty_cycle = DIV64_U64_ROUND_UP(state->duty_cycle, rate);
	}

	state->polarity = negedge < posedge ? PWM_POLARITY_INVERSED : PWM_POLARITY_NORMAL;

	return 0;
}

static const struct pwm_ops mchp_core_pwm_ops = {
	.apply = mchp_core_pwm_apply,
	.get_state = mchp_core_pwm_get_state,
	.owner = THIS_MODULE,
};

static const struct of_device_id mchp_core_of_match[] = {
	{
		.compatible = "microchip,corepwm-rtl-v4",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mchp_core_of_match);

static int mchp_core_pwm_probe(struct platform_device *pdev)
{
	struct mchp_core_pwm_chip *mchp_core_pwm;
	struct resource *regs;
	int ret;

	mchp_core_pwm = devm_kzalloc(&pdev->dev, sizeof(*mchp_core_pwm), GFP_KERNEL);
	if (!mchp_core_pwm)
		return -ENOMEM;

	mchp_core_pwm->base = devm_platform_get_and_ioremap_resource(pdev, 0, &regs);
	if (IS_ERR(mchp_core_pwm->base))
		return PTR_ERR(mchp_core_pwm->base);

	mchp_core_pwm->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(mchp_core_pwm->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(mchp_core_pwm->clk),
				     "failed to get PWM clock\n");

	if (of_property_read_u32(pdev->dev.of_node, "microchip,sync-update-mask",
				 &mchp_core_pwm->sync_update_mask))
		mchp_core_pwm->sync_update_mask = 0;

	mutex_init(&mchp_core_pwm->lock);

	mchp_core_pwm->chip.dev = &pdev->dev;
	mchp_core_pwm->chip.ops = &mchp_core_pwm_ops;
	mchp_core_pwm->chip.npwm = 16;

	mchp_core_pwm->channel_enabled = readb_relaxed(mchp_core_pwm->base + MCHPCOREPWM_EN(0));
	mchp_core_pwm->channel_enabled |=
		readb_relaxed(mchp_core_pwm->base + MCHPCOREPWM_EN(1)) << 8;

	/*
	 * Enable synchronous update mode for all channels for which shadow
	 * registers have been synthesised.
	 */
	writel_relaxed(1U, mchp_core_pwm->base + MCHPCOREPWM_SYNC_UPD);
	mchp_core_pwm->update_timestamp = ktime_get();

	ret = devm_pwmchip_add(&pdev->dev, &mchp_core_pwm->chip);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to add pwmchip\n");

	return 0;
}

static struct platform_driver mchp_core_pwm_driver = {
	.driver = {
		.name = "mchp-core-pwm",
		.of_match_table = mchp_core_of_match,
	},
	.probe = mchp_core_pwm_probe,
};
module_platform_driver(mchp_core_pwm_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_DESCRIPTION("corePWM driver for Microchip FPGAs");
