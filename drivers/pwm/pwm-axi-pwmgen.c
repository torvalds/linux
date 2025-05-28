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
 *   LOAD_CONFIG is written to AXI_PWMGEN_REG_RSTN, at which point
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
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define AXI_PWMGEN_REG_ID		0x04
#define AXI_PWMGEN_REG_SCRATCHPAD	0x08
#define AXI_PWMGEN_REG_CORE_MAGIC	0x0C
#define AXI_PWMGEN_REG_RSTN		0x10
#define   AXI_PWMGEN_REG_RSTN_LOAD_CONFIG	BIT(1)
#define   AXI_PWMGEN_REG_RSTN_RESET		BIT(0)
#define AXI_PWMGEN_REG_NPWM		0x14
#define AXI_PWMGEN_REG_CONFIG		0x18
#define   AXI_PWMGEN_REG_CONFIG_FORCE_ALIGN	BIT(1)
#define AXI_PWMGEN_CHX_PERIOD(ch)	(0x40 + (4 * (ch)))
#define AXI_PWMGEN_CHX_DUTY(ch)		(0x80 + (4 * (ch)))
#define AXI_PWMGEN_CHX_OFFSET(ch)	(0xC0 + (4 * (ch)))
#define AXI_PWMGEN_REG_CORE_MAGIC_VAL	0x601A3471 /* Identification number to test during setup */

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

/* This represents a hardware configuration for one channel */
struct axi_pwmgen_waveform {
	u32 period_cnt;
	u32 duty_cycle_cnt;
	u32 duty_offset_cnt;
};

static struct axi_pwmgen_ddata *axi_pwmgen_ddata_from_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int axi_pwmgen_round_waveform_tohw(struct pwm_chip *chip,
					  struct pwm_device *pwm,
					  const struct pwm_waveform *wf,
					  void *_wfhw)
{
	struct axi_pwmgen_waveform *wfhw = _wfhw;
	struct axi_pwmgen_ddata *ddata = axi_pwmgen_ddata_from_chip(chip);
	int ret = 0;

	if (wf->period_length_ns == 0) {
		*wfhw = (struct axi_pwmgen_waveform){
			.period_cnt = 0,
			.duty_cycle_cnt = 0,
			.duty_offset_cnt = 0,
		};
	} else {
		/* With ddata->clk_rate_hz < NSEC_PER_SEC this won't overflow. */
		wfhw->period_cnt = min_t(u64,
					 mul_u64_u32_div(wf->period_length_ns, ddata->clk_rate_hz, NSEC_PER_SEC),
					 U32_MAX);

		if (wfhw->period_cnt == 0) {
			/*
			 * The specified period is too short for the hardware.
			 * So round up .period_cnt to 1 (i.e. the smallest
			 * possible period). With .duty_cycle and .duty_offset
			 * being less than or equal to .period, their rounded
			 * value must be 0.
			 */
			wfhw->period_cnt = 1;
			wfhw->duty_cycle_cnt = 0;
			wfhw->duty_offset_cnt = 0;
			ret = 1;
		} else {
			wfhw->duty_cycle_cnt = min_t(u64,
						     mul_u64_u32_div(wf->duty_length_ns, ddata->clk_rate_hz, NSEC_PER_SEC),
						     U32_MAX);
			wfhw->duty_offset_cnt = min_t(u64,
						      mul_u64_u32_div(wf->duty_offset_ns, ddata->clk_rate_hz, NSEC_PER_SEC),
						      U32_MAX);
		}
	}

	dev_dbg(&chip->dev, "pwm#%u: %lld/%lld [+%lld] @%lu -> PERIOD: %08x, DUTY: %08x, OFFSET: %08x\n",
		pwm->hwpwm, wf->duty_length_ns, wf->period_length_ns, wf->duty_offset_ns,
		ddata->clk_rate_hz, wfhw->period_cnt, wfhw->duty_cycle_cnt, wfhw->duty_offset_cnt);

	return ret;
}

static int axi_pwmgen_round_waveform_fromhw(struct pwm_chip *chip, struct pwm_device *pwm,
					     const void *_wfhw, struct pwm_waveform *wf)
{
	const struct axi_pwmgen_waveform *wfhw = _wfhw;
	struct axi_pwmgen_ddata *ddata = axi_pwmgen_ddata_from_chip(chip);

	wf->period_length_ns = DIV64_U64_ROUND_UP((u64)wfhw->period_cnt * NSEC_PER_SEC,
					ddata->clk_rate_hz);

	wf->duty_length_ns = DIV64_U64_ROUND_UP((u64)wfhw->duty_cycle_cnt * NSEC_PER_SEC,
					    ddata->clk_rate_hz);

	wf->duty_offset_ns = DIV64_U64_ROUND_UP((u64)wfhw->duty_offset_cnt * NSEC_PER_SEC,
					     ddata->clk_rate_hz);

	return 0;
}

static int axi_pwmgen_write_waveform(struct pwm_chip *chip,
				     struct pwm_device *pwm,
				     const void *_wfhw)
{
	const struct axi_pwmgen_waveform *wfhw = _wfhw;
	struct axi_pwmgen_ddata *ddata = axi_pwmgen_ddata_from_chip(chip);
	struct regmap *regmap = ddata->regmap;
	unsigned int ch = pwm->hwpwm;
	int ret;

	ret = regmap_write(regmap, AXI_PWMGEN_CHX_PERIOD(ch), wfhw->period_cnt);
	if (ret)
		return ret;

	ret = regmap_write(regmap, AXI_PWMGEN_CHX_DUTY(ch), wfhw->duty_cycle_cnt);
	if (ret)
		return ret;

	ret = regmap_write(regmap, AXI_PWMGEN_CHX_OFFSET(ch), wfhw->duty_offset_cnt);
	if (ret)
		return ret;

	return regmap_write(regmap, AXI_PWMGEN_REG_RSTN, AXI_PWMGEN_REG_RSTN_LOAD_CONFIG);
}

static int axi_pwmgen_read_waveform(struct pwm_chip *chip,
				    struct pwm_device *pwm,
				    void *_wfhw)
{
	struct axi_pwmgen_waveform *wfhw = _wfhw;
	struct axi_pwmgen_ddata *ddata = axi_pwmgen_ddata_from_chip(chip);
	struct regmap *regmap = ddata->regmap;
	unsigned int ch = pwm->hwpwm;
	int ret;

	ret = regmap_read(regmap, AXI_PWMGEN_CHX_PERIOD(ch), &wfhw->period_cnt);
	if (ret)
		return ret;

	ret = regmap_read(regmap, AXI_PWMGEN_CHX_DUTY(ch), &wfhw->duty_cycle_cnt);
	if (ret)
		return ret;

	ret = regmap_read(regmap, AXI_PWMGEN_CHX_OFFSET(ch), &wfhw->duty_offset_cnt);
	if (ret)
		return ret;

	if (wfhw->duty_cycle_cnt > wfhw->period_cnt)
		wfhw->duty_cycle_cnt = wfhw->period_cnt;

	/* XXX: is this the actual behaviour of the hardware? */
	if (wfhw->duty_offset_cnt >= wfhw->period_cnt) {
		wfhw->duty_cycle_cnt = 0;
		wfhw->duty_offset_cnt = 0;
	}

	return 0;
}

static const struct pwm_ops axi_pwmgen_pwm_ops = {
	.sizeof_wfhw = sizeof(struct axi_pwmgen_waveform),
	.round_waveform_tohw = axi_pwmgen_round_waveform_tohw,
	.round_waveform_fromhw = axi_pwmgen_round_waveform_fromhw,
	.read_waveform = axi_pwmgen_read_waveform,
	.write_waveform = axi_pwmgen_write_waveform,
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

	ret = regmap_read(regmap, ADI_AXI_REG_VERSION, &val);
	if (ret)
		return ret;

	if (ADI_AXI_PCORE_VER_MAJOR(val) != 2) {
		return dev_err_probe(dev, -ENODEV, "Unsupported peripheral version %u.%u.%u\n",
			ADI_AXI_PCORE_VER_MAJOR(val),
			ADI_AXI_PCORE_VER_MINOR(val),
			ADI_AXI_PCORE_VER_PATCH(val));
	}

	/* Enable the core */
	ret = regmap_clear_bits(regmap, AXI_PWMGEN_REG_RSTN, AXI_PWMGEN_REG_RSTN_RESET);
	if (ret)
		return ret;

	/*
	 * Enable force align so that changes to PWM period and duty cycle take
	 * effect immediately. Otherwise, the effect of the change is delayed
	 * until the period of all channels run out, which can be long after the
	 * apply function returns.
	 */
	ret = regmap_set_bits(regmap, AXI_PWMGEN_REG_CONFIG, AXI_PWMGEN_REG_CONFIG_FORCE_ALIGN);
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
