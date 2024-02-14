// SPDX-License-Identifier: GPL-2.0+
/*
 * Cirrus Logic CLPS711X PWM driver
 * Author: Alexander Shiyan <shc_work@mail.ru>
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

struct clps711x_chip {
	struct pwm_chip chip;
	void __iomem *pmpcon;
	struct clk *clk;
	spinlock_t lock;
};

static inline struct clps711x_chip *to_clps711x_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct clps711x_chip, chip);
}

static int clps711x_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct clps711x_chip *priv = to_clps711x_chip(chip);
	unsigned int freq = clk_get_rate(priv->clk);

	if (!freq)
		return -EINVAL;

	/* Store constant period value */
	pwm->args.period = DIV_ROUND_CLOSEST(NSEC_PER_SEC, freq);

	return 0;
}

static int clps711x_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      const struct pwm_state *state)
{
	struct clps711x_chip *priv = to_clps711x_chip(chip);
	/* PWM0 - bits 4..7, PWM1 - bits 8..11 */
	u32 shift = (pwm->hwpwm + 1) * 4;
	unsigned long flags;
	u32 pmpcon, val;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (state->period != pwm->args.period)
		return -EINVAL;

	if (state->enabled)
		val = mul_u64_u64_div_u64(state->duty_cycle, 0xf, state->period);
	else
		val = 0;

	spin_lock_irqsave(&priv->lock, flags);

	pmpcon = readl(priv->pmpcon);
	pmpcon &= ~(0xf << shift);
	pmpcon |= val << shift;
	writel(pmpcon, priv->pmpcon);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static const struct pwm_ops clps711x_pwm_ops = {
	.request = clps711x_pwm_request,
	.apply = clps711x_pwm_apply,
};

static struct pwm_device *clps711x_pwm_xlate(struct pwm_chip *chip,
					     const struct of_phandle_args *args)
{
	if (args->args[0] >= chip->npwm)
		return ERR_PTR(-EINVAL);

	return pwm_request_from_chip(chip, args->args[0], NULL);
}

static int clps711x_pwm_probe(struct platform_device *pdev)
{
	struct clps711x_chip *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pmpcon = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->pmpcon))
		return PTR_ERR(priv->pmpcon);

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	priv->chip.ops = &clps711x_pwm_ops;
	priv->chip.dev = &pdev->dev;
	priv->chip.npwm = 2;
	priv->chip.of_xlate = clps711x_pwm_xlate;
	priv->chip.of_pwm_n_cells = 1;

	spin_lock_init(&priv->lock);

	return devm_pwmchip_add(&pdev->dev, &priv->chip);
}

static const struct of_device_id __maybe_unused clps711x_pwm_dt_ids[] = {
	{ .compatible = "cirrus,ep7209-pwm", },
	{ }
};
MODULE_DEVICE_TABLE(of, clps711x_pwm_dt_ids);

static struct platform_driver clps711x_pwm_driver = {
	.driver = {
		.name = "clps711x-pwm",
		.of_match_table = of_match_ptr(clps711x_pwm_dt_ids),
	},
	.probe = clps711x_pwm_probe,
};
module_platform_driver(clps711x_pwm_driver);

MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("Cirrus Logic CLPS711X PWM driver");
MODULE_LICENSE("GPL");
