/*
 * arch/arm/mach-sun7i/dma/dma_common.h
 * (C) Copyright 2010-2015
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * liugang <liugang@reuuimllatech.com>
 *
 * sun7i dma common header file
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __DMA_COMMON_H
#define __DMA_COMMON_H

#include <linux/spinlock.h>

/* dma print macro */
#define DMA_DBG_LEVEL		3

#if (DMA_DBG_LEVEL == 1)
	#define DMA_DBG(format,args...)   printk("[dma-dbg] "format,##args)
	#define DMA_INF(format,args...)   printk("[dma-inf] "format,##args)
	#define DMA_ERR(format,args...)   printk("[dma-err] "format,##args)
#elif (DMA_DBG_LEVEL == 2)
	#define DMA_DBG(format,args...)   do{}while(0)
	#define DMA_INF(format,args...)   printk("[dma-inf] "format,##args)
	#define DMA_ERR(format,args...)   printk("[dma-err] "format,##args)
#elif (DMA_DBG_LEVEL == 3)
	#define DMA_DBG(format,args...)   do{}while(0)
	#define DMA_INF(format,args...)   do{}while(0)
	#define DMA_ERR(format,args...)   printk("[dma-err] "format,##args)
#endif

/* dma channel number */
#define NR_CHAN_NORMAL		(8)
#define NR_CHAN_DEDICATE	(8)
#define DMA_CHAN_TOTAL		(NR_CHAN_NORMAL + NR_CHAN_DEDICATE)

typedef struct {
	u32 src_drq 		: 5; /* ndma src drq type */
	u32 src_addr_type 	: 1; /* ndma src address type */
	u32 src_sec 		: 1; /* dma src security */
	u32 src_bst_len 	: 2; /* dma src burst length */
	u32 src_data_width 	: 2; /* ndma src data width */
	u32 rsv0 		: 4; /* reserve */
	u32 bc_mod 		: 1; /* bc mode select */
	u32 dst_drq 		: 5; /* ndma dst drq type */
	u32 dst_addr_type 	: 1; /* ndma dst address type */
	u32 dst_sec 		: 1; /* dma dst security */
	u32 dst_bst_len 	: 2; /* dma dst burst length */
	u32 dst_data_width 	: 2; /* ndma dst data width */
	u32 wait_state 		: 3; /* dma wait state */
	u32 conti 		: 1; /* dma continuous mode enable */
	u32 loading 		: 1; /* dma loading */
}ndma_ctrl_t;

typedef struct {
	u32 src_drq 		: 5; /* ddma src drq type */
	u32 src_addr_mode 	: 2; /* dma src address mode */
	u32 src_bst_len 	: 2; /* dma src burst length */
	u32 src_data_width 	: 2; /* dma src data width */
	u32 rsv0 		: 1; /* reserve */
	u32 src_sec 		: 1; /* dma src security */
	u32 rsv1 		: 2; /* reserve */
	u32 bc_mod 		: 1; /* bc mode select */
	u32 dst_drq 		: 5; /* ddma dst drq type */
	u32 dst_addr_mode 	: 2; /* dma dst address mode */
	u32 dst_bst_len 	: 2; /* dma dst burst length */
	u32 dst_data_width 	: 2; /* dma dst data width */
	u32 rsv2 		: 1; /* reserve */
	u32 dst_sec 		: 1; /* dma dst security */
	u32 conti 		: 1; /* dma continuous mode enable */
	u32 busy 		: 1; /* dma busy status */
	u32 loading 		: 1; /* dma loading */
}ddma_ctrl_t;

typedef union {
	ndma_ctrl_t	n;
	ddma_ctrl_t	d;
}dma_ctrl_u;

/* dam channel state for single mode */
typedef enum {
	CHAN_STA_IDLE,  	/* maybe before start or after stop */
	CHAN_STA_RUNING,	/* transferring */
	CHAN_STA_LAST_DONE	/* the last buffer has done, in this state, sw_dma_enqueue will start dma */
}chan_state_e;

/* buf item define */
typedef struct {
	u32	saddr;		/* src phys address */
	u32	daddr;		/* dst phys address */
	u32	bcnt;		/* bytes cnt to transfer */
	struct list_head list;	/* list node */
}buf_item;

/* dma channel owner name max len */
#define MAX_NAME_LEN	32

/* define dma channel struct */
typedef struct {
	u32		used;     	/* 1 used, 0 unuse */
	u32		id;     	/* channel id, 0~15 */
	char 		owner[MAX_NAME_LEN]; /* dma chnnnel owner name */
	u32		reg_base;	/* ctrl reg virtual addr */
	dma_ctrl_u 	ctrl;		/* ctrl reg setting */
	dma_cb_t	hd_cb;		/* half done call back func */
	dma_cb_t	fd_cb;		/* full done call back func */
	chan_state_e	state;		/* channel state, in software */
	u32 		irq_spt;	/* channel irq support, used for irq handler, only enabled
					 * then can call irq callback
					 */
	u32		bconti_mode;	/* cotinue mode, same in ctrl, add here in order easy to use */
	spinlock_t 	lock;		/* channel lock for buf list and reg ops */
	buf_item	*pcur_buf;	/* cur buf in transferring */
	struct list_head buf_list;	/* buf list head */
}dma_channel_t;

/* dma manager struct */
struct dma_mgr_t {
	dma_channel_t chnl[DMA_CHAN_TOTAL];
};
extern struct dma_mgr_t g_dma_mgr;
extern struct kmem_cache *g_buf_cache;

/* dma channel lock */
#define DMA_CHAN_LOCK_INIT(lock)	spin_lock_init((lock))
#define DMA_CHAN_LOCK_DEINIT(lock)	do{}while(0)
#define DMA_CHAN_LOCK(lock, flag)	spin_lock_irqsave((lock), (flag))
#define DMA_CHAN_UNLOCK(lock, flag)	spin_unlock_irqrestore((lock), (flag))

#endif  /* __DMA_COMMON_H */

