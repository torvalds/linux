// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014-2015 Pengutronix, Markus Pargmann <mpa@pengutronix.de>
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdesc.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/mfd/imx25-tsadc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

static const struct regmap_config mx25_tsadc_regmap_config = {
	.max_register = 8,
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static void mx25_tsadc_irq_handler(struct irq_desc *desc)
{
	struct mx25_tsadc *tsadc = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u32 status;

	chained_irq_enter(chip, desc);

	regmap_read(tsadc->regs, MX25_TSC_TGSR, &status);

	if (status & MX25_TGSR_GCQ_INT)
		generic_handle_domain_irq(tsadc->domain, 1);

	if (status & MX25_TGSR_TCQ_INT)
		generic_handle_domain_irq(tsadc->domain, 0);

	chained_irq_exit(chip, desc);
}

static int mx25_tsadc_domain_map(struct irq_domain *d, unsigned int irq,
				 irq_hw_number_t hwirq)
{
	struct mx25_tsadc *tsadc = d->host_data;

	irq_set_chip_data(irq, tsadc);
	irq_set_chip_and_handler(irq, &dummy_irq_chip,
				 handle_level_irq);
	irq_modify_status(irq, IRQ_NOREQUEST, IRQ_NOPROBE);

	return 0;
}

static const struct irq_domain_ops mx25_tsadc_domain_ops = {
	.map = mx25_tsadc_domain_map,
	.xlate = irq_domain_xlate_onecell,
};

static int mx25_tsadc_setup_irq(struct platform_device *pdev,
				struct mx25_tsadc *tsadc)
{
	struct device *dev = &pdev->dev;
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	tsadc->domain = irq_domain_create_simple(dev_fwnode(dev), 2, 0, &mx25_tsadc_domain_ops,
						 tsadc);
	if (!tsadc->domain) {
		dev_err(dev, "Failed to add irq domain\n");
		return -ENOMEM;
	}

	irq_set_chained_handler_and_data(irq, mx25_tsadc_irq_handler, tsadc);

	return 0;
}

static int mx25_tsadc_unset_irq(struct platform_device *pdev)
{
	struct mx25_tsadc *tsadc = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	if (irq >= 0) {
		irq_set_chained_handler_and_data(irq, NULL, NULL);
		irq_domain_remove(tsadc->domain);
	}

	return 0;
}

static void mx25_tsadc_setup_clk(struct platform_device *pdev,
				 struct mx25_tsadc *tsadc)
{
	unsigned clk_div;

	/*
	 * According to the datasheet the ADC clock should never
	 * exceed 1,75 MHz. Base clock is the IPG and the ADC unit uses
	 * a funny clock divider. To keep the ADC conversion time constant
	 * adapt the ADC internal clock divider to the IPG clock rate.
	 */

	dev_dbg(&pdev->dev, "Found master clock at %lu Hz\n",
		clk_get_rate(tsadc->clk));

	clk_div = DIV_ROUND_UP(clk_get_rate(tsadc->clk), 1750000);
	dev_dbg(&pdev->dev, "Setting up ADC clock divider to %u\n", clk_div);

	/* adc clock = IPG clock / (2 * div + 2) */
	clk_div -= 2;
	clk_div /= 2;

	/*
	 * the ADC clock divider changes its behaviour when values below 4
	 * are used: it is fixed to "/ 10" in this case
	 */
	clk_div = max_t(unsigned, 4, clk_div);

	dev_dbg(&pdev->dev, "Resulting ADC conversion clock at %lu Hz\n",
		clk_get_rate(tsadc->clk) / (2 * clk_div + 2));

	regmap_update_bits(tsadc->regs, MX25_TSC_TGCR,
			   MX25_TGCR_ADCCLKCFG(0x1f),
			   MX25_TGCR_ADCCLKCFG(clk_div));
}

static int mx25_tsadc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mx25_tsadc *tsadc;
	int ret;
	void __iomem *iomem;

	tsadc = devm_kzalloc(dev, sizeof(*tsadc), GFP_KERNEL);
	if (!tsadc)
		return -ENOMEM;

	iomem = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(iomem))
		return PTR_ERR(iomem);

	tsadc->regs = devm_regmap_init_mmio(dev, iomem,
					    &mx25_tsadc_regmap_config);
	if (IS_ERR(tsadc->regs)) {
		dev_err(dev, "Failed to initialize regmap\n");
		return PTR_ERR(tsadc->regs);
	}

	tsadc->clk = devm_clk_get(dev, "ipg");
	if (IS_ERR(tsadc->clk)) {
		dev_err(dev, "Failed to get ipg clock\n");
		return PTR_ERR(tsadc->clk);
	}

	/* setup clock according to the datasheet */
	mx25_tsadc_setup_clk(pdev, tsadc);

	/* Enable clock and reset the component */
	regmap_update_bits(tsadc->regs, MX25_TSC_TGCR, MX25_TGCR_CLK_EN,
			   MX25_TGCR_CLK_EN);
	regmap_update_bits(tsadc->regs, MX25_TSC_TGCR, MX25_TGCR_TSC_RST,
			   MX25_TGCR_TSC_RST);

	/* Setup powersaving mode, but enable internal reference voltage */
	regmap_update_bits(tsadc->regs, MX25_TSC_TGCR, MX25_TGCR_POWERMODE_MASK,
			   MX25_TGCR_POWERMODE_SAVE);
	regmap_update_bits(tsadc->regs, MX25_TSC_TGCR, MX25_TGCR_INTREFEN,
			   MX25_TGCR_INTREFEN);

	ret = mx25_tsadc_setup_irq(pdev, tsadc);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, tsadc);

	ret = devm_of_platform_populate(dev);
	if (ret)
		goto err_irq;

	return 0;

err_irq:
	mx25_tsadc_unset_irq(pdev);

	return ret;
}

static void mx25_tsadc_remove(struct platform_device *pdev)
{
	mx25_tsadc_unset_irq(pdev);
}

static const struct of_device_id mx25_tsadc_ids[] = {
	{ .compatible = "fsl,imx25-tsadc" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, mx25_tsadc_ids);

static struct platform_driver mx25_tsadc_driver = {
	.driver = {
		.name = "mx25-tsadc",
		.of_match_table = mx25_tsadc_ids,
	},
	.probe = mx25_tsadc_probe,
	.remove = mx25_tsadc_remove,
};
module_platform_driver(mx25_tsadc_driver);

MODULE_DESCRIPTION("MFD for ADC/TSC for Freescale mx25");
MODULE_AUTHOR("Markus Pargmann <mpa@pengutronix.de>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mx25-tsadc");
