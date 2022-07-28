// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of STM32 ADC driver
 *
 * Copyright (C) 2016, STMicroelectronics - All Rights Reserved
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com>.
 *
 * Inspired from: fsl-imx25-tsadc
 *
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdesc.h>
#include <linux/irqdomain.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include "stm32-adc-core.h"

#define STM32_ADC_CORE_SLEEP_DELAY_MS	2000

/* SYSCFG registers */
#define STM32MP1_SYSCFG_PMCSETR		0x04
#define STM32MP1_SYSCFG_PMCCLRR		0x44

/* SYSCFG bit fields */
#define STM32MP1_SYSCFG_ANASWVDD_MASK	BIT(9)

/* SYSCFG capability flags */
#define HAS_VBOOSTER		BIT(0)
#define HAS_ANASWVDD		BIT(1)

/**
 * struct stm32_adc_common_regs - stm32 common registers
 * @csr:	common status register offset
 * @ccr:	common control register offset
 * @eoc_msk:    array of eoc (end of conversion flag) masks in csr for adc1..n
 * @ovr_msk:    array of ovr (overrun flag) masks in csr for adc1..n
 * @ier:	interrupt enable register offset for each adc
 * @eocie_msk:	end of conversion interrupt enable mask in @ier
 */
struct stm32_adc_common_regs {
	u32 csr;
	u32 ccr;
	u32 eoc_msk[STM32_ADC_MAX_ADCS];
	u32 ovr_msk[STM32_ADC_MAX_ADCS];
	u32 ier;
	u32 eocie_msk;
};

struct stm32_adc_priv;

/**
 * struct stm32_adc_priv_cfg - stm32 core compatible configuration data
 * @regs:	common registers for all instances
 * @clk_sel:	clock selection routine
 * @max_clk_rate_hz: maximum analog clock rate (Hz, from datasheet)
 * @has_syscfg: SYSCFG capability flags
 * @num_irqs:	number of interrupt lines
 * @num_adcs:   maximum number of ADC instances in the common registers
 */
struct stm32_adc_priv_cfg {
	const struct stm32_adc_common_regs *regs;
	int (*clk_sel)(struct platform_device *, struct stm32_adc_priv *);
	u32 max_clk_rate_hz;
	unsigned int has_syscfg;
	unsigned int num_irqs;
	unsigned int num_adcs;
};

/**
 * struct stm32_adc_priv - stm32 ADC core private data
 * @irq:		irq(s) for ADC block
 * @domain:		irq domain reference
 * @aclk:		clock reference for the analog circuitry
 * @bclk:		bus clock common for all ADCs, depends on part used
 * @max_clk_rate:	desired maximum clock rate
 * @booster:		booster supply reference
 * @vdd:		vdd supply reference
 * @vdda:		vdda analog supply reference
 * @vref:		regulator reference
 * @vdd_uv:		vdd supply voltage (microvolts)
 * @vdda_uv:		vdda supply voltage (microvolts)
 * @cfg:		compatible configuration data
 * @common:		common data for all ADC instances
 * @ccr_bak:		backup CCR in low power mode
 * @syscfg:		reference to syscon, system control registers
 */
struct stm32_adc_priv {
	int				irq[STM32_ADC_MAX_ADCS];
	struct irq_domain		*domain;
	struct clk			*aclk;
	struct clk			*bclk;
	u32				max_clk_rate;
	struct regulator		*booster;
	struct regulator		*vdd;
	struct regulator		*vdda;
	struct regulator		*vref;
	int				vdd_uv;
	int				vdda_uv;
	const struct stm32_adc_priv_cfg	*cfg;
	struct stm32_adc_common		common;
	u32				ccr_bak;
	struct regmap			*syscfg;
};

static struct stm32_adc_priv *to_stm32_adc_priv(struct stm32_adc_common *com)
{
	return container_of(com, struct stm32_adc_priv, common);
}

/* STM32F4 ADC internal common clock prescaler division ratios */
static int stm32f4_pclk_div[] = {2, 4, 6, 8};

/**
 * stm32f4_adc_clk_sel() - Select stm32f4 ADC common clock prescaler
 * @pdev: platform device
 * @priv: stm32 ADC core private data
 * Select clock prescaler used for analog conversions, before using ADC.
 */
static int stm32f4_adc_clk_sel(struct platform_device *pdev,
			       struct stm32_adc_priv *priv)
{
	unsigned long rate;
	u32 val;
	int i;

	/* stm32f4 has one clk input for analog (mandatory), enforce it here */
	if (!priv->aclk) {
		dev_err(&pdev->dev, "No 'adc' clock found\n");
		return -ENOENT;
	}

	rate = clk_get_rate(priv->aclk);
	if (!rate) {
		dev_err(&pdev->dev, "Invalid clock rate: 0\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(stm32f4_pclk_div); i++) {
		if ((rate / stm32f4_pclk_div[i]) <= priv->max_clk_rate)
			break;
	}
	if (i >= ARRAY_SIZE(stm32f4_pclk_div)) {
		dev_err(&pdev->dev, "adc clk selection failed\n");
		return -EINVAL;
	}

	priv->common.rate = rate / stm32f4_pclk_div[i];
	val = readl_relaxed(priv->common.base + STM32F4_ADC_CCR);
	val &= ~STM32F4_ADC_ADCPRE_MASK;
	val |= i << STM32F4_ADC_ADCPRE_SHIFT;
	writel_relaxed(val, priv->common.base + STM32F4_ADC_CCR);

	dev_dbg(&pdev->dev, "Using analog clock source at %ld kHz\n",
		priv->common.rate / 1000);

	return 0;
}

/**
 * struct stm32h7_adc_ck_spec - specification for stm32h7 adc clock
 * @ckmode: ADC clock mode, Async or sync with prescaler.
 * @presc: prescaler bitfield for async clock mode
 * @div: prescaler division ratio
 */
struct stm32h7_adc_ck_spec {
	u32 ckmode;
	u32 presc;
	int div;
};

static const struct stm32h7_adc_ck_spec stm32h7_adc_ckmodes_spec[] = {
	/* 00: CK_ADC[1..3]: Asynchronous clock modes */
	{ 0, 0, 1 },
	{ 0, 1, 2 },
	{ 0, 2, 4 },
	{ 0, 3, 6 },
	{ 0, 4, 8 },
	{ 0, 5, 10 },
	{ 0, 6, 12 },
	{ 0, 7, 16 },
	{ 0, 8, 32 },
	{ 0, 9, 64 },
	{ 0, 10, 128 },
	{ 0, 11, 256 },
	/* HCLK used: Synchronous clock modes (1, 2 or 4 prescaler) */
	{ 1, 0, 1 },
	{ 2, 0, 2 },
	{ 3, 0, 4 },
};

static int stm32h7_adc_clk_sel(struct platform_device *pdev,
			       struct stm32_adc_priv *priv)
{
	u32 ckmode, presc, val;
	unsigned long rate;
	int i, div;

	/* stm32h7 bus clock is common for all ADC instances (mandatory) */
	if (!priv->bclk) {
		dev_err(&pdev->dev, "No 'bus' clock found\n");
		return -ENOENT;
	}

	/*
	 * stm32h7 can use either 'bus' or 'adc' clock for analog circuitry.
	 * So, choice is to have bus clock mandatory and adc clock optional.
	 * If optional 'adc' clock has been found, then try to use it first.
	 */
	if (priv->aclk) {
		/*
		 * Asynchronous clock modes (e.g. ckmode == 0)
		 * From spec: PLL output musn't exceed max rate
		 */
		rate = clk_get_rate(priv->aclk);
		if (!rate) {
			dev_err(&pdev->dev, "Invalid adc clock rate: 0\n");
			return -EINVAL;
		}

		for (i = 0; i < ARRAY_SIZE(stm32h7_adc_ckmodes_spec); i++) {
			ckmode = stm32h7_adc_ckmodes_spec[i].ckmode;
			presc = stm32h7_adc_ckmodes_spec[i].presc;
			div = stm32h7_adc_ckmodes_spec[i].div;

			if (ckmode)
				continue;

			if ((rate / div) <= priv->max_clk_rate)
				goto out;
		}
	}

	/* Synchronous clock modes (e.g. ckmode is 1, 2 or 3) */
	rate = clk_get_rate(priv->bclk);
	if (!rate) {
		dev_err(&pdev->dev, "Invalid bus clock rate: 0\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(stm32h7_adc_ckmodes_spec); i++) {
		ckmode = stm32h7_adc_ckmodes_spec[i].ckmode;
		presc = stm32h7_adc_ckmodes_spec[i].presc;
		div = stm32h7_adc_ckmodes_spec[i].div;

		if (!ckmode)
			continue;

		if ((rate / div) <= priv->max_clk_rate)
			goto out;
	}

	dev_err(&pdev->dev, "adc clk selection failed\n");
	return -EINVAL;

out:
	/* rate used later by each ADC instance to control BOOST mode */
	priv->common.rate = rate / div;

	/* Set common clock mode and prescaler */
	val = readl_relaxed(priv->common.base + STM32H7_ADC_CCR);
	val &= ~(STM32H7_CKMODE_MASK | STM32H7_PRESC_MASK);
	val |= ckmode << STM32H7_CKMODE_SHIFT;
	val |= presc << STM32H7_PRESC_SHIFT;
	writel_relaxed(val, priv->common.base + STM32H7_ADC_CCR);

	dev_dbg(&pdev->dev, "Using %s clock/%d source at %ld kHz\n",
		ckmode ? "bus" : "adc", div, priv->common.rate / 1000);

	return 0;
}

/* STM32F4 common registers definitions */
static const struct stm32_adc_common_regs stm32f4_adc_common_regs = {
	.csr = STM32F4_ADC_CSR,
	.ccr = STM32F4_ADC_CCR,
	.eoc_msk = { STM32F4_EOC1, STM32F4_EOC2, STM32F4_EOC3},
	.ovr_msk = { STM32F4_OVR1, STM32F4_OVR2, STM32F4_OVR3},
	.ier = STM32F4_ADC_CR1,
	.eocie_msk = STM32F4_EOCIE,
};

/* STM32H7 common registers definitions */
static const struct stm32_adc_common_regs stm32h7_adc_common_regs = {
	.csr = STM32H7_ADC_CSR,
	.ccr = STM32H7_ADC_CCR,
	.eoc_msk = { STM32H7_EOC_MST, STM32H7_EOC_SLV},
	.ovr_msk = { STM32H7_OVR_MST, STM32H7_OVR_SLV},
	.ier = STM32H7_ADC_IER,
	.eocie_msk = STM32H7_EOCIE,
};

static const unsigned int stm32_adc_offset[STM32_ADC_MAX_ADCS] = {
	0, STM32_ADC_OFFSET, STM32_ADC_OFFSET * 2,
};

static unsigned int stm32_adc_eoc_enabled(struct stm32_adc_priv *priv,
					  unsigned int adc)
{
	u32 ier, offset = stm32_adc_offset[adc];

	ier = readl_relaxed(priv->common.base + offset + priv->cfg->regs->ier);

	return ier & priv->cfg->regs->eocie_msk;
}

/* ADC common interrupt for all instances */
static void stm32_adc_irq_handler(struct irq_desc *desc)
{
	struct stm32_adc_priv *priv = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int i;
	u32 status;

	chained_irq_enter(chip, desc);
	status = readl_relaxed(priv->common.base + priv->cfg->regs->csr);

	/*
	 * End of conversion may be handled by using IRQ or DMA. There may be a
	 * race here when two conversions complete at the same time on several
	 * ADCs. EOC may be read 'set' for several ADCs, with:
	 * - an ADC configured to use DMA (EOC triggers the DMA request, and
	 *   is then automatically cleared by DR read in hardware)
	 * - an ADC configured to use IRQs (EOCIE bit is set. The handler must
	 *   be called in this case)
	 * So both EOC status bit in CSR and EOCIE control bit must be checked
	 * before invoking the interrupt handler (e.g. call ISR only for
	 * IRQ-enabled ADCs).
	 */
	for (i = 0; i < priv->cfg->num_adcs; i++) {
		if ((status & priv->cfg->regs->eoc_msk[i] &&
		     stm32_adc_eoc_enabled(priv, i)) ||
		     (status & priv->cfg->regs->ovr_msk[i]))
			generic_handle_irq(irq_find_mapping(priv->domain, i));
	}

	chained_irq_exit(chip, desc);
};

static int stm32_adc_domain_map(struct irq_domain *d, unsigned int irq,
				irq_hw_number_t hwirq)
{
	irq_set_chip_data(irq, d->host_data);
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_level_irq);

	return 0;
}

static void stm32_adc_domain_unmap(struct irq_domain *d, unsigned int irq)
{
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops stm32_adc_domain_ops = {
	.map = stm32_adc_domain_map,
	.unmap  = stm32_adc_domain_unmap,
	.xlate = irq_domain_xlate_onecell,
};

static int stm32_adc_irq_probe(struct platform_device *pdev,
			       struct stm32_adc_priv *priv)
{
	struct device_node *np = pdev->dev.of_node;
	unsigned int i;

	/*
	 * Interrupt(s) must be provided, depending on the compatible:
	 * - stm32f4/h7 shares a common interrupt line.
	 * - stm32mp1, has one line per ADC
	 */
	for (i = 0; i < priv->cfg->num_irqs; i++) {
		priv->irq[i] = platform_get_irq(pdev, i);
		if (priv->irq[i] < 0)
			return priv->irq[i];
	}

	priv->domain = irq_domain_add_simple(np, STM32_ADC_MAX_ADCS, 0,
					     &stm32_adc_domain_ops,
					     priv);
	if (!priv->domain) {
		dev_err(&pdev->dev, "Failed to add irq domain\n");
		return -ENOMEM;
	}

	for (i = 0; i < priv->cfg->num_irqs; i++) {
		irq_set_chained_handler(priv->irq[i], stm32_adc_irq_handler);
		irq_set_handler_data(priv->irq[i], priv);
	}

	return 0;
}

static void stm32_adc_irq_remove(struct platform_device *pdev,
				 struct stm32_adc_priv *priv)
{
	int hwirq;
	unsigned int i;

	for (hwirq = 0; hwirq < STM32_ADC_MAX_ADCS; hwirq++)
		irq_dispose_mapping(irq_find_mapping(priv->domain, hwirq));
	irq_domain_remove(priv->domain);

	for (i = 0; i < priv->cfg->num_irqs; i++)
		irq_set_chained_handler(priv->irq[i], NULL);
}

static int stm32_adc_core_switches_supply_en(struct stm32_adc_priv *priv,
					     struct device *dev)
{
	int ret;

	/*
	 * On STM32H7 and STM32MP1, the ADC inputs are multiplexed with analog
	 * switches (via PCSEL) which have reduced performances when their
	 * supply is below 2.7V (vdda by default):
	 * - Voltage booster can be used, to get full ADC performances
	 *   (increases power consumption).
	 * - Vdd can be used to supply them, if above 2.7V (STM32MP1 only).
	 *
	 * Recommended settings for ANASWVDD and EN_BOOSTER:
	 * - vdda < 2.7V but vdd > 2.7V: ANASWVDD = 1, EN_BOOSTER = 0 (stm32mp1)
	 * - vdda < 2.7V and vdd < 2.7V: ANASWVDD = 0, EN_BOOSTER = 1
	 * - vdda >= 2.7V:               ANASWVDD = 0, EN_BOOSTER = 0 (default)
	 */
	if (priv->vdda_uv < 2700000) {
		if (priv->syscfg && priv->vdd_uv > 2700000) {
			ret = regulator_enable(priv->vdd);
			if (ret < 0) {
				dev_err(dev, "vdd enable failed %d\n", ret);
				return ret;
			}

			ret = regmap_write(priv->syscfg,
					   STM32MP1_SYSCFG_PMCSETR,
					   STM32MP1_SYSCFG_ANASWVDD_MASK);
			if (ret < 0) {
				regulator_disable(priv->vdd);
				dev_err(dev, "vdd select failed, %d\n", ret);
				return ret;
			}
			dev_dbg(dev, "analog switches supplied by vdd\n");

			return 0;
		}

		if (priv->booster) {
			/*
			 * This is optional, as this is a trade-off between
			 * analog performance and power consumption.
			 */
			ret = regulator_enable(priv->booster);
			if (ret < 0) {
				dev_err(dev, "booster enable failed %d\n", ret);
				return ret;
			}
			dev_dbg(dev, "analog switches supplied by booster\n");

			return 0;
		}
	}

	/* Fallback using vdda (default), nothing to do */
	dev_dbg(dev, "analog switches supplied by vdda (%d uV)\n",
		priv->vdda_uv);

	return 0;
}

static void stm32_adc_core_switches_supply_dis(struct stm32_adc_priv *priv)
{
	if (priv->vdda_uv < 2700000) {
		if (priv->syscfg && priv->vdd_uv > 2700000) {
			regmap_write(priv->syscfg, STM32MP1_SYSCFG_PMCCLRR,
				     STM32MP1_SYSCFG_ANASWVDD_MASK);
			regulator_disable(priv->vdd);
			return;
		}
		if (priv->booster)
			regulator_disable(priv->booster);
	}
}

static int stm32_adc_core_hw_start(struct device *dev)
{
	struct stm32_adc_common *common = dev_get_drvdata(dev);
	struct stm32_adc_priv *priv = to_stm32_adc_priv(common);
	int ret;

	ret = regulator_enable(priv->vdda);
	if (ret < 0) {
		dev_err(dev, "vdda enable failed %d\n", ret);
		return ret;
	}

	ret = regulator_get_voltage(priv->vdda);
	if (ret < 0) {
		dev_err(dev, "vdda get voltage failed, %d\n", ret);
		goto err_vdda_disable;
	}
	priv->vdda_uv = ret;

	ret = stm32_adc_core_switches_supply_en(priv, dev);
	if (ret < 0)
		goto err_vdda_disable;

	ret = regulator_enable(priv->vref);
	if (ret < 0) {
		dev_err(dev, "vref enable failed\n");
		goto err_switches_dis;
	}

	if (priv->bclk) {
		ret = clk_prepare_enable(priv->bclk);
		if (ret < 0) {
			dev_err(dev, "bus clk enable failed\n");
			goto err_regulator_disable;
		}
	}

	if (priv->aclk) {
		ret = clk_prepare_enable(priv->aclk);
		if (ret < 0) {
			dev_err(dev, "adc clk enable failed\n");
			goto err_bclk_disable;
		}
	}

	writel_relaxed(priv->ccr_bak, priv->common.base + priv->cfg->regs->ccr);

	return 0;

err_bclk_disable:
	if (priv->bclk)
		clk_disable_unprepare(priv->bclk);
err_regulator_disable:
	regulator_disable(priv->vref);
err_switches_dis:
	stm32_adc_core_switches_supply_dis(priv);
err_vdda_disable:
	regulator_disable(priv->vdda);

	return ret;
}

static void stm32_adc_core_hw_stop(struct device *dev)
{
	struct stm32_adc_common *common = dev_get_drvdata(dev);
	struct stm32_adc_priv *priv = to_stm32_adc_priv(common);

	/* Backup CCR that may be lost (depends on power state to achieve) */
	priv->ccr_bak = readl_relaxed(priv->common.base + priv->cfg->regs->ccr);
	if (priv->aclk)
		clk_disable_unprepare(priv->aclk);
	if (priv->bclk)
		clk_disable_unprepare(priv->bclk);
	regulator_disable(priv->vref);
	stm32_adc_core_switches_supply_dis(priv);
	regulator_disable(priv->vdda);
}

static int stm32_adc_core_switches_probe(struct device *dev,
					 struct stm32_adc_priv *priv)
{
	struct device_node *np = dev->of_node;
	int ret;

	/* Analog switches supply can be controlled by syscfg (optional) */
	priv->syscfg = syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(priv->syscfg)) {
		ret = PTR_ERR(priv->syscfg);
		if (ret != -ENODEV)
			return dev_err_probe(dev, ret, "Can't probe syscfg\n");

		priv->syscfg = NULL;
	}

	/* Booster can be used to supply analog switches (optional) */
	if (priv->cfg->has_syscfg & HAS_VBOOSTER &&
	    of_property_read_bool(np, "booster-supply")) {
		priv->booster = devm_regulator_get_optional(dev, "booster");
		if (IS_ERR(priv->booster)) {
			ret = PTR_ERR(priv->booster);
			if (ret != -ENODEV)
				return dev_err_probe(dev, ret, "can't get booster\n");

			priv->booster = NULL;
		}
	}

	/* Vdd can be used to supply analog switches (optional) */
	if (priv->cfg->has_syscfg & HAS_ANASWVDD &&
	    of_property_read_bool(np, "vdd-supply")) {
		priv->vdd = devm_regulator_get_optional(dev, "vdd");
		if (IS_ERR(priv->vdd)) {
			ret = PTR_ERR(priv->vdd);
			if (ret != -ENODEV)
				return dev_err_probe(dev, ret, "can't get vdd\n");

			priv->vdd = NULL;
		}
	}

	if (priv->vdd) {
		ret = regulator_enable(priv->vdd);
		if (ret < 0) {
			dev_err(dev, "vdd enable failed %d\n", ret);
			return ret;
		}

		ret = regulator_get_voltage(priv->vdd);
		if (ret < 0) {
			dev_err(dev, "vdd get voltage failed %d\n", ret);
			regulator_disable(priv->vdd);
			return ret;
		}
		priv->vdd_uv = ret;

		regulator_disable(priv->vdd);
	}

	return 0;
}

static int stm32_adc_probe(struct platform_device *pdev)
{
	struct stm32_adc_priv *priv;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	u32 max_rate;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, &priv->common);

	priv->cfg = (const struct stm32_adc_priv_cfg *)
		of_match_device(dev->driver->of_match_table, dev)->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->common.base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->common.base))
		return PTR_ERR(priv->common.base);
	priv->common.phys_base = res->start;

	priv->vdda = devm_regulator_get(&pdev->dev, "vdda");
	if (IS_ERR(priv->vdda))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->vdda),
				     "vdda get failed\n");

	priv->vref = devm_regulator_get(&pdev->dev, "vref");
	if (IS_ERR(priv->vref))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->vref),
				     "vref get failed\n");

	priv->aclk = devm_clk_get_optional(&pdev->dev, "adc");
	if (IS_ERR(priv->aclk))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->aclk),
				     "Can't get 'adc' clock\n");

	priv->bclk = devm_clk_get_optional(&pdev->dev, "bus");
	if (IS_ERR(priv->bclk))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->bclk),
				     "Can't get 'bus' clock\n");

	ret = stm32_adc_core_switches_probe(dev, priv);
	if (ret)
		return ret;

	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_set_autosuspend_delay(dev, STM32_ADC_CORE_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	ret = stm32_adc_core_hw_start(dev);
	if (ret)
		goto err_pm_stop;

	ret = regulator_get_voltage(priv->vref);
	if (ret < 0) {
		dev_err(&pdev->dev, "vref get voltage failed, %d\n", ret);
		goto err_hw_stop;
	}
	priv->common.vref_mv = ret / 1000;
	dev_dbg(&pdev->dev, "vref+=%dmV\n", priv->common.vref_mv);

	ret = of_property_read_u32(pdev->dev.of_node, "st,max-clk-rate-hz",
				   &max_rate);
	if (!ret)
		priv->max_clk_rate = min(max_rate, priv->cfg->max_clk_rate_hz);
	else
		priv->max_clk_rate = priv->cfg->max_clk_rate_hz;

	ret = priv->cfg->clk_sel(pdev, priv);
	if (ret < 0)
		goto err_hw_stop;

	ret = stm32_adc_irq_probe(pdev, priv);
	if (ret < 0)
		goto err_hw_stop;

	ret = of_platform_populate(np, NULL, NULL, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to populate DT children\n");
		goto err_irq_remove;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

err_irq_remove:
	stm32_adc_irq_remove(pdev, priv);
err_hw_stop:
	stm32_adc_core_hw_stop(dev);
err_pm_stop:
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);

	return ret;
}

static int stm32_adc_remove(struct platform_device *pdev)
{
	struct stm32_adc_common *common = platform_get_drvdata(pdev);
	struct stm32_adc_priv *priv = to_stm32_adc_priv(common);

	pm_runtime_get_sync(&pdev->dev);
	of_platform_depopulate(&pdev->dev);
	stm32_adc_irq_remove(pdev, priv);
	stm32_adc_core_hw_stop(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	return 0;
}

#if defined(CONFIG_PM)
static int stm32_adc_core_runtime_suspend(struct device *dev)
{
	stm32_adc_core_hw_stop(dev);

	return 0;
}

static int stm32_adc_core_runtime_resume(struct device *dev)
{
	return stm32_adc_core_hw_start(dev);
}

static int stm32_adc_core_runtime_idle(struct device *dev)
{
	pm_runtime_mark_last_busy(dev);

	return 0;
}
#endif

static const struct dev_pm_ops stm32_adc_core_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(stm32_adc_core_runtime_suspend,
			   stm32_adc_core_runtime_resume,
			   stm32_adc_core_runtime_idle)
};

static const struct stm32_adc_priv_cfg stm32f4_adc_priv_cfg = {
	.regs = &stm32f4_adc_common_regs,
	.clk_sel = stm32f4_adc_clk_sel,
	.max_clk_rate_hz = 36000000,
	.num_irqs = 1,
	.num_adcs = 3,
};

static const struct stm32_adc_priv_cfg stm32h7_adc_priv_cfg = {
	.regs = &stm32h7_adc_common_regs,
	.clk_sel = stm32h7_adc_clk_sel,
	.max_clk_rate_hz = 36000000,
	.has_syscfg = HAS_VBOOSTER,
	.num_irqs = 1,
	.num_adcs = 2,
};

static const struct stm32_adc_priv_cfg stm32mp1_adc_priv_cfg = {
	.regs = &stm32h7_adc_common_regs,
	.clk_sel = stm32h7_adc_clk_sel,
	.max_clk_rate_hz = 36000000,
	.has_syscfg = HAS_VBOOSTER | HAS_ANASWVDD,
	.num_irqs = 2,
	.num_adcs = 2,
};

static const struct of_device_id stm32_adc_of_match[] = {
	{
		.compatible = "st,stm32f4-adc-core",
		.data = (void *)&stm32f4_adc_priv_cfg
	}, {
		.compatible = "st,stm32h7-adc-core",
		.data = (void *)&stm32h7_adc_priv_cfg
	}, {
		.compatible = "st,stm32mp1-adc-core",
		.data = (void *)&stm32mp1_adc_priv_cfg
	}, {
	},
};
MODULE_DEVICE_TABLE(of, stm32_adc_of_match);

static struct platform_driver stm32_adc_driver = {
	.probe = stm32_adc_probe,
	.remove = stm32_adc_remove,
	.driver = {
		.name = "stm32-adc-core",
		.of_match_table = stm32_adc_of_match,
		.pm = &stm32_adc_core_pm_ops,
	},
};
module_platform_driver(stm32_adc_driver);

MODULE_AUTHOR("Fabrice Gasnier <fabrice.gasnier@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 ADC core driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:stm32-adc-core");
