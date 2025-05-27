// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G2L General PWM Timer (GPT) driver
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 *
 * Hardware manual for this IP can be found here
 * https://www.renesas.com/eu/en/document/mah/rzg2l-group-rzg2lc-group-users-manual-hardware-0?language=en
 *
 * Limitations:
 * - Counter must be stopped before modifying Mode and Prescaler.
 * - When PWM is disabled, the output is driven to inactive.
 * - While the hardware supports both polarities, the driver (for now)
 *   only handles normal polarity.
 * - General PWM Timer (GPT) has 8 HW channels for PWM operations and
 *   each HW channel have 2 IOs.
 * - Each IO is modelled as an independent PWM channel.
 * - When both channels are used, disabling the channel on one stops the
 *   other.
 * - When both channels are used, the period of both IOs in the HW channel
 *   must be same (for now).
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/reset.h>
#include <linux/time.h>
#include <linux/units.h>

#define RZG2L_GET_CH(hwpwm)	((hwpwm) / 2)
#define RZG2L_GET_CH_OFFS(ch)	(0x100 * (ch))

#define RZG2L_GTCR(ch)		(0x2c + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTUDDTYC(ch)	(0x30 + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTIOR(ch)		(0x34 + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTBER(ch)		(0x40 + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTCNT(ch)		(0x48 + RZG2L_GET_CH_OFFS(ch))
#define RZG2L_GTCCR(ch, sub_ch)	(0x4c + RZG2L_GET_CH_OFFS(ch) + 4 * (sub_ch))
#define RZG2L_GTPR(ch)		(0x64 + RZG2L_GET_CH_OFFS(ch))

#define RZG2L_GTCR_CST		BIT(0)
#define RZG2L_GTCR_MD		GENMASK(18, 16)
#define RZG2L_GTCR_TPCS		GENMASK(26, 24)

#define RZG2L_GTCR_MD_SAW_WAVE_PWM_MODE	FIELD_PREP(RZG2L_GTCR_MD, 0)

#define RZG2L_GTUDDTYC_UP	BIT(0)
#define RZG2L_GTUDDTYC_UDF	BIT(1)
#define RZG2L_GTUDDTYC_UP_COUNTING	(RZG2L_GTUDDTYC_UP | RZG2L_GTUDDTYC_UDF)

#define RZG2L_GTIOR_GTIOA	GENMASK(4, 0)
#define RZG2L_GTIOR_GTIOB	GENMASK(20, 16)
#define RZG2L_GTIOR_GTIOx(sub_ch)	((sub_ch) ? RZG2L_GTIOR_GTIOB : RZG2L_GTIOR_GTIOA)
#define RZG2L_GTIOR_OAE		BIT(8)
#define RZG2L_GTIOR_OBE		BIT(24)
#define RZG2L_GTIOR_OxE(sub_ch)		((sub_ch) ? RZG2L_GTIOR_OBE : RZG2L_GTIOR_OAE)

#define RZG2L_INIT_OUT_HI_OUT_HI_END_TOGGLE	0x1b
#define RZG2L_GTIOR_GTIOA_OUT_HI_END_TOGGLE_CMP_MATCH \
	(RZG2L_INIT_OUT_HI_OUT_HI_END_TOGGLE | RZG2L_GTIOR_OAE)
#define RZG2L_GTIOR_GTIOB_OUT_HI_END_TOGGLE_CMP_MATCH \
	(FIELD_PREP(RZG2L_GTIOR_GTIOB, RZG2L_INIT_OUT_HI_OUT_HI_END_TOGGLE) | RZG2L_GTIOR_OBE)

#define RZG2L_GTIOR_GTIOx_OUT_HI_END_TOGGLE_CMP_MATCH(sub_ch) \
	((sub_ch) ? RZG2L_GTIOR_GTIOB_OUT_HI_END_TOGGLE_CMP_MATCH : \
	 RZG2L_GTIOR_GTIOA_OUT_HI_END_TOGGLE_CMP_MATCH)

#define RZG2L_MAX_HW_CHANNELS	8
#define RZG2L_CHANNELS_PER_IO	2
#define RZG2L_MAX_PWM_CHANNELS	(RZG2L_MAX_HW_CHANNELS * RZG2L_CHANNELS_PER_IO)
#define RZG2L_MAX_SCALE_FACTOR	1024
#define RZG2L_MAX_TICKS		((u64)U32_MAX * RZG2L_MAX_SCALE_FACTOR)

struct rzg2l_gpt_chip {
	void __iomem *mmio;
	struct mutex lock; /* lock to protect shared channel resources */
	unsigned long rate_khz;
	u32 period_ticks[RZG2L_MAX_HW_CHANNELS];
	u32 channel_request_count[RZG2L_MAX_HW_CHANNELS];
	u32 channel_enable_count[RZG2L_MAX_HW_CHANNELS];
};

static inline struct rzg2l_gpt_chip *to_rzg2l_gpt_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static inline unsigned int rzg2l_gpt_subchannel(unsigned int hwpwm)
{
	return hwpwm & 0x1;
}

static void rzg2l_gpt_write(struct rzg2l_gpt_chip *rzg2l_gpt, u32 reg, u32 data)
{
	writel(data, rzg2l_gpt->mmio + reg);
}

static u32 rzg2l_gpt_read(struct rzg2l_gpt_chip *rzg2l_gpt, u32 reg)
{
	return readl(rzg2l_gpt->mmio + reg);
}

static void rzg2l_gpt_modify(struct rzg2l_gpt_chip *rzg2l_gpt, u32 reg, u32 clr,
			     u32 set)
{
	rzg2l_gpt_write(rzg2l_gpt, reg,
			(rzg2l_gpt_read(rzg2l_gpt, reg) & ~clr) | set);
}

static u8 rzg2l_gpt_calculate_prescale(struct rzg2l_gpt_chip *rzg2l_gpt,
				       u64 period_ticks)
{
	u32 prescaled_period_ticks;
	u8 prescale;

	prescaled_period_ticks = period_ticks >> 32;
	if (prescaled_period_ticks >= 256)
		prescale = 5;
	else
		prescale = (fls(prescaled_period_ticks) + 1) / 2;

	return prescale;
}

static int rzg2l_gpt_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
	u32 ch = RZG2L_GET_CH(pwm->hwpwm);

	guard(mutex)(&rzg2l_gpt->lock);
	rzg2l_gpt->channel_request_count[ch]++;

	return 0;
}

static void rzg2l_gpt_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
	u32 ch = RZG2L_GET_CH(pwm->hwpwm);

	guard(mutex)(&rzg2l_gpt->lock);
	rzg2l_gpt->channel_request_count[ch]--;
}

static bool rzg2l_gpt_is_ch_enabled(struct rzg2l_gpt_chip *rzg2l_gpt, u8 hwpwm)
{
	u8 ch = RZG2L_GET_CH(hwpwm);
	u32 val;

	val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCR(ch));
	if (!(val & RZG2L_GTCR_CST))
		return false;

	val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTIOR(ch));

	return val & RZG2L_GTIOR_OxE(rzg2l_gpt_subchannel(hwpwm));
}

/* Caller holds the lock while calling rzg2l_gpt_enable() */
static void rzg2l_gpt_enable(struct rzg2l_gpt_chip *rzg2l_gpt,
			     struct pwm_device *pwm)
{
	u8 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);
	u32 val = RZG2L_GTIOR_GTIOx(sub_ch) | RZG2L_GTIOR_OxE(sub_ch);
	u8 ch = RZG2L_GET_CH(pwm->hwpwm);

	/* Enable pin output */
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTIOR(ch), val,
			 RZG2L_GTIOR_GTIOx_OUT_HI_END_TOGGLE_CMP_MATCH(sub_ch));

	if (!rzg2l_gpt->channel_enable_count[ch])
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), 0, RZG2L_GTCR_CST);

	rzg2l_gpt->channel_enable_count[ch]++;
}

/* Caller holds the lock while calling rzg2l_gpt_disable() */
static void rzg2l_gpt_disable(struct rzg2l_gpt_chip *rzg2l_gpt,
			      struct pwm_device *pwm)
{
	u8 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);
	u8 ch = RZG2L_GET_CH(pwm->hwpwm);

	/* Stop count, Output low on GTIOCx pin when counting stops */
	rzg2l_gpt->channel_enable_count[ch]--;

	if (!rzg2l_gpt->channel_enable_count[ch])
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST, 0);

	/* Disable pin output */
	rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTIOR(ch), RZG2L_GTIOR_OxE(sub_ch), 0);
}

static u64 rzg2l_gpt_calculate_period_or_duty(struct rzg2l_gpt_chip *rzg2l_gpt,
					      u32 val, u8 prescale)
{
	u64 tmp;

	/*
	 * The calculation doesn't overflow an u64 because prescale ≤ 5 and so
	 * tmp = val << (2 * prescale) * USEC_PER_SEC
	 *     < 2^32 * 2^10 * 10^6
	 *     < 2^32 * 2^10 * 2^20
	 *     = 2^62
	 */
	tmp = (u64)val << (2 * prescale);
	tmp *= USEC_PER_SEC;

	return DIV64_U64_ROUND_UP(tmp, rzg2l_gpt->rate_khz);
}

static int rzg2l_gpt_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			       struct pwm_state *state)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);

	state->enabled = rzg2l_gpt_is_ch_enabled(rzg2l_gpt, pwm->hwpwm);
	if (state->enabled) {
		u32 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);
		u32 ch = RZG2L_GET_CH(pwm->hwpwm);
		u8 prescale;
		u32 val;

		val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCR(ch));
		prescale = FIELD_GET(RZG2L_GTCR_TPCS, val);

		val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTPR(ch));
		state->period = rzg2l_gpt_calculate_period_or_duty(rzg2l_gpt, val, prescale);

		val = rzg2l_gpt_read(rzg2l_gpt, RZG2L_GTCCR(ch, sub_ch));
		state->duty_cycle = rzg2l_gpt_calculate_period_or_duty(rzg2l_gpt, val, prescale);
		if (state->duty_cycle > state->period)
			state->duty_cycle = state->period;
	}

	state->polarity = PWM_POLARITY_NORMAL;

	return 0;
}

static u32 rzg2l_gpt_calculate_pv_or_dc(u64 period_or_duty_cycle, u8 prescale)
{
	return min_t(u64, DIV_ROUND_DOWN_ULL(period_or_duty_cycle, 1 << (2 * prescale)),
		     U32_MAX);
}

/* Caller holds the lock while calling rzg2l_gpt_config() */
static int rzg2l_gpt_config(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
	u8 sub_ch = rzg2l_gpt_subchannel(pwm->hwpwm);
	u8 ch = RZG2L_GET_CH(pwm->hwpwm);
	u64 period_ticks, duty_ticks;
	unsigned long pv, dc;
	u8 prescale;

	/* Limit period/duty cycle to max value supported by the HW */
	period_ticks = mul_u64_u64_div_u64(state->period, rzg2l_gpt->rate_khz, USEC_PER_SEC);
	if (period_ticks > RZG2L_MAX_TICKS)
		period_ticks = RZG2L_MAX_TICKS;
	/*
	 * GPT counter is shared by the two IOs of a single channel, so
	 * prescale and period can NOT be modified when there are multiple IOs
	 * in use with different settings.
	 */
	if (rzg2l_gpt->channel_request_count[ch] > 1) {
		if (period_ticks < rzg2l_gpt->period_ticks[ch])
			return -EBUSY;
		else
			period_ticks = rzg2l_gpt->period_ticks[ch];
	}

	prescale = rzg2l_gpt_calculate_prescale(rzg2l_gpt, period_ticks);
	pv = rzg2l_gpt_calculate_pv_or_dc(period_ticks, prescale);

	duty_ticks = mul_u64_u64_div_u64(state->duty_cycle, rzg2l_gpt->rate_khz, USEC_PER_SEC);
	if (duty_ticks > period_ticks)
		duty_ticks = period_ticks;
	dc = rzg2l_gpt_calculate_pv_or_dc(duty_ticks, prescale);

	/*
	 * GPT counter is shared by multiple channels, we cache the period ticks
	 * from the first enabled channel and use the same value for both
	 * channels.
	 */
	rzg2l_gpt->period_ticks[ch] = period_ticks;

	/*
	 * Counter must be stopped before modifying mode, prescaler, timer
	 * counter and buffer enable registers. These registers are shared
	 * between both channels. So allow updating these registers only for the
	 * first enabled channel.
	 */
	if (rzg2l_gpt->channel_enable_count[ch] <= 1) {
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_CST, 0);

		/* GPT set operating mode (saw-wave up-counting) */
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_MD,
				 RZG2L_GTCR_MD_SAW_WAVE_PWM_MODE);

		/* Set count direction */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTUDDTYC(ch), RZG2L_GTUDDTYC_UP_COUNTING);

		/* Select count clock */
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch), RZG2L_GTCR_TPCS,
				 FIELD_PREP(RZG2L_GTCR_TPCS, prescale));

		/* Set period */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTPR(ch), pv);
	}

	/* Set duty cycle */
	rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCCR(ch, sub_ch), dc);

	if (rzg2l_gpt->channel_enable_count[ch] <= 1) {
		/* Set initial value for counter */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTCNT(ch), 0);

		/* Set no buffer operation */
		rzg2l_gpt_write(rzg2l_gpt, RZG2L_GTBER(ch), 0);

		/* Restart the counter after updating the registers */
		rzg2l_gpt_modify(rzg2l_gpt, RZG2L_GTCR(ch),
				 RZG2L_GTCR_CST, RZG2L_GTCR_CST);
	}

	return 0;
}

static int rzg2l_gpt_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   const struct pwm_state *state)
{
	struct rzg2l_gpt_chip *rzg2l_gpt = to_rzg2l_gpt_chip(chip);
	bool enabled = pwm->state.enabled;
	int ret;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	guard(mutex)(&rzg2l_gpt->lock);
	if (!state->enabled) {
		if (enabled)
			rzg2l_gpt_disable(rzg2l_gpt, pwm);

		return 0;
	}

	ret = rzg2l_gpt_config(chip, pwm, state);
	if (!ret && !enabled)
		rzg2l_gpt_enable(rzg2l_gpt, pwm);

	return ret;
}

static const struct pwm_ops rzg2l_gpt_ops = {
	.request = rzg2l_gpt_request,
	.free = rzg2l_gpt_free,
	.get_state = rzg2l_gpt_get_state,
	.apply = rzg2l_gpt_apply,
};

static int rzg2l_gpt_probe(struct platform_device *pdev)
{
	struct rzg2l_gpt_chip *rzg2l_gpt;
	struct device *dev = &pdev->dev;
	struct reset_control *rstc;
	struct pwm_chip *chip;
	unsigned long rate;
	struct clk *clk;
	int ret;

	chip = devm_pwmchip_alloc(dev, RZG2L_MAX_PWM_CHANNELS, sizeof(*rzg2l_gpt));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	rzg2l_gpt = to_rzg2l_gpt_chip(chip);

	rzg2l_gpt->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rzg2l_gpt->mmio))
		return PTR_ERR(rzg2l_gpt->mmio);

	rstc = devm_reset_control_get_exclusive_deasserted(dev, NULL);
	if (IS_ERR(rstc))
		return dev_err_probe(dev, PTR_ERR(rstc), "Cannot deassert reset control\n");

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "Cannot get clock\n");

	ret = devm_clk_rate_exclusive_get(dev, clk);
	if (ret)
		return ret;

	rate = clk_get_rate(clk);
	if (!rate)
		return dev_err_probe(dev, -EINVAL, "The gpt clk rate is 0");

	/*
	 * Refuse clk rates > 1 GHz to prevent overflow later for computing
	 * period and duty cycle.
	 */
	if (rate > NSEC_PER_SEC)
		return dev_err_probe(dev, -EINVAL, "The gpt clk rate is > 1GHz");

	/*
	 * Rate is in MHz and is always integer for peripheral clk
	 * 2^32 * 2^10 (prescalar) * 10^6 (rate_khz) < 2^64
	 * So make sure rate is multiple of 1000.
	 */
	rzg2l_gpt->rate_khz = rate / KILO;
	if (rzg2l_gpt->rate_khz * KILO != rate)
		return dev_err_probe(dev, -EINVAL, "Rate is not multiple of 1000");

	mutex_init(&rzg2l_gpt->lock);

	chip->ops = &rzg2l_gpt_ops;
	ret = devm_pwmchip_add(dev, chip);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add PWM chip\n");

	return 0;
}

static const struct of_device_id rzg2l_gpt_of_table[] = {
	{ .compatible = "renesas,rzg2l-gpt", },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzg2l_gpt_of_table);

static struct platform_driver rzg2l_gpt_driver = {
	.driver = {
		.name = "pwm-rzg2l-gpt",
		.of_match_table = rzg2l_gpt_of_table,
	},
	.probe = rzg2l_gpt_probe,
};
module_platform_driver(rzg2l_gpt_driver);

MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G2L General PWM Timer (GPT) Driver");
MODULE_LICENSE("GPL");
