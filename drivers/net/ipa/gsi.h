/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */
#ifndef _GSI_H_
#define _GSI_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>

/* Maximum number of channels and event rings supported by the driver */
#define GSI_CHANNEL_COUNT_MAX	17
#define GSI_EVT_RING_COUNT_MAX	13

/* Maximum TLV FIFO size for a channel; 64 here is arbitrary (and high) */
#define GSI_TLV_MAX		64

struct device;
struct scatterlist;
struct platform_device;

struct gsi;
struct gsi_trans;
struct gsi_channel_data;
struct ipa_gsi_endpoint_data;

/* Execution environment IDs */
enum gsi_ee_id {
	GSI_EE_AP	= 0,
	GSI_EE_MODEM	= 1,
	GSI_EE_UC	= 2,
	GSI_EE_TZ	= 3,
};

struct gsi_ring {
	void *virt;			/* ring array base address */
	dma_addr_t addr;		/* primarily low 32 bits used */
	u32 count;			/* number of elements in ring */

	/* The ring index value indicates the next "open" entry in the ring.
	 *
	 * A channel ring consists of TRE entries filled by the AP and passed
	 * to the hardware for processing.  For a channel ring, the ring index
	 * identifies the next unused entry to be filled by the AP.
	 *
	 * An event ring consists of event structures filled by the hardware
	 * and passed to the AP.  For event rings, the ring index identifies
	 * the next ring entry that is not known to have been filled by the
	 * hardware.
	 */
	u32 index;
};

/* Transactions use several resources that can be allocated dynamically
 * but taken from a fixed-size pool.  The number of elements required for
 * the pool is limited by the total number of TREs that can be outstanding.
 *
 * If sufficient TREs are available to reserve for a transaction,
 * allocation from these pools is guaranteed to succeed.  Furthermore,
 * these resources are implicitly freed whenever the TREs in the
 * transaction they're associated with are released.
 *
 * The result of a pool allocation of multiple elements is always
 * contiguous.
 */
struct gsi_trans_pool {
	void *base;			/* base address of element pool */
	u32 count;			/* # elements in the pool */
	u32 free;			/* next free element in pool (modulo) */
	u32 size;			/* size (bytes) of an element */
	u32 max_alloc;			/* max allocation request */
	dma_addr_t addr;		/* DMA address if DMA pool (or 0) */
};

struct gsi_trans_info {
	atomic_t tre_avail;		/* TREs available for allocation */
	struct gsi_trans_pool pool;	/* transaction pool */
	struct gsi_trans_pool sg_pool;	/* scatterlist pool */
	struct gsi_trans_pool cmd_pool;	/* command payload DMA pool */
	struct gsi_trans_pool info_pool;/* command information pool */
	struct gsi_trans **map;		/* TRE -> transaction map */

	spinlock_t spinlock;		/* protects updates to the lists */
	struct list_head alloc;		/* allocated, not committed */
	struct list_head pending;	/* committed, awaiting completion */
	struct list_head complete;	/* completed, awaiting poll */
	struct list_head polled;	/* returned by gsi_channel_poll_one() */
};

/* Hardware values signifying the state of a channel */
enum gsi_channel_state {
	GSI_CHANNEL_STATE_NOT_ALLOCATED	= 0x0,
	GSI_CHANNEL_STATE_ALLOCATED	= 0x1,
	GSI_CHANNEL_STATE_STARTED	= 0x2,
	GSI_CHANNEL_STATE_STOPPED	= 0x3,
	GSI_CHANNEL_STATE_STOP_IN_PROC	= 0x4,
	GSI_CHANNEL_STATE_ERROR		= 0xf,
};

/* We only care about channels between IPA and AP */
struct gsi_channel {
	struct gsi *gsi;
	bool toward_ipa;
	bool command;			/* AP command TX channel or not */
	bool use_prefetch;		/* use prefetch (else escape buf) */

	u8 tlv_count;			/* # entries in TLV FIFO */
	u16 tre_count;
	u16 event_count;

	struct completion completion;	/* signals channel command completion */

	struct gsi_ring tre_ring;
	u32 evt_ring_id;

	u64 byte_count;			/* total # bytes transferred */
	u64 trans_count;		/* total # transactions */
	/* The following counts are used only for TX endpoints */
	u64 queued_byte_count;		/* last reported queued byte count */
	u64 queued_trans_count;		/* ...and queued trans count */
	u64 compl_byte_count;		/* last reported completed byte count */
	u64 compl_trans_count;		/* ...and completed trans count */

	struct gsi_trans_info trans_info;

	struct napi_struct napi;
};

/* Hardware values signifying the state of an event ring */
enum gsi_evt_ring_state {
	GSI_EVT_RING_STATE_NOT_ALLOCATED	= 0x0,
	GSI_EVT_RING_STATE_ALLOCATED		= 0x1,
	GSI_EVT_RING_STATE_ERROR		= 0xf,
};

struct gsi_evt_ring {
	struct gsi_channel *channel;
	struct completion completion;	/* signals event ring state changes */
	enum gsi_evt_ring_state state;
	struct gsi_ring ring;
};

struct gsi {
	struct device *dev;		/* Same as IPA device */
	struct net_device dummy_dev;	/* needed for NAPI */
	void __iomem *virt;
	u32 irq;
	u32 channel_count;
	u32 evt_ring_count;
	struct gsi_channel channel[GSI_CHANNEL_COUNT_MAX];
	struct gsi_evt_ring evt_ring[GSI_EVT_RING_COUNT_MAX];
	u32 event_bitmap;
	u32 event_enable_bitmap;
	u32 modem_channel_bitmap;
	struct completion completion;	/* for global EE commands */
	struct mutex mutex;		/* protects commands, programming */
};

/**
 * gsi_setup() - Set up the GSI subsystem
 * @gsi:	Address of GSI structure embedded in an IPA structure
 * @legacy:	Set up for legacy hardware
 *
 * Return:	0 if successful, or a negative error code
 *
 * Performs initialization that must wait until the GSI hardware is
 * ready (including firmware loaded).
 */
int gsi_setup(struct gsi *gsi, bool legacy);

/**
 * gsi_teardown() - Tear down GSI subsystem
 * @gsi:	GSI address previously passed to a successful gsi_setup() call
 */
void gsi_teardown(struct gsi *gsi);

/**
 * gsi_channel_tre_max() - Channel maximum number of in-flight TREs
 * @gsi:	GSI pointer
 * @channel_id:	Channel whose limit is to be returned
 *
 * Return:	 The maximum number of TREs oustanding on the channel
 */
u32 gsi_channel_tre_max(struct gsi *gsi, u32 channel_id);

/**
 * gsi_channel_trans_tre_max() - Maximum TREs in a single transaction
 * @gsi:	GSI pointer
 * @channel_id:	Channel whose limit is to be returned
 *
 * Return:	 The maximum TRE count per transaction on the channel
 */
u32 gsi_channel_trans_tre_max(struct gsi *gsi, u32 channel_id);

/**
 * gsi_channel_start() - Start an allocated GSI channel
 * @gsi:	GSI pointer
 * @channel_id:	Channel to start
 *
 * Return:	0 if successful, or a negative error code
 */
int gsi_channel_start(struct gsi *gsi, u32 channel_id);

/**
 * gsi_channel_stop() - Stop a started GSI channel
 * @gsi:	GSI pointer returned by gsi_setup()
 * @channel_id:	Channel to stop
 *
 * Return:	0 if successful, or a negative error code
 */
int gsi_channel_stop(struct gsi *gsi, u32 channel_id);

/**
 * gsi_channel_reset() - Reset an allocated GSI channel
 * @gsi:	GSI pointer
 * @channel_id:	Channel to be reset
 * @legacy:	Legacy behavior
 *
 * Reset a channel and reconfigure it.  The @legacy flag indicates
 * that some steps should be done differently for legacy hardware.
 *
 * GSI hardware relinquishes ownership of all pending receive buffer
 * transactions and they will complete with their cancelled flag set.
 */
void gsi_channel_reset(struct gsi *gsi, u32 channel_id, bool legacy);

int gsi_channel_suspend(struct gsi *gsi, u32 channel_id, bool stop);
int gsi_channel_resume(struct gsi *gsi, u32 channel_id, bool start);

/**
 * gsi_init() - Initialize the GSI subsystem
 * @gsi:	Address of GSI structure embedded in an IPA structure
 * @pdev:	IPA platform device
 *
 * Return:	0 if successful, or a negative error code
 *
 * Early stage initialization of the GSI subsystem, performing tasks
 * that can be done before the GSI hardware is ready to use.
 */
int gsi_init(struct gsi *gsi, struct platform_device *pdev, bool prefetch,
	     u32 count, const struct ipa_gsi_endpoint_data *data,
	     bool modem_alloc);

/**
 * gsi_exit() - Exit the GSI subsystem
 * @gsi:	GSI address previously passed to a successful gsi_init() call
 */
void gsi_exit(struct gsi *gsi);

#endif /* _GSI_H_ */
