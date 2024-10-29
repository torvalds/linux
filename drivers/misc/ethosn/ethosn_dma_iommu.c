/*
 *
 * (C) COPYRIGHT 2018-2023 Arm Limited.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include "ethosn_dma_iommu.h"

#include "ethosn_backport.h"
#include "ethosn_device.h"

#include <linux/dma-buf.h>
#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

/*
 * The iommu_tlb_sync function was renamed to iommu_iotbl_sync in 5.10 so a
 * macro is defined for the older versions to be able to use the same function
 * name.
 */
#if (KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE)
#define iommu_iotlb_sync iommu_tlb_sync
#endif

enum ethosn_memory_source {
	ETHOSN_MEMORY_ALLOC = 0,
	ETHOSN_MEMORY_IMPORT = 1,
	ETHOSN_MEMORY_PROTECTED = 2,
};

struct ethosn_iommu_stream {
	enum ethosn_stream_type type;       // 流类型, allocator 和 type 是一一对应的, 每一个 streamid 对应一个 group
	dma_addr_t addr_base;               // 对应的 DMA 地址的基地址, 即 CPU 可访问的物理地址经过 IOMMU 模块映射之后可以直接访问的虚拟地址
    void *bitmap;                       // 位图的每一个位表示将当前长度为 IOMMU_ADDR_SIZE 的虚拟地址空间完整映射到物理地址空间所需的每一个内存页, 相当于内存页的掩码
	size_t bits;                        // 而 bits 表示满足字节对齐要求的, 能够容纳此内存页掩码所需的总位数
	struct page *page;                  // 为当前 allocator 分配的物理内存页
	bool allocated_page;                // 当前流所属的页是否是通过 API 分配的
	spinlock_t lock;
};

struct ethosn_iommu_domain {
	struct iommu_domain *iommu_domain;  // iommu 域绑定在 ethos 顶层设备上, 所有 allocator 共享
	struct ethosn_iommu_stream stream;
};

struct ethosn_allocator_internal {
	struct ethosn_dma_sub_allocator allocator;
	/* Allocator private members */
	struct ethosn_iommu_domain ethosn_iommu_domain;
};

struct dma_buf_internal {
	struct dma_buf *dmabuf;
	int fd;
	/* Scatter-gather table of the imported buffer. */
	struct sg_table *sgt;
	/* dma-buf attachment of the imported buffer. */
	struct dma_buf_attachment *attachment;
};

struct ethosn_dma_info_internal {
	struct ethosn_dma_info info;
	/* Allocator private members */
	enum ethosn_memory_source source;
	dma_addr_t *dma_addr;
	struct page **pages;
	struct dma_buf_internal *dma_buf_internal;
	struct scatterlist **scatterlist;
	bool iova_mapped;
};

static phys_addr_t ethosn_page_to_phys(int index, struct ethosn_dma_info_internal *dma_info)
{
	switch (dma_info->source) {
	case ETHOSN_MEMORY_ALLOC:
	/* Fallthrough */
	case ETHOSN_MEMORY_PROTECTED:

		return page_to_phys(dma_info->pages[index]);
	case ETHOSN_MEMORY_IMPORT:

		return sg_phys(dma_info->scatterlist[index]);
	default:
		WARN_ON(1);

		return 0U;
	}
}

static size_t ethosn_page_size(unsigned int start, unsigned int end, struct ethosn_dma_info_internal *dma_info)
{
	size_t size = 0;
	unsigned int i;

	if (dma_info->source == ETHOSN_MEMORY_IMPORT)
		for (i = start; i < end; i++)
			size += sg_dma_len(dma_info->scatterlist[i]);

	else
		size = (end - start) * PAGE_SIZE;

	return size;
}

static int ethosn_nr_sg_objects_user_mmap(struct ethosn_dma_info_internal *dma_info, size_t vm_size)
{
	int nr_pages = 0;

	if (dma_info->source == ETHOSN_MEMORY_IMPORT)
		nr_pages = dma_info->dma_buf_internal->sgt->nents;
	else
		nr_pages = DIV_ROUND_UP(vm_size, PAGE_SIZE);

	return nr_pages;
}

static int ethosn_nr_sg_objects(struct ethosn_dma_info_internal *dma_info)
{
	int nr_pages = 0;

	if (dma_info->source == ETHOSN_MEMORY_IMPORT)
		nr_pages = dma_info->dma_buf_internal->sgt->nents;
	else
		nr_pages = DIV_ROUND_UP(dma_info->info.size, PAGE_SIZE);

	return nr_pages;
}

static int ethosn_nr_pages(struct ethosn_dma_info_internal *dma_info)
{
	/* info.size is calculated on import and can be used for scatterlists
	 * also.
	 */
	return DIV_ROUND_UP(dma_info->info.size, PAGE_SIZE);
}

static dma_addr_t iommu_get_addr_base(struct ethosn_dma_sub_allocator *allocator, enum ethosn_stream_type stream_type)
{
	struct ethosn_allocator_internal *allocator_private = container_of(allocator, typeof(*allocator_private), allocator);
	struct ethosn_iommu_stream *stream = &allocator_private->ethosn_iommu_domain.stream;

	if (WARN_ON(stream->type != stream_type))
		return 0U;

	return stream->addr_base;
}

static resource_size_t iommu_get_addr_size(struct ethosn_dma_sub_allocator *allocator, enum ethosn_stream_type stream_type)
{
	struct ethosn_allocator_internal *allocator_private = container_of(allocator, typeof(*allocator_private), allocator);
	struct ethosn_iommu_stream *stream = &allocator_private->ethosn_iommu_domain.stream;

	if (WARN_ON(stream->type != stream_type))
		return 0U;

	return IOMMU_ADDR_SIZE;
}

/**
 * iommu_alloc_iova - 为分配的物理页在 IOVA 地址空间中分配一个连续的虚拟地址范围
 * @dev: 设备结构体指针, 用于锁定设备上下文
 * @dma: 指向 ethosn_dma_info_internal 的指针, 包含 DMA 内存的信息
 * @stream: 指向 ethosn_iommu_stream 的指针, 表示 IOMMU 的流
 *
 * 此函数为指定的 DMA 内存分配一个 IOVA 地址范围, 确保设备可以通过 IOMMU 访问到该内存.
 * 首先计算需要分配的页数, 并检查流是否符合分配条件. 函数使用位图找到符合条件的连续
 * 空闲页, 并为这些页设置 IOVA 地址. 在成功分配 IOVA 地址后, 会更新流的位图以标记此区域已使用.
 *
 * 重要:
 * - 分配的 IOVA 地址范围在 stream 的地址基址之上连续分布.
 * - 如果 `extend_bitmap` 标志位设为 true, 则会扩展位图, 允许更大范围的地址分配.
 * - 函数在分配 IOVA 地址之前使用自旋锁, 确保多线程环境中的一致性.
 *
 * 返回值:
 * 成功时返回已分配的 IOVA 地址;
 * 失败时返回 0.
 */

static dma_addr_t iommu_alloc_iova(struct device *dev, struct ethosn_dma_info_internal *dma, struct ethosn_iommu_stream *stream)
{
	unsigned long start = 0;
	unsigned long flags;
	int ret;
	int nr_pages = ethosn_nr_pages(dma);
	dma_addr_t iova = 0;
	bool extend_bitmap = (dma->info.stream_type > ETHOSN_STREAM_COMMAND_STREAM);

	spin_lock_irqsave(&stream->lock, flags);

    // 这里去当前 allocator 绑定的 stream 中, 寻找该 stream 可用的下一段长度为 nr_pages*PAGE_SIZE 连续为零的 iova 地址段, 返回其起始地址
    // 若 extend_bitmap 为真, 且未找到合适的 iova 地址段, 则位图可能会拓展
	ret = ethosn_bitmap_find_next_zero_area(dev, &stream->bitmap, &stream->bits, nr_pages, &start, extend_bitmap);
	if (ret)
		goto ret;

	bitmap_set(stream->bitmap, start, nr_pages);    // 现在开始需要占用这段空间

	iova = stream->addr_base + PAGE_SIZE * start;

ret:
	spin_unlock_irqrestore(&stream->lock, flags);

	return iova;
}

static void iommu_free_iova(dma_addr_t start, struct ethosn_iommu_stream *stream, int nr_pages)
{
	unsigned long flags;

	if (!stream)
		return;

	spin_lock_irqsave(&stream->lock, flags);

	bitmap_clear(stream->bitmap, (start - stream->addr_base) / PAGE_SIZE, nr_pages);

	spin_unlock_irqrestore(&stream->lock, flags);
}

static void iommu_free_pages(struct ethosn_dma_sub_allocator *allocator, dma_addr_t dma_addr[], struct page *pages[], int nr_pages)
{
	int i;

	for (i = 0; i < nr_pages; ++i)
		if (dma_addr[i] && pages[i])
			dma_free_pages(allocator->dev, PAGE_SIZE, pages[i], dma_addr[i], DMA_BIDIRECTIONAL);
}

/**
 * iommu_alloc - 分配 DMA 内存页并进行内存映射
 * @allocator: 指向 ethosn_dma_sub_allocator 的指针, 用于绑定 DMA 内存分配操作
 * @size: 要分配的内存大小 (字节数)
 * @gfp: 用于内存分配的 GFP 标志
 *
 * 此函数通过分配多个物理内存页并将其映射为 DMA 地址, 为特定的流类型创建一个内存区域.
 * 函数首先计算所需页数, 并尝试为每一页分配物理内存和 DMA 地址映射. 在分配失败或映射
 * 失败的情况下, 函数会释放之前分配的内存, 并返回错误码. 若分配和映射成功, 函数将返回
 * ethosn_dma_info 结构指针, 该结构包含分配内存的信息, 包括分配大小, CPU 可访问地址
 * 和初始 DMA 地址等.
 *
 * 注意:
 * 返回的 ethosn_dma_info 结构中尚未进行 IOVA 地址的映射, 即 `iova_addr` 字段未初始化.
 * 需要调用其他函数完成 IOVA 地址的映射.
 *
 * 返回值:
 * 成功时, 返回分配并映射的 ethosn_dma_info 结构的指针;
 * 若内存分配或映射失败, 则返回错误指针, 通常为 ERR_PTR(-ENOMEM).
 */
static struct ethosn_dma_info *iommu_alloc(struct ethosn_dma_sub_allocator *allocator, const size_t size, gfp_t gfp)
{
	struct page **pages = NULL;
	struct ethosn_dma_info_internal *dma_info;
	void *cpu_addr = NULL;
	dma_addr_t *dma_addr = NULL;
	int nr_pages = DIV_ROUND_UP(size, PAGE_SIZE);   // 计算所需内存大小对应的页数
	int i;

	dma_info = devm_kzalloc(allocator->dev, sizeof(struct ethosn_dma_info_internal), GFP_KERNEL);
	if (!dma_info)
		goto early_exit;

	if (!size)
		goto ret;

	pages = (struct page **)devm_kzalloc(allocator->dev, sizeof(struct page *) * nr_pages, GFP_KERNEL);
	if (!pages)
		goto free_dma_info;

	dma_addr = (dma_addr_t *)devm_kzalloc(allocator->dev, sizeof(dma_addr_t) * nr_pages, GFP_KERNEL);
	if (!dma_addr)
		goto free_pages_list;

    // 为每一个页对象分配物理内存并映射 DMA 地址
	for (i = 0; i < nr_pages; ++i) {
		pages[i] = dma_alloc_pages(allocator->dev, PAGE_SIZE, &dma_addr[i], DMA_BIDIRECTIONAL, gfp);

		if (!pages[i])
			goto free_pages_and_dma_addr;

		if (dma_mapping_error(allocator->dev, dma_addr[i])) {
			dev_err(allocator->dev, "failed to dma map pa 0x%llX\n", page_to_phys(pages[i]));
			__free_page(pages[i]);
			goto free_pages_and_dma_addr;
		}
	}

	cpu_addr = vmap(pages, nr_pages, 0, PAGE_KERNEL);
	if (!cpu_addr)
		goto free_pages_and_dma_addr;

	dev_dbg(allocator->dev, "Allocated DMA. handle=%pK allocator->dev = %pK", dma_info, allocator->dev);

ret:
    // 这里分配的内存并未通过 iommu 映射, 因此 iova_addr 并未初始化
    // 这里将 cpu_addr 返回给到调用者, 是因为分配内存之后 CPU 需要立即访问并操作这些内存. 通常是初始化, 填充或者检查内容等;
	*dma_info =
		(struct ethosn_dma_info_internal){ .info = (struct ethosn_dma_info){ .size = size, .cpu_addr = cpu_addr, .iova_addr = 0, .imported = false },
						   .source = ETHOSN_MEMORY_ALLOC,
						   .dma_addr = dma_addr,
						   .pages = pages,
						   .iova_mapped = false };

	return &dma_info->info;

free_pages_and_dma_addr:
	iommu_free_pages(allocator, dma_addr, pages, i);
	devm_kfree(allocator->dev, dma_addr);
free_pages_list:
	devm_kfree(allocator->dev, pages);
free_dma_info:
	devm_kfree(allocator->dev, dma_info);
early_exit:

	return ERR_PTR(-ENOMEM);
}

static void iommu_unmap_iova_pages(struct ethosn_dma_info_internal *dma_info, struct iommu_domain *domain, struct ethosn_iommu_stream *stream, int nr_pages)
{
	int i;

	if (!dma_info->iova_mapped)
		return;

	for (i = 0; i < nr_pages; ++i) {
		unsigned long iova_addr = dma_info->info.iova_addr + ethosn_page_size(0, i, dma_info);

		if (dma_info->pages[i]) {
			/* TODO: Should handle error here */
			iommu_unmap(domain, iova_addr, ethosn_page_size(i, i + 1, dma_info));

			if (stream->page)
				iommu_map(domain, iova_addr, page_to_phys(stream->page), PAGE_SIZE, IOMMU_READ);
		}
	}

	dma_info->iova_mapped = false;
}

/**
 * iommu_iova_map - 将分配的物理页映射到 IOVA 地址空间
 * @allocator: 指向 ethosn_dma_sub_allocator 的指针, 用于绑定 DMA 内存映射操作
 * @dma_info: 指向 ethosn_dma_info 的指针, 包含需要映射的内存信息
 * @prot_ranges: 指向 ethosn_dma_prot_range 数组, 用于设置不同内存范围的访问权限
 * @num_prot_ranges: prot_ranges 数组的长度, 表示不同保护范围的数量
 *
 * 此函数将分配的物理内存页映射到 IOMMU IOVA 地址空间, 以便设备可以通过 IOMMU 访问该内存.
 * 根据传入的 prot_ranges 数组设置不同的访问权限 (读 / 写) 范围. 在函数执行过程中, 将
 * `dma_info->info.size` 以页为单位分块, 并为每一块分配 IOVA 地址, 并映射到对应的物理页.
 *
 * 在映射过程中, 函数将检查不同的保护范围, 确保每一个 scatter-gather 条目在同一范围内使用
 * 一致的权限. 如果出现不同权限重叠的情况或映射失败, 将取消当前映射并返回错误.
 *
 * 注意:
 * - 函数要求传入的 `prot_ranges` 数组按照地址从低到高排序, 以确保每个范围内的映射权限
 *   保持一致.
 * - 如果当前的 dma_info 结构的 `iova_addr` 已映射到 IOVA, 则不会执行重复映射.
 *
 * 返回值:
 * 成功时返回 0, 表示映射完成;
 * 失败时返回负数错误码, 并取消已完成的部分映射.
 */
static int iommu_iova_map(struct ethosn_dma_sub_allocator *allocator, struct ethosn_dma_info *_dma_info, struct ethosn_dma_prot_range *prot_ranges,
			  size_t num_prot_ranges)
{
	struct ethosn_allocator_internal *allocator_private = container_of(allocator, typeof(*allocator_private), allocator);
	struct ethosn_iommu_domain *domain = &allocator_private->ethosn_iommu_domain;
	struct ethosn_iommu_stream *stream = &domain->stream;
	struct ethosn_dma_info_internal *dma_info = container_of(_dma_info, typeof(*dma_info), info);
	int nr_scatter_entries = ethosn_nr_sg_objects(dma_info);
	dma_addr_t start_addr = 0;
	int i, err, prot;
	int iommu_prot = 0;
	const struct ethosn_dma_prot_range *current_prot_range = &prot_ranges[0];

	if (!dma_info->info.size)
		goto ret;

	if (WARN_ON(stream->type != _dma_info->stream_type))
		goto early_exit;

	if (!dma_info->pages)
		goto early_exit;

	start_addr = iommu_alloc_iova(allocator_private->allocator.dev, dma_info, stream);
	if (!start_addr)
		goto early_exit;

	if ((dma_info->info.iova_addr) && (dma_info->info.iova_addr != start_addr)) {
		dev_err(allocator->dev, "Invalid iova: 0x%llX != 0x%llX\n", dma_info->info.iova_addr, start_addr);
		goto free_iova;
	}

	dma_info->info.iova_addr = start_addr;

	dev_dbg(allocator->dev, "%s: mapping %lu bytes starting at 0x%llX prot 0x%x (+%zu others)\n", __func__, dma_info->info.size, start_addr,
		prot_ranges[0].prot, num_prot_ranges - 1);

	for (i = 0; i < nr_scatter_entries; ++i) {
		const size_t offset = ethosn_page_size(0, i, dma_info);
		const dma_addr_t addr = start_addr + offset;
		const size_t sg_entry_size = ethosn_page_size(i, i + 1, dma_info);

		/* Determine the prot for this sg entry, based on the
		 * prot_ranges
		 */
		if (offset >= current_prot_range->end) {
			++current_prot_range;
			if (current_prot_range >= &prot_ranges[num_prot_ranges]) {
				/* This should have been caught by the
				 * validation in
				 * ethosn_dma_map_with_prot_ranges.
				 */
				dev_err(allocator->dev, "Invalid prot range.\n");
				goto unmap_pages;
			}

			dev_dbg(allocator->dev, "Prot is now %d starting from offset 0x%zx.\n", current_prot_range->prot, offset);
		}

		/* Confirm that the prot doesn't need to change halfway through
		 * this sg entry, which is not supported.
		 */
		if (current_prot_range->end < dma_info->info.size && current_prot_range->end < offset + sg_entry_size) {
			dev_err(allocator->dev, "Can't have different prots within the same SG entry.\n");

			goto unmap_pages;
		}

		prot = current_prot_range->prot;
		iommu_prot = 0;
		if ((prot & ETHOSN_PROT_READ) == ETHOSN_PROT_READ)
			iommu_prot |= IOMMU_READ;

		if ((prot & ETHOSN_PROT_WRITE) == ETHOSN_PROT_WRITE)
			iommu_prot |= IOMMU_WRITE;

		/* Unmap existing mapping from the dummy page. */
		if (stream->page)
			iommu_unmap(domain->iommu_domain, addr, PAGE_SIZE);

		/* Print some debug logs but only for the first few entries,
		 * to avoid too much spam.
		 */
		if (i < 4)
			dev_dbg(allocator->dev, "%s: mapping scatter-gather entry %d/%d iova 0x%llX, pa 0x%llX, size %lu\n", __func__, i, nr_scatter_entries,
				addr, ethosn_page_to_phys(i, dma_info), sg_entry_size);

		err = iommu_map(domain->iommu_domain, addr, ethosn_page_to_phys(i, dma_info), sg_entry_size, iommu_prot);

		if (err) {
			dev_err(allocator->dev, "failed to iommu map iova 0x%llX pa 0x%llX size %lu\n", addr, ethosn_page_to_phys(i, dma_info), sg_entry_size);

			if (i > 0)
				dma_info->iova_mapped = true;

			goto unmap_pages;
		}
	}

	dma_info->iova_mapped = true;

ret:

	return 0;

unmap_pages:
	/* remap the current i-th page if it needs to */
	if (stream->page)
		iommu_map(domain->iommu_domain, start_addr + ethosn_page_size(0, i, dma_info), page_to_phys(stream->page), PAGE_SIZE, IOMMU_READ);

	/* Unmap only the actual number of pages mapped i.e. i */
	iommu_unmap_iova_pages(dma_info, domain->iommu_domain, stream, i);

free_iova:

	/* iommu_alloc_iova allocs the total number of pages,
	 * so it needs to free all of iovas irrespectively of
	 * how many have been actually mapped.
	 * Use start_addr since dma_info isn't updated in the
	 * case of error.
	 */
	iommu_free_iova(start_addr, stream, ethosn_nr_pages(dma_info));

early_exit:

	return -ENOMEM;
}

#ifdef ETHOSN_TZMP1
static struct ethosn_dma_info *iommu_from_protected(struct ethosn_dma_sub_allocator *allocator, phys_addr_t start_addr, size_t size)
{
	struct page **pages = NULL;
	struct ethosn_dma_info_internal *dma_info = NULL;
	size_t nr_pages = DIV_ROUND_UP(size, PAGE_SIZE);
	size_t i;
	int ret;

	if (!PAGE_ALIGNED(start_addr) || !PAGE_ALIGNED(size)) {
		ret = -EINVAL;
		dev_err(allocator->dev, "%s: Start address and size must be paged aligned", __func__);
		goto error;
	}

	dma_info = devm_kzalloc(allocator->dev, sizeof(*dma_info), GFP_KERNEL);
	if (!dma_info) {
		ret = -ENOMEM;
		dev_err(allocator->dev, "%s: Failed to allocate dma_info", __func__);
		goto error;
	}

	pages = devm_kzalloc(allocator->dev, sizeof(struct page *) * nr_pages, GFP_KERNEL);
	if (!pages) {
		ret = -ENOMEM;
		dev_err(allocator->dev, "%s: Failed to allocate pages", __func__);
		goto free_dma_info;
	}

	for (i = 0U; i < nr_pages; ++i) {
		const phys_addr_t page_phys = (start_addr + (i * PAGE_SIZE));

		pages[i] = pfn_to_page(__phys_to_pfn(page_phys));
	}

	*dma_info = (struct ethosn_dma_info_internal){
		.info = (struct ethosn_dma_info){ .size = size, .cpu_addr = NULL, .iova_addr = 0, .imported = false },
		.source = ETHOSN_MEMORY_PROTECTED,
		.pages = pages,
	};

	return &dma_info->info;

free_dma_info:
	memset(dma_info, 0, sizeof(*dma_info));
	devm_kfree(allocator->dev, dma_info);
error:

	return ERR_PTR(ret);
}

#endif

static struct ethosn_dma_info *iommu_import(struct ethosn_dma_sub_allocator *allocator, int fd, size_t size)
{
	struct page **pages = NULL;
	struct ethosn_dma_info_internal *dma_info;
	dma_addr_t *dma_addr = NULL;
	int i = 0;
	size_t scatterlist_size = 0;
	struct dma_buf_internal *dma_buf_internal = NULL;
	struct scatterlist **sctrlst = NULL;
	struct scatterlist *scatterlist = NULL;
	struct scatterlist *tmp_scatterlist = NULL;
	struct device *parent_device;

	dma_info = devm_kzalloc(allocator->dev, sizeof(struct ethosn_dma_info_internal), GFP_KERNEL);
	if (!dma_info) {
		dev_err(allocator->dev, "%s: devm_kzalloc for dma_info failed", __func__);
		goto early_exit;
	}

	dma_buf_internal = devm_kzalloc(allocator->dev, sizeof(struct dma_buf_internal), GFP_KERNEL);
	if (!dma_buf_internal) {
		dev_err(allocator->dev, "%s: devm_kzalloc for dma_buf_internal failed", __func__);

		goto free_dma_info;
	}

	dma_buf_internal->fd = fd;

	dma_buf_internal->dmabuf = dma_buf_get(fd);
	if (IS_ERR(dma_buf_internal->dmabuf)) {
		dev_err(allocator->dev, "%s: dma_buf_get failed", __func__);
		goto free_buf_internal;
	}

	/* We can't pass the allocator device to dma_buf_attach, which leads to
	 * the linux dma framework attempting to map the buffer using the iommu
	 * mentioned in the dts for the allocator, which is not what we want
	 * because we are handling the mapping ourselves. This was leading to
	 * two mappings occurring which was leading to crashes and corrupted
	 * data.
	 *
	 * Instead we pass the ethosn_core device, which does not have an
	 * associated iommu, so the linux dma framework does a "direct" mapping,
	 * which doesn't seem to cause any problems.
	 */
	parent_device = allocator->dev->parent->parent;
	dma_buf_internal->attachment = dma_buf_attach(dma_buf_internal->dmabuf, parent_device);
	if (IS_ERR(dma_buf_internal->attachment)) {
		dev_err(allocator->dev, "%s: dma_buf_attach failed", __func__);
		goto fail_put;
	}

	dma_buf_internal->sgt = ethosn_dma_buf_map_attachment(dma_buf_internal->attachment);
	if (IS_ERR(dma_buf_internal->sgt)) {
		dev_err(allocator->dev, "%s: ethosn_dma_buf_map_attachment failed", __func__);
		goto fail_detach;
	}

	dev_dbg(allocator->dev, "%s: sg table orig_nents = %d, nents = %d", __func__, dma_buf_internal->sgt->orig_nents, dma_buf_internal->sgt->nents);

	sctrlst = (struct scatterlist **)devm_kzalloc(allocator->dev, sizeof(struct scatterlist *) * dma_buf_internal->sgt->nents, GFP_KERNEL);
	if (!sctrlst) {
		dev_err(allocator->dev, "%s: devm_kzalloc for sctrlst failed", __func__);
		goto fail_unmap_attachment;
	}

	pages = (struct page **)devm_kzalloc(allocator->dev, sizeof(struct page *) * dma_buf_internal->sgt->nents, GFP_KERNEL);
	if (!pages) {
		dev_err(allocator->dev, "%s: devm_kzalloc for pages failed", __func__);
		goto free_scatterlist;
	}

	dma_addr = (dma_addr_t *)devm_kzalloc(allocator->dev, sizeof(dma_addr_t) * dma_buf_internal->sgt->nents, GFP_KERNEL);

	if (!dma_addr) {
		dev_err(allocator->dev, "%s: devm_kzalloc for dma_addr failed", __func__);
		goto free_pages_list;
	}

	/* Note:
	 * we copy the content of sg_table and scatterlist structs into the
	 * ethosn_dma_info_internal and ethosn_dma_info ones so that we can
	 * reuse most of the API functions we already made for
	 * ETHOSN_CREATE_BUFFER ioctl.
	 */
	scatterlist = dma_buf_internal->sgt->sgl;

	for_each_sg (scatterlist, tmp_scatterlist, dma_buf_internal->sgt->nents, i) {
		if (tmp_scatterlist->offset != 0) {
			dev_err(allocator->dev, "%s: failed to iommu import scatterlist offset is not zero, we only support zero", __func__);
			goto free_dma_address;
		}

		pages[i] = sg_page(tmp_scatterlist);
		dma_addr[i] = sg_dma_address(tmp_scatterlist);
		scatterlist_size += sg_dma_len(tmp_scatterlist);
		sctrlst[i] = tmp_scatterlist;

		/* Print some debug logs but only for the first few entries,
		 * to avoid too much spam.
		 */
		if (i < 4)
			dev_dbg(allocator->dev, "%s: Imported scatter-gather entry %d/%d: pfn %lu, dma addr %pad, size %d", __func__, i,
				dma_buf_internal->sgt->nents, page_to_pfn(pages[i]), &sg_dma_address(tmp_scatterlist), sg_dma_len(tmp_scatterlist));
	}

	if (scatterlist_size < size) {
		dev_err(allocator->dev, "%s: Provided buffer size does not match scatterlist", __func__);
		goto free_dma_address;
	}

	dev_dbg(allocator->dev, "%s: Imported shared DMA buffer. handle=%pK", __func__, dma_info);

	*dma_info = (struct ethosn_dma_info_internal){
		.info = (struct ethosn_dma_info){ .size = scatterlist_size, .cpu_addr = NULL, .iova_addr = 0, .imported = true },
		.source = ETHOSN_MEMORY_IMPORT,
		.dma_addr = dma_addr,
		.pages = pages,
		.dma_buf_internal = dma_buf_internal,
		.scatterlist = sctrlst,
		.iova_mapped = false
	};

	return &dma_info->info;

free_dma_address:
	devm_kfree(allocator->dev, dma_addr);
free_pages_list:
	devm_kfree(allocator->dev, pages);
free_scatterlist:
	devm_kfree(allocator->dev, sctrlst);
fail_unmap_attachment:
	dma_buf_unmap_attachment(dma_buf_internal->attachment, dma_buf_internal->sgt, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf_internal->dmabuf, dma_buf_internal->attachment);
fail_put:
	dma_buf_put(dma_buf_internal->dmabuf);
free_buf_internal:
	memset(dma_buf_internal, 0, sizeof(*dma_buf_internal));
	devm_kfree(allocator->dev, dma_buf_internal);
free_dma_info:
	memset(dma_info, 0, sizeof(*dma_info));
	devm_kfree(allocator->dev, dma_info);
early_exit:

	return ERR_PTR(-ENOMEM);
}

static void iommu_release(struct ethosn_dma_sub_allocator *allocator, struct ethosn_dma_info **_dma_info)
{
	struct ethosn_dma_info_internal *dma_info = container_of(*_dma_info, typeof(*dma_info), info);
	struct dma_buf_internal *dma_buf_internal = dma_info->dma_buf_internal;

	if (WARN_ON(dma_info->source != ETHOSN_MEMORY_IMPORT))
		return;

	if (dma_info->info.size) {
		memset(dma_info->dma_addr, 0, (sizeof(dma_addr_t) * dma_buf_internal->sgt->nents));
		devm_kfree(allocator->dev, dma_info->dma_addr);

		memset(dma_info->pages, 0, (sizeof(struct page *) * dma_buf_internal->sgt->nents));
		devm_kfree(allocator->dev, dma_info->pages);

		memset(dma_info->scatterlist, 0, (sizeof(struct scatterlist *) * dma_buf_internal->sgt->nents));
		devm_kfree(allocator->dev, dma_info->scatterlist);
	}

	dma_buf_unmap_attachment(dma_buf_internal->attachment, dma_buf_internal->sgt, DMA_BIDIRECTIONAL);
	dma_buf_detach(dma_buf_internal->dmabuf, dma_buf_internal->attachment);
	dma_buf_put(dma_buf_internal->dmabuf);

	memset(dma_buf_internal, 0, sizeof(*dma_buf_internal));
	devm_kfree(allocator->dev, dma_buf_internal);
	memset(dma_info, 0, sizeof(*dma_info));
	devm_kfree(allocator->dev, dma_info);

	/* Clear the caller's pointer, so they aren't left with it dangling */
	*_dma_info = (struct ethosn_dma_info *)NULL;
}

static void iommu_iova_unmap(struct ethosn_dma_sub_allocator *allocator, struct ethosn_dma_info *const _dma_info)
{
	struct ethosn_dma_info_internal *dma_info = container_of(_dma_info, typeof(*dma_info), info);
	struct ethosn_allocator_internal *allocator_private = container_of(allocator, typeof(*allocator_private), allocator);
	struct ethosn_iommu_domain *domain = &allocator_private->ethosn_iommu_domain;
	struct ethosn_iommu_stream *stream = &domain->stream;

	if (WARN_ON(stream->type != _dma_info->stream_type))
		return;

	if (dma_info->iova_mapped) {
		int nr_scatter_pages = ethosn_nr_sg_objects(dma_info);
		int nr_pages = ethosn_nr_pages(dma_info);

		iommu_unmap_iova_pages(dma_info, domain->iommu_domain, stream, nr_scatter_pages);

		iommu_free_iova(dma_info->info.iova_addr, stream, nr_pages);
	}
}

static void iommu_free(struct ethosn_dma_sub_allocator *allocator, struct ethosn_dma_info **_dma_info)
{
	struct ethosn_dma_info_internal *dma_info = container_of(*_dma_info, typeof(*dma_info), info);
	const size_t nr_pages = DIV_ROUND_UP((*_dma_info)->size, PAGE_SIZE);

	if (dma_info->info.size) {
		switch (dma_info->source) {
		case ETHOSN_MEMORY_ALLOC:
			/* Clear any data before freeing the memory */
			memset(dma_info->info.cpu_addr, 0, dma_info->info.size);
			vunmap(dma_info->info.cpu_addr);
			iommu_free_pages(allocator, dma_info->dma_addr, dma_info->pages, nr_pages);
			break;
		case ETHOSN_MEMORY_PROTECTED:
			/* Nothing to unmap here */
			break;
		case ETHOSN_MEMORY_IMPORT:
		/* Handled by iommu_release */
		/* Fallthrough */
		default:
			WARN_ON(1);
		}

		if (dma_info->dma_addr) {
			memset(dma_info->dma_addr, 0, sizeof(dma_addr_t) * nr_pages);
			devm_kfree(allocator->dev, dma_info->dma_addr);
		}

		memset(dma_info->pages, 0, sizeof(struct page *) * nr_pages);
		devm_kfree(allocator->dev, dma_info->pages);
	}

	memset(dma_info, 0, sizeof(*dma_info));
	devm_kfree(allocator->dev, dma_info);

	/* Clear the caller's pointer, so they aren't left with it dangling */
	*_dma_info = (struct ethosn_dma_info *)NULL;
}

static void iommu_sync_for_device(struct ethosn_dma_sub_allocator *allocator, struct ethosn_dma_info *_dma_info)
{
	struct ethosn_dma_info_internal *dma_info = container_of(_dma_info, typeof(*dma_info), info);
	int nr_pages = ethosn_nr_sg_objects(dma_info);
	int i;

	switch (dma_info->source) {
	case ETHOSN_MEMORY_ALLOC:
		for (i = 0; i < nr_pages; ++i)
			dma_sync_single_for_device(allocator->dev, dma_info->dma_addr[i], ethosn_page_size(i, i + 1, dma_info), DMA_TO_DEVICE);

		break;
	case ETHOSN_MEMORY_IMPORT:
		dma_buf_end_cpu_access(dma_info->dma_buf_internal->dmabuf, DMA_TO_DEVICE);
		break;
	case ETHOSN_MEMORY_PROTECTED:
		/* Protected memory can't be synced so do nothing */
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static void iommu_sync_for_cpu(struct ethosn_dma_sub_allocator *allocator, struct ethosn_dma_info *_dma_info)
{
	struct ethosn_dma_info_internal *dma_info = container_of(_dma_info, typeof(*dma_info), info);
	int nr_scatter_pages = ethosn_nr_sg_objects(dma_info);
	int i;

	switch (dma_info->source) {
	case ETHOSN_MEMORY_ALLOC:
		for (i = 0; i < nr_scatter_pages; ++i)
			dma_sync_single_for_cpu(allocator->dev, dma_info->dma_addr[i], ethosn_page_size(i, i + 1, dma_info), DMA_FROM_DEVICE);

		break;
	case ETHOSN_MEMORY_IMPORT:
		dma_buf_begin_cpu_access(dma_info->dma_buf_internal->dmabuf, DMA_FROM_DEVICE);
		break;
	case ETHOSN_MEMORY_PROTECTED:
		/* Protected memory can't be synced so do nothing */
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static int iommu_mmap(struct ethosn_dma_sub_allocator *allocator, struct vm_area_struct *const vma, const struct ethosn_dma_info *const _dma_info)
{
	struct ethosn_dma_info_internal *dma_info = container_of(_dma_info, typeof(*dma_info), info);
	size_t vm_map_req_size = vma->vm_end - vma->vm_start;
	int nr_scatter_pages = ethosn_nr_sg_objects_user_mmap(dma_info, vm_map_req_size);
	int i;

	switch (dma_info->source) {
	case ETHOSN_MEMORY_ALLOC:
		for (i = 0; i < nr_scatter_pages; ++i) {
			unsigned long addr = vma->vm_start + ethosn_page_size(0, i, dma_info);
			unsigned long pfn = page_to_pfn(dma_info->pages[i]);
			unsigned long size = ethosn_page_size(i, i + 1, dma_info);

			if (remap_pfn_range(vma, addr, pfn, size, vma->vm_page_prot))
				return -EAGAIN;
		}

		return 0;
	case ETHOSN_MEMORY_IMPORT:

		/* If this is a dmabuf, let the exporter do the mmapping. */
		return dma_buf_mmap(dma_info->dma_buf_internal->dmabuf, vma, vma->vm_pgoff);
	case ETHOSN_MEMORY_PROTECTED:

		return -EPERM;
	default:
		WARN_ON(1);

		return -EINVAL;
	}
}

// 将当前 allocator 和 stream_type 所关联的, 起点为 addr_base, 长度为 IOMMU_ADDR_SIZE 的 DMA 虚拟地址空间经过页对齐之后, 全部映射到同一段物理内存页中;
static int iommu_stream_init(struct ethosn_allocator_internal *allocator, enum ethosn_stream_type stream_type, dma_addr_t addr_base, size_t bitmap_size,
			     phys_addr_t speculative_page_addr)
{
	struct ethosn_iommu_domain *domain = &allocator->ethosn_iommu_domain;
	struct ethosn_iommu_stream *stream = &domain->stream;
	size_t nr_pages = DIV_ROUND_UP(IOMMU_ADDR_SIZE, PAGE_SIZE); // 为将当前虚拟地址空间全部映射到物理地址空间, 所需要的物理内存页数
	size_t i;
	int err;

	dev_dbg(allocator->allocator.dev, "%s: stream_type %u\n", __func__, stream_type);

	stream->bitmap = devm_kzalloc(allocator->allocator.dev, bitmap_size, GFP_KERNEL);
	if (!stream->bitmap)
		return -ENOMEM;

	stream->addr_base = addr_base;
	stream->type = stream_type;
    // bitmap_size 表示能够容纳此内存页掩码所需的总字节数, 而 bits 表示能够容纳此内存页掩码所需的总位数, 而非内存页的个数, bits 是比 nr_pages 稍大的 8 的倍数, 如此满足字节对齐要求
	stream->bits = bitmap_size * BITS_PER_BYTE;
	spin_lock_init(&stream->lock);

	if (stream_type > ETHOSN_STREAM_COMMAND_STREAM)
		return 0;

	if (!speculative_page_addr) {
		stream->page = alloc_page(GFP_KERNEL);
		stream->allocated_page = true;
	} else {
		stream->page = pfn_to_page(__phys_to_pfn(speculative_page_addr));
		stream->allocated_page = false;
	}

	if (!stream->page)
		goto free_bitmap;

	// 这里, 将从 dma iova 基地址 addr_base 开始的长度为 nr_pages*PAGE_SIZE 的虚拟地址逐一映射到同一段物理内存页; 这些映射由同一个 domain 进行权限管理和内存隔离 
    // 这样做的好处:
    // 1. 这一整段连续的虚拟地址空间具有完全一致的访问权限;
    // 2. 通过将多个虚拟地址映射到同一个物理页面, 可以简化内存管理, 因为只需要处理一个物理页面的分配和释放;
	for (i = 0; i < nr_pages; ++i) {
		err = iommu_map(domain->iommu_domain, stream->addr_base + i * PAGE_SIZE, page_to_phys(stream->page), PAGE_SIZE, IOMMU_READ);

		if (err) {
			dev_err(allocator->allocator.dev, "failed to iommu map iova 0x%llX pa 0x%llX size %lu\n", stream->addr_base + i * PAGE_SIZE,
				page_to_phys(stream->page), PAGE_SIZE);
			goto unmap_page;
		}
	}

	return 0;
unmap_page:
	iommu_unmap(domain->iommu_domain, stream->addr_base, i * PAGE_SIZE);

	if (stream->allocated_page)
		__free_page(stream->page);

free_bitmap:
	devm_kfree(allocator->allocator.dev, stream->bitmap);

	return -ENOMEM;
}

static void iommu_stream_deinit(struct ethosn_allocator_internal *allocator)
{
	struct ethosn_iommu_domain *domain = &allocator->ethosn_iommu_domain;
	struct ethosn_iommu_stream *stream = &domain->stream;
	size_t nr_pages = DIV_ROUND_UP(IOMMU_ADDR_SIZE, PAGE_SIZE);

	/* Parent and children share the streams, make sure that it is not
	 * freed twice.
	 */
	if (stream->bitmap) {
		devm_kfree(allocator->allocator.dev, stream->bitmap);
		stream->bitmap = NULL;
	}

	if (!stream->page)
		return;

	/* Unmap all the virtual space (see iommu_stream_init). */
	iommu_unmap(domain->iommu_domain, stream->addr_base, nr_pages * PAGE_SIZE);

	if (stream->allocated_page)
		__free_page(stream->page);

	stream->page = NULL;
}

// destroy 函数的作用是清理掉由 sub_allocator 构造的静态内存管理资源
static void iommu_allocator_destroy(struct ethosn_dma_sub_allocator *_allocator)
{
	struct ethosn_allocator_internal *allocator;
	struct iommu_domain *domain;
	struct device *dev;

	if (!_allocator)
		return;

	allocator = container_of(_allocator, typeof(*allocator), allocator);
	domain = allocator->ethosn_iommu_domain.iommu_domain;
	dev = _allocator->dev;

	iommu_stream_deinit(allocator);

	memset(allocator, 0, sizeof(struct ethosn_allocator_internal));
	devm_kfree(dev, allocator);

	ethosn_iommu_put_domain_for_dev(dev, domain);
}

// 使用 iommu 的方式构造内存管理单元 allocator
// 经此操作: 由 ethosn 设备对应的 dev 和 stream_type 共同指定的 sub_allocator 对象, 将管理起始地址为 addr_base, 长度为 IOMMU_ADDR_SIZE 的完整独立的虚拟地址空间的管理权限
struct ethosn_dma_sub_allocator *ethosn_dma_iommu_allocator_create(struct device *dev, enum ethosn_stream_type stream_type, dma_addr_t addr_base,
								   phys_addr_t speculative_page_addr)
{
	static const struct ethosn_dma_allocator_ops ops = { .destroy = iommu_allocator_destroy,
							     .alloc = iommu_alloc,
							     .free = iommu_free,
							     .mmap = iommu_mmap,
							     .map = iommu_iova_map,
#ifdef ETHOSN_TZMP1
							     .from_protected = iommu_from_protected,
#endif
							     .import = iommu_import,
							     .release = iommu_release,
							     .unmap = iommu_iova_unmap,
							     .sync_for_device = iommu_sync_for_device,
							     .sync_for_cpu = iommu_sync_for_cpu,
							     .get_addr_base = iommu_get_addr_base,
							     .get_addr_size = iommu_get_addr_size };
	static const struct ethosn_dma_allocator_ops ops_no_iommu = {
		.destroy = iommu_allocator_destroy,
		.alloc = iommu_alloc,
		.free = iommu_free,
		.mmap = iommu_mmap,
		.import = iommu_import,
		.release = iommu_release,
		.sync_for_device = iommu_sync_for_device,
		.sync_for_cpu = iommu_sync_for_cpu,
	};

	// 仅在当前模块中保存内部数据的数据结构, 包含了当前 allocator 与物理内存相关的信息
	struct ethosn_allocator_internal *allocator;
	struct iommu_domain *domain = NULL;
	struct iommu_fwspec *fwspec = NULL;
	size_t bitmap_size;
	int ret;

	domain = ethosn_iommu_get_domain_for_dev(dev);

	allocator = devm_kzalloc(dev, sizeof(struct ethosn_allocator_internal), GFP_KERNEL);
	if (!allocator)
		return ERR_PTR(-ENOMEM);

	allocator->allocator.dev = dev;
	allocator->ethosn_iommu_domain.iommu_domain = domain;

	if (domain) {
        // 位图的每一个位表示将当前长度为 IOMMU_ADDR_SIZE 的虚拟地址空间完整映射到物理地址空间所需的每一个内存页, 相当于内存页的掩码
        // bitmap_size 表示能够容纳此内存页掩码所需的总字节数
		bitmap_size = BITS_TO_LONGS(IOMMU_ADDR_SIZE >> PAGE_SHIFT) * sizeof(unsigned long);

		ret = iommu_stream_init(allocator, stream_type, addr_base, bitmap_size, speculative_page_addr);
		if (ret)
			goto err_stream;

		allocator->allocator.ops = &ops;
	} else {
		allocator->allocator.ops = &ops_no_iommu;
	}

	fwspec = dev_iommu_fwspec_get(allocator->allocator.dev);

	if (!fwspec || fwspec->num_ids != 1) {
		ret = -EINVAL;
		goto err_stream;
	}

	allocator->allocator.smmu_stream_id = fwspec->ids[0];

	dev_dbg(dev, "Created IOMMU DMA allocator. handle=%pK", allocator);

	return &allocator->allocator;

err_stream:
	devm_kfree(dev, allocator);

	return ERR_PTR(ret);
}
