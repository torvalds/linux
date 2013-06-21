/*
 * arch/arm/mach-sun7i/dma/dma_core.c
 * (C) Copyright 2010-2015
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * liugang <liugang@reuuimllatech.com>
 *
 * sun7i dma driver
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include "dma_include.h"

/**
 * __dma_start - start dma
 * @dma_hdl:	dma handle
 *
 * find the first buf in list, remove it, and start it.
 *
 * Returns 0 if sucess, otherwise failed.
 */
u32 __dma_start(dma_hdl_t dma_hdl)
{
	buf_item *pbuf = NULL;
	dma_channel_t *pchan = (dma_channel_t *)dma_hdl;

	if(unlikely(list_empty(&pchan->buf_list))) {
		BUG();
		return -EPERM;
	}

	/* remove from list */
	pbuf = list_entry(pchan->buf_list.next, buf_item, list);
	list_del(&pbuf->list); /* only remove from list, not free it */
	/* set src addr */
	csp_dma_set_saddr(pchan, pbuf->saddr);
	/* set dst addr */
	csp_dma_set_daddr(pchan, pbuf->daddr);
	/* set byte cnt */
	csp_dma_set_bcnt(pchan, pbuf->bcnt);
	/* irq enable */
	csp_dma_irq_enable(pchan, pchan->irq_spt);
	/* set ctrl reg */
	csp_dma_set_ctrl(pchan, *(u32 *)&pchan->ctrl);

	/* start dma */
	csp_dma_start(pchan);
	pchan->state = CHAN_STA_RUNING;
	pchan->pcur_buf = pbuf;

	return 0;
}

/**
 * __dma_free_buflist - free buf in list, not include cur buf
 * @pchan:	dma handle
 */
void __dma_free_buflist(dma_channel_t *pchan)
{
	buf_item *pbuf = NULL;

	while (!list_empty(&pchan->buf_list)) {
		pbuf = list_entry(pchan->buf_list.next, buf_item, list);
		list_del(&pbuf->list);
		kmem_cache_free(g_buf_cache, pbuf);
	}
}

/**
 * __dma_free_buflist - free all buf, include cur buf
 * @pchan:	dma handle
 */
void __dma_free_allbuf(dma_channel_t *pchan)
{
	if(NULL != pchan->pcur_buf) {
		kmem_cache_free(g_buf_cache, pchan->pcur_buf);
		pchan->pcur_buf = NULL;
	}
	__dma_free_buflist(pchan);
}

/**
 * __dma_stop - stop dma and free all buf
 * @dma_hdl:	dma handle
 *
 */
void __dma_stop(dma_hdl_t dma_hdl)
{
	dma_channel_t *pchan = (dma_channel_t *)dma_hdl;

	DMA_INF("%s: state %d, buf chain: \n", __func__, (u32)pchan->state);
	//dma_dump_chain(pchan); /* for debug */

	/* check state, for debug */
	switch(pchan->state) {
	case CHAN_STA_IDLE:
		DMA_INF("%s: state idle, maybe before start or after stop, so stop the channel, free all buf list\n", __func__);
		WARN_ON(NULL != pchan->pcur_buf);
		break;
	case CHAN_STA_RUNING:
		DMA_INF("%s: state running, so stop the channel, abort the cur buf, and free extra buf\n", __func__);
		WARN_ON(NULL == pchan->pcur_buf);
		break;
	case CHAN_STA_LAST_DONE:
		DMA_INF("%s: state last done, so stop the channel, buffer already freed all, to check\n", __func__);
		WARN_ON(NULL != pchan->pcur_buf || !list_empty(&pchan->buf_list));
		break;
	default:
		BUG();
		break;
	}

	/* stop dma channle and clear irq pending */
	csp_dma_stop(pchan);
	csp_dma_clear_irqpend(pchan, CHAN_IRQ_HD | CHAN_IRQ_FD);
	/* free buffer list */
	__dma_free_allbuf(pchan);

	/* change channel state to idle */
	pchan->state = CHAN_STA_IDLE;
}

void __dma_set_hd_cb(dma_hdl_t dma_hdl, dma_cb_t *pcb)
{
	dma_channel_t *pchan = (dma_channel_t *)dma_hdl;

	WARN_ON(CHAN_STA_IDLE != pchan->state);
	pchan->hd_cb.func = pcb->func;
	pchan->hd_cb.parg = pcb->parg;
}

void __dma_set_fd_cb(dma_hdl_t dma_hdl, dma_cb_t *pcb)
{
	dma_channel_t *pchan = (dma_channel_t *)dma_hdl;

	WARN_ON(CHAN_STA_IDLE != pchan->state);
	pchan->fd_cb.func = pcb->func;
	pchan->fd_cb.parg = pcb->parg;
}

/**
 * __dma_enqueue - add buf to channel buf list
 * @dma_hdl:	dma handle
 * @src_addr:	src phys addr
 * @dst_addr:	dst phys addr
 * @byte_cnt:	buffer length
 *
 * Returns 0 if sucess, otherwise failed.
 */
u32 __dma_enqueue(dma_hdl_t dma_hdl, u32 src_addr, u32 dst_addr, u32 byte_cnt)
{
	dma_channel_t *pchan = (dma_channel_t *)dma_hdl;
	buf_item *pbuf_item = NULL;
	u32 uret = 0;

	pbuf_item = (buf_item *)kmem_cache_alloc(g_buf_cache, GFP_ATOMIC);
	if(NULL == pbuf_item) {
		uret = __LINE__;
		goto end;
	}
	pbuf_item->saddr = src_addr;
	pbuf_item->daddr = dst_addr;
	pbuf_item->bcnt = byte_cnt;

	/* add to list end */
	list_add_tail(&pbuf_item->list, &pchan->buf_list);
	/* start it if state is last done*/
	if(CHAN_STA_LAST_DONE == pchan->state) {
		DMA_INF("%s(%d): last done\n", __func__, __LINE__);
		if(0 != __dma_start(dma_hdl)) {
			uret = __LINE__;
			goto end;
		}
	}
end:
	if(0 != uret) {
		DMA_ERR("%s err, line %d\n", __func__, uret);
		if(pbuf_item)
			kmem_cache_free(g_buf_cache, pbuf_item);
	}
	return uret;
}

/**
 * dma_hdl_irq_fd - full done irq handler
 * @pchan:	dma handle
 *
 * firstly, call full done callback; then, if state running, start the next buf in list,
 * or change state to CHAN_STA_LAST_DONE if have no buffer to transfer.
 *
 * Returns 0 if sucess, otherwise failed.
 */
u32 dma_hdl_irq_fd(dma_channel_t *pchan)
{
	chan_state_e cur_state = 0;
	unsigned long flags = 0;
	u32 uret = 0;

	/*
	 * cannot lock fd_cb function, in case sw_dma_enqueue called in callback and lock again,
	 * lead to deadlock
	 */
	if(NULL != pchan->fd_cb.func)
		pchan->fd_cb.func((dma_hdl_t)pchan, pchan->fd_cb.parg);

	DMA_CHAN_LOCK(&pchan->lock, flags);
	cur_state = pchan->state;
	switch(cur_state) {
	case CHAN_STA_IDLE: /* stopped in hd_cb/fd_cb/somewhere? */
		DMA_INF("%s: state idle, stopped in cb before? just return ok!\n", __func__);
		//WARN_ON(!list_empty(&pchan->buf_list)); /* maybe new enqueue after stopped */
		goto end;
	case CHAN_STA_RUNING:
		WARN_ON(NULL == pchan->pcur_buf);
		if(unlikely(true == pchan->bconti_mode)) /* hw restart, not need soft start */
			break;
		/* for no-continue mode, free cur buf and start the next buf in chain */
		kmem_cache_free(g_buf_cache, pchan->pcur_buf);
		pchan->pcur_buf = NULL;
		/* start next if there is, or change to last done */
		if(!list_empty(&pchan->buf_list)) {
			uret = __dma_start((dma_hdl_t)pchan);
			goto end;
		} else {
			DMA_INF("%s(%d), all buf done, change state to last done\n", __func__, __LINE__);
			pchan->state = CHAN_STA_LAST_DONE; /* change state to done */
		}
		break;
	default:
		uret = __LINE__;
		goto end;
	}

end:
	DMA_CHAN_UNLOCK(&pchan->lock, flags);
	if(0 != uret)
		DMA_ERR("%s err, line %d\n", __func__, uret);
	return uret;
}

/**
 * dma_dump_chain - dump channel struct
 * @pchan:	dma handle
 */
void dma_dump_chain(dma_channel_t *pchan)
{
	buf_item *pitem = NULL;

	if(NULL == pchan) {
		DMA_ERR("%s(%d) err, para is NULL\n", __func__, __LINE__);
		return;
	}
	printk("+++++++++++%s+++++++++++\n", __func__);
	printk("  channel id:        %d\n", pchan->id);
	printk("  channel used:      %d\n", pchan->used);
	printk("  channel owner:     %s\n", pchan->owner);
	printk("  bconti_mode:       %d\n", pchan->bconti_mode);
	printk("  channel irq_spt:   0x%08x\n", pchan->irq_spt);
	printk("  channel reg_base:  0x%08x\n", pchan->reg_base);
	printk("        irq_en:  0x%08x\n", readl(DMA_IRQ_EN_REG));
	printk("        irq_pd:  0x%08x\n", readl(DMA_IRQ_PEND_REG));
	printk("     auto_gate:  0x%08x\n", readl(NDMA_AUTO_GAT_REG));
	printk("        config:  0x%08x\n", readl(pchan->reg_base + DMA_OFF_REG_CTRL));
	printk("        config:  0x%08x\n", readl(pchan->reg_base + DMA_OFF_REG_CTRL));
	printk("           src:  0x%08x\n", readl(pchan->reg_base + DMA_OFF_REG_SADR));
	printk("           dst:  0x%08x\n", readl(pchan->reg_base + DMA_OFF_REG_DADR));
	printk("         bycnt:  0x%08x\n", readl(pchan->reg_base + DMA_OFF_REG_BC));
	if(IS_DEDICATE(pchan->id))
		printk("  	  para:  0x%08x\n", readl(pchan->reg_base + DMA_OFF_REG_PARA));
	printk("  channel state:     0x%08x\n", (u32)pchan->state);
	printk("  channel hd_cb:     0x%08x\n", (u32)pchan->hd_cb.func);
	printk("  channel fd_cb:     0x%08x\n", (u32)pchan->fd_cb.func);
	printk("  ctrl reg:          0x%08x\n", *(u32 *)&pchan->ctrl);
	printk("  pcur_buf:          0x%08x\n", (u32)pchan->pcur_buf);
	printk("  buf list:\n");
	list_for_each_entry(pitem, &pchan->buf_list, list) {
		printk("         saddr: 0x%08x, daddr 0x%08x, bcnt 0x%08x\n", pitem->saddr, pitem->daddr, pitem->bcnt);
	}
	printk("-----------%s-----------\n", __func__);
}

/**
 * dma_request_init - init some member after requested
 * @pchan:	dma handle
 */
void dma_request_init(dma_channel_t *pchan)
{
	INIT_LIST_HEAD(&pchan->buf_list);
	pchan->state = CHAN_STA_IDLE;
	pchan->pcur_buf = NULL;
}

/**
 * dma_release - release dma channel, for single mode
 * @dma_hdl:	dma handle
 *
 * return 0 if success, the err line number if not
 */
void dma_release(dma_hdl_t dma_hdl)
{
	unsigned long 	flags = 0;
	dma_channel_t *pchan = (dma_channel_t *)dma_hdl;

	DMA_CHAN_LOCK(&pchan->lock, flags);

	/* if not idle, call stop first */
	if(CHAN_STA_IDLE != pchan->state) {
		DMA_INF("%s(%d) maybe err: state(%d) not idle, call stop dma first!\n", __func__, __LINE__, pchan->state);
		__dma_stop(dma_hdl);
	}

	//memset(pchan, 0, sizeof(*pchan)); /* donot do that, because id...should not be cleared */
	pchan->used = 0;
	memset(pchan->owner, 0, sizeof(pchan->owner));
	pchan->irq_spt = CHAN_IRQ_NO;
	pchan->bconti_mode = false;
	memset(&pchan->ctrl, 0, sizeof(pchan->ctrl));
	memset(&pchan->hd_cb, 0, sizeof(pchan->hd_cb));
	memset(&pchan->fd_cb, 0, sizeof(pchan->fd_cb));
	/* maybe enqueued but not started, so free buf */
	WARN_ON(NULL != pchan->pcur_buf);
	__dma_free_buflist(pchan);

	DMA_CHAN_UNLOCK(&pchan->lock, flags);
}

/**
 * dma_ctrl - dma ctrl, for single mode
 * @dma_hdl:	dma handle
 * @op:		dma operation type
 * @parg:	arg for the op
 *
 * Returns 0 if sucess, the err line number if failed.
 */
u32 dma_ctrl(dma_hdl_t dma_hdl, dma_op_type_e op, void *parg)
{
	dma_channel_t *pchan = (dma_channel_t *)dma_hdl;
	unsigned long flags = 0;
	u32 uret = 0;

	DMA_CHAN_LOCK(&pchan->lock, flags);

	switch(op) {
	case DMA_OP_START:
		if(unlikely(CHAN_STA_IDLE != pchan->state)) {
			DMA_ERR("%s(%d): start when state is not idle, to check!\n", __func__, __LINE__);
			goto end;
		}
		uret = __dma_start(dma_hdl);
		break;
	case DMA_OP_STOP:
		__dma_stop(dma_hdl);
		break;
	case DMA_OP_GET_STATUS: /* only for dedicate dma */
		*(u32 *)parg = csp_dma_get_status(pchan);
		break;
	case DMA_OP_GET_BYTECNT_LEFT: /* bc_mode 1, so readback is left bytes */
		*(u32 *)parg = csp_dma_get_bcnt(pchan);
		break;
	case DMA_OP_SET_HD_CB:
		BUG_ON(NULL == parg);
		__dma_set_hd_cb(dma_hdl, (dma_cb_t *)parg);
		break;
	case DMA_OP_SET_FD_CB:
		BUG_ON(NULL == parg);
		__dma_set_fd_cb(dma_hdl, (dma_cb_t *)parg);
		break;
	case DMA_OP_SET_PARA_REG:
		BUG_ON(NULL == parg);
		csp_dma_set_para(pchan, *(dma_para_t *)parg);
		break;
	case DMA_OP_SET_WAIT_STATE: /* para is 0~7 */
		BUG_ON(NULL == parg);
		csp_ndma_set_wait_state(pchan, *(u32 *)parg);
		break;
	case DMA_OP_SET_SECURITY:
		BUG_ON(NULL == parg);
		csp_dma_set_security(pchan, *(u32 *)parg);
		break;
	default:
		uret = __LINE__;
		goto end;
	}

end:
	DMA_CHAN_UNLOCK(&pchan->lock, flags);
	if(0 != uret)
		DMA_ERR("%s err, line %d, dma_hdl 0x%08x\n", __func__, uret, (u32)dma_hdl);
	return uret;
}

/**
 * dma_config - config dma hardware paras
 * @dma_hdl:	dma handle
 * @pcfg:	dma cofig para
 *
 */
void dma_config(dma_hdl_t dma_hdl, dma_config_t *pcfg)
{
	dma_channel_t *pchan = (dma_channel_t *)dma_hdl;
	bool dedicate;
	dma_ctrl_u ctrl;

	BUG_ON(unlikely(NULL == pchan));
	/* get cfg from hw reg */
	ctrl = csp_dma_get_ctrl(pchan);
	dedicate = IS_DEDICATE(pchan->id);
	if(dedicate) {
		/*
		 * BC mode select, 0: normal mode(BC reg readback is bytes already transferred),
		 * 1: remain mode(BC reg readback is bytes to be transferred)
		 */
		ctrl.d.bc_mod = 1;
		ctrl.d.conti = pcfg->bconti_mode;
		ctrl.d.dst_addr_mode = pcfg->address_type.dst_addr_mode;
		ctrl.d.dst_bst_len = pcfg->xfer_type.dst_bst_len;
		ctrl.d.dst_data_width = pcfg->xfer_type.dst_data_width;
		ctrl.d.dst_drq = pcfg->dst_drq_type;
		//ctrl.d.dst_sec = pcfg->dst_secu;
		ctrl.d.dst_sec = 0;
		ctrl.d.src_addr_mode = pcfg->address_type.src_addr_mode;
		ctrl.d.src_bst_len = pcfg->xfer_type.src_bst_len;
		ctrl.d.src_data_width = pcfg->xfer_type.src_data_width;
		ctrl.d.src_drq = pcfg->src_drq_type;
		//ctrl.d.src_sec = pcfg->src_secu;
		ctrl.d.src_sec = 0;
		ctrl.d.loading = 0; /* not start */
	} else {
		ctrl.n.bc_mod = 1;
		ctrl.n.conti = pcfg->bconti_mode;
		ctrl.n.dst_addr_type = pcfg->address_type.dst_addr_mode;
		ctrl.n.dst_bst_len = pcfg->xfer_type.dst_bst_len;
		ctrl.n.dst_data_width = pcfg->xfer_type.dst_data_width;
		ctrl.n.dst_drq = pcfg->dst_drq_type;
		//ctrl.n.dst_sec = pcfg->dst_secu;
		ctrl.n.dst_sec = 0;
		ctrl.n.src_addr_type = pcfg->address_type.src_addr_mode;
		ctrl.n.src_bst_len = pcfg->xfer_type.src_bst_len;
		ctrl.n.src_data_width = pcfg->xfer_type.src_data_width;
		ctrl.n.src_drq = pcfg->src_drq_type;
		//ctrl.n.src_sec = pcfg->src_secu;
		ctrl.n.src_sec = 0;
		//ctrl.n.wait_state = pcfg->wait_state;
		ctrl.n.wait_state = 0;
		ctrl.n.loading = 0; /* not start */
	}
	pchan->ctrl = ctrl;
	pchan->bconti_mode = pcfg->bconti_mode;
	pchan->irq_spt = pcfg->irq_spt;
}

/**
 * sw_dma_enqueue - add buf to list
 * @dma_hdl:	dma handle
 * @src_addr:	buffer src phys addr
 * @dst_addr:	buffer dst phys addr
 * @byte_cnt:	buffer byte cnt
 *
 * Returns 0 if sucess, the err line number if failed.
 */
u32 dma_enqueue(dma_hdl_t dma_hdl, u32 src_addr, u32 dst_addr, u32 byte_cnt)
{
	u32 uret = 0;
	unsigned long flags = 0;
	dma_channel_t *pchan = (dma_channel_t *)dma_hdl;

	DMA_CHAN_LOCK(&pchan->lock, flags);

	/* cannot enqueue more than one buffer in single_continue mode */
	if(true == pchan->bconti_mode
		&& !list_empty(&pchan->buf_list)) {
		uret = __LINE__;
		goto end;
	}
	if(0 != __dma_enqueue(dma_hdl, src_addr, dst_addr, byte_cnt)) {
		uret = __LINE__;
		goto end;
	}

end:
	DMA_CHAN_UNLOCK(&pchan->lock, flags);
	if(0 != uret)
		DMA_ERR("%s err, line %d\n", __func__, uret);
	return uret;
}

