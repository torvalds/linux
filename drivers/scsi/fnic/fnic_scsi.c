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
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_tcq.h>
#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_fcoe.h>
#include <scsi/fc_frame.h>
#include <scsi/scsi_transport_fc.h>
#include "fnic_io.h"
#include "fnic.h"

static void fnic_cleanup_io(struct fnic *fnic, int exclude_id);

const char *fnic_state_str[] = {
	[FNIC_IN_FC_MODE] =           "FNIC_IN_FC_MODE",
	[FNIC_IN_FC_TRANS_ETH_MODE] = "FNIC_IN_FC_TRANS_ETH_MODE",
	[FNIC_IN_ETH_MODE] =          "FNIC_IN_ETH_MODE",
	[FNIC_IN_ETH_TRANS_FC_MODE] = "FNIC_IN_ETH_TRANS_FC_MODE",
};

static const char *fnic_ioreq_state_str[] = {
	[FNIC_IOREQ_NOT_INITED] = "FNIC_IOREQ_NOT_INITED",
	[FNIC_IOREQ_CMD_PENDING] = "FNIC_IOREQ_CMD_PENDING",
	[FNIC_IOREQ_ABTS_PENDING] = "FNIC_IOREQ_ABTS_PENDING",
	[FNIC_IOREQ_ABTS_COMPLETE] = "FNIC_IOREQ_ABTS_COMPLETE",
	[FNIC_IOREQ_CMD_COMPLETE] = "FNIC_IOREQ_CMD_COMPLETE",
};

static const char *fcpio_status_str[] =  {
	[FCPIO_SUCCESS] = "FCPIO_SUCCESS", /*0x0*/
	[FCPIO_INVALID_HEADER] = "FCPIO_INVALID_HEADER",
	[FCPIO_OUT_OF_RESOURCE] = "FCPIO_OUT_OF_RESOURCE",
	[FCPIO_INVALID_PARAM] = "FCPIO_INVALID_PARAM]",
	[FCPIO_REQ_NOT_SUPPORTED] = "FCPIO_REQ_NOT_SUPPORTED",
	[FCPIO_IO_NOT_FOUND] = "FCPIO_IO_NOT_FOUND",
	[FCPIO_ABORTED] = "FCPIO_ABORTED", /*0x41*/
	[FCPIO_TIMEOUT] = "FCPIO_TIMEOUT",
	[FCPIO_SGL_INVALID] = "FCPIO_SGL_INVALID",
	[FCPIO_MSS_INVALID] = "FCPIO_MSS_INVALID",
	[FCPIO_DATA_CNT_MISMATCH] = "FCPIO_DATA_CNT_MISMATCH",
	[FCPIO_FW_ERR] = "FCPIO_FW_ERR",
	[FCPIO_ITMF_REJECTED] = "FCPIO_ITMF_REJECTED",
	[FCPIO_ITMF_FAILED] = "FCPIO_ITMF_FAILED",
	[FCPIO_ITMF_INCORRECT_LUN] = "FCPIO_ITMF_INCORRECT_LUN",
	[FCPIO_CMND_REJECTED] = "FCPIO_CMND_REJECTED",
	[FCPIO_NO_PATH_AVAIL] = "FCPIO_NO_PATH_AVAIL",
	[FCPIO_PATH_FAILED] = "FCPIO_PATH_FAILED",
	[FCPIO_LUNMAP_CHNG_PEND] = "FCPIO_LUNHMAP_CHNG_PEND",
};

enum terminate_io_return {
	TERM_SUCCESS = 0,
	TERM_NO_SC = 1,
	TERM_IO_REQ_NOT_FOUND,
	TERM_ANOTHER_PORT,
	TERM_GSTATE,
	TERM_IO_BLOCKED,
	TERM_OUT_OF_WQ_DESC,
	TERM_TIMED_OUT,
	TERM_MISC,
};

const char *fnic_state_to_str(unsigned int state)
{
	if (state >= ARRAY_SIZE(fnic_state_str) || !fnic_state_str[state])
		return "unknown";

	return fnic_state_str[state];
}

static const char *fnic_ioreq_state_to_str(unsigned int state)
{
	if (state >= ARRAY_SIZE(fnic_ioreq_state_str) ||
	    !fnic_ioreq_state_str[state])
		return "unknown";

	return fnic_ioreq_state_str[state];
}

static const char *fnic_fcpio_status_to_str(unsigned int status)
{
	if (status >= ARRAY_SIZE(fcpio_status_str) || !fcpio_status_str[status])
		return "unknown";

	return fcpio_status_str[status];
}

/*
 * Unmap the data buffer and sense buffer for an io_req,
 * also unmap and free the device-private scatter/gather list.
 */
static void fnic_release_ioreq_buf(struct fnic *fnic,
				   struct fnic_io_req *io_req,
				   struct scsi_cmnd *sc)
{
	if (io_req->sgl_list_pa)
		dma_unmap_single(&fnic->pdev->dev, io_req->sgl_list_pa,
				 sizeof(io_req->sgl_list[0]) * io_req->sgl_cnt,
				 DMA_TO_DEVICE);
	scsi_dma_unmap(sc);

	if (io_req->sgl_cnt)
		mempool_free(io_req->sgl_list_alloc,
			     fnic->io_sgl_pool[io_req->sgl_type]);
	if (io_req->sense_buf_pa)
		dma_unmap_single(&fnic->pdev->dev, io_req->sense_buf_pa,
				 SCSI_SENSE_BUFFERSIZE, DMA_FROM_DEVICE);
}

static bool
fnic_count_portid_ioreqs_iter(struct fnic *fnic, struct scsi_cmnd *sc,
				void *data1, void *data2)
{
	u32 *portid = data1;
	unsigned int *count = data2;
	struct fnic_io_req *io_req = fnic_priv(sc)->io_req;

	if (!io_req || (*portid && (io_req->port_id != *portid)))
		return true;

	*count += 1;
	return true;
}

unsigned int fnic_count_ioreqs(struct fnic *fnic, u32 portid)
{
	unsigned int count = 0;

	fnic_scsi_io_iter(fnic, fnic_count_portid_ioreqs_iter,
				&portid, &count);

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		      "portid = 0x%x count = %u\n", portid, count);
	return count;
}

unsigned int fnic_count_all_ioreqs(struct fnic *fnic)
{
	return fnic_count_ioreqs(fnic, 0);
}

static bool
fnic_count_lun_ioreqs_iter(struct fnic *fnic, struct scsi_cmnd *sc,
				void *data1, void *data2)
{
	struct scsi_device *scsi_device = data1;
	unsigned int *count = data2;

	if (sc->device != scsi_device || !fnic_priv(sc)->io_req)
		return true;

	*count += 1;
	return true;
}

unsigned int
fnic_count_lun_ioreqs(struct fnic *fnic, struct scsi_device *scsi_device)
{
	unsigned int count = 0;

	fnic_scsi_io_iter(fnic, fnic_count_lun_ioreqs_iter,
				scsi_device, &count);

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		      "lun = %p count = %u\n", scsi_device, count);
	return count;
}

/* Free up Copy Wq descriptors. Called with copy_wq lock held */
static int free_wq_copy_descs(struct fnic *fnic, struct vnic_wq_copy *wq, unsigned int hwq)
{
	/* if no Ack received from firmware, then nothing to clean */
	if (!fnic->fw_ack_recd[hwq])
		return 1;

	/*
	 * Update desc_available count based on number of freed descriptors
	 * Account for wraparound
	 */
	if (wq->to_clean_index <= fnic->fw_ack_index[hwq])
		wq->ring.desc_avail += (fnic->fw_ack_index[hwq]
					- wq->to_clean_index + 1);
	else
		wq->ring.desc_avail += (wq->ring.desc_count
					- wq->to_clean_index
					+ fnic->fw_ack_index[hwq] + 1);

	/*
	 * just bump clean index to ack_index+1 accounting for wraparound
	 * this will essentially free up all descriptors between
	 * to_clean_index and fw_ack_index, both inclusive
	 */
	wq->to_clean_index =
		(fnic->fw_ack_index[hwq] + 1) % wq->ring.desc_count;

	/* we have processed the acks received so far */
	fnic->fw_ack_recd[hwq] = 0;
	return 0;
}


/*
 * __fnic_set_state_flags
 * Sets/Clears bits in fnic's state_flags
 **/
void
__fnic_set_state_flags(struct fnic *fnic, unsigned long st_flags,
			unsigned long clearbits)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	if (clearbits)
		fnic->state_flags &= ~st_flags;
	else
		fnic->state_flags |= st_flags;

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	return;
}


/*
 * fnic_fw_reset_handler
 * Routine to send reset msg to fw
 */
int fnic_fw_reset_handler(struct fnic *fnic)
{
	struct vnic_wq_copy *wq = &fnic->hw_copy_wq[0];
	int ret = 0;
	unsigned long flags;
	unsigned int ioreq_count;

	/* indicate fwreset to io path */
	fnic_set_state_flags(fnic, FNIC_FLAGS_FWRESET);
	ioreq_count = fnic_count_all_ioreqs(fnic);

	/* wait for io cmpl */
	while (atomic_read(&fnic->in_flight))
		schedule_timeout(msecs_to_jiffies(1));

	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq, 0);

	if (!vnic_wq_copy_desc_avail(wq))
		ret = -EAGAIN;
	else {
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			  "ioreq_count: %u\n", ioreq_count);
		fnic_queue_wq_copy_desc_fw_reset(wq, SCSI_NO_TAG);
		atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
		if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
			  atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
			atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
				atomic64_read(
				  &fnic->fnic_stats.fw_stats.active_fw_reqs));
	}

	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);

	if (!ret) {
		atomic64_inc(&fnic->fnic_stats.reset_stats.fw_resets);
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				"Issued fw reset\n");
	} else {
		fnic_clear_state_flags(fnic, FNIC_FLAGS_FWRESET);
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"Failed to issue fw reset\n");
	}

	return ret;
}


/*
 * fnic_flogi_reg_handler
 * Routine to send flogi register msg to fw
 */
int fnic_flogi_reg_handler(struct fnic *fnic, u32 fc_id)
{
	struct vnic_wq_copy *wq = &fnic->hw_copy_wq[0];
	enum fcpio_flogi_reg_format_type format;
	u8 gw_mac[ETH_ALEN];
	int ret = 0;
	unsigned long flags;
	struct fnic_iport_s *iport = &fnic->iport;

	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq, 0);

	if (!vnic_wq_copy_desc_avail(wq)) {
		ret = -EAGAIN;
		goto flogi_reg_ioreq_end;
	}

	memcpy(gw_mac, fnic->iport.fcfmac, ETH_ALEN);
	format = FCPIO_FLOGI_REG_GW_DEST;

	if (fnic->config.flags & VFCF_FIP_CAPABLE) {
		fnic_queue_wq_copy_desc_fip_reg(wq, SCSI_NO_TAG,
						fc_id, gw_mac,
						fnic->iport.fpma,
						iport->r_a_tov, iport->e_d_tov);
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			      "FLOGI FIP reg issued fcid: 0x%x src %p dest %p\n",
				  fc_id, fnic->iport.fpma, gw_mac);
	} else {
		fnic_queue_wq_copy_desc_flogi_reg(wq, SCSI_NO_TAG,
						  format, fc_id, gw_mac);
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"FLOGI reg issued fcid 0x%x dest %p\n",
			fc_id, gw_mac);
	}

	atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
	if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
		  atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
		atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
		  atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

flogi_reg_ioreq_end:
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);
	return ret;
}

/*
 * fnic_queue_wq_copy_desc
 * Routine to enqueue a wq copy desc
 */
static inline int fnic_queue_wq_copy_desc(struct fnic *fnic,
					  struct vnic_wq_copy *wq,
					  struct fnic_io_req *io_req,
					  struct scsi_cmnd *sc,
					  int sg_count,
					  uint32_t mqtag,
					  uint16_t hwq)
{
	struct scatterlist *sg;
	struct fc_rport *rport = starget_to_rport(scsi_target(sc->device));
	struct host_sg_desc *desc;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	unsigned int i;
	int flags;
	u8 exch_flags;
	struct scsi_lun fc_lun;
	struct fnic_tport_s *tport;
	struct rport_dd_data_s *rdd_data;

	rdd_data = rport->dd_data;
	tport = rdd_data->tport;

	if (sg_count) {
		/* For each SGE, create a device desc entry */
		desc = io_req->sgl_list;
		for_each_sg(scsi_sglist(sc), sg, sg_count, i) {
			desc->addr = cpu_to_le64(sg_dma_address(sg));
			desc->len = cpu_to_le32(sg_dma_len(sg));
			desc->_resvd = 0;
			desc++;
		}

		io_req->sgl_list_pa = dma_map_single(&fnic->pdev->dev,
				io_req->sgl_list,
				sizeof(io_req->sgl_list[0]) * sg_count,
				DMA_TO_DEVICE);
		if (dma_mapping_error(&fnic->pdev->dev, io_req->sgl_list_pa)) {
			printk(KERN_ERR "DMA mapping failed\n");
			return SCSI_MLQUEUE_HOST_BUSY;
		}
	}

	io_req->sense_buf_pa = dma_map_single(&fnic->pdev->dev,
					      sc->sense_buffer,
					      SCSI_SENSE_BUFFERSIZE,
					      DMA_FROM_DEVICE);
	if (dma_mapping_error(&fnic->pdev->dev, io_req->sense_buf_pa)) {
		dma_unmap_single(&fnic->pdev->dev, io_req->sgl_list_pa,
				sizeof(io_req->sgl_list[0]) * sg_count,
				DMA_TO_DEVICE);
		printk(KERN_ERR "DMA mapping failed\n");
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	int_to_scsilun(sc->device->lun, &fc_lun);

	/* Enqueue the descriptor in the Copy WQ */
	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[hwq])
		free_wq_copy_descs(fnic, wq, hwq);

	if (unlikely(!vnic_wq_copy_desc_avail(wq))) {
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			  "fnic_queue_wq_copy_desc failure - no descriptors\n");
		atomic64_inc(&misc_stats->io_cpwq_alloc_failures);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	flags = 0;
	if (sc->sc_data_direction == DMA_FROM_DEVICE)
		flags = FCPIO_ICMND_RDDATA;
	else if (sc->sc_data_direction == DMA_TO_DEVICE)
		flags = FCPIO_ICMND_WRDATA;

	exch_flags = 0;
	if ((fnic->config.flags & VFCF_FCP_SEQ_LVL_ERR) &&
		(tport->tgt_flags & FDLS_FC_RP_FLAGS_RETRY))
		exch_flags |= FCPIO_ICMND_SRFLAG_RETRY;

	fnic_queue_wq_copy_desc_icmnd_16(wq, mqtag,
					 0, exch_flags, io_req->sgl_cnt,
					 SCSI_SENSE_BUFFERSIZE,
					 io_req->sgl_list_pa,
					 io_req->sense_buf_pa,
					 0, /* scsi cmd ref, always 0 */
					 FCPIO_ICMND_PTA_SIMPLE,
					 	/* scsi pri and tag */
					 flags,	/* command flags */
					 sc->cmnd, sc->cmd_len,
					 scsi_bufflen(sc),
					 fc_lun.scsi_lun, io_req->port_id,
					 tport->max_payload_size,
					 tport->r_a_tov, tport->e_d_tov);

	atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
	if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
		  atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
		atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
		  atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

	return 0;
}

int fnic_queuecommand(struct Scsi_Host *shost, struct scsi_cmnd *sc)
{
	struct request *const rq = scsi_cmd_to_rq(sc);
	uint32_t mqtag = 0;
	void (*done)(struct scsi_cmnd *) = scsi_done;
	struct fc_rport *rport;
	struct fnic_io_req *io_req = NULL;
	struct fnic *fnic = *((struct fnic **) shost_priv(sc->device->host));
	struct fnic_iport_s *iport = NULL;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	struct vnic_wq_copy *wq;
	int ret = 1;
	u64 cmd_trace;
	int sg_count = 0;
	unsigned long flags = 0;
	unsigned long ptr;
	int io_lock_acquired = 0;
	uint16_t hwq = 0;
	struct fnic_tport_s *tport = NULL;
	struct rport_dd_data_s *rdd_data;
	uint16_t lun0_delay = 0;

	rport = starget_to_rport(scsi_target(sc->device));
	if (!rport) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"returning DID_NO_CONNECT for IO as rport is NULL\n");
		sc->result = DID_NO_CONNECT << 16;
		done(sc);
		return 0;
	}

	ret = fc_remote_port_chkready(rport);
	if (ret) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"rport is not ready\n");
		atomic64_inc(&fnic_stats->misc_stats.tport_not_ready);
		sc->result = ret;
		done(sc);
		return 0;
	}

	mqtag = blk_mq_unique_tag(rq);
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	iport = &fnic->iport;

	if (iport->state != FNIC_IPORT_STATE_READY) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
					  "returning DID_NO_CONNECT for IO as iport state: %d\n",
					  iport->state);
		sc->result = DID_NO_CONNECT << 16;
		done(sc);
		return 0;
	}

	/* fc_remote_port_add() may have added the tport to
	 * fc_transport but dd_data not yet set
	 */
	rdd_data = rport->dd_data;
	tport = rdd_data->tport;
	if (!tport || (rdd_data->iport != iport)) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
					  "dd_data not yet set in SCSI for rport portid: 0x%x\n",
					  rport->port_id);
		tport = fnic_find_tport_by_fcid(iport, rport->port_id);
		if (!tport) {
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
						  "returning DID_BUS_BUSY for IO as tport not found for: 0x%x\n",
						  rport->port_id);
			sc->result = DID_BUS_BUSY << 16;
			done(sc);
			return 0;
		}

		/* Re-assign same params as in fnic_fdls_add_tport */
		rport->maxframe_size = FNIC_FC_MAX_PAYLOAD_LEN;
		rport->supported_classes =
			FC_COS_CLASS3 | FC_RPORT_ROLE_FCP_TARGET;
		/* the dd_data is allocated by fctransport of size dd_fcrport_size */
		rdd_data = rport->dd_data;
		rdd_data->tport = tport;
		rdd_data->iport = iport;
		tport->rport = rport;
		tport->flags |= FNIC_FDLS_SCSI_REGISTERED;
	}

	if ((tport->state != FDLS_TGT_STATE_READY)
		&& (tport->state != FDLS_TGT_STATE_ADISC)) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
					  "returning DID_NO_CONNECT for IO as tport state: %d\n",
					  tport->state);
		sc->result = DID_NO_CONNECT << 16;
		done(sc);
		return 0;
	}

	atomic_inc(&fnic->in_flight);
	atomic_inc(&tport->in_flight);

	if (unlikely(fnic_chk_state_flags_locked(fnic, FNIC_FLAGS_IO_BLOCKED))) {
		atomic_dec(&fnic->in_flight);
		atomic_dec(&tport->in_flight);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	if (unlikely(fnic_chk_state_flags_locked(fnic, FNIC_FLAGS_FWRESET))) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		  "fnic flags FW reset: 0x%lx. Returning SCSI_MLQUEUE_HOST_BUSY\n",
		  fnic->state_flags);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	if (!tport->lun0_delay) {
		lun0_delay = 1;
		tport->lun0_delay++;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	fnic_priv(sc)->state = FNIC_IOREQ_NOT_INITED;
	fnic_priv(sc)->flags = FNIC_NO_FLAGS;

	/* Get a new io_req for this SCSI IO */
	io_req = mempool_alloc(fnic->io_req_pool, GFP_ATOMIC);
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.alloc_failures);
		ret = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}
	memset(io_req, 0, sizeof(*io_req));

	/* Map the data buffer */
	sg_count = scsi_dma_map(sc);
	if (sg_count < 0) {
		FNIC_TRACE(fnic_queuecommand, sc->device->host->host_no,
			  mqtag, sc, 0, sc->cmnd[0], sg_count, fnic_priv(sc)->state);
		mempool_free(io_req, fnic->io_req_pool);
		goto out;
	}

	io_req->tport = tport;
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
			ret = SCSI_MLQUEUE_HOST_BUSY;
			scsi_dma_unmap(sc);
			mempool_free(io_req, fnic->io_req_pool);
			goto out;
		}

		/* Cache sgl list allocated address before alignment */
		io_req->sgl_list_alloc = io_req->sgl_list;
		ptr = (unsigned long) io_req->sgl_list;
		if (ptr % FNIC_SG_DESC_ALIGN) {
			io_req->sgl_list = (struct host_sg_desc *)
				(((unsigned long) ptr
				  + FNIC_SG_DESC_ALIGN - 1)
				 & ~(FNIC_SG_DESC_ALIGN - 1));
		}
	}

	/*
	* Will acquire lock before setting to IO initialized.
	*/
	hwq = blk_mq_unique_tag_to_hwq(mqtag);
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);

	/* initialize rest of io_req */
	io_lock_acquired = 1;
	io_req->port_id = rport->port_id;
	io_req->start_time = jiffies;
	fnic_priv(sc)->state = FNIC_IOREQ_CMD_PENDING;
	fnic_priv(sc)->io_req = io_req;
	fnic_priv(sc)->flags |= FNIC_IO_INITIALIZED;
	io_req->sc = sc;

	if (fnic->sw_copy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(mqtag)] != NULL) {
		WARN(1, "fnic<%d>: %s: hwq: %d tag 0x%x already exists\n",
				fnic->fnic_num, __func__, hwq, blk_mq_unique_tag_to_tag(mqtag));
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	fnic->sw_copy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(mqtag)] = io_req;
	io_req->tag = mqtag;

	/* create copy wq desc and enqueue it */
	wq = &fnic->hw_copy_wq[hwq];
	atomic64_inc(&fnic_stats->io_stats.ios[hwq]);
	ret = fnic_queue_wq_copy_desc(fnic, wq, io_req, sc, sg_count, mqtag, hwq);
	if (ret) {
		/*
		 * In case another thread cancelled the request,
		 * refetch the pointer under the lock.
		 */
		FNIC_TRACE(fnic_queuecommand, sc->device->host->host_no,
			  mqtag, sc, 0, 0, 0, fnic_flags_and_state(sc));
		io_req = fnic_priv(sc)->io_req;
		fnic_priv(sc)->io_req = NULL;
		if (io_req)
			fnic->sw_copy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(mqtag)] = NULL;
		fnic_priv(sc)->state = FNIC_IOREQ_CMD_COMPLETE;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		if (io_req) {
			fnic_release_ioreq_buf(fnic, io_req, sc);
			mempool_free(io_req, fnic->io_req_pool);
		}
		atomic_dec(&fnic->in_flight);
		atomic_dec(&tport->in_flight);
		return ret;
	} else {
		atomic64_inc(&fnic_stats->io_stats.active_ios);
		atomic64_inc(&fnic_stats->io_stats.num_ios);
		if (atomic64_read(&fnic_stats->io_stats.active_ios) >
			  atomic64_read(&fnic_stats->io_stats.max_active_ios))
			atomic64_set(&fnic_stats->io_stats.max_active_ios,
			     atomic64_read(&fnic_stats->io_stats.active_ios));

		/* REVISIT: Use per IO lock in the final code */
		fnic_priv(sc)->flags |= FNIC_IO_ISSUED;
	}
out:
	cmd_trace = ((u64)sc->cmnd[0] << 56 | (u64)sc->cmnd[7] << 40 |
			(u64)sc->cmnd[8] << 32 | (u64)sc->cmnd[2] << 24 |
			(u64)sc->cmnd[3] << 16 | (u64)sc->cmnd[4] << 8 |
			sc->cmnd[5]);

	FNIC_TRACE(fnic_queuecommand, sc->device->host->host_no,
		   mqtag, sc, io_req, sg_count, cmd_trace,
		   fnic_flags_and_state(sc));

	/* if only we issued IO, will we have the io lock */
	if (io_lock_acquired)
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	atomic_dec(&fnic->in_flight);
	atomic_dec(&tport->in_flight);

	if (lun0_delay) {
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					  "LUN0 delay\n");
		mdelay(LUN0_DELAY_TIME);
	}

	return ret;
}


/*
 * fnic_fcpio_fw_reset_cmpl_handler
 * Routine to handle fw reset completion
 */
static int fnic_fcpio_fw_reset_cmpl_handler(struct fnic *fnic,
					    struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag tag;
	int ret = 0;
	unsigned long flags;
	struct reset_stats *reset_stats = &fnic->fnic_stats.reset_stats;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);

	atomic64_inc(&reset_stats->fw_reset_completions);

	/* Clean up all outstanding io requests */
	fnic_cleanup_io(fnic, SCSI_NO_TAG);

	atomic64_set(&fnic->fnic_stats.fw_stats.active_fw_reqs, 0);
	atomic64_set(&fnic->fnic_stats.io_stats.active_ios, 0);
	atomic64_set(&fnic->io_cmpl_skip, 0);

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	/* fnic should be in FC_TRANS_ETH_MODE */
	if (fnic->state == FNIC_IN_FC_TRANS_ETH_MODE) {
		/* Check status of reset completion */
		if (!hdr_status) {
			FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					"reset cmpl success\n");
			/* Ready to send flogi out */
			fnic->state = FNIC_IN_ETH_MODE;
		} else {
			FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				"reset failed with header status: %s\n",
				fnic_fcpio_status_to_str(hdr_status));

			fnic->state = FNIC_IN_FC_MODE;
			atomic64_inc(&reset_stats->fw_reset_failures);
			ret = -1;
		}
	} else {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"Unexpected state while processing reset completion: %s\n",
			fnic_state_to_str(fnic->state));
		atomic64_inc(&reset_stats->fw_reset_failures);
		ret = -1;
	}

	if (fnic->fw_reset_done)
		complete(fnic->fw_reset_done);

	/*
	 * If fnic is being removed, or fw reset failed
	 * free the flogi frame. Else, send it out
	 */
	if (ret) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		fnic_free_txq(&fnic->tx_queue);
		goto reset_cmpl_handler_end;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	queue_work(fnic_event_queue, &fnic->flush_work);

 reset_cmpl_handler_end:
	fnic_clear_state_flags(fnic, FNIC_FLAGS_FWRESET);

	return ret;
}

/*
 * fnic_fcpio_flogi_reg_cmpl_handler
 * Routine to handle flogi register completion
 */
static int fnic_fcpio_flogi_reg_cmpl_handler(struct fnic *fnic,
					     struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag tag;
	int ret = 0;
	unsigned long flags;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);

	/* Update fnic state based on status of flogi reg completion */
	spin_lock_irqsave(&fnic->fnic_lock, flags);

	if (fnic->state == FNIC_IN_ETH_TRANS_FC_MODE) {

		/* Check flogi registration completion status */
		if (!hdr_status) {
			FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
				      "FLOGI reg succeeded\n");
			fnic->state = FNIC_IN_FC_MODE;
		} else {
			FNIC_SCSI_DBG(KERN_DEBUG,
				      fnic->host, fnic->fnic_num,
				      "fnic flogi reg failed: %s\n",
				      fnic_fcpio_status_to_str(hdr_status));
			fnic->state = FNIC_IN_ETH_MODE;
			ret = -1;
		}
	} else {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
			      "Unexpected fnic state %s while"
			      " processing flogi reg completion\n",
			      fnic_state_to_str(fnic->state));
		ret = -1;
	}

	if (!ret) {
		if (fnic->stop_rx_link_events) {
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			goto reg_cmpl_handler_end;
		}
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);

		queue_work(fnic_event_queue, &fnic->flush_work);
		queue_work(fnic_event_queue, &fnic->frame_work);
	} else {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	}

reg_cmpl_handler_end:
	return ret;
}

static inline int is_ack_index_in_range(struct vnic_wq_copy *wq,
					u16 request_out)
{
	if (wq->to_clean_index <= wq->to_use_index) {
		/* out of range, stale request_out index */
		if (request_out < wq->to_clean_index ||
		    request_out >= wq->to_use_index)
			return 0;
	} else {
		/* out of range, stale request_out index */
		if (request_out < wq->to_clean_index &&
		    request_out >= wq->to_use_index)
			return 0;
	}
	/* request_out index is in range */
	return 1;
}


/*
 * Mark that ack received and store the Ack index. If there are multiple
 * acks received before Tx thread cleans it up, the latest value will be
 * used which is correct behavior. This state should be in the copy Wq
 * instead of in the fnic
 */
static inline void fnic_fcpio_ack_handler(struct fnic *fnic,
					  unsigned int cq_index,
					  struct fcpio_fw_req *desc)
{
	struct vnic_wq_copy *wq;
	u16 request_out = desc->u.ack.request_out;
	unsigned long flags;
	u64 *ox_id_tag = (u64 *)(void *)desc;
	unsigned int wq_index = cq_index;

	/* mark the ack state */
	wq = &fnic->hw_copy_wq[cq_index];
	spin_lock_irqsave(&fnic->wq_copy_lock[wq_index], flags);

	fnic->fnic_stats.misc_stats.last_ack_time = jiffies;
	if (is_ack_index_in_range(wq, request_out)) {
		fnic->fw_ack_index[wq_index] = request_out;
		fnic->fw_ack_recd[wq_index] = 1;
	} else
		atomic64_inc(
			&fnic->fnic_stats.misc_stats.ack_index_out_of_range);

	spin_unlock_irqrestore(&fnic->wq_copy_lock[wq_index], flags);
	FNIC_TRACE(fnic_fcpio_ack_handler,
		  fnic->host->host_no, 0, 0, ox_id_tag[2], ox_id_tag[3],
		  ox_id_tag[4], ox_id_tag[5]);
}

/*
 * fnic_fcpio_icmnd_cmpl_handler
 * Routine to handle icmnd completions
 */
static void fnic_fcpio_icmnd_cmpl_handler(struct fnic *fnic, unsigned int cq_index,
					 struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag ftag;
	u32 id;
	u64 xfer_len = 0;
	struct fcpio_icmnd_cmpl *icmnd_cmpl;
	struct fnic_io_req *io_req;
	struct scsi_cmnd *sc;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	unsigned long flags;
	u64 cmd_trace;
	unsigned long start_time;
	unsigned long io_duration_time;
	unsigned int hwq = 0;
	unsigned int mqtag = 0;
	unsigned int tag = 0;

	/* Decode the cmpl description to get the io_req id */
	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &ftag);
	fcpio_tag_id_dec(&ftag, &id);
	icmnd_cmpl = &desc->u.icmnd_cmpl;

	mqtag = id;
	tag = blk_mq_unique_tag_to_tag(mqtag);
	hwq = blk_mq_unique_tag_to_hwq(mqtag);

	if (hwq != cq_index) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hwq: %d mqtag: 0x%x tag: 0x%x cq index: %d ",
			hwq, mqtag, tag, cq_index);
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hdr status: %s icmnd completion on the wrong queue\n",
			fnic_fcpio_status_to_str(hdr_status));
	}

	if (tag >= fnic->fnic_max_tag_id) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hwq: %d mqtag: 0x%x tag: 0x%x cq index: %d ",
			hwq, mqtag, tag, cq_index);
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hdr status: %s Out of range tag\n",
			fnic_fcpio_status_to_str(hdr_status));
		return;
	}
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);

	sc = scsi_host_find_tag(fnic->host, id);
	WARN_ON_ONCE(!sc);
	if (!sc) {
		atomic64_inc(&fnic_stats->io_stats.sc_null);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		shost_printk(KERN_ERR, fnic->host,
			  "icmnd_cmpl sc is null - "
			  "hdr status = %s tag = 0x%x desc = 0x%p\n",
			  fnic_fcpio_status_to_str(hdr_status), id, desc);
		FNIC_TRACE(fnic_fcpio_icmnd_cmpl_handler,
			  fnic->host->host_no, id,
			  ((u64)icmnd_cmpl->_resvd0[1] << 16 |
			  (u64)icmnd_cmpl->_resvd0[0]),
			  ((u64)hdr_status << 16 |
			  (u64)icmnd_cmpl->scsi_status << 8 |
			  (u64)icmnd_cmpl->flags), desc,
			  (u64)icmnd_cmpl->residual, 0);
		return;
	}

	io_req = fnic_priv(sc)->io_req;
	if (fnic->sw_copy_wq[hwq].io_req_table[tag] != io_req) {
		WARN(1, "%s: %d: hwq: %d mqtag: 0x%x tag: 0x%x io_req tag mismatch\n",
			__func__, __LINE__, hwq, mqtag, tag);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		return;
	}

	WARN_ON_ONCE(!io_req);
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.ioreq_null);
		fnic_priv(sc)->flags |= FNIC_IO_REQ_NULL;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		shost_printk(KERN_ERR, fnic->host,
			  "icmnd_cmpl io_req is null - "
			  "hdr status = %s tag = 0x%x sc 0x%p\n",
			  fnic_fcpio_status_to_str(hdr_status), id, sc);
		return;
	}
	start_time = io_req->start_time;

	/* firmware completed the io */
	io_req->io_completed = 1;

	/*
	 *  if SCSI-ML has already issued abort on this command,
	 *  set completion of the IO. The abts path will clean it up
	 */
	if (fnic_priv(sc)->state == FNIC_IOREQ_ABTS_PENDING) {

		/*
		 * set the FNIC_IO_DONE so that this doesn't get
		 * flagged as 'out of order' if it was not aborted
		 */
		fnic_priv(sc)->flags |= FNIC_IO_DONE;
		fnic_priv(sc)->flags |= FNIC_IO_ABTS_PENDING;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		if(FCPIO_ABORTED == hdr_status)
			fnic_priv(sc)->flags |= FNIC_IO_ABORTED;

		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"icmnd_cmpl abts pending "
			  "hdr status = %s tag = 0x%x sc = 0x%p "
			  "scsi_status = %x residual = %d\n",
			  fnic_fcpio_status_to_str(hdr_status),
			  id, sc,
			  icmnd_cmpl->scsi_status,
			  icmnd_cmpl->residual);
		return;
	}

	/* Mark the IO as complete */
	fnic_priv(sc)->state = FNIC_IOREQ_CMD_COMPLETE;

	icmnd_cmpl = &desc->u.icmnd_cmpl;

	switch (hdr_status) {
	case FCPIO_SUCCESS:
		sc->result = (DID_OK << 16) | icmnd_cmpl->scsi_status;
		xfer_len = scsi_bufflen(sc);

		if (icmnd_cmpl->flags & FCPIO_ICMND_CMPL_RESID_UNDER) {
			xfer_len -= icmnd_cmpl->residual;
			scsi_set_resid(sc, icmnd_cmpl->residual);
		}

		if (icmnd_cmpl->scsi_status == SAM_STAT_CHECK_CONDITION)
			atomic64_inc(&fnic_stats->misc_stats.check_condition);

		if (icmnd_cmpl->scsi_status == SAM_STAT_TASK_SET_FULL)
			atomic64_inc(&fnic_stats->misc_stats.queue_fulls);

		FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
				"xfer_len: %llu", xfer_len);
		break;

	case FCPIO_TIMEOUT:          /* request was timed out */
		atomic64_inc(&fnic_stats->misc_stats.fcpio_timeout);
		sc->result = (DID_TIME_OUT << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_ABORTED:          /* request was aborted */
		atomic64_inc(&fnic_stats->misc_stats.fcpio_aborted);
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_DATA_CNT_MISMATCH: /* recv/sent more/less data than exp. */
		atomic64_inc(&fnic_stats->misc_stats.data_count_mismatch);
		scsi_set_resid(sc, icmnd_cmpl->residual);
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_OUT_OF_RESOURCE:  /* out of resources to complete request */
		atomic64_inc(&fnic_stats->fw_stats.fw_out_of_resources);
		sc->result = (DID_REQUEUE << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_IO_NOT_FOUND:     /* requested I/O was not found */
		atomic64_inc(&fnic_stats->io_stats.io_not_found);
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_SGL_INVALID:      /* request was aborted due to sgl error */
		atomic64_inc(&fnic_stats->misc_stats.sgl_invalid);
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_FW_ERR:           /* request was terminated due fw error */
		atomic64_inc(&fnic_stats->fw_stats.io_fw_errs);
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_MSS_INVALID:      /* request was aborted due to mss error */
		atomic64_inc(&fnic_stats->misc_stats.mss_invalid);
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_INVALID_HEADER:   /* header contains invalid data */
	case FCPIO_INVALID_PARAM:    /* some parameter in request invalid */
	case FCPIO_REQ_NOT_SUPPORTED:/* request type is not supported */
	default:
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;
	}

	/* Break link with the SCSI command */
	fnic_priv(sc)->io_req = NULL;
	io_req->sc = NULL;
	fnic_priv(sc)->flags |= FNIC_IO_DONE;
	fnic->sw_copy_wq[hwq].io_req_table[tag] = NULL;

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	if (hdr_status != FCPIO_SUCCESS) {
		atomic64_inc(&fnic_stats->io_stats.io_failures);
		shost_printk(KERN_ERR, fnic->host, "hdr status = %s\n",
			     fnic_fcpio_status_to_str(hdr_status));
	}

	fnic_release_ioreq_buf(fnic, io_req, sc);

	cmd_trace = ((u64)hdr_status << 56) |
		  (u64)icmnd_cmpl->scsi_status << 48 |
		  (u64)icmnd_cmpl->flags << 40 | (u64)sc->cmnd[0] << 32 |
		  (u64)sc->cmnd[2] << 24 | (u64)sc->cmnd[3] << 16 |
		  (u64)sc->cmnd[4] << 8 | sc->cmnd[5];

	FNIC_TRACE(fnic_fcpio_icmnd_cmpl_handler,
		  sc->device->host->host_no, id, sc,
		  ((u64)icmnd_cmpl->_resvd0[1] << 56 |
		  (u64)icmnd_cmpl->_resvd0[0] << 48 |
		  jiffies_to_msecs(jiffies - start_time)),
		  desc, cmd_trace, fnic_flags_and_state(sc));

	if (sc->sc_data_direction == DMA_FROM_DEVICE) {
		fnic_stats->host_stats.fcp_input_requests++;
		fnic->fcp_input_bytes += xfer_len;
	} else if (sc->sc_data_direction == DMA_TO_DEVICE) {
		fnic_stats->host_stats.fcp_output_requests++;
		fnic->fcp_output_bytes += xfer_len;
	} else
		fnic_stats->host_stats.fcp_control_requests++;

	/* Call SCSI completion function to complete the IO */
	scsi_done(sc);

	mempool_free(io_req, fnic->io_req_pool);

	atomic64_dec(&fnic_stats->io_stats.active_ios);
	if (atomic64_read(&fnic->io_cmpl_skip))
		atomic64_dec(&fnic->io_cmpl_skip);
	else
		atomic64_inc(&fnic_stats->io_stats.io_completions);


	io_duration_time = jiffies_to_msecs(jiffies) -
						jiffies_to_msecs(start_time);

	if(io_duration_time <= 10)
		atomic64_inc(&fnic_stats->io_stats.io_btw_0_to_10_msec);
	else if(io_duration_time <= 100)
		atomic64_inc(&fnic_stats->io_stats.io_btw_10_to_100_msec);
	else if(io_duration_time <= 500)
		atomic64_inc(&fnic_stats->io_stats.io_btw_100_to_500_msec);
	else if(io_duration_time <= 5000)
		atomic64_inc(&fnic_stats->io_stats.io_btw_500_to_5000_msec);
	else if(io_duration_time <= 10000)
		atomic64_inc(&fnic_stats->io_stats.io_btw_5000_to_10000_msec);
	else if(io_duration_time <= 30000)
		atomic64_inc(&fnic_stats->io_stats.io_btw_10000_to_30000_msec);
	else {
		atomic64_inc(&fnic_stats->io_stats.io_greater_than_30000_msec);

		if(io_duration_time > atomic64_read(&fnic_stats->io_stats.current_max_io_time))
			atomic64_set(&fnic_stats->io_stats.current_max_io_time, io_duration_time);
	}
}

/* fnic_fcpio_itmf_cmpl_handler
 * Routine to handle itmf completions
 */
static void fnic_fcpio_itmf_cmpl_handler(struct fnic *fnic, unsigned int cq_index,
					struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag ftag;
	u32 id;
	struct scsi_cmnd *sc = NULL;
	struct fnic_io_req *io_req;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	struct abort_stats *abts_stats = &fnic->fnic_stats.abts_stats;
	struct terminate_stats *term_stats = &fnic->fnic_stats.term_stats;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	unsigned long flags;
	unsigned long start_time;
	unsigned int hwq = cq_index;
	unsigned int mqtag;
	unsigned int tag;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &ftag);
	fcpio_tag_id_dec(&ftag, &id);

	mqtag = id & FNIC_TAG_MASK;
	tag = blk_mq_unique_tag_to_tag(id & FNIC_TAG_MASK);
	hwq = blk_mq_unique_tag_to_hwq(id & FNIC_TAG_MASK);

	if (hwq != cq_index) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hwq: %d mqtag: 0x%x tag: 0x%x cq index: %d ",
			hwq, mqtag, tag, cq_index);
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hdr status: %s ITMF completion on the wrong queue\n",
			fnic_fcpio_status_to_str(hdr_status));
	}

	if (tag > fnic->fnic_max_tag_id) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hwq: %d mqtag: 0x%x tag: 0x%x cq index: %d ",
			hwq, mqtag, tag, cq_index);
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hdr status: %s Tag out of range\n",
			fnic_fcpio_status_to_str(hdr_status));
		return;
	}  else if ((tag == fnic->fnic_max_tag_id) && !(id & FNIC_TAG_DEV_RST)) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hwq: %d mqtag: 0x%x tag: 0x%x cq index: %d ",
			hwq, mqtag, tag, cq_index);
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hdr status: %s Tag out of range\n",
			fnic_fcpio_status_to_str(hdr_status));
		return;
	}

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);

	/* If it is sg3utils allocated SC then tag_id
	 * is max_tag_id and SC is retrieved from io_req
	 */
	if ((mqtag == fnic->fnic_max_tag_id) && (id & FNIC_TAG_DEV_RST)) {
		io_req = fnic->sw_copy_wq[hwq].io_req_table[tag];
		if (io_req)
			sc = io_req->sc;
	} else {
		sc = scsi_host_find_tag(fnic->host, id & FNIC_TAG_MASK);
	}

	WARN_ON_ONCE(!sc);
	if (!sc) {
		atomic64_inc(&fnic_stats->io_stats.sc_null);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		shost_printk(KERN_ERR, fnic->host,
			  "itmf_cmpl sc is null - hdr status = %s tag = 0x%x\n",
			  fnic_fcpio_status_to_str(hdr_status), tag);
		return;
	}

	io_req = fnic_priv(sc)->io_req;
	WARN_ON_ONCE(!io_req);
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.ioreq_null);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		fnic_priv(sc)->flags |= FNIC_IO_ABT_TERM_REQ_NULL;
		shost_printk(KERN_ERR, fnic->host,
			  "itmf_cmpl io_req is null - "
			  "hdr status = %s tag = 0x%x sc 0x%p\n",
			  fnic_fcpio_status_to_str(hdr_status), tag, sc);
		return;
	}
	start_time = io_req->start_time;

	if ((id & FNIC_TAG_ABORT) && (id & FNIC_TAG_DEV_RST)) {
		/* Abort and terminate completion of device reset req */
		/* REVISIT : Add asserts about various flags */
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"hwq: %d mqtag: 0x%x tag: 0x%x hst: %s Abt/term completion received\n",
			hwq, mqtag, tag,
			fnic_fcpio_status_to_str(hdr_status));
		fnic_priv(sc)->state = FNIC_IOREQ_ABTS_COMPLETE;
		fnic_priv(sc)->abts_status = hdr_status;
		fnic_priv(sc)->flags |= FNIC_DEV_RST_DONE;
		if (io_req->abts_done)
			complete(io_req->abts_done);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
	} else if (id & FNIC_TAG_ABORT) {
		/* Completion of abort cmd */
		shost_printk(KERN_DEBUG, fnic->host,
			"hwq: %d mqtag: 0x%x tag: 0x%x Abort header status: %s\n",
			hwq, mqtag, tag,
			fnic_fcpio_status_to_str(hdr_status));
		switch (hdr_status) {
		case FCPIO_SUCCESS:
			break;
		case FCPIO_TIMEOUT:
			if (fnic_priv(sc)->flags & FNIC_IO_ABTS_ISSUED)
				atomic64_inc(&abts_stats->abort_fw_timeouts);
			else
				atomic64_inc(
					&term_stats->terminate_fw_timeouts);
			break;
		case FCPIO_ITMF_REJECTED:
			FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				"abort reject recd. id %d\n",
				(int)(id & FNIC_TAG_MASK));
			break;
		case FCPIO_IO_NOT_FOUND:
			if (fnic_priv(sc)->flags & FNIC_IO_ABTS_ISSUED)
				atomic64_inc(&abts_stats->abort_io_not_found);
			else
				atomic64_inc(
					&term_stats->terminate_io_not_found);
			break;
		default:
			if (fnic_priv(sc)->flags & FNIC_IO_ABTS_ISSUED)
				atomic64_inc(&abts_stats->abort_failures);
			else
				atomic64_inc(
					&term_stats->terminate_failures);
			break;
		}
		if (fnic_priv(sc)->state != FNIC_IOREQ_ABTS_PENDING) {
			/* This is a late completion. Ignore it */
			spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
			return;
		}

		fnic_priv(sc)->flags |= FNIC_IO_ABT_TERM_DONE;
		fnic_priv(sc)->abts_status = hdr_status;

		/* If the status is IO not found consider it as success */
		if (hdr_status == FCPIO_IO_NOT_FOUND)
			fnic_priv(sc)->abts_status = FCPIO_SUCCESS;

		if (!(fnic_priv(sc)->flags & (FNIC_IO_ABORTED | FNIC_IO_DONE)))
			atomic64_inc(&misc_stats->no_icmnd_itmf_cmpls);

		FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
			      "abts cmpl recd. id %d status %s\n",
			      (int)(id & FNIC_TAG_MASK),
			      fnic_fcpio_status_to_str(hdr_status));

		/*
		 * If scsi_eh thread is blocked waiting for abts to complete,
		 * signal completion to it. IO will be cleaned in the thread
		 * else clean it in this context
		 */
		if (io_req->abts_done) {
			complete(io_req->abts_done);
			spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
			shost_printk(KERN_INFO, fnic->host,
					"hwq: %d mqtag: 0x%x tag: 0x%x Waking up abort thread\n",
					hwq, mqtag, tag);
		} else {
			FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
				"hwq: %d mqtag: 0x%x tag: 0x%x hst: %s Completing IO\n",
				hwq, mqtag,
				tag, fnic_fcpio_status_to_str(hdr_status));
			fnic_priv(sc)->io_req = NULL;
			sc->result = (DID_ERROR << 16);
			fnic->sw_copy_wq[hwq].io_req_table[tag] = NULL;
			spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

			fnic_release_ioreq_buf(fnic, io_req, sc);
			mempool_free(io_req, fnic->io_req_pool);
			FNIC_TRACE(fnic_fcpio_itmf_cmpl_handler,
				   sc->device->host->host_no, id,
				   sc,
				   jiffies_to_msecs(jiffies - start_time),
				   desc,
				   (((u64)hdr_status << 40) |
				    (u64)sc->cmnd[0] << 32 |
				    (u64)sc->cmnd[2] << 24 |
				    (u64)sc->cmnd[3] << 16 |
				    (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
				   fnic_flags_and_state(sc));
			scsi_done(sc);
			atomic64_dec(&fnic_stats->io_stats.active_ios);
			if (atomic64_read(&fnic->io_cmpl_skip))
				atomic64_dec(&fnic->io_cmpl_skip);
			else
				atomic64_inc(&fnic_stats->io_stats.io_completions);
		}
	} else if (id & FNIC_TAG_DEV_RST) {
		/* Completion of device reset */
		shost_printk(KERN_INFO, fnic->host,
			"hwq: %d mqtag: 0x%x tag: 0x%x DR hst: %s\n",
			hwq, mqtag,
			tag, fnic_fcpio_status_to_str(hdr_status));
		fnic_priv(sc)->lr_status = hdr_status;
		if (fnic_priv(sc)->state == FNIC_IOREQ_ABTS_PENDING) {
			spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
			fnic_priv(sc)->flags |= FNIC_DEV_RST_ABTS_PENDING;
			FNIC_TRACE(fnic_fcpio_itmf_cmpl_handler,
				  sc->device->host->host_no, id, sc,
				  jiffies_to_msecs(jiffies - start_time),
				  desc, 0, fnic_flags_and_state(sc));
			FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
				"hwq: %d mqtag: 0x%x tag: 0x%x hst: %s Terminate pending\n",
				hwq, mqtag,
				tag, fnic_fcpio_status_to_str(hdr_status));
			return;
		}
		if (fnic_priv(sc)->flags & FNIC_DEV_RST_TIMED_OUT) {
			/* Need to wait for terminate completion */
			spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
			FNIC_TRACE(fnic_fcpio_itmf_cmpl_handler,
				  sc->device->host->host_no, id, sc,
				  jiffies_to_msecs(jiffies - start_time),
				  desc, 0, fnic_flags_and_state(sc));
			FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
				"dev reset cmpl recd after time out. "
				"id %d status %s\n",
				(int)(id & FNIC_TAG_MASK),
				fnic_fcpio_status_to_str(hdr_status));
			return;
		}
		fnic_priv(sc)->state = FNIC_IOREQ_CMD_COMPLETE;
		fnic_priv(sc)->flags |= FNIC_DEV_RST_DONE;
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"hwq: %d mqtag: 0x%x tag: 0x%x hst: %s DR completion received\n",
			hwq, mqtag,
			tag, fnic_fcpio_status_to_str(hdr_status));
		if (io_req->dr_done)
			complete(io_req->dr_done);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	} else {
		shost_printk(KERN_ERR, fnic->host,
			"%s: Unexpected itmf io state: hwq: %d tag 0x%x %s\n",
			__func__, hwq, id, fnic_ioreq_state_to_str(fnic_priv(sc)->state));
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
	}

}

/*
 * fnic_fcpio_cmpl_handler
 * Routine to service the cq for wq_copy
 */
static int fnic_fcpio_cmpl_handler(struct vnic_dev *vdev,
				   unsigned int cq_index,
				   struct fcpio_fw_req *desc)
{
	struct fnic *fnic = vnic_dev_priv(vdev);

	switch (desc->hdr.type) {
	case FCPIO_ICMND_CMPL: /* fw completed a command */
	case FCPIO_ITMF_CMPL: /* fw completed itmf (abort cmd, lun reset)*/
	case FCPIO_FLOGI_REG_CMPL: /* fw completed flogi_reg */
	case FCPIO_FLOGI_FIP_REG_CMPL: /* fw completed flogi_fip_reg */
	case FCPIO_RESET_CMPL: /* fw completed reset */
		atomic64_dec(&fnic->fnic_stats.fw_stats.active_fw_reqs);
		break;
	default:
		break;
	}

	cq_index -= fnic->copy_wq_base;

	switch (desc->hdr.type) {
	case FCPIO_ACK: /* fw copied copy wq desc to its queue */
		fnic_fcpio_ack_handler(fnic, cq_index, desc);
		break;

	case FCPIO_ICMND_CMPL: /* fw completed a command */
		fnic_fcpio_icmnd_cmpl_handler(fnic, cq_index, desc);
		break;

	case FCPIO_ITMF_CMPL: /* fw completed itmf (abort cmd, lun reset)*/
		fnic_fcpio_itmf_cmpl_handler(fnic, cq_index, desc);
		break;

	case FCPIO_FLOGI_REG_CMPL: /* fw completed flogi_reg */
	case FCPIO_FLOGI_FIP_REG_CMPL: /* fw completed flogi_fip_reg */
		fnic_fcpio_flogi_reg_cmpl_handler(fnic, desc);
		break;

	case FCPIO_RESET_CMPL: /* fw completed reset */
		fnic_fcpio_fw_reset_cmpl_handler(fnic, desc);
		break;

	default:
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
			      "firmware completion type %d\n",
			      desc->hdr.type);
		break;
	}

	return 0;
}

/*
 * fnic_wq_copy_cmpl_handler
 * Routine to process wq copy
 */
int fnic_wq_copy_cmpl_handler(struct fnic *fnic, int copy_work_to_do, unsigned int cq_index)
{
	unsigned int cur_work_done;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	u64 start_jiffies = 0;
	u64 end_jiffies = 0;
	u64 delta_jiffies = 0;
	u64 delta_ms = 0;

	start_jiffies = jiffies;
	cur_work_done = vnic_cq_copy_service(&fnic->cq[cq_index],
					fnic_fcpio_cmpl_handler,
					copy_work_to_do);
	end_jiffies = jiffies;
	delta_jiffies = end_jiffies - start_jiffies;
	if (delta_jiffies > (u64) atomic64_read(&misc_stats->max_isr_jiffies)) {
		atomic64_set(&misc_stats->max_isr_jiffies, delta_jiffies);
		delta_ms = jiffies_to_msecs(delta_jiffies);
		atomic64_set(&misc_stats->max_isr_time_ms, delta_ms);
		atomic64_set(&misc_stats->corr_work_done, cur_work_done);
	}

	return cur_work_done;
}

static bool fnic_cleanup_io_iter(struct scsi_cmnd *sc, void *data)
{
	struct request *const rq = scsi_cmd_to_rq(sc);
	struct fnic *fnic = data;
	struct fnic_io_req *io_req;
	unsigned long start_time = 0;
	unsigned long flags;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	uint16_t hwq = 0;
	int tag;
	int mqtag;

	mqtag = blk_mq_unique_tag(rq);
	hwq = blk_mq_unique_tag_to_hwq(mqtag);
	tag = blk_mq_unique_tag_to_tag(mqtag);

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);

	fnic->sw_copy_wq[hwq].io_req_table[tag] = NULL;

	io_req = fnic_priv(sc)->io_req;
	if (!io_req) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hwq: %d mqtag: 0x%x tag: 0x%x flags: 0x%x No ioreq. Returning\n",
			hwq, mqtag, tag, fnic_priv(sc)->flags);
		return true;
	}

	if ((fnic_priv(sc)->flags & FNIC_DEVICE_RESET) &&
		!(fnic_priv(sc)->flags & FNIC_DEV_RST_DONE)) {
		/*
		 * We will be here only when FW completes reset
		 * without sending completions for outstanding ios.
		 */
		fnic_priv(sc)->flags |= FNIC_DEV_RST_DONE;
		if (io_req && io_req->dr_done)
			complete(io_req->dr_done);
		else if (io_req && io_req->abts_done)
			complete(io_req->abts_done);

		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		return true;
	} else if (fnic_priv(sc)->flags & FNIC_DEVICE_RESET) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		return true;
	}

	fnic_priv(sc)->io_req = NULL;
	io_req->sc = NULL;
	start_time = io_req->start_time;
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	/*
	 * If there is a scsi_cmnd associated with this io_req, then
	 * free the corresponding state
	 */
	fnic_release_ioreq_buf(fnic, io_req, sc);
	mempool_free(io_req, fnic->io_req_pool);

	sc->result = DID_TRANSPORT_DISRUPTED << 16;
	FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
	"mqtag: 0x%x tag: 0x%x sc: 0x%p duration = %lu DID_TRANSPORT_DISRUPTED\n",
		mqtag, tag, sc, (jiffies - start_time));

	if (atomic64_read(&fnic->io_cmpl_skip))
		atomic64_dec(&fnic->io_cmpl_skip);
	else
		atomic64_inc(&fnic_stats->io_stats.io_completions);

	FNIC_TRACE(fnic_cleanup_io,
			   sc->device->host->host_no, tag, sc,
			   jiffies_to_msecs(jiffies - start_time),
			   0, ((u64) sc->cmnd[0] << 32 |
				   (u64) sc->cmnd[2] << 24 |
				   (u64) sc->cmnd[3] << 16 |
				   (u64) sc->cmnd[4] << 8 | sc->cmnd[5]),
			   (((u64) fnic_priv(sc)->flags << 32) | fnic_priv(sc)->
				state));

	/* Complete the command to SCSI */
	scsi_done(sc);
	return true;
}

static void fnic_cleanup_io(struct fnic *fnic, int exclude_id)
{
	unsigned int io_count = 0;
	unsigned long flags;
	struct fnic_io_req *io_req = NULL;
	struct scsi_cmnd *sc = NULL;

	io_count = fnic_count_all_ioreqs(fnic);
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
				  "Outstanding ioreq count: %d active io count: %lld Waiting\n",
				  io_count,
				  atomic64_read(&fnic->fnic_stats.io_stats.active_ios));

	scsi_host_busy_iter(fnic->host,
						fnic_cleanup_io_iter, fnic);

	/* with sg3utils device reset, SC needs to be retrieved from ioreq */
	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);
	io_req = fnic->sw_copy_wq[0].io_req_table[fnic->fnic_max_tag_id];
	if (io_req) {
		sc = io_req->sc;
		if (sc) {
			if ((fnic_priv(sc)->flags & FNIC_DEVICE_RESET)
				&& !(fnic_priv(sc)->flags & FNIC_DEV_RST_DONE)) {
				fnic_priv(sc)->flags |= FNIC_DEV_RST_DONE;
				if (io_req && io_req->dr_done)
					complete(io_req->dr_done);
			}
		}
	}
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);

	while ((io_count = fnic_count_all_ioreqs(fnic))) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		  "Outstanding ioreq count: %d active io count: %lld Waiting\n",
		  io_count,
		  atomic64_read(&fnic->fnic_stats.io_stats.active_ios));

		schedule_timeout(msecs_to_jiffies(100));
	}
}

void fnic_wq_copy_cleanup_handler(struct vnic_wq_copy *wq,
				  struct fcpio_host_req *desc)
{
	u32 id;
	struct fnic *fnic = vnic_dev_priv(wq->vdev);
	struct fnic_io_req *io_req;
	struct scsi_cmnd *sc;
	unsigned long flags;
	unsigned long start_time = 0;
	uint16_t hwq;

	/* get the tag reference */
	fcpio_tag_id_dec(&desc->hdr.tag, &id);
	id &= FNIC_TAG_MASK;

	if (id >= fnic->fnic_max_tag_id)
		return;

	sc = scsi_host_find_tag(fnic->host, id);
	if (!sc)
		return;

	hwq = blk_mq_unique_tag_to_hwq(id);
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);

	/* Get the IO context which this desc refers to */
	io_req = fnic_priv(sc)->io_req;

	/* fnic interrupts are turned off by now */

	if (!io_req) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		goto wq_copy_cleanup_scsi_cmd;
	}

	fnic_priv(sc)->io_req = NULL;
	io_req->sc = NULL;
	fnic->sw_copy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(id)] = NULL;

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	start_time = io_req->start_time;
	fnic_release_ioreq_buf(fnic, io_req, sc);
	mempool_free(io_req, fnic->io_req_pool);

wq_copy_cleanup_scsi_cmd:
	sc->result = DID_NO_CONNECT << 16;
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num, "wq_copy_cleanup_handler:"
		      " DID_NO_CONNECT\n");

	FNIC_TRACE(fnic_wq_copy_cleanup_handler,
		   sc->device->host->host_no, id, sc,
		   jiffies_to_msecs(jiffies - start_time),
		   0, ((u64)sc->cmnd[0] << 32 |
		       (u64)sc->cmnd[2] << 24 | (u64)sc->cmnd[3] << 16 |
		       (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
		   fnic_flags_and_state(sc));

	scsi_done(sc);
}

static inline int fnic_queue_abort_io_req(struct fnic *fnic, int tag,
					  u32 task_req, u8 *fc_lun,
					  struct fnic_io_req *io_req,
					  unsigned int hwq)
{
	struct vnic_wq_copy *wq = &fnic->hw_copy_wq[hwq];
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	unsigned long flags;
	struct fnic_tport_s *tport = io_req->tport;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (unlikely(fnic_chk_state_flags_locked(fnic,
						FNIC_FLAGS_IO_BLOCKED))) {
		atomic_dec(&fnic->in_flight);
		atomic_dec(&tport->in_flight);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return 1;
	} else
		atomic_inc(&fnic->in_flight);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[hwq])
		free_wq_copy_descs(fnic, wq, hwq);

	if (!vnic_wq_copy_desc_avail(wq)) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		atomic_dec(&fnic->in_flight);
		atomic_dec(&tport->in_flight);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
			"fnic_queue_abort_io_req: failure: no descriptors\n");
		atomic64_inc(&misc_stats->abts_cpwq_alloc_failures);
		return 1;
	}
	fnic_queue_wq_copy_desc_itmf(wq, tag | FNIC_TAG_ABORT,
				     0, task_req, tag, fc_lun, io_req->port_id,
				     fnic->config.ra_tov, fnic->config.ed_tov);

	atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
	if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
		  atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
		atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
		  atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
	atomic_dec(&fnic->in_flight);

	return 0;
}

struct fnic_rport_abort_io_iter_data {
	struct fnic *fnic;
	u32 port_id;
	int term_cnt;
};

static bool fnic_rport_abort_io_iter(struct scsi_cmnd *sc, void *data)
{
	struct request *const rq = scsi_cmd_to_rq(sc);
	struct fnic_rport_abort_io_iter_data *iter_data = data;
	struct fnic *fnic = iter_data->fnic;
	int abt_tag = 0;
	struct fnic_io_req *io_req;
	struct reset_stats *reset_stats = &fnic->fnic_stats.reset_stats;
	struct terminate_stats *term_stats = &fnic->fnic_stats.term_stats;
	struct scsi_lun fc_lun;
	enum fnic_ioreq_state old_ioreq_state;
	uint16_t hwq = 0;
	unsigned long flags;

	abt_tag = blk_mq_unique_tag(rq);
	hwq = blk_mq_unique_tag_to_hwq(abt_tag);

	if (!sc) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
					  "sc is NULL abt_tag: 0x%x hwq: %d\n", abt_tag, hwq);
		return true;
	}

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	io_req = fnic_priv(sc)->io_req;
	if (!io_req || io_req->port_id != iter_data->port_id) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		return true;
	}

	if ((fnic_priv(sc)->flags & FNIC_DEVICE_RESET) &&
	    !(fnic_priv(sc)->flags & FNIC_DEV_RST_ISSUED)) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hwq: %d abt_tag: 0x%x flags: 0x%x Device reset is not pending\n",
			hwq, abt_tag, fnic_priv(sc)->flags);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		return true;
	}

	/*
	 * Found IO that is still pending with firmware and
	 * belongs to rport that went away
	 */
	if (fnic_priv(sc)->state == FNIC_IOREQ_ABTS_PENDING) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		return true;
	}

	if (io_req->abts_done) {
		shost_printk(KERN_ERR, fnic->host,
			"fnic_rport_exch_reset: io_req->abts_done is set state is %s\n",
			fnic_ioreq_state_to_str(fnic_priv(sc)->state));
	}

	if (!(fnic_priv(sc)->flags & FNIC_IO_ISSUED)) {
		shost_printk(KERN_ERR, fnic->host,
			"rport_exch_reset IO not yet issued %p abt_tag 0x%x",
			sc, abt_tag);
		shost_printk(KERN_ERR, fnic->host,
			"flags %x state %d\n", fnic_priv(sc)->flags,
			fnic_priv(sc)->state);
	}
	old_ioreq_state = fnic_priv(sc)->state;
	fnic_priv(sc)->state = FNIC_IOREQ_ABTS_PENDING;
	fnic_priv(sc)->abts_status = FCPIO_INVALID_CODE;

	if (fnic_priv(sc)->flags & FNIC_DEVICE_RESET) {
		atomic64_inc(&reset_stats->device_reset_terminates);
		abt_tag |= FNIC_TAG_DEV_RST;
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
					  "dev reset sc 0x%p\n", sc);
	}
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		      "fnic_rport_exch_reset: dev rst sc 0x%p\n", sc);
	WARN_ON_ONCE(io_req->abts_done);
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		      "fnic_rport_reset_exch: Issuing abts\n");

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	/* Queue the abort command to firmware */
	int_to_scsilun(sc->device->lun, &fc_lun);

	if (fnic_queue_abort_io_req(fnic, abt_tag,
				    FCPIO_ITMF_ABT_TASK_TERM,
				    fc_lun.scsi_lun, io_req, hwq)) {
		/*
		 * Revert the cmd state back to old state, if
		 * it hasn't changed in between. This cmd will get
		 * aborted later by scsi_eh, or cleaned up during
		 * lun reset
		 */
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hwq: %d abt_tag: 0x%x flags: 0x%x Queuing abort failed\n",
			hwq, abt_tag, fnic_priv(sc)->flags);
		if (fnic_priv(sc)->state == FNIC_IOREQ_ABTS_PENDING)
			fnic_priv(sc)->state = old_ioreq_state;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
	} else {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
		if (fnic_priv(sc)->flags & FNIC_DEVICE_RESET)
			fnic_priv(sc)->flags |= FNIC_DEV_RST_TERM_ISSUED;
		else
			fnic_priv(sc)->flags |= FNIC_IO_INTERNAL_TERM_ISSUED;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		atomic64_inc(&term_stats->terminates);
		iter_data->term_cnt++;
	}

	return true;
}

void fnic_rport_exch_reset(struct fnic *fnic, u32 port_id)
{
	unsigned int io_count = 0;
	unsigned long flags;
	struct terminate_stats *term_stats = &fnic->fnic_stats.term_stats;
	struct fnic_rport_abort_io_iter_data iter_data = {
		.fnic = fnic,
		.port_id = port_id,
		.term_cnt = 0,
	};

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
				  "fnic rport exchange reset for tport: 0x%06x\n",
				  port_id);

	if (fnic->in_remove)
		return;

	io_count = fnic_count_ioreqs(fnic, port_id);
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
				  "Starting terminates: rport:0x%x  portid-io-count: %d active-io-count: %lld\n",
				  port_id, io_count,
				  atomic64_read(&fnic->fnic_stats.io_stats.active_ios));

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	/* Bump in_flight counter to hold off fnic_fw_reset_handler. */
	atomic_inc(&fnic->in_flight);
	if (unlikely(fnic_chk_state_flags_locked(fnic, FNIC_FLAGS_IO_BLOCKED))) {
		atomic_dec(&fnic->in_flight);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	scsi_host_busy_iter(fnic->host, fnic_rport_abort_io_iter,
			    &iter_data);

	if (iter_data.term_cnt > atomic64_read(&term_stats->max_terminates))
		atomic64_set(&term_stats->max_terminates, iter_data.term_cnt);

	atomic_dec(&fnic->in_flight);

	while ((io_count = fnic_count_ioreqs(fnic, port_id)))
		schedule_timeout(msecs_to_jiffies(1000));

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
				  "rport: 0x%x remaining portid-io-count: %d ",
				  port_id, io_count);
}

void fnic_terminate_rport_io(struct fc_rport *rport)
{
	struct fnic_tport_s *tport;
	struct rport_dd_data_s *rdd_data;
	struct fnic_iport_s *iport = NULL;
	struct fnic *fnic = NULL;

	if (!rport) {
		pr_err("rport is NULL\n");
		return;
	}

	rdd_data = rport->dd_data;
	if (rdd_data) {
		tport = rdd_data->tport;
		if (!tport) {
			pr_err(
			"term rport io called after tport is deleted. Returning 0x%8x\n",
		   rport->port_id);
		} else {
			pr_err(
			   "term rport io called after tport is set 0x%8x\n",
			   rport->port_id);
			pr_err(
			   "tport maybe rediscovered\n");

			iport = (struct fnic_iport_s *) tport->iport;
			fnic = iport->fnic;
			fnic_rport_exch_reset(fnic, rport->port_id);
		}
	}
}

/*
 * FCP-SCSI specific handling for module unload
 *
 */
void fnic_scsi_unload(struct fnic *fnic)
{
	unsigned long flags;

	/*
	 * Mark state so that the workqueue thread stops forwarding
	 * received frames and link events to the local port. ISR and
	 * other threads that can queue work items will also stop
	 * creating work items on the fnic workqueue
	 */
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->iport.state = FNIC_IPORT_STATE_LINK_WAIT;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (fdls_get_state(&fnic->iport.fabric) != FDLS_STATE_INIT)
		fnic_scsi_fcpio_reset(fnic);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->in_remove = 1;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	fnic_flush_tport_event_list(fnic);
	fnic_delete_fcp_tports(fnic);
}

void fnic_scsi_unload_cleanup(struct fnic *fnic)
{
	int hwq = 0;

	fc_remove_host(fnic->host);
	scsi_remove_host(fnic->host);
	for (hwq = 0; hwq < fnic->wq_copy_count; hwq++)
		kfree(fnic->sw_copy_wq[hwq].io_req_table);
}

/*
 * This function is exported to SCSI for sending abort cmnds.
 * A SCSI IO is represented by a io_req in the driver.
 * The ioreq is linked to the SCSI Cmd, thus a link with the ULP's IO.
 */
int fnic_abort_cmd(struct scsi_cmnd *sc)
{
	struct request *const rq = scsi_cmd_to_rq(sc);
	struct fnic_iport_s *iport;
	struct fnic_tport_s *tport;
	struct fnic *fnic;
	struct fnic_io_req *io_req = NULL;
	struct fc_rport *rport;
	struct rport_dd_data_s *rdd_data;
	unsigned long flags;
	unsigned long start_time = 0;
	int ret = SUCCESS;
	u32 task_req = 0;
	struct scsi_lun fc_lun;
	struct fnic_stats *fnic_stats;
	struct abort_stats *abts_stats;
	struct terminate_stats *term_stats;
	enum fnic_ioreq_state old_ioreq_state;
	int mqtag;
	unsigned long abt_issued_time;
	uint16_t hwq = 0;

	DECLARE_COMPLETION_ONSTACK(tm_done);

	/* Wait for rport to unblock */
	fc_block_scsi_eh(sc);

	/* Get local-port, check ready and link up */
	fnic = *((struct fnic **) shost_priv(sc->device->host));

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	iport = &fnic->iport;

	fnic_stats = &fnic->fnic_stats;
	abts_stats = &fnic->fnic_stats.abts_stats;
	term_stats = &fnic->fnic_stats.term_stats;

	rport = starget_to_rport(scsi_target(sc->device));
	mqtag = blk_mq_unique_tag(rq);
	hwq = blk_mq_unique_tag_to_hwq(mqtag);

	fnic_priv(sc)->flags = FNIC_NO_FLAGS;

	rdd_data = rport->dd_data;
	tport = rdd_data->tport;

	if (!tport) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			  "Abort cmd called after tport delete! rport fcid: 0x%x",
			  rport->port_id);
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			  "lun: %llu hwq: 0x%x mqtag: 0x%x Op: 0x%x flags: 0x%x\n",
			  sc->device->lun, hwq, mqtag,
			  sc->cmnd[0], fnic_priv(sc)->flags);
		ret = FAILED;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto fnic_abort_cmd_end;
	}

	FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
	  "Abort cmd called rport fcid: 0x%x lun: %llu hwq: 0x%x mqtag: 0x%x",
	  rport->port_id, sc->device->lun, hwq, mqtag);

	FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				  "Op: 0x%x flags: 0x%x\n",
				  sc->cmnd[0],
				  fnic_priv(sc)->flags);

	if (iport->state != FNIC_IPORT_STATE_READY) {
		atomic64_inc(&fnic_stats->misc_stats.iport_not_ready);
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					  "iport NOT in READY state");
		ret = FAILED;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto fnic_abort_cmd_end;
	}

	if ((tport->state != FDLS_TGT_STATE_READY) &&
		(tport->state != FDLS_TGT_STATE_ADISC)) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
					  "tport state: %d\n", tport->state);
		ret = FAILED;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto fnic_abort_cmd_end;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	/*
	 * Avoid a race between SCSI issuing the abort and the device
	 * completing the command.
	 *
	 * If the command is already completed by the fw cmpl code,
	 * we just return SUCCESS from here. This means that the abort
	 * succeeded. In the SCSI ML, since the timeout for command has
	 * happened, the completion wont actually complete the command
	 * and it will be considered as an aborted command
	 *
	 * .io_req will not be cleared except while holding io_req_lock.
	 */
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	io_req = fnic_priv(sc)->io_req;
	if (!io_req) {
		ret = FAILED;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		goto fnic_abort_cmd_end;
	}

	io_req->abts_done = &tm_done;

	if (fnic_priv(sc)->state == FNIC_IOREQ_ABTS_PENDING) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		goto wait_pending;
	}

	abt_issued_time = jiffies_to_msecs(jiffies) - jiffies_to_msecs(io_req->start_time);
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

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		"CDB Opcode: 0x%02x Abort issued time: %lu msec\n",
		sc->cmnd[0], abt_issued_time);
	/*
	 * Command is still pending, need to abort it
	 * If the firmware completes the command after this point,
	 * the completion wont be done till mid-layer, since abort
	 * has already started.
	 */
	old_ioreq_state = fnic_priv(sc)->state;
	fnic_priv(sc)->state = FNIC_IOREQ_ABTS_PENDING;
	fnic_priv(sc)->abts_status = FCPIO_INVALID_CODE;

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	/*
	 * Check readiness of the remote port. If the path to remote
	 * port is up, then send abts to the remote port to terminate
	 * the IO. Else, just locally terminate the IO in the firmware
	 */
	if (fc_remote_port_chkready(rport) == 0)
		task_req = FCPIO_ITMF_ABT_TASK;
	else {
		atomic64_inc(&fnic_stats->misc_stats.tport_not_ready);
		task_req = FCPIO_ITMF_ABT_TASK_TERM;
	}

	/* Now queue the abort command to firmware */
	int_to_scsilun(sc->device->lun, &fc_lun);

	if (fnic_queue_abort_io_req(fnic, mqtag, task_req, fc_lun.scsi_lun,
				    io_req, hwq)) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
		if (fnic_priv(sc)->state == FNIC_IOREQ_ABTS_PENDING)
			fnic_priv(sc)->state = old_ioreq_state;
		io_req = fnic_priv(sc)->io_req;
		if (io_req)
			io_req->abts_done = NULL;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}
	if (task_req == FCPIO_ITMF_ABT_TASK) {
		fnic_priv(sc)->flags |= FNIC_IO_ABTS_ISSUED;
		atomic64_inc(&fnic_stats->abts_stats.aborts);
	} else {
		fnic_priv(sc)->flags |= FNIC_IO_TERM_ISSUED;
		atomic64_inc(&fnic_stats->term_stats.terminates);
	}

	/*
	 * We queued an abort IO, wait for its completion.
	 * Once the firmware completes the abort command, it will
	 * wake up this thread.
	 */
 wait_pending:
	wait_for_completion_timeout(&tm_done,
				    msecs_to_jiffies
				    (2 * fnic->config.ra_tov +
				     fnic->config.ed_tov));

	/* Check the abort status */
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);

	io_req = fnic_priv(sc)->io_req;
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.ioreq_null);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		fnic_priv(sc)->flags |= FNIC_IO_ABT_TERM_REQ_NULL;
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}
	io_req->abts_done = NULL;

	/* fw did not complete abort, timed out */
	if (fnic_priv(sc)->abts_status == FCPIO_INVALID_CODE) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		if (task_req == FCPIO_ITMF_ABT_TASK) {
			atomic64_inc(&abts_stats->abort_drv_timeouts);
		} else {
			atomic64_inc(&term_stats->terminate_drv_timeouts);
		}
		fnic_priv(sc)->flags |= FNIC_IO_ABT_TERM_TIMED_OUT;
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}

	/* IO out of order */

	if (!(fnic_priv(sc)->flags & (FNIC_IO_ABORTED | FNIC_IO_DONE))) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			      "Issuing host reset due to out of order IO\n");

		ret = FAILED;
		goto fnic_abort_cmd_end;
	}

	fnic_priv(sc)->state = FNIC_IOREQ_ABTS_COMPLETE;

	start_time = io_req->start_time;
	/*
	 * firmware completed the abort, check the status,
	 * free the io_req if successful. If abort fails,
	 * Device reset will clean the I/O.
	 */
	if (fnic_priv(sc)->abts_status == FCPIO_SUCCESS ||
		(fnic_priv(sc)->abts_status == FCPIO_ABORTED)) {
		fnic_priv(sc)->io_req = NULL;
		io_req->sc = NULL;
	} else {
		ret = FAILED;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		goto fnic_abort_cmd_end;
	}

	fnic->sw_copy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(mqtag)] = NULL;
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	fnic_release_ioreq_buf(fnic, io_req, sc);
	mempool_free(io_req, fnic->io_req_pool);

	/* Call SCSI completion function to complete the IO */
	sc->result = DID_ABORT << 16;
	scsi_done(sc);
	atomic64_dec(&fnic_stats->io_stats.active_ios);
	if (atomic64_read(&fnic->io_cmpl_skip))
		atomic64_dec(&fnic->io_cmpl_skip);
	else
		atomic64_inc(&fnic_stats->io_stats.io_completions);

fnic_abort_cmd_end:
	FNIC_TRACE(fnic_abort_cmd, sc->device->host->host_no, mqtag, sc,
		  jiffies_to_msecs(jiffies - start_time),
		  0, ((u64)sc->cmnd[0] << 32 |
		  (u64)sc->cmnd[2] << 24 | (u64)sc->cmnd[3] << 16 |
		  (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
		  fnic_flags_and_state(sc));

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		      "Returning from abort cmd type %x %s\n", task_req,
		      (ret == SUCCESS) ?
		      "SUCCESS" : "FAILED");
	return ret;
}

static inline int fnic_queue_dr_io_req(struct fnic *fnic,
				       struct scsi_cmnd *sc,
				       struct fnic_io_req *io_req)
{
	struct vnic_wq_copy *wq;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	struct scsi_lun fc_lun;
	int ret = 0;
	unsigned long flags;
	uint16_t hwq = 0;
	uint32_t tag = 0;
	struct fnic_tport_s *tport = io_req->tport;

	tag = io_req->tag;
	hwq = blk_mq_unique_tag_to_hwq(tag);
	wq = &fnic->hw_copy_wq[hwq];

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (unlikely(fnic_chk_state_flags_locked(fnic,
						FNIC_FLAGS_IO_BLOCKED))) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return FAILED;
	} else {
		atomic_inc(&fnic->in_flight);
		atomic_inc(&tport->in_flight);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[hwq])
		free_wq_copy_descs(fnic, wq, hwq);

	if (!vnic_wq_copy_desc_avail(wq)) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
			  "queue_dr_io_req failure - no descriptors\n");
		atomic64_inc(&misc_stats->devrst_cpwq_alloc_failures);
		ret = -EAGAIN;
		goto lr_io_req_end;
	}

	/* fill in the lun info */
	int_to_scsilun(sc->device->lun, &fc_lun);

	tag |= FNIC_TAG_DEV_RST;
	fnic_queue_wq_copy_desc_itmf(wq, tag,
				     0, FCPIO_ITMF_LUN_RESET, SCSI_NO_TAG,
				     fc_lun.scsi_lun, io_req->port_id,
				     fnic->config.ra_tov, fnic->config.ed_tov);

	atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
	if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
		  atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
		atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
		  atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

lr_io_req_end:
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
	atomic_dec(&fnic->in_flight);
	atomic_dec(&tport->in_flight);

	return ret;
}

struct fnic_pending_aborts_iter_data {
	struct fnic *fnic;
	struct scsi_cmnd *lr_sc;
	struct scsi_device *lun_dev;
	int ret;
};

static bool fnic_pending_aborts_iter(struct scsi_cmnd *sc, void *data)
{
	struct request *const rq = scsi_cmd_to_rq(sc);
	struct fnic_pending_aborts_iter_data *iter_data = data;
	struct fnic *fnic = iter_data->fnic;
	struct scsi_device *lun_dev = iter_data->lun_dev;
	unsigned long abt_tag = 0;
	uint16_t hwq = 0;
	struct fnic_io_req *io_req;
	unsigned long flags;
	struct scsi_lun fc_lun;
	DECLARE_COMPLETION_ONSTACK(tm_done);
	enum fnic_ioreq_state old_ioreq_state;

	if (sc == iter_data->lr_sc || sc->device != lun_dev)
		return true;

	abt_tag = blk_mq_unique_tag(rq);
	hwq = blk_mq_unique_tag_to_hwq(abt_tag);

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	io_req = fnic_priv(sc)->io_req;
	if (!io_req) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		return true;
	}

	/*
	 * Found IO that is still pending with firmware and
	 * belongs to the LUN that we are resetting
	 */
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		      "Found IO in %s on lun\n",
		      fnic_ioreq_state_to_str(fnic_priv(sc)->state));

	if (fnic_priv(sc)->state == FNIC_IOREQ_ABTS_PENDING) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		return true;
	}
	if ((fnic_priv(sc)->flags & FNIC_DEVICE_RESET) &&
	    (!(fnic_priv(sc)->flags & FNIC_DEV_RST_ISSUED))) {
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			      "dev rst not pending sc 0x%p\n", sc);
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		return true;
	}

	if (io_req->abts_done)
		shost_printk(KERN_ERR, fnic->host,
			     "%s: io_req->abts_done is set state is %s\n",
			     __func__, fnic_ioreq_state_to_str(fnic_priv(sc)->state));
	old_ioreq_state = fnic_priv(sc)->state;
	/*
	 * Any pending IO issued prior to reset is expected to be
	 * in abts pending state, if not we need to set
	 * FNIC_IOREQ_ABTS_PENDING to indicate the IO is abort pending.
	 * When IO is completed, the IO will be handed over and
	 * handled in this function.
	 */
	fnic_priv(sc)->state = FNIC_IOREQ_ABTS_PENDING;

	BUG_ON(io_req->abts_done);

	if (fnic_priv(sc)->flags & FNIC_DEVICE_RESET) {
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			      "dev rst sc 0x%p\n", sc);
	}

	fnic_priv(sc)->abts_status = FCPIO_INVALID_CODE;
	io_req->abts_done = &tm_done;
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	/* Now queue the abort command to firmware */
	int_to_scsilun(sc->device->lun, &fc_lun);

	if (fnic_queue_abort_io_req(fnic, abt_tag,
				    FCPIO_ITMF_ABT_TASK_TERM,
				    fc_lun.scsi_lun, io_req, hwq)) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
		io_req = fnic_priv(sc)->io_req;
		if (io_req)
			io_req->abts_done = NULL;
		if (fnic_priv(sc)->state == FNIC_IOREQ_ABTS_PENDING)
			fnic_priv(sc)->state = old_ioreq_state;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		iter_data->ret = FAILED;
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"hwq: %d abt_tag: 0x%lx Abort could not be queued\n",
			hwq, abt_tag);
		return false;
	} else {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
		if (fnic_priv(sc)->flags & FNIC_DEVICE_RESET)
			fnic_priv(sc)->flags |= FNIC_DEV_RST_TERM_ISSUED;
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
	}
	fnic_priv(sc)->flags |= FNIC_IO_INTERNAL_TERM_ISSUED;

	wait_for_completion_timeout(&tm_done, msecs_to_jiffies
				    (fnic->config.ed_tov));

	/* Recheck cmd state to check if it is now aborted */
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	io_req = fnic_priv(sc)->io_req;
	if (!io_req) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		fnic_priv(sc)->flags |= FNIC_IO_ABT_TERM_REQ_NULL;
		return true;
	}

	io_req->abts_done = NULL;

	/* if abort is still pending with fw, fail */
	if (fnic_priv(sc)->abts_status == FCPIO_INVALID_CODE) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		fnic_priv(sc)->flags |= FNIC_IO_ABT_TERM_DONE;
		iter_data->ret = FAILED;
		return false;
	}
	fnic_priv(sc)->state = FNIC_IOREQ_ABTS_COMPLETE;

	/* original sc used for lr is handled by dev reset code */
	if (sc != iter_data->lr_sc) {
		fnic_priv(sc)->io_req = NULL;
		fnic->sw_copy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(abt_tag)] = NULL;
	}
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	/* original sc used for lr is handled by dev reset code */
	if (sc != iter_data->lr_sc) {
		fnic_release_ioreq_buf(fnic, io_req, sc);
		mempool_free(io_req, fnic->io_req_pool);
	}

	/*
	 * Any IO is returned during reset, it needs to call scsi_done
	 * to return the scsi_cmnd to upper layer.
	 */
	/* Set result to let upper SCSI layer retry */
	sc->result = DID_RESET << 16;
	scsi_done(sc);

	return true;
}

/*
 * Clean up any pending aborts on the lun
 * For each outstanding IO on this lun, whose abort is not completed by fw,
 * issue a local abort. Wait for abort to complete. Return 0 if all commands
 * successfully aborted, 1 otherwise
 */
static int fnic_clean_pending_aborts(struct fnic *fnic,
				     struct scsi_cmnd *lr_sc,
				     bool new_sc)

{
	int ret = 0;
	struct fnic_pending_aborts_iter_data iter_data = {
		.fnic = fnic,
		.lun_dev = lr_sc->device,
		.ret = SUCCESS,
	};

	iter_data.lr_sc = lr_sc;

	scsi_host_busy_iter(fnic->host,
			    fnic_pending_aborts_iter, &iter_data);
	if (iter_data.ret == FAILED) {
		ret = iter_data.ret;
		goto clean_pending_aborts_end;
	}
	schedule_timeout(msecs_to_jiffies(2 * fnic->config.ed_tov));

	/* walk again to check, if IOs are still pending in fw */
	if (fnic_is_abts_pending(fnic, lr_sc))
		ret = 1;

clean_pending_aborts_end:
	FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"exit status: %d\n", ret);
	return ret;
}

/*
 * SCSI Eh thread issues a Lun Reset when one or more commands on a LUN
 * fail to get aborted. It calls driver's eh_device_reset with a SCSI command
 * on the LUN.
 */
int fnic_device_reset(struct scsi_cmnd *sc)
{
	struct request *rq = scsi_cmd_to_rq(sc);
	struct fnic *fnic;
	struct fnic_io_req *io_req = NULL;
	struct fc_rport *rport;
	int status;
	int count = 0;
	int ret = FAILED;
	unsigned long flags;
	unsigned long start_time = 0;
	struct scsi_lun fc_lun;
	struct fnic_stats *fnic_stats;
	struct reset_stats *reset_stats;
	int mqtag = rq->tag;
	DECLARE_COMPLETION_ONSTACK(tm_done);
	bool new_sc = 0;
	uint16_t hwq = 0;
	struct fnic_iport_s *iport = NULL;
	struct rport_dd_data_s *rdd_data;
	struct fnic_tport_s *tport;
	u32 old_soft_reset_count;
	u32 old_link_down_cnt;
	int exit_dr = 0;

	/* Wait for rport to unblock */
	fc_block_scsi_eh(sc);

	/* Get local-port, check ready and link up */
	fnic = *((struct fnic **) shost_priv(sc->device->host));
	iport = &fnic->iport;

	fnic_stats = &fnic->fnic_stats;
	reset_stats = &fnic_stats->reset_stats;

	atomic64_inc(&reset_stats->device_resets);

	rport = starget_to_rport(scsi_target(sc->device));

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		"fcid: 0x%x lun: %llu hwq: %d mqtag: 0x%x flags: 0x%x Device reset\n",
		rport->port_id, sc->device->lun, hwq, mqtag,
		fnic_priv(sc)->flags);

	rdd_data = rport->dd_data;
	tport = rdd_data->tport;
	if (!tport) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		  "Dev rst called after tport delete! rport fcid: 0x%x lun: %llu\n",
		  rport->port_id, sc->device->lun);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto fnic_device_reset_end;
	}

	if (iport->state != FNIC_IPORT_STATE_READY) {
		atomic64_inc(&fnic_stats->misc_stats.iport_not_ready);
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					  "iport NOT in READY state");
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto fnic_device_reset_end;
	}

	if ((tport->state != FDLS_TGT_STATE_READY) &&
		(tport->state != FDLS_TGT_STATE_ADISC)) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
					  "tport state: %d\n", tport->state);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto fnic_device_reset_end;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	/* Check if remote port up */
	if (fc_remote_port_chkready(rport)) {
		atomic64_inc(&fnic_stats->misc_stats.tport_not_ready);
		goto fnic_device_reset_end;
	}

	fnic_priv(sc)->flags = FNIC_DEVICE_RESET;

	if (unlikely(mqtag < 0)) {
		/*
		 * For device reset issued through sg3utils, we let
		 * only one LUN_RESET to go through and use a special
		 * tag equal to max_tag_id so that we don't have to allocate
		 * or free it. It won't interact with tags
		 * allocated by mid layer.
		 */
		mutex_lock(&fnic->sgreset_mutex);
		mqtag = fnic->fnic_max_tag_id;
		new_sc = 1;
	}  else {
		mqtag = blk_mq_unique_tag(rq);
		hwq = blk_mq_unique_tag_to_hwq(mqtag);
	}

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	io_req = fnic_priv(sc)->io_req;

	/*
	 * If there is a io_req attached to this command, then use it,
	 * else allocate a new one.
	 */
	if (!io_req) {
		io_req = mempool_alloc(fnic->io_req_pool, GFP_ATOMIC);
		if (!io_req) {
			spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
			goto fnic_device_reset_end;
		}
		memset(io_req, 0, sizeof(*io_req));
		io_req->port_id = rport->port_id;
		io_req->tag = mqtag;
		fnic_priv(sc)->io_req = io_req;
		io_req->tport = tport;
		io_req->sc = sc;

		if (fnic->sw_copy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(mqtag)] != NULL)
			WARN(1, "fnic<%d>: %s: tag 0x%x already exists\n",
					fnic->fnic_num, __func__, blk_mq_unique_tag_to_tag(mqtag));

		fnic->sw_copy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(mqtag)] =
				io_req;
	}
	io_req->dr_done = &tm_done;
	fnic_priv(sc)->state = FNIC_IOREQ_CMD_PENDING;
	fnic_priv(sc)->lr_status = FCPIO_INVALID_CODE;
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num, "TAG %x\n", mqtag);

	/*
	 * issue the device reset, if enqueue failed, clean up the ioreq
	 * and break assoc with scsi cmd
	 */
	if (fnic_queue_dr_io_req(fnic, sc, io_req)) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
		io_req = fnic_priv(sc)->io_req;
		if (io_req)
			io_req->dr_done = NULL;
		goto fnic_device_reset_clean;
	}
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	fnic_priv(sc)->flags |= FNIC_DEV_RST_ISSUED;
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	old_link_down_cnt = iport->fnic->link_down_cnt;
	old_soft_reset_count = fnic->soft_reset_count;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	/*
	 * Wait on the local completion for LUN reset.  The io_req may be
	 * freed while we wait since we hold no lock.
	 */
	wait_for_completion_timeout(&tm_done,
				    msecs_to_jiffies(FNIC_LUN_RESET_TIMEOUT));

	/*
	 * Wake up can be due to the following reasons:
	 * 1) The device reset completed from target.
	 * 2) Device reset timed out.
	 * 3) A link-down/host_reset may have happened in between.
	 * 4) The device reset was aborted and io_req->dr_done was called.
	 */

	exit_dr = 0;
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if ((old_link_down_cnt != fnic->link_down_cnt) ||
		(fnic->reset_in_progress) ||
		(fnic->soft_reset_count != old_soft_reset_count) ||
		(iport->state != FNIC_IPORT_STATE_READY))
		exit_dr = 1;

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	io_req = fnic_priv(sc)->io_req;
	if (!io_req) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
				"io_req is null mqtag 0x%x sc 0x%p\n", mqtag, sc);
		goto fnic_device_reset_end;
	}

	if (exit_dr) {
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
					  "Host reset called for fnic. Exit device reset\n");
		io_req->dr_done = NULL;
		goto fnic_device_reset_clean;
	}
	io_req->dr_done = NULL;

	status = fnic_priv(sc)->lr_status;

	/*
	 * If lun reset not completed, bail out with failed. io_req
	 * gets cleaned up during higher levels of EH
	 */
	if (status == FCPIO_INVALID_CODE) {
		atomic64_inc(&reset_stats->device_reset_timeouts);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
			      "Device reset timed out\n");
		fnic_priv(sc)->flags |= FNIC_DEV_RST_TIMED_OUT;
		int_to_scsilun(sc->device->lun, &fc_lun);
		goto fnic_device_reset_clean;
	} else {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
	}

	/* Completed, but not successful, clean up the io_req, return fail */
	if (status != FCPIO_SUCCESS) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
		FNIC_SCSI_DBG(KERN_DEBUG,
			      fnic->host, fnic->fnic_num,
			      "Device reset completed - failed\n");
		io_req = fnic_priv(sc)->io_req;
		goto fnic_device_reset_clean;
	}

	/*
	 * Clean up any aborts on this lun that have still not
	 * completed. If any of these fail, then LUN reset fails.
	 * clean_pending_aborts cleans all cmds on this lun except
	 * the lun reset cmd. If all cmds get cleaned, the lun reset
	 * succeeds
	 */
	if (fnic_clean_pending_aborts(fnic, sc, new_sc)) {
		spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
		io_req = fnic_priv(sc)->io_req;
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
					  "Device reset failed: Cannot abort all IOs\n");
		goto fnic_device_reset_clean;
	}

	/* Clean lun reset command */
	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);
	io_req = fnic_priv(sc)->io_req;
	if (io_req)
		/* Completed, and successful */
		ret = SUCCESS;

fnic_device_reset_clean:
	if (io_req) {
		fnic_priv(sc)->io_req = NULL;
		io_req->sc = NULL;
		fnic->sw_copy_wq[hwq].io_req_table[blk_mq_unique_tag_to_tag(io_req->tag)] = NULL;
	}

	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);

	if (io_req) {
		start_time = io_req->start_time;
		fnic_release_ioreq_buf(fnic, io_req, sc);
		mempool_free(io_req, fnic->io_req_pool);
	}

	/*
	 * If link-event is seen while LUN reset is issued we need
	 * to complete the LUN reset here
	 */
	if (!new_sc) {
		sc->result = DID_RESET << 16;
		scsi_done(sc);
	}

fnic_device_reset_end:
	FNIC_TRACE(fnic_device_reset, sc->device->host->host_no, rq->tag, sc,
		  jiffies_to_msecs(jiffies - start_time),
		  0, ((u64)sc->cmnd[0] << 32 |
		  (u64)sc->cmnd[2] << 24 | (u64)sc->cmnd[3] << 16 |
		  (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
		  fnic_flags_and_state(sc));

	if (new_sc) {
		fnic->sgreset_sc = NULL;
		mutex_unlock(&fnic->sgreset_mutex);
	}

	while ((ret == SUCCESS) && fnic_count_lun_ioreqs(fnic, sc->device)) {
		if (count >= 2) {
			ret = FAILED;
			break;
		}
		FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
					  "Cannot clean up all IOs for the LUN\n");
		schedule_timeout(msecs_to_jiffies(1000));
		count++;
	}

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		      "Returning from device reset %s\n",
		      (ret == SUCCESS) ?
		      "SUCCESS" : "FAILED");

	if (ret == FAILED)
		atomic64_inc(&reset_stats->device_reset_failures);

	return ret;
}

static void fnic_post_flogo_linkflap(struct fnic *fnic)
{
	unsigned long flags;

	fnic_fdls_link_status_change(fnic, 0);
	spin_lock_irqsave(&fnic->fnic_lock, flags);

	if (fnic->link_status) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		fnic_fdls_link_status_change(fnic, 1);
		return;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

/* Logout from all the targets and simulate link flap */
void fnic_reset(struct Scsi_Host *shost)
{
	struct fnic *fnic;
	struct reset_stats *reset_stats;

	fnic = *((struct fnic **) shost_priv(shost));
	reset_stats = &fnic->fnic_stats.reset_stats;

	FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				  "Issuing fnic reset\n");

	atomic64_inc(&reset_stats->fnic_resets);
	fnic_post_flogo_linkflap(fnic);

	FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				  "Returning from fnic reset");

	atomic64_inc(&reset_stats->fnic_reset_completions);
}

int fnic_issue_fc_host_lip(struct Scsi_Host *shost)
{
	int ret = 0;
	struct fnic *fnic = *((struct fnic **) shost_priv(shost));

	FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				  "FC host lip issued");

	ret = fnic_host_reset(shost);
	return ret;
}

int fnic_host_reset(struct Scsi_Host *shost)
{
	int ret = SUCCESS;
	unsigned long wait_host_tmo;
	struct fnic *fnic = *((struct fnic **) shost_priv(shost));
	unsigned long flags;
	struct fnic_iport_s *iport = &fnic->iport;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (fnic->reset_in_progress == NOT_IN_PROGRESS) {
		fnic->reset_in_progress = IN_PROGRESS;
	} else {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		wait_for_completion_timeout(&fnic->reset_completion_wait,
									msecs_to_jiffies(10000));

		spin_lock_irqsave(&fnic->fnic_lock, flags);
		if (fnic->reset_in_progress == IN_PROGRESS) {
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			FNIC_SCSI_DBG(KERN_WARNING, fnic->host, fnic->fnic_num,
			  "Firmware reset in progress. Skipping another host reset\n");
			return SUCCESS;
		}
		fnic->reset_in_progress = IN_PROGRESS;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	/*
	 * If fnic_reset is successful, wait for fabric login to complete
	 * scsi-ml tries to send a TUR to every device if host reset is
	 * successful, so before returning to scsi, fabric should be up
	 */
	fnic_reset(shost);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->reset_in_progress = NOT_IN_PROGRESS;
	complete(&fnic->reset_completion_wait);
	fnic->soft_reset_count++;

	/* wait till the link is up */
	if (fnic->link_status) {
		wait_host_tmo = jiffies + FNIC_HOST_RESET_SETTLE_TIME * HZ;
		ret = FAILED;
		while (time_before(jiffies, wait_host_tmo)) {
			if (iport->state != FNIC_IPORT_STATE_READY
				&& fnic->link_status) {
				spin_unlock_irqrestore(&fnic->fnic_lock, flags);
				ssleep(1);
				spin_lock_irqsave(&fnic->fnic_lock, flags);
			} else {
				ret = SUCCESS;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				  "host reset return status: %d\n", ret);
	return ret;
}

static bool fnic_abts_pending_iter(struct scsi_cmnd *sc, void *data)
{
	struct request *const rq = scsi_cmd_to_rq(sc);
	struct fnic_pending_aborts_iter_data *iter_data = data;
	struct fnic *fnic = iter_data->fnic;
	int cmd_state;
	struct fnic_io_req *io_req;
	unsigned long flags;
	uint16_t hwq = 0;
	int tag;

	tag = blk_mq_unique_tag(rq);
	hwq = blk_mq_unique_tag_to_hwq(tag);

	/*
	 * ignore this lun reset cmd or cmds that do not belong to
	 * this lun
	 */
	if (iter_data->lr_sc && sc == iter_data->lr_sc)
		return true;
	if (iter_data->lun_dev && sc->device != iter_data->lun_dev)
		return true;

	spin_lock_irqsave(&fnic->wq_copy_lock[hwq], flags);

	io_req = fnic_priv(sc)->io_req;
	if (!io_req) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
		return true;
	}

	/*
	 * Found IO that is still pending with firmware and
	 * belongs to the LUN that we are resetting
	 */
	FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		"hwq: %d tag: 0x%x Found IO in state: %s on lun\n",
		hwq, tag,
		fnic_ioreq_state_to_str(fnic_priv(sc)->state));
	cmd_state = fnic_priv(sc)->state;
	spin_unlock_irqrestore(&fnic->wq_copy_lock[hwq], flags);
	if (cmd_state == FNIC_IOREQ_ABTS_PENDING)
		iter_data->ret = 1;

	return iter_data->ret ? false : true;
}

/*
 * fnic_is_abts_pending() is a helper function that
 * walks through tag map to check if there is any IOs pending,if there is one,
 * then it returns 1 (true), otherwise 0 (false)
 * if @lr_sc is non NULL, then it checks IOs specific to particular LUN,
 * otherwise, it checks for all IOs.
 */
int fnic_is_abts_pending(struct fnic *fnic, struct scsi_cmnd *lr_sc)
{
	struct fnic_pending_aborts_iter_data iter_data = {
		.fnic = fnic,
		.lun_dev = NULL,
		.ret = 0,
	};

	if (lr_sc) {
		iter_data.lun_dev = lr_sc->device;
		iter_data.lr_sc = lr_sc;
	}

	/* walk again to check, if IOs are still pending in fw */
	scsi_host_busy_iter(fnic->host,
			    fnic_abts_pending_iter, &iter_data);

	return iter_data.ret;
}

/*
 * SCSI Error handling calls driver's eh_host_reset if all prior
 * error handling levels return FAILED. If host reset completes
 * successfully, and if link is up, then Fabric login begins.
 *
 * Host Reset is the highest level of error recovery. If this fails, then
 * host is offlined by SCSI.
 *
 */
int fnic_eh_host_reset_handler(struct scsi_cmnd *sc)
{
	int ret = 0;
	struct Scsi_Host *shost = sc->device->host;
	struct fnic *fnic = *((struct fnic **) shost_priv(shost));

	FNIC_SCSI_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
				  "SCSI error handling: fnic host reset");

	ret = fnic_host_reset(shost);
	return ret;
}


void fnic_scsi_fcpio_reset(struct fnic *fnic)
{
	unsigned long flags;
	enum fnic_state old_state;
	struct fnic_iport_s *iport = &fnic->iport;
	DECLARE_COMPLETION_ONSTACK(fw_reset_done);
	int time_remain;

	/* issue fw reset */
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (unlikely(fnic->state == FNIC_IN_FC_TRANS_ETH_MODE)) {
		/* fw reset is in progress, poll for its completion */
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			  "fnic is in unexpected state: %d for fw_reset\n",
			  fnic->state);
		return;
	}

	old_state = fnic->state;
	fnic->state = FNIC_IN_FC_TRANS_ETH_MODE;

	fnic_update_mac_locked(fnic, iport->hwmac);
	fnic->fw_reset_done = &fw_reset_done;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				  "Issuing fw reset\n");
	if (fnic_fw_reset_handler(fnic)) {
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		if (fnic->state == FNIC_IN_FC_TRANS_ETH_MODE)
			fnic->state = old_state;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	} else {
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					  "Waiting for fw completion\n");
		time_remain = wait_for_completion_timeout(&fw_reset_done,
						  msecs_to_jiffies(FNIC_FW_RESET_TIMEOUT));
		FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					  "Woken up after fw completion timeout\n");
		if (time_remain == 0) {
			FNIC_SCSI_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				  "FW reset completion timed out after %d ms)\n",
				  FNIC_FW_RESET_TIMEOUT);
		}
		atomic64_inc(&fnic->fnic_stats.reset_stats.fw_reset_timeouts);
	}
	fnic->fw_reset_done = NULL;
}
