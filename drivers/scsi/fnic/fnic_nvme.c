// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */
#include <linux/mempool.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/scatterlist.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/nvme.h>
#include <linux/nvme-fc.h>
#include "fnic.h"
#include "fnic_trace.h"
#include "fdls_fc.h"

#if IS_REACHABLE(CONFIG_NVME_FC)

static bool nvfnic_ls_req_cleanup(struct fnic_iport_s *iport,
				  struct nvmefc_ls_req *lsreq,
				  uint16_t oxid);

int nvfnic_get_sg_count(struct fnic_io_req *io_req)
{
	return io_req->fcp_req->sg_cnt;
}

int nvfnic_dma_map_sgl(struct fnic *fnic, struct fnic_io_req *io_req,
		       int sg_count)
{
	io_req->sgl_list_pa = dma_map_single(&fnic->pdev->dev,
					     io_req->sgl_list,
					     sizeof(io_req->sgl_list[0]) *
					     sg_count, DMA_TO_DEVICE);
	if (dma_mapping_error(&fnic->pdev->dev, io_req->sgl_list_pa)) {
		dev_err(&fnic->pdev->dev, "DMA mapping failed\n");
		io_req->sgl_list_pa = 0;
		io_req->sgl_mapped = 0;
		return -EBUSY;
	}

	io_req->sgl_mapped = 1;
	return 0;
}

void nvfnic_dma_unmap_sgl(struct fnic *fnic, struct fnic_io_req *io_req)
{
	if (io_req->sgl_mapped) {
		dma_unmap_single(&fnic->pdev->dev, io_req->sgl_list_pa,
				 sizeof(io_req->sgl_list[0]) * io_req->sgl_cnt,
				 DMA_TO_DEVICE);
		io_req->sgl_mapped = 0;
		io_req->sgl_list_pa = 0;
	}
}

static void nvfnic_update_io_bytes(struct fnic *fnic,
				   struct fnic_io_req *io_req, u8 opcode)
{
	if (opcode == nvme_cmd_read)
		fnic->fcp_input_bytes += io_req->fcp_req->transferred_length;
	else if (opcode == nvme_cmd_write)
		fnic->fcp_output_bytes += io_req->fcp_req->transferred_length;
}

static void nvfnic_update_io_stats(struct fnic *fnic, u8 opcode)
{
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;

	switch (opcode) {
	case nvme_cmd_read:
		atomic64_inc(&fnic_stats->nvme_stats.nvme_input_requests);
		break;
	case nvme_cmd_write:
		atomic64_inc(&fnic_stats->nvme_stats.nvme_output_requests);
		break;
	default:
		atomic64_inc(&fnic_stats->nvme_stats.nvme_control_requests);
		break;
	}
}

static void nvfnic_update_cmpl_stats(struct fnic *fnic,
				     struct fnic_io_req *io_req)
{
	struct io_path_stats *io_stats = &fnic->fnic_stats.io_stats;
	atomic64_t *duration_stat;
	unsigned long io_duration_time;

	atomic64_dec(&io_stats->active_ios);
	if (atomic64_read(&fnic->io_cmpl_skip))
		atomic64_dec(&fnic->io_cmpl_skip);
	else
		atomic64_inc(&io_stats->io_completions);

	io_duration_time = jiffies_to_msecs(jiffies - io_req->start_time);

	if (io_duration_time <= 10)
		duration_stat = &io_stats->io_btw_0_to_10_msec;
	else if (io_duration_time <= 100)
		duration_stat = &io_stats->io_btw_10_to_100_msec;
	else if (io_duration_time <= 500)
		duration_stat = &io_stats->io_btw_100_to_500_msec;
	else if (io_duration_time <= 5000)
		duration_stat = &io_stats->io_btw_500_to_5000_msec;
	else if (io_duration_time <= 10000)
		duration_stat = &io_stats->io_btw_5000_to_10000_msec;
	else if (io_duration_time <= 30000)
		duration_stat = &io_stats->io_btw_10000_to_30000_msec;
	else {
		duration_stat = &io_stats->io_greater_than_30000_msec;
		if (io_duration_time >
		    atomic64_read(&io_stats->current_max_io_time))
			atomic64_set(&io_stats->current_max_io_time,
				     io_duration_time);
	}

	atomic64_inc(duration_stat);
}

int
nvfnic_alloc_fcpio_tag(struct fnic_iport_s *iport, struct fnic_io_req *io_req)
{
	struct fnic *fnic = iport->fnic;
	int tag;

	tag = sbitmap_get(&fnic->nvfnic_tag_map);
	if (tag >= 0) {
		WRITE_ONCE(io_req->tag, tag);
		fnic->sw_copy_wq[0].io_req_table[tag] = io_req;
		return tag;
	}
	return FNIC_NVME_NO_FREE_TAG;
}

void
nvfnic_free_fcpio_tag(struct fnic_iport_s *iport, struct fnic_io_req *io_req)
{
	struct fnic *fnic = iport->fnic;
	uint16_t tag = io_req->tag;

	if (tag == FNIC_NVME_NO_FREE_TAG) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			    "Attempting to free invalid tag: 0x%x\n", tag);
		return;
	}
	fnic->sw_copy_wq[0].io_req_table[tag] = NULL;
	WRITE_ONCE(io_req->tag, FNIC_NVME_NO_FREE_TAG);
	sbitmap_clear_bit(&fnic->nvfnic_tag_map, tag);
}

void
nvfnic_reset_fcpio_tag_pool(struct fnic_iport_s *iport)
{
	WARN_ON(sbitmap_weight(&iport->fnic->nvfnic_tag_map));
}

struct fnic_io_req *
nvfnic_find_io_req_by_tag(struct fnic *fnic, uint16_t tag)
{
	if (tag == FNIC_NVME_NO_FREE_TAG ||
		!sbitmap_test_bit(&fnic->nvfnic_tag_map, tag))
		return NULL;
	return fnic->sw_copy_wq[0].io_req_table[tag];
}

/*
 * Unmap the data buffer and sense buffer for an io_req,
 * also unmap and free the device-private scatter/gather list.
 */
void nvfnic_release_nvme_ioreq_buf(struct fnic_iport_s *iport,
					struct fnic_io_req *io_req)
{
	struct fnic *fnic = iport->fnic;

	nvfnic_dma_unmap_sgl(fnic, io_req);

	if (io_req->sgl_cnt)
		mempool_free(io_req->sgl_list_alloc,
			     fnic->io_sgl_pool[io_req->sgl_type]);
}

int nvfnic_get_nvmef_info(struct fnic *fnic, struct fnic_nvmef_info *info)
{
	int len = 0;
	struct fnic_iport_s *iport = &fnic->iport;
	int buf_size = info->buf_size;
	struct fnic_tport_s *tport;
	struct fnic_tport_s *next;
	unsigned long flags;

	if (buf_size <= 0)
		return 0;

	len += scnprintf(info->info_buffer + len, buf_size - len,
			 "lport wwpn 0x%llx wwnn 0x%llx fcid 0x%06x\n",
			 iport->wwpn, iport->wwnn, iport->fcid);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	list_for_each_entry_safe(tport, next, &iport->tport_list, links) {
		if (len >= buf_size - 1)
			break;

		len += scnprintf(info->info_buffer + len, buf_size - len,
				 "tport wwpn 0x%llx wwnn 0x%llx fcid 0x%06x\n",
				 tport->wwpn, tport->wwnn, tport->fcid);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	return len;
}

inline int nvfnic_queue_wq_nvme_copy_desc(struct fnic *fnic,
					       struct vnic_wq_copy *wq,
					       struct fnic_io_req *io_req,
					       int sg_count)
{
	struct scatterlist *sg;
	struct fnic_tport_s *tport = io_req->tport;
	struct host_sg_desc *desc;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	unsigned int i;
	unsigned long intr_flags;
	int flags;
	u8 exch_flags;
	struct scatterlist *sgl;
	int idx;
	int ret = 0;

	if (sg_count) {
		/* For each SGE, create a device desc entry */
		desc = io_req->sgl_list;
		sgl = io_req->fcp_req->first_sgl;
		for_each_sg(sgl, sg, sg_count, i) {
			desc->addr = cpu_to_le64(sg_dma_address(sg));
			desc->len = cpu_to_le32(sg_dma_len(sg));
			desc->_resvd = 0;
			desc++;
		}

		ret = nvfnic_dma_map_sgl(fnic, io_req, sg_count);
		if (ret)
			return ret;
	}

	idx = (struct vnic_wq_copy *)wq - &fnic->hw_copy_wq[0];

	/* Enqueue the descriptor in the Copy WQ */
	spin_lock_irqsave(&fnic->wq_copy_lock[idx], intr_flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[idx])
		free_wq_copy_descs(fnic, wq, idx);

	if (unlikely(!vnic_wq_copy_desc_avail(wq))) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[idx], intr_flags);
		FNIC_NVME_DBG(KERN_ERR, fnic,
			    "Enqueue failure: No descriptors\n");
		atomic64_inc(&misc_stats->io_cpwq_alloc_failures);
		return -EBUSY;
	}

	flags = 0;
	if (io_req->fcp_req->io_dir == NVMEFC_FCP_READ)
		flags = FCPIO_ICMND_RDDATA;
	else if (io_req->fcp_req->io_dir == NVMEFC_FCP_WRITE)
		flags = FCPIO_ICMND_WRDATA;

	exch_flags = 0;

	fnic_queue_wq_copy_desc_nvme_io(wq, io_req->tag,
					exch_flags, io_req->sgl_cnt,
					io_req->sgl_list_pa, flags,
					io_req->fcp_req->cmdaddr,
					io_req->fcp_req->cmdlen,
					io_req->fcp_req->payload_length,
					io_req->port_id,
					tport->max_payload_size, tport->r_a_tov,
					tport->e_d_tov);

	atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
	if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
	    atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
		atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
		     atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

	spin_unlock_irqrestore(&fnic->wq_copy_lock[idx], intr_flags);
	return 0;
}

bool
nvfnic_transport_ready(struct fnic_iport_s *iport, struct fnic_tport_s *tport)
{
	struct fnic *fnic = iport->fnic;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;

	if (tport == NULL)
		return false;

	if (fdls_get_state(&iport->fabric) == FDLS_STATE_LINKDOWN ||
	    iport->state != FNIC_IPORT_STATE_READY) {
		atomic64_inc(&fnic_stats->misc_stats.iport_not_ready);
		return false;
	}

	if (unlikely(fnic_chk_state_flags_locked(fnic, FNIC_FLAGS_IO_BLOCKED)))
		return false;

	if (fdls_tport_is_offline(tport)) {
		atomic64_inc(&fnic_stats->misc_stats.tport_not_ready);
		return false;
	}

	return true;
}

int nvfnic_queuecommand(struct fnic_io_req *io_req)
{
	struct fnic_iport_s *iport = io_req->iport;
	struct fnic *fnic = iport->fnic;
	struct fnic_tport_s *tport = io_req->tport;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	struct vnic_wq_copy *wq = io_req->wq;
	int ret = 0;
	int sg_count = 0;
	unsigned long ptr;
	unsigned char *lba;
	u64 cmd_trace;
	int idx;
	struct nvme_fc_cmd_iu *cmdiu = io_req->fcp_req->cmdaddr;

	io_req->cmd_state = FNIC_IOREQ_NOT_INITED;
	io_req->cmd_flags = FNIC_NO_FLAGS;
	/* Map the data buffer */
	sg_count = nvfnic_get_sg_count(io_req);
	if (sg_count < 0) {
		FNIC_TRACE(nvfnic_queuecommand, fnic->fnic_num,
			   io_req->tag, io_req, 0, io_req->fcp_req->io_dir,
			   sg_count, io_req->cmd_state);
		FNIC_NVME_DBG(KERN_INFO, fnic, "sg count is less-than-zero\n");
		ret = -1;
		goto out;
	}

	/* Determine the type of scatter/gather list we need */
	io_req->sgl_cnt = sg_count;
	io_req->sgl_type = FNIC_SGL_CACHE_DFLT;
	if (sg_count > FNIC_DFLT_SG_DESC_CNT)
		io_req->sgl_type = FNIC_SGL_CACHE_MAX;

	if (sg_count) {
		io_req->sgl_list =
		    mempool_alloc(fnic->io_sgl_pool[io_req->sgl_type],
				  GFP_ATOMIC);
		if (!io_req->sgl_list) {
			atomic64_inc(&fnic_stats->io_stats.alloc_failures);
			FNIC_NVME_DBG(KERN_INFO, fnic,
				      "Unable to alloc SGLs\n");
			ret = -ENOMEM;
			goto out;
		}

		/* Cache sgl list allocated address before alignment */
		io_req->sgl_list_alloc = io_req->sgl_list;
		ptr = (unsigned long)io_req->sgl_list;
		if (ptr % FNIC_SG_DESC_ALIGN) {
			io_req->sgl_list = (struct host_sg_desc *)
			    (((unsigned long)ptr + FNIC_SG_DESC_ALIGN - 1)
			     & ~(FNIC_SG_DESC_ALIGN - 1));
		}
	}

	io_req->port_id = tport->fcid;
	io_req->start_time = jiffies;
	io_req->cmd_state = FNIC_IOREQ_CMD_PENDING;
	io_req->cmd_flags = FNIC_IO_INITIALIZED;

	/* create copy wq desc and enqueue it */
	idx = wq - &fnic->hw_copy_wq[0];
	atomic64_inc(&fnic_stats->io_stats.ios[idx]);
	ret = nvfnic_queue_wq_nvme_copy_desc(fnic, io_req->wq, io_req, sg_count);
	if (ret) {
		FNIC_NVME_DBG(KERN_ERR, fnic, "Unable to queue frame\n");
		/*
		 * In case another thread cancelled the request,
		 * refetch the pointer under the lock.
		 */
		nvfnic_release_nvme_ioreq_buf(iport, io_req);
		FNIC_TRACE(nvfnic_queuecommand, fnic->fnic_num,
			   io_req->tag, io_req->fcp_req, 0, 0, 0,
			   (((u64)io_req->cmd_flags << 32) | io_req->cmd_state));
		return ret;
	}

	atomic64_inc(&fnic_stats->io_stats.active_ios);
	atomic64_inc(&fnic_stats->io_stats.num_ios);
	if (atomic64_read(&fnic_stats->io_stats.active_ios) >
	    atomic64_read(&fnic_stats->io_stats.max_active_ios))
		atomic64_set(&fnic_stats->io_stats.max_active_ios,
		     atomic64_read(&fnic_stats->io_stats.active_ios));
	io_req->cmd_flags |= FNIC_IO_ISSUED;
 out:
	lba = (char *)&cmdiu->sqe.rw.slba;
	cmd_trace = ((u64) cmdiu->sqe.rw.opcode << 56 | (u64) lba[4] << 40 |
		     (u64) lba[5] << 32 | (u64) lba[0] << 24 |
		     (u64) lba[1] << 16 | (u64) lba[2] << 8 | lba[3]);

	FNIC_TRACE(nvfnic_queuecommand, fnic->fnic_num,
		   io_req->tag, 0, io_req,
		   sg_count, cmd_trace,
		   (((u64)io_req->cmd_flags >> 32) |
		    io_req->cmd_state));

	return ret;
}

int nvfnic_fcpio_send(struct nvme_fc_local_port *lport,
		  struct nvme_fc_remote_port *rport, void *hw_queue_handle,
		  struct nvmefc_fcp_req *fcp_req)
{
	struct fnic_iport_s *iport = lport->private;
	struct fnic_io_req *io_req;
	int ret;
	struct fnic *fnic = iport->fnic;
	unsigned long flags = 0;
	struct fnic_tport_s *tport;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	tport = (struct fnic_tport_s *)rport->private;
	atomic64_inc(&fnic_stats->io_stats.nvme_io_reqs_rcvd);
	if (!nvfnic_transport_ready(iport, tport)) {
		atomic64_inc(&fnic_stats->io_stats.nvme_io_rsps_sent);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		if (tport != NULL)
			FNIC_NVME_DBG(KERN_INFO, fnic,
				      "iport: 0x%x tport: 0x%x not ready\n",
				      iport->fcid, tport->fcid);
		else
			FNIC_NVME_DBG(KERN_INFO, fnic,
				      "iport: 0x%x tport not ready\n",
				      iport->fcid);
		return -ENODEV;
	}
	atomic_inc(&fnic->in_flight);

	io_req = (struct fnic_io_req *) fcp_req->private;
	io_req->iport = iport;
	io_req->tport = (struct fnic_tport_s *)rport->private;
	io_req->fcp_req = fcp_req;
	io_req->done = nvfnic_fcpio_cmpl;
	io_req->sgl_list = NULL;
	io_req->sgl_list_alloc = NULL;
	io_req->sgl_list_pa = 0;
	io_req->sgl_cnt = 0;
	io_req->sgl_mapped = 0;
	io_req->wq = hw_queue_handle;
	init_llist_node(&io_req->nvfnic_io_cmpl);

	io_req->tag = nvfnic_alloc_fcpio_tag(iport, io_req);
	if (io_req->tag == FNIC_NVME_NO_FREE_TAG) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			    "No free tag available. Failing IO\n");
		atomic64_inc(&fnic_stats->io_stats.alloc_failures);
		atomic_dec(&fnic->in_flight);
		atomic64_inc(&fnic_stats->io_stats.nvme_io_rsps_sent);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return -EBUSY;
	}

	ret = nvfnic_queuecommand(io_req);
	if (ret) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "Queuecommand failed tag: 0x%x\n",
			      io_req->tag);
		nvfnic_free_fcpio_tag(iport, io_req);
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	atomic_dec(&fnic->in_flight);
	return ret;
}

void nvfnic_fcpio_nvme_fast_cmpl_handler(struct fnic *fnic,
				       struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag ftag;
	u32 id;
	struct fnic_io_req *io_req;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	unsigned long start_time;
	u64 cmd_trace;
	char *lba;
	struct nvme_fc_cmd_iu *cmdiu;
	struct fnic_iport_s *iport;
	unsigned int tag;

	/* Decode the cmpl description to get the io_req id */
	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &ftag);
	fcpio_tag_id_dec(&ftag, &id);
	tag = id & FNIC_TAG_MASK;

	if (tag >= fnic->fnic_max_tag_id) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			    "Tag out of range tag: 0x%x hdr status: %s\n", tag,
			    fnic_fcpio_status_to_str(hdr_status));
		return;
	}
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	io_req = nvfnic_find_io_req_by_tag(fnic, tag);

	WARN_ON_ONCE(!io_req);
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.ioreq_null);
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "IO req null hdr: %s tag: 0x%x desc: 0x%p\n",
			      fnic_fcpio_status_to_str(hdr_status), id, desc);
		FNIC_NVME_DBG(KERN_ERR, fnic,
			    "type: 0x%x status: 0x%x rsvd: 0x%x tag: 0x%x\n",
			    desc->hdr.type, desc->hdr.status, desc->hdr._resvd,
			    id);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	cmdiu = io_req->fcp_req->cmdaddr;
	if (io_req->tag != tag) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "Tag mismatch tag:%d io:0x%x id:0x%x csn:%08x\n",
			      tag, io_req->tag, id, be32_to_cpu(cmdiu->csn));
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}
	iport = io_req->iport;

	start_time = io_req->start_time;

	/* firmware completed the io */
	io_req->io_completed = 1;
	if (io_req->cmd_state == FNIC_IOREQ_ABTS_PENDING) {
		/*
		 * set the FNIC_IO_DONE so that this doesn't get
		 * flagged as 'out of order' if it was not aborted
		 */
		io_req->cmd_flags |= FNIC_IO_DONE;
		io_req->cmd_flags |= FNIC_IO_ABTS_PENDING;
		if (hdr_status == FCPIO_ABORTED)
			io_req->cmd_flags |= FNIC_IO_ABORTED;
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);

		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "icmnd abts hdr:%d %s tag:0x%x io:%p",
			      hdr_status, fnic_fcpio_status_to_str(hdr_status),
			      id, io_req);
		return;
	}

	if (io_req->cmd_state != FNIC_IOREQ_CMD_PENDING) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "IO freed id:%d tag:0x%x st:0x%x csn:%08x\n",
			      id, io_req->tag, io_req->cmd_state,
			      be32_to_cpu(cmdiu->csn));
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	/* Mark the IO as complete */
	io_req->cmd_state = FNIC_IOREQ_CMD_COMPLETE;
	switch (hdr_status) {
	case FCPIO_SUCCESS:
		io_req->fcp_req->status = 0;
		io_req->fcp_req->transferred_length =
		    io_req->fcp_req->payload_length;
		io_req->fcp_req->rcv_rsplen = 12;
		break;
	default:
		FNIC_NVME_DBG(KERN_ERR, fnic, "HDR status is non-zero\n");
		io_req->fcp_req->status = NVME_SC_INTERNAL;
		break;
	}

	if (hdr_status != FCPIO_SUCCESS) {
		atomic64_inc(&fnic_stats->io_stats.io_failures);
		FNIC_NVME_DBG(KERN_INFO, fnic, "hdr status: %s\n",
			    fnic_fcpio_status_to_str(hdr_status));
	}

	io_req->cmd_flags |= FNIC_IO_DONE;

	cmdiu = io_req->fcp_req->cmdaddr;
	lba = (char *)&cmdiu->sqe.rw.slba;
	cmd_trace = ((u64) hdr_status << 56) |
	    (u64) cmdiu->sqe.rw.opcode << 32 |
	    (u64) lba[0] << 24 | (u64) lba[1] << 16 |
	    (u64) lba[2] << 8 | lba[3];

	FNIC_TRACE(nvfnic_fcpio_nvme_fast_cmpl_handler, fnic->fnic_num,
			tag, io_req,
		   jiffies_to_msecs(jiffies - start_time),
		   desc, cmd_trace,
		   (((u64) io_req->cmd_flags << 32) |
		    io_req->cmd_state));

	nvfnic_update_io_stats(fnic, cmdiu->sqe.rw.opcode);
	nvfnic_update_io_bytes(fnic, io_req, cmdiu->sqe.rw.opcode);
	nvfnic_update_cmpl_stats(fnic, io_req);

	nvfnic_release_nvme_ioreq_buf(iport, io_req);
	if (io_req->done)
		io_req->done(io_req);

	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}

void nvfnic_fcpio_ersp_cmpl_handler(struct fnic *fnic,
				  struct fcpio_fw_req *desc, int sw_flag)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag ftag;
	u32 id;
	struct fcpio_nvme_cmpl *nvme_cmpl;
	struct fnic_io_req *io_req;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	unsigned long start_time;
	uint32_t rsplen;
	struct nvme_fc_ersp_iu *ersp;
	struct nvme_fc_ersp_iu *nrsp;
	struct nvme_fc_cmd_iu *cmdiu;
	struct nvme_command *sqe;
	struct nvme_completion *cqe;
	u64 cmd_trace;
	struct fnic_iport_s *iport;
	unsigned int tag;
	char *lba;
	uint64_t tport_wwpn = 0;

	/* Decode the cmpl description to get the io_req id */
	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &ftag);
	fcpio_tag_id_dec(&ftag, &id);
	nvme_cmpl = &desc->u.nvme_cmpl;
	ersp = (struct nvme_fc_ersp_iu *) nvme_cmpl->resp_bytes;
	tag = id & FNIC_TAG_MASK;

	if (tag >= fnic->fnic_max_tag_id) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "Tag out of range tag:0x%x hdr:%s\n", tag,
			      fnic_fcpio_status_to_str(hdr_status));
		return;
	}
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	io_req = nvfnic_find_io_req_by_tag(fnic, tag);
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.ioreq_null);
		FNIC_NVME_DBG(KERN_ERR, fnic,
			    "IOREQ is null hdr status: %s tag: 0x%x desc: %p\n",
			    fnic_fcpio_status_to_str(hdr_status), tag, desc);
		FNIC_NVME_DBG(KERN_ERR, fnic,
			    "type: 0x%x status: 0x%x rsvd: 0x%x\n",
			    desc->hdr.type, desc->hdr.status, desc->hdr._resvd);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}
	iport = io_req->iport;
	if (io_req->tport != NULL)
		tport_wwpn = io_req->tport->wwpn;
	cmdiu = io_req->fcp_req->cmdaddr;

	if (io_req->tag != tag) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "Tag mismatch io:0x%x tag:0x%x id:0x%x\n",
			      io_req->tag, tag, id);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	nrsp = (struct nvme_fc_ersp_iu *)io_req->fcp_req->rspaddr;
	cmdiu = (struct nvme_fc_cmd_iu *)io_req->fcp_req->cmdaddr;
	sqe = &cmdiu->sqe;
	cqe = &nrsp->cqe;
	start_time = io_req->start_time;

	/* firmware completed the io */
	io_req->io_completed = 1;

	if (io_req->cmd_state == FNIC_IOREQ_ABTS_PENDING) {
		/*
		 * set the FNIC_IO_DONE so that this doesn't get
		 * flagged as 'out of order' if it was not aborted
		 */
		io_req->cmd_flags |= FNIC_IO_DONE;
		io_req->cmd_flags |= FNIC_IO_ABTS_PENDING;
		if (hdr_status == FCPIO_ABORTED)
			io_req->cmd_flags |= FNIC_IO_ABORTED;

		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "ABTS pending hdr status: %s tag: 0x%x",
			      fnic_fcpio_status_to_str(hdr_status), tag);
		return;
	}

	if (io_req->cmd_state != FNIC_IOREQ_CMD_PENDING) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "IO already freed by abort. tag: 0x%x id: 0x%x\n",
			      io_req->tag, id);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	/* Mark the IO as complete */
	io_req->cmd_state = FNIC_IOREQ_CMD_COMPLETE;

	switch (hdr_status) {
	case FCPIO_SUCCESS:
		io_req->fcp_req->status = 0;
		if (!sw_flag) {
			io_req->fcp_req->transferred_length =
			    io_req->fcp_req->payload_length;
			rsplen = 32;
			nrsp->iu_len =
			    cpu_to_be16(sizeof(struct nvme_fc_ersp_iu) / 4);
			nrsp->xfrd_len =
			    cpu_to_be32(io_req->fcp_req->payload_length);

			nrsp->ersp_result = 0;
			cqe->command_id = sqe->common.command_id;
			cqe->status = 0;
			cqe->result.u64 = 0;
		} else {
			io_req->fcp_req->transferred_length =
			    be32_to_cpu(ersp->xfrd_len);
			rsplen = be16_to_cpu(ersp->iu_len) * 4;
			if (rsplen > sizeof(nvme_cmpl->resp_bytes) ||
			    rsplen > io_req->fcp_req->rsplen) {
				FNIC_NVME_DBG(KERN_ERR, fnic,
					      "tport wwpn 0x%llx ERSP len %u desc %u req %u\n",
					      tport_wwpn, rsplen,
					      (u32)sizeof(nvme_cmpl->resp_bytes),
					      io_req->fcp_req->rsplen);
				io_req->fcp_req->status = NVME_SC_INTERNAL;
				io_req->fcp_req->rcv_rsplen = 0;
				break;
			}
			memcpy(io_req->fcp_req->rspaddr, ersp, rsplen);
		}
		atomic64_inc(&fnic_stats->nvme_stats.nvme_ersps);
		io_req->fcp_req->rcv_rsplen = rsplen;
		break;

	default:
		FNIC_NVME_DBG(KERN_ERR, fnic,
				"Unexpected header status: %d\n", hdr_status);
		io_req->fcp_req->status = NVME_SC_INTERNAL;
		break;
	}

	if (hdr_status != FCPIO_SUCCESS) {
		atomic64_inc(&fnic_stats->io_stats.io_failures);
		FNIC_NVME_DBG(KERN_ERR, fnic, "hdr status: %s tag: 0x%x\n",
			    fnic_fcpio_status_to_str(hdr_status), tag);
	}

	io_req->cmd_flags |= FNIC_IO_DONE;

	lba = (char *) &cmdiu->sqe.rw.slba;
	cmd_trace = ((u64) hdr_status << 56) |
	    (u64) ersp->ersp_result << 48 |
	    (u64) cmdiu->sqe.rw.opcode << 32 |
	    (u64) lba[0] << 24 | (u64) lba[1] << 16 |
	    (u64) lba[2] << 8 | lba[3];

	FNIC_TRACE(nvfnic_fcpio_ersp_cmpl_handler, fnic->fnic_num,
		   tag, io_req,
		   ((u64) nvme_cmpl->resvd[1] << 56 |
		    (u64) nvme_cmpl->resvd[0] << 48 |
		    jiffies_to_msecs(jiffies - start_time)),
		   desc, cmd_trace,
		   (((u64) io_req->cmd_flags << 32) |
		    io_req->cmd_state));

	nvfnic_update_io_stats(fnic, cmdiu->sqe.rw.opcode);
	nvfnic_update_io_bytes(fnic, io_req, cmdiu->sqe.rw.opcode);
	nvfnic_update_cmpl_stats(fnic, io_req);

	nvfnic_release_nvme_ioreq_buf(iport, io_req);

	/* Call NVME completion function to complete the IO */
	if (io_req->done)
		io_req->done(io_req);

	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}

void nvfnic_fcpio_nvme_itmf_cmpl_handler(struct fnic *fnic,
				       struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag ftag;
	u32 id;
	unsigned int tag;
	struct fnic_io_req *io_req;
	struct nvme_fc_cmd_iu *cmd_iu;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	struct abort_stats *abts_stats = &fnic->fnic_stats.abts_stats;
	struct terminate_stats *term_stats = &fnic->fnic_stats.term_stats;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	struct fnic_iport_s *iport;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &ftag);
	fcpio_tag_id_dec(&ftag, &id);
	tag = id & FNIC_TAG_MASK;

	if (tag >= fnic->fnic_max_tag_id) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "Tag out of range id:0x%x tag:0x%x hdr:%s\n",
			      id, tag, fnic_fcpio_status_to_str(hdr_status));
		return;
	}
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	io_req = nvfnic_find_io_req_by_tag(fnic, tag);
	WARN_ON_ONCE(!io_req);
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.ioreq_null);
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "IOREQ null hdr:%s tag:0x%x desc:%p\n",
			      fnic_fcpio_status_to_str(hdr_status), tag, desc);
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "type: 0x%x status: 0x%x rsvd: 0x%x\n",
			      desc->hdr.type, desc->hdr.status, desc->hdr._resvd);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	cmd_iu = io_req->fcp_req->cmdaddr;
	FNIC_NVME_DBG(KERN_INFO, fnic,
	      "Received ITMF completion tag: 0x%x hdr_status: %d csn: 0x%08x\n",
	      tag, hdr_status, be32_to_cpu(cmd_iu->csn));

	iport = io_req->iport;

	/* Completion of abort cmd */
	switch (hdr_status) {
	case FCPIO_SUCCESS:
		FNIC_NVME_DBG(KERN_DEBUG, fnic,
				"Abort success received tag: 0x%x id: 0x%x\n",
			      tag, id);
		break;
	case FCPIO_TIMEOUT:
		FNIC_NVME_DBG(KERN_ERR, fnic,
				"Abort timeout received tag: 0x%x id: 0x%x\n",
			      tag, id);
		if (io_req->cmd_flags & FNIC_IO_ABTS_ISSUED)
			atomic64_inc(&abts_stats->abort_fw_timeouts);
		else
			atomic64_inc(&term_stats->terminate_fw_timeouts);
		break;
	case FCPIO_ITMF_REJECTED:
		FNIC_NVME_DBG(KERN_ERR, fnic,
				"Abort reject received tag: 0x%x id: 0x%x\n",
			      tag, id);
		break;

	case FCPIO_IO_NOT_FOUND:
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "Abort IO not found tag:0x%x id:0x%x\n",
			      tag, id);
		if (io_req->cmd_flags & FNIC_IO_ABTS_ISSUED)
			atomic64_inc(&abts_stats->abort_io_not_found);
		else
			atomic64_inc(&term_stats->terminate_io_not_found);
		break;
	default:
		FNIC_NVME_DBG(KERN_ERR, fnic,
				"Abort unknown received tag: 0x%x id: 0x%x\n",
			    tag, id);
		if (io_req->cmd_flags & FNIC_IO_ABTS_ISSUED)
			atomic64_inc(&abts_stats->abort_failures);
		else
			atomic64_inc(&term_stats->terminate_failures);
		break;
	}

	if (io_req->cmd_state != FNIC_IOREQ_ABTS_PENDING) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "Abort late completion tag:0x%x id:0x%x\n",
			      tag, id);
		/* This is a late completion. Ignore it */
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	io_req->abts_state = hdr_status;

	/* If the status is IO not found consider it as success.
	 * NVME sends abort even if rport is down in which case
	 * we will get FCPIO_TIMEOUT. Consider this as success.
	 */
	if ((hdr_status == FCPIO_IO_NOT_FOUND) ||
	    (hdr_status == FCPIO_TIMEOUT) ||
	    (hdr_status == FCPIO_ITMF_REJECTED))
		io_req->abts_state = FCPIO_SUCCESS;

	io_req->cmd_flags |= FNIC_IO_ABT_TERM_DONE;

	if (!(io_req->cmd_flags & (FNIC_IO_ABORTED | FNIC_IO_DONE)))
		atomic64_inc(&misc_stats->no_icmnd_itmf_cmpls);

	io_req->fcp_req->transferred_length = 0;
	io_req->fcp_req->rcv_rsplen = 0;
	if (io_req->abts_state == FCPIO_SUCCESS)
		io_req->fcp_req->status = NVME_SC_ABORT_REQ;
	else
		io_req->fcp_req->status = NVME_SC_INTERNAL;

	atomic64_dec(&fnic_stats->io_stats.active_ios);
	if (atomic64_read(&fnic->io_cmpl_skip))
		atomic64_dec(&fnic->io_cmpl_skip);
	else
		atomic64_inc(&fnic_stats->io_stats.io_completions);

	nvfnic_release_nvme_ioreq_buf(iport, io_req);
	if (io_req->done)
		io_req->done(io_req);
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}

bool _cleanup_tport_io(struct sbitmap *map, unsigned int tag,
			      void *data)
{
	struct fnic_tport_s *tport = data;
	struct fnic_iport_s *iport = tport->iport;
	struct fnic *fnic = iport->fnic;
	struct fnic_io_req *io_req;
	enum fnic_ioreq_state old_ioreq_state;
	unsigned long flags;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	io_req = nvfnic_find_io_req_by_tag(fnic, tag);
	if (!io_req || io_req->tport != tport) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return true;
	}

	if ((io_req->cmd_state == FNIC_IOREQ_ABTS_PENDING) ||
	    (io_req->cmd_state == FNIC_DEV_RST_TERM_ISSUED)) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "Abort already pending 0x%x\n", io_req->tag);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return true;
	}

	FNIC_NVME_DBG(KERN_ERR, fnic,
		      "io_req tag: 0x%x abort after unregister timeout\n",
		      io_req->tag);

	old_ioreq_state = io_req->cmd_state;
	io_req->cmd_state = FNIC_IOREQ_ABTS_PENDING;
	io_req->abts_state = FCPIO_INVALID_CODE;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (!nvfnic_queue_abort_io_req(fnic, io_req->tag,
				      FCPIO_ITMF_ABT_TASK_TERM,
				      io_req)) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			    "Failed to enqueue abort for ioreq tag: 0x%x\n",
			    io_req->tag);
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		io_req->cmd_state = old_ioreq_state;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	}
	return true;
}

void
nvfnic_cleanup_tport_io(struct fnic *fnic, struct fnic_tport_s *tport)
{
	unsigned long flags;
	struct nvfnic_ls_req *nvfnic_ls_req, *next;
	struct nvmefc_ls_req *lsreq;
	uint16_t oxid;
	LIST_HEAD(done_reqs);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	list_for_each_entry_safe(nvfnic_ls_req, next,
				 &(tport->ls_req_list), list) {
		lsreq = nvfnic_ls_req->ls_req;
		if (!lsreq || (lsreq->private == NULL)) {
			FNIC_NVME_DBG(KERN_INFO, fnic,
				"fnic_cleanup_tport_io lsreq NULL\n");
			continue;
		}
		if (nvfnic_ls_req->state == FNIC_LS_REQ_CMD_ABTS_STARTED) {
			FNIC_NVME_DBG(KERN_INFO, fnic,
				"fnic_cleanup_tport_io lsreq abort started\n");
			continue;
		}
		list_del_init(&nvfnic_ls_req->list);
		lsreq->private = NULL;
		oxid = nvfnic_ls_req->oxid;
		fdls_free_oxid(&fnic->iport, oxid, &nvfnic_ls_req->oxid);
		nvfnic_ls_req->state = FNIC_LS_REQ_CMD_COMPLETE;
		list_add_tail(&nvfnic_ls_req->list, &done_reqs);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	list_for_each_entry_safe(nvfnic_ls_req, next, &done_reqs, list) {
		list_del_init(&nvfnic_ls_req->list);
		lsreq = nvfnic_ls_req->ls_req;
		timer_delete_sync(&nvfnic_ls_req->ls_req_timer);
		lsreq->done(lsreq, -ENXIO);
	}

	/* For link-down, IOs are freed by firmware reset completion */
	if (fdls_get_state(&fnic->iport.fabric) == FDLS_STATE_LINKDOWN)
		return;

	sbitmap_for_each_set(&fnic->nvfnic_tag_map, _cleanup_tport_io, tport);
}

void
nvfnic_terminate_tport_ls_reqs(struct fnic *fnic, struct fnic_tport_s *tport)
{
	struct nvmefc_ls_req *lsreq;
	struct nvfnic_ls_req *nvfnic_ls_req, *next;
	int count = 0;
	uint16_t oxid;
	LIST_HEAD(done_reqs);

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	list_for_each_entry_safe(nvfnic_ls_req, next,
		&(tport->ls_req_list), list) {

		lsreq = nvfnic_ls_req->ls_req;
		if (!lsreq || (lsreq->private == NULL)) {
			FNIC_NVME_DBG(KERN_ERR, fnic,
				"lsreq is NULL\n");
			continue;
		}
		if (nvfnic_ls_req->state == FNIC_LS_REQ_CMD_ABTS_STARTED) {
			FNIC_NVME_DBG(KERN_INFO, fnic,
				"lsreq abort started\n");
			continue;
		}
		oxid = nvfnic_ls_req->oxid;
		list_del_init(&nvfnic_ls_req->list);
		lsreq->private = NULL;
		fdls_free_oxid(&fnic->iport, oxid, &nvfnic_ls_req->oxid);
		nvfnic_ls_req->state = FNIC_LS_REQ_CMD_COMPLETE;
		list_add_tail(&nvfnic_ls_req->list, &done_reqs);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);

	list_for_each_entry_safe(nvfnic_ls_req, next, &done_reqs, list) {
		list_del_init(&nvfnic_ls_req->list);
		lsreq = nvfnic_ls_req->ls_req;
		timer_delete_sync(&nvfnic_ls_req->ls_req_timer);
		lsreq->done(lsreq, -ENXIO);
		count++;
	}

	FNIC_NVME_DBG(KERN_INFO, fnic,
		"fnic_terminate_tport_lsreqs tport: 0x%x: freed lsreq: %d\n",
		tport->fcid, count);
}

bool _terminate_tport_ios(struct sbitmap *map, unsigned int tag,
				       void *data)
{
	struct fnic_tport_s *tport = data;
	struct fnic_iport_s *iport = tport->iport;
	struct fnic *fnic = iport->fnic;
	struct fnic_io_req *io_req;
	enum fnic_ioreq_state old_ioreq_state;
	unsigned long flags;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	io_req = fnic->sw_copy_wq[0].io_req_table[tag];
	if (!io_req) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return true;
	}

	if (io_req->tport != tport) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return true;
	}

	if ((io_req->cmd_state == FNIC_IOREQ_ABTS_PENDING) ||
	    (io_req->cmd_state == FNIC_DEV_RST_TERM_ISSUED)) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "Abort already pending 0x%x\n", io_req->tag);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return true;
	}

	FNIC_NVME_DBG(KERN_INFO, fnic,
		      "Terminate tag: 0x%x (tport fcid 0x%x)\n",
		      io_req->tag, io_req->tport->fcid);

	old_ioreq_state = io_req->cmd_state;
	io_req->cmd_state = FNIC_IOREQ_ABTS_PENDING;
	io_req->abts_state = FCPIO_INVALID_CODE;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (!nvfnic_queue_abort_io_req(fnic, io_req->tag,
					  FCPIO_ITMF_ABT_TASK_TERM, io_req)) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "Failed to enqueue abort for ioreq tag: 0x%x\n",
			      io_req->tag);
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		io_req->cmd_state = old_ioreq_state;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	}
	return true;
}

void nvfnic_terminate_tport_ios(struct fnic *fnic,
				     struct fnic_tport_s *tport)
{
	struct abort_stats *abts_stats = &fnic->fnic_stats.abts_stats;

	sbitmap_for_each_set(&fnic->nvfnic_tag_map, _terminate_tport_ios, tport);

	FNIC_NVME_DBG(KERN_INFO, fnic,
		      "tport: 0x%x aborted %lld in_flight %d\n",
		      tport->fcid, atomic64_read(&abts_stats->aborts),
		      atomic_read(&fnic->in_flight));
}

bool _cleanup_all_nvme_io(struct sbitmap *map, unsigned int tag,
				 void *data)
{
	struct fnic_iport_s *iport = data;
	struct fnic_io_req *io_req;

	io_req = iport->fnic->sw_copy_wq[0].io_req_table[tag];
	if (!io_req)
		return true;

	io_req->cmd_state = FNIC_DEV_RST_TERM_ISSUED;
	io_req->fcp_req->status = NVME_SC_INTERNAL;
	io_req->fcp_req->transferred_length = 0;
	io_req->fcp_req->rcv_rsplen = 0;
	nvfnic_release_nvme_ioreq_buf(iport, io_req);
	io_req->done(io_req);
	return true;
}

void nvfnic_cleanup_all_nvme_ios(struct fnic *fnic)
{
	struct fnic_iport_s *iport = &fnic->iport;

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	sbitmap_for_each_set(&fnic->nvfnic_tag_map, _cleanup_all_nvme_io,
			     iport);
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}

void nvfnic_nvme_zero_devloss_tports(struct fnic *fnic)
{
	struct fnic_tport_s *tport, *next;

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	list_for_each_entry_safe(tport, next, &fnic->iport.tport_list, links) {
		if (tport->flags & FNIC_FDLS_NVME_REGISTERED) {
			spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
			nvme_fc_set_remoteport_devloss(tport->nv_rport, 0);
			spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
		}
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}

void nvfnic_nvme_unload(struct fnic *fnic)
{
	int ret = 0;
	struct fnic_iport_s *iport = &fnic->iport;
	unsigned long flags;
	unsigned int time_wait =  FNIC_NVME_LPORT_REMOVE_WAIT;
	unsigned int time_remain;
	DECLARE_COMPLETION_ONSTACK(nvme_lport_unreg_done);

	/* Mark iport state as INIT so that no IOs can be issued from this point */
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->in_remove = 1;
	fnic->iport.state = FNIC_IPORT_STATE_LINK_WAIT;
	fnic->nvme_lport_unreg_done = &nvme_lport_unreg_done;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	/*
	 * If fnic is already processing link-down or fnic is held
	 * in disabled state following a reboot we dont need to issue
	 * firmware reset and unregister remote ports as it is already
	 * done as part of link down handling.
	 */
	if (fdls_get_state(&iport->fabric) == FDLS_STATE_LINKDOWN) {
		while (fnic->reset_in_progress == IN_PROGRESS) {
			wait_for_completion_timeout(&fnic->reset_completion_wait,
						    msecs_to_jiffies(5000));
			FNIC_NVME_DBG(KERN_INFO, fnic,
				      "rmmod waiting for reset %p\n", fnic);
		}
	} else if (fdls_get_state(&iport->fabric) != FDLS_STATE_INIT)
		fnic_fcpio_reset(fnic);

	/*
	 * Mark state so that the workqueue thread stops forwarding
	 * received frames and link events to the local port. ISR and
	 * other threads that can queue work items will also stop
	 * creating work items on the fnic workqueue
	 */
	nvfnic_nvme_zero_devloss_tports(fnic);
	fnic_flush_tport_event_list(fnic);
	fnic_delete_fcp_tports(fnic);

	if (iport->flags & FNIC_LPORT_NVME_REGISTERED) {
		ret = nvme_fc_unregister_localport(fnic->iport.nv_lport);
		if (ret) {
			FNIC_NVME_DBG(KERN_ERR, fnic,
				"Unregister nvme localport failed: %d\n", ret);
		} else {
			time_remain = wait_for_completion_timeout(
				fnic->nvme_lport_unreg_done,
				msecs_to_jiffies(time_wait));
			if (!time_remain) {
				FNIC_NVME_DBG(KERN_ERR, fnic,
					      "Local port removal timed out\n");
				WARN_ON(1);
			}
			iport->flags &= ~FNIC_LPORT_NVME_REGISTERED;
			kfree(iport->nv_tmpl);
		}
	}

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->nvme_lport_unreg_done = NULL;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	nvfnic_flush_nvme_io_list(fnic);
}

struct nvfnic_ls_req*
nvfnic_find_ls_req(struct fnic_tport_s *tport, uint16_t oxid)
{
	struct nvfnic_ls_req *nvfnic_ls_req, *next;

	list_for_each_entry_safe(nvfnic_ls_req, next, &(tport->ls_req_list), list) {
		if (nvfnic_ls_req->oxid == oxid)
			return nvfnic_ls_req;
	}
	return NULL;
}

void nvfnic_fcpio_cmpl(struct fnic_io_req *io_req)
{
	struct fnic *fnic = io_req->iport->fnic;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;

	nvfnic_free_fcpio_tag(io_req->iport, io_req);
	atomic64_inc(&fnic_stats->io_stats.nvme_ios_queued_for_rsp);
	io_req->waitq_start_time = jiffies;

	llist_add(&io_req->nvfnic_io_cmpl, &fnic->nvme_io_event_llist);
	atomic_inc(&fnic->nvme_io_event_queued);
	atomic64_inc(&fnic_stats->io_stats.nvme_num_ios_in_waitq);

	queue_work(fnic_cmpl_queue, &fnic->nvme_io_cmpl_work);
}

void nvfnic_process_ls_abts_rsp(struct fnic_iport_s *iport,
		struct fc_frame_header *fchdr)
{
	uint32_t tport_fcid;
	struct fnic_tport_s *tport;
	struct nvfnic_ls_req *nvfnic_ls_req;
	struct nvmefc_ls_req *lsreq;
	uint8_t *fcid;
	uint16_t oxid = FNIC_STD_GET_OX_ID(fchdr);
	struct fnic *fnic = iport->fnic;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;

	fcid = FNIC_STD_GET_S_ID(fchdr);
	tport_fcid = ntoh24(fcid);

	tport = fnic_find_tport_by_fcid(iport, tport_fcid);
	if (tport == NULL) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "tport: 0x%x not found\n", tport_fcid);
		return;
	}

	nvfnic_ls_req = nvfnic_find_ls_req(tport, oxid);
	if (nvfnic_ls_req == NULL) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "tport: 0x%x lsreq oxid: 0x%x not found\n",
			      tport_fcid, oxid);
		return;
	}

	lsreq = nvfnic_ls_req->ls_req;
	if ((lsreq == NULL) || (lsreq->private == NULL)) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "tport: 0x%x lsreq oxid: 0x%x already aborted\n",
			      tport_fcid, oxid);
		return;
	}

	atomic64_inc(&fnic_stats->nvme_stats.nvme_ls_abort_responses);
	nvfnic_ls_req->state = FNIC_LS_REQ_ABTS_COMPLETE;

	FNIC_NVME_DBG(KERN_DEBUG, fnic, "nvme_ls_requests: %lld\n",
		      (u64) atomic64_read(&fnic_stats->nvme_stats.nvme_ls_requests));

	list_del(&nvfnic_ls_req->list);
	fdls_free_oxid(iport, oxid, &nvfnic_ls_req->oxid);
	lsreq->private = NULL;
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
	timer_delete_sync(&nvfnic_ls_req->ls_req_timer);
	lsreq->done(lsreq, NVME_SC_HOST_ABORTED_CMD);
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
}

/**
 * nvfnic_ls_rsp_recv - Handle received NVMe FC link service (LS) response
 * @iport:  Pointer to the local FNIC port structure
 * @fchdr:  Pointer to the Fibre Channel frame header for the
 *          received response
 * @len:    Length of the received frame
 *
 * This function processes link service (LS) responses received from
 * NVMe Discovery Controllers or regular NVMe subsystems during
 * association.
 */
void nvfnic_ls_rsp_recv(struct fnic_iport_s *iport,
		struct fc_frame_header *fchdr, int len)
{
	uint8_t *fcid;
	uint32_t tport_fcid;
	struct fnic_tport_s *tport;
	struct nvfnic_ls_req *nvfnic_ls_req;
	struct nvmefc_ls_req *lsreq;
	uint16_t oxid;
	uint32_t rsp_len;
	int sid_len = offsetof(struct fc_frame_header, fh_s_id) +
		      sizeof(fchdr->fh_s_id);
	int status = 0;
	struct fnic *fnic = iport->fnic;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;

	if (len < (int)sizeof(*fchdr)) {
		if (len >= sid_len) {
			fcid = FNIC_STD_GET_S_ID(fchdr);
			tport_fcid = ntoh24(fcid);
			FNIC_NVME_DBG(KERN_ERR, fnic,
				      "tport: 0x%x LS rsp len %d too short\n",
				      tport_fcid, len);
		} else {
			FNIC_NVME_DBG(KERN_ERR, fnic,
				      "LS response len %d too short\n", len);
		}
		return;
	}
	rsp_len = len - sizeof(*fchdr);

	fcid = FNIC_STD_GET_S_ID(fchdr);
	tport_fcid = ntoh24(fcid);

	tport = fnic_find_tport_by_fcid(iport, tport_fcid);
	if (!tport) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "tport: 0x%x not found\n", tport_fcid);
		return;
	}

	oxid = FNIC_STD_GET_OX_ID(fchdr);
	nvfnic_ls_req = nvfnic_find_ls_req(tport, oxid);
	if (!nvfnic_ls_req) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "tport: 0x%x no nvfnic_lsreq for oxid: 0x%x\n",
			      tport_fcid, oxid);
		return;
	}

	lsreq = nvfnic_ls_req->ls_req;
	if (!lsreq || (lsreq->private == NULL)) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "tport:0x%x lsreq:0x%x already done\n",
			      tport_fcid, oxid);
		return;
	}
	if (!lsreq->rspaddr) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "tport 0x%x lsreq 0x%x rspaddr NULL\n",
			      tport_fcid, oxid);
		return;
	}

	if ((nvfnic_ls_req->state == FNIC_LS_REQ_CMD_ABTS_PENDING) ||
	    (nvfnic_ls_req->state == FNIC_LS_REQ_CMD_ABTS_STARTED)) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "tport 0x%x lsreq oxid: 0x%x abts pending\n",
			      tport_fcid, oxid);
		return;
	}

	if (rsp_len > lsreq->rsplen) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "tport:0x%x lsreq:0x%x rsp %u > %u\n",
			      tport_fcid, oxid, rsp_len, lsreq->rsplen);
		status = -EOVERFLOW;
	}

	nvfnic_ls_req->state = FNIC_LS_REQ_CMD_COMPLETE;
	atomic64_inc(&fnic_stats->nvme_stats.nvme_ls_responses);

	list_del_init(&nvfnic_ls_req->list);
	lsreq->private = NULL;
	fdls_free_oxid(iport, oxid, &nvfnic_ls_req->oxid);

	if (status == 0) {
		FNIC_NVME_DBG(KERN_DEBUG, fnic,
			      "tport:0x%x lsreq:0x%x completed\n",
			      tport_fcid, oxid);

		/* Copy the Response */
		memcpy(lsreq->rspaddr, (uint8_t *)fchdr + sizeof(*fchdr),
		       rsp_len);
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
	timer_delete_sync(&nvfnic_ls_req->ls_req_timer);
	lsreq->done(lsreq, status);
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
}

static bool nvfnic_ls_req_cleanup(struct fnic_iport_s *iport,
				  struct nvmefc_ls_req *lsreq,
				  uint16_t oxid)
{
	struct nvfnic_ls_req *nvfnic_ls_req = lsreq->private;

	if (!nvfnic_ls_req)
		return false;

	lsreq->private = NULL;
	list_del(&nvfnic_ls_req->list);
	fdls_free_oxid(iport, oxid, &nvfnic_ls_req->oxid);
	nvfnic_ls_req->state = FNIC_LS_REQ_CMD_COMPLETE;

	return true;
}

void nvfnic_ls_req_timeout(struct timer_list *t)
{
	struct nvfnic_ls_req *nvfnic_ls_req = timer_container_of(nvfnic_ls_req,
			t, ls_req_timer);
	struct fnic *fnic = nvfnic_ls_req->fnic;
	struct nvmefc_ls_req *ls_req = nvfnic_ls_req->ls_req;
	struct fnic_iport_s *iport = &fnic->iport;
	struct fnic_tport_s *tport = (struct fnic_tport_s *) nvfnic_ls_req->tport;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	uint16_t oxid = nvfnic_ls_req->oxid;
	int timeout;

	FNIC_NVME_DBG(KERN_INFO, fnic,
		      "tport: 0x%x lsreq: 0x%x state: %d timeout\n",
		      tport->fcid, nvfnic_ls_req->oxid,
		      nvfnic_ls_req->state);
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	if ((ls_req->private == NULL) ||
	    (nvfnic_ls_req->state == FNIC_LS_REQ_CMD_ABTS_STARTED)) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "tport: 0x%x lsreq: 0x%x already aborted\n",
			      tport->fcid, nvfnic_ls_req->oxid);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	if (nvfnic_ls_req->state == FNIC_LS_REQ_CMD_ABTS_PENDING) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "tport: 0x%x lsreq: 0x%x abort timeout\n",
			      tport->fcid, nvfnic_ls_req->oxid);

		ls_req = nvfnic_ls_req->ls_req;
		nvfnic_ls_req_cleanup(iport, ls_req, oxid);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		ls_req->done(ls_req, -ETIMEDOUT);
		return;
	} else if ((nvfnic_ls_req->state == FNIC_LS_REQ_CMD_PENDING) &&
		   (nvfnic_transport_ready(iport, tport))) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "tport: 0x%x lsreq: 0x%x sending abort\n",
			      tport->fcid, nvfnic_ls_req->oxid);
		nvfnic_ls_req->state = FNIC_LS_REQ_CMD_ABTS_PENDING;
		atomic64_inc(&fnic_stats->nvme_stats.nvme_ls_aborts);

		if (fdls_send_ls_req_abts(iport, tport, nvfnic_ls_req->oxid) == 0) {
			timeout = FNIC_LS_REQ_TMO_MSECS(ls_req->timeout);
			mod_timer(&nvfnic_ls_req->ls_req_timer,
				  round_jiffies(jiffies + msecs_to_jiffies(timeout)));
			spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
			return;
		}
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "tport: 0x%x lsreq: 0x%x cannot send abort\n",
			      tport->fcid, oxid);
	}

	if (ls_req->private == NULL) {
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	ls_req = nvfnic_ls_req->ls_req;
	nvfnic_ls_req_cleanup(iport, ls_req, oxid);

	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
	ls_req->done(ls_req, -ETIMEDOUT);
}

/**
 * nvfnic_ls_req_send - Send NVMe FC link service (LS) request
 * @lport:   Pointer to local NVMe FC port structure
 * @rport:   Pointer to remote NVMe FC port structure
 * @ls_req:  Pointer to the link service request structure
 *
 * This function is used to send link service (LS) commands to an NVMe
 * Discovery Controller for discovery operations, as well as to regular
 * NVMe subsystems during association. It encapsulates the logic for
 * transmitting LS requests over the NVMe over Fabrics (NVMe-oF) FC
 * transport.
 *
 * Returns: 0 on success, or a negative error code on failure.
 */
int nvfnic_ls_req_send(struct nvme_fc_local_port *lport,
		  struct nvme_fc_remote_port *rport,
		  struct nvmefc_ls_req *ls_req)
{
	int timeout;
	uint8_t *frame;
	uint8_t fcid[3];
	unsigned long flags = 0;
	struct fnic_iport_s *iport = lport->private;
	uint8_t *ls_req_payload;
	struct fnic *fnic = iport->fnic;
	struct fc_frame_header *fchdr;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	struct nvfnic_ls_req *nvfnic_ls_req = ls_req->private;
	uint16_t frame_size = FNIC_ETH_FCOE_HDRS_OFFSET +
			sizeof(struct fc_frame_header) + ls_req->rqstlen;
	struct fnic_tport_s *tport;
	int ret;

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	tport = (struct fnic_tport_s *)rport->private;
	INIT_LIST_HEAD(&nvfnic_ls_req->list);

	if (!nvfnic_transport_ready(iport, tport)) {
		if (tport != NULL)
			FNIC_NVME_DBG(KERN_INFO, fnic,
				      "iport: 0x%x tport: 0x%x transport not ready\n",
				      iport->fcid, tport->fcid);
		else
			FNIC_NVME_DBG(KERN_INFO, fnic,
				      "iport: 0x%x transport not ready\n",
				      iport->fcid);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return -ENOLINK;
	}

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
		     "Failed to allocate frame to send NVME LS REQ");
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return -ENOMEM;
	}

	if (fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_NVME_LS,
			&nvfnic_ls_req->oxid) == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic,
		     "0x%x: Failed to allocate OXID to send NVME LS REQ",
			 iport->fcid);
		mempool_free(frame, fnic->frame_pool);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return -EAGAIN;
	}

	atomic64_inc(&fnic_stats->nvme_stats.nvme_ls_requests);
	timer_setup(&nvfnic_ls_req->ls_req_timer, nvfnic_ls_req_timeout,
		     0UL);

	nvfnic_ls_req->fnic = fnic;
	nvfnic_ls_req->tport = tport;
	nvfnic_ls_req->state = FNIC_LS_REQ_CMD_INIT;
	nvfnic_ls_req->ls_req = ls_req;

	fchdr = (struct fc_frame_header *)(frame + FNIC_ETH_FCOE_HDRS_OFFSET);
	*fchdr = (struct fc_frame_header) {
		.fh_r_ctl = FC_RCTL_ELS4_REQ,
		.fh_type = FC_TYPE_NVME,
		.fh_f_ctl = {FNIC_ELS_REQ_FCTL, 0, 0},
		.fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID)
	};

	hton24(fcid, iport->fcid);
	FNIC_STD_SET_S_ID(*fchdr, fcid);

	hton24(fcid, tport->fcid);
	FNIC_STD_SET_D_ID(*fchdr, fcid);

	FNIC_STD_SET_OX_ID(*fchdr, nvfnic_ls_req->oxid);

	ls_req_payload = frame + FNIC_ETH_FCOE_HDRS_OFFSET + sizeof(*fchdr);
	memcpy(ls_req_payload, ls_req->rqstaddr, ls_req->rqstlen);

	FNIC_NVME_DBG(KERN_INFO, fnic,
		 "0x%x: NVME send ls req with oxid: 0x%x type: 0x%02x len: %d",
		 iport->fcid, nvfnic_ls_req->oxid, *((uint8_t *) ls_req->rqstaddr),
		 ls_req->rqstlen);

	FNIC_NVME_DBG(KERN_INFO, fnic,
		 "0x%x: ls_reqs count: %lld",
		 iport->fcid,
		 (u64) atomic64_read(&fnic_stats->nvme_stats.nvme_ls_requests));

	list_add_tail(&nvfnic_ls_req->list, &tport->ls_req_list);
	nvfnic_ls_req->state = FNIC_LS_REQ_CMD_PENDING;

	ret = fnic_send_fcoe_frame(iport, frame, frame_size);
	if (ret) {
		nvfnic_ls_req_cleanup(iport, ls_req, nvfnic_ls_req->oxid);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		mempool_free(frame, fnic->frame_pool);
		return ret;
	}

	timeout = FNIC_LS_REQ_TMO_MSECS(ls_req->timeout);
	mod_timer(&nvfnic_ls_req->ls_req_timer,
		  round_jiffies(jiffies + msecs_to_jiffies(timeout)));
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	return 0;
}

void nvfnic_local_port_delete(struct nvme_fc_local_port *lport)
{
	struct fnic_iport_s *iport = (struct fnic_iport_s *) lport->private;
	struct fnic *fnic = iport->fnic;
	unsigned long flags = 0;

	FNIC_NVME_DBG(KERN_INFO, fnic, "lport delete 0x%x\n",
		      iport->fcid);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (fnic->nvme_lport_unreg_done)
		complete(fnic->nvme_lport_unreg_done);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

void nvfnic_remote_port_delete(struct nvme_fc_remote_port *rport)
{
	/*
	 * Read rport->private without the lock only to find fnic.
	 * Re-read it under fnic_lock to claim this delete callback, since
	 * another callback may already have cleared it.
	 */
	struct fnic_tport_s *tport = (struct fnic_tport_s *)rport->private;
	struct fnic_iport_s *iport;
	struct fnic *fnic = NULL;
	unsigned long flags = 0;

	if (tport == NULL) {
		pr_err("Attempt to delete already deleted tport\n");
		return;
	}

	iport = tport->iport;
	fnic = iport->fnic;
	FNIC_NVME_DBG(KERN_INFO, fnic, "0x%x tport 0x%x\n",
		      iport->fcid, tport->fcid);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	tport = (struct fnic_tport_s *)rport->private;
	if (tport == NULL) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "NVMe tport callback after NULL set %p\n",
			      rport);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	iport = tport->iport;
	rport->private = NULL;

	if (tport->timer_pending) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "tport: 0x%x canceling discovery timer\n",
			      tport->fcid);
		/*
		 * The retry timer callback takes fnic_lock and dereferences
		 * this tport. Set del_timer_inprogress before dropping the
		 * lock so a callback that is already running observes teardown
		 * and exits.
		 *
		 * timer_delete_sync() then waits until no callback can still
		 * hold a reference before the remoteport-delete path completes
		 * or frees the target port.
		 */
		tport->del_timer_inprogress = 1;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		timer_delete_sync(&tport->retry_timer);
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		tport->del_timer_inprogress = 0;
		tport->timer_pending = 0;
	}

	if (tport->flags & FNIC_FDLS_NVME_TPORT_CLEANUP_PENDING) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "tport %8x waiting on clean pending\n",
			      tport->fcid);
	}

	while (tport->flags & FNIC_FDLS_NVME_TPORT_CLEANUP_PENDING) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		msleep(2000);
		spin_lock_irqsave(&fnic->fnic_lock, flags);
	}

	list_del(&tport->links);

	if (tport->tport_del_done) {
		tport->flags |= FNIC_TPORT_CAN_BE_FREED;
		complete(tport->tport_del_done);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	kfree(tport);
}

int nvfnic_create_queue(struct nvme_fc_local_port *lport,
		    unsigned int idx, u16 size, void **handle)
{
	struct fnic_iport_s *iport = (struct fnic_iport_s *)lport->private;
	struct fnic *fnic  = iport->fnic;

	FNIC_NVME_DBG(KERN_DEBUG, fnic,
		      "iport:0x%x queue:%d size:%d\n", iport->fcid, idx, size);

	if (idx > fnic->wq_copy_count)
		return -EINVAL;

	if (idx == 0) {
		/* Admin queue */
		*handle = &fnic->hw_copy_wq[0];
	} else {
		/* IO queues */
		*handle = &fnic->hw_copy_wq[idx-1];
	}

	return 0;
}

void nvfnic_ls_req_abort(struct nvme_fc_local_port *lport,
		   struct nvme_fc_remote_port *rport,
		   struct nvmefc_ls_req *lsreq)
{
	struct fnic_iport_s *iport = lport->private;
	struct fnic *fnic = iport->fnic;
	struct fnic_tport_s *tport;
	struct nvfnic_ls_req *nvfnic_ls_req;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	uint16_t oxid;
	int timeout;
	int ret;

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	tport = (struct fnic_tport_s *) rport->private;
	/* find the request */
	nvfnic_ls_req = lsreq->private;

	if (nvfnic_ls_req == NULL) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "0x%x null lsreq already scheduled for abort\n",
			      iport->fcid);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	if (nvfnic_ls_req->state == FNIC_LS_REQ_CMD_ABTS_PENDING) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "0x%x lsreq 0x%x already scheduled for abort\n",
			      iport->fcid, nvfnic_ls_req->oxid);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	FNIC_NVME_DBG(KERN_INFO, fnic,
		      "0x%x lsreq 0x%x abts\n",
		      iport->fcid, nvfnic_ls_req->oxid);

	nvfnic_ls_req->state = FNIC_LS_REQ_CMD_ABTS_STARTED;
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
	timer_delete_sync(&nvfnic_ls_req->ls_req_timer);

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	nvfnic_ls_req = lsreq->private;

	if ((nvfnic_ls_req == NULL) ||
	    (nvfnic_ls_req->state == FNIC_LS_REQ_CMD_ABTS_PENDING)) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "lsreq timeout raced with midlayer abort\n");
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	/* Basic validations of the state */
	if (!nvfnic_transport_ready(iport, tport)) {
		/* If iport or tport offline, it will be handled from that event */
		oxid = nvfnic_ls_req->oxid;
		lsreq->private = NULL;
		list_del(&nvfnic_ls_req->list);
		fdls_free_oxid(iport, oxid, &nvfnic_ls_req->oxid);
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		lsreq->done(lsreq, -ENXIO);
		FNIC_NVME_DBG(KERN_ERR, fnic,
				"nvfnic_lsreq_abort transport not ready\n");
		return;
	}

	/* Mark the state and flags */
	nvfnic_ls_req->state = FNIC_LS_REQ_CMD_ABTS_PENDING;
	atomic64_inc(&fnic_stats->nvme_stats.nvme_ls_aborts);
	oxid = nvfnic_ls_req->oxid;

	ret = fdls_send_ls_req_abts(iport, tport, oxid);
	if (!ret) {
		timeout = FNIC_LS_REQ_TMO_MSECS(lsreq->timeout);
		mod_timer(&nvfnic_ls_req->ls_req_timer,
			  round_jiffies(jiffies + msecs_to_jiffies(timeout)));
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	if (!nvfnic_ls_req_cleanup(iport, lsreq, oxid)) {
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		return;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
	lsreq->done(lsreq, -EAGAIN);
}

bool nvfnic_queue_abort_io_req(struct fnic *fnic, int tag,
			 u32 task_req, struct fnic_io_req *io_req)
{
	int idx;
	unsigned long flags;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;

	idx = io_req->wq - &fnic->hw_copy_wq[0];

	atomic_inc(&fnic->in_flight);

	spin_lock_irqsave(&fnic->wq_copy_lock[idx], flags);

	if (vnic_wq_copy_desc_avail(io_req->wq) <= fnic->wq_copy_desc_low[idx])
		free_wq_copy_descs(fnic, io_req->wq, idx);

	if (!vnic_wq_copy_desc_avail(io_req->wq)) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[idx], flags);
		atomic_dec(&fnic->in_flight);
		FNIC_NVME_DBG(KERN_ERR, fnic,
				"tag 0x%x failure: no descriptors\n", tag);
		atomic64_inc(&misc_stats->abts_cpwq_alloc_failures);
		return false;
	}
	fnic_queue_wq_copy_desc_itmf(io_req->wq, tag | FNIC_TAG_ABORT,
				     0, task_req, tag, NULL, io_req->port_id,
				     fnic->config.ra_tov, fnic->config.ed_tov);

	atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
	if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
	    atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
		atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
			     atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

	spin_unlock_irqrestore(&fnic->wq_copy_lock[idx], flags);
	atomic_dec(&fnic->in_flight);

	return true;
}

void nvfnic_fcpio_abort(struct nvme_fc_local_port *lport,
			struct nvme_fc_remote_port *rport,
			void *hw_queue_handle, struct nvmefc_fcp_req *fcp_req)
{
	struct fnic_iport_s *iport = lport->private;
	struct fnic *fnic = iport->fnic;
	struct nvme_fc_cmd_iu *cmd_iu = fcp_req->cmdaddr;
	struct fnic_io_req *io_req = (struct fnic_io_req *)fcp_req->private;
	unsigned int tag = io_req->tag;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	struct abort_stats *abts_stats;
	unsigned long flags = 0;
	unsigned long abt_issued_time;
	unsigned int task_req;
	enum fnic_ioreq_state old_ioreq_state;
	unsigned long num_ios_waitq, waitq_2sec, waitq_max_time;

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	if (io_req->tag == FNIC_NVME_NO_FREE_TAG) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			      "tag: (0x%x) tport_fcid: 0x%x\n",
			      io_req->tag, io_req->tport->fcid);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	if (io_req != nvfnic_find_io_req_by_tag(fnic, io_req->tag)) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "cmd tag freed or not issued:0x%x sn:0x%08x\n",
			      io_req->tag, be32_to_cpu(cmd_iu->csn));
		num_ios_waitq =
		    atomic64_read(&fnic_stats->io_stats.nvme_num_ios_in_waitq);
		waitq_2sec =
		    atomic64_read(&fnic_stats->io_stats.nvme_ios_in_waitq_3000_msec);
		waitq_max_time =
		    atomic64_read(&fnic_stats->io_stats.nvme_ios_in_waitq_max_time);
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "waitq:%ld waitq_2sec:%ld max_wait:%ld\n",
			      num_ios_waitq, waitq_2sec, waitq_max_time);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	if (io_req->cmd_state == FNIC_IOREQ_CMD_COMPLETE) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "IO already completed before abort: 0x%x\n", tag);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	if ((io_req->cmd_state == FNIC_IOREQ_ABTS_PENDING) ||
	    (io_req->cmd_state == FNIC_DEV_RST_TERM_ISSUED)) {
		FNIC_NVME_DBG(KERN_INFO, fnic, "abort already pending %d\n",
			      tag);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	if (io_req->cmd_state != FNIC_IOREQ_CMD_PENDING) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "io_req completed or aborted for tag:0x%x\n",
			      tag);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	FNIC_NVME_DBG(KERN_INFO, fnic, "in abort cmd_sn:%08x %llx tag: 0x%x\n",
		      be32_to_cpu(cmd_iu->csn),
		      le64_to_cpu(cmd_iu->sqe.rw.slba), io_req->tag);

	if (unlikely(fnic_chk_state_flags_locked(fnic, FNIC_FLAGS_IO_BLOCKED))) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "abort tag:0x%x returned during fw reset\n",
			      io_req->tag);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}
	atomic_inc(&fnic->in_flight);

	if (fdls_tport_is_offline(io_req->tport) ||
	    (io_req->cmd_state == FNIC_IOREQ_RESET_TERM)) {
		task_req = FCPIO_ITMF_ABT_TASK_TERM;
	} else {
		task_req = FCPIO_ITMF_ABT_TASK;
	}

	abts_stats = &fnic->fnic_stats.abts_stats;
	atomic64_inc(&abts_stats->aborts);

	abt_issued_time = jiffies_to_msecs(jiffies - io_req->start_time);
	if (abt_issued_time <= 6000)
		atomic64_inc(&abts_stats->abort_issued_btw_0_to_6_sec);
	else if (abt_issued_time > 6000 && abt_issued_time <= 20000)
		atomic64_inc(&abts_stats->abort_issued_btw_6_to_20_sec);
	else if (abt_issued_time > 20000 && abt_issued_time <= 30000)
		atomic64_inc(&abts_stats->abort_issued_btw_20_to_30_sec);
	else if (abt_issued_time > 30000 && abt_issued_time <= 40000)
		atomic64_inc(&abts_stats->abort_issued_btw_30_to_40_sec);
	else if (abt_issued_time > 40000 && abt_issued_time <= 50000)
		atomic64_inc(&abts_stats->abort_issued_btw_40_to_50_sec);
	else if (abt_issued_time > 50000 && abt_issued_time <= 60000)
		atomic64_inc(&abts_stats->abort_issued_btw_50_to_60_sec);
	else
		atomic64_inc(&abts_stats->abort_issued_greater_than_60_sec);

	old_ioreq_state = io_req->cmd_state;
	io_req->cmd_state = FNIC_IOREQ_ABTS_PENDING;
	io_req->abts_state = FCPIO_INVALID_CODE;

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (!nvfnic_queue_abort_io_req(fnic, io_req->tag, task_req, io_req)) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			      "Abort io req queue failed\n");
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		io_req->cmd_state = old_ioreq_state;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	}
	atomic_dec(&fnic->in_flight);
}

struct
nvme_fc_port_template nvfnic_port = {
	.localport_delete = nvfnic_local_port_delete,
	.remoteport_delete = nvfnic_remote_port_delete,
	.create_queue = nvfnic_create_queue,
	.delete_queue = NULL,
	.ls_req = nvfnic_ls_req_send,
	.ls_abort = nvfnic_ls_req_abort,
	.fcp_io = nvfnic_fcpio_send,
	.fcp_abort = nvfnic_fcpio_abort,
	.max_hw_queues = 1,
	.max_sgl_segments = 256,
	.max_dif_sgl_segments = 64,
	.dma_boundary = 0xFFFFFFFF,
	.local_priv_sz = sizeof(struct fnic_iport_s *),
	.remote_priv_sz = sizeof(struct fnic_tport_s *),
	.lsrqst_priv_sz = sizeof(struct nvfnic_ls_req),
	.fcprqst_priv_sz = sizeof(struct fnic_io_req),
};

void nvfnic_flush_nvme_io_list(struct fnic *fnic)
{
	queue_work(fnic_cmpl_queue, &fnic->nvme_io_cmpl_work);
	flush_work(&fnic->nvme_io_cmpl_work);
}

void nvfnic_nvme_iodone_work(struct work_struct *work)
{
	struct fnic *fnic = container_of(work, struct fnic, nvme_io_cmpl_work);
	struct llist_node *llnode;
	struct fnic_io_req *io_req, *tmp;

	llnode = llist_del_all(&fnic->nvme_io_event_llist);
	llist_for_each_entry_safe(io_req, tmp, llnode, nvfnic_io_cmpl) {
		atomic_dec(&fnic->nvme_io_event_queued);
		atomic64_dec(&fnic->fnic_stats.io_stats.nvme_num_ios_in_waitq);
		io_req->fcp_req->done(io_req->fcp_req);
	}
}

void nvfnic_exch_reset(struct fnic_iport_s *iport, struct fnic_tport_s *tport)
{
	FNIC_NVME_DBG(KERN_DEBUG, iport->fnic,
			"0x%x: Exchange reset scheduled for tport: 0x%x\n",
		    iport->fcid, tport->fcid);

	nvfnic_terminate_tport_ls_reqs(iport->fnic, tport);
	nvfnic_terminate_tport_ios(iport->fnic, tport);
}

void nvfnic_delete_tport(struct fnic_iport_s *iport,
						struct fnic_tport_s *tport,
						unsigned long flags)
{
	struct fnic *fnic = iport->fnic;
	int ret;
	unsigned int time_wait = FNIC_NVME_TPORT_REMOVE_WAIT;
	unsigned int time_remain;
	DECLARE_COMPLETION_ONSTACK(tm_done);
	unsigned int fcid;
	int count = 0;

	if (!tport)
		return;

	fcid = tport->fcid;
	fdls_set_tport_state(tport, FDLS_TGT_STATE_OFFLINE);

	FNIC_NVME_DBG(KERN_DEBUG, fnic,
			"0x%x: scheduled deletion for tport: 0x%x\n",
		    iport->fcid, tport->fcid);

	if (!(tport->flags & FNIC_FDLS_NVME_REGISTERED)) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			"0x%x: tport: 0x%x not registered. Freeing\n",
		    iport->fcid, tport->fcid);
		list_del(&tport->links);
		kfree(tport);
		return;
	}

	tport->tport_del_done = &tm_done;

	tport->flags |= FNIC_FDLS_TPORT_DELETED;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	ret = nvme_fc_unregister_remoteport(tport->nv_rport);
	if (ret) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			    "tport: 0x%x unregister failed %d\n",
			    tport->fcid, ret);

		nvfnic_terminate_tport_ls_reqs(fnic, tport);

		spin_lock_irqsave(&fnic->fnic_lock, flags);
		tport->tport_del_done = NULL;
		if (tport->nv_rport && tport->nv_rport->private == tport)
			tport->nv_rport->private = NULL;
		list_del(&tport->links);
		kfree(tport);
		return;
	}
	time_remain = wait_for_completion_timeout(tport->tport_del_done,
				msecs_to_jiffies(time_wait));

	FNIC_NVME_DBG(KERN_DEBUG, fnic,
		      "tport: 0x%x wait for deletion done\n",
		      tport->fcid);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	tport->tport_del_done = NULL;

	if (!time_remain) {
		FNIC_NVME_DBG(KERN_ERR, fnic,
			    "tport: 0x%x nvme midlayer completion timed out\n",
			    tport->fcid);

		if (tport->flags & FNIC_TPORT_CAN_BE_FREED) {
			kfree(tport);
			FNIC_NVME_DBG(KERN_INFO, fnic,
			      "tport: 0x%x delete complete\n", fcid);
			return;
		}

		tport->flags |= FNIC_FDLS_NVME_TPORT_CLEANUP_PENDING;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		nvfnic_cleanup_tport_io(fnic, tport);
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		tport->flags &= ~FNIC_FDLS_NVME_TPORT_CLEANUP_PENDING;
		return;
	}
	while (!(tport->flags & FNIC_TPORT_CAN_BE_FREED) &&
	       (count < FNIC_TPORT_CLEANUP_WAIT_COUNT)) {
		count++;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		msleep(2000);
		spin_lock_irqsave(&fnic->fnic_lock, flags);
	}
	if (tport->flags & FNIC_TPORT_CAN_BE_FREED)
		kfree(tport);

	FNIC_NVME_DBG(KERN_INFO, fnic,
		      "tport: 0x%x delete complete\n", fcid);
}

int nvfnic_add_tport(struct fnic *fnic, struct fnic_tport_s *tport,
		     unsigned long flags)
{
	struct fnic_iport_s *iport = &fnic->iport;
	struct nvme_fc_port_info pinfo;
	int ret = 0;

	FNIC_NVME_DBG(KERN_INFO, fnic,
		      "Adding tport to nvme wwpn: 0x%llx\n",
		      tport->wwpn);

	memset(&pinfo, 0, sizeof(struct nvme_fc_port_info));

	pinfo.port_name = tport->wwpn;
	pinfo.node_name = tport->wwnn;
	pinfo.port_role = FC_PORT_ROLE_NVME_DISCOVERY | FC_PORT_ROLE_NVME_TARGET;
	pinfo.port_id = tport->fcid;
	pinfo.dev_loss_tmo = nvme_dev_loss_tmo;

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	ret = nvme_fc_register_remoteport(iport->nv_lport, &pinfo,
					  &tport->nv_rport);
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (ret) {
		FNIC_NVME_DBG(KERN_INFO, fnic,
			    "Failed to register tport wwpn: 0x%llx ret: %d\n",
			    tport->wwpn, ret);
		return ret;
	}
	tport->flags |= FNIC_FDLS_NVME_REGISTERED;
	tport->nv_rport->private = tport;

	snprintf(tport->str_wwpn, sizeof(tport->str_wwpn), "0x%llx", tport->wwpn);
	snprintf(tport->str_wwnn, sizeof(tport->str_wwnn), "0x%llx", tport->wwnn);
	return ret;
}

int nvfnic_add_lport(struct fnic *fnic)
{
	struct nvme_fc_port_info pinfo;
	struct fnic_iport_s *iport = &fnic->iport;
	int ret = 0;

	FNIC_NVME_DBG(KERN_INFO, fnic,
		      "Adding lport nvme wwpn: 0x%llx\n",
		      iport->wwpn);

	pinfo.node_name = iport->wwnn;
	pinfo.port_name = iport->wwpn;
	pinfo.port_role = FC_PORT_ROLE_NVME_INITIATOR;
	pinfo.port_id = iport->fcid;

	nvfnic_reset_fcpio_tag_pool(iport);

	if (!(iport->flags & FNIC_LPORT_NVME_REGISTERED)) {
		iport->nv_tmpl = kzalloc_obj(struct nvme_fc_port_template, GFP_ATOMIC);
		if (!iport->nv_tmpl) {
			FNIC_FCS_DBG(KERN_INFO, fnic,
				     "iport:0x%x NVMe tmpl alloc failed\n",
				     iport->fcid);
			return -ENOMEM;
		}
		memcpy(iport->nv_tmpl, &nvfnic_port,
		       sizeof(struct nvme_fc_port_template));
		iport->nv_tmpl->max_hw_queues = fnic->wq_copy_count;

		ret = nvme_fc_register_localport(&pinfo, iport->nv_tmpl,
						 &fnic->pdev->dev, &iport->nv_lport);
		if (ret) {
			FNIC_NVME_DBG(KERN_ERR, fnic,
					"Failed to add wwpn: 0x%llx ret: %d\n",
					iport->wwpn, ret);
			kfree(iport->nv_tmpl);
			return ret;
		}
		iport->flags |= FNIC_LPORT_NVME_REGISTERED;
		iport->nv_lport->private = iport;
	}

	sprintf(iport->str_wwpn, "0x%llx", iport->wwpn);
	sprintf(iport->str_wwnn, "0x%llx", iport->wwnn);

	FNIC_NVME_DBG(KERN_INFO, fnic,
		      "Successfully added lport wwpn: 0x%llx\n",
		      iport->wwpn);
	return 0;
}

#endif
