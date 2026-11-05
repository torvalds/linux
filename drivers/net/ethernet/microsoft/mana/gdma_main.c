// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/utsname.h>
#include <linux/version.h>
#include <linux/msi.h>
#include <linux/irqdomain.h>
#include <linux/export.h>
#include <linux/uaccess.h>

#include <net/mana/mana.h>
#include <net/mana/hw_channel.h>

struct dentry *mana_debugfs_root;

/*
 * True if the underlying bus can allocate MSI-X vectors after probe time.
 * Buses that size their vector pool at probe install no callback.
 */
static bool mana_gd_msix_can_alloc_dyn(struct gdma_context *gc)
{
	if (!gc->bus_ops || !gc->bus_ops->msix_can_alloc_dyn)
		return false;

	return gc->bus_ops->msix_can_alloc_dyn(gc);
}

static int mana_gd_init_pf_regs(struct gdma_context *gc)
{
	u64 remaining_barsize;
	u64 sriov_base_off;
	u64 sriov_shm_off;

	gc->db_page_size = mana_gd_r32(gc, GDMA_PF_REG_DB_PAGE_SIZE) & 0xFFFF;

	/* mana_gd_ring_doorbell() accesses offsets up to DOORBELL_OFFSET_EQ
	 * (0xFF8) + 8 bytes = 4KB within each doorbell page, so the page
	 * size must be at least SZ_4K.
	 */
	if (gc->db_page_size < SZ_4K) {
		dev_err(gc->dev,
			"Doorbell page size %llu too small (min %u)\n",
			gc->db_page_size, SZ_4K);
		return -EPROTO;
	}

	gc->db_page_off = mana_gd_r64(gc, GDMA_PF_REG_DB_PAGE_OFF);

	/* Validate doorbell offset is within BAR0 */
	if (gc->db_page_off >= gc->bar0_size) {
		dev_err(gc->dev,
			"Doorbell offset 0x%llx exceeds BAR0 size 0x%llx\n",
			gc->db_page_off, (u64)gc->bar0_size);
		return -EPROTO;
	}

	gc->db_page_base = gc->bar0_va + gc->db_page_off;
	gc->phys_db_page_base = gc->bar0_pa + gc->db_page_off;

	sriov_base_off = mana_gd_r64(gc, GDMA_SRIOV_REG_CFG_BASE_OFF);
	if (sriov_base_off >= gc->bar0_size ||
	    gc->bar0_size - sriov_base_off <
		GDMA_PF_REG_SHM_OFF + sizeof(u64) ||
	    !IS_ALIGNED(sriov_base_off, sizeof(u64))) {
		dev_err(gc->dev,
			"SRIOV base offset 0x%llx out of range or unaligned (BAR0 size 0x%llx)\n",
			sriov_base_off, (u64)gc->bar0_size);
		return -EPROTO;
	}

	remaining_barsize = gc->bar0_size - sriov_base_off;
	sriov_shm_off = mana_gd_r64(gc, sriov_base_off + GDMA_PF_REG_SHM_OFF);
	if (sriov_shm_off >= remaining_barsize ||
	    remaining_barsize - sriov_shm_off < SMC_APERTURE_SIZE ||
	    !IS_ALIGNED(sriov_shm_off, sizeof(u32))) {
		dev_err(gc->dev,
			"SRIOV SHM offset 0x%llx out of range or unaligned (BAR0 size 0x%llx)\n",
			sriov_shm_off, (u64)gc->bar0_size);
		return -EPROTO;
	}

	gc->shm_base = gc->bar0_va + sriov_base_off + sriov_shm_off;

	return 0;
}

static int mana_gd_init_vf_regs(struct gdma_context *gc)
{
	u64 shm_off;

	gc->db_page_size = mana_gd_r32(gc, GDMA_REG_DB_PAGE_SIZE) & 0xFFFF;

	/* mana_gd_ring_doorbell() accesses offsets up to DOORBELL_OFFSET_EQ
	 * (0xFF8) + 8 bytes = 4KB within each doorbell page, so the page
	 * size must be at least SZ_4K.
	 */
	if (gc->db_page_size < SZ_4K) {
		dev_err(gc->dev,
			"Doorbell page size %llu too small (min %u)\n",
			gc->db_page_size, SZ_4K);
		return -EPROTO;
	}

	gc->db_page_off = mana_gd_r64(gc, GDMA_REG_DB_PAGE_OFFSET);

	/* Validate doorbell offset is within BAR0 */
	if (gc->db_page_off >= gc->bar0_size) {
		dev_err(gc->dev,
			"Doorbell offset 0x%llx exceeds BAR0 size 0x%llx\n",
			gc->db_page_off, (u64)gc->bar0_size);
		return -EPROTO;
	}

	gc->db_page_base = gc->bar0_va + gc->db_page_off;
	gc->phys_db_page_base = gc->bar0_pa + gc->db_page_off;

	shm_off = mana_gd_r64(gc, GDMA_REG_SHM_OFFSET);
	if (shm_off >= gc->bar0_size ||
	    gc->bar0_size - shm_off < SMC_APERTURE_SIZE ||
	    !IS_ALIGNED(shm_off, sizeof(u32))) {
		dev_err(gc->dev,
			"SHM offset 0x%llx out of range or unaligned (BAR0 size 0x%llx)\n",
			shm_off, (u64)gc->bar0_size);
		return -EPROTO;
	}

	gc->shm_base = gc->bar0_va + shm_off;

	return 0;
}

int mana_gd_init_registers(struct gdma_context *gc)
{
	if (gc->is_pf && !gc->is_pf2)
		return mana_gd_init_pf_regs(gc);
	else
		return mana_gd_init_vf_regs(gc);
}

/* Suppress logging when we set timeout to zero */
bool mana_need_log(struct gdma_context *gc, int err)
{
	struct hw_channel_context *hwc;

	if (err != -ETIMEDOUT)
		return true;

	if (!gc)
		return true;

	hwc = gc->hwc.driver_data;
	if (hwc && hwc->hwc_timeout == 0)
		return false;

	return true;
}

int mana_gd_query_max_resources(struct gdma_context *gc)
{
	struct gdma_query_max_resources_resp resp = {};
	struct gdma_general_req req = {};
	unsigned int max_num_queues;
	unsigned int msix_vec_count;
	u8 bm_hostmode;
	u16 num_ports;
	int err;

	/* Reset msi_sharing so it is recomputed from current hardware
	 * state. On resume, num_online_cpus() or num_msix_usable may
	 * have changed, making dedicated MSI-X feasible where it was
	 * not before. Only reset on platforms that support dynamic
	 * MSI-X allocation; on non-dyn platforms msi_sharing is
	 * unconditionally true (set in mana_gd_setup_hwc_irqs).
	 */
	if (mana_gd_msix_can_alloc_dyn(gc))
		gc->msi_sharing = false;

	mana_gd_init_req_hdr(&req.hdr, GDMA_QUERY_MAX_RESOURCES,
			     sizeof(req), sizeof(resp));

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		dev_err(gc->dev, "Failed to query resource info: %d, 0x%x\n",
			err, resp.hdr.status);
		return err ? err : -EPROTO;
	}

	if (!mana_gd_msix_can_alloc_dyn(gc)) {
		/* Buses that size their vector pool at probe time cannot grow
		 * it afterwards, so never raise num_msix_usable above what has
		 * already been allocated.
		 */
		if (gc->num_msix_usable > resp.max_msix)
			gc->num_msix_usable = resp.max_msix;
	} else {
		/* If dynamic allocation is enabled we have already allocated
		 * hwc msi
		 * Also, we make sure in this case the following is always true
		 * (num_msix_usable - 1 HWC) <= num_online_cpus()
		 */
		gc->num_msix_usable = min(resp.max_msix, num_online_cpus() + 1);
	}

	/* MSI-X vectors are allocated by index into the device MSI-X table, so
	 * never ask for more than the table holds. It can be smaller than both
	 * resp.max_msix and the CPU count. A bus that cannot report a table
	 * size installs no callback and skips the clamp.
	 */
	if (gc->bus_ops && gc->bus_ops->msix_vec_count) {
		err = gc->bus_ops->msix_vec_count(gc);
		if (err <= 0) {
			dev_err(gc->dev,
				"Failed to query MSI-X table size: %d\n", err);
			return err < 0 ? err : -ENOSPC;
		}
		msix_vec_count = err;

		if (gc->num_msix_usable > msix_vec_count) {
			dev_info(gc->dev,
				 "Limiting MSI-X vectors from %u to table size %u\n",
				 gc->num_msix_usable, msix_vec_count);
			gc->num_msix_usable = msix_vec_count;
		}
	}

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

	if (gc->max_num_queues == 0)
		return -ENOSPC;

	debugfs_create_u32("num_msix_usable", 0400, gc->mana_pci_debugfs,
			   &gc->num_msix_usable);
	debugfs_create_u32("max_num_queues", 0400, gc->mana_pci_debugfs,
			   &gc->max_num_queues);

	err = mana_gd_query_device_cfg(gc, MANA_MAJOR_VERSION,
				       MANA_MINOR_VERSION,
				       MANA_MICRO_VERSION,
				       &num_ports, &bm_hostmode);
	if (err)
		return err;

	if (!num_ports) {
		dev_err(gc->dev, "Failed to detect any vPort\n");
		return -EINVAL;
	}

	/* Cap to the same limit used by mana_probe() for port instantiation,
	 * so MSI-X and queue budgeting matches the actual port count.
	 */
	if (num_ports > MAX_PORTS_IN_MANA_DEV)
		num_ports = MAX_PORTS_IN_MANA_DEV;

	gc->num_ports = num_ports;

	/*
	 * Adjust the per-vPort max queue count to allow dedicated
	 * MSIx for each vPort. Prefer at least MANA_DEF_NUM_QUEUES,
	 * but the hardware max (gc->max_num_queues) takes precedence.
	 */
	max_num_queues = (gc->num_msix_usable - 1) / num_ports;
	max_num_queues = rounddown_pow_of_two(max(max_num_queues, 1U));
	if (max_num_queues < MANA_DEF_NUM_QUEUES)
		max_num_queues = MANA_DEF_NUM_QUEUES;

	/*
	 * Use dedicated MSIx for EQs whenever possible, use MSIx sharing for
	 * Ethernet EQs when (max_num_queues * num_ports > num_msix_usable - 1).
	 */
	max_num_queues = min(gc->max_num_queues, max_num_queues);
	if (max_num_queues * num_ports > gc->num_msix_usable - 1)
		gc->msi_sharing = true;

	/* If MSI is shared, use max allowed value */
	if (gc->msi_sharing)
		gc->max_num_queues_vport = min(gc->num_msix_usable - 1,
					       gc->max_num_queues);
	else
		gc->max_num_queues_vport = max_num_queues;

	dev_info(gc->dev, "MSI sharing mode %u max queues %u\n",
		 gc->msi_sharing, gc->max_num_queues_vport);

	return 0;
}

static int mana_gd_query_hwc_timeout(struct gdma_context *gc, u32 *timeout_val)
{
	struct gdma_query_hwc_timeout_resp resp = {};
	struct gdma_query_hwc_timeout_req req = {};
	int err;

	mana_gd_init_req_hdr(&req.hdr, GDMA_QUERY_HWC_TIMEOUT,
			     sizeof(req), sizeof(resp));
	req.timeout_ms = *timeout_val;
	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status)
		return err ? err : -EPROTO;

	*timeout_val = resp.timeout_ms;

	return 0;
}

int mana_gd_detect_devices(struct gdma_context *gc)
{
	struct gdma_list_devices_resp resp = {};
	struct gdma_general_req req = {};
	struct gdma_dev_id dev;
	int found_dev = 0;
	u16 dev_type;
	int err;
	u32 i;

	mana_gd_init_req_hdr(&req.hdr, GDMA_LIST_DEVICES, sizeof(req),
			     sizeof(resp));

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		dev_err(gc->dev, "Failed to detect devices: %d, 0x%x\n", err,
			resp.hdr.status);
		return err ? err : -EPROTO;
	}

	for (i = 0; i < GDMA_DEV_LIST_SIZE &&
	     found_dev < resp.num_of_devs; i++) {
		dev = resp.devs[i];
		dev_type = dev.type;

		/* Skip empty devices */
		if (dev.as_uint32 == 0)
			continue;

		found_dev++;

		/* HWC is already detected in mana_hwc_create_channel(). */
		if (dev_type == GDMA_DEVICE_HWC)
			continue;

		if (dev_type == GDMA_DEVICE_MANA) {
			gc->mana.gdma_context = gc;
			gc->mana.dev_id = dev;
		} else if (dev_type == GDMA_DEVICE_MANA_IB) {
			gc->mana_ib.dev_id = dev;
			gc->mana_ib.gdma_context = gc;
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
EXPORT_SYMBOL_NS(mana_gd_send_request, "NET_MANA");

int mana_gd_alloc_memory(struct gdma_context *gc, unsigned int length,
			 struct gdma_mem_info *gmi, bool allow_scatter)
{
	unsigned int npages, i;
	dma_addr_t dma_handle;
	bool can_fallback;
	void *buf;

	if (length < MANA_PAGE_SIZE || !is_power_of_2(length))
		return -EINVAL;

	gmi->dev = gc->dev;

	/* An allocation that fits in one page does not benefit from
	 * fallback.
	 */
	can_fallback = allow_scatter && length > PAGE_SIZE;

	/* Warn only when there is no fallback to rescue the failure. */
	buf = dma_alloc_coherent(gmi->dev, length, &dma_handle,
				 GFP_KERNEL |
				 (can_fallback ? __GFP_NOWARN : 0));
	if (buf) {
		gmi->dma_handle = dma_handle;
		gmi->virt_addr = buf;
		gmi->length = length;
		gmi->nr_pages = 0;
		return 0;
	}

	if (!can_fallback)
		return -ENOMEM;

	/* length is a power of 2 above PAGE_SIZE, so this divides exactly. */
	npages = length / PAGE_SIZE;

	gmi->pages_va = kvzalloc_objs(*gmi->pages_va, npages);
	if (!gmi->pages_va)
		return -ENOMEM;

	gmi->pages_dma = kvzalloc_objs(*gmi->pages_dma, npages);
	if (!gmi->pages_dma)
		goto free_va;

	for (i = 0; i < npages; i++) {
		gmi->pages_va[i] = dma_alloc_coherent(gmi->dev, PAGE_SIZE,
						      &gmi->pages_dma[i],
						      GFP_KERNEL);
		if (!gmi->pages_va[i])
			goto free_pages;
	}

	dev_info_ratelimited(gmi->dev,
			     "contiguous %u-byte DMA alloc failed; using %u scattered pages\n",
			     length, npages);

	gmi->virt_addr = NULL;
	gmi->dma_handle = 0;
	gmi->length = length;
	gmi->nr_pages = npages;

	return 0;

free_pages:
	while (i--)
		dma_free_coherent(gmi->dev, PAGE_SIZE, gmi->pages_va[i],
				  gmi->pages_dma[i]);
	kvfree(gmi->pages_dma);
	gmi->pages_dma = NULL;
free_va:
	kvfree(gmi->pages_va);
	gmi->pages_va = NULL;
	return -ENOMEM;
}

void mana_gd_free_memory(struct gdma_mem_info *gmi)
{
	unsigned int i;

	if (gmi->nr_pages > 0) {
		for (i = 0; i < gmi->nr_pages; i++)
			dma_free_coherent(gmi->dev, PAGE_SIZE, gmi->pages_va[i],
					  gmi->pages_dma[i]);
		kvfree(gmi->pages_va);
		kvfree(gmi->pages_dma);
		gmi->pages_va = NULL;
		gmi->pages_dma = NULL;
		gmi->nr_pages = 0;
		return;
	}

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
	req.gdma_region = queue->mem_info.dma_region_handle;
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
	queue->mem_info.dma_region_handle = GDMA_INVALID_DMA_REGION;
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
		if (mana_need_log(gc, err))
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
#define DOORBELL_OFFSET_DIM	0x820

static void mana_gd_ring_doorbell(struct gdma_context *gc, u32 db_index,
				  enum gdma_queue_type q_type, u32 qid,
				  u32 tail_ptr, u8 num_req, u8 client_offset)
{
	void __iomem *addr = gc->db_page_base + gc->db_page_size * db_index;
	union gdma_doorbell_entry e = {};

	addr += client_offset;

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

	case GDMA_DIM:
		e.dim.id = qid;
		e.dim.mod_usec = FIELD_GET(MANA_INTR_MODR_USEC_MAX, tail_ptr);
		e.dim.mod_usec_vld = !!(tail_ptr & MANA_INTR_MODR_USEC_VLD);
		e.dim.mod_comps = FIELD_GET(MANA_INTR_MODR_COMP_MASK, tail_ptr);
		e.dim.mod_comps_vld = num_req;

		addr += DOORBELL_OFFSET_DIM;
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
	/* Hardware Spec specifies that software client should set 0 for
	 * wqe_cnt for Receive Queues. This value is not used in Send Queues.
	 */
	mana_gd_ring_doorbell(gc, queue->gdma_dev->doorbell, queue->type,
			      queue->id, queue->head * GDMA_WQE_BU_SIZE, 0, 0);
}
EXPORT_SYMBOL_NS(mana_gd_wq_ring_doorbell, "NET_MANA");

void mana_gd_wq_ring_doorbell_ext(struct gdma_context *gc, struct gdma_queue *queue,
				  u32 tail_ptr, u8 wqe_cnt, u8 client_offset)
{
	mana_gd_ring_doorbell(gc, queue->gdma_dev->doorbell, queue->type,
			      queue->id, tail_ptr, wqe_cnt, client_offset);
}
EXPORT_SYMBOL_NS(mana_gd_wq_ring_doorbell_ext, "NET_MANA");

void mana_gd_ring_cq(struct gdma_queue *cq, u8 arm_bit)
{
	struct gdma_context *gc = cq->gdma_dev->gdma_context;

	u32 num_cqe = cq->queue_size / GDMA_CQE_SIZE;

	u32 head = cq->head % (num_cqe << GDMA_CQE_OWNER_BITS);

	mana_gd_ring_doorbell(gc, cq->gdma_dev->doorbell, cq->type, cq->id,
			      head, arm_bit, 0);
}
EXPORT_SYMBOL_NS(mana_gd_ring_cq, "NET_MANA");

void mana_gd_ring_dim(struct gdma_queue *cq, u32 mod_usec, bool mod_usec_vld,
		      u32 mod_comps, bool mod_comps_vld)
{
	struct gdma_context *gc = cq->gdma_dev->gdma_context;
	u32 dim_val;

	/* Convert the DIM values to doorbell parameters */
	dim_val = FIELD_PREP(MANA_INTR_MODR_USEC_MAX, mod_usec) |
		  FIELD_PREP(MANA_INTR_MODR_COMP_MASK, mod_comps);
	if (mod_usec_vld)
		dim_val |= MANA_INTR_MODR_USEC_VLD;

	mana_gd_ring_doorbell(gc, cq->gdma_dev->doorbell, GDMA_DIM, cq->id,
			      dim_val, mod_comps_vld, 0);
}
EXPORT_SYMBOL_NS(mana_gd_ring_dim, "NET_MANA");

/*
 * Queue device servicing or recovery on buses that support it.
 *
 * Servicing tears the device down and brings it back up, so it depends on
 * bus-level facilities the GDMA core does not have. Buses that provide no
 * servicing path install no callback and the request is rejected.
 */
int mana_schedule_serv_work(struct gdma_context *gc, enum gdma_eqe_type type)
{
	if (!gc->bus_ops || !gc->bus_ops->schedule_serv_work)
		return -EOPNOTSUPP;

	return gc->bus_ops->schedule_serv_work(gc, type);
}

/* The servicing workqueue is owned by the GDMA core because the queueing
 * sites live here and in mana_en.c, which every transport shares. Each bus
 * driver creates it during setup and destroys it during cleanup.
 */
int mana_gd_alloc_service_wq(struct gdma_context *gc)
{
	gc->service_wq = alloc_ordered_workqueue("gdma_service_wq", 0);
	if (!gc->service_wq)
		return -ENOMEM;

	return 0;
}

/* Return the CPU address of byte @offset within a queue's ring buffer. */
static void *mana_gd_ring_ptr(const struct gdma_queue *q, u32 offset)
{
	const struct gdma_mem_info *gmi = &q->mem_info;

	if (gmi->nr_pages > 0)
		return (u8 *)gmi->pages_va[offset / PAGE_SIZE] +
		       (offset & (PAGE_SIZE - 1));

	return q->queue_mem_ptr + offset;
}

/* Number of bytes from @offset to the end of the CPU-contiguous region: the
 * rest of the ring, or the rest of the current page when scattered.
 */
static u32 mana_gd_ring_contig_avail(const struct gdma_queue *q, u32 offset)
{
	if (q->mem_info.nr_pages > 0)
		return PAGE_SIZE - (offset & (PAGE_SIZE - 1));

	return q->queue_size - offset;
}

/* Copy up to @count bytes from ring offset *@pos of @q into user buffer @buf,
 * so a scattered ring reads back as if it were contiguous. Returns bytes
 * copied, 0 at end of ring, or a negative errno.
 */
ssize_t mana_gd_read_ring(struct gdma_queue *q, char __user *buf,
			  size_t count, loff_t *pos)
{
	u32 size = q->queue_size;
	loff_t off = *pos;
	size_t copied = 0;

	if (off < 0)
		return -EINVAL;
	if (off >= size || !count)
		return 0;
	count = min_t(size_t, count, size - off);

	while (count) {
		u32 offset = off;
		u32 avail = mana_gd_ring_contig_avail(q, offset);
		size_t chunk = min_t(size_t, count, avail);
		size_t left = copy_to_user(buf, mana_gd_ring_ptr(q, offset),
					   chunk);

		chunk -= left;
		buf += chunk;
		off += chunk;
		copied += chunk;
		count -= chunk;
		if (left)
			break;
	}

	if (!copied)
		return -EFAULT;

	*pos = off;
	return copied;
}

void mana_gd_free_service_wq(struct gdma_context *gc)
{
	if (!gc->service_wq)
		return;

	destroy_workqueue(gc->service_wq);
	gc->service_wq = NULL;
}

static void mana_gd_process_eqe(struct gdma_queue *eq)
{
	u32 head = eq->head % (eq->queue_size / GDMA_EQE_SIZE);
	struct gdma_context *gc = eq->gdma_dev->gdma_context;
	union gdma_eqe_info eqe_info;
	enum gdma_eqe_type type;
	struct gdma_event event;
	struct gdma_queue *cq;
	struct gdma_eqe *eqe;
	u32 cq_id;

	eqe = mana_gd_ring_ptr(eq, head * sizeof(*eqe));
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
	case GDMA_EQE_HWC_SOC_SERVICE:
	case GDMA_EQE_RNIC_QP_FATAL:
	case GDMA_EQE_HWC_SOC_RECONFIG_DATA:
		if (!eq->eq.callback)
			break;

		event.type = type;
		memcpy(&event.details, &eqe->details, GDMA_EVENT_DATA_SIZE);
		eq->eq.callback(eq->eq.context, eq, &event);
		break;

	case GDMA_EQE_HWC_FPGA_RECONFIG:
	case GDMA_EQE_HWC_RESET_REQUEST:
		dev_info(gc->dev, "Recv MANA service type:%d\n", type);

		if (!test_and_set_bit(GC_PROBE_SUCCEEDED, &gc->flags)) {
			/*
			 * Device is in probe and we received a hardware reset
			 * event, the probe function will detect that the flag
			 * has changed and perform service procedure.
			 */
			dev_info(gc->dev,
				 "Service is to be processed in probe\n");
			break;
		}
		mana_schedule_serv_work(gc, type);
		break;

	default:
		break;
	}
}

void mana_gd_process_eq_events(void *arg)
{
	u32 owner_bits, new_bits, old_bits;
	union gdma_eqe_info eqe_info;
	struct gdma_queue *eq = arg;
	struct gdma_context *gc;
	struct gdma_eqe *eqe;
	u32 head, num_eqe;
	int i;

	gc = eq->gdma_dev->gdma_context;

	num_eqe = eq->queue_size / GDMA_EQE_SIZE;

	/* Process up to 5 EQEs at a time, and update the HW head. */
	for (i = 0; i < 5; i++) {
		eqe = mana_gd_ring_ptr(eq, (eq->head % num_eqe) * sizeof(*eqe));
		eqe_info.as_uint32 = eqe->eqe_info;
		owner_bits = eqe_info.owner_bits;

		old_bits = (eq->head / num_eqe - 1) & GDMA_EQE_OWNER_MASK;
		/* No more entries */
		if (owner_bits == old_bits) {
			/* return here without ringing the doorbell */
			if (i == 0)
				return;
			break;
		}

		new_bits = (eq->head / num_eqe) & GDMA_EQE_OWNER_MASK;
		if (owner_bits != new_bits) {
			dev_err(gc->dev, "EQ %d: overflow detected\n", eq->id);
			break;
		}

		/* Per GDMA spec, rmb is necessary after checking owner_bits, before
		 * reading eqe.
		 */
		rmb();

		mana_gd_process_eqe(eq);

		eq->head++;
	}

	head = eq->head % (num_eqe << GDMA_EQE_OWNER_BITS);

	mana_gd_ring_doorbell(gc, eq->gdma_dev->doorbell, eq->type, eq->id,
			      head, SET_ARM_BIT, 0);
}

static int mana_gd_register_irq(struct gdma_queue *queue,
				const struct gdma_queue_spec *spec)
{
	struct gdma_dev *gd = queue->gdma_dev;
	struct gdma_irq_context *gic;
	struct gdma_context *gc;
	unsigned int msi_index;
	unsigned long flags;
	struct device *dev;
	int err = 0;

	gc = gd->gdma_context;
	dev = gc->dev;
	msi_index = spec->eq.msix_index;

	if (msi_index >= gc->num_msix_usable) {
		err = -ENOSPC;
		dev_err(dev, "Register IRQ err:%d, msi:%u nMSI:%u",
			err, msi_index, gc->num_msix_usable);

		return err;
	}

	queue->eq.msix_index = msi_index;
	/* The caller acquired a GIC reference via mana_gd_get_gic().
	 * That refcount prevents mana_gd_put_gic() from erasing this
	 * irq_contexts entry concurrently.
	 */
	gic = xa_load(&gc->irq_contexts, msi_index);
	if (WARN_ON(!gic))
		return -EINVAL;

	spin_lock_irqsave(&gic->lock, flags);
	list_add_rcu(&queue->entry, &gic->eq_list);
	spin_unlock_irqrestore(&gic->lock, flags);

	return 0;
}

static void mana_gd_deregister_irq(struct gdma_queue *queue)
{
	struct gdma_dev *gd = queue->gdma_dev;
	struct gdma_irq_context *gic;
	struct gdma_context *gc;
	unsigned int msix_index;
	unsigned long flags;
	struct gdma_queue *eq;

	gc = gd->gdma_context;

	/* At most num_online_cpus() + 1 interrupts are used. */
	msix_index = queue->eq.msix_index;
	if (WARN_ON(msix_index >= gc->num_msix_usable))
		return;

	/* The caller releases the GIC reference via mana_gd_put_gic()
	 * after this function returns. The refcount guarantees this
	 * irq_contexts entry is still valid.
	 */
	gic = xa_load(&gc->irq_contexts, msix_index);
	if (WARN_ON(!gic))
		return;

	spin_lock_irqsave(&gic->lock, flags);
	list_for_each_entry_rcu(eq, &gic->eq_list, entry) {
		if (queue == eq) {
			list_del_rcu(&eq->entry);
			break;
		}
	}
	spin_unlock_irqrestore(&gic->lock, flags);

	synchronize_rcu();
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
		if (mana_need_log(gc, err))
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
		if (err && mana_need_log(gc, err))
			dev_warn(gc->dev, "Failed to flush EQ: %d\n", err);
	}

	mana_gd_deregister_irq(queue);

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
	queue->id = INVALID_QUEUE_ID;

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
	queue->eq.msix_index = INVALID_PCI_MSIX_INDEX;
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

	queue = kzalloc_obj(*queue);
	if (!queue)
		return -ENOMEM;

	gmi = &queue->mem_info;
	err = mana_gd_alloc_memory(gc, spec->queue_size, gmi, false);
	if (err) {
		dev_err(gc->dev, "GDMA queue type: %d, size: %u, gdma memory allocation err: %d\n",
			spec->type, spec->queue_size, err);
		goto free_q;
	}

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
	dev_err(gc->dev, "Failed to create queue type %d of size %u, err: %d\n",
		spec->type, spec->queue_size, err);
	mana_gd_free_memory(gmi);
free_q:
	kfree(queue);
	return err;
}

int mana_gd_destroy_dma_region(struct gdma_context *gc, u64 dma_region_handle)
{
	struct gdma_destroy_dma_region_req req = {};
	struct gdma_general_resp resp = {};
	int err;

	if (dma_region_handle == GDMA_INVALID_DMA_REGION)
		return 0;

	mana_gd_init_req_hdr(&req.hdr, GDMA_DESTROY_DMA_REGION, sizeof(req),
			     sizeof(resp));
	req.dma_region_handle = dma_region_handle;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		if (mana_need_log(gc, err))
			dev_err(gc->dev, "Failed to destroy DMA region: %d, 0x%x\n",
				err, resp.hdr.status);
		return -EPROTO;
	}

	return 0;
}
EXPORT_SYMBOL_NS(mana_gd_destroy_dma_region, "NET_MANA");

static int mana_gd_create_dma_region(struct gdma_dev *gd,
				     struct gdma_mem_info *gmi)
{
	unsigned int num_page = gmi->length / MANA_PAGE_SIZE;
	struct gdma_create_dma_region_req *req = NULL;
	struct gdma_create_dma_region_resp resp = {};
	struct gdma_context *gc = gd->gdma_context;
	struct hw_channel_context *hwc;
	u32 length = gmi->length;
	size_t req_msg_size;
	int err;
	int i;

	if (length < MANA_PAGE_SIZE || !is_power_of_2(length))
		return -EINVAL;

	if (gmi->nr_pages == 0 && !MANA_PAGE_ALIGNED(gmi->virt_addr))
		return -EINVAL;

	hwc = gc->hwc.driver_data;
	req_msg_size = struct_size(req, page_addr_list, num_page);
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

	if (gmi->nr_pages > 0) {
		unsigned int subpages = PAGE_SIZE / MANA_PAGE_SIZE;
		unsigned int idx = 0;
		unsigned int pg, sub;

		/* Each PAGE_SIZE chunk is physically contiguous and contains
		 * PAGE_SIZE / MANA_PAGE_SIZE consecutive device pages.
		 */
		for (pg = 0; pg < gmi->nr_pages; pg++)
			for (sub = 0; sub < subpages; sub++)
				req->page_addr_list[idx++] =
					gmi->pages_dma[pg] +
					sub * MANA_PAGE_SIZE;
	} else {
		for (i = 0; i < num_page; i++)
			req->page_addr_list[i] =
				gmi->dma_handle + i * MANA_PAGE_SIZE;
	}

	err = mana_gd_send_request(gc, req_msg_size, req, sizeof(resp), &resp);
	if (err)
		goto out;

	if (resp.hdr.status ||
	    resp.dma_region_handle == GDMA_INVALID_DMA_REGION) {
		dev_err(gc->dev, "Failed to create DMA region: 0x%x\n",
			resp.hdr.status);
		err = -EPROTO;
		goto out;
	}

	gmi->dma_region_handle = resp.dma_region_handle;
	dev_dbg(gc->dev, "Created DMA region handle 0x%llx\n",
		gmi->dma_region_handle);
out:
	if (err)
		dev_dbg(gc->dev,
			"Failed to create DMA region of length: %u, page_type: %d, status: 0x%x, err: %d\n",
			length, req->gdma_page_type, resp.hdr.status, err);
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

	queue = kzalloc_obj(*queue);
	if (!queue)
		return -ENOMEM;

	gmi = &queue->mem_info;
	err = mana_gd_alloc_memory(gc, spec->queue_size, gmi, true);
	if (err) {
		dev_err(gc->dev, "GDMA queue type: %d, size: %u, gdma memory allocation err: %d\n",
			spec->type, spec->queue_size, err);
		goto free_q;
	}

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
	dev_err(gc->dev, "Failed to create queue type %d of size: %u, err: %d\n",
		spec->type, spec->queue_size, err);
	mana_gd_free_memory(gmi);
free_q:
	kfree(queue);
	return err;
}
EXPORT_SYMBOL_NS(mana_gd_create_mana_eq, "NET_MANA");

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

	queue = kzalloc_obj(*queue);
	if (!queue)
		return -ENOMEM;

	queue->id = INVALID_QUEUE_ID;

	gmi = &queue->mem_info;
	err = mana_gd_alloc_memory(gc, spec->queue_size, gmi, true);
	if (err) {
		dev_err(gc->dev, "GDMA queue type: %d, size: %u, memory allocation err: %d\n",
			spec->type, spec->queue_size, err);
		goto free_q;
	}

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
	dev_err(gc->dev, "Failed to create queue type %d of size: %u, err: %d\n",
		spec->type, spec->queue_size, err);
	mana_gd_free_memory(gmi);
free_q:
	kfree(queue);
	return err;
}
EXPORT_SYMBOL_NS(mana_gd_create_mana_wq_cq, "NET_MANA");

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

	mana_gd_destroy_dma_region(gc, gmi->dma_region_handle);
	mana_gd_free_memory(gmi);
	kfree(queue);
}
EXPORT_SYMBOL_NS(mana_gd_destroy_queue, "NET_MANA");

int mana_gd_verify_vf_version(struct gdma_context *gc)
{
	struct gdma_verify_ver_resp resp = {};
	struct gdma_verify_ver_req req = {};
	struct hw_channel_context *hwc;
	int err;

	hwc = gc->hwc.driver_data;
	mana_gd_init_req_hdr(&req.hdr, GDMA_VERIFY_VF_DRIVER_VERSION,
			     sizeof(req), sizeof(resp));

	req.protocol_ver_min = GDMA_PROTOCOL_FIRST;
	req.protocol_ver_max = GDMA_PROTOCOL_LAST;

	req.gd_drv_cap_flags1 = GDMA_DRV_CAP_FLAGS1;
	if (gc->bus_ops)
		req.gd_drv_cap_flags1 |= gc->bus_ops->drv_cap_flags1;
	req.gd_drv_cap_flags2 = GDMA_DRV_CAP_FLAGS2;
	req.gd_drv_cap_flags3 = GDMA_DRV_CAP_FLAGS3;
	req.gd_drv_cap_flags4 = GDMA_DRV_CAP_FLAGS4;

	req.drv_ver = 0;	/* Unused*/
	req.os_type = 0x10;	/* Linux */
	req.os_ver_major = LINUX_VERSION_MAJOR;
	req.os_ver_minor = LINUX_VERSION_PATCHLEVEL;
	req.os_ver_build = LINUX_VERSION_SUBLEVEL;
	strscpy(req.os_ver_str1, utsname()->sysname, sizeof(req.os_ver_str1));
	strscpy(req.os_ver_str2, utsname()->release, sizeof(req.os_ver_str2));
	strscpy(req.os_ver_str3, utsname()->version, sizeof(req.os_ver_str3));

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		dev_err(gc->dev, "VfVerifyVersionOutput: %d, status=0x%x\n",
			err, resp.hdr.status);
		return err ? err : -EPROTO;
	}
	gc->pf_cap_flags1 = resp.pf_cap_flags1;
	gc->gdma_protocol_ver = resp.gdma_protocol_ver;

	debugfs_create_x64("gdma_protocol_ver", 0400, gc->mana_pci_debugfs,
			   &gc->gdma_protocol_ver);
	debugfs_create_x64("pf_cap_flags1", 0400, gc->mana_pci_debugfs,
			   &gc->pf_cap_flags1);

	if (resp.pf_cap_flags1 & GDMA_DRV_CAP_FLAG_1_HWC_TIMEOUT_RECONFIG) {
		err = mana_gd_query_hwc_timeout(gc, &hwc->hwc_timeout);
		if (err) {
			dev_err(gc->dev, "Failed to set the hwc timeout %d\n", err);
			return err;
		}
		dev_dbg(gc->dev, "set the hwc timeout to %u\n", hwc->hwc_timeout);
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

	/* Validate that doorbell page for db_id is within the BAR0 region.
	 * In mana_gd_ring_doorbell(), the address is calculated as:
	 *   addr = db_page_base + db_page_size * db_id
	 *        = (bar0_va + db_page_off) + (db_page_size * db_id)
	 * So we need: db_page_off + db_page_size * (db_id + 1) <= bar0_size
	 */
	if (gc->db_page_off + gc->db_page_size * ((u64)resp.db_id + 1) > gc->bar0_size) {
		dev_err(gc->dev, "Doorbell ID %u out of range\n", resp.db_id);
		return -EPROTO;
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
		if (mana_need_log(gc, err))
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

	return mana_gd_ring_ptr(wq, offset);
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
		WARN_ON_ONCE(wqe_req->num_sge < 2);

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

static void mana_gd_write_sgl(struct gdma_queue *wq, u32 sgl_offset,
			      const struct gdma_wqe_request *wqe_req)
{
	u32 size_to_end = mana_gd_ring_contig_avail(wq, sgl_offset);
	u32 sgl_size = sizeof(struct gdma_sge) * wqe_req->num_sge;
	const u8 *address = (u8 *)wqe_req->sgl;

	if (size_to_end < sgl_size) {
		memcpy(mana_gd_ring_ptr(wq, sgl_offset), address, size_to_end);

		address += size_to_end;
		sgl_size -= size_to_end;
		sgl_offset += size_to_end;
		if (sgl_offset == wq->queue_size)
			sgl_offset = 0;
	}

	memcpy(mana_gd_ring_ptr(wq, sgl_offset), address, sgl_size);
}

static void mana_gd_write_ib_sgl(struct gdma_queue *wq, u32 sgl_offset,
				 const struct gdma_wqe_request *wqe_req)
{
	const struct ib_sge *sge = wqe_req->ib_sgl;
	struct gdma_sge *gdma_sgl;
	u32 i;

	for (i = 0; i < wqe_req->num_sge; ++i, ++sge) {
		gdma_sgl = mana_gd_ring_ptr(wq, sgl_offset);
		gdma_sgl->address = sge->addr;
		gdma_sgl->size = sge->length;
		gdma_sgl->mem_key = sge->lkey;
		sgl_offset += sizeof(*gdma_sgl);
		if (sgl_offset == wq->queue_size)
			sgl_offset = 0;
	}
}

int mana_gd_post_work_request(struct gdma_queue *wq,
			      const struct gdma_wqe_request *wqe_req,
			      struct gdma_posted_wqe_info *wqe_info)
{
	u32 client_oob_size = wqe_req->inline_oob_size;
	u32 sgl_data_size;
	u32 max_wqe_size;
	u32 wqe_offset;
	u32 sgl_offset;
	u32 wqe_size;
	u32 oob_len;
	u8 *wqe_ptr;
	u32 head;

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

	if (wq->monitor_avl_buf && wqe_size > mana_gd_wq_avail_space(wq))
		return -ENOSPC;

	head = wq->head;
	if (wqe_info) {
		wqe_info->wqe_size_in_bu = wqe_size / GDMA_WQE_BU_SIZE;
		wqe_info->wqe_offset = head;
	}

	wqe_offset = (head * GDMA_WQE_BU_SIZE) & (wq->queue_size - 1);
	wqe_ptr = mana_gd_get_wqe_ptr(wq, head);
	oob_len = mana_gd_write_client_oob(wqe_req, wq->type, client_oob_size,
					   sgl_data_size, wqe_ptr);

	sgl_offset = wqe_offset + oob_len;
	if (sgl_offset >= wq->queue_size)
		sgl_offset -= wq->queue_size;

	if (wqe_req->flags & GDMA_WR_IB_SGL)
		mana_gd_write_ib_sgl(wq, sgl_offset, wqe_req);
	else
		mana_gd_write_sgl(wq, sgl_offset, wqe_req);

	wq->head += wqe_size / GDMA_WQE_BU_SIZE;

	return 0;
}
EXPORT_SYMBOL_NS(mana_gd_post_work_request, "NET_MANA");

int mana_gd_post_and_ring(struct gdma_queue *queue,
			  const struct gdma_wqe_request *wqe_req,
			  struct gdma_posted_wqe_info *wqe_info)
{
	struct gdma_context *gc = queue->gdma_dev->gdma_context;
	int err;

	err = mana_gd_post_work_request(queue, wqe_req, wqe_info);
	if (err) {
		dev_err(gc->dev, "Failed to post work req from queue type %d of size %u (err=%d)\n",
			queue->type, queue->queue_size, err);
		return err;
	}

	mana_gd_wq_ring_doorbell(gc, queue);

	return 0;
}

static int mana_gd_read_cqe(struct gdma_queue *cq, struct gdma_comp *comp)
{
	unsigned int num_cqe = cq->queue_size / sizeof(struct gdma_cqe);
	u32 owner_bits, new_bits, old_bits;
	struct gdma_cqe *cqe;

	cqe = mana_gd_ring_ptr(cq, (cq->head % num_cqe) * sizeof(*cqe));
	owner_bits = cqe->cqe_info.owner_bits;

	old_bits = (cq->head / num_cqe - 1) & GDMA_CQE_OWNER_MASK;
	/* Return 0 if no more entries. */
	if (owner_bits == old_bits)
		return 0;

	new_bits = (cq->head / num_cqe) & GDMA_CQE_OWNER_MASK;
	/* Return -1 if overflow detected. */
	if (WARN_ON_ONCE(owner_bits != new_bits))
		return -1;

	/* Per GDMA spec, rmb is necessary after checking owner_bits, before
	 * reading completion info
	 */
	rmb();

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
EXPORT_SYMBOL_NS(mana_gd_poll_cq, "NET_MANA");

irqreturn_t mana_gd_intr(int irq, void *arg)
{
	struct gdma_irq_context *gic = arg;
	struct list_head *eq_list = &gic->eq_list;
	struct gdma_queue *eq;

	rcu_read_lock();
	list_for_each_entry_rcu(eq, eq_list, entry) {
		gic->handler(eq);
	}
	rcu_read_unlock();

	return IRQ_HANDLED;
}

/*
 * Reset the device using whatever mechanism the bus provides.
 */
int mana_gd_dev_reset(struct gdma_context *gc)
{
	if (!gc->bus_ops || !gc->bus_ops->dev_reset)
		return -EOPNOTSUPP;

	return gc->bus_ops->dev_reset(gc);
}

/*
 * Release a reference on the IRQ context backing an MSI vector, freeing
 * the vector once the last user is gone.
 */
void mana_gd_put_gic(struct gdma_context *gc, bool use_msi_bitmap, int msi)
{
	const struct gdma_bus_ops *ops = gc->bus_ops;
	struct gdma_irq_context *gic;
	int irq;

	mutex_lock(&gc->gic_mutex);

	gic = xa_load(&gc->irq_contexts, msi);
	if (WARN_ON(!gic)) {
		mutex_unlock(&gc->gic_mutex);
		return;
	}

	if (use_msi_bitmap)
		gic->bitmap_refs--;

	if (use_msi_bitmap && gic->bitmap_refs == 0)
		clear_bit(msi, gc->msi_bitmap);

	if (!refcount_dec_and_test(&gic->refcount))
		goto out;

	irq = gic->irq;

	irq_update_affinity_hint(irq, NULL);
	free_irq(irq, gic);

	if (gic->dyn_msix)
		ops->msix_free(gc, msi, irq);

	xa_erase(&gc->irq_contexts, msi);
	kfree(gic);

out:
	mutex_unlock(&gc->gic_mutex);
}
EXPORT_SYMBOL_NS(mana_gd_put_gic, "NET_MANA");

/*
 * Get a GIC (GDMA IRQ Context) on a MSI vector
 * a MSI can be shared between different EQs, this function supports setting
 * up separate MSIs using a bitmap, or directly using the MSI index
 *
 * @use_msi_bitmap:
 * True if MSI is assigned by this function on available slots from bitmap.
 * False if MSI is passed from *msi_requested
 */
struct gdma_irq_context *mana_gd_get_gic(struct gdma_context *gc,
					 bool use_msi_bitmap,
					 int *msi_requested)
{
	const struct gdma_bus_ops *ops = gc->bus_ops;
	struct gdma_irq_context *gic;
	bool dyn_msix = false;
	int msi, irq, err;

	mutex_lock(&gc->gic_mutex);

	if (use_msi_bitmap) {
		msi = find_first_zero_bit(gc->msi_bitmap, gc->num_msix_usable);
		if (msi >= gc->num_msix_usable) {
			dev_err(gc->dev, "No free MSI vectors available\n");
			gic = ERR_PTR(-ENOSPC);
			goto out;
		}
		*msi_requested = msi;
	} else {
		msi = *msi_requested;
	}

	gic = xa_load(&gc->irq_contexts, msi);
	if (gic) {
		refcount_inc(&gic->refcount);
		if (use_msi_bitmap) {
			gic->bitmap_refs++;
			set_bit(msi, gc->msi_bitmap);
		}
		goto out;
	}

	irq = ops->msix_virq(gc, msi);
	if (irq == -EINVAL) {
		/* A bus that sizes its vector pool up front has nothing left
		 * to hand out once every vector has been claimed.
		 */
		if (!ops->msix_alloc_at) {
			dev_err(gc->dev, "No IRQ for MSI %d\n", msi);
			gic = ERR_PTR(-ENOENT);
			goto out;
		}

		irq = ops->msix_alloc_at(gc, &msi);
		if (irq < 0) {
			dev_err(gc->dev, "Failed to alloc irq msi %d err %d\n",
				*msi_requested, irq);
			gic = ERR_PTR(irq);
			goto out;
		}

		dyn_msix = true;
		*msi_requested = msi;
	}

	gic = kzalloc_obj(*gic);
	if (!gic) {
		gic = ERR_PTR(-ENOMEM);
		if (dyn_msix)
			ops->msix_free(gc, msi, irq);
		goto out;
	}

	gic->handler = mana_gd_process_eq_events;
	gic->msi = msi;
	gic->irq = irq;
	INIT_LIST_HEAD(&gic->eq_list);
	spin_lock_init(&gic->lock);

	if (!gic->msi)
		snprintf(gic->name, MANA_IRQ_NAME_SZ, "mana_hwc@%s:%s",
			 ops->bus_name, dev_name(gc->dev));
	else
		snprintf(gic->name, MANA_IRQ_NAME_SZ, "mana_msi%d@%s:%s",
			 gic->msi, ops->bus_name, dev_name(gc->dev));

	err = request_irq(irq, mana_gd_intr, 0, gic->name, gic);
	if (err) {
		dev_err(gc->dev, "Failed to request irq %d %s\n",
			irq, gic->name);
		kfree(gic);
		gic = ERR_PTR(err);
		if (dyn_msix)
			ops->msix_free(gc, msi, irq);
		goto out;
	}

	gic->dyn_msix = dyn_msix;
	refcount_set(&gic->refcount, 1);
	gic->bitmap_refs = use_msi_bitmap ? 1 : 0;

	err = xa_err(xa_store(&gc->irq_contexts, msi, gic, GFP_KERNEL));
	if (err) {
		dev_err(gc->dev, "Failed to store irq context for msi %d: %d\n",
			msi, err);
		free_irq(irq, gic);
		kfree(gic);
		gic = ERR_PTR(err);
		if (dyn_msix)
			ops->msix_free(gc, msi, irq);
		goto out;
	}

	if (use_msi_bitmap)
		set_bit(msi, gc->msi_bitmap);

out:
	mutex_unlock(&gc->gic_mutex);
	return gic;
}
EXPORT_SYMBOL_NS(mana_gd_get_gic, "NET_MANA");

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

/* Bring up the GDMA context on a probed device: create the debugfs directory,
 * map the shared-memory registers, start the hardware channel and size the
 * interrupt pool. Every step here is common to all buses; the ones that are
 * not are reached through gdma_bus_ops.
 */
int mana_gd_setup(struct gdma_context *gc)
{
	int err;

	gc->mana_pci_debugfs = debugfs_create_dir(dev_name(gc->dev),
						  mana_debugfs_root);

	err = mana_gd_init_registers(gc);
	if (err)
		goto remove_debugfs;

	mana_smc_init(&gc->shm_channel, gc->dev, gc->shm_base);

	err = mana_gd_alloc_service_wq(gc);
	if (err)
		goto remove_debugfs;

	err = gc->bus_ops->setup_hwc_irqs(gc);
	if (err) {
		dev_err(gc->dev, "Failed to setup IRQs for HWC creation: %d\n",
			err);
		goto free_workqueue;
	}

	err = mana_hwc_create_channel(gc);
	if (err)
		goto remove_irq;

	err = mana_gd_verify_vf_version(gc);
	if (err)
		goto destroy_hwc;

	err = mana_gd_detect_devices(gc);
	if (err)
		goto destroy_hwc;

	err = mana_gd_query_max_resources(gc);
	if (err)
		goto destroy_hwc;

	err = gc->bus_ops->setup_remaining_irqs(gc);
	if (err)
		goto destroy_hwc;

	dev_dbg(gc->dev, "mana gdma setup successful\n");
	return 0;

destroy_hwc:
	mana_hwc_destroy_channel(gc);
remove_irq:
	gc->bus_ops->remove_irqs(gc);
free_workqueue:
	mana_gd_free_service_wq(gc);
remove_debugfs:
	debugfs_remove_recursive(gc->mana_pci_debugfs);
	gc->mana_pci_debugfs = NULL;
	dev_err(gc->dev, "%s failed (error %d)\n", __func__, err);
	return err;
}
EXPORT_SYMBOL_NS(mana_gd_setup, "NET_MANA");

void mana_gd_cleanup(struct gdma_context *gc)
{
	mana_hwc_destroy_channel(gc);

	gc->bus_ops->remove_irqs(gc);

	mana_gd_free_service_wq(gc);

	debugfs_remove_recursive(gc->mana_pci_debugfs);
	gc->mana_pci_debugfs = NULL;

	dev_dbg(gc->dev, "mana gdma cleanup successful\n");
}
EXPORT_SYMBOL_NS(mana_gd_cleanup, "NET_MANA");

static int __init gdma_core_init(void)
{
	mana_debugfs_root = debugfs_create_dir("mana", NULL);

	return 0;
}

static void __exit gdma_core_exit(void)
{
	debugfs_remove(mana_debugfs_root);

	mana_debugfs_root = NULL;
}

module_init(gdma_core_init);
module_exit(gdma_core_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Microsoft Azure Network Adapter GDMA core");
