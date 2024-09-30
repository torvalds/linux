// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021 Aspeed Technology Inc.
 *
 * PWM/TACH controller driver for Aspeed ast2600 SoCs.
 * This drivers doesn't support earlier version of the IP.
 *
 * The hardware operates in time quantities of length
 * Q := (DIV_L + 1) << DIV_H / input-clk
 * The length of a PWM period is (DUTY_CYCLE_PERIOD + 1) * Q.
 * The maximal value for DUTY_CYCLE_PERIOD is used here to provide
 * a fine grained selection for the duty cycle.
 *
 * This driver uses DUTY_CYCLE_RISING_POINT = 0, so from the start of a
 * period the output is active until DUTY_CYCLE_FALLING_POINT * Q. Note
 * that if DUTY_CYCLE_RISING_POINT = DUTY_CYCLE_FALLING_POINT the output is
 * always active.
 *
 * Register usage:
 * PIN_ENABLE: When it is unset the pwm controller will emit inactive level to the external.
 * Use to determine whether the PWM channel is enabled or disabled
 * CLK_ENABLE: When it is unset the pwm controller will assert the duty counter reset and
 * emit inactive level to the PIN_ENABLE mux after that the driver can still change the pwm period
 * and duty and the value will apply when CLK_ENABLE be set again.
 * Use to determine whether duty_cycle bigger than 0.
 * PWM_ASPEED_CTRL_INVERSE: When it is toggled the output value will inverse immediately.
 * PWM_ASPEED_DUTY_CYCLE_FALLING_POINT/PWM_ASPEED_DUTY_CYCLE_RISING_POINT: When these two
 * values are equal it means the duty cycle = 100%.
 *
 * The glitch may generate at:
 * - Enabled changing when the duty_cycle bigger than 0% and less than 100%.
 * - Polarity changing when the duty_cycle bigger than 0% and less than 100%.
 *
 * Limitations:
 * - When changing both duty cycle and period, we cannot prevent in
 *   software that the output might produce a period with mixed
 *   settings.
 * - Disabling the PWM doesn't complete the current period.
 *
 * Improvements:
 * - When only changing one of duty cycle or period, our pwm controller will not
 *   generate the glitch, the configure will change at next cycle of pwm.
 *   This improvement can disable/enable through PWM_ASPEED_CTRL_DUTY_SYNC_DISABLE.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/reset.h>
#include <linux/sysfs.h>

/* The channel number of Aspeed pwm controller */
#define PWM_ASPEED_NR_PWMS			16
/* PWM Control Register */
#define PWM_ASPEED_CTRL(ch)			((ch) * 0x10 + 0x00)
#define PWM_ASPEED_CTRL_LOAD_SEL_RISING_AS_WDT	BIT(19)
#define PWM_ASPEED_CTRL_DUTY_LOAD_AS_WDT_ENABLE	BIT(18)
#define PWM_ASPEED_CTRL_DUTY_SYNC_DISABLE	BIT(17)
#define PWM_ASPEED_CTRL_CLK_ENABLE		BIT(16)
#define PWM_ASPEED_CTRL_LEVEL_OUTPUT		BIT(15)
#define PWM_ASPEED_CTRL_INVERSE			BIT(14)
#define PWM_ASPEED_CTRL_OPEN_DRAIN_ENABLE	BIT(13)
#define PWM_ASPEED_CTRL_PIN_ENABLE		BIT(12)
#define PWM_ASPEED_CTRL_CLK_DIV_H		GENMASK(11, 8)
#define PWM_ASPEED_CTRL_CLK_DIV_L		GENMASK(7, 0)

/* PWM Duty Cycle Register */
#define PWM_ASPEED_DUTY_CYCLE(ch)		((ch) * 0x10 + 0x04)
#define PWM_ASPEED_DUTY_CYCLE_PERIOD		GENMASK(31, 24)
#define PWM_ASPEED_DUTY_CYCLE_POINT_AS_WDT	GENMASK(23, 16)
#define PWM_ASPEED_DUTY_CYCLE_FALLING_POINT	GENMASK(15, 8)
#define PWM_ASPEED_DUTY_CYCLE_RISING_POINT	GENMASK(7, 0)

/* PWM fixed value */
#define PWM_ASPEED_FIXED_PERIOD			FIELD_MAX(PWM_ASPEED_DUTY_CYCLE_PERIOD)

/* The channel number of Aspeed tach controller */
#define TACH_ASPEED_NR_TACHS		16
/* TACH Control Register */
#define TACH_ASPEED_CTRL(ch)		(((ch) * 0x10) + 0x08)
#define TACH_ASPEED_IER			BIT(31)
#define TACH_ASPEED_INVERS_LIMIT	BIT(30)
#define TACH_ASPEED_LOOPBACK		BIT(29)
#define TACH_ASPEED_ENABLE		BIT(28)
#define TACH_ASPEED_DEBOUNCE_MASK	GENMASK(27, 26)
#define TACH_ASPEED_DEBOUNCE_BIT	26
#define TACH_ASPEED_IO_EDGE_MASK	GENMASK(25, 24)
#define TACH_ASPEED_IO_EDGE_BIT		24
#define TACH_ASPEED_CLK_DIV_T_MASK	GENMASK(23, 20)
#define TACH_ASPEED_CLK_DIV_BIT		20
#define TACH_ASPEED_THRESHOLD_MASK	GENMASK(19, 0)
/* [27:26] */
#define DEBOUNCE_3_CLK			0x00
#define DEBOUNCE_2_CLK			0x01
#define DEBOUNCE_1_CLK			0x02
#define DEBOUNCE_0_CLK			0x03
/* [25:24] */
#define F2F_EDGES			0x00
#define R2R_EDGES			0x01
#define BOTH_EDGES			0x02
/* [23:20] */
/* divisor = 4 to the nth power, n = register value */
#define DEFAULT_TACH_DIV		1024
#define DIV_TO_REG(divisor)		(ilog2(divisor) >> 1)

/* TACH Status Register */
#define TACH_ASPEED_STS(ch)		(((ch) * 0x10) + 0x0C)

/*PWM_TACH_STS */
#define TACH_ASPEED_ISR			BIT(31)
#define TACH_ASPEED_PWM_OUT		BIT(25)
#define TACH_ASPEED_PWM_OEN		BIT(24)
#define TACH_ASPEED_DEB_INPUT		BIT(23)
#define TACH_ASPEED_RAW_INPUT		BIT(22)
#define TACH_ASPEED_VALUE_UPDATE	BIT(21)
#define TACH_ASPEED_FULL_MEASUREMENT	BIT(20)
#define TACH_ASPEED_VALUE_MASK		GENMASK(19, 0)
/**********************************************************
 * Software setting
 *********************************************************/
#define DEFAULT_FAN_PULSE_PR		2

struct aspeed_pwm_tach_data {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *reset;
	unsigned long clk_rate;
	bool tach_present[TACH_ASPEED_NR_TACHS];
	u32 tach_divisor;
};

static inline struct aspeed_pwm_tach_data *
aspeed_pwm_chip_to_data(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int aspeed_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				struct pwm_state *state)
{
	struct aspeed_pwm_tach_data *priv = aspeed_pwm_chip_to_data(chip);
	u32 hwpwm = pwm->hwpwm;
	bool polarity, pin_en, clk_en;
	u32 duty_pt, val;
	u64 div_h, div_l, duty_cycle_period, dividend;

	val = readl(priv->base + PWM_ASPEED_CTRL(hwpwm));
	polarity = FIELD_GET(PWM_ASPEED_CTRL_INVERSE, val);
	pin_en = FIELD_GET(PWM_ASPEED_CTRL_PIN_ENABLE, val);
	clk_en = FIELD_GET(PWM_ASPEED_CTRL_CLK_ENABLE, val);
	div_h = FIELD_GET(PWM_ASPEED_CTRL_CLK_DIV_H, val);
	div_l = FIELD_GET(PWM_ASPEED_CTRL_CLK_DIV_L, val);
	val = readl(priv->base + PWM_ASPEED_DUTY_CYCLE(hwpwm));
	duty_pt = FIELD_GET(PWM_ASPEED_DUTY_CYCLE_FALLING_POINT, val);
	duty_cycle_period = FIELD_GET(PWM_ASPEED_DUTY_CYCLE_PERIOD, val);
	/*
	 * This multiplication doesn't overflow, the upper bound is
	 * 1000000000 * 256 * 256 << 15 = 0x1dcd650000000000
	 */
	dividend = (u64)NSEC_PER_SEC * (div_l + 1) * (duty_cycle_period + 1)
		       << div_h;
	state->period = DIV_ROUND_UP_ULL(dividend, priv->clk_rate);

	if (clk_en && duty_pt) {
		dividend = (u64)NSEC_PER_SEC * (div_l + 1) * duty_pt
				 << div_h;
		state->duty_cycle = DIV_ROUND_UP_ULL(dividend, priv->clk_rate);
	} else {
		state->duty_cycle = clk_en ? state->period : 0;
	}
	state->polarity = polarity ? PWM_POLARITY_INVERSED : PWM_POLARITY_NORMAL;
	state->enabled = pin_en;
	return 0;
}

static int aspeed_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct aspeed_pwm_tach_data *priv = aspeed_pwm_chip_to_data(chip);
	u32 hwpwm = pwm->hwpwm, duty_pt, val;
	u64 div_h, div_l, divisor, expect_period;
	bool clk_en;

	expect_period = div64_u64(ULLONG_MAX, (u64)priv->clk_rate);
	expect_period = min(expect_period, state->period);
	dev_dbg(pwmchip_parent(chip), "expect period: %lldns, duty_cycle: %lldns",
		expect_period, state->duty_cycle);
	/*
	 * Pick the smallest value for div_h so that div_l can be the biggest
	 * which results in a finer resolution near the target period value.
	 */
	divisor = (u64)NSEC_PER_SEC * (PWM_ASPEED_FIXED_PERIOD + 1) *
		  (FIELD_MAX(PWM_ASPEED_CTRL_CLK_DIV_L) + 1);
	div_h = order_base_2(DIV64_U64_ROUND_UP(priv->clk_rate * expect_period, divisor));
	if (div_h > 0xf)
		div_h = 0xf;

	divisor = ((u64)NSEC_PER_SEC * (PWM_ASPEED_FIXED_PERIOD + 1)) << div_h;
	div_l = div64_u64(priv->clk_rate * expect_period, divisor);

	if (div_l == 0)
		return -ERANGE;

	div_l -= 1;

	if (div_l > 255)
		div_l = 255;

	dev_dbg(pwmchip_parent(chip), "clk source: %ld div_h %lld, div_l : %lld\n",
		priv->clk_rate, div_h, div_l);
	/* duty_pt = duty_cycle * (PERIOD + 1) / period */
	duty_pt = div64_u64(state->duty_cycle * priv->clk_rate,
			    (u64)NSEC_PER_SEC * (div_l + 1) << div_h);
	dev_dbg(pwmchip_parent(chip), "duty_cycle = %lld, duty_pt = %d\n",
		state->duty_cycle, duty_pt);

	/*
	 * Fixed DUTY_CYCLE_PERIOD to its max value to get a
	 * fine-grained resolution for duty_cycle at the expense of a
	 * coarser period resolution.
	 */
	val = readl(priv->base + PWM_ASPEED_DUTY_CYCLE(hwpwm));
	val &= ~PWM_ASPEED_DUTY_CYCLE_PERIOD;
	val |= FIELD_PREP(PWM_ASPEED_DUTY_CYCLE_PERIOD,
			  PWM_ASPEED_FIXED_PERIOD);
	writel(val, priv->base + PWM_ASPEED_DUTY_CYCLE(hwpwm));

	if (duty_pt == 0) {
		/* emit inactive level and assert the duty counter reset */
		clk_en = 0;
	} else {
		clk_en = 1;
		if (duty_pt >= (PWM_ASPEED_FIXED_PERIOD + 1))
			duty_pt = 0;
		val = readl(priv->base + PWM_ASPEED_DUTY_CYCLE(hwpwm));
		val &= ~(PWM_ASPEED_DUTY_CYCLE_RISING_POINT |
			 PWM_ASPEED_DUTY_CYCLE_FALLING_POINT);
		val |= FIELD_PREP(PWM_ASPEED_DUTY_CYCLE_FALLING_POINT, duty_pt);
		writel(val, priv->base + PWM_ASPEED_DUTY_CYCLE(hwpwm));
	}

	val = readl(priv->base + PWM_ASPEED_CTRL(hwpwm));
	val &= ~(PWM_ASPEED_CTRL_CLK_DIV_H | PWM_ASPEED_CTRL_CLK_DIV_L |
		 PWM_ASPEED_CTRL_PIN_ENABLE | PWM_ASPEED_CTRL_CLK_ENABLE |
		 PWM_ASPEED_CTRL_INVERSE);
	val |= FIELD_PREP(PWM_ASPEED_CTRL_CLK_DIV_H, div_h) |
	       FIELD_PREP(PWM_ASPEED_CTRL_CLK_DIV_L, div_l) |
	       FIELD_PREP(PWM_ASPEED_CTRL_PIN_ENABLE, state->enabled) |
	       FIELD_PREP(PWM_ASPEED_CTRL_CLK_ENABLE, clk_en) |
	       FIELD_PREP(PWM_ASPEED_CTRL_INVERSE, state->polarity);
	writel(val, priv->base + PWM_ASPEED_CTRL(hwpwm));

	return 0;
}

static const struct pwm_ops aspeed_pwm_ops = {
	.apply = aspeed_pwm_apply,
	.get_state = aspeed_pwm_get_state,
};

static void aspeed_tach_ch_enable(struct aspeed_pwm_tach_data *priv, u8 tach_ch,
				  bool enable)
{
	if (enable)
		writel(readl(priv->base + TACH_ASPEED_CTRL(tach_ch)) |
			       TACH_ASPEED_ENABLE,
		       priv->base + TACH_ASPEED_CTRL(tach_ch));
	else
		writel(readl(priv->base + TACH_ASPEED_CTRL(tach_ch)) &
			       ~TACH_ASPEED_ENABLE,
		       priv->base + TACH_ASPEED_CTRL(tach_ch));
}

static int aspeed_tach_val_to_rpm(struct aspeed_pwm_tach_data *priv, u32 tach_val)
{
	u64 rpm;
	u32 tach_div;

	tach_div = tach_val * priv->tach_divisor * DEFAULT_FAN_PULSE_PR;

	dev_dbg(priv->dev, "clk %ld, tach_val %d , tach_div %d\n",
		priv->clk_rate, tach_val, tach_div);

	rpm = (u64)priv->clk_rate * 60;
	do_div(rpm, tach_div);

	return (int)rpm;
}

static int aspeed_get_fan_tach_ch_rpm(struct aspeed_pwm_tach_data *priv,
				      u8 fan_tach_ch)
{
	u32 val;

	val = readl(priv->base + TACH_ASPEED_STS(fan_tach_ch));

	if (!(val & TACH_ASPEED_FULL_MEASUREMENT))
		return 0;
	val = FIELD_GET(TACH_ASPEED_VALUE_MASK, val);
	return aspeed_tach_val_to_rpm(priv, val);
}

static int aspeed_tach_hwmon_read(struct device *dev,
				  enum hwmon_sensor_types type, u32 attr,
				  int channel, long *val)
{
	struct aspeed_pwm_tach_data *priv = dev_get_drvdata(dev);
	u32 reg_val;

	switch (attr) {
	case hwmon_fan_input:
		*val = aspeed_get_fan_tach_ch_rpm(priv, channel);
		break;
	case hwmon_fan_div:
		reg_val = readl(priv->base + TACH_ASPEED_CTRL(channel));
		reg_val = FIELD_GET(TACH_ASPEED_CLK_DIV_T_MASK, reg_val);
		*val = BIT(reg_val << 1);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int aspeed_tach_hwmon_write(struct device *dev,
				   enum hwmon_sensor_types type, u32 attr,
				   int channel, long val)
{
	struct aspeed_pwm_tach_data *priv = dev_get_drvdata(dev);
	u32 reg_val;

	switch (attr) {
	case hwmon_fan_div:
		if (!is_power_of_2(val) || (ilog2(val) % 2) ||
		    DIV_TO_REG(val) > 0xb)
			return -EINVAL;
		priv->tach_divisor = val;
		reg_val = readl(priv->base + TACH_ASPEED_CTRL(channel));
		reg_val &= ~TACH_ASPEED_CLK_DIV_T_MASK;
		reg_val |= FIELD_PREP(TACH_ASPEED_CLK_DIV_T_MASK,
				      DIV_TO_REG(priv->tach_divisor));
		writel(reg_val, priv->base + TACH_ASPEED_CTRL(channel));
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static umode_t aspeed_tach_dev_is_visible(const void *drvdata,
					  enum hwmon_sensor_types type,
					  u32 attr, int channel)
{
	const struct aspeed_pwm_tach_data *priv = drvdata;

	if (!priv->tach_present[channel])
		return 0;
	switch (attr) {
	case hwmon_fan_input:
		return 0444;
	case hwmon_fan_div:
		return 0644;
	}
	return 0;
}

static const struct hwmon_ops aspeed_tach_ops = {
	.is_visible = aspeed_tach_dev_is_visible,
	.read = aspeed_tach_hwmon_read,
	.write = aspeed_tach_hwmon_write,
};

static const struct hwmon_channel_info *aspeed_tach_info[] = {
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT | HWMON_F_DIV, HWMON_F_INPUT | HWMON_F_DIV,
			   HWMON_F_INPUT | HWMON_F_DIV, HWMON_F_INPUT | HWMON_F_DIV,
			   HWMON_F_INPUT | HWMON_F_DIV, HWMON_F_INPUT | HWMON_F_DIV,
			   HWMON_F_INPUT | HWMON_F_DIV, HWMON_F_INPUT | HWMON_F_DIV,
			   HWMON_F_INPUT | HWMON_F_DIV, HWMON_F_INPUT | HWMON_F_DIV,
			   HWMON_F_INPUT | HWMON_F_DIV, HWMON_F_INPUT | HWMON_F_DIV,
			   HWMON_F_INPUT | HWMON_F_DIV, HWMON_F_INPUT | HWMON_F_DIV,
			   HWMON_F_INPUT | HWMON_F_DIV, HWMON_F_INPUT | HWMON_F_DIV),
	NULL
};

static const struct hwmon_chip_info aspeed_tach_chip_info = {
	.ops = &aspeed_tach_ops,
	.info = aspeed_tach_info,
};

static void aspeed_present_fan_tach(struct aspeed_pwm_tach_data *priv, u8 *tach_ch, int count)
{
	u8 ch, index;
	u32 val;

	for (index = 0; index < count; index++) {
		ch = tach_ch[index];
		priv->tach_present[ch] = true;
		priv->tach_divisor = DEFAULT_TACH_DIV;

		val = readl(priv->base + TACH_ASPEED_CTRL(ch));
		val &= ~(TACH_ASPEED_INVERS_LIMIT | TACH_ASPEED_DEBOUNCE_MASK |
			 TACH_ASPEED_IO_EDGE_MASK | TACH_ASPEED_CLK_DIV_T_MASK |
			 TACH_ASPEED_THRESHOLD_MASK);
		val |= (DEBOUNCE_3_CLK << TACH_ASPEED_DEBOUNCE_BIT) |
		       F2F_EDGES |
		       FIELD_PREP(TACH_ASPEED_CLK_DIV_T_MASK,
				  DIV_TO_REG(priv->tach_divisor));
		writel(val, priv->base + TACH_ASPEED_CTRL(ch));

		aspeed_tach_ch_enable(priv, ch, true);
	}
}

static int aspeed_create_fan_monitor(struct device *dev,
				     struct device_node *child,
				     struct aspeed_pwm_tach_data *priv)
{
	int ret, count;
	u8 *tach_ch;

	count = of_property_count_u8_elems(child, "tach-ch");
	if (count < 1)
		return -EINVAL;
	tach_ch = devm_kcalloc(dev, count, sizeof(*tach_ch), GFP_KERNEL);
	if (!tach_ch)
		return -ENOMEM;
	ret = of_property_read_u8_array(child, "tach-ch", tach_ch, count);
	if (ret)
		return ret;

	aspeed_present_fan_tach(priv, tach_ch, count);

	return 0;
}

static void aspeed_pwm_tach_reset_assert(void *data)
{
	struct reset_control *rst = data;

	reset_control_assert(rst);
}

static int aspeed_pwm_tach_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev, *hwmon;
	int ret;
	struct aspeed_pwm_tach_data *priv;
	struct pwm_chip *chip;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;
	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "Couldn't get clock\n");
	priv->clk_rate = clk_get_rate(priv->clk);
	priv->reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(priv->reset))
		return dev_err_probe(dev, PTR_ERR(priv->reset),
				     "Couldn't get reset control\n");

	ret = reset_control_deassert(priv->reset);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Couldn't deassert reset control\n");
	ret = devm_add_action_or_reset(dev, aspeed_pwm_tach_reset_assert,
				       priv->reset);
	if (ret)
		return ret;

	chip = devm_pwmchip_alloc(dev, PWM_ASPEED_NR_PWMS, 0);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	pwmchip_set_drvdata(chip, priv);
	chip->ops = &aspeed_pwm_ops;

	ret = devm_pwmchip_add(dev, chip);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add PWM chip\n");

	for_each_child_of_node_scoped(dev->of_node, child) {
		ret = aspeed_create_fan_monitor(dev, child, priv);
		if (ret) {
			dev_warn(dev, "Failed to create fan %d", ret);
			return 0;
		}
	}

	hwmon = devm_hwmon_device_register_with_info(dev, "aspeed_tach", priv,
						     &aspeed_tach_chip_info, NULL);
	ret = PTR_ERR_OR_ZERO(hwmon);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register hwmon device\n");

	of_platform_populate(dev->of_node, NULL, NULL, dev);

	return 0;
}

static void aspeed_pwm_tach_remove(struct platform_device *pdev)
{
	struct aspeed_pwm_tach_data *priv = platform_get_drvdata(pdev);

	reset_control_assert(priv->reset);
}

static const struct of_device_id aspeed_pwm_tach_match[] = {
	{
		.compatible = "aspeed,ast2600-pwm-tach",
	},
	{},
};
MODULE_DEVICE_TABLE(of, aspeed_pwm_tach_match);

static struct platform_driver aspeed_pwm_tach_driver = {
	.probe = aspeed_pwm_tach_probe,
	.remove_new = aspeed_pwm_tach_remove,
	.driver	= {
		.name = "aspeed-g6-pwm-tach",
		.of_match_table = aspeed_pwm_tach_match,
	},
};

module_platform_driver(aspeed_pwm_tach_driver);

MODULE_AUTHOR("Billy Tsai <billy_tsai@aspeedtech.com>");
MODULE_DESCRIPTION("Aspeed ast2600 PWM and Fan Tach device driver");
MODULE_LICENSE("GPL");
