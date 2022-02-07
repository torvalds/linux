// SPDX-License-Identifier: GPL-2.0-only
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/semaphore.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/err.h>
#include <asm/byteorder.h>

#include "hinic_hw_if.h"
#include "hinic_hw_wqe.h"
#include "hinic_hw_wq.h"
#include "hinic_hw_cmdq.h"

#define WQS_BLOCKS_PER_PAGE             4

#define WQ_BLOCK_SIZE                   4096
#define WQS_PAGE_SIZE                   (WQS_BLOCKS_PER_PAGE * WQ_BLOCK_SIZE)

#define WQS_MAX_NUM_BLOCKS              128
#define WQS_FREE_BLOCKS_SIZE(wqs)       (WQS_MAX_NUM_BLOCKS * \
					 sizeof((wqs)->free_blocks[0]))

#define WQ_SIZE(wq)                     ((wq)->q_depth * (wq)->wqebb_size)

#define WQ_PAGE_ADDR_SIZE               sizeof(u64)
#define WQ_MAX_PAGES                    (WQ_BLOCK_SIZE / WQ_PAGE_ADDR_SIZE)

#define CMDQ_BLOCK_SIZE                 512
#define CMDQ_PAGE_SIZE                  4096

#define CMDQ_WQ_MAX_PAGES               (CMDQ_BLOCK_SIZE / WQ_PAGE_ADDR_SIZE)

#define WQ_BASE_VADDR(wqs, wq)          \
			((void *)((wqs)->page_vaddr[(wq)->page_idx]) \
				+ (wq)->block_idx * WQ_BLOCK_SIZE)

#define WQ_BASE_PADDR(wqs, wq)          \
			((wqs)->page_paddr[(wq)->page_idx] \
				+ (wq)->block_idx * WQ_BLOCK_SIZE)

#define WQ_BASE_ADDR(wqs, wq)           \
			((void *)((wqs)->shadow_page_vaddr[(wq)->page_idx]) \
				+ (wq)->block_idx * WQ_BLOCK_SIZE)

#define CMDQ_BASE_VADDR(cmdq_pages, wq) \
			((void *)((cmdq_pages)->page_vaddr) \
				+ (wq)->block_idx * CMDQ_BLOCK_SIZE)

#define CMDQ_BASE_PADDR(cmdq_pages, wq) \
			((cmdq_pages)->page_paddr \
				+ (wq)->block_idx * CMDQ_BLOCK_SIZE)

#define CMDQ_BASE_ADDR(cmdq_pages, wq)  \
			((void *)((cmdq_pages)->shadow_page_vaddr) \
				+ (wq)->block_idx * CMDQ_BLOCK_SIZE)

#define WQ_PAGE_ADDR(wq, idx)           \
			((wq)->shadow_block_vaddr[WQE_PAGE_NUM(wq, idx)])

#define MASKED_WQE_IDX(wq, idx)         ((idx) & (wq)->mask)

#define WQE_IN_RANGE(wqe, start, end)   \
		(((unsigned long)(wqe) >= (unsigned long)(start)) && \
		 ((unsigned long)(wqe) < (unsigned long)(end)))

#define WQE_SHADOW_PAGE(wq, wqe)        \
		(((unsigned long)(wqe) - (unsigned long)(wq)->shadow_wqe) \
			/ (wq)->max_wqe_size)

static inline int WQE_PAGE_OFF(struct hinic_wq *wq, u16 idx)
{
	return (((idx) & ((wq)->num_wqebbs_per_page - 1))
		<< (wq)->wqebb_size_shift);
}

static inline int WQE_PAGE_NUM(struct hinic_wq *wq, u16 idx)
{
	return (((idx) >> ((wq)->wqebbs_per_page_shift))
		& ((wq)->num_q_pages - 1));
}

/**
 * queue_alloc_page - allocate page for Queue
 * @hwif: HW interface for allocating DMA
 * @vaddr: virtual address will be returned in this address
 * @paddr: physical address will be returned in this address
 * @shadow_vaddr: VM area will be return here for holding WQ page addresses
 * @page_sz: page size of each WQ page
 *
 * Return 0 - Success, negative - Failure
 **/
static int queue_alloc_page(struct hinic_hwif *hwif, u64 **vaddr, u64 *paddr,
			    void ***shadow_vaddr, size_t page_sz)
{
	struct pci_dev *pdev = hwif->pdev;
	dma_addr_t dma_addr;

	*vaddr = dma_alloc_coherent(&pdev->dev, page_sz, &dma_addr,
				    GFP_KERNEL);
	if (!*vaddr) {
		dev_err(&pdev->dev, "Failed to allocate dma for wqs page\n");
		return -ENOMEM;
	}

	*paddr = (u64)dma_addr;

	/* use vzalloc for big mem */
	*shadow_vaddr = vzalloc(page_sz);
	if (!*shadow_vaddr)
		goto err_shadow_vaddr;

	return 0;

err_shadow_vaddr:
	dma_free_coherent(&pdev->dev, page_sz, *vaddr, dma_addr);
	return -ENOMEM;
}

/**
 * wqs_allocate_page - allocate page for WQ set
 * @wqs: Work Queue Set
 * @page_idx: the page index of the page will be allocated
 *
 * Return 0 - Success, negative - Failure
 **/
static int wqs_allocate_page(struct hinic_wqs *wqs, int page_idx)
{
	return queue_alloc_page(wqs->hwif, &wqs->page_vaddr[page_idx],
				&wqs->page_paddr[page_idx],
				&wqs->shadow_page_vaddr[page_idx],
				WQS_PAGE_SIZE);
}

/**
 * wqs_free_page - free page of WQ set
 * @wqs: Work Queue Set
 * @page_idx: the page index of the page will be freed
 **/
static void wqs_free_page(struct hinic_wqs *wqs, int page_idx)
{
	struct hinic_hwif *hwif = wqs->hwif;
	struct pci_dev *pdev = hwif->pdev;

	dma_free_coherent(&pdev->dev, WQS_PAGE_SIZE,
			  wqs->page_vaddr[page_idx],
			  (dma_addr_t)wqs->page_paddr[page_idx]);
	vfree(wqs->shadow_page_vaddr[page_idx]);
}

/**
 * cmdq_allocate_page - allocate page for cmdq
 * @cmdq_pages: the pages of the cmdq queue struct to hold the page
 *
 * Return 0 - Success, negative - Failure
 **/
static int cmdq_allocate_page(struct hinic_cmdq_pages *cmdq_pages)
{
	return queue_alloc_page(cmdq_pages->hwif, &cmdq_pages->page_vaddr,
				&cmdq_pages->page_paddr,
				&cmdq_pages->shadow_page_vaddr,
				CMDQ_PAGE_SIZE);
}

/**
 * cmdq_free_page - free page from cmdq
 * @cmdq_pages: the pages of the cmdq queue struct that hold the page
 *
 * Return 0 - Success, negative - Failure
 **/
static void cmdq_free_page(struct hinic_cmdq_pages *cmdq_pages)
{
	struct hinic_hwif *hwif = cmdq_pages->hwif;
	struct pci_dev *pdev = hwif->pdev;

	dma_free_coherent(&pdev->dev, CMDQ_PAGE_SIZE,
			  cmdq_pages->page_vaddr,
			  (dma_addr_t)cmdq_pages->page_paddr);
	vfree(cmdq_pages->shadow_page_vaddr);
}

static int alloc_page_arrays(struct hinic_wqs *wqs)
{
	struct hinic_hwif *hwif = wqs->hwif;
	struct pci_dev *pdev = hwif->pdev;

	wqs->page_paddr = devm_kcalloc(&pdev->dev, wqs->num_pages,
				       sizeof(*wqs->page_paddr), GFP_KERNEL);
	if (!wqs->page_paddr)
		return -ENOMEM;

	wqs->page_vaddr = devm_kcalloc(&pdev->dev, wqs->num_pages,
				       sizeof(*wqs->page_vaddr), GFP_KERNEL);
	if (!wqs->page_vaddr)
		goto err_page_vaddr;

	wqs->shadow_page_vaddr = devm_kcalloc(&pdev->dev, wqs->num_pages,
					      sizeof(*wqs->shadow_page_vaddr),
					      GFP_KERNEL);
	if (!wqs->shadow_page_vaddr)
		goto err_page_shadow_vaddr;

	return 0;

err_page_shadow_vaddr:
	devm_kfree(&pdev->dev, wqs->page_vaddr);

err_page_vaddr:
	devm_kfree(&pdev->dev, wqs->page_paddr);
	return -ENOMEM;
}

static void free_page_arrays(struct hinic_wqs *wqs)
{
	struct hinic_hwif *hwif = wqs->hwif;
	struct pci_dev *pdev = hwif->pdev;

	devm_kfree(&pdev->dev, wqs->shadow_page_vaddr);
	devm_kfree(&pdev->dev, wqs->page_vaddr);
	devm_kfree(&pdev->dev, wqs->page_paddr);
}

static int wqs_next_block(struct hinic_wqs *wqs, int *page_idx,
			  int *block_idx)
{
	int pos;

	down(&wqs->alloc_blocks_lock);

	wqs->num_free_blks--;

	if (wqs->num_free_blks < 0) {
		wqs->num_free_blks++;
		up(&wqs->alloc_blocks_lock);
		return -ENOMEM;
	}

	pos = wqs->alloc_blk_pos++;
	pos &= WQS_MAX_NUM_BLOCKS - 1;

	*page_idx = wqs->free_blocks[pos].page_idx;
	*block_idx = wqs->free_blocks[pos].block_idx;

	wqs->free_blocks[pos].page_idx = -1;
	wqs->free_blocks[pos].block_idx = -1;

	up(&wqs->alloc_blocks_lock);
	return 0;
}

static void wqs_return_block(struct hinic_wqs *wqs, int page_idx,
			     int block_idx)
{
	int pos;

	down(&wqs->alloc_blocks_lock);

	pos = wqs->return_blk_pos++;
	pos &= WQS_MAX_NUM_BLOCKS - 1;

	wqs->free_blocks[pos].page_idx = page_idx;
	wqs->free_blocks[pos].block_idx = block_idx;

	wqs->num_free_blks++;

	up(&wqs->alloc_blocks_lock);
}

static void init_wqs_blocks_arr(struct hinic_wqs *wqs)
{
	int page_idx, blk_idx, pos = 0;

	for (page_idx = 0; page_idx < wqs->num_pages; page_idx++) {
		for (blk_idx = 0; blk_idx < WQS_BLOCKS_PER_PAGE; blk_idx++) {
			wqs->free_blocks[pos].page_idx = page_idx;
			wqs->free_blocks[pos].block_idx = blk_idx;
			pos++;
		}
	}

	wqs->alloc_blk_pos = 0;
	wqs->return_blk_pos = pos;
	wqs->num_free_blks = pos;

	sema_init(&wqs->alloc_blocks_lock, 1);
}

/**
 * hinic_wqs_alloc - allocate Work Queues set
 * @wqs: Work Queue Set
 * @max_wqs: maximum wqs to allocate
 * @hwif: HW interface for use for the allocation
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_wqs_alloc(struct hinic_wqs *wqs, int max_wqs,
		    struct hinic_hwif *hwif)
{
	struct pci_dev *pdev = hwif->pdev;
	int err, i, page_idx;

	max_wqs = ALIGN(max_wqs, WQS_BLOCKS_PER_PAGE);
	if (max_wqs > WQS_MAX_NUM_BLOCKS)  {
		dev_err(&pdev->dev, "Invalid max_wqs = %d\n", max_wqs);
		return -EINVAL;
	}

	wqs->hwif = hwif;
	wqs->num_pages = max_wqs / WQS_BLOCKS_PER_PAGE;

	if (alloc_page_arrays(wqs)) {
		dev_err(&pdev->dev,
			"Failed to allocate mem for page addresses\n");
		return -ENOMEM;
	}

	for (page_idx = 0; page_idx < wqs->num_pages; page_idx++) {
		err = wqs_allocate_page(wqs, page_idx);
		if (err) {
			dev_err(&pdev->dev, "Failed wq page allocation\n");
			goto err_wq_allocate_page;
		}
	}

	wqs->free_blocks = devm_kzalloc(&pdev->dev, WQS_FREE_BLOCKS_SIZE(wqs),
					GFP_KERNEL);
	if (!wqs->free_blocks) {
		err = -ENOMEM;
		goto err_alloc_blocks;
	}

	init_wqs_blocks_arr(wqs);
	return 0;

err_alloc_blocks:
err_wq_allocate_page:
	for (i = 0; i < page_idx; i++)
		wqs_free_page(wqs, i);

	free_page_arrays(wqs);
	return err;
}

/**
 * hinic_wqs_free - free Work Queues set
 * @wqs: Work Queue Set
 **/
void hinic_wqs_free(struct hinic_wqs *wqs)
{
	struct hinic_hwif *hwif = wqs->hwif;
	struct pci_dev *pdev = hwif->pdev;
	int page_idx;

	devm_kfree(&pdev->dev, wqs->free_blocks);

	for (page_idx = 0; page_idx < wqs->num_pages; page_idx++)
		wqs_free_page(wqs, page_idx);

	free_page_arrays(wqs);
}

/**
 * alloc_wqes_shadow - allocate WQE shadows for WQ
 * @wq: WQ to allocate shadows for
 *
 * Return 0 - Success, negative - Failure
 **/
static int alloc_wqes_shadow(struct hinic_wq *wq)
{
	struct hinic_hwif *hwif = wq->hwif;
	struct pci_dev *pdev = hwif->pdev;

	wq->shadow_wqe = devm_kcalloc(&pdev->dev, wq->num_q_pages,
				      wq->max_wqe_size, GFP_KERNEL);
	if (!wq->shadow_wqe)
		return -ENOMEM;

	wq->shadow_idx = devm_kcalloc(&pdev->dev, wq->num_q_pages,
				      sizeof(wq->prod_idx), GFP_KERNEL);
	if (!wq->shadow_idx)
		goto err_shadow_idx;

	return 0;

err_shadow_idx:
	devm_kfree(&pdev->dev, wq->shadow_wqe);
	return -ENOMEM;
}

/**
 * free_wqes_shadow - free WQE shadows of WQ
 * @wq: WQ to free shadows from
 **/
static void free_wqes_shadow(struct hinic_wq *wq)
{
	struct hinic_hwif *hwif = wq->hwif;
	struct pci_dev *pdev = hwif->pdev;

	devm_kfree(&pdev->dev, wq->shadow_idx);
	devm_kfree(&pdev->dev, wq->shadow_wqe);
}

/**
 * free_wq_pages - free pages of WQ
 * @hwif: HW interface for releasing dma addresses
 * @wq: WQ to free pages from
 * @num_q_pages: number pages to free
 **/
static void free_wq_pages(struct hinic_wq *wq, struct hinic_hwif *hwif,
			  int num_q_pages)
{
	struct pci_dev *pdev = hwif->pdev;
	int i;

	for (i = 0; i < num_q_pages; i++) {
		void **vaddr = &wq->shadow_block_vaddr[i];
		u64 *paddr = &wq->block_vaddr[i];
		dma_addr_t dma_addr;

		dma_addr = (dma_addr_t)be64_to_cpu(*paddr);
		dma_free_coherent(&pdev->dev, wq->wq_page_size, *vaddr,
				  dma_addr);
	}

	free_wqes_shadow(wq);
}

/**
 * alloc_wq_pages - alloc pages for WQ
 * @hwif: HW interface for allocating dma addresses
 * @wq: WQ to allocate pages for
 * @max_pages: maximum pages allowed
 *
 * Return 0 - Success, negative - Failure
 **/
static int alloc_wq_pages(struct hinic_wq *wq, struct hinic_hwif *hwif,
			  int max_pages)
{
	struct pci_dev *pdev = hwif->pdev;
	int i, err, num_q_pages;

	num_q_pages = ALIGN(WQ_SIZE(wq), wq->wq_page_size) / wq->wq_page_size;
	if (num_q_pages > max_pages) {
		dev_err(&pdev->dev, "Number wq pages exceeds the limit\n");
		return -EINVAL;
	}

	if (num_q_pages & (num_q_pages - 1)) {
		dev_err(&pdev->dev, "Number wq pages must be power of 2\n");
		return -EINVAL;
	}

	wq->num_q_pages = num_q_pages;

	err = alloc_wqes_shadow(wq);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate wqe shadow\n");
		return err;
	}

	for (i = 0; i < num_q_pages; i++) {
		void **vaddr = &wq->shadow_block_vaddr[i];
		u64 *paddr = &wq->block_vaddr[i];
		dma_addr_t dma_addr;

		*vaddr = dma_alloc_coherent(&pdev->dev, wq->wq_page_size,
					    &dma_addr, GFP_KERNEL);
		if (!*vaddr) {
			dev_err(&pdev->dev, "Failed to allocate wq page\n");
			goto err_alloc_wq_pages;
		}

		/* HW uses Big Endian Format */
		*paddr = cpu_to_be64(dma_addr);
	}

	return 0;

err_alloc_wq_pages:
	free_wq_pages(wq, hwif, i);
	return -ENOMEM;
}

/**
 * hinic_wq_allocate - Allocate the WQ resources from the WQS
 * @wqs: WQ set from which to allocate the WQ resources
 * @wq: WQ to allocate resources for it from the WQ set
 * @wqebb_size: Work Queue Block Byte Size
 * @wq_page_size: the page size in the Work Queue
 * @q_depth: number of wqebbs in WQ
 * @max_wqe_size: maximum WQE size that will be used in the WQ
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_wq_allocate(struct hinic_wqs *wqs, struct hinic_wq *wq,
		      u16 wqebb_size, u32 wq_page_size, u16 q_depth,
		      u16 max_wqe_size)
{
	struct hinic_hwif *hwif = wqs->hwif;
	struct pci_dev *pdev = hwif->pdev;
	u16 num_wqebbs_per_page;
	u16 wqebb_size_shift;
	int err;

	if (!is_power_of_2(wqebb_size)) {
		dev_err(&pdev->dev, "wqebb_size must be power of 2\n");
		return -EINVAL;
	}

	if (wq_page_size == 0) {
		dev_err(&pdev->dev, "wq_page_size must be > 0\n");
		return -EINVAL;
	}

	if (q_depth & (q_depth - 1)) {
		dev_err(&pdev->dev, "WQ q_depth must be power of 2\n");
		return -EINVAL;
	}

	wqebb_size_shift = ilog2(wqebb_size);
	num_wqebbs_per_page = ALIGN(wq_page_size, wqebb_size)
				>> wqebb_size_shift;

	if (!is_power_of_2(num_wqebbs_per_page)) {
		dev_err(&pdev->dev, "num wqebbs per page must be power of 2\n");
		return -EINVAL;
	}

	wq->hwif = hwif;

	err = wqs_next_block(wqs, &wq->page_idx, &wq->block_idx);
	if (err) {
		dev_err(&pdev->dev, "Failed to get free wqs next block\n");
		return err;
	}

	wq->wqebb_size = wqebb_size;
	wq->wq_page_size = wq_page_size;
	wq->q_depth = q_depth;
	wq->max_wqe_size = max_wqe_size;
	wq->num_wqebbs_per_page = num_wqebbs_per_page;
	wq->wqebbs_per_page_shift = ilog2(num_wqebbs_per_page);
	wq->wqebb_size_shift = wqebb_size_shift;
	wq->block_vaddr = WQ_BASE_VADDR(wqs, wq);
	wq->shadow_block_vaddr = WQ_BASE_ADDR(wqs, wq);
	wq->block_paddr = WQ_BASE_PADDR(wqs, wq);

	err = alloc_wq_pages(wq, wqs->hwif, WQ_MAX_PAGES);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate wq pages\n");
		goto err_alloc_wq_pages;
	}

	atomic_set(&wq->cons_idx, 0);
	atomic_set(&wq->prod_idx, 0);
	atomic_set(&wq->delta, q_depth);
	wq->mask = q_depth - 1;

	return 0;

err_alloc_wq_pages:
	wqs_return_block(wqs, wq->page_idx, wq->block_idx);
	return err;
}

/**
 * hinic_wq_free - Free the WQ resources to the WQS
 * @wqs: WQ set to free the WQ resources to it
 * @wq: WQ to free its resources to the WQ set resources
 **/
void hinic_wq_free(struct hinic_wqs *wqs, struct hinic_wq *wq)
{
	free_wq_pages(wq, wqs->hwif, wq->num_q_pages);

	wqs_return_block(wqs, wq->page_idx, wq->block_idx);
}

/**
 * hinic_wqs_cmdq_alloc - Allocate wqs for cmdqs
 * @cmdq_pages: will hold the pages of the cmdq
 * @wq: returned wqs
 * @hwif: HW interface
 * @cmdq_blocks: number of cmdq blocks/wq to allocate
 * @wqebb_size: Work Queue Block Byte Size
 * @wq_page_size: the page size in the Work Queue
 * @q_depth: number of wqebbs in WQ
 * @max_wqe_size: maximum WQE size that will be used in the WQ
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_wqs_cmdq_alloc(struct hinic_cmdq_pages *cmdq_pages,
			 struct hinic_wq *wq, struct hinic_hwif *hwif,
			 int cmdq_blocks, u16 wqebb_size, u32 wq_page_size,
			 u16 q_depth, u16 max_wqe_size)
{
	struct pci_dev *pdev = hwif->pdev;
	u16 num_wqebbs_per_page_shift;
	u16 num_wqebbs_per_page;
	u16 wqebb_size_shift;
	int i, j, err = -ENOMEM;

	if (!is_power_of_2(wqebb_size)) {
		dev_err(&pdev->dev, "wqebb_size must be power of 2\n");
		return -EINVAL;
	}

	if (wq_page_size == 0) {
		dev_err(&pdev->dev, "wq_page_size must be > 0\n");
		return -EINVAL;
	}

	if (q_depth & (q_depth - 1)) {
		dev_err(&pdev->dev, "WQ q_depth must be power of 2\n");
		return -EINVAL;
	}

	wqebb_size_shift = ilog2(wqebb_size);
	num_wqebbs_per_page = ALIGN(wq_page_size, wqebb_size)
				>> wqebb_size_shift;

	if (!is_power_of_2(num_wqebbs_per_page)) {
		dev_err(&pdev->dev, "num wqebbs per page must be power of 2\n");
		return -EINVAL;
	}

	cmdq_pages->hwif = hwif;

	err = cmdq_allocate_page(cmdq_pages);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate CMDQ page\n");
		return err;
	}
	num_wqebbs_per_page_shift = ilog2(num_wqebbs_per_page);

	for (i = 0; i < cmdq_blocks; i++) {
		wq[i].hwif = hwif;
		wq[i].page_idx = 0;
		wq[i].block_idx = i;

		wq[i].wqebb_size = wqebb_size;
		wq[i].wq_page_size = wq_page_size;
		wq[i].q_depth = q_depth;
		wq[i].max_wqe_size = max_wqe_size;
		wq[i].num_wqebbs_per_page = num_wqebbs_per_page;
		wq[i].wqebbs_per_page_shift = num_wqebbs_per_page_shift;
		wq[i].wqebb_size_shift = wqebb_size_shift;
		wq[i].block_vaddr = CMDQ_BASE_VADDR(cmdq_pages, &wq[i]);
		wq[i].shadow_block_vaddr = CMDQ_BASE_ADDR(cmdq_pages, &wq[i]);
		wq[i].block_paddr = CMDQ_BASE_PADDR(cmdq_pages, &wq[i]);

		err = alloc_wq_pages(&wq[i], cmdq_pages->hwif,
				     CMDQ_WQ_MAX_PAGES);
		if (err) {
			dev_err(&pdev->dev, "Failed to alloc CMDQ blocks\n");
			goto err_cmdq_block;
		}

		atomic_set(&wq[i].cons_idx, 0);
		atomic_set(&wq[i].prod_idx, 0);
		atomic_set(&wq[i].delta, q_depth);
		wq[i].mask = q_depth - 1;
	}

	return 0;

err_cmdq_block:
	for (j = 0; j < i; j++)
		free_wq_pages(&wq[j], cmdq_pages->hwif, wq[j].num_q_pages);

	cmdq_free_page(cmdq_pages);
	return err;
}

/**
 * hinic_wqs_cmdq_free - Free wqs from cmdqs
 * @cmdq_pages: hold the pages of the cmdq
 * @wq: wqs to free
 * @cmdq_blocks: number of wqs to free
 **/
void hinic_wqs_cmdq_free(struct hinic_cmdq_pages *cmdq_pages,
			 struct hinic_wq *wq, int cmdq_blocks)
{
	int i;

	for (i = 0; i < cmdq_blocks; i++)
		free_wq_pages(&wq[i], cmdq_pages->hwif, wq[i].num_q_pages);

	cmdq_free_page(cmdq_pages);
}

static void copy_wqe_to_shadow(struct hinic_wq *wq, void *shadow_addr,
			       int num_wqebbs, u16 idx)
{
	void *wqebb_addr;
	int i;

	for (i = 0; i < num_wqebbs; i++, idx++) {
		idx = MASKED_WQE_IDX(wq, idx);
		wqebb_addr = WQ_PAGE_ADDR(wq, idx) +
			     WQE_PAGE_OFF(wq, idx);

		memcpy(shadow_addr, wqebb_addr, wq->wqebb_size);

		shadow_addr += wq->wqebb_size;
	}
}

static void copy_wqe_from_shadow(struct hinic_wq *wq, void *shadow_addr,
				 int num_wqebbs, u16 idx)
{
	void *wqebb_addr;
	int i;

	for (i = 0; i < num_wqebbs; i++, idx++) {
		idx = MASKED_WQE_IDX(wq, idx);
		wqebb_addr = WQ_PAGE_ADDR(wq, idx) +
			     WQE_PAGE_OFF(wq, idx);

		memcpy(wqebb_addr, shadow_addr, wq->wqebb_size);
		shadow_addr += wq->wqebb_size;
	}
}

/**
 * hinic_get_wqe - get wqe ptr in the current pi and update the pi
 * @wq: wq to get wqe from
 * @wqe_size: wqe size
 * @prod_idx: returned pi
 *
 * Return wqe pointer
 **/
struct hinic_hw_wqe *hinic_get_wqe(struct hinic_wq *wq, unsigned int wqe_size,
				   u16 *prod_idx)
{
	int curr_pg, end_pg, num_wqebbs;
	u16 curr_prod_idx, end_prod_idx;

	*prod_idx = MASKED_WQE_IDX(wq, atomic_read(&wq->prod_idx));

	num_wqebbs = ALIGN(wqe_size, wq->wqebb_size) >> wq->wqebb_size_shift;

	if (atomic_sub_return(num_wqebbs, &wq->delta) <= 0) {
		atomic_add(num_wqebbs, &wq->delta);
		return ERR_PTR(-EBUSY);
	}

	end_prod_idx = atomic_add_return(num_wqebbs, &wq->prod_idx);

	end_prod_idx = MASKED_WQE_IDX(wq, end_prod_idx);
	curr_prod_idx = end_prod_idx - num_wqebbs;
	curr_prod_idx = MASKED_WQE_IDX(wq, curr_prod_idx);

	/* end prod index points to the next wqebb, therefore minus 1 */
	end_prod_idx = MASKED_WQE_IDX(wq, end_prod_idx - 1);

	curr_pg = WQE_PAGE_NUM(wq, curr_prod_idx);
	end_pg = WQE_PAGE_NUM(wq, end_prod_idx);

	*prod_idx = curr_prod_idx;

	/* If we only have one page, still need to get shadown wqe when
	 * wqe rolling-over page
	 */
	if (curr_pg != end_pg || MASKED_WQE_IDX(wq, end_prod_idx) < *prod_idx) {
		void *shadow_addr = &wq->shadow_wqe[curr_pg * wq->max_wqe_size];

		copy_wqe_to_shadow(wq, shadow_addr, num_wqebbs, *prod_idx);

		wq->shadow_idx[curr_pg] = *prod_idx;
		return shadow_addr;
	}

	return WQ_PAGE_ADDR(wq, *prod_idx) + WQE_PAGE_OFF(wq, *prod_idx);
}

/**
 * hinic_return_wqe - return the wqe when transmit failed
 * @wq: wq to return wqe
 * @wqe_size: wqe size
 **/
void hinic_return_wqe(struct hinic_wq *wq, unsigned int wqe_size)
{
	int num_wqebbs = ALIGN(wqe_size, wq->wqebb_size) / wq->wqebb_size;

	atomic_sub(num_wqebbs, &wq->prod_idx);

	atomic_add(num_wqebbs, &wq->delta);
}

/**
 * hinic_put_wqe - return the wqe place to use for a new wqe
 * @wq: wq to return wqe
 * @wqe_size: wqe size
 **/
void hinic_put_wqe(struct hinic_wq *wq, unsigned int wqe_size)
{
	int num_wqebbs = ALIGN(wqe_size, wq->wqebb_size)
			>> wq->wqebb_size_shift;

	atomic_add(num_wqebbs, &wq->cons_idx);

	atomic_add(num_wqebbs, &wq->delta);
}

/**
 * hinic_read_wqe - read wqe ptr in the current ci
 * @wq: wq to get read from
 * @wqe_size: wqe size
 * @cons_idx: returned ci
 *
 * Return wqe pointer
 **/
struct hinic_hw_wqe *hinic_read_wqe(struct hinic_wq *wq, unsigned int wqe_size,
				    u16 *cons_idx)
{
	int num_wqebbs = ALIGN(wqe_size, wq->wqebb_size)
			>> wq->wqebb_size_shift;
	u16 curr_cons_idx, end_cons_idx;
	int curr_pg, end_pg;

	if ((atomic_read(&wq->delta) + num_wqebbs) > wq->q_depth)
		return ERR_PTR(-EBUSY);

	curr_cons_idx = atomic_read(&wq->cons_idx);

	curr_cons_idx = MASKED_WQE_IDX(wq, curr_cons_idx);
	end_cons_idx = MASKED_WQE_IDX(wq, curr_cons_idx + num_wqebbs - 1);

	curr_pg = WQE_PAGE_NUM(wq, curr_cons_idx);
	end_pg = WQE_PAGE_NUM(wq, end_cons_idx);

	*cons_idx = curr_cons_idx;

	if (curr_pg != end_pg) {
		void *shadow_addr = &wq->shadow_wqe[curr_pg * wq->max_wqe_size];

		copy_wqe_to_shadow(wq, shadow_addr, num_wqebbs, *cons_idx);
		return shadow_addr;
	}

	return WQ_PAGE_ADDR(wq, *cons_idx) + WQE_PAGE_OFF(wq, *cons_idx);
}

/**
 * hinic_read_wqe_direct - read wqe directly from ci position
 * @wq: wq
 * @cons_idx: ci position
 *
 * Return wqe
 **/
struct hinic_hw_wqe *hinic_read_wqe_direct(struct hinic_wq *wq, u16 cons_idx)
{
	return WQ_PAGE_ADDR(wq, cons_idx) + WQE_PAGE_OFF(wq, cons_idx);
}

/**
 * wqe_shadow - check if a wqe is shadow
 * @wq: wq of the wqe
 * @wqe: the wqe for shadow checking
 *
 * Return true - shadow, false - Not shadow
 **/
static inline bool wqe_shadow(struct hinic_wq *wq, struct hinic_hw_wqe *wqe)
{
	size_t wqe_shadow_size = wq->num_q_pages * wq->max_wqe_size;

	return WQE_IN_RANGE(wqe, wq->shadow_wqe,
			    &wq->shadow_wqe[wqe_shadow_size]);
}

/**
 * hinic_write_wqe - write the wqe to the wq
 * @wq: wq to write wqe to
 * @wqe: wqe to write
 * @wqe_size: wqe size
 **/
void hinic_write_wqe(struct hinic_wq *wq, struct hinic_hw_wqe *wqe,
		     unsigned int wqe_size)
{
	int curr_pg, num_wqebbs;
	void *shadow_addr;
	u16 prod_idx;

	if (wqe_shadow(wq, wqe)) {
		curr_pg = WQE_SHADOW_PAGE(wq, wqe);

		prod_idx = wq->shadow_idx[curr_pg];
		num_wqebbs = ALIGN(wqe_size, wq->wqebb_size) / wq->wqebb_size;
		shadow_addr = &wq->shadow_wqe[curr_pg * wq->max_wqe_size];

		copy_wqe_from_shadow(wq, shadow_addr, num_wqebbs, prod_idx);
	}
}
