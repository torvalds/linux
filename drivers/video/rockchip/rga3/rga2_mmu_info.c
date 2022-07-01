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

static void rga2_dma_sync_flush_range(void *pstart, void *pend, struct rga_scheduler_t *scheduler)
{
	dma_sync_single_for_device(scheduler->dev, virt_to_phys(pstart),
				   pend - pstart, DMA_TO_DEVICE);
}

int rga_user_memory_check(struct page **pages, u32 w, u32 h, u32 format, int flag)
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

int rga_set_mmu_base(struct rga_job *job, struct rga2_req *req)
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

static int rga_mmu_buf_get_try(struct rga_mmu_base *t, uint32_t size)
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

unsigned int *rga_mmu_buf_get(struct rga_mmu_base *mmu_base, uint32_t size)
{
	int ret;
	unsigned int *buf = NULL;

	WARN_ON(!mutex_is_locked(&rga_drvdata->lock));

	size = ALIGN(size, 16);

	ret = rga_mmu_buf_get_try(mmu_base, size);
	if (ret < 0) {
		pr_err("Get MMU mem failed\n");
		return NULL;
	}

	buf = mmu_base->buf_virtual + mmu_base->front;

	mmu_base->front += size;

	if (mmu_base->back + size > 2 * mmu_base->size)
		mmu_base->back = size + mmu_base->size;
	else
		mmu_base->back += size;

	return buf;
}

struct rga_mmu_base *rga_mmu_base_init(size_t size)
{
	int order = 0;
	struct rga_mmu_base *mmu_base;

	mmu_base = kzalloc(sizeof(*mmu_base), GFP_KERNEL);
	if (mmu_base == NULL) {
		pr_err("Cannot alloc mmu_base!\n");
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * malloc pre scale mid buf mmu table:
	 * size * channel_num * address_size
	 */
	order = get_order(size * 3 * sizeof(*mmu_base->buf_virtual));
	mmu_base->buf_virtual = (uint32_t *) __get_free_pages(GFP_KERNEL | GFP_DMA32, order);
	if (mmu_base->buf_virtual == NULL) {
		pr_err("Can not alloc pages for mmu_page_table\n");
		goto err_free_mmu_base;
	}
	mmu_base->buf_order = order;

	order = get_order(size * sizeof(*mmu_base->pages));
	mmu_base->pages = (struct page **)__get_free_pages(GFP_KERNEL | GFP_DMA32, order);
	if (mmu_base->pages == NULL) {
		pr_err("Can not alloc pages for mmu_base->pages\n");
		goto err_free_buf_virtual;
	}
	mmu_base->pages_order = order;

	mmu_base->front = 0;
	mmu_base->back = RGA2_PHY_PAGE_SIZE * 3;
	mmu_base->size = RGA2_PHY_PAGE_SIZE * 3;

	return mmu_base;

err_free_buf_virtual:
	free_pages((unsigned long)mmu_base->buf_virtual, mmu_base->buf_order);
	mmu_base->buf_order = 0;

err_free_mmu_base:
	kfree(mmu_base);

	return ERR_PTR(-ENOMEM);
}

void rga_mmu_base_free(struct rga_mmu_base **mmu_base)
{
	struct rga_mmu_base *base = *mmu_base;

	if (base->buf_virtual != NULL) {
		free_pages((unsigned long)base->buf_virtual, base->buf_order);
		base->buf_virtual = NULL;
		base->buf_order = 0;
	}

	if (base->pages != NULL) {
		free_pages((unsigned long)base->pages, base->pages_order);
		base->pages = NULL;
		base->pages_order = 0;
	}

	kfree(base);
	*mmu_base = NULL;
}

