// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <asm/cacheflush.h>
#include <linux/bootmem.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#endif

#include "nandc.h"
#include "rkflash_api.h"
#include "rkflash_blk.h"

#define RKNANDC_VERSION_AND_DATE	"rknandc_base v1.1 2017-01-11"
#define	RKNANDC_CLK_SET_RATE		(150 * 1000 * 1000)

struct rknandc_info {
	void __iomem	*reg_base;
	int	irq;
	int	clk_rate;
	struct clk	*clk;		/* controller's clk*/
	struct clk	*ahb_clk;	/* ahb clk gate*/
	struct clk	*g_clk;		/* clk_src_en gate*/
};

static struct rknandc_info g_nandc_info;
static struct device *g_nandc_dev;
static struct completion nandc_irq_complete;

unsigned long rknandc_dma_map_single(unsigned long ptr, int size, int dir)
{
#ifdef CONFIG_ARM64
	__dma_map_area((void *)ptr, size, dir);
	return ((unsigned long)virt_to_phys((void *)ptr));
#else
	return dma_map_single(NULL, (void *)ptr, size
		, dir ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
#endif
}

void rknandc_dma_unmap_single(unsigned long ptr, int size, int dir)
{
#ifdef CONFIG_ARM64
	__dma_unmap_area(phys_to_virt(ptr), size, dir);
#else
	dma_unmap_single(NULL, (dma_addr_t)ptr, size
		, dir ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
#endif
}

static irqreturn_t rknandc_interrupt(int irq, void *dev_id)
{
	nandc_clean_irq();
	complete(&nandc_irq_complete);
	return IRQ_HANDLED;
}

static int rknandc_irq_config(int mode, void *pfun)
{
	int ret = 0;
	int irq = g_nandc_info.irq;

	if (mode)
		ret = request_irq(irq, pfun, 0, "rknandc",
				  g_nandc_info.reg_base);
	else
		free_irq(irq,  NULL);
	return ret;
}

static int rknandc_irq_init(void)
{
	init_completion(&nandc_irq_complete);
	rknandc_irq_config(1, rknandc_interrupt);
	return 0;
}

static int rknandc_irq_deinit(void)
{
	rknandc_irq_config(0, rknandc_interrupt);
	return 0;
}

static int rknandc_probe(struct platform_device *pdev)
{
	int irq;
	struct resource	*mem;
	void __iomem	*membase;
	int ret;

	g_nandc_dev = &pdev->dev;
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	membase = devm_ioremap_resource(&pdev->dev, mem);
	if (!membase) {
		dev_err(&pdev->dev, "no reg resource?\n");
		return -1;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return irq;
	}

	g_nandc_info.irq = irq;
	g_nandc_info.reg_base = membase;
	g_nandc_info.ahb_clk = devm_clk_get(&pdev->dev, "hclk_nandc");
	g_nandc_info.clk = devm_clk_get(&pdev->dev, "clk_nandc");
	g_nandc_info.g_clk = devm_clk_get(&pdev->dev, "g_clk_nandc");
	if (unlikely(IS_ERR(g_nandc_info.clk)) ||
	    unlikely(IS_ERR(g_nandc_info.ahb_clk))) {
		dev_err(&pdev->dev, "%s get clk error\n", __func__);
		return -1;
	}
	clk_prepare_enable(g_nandc_info.g_clk);
	clk_prepare_enable(g_nandc_info.ahb_clk);
	clk_set_rate(g_nandc_info.clk, RKNANDC_CLK_SET_RATE);
	g_nandc_info.clk_rate = clk_get_rate(g_nandc_info.clk);
	clk_prepare_enable(g_nandc_info.clk);
	dev_info(&pdev->dev,
		 "%s clk rate = %d\n",
		 __func__,
		 g_nandc_info.clk_rate);
	rknandc_irq_init();
	ret = rkflash_dev_init(g_nandc_info.reg_base, FLASH_CON_TYPE_NANDC);

	return ret;
}

static int rknandc_suspend(struct platform_device *pdev, pm_message_t state)
{
	return rkflash_dev_suspend();
}

static int rknandc_resume(struct platform_device *pdev)
{
	return rkflash_dev_resume(g_nandc_info.reg_base);
}

static void rknandc_shutdown(struct platform_device *pdev)
{
	rkflash_dev_shutdown();
}

#ifdef CONFIG_OF
static const struct of_device_id of_rknandc_match[] = {
	{.compatible = "rockchip,nandc"},
	{}
};
#endif

static struct platform_driver rknandc_driver = {
	.probe		= rknandc_probe,
	.suspend	= rknandc_suspend,
	.resume		= rknandc_resume,
	.shutdown	= rknandc_shutdown,
	.driver		= {
		.name	= "rknandc",
#ifdef CONFIG_OF
		.of_match_table	= of_rknandc_match,
#endif
	},
};

static void __exit rknandc_driver_exit(void)
{
	rkflash_dev_exit();
	rknandc_irq_deinit();
	platform_driver_unregister(&rknandc_driver);
}

static int __init rknandc_driver_init(void)
{
	int ret = 0;

	pr_err("%s\n", RKNANDC_VERSION_AND_DATE);
	ret = platform_driver_register(&rknandc_driver);
	return ret;
}

module_init(rknandc_driver_init);
module_exit(rknandc_driver_exit);
MODULE_ALIAS(DRIVER_NAME);
