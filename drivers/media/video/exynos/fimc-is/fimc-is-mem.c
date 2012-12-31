/*
 * Samsung Exynos4 SoC series FIMC-IS slave interface driver
 *
 * v4l2 subdev driver interface
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 * Contact: Younghwan Joo, <yhwan.joo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/cma.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <media/videobuf2-core.h>
#include <asm/cacheflush.h>
#include <media/videobuf2-cma-phys.h>
#if defined(CONFIG_VIDEOBUF2_ION)
#include <media/videobuf2-ion.h>
#endif
#include "fimc-is-core.h"
#include "fimc-is-param.h"

#if defined(CONFIG_VIDEOBUF2_ION)
#define	FIMC_IS_ION_NAME	"exynos4-fimc-is"
#define FIMC_IS_FW_BASE_MASK		((1 << 26) - 1)

struct vb2_buffer *is_vb;
void *buf_start;

struct vb2_ion_conf {
	struct device		*dev;
	const char		*name;

	struct ion_client	*client;

	unsigned long		align;
	bool			contig;
	bool			sharable;
	bool			cacheable;
	bool			use_mmu;
	atomic_t		mmu_enable;

	spinlock_t		slock;
};

struct vb2_ion_buf {
	struct vm_area_struct		*vma;
	struct vb2_ion_conf		*conf;
	struct vb2_vmarea_handler	handler;

	struct ion_handle		*handle;	/* Kernel space */
	int				fd;		/* User space */

	dma_addr_t			kva;
	dma_addr_t			dva;
	size_t				offset;
	unsigned long			size;

	struct scatterlist		*sg;
	int				nents;

	atomic_t			ref;

	bool				cacheable;
};
#endif

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
void fimc_is_mem_cache_clean(const void *start_addr, unsigned long size)
{
	unsigned long paddr;

	dmac_map_area(start_addr, size, DMA_TO_DEVICE);
	/*
	 * virtual & phsical addrees mapped directly, so we can convert
	 * the address just using offset
	 */
	paddr = __pa((unsigned long)start_addr);
	outer_clean_range(paddr, paddr + size);
}

void fimc_is_mem_cache_inv(const void *start_addr, unsigned long size)
{
	unsigned long paddr;
	paddr = __pa((unsigned long)start_addr);
	outer_inv_range(paddr, paddr + size);
	dmac_unmap_area(start_addr, size, DMA_FROM_DEVICE);
}

int fimc_is_init_mem_mgr(struct fimc_is_dev *dev)
{
	struct cma_info mem_info;
	int err;

	/* Alloc FW memory */
	err = cma_info(&mem_info, &dev->pdev->dev, FIMC_IS_MEM_FW);
	if (err) {
		dev_err(&dev->pdev->dev, "%s: get cma info failed\n", __func__);
		return -EINVAL;
	}
	printk(KERN_INFO "%s : [cma_info] start_addr : 0x%x, end_addr : 0x%x, "
			"total_size : 0x%x, free_size : 0x%x\n",
			__func__, mem_info.lower_bound, mem_info.upper_bound,
			mem_info.total_size, mem_info.free_size);
	dev->mem.size = mem_info.total_size;
	dev->mem.base = (dma_addr_t)cma_alloc
		(&dev->pdev->dev, FIMC_IS_MEM_FW, (size_t)dev->mem.size, 0);
	dev->is_p_region =
		(struct is_region *)(phys_to_virt(dev->mem.base +
				FIMC_IS_A5_MEM_SIZE - FIMC_IS_REGION_SIZE));
	dev->is_shared_region =
		(struct is_share_region *)(phys_to_virt(dev->mem.base +
				FIMC_IS_SHARED_REGION_ADDR));
	memset((void *)dev->is_p_region, 0,
		(unsigned long)sizeof(struct is_region));
	fimc_is_mem_cache_clean((void *)dev->is_p_region, IS_PARAM_SIZE);
	printk(KERN_INFO "ctrl->mem.base = 0x%x\n", dev->mem.base);
	printk(KERN_INFO "ctrl->mem.size = 0x%x\n", dev->mem.size);

	if (dev->mem.size >= (FIMC_IS_A5_MEM_SIZE + FIMC_IS_EXTRA_MEM_SIZE)) {
		dev->mem.fw_ref_base =
				dev->mem.base + FIMC_IS_A5_MEM_SIZE + 0x1000;
		dev->mem.setfile_ref_base =
				dev->mem.base + FIMC_IS_A5_MEM_SIZE + 0x1000
						+ FIMC_IS_EXTRA_FW_SIZE;
		printk(KERN_INFO "ctrl->mem.fw_ref_base = 0x%x\n",
							dev->mem.fw_ref_base);
		printk(KERN_INFO "ctrl->mem.setfile_ref_base = 0x%x\n",
						dev->mem.setfile_ref_base);
	} else {
		dev->mem.fw_ref_base = 0;
		dev->mem.setfile_ref_base = 0;
	}
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_IS_BAYER
	err = cma_info(&mem_info, &dev->pdev->dev, FIMC_IS_MEM_ISP_BUF);
	printk(KERN_INFO "%s : [cma_info] start_addr : 0x%x, end_addr : 0x%x, "
			"total_size : 0x%x, free_size : 0x%x\n",
			__func__, mem_info.lower_bound, mem_info.upper_bound,
			mem_info.total_size, mem_info.free_size);
	if (err) {
		dev_err(&dev->pdev->dev, "%s: get cma info failed\n", __func__);
		return -EINVAL;
	}
	dev->alloc_ctx = dev->vb2->init(dev);
	if (IS_ERR(dev->alloc_ctx))
		return PTR_ERR(dev->alloc_ctx);
#endif
	return 0;
}

#elif defined(CONFIG_VIDEOBUF2_ION)
struct vb2_mem_ops *fimc_is_mem_ops(void)
{
	return (struct vb2_mem_ops *)&vb2_ion_memops;
}

void *fimc_is_mem_init(struct device *dev)
{
	struct vb2_ion vb2_ion;
	void **alloc_ctxes;
	struct vb2_drv vb2_drv = {0, };

	/* TODO */
	vb2_ion.name = FIMC_IS_ION_NAME;
	vb2_ion.dev = dev;
	vb2_ion.cacheable = true;
	vb2_ion.align = SZ_4K;
	vb2_ion.contig = false;
	vb2_drv.use_mmu = true;

	alloc_ctxes = (void **)vb2_ion_init(&vb2_ion, &vb2_drv);
	return alloc_ctxes;
}

void fimc_is_mem_init_mem_cleanup(void *alloc_ctxes)
{
	vb2_ion_cleanup(alloc_ctxes);
}

void fimc_is_mem_resume(void *alloc_ctxes)
{
	vb2_ion_resume(alloc_ctxes);
}

void fimc_is_mem_suspend(void *alloc_ctxes)
{
	vb2_ion_suspend(alloc_ctxes);
}

int fimc_is_cache_flush(struct vb2_buffer *vb,
				const void *start_addr, unsigned long size)
{
	return vb2_ion_cache_flush(vb, 1);
}

int fimc_is_cache_inv(struct vb2_buffer *vb,
				const void *start_addr, unsigned long size)
{
	return vb2_ion_cache_inv(vb, 1);
}

/* Allocate firmware */
int fimc_is_alloc_firmware(struct fimc_is_dev *dev)
{
	void *fimc_is_bitproc_buf;
	dbg("Allocating memory for FIMC-IS firmware.\n");
	fimc_is_bitproc_buf =
		vb2_ion_memops.alloc(dev->alloc_ctx_fw, FIMC_IS_A5_MEM_SIZE);
	if (IS_ERR(fimc_is_bitproc_buf)) {
		fimc_is_bitproc_buf = 0;
		printk(KERN_ERR "Allocating bitprocessor buffer failed\n");
		return -ENOMEM;
	}

	dev->mem.dvaddr = (size_t)vb2_ion_memops.cookie(fimc_is_bitproc_buf);
	if (dev->mem.dvaddr  & FIMC_IS_FW_BASE_MASK) {
		err("The base memory is not aligned to 64MB.\n");
		vb2_ion_memops.put(fimc_is_bitproc_buf);
		dev->mem.dvaddr = 0;
		fimc_is_bitproc_buf = 0;
		return -EIO;
	}
	dbg("Device vaddr = %08x , size = %08x\n",
				dev->mem.dvaddr, FIMC_IS_A5_MEM_SIZE);

	dev->mem.kvaddr = vb2_ion_memops.vaddr(fimc_is_bitproc_buf);
	if (!dev->mem.kvaddr) {
		err("Bitprocessor memory remap failed\n");
		vb2_ion_memops.put(fimc_is_bitproc_buf);
		dev->mem.dvaddr = 0;
		fimc_is_bitproc_buf = 0;
		return -EIO;
	}
	dbg("Virtual address for FW: %08lx\n",
			(long unsigned int)dev->mem.kvaddr);
	dbg("Physical address for FW: %08lx\n",
			(long unsigned int)virt_to_phys(dev->mem.kvaddr));
	dev->mem.bitproc_buf = fimc_is_bitproc_buf;
	dev->mem.vb2_buf.planes[0].mem_priv = fimc_is_bitproc_buf;

	is_vb = &dev->mem.vb2_buf;
	buf_start = dev->mem.kvaddr;
	return 0;
}

void fimc_is_mem_cache_clean(const void *start_addr, unsigned long size)
{
	struct vb2_ion_buf *buf;
	struct scatterlist *sg;
	int i;
	off_t offset;

	if (start_addr < buf_start) {
		err("Start address error\n");
		return;
	}
	size--;

	offset = start_addr - buf_start;

	buf = (struct vb2_ion_buf *)is_vb->planes[0].mem_priv;
	dma_sync_sg_for_device(buf->conf->dev, buf->sg, buf->nents,
							DMA_BIDIRECTIONAL);
}

void fimc_is_mem_cache_inv(const void *start_addr, unsigned long size)
{
	struct vb2_ion_buf *buf;
	struct scatterlist *sg;
	int i;
	off_t offset;

	if (start_addr < buf_start) {
		err("Start address error\n");
		return;
	}

	offset = start_addr - buf_start;

	buf = (struct vb2_ion_buf *)is_vb->planes[0].mem_priv;
	for_each_sg(buf->sg, sg, buf->nents, i) {
		phys_addr_t start, end;

		if (offset >= sg_dma_len(sg)) {
			offset -= sg_dma_len(sg);
			continue;
		}

		start = sg_phys(sg);
		end = start + sg_dma_len(sg);

		dmac_flush_range(phys_to_virt(start),
				 phys_to_virt(end));
		outer_flush_range(start, end);	/* L2 */
		if (size == 0)
			break;
	}
}

int fimc_is_init_mem_mgr(struct fimc_is_dev *dev)
{
	int ret;
	dev->alloc_ctx_fw = (struct vb2_alloc_ctx *)
			fimc_is_mem_init(&dev->pdev->dev);
	if (IS_ERR(dev->alloc_ctx_fw)) {
		err("Couldn't prepare allocator FW ctx.\n");
		return PTR_ERR(dev->alloc_ctx_fw);
	}
	ret = fimc_is_alloc_firmware(dev);
	if (ret) {
		err("Couldn't alloc for FIMC-IS firmware\n");
		return -EINVAL;
	}
	memset(dev->mem.kvaddr, 0, FIMC_IS_A5_MEM_SIZE);
	dev->is_p_region =
		(struct is_region *)(dev->mem.kvaddr +
			FIMC_IS_A5_MEM_SIZE - FIMC_IS_REGION_SIZE);
	if (fimc_is_cache_flush(&dev->mem.vb2_buf,
			(void *)dev->is_p_region, IS_PARAM_SIZE)) {
		err("fimc_is_cache_flush-Err\n");
		return -EINVAL;
	}
	return 0;
}
#endif
