// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "hab.h"

#define HAB_OPEN_REQ_EXPIRE_TIME_S (3600*10)

void hab_open_request_init(struct hab_open_request *request,
		int type,
		struct physical_channel *pchan,
		int vchan_id,
		int sub_id,
		int open_id)
{
	request->type = type;
	request->pchan = pchan;
	request->xdata.vchan_id = vchan_id;
	request->xdata.sub_id = sub_id;
	request->xdata.open_id = open_id;
	request->xdata.ver_proto = HAB_VER_PROT;
}

int hab_open_request_send(struct hab_open_request *request)
{
	struct hab_header header = HAB_HEADER_INITIALIZER;
	int ret = 0;

	HAB_HEADER_SET_SIZE(header, sizeof(struct hab_open_send_data));
	HAB_HEADER_SET_TYPE(header, request->type);

	ret = physical_channel_send(request->pchan, &header, &request->xdata,
			HABMM_SOCKET_SEND_FLAGS_NON_BLOCKING);
	if (ret != 0)
		pr_err("pchan %s failed to send open req msg %d\n",
			request->pchan->name, ret);

	return ret;
}

/*
 * called when remote sends in open-request.
 * The sanity of the arg sizebytes is ensured by its caller hab_msg_recv.
 * The sizebytes should be equal to sizeof(struct hab_open_send_data)
 */
int hab_open_request_add(struct physical_channel *pchan,
			size_t sizebytes, int request_type)
{
	struct hab_open_node *node;
	struct hab_device *dev = pchan->habdev;
	struct hab_open_request *request;
	struct timespec64 ts = {0};
	int irqs_disabled = irqs_disabled();

	node = kzalloc(sizeof(*node), GFP_ATOMIC);
	if (!node)
		return -ENOMEM;

	request = &node->request;
	if (physical_channel_read(pchan, &request->xdata, sizebytes)
				!= sizebytes)
		return -EIO;

	request->type = request_type;
	request->pchan = pchan;

	ktime_get_ts64(&ts);
	node->age = ts.tv_sec + HAB_OPEN_REQ_EXPIRE_TIME_S +
		ts.tv_nsec/NSEC_PER_SEC;

	hab_pchan_get(pchan);

	hab_spin_lock(&dev->openlock, irqs_disabled);
	list_add_tail(&node->node, &dev->openq_list);
	dev->openq_cnt++;
	hab_spin_unlock(&dev->openlock, irqs_disabled);

	return 0;
}

/* local only */
static int hab_open_request_find(struct uhab_context *ctx,
		struct hab_device *dev,
		struct hab_open_request *listen,
		struct hab_open_request **recv_request)
{
	struct hab_open_node *node, *tmp;
	struct hab_open_request *request;
	struct timespec64 ts = {0};
	int ret = 0;

	if (ctx->closing ||
		(listen->pchan && listen->pchan->closed)) {
		*recv_request = NULL;
		return 1;
	}

	spin_lock_bh(&dev->openlock);
	if (list_empty(&dev->openq_list))
		goto done;

	ktime_get_ts64(&ts);

	list_for_each_entry_safe(node, tmp, &dev->openq_list, node) {
		request = (struct hab_open_request *)node;
		if  ((request->type == listen->type ||
			  request->type == HAB_PAYLOAD_TYPE_INIT_CANCEL) &&
			(request->xdata.sub_id == listen->xdata.sub_id) &&
			(!listen->xdata.open_id ||
			request->xdata.open_id == listen->xdata.open_id) &&
			(!listen->pchan   ||
			request->pchan == listen->pchan)) {
			list_del(&node->node);
			dev->openq_cnt--;
			*recv_request = request;
			ret = 1;
			break;
		}
		if (node->age < (int64_t)ts.tv_sec + ts.tv_nsec/NSEC_PER_SEC) {
			pr_warn("open request type %d sub %d open %d\n",
					request->type, request->xdata.sub_id,
					request->xdata.sub_id);
			list_del(&node->node);
			hab_open_request_free(request);
		}
	}

done:
	spin_unlock_bh(&dev->openlock);
	return ret;
}

void hab_open_request_free(struct hab_open_request *request)
{
	if (request) {
		hab_pchan_put(request->pchan);
		kfree(request);
	} else
		pr_err("empty request found\n");
}

int hab_open_listen(struct uhab_context *ctx,
		struct hab_device *dev,
		struct hab_open_request *listen,
		struct hab_open_request **recv_request,
		int ms_timeout,
		unsigned int flags)
{
	int ret = 0;

	unsigned int uninterruptible = 0;

	if (!ctx || !listen || !recv_request) {
		pr_err("listen failed ctx %pK listen %pK request %pK\n",
			ctx, listen, recv_request);
		return -EINVAL;
	}

	/* This flag is for HAB clients in kernel space only, to avoid calling any
	 * unexpected uninterruptible habmm_socket_open() since it is not killable.
	 */
	if (ctx->kernel)
		uninterruptible = (flags & HABMM_SOCKET_OPEN_FLAGS_UNINTERRUPTIBLE);

	*recv_request = NULL;
	if (ms_timeout > 0) { /* be case */
		ms_timeout = msecs_to_jiffies(ms_timeout);
		ret = wait_event_interruptible_timeout(dev->openq,
			hab_open_request_find(ctx, dev, listen, recv_request),
			ms_timeout);
		if (!ret) {
			pr_debug("%s timeout in open listen\n", dev->name);
			ret = -EAGAIN; /* condition not met */
		} else if (-ERESTARTSYS == ret) {
			pr_warn("something failed in open listen ret %d\n",
					ret);
			ret = -EINTR; /* condition not met */
		} else if (ret > 0)
			ret = 0; /* condition met */
	} else { /* fe case */
		if (uninterruptible) { /* fe uinterruptible case */
			wait_event(dev->openq,
			hab_open_request_find(ctx, dev, listen, recv_request));
			if (ctx->closing) {
				pr_warn("local closing during open ret %d\n", ret);
				ret = -ENODEV;
			}
		} else { /* fe interruptible case  */
			ret = wait_event_interruptible(dev->openq,
				hab_open_request_find(ctx, dev, listen, recv_request));
			if (ctx->closing) {
				pr_warn("local closing during open ret %d\n", ret);
				ret = -ENODEV;
			} else if (-ERESTARTSYS == ret) {
				pr_warn("local interrupted ret %d\n", ret);
				ret = -EINTR;
			}
		}
	}

	return ret;
}

/*
 * called when receiving remote's cancel init from FE or init-ack from BE.
 * The sanity of the arg sizebytes is ensured by its caller hab_msg_recv.
 * The sizebytes should be equal to sizeof(struct hab_open_send_data)
 */
int hab_open_receive_cancel(struct physical_channel *pchan,
		size_t sizebytes)
{
	struct hab_device *dev = pchan->habdev;
	struct hab_open_send_data data = {0};
	struct hab_open_request *request;
	struct hab_open_node *node, *tmp;
	int bfound = 0;
	struct timespec64 ts = {0};
	int irqs_disabled = irqs_disabled();

	if (physical_channel_read(pchan, &data, sizebytes) != sizebytes)
		return -EIO;

	hab_spin_lock(&dev->openlock, irqs_disabled);
	list_for_each_entry_safe(node, tmp, &dev->openq_list, node) {
		request = &node->request;
		/* check if open request has been serviced or not */
		if  ((request->type == HAB_PAYLOAD_TYPE_INIT ||
			  request->type == HAB_PAYLOAD_TYPE_INIT_ACK) &&
			 (request->xdata.sub_id == data.sub_id) &&
			(request->xdata.open_id == data.open_id) &&
			(request->xdata.vchan_id == data.vchan_id)) {
			list_del(&node->node);
			kfree(node);
			dev->openq_cnt--;
			pr_info("open cancelled on pchan %s vcid %x subid %d openid %d\n",
					pchan->name, data.vchan_id,
					data.sub_id, data.open_id);
			/* found un-serviced open request, delete it */
			bfound = 1;
			break;
		}
	}
	hab_spin_unlock(&dev->openlock, irqs_disabled);

	if (!bfound) {
		pr_info("init waiting is in-flight. vcid %x sub %d open %d\n",
				data.vchan_id, data.sub_id, data.open_id);
		/* add cancel to the openq to let the waiting open bail out */
		node = kzalloc(sizeof(*node), GFP_ATOMIC);
		if (!node)
			return -ENOMEM;

		request = &node->request;
		request->type     = HAB_PAYLOAD_TYPE_INIT_CANCEL;
		request->pchan    = pchan;
		request->xdata.vchan_id = data.vchan_id;
		request->xdata.sub_id   = data.sub_id;
		request->xdata.open_id  = data.open_id;
		request->xdata.ver_fe  = data.ver_fe;
		request->xdata.ver_be  = data.ver_be;

		ktime_get_ts64(&ts);
		node->age = ts.tv_sec + HAB_OPEN_REQ_EXPIRE_TIME_S +
			ts.tv_nsec/NSEC_PER_SEC;
		/* put when this node is handled in open path */
		hab_pchan_get(pchan);

		hab_spin_lock(&dev->openlock, irqs_disabled);
		list_add_tail(&node->node, &dev->openq_list);
		dev->openq_cnt++;
		hab_spin_unlock(&dev->openlock, irqs_disabled);

		wake_up(&dev->openq);
	}

	return 0;
}

/* calls locally to send cancel pending open to remote */
int hab_open_cancel_notify(struct hab_open_request *request)
{
	struct hab_header header = HAB_HEADER_INITIALIZER;
	int ret = 0;

	HAB_HEADER_SET_SIZE(header, sizeof(struct hab_open_send_data));
	HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_INIT_CANCEL);

	ret = physical_channel_send(request->pchan, &header, &request->xdata,
			HABMM_SOCKET_SEND_FLAGS_NON_BLOCKING);
	if (ret != 0)
		pr_err("pchan %s failed to send open cancel msg %d\n",
			request->pchan->name, ret);

	return ret;
}

/*
 * There will be scheduling in habmm_socket_open, which cannot be called
 * in the atomic context. Therefore, there is no need to consider such
 * atomic caller context which already disables h/w irq when using
 * hab_write_lock/hab_write_unlock here.
 */
int hab_open_pending_enter(struct uhab_context *ctx,
		struct physical_channel *pchan,
		struct hab_open_node *pending)
{
	hab_write_lock(&ctx->ctx_lock, !ctx->kernel);
	list_add_tail(&pending->node, &ctx->pending_open);
	ctx->pending_cnt++;
	hab_write_unlock(&ctx->ctx_lock, !ctx->kernel);

	return 0;
}

int hab_open_pending_exit(struct uhab_context *ctx,
		struct physical_channel *pchan,
		struct hab_open_node *pending)
{
	struct hab_open_node *node, *tmp;
	int ret = -ENOENT;

	hab_write_lock(&ctx->ctx_lock, !ctx->kernel);
	list_for_each_entry_safe(node, tmp, &ctx->pending_open, node) {
		if ((node->request.type == pending->request.type) &&
			(node->request.pchan
				== pending->request.pchan) &&
			(node->request.xdata.vchan_id
				== pending->request.xdata.vchan_id) &&
			(node->request.xdata.sub_id
				== pending->request.xdata.sub_id) &&
			(node->request.xdata.open_id
				== pending->request.xdata.open_id)) {
			list_del(&node->node);
			ctx->pending_cnt--;
			ret = 0;
		}
	}
	hab_write_unlock(&ctx->ctx_lock, !ctx->kernel);

	return ret;
}
