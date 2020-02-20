/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 *
 */

#ifndef _MHI_INT_H
#define _MHI_INT_H

#include <linux/mhi.h>

extern struct bus_type mhi_bus_type;

/* MHI transfer completion events */
enum mhi_ev_ccs {
	MHI_EV_CC_INVALID = 0x0,
	MHI_EV_CC_SUCCESS = 0x1,
	MHI_EV_CC_EOT = 0x2, /* End of transfer event */
	MHI_EV_CC_OVERFLOW = 0x3,
	MHI_EV_CC_EOB = 0x4, /* End of block event */
	MHI_EV_CC_OOB = 0x5, /* Out of block event */
	MHI_EV_CC_DB_MODE = 0x6,
	MHI_EV_CC_UNDEFINED_ERR = 0x10,
	MHI_EV_CC_BAD_TRE = 0x11,
};

enum mhi_ch_state {
	MHI_CH_STATE_DISABLED = 0x0,
	MHI_CH_STATE_ENABLED = 0x1,
	MHI_CH_STATE_RUNNING = 0x2,
	MHI_CH_STATE_SUSPENDED = 0x3,
	MHI_CH_STATE_STOP = 0x4,
	MHI_CH_STATE_ERROR = 0x5,
};

#define MHI_INVALID_BRSTMODE(mode) (mode != MHI_DB_BRST_DISABLE && \
				    mode != MHI_DB_BRST_ENABLE)

#define NR_OF_CMD_RINGS			1
#define CMD_EL_PER_RING			128
#define PRIMARY_CMD_RING		0
#define MHI_MAX_MTU			0xffff

enum mhi_er_type {
	MHI_ER_TYPE_INVALID = 0x0,
	MHI_ER_TYPE_VALID = 0x1,
};

struct db_cfg {
	bool reset_req;
	bool db_mode;
	u32 pollcfg;
	enum mhi_db_brst_mode brstmode;
	dma_addr_t db_val;
	void (*process_db)(struct mhi_controller *mhi_cntrl,
			   struct db_cfg *db_cfg, void __iomem *io_addr,
			   dma_addr_t db_val);
};

struct mhi_ring {
	dma_addr_t dma_handle;
	dma_addr_t iommu_base;
	u64 *ctxt_wp; /* point to ctxt wp */
	void *pre_aligned;
	void *base;
	void *rp;
	void *wp;
	size_t el_size;
	size_t len;
	size_t elements;
	size_t alloc_size;
	void __iomem *db_addr;
};

struct mhi_cmd {
	struct mhi_ring ring;
	spinlock_t lock;
};

struct mhi_buf_info {
	void *v_addr;
	void *bb_addr;
	void *wp;
	void *cb_buf;
	dma_addr_t p_addr;
	size_t len;
	enum dma_data_direction dir;
};

struct mhi_event {
	struct mhi_controller *mhi_cntrl;
	struct mhi_chan *mhi_chan; /* dedicated to channel */
	u32 er_index;
	u32 intmod;
	u32 irq;
	int chan; /* this event ring is dedicated to a channel (optional) */
	u32 priority;
	enum mhi_er_data_type data_type;
	struct mhi_ring ring;
	struct db_cfg db_cfg;
	struct tasklet_struct task;
	spinlock_t lock;
	int (*process_event)(struct mhi_controller *mhi_cntrl,
			     struct mhi_event *mhi_event,
			     u32 event_quota);
	bool hw_ring;
	bool cl_manage;
	bool offload_ev; /* managed by a device driver */
};

struct mhi_chan {
	const char *name;
	/*
	 * Important: When consuming, increment tre_ring first and when
	 * releasing, decrement buf_ring first. If tre_ring has space, buf_ring
	 * is guranteed to have space so we do not need to check both rings.
	 */
	struct mhi_ring buf_ring;
	struct mhi_ring tre_ring;
	u32 chan;
	u32 er_index;
	u32 intmod;
	enum mhi_ch_type type;
	enum dma_data_direction dir;
	struct db_cfg db_cfg;
	enum mhi_ch_ee_mask ee_mask;
	enum mhi_ch_state ch_state;
	enum mhi_ev_ccs ccs;
	struct mhi_device *mhi_dev;
	void (*xfer_cb)(struct mhi_device *mhi_dev, struct mhi_result *result);
	struct mutex mutex;
	struct completion completion;
	rwlock_t lock;
	struct list_head node;
	bool lpm_notify;
	bool configured;
	bool offload_ch;
	bool pre_alloc;
	bool auto_start;
	bool wake_capable;
};

/* Default MHI timeout */
#define MHI_TIMEOUT_MS (1000)

struct mhi_device *mhi_alloc_device(struct mhi_controller *mhi_cntrl);

int mhi_destroy_device(struct device *dev, void *data);
void mhi_create_devices(struct mhi_controller *mhi_cntrl);

#endif /* _MHI_INT_H */
