// SPDX-License-Identifier: GPL-2.0
#include <linux/cpumask.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci_regs.h>
#include <linux/vmalloc.h>
#include <linux/pci.h>

#include "nitrox_dev.h"
#include "nitrox_common.h"
#include "nitrox_req.h"
#include "nitrox_csr.h"

#define CRYPTO_CTX_SIZE	256

/* command queue alignments */
#define PKT_IN_ALIGN	16

static int cmdq_common_init(struct nitrox_cmdq *cmdq)
{
	struct nitrox_device *ndev = cmdq->ndev;
	u32 qsize;

	qsize = (ndev->qlen) * cmdq->instr_size;
	cmdq->head_unaligned = dma_zalloc_coherent(DEV(ndev),
						   (qsize + PKT_IN_ALIGN),
						   &cmdq->dma_unaligned,
						   GFP_KERNEL);
	if (!cmdq->head_unaligned)
		return -ENOMEM;

	cmdq->head = PTR_ALIGN(cmdq->head_unaligned, PKT_IN_ALIGN);
	cmdq->dma = PTR_ALIGN(cmdq->dma_unaligned, PKT_IN_ALIGN);
	cmdq->qsize = (qsize + PKT_IN_ALIGN);
	cmdq->write_idx = 0;

	spin_lock_init(&cmdq->response_lock);
	spin_lock_init(&cmdq->cmdq_lock);
	spin_lock_init(&cmdq->backlog_lock);

	INIT_LIST_HEAD(&cmdq->response_head);
	INIT_LIST_HEAD(&cmdq->backlog_head);
	INIT_WORK(&cmdq->backlog_qflush, backlog_qflush_work);

	atomic_set(&cmdq->pending_count, 0);
	atomic_set(&cmdq->backlog_count, 0);
	return 0;
}

static void cmdq_common_cleanup(struct nitrox_cmdq *cmdq)
{
	struct nitrox_device *ndev = cmdq->ndev;

	cancel_work_sync(&cmdq->backlog_qflush);

	dma_free_coherent(DEV(ndev), cmdq->qsize,
			  cmdq->head_unaligned, cmdq->dma_unaligned);

	atomic_set(&cmdq->pending_count, 0);
	atomic_set(&cmdq->backlog_count, 0);

	cmdq->dbell_csr_addr = NULL;
	cmdq->head = NULL;
	cmdq->dma = 0;
	cmdq->qsize = 0;
	cmdq->instr_size = 0;
}

static void nitrox_cleanup_pkt_cmdqs(struct nitrox_device *ndev)
{
	int i;

	for (i = 0; i < ndev->nr_queues; i++) {
		struct nitrox_cmdq *cmdq = &ndev->pkt_cmdqs[i];

		cmdq_common_cleanup(cmdq);
	}
	kfree(ndev->pkt_cmdqs);
	ndev->pkt_cmdqs = NULL;
}

static int nitrox_init_pkt_cmdqs(struct nitrox_device *ndev)
{
	int i, err, size;

	size = ndev->nr_queues * sizeof(struct nitrox_cmdq);
	ndev->pkt_cmdqs = kzalloc(size, GFP_KERNEL);
	if (!ndev->pkt_cmdqs)
		return -ENOMEM;

	for (i = 0; i < ndev->nr_queues; i++) {
		struct nitrox_cmdq *cmdq;
		u64 offset;

		cmdq = &ndev->pkt_cmdqs[i];
		cmdq->ndev = ndev;
		cmdq->qno = i;
		cmdq->instr_size = sizeof(struct nps_pkt_instr);

		offset = NPS_PKT_IN_INSTR_BAOFF_DBELLX(i);
		/* SE ring doorbell address for this queue */
		cmdq->dbell_csr_addr = NITROX_CSR_ADDR(ndev, offset);

		err = cmdq_common_init(cmdq);
		if (err)
			goto pkt_cmdq_fail;
	}
	return 0;

pkt_cmdq_fail:
	nitrox_cleanup_pkt_cmdqs(ndev);
	return err;
}

static int create_crypto_dma_pool(struct nitrox_device *ndev)
{
	size_t size;

	/* Crypto context pool, 16 byte aligned */
	size = CRYPTO_CTX_SIZE + sizeof(struct ctx_hdr);
	ndev->ctx_pool = dma_pool_create("crypto-context",
					 DEV(ndev), size, 16, 0);
	if (!ndev->ctx_pool)
		return -ENOMEM;

	return 0;
}

static void destroy_crypto_dma_pool(struct nitrox_device *ndev)
{
	if (!ndev->ctx_pool)
		return;

	dma_pool_destroy(ndev->ctx_pool);
	ndev->ctx_pool = NULL;
}

/*
 * crypto_alloc_context - Allocate crypto context from pool
 * @ndev: NITROX Device
 */
void *crypto_alloc_context(struct nitrox_device *ndev)
{
	struct ctx_hdr *ctx;
	struct crypto_ctx_hdr *chdr;
	void *vaddr;
	dma_addr_t dma;

	chdr = kmalloc(sizeof(*chdr), GFP_KERNEL);
	if (!chdr)
		return NULL;

	vaddr = dma_pool_alloc(ndev->ctx_pool, (GFP_KERNEL | __GFP_ZERO), &dma);
	if (!vaddr) {
		kfree(chdr);
		return NULL;
	}

	/* fill meta data */
	ctx = vaddr;
	ctx->pool = ndev->ctx_pool;
	ctx->dma = dma;
	ctx->ctx_dma = dma + sizeof(struct ctx_hdr);

	chdr->pool = ndev->ctx_pool;
	chdr->dma = dma;
	chdr->vaddr = vaddr;

	return chdr;
}

/**
 * crypto_free_context - Free crypto context to pool
 * @ctx: context to free
 */
void crypto_free_context(void *ctx)
{
	struct crypto_ctx_hdr *ctxp;

	if (!ctx)
		return;

	ctxp = ctx;
	dma_pool_free(ctxp->pool, ctxp->vaddr, ctxp->dma);
	kfree(ctxp);
}

/**
 * nitrox_common_sw_init - allocate software resources.
 * @ndev: NITROX device
 *
 * Allocates crypto context pools and command queues etc.
 *
 * Return: 0 on success, or a negative error code on error.
 */
int nitrox_common_sw_init(struct nitrox_device *ndev)
{
	int err = 0;

	/* per device crypto context pool */
	err = create_crypto_dma_pool(ndev);
	if (err)
		return err;

	err = nitrox_init_pkt_cmdqs(ndev);
	if (err)
		destroy_crypto_dma_pool(ndev);

	return err;
}

/**
 * nitrox_common_sw_cleanup - free software resources.
 * @ndev: NITROX device
 */
void nitrox_common_sw_cleanup(struct nitrox_device *ndev)
{
	nitrox_cleanup_pkt_cmdqs(ndev);
	destroy_crypto_dma_pool(ndev);
}
