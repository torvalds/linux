// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Sean Young <sean@mess.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pwm.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include <linux/completion.h>
#include <media/rc-core.h>

#define DRIVER_NAME	"pwm-ir-tx"
#define DEVICE_NAME	"PWM IR Transmitter"

struct pwm_ir {
	struct pwm_device *pwm;
	struct hrtimer timer;
	struct completion tx_done;
	struct pwm_state *state;
	u32 carrier;
	u32 duty_cycle;
	const unsigned int *txbuf;
	unsigned int txbuf_len;
	unsigned int txbuf_index;
};

static const struct of_device_id pwm_ir_of_match[] = {
	{ .compatible = "pwm-ir-tx", },
	{ .compatible = "nokia,n900-ir" },
	{ },
};
MODULE_DEVICE_TABLE(of, pwm_ir_of_match);

static int pwm_ir_set_duty_cycle(struct rc_dev *dev, u32 duty_cycle)
{
	struct pwm_ir *pwm_ir = dev->priv;

	pwm_ir->duty_cycle = duty_cycle;

	return 0;
}

static int pwm_ir_set_carrier(struct rc_dev *dev, u32 carrier)
{
	struct pwm_ir *pwm_ir = dev->priv;

	if (!carrier)
		return -EINVAL;

	pwm_ir->carrier = carrier;

	return 0;
}

static int pwm_ir_tx_sleep(struct rc_dev *dev, unsigned int *txbuf,
			   unsigned int count)
{
	struct pwm_ir *pwm_ir = dev->priv;
	struct pwm_device *pwm = pwm_ir->pwm;
	struct pwm_state state;
	int i;
	ktime_t edge;
	long delta;

	pwm_init_state(pwm, &state);

	state.period = DIV_ROUND_CLOSEST(NSEC_PER_SEC, pwm_ir->carrier);
	pwm_set_relative_duty_cycle(&state, pwm_ir->duty_cycle, 100);

	edge = ktime_get();

	for (i = 0; i < count; i++) {
		state.enabled = !(i % 2);
		pwm_apply_might_sleep(pwm, &state);

		edge = ktime_add_us(edge, txbuf[i]);
		delta = ktime_us_delta(edge, ktime_get());
		if (delta > 0)
			usleep_range(delta, delta + 10);
	}

	state.enabled = false;
	pwm_apply_might_sleep(pwm, &state);

	return count;
}

static int pwm_ir_tx_atomic(struct rc_dev *dev, unsigned int *txbuf,
			    unsigned int count)
{
	struct pwm_ir *pwm_ir = dev->priv;
	struct pwm_device *pwm = pwm_ir->pwm;
	struct pwm_state state;

	pwm_init_state(pwm, &state);

	state.period = DIV_ROUND_CLOSEST(NSEC_PER_SEC, pwm_ir->carrier);
	pwm_set_relative_duty_cycle(&state, pwm_ir->duty_cycle, 100);

	pwm_ir->txbuf = txbuf;
	pwm_ir->txbuf_len = count;
	pwm_ir->txbuf_index = 0;
	pwm_ir->state = &state;

	hrtimer_start(&pwm_ir->timer, 0, HRTIMER_MODE_REL);

	wait_for_completion(&pwm_ir->tx_done);

	return count;
}

static enum hrtimer_restart pwm_ir_timer(struct hrtimer *timer)
{
	struct pwm_ir *pwm_ir = container_of(timer, struct pwm_ir, timer);
	ktime_t now;

	/*
	 * If we happen to hit an odd latency spike, loop through the
	 * pulses until we catch up.
	 */
	do {
		u64 ns;

		pwm_ir->state->enabled = !(pwm_ir->txbuf_index % 2);
		pwm_apply_atomic(pwm_ir->pwm, pwm_ir->state);

		if (pwm_ir->txbuf_index >= pwm_ir->txbuf_len) {
			complete(&pwm_ir->tx_done);

			return HRTIMER_NORESTART;
		}

		ns = US_TO_NS(pwm_ir->txbuf[pwm_ir->txbuf_index]);
		hrtimer_add_expires_ns(timer, ns);

		pwm_ir->txbuf_index++;

		now = timer->base->get_time();
	} while (hrtimer_get_expires_tv64(timer) < now);

	return HRTIMER_RESTART;
}

static int pwm_ir_probe(struct platform_device *pdev)
{
	struct pwm_ir *pwm_ir;
	struct rc_dev *rcdev;
	int rc;

	pwm_ir = devm_kmalloc(&pdev->dev, sizeof(*pwm_ir), GFP_KERNEL);
	if (!pwm_ir)
		return -ENOMEM;

	pwm_ir->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(pwm_ir->pwm))
		return PTR_ERR(pwm_ir->pwm);

	pwm_ir->carrier = 38000;
	pwm_ir->duty_cycle = 50;

	rcdev = devm_rc_allocate_device(&pdev->dev, RC_DRIVER_IR_RAW_TX);
	if (!rcdev)
		return -ENOMEM;

	if (pwm_might_sleep(pwm_ir->pwm)) {
		dev_info(&pdev->dev, "TX will not be accurate as PWM device might sleep\n");
		rcdev->tx_ir = pwm_ir_tx_sleep;
	} else {
		init_completion(&pwm_ir->tx_done);
		hrtimer_init(&pwm_ir->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		pwm_ir->timer.function = pwm_ir_timer;
		rcdev->tx_ir = pwm_ir_tx_atomic;
	}

	rcdev->priv = pwm_ir;
	rcdev->driver_name = DRIVER_NAME;
	rcdev->device_name = DEVICE_NAME;
	rcdev->s_tx_duty_cycle = pwm_ir_set_duty_cycle;
	rcdev->s_tx_carrier = pwm_ir_set_carrier;

	rc = devm_rc_register_device(&pdev->dev, rcdev);
	if (rc < 0)
		dev_err(&pdev->dev, "failed to register rc device\n");

	return rc;
}

static struct platform_driver pwm_ir_driver = {
	.probe = pwm_ir_probe,
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = pwm_ir_of_match,
	},
};
module_platform_driver(pwm_ir_driver);

MODULE_DESCRIPTION("PWM IR Transmitter");
MODULE_AUTHOR("Sean Young <sean@mess.org>");
MODULE_LICENSE("GPL");
