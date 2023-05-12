// SPDX-License-Identifier: GPL-2.0-only
/*
 * A64FX diag driver.
 * Copyright (c) 2022 Fujitsu Ltd.
 */

#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define A64FX_DIAG_IRQ 1
#define BMC_DIAG_INTERRUPT_ENABLE 0x40
#define BMC_DIAG_INTERRUPT_STATUS 0x44
#define BMC_DIAG_INTERRUPT_MASK BIT(31)

struct a64fx_diag_priv {
	void __iomem *mmsc_reg_base;
	int irq;
	bool has_nmi;
};

static irqreturn_t a64fx_diag_handler_nmi(int irq, void *dev_id)
{
	nmi_panic(NULL, "a64fx_diag: interrupt received\n");

	return IRQ_HANDLED;
}

static irqreturn_t a64fx_diag_handler_irq(int irq, void *dev_id)
{
	panic("a64fx_diag: interrupt received\n");

	return IRQ_HANDLED;
}

static void a64fx_diag_interrupt_clear(struct a64fx_diag_priv *priv)
{
	void __iomem *diag_status_reg_addr;
	u32 mmsc;

	diag_status_reg_addr = priv->mmsc_reg_base + BMC_DIAG_INTERRUPT_STATUS;
	mmsc = readl(diag_status_reg_addr);
	if (mmsc & BMC_DIAG_INTERRUPT_MASK)
		writel(BMC_DIAG_INTERRUPT_MASK, diag_status_reg_addr);
}

static void a64fx_diag_interrupt_enable(struct a64fx_diag_priv *priv)
{
	void __iomem *diag_enable_reg_addr;
	u32 mmsc;

	diag_enable_reg_addr = priv->mmsc_reg_base + BMC_DIAG_INTERRUPT_ENABLE;
	mmsc = readl(diag_enable_reg_addr);
	if (!(mmsc & BMC_DIAG_INTERRUPT_MASK)) {
		mmsc |= BMC_DIAG_INTERRUPT_MASK;
		writel(mmsc, diag_enable_reg_addr);
	}
}

static void a64fx_diag_interrupt_disable(struct a64fx_diag_priv *priv)
{
	void __iomem *diag_enable_reg_addr;
	u32 mmsc;

	diag_enable_reg_addr = priv->mmsc_reg_base + BMC_DIAG_INTERRUPT_ENABLE;
	mmsc = readl(diag_enable_reg_addr);
	if (mmsc & BMC_DIAG_INTERRUPT_MASK) {
		mmsc &= ~BMC_DIAG_INTERRUPT_MASK;
		writel(mmsc, diag_enable_reg_addr);
	}
}

static int a64fx_diag_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct a64fx_diag_priv *priv;
	unsigned long irq_flags;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	priv->mmsc_reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->mmsc_reg_base))
		return PTR_ERR(priv->mmsc_reg_base);

	priv->irq = platform_get_irq(pdev, A64FX_DIAG_IRQ);
	if (priv->irq < 0)
		return priv->irq;

	platform_set_drvdata(pdev, priv);

	irq_flags = IRQF_PERCPU | IRQF_NOBALANCING | IRQF_NO_AUTOEN |
		   IRQF_NO_THREAD;
	ret = request_nmi(priv->irq, &a64fx_diag_handler_nmi, irq_flags,
			"a64fx_diag_nmi", NULL);
	if (ret) {
		ret = request_irq(priv->irq, &a64fx_diag_handler_irq,
				irq_flags, "a64fx_diag_irq", NULL);
		if (ret) {
			dev_err(dev, "cannot register IRQ %d\n", ret);
			return ret;
		}
		enable_irq(priv->irq);
	} else {
		enable_nmi(priv->irq);
		priv->has_nmi = true;
	}

	a64fx_diag_interrupt_clear(priv);
	a64fx_diag_interrupt_enable(priv);

	return 0;
}

static int a64fx_diag_remove(struct platform_device *pdev)
{
	struct a64fx_diag_priv *priv = platform_get_drvdata(pdev);

	a64fx_diag_interrupt_disable(priv);
	a64fx_diag_interrupt_clear(priv);

	if (priv->has_nmi)
		free_nmi(priv->irq, NULL);
	else
		free_irq(priv->irq, NULL);

	return 0;
}

static const struct acpi_device_id a64fx_diag_acpi_match[] = {
	{ "FUJI2007", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, a64fx_diag_acpi_match);


static struct platform_driver a64fx_diag_driver = {
	.driver = {
		.name = "a64fx_diag_driver",
		.acpi_match_table = ACPI_PTR(a64fx_diag_acpi_match),
	},
	.probe = a64fx_diag_probe,
	.remove = a64fx_diag_remove,
};

module_platform_driver(a64fx_diag_driver);

MODULE_AUTHOR("Hitomi Hasegawa <hasegawa-hitomi@fujitsu.com>");
MODULE_DESCRIPTION("A64FX diag driver");
