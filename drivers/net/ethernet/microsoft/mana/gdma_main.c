// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#include <linux/module.h>
#include <linux/pci.h>

#include "mana.h"

static u32 mana_gd_r32(struct gdma_context *g, u64 offset)
{
	return readl(g->bar0_va + offset);
}

static u64 mana_gd_r64(struct gdma_context *g, u64 offset)
{
	return readq(g->bar0_va + offset);
}

static void mana_gd_init_registers(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);

	gc->db_page_size = mana_gd_r32(gc, GDMA_REG_DB_PAGE_SIZE) & 0xFFFF;

	gc->db_page_base = gc->bar0_va +
				mana_gd_r64(gc, GDMA_REG_DB_PAGE_OFFSET);

	gc->shm_base = gc->bar0_va + mana_gd_r64(gc, GDMA_REG_SHM_OFFSET);
}

static int mana_gd_query_max_resources(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	struct gdma_query_max_resources_resp resp = {};
	struct gdma_general_req req = {};
	int err;

	mana_gd_init_req_hdr(&req.hdr, GDMA_QUERY_MAX_RESOURCES,
			     sizeof(req), sizeof(resp));

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		dev_err(gc->dev, "Failed to query resource info: %d, 0x%x\n",
			err, resp.hdr.status);
		return err ? err : -EPROTO;
	}

	if (gc->num_msix_usable > resp.max_msix)
		gc->num_msix_usable = resp.max_msix;

	if (gc->num_msix_usable <= 1)
		return -ENOSPC;

	gc->max_num_queues = num_online_cpus();
	if (gc->max_num_queues > MANA_MAX_NUM_QUEUES)
		gc->max_num_queues = MANA_MAX_NUM_QUEUES;

	if (gc->max_num_queues > resp.max_eq)
		gc->max_num_queues = resp.max_eq;

	if (gc->max_num_queues > resp.max_cq)
		gc->max_num_queues = resp.max_cq;

	if (gc->max_num_queues > resp.max_sq)
		gc->max_num_queues = resp.max_sq;

	if (gc->max_num_queues > resp.max_rq)
		gc->max_num_queues = resp.max_rq;

	/* The Hardware Channel (HWC) used 1 MSI-X */
	if (gc->max_num_queues > gc->num_msix_usable - 1)
		gc->max_num_queues = gc->num_msix_usable - 1;

	return 0;
}

static int mana_gd_detect_devices(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	struct gdma_list_devices_resp resp = {};
	struct gdma_general_req req = {};
	struct gdma_dev_id dev;
	u32 i, max_num_devs;
	u16 dev_type;
	int err;

	mana_gd_init_req_hdr(&req.hdr, GDMA_LIST_DEVICES, sizeof(req),
			     sizeof(resp));

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		dev_err(gc->dev, "Failed to detect devices: %d, 0x%x\n", err,
			resp.hdr.status);
		return err ? err : -EPROTO;
	}

	max_num_devs = min_t(u32, MAX_NUM_GDMA_DEVICES, resp.num_of_devs);

	for (i = 0; i < max_num_devs; i++) {
		dev = resp.devs[i];
		dev_type = dev.type;

		/* HWC is already detected in mana_hwc_create_channel(). */
		if (dev_type == GDMA_DEVICE_HWC)
			continue;

		if (dev_type == GDMA_DEVICE_MANA) {
			gc->mana.gdma_context = gc;
			gc->mana.dev_id = dev;
		}
	}

	return gc->mana.dev_id.type == 0 ? -ENODEV : 0;
}

int mana_gd_send_request(struct gdma_context *gc, u32 req_len, const void *req,
			 u32 resp_len, void *resp)
{
	struct hw_channel_context *hwc = gc->hwc.driver_data;

	return mana_hwc_send_request(hwc, req_len, req, resp_len, resp);
}

int mana_gd_alloc_memory(struct gdma_context *gc, unsigned int length,
			 struct gdma_mem_info *gmi)
{
	dma_addr_t dma_handle;
	void *buf;

	if (length < PAGE_SIZE || !is_power_of_2(length))
		return -EINVAL;

	gmi->dev = gc->dev;
	buf = dma_alloc_coherent(gmi->dev, length, &dma_handle, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	gmi->dma_handle = dma_handle;
	gmi->virt_addr = buf;
	gmi->length = length;

	return 0;
}

void mana_gd_free_memory(struct gdma_mem_info *gmi)
{
	dma_free_coherent(gmi->dev, gmi->length, gmi->virt_addr,
			  gmi->dma_handle);
}

static int mana_gd_create_hw_eq(struct gdma_context *gc,
				struct gdma_queue *queue)
{
	struct gdma_create_queue_resp resp = {};
	struct gdma_create_queue_req req = {};
	int err;

	if (queue->type != GDMA_EQ)
		return -EINVAL;

	mana_gd_init_req_hdr(&req.hdr, GDMA_CREATE_QUEUE,
			     sizeof(req), sizeof(resp));

	req.hdr.dev_id = queue->gdma_dev->dev_id;
	req.type = queue->type;
	req.pdid = queue->gdma_dev->pdid;
	req.doolbell_id = queue->gdma_dev->doorbell;
	req.gdma_region = queue->mem_info.gdma_region;
	req.queue_size = queue->queue_size;
	req.log2_throttle_limit = queue->eq.log2_throttle_limit;
	req.eq_pci_msix_index = queue->eq.msix_index;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		dev_err(gc->dev, "Failed to create queue: %d, 0x%x\n", err,
			resp.hdr.status);
		return err ? err : -EPROTO;
	}

	queue->id = resp.queue_index;
	queue->eq.disable_needed = true;
	queue->mem_info.gdma_region = GDMA_INVALID_DMA_REGION;
	return 0;
}

static int mana_gd_disable_queue(struct gdma_queue *queue)
{
	struct gdma_context *gc = queue->gdma_dev->gdma_context;
	struct gdma_disable_queue_req req = {};
	struct gdma_general_resp resp = {};
	int err;

	WARN_ON(queue->type != GDMA_EQ);

	mana_gd_init_req_hdr(&req.hdr, GDMA_DISABLE_QUEUE,
			     sizeof(req), sizeof(resp));

	req.hdr.dev_id = queue->gdma_dev->dev_id;
	req.type = queue->type;
	req.queue_index =  queue->id;
	req.alloc_res_id_on_creation = 1;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		dev_err(gc->dev, "Failed to disable queue: %d, 0x%x\n", err,
			resp.hdr.status);
		return err ? err : -EPROTO;
	}

	return 0;
}

#define DOORBELL_OFFSET_SQ	0x0
#define DOORBELL_OFFSET_RQ	0x400
#define DOORBELL_OFFSET_CQ	0x800
#define DOORBELL_OFFSET_EQ	0xFF8

static void mana_gd_ring_doorbell(struct gdma_context *gc, u32 db_index,
				  enum gdma_queue_type q_type, u32 qid,
				  u32 tail_ptr, u8 num_req)
{
	void __iomem *addr = gc->db_page_base + gc->db_page_size * db_index;
	union gdma_doorbell_entry e = {};

	switch (q_type) {
	case GDMA_EQ:
		e.eq.id = qid;
		e.eq.tail_ptr = tail_ptr;
		e.eq.arm = num_req;

		addr += DOORBELL_OFFSET_EQ;
		break;

	case GDMA_CQ:
		e.cq.id = qid;
		e.cq.tail_ptr = tail_ptr;
		e.cq.arm = num_req;

		addr += DOORBELL_OFFSET_CQ;
		break;

	case GDMA_RQ:
		e.rq.id = qid;
		e.rq.tail_ptr = tail_ptr;
		e.rq.wqe_cnt = num_req;

		addr += DOORBELL_OFFSET_RQ;
		break;

	case GDMA_SQ:
		e.sq.id = qid;
		e.sq.tail_ptr = tail_ptr;

		addr += DOORBELL_OFFSET_SQ;
		break;

	default:
		WARN_ON(1);
		return;
	}

	/* Ensure all writes are done before ring doorbell */
	wmb();

	writeq(e.as_uint64, addr);
}

void mana_gd_wq_ring_doorbell(struct gdma_context *gc, struct gdma_queue *queue)
{
	mana_gd_ring_doorbell(gc, queue->gdma_dev->doorbell, queue->type,
			      queue->id, queue->head * GDMA_WQE_BU_SIZE, 1);
}

void mana_gd_ring_cq(struct gdma_queue *cq, u8 arm_bit)
{
	struct gdma_context *gc = cq->gdma_dev->gdma_context;

	u32 num_cqe = cq->queue_size / GDMA_CQE_SIZE;

	u32 head = cq->head % (num_cqe << GDMA_CQE_OWNER_BITS);

	mana_gd_ring_doorbell(gc, cq->gdma_dev->doorbell, cq->type, cq->id,
			      head, arm_bit);
}

static void mana_gd_process_eqe(struct gdma_queue *eq)
{
	u32 head = eq->head % (eq->queue_size / GDMA_EQE_SIZE);
	struct gdma_context *gc = eq->gdma_dev->gdma_context;
	struct gdma_eqe *eq_eqe_ptr = eq->queue_mem_ptr;
	union gdma_eqe_info eqe_info;
	enum gdma_eqe_type type;
	struct gdma_event event;
	struct gdma_queue *cq;
	struct gdma_eqe *eqe;
	u32 cq_id;

	eqe = &eq_eqe_ptr[head];
	eqe_info.as_uint32 = eqe->eqe_info;
	type = eqe_info.type;

	switch (type) {
	case GDMA_EQE_COMPLETION:
		cq_id = eqe->details[0] & 0xFFFFFF;
		if (WARN_ON_ONCE(cq_id >= gc->max_num_cqs))
			break;

		cq = gc->cq_table[cq_id];
		if (WARN_ON_ONCE(!cq || cq->type != GDMA_CQ || cq->id != cq_id))
			break;

		if (cq->cq.callback)
			cq->cq.callback(cq->cq.context, cq);

		break;

	case GDMA_EQE_TEST_EVENT:
		gc->test_event_eq_id = eq->id;
		complete(&gc->eq_test_event);
		break;

	case GDMA_EQE_HWC_INIT_EQ_ID_DB:
	case GDMA_EQE_HWC_INIT_DATA:
	case GDMA_EQE_HWC_INIT_DONE:
		if (!eq->eq.callback)
			break;

		event.type = type;
		memcpy(&event.details, &eqe->details, GDMA_EVENT_DATA_SIZE);
		eq->eq.callback(eq->eq.context, eq, &event);
		break;

	default:
		break;
	}
}

static void mana_gd_process_eq_events(void *arg)
{
	u32 owner_bits, new_bits, old_bits;
	union gdma_eqe_info eqe_info;
	struct gdma_eqe *eq_eqe_ptr;
	struct gdma_queue *eq = arg;
	struct gdma_context *gc;
	struct gdma_eqe *eqe;
	u32 head, num_eqe;
	int i;

	gc = eq->gdma_dev->gdma_context;

	num_eqe = eq->queue_size / GDMA_EQE_SIZE;
	eq_eqe_ptr = eq->queue_mem_ptr;

	/* Process up to 5 EQEs at a time, and update the HW head. */
	for (i = 0; i < 5; i++) {
		eqe = &eq_eqe_ptr[eq->head % num_eqe];
		eqe_info.as_uint32 = eqe->eqe_info;
		owner_bits = eqe_info.owner_bits;

		old_bits = (eq->head / num_eqe - 1) & GDMA_EQE_OWNER_MASK;
		/* No more entries */
		if (owner_bits == old_bits)
			break;

		new_bits = (eq->head / num_eqe) & GDMA_EQE_OWNER_MASK;
		if (owner_bits != new_bits) {
			dev_err(gc->dev, "EQ %d: overflow detected\n", eq->id);
			break;
		}

		mana_gd_process_eqe(eq);

		eq->head++;
	}

	head = eq->head % (num_eqe << GDMA_EQE_OWNER_BITS);

	mana_gd_ring_doorbell(gc, eq->gdma_dev->doorbell, eq->type, eq->id,
			      head, SET_ARM_BIT);
}

static int mana_gd_register_irq(struct gdma_queue *queue,
				const struct gdma_queue_spec *spec)
{
	struct gdma_dev *gd = queue->gdma_dev;
	struct gdma_irq_context *gic;
	struct gdma_context *gc;
	struct gdma_resource *r;
	unsigned int msi_index;
	unsigned long flags;
	struct device *dev;
	int err = 0;

	gc = gd->gdma_context;
	r = &gc->msix_resource;
	dev = gc->dev;

	spin_lock_irqsave(&r->lock, flags);

	msi_index = find_first_zero_bit(r->map, r->size);
	if (msi_index >= r->size || msi_index >= gc->num_msix_usable) {
		err = -ENOSPC;
	} else {
		bitmap_set(r->map, msi_index, 1);
		queue->eq.msix_index = msi_index;
	}

	spin_unlock_irqrestore(&r->lock, flags);

	if (err) {
		dev_err(dev, "Register IRQ err:%d, msi:%u rsize:%u, nMSI:%u",
			err, msi_index, r->size, gc->num_msix_usable);

		return err;
	}

	gic = &gc->irq_contexts[msi_index];

	WARN_ON(gic->handler || gic->arg);

	gic->arg = queue;

	gic->handler = mana_gd_process_eq_events;

	return 0;
}

static void mana_gd_deregiser_irq(struct gdma_queue *queue)
{
	struct gdma_dev *gd = queue->gdma_dev;
	struct gdma_irq_context *gic;
	struct gdma_context *gc;
	struct gdma_resource *r;
	unsigned int msix_index;
	unsigned long flags;

	gc = gd->gdma_context;
	r = &gc->msix_resource;

	/* At most num_online_cpus() + 1 interrupts are used. */
	msix_index = queue->eq.msix_index;
	if (WARN_ON(msix_index >= gc->num_msix_usable))
		return;

	gic = &gc->irq_contexts[msix_index];
	gic->handler = NULL;
	gic->arg = NULL;

	spin_lock_irqsave(&r->lock, flags);
	bitmap_clear(r->map, msix_index, 1);
	spin_unlock_irqrestore(&r->lock, flags);

	queue->eq.msix_index = INVALID_PCI_MSIX_INDEX;
}

int mana_gd_test_eq(struct gdma_context *gc, struct gdma_queue *eq)
{
	struct gdma_generate_test_event_req req = {};
	struct gdma_general_resp resp = {};
	struct device *dev = gc->dev;
	int err;

	mutex_lock(&gc->eq_test_event_mutex);

	init_completion(&gc->eq_test_event);
	gc->test_event_eq_id = INVALID_QUEUE_ID;

	mana_gd_init_req_hdr(&req.hdr, GDMA_GENERATE_TEST_EQE,
			     sizeof(req), sizeof(resp));

	req.hdr.dev_id = eq->gdma_dev->dev_id;
	req.queue_index = eq->id;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err) {
		dev_err(dev, "test_eq failed: %d\n", err);
		goto out;
	}

	err = -EPROTO;

	if (resp.hdr.status) {
		dev_err(dev, "test_eq failed: 0x%x\n", resp.hdr.status);
		goto out;
	}

	if (!wait_for_completion_timeout(&gc->eq_test_event, 30 * HZ)) {
		dev_err(dev, "test_eq timed out on queue %d\n", eq->id);
		goto out;
	}

	if (eq->id != gc->test_event_eq_id) {
		dev_err(dev, "test_eq got an event on wrong queue %d (%d)\n",
			gc->test_event_eq_id, eq->id);
		goto out;
	}

	err = 0;
out:
	mutex_unlock(&gc->eq_test_event_mutex);
	return err;
}

static void mana_gd_destroy_eq(struct gdma_context *gc, bool flush_evenets,
			       struct gdma_queue *queue)
{
	int err;

	if (flush_evenets) {
		err = mana_gd_test_eq(gc, queue);
		if (err)
			dev_warn(gc->dev, "Failed to flush EQ: %d\n", err);
	}

	mana_gd_deregiser_irq(queue);

	if (queue->eq.disable_needed)
		mana_gd_disable_queue(queue);
}

static int mana_gd_create_eq(struct gdma_dev *gd,
			     const struct gdma_queue_spec *spec,
			     bool create_hwq, struct gdma_queue *queue)
{
	struct gdma_context *gc = gd->gdma_context;
	struct device *dev = gc->dev;
	u32 log2_num_entries;
	int err;

	queue->eq.msix_index = INVALID_PCI_MSIX_INDEX;

	log2_num_entries = ilog2(queue->queue_size / GDMA_EQE_SIZE);

	if (spec->eq.log2_throttle_limit > log2_num_entries) {
		dev_err(dev, "EQ throttling limit (%lu) > maximum EQE (%u)\n",
			spec->eq.log2_throttle_limit, log2_num_entries);
		return -EINVAL;
	}

	err = mana_gd_register_irq(queue, spec);
	if (err) {
		dev_err(dev, "Failed to register irq: %d\n", err);
		return err;
	}

	queue->eq.callback = spec->eq.callback;
	queue->eq.context = spec->eq.context;
	queue->head |= INITIALIZED_OWNER_BIT(log2_num_entries);
	queue->eq.log2_throttle_limit = spec->eq.log2_throttle_limit ?: 1;

	if (create_hwq) {
		err = mana_gd_create_hw_eq(gc, queue);
		if (err)
			goto out;

		err = mana_gd_test_eq(gc, queue);
		if (err)
			goto out;
	}

	return 0;
out:
	dev_err(dev, "Failed to create EQ: %d\n", err);
	mana_gd_destroy_eq(gc, false, queue);
	return err;
}

static void mana_gd_create_cq(const struct gdma_queue_spec *spec,
			      struct gdma_queue *queue)
{
	u32 log2_num_entries = ilog2(spec->queue_size / GDMA_CQE_SIZE);

	queue->head |= INITIALIZED_OWNER_BIT(log2_num_entries);
	queue->cq.parent = spec->cq.parent_eq;
	queue->cq.context = spec->cq.context;
	queue->cq.callback = spec->cq.callback;
}

static void mana_gd_destroy_cq(struct gdma_context *gc,
			       struct gdma_queue *queue)
{
	u32 id = queue->id;

	if (id >= gc->max_num_cqs)
		return;

	if (!gc->cq_table[id])
		return;

	gc->cq_table[id] = NULL;
}

int mana_gd_create_hwc_queue(struct gdma_dev *gd,
			     const struct gdma_queue_spec *spec,
			     struct gdma_queue **queue_ptr)
{
	struct gdma_context *gc = gd->gdma_context;
	struct gdma_mem_info *gmi;
	struct gdma_queue *queue;
	int err;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return -ENOMEM;

	gmi = &queue->mem_info;
	err = mana_gd_alloc_memory(gc, spec->queue_size, gmi);
	if (err)
		goto free_q;

	queue->head = 0;
	queue->tail = 0;
	queue->queue_mem_ptr = gmi->virt_addr;
	queue->queue_size = spec->queue_size;
	queue->monitor_avl_buf = spec->monitor_avl_buf;
	queue->type = spec->type;
	queue->gdma_dev = gd;

	if (spec->type == GDMA_EQ)
		err = mana_gd_create_eq(gd, spec, false, queue);
	else if (spec->type == GDMA_CQ)
		mana_gd_create_cq(spec, queue);

	if (err)
		goto out;

	*queue_ptr = queue;
	return 0;
out:
	mana_gd_free_memory(gmi);
free_q:
	kfree(queue);
	return err;
}

static void mana_gd_destroy_dma_region(struct gdma_context *gc, u64 gdma_region)
{
	struct gdma_destroy_dma_region_req req = {};
	struct gdma_general_resp resp = {};
	int err;

	if (gdma_region == GDMA_INVALID_DMA_REGION)
		return;

	mana_gd_init_req_hdr(&req.hdr, GDMA_DESTROY_DMA_REGION, sizeof(req),
			     sizeof(resp));
	req.gdma_region = gdma_region;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status)
		dev_err(gc->dev, "Failed to destroy DMA region: %d, 0x%x\n",
			err, resp.hdr.status);
}

static int mana_gd_create_dma_region(struct gdma_dev *gd,
				     struct gdma_mem_info *gmi)
{
	unsigned int num_page = gmi->length / PAGE_SIZE;
	struct gdma_create_dma_region_req *req = NULL;
	struct gdma_create_dma_region_resp resp = {};
	struct gdma_context *gc = gd->gdma_context;
	struct hw_channel_context *hwc;
	u32 length = gmi->length;
	u32 req_msg_size;
	int err;
	int i;

	if (length < PAGE_SIZE || !is_power_of_2(length))
		return -EINVAL;

	if (offset_in_page(gmi->virt_addr) != 0)
		return -EINVAL;

	hwc = gc->hwc.driver_data;
	req_msg_size = sizeof(*req) + num_page * sizeof(u64);
	if (req_msg_size > hwc->max_req_msg_size)
		return -EINVAL;

	req = kzalloc(req_msg_size, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	mana_gd_init_req_hdr(&req->hdr, GDMA_CREATE_DMA_REGION,
			     req_msg_size, sizeof(resp));
	req->length = length;
	req->offset_in_page = 0;
	req->gdma_page_type = GDMA_PAGE_TYPE_4K;
	req->page_count = num_page;
	req->page_addr_list_len = num_page;

	for (i = 0; i < num_page; i++)
		req->page_addr_list[i] = gmi->dma_handle +  i * PAGE_SIZE;

	err = mana_gd_send_request(gc, req_msg_size, req, sizeof(resp), &resp);
	if (err)
		goto out;

	if (resp.hdr.status || resp.gdma_region == GDMA_INVALID_DMA_REGION) {
		dev_err(gc->dev, "Failed to create DMA region: 0x%x\n",
			resp.hdr.status);
		err = -EPROTO;
		goto out;
	}

	gmi->gdma_region = resp.gdma_region;
out:
	kfree(req);
	return err;
}

int mana_gd_create_mana_eq(struct gdma_dev *gd,
			   const struct gdma_queue_spec *spec,
			   struct gdma_queue **queue_ptr)
{
	struct gdma_context *gc = gd->gdma_context;
	struct gdma_mem_info *gmi;
	struct gdma_queue *queue;
	int err;

	if (spec->type != GDMA_EQ)
		return -EINVAL;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return -ENOMEM;

	gmi = &queue->mem_info;
	err = mana_gd_alloc_memory(gc, spec->queue_size, gmi);
	if (err)
		goto free_q;

	err = mana_gd_create_dma_region(gd, gmi);
	if (err)
		goto out;

	queue->head = 0;
	queue->tail = 0;
	queue->queue_mem_ptr = gmi->virt_addr;
	queue->queue_size = spec->queue_size;
	queue->monitor_avl_buf = spec->monitor_avl_buf;
	queue->type = spec->type;
	queue->gdma_dev = gd;

	err = mana_gd_create_eq(gd, spec, true, queue);
	if (err)
		goto out;

	*queue_ptr = queue;
	return 0;
out:
	mana_gd_free_memory(gmi);
free_q:
	kfree(queue);
	return err;
}

int mana_gd_create_mana_wq_cq(struct gdma_dev *gd,
			      const struct gdma_queue_spec *spec,
			      struct gdma_queue **queue_ptr)
{
	struct gdma_context *gc = gd->gdma_context;
	struct gdma_mem_info *gmi;
	struct gdma_queue *queue;
	int err;

	if (spec->type != GDMA_CQ && spec->type != GDMA_SQ &&
	    spec->type != GDMA_RQ)
		return -EINVAL;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return -ENOMEM;

	gmi = &queue->mem_info;
	err = mana_gd_alloc_memory(gc, spec->queue_size, gmi);
	if (err)
		goto free_q;

	err = mana_gd_create_dma_region(gd, gmi);
	if (err)
		goto out;

	queue->head = 0;
	queue->tail = 0;
	queue->queue_mem_ptr = gmi->virt_addr;
	queue->queue_size = spec->queue_size;
	queue->monitor_avl_buf = spec->monitor_avl_buf;
	queue->type = spec->type;
	queue->gdma_dev = gd;

	if (spec->type == GDMA_CQ)
		mana_gd_create_cq(spec, queue);

	*queue_ptr = queue;
	return 0;
out:
	mana_gd_free_memory(gmi);
free_q:
	kfree(queue);
	return err;
}

void mana_gd_destroy_queue(struct gdma_context *gc, struct gdma_queue *queue)
{
	struct gdma_mem_info *gmi = &queue->mem_info;

	switch (queue->type) {
	case GDMA_EQ:
		mana_gd_destroy_eq(gc, queue->eq.disable_needed, queue);
		break;

	case GDMA_CQ:
		mana_gd_destroy_cq(gc, queue);
		break;

	case GDMA_RQ:
		break;

	case GDMA_SQ:
		break;

	default:
		dev_err(gc->dev, "Can't destroy unknown queue: type=%d\n",
			queue->type);
		return;
	}

	mana_gd_destroy_dma_region(gc, gmi->gdma_region);
	mana_gd_free_memory(gmi);
	kfree(queue);
}

int mana_gd_verify_vf_version(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	struct gdma_verify_ver_resp resp = {};
	struct gdma_verify_ver_req req = {};
	int err;

	mana_gd_init_req_hdr(&req.hdr, GDMA_VERIFY_VF_DRIVER_VERSION,
			     sizeof(req), sizeof(resp));

	req.protocol_ver_min = GDMA_PROTOCOL_FIRST;
	req.protocol_ver_max = GDMA_PROTOCOL_LAST;

	req.gd_drv_cap_flags1 = GDMA_DRV_CAP_FLAGS1;
	req.gd_drv_cap_flags2 = GDMA_DRV_CAP_FLAGS2;
	req.gd_drv_cap_flags3 = GDMA_DRV_CAP_FLAGS3;
	req.gd_drv_cap_flags4 = GDMA_DRV_CAP_FLAGS4;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		dev_err(gc->dev, "VfVerifyVersionOutput: %d, status=0x%x\n",
			err, resp.hdr.status);
		return err ? err : -EPROTO;
	}

	return 0;
}

int mana_gd_register_device(struct gdma_dev *gd)
{
	struct gdma_context *gc = gd->gdma_context;
	struct gdma_register_device_resp resp = {};
	struct gdma_general_req req = {};
	int err;

	gd->pdid = INVALID_PDID;
	gd->doorbell = INVALID_DOORBELL;
	gd->gpa_mkey = INVALID_MEM_KEY;

	mana_gd_init_req_hdr(&req.hdr, GDMA_REGISTER_DEVICE, sizeof(req),
			     sizeof(resp));

	req.hdr.dev_id = gd->dev_id;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		dev_err(gc->dev, "gdma_register_device_resp failed: %d, 0x%x\n",
			err, resp.hdr.status);
		return err ? err : -EPROTO;
	}

	gd->pdid = resp.pdid;
	gd->gpa_mkey = resp.gpa_mkey;
	gd->doorbell = resp.db_id;

	return 0;
}

int mana_gd_deregister_device(struct gdma_dev *gd)
{
	struct gdma_context *gc = gd->gdma_context;
	struct gdma_general_resp resp = {};
	struct gdma_general_req req = {};
	int err;

	if (gd->pdid == INVALID_PDID)
		return -EINVAL;

	mana_gd_init_req_hdr(&req.hdr, GDMA_DEREGISTER_DEVICE, sizeof(req),
			     sizeof(resp));

	req.hdr.dev_id = gd->dev_id;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		dev_err(gc->dev, "Failed to deregister device: %d, 0x%x\n",
			err, resp.hdr.status);
		if (!err)
			err = -EPROTO;
	}

	gd->pdid = INVALID_PDID;
	gd->doorbell = INVALID_DOORBELL;
	gd->gpa_mkey = INVALID_MEM_KEY;

	return err;
}

u32 mana_gd_wq_avail_space(struct gdma_queue *wq)
{
	u32 used_space = (wq->head - wq->tail) * GDMA_WQE_BU_SIZE;
	u32 wq_size = wq->queue_size;

	WARN_ON_ONCE(used_space > wq_size);

	return wq_size - used_space;
}

u8 *mana_gd_get_wqe_ptr(const struct gdma_queue *wq, u32 wqe_offset)
{
	u32 offset = (wqe_offset * GDMA_WQE_BU_SIZE) & (wq->queue_size - 1);

	WARN_ON_ONCE((offset + GDMA_WQE_BU_SIZE) > wq->queue_size);

	return wq->queue_mem_ptr + offset;
}

static u32 mana_gd_write_client_oob(const struct gdma_wqe_request *wqe_req,
				    enum gdma_queue_type q_type,
				    u32 client_oob_size, u32 sgl_data_size,
				    u8 *wqe_ptr)
{
	bool oob_in_sgl = !!(wqe_req->flags & GDMA_WR_OOB_IN_SGL);
	bool pad_data = !!(wqe_req->flags & GDMA_WR_PAD_BY_SGE0);
	struct gdma_wqe *header = (struct gdma_wqe *)wqe_ptr;
	u8 *ptr;

	memset(header, 0, sizeof(struct gdma_wqe));
	header->num_sge = wqe_req->num_sge;
	header->inline_oob_size_div4 = client_oob_size / sizeof(u32);

	if (oob_in_sgl) {
		WARN_ON_ONCE(!pad_data || wqe_req->num_sge < 2);

		header->client_oob_in_sgl = 1;

		if (pad_data)
			header->last_vbytes = wqe_req->sgl[0].size;
	}

	if (q_type == GDMA_SQ)
		header->client_data_unit = wqe_req->client_data_unit;

	/* The size of gdma_wqe + client_oob_size must be less than or equal
	 * to one Basic Unit (i.e. 32 bytes), so the pointer can't go beyond
	 * the queue memory buffer boundary.
	 */
	ptr = wqe_ptr + sizeof(header);

	if (wqe_req->inline_oob_data && wqe_req->inline_oob_size > 0) {
		memcpy(ptr, wqe_req->inline_oob_data, wqe_req->inline_oob_size);

		if (client_oob_size > wqe_req->inline_oob_size)
			memset(ptr + wqe_req->inline_oob_size, 0,
			       client_oob_size - wqe_req->inline_oob_size);
	}

	return sizeof(header) + client_oob_size;
}

static void mana_gd_write_sgl(struct gdma_queue *wq, u8 *wqe_ptr,
			      const struct gdma_wqe_request *wqe_req)
{
	u32 sgl_size = sizeof(struct gdma_sge) * wqe_req->num_sge;
	const u8 *address = (u8 *)wqe_req->sgl;
	u8 *base_ptr, *end_ptr;
	u32 size_to_end;

	base_ptr = wq->queue_mem_ptr;
	end_ptr = base_ptr + wq->queue_size;
	size_to_end = (u32)(end_ptr - wqe_ptr);

	if (size_to_end < sgl_size) {
		memcpy(wqe_ptr, address, size_to_end);

		wqe_ptr = base_ptr;
		address += size_to_end;
		sgl_size -= size_to_end;
	}

	memcpy(wqe_ptr, address, sgl_size);
}

int mana_gd_post_work_request(struct gdma_queue *wq,
			      const struct gdma_wqe_request *wqe_req,
			      struct gdma_posted_wqe_info *wqe_info)
{
	u32 client_oob_size = wqe_req->inline_oob_size;
	struct gdma_context *gc;
	u32 sgl_data_size;
	u32 max_wqe_size;
	u32 wqe_size;
	u8 *wqe_ptr;

	if (wqe_req->num_sge == 0)
		return -EINVAL;

	if (wq->type == GDMA_RQ) {
		if (client_oob_size != 0)
			return -EINVAL;

		client_oob_size = INLINE_OOB_SMALL_SIZE;

		max_wqe_size = GDMA_MAX_RQE_SIZE;
	} else {
		if (client_oob_size != INLINE_OOB_SMALL_SIZE &&
		    client_oob_size != INLINE_OOB_LARGE_SIZE)
			return -EINVAL;

		max_wqe_size = GDMA_MAX_SQE_SIZE;
	}

	sgl_data_size = sizeof(struct gdma_sge) * wqe_req->num_sge;
	wqe_size = ALIGN(sizeof(struct gdma_wqe) + client_oob_size +
			 sgl_data_size, GDMA_WQE_BU_SIZE);
	if (wqe_size > max_wqe_size)
		return -EINVAL;

	if (wq->monitor_avl_buf && wqe_size > mana_gd_wq_avail_space(wq)) {
		gc = wq->gdma_dev->gdma_context;
		dev_err(gc->dev, "unsuccessful flow control!\n");
		return -ENOSPC;
	}

	if (wqe_info)
		wqe_info->wqe_size_in_bu = wqe_size / GDMA_WQE_BU_SIZE;

	wqe_ptr = mana_gd_get_wqe_ptr(wq, wq->head);
	wqe_ptr += mana_gd_write_client_oob(wqe_req, wq->type, client_oob_size,
					    sgl_data_size, wqe_ptr);
	if (wqe_ptr >= (u8 *)wq->queue_mem_ptr + wq->queue_size)
		wqe_ptr -= wq->queue_size;

	mana_gd_write_sgl(wq, wqe_ptr, wqe_req);

	wq->head += wqe_size / GDMA_WQE_BU_SIZE;

	return 0;
}

int mana_gd_post_and_ring(struct gdma_queue *queue,
			  const struct gdma_wqe_request *wqe_req,
			  struct gdma_posted_wqe_info *wqe_info)
{
	struct gdma_context *gc = queue->gdma_dev->gdma_context;
	int err;

	err = mana_gd_post_work_request(queue, wqe_req, wqe_info);
	if (err)
		return err;

	mana_gd_wq_ring_doorbell(gc, queue);

	return 0;
}

static int mana_gd_read_cqe(struct gdma_queue *cq, struct gdma_comp *comp)
{
	unsigned int num_cqe = cq->queue_size / sizeof(struct gdma_cqe);
	struct gdma_cqe *cq_cqe = cq->queue_mem_ptr;
	u32 owner_bits, new_bits, old_bits;
	struct gdma_cqe *cqe;

	cqe = &cq_cqe[cq->head % num_cqe];
	owner_bits = cqe->cqe_info.owner_bits;

	old_bits = (cq->head / num_cqe - 1) & GDMA_CQE_OWNER_MASK;
	/* Return 0 if no more entries. */
	if (owner_bits == old_bits)
		return 0;

	new_bits = (cq->head / num_cqe) & GDMA_CQE_OWNER_MASK;
	/* Return -1 if overflow detected. */
	if (WARN_ON_ONCE(owner_bits != new_bits))
		return -1;

	comp->wq_num = cqe->cqe_info.wq_num;
	comp->is_sq = cqe->cqe_info.is_sq;
	memcpy(comp->cqe_data, cqe->cqe_data, GDMA_COMP_DATA_SIZE);

	return 1;
}

int mana_gd_poll_cq(struct gdma_queue *cq, struct gdma_comp *comp, int num_cqe)
{
	int cqe_idx;
	int ret;

	for (cqe_idx = 0; cqe_idx < num_cqe; cqe_idx++) {
		ret = mana_gd_read_cqe(cq, &comp[cqe_idx]);

		if (ret < 0) {
			cq->head -= cqe_idx;
			return ret;
		}

		if (ret == 0)
			break;

		cq->head++;
	}

	return cqe_idx;
}

static irqreturn_t mana_gd_intr(int irq, void *arg)
{
	struct gdma_irq_context *gic = arg;

	if (gic->handler)
		gic->handler(gic->arg);

	return IRQ_HANDLED;
}

int mana_gd_alloc_res_map(u32 res_avail, struct gdma_resource *r)
{
	r->map = bitmap_zalloc(res_avail, GFP_KERNEL);
	if (!r->map)
		return -ENOMEM;

	r->size = res_avail;
	spin_lock_init(&r->lock);

	return 0;
}

void mana_gd_free_res_map(struct gdma_resource *r)
{
	bitmap_free(r->map);
	r->map = NULL;
	r->size = 0;
}

static int mana_gd_setup_irqs(struct pci_dev *pdev)
{
	unsigned int max_queues_per_port = num_online_cpus();
	struct gdma_context *gc = pci_get_drvdata(pdev);
	struct gdma_irq_context *gic;
	unsigned int max_irqs;
	int nvec, irq;
	int err, i, j;

	if (max_queues_per_port > MANA_MAX_NUM_QUEUES)
		max_queues_per_port = MANA_MAX_NUM_QUEUES;

	/* Need 1 interrupt for the Hardware communication Channel (HWC) */
	max_irqs = max_queues_per_port + 1;

	nvec = pci_alloc_irq_vectors(pdev, 2, max_irqs, PCI_IRQ_MSIX);
	if (nvec < 0)
		return nvec;

	gc->irq_contexts = kcalloc(nvec, sizeof(struct gdma_irq_context),
				   GFP_KERNEL);
	if (!gc->irq_contexts) {
		err = -ENOMEM;
		goto free_irq_vector;
	}

	for (i = 0; i < nvec; i++) {
		gic = &gc->irq_contexts[i];
		gic->handler = NULL;
		gic->arg = NULL;

		irq = pci_irq_vector(pdev, i);
		if (irq < 0) {
			err = irq;
			goto free_irq;
		}

		err = request_irq(irq, mana_gd_intr, 0, "mana_intr", gic);
		if (err)
			goto free_irq;
	}

	err = mana_gd_alloc_res_map(nvec, &gc->msix_resource);
	if (err)
		goto free_irq;

	gc->max_num_msix = nvec;
	gc->num_msix_usable = nvec;

	return 0;

free_irq:
	for (j = i - 1; j >= 0; j--) {
		irq = pci_irq_vector(pdev, j);
		gic = &gc->irq_contexts[j];
		free_irq(irq, gic);
	}

	kfree(gc->irq_contexts);
	gc->irq_contexts = NULL;
free_irq_vector:
	pci_free_irq_vectors(pdev);
	return err;
}

static void mana_gd_remove_irqs(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	struct gdma_irq_context *gic;
	int irq, i;

	if (gc->max_num_msix < 1)
		return;

	mana_gd_free_res_map(&gc->msix_resource);

	for (i = 0; i < gc->max_num_msix; i++) {
		irq = pci_irq_vector(pdev, i);
		if (irq < 0)
			continue;

		gic = &gc->irq_contexts[i];
		free_irq(irq, gic);
	}

	pci_free_irq_vectors(pdev);

	gc->max_num_msix = 0;
	gc->num_msix_usable = 0;
	kfree(gc->irq_contexts);
	gc->irq_contexts = NULL;
}

static int mana_gd_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct gdma_context *gc;
	void __iomem *bar0_va;
	int bar = 0;
	int err;

	/* Each port has 2 CQs, each CQ has at most 1 EQE at a time */
	BUILD_BUG_ON(2 * MAX_PORTS_IN_MANA_DEV * GDMA_EQE_SIZE > EQ_SIZE);

	err = pci_enable_device(pdev);
	if (err)
		return -ENXIO;

	pci_set_master(pdev);

	err = pci_request_regions(pdev, "mana");
	if (err)
		goto disable_dev;

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err)
		goto release_region;

	err = -ENOMEM;
	gc = vzalloc(sizeof(*gc));
	if (!gc)
		goto release_region;

	bar0_va = pci_iomap(pdev, bar, 0);
	if (!bar0_va)
		goto free_gc;

	gc->bar0_va = bar0_va;
	gc->dev = &pdev->dev;

	pci_set_drvdata(pdev, gc);

	mana_gd_init_registers(pdev);

	mana_smc_init(&gc->shm_channel, gc->dev, gc->shm_base);

	err = mana_gd_setup_irqs(pdev);
	if (err)
		goto unmap_bar;

	mutex_init(&gc->eq_test_event_mutex);

	err = mana_hwc_create_channel(gc);
	if (err)
		goto remove_irq;

	err = mana_gd_verify_vf_version(pdev);
	if (err)
		goto remove_irq;

	err = mana_gd_query_max_resources(pdev);
	if (err)
		goto remove_irq;

	err = mana_gd_detect_devices(pdev);
	if (err)
		goto remove_irq;

	err = mana_probe(&gc->mana);
	if (err)
		goto clean_up_gdma;

	return 0;

clean_up_gdma:
	mana_hwc_destroy_channel(gc);
	vfree(gc->cq_table);
	gc->cq_table = NULL;
remove_irq:
	mana_gd_remove_irqs(pdev);
unmap_bar:
	pci_iounmap(pdev, bar0_va);
free_gc:
	vfree(gc);
release_region:
	pci_release_regions(pdev);
disable_dev:
	pci_clear_master(pdev);
	pci_disable_device(pdev);
	dev_err(&pdev->dev, "gdma probe failed: err = %d\n", err);
	return err;
}

static void mana_gd_remove(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);

	mana_remove(&gc->mana);

	mana_hwc_destroy_channel(gc);
	vfree(gc->cq_table);
	gc->cq_table = NULL;

	mana_gd_remove_irqs(pdev);

	pci_iounmap(pdev, gc->bar0_va);

	vfree(gc);

	pci_release_regions(pdev);
	pci_clear_master(pdev);
	pci_disable_device(pdev);
}

#ifndef PCI_VENDOR_ID_MICROSOFT
#define PCI_VENDOR_ID_MICROSOFT 0x1414
#endif

static const struct pci_device_id mana_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MICROSOFT, 0x00BA) },
	{ }
};

static struct pci_driver mana_driver = {
	.name		= "mana",
	.id_table	= mana_id_table,
	.probe		= mana_gd_probe,
	.remove		= mana_gd_remove,
};

module_pci_driver(mana_driver);

MODULE_DEVICE_TABLE(pci, mana_id_table);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Microsoft Azure Network Adapter driver");
