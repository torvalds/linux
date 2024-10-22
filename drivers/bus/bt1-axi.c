// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Authors:
 *   Serge Semin <Sergey.Semin@baikalelectronics.ru>
 *
 * Baikal-T1 AXI-bus driver
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/atomic.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/nmi.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/sysfs.h>

#define BT1_AXI_WERRL			0x110
#define BT1_AXI_WERRH			0x114
#define BT1_AXI_WERRH_TYPE		BIT(23)
#define BT1_AXI_WERRH_ADDR_FLD		24
#define BT1_AXI_WERRH_ADDR_MASK		GENMASK(31, BT1_AXI_WERRH_ADDR_FLD)

/*
 * struct bt1_axi - Baikal-T1 AXI-bus private data
 * @dev: Pointer to the device structure.
 * @qos_regs: AXI Interconnect QoS tuning registers.
 * @sys_regs: Baikal-T1 System Controller registers map.
 * @irq: Errors IRQ number.
 * @aclk: AXI reference clock.
 * @arst: AXI Interconnect reset line.
 * @count: Number of errors detected.
 */
struct bt1_axi {
	struct device *dev;

	void __iomem *qos_regs;
	struct regmap *sys_regs;
	int irq;

	struct clk *aclk;

	struct reset_control *arst;

	atomic_t count;
};

static irqreturn_t bt1_axi_isr(int irq, void *data)
{
	struct bt1_axi *axi = data;
	u32 low = 0, high = 0;

	regmap_read(axi->sys_regs, BT1_AXI_WERRL, &low);
	regmap_read(axi->sys_regs, BT1_AXI_WERRH, &high);

	dev_crit_ratelimited(axi->dev,
		"AXI-bus fault %d: %s at 0x%x%08x\n",
		atomic_inc_return(&axi->count),
		high & BT1_AXI_WERRH_TYPE ? "no slave" : "slave protocol error",
		high, low);

	/*
	 * Print backtrace on each CPU. This might be pointless if the fault
	 * has happened on the same CPU as the IRQ handler is executed or
	 * the other core proceeded further execution despite the error.
	 * But if it's not, by looking at the trace we would get straight to
	 * the cause of the problem.
	 */
	trigger_all_cpu_backtrace();

	return IRQ_HANDLED;
}

static void bt1_axi_clear_data(void *data)
{
	struct bt1_axi *axi = data;
	struct platform_device *pdev = to_platform_device(axi->dev);

	platform_set_drvdata(pdev, NULL);
}

static struct bt1_axi *bt1_axi_create_data(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bt1_axi *axi;
	int ret;

	axi = devm_kzalloc(dev, sizeof(*axi), GFP_KERNEL);
	if (!axi)
		return ERR_PTR(-ENOMEM);

	ret = devm_add_action(dev, bt1_axi_clear_data, axi);
	if (ret) {
		dev_err(dev, "Can't add AXI EHB data clear action\n");
		return ERR_PTR(ret);
	}

	axi->dev = dev;
	atomic_set(&axi->count, 0);
	platform_set_drvdata(pdev, axi);

	return axi;
}

static int bt1_axi_request_regs(struct bt1_axi *axi)
{
	struct platform_device *pdev = to_platform_device(axi->dev);
	struct device *dev = axi->dev;

	axi->sys_regs = syscon_regmap_lookup_by_phandle(dev->of_node, "syscon");
	if (IS_ERR(axi->sys_regs)) {
		dev_err(dev, "Couldn't find syscon registers\n");
		return PTR_ERR(axi->sys_regs);
	}

	axi->qos_regs = devm_platform_ioremap_resource_byname(pdev, "qos");
	if (IS_ERR(axi->qos_regs))
		dev_err(dev, "Couldn't map AXI-bus QoS registers\n");

	return PTR_ERR_OR_ZERO(axi->qos_regs);
}

static int bt1_axi_request_rst(struct bt1_axi *axi)
{
	int ret;

	axi->arst = devm_reset_control_get_optional_exclusive(axi->dev, "arst");
	if (IS_ERR(axi->arst))
		return dev_err_probe(axi->dev, PTR_ERR(axi->arst),
				     "Couldn't get reset control line\n");

	ret = reset_control_deassert(axi->arst);
	if (ret)
		dev_err(axi->dev, "Failed to deassert the reset line\n");

	return ret;
}

static int bt1_axi_request_clk(struct bt1_axi *axi)
{
	axi->aclk = devm_clk_get_enabled(axi->dev, "aclk");
	if (IS_ERR(axi->aclk))
		return dev_err_probe(axi->dev, PTR_ERR(axi->aclk),
				     "Couldn't get AXI Interconnect clock\n");

	return 0;
}

static int bt1_axi_request_irq(struct bt1_axi *axi)
{
	struct platform_device *pdev = to_platform_device(axi->dev);
	int ret;

	axi->irq = platform_get_irq(pdev, 0);
	if (axi->irq < 0)
		return axi->irq;

	ret = devm_request_irq(axi->dev, axi->irq, bt1_axi_isr, IRQF_SHARED,
			       "bt1-axi", axi);
	if (ret)
		dev_err(axi->dev, "Couldn't request AXI EHB IRQ\n");

	return ret;
}

static ssize_t count_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct bt1_axi *axi = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&axi->count));
}
static DEVICE_ATTR_RO(count);

static ssize_t inject_error_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "Error injection: bus unaligned\n");
}

static ssize_t inject_error_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *data, size_t count)
{
	struct bt1_axi *axi = dev_get_drvdata(dev);

	/*
	 * Performing unaligned read from the memory will cause the CM2 bus
	 * error while unaligned writing - the AXI bus write error handled
	 * by this driver.
	 */
	if (sysfs_streq(data, "bus"))
		readb(axi->qos_regs);
	else if (sysfs_streq(data, "unaligned"))
		writeb(0, axi->qos_regs);
	else
		return -EINVAL;

	return count;
}
static DEVICE_ATTR_RW(inject_error);

static struct attribute *bt1_axi_sysfs_attrs[] = {
	&dev_attr_count.attr,
	&dev_attr_inject_error.attr,
	NULL
};
ATTRIBUTE_GROUPS(bt1_axi_sysfs);

static void bt1_axi_remove_sysfs(void *data)
{
	struct bt1_axi *axi = data;

	device_remove_groups(axi->dev, bt1_axi_sysfs_groups);
}

static int bt1_axi_init_sysfs(struct bt1_axi *axi)
{
	int ret;

	ret = device_add_groups(axi->dev, bt1_axi_sysfs_groups);
	if (ret) {
		dev_err(axi->dev, "Failed to add sysfs files group\n");
		return ret;
	}

	ret = devm_add_action_or_reset(axi->dev, bt1_axi_remove_sysfs, axi);
	if (ret)
		dev_err(axi->dev, "Can't add AXI EHB sysfs remove action\n");

	return ret;
}

static int bt1_axi_probe(struct platform_device *pdev)
{
	struct bt1_axi *axi;
	int ret;

	axi = bt1_axi_create_data(pdev);
	if (IS_ERR(axi))
		return PTR_ERR(axi);

	ret = bt1_axi_request_regs(axi);
	if (ret)
		return ret;

	ret = bt1_axi_request_rst(axi);
	if (ret)
		return ret;

	ret = bt1_axi_request_clk(axi);
	if (ret)
		return ret;

	ret = bt1_axi_request_irq(axi);
	if (ret)
		return ret;

	ret = bt1_axi_init_sysfs(axi);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id bt1_axi_of_match[] = {
	{ .compatible = "baikal,bt1-axi" },
	{ }
};
MODULE_DEVICE_TABLE(of, bt1_axi_of_match);

static struct platform_driver bt1_axi_driver = {
	.probe = bt1_axi_probe,
	.driver = {
		.name = "bt1-axi",
		.of_match_table = bt1_axi_of_match
	}
};
module_platform_driver(bt1_axi_driver);

MODULE_AUTHOR("Serge Semin <Sergey.Semin@baikalelectronics.ru>");
MODULE_DESCRIPTION("Baikal-T1 AXI-bus driver");
