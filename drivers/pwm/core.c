// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic pwmlib implementation
 *
 * Copyright (C) 2011 Sascha Hauer <s.hauer@pengutronix.de>
 * Copyright (C) 2011-2012 Avionic Design GmbH
 */

#define DEFAULT_SYMBOL_NAMESPACE PWM

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/of.h>
#include <linux/pwm.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <dt-bindings/pwm/pwm.h>

#define CREATE_TRACE_POINTS
#include <trace/events/pwm.h>

/* protects access to pwm_chips */
static DEFINE_MUTEX(pwm_lock);

static DEFINE_IDR(pwm_chips);

static void pwmchip_lock(struct pwm_chip *chip)
{
	if (chip->atomic)
		spin_lock(&chip->atomic_lock);
	else
		mutex_lock(&chip->nonatomic_lock);
}

static void pwmchip_unlock(struct pwm_chip *chip)
{
	if (chip->atomic)
		spin_unlock(&chip->atomic_lock);
	else
		mutex_unlock(&chip->nonatomic_lock);
}

DEFINE_GUARD(pwmchip, struct pwm_chip *, pwmchip_lock(_T), pwmchip_unlock(_T))

static bool pwm_wf_valid(const struct pwm_waveform *wf)
{
	/*
	 * For now restrict waveforms to period_length_ns <= S64_MAX to provide
	 * some space for future extensions. One possibility is to simplify
	 * representing waveforms with inverted polarity using negative values
	 * somehow.
	 */
	if (wf->period_length_ns > S64_MAX)
		return false;

	if (wf->duty_length_ns > wf->period_length_ns)
		return false;

	/*
	 * .duty_offset_ns is supposed to be smaller than .period_length_ns, apart
	 * from the corner case .duty_offset_ns == 0 && .period_length_ns == 0.
	 */
	if (wf->duty_offset_ns && wf->duty_offset_ns >= wf->period_length_ns)
		return false;

	return true;
}

static void pwm_wf2state(const struct pwm_waveform *wf, struct pwm_state *state)
{
	if (wf->period_length_ns) {
		if (wf->duty_length_ns + wf->duty_offset_ns < wf->period_length_ns)
			*state = (struct pwm_state){
				.enabled = true,
				.polarity = PWM_POLARITY_NORMAL,
				.period = wf->period_length_ns,
				.duty_cycle = wf->duty_length_ns,
			};
		else
			*state = (struct pwm_state){
				.enabled = true,
				.polarity = PWM_POLARITY_INVERSED,
				.period = wf->period_length_ns,
				.duty_cycle = wf->period_length_ns - wf->duty_length_ns,
			};
	} else {
		*state = (struct pwm_state){
			.enabled = false,
		};
	}
}

static void pwm_state2wf(const struct pwm_state *state, struct pwm_waveform *wf)
{
	if (state->enabled) {
		if (state->polarity == PWM_POLARITY_NORMAL)
			*wf = (struct pwm_waveform){
				.period_length_ns = state->period,
				.duty_length_ns = state->duty_cycle,
				.duty_offset_ns = 0,
			};
		else
			*wf = (struct pwm_waveform){
				.period_length_ns = state->period,
				.duty_length_ns = state->period - state->duty_cycle,
				.duty_offset_ns = state->duty_cycle,
			};
	} else {
		*wf = (struct pwm_waveform){
			.period_length_ns = 0,
		};
	}
}

static int pwmwfcmp(const struct pwm_waveform *a, const struct pwm_waveform *b)
{
	if (a->period_length_ns > b->period_length_ns)
		return 1;

	if (a->period_length_ns < b->period_length_ns)
		return -1;

	if (a->duty_length_ns > b->duty_length_ns)
		return 1;

	if (a->duty_length_ns < b->duty_length_ns)
		return -1;

	if (a->duty_offset_ns > b->duty_offset_ns)
		return 1;

	if (a->duty_offset_ns < b->duty_offset_ns)
		return -1;

	return 0;
}

static bool pwm_check_rounding(const struct pwm_waveform *wf,
			       const struct pwm_waveform *wf_rounded)
{
	if (!wf->period_length_ns)
		return true;

	if (wf->period_length_ns < wf_rounded->period_length_ns)
		return false;

	if (wf->duty_length_ns < wf_rounded->duty_length_ns)
		return false;

	if (wf->duty_offset_ns < wf_rounded->duty_offset_ns)
		return false;

	return true;
}

static int __pwm_round_waveform_tohw(struct pwm_chip *chip, struct pwm_device *pwm,
				     const struct pwm_waveform *wf, void *wfhw)
{
	const struct pwm_ops *ops = chip->ops;
	int ret;

	ret = ops->round_waveform_tohw(chip, pwm, wf, wfhw);
	trace_pwm_round_waveform_tohw(pwm, wf, wfhw, ret);

	return ret;
}

static int __pwm_round_waveform_fromhw(struct pwm_chip *chip, struct pwm_device *pwm,
				       const void *wfhw, struct pwm_waveform *wf)
{
	const struct pwm_ops *ops = chip->ops;
	int ret;

	ret = ops->round_waveform_fromhw(chip, pwm, wfhw, wf);
	trace_pwm_round_waveform_fromhw(pwm, wfhw, wf, ret);

	return ret;
}

static int __pwm_read_waveform(struct pwm_chip *chip, struct pwm_device *pwm, void *wfhw)
{
	const struct pwm_ops *ops = chip->ops;
	int ret;

	ret = ops->read_waveform(chip, pwm, wfhw);
	trace_pwm_read_waveform(pwm, wfhw, ret);

	return ret;
}

static int __pwm_write_waveform(struct pwm_chip *chip, struct pwm_device *pwm, const void *wfhw)
{
	const struct pwm_ops *ops = chip->ops;
	int ret;

	ret = ops->write_waveform(chip, pwm, wfhw);
	trace_pwm_write_waveform(pwm, wfhw, ret);

	return ret;
}

#define WFHWSIZE 20

/**
 * pwm_round_waveform_might_sleep - Query hardware capabilities
 * Cannot be used in atomic context.
 * @pwm: PWM device
 * @wf: waveform to round and output parameter
 *
 * Typically a given waveform cannot be implemented exactly by hardware, e.g.
 * because hardware only supports coarse period resolution or no duty_offset.
 * This function returns the actually implemented waveform if you pass wf to
 * pwm_set_waveform_might_sleep now.
 *
 * Note however that the world doesn't stop turning when you call it, so when
 * doing
 *
 * 	pwm_round_waveform_might_sleep(mypwm, &wf);
 * 	pwm_set_waveform_might_sleep(mypwm, &wf, true);
 *
 * the latter might fail, e.g. because an input clock changed its rate between
 * these two calls and the waveform determined by
 * pwm_round_waveform_might_sleep() cannot be implemented any more.
 *
 * Returns 0 on success, 1 if there is no valid hardware configuration matching
 * the input waveform under the PWM rounding rules or a negative errno.
 */
int pwm_round_waveform_might_sleep(struct pwm_device *pwm, struct pwm_waveform *wf)
{
	struct pwm_chip *chip = pwm->chip;
	const struct pwm_ops *ops = chip->ops;
	struct pwm_waveform wf_req = *wf;
	char wfhw[WFHWSIZE];
	int ret_tohw, ret_fromhw;

	BUG_ON(WFHWSIZE < ops->sizeof_wfhw);

	if (!pwm_wf_valid(wf))
		return -EINVAL;

	guard(pwmchip)(chip);

	if (!chip->operational)
		return -ENODEV;

	ret_tohw = __pwm_round_waveform_tohw(chip, pwm, wf, wfhw);
	if (ret_tohw < 0)
		return ret_tohw;

	if (IS_ENABLED(CONFIG_PWM_DEBUG) && ret_tohw > 1)
		dev_err(&chip->dev, "Unexpected return value from __pwm_round_waveform_tohw: requested %llu/%llu [+%llu], return value %d\n",
			wf_req.duty_length_ns, wf_req.period_length_ns, wf_req.duty_offset_ns, ret_tohw);

	ret_fromhw = __pwm_round_waveform_fromhw(chip, pwm, wfhw, wf);
	if (ret_fromhw < 0)
		return ret_fromhw;

	if (IS_ENABLED(CONFIG_PWM_DEBUG) && ret_fromhw > 0)
		dev_err(&chip->dev, "Unexpected return value from __pwm_round_waveform_fromhw: requested %llu/%llu [+%llu], return value %d\n",
			wf_req.duty_length_ns, wf_req.period_length_ns, wf_req.duty_offset_ns, ret_tohw);

	if (IS_ENABLED(CONFIG_PWM_DEBUG) &&
	    ret_tohw == 0 && !pwm_check_rounding(&wf_req, wf))
		dev_err(&chip->dev, "Wrong rounding: requested %llu/%llu [+%llu], result %llu/%llu [+%llu]\n",
			wf_req.duty_length_ns, wf_req.period_length_ns, wf_req.duty_offset_ns,
			wf->duty_length_ns, wf->period_length_ns, wf->duty_offset_ns);

	return ret_tohw;
}
EXPORT_SYMBOL_GPL(pwm_round_waveform_might_sleep);

/**
 * pwm_get_waveform_might_sleep - Query hardware about current configuration
 * Cannot be used in atomic context.
 * @pwm: PWM device
 * @wf: output parameter
 *
 * Stores the current configuration of the PWM in @wf. Note this is the
 * equivalent of pwm_get_state_hw() (and not pwm_get_state()) for pwm_waveform.
 */
int pwm_get_waveform_might_sleep(struct pwm_device *pwm, struct pwm_waveform *wf)
{
	struct pwm_chip *chip = pwm->chip;
	const struct pwm_ops *ops = chip->ops;
	char wfhw[WFHWSIZE];
	int err;

	BUG_ON(WFHWSIZE < ops->sizeof_wfhw);

	guard(pwmchip)(chip);

	if (!chip->operational)
		return -ENODEV;

	err = __pwm_read_waveform(chip, pwm, &wfhw);
	if (err)
		return err;

	return __pwm_round_waveform_fromhw(chip, pwm, &wfhw, wf);
}
EXPORT_SYMBOL_GPL(pwm_get_waveform_might_sleep);

/* Called with the pwmchip lock held */
static int __pwm_set_waveform(struct pwm_device *pwm,
			      const struct pwm_waveform *wf,
			      bool exact)
{
	struct pwm_chip *chip = pwm->chip;
	const struct pwm_ops *ops = chip->ops;
	char wfhw[WFHWSIZE];
	struct pwm_waveform wf_rounded;
	int err;

	BUG_ON(WFHWSIZE < ops->sizeof_wfhw);

	if (!pwm_wf_valid(wf))
		return -EINVAL;

	err = __pwm_round_waveform_tohw(chip, pwm, wf, &wfhw);
	if (err)
		return err;

	if ((IS_ENABLED(CONFIG_PWM_DEBUG) || exact) && wf->period_length_ns) {
		err = __pwm_round_waveform_fromhw(chip, pwm, &wfhw, &wf_rounded);
		if (err)
			return err;

		if (IS_ENABLED(CONFIG_PWM_DEBUG) && !pwm_check_rounding(wf, &wf_rounded))
			dev_err(&chip->dev, "Wrong rounding: requested %llu/%llu [+%llu], result %llu/%llu [+%llu]\n",
				wf->duty_length_ns, wf->period_length_ns, wf->duty_offset_ns,
				wf_rounded.duty_length_ns, wf_rounded.period_length_ns, wf_rounded.duty_offset_ns);

		if (exact && pwmwfcmp(wf, &wf_rounded)) {
			dev_dbg(&chip->dev, "Requested no rounding, but %llu/%llu [+%llu] -> %llu/%llu [+%llu]\n",
				wf->duty_length_ns, wf->period_length_ns, wf->duty_offset_ns,
				wf_rounded.duty_length_ns, wf_rounded.period_length_ns, wf_rounded.duty_offset_ns);

			return 1;
		}
	}

	err = __pwm_write_waveform(chip, pwm, &wfhw);
	if (err)
		return err;

	/* update .state */
	pwm_wf2state(wf, &pwm->state);

	if (IS_ENABLED(CONFIG_PWM_DEBUG) && ops->read_waveform && wf->period_length_ns) {
		struct pwm_waveform wf_set;

		err = __pwm_read_waveform(chip, pwm, &wfhw);
		if (err)
			/* maybe ignore? */
			return err;

		err = __pwm_round_waveform_fromhw(chip, pwm, &wfhw, &wf_set);
		if (err)
			/* maybe ignore? */
			return err;

		if (pwmwfcmp(&wf_set, &wf_rounded) != 0)
			dev_err(&chip->dev,
				"Unexpected setting: requested %llu/%llu [+%llu], expected %llu/%llu [+%llu], set %llu/%llu [+%llu]\n",
				wf->duty_length_ns, wf->period_length_ns, wf->duty_offset_ns,
				wf_rounded.duty_length_ns, wf_rounded.period_length_ns, wf_rounded.duty_offset_ns,
				wf_set.duty_length_ns, wf_set.period_length_ns, wf_set.duty_offset_ns);
	}
	return 0;
}

/**
 * pwm_set_waveform_might_sleep - Apply a new waveform
 * Cannot be used in atomic context.
 * @pwm: PWM device
 * @wf: The waveform to apply
 * @exact: If true no rounding is allowed
 *
 * Typically a requested waveform cannot be implemented exactly, e.g. because
 * you requested .period_length_ns = 100 ns, but the hardware can only set
 * periods that are a multiple of 8.5 ns. With that hardware passing exact =
 * true results in pwm_set_waveform_might_sleep() failing and returning 1. If
 * exact = false you get a period of 93.5 ns (i.e. the biggest period not bigger
 * than the requested value).
 * Note that even with exact = true, some rounding by less than 1 is
 * possible/needed. In the above example requesting .period_length_ns = 94 and
 * exact = true, you get the hardware configured with period = 93.5 ns.
 */
int pwm_set_waveform_might_sleep(struct pwm_device *pwm,
				 const struct pwm_waveform *wf, bool exact)
{
	struct pwm_chip *chip = pwm->chip;
	int err;

	might_sleep();

	guard(pwmchip)(chip);

	if (!chip->operational)
		return -ENODEV;

	if (IS_ENABLED(CONFIG_PWM_DEBUG) && chip->atomic) {
		/*
		 * Catch any drivers that have been marked as atomic but
		 * that will sleep anyway.
		 */
		non_block_start();
		err = __pwm_set_waveform(pwm, wf, exact);
		non_block_end();
	} else {
		err = __pwm_set_waveform(pwm, wf, exact);
	}

	return err;
}
EXPORT_SYMBOL_GPL(pwm_set_waveform_might_sleep);

static void pwm_apply_debug(struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct pwm_state *last = &pwm->last;
	struct pwm_chip *chip = pwm->chip;
	struct pwm_state s1 = { 0 }, s2 = { 0 };
	int err;

	if (!IS_ENABLED(CONFIG_PWM_DEBUG))
		return;

	/* No reasonable diagnosis possible without .get_state() */
	if (!chip->ops->get_state)
		return;

	/*
	 * *state was just applied. Read out the hardware state and do some
	 * checks.
	 */

	err = chip->ops->get_state(chip, pwm, &s1);
	trace_pwm_get(pwm, &s1, err);
	if (err)
		/* If that failed there isn't much to debug */
		return;

	/*
	 * The lowlevel driver either ignored .polarity (which is a bug) or as
	 * best effort inverted .polarity and fixed .duty_cycle respectively.
	 * Undo this inversion and fixup for further tests.
	 */
	if (s1.enabled && s1.polarity != state->polarity) {
		s2.polarity = state->polarity;
		s2.duty_cycle = s1.period - s1.duty_cycle;
		s2.period = s1.period;
		s2.enabled = s1.enabled;
	} else {
		s2 = s1;
	}

	if (s2.polarity != state->polarity &&
	    state->duty_cycle < state->period)
		dev_warn(pwmchip_parent(chip), ".apply ignored .polarity\n");

	if (state->enabled && s2.enabled &&
	    last->polarity == state->polarity &&
	    last->period > s2.period &&
	    last->period <= state->period)
		dev_warn(pwmchip_parent(chip),
			 ".apply didn't pick the best available period (requested: %llu, applied: %llu, possible: %llu)\n",
			 state->period, s2.period, last->period);

	/*
	 * Rounding period up is fine only if duty_cycle is 0 then, because a
	 * flat line doesn't have a characteristic period.
	 */
	if (state->enabled && s2.enabled && state->period < s2.period && s2.duty_cycle)
		dev_warn(pwmchip_parent(chip),
			 ".apply is supposed to round down period (requested: %llu, applied: %llu)\n",
			 state->period, s2.period);

	if (state->enabled &&
	    last->polarity == state->polarity &&
	    last->period == s2.period &&
	    last->duty_cycle > s2.duty_cycle &&
	    last->duty_cycle <= state->duty_cycle)
		dev_warn(pwmchip_parent(chip),
			 ".apply didn't pick the best available duty cycle (requested: %llu/%llu, applied: %llu/%llu, possible: %llu/%llu)\n",
			 state->duty_cycle, state->period,
			 s2.duty_cycle, s2.period,
			 last->duty_cycle, last->period);

	if (state->enabled && s2.enabled && state->duty_cycle < s2.duty_cycle)
		dev_warn(pwmchip_parent(chip),
			 ".apply is supposed to round down duty_cycle (requested: %llu/%llu, applied: %llu/%llu)\n",
			 state->duty_cycle, state->period,
			 s2.duty_cycle, s2.period);

	if (!state->enabled && s2.enabled && s2.duty_cycle > 0)
		dev_warn(pwmchip_parent(chip),
			 "requested disabled, but yielded enabled with duty > 0\n");

	/* reapply the state that the driver reported being configured. */
	err = chip->ops->apply(chip, pwm, &s1);
	trace_pwm_apply(pwm, &s1, err);
	if (err) {
		*last = s1;
		dev_err(pwmchip_parent(chip), "failed to reapply current setting\n");
		return;
	}

	*last = (struct pwm_state){ 0 };
	err = chip->ops->get_state(chip, pwm, last);
	trace_pwm_get(pwm, last, err);
	if (err)
		return;

	/* reapplication of the current state should give an exact match */
	if (s1.enabled != last->enabled ||
	    s1.polarity != last->polarity ||
	    (s1.enabled && s1.period != last->period) ||
	    (s1.enabled && s1.duty_cycle != last->duty_cycle)) {
		dev_err(pwmchip_parent(chip),
			".apply is not idempotent (ena=%d pol=%d %llu/%llu) -> (ena=%d pol=%d %llu/%llu)\n",
			s1.enabled, s1.polarity, s1.duty_cycle, s1.period,
			last->enabled, last->polarity, last->duty_cycle,
			last->period);
	}
}

static bool pwm_state_valid(const struct pwm_state *state)
{
	/*
	 * For a disabled state all other state description is irrelevant and
	 * and supposed to be ignored. So also ignore any strange values and
	 * consider the state ok.
	 */
	if (state->enabled)
		return true;

	if (!state->period)
		return false;

	if (state->duty_cycle > state->period)
		return false;

	return true;
}

/**
 * __pwm_apply() - atomically apply a new state to a PWM device
 * @pwm: PWM device
 * @state: new state to apply
 */
static int __pwm_apply(struct pwm_device *pwm, const struct pwm_state *state)
{
	struct pwm_chip *chip;
	const struct pwm_ops *ops;
	int err;

	if (!pwm || !state)
		return -EINVAL;

	if (!pwm_state_valid(state)) {
		/*
		 * Allow to transition from one invalid state to another.
		 * This ensures that you can e.g. change the polarity while
		 * the period is zero. (This happens on stm32 when the hardware
		 * is in its poweron default state.) This greatly simplifies
		 * working with the sysfs API where you can only change one
		 * parameter at a time.
		 */
		if (!pwm_state_valid(&pwm->state)) {
			pwm->state = *state;
			return 0;
		}

		return -EINVAL;
	}

	chip = pwm->chip;
	ops = chip->ops;

	if (state->period == pwm->state.period &&
	    state->duty_cycle == pwm->state.duty_cycle &&
	    state->polarity == pwm->state.polarity &&
	    state->enabled == pwm->state.enabled &&
	    state->usage_power == pwm->state.usage_power)
		return 0;

	if (ops->write_waveform) {
		struct pwm_waveform wf;
		char wfhw[WFHWSIZE];

		BUG_ON(WFHWSIZE < ops->sizeof_wfhw);

		pwm_state2wf(state, &wf);

		/*
		 * The rounding is wrong here for states with inverted polarity.
		 * While .apply() rounds down duty_cycle (which represents the
		 * time from the start of the period to the inner edge),
		 * .round_waveform_tohw() rounds down the time the PWM is high.
		 * Can be fixed if the need arises, until reported otherwise
		 * let's assume that consumers don't care.
		 */

		err = __pwm_round_waveform_tohw(chip, pwm, &wf, &wfhw);
		if (err) {
			if (err > 0)
				/*
				 * This signals an invalid request, typically
				 * the requested period (or duty_offset) is
				 * smaller than possible with the hardware.
				 */
				return -EINVAL;

			return err;
		}

		if (IS_ENABLED(CONFIG_PWM_DEBUG)) {
			struct pwm_waveform wf_rounded;

			err = __pwm_round_waveform_fromhw(chip, pwm, &wfhw, &wf_rounded);
			if (err)
				return err;

			if (!pwm_check_rounding(&wf, &wf_rounded))
				dev_err(&chip->dev, "Wrong rounding: requested %llu/%llu [+%llu], result %llu/%llu [+%llu]\n",
					wf.duty_length_ns, wf.period_length_ns, wf.duty_offset_ns,
					wf_rounded.duty_length_ns, wf_rounded.period_length_ns, wf_rounded.duty_offset_ns);
		}

		err = __pwm_write_waveform(chip, pwm, &wfhw);
		if (err)
			return err;

		pwm->state = *state;

	} else {
		err = ops->apply(chip, pwm, state);
		trace_pwm_apply(pwm, state, err);
		if (err)
			return err;

		pwm->state = *state;

		/*
		 * only do this after pwm->state was applied as some
		 * implementations of .get_state() depend on this
		 */
		pwm_apply_debug(pwm, state);
	}

	return 0;
}

/**
 * pwm_apply_might_sleep() - atomically apply a new state to a PWM device
 * Cannot be used in atomic context.
 * @pwm: PWM device
 * @state: new state to apply
 */
int pwm_apply_might_sleep(struct pwm_device *pwm, const struct pwm_state *state)
{
	int err;
	struct pwm_chip *chip = pwm->chip;

	/*
	 * Some lowlevel driver's implementations of .apply() make use of
	 * mutexes, also with some drivers only returning when the new
	 * configuration is active calling pwm_apply_might_sleep() from atomic context
	 * is a bad idea. So make it explicit that calling this function might
	 * sleep.
	 */
	might_sleep();

	guard(pwmchip)(chip);

	if (!chip->operational)
		return -ENODEV;

	if (IS_ENABLED(CONFIG_PWM_DEBUG) && chip->atomic) {
		/*
		 * Catch any drivers that have been marked as atomic but
		 * that will sleep anyway.
		 */
		non_block_start();
		err = __pwm_apply(pwm, state);
		non_block_end();
	} else {
		err = __pwm_apply(pwm, state);
	}

	return err;
}
EXPORT_SYMBOL_GPL(pwm_apply_might_sleep);

/**
 * pwm_apply_atomic() - apply a new state to a PWM device from atomic context
 * Not all PWM devices support this function, check with pwm_might_sleep().
 * @pwm: PWM device
 * @state: new state to apply
 */
int pwm_apply_atomic(struct pwm_device *pwm, const struct pwm_state *state)
{
	struct pwm_chip *chip = pwm->chip;

	WARN_ONCE(!chip->atomic,
		  "sleeping PWM driver used in atomic context\n");

	guard(pwmchip)(chip);

	if (!chip->operational)
		return -ENODEV;

	return __pwm_apply(pwm, state);
}
EXPORT_SYMBOL_GPL(pwm_apply_atomic);

/**
 * pwm_get_state_hw() - get the current PWM state from hardware
 * @pwm: PWM device
 * @state: state to fill with the current PWM state
 *
 * Similar to pwm_get_state() but reads the current PWM state from hardware
 * instead of the requested state.
 *
 * Returns: 0 on success or a negative error code on failure.
 * Context: May sleep.
 */
int pwm_get_state_hw(struct pwm_device *pwm, struct pwm_state *state)
{
	struct pwm_chip *chip = pwm->chip;
	const struct pwm_ops *ops = chip->ops;
	int ret = -EOPNOTSUPP;

	might_sleep();

	guard(pwmchip)(chip);

	if (!chip->operational)
		return -ENODEV;

	if (ops->read_waveform) {
		char wfhw[WFHWSIZE];
		struct pwm_waveform wf;

		BUG_ON(WFHWSIZE < ops->sizeof_wfhw);

		ret = __pwm_read_waveform(chip, pwm, &wfhw);
		if (ret)
			return ret;

		ret = __pwm_round_waveform_fromhw(chip, pwm, &wfhw, &wf);
		if (ret)
			return ret;

		pwm_wf2state(&wf, state);

	} else if (ops->get_state) {
		ret = ops->get_state(chip, pwm, state);
		trace_pwm_get(pwm, state, ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(pwm_get_state_hw);

/**
 * pwm_adjust_config() - adjust the current PWM config to the PWM arguments
 * @pwm: PWM device
 *
 * This function will adjust the PWM config to the PWM arguments provided
 * by the DT or PWM lookup table. This is particularly useful to adapt
 * the bootloader config to the Linux one.
 */
int pwm_adjust_config(struct pwm_device *pwm)
{
	struct pwm_state state;
	struct pwm_args pargs;

	pwm_get_args(pwm, &pargs);
	pwm_get_state(pwm, &state);

	/*
	 * If the current period is zero it means that either the PWM driver
	 * does not support initial state retrieval or the PWM has not yet
	 * been configured.
	 *
	 * In either case, we setup the new period and polarity, and assign a
	 * duty cycle of 0.
	 */
	if (!state.period) {
		state.duty_cycle = 0;
		state.period = pargs.period;
		state.polarity = pargs.polarity;

		return pwm_apply_might_sleep(pwm, &state);
	}

	/*
	 * Adjust the PWM duty cycle/period based on the period value provided
	 * in PWM args.
	 */
	if (pargs.period != state.period) {
		u64 dutycycle = (u64)state.duty_cycle * pargs.period;

		do_div(dutycycle, state.period);
		state.duty_cycle = dutycycle;
		state.period = pargs.period;
	}

	/*
	 * If the polarity changed, we should also change the duty cycle.
	 */
	if (pargs.polarity != state.polarity) {
		state.polarity = pargs.polarity;
		state.duty_cycle = state.period - state.duty_cycle;
	}

	return pwm_apply_might_sleep(pwm, &state);
}
EXPORT_SYMBOL_GPL(pwm_adjust_config);

/**
 * pwm_capture() - capture and report a PWM signal
 * @pwm: PWM device
 * @result: structure to fill with capture result
 * @timeout: time to wait, in milliseconds, before giving up on capture
 *
 * Returns: 0 on success or a negative error code on failure.
 */
static int pwm_capture(struct pwm_device *pwm, struct pwm_capture *result,
		       unsigned long timeout)
{
	struct pwm_chip *chip = pwm->chip;
	const struct pwm_ops *ops = chip->ops;

	if (!ops->capture)
		return -ENOSYS;

	/*
	 * Holding the pwm_lock is probably not needed. If you use pwm_capture()
	 * and you're interested to speed it up, please convince yourself it's
	 * really not needed, test and then suggest a patch on the mailing list.
	 */
	guard(mutex)(&pwm_lock);

	guard(pwmchip)(chip);

	if (!chip->operational)
		return -ENODEV;

	return ops->capture(chip, pwm, result, timeout);
}

static struct pwm_chip *pwmchip_find_by_name(const char *name)
{
	struct pwm_chip *chip;
	unsigned long id, tmp;

	if (!name)
		return NULL;

	guard(mutex)(&pwm_lock);

	idr_for_each_entry_ul(&pwm_chips, chip, tmp, id) {
		if (device_match_name(pwmchip_parent(chip), name))
			return chip;
	}

	return NULL;
}

static int pwm_device_request(struct pwm_device *pwm, const char *label)
{
	int err;
	struct pwm_chip *chip = pwm->chip;
	const struct pwm_ops *ops = chip->ops;

	if (test_bit(PWMF_REQUESTED, &pwm->flags))
		return -EBUSY;

	/*
	 * This function is called while holding pwm_lock. As .operational only
	 * changes while holding this lock, checking it here without holding the
	 * chip lock is fine.
	 */
	if (!chip->operational)
		return -ENODEV;

	if (!try_module_get(chip->owner))
		return -ENODEV;

	if (!get_device(&chip->dev)) {
		err = -ENODEV;
		goto err_get_device;
	}

	if (ops->request) {
		err = ops->request(chip, pwm);
		if (err) {
			put_device(&chip->dev);
err_get_device:
			module_put(chip->owner);
			return err;
		}
	}

	if (ops->read_waveform || ops->get_state) {
		/*
		 * Zero-initialize state because most drivers are unaware of
		 * .usage_power. The other members of state are supposed to be
		 * set by lowlevel drivers. We still initialize the whole
		 * structure for simplicity even though this might paper over
		 * faulty implementations of .get_state().
		 */
		struct pwm_state state = { 0, };

		err = pwm_get_state_hw(pwm, &state);
		if (!err)
			pwm->state = state;

		if (IS_ENABLED(CONFIG_PWM_DEBUG))
			pwm->last = pwm->state;
	}

	set_bit(PWMF_REQUESTED, &pwm->flags);
	pwm->label = label;

	return 0;
}

/**
 * pwm_request_from_chip() - request a PWM device relative to a PWM chip
 * @chip: PWM chip
 * @index: per-chip index of the PWM to request
 * @label: a literal description string of this PWM
 *
 * Returns: A pointer to the PWM device at the given index of the given PWM
 * chip. A negative error code is returned if the index is not valid for the
 * specified PWM chip or if the PWM device cannot be requested.
 */
static struct pwm_device *pwm_request_from_chip(struct pwm_chip *chip,
						unsigned int index,
						const char *label)
{
	struct pwm_device *pwm;
	int err;

	if (!chip || index >= chip->npwm)
		return ERR_PTR(-EINVAL);

	guard(mutex)(&pwm_lock);

	pwm = &chip->pwms[index];

	err = pwm_device_request(pwm, label);
	if (err < 0)
		return ERR_PTR(err);

	return pwm;
}

struct pwm_device *
of_pwm_xlate_with_flags(struct pwm_chip *chip, const struct of_phandle_args *args)
{
	struct pwm_device *pwm;

	/* period in the second cell and flags in the third cell are optional */
	if (args->args_count < 1)
		return ERR_PTR(-EINVAL);

	pwm = pwm_request_from_chip(chip, args->args[0], NULL);
	if (IS_ERR(pwm))
		return pwm;

	if (args->args_count > 1)
		pwm->args.period = args->args[1];

	pwm->args.polarity = PWM_POLARITY_NORMAL;
	if (args->args_count > 2 && args->args[2] & PWM_POLARITY_INVERTED)
		pwm->args.polarity = PWM_POLARITY_INVERSED;

	return pwm;
}
EXPORT_SYMBOL_GPL(of_pwm_xlate_with_flags);

struct pwm_device *
of_pwm_single_xlate(struct pwm_chip *chip, const struct of_phandle_args *args)
{
	struct pwm_device *pwm;

	pwm = pwm_request_from_chip(chip, 0, NULL);
	if (IS_ERR(pwm))
		return pwm;

	if (args->args_count > 0)
		pwm->args.period = args->args[0];

	pwm->args.polarity = PWM_POLARITY_NORMAL;
	if (args->args_count > 1 && args->args[1] & PWM_POLARITY_INVERTED)
		pwm->args.polarity = PWM_POLARITY_INVERSED;

	return pwm;
}
EXPORT_SYMBOL_GPL(of_pwm_single_xlate);

struct pwm_export {
	struct device pwm_dev;
	struct pwm_device *pwm;
	struct mutex lock;
	struct pwm_state suspend;
};

static inline struct pwm_chip *pwmchip_from_dev(struct device *pwmchip_dev)
{
	return container_of(pwmchip_dev, struct pwm_chip, dev);
}

static inline struct pwm_export *pwmexport_from_dev(struct device *pwm_dev)
{
	return container_of(pwm_dev, struct pwm_export, pwm_dev);
}

static inline struct pwm_device *pwm_from_dev(struct device *pwm_dev)
{
	struct pwm_export *export = pwmexport_from_dev(pwm_dev);

	return export->pwm;
}

static ssize_t period_show(struct device *pwm_dev,
			   struct device_attribute *attr,
			   char *buf)
{
	const struct pwm_device *pwm = pwm_from_dev(pwm_dev);
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return sysfs_emit(buf, "%llu\n", state.period);
}

static ssize_t period_store(struct device *pwm_dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct pwm_export *export = pwmexport_from_dev(pwm_dev);
	struct pwm_device *pwm = export->pwm;
	struct pwm_state state;
	u64 val;
	int ret;

	ret = kstrtou64(buf, 0, &val);
	if (ret)
		return ret;

	guard(mutex)(&export->lock);

	pwm_get_state(pwm, &state);
	state.period = val;
	ret = pwm_apply_might_sleep(pwm, &state);

	return ret ? : size;
}

static ssize_t duty_cycle_show(struct device *pwm_dev,
			       struct device_attribute *attr,
			       char *buf)
{
	const struct pwm_device *pwm = pwm_from_dev(pwm_dev);
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return sysfs_emit(buf, "%llu\n", state.duty_cycle);
}

static ssize_t duty_cycle_store(struct device *pwm_dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct pwm_export *export = pwmexport_from_dev(pwm_dev);
	struct pwm_device *pwm = export->pwm;
	struct pwm_state state;
	u64 val;
	int ret;

	ret = kstrtou64(buf, 0, &val);
	if (ret)
		return ret;

	guard(mutex)(&export->lock);

	pwm_get_state(pwm, &state);
	state.duty_cycle = val;
	ret = pwm_apply_might_sleep(pwm, &state);

	return ret ? : size;
}

static ssize_t enable_show(struct device *pwm_dev,
			   struct device_attribute *attr,
			   char *buf)
{
	const struct pwm_device *pwm = pwm_from_dev(pwm_dev);
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return sysfs_emit(buf, "%d\n", state.enabled);
}

static ssize_t enable_store(struct device *pwm_dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct pwm_export *export = pwmexport_from_dev(pwm_dev);
	struct pwm_device *pwm = export->pwm;
	struct pwm_state state;
	int val, ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;

	guard(mutex)(&export->lock);

	pwm_get_state(pwm, &state);

	switch (val) {
	case 0:
		state.enabled = false;
		break;
	case 1:
		state.enabled = true;
		break;
	default:
		return -EINVAL;
	}

	ret = pwm_apply_might_sleep(pwm, &state);

	return ret ? : size;
}

static ssize_t polarity_show(struct device *pwm_dev,
			     struct device_attribute *attr,
			     char *buf)
{
	const struct pwm_device *pwm = pwm_from_dev(pwm_dev);
	const char *polarity = "unknown";
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	switch (state.polarity) {
	case PWM_POLARITY_NORMAL:
		polarity = "normal";
		break;

	case PWM_POLARITY_INVERSED:
		polarity = "inversed";
		break;
	}

	return sysfs_emit(buf, "%s\n", polarity);
}

static ssize_t polarity_store(struct device *pwm_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	struct pwm_export *export = pwmexport_from_dev(pwm_dev);
	struct pwm_device *pwm = export->pwm;
	enum pwm_polarity polarity;
	struct pwm_state state;
	int ret;

	if (sysfs_streq(buf, "normal"))
		polarity = PWM_POLARITY_NORMAL;
	else if (sysfs_streq(buf, "inversed"))
		polarity = PWM_POLARITY_INVERSED;
	else
		return -EINVAL;

	guard(mutex)(&export->lock);

	pwm_get_state(pwm, &state);
	state.polarity = polarity;
	ret = pwm_apply_might_sleep(pwm, &state);

	return ret ? : size;
}

static ssize_t capture_show(struct device *pwm_dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct pwm_device *pwm = pwm_from_dev(pwm_dev);
	struct pwm_capture result;
	int ret;

	ret = pwm_capture(pwm, &result, jiffies_to_msecs(HZ));
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u %u\n", result.period, result.duty_cycle);
}

static DEVICE_ATTR_RW(period);
static DEVICE_ATTR_RW(duty_cycle);
static DEVICE_ATTR_RW(enable);
static DEVICE_ATTR_RW(polarity);
static DEVICE_ATTR_RO(capture);

static struct attribute *pwm_attrs[] = {
	&dev_attr_period.attr,
	&dev_attr_duty_cycle.attr,
	&dev_attr_enable.attr,
	&dev_attr_polarity.attr,
	&dev_attr_capture.attr,
	NULL
};
ATTRIBUTE_GROUPS(pwm);

static void pwm_export_release(struct device *pwm_dev)
{
	struct pwm_export *export = pwmexport_from_dev(pwm_dev);

	kfree(export);
}

static int pwm_export_child(struct device *pwmchip_dev, struct pwm_device *pwm)
{
	struct pwm_export *export;
	char *pwm_prop[2];
	int ret;

	if (test_and_set_bit(PWMF_EXPORTED, &pwm->flags))
		return -EBUSY;

	export = kzalloc(sizeof(*export), GFP_KERNEL);
	if (!export) {
		clear_bit(PWMF_EXPORTED, &pwm->flags);
		return -ENOMEM;
	}

	export->pwm = pwm;
	mutex_init(&export->lock);

	export->pwm_dev.release = pwm_export_release;
	export->pwm_dev.parent = pwmchip_dev;
	export->pwm_dev.devt = MKDEV(0, 0);
	export->pwm_dev.groups = pwm_groups;
	dev_set_name(&export->pwm_dev, "pwm%u", pwm->hwpwm);

	ret = device_register(&export->pwm_dev);
	if (ret) {
		clear_bit(PWMF_EXPORTED, &pwm->flags);
		put_device(&export->pwm_dev);
		export = NULL;
		return ret;
	}
	pwm_prop[0] = kasprintf(GFP_KERNEL, "EXPORT=pwm%u", pwm->hwpwm);
	pwm_prop[1] = NULL;
	kobject_uevent_env(&pwmchip_dev->kobj, KOBJ_CHANGE, pwm_prop);
	kfree(pwm_prop[0]);

	return 0;
}

static int pwm_unexport_match(struct device *pwm_dev, void *data)
{
	return pwm_from_dev(pwm_dev) == data;
}

static int pwm_unexport_child(struct device *pwmchip_dev, struct pwm_device *pwm)
{
	struct device *pwm_dev;
	char *pwm_prop[2];

	if (!test_and_clear_bit(PWMF_EXPORTED, &pwm->flags))
		return -ENODEV;

	pwm_dev = device_find_child(pwmchip_dev, pwm, pwm_unexport_match);
	if (!pwm_dev)
		return -ENODEV;

	pwm_prop[0] = kasprintf(GFP_KERNEL, "UNEXPORT=pwm%u", pwm->hwpwm);
	pwm_prop[1] = NULL;
	kobject_uevent_env(&pwmchip_dev->kobj, KOBJ_CHANGE, pwm_prop);
	kfree(pwm_prop[0]);

	/* for device_find_child() */
	put_device(pwm_dev);
	device_unregister(pwm_dev);
	pwm_put(pwm);

	return 0;
}

static ssize_t export_store(struct device *pwmchip_dev,
			    struct device_attribute *attr,
			    const char *buf, size_t len)
{
	struct pwm_chip *chip = pwmchip_from_dev(pwmchip_dev);
	struct pwm_device *pwm;
	unsigned int hwpwm;
	int ret;

	ret = kstrtouint(buf, 0, &hwpwm);
	if (ret < 0)
		return ret;

	if (hwpwm >= chip->npwm)
		return -ENODEV;

	pwm = pwm_request_from_chip(chip, hwpwm, "sysfs");
	if (IS_ERR(pwm))
		return PTR_ERR(pwm);

	ret = pwm_export_child(pwmchip_dev, pwm);
	if (ret < 0)
		pwm_put(pwm);

	return ret ? : len;
}
static DEVICE_ATTR_WO(export);

static ssize_t unexport_store(struct device *pwmchip_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t len)
{
	struct pwm_chip *chip = pwmchip_from_dev(pwmchip_dev);
	unsigned int hwpwm;
	int ret;

	ret = kstrtouint(buf, 0, &hwpwm);
	if (ret < 0)
		return ret;

	if (hwpwm >= chip->npwm)
		return -ENODEV;

	ret = pwm_unexport_child(pwmchip_dev, &chip->pwms[hwpwm]);

	return ret ? : len;
}
static DEVICE_ATTR_WO(unexport);

static ssize_t npwm_show(struct device *pwmchip_dev, struct device_attribute *attr,
			 char *buf)
{
	const struct pwm_chip *chip = pwmchip_from_dev(pwmchip_dev);

	return sysfs_emit(buf, "%u\n", chip->npwm);
}
static DEVICE_ATTR_RO(npwm);

static struct attribute *pwm_chip_attrs[] = {
	&dev_attr_export.attr,
	&dev_attr_unexport.attr,
	&dev_attr_npwm.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pwm_chip);

/* takes export->lock on success */
static struct pwm_export *pwm_class_get_state(struct device *pwmchip_dev,
					      struct pwm_device *pwm,
					      struct pwm_state *state)
{
	struct device *pwm_dev;
	struct pwm_export *export;

	if (!test_bit(PWMF_EXPORTED, &pwm->flags))
		return NULL;

	pwm_dev = device_find_child(pwmchip_dev, pwm, pwm_unexport_match);
	if (!pwm_dev)
		return NULL;

	export = pwmexport_from_dev(pwm_dev);
	put_device(pwm_dev);	/* for device_find_child() */

	mutex_lock(&export->lock);
	pwm_get_state(pwm, state);

	return export;
}

static int pwm_class_apply_state(struct pwm_export *export,
				 struct pwm_device *pwm,
				 struct pwm_state *state)
{
	int ret = pwm_apply_might_sleep(pwm, state);

	/* release lock taken in pwm_class_get_state */
	mutex_unlock(&export->lock);

	return ret;
}

static int pwm_class_resume_npwm(struct device *pwmchip_dev, unsigned int npwm)
{
	struct pwm_chip *chip = pwmchip_from_dev(pwmchip_dev);
	unsigned int i;
	int ret = 0;

	for (i = 0; i < npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];
		struct pwm_state state;
		struct pwm_export *export;

		export = pwm_class_get_state(pwmchip_dev, pwm, &state);
		if (!export)
			continue;

		/* If pwmchip was not enabled before suspend, do nothing. */
		if (!export->suspend.enabled) {
			/* release lock taken in pwm_class_get_state */
			mutex_unlock(&export->lock);
			continue;
		}

		state.enabled = export->suspend.enabled;
		ret = pwm_class_apply_state(export, pwm, &state);
		if (ret < 0)
			break;
	}

	return ret;
}

static int pwm_class_suspend(struct device *pwmchip_dev)
{
	struct pwm_chip *chip = pwmchip_from_dev(pwmchip_dev);
	unsigned int i;
	int ret = 0;

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];
		struct pwm_state state;
		struct pwm_export *export;

		export = pwm_class_get_state(pwmchip_dev, pwm, &state);
		if (!export)
			continue;

		/*
		 * If pwmchip was not enabled before suspend, save
		 * state for resume time and do nothing else.
		 */
		export->suspend = state;
		if (!state.enabled) {
			/* release lock taken in pwm_class_get_state */
			mutex_unlock(&export->lock);
			continue;
		}

		state.enabled = false;
		ret = pwm_class_apply_state(export, pwm, &state);
		if (ret < 0) {
			/*
			 * roll back the PWM devices that were disabled by
			 * this suspend function.
			 */
			pwm_class_resume_npwm(pwmchip_dev, i);
			break;
		}
	}

	return ret;
}

static int pwm_class_resume(struct device *pwmchip_dev)
{
	struct pwm_chip *chip = pwmchip_from_dev(pwmchip_dev);

	return pwm_class_resume_npwm(pwmchip_dev, chip->npwm);
}

static DEFINE_SIMPLE_DEV_PM_OPS(pwm_class_pm_ops, pwm_class_suspend, pwm_class_resume);

static struct class pwm_class = {
	.name = "pwm",
	.dev_groups = pwm_chip_groups,
	.pm = pm_sleep_ptr(&pwm_class_pm_ops),
};

static void pwmchip_sysfs_unexport(struct pwm_chip *chip)
{
	unsigned int i;

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];

		if (test_bit(PWMF_EXPORTED, &pwm->flags))
			pwm_unexport_child(&chip->dev, pwm);
	}
}

#define PWMCHIP_ALIGN ARCH_DMA_MINALIGN

static void *pwmchip_priv(struct pwm_chip *chip)
{
	return (void *)chip + ALIGN(struct_size(chip, pwms, chip->npwm), PWMCHIP_ALIGN);
}

/* This is the counterpart to pwmchip_alloc() */
void pwmchip_put(struct pwm_chip *chip)
{
	put_device(&chip->dev);
}
EXPORT_SYMBOL_GPL(pwmchip_put);

static void pwmchip_release(struct device *pwmchip_dev)
{
	struct pwm_chip *chip = pwmchip_from_dev(pwmchip_dev);

	kfree(chip);
}

struct pwm_chip *pwmchip_alloc(struct device *parent, unsigned int npwm, size_t sizeof_priv)
{
	struct pwm_chip *chip;
	struct device *pwmchip_dev;
	size_t alloc_size;
	unsigned int i;

	alloc_size = size_add(ALIGN(struct_size(chip, pwms, npwm), PWMCHIP_ALIGN),
			      sizeof_priv);

	chip = kzalloc(alloc_size, GFP_KERNEL);
	if (!chip)
		return ERR_PTR(-ENOMEM);

	chip->npwm = npwm;
	chip->uses_pwmchip_alloc = true;
	chip->operational = false;

	pwmchip_dev = &chip->dev;
	device_initialize(pwmchip_dev);
	pwmchip_dev->class = &pwm_class;
	pwmchip_dev->parent = parent;
	pwmchip_dev->release = pwmchip_release;

	pwmchip_set_drvdata(chip, pwmchip_priv(chip));

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];
		pwm->chip = chip;
		pwm->hwpwm = i;
	}

	return chip;
}
EXPORT_SYMBOL_GPL(pwmchip_alloc);

static void devm_pwmchip_put(void *data)
{
	struct pwm_chip *chip = data;

	pwmchip_put(chip);
}

struct pwm_chip *devm_pwmchip_alloc(struct device *parent, unsigned int npwm, size_t sizeof_priv)
{
	struct pwm_chip *chip;
	int ret;

	chip = pwmchip_alloc(parent, npwm, sizeof_priv);
	if (IS_ERR(chip))
		return chip;

	ret = devm_add_action_or_reset(parent, devm_pwmchip_put, chip);
	if (ret)
		return ERR_PTR(ret);

	return chip;
}
EXPORT_SYMBOL_GPL(devm_pwmchip_alloc);

static void of_pwmchip_add(struct pwm_chip *chip)
{
	if (!pwmchip_parent(chip) || !pwmchip_parent(chip)->of_node)
		return;

	if (!chip->of_xlate)
		chip->of_xlate = of_pwm_xlate_with_flags;

	of_node_get(pwmchip_parent(chip)->of_node);
}

static void of_pwmchip_remove(struct pwm_chip *chip)
{
	if (pwmchip_parent(chip))
		of_node_put(pwmchip_parent(chip)->of_node);
}

static bool pwm_ops_check(const struct pwm_chip *chip)
{
	const struct pwm_ops *ops = chip->ops;

	if (ops->write_waveform) {
		if (!ops->round_waveform_tohw ||
		    !ops->round_waveform_fromhw ||
		    !ops->write_waveform)
			return false;

		if (WFHWSIZE < ops->sizeof_wfhw) {
			dev_warn(pwmchip_parent(chip), "WFHWSIZE < %zu\n", ops->sizeof_wfhw);
			return false;
		}
	} else {
		if (!ops->apply)
			return false;

		if (IS_ENABLED(CONFIG_PWM_DEBUG) && !ops->get_state)
			dev_warn(pwmchip_parent(chip),
				 "Please implement the .get_state() callback\n");
	}

	return true;
}

static struct device_link *pwm_device_link_add(struct device *dev,
					       struct pwm_device *pwm)
{
	struct device_link *dl;

	if (!dev) {
		/*
		 * No device for the PWM consumer has been provided. It may
		 * impact the PM sequence ordering: the PWM supplier may get
		 * suspended before the consumer.
		 */
		dev_warn(pwmchip_parent(pwm->chip),
			 "No consumer device specified to create a link to\n");
		return NULL;
	}

	dl = device_link_add(dev, pwmchip_parent(pwm->chip), DL_FLAG_AUTOREMOVE_CONSUMER);
	if (!dl) {
		dev_err(dev, "failed to create device link to %s\n",
			dev_name(pwmchip_parent(pwm->chip)));
		return ERR_PTR(-EINVAL);
	}

	return dl;
}

static struct pwm_chip *fwnode_to_pwmchip(struct fwnode_handle *fwnode)
{
	struct pwm_chip *chip;
	unsigned long id, tmp;

	guard(mutex)(&pwm_lock);

	idr_for_each_entry_ul(&pwm_chips, chip, tmp, id)
		if (pwmchip_parent(chip) && device_match_fwnode(pwmchip_parent(chip), fwnode))
			return chip;

	return ERR_PTR(-EPROBE_DEFER);
}

/**
 * of_pwm_get() - request a PWM via the PWM framework
 * @dev: device for PWM consumer
 * @np: device node to get the PWM from
 * @con_id: consumer name
 *
 * Returns the PWM device parsed from the phandle and index specified in the
 * "pwms" property of a device tree node or a negative error-code on failure.
 * Values parsed from the device tree are stored in the returned PWM device
 * object.
 *
 * If con_id is NULL, the first PWM device listed in the "pwms" property will
 * be requested. Otherwise the "pwm-names" property is used to do a reverse
 * lookup of the PWM index. This also means that the "pwm-names" property
 * becomes mandatory for devices that look up the PWM device via the con_id
 * parameter.
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
static struct pwm_device *of_pwm_get(struct device *dev, struct device_node *np,
				     const char *con_id)
{
	struct pwm_device *pwm = NULL;
	struct of_phandle_args args;
	struct device_link *dl;
	struct pwm_chip *chip;
	int index = 0;
	int err;

	if (con_id) {
		index = of_property_match_string(np, "pwm-names", con_id);
		if (index < 0)
			return ERR_PTR(index);
	}

	err = of_parse_phandle_with_args(np, "pwms", "#pwm-cells", index,
					 &args);
	if (err) {
		pr_err("%s(): can't parse \"pwms\" property\n", __func__);
		return ERR_PTR(err);
	}

	chip = fwnode_to_pwmchip(of_fwnode_handle(args.np));
	if (IS_ERR(chip)) {
		if (PTR_ERR(chip) != -EPROBE_DEFER)
			pr_err("%s(): PWM chip not found\n", __func__);

		pwm = ERR_CAST(chip);
		goto put;
	}

	pwm = chip->of_xlate(chip, &args);
	if (IS_ERR(pwm))
		goto put;

	dl = pwm_device_link_add(dev, pwm);
	if (IS_ERR(dl)) {
		/* of_xlate ended up calling pwm_request_from_chip() */
		pwm_put(pwm);
		pwm = ERR_CAST(dl);
		goto put;
	}

	/*
	 * If a consumer name was not given, try to look it up from the
	 * "pwm-names" property if it exists. Otherwise use the name of
	 * the user device node.
	 */
	if (!con_id) {
		err = of_property_read_string_index(np, "pwm-names", index,
						    &con_id);
		if (err < 0)
			con_id = np->name;
	}

	pwm->label = con_id;

put:
	of_node_put(args.np);

	return pwm;
}

/**
 * acpi_pwm_get() - request a PWM via parsing "pwms" property in ACPI
 * @fwnode: firmware node to get the "pwms" property from
 *
 * Returns the PWM device parsed from the fwnode and index specified in the
 * "pwms" property or a negative error-code on failure.
 * Values parsed from the device tree are stored in the returned PWM device
 * object.
 *
 * This is analogous to of_pwm_get() except con_id is not yet supported.
 * ACPI entries must look like
 * Package () {"pwms", Package ()
 *     { <PWM device reference>, <PWM index>, <PWM period> [, <PWM flags>]}}
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
static struct pwm_device *acpi_pwm_get(const struct fwnode_handle *fwnode)
{
	struct pwm_device *pwm;
	struct fwnode_reference_args args;
	struct pwm_chip *chip;
	int ret;

	memset(&args, 0, sizeof(args));

	ret = __acpi_node_get_property_reference(fwnode, "pwms", 0, 3, &args);
	if (ret < 0)
		return ERR_PTR(ret);

	if (args.nargs < 2)
		return ERR_PTR(-EPROTO);

	chip = fwnode_to_pwmchip(args.fwnode);
	if (IS_ERR(chip))
		return ERR_CAST(chip);

	pwm = pwm_request_from_chip(chip, args.args[0], NULL);
	if (IS_ERR(pwm))
		return pwm;

	pwm->args.period = args.args[1];
	pwm->args.polarity = PWM_POLARITY_NORMAL;

	if (args.nargs > 2 && args.args[2] & PWM_POLARITY_INVERTED)
		pwm->args.polarity = PWM_POLARITY_INVERSED;

	return pwm;
}

static DEFINE_MUTEX(pwm_lookup_lock);
static LIST_HEAD(pwm_lookup_list);

/**
 * pwm_get() - look up and request a PWM device
 * @dev: device for PWM consumer
 * @con_id: consumer name
 *
 * Lookup is first attempted using DT. If the device was not instantiated from
 * a device tree, a PWM chip and a relative index is looked up via a table
 * supplied by board setup code (see pwm_add_table()).
 *
 * Once a PWM chip has been found the specified PWM device will be requested
 * and is ready to be used.
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
struct pwm_device *pwm_get(struct device *dev, const char *con_id)
{
	const struct fwnode_handle *fwnode = dev ? dev_fwnode(dev) : NULL;
	const char *dev_id = dev ? dev_name(dev) : NULL;
	struct pwm_device *pwm;
	struct pwm_chip *chip;
	struct device_link *dl;
	unsigned int best = 0;
	struct pwm_lookup *p, *chosen = NULL;
	unsigned int match;
	int err;

	/* look up via DT first */
	if (is_of_node(fwnode))
		return of_pwm_get(dev, to_of_node(fwnode), con_id);

	/* then lookup via ACPI */
	if (is_acpi_node(fwnode)) {
		pwm = acpi_pwm_get(fwnode);
		if (!IS_ERR(pwm) || PTR_ERR(pwm) != -ENOENT)
			return pwm;
	}

	/*
	 * We look up the provider in the static table typically provided by
	 * board setup code. We first try to lookup the consumer device by
	 * name. If the consumer device was passed in as NULL or if no match
	 * was found, we try to find the consumer by directly looking it up
	 * by name.
	 *
	 * If a match is found, the provider PWM chip is looked up by name
	 * and a PWM device is requested using the PWM device per-chip index.
	 *
	 * The lookup algorithm was shamelessly taken from the clock
	 * framework:
	 *
	 * We do slightly fuzzy matching here:
	 *  An entry with a NULL ID is assumed to be a wildcard.
	 *  If an entry has a device ID, it must match
	 *  If an entry has a connection ID, it must match
	 * Then we take the most specific entry - with the following order
	 * of precedence: dev+con > dev only > con only.
	 */
	scoped_guard(mutex, &pwm_lookup_lock)
		list_for_each_entry(p, &pwm_lookup_list, list) {
			match = 0;

			if (p->dev_id) {
				if (!dev_id || strcmp(p->dev_id, dev_id))
					continue;

				match += 2;
			}

			if (p->con_id) {
				if (!con_id || strcmp(p->con_id, con_id))
					continue;

				match += 1;
			}

			if (match > best) {
				chosen = p;

				if (match != 3)
					best = match;
				else
					break;
			}
		}

	if (!chosen)
		return ERR_PTR(-ENODEV);

	chip = pwmchip_find_by_name(chosen->provider);

	/*
	 * If the lookup entry specifies a module, load the module and retry
	 * the PWM chip lookup. This can be used to work around driver load
	 * ordering issues if driver's can't be made to properly support the
	 * deferred probe mechanism.
	 */
	if (!chip && chosen->module) {
		err = request_module(chosen->module);
		if (err == 0)
			chip = pwmchip_find_by_name(chosen->provider);
	}

	if (!chip)
		return ERR_PTR(-EPROBE_DEFER);

	pwm = pwm_request_from_chip(chip, chosen->index, con_id ?: dev_id);
	if (IS_ERR(pwm))
		return pwm;

	dl = pwm_device_link_add(dev, pwm);
	if (IS_ERR(dl)) {
		pwm_put(pwm);
		return ERR_CAST(dl);
	}

	pwm->args.period = chosen->period;
	pwm->args.polarity = chosen->polarity;

	return pwm;
}
EXPORT_SYMBOL_GPL(pwm_get);

/**
 * pwm_put() - release a PWM device
 * @pwm: PWM device
 */
void pwm_put(struct pwm_device *pwm)
{
	struct pwm_chip *chip;

	if (!pwm)
		return;

	chip = pwm->chip;

	guard(mutex)(&pwm_lock);

	/*
	 * Trigger a warning if a consumer called pwm_put() twice.
	 * If the chip isn't operational, PWMF_REQUESTED was already cleared in
	 * pwmchip_remove(). So don't warn in this case.
	 */
	if (chip->operational && !test_and_clear_bit(PWMF_REQUESTED, &pwm->flags)) {
		pr_warn("PWM device already freed\n");
		return;
	}

	if (chip->operational && chip->ops->free)
		pwm->chip->ops->free(pwm->chip, pwm);

	pwm->label = NULL;

	put_device(&chip->dev);

	module_put(chip->owner);
}
EXPORT_SYMBOL_GPL(pwm_put);

static void devm_pwm_release(void *pwm)
{
	pwm_put(pwm);
}

/**
 * devm_pwm_get() - resource managed pwm_get()
 * @dev: device for PWM consumer
 * @con_id: consumer name
 *
 * This function performs like pwm_get() but the acquired PWM device will
 * automatically be released on driver detach.
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
struct pwm_device *devm_pwm_get(struct device *dev, const char *con_id)
{
	struct pwm_device *pwm;
	int ret;

	pwm = pwm_get(dev, con_id);
	if (IS_ERR(pwm))
		return pwm;

	ret = devm_add_action_or_reset(dev, devm_pwm_release, pwm);
	if (ret)
		return ERR_PTR(ret);

	return pwm;
}
EXPORT_SYMBOL_GPL(devm_pwm_get);

/**
 * devm_fwnode_pwm_get() - request a resource managed PWM from firmware node
 * @dev: device for PWM consumer
 * @fwnode: firmware node to get the PWM from
 * @con_id: consumer name
 *
 * Returns the PWM device parsed from the firmware node. See of_pwm_get() and
 * acpi_pwm_get() for a detailed description.
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
struct pwm_device *devm_fwnode_pwm_get(struct device *dev,
				       struct fwnode_handle *fwnode,
				       const char *con_id)
{
	struct pwm_device *pwm = ERR_PTR(-ENODEV);
	int ret;

	if (is_of_node(fwnode))
		pwm = of_pwm_get(dev, to_of_node(fwnode), con_id);
	else if (is_acpi_node(fwnode))
		pwm = acpi_pwm_get(fwnode);
	if (IS_ERR(pwm))
		return pwm;

	ret = devm_add_action_or_reset(dev, devm_pwm_release, pwm);
	if (ret)
		return ERR_PTR(ret);

	return pwm;
}
EXPORT_SYMBOL_GPL(devm_fwnode_pwm_get);

/**
 * __pwmchip_add() - register a new PWM chip
 * @chip: the PWM chip to add
 * @owner: reference to the module providing the chip.
 *
 * Register a new PWM chip. @owner is supposed to be THIS_MODULE, use the
 * pwmchip_add wrapper to do this right.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int __pwmchip_add(struct pwm_chip *chip, struct module *owner)
{
	int ret;

	if (!chip || !pwmchip_parent(chip) || !chip->ops || !chip->npwm)
		return -EINVAL;

	/*
	 * a struct pwm_chip must be allocated using (devm_)pwmchip_alloc,
	 * otherwise the embedded struct device might disappear too early
	 * resulting in memory corruption.
	 * Catch drivers that were not converted appropriately.
	 */
	if (!chip->uses_pwmchip_alloc)
		return -EINVAL;

	if (!pwm_ops_check(chip))
		return -EINVAL;

	chip->owner = owner;

	if (chip->atomic)
		spin_lock_init(&chip->atomic_lock);
	else
		mutex_init(&chip->nonatomic_lock);

	guard(mutex)(&pwm_lock);

	ret = idr_alloc(&pwm_chips, chip, 0, 0, GFP_KERNEL);
	if (ret < 0)
		return ret;

	chip->id = ret;

	dev_set_name(&chip->dev, "pwmchip%u", chip->id);

	if (IS_ENABLED(CONFIG_OF))
		of_pwmchip_add(chip);

	scoped_guard(pwmchip, chip)
		chip->operational = true;

	ret = device_add(&chip->dev);
	if (ret)
		goto err_device_add;

	return 0;

err_device_add:
	scoped_guard(pwmchip, chip)
		chip->operational = false;

	if (IS_ENABLED(CONFIG_OF))
		of_pwmchip_remove(chip);

	idr_remove(&pwm_chips, chip->id);

	return ret;
}
EXPORT_SYMBOL_GPL(__pwmchip_add);

/**
 * pwmchip_remove() - remove a PWM chip
 * @chip: the PWM chip to remove
 *
 * Removes a PWM chip.
 */
void pwmchip_remove(struct pwm_chip *chip)
{
	pwmchip_sysfs_unexport(chip);

	scoped_guard(mutex, &pwm_lock) {
		unsigned int i;

		scoped_guard(pwmchip, chip)
			chip->operational = false;

		for (i = 0; i < chip->npwm; ++i) {
			struct pwm_device *pwm = &chip->pwms[i];

			if (test_and_clear_bit(PWMF_REQUESTED, &pwm->flags)) {
				dev_warn(&chip->dev, "Freeing requested PWM #%u\n", i);
				if (pwm->chip->ops->free)
					pwm->chip->ops->free(pwm->chip, pwm);
			}
		}

		if (IS_ENABLED(CONFIG_OF))
			of_pwmchip_remove(chip);

		idr_remove(&pwm_chips, chip->id);
	}

	device_del(&chip->dev);
}
EXPORT_SYMBOL_GPL(pwmchip_remove);

static void devm_pwmchip_remove(void *data)
{
	struct pwm_chip *chip = data;

	pwmchip_remove(chip);
}

int __devm_pwmchip_add(struct device *dev, struct pwm_chip *chip, struct module *owner)
{
	int ret;

	ret = __pwmchip_add(chip, owner);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, devm_pwmchip_remove, chip);
}
EXPORT_SYMBOL_GPL(__devm_pwmchip_add);

/**
 * pwm_add_table() - register PWM device consumers
 * @table: array of consumers to register
 * @num: number of consumers in table
 */
void pwm_add_table(struct pwm_lookup *table, size_t num)
{
	guard(mutex)(&pwm_lookup_lock);

	while (num--) {
		list_add_tail(&table->list, &pwm_lookup_list);
		table++;
	}
}

/**
 * pwm_remove_table() - unregister PWM device consumers
 * @table: array of consumers to unregister
 * @num: number of consumers in table
 */
void pwm_remove_table(struct pwm_lookup *table, size_t num)
{
	guard(mutex)(&pwm_lookup_lock);

	while (num--) {
		list_del(&table->list);
		table++;
	}
}

static void pwm_dbg_show(struct pwm_chip *chip, struct seq_file *s)
{
	unsigned int i;

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];
		struct pwm_state state;

		pwm_get_state(pwm, &state);

		seq_printf(s, " pwm-%-3d (%-20.20s):", i, pwm->label);

		if (test_bit(PWMF_REQUESTED, &pwm->flags))
			seq_puts(s, " requested");

		if (state.enabled)
			seq_puts(s, " enabled");

		seq_printf(s, " period: %llu ns", state.period);
		seq_printf(s, " duty: %llu ns", state.duty_cycle);
		seq_printf(s, " polarity: %s",
			   state.polarity ? "inverse" : "normal");

		if (state.usage_power)
			seq_puts(s, " usage_power");

		seq_puts(s, "\n");
	}
}

static void *pwm_seq_start(struct seq_file *s, loff_t *pos)
{
	unsigned long id = *pos;
	void *ret;

	mutex_lock(&pwm_lock);
	s->private = "";

	ret = idr_get_next_ul(&pwm_chips, &id);
	*pos = id;
	return ret;
}

static void *pwm_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	unsigned long id = *pos + 1;
	void *ret;

	s->private = "\n";

	ret = idr_get_next_ul(&pwm_chips, &id);
	*pos = id;
	return ret;
}

static void pwm_seq_stop(struct seq_file *s, void *v)
{
	mutex_unlock(&pwm_lock);
}

static int pwm_seq_show(struct seq_file *s, void *v)
{
	struct pwm_chip *chip = v;

	seq_printf(s, "%s%d: %s/%s, %d PWM device%s\n",
		   (char *)s->private, chip->id,
		   pwmchip_parent(chip)->bus ? pwmchip_parent(chip)->bus->name : "no-bus",
		   dev_name(pwmchip_parent(chip)), chip->npwm,
		   (chip->npwm != 1) ? "s" : "");

	pwm_dbg_show(chip, s);

	return 0;
}

static const struct seq_operations pwm_debugfs_sops = {
	.start = pwm_seq_start,
	.next = pwm_seq_next,
	.stop = pwm_seq_stop,
	.show = pwm_seq_show,
};

DEFINE_SEQ_ATTRIBUTE(pwm_debugfs);

static int __init pwm_init(void)
{
	int ret;

	ret = class_register(&pwm_class);
	if (ret) {
		pr_err("Failed to initialize PWM class (%pe)\n", ERR_PTR(ret));
		return ret;
	}

	if (IS_ENABLED(CONFIG_DEBUG_FS))
		debugfs_create_file("pwm", 0444, NULL, NULL, &pwm_debugfs_fops);

	return 0;
}
subsys_initcall(pwm_init);
