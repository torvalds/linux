/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef MEM_BUF_MSGQ_H
#define MEM_BUF_MSGQ_H

#include <linux/mem-buf.h>
#include <linux/types.h>

/**
 * enum mem_buf_msg_type: Message types used by the membuf driver for
 * communication.
 * @MEM_BUF_ALLOC_REQ: The message is an allocation request from another VM to
 * the receiving VM
 * @MEM_BUF_ALLOC_RESP: The message is a response from a remote VM to an
 * allocation request issued by the receiving VM
 * @MEM_BUF_ALLOC_RELINQUISH: The message is a notification from another VM
 * that the receiving VM can reclaim the memory.
 * @MEM_BUF_ALLOC_RELINQUISH_RESP: Indicates completion of MEM_BUF_ALLOC_RELINQUISH.
 */
enum mem_buf_msg_type {
	MEM_BUF_ALLOC_REQ,
	MEM_BUF_ALLOC_RESP,
	MEM_BUF_ALLOC_RELINQUISH,
	MEM_BUF_ALLOC_RELINQUISH_RESP,
	MEM_BUF_ALLOC_REQ_MAX,
};

/**
 * struct mem_buf_msg_hdr: The header for all membuf messages
 * @txn_id: The transaction ID for the message. This field is only meaningful
 * for request/response type of messages.
 * @msg_type: The type of message.
 * @msg_size: The size of message.
 */
struct mem_buf_msg_hdr {
	u32 txn_id;
	u32 msg_type;
	u32 msg_size;
} __packed;

/**
 * struct mem_buf_alloc_req: The message format for a memory allocation request
 * to another VM.
 * @hdr: Message header
 * @size: The size of the memory allocation to be performed on the remote VM.
 * @src_mem_type: The type of memory that the remote VM should allocate.
 * @trans_type: One of GH_RM_TRANS_TYPE_DONATE/SHARE/LEND
 * @acl_desc: A GH ACL descriptor that describes the VMIDs that will be
 * accessing the memory, as well as what permissions each VMID will have.
 *
 * NOTE: Certain memory types require additional information for the remote VM
 * to interpret. That information should be concatenated with this structure
 * prior to sending the allocation request to the remote VM. For example,
 * with memory type DMAHEAP, the allocation request message will consist of this
 * structure, as well as the name of the heap that will source the allocation.
 */
struct mem_buf_alloc_req {
	struct mem_buf_msg_hdr hdr;
	u64 size;
	u32 src_mem_type;
	u32 trans_type;
	struct gh_acl_desc acl_desc;
} __packed;

/**
 * struct mem_buf_alloc_resp: The message format for a memory allocation
 * request response.
 * @hdr: Message header
 * @ret: Return code from remote VM
 * @hdl: The memparcel handle associated with the memory allocated to the
 * receiving VM. This field is only meaningful if the allocation on the remote
 * VM was carried out successfully, as denoted by @ret.
 * (i.e. memory donation, sharing, or lending).
 * @obj_id: Unique identifier for the memory object associated with handle.
 */
struct mem_buf_alloc_resp {
	struct mem_buf_msg_hdr hdr;
	s32 ret;
	u32 hdl;
	u32 obj_id;
} __packed;

/**
 * struct mem_buf_alloc_relinquish: The message format for a notification
 * that the current VM has relinquished access to the memory lent to it by
 * another VM.
 * @hdr: Message header
 * @hdl: The memparcel handle associated with the memory.
 * @obj_id: Unique identifier for the memory object associated with handle.
 */
struct mem_buf_alloc_relinquish {
	struct mem_buf_msg_hdr hdr;
	u32 hdl;
	u32 obj_id;
} __packed;

/*
 * mem_buf_msgq_ops: A set of ops that are invoked when a message of particular
 * types are received by a mem-buf message queue.
 *
 * The handlers are responsible for freeing the msg buffer that is passed to
 * them.
 *
 * @alloc_req_hdlr: The handler for messages of type MEM_BUF_ALLOC_REQ.
 * @alloc_resp_hdlr: The handler for messages of type MEM_BUF_ALLOC_RESP.
 * @relinquish_hdlr: The handler for messages of type MEM_BUF_ALLOC_RELINQUISH.
 * @relinquish_memparcel_hdl: Callback for relinquishing a memparcel. This is typically used in
 *                            case an allocation request times out, and the response arrives late.
 *                            In this case, the transaction will have been aborted, but the memory
 *                            still needs to be relinquished.
 */
struct mem_buf_msgq_ops {
	void (*alloc_req_hdlr)(void *hdlr_data, void *msg, size_t size);
	int (*alloc_resp_hdlr)(void *hdlr_data, void *msg, size_t size, void *resp_buf);
	void (*relinquish_hdlr)(void *hdlr_data, void *msg, size_t size);
	void (*relinquish_memparcel_hdl)(void *hdlr_data, u32 obj_id,
					 gh_memparcel_handle_t memparcel_hdl);
};

struct mem_buf_msgq_hdlr_info {
	const struct mem_buf_msgq_ops *msgq_ops;
	void *hdlr_data;
};

static inline u32 get_alloc_req_nr_acl_entries(struct mem_buf_alloc_req *req)
{
	return req->acl_desc.n_acl_entries;
}

static inline struct gh_acl_desc *get_alloc_req_gh_acl_desc(struct mem_buf_alloc_req *req)
{
	return &req->acl_desc;
}

static inline enum mem_buf_mem_type get_alloc_req_src_mem_type(struct mem_buf_alloc_req *req)
{
	return req->src_mem_type;
}

static inline u64 get_alloc_req_size(struct mem_buf_alloc_req *req)
{
	return req->size;
}

static inline u64 get_alloc_req_xfer_type(struct mem_buf_alloc_req *req)
{
	return req->trans_type;
}

static inline void *get_alloc_req_arb_payload(struct mem_buf_alloc_req *req)
{
	void *buf = req;
	size_t nr_acl_entries;
	size_t payload_offset;

	nr_acl_entries = req->acl_desc.n_acl_entries;
	if (nr_acl_entries != 1)
		return NULL;

	payload_offset = offsetof(struct mem_buf_alloc_req,
				  acl_desc.acl_entries[nr_acl_entries]);

	return buf + payload_offset;
}

static inline u32 get_alloc_req_txn_id(struct mem_buf_alloc_req *req)
{
	return req->hdr.txn_id;
}

static inline s32 get_alloc_resp_retval(struct mem_buf_alloc_resp *resp)
{
	return resp->ret;
}

static inline u32 get_alloc_resp_hdl(struct mem_buf_alloc_resp *resp)
{
	return resp->hdl;
}

static inline u32 get_alloc_resp_obj_id(struct mem_buf_alloc_resp *resp)
{
	return resp->obj_id;
}

static inline u32 get_relinquish_req_obj_id(struct mem_buf_alloc_relinquish *relinquish_msg)
{
	return relinquish_msg->obj_id;
}

#if IS_ENABLED(CONFIG_QCOM_MEM_BUF_MSGQ)
void *mem_buf_msgq_register(const char *msgq_name, struct mem_buf_msgq_hdlr_info *info);
void mem_buf_msgq_unregister(void *mem_buf_msgq_hdl);
void *mem_buf_init_txn(void *mem_buf_msgq_hdl, void *resp_buf);
int mem_buf_msgq_send(void *mem_buf_msgq_hdl, void *msg);
int mem_buf_txn_wait(void *mem_buf_msgq_hdl, void *mem_buf_txn);
void mem_buf_destroy_txn(void *mem_buf_msgq_hdl, void *mem_buf_txn);
int mem_buf_retrieve_txn_id(void *mem_buf_txn);
/*
 * It is the caller's responsibility to free the messages returned by
 * any functions invoked to construct them via a call to kfree().
 */
void *mem_buf_construct_alloc_req(void *mem_buf_txn, size_t alloc_size,
				  struct gh_acl_desc *acl_desc,
				  enum mem_buf_mem_type src_mem_type, void *src_data,
				  u32 trans_type);
void *mem_buf_construct_alloc_resp(void *req_msg, s32 alloc_ret,
				   gh_memparcel_handle_t memparcel_hdl,
				   u32 obj_id);
void *mem_buf_construct_relinquish_msg(void *mem_buf_txn, u32 obj_id,
				       gh_memparcel_handle_t memparcel_hdl);
void *mem_buf_construct_relinquish_resp(void *_msg);
#else
static inline void *mem_buf_msgq_register(const char *msgq_name,
					  struct mem_buf_msgq_hdlr_info *info)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void mem_buf_msgq_unregister(void *mem_buf_msgq_hdl)
{
}

static inline void *mem_buf_init_txn(void *mem_buf_msgq_hdl, void *resp_buf)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int mem_buf_msgq_send(void *mem_buf_msgq_hdl, void *msg)
{
	return -EOPNOTSUPP;
}

static inline int mem_buf_txn_wait(void *mem_buf_msgq_hdl, void *mem_buf_txn)
{
	return -EOPNOTSUPP;
}

static inline void mem_buf_destroy_txn(void *mem_buf_msgq_hdl, void *mem_buf_txn)
{
}

static inline void *mem_buf_construct_alloc_req(void *mem_buf_txn, size_t alloc_size,
						struct gh_acl_desc *acl_desc,
						enum mem_buf_mem_type src_mem_type, void *src_data,
						u32 trans_type)

{
	return ERR_PTR(-ENODEV);
}

static inline void *mem_buf_construct_alloc_resp(void *req_msg, s32 alloc_ret,
				   gh_memparcel_handle_t memparcel_hdl,
				   u32 obj_id)
{
	return ERR_PTR(-ENODEV);
}

static inline void *mem_buf_construct_relinquish_msg(void *mem_buf_txn, u32 obj_id,
						gh_memparcel_handle_t memparcel_hdl)
{
	return ERR_PTR(-ENODEV);
}

static inline void *mem_buf_construct_relinquish_resp(void *_msg)
{
	return ERR_PTR(-ENODEV);
}

static inline int mem_buf_retrieve_txn_id(void *mem_buf_txn)
{
	return -ENODEV;
}
#endif
#endif
