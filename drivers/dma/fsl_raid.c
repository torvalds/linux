/*
 * drivers/dma/fsl_raid.c
 *
 * Freescale RAID Engine device driver
 *
 * Author:
 *	Harninder Rai <harninder.rai@freescale.com>
 *	Naveen Burmi <naveenburmi@freescale.com>
 *
 * Rewrite:
 *	Xuelin Shi <xuelin.shi@freescale.com>
 *
 * Copyright (c) 2010-2014 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Theory of operation:
 *
 * General capabilities:
 *	RAID Engine (RE) block is capable of offloading XOR, memcpy and P/Q
 *	calculations required in RAID5 and RAID6 operations. RE driver
 *	registers with Linux's ASYNC layer as dma driver. RE hardware
 *	maintains strict ordering of the requests through chained
 *	command queueing.
 *
 * Data flow:
 *	Software RAID layer of Linux (MD layer) maintains RAID partitions,
 *	strips, stripes etc. It sends requests to the underlying ASYNC layer
 *	which further passes it to RE driver. ASYNC layer decides which request
 *	goes to which job ring of RE hardware. For every request processed by
 *	RAID Engine, driver gets an interrupt unless coalescing is set. The
 *	per job ring interrupt handler checks the status register for errors,
 *	clears the interrupt and leave the post interrupt processing to the irq
 *	thread.
 */
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/dmaengine.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#include "dmaengine.h"
#include "fsl_raid.h"

#define FSL_RE_MAX_XOR_SRCS	16
#define FSL_RE_MAX_PQ_SRCS	16
#define FSL_RE_MIN_DESCS	256
#define FSL_RE_MAX_DESCS	(4 * FSL_RE_MIN_DESCS)
#define FSL_RE_FRAME_FORMAT	0x1
#define FSL_RE_MAX_DATA_LEN	(1024*1024)

#define to_fsl_re_dma_desc(tx) container_of(tx, struct fsl_re_desc, async_tx)

/* Add descriptors into per chan software queue - submit_q */
static dma_cookie_t fsl_re_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct fsl_re_desc *desc;
	struct fsl_re_chan *re_chan;
	dma_cookie_t cookie;
	unsigned long flags;

	desc = to_fsl_re_dma_desc(tx);
	re_chan = container_of(tx->chan, struct fsl_re_chan, chan);

	spin_lock_irqsave(&re_chan->desc_lock, flags);
	cookie = dma_cookie_assign(tx);
	list_add_tail(&desc->node, &re_chan->submit_q);
	spin_unlock_irqrestore(&re_chan->desc_lock, flags);

	return cookie;
}

/* Copy descriptor from per chan software queue into hardware job ring */
static void fsl_re_issue_pending(struct dma_chan *chan)
{
	struct fsl_re_chan *re_chan;
	int avail;
	struct fsl_re_desc *desc, *_desc;
	unsigned long flags;

	re_chan = container_of(chan, struct fsl_re_chan, chan);

	spin_lock_irqsave(&re_chan->desc_lock, flags);
	avail = FSL_RE_SLOT_AVAIL(
		in_be32(&re_chan->jrregs->inbring_slot_avail));

	list_for_each_entry_safe(desc, _desc, &re_chan->submit_q, node) {
		if (!avail)
			break;

		list_move_tail(&desc->node, &re_chan->active_q);

		memcpy(&re_chan->inb_ring_virt_addr[re_chan->inb_count],
		       &desc->hwdesc, sizeof(struct fsl_re_hw_desc));

		re_chan->inb_count = (re_chan->inb_count + 1) &
						FSL_RE_RING_SIZE_MASK;
		out_be32(&re_chan->jrregs->inbring_add_job, FSL_RE_ADD_JOB(1));
		avail--;
	}
	spin_unlock_irqrestore(&re_chan->desc_lock, flags);
}

static void fsl_re_desc_done(struct fsl_re_desc *desc)
{
	dma_async_tx_callback callback;
	void *callback_param;

	dma_cookie_complete(&desc->async_tx);

	callback = desc->async_tx.callback;
	callback_param = desc->async_tx.callback_param;
	if (callback)
		callback(callback_param);

	dma_descriptor_unmap(&desc->async_tx);
}

static void fsl_re_cleanup_descs(struct fsl_re_chan *re_chan)
{
	struct fsl_re_desc *desc, *_desc;
	unsigned long flags;

	spin_lock_irqsave(&re_chan->desc_lock, flags);
	list_for_each_entry_safe(desc, _desc, &re_chan->ack_q, node) {
		if (async_tx_test_ack(&desc->async_tx))
			list_move_tail(&desc->node, &re_chan->free_q);
	}
	spin_unlock_irqrestore(&re_chan->desc_lock, flags);

	fsl_re_issue_pending(&re_chan->chan);
}

static void fsl_re_dequeue(unsigned long data)
{
	struct fsl_re_chan *re_chan;
	struct fsl_re_desc *desc, *_desc;
	struct fsl_re_hw_desc *hwdesc;
	unsigned long flags;
	unsigned int count, oub_count;
	int found;

	re_chan = dev_get_drvdata((struct device *)data);

	fsl_re_cleanup_descs(re_chan);

	spin_lock_irqsave(&re_chan->desc_lock, flags);
	count =	FSL_RE_SLOT_FULL(in_be32(&re_chan->jrregs->oubring_slot_full));
	while (count--) {
		found = 0;
		hwdesc = &re_chan->oub_ring_virt_addr[re_chan->oub_count];
		list_for_each_entry_safe(desc, _desc, &re_chan->active_q,
					 node) {
			/* compare the hw dma addr to find the completed */
			if (desc->hwdesc.lbea32 == hwdesc->lbea32 &&
			    desc->hwdesc.addr_low == hwdesc->addr_low) {
				found = 1;
				break;
			}
		}

		if (found) {
			fsl_re_desc_done(desc);
			list_move_tail(&desc->node, &re_chan->ack_q);
		} else {
			dev_err(re_chan->dev,
				"found hwdesc not in sw queue, discard it\n");
		}

		oub_count = (re_chan->oub_count + 1) & FSL_RE_RING_SIZE_MASK;
		re_chan->oub_count = oub_count;

		out_be32(&re_chan->jrregs->oubring_job_rmvd,
			 FSL_RE_RMVD_JOB(1));
	}
	spin_unlock_irqrestore(&re_chan->desc_lock, flags);
}

/* Per Job Ring interrupt handler */
static irqreturn_t fsl_re_isr(int irq, void *data)
{
	struct fsl_re_chan *re_chan;
	u32 irqstate, status;

	re_chan = dev_get_drvdata((struct device *)data);

	irqstate = in_be32(&re_chan->jrregs->jr_interrupt_status);
	if (!irqstate)
		return IRQ_NONE;

	/*
	 * There's no way in upper layer (read MD layer) to recover from
	 * error conditions except restart everything. In long term we
	 * need to do something more than just crashing
	 */
	if (irqstate & FSL_RE_ERROR) {
		status = in_be32(&re_chan->jrregs->jr_status);
		dev_err(re_chan->dev, "chan error irqstate: %x, status: %x\n",
			irqstate, status);
	}

	/* Clear interrupt */
	out_be32(&re_chan->jrregs->jr_interrupt_status, FSL_RE_CLR_INTR);

	tasklet_schedule(&re_chan->irqtask);

	return IRQ_HANDLED;
}

static enum dma_status fsl_re_tx_status(struct dma_chan *chan,
					dma_cookie_t cookie,
					struct dma_tx_state *txstate)
{
	return dma_cookie_status(chan, cookie, txstate);
}

static void fill_cfd_frame(struct fsl_re_cmpnd_frame *cf, u8 index,
			   size_t length, dma_addr_t addr, bool final)
{
	u32 efrl = length & FSL_RE_CF_LENGTH_MASK;

	efrl |= final << FSL_RE_CF_FINAL_SHIFT;
	cf[index].efrl32 = efrl;
	cf[index].addr_high = upper_32_bits(addr);
	cf[index].addr_low = lower_32_bits(addr);
}

static struct fsl_re_desc *fsl_re_init_desc(struct fsl_re_chan *re_chan,
					    struct fsl_re_desc *desc,
					    void *cf, dma_addr_t paddr)
{
	desc->re_chan = re_chan;
	desc->async_tx.tx_submit = fsl_re_tx_submit;
	dma_async_tx_descriptor_init(&desc->async_tx, &re_chan->chan);
	INIT_LIST_HEAD(&desc->node);

	desc->hwdesc.fmt32 = FSL_RE_FRAME_FORMAT << FSL_RE_HWDESC_FMT_SHIFT;
	desc->hwdesc.lbea32 = upper_32_bits(paddr);
	desc->hwdesc.addr_low = lower_32_bits(paddr);
	desc->cf_addr = cf;
	desc->cf_paddr = paddr;

	desc->cdb_addr = (void *)(cf + FSL_RE_CF_DESC_SIZE);
	desc->cdb_paddr = paddr + FSL_RE_CF_DESC_SIZE;

	return desc;
}

static struct fsl_re_desc *fsl_re_chan_alloc_desc(struct fsl_re_chan *re_chan,
						  unsigned long flags)
{
	struct fsl_re_desc *desc = NULL;
	void *cf;
	dma_addr_t paddr;
	unsigned long lock_flag;

	fsl_re_cleanup_descs(re_chan);

	spin_lock_irqsave(&re_chan->desc_lock, lock_flag);
	if (!list_empty(&re_chan->free_q)) {
		/* take one desc from free_q */
		desc = list_first_entry(&re_chan->free_q,
					struct fsl_re_desc, node);
		list_del(&desc->node);

		desc->async_tx.flags = flags;
	}
	spin_unlock_irqrestore(&re_chan->desc_lock, lock_flag);

	if (!desc) {
		desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
		if (!desc)
			return NULL;

		cf = dma_pool_alloc(re_chan->re_dev->cf_desc_pool, GFP_NOWAIT,
				    &paddr);
		if (!cf) {
			kfree(desc);
			return NULL;
		}

		desc = fsl_re_init_desc(re_chan, desc, cf, paddr);
		desc->async_tx.flags = flags;

		spin_lock_irqsave(&re_chan->desc_lock, lock_flag);
		re_chan->alloc_count++;
		spin_unlock_irqrestore(&re_chan->desc_lock, lock_flag);
	}

	return desc;
}

static struct dma_async_tx_descriptor *fsl_re_prep_dma_genq(
		struct dma_chan *chan, dma_addr_t dest, dma_addr_t *src,
		unsigned int src_cnt, const unsigned char *scf, size_t len,
		unsigned long flags)
{
	struct fsl_re_chan *re_chan;
	struct fsl_re_desc *desc;
	struct fsl_re_xor_cdb *xor;
	struct fsl_re_cmpnd_frame *cf;
	u32 cdb;
	unsigned int i, j;
	unsigned int save_src_cnt = src_cnt;
	int cont_q = 0;

	re_chan = container_of(chan, struct fsl_re_chan, chan);
	if (len > FSL_RE_MAX_DATA_LEN) {
		dev_err(re_chan->dev, "genq tx length %zu, max length %d\n",
			len, FSL_RE_MAX_DATA_LEN);
		return NULL;
	}

	desc = fsl_re_chan_alloc_desc(re_chan, flags);
	if (desc <= 0)
		return NULL;

	if (scf && (flags & DMA_PREP_CONTINUE)) {
		cont_q = 1;
		src_cnt += 1;
	}

	/* Filling xor CDB */
	cdb = FSL_RE_XOR_OPCODE << FSL_RE_CDB_OPCODE_SHIFT;
	cdb |= (src_cnt - 1) << FSL_RE_CDB_NRCS_SHIFT;
	cdb |= FSL_RE_BLOCK_SIZE << FSL_RE_CDB_BLKSIZE_SHIFT;
	cdb |= FSL_RE_INTR_ON_ERROR << FSL_RE_CDB_ERROR_SHIFT;
	cdb |= FSL_RE_DATA_DEP << FSL_RE_CDB_DEPEND_SHIFT;
	xor = desc->cdb_addr;
	xor->cdb32 = cdb;

	if (scf) {
		/* compute q = src0*coef0^src1*coef1^..., * is GF(8) mult */
		for (i = 0; i < save_src_cnt; i++)
			xor->gfm[i] = scf[i];
		if (cont_q)
			xor->gfm[i++] = 1;
	} else {
		/* compute P, that is XOR all srcs */
		for (i = 0; i < src_cnt; i++)
			xor->gfm[i] = 1;
	}

	/* Filling frame 0 of compound frame descriptor with CDB */
	cf = desc->cf_addr;
	fill_cfd_frame(cf, 0, sizeof(*xor), desc->cdb_paddr, 0);

	/* Fill CFD's 1st frame with dest buffer */
	fill_cfd_frame(cf, 1, len, dest, 0);

	/* Fill CFD's rest of the frames with source buffers */
	for (i = 2, j = 0; j < save_src_cnt; i++, j++)
		fill_cfd_frame(cf, i, len, src[j], 0);

	if (cont_q)
		fill_cfd_frame(cf, i++, len, dest, 0);

	/* Setting the final bit in the last source buffer frame in CFD */
	cf[i - 1].efrl32 |= 1 << FSL_RE_CF_FINAL_SHIFT;

	return &desc->async_tx;
}

/*
 * Prep function for P parity calculation.In RAID Engine terminology,
 * XOR calculation is called GenQ calculation done through GenQ command
 */
static struct dma_async_tx_descriptor *fsl_re_prep_dma_xor(
		struct dma_chan *chan, dma_addr_t dest, dma_addr_t *src,
		unsigned int src_cnt, size_t len, unsigned long flags)
{
	/* NULL let genq take all coef as 1 */
	return fsl_re_prep_dma_genq(chan, dest, src, src_cnt, NULL, len, flags);
}

/*
 * Prep function for P/Q parity calculation.In RAID Engine terminology,
 * P/Q calculation is called GenQQ done through GenQQ command
 */
static struct dma_async_tx_descriptor *fsl_re_prep_dma_pq(
		struct dma_chan *chan, dma_addr_t *dest, dma_addr_t *src,
		unsigned int src_cnt, const unsigned char *scf, size_t len,
		unsigned long flags)
{
	struct fsl_re_chan *re_chan;
	struct fsl_re_desc *desc;
	struct fsl_re_pq_cdb *pq;
	struct fsl_re_cmpnd_frame *cf;
	u32 cdb;
	u8 *p;
	int gfmq_len, i, j;
	unsigned int save_src_cnt = src_cnt;

	re_chan = container_of(chan, struct fsl_re_chan, chan);
	if (len > FSL_RE_MAX_DATA_LEN) {
		dev_err(re_chan->dev, "pq tx length is %zu, max length is %d\n",
			len, FSL_RE_MAX_DATA_LEN);
		return NULL;
	}

	/*
	 * RE requires at least 2 sources, if given only one source, we pass the
	 * second source same as the first one.
	 * With only one source, generating P is meaningless, only generate Q.
	 */
	if (src_cnt == 1) {
		struct dma_async_tx_descriptor *tx;
		dma_addr_t dma_src[2];
		unsigned char coef[2];

		dma_src[0] = *src;
		coef[0] = *scf;
		dma_src[1] = *src;
		coef[1] = 0;
		tx = fsl_re_prep_dma_genq(chan, dest[1], dma_src, 2, coef, len,
					  flags);
		if (tx)
			desc = to_fsl_re_dma_desc(tx);

		return tx;
	}

	/*
	 * During RAID6 array creation, Linux's MD layer gets P and Q
	 * calculated separately in two steps. But our RAID Engine has
	 * the capability to calculate both P and Q with a single command
	 * Hence to merge well with MD layer, we need to provide a hook
	 * here and call re_jq_prep_dma_genq() function
	 */

	if (flags & DMA_PREP_PQ_DISABLE_P)
		return fsl_re_prep_dma_genq(chan, dest[1], src, src_cnt,
				scf, len, flags);

	if (flags & DMA_PREP_CONTINUE)
		src_cnt += 3;

	desc = fsl_re_chan_alloc_desc(re_chan, flags);
	if (desc <= 0)
		return NULL;

	/* Filling GenQQ CDB */
	cdb = FSL_RE_PQ_OPCODE << FSL_RE_CDB_OPCODE_SHIFT;
	cdb |= (src_cnt - 1) << FSL_RE_CDB_NRCS_SHIFT;
	cdb |= FSL_RE_BLOCK_SIZE << FSL_RE_CDB_BLKSIZE_SHIFT;
	cdb |= FSL_RE_BUFFER_OUTPUT << FSL_RE_CDB_BUFFER_SHIFT;
	cdb |= FSL_RE_DATA_DEP << FSL_RE_CDB_DEPEND_SHIFT;

	pq = desc->cdb_addr;
	pq->cdb32 = cdb;

	p = pq->gfm_q1;
	/* Init gfm_q1[] */
	for (i = 0; i < src_cnt; i++)
		p[i] = 1;

	/* Align gfm[] to 32bit */
	gfmq_len = ALIGN(src_cnt, 4);

	/* Init gfm_q2[] */
	p += gfmq_len;
	for (i = 0; i < src_cnt; i++)
		p[i] = scf[i];

	/* Filling frame 0 of compound frame descriptor with CDB */
	cf = desc->cf_addr;
	fill_cfd_frame(cf, 0, sizeof(struct fsl_re_pq_cdb), desc->cdb_paddr, 0);

	/* Fill CFD's 1st & 2nd frame with dest buffers */
	for (i = 1, j = 0; i < 3; i++, j++)
		fill_cfd_frame(cf, i, len, dest[j], 0);

	/* Fill CFD's rest of the frames with source buffers */
	for (i = 3, j = 0; j < save_src_cnt; i++, j++)
		fill_cfd_frame(cf, i, len, src[j], 0);

	/* PQ computation continuation */
	if (flags & DMA_PREP_CONTINUE) {
		if (src_cnt - save_src_cnt == 3) {
			p[save_src_cnt] = 0;
			p[save_src_cnt + 1] = 0;
			p[save_src_cnt + 2] = 1;
			fill_cfd_frame(cf, i++, len, dest[0], 0);
			fill_cfd_frame(cf, i++, len, dest[1], 0);
			fill_cfd_frame(cf, i++, len, dest[1], 0);
		} else {
			dev_err(re_chan->dev, "PQ tx continuation error!\n");
			return NULL;
		}
	}

	/* Setting the final bit in the last source buffer frame in CFD */
	cf[i - 1].efrl32 |= 1 << FSL_RE_CF_FINAL_SHIFT;

	return &desc->async_tx;
}

/*
 * Prep function for memcpy. In RAID Engine, memcpy is done through MOVE
 * command. Logic of this function will need to be modified once multipage
 * support is added in Linux's MD/ASYNC Layer
 */
static struct dma_async_tx_descriptor *fsl_re_prep_dma_memcpy(
		struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct fsl_re_chan *re_chan;
	struct fsl_re_desc *desc;
	size_t length;
	struct fsl_re_cmpnd_frame *cf;
	struct fsl_re_move_cdb *move;
	u32 cdb;

	re_chan = container_of(chan, struct fsl_re_chan, chan);

	if (len > FSL_RE_MAX_DATA_LEN) {
		dev_err(re_chan->dev, "cp tx length is %zu, max length is %d\n",
			len, FSL_RE_MAX_DATA_LEN);
		return NULL;
	}

	desc = fsl_re_chan_alloc_desc(re_chan, flags);
	if (desc <= 0)
		return NULL;

	/* Filling move CDB */
	cdb = FSL_RE_MOVE_OPCODE << FSL_RE_CDB_OPCODE_SHIFT;
	cdb |= FSL_RE_BLOCK_SIZE << FSL_RE_CDB_BLKSIZE_SHIFT;
	cdb |= FSL_RE_INTR_ON_ERROR << FSL_RE_CDB_ERROR_SHIFT;
	cdb |= FSL_RE_DATA_DEP << FSL_RE_CDB_DEPEND_SHIFT;

	move = desc->cdb_addr;
	move->cdb32 = cdb;

	/* Filling frame 0 of CFD with move CDB */
	cf = desc->cf_addr;
	fill_cfd_frame(cf, 0, sizeof(*move), desc->cdb_paddr, 0);

	length = min_t(size_t, len, FSL_RE_MAX_DATA_LEN);

	/* Fill CFD's 1st frame with dest buffer */
	fill_cfd_frame(cf, 1, length, dest, 0);

	/* Fill CFD's 2nd frame with src buffer */
	fill_cfd_frame(cf, 2, length, src, 1);

	return &desc->async_tx;
}

static int fsl_re_alloc_chan_resources(struct dma_chan *chan)
{
	struct fsl_re_chan *re_chan;
	struct fsl_re_desc *desc;
	void *cf;
	dma_addr_t paddr;
	int i;

	re_chan = container_of(chan, struct fsl_re_chan, chan);
	for (i = 0; i < FSL_RE_MIN_DESCS; i++) {
		desc = kzalloc(sizeof(*desc), GFP_KERNEL);
		if (!desc)
			break;

		cf = dma_pool_alloc(re_chan->re_dev->cf_desc_pool, GFP_KERNEL,
				    &paddr);
		if (!cf) {
			kfree(desc);
			break;
		}

		INIT_LIST_HEAD(&desc->node);
		fsl_re_init_desc(re_chan, desc, cf, paddr);

		list_add_tail(&desc->node, &re_chan->free_q);
		re_chan->alloc_count++;
	}
	return re_chan->alloc_count;
}

static void fsl_re_free_chan_resources(struct dma_chan *chan)
{
	struct fsl_re_chan *re_chan;
	struct fsl_re_desc *desc;

	re_chan = container_of(chan, struct fsl_re_chan, chan);
	while (re_chan->alloc_count--) {
		desc = list_first_entry(&re_chan->free_q,
					struct fsl_re_desc,
					node);

		list_del(&desc->node);
		dma_pool_free(re_chan->re_dev->cf_desc_pool, desc->cf_addr,
			      desc->cf_paddr);
		kfree(desc);
	}

	if (!list_empty(&re_chan->free_q))
		dev_err(re_chan->dev, "chan resource cannot be cleaned!\n");
}

static int fsl_re_chan_probe(struct platform_device *ofdev,
		      struct device_node *np, u8 q, u32 off)
{
	struct device *dev, *chandev;
	struct fsl_re_drv_private *re_priv;
	struct fsl_re_chan *chan;
	struct dma_device *dma_dev;
	u32 ptr;
	u32 status;
	int ret = 0, rc;
	struct platform_device *chan_ofdev;

	dev = &ofdev->dev;
	re_priv = dev_get_drvdata(dev);
	dma_dev = &re_priv->dma_dev;

	chan = devm_kzalloc(dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	/* create platform device for chan node */
	chan_ofdev = of_platform_device_create(np, NULL, dev);
	if (!chan_ofdev) {
		dev_err(dev, "Not able to create ofdev for jr %d\n", q);
		ret = -EINVAL;
		goto err_free;
	}

	/* read reg property from dts */
	rc = of_property_read_u32(np, "reg", &ptr);
	if (rc) {
		dev_err(dev, "Reg property not found in jr %d\n", q);
		ret = -ENODEV;
		goto err_free;
	}

	chan->jrregs = (struct fsl_re_chan_cfg *)((u8 *)re_priv->re_regs +
			off + ptr);

	/* read irq property from dts */
	chan->irq = irq_of_parse_and_map(np, 0);
	if (chan->irq == NO_IRQ) {
		dev_err(dev, "No IRQ defined for JR %d\n", q);
		ret = -ENODEV;
		goto err_free;
	}

	snprintf(chan->name, sizeof(chan->name), "re_jr%02d", q);

	chandev = &chan_ofdev->dev;
	tasklet_init(&chan->irqtask, fsl_re_dequeue, (unsigned long)chandev);

	ret = request_irq(chan->irq, fsl_re_isr, 0, chan->name, chandev);
	if (ret) {
		dev_err(dev, "Unable to register interrupt for JR %d\n", q);
		ret = -EINVAL;
		goto err_free;
	}

	re_priv->re_jrs[q] = chan;
	chan->chan.device = dma_dev;
	chan->chan.private = chan;
	chan->dev = chandev;
	chan->re_dev = re_priv;

	spin_lock_init(&chan->desc_lock);
	INIT_LIST_HEAD(&chan->ack_q);
	INIT_LIST_HEAD(&chan->active_q);
	INIT_LIST_HEAD(&chan->submit_q);
	INIT_LIST_HEAD(&chan->free_q);

	chan->inb_ring_virt_addr = dma_pool_alloc(chan->re_dev->hw_desc_pool,
		GFP_KERNEL, &chan->inb_phys_addr);
	if (!chan->inb_ring_virt_addr) {
		dev_err(dev, "No dma memory for inb_ring_virt_addr\n");
		ret = -ENOMEM;
		goto err_free;
	}

	chan->oub_ring_virt_addr = dma_pool_alloc(chan->re_dev->hw_desc_pool,
		GFP_KERNEL, &chan->oub_phys_addr);
	if (!chan->oub_ring_virt_addr) {
		dev_err(dev, "No dma memory for oub_ring_virt_addr\n");
		ret = -ENOMEM;
		goto err_free_1;
	}

	/* Program the Inbound/Outbound ring base addresses and size */
	out_be32(&chan->jrregs->inbring_base_h,
		 chan->inb_phys_addr & FSL_RE_ADDR_BIT_MASK);
	out_be32(&chan->jrregs->oubring_base_h,
		 chan->oub_phys_addr & FSL_RE_ADDR_BIT_MASK);
	out_be32(&chan->jrregs->inbring_base_l,
		 chan->inb_phys_addr >> FSL_RE_ADDR_BIT_SHIFT);
	out_be32(&chan->jrregs->oubring_base_l,
		 chan->oub_phys_addr >> FSL_RE_ADDR_BIT_SHIFT);
	out_be32(&chan->jrregs->inbring_size,
		 FSL_RE_RING_SIZE << FSL_RE_RING_SIZE_SHIFT);
	out_be32(&chan->jrregs->oubring_size,
		 FSL_RE_RING_SIZE << FSL_RE_RING_SIZE_SHIFT);

	/* Read LIODN value from u-boot */
	status = in_be32(&chan->jrregs->jr_config_1) & FSL_RE_REG_LIODN_MASK;

	/* Program the CFG reg */
	out_be32(&chan->jrregs->jr_config_1,
		 FSL_RE_CFG1_CBSI | FSL_RE_CFG1_CBS0 | status);

	dev_set_drvdata(chandev, chan);

	/* Enable RE/CHAN */
	out_be32(&chan->jrregs->jr_command, FSL_RE_ENABLE);

	return 0;

err_free_1:
	dma_pool_free(chan->re_dev->hw_desc_pool, chan->inb_ring_virt_addr,
		      chan->inb_phys_addr);
err_free:
	return ret;
}

/* Probe function for RAID Engine */
static int fsl_re_probe(struct platform_device *ofdev)
{
	struct fsl_re_drv_private *re_priv;
	struct device_node *np;
	struct device_node *child;
	u32 off;
	u8 ridx = 0;
	struct dma_device *dma_dev;
	struct resource *res;
	int rc;
	struct device *dev = &ofdev->dev;

	re_priv = devm_kzalloc(dev, sizeof(*re_priv), GFP_KERNEL);
	if (!re_priv)
		return -ENOMEM;

	res = platform_get_resource(ofdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	/* IOMAP the entire RAID Engine region */
	re_priv->re_regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!re_priv->re_regs)
		return -EBUSY;

	/* Program the RE mode */
	out_be32(&re_priv->re_regs->global_config, FSL_RE_NON_DPAA_MODE);

	/* Program Galois Field polynomial */
	out_be32(&re_priv->re_regs->galois_field_config, FSL_RE_GFM_POLY);

	dev_info(dev, "version %x, mode %x, gfp %x\n",
		 in_be32(&re_priv->re_regs->re_version_id),
		 in_be32(&re_priv->re_regs->global_config),
		 in_be32(&re_priv->re_regs->galois_field_config));

	dma_dev = &re_priv->dma_dev;
	dma_dev->dev = dev;
	INIT_LIST_HEAD(&dma_dev->channels);
	dma_set_mask(dev, DMA_BIT_MASK(40));

	dma_dev->device_alloc_chan_resources = fsl_re_alloc_chan_resources;
	dma_dev->device_tx_status = fsl_re_tx_status;
	dma_dev->device_issue_pending = fsl_re_issue_pending;

	dma_dev->max_xor = FSL_RE_MAX_XOR_SRCS;
	dma_dev->device_prep_dma_xor = fsl_re_prep_dma_xor;
	dma_cap_set(DMA_XOR, dma_dev->cap_mask);

	dma_dev->max_pq = FSL_RE_MAX_PQ_SRCS;
	dma_dev->device_prep_dma_pq = fsl_re_prep_dma_pq;
	dma_cap_set(DMA_PQ, dma_dev->cap_mask);

	dma_dev->device_prep_dma_memcpy = fsl_re_prep_dma_memcpy;
	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);

	dma_dev->device_free_chan_resources = fsl_re_free_chan_resources;

	re_priv->total_chans = 0;

	re_priv->cf_desc_pool = dmam_pool_create("fsl_re_cf_desc_pool", dev,
					FSL_RE_CF_CDB_SIZE,
					FSL_RE_CF_CDB_ALIGN, 0);

	if (!re_priv->cf_desc_pool) {
		dev_err(dev, "No memory for fsl re_cf desc pool\n");
		return -ENOMEM;
	}

	re_priv->hw_desc_pool = dmam_pool_create("fsl_re_hw_desc_pool", dev,
			sizeof(struct fsl_re_hw_desc) * FSL_RE_RING_SIZE,
			FSL_RE_FRAME_ALIGN, 0);
	if (!re_priv->hw_desc_pool) {
		dev_err(dev, "No memory for fsl re_hw desc pool\n");
		return -ENOMEM;
	}

	dev_set_drvdata(dev, re_priv);

	/* Parse Device tree to find out the total number of JQs present */
	for_each_compatible_node(np, NULL, "fsl,raideng-v1.0-job-queue") {
		rc = of_property_read_u32(np, "reg", &off);
		if (rc) {
			dev_err(dev, "Reg property not found in JQ node\n");
			return -ENODEV;
		}
		/* Find out the Job Rings present under each JQ */
		for_each_child_of_node(np, child) {
			rc = of_device_is_compatible(child,
					     "fsl,raideng-v1.0-job-ring");
			if (rc) {
				fsl_re_chan_probe(ofdev, child, ridx++, off);
				re_priv->total_chans++;
			}
		}
	}

	dma_async_device_register(dma_dev);

	return 0;
}

static void fsl_re_remove_chan(struct fsl_re_chan *chan)
{
	tasklet_kill(&chan->irqtask);

	dma_pool_free(chan->re_dev->hw_desc_pool, chan->inb_ring_virt_addr,
		      chan->inb_phys_addr);

	dma_pool_free(chan->re_dev->hw_desc_pool, chan->oub_ring_virt_addr,
		      chan->oub_phys_addr);
}

static int fsl_re_remove(struct platform_device *ofdev)
{
	struct fsl_re_drv_private *re_priv;
	struct device *dev;
	int i;

	dev = &ofdev->dev;
	re_priv = dev_get_drvdata(dev);

	/* Cleanup chan related memory areas */
	for (i = 0; i < re_priv->total_chans; i++)
		fsl_re_remove_chan(re_priv->re_jrs[i]);

	/* Unregister the driver */
	dma_async_device_unregister(&re_priv->dma_dev);

	return 0;
}

static struct of_device_id fsl_re_ids[] = {
	{ .compatible = "fsl,raideng-v1.0", },
	{}
};

static struct platform_driver fsl_re_driver = {
	.driver = {
		.name = "fsl-raideng",
		.of_match_table = fsl_re_ids,
	},
	.probe = fsl_re_probe,
	.remove = fsl_re_remove,
};

module_platform_driver(fsl_re_driver);

MODULE_AUTHOR("Harninder Rai <harninder.rai@freescale.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Freescale RAID Engine Device Driver");
