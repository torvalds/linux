/*
 * Driver for Atmel Pulse Width Modulation Controller
 *
 * Copyright (C) 2013 Atmel Corporation
 *		 Bo Shen <voice.shen@atmel.com>
 *
 * Licensed under GPLv2.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>

/* The following is global registers for PWM controller */
#define PWM_ENA			0x04
#define PWM_DIS			0x08
#define PWM_SR			0x0C
#define PWM_ISR			0x1C
/* Bit field in SR */
#define PWM_SR_ALL_CH_ON	0x0F

/* The following register is PWM channel related registers */
#define PWM_CH_REG_OFFSET	0x200
#define PWM_CH_REG_SIZE		0x20

#define PWM_CMR			0x0
/* Bit field in CMR */
#define PWM_CMR_CPOL		(1 << 9)
#define PWM_CMR_UPD_CDTY	(1 << 10)
#define PWM_CMR_CPRE_MSK	0xF

/* The following registers for PWM v1 */
#define PWMV1_CDTY		0x04
#define PWMV1_CPRD		0x08
#define PWMV1_CUPD		0x10

/* The following registers for PWM v2 */
#define PWMV2_CDTY		0x04
#define PWMV2_CDTYUPD		0x08
#define PWMV2_CPRD		0x0C
#define PWMV2_CPRDUPD		0x10

/*
 * Max value for duty and period
 *
 * Although the duty and period register is 32 bit,
 * however only the LSB 16 bits are significant.
 */
#define PWM_MAX_DTY		0xFFFF
#define PWM_MAX_PRD		0xFFFF
#define PRD_MAX_PRES		10

struct atmel_pwm_chip {
	struct pwm_chip chip;
	struct clk *clk;
	void __iomem *base;

	unsigned int updated_pwms;
	/* ISR is cleared when read, ensure only one thread does that */
	struct mutex isr_lock;

	void (*config)(struct pwm_chip *chip, struct pwm_device *pwm,
		       unsigned long dty, unsigned long prd);
};

static inline struct atmel_pwm_chip *to_atmel_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct atmel_pwm_chip, chip);
}

static inline u32 atmel_pwm_readl(struct atmel_pwm_chip *chip,
				  unsigned long offset)
{
	return readl_relaxed(chip->base + offset);
}

static inline void atmel_pwm_writel(struct atmel_pwm_chip *chip,
				    unsigned long offset, unsigned long val)
{
	writel_relaxed(val, chip->base + offset);
}

static inline u32 atmel_pwm_ch_readl(struct atmel_pwm_chip *chip,
				     unsigned int ch, unsigned long offset)
{
	unsigned long base = PWM_CH_REG_OFFSET + ch * PWM_CH_REG_SIZE;

	return readl_relaxed(chip->base + base + offset);
}

static inline void atmel_pwm_ch_writel(struct atmel_pwm_chip *chip,
				       unsigned int ch, unsigned long offset,
				       unsigned long val)
{
	unsigned long base = PWM_CH_REG_OFFSET + ch * PWM_CH_REG_SIZE;

	writel_relaxed(val, chip->base + base + offset);
}

static int atmel_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			    int duty_ns, int period_ns)
{
	struct atmel_pwm_chip *atmel_pwm = to_atmel_pwm_chip(chip);
	unsigned long prd, dty;
	unsigned long long div;
	unsigned int pres = 0;
	u32 val;
	int ret;

	if (pwm_is_enabled(pwm) && (period_ns != pwm_get_period(pwm))) {
		dev_err(chip->dev, "cannot change PWM period while enabled\n");
		return -EBUSY;
	}

	/* Calculate the period cycles and prescale value */
	div = (unsigned long long)clk_get_rate(atmel_pwm->clk) * period_ns;
	do_div(div, NSEC_PER_SEC);

	while (div > PWM_MAX_PRD) {
		div >>= 1;
		pres++;
	}

	if (pres > PRD_MAX_PRES) {
		dev_err(chip->dev, "pres exceeds the maximum value\n");
		return -EINVAL;
	}

	/* Calculate the duty cycles */
	prd = div;
	div *= duty_ns;
	do_div(div, period_ns);
	dty = prd - div;

	ret = clk_enable(atmel_pwm->clk);
	if (ret) {
		dev_err(chip->dev, "failed to enable PWM clock\n");
		return ret;
	}

	/* It is necessary to preserve CPOL, inside CMR */
	val = atmel_pwm_ch_readl(atmel_pwm, pwm->hwpwm, PWM_CMR);
	val = (val & ~PWM_CMR_CPRE_MSK) | (pres & PWM_CMR_CPRE_MSK);
	atmel_pwm_ch_writel(atmel_pwm, pwm->hwpwm, PWM_CMR, val);
	atmel_pwm->config(chip, pwm, dty, prd);
	mutex_lock(&atmel_pwm->isr_lock);
	atmel_pwm->updated_pwms |= atmel_pwm_readl(atmel_pwm, PWM_ISR);
	atmel_pwm->updated_pwms &= ~(1 << pwm->hwpwm);
	mutex_unlock(&atmel_pwm->isr_lock);

	clk_disable(atmel_pwm->clk);
	return ret;
}

static void atmel_pwm_config_v1(struct pwm_chip *chip, struct pwm_device *pwm,
				unsigned long dty, unsigned long prd)
{
	struct atmel_pwm_chip *atmel_pwm = to_atmel_pwm_chip(chip);
	unsigned int val;


	atmel_pwm_ch_writel(atmel_pwm, pwm->hwpwm, PWMV1_CUPD, dty);

	val = atmel_pwm_ch_readl(atmel_pwm, pwm->hwpwm, PWM_CMR);
	val &= ~PWM_CMR_UPD_CDTY;
	atmel_pwm_ch_writel(atmel_pwm, pwm->hwpwm, PWM_CMR, val);

	/*
	 * If the PWM channel is enabled, only update CDTY by using the update
	 * register, it needs to set bit 10 of CMR to 0
	 */
	if (pwm_is_enabled(pwm))
		return;
	/*
	 * If the PWM channel is disabled, write value to duty and period
	 * registers directly.
	 */
	atmel_pwm_ch_writel(atmel_pwm, pwm->hwpwm, PWMV1_CDTY, dty);
	atmel_pwm_ch_writel(atmel_pwm, pwm->hwpwm, PWMV1_CPRD, prd);
}

static void atmel_pwm_config_v2(struct pwm_chip *chip, struct pwm_device *pwm,
				unsigned long dty, unsigned long prd)
{
	struct atmel_pwm_chip *atmel_pwm = to_atmel_pwm_chip(chip);

	if (pwm_is_enabled(pwm)) {
		/*
		 * If the PWM channel is enabled, using the duty update register
		 * to update the value.
		 */
		atmel_pwm_ch_writel(atmel_pwm, pwm->hwpwm, PWMV2_CDTYUPD, dty);
	} else {
		/*
		 * If the PWM channel is disabled, write value to duty and
		 * period registers directly.
		 */
		atmel_pwm_ch_writel(atmel_pwm, pwm->hwpwm, PWMV2_CDTY, dty);
		atmel_pwm_ch_writel(atmel_pwm, pwm->hwpwm, PWMV2_CPRD, prd);
	}
}

static int atmel_pwm_set_polarity(struct pwm_chip *chip, struct pwm_device *pwm,
				  enum pwm_polarity polarity)
{
	struct atmel_pwm_chip *atmel_pwm = to_atmel_pwm_chip(chip);
	u32 val;
	int ret;

	val = atmel_pwm_ch_readl(atmel_pwm, pwm->hwpwm, PWM_CMR);

	if (polarity == PWM_POLARITY_NORMAL)
		val &= ~PWM_CMR_CPOL;
	else
		val |= PWM_CMR_CPOL;

	ret = clk_enable(atmel_pwm->clk);
	if (ret) {
		dev_err(chip->dev, "failed to enable PWM clock\n");
		return ret;
	}

	atmel_pwm_ch_writel(atmel_pwm, pwm->hwpwm, PWM_CMR, val);

	clk_disable(atmel_pwm->clk);

	return 0;
}

static int atmel_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct atmel_pwm_chip *atmel_pwm = to_atmel_pwm_chip(chip);
	int ret;

	ret = clk_enable(atmel_pwm->clk);
	if (ret) {
		dev_err(chip->dev, "failed to enable PWM clock\n");
		return ret;
	}

	atmel_pwm_writel(atmel_pwm, PWM_ENA, 1 << pwm->hwpwm);

	return 0;
}

static void atmel_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct atmel_pwm_chip *atmel_pwm = to_atmel_pwm_chip(chip);
	unsigned long timeout = jiffies + 2 * HZ;

	/*
	 * Wait for at least a complete period to have passed before disabling a
	 * channel to be sure that CDTY has been updated
	 */
	mutex_lock(&atmel_pwm->isr_lock);
	atmel_pwm->updated_pwms |= atmel_pwm_readl(atmel_pwm, PWM_ISR);

	while (!(atmel_pwm->updated_pwms & (1 << pwm->hwpwm)) &&
	       time_before(jiffies, timeout)) {
		usleep_range(10, 100);
		atmel_pwm->updated_pwms |= atmel_pwm_readl(atmel_pwm, PWM_ISR);
	}

	mutex_unlock(&atmel_pwm->isr_lock);
	atmel_pwm_writel(atmel_pwm, PWM_DIS, 1 << pwm->hwpwm);

	/*
	 * Wait for the PWM channel disable operation to be effective before
	 * stopping the clock.
	 */
	timeout = jiffies + 2 * HZ;

	while ((atmel_pwm_readl(atmel_pwm, PWM_SR) & (1 << pwm->hwpwm)) &&
	       time_before(jiffies, timeout))
		usleep_range(10, 100);

	clk_disable(atmel_pwm->clk);
}

static const struct pwm_ops atmel_pwm_ops = {
	.config = atmel_pwm_config,
	.set_polarity = atmel_pwm_set_polarity,
	.enable = atmel_pwm_enable,
	.disable = atmel_pwm_disable,
	.owner = THIS_MODULE,
};

struct atmel_pwm_data {
	void (*config)(struct pwm_chip *chip, struct pwm_device *pwm,
		       unsigned long dty, unsigned long prd);
};

static const struct atmel_pwm_data atmel_pwm_data_v1 = {
	.config = atmel_pwm_config_v1,
};

static const struct atmel_pwm_data atmel_pwm_data_v2 = {
	.config = atmel_pwm_config_v2,
};

static const struct platform_device_id atmel_pwm_devtypes[] = {
	{
		.name = "at91sam9rl-pwm",
		.driver_data = (kernel_ulong_t)&atmel_pwm_data_v1,
	}, {
		.name = "sama5d3-pwm",
		.driver_data = (kernel_ulong_t)&atmel_pwm_data_v2,
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(platform, atmel_pwm_devtypes);

static const struct of_device_id atmel_pwm_dt_ids[] = {
	{
		.compatible = "atmel,at91sam9rl-pwm",
		.data = &atmel_pwm_data_v1,
	}, {
		.compatible = "atmel,sama5d3-pwm",
		.data = &atmel_pwm_data_v2,
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, atmel_pwm_dt_ids);

static inline const struct atmel_pwm_data *
atmel_pwm_get_driver_data(struct platform_device *pdev)
{
	const struct platform_device_id *id;

	if (pdev->dev.of_node)
		return of_device_get_match_data(&pdev->dev);

	id = platform_get_device_id(pdev);

	return (struct atmel_pwm_data *)id->driver_data;
}

static int atmel_pwm_probe(struct platform_device *pdev)
{
	const struct atmel_pwm_data *data;
	struct atmel_pwm_chip *atmel_pwm;
	struct resource *res;
	int ret;

	data = atmel_pwm_get_driver_data(pdev);
	if (!data)
		return -ENODEV;

	atmel_pwm = devm_kzalloc(&pdev->dev, sizeof(*atmel_pwm), GFP_KERNEL);
	if (!atmel_pwm)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	atmel_pwm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(atmel_pwm->base))
		return PTR_ERR(atmel_pwm->base);

	atmel_pwm->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(atmel_pwm->clk))
		return PTR_ERR(atmel_pwm->clk);

	ret = clk_prepare(atmel_pwm->clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare PWM clock\n");
		return ret;
	}

	atmel_pwm->chip.dev = &pdev->dev;
	atmel_pwm->chip.ops = &atmel_pwm_ops;

	if (pdev->dev.of_node) {
		atmel_pwm->chip.of_xlate = of_pwm_xlate_with_flags;
		atmel_pwm->chip.of_pwm_n_cells = 3;
	}

	atmel_pwm->chip.base = -1;
	atmel_pwm->chip.npwm = 4;
	atmel_pwm->chip.can_sleep = true;
	atmel_pwm->config = data->config;
	atmel_pwm->updated_pwms = 0;
	mutex_init(&atmel_pwm->isr_lock);

	ret = pwmchip_add(&atmel_pwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add PWM chip %d\n", ret);
		goto unprepare_clk;
	}

	platform_set_drvdata(pdev, atmel_pwm);

	return ret;

unprepare_clk:
	clk_unprepare(atmel_pwm->clk);
	return ret;
}

static int atmel_pwm_remove(struct platform_device *pdev)
{
	struct atmel_pwm_chip *atmel_pwm = platform_get_drvdata(pdev);

	clk_unprepare(atmel_pwm->clk);
	mutex_destroy(&atmel_pwm->isr_lock);

	return pwmchip_remove(&atmel_pwm->chip);
}

static struct platform_driver atmel_pwm_driver = {
	.driver = {
		.name = "atmel-pwm",
		.of_match_table = of_match_ptr(atmel_pwm_dt_ids),
	},
	.id_table = atmel_pwm_devtypes,
	.probe = atmel_pwm_probe,
	.remove = atmel_pwm_remove,
};
module_platform_driver(atmel_pwm_driver);

MODULE_ALIAS("platform:atmel-pwm");
MODULE_AUTHOR("Bo Shen <voice.shen@atmel.com>");
MODULE_DESCRIPTION("Atmel PWM driver");
MODULE_LICENSE("GPL v2");
