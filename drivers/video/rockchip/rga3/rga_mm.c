// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Cerf Yu <cerf.yu@rock-chips.com>
 */

#define pr_fmt(fmt) "rga_mm: " fmt

#include "rga.h"
#include "rga_mm.h"
#include "rga_dma_buf.h"

/* If it is within 0~4G, return 1 (true). */
static int rga_mm_check_range_sgt(struct sg_table *sgt)
{
	int i;
	struct scatterlist *sg;
	phys_addr_t s_phys = 0;

	for_each_sgtable_sg(sgt, sg, i) {
		s_phys = sg_phys(sg);
		if ((s_phys > 0xffffffff) || (s_phys + sg->length > 0xffffffff))
			return 0;
	}

	return 1;
}

static void rga_mm_unmap_dma_buffer(struct rga_internal_buffer *internal_buffer)
{
	int i;

	for (i = 0; i < internal_buffer->dma_buffer_size; i++)
		rga_dma_unmap_fd(&internal_buffer->dma_buffer[i]);

	kfree(internal_buffer->dma_buffer);
	internal_buffer->dma_buffer = NULL;
}

static int rga_mm_map_dma_buffer(struct rga_external_buffer *external_buffer,
	struct rga_internal_buffer *internal_buffer)
{
	int ret, i;

	internal_buffer->dma_buffer_size = rga_drvdata->num_of_scheduler;
	internal_buffer->dma_buffer = kcalloc(internal_buffer->dma_buffer_size,
					      sizeof(struct rga_dma_buffer), GFP_KERNEL);
	if (internal_buffer->dma_buffer == NULL) {
		pr_err("%s alloc internal_buffer error!\n", __func__);
		return  -ENOMEM;
	}

	for (i = 0; i < internal_buffer->dma_buffer_size; i++) {
		/* If the physical address is greater than 4G, there is no need to map RGA2. */
		if ((rga_drvdata->rga_scheduler[i]->core == RGA2_SCHEDULER_CORE0) &&
		    (~internal_buffer->mm_flag & RGA_MM_UNDER_4G))
			continue;

		ret = rga_dma_map_fd((int)external_buffer->memory,
				     &internal_buffer->dma_buffer[i],
				     DMA_BIDIRECTIONAL,
				     rga_drvdata->rga_scheduler[i]->dev);
		if (ret < 0) {
			pr_err("%s core[%d] map dma buffer error!\n",
				__func__, rga_drvdata->rga_scheduler[0]->core);
			goto FREE_RGA_DMA_BUF;
		}

		internal_buffer->dma_buffer[i].core = rga_drvdata->rga_scheduler[i]->core;

		/* At first, check whether the physical address is greater than 4G. */
		if (i == 0)
			if (rga_mm_check_range_sgt(internal_buffer->dma_buffer[0].sgt))
				internal_buffer->mm_flag |= RGA_MM_UNDER_4G;
	}

	return 0;

FREE_RGA_DMA_BUF:
	rga_mm_unmap_dma_buffer(internal_buffer);

	return ret;
}

static int rga_mm_unmap_buffer(struct rga_internal_buffer *internal_buffer)
{
	switch (internal_buffer->type) {
	case RGA_DMA_BUFFER:
		rga_mm_unmap_dma_buffer(internal_buffer);
		break;
	case RGA_VIRTUAL_ADDRESS:
		internal_buffer->vir_addr = 0;
		break;
	case RGA_PHYSICAL_ADDRESS:
		internal_buffer->phy_addr = 0;
		break;
	default:
		pr_err("Illegal external buffer!\n");
		return -EFAULT;
	}

	return 0;
}

static int rga_mm_map_buffer(struct rga_external_buffer *external_buffer,
	struct rga_internal_buffer *internal_buffer)
{
	int ret;

	switch (external_buffer->type) {
	case RGA_DMA_BUFFER:
		internal_buffer->type = RGA_DMA_BUFFER;

		ret = rga_mm_map_dma_buffer(external_buffer, internal_buffer);
		if (ret < 0) {
			pr_err("%s map dma_buf error!\n", __func__);
			return ret;
		}
		break;
	case RGA_VIRTUAL_ADDRESS:
		internal_buffer->type = RGA_VIRTUAL_ADDRESS;

		internal_buffer->vir_addr = external_buffer->memory;
		break;
	case RGA_PHYSICAL_ADDRESS:
		internal_buffer->type = RGA_PHYSICAL_ADDRESS;

		internal_buffer->phy_addr = external_buffer->memory;
		break;
	default:
		pr_err("Illegal external buffer!\n");
		return -EFAULT;
	}

	return 0;
}

static void rga_mm_kref_release_buffer(struct kref *ref)
{
	struct rga_internal_buffer *internal_buffer;

	internal_buffer = container_of(ref, struct rga_internal_buffer, refcount);
	rga_mm_unmap_buffer(internal_buffer);

	idr_remove(&rga_drvdata->mm->memory_idr, internal_buffer->handle);
	kfree(internal_buffer);
	rga_drvdata->mm->buffer_count--;
}

/*
 * Called at driver close to release the memory's handle references.
 */
static int rga_mm_handle_remove(int id, void *ptr, void *data)
{
	struct rga_internal_buffer *internal_buffer = ptr;

	rga_mm_kref_release_buffer(&internal_buffer->refcount);

	return 0;
}

static struct rga_internal_buffer *
rga_mm_internal_buffer_lookup_external(struct rga_mm *mm_session,
				       struct rga_external_buffer *external_buffer)
{
	int id;
	struct dma_buf *dma_buf = NULL;
	struct rga_internal_buffer *temp_buffer = NULL;
	struct rga_internal_buffer *output_buffer = NULL;

	WARN_ON(!mutex_is_locked(&mm_session->lock));

	switch (external_buffer->type) {
	case RGA_DMA_BUFFER:
		dma_buf = dma_buf_get((int)external_buffer->memory);
		if (IS_ERR(dma_buf))
			return (struct rga_internal_buffer *)dma_buf;

		idr_for_each_entry(&mm_session->memory_idr, temp_buffer, id) {
			if (temp_buffer->dma_buffer[0].dma_buf == dma_buf) {
				output_buffer = temp_buffer;
				break;
			}
		}

		dma_buf_put(dma_buf);
		break;
	case RGA_VIRTUAL_ADDRESS:
		idr_for_each_entry(&mm_session->memory_idr, temp_buffer, id) {
			if (temp_buffer->vir_addr == external_buffer->memory) {
				output_buffer = temp_buffer;
				break;
			}
		}

		break;
	case RGA_PHYSICAL_ADDRESS:
		idr_for_each_entry(&mm_session->memory_idr, temp_buffer, id) {
			if (temp_buffer->phy_addr == external_buffer->memory) {
				output_buffer = temp_buffer;
				break;
			}
		}

		break;
	default:
		pr_err("Illegal external buffer!\n");
		return NULL;
	}

	return output_buffer;
}

struct rga_internal_buffer *
rga_mm_internal_buffer_lookup_handle(struct rga_mm *mm_session, uint32_t handle)
{
	struct rga_internal_buffer *output_buffer;

	WARN_ON(!mutex_is_locked(&mm_session->lock));

	output_buffer = idr_find(&mm_session->memory_idr, handle);

	return output_buffer;
}

void rga_mm_dump_info(struct rga_mm *mm_session)
{
	int id;
	struct rga_internal_buffer *temp_buffer;

	WARN_ON(!mutex_is_locked(&mm_session->lock));

	pr_info("rga mm info:\n");
	pr_info("buffer count = %d\n", mm_session->buffer_count);

	idr_for_each_entry(&mm_session->memory_idr, temp_buffer, id) {
		pr_info("ID[%d] dma_buf = %p, handle = %d, refcount = %d, mm_flag = %x\n",
			id, temp_buffer->dma_buffer[0].dma_buf, temp_buffer->handle,
			kref_read(&temp_buffer->refcount), temp_buffer->mm_flag);
	}
}

uint32_t rga_mm_import_buffer(struct rga_external_buffer *external_buffer)
{
	int ret = 0;
	struct rga_mm *mm;
	struct rga_internal_buffer *internal_buffer;

	mm = rga_drvdata->mm;
	if (mm == NULL) {
		pr_err("rga mm is null!\n");
		return -EFAULT;
	}

	mutex_lock(&mm->lock);

	/* first, Check whether to rga_mm */
	internal_buffer = rga_mm_internal_buffer_lookup_external(mm, external_buffer);
	if (!IS_ERR_OR_NULL(internal_buffer)) {
		kref_get(&internal_buffer->refcount);

		mutex_unlock(&mm->lock);
		return internal_buffer->handle;
	}

	/* finally, map and cached external_buffer in rga_mm */
	internal_buffer = kzalloc(sizeof(struct rga_internal_buffer), GFP_KERNEL);
	if (internal_buffer == NULL) {
		pr_err("%s alloc internal_buffer error!\n", __func__);

		mutex_unlock(&mm->lock);
		return -ENOMEM;
	}

	ret = rga_mm_map_buffer(external_buffer, internal_buffer);
	if (ret < 0)
		goto FREE_INTERNAL_BUFFER;

	kref_init(&internal_buffer->refcount);

	/*
	 * Get the user-visible handle using idr. Preload and perform
	 * allocation under our spinlock.
	 */
	idr_preload(GFP_KERNEL);
	internal_buffer->handle = idr_alloc(&mm->memory_idr, internal_buffer, 1, 0, GFP_KERNEL);
	idr_preload_end();

	mm->buffer_count++;

	mutex_unlock(&mm->lock);
	return internal_buffer->handle;

FREE_INTERNAL_BUFFER:
	mutex_unlock(&mm->lock);
	kfree(internal_buffer);

	return ret;
}

int rga_mm_release_buffer(uint32_t handle)
{
	struct rga_mm *mm;
	struct rga_internal_buffer *internal_buffer;

	mm = rga_drvdata->mm;
	if (mm == NULL) {
		pr_err("rga mm is null!\n");
		return -EFAULT;
	}

	mutex_lock(&mm->lock);

	/* Find the buffer that has been imported */
	internal_buffer = rga_mm_internal_buffer_lookup_handle(mm, handle);
	if (IS_ERR_OR_NULL(internal_buffer)) {
		pr_err("This is not a buffer that has been imported, handle = %d\n", (int)handle);

		mutex_unlock(&mm->lock);
		return -ENOENT;
	}

	kref_put(&internal_buffer->refcount, rga_mm_kref_release_buffer);

	mutex_unlock(&mm->lock);
	return 0;
}

int rga_mm_init(struct rga_mm **mm_session)
{
	struct rga_mm *mm = NULL;

	*mm_session = kzalloc(sizeof(struct rga_mm), GFP_KERNEL);
	if (*mm_session == NULL) {
		pr_err("can not kzalloc for rga buffer mm_session\n");
		return -ENOMEM;
	}

	mm = *mm_session;

	mutex_init(&mm->lock);
	idr_init_base(&mm->memory_idr, 1);

	return 0;
}

int rga_mm_remove(struct rga_mm **mm_session)
{
	struct rga_mm *mm = *mm_session;

	mutex_lock(&mm->lock);

	idr_for_each(&mm->memory_idr, &rga_mm_handle_remove, mm);
	idr_destroy(&mm->memory_idr);

	mutex_unlock(&mm->lock);

	kfree(*mm_session);
	*mm_session = NULL;

	return 0;
}
