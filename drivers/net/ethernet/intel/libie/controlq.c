// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#include <linux/bitfield.h>
#include <net/libeth/rx.h>

#include <linux/net/intel/libie/controlq.h>

#define LIBIE_CTLQ_DESC_QWORD0(sz)			\
	(LIBIE_CTLQ_DESC_FLAG_BUF |			\
	 LIBIE_CTLQ_DESC_FLAG_RD |			\
	 FIELD_PREP(LIBIE_CTLQ_DESC_DATA_LEN, sz))

/**
 * libie_ctlq_free_fq - free fill queue resources, including buffers
 * @ctlq: Rx control queue whose resources need to be freed
 */
static void libie_ctlq_free_fq(struct libie_ctlq_info *ctlq)
{
	struct libeth_fq fq = {
		.fqes		= ctlq->rx_fqes,
		.pp		= ctlq->pp,
	};

	for (u32 ntc = ctlq->next_to_clean; ntc != ctlq->next_to_post; ) {
		page_pool_put_full_netmem(fq.pp, fq.fqes[ntc].netmem, false);

		if (++ntc >= ctlq->ring_len)
			ntc = 0;
	}

	libeth_rx_fq_destroy(&fq);
}

/**
 * libie_ctlq_init_fq - initialize fill queue for an Rx controlq
 * @ctlq: control queue that needs Rx buffer allocation
 *
 * Return: %0 on success, -%errno on failure
 */
static int libie_ctlq_init_fq(struct libie_ctlq_info *ctlq)
{
	struct libeth_fq fq = {
		.count		= ctlq->ring_len,
		.truesize	= LIBIE_CTLQ_MAX_BUF_LEN,
		.nid		= NUMA_NO_NODE,
		.type		= LIBETH_FQE_SHORT,
		.hsplit		= true,
		.no_napi	= true,
	};
	int err;

	err = libeth_rx_fq_create(&fq, ctlq->dev);
	if (err)
		return err;

	ctlq->pp = fq.pp;
	ctlq->rx_fqes = fq.fqes;
	ctlq->truesize = fq.truesize;

	return 0;
}

/**
 * libie_ctlq_prep_rx_desc - prepare the descriptor with a new address
 * @desc: descriptor to (re)initialize
 * @addr: physical address to put into descriptor
 * @mem_truesize: size of the accessible memory
 */
static void libie_ctlq_prep_rx_desc(struct libie_ctlq_desc *desc,
				    dma_addr_t addr, u32 mem_truesize)
{
	u64 qword;

	qword = LIBIE_CTLQ_DESC_QWORD0(mem_truesize);
	desc->qword0 = cpu_to_le64(qword);

	qword = FIELD_PREP(LIBIE_CTLQ_DESC_DATA_ADDR_HIGH,
			   upper_32_bits(addr)) |
		FIELD_PREP(LIBIE_CTLQ_DESC_DATA_ADDR_LOW,
			   lower_32_bits(addr));
	desc->qword3 = cpu_to_le64(qword);
}

/**
 * libie_ctlq_post_rx_buffs - post buffers to descriptor ring
 * @ctlq: control queue that requires Rx descriptor ring to be initialized with
 *	  new Rx buffers
 *
 * The caller must make sure that calls to libie_ctlq_post_rx_buffs()
 * and libie_ctlq_recv() for each queue are either serialized
 * or used under ctlq->lock.
 *
 * Return: %0 on success, -%ENOMEM if any buffer could not be allocated
 */
int libie_ctlq_post_rx_buffs(struct libie_ctlq_info *ctlq)
{
	u32 ntp = ctlq->next_to_post, ntc = ctlq->next_to_clean, num_to_post;
	const struct libeth_fq_fp fq = {
		.pp		= ctlq->pp,
		.fqes		= ctlq->rx_fqes,
		.truesize	= ctlq->truesize,
		.count		= ctlq->ring_len,
	};
	int ret = 0;

	num_to_post = (ntc > ntp ? 0 : ctlq->ring_len) + ntc - ntp - 1;

	while (num_to_post--) {
		dma_addr_t addr;

		ctlq->descs[ntp] = (struct libie_ctlq_desc) {};

		addr = libeth_rx_alloc(&fq, ntp);
		if (unlikely(addr == DMA_MAPPING_ERROR)) {
			ret = -ENOMEM;
			goto post_bufs;
		}

		libie_ctlq_prep_rx_desc(&ctlq->descs[ntp], addr, fq.truesize);

		if (unlikely(++ntp == ctlq->ring_len))
			ntp = 0;
	}

post_bufs:
	if (likely(ctlq->next_to_post != ntp)) {
		ctlq->next_to_post = ntp;

		dma_wmb();
		writel(ntp, ctlq->reg.tail);
	}

	return ret;
}
EXPORT_SYMBOL_NS_GPL(libie_ctlq_post_rx_buffs, "LIBIE_CP");

/**
 * libie_ctlq_free_tx_msgs - Free Tx control queue messages
 * @ctlq: Tx control queue being destroyed
 * @num_msgs: number of messages allocated so far
 */
static void libie_ctlq_free_tx_msgs(struct libie_ctlq_info *ctlq,
				    u32 num_msgs)
{
	for (u32 i = 0; i < num_msgs; i++)
		kfree(ctlq->tx_msg[i]);

	kvfree(ctlq->tx_msg);
}

/**
 * libie_ctlq_alloc_tx_msgs - Allocate Tx control queue messages
 * @ctlq: Tx control queue being created
 *
 * Return: %0 on success, -%ENOMEM on allocation error
 */
static int libie_ctlq_alloc_tx_msgs(struct libie_ctlq_info *ctlq)
{
	ctlq->tx_msg = kvzalloc_objs(*ctlq->tx_msg, ctlq->ring_len);
	if (!ctlq->tx_msg)
		return -ENOMEM;

	for (u32 i = 0; i < ctlq->ring_len; i++) {
		ctlq->tx_msg[i] = kzalloc_obj(*ctlq->tx_msg[i]);
		if (!ctlq->tx_msg[i]) {
			libie_ctlq_free_tx_msgs(ctlq, i);
			return -ENOMEM;
		}
	}

	return 0;
}

/**
 * libie_cp_free_desc_mem - free the previously allocated descriptor DMA memory
 * @dev: device information
 * @mem: DMA memory information
 */
static void libie_cp_free_desc_mem(struct device *dev,
				   struct libie_cp_dma_mem *mem)
{
	dma_free_coherent(dev, mem->size, mem->va, mem->pa);
	mem->va = NULL;
}

/**
 * libie_ctlq_dealloc_ring_res - free memory allocated for control queue
 * @ctlq: control queue that requires its ring memory to be freed
 *
 * Free the memory used by the ring, buffers and other related structures.
 */
static void libie_ctlq_dealloc_ring_res(struct libie_ctlq_info *ctlq)
{
	struct libie_cp_dma_mem *dma = &ctlq->ring_mem;

	if (ctlq->type == LIBIE_CTLQ_TYPE_TX)
		libie_ctlq_free_tx_msgs(ctlq, ctlq->ring_len);
	else
		libie_ctlq_free_fq(ctlq);

	libie_cp_free_desc_mem(ctlq->dev, dma);
}

/**
 * libie_cp_alloc_desc_mem - allocate DMA memory for descriptor ring
 * @dev: device information
 * @mem: memory for DMA information to be stored
 * @size: size of the memory to allocate
 *
 * Return: virtual address of DMA memory or NULL.
 */
static void *libie_cp_alloc_desc_mem(struct device *dev,
				     struct libie_cp_dma_mem *mem, u32 size)
{
	size = LARGEST_ALIGN(size);

	mem->va = dma_alloc_coherent(dev, size, &mem->pa, GFP_KERNEL);
	mem->size = size;
	mem->direction = DMA_BIDIRECTIONAL;

	return mem->va;
}

/**
 * libie_ctlq_alloc_queue_res - allocate memory for descriptor ring and bufs
 * @ctlq: control queue that requires its ring resources to be allocated
 *
 * Return: %0 on success, -%errno on failure
 */
static int libie_ctlq_alloc_queue_res(struct libie_ctlq_info *ctlq)
{
	size_t size = array_size(ctlq->ring_len, sizeof(*ctlq->descs));
	struct libie_cp_dma_mem *dma = &ctlq->ring_mem;
	int err = -ENOMEM;

	if (!libie_cp_alloc_desc_mem(ctlq->dev, dma, size))
		return -ENOMEM;

	ctlq->descs = dma->va;

	if (ctlq->type == LIBIE_CTLQ_TYPE_TX) {
		if (libie_ctlq_alloc_tx_msgs(ctlq))
			goto free_dma_mem;
	} else {
		err = libie_ctlq_init_fq(ctlq);
		if (err)
			goto free_dma_mem;

		err = libie_ctlq_post_rx_buffs(ctlq);
		if (err) {
			libie_ctlq_free_fq(ctlq);
			goto free_dma_mem;
		}
	}

	return 0;

free_dma_mem:
	libie_cp_free_desc_mem(ctlq->dev, dma);

	return err;
}

/**
 * libie_ctlq_init_regs - Initialize control queue registers
 * @ctlq: control queue that needs to be initialized
 *
 * Initialize registers. The caller is expected to have already initialized the
 * descriptor ring memory and buffer memory.
 */
static void libie_ctlq_init_regs(struct libie_ctlq_info *ctlq)
{
	u32 dword;

	if (ctlq->type == LIBIE_CTLQ_TYPE_RX)
		writel(ctlq->ring_len - 1, ctlq->reg.tail);
	else
		writel(0, ctlq->reg.tail);

	writel(0, ctlq->reg.head);
	writel(lower_32_bits(ctlq->ring_mem.pa), ctlq->reg.addr_low);
	writel(upper_32_bits(ctlq->ring_mem.pa), ctlq->reg.addr_high);

	dword = FIELD_PREP(LIBIE_CTLQ_MBX_ATQ_LEN, ctlq->ring_len) |
		ctlq->reg.len_ena_mask;
	writel(dword, ctlq->reg.len);
}

/**
 * libie_find_ctlq - find the controlq for the given id and type
 * @ctx: libie CP context information
 * @type: type of controlq to find
 * @id: controlq id to find
 *
 * Return: control queue info pointer on success, NULL on failure
 */
struct libie_ctlq_info *libie_find_ctlq(struct libie_ctlq_ctx *ctx,
					enum libie_ctlq_type type,
					int id)
{
	struct libie_ctlq_info *cq;

	guard(spinlock)(&ctx->ctlqs_lock);

	list_for_each_entry(cq, &ctx->ctlqs, list)
		if (cq->qid == id && cq->type == type)
			return cq;

	return NULL;
}
EXPORT_SYMBOL_NS_GPL(libie_find_ctlq, "LIBIE_CP");

/**
 * libie_ctlq_add - add one control queue
 * @ctx: libie CP context information
 * @qinfo: information required for queue creation
 *
 * Allocate and initialize a control queue and add it to the control queue list.
 * libie_ctlq_init() must be called prior to any calls to libie_ctlq_add.
 *
 * Return: added control queue info pointer on success, error pointer on failure
 */
static struct libie_ctlq_info *
libie_ctlq_add(struct libie_ctlq_ctx *ctx,
	       const struct libie_ctlq_create_info *qinfo)
{
	struct libie_ctlq_info *ctlq;
	int err;

	if (qinfo->id != LIBIE_CTLQ_MBX_ID)
		return ERR_PTR(-EOPNOTSUPP);

	if (qinfo->len > FIELD_MAX(LIBIE_CTLQ_MBX_ATQ_LEN) || !qinfo->len)
		return ERR_PTR(-EINVAL);

	ctlq = kvzalloc_obj(*ctlq);
	if (!ctlq)
		return ERR_PTR(-ENOMEM);

	ctlq->type = qinfo->type;
	ctlq->qid = qinfo->id;
	ctlq->ring_len = qinfo->len;
	ctlq->dev = &ctx->mmio_info.pdev->dev;
	ctlq->reg = qinfo->reg;

	err = libie_ctlq_alloc_queue_res(ctlq);
	if (err) {
		kvfree(ctlq);
		return ERR_PTR(err);
	}

	libie_ctlq_init_regs(ctlq);

	spin_lock_init(&ctlq->lock);

	scoped_guard(spinlock, &ctx->ctlqs_lock)
		list_add(&ctlq->list, &ctx->ctlqs);

	return ctlq;
}

/**
 * libie_ctlq_remove - deallocate and remove specified control queue
 * @ctx: libie CP context information
 * @ctlq: specific control queue that needs to be removed
 */
static void libie_ctlq_remove(struct libie_ctlq_ctx *ctx,
			      struct libie_ctlq_info *ctlq)
{
	scoped_guard(spinlock, &ctx->ctlqs_lock)
		list_del(&ctlq->list);

	libie_ctlq_dealloc_ring_res(ctlq);
	kvfree(ctlq);
}

/**
 * libie_ctlq_init - main initialization routine for all control queues
 * @ctx: libie CP context information
 * @qinfo: array of structs containing info for each queue to be initialized
 * @numq: number of queues to initialize
 *
 * This initializes queue list and adds any number and any type of control
 * queues. This is an all or nothing routine; if one fails, all previously
 * allocated queues will be destroyed.
 *
 * Please note that any control queue send/receive functions are not
 * softirq/NAPI safe, and therefore API can be used in process context only.
 *
 * Return: %0 on success, -%errno on failure
 */
int libie_ctlq_init(struct libie_ctlq_ctx *ctx,
		    const struct libie_ctlq_create_info *qinfo,
		    u32 numq)
{
	INIT_LIST_HEAD(&ctx->ctlqs);
	spin_lock_init(&ctx->ctlqs_lock);

	for (u32 i = 0; i < numq; i++) {
		struct libie_ctlq_info *ctlq;

		ctlq = libie_ctlq_add(ctx, &qinfo[i]);
		if (IS_ERR(ctlq)) {
			libie_ctlq_deinit(ctx);
			return PTR_ERR(ctlq);
		}
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(libie_ctlq_init, "LIBIE_CP");

/**
 * libie_ctlq_deinit - destroy all control queues
 * @ctx: libie CP context information
 */
void libie_ctlq_deinit(struct libie_ctlq_ctx *ctx)
{
	struct libie_ctlq_info *ctlq, *tmp;

	list_for_each_entry_safe(ctlq, tmp, &ctx->ctlqs, list)
		libie_ctlq_remove(ctx, ctlq);
}
EXPORT_SYMBOL_NS_GPL(libie_ctlq_deinit, "LIBIE_CP");

/**
 * libie_ctlq_tx_desc_from_msg - initialize a Tx descriptor from a message
 * @desc: descriptor to be initialized
 * @msg: filled control queue message
 */
static void libie_ctlq_tx_desc_from_msg(struct libie_ctlq_desc *desc,
					const struct libie_ctlq_msg *msg)
{
	const struct libie_cp_dma_mem *dma = &msg->send_mem;
	u64 qword;

	qword = FIELD_PREP(LIBIE_CTLQ_DESC_FLAGS, msg->flags) |
		FIELD_PREP(LIBIE_CTLQ_DESC_INFRA_OPCODE, msg->opcode) |
		FIELD_PREP(LIBIE_CTLQ_DESC_PFID_VFID, msg->func_id);
	desc->qword0 = cpu_to_le64(qword);

	qword = FIELD_PREP(LIBIE_CTLQ_DESC_VIRTCHNL_OPCODE,
			   msg->chnl_opcode) |
		FIELD_PREP(LIBIE_CTLQ_DESC_VIRTCHNL_MSG_RET_VAL,
			   msg->chnl_retval);
	desc->qword1 = cpu_to_le64(qword);

	qword = FIELD_PREP(LIBIE_CTLQ_DESC_MSG_PARAM0, msg->param0) |
		FIELD_PREP(LIBIE_CTLQ_DESC_SW_COOKIE,
			   msg->sw_cookie) |
		FIELD_PREP(LIBIE_CTLQ_DESC_VIRTCHNL_FLAGS,
			   msg->virt_flags);
	desc->qword2 = cpu_to_le64(qword);

	if (likely(msg->data_len)) {
		desc->qword0 |=
			cpu_to_le64(LIBIE_CTLQ_DESC_QWORD0(msg->data_len));
		qword = FIELD_PREP(LIBIE_CTLQ_DESC_DATA_ADDR_HIGH,
				   upper_32_bits(dma->pa)) |
			FIELD_PREP(LIBIE_CTLQ_DESC_DATA_ADDR_LOW,
				   lower_32_bits(dma->pa));
	} else {
		qword = msg->addr_param;
	}

	desc->qword3 = cpu_to_le64(qword);
}

/**
 * libie_ctlq_send_desc_avail - get number of free descriptors on a Tx ctlq
 * @ctlq: specific control queue which is going be used for sending messages
 *
 * The caller must hold ctlq->lock. Any dependent sending must be done
 * in the same critical section.
 *
 * Return: number of available descriptors/messages on a given control queue.
 */
u32 libie_ctlq_send_desc_avail(const struct libie_ctlq_info *ctlq)
{
	u32 ntu = ctlq->next_to_use, ntc = ctlq->next_to_clean;

	lockdep_assert_held(&ctlq->lock);

	return (ntc > ntu ? 0 : ctlq->ring_len) + ntc - ntu - 1;
}
EXPORT_SYMBOL_NS_GPL(libie_ctlq_send_desc_avail, "LIBIE_CP");

/**
 * libie_ctlq_send - send a message to Control Plane or Peer
 * @ctlq: specific control queue which is used for sending a message
 * @num_q_msg: number of messages present to send on @ctlq,
 *	       positive and no greater than the number of available descriptors
 *
 * The caller must fill in @num_q_msg Tx messages starting at ntu beforehand.
 *
 * The caller must hold ctlq->lock. The intended pattern is to first check
 * the number of descriptors available, then fill in the messages and perform
 * send within a single critical section.
 */
void libie_ctlq_send(struct libie_ctlq_info *ctlq, u32 num_q_msg)
{
	u32 ntu = ctlq->next_to_use;

	lockdep_assert_held(&ctlq->lock);

	for (int i = 0; i < num_q_msg; i++) {
		struct libie_ctlq_msg *msg = ctlq->tx_msg[ntu];
		struct libie_ctlq_desc *desc;

		desc = &ctlq->descs[ntu];
		libie_ctlq_tx_desc_from_msg(desc, msg);

		if (unlikely(++ntu == ctlq->ring_len))
			ntu = 0;
	}
	dma_wmb();
	writel(ntu, ctlq->reg.tail);
	ctlq->next_to_use = ntu;
}
EXPORT_SYMBOL_NS_GPL(libie_ctlq_send, "LIBIE_CP");

/**
 * libie_ctlq_send_clean - cleanup the send control queue message buffers
 * @params: information for handling of Tx completions
 *
 * Cleanup the send buffers for the given control queue, if force is set, then
 * clear all the outstanding send messages irrespective of their send status,
 * until a zero-length message is encountered, which is either a message that
 * is already cleared, or a VF reset message, which is always last.
 * Force should be used during deinit or reset.
 *
 * Return: number of send buffers cleaned.
 */
u32 libie_ctlq_send_clean(const struct libie_ctlq_clean_params *params)
{
	struct libie_ctlq_info *ctlq = params->ctlq;
	u32 ntc, i;

	spin_lock(&ctlq->lock);
	ntc = ctlq->next_to_clean;

	for (i = 0; i < params->num_msgs; i++) {
		struct libie_ctlq_msg *msg = ctlq->tx_msg[ntc];
		struct libie_ctlq_desc *desc;
		u64 qword;

		desc = &ctlq->descs[ntc];
		qword = le64_to_cpu(desc->qword0);

		if (!FIELD_GET(LIBIE_CTLQ_DESC_FLAG_DD, qword) &&
		    !(unlikely(params->force) && msg->data_len))
			break;

		/* This cannot be reordered and lock is taken, so no barriers */
		desc->qword0 = 0;

		params->rel_dma_mem(params->rel_ctx, &msg->send_mem);
		memset(msg, 0, sizeof(*msg));

		if (unlikely(++ntc == ctlq->ring_len))
			ntc = 0;
	}

	ctlq->next_to_clean = ntc;
	spin_unlock(&ctlq->lock);

	return i;
}
EXPORT_SYMBOL_NS_GPL(libie_ctlq_send_clean, "LIBIE_CP");

/**
 * libie_ctlq_fill_rx_msg - fill in a message from Rx descriptor and buffer
 * @msg: message to be filled in
 * @desc: received descriptor
 * @rx_buf: fill queue buffer associated with the descriptor
 */
static void libie_ctlq_fill_rx_msg(struct libie_ctlq_msg *msg,
				   const struct libie_ctlq_desc *desc,
				   struct libeth_fqe *rx_buf)
{
	u64 qword = le64_to_cpu(desc->qword0);

	msg->flags = FIELD_GET(LIBIE_CTLQ_DESC_FLAGS, qword);
	msg->opcode = FIELD_GET(LIBIE_CTLQ_DESC_INFRA_OPCODE, qword);
	msg->data_len = FIELD_GET(LIBIE_CTLQ_DESC_DATA_LEN, qword);
	msg->hw_retval = FIELD_GET(LIBIE_CTLQ_DESC_HW_RETVAL, qword);

	qword = le64_to_cpu(desc->qword1);
	msg->chnl_opcode =
		FIELD_GET(LIBIE_CTLQ_DESC_VIRTCHNL_OPCODE, qword);
	msg->chnl_retval =
		FIELD_GET(LIBIE_CTLQ_DESC_VIRTCHNL_MSG_RET_VAL, qword);

	qword = le64_to_cpu(desc->qword2);
	msg->param0 =
		FIELD_GET(LIBIE_CTLQ_DESC_MSG_PARAM0, qword);
	msg->sw_cookie =
		FIELD_GET(LIBIE_CTLQ_DESC_SW_COOKIE, qword);
	msg->virt_flags =
		FIELD_GET(LIBIE_CTLQ_DESC_VIRTCHNL_FLAGS, qword);

	if (likely(msg->data_len)) {
		if (unlikely(msg->data_len > LIBIE_CTLQ_MAX_BUF_LEN)) {
			msg->data_len = LIBIE_CTLQ_MAX_BUF_LEN;
			msg->chnl_retval = U32_MAX;
		}
		msg->recv_mem = (struct kvec) {
			.iov_base = netmem_address(rx_buf->netmem) +
				    rx_buf->offset,
			.iov_len = msg->data_len,
		};
		libeth_rx_sync_for_cpu(rx_buf, msg->data_len);
	} else {
		msg->recv_mem = (struct kvec) {};
		msg->addr_param = le64_to_cpu(desc->qword3);
		page_pool_put_full_netmem(netmem_get_pp(rx_buf->netmem),
					  rx_buf->netmem, false);
	}
}

/**
 * libie_ctlq_recv - receive control queue messages
 * @ctlq: control queue that needs to processed for receive
 * @msg: array of received control queue messages on this q;
 *	 needs to be pre-allocated by caller for as many messages as requested
 * @num_q_msg: number of messages that can be stored in msg buffer,
 *	       no greater than number of posted buffers
 *
 * Caller is expected to return buffers via libie_ctlq_release_rx_buf().
 *
 * The caller must make sure that calls to libie_ctlq_post_rx_buffs()
 * and libie_ctlq_recv() for each queue are either serialized
 * or used under ctlq->lock.
 *
 * Return: number of messages received
 */
u32 libie_ctlq_recv(struct libie_ctlq_info *ctlq, struct libie_ctlq_msg *msg,
		    u32 num_q_msg)
{
	u32 ntc, i;

	ntc = ctlq->next_to_clean;

	for (i = 0; i < num_q_msg; i++) {
		struct libie_ctlq_desc *desc = &ctlq->descs[ntc];
		struct libeth_fqe *rx_buf = &ctlq->rx_fqes[ntc];
		u64 qword;

		qword = le64_to_cpu(desc->qword0);
		if (!FIELD_GET(LIBIE_CTLQ_DESC_FLAG_DD, qword))
			break;

		dma_rmb();

		libie_ctlq_fill_rx_msg(&msg[i], desc, rx_buf);
		desc->qword0 = 0;

		if (unlikely(++ntc == ctlq->ring_len))
			ntc = 0;
	}

	ctlq->next_to_clean = ntc;

	return i;
}
EXPORT_SYMBOL_NS_GPL(libie_ctlq_recv, "LIBIE_CP");

/**
 * libie_ctlq_xn_pop_free - get a free Xn entry from the free list
 * @xnm: Xn transaction manager
 *
 * Retrieve a free Xn entry from the free list.
 *
 * Return: valid Xn entry pointer or NULL if there are no free Xn entries.
 */
static struct libie_ctlq_xn *
libie_ctlq_xn_pop_free(struct libie_ctlq_xn_manager *xnm)
{
	struct libie_ctlq_xn *xn;
	u32 free_idx;

	guard(spinlock)(&xnm->free_xns_bm_lock);

	if (unlikely(xnm->shutdown))
		return NULL;

	for_each_set_bit(free_idx, xnm->free_xns_bm,
			 LIBIE_CTLQ_MAX_XN_ENTRIES) {
		xn = &xnm->ring[free_idx];

		/* Torn read of the physical address is possible, the worst case
		 * scenario is a transient spurious skip. If the physical
		 * address is dirty in any way, reuse is already safe.
		 */
		if (xn->tx_msg &&
		    data_race(xn->tx_msg->send_mem.pa) == xn->small_dma_mem.pa)
			continue;

		clear_bit(free_idx, xnm->free_xns_bm);

		return xn;
	}

	return NULL;
}

/**
 * __libie_ctlq_xn_push_free - unsafely push an xn entry into the free list
 * @xnm: Xn transaction manager
 * @xn: xn entry to be added into the free list
 *
 * Return: whether xnm destruction can be triggered by the caller
 */
static bool __libie_ctlq_xn_push_free(struct libie_ctlq_xn_manager *xnm,
				      struct libie_ctlq_xn *xn)
{
	xn->cookie++;
	set_bit(xn->index, xnm->free_xns_bm);

	if (unlikely(xnm->shutdown) &&
	    bitmap_full(xnm->free_xns_bm, LIBIE_CTLQ_MAX_XN_ENTRIES))
		return true;

	return false;
}

/**
 * libie_ctlq_xn_push_free - push a Xn entry into the free list
 * @xnm: Xn transaction manager
 * @xn: xn entry to be added into the free list, not locked
 *
 * Safely add a used Xn entry back to the free list.
 */
static void libie_ctlq_xn_push_free(struct libie_ctlq_xn_manager *xnm,
				    struct libie_ctlq_xn *xn)
{
	bool can_destroy;

	scoped_guard(spinlock, &xnm->free_xns_bm_lock)
		can_destroy = __libie_ctlq_xn_push_free(xnm, xn);

	if (can_destroy)
		complete(&xnm->can_destroy);
}

/**
 * libie_ctlq_xn_deinit_dma - free the DMA memory allocated for send messages
 * @xnm: pointer to the transaction manager
 * @num_entries: number of Xn entries to free the DMA for
 */
static void libie_ctlq_xn_deinit_dma(struct libie_ctlq_xn_manager *xnm,
				     u32 num_entries)
{
	for (u32 i = 0; i < num_entries; i++) {
		struct libie_ctlq_xn *xn = &xnm->ring[i];

		dma_pool_free(xnm->small_buff_pool, xn->small_dma_mem.va,
			      xn->small_dma_mem.pa);
	}

	dma_pool_destroy(xnm->small_buff_pool);
}

/**
 * libie_ctlq_xn_init_dma - pre-allocate DMA memory for send messages that use
 * stack variables
 * @dev: device pointer
 * @xnm: pointer to transaction manager
 *
 * Return: %0 on success or error if memory allocation fails
 */
static int libie_ctlq_xn_init_dma(struct device *dev,
				  struct libie_ctlq_xn_manager *xnm)
{
	u32 i;

	xnm->small_buff_pool =
		dma_pool_create("libie_ctlq_xn_tx", dev, LIBIE_CP_TX_COPYBREAK,
				LIBIE_CP_TX_COPYBREAK, 0);
	if (!xnm->small_buff_pool)
		return -ENOMEM;

	for (i = 0; i < LIBIE_CTLQ_MAX_XN_ENTRIES; i++) {
		struct libie_cp_dma_mem *mem = &xnm->ring[i].small_dma_mem;

		mem->va = dma_pool_zalloc(xnm->small_buff_pool, GFP_KERNEL,
					  &mem->pa);
		if (!mem->va)
			goto dealloc_dma;

		mem->direction = DMA_BIDIRECTIONAL;
		mem->size = LIBIE_CP_TX_COPYBREAK;
	}

	return 0;

dealloc_dma:
	libie_ctlq_xn_deinit_dma(xnm, i);

	return -ENOMEM;
}

/**
 * libie_ctlq_xn_process_recv - process Xn data in receive message
 * @params: Xn receive param information to handle a receive message
 * @ctlq_msg: received control queue message
 *
 * Process a control queue receive message and send a complete event
 * notification.
 *
 * Return: true if a message has been processed, false otherwise.
 */
static bool
libie_ctlq_xn_process_recv(struct libie_ctlq_xn_recv_params *params,
			   struct libie_ctlq_msg *ctlq_msg)
{
	struct libie_ctlq_xn_manager *xnm = params->xnm;
	struct libie_ctlq_xn *xn;
	u16 msg_cookie, xn_index;
	struct kvec *response;
	int status;
	u16 data;

	data = ctlq_msg->sw_cookie;
	xn_index = FIELD_GET(LIBIE_CTLQ_XN_INDEX_M, data);
	msg_cookie = FIELD_GET(LIBIE_CTLQ_XN_COOKIE_M, data);
	status = ctlq_msg->chnl_retval ? -EFAULT : 0;

	xn = &xnm->ring[xn_index];
	spin_lock(&xn->xn_lock);
	if (ctlq_msg->chnl_opcode != xn->virtchnl_opcode ||
	    msg_cookie != xn->cookie) {
		spin_unlock(&xn->xn_lock);
		return false;
	}

	if (xn->state != LIBIE_CTLQ_XN_ASYNC &&
	    xn->state != LIBIE_CTLQ_XN_WAITING) {
		spin_unlock(&xn->xn_lock);
		return false;
	}

	response = &ctlq_msg->recv_mem;
	if (xn->state == LIBIE_CTLQ_XN_ASYNC) {
		xn->resp_cb(xn->send_ctx, response, status);
		libie_ctlq_release_rx_buf(response);
		xn->state = LIBIE_CTLQ_XN_IDLE;
		spin_unlock(&xn->xn_lock);
		libie_ctlq_xn_push_free(xnm, xn);

		return true;
	}

	xn->recv_mem = *response;
	xn->state = status ? LIBIE_CTLQ_XN_COMPLETED_FAILED :
			     LIBIE_CTLQ_XN_COMPLETED_SUCCESS;

	complete(&xn->cmd_completion_event);
	spin_unlock(&xn->xn_lock);

	return true;
}

/**
 * libie_xn_check_async_timeout - Check for asynchronous message timeouts
 * @xnm: Xn transaction manager
 *
 * Call the corresponding callback to notify the caller about the timeout.
 * Iterates free_xns_bm locklessly, potential races are caught under
 * xn->xn_lock.
 */
static void libie_xn_check_async_timeout(struct libie_ctlq_xn_manager *xnm)
{
	u32 idx;

	for_each_clear_bit(idx, xnm->free_xns_bm, LIBIE_CTLQ_MAX_XN_ENTRIES) {
		struct libie_ctlq_xn *xn = &xnm->ring[idx];
		u64 timeout_ms;

		spin_lock(&xn->xn_lock);

		timeout_ms = ktime_ms_delta(ktime_get(), xn->timestamp);
		if (xn->state != LIBIE_CTLQ_XN_ASYNC ||
		    timeout_ms < xn->timeout_ms) {
			spin_unlock(&xn->xn_lock);
			continue;
		}

		xn->resp_cb(xn->send_ctx, NULL, -ETIMEDOUT);
		xn->state = LIBIE_CTLQ_XN_IDLE;
		spin_unlock(&xn->xn_lock);
		libie_ctlq_xn_push_free(xnm, xn);
	}
}

/**
 * libie_ctlq_xn_recv - process control queue receive message
 * @params: Xn receive param information to handle a receive message
 *
 * Process a receive message and update the receive queue buffer.
 * Also terminates async transactions for which it failed to receive a response
 * within a given timeframe.
 * Function is intended to be called periodically from a single task.
 *
 * Return: remaining budget.
 */
u32 libie_ctlq_xn_recv(struct libie_ctlq_xn_recv_params *params)
{
	struct libie_ctlq_msg ctlq_msg;
	u32 budget = params->budget;

	while (budget && libie_ctlq_recv(params->ctlq, &ctlq_msg, 1)) {
		budget--;
		if (!libie_ctlq_xn_process_recv(params, &ctlq_msg))
			params->ctlq_msg_handler(params->xnm->ctx, &ctlq_msg);
	}

	libie_ctlq_post_rx_buffs(params->ctlq);
	libie_xn_check_async_timeout(params->xnm);

	return budget;
}
EXPORT_SYMBOL_NS_GPL(libie_ctlq_xn_recv, "LIBIE_CP");

/**
 * libie_cp_map_dma_mem - map a given virtual address for DMA
 * @dev: device information
 * @va: virtual address to be mapped
 * @size: size of the memory
 * @direction: DMA direction either from/to device
 * @dma_mem: memory for DMA information to be stored
 *
 * Return: true on success, false on DMA map failure.
 */
static bool libie_cp_map_dma_mem(struct device *dev, void *va, size_t size,
				 int direction,
				 struct libie_cp_dma_mem *dma_mem)
{
	dma_mem->pa = dma_map_single(dev, va, size, direction);

	return dma_mapping_error(dev, dma_mem->pa) ? false : true;
}

/**
 * libie_cp_unmap_dma_mem - unmap previously mapped DMA address
 * @dev: device information
 * @dma_mem: DMA memory information
 */
static void libie_cp_unmap_dma_mem(struct device *dev,
				   const struct libie_cp_dma_mem *dma_mem)
{
	dma_unmap_single(dev, dma_mem->pa, dma_mem->size,
			 dma_mem->direction);
}

/**
 * libie_ctlq_xn_process_send - process and send a control queue message
 * @params: Xn send param information for sending a control queue message
 * @xn: Assigned Xn entry for tracking the control queue message
 *
 * Return: %0 on success, -%errno on failure.
 */
static
int libie_ctlq_xn_process_send(struct libie_ctlq_xn_send_params *params,
			       struct libie_ctlq_xn *xn)
{
	size_t buf_len = params->send_buf.iov_len;
	struct device *dev = params->ctlq->dev;
	void *buf = params->send_buf.iov_base;
	struct libie_cp_dma_mem *dma_mem;
	u16 cookie;

	if (!buf || !buf_len)
		return -EOPNOTSUPP;

	if (libie_cp_can_send_onstack(buf_len)) {
		dma_mem = &xn->small_dma_mem;
		memcpy(dma_mem->va, buf, buf_len);
	} else {
		dma_mem = &xn->send_dma_mem;
		dma_mem->va = buf;
		dma_mem->size = buf_len;
		dma_mem->direction = DMA_TO_DEVICE;

		if (!libie_cp_map_dma_mem(dev, buf, buf_len, DMA_TO_DEVICE,
					  dma_mem))
			return -ENOMEM;
	}

	cookie = FIELD_PREP(LIBIE_CTLQ_XN_COOKIE_M, xn->cookie) |
		 FIELD_PREP(LIBIE_CTLQ_XN_INDEX_M, xn->index);

	scoped_guard(spinlock, &params->ctlq->lock) {
		struct libie_ctlq_info *ctlq = params->ctlq;
		struct libie_ctlq_msg *ctlq_msg;

		if (!libie_ctlq_send_desc_avail(ctlq)) {
			if (!libie_cp_can_send_onstack(buf_len))
				libie_cp_unmap_dma_mem(dev, dma_mem);

			return -EBUSY;
		}

		ctlq_msg = ctlq->tx_msg[ctlq->next_to_use];
		xn->tx_msg = dma_mem == &xn->small_dma_mem ? ctlq_msg : NULL;
		if (params->ctlq_msg)
			*ctlq_msg = *params->ctlq_msg;
		else
			/* Unused ctlq messages are already zeroed */
			ctlq_msg->opcode = LIBIE_CTLQ_SEND_MSG_TO_CP;

		ctlq_msg->sw_cookie = cookie;
		ctlq_msg->send_mem = *dma_mem;
		ctlq_msg->data_len = buf_len;
		ctlq_msg->chnl_opcode = params->chnl_opcode;
		libie_ctlq_send(params->ctlq, 1);
	}

	return 0;
}

/**
 * libie_ctlq_xn_send - send a control queue message, initiating a transaction
 * @params: Xn send param information for sending a control queue message
 *
 * Send a control queue (mailbox or config) message.
 * Based on the params value, the call can be completed synchronously or
 * asynchronously.
 *
 * Return: %0 on success, -%errno on failure.
 */
int libie_ctlq_xn_send(struct libie_ctlq_xn_send_params *params)
{
	bool free_send = !libie_cp_can_send_onstack(params->send_buf.iov_len);
	struct libie_ctlq_xn *xn;
	int ret;

	if (params->send_buf.iov_len > LIBIE_CTLQ_MAX_BUF_LEN) {
		ret = -EINVAL;
		goto free_buf;
	}

	xn = libie_ctlq_xn_pop_free(params->xnm);
	/* no free transactions available */
	if (unlikely(!xn)) {
		ret = -EAGAIN;
		goto free_buf;
	}

	spin_lock(&xn->xn_lock);
	if (xn->state == LIBIE_CTLQ_XN_SHUTDOWN) {
		ret = -ENXIO;
		goto unlock_xn;
	}

	xn->state = params->resp_cb ? LIBIE_CTLQ_XN_ASYNC :
				      LIBIE_CTLQ_XN_WAITING;
	xn->virtchnl_opcode = params->chnl_opcode;

	if (params->resp_cb) {
		xn->send_ctx = params->send_ctx;
		xn->resp_cb = params->resp_cb;
		xn->timeout_ms = params->timeout_ms;
		xn->timestamp = ktime_get();
	}

	ret = libie_ctlq_xn_process_send(params, xn);
	if (ret)
		goto release_xn;
	else
		free_send = false;

	spin_unlock(&xn->xn_lock);

	if (params->resp_cb)
		return 0;

	wait_for_completion_timeout(&xn->cmd_completion_event,
				    msecs_to_jiffies(params->timeout_ms));

	spin_lock(&xn->xn_lock);
	switch (xn->state) {
	case LIBIE_CTLQ_XN_WAITING:
		ret = -ETIMEDOUT;
		break;
	case LIBIE_CTLQ_XN_COMPLETED_SUCCESS:
		params->recv_mem = xn->recv_mem;
		break;
	default:
		ret = -EBADMSG;
		break;
	}

	/* Free the receive buffer in case of failure. On timeout, receive
	 * buffer is not allocated.
	 */
	if (ret && ret != -ETIMEDOUT)
		libie_ctlq_release_rx_buf(&xn->recv_mem);

release_xn:
	xn->state = LIBIE_CTLQ_XN_IDLE;
	reinit_completion(&xn->cmd_completion_event);
unlock_xn:
	spin_unlock(&xn->xn_lock);
	libie_ctlq_xn_push_free(params->xnm, xn);
free_buf:
	if (free_send)
		params->rel_tx_buf(params->send_buf.iov_base);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(libie_ctlq_xn_send, "LIBIE_CP");

/**
 * struct libie_ctlq_xn_rel_tx_ctx - context needed to release xn Tx message
 * @dev: device for which DMA was mapped
 * @rel_tx_buf: freeing function for non-small buffers
 */
struct libie_ctlq_xn_rel_tx_ctx {
	struct device *dev;
	void (*rel_tx_buf)(const void *buf_va);
};

/**
 * libie_ctlq_xn_rel_tx_buf - release xn-controlled Tx message buffer
 * @ctx: context, namely DMA device and freeing function
 * @dma_mem: DMA memory to reclaim/unmap
 */
static void libie_ctlq_xn_rel_tx_buf(const void *ctx,
				     struct libie_cp_dma_mem *dma_mem)
{
	const struct libie_ctlq_xn_rel_tx_ctx *rel_ctx = ctx;

	if (!libie_cp_can_send_onstack(dma_mem->size)) {
		libie_cp_unmap_dma_mem(rel_ctx->dev, dma_mem);
		rel_ctx->rel_tx_buf(dma_mem->va);
	}
}

/**
 * libie_ctlq_xn_send_clean - clean xn-controlled Tx messages
 * @ctlq: control queue to clean
 * @rel_tx_buf: driver callback to free the buffer
 * @force: clean regardless of DD
 *
 * Return: number of completed/released messages.
 */
u32 libie_ctlq_xn_send_clean(struct libie_ctlq_info *ctlq,
			     void (*rel_tx_buf)(const void *buf_va),
			     bool force)
{
	struct libie_ctlq_xn_rel_tx_ctx rel_ctx = {
		.dev = ctlq->dev,
		.rel_tx_buf = rel_tx_buf,
	};
	struct libie_ctlq_clean_params params = {
		.ctlq = ctlq,
		.force = force,
		.num_msgs = ctlq->ring_len,
		.rel_ctx = &rel_ctx,
		.rel_dma_mem = libie_ctlq_xn_rel_tx_buf,
	};

	return libie_ctlq_send_clean(&params);
}
EXPORT_SYMBOL_NS_GPL(libie_ctlq_xn_send_clean, "LIBIE_CP");

/**
 * libie_ctlq_xn_shutdown - terminate control queue transactions
 * @xnm: pointer to the transaction manager
 *
 * Synchronously terminate existing transactions and stop accepting new ones.
 * Async transactions are discarded without invoking resp_cb.
 */
void libie_ctlq_xn_shutdown(struct libie_ctlq_xn_manager *xnm)
{
	bool must_wait = false;
	u32 i;

	/* Should be no new clear bits after this */
	spin_lock(&xnm->free_xns_bm_lock);
	xnm->shutdown = true;

	for_each_clear_bit(i, xnm->free_xns_bm, LIBIE_CTLQ_MAX_XN_ENTRIES) {
		struct libie_ctlq_xn *xn = &xnm->ring[i];

		spin_lock(&xn->xn_lock);

		switch (xn->state) {
		/* if an idle xn is not free, it is about to be either
		 * freed or initialized, prevent the latter and wait
		 */
		case LIBIE_CTLQ_XN_IDLE:
			xn->state = LIBIE_CTLQ_XN_SHUTDOWN;
			fallthrough;
		/* waiting thread possibly needs a push to return the xn,
		 * transaction will be reported as timed out
		 */
		case LIBIE_CTLQ_XN_WAITING:
			complete(&xn->cmd_completion_event);
			fallthrough;
		/* these states will return the xn soon */
		case LIBIE_CTLQ_XN_COMPLETED_SUCCESS:
		case LIBIE_CTLQ_XN_COMPLETED_FAILED:
		case LIBIE_CTLQ_XN_SHUTDOWN:
			must_wait = true;
			break;
		/* no thread should reference async xns at this point */
		case LIBIE_CTLQ_XN_ASYNC:
			xn->state = LIBIE_CTLQ_XN_IDLE;
			__libie_ctlq_xn_push_free(xnm, xn);
			break;
		}

		spin_unlock(&xn->xn_lock);
	}

	spin_unlock(&xnm->free_xns_bm_lock);

	if (must_wait)
		wait_for_completion(&xnm->can_destroy);
}
EXPORT_SYMBOL_NS_GPL(libie_ctlq_xn_shutdown, "LIBIE_CP");

/**
 * libie_ctlq_xn_deinit - deallocate and free the transaction manager resources
 * @xnm: pointer to the transaction manager
 * @ctx: libie CP context information
 *
 * Rx processing must be stopped beforehand via cancelling tasks.
 * Tx processing must be stopped beforehand via libie_ctlq_xn_shutdown(),
 * all buffers must be force-cleaned from the send queue.
 */
void libie_ctlq_xn_deinit(struct libie_ctlq_xn_manager *xnm,
			  struct libie_ctlq_ctx *ctx)
{
	libie_ctlq_xn_deinit_dma(xnm, LIBIE_CTLQ_MAX_XN_ENTRIES);
	kvfree(xnm);
	libie_ctlq_deinit(ctx);
}
EXPORT_SYMBOL_NS_GPL(libie_ctlq_xn_deinit, "LIBIE_CP");

/**
 * libie_ctlq_xn_init - initialize the Xn transaction manager
 * @params: Xn init param information for allocating Xn manager resources
 *
 * Return: %0 on success, -%errno on failure.
 */
int libie_ctlq_xn_init(struct libie_ctlq_xn_init_params *params)
{
	struct libie_ctlq_xn_manager *xnm;
	int ret;

	ret = libie_ctlq_init(params->ctx, params->cctlq_info, params->num_qs);
	if (ret)
		return ret;

	xnm = kvzalloc_obj(*xnm);
	if (!xnm)
		goto ctlq_deinit;

	ret = libie_ctlq_xn_init_dma(&params->ctx->mmio_info.pdev->dev, xnm);
	if (ret)
		goto free_xnm;

	spin_lock_init(&xnm->free_xns_bm_lock);
	init_completion(&xnm->can_destroy);
	bitmap_fill(xnm->free_xns_bm, LIBIE_CTLQ_MAX_XN_ENTRIES);

	for (u32 i = 0; i < LIBIE_CTLQ_MAX_XN_ENTRIES; i++) {
		struct libie_ctlq_xn *xn = &xnm->ring[i];

		xn->index = i;
		init_completion(&xn->cmd_completion_event);
		spin_lock_init(&xn->xn_lock);
	}
	xnm->ctx = params->ctx;
	params->xnm = xnm;

	return 0;

free_xnm:
	kvfree(xnm);
ctlq_deinit:
	libie_ctlq_deinit(params->ctx);

	return -ENOMEM;
}
EXPORT_SYMBOL_NS_GPL(libie_ctlq_xn_init, "LIBIE_CP");

MODULE_DESCRIPTION("Control Plane communication API");
MODULE_IMPORT_NS("LIBETH");
MODULE_LICENSE("GPL");
