// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rga2_mmu: " fmt

#include "rga_iommu.h"
#include "rga_dma_buf.h"
#include "rga_mm.h"
#include "rga_job.h"
#include "rga_common.h"
#include "rga_hw_config.h"

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
		rga_dma_sync_flush_range(job->src_buffer.page_table,
					 (job->src_buffer.page_table +
					  job->src_buffer.page_count),
					 job->scheduler);
		req->mmu_info.src0_base_addr = virt_to_phys(job->src_buffer.page_table);
	}

	if (job->src1_buffer.page_table) {
		rga_dma_sync_flush_range(job->src1_buffer.page_table,
					 (job->src1_buffer.page_table +
					  job->src1_buffer.page_count),
					 job->scheduler);
		req->mmu_info.src1_base_addr = virt_to_phys(job->src1_buffer.page_table);
	}

	if (job->dst_buffer.page_table) {
		rga_dma_sync_flush_range(job->dst_buffer.page_table,
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
		rga_dma_sync_flush_range(job->els_buffer.page_table,
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

int rga_iommu_detach(struct rga_iommu_info *info)
{
	if (!info)
		return 0;

	iommu_detach_group(info->domain, info->group);
	return 0;
}

int rga_iommu_attach(struct rga_iommu_info *info)
{
	if (!info)
		return 0;

	return iommu_attach_group(info->domain, info->group);
}

struct rga_iommu_info *rga_iommu_probe(struct device *dev)
{
	int ret = 0;
	struct rga_iommu_info *info = NULL;
	struct iommu_domain *domain = NULL;
	struct iommu_group *group = NULL;

	group = iommu_group_get(dev);
	if (!group)
		return ERR_PTR(-EINVAL);

	domain = iommu_get_domain_for_dev(dev);
	if (!domain) {
		ret = -EINVAL;
		goto err_put_group;
	}

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		ret = -ENOMEM;
		goto err_put_group;
	}

	info->dev = dev;
	info->default_dev = info->dev;
	info->group = group;
	info->domain = domain;

	return info;

err_put_group:
	if (group)
		iommu_group_put(group);

	return ERR_PTR(ret);
}

int rga_iommu_remove(struct rga_iommu_info *info)
{
	if (!info)
		return 0;

	iommu_group_put(info->group);

	return 0;
}

int rga_iommu_bind(void)
{
	int i;
	int ret;
	struct rga_scheduler_t *scheduler = NULL;
	struct rga_iommu_info *main_iommu = NULL;
	int main_iommu_index = -1;
	int main_mmu_index = -1;
	int another_index = -1;

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->scheduler[i];

		switch (scheduler->data->mmu) {
		case RGA_IOMMU:
			if (scheduler->iommu_info == NULL)
				continue;

			if (main_iommu == NULL) {
				main_iommu = scheduler->iommu_info;
				main_iommu_index = i;
			} else {
				scheduler->iommu_info->domain = main_iommu->domain;
				scheduler->iommu_info->default_dev = main_iommu->default_dev;
				rga_iommu_attach(scheduler->iommu_info);
			}

			break;

		case RGA_MMU:
			if (rga_drvdata->mmu_base != NULL)
				continue;

			rga_drvdata->mmu_base = rga_mmu_base_init(RGA2_PHY_PAGE_SIZE);
			if (IS_ERR(rga_drvdata->mmu_base)) {
				dev_err(scheduler->dev, "rga mmu base init failed!\n");
				ret = PTR_ERR(rga_drvdata->mmu_base);
				rga_drvdata->mmu_base = NULL;

				return ret;
			}

			main_mmu_index = i;

			break;
		default:
			if (another_index != RGA_NONE_CORE)
				another_index = i;

			break;
		}
	}

	/*
	 * priority order: iommu > mmu > another
	 *   The scheduler core with IOMMU will be used preferentially as the
	 * default memory-mapped core. This ensures that all cores can obtain
	 * the required memory data when they are equipped with different
	 * versions of cores.
	 */
	if (main_iommu_index >= 0) {
		rga_drvdata->map_scheduler_index = main_iommu_index;
	} else if (main_mmu_index >= 0) {
		rga_drvdata->map_scheduler_index = main_mmu_index;
	} else if (another_index >= 0) {
		rga_drvdata->map_scheduler_index = another_index;
	} else {
		rga_drvdata->map_scheduler_index = -1;
		pr_err("%s, binding map scheduler failed!\n", __func__);
		return -EFAULT;
	}

	pr_info("IOMMU binding successfully, default mapping core[0x%x]\n",
		rga_drvdata->scheduler[rga_drvdata->map_scheduler_index]->core);

	return 0;
}

void rga_iommu_unbind(void)
{
	int i;

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++)
		if (rga_drvdata->scheduler[i]->iommu_info != NULL)
			rga_iommu_detach(rga_drvdata->scheduler[i]->iommu_info);

	if (rga_drvdata->mmu_base)
		rga_mmu_base_free(&rga_drvdata->mmu_base);

	rga_drvdata->map_scheduler_index = -1;
}
