/*
 * Copyright (C) 2014 Hisilicon Co. Ltd.
 * Copyright (C) 2015 ARM Limited
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

/**
 * @file mali_hikey.c
 * HiKey platform specific Mali driver functions.
 */

/* Set to 1 to enable ION (not tested yet). */
#define HISI6220_USE_ION 0

#define pr_fmt(fmt) "Mali: HiKey: " fmt

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/pm.h>
#include <linux/mm.h>
#include <linux/of.h>

#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/dma-mapping.h>
#if HISI6220_USE_ION
#include <linux/hisi/hisi_ion.h>
#endif
#include <linux/byteorder/generic.h>

#include <linux/mali/mali_utgard.h>

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_hikey_hi6220_registers_gpu.h"

#define MALI_GPU_MHZ 1000000
#define MALI_IRQ_ID 142
#define MALI_FRAME_BUFFER_ADDR 0x3F100000
#define MALI_FRAME_BUFFER_SIZE 0x00708000

#define MALI_CALC_REG_MASK(bit_start, bit_end)			\
	(((0x1 << (bit_end - bit_start + 1)) - 1) << bit_start)

enum mali_core_type {
	MALI_CORE_400_MP1 = 0,
	MALI_CORE_400_MP2 = 1,
	MALI_CORE_450_MP4 = 2,
	MALI_CORE_TYPE_MAX
};

enum mali_power_mode {
	MALI_POWER_MODE_ON,           /**< Power on */
	MALI_POWER_MODE_LIGHT_SLEEP,  /**< Idle for a short or PM suspend */
	MALI_POWER_MODE_DEEP_SLEEP,   /**< Idle for a long or OS suspend */
};

struct mali_soc_remap_addr_table {
	u8 *soc_media_sctrl_base_addr;
	u8 *soc_ao_sctrl_base_addr;
	u8 *soc_peri_sctrl_base_addr;
	u8 *soc_pmctl_base_addr;
};

static struct clk *mali_clk_g3d;
static struct clk *mali_pclk_g3d;
static struct regulator *mali_regulator;
static struct device_node *mali_np;
static bool mali_gpu_power_status;

static struct resource mali_gpu_resources_m450_mp4[] = {
	MALI_GPU_RESOURCES_MALI450_MP4(
		SOC_G3D_S_BASE_ADDR, MALI_IRQ_ID, MALI_IRQ_ID, MALI_IRQ_ID,
		MALI_IRQ_ID, MALI_IRQ_ID, MALI_IRQ_ID, MALI_IRQ_ID,
		MALI_IRQ_ID, MALI_IRQ_ID, MALI_IRQ_ID, MALI_IRQ_ID)
};

static struct mali_soc_remap_addr_table *mali_soc_addr_table;

static void mali_reg_writel(u8 *base_addr, unsigned int reg_offset,
			    unsigned char start_bit, unsigned char end_bit,
			    unsigned int val)
{
	int read_val;
	unsigned long flags;
	static DEFINE_SPINLOCK(reg_lock);
	void __iomem *addr;

	WARN_ON(!base_addr);

	addr = base_addr + reg_offset;
	spin_lock_irqsave(&reg_lock, flags);
	read_val = readl(addr) & ~(MALI_CALC_REG_MASK(start_bit, end_bit));
	read_val |= (MALI_CALC_REG_MASK(start_bit, end_bit)
		     & (val << start_bit));
	writel(read_val, addr);
	spin_unlock_irqrestore(&reg_lock, flags);
}

static unsigned int mali_reg_readl(u8 *base_addr, unsigned int reg_offset,
				   unsigned char start_bit,
				   unsigned char end_bit)
{
	unsigned int val;

	WARN_ON(!base_addr);

	val = readl((void __iomem *)(base_addr + reg_offset));
	val &= MALI_CALC_REG_MASK(start_bit, end_bit);

	return val >> start_bit;
}

static int mali_clock_on(void)
{
	u32 core_freq = 0;
	u32 pclk_freq = 0;
	int stat;

	stat = clk_prepare_enable(mali_pclk_g3d);
	if (stat)
		return stat;

	stat = of_property_read_u32(mali_np, "pclk_freq", &pclk_freq);
	if (stat)
		return stat;

	stat = clk_set_rate(mali_pclk_g3d, pclk_freq * MALI_GPU_MHZ);
	if (stat)
		return stat;

	stat = of_property_read_u32(mali_np, "mali_def_freq", &core_freq);
	if (stat)
		return stat;

	stat = clk_set_rate(mali_clk_g3d, core_freq * MALI_GPU_MHZ);
	if (stat)
		return stat;

	stat = clk_prepare_enable(mali_clk_g3d);
	if (stat)
		return stat;

	mali_reg_writel(mali_soc_addr_table->soc_media_sctrl_base_addr,
			SOC_MEDIA_SCTRL_SC_MEDIA_CLKDIS_ADDR(0), 17, 17, 1);

	return 0;
}

static void mali_clock_off(void)
{
	clk_disable_unprepare(mali_clk_g3d);
}

static int mali_domain_powerup_finish(void)
{
	unsigned int ret;

	mali_reg_writel(mali_soc_addr_table->soc_ao_sctrl_base_addr,
			SOC_AO_SCTRL_SC_PW_RSTDIS0_ADDR(0), 1, 1, 1);
	ret = mali_reg_readl(mali_soc_addr_table->soc_ao_sctrl_base_addr,
			     SOC_AO_SCTRL_SC_PW_RST_STAT0_ADDR(0), 1, 1);
	if (ret != 0) {
		pr_err("SET SC_PW_RSTDIS0 failed!\n");
		return -EFAULT;
	}

	mali_reg_writel(mali_soc_addr_table->soc_ao_sctrl_base_addr,
			SOC_AO_SCTRL_SC_PW_ISODIS0_ADDR(0), 1, 1, 1);
	ret = mali_reg_readl(mali_soc_addr_table->soc_ao_sctrl_base_addr,
			     SOC_AO_SCTRL_SC_PW_ISO_STAT0_ADDR(0), 1, 1);
	if (ret != 0) {
		pr_err("SET SC_PW_ISODIS0 failed!\n");
		return -EFAULT;
	}

	mali_reg_writel(mali_soc_addr_table->soc_ao_sctrl_base_addr,
			SOC_AO_SCTRL_SC_PW_CLKEN0_ADDR(0), 1, 1, 1);
	ret = mali_reg_readl(mali_soc_addr_table->soc_ao_sctrl_base_addr,
			     SOC_AO_SCTRL_SC_PW_CLK_STAT0_ADDR(0), 1, 1);
	if (ret != 1) {
		pr_err("SET SC_PW_CLKEN0 failed!\n");
		return -EFAULT;
	}

	mali_reg_writel(mali_soc_addr_table->soc_media_sctrl_base_addr,
			SOC_MEDIA_SCTRL_SC_MEDIA_RSTDIS_ADDR(0), 0, 0, 1);
	ret = mali_reg_readl(mali_soc_addr_table->soc_media_sctrl_base_addr,
			     SOC_MEDIA_SCTRL_SC_MEDIA_RST_STAT_ADDR(0), 0, 0);
	if (ret != 0) {
		pr_err("SET SC_MEDIA_RSTDIS failed!\n");
		return -EFAULT;
	}

	return 0;
}

static int mali_regulator_enable(void)
{
	int i, stat;

	stat = regulator_enable(mali_regulator);
	if (stat)
		return stat;

	for (i = 0; i < 50; i++) {
		unsigned int res = mali_reg_readl(
			mali_soc_addr_table->soc_ao_sctrl_base_addr,
			SOC_AO_SCTRL_SC_PW_MTCMOS_STAT0_ADDR(0), 1, 1);
		if (res)
			break;
		udelay(1);
	}

	if (50 == i) {
		pr_err("regulator enable timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int mali_platform_powerup(void)
{
	int stat;

	if (mali_gpu_power_status)
		return 0;

	stat = mali_regulator_enable();
	if (stat)
		return stat;

	stat = mali_clock_on();
	if (stat)
		return stat;

	stat = mali_domain_powerup_finish();
	if (stat)
		return stat;

	mali_gpu_power_status = true;

	return 0;
}

static int mali_regulator_disable(void)
{
	mali_reg_writel(mali_soc_addr_table->soc_media_sctrl_base_addr,
			SOC_MEDIA_SCTRL_SC_MEDIA_RSTEN_ADDR(0), 0, 0, 1);
	mali_reg_writel(mali_soc_addr_table->soc_media_sctrl_base_addr,
			SOC_MEDIA_SCTRL_SC_MEDIA_CLKDIS_ADDR(0), 1, 1, 1);
	mali_reg_writel(mali_soc_addr_table->soc_ao_sctrl_base_addr,
			SOC_AO_SCTRL_SC_PW_CLKDIS0_ADDR(0), 1, 1, 1);
	mali_reg_writel(mali_soc_addr_table->soc_ao_sctrl_base_addr,
			SOC_AO_SCTRL_SC_PW_ISOEN0_ADDR(0), 1, 1, 1);
	mali_reg_writel(mali_soc_addr_table->soc_ao_sctrl_base_addr,
			SOC_AO_SCTRL_SC_PW_RSTEN0_ADDR(0), 1, 1, 1);

	return regulator_disable(mali_regulator);
}

static int mali_platform_powerdown(void)
{
	int stat;

	if (!mali_gpu_power_status)
		return 0;

	stat = mali_regulator_disable();
	if (stat)
		return stat;

	mali_clock_off();
	mali_gpu_power_status = false;

	return 0;
}

static int mali_platform_power_mode_change(enum mali_power_mode power_mode)
{
	int stat;

	switch (power_mode) {
	case MALI_POWER_MODE_ON:
		stat = mali_platform_powerup();
		break;
	case MALI_POWER_MODE_LIGHT_SLEEP:
	case MALI_POWER_MODE_DEEP_SLEEP:
		stat = mali_platform_powerdown();
		break;
	default:
		pr_err("Invalid power mode\n");
		stat = -EINVAL;
		break;
	}

	return stat;
}

static int mali_os_suspend(struct device *device)
{
	int stat;

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->suspend) {
		stat = device->driver->pm->suspend(device);
	} else {
		stat = 0;
	}

	if (stat)
		return stat;

	return mali_platform_power_mode_change(MALI_POWER_MODE_DEEP_SLEEP);
}

static int mali_os_resume(struct device *device)
{
	int stat;

	stat = mali_platform_power_mode_change(MALI_POWER_MODE_ON);
	if (stat)
		return stat;

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->resume) {
		stat = device->driver->pm->resume(device);
	}

	return stat;
}

static int mali_os_freeze(struct device *device)
{
	int stat;

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->freeze) {
		stat = device->driver->pm->freeze(device);
	} else {
		stat = 0;
	}

	return stat;
}

static int mali_os_thaw(struct device *device)
{
	int stat;

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->thaw) {
		stat = device->driver->pm->thaw(device);
	} else {
		stat = 0;
	}

	return stat;
}

#ifdef CONFIG_PM_RUNTIME
static int mali_runtime_suspend(struct device *device)
{
	int stat;

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->runtime_suspend) {
		stat = device->driver->pm->runtime_suspend(device);
	} else {
		stat = 0;
	}

	if (stat)
		return stat;

	return mali_platform_power_mode_change(MALI_POWER_MODE_LIGHT_SLEEP);
}

static int mali_runtime_resume(struct device *device)
{
	int stat;

	stat = mali_platform_power_mode_change(MALI_POWER_MODE_ON);
	if (stat)
		return stat;

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->runtime_resume) {
		stat = device->driver->pm->runtime_resume(device);
	}

	return stat;
}

static int mali_runtime_idle(struct device *device)
{
	int stat;

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->runtime_idle) {
		stat = device->driver->pm->runtime_idle(device);
	} else {
		stat = 0;
	}

	if (stat)
		return stat;

	return pm_runtime_suspend(device);
}
#endif

static int init_mali_clock_regulator(struct platform_device *pdev)
{
	int stat, ret;

	BUG_ON(mali_regulator || mali_clk_g3d || mali_pclk_g3d);

	/* regulator init */

	mali_regulator  = regulator_get(&pdev->dev, "G3D_PD_VDD");
	if (IS_ERR(mali_regulator)) {
		pr_err("failed to get G3D_PD_VDD\n");
		return -ENODEV;
	}

	stat = mali_regulator_enable();
	if (stat)
		return stat;

	mali_gpu_power_status = true;

	/* clk init */

	mali_clk_g3d = clk_get(&pdev->dev, "clk_g3d");
	if (IS_ERR(mali_clk_g3d)) {
		pr_err("failed to get source CLK_G3D\n");
		return -ENODEV;
	}

	mali_pclk_g3d = clk_get(&pdev->dev, "pclk_g3d");
	if (IS_ERR(mali_pclk_g3d)) {
		pr_err("failed to get source PCLK_G3D\n");
		return -ENODEV;
	}

	ret = mali_reg_readl(mali_soc_addr_table->soc_peri_sctrl_base_addr,
			     SOC_PERI_SCTRL_SC_PERIPH_CLKSTAT12_ADDR(0),
			     10, 10);
	if (ret != 1) {
		mali_reg_writel(mali_soc_addr_table->soc_peri_sctrl_base_addr,
				SOC_PERI_SCTRL_SC_PERIPH_CLKEN12_ADDR(0),
				10, 10, 1);
		ret = mali_reg_readl(
			mali_soc_addr_table->soc_peri_sctrl_base_addr,
			SOC_PERI_SCTRL_SC_PERIPH_CLKSTAT12_ADDR(0), 10, 10);
		if (ret != 1) {
			pr_err("SET SC_PERIPH_CLKEN12 failed!\n");
			return -EFAULT;
		}
	}

	stat = mali_clock_on();
	if (stat)
		return stat;

	mali_reg_writel(mali_soc_addr_table->soc_media_sctrl_base_addr,
			SOC_MEDIA_SCTRL_SC_MEDIA_CLKCFG2_ADDR(0), 15, 15, 1);
	ret = mali_reg_readl(mali_soc_addr_table->soc_media_sctrl_base_addr,
			     SOC_MEDIA_SCTRL_SC_MEDIA_CLKCFG2_ADDR(0), 15, 15);
	if (ret != 1) {
		pr_err("SET SC_MEDIA_CLKCFG2 failed!\n");
		return -EFAULT;
	}

	return mali_domain_powerup_finish();
}

static int deinit_mali_clock_regulator(void)
{
	int stat;

	BUG_ON(!mali_regulator || !mali_clk_g3d || !mali_pclk_g3d);

	stat = mali_platform_powerdown();
	if (stat)
		return stat;

	clk_put(mali_clk_g3d);
	mali_clk_g3d = NULL;
	clk_put(mali_pclk_g3d);
	mali_pclk_g3d = NULL;
	regulator_put(mali_regulator);
	mali_regulator = NULL;

	return 0;
}

static struct mali_gpu_device_data mali_gpu_data = {
	.shared_mem_size = 1024 * 1024 * 1024, /* 1024MB */
	.fb_start = MALI_FRAME_BUFFER_ADDR,
	.fb_size = MALI_FRAME_BUFFER_SIZE,
	.max_job_runtime = 2000, /* 2 seconds time out */
	.control_interval = 50, /* 50ms */
#ifdef CONFIG_MALI_DVFS
	.utilization_callback = mali_gpu_utilization_proc,
#endif
};

static const struct dev_pm_ops mali_gpu_device_type_pm_ops = {
	.suspend = mali_os_suspend,
	.resume = mali_os_resume,
	.freeze = mali_os_freeze,
	.thaw = mali_os_thaw,
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend = mali_runtime_suspend,
	.runtime_resume = mali_runtime_resume,
	.runtime_idle = mali_runtime_idle,
#endif
};

static struct device_type mali_gpu_device_device_type = {
	.pm = &mali_gpu_device_type_pm_ops,
};

static enum mali_core_type mali_get_gpu_type(void)
{
	u32 gpu_type = MALI_CORE_TYPE_MAX;
	int err = of_property_read_u32(mali_np, "mali_type", &gpu_type);

	if (err) {
		pr_err("failed to read mali_type from device tree\n");
		return -EFAULT;
	}

	return gpu_type;
}

#if HISI6220_USE_ION
static int mali_ion_mem_init(void)
{
	struct ion_heap_info_data mem_data;

	if (hisi_ion_get_heap_info(ION_FB_HEAP_ID, &mem_data)) {
		pr_err("Failed to get ION_FB_HEAP_ID\n");
		return -EFAULT;
	}

	if (mem_data.heap_size == 0) {
		pr_err("fb size is 0\n");
		return -EINVAL;
	}

	mali_gpu_data.fb_size = mem_data.heap_size;
	mali_gpu_data.fb_start = (unsigned long)(mem_data.heap_phy);
	pr_debug("fb_size=0x%x, fb_start=0x%x\n",
		 mali_gpu_data.fb_size, mali_gpu_data.fb_start);

	return 0;
}
#endif

static int mali_remap_soc_addr(void)
{
	BUG_ON(mali_soc_addr_table);

	mali_soc_addr_table = kmalloc(sizeof(struct mali_soc_remap_addr_table),
				      GFP_KERNEL);
	if (!mali_soc_addr_table)
		return -ENOMEM;

	mali_soc_addr_table->soc_media_sctrl_base_addr =
		ioremap(SOC_MEDIA_SCTRL_BASE_ADDR, REG_MEDIA_SC_IOSIZE);
	mali_soc_addr_table->soc_ao_sctrl_base_addr =
		ioremap(SOC_AO_SCTRL_BASE_ADDR, REG_SC_ON_IOSIZE);
	mali_soc_addr_table->soc_peri_sctrl_base_addr =
		ioremap(SOC_PERI_SCTRL_BASE_ADDR, REG_SC_OFF_IOSIZE);
	mali_soc_addr_table->soc_pmctl_base_addr =
		ioremap(SOC_PMCTRL_BASE_ADDR, REG_PMCTRL_IOSIZE);

	if (!mali_soc_addr_table->soc_media_sctrl_base_addr
	    || !mali_soc_addr_table->soc_ao_sctrl_base_addr
	    || !mali_soc_addr_table->soc_peri_sctrl_base_addr
	    || !mali_soc_addr_table->soc_pmctl_base_addr) {
		pr_err("Failed to remap SoC addresses\n");
		return -ENOMEM;
	}

	return 0;
}

static void mali_unmap_soc_addr(void)
{
	iounmap((void __iomem *)mali_soc_addr_table->soc_media_sctrl_base_addr);
	iounmap((void __iomem *)mali_soc_addr_table->soc_ao_sctrl_base_addr);
	iounmap((void __iomem *)mali_soc_addr_table->soc_peri_sctrl_base_addr);
	iounmap((void __iomem *)mali_soc_addr_table->soc_pmctl_base_addr);
	kfree(mali_soc_addr_table);
	mali_soc_addr_table = NULL;
}

int mali_platform_device_init(struct platform_device *pdev)
{
	int stat;
	int irq, i;

#if HISI6220_USE_ION
	stat = mali_ion_mem_init();
	if (stat)
		return stat;
#endif

	stat = mali_remap_soc_addr();
	if (stat)
		return stat;

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	pdev->dev.type = &mali_gpu_device_device_type;
	pdev->dev.platform_data = &mali_gpu_data;
	pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	mali_np = pdev->dev.of_node;

	if (mali_get_gpu_type() != MALI_CORE_450_MP4) {
		pr_err("Unexpected GPU type\n");
		return -EINVAL;
	}

	/*
	 * We need to use DT to get the irq domain, so rewrite the static
	 * table with the irq given from platform_get_irq().
	 */
	irq = platform_get_irq(pdev, 0);
	for (i = 0; i < ARRAY_SIZE(mali_gpu_resources_m450_mp4); i++) {
		if (IORESOURCE_IRQ & mali_gpu_resources_m450_mp4[i].flags) {
			mali_gpu_resources_m450_mp4[i].start = irq;
			mali_gpu_resources_m450_mp4[i].end = irq;
		}
	}
	pdev->num_resources = ARRAY_SIZE(mali_gpu_resources_m450_mp4);
	pdev->resource = mali_gpu_resources_m450_mp4;

	stat = init_mali_clock_regulator(pdev);
	if (stat)
		return stat;

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_set_autosuspend_delay(&(pdev->dev), 1);
	pm_runtime_use_autosuspend(&(pdev->dev));
	pm_runtime_enable(&pdev->dev);
#endif

	return 0;
}

int mali_platform_device_deinit(void)
{
	int stat;

	stat = deinit_mali_clock_regulator();
	if (stat)
		return stat;

	mali_unmap_soc_addr();

	return 0;
}
