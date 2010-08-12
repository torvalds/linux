/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform PWM support
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/gpio.h>

#include <asm/mach-jz4740/gpio.h>
#include "timer.h"

static struct clk *jz4740_pwm_clk;

DEFINE_MUTEX(jz4740_pwm_mutex);

struct pwm_device {
	unsigned int id;
	unsigned int gpio;
	bool used;
};

static struct pwm_device jz4740_pwm_list[] = {
	{ 2, JZ_GPIO_PWM2, false },
	{ 3, JZ_GPIO_PWM3, false },
	{ 4, JZ_GPIO_PWM4, false },
	{ 5, JZ_GPIO_PWM5, false },
	{ 6, JZ_GPIO_PWM6, false },
	{ 7, JZ_GPIO_PWM7, false },
};

struct pwm_device *pwm_request(int id, const char *label)
{
	int ret = 0;
	struct pwm_device *pwm;

	if (id < 2 || id > 7 || !jz4740_pwm_clk)
		return ERR_PTR(-ENODEV);

	mutex_lock(&jz4740_pwm_mutex);

	pwm = &jz4740_pwm_list[id - 2];
	if (pwm->used)
		ret = -EBUSY;
	else
		pwm->used = true;

	mutex_unlock(&jz4740_pwm_mutex);

	if (ret)
		return ERR_PTR(ret);

	ret = gpio_request(pwm->gpio, label);

	if (ret) {
		printk(KERN_ERR "Failed to request pwm gpio: %d\n", ret);
		pwm->used = false;
		return ERR_PTR(ret);
	}

	jz_gpio_set_function(pwm->gpio, JZ_GPIO_FUNC_PWM);

	jz4740_timer_start(id);

	return pwm;
}

void pwm_free(struct pwm_device *pwm)
{
	pwm_disable(pwm);
	jz4740_timer_set_ctrl(pwm->id, 0);

	jz_gpio_set_function(pwm->gpio, JZ_GPIO_FUNC_NONE);
	gpio_free(pwm->gpio);

	jz4740_timer_stop(pwm->id);

	pwm->used = false;
}

int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	unsigned long long tmp;
	unsigned long period, duty;
	unsigned int prescaler = 0;
	unsigned int id = pwm->id;
	uint16_t ctrl;
	bool is_enabled;

	if (duty_ns < 0 || duty_ns > period_ns)
		return -EINVAL;

	tmp = (unsigned long long)clk_get_rate(jz4740_pwm_clk) * period_ns;
	do_div(tmp, 1000000000);
	period = tmp;

	while (period > 0xffff && prescaler < 6) {
		period >>= 2;
		++prescaler;
	}

	if (prescaler == 6)
		return -EINVAL;

	tmp = (unsigned long long)period * duty_ns;
	do_div(tmp, period_ns);
	duty = period - tmp;

	if (duty >= period)
		duty = period - 1;

	is_enabled = jz4740_timer_is_enabled(id);
	if (is_enabled)
		pwm_disable(pwm);

	jz4740_timer_set_count(id, 0);
	jz4740_timer_set_duty(id, duty);
	jz4740_timer_set_period(id, period);

	ctrl = JZ_TIMER_CTRL_PRESCALER(prescaler) | JZ_TIMER_CTRL_SRC_EXT |
		JZ_TIMER_CTRL_PWM_ABBRUPT_SHUTDOWN;

	jz4740_timer_set_ctrl(id, ctrl);

	if (is_enabled)
		pwm_enable(pwm);

	return 0;
}

int pwm_enable(struct pwm_device *pwm)
{
	uint32_t ctrl = jz4740_timer_get_ctrl(pwm->id);

	ctrl |= JZ_TIMER_CTRL_PWM_ENABLE;
	jz4740_timer_set_ctrl(pwm->id, ctrl);
	jz4740_timer_enable(pwm->id);

	return 0;
}

void pwm_disable(struct pwm_device *pwm)
{
	uint32_t ctrl = jz4740_timer_get_ctrl(pwm->id);

	ctrl &= ~JZ_TIMER_CTRL_PWM_ENABLE;
	jz4740_timer_disable(pwm->id);
	jz4740_timer_set_ctrl(pwm->id, ctrl);
}

static int __init jz4740_pwm_init(void)
{
	int ret = 0;

	jz4740_pwm_clk = clk_get(NULL, "ext");

	if (IS_ERR(jz4740_pwm_clk)) {
		ret = PTR_ERR(jz4740_pwm_clk);
		jz4740_pwm_clk = NULL;
	}

	return ret;
}
subsys_initcall(jz4740_pwm_init);
