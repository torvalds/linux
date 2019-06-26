// SPDX-License-Identifier: GPL-2.0
/*
 * simple driver for PWM (Pulse Width Modulator) controller
 *
 * Derived from pxa PWM driver by eric miao <eric.miao@marvell.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#define MX3_PWMCR			0x00    /* PWM Control Register */
#define MX3_PWMSR			0x04    /* PWM Status Register */
#define MX3_PWMSAR			0x0C    /* PWM Sample Register */
#define MX3_PWMPR			0x10    /* PWM Period Register */

#define MX3_PWMCR_FWM			GENMASK(27, 26)
#define MX3_PWMCR_STOPEN		BIT(25)
#define MX3_PWMCR_DOZEN			BIT(24)
#define MX3_PWMCR_WAITEN		BIT(23)
#define MX3_PWMCR_DBGEN			BIT(22)
#define MX3_PWMCR_BCTR			BIT(21)
#define MX3_PWMCR_HCTR			BIT(20)

#define MX3_PWMCR_POUTC			GENMASK(19, 18)
#define MX3_PWMCR_POUTC_NORMAL		0
#define MX3_PWMCR_POUTC_INVERTED	1
#define MX3_PWMCR_POUTC_OFF		2

#define MX3_PWMCR_CLKSRC		GENMASK(17, 16)
#define MX3_PWMCR_CLKSRC_OFF		0
#define MX3_PWMCR_CLKSRC_IPG		1
#define MX3_PWMCR_CLKSRC_IPG_HIGH	2
#define MX3_PWMCR_CLKSRC_IPG_32K	3

#define MX3_PWMCR_PRESCALER		GENMASK(15, 4)

#define MX3_PWMCR_SWR			BIT(3)

#define MX3_PWMCR_REPEAT		GENMASK(2, 1)
#define MX3_PWMCR_REPEAT_1X		0
#define MX3_PWMCR_REPEAT_2X		1
#define MX3_PWMCR_REPEAT_4X		2
#define MX3_PWMCR_REPEAT_8X		3

#define MX3_PWMCR_EN			BIT(0)

#define MX3_PWMSR_FWE			BIT(6)
#define MX3_PWMSR_CMP			BIT(5)
#define MX3_PWMSR_ROV			BIT(4)
#define MX3_PWMSR_FE			BIT(3)

#define MX3_PWMSR_FIFOAV		GENMASK(2, 0)
#define MX3_PWMSR_FIFOAV_EMPTY		0
#define MX3_PWMSR_FIFOAV_1WORD		1
#define MX3_PWMSR_FIFOAV_2WORDS		2
#define MX3_PWMSR_FIFOAV_3WORDS		3
#define MX3_PWMSR_FIFOAV_4WORDS		4

#define MX3_PWMCR_PRESCALER_SET(x)	FIELD_PREP(MX3_PWMCR_PRESCALER, (x) - 1)
#define MX3_PWMCR_PRESCALER_GET(x)	(FIELD_GET(MX3_PWMCR_PRESCALER, \
						   (x)) + 1)

#define MX3_PWM_SWR_LOOP		5

/* PWMPR register value of 0xffff has the same effect as 0xfffe */
#define MX3_PWMPR_MAX			0xfffe

struct pwm_imx27_chip {
	struct clk	*clk_ipg;
	struct clk	*clk_per;
	void __iomem	*mmio_base;
	struct pwm_chip	chip;
};

#define to_pwm_imx27_chip(chip)	container_of(chip, struct pwm_imx27_chip, chip)

static int pwm_imx27_clk_prepare_enable(struct pwm_chip *chip)
{
	struct pwm_imx27_chip *imx = to_pwm_imx27_chip(chip);
	int ret;

	ret = clk_prepare_enable(imx->clk_ipg);
	if (ret)
		return ret;

	ret = clk_prepare_enable(imx->clk_per);
	if (ret) {
		clk_disable_unprepare(imx->clk_ipg);
		return ret;
	}

	return 0;
}

static void pwm_imx27_clk_disable_unprepare(struct pwm_chip *chip)
{
	struct pwm_imx27_chip *imx = to_pwm_imx27_chip(chip);

	clk_disable_unprepare(imx->clk_per);
	clk_disable_unprepare(imx->clk_ipg);
}

static void pwm_imx27_get_state(struct pwm_chip *chip,
				struct pwm_device *pwm, struct pwm_state *state)
{
	struct pwm_imx27_chip *imx = to_pwm_imx27_chip(chip);
	u32 period, prescaler, pwm_clk, val;
	u64 tmp;
	int ret;

	ret = pwm_imx27_clk_prepare_enable(chip);
	if (ret < 0)
		return;

	val = readl(imx->mmio_base + MX3_PWMCR);

	if (val & MX3_PWMCR_EN)
		state->enabled = true;
	else
		state->enabled = false;

	switch (FIELD_GET(MX3_PWMCR_POUTC, val)) {
	case MX3_PWMCR_POUTC_NORMAL:
		state->polarity = PWM_POLARITY_NORMAL;
		break;
	case MX3_PWMCR_POUTC_INVERTED:
		state->polarity = PWM_POLARITY_INVERSED;
		break;
	default:
		dev_warn(chip->dev, "can't set polarity, output disconnected");
	}

	prescaler = MX3_PWMCR_PRESCALER_GET(val);
	pwm_clk = clk_get_rate(imx->clk_per);
	pwm_clk = DIV_ROUND_CLOSEST_ULL(pwm_clk, prescaler);
	val = readl(imx->mmio_base + MX3_PWMPR);
	period = val >= MX3_PWMPR_MAX ? MX3_PWMPR_MAX : val;

	/* PWMOUT (Hz) = PWMCLK / (PWMPR + 2) */
	tmp = NSEC_PER_SEC * (u64)(period + 2);
	state->period = DIV_ROUND_CLOSEST_ULL(tmp, pwm_clk);

	/* PWMSAR can be read only if PWM is enabled */
	if (state->enabled) {
		val = readl(imx->mmio_base + MX3_PWMSAR);
		tmp = NSEC_PER_SEC * (u64)(val);
		state->duty_cycle = DIV_ROUND_CLOSEST_ULL(tmp, pwm_clk);
	} else {
		state->duty_cycle = 0;
	}

	if (!state->enabled)
		pwm_imx27_clk_disable_unprepare(chip);
}

static void pwm_imx27_sw_reset(struct pwm_chip *chip)
{
	struct pwm_imx27_chip *imx = to_pwm_imx27_chip(chip);
	struct device *dev = chip->dev;
	int wait_count = 0;
	u32 cr;

	writel(MX3_PWMCR_SWR, imx->mmio_base + MX3_PWMCR);
	do {
		usleep_range(200, 1000);
		cr = readl(imx->mmio_base + MX3_PWMCR);
	} while ((cr & MX3_PWMCR_SWR) &&
		 (wait_count++ < MX3_PWM_SWR_LOOP));

	if (cr & MX3_PWMCR_SWR)
		dev_warn(dev, "software reset timeout\n");
}

static void pwm_imx27_wait_fifo_slot(struct pwm_chip *chip,
				     struct pwm_device *pwm)
{
	struct pwm_imx27_chip *imx = to_pwm_imx27_chip(chip);
	struct device *dev = chip->dev;
	unsigned int period_ms;
	int fifoav;
	u32 sr;

	sr = readl(imx->mmio_base + MX3_PWMSR);
	fifoav = FIELD_GET(MX3_PWMSR_FIFOAV, sr);
	if (fifoav == MX3_PWMSR_FIFOAV_4WORDS) {
		period_ms = DIV_ROUND_UP(pwm_get_period(pwm),
					 NSEC_PER_MSEC);
		msleep(period_ms);

		sr = readl(imx->mmio_base + MX3_PWMSR);
		if (fifoav == FIELD_GET(MX3_PWMSR_FIFOAV, sr))
			dev_warn(dev, "there is no free FIFO slot\n");
	}
}

static int pwm_imx27_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   struct pwm_state *state)
{
	unsigned long period_cycles, duty_cycles, prescale;
	struct pwm_imx27_chip *imx = to_pwm_imx27_chip(chip);
	struct pwm_state cstate;
	unsigned long long c;
	int ret;
	u32 cr;

	pwm_get_state(pwm, &cstate);

	if (state->enabled) {
		c = clk_get_rate(imx->clk_per);
		c *= state->period;

		do_div(c, 1000000000);
		period_cycles = c;

		prescale = period_cycles / 0x10000 + 1;

		period_cycles /= prescale;
		c = (unsigned long long)period_cycles * state->duty_cycle;
		do_div(c, state->period);
		duty_cycles = c;

		/*
		 * according to imx pwm RM, the real period value should be
		 * PERIOD value in PWMPR plus 2.
		 */
		if (period_cycles > 2)
			period_cycles -= 2;
		else
			period_cycles = 0;

		/*
		 * Wait for a free FIFO slot if the PWM is already enabled, and
		 * flush the FIFO if the PWM was disabled and is about to be
		 * enabled.
		 */
		if (cstate.enabled) {
			pwm_imx27_wait_fifo_slot(chip, pwm);
		} else {
			ret = pwm_imx27_clk_prepare_enable(chip);
			if (ret)
				return ret;

			pwm_imx27_sw_reset(chip);
		}

		writel(duty_cycles, imx->mmio_base + MX3_PWMSAR);
		writel(period_cycles, imx->mmio_base + MX3_PWMPR);

		cr = MX3_PWMCR_PRESCALER_SET(prescale) |
		     MX3_PWMCR_STOPEN | MX3_PWMCR_DOZEN | MX3_PWMCR_WAITEN |
		     FIELD_PREP(MX3_PWMCR_CLKSRC, MX3_PWMCR_CLKSRC_IPG_HIGH) |
		     MX3_PWMCR_DBGEN | MX3_PWMCR_EN;

		if (state->polarity == PWM_POLARITY_INVERSED)
			cr |= FIELD_PREP(MX3_PWMCR_POUTC,
					MX3_PWMCR_POUTC_INVERTED);

		writel(cr, imx->mmio_base + MX3_PWMCR);
	} else if (cstate.enabled) {
		writel(0, imx->mmio_base + MX3_PWMCR);

		pwm_imx27_clk_disable_unprepare(chip);
	}

	return 0;
}

static const struct pwm_ops pwm_imx27_ops = {
	.apply = pwm_imx27_apply,
	.get_state = pwm_imx27_get_state,
	.owner = THIS_MODULE,
};

static const struct of_device_id pwm_imx27_dt_ids[] = {
	{ .compatible = "fsl,imx27-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pwm_imx27_dt_ids);

static int pwm_imx27_probe(struct platform_device *pdev)
{
	struct pwm_imx27_chip *imx;

	imx = devm_kzalloc(&pdev->dev, sizeof(*imx), GFP_KERNEL);
	if (imx == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, imx);

	imx->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(imx->clk_ipg)) {
		dev_err(&pdev->dev, "getting ipg clock failed with %ld\n",
				PTR_ERR(imx->clk_ipg));
		return PTR_ERR(imx->clk_ipg);
	}

	imx->clk_per = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(imx->clk_per)) {
		int ret = PTR_ERR(imx->clk_per);

		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"failed to get peripheral clock: %d\n",
				ret);

		return ret;
	}

	imx->chip.ops = &pwm_imx27_ops;
	imx->chip.dev = &pdev->dev;
	imx->chip.base = -1;
	imx->chip.npwm = 1;

	imx->chip.of_xlate = of_pwm_xlate_with_flags;
	imx->chip.of_pwm_n_cells = 3;

	imx->mmio_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(imx->mmio_base))
		return PTR_ERR(imx->mmio_base);

	return pwmchip_add(&imx->chip);
}

static int pwm_imx27_remove(struct platform_device *pdev)
{
	struct pwm_imx27_chip *imx;

	imx = platform_get_drvdata(pdev);

	pwm_imx27_clk_disable_unprepare(&imx->chip);

	return pwmchip_remove(&imx->chip);
}

static struct platform_driver imx_pwm_driver = {
	.driver = {
		.name = "pwm-imx27",
		.of_match_table = pwm_imx27_dt_ids,
	},
	.probe = pwm_imx27_probe,
	.remove = pwm_imx27_remove,
};
module_platform_driver(imx_pwm_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
