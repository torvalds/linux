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
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/delay.h>
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

static void fnic_cleanup_io(struct fnic *fnic, int exclude_id);

static inline spinlock_t *fnic_io_lock_hash(struct fnic *fnic,
					    struct scsi_cmnd *sc)
{
	u32 hash = sc->request->tag & (FNIC_IO_LOCKS - 1);

	return &fnic->io_req_lock[hash];
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
		pci_unmap_single(fnic->pdev, io_req->sgl_list_pa,
				 sizeof(io_req->sgl_list[0]) * io_req->sgl_cnt,
				 PCI_DMA_TODEVICE);
	scsi_dma_unmap(sc);

	if (io_req->sgl_cnt)
		mempool_free(io_req->sgl_list_alloc,
			     fnic->io_sgl_pool[io_req->sgl_type]);
	if (io_req->sense_buf_pa)
		pci_unmap_single(fnic->pdev, io_req->sense_buf_pa,
				 SCSI_SENSE_BUFFERSIZE, PCI_DMA_FROMDEVICE);
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
 * fnic_fw_reset_handler
 * Routine to send reset msg to fw
 */
int fnic_fw_reset_handler(struct fnic *fnic)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);

	if (!vnic_wq_copy_desc_avail(wq))
		ret = -EAGAIN;
	else
		fnic_queue_wq_copy_desc_fw_reset(wq, SCSI_NO_TAG);

	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);

	if (!ret)
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "Issued fw reset\n");
	else
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "Failed to issue fw reset\n");
	return ret;
}


/*
 * fnic_flogi_reg_handler
 * Routine to send flogi register msg to fw
 */
int fnic_flogi_reg_handler(struct fnic *fnic)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
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

	if (fnic->fcoui_mode)
		memset(gw_mac, 0xff, ETH_ALEN);
	else
		memcpy(gw_mac, fnic->dest_addr, ETH_ALEN);

	fnic_queue_wq_copy_desc_flogi_reg(wq, SCSI_NO_TAG,
					  FCPIO_FLOGI_REG_GW_DEST,
					  fnic->s_id,
					  gw_mac);

flogi_reg_ioreq_end:
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);

	if (!ret)
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "flog reg issued\n");

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
	u8 pri_tag = 0;
	unsigned int i;
	unsigned long intr_flags;
	int flags;
	u8 exch_flags;
	struct scsi_lun fc_lun;
	char msg[2];

	if (sg_count) {
		/* For each SGE, create a device desc entry */
		desc = io_req->sgl_list;
		for_each_sg(scsi_sglist(sc), sg, sg_count, i) {
			desc->addr = cpu_to_le64(sg_dma_address(sg));
			desc->len = cpu_to_le32(sg_dma_len(sg));
			desc->_resvd = 0;
			desc++;
		}

		io_req->sgl_list_pa = pci_map_single
			(fnic->pdev,
			 io_req->sgl_list,
			 sizeof(io_req->sgl_list[0]) * sg_count,
			 PCI_DMA_TODEVICE);
	}

	io_req->sense_buf_pa = pci_map_single(fnic->pdev,
					      sc->sense_buffer,
					      SCSI_SENSE_BUFFERSIZE,
					      PCI_DMA_FROMDEVICE);

	int_to_scsilun(sc->device->lun, &fc_lun);

	pri_tag = FCPIO_ICMND_PTA_SIMPLE;
	msg[0] = MSG_SIMPLE_TAG;
	scsi_populate_tag_msg(sc, msg);
	if (msg[0] == MSG_ORDERED_TAG)
		pri_tag = FCPIO_ICMND_PTA_ORDERED;

	/* Enqueue the descriptor in the Copy WQ */
	spin_lock_irqsave(&fnic->wq_copy_lock[0], intr_flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);

	if (unlikely(!vnic_wq_copy_desc_avail(wq))) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[0], intr_flags);
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

	fnic_queue_wq_copy_desc_icmnd_16(wq, sc->request->tag,
					 0, exch_flags, io_req->sgl_cnt,
					 SCSI_SENSE_BUFFERSIZE,
					 io_req->sgl_list_pa,
					 io_req->sense_buf_pa,
					 0, /* scsi cmd ref, always 0 */
					 pri_tag, /* scsi pri and tag */
					 flags,	/* command flags */
					 sc->cmnd, scsi_bufflen(sc),
					 fc_lun.scsi_lun, io_req->port_id,
					 rport->maxframe_size, rp->r_a_tov,
					 rp->e_d_tov);

	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], intr_flags);
	return 0;
}

/*
 * fnic_queuecommand
 * Routine to send a scsi cdb
 * Called with host_lock held and interrupts disabled.
 */
int fnic_queuecommand(struct scsi_cmnd *sc, void (*done)(struct scsi_cmnd *))
{
	struct fc_lport *lp;
	struct fc_rport *rport;
	struct fnic_io_req *io_req;
	struct fnic *fnic;
	struct vnic_wq_copy *wq;
	int ret;
	int sg_count;
	unsigned long flags;
	unsigned long ptr;

	rport = starget_to_rport(scsi_target(sc->device));
	ret = fc_remote_port_chkready(rport);
	if (ret) {
		sc->result = ret;
		done(sc);
		return 0;
	}

	lp = shost_priv(sc->device->host);
	if (lp->state != LPORT_ST_READY || !(lp->link_up))
		return SCSI_MLQUEUE_HOST_BUSY;

	/*
	 * Release host lock, use driver resource specific locks from here.
	 * Don't re-enable interrupts in case they were disabled prior to the
	 * caller disabling them.
	 */
	spin_unlock(lp->host->host_lock);

	/* Get a new io_req for this SCSI IO */
	fnic = lport_priv(lp);

	io_req = mempool_alloc(fnic->io_req_pool, GFP_ATOMIC);
	if (!io_req) {
		ret = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}
	memset(io_req, 0, sizeof(*io_req));

	/* Map the data buffer */
	sg_count = scsi_dma_map(sc);
	if (sg_count < 0) {
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
				      GFP_ATOMIC | GFP_DMA);
		if (!io_req->sgl_list) {
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

	/* initialize rest of io_req */
	io_req->port_id = rport->port_id;
	CMD_STATE(sc) = FNIC_IOREQ_CMD_PENDING;
	CMD_SP(sc) = (char *)io_req;
	sc->scsi_done = done;

	/* create copy wq desc and enqueue it */
	wq = &fnic->wq_copy[0];
	ret = fnic_queue_wq_copy_desc(fnic, wq, io_req, sc, sg_count);
	if (ret) {
		/*
		 * In case another thread cancelled the request,
		 * refetch the pointer under the lock.
		 */
		spinlock_t *io_lock = fnic_io_lock_hash(fnic, sc);

		spin_lock_irqsave(io_lock, flags);
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		CMD_SP(sc) = NULL;
		CMD_STATE(sc) = FNIC_IOREQ_CMD_COMPLETE;
		spin_unlock_irqrestore(io_lock, flags);
		if (io_req) {
			fnic_release_ioreq_buf(fnic, io_req, sc);
			mempool_free(io_req, fnic->io_req_pool);
		}
	}
out:
	/* acquire host lock before returning to SCSI */
	spin_lock(lp->host->host_lock);
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
	struct fc_frame *flogi;
	unsigned long flags;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);

	/* Clean up all outstanding io requests */
	fnic_cleanup_io(fnic, SCSI_NO_TAG);

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	flogi = fnic->flogi;
	fnic->flogi = NULL;

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
			ret = -1;
		}
	} else {
		FNIC_SCSI_DBG(KERN_DEBUG,
			      fnic->lport->host,
			      "Unexpected state %s while processing"
			      " reset cmpl\n", fnic_state_to_str(fnic->state));
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
		fnic->flogi_oxid = FC_XID_UNKNOWN;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		if (flogi)
			dev_kfree_skb_irq(fp_skb(flogi));
		goto reset_cmpl_handler_end;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (flogi)
		ret = fnic_send_frame(fnic, flogi);

 reset_cmpl_handler_end:
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
	struct fc_frame *flogi_resp = NULL;
	unsigned long flags;
	struct sk_buff *skb;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);

	/* Update fnic state based on status of flogi reg completion */
	spin_lock_irqsave(&fnic->fnic_lock, flags);

	flogi_resp = fnic->flogi_resp;
	fnic->flogi_resp = NULL;

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

	/* Successful flogi reg cmpl, pass frame to LibFC */
	if (!ret && flogi_resp) {
		if (fnic->stop_rx_link_events) {
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			goto reg_cmpl_handler_end;
		}
		skb = (struct sk_buff *)flogi_resp;
		/* Use fr_flags to indicate whether flogi resp or not */
		fr_flags(flogi_resp) = 1;
		fr_dev(flogi_resp) = fnic->lport;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);

		skb_queue_tail(&fnic->frame_queue, skb);
		queue_work(fnic_event_queue, &fnic->frame_work);

	} else {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		if (flogi_resp)
			dev_kfree_skb_irq(fp_skb(flogi_resp));
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

	/* mark the ack state */
	wq = &fnic->wq_copy[cq_index - fnic->raw_wq_count - fnic->rq_count];
	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	if (is_ack_index_in_range(wq, request_out)) {
		fnic->fw_ack_index[0] = request_out;
		fnic->fw_ack_recd[0] = 1;
	}
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);
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
	unsigned long flags;
	spinlock_t *io_lock;

	/* Decode the cmpl description to get the io_req id */
	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);
	fcpio_tag_id_dec(&tag, &id);

	if (id >= FNIC_MAX_IO_REQ)
		return;

	sc = scsi_host_find_tag(fnic->lport->host, id);
	WARN_ON_ONCE(!sc);
	if (!sc)
		return;

	io_lock = fnic_io_lock_hash(fnic, sc);
	spin_lock_irqsave(io_lock, flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);
	WARN_ON_ONCE(!io_req);
	if (!io_req) {
		spin_unlock_irqrestore(io_lock, flags);
		return;
	}

	/* firmware completed the io */
	io_req->io_completed = 1;

	/*
	 *  if SCSI-ML has already issued abort on this command,
	 * ignore completion of the IO. The abts path will clean it up
	 */
	if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING) {
		spin_unlock_irqrestore(io_lock, flags);
		return;
	}

	/* Mark the IO as complete */
	CMD_STATE(sc) = FNIC_IOREQ_CMD_COMPLETE;

	icmnd_cmpl = &desc->u.icmnd_cmpl;

	switch (hdr_status) {
	case FCPIO_SUCCESS:
		sc->result = (DID_OK << 16) | icmnd_cmpl->scsi_status;
		xfer_len = scsi_bufflen(sc);
		scsi_set_resid(sc, icmnd_cmpl->residual);

		if (icmnd_cmpl->flags & FCPIO_ICMND_CMPL_RESID_UNDER)
			xfer_len -= icmnd_cmpl->residual;

		/*
		 * If queue_full, then try to reduce queue depth for all
		 * LUNS on the target. Todo: this should be accompanied
		 * by a periodic queue_depth rampup based on successful
		 * IO completion.
		 */
		if (icmnd_cmpl->scsi_status == QUEUE_FULL) {
			struct scsi_device *t_sdev;
			int qd = 0;

			shost_for_each_device(t_sdev, sc->device->host) {
				if (t_sdev->id != sc->device->id)
					continue;

				if (t_sdev->queue_depth > 1) {
					qd = scsi_track_queue_full
						(t_sdev,
						 t_sdev->queue_depth - 1);
					if (qd == -1)
						qd = t_sdev->host->cmd_per_lun;
					shost_printk(KERN_INFO,
						     fnic->lport->host,
						     "scsi[%d:%d:%d:%d"
						     "] queue full detected,"
						     "new depth = %d\n",
						     t_sdev->host->host_no,
						     t_sdev->channel,
						     t_sdev->id, t_sdev->lun,
						     t_sdev->queue_depth);
				}
			}
		}
		break;

	case FCPIO_TIMEOUT:          /* request was timed out */
		sc->result = (DID_TIME_OUT << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_ABORTED:          /* request was aborted */
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_DATA_CNT_MISMATCH: /* recv/sent more/less data than exp. */
		scsi_set_resid(sc, icmnd_cmpl->residual);
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;

	case FCPIO_OUT_OF_RESOURCE:  /* out of resources to complete request */
		sc->result = (DID_REQUEUE << 16) | icmnd_cmpl->scsi_status;
		break;
	case FCPIO_INVALID_HEADER:   /* header contains invalid data */
	case FCPIO_INVALID_PARAM:    /* some parameter in request invalid */
	case FCPIO_REQ_NOT_SUPPORTED:/* request type is not supported */
	case FCPIO_IO_NOT_FOUND:     /* requested I/O was not found */
	case FCPIO_SGL_INVALID:      /* request was aborted due to sgl error */
	case FCPIO_MSS_INVALID:      /* request was aborted due to mss error */
	case FCPIO_FW_ERR:           /* request was terminated due fw error */
	default:
		shost_printk(KERN_ERR, fnic->lport->host, "hdr status = %s\n",
			     fnic_fcpio_status_to_str(hdr_status));
		sc->result = (DID_ERROR << 16) | icmnd_cmpl->scsi_status;
		break;
	}

	/* Break link with the SCSI command */
	CMD_SP(sc) = NULL;

	spin_unlock_irqrestore(io_lock, flags);

	fnic_release_ioreq_buf(fnic, io_req, sc);

	mempool_free(io_req, fnic->io_req_pool);

	if (sc->sc_data_direction == DMA_FROM_DEVICE) {
		fnic->lport->host_stats.fcp_input_requests++;
		fnic->fcp_input_bytes += xfer_len;
	} else if (sc->sc_data_direction == DMA_TO_DEVICE) {
		fnic->lport->host_stats.fcp_output_requests++;
		fnic->fcp_output_bytes += xfer_len;
	} else
		fnic->lport->host_stats.fcp_control_requests++;

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
	unsigned long flags;
	spinlock_t *io_lock;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);
	fcpio_tag_id_dec(&tag, &id);

	if ((id & FNIC_TAG_MASK) >= FNIC_MAX_IO_REQ)
		return;

	sc = scsi_host_find_tag(fnic->lport->host, id & FNIC_TAG_MASK);
	WARN_ON_ONCE(!sc);
	if (!sc)
		return;

	io_lock = fnic_io_lock_hash(fnic, sc);
	spin_lock_irqsave(io_lock, flags);
	io_req = (struct fnic_io_req *)CMD_SP(sc);
	WARN_ON_ONCE(!io_req);
	if (!io_req) {
		spin_unlock_irqrestore(io_lock, flags);
		return;
	}

	if (id & FNIC_TAG_ABORT) {
		/* Completion of abort cmd */
		if (CMD_STATE(sc) != FNIC_IOREQ_ABTS_PENDING) {
			/* This is a late completion. Ignore it */
			spin_unlock_irqrestore(io_lock, flags);
			return;
		}
		CMD_STATE(sc) = FNIC_IOREQ_ABTS_COMPLETE;
		CMD_ABTS_STATUS(sc) = hdr_status;

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
			if (sc->scsi_done)
				sc->scsi_done(sc);
		}

	} else if (id & FNIC_TAG_DEV_RST) {
		/* Completion of device reset */
		CMD_LR_STATUS(sc) = hdr_status;
		CMD_STATE(sc) = FNIC_IOREQ_CMD_COMPLETE;
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
	int ret = 0;

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
		ret = fnic_fcpio_flogi_reg_cmpl_handler(fnic, desc);
		break;

	case FCPIO_RESET_CMPL: /* fw completed reset */
		ret = fnic_fcpio_fw_reset_cmpl_handler(fnic, desc);
		break;

	default:
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "firmware completion type %d\n",
			      desc->hdr.type);
		break;
	}

	return ret;
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

	for (i = 0; i < fnic->wq_copy_count; i++) {
		cq_index = i + fnic->raw_wq_count + fnic->rq_count;
		cur_work_done = vnic_cq_copy_service(&fnic->cq[cq_index],
						     fnic_fcpio_cmpl_handler,
						     copy_work_to_do);
		wq_work_done += cur_work_done;
	}
	return wq_work_done;
}

static void fnic_cleanup_io(struct fnic *fnic, int exclude_id)
{
	unsigned int i;
	struct fnic_io_req *io_req;
	unsigned long flags = 0;
	struct scsi_cmnd *sc;
	spinlock_t *io_lock;

	for (i = 0; i < FNIC_MAX_IO_REQ; i++) {
		if (i == exclude_id)
			continue;

		sc = scsi_host_find_tag(fnic->lport->host, i);
		if (!sc)
			continue;

		io_lock = fnic_io_lock_hash(fnic, sc);
		spin_lock_irqsave(io_lock, flags);
		io_req = (struct fnic_io_req *)CMD_SP(sc);
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
		fnic_release_ioreq_buf(fnic, io_req, sc);
		mempool_free(io_req, fnic->io_req_pool);

cleanup_scsi_cmd:
		sc->result = DID_TRANSPORT_DISRUPTED << 16;
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host, "fnic_cleanup_io:"
			      " DID_TRANSPORT_DISRUPTED\n");

		/* Complete the command to SCSI */
		if (sc->scsi_done)
			sc->scsi_done(sc);
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
	spinlock_t *io_lock;

	/* get the tag reference */
	fcpio_tag_id_dec(&desc->hdr.tag, &id);
	id &= FNIC_TAG_MASK;

	if (id >= FNIC_MAX_IO_REQ)
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

	fnic_release_ioreq_buf(fnic, io_req, sc);
	mempool_free(io_req, fnic->io_req_pool);

wq_copy_cleanup_scsi_cmd:
	sc->result = DID_NO_CONNECT << 16;
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host, "wq_copy_cleanup_handler:"
		      " DID_NO_CONNECT\n");

	if (sc->scsi_done)
		sc->scsi_done(sc);
}

static inline int fnic_queue_abort_io_req(struct fnic *fnic, int tag,
					  u32 task_req, u8 *fc_lun,
					  struct fnic_io_req *io_req)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	unsigned long flags;

	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);

	if (!vnic_wq_copy_desc_avail(wq)) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);
		return 1;
	}
	fnic_queue_wq_copy_desc_itmf(wq, tag | FNIC_TAG_ABORT,
				     0, task_req, tag, fc_lun, io_req->port_id,
				     fnic->config.ra_tov, fnic->config.ed_tov);

	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);
	return 0;
}

void fnic_rport_exch_reset(struct fnic *fnic, u32 port_id)
{
	int tag;
	struct fnic_io_req *io_req;
	spinlock_t *io_lock;
	unsigned long flags;
	struct scsi_cmnd *sc;
	struct scsi_lun fc_lun;
	enum fnic_ioreq_state old_ioreq_state;

	FNIC_SCSI_DBG(KERN_DEBUG,
		      fnic->lport->host,
		      "fnic_rport_reset_exch called portid 0x%06x\n",
		      port_id);

	if (fnic->in_remove)
		return;

	for (tag = 0; tag < FNIC_MAX_IO_REQ; tag++) {
		sc = scsi_host_find_tag(fnic->lport->host, tag);
		if (!sc)
			continue;

		io_lock = fnic_io_lock_hash(fnic, sc);
		spin_lock_irqsave(io_lock, flags);

		io_req = (struct fnic_io_req *)CMD_SP(sc);

		if (!io_req || io_req->port_id != port_id) {
			spin_unlock_irqrestore(io_lock, flags);
			continue;
		}

		/*
		 * Found IO that is still pending with firmware and
		 * belongs to rport that went away
		 */
		if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING) {
			spin_unlock_irqrestore(io_lock, flags);
			continue;
		}
		old_ioreq_state = CMD_STATE(sc);
		CMD_STATE(sc) = FNIC_IOREQ_ABTS_PENDING;
		CMD_ABTS_STATUS(sc) = FCPIO_INVALID_CODE;

		BUG_ON(io_req->abts_done);

		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "fnic_rport_reset_exch: Issuing abts\n");

		spin_unlock_irqrestore(io_lock, flags);

		/* Now queue the abort command to firmware */
		int_to_scsilun(sc->device->lun, &fc_lun);

		if (fnic_queue_abort_io_req(fnic, tag,
					    FCPIO_ITMF_ABT_TASK_TERM,
					    fc_lun.scsi_lun, io_req)) {
			/*
			 * Revert the cmd state back to old state, if
			 * it hasnt changed in between. This cmd will get
			 * aborted later by scsi_eh, or cleaned up during
			 * lun reset
			 */
			io_lock = fnic_io_lock_hash(fnic, sc);

			spin_lock_irqsave(io_lock, flags);
			if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING)
				CMD_STATE(sc) = old_ioreq_state;
			spin_unlock_irqrestore(io_lock, flags);
		}
	}

}

void fnic_terminate_rport_io(struct fc_rport *rport)
{
	int tag;
	struct fnic_io_req *io_req;
	spinlock_t *io_lock;
	unsigned long flags;
	struct scsi_cmnd *sc;
	struct scsi_lun fc_lun;
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	struct fc_lport *lport = rdata->local_port;
	struct fnic *fnic = lport_priv(lport);
	struct fc_rport *cmd_rport;
	enum fnic_ioreq_state old_ioreq_state;

	FNIC_SCSI_DBG(KERN_DEBUG,
		      fnic->lport->host, "fnic_terminate_rport_io called"
		      " wwpn 0x%llx, wwnn0x%llx, portid 0x%06x\n",
		      rport->port_name, rport->node_name,
		      rport->port_id);

	if (fnic->in_remove)
		return;

	for (tag = 0; tag < FNIC_MAX_IO_REQ; tag++) {
		sc = scsi_host_find_tag(fnic->lport->host, tag);
		if (!sc)
			continue;

		cmd_rport = starget_to_rport(scsi_target(sc->device));
		if (rport != cmd_rport)
			continue;

		io_lock = fnic_io_lock_hash(fnic, sc);
		spin_lock_irqsave(io_lock, flags);

		io_req = (struct fnic_io_req *)CMD_SP(sc);

		if (!io_req || rport != cmd_rport) {
			spin_unlock_irqrestore(io_lock, flags);
			continue;
		}

		/*
		 * Found IO that is still pending with firmware and
		 * belongs to rport that went away
		 */
		if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING) {
			spin_unlock_irqrestore(io_lock, flags);
			continue;
		}
		old_ioreq_state = CMD_STATE(sc);
		CMD_STATE(sc) = FNIC_IOREQ_ABTS_PENDING;
		CMD_ABTS_STATUS(sc) = FCPIO_INVALID_CODE;

		BUG_ON(io_req->abts_done);

		FNIC_SCSI_DBG(KERN_DEBUG,
			      fnic->lport->host,
			      "fnic_terminate_rport_io: Issuing abts\n");

		spin_unlock_irqrestore(io_lock, flags);

		/* Now queue the abort command to firmware */
		int_to_scsilun(sc->device->lun, &fc_lun);

		if (fnic_queue_abort_io_req(fnic, tag,
					    FCPIO_ITMF_ABT_TASK_TERM,
					    fc_lun.scsi_lun, io_req)) {
			/*
			 * Revert the cmd state back to old state, if
			 * it hasnt changed in between. This cmd will get
			 * aborted later by scsi_eh, or cleaned up during
			 * lun reset
			 */
			io_lock = fnic_io_lock_hash(fnic, sc);

			spin_lock_irqsave(io_lock, flags);
			if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING)
				CMD_STATE(sc) = old_ioreq_state;
			spin_unlock_irqrestore(io_lock, flags);
		}
	}

}

static void fnic_block_error_handler(struct scsi_cmnd *sc)
{
	struct Scsi_Host *shost = sc->device->host;
	struct fc_rport *rport = starget_to_rport(scsi_target(sc->device));
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	while (rport->port_state == FC_PORTSTATE_BLOCKED) {
		spin_unlock_irqrestore(shost->host_lock, flags);
		msleep(1000);
		spin_lock_irqsave(shost->host_lock, flags);
	}
	spin_unlock_irqrestore(shost->host_lock, flags);

}

/*
 * This function is exported to SCSI for sending abort cmnds.
 * A SCSI IO is represented by a io_req in the driver.
 * The ioreq is linked to the SCSI Cmd, thus a link with the ULP's IO.
 */
int fnic_abort_cmd(struct scsi_cmnd *sc)
{
	struct fc_lport *lp;
	struct fnic *fnic;
	struct fnic_io_req *io_req;
	struct fc_rport *rport;
	spinlock_t *io_lock;
	unsigned long flags;
	int ret = SUCCESS;
	u32 task_req;
	struct scsi_lun fc_lun;
	DECLARE_COMPLETION_ONSTACK(tm_done);

	/* Wait for rport to unblock */
	fnic_block_error_handler(sc);

	/* Get local-port, check ready and link up */
	lp = shost_priv(sc->device->host);

	fnic = lport_priv(lp);
	FNIC_SCSI_DBG(KERN_DEBUG,
		      fnic->lport->host,
		      "Abort Cmd called FCID 0x%x, LUN 0x%x TAG %d\n",
		      (starget_to_rport(scsi_target(sc->device)))->port_id,
		      sc->device->lun, sc->request->tag);

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
	/*
	 * Command is still pending, need to abort it
	 * If the firmware completes the command after this point,
	 * the completion wont be done till mid-layer, since abort
	 * has already started.
	 */
	CMD_STATE(sc) = FNIC_IOREQ_ABTS_PENDING;
	CMD_ABTS_STATUS(sc) = FCPIO_INVALID_CODE;

	spin_unlock_irqrestore(io_lock, flags);

	/*
	 * Check readiness of the remote port. If the path to remote
	 * port is up, then send abts to the remote port to terminate
	 * the IO. Else, just locally terminate the IO in the firmware
	 */
	rport = starget_to_rport(scsi_target(sc->device));
	if (fc_remote_port_chkready(rport) == 0)
		task_req = FCPIO_ITMF_ABT_TASK;
	else
		task_req = FCPIO_ITMF_ABT_TASK_TERM;

	/* Now queue the abort command to firmware */
	int_to_scsilun(sc->device->lun, &fc_lun);

	if (fnic_queue_abort_io_req(fnic, sc->request->tag, task_req,
				    fc_lun.scsi_lun, io_req)) {
		spin_lock_irqsave(io_lock, flags);
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		if (io_req)
			io_req->abts_done = NULL;
		spin_unlock_irqrestore(io_lock, flags);
		ret = FAILED;
		goto fnic_abort_cmd_end;
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
		spin_unlock_irqrestore(io_lock, flags);
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}
	io_req->abts_done = NULL;

	/* fw did not complete abort, timed out */
	if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING) {
		spin_unlock_irqrestore(io_lock, flags);
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}

	/*
	 * firmware completed the abort, check the status,
	 * free the io_req irrespective of failure or success
	 */
	if (CMD_ABTS_STATUS(sc) != FCPIO_SUCCESS)
		ret = FAILED;

	CMD_SP(sc) = NULL;

	spin_unlock_irqrestore(io_lock, flags);

	fnic_release_ioreq_buf(fnic, io_req, sc);
	mempool_free(io_req, fnic->io_req_pool);

fnic_abort_cmd_end:
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "Returning from abort cmd %s\n",
		      (ret == SUCCESS) ?
		      "SUCCESS" : "FAILED");
	return ret;
}

static inline int fnic_queue_dr_io_req(struct fnic *fnic,
				       struct scsi_cmnd *sc,
				       struct fnic_io_req *io_req)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	struct scsi_lun fc_lun;
	int ret = 0;
	unsigned long intr_flags;

	spin_lock_irqsave(&fnic->wq_copy_lock[0], intr_flags);

	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);

	if (!vnic_wq_copy_desc_avail(wq)) {
		ret = -EAGAIN;
		goto lr_io_req_end;
	}

	/* fill in the lun info */
	int_to_scsilun(sc->device->lun, &fc_lun);

	fnic_queue_wq_copy_desc_itmf(wq, sc->request->tag | FNIC_TAG_DEV_RST,
				     0, FCPIO_ITMF_LUN_RESET, SCSI_NO_TAG,
				     fc_lun.scsi_lun, io_req->port_id,
				     fnic->config.ra_tov, fnic->config.ed_tov);

lr_io_req_end:
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], intr_flags);

	return ret;
}

/*
 * Clean up any pending aborts on the lun
 * For each outstanding IO on this lun, whose abort is not completed by fw,
 * issue a local abort. Wait for abort to complete. Return 0 if all commands
 * successfully aborted, 1 otherwise
 */
static int fnic_clean_pending_aborts(struct fnic *fnic,
				     struct scsi_cmnd *lr_sc)
{
	int tag;
	struct fnic_io_req *io_req;
	spinlock_t *io_lock;
	unsigned long flags;
	int ret = 0;
	struct scsi_cmnd *sc;
	struct fc_rport *rport;
	struct scsi_lun fc_lun;
	struct scsi_device *lun_dev = lr_sc->device;
	DECLARE_COMPLETION_ONSTACK(tm_done);

	for (tag = 0; tag < FNIC_MAX_IO_REQ; tag++) {
		sc = scsi_host_find_tag(fnic->lport->host, tag);
		/*
		 * ignore this lun reset cmd or cmds that do not belong to
		 * this lun
		 */
		if (!sc || sc == lr_sc || sc->device != lun_dev)
			continue;

		io_lock = fnic_io_lock_hash(fnic, sc);
		spin_lock_irqsave(io_lock, flags);

		io_req = (struct fnic_io_req *)CMD_SP(sc);

		if (!io_req || sc->device != lun_dev) {
			spin_unlock_irqrestore(io_lock, flags);
			continue;
		}

		/*
		 * Found IO that is still pending with firmware and
		 * belongs to the LUN that we are resetting
		 */
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "Found IO in %s on lun\n",
			      fnic_ioreq_state_to_str(CMD_STATE(sc)));

		BUG_ON(CMD_STATE(sc) != FNIC_IOREQ_ABTS_PENDING);

		CMD_ABTS_STATUS(sc) = FCPIO_INVALID_CODE;
		io_req->abts_done = &tm_done;
		spin_unlock_irqrestore(io_lock, flags);

		/* Now queue the abort command to firmware */
		int_to_scsilun(sc->device->lun, &fc_lun);
		rport = starget_to_rport(scsi_target(sc->device));

		if (fnic_queue_abort_io_req(fnic, tag,
					    FCPIO_ITMF_ABT_TASK_TERM,
					    fc_lun.scsi_lun, io_req)) {
			spin_lock_irqsave(io_lock, flags);
			io_req = (struct fnic_io_req *)CMD_SP(sc);
			if (io_req)
				io_req->abts_done = NULL;
			spin_unlock_irqrestore(io_lock, flags);
			ret = 1;
			goto clean_pending_aborts_end;
		}

		wait_for_completion_timeout(&tm_done,
					    msecs_to_jiffies
					    (fnic->config.ed_tov));

		/* Recheck cmd state to check if it is now aborted */
		spin_lock_irqsave(io_lock, flags);
		io_req = (struct fnic_io_req *)CMD_SP(sc);
		if (!io_req) {
			spin_unlock_irqrestore(io_lock, flags);
			ret = 1;
			goto clean_pending_aborts_end;
		}

		io_req->abts_done = NULL;

		/* if abort is still pending with fw, fail */
		if (CMD_STATE(sc) == FNIC_IOREQ_ABTS_PENDING) {
			spin_unlock_irqrestore(io_lock, flags);
			ret = 1;
			goto clean_pending_aborts_end;
		}
		CMD_SP(sc) = NULL;
		spin_unlock_irqrestore(io_lock, flags);

		fnic_release_ioreq_buf(fnic, io_req, sc);
		mempool_free(io_req, fnic->io_req_pool);
	}

clean_pending_aborts_end:
	return ret;
}

/*
 * SCSI Eh thread issues a Lun Reset when one or more commands on a LUN
 * fail to get aborted. It calls driver's eh_device_reset with a SCSI command
 * on the LUN.
 */
int fnic_device_reset(struct scsi_cmnd *sc)
{
	struct fc_lport *lp;
	struct fnic *fnic;
	struct fnic_io_req *io_req;
	struct fc_rport *rport;
	int status;
	int ret = FAILED;
	spinlock_t *io_lock;
	unsigned long flags;
	DECLARE_COMPLETION_ONSTACK(tm_done);

	/* Wait for rport to unblock */
	fnic_block_error_handler(sc);

	/* Get local-port, check ready and link up */
	lp = shost_priv(sc->device->host);

	fnic = lport_priv(lp);
	FNIC_SCSI_DBG(KERN_DEBUG,
		      fnic->lport->host,
		      "Device reset called FCID 0x%x, LUN 0x%x\n",
		      (starget_to_rport(scsi_target(sc->device)))->port_id,
		      sc->device->lun);


	if (lp->state != LPORT_ST_READY || !(lp->link_up))
		goto fnic_device_reset_end;

	/* Check if remote port up */
	rport = starget_to_rport(scsi_target(sc->device));
	if (fc_remote_port_chkready(rport))
		goto fnic_device_reset_end;

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

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host, "TAG %d\n",
		      sc->request->tag);

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
		goto fnic_device_reset_end;
	}
	io_req->dr_done = NULL;

	status = CMD_LR_STATUS(sc);
	spin_unlock_irqrestore(io_lock, flags);

	/*
	 * If lun reset not completed, bail out with failed. io_req
	 * gets cleaned up during higher levels of EH
	 */
	if (status == FCPIO_INVALID_CODE) {
		FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
			      "Device reset timed out\n");
		goto fnic_device_reset_end;
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
	if (fnic_clean_pending_aborts(fnic, sc)) {
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
		fnic_release_ioreq_buf(fnic, io_req, sc);
		mempool_free(io_req, fnic->io_req_pool);
	}

fnic_device_reset_end:
	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "Returning from device reset %s\n",
		      (ret == SUCCESS) ?
		      "SUCCESS" : "FAILED");
	return ret;
}

/* Clean up all IOs, clean up libFC local port */
int fnic_reset(struct Scsi_Host *shost)
{
	struct fc_lport *lp;
	struct fnic *fnic;
	int ret = SUCCESS;

	lp = shost_priv(shost);
	fnic = lport_priv(lp);

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "fnic_reset called\n");

	/*
	 * Reset local port, this will clean up libFC exchanges,
	 * reset remote port sessions, and if link is up, begin flogi
	 */
	if (lp->tt.lport_reset(lp))
		ret = FAILED;

	FNIC_SCSI_DBG(KERN_DEBUG, fnic->lport->host,
		      "Returning from fnic reset %s\n",
		      (ret == SUCCESS) ?
		      "SUCCESS" : "FAILED");

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

	/*
	 * If fnic_reset is successful, wait for fabric login to complete
	 * scsi-ml tries to send a TUR to every device if host reset is
	 * successful, so before returning to scsi, fabric should be up
	 */
	ret = fnic_reset(shost);
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
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->remove_wait = &remove_wait;
	old_state = fnic->state;
	fnic->state = FNIC_IN_FC_TRANS_ETH_MODE;
	vnic_dev_del_addr(fnic->vdev, fnic->data_src_addr);
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
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	old_state = fnic->state;
	fnic->state = FNIC_IN_FC_TRANS_ETH_MODE;
	vnic_dev_del_addr(fnic->vdev, fnic->data_src_addr);
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
