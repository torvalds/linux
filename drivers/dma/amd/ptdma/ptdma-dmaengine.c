// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Passthrough DMA device driver
 * -- Based on the CCP driver
 *
 * Copyright (C) 2016,2021 Advanced Micro Devices, Inc.
 *
 * Author: Sanjay R Mehta <sanju.mehta@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 */

#include <linux/bitfield.h>
#include "ptdma.h"
#include "../ae4dma/ae4dma.h"
#include "../../dmaengine.h"

static char *ae4_error_codes[] = {
	"",
	"ERR 01: INVALID HEADER DW0",
	"ERR 02: INVALID STATUS",
	"ERR 03: INVALID LENGTH - 4 BYTE ALIGNMENT",
	"ERR 04: INVALID SRC ADDR - 4 BYTE ALIGNMENT",
	"ERR 05: INVALID DST ADDR - 4 BYTE ALIGNMENT",
	"ERR 06: INVALID ALIGNMENT",
	"ERR 07: INVALID DESCRIPTOR",
};

static void ae4_log_error(struct pt_device *d, int e)
{
	/* ERR 01 - 07 represents Invalid AE4 errors */
	if (e <= 7)
		dev_info(d->dev, "AE4DMA error: %s (0x%x)\n", ae4_error_codes[e], e);
	/* ERR 08 - 15 represents Invalid Descriptor errors */
	else if (e > 7 && e <= 15)
		dev_info(d->dev, "AE4DMA error: %s (0x%x)\n", "INVALID DESCRIPTOR", e);
	/* ERR 16 - 31 represents Firmware errors */
	else if (e > 15 && e <= 31)
		dev_info(d->dev, "AE4DMA error: %s (0x%x)\n", "FIRMWARE ERROR", e);
	/* ERR 32 - 63 represents Fatal errors */
	else if (e > 31 && e <= 63)
		dev_info(d->dev, "AE4DMA error: %s (0x%x)\n", "FATAL ERROR", e);
	/* ERR 64 - 255 represents PTE errors */
	else if (e > 63 && e <= 255)
		dev_info(d->dev, "AE4DMA error: %s (0x%x)\n", "PTE ERROR", e);
	else
		dev_info(d->dev, "Unknown AE4DMA error");
}

void ae4_check_status_error(struct ae4_cmd_queue *ae4cmd_q, int idx)
{
	struct pt_cmd_queue *cmd_q = &ae4cmd_q->cmd_q;
	struct ae4dma_desc desc;
	u8 status;

	memcpy(&desc, &cmd_q->qbase[idx], sizeof(struct ae4dma_desc));
	status = desc.dw1.status;
	if (status && status != AE4_DESC_COMPLETED) {
		cmd_q->cmd_error = desc.dw1.err_code;
		if (cmd_q->cmd_error)
			ae4_log_error(cmd_q->pt, cmd_q->cmd_error);
	}
}
EXPORT_SYMBOL_GPL(ae4_check_status_error);

static inline struct pt_dma_chan *to_pt_chan(struct dma_chan *dma_chan)
{
	return container_of(dma_chan, struct pt_dma_chan, vc.chan);
}

static inline struct pt_dma_desc *to_pt_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct pt_dma_desc, vd);
}

static void pt_free_chan_resources(struct dma_chan *dma_chan)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);

	vchan_free_chan_resources(&chan->vc);
}

static void pt_synchronize(struct dma_chan *dma_chan)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);

	vchan_synchronize(&chan->vc);
}

static void pt_do_cleanup(struct virt_dma_desc *vd)
{
	struct pt_dma_desc *desc = to_pt_desc(vd);
	struct pt_device *pt = desc->pt;

	kmem_cache_free(pt->dma_desc_cache, desc);
}

static struct pt_cmd_queue *pt_get_cmd_queue(struct pt_device *pt, struct pt_dma_chan *chan)
{
	struct ae4_cmd_queue *ae4cmd_q;
	struct pt_cmd_queue *cmd_q;
	struct ae4_device *ae4;

	if (pt->ver == AE4_DMA_VERSION) {
		ae4 = container_of(pt, struct ae4_device, pt);
		ae4cmd_q = &ae4->ae4cmd_q[chan->id];
		cmd_q = &ae4cmd_q->cmd_q;
	} else {
		cmd_q = &pt->cmd_q;
	}

	return cmd_q;
}

static int ae4_core_execute_cmd(struct ae4dma_desc *desc, struct ae4_cmd_queue *ae4cmd_q)
{
	bool soc = FIELD_GET(DWORD0_SOC, desc->dwouv.dw0);
	struct pt_cmd_queue *cmd_q = &ae4cmd_q->cmd_q;

	if (soc) {
		desc->dwouv.dw0 |= FIELD_PREP(DWORD0_IOC, desc->dwouv.dw0);
		desc->dwouv.dw0 &= ~DWORD0_SOC;
	}

	mutex_lock(&ae4cmd_q->cmd_lock);
	memcpy(&cmd_q->qbase[ae4cmd_q->tail_wi], desc, sizeof(struct ae4dma_desc));
	ae4cmd_q->q_cmd_count++;
	ae4cmd_q->tail_wi = (ae4cmd_q->tail_wi + 1) % CMD_Q_LEN;
	writel(ae4cmd_q->tail_wi, cmd_q->reg_control + AE4_WR_IDX_OFF);
	mutex_unlock(&ae4cmd_q->cmd_lock);

	wake_up(&ae4cmd_q->q_w);

	return 0;
}

static int pt_core_perform_passthru_ae4(struct pt_cmd_queue *cmd_q,
					struct pt_passthru_engine *pt_engine)
{
	struct ae4_cmd_queue *ae4cmd_q = container_of(cmd_q, struct ae4_cmd_queue, cmd_q);
	struct ae4dma_desc desc;

	cmd_q->cmd_error = 0;
	cmd_q->total_pt_ops++;
	memset(&desc, 0, sizeof(desc));
	desc.dwouv.dws.byte0 = CMD_AE4_DESC_DW0_VAL;

	desc.dw1.status = 0;
	desc.dw1.err_code = 0;
	desc.dw1.desc_id = 0;

	desc.length = pt_engine->src_len;

	desc.src_lo = upper_32_bits(pt_engine->src_dma);
	desc.src_hi = lower_32_bits(pt_engine->src_dma);
	desc.dst_lo = upper_32_bits(pt_engine->dst_dma);
	desc.dst_hi = lower_32_bits(pt_engine->dst_dma);

	return ae4_core_execute_cmd(&desc, ae4cmd_q);
}

static int pt_dma_start_desc(struct pt_dma_desc *desc, struct pt_dma_chan *chan)
{
	struct pt_passthru_engine *pt_engine;
	struct pt_device *pt;
	struct pt_cmd *pt_cmd;
	struct pt_cmd_queue *cmd_q;

	desc->issued_to_hw = 1;

	pt_cmd = &desc->pt_cmd;
	pt = pt_cmd->pt;

	cmd_q = pt_get_cmd_queue(pt, chan);

	pt_engine = &pt_cmd->passthru;

	pt->tdata.cmd = pt_cmd;

	/* Execute the command */
	if (pt->ver == AE4_DMA_VERSION)
		pt_cmd->ret = pt_core_perform_passthru_ae4(cmd_q, pt_engine);
	else
		pt_cmd->ret = pt_core_perform_passthru(cmd_q, pt_engine);

	return 0;
}

static struct pt_dma_desc *pt_next_dma_desc(struct pt_dma_chan *chan)
{
	/* Get the next DMA descriptor on the active list */
	struct virt_dma_desc *vd = vchan_next_desc(&chan->vc);

	return vd ? to_pt_desc(vd) : NULL;
}

static struct pt_dma_desc *pt_handle_active_desc(struct pt_dma_chan *chan,
						 struct pt_dma_desc *desc)
{
	struct dma_async_tx_descriptor *tx_desc;
	struct virt_dma_desc *vd;
	struct pt_device *pt;
	unsigned long flags;

	pt = chan->pt;
	/* Loop over descriptors until one is found with commands */
	do {
		if (desc) {
			if (!desc->issued_to_hw) {
				/* No errors, keep going */
				if (desc->status != DMA_ERROR)
					return desc;
			}

			tx_desc = &desc->vd.tx;
			vd = &desc->vd;
		} else {
			tx_desc = NULL;
		}

		spin_lock_irqsave(&chan->vc.lock, flags);

		if (pt->ver != AE4_DMA_VERSION && desc) {
			if (desc->status != DMA_COMPLETE) {
				if (desc->status != DMA_ERROR)
					desc->status = DMA_COMPLETE;

				dma_cookie_complete(tx_desc);
				dma_descriptor_unmap(tx_desc);
				list_del(&desc->vd.node);
			} else {
				/* Don't handle it twice */
				tx_desc = NULL;
			}
		}

		desc = pt_next_dma_desc(chan);

		spin_unlock_irqrestore(&chan->vc.lock, flags);

		if (pt->ver != AE4_DMA_VERSION && tx_desc) {
			dmaengine_desc_get_callback_invoke(tx_desc, NULL);
			dma_run_dependencies(tx_desc);
			vchan_vdesc_fini(vd);
		}
	} while (desc);

	return NULL;
}

static inline bool ae4_core_queue_full(struct pt_cmd_queue *cmd_q)
{
	u32 front_wi = readl(cmd_q->reg_control + AE4_WR_IDX_OFF);
	u32 rear_ri = readl(cmd_q->reg_control + AE4_RD_IDX_OFF);

	if (((MAX_CMD_QLEN + front_wi - rear_ri) % MAX_CMD_QLEN)  >= (MAX_CMD_QLEN - 1))
		return true;

	return false;
}

static void pt_cmd_callback(void *data, int err)
{
	struct pt_dma_desc *desc = data;
	struct ae4_cmd_queue *ae4cmd_q;
	struct dma_chan *dma_chan;
	struct pt_dma_chan *chan;
	struct ae4_device *ae4;
	struct pt_device *pt;
	int ret;

	if (err == -EINPROGRESS)
		return;

	dma_chan = desc->vd.tx.chan;
	chan = to_pt_chan(dma_chan);
	pt = chan->pt;

	if (err)
		desc->status = DMA_ERROR;

	while (true) {
		if (pt->ver == AE4_DMA_VERSION) {
			ae4 = container_of(pt, struct ae4_device, pt);
			ae4cmd_q = &ae4->ae4cmd_q[chan->id];

			if (ae4cmd_q->q_cmd_count >= (CMD_Q_LEN - 1) ||
			    ae4_core_queue_full(&ae4cmd_q->cmd_q)) {
				wake_up(&ae4cmd_q->q_w);

				if (wait_for_completion_timeout(&ae4cmd_q->cmp,
								msecs_to_jiffies(AE4_TIME_OUT))
								== 0) {
					dev_err(pt->dev, "TIMEOUT %d:\n", ae4cmd_q->id);
					break;
				}

				reinit_completion(&ae4cmd_q->cmp);
				continue;
			}
		}

		/* Check for DMA descriptor completion */
		desc = pt_handle_active_desc(chan, desc);

		/* Don't submit cmd if no descriptor or DMA is paused */
		if (!desc)
			break;

		ret = pt_dma_start_desc(desc, chan);
		if (!ret)
			break;

		desc->status = DMA_ERROR;
	}
}

static struct pt_dma_desc *pt_alloc_dma_desc(struct pt_dma_chan *chan,
					     unsigned long flags)
{
	struct pt_dma_desc *desc;

	desc = kmem_cache_zalloc(chan->pt->dma_desc_cache, GFP_NOWAIT);
	if (!desc)
		return NULL;

	vchan_tx_prep(&chan->vc, &desc->vd, flags);

	desc->pt = chan->pt;
	desc->pt->cmd_q.int_en = !!(flags & DMA_PREP_INTERRUPT);
	desc->issued_to_hw = 0;
	desc->status = DMA_IN_PROGRESS;

	return desc;
}

static void pt_cmd_callback_work(void *data, int err)
{
	struct dma_async_tx_descriptor *tx_desc;
	struct pt_dma_desc *desc = data;
	struct dma_chan *dma_chan;
	struct virt_dma_desc *vd;
	struct pt_dma_chan *chan;
	unsigned long flags;

	if (!desc)
		return;

	dma_chan = desc->vd.tx.chan;
	chan = to_pt_chan(dma_chan);

	if (err == -EINPROGRESS)
		return;

	tx_desc = &desc->vd.tx;
	vd = &desc->vd;

	if (err)
		desc->status = DMA_ERROR;

	spin_lock_irqsave(&chan->vc.lock, flags);
	if (desc->status != DMA_COMPLETE) {
		if (desc->status != DMA_ERROR)
			desc->status = DMA_COMPLETE;

		dma_cookie_complete(tx_desc);
		dma_descriptor_unmap(tx_desc);
	} else {
		tx_desc = NULL;
	}
	spin_unlock_irqrestore(&chan->vc.lock, flags);

	if (tx_desc) {
		dmaengine_desc_get_callback_invoke(tx_desc, NULL);
		dma_run_dependencies(tx_desc);
		list_del(&desc->vd.node);
		vchan_vdesc_fini(vd);
	}
}

static struct pt_dma_desc *pt_create_desc(struct dma_chan *dma_chan,
					  dma_addr_t dst,
					  dma_addr_t src,
					  unsigned int len,
					  unsigned long flags)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);
	struct pt_passthru_engine *pt_engine;
	struct pt_device *pt = chan->pt;
	struct ae4_cmd_queue *ae4cmd_q;
	struct pt_dma_desc *desc;
	struct ae4_device *ae4;
	struct pt_cmd *pt_cmd;

	desc = pt_alloc_dma_desc(chan, flags);
	if (!desc)
		return NULL;

	pt_cmd = &desc->pt_cmd;
	pt_cmd->pt = pt;
	pt_engine = &pt_cmd->passthru;
	pt_cmd->engine = PT_ENGINE_PASSTHRU;
	pt_engine->src_dma = src;
	pt_engine->dst_dma = dst;
	pt_engine->src_len = len;
	pt_cmd->pt_cmd_callback = pt_cmd_callback;
	pt_cmd->data = desc;

	desc->len = len;

	if (pt->ver == AE4_DMA_VERSION) {
		pt_cmd->pt_cmd_callback = pt_cmd_callback_work;
		ae4 = container_of(pt, struct ae4_device, pt);
		ae4cmd_q = &ae4->ae4cmd_q[chan->id];
		mutex_lock(&ae4cmd_q->cmd_lock);
		list_add_tail(&pt_cmd->entry, &ae4cmd_q->cmd);
		mutex_unlock(&ae4cmd_q->cmd_lock);
	}

	return desc;
}

static struct dma_async_tx_descriptor *
pt_prep_dma_memcpy(struct dma_chan *dma_chan, dma_addr_t dst,
		   dma_addr_t src, size_t len, unsigned long flags)
{
	struct pt_dma_desc *desc;

	desc = pt_create_desc(dma_chan, dst, src, len, flags);
	if (!desc)
		return NULL;

	return &desc->vd.tx;
}

static struct dma_async_tx_descriptor *
pt_prep_dma_interrupt(struct dma_chan *dma_chan, unsigned long flags)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);
	struct pt_dma_desc *desc;

	desc = pt_alloc_dma_desc(chan, flags);
	if (!desc)
		return NULL;

	return &desc->vd.tx;
}

static void pt_issue_pending(struct dma_chan *dma_chan)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);
	struct pt_dma_desc *desc;
	struct pt_device *pt;
	unsigned long flags;
	bool engine_is_idle = true;

	pt = chan->pt;

	spin_lock_irqsave(&chan->vc.lock, flags);

	desc = pt_next_dma_desc(chan);
	if (desc && pt->ver != AE4_DMA_VERSION)
		engine_is_idle = false;

	vchan_issue_pending(&chan->vc);

	desc = pt_next_dma_desc(chan);

	spin_unlock_irqrestore(&chan->vc.lock, flags);

	/* If there was nothing active, start processing */
	if (engine_is_idle && desc)
		pt_cmd_callback(desc, 0);
}

static void pt_check_status_trans_ae4(struct pt_device *pt, struct pt_cmd_queue *cmd_q)
{
	struct ae4_cmd_queue *ae4cmd_q = container_of(cmd_q, struct ae4_cmd_queue, cmd_q);
	int i;

	for (i = 0; i < CMD_Q_LEN; i++)
		ae4_check_status_error(ae4cmd_q, i);
}

static enum dma_status
pt_tx_status(struct dma_chan *c, dma_cookie_t cookie,
		struct dma_tx_state *txstate)
{
	struct pt_dma_chan *chan = to_pt_chan(c);
	struct pt_device *pt = chan->pt;
	struct pt_cmd_queue *cmd_q;

	cmd_q = pt_get_cmd_queue(pt, chan);

	if (pt->ver == AE4_DMA_VERSION)
		pt_check_status_trans_ae4(pt, cmd_q);
	else
		pt_check_status_trans(pt, cmd_q);

	return dma_cookie_status(c, cookie, txstate);
}

static int pt_pause(struct dma_chan *dma_chan)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);
	struct pt_device *pt = chan->pt;
	struct pt_cmd_queue *cmd_q;
	unsigned long flags;

	spin_lock_irqsave(&chan->vc.lock, flags);
	cmd_q = pt_get_cmd_queue(pt, chan);
	pt_stop_queue(cmd_q);
	spin_unlock_irqrestore(&chan->vc.lock, flags);

	return 0;
}

static int pt_resume(struct dma_chan *dma_chan)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);
	struct pt_dma_desc *desc = NULL;
	struct pt_device *pt = chan->pt;
	struct pt_cmd_queue *cmd_q;
	unsigned long flags;

	spin_lock_irqsave(&chan->vc.lock, flags);
	cmd_q = pt_get_cmd_queue(pt, chan);
	pt_start_queue(cmd_q);
	desc = pt_next_dma_desc(chan);
	spin_unlock_irqrestore(&chan->vc.lock, flags);

	/* If there was something active, re-start */
	if (desc)
		pt_cmd_callback(desc, 0);

	return 0;
}

static int pt_terminate_all(struct dma_chan *dma_chan)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);
	struct pt_device *pt = chan->pt;
	struct pt_cmd_queue *cmd_q;
	unsigned long flags;
	LIST_HEAD(head);

	cmd_q = pt_get_cmd_queue(pt, chan);
	if (pt->ver == AE4_DMA_VERSION)
		pt_stop_queue(cmd_q);
	else
		iowrite32(SUPPORTED_INTERRUPTS, cmd_q->reg_control + 0x0010);

	spin_lock_irqsave(&chan->vc.lock, flags);
	vchan_get_all_descriptors(&chan->vc, &head);
	spin_unlock_irqrestore(&chan->vc.lock, flags);

	vchan_dma_desc_free_list(&chan->vc, &head);
	vchan_free_chan_resources(&chan->vc);

	return 0;
}

int pt_dmaengine_register(struct pt_device *pt)
{
	struct dma_device *dma_dev = &pt->dma_dev;
	struct ae4_cmd_queue *ae4cmd_q = NULL;
	struct ae4_device *ae4 = NULL;
	struct pt_dma_chan *chan;
	char *desc_cache_name;
	char *cmd_cache_name;
	int ret, i;

	if (pt->ver == AE4_DMA_VERSION)
		ae4 = container_of(pt, struct ae4_device, pt);

	if (ae4)
		pt->pt_dma_chan = devm_kcalloc(pt->dev, ae4->cmd_q_count,
					       sizeof(*pt->pt_dma_chan), GFP_KERNEL);
	else
		pt->pt_dma_chan = devm_kzalloc(pt->dev, sizeof(*pt->pt_dma_chan),
					       GFP_KERNEL);

	if (!pt->pt_dma_chan)
		return -ENOMEM;

	cmd_cache_name = devm_kasprintf(pt->dev, GFP_KERNEL,
					"%s-dmaengine-cmd-cache",
					dev_name(pt->dev));
	if (!cmd_cache_name)
		return -ENOMEM;

	desc_cache_name = devm_kasprintf(pt->dev, GFP_KERNEL,
					 "%s-dmaengine-desc-cache",
					 dev_name(pt->dev));
	if (!desc_cache_name) {
		ret = -ENOMEM;
		goto err_cache;
	}

	pt->dma_desc_cache = kmem_cache_create(desc_cache_name,
					       sizeof(struct pt_dma_desc), 0,
					       SLAB_HWCACHE_ALIGN, NULL);
	if (!pt->dma_desc_cache) {
		ret = -ENOMEM;
		goto err_cache;
	}

	dma_dev->dev = pt->dev;
	dma_dev->src_addr_widths = DMA_SLAVE_BUSWIDTH_64_BYTES;
	dma_dev->dst_addr_widths = DMA_SLAVE_BUSWIDTH_64_BYTES;
	dma_dev->directions = DMA_MEM_TO_MEM;
	dma_dev->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
	dma_cap_set(DMA_INTERRUPT, dma_dev->cap_mask);

	/*
	 * PTDMA is intended to be used with the AMD NTB devices, hence
	 * marking it as DMA_PRIVATE.
	 */
	dma_cap_set(DMA_PRIVATE, dma_dev->cap_mask);

	INIT_LIST_HEAD(&dma_dev->channels);

	/* Set base and prep routines */
	dma_dev->device_free_chan_resources = pt_free_chan_resources;
	dma_dev->device_prep_dma_memcpy = pt_prep_dma_memcpy;
	dma_dev->device_prep_dma_interrupt = pt_prep_dma_interrupt;
	dma_dev->device_issue_pending = pt_issue_pending;
	dma_dev->device_tx_status = pt_tx_status;
	dma_dev->device_pause = pt_pause;
	dma_dev->device_resume = pt_resume;
	dma_dev->device_terminate_all = pt_terminate_all;
	dma_dev->device_synchronize = pt_synchronize;

	if (ae4) {
		for (i = 0; i < ae4->cmd_q_count; i++) {
			chan = pt->pt_dma_chan + i;
			ae4cmd_q = &ae4->ae4cmd_q[i];
			chan->id = ae4cmd_q->id;
			chan->pt = pt;
			chan->vc.desc_free = pt_do_cleanup;
			vchan_init(&chan->vc, dma_dev);
		}
	} else {
		chan = pt->pt_dma_chan;
		chan->pt = pt;
		chan->vc.desc_free = pt_do_cleanup;
		vchan_init(&chan->vc, dma_dev);
	}

	ret = dma_async_device_register(dma_dev);
	if (ret)
		goto err_reg;

	return 0;

err_reg:
	kmem_cache_destroy(pt->dma_desc_cache);

err_cache:
	kmem_cache_destroy(pt->dma_cmd_cache);

	return ret;
}
EXPORT_SYMBOL_GPL(pt_dmaengine_register);

void pt_dmaengine_unregister(struct pt_device *pt)
{
	struct dma_device *dma_dev = &pt->dma_dev;

	dma_async_device_unregister(dma_dev);

	kmem_cache_destroy(pt->dma_desc_cache);
	kmem_cache_destroy(pt->dma_cmd_cache);
}
