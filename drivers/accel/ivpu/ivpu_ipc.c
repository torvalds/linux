// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#include <linux/genalloc.h>
#include <linux/highmem.h>
#include <linux/pm_runtime.h>
#include <linux/wait.h>

#include "ivpu_drv.h"
#include "ivpu_gem.h"
#include "ivpu_hw.h"
#include "ivpu_hw_reg_io.h"
#include "ivpu_ipc.h"
#include "ivpu_jsm_msg.h"
#include "ivpu_pm.h"

#define IPC_MAX_RX_MSG	128

struct ivpu_ipc_tx_buf {
	struct ivpu_ipc_hdr ipc;
	struct vpu_jsm_msg jsm;
};

static void ivpu_ipc_msg_dump(struct ivpu_device *vdev, char *c,
			      struct ivpu_ipc_hdr *ipc_hdr, u32 vpu_addr)
{
	ivpu_dbg(vdev, IPC,
		 "%s: vpu:0x%x (data_addr:0x%08x, data_size:0x%x, channel:0x%x, src_node:0x%x, dst_node:0x%x, status:0x%x)",
		 c, vpu_addr, ipc_hdr->data_addr, ipc_hdr->data_size, ipc_hdr->channel,
		 ipc_hdr->src_node, ipc_hdr->dst_node, ipc_hdr->status);
}

static void ivpu_jsm_msg_dump(struct ivpu_device *vdev, char *c,
			      struct vpu_jsm_msg *jsm_msg, u32 vpu_addr)
{
	u32 *payload = (u32 *)&jsm_msg->payload;

	ivpu_dbg(vdev, JSM,
		 "%s: vpu:0x%08x (type:%s, status:0x%x, id: 0x%x, result: 0x%x, payload:0x%x 0x%x 0x%x 0x%x 0x%x)\n",
		 c, vpu_addr, ivpu_jsm_msg_type_to_str(jsm_msg->type),
		 jsm_msg->status, jsm_msg->request_id, jsm_msg->result,
		 payload[0], payload[1], payload[2], payload[3], payload[4]);
}

static void
ivpu_ipc_rx_mark_free(struct ivpu_device *vdev, struct ivpu_ipc_hdr *ipc_hdr,
		      struct vpu_jsm_msg *jsm_msg)
{
	ipc_hdr->status = IVPU_IPC_HDR_FREE;
	if (jsm_msg)
		jsm_msg->status = VPU_JSM_MSG_FREE;
	wmb(); /* Flush WC buffers for message statuses */
}

static void ivpu_ipc_mem_fini(struct ivpu_device *vdev)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;

	ivpu_bo_free(ipc->mem_rx);
	ivpu_bo_free(ipc->mem_tx);
}

static int
ivpu_ipc_tx_prepare(struct ivpu_device *vdev, struct ivpu_ipc_consumer *cons,
		    struct vpu_jsm_msg *req)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;
	struct ivpu_ipc_tx_buf *tx_buf;
	u32 tx_buf_vpu_addr;
	u32 jsm_vpu_addr;

	tx_buf_vpu_addr = gen_pool_alloc(ipc->mm_tx, sizeof(*tx_buf));
	if (!tx_buf_vpu_addr) {
		ivpu_err_ratelimited(vdev, "Failed to reserve IPC buffer, size %ld\n",
				     sizeof(*tx_buf));
		return -ENOMEM;
	}

	tx_buf = ivpu_to_cpu_addr(ipc->mem_tx, tx_buf_vpu_addr);
	if (drm_WARN_ON(&vdev->drm, !tx_buf)) {
		gen_pool_free(ipc->mm_tx, tx_buf_vpu_addr, sizeof(*tx_buf));
		return -EIO;
	}

	jsm_vpu_addr = tx_buf_vpu_addr + offsetof(struct ivpu_ipc_tx_buf, jsm);

	if (tx_buf->ipc.status != IVPU_IPC_HDR_FREE)
		ivpu_warn_ratelimited(vdev, "IPC message vpu:0x%x not released by firmware\n",
				      tx_buf_vpu_addr);

	if (tx_buf->jsm.status != VPU_JSM_MSG_FREE)
		ivpu_warn_ratelimited(vdev, "JSM message vpu:0x%x not released by firmware\n",
				      jsm_vpu_addr);

	memset(tx_buf, 0, sizeof(*tx_buf));
	tx_buf->ipc.data_addr = jsm_vpu_addr;
	/* TODO: Set data_size to actual JSM message size, not union of all messages */
	tx_buf->ipc.data_size = sizeof(*req);
	tx_buf->ipc.channel = cons->channel;
	tx_buf->ipc.src_node = 0;
	tx_buf->ipc.dst_node = 1;
	tx_buf->ipc.status = IVPU_IPC_HDR_ALLOCATED;
	tx_buf->jsm.type = req->type;
	tx_buf->jsm.status = VPU_JSM_MSG_ALLOCATED;
	tx_buf->jsm.payload = req->payload;

	req->request_id = atomic_inc_return(&ipc->request_id);
	tx_buf->jsm.request_id = req->request_id;
	cons->request_id = req->request_id;
	wmb(); /* Flush WC buffers for IPC, JSM msgs */

	cons->tx_vpu_addr = tx_buf_vpu_addr;

	ivpu_jsm_msg_dump(vdev, "TX", &tx_buf->jsm, jsm_vpu_addr);
	ivpu_ipc_msg_dump(vdev, "TX", &tx_buf->ipc, tx_buf_vpu_addr);

	return 0;
}

static void ivpu_ipc_tx_release(struct ivpu_device *vdev, u32 vpu_addr)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;

	if (vpu_addr)
		gen_pool_free(ipc->mm_tx, vpu_addr, sizeof(struct ivpu_ipc_tx_buf));
}

static void ivpu_ipc_tx(struct ivpu_device *vdev, u32 vpu_addr)
{
	ivpu_hw_reg_ipc_tx_set(vdev, vpu_addr);
}

static void
ivpu_ipc_rx_msg_add(struct ivpu_device *vdev, struct ivpu_ipc_consumer *cons,
		    struct ivpu_ipc_hdr *ipc_hdr, struct vpu_jsm_msg *jsm_msg)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;
	struct ivpu_ipc_rx_msg *rx_msg;

	lockdep_assert_held(&ipc->cons_lock);
	lockdep_assert_irqs_disabled();

	rx_msg = kzalloc(sizeof(*rx_msg), GFP_ATOMIC);
	if (!rx_msg) {
		ivpu_ipc_rx_mark_free(vdev, ipc_hdr, jsm_msg);
		return;
	}

	atomic_inc(&ipc->rx_msg_count);

	rx_msg->ipc_hdr = ipc_hdr;
	rx_msg->jsm_msg = jsm_msg;
	rx_msg->callback = cons->rx_callback;

	if (rx_msg->callback) {
		list_add_tail(&rx_msg->link, &ipc->cb_msg_list);
	} else {
		spin_lock(&cons->rx_lock);
		list_add_tail(&rx_msg->link, &cons->rx_msg_list);
		spin_unlock(&cons->rx_lock);
		wake_up(&cons->rx_msg_wq);
	}
}

static void
ivpu_ipc_rx_msg_del(struct ivpu_device *vdev, struct ivpu_ipc_rx_msg *rx_msg)
{
	list_del(&rx_msg->link);
	ivpu_ipc_rx_mark_free(vdev, rx_msg->ipc_hdr, rx_msg->jsm_msg);
	atomic_dec(&vdev->ipc->rx_msg_count);
	kfree(rx_msg);
}

void ivpu_ipc_consumer_add(struct ivpu_device *vdev, struct ivpu_ipc_consumer *cons,
			   u32 channel, ivpu_ipc_rx_callback_t rx_callback)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;

	INIT_LIST_HEAD(&cons->link);
	cons->channel = channel;
	cons->tx_vpu_addr = 0;
	cons->request_id = 0;
	cons->aborted = false;
	cons->rx_callback = rx_callback;
	spin_lock_init(&cons->rx_lock);
	INIT_LIST_HEAD(&cons->rx_msg_list);
	init_waitqueue_head(&cons->rx_msg_wq);

	spin_lock_irq(&ipc->cons_lock);
	list_add_tail(&cons->link, &ipc->cons_list);
	spin_unlock_irq(&ipc->cons_lock);
}

void ivpu_ipc_consumer_del(struct ivpu_device *vdev, struct ivpu_ipc_consumer *cons)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;
	struct ivpu_ipc_rx_msg *rx_msg, *r;

	spin_lock_irq(&ipc->cons_lock);
	list_del(&cons->link);
	spin_unlock_irq(&ipc->cons_lock);

	spin_lock_irq(&cons->rx_lock);
	list_for_each_entry_safe(rx_msg, r, &cons->rx_msg_list, link)
		ivpu_ipc_rx_msg_del(vdev, rx_msg);
	spin_unlock_irq(&cons->rx_lock);

	ivpu_ipc_tx_release(vdev, cons->tx_vpu_addr);
}

static int
ivpu_ipc_send(struct ivpu_device *vdev, struct ivpu_ipc_consumer *cons, struct vpu_jsm_msg *req)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;
	int ret;

	mutex_lock(&ipc->lock);

	if (!ipc->on) {
		ret = -EAGAIN;
		goto unlock;
	}

	ret = ivpu_ipc_tx_prepare(vdev, cons, req);
	if (ret)
		goto unlock;

	ivpu_ipc_tx(vdev, cons->tx_vpu_addr);

unlock:
	mutex_unlock(&ipc->lock);
	return ret;
}

static bool ivpu_ipc_rx_need_wakeup(struct ivpu_ipc_consumer *cons)
{
	bool ret;

	spin_lock_irq(&cons->rx_lock);
	ret = !list_empty(&cons->rx_msg_list) || cons->aborted;
	spin_unlock_irq(&cons->rx_lock);

	return ret;
}

int ivpu_ipc_receive(struct ivpu_device *vdev, struct ivpu_ipc_consumer *cons,
		     struct ivpu_ipc_hdr *ipc_buf,
		     struct vpu_jsm_msg *jsm_msg, unsigned long timeout_ms)
{
	struct ivpu_ipc_rx_msg *rx_msg;
	int wait_ret, ret = 0;

	if (drm_WARN_ONCE(&vdev->drm, cons->rx_callback, "Consumer works only in async mode\n"))
		return -EINVAL;

	wait_ret = wait_event_timeout(cons->rx_msg_wq,
				      ivpu_ipc_rx_need_wakeup(cons),
				      msecs_to_jiffies(timeout_ms));

	if (wait_ret == 0)
		return -ETIMEDOUT;

	spin_lock_irq(&cons->rx_lock);
	if (cons->aborted) {
		spin_unlock_irq(&cons->rx_lock);
		return -ECANCELED;
	}
	rx_msg = list_first_entry_or_null(&cons->rx_msg_list, struct ivpu_ipc_rx_msg, link);
	if (!rx_msg) {
		spin_unlock_irq(&cons->rx_lock);
		return -EAGAIN;
	}

	if (ipc_buf)
		memcpy(ipc_buf, rx_msg->ipc_hdr, sizeof(*ipc_buf));
	if (rx_msg->jsm_msg) {
		u32 size = min_t(int, rx_msg->ipc_hdr->data_size, sizeof(*jsm_msg));

		if (rx_msg->jsm_msg->result != VPU_JSM_STATUS_SUCCESS) {
			ivpu_dbg(vdev, IPC, "IPC resp result error: %d\n", rx_msg->jsm_msg->result);
			ret = -EBADMSG;
		}

		if (jsm_msg)
			memcpy(jsm_msg, rx_msg->jsm_msg, size);
	}

	ivpu_ipc_rx_msg_del(vdev, rx_msg);
	spin_unlock_irq(&cons->rx_lock);
	return ret;
}

static int
ivpu_ipc_send_receive_internal(struct ivpu_device *vdev, struct vpu_jsm_msg *req,
			       enum vpu_ipc_msg_type expected_resp_type,
			       struct vpu_jsm_msg *resp, u32 channel,
			       unsigned long timeout_ms)
{
	struct ivpu_ipc_consumer cons;
	int ret;

	ivpu_ipc_consumer_add(vdev, &cons, channel, NULL);

	ret = ivpu_ipc_send(vdev, &cons, req);
	if (ret) {
		ivpu_warn_ratelimited(vdev, "IPC send failed: %d\n", ret);
		goto consumer_del;
	}

	ret = ivpu_ipc_receive(vdev, &cons, NULL, resp, timeout_ms);
	if (ret) {
		ivpu_warn_ratelimited(vdev, "IPC receive failed: type %s, ret %d\n",
				      ivpu_jsm_msg_type_to_str(req->type), ret);
		goto consumer_del;
	}

	if (resp->type != expected_resp_type) {
		ivpu_warn_ratelimited(vdev, "Invalid JSM response type: 0x%x\n", resp->type);
		ret = -EBADE;
	}

consumer_del:
	ivpu_ipc_consumer_del(vdev, &cons);
	return ret;
}

int ivpu_ipc_send_receive_active(struct ivpu_device *vdev, struct vpu_jsm_msg *req,
				 enum vpu_ipc_msg_type expected_resp, struct vpu_jsm_msg *resp,
				 u32 channel, unsigned long timeout_ms)
{
	struct vpu_jsm_msg hb_req = { .type = VPU_JSM_MSG_QUERY_ENGINE_HB };
	struct vpu_jsm_msg hb_resp;
	int ret, hb_ret;

	drm_WARN_ON(&vdev->drm, pm_runtime_status_suspended(vdev->drm.dev));

	ret = ivpu_ipc_send_receive_internal(vdev, req, expected_resp, resp, channel, timeout_ms);
	if (ret != -ETIMEDOUT)
		return ret;

	hb_ret = ivpu_ipc_send_receive_internal(vdev, &hb_req, VPU_JSM_MSG_QUERY_ENGINE_HB_DONE,
						&hb_resp, VPU_IPC_CHAN_ASYNC_CMD,
						vdev->timeout.jsm);
	if (hb_ret == -ETIMEDOUT)
		ivpu_pm_trigger_recovery(vdev, "IPC timeout");

	return ret;
}

int ivpu_ipc_send_receive(struct ivpu_device *vdev, struct vpu_jsm_msg *req,
			  enum vpu_ipc_msg_type expected_resp, struct vpu_jsm_msg *resp,
			  u32 channel, unsigned long timeout_ms)
{
	int ret;

	ret = ivpu_rpm_get(vdev);
	if (ret < 0)
		return ret;

	ret = ivpu_ipc_send_receive_active(vdev, req, expected_resp, resp, channel, timeout_ms);

	ivpu_rpm_put(vdev);
	return ret;
}

static bool
ivpu_ipc_match_consumer(struct ivpu_device *vdev, struct ivpu_ipc_consumer *cons,
			struct ivpu_ipc_hdr *ipc_hdr, struct vpu_jsm_msg *jsm_msg)
{
	if (cons->channel != ipc_hdr->channel)
		return false;

	if (!jsm_msg || jsm_msg->request_id == cons->request_id)
		return true;

	return false;
}

void ivpu_ipc_irq_handler(struct ivpu_device *vdev, bool *wake_thread)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;
	struct ivpu_ipc_consumer *cons;
	struct ivpu_ipc_hdr *ipc_hdr;
	struct vpu_jsm_msg *jsm_msg;
	unsigned long flags;
	bool dispatched;
	u32 vpu_addr;

	/*
	 * Driver needs to purge all messages from IPC FIFO to clear IPC interrupt.
	 * Without purge IPC FIFO to 0 next IPC interrupts won't be generated.
	 */
	while (ivpu_hw_reg_ipc_rx_count_get(vdev)) {
		vpu_addr = ivpu_hw_reg_ipc_rx_addr_get(vdev);
		if (vpu_addr == REG_IO_ERROR) {
			ivpu_err_ratelimited(vdev, "Failed to read IPC rx addr register\n");
			return;
		}

		ipc_hdr = ivpu_to_cpu_addr(ipc->mem_rx, vpu_addr);
		if (!ipc_hdr) {
			ivpu_warn_ratelimited(vdev, "IPC msg 0x%x out of range\n", vpu_addr);
			continue;
		}
		ivpu_ipc_msg_dump(vdev, "RX", ipc_hdr, vpu_addr);

		jsm_msg = NULL;
		if (ipc_hdr->channel != IVPU_IPC_CHAN_BOOT_MSG) {
			jsm_msg = ivpu_to_cpu_addr(ipc->mem_rx, ipc_hdr->data_addr);
			if (!jsm_msg) {
				ivpu_warn_ratelimited(vdev, "JSM msg 0x%x out of range\n",
						      ipc_hdr->data_addr);
				ivpu_ipc_rx_mark_free(vdev, ipc_hdr, NULL);
				continue;
			}
			ivpu_jsm_msg_dump(vdev, "RX", jsm_msg, ipc_hdr->data_addr);
		}

		if (atomic_read(&ipc->rx_msg_count) > IPC_MAX_RX_MSG) {
			ivpu_warn_ratelimited(vdev, "IPC RX msg dropped, msg count %d\n",
					      IPC_MAX_RX_MSG);
			ivpu_ipc_rx_mark_free(vdev, ipc_hdr, jsm_msg);
			continue;
		}

		dispatched = false;
		spin_lock_irqsave(&ipc->cons_lock, flags);
		list_for_each_entry(cons, &ipc->cons_list, link) {
			if (ivpu_ipc_match_consumer(vdev, cons, ipc_hdr, jsm_msg)) {
				ivpu_ipc_rx_msg_add(vdev, cons, ipc_hdr, jsm_msg);
				dispatched = true;
				break;
			}
		}
		spin_unlock_irqrestore(&ipc->cons_lock, flags);

		if (!dispatched) {
			ivpu_dbg(vdev, IPC, "IPC RX msg 0x%x dropped (no consumer)\n", vpu_addr);
			ivpu_ipc_rx_mark_free(vdev, ipc_hdr, jsm_msg);
		}
	}

	if (wake_thread)
		*wake_thread = !list_empty(&ipc->cb_msg_list);
}

irqreturn_t ivpu_ipc_irq_thread_handler(struct ivpu_device *vdev)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;
	struct ivpu_ipc_rx_msg *rx_msg, *r;
	struct list_head cb_msg_list;

	INIT_LIST_HEAD(&cb_msg_list);

	spin_lock_irq(&ipc->cons_lock);
	list_splice_tail_init(&ipc->cb_msg_list, &cb_msg_list);
	spin_unlock_irq(&ipc->cons_lock);

	list_for_each_entry_safe(rx_msg, r, &cb_msg_list, link) {
		rx_msg->callback(vdev, rx_msg->ipc_hdr, rx_msg->jsm_msg);
		ivpu_ipc_rx_msg_del(vdev, rx_msg);
	}

	return IRQ_HANDLED;
}

int ivpu_ipc_init(struct ivpu_device *vdev)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;
	int ret;

	ipc->mem_tx = ivpu_bo_create_global(vdev, SZ_16K, DRM_IVPU_BO_WC | DRM_IVPU_BO_MAPPABLE);
	if (!ipc->mem_tx) {
		ivpu_err(vdev, "Failed to allocate mem_tx\n");
		return -ENOMEM;
	}

	ipc->mem_rx = ivpu_bo_create_global(vdev, SZ_16K, DRM_IVPU_BO_WC | DRM_IVPU_BO_MAPPABLE);
	if (!ipc->mem_rx) {
		ivpu_err(vdev, "Failed to allocate mem_rx\n");
		ret = -ENOMEM;
		goto err_free_tx;
	}

	ipc->mm_tx = devm_gen_pool_create(vdev->drm.dev, __ffs(IVPU_IPC_ALIGNMENT),
					  -1, "TX_IPC_JSM");
	if (IS_ERR(ipc->mm_tx)) {
		ret = PTR_ERR(ipc->mm_tx);
		ivpu_err(vdev, "Failed to create gen pool, %pe\n", ipc->mm_tx);
		goto err_free_rx;
	}

	ret = gen_pool_add(ipc->mm_tx, ipc->mem_tx->vpu_addr, ivpu_bo_size(ipc->mem_tx), -1);
	if (ret) {
		ivpu_err(vdev, "gen_pool_add failed, ret %d\n", ret);
		goto err_free_rx;
	}

	spin_lock_init(&ipc->cons_lock);
	INIT_LIST_HEAD(&ipc->cons_list);
	INIT_LIST_HEAD(&ipc->cb_msg_list);
	ret = drmm_mutex_init(&vdev->drm, &ipc->lock);
	if (ret) {
		ivpu_err(vdev, "Failed to initialize ipc->lock, ret %d\n", ret);
		goto err_free_rx;
	}
	ivpu_ipc_reset(vdev);
	return 0;

err_free_rx:
	ivpu_bo_free(ipc->mem_rx);
err_free_tx:
	ivpu_bo_free(ipc->mem_tx);
	return ret;
}

void ivpu_ipc_fini(struct ivpu_device *vdev)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;

	drm_WARN_ON(&vdev->drm, ipc->on);
	drm_WARN_ON(&vdev->drm, !list_empty(&ipc->cons_list));
	drm_WARN_ON(&vdev->drm, !list_empty(&ipc->cb_msg_list));
	drm_WARN_ON(&vdev->drm, atomic_read(&ipc->rx_msg_count) > 0);

	ivpu_ipc_mem_fini(vdev);
}

void ivpu_ipc_enable(struct ivpu_device *vdev)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;

	mutex_lock(&ipc->lock);
	ipc->on = true;
	mutex_unlock(&ipc->lock);
}

void ivpu_ipc_disable(struct ivpu_device *vdev)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;
	struct ivpu_ipc_consumer *cons, *c;
	struct ivpu_ipc_rx_msg *rx_msg, *r;

	drm_WARN_ON(&vdev->drm, !list_empty(&ipc->cb_msg_list));

	mutex_lock(&ipc->lock);
	ipc->on = false;
	mutex_unlock(&ipc->lock);

	spin_lock_irq(&ipc->cons_lock);
	list_for_each_entry_safe(cons, c, &ipc->cons_list, link) {
		spin_lock(&cons->rx_lock);
		if (!cons->rx_callback)
			cons->aborted = true;
		list_for_each_entry_safe(rx_msg, r, &cons->rx_msg_list, link)
			ivpu_ipc_rx_msg_del(vdev, rx_msg);
		spin_unlock(&cons->rx_lock);
		wake_up(&cons->rx_msg_wq);
	}
	spin_unlock_irq(&ipc->cons_lock);

	drm_WARN_ON(&vdev->drm, atomic_read(&ipc->rx_msg_count) > 0);
}

void ivpu_ipc_reset(struct ivpu_device *vdev)
{
	struct ivpu_ipc_info *ipc = vdev->ipc;

	mutex_lock(&ipc->lock);
	drm_WARN_ON(&vdev->drm, ipc->on);

	memset(ivpu_bo_vaddr(ipc->mem_tx), 0, ivpu_bo_size(ipc->mem_tx));
	memset(ivpu_bo_vaddr(ipc->mem_rx), 0, ivpu_bo_size(ipc->mem_rx));
	wmb(); /* Flush WC buffers for TX and RX rings */

	mutex_unlock(&ipc->lock);
}
