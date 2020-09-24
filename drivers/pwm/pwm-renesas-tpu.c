/*
 * R-Mobile TPU PWM driver
 *
 * Copyright (C) 2012 Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define TPU_CHANNEL_MAX		4

#define TPU_TSTR		0x00	/* Timer start register (shared) */

#define TPU_TCRn		0x00	/* Timer control register */
#define TPU_TCR_CCLR_NONE	(0 << 5)
#define TPU_TCR_CCLR_TGRA	(1 << 5)
#define TPU_TCR_CCLR_TGRB	(2 << 5)
#define TPU_TCR_CCLR_TGRC	(5 << 5)
#define TPU_TCR_CCLR_TGRD	(6 << 5)
#define TPU_TCR_CKEG_RISING	(0 << 3)
#define TPU_TCR_CKEG_FALLING	(1 << 3)
#define TPU_TCR_CKEG_BOTH	(2 << 3)
#define TPU_TMDRn		0x04	/* Timer mode register */
#define TPU_TMDR_BFWT		(1 << 6)
#define TPU_TMDR_BFB		(1 << 5)
#define TPU_TMDR_BFA		(1 << 4)
#define TPU_TMDR_MD_NORMAL	(0 << 0)
#define TPU_TMDR_MD_PWM		(2 << 0)
#define TPU_TIORn		0x08	/* Timer I/O control register */
#define TPU_TIOR_IOA_0		(0 << 0)
#define TPU_TIOR_IOA_0_CLR	(1 << 0)
#define TPU_TIOR_IOA_0_SET	(2 << 0)
#define TPU_TIOR_IOA_0_TOGGLE	(3 << 0)
#define TPU_TIOR_IOA_1		(4 << 0)
#define TPU_TIOR_IOA_1_CLR	(5 << 0)
#define TPU_TIOR_IOA_1_SET	(6 << 0)
#define TPU_TIOR_IOA_1_TOGGLE	(7 << 0)
#define TPU_TIERn		0x0c	/* Timer interrupt enable register */
#define TPU_TSRn		0x10	/* Timer status register */
#define TPU_TCNTn		0x14	/* Timer counter */
#define TPU_TGRAn		0x18	/* Timer general register A */
#define TPU_TGRBn		0x1c	/* Timer general register B */
#define TPU_TGRCn		0x20	/* Timer general register C */
#define TPU_TGRDn		0x24	/* Timer general register D */

#define TPU_CHANNEL_OFFSET	0x10
#define TPU_CHANNEL_SIZE	0x40

enum tpu_pin_state {
	TPU_PIN_INACTIVE,		/* Pin is driven inactive */
	TPU_PIN_PWM,			/* Pin is driven by PWM */
	TPU_PIN_ACTIVE,			/* Pin is driven active */
};

struct tpu_device;

struct tpu_pwm_device {
	bool timer_on;			/* Whether the timer is running */

	struct tpu_device *tpu;
	unsigned int channel;		/* Channel number in the TPU */

	enum pwm_polarity polarity;
	unsigned int prescaler;
	u16 period;
	u16 duty;
};

struct tpu_device {
	struct platform_device *pdev;
	struct pwm_chip chip;
	spinlock_t lock;

	void __iomem *base;
	struct clk *clk;
};

#define to_tpu_device(c)	container_of(c, struct tpu_device, chip)

static void tpu_pwm_write(struct tpu_pwm_device *pwm, int reg_nr, u16 value)
{
	void __iomem *base = pwm->tpu->base + TPU_CHANNEL_OFFSET
			   + pwm->channel * TPU_CHANNEL_SIZE;

	iowrite16(value, base + reg_nr);
}

static void tpu_pwm_set_pin(struct tpu_pwm_device *pwm,
			    enum tpu_pin_state state)
{
	static const char * const states[] = { "inactive", "PWM", "active" };

	dev_dbg(&pwm->tpu->pdev->dev, "%u: configuring pin as %s\n",
		pwm->channel, states[state]);

	switch (state) {
	case TPU_PIN_INACTIVE:
		tpu_pwm_write(pwm, TPU_TIORn,
			      pwm->polarity == PWM_POLARITY_INVERSED ?
			      TPU_TIOR_IOA_1 : TPU_TIOR_IOA_0);
		break;
	case TPU_PIN_PWM:
		tpu_pwm_write(pwm, TPU_TIORn,
			      pwm->polarity == PWM_POLARITY_INVERSED ?
			      TPU_TIOR_IOA_0_SET : TPU_TIOR_IOA_1_CLR);
		break;
	case TPU_PIN_ACTIVE:
		tpu_pwm_write(pwm, TPU_TIORn,
			      pwm->polarity == PWM_POLARITY_INVERSED ?
			      TPU_TIOR_IOA_0 : TPU_TIOR_IOA_1);
		break;
	}
}

static void tpu_pwm_start_stop(struct tpu_pwm_device *pwm, int start)
{
	unsigned long flags;
	u16 value;

	spin_lock_irqsave(&pwm->tpu->lock, flags);
	value = ioread16(pwm->tpu->base + TPU_TSTR);

	if (start)
		value |= 1 << pwm->channel;
	else
		value &= ~(1 << pwm->channel);

	iowrite16(value, pwm->tpu->base + TPU_TSTR);
	spin_unlock_irqrestore(&pwm->tpu->lock, flags);
}

static int tpu_pwm_timer_start(struct tpu_pwm_device *pwm)
{
	int ret;

	if (!pwm->timer_on) {
		/* Wake up device and enable clock. */
		pm_runtime_get_sync(&pwm->tpu->pdev->dev);
		ret = clk_prepare_enable(pwm->tpu->clk);
		if (ret) {
			dev_err(&pwm->tpu->pdev->dev, "cannot enable clock\n");
			return ret;
		}
		pwm->timer_on = true;
	}

	/*
	 * Make sure the channel is stopped, as we need to reconfigure it
	 * completely. First drive the pin to the inactive state to avoid
	 * glitches.
	 */
	tpu_pwm_set_pin(pwm, TPU_PIN_INACTIVE);
	tpu_pwm_start_stop(pwm, false);

	/*
	 * - Clear TCNT on TGRB match
	 * - Count on rising edge
	 * - Set prescaler
	 * - Output 0 until TGRA, output 1 until TGRB (active low polarity)
	 * - Output 1 until TGRA, output 0 until TGRB (active high polarity
	 * - PWM mode
	 */
	tpu_pwm_write(pwm, TPU_TCRn, TPU_TCR_CCLR_TGRB | TPU_TCR_CKEG_RISING |
		      pwm->prescaler);
	tpu_pwm_write(pwm, TPU_TMDRn, TPU_TMDR_MD_PWM);
	tpu_pwm_set_pin(pwm, TPU_PIN_PWM);
	tpu_pwm_write(pwm, TPU_TGRAn, pwm->duty);
	tpu_pwm_write(pwm, TPU_TGRBn, pwm->period);

	dev_dbg(&pwm->tpu->pdev->dev, "%u: TGRA 0x%04x TGRB 0x%04x\n",
		pwm->channel, pwm->duty, pwm->period);

	/* Start the channel. */
	tpu_pwm_start_stop(pwm, true);

	return 0;
}

static void tpu_pwm_timer_stop(struct tpu_pwm_device *pwm)
{
	if (!pwm->timer_on)
		return;

	/* Disable channel. */
	tpu_pwm_start_stop(pwm, false);

	/* Stop clock and mark device as idle. */
	clk_disable_unprepare(pwm->tpu->clk);
	pm_runtime_put(&pwm->tpu->pdev->dev);

	pwm->timer_on = false;
}

/* -----------------------------------------------------------------------------
 * PWM API
 */

static int tpu_pwm_request(struct pwm_chip *chip, struct pwm_device *_pwm)
{
	struct tpu_device *tpu = to_tpu_device(chip);
	struct tpu_pwm_device *pwm;

	if (_pwm->hwpwm >= TPU_CHANNEL_MAX)
		return -EINVAL;

	pwm = kzalloc(sizeof(*pwm), GFP_KERNEL);
	if (pwm == NULL)
		return -ENOMEM;

	pwm->tpu = tpu;
	pwm->channel = _pwm->hwpwm;
	pwm->polarity = PWM_POLARITY_NORMAL;
	pwm->prescaler = 0;
	pwm->period = 0;
	pwm->duty = 0;

	pwm->timer_on = false;

	pwm_set_chip_data(_pwm, pwm);

	return 0;
}

static void tpu_pwm_free(struct pwm_chip *chip, struct pwm_device *_pwm)
{
	struct tpu_pwm_device *pwm = pwm_get_chip_data(_pwm);

	tpu_pwm_timer_stop(pwm);
	kfree(pwm);
}

static int tpu_pwm_config(struct pwm_chip *chip, struct pwm_device *_pwm,
			  int duty_ns, int period_ns)
{
	static const unsigned int prescalers[] = { 1, 4, 16, 64 };
	struct tpu_pwm_device *pwm = pwm_get_chip_data(_pwm);
	struct tpu_device *tpu = to_tpu_device(chip);
	unsigned int prescaler;
	bool duty_only = false;
	u32 clk_rate;
	u32 period;
	u32 duty;
	int ret;

	/*
	 * Pick a prescaler to avoid overflowing the counter.
	 * TODO: Pick the highest acceptable prescaler.
	 */
	clk_rate = clk_get_rate(tpu->clk);

	for (prescaler = 0; prescaler < ARRAY_SIZE(prescalers); ++prescaler) {
		period = clk_rate / prescalers[prescaler]
		       / (NSEC_PER_SEC / period_ns);
		if (period <= 0xffff)
			break;
	}

	if (prescaler == ARRAY_SIZE(prescalers) || period == 0) {
		dev_err(&tpu->pdev->dev, "clock rate mismatch\n");
		return -ENOTSUPP;
	}

	if (duty_ns) {
		duty = clk_rate / prescalers[prescaler]
		     / (NSEC_PER_SEC / duty_ns);
		if (duty > period)
			return -EINVAL;
	} else {
		duty = 0;
	}

	dev_dbg(&tpu->pdev->dev,
		"rate %u, prescaler %u, period %u, duty %u\n",
		clk_rate, prescalers[prescaler], period, duty);

	if (pwm->prescaler == prescaler && pwm->period == period)
		duty_only = true;

	pwm->prescaler = prescaler;
	pwm->period = period;
	pwm->duty = duty;

	/* If the channel is disabled we're done. */
	if (!pwm_is_enabled(_pwm))
		return 0;

	if (duty_only && pwm->timer_on) {
		/*
		 * If only the duty cycle changed and the timer is already
		 * running, there's no need to reconfigure it completely, Just
		 * modify the duty cycle.
		 */
		tpu_pwm_write(pwm, TPU_TGRAn, pwm->duty);
		dev_dbg(&tpu->pdev->dev, "%u: TGRA 0x%04x\n", pwm->channel,
			pwm->duty);
	} else {
		/* Otherwise perform a full reconfiguration. */
		ret = tpu_pwm_timer_start(pwm);
		if (ret < 0)
			return ret;
	}

	if (duty == 0 || duty == period) {
		/*
		 * To avoid running the timer when not strictly required, handle
		 * 0% and 100% duty cycles as fixed levels and stop the timer.
		 */
		tpu_pwm_set_pin(pwm, duty ? TPU_PIN_ACTIVE : TPU_PIN_INACTIVE);
		tpu_pwm_timer_stop(pwm);
	}

	return 0;
}

static int tpu_pwm_set_polarity(struct pwm_chip *chip, struct pwm_device *_pwm,
				enum pwm_polarity polarity)
{
	struct tpu_pwm_device *pwm = pwm_get_chip_data(_pwm);

	pwm->polarity = polarity;

	return 0;
}

static int tpu_pwm_enable(struct pwm_chip *chip, struct pwm_device *_pwm)
{
	struct tpu_pwm_device *pwm = pwm_get_chip_data(_pwm);
	int ret;

	ret = tpu_pwm_timer_start(pwm);
	if (ret < 0)
		return ret;

	/*
	 * To avoid running the timer when not strictly required, handle 0% and
	 * 100% duty cycles as fixed levels and stop the timer.
	 */
	if (pwm->duty == 0 || pwm->duty == pwm->period) {
		tpu_pwm_set_pin(pwm, pwm->duty ?
				TPU_PIN_ACTIVE : TPU_PIN_INACTIVE);
		tpu_pwm_timer_stop(pwm);
	}

	return 0;
}

static void tpu_pwm_disable(struct pwm_chip *chip, struct pwm_device *_pwm)
{
	struct tpu_pwm_device *pwm = pwm_get_chip_data(_pwm);

	/* The timer must be running to modify the pin output configuration. */
	tpu_pwm_timer_start(pwm);
	tpu_pwm_set_pin(pwm, TPU_PIN_INACTIVE);
	tpu_pwm_timer_stop(pwm);
}

static const struct pwm_ops tpu_pwm_ops = {
	.request = tpu_pwm_request,
	.free = tpu_pwm_free,
	.config = tpu_pwm_config,
	.set_polarity = tpu_pwm_set_polarity,
	.enable = tpu_pwm_enable,
	.disable = tpu_pwm_disable,
	.owner = THIS_MODULE,
};

/* -----------------------------------------------------------------------------
 * Probe and remove
 */

static int tpu_probe(struct platform_device *pdev)
{
	struct tpu_device *tpu;
	struct resource *res;
	int ret;

	tpu = devm_kzalloc(&pdev->dev, sizeof(*tpu), GFP_KERNEL);
	if (tpu == NULL)
		return -ENOMEM;

	spin_lock_init(&tpu->lock);
	tpu->pdev = pdev;

	/* Map memory, get clock and pin control. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tpu->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tpu->base))
		return PTR_ERR(tpu->base);

	tpu->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(tpu->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		return PTR_ERR(tpu->clk);
	}

	/* Initialize and register the device. */
	platform_set_drvdata(pdev, tpu);

	tpu->chip.dev = &pdev->dev;
	tpu->chip.ops = &tpu_pwm_ops;
	tpu->chip.of_xlate = of_pwm_xlate_with_flags;
	tpu->chip.of_pwm_n_cells = 3;
	tpu->chip.base = -1;
	tpu->chip.npwm = TPU_CHANNEL_MAX;

	pm_runtime_enable(&pdev->dev);

	ret = pwmchip_add(&tpu->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register PWM chip\n");
		pm_runtime_disable(&pdev->dev);
		return ret;
	}

	dev_info(&pdev->dev, "TPU PWM %d registered\n", tpu->pdev->id);

	return 0;
}

static int tpu_remove(struct platform_device *pdev)
{
	struct tpu_device *tpu = platform_get_drvdata(pdev);
	int ret;

	ret = pwmchip_remove(&tpu->chip);

	pm_runtime_disable(&pdev->dev);

	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id tpu_of_table[] = {
	{ .compatible = "renesas,tpu-r8a73a4", },
	{ .compatible = "renesas,tpu-r8a7740", },
	{ .compatible = "renesas,tpu-r8a7790", },
	{ .compatible = "renesas,tpu", },
	{ },
};

MODULE_DEVICE_TABLE(of, tpu_of_table);
#endif

static struct platform_driver tpu_driver = {
	.probe		= tpu_probe,
	.remove		= tpu_remove,
	.driver		= {
		.name	= "renesas-tpu-pwm",
		.of_match_table = of_match_ptr(tpu_of_table),
	}
};

module_platform_driver(tpu_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Renesas TPU PWM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:renesas-tpu-pwm");
