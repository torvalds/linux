// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * PWM controller driver for Amlogic Meson SoCs.
 *
 * This PWM is only a set of Gates, Dividers and Counters:
 * PWM output is achieved by calculating a clock that permits calculating
 * two periods (low and high). The counter then has to be set to switch after
 * N cycles for the first half period.
 * The hardware has no "polarity" setting. This driver reverses the period
 * cycles (the low length is inverted with the high length) for
 * PWM_POLARITY_INVERSED. This means that .get_state cannot read the polarity
 * from the hardware.
 * Setting the duty cycle will disable and re-enable the PWM output.
 * Disabling the PWM stops the output immediately (without waiting for the
 * current period to complete first).
 *
 * The public S912 (GXM) datasheet contains some documentation for this PWM
 * controller starting on page 543:
 * https://dl.khadas.com/Hardware/VIM2/Datasheet/S912_Datasheet_V0.220170314publicversion-Wesion.pdf
 * An updated version of this IP block is found in S922X (G12B) SoCs. The
 * datasheet contains the description for this IP block revision starting at
 * page 1084:
 * https://dn.odroid.com/S922X/ODROID-N2/Datasheet/S922X_Public_Datasheet_V0.2.pdf
 *
 * Copyright (c) 2016 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2014 Amlogic, Inc.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define REG_PWM_A		0x0
#define REG_PWM_B		0x4
#define PWM_LOW_MASK		GENMASK(15, 0)
#define PWM_HIGH_MASK		GENMASK(31, 16)

#define REG_MISC_AB		0x8
#define MISC_B_CLK_EN_SHIFT	23
#define MISC_A_CLK_EN_SHIFT	15
#define MISC_CLK_DIV_WIDTH	7
#define MISC_B_CLK_DIV_SHIFT	16
#define MISC_A_CLK_DIV_SHIFT	8
#define MISC_B_CLK_SEL_SHIFT	6
#define MISC_A_CLK_SEL_SHIFT	4
#define MISC_CLK_SEL_MASK	0x3
#define MISC_B_EN		BIT(1)
#define MISC_A_EN		BIT(0)

#define MESON_NUM_PWMS		2
#define MESON_NUM_MUX_PARENTS	4

static struct meson_pwm_channel_data {
	u8		reg_offset;
	u8		clk_sel_shift;
	u8		clk_div_shift;
	u8		clk_en_shift;
	u32		pwm_en_mask;
} meson_pwm_per_channel_data[MESON_NUM_PWMS] = {
	{
		.reg_offset	= REG_PWM_A,
		.clk_sel_shift	= MISC_A_CLK_SEL_SHIFT,
		.clk_div_shift	= MISC_A_CLK_DIV_SHIFT,
		.clk_en_shift	= MISC_A_CLK_EN_SHIFT,
		.pwm_en_mask	= MISC_A_EN,
	},
	{
		.reg_offset	= REG_PWM_B,
		.clk_sel_shift	= MISC_B_CLK_SEL_SHIFT,
		.clk_div_shift	= MISC_B_CLK_DIV_SHIFT,
		.clk_en_shift	= MISC_B_CLK_EN_SHIFT,
		.pwm_en_mask	= MISC_B_EN,
	}
};

struct meson_pwm_channel {
	unsigned long rate;
	unsigned int hi;
	unsigned int lo;

	struct clk_mux mux;
	struct clk_divider div;
	struct clk_gate gate;
	struct clk *clk;
};

struct meson_pwm_data {
	const char *const parent_names[MESON_NUM_MUX_PARENTS];
	int (*channels_init)(struct pwm_chip *chip);
};

struct meson_pwm {
	const struct meson_pwm_data *data;
	struct meson_pwm_channel channels[MESON_NUM_PWMS];
	void __iomem *base;
	/*
	 * Protects register (write) access to the REG_MISC_AB register
	 * that is shared between the two PWMs.
	 */
	spinlock_t lock;
};

static inline struct meson_pwm *to_meson_pwm(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int meson_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct meson_pwm *meson = to_meson_pwm(chip);
	struct meson_pwm_channel *channel = &meson->channels[pwm->hwpwm];
	struct device *dev = pwmchip_parent(chip);
	int err;

	err = clk_prepare_enable(channel->clk);
	if (err < 0) {
		dev_err(dev, "failed to enable clock %s: %d\n",
			__clk_get_name(channel->clk), err);
		return err;
	}

	return 0;
}

static void meson_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct meson_pwm *meson = to_meson_pwm(chip);
	struct meson_pwm_channel *channel = &meson->channels[pwm->hwpwm];

	clk_disable_unprepare(channel->clk);
}

static int meson_pwm_calc(struct pwm_chip *chip, struct pwm_device *pwm,
			  const struct pwm_state *state)
{
	struct meson_pwm *meson = to_meson_pwm(chip);
	struct meson_pwm_channel *channel = &meson->channels[pwm->hwpwm];
	unsigned int cnt, duty_cnt;
	long fin_freq;
	u64 duty, period, freq;

	duty = state->duty_cycle;
	period = state->period;

	/*
	 * Note this is wrong. The result is an output wave that isn't really
	 * inverted and so is wrongly identified by .get_state as normal.
	 * Fixing this needs some care however as some machines might rely on
	 * this.
	 */
	if (state->polarity == PWM_POLARITY_INVERSED)
		duty = period - duty;

	freq = div64_u64(NSEC_PER_SEC * 0xffffULL, period);
	if (freq > ULONG_MAX)
		freq = ULONG_MAX;

	fin_freq = clk_round_rate(channel->clk, freq);
	if (fin_freq <= 0) {
		dev_err(pwmchip_parent(chip),
			"invalid source clock frequency %llu\n", freq);
		return fin_freq ? fin_freq : -EINVAL;
	}

	dev_dbg(pwmchip_parent(chip), "fin_freq: %ld Hz\n", fin_freq);

	cnt = mul_u64_u64_div_u64(fin_freq, period, NSEC_PER_SEC);
	if (cnt > 0xffff) {
		dev_err(pwmchip_parent(chip), "unable to get period cnt\n");
		return -EINVAL;
	}

	dev_dbg(pwmchip_parent(chip), "period=%llu cnt=%u\n", period, cnt);

	if (duty == period) {
		channel->hi = cnt;
		channel->lo = 0;
	} else if (duty == 0) {
		channel->hi = 0;
		channel->lo = cnt;
	} else {
		duty_cnt = mul_u64_u64_div_u64(fin_freq, duty, NSEC_PER_SEC);

		dev_dbg(pwmchip_parent(chip), "duty=%llu duty_cnt=%u\n", duty, duty_cnt);

		channel->hi = duty_cnt;
		channel->lo = cnt - duty_cnt;
	}

	channel->rate = fin_freq;

	return 0;
}

static void meson_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct meson_pwm *meson = to_meson_pwm(chip);
	struct meson_pwm_channel *channel = &meson->channels[pwm->hwpwm];
	struct meson_pwm_channel_data *channel_data;
	unsigned long flags;
	u32 value;
	int err;

	channel_data = &meson_pwm_per_channel_data[pwm->hwpwm];

	err = clk_set_rate(channel->clk, channel->rate);
	if (err)
		dev_err(pwmchip_parent(chip), "setting clock rate failed\n");

	spin_lock_irqsave(&meson->lock, flags);

	value = FIELD_PREP(PWM_HIGH_MASK, channel->hi) |
		FIELD_PREP(PWM_LOW_MASK, channel->lo);
	writel(value, meson->base + channel_data->reg_offset);

	value = readl(meson->base + REG_MISC_AB);
	value |= channel_data->pwm_en_mask;
	writel(value, meson->base + REG_MISC_AB);

	spin_unlock_irqrestore(&meson->lock, flags);
}

static void meson_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct meson_pwm *meson = to_meson_pwm(chip);
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&meson->lock, flags);

	value = readl(meson->base + REG_MISC_AB);
	value &= ~meson_pwm_per_channel_data[pwm->hwpwm].pwm_en_mask;
	writel(value, meson->base + REG_MISC_AB);

	spin_unlock_irqrestore(&meson->lock, flags);
}

static int meson_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   const struct pwm_state *state)
{
	struct meson_pwm *meson = to_meson_pwm(chip);
	struct meson_pwm_channel *channel = &meson->channels[pwm->hwpwm];
	int err = 0;

	if (!state->enabled) {
		if (state->polarity == PWM_POLARITY_INVERSED) {
			/*
			 * This IP block revision doesn't have an "always high"
			 * setting which we can use for "inverted disabled".
			 * Instead we achieve this by setting mux parent with
			 * highest rate and minimum divider value, resulting
			 * in the shortest possible duration for one "count"
			 * and "period == duty_cycle". This results in a signal
			 * which is LOW for one "count", while being HIGH for
			 * the rest of the (so the signal is HIGH for slightly
			 * less than 100% of the period, but this is the best
			 * we can achieve).
			 */
			channel->rate = ULONG_MAX;
			channel->hi = ~0;
			channel->lo = 0;

			meson_pwm_enable(chip, pwm);
		} else {
			meson_pwm_disable(chip, pwm);
		}
	} else {
		err = meson_pwm_calc(chip, pwm, state);
		if (err < 0)
			return err;

		meson_pwm_enable(chip, pwm);
	}

	return 0;
}

static u64 meson_pwm_cnt_to_ns(struct pwm_chip *chip, struct pwm_device *pwm,
			       u32 cnt)
{
	struct meson_pwm *meson = to_meson_pwm(chip);
	struct meson_pwm_channel *channel;
	unsigned long fin_freq;

	/* to_meson_pwm() can only be used after .get_state() is called */
	channel = &meson->channels[pwm->hwpwm];

	fin_freq = clk_get_rate(channel->clk);
	if (fin_freq == 0)
		return 0;

	return div64_ul(NSEC_PER_SEC * (u64)cnt, fin_freq);
}

static int meson_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			       struct pwm_state *state)
{
	struct meson_pwm *meson = to_meson_pwm(chip);
	struct meson_pwm_channel_data *channel_data;
	struct meson_pwm_channel *channel;
	u32 value;

	channel = &meson->channels[pwm->hwpwm];
	channel_data = &meson_pwm_per_channel_data[pwm->hwpwm];

	value = readl(meson->base + REG_MISC_AB);
	state->enabled = value & channel_data->pwm_en_mask;

	value = readl(meson->base + channel_data->reg_offset);
	channel->lo = FIELD_GET(PWM_LOW_MASK, value);
	channel->hi = FIELD_GET(PWM_HIGH_MASK, value);

	state->period = meson_pwm_cnt_to_ns(chip, pwm, channel->lo + channel->hi);
	state->duty_cycle = meson_pwm_cnt_to_ns(chip, pwm, channel->hi);

	state->polarity = PWM_POLARITY_NORMAL;

	return 0;
}

static const struct pwm_ops meson_pwm_ops = {
	.request = meson_pwm_request,
	.free = meson_pwm_free,
	.apply = meson_pwm_apply,
	.get_state = meson_pwm_get_state,
};

static int meson_pwm_init_clocks_meson8b(struct pwm_chip *chip,
					 struct clk_parent_data *mux_parent_data)
{
	struct meson_pwm *meson = to_meson_pwm(chip);
	struct device *dev = pwmchip_parent(chip);
	unsigned int i;
	char name[255];
	int err;

	for (i = 0; i < MESON_NUM_PWMS; i++) {
		struct meson_pwm_channel *channel = &meson->channels[i];
		struct clk_parent_data div_parent = {}, gate_parent = {};
		struct clk_init_data init = {};

		snprintf(name, sizeof(name), "%s#mux%u", dev_name(dev), i);

		init.name = name;
		init.ops = &clk_mux_ops;
		init.flags = 0;
		init.parent_data = mux_parent_data;
		init.num_parents = MESON_NUM_MUX_PARENTS;

		channel->mux.reg = meson->base + REG_MISC_AB;
		channel->mux.shift =
				meson_pwm_per_channel_data[i].clk_sel_shift;
		channel->mux.mask = MISC_CLK_SEL_MASK;
		channel->mux.flags = 0;
		channel->mux.lock = &meson->lock;
		channel->mux.table = NULL;
		channel->mux.hw.init = &init;

		err = devm_clk_hw_register(dev, &channel->mux.hw);
		if (err)
			return dev_err_probe(dev, err,
					     "failed to register %s\n", name);

		snprintf(name, sizeof(name), "%s#div%u", dev_name(dev), i);

		init.name = name;
		init.ops = &clk_divider_ops;
		init.flags = CLK_SET_RATE_PARENT;
		div_parent.index = -1;
		div_parent.hw = &channel->mux.hw;
		init.parent_data = &div_parent;
		init.num_parents = 1;

		channel->div.reg = meson->base + REG_MISC_AB;
		channel->div.shift = meson_pwm_per_channel_data[i].clk_div_shift;
		channel->div.width = MISC_CLK_DIV_WIDTH;
		channel->div.hw.init = &init;
		channel->div.flags = 0;
		channel->div.lock = &meson->lock;

		err = devm_clk_hw_register(dev, &channel->div.hw);
		if (err)
			return dev_err_probe(dev, err,
					     "failed to register %s\n", name);

		snprintf(name, sizeof(name), "%s#gate%u", dev_name(dev), i);

		init.name = name;
		init.ops = &clk_gate_ops;
		init.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED;
		gate_parent.index = -1;
		gate_parent.hw = &channel->div.hw;
		init.parent_data = &gate_parent;
		init.num_parents = 1;

		channel->gate.reg = meson->base + REG_MISC_AB;
		channel->gate.bit_idx = meson_pwm_per_channel_data[i].clk_en_shift;
		channel->gate.hw.init = &init;
		channel->gate.flags = 0;
		channel->gate.lock = &meson->lock;

		err = devm_clk_hw_register(dev, &channel->gate.hw);
		if (err)
			return dev_err_probe(dev, err, "failed to register %s\n", name);

		channel->clk = devm_clk_hw_get_clk(dev, &channel->gate.hw, NULL);
		if (IS_ERR(channel->clk))
			return dev_err_probe(dev, PTR_ERR(channel->clk),
					     "failed to register %s\n", name);
	}

	return 0;
}

static int meson_pwm_init_channels_meson8b_legacy(struct pwm_chip *chip)
{
	struct clk_parent_data mux_parent_data[MESON_NUM_MUX_PARENTS] = {};
	struct meson_pwm *meson = to_meson_pwm(chip);
	int i;

	dev_warn_once(pwmchip_parent(chip),
		      "using obsolete compatible, please consider updating dt\n");

	for (i = 0; i < MESON_NUM_MUX_PARENTS; i++) {
		mux_parent_data[i].index = -1;
		mux_parent_data[i].name = meson->data->parent_names[i];
	}

	return meson_pwm_init_clocks_meson8b(chip, mux_parent_data);
}

static int meson_pwm_init_channels_meson8b_v2(struct pwm_chip *chip)
{
	struct clk_parent_data mux_parent_data[MESON_NUM_MUX_PARENTS] = {};
	int i;

	/*
	 * NOTE: Instead of relying on the hard coded names in the driver
	 * as the legacy version, this relies on DT to provide the list of
	 * clocks.
	 * For once, using input numbers actually makes more sense than names.
	 * Also DT requires clock-names to be explicitly ordered, so there is
	 * no point bothering with clock names in this case.
	 */
	for (i = 0; i < MESON_NUM_MUX_PARENTS; i++)
		mux_parent_data[i].index = i;

	return meson_pwm_init_clocks_meson8b(chip, mux_parent_data);
}

static const struct meson_pwm_data pwm_meson8b_data = {
	.parent_names = { "xtal", NULL, "fclk_div4", "fclk_div3" },
	.channels_init = meson_pwm_init_channels_meson8b_legacy,
};

/*
 * Only the 2 first inputs of the GXBB AO PWMs are valid
 * The last 2 are grounded
 */
static const struct meson_pwm_data pwm_gxbb_ao_data = {
	.parent_names = { "xtal", "clk81", NULL, NULL },
	.channels_init = meson_pwm_init_channels_meson8b_legacy,
};

static const struct meson_pwm_data pwm_axg_ee_data = {
	.parent_names = { "xtal", "fclk_div5", "fclk_div4", "fclk_div3" },
	.channels_init = meson_pwm_init_channels_meson8b_legacy,
};

static const struct meson_pwm_data pwm_axg_ao_data = {
	.parent_names = { "xtal", "axg_ao_clk81", "fclk_div4", "fclk_div5" },
	.channels_init = meson_pwm_init_channels_meson8b_legacy,
};

static const struct meson_pwm_data pwm_g12a_ao_ab_data = {
	.parent_names = { "xtal", "g12a_ao_clk81", "fclk_div4", "fclk_div5" },
	.channels_init = meson_pwm_init_channels_meson8b_legacy,
};

static const struct meson_pwm_data pwm_g12a_ao_cd_data = {
	.parent_names = { "xtal", "g12a_ao_clk81", NULL, NULL },
	.channels_init = meson_pwm_init_channels_meson8b_legacy,
};

static const struct meson_pwm_data pwm_meson8_v2_data = {
	.channels_init = meson_pwm_init_channels_meson8b_v2,
};

static const struct of_device_id meson_pwm_matches[] = {
	{
		.compatible = "amlogic,meson8-pwm-v2",
		.data = &pwm_meson8_v2_data
	},
	/* The following compatibles are obsolete */
	{
		.compatible = "amlogic,meson8b-pwm",
		.data = &pwm_meson8b_data
	},
	{
		.compatible = "amlogic,meson-gxbb-pwm",
		.data = &pwm_meson8b_data
	},
	{
		.compatible = "amlogic,meson-gxbb-ao-pwm",
		.data = &pwm_gxbb_ao_data
	},
	{
		.compatible = "amlogic,meson-axg-ee-pwm",
		.data = &pwm_axg_ee_data
	},
	{
		.compatible = "amlogic,meson-axg-ao-pwm",
		.data = &pwm_axg_ao_data
	},
	{
		.compatible = "amlogic,meson-g12a-ee-pwm",
		.data = &pwm_meson8b_data
	},
	{
		.compatible = "amlogic,meson-g12a-ao-pwm-ab",
		.data = &pwm_g12a_ao_ab_data
	},
	{
		.compatible = "amlogic,meson-g12a-ao-pwm-cd",
		.data = &pwm_g12a_ao_cd_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, meson_pwm_matches);

static int meson_pwm_probe(struct platform_device *pdev)
{
	struct pwm_chip *chip;
	struct meson_pwm *meson;
	int err;

	chip = devm_pwmchip_alloc(&pdev->dev, MESON_NUM_PWMS, sizeof(*meson));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	meson = to_meson_pwm(chip);

	meson->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(meson->base))
		return PTR_ERR(meson->base);

	spin_lock_init(&meson->lock);
	chip->ops = &meson_pwm_ops;

	meson->data = of_device_get_match_data(&pdev->dev);

	err = meson->data->channels_init(chip);
	if (err < 0)
		return err;

	err = devm_pwmchip_add(&pdev->dev, chip);
	if (err < 0)
		return dev_err_probe(&pdev->dev, err,
				     "failed to register PWM chip\n");

	return 0;
}

static struct platform_driver meson_pwm_driver = {
	.driver = {
		.name = "meson-pwm",
		.of_match_table = meson_pwm_matches,
	},
	.probe = meson_pwm_probe,
};
module_platform_driver(meson_pwm_driver);

MODULE_DESCRIPTION("Amlogic Meson PWM Generator driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("Dual BSD/GPL");
