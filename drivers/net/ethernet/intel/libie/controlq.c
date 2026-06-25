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
	ctlq->tx_msg = kvzalloc_objs(*ctlq->tx_msg, ctlq->ring_len,
				     GFP_KERNEL);
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

MODULE_DESCRIPTION("Control Plane communication API");
MODULE_IMPORT_NS("LIBETH");
MODULE_LICENSE("GPL");
