// SPDX-License-Identifier: GPL-2.0
/*
 * NVMe over Fabrics TCP host.
 * Copyright (c) 2018 Lightbits Labs. All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/nvme-tcp.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <linux/blk-mq.h>
#include <crypto/hash.h>
#include <net/busy_poll.h>

#include "nvme.h"
#include "fabrics.h"

struct nvme_tcp_queue;

/* Define the socket priority to use for connections were it is desirable
 * that the NIC consider performing optimized packet processing or filtering.
 * A non-zero value being sufficient to indicate general consideration of any
 * possible optimization.  Making it a module param allows for alternative
 * values that may be unique for some NIC implementations.
 */
static int so_priority;
module_param(so_priority, int, 0644);
MODULE_PARM_DESC(so_priority, "nvme tcp socket optimize priority");

enum nvme_tcp_send_state {
	NVME_TCP_SEND_CMD_PDU = 0,
	NVME_TCP_SEND_H2C_PDU,
	NVME_TCP_SEND_DATA,
	NVME_TCP_SEND_DDGST,
};

struct nvme_tcp_request {
	struct nvme_request	req;
	void			*pdu;
	struct nvme_tcp_queue	*queue;
	u32			data_len;
	u32			pdu_len;
	u32			pdu_sent;
	u16			ttag;
	struct list_head	entry;
	struct llist_node	lentry;
	__le32			ddgst;

	struct bio		*curr_bio;
	struct iov_iter		iter;

	/* send state */
	size_t			offset;
	size_t			data_sent;
	enum nvme_tcp_send_state state;
};

enum nvme_tcp_queue_flags {
	NVME_TCP_Q_ALLOCATED	= 0,
	NVME_TCP_Q_LIVE		= 1,
	NVME_TCP_Q_POLLING	= 2,
};

enum nvme_tcp_recv_state {
	NVME_TCP_RECV_PDU = 0,
	NVME_TCP_RECV_DATA,
	NVME_TCP_RECV_DDGST,
};

struct nvme_tcp_ctrl;
struct nvme_tcp_queue {
	struct socket		*sock;
	struct work_struct	io_work;
	int			io_cpu;

	struct mutex		queue_lock;
	struct mutex		send_mutex;
	struct llist_head	req_list;
	struct list_head	send_list;
	bool			more_requests;

	/* recv state */
	void			*pdu;
	int			pdu_remaining;
	int			pdu_offset;
	size_t			data_remaining;
	size_t			ddgst_remaining;
	unsigned int		nr_cqe;

	/* send state */
	struct nvme_tcp_request *request;

	int			queue_size;
	size_t			cmnd_capsule_len;
	struct nvme_tcp_ctrl	*ctrl;
	unsigned long		flags;
	bool			rd_enabled;

	bool			hdr_digest;
	bool			data_digest;
	struct ahash_request	*rcv_hash;
	struct ahash_request	*snd_hash;
	__le32			exp_ddgst;
	__le32			recv_ddgst;

	struct page_frag_cache	pf_cache;

	void (*state_change)(struct sock *);
	void (*data_ready)(struct sock *);
	void (*write_space)(struct sock *);
};

struct nvme_tcp_ctrl {
	/* read only in the hot path */
	struct nvme_tcp_queue	*queues;
	struct blk_mq_tag_set	tag_set;

	/* other member variables */
	struct list_head	list;
	struct blk_mq_tag_set	admin_tag_set;
	struct sockaddr_storage addr;
	struct sockaddr_storage src_addr;
	struct net_device	*ndev;
	struct nvme_ctrl	ctrl;

	struct work_struct	err_work;
	struct delayed_work	connect_work;
	struct nvme_tcp_request async_req;
	u32			io_queues[HCTX_MAX_TYPES];
};

static LIST_HEAD(nvme_tcp_ctrl_list);
static DEFINE_MUTEX(nvme_tcp_ctrl_mutex);
static struct workqueue_struct *nvme_tcp_wq;
static const struct blk_mq_ops nvme_tcp_mq_ops;
static const struct blk_mq_ops nvme_tcp_admin_mq_ops;
static int nvme_tcp_try_send(struct nvme_tcp_queue *queue);

static inline struct nvme_tcp_ctrl *to_tcp_ctrl(struct nvme_ctrl *ctrl)
{
	return container_of(ctrl, struct nvme_tcp_ctrl, ctrl);
}

static inline int nvme_tcp_queue_id(struct nvme_tcp_queue *queue)
{
	return queue - queue->ctrl->queues;
}

static inline struct blk_mq_tags *nvme_tcp_tagset(struct nvme_tcp_queue *queue)
{
	u32 queue_idx = nvme_tcp_queue_id(queue);

	if (queue_idx == 0)
		return queue->ctrl->admin_tag_set.tags[queue_idx];
	return queue->ctrl->tag_set.tags[queue_idx - 1];
}

static inline u8 nvme_tcp_hdgst_len(struct nvme_tcp_queue *queue)
{
	return queue->hdr_digest ? NVME_TCP_DIGEST_LENGTH : 0;
}

static inline u8 nvme_tcp_ddgst_len(struct nvme_tcp_queue *queue)
{
	return queue->data_digest ? NVME_TCP_DIGEST_LENGTH : 0;
}

static inline size_t nvme_tcp_inline_data_size(struct nvme_tcp_queue *queue)
{
	return queue->cmnd_capsule_len - sizeof(struct nvme_command);
}

static inline bool nvme_tcp_async_req(struct nvme_tcp_request *req)
{
	return req == &req->queue->ctrl->async_req;
}

static inline bool nvme_tcp_has_inline_data(struct nvme_tcp_request *req)
{
	struct request *rq;

	if (unlikely(nvme_tcp_async_req(req)))
		return false; /* async events don't have a request */

	rq = blk_mq_rq_from_pdu(req);

	return rq_data_dir(rq) == WRITE && req->data_len &&
		req->data_len <= nvme_tcp_inline_data_size(req->queue);
}

static inline struct page *nvme_tcp_req_cur_page(struct nvme_tcp_request *req)
{
	return req->iter.bvec->bv_page;
}

static inline size_t nvme_tcp_req_cur_offset(struct nvme_tcp_request *req)
{
	return req->iter.bvec->bv_offset + req->iter.iov_offset;
}

static inline size_t nvme_tcp_req_cur_length(struct nvme_tcp_request *req)
{
	return min_t(size_t, iov_iter_single_seg_count(&req->iter),
			req->pdu_len - req->pdu_sent);
}

static inline size_t nvme_tcp_pdu_data_left(struct nvme_tcp_request *req)
{
	return rq_data_dir(blk_mq_rq_from_pdu(req)) == WRITE ?
			req->pdu_len - req->pdu_sent : 0;
}

static inline size_t nvme_tcp_pdu_last_send(struct nvme_tcp_request *req,
		int len)
{
	return nvme_tcp_pdu_data_left(req) <= len;
}

static void nvme_tcp_init_iter(struct nvme_tcp_request *req,
		unsigned int dir)
{
	struct request *rq = blk_mq_rq_from_pdu(req);
	struct bio_vec *vec;
	unsigned int size;
	int nr_bvec;
	size_t offset;

	if (rq->rq_flags & RQF_SPECIAL_PAYLOAD) {
		vec = &rq->special_vec;
		nr_bvec = 1;
		size = blk_rq_payload_bytes(rq);
		offset = 0;
	} else {
		struct bio *bio = req->curr_bio;
		struct bvec_iter bi;
		struct bio_vec bv;

		vec = __bvec_iter_bvec(bio->bi_io_vec, bio->bi_iter);
		nr_bvec = 0;
		bio_for_each_bvec(bv, bio, bi) {
			nr_bvec++;
		}
		size = bio->bi_iter.bi_size;
		offset = bio->bi_iter.bi_bvec_done;
	}

	iov_iter_bvec(&req->iter, dir, vec, nr_bvec, size);
	req->iter.iov_offset = offset;
}

static inline void nvme_tcp_advance_req(struct nvme_tcp_request *req,
		int len)
{
	req->data_sent += len;
	req->pdu_sent += len;
	iov_iter_advance(&req->iter, len);
	if (!iov_iter_count(&req->iter) &&
	    req->data_sent < req->data_len) {
		req->curr_bio = req->curr_bio->bi_next;
		nvme_tcp_init_iter(req, WRITE);
	}
}

static inline void nvme_tcp_send_all(struct nvme_tcp_queue *queue)
{
	int ret;

	/* drain the send queue as much as we can... */
	do {
		ret = nvme_tcp_try_send(queue);
	} while (ret > 0);
}

static inline void nvme_tcp_queue_request(struct nvme_tcp_request *req,
		bool sync, bool last)
{
	struct nvme_tcp_queue *queue = req->queue;
	bool empty;

	empty = llist_add(&req->lentry, &queue->req_list) &&
		list_empty(&queue->send_list) && !queue->request;

	/*
	 * if we're the first on the send_list and we can try to send
	 * directly, otherwise queue io_work. Also, only do that if we
	 * are on the same cpu, so we don't introduce contention.
	 */
	if (queue->io_cpu == raw_smp_processor_id() &&
	    sync && empty && mutex_trylock(&queue->send_mutex)) {
		queue->more_requests = !last;
		nvme_tcp_send_all(queue);
		queue->more_requests = false;
		mutex_unlock(&queue->send_mutex);
	} else if (last) {
		queue_work_on(queue->io_cpu, nvme_tcp_wq, &queue->io_work);
	}
}

static void nvme_tcp_process_req_list(struct nvme_tcp_queue *queue)
{
	struct nvme_tcp_request *req;
	struct llist_node *node;

	for (node = llist_del_all(&queue->req_list); node; node = node->next) {
		req = llist_entry(node, struct nvme_tcp_request, lentry);
		list_add(&req->entry, &queue->send_list);
	}
}

static inline struct nvme_tcp_request *
nvme_tcp_fetch_request(struct nvme_tcp_queue *queue)
{
	struct nvme_tcp_request *req;

	req = list_first_entry_or_null(&queue->send_list,
			struct nvme_tcp_request, entry);
	if (!req) {
		nvme_tcp_process_req_list(queue);
		req = list_first_entry_or_null(&queue->send_list,
				struct nvme_tcp_request, entry);
		if (unlikely(!req))
			return NULL;
	}

	list_del(&req->entry);
	return req;
}

static inline void nvme_tcp_ddgst_final(struct ahash_request *hash,
		__le32 *dgst)
{
	ahash_request_set_crypt(hash, NULL, (u8 *)dgst, 0);
	crypto_ahash_final(hash);
}

static inline void nvme_tcp_ddgst_update(struct ahash_request *hash,
		struct page *page, off_t off, size_t len)
{
	struct scatterlist sg;

	sg_init_marker(&sg, 1);
	sg_set_page(&sg, page, len, off);
	ahash_request_set_crypt(hash, &sg, NULL, len);
	crypto_ahash_update(hash);
}

static inline void nvme_tcp_hdgst(struct ahash_request *hash,
		void *pdu, size_t len)
{
	struct scatterlist sg;

	sg_init_one(&sg, pdu, len);
	ahash_request_set_crypt(hash, &sg, pdu + len, len);
	crypto_ahash_digest(hash);
}

static int nvme_tcp_verify_hdgst(struct nvme_tcp_queue *queue,
		void *pdu, size_t pdu_len)
{
	struct nvme_tcp_hdr *hdr = pdu;
	__le32 recv_digest;
	__le32 exp_digest;

	if (unlikely(!(hdr->flags & NVME_TCP_F_HDGST))) {
		dev_err(queue->ctrl->ctrl.device,
			"queue %d: header digest flag is cleared\n",
			nvme_tcp_queue_id(queue));
		return -EPROTO;
	}

	recv_digest = *(__le32 *)(pdu + hdr->hlen);
	nvme_tcp_hdgst(queue->rcv_hash, pdu, pdu_len);
	exp_digest = *(__le32 *)(pdu + hdr->hlen);
	if (recv_digest != exp_digest) {
		dev_err(queue->ctrl->ctrl.device,
			"header digest error: recv %#x expected %#x\n",
			le32_to_cpu(recv_digest), le32_to_cpu(exp_digest));
		return -EIO;
	}

	return 0;
}

static int nvme_tcp_check_ddgst(struct nvme_tcp_queue *queue, void *pdu)
{
	struct nvme_tcp_hdr *hdr = pdu;
	u8 digest_len = nvme_tcp_hdgst_len(queue);
	u32 len;

	len = le32_to_cpu(hdr->plen) - hdr->hlen -
		((hdr->flags & NVME_TCP_F_HDGST) ? digest_len : 0);

	if (unlikely(len && !(hdr->flags & NVME_TCP_F_DDGST))) {
		dev_err(queue->ctrl->ctrl.device,
			"queue %d: data digest flag is cleared\n",
		nvme_tcp_queue_id(queue));
		return -EPROTO;
	}
	crypto_ahash_init(queue->rcv_hash);

	return 0;
}

static void nvme_tcp_exit_request(struct blk_mq_tag_set *set,
		struct request *rq, unsigned int hctx_idx)
{
	struct nvme_tcp_request *req = blk_mq_rq_to_pdu(rq);

	page_frag_free(req->pdu);
}

static int nvme_tcp_init_request(struct blk_mq_tag_set *set,
		struct request *rq, unsigned int hctx_idx,
		unsigned int numa_node)
{
	struct nvme_tcp_ctrl *ctrl = set->driver_data;
	struct nvme_tcp_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_tcp_cmd_pdu *pdu;
	int queue_idx = (set == &ctrl->tag_set) ? hctx_idx + 1 : 0;
	struct nvme_tcp_queue *queue = &ctrl->queues[queue_idx];
	u8 hdgst = nvme_tcp_hdgst_len(queue);

	req->pdu = page_frag_alloc(&queue->pf_cache,
		sizeof(struct nvme_tcp_cmd_pdu) + hdgst,
		GFP_KERNEL | __GFP_ZERO);
	if (!req->pdu)
		return -ENOMEM;

	pdu = req->pdu;
	req->queue = queue;
	nvme_req(rq)->ctrl = &ctrl->ctrl;
	nvme_req(rq)->cmd = &pdu->cmd;

	return 0;
}

static int nvme_tcp_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
		unsigned int hctx_idx)
{
	struct nvme_tcp_ctrl *ctrl = data;
	struct nvme_tcp_queue *queue = &ctrl->queues[hctx_idx + 1];

	hctx->driver_data = queue;
	return 0;
}

static int nvme_tcp_init_admin_hctx(struct blk_mq_hw_ctx *hctx, void *data,
		unsigned int hctx_idx)
{
	struct nvme_tcp_ctrl *ctrl = data;
	struct nvme_tcp_queue *queue = &ctrl->queues[0];

	hctx->driver_data = queue;
	return 0;
}

static enum nvme_tcp_recv_state
nvme_tcp_recv_state(struct nvme_tcp_queue *queue)
{
	return  (queue->pdu_remaining) ? NVME_TCP_RECV_PDU :
		(queue->ddgst_remaining) ? NVME_TCP_RECV_DDGST :
		NVME_TCP_RECV_DATA;
}

static void nvme_tcp_init_recv_ctx(struct nvme_tcp_queue *queue)
{
	queue->pdu_remaining = sizeof(struct nvme_tcp_rsp_pdu) +
				nvme_tcp_hdgst_len(queue);
	queue->pdu_offset = 0;
	queue->data_remaining = -1;
	queue->ddgst_remaining = 0;
}

static void nvme_tcp_error_recovery(struct nvme_ctrl *ctrl)
{
	if (!nvme_change_ctrl_state(ctrl, NVME_CTRL_RESETTING))
		return;

	dev_warn(ctrl->device, "starting error recovery\n");
	queue_work(nvme_reset_wq, &to_tcp_ctrl(ctrl)->err_work);
}

static int nvme_tcp_process_nvme_cqe(struct nvme_tcp_queue *queue,
		struct nvme_completion *cqe)
{
	struct request *rq;

	rq = blk_mq_tag_to_rq(nvme_tcp_tagset(queue), cqe->command_id);
	if (!rq) {
		dev_err(queue->ctrl->ctrl.device,
			"queue %d tag 0x%x not found\n",
			nvme_tcp_queue_id(queue), cqe->command_id);
		nvme_tcp_error_recovery(&queue->ctrl->ctrl);
		return -EINVAL;
	}

	if (!nvme_try_complete_req(rq, cqe->status, cqe->result))
		nvme_complete_rq(rq);
	queue->nr_cqe++;

	return 0;
}

static int nvme_tcp_handle_c2h_data(struct nvme_tcp_queue *queue,
		struct nvme_tcp_data_pdu *pdu)
{
	struct request *rq;

	rq = blk_mq_tag_to_rq(nvme_tcp_tagset(queue), pdu->command_id);
	if (!rq) {
		dev_err(queue->ctrl->ctrl.device,
			"queue %d tag %#x not found\n",
			nvme_tcp_queue_id(queue), pdu->command_id);
		return -ENOENT;
	}

	if (!blk_rq_payload_bytes(rq)) {
		dev_err(queue->ctrl->ctrl.device,
			"queue %d tag %#x unexpected data\n",
			nvme_tcp_queue_id(queue), rq->tag);
		return -EIO;
	}

	queue->data_remaining = le32_to_cpu(pdu->data_length);

	if (pdu->hdr.flags & NVME_TCP_F_DATA_SUCCESS &&
	    unlikely(!(pdu->hdr.flags & NVME_TCP_F_DATA_LAST))) {
		dev_err(queue->ctrl->ctrl.device,
			"queue %d tag %#x SUCCESS set but not last PDU\n",
			nvme_tcp_queue_id(queue), rq->tag);
		nvme_tcp_error_recovery(&queue->ctrl->ctrl);
		return -EPROTO;
	}

	return 0;
}

static int nvme_tcp_handle_comp(struct nvme_tcp_queue *queue,
		struct nvme_tcp_rsp_pdu *pdu)
{
	struct nvme_completion *cqe = &pdu->cqe;
	int ret = 0;

	/*
	 * AEN requests are special as they don't time out and can
	 * survive any kind of queue freeze and often don't respond to
	 * aborts.  We don't even bother to allocate a struct request
	 * for them but rather special case them here.
	 */
	if (unlikely(nvme_is_aen_req(nvme_tcp_queue_id(queue),
				     cqe->command_id)))
		nvme_complete_async_event(&queue->ctrl->ctrl, cqe->status,
				&cqe->result);
	else
		ret = nvme_tcp_process_nvme_cqe(queue, cqe);

	return ret;
}

static int nvme_tcp_setup_h2c_data_pdu(struct nvme_tcp_request *req,
		struct nvme_tcp_r2t_pdu *pdu)
{
	struct nvme_tcp_data_pdu *data = req->pdu;
	struct nvme_tcp_queue *queue = req->queue;
	struct request *rq = blk_mq_rq_from_pdu(req);
	u8 hdgst = nvme_tcp_hdgst_len(queue);
	u8 ddgst = nvme_tcp_ddgst_len(queue);

	req->pdu_len = le32_to_cpu(pdu->r2t_length);
	req->pdu_sent = 0;

	if (unlikely(!req->pdu_len)) {
		dev_err(queue->ctrl->ctrl.device,
			"req %d r2t len is %u, probably a bug...\n",
			rq->tag, req->pdu_len);
		return -EPROTO;
	}

	if (unlikely(req->data_sent + req->pdu_len > req->data_len)) {
		dev_err(queue->ctrl->ctrl.device,
			"req %d r2t len %u exceeded data len %u (%zu sent)\n",
			rq->tag, req->pdu_len, req->data_len,
			req->data_sent);
		return -EPROTO;
	}

	if (unlikely(le32_to_cpu(pdu->r2t_offset) < req->data_sent)) {
		dev_err(queue->ctrl->ctrl.device,
			"req %d unexpected r2t offset %u (expected %zu)\n",
			rq->tag, le32_to_cpu(pdu->r2t_offset),
			req->data_sent);
		return -EPROTO;
	}

	memset(data, 0, sizeof(*data));
	data->hdr.type = nvme_tcp_h2c_data;
	data->hdr.flags = NVME_TCP_F_DATA_LAST;
	if (queue->hdr_digest)
		data->hdr.flags |= NVME_TCP_F_HDGST;
	if (queue->data_digest)
		data->hdr.flags |= NVME_TCP_F_DDGST;
	data->hdr.hlen = sizeof(*data);
	data->hdr.pdo = data->hdr.hlen + hdgst;
	data->hdr.plen =
		cpu_to_le32(data->hdr.hlen + hdgst + req->pdu_len + ddgst);
	data->ttag = pdu->ttag;
	data->command_id = rq->tag;
	data->data_offset = cpu_to_le32(req->data_sent);
	data->data_length = cpu_to_le32(req->pdu_len);
	return 0;
}

static int nvme_tcp_handle_r2t(struct nvme_tcp_queue *queue,
		struct nvme_tcp_r2t_pdu *pdu)
{
	struct nvme_tcp_request *req;
	struct request *rq;
	int ret;

	rq = blk_mq_tag_to_rq(nvme_tcp_tagset(queue), pdu->command_id);
	if (!rq) {
		dev_err(queue->ctrl->ctrl.device,
			"queue %d tag %#x not found\n",
			nvme_tcp_queue_id(queue), pdu->command_id);
		return -ENOENT;
	}
	req = blk_mq_rq_to_pdu(rq);

	ret = nvme_tcp_setup_h2c_data_pdu(req, pdu);
	if (unlikely(ret))
		return ret;

	req->state = NVME_TCP_SEND_H2C_PDU;
	req->offset = 0;

	nvme_tcp_queue_request(req, false, true);

	return 0;
}

static int nvme_tcp_recv_pdu(struct nvme_tcp_queue *queue, struct sk_buff *skb,
		unsigned int *offset, size_t *len)
{
	struct nvme_tcp_hdr *hdr;
	char *pdu = queue->pdu;
	size_t rcv_len = min_t(size_t, *len, queue->pdu_remaining);
	int ret;

	ret = skb_copy_bits(skb, *offset,
		&pdu[queue->pdu_offset], rcv_len);
	if (unlikely(ret))
		return ret;

	queue->pdu_remaining -= rcv_len;
	queue->pdu_offset += rcv_len;
	*offset += rcv_len;
	*len -= rcv_len;
	if (queue->pdu_remaining)
		return 0;

	hdr = queue->pdu;
	if (queue->hdr_digest) {
		ret = nvme_tcp_verify_hdgst(queue, queue->pdu, hdr->hlen);
		if (unlikely(ret))
			return ret;
	}


	if (queue->data_digest) {
		ret = nvme_tcp_check_ddgst(queue, queue->pdu);
		if (unlikely(ret))
			return ret;
	}

	switch (hdr->type) {
	case nvme_tcp_c2h_data:
		return nvme_tcp_handle_c2h_data(queue, (void *)queue->pdu);
	case nvme_tcp_rsp:
		nvme_tcp_init_recv_ctx(queue);
		return nvme_tcp_handle_comp(queue, (void *)queue->pdu);
	case nvme_tcp_r2t:
		nvme_tcp_init_recv_ctx(queue);
		return nvme_tcp_handle_r2t(queue, (void *)queue->pdu);
	default:
		dev_err(queue->ctrl->ctrl.device,
			"unsupported pdu type (%d)\n", hdr->type);
		return -EINVAL;
	}
}

static inline void nvme_tcp_end_request(struct request *rq, u16 status)
{
	union nvme_result res = {};

	if (!nvme_try_complete_req(rq, cpu_to_le16(status << 1), res))
		nvme_complete_rq(rq);
}

static int nvme_tcp_recv_data(struct nvme_tcp_queue *queue, struct sk_buff *skb,
			      unsigned int *offset, size_t *len)
{
	struct nvme_tcp_data_pdu *pdu = (void *)queue->pdu;
	struct nvme_tcp_request *req;
	struct request *rq;

	rq = blk_mq_tag_to_rq(nvme_tcp_tagset(queue), pdu->command_id);
	if (!rq) {
		dev_err(queue->ctrl->ctrl.device,
			"queue %d tag %#x not found\n",
			nvme_tcp_queue_id(queue), pdu->command_id);
		return -ENOENT;
	}
	req = blk_mq_rq_to_pdu(rq);

	while (true) {
		int recv_len, ret;

		recv_len = min_t(size_t, *len, queue->data_remaining);
		if (!recv_len)
			break;

		if (!iov_iter_count(&req->iter)) {
			req->curr_bio = req->curr_bio->bi_next;

			/*
			 * If we don`t have any bios it means that controller
			 * sent more data than we requested, hence error
			 */
			if (!req->curr_bio) {
				dev_err(queue->ctrl->ctrl.device,
					"queue %d no space in request %#x",
					nvme_tcp_queue_id(queue), rq->tag);
				nvme_tcp_init_recv_ctx(queue);
				return -EIO;
			}
			nvme_tcp_init_iter(req, READ);
		}

		/* we can read only from what is left in this bio */
		recv_len = min_t(size_t, recv_len,
				iov_iter_count(&req->iter));

		if (queue->data_digest)
			ret = skb_copy_and_hash_datagram_iter(skb, *offset,
				&req->iter, recv_len, queue->rcv_hash);
		else
			ret = skb_copy_datagram_iter(skb, *offset,
					&req->iter, recv_len);
		if (ret) {
			dev_err(queue->ctrl->ctrl.device,
				"queue %d failed to copy request %#x data",
				nvme_tcp_queue_id(queue), rq->tag);
			return ret;
		}

		*len -= recv_len;
		*offset += recv_len;
		queue->data_remaining -= recv_len;
	}

	if (!queue->data_remaining) {
		if (queue->data_digest) {
			nvme_tcp_ddgst_final(queue->rcv_hash, &queue->exp_ddgst);
			queue->ddgst_remaining = NVME_TCP_DIGEST_LENGTH;
		} else {
			if (pdu->hdr.flags & NVME_TCP_F_DATA_SUCCESS) {
				nvme_tcp_end_request(rq, NVME_SC_SUCCESS);
				queue->nr_cqe++;
			}
			nvme_tcp_init_recv_ctx(queue);
		}
	}

	return 0;
}

static int nvme_tcp_recv_ddgst(struct nvme_tcp_queue *queue,
		struct sk_buff *skb, unsigned int *offset, size_t *len)
{
	struct nvme_tcp_data_pdu *pdu = (void *)queue->pdu;
	char *ddgst = (char *)&queue->recv_ddgst;
	size_t recv_len = min_t(size_t, *len, queue->ddgst_remaining);
	off_t off = NVME_TCP_DIGEST_LENGTH - queue->ddgst_remaining;
	int ret;

	ret = skb_copy_bits(skb, *offset, &ddgst[off], recv_len);
	if (unlikely(ret))
		return ret;

	queue->ddgst_remaining -= recv_len;
	*offset += recv_len;
	*len -= recv_len;
	if (queue->ddgst_remaining)
		return 0;

	if (queue->recv_ddgst != queue->exp_ddgst) {
		dev_err(queue->ctrl->ctrl.device,
			"data digest error: recv %#x expected %#x\n",
			le32_to_cpu(queue->recv_ddgst),
			le32_to_cpu(queue->exp_ddgst));
		return -EIO;
	}

	if (pdu->hdr.flags & NVME_TCP_F_DATA_SUCCESS) {
		struct request *rq = blk_mq_tag_to_rq(nvme_tcp_tagset(queue),
						pdu->command_id);

		nvme_tcp_end_request(rq, NVME_SC_SUCCESS);
		queue->nr_cqe++;
	}

	nvme_tcp_init_recv_ctx(queue);
	return 0;
}

static int nvme_tcp_recv_skb(read_descriptor_t *desc, struct sk_buff *skb,
			     unsigned int offset, size_t len)
{
	struct nvme_tcp_queue *queue = desc->arg.data;
	size_t consumed = len;
	int result;

	while (len) {
		switch (nvme_tcp_recv_state(queue)) {
		case NVME_TCP_RECV_PDU:
			result = nvme_tcp_recv_pdu(queue, skb, &offset, &len);
			break;
		case NVME_TCP_RECV_DATA:
			result = nvme_tcp_recv_data(queue, skb, &offset, &len);
			break;
		case NVME_TCP_RECV_DDGST:
			result = nvme_tcp_recv_ddgst(queue, skb, &offset, &len);
			break;
		default:
			result = -EFAULT;
		}
		if (result) {
			dev_err(queue->ctrl->ctrl.device,
				"receive failed:  %d\n", result);
			queue->rd_enabled = false;
			nvme_tcp_error_recovery(&queue->ctrl->ctrl);
			return result;
		}
	}

	return consumed;
}

static void nvme_tcp_data_ready(struct sock *sk)
{
	struct nvme_tcp_queue *queue;

	read_lock_bh(&sk->sk_callback_lock);
	queue = sk->sk_user_data;
	if (likely(queue && queue->rd_enabled) &&
	    !test_bit(NVME_TCP_Q_POLLING, &queue->flags))
		queue_work_on(queue->io_cpu, nvme_tcp_wq, &queue->io_work);
	read_unlock_bh(&sk->sk_callback_lock);
}

static void nvme_tcp_write_space(struct sock *sk)
{
	struct nvme_tcp_queue *queue;

	read_lock_bh(&sk->sk_callback_lock);
	queue = sk->sk_user_data;
	if (likely(queue && sk_stream_is_writeable(sk))) {
		clear_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		queue_work_on(queue->io_cpu, nvme_tcp_wq, &queue->io_work);
	}
	read_unlock_bh(&sk->sk_callback_lock);
}

static void nvme_tcp_state_change(struct sock *sk)
{
	struct nvme_tcp_queue *queue;

	read_lock_bh(&sk->sk_callback_lock);
	queue = sk->sk_user_data;
	if (!queue)
		goto done;

	switch (sk->sk_state) {
	case TCP_CLOSE:
	case TCP_CLOSE_WAIT:
	case TCP_LAST_ACK:
	case TCP_FIN_WAIT1:
	case TCP_FIN_WAIT2:
		nvme_tcp_error_recovery(&queue->ctrl->ctrl);
		break;
	default:
		dev_info(queue->ctrl->ctrl.device,
			"queue %d socket state %d\n",
			nvme_tcp_queue_id(queue), sk->sk_state);
	}

	queue->state_change(sk);
done:
	read_unlock_bh(&sk->sk_callback_lock);
}

static inline bool nvme_tcp_queue_more(struct nvme_tcp_queue *queue)
{
	return !list_empty(&queue->send_list) ||
		!llist_empty(&queue->req_list) || queue->more_requests;
}

static inline void nvme_tcp_done_send_req(struct nvme_tcp_queue *queue)
{
	queue->request = NULL;
}

static void nvme_tcp_fail_request(struct nvme_tcp_request *req)
{
	nvme_tcp_end_request(blk_mq_rq_from_pdu(req), NVME_SC_HOST_PATH_ERROR);
}

static int nvme_tcp_try_send_data(struct nvme_tcp_request *req)
{
	struct nvme_tcp_queue *queue = req->queue;

	while (true) {
		struct page *page = nvme_tcp_req_cur_page(req);
		size_t offset = nvme_tcp_req_cur_offset(req);
		size_t len = nvme_tcp_req_cur_length(req);
		bool last = nvme_tcp_pdu_last_send(req, len);
		int ret, flags = MSG_DONTWAIT;

		if (last && !queue->data_digest && !nvme_tcp_queue_more(queue))
			flags |= MSG_EOR;
		else
			flags |= MSG_MORE | MSG_SENDPAGE_NOTLAST;

		if (sendpage_ok(page)) {
			ret = kernel_sendpage(queue->sock, page, offset, len,
					flags);
		} else {
			ret = sock_no_sendpage(queue->sock, page, offset, len,
					flags);
		}
		if (ret <= 0)
			return ret;

		if (queue->data_digest)
			nvme_tcp_ddgst_update(queue->snd_hash, page,
					offset, ret);

		/* fully successful last write*/
		if (last && ret == len) {
			if (queue->data_digest) {
				nvme_tcp_ddgst_final(queue->snd_hash,
					&req->ddgst);
				req->state = NVME_TCP_SEND_DDGST;
				req->offset = 0;
			} else {
				nvme_tcp_done_send_req(queue);
			}
			return 1;
		}
		nvme_tcp_advance_req(req, ret);
	}
	return -EAGAIN;
}

static int nvme_tcp_try_send_cmd_pdu(struct nvme_tcp_request *req)
{
	struct nvme_tcp_queue *queue = req->queue;
	struct nvme_tcp_cmd_pdu *pdu = req->pdu;
	bool inline_data = nvme_tcp_has_inline_data(req);
	u8 hdgst = nvme_tcp_hdgst_len(queue);
	int len = sizeof(*pdu) + hdgst - req->offset;
	int flags = MSG_DONTWAIT;
	int ret;

	if (inline_data || nvme_tcp_queue_more(queue))
		flags |= MSG_MORE | MSG_SENDPAGE_NOTLAST;
	else
		flags |= MSG_EOR;

	if (queue->hdr_digest && !req->offset)
		nvme_tcp_hdgst(queue->snd_hash, pdu, sizeof(*pdu));

	ret = kernel_sendpage(queue->sock, virt_to_page(pdu),
			offset_in_page(pdu) + req->offset, len,  flags);
	if (unlikely(ret <= 0))
		return ret;

	len -= ret;
	if (!len) {
		if (inline_data) {
			req->state = NVME_TCP_SEND_DATA;
			if (queue->data_digest)
				crypto_ahash_init(queue->snd_hash);
		} else {
			nvme_tcp_done_send_req(queue);
		}
		return 1;
	}
	req->offset += ret;

	return -EAGAIN;
}

static int nvme_tcp_try_send_data_pdu(struct nvme_tcp_request *req)
{
	struct nvme_tcp_queue *queue = req->queue;
	struct nvme_tcp_data_pdu *pdu = req->pdu;
	u8 hdgst = nvme_tcp_hdgst_len(queue);
	int len = sizeof(*pdu) - req->offset + hdgst;
	int ret;

	if (queue->hdr_digest && !req->offset)
		nvme_tcp_hdgst(queue->snd_hash, pdu, sizeof(*pdu));

	ret = kernel_sendpage(queue->sock, virt_to_page(pdu),
			offset_in_page(pdu) + req->offset, len,
			MSG_DONTWAIT | MSG_MORE | MSG_SENDPAGE_NOTLAST);
	if (unlikely(ret <= 0))
		return ret;

	len -= ret;
	if (!len) {
		req->state = NVME_TCP_SEND_DATA;
		if (queue->data_digest)
			crypto_ahash_init(queue->snd_hash);
		return 1;
	}
	req->offset += ret;

	return -EAGAIN;
}

static int nvme_tcp_try_send_ddgst(struct nvme_tcp_request *req)
{
	struct nvme_tcp_queue *queue = req->queue;
	int ret;
	struct msghdr msg = { .msg_flags = MSG_DONTWAIT };
	struct kvec iov = {
		.iov_base = &req->ddgst + req->offset,
		.iov_len = NVME_TCP_DIGEST_LENGTH - req->offset
	};

	if (nvme_tcp_queue_more(queue))
		msg.msg_flags |= MSG_MORE;
	else
		msg.msg_flags |= MSG_EOR;

	ret = kernel_sendmsg(queue->sock, &msg, &iov, 1, iov.iov_len);
	if (unlikely(ret <= 0))
		return ret;

	if (req->offset + ret == NVME_TCP_DIGEST_LENGTH) {
		nvme_tcp_done_send_req(queue);
		return 1;
	}

	req->offset += ret;
	return -EAGAIN;
}

static int nvme_tcp_try_send(struct nvme_tcp_queue *queue)
{
	struct nvme_tcp_request *req;
	int ret = 1;

	if (!queue->request) {
		queue->request = nvme_tcp_fetch_request(queue);
		if (!queue->request)
			return 0;
	}
	req = queue->request;

	if (req->state == NVME_TCP_SEND_CMD_PDU) {
		ret = nvme_tcp_try_send_cmd_pdu(req);
		if (ret <= 0)
			goto done;
		if (!nvme_tcp_has_inline_data(req))
			return ret;
	}

	if (req->state == NVME_TCP_SEND_H2C_PDU) {
		ret = nvme_tcp_try_send_data_pdu(req);
		if (ret <= 0)
			goto done;
	}

	if (req->state == NVME_TCP_SEND_DATA) {
		ret = nvme_tcp_try_send_data(req);
		if (ret <= 0)
			goto done;
	}

	if (req->state == NVME_TCP_SEND_DDGST)
		ret = nvme_tcp_try_send_ddgst(req);
done:
	if (ret == -EAGAIN) {
		ret = 0;
	} else if (ret < 0) {
		dev_err(queue->ctrl->ctrl.device,
			"failed to send request %d\n", ret);
		if (ret != -EPIPE && ret != -ECONNRESET)
			nvme_tcp_fail_request(queue->request);
		nvme_tcp_done_send_req(queue);
	}
	return ret;
}

static int nvme_tcp_try_recv(struct nvme_tcp_queue *queue)
{
	struct socket *sock = queue->sock;
	struct sock *sk = sock->sk;
	read_descriptor_t rd_desc;
	int consumed;

	rd_desc.arg.data = queue;
	rd_desc.count = 1;
	lock_sock(sk);
	queue->nr_cqe = 0;
	consumed = sock->ops->read_sock(sk, &rd_desc, nvme_tcp_recv_skb);
	release_sock(sk);
	return consumed;
}

static void nvme_tcp_io_work(struct work_struct *w)
{
	struct nvme_tcp_queue *queue =
		container_of(w, struct nvme_tcp_queue, io_work);
	unsigned long deadline = jiffies + msecs_to_jiffies(1);

	do {
		bool pending = false;
		int result;

		if (mutex_trylock(&queue->send_mutex)) {
			result = nvme_tcp_try_send(queue);
			mutex_unlock(&queue->send_mutex);
			if (result > 0)
				pending = true;
			else if (unlikely(result < 0))
				break;
		} else
			pending = !llist_empty(&queue->req_list);

		result = nvme_tcp_try_recv(queue);
		if (result > 0)
			pending = true;
		else if (unlikely(result < 0))
			return;

		if (!pending)
			return;

	} while (!time_after(jiffies, deadline)); /* quota is exhausted */

	queue_work_on(queue->io_cpu, nvme_tcp_wq, &queue->io_work);
}

static void nvme_tcp_free_crypto(struct nvme_tcp_queue *queue)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(queue->rcv_hash);

	ahash_request_free(queue->rcv_hash);
	ahash_request_free(queue->snd_hash);
	crypto_free_ahash(tfm);
}

static int nvme_tcp_alloc_crypto(struct nvme_tcp_queue *queue)
{
	struct crypto_ahash *tfm;

	tfm = crypto_alloc_ahash("crc32c", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	queue->snd_hash = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!queue->snd_hash)
		goto free_tfm;
	ahash_request_set_callback(queue->snd_hash, 0, NULL, NULL);

	queue->rcv_hash = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!queue->rcv_hash)
		goto free_snd_hash;
	ahash_request_set_callback(queue->rcv_hash, 0, NULL, NULL);

	return 0;
free_snd_hash:
	ahash_request_free(queue->snd_hash);
free_tfm:
	crypto_free_ahash(tfm);
	return -ENOMEM;
}

static void nvme_tcp_free_async_req(struct nvme_tcp_ctrl *ctrl)
{
	struct nvme_tcp_request *async = &ctrl->async_req;

	page_frag_free(async->pdu);
}

static int nvme_tcp_alloc_async_req(struct nvme_tcp_ctrl *ctrl)
{
	struct nvme_tcp_queue *queue = &ctrl->queues[0];
	struct nvme_tcp_request *async = &ctrl->async_req;
	u8 hdgst = nvme_tcp_hdgst_len(queue);

	async->pdu = page_frag_alloc(&queue->pf_cache,
		sizeof(struct nvme_tcp_cmd_pdu) + hdgst,
		GFP_KERNEL | __GFP_ZERO);
	if (!async->pdu)
		return -ENOMEM;

	async->queue = &ctrl->queues[0];
	return 0;
}

static void nvme_tcp_free_queue(struct nvme_ctrl *nctrl, int qid)
{
	struct nvme_tcp_ctrl *ctrl = to_tcp_ctrl(nctrl);
	struct nvme_tcp_queue *queue = &ctrl->queues[qid];

	if (!test_and_clear_bit(NVME_TCP_Q_ALLOCATED, &queue->flags))
		return;

	if (queue->hdr_digest || queue->data_digest)
		nvme_tcp_free_crypto(queue);

	sock_release(queue->sock);
	kfree(queue->pdu);
	mutex_destroy(&queue->queue_lock);
}

static int nvme_tcp_init_connection(struct nvme_tcp_queue *queue)
{
	struct nvme_tcp_icreq_pdu *icreq;
	struct nvme_tcp_icresp_pdu *icresp;
	struct msghdr msg = {};
	struct kvec iov;
	bool ctrl_hdgst, ctrl_ddgst;
	int ret;

	icreq = kzalloc(sizeof(*icreq), GFP_KERNEL);
	if (!icreq)
		return -ENOMEM;

	icresp = kzalloc(sizeof(*icresp), GFP_KERNEL);
	if (!icresp) {
		ret = -ENOMEM;
		goto free_icreq;
	}

	icreq->hdr.type = nvme_tcp_icreq;
	icreq->hdr.hlen = sizeof(*icreq);
	icreq->hdr.pdo = 0;
	icreq->hdr.plen = cpu_to_le32(icreq->hdr.hlen);
	icreq->pfv = cpu_to_le16(NVME_TCP_PFV_1_0);
	icreq->maxr2t = 0; /* single inflight r2t supported */
	icreq->hpda = 0; /* no alignment constraint */
	if (queue->hdr_digest)
		icreq->digest |= NVME_TCP_HDR_DIGEST_ENABLE;
	if (queue->data_digest)
		icreq->digest |= NVME_TCP_DATA_DIGEST_ENABLE;

	iov.iov_base = icreq;
	iov.iov_len = sizeof(*icreq);
	ret = kernel_sendmsg(queue->sock, &msg, &iov, 1, iov.iov_len);
	if (ret < 0)
		goto free_icresp;

	memset(&msg, 0, sizeof(msg));
	iov.iov_base = icresp;
	iov.iov_len = sizeof(*icresp);
	ret = kernel_recvmsg(queue->sock, &msg, &iov, 1,
			iov.iov_len, msg.msg_flags);
	if (ret < 0)
		goto free_icresp;

	ret = -EINVAL;
	if (icresp->hdr.type != nvme_tcp_icresp) {
		pr_err("queue %d: bad type returned %d\n",
			nvme_tcp_queue_id(queue), icresp->hdr.type);
		goto free_icresp;
	}

	if (le32_to_cpu(icresp->hdr.plen) != sizeof(*icresp)) {
		pr_err("queue %d: bad pdu length returned %d\n",
			nvme_tcp_queue_id(queue), icresp->hdr.plen);
		goto free_icresp;
	}

	if (icresp->pfv != NVME_TCP_PFV_1_0) {
		pr_err("queue %d: bad pfv returned %d\n",
			nvme_tcp_queue_id(queue), icresp->pfv);
		goto free_icresp;
	}

	ctrl_ddgst = !!(icresp->digest & NVME_TCP_DATA_DIGEST_ENABLE);
	if ((queue->data_digest && !ctrl_ddgst) ||
	    (!queue->data_digest && ctrl_ddgst)) {
		pr_err("queue %d: data digest mismatch host: %s ctrl: %s\n",
			nvme_tcp_queue_id(queue),
			queue->data_digest ? "enabled" : "disabled",
			ctrl_ddgst ? "enabled" : "disabled");
		goto free_icresp;
	}

	ctrl_hdgst = !!(icresp->digest & NVME_TCP_HDR_DIGEST_ENABLE);
	if ((queue->hdr_digest && !ctrl_hdgst) ||
	    (!queue->hdr_digest && ctrl_hdgst)) {
		pr_err("queue %d: header digest mismatch host: %s ctrl: %s\n",
			nvme_tcp_queue_id(queue),
			queue->hdr_digest ? "enabled" : "disabled",
			ctrl_hdgst ? "enabled" : "disabled");
		goto free_icresp;
	}

	if (icresp->cpda != 0) {
		pr_err("queue %d: unsupported cpda returned %d\n",
			nvme_tcp_queue_id(queue), icresp->cpda);
		goto free_icresp;
	}

	ret = 0;
free_icresp:
	kfree(icresp);
free_icreq:
	kfree(icreq);
	return ret;
}

static bool nvme_tcp_admin_queue(struct nvme_tcp_queue *queue)
{
	return nvme_tcp_queue_id(queue) == 0;
}

static bool nvme_tcp_default_queue(struct nvme_tcp_queue *queue)
{
	struct nvme_tcp_ctrl *ctrl = queue->ctrl;
	int qid = nvme_tcp_queue_id(queue);

	return !nvme_tcp_admin_queue(queue) &&
		qid < 1 + ctrl->io_queues[HCTX_TYPE_DEFAULT];
}

static bool nvme_tcp_read_queue(struct nvme_tcp_queue *queue)
{
	struct nvme_tcp_ctrl *ctrl = queue->ctrl;
	int qid = nvme_tcp_queue_id(queue);

	return !nvme_tcp_admin_queue(queue) &&
		!nvme_tcp_default_queue(queue) &&
		qid < 1 + ctrl->io_queues[HCTX_TYPE_DEFAULT] +
			  ctrl->io_queues[HCTX_TYPE_READ];
}

static bool nvme_tcp_poll_queue(struct nvme_tcp_queue *queue)
{
	struct nvme_tcp_ctrl *ctrl = queue->ctrl;
	int qid = nvme_tcp_queue_id(queue);

	return !nvme_tcp_admin_queue(queue) &&
		!nvme_tcp_default_queue(queue) &&
		!nvme_tcp_read_queue(queue) &&
		qid < 1 + ctrl->io_queues[HCTX_TYPE_DEFAULT] +
			  ctrl->io_queues[HCTX_TYPE_READ] +
			  ctrl->io_queues[HCTX_TYPE_POLL];
}

static void nvme_tcp_set_queue_io_cpu(struct nvme_tcp_queue *queue)
{
	struct nvme_tcp_ctrl *ctrl = queue->ctrl;
	int qid = nvme_tcp_queue_id(queue);
	int n = 0;

	if (nvme_tcp_default_queue(queue))
		n = qid - 1;
	else if (nvme_tcp_read_queue(queue))
		n = qid - ctrl->io_queues[HCTX_TYPE_DEFAULT] - 1;
	else if (nvme_tcp_poll_queue(queue))
		n = qid - ctrl->io_queues[HCTX_TYPE_DEFAULT] -
				ctrl->io_queues[HCTX_TYPE_READ] - 1;
	queue->io_cpu = cpumask_next_wrap(n - 1, cpu_online_mask, -1, false);
}

static int nvme_tcp_alloc_queue(struct nvme_ctrl *nctrl,
		int qid, size_t queue_size)
{
	struct nvme_tcp_ctrl *ctrl = to_tcp_ctrl(nctrl);
	struct nvme_tcp_queue *queue = &ctrl->queues[qid];
	int ret, rcv_pdu_size;

	mutex_init(&queue->queue_lock);
	queue->ctrl = ctrl;
	init_llist_head(&queue->req_list);
	INIT_LIST_HEAD(&queue->send_list);
	mutex_init(&queue->send_mutex);
	INIT_WORK(&queue->io_work, nvme_tcp_io_work);
	queue->queue_size = queue_size;

	if (qid > 0)
		queue->cmnd_capsule_len = nctrl->ioccsz * 16;
	else
		queue->cmnd_capsule_len = sizeof(struct nvme_command) +
						NVME_TCP_ADMIN_CCSZ;

	ret = sock_create(ctrl->addr.ss_family, SOCK_STREAM,
			IPPROTO_TCP, &queue->sock);
	if (ret) {
		dev_err(nctrl->device,
			"failed to create socket: %d\n", ret);
		goto err_destroy_mutex;
	}

	/* Single syn retry */
	tcp_sock_set_syncnt(queue->sock->sk, 1);

	/* Set TCP no delay */
	tcp_sock_set_nodelay(queue->sock->sk);

	/*
	 * Cleanup whatever is sitting in the TCP transmit queue on socket
	 * close. This is done to prevent stale data from being sent should
	 * the network connection be restored before TCP times out.
	 */
	sock_no_linger(queue->sock->sk);

	if (so_priority > 0)
		sock_set_priority(queue->sock->sk, so_priority);

	/* Set socket type of service */
	if (nctrl->opts->tos >= 0)
		ip_sock_set_tos(queue->sock->sk, nctrl->opts->tos);

	/* Set 10 seconds timeout for icresp recvmsg */
	queue->sock->sk->sk_rcvtimeo = 10 * HZ;

	queue->sock->sk->sk_allocation = GFP_ATOMIC;
	nvme_tcp_set_queue_io_cpu(queue);
	queue->request = NULL;
	queue->data_remaining = 0;
	queue->ddgst_remaining = 0;
	queue->pdu_remaining = 0;
	queue->pdu_offset = 0;
	sk_set_memalloc(queue->sock->sk);

	if (nctrl->opts->mask & NVMF_OPT_HOST_TRADDR) {
		ret = kernel_bind(queue->sock, (struct sockaddr *)&ctrl->src_addr,
			sizeof(ctrl->src_addr));
		if (ret) {
			dev_err(nctrl->device,
				"failed to bind queue %d socket %d\n",
				qid, ret);
			goto err_sock;
		}
	}

	if (nctrl->opts->mask & NVMF_OPT_HOST_IFACE) {
		char *iface = nctrl->opts->host_iface;
		sockptr_t optval = KERNEL_SOCKPTR(iface);

		ret = sock_setsockopt(queue->sock, SOL_SOCKET, SO_BINDTODEVICE,
				      optval, strlen(iface));
		if (ret) {
			dev_err(nctrl->device,
			  "failed to bind to interface %s queue %d err %d\n",
			  iface, qid, ret);
			goto err_sock;
		}
	}

	queue->hdr_digest = nctrl->opts->hdr_digest;
	queue->data_digest = nctrl->opts->data_digest;
	if (queue->hdr_digest || queue->data_digest) {
		ret = nvme_tcp_alloc_crypto(queue);
		if (ret) {
			dev_err(nctrl->device,
				"failed to allocate queue %d crypto\n", qid);
			goto err_sock;
		}
	}

	rcv_pdu_size = sizeof(struct nvme_tcp_rsp_pdu) +
			nvme_tcp_hdgst_len(queue);
	queue->pdu = kmalloc(rcv_pdu_size, GFP_KERNEL);
	if (!queue->pdu) {
		ret = -ENOMEM;
		goto err_crypto;
	}

	dev_dbg(nctrl->device, "connecting queue %d\n",
			nvme_tcp_queue_id(queue));

	ret = kernel_connect(queue->sock, (struct sockaddr *)&ctrl->addr,
		sizeof(ctrl->addr), 0);
	if (ret) {
		dev_err(nctrl->device,
			"failed to connect socket: %d\n", ret);
		goto err_rcv_pdu;
	}

	ret = nvme_tcp_init_connection(queue);
	if (ret)
		goto err_init_connect;

	queue->rd_enabled = true;
	set_bit(NVME_TCP_Q_ALLOCATED, &queue->flags);
	nvme_tcp_init_recv_ctx(queue);

	write_lock_bh(&queue->sock->sk->sk_callback_lock);
	queue->sock->sk->sk_user_data = queue;
	queue->state_change = queue->sock->sk->sk_state_change;
	queue->data_ready = queue->sock->sk->sk_data_ready;
	queue->write_space = queue->sock->sk->sk_write_space;
	queue->sock->sk->sk_data_ready = nvme_tcp_data_ready;
	queue->sock->sk->sk_state_change = nvme_tcp_state_change;
	queue->sock->sk->sk_write_space = nvme_tcp_write_space;
#ifdef CONFIG_NET_RX_BUSY_POLL
	queue->sock->sk->sk_ll_usec = 1;
#endif
	write_unlock_bh(&queue->sock->sk->sk_callback_lock);

	return 0;

err_init_connect:
	kernel_sock_shutdown(queue->sock, SHUT_RDWR);
err_rcv_pdu:
	kfree(queue->pdu);
err_crypto:
	if (queue->hdr_digest || queue->data_digest)
		nvme_tcp_free_crypto(queue);
err_sock:
	sock_release(queue->sock);
	queue->sock = NULL;
err_destroy_mutex:
	mutex_destroy(&queue->queue_lock);
	return ret;
}

static void nvme_tcp_restore_sock_calls(struct nvme_tcp_queue *queue)
{
	struct socket *sock = queue->sock;

	write_lock_bh(&sock->sk->sk_callback_lock);
	sock->sk->sk_user_data  = NULL;
	sock->sk->sk_data_ready = queue->data_ready;
	sock->sk->sk_state_change = queue->state_change;
	sock->sk->sk_write_space  = queue->write_space;
	write_unlock_bh(&sock->sk->sk_callback_lock);
}

static void __nvme_tcp_stop_queue(struct nvme_tcp_queue *queue)
{
	kernel_sock_shutdown(queue->sock, SHUT_RDWR);
	nvme_tcp_restore_sock_calls(queue);
	cancel_work_sync(&queue->io_work);
}

static void nvme_tcp_stop_queue(struct nvme_ctrl *nctrl, int qid)
{
	struct nvme_tcp_ctrl *ctrl = to_tcp_ctrl(nctrl);
	struct nvme_tcp_queue *queue = &ctrl->queues[qid];

	mutex_lock(&queue->queue_lock);
	if (test_and_clear_bit(NVME_TCP_Q_LIVE, &queue->flags))
		__nvme_tcp_stop_queue(queue);
	mutex_unlock(&queue->queue_lock);
}

static int nvme_tcp_start_queue(struct nvme_ctrl *nctrl, int idx)
{
	struct nvme_tcp_ctrl *ctrl = to_tcp_ctrl(nctrl);
	int ret;

	if (idx)
		ret = nvmf_connect_io_queue(nctrl, idx);
	else
		ret = nvmf_connect_admin_queue(nctrl);

	if (!ret) {
		set_bit(NVME_TCP_Q_LIVE, &ctrl->queues[idx].flags);
	} else {
		if (test_bit(NVME_TCP_Q_ALLOCATED, &ctrl->queues[idx].flags))
			__nvme_tcp_stop_queue(&ctrl->queues[idx]);
		dev_err(nctrl->device,
			"failed to connect queue: %d ret=%d\n", idx, ret);
	}
	return ret;
}

static struct blk_mq_tag_set *nvme_tcp_alloc_tagset(struct nvme_ctrl *nctrl,
		bool admin)
{
	struct nvme_tcp_ctrl *ctrl = to_tcp_ctrl(nctrl);
	struct blk_mq_tag_set *set;
	int ret;

	if (admin) {
		set = &ctrl->admin_tag_set;
		memset(set, 0, sizeof(*set));
		set->ops = &nvme_tcp_admin_mq_ops;
		set->queue_depth = NVME_AQ_MQ_TAG_DEPTH;
		set->reserved_tags = NVMF_RESERVED_TAGS;
		set->numa_node = nctrl->numa_node;
		set->flags = BLK_MQ_F_BLOCKING;
		set->cmd_size = sizeof(struct nvme_tcp_request);
		set->driver_data = ctrl;
		set->nr_hw_queues = 1;
		set->timeout = NVME_ADMIN_TIMEOUT;
	} else {
		set = &ctrl->tag_set;
		memset(set, 0, sizeof(*set));
		set->ops = &nvme_tcp_mq_ops;
		set->queue_depth = nctrl->sqsize + 1;
		set->reserved_tags = NVMF_RESERVED_TAGS;
		set->numa_node = nctrl->numa_node;
		set->flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_BLOCKING;
		set->cmd_size = sizeof(struct nvme_tcp_request);
		set->driver_data = ctrl;
		set->nr_hw_queues = nctrl->queue_count - 1;
		set->timeout = NVME_IO_TIMEOUT;
		set->nr_maps = nctrl->opts->nr_poll_queues ? HCTX_MAX_TYPES : 2;
	}

	ret = blk_mq_alloc_tag_set(set);
	if (ret)
		return ERR_PTR(ret);

	return set;
}

static void nvme_tcp_free_admin_queue(struct nvme_ctrl *ctrl)
{
	if (to_tcp_ctrl(ctrl)->async_req.pdu) {
		cancel_work_sync(&ctrl->async_event_work);
		nvme_tcp_free_async_req(to_tcp_ctrl(ctrl));
		to_tcp_ctrl(ctrl)->async_req.pdu = NULL;
	}

	nvme_tcp_free_queue(ctrl, 0);
}

static void nvme_tcp_free_io_queues(struct nvme_ctrl *ctrl)
{
	int i;

	for (i = 1; i < ctrl->queue_count; i++)
		nvme_tcp_free_queue(ctrl, i);
}

static void nvme_tcp_stop_io_queues(struct nvme_ctrl *ctrl)
{
	int i;

	for (i = 1; i < ctrl->queue_count; i++)
		nvme_tcp_stop_queue(ctrl, i);
}

static int nvme_tcp_start_io_queues(struct nvme_ctrl *ctrl)
{
	int i, ret = 0;

	for (i = 1; i < ctrl->queue_count; i++) {
		ret = nvme_tcp_start_queue(ctrl, i);
		if (ret)
			goto out_stop_queues;
	}

	return 0;

out_stop_queues:
	for (i--; i >= 1; i--)
		nvme_tcp_stop_queue(ctrl, i);
	return ret;
}

static int nvme_tcp_alloc_admin_queue(struct nvme_ctrl *ctrl)
{
	int ret;

	ret = nvme_tcp_alloc_queue(ctrl, 0, NVME_AQ_DEPTH);
	if (ret)
		return ret;

	ret = nvme_tcp_alloc_async_req(to_tcp_ctrl(ctrl));
	if (ret)
		goto out_free_queue;

	return 0;

out_free_queue:
	nvme_tcp_free_queue(ctrl, 0);
	return ret;
}

static int __nvme_tcp_alloc_io_queues(struct nvme_ctrl *ctrl)
{
	int i, ret;

	for (i = 1; i < ctrl->queue_count; i++) {
		ret = nvme_tcp_alloc_queue(ctrl, i,
				ctrl->sqsize + 1);
		if (ret)
			goto out_free_queues;
	}

	return 0;

out_free_queues:
	for (i--; i >= 1; i--)
		nvme_tcp_free_queue(ctrl, i);

	return ret;
}

static unsigned int nvme_tcp_nr_io_queues(struct nvme_ctrl *ctrl)
{
	unsigned int nr_io_queues;

	nr_io_queues = min(ctrl->opts->nr_io_queues, num_online_cpus());
	nr_io_queues += min(ctrl->opts->nr_write_queues, num_online_cpus());
	nr_io_queues += min(ctrl->opts->nr_poll_queues, num_online_cpus());

	return nr_io_queues;
}

static void nvme_tcp_set_io_queues(struct nvme_ctrl *nctrl,
		unsigned int nr_io_queues)
{
	struct nvme_tcp_ctrl *ctrl = to_tcp_ctrl(nctrl);
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

static int nvme_tcp_alloc_io_queues(struct nvme_ctrl *ctrl)
{
	unsigned int nr_io_queues;
	int ret;

	nr_io_queues = nvme_tcp_nr_io_queues(ctrl);
	ret = nvme_set_queue_count(ctrl, &nr_io_queues);
	if (ret)
		return ret;

	ctrl->queue_count = nr_io_queues + 1;
	if (ctrl->queue_count < 2) {
		dev_err(ctrl->device,
			"unable to set any I/O queues\n");
		return -ENOMEM;
	}

	dev_info(ctrl->device,
		"creating %d I/O queues.\n", nr_io_queues);

	nvme_tcp_set_io_queues(ctrl, nr_io_queues);

	return __nvme_tcp_alloc_io_queues(ctrl);
}

static void nvme_tcp_destroy_io_queues(struct nvme_ctrl *ctrl, bool remove)
{
	nvme_tcp_stop_io_queues(ctrl);
	if (remove) {
		blk_cleanup_queue(ctrl->connect_q);
		blk_mq_free_tag_set(ctrl->tagset);
	}
	nvme_tcp_free_io_queues(ctrl);
}

static int nvme_tcp_configure_io_queues(struct nvme_ctrl *ctrl, bool new)
{
	int ret;

	ret = nvme_tcp_alloc_io_queues(ctrl);
	if (ret)
		return ret;

	if (new) {
		ctrl->tagset = nvme_tcp_alloc_tagset(ctrl, false);
		if (IS_ERR(ctrl->tagset)) {
			ret = PTR_ERR(ctrl->tagset);
			goto out_free_io_queues;
		}

		ctrl->connect_q = blk_mq_init_queue(ctrl->tagset);
		if (IS_ERR(ctrl->connect_q)) {
			ret = PTR_ERR(ctrl->connect_q);
			goto out_free_tag_set;
		}
	}

	ret = nvme_tcp_start_io_queues(ctrl);
	if (ret)
		goto out_cleanup_connect_q;

	if (!new) {
		nvme_start_queues(ctrl);
		if (!nvme_wait_freeze_timeout(ctrl, NVME_IO_TIMEOUT)) {
			/*
			 * If we timed out waiting for freeze we are likely to
			 * be stuck.  Fail the controller initialization just
			 * to be safe.
			 */
			ret = -ENODEV;
			goto out_wait_freeze_timed_out;
		}
		blk_mq_update_nr_hw_queues(ctrl->tagset,
			ctrl->queue_count - 1);
		nvme_unfreeze(ctrl);
	}

	return 0;

out_wait_freeze_timed_out:
	nvme_stop_queues(ctrl);
	nvme_sync_io_queues(ctrl);
	nvme_tcp_stop_io_queues(ctrl);
out_cleanup_connect_q:
	nvme_cancel_tagset(ctrl);
	if (new)
		blk_cleanup_queue(ctrl->connect_q);
out_free_tag_set:
	if (new)
		blk_mq_free_tag_set(ctrl->tagset);
out_free_io_queues:
	nvme_tcp_free_io_queues(ctrl);
	return ret;
}

static void nvme_tcp_destroy_admin_queue(struct nvme_ctrl *ctrl, bool remove)
{
	nvme_tcp_stop_queue(ctrl, 0);
	if (remove) {
		blk_cleanup_queue(ctrl->admin_q);
		blk_cleanup_queue(ctrl->fabrics_q);
		blk_mq_free_tag_set(ctrl->admin_tagset);
	}
	nvme_tcp_free_admin_queue(ctrl);
}

static int nvme_tcp_configure_admin_queue(struct nvme_ctrl *ctrl, bool new)
{
	int error;

	error = nvme_tcp_alloc_admin_queue(ctrl);
	if (error)
		return error;

	if (new) {
		ctrl->admin_tagset = nvme_tcp_alloc_tagset(ctrl, true);
		if (IS_ERR(ctrl->admin_tagset)) {
			error = PTR_ERR(ctrl->admin_tagset);
			goto out_free_queue;
		}

		ctrl->fabrics_q = blk_mq_init_queue(ctrl->admin_tagset);
		if (IS_ERR(ctrl->fabrics_q)) {
			error = PTR_ERR(ctrl->fabrics_q);
			goto out_free_tagset;
		}

		ctrl->admin_q = blk_mq_init_queue(ctrl->admin_tagset);
		if (IS_ERR(ctrl->admin_q)) {
			error = PTR_ERR(ctrl->admin_q);
			goto out_cleanup_fabrics_q;
		}
	}

	error = nvme_tcp_start_queue(ctrl, 0);
	if (error)
		goto out_cleanup_queue;

	error = nvme_enable_ctrl(ctrl);
	if (error)
		goto out_stop_queue;

	blk_mq_unquiesce_queue(ctrl->admin_q);

	error = nvme_init_ctrl_finish(ctrl);
	if (error)
		goto out_quiesce_queue;

	return 0;

out_quiesce_queue:
	blk_mq_quiesce_queue(ctrl->admin_q);
	blk_sync_queue(ctrl->admin_q);
out_stop_queue:
	nvme_tcp_stop_queue(ctrl, 0);
	nvme_cancel_admin_tagset(ctrl);
out_cleanup_queue:
	if (new)
		blk_cleanup_queue(ctrl->admin_q);
out_cleanup_fabrics_q:
	if (new)
		blk_cleanup_queue(ctrl->fabrics_q);
out_free_tagset:
	if (new)
		blk_mq_free_tag_set(ctrl->admin_tagset);
out_free_queue:
	nvme_tcp_free_admin_queue(ctrl);
	return error;
}

static void nvme_tcp_teardown_admin_queue(struct nvme_ctrl *ctrl,
		bool remove)
{
	blk_mq_quiesce_queue(ctrl->admin_q);
	blk_sync_queue(ctrl->admin_q);
	nvme_tcp_stop_queue(ctrl, 0);
	nvme_cancel_admin_tagset(ctrl);
	if (remove)
		blk_mq_unquiesce_queue(ctrl->admin_q);
	nvme_tcp_destroy_admin_queue(ctrl, remove);
}

static void nvme_tcp_teardown_io_queues(struct nvme_ctrl *ctrl,
		bool remove)
{
	if (ctrl->queue_count <= 1)
		return;
	blk_mq_quiesce_queue(ctrl->admin_q);
	nvme_start_freeze(ctrl);
	nvme_stop_queues(ctrl);
	nvme_sync_io_queues(ctrl);
	nvme_tcp_stop_io_queues(ctrl);
	nvme_cancel_tagset(ctrl);
	if (remove)
		nvme_start_queues(ctrl);
	nvme_tcp_destroy_io_queues(ctrl, remove);
}

static void nvme_tcp_reconnect_or_remove(struct nvme_ctrl *ctrl)
{
	/* If we are resetting/deleting then do nothing */
	if (ctrl->state != NVME_CTRL_CONNECTING) {
		WARN_ON_ONCE(ctrl->state == NVME_CTRL_NEW ||
			ctrl->state == NVME_CTRL_LIVE);
		return;
	}

	if (nvmf_should_reconnect(ctrl)) {
		dev_info(ctrl->device, "Reconnecting in %d seconds...\n",
			ctrl->opts->reconnect_delay);
		queue_delayed_work(nvme_wq, &to_tcp_ctrl(ctrl)->connect_work,
				ctrl->opts->reconnect_delay * HZ);
	} else {
		dev_info(ctrl->device, "Removing controller...\n");
		nvme_delete_ctrl(ctrl);
	}
}

static int nvme_tcp_setup_ctrl(struct nvme_ctrl *ctrl, bool new)
{
	struct nvmf_ctrl_options *opts = ctrl->opts;
	int ret;

	ret = nvme_tcp_configure_admin_queue(ctrl, new);
	if (ret)
		return ret;

	if (ctrl->icdoff) {
		ret = -EOPNOTSUPP;
		dev_err(ctrl->device, "icdoff is not supported!\n");
		goto destroy_admin;
	}

	if (!nvme_ctrl_sgl_supported(ctrl)) {
		ret = -EOPNOTSUPP;
		dev_err(ctrl->device, "Mandatory sgls are not supported!\n");
		goto destroy_admin;
	}

	if (opts->queue_size > ctrl->sqsize + 1)
		dev_warn(ctrl->device,
			"queue_size %zu > ctrl sqsize %u, clamping down\n",
			opts->queue_size, ctrl->sqsize + 1);

	if (ctrl->sqsize + 1 > ctrl->maxcmd) {
		dev_warn(ctrl->device,
			"sqsize %u > ctrl maxcmd %u, clamping down\n",
			ctrl->sqsize + 1, ctrl->maxcmd);
		ctrl->sqsize = ctrl->maxcmd - 1;
	}

	if (ctrl->queue_count > 1) {
		ret = nvme_tcp_configure_io_queues(ctrl, new);
		if (ret)
			goto destroy_admin;
	}

	if (!nvme_change_ctrl_state(ctrl, NVME_CTRL_LIVE)) {
		/*
		 * state change failure is ok if we started ctrl delete,
		 * unless we're during creation of a new controller to
		 * avoid races with teardown flow.
		 */
		WARN_ON_ONCE(ctrl->state != NVME_CTRL_DELETING &&
			     ctrl->state != NVME_CTRL_DELETING_NOIO);
		WARN_ON_ONCE(new);
		ret = -EINVAL;
		goto destroy_io;
	}

	nvme_start_ctrl(ctrl);
	return 0;

destroy_io:
	if (ctrl->queue_count > 1) {
		nvme_stop_queues(ctrl);
		nvme_sync_io_queues(ctrl);
		nvme_tcp_stop_io_queues(ctrl);
		nvme_cancel_tagset(ctrl);
		nvme_tcp_destroy_io_queues(ctrl, new);
	}
destroy_admin:
	blk_mq_quiesce_queue(ctrl->admin_q);
	blk_sync_queue(ctrl->admin_q);
	nvme_tcp_stop_queue(ctrl, 0);
	nvme_cancel_admin_tagset(ctrl);
	nvme_tcp_destroy_admin_queue(ctrl, new);
	return ret;
}

static void nvme_tcp_reconnect_ctrl_work(struct work_struct *work)
{
	struct nvme_tcp_ctrl *tcp_ctrl = container_of(to_delayed_work(work),
			struct nvme_tcp_ctrl, connect_work);
	struct nvme_ctrl *ctrl = &tcp_ctrl->ctrl;

	++ctrl->nr_reconnects;

	if (nvme_tcp_setup_ctrl(ctrl, false))
		goto requeue;

	dev_info(ctrl->device, "Successfully reconnected (%d attempt)\n",
			ctrl->nr_reconnects);

	ctrl->nr_reconnects = 0;

	return;

requeue:
	dev_info(ctrl->device, "Failed reconnect attempt %d\n",
			ctrl->nr_reconnects);
	nvme_tcp_reconnect_or_remove(ctrl);
}

static void nvme_tcp_error_recovery_work(struct work_struct *work)
{
	struct nvme_tcp_ctrl *tcp_ctrl = container_of(work,
				struct nvme_tcp_ctrl, err_work);
	struct nvme_ctrl *ctrl = &tcp_ctrl->ctrl;

	nvme_stop_keep_alive(ctrl);
	nvme_tcp_teardown_io_queues(ctrl, false);
	/* unquiesce to fail fast pending requests */
	nvme_start_queues(ctrl);
	nvme_tcp_teardown_admin_queue(ctrl, false);
	blk_mq_unquiesce_queue(ctrl->admin_q);

	if (!nvme_change_ctrl_state(ctrl, NVME_CTRL_CONNECTING)) {
		/* state change failure is ok if we started ctrl delete */
		WARN_ON_ONCE(ctrl->state != NVME_CTRL_DELETING &&
			     ctrl->state != NVME_CTRL_DELETING_NOIO);
		return;
	}

	nvme_tcp_reconnect_or_remove(ctrl);
}

static void nvme_tcp_teardown_ctrl(struct nvme_ctrl *ctrl, bool shutdown)
{
	cancel_work_sync(&to_tcp_ctrl(ctrl)->err_work);
	cancel_delayed_work_sync(&to_tcp_ctrl(ctrl)->connect_work);

	nvme_tcp_teardown_io_queues(ctrl, shutdown);
	blk_mq_quiesce_queue(ctrl->admin_q);
	if (shutdown)
		nvme_shutdown_ctrl(ctrl);
	else
		nvme_disable_ctrl(ctrl);
	nvme_tcp_teardown_admin_queue(ctrl, shutdown);
}

static void nvme_tcp_delete_ctrl(struct nvme_ctrl *ctrl)
{
	nvme_tcp_teardown_ctrl(ctrl, true);
}

static void nvme_reset_ctrl_work(struct work_struct *work)
{
	struct nvme_ctrl *ctrl =
		container_of(work, struct nvme_ctrl, reset_work);

	nvme_stop_ctrl(ctrl);
	nvme_tcp_teardown_ctrl(ctrl, false);

	if (!nvme_change_ctrl_state(ctrl, NVME_CTRL_CONNECTING)) {
		/* state change failure is ok if we started ctrl delete */
		WARN_ON_ONCE(ctrl->state != NVME_CTRL_DELETING &&
			     ctrl->state != NVME_CTRL_DELETING_NOIO);
		return;
	}

	if (nvme_tcp_setup_ctrl(ctrl, false))
		goto out_fail;

	return;

out_fail:
	++ctrl->nr_reconnects;
	nvme_tcp_reconnect_or_remove(ctrl);
}

static void nvme_tcp_free_ctrl(struct nvme_ctrl *nctrl)
{
	struct nvme_tcp_ctrl *ctrl = to_tcp_ctrl(nctrl);

	if (list_empty(&ctrl->list))
		goto free_ctrl;

	mutex_lock(&nvme_tcp_ctrl_mutex);
	list_del(&ctrl->list);
	mutex_unlock(&nvme_tcp_ctrl_mutex);

	nvmf_free_options(nctrl->opts);
free_ctrl:
	kfree(ctrl->queues);
	kfree(ctrl);
}

static void nvme_tcp_set_sg_null(struct nvme_command *c)
{
	struct nvme_sgl_desc *sg = &c->common.dptr.sgl;

	sg->addr = 0;
	sg->length = 0;
	sg->type = (NVME_TRANSPORT_SGL_DATA_DESC << 4) |
			NVME_SGL_FMT_TRANSPORT_A;
}

static void nvme_tcp_set_sg_inline(struct nvme_tcp_queue *queue,
		struct nvme_command *c, u32 data_len)
{
	struct nvme_sgl_desc *sg = &c->common.dptr.sgl;

	sg->addr = cpu_to_le64(queue->ctrl->ctrl.icdoff);
	sg->length = cpu_to_le32(data_len);
	sg->type = (NVME_SGL_FMT_DATA_DESC << 4) | NVME_SGL_FMT_OFFSET;
}

static void nvme_tcp_set_sg_host_data(struct nvme_command *c,
		u32 data_len)
{
	struct nvme_sgl_desc *sg = &c->common.dptr.sgl;

	sg->addr = 0;
	sg->length = cpu_to_le32(data_len);
	sg->type = (NVME_TRANSPORT_SGL_DATA_DESC << 4) |
			NVME_SGL_FMT_TRANSPORT_A;
}

static void nvme_tcp_submit_async_event(struct nvme_ctrl *arg)
{
	struct nvme_tcp_ctrl *ctrl = to_tcp_ctrl(arg);
	struct nvme_tcp_queue *queue = &ctrl->queues[0];
	struct nvme_tcp_cmd_pdu *pdu = ctrl->async_req.pdu;
	struct nvme_command *cmd = &pdu->cmd;
	u8 hdgst = nvme_tcp_hdgst_len(queue);

	memset(pdu, 0, sizeof(*pdu));
	pdu->hdr.type = nvme_tcp_cmd;
	if (queue->hdr_digest)
		pdu->hdr.flags |= NVME_TCP_F_HDGST;
	pdu->hdr.hlen = sizeof(*pdu);
	pdu->hdr.plen = cpu_to_le32(pdu->hdr.hlen + hdgst);

	cmd->common.opcode = nvme_admin_async_event;
	cmd->common.command_id = NVME_AQ_BLK_MQ_DEPTH;
	cmd->common.flags |= NVME_CMD_SGL_METABUF;
	nvme_tcp_set_sg_null(cmd);

	ctrl->async_req.state = NVME_TCP_SEND_CMD_PDU;
	ctrl->async_req.offset = 0;
	ctrl->async_req.curr_bio = NULL;
	ctrl->async_req.data_len = 0;

	nvme_tcp_queue_request(&ctrl->async_req, true, true);
}

static void nvme_tcp_complete_timed_out(struct request *rq)
{
	struct nvme_tcp_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_ctrl *ctrl = &req->queue->ctrl->ctrl;

	nvme_tcp_stop_queue(ctrl, nvme_tcp_queue_id(req->queue));
	if (blk_mq_request_started(rq) && !blk_mq_request_completed(rq)) {
		nvme_req(rq)->status = NVME_SC_HOST_ABORTED_CMD;
		blk_mq_complete_request(rq);
	}
}

static enum blk_eh_timer_return
nvme_tcp_timeout(struct request *rq, bool reserved)
{
	struct nvme_tcp_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_ctrl *ctrl = &req->queue->ctrl->ctrl;
	struct nvme_tcp_cmd_pdu *pdu = req->pdu;

	dev_warn(ctrl->device,
		"queue %d: timeout request %#x type %d\n",
		nvme_tcp_queue_id(req->queue), rq->tag, pdu->hdr.type);

	if (ctrl->state != NVME_CTRL_LIVE) {
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
		nvme_tcp_complete_timed_out(rq);
		return BLK_EH_DONE;
	}

	/*
	 * LIVE state should trigger the normal error recovery which will
	 * handle completing this request.
	 */
	nvme_tcp_error_recovery(ctrl);
	return BLK_EH_RESET_TIMER;
}

static blk_status_t nvme_tcp_map_data(struct nvme_tcp_queue *queue,
			struct request *rq)
{
	struct nvme_tcp_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_tcp_cmd_pdu *pdu = req->pdu;
	struct nvme_command *c = &pdu->cmd;

	c->common.flags |= NVME_CMD_SGL_METABUF;

	if (!blk_rq_nr_phys_segments(rq))
		nvme_tcp_set_sg_null(c);
	else if (rq_data_dir(rq) == WRITE &&
	    req->data_len <= nvme_tcp_inline_data_size(queue))
		nvme_tcp_set_sg_inline(queue, c, req->data_len);
	else
		nvme_tcp_set_sg_host_data(c, req->data_len);

	return 0;
}

static blk_status_t nvme_tcp_setup_cmd_pdu(struct nvme_ns *ns,
		struct request *rq)
{
	struct nvme_tcp_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_tcp_cmd_pdu *pdu = req->pdu;
	struct nvme_tcp_queue *queue = req->queue;
	u8 hdgst = nvme_tcp_hdgst_len(queue), ddgst = 0;
	blk_status_t ret;

	ret = nvme_setup_cmd(ns, rq);
	if (ret)
		return ret;

	req->state = NVME_TCP_SEND_CMD_PDU;
	req->offset = 0;
	req->data_sent = 0;
	req->pdu_len = 0;
	req->pdu_sent = 0;
	req->data_len = blk_rq_nr_phys_segments(rq) ?
				blk_rq_payload_bytes(rq) : 0;
	req->curr_bio = rq->bio;
	if (req->curr_bio && req->data_len)
		nvme_tcp_init_iter(req, rq_data_dir(rq));

	if (rq_data_dir(rq) == WRITE &&
	    req->data_len <= nvme_tcp_inline_data_size(queue))
		req->pdu_len = req->data_len;

	pdu->hdr.type = nvme_tcp_cmd;
	pdu->hdr.flags = 0;
	if (queue->hdr_digest)
		pdu->hdr.flags |= NVME_TCP_F_HDGST;
	if (queue->data_digest && req->pdu_len) {
		pdu->hdr.flags |= NVME_TCP_F_DDGST;
		ddgst = nvme_tcp_ddgst_len(queue);
	}
	pdu->hdr.hlen = sizeof(*pdu);
	pdu->hdr.pdo = req->pdu_len ? pdu->hdr.hlen + hdgst : 0;
	pdu->hdr.plen =
		cpu_to_le32(pdu->hdr.hlen + hdgst + req->pdu_len + ddgst);

	ret = nvme_tcp_map_data(queue, rq);
	if (unlikely(ret)) {
		nvme_cleanup_cmd(rq);
		dev_err(queue->ctrl->ctrl.device,
			"Failed to map data (%d)\n", ret);
		return ret;
	}

	return 0;
}

static void nvme_tcp_commit_rqs(struct blk_mq_hw_ctx *hctx)
{
	struct nvme_tcp_queue *queue = hctx->driver_data;

	if (!llist_empty(&queue->req_list))
		queue_work_on(queue->io_cpu, nvme_tcp_wq, &queue->io_work);
}

static blk_status_t nvme_tcp_queue_rq(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd)
{
	struct nvme_ns *ns = hctx->queue->queuedata;
	struct nvme_tcp_queue *queue = hctx->driver_data;
	struct request *rq = bd->rq;
	struct nvme_tcp_request *req = blk_mq_rq_to_pdu(rq);
	bool queue_ready = test_bit(NVME_TCP_Q_LIVE, &queue->flags);
	blk_status_t ret;

	if (!nvme_check_ready(&queue->ctrl->ctrl, rq, queue_ready))
		return nvme_fail_nonready_command(&queue->ctrl->ctrl, rq);

	ret = nvme_tcp_setup_cmd_pdu(ns, rq);
	if (unlikely(ret))
		return ret;

	blk_mq_start_request(rq);

	nvme_tcp_queue_request(req, true, bd->last);

	return BLK_STS_OK;
}

static int nvme_tcp_map_queues(struct blk_mq_tag_set *set)
{
	struct nvme_tcp_ctrl *ctrl = set->driver_data;
	struct nvmf_ctrl_options *opts = ctrl->ctrl.opts;

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

	dev_info(ctrl->ctrl.device,
		"mapped %d/%d/%d default/read/poll queues.\n",
		ctrl->io_queues[HCTX_TYPE_DEFAULT],
		ctrl->io_queues[HCTX_TYPE_READ],
		ctrl->io_queues[HCTX_TYPE_POLL]);

	return 0;
}

static int nvme_tcp_poll(struct blk_mq_hw_ctx *hctx)
{
	struct nvme_tcp_queue *queue = hctx->driver_data;
	struct sock *sk = queue->sock->sk;

	if (!test_bit(NVME_TCP_Q_LIVE, &queue->flags))
		return 0;

	set_bit(NVME_TCP_Q_POLLING, &queue->flags);
	if (sk_can_busy_loop(sk) && skb_queue_empty_lockless(&sk->sk_receive_queue))
		sk_busy_loop(sk, true);
	nvme_tcp_try_recv(queue);
	clear_bit(NVME_TCP_Q_POLLING, &queue->flags);
	return queue->nr_cqe;
}

static const struct blk_mq_ops nvme_tcp_mq_ops = {
	.queue_rq	= nvme_tcp_queue_rq,
	.commit_rqs	= nvme_tcp_commit_rqs,
	.complete	= nvme_complete_rq,
	.init_request	= nvme_tcp_init_request,
	.exit_request	= nvme_tcp_exit_request,
	.init_hctx	= nvme_tcp_init_hctx,
	.timeout	= nvme_tcp_timeout,
	.map_queues	= nvme_tcp_map_queues,
	.poll		= nvme_tcp_poll,
};

static const struct blk_mq_ops nvme_tcp_admin_mq_ops = {
	.queue_rq	= nvme_tcp_queue_rq,
	.complete	= nvme_complete_rq,
	.init_request	= nvme_tcp_init_request,
	.exit_request	= nvme_tcp_exit_request,
	.init_hctx	= nvme_tcp_init_admin_hctx,
	.timeout	= nvme_tcp_timeout,
};

static const struct nvme_ctrl_ops nvme_tcp_ctrl_ops = {
	.name			= "tcp",
	.module			= THIS_MODULE,
	.flags			= NVME_F_FABRICS,
	.reg_read32		= nvmf_reg_read32,
	.reg_read64		= nvmf_reg_read64,
	.reg_write32		= nvmf_reg_write32,
	.free_ctrl		= nvme_tcp_free_ctrl,
	.submit_async_event	= nvme_tcp_submit_async_event,
	.delete_ctrl		= nvme_tcp_delete_ctrl,
	.get_address		= nvmf_get_address,
};

static bool
nvme_tcp_existing_controller(struct nvmf_ctrl_options *opts)
{
	struct nvme_tcp_ctrl *ctrl;
	bool found = false;

	mutex_lock(&nvme_tcp_ctrl_mutex);
	list_for_each_entry(ctrl, &nvme_tcp_ctrl_list, list) {
		found = nvmf_ip_options_match(&ctrl->ctrl, opts);
		if (found)
			break;
	}
	mutex_unlock(&nvme_tcp_ctrl_mutex);

	return found;
}

static struct nvme_ctrl *nvme_tcp_create_ctrl(struct device *dev,
		struct nvmf_ctrl_options *opts)
{
	struct nvme_tcp_ctrl *ctrl;
	int ret;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&ctrl->list);
	ctrl->ctrl.opts = opts;
	ctrl->ctrl.queue_count = opts->nr_io_queues + opts->nr_write_queues +
				opts->nr_poll_queues + 1;
	ctrl->ctrl.sqsize = opts->queue_size - 1;
	ctrl->ctrl.kato = opts->kato;

	INIT_DELAYED_WORK(&ctrl->connect_work,
			nvme_tcp_reconnect_ctrl_work);
	INIT_WORK(&ctrl->err_work, nvme_tcp_error_recovery_work);
	INIT_WORK(&ctrl->ctrl.reset_work, nvme_reset_ctrl_work);

	if (!(opts->mask & NVMF_OPT_TRSVCID)) {
		opts->trsvcid =
			kstrdup(__stringify(NVME_TCP_DISC_PORT), GFP_KERNEL);
		if (!opts->trsvcid) {
			ret = -ENOMEM;
			goto out_free_ctrl;
		}
		opts->mask |= NVMF_OPT_TRSVCID;
	}

	ret = inet_pton_with_scope(&init_net, AF_UNSPEC,
			opts->traddr, opts->trsvcid, &ctrl->addr);
	if (ret) {
		pr_err("malformed address passed: %s:%s\n",
			opts->traddr, opts->trsvcid);
		goto out_free_ctrl;
	}

	if (opts->mask & NVMF_OPT_HOST_TRADDR) {
		ret = inet_pton_with_scope(&init_net, AF_UNSPEC,
			opts->host_traddr, NULL, &ctrl->src_addr);
		if (ret) {
			pr_err("malformed src address passed: %s\n",
			       opts->host_traddr);
			goto out_free_ctrl;
		}
	}

	if (opts->mask & NVMF_OPT_HOST_IFACE) {
		ctrl->ndev = dev_get_by_name(&init_net, opts->host_iface);
		if (!ctrl->ndev) {
			pr_err("invalid interface passed: %s\n",
			       opts->host_iface);
			ret = -ENODEV;
			goto out_free_ctrl;
		}
	}

	if (!opts->duplicate_connect && nvme_tcp_existing_controller(opts)) {
		ret = -EALREADY;
		goto out_free_ctrl;
	}

	ctrl->queues = kcalloc(ctrl->ctrl.queue_count, sizeof(*ctrl->queues),
				GFP_KERNEL);
	if (!ctrl->queues) {
		ret = -ENOMEM;
		goto out_free_ctrl;
	}

	ret = nvme_init_ctrl(&ctrl->ctrl, dev, &nvme_tcp_ctrl_ops, 0);
	if (ret)
		goto out_kfree_queues;

	if (!nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_CONNECTING)) {
		WARN_ON_ONCE(1);
		ret = -EINTR;
		goto out_uninit_ctrl;
	}

	ret = nvme_tcp_setup_ctrl(&ctrl->ctrl, true);
	if (ret)
		goto out_uninit_ctrl;

	dev_info(ctrl->ctrl.device, "new ctrl: NQN \"%s\", addr %pISp\n",
		ctrl->ctrl.opts->subsysnqn, &ctrl->addr);

	mutex_lock(&nvme_tcp_ctrl_mutex);
	list_add_tail(&ctrl->list, &nvme_tcp_ctrl_list);
	mutex_unlock(&nvme_tcp_ctrl_mutex);

	return &ctrl->ctrl;

out_uninit_ctrl:
	nvme_uninit_ctrl(&ctrl->ctrl);
	nvme_put_ctrl(&ctrl->ctrl);
	if (ret > 0)
		ret = -EIO;
	return ERR_PTR(ret);
out_kfree_queues:
	kfree(ctrl->queues);
out_free_ctrl:
	kfree(ctrl);
	return ERR_PTR(ret);
}

static struct nvmf_transport_ops nvme_tcp_transport = {
	.name		= "tcp",
	.module		= THIS_MODULE,
	.required_opts	= NVMF_OPT_TRADDR,
	.allowed_opts	= NVMF_OPT_TRSVCID | NVMF_OPT_RECONNECT_DELAY |
			  NVMF_OPT_HOST_TRADDR | NVMF_OPT_CTRL_LOSS_TMO |
			  NVMF_OPT_HDR_DIGEST | NVMF_OPT_DATA_DIGEST |
			  NVMF_OPT_NR_WRITE_QUEUES | NVMF_OPT_NR_POLL_QUEUES |
			  NVMF_OPT_TOS | NVMF_OPT_HOST_IFACE,
	.create_ctrl	= nvme_tcp_create_ctrl,
};

static int __init nvme_tcp_init_module(void)
{
	nvme_tcp_wq = alloc_workqueue("nvme_tcp_wq",
			WQ_MEM_RECLAIM | WQ_HIGHPRI, 0);
	if (!nvme_tcp_wq)
		return -ENOMEM;

	nvmf_register_transport(&nvme_tcp_transport);
	return 0;
}

static void __exit nvme_tcp_cleanup_module(void)
{
	struct nvme_tcp_ctrl *ctrl;

	nvmf_unregister_transport(&nvme_tcp_transport);

	mutex_lock(&nvme_tcp_ctrl_mutex);
	list_for_each_entry(ctrl, &nvme_tcp_ctrl_list, list)
		nvme_delete_ctrl(&ctrl->ctrl);
	mutex_unlock(&nvme_tcp_ctrl_mutex);
	flush_workqueue(nvme_delete_wq);

	destroy_workqueue(nvme_tcp_wq);
}

module_init(nvme_tcp_init_module);
module_exit(nvme_tcp_cleanup_module);

MODULE_LICENSE("GPL v2");
