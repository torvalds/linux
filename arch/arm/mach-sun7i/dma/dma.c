/*
 * arch/arm/mach-sun7i/dma/dma.c
 * (C) Copyright 2010-2015
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * liugang <liugang@reuuimllatech.com>
 *
 * sun7i dma driver interface
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include "dma_include.h"
#include <linux/pm.h>

struct kmem_cache *g_buf_cache;

/**
 * handle_dma_irq - dma irq handle
 * @pchan:	dma channel handle
 * @upend_bits:	irq pending for the channel
 *
 */
void handle_dma_irq(dma_channel_t *pchan, u32 upend_bits)
{
	u32	irq_spt = 0;

	WARN_ON(0 == upend_bits);
	irq_spt = pchan->irq_spt;

	/* deal half done */
	if(upend_bits & CHAN_IRQ_HD) {
		csp_dma_clear_irqpend(pchan, CHAN_IRQ_HD);
		if((irq_spt & CHAN_IRQ_HD) && NULL != pchan->hd_cb.func)
			pchan->hd_cb.func((dma_hdl_t)pchan, pchan->hd_cb.parg);
	}
	/* deal queue done */
	if(upend_bits & CHAN_IRQ_FD) {
		csp_dma_clear_irqpend(pchan, CHAN_IRQ_FD);
		if(irq_spt & CHAN_IRQ_FD)
			dma_hdl_irq_fd(pchan);
	}
}

/**
 * __dma_irq_hdl - dma irq process function
 * @irq:	dma physical irq num
 * @dev:	para passed in request_irq function
 *
 * we cannot lock __dma_irq_hdl through,
 * because sw_dma_enqueue maybe called in cb,
 * which will result in deadlock
 *
 * Returns 0 if sucess, the err line number if failed.
 */
irqreturn_t __dma_irq_hdl(int irq, void *dev)
{
	u32 i = 0;
	u32 pend_bits = 0;
	dma_channel_t *pchan = NULL;
	struct dma_mgr_t *pdma_mgr = NULL;

	DMA_DBG("%s(%d), dma en 0x%08x, pd 0x%08x\n", __func__, __LINE__, DMA_READ_REG(DMA_IRQ_EN_REG), DMA_READ_REG(DMA_IRQ_PEND_REG));

	pdma_mgr = (struct dma_mgr_t *)dev;
	for(i = 0; i < DMA_CHAN_TOTAL; i++) {
		pchan = &pdma_mgr->chnl[i];
		pend_bits = csp_dma_get_irqpend(pchan);
		if(0 == pend_bits)
			continue;

		handle_dma_irq(pchan, pend_bits);
	}
	return IRQ_HANDLED;
}

static void __dma_cache_ctor(void *p)
{
	memset(p, 0, sizeof(buf_item));
}

/**
 * __dma_init - initial the dma manager, request irq
 * @device:	platform device pointer
 *
 * Returns 0 if sucess, the err line number if failed.
 */
int __dma_init(struct platform_device *device)
{
	int ret = 0;
	int i = 0;
	dma_channel_t *pchan = NULL;

	/* init dma controller */
	csp_dma_init();

	/* initial the dma manager */
	memset(&g_dma_mgr, 0, sizeof(g_dma_mgr));
	for(i = 0; i < DMA_CHAN_TOTAL; i++) {
		pchan 		= &g_dma_mgr.chnl[i];
		pchan->used 	= 0;
		pchan->id 	= i;
		pchan->reg_base = (u32)DMA_CTRL_REG(i);
		pchan->irq_spt 	= CHAN_IRQ_NO;
		pchan->bconti_mode = false;
		DMA_CHAN_LOCK_INIT(&pchan->lock);
		pchan->state = CHAN_STA_IDLE;
	}

	/* alloc dma pool for des list */
	g_buf_cache = kmem_cache_create("dma_desc", sizeof(buf_item), 0, SLAB_HWCACHE_ALIGN, __dma_cache_ctor);
	if(NULL == g_buf_cache) {
		ret = __LINE__;
		goto end;
	}
	DMA_INF("%s(%d): g_buf_cache 0x%08x\n", __func__, __LINE__, (u32)g_buf_cache);

	/* register dma interrupt */
	ret = request_irq(AW_IRQ_DMA, __dma_irq_hdl, IRQF_DISABLED, "dma_irq", (void *)&g_dma_mgr);
	if(ret) {
		DMA_ERR("%s err: request_irq return %d\n", __func__, ret);
		ret = __LINE__;
		goto end;
	}
	DMA_INF("%s success\n", __func__);

end:
	if(0 != ret) {
		DMA_ERR("%s err, line %d\n", __func__, ret);
		if (NULL != g_buf_cache) {
			kmem_cache_destroy(g_buf_cache);
			g_buf_cache = NULL;
		}
		for(i = 0; i < DMA_CHAN_TOTAL; i++)
			DMA_CHAN_LOCK_DEINIT(&g_dma_mgr.chnl[i].lock);
	}
	return ret;
}

/**
 * __dma_deinit - deinit the dma manager, free irq
 *
 * Returns 0 if sucess, the err line number if failed.
 */
int __dma_deinit(void)
{
	u32 	i = 0;

	DMA_INF("%s, line %d\n", __func__, __LINE__);
	/* free dma irq */
	free_irq(AW_IRQ_DMA, (void *)&g_dma_mgr);
	/* free kcache */
	if (NULL != g_buf_cache) {
		kmem_cache_destroy(g_buf_cache);
		g_buf_cache = NULL;
	}
	for(i = 0; i < DMA_CHAN_TOTAL; i++)
		DMA_CHAN_LOCK_DEINIT(&g_dma_mgr.chnl[i].lock);
	/* clear dma manager */
	memset(&g_dma_mgr, 0, sizeof(g_dma_mgr));
	return 0;
}

/**
 * dma_drv_probe - dma driver inital function.
 * @dev:	platform device pointer
 *
 * Returns 0 if success, otherwise return the err line number.
 */
static int __devinit dma_drv_probe(struct platform_device *dev)
{
	return __dma_init(dev);
}

/**
 * dma_drv_remove - dma driver deinital function.
 * @dev:	platform device pointer
 *
 * Returns 0 if success, otherwise means err.
 */
static int __devexit dma_drv_remove(struct platform_device *dev)
{
	return __dma_deinit();
}

#ifdef CONFIG_PM
/**
 * dma_drv_suspend - dma driver suspend function.
 * @dev:	platform device pointer
 * @state:	power state
 *
 * Returns 0 if success, otherwise means err.
 */
int dma_drv_suspend(struct device *dev)
{
	if(NORMAL_STANDBY == standby_type)
 		DMA_INF("%s(%d): normal standby\n", __func__, __LINE__);
	else if(SUPER_STANDBY == standby_type) {
 		DMA_INF("%s(%d): super standby\n", __func__, __LINE__);
		if(0 != dma_clk_deinit())
			DMA_ERR("%s err, dma_clk_deinit failed\n", __func__);
	}
	return 0;
}

/**
 * dma_drv_resume - dma driver resume function.
 * @dev:	platform device pointer
 *
 * Returns 0 if success, otherwise means err.
 */
int dma_drv_resume(struct device *dev)
{
	if(NORMAL_STANDBY == standby_type)
 		DMA_INF("%s(%d): normal standby\n", __func__, __LINE__);
	else if(SUPER_STANDBY == standby_type) {
 		DMA_INF("%s(%d): super standby\n", __func__, __LINE__);
		if(0 != dma_clk_init())
			DMA_ERR("%s err, dma_clk_init failed\n", __func__);
	}
	return 0;
}

static const struct dev_pm_ops sw_dmac_pm = {
	.suspend	= dma_drv_suspend,
	.resume		= dma_drv_resume,
};
#endif
static struct platform_driver sw_dmac_driver = {
	.probe          = dma_drv_probe,
	.remove         = __devexit_p(dma_drv_remove),
	.driver         = {
		.name   = "sw_dmac",
		.owner  = THIS_MODULE,
#ifdef CONFIG_PM
		.pm 	= &sw_dmac_pm,
#endif
		},
};

/**
 * drv_dma_init - dma driver register function
 *
 * Returns 0 if success, otherwise means err.
 */
static int __init drv_dma_init(void)
{
	if(platform_driver_register(&sw_dmac_driver))
		printk("%s(%d) err: platform_driver_register failed\n", __func__, __LINE__);
	return 0;
}
arch_initcall(drv_dma_init);

