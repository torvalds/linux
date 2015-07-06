/*
 * ACPI support for Intel Lynxpoint LPSS.
 *
 * Copyright (C) 2013, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *          Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/platform_data/clk-lpss.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>

#include "internal.h"

ACPI_MODULE_NAME("acpi_lpss");

#ifdef CONFIG_X86_INTEL_LPSS

#define LPSS_ADDR(desc) ((unsigned long)&desc)

#define LPSS_CLK_SIZE	0x04
#define LPSS_LTR_SIZE	0x18

/* Offsets relative to LPSS_PRIVATE_OFFSET */
#define LPSS_CLK_DIVIDER_DEF_MASK	(BIT(1) | BIT(16))
#define LPSS_RESETS			0x04
#define LPSS_RESETS_RESET_FUNC		BIT(0)
#define LPSS_RESETS_RESET_APB		BIT(1)
#define LPSS_GENERAL			0x08
#define LPSS_GENERAL_LTR_MODE_SW	BIT(2)
#define LPSS_GENERAL_UART_RTS_OVRD	BIT(3)
#define LPSS_SW_LTR			0x10
#define LPSS_AUTO_LTR			0x14
#define LPSS_LTR_SNOOP_REQ		BIT(15)
#define LPSS_LTR_SNOOP_MASK		0x0000FFFF
#define LPSS_LTR_SNOOP_LAT_1US		0x800
#define LPSS_LTR_SNOOP_LAT_32US		0xC00
#define LPSS_LTR_SNOOP_LAT_SHIFT	5
#define LPSS_LTR_SNOOP_LAT_CUTOFF	3000
#define LPSS_LTR_MAX_VAL		0x3FF
#define LPSS_TX_INT			0x20
#define LPSS_TX_INT_MASK		BIT(1)

#define LPSS_PRV_REG_COUNT		9

/* LPSS Flags */
#define LPSS_CLK			BIT(0)
#define LPSS_CLK_GATE			BIT(1)
#define LPSS_CLK_DIVIDER		BIT(2)
#define LPSS_LTR			BIT(3)
#define LPSS_SAVE_CTX			BIT(4)

struct lpss_private_data;

struct lpss_device_desc {
	unsigned int flags;
	const char *clk_con_id;
	unsigned int prv_offset;
	size_t prv_size_override;
	void (*setup)(struct lpss_private_data *pdata);
};

static struct lpss_device_desc lpss_dma_desc = {
	.flags = LPSS_CLK,
};

struct lpss_private_data {
	void __iomem *mmio_base;
	resource_size_t mmio_size;
	unsigned int fixed_clk_rate;
	struct clk *clk;
	const struct lpss_device_desc *dev_desc;
	u32 prv_reg_ctx[LPSS_PRV_REG_COUNT];
};

/* UART Component Parameter Register */
#define LPSS_UART_CPR			0xF4
#define LPSS_UART_CPR_AFCE		BIT(4)

static void lpss_uart_setup(struct lpss_private_data *pdata)
{
	unsigned int offset;
	u32 val;

	offset = pdata->dev_desc->prv_offset + LPSS_TX_INT;
	val = readl(pdata->mmio_base + offset);
	writel(val | LPSS_TX_INT_MASK, pdata->mmio_base + offset);

	val = readl(pdata->mmio_base + LPSS_UART_CPR);
	if (!(val & LPSS_UART_CPR_AFCE)) {
		offset = pdata->dev_desc->prv_offset + LPSS_GENERAL;
		val = readl(pdata->mmio_base + offset);
		val |= LPSS_GENERAL_UART_RTS_OVRD;
		writel(val, pdata->mmio_base + offset);
	}
}

static void lpss_deassert_reset(struct lpss_private_data *pdata)
{
	unsigned int offset;
	u32 val;

	offset = pdata->dev_desc->prv_offset + LPSS_RESETS;
	val = readl(pdata->mmio_base + offset);
	val |= LPSS_RESETS_RESET_APB | LPSS_RESETS_RESET_FUNC;
	writel(val, pdata->mmio_base + offset);
}

#define LPSS_I2C_ENABLE			0x6c

static void byt_i2c_setup(struct lpss_private_data *pdata)
{
	lpss_deassert_reset(pdata);

	if (readl(pdata->mmio_base + pdata->dev_desc->prv_offset))
		pdata->fixed_clk_rate = 133000000;

	writel(0, pdata->mmio_base + LPSS_I2C_ENABLE);
}

static struct lpss_device_desc lpt_dev_desc = {
	.flags = LPSS_CLK | LPSS_CLK_GATE | LPSS_CLK_DIVIDER | LPSS_LTR,
	.prv_offset = 0x800,
};

static struct lpss_device_desc lpt_i2c_dev_desc = {
	.flags = LPSS_CLK | LPSS_CLK_GATE | LPSS_LTR,
	.prv_offset = 0x800,
};

static struct lpss_device_desc lpt_uart_dev_desc = {
	.flags = LPSS_CLK | LPSS_CLK_GATE | LPSS_CLK_DIVIDER | LPSS_LTR,
	.clk_con_id = "baudclk",
	.prv_offset = 0x800,
	.setup = lpss_uart_setup,
};

static struct lpss_device_desc lpt_sdio_dev_desc = {
	.flags = LPSS_LTR,
	.prv_offset = 0x1000,
	.prv_size_override = 0x1018,
};

static struct lpss_device_desc byt_pwm_dev_desc = {
	.flags = LPSS_SAVE_CTX,
};

static struct lpss_device_desc byt_uart_dev_desc = {
	.flags = LPSS_CLK | LPSS_CLK_GATE | LPSS_CLK_DIVIDER | LPSS_SAVE_CTX,
	.clk_con_id = "baudclk",
	.prv_offset = 0x800,
	.setup = lpss_uart_setup,
};

static struct lpss_device_desc byt_spi_dev_desc = {
	.flags = LPSS_CLK | LPSS_CLK_GATE | LPSS_CLK_DIVIDER | LPSS_SAVE_CTX,
	.prv_offset = 0x400,
};

static struct lpss_device_desc byt_sdio_dev_desc = {
	.flags = LPSS_CLK,
};

static struct lpss_device_desc byt_i2c_dev_desc = {
	.flags = LPSS_CLK | LPSS_SAVE_CTX,
	.prv_offset = 0x800,
	.setup = byt_i2c_setup,
};

static struct lpss_device_desc bsw_spi_dev_desc = {
	.flags = LPSS_CLK | LPSS_CLK_GATE | LPSS_CLK_DIVIDER | LPSS_SAVE_CTX,
	.prv_offset = 0x400,
	.setup = lpss_deassert_reset,
};

#else

#define LPSS_ADDR(desc) (0UL)

#endif /* CONFIG_X86_INTEL_LPSS */

static const struct acpi_device_id acpi_lpss_device_ids[] = {
	/* Generic LPSS devices */
	{ "INTL9C60", LPSS_ADDR(lpss_dma_desc) },

	/* Lynxpoint LPSS devices */
	{ "INT33C0", LPSS_ADDR(lpt_dev_desc) },
	{ "INT33C1", LPSS_ADDR(lpt_dev_desc) },
	{ "INT33C2", LPSS_ADDR(lpt_i2c_dev_desc) },
	{ "INT33C3", LPSS_ADDR(lpt_i2c_dev_desc) },
	{ "INT33C4", LPSS_ADDR(lpt_uart_dev_desc) },
	{ "INT33C5", LPSS_ADDR(lpt_uart_dev_desc) },
	{ "INT33C6", LPSS_ADDR(lpt_sdio_dev_desc) },
	{ "INT33C7", },

	/* BayTrail LPSS devices */
	{ "80860F09", LPSS_ADDR(byt_pwm_dev_desc) },
	{ "80860F0A", LPSS_ADDR(byt_uart_dev_desc) },
	{ "80860F0E", LPSS_ADDR(byt_spi_dev_desc) },
	{ "80860F14", LPSS_ADDR(byt_sdio_dev_desc) },
	{ "80860F41", LPSS_ADDR(byt_i2c_dev_desc) },
	{ "INT33B2", },
	{ "INT33FC", },

	/* Braswell LPSS devices */
	{ "80862288", LPSS_ADDR(byt_pwm_dev_desc) },
	{ "8086228A", LPSS_ADDR(byt_uart_dev_desc) },
	{ "8086228E", LPSS_ADDR(bsw_spi_dev_desc) },
	{ "808622C1", LPSS_ADDR(byt_i2c_dev_desc) },

	{ "INT3430", LPSS_ADDR(lpt_dev_desc) },
	{ "INT3431", LPSS_ADDR(lpt_dev_desc) },
	{ "INT3432", LPSS_ADDR(lpt_i2c_dev_desc) },
	{ "INT3433", LPSS_ADDR(lpt_i2c_dev_desc) },
	{ "INT3434", LPSS_ADDR(lpt_uart_dev_desc) },
	{ "INT3435", LPSS_ADDR(lpt_uart_dev_desc) },
	{ "INT3436", LPSS_ADDR(lpt_sdio_dev_desc) },
	{ "INT3437", },

	/* Wildcat Point LPSS devices */
	{ "INT3438", LPSS_ADDR(lpt_dev_desc) },

	{ }
};

#ifdef CONFIG_X86_INTEL_LPSS

static int is_memory(struct acpi_resource *res, void *not_used)
{
	struct resource r;
	return !acpi_dev_resource_memory(res, &r);
}

/* LPSS main clock device. */
static struct platform_device *lpss_clk_dev;

static inline void lpt_register_clock_device(void)
{
	lpss_clk_dev = platform_device_register_simple("clk-lpt", -1, NULL, 0);
}

static int register_device_clock(struct acpi_device *adev,
				 struct lpss_private_data *pdata)
{
	const struct lpss_device_desc *dev_desc = pdata->dev_desc;
	const char *devname = dev_name(&adev->dev);
	struct clk *clk = ERR_PTR(-ENODEV);
	struct lpss_clk_data *clk_data;
	const char *parent, *clk_name;
	void __iomem *prv_base;

	if (!lpss_clk_dev)
		lpt_register_clock_device();

	clk_data = platform_get_drvdata(lpss_clk_dev);
	if (!clk_data)
		return -ENODEV;
	clk = clk_data->clk;

	if (!pdata->mmio_base
	    || pdata->mmio_size < dev_desc->prv_offset + LPSS_CLK_SIZE)
		return -ENODATA;

	parent = clk_data->name;
	prv_base = pdata->mmio_base + dev_desc->prv_offset;

	if (pdata->fixed_clk_rate) {
		clk = clk_register_fixed_rate(NULL, devname, parent, 0,
					      pdata->fixed_clk_rate);
		goto out;
	}

	if (dev_desc->flags & LPSS_CLK_GATE) {
		clk = clk_register_gate(NULL, devname, parent, 0,
					prv_base, 0, 0, NULL);
		parent = devname;
	}

	if (dev_desc->flags & LPSS_CLK_DIVIDER) {
		/* Prevent division by zero */
		if (!readl(prv_base))
			writel(LPSS_CLK_DIVIDER_DEF_MASK, prv_base);

		clk_name = kasprintf(GFP_KERNEL, "%s-div", devname);
		if (!clk_name)
			return -ENOMEM;
		clk = clk_register_fractional_divider(NULL, clk_name, parent,
						      0, prv_base,
						      1, 15, 16, 15, 0, NULL);
		parent = clk_name;

		clk_name = kasprintf(GFP_KERNEL, "%s-update", devname);
		if (!clk_name) {
			kfree(parent);
			return -ENOMEM;
		}
		clk = clk_register_gate(NULL, clk_name, parent,
					CLK_SET_RATE_PARENT | CLK_SET_RATE_GATE,
					prv_base, 31, 0, NULL);
		kfree(parent);
		kfree(clk_name);
	}
out:
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	pdata->clk = clk;
	clk_register_clkdev(clk, dev_desc->clk_con_id, devname);
	return 0;
}

static int acpi_lpss_create_device(struct acpi_device *adev,
				   const struct acpi_device_id *id)
{
	struct lpss_device_desc *dev_desc;
	struct lpss_private_data *pdata;
	struct resource_entry *rentry;
	struct list_head resource_list;
	struct platform_device *pdev;
	int ret;

	dev_desc = (struct lpss_device_desc *)id->driver_data;
	if (!dev_desc) {
		pdev = acpi_create_platform_device(adev);
		return IS_ERR_OR_NULL(pdev) ? PTR_ERR(pdev) : 1;
	}
	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list, is_memory, NULL);
	if (ret < 0)
		goto err_out;

	list_for_each_entry(rentry, &resource_list, node)
		if (resource_type(rentry->res) == IORESOURCE_MEM) {
			if (dev_desc->prv_size_override)
				pdata->mmio_size = dev_desc->prv_size_override;
			else
				pdata->mmio_size = resource_size(rentry->res);
			pdata->mmio_base = ioremap(rentry->res->start,
						   pdata->mmio_size);
			break;
		}

	acpi_dev_free_resource_list(&resource_list);

	if (!pdata->mmio_base) {
		ret = -ENOMEM;
		goto err_out;
	}

	pdata->dev_desc = dev_desc;

	if (dev_desc->setup)
		dev_desc->setup(pdata);

	if (dev_desc->flags & LPSS_CLK) {
		ret = register_device_clock(adev, pdata);
		if (ret) {
			/* Skip the device, but continue the namespace scan. */
			ret = 0;
			goto err_out;
		}
	}

	/*
	 * This works around a known issue in ACPI tables where LPSS devices
	 * have _PS0 and _PS3 without _PSC (and no power resources), so
	 * acpi_bus_init_power() will assume that the BIOS has put them into D0.
	 */
	ret = acpi_device_fix_up_power(adev);
	if (ret) {
		/* Skip the device, but continue the namespace scan. */
		ret = 0;
		goto err_out;
	}

	adev->driver_data = pdata;
	pdev = acpi_create_platform_device(adev);
	if (!IS_ERR_OR_NULL(pdev)) {
		return 1;
	}

	ret = PTR_ERR(pdev);
	adev->driver_data = NULL;

 err_out:
	kfree(pdata);
	return ret;
}

static u32 __lpss_reg_read(struct lpss_private_data *pdata, unsigned int reg)
{
	return readl(pdata->mmio_base + pdata->dev_desc->prv_offset + reg);
}

static void __lpss_reg_write(u32 val, struct lpss_private_data *pdata,
			     unsigned int reg)
{
	writel(val, pdata->mmio_base + pdata->dev_desc->prv_offset + reg);
}

static int lpss_reg_read(struct device *dev, unsigned int reg, u32 *val)
{
	struct acpi_device *adev;
	struct lpss_private_data *pdata;
	unsigned long flags;
	int ret;

	ret = acpi_bus_get_device(ACPI_HANDLE(dev), &adev);
	if (WARN_ON(ret))
		return ret;

	spin_lock_irqsave(&dev->power.lock, flags);
	if (pm_runtime_suspended(dev)) {
		ret = -EAGAIN;
		goto out;
	}
	pdata = acpi_driver_data(adev);
	if (WARN_ON(!pdata || !pdata->mmio_base)) {
		ret = -ENODEV;
		goto out;
	}
	*val = __lpss_reg_read(pdata, reg);

 out:
	spin_unlock_irqrestore(&dev->power.lock, flags);
	return ret;
}

static ssize_t lpss_ltr_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	u32 ltr_value = 0;
	unsigned int reg;
	int ret;

	reg = strcmp(attr->attr.name, "auto_ltr") ? LPSS_SW_LTR : LPSS_AUTO_LTR;
	ret = lpss_reg_read(dev, reg, &ltr_value);
	if (ret)
		return ret;

	return snprintf(buf, PAGE_SIZE, "%08x\n", ltr_value);
}

static ssize_t lpss_ltr_mode_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	u32 ltr_mode = 0;
	char *outstr;
	int ret;

	ret = lpss_reg_read(dev, LPSS_GENERAL, &ltr_mode);
	if (ret)
		return ret;

	outstr = (ltr_mode & LPSS_GENERAL_LTR_MODE_SW) ? "sw" : "auto";
	return sprintf(buf, "%s\n", outstr);
}

static DEVICE_ATTR(auto_ltr, S_IRUSR, lpss_ltr_show, NULL);
static DEVICE_ATTR(sw_ltr, S_IRUSR, lpss_ltr_show, NULL);
static DEVICE_ATTR(ltr_mode, S_IRUSR, lpss_ltr_mode_show, NULL);

static struct attribute *lpss_attrs[] = {
	&dev_attr_auto_ltr.attr,
	&dev_attr_sw_ltr.attr,
	&dev_attr_ltr_mode.attr,
	NULL,
};

static struct attribute_group lpss_attr_group = {
	.attrs = lpss_attrs,
	.name = "lpss_ltr",
};

static void acpi_lpss_set_ltr(struct device *dev, s32 val)
{
	struct lpss_private_data *pdata = acpi_driver_data(ACPI_COMPANION(dev));
	u32 ltr_mode, ltr_val;

	ltr_mode = __lpss_reg_read(pdata, LPSS_GENERAL);
	if (val < 0) {
		if (ltr_mode & LPSS_GENERAL_LTR_MODE_SW) {
			ltr_mode &= ~LPSS_GENERAL_LTR_MODE_SW;
			__lpss_reg_write(ltr_mode, pdata, LPSS_GENERAL);
		}
		return;
	}
	ltr_val = __lpss_reg_read(pdata, LPSS_SW_LTR) & ~LPSS_LTR_SNOOP_MASK;
	if (val >= LPSS_LTR_SNOOP_LAT_CUTOFF) {
		ltr_val |= LPSS_LTR_SNOOP_LAT_32US;
		val = LPSS_LTR_MAX_VAL;
	} else if (val > LPSS_LTR_MAX_VAL) {
		ltr_val |= LPSS_LTR_SNOOP_LAT_32US | LPSS_LTR_SNOOP_REQ;
		val >>= LPSS_LTR_SNOOP_LAT_SHIFT;
	} else {
		ltr_val |= LPSS_LTR_SNOOP_LAT_1US | LPSS_LTR_SNOOP_REQ;
	}
	ltr_val |= val;
	__lpss_reg_write(ltr_val, pdata, LPSS_SW_LTR);
	if (!(ltr_mode & LPSS_GENERAL_LTR_MODE_SW)) {
		ltr_mode |= LPSS_GENERAL_LTR_MODE_SW;
		__lpss_reg_write(ltr_mode, pdata, LPSS_GENERAL);
	}
}

#ifdef CONFIG_PM
/**
 * acpi_lpss_save_ctx() - Save the private registers of LPSS device
 * @dev: LPSS device
 * @pdata: pointer to the private data of the LPSS device
 *
 * Most LPSS devices have private registers which may loose their context when
 * the device is powered down. acpi_lpss_save_ctx() saves those registers into
 * prv_reg_ctx array.
 */
static void acpi_lpss_save_ctx(struct device *dev,
			       struct lpss_private_data *pdata)
{
	unsigned int i;

	for (i = 0; i < LPSS_PRV_REG_COUNT; i++) {
		unsigned long offset = i * sizeof(u32);

		pdata->prv_reg_ctx[i] = __lpss_reg_read(pdata, offset);
		dev_dbg(dev, "saving 0x%08x from LPSS reg at offset 0x%02lx\n",
			pdata->prv_reg_ctx[i], offset);
	}
}

/**
 * acpi_lpss_restore_ctx() - Restore the private registers of LPSS device
 * @dev: LPSS device
 * @pdata: pointer to the private data of the LPSS device
 *
 * Restores the registers that were previously stored with acpi_lpss_save_ctx().
 */
static void acpi_lpss_restore_ctx(struct device *dev,
				  struct lpss_private_data *pdata)
{
	unsigned int i;

	/*
	 * The following delay is needed or the subsequent write operations may
	 * fail. The LPSS devices are actually PCI devices and the PCI spec
	 * expects 10ms delay before the device can be accessed after D3 to D0
	 * transition.
	 */
	msleep(10);

	for (i = 0; i < LPSS_PRV_REG_COUNT; i++) {
		unsigned long offset = i * sizeof(u32);

		__lpss_reg_write(pdata->prv_reg_ctx[i], pdata, offset);
		dev_dbg(dev, "restoring 0x%08x to LPSS reg at offset 0x%02lx\n",
			pdata->prv_reg_ctx[i], offset);
	}
}

#ifdef CONFIG_PM_SLEEP
static int acpi_lpss_suspend_late(struct device *dev)
{
	struct lpss_private_data *pdata = acpi_driver_data(ACPI_COMPANION(dev));
	int ret;

	ret = pm_generic_suspend_late(dev);
	if (ret)
		return ret;

	if (pdata->dev_desc->flags & LPSS_SAVE_CTX)
		acpi_lpss_save_ctx(dev, pdata);

	return acpi_dev_suspend_late(dev);
}

static int acpi_lpss_resume_early(struct device *dev)
{
	struct lpss_private_data *pdata = acpi_driver_data(ACPI_COMPANION(dev));
	int ret;

	ret = acpi_dev_resume_early(dev);
	if (ret)
		return ret;

	if (pdata->dev_desc->flags & LPSS_SAVE_CTX)
		acpi_lpss_restore_ctx(dev, pdata);

	return pm_generic_resume_early(dev);
}
#endif /* CONFIG_PM_SLEEP */

static int acpi_lpss_runtime_suspend(struct device *dev)
{
	struct lpss_private_data *pdata = acpi_driver_data(ACPI_COMPANION(dev));
	int ret;

	ret = pm_generic_runtime_suspend(dev);
	if (ret)
		return ret;

	if (pdata->dev_desc->flags & LPSS_SAVE_CTX)
		acpi_lpss_save_ctx(dev, pdata);

	return acpi_dev_runtime_suspend(dev);
}

static int acpi_lpss_runtime_resume(struct device *dev)
{
	struct lpss_private_data *pdata = acpi_driver_data(ACPI_COMPANION(dev));
	int ret;

	ret = acpi_dev_runtime_resume(dev);
	if (ret)
		return ret;

	if (pdata->dev_desc->flags & LPSS_SAVE_CTX)
		acpi_lpss_restore_ctx(dev, pdata);

	return pm_generic_runtime_resume(dev);
}
#endif /* CONFIG_PM */

static struct dev_pm_domain acpi_lpss_pm_domain = {
	.ops = {
#ifdef CONFIG_PM
#ifdef CONFIG_PM_SLEEP
		.prepare = acpi_subsys_prepare,
		.complete = acpi_subsys_complete,
		.suspend = acpi_subsys_suspend,
		.suspend_late = acpi_lpss_suspend_late,
		.resume_early = acpi_lpss_resume_early,
		.freeze = acpi_subsys_freeze,
		.poweroff = acpi_subsys_suspend,
		.poweroff_late = acpi_lpss_suspend_late,
		.restore_early = acpi_lpss_resume_early,
#endif
		.runtime_suspend = acpi_lpss_runtime_suspend,
		.runtime_resume = acpi_lpss_runtime_resume,
#endif
	},
};

static int acpi_lpss_platform_notify(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct platform_device *pdev = to_platform_device(data);
	struct lpss_private_data *pdata;
	struct acpi_device *adev;
	const struct acpi_device_id *id;

	id = acpi_match_device(acpi_lpss_device_ids, &pdev->dev);
	if (!id || !id->driver_data)
		return 0;

	if (acpi_bus_get_device(ACPI_HANDLE(&pdev->dev), &adev))
		return 0;

	pdata = acpi_driver_data(adev);
	if (!pdata)
		return 0;

	if (pdata->mmio_base &&
	    pdata->mmio_size < pdata->dev_desc->prv_offset + LPSS_LTR_SIZE) {
		dev_err(&pdev->dev, "MMIO size insufficient to access LTR\n");
		return 0;
	}

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		pdev->dev.pm_domain = &acpi_lpss_pm_domain;
		if (pdata->dev_desc->flags & LPSS_LTR)
			return sysfs_create_group(&pdev->dev.kobj,
						  &lpss_attr_group);
		break;
	case BUS_NOTIFY_DEL_DEVICE:
		if (pdata->dev_desc->flags & LPSS_LTR)
			sysfs_remove_group(&pdev->dev.kobj, &lpss_attr_group);
		pdev->dev.pm_domain = NULL;
		break;
	default:
		break;
	}

	return 0;
}

static struct notifier_block acpi_lpss_nb = {
	.notifier_call = acpi_lpss_platform_notify,
};

static void acpi_lpss_bind(struct device *dev)
{
	struct lpss_private_data *pdata = acpi_driver_data(ACPI_COMPANION(dev));

	if (!pdata || !pdata->mmio_base || !(pdata->dev_desc->flags & LPSS_LTR))
		return;

	if (pdata->mmio_size >= pdata->dev_desc->prv_offset + LPSS_LTR_SIZE)
		dev->power.set_latency_tolerance = acpi_lpss_set_ltr;
	else
		dev_err(dev, "MMIO size insufficient to access LTR\n");
}

static void acpi_lpss_unbind(struct device *dev)
{
	dev->power.set_latency_tolerance = NULL;
}

static struct acpi_scan_handler lpss_handler = {
	.ids = acpi_lpss_device_ids,
	.attach = acpi_lpss_create_device,
	.bind = acpi_lpss_bind,
	.unbind = acpi_lpss_unbind,
};

void __init acpi_lpss_init(void)
{
	if (!lpt_clk_init()) {
		bus_register_notifier(&platform_bus_type, &acpi_lpss_nb);
		acpi_scan_add_handler(&lpss_handler);
	}
}

#else

static struct acpi_scan_handler lpss_handler = {
	.ids = acpi_lpss_device_ids,
};

void __init acpi_lpss_init(void)
{
	acpi_scan_add_handler(&lpss_handler);
}

#endif /* CONFIG_X86_INTEL_LPSS */
