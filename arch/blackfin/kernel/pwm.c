/*
 * Blackfin Pulse Width Modulation (PWM) core
 *
 * Copyright (c) 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#include <asm/gptimers.h>
#include <asm/portmux.h>

struct pwm_device {
	unsigned id;
	unsigned short pin;
};

static const unsigned short pwm_to_gptimer_per[] = {
	P_TMR0, P_TMR1, P_TMR2, P_TMR3, P_TMR4, P_TMR5,
	P_TMR6, P_TMR7, P_TMR8, P_TMR9, P_TMR10, P_TMR11,
};

struct pwm_device *pwm_request(int pwm_id, const char *label)
{
	struct pwm_device *pwm;
	int ret;

	/* XXX: pwm_id really should be unsigned */
	if (pwm_id < 0)
		return NULL;

	pwm = kzalloc(sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return pwm;

	pwm->id = pwm_id;
	if (pwm->id >= ARRAY_SIZE(pwm_to_gptimer_per))
		goto err;

	pwm->pin = pwm_to_gptimer_per[pwm->id];
	ret = peripheral_request(pwm->pin, label);
	if (ret)
		goto err;

	return pwm;
 err:
	kfree(pwm);
	return NULL;
}
EXPORT_SYMBOL(pwm_request);

void pwm_free(struct pwm_device *pwm)
{
	peripheral_free(pwm->pin);
	kfree(pwm);
}
EXPORT_SYMBOL(pwm_free);

int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	unsigned long period, duty;
	unsigned long long val;

	if (duty_ns < 0 || duty_ns > period_ns)
		return -EINVAL;

	val = (unsigned long long)get_sclk() * period_ns;
	do_div(val, NSEC_PER_SEC);
	period = val;

	val = (unsigned long long)period * duty_ns;
	do_div(val, period_ns);
	duty = period - val;

	if (duty >= period)
		duty = period - 1;

	set_gptimer_config(pwm->id, TIMER_MODE_PWM | TIMER_PERIOD_CNT);
	set_gptimer_pwidth(pwm->id, duty);
	set_gptimer_period(pwm->id, period);

	return 0;
}
EXPORT_SYMBOL(pwm_config);

int pwm_enable(struct pwm_device *pwm)
{
	enable_gptimer(pwm->id);
	return 0;
}
EXPORT_SYMBOL(pwm_enable);

void pwm_disable(struct pwm_device *pwm)
{
	disable_gptimer(pwm->id);
}
EXPORT_SYMBOL(pwm_disable);
