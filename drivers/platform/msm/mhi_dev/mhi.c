// SPDX-License-Identifier: GPL-2.0-only
//Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

/*
 * MSM MHI device core driver.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/msm_ep_pcie.h>
#include <linux/mhi_dma.h>
#include <linux/vmalloc.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/limits.h>

#include "mhi.h"
#include "mhi_hwio.h"
#include "mhi_sm.h"

/* Wait time on the device for Host to set M0 state */
#define MHI_DEV_M0_MAX_CNT		30
/* Wait time before suspend/resume is complete */
#define MHI_SUSPEND_MIN			100
#define MHI_SUSPEND_TIMEOUT		600
/* Wait time for completion */
#define DMA_READ_TOUT_MS		3000
/* Wait time on the device for Host to set BHI_INTVEC */
#define MHI_BHI_INTVEC_MAX_CNT			200
#define MHI_BHI_INTVEC_WAIT_MS		50
#define MHI_WAKEUP_TIMEOUT_CNT		20
#define MHI_MASK_CH_EV_LEN		32
#define MHI_RING_CMD_ID			0
#define MHI_RING_PRIMARY_EVT_ID		1
#define MHI_1K_SIZE			0x1000
/* Updated Specification for event start is NER - 2 and end - NER -1 */
#define MHI_HW_ACC_EVT_RING_END		1

#define MHI_HOST_REGION_NUM             2

#define MHI_MMIO_CTRL_INT_STATUS_A7_MSK	0x1
#define MHI_MMIO_CTRL_CRDB_STATUS_MSK	0x2

#define HOST_ADDR(lsb, msb)		((lsb) | ((uint64_t)(msb) << 32))
#define HOST_ADDR_LSB(addr)		(addr & 0xFFFFFFFF)
#define HOST_ADDR_MSB(addr)		((addr >> 32) & 0xFFFFFFFF)

#define MHI_IPC_LOG_PAGES		(100)
#define MHI_IPC_ERR_LOG_PAGES		(10)
#define MHI_REGLEN			0x100
#define MHI_INIT			0
#define MHI_REINIT			1

#define TR_RING_ELEMENT_SZ	sizeof(struct mhi_dev_transfer_ring_element)
#define RING_ELEMENT_TYPE_SZ	sizeof(union mhi_dev_ring_element_type)

#define MHI_DEV_CH_CLOSE_TIMEOUT_MIN	5000
#define MHI_DEV_CH_CLOSE_TIMEOUT_MAX	5100
#define MHI_DEV_CH_CLOSE_TIMEOUT_COUNT	200

#define IGNORE_CH_SIZE			4
int ignore_ch_channel[IGNORE_CH_SIZE] = {2, 3, 24, 25};

uint32_t bhi_imgtxdb;
enum mhi_msg_level mhi_msg_lvl = MHI_MSG_ERROR;
enum mhi_msg_level mhi_ipc_msg_lvl = MHI_MSG_VERBOSE;
enum mhi_msg_level mhi_ipc_err_msg_lvl = MHI_MSG_ERROR;
void *mhi_ipc_vf_log[MHI_MAX_NUM_INSTANCES];
void *mhi_ipc_err_log;
void *mhi_ipc_default_err_log;

static struct mhi_dev_ctx *mhi_hw_ctx;
static struct mhi_dma_ops *mhi_dma_fun_ops;
static void mhi_hwc_cb(void *priv, enum mhi_dma_event_type event,
	unsigned long data);
static void mhi_ring_init_cb(void *user_data);
static void mhi_update_state_info(struct mhi_dev *mhi, enum mhi_ctrl_info info);
static void mhi_update_state_info_ch(u32 vf_id, uint32_t ch_id, enum mhi_ctrl_info info);
static int mhi_deinit(struct mhi_dev *mhi);
static int mhi_dev_pcie_notify_event;
static void mhi_dev_transfer_completion_cb(void *mreq);
static int mhi_dev_alloc_evt_buf_evt_req(struct mhi_dev *mhi,
		struct mhi_dev_channel *ch, struct mhi_dev_ring *evt_ring);
static int mhi_dev_schedule_msi_mhi_dma(struct mhi_dev *mhi,
		struct event_req *ereq);
static void mhi_dev_event_msi_cb(void *req);
static void mhi_dev_cmd_event_msi_cb(void *req);

static int mhi_dev_alloc_cmd_ack_buf_req(struct mhi_dev *mhi);

static int mhi_dev_ring_init(struct mhi_dev *dev);
static int mhi_dev_get_event_notify(enum mhi_dev_state state, enum mhi_dev_event *event);
static void mhi_dev_pcie_handle_event(struct work_struct *work);
static void mhi_dev_setup_virt_device(struct mhi_dev_ctx *mhictx);
static inline struct mhi_dev *mhi_get_dev_ctx(struct mhi_dev_ctx *mhi_hw_ctx, enum mhi_id id);

static struct mhi_dev_uevent_info channel_state_info[MHI_MAX_NUM_INSTANCES][MHI_MAX_CHANNELS];
static void mhi_dev_event_buf_completion_cb(void *req);
static void mhi_dev_event_rd_offset_completion_cb(void *req);
static DECLARE_COMPLETION(read_from_host);
static DECLARE_COMPLETION(write_to_host);
static DECLARE_COMPLETION(transfer_host_to_device);
static DECLARE_COMPLETION(transfer_device_to_host);
static int mhi_dev_channel_init(struct mhi_dev *mhi, uint32_t ch_id);

bool check_ignore_ch_list(int ch_id)
{
	int i = 0;

	for ( ; i < IGNORE_CH_SIZE; i++) {
		if (ch_id == ignore_ch_channel[i])
			return true;
	}

	return false;
}

int mhi_dma_provide_ops(const struct mhi_dma_ops *ops)
{
	int rc;
	struct mhi_dev *mhi = mhi_get_dev_ctx(mhi_hw_ctx, MHI_DEV_PHY_FUN);

	if (!mhi) {
		mhi_log(MHI_DEV_PHY_FUN, MHI_MSG_ERROR, "MHI is NULL, defering\n");
		return -EINVAL;
	}

	mhi_log(MHI_DEV_PHY_FUN, MHI_MSG_VERBOSE, "Received MHI DMA fun ops\n");

	memcpy(&mhi_hw_ctx->mhi_dma_fun_ops, ops, sizeof(struct mhi_dma_ops));
	mhi_dma_fun_ops = &mhi_hw_ctx->mhi_dma_fun_ops;

	mutex_lock(&mhi->mhi_lock);
	if (mhi->use_mhi_dma) {
		rc =  mhi_dma_fun_ops->mhi_dma_register_ready_cb(mhi_ring_init_cb, mhi);
		if (rc != -EEXIST && rc != 0) {
			mhi_log(MHI_DEV_PHY_FUN, MHI_MSG_ERROR,
				"Error while registering with MHI DMA, unexpected\n");
			WARN_ON(1);
		} else if (rc == -EEXIST) {
			mhi->mhi_dma_ready = true;
		}
	}
	mhi->mhi_hw_ctx->phandle = ep_pcie_get_phandle(mhi->mhi_hw_ctx->ifc_id);
	if (mhi->mhi_hw_ctx->phandle) {
		/* Get EP PCIe capabilities to check if it supports SRIOV capability */
		ep_pcie_core_get_capability(mhi_hw_ctx->phandle, &mhi_hw_ctx->ep_cap);
		/* PCIe link is already up */
		mhi_dev_pcie_notify_event = MHI_INIT;
		mhi_dev_setup_virt_device(mhi->mhi_hw_ctx);
		queue_work(mhi->pcie_event_wq, &mhi->pcie_event);
	}
	mutex_unlock(&mhi->mhi_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dma_provide_ops);

static void unmap_single(struct mhi_dev *mhi, dma_addr_t phys,
		size_t size, enum dma_data_direction dir)
{
	if (mhi->use_mhi_dma)
		mhi_dma_fun_ops->mhi_dma_unmap_buffer(phys, size, dir);
	else
		dma_unmap_single(&mhi->mhi_hw_ctx->pdev->dev, phys, size, dir);
}

static dma_addr_t map_single(struct mhi_dev *mhi, void *virt, size_t size,
			enum dma_data_direction dir)
{
	dma_addr_t dma;

	if (mhi->use_mhi_dma)
		dma = mhi_dma_fun_ops->mhi_dma_map_buffer(virt, size, dir);
	else
		dma = dma_map_single(&mhi->mhi_hw_ctx->pdev->dev, virt, size, dir);
	return dma;
}

void free_coherent(struct mhi_dev *mhi, size_t size, void *virt, dma_addr_t phys)
{
	if (mhi->use_mhi_dma)
		mhi_dma_fun_ops->mhi_dma_free_buffer(size, virt, phys);
	else
		dma_free_coherent(&mhi->mhi_hw_ctx->pdev->dev, size, virt, phys);
}
EXPORT_SYMBOL_GPL(free_coherent);

void *alloc_coherent(struct mhi_dev *mhi, size_t size, dma_addr_t *phys, gfp_t gfp)
{
	if (mhi->use_mhi_dma)
		return mhi_dma_fun_ops->mhi_dma_alloc_buffer(size, phys, gfp);
	else
		return dma_alloc_coherent(&mhi->mhi_hw_ctx->pdev->dev, size, phys, gfp);
}
EXPORT_SYMBOL_GPL(alloc_coherent);

static inline struct mhi_dev *mhi_get_dev_ctx(struct mhi_dev_ctx *mhi_hw_ctx, enum mhi_id id)
{
	return (mhi_hw_ctx && (id <= mhi_hw_ctx->ep_cap.num_vfs)) ?
					mhi_hw_ctx->mhi_dev[id] : NULL;
}

/*
 * mhi_dev_get_msi_config () - Fetch the MSI config from
 * PCIe and set the msi_disable flag accordingly
 *
 * @phandle : phandle structure
 * @cfg :     PCIe MSI config structure
 * @vf_id:    VF ID
 */
static int mhi_dev_get_msi_config(struct ep_pcie_hw *phandle,
				struct ep_pcie_msi_config *cfg, u32 vf_id)
{
	int rc;
	struct mhi_dev *mhi = mhi_get_dev_ctx(mhi_hw_ctx, vf_id);

	/*
	 * Fetching MSI config to read the MSI capability and setting the
	 * msi_disable flag based on it.
	 */

	if (!mhi) {
		mhi_log(MHI_DEV_PHY_FUN, MHI_MSG_ERROR, "MHI is NULL, defering\n");
		return -EINVAL;
	}

	rc = ep_pcie_get_msi_config(phandle, cfg, vf_id);
	if (rc == -EOPNOTSUPP) {
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "MSI is disabled\n");
		mhi->msi_disable = true;
	} else if (!rc) {
		mhi->msi_disable = false;
	} else {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Error retrieving pcie msi logic\n");
		return rc;
	}

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "msi_disable = %d\n",
					mhi->msi_disable);
	return 0;
}

/*
 * mhi_dev_ring_cache_completion_cb () - Call back function called
 * by MHI DMA driver when ring element cache is done
 *
 * @req : ring cache request
 */
static void mhi_dev_ring_cache_completion_cb(void *req)
{
	struct ring_cache_req *ring_req = req;

	if (ring_req)
		complete(ring_req->done);
	else
		mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR, "ring cache req is NULL\n");
}

static void mhi_dev_edma_sync_cb(void *done)
{
	complete((struct completion *)done);
}

/**
 * mhi_dev_process_wake_db() - Handle device wake doorbell.
 * @mhi:       MHI dev structure
 */
static int mhi_dev_process_wake_db(struct mhi_dev *mhi)
{
	int res = 0;
	uint32_t db_val = 0;

	res = mhi_dev_mmio_read(mhi, CHDB_LOWER_n(MHI_DEV_WAKE_DB_CHAN),
					&db_val);
	if (res) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Failed to read CHDB register for ch_id:%d\n",
			MHI_DEV_WAKE_DB_CHAN);
		return res;
	}

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "DB value: 0x%lx\n",
				(size_t) db_val);

	if ((db_val != 0x1) && (db_val != 0x0)) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Invalid value (%d) as WAKE DB received on ch_id:%d\n",
			db_val, MHI_DEV_WAKE_DB_CHAN);
		return -EINVAL;
	}

	/* Check if current wake DB value is already configured */
	if (db_val == mhi->wake_db_status) {
		mhi_log(mhi->vf_id, MHI_MSG_DBG,
			"Wake db already %s\n", db_val ? "asserted" : "de-asserted");
		return res;
	}

	/* Assign wake doorbell value to wake_db_status */
	mhi->wake_db_status = !!db_val;

	res = mhi_dev_configure_inactivity_timer(mhi, !mhi->wake_db_status);
	if (res)
		return res;

	return 0;
}

/**
 * mhi_dev_configure_inactivity_timer() - Configure inactive timer.
 * @mhi:       MHI dev structure
 * @enable:    Flag to enable or disable timer
 */
int mhi_dev_configure_inactivity_timer(struct mhi_dev *mhi, bool enable)
{
	int res = 0;
	struct ep_pcie_inactivity inact_param;

	inact_param.enable = enable;
	inact_param.timer_us = PCIE_EP_TIMER_US;
	res = ep_pcie_configure_inactivity_timer(mhi->mhi_hw_ctx->phandle,
				&inact_param);
	if (res) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Failed to configure inact timer with enable = %d\n", enable);
		return res;
	}

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
		"%s inactivity timer\n", enable ? "Enabled" : "Disabled");
	return 0;
}

/**
 * mhi_dev_read_from_host_mhi_dma - memcpy equivalent API to transfer data
 *		from host to device.
 * @mhi:	MHI dev structure.
 * @transfer:	Host and device address details.
 */
void mhi_dev_read_from_host_mhi_dma(struct mhi_dev *mhi, struct mhi_addr *transfer)
{
	int rc = 0;
	uint64_t bit_40 = ((u64) 1) << 40, host_addr_pa = 0, offset = 0;
	struct ring_cache_req ring_req;

	DECLARE_COMPLETION(done);

	ring_req.done = &done;

	if (WARN_ON(!mhi))
		return;

	if (mhi->config_iatu) {
		offset = (uint64_t) transfer->host_pa - mhi->ctrl_base.host_pa;
		/* Mapping the translated physical address on the device */
		host_addr_pa = (uint64_t) mhi->ctrl_base.device_pa + offset;
	} else {
		host_addr_pa = transfer->host_pa | bit_40;
	}

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
		"Async: device 0x%llx <<-- host 0x%llx, size %d\n",
		transfer->phy_addr, host_addr_pa,
		(int) transfer->size);

	rc = mhi_dma_fun_ops->mhi_dma_async_memcpy((u64)transfer->phy_addr,
			host_addr_pa,
			(int) transfer->size,
			mhi->mhi_dma_fun_params,
			mhi_dev_ring_cache_completion_cb,
			&ring_req);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "error while reading from host:%d\n",
				rc);
		WARN_ON(1);
		return;
	}

	if (!wait_for_completion_timeout(&done, msecs_to_jiffies(DMA_READ_TOUT_MS))) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
		"Timeout - DMA is either stuck or taking longer to perform transfer.\n");
		BUG_ON(1);
	}
}

/**
 * mhi_dev_write_to_host_mhi_dma - Transfer data from device to host.
 *		Based on support available, either DMA or memcpy is used.
 * @mhi:	MHI dev structure.
 * @transfer:	Host and device address details.
 * @ereq:	event_req structure.
 * @tr_type:	Transfer type.
 */
void mhi_dev_write_to_host_mhi_dma(struct mhi_dev *mhi, struct mhi_addr *transfer,
		struct event_req *ereq, enum mhi_dev_transfer_type tr_type)
{
	int rc = 0;
	uint64_t bit_40 = ((u64) 1) << 40, host_addr_pa = 0, offset = 0;
	dma_addr_t dma;
	void (*cb_func)(void *req);

	if (WARN_ON(!mhi))
		return;

	if (mhi->config_iatu) {
		offset = (uint64_t) transfer->host_pa - mhi->ctrl_base.host_pa;
		/* Mapping the translated physical address on the device */
		host_addr_pa = (uint64_t) mhi->ctrl_base.device_pa + offset;
	} else {
		host_addr_pa = transfer->host_pa | bit_40;
	}

	if (tr_type == MHI_DEV_DMA_ASYNC) {
		/*
		 * Event read pointer memory and MSI buf are dma_alloc_coherent
		 * memory so don't need to dma_map. Use the physical address
		 * passed in phy_addr.
		 */
		if (ereq->event_type == SEND_EVENT_RD_OFFSET ||
			ereq->event_type == SEND_MSI)
			dma = transfer->phy_addr;
		else
			dma = map_single(mhi,
				transfer->virt_addr, transfer->size,
				DMA_TO_DEVICE);
		if (ereq->event_type == SEND_EVENT_BUFFER) {
			ereq->dma = dma;
			ereq->dma_len = transfer->size;
			cb_func = mhi_dev_event_buf_completion_cb;
		} else if (ereq->event_type == SEND_EVENT_RD_OFFSET) {
			/*
			 * Event read pointer memory is dma_alloc_coherent
			 * memory. Don't need to dma_unmap.
			 */
			cb_func = mhi_dev_event_rd_offset_completion_cb;
		} else {
			if (!ereq->is_cmd_cpl)
				cb_func = mhi_dev_event_msi_cb;
			else
				cb_func = mhi_dev_cmd_event_msi_cb;
		}
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Async: device 0x%llx --> host 0x%llx, size %d\n",
				(uint64_t) dma, host_addr_pa,
				(int) transfer->size);

		rc = mhi_dma_fun_ops->mhi_dma_async_memcpy(host_addr_pa,
				(uint64_t)dma,
				(int) transfer->size,
				mhi->mhi_dma_fun_params,
				cb_func,
				ereq);
		if (rc)
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"error while writing to host:%d\n", rc);
	} else if (tr_type == MHI_DEV_DMA_SYNC) {
		if (transfer->phy_addr) {
			dma = transfer->phy_addr;
		} else {
			dma = map_single(mhi,
					transfer->virt_addr, transfer->size,
					DMA_TO_DEVICE);
			if (dma_mapping_error(&mhi->mhi_hw_ctx->pdev->dev, dma)) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
						"dma mapping failed\n");
				return;
			}
		}

		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Sync: device 0x%llx --> host 0x%llx, size %d\n",
				(uint64_t) dma, host_addr_pa,
				(int) transfer->size);
		rc = mhi_dma_fun_ops->mhi_dma_sync_memcpy(host_addr_pa,
				(u64) dma,
				(int) transfer->size,
				mhi->mhi_dma_fun_params);
		if (rc)
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"error while writing to host:%d\n", rc);
		if (!transfer->phy_addr)
			unmap_single(mhi, dma, transfer->size, DMA_TO_DEVICE);
	}
}

/*
 * mhi_dev_event_buf_completion_cb() - CB function called by MHI DMA driver
 * when transfer completion event buffer copy to host is done.
 *
 * @req -  event_req structure
 */
static void mhi_dev_event_buf_completion_cb(void *req)
{
	struct mhi_dev_channel *ch;
	struct mhi_dev *mhi;
	struct event_req *ereq = req;

	if (!ereq) {
		mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR, "Null ereq\n");
		return;
	}

	if (ereq->is_cmd_cpl)
		mhi = ereq->context;
	else {
		ch = ereq->context;
		mhi = ch->ring->mhi_dev;
	}
	ereq->snd_cmpl = false;

	if (ereq->dma && ereq->dma_len)
		unmap_single(mhi, ereq->dma, ereq->dma_len,
				DMA_TO_DEVICE);

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
		"Event buf dma completed for flush req %d\n", ereq->flush_num);
}

static int mhi_dev_schedule_msi_mhi_dma(struct mhi_dev *mhi, struct event_req *ereq)
{
	struct ep_pcie_msi_config cfg;
	struct mhi_addr msi_addr;
	struct mhi_dev_channel *ch;
	uint64_t evnt_ring_idx;
	struct mhi_dev_ring *ring;
	union mhi_dev_ring_ctx *ctx;
	uint32_t event_ring;
	int rc;

	rc = mhi_dev_get_msi_config(mhi->mhi_hw_ctx->phandle, &cfg, mhi->vf_id);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Error retrieving pcie msi logic\n");
		return rc;
	}

	/* If MSI is disabled, bailing out */
	if (mhi->msi_disable)
		return 0;

	msi_addr.size = sizeof(uint32_t);
	msi_addr.host_pa = (uint64_t)((uint64_t)cfg.upper << 32) |
					(uint64_t)cfg.lower;

	ereq->event_type = SEND_MSI;
	if (!ereq->is_cmd_cpl) {
		ch = ereq->context;
		event_ring =  mhi->ch_ctx_cache[ch->ch_id].err_indx;
		evnt_ring_idx = mhi->ev_ring_start + event_ring;
		ring = mhi->ring[evnt_ring_idx];
		ctx = (union mhi_dev_ring_ctx *)&mhi->ev_ctx_cache[event_ring];
		*ring->msi_buf = cfg.data + ctx->ev.msivec;
		ch->msi_cnt++;
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
			"Sending MSI %d to 0x%llx as data = 0x%x for ch_id:%d\t"
			"msi_count %d, ereq flush_num %d\n",
			ctx->ev.msivec, msi_addr.host_pa,
			*ring->msi_buf, ch->ch_id,
			ch->msi_cnt, ereq->flush_num);
	} else {
		/* ring 0 is for command ring, command ring's event ring is always 1 */
		event_ring = 0;
		evnt_ring_idx = mhi->ev_ring_start + event_ring;
		ring = mhi->ring[evnt_ring_idx];
		ctx = (union mhi_dev_ring_ctx *)&mhi->ev_ctx_cache[event_ring];
		*ring->msi_buf = cfg.data + ctx->ev.msivec;
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
			"Sending MSI %d to 0x%llx as data = 0x%x for cmd ack\t"
			"ereq flush_num %d\n",
			ctx->ev.msivec, msi_addr.host_pa, *ring->msi_buf,
			ereq->flush_num);
	}

	msi_addr.phy_addr = ring->msi_buf_dma_handle;
	mhi->write_to_host(mhi, &msi_addr, ereq, MHI_DEV_DMA_ASYNC);

	return 0;
}

/*
 * mhi_dev_event_rd_offset_completion_cb() - CB function called by MHI DMA driver
 * when event ring rd_offset transfer is done.
 *
 * @req -  event_req structure
 */
static void mhi_dev_event_rd_offset_completion_cb(void *req)
{
	struct mhi_dev *mhi;
	struct mhi_dev_channel *ch;
	struct event_req *ereq = req;

	if (!ereq)
		return;

	if (ereq->is_cmd_cpl)
		mhi = ereq->context;
	else  {
		ch = ereq->context;
		mhi = ch->ring->mhi_dev;
	}

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "Rd offset dma completed for flush req %d\n",
		ereq->flush_num);

	/*
	 * The mhi_dev_cmd_event_msi_cb and mhi_dev_event_msi_cb APIs does
	 * add back the flushed events space to the event buffer and returns
	 * the event req back to the list. These are registered in the API
	 * mhi_dev_schedule_msi_ipa and get invoked when MSI triggering is
	 * complete.
	 * In the case of MSI being disabled by the host, these callbacks will
	 * not get invoked as triggering MSI is suppressed from device side.
	 * Hence, invoking these callbacks as part of this API to ensure we do
	 * not run out on ereq buffer space in this scenario.
	 */
	if (mhi->msi_disable) {
		if (ereq->is_cmd_cpl)
			mhi_dev_cmd_event_msi_cb(ereq);
		else
			mhi_dev_event_msi_cb(ereq);
	}
}

static void mhi_dev_cmd_event_msi_cb(void *req)
{
	struct mhi_cmd_cmpl_ctx *cmd_ctx;
	struct mhi_dev *mhi;
	struct event_req *ereq = req;
	unsigned long flags;

	mhi = ereq->context;

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "MSI completed for flush req %d\n",
		ereq->flush_num);

	/*Cmd completion handling*/
	cmd_ctx = mhi->cmd_ctx;
	cmd_ctx->cmd_buf_wp += ereq->num_events;
	if (cmd_ctx->cmd_buf_wp == NUM_CMD_EVENTS_DEFAULT)
		cmd_ctx->cmd_buf_wp = 0;
	spin_lock_irqsave(&mhi->lock, flags);
	list_add_tail(&ereq->list, &cmd_ctx->cmd_req_buffers);
	spin_unlock_irqrestore(&mhi->lock, flags);
}

static void mhi_dev_event_msi_cb(void *req)
{
	struct event_req *ereq = req;
	struct mhi_dev_channel *ch;
	struct mhi_dev *mhi;

	if (!ereq) {
		mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR,
			"Null ereq, valid only for sync dma and cmd ack to host\n");
		return;
	}

	ch = ereq->context;
	if (!ch) {
		mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR, "Invalid ereq context, return\n");
		return;
	}
	if (ch->pend_flush_cnt++ >= U32_MAX)
		ch->pend_flush_cnt = 0;
	mhi = ch->ring->mhi_dev;

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "MSI completed for %s flush req %d\n",
			ereq->is_stale ? "stale" : "", ereq->flush_num);

	/* Add back the flushed events space to the event buffer */
	ch->evt_buf_wp = ereq->start + ereq->num_events;
	if (ch->evt_buf_wp == ch->evt_buf_size)
		ch->evt_buf_wp = 0;
	/* Return the event req to the list */
	mutex_lock(&ch->ch_lock);
	if (ch->curr_ereq == NULL)
		ch->curr_ereq = ereq;
	else {
		if (ereq->is_stale)
			ereq->is_stale = false;
		list_add_tail(&ereq->list, &ch->event_req_buffers);
	}
	mutex_unlock(&ch->ch_lock);
}

static int mhi_trigger_msi_edma(struct mhi_dev_ring *ring, u32 idx, struct event_req *ereq)
{
	struct dma_async_tx_descriptor *descriptor;
	struct ep_pcie_msi_config cfg;
	struct msi_buf_cb_data *msi_buffer;
	struct mhi_dev_channel *ch;
	int rc;
	unsigned long flags;
	struct mhi_dev *mhi = ring->mhi_dev;

	if (!mhi->msi_lower) {
		rc = mhi_dev_get_msi_config(mhi->mhi_hw_ctx->phandle, &cfg, mhi->vf_id);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"Error retrieving pcie msi logic\n");
			return rc;
		}

		/* If MSI is disabled, bailing out */
		if (mhi->msi_disable)
			return 0;

		mhi->msi_data = cfg.data;
		mhi->msi_lower = cfg.lower;
	}

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
		"Trigger MSI via edma, MSI lower:%x IRQ:%d idx:%d\n",
		mhi->msi_lower, mhi->msi_data + idx, idx);

	spin_lock_irqsave(&mhi->msi_lock, flags);

	msi_buffer = &ring->msi_buffer;
	msi_buffer->buf[0] = (mhi->msi_data + idx);

	descriptor = dmaengine_prep_dma_memcpy(mhi->mhi_hw_ctx->tx_dma_chan,
				(dma_addr_t)(mhi->msi_lower),
				msi_buffer->dma_addr,
				sizeof(u32),
				DMA_PREP_INTERRUPT);
	if (!descriptor) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"desc is null, MSI to Host failed\n");
		spin_unlock_irqrestore(&mhi->msi_lock, flags);
		return -EFAULT;
	}

	if (ereq) {
		ereq->event_type = SEND_MSI;
		descriptor->callback_param = ereq;
		if (!ereq->is_cmd_cpl) {
			ch = ereq->context;
			descriptor->callback = mhi_dev_event_msi_cb;
			ch->msi_cnt++;
		} else {
			descriptor->callback = mhi_dev_cmd_event_msi_cb;
		}
	}

	dma_async_issue_pending(mhi->mhi_hw_ctx->tx_dma_chan);

	spin_unlock_irqrestore(&mhi->msi_lock, flags);

	return 0;
}

static int mhi_dev_send_multiple_tr_events(struct mhi_dev *mhi, int evnt_ring,
		struct event_req *ereq, uint32_t evt_len,
		enum mhi_dev_tr_compl_evt_type event_type)
{
	int rc = 0;
	uint64_t evnt_ring_idx = mhi->ev_ring_start + evnt_ring;
	struct mhi_dev_ring *ring = mhi->ring[evnt_ring_idx];
	union mhi_dev_ring_ctx *ctx;
	struct mhi_addr transfer_addr;
	struct mhi_dev_channel *ch;

	if (!ereq) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "invalid event req\n");
		return -EINVAL;
	}

	if (evnt_ring_idx > mhi->cfg.event_rings) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Invalid event ring_id:%lld\n", evnt_ring_idx);
		return -EINVAL;
	}

	if (!ring) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "ring_id:%llu not present\n",
					evnt_ring_idx);
		return -EINVAL;
	}

	ctx = (union mhi_dev_ring_ctx *)&mhi->ev_ctx_cache[evnt_ring];

	if (mhi_ring_get_state(ring) == RING_STATE_UINT) {
		rc = mhi_ring_start(ring, ctx, mhi);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"error starting event ring_id:%llu\n",
				evnt_ring_idx);
			return rc;
		}
	}


	mutex_lock(&ring->event_lock);

	/* add the events */
	ereq->is_cmd_cpl = (event_type == SEND_CMD_CMP) ? true:false;
	ereq->event_type = SEND_EVENT_BUFFER;

	if (!ereq->is_cmd_cpl) {
		ch = ereq->context;
		/*
		 * Take Channel ring event lock to prevent sending
		 * completion command while the channel is getting
		 * reset/stopped.
		 * Abort sending completion event if channel has moved to
		 * stopped state.
		 */
		mutex_lock(&ch->ring->event_lock);
		if (ch->state == MHI_DEV_CH_STOPPED ||
			ch->state == MHI_DEV_CH_PENDING_STOP) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"ch_id:%d is in %d state, abort sending cmpl evnt\n"
					, ch->ch_id, ch->state);
			rc = -ENXIO;
			goto exit;
		}
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "Flushing %d cmpl events of ch_id:%d\n",
				ereq->num_events, ch->ch_id);
	} else {
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
			"Flushing %d cmpl events of cmd ring\n",
			ereq->num_events);
	}

	rc = mhi_dev_add_element(ring, ereq->tr_events, ereq, evt_len);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"error in adding element rc %d\n", rc);
		goto exit;
	}

	ring->ring_ctx_shadow->ev.rp = (ring->rd_offset *
		sizeof(union mhi_dev_ring_element_type)) +
		ring->ring_ctx->generic.rbase;

	ring->evt_rp_cache[ring->rd_offset] = ring->ring_ctx_shadow->ev.rp;
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "Caching rp %llx for rd offset %zu\n",
		ring->evt_rp_cache[ring->rd_offset], ring->rd_offset);

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "ev.rp = %llx for %lld\n",
		ring->ring_ctx_shadow->ev.rp, evnt_ring_idx);

	if (MHI_USE_DMA(mhi)) {
		transfer_addr.host_pa = (mhi->ev_ctx_shadow.host_pa +
		sizeof(struct mhi_dev_ev_ctx) *
		evnt_ring) + (size_t)&ring->ring_ctx->ev.rp -
		(size_t)ring->ring_ctx;
		transfer_addr.phy_addr = ring->evt_rp_cache_dma_handle +
			(sizeof(uint64_t) * ring->rd_offset);
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
			"RP phy addr = 0x%llx for ring rd offset %zu\n",
			transfer_addr.phy_addr, ring->rd_offset);
	} else {
		transfer_addr.device_va =
			(size_t)(ring->evt_rp_cache + ring->rd_offset);
	}

	transfer_addr.virt_addr = &ring->evt_rp_cache[ring->rd_offset];
	transfer_addr.size = sizeof(uint64_t);
	ereq->event_type = SEND_EVENT_RD_OFFSET;

	/* Schedule DMA for event ring RP*/
	mhi->write_to_host(mhi, &transfer_addr, ereq, MHI_DEV_DMA_ASYNC);


	if (mhi->use_edma) {
		rc = mhi_trigger_msi_edma(ring, ctx->ev.msivec, ereq);
		if (rc)
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "error sending in msi\n");
	} else {
		/* Schedule DMA for MSI*/
		rc = mhi_dev_schedule_msi_mhi_dma(mhi, ereq);
		if (rc)
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "error sending in msi\n");
	}

exit:
	if (!ereq->is_cmd_cpl)
		mutex_unlock(&ch->ring->event_lock);
	mutex_unlock(&ring->event_lock);
	return rc;
}

static int mhi_dev_flush_cmd_completion_events(struct mhi_dev *mhi,
		union mhi_dev_ring_element_type *el)
{
	struct mhi_cmd_cmpl_ctx *cmd_ctx = mhi->cmd_ctx;
	unsigned long flags;
	struct event_req *flush_ereq;
	union mhi_dev_ring_element_type *compl_ev;
	int rc = 0;

	/*cmd completions are sent on event ring 0 always*/
	if (!mhi->cmd_ctx) {
		if (mhi_dev_alloc_cmd_ack_buf_req(mhi)) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Alloc cmd ack buff failed");
			return -ENOMEM;
		}
	}
	cmd_ctx = mhi->cmd_ctx;
	if (list_empty(&cmd_ctx->cmd_req_buffers)) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "cmd req buff list is empty");
		return -ENOMEM;
	}

	spin_lock_irqsave(&mhi->lock, flags);
	flush_ereq = container_of(cmd_ctx->cmd_req_buffers.next,
					struct event_req, list);
	list_del_init(&flush_ereq->list);
	flush_ereq->context = mhi;
	spin_unlock_irqrestore(&mhi->lock, flags);

	compl_ev = cmd_ctx->cmd_events + cmd_ctx->cmd_buf_rp;
	memcpy(compl_ev, el, sizeof(union mhi_dev_ring_element_type));
	cmd_ctx->cmd_buf_rp++;
	if (cmd_ctx->cmd_buf_rp == NUM_CMD_EVENTS_DEFAULT)
		cmd_ctx->cmd_buf_rp = 0;
	flush_ereq->tr_events = compl_ev;
	rc = mhi_dev_send_multiple_tr_events(mhi,
				0,
				flush_ereq,
				(1 *
				sizeof(union mhi_dev_ring_element_type)),
				SEND_CMD_CMP);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "failed to send compl evts\n");
		return rc;
	}

	return rc;
}

static int mhi_dev_flush_transfer_completion_events(struct mhi_dev *mhi,
		struct mhi_dev_channel *ch, bool snd_cmpl_num)
{
	int rc = 0;
	struct event_req *flush_ereq = NULL;
	struct event_req *itr, *tmp;

	do {

		/*
		 * Channel got stopped or closed with transfers pending
		 * Do not send completion events to host
		 */
		if (ch->state == MHI_DEV_CH_CLOSED ||
			ch->state == MHI_DEV_CH_STOPPED) {
			mhi_log(mhi->vf_id, MHI_MSG_DBG,
				"ch_id:%d closed with %d writes pending\n",
				ch->ch_id, ch->pend_wr_count + 1);
			rc = -ENODEV;
			break;
		}

		if (list_empty(&ch->flush_event_req_buffers))
			break;

		if (mhi->use_mhi_dma && !mhi->use_edma)
			flush_ereq = container_of(ch->flush_event_req_buffers.next,
					struct event_req, list);

		/*
		 * Edma read and write channels can run parallelly, where as ipa will acts as
		 * single channel and serializes the read and write reqs. For read channel
		 * completion  should be sent only after that transfer is completed. To track
		 * the completed reqs updating the mreq with snd completion number. When that
		 * particular req is completed, from the callback function we retrieve the
		 * send completion from the mreq, call this function with send completion
		 * number, here we compare the send completion number and if it matches
		 * with flush req send completion number we send completion to the host
		 */
		if (mhi->use_edma) {
			list_for_each_entry_safe(itr, tmp, &ch->flush_event_req_buffers, list) {
				flush_ereq = itr;
				if (flush_ereq && flush_ereq->snd_cmpl == snd_cmpl_num)
					break;
			}

			if (flush_ereq && (flush_ereq->snd_cmpl != snd_cmpl_num))
				break;
		}

		if (!flush_ereq) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "failed to assign flush_ereq\n");
			return -EINVAL;
		}

		list_del_init(&flush_ereq->list);

		if (ch->flush_req_cnt++ >= U32_MAX)
			ch->flush_req_cnt = 0;
		flush_ereq->flush_num = ch->flush_req_cnt;
		mhi_log(mhi->vf_id, MHI_MSG_DBG, "Flush num %d called for ch_id:%d\n",
			ch->flush_req_cnt, ch->ch_id);

		/* Check the limits of the buffer to be flushed */
		if (flush_ereq->tr_events < ch->tr_events ||
			(flush_ereq->tr_events + flush_ereq->num_events) >
			(ch->tr_events + ch->evt_buf_size)) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Invalid cmpl evt buf - start %pK, end %pK\n",
				flush_ereq->tr_events,
				flush_ereq->tr_events + flush_ereq->num_events);
			rc = -EINVAL;
			break;
		}
		rc = mhi_dev_send_multiple_tr_events(mhi,
				mhi->ch_ctx_cache[ch->ch_id].err_indx,
				flush_ereq,
				(flush_ereq->num_events *
				sizeof(union mhi_dev_ring_element_type)),
				SEND_EVENT_BUFFER);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "failed to send compl evts\n");
			break;
		}

		/*
		 * In edma case as we send completion only when we get completion
		 * callback breaking from the while loop
		 */
		if (mhi->use_edma)
			break;
	} while (true);

	return rc;
}

static bool mhi_dev_is_full_compl_evt_buf(struct mhi_dev_channel *ch)
{
	if (((ch->evt_buf_rp + 1) % ch->evt_buf_size) == ch->evt_buf_wp)
		return true;

	return false;
}

static void mhi_dev_rollback_compl_evt(struct mhi_dev_channel *ch)
{
	if (ch->evt_buf_rp)
		ch->evt_buf_rp--;
	else
		ch->evt_buf_rp = ch->evt_buf_size - 1;
}

/*
 * mhi_dev_queue_transfer_completion() - Queues a transfer completion
 * event to the event buffer (where events are stored until they get
 * flushed to host). Also determines when the completion events are
 * to be flushed (sent) to host.
 *
 * @req -  event_req structure
 * @flush - Set to true when completion events are to be flushed.
 */

static int mhi_dev_queue_transfer_completion(struct mhi_req *mreq, bool *flush)
{
	union mhi_dev_ring_element_type *compl_ev;
	struct mhi_dev_channel *ch = mreq->client->channel;

	if (mhi_dev_is_full_compl_evt_buf(ch) || ch->curr_ereq == NULL) {
		mhi_log(mreq->vf_id, MHI_MSG_VERBOSE, "Ran out of %s\n",
			(ch->curr_ereq ? "compl evts" : "ereqs"));
		return -EBUSY;
	}

	if (mreq->el->tre.ieot) {
		compl_ev = ch->tr_events + ch->evt_buf_rp;
		compl_ev->evt_tr_comp.chid = ch->ch_id;
		compl_ev->evt_tr_comp.type =
			MHI_DEV_RING_EL_TRANSFER_COMPLETION_EVENT;
		compl_ev->evt_tr_comp.len = mreq->transfer_len;
		compl_ev->evt_tr_comp.code = MHI_CMD_COMPL_CODE_EOT;
		compl_ev->evt_tr_comp.ptr = ch->ring->ring_ctx->generic.rbase +
			mreq->rd_offset * TR_RING_ELEMENT_SZ;
		ch->evt_buf_rp++;
		/* Ensure new event is flushed to memory */
		wmb();
		if (ch->evt_buf_rp == ch->evt_buf_size)
			ch->evt_buf_rp = 0;
		ch->curr_ereq->num_events++;

		mhi_log(mreq->vf_id, MHI_MSG_VERBOSE, "evnt ptr : 0x%llx\n",
			compl_ev->evt_tr_comp.ptr);
		mhi_log(mreq->vf_id, MHI_MSG_VERBOSE, "evnt len : 0x%x\n",
			compl_ev->evt_tr_comp.len);
		mhi_log(mreq->vf_id, MHI_MSG_VERBOSE, "evnt code :0x%x\n",
			compl_ev->evt_tr_comp.code);
		mhi_log(mreq->vf_id, MHI_MSG_VERBOSE, "evnt type :0x%x\n",
			compl_ev->evt_tr_comp.type);
		mhi_log(mreq->vf_id, MHI_MSG_VERBOSE, "evnt chid :0x%x\n",
			compl_ev->evt_tr_comp.chid);
		mhi_log(mreq->vf_id, MHI_MSG_VERBOSE, "evt_buf_rp: 0x%x, curr_ereq:0x%x\n",
			ch->evt_buf_rp, ch->curr_ereq->num_events);
		/*
		 * It is not necessary to flush when we need to wrap-around, if
		 * we do have free space in the buffer upon wrap-around.
		 * But when we really need to flush, we need a separate dma op
		 * anyway for the current chunk (from flush_start to the
		 * physical buffer end) since the buffer is circular. So we
		 * might as well flush on wrap-around.
		 * Also, we flush when we hit the threshold as well. The flush
		 * threshold is based on the channel's event ring size.
		 *
		 * In summary, completion event buffer flush is done if
		 *    * Client requests it (snd_cmpl was set to 1) OR
		 *    * Physical end of the event buffer is reached OR
		 *    * Flush threshold is reached for the current ereq
		 *
		 * When events are to be flushed, the current ereq is moved to
		 * the flush list, and the flush param is set to true for the
		 * second and third cases above. The actual flush of the events
		 * is done in the write_to_host API (for the write path) or
		 * in the transfer completion callback (for the read path).
		 */
		if (ch->evt_buf_rp == 0 ||
			ch->curr_ereq->num_events >=
			MHI_CMPL_EVT_FLUSH_THRSHLD(ch->evt_buf_size)
			|| mreq->snd_cmpl) {
			if (flush)
				*flush = true;

			/*
			 * Update the send complete number in both flush req
			 * and mreq
			 */
			ch->curr_ereq->snd_cmpl = ch->snd_cmpl_cnt;
			mreq->snd_cmpl = ch->curr_ereq->snd_cmpl;
			if (ch->snd_cmpl_cnt++ >= U32_MAX)
				ch->snd_cmpl_cnt = 1;

			ch->curr_ereq->tr_events = ch->tr_events +
				ch->curr_ereq->start;
			ch->curr_ereq->context = ch;

			/* Move current event req to flush list*/
			list_add_tail(&ch->curr_ereq->list,
				&ch->flush_event_req_buffers);

			if (!list_empty(&ch->event_req_buffers)) {
				ch->curr_ereq =
					container_of(ch->event_req_buffers.next,
						struct event_req, list);
				list_del_init(&ch->curr_ereq->list);
				ch->curr_ereq->num_events = 0;
				ch->curr_ereq->start = ch->evt_buf_rp;
			} else {
				mhi_log(mreq->vf_id, MHI_MSG_ERROR,
						"evt req buffers empty\n");
				ch->curr_ereq = NULL;
			}
		}
		return 0;
	}

	mhi_log(mreq->vf_id, MHI_MSG_ERROR, "ieot is not valid\n");
	return -EINVAL;
}

/**
 * mhi_transfer_host_to_device_mhi_dma - memcpy equivalent API to transfer data
 *					from host to the device.
 * @dev:	Physical destination virtual address.
 * @host_pa:	Source physical address.
 * @len:	Number of bytes to be transferred.
 * @mhi:	MHI dev structure.
 * @mreq:	mhi_req structure
 */
int mhi_transfer_host_to_device_mhi_dma(void *dev, uint64_t host_pa, uint32_t len,
		struct mhi_dev *mhi, struct mhi_req *mreq)
{
	int rc = 0;
	uint64_t bit_40 = ((u64) 1) << 40, host_addr_pa = 0, offset = 0;
	struct mhi_dev_ring *ring = NULL;
	struct mhi_dev_channel *ch;

	if (WARN_ON_ONCE(!mhi || !dev || !host_pa || !mreq))
		return -EINVAL;

	if (mhi->config_iatu) {
		offset = (uint64_t)host_pa - mhi->data_base.host_pa;
		/* Mapping the translated physical address on the device */
		host_addr_pa = (uint64_t) mhi->data_base.device_pa + offset;
	} else {
		host_addr_pa = host_pa | bit_40;
	}

	ch = mreq->client->channel;
	if (mreq->mode == DMA_SYNC) {
		mreq->dma = map_single(mhi, dev, len, DMA_FROM_DEVICE);
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Sync: device 0x%llx <-- host 0x%llx, size %d\n",
				(uint64_t) mreq->dma, host_addr_pa, (int) len);
		rc = mhi_dma_fun_ops->mhi_dma_sync_memcpy(mreq->dma, host_addr_pa,
				len,
				mhi->mhi_dma_fun_params);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"error while reading ch_id:%d using sync, rc\n",
				ch->ch_id);
			unmap_single(mhi, mreq->dma, len, DMA_FROM_DEVICE);
			return rc;
		}
		unmap_single(mhi, mreq->dma, len, DMA_FROM_DEVICE);
	} else if (mreq->mode == DMA_ASYNC) {
		ch = mreq->client->channel;
		ring = ch->ring;
		mreq->dma = map_single(mhi, dev, len, DMA_FROM_DEVICE);
		mhi_dev_ring_inc_index(ring, ring->rd_offset);

		if (ring->rd_offset == ring->wr_offset) {
			mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Setting snd_cmpl to 1 for ch_id:%d\n",
				ch->ch_id);
			mreq->snd_cmpl = true;
		}

		/* Queue the completion event for the current transfer */
		rc = mhi_dev_queue_transfer_completion(mreq, NULL);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Failed to queue completion for ch_id:%d, rc %d\n",
				ch->ch_id, rc);
			return rc;
		}
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Async: device 0x%llx <-- host 0x%llx, size %d\n",
				mreq->dma, host_addr_pa, (int) len);
		rc = mhi_dma_fun_ops->mhi_dma_async_memcpy(mreq->dma, host_addr_pa,
				(int) len,
				mhi->mhi_dma_fun_params,
				mhi_dev_transfer_completion_cb,
				mreq);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"DMA read error %d for ch_id:%d\n",
				rc, ch->ch_id);
			/* Roll back the completion event that we wrote above */
			mhi_dev_rollback_compl_evt(ch);
			/* Unmap the buffer */
			unmap_single(mhi, mreq->dma, len, DMA_FROM_DEVICE);
			return rc;
		}
	}
	return rc;
}

/**
 * mhi_dev_write_to_host_mhi_dma - memcpy equivalent API to transfer data
 *		from device to the host.
 * @host_addr:	Physical destination address.
 * @dev:	Source virtual address.
 * @len:	Number of bytes to be transferred.
 * @mhi:	MHI dev structure.
 * @req:	mhi_req structure
 */
int mhi_transfer_device_to_host_mhi_dma(uint64_t host_addr, void *dev, uint32_t len,
		struct mhi_dev *mhi, struct mhi_req *req)
{
	uint64_t bit_40 = ((u64) 1) << 40, host_addr_pa = 0, offset = 0;
	struct mhi_dev_ring *ring = NULL;
	bool flush;
	struct mhi_dev_channel *ch;
	bool snd_cmpl;
	int rc;

	if (WARN_ON(!mhi || !dev || !req  || !host_addr))
		return -EINVAL;

	if (mhi->config_iatu) {
		offset = (uint64_t)host_addr - mhi->data_base.host_pa;
		/* Mapping the translated physical address on the device */
		host_addr_pa = (uint64_t) mhi->data_base.device_pa + offset;
	} else {
		host_addr_pa = host_addr | bit_40;
	}

	if (req->mode == DMA_SYNC) {
		req->dma = map_single(mhi, dev, len, DMA_TO_DEVICE);
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Sync: device 0x%llx ---> host 0x%llx, size %d\n",
				(uint64_t) req->dma,
				host_addr_pa, (int) len);
		rc =  mhi_dma_fun_ops->mhi_dma_sync_memcpy(host_addr_pa,
				(u64) req->dma,
				(int) len,
				mhi->mhi_dma_fun_params);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Error sending data to host\n");
			unmap_single(mhi, req->dma, len, DMA_TO_DEVICE);
			return rc;
		}
		unmap_single(mhi, req->dma, len, DMA_TO_DEVICE);
	} else if (req->mode == DMA_ASYNC) {
		ch = req->client->channel;

		req->dma = map_single(mhi, req->buf, req->len, DMA_TO_DEVICE);

		ring = ch->ring;
		mhi_dev_ring_inc_index(ring, ring->rd_offset);
		if (ring->rd_offset == ring->wr_offset)
			req->snd_cmpl = true;
		snd_cmpl = req->snd_cmpl;

		/* Queue the completion event for the current transfer */
		rc = mhi_dev_queue_transfer_completion(req, &flush);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Failed to queue completion: %d\n", rc);
			return rc;
		}
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Async: device 0x%llx ---> host 0x%llx, size %d\n",
				(uint64_t) req->dma,
				host_addr_pa, (int) len);

		rc = mhi_dma_fun_ops->mhi_dma_async_memcpy(host_addr_pa,
				(uint64_t) req->dma,
				(int) len,
				mhi->mhi_dma_fun_params,
				mhi_dev_transfer_completion_cb,
				req);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Error sending data to host\n");
			/* Roll back the completion event that we wrote above */
			mhi_dev_rollback_compl_evt(ch);
			/* Unmap the buffer */
			unmap_single(mhi, req->dma, req->len, DMA_TO_DEVICE);
			return rc;
		}
		if (snd_cmpl || flush) {
			rc = mhi_dev_flush_transfer_completion_events(mhi, ch, req->snd_cmpl);
			if (rc) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"Failed to flush write completions to host\n");
				return rc;
			}
		}
	}
	return 0;
}

/**
 * mhi_dev_read_from_host_edma - memcpy equivalent API to transfer data
 *		from host to device.
 * @mhi:	MHI dev structure.
 * @transfer:	Host and device address details.
 */
void mhi_dev_read_from_host_edma(struct mhi_dev *mhi, struct mhi_addr *transfer)
{
	uint64_t host_addr_pa = 0, offset = 0;
	struct dma_async_tx_descriptor *descriptor;

	reinit_completion(&read_from_host);

	if (mhi->config_iatu) {
		offset = (uint64_t) transfer->host_pa - mhi->ctrl_base.host_pa;
		/* Mapping the translated physical address on the device */
		host_addr_pa = (uint64_t) mhi->ctrl_base.device_pa + offset;
	} else {
		host_addr_pa = transfer->host_pa;
	}

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
		"device 0x%llx <<-- host 0x%llx, size %d\n",
		transfer->phy_addr, host_addr_pa,
		(int) transfer->size);

	descriptor = dmaengine_prep_dma_memcpy(mhi->mhi_hw_ctx->rx_dma_chan,
				transfer->phy_addr, host_addr_pa,
				(int)transfer->size, DMA_PREP_INTERRUPT);
	if (!descriptor) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "descriptor is null\n");
		return;
	}
	descriptor->callback_param = &read_from_host;
	descriptor->callback = mhi_dev_edma_sync_cb;
	dma_async_issue_pending(mhi->mhi_hw_ctx->rx_dma_chan);

	if (!wait_for_completion_timeout
			(&read_from_host, msecs_to_jiffies(1000)))
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "read from host timed out\n");
}

/**
 * mhi_dev_write_to_host_edma - Transfer data from device to host using eDMA.
 * @mhi:	MHI dev structure.
 * @transfer:	Host and device address details.
 * @ereq:	event_req structure.
 * @tr_type:	Transfer type.
 */
void mhi_dev_write_to_host_edma(struct mhi_dev *mhi, struct mhi_addr *transfer,
		struct event_req *ereq, enum mhi_dev_transfer_type tr_type)
{
	uint64_t host_addr_pa = 0, offset = 0;
	dma_addr_t dma;
	struct dma_async_tx_descriptor *descriptor;
	void (*cb_func)(void *req);

	if (mhi->config_iatu) {
		offset = (uint64_t) transfer->host_pa - mhi->ctrl_base.host_pa;
		/* Mapping the translated physical address on the device */
		host_addr_pa = (uint64_t) mhi->ctrl_base.device_pa + offset;
	} else {
		host_addr_pa = transfer->host_pa;
	}

	if (tr_type == MHI_DEV_DMA_ASYNC) {
		/*
		 * Event read pointer memory is dma_alloc_coherent memory
		 * don't need to dma_map. Assigns the physical address in
		 * phy_addr.
		 */
		if (transfer->phy_addr) {
			dma = transfer->phy_addr;
		} else {
			dma = map_single(mhi,
				transfer->virt_addr, transfer->size,
				DMA_TO_DEVICE);
			if (dma_mapping_error(&mhi->mhi_hw_ctx->pdev->dev, dma)) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"dma mapping failed\n");
				return;
			}
		}

		if (ereq->event_type == SEND_EVENT_BUFFER) {
			ereq->dma = dma;
			ereq->dma_len = transfer->size;
			cb_func = mhi_dev_event_buf_completion_cb;
		} else if (ereq->event_type == SEND_EVENT_RD_OFFSET) {
			/*
			 * Event read pointer memory is dma_alloc_coherent
			 * memory. Don't need to dma_unmap.
			 */
			cb_func = mhi_dev_event_rd_offset_completion_cb;
		} else {
			if (!ereq->is_cmd_cpl)
				cb_func = mhi_dev_event_msi_cb;
			else
				cb_func = mhi_dev_cmd_event_msi_cb;
		}
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Async: device 0x%llx --> host 0x%llx, size %d, type = %d\n",
				dma, host_addr_pa,
				(int) transfer->size, tr_type);

		descriptor = dmaengine_prep_dma_memcpy(
				mhi->mhi_hw_ctx->tx_dma_chan, host_addr_pa,
				dma, (int)transfer->size,
				DMA_PREP_INTERRUPT);
		if (!descriptor) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "descriptor is null\n");
			unmap_single(mhi,
				(size_t)transfer->virt_addr, transfer->size,
				DMA_TO_DEVICE);
			return;
		}
		descriptor->callback_param = ereq;
		descriptor->callback = cb_func;
		dma_async_issue_pending(mhi->mhi_hw_ctx->tx_dma_chan);
	} else if (tr_type == MHI_DEV_DMA_SYNC) {
		reinit_completion(&write_to_host);

		if (transfer->phy_addr) {
			dma = transfer->phy_addr;
		} else {
			dma = map_single(mhi,
				transfer->virt_addr, transfer->size,
				DMA_TO_DEVICE);
			if (dma_mapping_error(&mhi->mhi_hw_ctx->pdev->dev, dma)) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
						"dma mapping failed\n");
				return;
			}
		}

		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Sync: device 0x%llx --> host 0x%llx, size %d, type = %d\n",
				dma, host_addr_pa,
				(int) transfer->size, tr_type);

		descriptor = dmaengine_prep_dma_memcpy(
				mhi->mhi_hw_ctx->tx_dma_chan, host_addr_pa,
				dma, (int)transfer->size,
				DMA_PREP_INTERRUPT);
		if (!descriptor) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "descriptor is null\n");
			if (!transfer->phy_addr)
				unmap_single(mhi, dma, transfer->size, DMA_TO_DEVICE);
			return;
		}

		descriptor->callback_param = &write_to_host;
		descriptor->callback = mhi_dev_edma_sync_cb;
		dma_async_issue_pending(mhi_hw_ctx->tx_dma_chan);
		if (!wait_for_completion_timeout
			(&write_to_host, msecs_to_jiffies(1000)))
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "write to host timed out\n");
		if (!transfer->phy_addr)
			unmap_single(mhi, dma, transfer->size, DMA_TO_DEVICE);
	}
}

/**
 * mhi_transfer_host_to_device_edma - memcpy equivalent API to transfer data
 *		from host to the device.
 * @dev:	Physical destination virtual address.
 * @host_pa:	Source physical address.
 * @len:	Number of bytes to be transferred.
 * @mhi:	MHI dev structure.
 * @mreq:	mhi_req structure
 */
int mhi_transfer_host_to_device_edma(void *dev, uint64_t host_pa, uint32_t len,
		struct mhi_dev *mhi, struct mhi_req *mreq)
{
	uint64_t host_addr_pa = 0, offset = 0;
	struct mhi_dev_ring *ring;
	struct dma_async_tx_descriptor *descriptor;
	struct mhi_dev_channel *ch;
	int rc;

	if (WARN_ON(!mhi || !dev || !host_pa || !mreq))
		return -EINVAL;

	if (mhi->config_iatu) {
		offset = (uint64_t)host_pa - mhi->data_base.host_pa;
		/* Mapping the translated physical address on the device */
		host_addr_pa = (uint64_t) mhi->data_base.device_pa + offset;
	} else {
		host_addr_pa = host_pa;
	}

	if (mreq->mode == DMA_SYNC) {
		reinit_completion(&transfer_host_to_device);

		mreq->dma = map_single(mhi, dev, len, DMA_FROM_DEVICE);
		if (dma_mapping_error(&mhi->mhi_hw_ctx->pdev->dev, mreq->dma)) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "dma map single failed\n");
			return -ENOMEM;
		}
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Sync: device 0x%llx <-- host 0x%llx, size %d\n",
				mreq->dma, host_addr_pa, (int) len);
		descriptor = dmaengine_prep_dma_memcpy(
				mhi->mhi_hw_ctx->rx_dma_chan, mreq->dma,
				host_addr_pa, (int)len,
				DMA_PREP_INTERRUPT);
		if (!descriptor) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "descriptor is null\n");
			unmap_single(mhi, (size_t)dev, len, DMA_FROM_DEVICE);
			return -EFAULT;
		}
		descriptor->callback_param = &transfer_host_to_device;
		descriptor->callback = mhi_dev_edma_sync_cb;
		dma_async_issue_pending(mhi->mhi_hw_ctx->rx_dma_chan);
		if (!wait_for_completion_timeout
			(&transfer_host_to_device, msecs_to_jiffies(1000))) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"transfer host to device timed out\n");
			unmap_single(mhi, (size_t)dev, len, DMA_FROM_DEVICE);
			return -ETIMEDOUT;
		}
		unmap_single(mhi, (size_t)dev, len, DMA_FROM_DEVICE);
	} else if (mreq->mode == DMA_ASYNC) {
		ch = mreq->client->channel;
		ring = ch->ring;
		mreq->dma = map_single(mhi, dev, len, DMA_FROM_DEVICE);
		if (dma_mapping_error(&mhi->mhi_hw_ctx->pdev->dev, mreq->dma)) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "dma map single failed\n");
			return -ENOMEM;
		}

		mhi_dev_ring_inc_index(ring, ring->rd_offset);

		if (ring->rd_offset == ring->wr_offset) {
			mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Setting snd_cmpl to 1 for ch_id:%d\n",
				ch->ch_id);
			mreq->snd_cmpl = true;
		}

		/* Queue the completion event for the current transfer */
		rc = mhi_dev_queue_transfer_completion(mreq, NULL);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Failed to queue completion: %d\n", rc);
			return rc;
		}

		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Async: device 0x%llx <-- host 0x%llx, size %d\n",
				mreq->dma, host_addr_pa, (int) len);
		descriptor = dmaengine_prep_dma_memcpy(
				mhi->mhi_hw_ctx->rx_dma_chan, mreq->dma,
				host_addr_pa, (int)len,
				DMA_PREP_INTERRUPT);
		if (!descriptor) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "descriptor is null\n");
			/* Roll back the completion event that we wrote above */
			mhi_dev_rollback_compl_evt(ch);
			unmap_single(mhi, (size_t)dev, len, DMA_FROM_DEVICE);
			return -EFAULT;
		}
		descriptor->callback_param = mreq;
		descriptor->callback =
			mhi_dev_transfer_completion_cb;
		dma_async_issue_pending(mhi->mhi_hw_ctx->rx_dma_chan);
	}

	return 0;
}

/**
 * mhi_transfer_device_to_host_edma - memcpy equivalent API to transfer data
 *		from device to the host.
 * @host_addr:	Physical destination address.
 * @dev:	Source virtual address.
 * @len:	Number of bytes to be transferred.
 * @mhi:	MHI dev structure.
 * @req:	mhi_req structure
 */
int mhi_transfer_device_to_host_edma(uint64_t host_addr, void *dev,
		uint32_t len, struct mhi_dev *mhi, struct mhi_req *req)
{
	uint64_t host_addr_pa = 0, offset = 0;
	struct mhi_dev_ring *ring;
	struct dma_async_tx_descriptor *descriptor;
	bool flush;
	bool snd_cmpl;
	struct mhi_dev_channel *ch;
	int rc;

	if (WARN_ON(!mhi || !dev || !req  || !host_addr))
		return -EINVAL;

	if (mhi->config_iatu) {
		offset = (uint64_t)host_addr - mhi->data_base.host_pa;
		/* Mapping the translated physical address on the device */
		host_addr_pa = (uint64_t) mhi->data_base.device_pa + offset;
	} else {
		host_addr_pa = host_addr;
	}

	if (req->mode == DMA_SYNC) {
		reinit_completion(&transfer_device_to_host);

		req->dma = map_single(mhi, dev, len, DMA_TO_DEVICE);
		if (dma_mapping_error(&mhi->mhi_hw_ctx->pdev->dev, req->dma)) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "dma map single failed\n");
			return -ENOMEM;
		}
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Sync: device 0x%llx ---> host 0x%llx, size %d\n",
				req->dma,
				host_addr_pa, (int) len);

		descriptor = dmaengine_prep_dma_memcpy(mhi->mhi_hw_ctx->tx_dma_chan,
			host_addr_pa, req->dma,
			(int)len, DMA_PREP_INTERRUPT);
		if (!descriptor) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "descriptor is null\n");
			unmap_single(mhi, (size_t)req->buf, req->len, DMA_TO_DEVICE);
			return -EFAULT;
		}
		descriptor->callback_param = &transfer_device_to_host;
		descriptor->callback = mhi_dev_edma_sync_cb;
		dma_async_issue_pending(mhi->mhi_hw_ctx->tx_dma_chan);

		if (!wait_for_completion_timeout
			(&transfer_device_to_host, msecs_to_jiffies(1000))) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"transfer device to host timed out\n");
			unmap_single(mhi, (size_t)req->buf, req->len, DMA_TO_DEVICE);
			return -ETIMEDOUT;
		}
		unmap_single(mhi, (size_t)req->buf, req->len, DMA_TO_DEVICE);
	} else if (req->mode == DMA_ASYNC) {
		ch = req->client->channel;
		req->dma = map_single(mhi, req->buf, req->len, DMA_TO_DEVICE);
		if (dma_mapping_error(&mhi->mhi_hw_ctx->pdev->dev, req->dma)) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "dma map single failed\n");
			return -ENOMEM;
		}

		ring = ch->ring;
		mhi_dev_ring_inc_index(ring, ring->rd_offset);
		if (ring->rd_offset == ring->wr_offset)
			req->snd_cmpl = true;
		snd_cmpl = req->snd_cmpl;

		/* Queue the completion event for the current transfer */
		rc = mhi_dev_queue_transfer_completion(req, &flush);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Failed to queue completion: %d\n", rc);
			return rc;
		}
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Async: device 0x%llx ---> host 0x%llx, size %d\n",
				req->dma,
				host_addr_pa, (int) len);

		descriptor = dmaengine_prep_dma_memcpy(mhi->mhi_hw_ctx->tx_dma_chan,
			host_addr_pa, req->dma, (int) len,
			DMA_PREP_INTERRUPT);
		if (!descriptor) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "descriptor is null\n");
			/* Roll back the completion event that we wrote above */
			mhi_dev_rollback_compl_evt(ch);
			/* Unmap the buffer */
			unmap_single(mhi, (size_t)req->buf, req->len, DMA_TO_DEVICE);
			return -EFAULT;
		}
		descriptor->callback_param = req;
		descriptor->callback = mhi_dev_transfer_completion_cb;

		dma_async_issue_pending(mhi->mhi_hw_ctx->tx_dma_chan);

		if (snd_cmpl || flush) {
			rc = mhi_dev_flush_transfer_completion_events(mhi, ch, req->snd_cmpl);
			if (rc) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"Failed to flush write completions to host\n");
				return rc;
			}
		}
	}

	return 0;
}

int mhi_dev_is_list_empty(struct mhi_dev *mhi_ctx)
{
	if (list_empty(&mhi_ctx->event_ring_list) &&
			list_empty(&mhi_ctx->process_ring_list))
		return 0;

	return 1;
}
EXPORT_SYMBOL_GPL(mhi_dev_is_list_empty);

static void mhi_dev_get_erdb_db_cfg(struct mhi_dev *mhi,
				struct ep_pcie_db_config *erdb_cfg)
{
	if (mhi->cfg.event_rings == NUM_CHANNELS) {
		erdb_cfg->base = mhi->mhi_chan_hw_base;
		if (mhi->enable_m2)
			erdb_cfg->end = HW_CHANNEL_END-1;
		else
			erdb_cfg->end = HW_CHANNEL_END;
	} else {
		erdb_cfg->base = mhi->cfg.event_rings -
					(mhi->cfg.hw_event_rings);
		erdb_cfg->end =  mhi->cfg.event_rings -
					MHI_HW_ACC_EVT_RING_END;
	}
}

int mhi_pcie_config_db_routing(struct mhi_dev *mhi)
{
	struct ep_pcie_db_config chdb_cfg, erdb_cfg;

	if (WARN_ON(!mhi))
		return -EINVAL;

	/* Configure Doorbell routing */
	chdb_cfg.base = mhi->mhi_chan_hw_base;
	if (mhi->enable_m2)
		chdb_cfg.end = HW_CHANNEL_END-1;
	else
		chdb_cfg.end = HW_CHANNEL_END;
	chdb_cfg.tgt_addr = (uint32_t) mhi->mhi_dma_uc_mbox_crdb;
	if (mhi->mhi_has_smmu) {
		chdb_cfg.tgt_addr = (uint32_t) dma_map_resource(&mhi->mhi_hw_ctx->pdev->dev,
					(phys_addr_t)(chdb_cfg.tgt_addr),
					8 * (HW_CHANNEL_END - (mhi->mhi_chan_hw_base)),
					DMA_BIDIRECTIONAL,
					0);
	}

	mhi_dev_get_erdb_db_cfg(mhi, &erdb_cfg);

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
		"Event rings 0x%x => er_base 0x%x, er_end %d\n",
		mhi->cfg.event_rings, erdb_cfg.base, erdb_cfg.end);
	erdb_cfg.tgt_addr = (uint32_t) mhi->mhi_dma_uc_mbox_erdb;
	if (mhi->mhi_has_smmu) {
		erdb_cfg.tgt_addr = (uint32_t)dma_map_resource(&mhi->mhi_hw_ctx->pdev->dev,
					(phys_addr_t)(erdb_cfg.tgt_addr),
					8 * (mhi->cfg.event_rings),
					DMA_BIDIRECTIONAL,
					0);
	}
	ep_pcie_config_db_routing(mhi->mhi_hw_ctx->phandle, chdb_cfg, erdb_cfg,
					mhi->vf_id);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_pcie_config_db_routing);

static int mhi_enable_int(struct mhi_dev *mhi_ctx)
{
	int rc = 0;

	mhi_log(mhi_ctx->vf_id, MHI_MSG_VERBOSE,
		"Enable ctrl and cmdb interrupts\n");

	rc = mhi_dev_mmio_enable_ctrl_interrupt(mhi_ctx);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
			"Failed to enable control interrupt: %d\n", rc);
		return rc;
	}

	rc = mhi_dev_mmio_enable_cmdb_interrupt(mhi_ctx);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
			"Failed to enable command db: %d\n", rc);
		return rc;
	}
	mhi_update_state_info(mhi_ctx, MHI_STATE_CONNECTED);
	if (!mhi_ctx->mhi_int)
		ep_pcie_mask_irq_event(mhi_ctx->mhi_hw_ctx->phandle,
				EP_PCIE_INT_EVT_MHI_A7, true);
	return 0;
}

static int mhi_hwc_init(struct mhi_dev *mhi_ctx)
{
	int rc = 0;
	struct ep_pcie_msi_config cfg;
	struct mhi_dma_init_params mhi_init_dma_params;
	struct mhi_dma_init_out mhi_init_dma_out;
	struct ep_pcie_db_config erdb_cfg;
	struct mhi_dma_function_params mhi_dma_fun_params;

	if (mhi_ctx->use_edma || mhi_ctx->no_path_from_ipa_to_pcie) {
		/*
		 * Interrupts are enabled during the MHI DMA callback
		 * once the MHI DMA HW is ready. Callback is not triggerred
		 * in case of edma, hence enable interrupts.
		 */
		rc = mhi_enable_int(mhi_ctx);
		if (rc)
			mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
				"Error configuring interrupts: rc = %d\n", rc);
		return rc;
	}

	/* Call MHI DMA HW_ACC Init with MSI Address and db routing info */
	rc = mhi_dev_get_msi_config(mhi_ctx->mhi_hw_ctx->phandle, &cfg, mhi_ctx->vf_id);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
			"Error retrieving pcie msi logic\n");
		return rc;
	}

	memset(&mhi_init_dma_params, 0, sizeof(mhi_init_dma_params));
	mhi_init_dma_params.msi.addr_hi = cfg.upper;
	mhi_init_dma_params.msi.addr_low = cfg.lower;
	mhi_init_dma_params.msi.data = cfg.data;
	mhi_init_dma_params.msi.mask = cfg.msg_num;
	mhi_init_dma_params.first_er_idx = mhi_ctx->cfg.event_rings -
						(mhi_ctx->cfg.hw_event_rings);
	mhi_init_dma_params.first_ch_idx = mhi_ctx->mhi_chan_hw_base;
	mhi_init_dma_params.disable_msi = mhi_ctx->msi_disable;

	if (mhi_ctx->config_iatu)
		mhi_init_dma_params.mmio_addr =
			((uint32_t) mhi_ctx->mmio_base_pa_addr) + MHI_REGLEN;
	else
		mhi_init_dma_params.mmio_addr =
			((uint32_t) mhi_ctx->mmio_base_pa_addr);

	if (!mhi_ctx->config_iatu)
		mhi_init_dma_params.assert_bit40 = true;
	mhi_log(mhi_ctx->vf_id, MHI_MSG_VERBOSE,
			"MMIO Addr 0x%x, MSI config: U:0x%x L: 0x%x D: 0x%x vf_id:%d\n",
			mhi_init_dma_params.mmio_addr, cfg.upper, cfg.lower, cfg.data,
			mhi_ctx->vf_id - 1);
	mhi_init_dma_params.notify = mhi_hwc_cb;
	mhi_init_dma_params.priv = mhi_ctx;
	mhi_dma_fun_params.vf_id = mhi_ctx->vf_id - 1;

	rc = mhi_dma_fun_ops->mhi_dma_init(mhi_ctx->mhi_dma_fun_params,
			&mhi_init_dma_params, &mhi_init_dma_out);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR, "mhi_dma_init fail:%d", rc);
		return rc;
	}

	mhi_ctx->mhi_dma_uc_mbox_crdb = mhi_init_dma_out.ch_db_fwd_base;
	mhi_ctx->mhi_dma_uc_mbox_erdb = mhi_init_dma_out.ev_db_fwd_base;

	mhi_dev_get_erdb_db_cfg(mhi_ctx, &erdb_cfg);
	mhi_log(mhi_ctx->vf_id, MHI_MSG_VERBOSE,
		"Event rings 0x%x => er_base 0x%x, er_end %d\n",
		mhi_ctx->cfg.event_rings, erdb_cfg.base, erdb_cfg.end);

	erdb_cfg.tgt_addr = (uint32_t) mhi_ctx->mhi_dma_uc_mbox_erdb;

	rc = mhi_pcie_config_db_routing(mhi_ctx);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR, "Error configuring DB routing\n");
		return rc;
	}
	return rc;
}

static int mhi_hwc_start(struct mhi_dev *mhi)
{
	struct mhi_dma_start_params mhi_dma_start_param;

	memset(&mhi_dma_start_param, 0, sizeof(mhi_dma_start_param));
	if (mhi->config_iatu) {
		mhi_dma_start_param.host_ctrl_addr = mhi->ctrl_base.device_pa;
		mhi_dma_start_param.host_data_addr = mhi->data_base.device_pa;
	} else {
		mhi_dma_start_param.channel_context_array_addr =
				mhi->ch_ctx_shadow.host_pa;
		mhi_dma_start_param.event_context_array_addr =
				mhi->ev_ctx_shadow.host_pa;
	}

	return mhi_dma_fun_ops->mhi_dma_start(mhi->mhi_dma_fun_params, &mhi_dma_start_param);
}

static void mhi_hwc_cb(void *priv, enum mhi_dma_event_type event,
	unsigned long data)
{
	int rc = 0;
	enum mhi_dev_state state;
	enum mhi_dev_event mhi_event = 0;
	u32 mhi_reset;
	struct mhi_dev *mhi_ctx = (struct mhi_dev *)priv;

	mutex_lock(&mhi_ctx->mhi_lock);
	switch (event) {
	case MHI_DMA_EVENT_READY:
		mhi_log(mhi_ctx->vf_id, MHI_MSG_INFO,
			"HW ch uC is ready event=0x%X\n", event);
		rc = mhi_hwc_start(mhi_ctx);
		if (rc) {
			mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
				"hwc_init start failed with %d\n", rc);
			goto err;
		}

		rc = mhi_dev_mmio_get_mhi_state(mhi_ctx, &state, &mhi_reset);
		if (rc) {
			mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR, "get mhi state failed\n");
			goto err;
		}

		if (state == MHI_DEV_M0_STATE && !mhi_reset) {
			mhi_dev_get_event_notify(MHI_DEV_M0_STATE, &mhi_event);
			mhi_dev_notify_sm_event(mhi_ctx, mhi_event);
		}

		if (mhi_ctx->config_iatu || mhi_ctx->mhi_int) {
			mhi_ctx->mhi_int_en = true;
			enable_irq(mhi_ctx->mhi_irq);
		}

		rc = mhi_enable_int(mhi_ctx);
		if (rc) {
			mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
				"Error configuring interrupts, rc = %d\n", rc);
			goto err;
		}

		mhi_log(mhi_ctx->vf_id, MHI_MSG_INFO, "Device in M0 State\n");
		break;
	case MHI_DMA_EVENT_DATA_AVAILABLE:
		rc = mhi_dev_notify_sm_event(mhi_ctx, MHI_DEV_EVENT_HW_ACC_WAKEUP);
		if (rc) {
			mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
				"Event HW_ACC_WAKEUP failed with %d\n", rc);
			goto err;
		}
		break;
	case MHI_DMA_EVENT_SSR_RESET:
		mhi_log(mhi_ctx->vf_id, MHI_MSG_INFO,
			"SSR event notified on vf_id%d", mhi_ctx->vf_id);
		/*
		 * Queue the Channel error handling to SM layer to avoid any
		 * race conditions with M2/M3 state handling.
		 */
		mhi_dev_notify_sm_event(mhi_ctx, MHI_DEV_EVENT_CHANNEL_ERROR);
		break;
	default:
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
			"HW ch uC unknown event 0x%X\n", event);
		break;
	}
err:
	mutex_unlock(&mhi_ctx->mhi_lock);
	return;

}

static int mhi_hwc_chcmd(struct mhi_dev *mhi, uint chid,
				enum mhi_dev_ring_element_type_id type)
{
	int rc = -EINVAL;
	struct mhi_dma_connect_params connect_params;
	struct mhi_dma_disconnect_params disconnect_params;

	memset(&connect_params, 0, sizeof(connect_params));
	memset(&disconnect_params, 0, sizeof(disconnect_params));

	switch (type) {
	case MHI_DEV_RING_EL_RESET:
	case MHI_DEV_RING_EL_STOP:
		if ((chid-(mhi->mhi_chan_hw_base)) >= NUM_HW_CHANNELS) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Invalid HW ch_id:%d\n", chid);
			return -EINVAL;
		}

		disconnect_params.clnt_hdl = mhi->dma_clnt_hndl[chid-(mhi->mhi_chan_hw_base)];

		rc = mhi_dma_fun_ops->mhi_dma_disconnect_endp(mhi->mhi_dma_fun_params,
				&disconnect_params);
		if (rc)
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Stopping HW ch_id:%d failed 0x%X\n",
							chid, rc);
		break;
	case MHI_DEV_RING_EL_START:

		if (chid > HW_CHANNEL_END) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"ch DB for ch_id:%d not enabled\n", chid);
			return -EINVAL;
		}

		if ((chid-(mhi->mhi_chan_hw_base)) >= NUM_HW_CHANNELS) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Invalid HW ch_id:%d\n", chid);
			return -EINVAL;
		}

		connect_params.channel_id = chid;

		rc = mhi_dma_fun_ops->mhi_dma_connect_endp(mhi->mhi_dma_fun_params,
				&connect_params,
				&mhi->dma_clnt_hndl[chid-(mhi->mhi_chan_hw_base)]);
		if (rc)
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"HW ch_id:%d start failed : %d\n",
							chid, rc);
		break;
	case MHI_DEV_RING_EL_INVALID:
	default:
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Invalid Ring Element type = 0x%X\n", type);
		break;
	}

	return rc;
}

int mhi_dev_get_ep_mapping(int channel_id)
{
	int pipe_num = 0;

	pipe_num = mhi_dma_fun_ops->mhi_dma_get_ep_mapping(channel_id);
	return pipe_num;
}
EXPORT_SYMBOL_GPL(mhi_dev_get_ep_mapping);

static void mhi_dev_core_ack_ctrl_interrupts(struct mhi_dev *dev,
							uint32_t *int_value)
{
	int rc = 0;

	rc = mhi_dev_mmio_read(dev, MHI_CTRL_INT_STATUS_A7, int_value);
	if (rc) {
		mhi_log(dev->vf_id, MHI_MSG_ERROR, "Failed to read A7 status\n");
		return;
	}

	rc = mhi_dev_mmio_write(dev, MHI_CTRL_INT_CLEAR_A7, *int_value);
	if (rc) {
		mhi_log(dev->vf_id, MHI_MSG_ERROR, "Failed to clear A7 status\n");
		return;
	}
}

static void mhi_dev_fetch_ch_ctx(struct mhi_dev *mhi, uint32_t ch_id)
{
	struct mhi_addr data_transfer;

	if (MHI_USE_DMA(mhi)) {
		data_transfer.host_pa = mhi->ch_ctx_shadow.host_pa +
					sizeof(struct mhi_dev_ch_ctx) * ch_id;
		data_transfer.phy_addr = mhi->ch_ctx_cache_dma_handle +
					sizeof(struct mhi_dev_ch_ctx) * ch_id;
	}

	data_transfer.size  = sizeof(struct mhi_dev_ch_ctx);
	/* Fetch the channel ctx (*dst, *src, size) */
	mhi->read_from_host(mhi, &data_transfer);
}

int mhi_dev_syserr(struct mhi_dev *mhi)
{
	if (WARN_ON(!mhi))
		return -EINVAL;

	mhi_log(mhi->vf_id, MHI_MSG_ERROR, "MHI dev sys error\n");

	return mhi_dev_dump_mmio(mhi);
}
EXPORT_SYMBOL_GPL(mhi_dev_syserr);

int mhi_dev_send_event(struct mhi_dev *mhi, int evnt_ring,
					union mhi_dev_ring_element_type *el)
{
	int rc = 0;
	uint64_t evnt_ring_idx = mhi->ev_ring_start + evnt_ring;
	struct mhi_dev_ring *ring = mhi->ring[evnt_ring_idx];
	union mhi_dev_ring_ctx *ctx;
	struct ep_pcie_msi_config cfg;
	struct mhi_addr transfer_addr;

	rc = mhi_dev_get_msi_config(mhi->mhi_hw_ctx->phandle, &cfg, mhi->vf_id);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Error retrieving pcie msi logic\n");
		return rc;
	}

	if (evnt_ring_idx > mhi->cfg.event_rings) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Invalid event ring_id: %lld\n", evnt_ring_idx);
		return -EINVAL;
	}

	ctx = (union mhi_dev_ring_ctx *)&mhi->ev_ctx_cache[evnt_ring];
	if (mhi_ring_get_state(ring) == RING_STATE_UINT) {
		rc = mhi_ring_start(ring, ctx, mhi);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"error starting event ring_id:%llu\n",
				evnt_ring_idx);
			return rc;
		}
	}

	mutex_lock(&ring->event_lock);
	/* add the ring element */
	mhi_dev_add_element(ring, el, NULL, 0);

	ring->ring_ctx_shadow->ev.rp =  (ring->rd_offset *
				sizeof(union mhi_dev_ring_element_type)) +
				ring->ring_ctx->generic.rbase;

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "ev.rp = %llx for %lld\n",
				ring->ring_ctx_shadow->ev.rp, evnt_ring_idx);

	if (MHI_USE_DMA(mhi))
		transfer_addr.host_pa = (mhi->ev_ctx_shadow.host_pa +
			sizeof(struct mhi_dev_ev_ctx) *
			evnt_ring) + (size_t) &ring->ring_ctx->ev.rp -
			(size_t) ring->ring_ctx;
	else
		transfer_addr.device_va = (mhi->ev_ctx_shadow.device_va +
			sizeof(struct mhi_dev_ev_ctx) *
			evnt_ring) + (size_t) &ring->ring_ctx->ev.rp -
			(size_t) ring->ring_ctx;

	ring->evt_rp_cache[ring->rd_offset] = ring->ring_ctx_shadow->ev.rp;
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "Caching rp %llx for rd offset %zu\n",
			ring->evt_rp_cache[ring->rd_offset], ring->rd_offset);
	transfer_addr.virt_addr = &ring->ring_ctx_shadow->ev.rp;
	transfer_addr.size = sizeof(uint64_t);
	transfer_addr.phy_addr = ring->evt_rp_cache_dma_handle +
			(sizeof(uint64_t) * ring->rd_offset);

	mhi->write_to_host(mhi, &transfer_addr, NULL, MHI_DEV_DMA_SYNC);
	/*
	 * rp update in host memory should be flushed
	 * before sending a MSI to the host
	 */
	wmb();

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "event sent:\n");
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "evnt ptr : 0x%llx\n", el->evt_tr_comp.ptr);
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "evnt len : 0x%x\n", el->evt_tr_comp.len);
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "evnt code :0x%x\n", el->evt_tr_comp.code);
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "evnt type :0x%x\n", el->evt_tr_comp.type);
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "evnt chid :0x%x\n", el->evt_tr_comp.chid);

	if (mhi->use_edma)
		rc = mhi_trigger_msi_edma(ring, ctx->ev.msivec, NULL);
	else
		rc = ep_pcie_trigger_msi(mhi->mhi_hw_ctx->phandle, ctx->ev.msivec, mhi->vf_id);

	mutex_unlock(&ring->event_lock);
	return rc;
}

static int mhi_dev_send_completion_event_async(struct mhi_dev_channel *ch,
			size_t rd_ofst, uint32_t len,
			enum mhi_dev_cmd_completion_code code,
			struct mhi_req *mreq)
{
	int rc;
	struct mhi_dev *mhi = ch->ring->mhi_dev;

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "Ch %d\n", ch->ch_id);

	/* Queue the completion event for the current transfer */
	mreq->snd_cmpl = true;
	rc = mhi_dev_queue_transfer_completion(mreq, NULL);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Failed to queue completion for ch_id:%d, rc %d\n",
			ch->ch_id, rc);
		return rc;
	}

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "Calling flush for ch_id %d\n", ch->ch_id);
	rc = mhi_dev_flush_transfer_completion_events(mhi, ch, mreq->snd_cmpl);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Failed to flush read completions to host\n");
		return rc;
	}

	return 0;
}

static int mhi_dev_send_completion_event(struct mhi_dev_channel *ch,
			size_t rd_ofst, uint32_t len,
			enum mhi_dev_cmd_completion_code code)
{
	int rc;

	union mhi_dev_ring_element_type *compl_event =
					kzalloc(sizeof(union mhi_dev_ring_element_type),
						GFP_KERNEL);
	struct mhi_dev *mhi = ch->ring->mhi_dev;

	compl_event->evt_tr_comp.chid = ch->ch_id;
	compl_event->evt_tr_comp.type =
				MHI_DEV_RING_EL_TRANSFER_COMPLETION_EVENT;
	compl_event->evt_tr_comp.len = len;
	compl_event->evt_tr_comp.code = code;
	compl_event->evt_tr_comp.ptr = ch->ring->ring_ctx->generic.rbase +
			rd_ofst * sizeof(struct mhi_dev_transfer_ring_element);

	rc = mhi_dev_send_event(mhi,
			mhi->ch_ctx_cache[ch->ch_id].err_indx, compl_event);
	kfree(compl_event);

	return rc;
}

int mhi_dev_send_state_change_event(struct mhi_dev *mhi,
						enum mhi_dev_state state)
{
	int rc;

	union mhi_dev_ring_element_type *event = kzalloc(sizeof(union mhi_dev_ring_element_type),
						GFP_KERNEL);

	event->evt_state_change.type = MHI_DEV_RING_EL_MHI_STATE_CHG;
	event->evt_state_change.mhistate = state;

	rc = mhi_dev_flush_cmd_completion_events(mhi, event);
	kfree(event);

	return rc;
}
EXPORT_SYMBOL_GPL(mhi_dev_send_state_change_event);

int mhi_dev_send_ee_event(struct mhi_dev *mhi, enum mhi_dev_execenv exec_env)
{
	int rc;

	union mhi_dev_ring_element_type *event = kzalloc(sizeof(union mhi_dev_ring_element_type),
						GFP_KERNEL);

	event->evt_ee_state.type = MHI_DEV_RING_EL_EE_STATE_CHANGE_NOTIFY;
	event->evt_ee_state.execenv = exec_env;

	rc = mhi_dev_flush_cmd_completion_events(mhi, event);
	kfree(event);

	return rc;
}
EXPORT_SYMBOL_GPL(mhi_dev_send_ee_event);

static void mhi_dev_trigger_cb(uint32_t vf_id, enum mhi_client_channel ch_id)
{
	struct mhi_dev_ready_cb_info *info;
	enum mhi_ctrl_info state_data;
	struct mhi_dev *mhi_ctx = mhi_get_dev_ctx(mhi_hw_ctx, vf_id);

	/* Currently no clients register for HW channel notification */
	if (ch_id >= MHI_MAX_SOFTWARE_CHANNELS)
		return;

	if (!mhi_ctx) {
		mhi_log(vf_id, MHI_MSG_ERROR, "mhi_ctx is NULL\n");
		return;
	}

	list_for_each_entry(info, &mhi_ctx->client_cb_list, list)
		if (info->cb && info->cb_data.channel == ch_id) {
			mhi_vf_ctrl_state_info(mhi_ctx->vf_id,
					       info->cb_data.channel,
					       &state_data);
			info->cb_data.ctrl_info = state_data;
			info->cb(&info->cb_data);
		}
}

int mhi_dev_trigger_hw_acc_wakeup(struct mhi_dev *mhi)
{
	/*
	 * Expected usage is when there is HW ACC traffic MHI DMA uC notifes
	 * Q6 -> MHI DMA A7 -> MHI core -> MHI SM
	 */
	return mhi_dev_notify_sm_event(mhi, MHI_DEV_EVENT_HW_ACC_WAKEUP);
}
EXPORT_SYMBOL_GPL(mhi_dev_trigger_hw_acc_wakeup);

static int mhi_dev_send_cmd_comp_event(struct mhi_dev *mhi,
				enum mhi_dev_cmd_completion_code code)
{
	int rc;

	union mhi_dev_ring_element_type *event = kzalloc(sizeof(union mhi_dev_ring_element_type),
						GFP_KERNEL);

	if (code > MHI_CMD_COMPL_CODE_RES) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Invalid cmd compl code: %d\n", code);
		return -EINVAL;
	}

	/* send the command completion event to the host */
	event->evt_cmd_comp.ptr = mhi->cmd_ctx_cache->rbase
			+ (mhi->ring[MHI_RING_CMD_ID]->rd_offset *
			(sizeof(union mhi_dev_ring_element_type)));
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "evt cmd comp ptr :%lx\n",
			(size_t) event->evt_cmd_comp.ptr);
	event->evt_cmd_comp.type = MHI_DEV_RING_EL_CMD_COMPLETION_EVT;
	event->evt_cmd_comp.code = code;
	rc = mhi_dev_flush_cmd_completion_events(mhi, event);
	kfree(event);

	return rc;
}

static int mhi_dev_process_stop_cmd(struct mhi_dev_ring *ring, uint32_t ch_id,
							struct mhi_dev *mhi)
{
	struct mhi_addr data_transfer;

	if (ring->rd_offset != ring->wr_offset &&
		mhi->ch_ctx_cache[ch_id].ch_type ==
				MHI_DEV_CH_TYPE_OUTBOUND_CHANNEL) {
		mhi_log(mhi->vf_id, MHI_MSG_INFO, "Pending outbound transaction\n");
		return 0;
	} else if (mhi->ch_ctx_cache[ch_id].ch_type ==
			MHI_DEV_CH_TYPE_INBOUND_CHANNEL &&
			(mhi->ch[ch_id]->pend_wr_count > 0)) {
		mhi_log(mhi->vf_id, MHI_MSG_INFO, "Pending inbound transaction\n");
		return 0;
	}

	/* set the channel to stop */
	mhi->ch_ctx_cache[ch_id].ch_state = MHI_DEV_CH_STATE_STOP;
	mhi->ch[ch_id]->state = MHI_DEV_CH_STOPPED;

	if (MHI_USE_DMA(mhi)) {
		data_transfer.host_pa = mhi->ch_ctx_shadow.host_pa +
				sizeof(struct mhi_dev_ch_ctx) * ch_id;
	} else {
		data_transfer.device_va = mhi->ch_ctx_shadow.device_va +
				sizeof(struct mhi_dev_ch_ctx) * ch_id;
		data_transfer.device_pa = mhi->ch_ctx_shadow.device_pa +
				sizeof(struct mhi_dev_ch_ctx) * ch_id;
	}
	data_transfer.size = sizeof(enum mhi_dev_ch_ctx_state);
	data_transfer.virt_addr = &mhi->ch_ctx_cache[ch_id].ch_state;
	data_transfer.phy_addr = mhi->ch_ctx_cache_dma_handle +
			sizeof(struct mhi_dev_ch_ctx) * ch_id;

	/* update the channel state in the host */
	mhi->write_to_host(mhi, &data_transfer, NULL, MHI_DEV_DMA_SYNC);

	/* send the completion event to the host */
	return mhi_dev_send_cmd_comp_event(mhi,
					MHI_CMD_COMPL_CODE_SUCCESS);
}

static void mhi_dev_process_reset_cmd(struct mhi_dev *mhi, int ch_id)
{
	int rc = 0;
	struct mhi_dev_channel *ch;
	struct mhi_addr host_addr;
	struct event_req *itr, *tmp;

	rc = mhi_dev_mmio_disable_chdb_a7(mhi, ch_id);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
			"Failed to disable chdb for ch_id:%d\n", ch_id);
		rc = mhi_dev_send_cmd_comp_event(mhi,
				MHI_CMD_COMPL_CODE_UNDEFINED);
		if (rc)
			mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Error with compl event\n");
		return;
	}

	ch = mhi->ch[ch_id];
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "Processing reset cmd for ch_id:%d\n", ch_id);
	/*
	 * Ensure that the completions that are present in the flush list are
	 * removed from the list and added to event req list before channel
	 * reset. Otherwise, those stale events may get flushed along with a
	 * valid event in the next flush operation.
	 */
	if (!list_empty(&ch->flush_event_req_buffers)) {
		list_for_each_entry_safe(itr, tmp, &ch->flush_event_req_buffers, list) {
			list_del(&itr->list);
			list_add_tail(&itr->list, &ch->event_req_buffers);
		}
	}

	/* hard stop and set the channel to stop */
	mhi->ch_ctx_cache[ch_id].ch_state =
				MHI_DEV_CH_STATE_DISABLED;
	mhi->ch[ch_id]->state = MHI_DEV_CH_STOPPED;

	if (MHI_USE_DMA(mhi))
		host_addr.host_pa =
			mhi->ch_ctx_shadow.host_pa +
			(sizeof(struct mhi_dev_ch_ctx) * ch_id);
	else
		host_addr.device_va =
			mhi->ch_ctx_shadow.device_va +
			(sizeof(struct mhi_dev_ch_ctx) * ch_id);

	host_addr.virt_addr =
			&mhi->ch_ctx_cache[ch_id].ch_state;
	host_addr.size = sizeof(enum mhi_dev_ch_ctx_state);
	host_addr.phy_addr = mhi->ch_ctx_cache_dma_handle +
			sizeof(struct mhi_dev_ch_ctx) * ch_id;

	/* update the channel state in the host */
	mhi->write_to_host(mhi, &host_addr, NULL,
			MHI_DEV_DMA_SYNC);

	/* send the completion event to the host */
	rc = mhi_dev_send_cmd_comp_event(mhi,
				MHI_CMD_COMPL_CODE_SUCCESS);
	if (rc)
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Error sending command completion event\n");

	ch->reset_pending = false;
}

static int mhi_dev_process_cmd_ring(struct mhi_dev *mhi,
			union mhi_dev_ring_element_type *el, void *ctx)
{
	int rc = 0;
	uint32_t ch_id = 0, vf_id = mhi->vf_id;
	union mhi_dev_ring_element_type event;
	struct mhi_addr host_addr;
	struct mhi_dev_channel *ch;
	struct mhi_dev_ring *ring;
	union mhi_dev_ring_ctx *evt_ctx;

	ch_id = el->generic.chid;
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "for ch_id:%d and cmd %d\n",
		ch_id, el->generic.type);

	switch (el->generic.type) {
	case MHI_DEV_RING_EL_START:
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "received start cmd for ch_id:%d\n",
								ch_id);
		if (!mhi->ch[ch_id]) {
			if (mhi_dev_channel_init(mhi, ch_id))
				return -ENOMEM;
		}

		if (ch_id >= (mhi->mhi_chan_hw_base)) {
			rc = mhi_hwc_chcmd(mhi, ch_id, el->generic.type);
			if (rc) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"Error with HW ch cmd %d\n", rc);
				rc = mhi_dev_send_cmd_comp_event(mhi,
					MHI_CMD_COMPL_CODE_UNDEFINED);
				if (rc)
					mhi_log(mhi->vf_id, MHI_MSG_ERROR,
						"Error with compl event\n");
				return rc;
			}
			goto send_start_completion_event;
		} else {
			rc = mhi_dev_mmio_enable_chdb_a7(mhi, ch_id);
			if (rc) {
				mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
					"Failed to enable chdb for ch_id:%d\n",
						ch_id);
				goto send_undef_completion_event;
			}
		}

		/* fetch the channel context from host */
		mhi_dev_fetch_ch_ctx(mhi, ch_id);

		/* In flashless  boot scenario, certain channels like sahara may be
		 * started and reset during SBL state, and we may receive the
		 * command processing as the reset may not have been handled or
		 * RP may not have been updated. Check channel context to see
		 * if channel is in disabled state and ignore it
		 */
		if (mhi->is_flashless && check_ignore_ch_list(ch_id)) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Ignoring start cmd, ch:%d is in ignore list\n", ch_id);
			return 0;
		}

		/* Initialize and configure the corresponding channel ring */
		rc = mhi_ring_start(mhi->ring[mhi->ch_ring_start + ch_id],
			(union mhi_dev_ring_ctx *)&mhi->ch_ctx_cache[ch_id],
			mhi);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"start ring failed for ch_id:%d\n", ch_id);
			goto send_undef_completion_event;
		}

		mhi->ring[mhi->ch_ring_start + ch_id]->state =
						RING_STATE_PENDING;

		/* set the channel to running */
		mhi->ch_ctx_cache[ch_id].ch_state = MHI_DEV_CH_STATE_RUNNING;
		mhi->ch[ch_id]->state = MHI_DEV_CH_STARTED;
		mhi->ch[ch_id]->ch_id = ch_id;
		mhi->ch[ch_id]->ring = mhi->ring[mhi->ch_ring_start + ch_id];
		mhi->ch[ch_id]->ch_type = mhi->ch_ctx_cache[ch_id].ch_type;

		if (MHI_USE_DMA(mhi)) {
			uint32_t evnt_ring_idx = mhi->ev_ring_start +
					mhi->ch_ctx_cache[ch_id].err_indx;
			struct mhi_dev_ring *evt_ring =
				mhi->ring[evnt_ring_idx];
			evt_ctx = (union mhi_dev_ring_ctx *)&mhi->ev_ctx_cache
				[mhi->ch_ctx_cache[ch_id].err_indx];
			if (mhi_ring_get_state(evt_ring) == RING_STATE_UINT) {
				rc = mhi_ring_start(evt_ring, evt_ctx, mhi);
				if (rc) {
					mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"error starting event ring_id:%d\n",
					mhi->ch_ctx_cache[ch_id].err_indx);
					goto send_undef_completion_event;
				}
			}
		}

		if (MHI_USE_DMA(mhi))
			host_addr.host_pa = mhi->ch_ctx_shadow.host_pa +
					sizeof(struct mhi_dev_ch_ctx) * ch_id;
		else
			host_addr.device_va = mhi->ch_ctx_shadow.device_va +
					sizeof(struct mhi_dev_ch_ctx) * ch_id;

		host_addr.virt_addr = &mhi->ch_ctx_cache[ch_id].ch_state;
		host_addr.size = sizeof(enum mhi_dev_ch_ctx_state);
		host_addr.phy_addr = mhi->ch_ctx_cache_dma_handle +
				sizeof(struct mhi_dev_ch_ctx) * ch_id;

		mhi->write_to_host(mhi, &host_addr, NULL, MHI_DEV_DMA_SYNC);

send_start_completion_event:
		rc = mhi_dev_send_cmd_comp_event(mhi,
						MHI_CMD_COMPL_CODE_SUCCESS);
		if (rc)
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Error sending compl event for ch_id:%d\n",
				ch_id);

		mhi_update_state_info_ch(vf_id, ch_id, MHI_STATE_CONNECTED);
		/* Trigger callback to clients */
		mhi_dev_trigger_cb(vf_id, ch_id);
		mhi_uci_chan_state_notify(mhi, ch_id, MHI_STATE_CONNECTED);
		break;

send_undef_completion_event:
		mhi->ch_ctx_cache[ch_id].ch_state = MHI_DEV_CH_STATE_DISABLED;
		mhi->ch[ch_id]->state = MHI_DEV_CH_UNINT;

		rc = mhi_dev_send_cmd_comp_event(mhi,
				MHI_CMD_COMPL_CODE_UNDEFINED);
		if (rc)
			mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "Error with compl event\n");

		mhi_dev_mmio_disable_chdb_a7(mhi, ch_id);

		return rc;

	case MHI_DEV_RING_EL_STOP:
		if (ch_id >= (mhi->mhi_chan_hw_base)) {
			rc = mhi_hwc_chcmd(mhi, ch_id, el->generic.type);
			if (rc)
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"send ch stop cmd event failed for ch_id:%d\n",
					ch_id);

			/* send the completion event to the host */
			event.evt_cmd_comp.ptr = mhi->cmd_ctx_cache->rbase +
				(mhi->ring[MHI_RING_CMD_ID]->rd_offset *
				(sizeof(union mhi_dev_ring_element_type)));
			event.evt_cmd_comp.type =
					MHI_DEV_RING_EL_CMD_COMPLETION_EVT;
			if (rc == 0)
				event.evt_cmd_comp.code =
					MHI_CMD_COMPL_CODE_SUCCESS;
			else
				event.evt_cmd_comp.code =
					MHI_CMD_COMPL_CODE_UNDEFINED;

			rc = mhi_dev_flush_cmd_completion_events(mhi, &event);
			if (rc) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
						"stop event send failed\n");
				return rc;
			}
		} else {
			/*
			 * Check if there are any pending transactions for the
			 * ring associated with the channel. If no, proceed to
			 * write disable the channel state else send stop
			 * channel command to check if one can suspend the
			 * command.
			 */
			ring = mhi->ring[ch_id + mhi->ch_ring_start];
			if (ring->state == RING_STATE_UINT) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"Channel not opened for ch_id:%d\n",
					ch_id);
				return -EINVAL;
			}

			ch = mhi->ch[ch_id];

			mutex_lock(&ch->ch_lock);
			mutex_lock(&ch->ring->event_lock);

			mhi->ch[ch_id]->state = MHI_DEV_CH_PENDING_STOP;
			rc = mhi_dev_process_stop_cmd(
				mhi->ring[mhi->ch_ring_start + ch_id],
				ch_id, mhi);
			if (rc)
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
						"stop event send failed\n");

			mutex_unlock(&ch->ring->event_lock);
			mutex_unlock(&ch->ch_lock);
			mhi_update_state_info_ch(vf_id, ch_id, MHI_STATE_DISCONNECTED);
			/* Trigger callback to clients */
			mhi_dev_trigger_cb(vf_id, ch_id);
			mhi_uci_chan_state_notify(mhi, ch_id,
					MHI_STATE_DISCONNECTED);
		}
		break;
	case MHI_DEV_RING_EL_RESET:
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
			"received reset cmd for channel %d\n", ch_id);
		if (ch_id >= (mhi->mhi_chan_hw_base)) {
			rc = mhi_hwc_chcmd(mhi, ch_id, el->generic.type);
			if (rc)
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"send ch stop cmd event failed ch_id:%d\n",
					ch_id);

			/* send the completion event to the host */
			event.evt_cmd_comp.ptr = mhi->cmd_ctx_cache->rbase +
				(mhi->ring[MHI_RING_CMD_ID]->rd_offset *
				(sizeof(union mhi_dev_ring_element_type)));
			event.evt_cmd_comp.type =
					MHI_DEV_RING_EL_CMD_COMPLETION_EVT;
			if (rc == 0)
				event.evt_cmd_comp.code =
					MHI_CMD_COMPL_CODE_SUCCESS;
			else
				event.evt_cmd_comp.code =
					MHI_CMD_COMPL_CODE_UNDEFINED;

			rc = mhi_dev_flush_cmd_completion_events(mhi, &event);
			if (rc) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"stop event send failed for ch_id:%d\n",
					ch_id);
				return rc;
			}
		} else {

			mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
					"received reset cmd for channel %d\n",
					ch_id);
			/* In flashless  boot scenario, certain channels like sahara may be
			 * started and reset during SBL state, and we may receive the
			 * command processing as the reset may not have been handled or
			 * RP may not have been updated. Check channel context to see
			 * if channel is in disabled state and ignore it
			 */
			if (mhi->is_flashless && check_ignore_ch_list(ch_id)) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"Ignoring reset cmd, ch:%d is in ignore list\n", ch_id);
				return 0;
			}

			ring = mhi->ring[ch_id + mhi->ch_ring_start];
			if (ring->state == RING_STATE_UINT) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"Channel not opened for ch_id:%d\n",
					ch_id);
				return -EINVAL;
			}
			ch = mhi->ch[ch_id];
			mutex_lock(&ch->ch_lock);
			mutex_lock(&ch->ring->event_lock);
			if (ch->db_pending) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"skipping reset cmd ack for ch_id:%d\n",
						ch_id);
				ch->reset_pending = true;
				mutex_unlock(&ch->ring->event_lock);
				mutex_unlock(&ch->ch_lock);
				rc = -EBUSY;
				return rc;
			}
			mhi_dev_process_reset_cmd(mhi, ch_id);
			mutex_unlock(&ch->ring->event_lock);
			mutex_unlock(&ch->ch_lock);
			mhi_update_state_info_ch(vf_id, ch_id, MHI_STATE_DISCONNECTED);
			mhi_dev_trigger_cb(vf_id, ch_id);
			mhi_uci_chan_state_notify(mhi, ch_id,
					MHI_STATE_DISCONNECTED);
		}
		break;
	default:
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Invalid command:%d, ch_id:%d\n",
			el->generic.type, ch_id);
		break;
	}
	return rc;
}

static int mhi_dev_process_tre_ring(struct mhi_dev *mhi,
			union mhi_dev_ring_element_type *el, void *ctx)
{
	struct mhi_dev_ring *ring = (struct mhi_dev_ring *)ctx;
	struct mhi_dev_channel *ch;
	struct mhi_dev_client_cb_reason reason;

	if (ring->id < mhi->ch_ring_start) {
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
			"invalid channel ring_id:%d, should be < %lu\n",
			ring->id, mhi->ch_ring_start);
		return -EINVAL;
	}

	ch = mhi->ch[ring->id - mhi->ch_ring_start];
	reason.vf_id = mhi->vf_id;
	reason.ch_id = ch->ch_id;
	reason.reason = MHI_DEV_TRE_AVAILABLE;
	/*
	 * Save lowest value of tre_len to split packets in UCI layer
	 * for write request of size more than tre_len.
	 */
	if (!ch->tre_size || ch->tre_size > el->tre.len)
		ch->tre_size = el->tre.len;

	/* Invoke a callback to let the client know its data is ready.
	 * Copy this event to the clients context so that it can be
	 * sent out once the client has fetch the data. Update the rp
	 * before sending the data as part of the event completion
	 */
	if (ch->active_client && ch->active_client->event_trigger != NULL)
		ch->active_client->event_trigger(&reason);
	return 0;
}

static void mhi_dev_process_ring_pending(struct work_struct *work)
{
	struct mhi_dev *mhi = container_of(work,
				struct mhi_dev, pending_work);
	struct list_head *cp, *q;
	struct mhi_dev_ring *ring;
	struct mhi_dev_channel *ch;
	int rc = 0, ch_id;

	mutex_lock(&mhi->mhi_lock);

	rc = mhi_dev_process_ring(mhi->ring[mhi->cmd_ring_idx]);
	if (rc && rc != -EBUSY) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "error processing command ring\n");
		goto exit;
	}

	list_for_each_safe(cp, q, &mhi->process_ring_list) {
		ring = list_entry(cp, struct mhi_dev_ring, list);
		list_del(cp);
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "processing ring_id:%d\n", ring->id);

		if (ring->id < mhi->ch_ring_start) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"ring_id:%d is not a channel ring\n", ring->id);
			goto exit;
		}

		ch = mhi->ch[ring->id - mhi->ch_ring_start];

		rc = mhi_dev_process_ring(ring);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"error processing ring_id:%d\n", ring->id);
			goto exit;
		}
		mutex_lock(&ch->ch_lock);
		ch->db_pending = false;
		mutex_unlock(&ch->ch_lock);

		if (ch->reset_pending) {
			/*
			 * The channel might be reset asynchronously by the
			 * host, below reset ack is in  case the channel
			 * was stopped/reset with pending DB.
			 */
			ch_id = ch->ch_id;
			mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"processing pending ch_id:%d reset\n", ch_id);
			rc = mhi_dev_process_ring(
				mhi->ring[mhi->cmd_ring_idx]);
			if (rc) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"error processing command ring\n");
				goto exit;
			}
		}

		rc = mhi_dev_mmio_enable_chdb_a7(mhi, ch->ch_id);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"error enabling chdb interrupt for %d\n", ch->ch_id);
			goto exit;
		}
	}

exit:
	mutex_unlock(&mhi->mhi_lock);
}

static int mhi_dev_get_event_notify(enum mhi_dev_state state,
						enum mhi_dev_event *event)
{
	int rc = 0;

	switch (state) {
	case MHI_DEV_M0_STATE:
		*event = MHI_DEV_EVENT_M0_STATE;
		break;
	case MHI_DEV_M1_STATE:
		*event = MHI_DEV_EVENT_M1_STATE;
		break;
	case MHI_DEV_M2_STATE:
		*event = MHI_DEV_EVENT_M2_STATE;
		break;
	case MHI_DEV_M3_STATE:
		*event = MHI_DEV_EVENT_M3_STATE;
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static bool mhi_dev_queue_channel_db(struct mhi_dev *mhi,
					uint32_t chintr_value, uint32_t ch_num)
{
	struct mhi_dev_ring *ring;
	struct mhi_dev_channel *ch;
	bool work_pending = false;
	int rc = 0;

	for (; chintr_value; ch_num++, chintr_value >>= 1) {
		if (chintr_value & 1) {
			ring = mhi->ring[ch_num + mhi->ch_ring_start];
			if (ring->state == RING_STATE_UINT) {
				pr_debug("Channel not opened for ch_id:%d\n",
						ch_num);
				continue;
			}
			mhi_ring_set_state(ring, RING_STATE_PENDING);
			list_add(&ring->list, &mhi->process_ring_list);
			ch = mhi->ch[ch_num];
			mutex_lock(&ch->ch_lock);
			ch->db_pending = true;
			work_pending = true;
			mutex_unlock(&ch->ch_lock);
			rc = mhi_dev_mmio_disable_chdb_a7(mhi, ch_num);
			if (rc) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"Error disabling chdb\n");
				return work_pending;
			}
		}
	}
	return work_pending;
}

/*
 * mhi_dev_check_channel_interrupt () - function called
 * to check if CH DB interrupts are present to process.
 *
 * Return : true if valid interrupts are present
 * to process, false if not.
 */

static bool mhi_dev_check_channel_interrupt(struct mhi_dev *mhi)
{
	int i, rc = 0;
	bool pending_work = false;
	uint32_t chintr_value = 0, ch_num = 0;

	rc = mhi_dev_mmio_read_chdb_status_interrupts(mhi);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Error while reading CH DB\n");
		return pending_work;
	}

	for (i = 0; i < MHI_MASK_ROWS_CH_EV_DB; i++) {
		ch_num = i * MHI_MASK_CH_EV_LEN;
		/* Process channel status whose mask is enabled */
		chintr_value = (mhi->chdb[i].status & mhi->chdb[i].mask);

		/* Device Wake doorbell handling */
		if ((i == (MHI_MASK_ROWS_CH_EV_DB-1)) && (chintr_value & (1 << 31))
				&& (mhi->enable_m2)) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Wake doorbell received\n");
			rc = mhi_dev_process_wake_db(mhi);
			if (rc) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"Error processing device wake doorbell with rc %d\n", rc);
				return pending_work;
			}
		} else if (chintr_value) {
			mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"processing id: %d, ch interrupt 0x%x\n",
							i, chintr_value);
			pending_work |= mhi_dev_queue_channel_db(mhi,
							chintr_value, ch_num);
		}

		rc = mhi_dev_mmio_write(mhi, MHI_CHDB_INT_CLEAR_A7_n(i), chintr_value);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"Error writing interrupt clear for A7\n");
			return pending_work;
		}
	}
	return pending_work;
}

static void mhi_update_state_info_all(struct mhi_dev *mhi_ctx, enum mhi_ctrl_info info)
{
	int ch_id;
	u32 vf_id = mhi_ctx->vf_id;
	struct mhi_dev_client_cb_reason reason;

	mhi_ctx->ctrl_info = info;
	for (ch_id = 0; ch_id < MHI_MAX_SOFTWARE_CHANNELS; ++ch_id) {
		/*
		 * Skip channel state info change
		 * if channel is already in the desired state.
		 */
		if (channel_state_info[vf_id][ch_id].ctrl_info == info ||
		    (info == MHI_STATE_DISCONNECTED &&
		    channel_state_info[vf_id][ch_id].ctrl_info == MHI_STATE_CONFIGURED))
			continue;
		channel_state_info[vf_id][ch_id].ctrl_info = info;
		/* Notify kernel clients */
		mhi_dev_trigger_cb(vf_id, ch_id);
	}

	/* For legacy reasons for QTI client */
	reason.reason = MHI_DEV_CTRL_UPDATE;
	uci_ctrl_update(&reason);
}

static int mhi_dev_abort(struct mhi_dev *mhi)
{
	struct mhi_dev_channel *ch;
	struct mhi_dev_ring *ring;
	int ch_id = 0, rc = 0;

	/* Hard stop all the channels */
	for (ch_id = 0; ch_id < mhi->cfg.channels; ch_id++) {
		ring = mhi->ring[ch_id + mhi->ch_ring_start];
		if (!ring || ring->state == RING_STATE_UINT)
			continue;
		ch = mhi->ch[ch_id];
		mutex_lock(&ch->ch_lock);
		mhi->ch[ch_id]->state = MHI_DEV_CH_STOPPED;
		mutex_unlock(&ch->ch_lock);
	}

	/* Update channel state and notify clients */
	mhi_update_state_info_all(mhi, MHI_STATE_DISCONNECTED);
	mhi_uci_chan_state_notify_all(mhi, MHI_STATE_DISCONNECTED);

	flush_workqueue(mhi->ring_init_wq);
	flush_workqueue(mhi->pending_ring_wq);

	/* Clean up initialized channels */
	rc = mhi_deinit(mhi);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Error during mhi_deinit with %d\n", rc);
		return rc;
	}

	rc = mhi_dev_mmio_mask_chdb_interrupts(mhi);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Failed to enable ch db\n");
		return rc;
	}

	rc = mhi_dev_mmio_disable_ctrl_interrupt(mhi);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Failed to enable control interrupt\n");
		return rc;
	}

	rc = mhi_dev_mmio_disable_cmdb_interrupt(mhi);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Failed to enable command db\n");
		return rc;
	}


	atomic_set(&mhi->re_init_done, 0);

	mhi_log(mhi->vf_id, MHI_MSG_INFO,
			"Register a PCIe callback during re-init\n");
	mhi->mhi_hw_ctx->event_reg.events = EP_PCIE_EVENT_LINKUP | EP_PCIE_EVENT_LINKUP_VF;
	mhi->mhi_hw_ctx->event_reg.user = mhi->mhi_hw_ctx;
	mhi->mhi_hw_ctx->event_reg.mode = EP_PCIE_TRIGGER_CALLBACK;
	mhi->mhi_hw_ctx->event_reg.callback = mhi_dev_resume_init_with_link_up;
	mhi->mhi_hw_ctx->event_reg.options = MHI_REINIT;

	rc = ep_pcie_register_event(mhi->mhi_hw_ctx->phandle,
					&mhi->mhi_hw_ctx->event_reg);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Failed to register for events from PCIe\n");
		return rc;
	}

	/* Set RESET field to 0 */
	mhi_dev_mmio_reset(mhi);

	return rc;
}

static void mhi_dev_transfer_completion_cb(void *mreq)
{
	int rc = 0;
	struct mhi_req *req = mreq;
	struct mhi_dev_channel *ch;
	bool snd_cmpl = req->snd_cmpl;
	bool inbound = false;
	struct mhi_dev *mhi_ctx;

	ch = req->client->channel;
	mhi_ctx = ch->ring->mhi_dev;

	unmap_single(mhi_ctx, req->dma, req->transfer_len, DMA_FROM_DEVICE);

	if (mhi_ctx->ch_ctx_cache[ch->ch_id].ch_type ==
		MHI_DEV_CH_TYPE_INBOUND_CHANNEL) {
		inbound = true;
		ch->pend_wr_count--;
	}

	/*
	 * Channel got closed with transfers pending
	 * Do not trigger callback or send cmpl to host
	 */
	if (ch->state == MHI_DEV_CH_CLOSED ||
		ch->state == MHI_DEV_CH_STOPPED) {
		if (inbound)
			mhi_log(mhi_ctx->vf_id, MHI_MSG_DBG,
			"ch_id:%d closed with %d writes pending\n",
			ch->ch_id, ch->pend_wr_count + 1);
		else
			mhi_log(mhi_ctx->vf_id, MHI_MSG_DBG,
			"ch_id:%d closed with read pending\n", ch->ch_id);
		return;
	}

	/* Trigger client call back */
	req->client_cb(req);

	/* Flush read completions to host */
	if (snd_cmpl && mhi_ctx->ch_ctx_cache[ch->ch_id].ch_type ==
				MHI_DEV_CH_TYPE_OUTBOUND_CHANNEL) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_DBG, "Calling flush for ch_id:%d\n", ch->ch_id);
		mutex_lock(&ch->ch_lock);
		rc = mhi_dev_flush_transfer_completion_events(mhi_ctx, ch, snd_cmpl);
		mutex_unlock(&ch->ch_lock);
		if (rc) {
			mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
				"Failed to flush read completions to host\n");
		}
	}

	if (ch->state == MHI_DEV_CH_PENDING_STOP) {
		ch->state = MHI_DEV_CH_STOPPED;
		rc = mhi_dev_process_stop_cmd(ch->ring, ch->ch_id, mhi_ctx);
		if (rc)
			mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
			"Error while stopping ch_id:%d\n", ch->ch_id);
	}
}

static void mhi_dev_scheduler(struct work_struct *work)
{
	struct mhi_dev *mhi = container_of(work,
				struct mhi_dev, chdb_ctrl_work);
	int rc = 0;
	bool work_pending = false;
	uint32_t int_value = 0;
	struct mhi_dev_ring *ring;
	enum mhi_dev_state state;
	enum mhi_dev_event event = 0;
	u32 mhi_reset;

	mutex_lock(&mhi->mhi_lock);
	/* Check for interrupts */
	mhi_dev_core_ack_ctrl_interrupts(mhi, &int_value);

	if (int_value & MHI_MMIO_CTRL_INT_STATUS_A7_MSK) {
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
			"processing ctrl interrupt with %d\n", int_value);

		rc = mhi_dev_mmio_read(mhi, BHI_IMGTXDB, &bhi_imgtxdb);
		mhi_log(mhi->vf_id, MHI_MSG_DBG, "BHI_IMGTXDB = 0x%x\n", bhi_imgtxdb);

		rc = mhi_dev_mmio_get_mhi_state(mhi, &state, &mhi_reset);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "get mhi state failed\n");
			mutex_unlock(&mhi->mhi_lock);
			return;
		}

		if (mhi_reset) {
			mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"processing mhi device reset\n");
			rc = mhi_dev_abort(mhi);
			if (rc)
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"device reset failed:%d\n", rc);
			mutex_unlock(&mhi->mhi_lock);
			queue_work(mhi->ring_init_wq, &mhi->re_init);
			return;
		}

		rc = mhi_dev_get_event_notify(state, &event);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"unsupported state :%d\n", state);
			goto fail;
		}

		rc = mhi_dev_notify_sm_event(mhi, event);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "error sending SM event\n");
			goto fail;
		}
	}

	if (int_value & MHI_MMIO_CTRL_CRDB_STATUS_MSK) {
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
			"processing cmd db interrupt with %d\n", int_value);
		ring = mhi->ring[MHI_RING_CMD_ID];
		ring->state = RING_STATE_PENDING;
		work_pending = true;
	}

	/* get the specific channel interrupts */
	work_pending |= mhi_dev_check_channel_interrupt(mhi);
	if (work_pending)
		queue_work(mhi->pending_ring_wq, &mhi->pending_work);

fail:
	mutex_unlock(&mhi->mhi_lock);

	if (mhi->config_iatu || mhi->mhi_int)
		enable_irq(mhi->mhi_irq);
	else
		ep_pcie_mask_irq_event(mhi->mhi_hw_ctx->phandle,
				EP_PCIE_INT_EVT_MHI_A7, true);
}

void mhi_dev_notify_a7_event(struct mhi_dev *mhi)
{

	if (!atomic_read(&mhi->mhi_dev_wake)) {
		pm_stay_awake(mhi->mhi_hw_ctx->dev);
		atomic_set(&mhi->mhi_dev_wake, 1);
	}
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "acquiring mhi wakelock\n");

	schedule_work(&mhi->chdb_ctrl_work);
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "mhi irq triggered\n");
}
EXPORT_SYMBOL_GPL(mhi_dev_notify_a7_event);

static irqreturn_t mhi_dev_isr(int irq, void *dev_id)
{
	struct mhi_dev *mhi = dev_id;

	if (!atomic_read(&mhi->mhi_dev_wake)) {
		pm_stay_awake(mhi->mhi_hw_ctx->dev);
		atomic_set(&mhi->mhi_dev_wake, 1);
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "acquiring mhi wakelock in ISR\n");
	}

	disable_irq_nosync(mhi->mhi_irq);
	schedule_work(&mhi->chdb_ctrl_work);
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "mhi irq triggered\n");

	return IRQ_HANDLED;
}

int mhi_dev_config_outbound_iatu(struct mhi_dev *mhi)
{
	struct ep_pcie_iatu control, data;
	struct ep_pcie_iatu entries[MHI_HOST_REGION_NUM];

	data.start = mhi->data_base.device_pa;
	data.end = mhi->data_base.device_pa + mhi->data_base.size - 1;
	data.tgt_lower = HOST_ADDR_LSB(mhi->data_base.host_pa);
	data.tgt_upper = HOST_ADDR_MSB(mhi->data_base.host_pa);

	control.start = mhi->ctrl_base.device_pa;
	control.end = mhi->ctrl_base.device_pa + mhi->ctrl_base.size - 1;
	control.tgt_lower = HOST_ADDR_LSB(mhi->ctrl_base.host_pa);
	control.tgt_upper = HOST_ADDR_MSB(mhi->ctrl_base.host_pa);

	entries[0] = data;
	entries[1] = control;

	return ep_pcie_config_outbound_iatu(mhi->mhi_hw_ctx->phandle, entries,
					MHI_HOST_REGION_NUM, mhi->vf_id);
}
EXPORT_SYMBOL_GPL(mhi_dev_config_outbound_iatu);

static int mhi_dev_cache_host_cfg(struct mhi_dev *mhi)
{
	int rc = 0, i = 0, cmd_ring_size = 0, ev_ring_size = 0;
	struct platform_device *pdev;
	uint64_t addr1 = 0;
	struct mhi_addr data_transfer;

	pdev = mhi->mhi_hw_ctx->pdev;

	/* Get host memory region configuration */
	mhi_dev_get_mhi_addr(mhi);

	mhi->ctrl_base.host_pa  = HOST_ADDR(mhi->host_addr.ctrl_base_lsb,
						mhi->host_addr.ctrl_base_msb);
	mhi->data_base.host_pa  = HOST_ADDR(mhi->host_addr.data_base_lsb,
						mhi->host_addr.data_base_msb);

	addr1 = HOST_ADDR(mhi->host_addr.ctrl_limit_lsb,
					mhi->host_addr.ctrl_limit_msb);
	mhi->ctrl_base.size = addr1 - mhi->ctrl_base.host_pa;
	addr1 = HOST_ADDR(mhi->host_addr.data_limit_lsb,
					mhi->host_addr.data_limit_msb);
	mhi->data_base.size = addr1 - mhi->data_base.host_pa;

	if (mhi->config_iatu) {
		if (mhi->ctrl_base.host_pa > mhi->data_base.host_pa) {
			mhi->data_base.device_pa = mhi->device_local_pa_base;
			mhi->ctrl_base.device_pa = mhi->device_local_pa_base +
				mhi->ctrl_base.host_pa - mhi->data_base.host_pa;
		} else {
			mhi->ctrl_base.device_pa = mhi->device_local_pa_base;
			mhi->data_base.device_pa = mhi->device_local_pa_base +
				mhi->data_base.host_pa - mhi->ctrl_base.host_pa;
		}

		if (!MHI_USE_DMA(mhi)) {
			mhi->ctrl_base.device_va =
				(uintptr_t) devm_ioremap(&pdev->dev,
				mhi->ctrl_base.device_pa,
				mhi->ctrl_base.size);
			if (!mhi->ctrl_base.device_va) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"io remap failed for mhi address\n");
				return -EINVAL;
			}
		}
	}

	if (mhi->config_iatu) {
		rc = mhi_dev_config_outbound_iatu(mhi);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Configuring iATU failed\n");
			return rc;
		}
	}

	/* Get Channel, event and command context base pointer */
	rc = mhi_dev_mmio_get_chc_base(mhi);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Fetching ch context failed\n");
		return rc;
	}

	rc = mhi_dev_mmio_get_erc_base(mhi);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Fetching event ring context failed\n");
		return rc;
	}

	rc = mhi_dev_mmio_get_crc_base(mhi);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Fetching command ring context failed\n");
		return rc;
	}

	rc = mhi_dev_update_ner(mhi);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Fetching NER failed\n");
		return rc;
	}

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
		"Number of Event rings : %d, HW Event rings : %d\n",
			mhi->cfg.event_rings, mhi->cfg.hw_event_rings);

	mhi->cmd_ctx_shadow.size = sizeof(struct mhi_dev_cmd_ctx);
	mhi->ev_ctx_shadow.size = sizeof(struct mhi_dev_ev_ctx) *
					mhi->cfg.event_rings;
	mhi->ch_ctx_shadow.size = sizeof(struct mhi_dev_ch_ctx) *
					mhi->cfg.channels;

	/*
	 * Allocate memory for an array of ring pointers, during
	 * M0 when host would have updated MHICFG register.
	 */

	if (!mhi->ring) {
		mhi->ring = devm_kcalloc(&pdev->dev,
				(mhi->cfg.channels + mhi->cfg.event_rings+1),
				sizeof(struct mhi_dev_ring *),
				GFP_KERNEL);
		if (!mhi->ring) {
			rc = -ENOMEM;
			goto exit;
		}

		/* Allocate memory for command ring as it is needed by default. */
		if (!mhi->ring[0])
			mhi->ring[0] = devm_kzalloc(&pdev->dev, sizeof(struct mhi_dev_ring),
						GFP_KERNEL);
		if (!mhi->ring[0]) {
			rc = -ENOMEM;
			goto exit;
		}
		cmd_ring_size = sizeof(*(mhi->ring[0]));

		/*
		 * Allocate memory for event rings as it is the host
		 * that decides which event ring will be used.
		 */
		for (i = 1 ; i < mhi->cfg.event_rings+1; i++) {
			if (!mhi->ring[i])
				mhi->ring[i] = devm_kzalloc(&pdev->dev, sizeof(struct mhi_dev_ring),
							GFP_KERNEL);
			if (!mhi->ring[i]) {
				rc = -ENOMEM;
				goto exit;
			}
			ev_ring_size += sizeof(*(mhi->ring[i]));
		}
		mhi_log(mhi->vf_id, MHI_MSG_INFO,
			"MEM_ALLOC: size:%lu RING_ALLOC\n",
			(sizeof(struct mhi_dev_ring *) *
			(mhi->cfg.channels + mhi->cfg.event_rings + 1))+ev_ring_size+cmd_ring_size);
	}
	/*
	 * This func mhi_dev_cache_host_cfg will be called when
	 * processing mhi device reset as well, do not allocate
	 * the command, event and channel context caches if they
	 * were already allocated during device boot, to avoid
	 * memory leak.
	 */
	if (!mhi->cmd_ctx_cache) {
		mhi->cmd_ctx_cache = alloc_coherent(mhi, sizeof(struct mhi_dev_cmd_ctx),
				&mhi->cmd_ctx_cache_dma_handle, GFP_KERNEL);
		if (!mhi->cmd_ctx_cache) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"no memory while allocating cmd ctx\n");
			rc = -ENOMEM;
			goto exit;
		}
	}

	if (!mhi->ev_ctx_cache) {
		mhi->ev_ctx_cache = alloc_coherent(mhi,
				sizeof(struct mhi_dev_ev_ctx) *
				mhi->cfg.event_rings,
				&mhi->ev_ctx_cache_dma_handle,
				GFP_KERNEL);
		if (!mhi->ev_ctx_cache) {
			rc = -ENOMEM;
			goto exit;
		}
	}
	memset(mhi->ev_ctx_cache, 0, sizeof(struct mhi_dev_ev_ctx) *
						mhi->cfg.event_rings);

	if (!mhi->ch_ctx_cache) {
		mhi->ch_ctx_cache = alloc_coherent(mhi,
				sizeof(struct mhi_dev_ch_ctx) *
				mhi->cfg.channels,
				&mhi->ch_ctx_cache_dma_handle,
				GFP_KERNEL);
		if (!mhi->ch_ctx_cache) {
			rc = -ENOMEM;
			goto exit;
		}
	}
	memset(mhi->ch_ctx_cache, 0, sizeof(struct mhi_dev_ch_ctx) *
						mhi->cfg.channels);

	rc = mhi_dev_ring_init(mhi);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "MHI dev ring init failed\n");
		goto exit;
	}

	if (MHI_USE_DMA(mhi)) {
		data_transfer.phy_addr = mhi->cmd_ctx_cache_dma_handle;
		data_transfer.host_pa = mhi->cmd_ctx_shadow.host_pa;
	}

	data_transfer.size = mhi->cmd_ctx_shadow.size;

	/* Cache the command and event context */
	mhi->read_from_host(mhi, &data_transfer);

	if (MHI_USE_DMA(mhi)) {
		data_transfer.phy_addr = mhi->ev_ctx_cache_dma_handle;
		data_transfer.host_pa = mhi->ev_ctx_shadow.host_pa;
	}

	data_transfer.size = mhi->ev_ctx_shadow.size;

	mhi->read_from_host(mhi, &data_transfer);

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
			"cmd ring_base:0x%llx, rp:0x%llx, wp:0x%llx\n",
					mhi->cmd_ctx_cache->rbase,
					mhi->cmd_ctx_cache->rp,
					mhi->cmd_ctx_cache->wp);
	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
			"ev ring_base:0x%llx, rp:0x%llx, wp:0x%llx\n",
					mhi->ev_ctx_cache->rbase,
					mhi->ev_ctx_cache->rp,
					mhi->ev_ctx_cache->wp);

	rc = mhi_ring_start(mhi->ring[0],
			(union mhi_dev_ring_ctx *)mhi->cmd_ctx_cache, mhi);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "MHI ring start failed:%d\n", rc);
		goto exit;
	}

	return 0;
exit:
	if (mhi->ring) {
		mhi->ring = NULL;
		mhi_log(mhi->vf_id, MHI_MSG_INFO,
			"MEM_DEALLOC: size:%lu RING_ALLOC\n",
			(sizeof(struct mhi_dev_ring) *
			(mhi->cfg.channels + mhi->cfg.event_rings + 1)));
	}
	if (mhi->cmd_ctx_cache) {
		free_coherent(mhi,
			sizeof(struct mhi_dev_cmd_ctx),
			mhi->cmd_ctx_cache,
			mhi->cmd_ctx_cache_dma_handle);
		mhi_log(mhi->vf_id, MHI_MSG_INFO,
			"MEM_DEALLOC: size:%lu CMD_CTX_CACHE\n",
			sizeof(struct mhi_dev_cmd_ctx));
	}
	if (mhi->ev_ctx_cache) {
		free_coherent(mhi,
			sizeof(struct mhi_dev_ev_ctx) *
			mhi->cfg.event_rings,
			mhi->ev_ctx_cache,
			mhi->ev_ctx_cache_dma_handle);
		mhi_log(mhi->vf_id, MHI_MSG_INFO,
			"MEM_DEALLOC: size:%lu EV_CTX_CACHE\n",
			sizeof(struct mhi_dev_ev_ctx) *
			mhi->cfg.event_rings);
	}
	return rc;
}

void mhi_dev_pm_relax(struct mhi_dev *mhi_ctx)
{
	atomic_set(&mhi_ctx->mhi_dev_wake, 0);
	pm_relax(mhi_ctx->mhi_hw_ctx->dev);
	mhi_log(mhi_ctx->vf_id, MHI_MSG_VERBOSE, "releasing mhi wakelock\n");
}
EXPORT_SYMBOL_GPL(mhi_dev_pm_relax);

int mhi_channel_error_notif(struct mhi_dev *mhi)
{
	union mhi_dev_ring_element_type event;

	event.evt_ee_state.type = MHI_DEV_RING_EL_CH_STATE_CHANGE_NOTIFY;
	return mhi_dev_flush_cmd_completion_events(mhi, &event);
}
EXPORT_SYMBOL_GPL(mhi_channel_error_notif);

int mhi_dev_suspend(struct mhi_dev *mhi)
{
	int ch_id = 0;
	struct mhi_addr data_transfer;

	mutex_lock(&mhi->mhi_write_test);
	atomic_set(&mhi->is_suspended, 1);

	for (ch_id = 0; ch_id < mhi->cfg.channels; ch_id++) {
		if (mhi->ch_ctx_cache[ch_id].ch_state !=
						MHI_DEV_CH_STATE_RUNNING)
			continue;

		mhi->ch_ctx_cache[ch_id].ch_state = MHI_DEV_CH_STATE_SUSPENDED;

		if (MHI_USE_DMA(mhi)) {
			data_transfer.host_pa = mhi->ch_ctx_shadow.host_pa +
				sizeof(struct mhi_dev_ch_ctx) * ch_id;
		} else {
			data_transfer.device_va = mhi->ch_ctx_shadow.device_va +
				sizeof(struct mhi_dev_ch_ctx) * ch_id;
			data_transfer.device_pa = mhi->ch_ctx_shadow.device_pa +
				sizeof(struct mhi_dev_ch_ctx) * ch_id;
		}

		data_transfer.size = sizeof(enum mhi_dev_ch_ctx_state);
		data_transfer.virt_addr = &mhi->ch_ctx_cache[ch_id].ch_state;
		data_transfer.phy_addr = mhi->ch_ctx_cache_dma_handle +
				sizeof(struct mhi_dev_ch_ctx) * ch_id;

		/* update the channel state in the host */
		mhi->write_to_host(mhi, &data_transfer, NULL,
				MHI_DEV_DMA_SYNC);

	}

	mutex_unlock(&mhi->mhi_write_test);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_suspend);

int mhi_dev_resume(struct mhi_dev *mhi)
{
	int ch_id = 0;
	struct mhi_addr data_transfer;

	for (ch_id = 0; ch_id < mhi->cfg.channels; ch_id++) {
		if (mhi->ch_ctx_cache[ch_id].ch_state !=
				MHI_DEV_CH_STATE_SUSPENDED)
			continue;

		mhi->ch_ctx_cache[ch_id].ch_state = MHI_DEV_CH_STATE_RUNNING;
		if (MHI_USE_DMA(mhi)) {
			data_transfer.host_pa = mhi->ch_ctx_shadow.host_pa +
				sizeof(struct mhi_dev_ch_ctx) * ch_id;
		} else {
			data_transfer.device_va = mhi->ch_ctx_shadow.device_va +
				sizeof(struct mhi_dev_ch_ctx) * ch_id;
			data_transfer.device_pa = mhi->ch_ctx_shadow.device_pa +
				sizeof(struct mhi_dev_ch_ctx) * ch_id;
		}

		data_transfer.size = sizeof(enum mhi_dev_ch_ctx_state);
		data_transfer.virt_addr = &mhi->ch_ctx_cache[ch_id].ch_state;
		data_transfer.phy_addr = mhi->ch_ctx_cache_dma_handle +
				sizeof(struct mhi_dev_ch_ctx) * ch_id;

		/* update the channel state in the host */
		mhi->write_to_host(mhi, &data_transfer, NULL,
				MHI_DEV_DMA_SYNC);
	}
	mhi_update_state_info(mhi, MHI_STATE_CONNECTED);

	atomic_set(&mhi->is_suspended, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_resume);

static int mhi_dev_ring_init(struct mhi_dev *dev)
{
	int i = 0;

	mhi_log(dev->vf_id, MHI_MSG_INFO, "initializing all rings");
	dev->cmd_ring_idx = 0;
	dev->ev_ring_start = 1;
	dev->ch_ring_start = dev->ev_ring_start + dev->cfg.event_rings;

	/* Initialize CMD ring */
	mhi_ring_init(dev->ring[dev->cmd_ring_idx],
				RING_TYPE_CMD, dev->cmd_ring_idx);

	mhi_ring_set_cb(dev->ring[dev->cmd_ring_idx],
				mhi_dev_process_cmd_ring);

	/* Initialize Event ring */
	for (i = dev->ev_ring_start; i < (dev->cfg.event_rings
					+ dev->ev_ring_start); i++)
		mhi_ring_init(dev->ring[i], RING_TYPE_ER, i);

	return 0;
}

static uint32_t mhi_dev_get_evt_ring_size(struct mhi_dev *mhi, uint32_t ch_id)
{
	uint32_t info;
	int rc;

	/* If channel was started by host, get event ring size */
	rc = mhi_vf_ctrl_state_info(mhi->vf_id, ch_id, &info);
	if (rc || (info != MHI_STATE_CONNECTED))
		return NUM_TR_EVENTS_DEFAULT;

	return mhi->ring[mhi->ev_ring_start +
		mhi->ch_ctx_cache[ch_id].err_indx]->ring_size;
}

static int mhi_dev_alloc_cmd_ack_buf_req(struct mhi_dev *mhi)
{
	int rc = 0;
	uint32_t i;
	struct mhi_cmd_cmpl_ctx *cmd_ctx;
	union mhi_dev_ring_element_type *cmd_events;

	mhi->cmd_ctx = kmalloc(sizeof(struct mhi_cmd_cmpl_ctx),
					GFP_KERNEL);
	if (!mhi->cmd_ctx)
		return -ENOMEM;

	cmd_ctx = mhi->cmd_ctx;
	/* Allocate event requests */
	cmd_ctx->ereqs = kcalloc(NUM_CMD_EVENTS_DEFAULT,
						sizeof(*cmd_ctx->ereqs),
						GFP_KERNEL);
	if (!cmd_ctx->ereqs) {
		rc = -ENOMEM;
		goto free_ereqs;
	}

	/* Allocate buffers to queue transfer completion events */
	cmd_ctx->cmd_events = kcalloc(NUM_CMD_EVENTS_DEFAULT,
							sizeof(*cmd_events),
							GFP_KERNEL);
	if (!cmd_ctx->cmd_events) {
		rc = -ENOMEM;
		goto free_ereqs;
	}

	/* Organize event flush requests into a linked list */
	INIT_LIST_HEAD(&cmd_ctx->cmd_req_buffers);

	for (i = 0; i < NUM_CMD_EVENTS_DEFAULT; ++i) {
		list_add_tail(&cmd_ctx->ereqs[i].list,
					&cmd_ctx->cmd_req_buffers);
	}

	/*
	 * Initialize cmpl event buffer indexes - cmd_buf_rp and
	 * cmd_buf_wp point to the first and last free index available.
	 */
	cmd_ctx->cmd_buf_rp = 0;
	cmd_ctx->cmd_buf_wp = NUM_CMD_EVENTS_DEFAULT - 1;

	return 0;
free_ereqs:
		kfree(cmd_ctx->ereqs);
		cmd_ctx->ereqs = NULL;

		kfree(mhi->cmd_ctx);
		mhi_log(mhi->vf_id, MHI_MSG_INFO,
				"MEM_DEALLOC: size:%lu CMD_CTX\n",
				sizeof(struct mhi_cmd_cmpl_ctx));
		mhi->cmd_ctx = NULL;
		return rc;
}

static int mhi_dev_alloc_evt_buf_evt_req(struct mhi_dev *mhi,
		struct mhi_dev_channel *ch, struct mhi_dev_ring *evt_ring)
{
	int rc;
	uint32_t size, i;
	struct event_req *req, *tmp;

	size = mhi_dev_get_evt_ring_size(mhi, ch->ch_id);

	if (!size) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Evt buf size is 0 for ch_id:%d", ch->ch_id);
		return -EINVAL;
	}

	/* Previous allocated evt buf size matches requested size */
	if (size == ch->evt_buf_size)
		return 0;

	/*
	 * Either evt buf and evt reqs were not allocated yet or
	 * they were allocated with a different size
	 */
	if (ch->evt_buf_size) {
		list_for_each_entry_safe(req, tmp, &ch->event_req_buffers, list) {
			list_del(&req->list);
			kfree(req);
		}
		list_for_each_entry_safe(req, tmp, &ch->flush_event_req_buffers, list) {
			list_del(&req->list);
			kfree(req);
		}
		kfree(ch->tr_events);
	}
	/*
	 * Set number of event flush req buffers equal to size of
	 * event buf since in the worst case we may need to flush
	 * every event ring element individually
	 */
	ch->evt_buf_size = size;
	ch->evt_req_size = size;

	mhi_log(mhi->vf_id, MHI_MSG_INFO,
		"ch_id:%d evt buf size is %d\n", ch->ch_id, ch->evt_buf_size);

	INIT_LIST_HEAD(&ch->event_req_buffers);
	INIT_LIST_HEAD(&ch->flush_event_req_buffers);

	/* Allocate buffers to queue transfer completion events */
	ch->tr_events = kcalloc(ch->evt_buf_size, sizeof(*ch->tr_events),
			GFP_KERNEL);
	if (!ch->tr_events) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Failed to alloc tr_events buffer for ch_id:%d\n",
			ch->ch_id);
		rc = -ENOMEM;
		goto free_ereqs;
	}

	/* Allocate event requests */
	for (i = 0; i < ch->evt_req_size; ++i) {
		req = kzalloc(sizeof(struct event_req), GFP_KERNEL);
		if (!req)
			goto free_ereqs;
		list_add_tail(&req->list, &ch->event_req_buffers);
	}

	ch->curr_ereq =
		container_of(ch->event_req_buffers.next,
					struct event_req, list);
	list_del_init(&ch->curr_ereq->list);
	ch->curr_ereq->start = 0;

	/*
	 * Initialize cmpl event buffer indexes - evt_buf_rp and
	 * evt_buf_wp point to the first and last free index available.
	 */
	ch->evt_buf_rp = 0;
	ch->evt_buf_wp = ch->evt_buf_size - 1;

	return 0;

free_ereqs:
	if (!list_empty(&ch->event_req_buffers)) {
		list_for_each_entry_safe(req, tmp, &ch->event_req_buffers, list) {
			list_del(&req->list);
			kfree(req);
		}
	}
	kfree(ch->tr_events);
	ch->evt_buf_size = 0;
	ch->evt_req_size = 0;

	return rc;
}

static int __mhi_dev_open_channel(struct mhi_dev *mhi_ctx,
				  uint32_t chan_id,
				  struct mhi_dev_client **handle_client,
				  void (*mhi_dev_client_cb_reason)
				  (struct mhi_dev_client_cb_reason *cb))
{
	int rc = 0;
	struct mhi_dev_channel *ch;
	struct platform_device *pdev;

	if (!mhi_ctx || !mhi_ctx->mhi_hw_ctx->pdev) {
		mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR,
				"Invalid open channel call for ch_id:%d\n",
				chan_id);
		return -EINVAL;
	}

	pdev = mhi_ctx->mhi_hw_ctx->pdev;
	ch = mhi_ctx->ch[chan_id];
	if (!ch) {
		WARN_ON(1);
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
				"Pointer for chid%d is NULL\n", chan_id);
		return -ENOMEM;
	}
	mutex_lock(&ch->ch_lock);

	if (ch->active_client) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
			"ch_id:%d already opened by client\n", chan_id);
		rc = -EINVAL;
		goto exit;
	}

	/* Initialize the channel, client and state information */
	*handle_client = kzalloc(sizeof(struct mhi_dev_client), GFP_KERNEL);
	if (!(*handle_client)) {
		dev_err(&pdev->dev, "can not allocate mhi_dev memory\n");
		rc = -ENOMEM;
		goto exit;
	}

	rc = mhi_dev_alloc_evt_buf_evt_req(mhi_ctx, ch, NULL);
	if (rc)
		goto free_client;

	ch->active_client = (*handle_client);
	(*handle_client)->channel = ch;
	(*handle_client)->event_trigger = mhi_dev_client_cb_reason;
	(*handle_client)->vf_id = mhi_ctx->vf_id;
	ch->pend_wr_count = 0;
	ch->snd_cmpl_cnt = 1;

	if (ch->state == MHI_DEV_CH_UNINT) {
		ch->ring = mhi_ctx->ring[chan_id + mhi_ctx->ch_ring_start];
		ch->state = MHI_DEV_CH_PENDING_START;
	} else if (ch->state == MHI_DEV_CH_CLOSED)
		ch->state = MHI_DEV_CH_STARTED;
	else if (ch->state == MHI_DEV_CH_STOPPED)
		ch->state = MHI_DEV_CH_PENDING_START;

	goto exit;

free_client:
	kfree(*handle_client);
	*handle_client = NULL;

exit:
	mutex_unlock(&ch->ch_lock);
	return rc;
}

int mhi_dev_open_channel(uint32_t chan_id,
			 struct mhi_dev_client **handle_client,
			 void (*mhi_dev_client_cb_reason)
			 (struct mhi_dev_client_cb_reason *cb))
{
	struct mhi_dev *mhi = mhi_get_dev_ctx(mhi_hw_ctx, MHI_DEV_PHY_FUN);

	return __mhi_dev_open_channel(mhi, chan_id, handle_client,
				      mhi_dev_client_cb_reason);
}
EXPORT_SYMBOL_GPL(mhi_dev_open_channel);

int mhi_dev_vf_open_channel(uint32_t vf_id, uint32_t chan_id,
			    struct mhi_dev_client **handle_client,
			    void (*mhi_dev_client_cb_reason)
			    (struct mhi_dev_client_cb_reason *cb))
{
	struct mhi_dev *mhi = mhi_get_dev_ctx(mhi_hw_ctx, vf_id);

	return __mhi_dev_open_channel(mhi, chan_id, handle_client,
				      mhi_dev_client_cb_reason);

}
EXPORT_SYMBOL_GPL(mhi_dev_vf_open_channel);

int mhi_dev_channel_isempty(struct mhi_dev_client *handle)
{
	struct mhi_dev_channel *ch;
	int rc;

	if (!handle) {
		mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR, "Invalid ch access\n");
		return -EINVAL;
	}

	ch = handle->channel;
	if (!ch)
		return -EINVAL;

	rc = ch->ring->rd_offset == ch->ring->wr_offset;
	if (rc)
		mhi_log(handle->vf_id, MHI_MSG_WARNING, "Chan_id=0x%x is empty rp/wp:%zx\n",
			ch->ch_id,
			ch->ring->rd_offset);

	return rc;
}
EXPORT_SYMBOL_GPL(mhi_dev_channel_isempty);

bool mhi_dev_channel_has_pending_write(struct mhi_dev_client *handle)
{
	struct mhi_dev_channel *ch;

	if (!handle) {
		mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR, "Invalid ch access\n");
		return -EINVAL;
	}

	ch = handle->channel;
	if (!ch)
		return -EINVAL;

	return ch->pend_wr_count ? true : false;
}
EXPORT_SYMBOL_GPL(mhi_dev_channel_has_pending_write);

void mhi_dev_close_channel(struct mhi_dev_client *handle)
{
	struct mhi_dev_channel *ch;
	int count = 0;
	struct event_req *itr, *tmp;

	if (!handle) {
		mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR, "Invalid ch access:%d\n", -ENODEV);
		return;
	}
	ch = handle->channel;

	do {
		if (ch->flush_req_cnt != ch->pend_flush_cnt) {
			usleep_range(MHI_DEV_CH_CLOSE_TIMEOUT_MIN,
					MHI_DEV_CH_CLOSE_TIMEOUT_MAX);
		} else
			break;
	} while (++count < MHI_DEV_CH_CLOSE_TIMEOUT_COUNT);

	mutex_lock(&ch->ch_lock);

	if (ch->pend_wr_count)
		mhi_log(handle->vf_id, MHI_MSG_INFO, "%d writes pending for ch_id:%d\n",
			ch->pend_wr_count, ch->ch_id);
	if (!list_empty(&ch->event_req_buffers))
		mhi_log(handle->vf_id, MHI_MSG_INFO, "%d pending flush for ch_id:%d\n",
		ch->pend_wr_count, ch->ch_id);

	if (ch->state != MHI_DEV_CH_PENDING_START)
		if ((ch->ch_type == MHI_DEV_CH_TYPE_OUTBOUND_CHANNEL &&
			!mhi_dev_channel_isempty(handle)) || ch->tre_loc)
			mhi_log(handle->vf_id, MHI_MSG_DBG,
				"Trying to close an active ch_id:%d\n",
				ch->ch_id);

	if (!list_empty(&ch->flush_event_req_buffers)) {
		list_for_each_entry_safe(itr, tmp, &ch->flush_event_req_buffers, list) {
			itr->is_stale = true;
		}
	}

	ch->state = MHI_DEV_CH_CLOSED;
	ch->active_client = NULL;
	mhi_log(handle->vf_id, MHI_MSG_INFO,
		"MEM_ALLOC:ch:%d size:%lu CLNT_HANDLE\n",
		ch->ch_id, sizeof(struct mhi_dev_client));

	kfree(handle);

	mutex_unlock(&ch->ch_lock);
}
EXPORT_SYMBOL_GPL(mhi_dev_close_channel);

static int mhi_dev_check_tre_bytes_left(struct mhi_dev_channel *ch,
		struct mhi_dev_ring *ring, union mhi_dev_ring_element_type *el,
		struct mhi_req *mreq)
{
	uint32_t td_done = 0;

	/*
	 * A full TRE worth of data was consumed.
	 * Check if we are at a TD boundary.
	 */
	if (ch->tre_bytes_left == 0) {
		if (el->tre.chain) {
			if (el->tre.ieob)
				mhi_dev_send_completion_event_async(ch,
				ring->rd_offset, el->tre.len,
				MHI_CMD_COMPL_CODE_EOB, mreq);
			mreq->chain = 1;
		} else {
			if (el->tre.ieot)
				mhi_dev_send_completion_event_async(
				ch, ring->rd_offset, el->tre.len,
				MHI_CMD_COMPL_CODE_EOT, mreq);
			td_done = 1;
			mreq->chain = 0;
		}
		mhi_dev_ring_inc_index(ring, ring->rd_offset);
		ch->tre_bytes_left = 0;
		ch->tre_loc = 0;
	}

	return td_done;
}

int mhi_dev_read_channel(struct mhi_req *mreq)
{
	struct mhi_dev_channel *ch;
	struct mhi_dev_ring *ring;
	union mhi_dev_ring_element_type *el;
	size_t bytes_to_read, addr_offset;
	uint64_t read_from_loc;
	ssize_t bytes_read = 0;
	size_t write_to_loc = 0;
	uint32_t usr_buf_remaining, tre_size;
	int td_done = 0, rc = 0;
	struct mhi_dev_client *handle_client;
	struct mhi_dev *mhi_ctx = NULL;

	if (WARN_ON(!mreq))
		return -ENXIO;

	mhi_ctx = mhi_get_dev_ctx(mhi_hw_ctx, mreq->vf_id);
	if (!mhi_ctx) {
		mhi_log(mreq->vf_id, MHI_MSG_ERROR, "invalid mhi context for vf =%d\n",
			mreq->vf_id);
		return -ENXIO;
	}

	if (mhi_ctx->ctrl_info != MHI_STATE_CONNECTED) {
		mhi_log(mreq->vf_id, MHI_MSG_ERROR,
			"ch not connected:%d\n", mhi_ctx->ctrl_info);
		return -ENODEV;
	}

	if (!mreq->client) {
		mhi_log(mreq->vf_id, MHI_MSG_ERROR, "invalid mhi request\n");
		return -ENXIO;
	}

	if (atomic_read(&mhi_ctx->is_suspended)) {
		mhi_log(mreq->vf_id, MHI_MSG_ERROR,
			"mhi still in suspend, return %d for read ch_id:%d\n",
				rc, mreq->client->channel->ch_id);
		return -ENODEV;
	}

	handle_client = mreq->client;
	ch = handle_client->channel;
	usr_buf_remaining = mreq->len;
	ring = ch->ring;
	mreq->chain = 0;

	mutex_lock(&ch->ch_lock);

	do {
		if (ch->state == MHI_DEV_CH_STOPPED || ch->reset_pending) {
			mhi_log(mreq->vf_id, MHI_MSG_VERBOSE,
				"ch_id:%d already stopped or RST pending\n",
				mreq->chan);
			bytes_read = -1;
			goto exit;
		}

		el = &ring->ring_cache[ring->rd_offset];
		mhi_log(mreq->vf_id, MHI_MSG_VERBOSE,
				"TRE.PTR: 0x%llx, TRE.LEN: 0x%x, rd offset: %lu\n",
				el->tre.data_buf_ptr, el->tre.len, ring->rd_offset);

		if (ch->tre_loc) {
			bytes_to_read = min(usr_buf_remaining,
						ch->tre_bytes_left);
			mreq->chain = 1;
			mhi_log(mreq->vf_id, MHI_MSG_VERBOSE,
				"remaining buffered data size %d\n",
				(int) ch->tre_bytes_left);
		} else {
			if (ring->rd_offset == ring->wr_offset) {
				mhi_log(mreq->vf_id, MHI_MSG_VERBOSE,
					"nothing to read, returning\n");
				bytes_read = 0;
				goto exit;
			}


			ch->tre_loc = el->tre.data_buf_ptr;
			tre_size = el->tre.len;
			ch->tre_bytes_left = el->tre.len;
			mhi_log(mreq->vf_id, MHI_MSG_VERBOSE,
					"user_buf_remaining %d, tre_size %d\n",
					usr_buf_remaining, el->tre.len);
			bytes_to_read = min(usr_buf_remaining, tre_size);
		}

		bytes_read += bytes_to_read;
		addr_offset = el->tre.len - ch->tre_bytes_left;
		read_from_loc = ch->tre_loc + addr_offset;
		write_to_loc = (size_t) mreq->buf +
			(mreq->len - usr_buf_remaining);
		ch->tre_bytes_left -= bytes_to_read;
		mreq->el = el;
		mreq->transfer_len = bytes_to_read;
		mreq->rd_offset = ring->rd_offset;
		mhi_log(mreq->vf_id, MHI_MSG_VERBOSE, "reading %lu bytes from ch_id:%d\n",
				bytes_to_read, mreq->chan);
		rc = mhi_ctx->host_to_device((void *) write_to_loc,
				read_from_loc, bytes_to_read, mhi_ctx, mreq);
		if (rc) {
			mhi_log(mreq->vf_id, MHI_MSG_DBG,
				"dest va = 0x%zx ; source pa = 0x%llx ; size = %lu\n",
				write_to_loc, read_from_loc, bytes_to_read);
			mhi_log(mreq->vf_id, MHI_MSG_ERROR,
					"Error while reading ch_id:%d rc %d\n",
					mreq->chan, rc);
			mutex_unlock(&ch->ch_lock);
			return rc;
		}
		usr_buf_remaining -= bytes_to_read;

		if (mreq->mode == DMA_ASYNC) {
			ch->tre_bytes_left = 0;
			ch->tre_loc = 0;
			goto exit;
		} else {
			td_done = mhi_dev_check_tre_bytes_left(ch, ring,
					el, mreq);
		}
	} while (usr_buf_remaining  && !td_done);
	if (td_done && ch->state == MHI_DEV_CH_PENDING_STOP) {
		ch->state = MHI_DEV_CH_STOPPED;
		rc = mhi_dev_process_stop_cmd(ring, mreq->chan, mhi_ctx);
		if (rc) {
			mhi_log(mreq->vf_id, MHI_MSG_ERROR,
					"Error while stopping ch_id:%d\n",
					mreq->chan);
			bytes_read = -EIO;
		}
	}
exit:
	mutex_unlock(&ch->ch_lock);
	return bytes_read;
}
EXPORT_SYMBOL_GPL(mhi_dev_read_channel);

static void skip_to_next_td(struct mhi_dev_channel *ch)
{
	struct mhi_dev_ring *ring = ch->ring;
	union mhi_dev_ring_element_type *el;
	uint32_t td_boundary_reached = 0;

	ch->skip_td = true;
	el = &ring->ring_cache[ring->rd_offset];
	while (ring->rd_offset != ring->wr_offset) {
		if (td_boundary_reached) {
			ch->skip_td = false;
			break;
		}
		if (!el->tre.chain)
			td_boundary_reached = 1;
		mhi_dev_ring_inc_index(ring, ring->rd_offset);
		el = &ring->ring_cache[ring->rd_offset];
	}
}

int mhi_dev_write_channel(struct mhi_req *wreq)
{
	struct mhi_dev_channel *ch;
	struct mhi_dev_ring *ring;
	struct mhi_dev_client *handle_client;
	union mhi_dev_ring_element_type *el;
	enum mhi_dev_cmd_completion_code code = MHI_CMD_COMPL_CODE_INVALID;
	int rc = 0;
	uint64_t skip_tres = 0, write_to_loc;
	size_t read_from_loc;
	uint32_t usr_buf_remaining;
	size_t usr_buf_offset = 0;
	size_t bytes_to_write = 0;
	size_t bytes_written = 0;
	uint32_t tre_len = 0, suspend_wait_timeout = 0;
	bool async_wr_sched = false;
	enum mhi_ctrl_info info;
	struct mhi_dev *mhi_ctx = NULL;

	if (WARN_ON(!wreq || !wreq->client || !wreq->buf)) {
		mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR,
				"invalid parameters\n");
		return -ENXIO;
	}

	mhi_ctx = mhi_get_dev_ctx(mhi_hw_ctx, wreq->vf_id);
	if (!mhi_ctx) {
		mhi_log(wreq->vf_id, MHI_MSG_ERROR, "invalid mhi context for vf =%d\n",
			wreq->vf_id);
		return -ENXIO;
	}

	if (mhi_ctx->ctrl_info != MHI_STATE_CONNECTED) {
		mhi_log(wreq->vf_id, MHI_MSG_ERROR,
			"ch not connected:%d\n", mhi_ctx->ctrl_info);
		return -ENODEV;
	}

	usr_buf_remaining =  wreq->len;
	handle_client = wreq->client;
	ch = handle_client->channel;
	mutex_lock(&mhi_ctx->mhi_write_test);

	if (atomic_read(&mhi_ctx->is_suspended)) {
		/*
		 * Expected usage is when there is a write
		 * to the MHI core -> notify SM.
		 */
		mutex_lock(&mhi_ctx->mhi_lock);
		mhi_log(wreq->vf_id, MHI_MSG_CRITICAL, "Wakeup by ch_id:%d\n", ch->ch_id);
		rc = mhi_dev_notify_sm_event(mhi_ctx, MHI_DEV_EVENT_CORE_WAKEUP);
		if (rc) {
			mhi_log(wreq->vf_id, MHI_MSG_ERROR,
					"error sending core wakeup event\n");
			mutex_unlock(&mhi_ctx->mhi_lock);
			mutex_unlock(&mhi_ctx->mhi_write_test);
			return rc;
		}
		mutex_unlock(&mhi_ctx->mhi_lock);
	}

	while (atomic_read(&mhi_ctx->is_suspended) &&
			suspend_wait_timeout < MHI_WAKEUP_TIMEOUT_CNT) {
		/* wait for the suspend to finish */
		msleep(MHI_SUSPEND_MIN);
		suspend_wait_timeout++;
	}

	if (suspend_wait_timeout >= MHI_WAKEUP_TIMEOUT_CNT ||
				mhi_ctx->ctrl_info != MHI_STATE_CONNECTED) {
		mhi_log(wreq->vf_id, MHI_MSG_ERROR, "Failed to wake up core\n");
		mutex_unlock(&mhi_ctx->mhi_write_test);
		return -ENODEV;
	}


	ring = ch->ring;

	mutex_lock(&ch->ch_lock);

	rc = mhi_vf_ctrl_state_info(mhi_ctx->vf_id, ch->ch_id, &info);
	if (rc || (info != MHI_STATE_CONNECTED)) {
		mhi_log(wreq->vf_id, MHI_MSG_ERROR, "ch_id %d not started by host\n",
				ch->ch_id);
		mutex_unlock(&ch->ch_lock);
		mutex_unlock(&mhi_ctx->mhi_write_test);
		return -ENODEV;
	}

	ch->pend_wr_count++;
	if (ch->state == MHI_DEV_CH_STOPPED || ch->reset_pending) {
		mhi_log(wreq->vf_id, MHI_MSG_ERROR,
			"ch_id:%d already stopped or RST pending\n",
			wreq->chan);
		bytes_written = -1;
		goto exit;
	}

	if (ch->state == MHI_DEV_CH_PENDING_STOP) {
		if (mhi_dev_process_stop_cmd(ring, wreq->chan, mhi_ctx) < 0)
			bytes_written = -1;
		goto exit;
	}

	if (ch->skip_td)
		skip_to_next_td(ch);

	do {
		if (ring->rd_offset == ring->wr_offset) {
			mhi_log(wreq->vf_id, MHI_MSG_ERROR,
				"rd & wr offsets are equal for ch_id:%d\n",
				 wreq->chan);
			mhi_log(wreq->vf_id, MHI_MSG_INFO, "No TREs available\n");
			break;
		}

		el = &ring->ring_cache[ring->rd_offset];
		tre_len = el->tre.len;

		bytes_to_write = min(usr_buf_remaining, tre_len);
		usr_buf_offset = wreq->len - bytes_to_write;
		read_from_loc = (size_t) wreq->buf + usr_buf_offset;
		write_to_loc = el->tre.data_buf_ptr;
		wreq->rd_offset = ring->rd_offset;
		wreq->el = el;
		wreq->transfer_len = bytes_to_write;
		rc = mhi_ctx->device_to_host(write_to_loc,
						(void *) read_from_loc,
						bytes_to_write,
						mhi_ctx, wreq);
		if (rc) {
			mhi_log(wreq->vf_id, MHI_MSG_ERROR,
					"Error while writing ch_id:%d rc %d\n",
					wreq->chan, rc);
			goto exit;
		} else if (wreq->mode == DMA_ASYNC)
			async_wr_sched = true;
		bytes_written += bytes_to_write;
		usr_buf_remaining -= bytes_to_write;

		if (usr_buf_remaining) {
			if (!el->tre.chain)
				code = MHI_CMD_COMPL_CODE_OVERFLOW;
			else if (el->tre.ieob)
				code = MHI_CMD_COMPL_CODE_EOB;
		} else {
			if (el->tre.chain)
				skip_tres = 1;
			code = MHI_CMD_COMPL_CODE_EOT;
		}
		if (wreq->mode == DMA_SYNC) {
			rc = mhi_dev_send_completion_event(ch,
					ring->rd_offset, bytes_to_write, code);
			if (rc) {
				mhi_log(wreq->vf_id, MHI_MSG_VERBOSE,
					"err in snding cmpl evt ch_id:%d\n",
					wreq->chan);
			}
			 mhi_dev_ring_inc_index(ring, ring->rd_offset);
		}

		if (ch->state == MHI_DEV_CH_PENDING_STOP)
			break;

	} while (!skip_tres && usr_buf_remaining);

	if (skip_tres)
		skip_to_next_td(ch);

	if (ch->state == MHI_DEV_CH_PENDING_STOP) {
		rc = mhi_dev_process_stop_cmd(ring, wreq->chan, mhi_ctx);
		if (rc) {
			mhi_log(wreq->vf_id, MHI_MSG_ERROR,
				"ch_id:%d stop failed\n", wreq->chan);
		}
	}
exit:
	if (wreq->mode == DMA_SYNC || !async_wr_sched)
		ch->pend_wr_count--;
	mutex_unlock(&ch->ch_lock);
	mutex_unlock(&mhi_ctx->mhi_write_test);
	return bytes_written;
}
EXPORT_SYMBOL_GPL(mhi_dev_write_channel);

struct mhi_dev_ops dev_ops = {
	.register_state_cb	= mhi_vf_register_state_cb,
	.ctrl_state_info	= mhi_vf_ctrl_state_info,
	.open_channel		= mhi_dev_vf_open_channel,
	.close_channel		= mhi_dev_close_channel,
	.write_channel		= mhi_dev_write_channel,
	.read_channel		= mhi_dev_read_channel,
	.is_channel_empty	= mhi_dev_channel_isempty,
};

static int mhi_dev_recover(struct mhi_dev *mhi)
{
	int rc = 0;
	uint32_t syserr, max_cnt = 0, bhi_intvec = 0, bhi_max_cnt = 0;
	u32 mhi_reset;
	enum mhi_dev_state state;

	/* Check if MHI is in syserr */
	mhi_dev_mmio_masked_read(mhi, MHISTATUS,
				MHISTATUS_SYSERR_MASK,
				MHISTATUS_SYSERR_SHIFT, &syserr);

	mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "mhi_syserr = 0x%X\n", syserr);
	if (syserr) {
		/* Poll for the host to set the reset bit */
		rc = mhi_dev_mmio_get_mhi_state(mhi, &state, &mhi_reset);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"get mhi state failed\n");
			return rc;
		}

		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "mhi_state = 0x%X, reset = %d\n",
				state, mhi_reset);

		if (mhi->msi_disable)
			goto poll_for_reset;

		rc = mhi_dev_mmio_read(mhi, BHI_INTVEC, &bhi_intvec);
		if (rc)
			return rc;

		while (bhi_intvec == 0xffffffff &&
			bhi_max_cnt < MHI_BHI_INTVEC_MAX_CNT) {
			/* Wait for Host to set the bhi_intvec */
			msleep(MHI_BHI_INTVEC_WAIT_MS);
			mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Wait for Host to set BHI_INTVEC\n");
			rc = mhi_dev_mmio_read(mhi, BHI_INTVEC, &bhi_intvec);
			if (rc) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"Get BHI_INTVEC failed\n");
				return rc;
			}
			bhi_max_cnt++;
		}

		if (bhi_max_cnt == MHI_BHI_INTVEC_MAX_CNT) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Host failed to set BHI_INTVEC\n");
			return -EINVAL;
		}

		if (bhi_intvec != 0xffffffff) {
			/* Indicate the host that device is ready */
			rc = ep_pcie_trigger_msi(mhi->mhi_hw_ctx->phandle, bhi_intvec, mhi->vf_id);
			if (rc) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR, "error sending msi\n");
				return rc;
			}
		}

poll_for_reset:
		/* Poll for the host to set the reset bit */
		rc = mhi_dev_mmio_get_mhi_state(mhi, &state, &mhi_reset);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "get mhi state failed\n");
			return rc;
		}

		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "mhi_state = 0x%X, reset = %d\n",
				state, mhi_reset);

		while (mhi_reset != 0x1 && max_cnt < MHI_SUSPEND_TIMEOUT) {
			/* Wait for Host to set the reset */
			msleep(MHI_SUSPEND_MIN);
			rc = mhi_dev_mmio_get_mhi_state(mhi, &state,
								&mhi_reset);
			if (rc) {
				mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"get mhi state failed\n");
				return rc;
			}
			max_cnt++;
		}

		if (!mhi_reset) {
			mhi_log(mhi->vf_id, MHI_MSG_VERBOSE, "Host failed to set reset\n");
			return -EINVAL;
		}

	}
	/*
	 * Now mask the interrupts so that the state machine moves
	 * only after MHI DMA is ready
	 */
	mhi_dev_mmio_mask_interrupts(mhi);
	return 0;
}

static void mhi_dev_enable(struct work_struct *work)
{
	int rc = 0;
	struct mhi_dev *mhi = container_of(work,
				struct mhi_dev, ring_init_cb_work);
	u32 mhi_reset;
	enum mhi_dev_state state;
	uint32_t max_cnt = 0;

	mutex_lock(&mhi->mhi_lock);

	if (mhi->use_mhi_dma) {
		rc = mhi_dma_fun_ops->mhi_dma_memcpy_init(mhi->mhi_dma_fun_params);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "ipa dma init failed\n");
			goto exit;
		}

		rc = mhi_dma_fun_ops->mhi_dma_memcpy_enable(mhi->mhi_dma_fun_params);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "ipa enable failed\n");
			goto exit;
		}
	}

	rc = mhi_dev_mmio_get_mhi_state(mhi, &state, &mhi_reset);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "get mhi state failed\n");
		goto exit;
	}
	if (mhi_reset) {
		mhi_dev_mmio_clear_reset(mhi);
		mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
			"Cleared reset before waiting for M0\n");
	}

	while (state != MHI_DEV_M0_STATE && !mhi->stop_polling_m0 &&
		((max_cnt < MHI_SUSPEND_TIMEOUT) || mhi->no_m0_timeout)) {
		/* Wait for Host to set the M0 state */
		msleep(MHI_SUSPEND_MIN);
		rc = mhi_dev_mmio_get_mhi_state(mhi, &state, &mhi_reset);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "get mhi state failed\n");
			goto exit;
		}
		if (mhi_reset) {
			mhi_dev_mmio_clear_reset(mhi);
			mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Cleared reset while waiting for M0\n");
		}
		max_cnt++;
	}

	mhi_log(mhi->vf_id, MHI_MSG_INFO, "state:%d\n", state);

	if (state == MHI_DEV_M0_STATE) {
		/* Setting the default wake_db status to deassert */
		mhi->wake_db_status = false;

		rc = mhi_dev_cache_host_cfg(mhi);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
					"Failed to cache the host config\n");
			goto exit;
		}

		rc = mhi_dev_mmio_set_env(mhi, MHI_ENV_VALUE);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR, "env setting failed\n");
			goto exit;
		}
	} else {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "MHI device failed to enter M0\n");
		goto exit;
	}

	rc = mhi_hwc_init(mhi);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "error during hwc_init\n");
		goto exit;
	}

	if (mhi->use_edma || mhi->no_path_from_ipa_to_pcie) {
		if (mhi->config_iatu || mhi->mhi_int) {
			mhi->mhi_int_en = true;
			enable_irq(mhi->mhi_irq);
		}
	}

	/*
	 * ctrl_info might already be set to CONNECTED state in the
	 * callback function mhi_hwc_cb triggered from MHI DMA when mhi_hwc_init
	 * is called above, so set to CONFIGURED state only when it
	 * is not already set to CONNECTED
	 */
	if (mhi->ctrl_info != MHI_STATE_CONNECTED)
		mhi_update_state_info(mhi, MHI_STATE_CONFIGURED);

	mutex_unlock(&mhi->mhi_lock);

	/* Enable MHI dev network stack Interface */
	rc = mhi_dev_net_interface_init(&dev_ops, mhi->vf_id, mhi_hw_ctx->ep_cap.num_vfs);
	if (rc)
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Failed to initialize mhi_dev_net iface\n");
	return;
exit:
	mutex_unlock(&mhi->mhi_lock);
}

static void mhi_ring_init_cb(void *data)
{
	struct mhi_dev *mhi = data;

	if (WARN_ON(!mhi))
		return;

	mutex_lock(&mhi->mhi_lock);
	if (!mhi->init_done) {
		mhi_log(mhi->vf_id, MHI_MSG_INFO, "mhi init is not done, returning\n");
		mhi->mhi_dma_ready = true;
		mutex_unlock(&mhi->mhi_lock);
		return;
	}
	queue_work(mhi->ring_init_wq, &mhi->ring_init_cb_work);
	mutex_unlock(&mhi->mhi_lock);
}

static int __mhi_register_state_cb(void (*mhi_state_cb)
				   (struct mhi_dev_client_cb_data *cb_data),
				   void *data,
				   enum mhi_client_channel channel,
				   struct mhi_dev *mhi)
{
	struct mhi_dev_ready_cb_info *cb_info = NULL;

	cb_info = kmalloc(sizeof(*cb_info), GFP_KERNEL);
	if (!cb_info)
		return -ENOMEM;

	cb_info->cb = mhi_state_cb;
	cb_info->cb_data.user_data = data;
	cb_info->cb_data.channel = channel;

	list_add_tail(&cb_info->list, &mhi->client_cb_list);

	/**
	 * If channel is open during registration, no callback is issued.
	 * Instead return -EEXIST to notify the client. Clients request
	 * is added to the list to notify future state change notification.
	 * Channel struct may not be allocated yet if this function is called
	 * early during boot - add an explicit check for non-null "ch".
	 */
	if (mhi->ch && mhi->ch[channel] && (mhi->ch[channel]->state == MHI_DEV_CH_STARTED))
		return -EEXIST;

	return 0;
}

int mhi_register_state_cb(void (*mhi_state_cb) (struct mhi_dev_client_cb_data *cb_data),
				void *data, enum mhi_client_channel channel)
{
	struct mhi_dev *mhi = mhi_get_dev_ctx(mhi_hw_ctx, MHI_DEV_PHY_FUN);
	int ret_val = 0;

	if (!mhi)
		return -EPROBE_DEFER;

	if (channel >= MHI_MAX_SOFTWARE_CHANNELS) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Invalid ch_id:%d\n", channel);
		return -EINVAL;
	}

	mutex_lock(&mhi->mhi_lock);
	ret_val = __mhi_register_state_cb(mhi_state_cb, data, channel, mhi);
	mutex_unlock(&mhi->mhi_lock);

	return ret_val;
}
EXPORT_SYMBOL_GPL(mhi_register_state_cb);

int mhi_vf_register_state_cb(void (*mhi_state_cb)(struct mhi_dev_client_cb_data *cb_data),
			     void *data, enum mhi_client_channel channel, unsigned int vf_id)
{

	struct mhi_dev *mhi = mhi_get_dev_ctx(mhi_hw_ctx, vf_id);
	int ret_val = 0;

	if (!mhi)
		return -EPROBE_DEFER;

	if (channel >= MHI_MAX_SOFTWARE_CHANNELS) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Invalid ch_id:%d\n", channel);
		return -EINVAL;
	}

	mutex_lock(&mhi->mhi_lock);
	ret_val = __mhi_register_state_cb(mhi_state_cb, data, channel, mhi);
	mutex_unlock(&mhi->mhi_lock);

	return ret_val;
}
EXPORT_SYMBOL_GPL(mhi_vf_register_state_cb);

static void mhi_update_state_info_ch(uint32_t vf_id,
				     uint32_t ch_id,
				     enum mhi_ctrl_info info)
{
	struct mhi_dev_client_cb_reason reason;

	/* Currently no clients register for HW channel notification */
	if (ch_id >= MHI_MAX_SOFTWARE_CHANNELS)
		return;

	channel_state_info[vf_id][ch_id].ctrl_info = info;
	if (ch_id == MHI_CLIENT_QMI_OUT || ch_id == MHI_CLIENT_QMI_IN) {
		/* For legacy reasons for QTI client */
		reason.reason = MHI_DEV_CTRL_UPDATE;
		uci_ctrl_update(&reason);
	}
}

static inline void mhi_update_state_info(struct mhi_dev *mhi,
					 enum mhi_ctrl_info info)
{
	mhi->ctrl_info = info;
}

static int __mhi_ctrl_state_info(struct mhi_dev *mhi, uint32_t vf_id,
				    uint32_t ch_id, uint32_t *info)
{
	if (ch_id == MHI_DEV_UEVENT_CTRL)
		*info = mhi->ctrl_info;
	else
		if (ch_id < MHI_MAX_SOFTWARE_CHANNELS)
			*info = channel_state_info[vf_id][ch_id].ctrl_info;
		else
			return -EINVAL;

	mhi_log(vf_id, MHI_MSG_VERBOSE, "vf_id:%d ch_id:%d, ctrl:%d\n",
				  vf_id, ch_id, *info);

	return 0;
}

int mhi_ctrl_state_info(uint32_t ch_id, uint32_t *info)
{
	struct mhi_dev *mhi = mhi_get_dev_ctx(mhi_hw_ctx, MHI_DEV_PHY_FUN);

	if (!mhi) {
		mhi_log(MHI_DEV_PHY_FUN, MHI_MSG_ERROR, "MHI is NULL, defering\n");
		return -EINVAL;
	}

	return __mhi_ctrl_state_info(mhi, mhi->vf_id, ch_id, info);
}
EXPORT_SYMBOL_GPL(mhi_ctrl_state_info);

int mhi_vf_ctrl_state_info(uint32_t vf_id, uint32_t ch_id, uint32_t *info)
{
	struct mhi_dev *mhi = mhi_get_dev_ctx(mhi_hw_ctx, vf_id);

	if (!mhi) {
		mhi_log(vf_id, MHI_MSG_ERROR, "MHI is NULL, defering\n");
		return -EINVAL;
	}

	return __mhi_ctrl_state_info(mhi, vf_id, ch_id, info);
}
EXPORT_SYMBOL_GPL(mhi_vf_ctrl_state_info);

static int mhi_get_device_tree_data(struct mhi_dev_ctx *mhictx, int vf_id)
{
	struct mhi_dev *mhi = mhictx->mhi_dev[vf_id];
	struct platform_device *pdev = mhictx->pdev;
	struct resource *res_mem = NULL;
	char mhi_vf_int_name[23] = "mhi-virt-device-int-%d";
	int rc = 0;

	res_mem = platform_get_resource_byname(pdev,
					IORESOURCE_MEM, "mhi_mmio_base");
	if (!res_mem) {
		dev_err(&pdev->dev,
			"Request MHI MMIO physical memory region failed\n");
		return -EINVAL;
	}

	mhi->mmio_base_pa_addr = res_mem->start + (vf_id * MHI_1K_SIZE);
	mhi->mmio_base_addr = ioremap(mhi->mmio_base_pa_addr, MHI_1K_SIZE);
	if (!mhi->mmio_base_addr) {
		dev_err(&pdev->dev, "Failed to IO map MHI MMIO registers\n");
		return -EINVAL;
	}

	mhi->use_mhi_dma = of_property_read_bool((&pdev->dev)->of_node,
					"qcom,use-mhi-dma-software-channel");

	mhi->no_path_from_ipa_to_pcie = of_property_read_bool((&pdev->dev)->of_node,
					"qcom,no-path-from-ipa-to-pcie");

	rc = of_property_read_u32((&pdev->dev)->of_node,
				"qcom,mhi-ifc-id",
				&mhictx->ifc_id);
	if (rc) {
		dev_err(&pdev->dev, "qcom,mhi-ifc-id does not exist\n");
		goto err;
	}

	mhi->vf_id = vf_id;

	mhi->is_mhi_pf = !vf_id;

	if (mhi->is_mhi_pf)
		mhi->mhi_dma_fun_params.function_type = MHI_DMA_FUNCTION_TYPE_PHYSICAL;
	else
		mhi->mhi_dma_fun_params.function_type = MHI_DMA_FUNCTION_TYPE_VIRTUAL;

	mhi->mhi_dma_fun_params.vf_id = mhi->vf_id - 1;

	rc = of_property_read_u32((&pdev->dev)->of_node,
				"qcom,mhi-ep-msi",
				&mhi->mhi_ep_msi_num);
	if (rc) {
		dev_err(&pdev->dev, "qcom,mhi-ep-msi does not exist\n");
		goto err;
	}

	rc = of_property_read_u32((&pdev->dev)->of_node,
				"qcom,mhi-version",
				&mhi->mhi_version);
	if (rc) {
		dev_err(&pdev->dev, "qcom,mhi-version does not exist\n");
		goto err;
	}

	rc = of_property_read_u32((&pdev->dev)->of_node,
				"qcom,mhi-chan-hw-base",
				&mhi->mhi_chan_hw_base);
	if (rc)
		mhi->mhi_chan_hw_base = HW_CHANNEL_BASE;

	mhi_log(vf_id, MHI_MSG_INFO, "MHI HW_CHAN_BASE:%d\n", mhi->mhi_chan_hw_base);

	mhi->use_edma = of_property_read_bool((&pdev->dev)->of_node,
				"qcom,use-pcie-edma");

	mhi->config_iatu = of_property_read_bool((&pdev->dev)->of_node,
				"qcom,mhi-config-iatu");

	if (mhi->config_iatu) {
		rc = of_property_read_u64((&pdev->dev)->of_node,
				"qcom,mhi-local-pa-base",
				&mhi->device_local_pa_base);
		if (rc) {
			dev_err(&pdev->dev, "qcom,mhi-local-pa-base does not exist\n");
			goto err;
		}
	}

	mhi->mhi_int = of_property_read_bool((&pdev->dev)->of_node,
					"qcom,mhi-interrupt");

	if (mhi->config_iatu || mhi->mhi_int) {
		mhi->mhi_irq = platform_get_irq_byname(pdev, "mhi-device-inta");
		if (mhi->mhi_irq < 0) {
			dev_err(&pdev->dev, "Invalid MHI device interrupt\n");
			rc = mhi->mhi_irq;
			goto err;
		}

		if (!mhi->is_mhi_pf) {
			snprintf(mhi_vf_int_name, sizeof(mhi_vf_int_name),
					"mhi-virt-device-int-%d", vf_id - 1);
			mhi_log(vf_id, MHI_MSG_INFO, "mhi irq %d vf_id:%d %s\n",
					mhi->mhi_irq, vf_id - 1, mhi_vf_int_name);
			mhi->mhi_irq = platform_get_irq_byname(pdev, mhi_vf_int_name);
		}
	}

	mhi->is_flashless = of_property_read_bool((&pdev->dev)->of_node,
				"qcom,mhi-is-flashless");
	mhi->mhi_has_smmu = of_property_read_bool((&pdev->dev)->of_node,
				"qcom,mhi-has-smmu");

	mhi->enable_m2 = of_property_read_bool((&pdev->dev)->of_node,
				"qcom,enable-m2");

	mhi->no_m0_timeout = of_property_read_bool((&pdev->dev)->of_node,
		"qcom,no-m0-timeout");

	rc = of_property_read_u32((&pdev->dev)->of_node, "qcom,mhi-num-ipc-pages-dev-fac",
					&mhi->mhi_num_ipc_pages_dev_fac);
	if (rc) {
		dev_err(&pdev->dev, "qcom,mhi-num-ipc-pages-dev-fac does not exist\n");
		mhi->mhi_num_ipc_pages_dev_fac = 1;
	}
	if (mhi->mhi_num_ipc_pages_dev_fac < 1 || mhi->mhi_num_ipc_pages_dev_fac > 5) {
		dev_err(&pdev->dev, "Invalid value received for mhi->mhi_num_ipc_pages_dev_fac\n");
		mhi->mhi_num_ipc_pages_dev_fac = 1;
	}

	return 0;
err:
	iounmap(mhi->mmio_base_addr);
	return rc;
}

static int mhi_dev_basic_init(struct mhi_dev_ctx *mhictx, int vf_id)
{
	struct mhi_dev *mhi = mhictx->mhi_dev[vf_id];
	struct platform_device *pdev = mhictx->pdev;
	int rc = 0;

	if (mhi->use_edma) {
		mhi->read_from_host = mhi_dev_read_from_host_edma;
		mhi->write_to_host = mhi_dev_write_to_host_edma;
		mhi->host_to_device = mhi_transfer_host_to_device_edma;
		mhi->device_to_host = mhi_transfer_device_to_host_edma;
	} else {
		mhi->read_from_host = mhi_dev_read_from_host_mhi_dma;
		mhi->write_to_host = mhi_dev_write_to_host_mhi_dma;
		mhi->host_to_device = mhi_transfer_host_to_device_mhi_dma;
		mhi->device_to_host = mhi_transfer_device_to_host_mhi_dma;
	}

	INIT_WORK(&mhi->pcie_event, mhi_dev_pcie_handle_event);
	mhi->pcie_event_wq = alloc_workqueue("mhi_dev_pcie_event_wq",
							WQ_HIGHPRI, 1);
	if (!mhi->pcie_event_wq) {
		dev_err(&pdev->dev, "mhi pcie ev_wq alloc failed\n");
		rc = -ENOMEM;
		goto err;
	}

	device_init_wakeup(mhictx->dev, true);
	/* MHI device will be woken up from PCIe event */
	device_set_wakeup_capable(mhictx->dev, false);
	/* Hold a wakelock until completion of M0 */
	pm_stay_awake(mhictx->dev);
	atomic_add(1, &mhi->mhi_dev_wake);
	mhi_log(vf_id, MHI_MSG_VERBOSE, "acquiring wakelock\n");

	return 0;
err:
	iounmap(mhi->mmio_base_addr);
	return rc;
}

static int mhi_get_device_info(struct platform_device *pdev)
{
	struct mhi_dev *mhi_pf;
	struct mhi_dev_ctx *mhictx;
	int ret = 0;

	mhictx = devm_kzalloc(&pdev->dev,
			sizeof(struct mhi_dev_ctx), GFP_KERNEL);
	if (!mhictx) {
		mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR, "Failed to alloc mhictx\n");
		return -ENOMEM;
	}

	mhictx->pdev = pdev;
	mhictx->dev = &pdev->dev;
	mhi_hw_ctx = mhictx;

	mhi_pf = devm_kzalloc(&pdev->dev,
			sizeof(struct mhi_dev), GFP_KERNEL);
	if (!mhi_pf) {
		mhi_log(MHI_DEV_PHY_FUN, MHI_MSG_ERROR, "Failed to alloc mhi_pf\n");
		return -ENOMEM;
	}

	/* Allocate memory for Physcial MHI instance */
	mhictx->mhi_dev[MHI_DEV_PHY_FUN] = mhi_pf;

	mhi_pf->mhi_hw_ctx = mhictx;

	/* populate or initialize physical MHI details in probe */
	ret = mhi_get_device_tree_data(mhictx, MHI_DEV_PHY_FUN);
	if (ret != 0) {
		dev_err(&pdev->dev, "mhi get device tree data failed\n");
		return ret;
	}

	ret = mhi_dev_basic_init(mhictx, MHI_DEV_PHY_FUN);
	if (ret != 0) {
		dev_err(&pdev->dev, "mhi dev basic init failed\n");
		return ret;
	}

	return 0;
}

static int mhi_deinit(struct mhi_dev *mhi)
{

	mhi_dev_sm_exit(mhi);

	return 0;
}

static int mhi_init(struct mhi_dev *mhi, bool init_state)
{
	int rc = 0;
	struct platform_device *pdev = mhi->mhi_hw_ctx->pdev;

	if (mhi->use_edma) {
		rc = mhi_edma_init(&pdev->dev);
		if (rc) {
			pr_err("MHI: mhi edma init failed, rc = %d\n", rc);
			return rc;
		}
	}

	if (mhi->use_edma || mhi->no_path_from_ipa_to_pcie) {
		rc = dma_set_mask_and_coherent(&pdev->dev,
				DMA_BIT_MASK(DMA_SLAVE_BUSWIDTH_64_BYTES));
		if (rc) {
			pr_err("Error set MHI DMA mask: rc = %d\n", rc);
			return rc;
		}
	}

	rc = mhi_dev_mmio_init(mhi);
	if (rc) {
		mhi_log(mhi->vf_id, MHI_MSG_ERROR,
			"Failed to update the MMIO init\n");
		return rc;
	}

	/*
	 * In warm reboot path, boot loaders doesn't clear error state
	 * in MHI-STATUS, MHI-CTRL. So if MHI reset is set by, update status also
	 * to RESET state.
	 */
	if (init_state == MHI_REINIT || mhi->vf_id) {
		mhi_log(mhi->vf_id, MHI_MSG_DBG,
			"Set MHISTATUS/MHICTRL to Reset, init_state:%d for vf=%d\n",
			init_state, mhi->vf_id);
		mhi_dev_mmio_reset(mhi);
	}

	/*
	 * mhi_init is also called during device reset, in
	 * which case channel mem will already be allocated.
	 */
	if (!mhi->ch) {
		mhi->ch = devm_kzalloc(&pdev->dev,
			(sizeof(struct mhi_dev_channel *) * (mhi->cfg.channels)), GFP_KERNEL);
		if (!mhi->ch)
			return -ENOMEM;
	}

	spin_lock_init(&mhi->lock);
	spin_lock_init(&mhi->msi_lock);

	if (!mhi->mmio_backup)
		mhi->mmio_backup = devm_kzalloc(&pdev->dev, MHI_DEV_MMIO_RANGE, GFP_KERNEL);

	if (!mhi->mmio_backup)
		return -ENOMEM;

	return 0;
}

static int mhi_dev_channel_init(struct mhi_dev *mhi, uint32_t ch_id)
{
	struct platform_device *pdev = mhi->mhi_hw_ctx->pdev;

	if (!mhi->ch[ch_id]) {
		mhi->ch[ch_id]  = devm_kzalloc(&pdev->dev, sizeof(struct mhi_dev_channel),
						GFP_KERNEL);
	}
	if (!mhi->ch[ch_id])
		return -ENOMEM;
	mhi->ch[ch_id]->ch_id = ch_id;
	mutex_init(&mhi->ch[ch_id]->ch_lock);
	INIT_LIST_HEAD(&mhi->ch[ch_id]->event_req_buffers);
	INIT_LIST_HEAD(&mhi->ch[ch_id]->flush_event_req_buffers);

	/*
	 * Initialize CH Transfer ring for channels that have
	 * received start. This memory is then not deallocated
	 * at anytime in future, to help debugging.
	 */
	if (!mhi->ring[mhi->ch_ring_start + ch_id])
		mhi->ring[mhi->ch_ring_start + ch_id] = devm_kzalloc(&pdev->dev,
								sizeof(struct mhi_dev_ring),
								GFP_KERNEL);
	if (!mhi->ring[mhi->ch_ring_start + ch_id])
		return -ENOMEM;
	mhi_ring_init(mhi->ring[mhi->ch_ring_start + ch_id], RING_TYPE_CH,
			mhi->ch_ring_start + ch_id);
	mhi_ring_set_cb(mhi->ring[mhi->ch_ring_start + ch_id], mhi_dev_process_tre_ring);
	mhi_log(mhi->vf_id, MHI_MSG_INFO, "Memory allocated for ring of ch =%d\n",
			ch_id);

	return 0;
}

static int mhi_dev_resume_mmio_mhi_reinit(struct mhi_dev *mhi_ctx)
{
	int rc = 0;

	mutex_lock(&mhi_ctx->mhi_lock);
	if (atomic_read(&mhi_ctx->re_init_done)) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_INFO, "Re_init done, return\n");
		mutex_unlock(&mhi_ctx->mhi_lock);
		return 0;
	}

	rc = mhi_init(mhi_ctx, MHI_REINIT);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
			"Error initializing MHI MMIO with %d\n", rc);
		goto fail;
	}

	if (mhi_ctx->use_mhi_dma) {
		/*
		 * In case of MHI VF, mhi_ring_int_cb() is not called as MHI DMA ready
		 * event is registered/handled by PF. Involking mhi_dev_enable()
		 * is necessary for enabling MHI IRQ.
		 */
		if (mhi_ctx->mhi_dma_ready || !mhi_ctx->is_mhi_pf)
			queue_work(mhi_ctx->ring_init_wq, &mhi_ctx->ring_init_cb_work);
	}

	/* Invoke MHI SM when device is in RESET state */
	rc = mhi_dev_sm_init(mhi_ctx);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR, "Error during SM init\n");
		goto fail;
	}

	/* set the env before setting the ready bit */
	rc = mhi_dev_mmio_set_env(mhi_ctx, MHI_ENV_VALUE);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR, "env setting failed\n");
		goto fail;
	}

	if (mhi_ctx->is_mhi_pf) {
		mhi_ctx->mhi_hw_ctx->event_reg.events = EP_PCIE_EVENT_PM_D3_HOT |
			EP_PCIE_EVENT_PM_D3_COLD |
			EP_PCIE_EVENT_PM_D0 |
			EP_PCIE_EVENT_PM_RST_DEAST |
			EP_PCIE_EVENT_L1SUB_TIMEOUT |
			EP_PCIE_EVENT_L1SUB_TIMEOUT_EXIT |
			EP_PCIE_EVENT_LINKDOWN |
			EP_PCIE_EVENT_LINKUP_VF;
		if (!mhi_ctx->mhi_int)
			mhi_ctx->mhi_hw_ctx->event_reg.events |= EP_PCIE_EVENT_MHI_A7;
		mhi_ctx->mhi_hw_ctx->event_reg.user = mhi_ctx->mhi_hw_ctx;
		mhi_ctx->mhi_hw_ctx->event_reg.mode = EP_PCIE_TRIGGER_CALLBACK;
		mhi_ctx->mhi_hw_ctx->event_reg.callback = mhi_dev_sm_pcie_handler;

		rc = ep_pcie_register_event(mhi_ctx->mhi_hw_ctx->phandle,
				&mhi_ctx->mhi_hw_ctx->event_reg);
		if (rc) {
			mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
				"Failed to register for events from PCIe\n");
			goto fail;
		}
	}

	/* All set, notify the host */
	rc = mhi_dev_sm_set_ready(mhi_ctx);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
				"unable to set ready bit\n");
		goto fail;
	}

	atomic_set(&mhi_ctx->is_suspended, 0);
	atomic_set(&mhi_ctx->re_init_done, 1);
	mutex_unlock(&mhi_ctx->mhi_lock);
	if (mhi_ctx->use_edma)
		mhi_ring_init_cb(mhi_ctx);
	return rc;
fail:
	mutex_unlock(&mhi_ctx->mhi_lock);
	return rc;
}

static void mhi_dev_reinit(struct work_struct *work)
{
	struct mhi_dev *mhi_ctx = container_of(work,
				struct mhi_dev, re_init);
	enum ep_pcie_link_status link_state;
	int rc = 0;

	link_state = ep_pcie_get_linkstatus(mhi_ctx->mhi_hw_ctx->phandle);
	if (link_state == EP_PCIE_LINK_ENABLED) {
		/* PCIe link is up with BME set */
		rc = mhi_dev_resume_mmio_mhi_reinit(mhi_ctx);
		if (rc) {
			mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
				"Failed to register for events from PCIe\n");
			return;
		}
	}

	mhi_log(mhi_ctx->vf_id, MHI_MSG_VERBOSE, "Wait for PCIe linkup\n");
}

static int mhi_dev_resume_mmio_mhi_init(struct mhi_dev *mhi_ctx)
{
	struct platform_device *pdev;
	struct ep_pcie_msi_config cfg;
	int rc = 0;

	/*
	 * There could be multiple calls to this function if device gets
	 * multiple link-up events (bme irqs).
	 */
	mutex_lock(&mhi_ctx->mhi_lock);
	if (mhi_ctx->init_done) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_INFO, "mhi init already done, returning\n");
		mutex_unlock(&mhi_ctx->mhi_lock);
		return 0;
	}

	pdev = mhi_ctx->mhi_hw_ctx->pdev;

	INIT_WORK(&mhi_ctx->chdb_ctrl_work, mhi_dev_scheduler);

	mhi_ctx->pending_ring_wq = alloc_workqueue("mhi_pending_wq",
							WQ_HIGHPRI, 0);
	if (!mhi_ctx->pending_ring_wq) {
		rc = -ENOMEM;
		mutex_unlock(&mhi_ctx->mhi_lock);
		return rc;
	}

	INIT_WORK(&mhi_ctx->pending_work, mhi_dev_process_ring_pending);

	INIT_WORK(&mhi_ctx->ring_init_cb_work, mhi_dev_enable);

	INIT_WORK(&mhi_ctx->re_init, mhi_dev_reinit);

	mhi_ctx->ring_init_wq = alloc_workqueue("mhi_ring_init_cb_wq",
							WQ_HIGHPRI, 0);
	if (!mhi_ctx->ring_init_wq) {
		rc = -ENOMEM;
		mutex_unlock(&mhi_ctx->mhi_lock);
		return rc;
	}

	INIT_LIST_HEAD(&mhi_ctx->event_ring_list);
	INIT_LIST_HEAD(&mhi_ctx->process_ring_list);
	mutex_init(&mhi_ctx->mhi_event_lock);
	mutex_init(&mhi_ctx->mhi_write_test);
	if (mhi_ctx->is_mhi_pf) {
		mhi_ctx->mhi_hw_ctx->phandle = ep_pcie_get_phandle(mhi_ctx->mhi_hw_ctx->ifc_id);
		if (!mhi_ctx->mhi_hw_ctx->phandle) {
			mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
				"PCIe driver get handle failed.\n");
			mutex_unlock(&mhi_ctx->mhi_lock);
			return -EINVAL;
		}
	}

	rc = mhi_dev_get_msi_config(mhi_ctx->mhi_hw_ctx->phandle, &cfg, mhi_ctx->vf_id);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
				"Error retrieving pcie msi logic\n");
		mutex_unlock(&mhi_ctx->mhi_lock);
		return rc;
	}

	rc = mhi_dev_recover(mhi_ctx);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR, "get mhi state failed\n");
		mutex_unlock(&mhi_ctx->mhi_lock);
		return rc;
	}

	rc = mhi_init(mhi_ctx, MHI_INIT);
	if (rc) {
		mutex_unlock(&mhi_ctx->mhi_lock);
		return rc;
	}

	rc = mhi_dev_mmio_write(mhi_ctx, MHIVER, mhi_ctx->mhi_version);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
			"Failed to update the MHI version\n");
		mutex_unlock(&mhi_ctx->mhi_lock);
		return rc;
	}

	/*
	 * Invoke MHI SM when device is in RESET state.
	 * Also make sure sm-init is completed before registering for any
	 * PCIe events as in warm-reboot path, linkup event for VFs can get
	 * raised immediately once event is registered. To handle this
	 * sm_init should be completed.
	 */
	rc = mhi_dev_sm_init(mhi_ctx);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR, "Error during SM init\n");
		mutex_unlock(&mhi_ctx->mhi_lock);
		return rc;
	}

	/* set the env before setting the ready bit */
	rc = mhi_dev_mmio_set_env(mhi_ctx, MHI_ENV_VALUE);
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR, "env setting failed\n");
		mutex_unlock(&mhi_ctx->mhi_lock);
		return rc;
	}

	if (mhi_ctx->is_mhi_pf) {
		mhi_ctx->mhi_hw_ctx->event_reg.events = EP_PCIE_EVENT_PM_D3_HOT |
			EP_PCIE_EVENT_PM_D3_COLD |
			EP_PCIE_EVENT_PM_D0 |
			EP_PCIE_EVENT_PM_RST_DEAST |
			EP_PCIE_EVENT_L1SUB_TIMEOUT |
			EP_PCIE_EVENT_L1SUB_TIMEOUT_EXIT |
			EP_PCIE_EVENT_LINKDOWN |
			EP_PCIE_EVENT_LINKUP_VF;
		if (!mhi_ctx->mhi_int)
			mhi_ctx->mhi_hw_ctx->event_reg.events |= EP_PCIE_EVENT_MHI_A7;
		mhi_ctx->mhi_hw_ctx->event_reg.user = mhi_ctx->mhi_hw_ctx;
		mhi_ctx->mhi_hw_ctx->event_reg.mode = EP_PCIE_TRIGGER_CALLBACK;
		mhi_ctx->mhi_hw_ctx->event_reg.callback = mhi_dev_sm_pcie_handler;

		rc = ep_pcie_register_event(mhi_ctx->mhi_hw_ctx->phandle,
						&mhi_ctx->mhi_hw_ctx->event_reg);
	}
	if (rc) {
		mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR,
			"Failed to register for events from PCIe\n");
		mutex_unlock(&mhi_ctx->mhi_lock);
		return rc;
	}

	/* All set, notify the host */
	mhi_dev_sm_set_ready(mhi_ctx);

	if (mhi_ctx->config_iatu || mhi_ctx->mhi_int) {

		dev_info(&pdev->dev, "request mhi irq %d\n", mhi_ctx->mhi_irq);
		rc = devm_request_irq(&pdev->dev, mhi_ctx->mhi_irq, mhi_dev_isr,
			IRQF_TRIGGER_HIGH, "mhi_isr", mhi_ctx);
		if (rc) {
			mhi_log(mhi_ctx->vf_id, MHI_MSG_ERROR, "request mhi irq failed %d\n", rc);
			mutex_unlock(&mhi_ctx->mhi_lock);
			return -EINVAL;
		}

		disable_irq(mhi_ctx->mhi_irq);
	}

	mhi_ctx->init_done = true;

	if (mhi_ctx->use_mhi_dma) {
		/*
		 * In case of MHI VF, mhi_ring_int_cb() is not called as MHI DMA ready
		 * event is registered/handled by PF. Involking mhi_dev_enable()
		 * is necessary for enabling MHI IRQ.
		 */
		if (mhi_ctx->mhi_dma_ready || !mhi_ctx->is_mhi_pf)
			queue_work(mhi_ctx->ring_init_wq, &mhi_ctx->ring_init_cb_work);
	}
	mutex_unlock(&mhi_ctx->mhi_lock);

	if (mhi_ctx->use_edma)
		mhi_ring_init_cb(mhi_ctx);

	return 0;
}

void mhi_dev_resume_init_with_link_up(struct ep_pcie_notify *notify)
{
	struct mhi_dev_ctx *mhictx;
	struct mhi_dev *mhi;

	if (!notify || !notify->user) {
		mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR, "Null argument for notify\n");
		return;
	}

	mhictx = notify->user;
	mhi_dev_pcie_notify_event = notify->options;
	mhictx->notify = notify;
	mhi_log(mhi_hw_ctx->notify->vf_id, MHI_MSG_INFO, "PCIe event=0x%x, vf_id=%d\n",
			notify->options, notify->vf_id);

	mhi = mhi_get_dev_ctx(mhi_hw_ctx, mhi_hw_ctx->notify->vf_id);
	if (!mhi) {
		mhi_log(mhi_hw_ctx->notify->vf_id, MHI_MSG_INFO, "MHI vf_id:%d not initalied",
				notify->vf_id);
		return;
	}
	queue_work(mhi->pcie_event_wq, &mhi->pcie_event);
}

static void mhi_dev_pcie_handle_event(struct work_struct *work)
{
	int rc = 0;
	enum ep_pcie_link_status link_state;
	struct mhi_dev *mhi = container_of(work, struct mhi_dev, pcie_event);

	if (!mhi_dma_fun_ops && !mhi->use_edma) {
		/*
		 * Register for linkup event if it is not registered in
		 * mhi_dev_probe, to get linkup event after host wake
		 * as part of mhi_dma_provide_ops.
		 */
		if (!(mhi_hw_ctx->event_reg.events & EP_PCIE_EVENT_LINKUP)) {
			mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
				"Register for Link up event if not registered in mhi_dev_probe\n");
			mhi_hw_ctx->event_reg.events = EP_PCIE_EVENT_LINKUP;
			mhi_hw_ctx->event_reg.user = mhi_hw_ctx;
			mhi_hw_ctx->event_reg.mode = EP_PCIE_TRIGGER_CALLBACK;
			mhi_hw_ctx->event_reg.callback = mhi_dev_resume_init_with_link_up;
			mhi_hw_ctx->event_reg.options = MHI_INIT;
			rc = ep_pcie_register_event(mhi_hw_ctx->phandle, &mhi_hw_ctx->event_reg);
			if (rc)
				mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR,
						"Failed to register for events from PCIe\n");
		}
		mhi_log(mhi->vf_id, MHI_MSG_ERROR, "MHI DMA fun ops missing, defering\n");
		return;
	}

	mhi_hw_ctx->phandle = ep_pcie_get_phandle(mhi_hw_ctx->ifc_id);
	if (mhi_hw_ctx->phandle) {
		link_state = ep_pcie_get_linkstatus(mhi_hw_ctx->phandle);
		if (link_state != EP_PCIE_LINK_ENABLED) {
			mhi_log(mhi->vf_id, MHI_MSG_VERBOSE,
					"Link disabled, link state = %d\n", link_state);
			/* Wake host if the link is not enabled in mhi_dma_provide_ops call. */
			rc = ep_pcie_wakeup_host(mhi_hw_ctx->phandle, EP_PCIE_EVENT_INVALID);
			if (rc)
				mhi_log(mhi->vf_id, MHI_MSG_ERROR, "Failed to wake up Host\n");
			return;
		}
	}

	/* Get EP PCIe capabilities to check if it supports SRIOV capability */
	ep_pcie_core_get_capability(mhi_hw_ctx->phandle, &mhi_hw_ctx->ep_cap);

	/*
	 * Setup all virtual device prior to PF Mission mode
	 * completion to make sure VF's are initialized in mission
	 * mode directly, if not host assumes it in PBL state.
	 */
	if (mhi_hw_ctx->notify && !mhi_hw_ctx->notify->vf_id)
		mhi_dev_setup_virt_device(mhi_hw_ctx);

	mhi_log(mhi->vf_id, MHI_MSG_INFO,
			"MHI vf_id=0x%x\n", mhi->vf_id);

	if (mhi_dev_pcie_notify_event == MHI_INIT) {
		rc = mhi_dev_resume_mmio_mhi_init(mhi);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Error during MHI device initialization\n");
			return;
		}
	} else if (mhi_dev_pcie_notify_event == MHI_REINIT) {
		rc = mhi_dev_resume_mmio_mhi_reinit(mhi);
		if (rc) {
			mhi_log(mhi->vf_id, MHI_MSG_ERROR,
				"Error during MHI device re-initialization\n");
			return;
		}
	}
}

static void mhi_dev_setup_virt_device(struct mhi_dev_ctx *mhictx)
{
	struct platform_device *pdev = mhictx->pdev;
	struct mhi_dev *mhi_vf;
	u32 i, rc, dev_fac;
	char mhi_vf_ipc_name[11] = "mhi-vf-nn";

	for (i = 1; i <= mhictx->ep_cap.num_vfs; i++) {
		mhi_vf = mhi_get_dev_ctx(mhi_hw_ctx, i);

		if (!mhi_vf) {
			mhi_vf = devm_kzalloc(&pdev->dev,
					sizeof(struct mhi_dev), GFP_KERNEL);
			if (!mhi_vf) {
				mhi_log(i, MHI_MSG_ERROR, "Failed to allocate mhi_vf for vf=%d\n",
						i);
				return;
			}
			snprintf(mhi_vf_ipc_name, sizeof(mhi_vf_ipc_name), "mhi-vf-%d", i);
			mhictx->mhi_dev[i] = mhi_vf;
			rc = mhi_get_device_tree_data(mhictx, i);
			if (rc == 0)
				mhi_dev_basic_init(mhictx, i);
			mhi_vf->mhi_hw_ctx = mhictx;
			dev_fac = mhi_vf->mhi_num_ipc_pages_dev_fac;
			mhi_ipc_vf_log[i] = ipc_log_context_create(MHI_IPC_LOG_PAGES/dev_fac,
								mhi_vf_ipc_name, 0);
			if (mhi_ipc_vf_log[i] == NULL) {
				dev_err(&pdev->dev,
					"Failed to create IPC logging context for mhi vf = %d\n",
						i);
			}
			INIT_LIST_HEAD(&mhi_vf->client_cb_list);
			mutex_init(&mhi_vf->mhi_lock);
		}
		rc = mhi_dev_mmio_set_env(mhi_vf, MHI_ENV_VALUE);
		if (rc)
			mhi_log(i, MHI_MSG_ERROR, "env setting failed\n");
	}
}

int mhi_edma_release(void)
{
	dma_release_channel(mhi_hw_ctx->tx_dma_chan);
	mhi_hw_ctx->tx_dma_chan = NULL;
	dma_release_channel(mhi_hw_ctx->rx_dma_chan);
	mhi_hw_ctx->rx_dma_chan = NULL;
	return 0;
}

int mhi_edma_status(void)
{
	int ret = 0;

	if (dmaengine_tx_status(mhi_hw_ctx->tx_dma_chan, 0, NULL) == DMA_IN_PROGRESS)
		ret = -EBUSY;
	if (dmaengine_tx_status(mhi_hw_ctx->rx_dma_chan, 0, NULL) == DMA_IN_PROGRESS)
		ret = -EBUSY;

	return ret;
}

int mhi_edma_init(struct device *dev)
{
	if (!mhi_hw_ctx->tx_dma_chan) {
		mhi_hw_ctx->tx_dma_chan = dma_request_slave_channel(dev, "tx");
		if (IS_ERR_OR_NULL(mhi_hw_ctx->tx_dma_chan)) {
			mhi_log(mhi_hw_ctx->notify->vf_id, MHI_MSG_ERROR,
					"request for TX ch failed\n");
			return -EIO;
		}
	}
	mhi_log(mhi_hw_ctx->notify->vf_id, MHI_MSG_VERBOSE, "request for TX ch returned :%pK\n",
			mhi_hw_ctx->tx_dma_chan);

	if (!mhi_hw_ctx->rx_dma_chan) {
		mhi_hw_ctx->rx_dma_chan = dma_request_slave_channel(dev, "rx");
		if (IS_ERR_OR_NULL(mhi_hw_ctx->rx_dma_chan)) {
			mhi_log(mhi_hw_ctx->notify->vf_id, MHI_MSG_ERROR,
					"request for RX ch failed\n");
			return -EIO;
		}
	}
	mhi_log(mhi_hw_ctx->notify->vf_id, MHI_MSG_VERBOSE, "request for RX ch returned :%pK\n",
			mhi_hw_ctx->rx_dma_chan);
	return 0;
}

static int mhi_dev_probe(struct platform_device *pdev)
{
	struct mhi_dev *mhi_pf = NULL;
	int rc = 0, devfac = 0;

	if (pdev->dev.of_node) {
		rc = mhi_get_device_info(pdev);
		if (rc) {
			dev_err(&pdev->dev, "Error reading MHI Dev DT\n");
			return rc;
		}

		mhi_pf = mhi_get_dev_ctx(mhi_hw_ctx, MHI_DEV_PHY_FUN);

		if (!mhi_pf) {
			mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR,
					"mhi_pf is NULL, defering\n");
			return -EINVAL;
		}

		devfac = mhi_pf->mhi_num_ipc_pages_dev_fac;
		mhi_ipc_vf_log[MHI_DEV_PHY_FUN] = ipc_log_context_create(MHI_IPC_LOG_PAGES/devfac,
								"mhi-pf-0", 0);
		if (mhi_ipc_vf_log[MHI_DEV_PHY_FUN] == NULL) {
			dev_err(&pdev->dev,
				"Failed to create IPC logging context for mhi pf = 0\n");
		}

		mhi_ipc_err_log = ipc_log_context_create(MHI_IPC_ERR_LOG_PAGES/devfac,
								"mhi_err", 0);
		if (mhi_ipc_err_log == NULL) {
			dev_err(&pdev->dev,
				"Failed to create IPC ERR logging context\n");
		}

		mhi_ipc_default_err_log = ipc_log_context_create(MHI_IPC_ERR_LOG_PAGES/devfac,
								"mhi_default_err", 0);

		if (mhi_ipc_default_err_log == NULL) {
			dev_err(&pdev->dev,
				"Failed to create IPC DEFAULT ERR logging context\n");
		}
		/*
		 * The below list and mutex should be initialized
		 * before calling mhi_uci_init to avoid crash in
		 * mhi_register_state_cb when accessing these.
		 */
		INIT_LIST_HEAD(&mhi_pf->client_cb_list);
		mutex_init(&mhi_pf->mhi_lock);

		mhi_uci_init();
		mhi_update_state_info(mhi_pf, MHI_STATE_CONFIGURED);
	}

	mhi_hw_ctx->phandle = ep_pcie_get_phandle(mhi_hw_ctx->ifc_id);
	if (mhi_hw_ctx->phandle) {
		/* PCIe link is already up */
		mhi_dev_pcie_notify_event = MHI_INIT;
		/* Get EP PCIe capabilities to check if it supports SRIOV capability */
		ep_pcie_core_get_capability(mhi_hw_ctx->phandle, &mhi_hw_ctx->ep_cap);
		/*
		 * Setup all virtual device prior to PF Mission mode
		 * completion to make sure VF's are initialized in mission
		 * mode directly, if not host assumes it in PBL state.
		 */

		if (!mhi_pf) {
			mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR,
					"mhi_pf is NULL, defering\n");
			return -EINVAL;
		}

		mhi_dev_setup_virt_device(mhi_hw_ctx);
		queue_work(mhi_pf->pcie_event_wq, &mhi_pf->pcie_event);
	} else {
		pr_debug("Register a PCIe callback\n");
		mhi_hw_ctx->event_reg.events = EP_PCIE_EVENT_LINKUP;
		mhi_hw_ctx->event_reg.user = mhi_hw_ctx;
		mhi_hw_ctx->event_reg.mode = EP_PCIE_TRIGGER_CALLBACK;
		mhi_hw_ctx->event_reg.callback = mhi_dev_resume_init_with_link_up;
		mhi_hw_ctx->event_reg.options = MHI_INIT;

		rc = ep_pcie_register_event(mhi_hw_ctx->phandle, &mhi_hw_ctx->event_reg);
		if (rc) {
			mhi_log(MHI_DEFAULT_ERROR_LOG_ID, MHI_MSG_ERROR,
				"Failed to register for events from PCIe\n");
			return rc;
		}
	}

	return 0;
}

static int mhi_dev_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id mhi_dev_match_table[] = {
	{	.compatible = "qcom,msm-mhi-dev" },
	{}
};

static struct platform_driver mhi_dev_driver = {
	.driver		= {
		.name	= "qcom,msm-mhi-dev",
		.of_match_table = mhi_dev_match_table,
	},
	.probe		= mhi_dev_probe,
	.remove		= mhi_dev_remove,
};

static int __init mhi_dev_init(void)
{
	return platform_driver_register(&mhi_dev_driver);
}
subsys_initcall(mhi_dev_init);

static void __exit mhi_dev_exit(void)
{
	platform_driver_unregister(&mhi_dev_driver);
}
module_exit(mhi_dev_exit);

MODULE_DESCRIPTION("MHI device driver");
MODULE_LICENSE("GPL");
