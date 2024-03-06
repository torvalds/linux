// SPDX-License-Identifier: GPL-2.0
/*
 * R-Mobile TPU PWM driver
 *
 * Copyright (C) 2012 Renesas Solutions Corp.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/module.h>
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
	struct tpu_pwm_device tpd[TPU_CHANNEL_MAX];
};

#define to_tpu_device(c)	container_of(c, struct tpu_device, chip)

static void tpu_pwm_write(struct tpu_pwm_device *tpd, int reg_nr, u16 value)
{
	void __iomem *base = tpd->tpu->base + TPU_CHANNEL_OFFSET
			   + tpd->channel * TPU_CHANNEL_SIZE;

	iowrite16(value, base + reg_nr);
}

static void tpu_pwm_set_pin(struct tpu_pwm_device *tpd,
			    enum tpu_pin_state state)
{
	static const char * const states[] = { "inactive", "PWM", "active" };

	dev_dbg(&tpd->tpu->pdev->dev, "%u: configuring pin as %s\n",
		tpd->channel, states[state]);

	switch (state) {
	case TPU_PIN_INACTIVE:
		tpu_pwm_write(tpd, TPU_TIORn,
			      tpd->polarity == PWM_POLARITY_INVERSED ?
			      TPU_TIOR_IOA_1 : TPU_TIOR_IOA_0);
		break;
	case TPU_PIN_PWM:
		tpu_pwm_write(tpd, TPU_TIORn,
			      tpd->polarity == PWM_POLARITY_INVERSED ?
			      TPU_TIOR_IOA_0_SET : TPU_TIOR_IOA_1_CLR);
		break;
	case TPU_PIN_ACTIVE:
		tpu_pwm_write(tpd, TPU_TIORn,
			      tpd->polarity == PWM_POLARITY_INVERSED ?
			      TPU_TIOR_IOA_0 : TPU_TIOR_IOA_1);
		break;
	}
}

static void tpu_pwm_start_stop(struct tpu_pwm_device *tpd, int start)
{
	unsigned long flags;
	u16 value;

	spin_lock_irqsave(&tpd->tpu->lock, flags);
	value = ioread16(tpd->tpu->base + TPU_TSTR);

	if (start)
		value |= 1 << tpd->channel;
	else
		value &= ~(1 << tpd->channel);

	iowrite16(value, tpd->tpu->base + TPU_TSTR);
	spin_unlock_irqrestore(&tpd->tpu->lock, flags);
}

static int tpu_pwm_timer_start(struct tpu_pwm_device *tpd)
{
	int ret;

	if (!tpd->timer_on) {
		/* Wake up device and enable clock. */
		pm_runtime_get_sync(&tpd->tpu->pdev->dev);
		ret = clk_prepare_enable(tpd->tpu->clk);
		if (ret) {
			dev_err(&tpd->tpu->pdev->dev, "cannot enable clock\n");
			return ret;
		}
		tpd->timer_on = true;
	}

	/*
	 * Make sure the channel is stopped, as we need to reconfigure it
	 * completely. First drive the pin to the inactive state to avoid
	 * glitches.
	 */
	tpu_pwm_set_pin(tpd, TPU_PIN_INACTIVE);
	tpu_pwm_start_stop(tpd, false);

	/*
	 * - Clear TCNT on TGRB match
	 * - Count on rising edge
	 * - Set prescaler
	 * - Output 0 until TGRA, output 1 until TGRB (active low polarity)
	 * - Output 1 until TGRA, output 0 until TGRB (active high polarity
	 * - PWM mode
	 */
	tpu_pwm_write(tpd, TPU_TCRn, TPU_TCR_CCLR_TGRB | TPU_TCR_CKEG_RISING |
		      tpd->prescaler);
	tpu_pwm_write(tpd, TPU_TMDRn, TPU_TMDR_MD_PWM);
	tpu_pwm_set_pin(tpd, TPU_PIN_PWM);
	tpu_pwm_write(tpd, TPU_TGRAn, tpd->duty);
	tpu_pwm_write(tpd, TPU_TGRBn, tpd->period);

	dev_dbg(&tpd->tpu->pdev->dev, "%u: TGRA 0x%04x TGRB 0x%04x\n",
		tpd->channel, tpd->duty, tpd->period);

	/* Start the channel. */
	tpu_pwm_start_stop(tpd, true);

	return 0;
}

static void tpu_pwm_timer_stop(struct tpu_pwm_device *tpd)
{
	if (!tpd->timer_on)
		return;

	/* Disable channel. */
	tpu_pwm_start_stop(tpd, false);

	/* Stop clock and mark device as idle. */
	clk_disable_unprepare(tpd->tpu->clk);
	pm_runtime_put(&tpd->tpu->pdev->dev);

	tpd->timer_on = false;
}

/* -----------------------------------------------------------------------------
 * PWM API
 */

static int tpu_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct tpu_device *tpu = to_tpu_device(chip);
	struct tpu_pwm_device *tpd;

	if (pwm->hwpwm >= TPU_CHANNEL_MAX)
		return -EINVAL;

	tpd = &tpu->tpd[pwm->hwpwm];

	tpd->tpu = tpu;
	tpd->channel = pwm->hwpwm;
	tpd->polarity = PWM_POLARITY_NORMAL;
	tpd->prescaler = 0;
	tpd->period = 0;
	tpd->duty = 0;

	tpd->timer_on = false;

	return 0;
}

static void tpu_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct tpu_device *tpu = to_tpu_device(chip);
	struct tpu_pwm_device *tpd = &tpu->tpd[pwm->hwpwm];

	tpu_pwm_timer_stop(tpd);
}

static int tpu_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			  u64 duty_ns, u64 period_ns, bool enabled)
{
	struct tpu_device *tpu = to_tpu_device(chip);
	struct tpu_pwm_device *tpd = &tpu->tpd[pwm->hwpwm];
	unsigned int prescaler;
	bool duty_only = false;
	u32 clk_rate;
	u64 period;
	u32 duty;
	int ret;

	clk_rate = clk_get_rate(tpu->clk);
	if (unlikely(clk_rate > NSEC_PER_SEC)) {
		/*
		 * This won't happen in the nearer future, so this is only a
		 * safeguard to prevent the following calculation from
		 * overflowing. With this clk_rate * period_ns / NSEC_PER_SEC is
		 * not greater than period_ns and so fits into an u64.
		 */
		return -EINVAL;
	}

	period = mul_u64_u64_div_u64(clk_rate, period_ns, NSEC_PER_SEC);

	/*
	 * Find the minimal prescaler in [0..3] such that
	 *
	 *     period >> (2 * prescaler) < 0x10000
	 *
	 * This could be calculated using something like:
	 *
	 *     prescaler = max(ilog2(period) / 2, 7) - 7;
	 *
	 * but given there are only four allowed results and that ilog2 isn't
	 * cheap on all platforms using a switch statement is more effective.
	 */
	switch (period) {
	case 1 ... 0xffff:
		prescaler = 0;
		break;

	case 0x10000 ... 0x3ffff:
		prescaler = 1;
		break;

	case 0x40000 ... 0xfffff:
		prescaler = 2;
		break;

	case 0x100000 ... 0x3fffff:
		prescaler = 3;
		break;

	default:
		return -EINVAL;
	}

	period >>= 2 * prescaler;

	if (duty_ns)
		duty = mul_u64_u64_div_u64(clk_rate, duty_ns,
					   (u64)NSEC_PER_SEC << (2 * prescaler));
	else
		duty = 0;

	dev_dbg(&tpu->pdev->dev,
		"rate %u, prescaler %u, period %u, duty %u\n",
		clk_rate, 1 << (2 * prescaler), (u32)period, duty);

	if (tpd->prescaler == prescaler && tpd->period == period)
		duty_only = true;

	tpd->prescaler = prescaler;
	tpd->period = period;
	tpd->duty = duty;

	/* If the channel is disabled we're done. */
	if (!enabled)
		return 0;

	if (duty_only && tpd->timer_on) {
		/*
		 * If only the duty cycle changed and the timer is already
		 * running, there's no need to reconfigure it completely, Just
		 * modify the duty cycle.
		 */
		tpu_pwm_write(tpd, TPU_TGRAn, tpd->duty);
		dev_dbg(&tpu->pdev->dev, "%u: TGRA 0x%04x\n", tpd->channel,
			tpd->duty);
	} else {
		/* Otherwise perform a full reconfiguration. */
		ret = tpu_pwm_timer_start(tpd);
		if (ret < 0)
			return ret;
	}

	if (duty == 0 || duty == period) {
		/*
		 * To avoid running the timer when not strictly required, handle
		 * 0% and 100% duty cycles as fixed levels and stop the timer.
		 */
		tpu_pwm_set_pin(tpd, duty ? TPU_PIN_ACTIVE : TPU_PIN_INACTIVE);
		tpu_pwm_timer_stop(tpd);
	}

	return 0;
}

static int tpu_pwm_set_polarity(struct pwm_chip *chip, struct pwm_device *pwm,
				enum pwm_polarity polarity)
{
	struct tpu_device *tpu = to_tpu_device(chip);
	struct tpu_pwm_device *tpd = &tpu->tpd[pwm->hwpwm];

	tpd->polarity = polarity;

	return 0;
}

static int tpu_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct tpu_device *tpu = to_tpu_device(chip);
	struct tpu_pwm_device *tpd = &tpu->tpd[pwm->hwpwm];
	int ret;

	ret = tpu_pwm_timer_start(tpd);
	if (ret < 0)
		return ret;

	/*
	 * To avoid running the timer when not strictly required, handle 0% and
	 * 100% duty cycles as fixed levels and stop the timer.
	 */
	if (tpd->duty == 0 || tpd->duty == tpd->period) {
		tpu_pwm_set_pin(tpd, tpd->duty ?
				TPU_PIN_ACTIVE : TPU_PIN_INACTIVE);
		tpu_pwm_timer_stop(tpd);
	}

	return 0;
}

static void tpu_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct tpu_device *tpu = to_tpu_device(chip);
	struct tpu_pwm_device *tpd = &tpu->tpd[pwm->hwpwm];

	/* The timer must be running to modify the pin output configuration. */
	tpu_pwm_timer_start(tpd);
	tpu_pwm_set_pin(tpd, TPU_PIN_INACTIVE);
	tpu_pwm_timer_stop(tpd);
}

static int tpu_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	int err;
	bool enabled = pwm->state.enabled;

	if (state->polarity != pwm->state.polarity) {
		if (enabled) {
			tpu_pwm_disable(chip, pwm);
			enabled = false;
		}

		err = tpu_pwm_set_polarity(chip, pwm, state->polarity);
		if (err)
			return err;
	}

	if (!state->enabled) {
		if (enabled)
			tpu_pwm_disable(chip, pwm);

		return 0;
	}

	err = tpu_pwm_config(chip, pwm,
			     state->duty_cycle, state->period, enabled);
	if (err)
		return err;

	if (!enabled)
		err = tpu_pwm_enable(chip, pwm);

	return err;
}

static const struct pwm_ops tpu_pwm_ops = {
	.request = tpu_pwm_request,
	.free = tpu_pwm_free,
	.apply = tpu_pwm_apply,
};

/* -----------------------------------------------------------------------------
 * Probe and remove
 */

static int tpu_probe(struct platform_device *pdev)
{
	struct tpu_device *tpu;
	int ret;

	tpu = devm_kzalloc(&pdev->dev, sizeof(*tpu), GFP_KERNEL);
	if (tpu == NULL)
		return -ENOMEM;

	spin_lock_init(&tpu->lock);
	tpu->pdev = pdev;

	/* Map memory, get clock and pin control. */
	tpu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tpu->base))
		return PTR_ERR(tpu->base);

	tpu->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(tpu->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(tpu->clk), "Failed to get clock\n");

	/* Initialize and register the device. */
	platform_set_drvdata(pdev, tpu);

	tpu->chip.dev = &pdev->dev;
	tpu->chip.ops = &tpu_pwm_ops;
	tpu->chip.npwm = TPU_CHANNEL_MAX;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "Failed to enable runtime PM\n");

	ret = devm_pwmchip_add(&pdev->dev, &tpu->chip);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "Failed to register PWM chip\n");

	return 0;
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
