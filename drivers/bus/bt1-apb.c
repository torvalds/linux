// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Authors:
 *   Serge Semin <Sergey.Semin@baikalelectronics.ru>
 *
 * Baikal-T1 APB-bus driver
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/atomic.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/nmi.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/time64.h>
#include <linux/sysfs.h>

#define APB_EHB_ISR			0x00
#define APB_EHB_ISR_PENDING		BIT(0)
#define APB_EHB_ISR_MASK		BIT(1)
#define APB_EHB_ADDR			0x04
#define APB_EHB_TIMEOUT			0x08

#define APB_EHB_TIMEOUT_MIN		0x000003FFU
#define APB_EHB_TIMEOUT_MAX		0xFFFFFFFFU

/*
 * struct bt1_apb - Baikal-T1 APB EHB private data
 * @dev: Pointer to the device structure.
 * @regs: APB EHB registers map.
 * @res: No-device error injection memory region.
 * @irq: Errors IRQ number.
 * @rate: APB-bus reference clock rate.
 * @pclk: APB-reference clock.
 * @prst: APB domain reset line.
 * @count: Number of errors detected.
 */
struct bt1_apb {
	struct device *dev;

	struct regmap *regs;
	void __iomem *res;
	int irq;

	unsigned long rate;
	struct clk *pclk;

	struct reset_control *prst;

	atomic_t count;
};

static const struct regmap_config bt1_apb_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = APB_EHB_TIMEOUT,
	.fast_io = true
};

static inline unsigned long bt1_apb_n_to_timeout_us(struct bt1_apb *apb, u32 n)
{
	u64 timeout = (u64)n * USEC_PER_SEC;

	do_div(timeout, apb->rate);

	return timeout;

}

static inline unsigned long bt1_apb_timeout_to_n_us(struct bt1_apb *apb,
						    unsigned long timeout)
{
	u64 n = (u64)timeout * apb->rate;

	do_div(n, USEC_PER_SEC);

	return n;

}

static irqreturn_t bt1_apb_isr(int irq, void *data)
{
	struct bt1_apb *apb = data;
	u32 addr = 0;

	regmap_read(apb->regs, APB_EHB_ADDR, &addr);

	dev_crit_ratelimited(apb->dev,
		"APB-bus fault %d: Slave access timeout at 0x%08x\n",
		atomic_inc_return(&apb->count),
		addr);

	/*
	 * Print backtrace on each CPU. This might be pointless if the fault
	 * has happened on the same CPU as the IRQ handler is executed or
	 * the other core proceeded further execution despite the error.
	 * But if it's not, by looking at the trace we would get straight to
	 * the cause of the problem.
	 */
	trigger_all_cpu_backtrace();

	regmap_update_bits(apb->regs, APB_EHB_ISR, APB_EHB_ISR_PENDING, 0);

	return IRQ_HANDLED;
}

static void bt1_apb_clear_data(void *data)
{
	struct bt1_apb *apb = data;
	struct platform_device *pdev = to_platform_device(apb->dev);

	platform_set_drvdata(pdev, NULL);
}

static struct bt1_apb *bt1_apb_create_data(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bt1_apb *apb;
	int ret;

	apb = devm_kzalloc(dev, sizeof(*apb), GFP_KERNEL);
	if (!apb)
		return ERR_PTR(-ENOMEM);

	ret = devm_add_action(dev, bt1_apb_clear_data, apb);
	if (ret) {
		dev_err(dev, "Can't add APB EHB data clear action\n");
		return ERR_PTR(ret);
	}

	apb->dev = dev;
	atomic_set(&apb->count, 0);
	platform_set_drvdata(pdev, apb);

	return apb;
}

static int bt1_apb_request_regs(struct bt1_apb *apb)
{
	struct platform_device *pdev = to_platform_device(apb->dev);
	void __iomem *regs;

	regs = devm_platform_ioremap_resource_byname(pdev, "ehb");
	if (IS_ERR(regs)) {
		dev_err(apb->dev, "Couldn't map APB EHB registers\n");
		return PTR_ERR(regs);
	}

	apb->regs = devm_regmap_init_mmio(apb->dev, regs, &bt1_apb_regmap_cfg);
	if (IS_ERR(apb->regs)) {
		dev_err(apb->dev, "Couldn't create APB EHB regmap\n");
		return PTR_ERR(apb->regs);
	}

	apb->res = devm_platform_ioremap_resource_byname(pdev, "nodev");
	if (IS_ERR(apb->res))
		dev_err(apb->dev, "Couldn't map reserved region\n");

	return PTR_ERR_OR_ZERO(apb->res);
}

static int bt1_apb_request_rst(struct bt1_apb *apb)
{
	int ret;

	apb->prst = devm_reset_control_get_optional_exclusive(apb->dev, "prst");
	if (IS_ERR(apb->prst))
		return dev_err_probe(apb->dev, PTR_ERR(apb->prst),
				     "Couldn't get reset control line\n");

	ret = reset_control_deassert(apb->prst);
	if (ret)
		dev_err(apb->dev, "Failed to deassert the reset line\n");

	return ret;
}

static int bt1_apb_request_clk(struct bt1_apb *apb)
{
	apb->pclk = devm_clk_get_enabled(apb->dev, "pclk");
	if (IS_ERR(apb->pclk))
		return dev_err_probe(apb->dev, PTR_ERR(apb->pclk),
				     "Couldn't get APB clock descriptor\n");

	apb->rate = clk_get_rate(apb->pclk);
	if (!apb->rate) {
		dev_err(apb->dev, "Invalid clock rate\n");
		return -EINVAL;
	}

	return 0;
}

static void bt1_apb_clear_irq(void *data)
{
	struct bt1_apb *apb = data;

	regmap_update_bits(apb->regs, APB_EHB_ISR, APB_EHB_ISR_MASK, 0);
}

static int bt1_apb_request_irq(struct bt1_apb *apb)
{
	struct platform_device *pdev = to_platform_device(apb->dev);
	int ret;

	apb->irq = platform_get_irq(pdev, 0);
	if (apb->irq < 0)
		return apb->irq;

	ret = devm_request_irq(apb->dev, apb->irq, bt1_apb_isr, IRQF_SHARED,
			       "bt1-apb", apb);
	if (ret) {
		dev_err(apb->dev, "Couldn't request APB EHB IRQ\n");
		return ret;
	}

	ret = devm_add_action(apb->dev, bt1_apb_clear_irq, apb);
	if (ret) {
		dev_err(apb->dev, "Can't add APB EHB IRQs clear action\n");
		return ret;
	}

	/* Unmask IRQ and clear it' pending flag. */
	regmap_update_bits(apb->regs, APB_EHB_ISR,
			   APB_EHB_ISR_PENDING | APB_EHB_ISR_MASK,
			   APB_EHB_ISR_MASK);

	return 0;
}

static ssize_t count_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct bt1_apb *apb = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&apb->count));
}
static DEVICE_ATTR_RO(count);

static ssize_t timeout_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct bt1_apb *apb = dev_get_drvdata(dev);
	unsigned long timeout;
	int ret;
	u32 n;

	ret = regmap_read(apb->regs, APB_EHB_TIMEOUT, &n);
	if (ret)
		return ret;

	timeout = bt1_apb_n_to_timeout_us(apb, n);

	return scnprintf(buf, PAGE_SIZE, "%lu\n", timeout);
}

static ssize_t timeout_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct bt1_apb *apb = dev_get_drvdata(dev);
	unsigned long timeout;
	int ret;
	u32 n;

	if (kstrtoul(buf, 0, &timeout) < 0)
		return -EINVAL;

	n = bt1_apb_timeout_to_n_us(apb, timeout);
	n = clamp(n, APB_EHB_TIMEOUT_MIN, APB_EHB_TIMEOUT_MAX);

	ret = regmap_write(apb->regs, APB_EHB_TIMEOUT, n);

	return ret ?: count;
}
static DEVICE_ATTR_RW(timeout);

static ssize_t inject_error_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "Error injection: nodev irq\n");
}

static ssize_t inject_error_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *data, size_t count)
{
	struct bt1_apb *apb = dev_get_drvdata(dev);

	/*
	 * Either dummy read from the unmapped address in the APB IO area
	 * or manually set the IRQ status.
	 */
	if (sysfs_streq(data, "nodev"))
		readl(apb->res);
	else if (sysfs_streq(data, "irq"))
		regmap_update_bits(apb->regs, APB_EHB_ISR, APB_EHB_ISR_PENDING,
				   APB_EHB_ISR_PENDING);
	else
		return -EINVAL;

	return count;
}
static DEVICE_ATTR_RW(inject_error);

static struct attribute *bt1_apb_sysfs_attrs[] = {
	&dev_attr_count.attr,
	&dev_attr_timeout.attr,
	&dev_attr_inject_error.attr,
	NULL
};
ATTRIBUTE_GROUPS(bt1_apb_sysfs);

static void bt1_apb_remove_sysfs(void *data)
{
	struct bt1_apb *apb = data;

	device_remove_groups(apb->dev, bt1_apb_sysfs_groups);
}

static int bt1_apb_init_sysfs(struct bt1_apb *apb)
{
	int ret;

	ret = device_add_groups(apb->dev, bt1_apb_sysfs_groups);
	if (ret) {
		dev_err(apb->dev, "Failed to create EHB APB sysfs nodes\n");
		return ret;
	}

	ret = devm_add_action_or_reset(apb->dev, bt1_apb_remove_sysfs, apb);
	if (ret)
		dev_err(apb->dev, "Can't add APB EHB sysfs remove action\n");

	return ret;
}

static int bt1_apb_probe(struct platform_device *pdev)
{
	struct bt1_apb *apb;
	int ret;

	apb = bt1_apb_create_data(pdev);
	if (IS_ERR(apb))
		return PTR_ERR(apb);

	ret = bt1_apb_request_regs(apb);
	if (ret)
		return ret;

	ret = bt1_apb_request_rst(apb);
	if (ret)
		return ret;

	ret = bt1_apb_request_clk(apb);
	if (ret)
		return ret;

	ret = bt1_apb_request_irq(apb);
	if (ret)
		return ret;

	ret = bt1_apb_init_sysfs(apb);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id bt1_apb_of_match[] = {
	{ .compatible = "baikal,bt1-apb" },
	{ }
};
MODULE_DEVICE_TABLE(of, bt1_apb_of_match);

static struct platform_driver bt1_apb_driver = {
	.probe = bt1_apb_probe,
	.driver = {
		.name = "bt1-apb",
		.of_match_table = bt1_apb_of_match
	}
};
module_platform_driver(bt1_apb_driver);

MODULE_AUTHOR("Serge Semin <Sergey.Semin@baikalelectronics.ru>");
MODULE_DESCRIPTION("Baikal-T1 APB-bus driver");
