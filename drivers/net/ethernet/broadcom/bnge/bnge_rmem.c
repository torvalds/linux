// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/crash_dump.h>

#include "bnge.h"
#include "../bnxt/bnxt_hsi.h"
#include "bnge_hwrm_lib.h"
#include "bnge_rmem.h"

void bnge_free_ring(struct bnge_dev *bd, struct bnge_ring_mem_info *rmem)
{
	struct pci_dev *pdev = bd->pdev;
	int i;

	if (!rmem->pg_arr)
		goto skip_pages;

	for (i = 0; i < rmem->nr_pages; i++) {
		if (!rmem->pg_arr[i])
			continue;

		dma_free_coherent(&pdev->dev, rmem->page_size,
				  rmem->pg_arr[i], rmem->dma_arr[i]);

		rmem->pg_arr[i] = NULL;
	}
skip_pages:
	if (rmem->pg_tbl) {
		size_t pg_tbl_size = rmem->nr_pages * 8;

		if (rmem->flags & BNGE_RMEM_USE_FULL_PAGE_FLAG)
			pg_tbl_size = rmem->page_size;
		dma_free_coherent(&pdev->dev, pg_tbl_size,
				  rmem->pg_tbl, rmem->dma_pg_tbl);
		rmem->pg_tbl = NULL;
	}
	if (rmem->vmem_size && *rmem->vmem) {
		vfree(*rmem->vmem);
		*rmem->vmem = NULL;
	}
}

int bnge_alloc_ring(struct bnge_dev *bd, struct bnge_ring_mem_info *rmem)
{
	struct pci_dev *pdev = bd->pdev;
	u64 valid_bit = 0;
	int i;

	if (rmem->flags & (BNGE_RMEM_VALID_PTE_FLAG | BNGE_RMEM_RING_PTE_FLAG))
		valid_bit = PTU_PTE_VALID;

	if ((rmem->nr_pages > 1 || rmem->depth > 0) && !rmem->pg_tbl) {
		size_t pg_tbl_size = rmem->nr_pages * 8;

		if (rmem->flags & BNGE_RMEM_USE_FULL_PAGE_FLAG)
			pg_tbl_size = rmem->page_size;
		rmem->pg_tbl = dma_alloc_coherent(&pdev->dev, pg_tbl_size,
						  &rmem->dma_pg_tbl,
						  GFP_KERNEL);
		if (!rmem->pg_tbl)
			return -ENOMEM;
	}

	for (i = 0; i < rmem->nr_pages; i++) {
		u64 extra_bits = valid_bit;

		rmem->pg_arr[i] = dma_alloc_coherent(&pdev->dev,
						     rmem->page_size,
						     &rmem->dma_arr[i],
						     GFP_KERNEL);
		if (!rmem->pg_arr[i])
			return -ENOMEM;

		if (rmem->nr_pages > 1 || rmem->depth > 0) {
			if (i == rmem->nr_pages - 2 &&
			    (rmem->flags & BNGE_RMEM_RING_PTE_FLAG))
				extra_bits |= PTU_PTE_NEXT_TO_LAST;
			else if (i == rmem->nr_pages - 1 &&
				 (rmem->flags & BNGE_RMEM_RING_PTE_FLAG))
				extra_bits |= PTU_PTE_LAST;
			rmem->pg_tbl[i] =
				cpu_to_le64(rmem->dma_arr[i] | extra_bits);
		}
	}

	if (rmem->vmem_size) {
		*rmem->vmem = vzalloc(rmem->vmem_size);
		if (!(*rmem->vmem))
			return -ENOMEM;
	}

	return 0;
}
