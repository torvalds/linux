// SPDX-License-Identifier: GPL-2.0
/*
 * NVMe over Fabrics TCP target.
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
#include <linux/inet.h>
#include <linux/llist.h>
#include <crypto/hash.h>

#include "nvmet.h"

#define NVMET_TCP_DEF_INLINE_DATA_SIZE	(4 * PAGE_SIZE)

#define NVMET_TCP_RECV_BUDGET		8
#define NVMET_TCP_SEND_BUDGET		8
#define NVMET_TCP_IO_WORK_BUDGET	64

enum nvmet_tcp_send_state {
	NVMET_TCP_SEND_DATA_PDU,
	NVMET_TCP_SEND_DATA,
	NVMET_TCP_SEND_R2T,
	NVMET_TCP_SEND_DDGST,
	NVMET_TCP_SEND_RESPONSE
};

enum nvmet_tcp_recv_state {
	NVMET_TCP_RECV_PDU,
	NVMET_TCP_RECV_DATA,
	NVMET_TCP_RECV_DDGST,
	NVMET_TCP_RECV_ERR,
};

enum {
	NVMET_TCP_F_INIT_FAILED = (1 << 0),
};

struct nvmet_tcp_cmd {
	struct nvmet_tcp_queue		*queue;
	struct nvmet_req		req;

	struct nvme_tcp_cmd_pdu		*cmd_pdu;
	struct nvme_tcp_rsp_pdu		*rsp_pdu;
	struct nvme_tcp_data_pdu	*data_pdu;
	struct nvme_tcp_r2t_pdu		*r2t_pdu;

	u32				rbytes_done;
	u32				wbytes_done;

	u32				pdu_len;
	u32				pdu_recv;
	int				sg_idx;
	int				nr_mapped;
	struct msghdr			recv_msg;
	struct kvec			*iov;
	u32				flags;

	struct list_head		entry;
	struct llist_node		lentry;

	/* send state */
	u32				offset;
	struct scatterlist		*cur_sg;
	enum nvmet_tcp_send_state	state;

	__le32				exp_ddgst;
	__le32				recv_ddgst;
};

enum nvmet_tcp_queue_state {
	NVMET_TCP_Q_CONNECTING,
	NVMET_TCP_Q_LIVE,
	NVMET_TCP_Q_DISCONNECTING,
};

struct nvmet_tcp_queue {
	struct socket		*sock;
	struct nvmet_tcp_port	*port;
	struct work_struct	io_work;
	int			cpu;
	struct nvmet_cq		nvme_cq;
	struct nvmet_sq		nvme_sq;

	/* send state */
	struct nvmet_tcp_cmd	*cmds;
	unsigned int		nr_cmds;
	struct list_head	free_list;
	struct llist_head	resp_list;
	struct list_head	resp_send_list;
	int			send_list_len;
	struct nvmet_tcp_cmd	*snd_cmd;

	/* recv state */
	int			offset;
	int			left;
	enum nvmet_tcp_recv_state rcv_state;
	struct nvmet_tcp_cmd	*cmd;
	union nvme_tcp_pdu	pdu;

	/* digest state */
	bool			hdr_digest;
	bool			data_digest;
	struct ahash_request	*snd_hash;
	struct ahash_request	*rcv_hash;

	spinlock_t		state_lock;
	enum nvmet_tcp_queue_state state;

	struct sockaddr_storage	sockaddr;
	struct sockaddr_storage	sockaddr_peer;
	struct work_struct	release_work;

	int			idx;
	struct list_head	queue_list;

	struct nvmet_tcp_cmd	connect;

	struct page_frag_cache	pf_cache;

	void (*data_ready)(struct sock *);
	void (*state_change)(struct sock *);
	void (*write_space)(struct sock *);
};

struct nvmet_tcp_port {
	struct socket		*sock;
	struct work_struct	accept_work;
	struct nvmet_port	*nport;
	struct sockaddr_storage addr;
	int			last_cpu;
	void (*data_ready)(struct sock *);
};

static DEFINE_IDA(nvmet_tcp_queue_ida);
static LIST_HEAD(nvmet_tcp_queue_list);
static DEFINE_MUTEX(nvmet_tcp_queue_mutex);

static struct workqueue_struct *nvmet_tcp_wq;
static struct nvmet_fabrics_ops nvmet_tcp_ops;
static void nvmet_tcp_free_cmd(struct nvmet_tcp_cmd *c);
static void nvmet_tcp_finish_cmd(struct nvmet_tcp_cmd *cmd);

static inline u16 nvmet_tcp_cmd_tag(struct nvmet_tcp_queue *queue,
		struct nvmet_tcp_cmd *cmd)
{
	return cmd - queue->cmds;
}

static inline bool nvmet_tcp_has_data_in(struct nvmet_tcp_cmd *cmd)
{
	return nvme_is_write(cmd->req.cmd) &&
		cmd->rbytes_done < cmd->req.transfer_len;
}

static inline bool nvmet_tcp_need_data_in(struct nvmet_tcp_cmd *cmd)
{
	return nvmet_tcp_has_data_in(cmd) && !cmd->req.rsp->status;
}

static inline bool nvmet_tcp_need_data_out(struct nvmet_tcp_cmd *cmd)
{
	return !nvme_is_write(cmd->req.cmd) &&
		cmd->req.transfer_len > 0 &&
		!cmd->req.rsp->status;
}

static inline bool nvmet_tcp_has_inline_data(struct nvmet_tcp_cmd *cmd)
{
	return nvme_is_write(cmd->req.cmd) && cmd->pdu_len &&
		!cmd->rbytes_done;
}

static inline struct nvmet_tcp_cmd *
nvmet_tcp_get_cmd(struct nvmet_tcp_queue *queue)
{
	struct nvmet_tcp_cmd *cmd;

	cmd = list_first_entry_or_null(&queue->free_list,
				struct nvmet_tcp_cmd, entry);
	if (!cmd)
		return NULL;
	list_del_init(&cmd->entry);

	cmd->rbytes_done = cmd->wbytes_done = 0;
	cmd->pdu_len = 0;
	cmd->pdu_recv = 0;
	cmd->iov = NULL;
	cmd->flags = 0;
	return cmd;
}

static inline void nvmet_tcp_put_cmd(struct nvmet_tcp_cmd *cmd)
{
	if (unlikely(cmd == &cmd->queue->connect))
		return;

	list_add_tail(&cmd->entry, &cmd->queue->free_list);
}

static inline u8 nvmet_tcp_hdgst_len(struct nvmet_tcp_queue *queue)
{
	return queue->hdr_digest ? NVME_TCP_DIGEST_LENGTH : 0;
}

static inline u8 nvmet_tcp_ddgst_len(struct nvmet_tcp_queue *queue)
{
	return queue->data_digest ? NVME_TCP_DIGEST_LENGTH : 0;
}

static inline void nvmet_tcp_hdgst(struct ahash_request *hash,
		void *pdu, size_t len)
{
	struct scatterlist sg;

	sg_init_one(&sg, pdu, len);
	ahash_request_set_crypt(hash, &sg, pdu + len, len);
	crypto_ahash_digest(hash);
}

static int nvmet_tcp_verify_hdgst(struct nvmet_tcp_queue *queue,
	void *pdu, size_t len)
{
	struct nvme_tcp_hdr *hdr = pdu;
	__le32 recv_digest;
	__le32 exp_digest;

	if (unlikely(!(hdr->flags & NVME_TCP_F_HDGST))) {
		pr_err("queue %d: header digest enabled but no header digest\n",
			queue->idx);
		return -EPROTO;
	}

	recv_digest = *(__le32 *)(pdu + hdr->hlen);
	nvmet_tcp_hdgst(queue->rcv_hash, pdu, len);
	exp_digest = *(__le32 *)(pdu + hdr->hlen);
	if (recv_digest != exp_digest) {
		pr_err("queue %d: header digest error: recv %#x expected %#x\n",
			queue->idx, le32_to_cpu(recv_digest),
			le32_to_cpu(exp_digest));
		return -EPROTO;
	}

	return 0;
}

static int nvmet_tcp_check_ddgst(struct nvmet_tcp_queue *queue, void *pdu)
{
	struct nvme_tcp_hdr *hdr = pdu;
	u8 digest_len = nvmet_tcp_hdgst_len(queue);
	u32 len;

	len = le32_to_cpu(hdr->plen) - hdr->hlen -
		(hdr->flags & NVME_TCP_F_HDGST ? digest_len : 0);

	if (unlikely(len && !(hdr->flags & NVME_TCP_F_DDGST))) {
		pr_err("queue %d: data digest flag is cleared\n", queue->idx);
		return -EPROTO;
	}

	return 0;
}

static void nvmet_tcp_unmap_pdu_iovec(struct nvmet_tcp_cmd *cmd)
{
	struct scatterlist *sg;
	int i;

	sg = &cmd->req.sg[cmd->sg_idx];

	for (i = 0; i < cmd->nr_mapped; i++)
		kunmap(sg_page(&sg[i]));
}

static void nvmet_tcp_map_pdu_iovec(struct nvmet_tcp_cmd *cmd)
{
	struct kvec *iov = cmd->iov;
	struct scatterlist *sg;
	u32 length, offset, sg_offset;

	length = cmd->pdu_len;
	cmd->nr_mapped = DIV_ROUND_UP(length, PAGE_SIZE);
	offset = cmd->rbytes_done;
	cmd->sg_idx = DIV_ROUND_UP(offset, PAGE_SIZE);
	sg_offset = offset % PAGE_SIZE;
	sg = &cmd->req.sg[cmd->sg_idx];

	while (length) {
		u32 iov_len = min_t(u32, length, sg->length - sg_offset);

		iov->iov_base = kmap(sg_page(sg)) + sg->offset + sg_offset;
		iov->iov_len = iov_len;

		length -= iov_len;
		sg = sg_next(sg);
		iov++;
	}

	iov_iter_kvec(&cmd->recv_msg.msg_iter, READ, cmd->iov,
		cmd->nr_mapped, cmd->pdu_len);
}

static void nvmet_tcp_fatal_error(struct nvmet_tcp_queue *queue)
{
	queue->rcv_state = NVMET_TCP_RECV_ERR;
	if (queue->nvme_sq.ctrl)
		nvmet_ctrl_fatal_error(queue->nvme_sq.ctrl);
	else
		kernel_sock_shutdown(queue->sock, SHUT_RDWR);
}

static int nvmet_tcp_map_data(struct nvmet_tcp_cmd *cmd)
{
	struct nvme_sgl_desc *sgl = &cmd->req.cmd->common.dptr.sgl;
	u32 len = le32_to_cpu(sgl->length);

	if (!cmd->req.data_len)
		return 0;

	if (sgl->type == ((NVME_SGL_FMT_DATA_DESC << 4) |
			  NVME_SGL_FMT_OFFSET)) {
		if (!nvme_is_write(cmd->req.cmd))
			return NVME_SC_INVALID_FIELD | NVME_SC_DNR;

		if (len > cmd->req.port->inline_data_size)
			return NVME_SC_SGL_INVALID_OFFSET | NVME_SC_DNR;
		cmd->pdu_len = len;
	}
	cmd->req.transfer_len += len;

	cmd->req.sg = sgl_alloc(len, GFP_KERNEL, &cmd->req.sg_cnt);
	if (!cmd->req.sg)
		return NVME_SC_INTERNAL;
	cmd->cur_sg = cmd->req.sg;

	if (nvmet_tcp_has_data_in(cmd)) {
		cmd->iov = kmalloc_array(cmd->req.sg_cnt,
				sizeof(*cmd->iov), GFP_KERNEL);
		if (!cmd->iov)
			goto err;
	}

	return 0;
err:
	sgl_free(cmd->req.sg);
	return NVME_SC_INTERNAL;
}

static void nvmet_tcp_ddgst(struct ahash_request *hash,
		struct nvmet_tcp_cmd *cmd)
{
	ahash_request_set_crypt(hash, cmd->req.sg,
		(void *)&cmd->exp_ddgst, cmd->req.transfer_len);
	crypto_ahash_digest(hash);
}

static void nvmet_setup_c2h_data_pdu(struct nvmet_tcp_cmd *cmd)
{
	struct nvme_tcp_data_pdu *pdu = cmd->data_pdu;
	struct nvmet_tcp_queue *queue = cmd->queue;
	u8 hdgst = nvmet_tcp_hdgst_len(cmd->queue);
	u8 ddgst = nvmet_tcp_ddgst_len(cmd->queue);

	cmd->offset = 0;
	cmd->state = NVMET_TCP_SEND_DATA_PDU;

	pdu->hdr.type = nvme_tcp_c2h_data;
	pdu->hdr.flags = NVME_TCP_F_DATA_LAST | (queue->nvme_sq.sqhd_disabled ?
						NVME_TCP_F_DATA_SUCCESS : 0);
	pdu->hdr.hlen = sizeof(*pdu);
	pdu->hdr.pdo = pdu->hdr.hlen + hdgst;
	pdu->hdr.plen =
		cpu_to_le32(pdu->hdr.hlen + hdgst +
				cmd->req.transfer_len + ddgst);
	pdu->command_id = cmd->req.rsp->command_id;
	pdu->data_length = cpu_to_le32(cmd->req.transfer_len);
	pdu->data_offset = cpu_to_le32(cmd->wbytes_done);

	if (queue->data_digest) {
		pdu->hdr.flags |= NVME_TCP_F_DDGST;
		nvmet_tcp_ddgst(queue->snd_hash, cmd);
	}

	if (cmd->queue->hdr_digest) {
		pdu->hdr.flags |= NVME_TCP_F_HDGST;
		nvmet_tcp_hdgst(queue->snd_hash, pdu, sizeof(*pdu));
	}
}

static void nvmet_setup_r2t_pdu(struct nvmet_tcp_cmd *cmd)
{
	struct nvme_tcp_r2t_pdu *pdu = cmd->r2t_pdu;
	struct nvmet_tcp_queue *queue = cmd->queue;
	u8 hdgst = nvmet_tcp_hdgst_len(cmd->queue);

	cmd->offset = 0;
	cmd->state = NVMET_TCP_SEND_R2T;

	pdu->hdr.type = nvme_tcp_r2t;
	pdu->hdr.flags = 0;
	pdu->hdr.hlen = sizeof(*pdu);
	pdu->hdr.pdo = 0;
	pdu->hdr.plen = cpu_to_le32(pdu->hdr.hlen + hdgst);

	pdu->command_id = cmd->req.cmd->common.command_id;
	pdu->ttag = nvmet_tcp_cmd_tag(cmd->queue, cmd);
	pdu->r2t_length = cpu_to_le32(cmd->req.transfer_len - cmd->rbytes_done);
	pdu->r2t_offset = cpu_to_le32(cmd->rbytes_done);
	if (cmd->queue->hdr_digest) {
		pdu->hdr.flags |= NVME_TCP_F_HDGST;
		nvmet_tcp_hdgst(queue->snd_hash, pdu, sizeof(*pdu));
	}
}

static void nvmet_setup_response_pdu(struct nvmet_tcp_cmd *cmd)
{
	struct nvme_tcp_rsp_pdu *pdu = cmd->rsp_pdu;
	struct nvmet_tcp_queue *queue = cmd->queue;
	u8 hdgst = nvmet_tcp_hdgst_len(cmd->queue);

	cmd->offset = 0;
	cmd->state = NVMET_TCP_SEND_RESPONSE;

	pdu->hdr.type = nvme_tcp_rsp;
	pdu->hdr.flags = 0;
	pdu->hdr.hlen = sizeof(*pdu);
	pdu->hdr.pdo = 0;
	pdu->hdr.plen = cpu_to_le32(pdu->hdr.hlen + hdgst);
	if (cmd->queue->hdr_digest) {
		pdu->hdr.flags |= NVME_TCP_F_HDGST;
		nvmet_tcp_hdgst(queue->snd_hash, pdu, sizeof(*pdu));
	}
}

static void nvmet_tcp_process_resp_list(struct nvmet_tcp_queue *queue)
{
	struct llist_node *node;

	node = llist_del_all(&queue->resp_list);
	if (!node)
		return;

	while (node) {
		struct nvmet_tcp_cmd *cmd = llist_entry(node,
					struct nvmet_tcp_cmd, lentry);

		list_add(&cmd->entry, &queue->resp_send_list);
		node = node->next;
		queue->send_list_len++;
	}
}

static struct nvmet_tcp_cmd *nvmet_tcp_fetch_cmd(struct nvmet_tcp_queue *queue)
{
	queue->snd_cmd = list_first_entry_or_null(&queue->resp_send_list,
				struct nvmet_tcp_cmd, entry);
	if (!queue->snd_cmd) {
		nvmet_tcp_process_resp_list(queue);
		queue->snd_cmd =
			list_first_entry_or_null(&queue->resp_send_list,
					struct nvmet_tcp_cmd, entry);
		if (unlikely(!queue->snd_cmd))
			return NULL;
	}

	list_del_init(&queue->snd_cmd->entry);
	queue->send_list_len--;

	if (nvmet_tcp_need_data_out(queue->snd_cmd))
		nvmet_setup_c2h_data_pdu(queue->snd_cmd);
	else if (nvmet_tcp_need_data_in(queue->snd_cmd))
		nvmet_setup_r2t_pdu(queue->snd_cmd);
	else
		nvmet_setup_response_pdu(queue->snd_cmd);

	return queue->snd_cmd;
}

static void nvmet_tcp_queue_response(struct nvmet_req *req)
{
	struct nvmet_tcp_cmd *cmd =
		container_of(req, struct nvmet_tcp_cmd, req);
	struct nvmet_tcp_queue	*queue = cmd->queue;

	llist_add(&cmd->lentry, &queue->resp_list);
	queue_work_on(cmd->queue->cpu, nvmet_tcp_wq, &cmd->queue->io_work);
}

static int nvmet_try_send_data_pdu(struct nvmet_tcp_cmd *cmd)
{
	u8 hdgst = nvmet_tcp_hdgst_len(cmd->queue);
	int left = sizeof(*cmd->data_pdu) - cmd->offset + hdgst;
	int ret;

	ret = kernel_sendpage(cmd->queue->sock, virt_to_page(cmd->data_pdu),
			offset_in_page(cmd->data_pdu) + cmd->offset,
			left, MSG_DONTWAIT | MSG_MORE);
	if (ret <= 0)
		return ret;

	cmd->offset += ret;
	left -= ret;

	if (left)
		return -EAGAIN;

	cmd->state = NVMET_TCP_SEND_DATA;
	cmd->offset  = 0;
	return 1;
}

static int nvmet_try_send_data(struct nvmet_tcp_cmd *cmd)
{
	struct nvmet_tcp_queue *queue = cmd->queue;
	int ret;

	while (cmd->cur_sg) {
		struct page *page = sg_page(cmd->cur_sg);
		u32 left = cmd->cur_sg->length - cmd->offset;

		ret = kernel_sendpage(cmd->queue->sock, page, cmd->offset,
					left, MSG_DONTWAIT | MSG_MORE);
		if (ret <= 0)
			return ret;

		cmd->offset += ret;
		cmd->wbytes_done += ret;

		/* Done with sg?*/
		if (cmd->offset == cmd->cur_sg->length) {
			cmd->cur_sg = sg_next(cmd->cur_sg);
			cmd->offset = 0;
		}
	}

	if (queue->data_digest) {
		cmd->state = NVMET_TCP_SEND_DDGST;
		cmd->offset = 0;
	} else {
		if (queue->nvme_sq.sqhd_disabled) {
			cmd->queue->snd_cmd = NULL;
			nvmet_tcp_put_cmd(cmd);
		} else {
			nvmet_setup_response_pdu(cmd);
		}
	}

	if (queue->nvme_sq.sqhd_disabled) {
		kfree(cmd->iov);
		sgl_free(cmd->req.sg);
	}

	return 1;

}

static int nvmet_try_send_response(struct nvmet_tcp_cmd *cmd,
		bool last_in_batch)
{
	u8 hdgst = nvmet_tcp_hdgst_len(cmd->queue);
	int left = sizeof(*cmd->rsp_pdu) - cmd->offset + hdgst;
	int flags = MSG_DONTWAIT;
	int ret;

	if (!last_in_batch && cmd->queue->send_list_len)
		flags |= MSG_MORE;
	else
		flags |= MSG_EOR;

	ret = kernel_sendpage(cmd->queue->sock, virt_to_page(cmd->rsp_pdu),
		offset_in_page(cmd->rsp_pdu) + cmd->offset, left, flags);
	if (ret <= 0)
		return ret;
	cmd->offset += ret;
	left -= ret;

	if (left)
		return -EAGAIN;

	kfree(cmd->iov);
	sgl_free(cmd->req.sg);
	cmd->queue->snd_cmd = NULL;
	nvmet_tcp_put_cmd(cmd);
	return 1;
}

static int nvmet_try_send_r2t(struct nvmet_tcp_cmd *cmd, bool last_in_batch)
{
	u8 hdgst = nvmet_tcp_hdgst_len(cmd->queue);
	int left = sizeof(*cmd->r2t_pdu) - cmd->offset + hdgst;
	int flags = MSG_DONTWAIT;
	int ret;

	if (!last_in_batch && cmd->queue->send_list_len)
		flags |= MSG_MORE;
	else
		flags |= MSG_EOR;

	ret = kernel_sendpage(cmd->queue->sock, virt_to_page(cmd->r2t_pdu),
		offset_in_page(cmd->r2t_pdu) + cmd->offset, left, flags);
	if (ret <= 0)
		return ret;
	cmd->offset += ret;
	left -= ret;

	if (left)
		return -EAGAIN;

	cmd->queue->snd_cmd = NULL;
	return 1;
}

static int nvmet_try_send_ddgst(struct nvmet_tcp_cmd *cmd)
{
	struct nvmet_tcp_queue *queue = cmd->queue;
	struct msghdr msg = { .msg_flags = MSG_DONTWAIT };
	struct kvec iov = {
		.iov_base = &cmd->exp_ddgst + cmd->offset,
		.iov_len = NVME_TCP_DIGEST_LENGTH - cmd->offset
	};
	int ret;

	ret = kernel_sendmsg(queue->sock, &msg, &iov, 1, iov.iov_len);
	if (unlikely(ret <= 0))
		return ret;

	cmd->offset += ret;

	if (queue->nvme_sq.sqhd_disabled) {
		cmd->queue->snd_cmd = NULL;
		nvmet_tcp_put_cmd(cmd);
	} else {
		nvmet_setup_response_pdu(cmd);
	}
	return 1;
}

static int nvmet_tcp_try_send_one(struct nvmet_tcp_queue *queue,
		bool last_in_batch)
{
	struct nvmet_tcp_cmd *cmd = queue->snd_cmd;
	int ret = 0;

	if (!cmd || queue->state == NVMET_TCP_Q_DISCONNECTING) {
		cmd = nvmet_tcp_fetch_cmd(queue);
		if (unlikely(!cmd))
			return 0;
	}

	if (cmd->state == NVMET_TCP_SEND_DATA_PDU) {
		ret = nvmet_try_send_data_pdu(cmd);
		if (ret <= 0)
			goto done_send;
	}

	if (cmd->state == NVMET_TCP_SEND_DATA) {
		ret = nvmet_try_send_data(cmd);
		if (ret <= 0)
			goto done_send;
	}

	if (cmd->state == NVMET_TCP_SEND_DDGST) {
		ret = nvmet_try_send_ddgst(cmd);
		if (ret <= 0)
			goto done_send;
	}

	if (cmd->state == NVMET_TCP_SEND_R2T) {
		ret = nvmet_try_send_r2t(cmd, last_in_batch);
		if (ret <= 0)
			goto done_send;
	}

	if (cmd->state == NVMET_TCP_SEND_RESPONSE)
		ret = nvmet_try_send_response(cmd, last_in_batch);

done_send:
	if (ret < 0) {
		if (ret == -EAGAIN)
			return 0;
		return ret;
	}

	return 1;
}

static int nvmet_tcp_try_send(struct nvmet_tcp_queue *queue,
		int budget, int *sends)
{
	int i, ret = 0;

	for (i = 0; i < budget; i++) {
		ret = nvmet_tcp_try_send_one(queue, i == budget - 1);
		if (ret <= 0)
			break;
		(*sends)++;
	}

	return ret;
}

static void nvmet_prepare_receive_pdu(struct nvmet_tcp_queue *queue)
{
	queue->offset = 0;
	queue->left = sizeof(struct nvme_tcp_hdr);
	queue->cmd = NULL;
	queue->rcv_state = NVMET_TCP_RECV_PDU;
}

static void nvmet_tcp_free_crypto(struct nvmet_tcp_queue *queue)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(queue->rcv_hash);

	ahash_request_free(queue->rcv_hash);
	ahash_request_free(queue->snd_hash);
	crypto_free_ahash(tfm);
}

static int nvmet_tcp_alloc_crypto(struct nvmet_tcp_queue *queue)
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


static int nvmet_tcp_handle_icreq(struct nvmet_tcp_queue *queue)
{
	struct nvme_tcp_icreq_pdu *icreq = &queue->pdu.icreq;
	struct nvme_tcp_icresp_pdu *icresp = &queue->pdu.icresp;
	struct msghdr msg = {};
	struct kvec iov;
	int ret;

	if (le32_to_cpu(icreq->hdr.plen) != sizeof(struct nvme_tcp_icreq_pdu)) {
		pr_err("bad nvme-tcp pdu length (%d)\n",
			le32_to_cpu(icreq->hdr.plen));
		nvmet_tcp_fatal_error(queue);
	}

	if (icreq->pfv != NVME_TCP_PFV_1_0) {
		pr_err("queue %d: bad pfv %d\n", queue->idx, icreq->pfv);
		return -EPROTO;
	}

	if (icreq->hpda != 0) {
		pr_err("queue %d: unsupported hpda %d\n", queue->idx,
			icreq->hpda);
		return -EPROTO;
	}

	if (icreq->maxr2t != 0) {
		pr_err("queue %d: unsupported maxr2t %d\n", queue->idx,
			le32_to_cpu(icreq->maxr2t) + 1);
		return -EPROTO;
	}

	queue->hdr_digest = !!(icreq->digest & NVME_TCP_HDR_DIGEST_ENABLE);
	queue->data_digest = !!(icreq->digest & NVME_TCP_DATA_DIGEST_ENABLE);
	if (queue->hdr_digest || queue->data_digest) {
		ret = nvmet_tcp_alloc_crypto(queue);
		if (ret)
			return ret;
	}

	memset(icresp, 0, sizeof(*icresp));
	icresp->hdr.type = nvme_tcp_icresp;
	icresp->hdr.hlen = sizeof(*icresp);
	icresp->hdr.pdo = 0;
	icresp->hdr.plen = cpu_to_le32(icresp->hdr.hlen);
	icresp->pfv = cpu_to_le16(NVME_TCP_PFV_1_0);
	icresp->maxdata = cpu_to_le32(0xffff); /* FIXME: support r2t */
	icresp->cpda = 0;
	if (queue->hdr_digest)
		icresp->digest |= NVME_TCP_HDR_DIGEST_ENABLE;
	if (queue->data_digest)
		icresp->digest |= NVME_TCP_DATA_DIGEST_ENABLE;

	iov.iov_base = icresp;
	iov.iov_len = sizeof(*icresp);
	ret = kernel_sendmsg(queue->sock, &msg, &iov, 1, iov.iov_len);
	if (ret < 0)
		goto free_crypto;

	queue->state = NVMET_TCP_Q_LIVE;
	nvmet_prepare_receive_pdu(queue);
	return 0;
free_crypto:
	if (queue->hdr_digest || queue->data_digest)
		nvmet_tcp_free_crypto(queue);
	return ret;
}

static void nvmet_tcp_handle_req_failure(struct nvmet_tcp_queue *queue,
		struct nvmet_tcp_cmd *cmd, struct nvmet_req *req)
{
	int ret;

	/* recover the expected data transfer length */
	req->data_len = le32_to_cpu(req->cmd->common.dptr.sgl.length);

	if (!nvme_is_write(cmd->req.cmd) ||
	    req->data_len > cmd->req.port->inline_data_size) {
		nvmet_prepare_receive_pdu(queue);
		return;
	}

	ret = nvmet_tcp_map_data(cmd);
	if (unlikely(ret)) {
		pr_err("queue %d: failed to map data\n", queue->idx);
		nvmet_tcp_fatal_error(queue);
		return;
	}

	queue->rcv_state = NVMET_TCP_RECV_DATA;
	nvmet_tcp_map_pdu_iovec(cmd);
	cmd->flags |= NVMET_TCP_F_INIT_FAILED;
}

static int nvmet_tcp_handle_h2c_data_pdu(struct nvmet_tcp_queue *queue)
{
	struct nvme_tcp_data_pdu *data = &queue->pdu.data;
	struct nvmet_tcp_cmd *cmd;

	cmd = &queue->cmds[data->ttag];

	if (le32_to_cpu(data->data_offset) != cmd->rbytes_done) {
		pr_err("ttag %u unexpected data offset %u (expected %u)\n",
			data->ttag, le32_to_cpu(data->data_offset),
			cmd->rbytes_done);
		/* FIXME: use path and transport errors */
		nvmet_req_complete(&cmd->req,
			NVME_SC_INVALID_FIELD | NVME_SC_DNR);
		return -EPROTO;
	}

	cmd->pdu_len = le32_to_cpu(data->data_length);
	cmd->pdu_recv = 0;
	nvmet_tcp_map_pdu_iovec(cmd);
	queue->cmd = cmd;
	queue->rcv_state = NVMET_TCP_RECV_DATA;

	return 0;
}

static int nvmet_tcp_done_recv_pdu(struct nvmet_tcp_queue *queue)
{
	struct nvme_tcp_hdr *hdr = &queue->pdu.cmd.hdr;
	struct nvme_command *nvme_cmd = &queue->pdu.cmd.cmd;
	struct nvmet_req *req;
	int ret;

	if (unlikely(queue->state == NVMET_TCP_Q_CONNECTING)) {
		if (hdr->type != nvme_tcp_icreq) {
			pr_err("unexpected pdu type (%d) before icreq\n",
				hdr->type);
			nvmet_tcp_fatal_error(queue);
			return -EPROTO;
		}
		return nvmet_tcp_handle_icreq(queue);
	}

	if (hdr->type == nvme_tcp_h2c_data) {
		ret = nvmet_tcp_handle_h2c_data_pdu(queue);
		if (unlikely(ret))
			return ret;
		return 0;
	}

	queue->cmd = nvmet_tcp_get_cmd(queue);
	if (unlikely(!queue->cmd)) {
		/* This should never happen */
		pr_err("queue %d: out of commands (%d) send_list_len: %d, opcode: %d",
			queue->idx, queue->nr_cmds, queue->send_list_len,
			nvme_cmd->common.opcode);
		nvmet_tcp_fatal_error(queue);
		return -ENOMEM;
	}

	req = &queue->cmd->req;
	memcpy(req->cmd, nvme_cmd, sizeof(*nvme_cmd));

	if (unlikely(!nvmet_req_init(req, &queue->nvme_cq,
			&queue->nvme_sq, &nvmet_tcp_ops))) {
		pr_err("failed cmd %p id %d opcode %d, data_len: %d\n",
			req->cmd, req->cmd->common.command_id,
			req->cmd->common.opcode,
			le32_to_cpu(req->cmd->common.dptr.sgl.length));

		nvmet_tcp_handle_req_failure(queue, queue->cmd, req);
		return -EAGAIN;
	}

	ret = nvmet_tcp_map_data(queue->cmd);
	if (unlikely(ret)) {
		pr_err("queue %d: failed to map data\n", queue->idx);
		if (nvmet_tcp_has_inline_data(queue->cmd))
			nvmet_tcp_fatal_error(queue);
		else
			nvmet_req_complete(req, ret);
		ret = -EAGAIN;
		goto out;
	}

	if (nvmet_tcp_need_data_in(queue->cmd)) {
		if (nvmet_tcp_has_inline_data(queue->cmd)) {
			queue->rcv_state = NVMET_TCP_RECV_DATA;
			nvmet_tcp_map_pdu_iovec(queue->cmd);
			return 0;
		}
		/* send back R2T */
		nvmet_tcp_queue_response(&queue->cmd->req);
		goto out;
	}

	nvmet_req_execute(&queue->cmd->req);
out:
	nvmet_prepare_receive_pdu(queue);
	return ret;
}

static const u8 nvme_tcp_pdu_sizes[] = {
	[nvme_tcp_icreq]	= sizeof(struct nvme_tcp_icreq_pdu),
	[nvme_tcp_cmd]		= sizeof(struct nvme_tcp_cmd_pdu),
	[nvme_tcp_h2c_data]	= sizeof(struct nvme_tcp_data_pdu),
};

static inline u8 nvmet_tcp_pdu_size(u8 type)
{
	size_t idx = type;

	return (idx < ARRAY_SIZE(nvme_tcp_pdu_sizes) &&
		nvme_tcp_pdu_sizes[idx]) ?
			nvme_tcp_pdu_sizes[idx] : 0;
}

static inline bool nvmet_tcp_pdu_valid(u8 type)
{
	switch (type) {
	case nvme_tcp_icreq:
	case nvme_tcp_cmd:
	case nvme_tcp_h2c_data:
		/* fallthru */
		return true;
	}

	return false;
}

static int nvmet_tcp_try_recv_pdu(struct nvmet_tcp_queue *queue)
{
	struct nvme_tcp_hdr *hdr = &queue->pdu.cmd.hdr;
	int len;
	struct kvec iov;
	struct msghdr msg = { .msg_flags = MSG_DONTWAIT };

recv:
	iov.iov_base = (void *)&queue->pdu + queue->offset;
	iov.iov_len = queue->left;
	len = kernel_recvmsg(queue->sock, &msg, &iov, 1,
			iov.iov_len, msg.msg_flags);
	if (unlikely(len < 0))
		return len;

	queue->offset += len;
	queue->left -= len;
	if (queue->left)
		return -EAGAIN;

	if (queue->offset == sizeof(struct nvme_tcp_hdr)) {
		u8 hdgst = nvmet_tcp_hdgst_len(queue);

		if (unlikely(!nvmet_tcp_pdu_valid(hdr->type))) {
			pr_err("unexpected pdu type %d\n", hdr->type);
			nvmet_tcp_fatal_error(queue);
			return -EIO;
		}

		if (unlikely(hdr->hlen != nvmet_tcp_pdu_size(hdr->type))) {
			pr_err("pdu %d bad hlen %d\n", hdr->type, hdr->hlen);
			return -EIO;
		}

		queue->left = hdr->hlen - queue->offset + hdgst;
		goto recv;
	}

	if (queue->hdr_digest &&
	    nvmet_tcp_verify_hdgst(queue, &queue->pdu, queue->offset)) {
		nvmet_tcp_fatal_error(queue); /* fatal */
		return -EPROTO;
	}

	if (queue->data_digest &&
	    nvmet_tcp_check_ddgst(queue, &queue->pdu)) {
		nvmet_tcp_fatal_error(queue); /* fatal */
		return -EPROTO;
	}

	return nvmet_tcp_done_recv_pdu(queue);
}

static void nvmet_tcp_prep_recv_ddgst(struct nvmet_tcp_cmd *cmd)
{
	struct nvmet_tcp_queue *queue = cmd->queue;

	nvmet_tcp_ddgst(queue->rcv_hash, cmd);
	queue->offset = 0;
	queue->left = NVME_TCP_DIGEST_LENGTH;
	queue->rcv_state = NVMET_TCP_RECV_DDGST;
}

static int nvmet_tcp_try_recv_data(struct nvmet_tcp_queue *queue)
{
	struct nvmet_tcp_cmd  *cmd = queue->cmd;
	int ret;

	while (msg_data_left(&cmd->recv_msg)) {
		ret = sock_recvmsg(cmd->queue->sock, &cmd->recv_msg,
			cmd->recv_msg.msg_flags);
		if (ret <= 0)
			return ret;

		cmd->pdu_recv += ret;
		cmd->rbytes_done += ret;
	}

	nvmet_tcp_unmap_pdu_iovec(cmd);

	if (!(cmd->flags & NVMET_TCP_F_INIT_FAILED) &&
	    cmd->rbytes_done == cmd->req.transfer_len) {
		if (queue->data_digest) {
			nvmet_tcp_prep_recv_ddgst(cmd);
			return 0;
		}
		nvmet_req_execute(&cmd->req);
	}

	nvmet_prepare_receive_pdu(queue);
	return 0;
}

static int nvmet_tcp_try_recv_ddgst(struct nvmet_tcp_queue *queue)
{
	struct nvmet_tcp_cmd *cmd = queue->cmd;
	int ret;
	struct msghdr msg = { .msg_flags = MSG_DONTWAIT };
	struct kvec iov = {
		.iov_base = (void *)&cmd->recv_ddgst + queue->offset,
		.iov_len = queue->left
	};

	ret = kernel_recvmsg(queue->sock, &msg, &iov, 1,
			iov.iov_len, msg.msg_flags);
	if (unlikely(ret < 0))
		return ret;

	queue->offset += ret;
	queue->left -= ret;
	if (queue->left)
		return -EAGAIN;

	if (queue->data_digest && cmd->exp_ddgst != cmd->recv_ddgst) {
		pr_err("queue %d: cmd %d pdu (%d) data digest error: recv %#x expected %#x\n",
			queue->idx, cmd->req.cmd->common.command_id,
			queue->pdu.cmd.hdr.type, le32_to_cpu(cmd->recv_ddgst),
			le32_to_cpu(cmd->exp_ddgst));
		nvmet_tcp_finish_cmd(cmd);
		nvmet_tcp_fatal_error(queue);
		ret = -EPROTO;
		goto out;
	}

	if (!(cmd->flags & NVMET_TCP_F_INIT_FAILED) &&
	    cmd->rbytes_done == cmd->req.transfer_len)
		nvmet_req_execute(&cmd->req);
	ret = 0;
out:
	nvmet_prepare_receive_pdu(queue);
	return ret;
}

static int nvmet_tcp_try_recv_one(struct nvmet_tcp_queue *queue)
{
	int result = 0;

	if (unlikely(queue->rcv_state == NVMET_TCP_RECV_ERR))
		return 0;

	if (queue->rcv_state == NVMET_TCP_RECV_PDU) {
		result = nvmet_tcp_try_recv_pdu(queue);
		if (result != 0)
			goto done_recv;
	}

	if (queue->rcv_state == NVMET_TCP_RECV_DATA) {
		result = nvmet_tcp_try_recv_data(queue);
		if (result != 0)
			goto done_recv;
	}

	if (queue->rcv_state == NVMET_TCP_RECV_DDGST) {
		result = nvmet_tcp_try_recv_ddgst(queue);
		if (result != 0)
			goto done_recv;
	}

done_recv:
	if (result < 0) {
		if (result == -EAGAIN)
			return 0;
		return result;
	}
	return 1;
}

static int nvmet_tcp_try_recv(struct nvmet_tcp_queue *queue,
		int budget, int *recvs)
{
	int i, ret = 0;

	for (i = 0; i < budget; i++) {
		ret = nvmet_tcp_try_recv_one(queue);
		if (ret <= 0)
			break;
		(*recvs)++;
	}

	return ret;
}

static void nvmet_tcp_schedule_release_queue(struct nvmet_tcp_queue *queue)
{
	spin_lock(&queue->state_lock);
	if (queue->state != NVMET_TCP_Q_DISCONNECTING) {
		queue->state = NVMET_TCP_Q_DISCONNECTING;
		schedule_work(&queue->release_work);
	}
	spin_unlock(&queue->state_lock);
}

static void nvmet_tcp_io_work(struct work_struct *w)
{
	struct nvmet_tcp_queue *queue =
		container_of(w, struct nvmet_tcp_queue, io_work);
	bool pending;
	int ret, ops = 0;

	do {
		pending = false;

		ret = nvmet_tcp_try_recv(queue, NVMET_TCP_RECV_BUDGET, &ops);
		if (ret > 0) {
			pending = true;
		} else if (ret < 0) {
			if (ret == -EPIPE || ret == -ECONNRESET)
				kernel_sock_shutdown(queue->sock, SHUT_RDWR);
			else
				nvmet_tcp_fatal_error(queue);
			return;
		}

		ret = nvmet_tcp_try_send(queue, NVMET_TCP_SEND_BUDGET, &ops);
		if (ret > 0) {
			/* transmitted message/data */
			pending = true;
		} else if (ret < 0) {
			if (ret == -EPIPE || ret == -ECONNRESET)
				kernel_sock_shutdown(queue->sock, SHUT_RDWR);
			else
				nvmet_tcp_fatal_error(queue);
			return;
		}

	} while (pending && ops < NVMET_TCP_IO_WORK_BUDGET);

	/*
	 * We exahusted our budget, requeue our selves
	 */
	if (pending)
		queue_work_on(queue->cpu, nvmet_tcp_wq, &queue->io_work);
}

static int nvmet_tcp_alloc_cmd(struct nvmet_tcp_queue *queue,
		struct nvmet_tcp_cmd *c)
{
	u8 hdgst = nvmet_tcp_hdgst_len(queue);

	c->queue = queue;
	c->req.port = queue->port->nport;

	c->cmd_pdu = page_frag_alloc(&queue->pf_cache,
			sizeof(*c->cmd_pdu) + hdgst, GFP_KERNEL | __GFP_ZERO);
	if (!c->cmd_pdu)
		return -ENOMEM;
	c->req.cmd = &c->cmd_pdu->cmd;

	c->rsp_pdu = page_frag_alloc(&queue->pf_cache,
			sizeof(*c->rsp_pdu) + hdgst, GFP_KERNEL | __GFP_ZERO);
	if (!c->rsp_pdu)
		goto out_free_cmd;
	c->req.rsp = &c->rsp_pdu->cqe;

	c->data_pdu = page_frag_alloc(&queue->pf_cache,
			sizeof(*c->data_pdu) + hdgst, GFP_KERNEL | __GFP_ZERO);
	if (!c->data_pdu)
		goto out_free_rsp;

	c->r2t_pdu = page_frag_alloc(&queue->pf_cache,
			sizeof(*c->r2t_pdu) + hdgst, GFP_KERNEL | __GFP_ZERO);
	if (!c->r2t_pdu)
		goto out_free_data;

	c->recv_msg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;

	list_add_tail(&c->entry, &queue->free_list);

	return 0;
out_free_data:
	page_frag_free(c->data_pdu);
out_free_rsp:
	page_frag_free(c->rsp_pdu);
out_free_cmd:
	page_frag_free(c->cmd_pdu);
	return -ENOMEM;
}

static void nvmet_tcp_free_cmd(struct nvmet_tcp_cmd *c)
{
	page_frag_free(c->r2t_pdu);
	page_frag_free(c->data_pdu);
	page_frag_free(c->rsp_pdu);
	page_frag_free(c->cmd_pdu);
}

static int nvmet_tcp_alloc_cmds(struct nvmet_tcp_queue *queue)
{
	struct nvmet_tcp_cmd *cmds;
	int i, ret = -EINVAL, nr_cmds = queue->nr_cmds;

	cmds = kcalloc(nr_cmds, sizeof(struct nvmet_tcp_cmd), GFP_KERNEL);
	if (!cmds)
		goto out;

	for (i = 0; i < nr_cmds; i++) {
		ret = nvmet_tcp_alloc_cmd(queue, cmds + i);
		if (ret)
			goto out_free;
	}

	queue->cmds = cmds;

	return 0;
out_free:
	while (--i >= 0)
		nvmet_tcp_free_cmd(cmds + i);
	kfree(cmds);
out:
	return ret;
}

static void nvmet_tcp_free_cmds(struct nvmet_tcp_queue *queue)
{
	struct nvmet_tcp_cmd *cmds = queue->cmds;
	int i;

	for (i = 0; i < queue->nr_cmds; i++)
		nvmet_tcp_free_cmd(cmds + i);

	nvmet_tcp_free_cmd(&queue->connect);
	kfree(cmds);
}

static void nvmet_tcp_restore_socket_callbacks(struct nvmet_tcp_queue *queue)
{
	struct socket *sock = queue->sock;

	write_lock_bh(&sock->sk->sk_callback_lock);
	sock->sk->sk_data_ready =  queue->data_ready;
	sock->sk->sk_state_change = queue->state_change;
	sock->sk->sk_write_space = queue->write_space;
	sock->sk->sk_user_data = NULL;
	write_unlock_bh(&sock->sk->sk_callback_lock);
}

static void nvmet_tcp_finish_cmd(struct nvmet_tcp_cmd *cmd)
{
	nvmet_req_uninit(&cmd->req);
	nvmet_tcp_unmap_pdu_iovec(cmd);
	sgl_free(cmd->req.sg);
}

static void nvmet_tcp_uninit_data_in_cmds(struct nvmet_tcp_queue *queue)
{
	struct nvmet_tcp_cmd *cmd = queue->cmds;
	int i;

	for (i = 0; i < queue->nr_cmds; i++, cmd++) {
		if (nvmet_tcp_need_data_in(cmd))
			nvmet_tcp_finish_cmd(cmd);
	}

	if (!queue->nr_cmds && nvmet_tcp_need_data_in(&queue->connect)) {
		/* failed in connect */
		nvmet_tcp_finish_cmd(&queue->connect);
	}
}

static void nvmet_tcp_release_queue_work(struct work_struct *w)
{
	struct nvmet_tcp_queue *queue =
		container_of(w, struct nvmet_tcp_queue, release_work);

	mutex_lock(&nvmet_tcp_queue_mutex);
	list_del_init(&queue->queue_list);
	mutex_unlock(&nvmet_tcp_queue_mutex);

	nvmet_tcp_restore_socket_callbacks(queue);
	flush_work(&queue->io_work);

	nvmet_tcp_uninit_data_in_cmds(queue);
	nvmet_sq_destroy(&queue->nvme_sq);
	cancel_work_sync(&queue->io_work);
	sock_release(queue->sock);
	nvmet_tcp_free_cmds(queue);
	if (queue->hdr_digest || queue->data_digest)
		nvmet_tcp_free_crypto(queue);
	ida_simple_remove(&nvmet_tcp_queue_ida, queue->idx);

	kfree(queue);
}

static void nvmet_tcp_data_ready(struct sock *sk)
{
	struct nvmet_tcp_queue *queue;

	read_lock_bh(&sk->sk_callback_lock);
	queue = sk->sk_user_data;
	if (likely(queue))
		queue_work_on(queue->cpu, nvmet_tcp_wq, &queue->io_work);
	read_unlock_bh(&sk->sk_callback_lock);
}

static void nvmet_tcp_write_space(struct sock *sk)
{
	struct nvmet_tcp_queue *queue;

	read_lock_bh(&sk->sk_callback_lock);
	queue = sk->sk_user_data;
	if (unlikely(!queue))
		goto out;

	if (unlikely(queue->state == NVMET_TCP_Q_CONNECTING)) {
		queue->write_space(sk);
		goto out;
	}

	if (sk_stream_is_writeable(sk)) {
		clear_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		queue_work_on(queue->cpu, nvmet_tcp_wq, &queue->io_work);
	}
out:
	read_unlock_bh(&sk->sk_callback_lock);
}

static void nvmet_tcp_state_change(struct sock *sk)
{
	struct nvmet_tcp_queue *queue;

	write_lock_bh(&sk->sk_callback_lock);
	queue = sk->sk_user_data;
	if (!queue)
		goto done;

	switch (sk->sk_state) {
	case TCP_FIN_WAIT1:
	case TCP_CLOSE_WAIT:
	case TCP_CLOSE:
		/* FALLTHRU */
		sk->sk_user_data = NULL;
		nvmet_tcp_schedule_release_queue(queue);
		break;
	default:
		pr_warn("queue %d unhandled state %d\n",
			queue->idx, sk->sk_state);
	}
done:
	write_unlock_bh(&sk->sk_callback_lock);
}

static int nvmet_tcp_set_queue_sock(struct nvmet_tcp_queue *queue)
{
	struct socket *sock = queue->sock;
	struct linger sol = { .l_onoff = 1, .l_linger = 0 };
	int ret;

	ret = kernel_getsockname(sock,
		(struct sockaddr *)&queue->sockaddr);
	if (ret < 0)
		return ret;

	ret = kernel_getpeername(sock,
		(struct sockaddr *)&queue->sockaddr_peer);
	if (ret < 0)
		return ret;

	/*
	 * Cleanup whatever is sitting in the TCP transmit queue on socket
	 * close. This is done to prevent stale data from being sent should
	 * the network connection be restored before TCP times out.
	 */
	ret = kernel_setsockopt(sock, SOL_SOCKET, SO_LINGER,
			(char *)&sol, sizeof(sol));
	if (ret)
		return ret;

	write_lock_bh(&sock->sk->sk_callback_lock);
	sock->sk->sk_user_data = queue;
	queue->data_ready = sock->sk->sk_data_ready;
	sock->sk->sk_data_ready = nvmet_tcp_data_ready;
	queue->state_change = sock->sk->sk_state_change;
	sock->sk->sk_state_change = nvmet_tcp_state_change;
	queue->write_space = sock->sk->sk_write_space;
	sock->sk->sk_write_space = nvmet_tcp_write_space;
	write_unlock_bh(&sock->sk->sk_callback_lock);

	return 0;
}

static int nvmet_tcp_alloc_queue(struct nvmet_tcp_port *port,
		struct socket *newsock)
{
	struct nvmet_tcp_queue *queue;
	int ret;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return -ENOMEM;

	INIT_WORK(&queue->release_work, nvmet_tcp_release_queue_work);
	INIT_WORK(&queue->io_work, nvmet_tcp_io_work);
	queue->sock = newsock;
	queue->port = port;
	queue->nr_cmds = 0;
	spin_lock_init(&queue->state_lock);
	queue->state = NVMET_TCP_Q_CONNECTING;
	INIT_LIST_HEAD(&queue->free_list);
	init_llist_head(&queue->resp_list);
	INIT_LIST_HEAD(&queue->resp_send_list);

	queue->idx = ida_simple_get(&nvmet_tcp_queue_ida, 0, 0, GFP_KERNEL);
	if (queue->idx < 0) {
		ret = queue->idx;
		goto out_free_queue;
	}

	ret = nvmet_tcp_alloc_cmd(queue, &queue->connect);
	if (ret)
		goto out_ida_remove;

	ret = nvmet_sq_init(&queue->nvme_sq);
	if (ret)
		goto out_free_connect;

	port->last_cpu = cpumask_next_wrap(port->last_cpu,
				cpu_online_mask, -1, false);
	queue->cpu = port->last_cpu;
	nvmet_prepare_receive_pdu(queue);

	mutex_lock(&nvmet_tcp_queue_mutex);
	list_add_tail(&queue->queue_list, &nvmet_tcp_queue_list);
	mutex_unlock(&nvmet_tcp_queue_mutex);

	ret = nvmet_tcp_set_queue_sock(queue);
	if (ret)
		goto out_destroy_sq;

	queue_work_on(queue->cpu, nvmet_tcp_wq, &queue->io_work);

	return 0;
out_destroy_sq:
	mutex_lock(&nvmet_tcp_queue_mutex);
	list_del_init(&queue->queue_list);
	mutex_unlock(&nvmet_tcp_queue_mutex);
	nvmet_sq_destroy(&queue->nvme_sq);
out_free_connect:
	nvmet_tcp_free_cmd(&queue->connect);
out_ida_remove:
	ida_simple_remove(&nvmet_tcp_queue_ida, queue->idx);
out_free_queue:
	kfree(queue);
	return ret;
}

static void nvmet_tcp_accept_work(struct work_struct *w)
{
	struct nvmet_tcp_port *port =
		container_of(w, struct nvmet_tcp_port, accept_work);
	struct socket *newsock;
	int ret;

	while (true) {
		ret = kernel_accept(port->sock, &newsock, O_NONBLOCK);
		if (ret < 0) {
			if (ret != -EAGAIN)
				pr_warn("failed to accept err=%d\n", ret);
			return;
		}
		ret = nvmet_tcp_alloc_queue(port, newsock);
		if (ret) {
			pr_err("failed to allocate queue\n");
			sock_release(newsock);
		}
	}
}

static void nvmet_tcp_listen_data_ready(struct sock *sk)
{
	struct nvmet_tcp_port *port;

	read_lock_bh(&sk->sk_callback_lock);
	port = sk->sk_user_data;
	if (!port)
		goto out;

	if (sk->sk_state == TCP_LISTEN)
		schedule_work(&port->accept_work);
out:
	read_unlock_bh(&sk->sk_callback_lock);
}

static int nvmet_tcp_add_port(struct nvmet_port *nport)
{
	struct nvmet_tcp_port *port;
	__kernel_sa_family_t af;
	int opt, ret;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	switch (nport->disc_addr.adrfam) {
	case NVMF_ADDR_FAMILY_IP4:
		af = AF_INET;
		break;
	case NVMF_ADDR_FAMILY_IP6:
		af = AF_INET6;
		break;
	default:
		pr_err("address family %d not supported\n",
				nport->disc_addr.adrfam);
		ret = -EINVAL;
		goto err_port;
	}

	ret = inet_pton_with_scope(&init_net, af, nport->disc_addr.traddr,
			nport->disc_addr.trsvcid, &port->addr);
	if (ret) {
		pr_err("malformed ip/port passed: %s:%s\n",
			nport->disc_addr.traddr, nport->disc_addr.trsvcid);
		goto err_port;
	}

	port->nport = nport;
	port->last_cpu = -1;
	INIT_WORK(&port->accept_work, nvmet_tcp_accept_work);
	if (port->nport->inline_data_size < 0)
		port->nport->inline_data_size = NVMET_TCP_DEF_INLINE_DATA_SIZE;

	ret = sock_create(port->addr.ss_family, SOCK_STREAM,
				IPPROTO_TCP, &port->sock);
	if (ret) {
		pr_err("failed to create a socket\n");
		goto err_port;
	}

	port->sock->sk->sk_user_data = port;
	port->data_ready = port->sock->sk->sk_data_ready;
	port->sock->sk->sk_data_ready = nvmet_tcp_listen_data_ready;

	opt = 1;
	ret = kernel_setsockopt(port->sock, IPPROTO_TCP,
			TCP_NODELAY, (char *)&opt, sizeof(opt));
	if (ret) {
		pr_err("failed to set TCP_NODELAY sock opt %d\n", ret);
		goto err_sock;
	}

	ret = kernel_setsockopt(port->sock, SOL_SOCKET, SO_REUSEADDR,
			(char *)&opt, sizeof(opt));
	if (ret) {
		pr_err("failed to set SO_REUSEADDR sock opt %d\n", ret);
		goto err_sock;
	}

	ret = kernel_bind(port->sock, (struct sockaddr *)&port->addr,
			sizeof(port->addr));
	if (ret) {
		pr_err("failed to bind port socket %d\n", ret);
		goto err_sock;
	}

	ret = kernel_listen(port->sock, 128);
	if (ret) {
		pr_err("failed to listen %d on port sock\n", ret);
		goto err_sock;
	}

	nport->priv = port;
	pr_info("enabling port %d (%pISpc)\n",
		le16_to_cpu(nport->disc_addr.portid), &port->addr);

	return 0;

err_sock:
	sock_release(port->sock);
err_port:
	kfree(port);
	return ret;
}

static void nvmet_tcp_remove_port(struct nvmet_port *nport)
{
	struct nvmet_tcp_port *port = nport->priv;

	write_lock_bh(&port->sock->sk->sk_callback_lock);
	port->sock->sk->sk_data_ready = port->data_ready;
	port->sock->sk->sk_user_data = NULL;
	write_unlock_bh(&port->sock->sk->sk_callback_lock);
	cancel_work_sync(&port->accept_work);

	sock_release(port->sock);
	kfree(port);
}

static void nvmet_tcp_delete_ctrl(struct nvmet_ctrl *ctrl)
{
	struct nvmet_tcp_queue *queue;

	mutex_lock(&nvmet_tcp_queue_mutex);
	list_for_each_entry(queue, &nvmet_tcp_queue_list, queue_list)
		if (queue->nvme_sq.ctrl == ctrl)
			kernel_sock_shutdown(queue->sock, SHUT_RDWR);
	mutex_unlock(&nvmet_tcp_queue_mutex);
}

static u16 nvmet_tcp_install_queue(struct nvmet_sq *sq)
{
	struct nvmet_tcp_queue *queue =
		container_of(sq, struct nvmet_tcp_queue, nvme_sq);

	if (sq->qid == 0) {
		/* Let inflight controller teardown complete */
		flush_scheduled_work();
	}

	queue->nr_cmds = sq->size * 2;
	if (nvmet_tcp_alloc_cmds(queue))
		return NVME_SC_INTERNAL;
	return 0;
}

static void nvmet_tcp_disc_port_addr(struct nvmet_req *req,
		struct nvmet_port *nport, char *traddr)
{
	struct nvmet_tcp_port *port = nport->priv;

	if (inet_addr_is_any((struct sockaddr *)&port->addr)) {
		struct nvmet_tcp_cmd *cmd =
			container_of(req, struct nvmet_tcp_cmd, req);
		struct nvmet_tcp_queue *queue = cmd->queue;

		sprintf(traddr, "%pISc", (struct sockaddr *)&queue->sockaddr);
	} else {
		memcpy(traddr, nport->disc_addr.traddr, NVMF_TRADDR_SIZE);
	}
}

static struct nvmet_fabrics_ops nvmet_tcp_ops = {
	.owner			= THIS_MODULE,
	.type			= NVMF_TRTYPE_TCP,
	.msdbd			= 1,
	.has_keyed_sgls		= 0,
	.add_port		= nvmet_tcp_add_port,
	.remove_port		= nvmet_tcp_remove_port,
	.queue_response		= nvmet_tcp_queue_response,
	.delete_ctrl		= nvmet_tcp_delete_ctrl,
	.install_queue		= nvmet_tcp_install_queue,
	.disc_traddr		= nvmet_tcp_disc_port_addr,
};

static int __init nvmet_tcp_init(void)
{
	int ret;

	nvmet_tcp_wq = alloc_workqueue("nvmet_tcp_wq", WQ_HIGHPRI, 0);
	if (!nvmet_tcp_wq)
		return -ENOMEM;

	ret = nvmet_register_transport(&nvmet_tcp_ops);
	if (ret)
		goto err;

	return 0;
err:
	destroy_workqueue(nvmet_tcp_wq);
	return ret;
}

static void __exit nvmet_tcp_exit(void)
{
	struct nvmet_tcp_queue *queue;

	nvmet_unregister_transport(&nvmet_tcp_ops);

	flush_scheduled_work();
	mutex_lock(&nvmet_tcp_queue_mutex);
	list_for_each_entry(queue, &nvmet_tcp_queue_list, queue_list)
		kernel_sock_shutdown(queue->sock, SHUT_RDWR);
	mutex_unlock(&nvmet_tcp_queue_mutex);
	flush_scheduled_work();

	destroy_workqueue(nvmet_tcp_wq);
}

module_init(nvmet_tcp_init);
module_exit(nvmet_tcp_exit);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("nvmet-transport-3"); /* 3 == NVMF_TRTYPE_TCP */
