// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/completion.h>
#include <linux/gunyah/gh_msgq.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>

#include "mem-buf-msgq.h"
#include "trace-mem-buf.h"

#define MEM_BUF_TIMEOUT_MS 3500

/*
 * Data structures for tracking request/reply transactions, as well as message
 * queue usage
 */
static DEFINE_MUTEX(mem_buf_msgq_list_lock);
static LIST_HEAD(mem_buf_msgq_list);

struct mem_buf_msgq_id {
	const char *name;
	int label;
};

static struct mem_buf_msgq_id mem_buf_msgqs[] = {
	{
		.name = "trusted_vm",
		.label = GH_MSGQ_LABEL_MEMBUF,
	},
	{
	},
};

/**
 * struct mem_buf_txn: Represents a transaction (request/response pair) in the
 * mem-buf-msgq driver.
 * @txn_id: Transaction ID used to match requests and responses (i.e. a new ID
 * is allocated per request, and the response will have a matching ID).
 * @txn_ret: The return value of the transaction.
 * @txn_done: Signals that a response has arrived.
 * @resp_buf: A pointer to a buffer where the response should be decoded into.
 */
struct mem_buf_txn {
	int txn_id;
	int txn_ret;
	struct completion txn_done;
	void *resp_buf;
};

struct mem_buf_msgq_desc {
	const struct mem_buf_msgq_ops *msgq_ops;
	void *hdlr_data;
	struct mutex idr_mutex;
	struct idr txn_idr;
	void *msgq_hdl;
	struct task_struct *recv_thr;
	struct list_head list;
};

static size_t mem_buf_get_mem_type_alloc_req_size(enum mem_buf_mem_type type)
{
	if (type == MEM_BUF_DMAHEAP_MEM_TYPE)
		return MEM_BUF_MAX_DMAHEAP_NAME_LEN;
	/* Do nothing for MEM_BUF_BUDDY_MEM_TYPE */

	return 0;
}

static void mem_buf_populate_alloc_req_arb_payload(void *dst, void *src,
						   enum mem_buf_mem_type type)
{
	if (type == MEM_BUF_DMAHEAP_MEM_TYPE)
		strscpy(dst, src, MEM_BUF_MAX_DMAHEAP_NAME_LEN);
	/* Do nothing for MEM_BUF_BUDDY_MEM_TYPE */
}

/*
 * mem_buf_construct_alloc_req: Constructs an allocation request message.
 * @mem_buf_txn: A valid transaction structure allocated by a call to mem_buf_init_txn().
 * @alloc_size: The size of the allocation to be requested.
 * @acl_desc: A GH ACL descriptor that describes who will have access to the memory allocated and
 *            with what permissions.
 * @src_mem_type: The type of memory that will be used to satisfy the allocation.
 * @src_data: A pointer to auxiliary data required to satisfy the allocation.
 * @trans_type: One of GH_RM_TRANS_TYPE_DONATE/LEND/SHARE
 */
void *mem_buf_construct_alloc_req(void *mem_buf_txn, size_t alloc_size,
				  struct gh_acl_desc *acl_desc,
				  enum mem_buf_mem_type src_mem_type, void *src_data,
				  u32 trans_type)
{
	size_t tot_size, alloc_req_size, acl_desc_size;
	void *req_buf, *arb_payload;
	unsigned int nr_acl_entries = acl_desc->n_acl_entries;
	struct mem_buf_alloc_req *req;
	struct mem_buf_txn *txn = mem_buf_txn;
	int txn_id = txn->txn_id;


	alloc_req_size = offsetof(struct mem_buf_alloc_req,
				  acl_desc.acl_entries[nr_acl_entries]);
	tot_size = alloc_req_size +
		   mem_buf_get_mem_type_alloc_req_size(src_mem_type);

	req_buf = kzalloc(tot_size, GFP_KERNEL);
	if (!req_buf)
		return ERR_PTR(-ENOMEM);

	req = req_buf;
	req->hdr.txn_id = txn_id;
	req->hdr.msg_type = MEM_BUF_ALLOC_REQ;
	req->hdr.msg_size = tot_size;
	req->size = alloc_size;
	req->src_mem_type = src_mem_type;
	req->trans_type = trans_type;
	acl_desc_size = offsetof(struct gh_acl_desc,
				 acl_entries[nr_acl_entries]);
	memcpy(&req->acl_desc, acl_desc, acl_desc_size);

	arb_payload = req_buf + alloc_req_size;
	mem_buf_populate_alloc_req_arb_payload(arb_payload, src_data,
					       src_mem_type);

	trace_send_alloc_req(req);
	return req_buf;
}
EXPORT_SYMBOL(mem_buf_construct_alloc_req);

/*
 * mem_buf_construct_alloc_resp: Construct a response message to an allocation request.
 * @req_msg: The request message that is being replied to.
 * @alloc_ret: The return code of the allocation.
 * @memparcel_hdl: The memparcel handle that corresponds to the memory that was allocated.
 * sharing, or lending).
 */
void *mem_buf_construct_alloc_resp(void *req_msg, s32 alloc_ret,
				   gh_memparcel_handle_t memparcel_hdl,
				   u32 obj_id)
{
	struct mem_buf_alloc_req *req = req_msg;
	struct mem_buf_alloc_resp *resp_msg = kzalloc(sizeof(*resp_msg), GFP_KERNEL);

	if (!resp_msg)
		return ERR_PTR(-ENOMEM);

	resp_msg->hdr.txn_id = req->hdr.txn_id;
	resp_msg->hdr.msg_type = MEM_BUF_ALLOC_RESP;
	resp_msg->hdr.msg_size = sizeof(*resp_msg);
	resp_msg->ret = alloc_ret;
	resp_msg->hdl = memparcel_hdl;
	resp_msg->obj_id = obj_id;

	return resp_msg;
}
EXPORT_SYMBOL(mem_buf_construct_alloc_resp);

/*
 * mem_buf_construct_relinquish_msg: Construct a relinquish message.
 * @mem_buf_txn: The transaction object.
 * @obj_id: Uniquely identifies an object.
 * @memparcel_hdl: The memparcel that corresponds to the memory that is being relinquished.
 */
void *mem_buf_construct_relinquish_msg(void *mem_buf_txn, u32 obj_id,
				       gh_memparcel_handle_t memparcel_hdl)
{
	struct mem_buf_alloc_relinquish *relinquish_msg;
	struct mem_buf_txn *txn = mem_buf_txn;

	relinquish_msg = kzalloc(sizeof(*relinquish_msg), GFP_KERNEL);
	if (!relinquish_msg)
		return ERR_PTR(-ENOMEM);

	relinquish_msg->hdr.msg_type = MEM_BUF_ALLOC_RELINQUISH;
	relinquish_msg->hdr.msg_size = sizeof(*relinquish_msg);
	relinquish_msg->hdr.txn_id = txn->txn_id;
	relinquish_msg->obj_id = obj_id;
	relinquish_msg->hdl = memparcel_hdl;

	return relinquish_msg;
}
EXPORT_SYMBOL(mem_buf_construct_relinquish_msg);

/*
 * mem_buf_construct_relinquish_resp: Construct a relinquish resp message.
 * @msg: The msg to reply to.
 */
void *mem_buf_construct_relinquish_resp(void *_msg)
{
	struct mem_buf_alloc_relinquish *msg = _msg;
	struct mem_buf_alloc_relinquish *resp;

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);
	if (!resp)
		return ERR_PTR(-ENOMEM);

	resp->hdr.msg_type = MEM_BUF_ALLOC_RELINQUISH_RESP;
	resp->hdr.msg_size = sizeof(*resp);
	resp->hdr.txn_id = msg->hdr.txn_id;

	return resp;
}
EXPORT_SYMBOL(mem_buf_construct_relinquish_resp);

int mem_buf_retrieve_txn_id(void *mem_buf_txn)
{
	struct mem_buf_txn *txn = mem_buf_txn;

	return txn->txn_id;
}
EXPORT_SYMBOL(mem_buf_retrieve_txn_id);

/*
 * mem_buf_init_txn: Allocates a mem-buf transaction that is used in request-response
 *                   message pairs.
 * @mem_buf_msgq_hdl: The handle for the message queue that will be used to transmit the message.
 * @resp_buf: The buffer that will store the output of the response from the recipient of the
 *            request.
 */
void *mem_buf_init_txn(void *mem_buf_msgq_hdl, void *resp_buf)
{
	struct mem_buf_txn *txn;
	struct mem_buf_msgq_desc *desc = mem_buf_msgq_hdl;
	int ret;

	txn = kzalloc(sizeof(*txn), GFP_KERNEL);
	if (!txn)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&desc->idr_mutex);
	ret = idr_alloc_cyclic(&desc->txn_idr, txn, 0, INT_MAX, GFP_KERNEL);
	if (ret < 0) {
		pr_err("%s: failed to allocate transaction id rc: %d\n", __func__, ret);
		mutex_unlock(&desc->idr_mutex);
		kfree(txn);
		return ERR_PTR(ret);
	}

	txn->txn_id = ret;
	init_completion(&txn->txn_done);
	txn->resp_buf = resp_buf;
	mutex_unlock(&desc->idr_mutex);

	return txn;
}
EXPORT_SYMBOL(mem_buf_init_txn);

/*
 * mem_buf_msgq_send: Send a mem-buf message over a particular message queue.
 * @mem_buf_msgq_hdl: The handle for the message queue that will be used to send the message.
 * @msg: The message to be sent. This message must be a mem-buf allocation request, response, or
 *       relinquish request.
 */
int mem_buf_msgq_send(void *mem_buf_msgq_hdl, void *msg)
{
	struct mem_buf_msgq_desc *desc = mem_buf_msgq_hdl;
	struct mem_buf_msg_hdr *hdr = msg;
	int ret;

	if (!(hdr->msg_type >= MEM_BUF_ALLOC_REQ && hdr->msg_type < MEM_BUF_ALLOC_REQ_MAX)) {
		pr_err("%s: message type invalid\n", __func__);
		return -EINVAL;
	}

	ret = gh_msgq_send(desc->msgq_hdl, msg, hdr->msg_size, 0);
	if (ret < 0)
		pr_err("%s: failed to send allocation request rc: %d\n", __func__, ret);
	else
		pr_debug("%s: alloc request sent\n", __func__);

	return ret;
}
EXPORT_SYMBOL(mem_buf_msgq_send);

/*
 * mem_buf_txn_wait: Wait for a response for a particular request.
 * @mem_buf_msgq_hdl: The handle that corresponds to the message queue used for messaging.
 * @mem_buf_txn: A valid transaction which corresponds to a request that was sent.
 *
 * When this function returns successfully, the output of the response will be in the @resp_buf
 * parameter that was passed into mem_buf_txn_init().
 */
int mem_buf_txn_wait(void *mem_buf_msgq_hdl, void *mem_buf_txn)
{
	struct mem_buf_msgq_desc *desc = mem_buf_msgq_hdl;
	struct mem_buf_txn *txn = mem_buf_txn;
	int ret;

	pr_debug("%s: waiting for allocation response\n", __func__);
	ret = wait_for_completion_timeout(&txn->txn_done,
					  msecs_to_jiffies(MEM_BUF_TIMEOUT_MS));

	/*
	 * Recheck under lock.
	 * Handle race condition where we receive a message immediately after
	 * timing out above as complete() is called under idr_mutex.
	 */
	mutex_lock(&desc->idr_mutex);
	if (!ret && !try_wait_for_completion(&txn->txn_done)) {
		pr_err("%s: timed out waiting for allocation response\n",
		       __func__);
		ret = -ETIMEDOUT;
	} else {
		pr_debug("%s: alloc response received\n", __func__);
		ret = 0;
	}

	idr_remove(&desc->txn_idr, txn->txn_id);
	mutex_unlock(&desc->idr_mutex);

	return ret ? ret : txn->txn_ret;
}
EXPORT_SYMBOL(mem_buf_txn_wait);

/*
 * mem_buf_destroy_txn: Releases all resources associated with a mem-buf transaction.
 * @mem_buf_msgq_hdl: The handle that corresponds to the message queue used for messaging.
 * @mem_buf_txn: The transaction structure that was involved in the messaging.
 */
void mem_buf_destroy_txn(void *mem_buf_msgq_hdl, void *mem_buf_txn)
{
	struct mem_buf_txn *txn = mem_buf_txn;

	kfree(txn);
}
EXPORT_SYMBOL(mem_buf_destroy_txn);

static void mem_buf_process_alloc_resp(struct mem_buf_msgq_desc *desc, void *buf, size_t size)
{
	struct mem_buf_msg_hdr *hdr = buf;
	struct mem_buf_alloc_resp *alloc_resp = buf;
	struct mem_buf_txn *txn;
	unsigned int noreclaim_flag;

	mutex_lock(&desc->idr_mutex);
	noreclaim_flag = memalloc_noreclaim_save();
	txn = idr_find(&desc->txn_idr, hdr->txn_id);
	if (!txn) {
		pr_err("%s no txn associated with id: %d\n", __func__, hdr->txn_id);
		/*
		 * If this was a legitimate allocation, we should let the
		 * allocator know that the memory is not in use, so that
		 * it can be reclaimed.
		 */
		if (!alloc_resp->ret) {
			desc->msgq_ops->relinquish_memparcel_hdl(desc->hdlr_data,
								 alloc_resp->obj_id,
								 alloc_resp->hdl);
		}
	} else {
		txn->txn_ret = desc->msgq_ops->alloc_resp_hdlr(desc->hdlr_data, buf, size,
							       txn->resp_buf);
		complete(&txn->txn_done);
	}
	memalloc_noreclaim_restore(noreclaim_flag);
	mutex_unlock(&desc->idr_mutex);
}

static void mem_buf_process_relinquish_resp(struct mem_buf_msgq_desc *desc,
					    void *buf, size_t size)
{
	struct mem_buf_txn *txn;
	struct mem_buf_alloc_relinquish *relinquish_resp_msg = buf;

	if (size != sizeof(*relinquish_resp_msg)) {
		pr_err("%s response received is not of correct size\n",
		       __func__);
		return;
	}
	trace_receive_relinquish_resp_msg(relinquish_resp_msg);

	mutex_lock(&desc->idr_mutex);
	txn = idr_find(&desc->txn_idr, relinquish_resp_msg->hdr.txn_id);
	if (!txn)
		pr_err("%s no txn associated with id: %d\n", __func__,
		       relinquish_resp_msg->hdr.txn_id);
	else
		complete(&txn->txn_done);
	mutex_unlock(&desc->idr_mutex);
}

static void mem_buf_process_msg(struct mem_buf_msgq_desc *desc, void *buf, size_t size)
{
	struct mem_buf_msg_hdr *hdr = buf;

	pr_debug("%s: mem-buf message received\n", __func__);
	if (size < sizeof(*hdr) || hdr->msg_size != size) {
		pr_err("%s: message received is not of a proper size: 0x%lx\n",
		       __func__, size);
		return;
	}

	switch (hdr->msg_type) {
	case MEM_BUF_ALLOC_REQ:
		desc->msgq_ops->alloc_req_hdlr(desc->hdlr_data, buf, size);
		break;
	case MEM_BUF_ALLOC_RESP:
		mem_buf_process_alloc_resp(desc, buf, size);
		break;
	case MEM_BUF_ALLOC_RELINQUISH:
		desc->msgq_ops->relinquish_hdlr(desc->hdlr_data, buf, size);
		break;
	case MEM_BUF_ALLOC_RELINQUISH_RESP:
		mem_buf_process_relinquish_resp(desc, buf, size);
		break;
	default:
		pr_err("%s: received message of unknown type: %d\n", __func__,
		       hdr->msg_type);
	}
}

static int mem_buf_msgq_name_to_msgq_label(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mem_buf_msgqs); i++)
		if (!strcmp(name, mem_buf_msgqs[i].name))
			return mem_buf_msgqs[i].label;

	return -EINVAL;
}

static int mem_buf_msgq_recv_fn(void *data)
{
	struct mem_buf_msgq_desc *desc = data;
	void *buf;
	size_t size;
	int ret;

	buf = kzalloc(GH_MSGQ_MAX_MSG_SIZE_BYTES, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	while (!kthread_should_stop()) {
		ret = gh_msgq_recv(desc->msgq_hdl, buf, GH_MSGQ_MAX_MSG_SIZE_BYTES, &size, 0);
		if (ret < 0) {
			pr_err_ratelimited("%s failed to receive message rc: %d\n", __func__, ret);
		} else {
			mem_buf_process_msg(desc, buf, size);
		}
	}

	kfree(buf);
	return 0;
}

void *mem_buf_msgq_register(const char *msgq_name, struct mem_buf_msgq_hdlr_info *info)
{
	struct mem_buf_msgq_desc *desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	int label;
	void *ret;

	if (!desc)
		return ERR_PTR(-ENOMEM);
	else if (!info || !info->msgq_ops || !info->msgq_ops->alloc_req_hdlr ||
		 !info->msgq_ops->alloc_resp_hdlr || !info->msgq_ops->relinquish_hdlr)
		return ERR_PTR(-EINVAL);

	label = mem_buf_msgq_name_to_msgq_label(msgq_name);
	if (label < 0)
		return ERR_PTR(label);

	INIT_LIST_HEAD(&desc->list);
	desc->msgq_ops = info->msgq_ops;
	desc->hdlr_data = info->hdlr_data;
	mutex_init(&desc->idr_mutex);
	idr_init(&desc->txn_idr);

	desc->msgq_hdl = gh_msgq_register(label);
	if (IS_ERR(desc->msgq_hdl)) {
		ret = desc->msgq_hdl;
		pr_err("Message queue registration failed: rc: %d\n", PTR_ERR(desc->msgq_hdl));
		goto err_msgq_register;
	}

	mutex_lock(&mem_buf_msgq_list_lock);
	list_add_tail(&desc->list, &mem_buf_msgq_list);
	mutex_unlock(&mem_buf_msgq_list_lock);

	desc->recv_thr = kthread_run(mem_buf_msgq_recv_fn, desc, "mem_buf_%s_rcvr", msgq_name);
	if (IS_ERR(desc->recv_thr)) {
		ret = desc->recv_thr;
		pr_err("Failed to create msgq receiver thread rc: %d\n", PTR_ERR(desc->recv_thr));
		goto err_thr_create;
	}

	return desc;

err_thr_create:
	gh_msgq_unregister(desc->msgq_hdl);
err_msgq_register:
	idr_destroy(&desc->txn_idr);
	mutex_destroy(&desc->idr_mutex);
	kfree(desc);
	return ret;
}
EXPORT_SYMBOL(mem_buf_msgq_register);

void mem_buf_msgq_unregister(void *mem_buf_msgq_hdl)
{
	struct mem_buf_msgq_desc *desc = mem_buf_msgq_hdl;

	kthread_stop(desc->recv_thr);
	mutex_lock(&mem_buf_msgq_list_lock);
	list_del(&desc->list);
	mutex_unlock(&mem_buf_msgq_list_lock);
	gh_msgq_unregister(desc->msgq_hdl);
	idr_destroy(&desc->txn_idr);
	mutex_destroy(&desc->idr_mutex);
	kfree(desc);
}
EXPORT_SYMBOL(mem_buf_msgq_unregister);

MODULE_LICENSE("GPL");
