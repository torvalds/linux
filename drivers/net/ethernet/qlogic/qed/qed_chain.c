// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* Copyright (c) 2020 Marvell International Ltd. */

#include <linux/dma-mapping.h>
#include <linux/qed/qed_chain.h>
#include <linux/vmalloc.h>

#include "qed_dev_api.h"

static void qed_chain_init(struct qed_chain *chain,
			   const struct qed_chain_init_params *params,
			   u32 page_cnt)
{
	memset(chain, 0, sizeof(*chain));

	chain->elem_size = params->elem_size;
	chain->intended_use = params->intended_use;
	chain->mode = params->mode;
	chain->cnt_type = params->cnt_type;

	chain->elem_per_page = ELEMS_PER_PAGE(params->elem_size,
					      params->page_size);
	chain->usable_per_page = USABLE_ELEMS_PER_PAGE(params->elem_size,
						       params->page_size,
						       params->mode);
	chain->elem_unusable = UNUSABLE_ELEMS_PER_PAGE(params->elem_size,
						       params->mode);

	chain->elem_per_page_mask = chain->elem_per_page - 1;
	chain->next_page_mask = chain->usable_per_page &
				chain->elem_per_page_mask;

	chain->page_size = params->page_size;
	chain->page_cnt = page_cnt;
	chain->capacity = chain->usable_per_page * page_cnt;
	chain->size = chain->elem_per_page * page_cnt;

	if (params->ext_pbl_virt) {
		chain->pbl_sp.table_virt = params->ext_pbl_virt;
		chain->pbl_sp.table_phys = params->ext_pbl_phys;

		chain->b_external_pbl = true;
	}
}

static void qed_chain_init_next_ptr_elem(const struct qed_chain *chain,
					 void *virt_curr, void *virt_next,
					 dma_addr_t phys_next)
{
	struct qed_chain_next *next;
	u32 size;

	size = chain->elem_size * chain->usable_per_page;
	next = virt_curr + size;

	DMA_REGPAIR_LE(next->next_phys, phys_next);
	next->next_virt = virt_next;
}

static void qed_chain_init_mem(struct qed_chain *chain, void *virt_addr,
			       dma_addr_t phys_addr)
{
	chain->p_virt_addr = virt_addr;
	chain->p_phys_addr = phys_addr;
}

static void qed_chain_free_next_ptr(struct qed_dev *cdev,
				    struct qed_chain *chain)
{
	struct device *dev = &cdev->pdev->dev;
	struct qed_chain_next *next;
	dma_addr_t phys, phys_next;
	void *virt, *virt_next;
	u32 size, i;

	size = chain->elem_size * chain->usable_per_page;
	virt = chain->p_virt_addr;
	phys = chain->p_phys_addr;

	for (i = 0; i < chain->page_cnt; i++) {
		if (!virt)
			break;

		next = virt + size;
		virt_next = next->next_virt;
		phys_next = HILO_DMA_REGPAIR(next->next_phys);

		dma_free_coherent(dev, chain->page_size, virt, phys);

		virt = virt_next;
		phys = phys_next;
	}
}

static void qed_chain_free_single(struct qed_dev *cdev,
				  struct qed_chain *chain)
{
	if (!chain->p_virt_addr)
		return;

	dma_free_coherent(&cdev->pdev->dev, chain->page_size,
			  chain->p_virt_addr, chain->p_phys_addr);
}

static void qed_chain_free_pbl(struct qed_dev *cdev, struct qed_chain *chain)
{
	struct device *dev = &cdev->pdev->dev;
	struct addr_tbl_entry *entry;
	u32 i;

	if (!chain->pbl.pp_addr_tbl)
		return;

	for (i = 0; i < chain->page_cnt; i++) {
		entry = chain->pbl.pp_addr_tbl + i;
		if (!entry->virt_addr)
			break;

		dma_free_coherent(dev, chain->page_size, entry->virt_addr,
				  entry->dma_map);
	}

	if (!chain->b_external_pbl)
		dma_free_coherent(dev, chain->pbl_sp.table_size,
				  chain->pbl_sp.table_virt,
				  chain->pbl_sp.table_phys);

	vfree(chain->pbl.pp_addr_tbl);
	chain->pbl.pp_addr_tbl = NULL;
}

/**
 * qed_chain_free() - Free chain DMA memory.
 *
 * @cdev: Main device structure.
 * @chain: Chain to free.
 */
void qed_chain_free(struct qed_dev *cdev, struct qed_chain *chain)
{
	switch (chain->mode) {
	case QED_CHAIN_MODE_NEXT_PTR:
		qed_chain_free_next_ptr(cdev, chain);
		break;
	case QED_CHAIN_MODE_SINGLE:
		qed_chain_free_single(cdev, chain);
		break;
	case QED_CHAIN_MODE_PBL:
		qed_chain_free_pbl(cdev, chain);
		break;
	default:
		return;
	}

	qed_chain_init_mem(chain, NULL, 0);
}

static int
qed_chain_alloc_sanity_check(struct qed_dev *cdev,
			     const struct qed_chain_init_params *params,
			     u32 page_cnt)
{
	u64 chain_size;

	chain_size = ELEMS_PER_PAGE(params->elem_size, params->page_size);
	chain_size *= page_cnt;

	if (!chain_size)
		return -EINVAL;

	/* The actual chain size can be larger than the maximal possible value
	 * after rounding up the requested elements number to pages, and after
	 * taking into account the unusuable elements (next-ptr elements).
	 * The size of a "u16" chain can be (U16_MAX + 1) since the chain
	 * size/capacity fields are of u32 type.
	 */
	switch (params->cnt_type) {
	case QED_CHAIN_CNT_TYPE_U16:
		if (chain_size > U16_MAX + 1)
			break;

		return 0;
	case QED_CHAIN_CNT_TYPE_U32:
		if (chain_size > U32_MAX)
			break;

		return 0;
	default:
		return -EINVAL;
	}

	DP_NOTICE(cdev,
		  "The actual chain size (0x%llx) is larger than the maximal possible value\n",
		  chain_size);

	return -EINVAL;
}

static int qed_chain_alloc_next_ptr(struct qed_dev *cdev,
				    struct qed_chain *chain)
{
	struct device *dev = &cdev->pdev->dev;
	void *virt, *virt_prev = NULL;
	dma_addr_t phys;
	u32 i;

	for (i = 0; i < chain->page_cnt; i++) {
		virt = dma_alloc_coherent(dev, chain->page_size, &phys,
					  GFP_KERNEL);
		if (!virt)
			return -ENOMEM;

		if (i == 0) {
			qed_chain_init_mem(chain, virt, phys);
			qed_chain_reset(chain);
		} else {
			qed_chain_init_next_ptr_elem(chain, virt_prev, virt,
						     phys);
		}

		virt_prev = virt;
	}

	/* Last page's next element should point to the beginning of the
	 * chain.
	 */
	qed_chain_init_next_ptr_elem(chain, virt_prev, chain->p_virt_addr,
				     chain->p_phys_addr);

	return 0;
}

static int qed_chain_alloc_single(struct qed_dev *cdev,
				  struct qed_chain *chain)
{
	dma_addr_t phys;
	void *virt;

	virt = dma_alloc_coherent(&cdev->pdev->dev, chain->page_size,
				  &phys, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	qed_chain_init_mem(chain, virt, phys);
	qed_chain_reset(chain);

	return 0;
}

static int qed_chain_alloc_pbl(struct qed_dev *cdev, struct qed_chain *chain)
{
	struct device *dev = &cdev->pdev->dev;
	struct addr_tbl_entry *addr_tbl;
	dma_addr_t phys, pbl_phys;
	__le64 *pbl_virt;
	u32 page_cnt, i;
	size_t size;
	void *virt;

	page_cnt = chain->page_cnt;

	size = array_size(page_cnt, sizeof(*addr_tbl));
	if (unlikely(size == SIZE_MAX))
		return -EOVERFLOW;

	addr_tbl = vzalloc(size);
	if (!addr_tbl)
		return -ENOMEM;

	chain->pbl.pp_addr_tbl = addr_tbl;

	if (chain->b_external_pbl) {
		pbl_virt = chain->pbl_sp.table_virt;
		goto alloc_pages;
	}

	size = array_size(page_cnt, sizeof(*pbl_virt));
	if (unlikely(size == SIZE_MAX))
		return -EOVERFLOW;

	pbl_virt = dma_alloc_coherent(dev, size, &pbl_phys, GFP_KERNEL);
	if (!pbl_virt)
		return -ENOMEM;

	chain->pbl_sp.table_virt = pbl_virt;
	chain->pbl_sp.table_phys = pbl_phys;
	chain->pbl_sp.table_size = size;

alloc_pages:
	for (i = 0; i < page_cnt; i++) {
		virt = dma_alloc_coherent(dev, chain->page_size, &phys,
					  GFP_KERNEL);
		if (!virt)
			return -ENOMEM;

		if (i == 0) {
			qed_chain_init_mem(chain, virt, phys);
			qed_chain_reset(chain);
		}

		/* Fill the PBL table with the physical address of the page */
		pbl_virt[i] = cpu_to_le64(phys);

		/* Keep the virtual address of the page */
		addr_tbl[i].virt_addr = virt;
		addr_tbl[i].dma_map = phys;
	}

	return 0;
}

/**
 * qed_chain_alloc() - Allocate and initialize a chain.
 *
 * @cdev: Main device structure.
 * @chain: Chain to be processed.
 * @params: Chain initialization parameters.
 *
 * Return: 0 on success, negative errno otherwise.
 */
int qed_chain_alloc(struct qed_dev *cdev, struct qed_chain *chain,
		    struct qed_chain_init_params *params)
{
	u32 page_cnt;
	int rc;

	if (!params->page_size)
		params->page_size = QED_CHAIN_PAGE_SIZE;

	if (params->mode == QED_CHAIN_MODE_SINGLE)
		page_cnt = 1;
	else
		page_cnt = QED_CHAIN_PAGE_CNT(params->num_elems,
					      params->elem_size,
					      params->page_size,
					      params->mode);

	rc = qed_chain_alloc_sanity_check(cdev, params, page_cnt);
	if (rc) {
		DP_NOTICE(cdev,
			  "Cannot allocate a chain with the given arguments:\n");
		DP_NOTICE(cdev,
			  "[use_mode %d, mode %d, cnt_type %d, num_elems %d, elem_size %zu, page_size %u]\n",
			  params->intended_use, params->mode, params->cnt_type,
			  params->num_elems, params->elem_size,
			  params->page_size);
		return rc;
	}

	qed_chain_init(chain, params, page_cnt);

	switch (params->mode) {
	case QED_CHAIN_MODE_NEXT_PTR:
		rc = qed_chain_alloc_next_ptr(cdev, chain);
		break;
	case QED_CHAIN_MODE_SINGLE:
		rc = qed_chain_alloc_single(cdev, chain);
		break;
	case QED_CHAIN_MODE_PBL:
		rc = qed_chain_alloc_pbl(cdev, chain);
		break;
	default:
		return -EINVAL;
	}

	if (!rc)
		return 0;

	qed_chain_free(cdev, chain);

	return rc;
}
