// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Wave5 series multi-standard codec IP - low level access functions
 *
 * Copyright (C) 2021-2023 CHIPS&MEDIA INC
 */

#include <linux/bug.h>
#include "wave5-vdi.h"
#include "wave5-vpu.h"
#include "wave5-regdefine.h"
#include <linux/delay.h>

static int wave5_vdi_allocate_common_memory(struct device *dev)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);

	if (!vpu_dev->common_mem.vaddr) {
		int ret;

		vpu_dev->common_mem.size = SIZE_COMMON;
		ret = wave5_vdi_allocate_dma_memory(vpu_dev, &vpu_dev->common_mem);
		if (ret) {
			dev_err(dev, "unable to allocate common buffer\n");
			return ret;
		}
	}

	dev_dbg(dev, "[VDI] common_mem: daddr=%pad size=%zu vaddr=0x%p\n",
		&vpu_dev->common_mem.daddr, vpu_dev->common_mem.size, vpu_dev->common_mem.vaddr);

	return 0;
}

int wave5_vdi_init(struct device *dev)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	int ret;

	ret = wave5_vdi_allocate_common_memory(dev);
	if (ret < 0) {
		dev_err(dev, "[VDI] failed to get vpu common buffer from driver\n");
		return ret;
	}

	if (!PRODUCT_CODE_W_SERIES(vpu_dev->product_code)) {
		WARN_ONCE(1, "unsupported product code: 0x%x\n", vpu_dev->product_code);
		return -EOPNOTSUPP;
	}

	/* if BIT processor is not running. */
	if (wave5_vdi_read_register(vpu_dev, W5_VCPU_CUR_PC) == 0) {
		int i;

		for (i = 0; i < 64; i++)
			wave5_vdi_write_register(vpu_dev, (i * 4) + 0x100, 0x0);
	}

	dev_dbg(dev, "[VDI] driver initialized successfully\n");

	return 0;
}

int wave5_vdi_release(struct device *dev)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);

	vpu_dev->vdb_register = NULL;
	wave5_vdi_free_dma_memory(vpu_dev, &vpu_dev->common_mem);

	return 0;
}

void wave5_vdi_write_register(struct vpu_device *vpu_dev, u32 addr, u32 data)
{
	writel(data, vpu_dev->vdb_register + addr);
}

unsigned int wave5_vdi_read_register(struct vpu_device *vpu_dev, u32 addr)
{
	return readl(vpu_dev->vdb_register + addr);
}

int wave5_vdi_clear_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb)
{
	if (!vb || !vb->vaddr) {
		dev_err(vpu_dev->dev, "%s: unable to clear unmapped buffer\n", __func__);
		return -EINVAL;
	}

	memset(vb->vaddr, 0, vb->size);
	return vb->size;
}

int wave5_vdi_write_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb, size_t offset,
			   u8 *data, size_t len)
{
	if (!vb || !vb->vaddr) {
		dev_err(vpu_dev->dev, "%s: unable to write to unmapped buffer\n", __func__);
		return -EINVAL;
	}

	if (offset > vb->size || len > vb->size || offset + len > vb->size) {
		dev_err(vpu_dev->dev, "%s: buffer too small\n", __func__);
		return -ENOSPC;
	}

	memcpy(vb->vaddr + offset, data, len);

	return len;
}

int wave5_vdi_allocate_dma_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb)
{
	void *vaddr;
	dma_addr_t daddr;

	if (!vb->size) {
		dev_err(vpu_dev->dev, "%s: requested size==0\n", __func__);
		return -EINVAL;
	}

	vaddr = dma_alloc_coherent(vpu_dev->dev, vb->size, &daddr, GFP_KERNEL);
	if (!vaddr)
		return -ENOMEM;
	vb->vaddr = vaddr;
	vb->daddr = daddr;

	return 0;
}

int wave5_vdi_free_dma_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb)
{
	if (vb->size == 0)
		return -EINVAL;

	if (!vb->vaddr)
		dev_err(vpu_dev->dev, "%s: requested free of unmapped buffer\n", __func__);
	else
		dma_free_coherent(vpu_dev->dev, vb->size, vb->vaddr, vb->daddr);

	memset(vb, 0, sizeof(*vb));

	return 0;
}

int wave5_vdi_allocate_array(struct vpu_device *vpu_dev, struct vpu_buf *array, unsigned int count,
			     size_t size)
{
	struct vpu_buf vb_buf;
	int i, ret = 0;

	vb_buf.size = size;

	for (i = 0; i < count; i++) {
		if (array[i].size == size)
			continue;

		if (array[i].size != 0)
			wave5_vdi_free_dma_memory(vpu_dev, &array[i]);

		ret = wave5_vdi_allocate_dma_memory(vpu_dev, &vb_buf);
		if (ret)
			return -ENOMEM;
		array[i] = vb_buf;
	}

	for (i = count; i < MAX_REG_FRAME; i++)
		wave5_vdi_free_dma_memory(vpu_dev, &array[i]);

	return 0;
}

void wave5_vdi_allocate_sram(struct vpu_device *vpu_dev)
{
	struct vpu_buf *vb = &vpu_dev->sram_buf;

	if (!vpu_dev->sram_pool || !vpu_dev->sram_size)
		return;

	if (!vb->vaddr) {
		vb->size = vpu_dev->sram_size;
		vb->vaddr = gen_pool_dma_alloc(vpu_dev->sram_pool, vb->size,
					       &vb->daddr);
		if (!vb->vaddr)
			vb->size = 0;
	}

	dev_dbg(vpu_dev->dev, "%s: sram daddr: %pad, size: %zu, vaddr: 0x%p\n",
		__func__, &vb->daddr, vb->size, vb->vaddr);
}

void wave5_vdi_free_sram(struct vpu_device *vpu_dev)
{
	struct vpu_buf *vb = &vpu_dev->sram_buf;

	if (!vb->size || !vb->vaddr)
		return;

	if (vb->vaddr)
		gen_pool_free(vpu_dev->sram_pool, (unsigned long)vb->vaddr,
			      vb->size);

	memset(vb, 0, sizeof(*vb));
}
