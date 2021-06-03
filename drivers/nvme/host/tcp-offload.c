// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Marvell. All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
/* Kernel includes */
#include <linux/kernel.h>
#include <linux/module.h>

/* Driver includes */
#include "tcp-offload.h"

static LIST_HEAD(nvme_tcp_ofld_devices);
static DEFINE_MUTEX(nvme_tcp_ofld_devices_mutex);
static LIST_HEAD(nvme_tcp_ofld_ctrl_list);
static DEFINE_MUTEX(nvme_tcp_ofld_ctrl_mutex);
static struct blk_mq_ops nvme_tcp_ofld_admin_mq_ops;
static struct blk_mq_ops nvme_tcp_ofld_mq_ops;

static inline struct nvme_tcp_ofld_ctrl *to_tcp_ofld_ctrl(struct nvme_ctrl *nctrl)
{
	return container_of(nctrl, struct nvme_tcp_ofld_ctrl, nctrl);
}

static inline int nvme_tcp_ofld_qid(struct nvme_tcp_ofld_queue *queue)
{
	return queue - queue->ctrl->queues;
}

/**
 * nvme_tcp_ofld_register_dev() - NVMeTCP Offload Library registration
 * function.
 * @dev:	NVMeTCP offload device instance to be registered to the
 *		common tcp offload instance.
 *
 * API function that registers the type of vendor specific driver
 * being implemented to the common NVMe over TCP offload library. Part of
 * the overall init sequence of starting up an offload driver.
 */
int nvme_tcp_ofld_register_dev(struct nvme_tcp_ofld_dev *dev)
{
	struct nvme_tcp_ofld_ops *ops = dev->ops;

	if (!ops->claim_dev ||
	    !ops->setup_ctrl ||
	    !ops->release_ctrl ||
	    !ops->create_queue ||
	    !ops->drain_queue ||
	    !ops->destroy_queue ||
	    !ops->poll_queue ||
	    !ops->send_req)
		return -EINVAL;

	mutex_lock(&nvme_tcp_ofld_devices_mutex);
	list_add_tail(&dev->entry, &nvme_tcp_ofld_devices);
	mutex_unlock(&nvme_tcp_ofld_devices_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(nvme_tcp_ofld_register_dev);

/**
 * nvme_tcp_ofld_unregister_dev() - NVMeTCP Offload Library unregistration
 * function.
 * @dev:	NVMeTCP offload device instance to be unregistered from the
 *		common tcp offload instance.
 *
 * API function that unregisters the type of vendor specific driver being
 * implemented from the common NVMe over TCP offload library.
 * Part of the overall exit sequence of unloading the implemented driver.
 */
void nvme_tcp_ofld_unregister_dev(struct nvme_tcp_ofld_dev *dev)
{
	mutex_lock(&nvme_tcp_ofld_devices_mutex);
	list_del(&dev->entry);
	mutex_unlock(&nvme_tcp_ofld_devices_mutex);
}
EXPORT_SYMBOL_GPL(nvme_tcp_ofld_unregister_dev);

/**
 * nvme_tcp_ofld_error_recovery() - NVMeTCP Offload library error recovery.
 * function.
 * @nctrl:	NVMe controller instance to change to resetting.
 *
 * API function that change the controller state to resseting.
 * Part of the overall controller reset sequence.
 */
void nvme_tcp_ofld_error_recovery(struct nvme_ctrl *nctrl)
{
	if (!nvme_change_ctrl_state(nctrl, NVME_CTRL_RESETTING))
		return;

	queue_work(nvme_reset_wq, &to_tcp_ofld_ctrl(nctrl)->err_work);
}
EXPORT_SYMBOL_GPL(nvme_tcp_ofld_error_recovery);

/**
 * nvme_tcp_ofld_report_queue_err() - NVMeTCP Offload report error event
 * callback function. Pointed to by nvme_tcp_ofld_queue->report_err.
 * @queue:	NVMeTCP offload queue instance on which the error has occurred.
 *
 * API function that allows the vendor specific offload driver to reports errors
 * to the common offload layer, to invoke error recovery.
 */
int nvme_tcp_ofld_report_queue_err(struct nvme_tcp_ofld_queue *queue)
{
	pr_err("nvme-tcp-offload queue error\n");
	nvme_tcp_ofld_error_recovery(&queue->ctrl->nctrl);

	return 0;
}

/**
 * nvme_tcp_ofld_req_done() - NVMeTCP Offload request done callback
 * function. Pointed to by nvme_tcp_ofld_req->done.
 * Handles both NVME_TCP_F_DATA_SUCCESS flag and NVMe CQ.
 * @req:	NVMeTCP offload request to complete.
 * @result:     The nvme_result.
 * @status:     The completion status.
 *
 * API function that allows the vendor specific offload driver to report request
 * completions to the common offload layer.
 */
void nvme_tcp_ofld_req_done(struct nvme_tcp_ofld_req *req,
			    union nvme_result *result,
			    __le16 status)
{
	struct request *rq = blk_mq_rq_from_pdu(req);

	if (!nvme_try_complete_req(rq, cpu_to_le16(status << 1), *result))
		nvme_complete_rq(rq);
}

/**
 * nvme_tcp_ofld_async_req_done() - NVMeTCP Offload request done callback
 * function for async request. Pointed to by nvme_tcp_ofld_req->done.
 * Handles both NVME_TCP_F_DATA_SUCCESS flag and NVMe CQ.
 * @req:	NVMeTCP offload request to complete.
 * @result:     The nvme_result.
 * @status:     The completion status.
 *
 * API function that allows the vendor specific offload driver to report request
 * completions to the common offload layer.
 */
void nvme_tcp_ofld_async_req_done(struct nvme_tcp_ofld_req *req,
				  union nvme_result *result, __le16 status)
{
	struct nvme_tcp_ofld_queue *queue = req->queue;
	struct nvme_tcp_ofld_ctrl *ctrl = queue->ctrl;

	nvme_complete_async_event(&ctrl->nctrl, status, result);
}

static struct nvme_tcp_ofld_dev *
nvme_tcp_ofld_lookup_dev(struct nvme_tcp_ofld_ctrl *ctrl)
{
	struct nvme_tcp_ofld_dev *dev;

	mutex_lock(&nvme_tcp_ofld_devices_mutex);
	list_for_each_entry(dev, &nvme_tcp_ofld_devices, entry) {
		if (dev->ops->claim_dev(dev, ctrl))
			goto out;
	}

	dev = NULL;
out:
	mutex_unlock(&nvme_tcp_ofld_devices_mutex);

	return dev;
}

static struct blk_mq_tag_set *
nvme_tcp_ofld_alloc_tagset(struct nvme_ctrl *nctrl, bool admin)
{
	struct nvme_tcp_ofld_ctrl *ctrl = to_tcp_ofld_ctrl(nctrl);
	struct blk_mq_tag_set *set;
	int rc;

	if (admin) {
		set = &ctrl->admin_tag_set;
		memset(set, 0, sizeof(*set));
		set->ops = &nvme_tcp_ofld_admin_mq_ops;
		set->queue_depth = NVME_AQ_MQ_TAG_DEPTH;
		set->reserved_tags = NVMF_RESERVED_TAGS;
		set->numa_node = nctrl->numa_node;
		set->flags = BLK_MQ_F_BLOCKING;
		set->cmd_size = sizeof(struct nvme_tcp_ofld_req);
		set->driver_data = ctrl;
		set->nr_hw_queues = 1;
		set->timeout = NVME_ADMIN_TIMEOUT;
	} else {
		set = &ctrl->tag_set;
		memset(set, 0, sizeof(*set));
		set->ops = &nvme_tcp_ofld_mq_ops;
		set->queue_depth = nctrl->sqsize + 1;
		set->reserved_tags = NVMF_RESERVED_TAGS;
		set->numa_node = nctrl->numa_node;
		set->flags = BLK_MQ_F_SHOULD_MERGE;
		set->cmd_size = sizeof(struct nvme_tcp_ofld_req);
		set->driver_data = ctrl;
		set->nr_hw_queues = nctrl->queue_count - 1;
		set->timeout = NVME_IO_TIMEOUT;
		set->nr_maps = nctrl->opts->nr_poll_queues ? HCTX_MAX_TYPES : 2;
	}

	rc = blk_mq_alloc_tag_set(set);
	if (rc)
		return ERR_PTR(rc);

	return set;
}

static void __nvme_tcp_ofld_stop_queue(struct nvme_tcp_ofld_queue *queue)
{
	queue->dev->ops->drain_queue(queue);
}

static void nvme_tcp_ofld_stop_queue(struct nvme_ctrl *nctrl, int qid)
{
	struct nvme_tcp_ofld_ctrl *ctrl = to_tcp_ofld_ctrl(nctrl);
	struct nvme_tcp_ofld_queue *queue = &ctrl->queues[qid];

	mutex_lock(&queue->queue_lock);
	if (test_and_clear_bit(NVME_TCP_OFLD_Q_LIVE, &queue->flags))
		__nvme_tcp_ofld_stop_queue(queue);
	mutex_unlock(&queue->queue_lock);
}

static void nvme_tcp_ofld_stop_io_queues(struct nvme_ctrl *ctrl)
{
	int i;

	for (i = 1; i < ctrl->queue_count; i++)
		nvme_tcp_ofld_stop_queue(ctrl, i);
}

static void __nvme_tcp_ofld_free_queue(struct nvme_tcp_ofld_queue *queue)
{
	queue->dev->ops->destroy_queue(queue);
}

static void nvme_tcp_ofld_free_queue(struct nvme_ctrl *nctrl, int qid)
{
	struct nvme_tcp_ofld_ctrl *ctrl = to_tcp_ofld_ctrl(nctrl);
	struct nvme_tcp_ofld_queue *queue = &ctrl->queues[qid];

	if (test_and_clear_bit(NVME_TCP_OFLD_Q_ALLOCATED, &queue->flags)) {
		__nvme_tcp_ofld_free_queue(queue);
		mutex_destroy(&queue->queue_lock);
	}
}

static void
nvme_tcp_ofld_free_io_queues(struct nvme_ctrl *nctrl)
{
	int i;

	for (i = 1; i < nctrl->queue_count; i++)
		nvme_tcp_ofld_free_queue(nctrl, i);
}

static void nvme_tcp_ofld_destroy_io_queues(struct nvme_ctrl *nctrl, bool remove)
{
	nvme_tcp_ofld_stop_io_queues(nctrl);
	if (remove) {
		blk_cleanup_queue(nctrl->connect_q);
		blk_mq_free_tag_set(nctrl->tagset);
	}
	nvme_tcp_ofld_free_io_queues(nctrl);
}

static void nvme_tcp_ofld_destroy_admin_queue(struct nvme_ctrl *nctrl, bool remove)
{
	nvme_tcp_ofld_stop_queue(nctrl, 0);
	if (remove) {
		blk_cleanup_queue(nctrl->admin_q);
		blk_cleanup_queue(nctrl->fabrics_q);
		blk_mq_free_tag_set(nctrl->admin_tagset);
	}
	nvme_tcp_ofld_free_queue(nctrl, 0);
}

static int nvme_tcp_ofld_start_queue(struct nvme_ctrl *nctrl, int qid)
{
	struct nvme_tcp_ofld_ctrl *ctrl = to_tcp_ofld_ctrl(nctrl);
	struct nvme_tcp_ofld_queue *queue = &ctrl->queues[qid];
	int rc;

	queue = &ctrl->queues[qid];
	if (qid) {
		queue->cmnd_capsule_len = nctrl->ioccsz * 16;
		rc = nvmf_connect_io_queue(nctrl, qid, false);
	} else {
		queue->cmnd_capsule_len = sizeof(struct nvme_command) + NVME_TCP_ADMIN_CCSZ;
		rc = nvmf_connect_admin_queue(nctrl);
	}

	if (!rc) {
		set_bit(NVME_TCP_OFLD_Q_LIVE, &queue->flags);
	} else {
		if (test_bit(NVME_TCP_OFLD_Q_ALLOCATED, &queue->flags))
			__nvme_tcp_ofld_stop_queue(queue);
		dev_err(nctrl->device,
			"failed to connect queue: %d ret=%d\n", qid, rc);
	}

	return rc;
}

static int nvme_tcp_ofld_configure_admin_queue(struct nvme_ctrl *nctrl,
					       bool new)
{
	struct nvme_tcp_ofld_ctrl *ctrl = to_tcp_ofld_ctrl(nctrl);
	struct nvme_tcp_ofld_queue *queue = &ctrl->queues[0];
	int rc;

	mutex_init(&queue->queue_lock);

	rc = ctrl->dev->ops->create_queue(queue, 0, NVME_AQ_DEPTH);
	if (rc)
		return rc;

	set_bit(NVME_TCP_OFLD_Q_ALLOCATED, &queue->flags);
	if (new) {
		nctrl->admin_tagset =
				nvme_tcp_ofld_alloc_tagset(nctrl, true);
		if (IS_ERR(nctrl->admin_tagset)) {
			rc = PTR_ERR(nctrl->admin_tagset);
			nctrl->admin_tagset = NULL;
			goto out_free_queue;
		}

		nctrl->fabrics_q = blk_mq_init_queue(nctrl->admin_tagset);
		if (IS_ERR(nctrl->fabrics_q)) {
			rc = PTR_ERR(nctrl->fabrics_q);
			nctrl->fabrics_q = NULL;
			goto out_free_tagset;
		}

		nctrl->admin_q = blk_mq_init_queue(nctrl->admin_tagset);
		if (IS_ERR(nctrl->admin_q)) {
			rc = PTR_ERR(nctrl->admin_q);
			nctrl->admin_q = NULL;
			goto out_cleanup_fabrics_q;
		}
	}

	rc = nvme_tcp_ofld_start_queue(nctrl, 0);
	if (rc)
		goto out_cleanup_queue;

	rc = nvme_enable_ctrl(nctrl);
	if (rc)
		goto out_stop_queue;

	blk_mq_unquiesce_queue(nctrl->admin_q);

	rc = nvme_init_ctrl_finish(nctrl);
	if (rc)
		goto out_quiesce_queue;

	return 0;

out_quiesce_queue:
	blk_mq_quiesce_queue(nctrl->admin_q);
	blk_sync_queue(nctrl->admin_q);
out_stop_queue:
	nvme_tcp_ofld_stop_queue(nctrl, 0);
	nvme_cancel_admin_tagset(nctrl);
out_cleanup_queue:
	if (new)
		blk_cleanup_queue(nctrl->admin_q);
out_cleanup_fabrics_q:
	if (new)
		blk_cleanup_queue(nctrl->fabrics_q);
out_free_tagset:
	if (new)
		blk_mq_free_tag_set(nctrl->admin_tagset);
out_free_queue:
	nvme_tcp_ofld_free_queue(nctrl, 0);

	return rc;
}

static unsigned int nvme_tcp_ofld_nr_io_queues(struct nvme_ctrl *nctrl)
{
	struct nvme_tcp_ofld_ctrl *ctrl = to_tcp_ofld_ctrl(nctrl);
	struct nvme_tcp_ofld_dev *dev = ctrl->dev;
	u32 hw_vectors = dev->num_hw_vectors;
	u32 nr_write_queues, nr_poll_queues;
	u32 nr_io_queues, nr_total_queues;

	nr_io_queues = min3(nctrl->opts->nr_io_queues, num_online_cpus(),
			    hw_vectors);
	nr_write_queues = min3(nctrl->opts->nr_write_queues, num_online_cpus(),
			       hw_vectors);
	nr_poll_queues = min3(nctrl->opts->nr_poll_queues, num_online_cpus(),
			      hw_vectors);

	nr_total_queues = nr_io_queues + nr_write_queues + nr_poll_queues;

	return nr_total_queues;
}

static void
nvme_tcp_ofld_set_io_queues(struct nvme_ctrl *nctrl, unsigned int nr_io_queues)
{
	struct nvme_tcp_ofld_ctrl *ctrl = to_tcp_ofld_ctrl(nctrl);
	struct nvmf_ctrl_options *opts = nctrl->opts;

	if (opts->nr_write_queues && opts->nr_io_queues < nr_io_queues) {
		/*
		 * separate read/write queues
		 * hand out dedicated default queues only after we have
		 * sufficient read queues.
		 */
		ctrl->io_queues[HCTX_TYPE_READ] = opts->nr_io_queues;
		nr_io_queues -= ctrl->io_queues[HCTX_TYPE_READ];
		ctrl->io_queues[HCTX_TYPE_DEFAULT] =
			min(opts->nr_write_queues, nr_io_queues);
		nr_io_queues -= ctrl->io_queues[HCTX_TYPE_DEFAULT];
	} else {
		/*
		 * shared read/write queues
		 * either no write queues were requested, or we don't have
		 * sufficient queue count to have dedicated default queues.
		 */
		ctrl->io_queues[HCTX_TYPE_DEFAULT] =
			min(opts->nr_io_queues, nr_io_queues);
		nr_io_queues -= ctrl->io_queues[HCTX_TYPE_DEFAULT];
	}

	if (opts->nr_poll_queues && nr_io_queues) {
		/* map dedicated poll queues only if we have queues left */
		ctrl->io_queues[HCTX_TYPE_POLL] =
			min(opts->nr_poll_queues, nr_io_queues);
	}
}

static int nvme_tcp_ofld_create_io_queues(struct nvme_ctrl *nctrl)
{
	struct nvme_tcp_ofld_ctrl *ctrl = to_tcp_ofld_ctrl(nctrl);
	int i, rc;

	for (i = 1; i < nctrl->queue_count; i++) {
		mutex_init(&ctrl->queues[i].queue_lock);

		rc = ctrl->dev->ops->create_queue(&ctrl->queues[i],
						  i, nctrl->sqsize + 1);
		if (rc)
			goto out_free_queues;

		set_bit(NVME_TCP_OFLD_Q_ALLOCATED, &ctrl->queues[i].flags);
	}

	return 0;

out_free_queues:
	for (i--; i >= 1; i--)
		nvme_tcp_ofld_free_queue(nctrl, i);

	return rc;
}

static int nvme_tcp_ofld_alloc_io_queues(struct nvme_ctrl *nctrl)
{
	unsigned int nr_io_queues;
	int rc;

	nr_io_queues = nvme_tcp_ofld_nr_io_queues(nctrl);
	rc = nvme_set_queue_count(nctrl, &nr_io_queues);
	if (rc)
		return rc;

	nctrl->queue_count = nr_io_queues + 1;
	if (nctrl->queue_count < 2) {
		dev_err(nctrl->device,
			"unable to set any I/O queues\n");

		return -ENOMEM;
	}

	dev_info(nctrl->device, "creating %d I/O queues.\n", nr_io_queues);
	nvme_tcp_ofld_set_io_queues(nctrl, nr_io_queues);

	return nvme_tcp_ofld_create_io_queues(nctrl);
}

static int nvme_tcp_ofld_start_io_queues(struct nvme_ctrl *nctrl)
{
	int i, rc = 0;

	for (i = 1; i < nctrl->queue_count; i++) {
		rc = nvme_tcp_ofld_start_queue(nctrl, i);
		if (rc)
			goto out_stop_queues;
	}

	return 0;

out_stop_queues:
	for (i--; i >= 1; i--)
		nvme_tcp_ofld_stop_queue(nctrl, i);

	return rc;
}

static int
nvme_tcp_ofld_configure_io_queues(struct nvme_ctrl *nctrl, bool new)
{
	int rc = nvme_tcp_ofld_alloc_io_queues(nctrl);

	if (rc)
		return rc;

	if (new) {
		nctrl->tagset = nvme_tcp_ofld_alloc_tagset(nctrl, false);
		if (IS_ERR(nctrl->tagset)) {
			rc = PTR_ERR(nctrl->tagset);
			nctrl->tagset = NULL;
			goto out_free_io_queues;
		}

		nctrl->connect_q = blk_mq_init_queue(nctrl->tagset);
		if (IS_ERR(nctrl->connect_q)) {
			rc = PTR_ERR(nctrl->connect_q);
			nctrl->connect_q = NULL;
			goto out_free_tag_set;
		}
	}

	rc = nvme_tcp_ofld_start_io_queues(nctrl);
	if (rc)
		goto out_cleanup_connect_q;

	if (!new) {
		nvme_start_queues(nctrl);
		if (!nvme_wait_freeze_timeout(nctrl, NVME_IO_TIMEOUT)) {
			/*
			 * If we timed out waiting for freeze we are likely to
			 * be stuck.  Fail the controller initialization just
			 * to be safe.
			 */
			rc = -ENODEV;
			goto out_wait_freeze_timed_out;
		}
		blk_mq_update_nr_hw_queues(nctrl->tagset, nctrl->queue_count - 1);
		nvme_unfreeze(nctrl);
	}

	return 0;

out_wait_freeze_timed_out:
	nvme_stop_queues(nctrl);
	nvme_sync_io_queues(nctrl);
	nvme_tcp_ofld_stop_io_queues(nctrl);
out_cleanup_connect_q:
	nvme_cancel_tagset(nctrl);
	if (new)
		blk_cleanup_queue(nctrl->connect_q);
out_free_tag_set:
	if (new)
		blk_mq_free_tag_set(nctrl->tagset);
out_free_io_queues:
	nvme_tcp_ofld_free_io_queues(nctrl);

	return rc;
}

static void nvme_tcp_ofld_reconnect_or_remove(struct nvme_ctrl *nctrl)
{
	/* If we are resetting/deleting then do nothing */
	if (nctrl->state != NVME_CTRL_CONNECTING) {
		WARN_ON_ONCE(nctrl->state == NVME_CTRL_NEW ||
			     nctrl->state == NVME_CTRL_LIVE);

		return;
	}

	if (nvmf_should_reconnect(nctrl)) {
		dev_info(nctrl->device, "Reconnecting in %d seconds...\n",
			 nctrl->opts->reconnect_delay);
		queue_delayed_work(nvme_wq,
				   &to_tcp_ofld_ctrl(nctrl)->connect_work,
				   nctrl->opts->reconnect_delay * HZ);
	} else {
		dev_info(nctrl->device, "Removing controller...\n");
		nvme_delete_ctrl(nctrl);
	}
}

static int
nvme_tcp_ofld_init_admin_hctx(struct blk_mq_hw_ctx *hctx, void *data,
			      unsigned int hctx_idx)
{
	struct nvme_tcp_ofld_ctrl *ctrl = data;

	hctx->driver_data = &ctrl->queues[0];

	return 0;
}

static int nvme_tcp_ofld_setup_ctrl(struct nvme_ctrl *nctrl, bool new)
{
	struct nvme_tcp_ofld_ctrl *ctrl = to_tcp_ofld_ctrl(nctrl);
	struct nvmf_ctrl_options *opts = nctrl->opts;
	int rc = 0;

	rc = ctrl->dev->ops->setup_ctrl(ctrl);
	if (rc)
		return rc;

	rc = nvme_tcp_ofld_configure_admin_queue(nctrl, new);
	if (rc)
		goto out_release_ctrl;

	if (nctrl->icdoff) {
		dev_err(nctrl->device, "icdoff is not supported!\n");
		rc = -EINVAL;
		goto destroy_admin;
	}

	if (!(nctrl->sgls & ((1 << 0) | (1 << 1)))) {
		dev_err(nctrl->device, "Mandatory sgls are not supported!\n");
		goto destroy_admin;
	}

	if (opts->queue_size > nctrl->sqsize + 1)
		dev_warn(nctrl->device,
			 "queue_size %zu > ctrl sqsize %u, clamping down\n",
			 opts->queue_size, nctrl->sqsize + 1);

	if (nctrl->sqsize + 1 > nctrl->maxcmd) {
		dev_warn(nctrl->device,
			 "sqsize %u > ctrl maxcmd %u, clamping down\n",
			 nctrl->sqsize + 1, nctrl->maxcmd);
		nctrl->sqsize = nctrl->maxcmd - 1;
	}

	if (nctrl->queue_count > 1) {
		rc = nvme_tcp_ofld_configure_io_queues(nctrl, new);
		if (rc)
			goto destroy_admin;
	}

	if (!nvme_change_ctrl_state(nctrl, NVME_CTRL_LIVE)) {
		/*
		 * state change failure is ok if we started ctrl delete,
		 * unless we're during creation of a new controller to
		 * avoid races with teardown flow.
		 */
		WARN_ON_ONCE(nctrl->state != NVME_CTRL_DELETING &&
			     nctrl->state != NVME_CTRL_DELETING_NOIO);
		WARN_ON_ONCE(new);
		rc = -EINVAL;
		goto destroy_io;
	}

	nvme_start_ctrl(nctrl);

	return 0;

destroy_io:
	if (nctrl->queue_count > 1) {
		nvme_stop_queues(nctrl);
		nvme_sync_io_queues(nctrl);
		nvme_tcp_ofld_stop_io_queues(nctrl);
		nvme_cancel_tagset(nctrl);
		nvme_tcp_ofld_destroy_io_queues(nctrl, new);
	}
destroy_admin:
	blk_mq_quiesce_queue(nctrl->admin_q);
	blk_sync_queue(nctrl->admin_q);
	nvme_tcp_ofld_stop_queue(nctrl, 0);
	nvme_cancel_admin_tagset(nctrl);
	nvme_tcp_ofld_destroy_admin_queue(nctrl, new);
out_release_ctrl:
	ctrl->dev->ops->release_ctrl(ctrl);

	return rc;
}

static int
nvme_tcp_ofld_check_dev_opts(struct nvmf_ctrl_options *opts,
			     struct nvme_tcp_ofld_ops *ofld_ops)
{
	unsigned int nvme_tcp_ofld_opt_mask = NVMF_ALLOWED_OPTS |
			ofld_ops->allowed_opts | ofld_ops->required_opts;
	struct nvmf_ctrl_options dev_opts_mask;

	if (opts->mask & ~nvme_tcp_ofld_opt_mask) {
		pr_warn("One or more nvmf options missing from ofld drvr %s.\n",
			ofld_ops->name);

		dev_opts_mask.mask = nvme_tcp_ofld_opt_mask;

		return nvmf_check_required_opts(&dev_opts_mask, opts->mask);
	}

	return 0;
}

static void nvme_tcp_ofld_free_ctrl(struct nvme_ctrl *nctrl)
{
	struct nvme_tcp_ofld_ctrl *ctrl = to_tcp_ofld_ctrl(nctrl);
	struct nvme_tcp_ofld_dev *dev = ctrl->dev;

	if (list_empty(&ctrl->list))
		goto free_ctrl;

	ctrl->dev->ops->release_ctrl(ctrl);

	mutex_lock(&nvme_tcp_ofld_ctrl_mutex);
	list_del(&ctrl->list);
	mutex_unlock(&nvme_tcp_ofld_ctrl_mutex);

	nvmf_free_options(nctrl->opts);
free_ctrl:
	module_put(dev->ops->module);
	kfree(ctrl->queues);
	kfree(ctrl);
}

static void nvme_tcp_ofld_set_sg_null(struct nvme_command *c)
{
	struct nvme_sgl_desc *sg = &c->common.dptr.sgl;

	sg->addr = 0;
	sg->length = 0;
	sg->type = (NVME_TRANSPORT_SGL_DATA_DESC << 4) | NVME_SGL_FMT_TRANSPORT_A;
}

inline void nvme_tcp_ofld_set_sg_inline(struct nvme_tcp_ofld_queue *queue,
					struct nvme_command *c, u32 data_len)
{
	struct nvme_sgl_desc *sg = &c->common.dptr.sgl;

	sg->addr = cpu_to_le64(queue->ctrl->nctrl.icdoff);
	sg->length = cpu_to_le32(data_len);
	sg->type = (NVME_SGL_FMT_DATA_DESC << 4) | NVME_SGL_FMT_OFFSET;
}

static void nvme_tcp_ofld_map_data(struct nvme_command *c, u32 data_len)
{
	struct nvme_sgl_desc *sg = &c->common.dptr.sgl;

	sg->addr = 0;
	sg->length = cpu_to_le32(data_len);
	sg->type = (NVME_TRANSPORT_SGL_DATA_DESC << 4) | NVME_SGL_FMT_TRANSPORT_A;
}

static void nvme_tcp_ofld_submit_async_event(struct nvme_ctrl *arg)
{
	struct nvme_tcp_ofld_ctrl *ctrl = to_tcp_ofld_ctrl(arg);
	struct nvme_tcp_ofld_queue *queue = &ctrl->queues[0];
	struct nvme_tcp_ofld_dev *dev = queue->dev;
	struct nvme_tcp_ofld_ops *ops = dev->ops;

	ctrl->async_req.nvme_cmd.common.opcode = nvme_admin_async_event;
	ctrl->async_req.nvme_cmd.common.command_id = NVME_AQ_BLK_MQ_DEPTH;
	ctrl->async_req.nvme_cmd.common.flags |= NVME_CMD_SGL_METABUF;

	nvme_tcp_ofld_set_sg_null(&ctrl->async_req.nvme_cmd);

	ctrl->async_req.async = true;
	ctrl->async_req.queue = queue;
	ctrl->async_req.done = nvme_tcp_ofld_async_req_done;

	ops->send_req(&ctrl->async_req);
}

static void
nvme_tcp_ofld_teardown_admin_queue(struct nvme_ctrl *nctrl, bool remove)
{
	blk_mq_quiesce_queue(nctrl->admin_q);
	blk_sync_queue(nctrl->admin_q);

	nvme_tcp_ofld_stop_queue(nctrl, 0);
	nvme_cancel_admin_tagset(nctrl);

	if (remove)
		blk_mq_unquiesce_queue(nctrl->admin_q);

	nvme_tcp_ofld_destroy_admin_queue(nctrl, remove);
}

static void
nvme_tcp_ofld_teardown_io_queues(struct nvme_ctrl *nctrl, bool remove)
{
	if (nctrl->queue_count <= 1)
		return;

	blk_mq_quiesce_queue(nctrl->admin_q);
	nvme_start_freeze(nctrl);
	nvme_stop_queues(nctrl);
	nvme_sync_io_queues(nctrl);
	nvme_tcp_ofld_stop_io_queues(nctrl);
	nvme_cancel_tagset(nctrl);

	if (remove)
		nvme_start_queues(nctrl);

	nvme_tcp_ofld_destroy_io_queues(nctrl, remove);
}

static void nvme_tcp_ofld_reconnect_ctrl_work(struct work_struct *work)
{
	struct nvme_tcp_ofld_ctrl *ctrl =
				container_of(to_delayed_work(work),
					     struct nvme_tcp_ofld_ctrl,
					     connect_work);
	struct nvme_ctrl *nctrl = &ctrl->nctrl;

	++nctrl->nr_reconnects;

	if (nvme_tcp_ofld_setup_ctrl(nctrl, false))
		goto requeue;

	dev_info(nctrl->device, "Successfully reconnected (%d attempt)\n",
		 nctrl->nr_reconnects);

	nctrl->nr_reconnects = 0;

	return;

requeue:
	dev_info(nctrl->device, "Failed reconnect attempt %d\n",
		 nctrl->nr_reconnects);
	nvme_tcp_ofld_reconnect_or_remove(nctrl);
}

static void nvme_tcp_ofld_error_recovery_work(struct work_struct *work)
{
	struct nvme_tcp_ofld_ctrl *ctrl =
		container_of(work, struct nvme_tcp_ofld_ctrl, err_work);
	struct nvme_ctrl *nctrl = &ctrl->nctrl;

	nvme_stop_keep_alive(nctrl);
	nvme_tcp_ofld_teardown_io_queues(nctrl, false);
	/* unquiesce to fail fast pending requests */
	nvme_start_queues(nctrl);
	nvme_tcp_ofld_teardown_admin_queue(nctrl, false);
	blk_mq_unquiesce_queue(nctrl->admin_q);

	if (!nvme_change_ctrl_state(nctrl, NVME_CTRL_CONNECTING)) {
		/* state change failure is ok if we started nctrl delete */
		WARN_ON_ONCE(nctrl->state != NVME_CTRL_DELETING &&
			     nctrl->state != NVME_CTRL_DELETING_NOIO);

		return;
	}

	nvme_tcp_ofld_reconnect_or_remove(nctrl);
}

static void
nvme_tcp_ofld_teardown_ctrl(struct nvme_ctrl *nctrl, bool shutdown)
{
	struct nvme_tcp_ofld_ctrl *ctrl = to_tcp_ofld_ctrl(nctrl);

	cancel_work_sync(&ctrl->err_work);
	cancel_delayed_work_sync(&ctrl->connect_work);
	nvme_tcp_ofld_teardown_io_queues(nctrl, shutdown);
	blk_mq_quiesce_queue(nctrl->admin_q);
	if (shutdown)
		nvme_shutdown_ctrl(nctrl);
	else
		nvme_disable_ctrl(nctrl);
	nvme_tcp_ofld_teardown_admin_queue(nctrl, shutdown);
}

static void nvme_tcp_ofld_delete_ctrl(struct nvme_ctrl *nctrl)
{
	nvme_tcp_ofld_teardown_ctrl(nctrl, true);
}

static void nvme_tcp_ofld_reset_ctrl_work(struct work_struct *work)
{
	struct nvme_ctrl *nctrl =
		container_of(work, struct nvme_ctrl, reset_work);

	nvme_stop_ctrl(nctrl);
	nvme_tcp_ofld_teardown_ctrl(nctrl, false);

	if (!nvme_change_ctrl_state(nctrl, NVME_CTRL_CONNECTING)) {
		/* state change failure is ok if we started ctrl delete */
		WARN_ON_ONCE(nctrl->state != NVME_CTRL_DELETING &&
			     nctrl->state != NVME_CTRL_DELETING_NOIO);

		return;
	}

	if (nvme_tcp_ofld_setup_ctrl(nctrl, false))
		goto out_fail;

	return;

out_fail:
	++nctrl->nr_reconnects;
	nvme_tcp_ofld_reconnect_or_remove(nctrl);
}

static int
nvme_tcp_ofld_init_request(struct blk_mq_tag_set *set,
			   struct request *rq,
			   unsigned int hctx_idx,
			   unsigned int numa_node)
{
	struct nvme_tcp_ofld_req *req = blk_mq_rq_to_pdu(rq);
	struct nvme_tcp_ofld_ctrl *ctrl = set->driver_data;
	int qid;

	qid = (set == &ctrl->tag_set) ? hctx_idx + 1 : 0;
	req->queue = &ctrl->queues[qid];
	nvme_req(rq)->ctrl = &ctrl->nctrl;
	nvme_req(rq)->cmd = &req->nvme_cmd;
	req->done = nvme_tcp_ofld_req_done;

	return 0;
}

inline size_t nvme_tcp_ofld_inline_data_size(struct nvme_tcp_ofld_queue *queue)
{
	return queue->cmnd_capsule_len - sizeof(struct nvme_command);
}
EXPORT_SYMBOL_GPL(nvme_tcp_ofld_inline_data_size);

static blk_status_t
nvme_tcp_ofld_queue_rq(struct blk_mq_hw_ctx *hctx,
		       const struct blk_mq_queue_data *bd)
{
	struct nvme_tcp_ofld_req *req = blk_mq_rq_to_pdu(bd->rq);
	struct nvme_tcp_ofld_queue *queue = hctx->driver_data;
	struct nvme_tcp_ofld_ctrl *ctrl = queue->ctrl;
	struct nvme_ns *ns = hctx->queue->queuedata;
	struct nvme_tcp_ofld_dev *dev = queue->dev;
	struct nvme_tcp_ofld_ops *ops = dev->ops;
	struct nvme_command *nvme_cmd;
	struct request *rq = bd->rq;
	bool queue_ready;
	u32 data_len;
	int rc;

	queue_ready = test_bit(NVME_TCP_OFLD_Q_LIVE, &queue->flags);

	req->async = false;

	if (!nvme_check_ready(&ctrl->nctrl, rq, queue_ready))
		return nvme_fail_nonready_command(&ctrl->nctrl, rq);

	rc = nvme_setup_cmd(ns, rq);
	if (unlikely(rc))
		return rc;

	blk_mq_start_request(rq);

	nvme_cmd = &req->nvme_cmd;
	nvme_cmd->common.flags |= NVME_CMD_SGL_METABUF;

	data_len = blk_rq_nr_phys_segments(rq) ? blk_rq_payload_bytes(rq) : 0;
	if (!data_len)
		nvme_tcp_ofld_set_sg_null(&req->nvme_cmd);
	else if ((rq_data_dir(rq) == WRITE) &&
		 data_len <= nvme_tcp_ofld_inline_data_size(queue))
		nvme_tcp_ofld_set_sg_inline(queue, nvme_cmd, data_len);
	else
		nvme_tcp_ofld_map_data(nvme_cmd, data_len);

	rc = ops->send_req(req);
	if (unlikely(rc))
		return rc;

	return BLK_STS_OK;
}

static void
nvme_tcp_ofld_exit_request(struct blk_mq_tag_set *set,
			   struct request *rq, unsigned int hctx_idx)
{
	/*
	 * Nothing is allocated in nvme_tcp_ofld_init_request,
	 * hence empty.
	 */
}

static int
nvme_tcp_ofld_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
			unsigned int hctx_idx)
{
	struct nvme_tcp_ofld_ctrl *ctrl = data;

	hctx->driver_data = &ctrl->queues[hctx_idx + 1];

	return 0;
}

static int nvme_tcp_ofld_map_queues(struct blk_mq_tag_set *set)
{
	struct nvme_tcp_ofld_ctrl *ctrl = set->driver_data;
	struct nvmf_ctrl_options *opts = ctrl->nctrl.opts;

	if (opts->nr_write_queues && ctrl->io_queues[HCTX_TYPE_READ]) {
		/* separate read/write queues */
		set->map[HCTX_TYPE_DEFAULT].nr_queues =
			ctrl->io_queues[HCTX_TYPE_DEFAULT];
		set->map[HCTX_TYPE_DEFAULT].queue_offset = 0;
		set->map[HCTX_TYPE_READ].nr_queues =
			ctrl->io_queues[HCTX_TYPE_READ];
		set->map[HCTX_TYPE_READ].queue_offset =
			ctrl->io_queues[HCTX_TYPE_DEFAULT];
	} else {
		/* shared read/write queues */
		set->map[HCTX_TYPE_DEFAULT].nr_queues =
			ctrl->io_queues[HCTX_TYPE_DEFAULT];
		set->map[HCTX_TYPE_DEFAULT].queue_offset = 0;
		set->map[HCTX_TYPE_READ].nr_queues =
			ctrl->io_queues[HCTX_TYPE_DEFAULT];
		set->map[HCTX_TYPE_READ].queue_offset = 0;
	}
	blk_mq_map_queues(&set->map[HCTX_TYPE_DEFAULT]);
	blk_mq_map_queues(&set->map[HCTX_TYPE_READ]);

	if (opts->nr_poll_queues && ctrl->io_queues[HCTX_TYPE_POLL]) {
		/* map dedicated poll queues only if we have queues left */
		set->map[HCTX_TYPE_POLL].nr_queues =
				ctrl->io_queues[HCTX_TYPE_POLL];
		set->map[HCTX_TYPE_POLL].queue_offset =
			ctrl->io_queues[HCTX_TYPE_DEFAULT] +
			ctrl->io_queues[HCTX_TYPE_READ];
		blk_mq_map_queues(&set->map[HCTX_TYPE_POLL]);
	}

	dev_info(ctrl->nctrl.device,
		 "mapped %d/%d/%d default/read/poll queues.\n",
		 ctrl->io_queues[HCTX_TYPE_DEFAULT],
		 ctrl->io_queues[HCTX_TYPE_READ],
		 ctrl->io_queues[HCTX_TYPE_POLL]);

	return 0;
}

static int nvme_tcp_ofld_poll(struct blk_mq_hw_ctx *hctx)
{
	struct nvme_tcp_ofld_queue *queue = hctx->driver_data;
	struct nvme_tcp_ofld_dev *dev = queue->dev;
	struct nvme_tcp_ofld_ops *ops = dev->ops;

	return ops->poll_queue(queue);
}

static void nvme_tcp_ofld_complete_timed_out(struct request *rq)
{
	struct nvme_tcp_ofld_req *req = blk_mq_rq_to_pdu(rq);
	struct nvme_ctrl *nctrl = &req->queue->ctrl->nctrl;

	nvme_tcp_ofld_stop_queue(nctrl, nvme_tcp_ofld_qid(req->queue));
	if (blk_mq_request_started(rq) && !blk_mq_request_completed(rq)) {
		nvme_req(rq)->status = NVME_SC_HOST_ABORTED_CMD;
		blk_mq_complete_request(rq);
	}
}

static enum blk_eh_timer_return nvme_tcp_ofld_timeout(struct request *rq, bool reserved)
{
	struct nvme_tcp_ofld_req *req = blk_mq_rq_to_pdu(rq);
	struct nvme_tcp_ofld_ctrl *ctrl = req->queue->ctrl;

	dev_warn(ctrl->nctrl.device,
		 "queue %d: timeout request %#x type %d\n",
		 nvme_tcp_ofld_qid(req->queue), rq->tag, req->nvme_cmd.common.opcode);

	if (ctrl->nctrl.state != NVME_CTRL_LIVE) {
		/*
		 * If we are resetting, connecting or deleting we should
		 * complete immediately because we may block controller
		 * teardown or setup sequence
		 * - ctrl disable/shutdown fabrics requests
		 * - connect requests
		 * - initialization admin requests
		 * - I/O requests that entered after unquiescing and
		 *   the controller stopped responding
		 *
		 * All other requests should be cancelled by the error
		 * recovery work, so it's fine that we fail it here.
		 */
		nvme_tcp_ofld_complete_timed_out(rq);

		return BLK_EH_DONE;
	}

	nvme_tcp_ofld_error_recovery(&ctrl->nctrl);

	return BLK_EH_RESET_TIMER;
}

static struct blk_mq_ops nvme_tcp_ofld_mq_ops = {
	.queue_rq	= nvme_tcp_ofld_queue_rq,
	.complete	= nvme_complete_rq,
	.init_request	= nvme_tcp_ofld_init_request,
	.exit_request	= nvme_tcp_ofld_exit_request,
	.init_hctx	= nvme_tcp_ofld_init_hctx,
	.timeout	= nvme_tcp_ofld_timeout,
	.map_queues	= nvme_tcp_ofld_map_queues,
	.poll		= nvme_tcp_ofld_poll,
};

static struct blk_mq_ops nvme_tcp_ofld_admin_mq_ops = {
	.queue_rq	= nvme_tcp_ofld_queue_rq,
	.complete	= nvme_complete_rq,
	.init_request	= nvme_tcp_ofld_init_request,
	.exit_request	= nvme_tcp_ofld_exit_request,
	.init_hctx	= nvme_tcp_ofld_init_admin_hctx,
	.timeout	= nvme_tcp_ofld_timeout,
};

static const struct nvme_ctrl_ops nvme_tcp_ofld_ctrl_ops = {
	.name			= "tcp_offload",
	.module			= THIS_MODULE,
	.flags			= NVME_F_FABRICS,
	.reg_read32		= nvmf_reg_read32,
	.reg_read64		= nvmf_reg_read64,
	.reg_write32		= nvmf_reg_write32,
	.free_ctrl		= nvme_tcp_ofld_free_ctrl,
	.submit_async_event     = nvme_tcp_ofld_submit_async_event,
	.delete_ctrl		= nvme_tcp_ofld_delete_ctrl,
	.get_address		= nvmf_get_address,
};

static bool
nvme_tcp_ofld_existing_controller(struct nvmf_ctrl_options *opts)
{
	struct nvme_tcp_ofld_ctrl *ctrl;
	bool found = false;

	mutex_lock(&nvme_tcp_ofld_ctrl_mutex);
	list_for_each_entry(ctrl, &nvme_tcp_ofld_ctrl_list, list) {
		found = nvmf_ip_options_match(&ctrl->nctrl, opts);
		if (found)
			break;
	}
	mutex_unlock(&nvme_tcp_ofld_ctrl_mutex);

	return found;
}

static struct nvme_ctrl *
nvme_tcp_ofld_create_ctrl(struct device *ndev, struct nvmf_ctrl_options *opts)
{
	struct nvme_tcp_ofld_queue *queue;
	struct nvme_tcp_ofld_ctrl *ctrl;
	struct nvme_tcp_ofld_dev *dev;
	struct nvme_ctrl *nctrl;
	int i, rc = 0;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&ctrl->list);
	nctrl = &ctrl->nctrl;
	nctrl->opts = opts;
	nctrl->queue_count = opts->nr_io_queues + opts->nr_write_queues +
			     opts->nr_poll_queues + 1;
	nctrl->sqsize = opts->queue_size - 1;
	nctrl->kato = opts->kato;
	INIT_DELAYED_WORK(&ctrl->connect_work,
			  nvme_tcp_ofld_reconnect_ctrl_work);
	INIT_WORK(&ctrl->err_work, nvme_tcp_ofld_error_recovery_work);
	INIT_WORK(&nctrl->reset_work, nvme_tcp_ofld_reset_ctrl_work);
	if (!(opts->mask & NVMF_OPT_TRSVCID)) {
		opts->trsvcid =
			kstrdup(__stringify(NVME_TCP_DISC_PORT), GFP_KERNEL);
		if (!opts->trsvcid) {
			rc = -ENOMEM;
			goto out_free_ctrl;
		}
		opts->mask |= NVMF_OPT_TRSVCID;
	}

	rc = inet_pton_with_scope(&init_net, AF_UNSPEC, opts->traddr,
				  opts->trsvcid,
				  &ctrl->conn_params.remote_ip_addr);
	if (rc) {
		pr_err("malformed address passed: %s:%s\n",
		       opts->traddr, opts->trsvcid);
		goto out_free_ctrl;
	}

	if (opts->mask & NVMF_OPT_HOST_TRADDR) {
		rc = inet_pton_with_scope(&init_net, AF_UNSPEC,
					  opts->host_traddr, NULL,
					  &ctrl->conn_params.local_ip_addr);
		if (rc) {
			pr_err("malformed src address passed: %s\n",
			       opts->host_traddr);
			goto out_free_ctrl;
		}
	}

	if (!opts->duplicate_connect &&
	    nvme_tcp_ofld_existing_controller(opts)) {
		rc = -EALREADY;
		goto out_free_ctrl;
	}

	/* Find device that can reach the dest addr */
	dev = nvme_tcp_ofld_lookup_dev(ctrl);
	if (!dev) {
		pr_info("no device found for addr %s:%s.\n",
			opts->traddr, opts->trsvcid);
		rc = -EINVAL;
		goto out_free_ctrl;
	}

	/* Increase driver refcnt */
	if (!try_module_get(dev->ops->module)) {
		pr_err("try_module_get failed\n");
		dev = NULL;
		goto out_free_ctrl;
	}

	rc = nvme_tcp_ofld_check_dev_opts(opts, dev->ops);
	if (rc)
		goto out_module_put;

	ctrl->dev = dev;

	if (ctrl->dev->ops->max_hw_sectors)
		nctrl->max_hw_sectors = ctrl->dev->ops->max_hw_sectors;
	if (ctrl->dev->ops->max_segments)
		nctrl->max_segments = ctrl->dev->ops->max_segments;

	ctrl->queues = kcalloc(nctrl->queue_count,
			       sizeof(struct nvme_tcp_ofld_queue),
			       GFP_KERNEL);
	if (!ctrl->queues) {
		rc = -ENOMEM;
		goto out_module_put;
	}

	for (i = 0; i < nctrl->queue_count; ++i) {
		queue = &ctrl->queues[i];
		queue->ctrl = ctrl;
		queue->dev = dev;
		queue->report_err = nvme_tcp_ofld_report_queue_err;
	}

	rc = nvme_init_ctrl(nctrl, ndev, &nvme_tcp_ofld_ctrl_ops, 0);
	if (rc)
		goto out_free_queues;

	if (!nvme_change_ctrl_state(nctrl, NVME_CTRL_CONNECTING)) {
		WARN_ON_ONCE(1);
		rc = -EINTR;
		goto out_uninit_ctrl;
	}

	rc = nvme_tcp_ofld_setup_ctrl(nctrl, true);
	if (rc)
		goto out_uninit_ctrl;

	dev_info(nctrl->device, "new ctrl: NQN \"%s\", addr %pISp\n",
		 opts->subsysnqn, &ctrl->conn_params.remote_ip_addr);

	mutex_lock(&nvme_tcp_ofld_ctrl_mutex);
	list_add_tail(&ctrl->list, &nvme_tcp_ofld_ctrl_list);
	mutex_unlock(&nvme_tcp_ofld_ctrl_mutex);

	return nctrl;

out_uninit_ctrl:
	nvme_uninit_ctrl(nctrl);
	nvme_put_ctrl(nctrl);
out_free_queues:
	kfree(ctrl->queues);
out_module_put:
	module_put(dev->ops->module);
out_free_ctrl:
	kfree(ctrl);

	return ERR_PTR(rc);
}

static struct nvmf_transport_ops nvme_tcp_ofld_transport = {
	.name		= "tcp_offload",
	.module		= THIS_MODULE,
	.required_opts	= NVMF_OPT_TRADDR,
	.allowed_opts	= NVMF_OPT_TRSVCID | NVMF_OPT_NR_WRITE_QUEUES  |
			  NVMF_OPT_HOST_TRADDR | NVMF_OPT_CTRL_LOSS_TMO |
			  NVMF_OPT_RECONNECT_DELAY | NVMF_OPT_HDR_DIGEST |
			  NVMF_OPT_DATA_DIGEST | NVMF_OPT_NR_POLL_QUEUES |
			  NVMF_OPT_TOS,
	.create_ctrl	= nvme_tcp_ofld_create_ctrl,
};

static int __init nvme_tcp_ofld_init_module(void)
{
	nvmf_register_transport(&nvme_tcp_ofld_transport);

	return 0;
}

static void __exit nvme_tcp_ofld_cleanup_module(void)
{
	struct nvme_tcp_ofld_ctrl *ctrl;

	nvmf_unregister_transport(&nvme_tcp_ofld_transport);

	mutex_lock(&nvme_tcp_ofld_ctrl_mutex);
	list_for_each_entry(ctrl, &nvme_tcp_ofld_ctrl_list, list)
		nvme_delete_ctrl(&ctrl->nctrl);
	mutex_unlock(&nvme_tcp_ofld_ctrl_mutex);
	flush_workqueue(nvme_delete_wq);
}

module_init(nvme_tcp_ofld_init_module);
module_exit(nvme_tcp_ofld_cleanup_module);
MODULE_LICENSE("GPL v2");
