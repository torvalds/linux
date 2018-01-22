/*
 * Copyright (C) 2017 Broadcom
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
 * This driver implements a DMA device with one DMA channel using a single
 * mailbox channel provided by Broadcom SoC specific ring manager driver.
 * For having more SBA DMA channels, we can create more SBA device nodes
 * in Broadcom SoC specific DTS based on number of hardware rings supported
 * by Broadcom SoC ring manager.
 */

#include <linux/bitops.h>
#include <linux/debugfs.h>
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

/* ====== Driver macros and defines ===== */

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

#define SBA_MAX_REQ_PER_MBOX_CHANNEL			8192
#define SBA_MAX_MSG_SEND_PER_MBOX_CHANNEL		8

/* Driver helper macros */
#define to_sba_request(tx)		\
	container_of(tx, struct sba_request, tx)
#define to_sba_device(dchan)		\
	container_of(dchan, struct sba_device, dma_chan)

/* ===== Driver data structures ===== */

enum sba_request_flags {
	SBA_REQUEST_STATE_FREE		= 0x001,
	SBA_REQUEST_STATE_ALLOCED	= 0x002,
	SBA_REQUEST_STATE_PENDING	= 0x004,
	SBA_REQUEST_STATE_ACTIVE	= 0x008,
	SBA_REQUEST_STATE_ABORTED	= 0x010,
	SBA_REQUEST_STATE_MASK		= 0x0ff,
	SBA_REQUEST_FENCE		= 0x100,
};

struct sba_request {
	/* Global state */
	struct list_head node;
	struct sba_device *sba;
	u32 flags;
	/* Chained requests management */
	struct sba_request *first;
	struct list_head next;
	atomic_t next_pending_count;
	/* BRCM message data */
	struct brcm_message msg;
	struct dma_async_tx_descriptor tx;
	/* SBA commands */
	struct brcm_sba_command cmds[0];
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
	struct mbox_chan *mchan;
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
	bool reqs_fence;
	struct list_head reqs_alloc_list;
	struct list_head reqs_pending_list;
	struct list_head reqs_active_list;
	struct list_head reqs_aborted_list;
	struct list_head reqs_free_list;
	/* DebugFS directory entries */
	struct dentry *root;
	struct dentry *stats;
};

/* ====== Command helper routines ===== */

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

/* ====== General helper routines ===== */

static struct sba_request *sba_alloc_request(struct sba_device *sba)
{
	bool found = false;
	unsigned long flags;
	struct sba_request *req = NULL;

	spin_lock_irqsave(&sba->reqs_lock, flags);
	list_for_each_entry(req, &sba->reqs_free_list, node) {
		if (async_tx_test_ack(&req->tx)) {
			list_move_tail(&req->node, &sba->reqs_alloc_list);
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&sba->reqs_lock, flags);

	if (!found) {
		/*
		 * We have no more free requests so, we peek
		 * mailbox channels hoping few active requests
		 * would have completed which will create more
		 * room for new requests.
		 */
		mbox_client_peek_data(sba->mchan);
		return NULL;
	}

	req->flags = SBA_REQUEST_STATE_ALLOCED;
	req->first = req;
	INIT_LIST_HEAD(&req->next);
	atomic_set(&req->next_pending_count, 1);

	dma_async_tx_descriptor_init(&req->tx, &sba->dma_chan);
	async_tx_ack(&req->tx);

	return req;
}

/* Note: Must be called with sba->reqs_lock held */
static void _sba_pending_request(struct sba_device *sba,
				 struct sba_request *req)
{
	lockdep_assert_held(&sba->reqs_lock);
	req->flags &= ~SBA_REQUEST_STATE_MASK;
	req->flags |= SBA_REQUEST_STATE_PENDING;
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
	req->flags &= ~SBA_REQUEST_STATE_MASK;
	req->flags |= SBA_REQUEST_STATE_ACTIVE;
	list_move_tail(&req->node, &sba->reqs_active_list);
	if (req->flags & SBA_REQUEST_FENCE)
		sba->reqs_fence = true;
	return true;
}

/* Note: Must be called with sba->reqs_lock held */
static void _sba_abort_request(struct sba_device *sba,
			       struct sba_request *req)
{
	lockdep_assert_held(&sba->reqs_lock);
	req->flags &= ~SBA_REQUEST_STATE_MASK;
	req->flags |= SBA_REQUEST_STATE_ABORTED;
	list_move_tail(&req->node, &sba->reqs_aborted_list);
	if (list_empty(&sba->reqs_active_list))
		sba->reqs_fence = false;
}

/* Note: Must be called with sba->reqs_lock held */
static void _sba_free_request(struct sba_device *sba,
			      struct sba_request *req)
{
	lockdep_assert_held(&sba->reqs_lock);
	req->flags &= ~SBA_REQUEST_STATE_MASK;
	req->flags |= SBA_REQUEST_STATE_FREE;
	list_move_tail(&req->node, &sba->reqs_free_list);
	if (list_empty(&sba->reqs_active_list))
		sba->reqs_fence = false;
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
	atomic_inc(&first->next_pending_count);

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

static int sba_send_mbox_request(struct sba_device *sba,
				 struct sba_request *req)
{
	int ret = 0;

	/* Send message for the request */
	req->msg.error = 0;
	ret = mbox_send_message(sba->mchan, &req->msg);
	if (ret < 0) {
		dev_err(sba->dev, "send message failed with error %d", ret);
		return ret;
	}

	/* Check error returned by mailbox controller */
	ret = req->msg.error;
	if (ret < 0) {
		dev_err(sba->dev, "message error %d", ret);
	}

	/* Signal txdone for mailbox channel */
	mbox_client_txdone(sba->mchan, ret);

	return ret;
}

/* Note: Must be called with sba->reqs_lock held */
static void _sba_process_pending_requests(struct sba_device *sba)
{
	int ret;
	u32 count;
	struct sba_request *req;

	/* Process few pending requests */
	count = SBA_MAX_MSG_SEND_PER_MBOX_CHANNEL;
	while (!list_empty(&sba->reqs_pending_list) && count) {
		/* Get the first pending request */
		req = list_first_entry(&sba->reqs_pending_list,
				       struct sba_request, node);

		/* Try to make request active */
		if (!_sba_active_request(sba, req))
			break;

		/* Send request to mailbox channel */
		ret = sba_send_mbox_request(sba, req);
		if (ret < 0) {
			_sba_pending_request(sba, req);
			break;
		}

		count--;
	}
}

static void sba_process_received_request(struct sba_device *sba,
					 struct sba_request *req)
{
	unsigned long flags;
	struct dma_async_tx_descriptor *tx;
	struct sba_request *nreq, *first = req->first;

	/* Process only after all chained requests are received */
	if (!atomic_dec_return(&first->next_pending_count)) {
		tx = &first->tx;

		WARN_ON(tx->cookie < 0);
		if (tx->cookie > 0) {
			spin_lock_irqsave(&sba->reqs_lock, flags);
			dma_cookie_complete(tx);
			spin_unlock_irqrestore(&sba->reqs_lock, flags);
			dmaengine_desc_get_callback_invoke(tx, NULL);
			dma_descriptor_unmap(tx);
			tx->callback = NULL;
			tx->callback_result = NULL;
		}

		dma_run_dependencies(tx);

		spin_lock_irqsave(&sba->reqs_lock, flags);

		/* Free all requests chained to first request */
		list_for_each_entry(nreq, &first->next, next)
			_sba_free_request(sba, nreq);
		INIT_LIST_HEAD(&first->next);

		/* Free the first request */
		_sba_free_request(sba, first);

		/* Process pending requests */
		_sba_process_pending_requests(sba);

		spin_unlock_irqrestore(&sba->reqs_lock, flags);
	}
}

static void sba_write_stats_in_seqfile(struct sba_device *sba,
				       struct seq_file *file)
{
	unsigned long flags;
	struct sba_request *req;
	u32 free_count = 0, alloced_count = 0;
	u32 pending_count = 0, active_count = 0, aborted_count = 0;

	spin_lock_irqsave(&sba->reqs_lock, flags);

	list_for_each_entry(req, &sba->reqs_free_list, node)
		if (async_tx_test_ack(&req->tx))
			free_count++;

	list_for_each_entry(req, &sba->reqs_alloc_list, node)
		alloced_count++;

	list_for_each_entry(req, &sba->reqs_pending_list, node)
		pending_count++;

	list_for_each_entry(req, &sba->reqs_active_list, node)
		active_count++;

	list_for_each_entry(req, &sba->reqs_aborted_list, node)
		aborted_count++;

	spin_unlock_irqrestore(&sba->reqs_lock, flags);

	seq_printf(file, "maximum requests   = %d\n", sba->max_req);
	seq_printf(file, "free requests      = %d\n", free_count);
	seq_printf(file, "alloced requests   = %d\n", alloced_count);
	seq_printf(file, "pending requests   = %d\n", pending_count);
	seq_printf(file, "active requests    = %d\n", active_count);
	seq_printf(file, "aborted requests   = %d\n", aborted_count);
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

static void sba_issue_pending(struct dma_chan *dchan)
{
	unsigned long flags;
	struct sba_device *sba = to_sba_device(dchan);

	/* Process pending requests */
	spin_lock_irqsave(&sba->reqs_lock, flags);
	_sba_process_pending_requests(sba);
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
	enum dma_status ret;
	struct sba_device *sba = to_sba_device(dchan);

	ret = dma_cookie_status(dchan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	mbox_client_peek_data(sba->mchan);

	return dma_cookie_status(dchan, cookie, txstate);
}

static void sba_fillup_interrupt_msg(struct sba_request *req,
				     struct brcm_sba_command *cmds,
				     struct brcm_message *msg)
{
	u64 cmd;
	u32 c_mdata;
	dma_addr_t resp_dma = req->tx.phys;
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
	cmdsp->data = resp_dma;
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
		cmdsp->resp = resp_dma;
		cmdsp->resp_len = req->sba->hw_resp_size;
	}
	cmdsp->flags |= BRCM_SBA_CMD_HAS_OUTPUT;
	cmdsp->data = resp_dma;
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
	req->flags |= SBA_REQUEST_FENCE;

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
	dma_addr_t resp_dma = req->tx.phys;
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
		cmdsp->resp = resp_dma;
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
	if (flags & DMA_PREP_FENCE)
		req->flags |= SBA_REQUEST_FENCE;

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
	dma_addr_t resp_dma = req->tx.phys;
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
		cmdsp->resp = resp_dma;
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
sba_prep_dma_xor_req(struct sba_device *sba,
		     dma_addr_t off, dma_addr_t dst, dma_addr_t *src,
		     u32 src_cnt, size_t len, unsigned long flags)
{
	struct sba_request *req = NULL;

	/* Alloc new request */
	req = sba_alloc_request(sba);
	if (!req)
		return NULL;
	if (flags & DMA_PREP_FENCE)
		req->flags |= SBA_REQUEST_FENCE;

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
	dma_addr_t resp_dma = req->tx.phys;
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
			cmdsp->resp = resp_dma;
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
			cmdsp->resp = resp_dma;
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

static struct sba_request *
sba_prep_dma_pq_req(struct sba_device *sba, dma_addr_t off,
		    dma_addr_t *dst_p, dma_addr_t *dst_q, dma_addr_t *src,
		    u32 src_cnt, const u8 *scf, size_t len, unsigned long flags)
{
	struct sba_request *req = NULL;

	/* Alloc new request */
	req = sba_alloc_request(sba);
	if (!req)
		return NULL;
	if (flags & DMA_PREP_FENCE)
		req->flags |= SBA_REQUEST_FENCE;

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
	dma_addr_t resp_dma = req->tx.phys;
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
		cmdsp->resp = resp_dma;
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
		cmdsp->resp = resp_dma;
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

static struct sba_request *
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
	if (flags & DMA_PREP_FENCE)
		req->flags |= SBA_REQUEST_FENCE;

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

static void sba_receive_message(struct mbox_client *cl, void *msg)
{
	struct brcm_message *m = msg;
	struct sba_request *req = m->ctx;
	struct sba_device *sba = req->sba;

	/* Error count if message has error */
	if (m->error < 0)
		dev_err(sba->dev, "%s got message with error %d",
			dma_chan_name(&sba->dma_chan), m->error);

	/* Process received request */
	sba_process_received_request(sba, req);
}

/* ====== Debugfs callbacks ====== */

static int sba_debugfs_stats_show(struct seq_file *file, void *offset)
{
	struct platform_device *pdev = to_platform_device(file->private);
	struct sba_device *sba = platform_get_drvdata(pdev);

	/* Write stats in file */
	sba_write_stats_in_seqfile(sba, file);

	return 0;
}

/* ====== Platform driver routines ===== */

static int sba_prealloc_channel_resources(struct sba_device *sba)
{
	int i, j, ret = 0;
	struct sba_request *req = NULL;

	sba->resp_base = dma_alloc_coherent(sba->mbox_dev,
					    sba->max_resp_pool_size,
					    &sba->resp_dma_base, GFP_KERNEL);
	if (!sba->resp_base)
		return -ENOMEM;

	sba->cmds_base = dma_alloc_coherent(sba->mbox_dev,
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
	INIT_LIST_HEAD(&sba->reqs_aborted_list);
	INIT_LIST_HEAD(&sba->reqs_free_list);

	for (i = 0; i < sba->max_req; i++) {
		req = devm_kzalloc(sba->dev,
				sizeof(*req) +
				sba->max_cmd_per_req * sizeof(req->cmds[0]),
				GFP_KERNEL);
		if (!req) {
			ret = -ENOMEM;
			goto fail_free_cmds_pool;
		}
		INIT_LIST_HEAD(&req->node);
		req->sba = sba;
		req->flags = SBA_REQUEST_STATE_FREE;
		INIT_LIST_HEAD(&req->next);
		atomic_set(&req->next_pending_count, 0);
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
		async_tx_ack(&req->tx);
		req->tx.tx_submit = sba_tx_submit;
		req->tx.phys = sba->resp_dma_base + i * sba->hw_resp_size;
		list_add_tail(&req->node, &sba->reqs_free_list);
	}

	return 0;

fail_free_cmds_pool:
	dma_free_coherent(sba->mbox_dev,
			  sba->max_cmds_pool_size,
			  sba->cmds_base, sba->cmds_dma_base);
fail_free_resp_pool:
	dma_free_coherent(sba->mbox_dev,
			  sba->max_resp_pool_size,
			  sba->resp_base, sba->resp_dma_base);
	return ret;
}

static void sba_freeup_channel_resources(struct sba_device *sba)
{
	dmaengine_terminate_all(&sba->dma_chan);
	dma_free_coherent(sba->mbox_dev, sba->max_cmds_pool_size,
			  sba->cmds_base, sba->cmds_dma_base);
	dma_free_coherent(sba->mbox_dev, sba->max_resp_pool_size,
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
	int ret = 0;
	struct sba_device *sba;
	struct platform_device *mbox_pdev;
	struct of_phandle_args args;

	/* Allocate main SBA struct */
	sba = devm_kzalloc(&pdev->dev, sizeof(*sba), GFP_KERNEL);
	if (!sba)
		return -ENOMEM;

	sba->dev = &pdev->dev;
	platform_set_drvdata(pdev, sba);

	/* Number of mailbox channels should be atleast 1 */
	ret = of_count_phandle_with_args(pdev->dev.of_node,
					 "mboxes", "#mbox-cells");
	if (ret <= 0)
		return -ENODEV;

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
		sba->hw_buf_size = 4096;
		sba->hw_resp_size = 8;
		sba->max_pq_coefs = 6;
		sba->max_pq_srcs = 6;
		break;
	case SBA_VER_2:
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
	sba->max_req = SBA_MAX_REQ_PER_MBOX_CHANNEL;
	sba->max_cmd_per_req = sba->max_pq_srcs + 3;
	sba->max_xor_srcs = sba->max_cmd_per_req - 1;
	sba->max_resp_pool_size = sba->max_req * sba->hw_resp_size;
	sba->max_cmds_pool_size = sba->max_req *
				  sba->max_cmd_per_req * sizeof(u64);

	/* Setup mailbox client */
	sba->client.dev			= &pdev->dev;
	sba->client.rx_callback		= sba_receive_message;
	sba->client.tx_block		= false;
	sba->client.knows_txdone	= true;
	sba->client.tx_tout		= 0;

	/* Request mailbox channel */
	sba->mchan = mbox_request_channel(&sba->client, 0);
	if (IS_ERR(sba->mchan)) {
		ret = PTR_ERR(sba->mchan);
		goto fail_free_mchan;
	}

	/* Find-out underlying mailbox device */
	ret = of_parse_phandle_with_args(pdev->dev.of_node,
					 "mboxes", "#mbox-cells", 0, &args);
	if (ret)
		goto fail_free_mchan;
	mbox_pdev = of_find_device_by_node(args.np);
	of_node_put(args.np);
	if (!mbox_pdev) {
		ret = -ENODEV;
		goto fail_free_mchan;
	}
	sba->mbox_dev = &mbox_pdev->dev;

	/* Prealloc channel resource */
	ret = sba_prealloc_channel_resources(sba);
	if (ret)
		goto fail_free_mchan;

	/* Check availability of debugfs */
	if (!debugfs_initialized())
		goto skip_debugfs;

	/* Create debugfs root entry */
	sba->root = debugfs_create_dir(dev_name(sba->dev), NULL);
	if (IS_ERR_OR_NULL(sba->root)) {
		dev_err(sba->dev, "failed to create debugfs root entry\n");
		sba->root = NULL;
		goto skip_debugfs;
	}

	/* Create debugfs stats entry */
	sba->stats = debugfs_create_devm_seqfile(sba->dev, "stats", sba->root,
						 sba_debugfs_stats_show);
	if (IS_ERR_OR_NULL(sba->stats))
		dev_err(sba->dev, "failed to create debugfs stats file\n");
skip_debugfs:

	/* Register DMA device with Linux async framework */
	ret = sba_async_register(sba);
	if (ret)
		goto fail_free_resources;

	/* Print device info */
	dev_info(sba->dev, "%s using SBAv%d mailbox channel from %s",
		 dma_chan_name(&sba->dma_chan), sba->ver+1,
		 dev_name(sba->mbox_dev));

	return 0;

fail_free_resources:
	debugfs_remove_recursive(sba->root);
	sba_freeup_channel_resources(sba);
fail_free_mchan:
	mbox_free_channel(sba->mchan);
	return ret;
}

static int sba_remove(struct platform_device *pdev)
{
	struct sba_device *sba = platform_get_drvdata(pdev);

	dma_async_device_unregister(&sba->dma_dev);

	debugfs_remove_recursive(sba->root);

	sba_freeup_channel_resources(sba);

	mbox_free_channel(sba->mchan);

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
