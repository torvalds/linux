/*
 * Copyright (C) 2017 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Broadcom SBA RAID Driver
 *
 * The Broadcom stream buffer accelerator (SBA) provides offloading
 * capabilities for RAID operations. The SBA offload engine is accessible
 * via Broadcom SoC specific ring manager. Two or more offload engines
 * can share same Broadcom SoC specific ring manager due to this Broadcom
 * SoC specific ring manager driver is implemented as a mailbox controller
 * driver and offload engine drivers are implemented as mallbox clients.
 *
 * Typically, Broadcom SoC specific ring manager will implement larger
 * number of hardware rings over one or more SBA hardware devices. By
 * design, the internal buffer size of SBA hardware device is limited
 * but all offload operations supported by SBA can be broken down into
 * multiple small size requests and executed parallely on multiple SBA
 * hardware devices for achieving high through-put.
 *
 * The Broadcom SBA RAID driver does not require any register programming
 * except submitting request to SBA hardware device via mailbox channels.
 * This driver implements a DMA device with one DMA channel using a set
 * of mailbox channels provided by Broadcom SoC specific ring manager
 * driver. To exploit parallelism (as described above), all DMA request
 * coming to SBA RAID DMA channel are broken down to smaller requests
 * and submitted to multiple mailbox channels in round-robin fashion.
 * For having more SBA DMA channels, we can create more SBA device nodes
 * in Broadcom SoC specific DTS based on number of hardware rings supported
 * by Broadcom SoC ring manager.
 */

#include <linux/bitops.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/list.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox/brcm-message.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/raid/pq.h>

#include "dmaengine.h"

/* SBA command related defines */
#define SBA_TYPE_SHIFT					48
#define SBA_TYPE_MASK					GENMASK(1, 0)
#define SBA_TYPE_A					0x0
#define SBA_TYPE_B					0x2
#define SBA_TYPE_C					0x3
#define SBA_USER_DEF_SHIFT				32
#define SBA_USER_DEF_MASK				GENMASK(15, 0)
#define SBA_R_MDATA_SHIFT				24
#define SBA_R_MDATA_MASK				GENMASK(7, 0)
#define SBA_C_MDATA_MS_SHIFT				18
#define SBA_C_MDATA_MS_MASK				GENMASK(1, 0)
#define SBA_INT_SHIFT					17
#define SBA_INT_MASK					BIT(0)
#define SBA_RESP_SHIFT					16
#define SBA_RESP_MASK					BIT(0)
#define SBA_C_MDATA_SHIFT				8
#define SBA_C_MDATA_MASK				GENMASK(7, 0)
#define SBA_C_MDATA_BNUMx_SHIFT(__bnum)			(2 * (__bnum))
#define SBA_C_MDATA_BNUMx_MASK				GENMASK(1, 0)
#define SBA_C_MDATA_DNUM_SHIFT				5
#define SBA_C_MDATA_DNUM_MASK				GENMASK(4, 0)
#define SBA_C_MDATA_LS(__v)				((__v) & 0xff)
#define SBA_C_MDATA_MS(__v)				(((__v) >> 8) & 0x3)
#define SBA_CMD_SHIFT					0
#define SBA_CMD_MASK					GENMASK(3, 0)
#define SBA_CMD_ZERO_BUFFER				0x4
#define SBA_CMD_ZERO_ALL_BUFFERS			0x8
#define SBA_CMD_LOAD_BUFFER				0x9
#define SBA_CMD_XOR					0xa
#define SBA_CMD_GALOIS_XOR				0xb
#define SBA_CMD_WRITE_BUFFER				0xc
#define SBA_CMD_GALOIS					0xe

/* Driver helper macros */
#define to_sba_request(tx)		\
	container_of(tx, struct sba_request, tx)
#define to_sba_device(dchan)		\
	container_of(dchan, struct sba_device, dma_chan)

enum sba_request_state {
	SBA_REQUEST_STATE_FREE = 1,
	SBA_REQUEST_STATE_ALLOCED = 2,
	SBA_REQUEST_STATE_PENDING = 3,
	SBA_REQUEST_STATE_ACTIVE = 4,
	SBA_REQUEST_STATE_RECEIVED = 5,
	SBA_REQUEST_STATE_COMPLETED = 6,
	SBA_REQUEST_STATE_ABORTED = 7,
};

struct sba_request {
	/* Global state */
	struct list_head node;
	struct sba_device *sba;
	enum sba_request_state state;
	bool fence;
	/* Chained requests management */
	struct sba_request *first;
	struct list_head next;
	unsigned int next_count;
	atomic_t next_pending_count;
	/* BRCM message data */
	void *resp;
	dma_addr_t resp_dma;
	struct brcm_sba_command *cmds;
	struct brcm_message msg;
	struct dma_async_tx_descriptor tx;
};

enum sba_version {
	SBA_VER_1 = 0,
	SBA_VER_2
};

struct sba_device {
	/* Underlying device */
	struct device *dev;
	/* DT configuration parameters */
	enum sba_version ver;
	/* Derived configuration parameters */
	u32 max_req;
	u32 hw_buf_size;
	u32 hw_resp_size;
	u32 max_pq_coefs;
	u32 max_pq_srcs;
	u32 max_cmd_per_req;
	u32 max_xor_srcs;
	u32 max_resp_pool_size;
	u32 max_cmds_pool_size;
	/* Maibox client and Mailbox channels */
	struct mbox_client client;
	int mchans_count;
	atomic_t mchans_current;
	struct mbox_chan **mchans;
	struct device *mbox_dev;
	/* DMA device and DMA channel */
	struct dma_device dma_dev;
	struct dma_chan dma_chan;
	/* DMA channel resources */
	void *resp_base;
	dma_addr_t resp_dma_base;
	void *cmds_base;
	dma_addr_t cmds_dma_base;
	spinlock_t reqs_lock;
	struct sba_request *reqs;
	bool reqs_fence;
	struct list_head reqs_alloc_list;
	struct list_head reqs_pending_list;
	struct list_head reqs_active_list;
	struct list_head reqs_received_list;
	struct list_head reqs_completed_list;
	struct list_head reqs_aborted_list;
	struct list_head reqs_free_list;
	int reqs_free_count;
};

/* ====== SBA command helper routines ===== */

static inline u64 __pure sba_cmd_enc(u64 cmd, u32 val, u32 shift, u32 mask)
{
	cmd &= ~((u64)mask << shift);
	cmd |= ((u64)(val & mask) << shift);
	return cmd;
}

static inline u32 __pure sba_cmd_load_c_mdata(u32 b0)
{
	return b0 & SBA_C_MDATA_BNUMx_MASK;
}

static inline u32 __pure sba_cmd_write_c_mdata(u32 b0)
{
	return b0 & SBA_C_MDATA_BNUMx_MASK;
}

static inline u32 __pure sba_cmd_xor_c_mdata(u32 b1, u32 b0)
{
	return (b0 & SBA_C_MDATA_BNUMx_MASK) |
	       ((b1 & SBA_C_MDATA_BNUMx_MASK) << SBA_C_MDATA_BNUMx_SHIFT(1));
}

static inline u32 __pure sba_cmd_pq_c_mdata(u32 d, u32 b1, u32 b0)
{
	return (b0 & SBA_C_MDATA_BNUMx_MASK) |
	       ((b1 & SBA_C_MDATA_BNUMx_MASK) << SBA_C_MDATA_BNUMx_SHIFT(1)) |
	       ((d & SBA_C_MDATA_DNUM_MASK) << SBA_C_MDATA_DNUM_SHIFT);
}

/* ====== Channel resource management routines ===== */

static struct sba_request *sba_alloc_request(struct sba_device *sba)
{
	unsigned long flags;
	struct sba_request *req = NULL;

	spin_lock_irqsave(&sba->reqs_lock, flags);

	req = list_first_entry_or_null(&sba->reqs_free_list,
				       struct sba_request, node);
	if (req) {
		list_move_tail(&req->node, &sba->reqs_alloc_list);
		req->state = SBA_REQUEST_STATE_ALLOCED;
		req->fence = false;
		req->first = req;
		INIT_LIST_HEAD(&req->next);
		req->next_count = 1;
		atomic_set(&req->next_pending_count, 1);

		sba->reqs_free_count--;

		dma_async_tx_descriptor_init(&req->tx, &sba->dma_chan);
	}

	spin_unlock_irqrestore(&sba->reqs_lock, flags);

	return req;
}

/* Note: Must be called with sba->reqs_lock held */
static void _sba_pending_request(struct sba_device *sba,
				 struct sba_request *req)
{
	lockdep_assert_held(&sba->reqs_lock);
	req->state = SBA_REQUEST_STATE_PENDING;
	list_move_tail(&req->node, &sba->reqs_pending_list);
	if (list_empty(&sba->reqs_active_list))
		sba->reqs_fence = false;
}

/* Note: Must be called with sba->reqs_lock held */
static bool _sba_active_request(struct sba_device *sba,
				struct sba_request *req)
{
	lockdep_assert_held(&sba->reqs_lock);
	if (list_empty(&sba->reqs_active_list))
		sba->reqs_fence = false;
	if (sba->reqs_fence)
		return false;
	req->state = SBA_REQUEST_STATE_ACTIVE;
	list_move_tail(&req->node, &sba->reqs_active_list);
	if (req->fence)
		sba->reqs_fence = true;
	return true;
}

/* Note: Must be called with sba->reqs_lock held */
static void _sba_abort_request(struct sba_device *sba,
			       struct sba_request *req)
{
	lockdep_assert_held(&sba->reqs_lock);
	req->state = SBA_REQUEST_STATE_ABORTED;
	list_move_tail(&req->node, &sba->reqs_aborted_list);
	if (list_empty(&sba->reqs_active_list))
		sba->reqs_fence = false;
}

/* Note: Must be called with sba->reqs_lock held */
static void _sba_free_request(struct sba_device *sba,
			      struct sba_request *req)
{
	lockdep_assert_held(&sba->reqs_lock);
	req->state = SBA_REQUEST_STATE_FREE;
	list_move_tail(&req->node, &sba->reqs_free_list);
	if (list_empty(&sba->reqs_active_list))
		sba->reqs_fence = false;
	sba->reqs_free_count++;
}

static void sba_received_request(struct sba_request *req)
{
	unsigned long flags;
	struct sba_device *sba = req->sba;

	spin_lock_irqsave(&sba->reqs_lock, flags);
	req->state = SBA_REQUEST_STATE_RECEIVED;
	list_move_tail(&req->node, &sba->reqs_received_list);
	spin_unlock_irqrestore(&sba->reqs_lock, flags);
}

static void sba_complete_chained_requests(struct sba_request *req)
{
	unsigned long flags;
	struct sba_request *nreq;
	struct sba_device *sba = req->sba;

	spin_lock_irqsave(&sba->reqs_lock, flags);

	req->state = SBA_REQUEST_STATE_COMPLETED;
	list_move_tail(&req->node, &sba->reqs_completed_list);
	list_for_each_entry(nreq, &req->next, next) {
		nreq->state = SBA_REQUEST_STATE_COMPLETED;
		list_move_tail(&nreq->node, &sba->reqs_completed_list);
	}
	if (list_empty(&sba->reqs_active_list))
		sba->reqs_fence = false;

	spin_unlock_irqrestore(&sba->reqs_lock, flags);
}

static void sba_free_chained_requests(struct sba_request *req)
{
	unsigned long flags;
	struct sba_request *nreq;
	struct sba_device *sba = req->sba;

	spin_lock_irqsave(&sba->reqs_lock, flags);

	_sba_free_request(sba, req);
	list_for_each_entry(nreq, &req->next, next)
		_sba_free_request(sba, nreq);

	spin_unlock_irqrestore(&sba->reqs_lock, flags);
}

static void sba_chain_request(struct sba_request *first,
			      struct sba_request *req)
{
	unsigned long flags;
	struct sba_device *sba = req->sba;

	spin_lock_irqsave(&sba->reqs_lock, flags);

	list_add_tail(&req->next, &first->next);
	req->first = first;
	first->next_count++;
	atomic_set(&first->next_pending_count, first->next_count);

	spin_unlock_irqrestore(&sba->reqs_lock, flags);
}

static void sba_cleanup_nonpending_requests(struct sba_device *sba)
{
	unsigned long flags;
	struct sba_request *req, *req1;

	spin_lock_irqsave(&sba->reqs_lock, flags);

	/* Freeup all alloced request */
	list_for_each_entry_safe(req, req1, &sba->reqs_alloc_list, node)
		_sba_free_request(sba, req);

	/* Freeup all received request */
	list_for_each_entry_safe(req, req1, &sba->reqs_received_list, node)
		_sba_free_request(sba, req);

	/* Freeup all completed request */
	list_for_each_entry_safe(req, req1, &sba->reqs_completed_list, node)
		_sba_free_request(sba, req);

	/* Set all active requests as aborted */
	list_for_each_entry_safe(req, req1, &sba->reqs_active_list, node)
		_sba_abort_request(sba, req);

	/*
	 * Note: We expect that aborted request will be eventually
	 * freed by sba_receive_message()
	 */

	spin_unlock_irqrestore(&sba->reqs_lock, flags);
}

static void sba_cleanup_pending_requests(struct sba_device *sba)
{
	unsigned long flags;
	struct sba_request *req, *req1;

	spin_lock_irqsave(&sba->reqs_lock, flags);

	/* Freeup all pending request */
	list_for_each_entry_safe(req, req1, &sba->reqs_pending_list, node)
		_sba_free_request(sba, req);

	spin_unlock_irqrestore(&sba->reqs_lock, flags);
}

/* ====== DMAENGINE callbacks ===== */

static void sba_free_chan_resources(struct dma_chan *dchan)
{
	/*
	 * Channel resources are pre-alloced so we just free-up
	 * whatever we can so that we can re-use pre-alloced
	 * channel resources next time.
	 */
	sba_cleanup_nonpending_requests(to_sba_device(dchan));
}

static int sba_device_terminate_all(struct dma_chan *dchan)
{
	/* Cleanup all pending requests */
	sba_cleanup_pending_requests(to_sba_device(dchan));

	return 0;
}

static int sba_send_mbox_request(struct sba_device *sba,
				 struct sba_request *req)
{
	int mchans_idx, ret = 0;

	/* Select mailbox channel in round-robin fashion */
	mchans_idx = atomic_inc_return(&sba->mchans_current);
	mchans_idx = mchans_idx % sba->mchans_count;

	/* Send message for the request */
	req->msg.error = 0;
	ret = mbox_send_message(sba->mchans[mchans_idx], &req->msg);
	if (ret < 0) {
		dev_err(sba->dev, "send message failed with error %d", ret);
		return ret;
	}
	ret = req->msg.error;
	if (ret < 0) {
		dev_err(sba->dev, "message error %d", ret);
		return ret;
	}

	return 0;
}

static void sba_issue_pending(struct dma_chan *dchan)
{
	int ret;
	unsigned long flags;
	struct sba_request *req, *req1;
	struct sba_device *sba = to_sba_device(dchan);

	spin_lock_irqsave(&sba->reqs_lock, flags);

	/* Process all pending request */
	list_for_each_entry_safe(req, req1, &sba->reqs_pending_list, node) {
		/* Try to make request active */
		if (!_sba_active_request(sba, req))
			break;

		/* Send request to mailbox channel */
		spin_unlock_irqrestore(&sba->reqs_lock, flags);
		ret = sba_send_mbox_request(sba, req);
		spin_lock_irqsave(&sba->reqs_lock, flags);

		/* If something went wrong then keep request pending */
		if (ret < 0) {
			_sba_pending_request(sba, req);
			break;
		}
	}

	spin_unlock_irqrestore(&sba->reqs_lock, flags);
}

static dma_cookie_t sba_tx_submit(struct dma_async_tx_descriptor *tx)
{
	unsigned long flags;
	dma_cookie_t cookie;
	struct sba_device *sba;
	struct sba_request *req, *nreq;

	if (unlikely(!tx))
		return -EINVAL;

	sba = to_sba_device(tx->chan);
	req = to_sba_request(tx);

	/* Assign cookie and mark all chained requests pending */
	spin_lock_irqsave(&sba->reqs_lock, flags);
	cookie = dma_cookie_assign(tx);
	_sba_pending_request(sba, req);
	list_for_each_entry(nreq, &req->next, next)
		_sba_pending_request(sba, nreq);
	spin_unlock_irqrestore(&sba->reqs_lock, flags);

	return cookie;
}

static enum dma_status sba_tx_status(struct dma_chan *dchan,
				     dma_cookie_t cookie,
				     struct dma_tx_state *txstate)
{
	int mchan_idx;
	enum dma_status ret;
	struct sba_device *sba = to_sba_device(dchan);

	for (mchan_idx = 0; mchan_idx < sba->mchans_count; mchan_idx++)
		mbox_client_peek_data(sba->mchans[mchan_idx]);

	ret = dma_cookie_status(dchan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	return dma_cookie_status(dchan, cookie, txstate);
}

static void sba_fillup_interrupt_msg(struct sba_request *req,
				     struct brcm_sba_command *cmds,
				     struct brcm_message *msg)
{
	u64 cmd;
	u32 c_mdata;
	struct brcm_sba_command *cmdsp = cmds;

	/* Type-B command to load dummy data into buf0 */
	cmd = sba_cmd_enc(0x0, SBA_TYPE_B,
			  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
	cmd = sba_cmd_enc(cmd, req->sba->hw_resp_size,
			  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
	c_mdata = sba_cmd_load_c_mdata(0);
	cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
			  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
	cmd = sba_cmd_enc(cmd, SBA_CMD_LOAD_BUFFER,
			  SBA_CMD_SHIFT, SBA_CMD_MASK);
	cmdsp->cmd = cmd;
	*cmdsp->cmd_dma = cpu_to_le64(cmd);
	cmdsp->flags = BRCM_SBA_CMD_TYPE_B;
	cmdsp->data = req->resp_dma;
	cmdsp->data_len = req->sba->hw_resp_size;
	cmdsp++;

	/* Type-A command to write buf0 to dummy location */
	cmd = sba_cmd_enc(0x0, SBA_TYPE_A,
			  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
	cmd = sba_cmd_enc(cmd, req->sba->hw_resp_size,
			  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
	cmd = sba_cmd_enc(cmd, 0x1,
			  SBA_RESP_SHIFT, SBA_RESP_MASK);
	c_mdata = sba_cmd_write_c_mdata(0);
	cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
			  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
	cmd = sba_cmd_enc(cmd, SBA_CMD_WRITE_BUFFER,
			  SBA_CMD_SHIFT, SBA_CMD_MASK);
	cmdsp->cmd = cmd;
	*cmdsp->cmd_dma = cpu_to_le64(cmd);
	cmdsp->flags = BRCM_SBA_CMD_TYPE_A;
	if (req->sba->hw_resp_size) {
		cmdsp->flags |= BRCM_SBA_CMD_HAS_RESP;
		cmdsp->resp = req->resp_dma;
		cmdsp->resp_len = req->sba->hw_resp_size;
	}
	cmdsp->flags |= BRCM_SBA_CMD_HAS_OUTPUT;
	cmdsp->data = req->resp_dma;
	cmdsp->data_len = req->sba->hw_resp_size;
	cmdsp++;

	/* Fillup brcm_message */
	msg->type = BRCM_MESSAGE_SBA;
	msg->sba.cmds = cmds;
	msg->sba.cmds_count = cmdsp - cmds;
	msg->ctx = req;
	msg->error = 0;
}

static struct dma_async_tx_descriptor *
sba_prep_dma_interrupt(struct dma_chan *dchan, unsigned long flags)
{
	struct sba_request *req = NULL;
	struct sba_device *sba = to_sba_device(dchan);

	/* Alloc new request */
	req = sba_alloc_request(sba);
	if (!req)
		return NULL;

	/*
	 * Force fence so that no requests are submitted
	 * until DMA callback for this request is invoked.
	 */
	req->fence = true;

	/* Fillup request message */
	sba_fillup_interrupt_msg(req, req->cmds, &req->msg);

	/* Init async_tx descriptor */
	req->tx.flags = flags;
	req->tx.cookie = -EBUSY;

	return &req->tx;
}

static void sba_fillup_memcpy_msg(struct sba_request *req,
				  struct brcm_sba_command *cmds,
				  struct brcm_message *msg,
				  dma_addr_t msg_offset, size_t msg_len,
				  dma_addr_t dst, dma_addr_t src)
{
	u64 cmd;
	u32 c_mdata;
	struct brcm_sba_command *cmdsp = cmds;

	/* Type-B command to load data into buf0 */
	cmd = sba_cmd_enc(0x0, SBA_TYPE_B,
			  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
	cmd = sba_cmd_enc(cmd, msg_len,
			  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
	c_mdata = sba_cmd_load_c_mdata(0);
	cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
			  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
	cmd = sba_cmd_enc(cmd, SBA_CMD_LOAD_BUFFER,
			  SBA_CMD_SHIFT, SBA_CMD_MASK);
	cmdsp->cmd = cmd;
	*cmdsp->cmd_dma = cpu_to_le64(cmd);
	cmdsp->flags = BRCM_SBA_CMD_TYPE_B;
	cmdsp->data = src + msg_offset;
	cmdsp->data_len = msg_len;
	cmdsp++;

	/* Type-A command to write buf0 */
	cmd = sba_cmd_enc(0x0, SBA_TYPE_A,
			  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
	cmd = sba_cmd_enc(cmd, msg_len,
			  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
	cmd = sba_cmd_enc(cmd, 0x1,
			  SBA_RESP_SHIFT, SBA_RESP_MASK);
	c_mdata = sba_cmd_write_c_mdata(0);
	cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
			  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
	cmd = sba_cmd_enc(cmd, SBA_CMD_WRITE_BUFFER,
			  SBA_CMD_SHIFT, SBA_CMD_MASK);
	cmdsp->cmd = cmd;
	*cmdsp->cmd_dma = cpu_to_le64(cmd);
	cmdsp->flags = BRCM_SBA_CMD_TYPE_A;
	if (req->sba->hw_resp_size) {
		cmdsp->flags |= BRCM_SBA_CMD_HAS_RESP;
		cmdsp->resp = req->resp_dma;
		cmdsp->resp_len = req->sba->hw_resp_size;
	}
	cmdsp->flags |= BRCM_SBA_CMD_HAS_OUTPUT;
	cmdsp->data = dst + msg_offset;
	cmdsp->data_len = msg_len;
	cmdsp++;

	/* Fillup brcm_message */
	msg->type = BRCM_MESSAGE_SBA;
	msg->sba.cmds = cmds;
	msg->sba.cmds_count = cmdsp - cmds;
	msg->ctx = req;
	msg->error = 0;
}

static struct sba_request *
sba_prep_dma_memcpy_req(struct sba_device *sba,
			dma_addr_t off, dma_addr_t dst, dma_addr_t src,
			size_t len, unsigned long flags)
{
	struct sba_request *req = NULL;

	/* Alloc new request */
	req = sba_alloc_request(sba);
	if (!req)
		return NULL;
	req->fence = (flags & DMA_PREP_FENCE) ? true : false;

	/* Fillup request message */
	sba_fillup_memcpy_msg(req, req->cmds, &req->msg,
			      off, len, dst, src);

	/* Init async_tx descriptor */
	req->tx.flags = flags;
	req->tx.cookie = -EBUSY;

	return req;
}

static struct dma_async_tx_descriptor *
sba_prep_dma_memcpy(struct dma_chan *dchan, dma_addr_t dst, dma_addr_t src,
		    size_t len, unsigned long flags)
{
	size_t req_len;
	dma_addr_t off = 0;
	struct sba_device *sba = to_sba_device(dchan);
	struct sba_request *first = NULL, *req;

	/* Create chained requests where each request is upto hw_buf_size */
	while (len) {
		req_len = (len < sba->hw_buf_size) ? len : sba->hw_buf_size;

		req = sba_prep_dma_memcpy_req(sba, off, dst, src,
					      req_len, flags);
		if (!req) {
			if (first)
				sba_free_chained_requests(first);
			return NULL;
		}

		if (first)
			sba_chain_request(first, req);
		else
			first = req;

		off += req_len;
		len -= req_len;
	}

	return (first) ? &first->tx : NULL;
}

static void sba_fillup_xor_msg(struct sba_request *req,
				struct brcm_sba_command *cmds,
				struct brcm_message *msg,
				dma_addr_t msg_offset, size_t msg_len,
				dma_addr_t dst, dma_addr_t *src, u32 src_cnt)
{
	u64 cmd;
	u32 c_mdata;
	unsigned int i;
	struct brcm_sba_command *cmdsp = cmds;

	/* Type-B command to load data into buf0 */
	cmd = sba_cmd_enc(0x0, SBA_TYPE_B,
			  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
	cmd = sba_cmd_enc(cmd, msg_len,
			  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
	c_mdata = sba_cmd_load_c_mdata(0);
	cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
			  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
	cmd = sba_cmd_enc(cmd, SBA_CMD_LOAD_BUFFER,
			  SBA_CMD_SHIFT, SBA_CMD_MASK);
	cmdsp->cmd = cmd;
	*cmdsp->cmd_dma = cpu_to_le64(cmd);
	cmdsp->flags = BRCM_SBA_CMD_TYPE_B;
	cmdsp->data = src[0] + msg_offset;
	cmdsp->data_len = msg_len;
	cmdsp++;

	/* Type-B commands to xor data with buf0 and put it back in buf0 */
	for (i = 1; i < src_cnt; i++) {
		cmd = sba_cmd_enc(0x0, SBA_TYPE_B,
				  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
		cmd = sba_cmd_enc(cmd, msg_len,
				  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
		c_mdata = sba_cmd_xor_c_mdata(0, 0);
		cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
				  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
		cmd = sba_cmd_enc(cmd, SBA_CMD_XOR,
				  SBA_CMD_SHIFT, SBA_CMD_MASK);
		cmdsp->cmd = cmd;
		*cmdsp->cmd_dma = cpu_to_le64(cmd);
		cmdsp->flags = BRCM_SBA_CMD_TYPE_B;
		cmdsp->data = src[i] + msg_offset;
		cmdsp->data_len = msg_len;
		cmdsp++;
	}

	/* Type-A command to write buf0 */
	cmd = sba_cmd_enc(0x0, SBA_TYPE_A,
			  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
	cmd = sba_cmd_enc(cmd, msg_len,
			  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
	cmd = sba_cmd_enc(cmd, 0x1,
			  SBA_RESP_SHIFT, SBA_RESP_MASK);
	c_mdata = sba_cmd_write_c_mdata(0);
	cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
			  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
	cmd = sba_cmd_enc(cmd, SBA_CMD_WRITE_BUFFER,
			  SBA_CMD_SHIFT, SBA_CMD_MASK);
	cmdsp->cmd = cmd;
	*cmdsp->cmd_dma = cpu_to_le64(cmd);
	cmdsp->flags = BRCM_SBA_CMD_TYPE_A;
	if (req->sba->hw_resp_size) {
		cmdsp->flags |= BRCM_SBA_CMD_HAS_RESP;
		cmdsp->resp = req->resp_dma;
		cmdsp->resp_len = req->sba->hw_resp_size;
	}
	cmdsp->flags |= BRCM_SBA_CMD_HAS_OUTPUT;
	cmdsp->data = dst + msg_offset;
	cmdsp->data_len = msg_len;
	cmdsp++;

	/* Fillup brcm_message */
	msg->type = BRCM_MESSAGE_SBA;
	msg->sba.cmds = cmds;
	msg->sba.cmds_count = cmdsp - cmds;
	msg->ctx = req;
	msg->error = 0;
}

struct sba_request *
sba_prep_dma_xor_req(struct sba_device *sba,
		     dma_addr_t off, dma_addr_t dst, dma_addr_t *src,
		     u32 src_cnt, size_t len, unsigned long flags)
{
	struct sba_request *req = NULL;

	/* Alloc new request */
	req = sba_alloc_request(sba);
	if (!req)
		return NULL;
	req->fence = (flags & DMA_PREP_FENCE) ? true : false;

	/* Fillup request message */
	sba_fillup_xor_msg(req, req->cmds, &req->msg,
			   off, len, dst, src, src_cnt);

	/* Init async_tx descriptor */
	req->tx.flags = flags;
	req->tx.cookie = -EBUSY;

	return req;
}

static struct dma_async_tx_descriptor *
sba_prep_dma_xor(struct dma_chan *dchan, dma_addr_t dst, dma_addr_t *src,
		 u32 src_cnt, size_t len, unsigned long flags)
{
	size_t req_len;
	dma_addr_t off = 0;
	struct sba_device *sba = to_sba_device(dchan);
	struct sba_request *first = NULL, *req;

	/* Sanity checks */
	if (unlikely(src_cnt > sba->max_xor_srcs))
		return NULL;

	/* Create chained requests where each request is upto hw_buf_size */
	while (len) {
		req_len = (len < sba->hw_buf_size) ? len : sba->hw_buf_size;

		req = sba_prep_dma_xor_req(sba, off, dst, src, src_cnt,
					   req_len, flags);
		if (!req) {
			if (first)
				sba_free_chained_requests(first);
			return NULL;
		}

		if (first)
			sba_chain_request(first, req);
		else
			first = req;

		off += req_len;
		len -= req_len;
	}

	return (first) ? &first->tx : NULL;
}

static void sba_fillup_pq_msg(struct sba_request *req,
				bool pq_continue,
				struct brcm_sba_command *cmds,
				struct brcm_message *msg,
				dma_addr_t msg_offset, size_t msg_len,
				dma_addr_t *dst_p, dma_addr_t *dst_q,
				const u8 *scf, dma_addr_t *src, u32 src_cnt)
{
	u64 cmd;
	u32 c_mdata;
	unsigned int i;
	struct brcm_sba_command *cmdsp = cmds;

	if (pq_continue) {
		/* Type-B command to load old P into buf0 */
		if (dst_p) {
			cmd = sba_cmd_enc(0x0, SBA_TYPE_B,
				SBA_TYPE_SHIFT, SBA_TYPE_MASK);
			cmd = sba_cmd_enc(cmd, msg_len,
				SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
			c_mdata = sba_cmd_load_c_mdata(0);
			cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
				SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
			cmd = sba_cmd_enc(cmd, SBA_CMD_LOAD_BUFFER,
				SBA_CMD_SHIFT, SBA_CMD_MASK);
			cmdsp->cmd = cmd;
			*cmdsp->cmd_dma = cpu_to_le64(cmd);
			cmdsp->flags = BRCM_SBA_CMD_TYPE_B;
			cmdsp->data = *dst_p + msg_offset;
			cmdsp->data_len = msg_len;
			cmdsp++;
		}

		/* Type-B command to load old Q into buf1 */
		if (dst_q) {
			cmd = sba_cmd_enc(0x0, SBA_TYPE_B,
				SBA_TYPE_SHIFT, SBA_TYPE_MASK);
			cmd = sba_cmd_enc(cmd, msg_len,
				SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
			c_mdata = sba_cmd_load_c_mdata(1);
			cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
				SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
			cmd = sba_cmd_enc(cmd, SBA_CMD_LOAD_BUFFER,
				SBA_CMD_SHIFT, SBA_CMD_MASK);
			cmdsp->cmd = cmd;
			*cmdsp->cmd_dma = cpu_to_le64(cmd);
			cmdsp->flags = BRCM_SBA_CMD_TYPE_B;
			cmdsp->data = *dst_q + msg_offset;
			cmdsp->data_len = msg_len;
			cmdsp++;
		}
	} else {
		/* Type-A command to zero all buffers */
		cmd = sba_cmd_enc(0x0, SBA_TYPE_A,
				  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
		cmd = sba_cmd_enc(cmd, msg_len,
				  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
		cmd = sba_cmd_enc(cmd, SBA_CMD_ZERO_ALL_BUFFERS,
				  SBA_CMD_SHIFT, SBA_CMD_MASK);
		cmdsp->cmd = cmd;
		*cmdsp->cmd_dma = cpu_to_le64(cmd);
		cmdsp->flags = BRCM_SBA_CMD_TYPE_A;
		cmdsp++;
	}

	/* Type-B commands for generate P onto buf0 and Q onto buf1 */
	for (i = 0; i < src_cnt; i++) {
		cmd = sba_cmd_enc(0x0, SBA_TYPE_B,
				  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
		cmd = sba_cmd_enc(cmd, msg_len,
				  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
		c_mdata = sba_cmd_pq_c_mdata(raid6_gflog[scf[i]], 1, 0);
		cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
				  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
		cmd = sba_cmd_enc(cmd, SBA_C_MDATA_MS(c_mdata),
				  SBA_C_MDATA_MS_SHIFT, SBA_C_MDATA_MS_MASK);
		cmd = sba_cmd_enc(cmd, SBA_CMD_GALOIS_XOR,
				  SBA_CMD_SHIFT, SBA_CMD_MASK);
		cmdsp->cmd = cmd;
		*cmdsp->cmd_dma = cpu_to_le64(cmd);
		cmdsp->flags = BRCM_SBA_CMD_TYPE_B;
		cmdsp->data = src[i] + msg_offset;
		cmdsp->data_len = msg_len;
		cmdsp++;
	}

	/* Type-A command to write buf0 */
	if (dst_p) {
		cmd = sba_cmd_enc(0x0, SBA_TYPE_A,
				  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
		cmd = sba_cmd_enc(cmd, msg_len,
				  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
		cmd = sba_cmd_enc(cmd, 0x1,
				  SBA_RESP_SHIFT, SBA_RESP_MASK);
		c_mdata = sba_cmd_write_c_mdata(0);
		cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
				  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
		cmd = sba_cmd_enc(cmd, SBA_CMD_WRITE_BUFFER,
				  SBA_CMD_SHIFT, SBA_CMD_MASK);
		cmdsp->cmd = cmd;
		*cmdsp->cmd_dma = cpu_to_le64(cmd);
		cmdsp->flags = BRCM_SBA_CMD_TYPE_A;
		if (req->sba->hw_resp_size) {
			cmdsp->flags |= BRCM_SBA_CMD_HAS_RESP;
			cmdsp->resp = req->resp_dma;
			cmdsp->resp_len = req->sba->hw_resp_size;
		}
		cmdsp->flags |= BRCM_SBA_CMD_HAS_OUTPUT;
		cmdsp->data = *dst_p + msg_offset;
		cmdsp->data_len = msg_len;
		cmdsp++;
	}

	/* Type-A command to write buf1 */
	if (dst_q) {
		cmd = sba_cmd_enc(0x0, SBA_TYPE_A,
				  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
		cmd = sba_cmd_enc(cmd, msg_len,
				  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
		cmd = sba_cmd_enc(cmd, 0x1,
				  SBA_RESP_SHIFT, SBA_RESP_MASK);
		c_mdata = sba_cmd_write_c_mdata(1);
		cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
				  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
		cmd = sba_cmd_enc(cmd, SBA_CMD_WRITE_BUFFER,
				  SBA_CMD_SHIFT, SBA_CMD_MASK);
		cmdsp->cmd = cmd;
		*cmdsp->cmd_dma = cpu_to_le64(cmd);
		cmdsp->flags = BRCM_SBA_CMD_TYPE_A;
		if (req->sba->hw_resp_size) {
			cmdsp->flags |= BRCM_SBA_CMD_HAS_RESP;
			cmdsp->resp = req->resp_dma;
			cmdsp->resp_len = req->sba->hw_resp_size;
		}
		cmdsp->flags |= BRCM_SBA_CMD_HAS_OUTPUT;
		cmdsp->data = *dst_q + msg_offset;
		cmdsp->data_len = msg_len;
		cmdsp++;
	}

	/* Fillup brcm_message */
	msg->type = BRCM_MESSAGE_SBA;
	msg->sba.cmds = cmds;
	msg->sba.cmds_count = cmdsp - cmds;
	msg->ctx = req;
	msg->error = 0;
}

struct sba_request *
sba_prep_dma_pq_req(struct sba_device *sba, dma_addr_t off,
		    dma_addr_t *dst_p, dma_addr_t *dst_q, dma_addr_t *src,
		    u32 src_cnt, const u8 *scf, size_t len, unsigned long flags)
{
	struct sba_request *req = NULL;

	/* Alloc new request */
	req = sba_alloc_request(sba);
	if (!req)
		return NULL;
	req->fence = (flags & DMA_PREP_FENCE) ? true : false;

	/* Fillup request messages */
	sba_fillup_pq_msg(req, dmaf_continue(flags),
			  req->cmds, &req->msg,
			  off, len, dst_p, dst_q, scf, src, src_cnt);

	/* Init async_tx descriptor */
	req->tx.flags = flags;
	req->tx.cookie = -EBUSY;

	return req;
}

static void sba_fillup_pq_single_msg(struct sba_request *req,
				bool pq_continue,
				struct brcm_sba_command *cmds,
				struct brcm_message *msg,
				dma_addr_t msg_offset, size_t msg_len,
				dma_addr_t *dst_p, dma_addr_t *dst_q,
				dma_addr_t src, u8 scf)
{
	u64 cmd;
	u32 c_mdata;
	u8 pos, dpos = raid6_gflog[scf];
	struct brcm_sba_command *cmdsp = cmds;

	if (!dst_p)
		goto skip_p;

	if (pq_continue) {
		/* Type-B command to load old P into buf0 */
		cmd = sba_cmd_enc(0x0, SBA_TYPE_B,
				  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
		cmd = sba_cmd_enc(cmd, msg_len,
				  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
		c_mdata = sba_cmd_load_c_mdata(0);
		cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
				  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
		cmd = sba_cmd_enc(cmd, SBA_CMD_LOAD_BUFFER,
				  SBA_CMD_SHIFT, SBA_CMD_MASK);
		cmdsp->cmd = cmd;
		*cmdsp->cmd_dma = cpu_to_le64(cmd);
		cmdsp->flags = BRCM_SBA_CMD_TYPE_B;
		cmdsp->data = *dst_p + msg_offset;
		cmdsp->data_len = msg_len;
		cmdsp++;

		/*
		 * Type-B commands to xor data with buf0 and put it
		 * back in buf0
		 */
		cmd = sba_cmd_enc(0x0, SBA_TYPE_B,
				  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
		cmd = sba_cmd_enc(cmd, msg_len,
				  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
		c_mdata = sba_cmd_xor_c_mdata(0, 0);
		cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
				  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
		cmd = sba_cmd_enc(cmd, SBA_CMD_XOR,
				  SBA_CMD_SHIFT, SBA_CMD_MASK);
		cmdsp->cmd = cmd;
		*cmdsp->cmd_dma = cpu_to_le64(cmd);
		cmdsp->flags = BRCM_SBA_CMD_TYPE_B;
		cmdsp->data = src + msg_offset;
		cmdsp->data_len = msg_len;
		cmdsp++;
	} else {
		/* Type-B command to load old P into buf0 */
		cmd = sba_cmd_enc(0x0, SBA_TYPE_B,
				  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
		cmd = sba_cmd_enc(cmd, msg_len,
				  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
		c_mdata = sba_cmd_load_c_mdata(0);
		cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
				  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
		cmd = sba_cmd_enc(cmd, SBA_CMD_LOAD_BUFFER,
				  SBA_CMD_SHIFT, SBA_CMD_MASK);
		cmdsp->cmd = cmd;
		*cmdsp->cmd_dma = cpu_to_le64(cmd);
		cmdsp->flags = BRCM_SBA_CMD_TYPE_B;
		cmdsp->data = src + msg_offset;
		cmdsp->data_len = msg_len;
		cmdsp++;
	}

	/* Type-A command to write buf0 */
	cmd = sba_cmd_enc(0x0, SBA_TYPE_A,
			  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
	cmd = sba_cmd_enc(cmd, msg_len,
			  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
	cmd = sba_cmd_enc(cmd, 0x1,
			  SBA_RESP_SHIFT, SBA_RESP_MASK);
	c_mdata = sba_cmd_write_c_mdata(0);
	cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
			  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
	cmd = sba_cmd_enc(cmd, SBA_CMD_WRITE_BUFFER,
			  SBA_CMD_SHIFT, SBA_CMD_MASK);
	cmdsp->cmd = cmd;
	*cmdsp->cmd_dma = cpu_to_le64(cmd);
	cmdsp->flags = BRCM_SBA_CMD_TYPE_A;
	if (req->sba->hw_resp_size) {
		cmdsp->flags |= BRCM_SBA_CMD_HAS_RESP;
		cmdsp->resp = req->resp_dma;
		cmdsp->resp_len = req->sba->hw_resp_size;
	}
	cmdsp->flags |= BRCM_SBA_CMD_HAS_OUTPUT;
	cmdsp->data = *dst_p + msg_offset;
	cmdsp->data_len = msg_len;
	cmdsp++;

skip_p:
	if (!dst_q)
		goto skip_q;

	/* Type-A command to zero all buffers */
	cmd = sba_cmd_enc(0x0, SBA_TYPE_A,
			  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
	cmd = sba_cmd_enc(cmd, msg_len,
			  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
	cmd = sba_cmd_enc(cmd, SBA_CMD_ZERO_ALL_BUFFERS,
			  SBA_CMD_SHIFT, SBA_CMD_MASK);
	cmdsp->cmd = cmd;
	*cmdsp->cmd_dma = cpu_to_le64(cmd);
	cmdsp->flags = BRCM_SBA_CMD_TYPE_A;
	cmdsp++;

	if (dpos == 255)
		goto skip_q_computation;
	pos = (dpos < req->sba->max_pq_coefs) ?
		dpos : (req->sba->max_pq_coefs - 1);

	/*
	 * Type-B command to generate initial Q from data
	 * and store output into buf0
	 */
	cmd = sba_cmd_enc(0x0, SBA_TYPE_B,
			  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
	cmd = sba_cmd_enc(cmd, msg_len,
			  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
	c_mdata = sba_cmd_pq_c_mdata(pos, 0, 0);
	cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
			  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
	cmd = sba_cmd_enc(cmd, SBA_C_MDATA_MS(c_mdata),
			  SBA_C_MDATA_MS_SHIFT, SBA_C_MDATA_MS_MASK);
	cmd = sba_cmd_enc(cmd, SBA_CMD_GALOIS,
			  SBA_CMD_SHIFT, SBA_CMD_MASK);
	cmdsp->cmd = cmd;
	*cmdsp->cmd_dma = cpu_to_le64(cmd);
	cmdsp->flags = BRCM_SBA_CMD_TYPE_B;
	cmdsp->data = src + msg_offset;
	cmdsp->data_len = msg_len;
	cmdsp++;

	dpos -= pos;

	/* Multiple Type-A command to generate final Q */
	while (dpos) {
		pos = (dpos < req->sba->max_pq_coefs) ?
			dpos : (req->sba->max_pq_coefs - 1);

		/*
		 * Type-A command to generate Q with buf0 and
		 * buf1 store result in buf0
		 */
		cmd = sba_cmd_enc(0x0, SBA_TYPE_A,
				  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
		cmd = sba_cmd_enc(cmd, msg_len,
				  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
		c_mdata = sba_cmd_pq_c_mdata(pos, 0, 1);
		cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
				  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
		cmd = sba_cmd_enc(cmd, SBA_C_MDATA_MS(c_mdata),
				  SBA_C_MDATA_MS_SHIFT, SBA_C_MDATA_MS_MASK);
		cmd = sba_cmd_enc(cmd, SBA_CMD_GALOIS,
				  SBA_CMD_SHIFT, SBA_CMD_MASK);
		cmdsp->cmd = cmd;
		*cmdsp->cmd_dma = cpu_to_le64(cmd);
		cmdsp->flags = BRCM_SBA_CMD_TYPE_A;
		cmdsp++;

		dpos -= pos;
	}

skip_q_computation:
	if (pq_continue) {
		/*
		 * Type-B command to XOR previous output with
		 * buf0 and write it into buf0
		 */
		cmd = sba_cmd_enc(0x0, SBA_TYPE_B,
				  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
		cmd = sba_cmd_enc(cmd, msg_len,
				  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
		c_mdata = sba_cmd_xor_c_mdata(0, 0);
		cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
				  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
		cmd = sba_cmd_enc(cmd, SBA_CMD_XOR,
				  SBA_CMD_SHIFT, SBA_CMD_MASK);
		cmdsp->cmd = cmd;
		*cmdsp->cmd_dma = cpu_to_le64(cmd);
		cmdsp->flags = BRCM_SBA_CMD_TYPE_B;
		cmdsp->data = *dst_q + msg_offset;
		cmdsp->data_len = msg_len;
		cmdsp++;
	}

	/* Type-A command to write buf0 */
	cmd = sba_cmd_enc(0x0, SBA_TYPE_A,
			  SBA_TYPE_SHIFT, SBA_TYPE_MASK);
	cmd = sba_cmd_enc(cmd, msg_len,
			  SBA_USER_DEF_SHIFT, SBA_USER_DEF_MASK);
	cmd = sba_cmd_enc(cmd, 0x1,
			  SBA_RESP_SHIFT, SBA_RESP_MASK);
	c_mdata = sba_cmd_write_c_mdata(0);
	cmd = sba_cmd_enc(cmd, SBA_C_MDATA_LS(c_mdata),
			  SBA_C_MDATA_SHIFT, SBA_C_MDATA_MASK);
	cmd = sba_cmd_enc(cmd, SBA_CMD_WRITE_BUFFER,
			  SBA_CMD_SHIFT, SBA_CMD_MASK);
	cmdsp->cmd = cmd;
	*cmdsp->cmd_dma = cpu_to_le64(cmd);
	cmdsp->flags = BRCM_SBA_CMD_TYPE_A;
	if (req->sba->hw_resp_size) {
		cmdsp->flags |= BRCM_SBA_CMD_HAS_RESP;
		cmdsp->resp = req->resp_dma;
		cmdsp->resp_len = req->sba->hw_resp_size;
	}
	cmdsp->flags |= BRCM_SBA_CMD_HAS_OUTPUT;
	cmdsp->data = *dst_q + msg_offset;
	cmdsp->data_len = msg_len;
	cmdsp++;

skip_q:
	/* Fillup brcm_message */
	msg->type = BRCM_MESSAGE_SBA;
	msg->sba.cmds = cmds;
	msg->sba.cmds_count = cmdsp - cmds;
	msg->ctx = req;
	msg->error = 0;
}

struct sba_request *
sba_prep_dma_pq_single_req(struct sba_device *sba, dma_addr_t off,
			   dma_addr_t *dst_p, dma_addr_t *dst_q,
			   dma_addr_t src, u8 scf, size_t len,
			   unsigned long flags)
{
	struct sba_request *req = NULL;

	/* Alloc new request */
	req = sba_alloc_request(sba);
	if (!req)
		return NULL;
	req->fence = (flags & DMA_PREP_FENCE) ? true : false;

	/* Fillup request messages */
	sba_fillup_pq_single_msg(req,  dmaf_continue(flags),
				 req->cmds, &req->msg, off, len,
				 dst_p, dst_q, src, scf);

	/* Init async_tx descriptor */
	req->tx.flags = flags;
	req->tx.cookie = -EBUSY;

	return req;
}

static struct dma_async_tx_descriptor *
sba_prep_dma_pq(struct dma_chan *dchan, dma_addr_t *dst, dma_addr_t *src,
		u32 src_cnt, const u8 *scf, size_t len, unsigned long flags)
{
	u32 i, dst_q_index;
	size_t req_len;
	bool slow = false;
	dma_addr_t off = 0;
	dma_addr_t *dst_p = NULL, *dst_q = NULL;
	struct sba_device *sba = to_sba_device(dchan);
	struct sba_request *first = NULL, *req;

	/* Sanity checks */
	if (unlikely(src_cnt > sba->max_pq_srcs))
		return NULL;
	for (i = 0; i < src_cnt; i++)
		if (sba->max_pq_coefs <= raid6_gflog[scf[i]])
			slow = true;

	/* Figure-out P and Q destination addresses */
	if (!(flags & DMA_PREP_PQ_DISABLE_P))
		dst_p = &dst[0];
	if (!(flags & DMA_PREP_PQ_DISABLE_Q))
		dst_q = &dst[1];

	/* Create chained requests where each request is upto hw_buf_size */
	while (len) {
		req_len = (len < sba->hw_buf_size) ? len : sba->hw_buf_size;

		if (slow) {
			dst_q_index = src_cnt;

			if (dst_q) {
				for (i = 0; i < src_cnt; i++) {
					if (*dst_q == src[i]) {
						dst_q_index = i;
						break;
					}
				}
			}

			if (dst_q_index < src_cnt) {
				i = dst_q_index;
				req = sba_prep_dma_pq_single_req(sba,
					off, dst_p, dst_q, src[i], scf[i],
					req_len, flags | DMA_PREP_FENCE);
				if (!req)
					goto fail;

				if (first)
					sba_chain_request(first, req);
				else
					first = req;

				flags |= DMA_PREP_CONTINUE;
			}

			for (i = 0; i < src_cnt; i++) {
				if (dst_q_index == i)
					continue;

				req = sba_prep_dma_pq_single_req(sba,
					off, dst_p, dst_q, src[i], scf[i],
					req_len, flags | DMA_PREP_FENCE);
				if (!req)
					goto fail;

				if (first)
					sba_chain_request(first, req);
				else
					first = req;

				flags |= DMA_PREP_CONTINUE;
			}
		} else {
			req = sba_prep_dma_pq_req(sba, off,
						  dst_p, dst_q, src, src_cnt,
						  scf, req_len, flags);
			if (!req)
				goto fail;

			if (first)
				sba_chain_request(first, req);
			else
				first = req;
		}

		off += req_len;
		len -= req_len;
	}

	return (first) ? &first->tx : NULL;

fail:
	if (first)
		sba_free_chained_requests(first);
	return NULL;
}

/* ====== Mailbox callbacks ===== */

static void sba_dma_tx_actions(struct sba_request *req)
{
	struct dma_async_tx_descriptor *tx = &req->tx;

	WARN_ON(tx->cookie < 0);

	if (tx->cookie > 0) {
		dma_cookie_complete(tx);

		/*
		 * Call the callback (must not sleep or submit new
		 * operations to this channel)
		 */
		if (tx->callback)
			tx->callback(tx->callback_param);

		dma_descriptor_unmap(tx);
	}

	/* Run dependent operations */
	dma_run_dependencies(tx);

	/* If waiting for 'ack' then move to completed list */
	if (!async_tx_test_ack(&req->tx))
		sba_complete_chained_requests(req);
	else
		sba_free_chained_requests(req);
}

static void sba_receive_message(struct mbox_client *cl, void *msg)
{
	unsigned long flags;
	struct brcm_message *m = msg;
	struct sba_request *req = m->ctx, *req1;
	struct sba_device *sba = req->sba;

	/* Error count if message has error */
	if (m->error < 0)
		dev_err(sba->dev, "%s got message with error %d",
			dma_chan_name(&sba->dma_chan), m->error);

	/* Mark request as received */
	sba_received_request(req);

	/* Wait for all chained requests to be completed */
	if (atomic_dec_return(&req->first->next_pending_count))
		goto done;

	/* Point to first request */
	req = req->first;

	/* Update request */
	if (req->state == SBA_REQUEST_STATE_RECEIVED)
		sba_dma_tx_actions(req);
	else
		sba_free_chained_requests(req);

	spin_lock_irqsave(&sba->reqs_lock, flags);

	/* Re-check all completed request waiting for 'ack' */
	list_for_each_entry_safe(req, req1, &sba->reqs_completed_list, node) {
		spin_unlock_irqrestore(&sba->reqs_lock, flags);
		sba_dma_tx_actions(req);
		spin_lock_irqsave(&sba->reqs_lock, flags);
	}

	spin_unlock_irqrestore(&sba->reqs_lock, flags);

done:
	/* Try to submit pending request */
	sba_issue_pending(&sba->dma_chan);
}

/* ====== Platform driver routines ===== */

static int sba_prealloc_channel_resources(struct sba_device *sba)
{
	int i, j, p, ret = 0;
	struct sba_request *req = NULL;

	sba->resp_base = dma_alloc_coherent(sba->dma_dev.dev,
					    sba->max_resp_pool_size,
					    &sba->resp_dma_base, GFP_KERNEL);
	if (!sba->resp_base)
		return -ENOMEM;

	sba->cmds_base = dma_alloc_coherent(sba->dma_dev.dev,
					    sba->max_cmds_pool_size,
					    &sba->cmds_dma_base, GFP_KERNEL);
	if (!sba->cmds_base) {
		ret = -ENOMEM;
		goto fail_free_resp_pool;
	}

	spin_lock_init(&sba->reqs_lock);
	sba->reqs_fence = false;
	INIT_LIST_HEAD(&sba->reqs_alloc_list);
	INIT_LIST_HEAD(&sba->reqs_pending_list);
	INIT_LIST_HEAD(&sba->reqs_active_list);
	INIT_LIST_HEAD(&sba->reqs_received_list);
	INIT_LIST_HEAD(&sba->reqs_completed_list);
	INIT_LIST_HEAD(&sba->reqs_aborted_list);
	INIT_LIST_HEAD(&sba->reqs_free_list);

	sba->reqs = devm_kcalloc(sba->dev, sba->max_req,
				 sizeof(*req), GFP_KERNEL);
	if (!sba->reqs) {
		ret = -ENOMEM;
		goto fail_free_cmds_pool;
	}

	for (i = 0, p = 0; i < sba->max_req; i++) {
		req = &sba->reqs[i];
		INIT_LIST_HEAD(&req->node);
		req->sba = sba;
		req->state = SBA_REQUEST_STATE_FREE;
		INIT_LIST_HEAD(&req->next);
		req->next_count = 1;
		atomic_set(&req->next_pending_count, 0);
		req->fence = false;
		req->resp = sba->resp_base + p;
		req->resp_dma = sba->resp_dma_base + p;
		p += sba->hw_resp_size;
		req->cmds = devm_kcalloc(sba->dev, sba->max_cmd_per_req,
					 sizeof(*req->cmds), GFP_KERNEL);
		if (!req->cmds) {
			ret = -ENOMEM;
			goto fail_free_cmds_pool;
		}
		for (j = 0; j < sba->max_cmd_per_req; j++) {
			req->cmds[j].cmd = 0;
			req->cmds[j].cmd_dma = sba->cmds_base +
				(i * sba->max_cmd_per_req + j) * sizeof(u64);
			req->cmds[j].cmd_dma_addr = sba->cmds_dma_base +
				(i * sba->max_cmd_per_req + j) * sizeof(u64);
			req->cmds[j].flags = 0;
		}
		memset(&req->msg, 0, sizeof(req->msg));
		dma_async_tx_descriptor_init(&req->tx, &sba->dma_chan);
		req->tx.tx_submit = sba_tx_submit;
		req->tx.phys = req->resp_dma;
		list_add_tail(&req->node, &sba->reqs_free_list);
	}

	sba->reqs_free_count = sba->max_req;

	return 0;

fail_free_cmds_pool:
	dma_free_coherent(sba->dma_dev.dev,
			  sba->max_cmds_pool_size,
			  sba->cmds_base, sba->cmds_dma_base);
fail_free_resp_pool:
	dma_free_coherent(sba->dma_dev.dev,
			  sba->max_resp_pool_size,
			  sba->resp_base, sba->resp_dma_base);
	return ret;
}

static void sba_freeup_channel_resources(struct sba_device *sba)
{
	dmaengine_terminate_all(&sba->dma_chan);
	dma_free_coherent(sba->dma_dev.dev, sba->max_cmds_pool_size,
			  sba->cmds_base, sba->cmds_dma_base);
	dma_free_coherent(sba->dma_dev.dev, sba->max_resp_pool_size,
			  sba->resp_base, sba->resp_dma_base);
	sba->resp_base = NULL;
	sba->resp_dma_base = 0;
}

static int sba_async_register(struct sba_device *sba)
{
	int ret;
	struct dma_device *dma_dev = &sba->dma_dev;

	/* Initialize DMA channel cookie */
	sba->dma_chan.device = dma_dev;
	dma_cookie_init(&sba->dma_chan);

	/* Initialize DMA device capability mask */
	dma_cap_zero(dma_dev->cap_mask);
	dma_cap_set(DMA_INTERRUPT, dma_dev->cap_mask);
	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
	dma_cap_set(DMA_XOR, dma_dev->cap_mask);
	dma_cap_set(DMA_PQ, dma_dev->cap_mask);

	/*
	 * Set mailbox channel device as the base device of
	 * our dma_device because the actual memory accesses
	 * will be done by mailbox controller
	 */
	dma_dev->dev = sba->mbox_dev;

	/* Set base prep routines */
	dma_dev->device_free_chan_resources = sba_free_chan_resources;
	dma_dev->device_terminate_all = sba_device_terminate_all;
	dma_dev->device_issue_pending = sba_issue_pending;
	dma_dev->device_tx_status = sba_tx_status;

	/* Set interrupt routine */
	if (dma_has_cap(DMA_INTERRUPT, dma_dev->cap_mask))
		dma_dev->device_prep_dma_interrupt = sba_prep_dma_interrupt;

	/* Set memcpy routine */
	if (dma_has_cap(DMA_MEMCPY, dma_dev->cap_mask))
		dma_dev->device_prep_dma_memcpy = sba_prep_dma_memcpy;

	/* Set xor routine and capability */
	if (dma_has_cap(DMA_XOR, dma_dev->cap_mask)) {
		dma_dev->device_prep_dma_xor = sba_prep_dma_xor;
		dma_dev->max_xor = sba->max_xor_srcs;
	}

	/* Set pq routine and capability */
	if (dma_has_cap(DMA_PQ, dma_dev->cap_mask)) {
		dma_dev->device_prep_dma_pq = sba_prep_dma_pq;
		dma_set_maxpq(dma_dev, sba->max_pq_srcs, 0);
	}

	/* Initialize DMA device channel list */
	INIT_LIST_HEAD(&dma_dev->channels);
	list_add_tail(&sba->dma_chan.device_node, &dma_dev->channels);

	/* Register with Linux async DMA framework*/
	ret = dma_async_device_register(dma_dev);
	if (ret) {
		dev_err(sba->dev, "async device register error %d", ret);
		return ret;
	}

	dev_info(sba->dev, "%s capabilities: %s%s%s%s\n",
	dma_chan_name(&sba->dma_chan),
	dma_has_cap(DMA_INTERRUPT, dma_dev->cap_mask) ? "interrupt " : "",
	dma_has_cap(DMA_MEMCPY, dma_dev->cap_mask) ? "memcpy " : "",
	dma_has_cap(DMA_XOR, dma_dev->cap_mask) ? "xor " : "",
	dma_has_cap(DMA_PQ, dma_dev->cap_mask) ? "pq " : "");

	return 0;
}

static int sba_probe(struct platform_device *pdev)
{
	int i, ret = 0, mchans_count;
	struct sba_device *sba;
	struct platform_device *mbox_pdev;
	struct of_phandle_args args;

	/* Allocate main SBA struct */
	sba = devm_kzalloc(&pdev->dev, sizeof(*sba), GFP_KERNEL);
	if (!sba)
		return -ENOMEM;

	sba->dev = &pdev->dev;
	platform_set_drvdata(pdev, sba);

	/* Determine SBA version from DT compatible string */
	if (of_device_is_compatible(sba->dev->of_node, "brcm,iproc-sba"))
		sba->ver = SBA_VER_1;
	else if (of_device_is_compatible(sba->dev->of_node,
					 "brcm,iproc-sba-v2"))
		sba->ver = SBA_VER_2;
	else
		return -ENODEV;

	/* Derived Configuration parameters */
	switch (sba->ver) {
	case SBA_VER_1:
		sba->max_req = 1024;
		sba->hw_buf_size = 4096;
		sba->hw_resp_size = 8;
		sba->max_pq_coefs = 6;
		sba->max_pq_srcs = 6;
		break;
	case SBA_VER_2:
		sba->max_req = 1024;
		sba->hw_buf_size = 4096;
		sba->hw_resp_size = 8;
		sba->max_pq_coefs = 30;
		/*
		 * We can support max_pq_srcs == max_pq_coefs because
		 * we are limited by number of SBA commands that we can
		 * fit in one message for underlying ring manager HW.
		 */
		sba->max_pq_srcs = 12;
		break;
	default:
		return -EINVAL;
	}
	sba->max_cmd_per_req = sba->max_pq_srcs + 3;
	sba->max_xor_srcs = sba->max_cmd_per_req - 1;
	sba->max_resp_pool_size = sba->max_req * sba->hw_resp_size;
	sba->max_cmds_pool_size = sba->max_req *
				  sba->max_cmd_per_req * sizeof(u64);

	/* Setup mailbox client */
	sba->client.dev			= &pdev->dev;
	sba->client.rx_callback		= sba_receive_message;
	sba->client.tx_block		= false;
	sba->client.knows_txdone	= false;
	sba->client.tx_tout		= 0;

	/* Number of channels equals number of mailbox channels */
	ret = of_count_phandle_with_args(pdev->dev.of_node,
					 "mboxes", "#mbox-cells");
	if (ret <= 0)
		return -ENODEV;
	mchans_count = ret;
	sba->mchans_count = 0;
	atomic_set(&sba->mchans_current, 0);

	/* Allocate mailbox channel array */
	sba->mchans = devm_kcalloc(&pdev->dev, sba->mchans_count,
				   sizeof(*sba->mchans), GFP_KERNEL);
	if (!sba->mchans)
		return -ENOMEM;

	/* Request mailbox channels */
	for (i = 0; i < mchans_count; i++) {
		sba->mchans[i] = mbox_request_channel(&sba->client, i);
		if (IS_ERR(sba->mchans[i])) {
			ret = PTR_ERR(sba->mchans[i]);
			goto fail_free_mchans;
		}
		sba->mchans_count++;
	}

	/* Find-out underlying mailbox device */
	ret = of_parse_phandle_with_args(pdev->dev.of_node,
					 "mboxes", "#mbox-cells", 0, &args);
	if (ret)
		goto fail_free_mchans;
	mbox_pdev = of_find_device_by_node(args.np);
	of_node_put(args.np);
	if (!mbox_pdev) {
		ret = -ENODEV;
		goto fail_free_mchans;
	}
	sba->mbox_dev = &mbox_pdev->dev;

	/* All mailbox channels should be of same ring manager device */
	for (i = 1; i < mchans_count; i++) {
		ret = of_parse_phandle_with_args(pdev->dev.of_node,
					 "mboxes", "#mbox-cells", i, &args);
		if (ret)
			goto fail_free_mchans;
		mbox_pdev = of_find_device_by_node(args.np);
		of_node_put(args.np);
		if (sba->mbox_dev != &mbox_pdev->dev) {
			ret = -EINVAL;
			goto fail_free_mchans;
		}
	}

	/* Register DMA device with linux async framework */
	ret = sba_async_register(sba);
	if (ret)
		goto fail_free_mchans;

	/* Prealloc channel resource */
	ret = sba_prealloc_channel_resources(sba);
	if (ret)
		goto fail_async_dev_unreg;

	/* Print device info */
	dev_info(sba->dev, "%s using SBAv%d and %d mailbox channels",
		 dma_chan_name(&sba->dma_chan), sba->ver+1,
		 sba->mchans_count);

	return 0;

fail_async_dev_unreg:
	dma_async_device_unregister(&sba->dma_dev);
fail_free_mchans:
	for (i = 0; i < sba->mchans_count; i++)
		mbox_free_channel(sba->mchans[i]);
	return ret;
}

static int sba_remove(struct platform_device *pdev)
{
	int i;
	struct sba_device *sba = platform_get_drvdata(pdev);

	sba_freeup_channel_resources(sba);

	dma_async_device_unregister(&sba->dma_dev);

	for (i = 0; i < sba->mchans_count; i++)
		mbox_free_channel(sba->mchans[i]);

	return 0;
}

static const struct of_device_id sba_of_match[] = {
	{ .compatible = "brcm,iproc-sba", },
	{ .compatible = "brcm,iproc-sba-v2", },
	{},
};
MODULE_DEVICE_TABLE(of, sba_of_match);

static struct platform_driver sba_driver = {
	.probe = sba_probe,
	.remove = sba_remove,
	.driver = {
		.name = "bcm-sba-raid",
		.of_match_table = sba_of_match,
	},
};
module_platform_driver(sba_driver);

MODULE_DESCRIPTION("Broadcom SBA RAID driver");
MODULE_AUTHOR("Anup Patel <anup.patel@broadcom.com>");
MODULE_LICENSE("GPL v2");
