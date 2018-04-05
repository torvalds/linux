/*
 * NVMe over Fabrics loopback device.
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/scatterlist.h>
#include <linux/blk-mq.h>
#include <linux/nvme.h>
#include <linux/module.h>
#include <linux/parser.h>
#include "nvmet.h"
#include "../host/nvme.h"
#include "../host/fabrics.h"

#define NVME_LOOP_MAX_SEGMENTS		256

struct nvme_loop_iod {
	struct nvme_request	nvme_req;
	struct nvme_command	cmd;
	struct nvme_completion	rsp;
	struct nvmet_req	req;
	struct nvme_loop_queue	*queue;
	struct work_struct	work;
	struct sg_table		sg_table;
	struct scatterlist	first_sgl[];
};

struct nvme_loop_ctrl {
	struct nvme_loop_queue	*queues;

	struct blk_mq_tag_set	admin_tag_set;

	struct list_head	list;
	struct blk_mq_tag_set	tag_set;
	struct nvme_loop_iod	async_event_iod;
	struct nvme_ctrl	ctrl;

	struct nvmet_ctrl	*target_ctrl;
};

static inline struct nvme_loop_ctrl *to_loop_ctrl(struct nvme_ctrl *ctrl)
{
	return container_of(ctrl, struct nvme_loop_ctrl, ctrl);
}

enum nvme_loop_queue_flags {
	NVME_LOOP_Q_LIVE	= 0,
};

struct nvme_loop_queue {
	struct nvmet_cq		nvme_cq;
	struct nvmet_sq		nvme_sq;
	struct nvme_loop_ctrl	*ctrl;
	unsigned long		flags;
};

static struct nvmet_port *nvmet_loop_port;

static LIST_HEAD(nvme_loop_ctrl_list);
static DEFINE_MUTEX(nvme_loop_ctrl_mutex);

static void nvme_loop_queue_response(struct nvmet_req *nvme_req);
static void nvme_loop_delete_ctrl(struct nvmet_ctrl *ctrl);

static const struct nvmet_fabrics_ops nvme_loop_ops;

static inline int nvme_loop_queue_idx(struct nvme_loop_queue *queue)
{
	return queue - queue->ctrl->queues;
}

static void nvme_loop_complete_rq(struct request *req)
{
	struct nvme_loop_iod *iod = blk_mq_rq_to_pdu(req);

	nvme_cleanup_cmd(req);
	sg_free_table_chained(&iod->sg_table, true);
	nvme_complete_rq(req);
}

static struct blk_mq_tags *nvme_loop_tagset(struct nvme_loop_queue *queue)
{
	u32 queue_idx = nvme_loop_queue_idx(queue);

	if (queue_idx == 0)
		return queue->ctrl->admin_tag_set.tags[queue_idx];
	return queue->ctrl->tag_set.tags[queue_idx - 1];
}

static void nvme_loop_queue_response(struct nvmet_req *req)
{
	struct nvme_loop_queue *queue =
		container_of(req->sq, struct nvme_loop_queue, nvme_sq);
	struct nvme_completion *cqe = req->rsp;

	/*
	 * AEN requests are special as they don't time out and can
	 * survive any kind of queue freeze and often don't respond to
	 * aborts.  We don't even bother to allocate a struct request
	 * for them but rather special case them here.
	 */
	if (unlikely(nvme_loop_queue_idx(queue) == 0 &&
			cqe->command_id >= NVME_AQ_BLK_MQ_DEPTH)) {
		nvme_complete_async_event(&queue->ctrl->ctrl, cqe->status,
				&cqe->result);
	} else {
		struct request *rq;

		rq = blk_mq_tag_to_rq(nvme_loop_tagset(queue), cqe->command_id);
		if (!rq) {
			dev_err(queue->ctrl->ctrl.device,
				"tag 0x%x on queue %d not found\n",
				cqe->command_id, nvme_loop_queue_idx(queue));
			return;
		}

		nvme_end_request(rq, cqe->status, cqe->result);
	}
}

static void nvme_loop_execute_work(struct work_struct *work)
{
	struct nvme_loop_iod *iod =
		container_of(work, struct nvme_loop_iod, work);

	nvmet_req_execute(&iod->req);
}

static enum blk_eh_timer_return
nvme_loop_timeout(struct request *rq, bool reserved)
{
	struct nvme_loop_iod *iod = blk_mq_rq_to_pdu(rq);

	/* queue error recovery */
	nvme_reset_ctrl(&iod->queue->ctrl->ctrl);

	/* fail with DNR on admin cmd timeout */
	nvme_req(rq)->status = NVME_SC_ABORT_REQ | NVME_SC_DNR;

	return BLK_EH_HANDLED;
}

static inline blk_status_t nvme_loop_is_ready(struct nvme_loop_queue *queue,
		struct request *rq)
{
	if (unlikely(!test_bit(NVME_LOOP_Q_LIVE, &queue->flags)))
		return nvmf_check_init_req(&queue->ctrl->ctrl, rq);
	return BLK_STS_OK;
}

static blk_status_t nvme_loop_queue_rq(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd)
{
	struct nvme_ns *ns = hctx->queue->queuedata;
	struct nvme_loop_queue *queue = hctx->driver_data;
	struct request *req = bd->rq;
	struct nvme_loop_iod *iod = blk_mq_rq_to_pdu(req);
	blk_status_t ret;

	ret = nvme_loop_is_ready(queue, req);
	if (unlikely(ret))
		return ret;

	ret = nvme_setup_cmd(ns, req, &iod->cmd);
	if (ret)
		return ret;

	iod->cmd.common.flags |= NVME_CMD_SGL_METABUF;
	iod->req.port = nvmet_loop_port;
	if (!nvmet_req_init(&iod->req, &queue->nvme_cq,
			&queue->nvme_sq, &nvme_loop_ops)) {
		nvme_cleanup_cmd(req);
		blk_mq_start_request(req);
		nvme_loop_queue_response(&iod->req);
		return BLK_STS_OK;
	}

	if (blk_rq_payload_bytes(req)) {
		iod->sg_table.sgl = iod->first_sgl;
		if (sg_alloc_table_chained(&iod->sg_table,
				blk_rq_nr_phys_segments(req),
				iod->sg_table.sgl))
			return BLK_STS_RESOURCE;

		iod->req.sg = iod->sg_table.sgl;
		iod->req.sg_cnt = blk_rq_map_sg(req->q, req, iod->sg_table.sgl);
		iod->req.transfer_len = blk_rq_payload_bytes(req);
	}

	blk_mq_start_request(req);

	schedule_work(&iod->work);
	return BLK_STS_OK;
}

static void nvme_loop_submit_async_event(struct nvme_ctrl *arg)
{
	struct nvme_loop_ctrl *ctrl = to_loop_ctrl(arg);
	struct nvme_loop_queue *queue = &ctrl->queues[0];
	struct nvme_loop_iod *iod = &ctrl->async_event_iod;

	memset(&iod->cmd, 0, sizeof(iod->cmd));
	iod->cmd.common.opcode = nvme_admin_async_event;
	iod->cmd.common.command_id = NVME_AQ_BLK_MQ_DEPTH;
	iod->cmd.common.flags |= NVME_CMD_SGL_METABUF;

	if (!nvmet_req_init(&iod->req, &queue->nvme_cq, &queue->nvme_sq,
			&nvme_loop_ops)) {
		dev_err(ctrl->ctrl.device, "failed async event work\n");
		return;
	}

	schedule_work(&iod->work);
}

static int nvme_loop_init_iod(struct nvme_loop_ctrl *ctrl,
		struct nvme_loop_iod *iod, unsigned int queue_idx)
{
	iod->req.cmd = &iod->cmd;
	iod->req.rsp = &iod->rsp;
	iod->queue = &ctrl->queues[queue_idx];
	INIT_WORK(&iod->work, nvme_loop_execute_work);
	return 0;
}

static int nvme_loop_init_request(struct blk_mq_tag_set *set,
		struct request *req, unsigned int hctx_idx,
		unsigned int numa_node)
{
	struct nvme_loop_ctrl *ctrl = set->driver_data;

	return nvme_loop_init_iod(ctrl, blk_mq_rq_to_pdu(req),
			(set == &ctrl->tag_set) ? hctx_idx + 1 : 0);
}

static int nvme_loop_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
		unsigned int hctx_idx)
{
	struct nvme_loop_ctrl *ctrl = data;
	struct nvme_loop_queue *queue = &ctrl->queues[hctx_idx + 1];

	BUG_ON(hctx_idx >= ctrl->ctrl.queue_count);

	hctx->driver_data = queue;
	return 0;
}

static int nvme_loop_init_admin_hctx(struct blk_mq_hw_ctx *hctx, void *data,
		unsigned int hctx_idx)
{
	struct nvme_loop_ctrl *ctrl = data;
	struct nvme_loop_queue *queue = &ctrl->queues[0];

	BUG_ON(hctx_idx != 0);

	hctx->driver_data = queue;
	return 0;
}

static const struct blk_mq_ops nvme_loop_mq_ops = {
	.queue_rq	= nvme_loop_queue_rq,
	.complete	= nvme_loop_complete_rq,
	.init_request	= nvme_loop_init_request,
	.init_hctx	= nvme_loop_init_hctx,
	.timeout	= nvme_loop_timeout,
};

static const struct blk_mq_ops nvme_loop_admin_mq_ops = {
	.queue_rq	= nvme_loop_queue_rq,
	.complete	= nvme_loop_complete_rq,
	.init_request	= nvme_loop_init_request,
	.init_hctx	= nvme_loop_init_admin_hctx,
	.timeout	= nvme_loop_timeout,
};

static void nvme_loop_destroy_admin_queue(struct nvme_loop_ctrl *ctrl)
{
	clear_bit(NVME_LOOP_Q_LIVE, &ctrl->queues[0].flags);
	nvmet_sq_destroy(&ctrl->queues[0].nvme_sq);
	blk_cleanup_queue(ctrl->ctrl.admin_q);
	blk_mq_free_tag_set(&ctrl->admin_tag_set);
}

static void nvme_loop_free_ctrl(struct nvme_ctrl *nctrl)
{
	struct nvme_loop_ctrl *ctrl = to_loop_ctrl(nctrl);

	if (list_empty(&ctrl->list))
		goto free_ctrl;

	mutex_lock(&nvme_loop_ctrl_mutex);
	list_del(&ctrl->list);
	mutex_unlock(&nvme_loop_ctrl_mutex);

	if (nctrl->tagset) {
		blk_cleanup_queue(ctrl->ctrl.connect_q);
		blk_mq_free_tag_set(&ctrl->tag_set);
	}
	kfree(ctrl->queues);
	nvmf_free_options(nctrl->opts);
free_ctrl:
	kfree(ctrl);
}

static void nvme_loop_destroy_io_queues(struct nvme_loop_ctrl *ctrl)
{
	int i;

	for (i = 1; i < ctrl->ctrl.queue_count; i++) {
		clear_bit(NVME_LOOP_Q_LIVE, &ctrl->queues[i].flags);
		nvmet_sq_destroy(&ctrl->queues[i].nvme_sq);
	}
}

static int nvme_loop_init_io_queues(struct nvme_loop_ctrl *ctrl)
{
	struct nvmf_ctrl_options *opts = ctrl->ctrl.opts;
	unsigned int nr_io_queues;
	int ret, i;

	nr_io_queues = min(opts->nr_io_queues, num_online_cpus());
	ret = nvme_set_queue_count(&ctrl->ctrl, &nr_io_queues);
	if (ret || !nr_io_queues)
		return ret;

	dev_info(ctrl->ctrl.device, "creating %d I/O queues.\n", nr_io_queues);

	for (i = 1; i <= nr_io_queues; i++) {
		ctrl->queues[i].ctrl = ctrl;
		ret = nvmet_sq_init(&ctrl->queues[i].nvme_sq);
		if (ret)
			goto out_destroy_queues;

		ctrl->ctrl.queue_count++;
	}

	return 0;

out_destroy_queues:
	nvme_loop_destroy_io_queues(ctrl);
	return ret;
}

static int nvme_loop_connect_io_queues(struct nvme_loop_ctrl *ctrl)
{
	int i, ret;

	for (i = 1; i < ctrl->ctrl.queue_count; i++) {
		ret = nvmf_connect_io_queue(&ctrl->ctrl, i);
		if (ret)
			return ret;
		set_bit(NVME_LOOP_Q_LIVE, &ctrl->queues[i].flags);
	}

	return 0;
}

static int nvme_loop_configure_admin_queue(struct nvme_loop_ctrl *ctrl)
{
	int error;

	memset(&ctrl->admin_tag_set, 0, sizeof(ctrl->admin_tag_set));
	ctrl->admin_tag_set.ops = &nvme_loop_admin_mq_ops;
	ctrl->admin_tag_set.queue_depth = NVME_AQ_MQ_TAG_DEPTH;
	ctrl->admin_tag_set.reserved_tags = 2; /* connect + keep-alive */
	ctrl->admin_tag_set.numa_node = NUMA_NO_NODE;
	ctrl->admin_tag_set.cmd_size = sizeof(struct nvme_loop_iod) +
		SG_CHUNK_SIZE * sizeof(struct scatterlist);
	ctrl->admin_tag_set.driver_data = ctrl;
	ctrl->admin_tag_set.nr_hw_queues = 1;
	ctrl->admin_tag_set.timeout = ADMIN_TIMEOUT;
	ctrl->admin_tag_set.flags = BLK_MQ_F_NO_SCHED;

	ctrl->queues[0].ctrl = ctrl;
	error = nvmet_sq_init(&ctrl->queues[0].nvme_sq);
	if (error)
		return error;
	ctrl->ctrl.queue_count = 1;

	error = blk_mq_alloc_tag_set(&ctrl->admin_tag_set);
	if (error)
		goto out_free_sq;
	ctrl->ctrl.admin_tagset = &ctrl->admin_tag_set;

	ctrl->ctrl.admin_q = blk_mq_init_queue(&ctrl->admin_tag_set);
	if (IS_ERR(ctrl->ctrl.admin_q)) {
		error = PTR_ERR(ctrl->ctrl.admin_q);
		goto out_free_tagset;
	}

	error = nvmf_connect_admin_queue(&ctrl->ctrl);
	if (error)
		goto out_cleanup_queue;

	set_bit(NVME_LOOP_Q_LIVE, &ctrl->queues[0].flags);

	error = nvmf_reg_read64(&ctrl->ctrl, NVME_REG_CAP, &ctrl->ctrl.cap);
	if (error) {
		dev_err(ctrl->ctrl.device,
			"prop_get NVME_REG_CAP failed\n");
		goto out_cleanup_queue;
	}

	ctrl->ctrl.sqsize =
		min_t(int, NVME_CAP_MQES(ctrl->ctrl.cap), ctrl->ctrl.sqsize);

	error = nvme_enable_ctrl(&ctrl->ctrl, ctrl->ctrl.cap);
	if (error)
		goto out_cleanup_queue;

	ctrl->ctrl.max_hw_sectors =
		(NVME_LOOP_MAX_SEGMENTS - 1) << (PAGE_SHIFT - 9);

	error = nvme_init_identify(&ctrl->ctrl);
	if (error)
		goto out_cleanup_queue;

	return 0;

out_cleanup_queue:
	blk_cleanup_queue(ctrl->ctrl.admin_q);
out_free_tagset:
	blk_mq_free_tag_set(&ctrl->admin_tag_set);
out_free_sq:
	nvmet_sq_destroy(&ctrl->queues[0].nvme_sq);
	return error;
}

static void nvme_loop_shutdown_ctrl(struct nvme_loop_ctrl *ctrl)
{
	if (ctrl->ctrl.queue_count > 1) {
		nvme_stop_queues(&ctrl->ctrl);
		blk_mq_tagset_busy_iter(&ctrl->tag_set,
					nvme_cancel_request, &ctrl->ctrl);
		nvme_loop_destroy_io_queues(ctrl);
	}

	if (ctrl->ctrl.state == NVME_CTRL_LIVE)
		nvme_shutdown_ctrl(&ctrl->ctrl);

	blk_mq_quiesce_queue(ctrl->ctrl.admin_q);
	blk_mq_tagset_busy_iter(&ctrl->admin_tag_set,
				nvme_cancel_request, &ctrl->ctrl);
	blk_mq_unquiesce_queue(ctrl->ctrl.admin_q);
	nvme_loop_destroy_admin_queue(ctrl);
}

static void nvme_loop_delete_ctrl_host(struct nvme_ctrl *ctrl)
{
	nvme_loop_shutdown_ctrl(to_loop_ctrl(ctrl));
}

static void nvme_loop_delete_ctrl(struct nvmet_ctrl *nctrl)
{
	struct nvme_loop_ctrl *ctrl;

	mutex_lock(&nvme_loop_ctrl_mutex);
	list_for_each_entry(ctrl, &nvme_loop_ctrl_list, list) {
		if (ctrl->ctrl.cntlid == nctrl->cntlid)
			nvme_delete_ctrl(&ctrl->ctrl);
	}
	mutex_unlock(&nvme_loop_ctrl_mutex);
}

static void nvme_loop_reset_ctrl_work(struct work_struct *work)
{
	struct nvme_loop_ctrl *ctrl =
		container_of(work, struct nvme_loop_ctrl, ctrl.reset_work);
	bool changed;
	int ret;

	nvme_stop_ctrl(&ctrl->ctrl);
	nvme_loop_shutdown_ctrl(ctrl);

	ret = nvme_loop_configure_admin_queue(ctrl);
	if (ret)
		goto out_disable;

	ret = nvme_loop_init_io_queues(ctrl);
	if (ret)
		goto out_destroy_admin;

	ret = nvme_loop_connect_io_queues(ctrl);
	if (ret)
		goto out_destroy_io;

	blk_mq_update_nr_hw_queues(&ctrl->tag_set,
			ctrl->ctrl.queue_count - 1);

	changed = nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_LIVE);
	WARN_ON_ONCE(!changed);

	nvme_start_ctrl(&ctrl->ctrl);

	return;

out_destroy_io:
	nvme_loop_destroy_io_queues(ctrl);
out_destroy_admin:
	nvme_loop_destroy_admin_queue(ctrl);
out_disable:
	dev_warn(ctrl->ctrl.device, "Removing after reset failure\n");
	nvme_uninit_ctrl(&ctrl->ctrl);
	nvme_put_ctrl(&ctrl->ctrl);
}

static const struct nvme_ctrl_ops nvme_loop_ctrl_ops = {
	.name			= "loop",
	.module			= THIS_MODULE,
	.flags			= NVME_F_FABRICS,
	.reg_read32		= nvmf_reg_read32,
	.reg_read64		= nvmf_reg_read64,
	.reg_write32		= nvmf_reg_write32,
	.free_ctrl		= nvme_loop_free_ctrl,
	.submit_async_event	= nvme_loop_submit_async_event,
	.delete_ctrl		= nvme_loop_delete_ctrl_host,
};

static int nvme_loop_create_io_queues(struct nvme_loop_ctrl *ctrl)
{
	int ret;

	ret = nvme_loop_init_io_queues(ctrl);
	if (ret)
		return ret;

	memset(&ctrl->tag_set, 0, sizeof(ctrl->tag_set));
	ctrl->tag_set.ops = &nvme_loop_mq_ops;
	ctrl->tag_set.queue_depth = ctrl->ctrl.opts->queue_size;
	ctrl->tag_set.reserved_tags = 1; /* fabric connect */
	ctrl->tag_set.numa_node = NUMA_NO_NODE;
	ctrl->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	ctrl->tag_set.cmd_size = sizeof(struct nvme_loop_iod) +
		SG_CHUNK_SIZE * sizeof(struct scatterlist);
	ctrl->tag_set.driver_data = ctrl;
	ctrl->tag_set.nr_hw_queues = ctrl->ctrl.queue_count - 1;
	ctrl->tag_set.timeout = NVME_IO_TIMEOUT;
	ctrl->ctrl.tagset = &ctrl->tag_set;

	ret = blk_mq_alloc_tag_set(&ctrl->tag_set);
	if (ret)
		goto out_destroy_queues;

	ctrl->ctrl.connect_q = blk_mq_init_queue(&ctrl->tag_set);
	if (IS_ERR(ctrl->ctrl.connect_q)) {
		ret = PTR_ERR(ctrl->ctrl.connect_q);
		goto out_free_tagset;
	}

	ret = nvme_loop_connect_io_queues(ctrl);
	if (ret)
		goto out_cleanup_connect_q;

	return 0;

out_cleanup_connect_q:
	blk_cleanup_queue(ctrl->ctrl.connect_q);
out_free_tagset:
	blk_mq_free_tag_set(&ctrl->tag_set);
out_destroy_queues:
	nvme_loop_destroy_io_queues(ctrl);
	return ret;
}

static struct nvme_ctrl *nvme_loop_create_ctrl(struct device *dev,
		struct nvmf_ctrl_options *opts)
{
	struct nvme_loop_ctrl *ctrl;
	bool changed;
	int ret;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return ERR_PTR(-ENOMEM);
	ctrl->ctrl.opts = opts;
	INIT_LIST_HEAD(&ctrl->list);

	INIT_WORK(&ctrl->ctrl.reset_work, nvme_loop_reset_ctrl_work);

	ret = nvme_init_ctrl(&ctrl->ctrl, dev, &nvme_loop_ctrl_ops,
				0 /* no quirks, we're perfect! */);
	if (ret)
		goto out_put_ctrl;

	ret = -ENOMEM;

	ctrl->ctrl.sqsize = opts->queue_size - 1;
	ctrl->ctrl.kato = opts->kato;

	ctrl->queues = kcalloc(opts->nr_io_queues + 1, sizeof(*ctrl->queues),
			GFP_KERNEL);
	if (!ctrl->queues)
		goto out_uninit_ctrl;

	ret = nvme_loop_configure_admin_queue(ctrl);
	if (ret)
		goto out_free_queues;

	if (opts->queue_size > ctrl->ctrl.maxcmd) {
		/* warn if maxcmd is lower than queue_size */
		dev_warn(ctrl->ctrl.device,
			"queue_size %zu > ctrl maxcmd %u, clamping down\n",
			opts->queue_size, ctrl->ctrl.maxcmd);
		opts->queue_size = ctrl->ctrl.maxcmd;
	}

	if (opts->nr_io_queues) {
		ret = nvme_loop_create_io_queues(ctrl);
		if (ret)
			goto out_remove_admin_queue;
	}

	nvme_loop_init_iod(ctrl, &ctrl->async_event_iod, 0);

	dev_info(ctrl->ctrl.device,
		 "new ctrl: \"%s\"\n", ctrl->ctrl.opts->subsysnqn);

	nvme_get_ctrl(&ctrl->ctrl);

	changed = nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_LIVE);
	WARN_ON_ONCE(!changed);

	mutex_lock(&nvme_loop_ctrl_mutex);
	list_add_tail(&ctrl->list, &nvme_loop_ctrl_list);
	mutex_unlock(&nvme_loop_ctrl_mutex);

	nvme_start_ctrl(&ctrl->ctrl);

	return &ctrl->ctrl;

out_remove_admin_queue:
	nvme_loop_destroy_admin_queue(ctrl);
out_free_queues:
	kfree(ctrl->queues);
out_uninit_ctrl:
	nvme_uninit_ctrl(&ctrl->ctrl);
out_put_ctrl:
	nvme_put_ctrl(&ctrl->ctrl);
	if (ret > 0)
		ret = -EIO;
	return ERR_PTR(ret);
}

static int nvme_loop_add_port(struct nvmet_port *port)
{
	/*
	 * XXX: disalow adding more than one port so
	 * there is no connection rejections when a
	 * a subsystem is assigned to a port for which
	 * loop doesn't have a pointer.
	 * This scenario would be possible if we allowed
	 * more than one port to be added and a subsystem
	 * was assigned to a port other than nvmet_loop_port.
	 */

	if (nvmet_loop_port)
		return -EPERM;

	nvmet_loop_port = port;
	return 0;
}

static void nvme_loop_remove_port(struct nvmet_port *port)
{
	if (port == nvmet_loop_port)
		nvmet_loop_port = NULL;
}

static const struct nvmet_fabrics_ops nvme_loop_ops = {
	.owner		= THIS_MODULE,
	.type		= NVMF_TRTYPE_LOOP,
	.add_port	= nvme_loop_add_port,
	.remove_port	= nvme_loop_remove_port,
	.queue_response = nvme_loop_queue_response,
	.delete_ctrl	= nvme_loop_delete_ctrl,
};

static struct nvmf_transport_ops nvme_loop_transport = {
	.name		= "loop",
	.module		= THIS_MODULE,
	.create_ctrl	= nvme_loop_create_ctrl,
};

static int __init nvme_loop_init_module(void)
{
	int ret;

	ret = nvmet_register_transport(&nvme_loop_ops);
	if (ret)
		return ret;

	ret = nvmf_register_transport(&nvme_loop_transport);
	if (ret)
		nvmet_unregister_transport(&nvme_loop_ops);

	return ret;
}

static void __exit nvme_loop_cleanup_module(void)
{
	struct nvme_loop_ctrl *ctrl, *next;

	nvmf_unregister_transport(&nvme_loop_transport);
	nvmet_unregister_transport(&nvme_loop_ops);

	mutex_lock(&nvme_loop_ctrl_mutex);
	list_for_each_entry_safe(ctrl, next, &nvme_loop_ctrl_list, list)
		nvme_delete_ctrl(&ctrl->ctrl);
	mutex_unlock(&nvme_loop_ctrl_mutex);

	flush_workqueue(nvme_delete_wq);
}

module_init(nvme_loop_init_module);
module_exit(nvme_loop_cleanup_module);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("nvmet-transport-254"); /* 254 == NVMF_TRTYPE_LOOP */
