// SPDX-License-Identifier: GPL-2.0
/*
 * PWM driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2018 StarFive Technology Co., Ltd.
 */

#include <dt-bindings/pwm/pwm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/io.h>

/* how many parameters can be transferred to ptc */
#define OF_PWM_N_CELLS			3

/* max channel of pwm */
#define MAX_PWM				8

/* PTC Register offsets */
#define REG_RPTC_CNTR			0x0
#define REG_RPTC_HRC			0x4
#define REG_RPTC_LRC			0x8
#define REG_RPTC_CTRL			0xC

/* Bit for PWM clock */
#define BIT_PWM_CLOCK_EN		31

/* Bit for clock gen soft reset */
#define BIT_CLK_GEN_SOFT_RESET		13

#define NS_PER_SECOND			1000000000
#define DEFAULT_FREQ_HZ			2000000

/*
 * Access PTC register (cntr hrc lrc and ctrl),
 * need to replace PWM_BASE_ADDR
 */
#define REG_PTC_BASE_ADDR_SUB(base, N)	\
((base) + (((N) > 3) ? (((N) % 4) * 0x10 + (1 << 15)) : ((N) * 0x10)))
#define REG_PTC_RPTC_CNTR(base, N)	(REG_PTC_BASE_ADDR_SUB(base, N))
#define REG_PTC_RPTC_HRC(base, N)	(REG_PTC_BASE_ADDR_SUB(base, N) + 0x4)
#define REG_PTC_RPTC_LRC(base, N)	(REG_PTC_BASE_ADDR_SUB(base, N) + 0x8)
#define REG_PTC_RPTC_CTRL(base, N)	(REG_PTC_BASE_ADDR_SUB(base, N) + 0xC)

/* PTC_RPTC_CTRL */
#define PTC_EN      BIT(0)
#define PTC_ECLK    BIT(1)
#define PTC_NEC     BIT(2)
#define PTC_OE      BIT(3)
#define PTC_SIGNLE  BIT(4)
#define PTC_INTE    BIT(5)
#define PTC_INT     BIT(6)
#define PTC_CNTRRST BIT(7)
#define PTC_CAPTE   BIT(8)

struct starfive_pwm_ptc_device {
	struct pwm_chip		chip;
	struct clk		*clk;
	struct reset_control	*rst;
	void __iomem		*regs;
	int			irq;
	/*pwm apb clock frequency*/
	unsigned int		approx_freq;
};

static inline struct starfive_pwm_ptc_device *
		chip_to_starfive_ptc(struct pwm_chip *c)
{
	return container_of(c, struct starfive_pwm_ptc_device, chip);
}

static void starfive_pwm_ptc_get_state(struct pwm_chip *chip,
				       struct pwm_device *dev,
				       struct pwm_state *state)
{
	struct starfive_pwm_ptc_device *pwm = chip_to_starfive_ptc(chip);
	u32 data_lrc;
	u32 data_hrc;
	u32 pwm_clk_ns = 0;

	data_lrc = ioread32(REG_PTC_RPTC_LRC(pwm->regs, dev->hwpwm));
	data_hrc = ioread32(REG_PTC_RPTC_HRC(pwm->regs, dev->hwpwm));

	pwm_clk_ns = NS_PER_SECOND / pwm->approx_freq;

	state->period = data_lrc * pwm_clk_ns;
	state->duty_cycle = data_hrc * pwm_clk_ns;
	state->polarity = PWM_POLARITY_NORMAL;
	state->enabled = 1;
}

static int starfive_pwm_ptc_apply(struct pwm_chip *chip,
				  struct pwm_device *dev,
				  struct pwm_state *state)
{
	struct starfive_pwm_ptc_device *pwm = chip_to_starfive_ptc(chip);
	u32 data_hrc = 0;
	u32 data_lrc = 0;
	u32 period_data = 0;
	u32 duty_data = 0;
	s64 multi = pwm->approx_freq;
	s64 div = NS_PER_SECOND;
	void __iomem *reg_addr;

	if (state->duty_cycle > state->period)
		state->duty_cycle = state->period;

	while (multi % 10 == 0 && div % 10 == 0 && multi > 0 && div > 0) {
		multi /= 10;
		div /= 10;
	}

	period_data = (u32)(state->period * multi / div);
	if (abs(period_data * div / multi - state->period)
	    > abs((period_data + 1) * div / multi - state->period) ||
	    (state->period > 0 && period_data == 0))
		period_data += 1;

	if (state->enabled) {
		duty_data = (u32)(state->duty_cycle * multi / div);
		if (abs(duty_data * div / multi - state->duty_cycle)
		    > abs((duty_data + 1) * div / multi - state->duty_cycle) ||
		    (state->duty_cycle > 0 && duty_data == 0))
			duty_data += 1;
	} else {
		duty_data = 0;
	}

	if (state->polarity == PWM_POLARITY_NORMAL)
		data_hrc = period_data - duty_data;
	else
		data_hrc = duty_data;

	data_lrc = period_data;

	reg_addr = REG_PTC_RPTC_HRC(pwm->regs, dev->hwpwm);
	iowrite32(data_hrc, reg_addr);

	reg_addr = REG_PTC_RPTC_LRC(pwm->regs, dev->hwpwm);
	iowrite32(data_lrc, reg_addr);

	reg_addr = REG_PTC_RPTC_CNTR(pwm->regs, dev->hwpwm);
	iowrite32(0, reg_addr);

	reg_addr = REG_PTC_RPTC_CTRL(pwm->regs, dev->hwpwm);
	iowrite32(PTC_EN | PTC_OE, reg_addr);

	return 0;
}

static const struct pwm_ops starfive_pwm_ptc_ops = {
	.get_state	= starfive_pwm_ptc_get_state,
	.apply		= (void *)starfive_pwm_ptc_apply,
	.owner		= THIS_MODULE,
};

static int starfive_pwm_ptc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct starfive_pwm_ptc_device *pwm;
	struct pwm_chip *chip;
	struct resource *res;
	unsigned int clk_apb_freq;
	int ret;

	pwm = devm_kzalloc(dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	chip = &pwm->chip;
	chip->dev = dev;
	chip->ops = &starfive_pwm_ptc_ops;

	chip->of_pwm_n_cells = OF_PWM_N_CELLS;
	chip->base = -1;

	ret = of_property_read_u32(node, "starfive,npwm", &chip->npwm);
	if (ret < 0 || chip->npwm > MAX_PWM)
		chip->npwm = MAX_PWM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pwm->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(pwm->regs)) {
		dev_err(dev, "Unable to map IO resources\n");
		return PTR_ERR(pwm->regs);
	}

	pwm->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(pwm->clk)) {
		dev_err(dev, "Unable to get pwm clock\n");
		return PTR_ERR(pwm->clk);
	}
	pwm->rst = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(pwm->rst)) {
		dev_err(dev, "Unable to get pwm reset\n");
		return PTR_ERR(pwm->rst);
	}

	ret = clk_prepare_enable(pwm->clk);
	if (ret) {
		dev_err(dev,
			"Failed to enable pwm clock, %d\n", ret);
		return ret;
	}
	reset_control_deassert(pwm->rst);

	ret = of_property_read_u32(node, "starfive,approx-freq",
				   &clk_apb_freq);
	if (!ret)
		pwm->approx_freq = clk_apb_freq;
	else
		pwm->approx_freq = DEFAULT_FREQ_HZ;

	clk_apb_freq = (unsigned int)clk_get_rate(pwm->clk);
	if (!clk_apb_freq)
		dev_warn(dev,
			 "get pwm apb clock rate failed.\n");
	else
		pwm->approx_freq = clk_apb_freq;

	ret = pwmchip_add(chip);
	if (ret < 0) {
		dev_err(dev, "cannot register PTC: %d\n", ret);
		clk_disable_unprepare(pwm->clk);
		return ret;
	}

	platform_set_drvdata(pdev, pwm);

	return 0;
}

static int starfive_pwm_ptc_remove(struct platform_device *dev)
{
	struct starfive_pwm_ptc_device *pwm = platform_get_drvdata(dev);
	struct pwm_chip *chip = &pwm->chip;

	clk_disable_unprepare(pwm->clk);
	pwmchip_remove(chip);

	return 0;
}

static const struct of_device_id starfive_pwm_ptc_of_match[] = {
	{ .compatible = "starfive,pwm" },
	{},
};
MODULE_DEVICE_TABLE(of, starfive_pwm_ptc_of_match);

static struct platform_driver starfive_pwm_ptc_driver = {
	.probe = starfive_pwm_ptc_probe,
	.remove = starfive_pwm_ptc_remove,
	.driver = {
		.name = "pwm-starfive-ptc",
		.of_match_table = of_match_ptr(starfive_pwm_ptc_of_match),
	},
};
module_platform_driver(starfive_pwm_ptc_driver);

MODULE_AUTHOR("Jenny Zhang <jenny.zhang@starfivetech.com>");
MODULE_AUTHOR("Hal Feng <hal.feng@starfivetech.com>");
MODULE_DESCRIPTION("StarFive PWM PTC driver");
MODULE_LICENSE("GPL v2");
