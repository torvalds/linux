/*
 * Copyright 2014 Cisco Systems, Inc.  All rights reserved.
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

#include <linux/errno.h>
#include <linux/mempool.h>

#include <scsi/scsi_tcq.h>

#include "snic_disc.h"
#include "snic.h"
#include "snic_io.h"


/* snic target types */
static const char * const snic_tgt_type_str[] = {
	[SNIC_TGT_DAS] = "DAS",
	[SNIC_TGT_SAN] = "SAN",
};

static inline const char *
snic_tgt_type_to_str(int typ)
{
	return ((typ > SNIC_TGT_NONE && typ <= SNIC_TGT_SAN) ?
		 snic_tgt_type_str[typ] : "Unknown");
}

static const char * const snic_tgt_state_str[] = {
	[SNIC_TGT_STAT_INIT]	= "INIT",
	[SNIC_TGT_STAT_ONLINE]	= "ONLINE",
	[SNIC_TGT_STAT_OFFLINE]	= "OFFLINE",
	[SNIC_TGT_STAT_DEL]	= "DELETION IN PROGRESS",
};

const char *
snic_tgt_state_to_str(int state)
{
	return ((state >= SNIC_TGT_STAT_INIT && state <= SNIC_TGT_STAT_DEL) ?
		snic_tgt_state_str[state] : "UNKNOWN");
}

/*
 * Initiate report_tgt req desc
 */
static void
snic_report_tgt_init(struct snic_host_req *req, u32 hid, u8 *buf, u32 len,
		     dma_addr_t rsp_buf_pa, ulong ctx)
{
	struct snic_sg_desc *sgd = NULL;


	snic_io_hdr_enc(&req->hdr, SNIC_REQ_REPORT_TGTS, 0, SCSI_NO_TAG, hid,
			1, ctx);

	req->u.rpt_tgts.sg_cnt = cpu_to_le16(1);
	sgd = req_to_sgl(req);
	sgd[0].addr = cpu_to_le64(rsp_buf_pa);
	sgd[0].len = cpu_to_le32(len);
	sgd[0]._resvd = 0;
	req->u.rpt_tgts.sg_addr = cpu_to_le64((ulong)sgd);
}

/*
 * snic_queue_report_tgt_req: Queues report target request.
 */
static int
snic_queue_report_tgt_req(struct snic *snic)
{
	struct snic_req_info *rqi = NULL;
	u32 ntgts, buf_len = 0;
	u8 *buf = NULL;
	dma_addr_t pa = 0;
	int ret = 0;

	rqi = snic_req_init(snic, 1);
	if (!rqi) {
		ret = -ENOMEM;
		goto error;
	}

	if (snic->fwinfo.max_tgts)
		ntgts = min_t(u32, snic->fwinfo.max_tgts, snic->shost->max_id);
	else
		ntgts = snic->shost->max_id;

	/* Allocate Response Buffer */
	SNIC_BUG_ON(ntgts == 0);
	buf_len = ntgts * sizeof(struct snic_tgt_id) + SNIC_SG_DESC_ALIGN;

	buf = kzalloc(buf_len, GFP_KERNEL|GFP_DMA);
	if (!buf) {
		snic_req_free(snic, rqi);
		SNIC_HOST_ERR(snic->shost, "Resp Buf Alloc Failed.\n");

		ret = -ENOMEM;
		goto error;
	}

	SNIC_BUG_ON((((unsigned long)buf) % SNIC_SG_DESC_ALIGN) != 0);

	pa = pci_map_single(snic->pdev, buf, buf_len, PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(snic->pdev, pa)) {
		kfree(buf);
		snic_req_free(snic, rqi);
		SNIC_HOST_ERR(snic->shost,
			      "Rpt-tgt rspbuf %p: PCI DMA Mapping Failed\n",
			      buf);
		ret = -EINVAL;

		goto error;
	}


	SNIC_BUG_ON(pa == 0);
	rqi->sge_va = (ulong) buf;

	snic_report_tgt_init(rqi->req,
			     snic->config.hid,
			     buf,
			     buf_len,
			     pa,
			     (ulong)rqi);

	snic_handle_untagged_req(snic, rqi);

	ret = snic_queue_wq_desc(snic, rqi->req, rqi->req_len);
	if (ret) {
		pci_unmap_single(snic->pdev, pa, buf_len, PCI_DMA_FROMDEVICE);
		kfree(buf);
		rqi->sge_va = 0;
		snic_release_untagged_req(snic, rqi);
		SNIC_HOST_ERR(snic->shost, "Queuing Report Tgts Failed.\n");

		goto error;
	}

	SNIC_DISC_DBG(snic->shost, "Report Targets Issued.\n");

	return ret;

error:
	SNIC_HOST_ERR(snic->shost,
		      "Queuing Report Targets Failed, err = %d\n",
		      ret);
	return ret;
} /* end of snic_queue_report_tgt_req */

/* call into SML */
static void
snic_scsi_scan_tgt(struct work_struct *work)
{
	struct snic_tgt *tgt = container_of(work, struct snic_tgt, scan_work);
	struct Scsi_Host *shost = dev_to_shost(&tgt->dev);
	unsigned long flags;

	SNIC_HOST_INFO(shost, "Scanning Target id 0x%x\n", tgt->id);
	scsi_scan_target(&tgt->dev,
			 tgt->channel,
			 tgt->scsi_tgt_id,
			 SCAN_WILD_CARD,
			 SCSI_SCAN_RESCAN);

	spin_lock_irqsave(shost->host_lock, flags);
	tgt->flags &= ~SNIC_TGT_SCAN_PENDING;
	spin_unlock_irqrestore(shost->host_lock, flags);
} /* end of snic_scsi_scan_tgt */

/*
 * snic_tgt_lookup :
 */
static struct snic_tgt *
snic_tgt_lookup(struct snic *snic, struct snic_tgt_id *tgtid)
{
	struct list_head *cur, *nxt;
	struct snic_tgt *tgt = NULL;

	list_for_each_safe(cur, nxt, &snic->disc.tgt_list) {
		tgt = list_entry(cur, struct snic_tgt, list);
		if (tgt->id == le32_to_cpu(tgtid->tgt_id))
			return tgt;
		tgt = NULL;
	}

	return tgt;
} /* end of snic_tgt_lookup */

/*
 * snic_tgt_dev_release : Called on dropping last ref for snic_tgt object
 */
void
snic_tgt_dev_release(struct device *dev)
{
	struct snic_tgt *tgt = dev_to_tgt(dev);

	SNIC_HOST_INFO(snic_tgt_to_shost(tgt),
		       "Target Device ID %d (%s) Permanently Deleted.\n",
		       tgt->id,
		       dev_name(dev));

	SNIC_BUG_ON(!list_empty(&tgt->list));
	kfree(tgt);
}

/*
 * snic_tgt_del : work function to delete snic_tgt
 */
static void
snic_tgt_del(struct work_struct *work)
{
	struct snic_tgt *tgt = container_of(work, struct snic_tgt, del_work);
	struct Scsi_Host *shost = snic_tgt_to_shost(tgt);

	if (tgt->flags & SNIC_TGT_SCAN_PENDING)
		scsi_flush_work(shost);

	/* Block IOs on child devices, stops new IOs */
	scsi_target_block(&tgt->dev);

	/* Cleanup IOs */
	snic_tgt_scsi_abort_io(tgt);

	/* Unblock IOs now, to flush if there are any. */
	scsi_target_unblock(&tgt->dev, SDEV_TRANSPORT_OFFLINE);

	/* Delete SCSI Target and sdevs */
	scsi_remove_target(&tgt->dev);  /* ?? */
	device_del(&tgt->dev);
	put_device(&tgt->dev);
} /* end of snic_tgt_del */

/* snic_tgt_create: checks for existence of snic_tgt, if it doesn't
 * it creates one.
 */
static struct snic_tgt *
snic_tgt_create(struct snic *snic, struct snic_tgt_id *tgtid)
{
	struct snic_tgt *tgt = NULL;
	unsigned long flags;
	int ret;

	tgt = snic_tgt_lookup(snic, tgtid);
	if (tgt) {
		/* update the information if required */
		return tgt;
	}

	tgt = kzalloc(sizeof(*tgt), GFP_KERNEL);
	if (!tgt) {
		SNIC_HOST_ERR(snic->shost, "Failure to allocate snic_tgt.\n");
		ret = -ENOMEM;

		return tgt;
	}

	INIT_LIST_HEAD(&tgt->list);
	tgt->id = le32_to_cpu(tgtid->tgt_id);
	tgt->channel = 0;

	SNIC_BUG_ON(le16_to_cpu(tgtid->tgt_type) > SNIC_TGT_SAN);
	tgt->tdata.typ = le16_to_cpu(tgtid->tgt_type);

	/*
	 * Plugging into SML Device Tree
	 */
	tgt->tdata.disc_id = 0;
	tgt->state = SNIC_TGT_STAT_INIT;
	device_initialize(&tgt->dev);
	tgt->dev.parent = get_device(&snic->shost->shost_gendev);
	tgt->dev.release = snic_tgt_dev_release;
	INIT_WORK(&tgt->scan_work, snic_scsi_scan_tgt);
	INIT_WORK(&tgt->del_work, snic_tgt_del);
	switch (tgt->tdata.typ) {
	case SNIC_TGT_DAS:
		dev_set_name(&tgt->dev, "snic_das_tgt:%d:%d-%d",
			     snic->shost->host_no, tgt->channel, tgt->id);
		break;

	case SNIC_TGT_SAN:
		dev_set_name(&tgt->dev, "snic_san_tgt:%d:%d-%d",
			     snic->shost->host_no, tgt->channel, tgt->id);
		break;

	default:
		SNIC_HOST_INFO(snic->shost, "Target type Unknown Detected.\n");
		dev_set_name(&tgt->dev, "snic_das_tgt:%d:%d-%d",
			     snic->shost->host_no, tgt->channel, tgt->id);
		break;
	}

	spin_lock_irqsave(snic->shost->host_lock, flags);
	list_add_tail(&tgt->list, &snic->disc.tgt_list);
	tgt->scsi_tgt_id = snic->disc.nxt_tgt_id++;
	tgt->state = SNIC_TGT_STAT_ONLINE;
	spin_unlock_irqrestore(snic->shost->host_lock, flags);

	SNIC_HOST_INFO(snic->shost,
		       "Tgt %d, type = %s detected. Adding..\n",
		       tgt->id, snic_tgt_type_to_str(tgt->tdata.typ));

	ret = device_add(&tgt->dev);
	if (ret) {
		SNIC_HOST_ERR(snic->shost,
			      "Snic Tgt: device_add, with err = %d\n",
			      ret);

		put_device(&snic->shost->shost_gendev);
		kfree(tgt);
		tgt = NULL;

		return tgt;
	}

	SNIC_HOST_INFO(snic->shost, "Scanning %s.\n", dev_name(&tgt->dev));

	scsi_queue_work(snic->shost, &tgt->scan_work);

	return tgt;
} /* end of snic_tgt_create */

/* Handler for discovery */
void
snic_handle_tgt_disc(struct work_struct *work)
{
	struct snic *snic = container_of(work, struct snic, tgt_work);
	struct snic_tgt_id *tgtid = NULL;
	struct snic_tgt *tgt = NULL;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&snic->snic_lock, flags);
	if (snic->in_remove) {
		spin_unlock_irqrestore(&snic->snic_lock, flags);
		kfree(snic->disc.rtgt_info);

		return;
	}
	spin_unlock_irqrestore(&snic->snic_lock, flags);

	mutex_lock(&snic->disc.mutex);
	/* Discover triggered during disc in progress */
	if (snic->disc.req_cnt) {
		snic->disc.state = SNIC_DISC_DONE;
		snic->disc.req_cnt = 0;
		mutex_unlock(&snic->disc.mutex);
		kfree(snic->disc.rtgt_info);
		snic->disc.rtgt_info = NULL;

		SNIC_HOST_INFO(snic->shost, "tgt_disc: Discovery restart.\n");
		/* Start Discovery Again */
		snic_disc_start(snic);

		return;
	}

	tgtid = (struct snic_tgt_id *)snic->disc.rtgt_info;

	SNIC_BUG_ON(snic->disc.rtgt_cnt == 0 || tgtid == NULL);

	for (i = 0; i < snic->disc.rtgt_cnt; i++) {
		tgt = snic_tgt_create(snic, &tgtid[i]);
		if (!tgt) {
			int buf_sz = snic->disc.rtgt_cnt * sizeof(*tgtid);

			SNIC_HOST_ERR(snic->shost, "Failed to create tgt.\n");
			snic_hex_dump("rpt_tgt_rsp", (char *)tgtid, buf_sz);
			break;
		}
	}

	snic->disc.rtgt_info = NULL;
	snic->disc.state = SNIC_DISC_DONE;
	mutex_unlock(&snic->disc.mutex);

	SNIC_HOST_INFO(snic->shost, "Discovery Completed.\n");

	kfree(tgtid);
} /* end of snic_handle_tgt_disc */


int
snic_report_tgt_cmpl_handler(struct snic *snic, struct snic_fw_req *fwreq)
{

	u8 typ, cmpl_stat;
	u32 cmnd_id, hid, tgt_cnt = 0;
	ulong ctx;
	struct snic_req_info *rqi = NULL;
	struct snic_tgt_id *tgtid;
	int i, ret = 0;

	snic_io_hdr_dec(&fwreq->hdr, &typ, &cmpl_stat, &cmnd_id, &hid, &ctx);
	rqi = (struct snic_req_info *) ctx;
	tgtid = (struct snic_tgt_id *) rqi->sge_va;

	tgt_cnt = le32_to_cpu(fwreq->u.rpt_tgts_cmpl.tgt_cnt);
	if (tgt_cnt == 0) {
		SNIC_HOST_ERR(snic->shost, "No Targets Found on this host.\n");
		ret = 1;

		goto end;
	}

	/* printing list of targets here */
	SNIC_HOST_INFO(snic->shost, "Target Count = %d\n", tgt_cnt);

	SNIC_BUG_ON(tgt_cnt > snic->fwinfo.max_tgts);

	for (i = 0; i < tgt_cnt; i++)
		SNIC_HOST_INFO(snic->shost,
			       "Tgt id = 0x%x\n",
			       le32_to_cpu(tgtid[i].tgt_id));

	/*
	 * Queue work for further processing,
	 * Response Buffer Memory is freed after creating targets
	 */
	snic->disc.rtgt_cnt = tgt_cnt;
	snic->disc.rtgt_info = (u8 *) tgtid;
	queue_work(snic_glob->event_q, &snic->tgt_work);
	ret = 0;

end:
	/* Unmap Response Buffer */
	snic_pci_unmap_rsp_buf(snic, rqi);
	if (ret)
		kfree(tgtid);

	rqi->sge_va = 0;
	snic_release_untagged_req(snic, rqi);

	return ret;
} /* end of snic_report_tgt_cmpl_handler */

/* Discovery init fn */
void
snic_disc_init(struct snic_disc *disc)
{
	INIT_LIST_HEAD(&disc->tgt_list);
	mutex_init(&disc->mutex);
	disc->disc_id = 0;
	disc->nxt_tgt_id = 0;
	disc->state = SNIC_DISC_INIT;
	disc->req_cnt = 0;
	disc->rtgt_cnt = 0;
	disc->rtgt_info = NULL;
	disc->cb = NULL;
} /* end of snic_disc_init */

/* Discovery, uninit fn */
void
snic_disc_term(struct snic *snic)
{
	struct snic_disc *disc = &snic->disc;

	mutex_lock(&disc->mutex);
	if (disc->req_cnt) {
		disc->req_cnt = 0;
		SNIC_SCSI_DBG(snic->shost, "Terminating Discovery.\n");
	}
	mutex_unlock(&disc->mutex);
}

/*
 * snic_disc_start: Discovery Start ...
 */
int
snic_disc_start(struct snic *snic)
{
	struct snic_disc *disc = &snic->disc;
	unsigned long flags;
	int ret = 0;

	SNIC_SCSI_DBG(snic->shost, "Discovery Start.\n");

	spin_lock_irqsave(&snic->snic_lock, flags);
	if (snic->in_remove) {
		spin_unlock_irqrestore(&snic->snic_lock, flags);
		SNIC_ERR("snic driver removal in progress ...\n");
		ret = 0;

		return ret;
	}
	spin_unlock_irqrestore(&snic->snic_lock, flags);

	mutex_lock(&disc->mutex);
	if (disc->state == SNIC_DISC_PENDING) {
		disc->req_cnt++;
		mutex_unlock(&disc->mutex);

		return ret;
	}
	disc->state = SNIC_DISC_PENDING;
	mutex_unlock(&disc->mutex);

	ret = snic_queue_report_tgt_req(snic);
	if (ret)
		SNIC_HOST_INFO(snic->shost, "Discovery Failed, err=%d.\n", ret);

	return ret;
} /* end of snic_disc_start */

/*
 * snic_disc_work :
 */
void
snic_handle_disc(struct work_struct *work)
{
	struct snic *snic = container_of(work, struct snic, disc_work);
	int ret = 0;

	SNIC_HOST_INFO(snic->shost, "disc_work: Discovery\n");

	ret = snic_disc_start(snic);
	if (ret)
		goto disc_err;

disc_err:
	SNIC_HOST_ERR(snic->shost,
		      "disc_work: Discovery Failed w/ err = %d\n",
		      ret);
} /* end of snic_disc_work */

/*
 * snic_tgt_del_all : cleanup all snic targets
 * Called on unbinding the interface
 */
void
snic_tgt_del_all(struct snic *snic)
{
	struct snic_tgt *tgt = NULL;
	struct list_head *cur, *nxt;
	unsigned long flags;

	scsi_flush_work(snic->shost);

	mutex_lock(&snic->disc.mutex);
	spin_lock_irqsave(snic->shost->host_lock, flags);

	list_for_each_safe(cur, nxt, &snic->disc.tgt_list) {
		tgt = list_entry(cur, struct snic_tgt, list);
		tgt->state = SNIC_TGT_STAT_DEL;
		list_del_init(&tgt->list);
		SNIC_HOST_INFO(snic->shost, "Tgt %d q'ing for del\n", tgt->id);
		queue_work(snic_glob->event_q, &tgt->del_work);
		tgt = NULL;
	}
	spin_unlock_irqrestore(snic->shost->host_lock, flags);
	mutex_unlock(&snic->disc.mutex);

	flush_workqueue(snic_glob->event_q);
} /* end of snic_tgt_del_all */
