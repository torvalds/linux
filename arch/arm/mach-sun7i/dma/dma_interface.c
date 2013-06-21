/*
 * arch/arm/mach-sun7i/dma/dma_interface.c
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

/* dma manager */
struct dma_mgr_t g_dma_mgr; /* compile warning if "g_dma_mgr = {0}" */

/* dma channel request lock */
static DEFINE_MUTEX(dma_mutex);

/**
 * dma_handle_is_valid - check if dma handle is valid
 * @dma_hdl:	dma handle
 *
 * return true if vaild, false otherwise
 */
bool inline dma_handle_is_valid(dma_hdl_t dma_hdl)
{
	dma_channel_t *pchan = (dma_channel_t *)dma_hdl;
	return pchan && pchan->used && pchan->id < DMA_CHAN_TOTAL;
}

/**
 * __chan_is_free - check if channel is free
 * @pchan:	dma handle
 *
 * return true if channel is free, false if not
 *
 * NOTE: can only be called in sw_dma_request recently, becase
 * should be locked
 */
static bool __chan_is_free(dma_channel_t *pchan)
{
	if(0 == pchan->used
		&& 0 == pchan->owner[0]
		//&& CHAN_IRQ_NO == pchan->irq_spt /* maybe not use dma irq? */
		&& NULL == pchan->hd_cb.func
		&& NULL == pchan->fd_cb.func
		&& CHAN_STA_IDLE == pchan->state
		)
		return true;
	else {
		dma_dump_chain(pchan);
		return false;
	}
}

/**
 * __dma_channel_already_exist - check if channel already requested by others
 * @name:	channel name
 *
 * return true if channel already requested, false if not
 */
bool __dma_channel_already_exist(char *name)
{
	u32 i = 0;

	if(NULL == name)
		return false;
	for(i = 0; i < DMA_CHAN_TOTAL; i++) {
		if(1 == g_dma_mgr.chnl[i].used && !strcmp(g_dma_mgr.chnl[i].owner, name))
			return true;
	}
	return false;
}

/**
 * sw_dma_request - request a dma channel
 * @name:	dma channel name
 * @type:	channel type, normal or dedicate
 *
 * Returns handle to the channel if success, NULL if failed.
 */
dma_hdl_t sw_dma_request(char * name, dma_chan_type_e type)
{
	u32 i, num;
	u32 usign = 0;
	dma_channel_t *pchan = NULL;

	DMA_DBG("%s: name %s, chan type %d\n", __func__, name, (u32)type);
	if((name && strlen(name) >= MAX_NAME_LEN) || (type != CHAN_NORAML && type != CHAN_DEDICATE)) {
		DMA_ERR("%s: para err, name %s, type %d\n", __func__, name, (u32)type);
		return NULL;
	}

	mutex_lock(&dma_mutex);
	/* check if already exist */
	if(NULL != name && __dma_channel_already_exist(name)) {
		usign = __LINE__;
		goto end;
	}
	/* get a free channel */
	if(CHAN_NORAML == type)
		i = 0;
	else
		i = 8;
	num = i + 8;
	for(; i < num ; i++) {
		if(0 == g_dma_mgr.chnl[i].used) {
			WARN_ON(!__chan_is_free(&g_dma_mgr.chnl[i]));
			break;
		}
	}
	if(num == i) {
		usign = __LINE__;
		goto end;
	}

	/* init channel */
	pchan = &g_dma_mgr.chnl[i];
	pchan->used = 1;
	dma_request_init(pchan);
	if(NULL != name)
		strcpy(pchan->owner, name);

end:
	mutex_unlock(&dma_mutex);
	if(0 != usign)
		DMA_ERR("%s err, line %d\n", __func__, usign);
	else
		DMA_DBG("%s: success, channel id %d\n", __func__, i);
	return (dma_hdl_t)pchan;
}
EXPORT_SYMBOL(sw_dma_request);

/**
 * sw_dma_release - free a dma channel
 * @dma_hdl:	dma handle
 *
 * Returns 0 if sucess, otherwise failed
 */
u32 sw_dma_release(dma_hdl_t dma_hdl)
{
	BUG_ON(unlikely(!dma_handle_is_valid(dma_hdl)));
	dma_release(dma_hdl);
	return 0;
}
EXPORT_SYMBOL(sw_dma_release);

/**
 * sw_dma_ctl - dma ctrl operation
 * @dma_hdl:	dma handle
 * @op:		dma operation type
 * @parg:	arg for the op
 *
 * Returns 0 if sucess, otherwise failed
 */
u32 sw_dma_ctl(dma_hdl_t dma_hdl, dma_op_type_e op, void *parg)
{
	BUG_ON(unlikely(!dma_handle_is_valid(dma_hdl)));
	return dma_ctrl(dma_hdl, op, parg);
}
EXPORT_SYMBOL(sw_dma_ctl);

/**
 * sw_dma_config - config dma hardware paras
 * @dma_hdl:	dma handle
 * @pcfg:	dma cofig para
 *
 * Returns 0 if sucess, otherwise failed
 */
u32 sw_dma_config(dma_hdl_t dma_hdl, dma_config_t *pcfg)
{
	BUG_ON(unlikely(!dma_handle_is_valid(dma_hdl)));
	dma_config(dma_hdl, pcfg);
	return 0;
}
EXPORT_SYMBOL(sw_dma_config);

/**
 * sw_dma_enqueue - add buf to list
 * @dma_hdl:	dma handle
 * @src_addr:	buffer src phys addr
 * @dst_addr:	buffer dst phys addr
 * @byte_cnt:	buffer byte cnt
 *
 * Returns 0 if sucess, otherwise failed
 */
u32 sw_dma_enqueue(dma_hdl_t dma_hdl, u32 src_addr, u32 dst_addr, u32 byte_cnt)
{
	BUG_ON(unlikely(!dma_handle_is_valid(dma_hdl)));
	return dma_enqueue(dma_hdl, src_addr, dst_addr, byte_cnt);
}
EXPORT_SYMBOL(sw_dma_enqueue);

/**
 * sw_dma_getposition - get src and dst position
 * @dma_hdl:	dma handle
 * @psrc:	stored the src addr got
 * @pdst:	stored the dst addr got
 *
 * Returns 0 if sucess, otherwise failed
 */
int sw_dma_getposition(dma_hdl_t dma_hdl, u32 *psrc, u32 *pdst)
{
	dma_channel_t *pchan = (dma_channel_t *)dma_hdl;
	unsigned long flags;
	u32 saddr, daddr, reamin;

	BUG_ON(unlikely(!dma_handle_is_valid(dma_hdl)));
	BUG_ON(unlikely(NULL == psrc || NULL == pdst));

	DMA_CHAN_LOCK(&pchan->lock, flags);
	/* get src/dst start addr */
	saddr = csp_dma_get_saddr(pchan);
	daddr = csp_dma_get_daddr(pchan);
	/* get remain bytes */
	reamin = csp_dma_get_bcnt(pchan);
	/* note: tha caller use "period - reamin" to get transferred bytes */
	*psrc = saddr - reamin;
	*pdst = daddr - reamin;
	DMA_CHAN_UNLOCK(&pchan->lock, flags);
	DMA_DBG("%s: get *psrc 0x%08x, *pdst 0x%08x\n", __func__, *psrc, *pdst);
	return 0;
}
EXPORT_SYMBOL(sw_dma_getposition);

/**
 * sw_dma_dump_chan - dump dma chain
 * @dma_hdl:	dma handle
 */
void sw_dma_dump_chan(dma_hdl_t dma_hdl)
{
	dma_channel_t *pchan = (dma_channel_t *)dma_hdl;
	unsigned long	flags = 0;

	BUG_ON(unlikely(!dma_handle_is_valid(dma_hdl)));

	DMA_CHAN_LOCK(&pchan->lock, flags);
	dma_dump_chain(pchan);
	DMA_CHAN_UNLOCK(&pchan->lock, flags);
}
EXPORT_SYMBOL(sw_dma_dump_chan);

