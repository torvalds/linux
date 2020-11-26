// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
//
// Authors:
//   Ramil Zaripov <Ramil.Zaripov@baikalelectronics.ru>
//   Serge Semin <Sergey.Semin@baikalelectronics.ru>
//
// Baikal-T1 DW APB SPI and System Boot SPI driver
//

#include <linux/clk.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mux/consumer.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spi/spi-mem.h>
#include <linux/spi/spi.h>

#include "spi-dw.h"

#define BT1_BOOT_DIRMAP		0
#define BT1_BOOT_REGS		1

struct dw_spi_bt1 {
	struct dw_spi		dws;
	struct clk		*clk;
	struct mux_control	*mux;

#ifdef CONFIG_SPI_DW_BT1_DIRMAP
	void __iomem		*map;
	resource_size_t		map_len;
#endif
};
#define to_dw_spi_bt1(_ctlr) \
	container_of(spi_controller_get_devdata(_ctlr), struct dw_spi_bt1, dws)

typedef int (*dw_spi_bt1_init_cb)(struct platform_device *pdev,
				    struct dw_spi_bt1 *dwsbt1);

#ifdef CONFIG_SPI_DW_BT1_DIRMAP

static int dw_spi_bt1_dirmap_create(struct spi_mem_dirmap_desc *desc)
{
	struct dw_spi_bt1 *dwsbt1 = to_dw_spi_bt1(desc->mem->spi->controller);

	if (!dwsbt1->map ||
	    !dwsbt1->dws.mem_ops.supports_op(desc->mem, &desc->info.op_tmpl))
		return -EOPNOTSUPP;

	/*
	 * Make sure the requested region doesn't go out of the physically
	 * mapped flash memory bounds and the operation is read-only.
	 */
	if (desc->info.offset + desc->info.length > dwsbt1->map_len ||
	    desc->info.op_tmpl.data.dir != SPI_MEM_DATA_IN)
		return -EOPNOTSUPP;

	return 0;
}

/*
 * Directly mapped SPI memory region is only accessible in the dword chunks.
 * That's why we have to create a dedicated read-method to copy data from there
 * to the passed buffer.
 */
static void dw_spi_bt1_dirmap_copy_from_map(void *to, void __iomem *from, size_t len)
{
	size_t shift, chunk;
	u32 data;

	/*
	 * We split the copying up into the next three stages: unaligned head,
	 * aligned body, unaligned tail.
	 */
	shift = (size_t)from & 0x3;
	if (shift) {
		chunk = min_t(size_t, 4 - shift, len);
		data = readl_relaxed(from - shift);
		memcpy(to, &data + shift, chunk);
		from += chunk;
		to += chunk;
		len -= chunk;
	}

	while (len >= 4) {
		data = readl_relaxed(from);
		memcpy(to, &data, 4);
		from += 4;
		to += 4;
		len -= 4;
	}

	if (len) {
		data = readl_relaxed(from);
		memcpy(to, &data, len);
	}
}

static ssize_t dw_spi_bt1_dirmap_read(struct spi_mem_dirmap_desc *desc,
				      u64 offs, size_t len, void *buf)
{
	struct dw_spi_bt1 *dwsbt1 = to_dw_spi_bt1(desc->mem->spi->controller);
	struct dw_spi *dws = &dwsbt1->dws;
	struct spi_mem *mem = desc->mem;
	struct dw_spi_cfg cfg;
	int ret;

	/*
	 * Make sure the requested operation length is valid. Truncate the
	 * length if it's greater than the length of the MMIO region.
	 */
	if (offs >= dwsbt1->map_len || !len)
		return 0;

	len = min_t(size_t, len, dwsbt1->map_len - offs);

	/* Collect the controller configuration required by the operation */
	cfg.tmode = SPI_TMOD_EPROMREAD;
	cfg.dfs = 8;
	cfg.ndf = 4;
	cfg.freq = mem->spi->max_speed_hz;

	/* Make sure the corresponding CS is de-asserted on transmission */
	dw_spi_set_cs(mem->spi, false);

	spi_enable_chip(dws, 0);

	dw_spi_update_config(dws, mem->spi, &cfg);

	spi_umask_intr(dws, SPI_INT_RXFI);

	spi_enable_chip(dws, 1);

	/*
	 * Enable the transparent mode of the System Boot Controller.
	 * The SPI core IO should have been locked before calling this method
	 * so noone would be touching the controller' registers during the
	 * dirmap operation.
	 */
	ret = mux_control_select(dwsbt1->mux, BT1_BOOT_DIRMAP);
	if (ret)
		return ret;

	dw_spi_bt1_dirmap_copy_from_map(buf, dwsbt1->map + offs, len);

	mux_control_deselect(dwsbt1->mux);

	dw_spi_set_cs(mem->spi, true);

	ret = dw_spi_check_status(dws, true);

	return ret ?: len;
}

#endif /* CONFIG_SPI_DW_BT1_DIRMAP */

static int dw_spi_bt1_std_init(struct platform_device *pdev,
			       struct dw_spi_bt1 *dwsbt1)
{
	struct dw_spi *dws = &dwsbt1->dws;

	dws->irq = platform_get_irq(pdev, 0);
	if (dws->irq < 0)
		return dws->irq;

	dws->num_cs = 4;

	/*
	 * Baikal-T1 Normal SPI Controllers don't always keep up with full SPI
	 * bus speed especially when it comes to the concurrent access to the
	 * APB bus resources. Thus we have no choice but to set a constraint on
	 * the SPI bus frequency for the memory operations which require to
	 * read/write data as fast as possible.
	 */
	dws->max_mem_freq = 20000000U;

	dw_spi_dma_setup_generic(dws);

	return 0;
}

static int dw_spi_bt1_sys_init(struct platform_device *pdev,
			       struct dw_spi_bt1 *dwsbt1)
{
	struct resource *mem __maybe_unused;
	struct dw_spi *dws = &dwsbt1->dws;

	/*
	 * Baikal-T1 System Boot Controller is equipped with a mux, which
	 * switches between the directly mapped SPI flash access mode and
	 * IO access to the DW APB SSI registers. Note the mux controller
	 * must be setup to preserve the registers being accessible by default
	 * (on idle-state).
	 */
	dwsbt1->mux = devm_mux_control_get(&pdev->dev, NULL);
	if (IS_ERR(dwsbt1->mux))
		return PTR_ERR(dwsbt1->mux);

	/*
	 * Directly mapped SPI flash memory is a 16MB MMIO region, which can be
	 * used to access a peripheral memory device just by reading/writing
	 * data from/to it. Note the system APB bus will stall during each IO
	 * from/to the dirmap region until the operation is finished. So don't
	 * use it concurrently with time-critical tasks (like the SPI memory
	 * operations implemented in the DW APB SSI driver).
	 */
#ifdef CONFIG_SPI_DW_BT1_DIRMAP
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (mem) {
		dwsbt1->map = devm_ioremap_resource(&pdev->dev, mem);
		if (!IS_ERR(dwsbt1->map)) {
			dwsbt1->map_len = (mem->end - mem->start + 1);
			dws->mem_ops.dirmap_create = dw_spi_bt1_dirmap_create;
			dws->mem_ops.dirmap_read = dw_spi_bt1_dirmap_read;
		} else {
			dwsbt1->map = NULL;
		}
	}
#endif /* CONFIG_SPI_DW_BT1_DIRMAP */

	/*
	 * There is no IRQ, no DMA and just one CS available on the System Boot
	 * SPI controller.
	 */
	dws->irq = IRQ_NOTCONNECTED;
	dws->num_cs = 1;

	/*
	 * Baikal-T1 System Boot SPI Controller doesn't keep up with the full
	 * SPI bus speed due to relatively slow APB bus and races for it'
	 * resources from different CPUs. The situation is worsen by a small
	 * FIFOs depth (just 8 words). It works better in a single CPU mode
	 * though, but still tends to be not fast enough at low CPU
	 * frequencies.
	 */
	if (num_possible_cpus() > 1)
		dws->max_mem_freq = 10000000U;
	else
		dws->max_mem_freq = 20000000U;

	return 0;
}

static int dw_spi_bt1_probe(struct platform_device *pdev)
{
	dw_spi_bt1_init_cb init_func;
	struct dw_spi_bt1 *dwsbt1;
	struct resource *mem;
	struct dw_spi *dws;
	int ret;

	dwsbt1 = devm_kzalloc(&pdev->dev, sizeof(struct dw_spi_bt1), GFP_KERNEL);
	if (!dwsbt1)
		return -ENOMEM;

	dws = &dwsbt1->dws;

	dws->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(dws->regs))
		return PTR_ERR(dws->regs);

	dws->paddr = mem->start;

	dwsbt1->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dwsbt1->clk))
		return PTR_ERR(dwsbt1->clk);

	ret = clk_prepare_enable(dwsbt1->clk);
	if (ret)
		return ret;

	dws->bus_num = pdev->id;
	dws->reg_io_width = 4;
	dws->max_freq = clk_get_rate(dwsbt1->clk);
	if (!dws->max_freq)
		goto err_disable_clk;

	init_func = device_get_match_data(&pdev->dev);
	ret = init_func(pdev, dwsbt1);
	if (ret)
		goto err_disable_clk;

	pm_runtime_enable(&pdev->dev);

	ret = dw_spi_add_host(&pdev->dev, dws);
	if (ret)
		goto err_disable_clk;

	platform_set_drvdata(pdev, dwsbt1);

	return 0;

err_disable_clk:
	clk_disable_unprepare(dwsbt1->clk);

	return ret;
}

static int dw_spi_bt1_remove(struct platform_device *pdev)
{
	struct dw_spi_bt1 *dwsbt1 = platform_get_drvdata(pdev);

	dw_spi_remove_host(&dwsbt1->dws);

	pm_runtime_disable(&pdev->dev);

	clk_disable_unprepare(dwsbt1->clk);

	return 0;
}

static const struct of_device_id dw_spi_bt1_of_match[] = {
	{ .compatible = "baikal,bt1-ssi", .data = dw_spi_bt1_std_init},
	{ .compatible = "baikal,bt1-sys-ssi", .data = dw_spi_bt1_sys_init},
	{ }
};
MODULE_DEVICE_TABLE(of, dw_spi_bt1_of_match);

static struct platform_driver dw_spi_bt1_driver = {
	.probe	= dw_spi_bt1_probe,
	.remove	= dw_spi_bt1_remove,
	.driver	= {
		.name		= "bt1-sys-ssi",
		.of_match_table	= dw_spi_bt1_of_match,
	},
};
module_platform_driver(dw_spi_bt1_driver);

MODULE_AUTHOR("Serge Semin <Sergey.Semin@baikalelectronics.ru>");
MODULE_DESCRIPTION("Baikal-T1 System Boot SPI Controller driver");
MODULE_LICENSE("GPL v2");
