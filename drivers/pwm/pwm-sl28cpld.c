// SPDX-License-Identifier: GPL-2.0-only
/*
 * sl28cpld PWM driver
 *
 * Copyright (c) 2020 Michael Walle <michael@walle.cc>
 *
 * There is no public datasheet available for this PWM core. But it is easy
 * enough to be briefly explained. It consists of one 8-bit counter. The PWM
 * supports four distinct frequencies by selecting when to reset the counter.
 * With the prescaler setting you can select which bit of the counter is used
 * to reset it. This implies that the higher the frequency the less remaining
 * bits are available for the actual counter.
 *
 * Let cnt[7:0] be the counter, clocked at 32kHz:
 * +-----------+--------+--------------+-----------+---------------+
 * | prescaler |  reset | counter bits | frequency | period length |
 * +-----------+--------+--------------+-----------+---------------+
 * |         0 | cnt[7] |     cnt[6:0] |    250 Hz |    4000000 ns |
 * |         1 | cnt[6] |     cnt[5:0] |    500 Hz |    2000000 ns |
 * |         2 | cnt[5] |     cnt[4:0] |     1 kHz |    1000000 ns |
 * |         3 | cnt[4] |     cnt[3:0] |     2 kHz |     500000 ns |
 * +-----------+--------+--------------+-----------+---------------+
 *
 * Limitations:
 * - The hardware cannot generate a 100% duty cycle if the prescaler is 0.
 * - The hardware cannot atomically set the prescaler and the counter value,
 *   which might lead to glitches and inconsistent states if a write fails.
 * - The counter is not reset if you switch the prescaler which leads
 *   to glitches, too.
 * - The duty cycle will switch immediately and not after a complete cycle.
 * - Depending on the actual implementation, disabling the PWM might have
 *   side effects. For example, if the output pin is shared with a GPIO pin
 *   it will automatically switch back to GPIO mode.
 */

#include <linux/bitfield.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>

/*
 * PWM timer block registers.
 */
#define SL28CPLD_PWM_CTRL			0x00
#define   SL28CPLD_PWM_CTRL_ENABLE		BIT(7)
#define   SL28CPLD_PWM_CTRL_PRESCALER_MASK	GENMASK(1, 0)
#define SL28CPLD_PWM_CYCLE			0x01
#define   SL28CPLD_PWM_CYCLE_MAX		GENMASK(6, 0)

#define SL28CPLD_PWM_CLK			32000 /* 32 kHz */
#define SL28CPLD_PWM_MAX_DUTY_CYCLE(prescaler)	(1 << (7 - (prescaler)))
#define SL28CPLD_PWM_PERIOD(prescaler) \
	(NSEC_PER_SEC / SL28CPLD_PWM_CLK * SL28CPLD_PWM_MAX_DUTY_CYCLE(prescaler))

/*
 * We calculate the duty cycle like this:
 *   duty_cycle_ns = pwm_cycle_reg * max_period_ns / max_duty_cycle
 *
 * With
 *   max_period_ns = 1 << (7 - prescaler) / SL28CPLD_PWM_CLK * NSEC_PER_SEC
 *   max_duty_cycle = 1 << (7 - prescaler)
 * this then simplifies to:
 *   duty_cycle_ns = pwm_cycle_reg / SL28CPLD_PWM_CLK * NSEC_PER_SEC
 *                 = NSEC_PER_SEC / SL28CPLD_PWM_CLK * pwm_cycle_reg
 *
 * NSEC_PER_SEC is a multiple of SL28CPLD_PWM_CLK, therefore we're not losing
 * precision by doing the divison first.
 */
#define SL28CPLD_PWM_TO_DUTY_CYCLE(reg) \
	(NSEC_PER_SEC / SL28CPLD_PWM_CLK * (reg))
#define SL28CPLD_PWM_FROM_DUTY_CYCLE(duty_cycle) \
	(DIV_ROUND_DOWN_ULL((duty_cycle), NSEC_PER_SEC / SL28CPLD_PWM_CLK))

#define sl28cpld_pwm_read(priv, reg, val) \
	regmap_read((priv)->regmap, (priv)->offset + (reg), (val))
#define sl28cpld_pwm_write(priv, reg, val) \
	regmap_write((priv)->regmap, (priv)->offset + (reg), (val))

struct sl28cpld_pwm {
	struct pwm_chip pwm_chip;
	struct regmap *regmap;
	u32 offset;
};

static void sl28cpld_pwm_get_state(struct pwm_chip *chip,
				   struct pwm_device *pwm,
				   struct pwm_state *state)
{
	struct sl28cpld_pwm *priv = dev_get_drvdata(chip->dev);
	unsigned int reg;
	int prescaler;

	sl28cpld_pwm_read(priv, SL28CPLD_PWM_CTRL, &reg);

	state->enabled = reg & SL28CPLD_PWM_CTRL_ENABLE;

	prescaler = FIELD_GET(SL28CPLD_PWM_CTRL_PRESCALER_MASK, reg);
	state->period = SL28CPLD_PWM_PERIOD(prescaler);

	sl28cpld_pwm_read(priv, SL28CPLD_PWM_CYCLE, &reg);
	state->duty_cycle = SL28CPLD_PWM_TO_DUTY_CYCLE(reg);
	state->polarity = PWM_POLARITY_NORMAL;

	/*
	 * Sanitize values for the PWM core. Depending on the prescaler it
	 * might happen that we calculate a duty_cycle greater than the actual
	 * period. This might happen if someone (e.g. the bootloader) sets an
	 * invalid combination of values. The behavior of the hardware is
	 * undefined in this case. But we need to report sane values back to
	 * the PWM core.
	 */
	state->duty_cycle = min(state->duty_cycle, state->period);
}

static int sl28cpld_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      const struct pwm_state *state)
{
	struct sl28cpld_pwm *priv = dev_get_drvdata(chip->dev);
	unsigned int cycle, prescaler;
	bool write_duty_cycle_first;
	int ret;
	u8 ctrl;

	/* Polarity inversion is not supported */
	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	/*
	 * Calculate the prescaler. Pick the biggest period that isn't
	 * bigger than the requested period.
	 */
	prescaler = DIV_ROUND_UP_ULL(SL28CPLD_PWM_PERIOD(0), state->period);
	prescaler = order_base_2(prescaler);

	if (prescaler > field_max(SL28CPLD_PWM_CTRL_PRESCALER_MASK))
		return -ERANGE;

	ctrl = FIELD_PREP(SL28CPLD_PWM_CTRL_PRESCALER_MASK, prescaler);
	if (state->enabled)
		ctrl |= SL28CPLD_PWM_CTRL_ENABLE;

	cycle = SL28CPLD_PWM_FROM_DUTY_CYCLE(state->duty_cycle);
	cycle = min_t(unsigned int, cycle, SL28CPLD_PWM_MAX_DUTY_CYCLE(prescaler));

	/*
	 * Work around the hardware limitation. See also above. Trap 100% duty
	 * cycle if the prescaler is 0. Set prescaler to 1 instead. We don't
	 * care about the frequency because its "all-one" in either case.
	 *
	 * We don't need to check the actual prescaler setting, because only
	 * if the prescaler is 0 we can have this particular value.
	 */
	if (cycle == SL28CPLD_PWM_MAX_DUTY_CYCLE(0)) {
		ctrl &= ~SL28CPLD_PWM_CTRL_PRESCALER_MASK;
		ctrl |= FIELD_PREP(SL28CPLD_PWM_CTRL_PRESCALER_MASK, 1);
		cycle = SL28CPLD_PWM_MAX_DUTY_CYCLE(1);
	}

	/*
	 * To avoid glitches when we switch the prescaler, we have to make sure
	 * we have a valid duty cycle for the new mode.
	 *
	 * Take the current prescaler (or the current period length) into
	 * account to decide whether we have to write the duty cycle or the new
	 * prescaler first. If the period length is decreasing we have to
	 * write the duty cycle first.
	 */
	write_duty_cycle_first = pwm->state.period > state->period;

	if (write_duty_cycle_first) {
		ret = sl28cpld_pwm_write(priv, SL28CPLD_PWM_CYCLE, cycle);
		if (ret)
			return ret;
	}

	ret = sl28cpld_pwm_write(priv, SL28CPLD_PWM_CTRL, ctrl);
	if (ret)
		return ret;

	if (!write_duty_cycle_first) {
		ret = sl28cpld_pwm_write(priv, SL28CPLD_PWM_CYCLE, cycle);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pwm_ops sl28cpld_pwm_ops = {
	.apply = sl28cpld_pwm_apply,
	.get_state = sl28cpld_pwm_get_state,
	.owner = THIS_MODULE,
};

static int sl28cpld_pwm_probe(struct platform_device *pdev)
{
	struct sl28cpld_pwm *priv;
	struct pwm_chip *chip;
	int ret;

	if (!pdev->dev.parent) {
		dev_err(&pdev->dev, "no parent device\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!priv->regmap) {
		dev_err(&pdev->dev, "could not get parent regmap\n");
		return -ENODEV;
	}

	ret = device_property_read_u32(&pdev->dev, "reg", &priv->offset);
	if (ret) {
		dev_err(&pdev->dev, "no 'reg' property found (%pe)\n",
			ERR_PTR(ret));
		return -EINVAL;
	}

	/* Initialize the pwm_chip structure */
	chip = &priv->pwm_chip;
	chip->dev = &pdev->dev;
	chip->ops = &sl28cpld_pwm_ops;
	chip->base = -1;
	chip->npwm = 1;

	ret = pwmchip_add(&priv->pwm_chip);
	if (ret) {
		dev_err(&pdev->dev, "failed to add PWM chip (%pe)",
			ERR_PTR(ret));
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	return 0;
}

static int sl28cpld_pwm_remove(struct platform_device *pdev)
{
	struct sl28cpld_pwm *priv = platform_get_drvdata(pdev);

	return pwmchip_remove(&priv->pwm_chip);
}

static const struct of_device_id sl28cpld_pwm_of_match[] = {
	{ .compatible = "kontron,sl28cpld-pwm" },
	{}
};
MODULE_DEVICE_TABLE(of, sl28cpld_pwm_of_match);

static struct platform_driver sl28cpld_pwm_driver = {
	.probe = sl28cpld_pwm_probe,
	.remove	= sl28cpld_pwm_remove,
	.driver = {
		.name = "sl28cpld-pwm",
		.of_match_table = sl28cpld_pwm_of_match,
	},
};
module_platform_driver(sl28cpld_pwm_driver);

MODULE_DESCRIPTION("sl28cpld PWM Driver");
MODULE_AUTHOR("Michael Walle <michael@walle.cc>");
MODULE_LICENSE("GPL");
