/*
 * Copyright (C) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/pm.h>

#include <asm/bug.h>
#include <asm/signal.h>

#define ARB_TIMER			0x008
#define ARB_ERR_CAP_CLR			0x7e4
#define  ARB_ERR_CAP_CLEAR		(1 << 0)
#define ARB_ERR_CAP_HI_ADDR		0x7e8
#define ARB_ERR_CAP_ADDR		0x7ec
#define ARB_ERR_CAP_DATA		0x7f0
#define ARB_ERR_CAP_STATUS		0x7f4
#define  ARB_ERR_CAP_STATUS_TIMEOUT	(1 << 12)
#define  ARB_ERR_CAP_STATUS_TEA		(1 << 11)
#define  ARB_ERR_CAP_STATUS_BS_SHIFT	(1 << 2)
#define  ARB_ERR_CAP_STATUS_BS_MASK	0x3c
#define  ARB_ERR_CAP_STATUS_WRITE	(1 << 1)
#define  ARB_ERR_CAP_STATUS_VALID	(1 << 0)
#define ARB_ERR_CAP_MASTER		0x7f8

struct brcmstb_gisb_arb_device {
	void __iomem	*base;
	struct mutex	lock;
	struct list_head next;
	u32 valid_mask;
	const char *master_names[sizeof(u32) * BITS_PER_BYTE];
	u32 saved_timeout;
};

static LIST_HEAD(brcmstb_gisb_arb_device_list);

static ssize_t gisb_arb_get_timeout(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct brcmstb_gisb_arb_device *gdev = platform_get_drvdata(pdev);
	u32 timeout;

	mutex_lock(&gdev->lock);
	timeout = ioread32(gdev->base + ARB_TIMER);
	mutex_unlock(&gdev->lock);

	return sprintf(buf, "%d", timeout);
}

static ssize_t gisb_arb_set_timeout(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct brcmstb_gisb_arb_device *gdev = platform_get_drvdata(pdev);
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret < 0)
		return ret;

	if (val == 0 || val >= 0xffffffff)
		return -EINVAL;

	mutex_lock(&gdev->lock);
	iowrite32(val, gdev->base + ARB_TIMER);
	mutex_unlock(&gdev->lock);

	return count;
}

static const char *
brcmstb_gisb_master_to_str(struct brcmstb_gisb_arb_device *gdev,
						u32 masters)
{
	u32 mask = gdev->valid_mask & masters;

	if (hweight_long(mask) != 1)
		return NULL;

	return gdev->master_names[ffs(mask) - 1];
}

static int brcmstb_gisb_arb_decode_addr(struct brcmstb_gisb_arb_device *gdev,
					const char *reason)
{
	u32 cap_status;
	unsigned long arb_addr;
	u32 master;
	const char *m_name;
	char m_fmt[11];

	cap_status = ioread32(gdev->base + ARB_ERR_CAP_STATUS);

	/* Invalid captured address, bail out */
	if (!(cap_status & ARB_ERR_CAP_STATUS_VALID))
		return 1;

	/* Read the address and master */
	arb_addr = ioread32(gdev->base + ARB_ERR_CAP_ADDR) & 0xffffffff;
#if (IS_ENABLED(CONFIG_PHYS_ADDR_T_64BIT))
	arb_addr |= (u64)ioread32(gdev->base + ARB_ERR_CAP_HI_ADDR) << 32;
#endif
	master = ioread32(gdev->base + ARB_ERR_CAP_MASTER);

	m_name = brcmstb_gisb_master_to_str(gdev, master);
	if (!m_name) {
		snprintf(m_fmt, sizeof(m_fmt), "0x%08x", master);
		m_name = m_fmt;
	}

	pr_crit("%s: %s at 0x%lx [%c %s], core: %s\n",
		__func__, reason, arb_addr,
		cap_status & ARB_ERR_CAP_STATUS_WRITE ? 'W' : 'R',
		cap_status & ARB_ERR_CAP_STATUS_TIMEOUT ? "timeout" : "",
		m_name);

	/* clear the GISB error */
	iowrite32(ARB_ERR_CAP_CLEAR, gdev->base + ARB_ERR_CAP_CLR);

	return 0;
}

static int brcmstb_bus_error_handler(unsigned long addr, unsigned int fsr,
				     struct pt_regs *regs)
{
	int ret = 0;
	struct brcmstb_gisb_arb_device *gdev;

	/* iterate over each GISB arb registered handlers */
	list_for_each_entry(gdev, &brcmstb_gisb_arb_device_list, next)
		ret |= brcmstb_gisb_arb_decode_addr(gdev, "bus error");
	/*
	 * If it was an imprecise abort, then we need to correct the
	 * return address to be _after_ the instruction.
	*/
	if (fsr & (1 << 10))
		regs->ARM_pc += 4;

	return ret;
}

static irqreturn_t brcmstb_gisb_timeout_handler(int irq, void *dev_id)
{
	brcmstb_gisb_arb_decode_addr(dev_id, "timeout");

	return IRQ_HANDLED;
}

static irqreturn_t brcmstb_gisb_tea_handler(int irq, void *dev_id)
{
	brcmstb_gisb_arb_decode_addr(dev_id, "target abort");

	return IRQ_HANDLED;
}

static DEVICE_ATTR(gisb_arb_timeout, S_IWUSR | S_IRUGO,
		gisb_arb_get_timeout, gisb_arb_set_timeout);

static struct attribute *gisb_arb_sysfs_attrs[] = {
	&dev_attr_gisb_arb_timeout.attr,
	NULL,
};

static struct attribute_group gisb_arb_sysfs_attr_group = {
	.attrs = gisb_arb_sysfs_attrs,
};

static int brcmstb_gisb_arb_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct brcmstb_gisb_arb_device *gdev;
	struct resource *r;
	int err, timeout_irq, tea_irq;
	unsigned int num_masters, j = 0;
	int i, first, last;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	timeout_irq = platform_get_irq(pdev, 0);
	tea_irq = platform_get_irq(pdev, 1);

	gdev = devm_kzalloc(&pdev->dev, sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		return -ENOMEM;

	mutex_init(&gdev->lock);
	INIT_LIST_HEAD(&gdev->next);

	gdev->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(gdev->base))
		return PTR_ERR(gdev->base);

	err = devm_request_irq(&pdev->dev, timeout_irq,
				brcmstb_gisb_timeout_handler, 0, pdev->name,
				gdev);
	if (err < 0)
		return err;

	err = devm_request_irq(&pdev->dev, tea_irq,
				brcmstb_gisb_tea_handler, 0, pdev->name,
				gdev);
	if (err < 0)
		return err;

	/* If we do not have a valid mask, assume all masters are enabled */
	if (of_property_read_u32(dn, "brcm,gisb-arb-master-mask",
				&gdev->valid_mask))
		gdev->valid_mask = 0xffffffff;

	/* Proceed with reading the litteral names if we agree on the
	 * number of masters
	 */
	num_masters = of_property_count_strings(dn,
			"brcm,gisb-arb-master-names");
	if (hweight_long(gdev->valid_mask) == num_masters) {
		first = ffs(gdev->valid_mask) - 1;
		last = fls(gdev->valid_mask) - 1;

		for (i = first; i < last; i++) {
			if (!(gdev->valid_mask & BIT(i)))
				continue;

			of_property_read_string_index(dn,
					"brcm,gisb-arb-master-names", j,
					&gdev->master_names[i]);
			j++;
		}
	}

	err = sysfs_create_group(&pdev->dev.kobj, &gisb_arb_sysfs_attr_group);
	if (err)
		return err;

	platform_set_drvdata(pdev, gdev);

	list_add_tail(&gdev->next, &brcmstb_gisb_arb_device_list);

	hook_fault_code(22, brcmstb_bus_error_handler, SIGBUS, 0,
			"imprecise external abort");

	dev_info(&pdev->dev, "registered mem: %p, irqs: %d, %d\n",
			gdev->base, timeout_irq, tea_irq);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int brcmstb_gisb_arb_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct brcmstb_gisb_arb_device *gdev = platform_get_drvdata(pdev);

	gdev->saved_timeout = ioread32(gdev->base + ARB_TIMER);

	return 0;
}

/* Make sure we provide the same timeout value that was configured before, and
 * do this before the GISB timeout interrupt handler has any chance to run.
 */
static int brcmstb_gisb_arb_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct brcmstb_gisb_arb_device *gdev = platform_get_drvdata(pdev);

	iowrite32(gdev->saved_timeout, gdev->base + ARB_TIMER);

	return 0;
}
#else
#define brcmstb_gisb_arb_suspend       NULL
#define brcmstb_gisb_arb_resume_noirq  NULL
#endif

static const struct dev_pm_ops brcmstb_gisb_arb_pm_ops = {
	.suspend	= brcmstb_gisb_arb_suspend,
	.resume_noirq	= brcmstb_gisb_arb_resume_noirq,
};

static const struct of_device_id brcmstb_gisb_arb_of_match[] = {
	{ .compatible = "brcm,gisb-arb" },
	{ },
};

static struct platform_driver brcmstb_gisb_arb_driver = {
	.probe	= brcmstb_gisb_arb_probe,
	.driver = {
		.name	= "brcm-gisb-arb",
		.owner	= THIS_MODULE,
		.of_match_table = brcmstb_gisb_arb_of_match,
		.pm	= &brcmstb_gisb_arb_pm_ops,
	},
};

static int __init brcm_gisb_driver_init(void)
{
	return platform_driver_register(&brcmstb_gisb_arb_driver);
}

module_init(brcm_gisb_driver_init);
