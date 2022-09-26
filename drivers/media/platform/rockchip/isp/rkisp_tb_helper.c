// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 Rockchip Electronics Co., Ltd. */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <linux/dma-buf.h>
#include <linux/highmem.h>
#include <linux/soc/rockchip/rockchip_thunderboot_service.h>

#include "rkisp_tb_helper.h"

static struct platform_device *rkisp_tb_pdev;
static struct clk_bulk_data *rkisp_tb_clk;
static int rkisp_tb_clk_num;
static struct rk_tb_client tb_cl;

struct shm_data {
	int npages;
	struct page *pages[];
};

static struct sg_table *shm_map_dma_buf(struct dma_buf_attachment *attachment,
					enum dma_data_direction dir)
{
	struct shm_data *data = attachment->dmabuf->priv;
	struct sg_table *table;
	struct scatterlist *sg;
	int i;

	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return ERR_PTR(-ENOMEM);

	sg_alloc_table(table, data->npages, GFP_KERNEL);
	sg = table->sgl;
	for (i = 0; i < data->npages; i++) {
		sg_set_page(sg, data->pages[i], PAGE_SIZE, 0);
		sg = sg_next(sg);
	}

	dma_map_sg_attrs(attachment->dev, table->sgl, table->nents, dir, DMA_ATTR_SKIP_CPU_SYNC);

	return table;
}

static void shm_unmap_dma_buf(struct dma_buf_attachment *attachment,
			      struct sg_table *table,
			      enum dma_data_direction dir)
{
	dma_unmap_sg(attachment->dev, table->sgl, table->nents, dir);
	sg_free_table(table);
	kfree(table);
}

static void shm_release(struct dma_buf *dma_buf)
{
	struct shm_data *data = dma_buf->priv;

	kfree(data);
}

static void *shm_vmap(struct dma_buf *dma_buf)
{
	struct shm_data *data = dma_buf->priv;

	return vm_map_ram(data->pages, data->npages, 0);
}

static void shm_vunmap(struct dma_buf *dma_buf, void *vaddr)
{
	struct shm_data *data = dma_buf->priv;

	vm_unmap_ram(vaddr, data->npages);
}

static int shm_mmap(struct dma_buf *dma_buf, struct vm_area_struct *vma)
{
	struct shm_data *data = dma_buf->priv;
	unsigned long vm_start = vma->vm_start;
	int i;

	for (i = 0; i < data->npages; i++) {
		remap_pfn_range(vma, vm_start, page_to_pfn(data->pages[i]),
				PAGE_SIZE, vma->vm_page_prot);
		vm_start += PAGE_SIZE;
	}

	return 0;
}

static int shm_begin_cpu_access(struct dma_buf *dmabuf, enum dma_data_direction dir)
{
	struct dma_buf_attachment *attachment;
	struct sg_table *table;

	attachment = list_first_entry(&dmabuf->attachments, struct dma_buf_attachment, node);
	table = attachment->priv;
	dma_sync_sg_for_cpu(NULL, table->sgl, table->nents, dir);

	return 0;
}

static int shm_end_cpu_access(struct dma_buf *dmabuf, enum dma_data_direction dir)
{
	struct dma_buf_attachment *attachment;
	struct sg_table *table;

	attachment = list_first_entry(&dmabuf->attachments, struct dma_buf_attachment, node);
	table = attachment->priv;
	dma_sync_sg_for_device(NULL, table->sgl, table->nents, dir);

	return 0;
}

static const struct dma_buf_ops shm_dmabuf_ops = {
	.map_dma_buf = shm_map_dma_buf,
	.unmap_dma_buf = shm_unmap_dma_buf,
	.release = shm_release,
	.mmap = shm_mmap,
	.vmap = shm_vmap,
	.vunmap = shm_vunmap,
	.begin_cpu_access = shm_begin_cpu_access,
	.end_cpu_access = shm_end_cpu_access,
};

static struct dma_buf *shm_alloc(struct rkisp_thunderboot_shmem *shmem)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	struct shm_data *data;
	int i, npages;

	npages = PAGE_ALIGN(shmem->shm_size) / PAGE_SIZE;
	data = kmalloc(sizeof(*data) + npages * sizeof(struct page *), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	data->npages = npages;
	for (i = 0; i < npages; i++)
		data->pages[i] = phys_to_page(shmem->shm_start + i * PAGE_SIZE);

	exp_info.ops = &shm_dmabuf_ops;
	exp_info.size = npages * PAGE_SIZE;
	exp_info.flags = O_RDWR;
	exp_info.priv = data;

	dmabuf = dma_buf_export(&exp_info);

	return dmabuf;
}

static int __maybe_unused rkisp_tb_clocks_loader_protect(void)
{
	int ret = 0;

	if (rkisp_tb_pdev) {
		pm_runtime_enable(&rkisp_tb_pdev->dev);
		pm_runtime_get_sync(&rkisp_tb_pdev->dev);
		if (rkisp_tb_clk_num) {
			ret = clk_bulk_prepare_enable(rkisp_tb_clk_num, rkisp_tb_clk);
			if (ret)
				dev_err(&rkisp_tb_pdev->dev, "Cannot enable clock\n");
		}
	}

	return ret;
}

static int __maybe_unused rkisp_tb_clocks_loader_unprotect(void)
{
	if (rkisp_tb_pdev) {
		if (rkisp_tb_clk_num)
			clk_bulk_disable_unprepare(rkisp_tb_clk_num, rkisp_tb_clk);
		pm_runtime_put_sync(&rkisp_tb_pdev->dev);
		pm_runtime_disable(&rkisp_tb_pdev->dev);
	}
	return 0;
}

static void rkisp_tb_cb(void *data)
{
	rkisp_tb_clocks_loader_unprotect();
}

static int __maybe_unused rkisp_tb_runtime_suspend(struct device *dev)
{
	return 0;
}

static int __maybe_unused rkisp_tb_runtime_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops rkisp_tb_plat_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkisp_tb_runtime_suspend,
			   rkisp_tb_runtime_resume, NULL)
};

static const struct of_device_id rkisp_tb_plat_of_match[] = {
	{
		.compatible = "rockchip,thunder-boot-rkisp",
	},
	{},
};

static int rkisp_tb_plat_probe(struct platform_device *pdev)
{
	rkisp_tb_pdev = pdev;
	rkisp_tb_clk_num = devm_clk_bulk_get_all(&pdev->dev, &rkisp_tb_clk);
	if (rkisp_tb_clk_num <= 0) {
		dev_warn(&pdev->dev, "get clk fail:%d\n", rkisp_tb_clk_num);
		rkisp_tb_clk_num = 0;
	}
	rkisp_tb_clocks_loader_protect();

	if (IS_ENABLED(CONFIG_ROCKCHIP_THUNDER_BOOT_SERVICE)) {
		tb_cl.cb = rkisp_tb_cb;
		return rk_tb_client_register_cb(&tb_cl);
	}

	return 0;
}

static int rkisp_tb_plat_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver __maybe_unused rkisp_tb_plat_drv = {
	.driver = {
		.name = "rkisp_thunderboot",
		.of_match_table = of_match_ptr(rkisp_tb_plat_of_match),
		.pm = &rkisp_tb_plat_pm_ops,
	},
	.probe = rkisp_tb_plat_probe,
	.remove = rkisp_tb_plat_remove,
};

static int __init rkisp_tb_plat_drv_init(void)
{
	return platform_driver_register(&rkisp_tb_plat_drv);
}

arch_initcall_sync(rkisp_tb_plat_drv_init);

long rkisp_tb_shm_ioctl(struct rkisp_thunderboot_shmem *shmem)
{
	struct dma_buf *dmabuf;
	int fd, ret;

	dmabuf = shm_alloc(shmem);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		return ret;
	}

	fd = dma_buf_fd(dmabuf, O_CLOEXEC);
	shmem->shm_fd = fd;

	return 0;
}

void rkisp_tb_unprotect_clk(void)
{
	if (IS_ENABLED(CONFIG_ROCKCHIP_THUNDER_BOOT_SERVICE))
		return;

	rkisp_tb_clocks_loader_unprotect();
}
EXPORT_SYMBOL(rkisp_tb_unprotect_clk);

static enum rkisp_tb_state tb_state;

void rkisp_tb_set_state(enum rkisp_tb_state result)
{
	tb_state = result;
}
EXPORT_SYMBOL(rkisp_tb_set_state);

enum rkisp_tb_state rkisp_tb_get_state(void)
{
	return tb_state;
}
EXPORT_SYMBOL(rkisp_tb_get_state);
