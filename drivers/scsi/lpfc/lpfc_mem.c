/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2006 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 * Portions Copyright (C) 2004-2005 Christoph Hellwig              *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#include <linux/mempool.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include <scsi/scsi_device.h>
#include <scsi/scsi_transport_fc.h>

#include <scsi/scsi.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_crtn.h"

#define LPFC_MBUF_POOL_SIZE     64      /* max elements in MBUF safety pool */
#define LPFC_MEM_POOL_SIZE      64      /* max elem in non-DMA safety pool */



int
lpfc_mem_alloc(struct lpfc_hba * phba)
{
	struct lpfc_dma_pool *pool = &phba->lpfc_mbuf_safety_pool;
	int longs;
	int i;

	phba->lpfc_scsi_dma_buf_pool = pci_pool_create("lpfc_scsi_dma_buf_pool",
				phba->pcidev, phba->cfg_sg_dma_buf_size, 8, 0);
	if (!phba->lpfc_scsi_dma_buf_pool)
		goto fail;

	phba->lpfc_mbuf_pool = pci_pool_create("lpfc_mbuf_pool", phba->pcidev,
							LPFC_BPL_SIZE, 8,0);
	if (!phba->lpfc_mbuf_pool)
		goto fail_free_dma_buf_pool;

	pool->elements = kmalloc(sizeof(struct lpfc_dmabuf) *
					 LPFC_MBUF_POOL_SIZE, GFP_KERNEL);
	if (!pool->elements)
		goto fail_free_lpfc_mbuf_pool;

	pool->max_count = 0;
	pool->current_count = 0;
	for ( i = 0; i < LPFC_MBUF_POOL_SIZE; i++) {
		pool->elements[i].virt = pci_pool_alloc(phba->lpfc_mbuf_pool,
				       GFP_KERNEL, &pool->elements[i].phys);
		if (!pool->elements[i].virt)
			goto fail_free_mbuf_pool;
		pool->max_count++;
		pool->current_count++;
	}

	phba->mbox_mem_pool = mempool_create_kmalloc_pool(LPFC_MEM_POOL_SIZE,
							 sizeof(LPFC_MBOXQ_t));
	if (!phba->mbox_mem_pool)
		goto fail_free_mbuf_pool;

	phba->nlp_mem_pool = mempool_create_kmalloc_pool(LPFC_MEM_POOL_SIZE,
						sizeof(struct lpfc_nodelist));
	if (!phba->nlp_mem_pool)
		goto fail_free_mbox_pool;

	phba->lpfc_hbq_pool = pci_pool_create("lpfc_hbq_pool",phba->pcidev,
					      LPFC_BPL_SIZE, 8, 0);
	if (!phba->lpfc_hbq_pool)
		goto fail_free_nlp_mem_pool;

	/* vpi zero is reserved for the physical port so add 1 to max */
	longs = ((phba->max_vpi + 1) + BITS_PER_LONG - 1) / BITS_PER_LONG;
	phba->vpi_bmask = kzalloc(longs * sizeof(unsigned long), GFP_KERNEL);
	if (!phba->vpi_bmask)
		goto fail_free_hbq_pool;

	return 0;

 fail_free_hbq_pool:
	lpfc_sli_hbqbuf_free_all(phba);
	pci_pool_destroy(phba->lpfc_hbq_pool);
 fail_free_nlp_mem_pool:
	mempool_destroy(phba->nlp_mem_pool);
	phba->nlp_mem_pool = NULL;
 fail_free_mbox_pool:
	mempool_destroy(phba->mbox_mem_pool);
	phba->mbox_mem_pool = NULL;
 fail_free_mbuf_pool:
	while (i--)
		pci_pool_free(phba->lpfc_mbuf_pool, pool->elements[i].virt,
						 pool->elements[i].phys);
	kfree(pool->elements);
 fail_free_lpfc_mbuf_pool:
	pci_pool_destroy(phba->lpfc_mbuf_pool);
	phba->lpfc_mbuf_pool = NULL;
 fail_free_dma_buf_pool:
	pci_pool_destroy(phba->lpfc_scsi_dma_buf_pool);
	phba->lpfc_scsi_dma_buf_pool = NULL;
 fail:
	return -ENOMEM;
}

void
lpfc_mem_free(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_dma_pool *pool = &phba->lpfc_mbuf_safety_pool;
	LPFC_MBOXQ_t *mbox, *next_mbox;
	struct lpfc_dmabuf   *mp;
	int i;

	kfree(phba->vpi_bmask);
	lpfc_sli_hbqbuf_free_all(phba);

	list_for_each_entry_safe(mbox, next_mbox, &psli->mboxq, list) {
		mp = (struct lpfc_dmabuf *) (mbox->context1);
		if (mp) {
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
		}
		list_del(&mbox->list);
		mempool_free(mbox, phba->mbox_mem_pool);
	}
	list_for_each_entry_safe(mbox, next_mbox, &psli->mboxq_cmpl, list) {
		mp = (struct lpfc_dmabuf *) (mbox->context1);
		if (mp) {
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
		}
		list_del(&mbox->list);
		mempool_free(mbox, phba->mbox_mem_pool);
	}

	psli->sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
	if (psli->mbox_active) {
		mbox = psli->mbox_active;
		mp = (struct lpfc_dmabuf *) (mbox->context1);
		if (mp) {
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
		}
		mempool_free(mbox, phba->mbox_mem_pool);
		psli->mbox_active = NULL;
	}

	for (i = 0; i < pool->current_count; i++)
		pci_pool_free(phba->lpfc_mbuf_pool, pool->elements[i].virt,
						 pool->elements[i].phys);
	kfree(pool->elements);

	pci_pool_destroy(phba->lpfc_hbq_pool);
	mempool_destroy(phba->nlp_mem_pool);
	mempool_destroy(phba->mbox_mem_pool);

	pci_pool_destroy(phba->lpfc_scsi_dma_buf_pool);
	pci_pool_destroy(phba->lpfc_mbuf_pool);

	phba->lpfc_hbq_pool = NULL;
	phba->nlp_mem_pool = NULL;
	phba->mbox_mem_pool = NULL;
	phba->lpfc_scsi_dma_buf_pool = NULL;
	phba->lpfc_mbuf_pool = NULL;

				/* Free the iocb lookup array */
	kfree(psli->iocbq_lookup);
	psli->iocbq_lookup = NULL;

}

void *
lpfc_mbuf_alloc(struct lpfc_hba *phba, int mem_flags, dma_addr_t *handle)
{
	struct lpfc_dma_pool *pool = &phba->lpfc_mbuf_safety_pool;
	unsigned long iflags;
	void *ret;

	ret = pci_pool_alloc(phba->lpfc_mbuf_pool, GFP_KERNEL, handle);

	spin_lock_irqsave(&phba->hbalock, iflags);
	if (!ret && (mem_flags & MEM_PRI) && pool->current_count) {
		pool->current_count--;
		ret = pool->elements[pool->current_count].virt;
		*handle = pool->elements[pool->current_count].phys;
	}
	spin_unlock_irqrestore(&phba->hbalock, iflags);
	return ret;
}

void
__lpfc_mbuf_free(struct lpfc_hba * phba, void *virt, dma_addr_t dma)
{
	struct lpfc_dma_pool *pool = &phba->lpfc_mbuf_safety_pool;

	if (pool->current_count < pool->max_count) {
		pool->elements[pool->current_count].virt = virt;
		pool->elements[pool->current_count].phys = dma;
		pool->current_count++;
	} else {
		pci_pool_free(phba->lpfc_mbuf_pool, virt, dma);
	}
	return;
}

void
lpfc_mbuf_free(struct lpfc_hba * phba, void *virt, dma_addr_t dma)
{
	unsigned long iflags;

	spin_lock_irqsave(&phba->hbalock, iflags);
	__lpfc_mbuf_free(phba, virt, dma);
	spin_unlock_irqrestore(&phba->hbalock, iflags);
	return;
}

struct hbq_dmabuf *
lpfc_els_hbq_alloc(struct lpfc_hba *phba)
{
	struct hbq_dmabuf *hbqbp;

	hbqbp = kmalloc(sizeof(struct hbq_dmabuf), GFP_KERNEL);
	if (!hbqbp)
		return NULL;

	hbqbp->dbuf.virt = pci_pool_alloc(phba->lpfc_hbq_pool, GFP_KERNEL,
					  &hbqbp->dbuf.phys);
	if (!hbqbp->dbuf.virt) {
		kfree(hbqbp);
		return NULL;
	}
	hbqbp->size = LPFC_BPL_SIZE;
	return hbqbp;
}

void
lpfc_els_hbq_free(struct lpfc_hba *phba, struct hbq_dmabuf *hbqbp)
{
	pci_pool_free(phba->lpfc_hbq_pool, hbqbp->dbuf.virt, hbqbp->dbuf.phys);
	kfree(hbqbp);
	return;
}

/* This is ONLY called for the LPFC_ELS_HBQ */
void
lpfc_in_buf_free(struct lpfc_hba *phba, struct lpfc_dmabuf *mp)
{
	struct hbq_dmabuf *hbq_entry;
	unsigned long flags;

	if (!mp)
		return;

	if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) {
		/* Check whether HBQ is still in use */
		spin_lock_irqsave(&phba->hbalock, flags);
		if (!phba->hbq_in_use) {
			spin_unlock_irqrestore(&phba->hbalock, flags);
			return;
		}
		hbq_entry = container_of(mp, struct hbq_dmabuf, dbuf);
		list_del(&hbq_entry->dbuf.list);
		if (hbq_entry->tag == -1) {
			(phba->hbqs[LPFC_ELS_HBQ].hbq_free_buffer)
				(phba, hbq_entry);
		} else {
			lpfc_sli_free_hbq(phba, hbq_entry);
		}
		spin_unlock_irqrestore(&phba->hbalock, flags);
	} else {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
	}
	return;
}
