/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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
#include <scsi/libfc.h>
#include <scsi/fc_frame.h>
#include "fnic_io.h"
#include "fnic.h"

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

static void fnic_cleanup_io(struct fnic *fnic);

static inline spinlock_t *fnic_io_lock_hash(struct fnic *fnic,
					    struct scsi_cmnd *sc)
{
	u32 hash = scsi_cmd_to_rq(sc)->tag & (FNIC_IO_LOCKS - 1);

	return &fnic->io_req_lock[hash];
}

static inline spinlock_t *fnic_io_lock_tag(struct fnic *fnic,
					    int tag)
{
	return &fnic->io_req_lock[tag & (FNIC_IO_LOCKS - 1)];
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

/* Free up Copy Wq descriptors. Called with copy_wq lock held */
static int free_wq_copy_descs(struct fnic *fnic, struct vnic_wq_copy *wq)
{
	/* if no Ack received from firmware, then nothing to clean */
	if (!fnic->fw_ack_recd[0])
		return 1;

	/*
	 * Update desc_available count based on number of freed descriptors
	 * Account for wraparound
	 */
	if (wq->to_clean_index <= fnic->fw_ack_index[0])
		wq->ring.desc_avail += (fnic->fw_ack_index[0]
					- wq->to_clean_index + 1);
	else
		wq->ring.desc_avail += (wq->ring.desc_count
					- wq->to_clean_index
					+ fnic->fw_ack_index[0] + 1);

	/*
	 * just bump clean index to ack_index+1 accounting for wraparound
	 * this will essentially free up all descriptors between
	 * to_clean_index and fw_ack_index, both inclusive
	 */
	wq->to_clean_index =
		(fnic->fw_ack_index[0] + 1) % wq->ring.desc_count;

	/* we have processed the acks received so far */
	fnic->fw_ack_recd[0] = 0;
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
	unsigned long host_lock_flags = 0;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	spin_lock_irqsave(fnic->lport->host->host_lock, host_lock_flags);

	if (clearbits)
		fnic->state_flags &= ~st_flags;
	else
		fnic->state_flags |= st_flags;

	spin_unlock_irqrestore(fnic->lport->host->host_lock, host_lock_flags);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	return;
}


/*
 * fnic_fw_reset_handler
 * Routine to send reset msg to fw
 */
int fnic_fw_reset_handler(struct fnic *fnic)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	int ret = 0;
	unsigned long flags;

	/* indicate fwreset to io path */
	fnic_set_state_flags(fnic, FNIC_FLAGS_FWRESET);

	skb_queue_purge(&fnic->frame_queue);
	skb_queue_purge(&fnic->tx_queue);

	/* wait for io cmpl */
	while (atomic_read(&fnic->in_flight))
		schedule_timeout(msecs_to_jiffies(1));

	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);

	if (!vnic_wq_copy_desc_avail(wq))
		ret = -EAGAIN;
	else {
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
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "Issued fw reset\n");
	} else {
		fnic_clear_state_flags(fnic, FNIC_FLAGS_FWRESET);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
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
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	enum fcpio_flogi_reg_format_type format;
	struct fc_lport *lp = fnic->lport;
	u8 gw_mac[ETH_ALEN];
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);

	if (!vnic_wq_copy_desc_avail(wq)) {
		ret = -EAGAIN;
		goto flogi_reg_ioreq_end;
	}

	if (fnic->ctlr.map_dest) {
		eth_broadcast_addr(gw_mac);
		format = FCPIO_FLOGI_REG_DEF_DEST;
	} else {
		memcpy(gw_mac, fnic->ctlr.dest_addr, ETH_ALEN);
		format = FCPIO_FLOGI_REG_GW_DEST;
	}

	if ((fnic->config.flags & VFCF_FIP_CAPABLE) && !fnic->ctlr.map_dest) {
		fnic_queue_wq_copy_desc_fip_reg(wq, SCSI_NO_TAG,
						fc_id, gw_mac,
						fnic->data_src_addr,
						lp->r_a_tov, lp->e_d_tov);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "FLOGI FIP reg issued fcid %x src %pM dest %pM\n",
			      fc_id, fnic->data_src_addr, gw_mac);
	} else {
		fnic_queue_wq_copy_desc_flogi_reg(wq, SCSI_NO_TAG,
						  format, fc_id, gw_mac);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "FLOGI reg issued fcid %x map %d dest %pM\n",
			      fc_id, fnic->ctlr.map_dest, gw_mac);
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
					  int sg_count)
{
	struct scatterlist *sg;
	struct fc_rport *rport = starget_to_rport(scsi_target(sc->device));
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct host_sg_desc *desc;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	unsigned int i;
	unsigned long intr_flags;
	int flags;
	u8 exch_flags;
	struct scsi_lun fc_lun;

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
	spin_lock_irqsave(&fnic->wq_copy_lock[0], intr_flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);

	if (unlikely(!vnic_wq_copy_desc_avail(wq))) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[0], intr_flags);
		FNIC_SCSI_DBG(KERN_INFO, fnic->lport->host,
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
	    (rp->flags & FC_RP_FLAGS_RETRY))
		exch_flags |= FCPIO_ICMND_SRFLAG_RETRY;

	fnic_queue_wq_copy_desc_icmnd_16(wq, scsi_cmd_to_rq(sc)->tag,
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
					 rport->maxframe_size, rp->r_a_tov,
					 rp->e_d_tov);

	atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
	if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
		  atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
		atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
		  atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], intr_flags);
	return 0;
}

/*
 * fnic_queuecommand
 * Routine to send a scsi cdb
 * Called with host_lock held and interrupts disabled.
 */
static int fnic_queuecommand_lck(struct scsi_cmnd *sc, void (*done)(struct scsi_cmnd *))
{
	const int tag = scsi_cmd_to_rq(sc)->tag;
	struct fc_lport *lp = shost_priv(sc->device->host);
	struct fc_rport *rport;
	struct fnic_io_req *io_req = NULL;
	struct fnic *fnic = lport_priv(lp);
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	struct vnic_wq_copy *wq;
	int ret;
	u64 cmd_trace;
	int sg_count = 0;
	unsigned long flags = 0;
	unsigned long ptr;
	spinlock_t *io_lock = NULL;
	int io_lock_acquired = 0;
	struct fc_rport_libfc_priv *rp;

	if (unlikely(fnic_chk_state_flags_locked(fnic, FNIC_FLAGS_IO_BLOCKED)))
		return SCSI_MLQUEUE_HOST_BUSY;

	if (unlikely(fnic_chk_state_flags_locked(fnic, FNIC_FLAGS_FWRESET)))
		return SCSI_MLQUEUE_HOST_BUSY;

	rport = starget_to_rport(scsi_target(sc->device));
	if (!rport) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
				"returning DID_NO_CONNECT for IO as rport is NULL\n");
		sc->result = DID_NO_CONNECT << 16;
		done(sc);
		return 0;
	}

	ret = fc_remote_port_chkready(rport);
	if (ret) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
				"rport is not ready\n");
		atomic64_inc(&fnic_stats->misc_stats.rport_not_ready);
		sc->result = ret;
		done(sc);
		return 0;
	}

	rp = rport->dd_data;
	if (!rp || rp->rp_state == RPORT_ST_DELETE) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			"rport 0x%x removed, returning DID_NO_CONNECT\n",
			rport->port_id);

		atomic64_inc(&fnic_stats->misc_stats.rport_not_ready);
		sc->result = DID_NO_CONNECT<<16;
		done(sc);
		return 0;
	}

	if (rp->rp_state != RPORT_ST_READY) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			"rport 0x%x in state 0x%x, returning DID_IMM_RETRY\n",
			rport->port_id, rp->rp_state);

		sc->result = DID_IMM_RETRY << 16;
		done(sc);
		return 0;
	}

	if (lp->state != LPORT_ST_READY || !(lp->link_up))
		return SCSI_MLQUEUE_HOST_BUSY;

	atomic_inc(&fnic->in_flight);

	/*
	 * Release host lock, use driver resource specific locks from here.
	 * Don't re-enable interrupts in case they were disabled prior to the
	 * caller disabling them.
	 */
	spin_unlock(lp->host->host_lock);
	CMD_STATE(sc) = FNIC_IOREQ_NOT_INITED;
	CMD_FLAGS(sc) = FNIC_NO_FLAGS;

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
			  tag, sc, 0, sc->cmnd[0], sg_count, CMD_STATE(sc));
		mempool_free(io_req, fnic->io_req_pool);
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
	* Will acquire lock defore setting to IO initialized.
	*/

	io_lock = fnic_io_lock_hash(fnic, sc);
	spin_lock_irqsave(io_lock, flags);

	/* initialize rest of io_req */
	io_lock_acquired = 1;
	io_req->port_id = rport->port_id;
	io_req->start_time = jiffies;
	CMD_STATE(sc) = FNIC_IOREQ_CMD_PENDING;
	CMD_SP(sc) = (char *)io_req;
	CMD_FLAGS(sc) |= FNIC_IO_INITIALIZED;
	sc->scsi_done = done;

	/* create copy wq desc and enqueue it */
	wq = &fnic->wq_copy[0];
	ret = fnic_queue_wq_copy_desc(fnic, wq, io_req, sc, sg_count);
	if (ret) {
		/*
		 * In case another thread cancelled the request,
		 * refetch the pointer under the lock.
		 */
		FNIC_TRACE(fnic_queuecommand, sc->device->host->host_no,
			  tag, sc, 0, 0, 0,
			  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		CMD_SP(sc) = NULL;
		CMD_STATE(sc) = FNIC_IOREQ_CMD_COMPLETE;
		spin_unlock_irqrestore(io_lock, flags);
		if (io_req) {
			fnic_release_ioreq_buf(fnic, io_req, sc);
			mempool_free(io_req, fnic->io_req_pool);
		}
		atomic_dec(&fnic->in_flight);
		/* acquire host lock before returning to SCSI */
		spin_lock(lp->host->host_lock);
		return ret;
	} else {
		atomic64_inc(&fnic_stats->io_stats.active_ios);
		atomic64_inc(&fnic_stats->io_stats.num_ios);
		if (atomic64_read(&fnic_stats->io_stats.active_ios) >
			  atomic64_read(&fnic_stats->io_stats.max_active_ios))
			atomic64_set(&fnic_stats->io_stats.max_active_ios,
			     atomic64_read(&fnic_stats->io_stats.active_ios));

		/* REVISIT: Use per IO lock in the final code */
		CMD_FLAGS(sc) |= FNIC_IO_ISSUED;
	}
out:
	cmd_trace = ((u64)sc->cmnd[0] << 56 | (u64)sc->cmnd[7] << 40 |
			(u64)sc->cmnd[8] << 32 | (u64)sc->cmnd[2] << 24 |
			(u64)sc->cmnd[3] << 16 | (u64)sc->cmnd[4] << 8 |
			sc->cmnd[5]);

	FNIC_TRACE(fnic_queuecommand, sc->device->host->host_no,
		  tag, sc, io_req, sg_count, cmd_trace,
		  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));

	/* if only we issued IO, will we have the io lock */
	if (io_lock_acquired)
		spin_unlock_irqrestore(io_lock, flags);

	atomic_dec(&fnic->in_flight);
	/* acquire host lock before returning to SCSI */
	spin_lock(lp->host->host_lock);
	return ret;
}

DEF_SCSI_QCMD(fnic_queuecommand)

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
	fnic_cleanup_io(fnic);

	atomic64_set(&fnic->fnic_stats.fw_stats.active_fw_reqs, 0);
	atomic64_set(&fnic->fnic_stats.io_stats.active_ios, 0);
	atomic64_set(&fnic->io_cmpl_skip, 0);

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	/* fnic should be in FC_TRANS_ETH_MODE */
	if (fnic->state == FNIC_IN_FC_TRANS_ETH_MODE) {
		/* Check status of reset completion */
		if (!hdr_status) {
			FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
				      "reset cmpl success\n");
			/* Ready to send flogi out */
			fnic->state = FNIC_IN_ETH_MODE;
		} else {
			FNIC_SCSI_DBG(KERN_DEBUG,
				      fnic->lport->host,
				      "fnic fw_reset : failed %s\n",
				      fnic_fcpio_status_to_str(hdr_status));

			/*
			 * Unable to change to eth mode, cannot send out flogi
			 * Change state to fc mode, so that subsequent Flogi
			 * requests from libFC will cause more attempts to
			 * reset the firmware. Free the cached flogi
			 */
			fnic->state = FNIC_IN_FC_MODE;
			atomic64_inc(&reset_stats->fw_reset_failures);
			ret = -1;
		}
	} else {
		FNIC_SCSI_DBG(KERN_DEBUG,
			      fnic->lport->host,
			      "Unexpected state %s while processing"
			      " reset cmpl\n", fnic_state_to_str(fnic->state));
		atomic64_inc(&reset_stats->fw_reset_failures);
		ret = -1;
	}

	/* Thread removing device blocks till firmware reset is complete */
	if (fnic->remove_wait)
		complete(fnic->remove_wait);

	/*
	 * If fnic is being removed, or fw reset failed
	 * free the flogi frame. Else, send it out
	 */
	if (fnic->remove_wait || ret) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		skb_queue_purge(&fnic->tx_queue);
		goto reset_cmpl_handler_end;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	fnic_flush_tx(fnic);

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
			FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
				      "flog reg succeeded\n");
			fnic->state = FNIC_IN_FC_MODE;
		} else {
			FNIC_SCSI_DBG(KERN_DEBUG,
				      fnic->lport->host,
				      "fnic flogi reg :failed %s\n",
				      fnic_fcpio_status_to_str(hdr_status));
			fnic->state = FNIC_IN_ETH_MODE;
			ret = -1;
		}
	} else {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
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

		fnic_flush_tx(fnic);
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

	/* mark the ack state */
	wq = &fnic->wq_copy[cq_index - fnic->raw_wq_count - fnic->rq_count];
	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	fnic->fnic_stats.misc_stats.last_ack_time = jiffies;
	if (is_ack_index_in_range(wq, request_out)) {
		fnic->fw_ack_index[0] = request_out;
		fnic->fw_ack_recd[0] = 1;
	} else
		atomic64_inc(
			&fnic->fnic_stats.misc_stats.ack_index_out_of_range);

	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);
	FNIC_TRACE(fnic_fcpio_ack_handler,
		  fnic->lport->host->host_no, 0, 0, ox_id_tag[2], ox_id_tag[3],
		  ox_id_tag[4], ox_id_tag[5]);
}

/*
 * fnic_fcpio_icmnd_cmpl_handler
 * Routine to handle icmnd completions
 */
static void fnic_fcpio_icmnd_cmpl_handler(struct fnic *fnic,
					 struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag tag;
	u32 id;
	u64 xfer_len = 0;
	struct fcpio_icmnd_cmpl *icmnd_cmpl;
	struct fnic_io_req *io_req;
	struct scsi_cmnd *sc;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	unsigned long flags;
	spinlock_t *io_lock;
	u64 cmd_trace;
	unsigned long start_time;
	unsigned long io_duration_time;

	/* Decode the cmpl description to get the io_req id */
	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);
	fcpio_tag_id_dec(&tag, &id);
	icmnd_cmpl = &desc->u.icmnd_cmpl;

	if (id >= fnic->fnic_max_tag_id) {
		shost_printk(KERN_ERR, fnic->lport->host,
			"Tag out of range tag %x hdr status = %s\n",
			     id, fnic_fcpio_status_to_str(hdr_status));
		return;
	}

	sc = scsi_host_find_tag(fnic->lport->host, id);
	WARN_ON_ONCE(!sc);
	if (!sc) {
		atomic64_inc(&fnic_stats->io_stats.sc_null);
		shost_printk(KERN_ERR, fnic->lport->host,
			  "icmnd_cmpl sc is null - "
			  "hdr status = %s tag = 0x%x desc = 0x%p\n",
			  fnic_fcpio_status_to_str(hdr_status), id, desc);
		FNIC_TRACE(fnic_fcpio_icmnd_cmpl_handler,
			  fnic->lport->host->host_no, id,
			  ((u64)icmnd_cmpl->_resvd0[1] << 16 |
			  (u64)icmnd_cmpl->_resvd0[0]),
			  ((u64)hdr_status << 16 |
			  (u64)icmnd_cmpl->scsi_status << 8 |
			  (u64)icmnd_cmpl->flags), desc,
			  (u64)icmnd_cmpl->residual, 0);
		return;
	}

	io_lock = fnic_io_lock_hash(fnic, sc);
	spin_lock_irqsave(io_lock, flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);
	WARN_ON_ONCE(!io_req);
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.ioreq_null);
		CMD_FLAGS(sc) |= FNIC_IO_REQ_NULL;
		spin_unlock_irqrestore(io_lock, flags);
		shost_printk(KERN_ERR, fnic->lport->host,
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
	if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING) {

		/*
		 * set the FNIC_IO_DONE so that this doesn't get
		 * flagged as 'out of order' if it was not aborted
		 */
		CMD_FLAGS(sc) |= FNIC_IO_DONE;
		CMD_FLAGS(sc) |= FNIC_IO_ABTS_PENDING;
		spin_unlock_irqrestore(io_lock, flags);
		if(FCPIO_ABORTED == hdr_status)
			CMD_FLAGS(sc) |= FNIC_IO_ABORTED;

		FNIC_SCSI_DBG(KERN_INFO, fnic->lport->host,
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
	CMD_STATE(sc) = FNIC_IOREQ_CMD_COMPLETE;

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
	CMD_SP(sc) = NULL;
	CMD_FLAGS(sc) |= FNIC_IO_DONE;

	spin_unlock_irqrestore(io_lock, flags);

	if (hdr_status != FCPIO_SUCCESS) {
		atomic64_inc(&fnic_stats->io_stats.io_failures);
		shost_printk(KERN_ERR, fnic->lport->host, "hdr status = %s\n",
			     fnic_fcpio_status_to_str(hdr_status));
	}

	fnic_release_ioreq_buf(fnic, io_req, sc);

	mempool_free(io_req, fnic->io_req_pool);

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
		  desc, cmd_trace,
		  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));

	if (sc->sc_data_direction == DMA_FROM_DEVICE) {
		fnic->lport->host_stats.fcp_input_requests++;
		fnic->fcp_input_bytes += xfer_len;
	} else if (sc->sc_data_direction == DMA_TO_DEVICE) {
		fnic->lport->host_stats.fcp_output_requests++;
		fnic->fcp_output_bytes += xfer_len;
	} else
		fnic->lport->host_stats.fcp_control_requests++;

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

	/* Call SCSI completion function to complete the IO */
	if (sc->scsi_done)
		sc->scsi_done(sc);
}

/* fnic_fcpio_itmf_cmpl_handler
 * Routine to handle itmf completions
 */
static void fnic_fcpio_itmf_cmpl_handler(struct fnic *fnic,
					struct fcpio_fw_req *desc)
{
	u8 type;
	u8 hdr_status;
	struct fcpio_tag tag;
	u32 id;
	struct scsi_cmnd *sc;
	struct fnic_io_req *io_req;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	struct abort_stats *abts_stats = &fnic->fnic_stats.abts_stats;
	struct terminate_stats *term_stats = &fnic->fnic_stats.term_stats;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	unsigned long flags;
	spinlock_t *io_lock;
	unsigned long start_time;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);
	fcpio_tag_id_dec(&tag, &id);

	if ((id & FNIC_TAG_MASK) >= fnic->fnic_max_tag_id) {
		shost_printk(KERN_ERR, fnic->lport->host,
		"Tag out of range tag %x hdr status = %s\n",
		id, fnic_fcpio_status_to_str(hdr_status));
		return;
	}

	sc = scsi_host_find_tag(fnic->lport->host, id & FNIC_TAG_MASK);
	WARN_ON_ONCE(!sc);
	if (!sc) {
		atomic64_inc(&fnic_stats->io_stats.sc_null);
		shost_printk(KERN_ERR, fnic->lport->host,
			  "itmf_cmpl sc is null - hdr status = %s tag = 0x%x\n",
			  fnic_fcpio_status_to_str(hdr_status), id);
		return;
	}
	io_lock = fnic_io_lock_hash(fnic, sc);
	spin_lock_irqsave(io_lock, flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);
	WARN_ON_ONCE(!io_req);
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.ioreq_null);
		spin_unlock_irqrestore(io_lock, flags);
		CMD_FLAGS(sc) |= FNIC_IO_ABT_TERM_REQ_NULL;
		shost_printk(KERN_ERR, fnic->lport->host,
			  "itmf_cmpl io_req is null - "
			  "hdr status = %s tag = 0x%x sc 0x%p\n",
			  fnic_fcpio_status_to_str(hdr_status), id, sc);
		return;
	}
	start_time = io_req->start_time;

	if ((id & FNIC_TAG_ABORT) && (id & FNIC_TAG_DEV_RST)) {
		/* Abort and terminate completion of device reset req */
		/* REVISIT : Add asserts about various flags */
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "dev reset abts cmpl recd. id %x status %s\n",
			      id, fnic_fcpio_status_to_str(hdr_status));
		CMD_STATE(sc) = FNIC_IOREQ_ABTS_COMPLETE;
		CMD_ABTS_STATUS(sc) = hdr_status;
		CMD_FLAGS(sc) |= FNIC_DEV_RST_DONE;
		if (io_req->abts_done)
			complete(io_req->abts_done);
		spin_unlock_irqrestore(io_lock, flags);
	} else if (id & FNIC_TAG_ABORT) {
		/* Completion of abort cmd */
		switch (hdr_status) {
		case FCPIO_SUCCESS:
			break;
		case FCPIO_TIMEOUT:
			if (CMD_FLAGS(sc) & FNIC_IO_ABTS_ISSUED)
				atomic64_inc(&abts_stats->abort_fw_timeouts);
			else
				atomic64_inc(
					&term_stats->terminate_fw_timeouts);
			break;
		case FCPIO_ITMF_REJECTED:
			FNIC_SCSI_DBG(KERN_INFO, fnic->lport->host,
				"abort reject recd. id %d\n",
				(int)(id & FNIC_TAG_MASK));
			break;
		case FCPIO_IO_NOT_FOUND:
			if (CMD_FLAGS(sc) & FNIC_IO_ABTS_ISSUED)
				atomic64_inc(&abts_stats->abort_io_not_found);
			else
				atomic64_inc(
					&term_stats->terminate_io_not_found);
			break;
		default:
			if (CMD_FLAGS(sc) & FNIC_IO_ABTS_ISSUED)
				atomic64_inc(&abts_stats->abort_failures);
			else
				atomic64_inc(
					&term_stats->terminate_failures);
			break;
		}
		if (CMD_STATE(sc) != FNIC_IOREQ_ABTS_PENDING) {
			/* This is a late completion. Ignore it */
			spin_unlock_irqrestore(io_lock, flags);
			return;
		}

		CMD_FLAGS(sc) |= FNIC_IO_ABT_TERM_DONE;
		CMD_ABTS_STATUS(sc) = hdr_status;

		/* If the status is IO not found consider it as success */
		if (hdr_status == FCPIO_IO_NOT_FOUND)
			CMD_ABTS_STATUS(sc) = FCPIO_SUCCESS;

		if (!(CMD_FLAGS(sc) & (FNIC_IO_ABORTED | FNIC_IO_DONE)))
			atomic64_inc(&misc_stats->no_icmnd_itmf_cmpls);

		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
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
			spin_unlock_irqrestore(io_lock, flags);
		} else {
			FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
				      "abts cmpl, completing IO\n");
			CMD_SP(sc) = NULL;
			sc->result = (DID_ERROR << 16);

			spin_unlock_irqrestore(io_lock, flags);

			fnic_release_ioreq_buf(fnic, io_req, sc);
			mempool_free(io_req, fnic->io_req_pool);
			if (sc->scsi_done) {
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
					(((u64)CMD_FLAGS(sc) << 32) |
					CMD_STATE(sc)));
				sc->scsi_done(sc);
				atomic64_dec(&fnic_stats->io_stats.active_ios);
				if (atomic64_read(&fnic->io_cmpl_skip))
					atomic64_dec(&fnic->io_cmpl_skip);
				else
					atomic64_inc(&fnic_stats->io_stats.io_completions);
			}
		}

	} else if (id & FNIC_TAG_DEV_RST) {
		/* Completion of device reset */
		CMD_LR_STATUS(sc) = hdr_status;
		if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING) {
			spin_unlock_irqrestore(io_lock, flags);
			CMD_FLAGS(sc) |= FNIC_DEV_RST_ABTS_PENDING;
			FNIC_TRACE(fnic_fcpio_itmf_cmpl_handler,
				  sc->device->host->host_no, id, sc,
				  jiffies_to_msecs(jiffies - start_time),
				  desc, 0,
				  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));
			FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
				"Terminate pending "
				"dev reset cmpl recd. id %d status %s\n",
				(int)(id & FNIC_TAG_MASK),
				fnic_fcpio_status_to_str(hdr_status));
			return;
		}
		if (CMD_FLAGS(sc) & FNIC_DEV_RST_TIMED_OUT) {
			/* Need to wait for terminate completion */
			spin_unlock_irqrestore(io_lock, flags);
			FNIC_TRACE(fnic_fcpio_itmf_cmpl_handler,
				  sc->device->host->host_no, id, sc,
				  jiffies_to_msecs(jiffies - start_time),
				  desc, 0,
				  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));
			FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
				"dev reset cmpl recd after time out. "
				"id %d status %s\n",
				(int)(id & FNIC_TAG_MASK),
				fnic_fcpio_status_to_str(hdr_status));
			return;
		}
		CMD_STATE(sc) = FNIC_IOREQ_CMD_COMPLETE;
		CMD_FLAGS(sc) |= FNIC_DEV_RST_DONE;
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "dev reset cmpl recd. id %d status %s\n",
			      (int)(id & FNIC_TAG_MASK),
			      fnic_fcpio_status_to_str(hdr_status));
		if (io_req->dr_done)
			complete(io_req->dr_done);
		spin_unlock_irqrestore(io_lock, flags);

	} else {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "Unexpected itmf io state %s tag %x\n",
			     fnic_ioreq_state_to_str(CMD_STATE(sc)), id);
		spin_unlock_irqrestore(io_lock, flags);
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

	switch (desc->hdr.type) {
	case FCPIO_ACK: /* fw copied copy wq desc to its queue */
		fnic_fcpio_ack_handler(fnic, cq_index, desc);
		break;

	case FCPIO_ICMND_CMPL: /* fw completed a command */
		fnic_fcpio_icmnd_cmpl_handler(fnic, desc);
		break;

	case FCPIO_ITMF_CMPL: /* fw completed itmf (abort cmd, lun reset)*/
		fnic_fcpio_itmf_cmpl_handler(fnic, desc);
		break;

	case FCPIO_FLOGI_REG_CMPL: /* fw completed flogi_reg */
	case FCPIO_FLOGI_FIP_REG_CMPL: /* fw completed flogi_fip_reg */
		fnic_fcpio_flogi_reg_cmpl_handler(fnic, desc);
		break;

	case FCPIO_RESET_CMPL: /* fw completed reset */
		fnic_fcpio_fw_reset_cmpl_handler(fnic, desc);
		break;

	default:
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
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
int fnic_wq_copy_cmpl_handler(struct fnic *fnic, int copy_work_to_do)
{
	unsigned int wq_work_done = 0;
	unsigned int i, cq_index;
	unsigned int cur_work_done;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	u64 start_jiffies = 0;
	u64 end_jiffies = 0;
	u64 delta_jiffies = 0;
	u64 delta_ms = 0;

	for (i = 0; i < fnic->wq_copy_count; i++) {
		cq_index = i + fnic->raw_wq_count + fnic->rq_count;

		start_jiffies = jiffies;
		cur_work_done = vnic_cq_copy_service(&fnic->cq[cq_index],
						     fnic_fcpio_cmpl_handler,
						     copy_work_to_do);
		end_jiffies = jiffies;

		wq_work_done += cur_work_done;
		delta_jiffies = end_jiffies - start_jiffies;
		if (delta_jiffies >
			(u64) atomic64_read(&misc_stats->max_isr_jiffies)) {
			atomic64_set(&misc_stats->max_isr_jiffies,
					delta_jiffies);
			delta_ms = jiffies_to_msecs(delta_jiffies);
			atomic64_set(&misc_stats->max_isr_time_ms, delta_ms);
			atomic64_set(&misc_stats->corr_work_done,
					cur_work_done);
		}
	}
	return wq_work_done;
}

static bool fnic_cleanup_io_iter(struct scsi_cmnd *sc, void *data,
				 bool reserved)
{
	const int tag = scsi_cmd_to_rq(sc)->tag;
	struct fnic *fnic = data;
	struct fnic_io_req *io_req;
	unsigned long flags = 0;
	spinlock_t *io_lock;
	unsigned long start_time = 0;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;

	io_lock = fnic_io_lock_tag(fnic, tag);
	spin_lock_irqsave(io_lock, flags);

	io_req = (struct fnic_io_req *)CMD_SP(sc);
	if ((CMD_FLAGS(sc) & FNIC_DEVICE_RESET) &&
	    !(CMD_FLAGS(sc) & FNIC_DEV_RST_DONE)) {
		/*
		 * We will be here only when FW completes reset
		 * without sending completions for outstanding ios.
		 */
		CMD_FLAGS(sc) |= FNIC_DEV_RST_DONE;
		if (io_req && io_req->dr_done)
			complete(io_req->dr_done);
		else if (io_req && io_req->abts_done)
			complete(io_req->abts_done);
		spin_unlock_irqrestore(io_lock, flags);
		return true;
	} else if (CMD_FLAGS(sc) & FNIC_DEVICE_RESET) {
		spin_unlock_irqrestore(io_lock, flags);
		return true;
	}
	if (!io_req) {
		spin_unlock_irqrestore(io_lock, flags);
		goto cleanup_scsi_cmd;
	}

	CMD_SP(sc) = NULL;

	spin_unlock_irqrestore(io_lock, flags);

	/*
	 * If there is a scsi_cmnd associated with this io_req, then
	 * free the corresponding state
	 */
	start_time = io_req->start_time;
	fnic_release_ioreq_buf(fnic, io_req, sc);
	mempool_free(io_req, fnic->io_req_pool);

cleanup_scsi_cmd:
	sc->result = DID_TRANSPORT_DISRUPTED << 16;
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "fnic_cleanup_io: tag:0x%x : sc:0x%p duration = %lu DID_TRANSPORT_DISRUPTED\n",
		      tag, sc, jiffies - start_time);

	if (atomic64_read(&fnic->io_cmpl_skip))
		atomic64_dec(&fnic->io_cmpl_skip);
	else
		atomic64_inc(&fnic_stats->io_stats.io_completions);

	/* Complete the command to SCSI */
	if (sc->scsi_done) {
		if (!(CMD_FLAGS(sc) & FNIC_IO_ISSUED))
			shost_printk(KERN_ERR, fnic->lport->host,
				     "Calling done for IO not issued to fw: tag:0x%x sc:0x%p\n",
				     tag, sc);

		FNIC_TRACE(fnic_cleanup_io,
			   sc->device->host->host_no, tag, sc,
			   jiffies_to_msecs(jiffies - start_time),
			   0, ((u64)sc->cmnd[0] << 32 |
			       (u64)sc->cmnd[2] << 24 |
			       (u64)sc->cmnd[3] << 16 |
			       (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
			   (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));

		sc->scsi_done(sc);
	}
	return true;
}

static void fnic_cleanup_io(struct fnic *fnic)
{
	scsi_host_busy_iter(fnic->lport->host,
			    fnic_cleanup_io_iter, fnic);
}

void fnic_wq_copy_cleanup_handler(struct vnic_wq_copy *wq,
				  struct fcpio_host_req *desc)
{
	u32 id;
	struct fnic *fnic = vnic_dev_priv(wq->vdev);
	struct fnic_io_req *io_req;
	struct scsi_cmnd *sc;
	unsigned long flags;
	spinlock_t *io_lock;
	unsigned long start_time = 0;

	/* get the tag reference */
	fcpio_tag_id_dec(&desc->hdr.tag, &id);
	id &= FNIC_TAG_MASK;

	if (id >= fnic->fnic_max_tag_id)
		return;

	sc = scsi_host_find_tag(fnic->lport->host, id);
	if (!sc)
		return;

	io_lock = fnic_io_lock_hash(fnic, sc);
	spin_lock_irqsave(io_lock, flags);

	/* Get the IO context which this desc refers to */
	io_req = (struct fnic_io_req *)CMD_SP(sc);

	/* fnic interrupts are turned off by now */

	if (!io_req) {
		spin_unlock_irqrestore(io_lock, flags);
		goto wq_copy_cleanup_scsi_cmd;
	}

	CMD_SP(sc) = NULL;

	spin_unlock_irqrestore(io_lock, flags);

	start_time = io_req->start_time;
	fnic_release_ioreq_buf(fnic, io_req, sc);
	mempool_free(io_req, fnic->io_req_pool);

wq_copy_cleanup_scsi_cmd:
	sc->result = DID_NO_CONNECT << 16;
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host, "wq_copy_cleanup_handler:"
		      " DID_NO_CONNECT\n");

	if (sc->scsi_done) {
		FNIC_TRACE(fnic_wq_copy_cleanup_handler,
			  sc->device->host->host_no, id, sc,
			  jiffies_to_msecs(jiffies - start_time),
			  0, ((u64)sc->cmnd[0] << 32 |
			  (u64)sc->cmnd[2] << 24 | (u64)sc->cmnd[3] << 16 |
			  (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
			  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));

		sc->scsi_done(sc);
	}
}

static inline int fnic_queue_abort_io_req(struct fnic *fnic, int tag,
					  u32 task_req, u8 *fc_lun,
					  struct fnic_io_req *io_req)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	struct Scsi_Host *host = fnic->lport->host;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	unsigned long flags;

	spin_lock_irqsave(host->host_lock, flags);
	if (unlikely(fnic_chk_state_flags_locked(fnic,
						FNIC_FLAGS_IO_BLOCKED))) {
		spin_unlock_irqrestore(host->host_lock, flags);
		return 1;
	} else
		atomic_inc(&fnic->in_flight);
	spin_unlock_irqrestore(host->host_lock, flags);

	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);

	if (!vnic_wq_copy_desc_avail(wq)) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);
		atomic_dec(&fnic->in_flight);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
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

	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);
	atomic_dec(&fnic->in_flight);

	return 0;
}

struct fnic_rport_abort_io_iter_data {
	struct fnic *fnic;
	u32 port_id;
	int term_cnt;
};

static bool fnic_rport_abort_io_iter(struct scsi_cmnd *sc, void *data,
				     bool reserved)
{
	struct fnic_rport_abort_io_iter_data *iter_data = data;
	struct fnic *fnic = iter_data->fnic;
	int abt_tag = scsi_cmd_to_rq(sc)->tag;
	struct fnic_io_req *io_req;
	spinlock_t *io_lock;
	unsigned long flags;
	struct reset_stats *reset_stats = &fnic->fnic_stats.reset_stats;
	struct terminate_stats *term_stats = &fnic->fnic_stats.term_stats;
	struct scsi_lun fc_lun;
	enum fnic_ioreq_state old_ioreq_state;

	io_lock = fnic_io_lock_tag(fnic, abt_tag);
	spin_lock_irqsave(io_lock, flags);

	io_req = (struct fnic_io_req *)CMD_SP(sc);

	if (!io_req || io_req->port_id != iter_data->port_id) {
		spin_unlock_irqrestore(io_lock, flags);
		return true;
	}

	if ((CMD_FLAGS(sc) & FNIC_DEVICE_RESET) &&
	    (!(CMD_FLAGS(sc) & FNIC_DEV_RST_ISSUED))) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			"fnic_rport_exch_reset dev rst not pending sc 0x%p\n",
			sc);
		spin_unlock_irqrestore(io_lock, flags);
		return true;
	}

	/*
	 * Found IO that is still pending with firmware and
	 * belongs to rport that went away
	 */
	if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING) {
		spin_unlock_irqrestore(io_lock, flags);
		return true;
	}
	if (io_req->abts_done) {
		shost_printk(KERN_ERR, fnic->lport->host,
			"fnic_rport_exch_reset: io_req->abts_done is set "
			"state is %s\n",
			fnic_ioreq_state_to_str(CMD_STATE(sc)));
	}

	if (!(CMD_FLAGS(sc) & FNIC_IO_ISSUED)) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "rport_exch_reset "
			     "IO not yet issued %p tag 0x%x flags "
			     "%x state %d\n",
			     sc, abt_tag, CMD_FLAGS(sc), CMD_STATE(sc));
	}
	old_ioreq_state = CMD_STATE(sc);
	CMD_STATE(sc) = FNIC_IOREQ_ABTS_PENDING;
	CMD_ABTS_STATUS(sc) = FCPIO_INVALID_CODE;
	if (CMD_FLAGS(sc) & FNIC_DEVICE_RESET) {
		atomic64_inc(&reset_stats->device_reset_terminates);
		abt_tag |= FNIC_TAG_DEV_RST;
	}
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "fnic_rport_exch_reset dev rst sc 0x%p\n", sc);
	BUG_ON(io_req->abts_done);

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "fnic_rport_reset_exch: Issuing abts\n");

	spin_unlock_irqrestore(io_lock, flags);

	/* Now queue the abort command to firmware */
	int_to_scsilun(sc->device->lun, &fc_lun);

	if (fnic_queue_abort_io_req(fnic, abt_tag,
				    FCPIO_ITMF_ABT_TASK_TERM,
				    fc_lun.scsi_lun, io_req)) {
		/*
		 * Revert the cmd state back to old state, if
		 * it hasn't changed in between. This cmd will get
		 * aborted later by scsi_eh, or cleaned up during
		 * lun reset
		 */
		spin_lock_irqsave(io_lock, flags);
		if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING)
			CMD_STATE(sc) = old_ioreq_state;
		spin_unlock_irqrestore(io_lock, flags);
	} else {
		spin_lock_irqsave(io_lock, flags);
		if (CMD_FLAGS(sc) & FNIC_DEVICE_RESET)
			CMD_FLAGS(sc) |= FNIC_DEV_RST_TERM_ISSUED;
		else
			CMD_FLAGS(sc) |= FNIC_IO_INTERNAL_TERM_ISSUED;
		spin_unlock_irqrestore(io_lock, flags);
		atomic64_inc(&term_stats->terminates);
		iter_data->term_cnt++;
	}
	return true;
}

static void fnic_rport_exch_reset(struct fnic *fnic, u32 port_id)
{
	struct terminate_stats *term_stats = &fnic->fnic_stats.term_stats;
	struct fnic_rport_abort_io_iter_data iter_data = {
		.fnic = fnic,
		.port_id = port_id,
		.term_cnt = 0,
	};

	FNIC_SCSI_DBG(KERN_DEBUG,
		      fnic->lport->host,
		      "fnic_rport_exch_reset called portid 0x%06x\n",
		      port_id);

	if (fnic->in_remove)
		return;

	scsi_host_busy_iter(fnic->lport->host, fnic_rport_abort_io_iter,
			    &iter_data);
	if (iter_data.term_cnt > atomic64_read(&term_stats->max_terminates))
		atomic64_set(&term_stats->max_terminates, iter_data.term_cnt);

}

void fnic_terminate_rport_io(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rdata;
	struct fc_lport *lport;
	struct fnic *fnic;

	if (!rport) {
		printk(KERN_ERR "fnic_terminate_rport_io: rport is NULL\n");
		return;
	}
	rdata = rport->dd_data;

	if (!rdata) {
		printk(KERN_ERR "fnic_terminate_rport_io: rdata is NULL\n");
		return;
	}
	lport = rdata->local_port;

	if (!lport) {
		printk(KERN_ERR "fnic_terminate_rport_io: lport is NULL\n");
		return;
	}
	fnic = lport_priv(lport);
	FNIC_SCSI_DBG(KERN_DEBUG,
		      fnic->lport->host, "fnic_terminate_rport_io called"
		      " wwpn 0x%llx, wwnn0x%llx, rport 0x%p, portid 0x%06x\n",
		      rport->port_name, rport->node_name, rport,
		      rport->port_id);

	if (fnic->in_remove)
		return;

	fnic_rport_exch_reset(fnic, rport->port_id);
}

/*
 * This function is exported to SCSI for sending abort cmnds.
 * A SCSI IO is represented by a io_req in the driver.
 * The ioreq is linked to the SCSI Cmd, thus a link with the ULP's IO.
 */
int fnic_abort_cmd(struct scsi_cmnd *sc)
{
	struct request *const rq = scsi_cmd_to_rq(sc);
	struct fc_lport *lp;
	struct fnic *fnic;
	struct fnic_io_req *io_req = NULL;
	struct fc_rport *rport;
	spinlock_t *io_lock;
	unsigned long flags;
	unsigned long start_time = 0;
	int ret = SUCCESS;
	u32 task_req = 0;
	struct scsi_lun fc_lun;
	struct fnic_stats *fnic_stats;
	struct abort_stats *abts_stats;
	struct terminate_stats *term_stats;
	enum fnic_ioreq_state old_ioreq_state;
	const int tag = rq->tag;
	unsigned long abt_issued_time;
	DECLARE_COMPLETION_ONSTACK(tm_done);

	/* Wait for rport to unblock */
	fc_block_scsi_eh(sc);

	/* Get local-port, check ready and link up */
	lp = shost_priv(sc->device->host);

	fnic = lport_priv(lp);
	fnic_stats = &fnic->fnic_stats;
	abts_stats = &fnic->fnic_stats.abts_stats;
	term_stats = &fnic->fnic_stats.term_stats;

	rport = starget_to_rport(scsi_target(sc->device));
	FNIC_SCSI_DBG(KERN_DEBUG,
		fnic->lport->host,
		"Abort Cmd called FCID 0x%x, LUN 0x%llx TAG %x flags %x\n",
		rport->port_id, sc->device->lun, tag, CMD_FLAGS(sc));

	CMD_FLAGS(sc) = FNIC_NO_FLAGS;

	if (lp->state != LPORT_ST_READY || !(lp->link_up)) {
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}

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
	 * The CMD_SP will not be cleared except while holding io_req_lock.
	 */
	io_lock = fnic_io_lock_hash(fnic, sc);
	spin_lock_irqsave(io_lock, flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);
	if (!io_req) {
		spin_unlock_irqrestore(io_lock, flags);
		goto fnic_abort_cmd_end;
	}

	io_req->abts_done = &tm_done;

	if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING) {
		spin_unlock_irqrestore(io_lock, flags);
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

	FNIC_SCSI_DBG(KERN_INFO, fnic->lport->host,
		"CBD Opcode: %02x Abort issued time: %lu msec\n", sc->cmnd[0], abt_issued_time);
	/*
	 * Command is still pending, need to abort it
	 * If the firmware completes the command after this point,
	 * the completion wont be done till mid-layer, since abort
	 * has already started.
	 */
	old_ioreq_state = CMD_STATE(sc);
	CMD_STATE(sc) = FNIC_IOREQ_ABTS_PENDING;
	CMD_ABTS_STATUS(sc) = FCPIO_INVALID_CODE;

	spin_unlock_irqrestore(io_lock, flags);

	/*
	 * Check readiness of the remote port. If the path to remote
	 * port is up, then send abts to the remote port to terminate
	 * the IO. Else, just locally terminate the IO in the firmware
	 */
	if (fc_remote_port_chkready(rport) == 0)
		task_req = FCPIO_ITMF_ABT_TASK;
	else {
		atomic64_inc(&fnic_stats->misc_stats.rport_not_ready);
		task_req = FCPIO_ITMF_ABT_TASK_TERM;
	}

	/* Now queue the abort command to firmware */
	int_to_scsilun(sc->device->lun, &fc_lun);

	if (fnic_queue_abort_io_req(fnic, tag, task_req, fc_lun.scsi_lun,
				    io_req)) {
		spin_lock_irqsave(io_lock, flags);
		if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING)
			CMD_STATE(sc) = old_ioreq_state;
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		if (io_req)
			io_req->abts_done = NULL;
		spin_unlock_irqrestore(io_lock, flags);
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}
	if (task_req == FCPIO_ITMF_ABT_TASK) {
		CMD_FLAGS(sc) |= FNIC_IO_ABTS_ISSUED;
		atomic64_inc(&fnic_stats->abts_stats.aborts);
	} else {
		CMD_FLAGS(sc) |= FNIC_IO_TERM_ISSUED;
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
	spin_lock_irqsave(io_lock, flags);

	io_req = (struct fnic_io_req *)CMD_SP(sc);
	if (!io_req) {
		atomic64_inc(&fnic_stats->io_stats.ioreq_null);
		spin_unlock_irqrestore(io_lock, flags);
		CMD_FLAGS(sc) |= FNIC_IO_ABT_TERM_REQ_NULL;
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}
	io_req->abts_done = NULL;

	/* fw did not complete abort, timed out */
	if (CMD_ABTS_STATUS(sc) == FCPIO_INVALID_CODE) {
		spin_unlock_irqrestore(io_lock, flags);
		if (task_req == FCPIO_ITMF_ABT_TASK) {
			atomic64_inc(&abts_stats->abort_drv_timeouts);
		} else {
			atomic64_inc(&term_stats->terminate_drv_timeouts);
		}
		CMD_FLAGS(sc) |= FNIC_IO_ABT_TERM_TIMED_OUT;
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}

	/* IO out of order */

	if (!(CMD_FLAGS(sc) & (FNIC_IO_ABORTED | FNIC_IO_DONE))) {
		spin_unlock_irqrestore(io_lock, flags);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			"Issuing Host reset due to out of order IO\n");

		ret = FAILED;
		goto fnic_abort_cmd_end;
	}

	CMD_STATE(sc) = FNIC_IOREQ_ABTS_COMPLETE;

	start_time = io_req->start_time;
	/*
	 * firmware completed the abort, check the status,
	 * free the io_req if successful. If abort fails,
	 * Device reset will clean the I/O.
	 */
	if (CMD_ABTS_STATUS(sc) == FCPIO_SUCCESS)
		CMD_SP(sc) = NULL;
	else {
		ret = FAILED;
		spin_unlock_irqrestore(io_lock, flags);
		goto fnic_abort_cmd_end;
	}

	spin_unlock_irqrestore(io_lock, flags);

	fnic_release_ioreq_buf(fnic, io_req, sc);
	mempool_free(io_req, fnic->io_req_pool);

	if (sc->scsi_done) {
	/* Call SCSI completion function to complete the IO */
		sc->result = (DID_ABORT << 16);
		sc->scsi_done(sc);
		atomic64_dec(&fnic_stats->io_stats.active_ios);
		if (atomic64_read(&fnic->io_cmpl_skip))
			atomic64_dec(&fnic->io_cmpl_skip);
		else
			atomic64_inc(&fnic_stats->io_stats.io_completions);
	}

fnic_abort_cmd_end:
	FNIC_TRACE(fnic_abort_cmd, sc->device->host->host_no, tag, sc,
		  jiffies_to_msecs(jiffies - start_time),
		  0, ((u64)sc->cmnd[0] << 32 |
		  (u64)sc->cmnd[2] << 24 | (u64)sc->cmnd[3] << 16 |
		  (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
		  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "Returning from abort cmd type %x %s\n", task_req,
		      (ret == SUCCESS) ?
		      "SUCCESS" : "FAILED");
	return ret;
}

static inline int fnic_queue_dr_io_req(struct fnic *fnic,
				       struct scsi_cmnd *sc,
				       struct fnic_io_req *io_req)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	struct Scsi_Host *host = fnic->lport->host;
	struct misc_stats *misc_stats = &fnic->fnic_stats.misc_stats;
	struct scsi_lun fc_lun;
	int ret = 0;
	unsigned long intr_flags;

	spin_lock_irqsave(host->host_lock, intr_flags);
	if (unlikely(fnic_chk_state_flags_locked(fnic,
						FNIC_FLAGS_IO_BLOCKED))) {
		spin_unlock_irqrestore(host->host_lock, intr_flags);
		return FAILED;
	} else
		atomic_inc(&fnic->in_flight);
	spin_unlock_irqrestore(host->host_lock, intr_flags);

	spin_lock_irqsave(&fnic->wq_copy_lock[0], intr_flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);

	if (!vnic_wq_copy_desc_avail(wq)) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			  "queue_dr_io_req failure - no descriptors\n");
		atomic64_inc(&misc_stats->devrst_cpwq_alloc_failures);
		ret = -EAGAIN;
		goto lr_io_req_end;
	}

	/* fill in the lun info */
	int_to_scsilun(sc->device->lun, &fc_lun);

	fnic_queue_wq_copy_desc_itmf(wq, scsi_cmd_to_rq(sc)->tag | FNIC_TAG_DEV_RST,
				     0, FCPIO_ITMF_LUN_RESET, SCSI_NO_TAG,
				     fc_lun.scsi_lun, io_req->port_id,
				     fnic->config.ra_tov, fnic->config.ed_tov);

	atomic64_inc(&fnic->fnic_stats.fw_stats.active_fw_reqs);
	if (atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs) >
		  atomic64_read(&fnic->fnic_stats.fw_stats.max_fw_reqs))
		atomic64_set(&fnic->fnic_stats.fw_stats.max_fw_reqs,
		  atomic64_read(&fnic->fnic_stats.fw_stats.active_fw_reqs));

lr_io_req_end:
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], intr_flags);
	atomic_dec(&fnic->in_flight);

	return ret;
}

struct fnic_pending_aborts_iter_data {
	struct fnic *fnic;
	struct scsi_cmnd *lr_sc;
	struct scsi_device *lun_dev;
	int ret;
};

static bool fnic_pending_aborts_iter(struct scsi_cmnd *sc,
				     void *data, bool reserved)
{
	struct fnic_pending_aborts_iter_data *iter_data = data;
	struct fnic *fnic = iter_data->fnic;
	struct scsi_device *lun_dev = iter_data->lun_dev;
	int abt_tag = scsi_cmd_to_rq(sc)->tag;
	struct fnic_io_req *io_req;
	spinlock_t *io_lock;
	unsigned long flags;
	struct scsi_lun fc_lun;
	DECLARE_COMPLETION_ONSTACK(tm_done);
	enum fnic_ioreq_state old_ioreq_state;

	if (sc == iter_data->lr_sc || sc->device != lun_dev)
		return true;
	if (reserved)
		return true;

	io_lock = fnic_io_lock_tag(fnic, abt_tag);
	spin_lock_irqsave(io_lock, flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);
	if (!io_req) {
		spin_unlock_irqrestore(io_lock, flags);
		return true;
	}

	/*
	 * Found IO that is still pending with firmware and
	 * belongs to the LUN that we are resetting
	 */
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "Found IO in %s on lun\n",
		      fnic_ioreq_state_to_str(CMD_STATE(sc)));

	if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING) {
		spin_unlock_irqrestore(io_lock, flags);
		return true;
	}
	if ((CMD_FLAGS(sc) & FNIC_DEVICE_RESET) &&
	    (!(CMD_FLAGS(sc) & FNIC_DEV_RST_ISSUED))) {
		FNIC_SCSI_DBG(KERN_INFO, fnic->lport->host,
			      "%s dev rst not pending sc 0x%p\n", __func__,
			      sc);
		spin_unlock_irqrestore(io_lock, flags);
		return true;
	}

	if (io_req->abts_done)
		shost_printk(KERN_ERR, fnic->lport->host,
			     "%s: io_req->abts_done is set state is %s\n",
			     __func__, fnic_ioreq_state_to_str(CMD_STATE(sc)));
	old_ioreq_state = CMD_STATE(sc);
	/*
	 * Any pending IO issued prior to reset is expected to be
	 * in abts pending state, if not we need to set
	 * FNIC_IOREQ_ABTS_PENDING to indicate the IO is abort pending.
	 * When IO is completed, the IO will be handed over and
	 * handled in this function.
	 */
	CMD_STATE(sc) = FNIC_IOREQ_ABTS_PENDING;

	BUG_ON(io_req->abts_done);

	if (CMD_FLAGS(sc) & FNIC_DEVICE_RESET) {
		abt_tag |= FNIC_TAG_DEV_RST;
		FNIC_SCSI_DBG(KERN_INFO, fnic->lport->host,
			      "%s: dev rst sc 0x%p\n", __func__, sc);
	}

	CMD_ABTS_STATUS(sc) = FCPIO_INVALID_CODE;
	io_req->abts_done = &tm_done;
	spin_unlock_irqrestore(io_lock, flags);

	/* Now queue the abort command to firmware */
	int_to_scsilun(sc->device->lun, &fc_lun);

	if (fnic_queue_abort_io_req(fnic, abt_tag,
				    FCPIO_ITMF_ABT_TASK_TERM,
				    fc_lun.scsi_lun, io_req)) {
		spin_lock_irqsave(io_lock, flags);
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		if (io_req)
			io_req->abts_done = NULL;
		if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING)
			CMD_STATE(sc) = old_ioreq_state;
		spin_unlock_irqrestore(io_lock, flags);
		iter_data->ret = FAILED;
		return false;
	} else {
		spin_lock_irqsave(io_lock, flags);
		if (CMD_FLAGS(sc) & FNIC_DEVICE_RESET)
			CMD_FLAGS(sc) |= FNIC_DEV_RST_TERM_ISSUED;
		spin_unlock_irqrestore(io_lock, flags);
	}
	CMD_FLAGS(sc) |= FNIC_IO_INTERNAL_TERM_ISSUED;

	wait_for_completion_timeout(&tm_done, msecs_to_jiffies
				    (fnic->config.ed_tov));

	/* Recheck cmd state to check if it is now aborted */
	spin_lock_irqsave(io_lock, flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);
	if (!io_req) {
		spin_unlock_irqrestore(io_lock, flags);
		CMD_FLAGS(sc) |= FNIC_IO_ABT_TERM_REQ_NULL;
		return true;
	}

	io_req->abts_done = NULL;

	/* if abort is still pending with fw, fail */
	if (CMD_ABTS_STATUS(sc) == FCPIO_INVALID_CODE) {
		spin_unlock_irqrestore(io_lock, flags);
		CMD_FLAGS(sc) |= FNIC_IO_ABT_TERM_DONE;
		iter_data->ret = FAILED;
		return false;
	}
	CMD_STATE(sc) = FNIC_IOREQ_ABTS_COMPLETE;

	/* original sc used for lr is handled by dev reset code */
	if (sc != iter_data->lr_sc)
		CMD_SP(sc) = NULL;
	spin_unlock_irqrestore(io_lock, flags);

	/* original sc used for lr is handled by dev reset code */
	if (sc != iter_data->lr_sc) {
		fnic_release_ioreq_buf(fnic, io_req, sc);
		mempool_free(io_req, fnic->io_req_pool);
	}

	/*
	 * Any IO is returned during reset, it needs to call scsi_done
	 * to return the scsi_cmnd to upper layer.
	 */
	if (sc->scsi_done) {
		/* Set result to let upper SCSI layer retry */
		sc->result = DID_RESET << 16;
		sc->scsi_done(sc);
	}
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
	int ret = SUCCESS;
	struct fnic_pending_aborts_iter_data iter_data = {
		.fnic = fnic,
		.lun_dev = lr_sc->device,
		.ret = SUCCESS,
	};

	if (new_sc)
		iter_data.lr_sc = lr_sc;

	scsi_host_busy_iter(fnic->lport->host,
			    fnic_pending_aborts_iter, &iter_data);
	if (iter_data.ret == FAILED) {
		ret = iter_data.ret;
		goto clean_pending_aborts_end;
	}
	schedule_timeout(msecs_to_jiffies(2 * fnic->config.ed_tov));

	/* walk again to check, if IOs are still pending in fw */
	if (fnic_is_abts_pending(fnic, lr_sc))
		ret = FAILED;

clean_pending_aborts_end:
	return ret;
}

/*
 * fnic_scsi_host_start_tag
 * Allocates tagid from host's tag list
 **/
static inline int
fnic_scsi_host_start_tag(struct fnic *fnic, struct scsi_cmnd *sc)
{
	struct request *rq = scsi_cmd_to_rq(sc);
	struct request_queue *q = rq->q;
	struct request *dummy;

	dummy = blk_mq_alloc_request(q, REQ_OP_WRITE, BLK_MQ_REQ_NOWAIT);
	if (IS_ERR(dummy))
		return SCSI_NO_TAG;

	rq->tag = dummy->tag;
	sc->host_scribble = (unsigned char *)dummy;

	return dummy->tag;
}

/*
 * fnic_scsi_host_end_tag
 * frees tag allocated by fnic_scsi_host_start_tag.
 **/
static inline void
fnic_scsi_host_end_tag(struct fnic *fnic, struct scsi_cmnd *sc)
{
	struct request *dummy = (struct request *)sc->host_scribble;

	blk_mq_free_request(dummy);
}

/*
 * SCSI Eh thread issues a Lun Reset when one or more commands on a LUN
 * fail to get aborted. It calls driver's eh_device_reset with a SCSI command
 * on the LUN.
 */
int fnic_device_reset(struct scsi_cmnd *sc)
{
	struct request *rq = scsi_cmd_to_rq(sc);
	struct fc_lport *lp;
	struct fnic *fnic;
	struct fnic_io_req *io_req = NULL;
	struct fc_rport *rport;
	int status;
	int ret = FAILED;
	spinlock_t *io_lock;
	unsigned long flags;
	unsigned long start_time = 0;
	struct scsi_lun fc_lun;
	struct fnic_stats *fnic_stats;
	struct reset_stats *reset_stats;
	int tag = rq->tag;
	DECLARE_COMPLETION_ONSTACK(tm_done);
	int tag_gen_flag = 0;   /*to track tags allocated by fnic driver*/
	bool new_sc = 0;

	/* Wait for rport to unblock */
	fc_block_scsi_eh(sc);

	/* Get local-port, check ready and link up */
	lp = shost_priv(sc->device->host);

	fnic = lport_priv(lp);
	fnic_stats = &fnic->fnic_stats;
	reset_stats = &fnic->fnic_stats.reset_stats;

	atomic64_inc(&reset_stats->device_resets);

	rport = starget_to_rport(scsi_target(sc->device));
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "Device reset called FCID 0x%x, LUN 0x%llx sc 0x%p\n",
		      rport->port_id, sc->device->lun, sc);

	if (lp->state != LPORT_ST_READY || !(lp->link_up))
		goto fnic_device_reset_end;

	/* Check if remote port up */
	if (fc_remote_port_chkready(rport)) {
		atomic64_inc(&fnic_stats->misc_stats.rport_not_ready);
		goto fnic_device_reset_end;
	}

	CMD_FLAGS(sc) = FNIC_DEVICE_RESET;
	/* Allocate tag if not present */

	if (unlikely(tag < 0)) {
		/*
		 * Really should fix the midlayer to pass in a proper
		 * request for ioctls...
		 */
		tag = fnic_scsi_host_start_tag(fnic, sc);
		if (unlikely(tag == SCSI_NO_TAG))
			goto fnic_device_reset_end;
		tag_gen_flag = 1;
		new_sc = 1;
	}
	io_lock = fnic_io_lock_hash(fnic, sc);
	spin_lock_irqsave(io_lock, flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);

	/*
	 * If there is a io_req attached to this command, then use it,
	 * else allocate a new one.
	 */
	if (!io_req) {
		io_req = mempool_alloc(fnic->io_req_pool, GFP_ATOMIC);
		if (!io_req) {
			spin_unlock_irqrestore(io_lock, flags);
			goto fnic_device_reset_end;
		}
		memset(io_req, 0, sizeof(*io_req));
		io_req->port_id = rport->port_id;
		CMD_SP(sc) = (char *)io_req;
	}
	io_req->dr_done = &tm_done;
	CMD_STATE(sc) = FNIC_IOREQ_CMD_PENDING;
	CMD_LR_STATUS(sc) = FCPIO_INVALID_CODE;
	spin_unlock_irqrestore(io_lock, flags);

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host, "TAG %x\n", tag);

	/*
	 * issue the device reset, if enqueue failed, clean up the ioreq
	 * and break assoc with scsi cmd
	 */
	if (fnic_queue_dr_io_req(fnic, sc, io_req)) {
		spin_lock_irqsave(io_lock, flags);
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		if (io_req)
			io_req->dr_done = NULL;
		goto fnic_device_reset_clean;
	}
	spin_lock_irqsave(io_lock, flags);
	CMD_FLAGS(sc) |= FNIC_DEV_RST_ISSUED;
	spin_unlock_irqrestore(io_lock, flags);

	/*
	 * Wait on the local completion for LUN reset.  The io_req may be
	 * freed while we wait since we hold no lock.
	 */
	wait_for_completion_timeout(&tm_done,
				    msecs_to_jiffies(FNIC_LUN_RESET_TIMEOUT));

	spin_lock_irqsave(io_lock, flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);
	if (!io_req) {
		spin_unlock_irqrestore(io_lock, flags);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
				"io_req is null tag 0x%x sc 0x%p\n", tag, sc);
		goto fnic_device_reset_end;
	}
	io_req->dr_done = NULL;

	status = CMD_LR_STATUS(sc);

	/*
	 * If lun reset not completed, bail out with failed. io_req
	 * gets cleaned up during higher levels of EH
	 */
	if (status == FCPIO_INVALID_CODE) {
		atomic64_inc(&reset_stats->device_reset_timeouts);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "Device reset timed out\n");
		CMD_FLAGS(sc) |= FNIC_DEV_RST_TIMED_OUT;
		spin_unlock_irqrestore(io_lock, flags);
		int_to_scsilun(sc->device->lun, &fc_lun);
		/*
		 * Issue abort and terminate on device reset request.
		 * If q'ing of terminate fails, retry it after a delay.
		 */
		while (1) {
			spin_lock_irqsave(io_lock, flags);
			if (CMD_FLAGS(sc) & FNIC_DEV_RST_TERM_ISSUED) {
				spin_unlock_irqrestore(io_lock, flags);
				break;
			}
			spin_unlock_irqrestore(io_lock, flags);
			if (fnic_queue_abort_io_req(fnic,
				tag | FNIC_TAG_DEV_RST,
				FCPIO_ITMF_ABT_TASK_TERM,
				fc_lun.scsi_lun, io_req)) {
				wait_for_completion_timeout(&tm_done,
				msecs_to_jiffies(FNIC_ABT_TERM_DELAY_TIMEOUT));
			} else {
				spin_lock_irqsave(io_lock, flags);
				CMD_FLAGS(sc) |= FNIC_DEV_RST_TERM_ISSUED;
				CMD_STATE(sc) = FNIC_IOREQ_ABTS_PENDING;
				io_req->abts_done = &tm_done;
				spin_unlock_irqrestore(io_lock, flags);
				FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
				"Abort and terminate issued on Device reset "
				"tag 0x%x sc 0x%p\n", tag, sc);
				break;
			}
		}
		while (1) {
			spin_lock_irqsave(io_lock, flags);
			if (!(CMD_FLAGS(sc) & FNIC_DEV_RST_DONE)) {
				spin_unlock_irqrestore(io_lock, flags);
				wait_for_completion_timeout(&tm_done,
				msecs_to_jiffies(FNIC_LUN_RESET_TIMEOUT));
				break;
			} else {
				io_req = (struct fnic_io_req *)CMD_SP(sc);
				io_req->abts_done = NULL;
				goto fnic_device_reset_clean;
			}
		}
	} else {
		spin_unlock_irqrestore(io_lock, flags);
	}

	/* Completed, but not successful, clean up the io_req, return fail */
	if (status != FCPIO_SUCCESS) {
		spin_lock_irqsave(io_lock, flags);
		FNIC_SCSI_DBG(KERN_DEBUG,
			      fnic->lport->host,
			      "Device reset completed - failed\n");
		io_req = (struct fnic_io_req *)CMD_SP(sc);
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
		spin_lock_irqsave(io_lock, flags);
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "Device reset failed"
			      " since could not abort all IOs\n");
		goto fnic_device_reset_clean;
	}

	/* Clean lun reset command */
	spin_lock_irqsave(io_lock, flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);
	if (io_req)
		/* Completed, and successful */
		ret = SUCCESS;

fnic_device_reset_clean:
	if (io_req)
		CMD_SP(sc) = NULL;

	spin_unlock_irqrestore(io_lock, flags);

	if (io_req) {
		start_time = io_req->start_time;
		fnic_release_ioreq_buf(fnic, io_req, sc);
		mempool_free(io_req, fnic->io_req_pool);
	}

fnic_device_reset_end:
	FNIC_TRACE(fnic_device_reset, sc->device->host->host_no, rq->tag, sc,
		  jiffies_to_msecs(jiffies - start_time),
		  0, ((u64)sc->cmnd[0] << 32 |
		  (u64)sc->cmnd[2] << 24 | (u64)sc->cmnd[3] << 16 |
		  (u64)sc->cmnd[4] << 8 | sc->cmnd[5]),
		  (((u64)CMD_FLAGS(sc) << 32) | CMD_STATE(sc)));

	/* free tag if it is allocated */
	if (unlikely(tag_gen_flag))
		fnic_scsi_host_end_tag(fnic, sc);

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "Returning from device reset %s\n",
		      (ret == SUCCESS) ?
		      "SUCCESS" : "FAILED");

	if (ret == FAILED)
		atomic64_inc(&reset_stats->device_reset_failures);

	return ret;
}

/* Clean up all IOs, clean up libFC local port */
int fnic_reset(struct Scsi_Host *shost)
{
	struct fc_lport *lp;
	struct fnic *fnic;
	int ret = 0;
	struct reset_stats *reset_stats;

	lp = shost_priv(shost);
	fnic = lport_priv(lp);
	reset_stats = &fnic->fnic_stats.reset_stats;

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "fnic_reset called\n");

	atomic64_inc(&reset_stats->fnic_resets);

	/*
	 * Reset local port, this will clean up libFC exchanges,
	 * reset remote port sessions, and if link is up, begin flogi
	 */
	ret = fc_lport_reset(lp);

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "Returning from fnic reset %s\n",
		      (ret == 0) ?
		      "SUCCESS" : "FAILED");

	if (ret == 0)
		atomic64_inc(&reset_stats->fnic_reset_completions);
	else
		atomic64_inc(&reset_stats->fnic_reset_failures);

	return ret;
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
int fnic_host_reset(struct scsi_cmnd *sc)
{
	int ret;
	unsigned long wait_host_tmo;
	struct Scsi_Host *shost = sc->device->host;
	struct fc_lport *lp = shost_priv(shost);
	struct fnic *fnic = lport_priv(lp);
	unsigned long flags;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (!fnic->internal_reset_inprogress) {
		fnic->internal_reset_inprogress = true;
	} else {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			"host reset in progress skipping another host reset\n");
		return SUCCESS;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	/*
	 * If fnic_reset is successful, wait for fabric login to complete
	 * scsi-ml tries to send a TUR to every device if host reset is
	 * successful, so before returning to scsi, fabric should be up
	 */
	ret = (fnic_reset(shost) == 0) ? SUCCESS : FAILED;
	if (ret == SUCCESS) {
		wait_host_tmo = jiffies + FNIC_HOST_RESET_SETTLE_TIME * HZ;
		ret = FAILED;
		while (time_before(jiffies, wait_host_tmo)) {
			if ((lp->state == LPORT_ST_READY) &&
			    (lp->link_up)) {
				ret = SUCCESS;
				break;
			}
			ssleep(1);
		}
	}

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->internal_reset_inprogress = false;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	return ret;
}

/*
 * This fxn is called from libFC when host is removed
 */
void fnic_scsi_abort_io(struct fc_lport *lp)
{
	int err = 0;
	unsigned long flags;
	enum fnic_state old_state;
	struct fnic *fnic = lport_priv(lp);
	DECLARE_COMPLETION_ONSTACK(remove_wait);

	/* Issue firmware reset for fnic, wait for reset to complete */
retry_fw_reset:
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (unlikely(fnic->state == FNIC_IN_FC_TRANS_ETH_MODE) &&
		     fnic->link_events) {
		/* fw reset is in progress, poll for its completion */
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		schedule_timeout(msecs_to_jiffies(100));
		goto retry_fw_reset;
	}

	fnic->remove_wait = &remove_wait;
	old_state = fnic->state;
	fnic->state = FNIC_IN_FC_TRANS_ETH_MODE;
	fnic_update_mac_locked(fnic, fnic->ctlr.ctl_src_addr);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	err = fnic_fw_reset_handler(fnic);
	if (err) {
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		if (fnic->state == FNIC_IN_FC_TRANS_ETH_MODE)
			fnic->state = old_state;
		fnic->remove_wait = NULL;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	/* Wait for firmware reset to complete */
	wait_for_completion_timeout(&remove_wait,
				    msecs_to_jiffies(FNIC_RMDEVICE_TIMEOUT));

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->remove_wait = NULL;
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "fnic_scsi_abort_io %s\n",
		      (fnic->state == FNIC_IN_ETH_MODE) ?
		      "SUCCESS" : "FAILED");
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

}

/*
 * This fxn called from libFC to clean up driver IO state on link down
 */
void fnic_scsi_cleanup(struct fc_lport *lp)
{
	unsigned long flags;
	enum fnic_state old_state;
	struct fnic *fnic = lport_priv(lp);

	/* issue fw reset */
retry_fw_reset:
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (unlikely(fnic->state == FNIC_IN_FC_TRANS_ETH_MODE)) {
		/* fw reset is in progress, poll for its completion */
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		schedule_timeout(msecs_to_jiffies(100));
		goto retry_fw_reset;
	}
	old_state = fnic->state;
	fnic->state = FNIC_IN_FC_TRANS_ETH_MODE;
	fnic_update_mac_locked(fnic, fnic->ctlr.ctl_src_addr);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (fnic_fw_reset_handler(fnic)) {
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		if (fnic->state == FNIC_IN_FC_TRANS_ETH_MODE)
			fnic->state = old_state;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	}

}

void fnic_empty_scsi_cleanup(struct fc_lport *lp)
{
}

void fnic_exch_mgr_reset(struct fc_lport *lp, u32 sid, u32 did)
{
	struct fnic *fnic = lport_priv(lp);

	/* Non-zero sid, nothing to do */
	if (sid)
		goto call_fc_exch_mgr_reset;

	if (did) {
		fnic_rport_exch_reset(fnic, did);
		goto call_fc_exch_mgr_reset;
	}

	/*
	 * sid = 0, did = 0
	 * link down or device being removed
	 */
	if (!fnic->in_remove)
		fnic_scsi_cleanup(lp);
	else
		fnic_scsi_abort_io(lp);

	/* call libFC exch mgr reset to reset its exchanges */
call_fc_exch_mgr_reset:
	fc_exch_mgr_reset(lp, sid, did);

}

static bool fnic_abts_pending_iter(struct scsi_cmnd *sc, void *data,
				   bool reserved)
{
	struct fnic_pending_aborts_iter_data *iter_data = data;
	struct fnic *fnic = iter_data->fnic;
	int cmd_state;
	struct fnic_io_req *io_req;
	spinlock_t *io_lock;
	unsigned long flags;

	/*
	 * ignore this lun reset cmd or cmds that do not belong to
	 * this lun
	 */
	if (iter_data->lr_sc && sc == iter_data->lr_sc)
		return true;
	if (iter_data->lun_dev && sc->device != iter_data->lun_dev)
		return true;

	io_lock = fnic_io_lock_hash(fnic, sc);
	spin_lock_irqsave(io_lock, flags);

	io_req = (struct fnic_io_req *)CMD_SP(sc);
	if (!io_req) {
		spin_unlock_irqrestore(io_lock, flags);
		return true;
	}

	/*
	 * Found IO that is still pending with firmware and
	 * belongs to the LUN that we are resetting
	 */
	FNIC_SCSI_DBG(KERN_INFO, fnic->lport->host,
		      "Found IO in %s on lun\n",
		      fnic_ioreq_state_to_str(CMD_STATE(sc)));
	cmd_state = CMD_STATE(sc);
	spin_unlock_irqrestore(io_lock, flags);
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
	scsi_host_busy_iter(fnic->lport->host,
			    fnic_abts_pending_iter, &iter_data);

	return iter_data.ret;
}
