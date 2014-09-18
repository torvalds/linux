/*
 * rockchip spi interface driver for DW SPI Core
 *
 * Copyright (c) 2014, ROCKCHIP Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_data/spi-rockchip.h>

#include "spi-rockchip-core.h"


#define DRIVER_NAME "rockchip_spi_driver_data"
#define SPI_MAX_FREQUENCY	24000000

struct rockchip_spi_driver_data {
	struct platform_device *pdev;
	struct dw_spi	dws;
	struct rockchip_spi_info *info;
	struct clk                      *clk_spi;
	struct clk                      *pclk_spi;
};

#ifdef CONFIG_OF
static struct rockchip_spi_info *rockchip_spi_parse_dt(struct device *dev)
{
	struct rockchip_spi_info *info;
	u32 temp;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(dev, "memory allocation for spi_info failed\n");
		return ERR_PTR(-ENOMEM);
	}

	if (of_property_read_u32(dev->of_node, "rockchip,spi-src-clk", &temp)) {
		dev_warn(dev, "spi bus clock parent not specified, using clock at index 0 as parent\n");
		info->src_clk_nr = 0;
	} else {
		info->src_clk_nr = temp;
	}
#if 0
	if (of_property_read_u32(dev->of_node, "bus-num", &temp)) {
		dev_warn(dev, "number of bus not specified, assuming bus 0\n");
		info->bus_num= 0;
	} else {
		info->bus_num = temp;
	}
#endif
	if (of_property_read_u32(dev->of_node, "num-cs", &temp)) {
		dev_warn(dev, "number of chip select lines not specified, assuming 1 chip select line\n");
		info->num_cs = 1;
	} else {
		info->num_cs = temp;
	}

	if (of_property_read_u32(dev->of_node, "max-freq", &temp)) {
		dev_warn(dev, "fail to get max-freq,default set %dHZ\n",SPI_MAX_FREQUENCY);
		info->spi_freq = SPI_MAX_FREQUENCY;
	} else {
		info->spi_freq = temp;
	}
	
	//printk("%s:line=%d,src_clk_nr=%d,bus_num=%d,num_cs=%d\n",__func__, __LINE__,info->src_clk_nr,info->bus_num,info->num_cs);
	
	return info;
}
#else
static struct rockchip_spi_info *rockchip_spi_parse_dt(struct device *dev)
{
	return dev->platform_data;
}
#endif


static int rockchip_spi_probe(struct platform_device *pdev)
{
	struct resource	*mem_res;
	struct rockchip_spi_driver_data *sdd;
	struct rockchip_spi_info *info = pdev->dev.platform_data;
	struct dw_spi *dws;
	int ret, irq;
	char clk_name[16];

	if (!info && pdev->dev.of_node) {
		info = rockchip_spi_parse_dt(&pdev->dev);
		if (IS_ERR(info))
			return PTR_ERR(info);
	}

	if (!info) {
		dev_err(&pdev->dev, "platform_data missing!\n");
		return -ENODEV;
	}	

	sdd = kzalloc(sizeof(struct rockchip_spi_driver_data), GFP_KERNEL);
	if (!sdd) {
		ret = -ENOMEM;
		goto err_kfree;
	}

	
	sdd->pdev = pdev;
	sdd->info = info;
	dws = &sdd->dws;

	atomic_set(&dws->debug_flag, 0);//debug flag

	/* Get basic io resource and map it */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_warn(&pdev->dev, "Failed to get IRQ: %d\n", irq);
		return irq;
	}
	
	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem_res == NULL) {
		dev_err(&pdev->dev, "Unable to get SPI MEM resource\n");
		ret =  -ENXIO;
		goto err_unmap;
	}
	
	dws->regs = ioremap(mem_res->start, (mem_res->end - mem_res->start) + 1);
	if (!dws->regs){
		ret = -EBUSY;
		goto err_unmap;
	}

	dws->paddr = mem_res->start;
	dws->iolen = (mem_res->end - mem_res->start) + 1;
	
	printk(KERN_INFO "dws->regs: %p\n", dws->regs);

	//get bus num
	if (pdev->dev.of_node) {
		ret = of_alias_get_id(pdev->dev.of_node, "spi");
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to get alias id, errno %d\n",
				ret);
			goto err_release_mem;
		}
		info->bus_num = ret;
	} else {
		info->bus_num = pdev->id;
	}

	/* Setup clocks */
	sdd->clk_spi = devm_clk_get(&pdev->dev, "spi");
	if (IS_ERR(sdd->clk_spi)) {
		dev_err(&pdev->dev, "Unable to acquire clock 'spi'\n");
		ret = PTR_ERR(sdd->clk_spi);
		goto err_clk;
	}

	if (clk_prepare_enable(sdd->clk_spi)) {
		dev_err(&pdev->dev, "Couldn't enable clock 'spi'\n");
		ret = -EBUSY;
		goto err_clk;
	}
	
	sprintf(clk_name, "pclk_spi%d", info->src_clk_nr);
	sdd->pclk_spi = devm_clk_get(&pdev->dev, clk_name);
	if (IS_ERR(sdd->pclk_spi)) {
		dev_err(&pdev->dev,
			"Unable to acquire clock '%s'\n", clk_name);
		ret = PTR_ERR(sdd->pclk_spi);
		goto err_pclk;
	}

	if (clk_prepare_enable(sdd->pclk_spi)) {
		dev_err(&pdev->dev, "Couldn't enable clock '%s'\n", clk_name);
		ret = -EBUSY;
		goto err_pclk;
	}

	clk_set_rate(sdd->clk_spi, info->spi_freq);
	
	dws->max_freq = clk_get_rate(sdd->clk_spi);
	dws->parent_dev = &pdev->dev;
	dws->bus_num = info->bus_num;
	dws->num_cs = info->num_cs;
	dws->irq = irq;
	dws->clk_spi = sdd->clk_spi;	
	dws->pclk_spi = sdd->pclk_spi;

	/*
	 * handling for rockchip paltforms, like dma setup,
	 * clock rate, FIFO depth.
	 */
	
#ifdef CONFIG_SPI_ROCKCHIP_DMA
	ret = dw_spi_dma_init(dws);
	if (ret)
	printk("%s:fail to init dma\n",__func__);
#endif

	ret = dw_spi_add_host(dws);
	if (ret)
		goto err_release_mem;

	platform_set_drvdata(pdev, sdd);

	printk("%s:num_cs=%d,bus_num=%d,irq=%d,freq=%d ok\n",__func__, info->num_cs, info->bus_num, irq, dws->max_freq);
	
	return 0;
err_release_mem:
    release_mem_region(mem_res->start, (mem_res->end - mem_res->start) + 1);
err_pclk:
	clk_disable_unprepare(sdd->pclk_spi);
err_clk:
	clk_disable_unprepare(sdd->clk_spi);
err_unmap:
	iounmap(dws->regs);
err_kfree:
	kfree(sdd);
	return ret;
}

static int rockchip_spi_remove(struct platform_device *pdev)
{
	struct rockchip_spi_driver_data *sdd = platform_get_drvdata(pdev);
	
	platform_set_drvdata(pdev, NULL);
	dw_spi_remove_host(&sdd->dws);
	iounmap(sdd->dws.regs);
	kfree(sdd);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rockchip_spi_suspend(struct device *dev)
{
	struct rockchip_spi_driver_data *sdd = dev_get_drvdata(dev);
	int ret = 0;
	
	ret = dw_spi_suspend_host(&sdd->dws);
	
	return ret;
}

static int rockchip_spi_resume(struct device *dev)
{
	struct rockchip_spi_driver_data *sdd = dev_get_drvdata(dev);
	int ret = 0;
	
	ret = dw_spi_resume_host(&sdd->dws);	

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM_RUNTIME
static int rockchip_spi_runtime_suspend(struct device *dev)
{
	struct rockchip_spi_driver_data *sdd = dev_get_drvdata(dev);
	struct dw_spi *dws = &sdd->dws;
	
	clk_disable_unprepare(sdd->clk_spi);
	clk_disable_unprepare(sdd->pclk_spi);

	
	DBG_SPI("%s\n",__func__);
	
	return 0;
}

static int rockchip_spi_runtime_resume(struct device *dev)
{
	struct rockchip_spi_driver_data *sdd = dev_get_drvdata(dev);
	struct dw_spi *dws = &sdd->dws;

	clk_prepare_enable(sdd->pclk_spi);
	clk_prepare_enable(sdd->clk_spi);
	
	DBG_SPI("%s\n",__func__);
	return 0;
}
#endif /* CONFIG_PM_RUNTIME */

static const struct dev_pm_ops rockchip_spi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_spi_suspend, rockchip_spi_resume)
	SET_RUNTIME_PM_OPS(rockchip_spi_runtime_suspend,
			   rockchip_spi_runtime_resume, NULL)
};

#ifdef CONFIG_OF
static const struct of_device_id rockchip_spi_dt_match[] = {
	{ .compatible = "rockchip,rockchip-spi",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_spi_dt_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_spi_driver = {
	.driver = {
		.name	= "rockchip-spi",
		.owner = THIS_MODULE,
		.pm = &rockchip_spi_pm,
		.of_match_table = of_match_ptr(rockchip_spi_dt_match),
	},
	.remove = rockchip_spi_remove,
};
MODULE_ALIAS("platform:rockchip-spi");

static int __init rockchip_spi_init(void)
{
	return platform_driver_probe(&rockchip_spi_driver, rockchip_spi_probe);
}
module_init(rockchip_spi_init);

static void __exit rockchip_spi_exit(void)
{
	platform_driver_unregister(&rockchip_spi_driver);
}
module_exit(rockchip_spi_exit);

MODULE_AUTHOR("Luo Wei <lw@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP SPI Controller Driver");
MODULE_LICENSE("GPL");
