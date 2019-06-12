/*
 * Qualcomm Technologies HIDMA data structures
 *
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef QCOM_HIDMA_H
#define QCOM_HIDMA_H

#include <linux/kfifo.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>

#define HIDMA_TRE_SIZE			32 /* each TRE is 32 bytes  */
#define HIDMA_TRE_CFG_IDX		0
#define HIDMA_TRE_LEN_IDX		1
#define HIDMA_TRE_SRC_LOW_IDX		2
#define HIDMA_TRE_SRC_HI_IDX		3
#define HIDMA_TRE_DEST_LOW_IDX		4
#define HIDMA_TRE_DEST_HI_IDX		5

enum tre_type {
	HIDMA_TRE_MEMCPY = 3,
	HIDMA_TRE_MEMSET = 4,
};

struct hidma_tre {
	atomic_t allocated;		/* if this channel is allocated	    */
	bool queued;			/* flag whether this is pending     */
	u16 status;			/* status			    */
	u32 idx;			/* index of the tre		    */
	u32 dma_sig;			/* signature of the tre		    */
	const char *dev_name;		/* name of the device		    */
	void (*callback)(void *data);	/* requester callback		    */
	void *data;			/* Data associated with this channel*/
	struct hidma_lldev *lldev;	/* lldma device pointer		    */
	u32 tre_local[HIDMA_TRE_SIZE / sizeof(u32) + 1]; /* TRE local copy  */
	u32 tre_index;			/* the offset where this was written*/
	u32 int_flags;			/* interrupt flags		    */
	u8 err_info;			/* error record in this transfer    */
	u8 err_code;			/* completion code		    */
};

struct hidma_lldev {
	bool msi_support;		/* flag indicating MSI support    */
	bool initialized;		/* initialized flag               */
	u8 trch_state;			/* trch_state of the device	  */
	u8 evch_state;			/* evch_state of the device	  */
	u8 chidx;			/* channel index in the core	  */
	u32 nr_tres;			/* max number of configs          */
	spinlock_t lock;		/* reentrancy                     */
	struct hidma_tre *trepool;	/* trepool of user configs */
	struct device *dev;		/* device			  */
	void __iomem *trca;		/* Transfer Channel address       */
	void __iomem *evca;		/* Event Channel address          */
	struct hidma_tre
		**pending_tre_list;	/* Pointers to pending TREs	  */
	atomic_t pending_tre_count;	/* Number of TREs pending	  */

	void *tre_ring;			/* TRE ring			  */
	dma_addr_t tre_dma;		/* TRE ring to be shared with HW  */
	u32 tre_ring_size;		/* Byte size of the ring	  */
	u32 tre_processed_off;		/* last processed TRE		  */

	void *evre_ring;		/* EVRE ring			   */
	dma_addr_t evre_dma;		/* EVRE ring to be shared with HW  */
	u32 evre_ring_size;		/* Byte size of the ring	   */
	u32 evre_processed_off;		/* last processed EVRE		   */

	u32 tre_write_offset;           /* TRE write location              */
	struct tasklet_struct task;	/* task delivering notifications   */
	DECLARE_KFIFO_PTR(handoff_fifo,
		struct hidma_tre *);    /* pending TREs FIFO               */
};

struct hidma_desc {
	struct dma_async_tx_descriptor	desc;
	/* link list node for this channel*/
	struct list_head		node;
	u32				tre_ch;
};

struct hidma_chan {
	bool				paused;
	bool				allocated;
	char				dbg_name[16];
	u32				dma_sig;
	dma_cookie_t			last_success;

	/*
	 * active descriptor on this channel
	 * It is used by the DMA complete notification to
	 * locate the descriptor that initiated the transfer.
	 */
	struct hidma_dev		*dmadev;
	struct hidma_desc		*running;

	struct dma_chan			chan;
	struct list_head		free;
	struct list_head		prepared;
	struct list_head		queued;
	struct list_head		active;
	struct list_head		completed;

	/* Lock for this structure */
	spinlock_t			lock;
};

struct hidma_dev {
	int				irq;
	int				chidx;
	u32				nr_descriptors;
	int				msi_virqbase;

	struct hidma_lldev		*lldev;
	void				__iomem *dev_trca;
	struct resource			*trca_resource;
	void				__iomem *dev_evca;
	struct resource			*evca_resource;

	/* used to protect the pending channel list*/
	spinlock_t			lock;
	struct dma_device		ddev;

	struct dentry			*debugfs;

	/* sysfs entry for the channel id */
	struct device_attribute		*chid_attrs;

	/* Task delivering issue_pending */
	struct tasklet_struct		task;
};

int hidma_ll_request(struct hidma_lldev *llhndl, u32 dev_id,
			const char *dev_name,
			void (*callback)(void *data), void *data, u32 *tre_ch);

void hidma_ll_free(struct hidma_lldev *llhndl, u32 tre_ch);
enum dma_status hidma_ll_status(struct hidma_lldev *llhndl, u32 tre_ch);
bool hidma_ll_isenabled(struct hidma_lldev *llhndl);
void hidma_ll_queue_request(struct hidma_lldev *llhndl, u32 tre_ch);
void hidma_ll_start(struct hidma_lldev *llhndl);
int hidma_ll_disable(struct hidma_lldev *lldev);
int hidma_ll_enable(struct hidma_lldev *llhndl);
void hidma_ll_set_transfer_params(struct hidma_lldev *llhndl, u32 tre_ch,
	dma_addr_t src, dma_addr_t dest, u32 len, u32 flags, u32 txntype);
void hidma_ll_setup_irq(struct hidma_lldev *lldev, bool msi);
int hidma_ll_setup(struct hidma_lldev *lldev);
struct hidma_lldev *hidma_ll_init(struct device *dev, u32 max_channels,
			void __iomem *trca, void __iomem *evca,
			u8 chidx);
int hidma_ll_uninit(struct hidma_lldev *llhndl);
irqreturn_t hidma_ll_inthandler(int irq, void *arg);
irqreturn_t hidma_ll_inthandler_msi(int irq, void *arg, int cause);
void hidma_cleanup_pending_tre(struct hidma_lldev *llhndl, u8 err_info,
				u8 err_code);
void hidma_debug_init(struct hidma_dev *dmadev);
void hidma_debug_uninit(struct hidma_dev *dmadev);
#endif
