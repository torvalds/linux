/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/bootmem.h>
#include <asm/cacheflush.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/debugfs.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#endif
#include "rk_nand_blk.h"
#include "rk_ftl_api.h"

#define RKNAND_VERSION_AND_DATE  "rknandbase v1.1 2016-11-08"

struct rk_nandc_info {
	int	id;
	void __iomem     *reg_base;
	int	irq;
	int	clk_rate;
	struct clk	*clk;	/* flash clk*/
	struct clk	*hclk;	/* nandc clk*/
	struct clk	*gclk;  /* flash clk gate*/
};

static struct rk_nandc_info g_nandc_info[2];
struct device *g_nand_device;
static char nand_idb_data[2048];
static int rk_nand_wait_busy_schedule;
static int rk_nand_suspend_state;
static int rk_nand_shutdown_state;
/*1:flash 2:emmc 4:sdcard0 8:sdcard1*/
static int rknand_boot_media = 2;
static DECLARE_WAIT_QUEUE_HEAD(rk29_nandc_wait);
static void rk_nand_iqr_timeout_hack(unsigned long data);
static DEFINE_TIMER(rk_nand_iqr_timeout, rk_nand_iqr_timeout_hack, 0, 0);
static int nandc0_xfer_completed_flag;
static int nandc0_ready_completed_flag;
static int nandc1_xfer_completed_flag;
static int nandc1_ready_completed_flag;
static int rk_timer_add;

void *ftl_malloc(int size)
{
	return kmalloc(size, GFP_KERNEL | GFP_DMA);
}

void ftl_free(void *buf)
{
	kfree(buf);
}

char rknand_get_sn(char *pbuf)
{
	memcpy(pbuf, &nand_idb_data[0x600], 0x200);
	return 0;
}

char rknand_get_vendor0(char *pbuf)
{
	memcpy(pbuf, &nand_idb_data[0x400 + 8], 504);
	return 0;
}

char *rknand_get_idb_data(void)
{
	return nand_idb_data;
}
EXPORT_SYMBOL(rknand_get_idb_data);

int rknand_get_clk_rate(int nandc_id)
{
	return g_nandc_info[nandc_id].clk_rate;
}
EXPORT_SYMBOL(rknand_get_clk_rate);

unsigned long rknand_dma_flush_dcache(unsigned long ptr, int size, int dir)
{
#ifdef CONFIG_ARM64
	__flush_dcache_area((void *)ptr, size + 63);
#else
	__cpuc_flush_dcache_area((void *)ptr, size + 63);
#endif
	return ((unsigned long)virt_to_phys((void *)ptr));
}
EXPORT_SYMBOL(rknand_dma_flush_dcache);

unsigned long rknand_dma_map_single(unsigned long ptr, int size, int dir)
{
#ifdef CONFIG_ARM64
	__dma_map_area((void *)ptr, size, dir);
	return ((unsigned long)virt_to_phys((void *)ptr));
#else
	return dma_map_single(NULL, (void *)ptr, size
		, dir ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
#endif
}
EXPORT_SYMBOL(rknand_dma_map_single);

void rknand_dma_unmap_single(unsigned long ptr, int size, int dir)
{
#ifdef CONFIG_ARM64
	__dma_unmap_area(phys_to_virt(ptr), size, dir);
#else
	dma_unmap_single(NULL, (dma_addr_t)ptr, size
		, dir ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
#endif
}
EXPORT_SYMBOL(rknand_dma_unmap_single);

int rknand_flash_cs_init(int id)
{
	return 0;
}
EXPORT_SYMBOL(rknand_flash_cs_init);

int rknand_get_reg_addr(unsigned long *p_nandc0, unsigned long *p_nandc1)
{
	*p_nandc0 = (unsigned long)g_nandc_info[0].reg_base;
	*p_nandc1 = (unsigned long)g_nandc_info[1].reg_base;
	return 0;
}
EXPORT_SYMBOL(rknand_get_reg_addr);

int rknand_get_boot_media(void)
{
	return rknand_boot_media;
}
EXPORT_SYMBOL(rknand_get_boot_media);

unsigned long rk_copy_from_user(void *to, const void __user *from,
				unsigned long n)
{
	return copy_from_user(to, from, n);
}

unsigned long rk_copy_to_user(void __user *to, const void *from,
			      unsigned long n)
{
	return copy_to_user(to, from, n);
}

static const struct file_operations rknand_sys_storage_fops = {
	.compat_ioctl = rknand_sys_storage_ioctl,
	.unlocked_ioctl = rknand_sys_storage_ioctl,
};

static struct miscdevice rknand_sys_storage_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "rknand_sys_storage",
	.fops  = &rknand_sys_storage_fops,
};

int rknand_sys_storage_init(void)
{
	return misc_register(&rknand_sys_storage_dev);
}

static const struct file_operations rknand_vendor_storage_fops = {
	.compat_ioctl	= rk_ftl_vendor_storage_ioctl,
	.unlocked_ioctl = rk_ftl_vendor_storage_ioctl,
};

static struct miscdevice rknand_vender_storage_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "vendor_storage",
	.fops  = &rknand_vendor_storage_fops,
};

int rknand_vendor_storage_init(void)
{
	return misc_register(&rknand_vender_storage_dev);
}

int rk_nand_schedule_enable_config(int en)
{
	int tmp = rk_nand_wait_busy_schedule;

	rk_nand_wait_busy_schedule = en;
	return tmp;
}

static void rk_nand_iqr_timeout_hack(unsigned long data)
{
	del_timer(&rk_nand_iqr_timeout);
	rk_timer_add = 0;
	nandc0_xfer_completed_flag = 1;
	nandc0_ready_completed_flag = 1;
	nandc1_xfer_completed_flag = 1;
	nandc1_ready_completed_flag = 1;
	wake_up(&rk29_nandc_wait);
}

static void rk_add_timer(void)
{
	if (rk_timer_add == 0) {
		rk_timer_add = 1;
		rk_nand_iqr_timeout.expires = jiffies + HZ / 50;
		add_timer(&rk_nand_iqr_timeout);
	}
}

static void rk_del_timer(void)
{
	if (rk_timer_add)
		del_timer(&rk_nand_iqr_timeout);
	rk_timer_add = 0;
}

static irqreturn_t rk_nandc_interrupt(int irq, void *dev_id)
{
	unsigned int irq_status = rk_nandc_get_irq_status(dev_id);

	if (irq_status & (1 << 0)) {
		rk_nandc_flash_xfer_completed(dev_id);
		if (dev_id == g_nandc_info[0].reg_base)
			nandc0_xfer_completed_flag = 1;
		else
			nandc1_xfer_completed_flag = 1;
	}

	if (irq_status & (1 << 1)) {
		rk_nandc_flash_ready(dev_id);
		if (dev_id == g_nandc_info[0].reg_base)
			nandc0_ready_completed_flag = 1;
		else
			nandc1_ready_completed_flag = 1;
	}

	wake_up(&rk29_nandc_wait);
	return IRQ_HANDLED;
}

void rk_nandc_xfer_irq_flag_init(void *nandc_reg)
{
	if (nandc_reg == g_nandc_info[0].reg_base)
		nandc0_xfer_completed_flag = 0;
	else
		nandc1_xfer_completed_flag = 0;
}

void rk_nandc_rb_irq_flag_init(void *nandc_reg)
{
	if (nandc_reg == g_nandc_info[0].reg_base)
		nandc0_ready_completed_flag = 0;
	else
		nandc1_ready_completed_flag = 0;
}

void wait_for_nandc_xfer_completed(void *nandc_reg)
{
	if (rk_nand_wait_busy_schedule)	{
		rk_add_timer();
		if (nandc_reg == g_nandc_info[0].reg_base)
			wait_event(rk29_nandc_wait, nandc0_xfer_completed_flag);
		else
			wait_event(rk29_nandc_wait, nandc1_xfer_completed_flag);
		rk_del_timer();
	}
	if (nandc_reg == g_nandc_info[0].reg_base)
		nandc0_xfer_completed_flag = 0;
	else
		nandc1_xfer_completed_flag = 0;
}

void wait_for_nand_flash_ready(void *nandc_reg)
{
	if (rk_nand_wait_busy_schedule)	{
		rk_add_timer();
		if (nandc_reg == g_nandc_info[0].reg_base)
			wait_event(rk29_nandc_wait
				, nandc0_ready_completed_flag);
		else
			wait_event(rk29_nandc_wait
				, nandc1_ready_completed_flag);
		rk_del_timer();
	}
	if (nandc_reg == g_nandc_info[0].reg_base)
		nandc0_ready_completed_flag = 0;
	else
		nandc1_ready_completed_flag = 0;
}

static int rk_nandc_irq_config(int id, int mode, void *pfun)
{
	int ret = 0;
	int irq = g_nandc_info[id].irq;

	if (mode)
		ret = request_irq(irq, pfun, 0, "nandc"
			, g_nandc_info[id].reg_base);
	else
		free_irq(irq,  NULL);
	return ret;
}

int rk_nandc_irq_init(void)
{
	int ret = 0;

	rk_timer_add = 0;
	nandc0_ready_completed_flag = 0;
	nandc0_xfer_completed_flag = 0;
	ret = rk_nandc_irq_config(0, 1, rk_nandc_interrupt);

	if (g_nandc_info[1].reg_base != 0) {
		nandc1_ready_completed_flag = 0;
		nandc1_xfer_completed_flag = 0;
		rk_nandc_irq_config(1, 1, rk_nandc_interrupt);
	}
	return ret;
}

int rk_nandc_irq_deinit(void)
{
	int ret = 0;

	rk_nandc_irq_config(0, 0, rk_nandc_interrupt);
	if (g_nandc_info[1].reg_base != 0)
		rk_nandc_irq_config(1, 0, rk_nandc_interrupt);
	return ret;
}

static int rknand_probe(struct platform_device *pdev)
{
	unsigned int id = 0;
	int irq;
	struct resource	*mem;
	void __iomem	*membase;

	g_nand_device = &pdev->dev;
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	membase = devm_ioremap_resource(&pdev->dev, mem);
	if (membase == 0) {
		dev_err(&pdev->dev, "no reg resource?\n");
		return -1;
	}

	#ifdef CONFIG_OF
	of_property_read_u32(pdev->dev.of_node, "nandc_id", &id);
	pdev->id = id;
	#endif

	if (id == 0) {
		memcpy(nand_idb_data, membase + 0x1000, 0x800);
		if (*(int *)(&nand_idb_data[0]) == 0x44535953) {
			rknand_boot_media = *(int *)(&nand_idb_data[8]);
			if (rknand_boot_media == 2) /*boot from emmc*/
				return -1;
		}
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return irq;
	}

	g_nandc_info[id].id = id;
	g_nandc_info[id].irq = irq;
	g_nandc_info[id].reg_base = membase;

	g_nandc_info[id].hclk = devm_clk_get(&pdev->dev, "hclk_nandc");
	g_nandc_info[id].clk = devm_clk_get(&pdev->dev, "clk_nandc");
	g_nandc_info[id].gclk = devm_clk_get(&pdev->dev, "g_clk_nandc");

	if (unlikely(IS_ERR(g_nandc_info[id].hclk))) {
		dev_err(&pdev->dev, "rknand_probe get hclk error\n");
		return PTR_ERR(g_nandc_info[id].hclk);
	}

	if (!(IS_ERR(g_nandc_info[id].clk))) {
		clk_set_rate(g_nandc_info[id].clk, 150 * 1000 * 1000);
		g_nandc_info[id].clk_rate = clk_get_rate(g_nandc_info[id].clk);
		clk_prepare_enable(g_nandc_info[id].clk);
		dev_info(&pdev->dev,
			 "rknand_probe clk rate = %d\n",
			 g_nandc_info[id].clk_rate);
	}

	clk_prepare_enable(g_nandc_info[id].hclk);
	if (!(IS_ERR(g_nandc_info[id].gclk)))
		clk_prepare_enable(g_nandc_info[id].gclk);

	return 0;
}

static int rknand_suspend(struct platform_device *pdev, pm_message_t state)
{
	if (rk_nand_suspend_state == 0) {
		rk_nand_suspend_state = 1;
		rknand_dev_suspend();
	}
	return 0;
}

static int rknand_resume(struct platform_device *pdev)
{
	if (rk_nand_suspend_state == 1) {
		rk_nand_suspend_state = 0;
		rknand_dev_resume();
	}
	return 0;
}

static void rknand_shutdown(struct platform_device *pdev)
{
	if (rk_nand_shutdown_state == 0) {
		rk_nand_shutdown_state = 1;
		rknand_dev_shutdown();
	}
}

void rknand_dev_cache_flush(void)
{
	rknand_dev_flush();
}

#ifdef CONFIG_OF
static const struct of_device_id of_rk_nandc_match[] = {
	{.compatible = "rockchip,rk-nandc"},
	{}
};
#endif

static struct platform_driver rknand_driver = {
	.probe		= rknand_probe,
	.suspend	= rknand_suspend,
	.resume		= rknand_resume,
	.shutdown	= rknand_shutdown,
	.driver		= {
		.name	= "rknand",
#ifdef CONFIG_OF
		.of_match_table	= of_rk_nandc_match,
#endif
		.owner	= THIS_MODULE,
	},
};

static void __exit rknand_driver_exit(void)
{
	rknand_dev_exit();
	platform_driver_unregister(&rknand_driver);
}

static int __init rknand_driver_init(void)
{
	int ret = 0;

	pr_err("%s\n", RKNAND_VERSION_AND_DATE);
	ret = platform_driver_register(&rknand_driver);
	if (ret == 0)
		ret = rknand_dev_init();
	return ret;
}

module_init(rknand_driver_init);
module_exit(rknand_driver_exit);
MODULE_ALIAS(DRIVER_NAME);
