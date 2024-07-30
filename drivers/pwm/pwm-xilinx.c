// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Sean Anderson <sean.anderson@seco.com>
 *
 * Limitations:
 * - When changing both duty cycle and period, we may end up with one cycle
 *   with the old duty cycle and the new period. This is because the counters
 *   may only be reloaded by first stopping them, or by letting them be
 *   automatically reloaded at the end of a cycle. If this automatic reload
 *   happens after we set TLR0 but before we set TLR1 then we will have a
 *   bad cycle. This could probably be fixed by reading TCR0 just before
 *   reprogramming, but I think it would add complexity for little gain.
 * - Cannot produce 100% duty cycle by configuring the TLRs. This might be
 *   possible by stopping the counters at an appropriate point in the cycle,
 *   but this is not (yet) implemented.
 * - Only produces "normal" output.
 * - Always produces low output if disabled.
 */

#include <clocksource/timer-xilinx.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>

/*
 * The following functions are "common" to drivers for this device, and may be
 * exported at a future date.
 */
u32 xilinx_timer_tlr_cycles(struct xilinx_timer_priv *priv, u32 tcsr,
			    u64 cycles)
{
	WARN_ON(cycles < 2 || cycles - 2 > priv->max);

	if (tcsr & TCSR_UDT)
		return cycles - 2;
	return priv->max - cycles + 2;
}

unsigned int xilinx_timer_get_period(struct xilinx_timer_priv *priv,
				     u32 tlr, u32 tcsr)
{
	u64 cycles;

	if (tcsr & TCSR_UDT)
		cycles = tlr + 2;
	else
		cycles = (u64)priv->max - tlr + 2;

	/* cycles has a max of 2^32 + 2, so we can't overflow */
	return DIV64_U64_ROUND_UP(cycles * NSEC_PER_SEC,
				  clk_get_rate(priv->clk));
}

/*
 * The idea here is to capture whether the PWM is actually running (e.g.
 * because we or the bootloader set it up) and we need to be careful to ensure
 * we don't cause a glitch. According to the data sheet, to enable the PWM we
 * need to
 *
 * - Set both timers to generate mode (MDT=1)
 * - Set both timers to PWM mode (PWMA=1)
 * - Enable the generate out signals (GENT=1)
 *
 * In addition,
 *
 * - The timer must be running (ENT=1)
 * - The timer must auto-reload TLR into TCR (ARHT=1)
 * - We must not be in the process of loading TLR into TCR (LOAD=0)
 * - Cascade mode must be disabled (CASC=0)
 *
 * If any of these differ from usual, then the PWM is either disabled, or is
 * running in a mode that this driver does not support.
 */
#define TCSR_PWM_SET (TCSR_GENT | TCSR_ARHT | TCSR_ENT | TCSR_PWMA)
#define TCSR_PWM_CLEAR (TCSR_MDT | TCSR_LOAD)
#define TCSR_PWM_MASK (TCSR_PWM_SET | TCSR_PWM_CLEAR)

static inline struct xilinx_timer_priv
*xilinx_pwm_chip_to_priv(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static bool xilinx_timer_pwm_enabled(u32 tcsr0, u32 tcsr1)
{
	return ((TCSR_PWM_MASK | TCSR_CASC) & tcsr0) == TCSR_PWM_SET &&
		(TCSR_PWM_MASK & tcsr1) == TCSR_PWM_SET;
}

static int xilinx_pwm_apply(struct pwm_chip *chip, struct pwm_device *unused,
			    const struct pwm_state *state)
{
	struct xilinx_timer_priv *priv = xilinx_pwm_chip_to_priv(chip);
	u32 tlr0, tlr1, tcsr0, tcsr1;
	u64 period_cycles, duty_cycles;
	unsigned long rate;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	/*
	 * To be representable by TLR, cycles must be between 2 and
	 * priv->max + 2. To enforce this we can reduce the cycles, but we may
	 * not increase them. Caveat emptor: while this does result in more
	 * predictable rounding, it may also result in a completely different
	 * duty cycle (% high time) than what was requested.
	 */
	rate = clk_get_rate(priv->clk);
	/* Avoid overflow */
	period_cycles = min_t(u64, state->period, U32_MAX * NSEC_PER_SEC);
	period_cycles = mul_u64_u32_div(period_cycles, rate, NSEC_PER_SEC);
	period_cycles = min_t(u64, period_cycles, priv->max + 2);
	if (period_cycles < 2)
		return -ERANGE;

	/* Same thing for duty cycles */
	duty_cycles = min_t(u64, state->duty_cycle, U32_MAX * NSEC_PER_SEC);
	duty_cycles = mul_u64_u32_div(duty_cycles, rate, NSEC_PER_SEC);
	duty_cycles = min_t(u64, duty_cycles, priv->max + 2);

	/*
	 * If we specify 100% duty cycle, we will get 0% instead, so decrease
	 * the duty cycle count by one.
	 */
	if (duty_cycles >= period_cycles)
		duty_cycles = period_cycles - 1;

	/* Round down to 0% duty cycle for unrepresentable duty cycles */
	if (duty_cycles < 2)
		duty_cycles = period_cycles;

	regmap_read(priv->map, TCSR0, &tcsr0);
	regmap_read(priv->map, TCSR1, &tcsr1);
	tlr0 = xilinx_timer_tlr_cycles(priv, tcsr0, period_cycles);
	tlr1 = xilinx_timer_tlr_cycles(priv, tcsr1, duty_cycles);
	regmap_write(priv->map, TLR0, tlr0);
	regmap_write(priv->map, TLR1, tlr1);

	if (state->enabled) {
		/*
		 * If the PWM is already running, then the counters will be
		 * reloaded at the end of the current cycle.
		 */
		if (!xilinx_timer_pwm_enabled(tcsr0, tcsr1)) {
			/* Load TLR into TCR */
			regmap_write(priv->map, TCSR0, tcsr0 | TCSR_LOAD);
			regmap_write(priv->map, TCSR1, tcsr1 | TCSR_LOAD);
			/* Enable timers all at once with ENALL */
			tcsr0 = (TCSR_PWM_SET & ~TCSR_ENT) | (tcsr0 & TCSR_UDT);
			tcsr1 = TCSR_PWM_SET | TCSR_ENALL | (tcsr1 & TCSR_UDT);
			regmap_write(priv->map, TCSR0, tcsr0);
			regmap_write(priv->map, TCSR1, tcsr1);
		}
	} else {
		regmap_write(priv->map, TCSR0, 0);
		regmap_write(priv->map, TCSR1, 0);
	}

	return 0;
}

static int xilinx_pwm_get_state(struct pwm_chip *chip,
				struct pwm_device *unused,
				struct pwm_state *state)
{
	struct xilinx_timer_priv *priv = xilinx_pwm_chip_to_priv(chip);
	u32 tlr0, tlr1, tcsr0, tcsr1;

	regmap_read(priv->map, TLR0, &tlr0);
	regmap_read(priv->map, TLR1, &tlr1);
	regmap_read(priv->map, TCSR0, &tcsr0);
	regmap_read(priv->map, TCSR1, &tcsr1);
	state->period = xilinx_timer_get_period(priv, tlr0, tcsr0);
	state->duty_cycle = xilinx_timer_get_period(priv, tlr1, tcsr1);
	state->enabled = xilinx_timer_pwm_enabled(tcsr0, tcsr1);
	state->polarity = PWM_POLARITY_NORMAL;

	/*
	 * 100% duty cycle results in constant low output. This may be (very)
	 * wrong if rate > 1 GHz, so fix this if you have such hardware :)
	 */
	if (state->period == state->duty_cycle)
		state->duty_cycle = 0;

	return 0;
}

static const struct pwm_ops xilinx_pwm_ops = {
	.apply = xilinx_pwm_apply,
	.get_state = xilinx_pwm_get_state,
};

static const struct regmap_config xilinx_pwm_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.max_register = TCR1,
};

static int xilinx_pwm_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct xilinx_timer_priv *priv;
	struct pwm_chip *chip;
	u32 pwm_cells, one_timer, width;
	void __iomem *regs;

	/* If there are no PWM cells, this binding is for a timer */
	ret = of_property_read_u32(np, "#pwm-cells", &pwm_cells);
	if (ret == -EINVAL)
		return -ENODEV;
	if (ret)
		return dev_err_probe(dev, ret, "could not read #pwm-cells\n");

	chip = devm_pwmchip_alloc(dev, 1, sizeof(*priv));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	priv = xilinx_pwm_chip_to_priv(chip);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	priv->map = devm_regmap_init_mmio(dev, regs,
					  &xilinx_pwm_regmap_config);
	if (IS_ERR(priv->map))
		return dev_err_probe(dev, PTR_ERR(priv->map),
				     "Could not create regmap\n");

	ret = of_property_read_u32(np, "xlnx,one-timer-only", &one_timer);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Could not read xlnx,one-timer-only\n");

	if (one_timer)
		return dev_err_probe(dev, -EINVAL,
				     "Two timers required for PWM mode\n");

	ret = of_property_read_u32(np, "xlnx,count-width", &width);
	if (ret == -EINVAL)
		width = 32;
	else if (ret)
		return dev_err_probe(dev, ret,
				     "Could not read xlnx,count-width\n");

	if (width != 8 && width != 16 && width != 32)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid counter width %d\n", width);
	priv->max = BIT_ULL(width) - 1;

	/*
	 * The polarity of the Generate Out signals must be active high for PWM
	 * mode to work. We could determine this from the device tree, but
	 * alas, such properties are not allowed to be used.
	 */

	priv->clk = devm_clk_get_enabled(dev, "s_axi_aclk");
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "Could not get clock\n");

	ret = devm_clk_rate_exclusive_get(dev, priv->clk);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to lock clock rate\n");

	chip->ops = &xilinx_pwm_ops;
	ret = devm_pwmchip_add(dev, chip);
	if (ret)
		return dev_err_probe(dev, ret, "Could not register PWM chip\n");

	return 0;
}

static const struct of_device_id xilinx_pwm_of_match[] = {
	{ .compatible = "xlnx,xps-timer-1.00.a", },
	{},
};
MODULE_DEVICE_TABLE(of, xilinx_pwm_of_match);

static struct platform_driver xilinx_pwm_driver = {
	.probe = xilinx_pwm_probe,
	.driver = {
		.name = "xilinx-pwm",
		.of_match_table = of_match_ptr(xilinx_pwm_of_match),
	},
};
module_platform_driver(xilinx_pwm_driver);

MODULE_ALIAS("platform:xilinx-pwm");
MODULE_DESCRIPTION("PWM driver for Xilinx LogiCORE IP AXI Timer");
MODULE_LICENSE("GPL");
