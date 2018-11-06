/*
 * DMA driver for STMicroelectronics STi FDMA controller
 *
 * Copyright (C) 2014 STMicroelectronics
 *
 * Author: Ludovic Barre <Ludovic.barre@st.com>
 *	   Peter Griffin <peter.griffin@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/remoteproc.h>

#include "st_fdma.h"

static inline struct st_fdma_chan *to_st_fdma_chan(struct dma_chan *c)
{
	return container_of(c, struct st_fdma_chan, vchan.chan);
}

static struct st_fdma_desc *to_st_fdma_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct st_fdma_desc, vdesc);
}

static int st_fdma_dreq_get(struct st_fdma_chan *fchan)
{
	struct st_fdma_dev *fdev = fchan->fdev;
	u32 req_line_cfg = fchan->cfg.req_line;
	u32 dreq_line;
	int try = 0;

	/*
	 * dreq_mask is shared for n channels of fdma, so all accesses must be
	 * atomic. if the dreq_mask is changed between ffz and set_bit,
	 * we retry
	 */
	do {
		if (fdev->dreq_mask == ~0L) {
			dev_err(fdev->dev, "No req lines available\n");
			return -EINVAL;
		}

		if (try || req_line_cfg >= ST_FDMA_NR_DREQS) {
			dev_err(fdev->dev, "Invalid or used req line\n");
			return -EINVAL;
		} else {
			dreq_line = req_line_cfg;
		}

		try++;
	} while (test_and_set_bit(dreq_line, &fdev->dreq_mask));

	dev_dbg(fdev->dev, "get dreq_line:%d mask:%#lx\n",
		dreq_line, fdev->dreq_mask);

	return dreq_line;
}

static void st_fdma_dreq_put(struct st_fdma_chan *fchan)
{
	struct st_fdma_dev *fdev = fchan->fdev;

	dev_dbg(fdev->dev, "put dreq_line:%#x\n", fchan->dreq_line);
	clear_bit(fchan->dreq_line, &fdev->dreq_mask);
}

static void st_fdma_xfer_desc(struct st_fdma_chan *fchan)
{
	struct virt_dma_desc *vdesc;
	unsigned long nbytes, ch_cmd, cmd;

	vdesc = vchan_next_desc(&fchan->vchan);
	if (!vdesc)
		return;

	fchan->fdesc = to_st_fdma_desc(vdesc);
	nbytes = fchan->fdesc->node[0].desc->nbytes;
	cmd = FDMA_CMD_START(fchan->vchan.chan.chan_id);
	ch_cmd = fchan->fdesc->node[0].pdesc | FDMA_CH_CMD_STA_START;

	/* start the channel for the descriptor */
	fnode_write(fchan, nbytes, FDMA_CNTN_OFST);
	fchan_write(fchan, ch_cmd, FDMA_CH_CMD_OFST);
	writel(cmd,
		fchan->fdev->slim_rproc->peri + FDMA_CMD_SET_OFST);

	dev_dbg(fchan->fdev->dev, "start chan:%d\n", fchan->vchan.chan.chan_id);
}

static void st_fdma_ch_sta_update(struct st_fdma_chan *fchan,
				  unsigned long int_sta)
{
	unsigned long ch_sta, ch_err;
	int ch_id = fchan->vchan.chan.chan_id;
	struct st_fdma_dev *fdev = fchan->fdev;

	ch_sta = fchan_read(fchan, FDMA_CH_CMD_OFST);
	ch_err = ch_sta & FDMA_CH_CMD_ERR_MASK;
	ch_sta &= FDMA_CH_CMD_STA_MASK;

	if (int_sta & FDMA_INT_STA_ERR) {
		dev_warn(fdev->dev, "chan:%d, error:%ld\n", ch_id, ch_err);
		fchan->status = DMA_ERROR;
		return;
	}

	switch (ch_sta) {
	case FDMA_CH_CMD_STA_PAUSED:
		fchan->status = DMA_PAUSED;
		break;

	case FDMA_CH_CMD_STA_RUNNING:
		fchan->status = DMA_IN_PROGRESS;
		break;
	}
}

static irqreturn_t st_fdma_irq_handler(int irq, void *dev_id)
{
	struct st_fdma_dev *fdev = dev_id;
	irqreturn_t ret = IRQ_NONE;
	struct st_fdma_chan *fchan = &fdev->chans[0];
	unsigned long int_sta, clr;

	int_sta = fdma_read(fdev, FDMA_INT_STA_OFST);
	clr = int_sta;

	for (; int_sta != 0 ; int_sta >>= 2, fchan++) {
		if (!(int_sta & (FDMA_INT_STA_CH | FDMA_INT_STA_ERR)))
			continue;

		spin_lock(&fchan->vchan.lock);
		st_fdma_ch_sta_update(fchan, int_sta);

		if (fchan->fdesc) {
			if (!fchan->fdesc->iscyclic) {
				list_del(&fchan->fdesc->vdesc.node);
				vchan_cookie_complete(&fchan->fdesc->vdesc);
				fchan->fdesc = NULL;
				fchan->status = DMA_COMPLETE;
			} else {
				vchan_cyclic_callback(&fchan->fdesc->vdesc);
			}

			/* Start the next descriptor (if available) */
			if (!fchan->fdesc)
				st_fdma_xfer_desc(fchan);
		}

		spin_unlock(&fchan->vchan.lock);
		ret = IRQ_HANDLED;
	}

	fdma_write(fdev, clr, FDMA_INT_CLR_OFST);

	return ret;
}

static struct dma_chan *st_fdma_of_xlate(struct of_phandle_args *dma_spec,
					 struct of_dma *ofdma)
{
	struct st_fdma_dev *fdev = ofdma->of_dma_data;
	struct dma_chan *chan;
	struct st_fdma_chan *fchan;
	int ret;

	if (dma_spec->args_count < 1)
		return ERR_PTR(-EINVAL);

	if (fdev->dma_device.dev->of_node != dma_spec->np)
		return ERR_PTR(-EINVAL);

	ret = rproc_boot(fdev->slim_rproc->rproc);
	if (ret == -ENOENT)
		return ERR_PTR(-EPROBE_DEFER);
	else if (ret)
		return ERR_PTR(ret);

	chan = dma_get_any_slave_channel(&fdev->dma_device);
	if (!chan)
		goto err_chan;

	fchan = to_st_fdma_chan(chan);

	fchan->cfg.of_node = dma_spec->np;
	fchan->cfg.req_line = dma_spec->args[0];
	fchan->cfg.req_ctrl = 0;
	fchan->cfg.type = ST_FDMA_TYPE_FREE_RUN;

	if (dma_spec->args_count > 1)
		fchan->cfg.req_ctrl = dma_spec->args[1]
			& FDMA_REQ_CTRL_CFG_MASK;

	if (dma_spec->args_count > 2)
		fchan->cfg.type = dma_spec->args[2];

	if (fchan->cfg.type == ST_FDMA_TYPE_FREE_RUN) {
		fchan->dreq_line = 0;
	} else {
		fchan->dreq_line = st_fdma_dreq_get(fchan);
		if (IS_ERR_VALUE(fchan->dreq_line)) {
			chan = ERR_PTR(fchan->dreq_line);
			goto err_chan;
		}
	}

	dev_dbg(fdev->dev, "xlate req_line:%d type:%d req_ctrl:%#lx\n",
		fchan->cfg.req_line, fchan->cfg.type, fchan->cfg.req_ctrl);

	return chan;

err_chan:
	rproc_shutdown(fdev->slim_rproc->rproc);
	return chan;

}

static void st_fdma_free_desc(struct virt_dma_desc *vdesc)
{
	struct st_fdma_desc *fdesc;
	int i;

	fdesc = to_st_fdma_desc(vdesc);
	for (i = 0; i < fdesc->n_nodes; i++)
		dma_pool_free(fdesc->fchan->node_pool, fdesc->node[i].desc,
			      fdesc->node[i].pdesc);
	kfree(fdesc);
}

static struct st_fdma_desc *st_fdma_alloc_desc(struct st_fdma_chan *fchan,
					       int sg_len)
{
	struct st_fdma_desc *fdesc;
	int i;

	fdesc = kzalloc(sizeof(*fdesc) +
			sizeof(struct st_fdma_sw_node) * sg_len, GFP_NOWAIT);
	if (!fdesc)
		return NULL;

	fdesc->fchan = fchan;
	fdesc->n_nodes = sg_len;
	for (i = 0; i < sg_len; i++) {
		fdesc->node[i].desc = dma_pool_alloc(fchan->node_pool,
				GFP_NOWAIT, &fdesc->node[i].pdesc);
		if (!fdesc->node[i].desc)
			goto err;
	}
	return fdesc;

err:
	while (--i >= 0)
		dma_pool_free(fchan->node_pool, fdesc->node[i].desc,
			      fdesc->node[i].pdesc);
	kfree(fdesc);
	return NULL;
}

static int st_fdma_alloc_chan_res(struct dma_chan *chan)
{
	struct st_fdma_chan *fchan = to_st_fdma_chan(chan);

	/* Create the dma pool for descriptor allocation */
	fchan->node_pool = dma_pool_create(dev_name(&chan->dev->device),
					    fchan->fdev->dev,
					    sizeof(struct st_fdma_hw_node),
					    __alignof__(struct st_fdma_hw_node),
					    0);

	if (!fchan->node_pool) {
		dev_err(fchan->fdev->dev, "unable to allocate desc pool\n");
		return -ENOMEM;
	}

	dev_dbg(fchan->fdev->dev, "alloc ch_id:%d type:%d\n",
		fchan->vchan.chan.chan_id, fchan->cfg.type);

	return 0;
}

static void st_fdma_free_chan_res(struct dma_chan *chan)
{
	struct st_fdma_chan *fchan = to_st_fdma_chan(chan);
	struct rproc *rproc = fchan->fdev->slim_rproc->rproc;
	unsigned long flags;

	LIST_HEAD(head);

	dev_dbg(fchan->fdev->dev, "%s: freeing chan:%d\n",
		__func__, fchan->vchan.chan.chan_id);

	if (fchan->cfg.type != ST_FDMA_TYPE_FREE_RUN)
		st_fdma_dreq_put(fchan);

	spin_lock_irqsave(&fchan->vchan.lock, flags);
	fchan->fdesc = NULL;
	spin_unlock_irqrestore(&fchan->vchan.lock, flags);

	dma_pool_destroy(fchan->node_pool);
	fchan->node_pool = NULL;
	memset(&fchan->cfg, 0, sizeof(struct st_fdma_cfg));

	rproc_shutdown(rproc);
}

static struct dma_async_tx_descriptor *st_fdma_prep_dma_memcpy(
	struct dma_chan *chan,	dma_addr_t dst, dma_addr_t src,
	size_t len, unsigned long flags)
{
	struct st_fdma_chan *fchan;
	struct st_fdma_desc *fdesc;
	struct st_fdma_hw_node *hw_node;

	if (!len)
		return NULL;

	fchan = to_st_fdma_chan(chan);

	/* We only require a single descriptor */
	fdesc = st_fdma_alloc_desc(fchan, 1);
	if (!fdesc) {
		dev_err(fchan->fdev->dev, "no memory for desc\n");
		return NULL;
	}

	hw_node = fdesc->node[0].desc;
	hw_node->next = 0;
	hw_node->control = FDMA_NODE_CTRL_REQ_MAP_FREE_RUN;
	hw_node->control |= FDMA_NODE_CTRL_SRC_INCR;
	hw_node->control |= FDMA_NODE_CTRL_DST_INCR;
	hw_node->control |= FDMA_NODE_CTRL_INT_EON;
	hw_node->nbytes = len;
	hw_node->saddr = src;
	hw_node->daddr = dst;
	hw_node->generic.length = len;
	hw_node->generic.sstride = 0;
	hw_node->generic.dstride = 0;

	return vchan_tx_prep(&fchan->vchan, &fdesc->vdesc, flags);
}

static int config_reqctrl(struct st_fdma_chan *fchan,
			  enum dma_transfer_direction direction)
{
	u32 maxburst = 0, addr = 0;
	enum dma_slave_buswidth width;
	int ch_id = fchan->vchan.chan.chan_id;
	struct st_fdma_dev *fdev = fchan->fdev;

	switch (direction) {

	case DMA_DEV_TO_MEM:
		fchan->cfg.req_ctrl &= ~FDMA_REQ_CTRL_WNR;
		maxburst = fchan->scfg.src_maxburst;
		width = fchan->scfg.src_addr_width;
		addr = fchan->scfg.src_addr;
		break;

	case DMA_MEM_TO_DEV:
		fchan->cfg.req_ctrl |= FDMA_REQ_CTRL_WNR;
		maxburst = fchan->scfg.dst_maxburst;
		width = fchan->scfg.dst_addr_width;
		addr = fchan->scfg.dst_addr;
		break;

	default:
		return -EINVAL;
	}

	fchan->cfg.req_ctrl &= ~FDMA_REQ_CTRL_OPCODE_MASK;

	switch (width) {

	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		fchan->cfg.req_ctrl |= FDMA_REQ_CTRL_OPCODE_LD_ST1;
		break;

	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		fchan->cfg.req_ctrl |= FDMA_REQ_CTRL_OPCODE_LD_ST2;
		break;

	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		fchan->cfg.req_ctrl |= FDMA_REQ_CTRL_OPCODE_LD_ST4;
		break;

	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		fchan->cfg.req_ctrl |= FDMA_REQ_CTRL_OPCODE_LD_ST8;
		break;

	default:
		return -EINVAL;
	}

	fchan->cfg.req_ctrl &= ~FDMA_REQ_CTRL_NUM_OPS_MASK;
	fchan->cfg.req_ctrl |= FDMA_REQ_CTRL_NUM_OPS(maxburst-1);
	dreq_write(fchan, fchan->cfg.req_ctrl, FDMA_REQ_CTRL_OFST);

	fchan->cfg.dev_addr = addr;
	fchan->cfg.dir = direction;

	dev_dbg(fdev->dev, "chan:%d config_reqctrl:%#x req_ctrl:%#lx\n",
		ch_id, addr, fchan->cfg.req_ctrl);

	return 0;
}

static void fill_hw_node(struct st_fdma_hw_node *hw_node,
			struct st_fdma_chan *fchan,
			enum dma_transfer_direction direction)
{
	if (direction == DMA_MEM_TO_DEV) {
		hw_node->control |= FDMA_NODE_CTRL_SRC_INCR;
		hw_node->control |= FDMA_NODE_CTRL_DST_STATIC;
		hw_node->daddr = fchan->cfg.dev_addr;
	} else {
		hw_node->control |= FDMA_NODE_CTRL_SRC_STATIC;
		hw_node->control |= FDMA_NODE_CTRL_DST_INCR;
		hw_node->saddr = fchan->cfg.dev_addr;
	}

	hw_node->generic.sstride = 0;
	hw_node->generic.dstride = 0;
}

static inline struct st_fdma_chan *st_fdma_prep_common(struct dma_chan *chan,
		size_t len, enum dma_transfer_direction direction)
{
	struct st_fdma_chan *fchan;

	if (!chan || !len)
		return NULL;

	fchan = to_st_fdma_chan(chan);

	if (!is_slave_direction(direction)) {
		dev_err(fchan->fdev->dev, "bad direction?\n");
		return NULL;
	}

	return fchan;
}

static struct dma_async_tx_descriptor *st_fdma_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t buf_addr, size_t len,
		size_t period_len, enum dma_transfer_direction direction,
		unsigned long flags)
{
	struct st_fdma_chan *fchan;
	struct st_fdma_desc *fdesc;
	int sg_len, i;

	fchan = st_fdma_prep_common(chan, len, direction);
	if (!fchan)
		return NULL;

	if (!period_len)
		return NULL;

	if (config_reqctrl(fchan, direction)) {
		dev_err(fchan->fdev->dev, "bad width or direction\n");
		return NULL;
	}

	/* the buffer length must be a multiple of period_len */
	if (len % period_len != 0) {
		dev_err(fchan->fdev->dev, "len is not multiple of period\n");
		return NULL;
	}

	sg_len = len / period_len;
	fdesc = st_fdma_alloc_desc(fchan, sg_len);
	if (!fdesc) {
		dev_err(fchan->fdev->dev, "no memory for desc\n");
		return NULL;
	}

	fdesc->iscyclic = true;

	for (i = 0; i < sg_len; i++) {
		struct st_fdma_hw_node *hw_node = fdesc->node[i].desc;

		hw_node->next = fdesc->node[(i + 1) % sg_len].pdesc;

		hw_node->control =
			FDMA_NODE_CTRL_REQ_MAP_DREQ(fchan->dreq_line);
		hw_node->control |= FDMA_NODE_CTRL_INT_EON;

		fill_hw_node(hw_node, fchan, direction);

		if (direction == DMA_MEM_TO_DEV)
			hw_node->saddr = buf_addr + (i * period_len);
		else
			hw_node->daddr = buf_addr + (i * period_len);

		hw_node->nbytes = period_len;
		hw_node->generic.length = period_len;
	}

	return vchan_tx_prep(&fchan->vchan, &fdesc->vdesc, flags);
}

static struct dma_async_tx_descriptor *st_fdma_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct st_fdma_chan *fchan;
	struct st_fdma_desc *fdesc;
	struct st_fdma_hw_node *hw_node;
	struct scatterlist *sg;
	int i;

	fchan = st_fdma_prep_common(chan, sg_len, direction);
	if (!fchan)
		return NULL;

	if (!sgl)
		return NULL;

	fdesc = st_fdma_alloc_desc(fchan, sg_len);
	if (!fdesc) {
		dev_err(fchan->fdev->dev, "no memory for desc\n");
		return NULL;
	}

	fdesc->iscyclic = false;

	for_each_sg(sgl, sg, sg_len, i) {
		hw_node = fdesc->node[i].desc;

		hw_node->next = fdesc->node[(i + 1) % sg_len].pdesc;
		hw_node->control = FDMA_NODE_CTRL_REQ_MAP_DREQ(fchan->dreq_line);

		fill_hw_node(hw_node, fchan, direction);

		if (direction == DMA_MEM_TO_DEV)
			hw_node->saddr = sg_dma_address(sg);
		else
			hw_node->daddr = sg_dma_address(sg);

		hw_node->nbytes = sg_dma_len(sg);
		hw_node->generic.length = sg_dma_len(sg);
	}

	/* interrupt at end of last node */
	hw_node->control |= FDMA_NODE_CTRL_INT_EON;

	return vchan_tx_prep(&fchan->vchan, &fdesc->vdesc, flags);
}

static size_t st_fdma_desc_residue(struct st_fdma_chan *fchan,
				   struct virt_dma_desc *vdesc,
				   bool in_progress)
{
	struct st_fdma_desc *fdesc = fchan->fdesc;
	size_t residue = 0;
	dma_addr_t cur_addr = 0;
	int i;

	if (in_progress) {
		cur_addr = fchan_read(fchan, FDMA_CH_CMD_OFST);
		cur_addr &= FDMA_CH_CMD_DATA_MASK;
	}

	for (i = fchan->fdesc->n_nodes - 1 ; i >= 0; i--) {
		if (cur_addr == fdesc->node[i].pdesc) {
			residue += fnode_read(fchan, FDMA_CNTN_OFST);
			break;
		}
		residue += fdesc->node[i].desc->nbytes;
	}

	return residue;
}

static enum dma_status st_fdma_tx_status(struct dma_chan *chan,
					 dma_cookie_t cookie,
					 struct dma_tx_state *txstate)
{
	struct st_fdma_chan *fchan = to_st_fdma_chan(chan);
	struct virt_dma_desc *vd;
	enum dma_status ret;
	unsigned long flags;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate)
		return ret;

	spin_lock_irqsave(&fchan->vchan.lock, flags);
	vd = vchan_find_desc(&fchan->vchan, cookie);
	if (fchan->fdesc && cookie == fchan->fdesc->vdesc.tx.cookie)
		txstate->residue = st_fdma_desc_residue(fchan, vd, true);
	else if (vd)
		txstate->residue = st_fdma_desc_residue(fchan, vd, false);
	else
		txstate->residue = 0;

	spin_unlock_irqrestore(&fchan->vchan.lock, flags);

	return ret;
}

static void st_fdma_issue_pending(struct dma_chan *chan)
{
	struct st_fdma_chan *fchan = to_st_fdma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&fchan->vchan.lock, flags);

	if (vchan_issue_pending(&fchan->vchan) && !fchan->fdesc)
		st_fdma_xfer_desc(fchan);

	spin_unlock_irqrestore(&fchan->vchan.lock, flags);
}

static int st_fdma_pause(struct dma_chan *chan)
{
	unsigned long flags;
	LIST_HEAD(head);
	struct st_fdma_chan *fchan = to_st_fdma_chan(chan);
	int ch_id = fchan->vchan.chan.chan_id;
	unsigned long cmd = FDMA_CMD_PAUSE(ch_id);

	dev_dbg(fchan->fdev->dev, "pause chan:%d\n", ch_id);

	spin_lock_irqsave(&fchan->vchan.lock, flags);
	if (fchan->fdesc)
		fdma_write(fchan->fdev, cmd, FDMA_CMD_SET_OFST);
	spin_unlock_irqrestore(&fchan->vchan.lock, flags);

	return 0;
}

static int st_fdma_resume(struct dma_chan *chan)
{
	unsigned long flags;
	unsigned long val;
	struct st_fdma_chan *fchan = to_st_fdma_chan(chan);
	int ch_id = fchan->vchan.chan.chan_id;

	dev_dbg(fchan->fdev->dev, "resume chan:%d\n", ch_id);

	spin_lock_irqsave(&fchan->vchan.lock, flags);
	if (fchan->fdesc) {
		val = fchan_read(fchan, FDMA_CH_CMD_OFST);
		val &= FDMA_CH_CMD_DATA_MASK;
		fchan_write(fchan, val, FDMA_CH_CMD_OFST);
	}
	spin_unlock_irqrestore(&fchan->vchan.lock, flags);

	return 0;
}

static int st_fdma_terminate_all(struct dma_chan *chan)
{
	unsigned long flags;
	LIST_HEAD(head);
	struct st_fdma_chan *fchan = to_st_fdma_chan(chan);
	int ch_id = fchan->vchan.chan.chan_id;
	unsigned long cmd = FDMA_CMD_PAUSE(ch_id);

	dev_dbg(fchan->fdev->dev, "terminate chan:%d\n", ch_id);

	spin_lock_irqsave(&fchan->vchan.lock, flags);
	fdma_write(fchan->fdev, cmd, FDMA_CMD_SET_OFST);
	fchan->fdesc = NULL;
	vchan_get_all_descriptors(&fchan->vchan, &head);
	spin_unlock_irqrestore(&fchan->vchan.lock, flags);
	vchan_dma_desc_free_list(&fchan->vchan, &head);

	return 0;
}

static int st_fdma_slave_config(struct dma_chan *chan,
				struct dma_slave_config *slave_cfg)
{
	struct st_fdma_chan *fchan = to_st_fdma_chan(chan);

	memcpy(&fchan->scfg, slave_cfg, sizeof(fchan->scfg));
	return 0;
}

static const struct st_fdma_driverdata fdma_mpe31_stih407_11 = {
	.name = "STiH407",
	.id = 0,
};

static const struct st_fdma_driverdata fdma_mpe31_stih407_12 = {
	.name = "STiH407",
	.id = 1,
};

static const struct st_fdma_driverdata fdma_mpe31_stih407_13 = {
	.name = "STiH407",
	.id = 2,
};

static const struct of_device_id st_fdma_match[] = {
	{ .compatible = "st,stih407-fdma-mpe31-11"
	  , .data = &fdma_mpe31_stih407_11 },
	{ .compatible = "st,stih407-fdma-mpe31-12"
	  , .data = &fdma_mpe31_stih407_12 },
	{ .compatible = "st,stih407-fdma-mpe31-13"
	  , .data = &fdma_mpe31_stih407_13 },
	{},
};
MODULE_DEVICE_TABLE(of, st_fdma_match);

static int st_fdma_parse_dt(struct platform_device *pdev,
			const struct st_fdma_driverdata *drvdata,
			struct st_fdma_dev *fdev)
{
	snprintf(fdev->fw_name, FW_NAME_SIZE, "fdma_%s_%d.elf",
		drvdata->name, drvdata->id);

	return of_property_read_u32(pdev->dev.of_node, "dma-channels",
				    &fdev->nr_channels);
}
#define FDMA_DMA_BUSWIDTHS	(BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
				 BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_3_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_4_BYTES))

static void st_fdma_free(struct st_fdma_dev *fdev)
{
	struct st_fdma_chan *fchan;
	int i;

	for (i = 0; i < fdev->nr_channels; i++) {
		fchan = &fdev->chans[i];
		list_del(&fchan->vchan.chan.device_node);
		tasklet_kill(&fchan->vchan.task);
	}
}

static int st_fdma_probe(struct platform_device *pdev)
{
	struct st_fdma_dev *fdev;
	const struct of_device_id *match;
	struct device_node *np = pdev->dev.of_node;
	const struct st_fdma_driverdata *drvdata;
	int ret, i;

	match = of_match_device((st_fdma_match), &pdev->dev);
	if (!match || !match->data) {
		dev_err(&pdev->dev, "No device match found\n");
		return -ENODEV;
	}

	drvdata = match->data;

	fdev = devm_kzalloc(&pdev->dev, sizeof(*fdev), GFP_KERNEL);
	if (!fdev)
		return -ENOMEM;

	ret = st_fdma_parse_dt(pdev, drvdata, fdev);
	if (ret) {
		dev_err(&pdev->dev, "unable to find platform data\n");
		goto err;
	}

	fdev->chans = devm_kcalloc(&pdev->dev, fdev->nr_channels,
				   sizeof(struct st_fdma_chan), GFP_KERNEL);
	if (!fdev->chans)
		return -ENOMEM;

	fdev->dev = &pdev->dev;
	fdev->drvdata = drvdata;
	platform_set_drvdata(pdev, fdev);

	fdev->irq = platform_get_irq(pdev, 0);
	if (fdev->irq < 0) {
		dev_err(&pdev->dev, "Failed to get irq resource\n");
		return -EINVAL;
	}

	ret = devm_request_irq(&pdev->dev, fdev->irq, st_fdma_irq_handler, 0,
			       dev_name(&pdev->dev), fdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq (%d)\n", ret);
		goto err;
	}

	fdev->slim_rproc = st_slim_rproc_alloc(pdev, fdev->fw_name);
	if (IS_ERR(fdev->slim_rproc)) {
		ret = PTR_ERR(fdev->slim_rproc);
		dev_err(&pdev->dev, "slim_rproc_alloc failed (%d)\n", ret);
		goto err;
	}

	/* Initialise list of FDMA channels */
	INIT_LIST_HEAD(&fdev->dma_device.channels);
	for (i = 0; i < fdev->nr_channels; i++) {
		struct st_fdma_chan *fchan = &fdev->chans[i];

		fchan->fdev = fdev;
		fchan->vchan.desc_free = st_fdma_free_desc;
		vchan_init(&fchan->vchan, &fdev->dma_device);
	}

	/* Initialise the FDMA dreq (reserve 0 & 31 for FDMA use) */
	fdev->dreq_mask = BIT(0) | BIT(31);

	dma_cap_set(DMA_SLAVE, fdev->dma_device.cap_mask);
	dma_cap_set(DMA_CYCLIC, fdev->dma_device.cap_mask);
	dma_cap_set(DMA_MEMCPY, fdev->dma_device.cap_mask);

	fdev->dma_device.dev = &pdev->dev;
	fdev->dma_device.device_alloc_chan_resources = st_fdma_alloc_chan_res;
	fdev->dma_device.device_free_chan_resources = st_fdma_free_chan_res;
	fdev->dma_device.device_prep_dma_cyclic	= st_fdma_prep_dma_cyclic;
	fdev->dma_device.device_prep_slave_sg = st_fdma_prep_slave_sg;
	fdev->dma_device.device_prep_dma_memcpy = st_fdma_prep_dma_memcpy;
	fdev->dma_device.device_tx_status = st_fdma_tx_status;
	fdev->dma_device.device_issue_pending = st_fdma_issue_pending;
	fdev->dma_device.device_terminate_all = st_fdma_terminate_all;
	fdev->dma_device.device_config = st_fdma_slave_config;
	fdev->dma_device.device_pause = st_fdma_pause;
	fdev->dma_device.device_resume = st_fdma_resume;

	fdev->dma_device.src_addr_widths = FDMA_DMA_BUSWIDTHS;
	fdev->dma_device.dst_addr_widths = FDMA_DMA_BUSWIDTHS;
	fdev->dma_device.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	fdev->dma_device.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;

	ret = dmaenginem_async_device_register(&fdev->dma_device);
	if (ret) {
		dev_err(&pdev->dev,
			"Failed to register DMA device (%d)\n", ret);
		goto err_rproc;
	}

	ret = of_dma_controller_register(np, st_fdma_of_xlate, fdev);
	if (ret) {
		dev_err(&pdev->dev,
			"Failed to register controller (%d)\n", ret);
		goto err_rproc;
	}

	dev_info(&pdev->dev, "ST FDMA engine driver, irq:%d\n", fdev->irq);

	return 0;

err_rproc:
	st_fdma_free(fdev);
	st_slim_rproc_put(fdev->slim_rproc);
err:
	return ret;
}

static int st_fdma_remove(struct platform_device *pdev)
{
	struct st_fdma_dev *fdev = platform_get_drvdata(pdev);

	devm_free_irq(&pdev->dev, fdev->irq, fdev);
	st_slim_rproc_put(fdev->slim_rproc);
	of_dma_controller_free(pdev->dev.of_node);

	return 0;
}

static struct platform_driver st_fdma_platform_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = st_fdma_match,
	},
	.probe = st_fdma_probe,
	.remove = st_fdma_remove,
};
module_platform_driver(st_fdma_platform_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("STMicroelectronics FDMA engine driver");
MODULE_AUTHOR("Ludovic.barre <Ludovic.barre@st.com>");
MODULE_AUTHOR("Peter Griffin <peter.griffin@linaro.org>");
MODULE_ALIAS("platform: " DRIVER_NAME);
