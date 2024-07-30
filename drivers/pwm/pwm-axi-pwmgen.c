// SPDX-License-Identifier: GPL-2.0
/*
 * Analog Devices AXI PWM generator
 *
 * Copyright 2024 Analog Devices Inc.
 * Copyright 2024 Baylibre SAS
 *
 * Device docs: https://analogdevicesinc.github.io/hdl/library/axi_pwm_gen/index.html
 *
 * Limitations:
 * - The writes to registers for period and duty are shadowed until
 *   LOAD_CONFIG is written to AXI_PWMGEN_REG_CONFIG, at which point
 *   they take effect.
 * - Writing LOAD_CONFIG also has the effect of re-synchronizing all
 *   enabled channels, which could cause glitching on other channels. It
 *   is therefore expected that channels are assigned harmonic periods
 *   and all have a single user coordinating this.
 * - Supports normal polarity. Does not support changing polarity.
 * - On disable, the PWM output becomes low (inactive).
 */
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/fpga/adi-axi-common.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define AXI_PWMGEN_REG_CORE_VERSION	0x00
#define AXI_PWMGEN_REG_ID		0x04
#define AXI_PWMGEN_REG_SCRATCHPAD	0x08
#define AXI_PWMGEN_REG_CORE_MAGIC	0x0C
#define AXI_PWMGEN_REG_CONFIG		0x10
#define AXI_PWMGEN_REG_NPWM		0x14
#define AXI_PWMGEN_CHX_PERIOD(ch)	(0x40 + (4 * (ch)))
#define AXI_PWMGEN_CHX_DUTY(ch)		(0x80 + (4 * (ch)))
#define AXI_PWMGEN_CHX_OFFSET(ch)	(0xC0 + (4 * (ch)))
#define AXI_PWMGEN_REG_CORE_MAGIC_VAL	0x601A3471 /* Identification number to test during setup */
#define AXI_PWMGEN_LOAD_CONFIG		BIT(1)
#define AXI_PWMGEN_REG_CONFIG_RESET	BIT(0)

struct axi_pwmgen_ddata {
	struct regmap *regmap;
	unsigned long clk_rate_hz;
};

static const struct regmap_config axi_pwmgen_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xFC,
};

static int axi_pwmgen_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct axi_pwmgen_ddata *ddata = pwmchip_get_drvdata(chip);
	unsigned int ch = pwm->hwpwm;
	struct regmap *regmap = ddata->regmap;
	u64 period_cnt, duty_cnt;
	int ret;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (state->enabled) {
		period_cnt = mul_u64_u64_div_u64(state->period, ddata->clk_rate_hz, NSEC_PER_SEC);
		if (period_cnt > UINT_MAX)
			period_cnt = UINT_MAX;

		if (period_cnt == 0)
			return -EINVAL;

		ret = regmap_write(regmap, AXI_PWMGEN_CHX_PERIOD(ch), period_cnt);
		if (ret)
			return ret;

		duty_cnt = mul_u64_u64_div_u64(state->duty_cycle, ddata->clk_rate_hz, NSEC_PER_SEC);
		if (duty_cnt > UINT_MAX)
			duty_cnt = UINT_MAX;

		ret = regmap_write(regmap, AXI_PWMGEN_CHX_DUTY(ch), duty_cnt);
		if (ret)
			return ret;
	} else {
		ret = regmap_write(regmap, AXI_PWMGEN_CHX_PERIOD(ch), 0);
		if (ret)
			return ret;

		ret = regmap_write(regmap, AXI_PWMGEN_CHX_DUTY(ch), 0);
		if (ret)
			return ret;
	}

	return regmap_write(regmap, AXI_PWMGEN_REG_CONFIG, AXI_PWMGEN_LOAD_CONFIG);
}

static int axi_pwmgen_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				struct pwm_state *state)
{
	struct axi_pwmgen_ddata *ddata = pwmchip_get_drvdata(chip);
	struct regmap *regmap = ddata->regmap;
	unsigned int ch = pwm->hwpwm;
	u32 cnt;
	int ret;

	ret = regmap_read(regmap, AXI_PWMGEN_CHX_PERIOD(ch), &cnt);
	if (ret)
		return ret;

	state->enabled = cnt != 0;

	state->period = DIV_ROUND_UP_ULL((u64)cnt * NSEC_PER_SEC, ddata->clk_rate_hz);

	ret = regmap_read(regmap, AXI_PWMGEN_CHX_DUTY(ch), &cnt);
	if (ret)
		return ret;

	state->duty_cycle = DIV_ROUND_UP_ULL((u64)cnt * NSEC_PER_SEC, ddata->clk_rate_hz);

	state->polarity = PWM_POLARITY_NORMAL;

	return 0;
}

static const struct pwm_ops axi_pwmgen_pwm_ops = {
	.apply = axi_pwmgen_apply,
	.get_state = axi_pwmgen_get_state,
};

static int axi_pwmgen_setup(struct regmap *regmap, struct device *dev)
{
	int ret;
	u32 val;

	ret = regmap_read(regmap, AXI_PWMGEN_REG_CORE_MAGIC, &val);
	if (ret)
		return ret;

	if (val != AXI_PWMGEN_REG_CORE_MAGIC_VAL)
		return dev_err_probe(dev, -ENODEV,
			"failed to read expected value from register: got %08x, expected %08x\n",
			val, AXI_PWMGEN_REG_CORE_MAGIC_VAL);

	ret = regmap_read(regmap, AXI_PWMGEN_REG_CORE_VERSION, &val);
	if (ret)
		return ret;

	if (ADI_AXI_PCORE_VER_MAJOR(val) != 2) {
		return dev_err_probe(dev, -ENODEV, "Unsupported peripheral version %u.%u.%u\n",
			ADI_AXI_PCORE_VER_MAJOR(val),
			ADI_AXI_PCORE_VER_MINOR(val),
			ADI_AXI_PCORE_VER_PATCH(val));
	}

	/* Enable the core */
	ret = regmap_clear_bits(regmap, AXI_PWMGEN_REG_CONFIG, AXI_PWMGEN_REG_CONFIG_RESET);
	if (ret)
		return ret;

	ret = regmap_read(regmap, AXI_PWMGEN_REG_NPWM, &val);
	if (ret)
		return ret;

	/* Return the number of PWMs */
	return val;
}

static int axi_pwmgen_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	struct pwm_chip *chip;
	struct axi_pwmgen_ddata *ddata;
	struct clk *clk;
	void __iomem *io_base;
	int ret;

	io_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	regmap = devm_regmap_init_mmio(dev, io_base, &axi_pwmgen_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "failed to init register map\n");

	ret = axi_pwmgen_setup(regmap, dev);
	if (ret < 0)
		return ret;

	chip = devm_pwmchip_alloc(dev, ret, sizeof(*ddata));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	ddata = pwmchip_get_drvdata(chip);
	ddata->regmap = regmap;

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "failed to get clock\n");

	ret = devm_clk_rate_exclusive_get(dev, clk);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get exclusive rate\n");

	ddata->clk_rate_hz = clk_get_rate(clk);
	if (!ddata->clk_rate_hz || ddata->clk_rate_hz > NSEC_PER_SEC)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid clock rate: %lu\n", ddata->clk_rate_hz);

	chip->ops = &axi_pwmgen_pwm_ops;
	chip->atomic = true;

	ret = devm_pwmchip_add(dev, chip);
	if (ret)
		return dev_err_probe(dev, ret, "could not add PWM chip\n");

	return 0;
}

static const struct of_device_id axi_pwmgen_ids[] = {
	{ .compatible = "adi,axi-pwmgen-2.00.a" },
	{ }
};
MODULE_DEVICE_TABLE(of, axi_pwmgen_ids);

static struct platform_driver axi_pwmgen_driver = {
	.driver = {
		.name = "axi-pwmgen",
		.of_match_table = axi_pwmgen_ids,
	},
	.probe = axi_pwmgen_probe,
};
module_platform_driver(axi_pwmgen_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sergiu Cuciurean <sergiu.cuciurean@analog.com>");
MODULE_AUTHOR("Trevor Gamblin <tgamblin@baylibre.com>");
MODULE_DESCRIPTION("Driver for the Analog Devices AXI PWM generator");
