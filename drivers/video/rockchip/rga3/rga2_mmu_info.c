// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rga2_mmu: " fmt

#include "rga2_mmu_info.h"
#include "rga_dma_buf.h"
#include "rga_mm.h"
#include "rga_job.h"
#include "rga_common.h"

struct rga2_mmu_info_t rga2_mmu_info;

static void rga2_dma_sync_flush_range(void *pstart, void *pend, struct rga_scheduler_t *scheduler)
{
	dma_sync_single_for_device(scheduler->dev, virt_to_phys(pstart),
				   pend - pstart, DMA_TO_DEVICE);
}

int rga2_user_memory_check(struct page **pages, u32 w, u32 h, u32 format, int flag)
{
	int bits;
	void *vaddr = NULL;
	int taipage_num;
	int taidata_num;
	int *tai_vaddr = NULL;

	bits = rga_get_format_bits(format);
	if (bits < 0)
		return -1;

	taipage_num = w * h * bits / 8 / (1024 * 4);
	taidata_num = w * h * bits / 8 % (1024 * 4);
	if (taidata_num == 0) {
		vaddr = kmap(pages[taipage_num - 1]);
		tai_vaddr = (int *)vaddr + 1023;
	} else {
		vaddr = kmap(pages[taipage_num]);
		tai_vaddr = (int *)vaddr + taidata_num / 4 - 1;
	}

	if (flag == 1) {
		pr_info("src user memory check\n");
		pr_info("tai data is %d\n", *tai_vaddr);
	} else {
		pr_info("dst user memory check\n");
		pr_info("tai data is %d\n", *tai_vaddr);
	}

	if (taidata_num == 0)
		kunmap(pages[taipage_num - 1]);
	else
		kunmap(pages[taipage_num]);

	return 0;
}

int rga2_set_mmu_base(struct rga_job *job, struct rga2_req *req)
{
	if (job->src_buffer.page_table) {
		rga2_dma_sync_flush_range(job->src_buffer.page_table,
					  (job->src_buffer.page_table +
					   job->src_buffer.page_count),
					  job->scheduler);
		req->mmu_info.src0_base_addr = virt_to_phys(job->src_buffer.page_table);
	}

	if (job->src1_buffer.page_table) {
		rga2_dma_sync_flush_range(job->src1_buffer.page_table,
					  (job->src1_buffer.page_table +
					   job->src1_buffer.page_count),
					  job->scheduler);
		req->mmu_info.src1_base_addr = virt_to_phys(job->src1_buffer.page_table);
	}

	if (job->dst_buffer.page_table) {
		rga2_dma_sync_flush_range(job->dst_buffer.page_table,
					  (job->dst_buffer.page_table +
					   job->dst_buffer.page_count),
					  job->scheduler);
		req->mmu_info.dst_base_addr = virt_to_phys(job->dst_buffer.page_table);

		if (((req->alpha_rop_flag & 1) == 1) && (req->bitblt_mode == 0)) {
			req->mmu_info.src1_base_addr = req->mmu_info.dst_base_addr;
			req->mmu_info.src1_mmu_flag = req->mmu_info.dst_mmu_flag;
		}
	}

	if (job->els_buffer.page_table) {
		rga2_dma_sync_flush_range(job->els_buffer.page_table,
					  (job->els_buffer.page_table +
					   job->els_buffer.page_count),
					  job->scheduler);
		req->mmu_info.els_base_addr = virt_to_phys(job->els_buffer.page_table);
	}

	return 0;
}

static int rga2_mmu_buf_get_try(struct rga2_mmu_info_t *t, uint32_t size)
{
	int ret = 0;

	if ((t->back - t->front) > t->size) {
		if (t->front + size > t->back - t->size) {
			pr_info("front %d, back %d dsize %d size %d",
				t->front, t->back, t->size, size);
			ret = -ENOMEM;
			goto out;
		}
	} else {
		if ((t->front + size) > t->back) {
			pr_info("front %d, back %d dsize %d size %d",
				t->front, t->back, t->size, size);
			ret = -ENOMEM;
			goto out;
		}

		if (t->front + size > t->size) {
			if (size > (t->back - t->size)) {
				pr_info("front %d, back %d dsize %d size %d",
					t->front, t->back, t->size, size);
				ret = -ENOMEM;
				goto out;
			}
			t->front = 0;
		}
	}
out:
	return ret;
}

unsigned int *rga2_mmu_buf_get(uint32_t size)
{
	int ret;
	unsigned int *mmu_base = NULL;

	size = ALIGN(size, 16);

	mutex_lock(&rga_drvdata->lock);

	ret = rga2_mmu_buf_get_try(&rga2_mmu_info, size);
	if (ret < 0) {
		pr_err("Get MMU mem failed\n");
		mutex_unlock(&rga_drvdata->lock);
		return NULL;
	}

	mmu_base = rga2_mmu_info.buf_virtual + rga2_mmu_info.front;

	rga2_mmu_info.front += size;

	if (rga2_mmu_info.back + size > 2 * rga2_mmu_info.size)
		rga2_mmu_info.back = size + rga2_mmu_info.size;
	else
		rga2_mmu_info.back += size;

	mutex_unlock(&rga_drvdata->lock);

	return mmu_base;
}

int rga2_mmu_base_init(void)
{
	int order = 0;
	uint32_t *buf_p;
	uint32_t *buf;

	/*
	 * malloc pre scale mid buf mmu table:
	 * RGA2_PHY_PAGE_SIZE * channel_num * address_size
	 */
	order = get_order(RGA2_PHY_PAGE_SIZE * 3 * sizeof(buf_p));
	buf_p = (uint32_t *) __get_free_pages(GFP_KERNEL | GFP_DMA32, order);
	if (buf_p == NULL) {
		pr_err("Can not alloc pages for mmu_page_table\n");
		return -ENOMEM;
	}

	rga2_mmu_info.buf_order = order;

	order = get_order(RGA2_PHY_PAGE_SIZE * sizeof(struct page *));
	rga2_mmu_info.pages =
		(struct page **)__get_free_pages(GFP_KERNEL | GFP_DMA32, order);
	if (rga2_mmu_info.pages == NULL) {
		pr_err("Can not alloc pages for rga2_mmu_info.pages\n");
		goto err_free_buf_virtual;
	}

	rga2_mmu_info.pages_order = order;

#if (defined(CONFIG_ARM) && defined(CONFIG_ARM_LPAE))
	buf =
		(uint32_t *) (uint32_t)
		virt_to_phys((void *)((unsigned long)buf_p));
#else
	buf = (uint32_t *) virt_to_phys((void *)((unsigned long)buf_p));
#endif
	rga2_mmu_info.buf_virtual = buf_p;
	rga2_mmu_info.buf = buf;
	rga2_mmu_info.front = 0;
	rga2_mmu_info.back = RGA2_PHY_PAGE_SIZE * 3;
	rga2_mmu_info.size = RGA2_PHY_PAGE_SIZE * 3;

	return 0;

err_free_buf_virtual:
	free_pages((unsigned long)buf_p, rga2_mmu_info.buf_order);
	rga2_mmu_info.buf_order = 0;

	return -ENOMEM;
}

void rga2_mmu_base_free(void)
{
	if (rga2_mmu_info.buf_virtual != NULL) {
		free_pages((unsigned long)rga2_mmu_info.buf_virtual, rga2_mmu_info.buf_order);
		rga2_mmu_info.buf_virtual = NULL;
		rga2_mmu_info.buf_order = 0;
	}

	if (rga2_mmu_info.pages != NULL) {
		free_pages((unsigned long)rga2_mmu_info.pages, rga2_mmu_info.pages_order);
		rga2_mmu_info.pages = NULL;
		rga2_mmu_info.pages_order = 0;
	}
}
