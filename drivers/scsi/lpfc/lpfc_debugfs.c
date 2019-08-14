/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2019 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.  *
 * Copyright (C) 2007-2015 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/fc/fc_fs.h>

#include <linux/nvme-fc-driver.h>

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_scsi.h"
#include "lpfc_nvme.h"
#include "lpfc_nvmet.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"
#include "lpfc_version.h"
#include "lpfc_compat.h"
#include "lpfc_debugfs.h"
#include "lpfc_bsg.h"

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
/*
 * debugfs interface
 *
 * To access this interface the user should:
 * # mount -t debugfs none /sys/kernel/debug
 *
 * The lpfc debugfs directory hierarchy is:
 * /sys/kernel/debug/lpfc/fnX/vportY
 * where X is the lpfc hba function unique_id
 * where Y is the vport VPI on that hba
 *
 * Debugging services available per vport:
 * discovery_trace
 * This is an ACSII readable file that contains a trace of the last
 * lpfc_debugfs_max_disc_trc events that happened on a specific vport.
 * See lpfc_debugfs.h for different categories of  discovery events.
 * To enable the discovery trace, the following module parameters must be set:
 * lpfc_debugfs_enable=1         Turns on lpfc debugfs filesystem support
 * lpfc_debugfs_max_disc_trc=X   Where X is the event trace depth for
 *                               EACH vport. X MUST also be a power of 2.
 * lpfc_debugfs_mask_disc_trc=Y  Where Y is an event mask as defined in
 *                               lpfc_debugfs.h .
 *
 * slow_ring_trace
 * This is an ACSII readable file that contains a trace of the last
 * lpfc_debugfs_max_slow_ring_trc events that happened on a specific HBA.
 * To enable the slow ring trace, the following module parameters must be set:
 * lpfc_debugfs_enable=1         Turns on lpfc debugfs filesystem support
 * lpfc_debugfs_max_slow_ring_trc=X   Where X is the event trace depth for
 *                               the HBA. X MUST also be a power of 2.
 */
static int lpfc_debugfs_enable = 1;
module_param(lpfc_debugfs_enable, int, S_IRUGO);
MODULE_PARM_DESC(lpfc_debugfs_enable, "Enable debugfs services");

/* This MUST be a power of 2 */
static int lpfc_debugfs_max_disc_trc;
module_param(lpfc_debugfs_max_disc_trc, int, S_IRUGO);
MODULE_PARM_DESC(lpfc_debugfs_max_disc_trc,
	"Set debugfs discovery trace depth");

/* This MUST be a power of 2 */
static int lpfc_debugfs_max_slow_ring_trc;
module_param(lpfc_debugfs_max_slow_ring_trc, int, S_IRUGO);
MODULE_PARM_DESC(lpfc_debugfs_max_slow_ring_trc,
	"Set debugfs slow ring trace depth");

/* This MUST be a power of 2 */
static int lpfc_debugfs_max_nvmeio_trc;
module_param(lpfc_debugfs_max_nvmeio_trc, int, 0444);
MODULE_PARM_DESC(lpfc_debugfs_max_nvmeio_trc,
		 "Set debugfs NVME IO trace depth");

static int lpfc_debugfs_mask_disc_trc;
module_param(lpfc_debugfs_mask_disc_trc, int, S_IRUGO);
MODULE_PARM_DESC(lpfc_debugfs_mask_disc_trc,
	"Set debugfs discovery trace mask");

#include <linux/debugfs.h>

static atomic_t lpfc_debugfs_seq_trc_cnt = ATOMIC_INIT(0);
static unsigned long lpfc_debugfs_start_time = 0L;

/* iDiag */
static struct lpfc_idiag idiag;

/**
 * lpfc_debugfs_disc_trc_data - Dump discovery logging to a buffer
 * @vport: The vport to gather the log info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine gathers the lpfc discovery debugfs data from the @vport and
 * dumps it to @buf up to @size number of bytes. It will start at the next entry
 * in the log and process the log until the end of the buffer. Then it will
 * gather from the beginning of the log and process until the current entry.
 *
 * Notes:
 * Discovery logging will be disabled while while this routine dumps the log.
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_disc_trc_data(struct lpfc_vport *vport, char *buf, int size)
{
	int i, index, len, enable;
	uint32_t ms;
	struct lpfc_debugfs_trc *dtp;
	char *buffer;

	buffer = kmalloc(LPFC_DEBUG_TRC_ENTRY_SIZE, GFP_KERNEL);
	if (!buffer)
		return 0;

	enable = lpfc_debugfs_enable;
	lpfc_debugfs_enable = 0;

	len = 0;
	index = (atomic_read(&vport->disc_trc_cnt) + 1) &
		(lpfc_debugfs_max_disc_trc - 1);
	for (i = index; i < lpfc_debugfs_max_disc_trc; i++) {
		dtp = vport->disc_trc + i;
		if (!dtp->fmt)
			continue;
		ms = jiffies_to_msecs(dtp->jif - lpfc_debugfs_start_time);
		snprintf(buffer,
			LPFC_DEBUG_TRC_ENTRY_SIZE, "%010d:%010d ms:%s\n",
			dtp->seq_cnt, ms, dtp->fmt);
		len +=  scnprintf(buf+len, size-len, buffer,
			dtp->data1, dtp->data2, dtp->data3);
	}
	for (i = 0; i < index; i++) {
		dtp = vport->disc_trc + i;
		if (!dtp->fmt)
			continue;
		ms = jiffies_to_msecs(dtp->jif - lpfc_debugfs_start_time);
		snprintf(buffer,
			LPFC_DEBUG_TRC_ENTRY_SIZE, "%010d:%010d ms:%s\n",
			dtp->seq_cnt, ms, dtp->fmt);
		len +=  scnprintf(buf+len, size-len, buffer,
			dtp->data1, dtp->data2, dtp->data3);
	}

	lpfc_debugfs_enable = enable;
	kfree(buffer);

	return len;
}

/**
 * lpfc_debugfs_slow_ring_trc_data - Dump slow ring logging to a buffer
 * @phba: The HBA to gather the log info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine gathers the lpfc slow ring debugfs data from the @phba and
 * dumps it to @buf up to @size number of bytes. It will start at the next entry
 * in the log and process the log until the end of the buffer. Then it will
 * gather from the beginning of the log and process until the current entry.
 *
 * Notes:
 * Slow ring logging will be disabled while while this routine dumps the log.
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_slow_ring_trc_data(struct lpfc_hba *phba, char *buf, int size)
{
	int i, index, len, enable;
	uint32_t ms;
	struct lpfc_debugfs_trc *dtp;
	char *buffer;

	buffer = kmalloc(LPFC_DEBUG_TRC_ENTRY_SIZE, GFP_KERNEL);
	if (!buffer)
		return 0;

	enable = lpfc_debugfs_enable;
	lpfc_debugfs_enable = 0;

	len = 0;
	index = (atomic_read(&phba->slow_ring_trc_cnt) + 1) &
		(lpfc_debugfs_max_slow_ring_trc - 1);
	for (i = index; i < lpfc_debugfs_max_slow_ring_trc; i++) {
		dtp = phba->slow_ring_trc + i;
		if (!dtp->fmt)
			continue;
		ms = jiffies_to_msecs(dtp->jif - lpfc_debugfs_start_time);
		snprintf(buffer,
			LPFC_DEBUG_TRC_ENTRY_SIZE, "%010d:%010d ms:%s\n",
			dtp->seq_cnt, ms, dtp->fmt);
		len +=  scnprintf(buf+len, size-len, buffer,
			dtp->data1, dtp->data2, dtp->data3);
	}
	for (i = 0; i < index; i++) {
		dtp = phba->slow_ring_trc + i;
		if (!dtp->fmt)
			continue;
		ms = jiffies_to_msecs(dtp->jif - lpfc_debugfs_start_time);
		snprintf(buffer,
			LPFC_DEBUG_TRC_ENTRY_SIZE, "%010d:%010d ms:%s\n",
			dtp->seq_cnt, ms, dtp->fmt);
		len +=  scnprintf(buf+len, size-len, buffer,
			dtp->data1, dtp->data2, dtp->data3);
	}

	lpfc_debugfs_enable = enable;
	kfree(buffer);

	return len;
}

static int lpfc_debugfs_last_hbq = -1;

/**
 * lpfc_debugfs_hbqinfo_data - Dump host buffer queue info to a buffer
 * @phba: The HBA to gather host buffer info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine dumps the host buffer queue info from the @phba to @buf up to
 * @size number of bytes. A header that describes the current hbq state will be
 * dumped to @buf first and then info on each hbq entry will be dumped to @buf
 * until @size bytes have been dumped or all the hbq info has been dumped.
 *
 * Notes:
 * This routine will rotate through each configured HBQ each time called.
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_hbqinfo_data(struct lpfc_hba *phba, char *buf, int size)
{
	int len = 0;
	int i, j, found, posted, low;
	uint32_t phys, raw_index, getidx;
	struct lpfc_hbq_init *hip;
	struct hbq_s *hbqs;
	struct lpfc_hbq_entry *hbqe;
	struct lpfc_dmabuf *d_buf;
	struct hbq_dmabuf *hbq_buf;

	if (phba->sli_rev != 3)
		return 0;

	spin_lock_irq(&phba->hbalock);

	/* toggle between multiple hbqs, if any */
	i = lpfc_sli_hbq_count();
	if (i > 1) {
		 lpfc_debugfs_last_hbq++;
		 if (lpfc_debugfs_last_hbq >= i)
			lpfc_debugfs_last_hbq = 0;
	}
	else
		lpfc_debugfs_last_hbq = 0;

	i = lpfc_debugfs_last_hbq;

	len +=  scnprintf(buf+len, size-len, "HBQ %d Info\n", i);

	hbqs =  &phba->hbqs[i];
	posted = 0;
	list_for_each_entry(d_buf, &hbqs->hbq_buffer_list, list)
		posted++;

	hip =  lpfc_hbq_defs[i];
	len +=  scnprintf(buf+len, size-len,
		"idx:%d prof:%d rn:%d bufcnt:%d icnt:%d acnt:%d posted %d\n",
		hip->hbq_index, hip->profile, hip->rn,
		hip->buffer_count, hip->init_count, hip->add_count, posted);

	raw_index = phba->hbq_get[i];
	getidx = le32_to_cpu(raw_index);
	len +=  scnprintf(buf+len, size-len,
		"entries:%d bufcnt:%d Put:%d nPut:%d localGet:%d hbaGet:%d\n",
		hbqs->entry_count, hbqs->buffer_count, hbqs->hbqPutIdx,
		hbqs->next_hbqPutIdx, hbqs->local_hbqGetIdx, getidx);

	hbqe = (struct lpfc_hbq_entry *) phba->hbqs[i].hbq_virt;
	for (j=0; j<hbqs->entry_count; j++) {
		len +=  scnprintf(buf+len, size-len,
			"%03d: %08x %04x %05x ", j,
			le32_to_cpu(hbqe->bde.addrLow),
			le32_to_cpu(hbqe->bde.tus.w),
			le32_to_cpu(hbqe->buffer_tag));
		i = 0;
		found = 0;

		/* First calculate if slot has an associated posted buffer */
		low = hbqs->hbqPutIdx - posted;
		if (low >= 0) {
			if ((j >= hbqs->hbqPutIdx) || (j < low)) {
				len +=  scnprintf(buf + len, size - len,
						"Unused\n");
				goto skipit;
			}
		}
		else {
			if ((j >= hbqs->hbqPutIdx) &&
				(j < (hbqs->entry_count+low))) {
				len +=  scnprintf(buf + len, size - len,
						"Unused\n");
				goto skipit;
			}
		}

		/* Get the Buffer info for the posted buffer */
		list_for_each_entry(d_buf, &hbqs->hbq_buffer_list, list) {
			hbq_buf = container_of(d_buf, struct hbq_dmabuf, dbuf);
			phys = ((uint64_t)hbq_buf->dbuf.phys & 0xffffffff);
			if (phys == le32_to_cpu(hbqe->bde.addrLow)) {
				len +=  scnprintf(buf+len, size-len,
					"Buf%d: x%px %06x\n", i,
					hbq_buf->dbuf.virt, hbq_buf->tag);
				found = 1;
				break;
			}
			i++;
		}
		if (!found) {
			len +=  scnprintf(buf+len, size-len, "No DMAinfo?\n");
		}
skipit:
		hbqe++;
		if (len > LPFC_HBQINFO_SIZE - 54)
			break;
	}
	spin_unlock_irq(&phba->hbalock);
	return len;
}

static int lpfc_debugfs_last_xripool;

/**
 * lpfc_debugfs_common_xri_data - Dump Hardware Queue info to a buffer
 * @phba: The HBA to gather host buffer info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine dumps the Hardware Queue info from the @phba to @buf up to
 * @size number of bytes. A header that describes the current hdwq state will be
 * dumped to @buf first and then info on each hdwq entry will be dumped to @buf
 * until @size bytes have been dumped or all the hdwq info has been dumped.
 *
 * Notes:
 * This routine will rotate through each configured Hardware Queue each
 * time called.
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_commonxripools_data(struct lpfc_hba *phba, char *buf, int size)
{
	struct lpfc_sli4_hdw_queue *qp;
	int len = 0;
	int i, out;
	unsigned long iflag;

	for (i = 0; i < phba->cfg_hdw_queue; i++) {
		if (len > (LPFC_DUMP_MULTIXRIPOOL_SIZE - 80))
			break;
		qp = &phba->sli4_hba.hdwq[lpfc_debugfs_last_xripool];

		len += scnprintf(buf + len, size - len, "HdwQ %d Info ", i);
		spin_lock_irqsave(&qp->abts_scsi_buf_list_lock, iflag);
		spin_lock(&qp->abts_nvme_buf_list_lock);
		spin_lock(&qp->io_buf_list_get_lock);
		spin_lock(&qp->io_buf_list_put_lock);
		out = qp->total_io_bufs - (qp->get_io_bufs + qp->put_io_bufs +
			qp->abts_scsi_io_bufs + qp->abts_nvme_io_bufs);
		len += scnprintf(buf + len, size - len,
				 "tot:%d get:%d put:%d mt:%d "
				 "ABTS scsi:%d nvme:%d Out:%d\n",
			qp->total_io_bufs, qp->get_io_bufs, qp->put_io_bufs,
			qp->empty_io_bufs, qp->abts_scsi_io_bufs,
			qp->abts_nvme_io_bufs, out);
		spin_unlock(&qp->io_buf_list_put_lock);
		spin_unlock(&qp->io_buf_list_get_lock);
		spin_unlock(&qp->abts_nvme_buf_list_lock);
		spin_unlock_irqrestore(&qp->abts_scsi_buf_list_lock, iflag);

		lpfc_debugfs_last_xripool++;
		if (lpfc_debugfs_last_xripool >= phba->cfg_hdw_queue)
			lpfc_debugfs_last_xripool = 0;
	}

	return len;
}

/**
 * lpfc_debugfs_multixripools_data - Display multi-XRI pools information
 * @phba: The HBA to gather host buffer info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine displays current multi-XRI pools information including XRI
 * count in public, private and txcmplq. It also displays current high and
 * low watermark.
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_multixripools_data(struct lpfc_hba *phba, char *buf, int size)
{
	u32 i;
	u32 hwq_count;
	struct lpfc_sli4_hdw_queue *qp;
	struct lpfc_multixri_pool *multixri_pool;
	struct lpfc_pvt_pool *pvt_pool;
	struct lpfc_pbl_pool *pbl_pool;
	u32 txcmplq_cnt;
	char tmp[LPFC_DEBUG_OUT_LINE_SZ] = {0};

	if (phba->sli_rev != LPFC_SLI_REV4)
		return 0;

	if (!phba->sli4_hba.hdwq)
		return 0;

	if (!phba->cfg_xri_rebalancing) {
		i = lpfc_debugfs_commonxripools_data(phba, buf, size);
		return i;
	}

	/*
	 * Pbl: Current number of free XRIs in public pool
	 * Pvt: Current number of free XRIs in private pool
	 * Busy: Current number of outstanding XRIs
	 * HWM: Current high watermark
	 * pvt_empty: Incremented by 1 when IO submission fails (no xri)
	 * pbl_empty: Incremented by 1 when all pbl_pool are empty during
	 *            IO submission
	 */
	scnprintf(tmp, sizeof(tmp),
		  "HWQ:  Pbl  Pvt Busy  HWM |  pvt_empty  pbl_empty ");
	if (strlcat(buf, tmp, size) >= size)
		return strnlen(buf, size);

#ifdef LPFC_MXP_STAT
	/*
	 * MAXH: Max high watermark seen so far
	 * above_lmt: Incremented by 1 if xri_owned > xri_limit during
	 *            IO submission
	 * below_lmt: Incremented by 1 if xri_owned <= xri_limit  during
	 *            IO submission
	 * locPbl_hit: Incremented by 1 if successfully get a batch of XRI from
	 *             local pbl_pool
	 * othPbl_hit: Incremented by 1 if successfully get a batch of XRI from
	 *             other pbl_pool
	 */
	scnprintf(tmp, sizeof(tmp),
		  "MAXH  above_lmt  below_lmt locPbl_hit othPbl_hit");
	if (strlcat(buf, tmp, size) >= size)
		return strnlen(buf, size);

	/*
	 * sPbl: snapshot of Pbl 15 sec after stat gets cleared
	 * sPvt: snapshot of Pvt 15 sec after stat gets cleared
	 * sBusy: snapshot of Busy 15 sec after stat gets cleared
	 */
	scnprintf(tmp, sizeof(tmp),
		  " | sPbl sPvt sBusy");
	if (strlcat(buf, tmp, size) >= size)
		return strnlen(buf, size);
#endif

	scnprintf(tmp, sizeof(tmp), "\n");
	if (strlcat(buf, tmp, size) >= size)
		return strnlen(buf, size);

	hwq_count = phba->cfg_hdw_queue;
	for (i = 0; i < hwq_count; i++) {
		qp = &phba->sli4_hba.hdwq[i];
		multixri_pool = qp->p_multixri_pool;
		if (!multixri_pool)
			continue;
		pbl_pool = &multixri_pool->pbl_pool;
		pvt_pool = &multixri_pool->pvt_pool;
		txcmplq_cnt = qp->fcp_wq->pring->txcmplq_cnt;
		if (qp->nvme_wq)
			txcmplq_cnt += qp->nvme_wq->pring->txcmplq_cnt;

		scnprintf(tmp, sizeof(tmp),
			  "%03d: %4d %4d %4d %4d | %10d %10d ",
			  i, pbl_pool->count, pvt_pool->count,
			  txcmplq_cnt, pvt_pool->high_watermark,
			  qp->empty_io_bufs, multixri_pool->pbl_empty_count);
		if (strlcat(buf, tmp, size) >= size)
			break;

#ifdef LPFC_MXP_STAT
		scnprintf(tmp, sizeof(tmp),
			  "%4d %10d %10d %10d %10d",
			  multixri_pool->stat_max_hwm,
			  multixri_pool->above_limit_count,
			  multixri_pool->below_limit_count,
			  multixri_pool->local_pbl_hit_count,
			  multixri_pool->other_pbl_hit_count);
		if (strlcat(buf, tmp, size) >= size)
			break;

		scnprintf(tmp, sizeof(tmp),
			  " | %4d %4d %5d",
			  multixri_pool->stat_pbl_count,
			  multixri_pool->stat_pvt_count,
			  multixri_pool->stat_busy_count);
		if (strlcat(buf, tmp, size) >= size)
			break;
#endif

		scnprintf(tmp, sizeof(tmp), "\n");
		if (strlcat(buf, tmp, size) >= size)
			break;
	}
	return strnlen(buf, size);
}


#ifdef LPFC_HDWQ_LOCK_STAT
static int lpfc_debugfs_last_lock;

/**
 * lpfc_debugfs_lockstat_data - Dump Hardware Queue info to a buffer
 * @phba: The HBA to gather host buffer info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine dumps the Hardware Queue info from the @phba to @buf up to
 * @size number of bytes. A header that describes the current hdwq state will be
 * dumped to @buf first and then info on each hdwq entry will be dumped to @buf
 * until @size bytes have been dumped or all the hdwq info has been dumped.
 *
 * Notes:
 * This routine will rotate through each configured Hardware Queue each
 * time called.
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_lockstat_data(struct lpfc_hba *phba, char *buf, int size)
{
	struct lpfc_sli4_hdw_queue *qp;
	int len = 0;
	int i;

	if (phba->sli_rev != LPFC_SLI_REV4)
		return 0;

	if (!phba->sli4_hba.hdwq)
		return 0;

	for (i = 0; i < phba->cfg_hdw_queue; i++) {
		if (len > (LPFC_HDWQINFO_SIZE - 100))
			break;
		qp = &phba->sli4_hba.hdwq[lpfc_debugfs_last_lock];

		len += scnprintf(buf + len, size - len, "HdwQ %03d Lock ", i);
		if (phba->cfg_xri_rebalancing) {
			len += scnprintf(buf + len, size - len,
					 "get_pvt:%d mv_pvt:%d "
					 "mv2pub:%d mv2pvt:%d "
					 "put_pvt:%d put_pub:%d wq:%d\n",
					 qp->lock_conflict.alloc_pvt_pool,
					 qp->lock_conflict.mv_from_pvt_pool,
					 qp->lock_conflict.mv_to_pub_pool,
					 qp->lock_conflict.mv_to_pvt_pool,
					 qp->lock_conflict.free_pvt_pool,
					 qp->lock_conflict.free_pub_pool,
					 qp->lock_conflict.wq_access);
		} else {
			len += scnprintf(buf + len, size - len,
					 "get:%d put:%d free:%d wq:%d\n",
					 qp->lock_conflict.alloc_xri_get,
					 qp->lock_conflict.alloc_xri_put,
					 qp->lock_conflict.free_xri,
					 qp->lock_conflict.wq_access);
		}

		lpfc_debugfs_last_lock++;
		if (lpfc_debugfs_last_lock >= phba->cfg_hdw_queue)
			lpfc_debugfs_last_lock = 0;
	}

	return len;
}
#endif

static int lpfc_debugfs_last_hba_slim_off;

/**
 * lpfc_debugfs_dumpHBASlim_data - Dump HBA SLIM info to a buffer
 * @phba: The HBA to gather SLIM info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine dumps the current contents of HBA SLIM for the HBA associated
 * with @phba to @buf up to @size bytes of data. This is the raw HBA SLIM data.
 *
 * Notes:
 * This routine will only dump up to 1024 bytes of data each time called and
 * should be called multiple times to dump the entire HBA SLIM.
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_dumpHBASlim_data(struct lpfc_hba *phba, char *buf, int size)
{
	int len = 0;
	int i, off;
	uint32_t *ptr;
	char *buffer;

	buffer = kmalloc(1024, GFP_KERNEL);
	if (!buffer)
		return 0;

	off = 0;
	spin_lock_irq(&phba->hbalock);

	len +=  scnprintf(buf+len, size-len, "HBA SLIM\n");
	lpfc_memcpy_from_slim(buffer,
		phba->MBslimaddr + lpfc_debugfs_last_hba_slim_off, 1024);

	ptr = (uint32_t *)&buffer[0];
	off = lpfc_debugfs_last_hba_slim_off;

	/* Set it up for the next time */
	lpfc_debugfs_last_hba_slim_off += 1024;
	if (lpfc_debugfs_last_hba_slim_off >= 4096)
		lpfc_debugfs_last_hba_slim_off = 0;

	i = 1024;
	while (i > 0) {
		len +=  scnprintf(buf+len, size-len,
		"%08x: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		off, *ptr, *(ptr+1), *(ptr+2), *(ptr+3), *(ptr+4),
		*(ptr+5), *(ptr+6), *(ptr+7));
		ptr += 8;
		i -= (8 * sizeof(uint32_t));
		off += (8 * sizeof(uint32_t));
	}

	spin_unlock_irq(&phba->hbalock);
	kfree(buffer);

	return len;
}

/**
 * lpfc_debugfs_dumpHostSlim_data - Dump host SLIM info to a buffer
 * @phba: The HBA to gather Host SLIM info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine dumps the current contents of host SLIM for the host associated
 * with @phba to @buf up to @size bytes of data. The dump will contain the
 * Mailbox, PCB, Rings, and Registers that are located in host memory.
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_dumpHostSlim_data(struct lpfc_hba *phba, char *buf, int size)
{
	int len = 0;
	int i, off;
	uint32_t word0, word1, word2, word3;
	uint32_t *ptr;
	struct lpfc_pgp *pgpp;
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring;

	off = 0;
	spin_lock_irq(&phba->hbalock);

	len +=  scnprintf(buf+len, size-len, "SLIM Mailbox\n");
	ptr = (uint32_t *)phba->slim2p.virt;
	i = sizeof(MAILBOX_t);
	while (i > 0) {
		len +=  scnprintf(buf+len, size-len,
		"%08x: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		off, *ptr, *(ptr+1), *(ptr+2), *(ptr+3), *(ptr+4),
		*(ptr+5), *(ptr+6), *(ptr+7));
		ptr += 8;
		i -= (8 * sizeof(uint32_t));
		off += (8 * sizeof(uint32_t));
	}

	len +=  scnprintf(buf+len, size-len, "SLIM PCB\n");
	ptr = (uint32_t *)phba->pcb;
	i = sizeof(PCB_t);
	while (i > 0) {
		len +=  scnprintf(buf+len, size-len,
		"%08x: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		off, *ptr, *(ptr+1), *(ptr+2), *(ptr+3), *(ptr+4),
		*(ptr+5), *(ptr+6), *(ptr+7));
		ptr += 8;
		i -= (8 * sizeof(uint32_t));
		off += (8 * sizeof(uint32_t));
	}

	if (phba->sli_rev <= LPFC_SLI_REV3) {
		for (i = 0; i < 4; i++) {
			pgpp = &phba->port_gp[i];
			pring = &psli->sli3_ring[i];
			len +=  scnprintf(buf+len, size-len,
					 "Ring %d: CMD GetInx:%d "
					 "(Max:%d Next:%d "
					 "Local:%d flg:x%x)  "
					 "RSP PutInx:%d Max:%d\n",
					 i, pgpp->cmdGetInx,
					 pring->sli.sli3.numCiocb,
					 pring->sli.sli3.next_cmdidx,
					 pring->sli.sli3.local_getidx,
					 pring->flag, pgpp->rspPutInx,
					 pring->sli.sli3.numRiocb);
		}

		word0 = readl(phba->HAregaddr);
		word1 = readl(phba->CAregaddr);
		word2 = readl(phba->HSregaddr);
		word3 = readl(phba->HCregaddr);
		len +=  scnprintf(buf+len, size-len, "HA:%08x CA:%08x HS:%08x "
				 "HC:%08x\n", word0, word1, word2, word3);
	}
	spin_unlock_irq(&phba->hbalock);
	return len;
}

/**
 * lpfc_debugfs_nodelist_data - Dump target node list to a buffer
 * @vport: The vport to gather target node info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine dumps the current target node list associated with @vport to
 * @buf up to @size bytes of data. Each node entry in the dump will contain a
 * node state, DID, WWPN, WWNN, RPI, flags, type, and other useful fields.
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_nodelist_data(struct lpfc_vport *vport, char *buf, int size)
{
	int len = 0;
	int i, iocnt, outio, cnt;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_hba  *phba = vport->phba;
	struct lpfc_nodelist *ndlp;
	unsigned char *statep;
	struct nvme_fc_local_port *localport;
	struct nvme_fc_remote_port *nrport = NULL;
	struct lpfc_nvme_rport *rport;

	cnt = (LPFC_NODELIST_SIZE / LPFC_NODELIST_ENTRY_SIZE);
	outio = 0;

	len += scnprintf(buf+len, size-len, "\nFCP Nodelist Entries ...\n");
	spin_lock_irq(shost->host_lock);
	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		iocnt = 0;
		if (!cnt) {
			len +=  scnprintf(buf+len, size-len,
				"Missing Nodelist Entries\n");
			break;
		}
		cnt--;
		switch (ndlp->nlp_state) {
		case NLP_STE_UNUSED_NODE:
			statep = "UNUSED";
			break;
		case NLP_STE_PLOGI_ISSUE:
			statep = "PLOGI ";
			break;
		case NLP_STE_ADISC_ISSUE:
			statep = "ADISC ";
			break;
		case NLP_STE_REG_LOGIN_ISSUE:
			statep = "REGLOG";
			break;
		case NLP_STE_PRLI_ISSUE:
			statep = "PRLI  ";
			break;
		case NLP_STE_LOGO_ISSUE:
			statep = "LOGO  ";
			break;
		case NLP_STE_UNMAPPED_NODE:
			statep = "UNMAP ";
			iocnt = 1;
			break;
		case NLP_STE_MAPPED_NODE:
			statep = "MAPPED";
			iocnt = 1;
			break;
		case NLP_STE_NPR_NODE:
			statep = "NPR   ";
			break;
		default:
			statep = "UNKNOWN";
		}
		len += scnprintf(buf+len, size-len, "%s DID:x%06x ",
				statep, ndlp->nlp_DID);
		len += scnprintf(buf+len, size-len,
				"WWPN x%llx ",
				wwn_to_u64(ndlp->nlp_portname.u.wwn));
		len += scnprintf(buf+len, size-len,
				"WWNN x%llx ",
				wwn_to_u64(ndlp->nlp_nodename.u.wwn));
		if (ndlp->nlp_flag & NLP_RPI_REGISTERED)
			len += scnprintf(buf+len, size-len, "RPI:%03d ",
					ndlp->nlp_rpi);
		else
			len += scnprintf(buf+len, size-len, "RPI:none ");
		len +=  scnprintf(buf+len, size-len, "flag:x%08x ",
			ndlp->nlp_flag);
		if (!ndlp->nlp_type)
			len += scnprintf(buf+len, size-len, "UNKNOWN_TYPE ");
		if (ndlp->nlp_type & NLP_FC_NODE)
			len += scnprintf(buf+len, size-len, "FC_NODE ");
		if (ndlp->nlp_type & NLP_FABRIC) {
			len += scnprintf(buf+len, size-len, "FABRIC ");
			iocnt = 0;
		}
		if (ndlp->nlp_type & NLP_FCP_TARGET)
			len += scnprintf(buf+len, size-len, "FCP_TGT sid:%d ",
				ndlp->nlp_sid);
		if (ndlp->nlp_type & NLP_FCP_INITIATOR)
			len += scnprintf(buf+len, size-len, "FCP_INITIATOR ");
		if (ndlp->nlp_type & NLP_NVME_TARGET)
			len += scnprintf(buf + len,
					size - len, "NVME_TGT sid:%d ",
					NLP_NO_SID);
		if (ndlp->nlp_type & NLP_NVME_INITIATOR)
			len += scnprintf(buf + len,
					size - len, "NVME_INITIATOR ");
		len += scnprintf(buf+len, size-len, "usgmap:%x ",
			ndlp->nlp_usg_map);
		len += scnprintf(buf+len, size-len, "refcnt:%x",
			kref_read(&ndlp->kref));
		if (iocnt) {
			i = atomic_read(&ndlp->cmd_pending);
			len += scnprintf(buf + len, size - len,
					" OutIO:x%x Qdepth x%x",
					i, ndlp->cmd_qdepth);
			outio += i;
		}
		len += scnprintf(buf + len, size - len, "defer:%x ",
			ndlp->nlp_defer_did);
		len +=  scnprintf(buf+len, size-len, "\n");
	}
	spin_unlock_irq(shost->host_lock);

	len += scnprintf(buf + len, size - len,
			"\nOutstanding IO x%x\n",  outio);

	if (phba->nvmet_support && phba->targetport && (vport == phba->pport)) {
		len += scnprintf(buf + len, size - len,
				"\nNVME Targetport Entry ...\n");

		/* Port state is only one of two values for now. */
		if (phba->targetport->port_id)
			statep = "REGISTERED";
		else
			statep = "INIT";
		len += scnprintf(buf + len, size - len,
				"TGT WWNN x%llx WWPN x%llx State %s\n",
				wwn_to_u64(vport->fc_nodename.u.wwn),
				wwn_to_u64(vport->fc_portname.u.wwn),
				statep);
		len += scnprintf(buf + len, size - len,
				"    Targetport DID x%06x\n",
				phba->targetport->port_id);
		goto out_exit;
	}

	len += scnprintf(buf + len, size - len,
				"\nNVME Lport/Rport Entries ...\n");

	localport = vport->localport;
	if (!localport)
		goto out_exit;

	spin_lock_irq(shost->host_lock);

	/* Port state is only one of two values for now. */
	if (localport->port_id)
		statep = "ONLINE";
	else
		statep = "UNKNOWN ";

	len += scnprintf(buf + len, size - len,
			"Lport DID x%06x PortState %s\n",
			localport->port_id, statep);

	len += scnprintf(buf + len, size - len, "\tRport List:\n");
	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		/* local short-hand pointer. */
		spin_lock(&phba->hbalock);
		rport = lpfc_ndlp_get_nrport(ndlp);
		if (rport)
			nrport = rport->remoteport;
		else
			nrport = NULL;
		spin_unlock(&phba->hbalock);
		if (!nrport)
			continue;

		/* Port state is only one of two values for now. */
		switch (nrport->port_state) {
		case FC_OBJSTATE_ONLINE:
			statep = "ONLINE";
			break;
		case FC_OBJSTATE_UNKNOWN:
			statep = "UNKNOWN ";
			break;
		default:
			statep = "UNSUPPORTED";
			break;
		}

		/* Tab in to show lport ownership. */
		len += scnprintf(buf + len, size - len,
				"\t%s Port ID:x%06x ",
				statep, nrport->port_id);
		len += scnprintf(buf + len, size - len, "WWPN x%llx ",
				nrport->port_name);
		len += scnprintf(buf + len, size - len, "WWNN x%llx ",
				nrport->node_name);

		/* An NVME rport can have multiple roles. */
		if (nrport->port_role & FC_PORT_ROLE_NVME_INITIATOR)
			len +=  scnprintf(buf + len, size - len,
					 "INITIATOR ");
		if (nrport->port_role & FC_PORT_ROLE_NVME_TARGET)
			len +=  scnprintf(buf + len, size - len,
					 "TARGET ");
		if (nrport->port_role & FC_PORT_ROLE_NVME_DISCOVERY)
			len +=  scnprintf(buf + len, size - len,
					 "DISCSRVC ");
		if (nrport->port_role & ~(FC_PORT_ROLE_NVME_INITIATOR |
					  FC_PORT_ROLE_NVME_TARGET |
					  FC_PORT_ROLE_NVME_DISCOVERY))
			len +=  scnprintf(buf + len, size - len,
					 "UNKNOWN ROLE x%x",
					 nrport->port_role);
		/* Terminate the string. */
		len +=  scnprintf(buf + len, size - len, "\n");
	}

	spin_unlock_irq(shost->host_lock);
 out_exit:
	return len;
}

/**
 * lpfc_debugfs_nvmestat_data - Dump target node list to a buffer
 * @vport: The vport to gather target node info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine dumps the NVME statistics associated with @vport
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_nvmestat_data(struct lpfc_vport *vport, char *buf, int size)
{
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_nvmet_tgtport *tgtp;
	struct lpfc_nvmet_rcv_ctx *ctxp, *next_ctxp;
	struct nvme_fc_local_port *localport;
	struct lpfc_fc4_ctrl_stat *cstat;
	struct lpfc_nvme_lport *lport;
	uint64_t data1, data2, data3;
	uint64_t tot, totin, totout;
	int cnt, i;
	int len = 0;

	if (phba->nvmet_support) {
		if (!phba->targetport)
			return len;
		tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
		len += scnprintf(buf + len, size - len,
				"\nNVME Targetport Statistics\n");

		len += scnprintf(buf + len, size - len,
				"LS: Rcv %08x Drop %08x Abort %08x\n",
				atomic_read(&tgtp->rcv_ls_req_in),
				atomic_read(&tgtp->rcv_ls_req_drop),
				atomic_read(&tgtp->xmt_ls_abort));
		if (atomic_read(&tgtp->rcv_ls_req_in) !=
		    atomic_read(&tgtp->rcv_ls_req_out)) {
			len += scnprintf(buf + len, size - len,
					"Rcv LS: in %08x != out %08x\n",
					atomic_read(&tgtp->rcv_ls_req_in),
					atomic_read(&tgtp->rcv_ls_req_out));
		}

		len += scnprintf(buf + len, size - len,
				"LS: Xmt %08x Drop %08x Cmpl %08x\n",
				atomic_read(&tgtp->xmt_ls_rsp),
				atomic_read(&tgtp->xmt_ls_drop),
				atomic_read(&tgtp->xmt_ls_rsp_cmpl));

		len += scnprintf(buf + len, size - len,
				"LS: RSP Abort %08x xb %08x Err %08x\n",
				atomic_read(&tgtp->xmt_ls_rsp_aborted),
				atomic_read(&tgtp->xmt_ls_rsp_xb_set),
				atomic_read(&tgtp->xmt_ls_rsp_error));

		len += scnprintf(buf + len, size - len,
				"FCP: Rcv %08x Defer %08x Release %08x "
				"Drop %08x\n",
				atomic_read(&tgtp->rcv_fcp_cmd_in),
				atomic_read(&tgtp->rcv_fcp_cmd_defer),
				atomic_read(&tgtp->xmt_fcp_release),
				atomic_read(&tgtp->rcv_fcp_cmd_drop));

		if (atomic_read(&tgtp->rcv_fcp_cmd_in) !=
		    atomic_read(&tgtp->rcv_fcp_cmd_out)) {
			len += scnprintf(buf + len, size - len,
					"Rcv FCP: in %08x != out %08x\n",
					atomic_read(&tgtp->rcv_fcp_cmd_in),
					atomic_read(&tgtp->rcv_fcp_cmd_out));
		}

		len += scnprintf(buf + len, size - len,
				"FCP Rsp: read %08x readrsp %08x "
				"write %08x rsp %08x\n",
				atomic_read(&tgtp->xmt_fcp_read),
				atomic_read(&tgtp->xmt_fcp_read_rsp),
				atomic_read(&tgtp->xmt_fcp_write),
				atomic_read(&tgtp->xmt_fcp_rsp));

		len += scnprintf(buf + len, size - len,
				"FCP Rsp Cmpl: %08x err %08x drop %08x\n",
				atomic_read(&tgtp->xmt_fcp_rsp_cmpl),
				atomic_read(&tgtp->xmt_fcp_rsp_error),
				atomic_read(&tgtp->xmt_fcp_rsp_drop));

		len += scnprintf(buf + len, size - len,
				"FCP Rsp Abort: %08x xb %08x xricqe  %08x\n",
				atomic_read(&tgtp->xmt_fcp_rsp_aborted),
				atomic_read(&tgtp->xmt_fcp_rsp_xb_set),
				atomic_read(&tgtp->xmt_fcp_xri_abort_cqe));

		len += scnprintf(buf + len, size - len,
				"ABORT: Xmt %08x Cmpl %08x\n",
				atomic_read(&tgtp->xmt_fcp_abort),
				atomic_read(&tgtp->xmt_fcp_abort_cmpl));

		len += scnprintf(buf + len, size - len,
				"ABORT: Sol %08x  Usol %08x Err %08x Cmpl %08x",
				atomic_read(&tgtp->xmt_abort_sol),
				atomic_read(&tgtp->xmt_abort_unsol),
				atomic_read(&tgtp->xmt_abort_rsp),
				atomic_read(&tgtp->xmt_abort_rsp_error));

		len +=  scnprintf(buf + len, size - len, "\n");

		cnt = 0;
		spin_lock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		list_for_each_entry_safe(ctxp, next_ctxp,
				&phba->sli4_hba.lpfc_abts_nvmet_ctx_list,
				list) {
			cnt++;
		}
		spin_unlock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		if (cnt) {
			len += scnprintf(buf + len, size - len,
					"ABORT: %d ctx entries\n", cnt);
			spin_lock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
			list_for_each_entry_safe(ctxp, next_ctxp,
				    &phba->sli4_hba.lpfc_abts_nvmet_ctx_list,
				    list) {
				if (len >= (size - LPFC_DEBUG_OUT_LINE_SZ))
					break;
				len += scnprintf(buf + len, size - len,
						"Entry: oxid %x state %x "
						"flag %x\n",
						ctxp->oxid, ctxp->state,
						ctxp->flag);
			}
			spin_unlock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		}

		/* Calculate outstanding IOs */
		tot = atomic_read(&tgtp->rcv_fcp_cmd_drop);
		tot += atomic_read(&tgtp->xmt_fcp_release);
		tot = atomic_read(&tgtp->rcv_fcp_cmd_in) - tot;

		len += scnprintf(buf + len, size - len,
				"IO_CTX: %08x  WAIT: cur %08x tot %08x\n"
				"CTX Outstanding %08llx\n",
				phba->sli4_hba.nvmet_xri_cnt,
				phba->sli4_hba.nvmet_io_wait_cnt,
				phba->sli4_hba.nvmet_io_wait_total,
				tot);
	} else {
		if (!(vport->cfg_enable_fc4_type & LPFC_ENABLE_NVME))
			return len;

		localport = vport->localport;
		if (!localport)
			return len;
		lport = (struct lpfc_nvme_lport *)localport->private;
		if (!lport)
			return len;

		len += scnprintf(buf + len, size - len,
				"\nNVME HDWQ Statistics\n");

		len += scnprintf(buf + len, size - len,
				"LS: Xmt %016x Cmpl %016x\n",
				atomic_read(&lport->fc4NvmeLsRequests),
				atomic_read(&lport->fc4NvmeLsCmpls));

		totin = 0;
		totout = 0;
		for (i = 0; i < phba->cfg_hdw_queue; i++) {
			cstat = &phba->sli4_hba.hdwq[i].nvme_cstat;
			tot = cstat->io_cmpls;
			totin += tot;
			data1 = cstat->input_requests;
			data2 = cstat->output_requests;
			data3 = cstat->control_requests;
			totout += (data1 + data2 + data3);

			/* Limit to 32, debugfs display buffer limitation */
			if (i >= 32)
				continue;

			len += scnprintf(buf + len, PAGE_SIZE - len,
					"HDWQ (%d): Rd %016llx Wr %016llx "
					"IO %016llx ",
					i, data1, data2, data3);
			len += scnprintf(buf + len, PAGE_SIZE - len,
					"Cmpl %016llx OutIO %016llx\n",
					tot, ((data1 + data2 + data3) - tot));
		}
		len += scnprintf(buf + len, PAGE_SIZE - len,
				"Total FCP Cmpl %016llx Issue %016llx "
				"OutIO %016llx\n",
				totin, totout, totout - totin);

		len += scnprintf(buf + len, size - len,
				"LS Xmt Err: Abrt %08x Err %08x  "
				"Cmpl Err: xb %08x Err %08x\n",
				atomic_read(&lport->xmt_ls_abort),
				atomic_read(&lport->xmt_ls_err),
				atomic_read(&lport->cmpl_ls_xb),
				atomic_read(&lport->cmpl_ls_err));

		len += scnprintf(buf + len, size - len,
				"FCP Xmt Err: noxri %06x nondlp %06x "
				"qdepth %06x wqerr %06x err %06x Abrt %06x\n",
				atomic_read(&lport->xmt_fcp_noxri),
				atomic_read(&lport->xmt_fcp_bad_ndlp),
				atomic_read(&lport->xmt_fcp_qdepth),
				atomic_read(&lport->xmt_fcp_wqerr),
				atomic_read(&lport->xmt_fcp_err),
				atomic_read(&lport->xmt_fcp_abort));

		len += scnprintf(buf + len, size - len,
				"FCP Cmpl Err: xb %08x Err %08x\n",
				atomic_read(&lport->cmpl_fcp_xb),
				atomic_read(&lport->cmpl_fcp_err));

	}

	return len;
}

/**
 * lpfc_debugfs_scsistat_data - Dump target node list to a buffer
 * @vport: The vport to gather target node info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine dumps the SCSI statistics associated with @vport
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_scsistat_data(struct lpfc_vport *vport, char *buf, int size)
{
	int len;
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_fc4_ctrl_stat *cstat;
	u64 data1, data2, data3;
	u64 tot, totin, totout;
	int i;
	char tmp[LPFC_MAX_SCSI_INFO_TMP_LEN] = {0};

	if (!(vport->cfg_enable_fc4_type & LPFC_ENABLE_FCP) ||
	    (phba->sli_rev != LPFC_SLI_REV4))
		return 0;

	scnprintf(buf, size, "SCSI HDWQ Statistics\n");

	totin = 0;
	totout = 0;
	for (i = 0; i < phba->cfg_hdw_queue; i++) {
		cstat = &phba->sli4_hba.hdwq[i].scsi_cstat;
		tot = cstat->io_cmpls;
		totin += tot;
		data1 = cstat->input_requests;
		data2 = cstat->output_requests;
		data3 = cstat->control_requests;
		totout += (data1 + data2 + data3);

		scnprintf(tmp, sizeof(tmp), "HDWQ (%d): Rd %016llx Wr %016llx "
			  "IO %016llx ", i, data1, data2, data3);
		if (strlcat(buf, tmp, size) >= size)
			goto buffer_done;

		scnprintf(tmp, sizeof(tmp), "Cmpl %016llx OutIO %016llx\n",
			  tot, ((data1 + data2 + data3) - tot));
		if (strlcat(buf, tmp, size) >= size)
			goto buffer_done;
	}
	scnprintf(tmp, sizeof(tmp), "Total FCP Cmpl %016llx Issue %016llx "
		  "OutIO %016llx\n", totin, totout, totout - totin);
	strlcat(buf, tmp, size);

buffer_done:
	len = strnlen(buf, size);

	return len;
}

/**
 * lpfc_debugfs_nvmektime_data - Dump target node list to a buffer
 * @vport: The vport to gather target node info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine dumps the NVME statistics associated with @vport
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_nvmektime_data(struct lpfc_vport *vport, char *buf, int size)
{
	struct lpfc_hba   *phba = vport->phba;
	int len = 0;

	if (phba->nvmet_support == 0) {
		/* NVME Initiator */
		len += scnprintf(buf + len, PAGE_SIZE - len,
				"ktime %s: Total Samples: %lld\n",
				(phba->ktime_on ?  "Enabled" : "Disabled"),
				phba->ktime_data_samples);
		if (phba->ktime_data_samples == 0)
			return len;

		len += scnprintf(
			buf + len, PAGE_SIZE - len,
			"Segment 1: Last NVME Cmd cmpl "
			"done -to- Start of next NVME cnd (in driver)\n");
		len += scnprintf(
			buf + len, PAGE_SIZE - len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg1_total,
				phba->ktime_data_samples),
			phba->ktime_seg1_min,
			phba->ktime_seg1_max);
		len += scnprintf(
			buf + len, PAGE_SIZE - len,
			"Segment 2: Driver start of NVME cmd "
			"-to- Firmware WQ doorbell\n");
		len += scnprintf(
			buf + len, PAGE_SIZE - len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg2_total,
				phba->ktime_data_samples),
			phba->ktime_seg2_min,
			phba->ktime_seg2_max);
		len += scnprintf(
			buf + len, PAGE_SIZE - len,
			"Segment 3: Firmware WQ doorbell -to- "
			"MSI-X ISR cmpl\n");
		len += scnprintf(
			buf + len, PAGE_SIZE - len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg3_total,
				phba->ktime_data_samples),
			phba->ktime_seg3_min,
			phba->ktime_seg3_max);
		len += scnprintf(
			buf + len, PAGE_SIZE - len,
			"Segment 4: MSI-X ISR cmpl -to- "
			"NVME cmpl done\n");
		len += scnprintf(
			buf + len, PAGE_SIZE - len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg4_total,
				phba->ktime_data_samples),
			phba->ktime_seg4_min,
			phba->ktime_seg4_max);
		len += scnprintf(
			buf + len, PAGE_SIZE - len,
			"Total IO avg time: %08lld\n",
			div_u64(phba->ktime_seg1_total +
			phba->ktime_seg2_total  +
			phba->ktime_seg3_total +
			phba->ktime_seg4_total,
			phba->ktime_data_samples));
		return len;
	}

	/* NVME Target */
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"ktime %s: Total Samples: %lld %lld\n",
			(phba->ktime_on ? "Enabled" : "Disabled"),
			phba->ktime_data_samples,
			phba->ktime_status_samples);
	if (phba->ktime_data_samples == 0)
		return len;

	len += scnprintf(buf + len, PAGE_SIZE-len,
			"Segment 1: MSI-X ISR Rcv cmd -to- "
			"cmd pass to NVME Layer\n");
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg1_total,
				phba->ktime_data_samples),
			phba->ktime_seg1_min,
			phba->ktime_seg1_max);
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"Segment 2: cmd pass to NVME Layer- "
			"-to- Driver rcv cmd OP (action)\n");
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg2_total,
				phba->ktime_data_samples),
			phba->ktime_seg2_min,
			phba->ktime_seg2_max);
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"Segment 3: Driver rcv cmd OP -to- "
			"Firmware WQ doorbell: cmd\n");
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg3_total,
				phba->ktime_data_samples),
			phba->ktime_seg3_min,
			phba->ktime_seg3_max);
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"Segment 4: Firmware WQ doorbell: cmd "
			"-to- MSI-X ISR for cmd cmpl\n");
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg4_total,
				phba->ktime_data_samples),
			phba->ktime_seg4_min,
			phba->ktime_seg4_max);
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"Segment 5: MSI-X ISR for cmd cmpl "
			"-to- NVME layer passed cmd done\n");
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg5_total,
				phba->ktime_data_samples),
			phba->ktime_seg5_min,
			phba->ktime_seg5_max);

	if (phba->ktime_status_samples == 0) {
		len += scnprintf(buf + len, PAGE_SIZE-len,
				"Total: cmd received by MSI-X ISR "
				"-to- cmd completed on wire\n");
		len += scnprintf(buf + len, PAGE_SIZE-len,
				"avg:%08lld min:%08lld "
				"max %08lld\n",
				div_u64(phba->ktime_seg10_total,
					phba->ktime_data_samples),
				phba->ktime_seg10_min,
				phba->ktime_seg10_max);
		return len;
	}

	len += scnprintf(buf + len, PAGE_SIZE-len,
			"Segment 6: NVME layer passed cmd done "
			"-to- Driver rcv rsp status OP\n");
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg6_total,
				phba->ktime_status_samples),
			phba->ktime_seg6_min,
			phba->ktime_seg6_max);
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"Segment 7: Driver rcv rsp status OP "
			"-to- Firmware WQ doorbell: status\n");
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg7_total,
				phba->ktime_status_samples),
			phba->ktime_seg7_min,
			phba->ktime_seg7_max);
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"Segment 8: Firmware WQ doorbell: status"
			" -to- MSI-X ISR for status cmpl\n");
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg8_total,
				phba->ktime_status_samples),
			phba->ktime_seg8_min,
			phba->ktime_seg8_max);
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"Segment 9: MSI-X ISR for status cmpl  "
			"-to- NVME layer passed status done\n");
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg9_total,
				phba->ktime_status_samples),
			phba->ktime_seg9_min,
			phba->ktime_seg9_max);
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"Total: cmd received by MSI-X ISR -to- "
			"cmd completed on wire\n");
	len += scnprintf(buf + len, PAGE_SIZE-len,
			"avg:%08lld min:%08lld max %08lld\n",
			div_u64(phba->ktime_seg10_total,
				phba->ktime_status_samples),
			phba->ktime_seg10_min,
			phba->ktime_seg10_max);
	return len;
}

/**
 * lpfc_debugfs_nvmeio_trc_data - Dump NVME IO trace list to a buffer
 * @phba: The phba to gather target node info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine dumps the NVME IO trace associated with @phba
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_nvmeio_trc_data(struct lpfc_hba *phba, char *buf, int size)
{
	struct lpfc_debugfs_nvmeio_trc *dtp;
	int i, state, index, skip;
	int len = 0;

	state = phba->nvmeio_trc_on;

	index = (atomic_read(&phba->nvmeio_trc_cnt) + 1) &
		(phba->nvmeio_trc_size - 1);
	skip = phba->nvmeio_trc_output_idx;

	len += scnprintf(buf + len, size - len,
			"%s IO Trace %s: next_idx %d skip %d size %d\n",
			(phba->nvmet_support ? "NVME" : "NVMET"),
			(state ? "Enabled" : "Disabled"),
			index, skip, phba->nvmeio_trc_size);

	if (!phba->nvmeio_trc || state)
		return len;

	/* trace MUST bhe off to continue */

	for (i = index; i < phba->nvmeio_trc_size; i++) {
		if (skip) {
			skip--;
			continue;
		}
		dtp = phba->nvmeio_trc + i;
		phba->nvmeio_trc_output_idx++;

		if (!dtp->fmt)
			continue;

		len +=  scnprintf(buf + len, size - len, dtp->fmt,
			dtp->data1, dtp->data2, dtp->data3);

		if (phba->nvmeio_trc_output_idx >= phba->nvmeio_trc_size) {
			phba->nvmeio_trc_output_idx = 0;
			len += scnprintf(buf + len, size - len,
					"Trace Complete\n");
			goto out;
		}

		if (len >= (size - LPFC_DEBUG_OUT_LINE_SZ)) {
			len += scnprintf(buf + len, size - len,
					"Trace Continue (%d of %d)\n",
					phba->nvmeio_trc_output_idx,
					phba->nvmeio_trc_size);
			goto out;
		}
	}
	for (i = 0; i < index; i++) {
		if (skip) {
			skip--;
			continue;
		}
		dtp = phba->nvmeio_trc + i;
		phba->nvmeio_trc_output_idx++;

		if (!dtp->fmt)
			continue;

		len +=  scnprintf(buf + len, size - len, dtp->fmt,
			dtp->data1, dtp->data2, dtp->data3);

		if (phba->nvmeio_trc_output_idx >= phba->nvmeio_trc_size) {
			phba->nvmeio_trc_output_idx = 0;
			len += scnprintf(buf + len, size - len,
					"Trace Complete\n");
			goto out;
		}

		if (len >= (size - LPFC_DEBUG_OUT_LINE_SZ)) {
			len += scnprintf(buf + len, size - len,
					"Trace Continue (%d of %d)\n",
					phba->nvmeio_trc_output_idx,
					phba->nvmeio_trc_size);
			goto out;
		}
	}

	len += scnprintf(buf + len, size - len,
			"Trace Done\n");
out:
	return len;
}

/**
 * lpfc_debugfs_cpucheck_data - Dump target node list to a buffer
 * @vport: The vport to gather target node info from.
 * @buf: The buffer to dump log into.
 * @size: The maximum amount of data to process.
 *
 * Description:
 * This routine dumps the NVME statistics associated with @vport
 *
 * Return Value:
 * This routine returns the amount of bytes that were dumped into @buf and will
 * not exceed @size.
 **/
static int
lpfc_debugfs_cpucheck_data(struct lpfc_vport *vport, char *buf, int size)
{
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_sli4_hdw_queue *qp;
	int i, j, max_cnt;
	int len = 0;
	uint32_t tot_xmt;
	uint32_t tot_rcv;
	uint32_t tot_cmpl;

	len += scnprintf(buf + len, PAGE_SIZE - len,
			"CPUcheck %s ",
			(phba->cpucheck_on & LPFC_CHECK_NVME_IO ?
				"Enabled" : "Disabled"));
	if (phba->nvmet_support) {
		len += scnprintf(buf + len, PAGE_SIZE - len,
				"%s\n",
				(phba->cpucheck_on & LPFC_CHECK_NVMET_RCV ?
					"Rcv Enabled\n" : "Rcv Disabled\n"));
	} else {
		len += scnprintf(buf + len, PAGE_SIZE - len, "\n");
	}
	max_cnt = size - LPFC_DEBUG_OUT_LINE_SZ;

	for (i = 0; i < phba->cfg_hdw_queue; i++) {
		qp = &phba->sli4_hba.hdwq[i];

		tot_rcv = 0;
		tot_xmt = 0;
		tot_cmpl = 0;
		for (j = 0; j < LPFC_CHECK_CPU_CNT; j++) {
			tot_xmt += qp->cpucheck_xmt_io[j];
			tot_cmpl += qp->cpucheck_cmpl_io[j];
			if (phba->nvmet_support)
				tot_rcv += qp->cpucheck_rcv_io[j];
		}

		/* Only display Hardware Qs with something */
		if (!tot_xmt && !tot_cmpl && !tot_rcv)
			continue;

		len += scnprintf(buf + len, PAGE_SIZE - len,
				"HDWQ %03d: ", i);
		for (j = 0; j < LPFC_CHECK_CPU_CNT; j++) {
			/* Only display non-zero counters */
			if (!qp->cpucheck_xmt_io[j] &&
			    !qp->cpucheck_cmpl_io[j] &&
			    !qp->cpucheck_rcv_io[j])
				continue;
			if (phba->nvmet_support) {
				len += scnprintf(buf + len, PAGE_SIZE - len,
						"CPU %03d: %x/%x/%x ", j,
						qp->cpucheck_rcv_io[j],
						qp->cpucheck_xmt_io[j],
						qp->cpucheck_cmpl_io[j]);
			} else {
				len += scnprintf(buf + len, PAGE_SIZE - len,
						"CPU %03d: %x/%x ", j,
						qp->cpucheck_xmt_io[j],
						qp->cpucheck_cmpl_io[j]);
			}
		}
		len += scnprintf(buf + len, PAGE_SIZE - len,
				"Total: %x\n", tot_xmt);
		if (len >= max_cnt) {
			len += scnprintf(buf + len, PAGE_SIZE - len,
					"Truncated ...\n");
			return len;
		}
	}
	return len;
}

#endif

/**
 * lpfc_debugfs_disc_trc - Store discovery trace log
 * @vport: The vport to associate this trace string with for retrieval.
 * @mask: Log entry classification.
 * @fmt: Format string to be displayed when dumping the log.
 * @data1: 1st data parameter to be applied to @fmt.
 * @data2: 2nd data parameter to be applied to @fmt.
 * @data3: 3rd data parameter to be applied to @fmt.
 *
 * Description:
 * This routine is used by the driver code to add a debugfs log entry to the
 * discovery trace buffer associated with @vport. Only entries with a @mask that
 * match the current debugfs discovery mask will be saved. Entries that do not
 * match will be thrown away. @fmt, @data1, @data2, and @data3 are used like
 * printf when displaying the log.
 **/
inline void
lpfc_debugfs_disc_trc(struct lpfc_vport *vport, int mask, char *fmt,
	uint32_t data1, uint32_t data2, uint32_t data3)
{
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	struct lpfc_debugfs_trc *dtp;
	int index;

	if (!(lpfc_debugfs_mask_disc_trc & mask))
		return;

	if (!lpfc_debugfs_enable || !lpfc_debugfs_max_disc_trc ||
		!vport || !vport->disc_trc)
		return;

	index = atomic_inc_return(&vport->disc_trc_cnt) &
		(lpfc_debugfs_max_disc_trc - 1);
	dtp = vport->disc_trc + index;
	dtp->fmt = fmt;
	dtp->data1 = data1;
	dtp->data2 = data2;
	dtp->data3 = data3;
	dtp->seq_cnt = atomic_inc_return(&lpfc_debugfs_seq_trc_cnt);
	dtp->jif = jiffies;
#endif
	return;
}

/**
 * lpfc_debugfs_slow_ring_trc - Store slow ring trace log
 * @phba: The phba to associate this trace string with for retrieval.
 * @fmt: Format string to be displayed when dumping the log.
 * @data1: 1st data parameter to be applied to @fmt.
 * @data2: 2nd data parameter to be applied to @fmt.
 * @data3: 3rd data parameter to be applied to @fmt.
 *
 * Description:
 * This routine is used by the driver code to add a debugfs log entry to the
 * discovery trace buffer associated with @vport. @fmt, @data1, @data2, and
 * @data3 are used like printf when displaying the log.
 **/
inline void
lpfc_debugfs_slow_ring_trc(struct lpfc_hba *phba, char *fmt,
	uint32_t data1, uint32_t data2, uint32_t data3)
{
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	struct lpfc_debugfs_trc *dtp;
	int index;

	if (!lpfc_debugfs_enable || !lpfc_debugfs_max_slow_ring_trc ||
		!phba || !phba->slow_ring_trc)
		return;

	index = atomic_inc_return(&phba->slow_ring_trc_cnt) &
		(lpfc_debugfs_max_slow_ring_trc - 1);
	dtp = phba->slow_ring_trc + index;
	dtp->fmt = fmt;
	dtp->data1 = data1;
	dtp->data2 = data2;
	dtp->data3 = data3;
	dtp->seq_cnt = atomic_inc_return(&lpfc_debugfs_seq_trc_cnt);
	dtp->jif = jiffies;
#endif
	return;
}

/**
 * lpfc_debugfs_nvme_trc - Store NVME/NVMET trace log
 * @phba: The phba to associate this trace string with for retrieval.
 * @fmt: Format string to be displayed when dumping the log.
 * @data1: 1st data parameter to be applied to @fmt.
 * @data2: 2nd data parameter to be applied to @fmt.
 * @data3: 3rd data parameter to be applied to @fmt.
 *
 * Description:
 * This routine is used by the driver code to add a debugfs log entry to the
 * nvme trace buffer associated with @phba. @fmt, @data1, @data2, and
 * @data3 are used like printf when displaying the log.
 **/
inline void
lpfc_debugfs_nvme_trc(struct lpfc_hba *phba, char *fmt,
		      uint16_t data1, uint16_t data2, uint32_t data3)
{
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	struct lpfc_debugfs_nvmeio_trc *dtp;
	int index;

	if (!phba->nvmeio_trc_on || !phba->nvmeio_trc)
		return;

	index = atomic_inc_return(&phba->nvmeio_trc_cnt) &
		(phba->nvmeio_trc_size - 1);
	dtp = phba->nvmeio_trc + index;
	dtp->fmt = fmt;
	dtp->data1 = data1;
	dtp->data2 = data2;
	dtp->data3 = data3;
#endif
}

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
/**
 * lpfc_debugfs_disc_trc_open - Open the discovery trace log
 * @inode: The inode pointer that contains a vport pointer.
 * @file: The file pointer to attach the log output.
 *
 * Description:
 * This routine is the entry point for the debugfs open file operation. It gets
 * the vport from the i_private field in @inode, allocates the necessary buffer
 * for the log, fills the buffer from the in-memory log for this vport, and then
 * returns a pointer to that log in the private_data field in @file.
 *
 * Returns:
 * This function returns zero if successful. On error it will return a negative
 * error value.
 **/
static int
lpfc_debugfs_disc_trc_open(struct inode *inode, struct file *file)
{
	struct lpfc_vport *vport = inode->i_private;
	struct lpfc_debug *debug;
	int size;
	int rc = -ENOMEM;

	if (!lpfc_debugfs_max_disc_trc) {
		rc = -ENOSPC;
		goto out;
	}

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundary */
	size =  (lpfc_debugfs_max_disc_trc * LPFC_DEBUG_TRC_ENTRY_SIZE);
	size = PAGE_ALIGN(size);

	debug->buffer = kmalloc(size, GFP_KERNEL);
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = lpfc_debugfs_disc_trc_data(vport, debug->buffer, size);
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

/**
 * lpfc_debugfs_slow_ring_trc_open - Open the Slow Ring trace log
 * @inode: The inode pointer that contains a vport pointer.
 * @file: The file pointer to attach the log output.
 *
 * Description:
 * This routine is the entry point for the debugfs open file operation. It gets
 * the vport from the i_private field in @inode, allocates the necessary buffer
 * for the log, fills the buffer from the in-memory log for this vport, and then
 * returns a pointer to that log in the private_data field in @file.
 *
 * Returns:
 * This function returns zero if successful. On error it will return a negative
 * error value.
 **/
static int
lpfc_debugfs_slow_ring_trc_open(struct inode *inode, struct file *file)
{
	struct lpfc_hba *phba = inode->i_private;
	struct lpfc_debug *debug;
	int size;
	int rc = -ENOMEM;

	if (!lpfc_debugfs_max_slow_ring_trc) {
		rc = -ENOSPC;
		goto out;
	}

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundary */
	size =  (lpfc_debugfs_max_slow_ring_trc * LPFC_DEBUG_TRC_ENTRY_SIZE);
	size = PAGE_ALIGN(size);

	debug->buffer = kmalloc(size, GFP_KERNEL);
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = lpfc_debugfs_slow_ring_trc_data(phba, debug->buffer, size);
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

/**
 * lpfc_debugfs_hbqinfo_open - Open the hbqinfo debugfs buffer
 * @inode: The inode pointer that contains a vport pointer.
 * @file: The file pointer to attach the log output.
 *
 * Description:
 * This routine is the entry point for the debugfs open file operation. It gets
 * the vport from the i_private field in @inode, allocates the necessary buffer
 * for the log, fills the buffer from the in-memory log for this vport, and then
 * returns a pointer to that log in the private_data field in @file.
 *
 * Returns:
 * This function returns zero if successful. On error it will return a negative
 * error value.
 **/
static int
lpfc_debugfs_hbqinfo_open(struct inode *inode, struct file *file)
{
	struct lpfc_hba *phba = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundary */
	debug->buffer = kmalloc(LPFC_HBQINFO_SIZE, GFP_KERNEL);
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = lpfc_debugfs_hbqinfo_data(phba, debug->buffer,
		LPFC_HBQINFO_SIZE);
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

/**
 * lpfc_debugfs_multixripools_open - Open the multixripool debugfs buffer
 * @inode: The inode pointer that contains a hba pointer.
 * @file: The file pointer to attach the log output.
 *
 * Description:
 * This routine is the entry point for the debugfs open file operation. It gets
 * the hba from the i_private field in @inode, allocates the necessary buffer
 * for the log, fills the buffer from the in-memory log for this hba, and then
 * returns a pointer to that log in the private_data field in @file.
 *
 * Returns:
 * This function returns zero if successful. On error it will return a negative
 * error value.
 **/
static int
lpfc_debugfs_multixripools_open(struct inode *inode, struct file *file)
{
	struct lpfc_hba *phba = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundary */
	debug->buffer = kzalloc(LPFC_DUMP_MULTIXRIPOOL_SIZE, GFP_KERNEL);
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = lpfc_debugfs_multixripools_data(
		phba, debug->buffer, LPFC_DUMP_MULTIXRIPOOL_SIZE);

	debug->i_private = inode->i_private;
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

#ifdef LPFC_HDWQ_LOCK_STAT
/**
 * lpfc_debugfs_lockstat_open - Open the lockstat debugfs buffer
 * @inode: The inode pointer that contains a vport pointer.
 * @file: The file pointer to attach the log output.
 *
 * Description:
 * This routine is the entry point for the debugfs open file operation. It gets
 * the vport from the i_private field in @inode, allocates the necessary buffer
 * for the log, fills the buffer from the in-memory log for this vport, and then
 * returns a pointer to that log in the private_data field in @file.
 *
 * Returns:
 * This function returns zero if successful. On error it will return a negative
 * error value.
 **/
static int
lpfc_debugfs_lockstat_open(struct inode *inode, struct file *file)
{
	struct lpfc_hba *phba = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundary */
	debug->buffer = kmalloc(LPFC_HDWQINFO_SIZE, GFP_KERNEL);
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = lpfc_debugfs_lockstat_data(phba, debug->buffer,
		LPFC_HBQINFO_SIZE);
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

static ssize_t
lpfc_debugfs_lockstat_write(struct file *file, const char __user *buf,
			    size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	struct lpfc_sli4_hdw_queue *qp;
	char mybuf[64];
	char *pbuf;
	int i;

	/* Protect copy from user */
	if (!access_ok(buf, nbytes))
		return -EFAULT;

	memset(mybuf, 0, sizeof(mybuf));

	if (copy_from_user(mybuf, buf, nbytes))
		return -EFAULT;
	pbuf = &mybuf[0];

	if ((strncmp(pbuf, "reset", strlen("reset")) == 0) ||
	    (strncmp(pbuf, "zero", strlen("zero")) == 0)) {
		for (i = 0; i < phba->cfg_hdw_queue; i++) {
			qp = &phba->sli4_hba.hdwq[i];
			qp->lock_conflict.alloc_xri_get = 0;
			qp->lock_conflict.alloc_xri_put = 0;
			qp->lock_conflict.free_xri = 0;
			qp->lock_conflict.wq_access = 0;
			qp->lock_conflict.alloc_pvt_pool = 0;
			qp->lock_conflict.mv_from_pvt_pool = 0;
			qp->lock_conflict.mv_to_pub_pool = 0;
			qp->lock_conflict.mv_to_pvt_pool = 0;
			qp->lock_conflict.free_pvt_pool = 0;
			qp->lock_conflict.free_pub_pool = 0;
			qp->lock_conflict.wq_access = 0;
		}
	}
	return nbytes;
}
#endif

/**
 * lpfc_debugfs_dumpHBASlim_open - Open the Dump HBA SLIM debugfs buffer
 * @inode: The inode pointer that contains a vport pointer.
 * @file: The file pointer to attach the log output.
 *
 * Description:
 * This routine is the entry point for the debugfs open file operation. It gets
 * the vport from the i_private field in @inode, allocates the necessary buffer
 * for the log, fills the buffer from the in-memory log for this vport, and then
 * returns a pointer to that log in the private_data field in @file.
 *
 * Returns:
 * This function returns zero if successful. On error it will return a negative
 * error value.
 **/
static int
lpfc_debugfs_dumpHBASlim_open(struct inode *inode, struct file *file)
{
	struct lpfc_hba *phba = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundary */
	debug->buffer = kmalloc(LPFC_DUMPHBASLIM_SIZE, GFP_KERNEL);
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = lpfc_debugfs_dumpHBASlim_data(phba, debug->buffer,
		LPFC_DUMPHBASLIM_SIZE);
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

/**
 * lpfc_debugfs_dumpHostSlim_open - Open the Dump Host SLIM debugfs buffer
 * @inode: The inode pointer that contains a vport pointer.
 * @file: The file pointer to attach the log output.
 *
 * Description:
 * This routine is the entry point for the debugfs open file operation. It gets
 * the vport from the i_private field in @inode, allocates the necessary buffer
 * for the log, fills the buffer from the in-memory log for this vport, and then
 * returns a pointer to that log in the private_data field in @file.
 *
 * Returns:
 * This function returns zero if successful. On error it will return a negative
 * error value.
 **/
static int
lpfc_debugfs_dumpHostSlim_open(struct inode *inode, struct file *file)
{
	struct lpfc_hba *phba = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundary */
	debug->buffer = kmalloc(LPFC_DUMPHOSTSLIM_SIZE, GFP_KERNEL);
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = lpfc_debugfs_dumpHostSlim_data(phba, debug->buffer,
		LPFC_DUMPHOSTSLIM_SIZE);
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

static int
lpfc_debugfs_dumpData_open(struct inode *inode, struct file *file)
{
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	if (!_dump_buf_data)
		return -EBUSY;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundary */
	pr_err("9059 BLKGRD:  %s: _dump_buf_data=0x%p\n",
			__func__, _dump_buf_data);
	debug->buffer = _dump_buf_data;
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = (1 << _dump_buf_data_order) << PAGE_SHIFT;
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

static int
lpfc_debugfs_dumpDif_open(struct inode *inode, struct file *file)
{
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	if (!_dump_buf_dif)
		return -EBUSY;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundary */
	pr_err("9060 BLKGRD: %s: _dump_buf_dif=x%px file=%pD\n",
			__func__, _dump_buf_dif, file);
	debug->buffer = _dump_buf_dif;
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = (1 << _dump_buf_dif_order) << PAGE_SHIFT;
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

static ssize_t
lpfc_debugfs_dumpDataDif_write(struct file *file, const char __user *buf,
		  size_t nbytes, loff_t *ppos)
{
	/*
	 * The Data/DIF buffers only save one failing IO
	 * The write op is used as a reset mechanism after an IO has
	 * already been saved to the next one can be saved
	 */
	spin_lock(&_dump_buf_lock);

	memset((void *)_dump_buf_data, 0,
			((1 << PAGE_SHIFT) << _dump_buf_data_order));
	memset((void *)_dump_buf_dif, 0,
			((1 << PAGE_SHIFT) << _dump_buf_dif_order));

	_dump_buf_done = 0;

	spin_unlock(&_dump_buf_lock);

	return nbytes;
}

static ssize_t
lpfc_debugfs_dif_err_read(struct file *file, char __user *buf,
	size_t nbytes, loff_t *ppos)
{
	struct dentry *dent = file->f_path.dentry;
	struct lpfc_hba *phba = file->private_data;
	char cbuf[32];
	uint64_t tmp = 0;
	int cnt = 0;

	if (dent == phba->debug_writeGuard)
		cnt = scnprintf(cbuf, 32, "%u\n", phba->lpfc_injerr_wgrd_cnt);
	else if (dent == phba->debug_writeApp)
		cnt = scnprintf(cbuf, 32, "%u\n", phba->lpfc_injerr_wapp_cnt);
	else if (dent == phba->debug_writeRef)
		cnt = scnprintf(cbuf, 32, "%u\n", phba->lpfc_injerr_wref_cnt);
	else if (dent == phba->debug_readGuard)
		cnt = scnprintf(cbuf, 32, "%u\n", phba->lpfc_injerr_rgrd_cnt);
	else if (dent == phba->debug_readApp)
		cnt = scnprintf(cbuf, 32, "%u\n", phba->lpfc_injerr_rapp_cnt);
	else if (dent == phba->debug_readRef)
		cnt = scnprintf(cbuf, 32, "%u\n", phba->lpfc_injerr_rref_cnt);
	else if (dent == phba->debug_InjErrNPortID)
		cnt = scnprintf(cbuf, 32, "0x%06x\n",
				phba->lpfc_injerr_nportid);
	else if (dent == phba->debug_InjErrWWPN) {
		memcpy(&tmp, &phba->lpfc_injerr_wwpn, sizeof(struct lpfc_name));
		tmp = cpu_to_be64(tmp);
		cnt = scnprintf(cbuf, 32, "0x%016llx\n", tmp);
	} else if (dent == phba->debug_InjErrLBA) {
		if (phba->lpfc_injerr_lba == (sector_t)(-1))
			cnt = scnprintf(cbuf, 32, "off\n");
		else
			cnt = scnprintf(cbuf, 32, "0x%llx\n",
				 (uint64_t) phba->lpfc_injerr_lba);
	} else
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			 "0547 Unknown debugfs error injection entry\n");

	return simple_read_from_buffer(buf, nbytes, ppos, &cbuf, cnt);
}

static ssize_t
lpfc_debugfs_dif_err_write(struct file *file, const char __user *buf,
	size_t nbytes, loff_t *ppos)
{
	struct dentry *dent = file->f_path.dentry;
	struct lpfc_hba *phba = file->private_data;
	char dstbuf[33];
	uint64_t tmp = 0;
	int size;

	memset(dstbuf, 0, 33);
	size = (nbytes < 32) ? nbytes : 32;
	if (copy_from_user(dstbuf, buf, size))
		return 0;

	if (dent == phba->debug_InjErrLBA) {
		if ((buf[0] == 'o') && (buf[1] == 'f') && (buf[2] == 'f'))
			tmp = (uint64_t)(-1);
	}

	if ((tmp == 0) && (kstrtoull(dstbuf, 0, &tmp)))
		return 0;

	if (dent == phba->debug_writeGuard)
		phba->lpfc_injerr_wgrd_cnt = (uint32_t)tmp;
	else if (dent == phba->debug_writeApp)
		phba->lpfc_injerr_wapp_cnt = (uint32_t)tmp;
	else if (dent == phba->debug_writeRef)
		phba->lpfc_injerr_wref_cnt = (uint32_t)tmp;
	else if (dent == phba->debug_readGuard)
		phba->lpfc_injerr_rgrd_cnt = (uint32_t)tmp;
	else if (dent == phba->debug_readApp)
		phba->lpfc_injerr_rapp_cnt = (uint32_t)tmp;
	else if (dent == phba->debug_readRef)
		phba->lpfc_injerr_rref_cnt = (uint32_t)tmp;
	else if (dent == phba->debug_InjErrLBA)
		phba->lpfc_injerr_lba = (sector_t)tmp;
	else if (dent == phba->debug_InjErrNPortID)
		phba->lpfc_injerr_nportid = (uint32_t)(tmp & Mask_DID);
	else if (dent == phba->debug_InjErrWWPN) {
		tmp = cpu_to_be64(tmp);
		memcpy(&phba->lpfc_injerr_wwpn, &tmp, sizeof(struct lpfc_name));
	} else
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			 "0548 Unknown debugfs error injection entry\n");

	return nbytes;
}

static int
lpfc_debugfs_dif_err_release(struct inode *inode, struct file *file)
{
	return 0;
}

/**
 * lpfc_debugfs_nodelist_open - Open the nodelist debugfs file
 * @inode: The inode pointer that contains a vport pointer.
 * @file: The file pointer to attach the log output.
 *
 * Description:
 * This routine is the entry point for the debugfs open file operation. It gets
 * the vport from the i_private field in @inode, allocates the necessary buffer
 * for the log, fills the buffer from the in-memory log for this vport, and then
 * returns a pointer to that log in the private_data field in @file.
 *
 * Returns:
 * This function returns zero if successful. On error it will return a negative
 * error value.
 **/
static int
lpfc_debugfs_nodelist_open(struct inode *inode, struct file *file)
{
	struct lpfc_vport *vport = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundary */
	debug->buffer = kmalloc(LPFC_NODELIST_SIZE, GFP_KERNEL);
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = lpfc_debugfs_nodelist_data(vport, debug->buffer,
		LPFC_NODELIST_SIZE);
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

/**
 * lpfc_debugfs_lseek - Seek through a debugfs file
 * @file: The file pointer to seek through.
 * @off: The offset to seek to or the amount to seek by.
 * @whence: Indicates how to seek.
 *
 * Description:
 * This routine is the entry point for the debugfs lseek file operation. The
 * @whence parameter indicates whether @off is the offset to directly seek to,
 * or if it is a value to seek forward or reverse by. This function figures out
 * what the new offset of the debugfs file will be and assigns that value to the
 * f_pos field of @file.
 *
 * Returns:
 * This function returns the new offset if successful and returns a negative
 * error if unable to process the seek.
 **/
static loff_t
lpfc_debugfs_lseek(struct file *file, loff_t off, int whence)
{
	struct lpfc_debug *debug = file->private_data;
	return fixed_size_llseek(file, off, whence, debug->len);
}

/**
 * lpfc_debugfs_read - Read a debugfs file
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the data to.
 * @nbytes: The number of bytes to read.
 * @ppos: The position in the file to start reading from.
 *
 * Description:
 * This routine reads data from from the buffer indicated in the private_data
 * field of @file. It will start reading at @ppos and copy up to @nbytes of
 * data to @buf.
 *
 * Returns:
 * This function returns the amount of data that was read (this could be less
 * than @nbytes if the end of the file was reached) or a negative error value.
 **/
static ssize_t
lpfc_debugfs_read(struct file *file, char __user *buf,
		  size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;

	return simple_read_from_buffer(buf, nbytes, ppos, debug->buffer,
				       debug->len);
}

/**
 * lpfc_debugfs_release - Release the buffer used to store debugfs file data
 * @inode: The inode pointer that contains a vport pointer. (unused)
 * @file: The file pointer that contains the buffer to release.
 *
 * Description:
 * This routine frees the buffer that was allocated when the debugfs file was
 * opened.
 *
 * Returns:
 * This function returns zero.
 **/
static int
lpfc_debugfs_release(struct inode *inode, struct file *file)
{
	struct lpfc_debug *debug = file->private_data;

	kfree(debug->buffer);
	kfree(debug);

	return 0;
}

static int
lpfc_debugfs_dumpDataDif_release(struct inode *inode, struct file *file)
{
	struct lpfc_debug *debug = file->private_data;

	debug->buffer = NULL;
	kfree(debug);

	return 0;
}

/**
 * lpfc_debugfs_multixripools_write - Clear multi-XRI pools statistics
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the user data from.
 * @nbytes: The number of bytes to get.
 * @ppos: The position in the file to start reading from.
 *
 * Description:
 * This routine clears multi-XRI pools statistics when buf contains "clear".
 *
 * Return Value:
 * It returns the @nbytges passing in from debugfs user space when successful.
 * In case of error conditions, it returns proper error code back to the user
 * space.
 **/
static ssize_t
lpfc_debugfs_multixripools_write(struct file *file, const char __user *buf,
				 size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	char mybuf[64];
	char *pbuf;
	u32 i;
	u32 hwq_count;
	struct lpfc_sli4_hdw_queue *qp;
	struct lpfc_multixri_pool *multixri_pool;

	if (nbytes > 64)
		nbytes = 64;

	/* Protect copy from user */
	if (!access_ok(buf, nbytes))
		return -EFAULT;

	memset(mybuf, 0, sizeof(mybuf));

	if (copy_from_user(mybuf, buf, nbytes))
		return -EFAULT;
	pbuf = &mybuf[0];

	if ((strncmp(pbuf, "clear", strlen("clear"))) == 0) {
		hwq_count = phba->cfg_hdw_queue;
		for (i = 0; i < hwq_count; i++) {
			qp = &phba->sli4_hba.hdwq[i];
			multixri_pool = qp->p_multixri_pool;
			if (!multixri_pool)
				continue;

			qp->empty_io_bufs = 0;
			multixri_pool->pbl_empty_count = 0;
#ifdef LPFC_MXP_STAT
			multixri_pool->above_limit_count = 0;
			multixri_pool->below_limit_count = 0;
			multixri_pool->stat_max_hwm = 0;
			multixri_pool->local_pbl_hit_count = 0;
			multixri_pool->other_pbl_hit_count = 0;

			multixri_pool->stat_pbl_count = 0;
			multixri_pool->stat_pvt_count = 0;
			multixri_pool->stat_busy_count = 0;
			multixri_pool->stat_snapshot_taken = 0;
#endif
		}
		return strlen(pbuf);
	}

	return -EINVAL;
}

static int
lpfc_debugfs_nvmestat_open(struct inode *inode, struct file *file)
{
	struct lpfc_vport *vport = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	 /* Round to page boundary */
	debug->buffer = kmalloc(LPFC_NVMESTAT_SIZE, GFP_KERNEL);
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = lpfc_debugfs_nvmestat_data(vport, debug->buffer,
		LPFC_NVMESTAT_SIZE);

	debug->i_private = inode->i_private;
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

static ssize_t
lpfc_debugfs_nvmestat_write(struct file *file, const char __user *buf,
			    size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_vport *vport = (struct lpfc_vport *)debug->i_private;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_nvmet_tgtport *tgtp;
	char mybuf[64];
	char *pbuf;

	if (!phba->targetport)
		return -ENXIO;

	if (nbytes > 64)
		nbytes = 64;

	memset(mybuf, 0, sizeof(mybuf));

	if (copy_from_user(mybuf, buf, nbytes))
		return -EFAULT;
	pbuf = &mybuf[0];

	tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
	if ((strncmp(pbuf, "reset", strlen("reset")) == 0) ||
	    (strncmp(pbuf, "zero", strlen("zero")) == 0)) {
		atomic_set(&tgtp->rcv_ls_req_in, 0);
		atomic_set(&tgtp->rcv_ls_req_out, 0);
		atomic_set(&tgtp->rcv_ls_req_drop, 0);
		atomic_set(&tgtp->xmt_ls_abort, 0);
		atomic_set(&tgtp->xmt_ls_abort_cmpl, 0);
		atomic_set(&tgtp->xmt_ls_rsp, 0);
		atomic_set(&tgtp->xmt_ls_drop, 0);
		atomic_set(&tgtp->xmt_ls_rsp_error, 0);
		atomic_set(&tgtp->xmt_ls_rsp_cmpl, 0);

		atomic_set(&tgtp->rcv_fcp_cmd_in, 0);
		atomic_set(&tgtp->rcv_fcp_cmd_out, 0);
		atomic_set(&tgtp->rcv_fcp_cmd_drop, 0);
		atomic_set(&tgtp->xmt_fcp_drop, 0);
		atomic_set(&tgtp->xmt_fcp_read_rsp, 0);
		atomic_set(&tgtp->xmt_fcp_read, 0);
		atomic_set(&tgtp->xmt_fcp_write, 0);
		atomic_set(&tgtp->xmt_fcp_rsp, 0);
		atomic_set(&tgtp->xmt_fcp_release, 0);
		atomic_set(&tgtp->xmt_fcp_rsp_cmpl, 0);
		atomic_set(&tgtp->xmt_fcp_rsp_error, 0);
		atomic_set(&tgtp->xmt_fcp_rsp_drop, 0);

		atomic_set(&tgtp->xmt_fcp_abort, 0);
		atomic_set(&tgtp->xmt_fcp_abort_cmpl, 0);
		atomic_set(&tgtp->xmt_abort_sol, 0);
		atomic_set(&tgtp->xmt_abort_unsol, 0);
		atomic_set(&tgtp->xmt_abort_rsp, 0);
		atomic_set(&tgtp->xmt_abort_rsp_error, 0);
	}
	return nbytes;
}

static int
lpfc_debugfs_scsistat_open(struct inode *inode, struct file *file)
{
	struct lpfc_vport *vport = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	 /* Round to page boundary */
	debug->buffer = kzalloc(LPFC_SCSISTAT_SIZE, GFP_KERNEL);
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = lpfc_debugfs_scsistat_data(vport, debug->buffer,
		LPFC_SCSISTAT_SIZE);

	debug->i_private = inode->i_private;
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

static ssize_t
lpfc_debugfs_scsistat_write(struct file *file, const char __user *buf,
			    size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_vport *vport = (struct lpfc_vport *)debug->i_private;
	struct lpfc_hba *phba = vport->phba;
	char mybuf[6] = {0};
	int i;

	/* Protect copy from user */
	if (!access_ok(buf, nbytes))
		return -EFAULT;

	if (copy_from_user(mybuf, buf, (nbytes >= sizeof(mybuf)) ?
				       (sizeof(mybuf) - 1) : nbytes))
		return -EFAULT;

	if ((strncmp(&mybuf[0], "reset", strlen("reset")) == 0) ||
	    (strncmp(&mybuf[0], "zero", strlen("zero")) == 0)) {
		for (i = 0; i < phba->cfg_hdw_queue; i++) {
			memset(&phba->sli4_hba.hdwq[i].scsi_cstat, 0,
			       sizeof(phba->sli4_hba.hdwq[i].scsi_cstat));
		}
	}

	return nbytes;
}

static int
lpfc_debugfs_nvmektime_open(struct inode *inode, struct file *file)
{
	struct lpfc_vport *vport = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	 /* Round to page boundary */
	debug->buffer = kmalloc(LPFC_NVMEKTIME_SIZE, GFP_KERNEL);
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = lpfc_debugfs_nvmektime_data(vport, debug->buffer,
		LPFC_NVMEKTIME_SIZE);

	debug->i_private = inode->i_private;
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

static ssize_t
lpfc_debugfs_nvmektime_write(struct file *file, const char __user *buf,
			     size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_vport *vport = (struct lpfc_vport *)debug->i_private;
	struct lpfc_hba   *phba = vport->phba;
	char mybuf[64];
	char *pbuf;

	if (nbytes > 64)
		nbytes = 64;

	memset(mybuf, 0, sizeof(mybuf));

	if (copy_from_user(mybuf, buf, nbytes))
		return -EFAULT;
	pbuf = &mybuf[0];

	if ((strncmp(pbuf, "on", sizeof("on") - 1) == 0)) {
		phba->ktime_data_samples = 0;
		phba->ktime_status_samples = 0;
		phba->ktime_seg1_total = 0;
		phba->ktime_seg1_max = 0;
		phba->ktime_seg1_min = 0xffffffff;
		phba->ktime_seg2_total = 0;
		phba->ktime_seg2_max = 0;
		phba->ktime_seg2_min = 0xffffffff;
		phba->ktime_seg3_total = 0;
		phba->ktime_seg3_max = 0;
		phba->ktime_seg3_min = 0xffffffff;
		phba->ktime_seg4_total = 0;
		phba->ktime_seg4_max = 0;
		phba->ktime_seg4_min = 0xffffffff;
		phba->ktime_seg5_total = 0;
		phba->ktime_seg5_max = 0;
		phba->ktime_seg5_min = 0xffffffff;
		phba->ktime_seg6_total = 0;
		phba->ktime_seg6_max = 0;
		phba->ktime_seg6_min = 0xffffffff;
		phba->ktime_seg7_total = 0;
		phba->ktime_seg7_max = 0;
		phba->ktime_seg7_min = 0xffffffff;
		phba->ktime_seg8_total = 0;
		phba->ktime_seg8_max = 0;
		phba->ktime_seg8_min = 0xffffffff;
		phba->ktime_seg9_total = 0;
		phba->ktime_seg9_max = 0;
		phba->ktime_seg9_min = 0xffffffff;
		phba->ktime_seg10_total = 0;
		phba->ktime_seg10_max = 0;
		phba->ktime_seg10_min = 0xffffffff;

		phba->ktime_on = 1;
		return strlen(pbuf);
	} else if ((strncmp(pbuf, "off",
		   sizeof("off") - 1) == 0)) {
		phba->ktime_on = 0;
		return strlen(pbuf);
	} else if ((strncmp(pbuf, "zero",
		   sizeof("zero") - 1) == 0)) {
		phba->ktime_data_samples = 0;
		phba->ktime_status_samples = 0;
		phba->ktime_seg1_total = 0;
		phba->ktime_seg1_max = 0;
		phba->ktime_seg1_min = 0xffffffff;
		phba->ktime_seg2_total = 0;
		phba->ktime_seg2_max = 0;
		phba->ktime_seg2_min = 0xffffffff;
		phba->ktime_seg3_total = 0;
		phba->ktime_seg3_max = 0;
		phba->ktime_seg3_min = 0xffffffff;
		phba->ktime_seg4_total = 0;
		phba->ktime_seg4_max = 0;
		phba->ktime_seg4_min = 0xffffffff;
		phba->ktime_seg5_total = 0;
		phba->ktime_seg5_max = 0;
		phba->ktime_seg5_min = 0xffffffff;
		phba->ktime_seg6_total = 0;
		phba->ktime_seg6_max = 0;
		phba->ktime_seg6_min = 0xffffffff;
		phba->ktime_seg7_total = 0;
		phba->ktime_seg7_max = 0;
		phba->ktime_seg7_min = 0xffffffff;
		phba->ktime_seg8_total = 0;
		phba->ktime_seg8_max = 0;
		phba->ktime_seg8_min = 0xffffffff;
		phba->ktime_seg9_total = 0;
		phba->ktime_seg9_max = 0;
		phba->ktime_seg9_min = 0xffffffff;
		phba->ktime_seg10_total = 0;
		phba->ktime_seg10_max = 0;
		phba->ktime_seg10_min = 0xffffffff;
		return strlen(pbuf);
	}
	return -EINVAL;
}

static int
lpfc_debugfs_nvmeio_trc_open(struct inode *inode, struct file *file)
{
	struct lpfc_hba *phba = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	 /* Round to page boundary */
	debug->buffer = kmalloc(LPFC_NVMEIO_TRC_SIZE, GFP_KERNEL);
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = lpfc_debugfs_nvmeio_trc_data(phba, debug->buffer,
		LPFC_NVMEIO_TRC_SIZE);

	debug->i_private = inode->i_private;
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

static ssize_t
lpfc_debugfs_nvmeio_trc_write(struct file *file, const char __user *buf,
			      size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	int i;
	unsigned long sz;
	char mybuf[64];
	char *pbuf;

	if (nbytes > 64)
		nbytes = 64;

	memset(mybuf, 0, sizeof(mybuf));

	if (copy_from_user(mybuf, buf, nbytes))
		return -EFAULT;
	pbuf = &mybuf[0];

	if ((strncmp(pbuf, "off", sizeof("off") - 1) == 0)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0570 nvmeio_trc_off\n");
		phba->nvmeio_trc_output_idx = 0;
		phba->nvmeio_trc_on = 0;
		return strlen(pbuf);
	} else if ((strncmp(pbuf, "on", sizeof("on") - 1) == 0)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0571 nvmeio_trc_on\n");
		phba->nvmeio_trc_output_idx = 0;
		phba->nvmeio_trc_on = 1;
		return strlen(pbuf);
	}

	/* We must be off to allocate the trace buffer */
	if (phba->nvmeio_trc_on != 0)
		return -EINVAL;

	/* If not on or off, the parameter is the trace buffer size */
	i = kstrtoul(pbuf, 0, &sz);
	if (i)
		return -EINVAL;
	phba->nvmeio_trc_size = (uint32_t)sz;

	/* It must be a power of 2 - round down */
	i = 0;
	while (sz > 1) {
		sz = sz >> 1;
		i++;
	}
	sz = (1 << i);
	if (phba->nvmeio_trc_size != sz)
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0572 nvmeio_trc_size changed to %ld\n",
				sz);
	phba->nvmeio_trc_size = (uint32_t)sz;

	/* If one previously exists, free it */
	kfree(phba->nvmeio_trc);

	/* Allocate new trace buffer and initialize */
	phba->nvmeio_trc = kzalloc((sizeof(struct lpfc_debugfs_nvmeio_trc) *
				    sz), GFP_KERNEL);
	if (!phba->nvmeio_trc) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0573 Cannot create debugfs "
				"nvmeio_trc buffer\n");
		return -ENOMEM;
	}
	atomic_set(&phba->nvmeio_trc_cnt, 0);
	phba->nvmeio_trc_on = 0;
	phba->nvmeio_trc_output_idx = 0;

	return strlen(pbuf);
}

static int
lpfc_debugfs_cpucheck_open(struct inode *inode, struct file *file)
{
	struct lpfc_vport *vport = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	 /* Round to page boundary */
	debug->buffer = kmalloc(LPFC_CPUCHECK_SIZE, GFP_KERNEL);
	if (!debug->buffer) {
		kfree(debug);
		goto out;
	}

	debug->len = lpfc_debugfs_cpucheck_data(vport, debug->buffer,
		LPFC_CPUCHECK_SIZE);

	debug->i_private = inode->i_private;
	file->private_data = debug;

	rc = 0;
out:
	return rc;
}

static ssize_t
lpfc_debugfs_cpucheck_write(struct file *file, const char __user *buf,
			    size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_vport *vport = (struct lpfc_vport *)debug->i_private;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_sli4_hdw_queue *qp;
	char mybuf[64];
	char *pbuf;
	int i, j;

	if (nbytes > 64)
		nbytes = 64;

	memset(mybuf, 0, sizeof(mybuf));

	if (copy_from_user(mybuf, buf, nbytes))
		return -EFAULT;
	pbuf = &mybuf[0];

	if ((strncmp(pbuf, "on", sizeof("on") - 1) == 0)) {
		if (phba->nvmet_support)
			phba->cpucheck_on |= LPFC_CHECK_NVMET_IO;
		else
			phba->cpucheck_on |= (LPFC_CHECK_NVME_IO |
				LPFC_CHECK_SCSI_IO);
		return strlen(pbuf);
	} else if ((strncmp(pbuf, "nvme_on", sizeof("nvme_on") - 1) == 0)) {
		if (phba->nvmet_support)
			phba->cpucheck_on |= LPFC_CHECK_NVMET_IO;
		else
			phba->cpucheck_on |= LPFC_CHECK_NVME_IO;
		return strlen(pbuf);
	} else if ((strncmp(pbuf, "scsi_on", sizeof("scsi_on") - 1) == 0)) {
		phba->cpucheck_on |= LPFC_CHECK_SCSI_IO;
		return strlen(pbuf);
	} else if ((strncmp(pbuf, "rcv",
		   sizeof("rcv") - 1) == 0)) {
		if (phba->nvmet_support)
			phba->cpucheck_on |= LPFC_CHECK_NVMET_RCV;
		else
			return -EINVAL;
		return strlen(pbuf);
	} else if ((strncmp(pbuf, "off",
		   sizeof("off") - 1) == 0)) {
		phba->cpucheck_on = LPFC_CHECK_OFF;
		return strlen(pbuf);
	} else if ((strncmp(pbuf, "zero",
		   sizeof("zero") - 1) == 0)) {
		for (i = 0; i < phba->cfg_hdw_queue; i++) {
			qp = &phba->sli4_hba.hdwq[i];

			for (j = 0; j < LPFC_CHECK_CPU_CNT; j++) {
				qp->cpucheck_rcv_io[j] = 0;
				qp->cpucheck_xmt_io[j] = 0;
				qp->cpucheck_cmpl_io[j] = 0;
			}
		}
		return strlen(pbuf);
	}
	return -EINVAL;
}

/*
 * ---------------------------------
 * iDiag debugfs file access methods
 * ---------------------------------
 *
 * All access methods are through the proper SLI4 PCI function's debugfs
 * iDiag directory:
 *
 *     /sys/kernel/debug/lpfc/fn<#>/iDiag
 */

/**
 * lpfc_idiag_cmd_get - Get and parse idiag debugfs comands from user space
 * @buf: The pointer to the user space buffer.
 * @nbytes: The number of bytes in the user space buffer.
 * @idiag_cmd: pointer to the idiag command struct.
 *
 * This routine reads data from debugfs user space buffer and parses the
 * buffer for getting the idiag command and arguments. The while space in
 * between the set of data is used as the parsing separator.
 *
 * This routine returns 0 when successful, it returns proper error code
 * back to the user space in error conditions.
 */
static int lpfc_idiag_cmd_get(const char __user *buf, size_t nbytes,
			      struct lpfc_idiag_cmd *idiag_cmd)
{
	char mybuf[64];
	char *pbuf, *step_str;
	int i;
	size_t bsize;

	memset(mybuf, 0, sizeof(mybuf));
	memset(idiag_cmd, 0, sizeof(*idiag_cmd));
	bsize = min(nbytes, (sizeof(mybuf)-1));

	if (copy_from_user(mybuf, buf, bsize))
		return -EFAULT;
	pbuf = &mybuf[0];
	step_str = strsep(&pbuf, "\t ");

	/* The opcode must present */
	if (!step_str)
		return -EINVAL;

	idiag_cmd->opcode = simple_strtol(step_str, NULL, 0);
	if (idiag_cmd->opcode == 0)
		return -EINVAL;

	for (i = 0; i < LPFC_IDIAG_CMD_DATA_SIZE; i++) {
		step_str = strsep(&pbuf, "\t ");
		if (!step_str)
			return i;
		idiag_cmd->data[i] = simple_strtol(step_str, NULL, 0);
	}
	return i;
}

/**
 * lpfc_idiag_open - idiag open debugfs
 * @inode: The inode pointer that contains a pointer to phba.
 * @file: The file pointer to attach the file operation.
 *
 * Description:
 * This routine is the entry point for the debugfs open file operation. It
 * gets the reference to phba from the i_private field in @inode, it then
 * allocates buffer for the file operation, performs the necessary PCI config
 * space read into the allocated buffer according to the idiag user command
 * setup, and then returns a pointer to buffer in the private_data field in
 * @file.
 *
 * Returns:
 * This function returns zero if successful. On error it will return an
 * negative error value.
 **/
static int
lpfc_idiag_open(struct inode *inode, struct file *file)
{
	struct lpfc_debug *debug;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		return -ENOMEM;

	debug->i_private = inode->i_private;
	debug->buffer = NULL;
	file->private_data = debug;

	return 0;
}

/**
 * lpfc_idiag_release - Release idiag access file operation
 * @inode: The inode pointer that contains a vport pointer. (unused)
 * @file: The file pointer that contains the buffer to release.
 *
 * Description:
 * This routine is the generic release routine for the idiag access file
 * operation, it frees the buffer that was allocated when the debugfs file
 * was opened.
 *
 * Returns:
 * This function returns zero.
 **/
static int
lpfc_idiag_release(struct inode *inode, struct file *file)
{
	struct lpfc_debug *debug = file->private_data;

	/* Free the buffers to the file operation */
	kfree(debug->buffer);
	kfree(debug);

	return 0;
}

/**
 * lpfc_idiag_cmd_release - Release idiag cmd access file operation
 * @inode: The inode pointer that contains a vport pointer. (unused)
 * @file: The file pointer that contains the buffer to release.
 *
 * Description:
 * This routine frees the buffer that was allocated when the debugfs file
 * was opened. It also reset the fields in the idiag command struct in the
 * case of command for write operation.
 *
 * Returns:
 * This function returns zero.
 **/
static int
lpfc_idiag_cmd_release(struct inode *inode, struct file *file)
{
	struct lpfc_debug *debug = file->private_data;

	if (debug->op == LPFC_IDIAG_OP_WR) {
		switch (idiag.cmd.opcode) {
		case LPFC_IDIAG_CMD_PCICFG_WR:
		case LPFC_IDIAG_CMD_PCICFG_ST:
		case LPFC_IDIAG_CMD_PCICFG_CL:
		case LPFC_IDIAG_CMD_QUEACC_WR:
		case LPFC_IDIAG_CMD_QUEACC_ST:
		case LPFC_IDIAG_CMD_QUEACC_CL:
			memset(&idiag, 0, sizeof(idiag));
			break;
		default:
			break;
		}
	}

	/* Free the buffers to the file operation */
	kfree(debug->buffer);
	kfree(debug);

	return 0;
}

/**
 * lpfc_idiag_pcicfg_read - idiag debugfs read pcicfg
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the data to.
 * @nbytes: The number of bytes to read.
 * @ppos: The position in the file to start reading from.
 *
 * Description:
 * This routine reads data from the @phba pci config space according to the
 * idiag command, and copies to user @buf. Depending on the PCI config space
 * read command setup, it does either a single register read of a byte
 * (8 bits), a word (16 bits), or a dword (32 bits) or browsing through all
 * registers from the 4K extended PCI config space.
 *
 * Returns:
 * This function returns the amount of data that was read (this could be less
 * than @nbytes if the end of the file was reached) or a negative error value.
 **/
static ssize_t
lpfc_idiag_pcicfg_read(struct file *file, char __user *buf, size_t nbytes,
		       loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	int offset_label, offset, len = 0, index = LPFC_PCI_CFG_RD_SIZE;
	int where, count;
	char *pbuffer;
	struct pci_dev *pdev;
	uint32_t u32val;
	uint16_t u16val;
	uint8_t u8val;

	pdev = phba->pcidev;
	if (!pdev)
		return 0;

	/* This is a user read operation */
	debug->op = LPFC_IDIAG_OP_RD;

	if (!debug->buffer)
		debug->buffer = kmalloc(LPFC_PCI_CFG_SIZE, GFP_KERNEL);
	if (!debug->buffer)
		return 0;
	pbuffer = debug->buffer;

	if (*ppos)
		return 0;

	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_RD) {
		where = idiag.cmd.data[IDIAG_PCICFG_WHERE_INDX];
		count = idiag.cmd.data[IDIAG_PCICFG_COUNT_INDX];
	} else
		return 0;

	/* Read single PCI config space register */
	switch (count) {
	case SIZE_U8: /* byte (8 bits) */
		pci_read_config_byte(pdev, where, &u8val);
		len += scnprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
				"%03x: %02x\n", where, u8val);
		break;
	case SIZE_U16: /* word (16 bits) */
		pci_read_config_word(pdev, where, &u16val);
		len += scnprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
				"%03x: %04x\n", where, u16val);
		break;
	case SIZE_U32: /* double word (32 bits) */
		pci_read_config_dword(pdev, where, &u32val);
		len += scnprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
				"%03x: %08x\n", where, u32val);
		break;
	case LPFC_PCI_CFG_BROWSE: /* browse all */
		goto pcicfg_browse;
		break;
	default:
		/* illegal count */
		len = 0;
		break;
	}
	return simple_read_from_buffer(buf, nbytes, ppos, pbuffer, len);

pcicfg_browse:

	/* Browse all PCI config space registers */
	offset_label = idiag.offset.last_rd;
	offset = offset_label;

	/* Read PCI config space */
	len += scnprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
			"%03x: ", offset_label);
	while (index > 0) {
		pci_read_config_dword(pdev, offset, &u32val);
		len += scnprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
				"%08x ", u32val);
		offset += sizeof(uint32_t);
		if (offset >= LPFC_PCI_CFG_SIZE) {
			len += scnprintf(pbuffer+len,
					LPFC_PCI_CFG_SIZE-len, "\n");
			break;
		}
		index -= sizeof(uint32_t);
		if (!index)
			len += scnprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
					"\n");
		else if (!(index % (8 * sizeof(uint32_t)))) {
			offset_label += (8 * sizeof(uint32_t));
			len += scnprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
					"\n%03x: ", offset_label);
		}
	}

	/* Set up the offset for next portion of pci cfg read */
	if (index == 0) {
		idiag.offset.last_rd += LPFC_PCI_CFG_RD_SIZE;
		if (idiag.offset.last_rd >= LPFC_PCI_CFG_SIZE)
			idiag.offset.last_rd = 0;
	} else
		idiag.offset.last_rd = 0;

	return simple_read_from_buffer(buf, nbytes, ppos, pbuffer, len);
}

/**
 * lpfc_idiag_pcicfg_write - Syntax check and set up idiag pcicfg commands
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the user data from.
 * @nbytes: The number of bytes to get.
 * @ppos: The position in the file to start reading from.
 *
 * This routine get the debugfs idiag command struct from user space and
 * then perform the syntax check for PCI config space read or write command
 * accordingly. In the case of PCI config space read command, it sets up
 * the command in the idiag command struct for the debugfs read operation.
 * In the case of PCI config space write operation, it executes the write
 * operation into the PCI config space accordingly.
 *
 * It returns the @nbytges passing in from debugfs user space when successful.
 * In case of error conditions, it returns proper error code back to the user
 * space.
 */
static ssize_t
lpfc_idiag_pcicfg_write(struct file *file, const char __user *buf,
			size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	uint32_t where, value, count;
	uint32_t u32val;
	uint16_t u16val;
	uint8_t u8val;
	struct pci_dev *pdev;
	int rc;

	pdev = phba->pcidev;
	if (!pdev)
		return -EFAULT;

	/* This is a user write operation */
	debug->op = LPFC_IDIAG_OP_WR;

	rc = lpfc_idiag_cmd_get(buf, nbytes, &idiag.cmd);
	if (rc < 0)
		return rc;

	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_RD) {
		/* Sanity check on PCI config read command line arguments */
		if (rc != LPFC_PCI_CFG_RD_CMD_ARG)
			goto error_out;
		/* Read command from PCI config space, set up command fields */
		where = idiag.cmd.data[IDIAG_PCICFG_WHERE_INDX];
		count = idiag.cmd.data[IDIAG_PCICFG_COUNT_INDX];
		if (count == LPFC_PCI_CFG_BROWSE) {
			if (where % sizeof(uint32_t))
				goto error_out;
			/* Starting offset to browse */
			idiag.offset.last_rd = where;
		} else if ((count != sizeof(uint8_t)) &&
			   (count != sizeof(uint16_t)) &&
			   (count != sizeof(uint32_t)))
			goto error_out;
		if (count == sizeof(uint8_t)) {
			if (where > LPFC_PCI_CFG_SIZE - sizeof(uint8_t))
				goto error_out;
			if (where % sizeof(uint8_t))
				goto error_out;
		}
		if (count == sizeof(uint16_t)) {
			if (where > LPFC_PCI_CFG_SIZE - sizeof(uint16_t))
				goto error_out;
			if (where % sizeof(uint16_t))
				goto error_out;
		}
		if (count == sizeof(uint32_t)) {
			if (where > LPFC_PCI_CFG_SIZE - sizeof(uint32_t))
				goto error_out;
			if (where % sizeof(uint32_t))
				goto error_out;
		}
	} else if (idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_WR ||
		   idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_ST ||
		   idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_CL) {
		/* Sanity check on PCI config write command line arguments */
		if (rc != LPFC_PCI_CFG_WR_CMD_ARG)
			goto error_out;
		/* Write command to PCI config space, read-modify-write */
		where = idiag.cmd.data[IDIAG_PCICFG_WHERE_INDX];
		count = idiag.cmd.data[IDIAG_PCICFG_COUNT_INDX];
		value = idiag.cmd.data[IDIAG_PCICFG_VALUE_INDX];
		/* Sanity checks */
		if ((count != sizeof(uint8_t)) &&
		    (count != sizeof(uint16_t)) &&
		    (count != sizeof(uint32_t)))
			goto error_out;
		if (count == sizeof(uint8_t)) {
			if (where > LPFC_PCI_CFG_SIZE - sizeof(uint8_t))
				goto error_out;
			if (where % sizeof(uint8_t))
				goto error_out;
			if (idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_WR)
				pci_write_config_byte(pdev, where,
						      (uint8_t)value);
			if (idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_ST) {
				rc = pci_read_config_byte(pdev, where, &u8val);
				if (!rc) {
					u8val |= (uint8_t)value;
					pci_write_config_byte(pdev, where,
							      u8val);
				}
			}
			if (idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_CL) {
				rc = pci_read_config_byte(pdev, where, &u8val);
				if (!rc) {
					u8val &= (uint8_t)(~value);
					pci_write_config_byte(pdev, where,
							      u8val);
				}
			}
		}
		if (count == sizeof(uint16_t)) {
			if (where > LPFC_PCI_CFG_SIZE - sizeof(uint16_t))
				goto error_out;
			if (where % sizeof(uint16_t))
				goto error_out;
			if (idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_WR)
				pci_write_config_word(pdev, where,
						      (uint16_t)value);
			if (idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_ST) {
				rc = pci_read_config_word(pdev, where, &u16val);
				if (!rc) {
					u16val |= (uint16_t)value;
					pci_write_config_word(pdev, where,
							      u16val);
				}
			}
			if (idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_CL) {
				rc = pci_read_config_word(pdev, where, &u16val);
				if (!rc) {
					u16val &= (uint16_t)(~value);
					pci_write_config_word(pdev, where,
							      u16val);
				}
			}
		}
		if (count == sizeof(uint32_t)) {
			if (where > LPFC_PCI_CFG_SIZE - sizeof(uint32_t))
				goto error_out;
			if (where % sizeof(uint32_t))
				goto error_out;
			if (idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_WR)
				pci_write_config_dword(pdev, where, value);
			if (idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_ST) {
				rc = pci_read_config_dword(pdev, where,
							   &u32val);
				if (!rc) {
					u32val |= value;
					pci_write_config_dword(pdev, where,
							       u32val);
				}
			}
			if (idiag.cmd.opcode == LPFC_IDIAG_CMD_PCICFG_CL) {
				rc = pci_read_config_dword(pdev, where,
							   &u32val);
				if (!rc) {
					u32val &= ~value;
					pci_write_config_dword(pdev, where,
							       u32val);
				}
			}
		}
	} else
		/* All other opecodes are illegal for now */
		goto error_out;

	return nbytes;
error_out:
	memset(&idiag, 0, sizeof(idiag));
	return -EINVAL;
}

/**
 * lpfc_idiag_baracc_read - idiag debugfs pci bar access read
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the data to.
 * @nbytes: The number of bytes to read.
 * @ppos: The position in the file to start reading from.
 *
 * Description:
 * This routine reads data from the @phba pci bar memory mapped space
 * according to the idiag command, and copies to user @buf.
 *
 * Returns:
 * This function returns the amount of data that was read (this could be less
 * than @nbytes if the end of the file was reached) or a negative error value.
 **/
static ssize_t
lpfc_idiag_baracc_read(struct file *file, char __user *buf, size_t nbytes,
		       loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	int offset_label, offset, offset_run, len = 0, index;
	int bar_num, acc_range, bar_size;
	char *pbuffer;
	void __iomem *mem_mapped_bar;
	uint32_t if_type;
	struct pci_dev *pdev;
	uint32_t u32val;

	pdev = phba->pcidev;
	if (!pdev)
		return 0;

	/* This is a user read operation */
	debug->op = LPFC_IDIAG_OP_RD;

	if (!debug->buffer)
		debug->buffer = kmalloc(LPFC_PCI_BAR_RD_BUF_SIZE, GFP_KERNEL);
	if (!debug->buffer)
		return 0;
	pbuffer = debug->buffer;

	if (*ppos)
		return 0;

	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_BARACC_RD) {
		bar_num   = idiag.cmd.data[IDIAG_BARACC_BAR_NUM_INDX];
		offset    = idiag.cmd.data[IDIAG_BARACC_OFF_SET_INDX];
		acc_range = idiag.cmd.data[IDIAG_BARACC_ACC_MOD_INDX];
		bar_size = idiag.cmd.data[IDIAG_BARACC_BAR_SZE_INDX];
	} else
		return 0;

	if (acc_range == 0)
		return 0;

	if_type = bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf);
	if (if_type == LPFC_SLI_INTF_IF_TYPE_0) {
		if (bar_num == IDIAG_BARACC_BAR_0)
			mem_mapped_bar = phba->sli4_hba.conf_regs_memmap_p;
		else if (bar_num == IDIAG_BARACC_BAR_1)
			mem_mapped_bar = phba->sli4_hba.ctrl_regs_memmap_p;
		else if (bar_num == IDIAG_BARACC_BAR_2)
			mem_mapped_bar = phba->sli4_hba.drbl_regs_memmap_p;
		else
			return 0;
	} else if (if_type == LPFC_SLI_INTF_IF_TYPE_2) {
		if (bar_num == IDIAG_BARACC_BAR_0)
			mem_mapped_bar = phba->sli4_hba.conf_regs_memmap_p;
		else
			return 0;
	} else
		return 0;

	/* Read single PCI bar space register */
	if (acc_range == SINGLE_WORD) {
		offset_run = offset;
		u32val = readl(mem_mapped_bar + offset_run);
		len += scnprintf(pbuffer+len, LPFC_PCI_BAR_RD_BUF_SIZE-len,
				"%05x: %08x\n", offset_run, u32val);
	} else
		goto baracc_browse;

	return simple_read_from_buffer(buf, nbytes, ppos, pbuffer, len);

baracc_browse:

	/* Browse all PCI bar space registers */
	offset_label = idiag.offset.last_rd;
	offset_run = offset_label;

	/* Read PCI bar memory mapped space */
	len += scnprintf(pbuffer+len, LPFC_PCI_BAR_RD_BUF_SIZE-len,
			"%05x: ", offset_label);
	index = LPFC_PCI_BAR_RD_SIZE;
	while (index > 0) {
		u32val = readl(mem_mapped_bar + offset_run);
		len += scnprintf(pbuffer+len, LPFC_PCI_BAR_RD_BUF_SIZE-len,
				"%08x ", u32val);
		offset_run += sizeof(uint32_t);
		if (acc_range == LPFC_PCI_BAR_BROWSE) {
			if (offset_run >= bar_size) {
				len += scnprintf(pbuffer+len,
					LPFC_PCI_BAR_RD_BUF_SIZE-len, "\n");
				break;
			}
		} else {
			if (offset_run >= offset +
			    (acc_range * sizeof(uint32_t))) {
				len += scnprintf(pbuffer+len,
					LPFC_PCI_BAR_RD_BUF_SIZE-len, "\n");
				break;
			}
		}
		index -= sizeof(uint32_t);
		if (!index)
			len += scnprintf(pbuffer+len,
					LPFC_PCI_BAR_RD_BUF_SIZE-len, "\n");
		else if (!(index % (8 * sizeof(uint32_t)))) {
			offset_label += (8 * sizeof(uint32_t));
			len += scnprintf(pbuffer+len,
					LPFC_PCI_BAR_RD_BUF_SIZE-len,
					"\n%05x: ", offset_label);
		}
	}

	/* Set up the offset for next portion of pci bar read */
	if (index == 0) {
		idiag.offset.last_rd += LPFC_PCI_BAR_RD_SIZE;
		if (acc_range == LPFC_PCI_BAR_BROWSE) {
			if (idiag.offset.last_rd >= bar_size)
				idiag.offset.last_rd = 0;
		} else {
			if (offset_run >= offset +
			    (acc_range * sizeof(uint32_t)))
				idiag.offset.last_rd = offset;
		}
	} else {
		if (acc_range == LPFC_PCI_BAR_BROWSE)
			idiag.offset.last_rd = 0;
		else
			idiag.offset.last_rd = offset;
	}

	return simple_read_from_buffer(buf, nbytes, ppos, pbuffer, len);
}

/**
 * lpfc_idiag_baracc_write - Syntax check and set up idiag bar access commands
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the user data from.
 * @nbytes: The number of bytes to get.
 * @ppos: The position in the file to start reading from.
 *
 * This routine get the debugfs idiag command struct from user space and
 * then perform the syntax check for PCI bar memory mapped space read or
 * write command accordingly. In the case of PCI bar memory mapped space
 * read command, it sets up the command in the idiag command struct for
 * the debugfs read operation. In the case of PCI bar memorpy mapped space
 * write operation, it executes the write operation into the PCI bar memory
 * mapped space accordingly.
 *
 * It returns the @nbytges passing in from debugfs user space when successful.
 * In case of error conditions, it returns proper error code back to the user
 * space.
 */
static ssize_t
lpfc_idiag_baracc_write(struct file *file, const char __user *buf,
			size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	uint32_t bar_num, bar_size, offset, value, acc_range;
	struct pci_dev *pdev;
	void __iomem *mem_mapped_bar;
	uint32_t if_type;
	uint32_t u32val;
	int rc;

	pdev = phba->pcidev;
	if (!pdev)
		return -EFAULT;

	/* This is a user write operation */
	debug->op = LPFC_IDIAG_OP_WR;

	rc = lpfc_idiag_cmd_get(buf, nbytes, &idiag.cmd);
	if (rc < 0)
		return rc;

	if_type = bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf);
	bar_num = idiag.cmd.data[IDIAG_BARACC_BAR_NUM_INDX];

	if (if_type == LPFC_SLI_INTF_IF_TYPE_0) {
		if ((bar_num != IDIAG_BARACC_BAR_0) &&
		    (bar_num != IDIAG_BARACC_BAR_1) &&
		    (bar_num != IDIAG_BARACC_BAR_2))
			goto error_out;
	} else if (if_type == LPFC_SLI_INTF_IF_TYPE_2) {
		if (bar_num != IDIAG_BARACC_BAR_0)
			goto error_out;
	} else
		goto error_out;

	if (if_type == LPFC_SLI_INTF_IF_TYPE_0) {
		if (bar_num == IDIAG_BARACC_BAR_0) {
			idiag.cmd.data[IDIAG_BARACC_BAR_SZE_INDX] =
				LPFC_PCI_IF0_BAR0_SIZE;
			mem_mapped_bar = phba->sli4_hba.conf_regs_memmap_p;
		} else if (bar_num == IDIAG_BARACC_BAR_1) {
			idiag.cmd.data[IDIAG_BARACC_BAR_SZE_INDX] =
				LPFC_PCI_IF0_BAR1_SIZE;
			mem_mapped_bar = phba->sli4_hba.ctrl_regs_memmap_p;
		} else if (bar_num == IDIAG_BARACC_BAR_2) {
			idiag.cmd.data[IDIAG_BARACC_BAR_SZE_INDX] =
				LPFC_PCI_IF0_BAR2_SIZE;
			mem_mapped_bar = phba->sli4_hba.drbl_regs_memmap_p;
		} else
			goto error_out;
	} else if (if_type == LPFC_SLI_INTF_IF_TYPE_2) {
		if (bar_num == IDIAG_BARACC_BAR_0) {
			idiag.cmd.data[IDIAG_BARACC_BAR_SZE_INDX] =
				LPFC_PCI_IF2_BAR0_SIZE;
			mem_mapped_bar = phba->sli4_hba.conf_regs_memmap_p;
		} else
			goto error_out;
	} else
		goto error_out;

	offset = idiag.cmd.data[IDIAG_BARACC_OFF_SET_INDX];
	if (offset % sizeof(uint32_t))
		goto error_out;

	bar_size = idiag.cmd.data[IDIAG_BARACC_BAR_SZE_INDX];
	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_BARACC_RD) {
		/* Sanity check on PCI config read command line arguments */
		if (rc != LPFC_PCI_BAR_RD_CMD_ARG)
			goto error_out;
		acc_range = idiag.cmd.data[IDIAG_BARACC_ACC_MOD_INDX];
		if (acc_range == LPFC_PCI_BAR_BROWSE) {
			if (offset > bar_size - sizeof(uint32_t))
				goto error_out;
			/* Starting offset to browse */
			idiag.offset.last_rd = offset;
		} else if (acc_range > SINGLE_WORD) {
			if (offset + acc_range * sizeof(uint32_t) > bar_size)
				goto error_out;
			/* Starting offset to browse */
			idiag.offset.last_rd = offset;
		} else if (acc_range != SINGLE_WORD)
			goto error_out;
	} else if (idiag.cmd.opcode == LPFC_IDIAG_CMD_BARACC_WR ||
		   idiag.cmd.opcode == LPFC_IDIAG_CMD_BARACC_ST ||
		   idiag.cmd.opcode == LPFC_IDIAG_CMD_BARACC_CL) {
		/* Sanity check on PCI bar write command line arguments */
		if (rc != LPFC_PCI_BAR_WR_CMD_ARG)
			goto error_out;
		/* Write command to PCI bar space, read-modify-write */
		acc_range = SINGLE_WORD;
		value = idiag.cmd.data[IDIAG_BARACC_REG_VAL_INDX];
		if (idiag.cmd.opcode == LPFC_IDIAG_CMD_BARACC_WR) {
			writel(value, mem_mapped_bar + offset);
			readl(mem_mapped_bar + offset);
		}
		if (idiag.cmd.opcode == LPFC_IDIAG_CMD_BARACC_ST) {
			u32val = readl(mem_mapped_bar + offset);
			u32val |= value;
			writel(u32val, mem_mapped_bar + offset);
			readl(mem_mapped_bar + offset);
		}
		if (idiag.cmd.opcode == LPFC_IDIAG_CMD_BARACC_CL) {
			u32val = readl(mem_mapped_bar + offset);
			u32val &= ~value;
			writel(u32val, mem_mapped_bar + offset);
			readl(mem_mapped_bar + offset);
		}
	} else
		/* All other opecodes are illegal for now */
		goto error_out;

	return nbytes;
error_out:
	memset(&idiag, 0, sizeof(idiag));
	return -EINVAL;
}

static int
__lpfc_idiag_print_wq(struct lpfc_queue *qp, char *wqtype,
			char *pbuffer, int len)
{
	if (!qp)
		return len;

	len += scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"\t\t%s WQ info: ", wqtype);
	len += scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"AssocCQID[%04d]: WQ-STAT[oflow:x%x posted:x%llx]\n",
			qp->assoc_qid, qp->q_cnt_1,
			(unsigned long long)qp->q_cnt_4);
	len += scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"\t\tWQID[%02d], QE-CNT[%04d], QE-SZ[%04d], "
			"HST-IDX[%04d], PRT-IDX[%04d], NTFI[%03d]",
			qp->queue_id, qp->entry_count,
			qp->entry_size, qp->host_index,
			qp->hba_index, qp->notify_interval);
	len +=  scnprintf(pbuffer + len,
			LPFC_QUE_INFO_GET_BUF_SIZE - len, "\n");
	return len;
}

static int
lpfc_idiag_wqs_for_cq(struct lpfc_hba *phba, char *wqtype, char *pbuffer,
		int *len, int max_cnt, int cq_id)
{
	struct lpfc_queue *qp;
	int qidx;

	for (qidx = 0; qidx < phba->cfg_hdw_queue; qidx++) {
		qp = phba->sli4_hba.hdwq[qidx].fcp_wq;
		if (qp->assoc_qid != cq_id)
			continue;
		*len = __lpfc_idiag_print_wq(qp, wqtype, pbuffer, *len);
		if (*len >= max_cnt)
			return 1;
	}
	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
		for (qidx = 0; qidx < phba->cfg_hdw_queue; qidx++) {
			qp = phba->sli4_hba.hdwq[qidx].nvme_wq;
			if (qp->assoc_qid != cq_id)
				continue;
			*len = __lpfc_idiag_print_wq(qp, wqtype, pbuffer, *len);
			if (*len >= max_cnt)
				return 1;
		}
	}
	return 0;
}

static int
__lpfc_idiag_print_cq(struct lpfc_queue *qp, char *cqtype,
			char *pbuffer, int len)
{
	if (!qp)
		return len;

	len += scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"\t%s CQ info: ", cqtype);
	len += scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"AssocEQID[%02d]: CQ STAT[max:x%x relw:x%x "
			"xabt:x%x wq:x%llx]\n",
			qp->assoc_qid, qp->q_cnt_1, qp->q_cnt_2,
			qp->q_cnt_3, (unsigned long long)qp->q_cnt_4);
	len += scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"\tCQID[%02d], QE-CNT[%04d], QE-SZ[%04d], "
			"HST-IDX[%04d], NTFI[%03d], PLMT[%03d]",
			qp->queue_id, qp->entry_count,
			qp->entry_size, qp->host_index,
			qp->notify_interval, qp->max_proc_limit);

	len +=  scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"\n");

	return len;
}

static int
__lpfc_idiag_print_rqpair(struct lpfc_queue *qp, struct lpfc_queue *datqp,
			char *rqtype, char *pbuffer, int len)
{
	if (!qp || !datqp)
		return len;

	len += scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"\t\t%s RQ info: ", rqtype);
	len += scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"AssocCQID[%02d]: RQ-STAT[nopost:x%x nobuf:x%x "
			"posted:x%x rcv:x%llx]\n",
			qp->assoc_qid, qp->q_cnt_1, qp->q_cnt_2,
			qp->q_cnt_3, (unsigned long long)qp->q_cnt_4);
	len += scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"\t\tHQID[%02d], QE-CNT[%04d], QE-SZ[%04d], "
			"HST-IDX[%04d], PRT-IDX[%04d], NTFI[%03d]\n",
			qp->queue_id, qp->entry_count, qp->entry_size,
			qp->host_index, qp->hba_index, qp->notify_interval);
	len += scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"\t\tDQID[%02d], QE-CNT[%04d], QE-SZ[%04d], "
			"HST-IDX[%04d], PRT-IDX[%04d], NTFI[%03d]\n",
			datqp->queue_id, datqp->entry_count,
			datqp->entry_size, datqp->host_index,
			datqp->hba_index, datqp->notify_interval);
	return len;
}

static int
lpfc_idiag_cqs_for_eq(struct lpfc_hba *phba, char *pbuffer,
		int *len, int max_cnt, int eqidx, int eq_id)
{
	struct lpfc_queue *qp;
	int rc;

	qp = phba->sli4_hba.hdwq[eqidx].fcp_cq;

	*len = __lpfc_idiag_print_cq(qp, "FCP", pbuffer, *len);

	/* Reset max counter */
	qp->CQ_max_cqe = 0;

	if (*len >= max_cnt)
		return 1;

	rc = lpfc_idiag_wqs_for_cq(phba, "FCP", pbuffer, len,
				   max_cnt, qp->queue_id);
	if (rc)
		return 1;

	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
		qp = phba->sli4_hba.hdwq[eqidx].nvme_cq;

		*len = __lpfc_idiag_print_cq(qp, "NVME", pbuffer, *len);

		/* Reset max counter */
		qp->CQ_max_cqe = 0;

		if (*len >= max_cnt)
			return 1;

		rc = lpfc_idiag_wqs_for_cq(phba, "NVME", pbuffer, len,
					   max_cnt, qp->queue_id);
		if (rc)
			return 1;
	}

	if ((eqidx < phba->cfg_nvmet_mrq) && phba->nvmet_support) {
		/* NVMET CQset */
		qp = phba->sli4_hba.nvmet_cqset[eqidx];
		*len = __lpfc_idiag_print_cq(qp, "NVMET CQset", pbuffer, *len);

		/* Reset max counter */
		qp->CQ_max_cqe = 0;

		if (*len >= max_cnt)
			return 1;

		/* RQ header */
		qp = phba->sli4_hba.nvmet_mrq_hdr[eqidx];
		*len = __lpfc_idiag_print_rqpair(qp,
				phba->sli4_hba.nvmet_mrq_data[eqidx],
				"NVMET MRQ", pbuffer, *len);

		if (*len >= max_cnt)
			return 1;
	}

	return 0;
}

static int
__lpfc_idiag_print_eq(struct lpfc_queue *qp, char *eqtype,
			char *pbuffer, int len)
{
	if (!qp)
		return len;

	len += scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"\n%s EQ info: EQ-STAT[max:x%x noE:x%x "
			"cqe_proc:x%x eqe_proc:x%llx eqd %d]\n",
			eqtype, qp->q_cnt_1, qp->q_cnt_2, qp->q_cnt_3,
			(unsigned long long)qp->q_cnt_4, qp->q_mode);
	len += scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"EQID[%02d], QE-CNT[%04d], QE-SZ[%04d], "
			"HST-IDX[%04d], NTFI[%03d], PLMT[%03d], AFFIN[%03d]",
			qp->queue_id, qp->entry_count, qp->entry_size,
			qp->host_index, qp->notify_interval,
			qp->max_proc_limit, qp->chann);
	len +=  scnprintf(pbuffer + len, LPFC_QUE_INFO_GET_BUF_SIZE - len,
			"\n");

	return len;
}

/**
 * lpfc_idiag_queinfo_read - idiag debugfs read queue information
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the data to.
 * @nbytes: The number of bytes to read.
 * @ppos: The position in the file to start reading from.
 *
 * Description:
 * This routine reads data from the @phba SLI4 PCI function queue information,
 * and copies to user @buf.
 * This routine only returns 1 EQs worth of information. It remembers the last
 * EQ read and jumps to the next EQ. Thus subsequent calls to queInfo will
 * retrieve all EQs allocated for the phba.
 *
 * Returns:
 * This function returns the amount of data that was read (this could be less
 * than @nbytes if the end of the file was reached) or a negative error value.
 **/
static ssize_t
lpfc_idiag_queinfo_read(struct file *file, char __user *buf, size_t nbytes,
			loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	char *pbuffer;
	int max_cnt, rc, x, len = 0;
	struct lpfc_queue *qp = NULL;

	if (!debug->buffer)
		debug->buffer = kmalloc(LPFC_QUE_INFO_GET_BUF_SIZE, GFP_KERNEL);
	if (!debug->buffer)
		return 0;
	pbuffer = debug->buffer;
	max_cnt = LPFC_QUE_INFO_GET_BUF_SIZE - 256;

	if (*ppos)
		return 0;

	spin_lock_irq(&phba->hbalock);

	/* Fast-path event queue */
	if (phba->sli4_hba.hdwq && phba->cfg_hdw_queue) {

		x = phba->lpfc_idiag_last_eq;
		phba->lpfc_idiag_last_eq++;
		if (phba->lpfc_idiag_last_eq >= phba->cfg_hdw_queue)
			phba->lpfc_idiag_last_eq = 0;

		len += scnprintf(pbuffer + len,
				 LPFC_QUE_INFO_GET_BUF_SIZE - len,
				 "HDWQ %d out of %d HBA HDWQs\n",
				 x, phba->cfg_hdw_queue);

		/* Fast-path EQ */
		qp = phba->sli4_hba.hdwq[x].hba_eq;
		if (!qp)
			goto out;

		len = __lpfc_idiag_print_eq(qp, "HBA", pbuffer, len);

		/* Reset max counter */
		qp->EQ_max_eqe = 0;

		if (len >= max_cnt)
			goto too_big;

		/* will dump both fcp and nvme cqs/wqs for the eq */
		rc = lpfc_idiag_cqs_for_eq(phba, pbuffer, &len,
			max_cnt, x, qp->queue_id);
		if (rc)
			goto too_big;

		/* Only EQ 0 has slow path CQs configured */
		if (x)
			goto out;

		/* Slow-path mailbox CQ */
		qp = phba->sli4_hba.mbx_cq;
		len = __lpfc_idiag_print_cq(qp, "MBX", pbuffer, len);
		if (len >= max_cnt)
			goto too_big;

		/* Slow-path MBOX MQ */
		qp = phba->sli4_hba.mbx_wq;
		len = __lpfc_idiag_print_wq(qp, "MBX", pbuffer, len);
		if (len >= max_cnt)
			goto too_big;

		/* Slow-path ELS response CQ */
		qp = phba->sli4_hba.els_cq;
		len = __lpfc_idiag_print_cq(qp, "ELS", pbuffer, len);
		/* Reset max counter */
		if (qp)
			qp->CQ_max_cqe = 0;
		if (len >= max_cnt)
			goto too_big;

		/* Slow-path ELS WQ */
		qp = phba->sli4_hba.els_wq;
		len = __lpfc_idiag_print_wq(qp, "ELS", pbuffer, len);
		if (len >= max_cnt)
			goto too_big;

		qp = phba->sli4_hba.hdr_rq;
		len = __lpfc_idiag_print_rqpair(qp, phba->sli4_hba.dat_rq,
						"ELS RQpair", pbuffer, len);
		if (len >= max_cnt)
			goto too_big;

		/* Slow-path NVME LS response CQ */
		qp = phba->sli4_hba.nvmels_cq;
		len = __lpfc_idiag_print_cq(qp, "NVME LS",
						pbuffer, len);
		/* Reset max counter */
		if (qp)
			qp->CQ_max_cqe = 0;
		if (len >= max_cnt)
			goto too_big;

		/* Slow-path NVME LS WQ */
		qp = phba->sli4_hba.nvmels_wq;
		len = __lpfc_idiag_print_wq(qp, "NVME LS",
						pbuffer, len);
		if (len >= max_cnt)
			goto too_big;

		goto out;
	}

	spin_unlock_irq(&phba->hbalock);
	return simple_read_from_buffer(buf, nbytes, ppos, pbuffer, len);

too_big:
	len +=  scnprintf(pbuffer + len,
		LPFC_QUE_INFO_GET_BUF_SIZE - len, "Truncated ...\n");
out:
	spin_unlock_irq(&phba->hbalock);
	return simple_read_from_buffer(buf, nbytes, ppos, pbuffer, len);
}

/**
 * lpfc_idiag_que_param_check - queue access command parameter sanity check
 * @q: The pointer to queue structure.
 * @index: The index into a queue entry.
 * @count: The number of queue entries to access.
 *
 * Description:
 * The routine performs sanity check on device queue access method commands.
 *
 * Returns:
 * This function returns -EINVAL when fails the sanity check, otherwise, it
 * returns 0.
 **/
static int
lpfc_idiag_que_param_check(struct lpfc_queue *q, int index, int count)
{
	/* Only support single entry read or browsing */
	if ((count != 1) && (count != LPFC_QUE_ACC_BROWSE))
		return -EINVAL;
	if (index > q->entry_count - 1)
		return -EINVAL;
	return 0;
}

/**
 * lpfc_idiag_queacc_read_qe - read a single entry from the given queue index
 * @pbuffer: The pointer to buffer to copy the read data into.
 * @pque: The pointer to the queue to be read.
 * @index: The index into the queue entry.
 *
 * Description:
 * This routine reads out a single entry from the given queue's index location
 * and copies it into the buffer provided.
 *
 * Returns:
 * This function returns 0 when it fails, otherwise, it returns the length of
 * the data read into the buffer provided.
 **/
static int
lpfc_idiag_queacc_read_qe(char *pbuffer, int len, struct lpfc_queue *pque,
			  uint32_t index)
{
	int offset, esize;
	uint32_t *pentry;

	if (!pbuffer || !pque)
		return 0;

	esize = pque->entry_size;
	len += scnprintf(pbuffer+len, LPFC_QUE_ACC_BUF_SIZE-len,
			"QE-INDEX[%04d]:\n", index);

	offset = 0;
	pentry = lpfc_sli4_qe(pque, index);
	while (esize > 0) {
		len += scnprintf(pbuffer+len, LPFC_QUE_ACC_BUF_SIZE-len,
				"%08x ", *pentry);
		pentry++;
		offset += sizeof(uint32_t);
		esize -= sizeof(uint32_t);
		if (esize > 0 && !(offset % (4 * sizeof(uint32_t))))
			len += scnprintf(pbuffer+len,
					LPFC_QUE_ACC_BUF_SIZE-len, "\n");
	}
	len += scnprintf(pbuffer+len, LPFC_QUE_ACC_BUF_SIZE-len, "\n");

	return len;
}

/**
 * lpfc_idiag_queacc_read - idiag debugfs read port queue
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the data to.
 * @nbytes: The number of bytes to read.
 * @ppos: The position in the file to start reading from.
 *
 * Description:
 * This routine reads data from the @phba device queue memory according to the
 * idiag command, and copies to user @buf. Depending on the queue dump read
 * command setup, it does either a single queue entry read or browing through
 * all entries of the queue.
 *
 * Returns:
 * This function returns the amount of data that was read (this could be less
 * than @nbytes if the end of the file was reached) or a negative error value.
 **/
static ssize_t
lpfc_idiag_queacc_read(struct file *file, char __user *buf, size_t nbytes,
		       loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	uint32_t last_index, index, count;
	struct lpfc_queue *pque = NULL;
	char *pbuffer;
	int len = 0;

	/* This is a user read operation */
	debug->op = LPFC_IDIAG_OP_RD;

	if (!debug->buffer)
		debug->buffer = kmalloc(LPFC_QUE_ACC_BUF_SIZE, GFP_KERNEL);
	if (!debug->buffer)
		return 0;
	pbuffer = debug->buffer;

	if (*ppos)
		return 0;

	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_QUEACC_RD) {
		index = idiag.cmd.data[IDIAG_QUEACC_INDEX_INDX];
		count = idiag.cmd.data[IDIAG_QUEACC_COUNT_INDX];
		pque = (struct lpfc_queue *)idiag.ptr_private;
	} else
		return 0;

	/* Browse the queue starting from index */
	if (count == LPFC_QUE_ACC_BROWSE)
		goto que_browse;

	/* Read a single entry from the queue */
	len = lpfc_idiag_queacc_read_qe(pbuffer, len, pque, index);

	return simple_read_from_buffer(buf, nbytes, ppos, pbuffer, len);

que_browse:

	/* Browse all entries from the queue */
	last_index = idiag.offset.last_rd;
	index = last_index;

	while (len < LPFC_QUE_ACC_SIZE - pque->entry_size) {
		len = lpfc_idiag_queacc_read_qe(pbuffer, len, pque, index);
		index++;
		if (index > pque->entry_count - 1)
			break;
	}

	/* Set up the offset for next portion of pci cfg read */
	if (index > pque->entry_count - 1)
		index = 0;
	idiag.offset.last_rd = index;

	return simple_read_from_buffer(buf, nbytes, ppos, pbuffer, len);
}

/**
 * lpfc_idiag_queacc_write - Syntax check and set up idiag queacc commands
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the user data from.
 * @nbytes: The number of bytes to get.
 * @ppos: The position in the file to start reading from.
 *
 * This routine get the debugfs idiag command struct from user space and then
 * perform the syntax check for port queue read (dump) or write (set) command
 * accordingly. In the case of port queue read command, it sets up the command
 * in the idiag command struct for the following debugfs read operation. In
 * the case of port queue write operation, it executes the write operation
 * into the port queue entry accordingly.
 *
 * It returns the @nbytges passing in from debugfs user space when successful.
 * In case of error conditions, it returns proper error code back to the user
 * space.
 **/
static ssize_t
lpfc_idiag_queacc_write(struct file *file, const char __user *buf,
			size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	uint32_t qidx, quetp, queid, index, count, offset, value;
	uint32_t *pentry;
	struct lpfc_queue *pque, *qp;
	int rc;

	/* This is a user write operation */
	debug->op = LPFC_IDIAG_OP_WR;

	rc = lpfc_idiag_cmd_get(buf, nbytes, &idiag.cmd);
	if (rc < 0)
		return rc;

	/* Get and sanity check on command feilds */
	quetp  = idiag.cmd.data[IDIAG_QUEACC_QUETP_INDX];
	queid  = idiag.cmd.data[IDIAG_QUEACC_QUEID_INDX];
	index  = idiag.cmd.data[IDIAG_QUEACC_INDEX_INDX];
	count  = idiag.cmd.data[IDIAG_QUEACC_COUNT_INDX];
	offset = idiag.cmd.data[IDIAG_QUEACC_OFFST_INDX];
	value  = idiag.cmd.data[IDIAG_QUEACC_VALUE_INDX];

	/* Sanity check on command line arguments */
	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_QUEACC_WR ||
	    idiag.cmd.opcode == LPFC_IDIAG_CMD_QUEACC_ST ||
	    idiag.cmd.opcode == LPFC_IDIAG_CMD_QUEACC_CL) {
		if (rc != LPFC_QUE_ACC_WR_CMD_ARG)
			goto error_out;
		if (count != 1)
			goto error_out;
	} else if (idiag.cmd.opcode == LPFC_IDIAG_CMD_QUEACC_RD) {
		if (rc != LPFC_QUE_ACC_RD_CMD_ARG)
			goto error_out;
	} else
		goto error_out;

	switch (quetp) {
	case LPFC_IDIAG_EQ:
		/* HBA event queue */
		if (phba->sli4_hba.hdwq) {
			for (qidx = 0; qidx < phba->cfg_hdw_queue; qidx++) {
				qp = phba->sli4_hba.hdwq[qidx].hba_eq;
				if (qp && qp->queue_id == queid) {
					/* Sanity check */
					rc = lpfc_idiag_que_param_check(qp,
						index, count);
					if (rc)
						goto error_out;
					idiag.ptr_private = qp;
					goto pass_check;
				}
			}
		}
		goto error_out;
		break;
	case LPFC_IDIAG_CQ:
		/* MBX complete queue */
		if (phba->sli4_hba.mbx_cq &&
		    phba->sli4_hba.mbx_cq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.mbx_cq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.mbx_cq;
			goto pass_check;
		}
		/* ELS complete queue */
		if (phba->sli4_hba.els_cq &&
		    phba->sli4_hba.els_cq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.els_cq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.els_cq;
			goto pass_check;
		}
		/* NVME LS complete queue */
		if (phba->sli4_hba.nvmels_cq &&
		    phba->sli4_hba.nvmels_cq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.nvmels_cq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.nvmels_cq;
			goto pass_check;
		}
		/* FCP complete queue */
		if (phba->sli4_hba.hdwq) {
			for (qidx = 0; qidx < phba->cfg_hdw_queue;
								qidx++) {
				qp = phba->sli4_hba.hdwq[qidx].fcp_cq;
				if (qp && qp->queue_id == queid) {
					/* Sanity check */
					rc = lpfc_idiag_que_param_check(
						qp, index, count);
					if (rc)
						goto error_out;
					idiag.ptr_private = qp;
					goto pass_check;
				}
			}
		}
		/* NVME complete queue */
		if (phba->sli4_hba.hdwq) {
			qidx = 0;
			do {
				qp = phba->sli4_hba.hdwq[qidx].nvme_cq;
				if (qp && qp->queue_id == queid) {
					/* Sanity check */
					rc = lpfc_idiag_que_param_check(
						qp, index, count);
					if (rc)
						goto error_out;
					idiag.ptr_private = qp;
					goto pass_check;
				}
			} while (++qidx < phba->cfg_hdw_queue);
		}
		goto error_out;
		break;
	case LPFC_IDIAG_MQ:
		/* MBX work queue */
		if (phba->sli4_hba.mbx_wq &&
		    phba->sli4_hba.mbx_wq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.mbx_wq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.mbx_wq;
			goto pass_check;
		}
		goto error_out;
		break;
	case LPFC_IDIAG_WQ:
		/* ELS work queue */
		if (phba->sli4_hba.els_wq &&
		    phba->sli4_hba.els_wq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.els_wq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.els_wq;
			goto pass_check;
		}
		/* NVME LS work queue */
		if (phba->sli4_hba.nvmels_wq &&
		    phba->sli4_hba.nvmels_wq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.nvmels_wq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.nvmels_wq;
			goto pass_check;
		}

		if (phba->sli4_hba.hdwq) {
			/* FCP/SCSI work queue */
			for (qidx = 0; qidx < phba->cfg_hdw_queue; qidx++) {
				qp = phba->sli4_hba.hdwq[qidx].fcp_wq;
				if (qp && qp->queue_id == queid) {
					/* Sanity check */
					rc = lpfc_idiag_que_param_check(
						qp, index, count);
					if (rc)
						goto error_out;
					idiag.ptr_private = qp;
					goto pass_check;
				}
			}
			/* NVME work queue */
			for (qidx = 0; qidx < phba->cfg_hdw_queue; qidx++) {
				qp = phba->sli4_hba.hdwq[qidx].nvme_wq;
				if (qp && qp->queue_id == queid) {
					/* Sanity check */
					rc = lpfc_idiag_que_param_check(
						qp, index, count);
					if (rc)
						goto error_out;
					idiag.ptr_private = qp;
					goto pass_check;
				}
			}
		}

		goto error_out;
		break;
	case LPFC_IDIAG_RQ:
		/* HDR queue */
		if (phba->sli4_hba.hdr_rq &&
		    phba->sli4_hba.hdr_rq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.hdr_rq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.hdr_rq;
			goto pass_check;
		}
		/* DAT queue */
		if (phba->sli4_hba.dat_rq &&
		    phba->sli4_hba.dat_rq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.dat_rq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.dat_rq;
			goto pass_check;
		}
		goto error_out;
		break;
	default:
		goto error_out;
		break;
	}

pass_check:

	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_QUEACC_RD) {
		if (count == LPFC_QUE_ACC_BROWSE)
			idiag.offset.last_rd = index;
	}

	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_QUEACC_WR ||
	    idiag.cmd.opcode == LPFC_IDIAG_CMD_QUEACC_ST ||
	    idiag.cmd.opcode == LPFC_IDIAG_CMD_QUEACC_CL) {
		/* Additional sanity checks on write operation */
		pque = (struct lpfc_queue *)idiag.ptr_private;
		if (offset > pque->entry_size/sizeof(uint32_t) - 1)
			goto error_out;
		pentry = lpfc_sli4_qe(pque, index);
		pentry += offset;
		if (idiag.cmd.opcode == LPFC_IDIAG_CMD_QUEACC_WR)
			*pentry = value;
		if (idiag.cmd.opcode == LPFC_IDIAG_CMD_QUEACC_ST)
			*pentry |= value;
		if (idiag.cmd.opcode == LPFC_IDIAG_CMD_QUEACC_CL)
			*pentry &= ~value;
	}
	return nbytes;

error_out:
	/* Clean out command structure on command error out */
	memset(&idiag, 0, sizeof(idiag));
	return -EINVAL;
}

/**
 * lpfc_idiag_drbacc_read_reg - idiag debugfs read a doorbell register
 * @phba: The pointer to hba structure.
 * @pbuffer: The pointer to the buffer to copy the data to.
 * @len: The length of bytes to copied.
 * @drbregid: The id to doorbell registers.
 *
 * Description:
 * This routine reads a doorbell register and copies its content to the
 * user buffer pointed to by @pbuffer.
 *
 * Returns:
 * This function returns the amount of data that was copied into @pbuffer.
 **/
static int
lpfc_idiag_drbacc_read_reg(struct lpfc_hba *phba, char *pbuffer,
			   int len, uint32_t drbregid)
{

	if (!pbuffer)
		return 0;

	switch (drbregid) {
	case LPFC_DRB_EQ:
		len += scnprintf(pbuffer + len, LPFC_DRB_ACC_BUF_SIZE-len,
				"EQ-DRB-REG: 0x%08x\n",
				readl(phba->sli4_hba.EQDBregaddr));
		break;
	case LPFC_DRB_CQ:
		len += scnprintf(pbuffer + len, LPFC_DRB_ACC_BUF_SIZE - len,
				"CQ-DRB-REG: 0x%08x\n",
				readl(phba->sli4_hba.CQDBregaddr));
		break;
	case LPFC_DRB_MQ:
		len += scnprintf(pbuffer+len, LPFC_DRB_ACC_BUF_SIZE-len,
				"MQ-DRB-REG:   0x%08x\n",
				readl(phba->sli4_hba.MQDBregaddr));
		break;
	case LPFC_DRB_WQ:
		len += scnprintf(pbuffer+len, LPFC_DRB_ACC_BUF_SIZE-len,
				"WQ-DRB-REG:   0x%08x\n",
				readl(phba->sli4_hba.WQDBregaddr));
		break;
	case LPFC_DRB_RQ:
		len += scnprintf(pbuffer+len, LPFC_DRB_ACC_BUF_SIZE-len,
				"RQ-DRB-REG:   0x%08x\n",
				readl(phba->sli4_hba.RQDBregaddr));
		break;
	default:
		break;
	}

	return len;
}

/**
 * lpfc_idiag_drbacc_read - idiag debugfs read port doorbell
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the data to.
 * @nbytes: The number of bytes to read.
 * @ppos: The position in the file to start reading from.
 *
 * Description:
 * This routine reads data from the @phba device doorbell register according
 * to the idiag command, and copies to user @buf. Depending on the doorbell
 * register read command setup, it does either a single doorbell register
 * read or dump all doorbell registers.
 *
 * Returns:
 * This function returns the amount of data that was read (this could be less
 * than @nbytes if the end of the file was reached) or a negative error value.
 **/
static ssize_t
lpfc_idiag_drbacc_read(struct file *file, char __user *buf, size_t nbytes,
		       loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	uint32_t drb_reg_id, i;
	char *pbuffer;
	int len = 0;

	/* This is a user read operation */
	debug->op = LPFC_IDIAG_OP_RD;

	if (!debug->buffer)
		debug->buffer = kmalloc(LPFC_DRB_ACC_BUF_SIZE, GFP_KERNEL);
	if (!debug->buffer)
		return 0;
	pbuffer = debug->buffer;

	if (*ppos)
		return 0;

	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_DRBACC_RD)
		drb_reg_id = idiag.cmd.data[IDIAG_DRBACC_REGID_INDX];
	else
		return 0;

	if (drb_reg_id == LPFC_DRB_ACC_ALL)
		for (i = 1; i <= LPFC_DRB_MAX; i++)
			len = lpfc_idiag_drbacc_read_reg(phba,
							 pbuffer, len, i);
	else
		len = lpfc_idiag_drbacc_read_reg(phba,
						 pbuffer, len, drb_reg_id);

	return simple_read_from_buffer(buf, nbytes, ppos, pbuffer, len);
}

/**
 * lpfc_idiag_drbacc_write - Syntax check and set up idiag drbacc commands
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the user data from.
 * @nbytes: The number of bytes to get.
 * @ppos: The position in the file to start reading from.
 *
 * This routine get the debugfs idiag command struct from user space and then
 * perform the syntax check for port doorbell register read (dump) or write
 * (set) command accordingly. In the case of port queue read command, it sets
 * up the command in the idiag command struct for the following debugfs read
 * operation. In the case of port doorbell register write operation, it
 * executes the write operation into the port doorbell register accordingly.
 *
 * It returns the @nbytges passing in from debugfs user space when successful.
 * In case of error conditions, it returns proper error code back to the user
 * space.
 **/
static ssize_t
lpfc_idiag_drbacc_write(struct file *file, const char __user *buf,
			size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	uint32_t drb_reg_id, value, reg_val = 0;
	void __iomem *drb_reg;
	int rc;

	/* This is a user write operation */
	debug->op = LPFC_IDIAG_OP_WR;

	rc = lpfc_idiag_cmd_get(buf, nbytes, &idiag.cmd);
	if (rc < 0)
		return rc;

	/* Sanity check on command line arguments */
	drb_reg_id = idiag.cmd.data[IDIAG_DRBACC_REGID_INDX];
	value = idiag.cmd.data[IDIAG_DRBACC_VALUE_INDX];

	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_DRBACC_WR ||
	    idiag.cmd.opcode == LPFC_IDIAG_CMD_DRBACC_ST ||
	    idiag.cmd.opcode == LPFC_IDIAG_CMD_DRBACC_CL) {
		if (rc != LPFC_DRB_ACC_WR_CMD_ARG)
			goto error_out;
		if (drb_reg_id > LPFC_DRB_MAX)
			goto error_out;
	} else if (idiag.cmd.opcode == LPFC_IDIAG_CMD_DRBACC_RD) {
		if (rc != LPFC_DRB_ACC_RD_CMD_ARG)
			goto error_out;
		if ((drb_reg_id > LPFC_DRB_MAX) &&
		    (drb_reg_id != LPFC_DRB_ACC_ALL))
			goto error_out;
	} else
		goto error_out;

	/* Perform the write access operation */
	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_DRBACC_WR ||
	    idiag.cmd.opcode == LPFC_IDIAG_CMD_DRBACC_ST ||
	    idiag.cmd.opcode == LPFC_IDIAG_CMD_DRBACC_CL) {
		switch (drb_reg_id) {
		case LPFC_DRB_EQ:
			drb_reg = phba->sli4_hba.EQDBregaddr;
			break;
		case LPFC_DRB_CQ:
			drb_reg = phba->sli4_hba.CQDBregaddr;
			break;
		case LPFC_DRB_MQ:
			drb_reg = phba->sli4_hba.MQDBregaddr;
			break;
		case LPFC_DRB_WQ:
			drb_reg = phba->sli4_hba.WQDBregaddr;
			break;
		case LPFC_DRB_RQ:
			drb_reg = phba->sli4_hba.RQDBregaddr;
			break;
		default:
			goto error_out;
		}

		if (idiag.cmd.opcode == LPFC_IDIAG_CMD_DRBACC_WR)
			reg_val = value;
		if (idiag.cmd.opcode == LPFC_IDIAG_CMD_DRBACC_ST) {
			reg_val = readl(drb_reg);
			reg_val |= value;
		}
		if (idiag.cmd.opcode == LPFC_IDIAG_CMD_DRBACC_CL) {
			reg_val = readl(drb_reg);
			reg_val &= ~value;
		}
		writel(reg_val, drb_reg);
		readl(drb_reg); /* flush */
	}
	return nbytes;

error_out:
	/* Clean out command structure on command error out */
	memset(&idiag, 0, sizeof(idiag));
	return -EINVAL;
}

/**
 * lpfc_idiag_ctlacc_read_reg - idiag debugfs read a control registers
 * @phba: The pointer to hba structure.
 * @pbuffer: The pointer to the buffer to copy the data to.
 * @len: The length of bytes to copied.
 * @drbregid: The id to doorbell registers.
 *
 * Description:
 * This routine reads a control register and copies its content to the
 * user buffer pointed to by @pbuffer.
 *
 * Returns:
 * This function returns the amount of data that was copied into @pbuffer.
 **/
static int
lpfc_idiag_ctlacc_read_reg(struct lpfc_hba *phba, char *pbuffer,
			   int len, uint32_t ctlregid)
{

	if (!pbuffer)
		return 0;

	switch (ctlregid) {
	case LPFC_CTL_PORT_SEM:
		len += scnprintf(pbuffer+len, LPFC_CTL_ACC_BUF_SIZE-len,
				"Port SemReg:   0x%08x\n",
				readl(phba->sli4_hba.conf_regs_memmap_p +
				      LPFC_CTL_PORT_SEM_OFFSET));
		break;
	case LPFC_CTL_PORT_STA:
		len += scnprintf(pbuffer+len, LPFC_CTL_ACC_BUF_SIZE-len,
				"Port StaReg:   0x%08x\n",
				readl(phba->sli4_hba.conf_regs_memmap_p +
				      LPFC_CTL_PORT_STA_OFFSET));
		break;
	case LPFC_CTL_PORT_CTL:
		len += scnprintf(pbuffer+len, LPFC_CTL_ACC_BUF_SIZE-len,
				"Port CtlReg:   0x%08x\n",
				readl(phba->sli4_hba.conf_regs_memmap_p +
				      LPFC_CTL_PORT_CTL_OFFSET));
		break;
	case LPFC_CTL_PORT_ER1:
		len += scnprintf(pbuffer+len, LPFC_CTL_ACC_BUF_SIZE-len,
				"Port Er1Reg:   0x%08x\n",
				readl(phba->sli4_hba.conf_regs_memmap_p +
				      LPFC_CTL_PORT_ER1_OFFSET));
		break;
	case LPFC_CTL_PORT_ER2:
		len += scnprintf(pbuffer+len, LPFC_CTL_ACC_BUF_SIZE-len,
				"Port Er2Reg:   0x%08x\n",
				readl(phba->sli4_hba.conf_regs_memmap_p +
				      LPFC_CTL_PORT_ER2_OFFSET));
		break;
	case LPFC_CTL_PDEV_CTL:
		len += scnprintf(pbuffer+len, LPFC_CTL_ACC_BUF_SIZE-len,
				"PDev CtlReg:   0x%08x\n",
				readl(phba->sli4_hba.conf_regs_memmap_p +
				      LPFC_CTL_PDEV_CTL_OFFSET));
		break;
	default:
		break;
	}
	return len;
}

/**
 * lpfc_idiag_ctlacc_read - idiag debugfs read port and device control register
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the data to.
 * @nbytes: The number of bytes to read.
 * @ppos: The position in the file to start reading from.
 *
 * Description:
 * This routine reads data from the @phba port and device registers according
 * to the idiag command, and copies to user @buf.
 *
 * Returns:
 * This function returns the amount of data that was read (this could be less
 * than @nbytes if the end of the file was reached) or a negative error value.
 **/
static ssize_t
lpfc_idiag_ctlacc_read(struct file *file, char __user *buf, size_t nbytes,
		       loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	uint32_t ctl_reg_id, i;
	char *pbuffer;
	int len = 0;

	/* This is a user read operation */
	debug->op = LPFC_IDIAG_OP_RD;

	if (!debug->buffer)
		debug->buffer = kmalloc(LPFC_CTL_ACC_BUF_SIZE, GFP_KERNEL);
	if (!debug->buffer)
		return 0;
	pbuffer = debug->buffer;

	if (*ppos)
		return 0;

	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_CTLACC_RD)
		ctl_reg_id = idiag.cmd.data[IDIAG_CTLACC_REGID_INDX];
	else
		return 0;

	if (ctl_reg_id == LPFC_CTL_ACC_ALL)
		for (i = 1; i <= LPFC_CTL_MAX; i++)
			len = lpfc_idiag_ctlacc_read_reg(phba,
							 pbuffer, len, i);
	else
		len = lpfc_idiag_ctlacc_read_reg(phba,
						 pbuffer, len, ctl_reg_id);

	return simple_read_from_buffer(buf, nbytes, ppos, pbuffer, len);
}

/**
 * lpfc_idiag_ctlacc_write - Syntax check and set up idiag ctlacc commands
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the user data from.
 * @nbytes: The number of bytes to get.
 * @ppos: The position in the file to start reading from.
 *
 * This routine get the debugfs idiag command struct from user space and then
 * perform the syntax check for port and device control register read (dump)
 * or write (set) command accordingly.
 *
 * It returns the @nbytges passing in from debugfs user space when successful.
 * In case of error conditions, it returns proper error code back to the user
 * space.
 **/
static ssize_t
lpfc_idiag_ctlacc_write(struct file *file, const char __user *buf,
			size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	uint32_t ctl_reg_id, value, reg_val = 0;
	void __iomem *ctl_reg;
	int rc;

	/* This is a user write operation */
	debug->op = LPFC_IDIAG_OP_WR;

	rc = lpfc_idiag_cmd_get(buf, nbytes, &idiag.cmd);
	if (rc < 0)
		return rc;

	/* Sanity check on command line arguments */
	ctl_reg_id = idiag.cmd.data[IDIAG_CTLACC_REGID_INDX];
	value = idiag.cmd.data[IDIAG_CTLACC_VALUE_INDX];

	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_CTLACC_WR ||
	    idiag.cmd.opcode == LPFC_IDIAG_CMD_CTLACC_ST ||
	    idiag.cmd.opcode == LPFC_IDIAG_CMD_CTLACC_CL) {
		if (rc != LPFC_CTL_ACC_WR_CMD_ARG)
			goto error_out;
		if (ctl_reg_id > LPFC_CTL_MAX)
			goto error_out;
	} else if (idiag.cmd.opcode == LPFC_IDIAG_CMD_CTLACC_RD) {
		if (rc != LPFC_CTL_ACC_RD_CMD_ARG)
			goto error_out;
		if ((ctl_reg_id > LPFC_CTL_MAX) &&
		    (ctl_reg_id != LPFC_CTL_ACC_ALL))
			goto error_out;
	} else
		goto error_out;

	/* Perform the write access operation */
	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_CTLACC_WR ||
	    idiag.cmd.opcode == LPFC_IDIAG_CMD_CTLACC_ST ||
	    idiag.cmd.opcode == LPFC_IDIAG_CMD_CTLACC_CL) {
		switch (ctl_reg_id) {
		case LPFC_CTL_PORT_SEM:
			ctl_reg = phba->sli4_hba.conf_regs_memmap_p +
					LPFC_CTL_PORT_SEM_OFFSET;
			break;
		case LPFC_CTL_PORT_STA:
			ctl_reg = phba->sli4_hba.conf_regs_memmap_p +
					LPFC_CTL_PORT_STA_OFFSET;
			break;
		case LPFC_CTL_PORT_CTL:
			ctl_reg = phba->sli4_hba.conf_regs_memmap_p +
					LPFC_CTL_PORT_CTL_OFFSET;
			break;
		case LPFC_CTL_PORT_ER1:
			ctl_reg = phba->sli4_hba.conf_regs_memmap_p +
					LPFC_CTL_PORT_ER1_OFFSET;
			break;
		case LPFC_CTL_PORT_ER2:
			ctl_reg = phba->sli4_hba.conf_regs_memmap_p +
					LPFC_CTL_PORT_ER2_OFFSET;
			break;
		case LPFC_CTL_PDEV_CTL:
			ctl_reg = phba->sli4_hba.conf_regs_memmap_p +
					LPFC_CTL_PDEV_CTL_OFFSET;
			break;
		default:
			goto error_out;
		}

		if (idiag.cmd.opcode == LPFC_IDIAG_CMD_CTLACC_WR)
			reg_val = value;
		if (idiag.cmd.opcode == LPFC_IDIAG_CMD_CTLACC_ST) {
			reg_val = readl(ctl_reg);
			reg_val |= value;
		}
		if (idiag.cmd.opcode == LPFC_IDIAG_CMD_CTLACC_CL) {
			reg_val = readl(ctl_reg);
			reg_val &= ~value;
		}
		writel(reg_val, ctl_reg);
		readl(ctl_reg); /* flush */
	}
	return nbytes;

error_out:
	/* Clean out command structure on command error out */
	memset(&idiag, 0, sizeof(idiag));
	return -EINVAL;
}

/**
 * lpfc_idiag_mbxacc_get_setup - idiag debugfs get mailbox access setup
 * @phba: Pointer to HBA context object.
 * @pbuffer: Pointer to data buffer.
 *
 * Description:
 * This routine gets the driver mailbox access debugfs setup information.
 *
 * Returns:
 * This function returns the amount of data that was read (this could be less
 * than @nbytes if the end of the file was reached) or a negative error value.
 **/
static int
lpfc_idiag_mbxacc_get_setup(struct lpfc_hba *phba, char *pbuffer)
{
	uint32_t mbx_dump_map, mbx_dump_cnt, mbx_word_cnt, mbx_mbox_cmd;
	int len = 0;

	mbx_mbox_cmd = idiag.cmd.data[IDIAG_MBXACC_MBCMD_INDX];
	mbx_dump_map = idiag.cmd.data[IDIAG_MBXACC_DPMAP_INDX];
	mbx_dump_cnt = idiag.cmd.data[IDIAG_MBXACC_DPCNT_INDX];
	mbx_word_cnt = idiag.cmd.data[IDIAG_MBXACC_WDCNT_INDX];

	len += scnprintf(pbuffer+len, LPFC_MBX_ACC_BUF_SIZE-len,
			"mbx_dump_map: 0x%08x\n", mbx_dump_map);
	len += scnprintf(pbuffer+len, LPFC_MBX_ACC_BUF_SIZE-len,
			"mbx_dump_cnt: %04d\n", mbx_dump_cnt);
	len += scnprintf(pbuffer+len, LPFC_MBX_ACC_BUF_SIZE-len,
			"mbx_word_cnt: %04d\n", mbx_word_cnt);
	len += scnprintf(pbuffer+len, LPFC_MBX_ACC_BUF_SIZE-len,
			"mbx_mbox_cmd: 0x%02x\n", mbx_mbox_cmd);

	return len;
}

/**
 * lpfc_idiag_mbxacc_read - idiag debugfs read on mailbox access
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the data to.
 * @nbytes: The number of bytes to read.
 * @ppos: The position in the file to start reading from.
 *
 * Description:
 * This routine reads data from the @phba driver mailbox access debugfs setup
 * information.
 *
 * Returns:
 * This function returns the amount of data that was read (this could be less
 * than @nbytes if the end of the file was reached) or a negative error value.
 **/
static ssize_t
lpfc_idiag_mbxacc_read(struct file *file, char __user *buf, size_t nbytes,
		       loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	char *pbuffer;
	int len = 0;

	/* This is a user read operation */
	debug->op = LPFC_IDIAG_OP_RD;

	if (!debug->buffer)
		debug->buffer = kmalloc(LPFC_MBX_ACC_BUF_SIZE, GFP_KERNEL);
	if (!debug->buffer)
		return 0;
	pbuffer = debug->buffer;

	if (*ppos)
		return 0;

	if ((idiag.cmd.opcode != LPFC_IDIAG_CMD_MBXACC_DP) &&
	    (idiag.cmd.opcode != LPFC_IDIAG_BSG_MBXACC_DP))
		return 0;

	len = lpfc_idiag_mbxacc_get_setup(phba, pbuffer);

	return simple_read_from_buffer(buf, nbytes, ppos, pbuffer, len);
}

/**
 * lpfc_idiag_mbxacc_write - Syntax check and set up idiag mbxacc commands
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the user data from.
 * @nbytes: The number of bytes to get.
 * @ppos: The position in the file to start reading from.
 *
 * This routine get the debugfs idiag command struct from user space and then
 * perform the syntax check for driver mailbox command (dump) and sets up the
 * necessary states in the idiag command struct accordingly.
 *
 * It returns the @nbytges passing in from debugfs user space when successful.
 * In case of error conditions, it returns proper error code back to the user
 * space.
 **/
static ssize_t
lpfc_idiag_mbxacc_write(struct file *file, const char __user *buf,
			size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	uint32_t mbx_dump_map, mbx_dump_cnt, mbx_word_cnt, mbx_mbox_cmd;
	int rc;

	/* This is a user write operation */
	debug->op = LPFC_IDIAG_OP_WR;

	rc = lpfc_idiag_cmd_get(buf, nbytes, &idiag.cmd);
	if (rc < 0)
		return rc;

	/* Sanity check on command line arguments */
	mbx_mbox_cmd = idiag.cmd.data[IDIAG_MBXACC_MBCMD_INDX];
	mbx_dump_map = idiag.cmd.data[IDIAG_MBXACC_DPMAP_INDX];
	mbx_dump_cnt = idiag.cmd.data[IDIAG_MBXACC_DPCNT_INDX];
	mbx_word_cnt = idiag.cmd.data[IDIAG_MBXACC_WDCNT_INDX];

	if (idiag.cmd.opcode == LPFC_IDIAG_CMD_MBXACC_DP) {
		if (!(mbx_dump_map & LPFC_MBX_DMP_MBX_ALL))
			goto error_out;
		if ((mbx_dump_map & ~LPFC_MBX_DMP_MBX_ALL) &&
		    (mbx_dump_map != LPFC_MBX_DMP_ALL))
			goto error_out;
		if (mbx_word_cnt > sizeof(MAILBOX_t))
			goto error_out;
	} else if (idiag.cmd.opcode == LPFC_IDIAG_BSG_MBXACC_DP) {
		if (!(mbx_dump_map & LPFC_BSG_DMP_MBX_ALL))
			goto error_out;
		if ((mbx_dump_map & ~LPFC_BSG_DMP_MBX_ALL) &&
		    (mbx_dump_map != LPFC_MBX_DMP_ALL))
			goto error_out;
		if (mbx_word_cnt > (BSG_MBOX_SIZE)/4)
			goto error_out;
		if (mbx_mbox_cmd != 0x9b)
			goto error_out;
	} else
		goto error_out;

	if (mbx_word_cnt == 0)
		goto error_out;
	if (rc != LPFC_MBX_DMP_ARG)
		goto error_out;
	if (mbx_mbox_cmd & ~0xff)
		goto error_out;

	/* condition for stop mailbox dump */
	if (mbx_dump_cnt == 0)
		goto reset_out;

	return nbytes;

reset_out:
	/* Clean out command structure on command error out */
	memset(&idiag, 0, sizeof(idiag));
	return nbytes;

error_out:
	/* Clean out command structure on command error out */
	memset(&idiag, 0, sizeof(idiag));
	return -EINVAL;
}

/**
 * lpfc_idiag_extacc_avail_get - get the available extents information
 * @phba: pointer to lpfc hba data structure.
 * @pbuffer: pointer to internal buffer.
 * @len: length into the internal buffer data has been copied.
 *
 * Description:
 * This routine is to get the available extent information.
 *
 * Returns:
 * overall lenth of the data read into the internal buffer.
 **/
static int
lpfc_idiag_extacc_avail_get(struct lpfc_hba *phba, char *pbuffer, int len)
{
	uint16_t ext_cnt, ext_size;

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\nAvailable Extents Information:\n");

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\tPort Available VPI extents: ");
	lpfc_sli4_get_avail_extnt_rsrc(phba, LPFC_RSC_TYPE_FCOE_VPI,
				       &ext_cnt, &ext_size);
	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"Count %3d, Size %3d\n", ext_cnt, ext_size);

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\tPort Available VFI extents: ");
	lpfc_sli4_get_avail_extnt_rsrc(phba, LPFC_RSC_TYPE_FCOE_VFI,
				       &ext_cnt, &ext_size);
	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"Count %3d, Size %3d\n", ext_cnt, ext_size);

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\tPort Available RPI extents: ");
	lpfc_sli4_get_avail_extnt_rsrc(phba, LPFC_RSC_TYPE_FCOE_RPI,
				       &ext_cnt, &ext_size);
	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"Count %3d, Size %3d\n", ext_cnt, ext_size);

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\tPort Available XRI extents: ");
	lpfc_sli4_get_avail_extnt_rsrc(phba, LPFC_RSC_TYPE_FCOE_XRI,
				       &ext_cnt, &ext_size);
	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"Count %3d, Size %3d\n", ext_cnt, ext_size);

	return len;
}

/**
 * lpfc_idiag_extacc_alloc_get - get the allocated extents information
 * @phba: pointer to lpfc hba data structure.
 * @pbuffer: pointer to internal buffer.
 * @len: length into the internal buffer data has been copied.
 *
 * Description:
 * This routine is to get the allocated extent information.
 *
 * Returns:
 * overall lenth of the data read into the internal buffer.
 **/
static int
lpfc_idiag_extacc_alloc_get(struct lpfc_hba *phba, char *pbuffer, int len)
{
	uint16_t ext_cnt, ext_size;
	int rc;

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\nAllocated Extents Information:\n");

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\tHost Allocated VPI extents: ");
	rc = lpfc_sli4_get_allocated_extnts(phba, LPFC_RSC_TYPE_FCOE_VPI,
					    &ext_cnt, &ext_size);
	if (!rc)
		len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
				"Port %d Extent %3d, Size %3d\n",
				phba->brd_no, ext_cnt, ext_size);
	else
		len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
				"N/A\n");

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\tHost Allocated VFI extents: ");
	rc = lpfc_sli4_get_allocated_extnts(phba, LPFC_RSC_TYPE_FCOE_VFI,
					    &ext_cnt, &ext_size);
	if (!rc)
		len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
				"Port %d Extent %3d, Size %3d\n",
				phba->brd_no, ext_cnt, ext_size);
	else
		len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
				"N/A\n");

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\tHost Allocated RPI extents: ");
	rc = lpfc_sli4_get_allocated_extnts(phba, LPFC_RSC_TYPE_FCOE_RPI,
					    &ext_cnt, &ext_size);
	if (!rc)
		len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
				"Port %d Extent %3d, Size %3d\n",
				phba->brd_no, ext_cnt, ext_size);
	else
		len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
				"N/A\n");

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\tHost Allocated XRI extents: ");
	rc = lpfc_sli4_get_allocated_extnts(phba, LPFC_RSC_TYPE_FCOE_XRI,
					    &ext_cnt, &ext_size);
	if (!rc)
		len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
				"Port %d Extent %3d, Size %3d\n",
				phba->brd_no, ext_cnt, ext_size);
	else
		len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
				"N/A\n");

	return len;
}

/**
 * lpfc_idiag_extacc_drivr_get - get driver extent information
 * @phba: pointer to lpfc hba data structure.
 * @pbuffer: pointer to internal buffer.
 * @len: length into the internal buffer data has been copied.
 *
 * Description:
 * This routine is to get the driver extent information.
 *
 * Returns:
 * overall lenth of the data read into the internal buffer.
 **/
static int
lpfc_idiag_extacc_drivr_get(struct lpfc_hba *phba, char *pbuffer, int len)
{
	struct lpfc_rsrc_blks *rsrc_blks;
	int index;

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\nDriver Extents Information:\n");

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\tVPI extents:\n");
	index = 0;
	list_for_each_entry(rsrc_blks, &phba->lpfc_vpi_blk_list, list) {
		len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
				"\t\tBlock %3d: Start %4d, Count %4d\n",
				index, rsrc_blks->rsrc_start,
				rsrc_blks->rsrc_size);
		index++;
	}
	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\tVFI extents:\n");
	index = 0;
	list_for_each_entry(rsrc_blks, &phba->sli4_hba.lpfc_vfi_blk_list,
			    list) {
		len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
				"\t\tBlock %3d: Start %4d, Count %4d\n",
				index, rsrc_blks->rsrc_start,
				rsrc_blks->rsrc_size);
		index++;
	}

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\tRPI extents:\n");
	index = 0;
	list_for_each_entry(rsrc_blks, &phba->sli4_hba.lpfc_rpi_blk_list,
			    list) {
		len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
				"\t\tBlock %3d: Start %4d, Count %4d\n",
				index, rsrc_blks->rsrc_start,
				rsrc_blks->rsrc_size);
		index++;
	}

	len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
			"\tXRI extents:\n");
	index = 0;
	list_for_each_entry(rsrc_blks, &phba->sli4_hba.lpfc_xri_blk_list,
			    list) {
		len += scnprintf(pbuffer+len, LPFC_EXT_ACC_BUF_SIZE-len,
				"\t\tBlock %3d: Start %4d, Count %4d\n",
				index, rsrc_blks->rsrc_start,
				rsrc_blks->rsrc_size);
		index++;
	}

	return len;
}

/**
 * lpfc_idiag_extacc_write - Syntax check and set up idiag extacc commands
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the user data from.
 * @nbytes: The number of bytes to get.
 * @ppos: The position in the file to start reading from.
 *
 * This routine get the debugfs idiag command struct from user space and then
 * perform the syntax check for extent information access commands and sets
 * up the necessary states in the idiag command struct accordingly.
 *
 * It returns the @nbytges passing in from debugfs user space when successful.
 * In case of error conditions, it returns proper error code back to the user
 * space.
 **/
static ssize_t
lpfc_idiag_extacc_write(struct file *file, const char __user *buf,
			size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	uint32_t ext_map;
	int rc;

	/* This is a user write operation */
	debug->op = LPFC_IDIAG_OP_WR;

	rc = lpfc_idiag_cmd_get(buf, nbytes, &idiag.cmd);
	if (rc < 0)
		return rc;

	ext_map = idiag.cmd.data[IDIAG_EXTACC_EXMAP_INDX];

	if (idiag.cmd.opcode != LPFC_IDIAG_CMD_EXTACC_RD)
		goto error_out;
	if (rc != LPFC_EXT_ACC_CMD_ARG)
		goto error_out;
	if (!(ext_map & LPFC_EXT_ACC_ALL))
		goto error_out;

	return nbytes;
error_out:
	/* Clean out command structure on command error out */
	memset(&idiag, 0, sizeof(idiag));
	return -EINVAL;
}

/**
 * lpfc_idiag_extacc_read - idiag debugfs read access to extent information
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the data to.
 * @nbytes: The number of bytes to read.
 * @ppos: The position in the file to start reading from.
 *
 * Description:
 * This routine reads data from the proper extent information according to
 * the idiag command, and copies to user @buf.
 *
 * Returns:
 * This function returns the amount of data that was read (this could be less
 * than @nbytes if the end of the file was reached) or a negative error value.
 **/
static ssize_t
lpfc_idiag_extacc_read(struct file *file, char __user *buf, size_t nbytes,
		       loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	struct lpfc_hba *phba = (struct lpfc_hba *)debug->i_private;
	char *pbuffer;
	uint32_t ext_map;
	int len = 0;

	/* This is a user read operation */
	debug->op = LPFC_IDIAG_OP_RD;

	if (!debug->buffer)
		debug->buffer = kmalloc(LPFC_EXT_ACC_BUF_SIZE, GFP_KERNEL);
	if (!debug->buffer)
		return 0;
	pbuffer = debug->buffer;
	if (*ppos)
		return 0;
	if (idiag.cmd.opcode != LPFC_IDIAG_CMD_EXTACC_RD)
		return 0;

	ext_map = idiag.cmd.data[IDIAG_EXTACC_EXMAP_INDX];
	if (ext_map & LPFC_EXT_ACC_AVAIL)
		len = lpfc_idiag_extacc_avail_get(phba, pbuffer, len);
	if (ext_map & LPFC_EXT_ACC_ALLOC)
		len = lpfc_idiag_extacc_alloc_get(phba, pbuffer, len);
	if (ext_map & LPFC_EXT_ACC_DRIVR)
		len = lpfc_idiag_extacc_drivr_get(phba, pbuffer, len);

	return simple_read_from_buffer(buf, nbytes, ppos, pbuffer, len);
}

#undef lpfc_debugfs_op_disc_trc
static const struct file_operations lpfc_debugfs_op_disc_trc = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_disc_trc_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_nodelist
static const struct file_operations lpfc_debugfs_op_nodelist = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_nodelist_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_multixripools
static const struct file_operations lpfc_debugfs_op_multixripools = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_multixripools_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.write =	lpfc_debugfs_multixripools_write,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_hbqinfo
static const struct file_operations lpfc_debugfs_op_hbqinfo = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_hbqinfo_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.release =      lpfc_debugfs_release,
};

#ifdef LPFC_HDWQ_LOCK_STAT
#undef lpfc_debugfs_op_lockstat
static const struct file_operations lpfc_debugfs_op_lockstat = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_lockstat_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.write =        lpfc_debugfs_lockstat_write,
	.release =      lpfc_debugfs_release,
};
#endif

#undef lpfc_debugfs_op_dumpHBASlim
static const struct file_operations lpfc_debugfs_op_dumpHBASlim = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_dumpHBASlim_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_dumpHostSlim
static const struct file_operations lpfc_debugfs_op_dumpHostSlim = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_dumpHostSlim_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_nvmestat
static const struct file_operations lpfc_debugfs_op_nvmestat = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_nvmestat_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.write =	lpfc_debugfs_nvmestat_write,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_scsistat
static const struct file_operations lpfc_debugfs_op_scsistat = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_scsistat_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.write =	lpfc_debugfs_scsistat_write,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_nvmektime
static const struct file_operations lpfc_debugfs_op_nvmektime = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_nvmektime_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.write =	lpfc_debugfs_nvmektime_write,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_nvmeio_trc
static const struct file_operations lpfc_debugfs_op_nvmeio_trc = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_nvmeio_trc_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.write =	lpfc_debugfs_nvmeio_trc_write,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_cpucheck
static const struct file_operations lpfc_debugfs_op_cpucheck = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_cpucheck_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.write =	lpfc_debugfs_cpucheck_write,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_dumpData
static const struct file_operations lpfc_debugfs_op_dumpData = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_dumpData_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.write =	lpfc_debugfs_dumpDataDif_write,
	.release =      lpfc_debugfs_dumpDataDif_release,
};

#undef lpfc_debugfs_op_dumpDif
static const struct file_operations lpfc_debugfs_op_dumpDif = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_dumpDif_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.write =	lpfc_debugfs_dumpDataDif_write,
	.release =      lpfc_debugfs_dumpDataDif_release,
};

#undef lpfc_debugfs_op_dif_err
static const struct file_operations lpfc_debugfs_op_dif_err = {
	.owner =	THIS_MODULE,
	.open =		simple_open,
	.llseek =	lpfc_debugfs_lseek,
	.read =		lpfc_debugfs_dif_err_read,
	.write =	lpfc_debugfs_dif_err_write,
	.release =	lpfc_debugfs_dif_err_release,
};

#undef lpfc_debugfs_op_slow_ring_trc
static const struct file_operations lpfc_debugfs_op_slow_ring_trc = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_slow_ring_trc_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.release =      lpfc_debugfs_release,
};

static struct dentry *lpfc_debugfs_root = NULL;
static atomic_t lpfc_debugfs_hba_count;

/*
 * File operations for the iDiag debugfs
 */
#undef lpfc_idiag_op_pciCfg
static const struct file_operations lpfc_idiag_op_pciCfg = {
	.owner =        THIS_MODULE,
	.open =         lpfc_idiag_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_idiag_pcicfg_read,
	.write =        lpfc_idiag_pcicfg_write,
	.release =      lpfc_idiag_cmd_release,
};

#undef lpfc_idiag_op_barAcc
static const struct file_operations lpfc_idiag_op_barAcc = {
	.owner =        THIS_MODULE,
	.open =         lpfc_idiag_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_idiag_baracc_read,
	.write =        lpfc_idiag_baracc_write,
	.release =      lpfc_idiag_cmd_release,
};

#undef lpfc_idiag_op_queInfo
static const struct file_operations lpfc_idiag_op_queInfo = {
	.owner =        THIS_MODULE,
	.open =         lpfc_idiag_open,
	.read =         lpfc_idiag_queinfo_read,
	.release =      lpfc_idiag_release,
};

#undef lpfc_idiag_op_queAcc
static const struct file_operations lpfc_idiag_op_queAcc = {
	.owner =        THIS_MODULE,
	.open =         lpfc_idiag_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_idiag_queacc_read,
	.write =        lpfc_idiag_queacc_write,
	.release =      lpfc_idiag_cmd_release,
};

#undef lpfc_idiag_op_drbAcc
static const struct file_operations lpfc_idiag_op_drbAcc = {
	.owner =        THIS_MODULE,
	.open =         lpfc_idiag_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_idiag_drbacc_read,
	.write =        lpfc_idiag_drbacc_write,
	.release =      lpfc_idiag_cmd_release,
};

#undef lpfc_idiag_op_ctlAcc
static const struct file_operations lpfc_idiag_op_ctlAcc = {
	.owner =        THIS_MODULE,
	.open =         lpfc_idiag_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_idiag_ctlacc_read,
	.write =        lpfc_idiag_ctlacc_write,
	.release =      lpfc_idiag_cmd_release,
};

#undef lpfc_idiag_op_mbxAcc
static const struct file_operations lpfc_idiag_op_mbxAcc = {
	.owner =        THIS_MODULE,
	.open =         lpfc_idiag_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_idiag_mbxacc_read,
	.write =        lpfc_idiag_mbxacc_write,
	.release =      lpfc_idiag_cmd_release,
};

#undef lpfc_idiag_op_extAcc
static const struct file_operations lpfc_idiag_op_extAcc = {
	.owner =        THIS_MODULE,
	.open =         lpfc_idiag_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_idiag_extacc_read,
	.write =        lpfc_idiag_extacc_write,
	.release =      lpfc_idiag_cmd_release,
};

#endif

/* lpfc_idiag_mbxacc_dump_bsg_mbox - idiag debugfs dump bsg mailbox command
 * @phba: Pointer to HBA context object.
 * @dmabuf: Pointer to a DMA buffer descriptor.
 *
 * Description:
 * This routine dump a bsg pass-through non-embedded mailbox command with
 * external buffer.
 **/
void
lpfc_idiag_mbxacc_dump_bsg_mbox(struct lpfc_hba *phba, enum nemb_type nemb_tp,
				enum mbox_type mbox_tp, enum dma_type dma_tp,
				enum sta_type sta_tp,
				struct lpfc_dmabuf *dmabuf, uint32_t ext_buf)
{
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint32_t *mbx_mbox_cmd, *mbx_dump_map, *mbx_dump_cnt, *mbx_word_cnt;
	char line_buf[LPFC_MBX_ACC_LBUF_SZ];
	int len = 0;
	uint32_t do_dump = 0;
	uint32_t *pword;
	uint32_t i;

	if (idiag.cmd.opcode != LPFC_IDIAG_BSG_MBXACC_DP)
		return;

	mbx_mbox_cmd = &idiag.cmd.data[IDIAG_MBXACC_MBCMD_INDX];
	mbx_dump_map = &idiag.cmd.data[IDIAG_MBXACC_DPMAP_INDX];
	mbx_dump_cnt = &idiag.cmd.data[IDIAG_MBXACC_DPCNT_INDX];
	mbx_word_cnt = &idiag.cmd.data[IDIAG_MBXACC_WDCNT_INDX];

	if (!(*mbx_dump_map & LPFC_MBX_DMP_ALL) ||
	    (*mbx_dump_cnt == 0) ||
	    (*mbx_word_cnt == 0))
		return;

	if (*mbx_mbox_cmd != 0x9B)
		return;

	if ((mbox_tp == mbox_rd) && (dma_tp == dma_mbox)) {
		if (*mbx_dump_map & LPFC_BSG_DMP_MBX_RD_MBX) {
			do_dump |= LPFC_BSG_DMP_MBX_RD_MBX;
			pr_err("\nRead mbox command (x%x), "
			       "nemb:0x%x, extbuf_cnt:%d:\n",
			       sta_tp, nemb_tp, ext_buf);
		}
	}
	if ((mbox_tp == mbox_rd) && (dma_tp == dma_ebuf)) {
		if (*mbx_dump_map & LPFC_BSG_DMP_MBX_RD_BUF) {
			do_dump |= LPFC_BSG_DMP_MBX_RD_BUF;
			pr_err("\nRead mbox buffer (x%x), "
			       "nemb:0x%x, extbuf_seq:%d:\n",
			       sta_tp, nemb_tp, ext_buf);
		}
	}
	if ((mbox_tp == mbox_wr) && (dma_tp == dma_mbox)) {
		if (*mbx_dump_map & LPFC_BSG_DMP_MBX_WR_MBX) {
			do_dump |= LPFC_BSG_DMP_MBX_WR_MBX;
			pr_err("\nWrite mbox command (x%x), "
			       "nemb:0x%x, extbuf_cnt:%d:\n",
			       sta_tp, nemb_tp, ext_buf);
		}
	}
	if ((mbox_tp == mbox_wr) && (dma_tp == dma_ebuf)) {
		if (*mbx_dump_map & LPFC_BSG_DMP_MBX_WR_BUF) {
			do_dump |= LPFC_BSG_DMP_MBX_WR_BUF;
			pr_err("\nWrite mbox buffer (x%x), "
			       "nemb:0x%x, extbuf_seq:%d:\n",
			       sta_tp, nemb_tp, ext_buf);
		}
	}

	/* dump buffer content */
	if (do_dump) {
		pword = (uint32_t *)dmabuf->virt;
		for (i = 0; i < *mbx_word_cnt; i++) {
			if (!(i % 8)) {
				if (i != 0)
					pr_err("%s\n", line_buf);
				len = 0;
				len += scnprintf(line_buf+len,
						LPFC_MBX_ACC_LBUF_SZ-len,
						"%03d: ", i);
			}
			len += scnprintf(line_buf+len, LPFC_MBX_ACC_LBUF_SZ-len,
					"%08x ", (uint32_t)*pword);
			pword++;
		}
		if ((i - 1) % 8)
			pr_err("%s\n", line_buf);
		(*mbx_dump_cnt)--;
	}

	/* Clean out command structure on reaching dump count */
	if (*mbx_dump_cnt == 0)
		memset(&idiag, 0, sizeof(idiag));
	return;
#endif
}

/* lpfc_idiag_mbxacc_dump_issue_mbox - idiag debugfs dump issue mailbox command
 * @phba: Pointer to HBA context object.
 * @dmabuf: Pointer to a DMA buffer descriptor.
 *
 * Description:
 * This routine dump a pass-through non-embedded mailbox command from issue
 * mailbox command.
 **/
void
lpfc_idiag_mbxacc_dump_issue_mbox(struct lpfc_hba *phba, MAILBOX_t *pmbox)
{
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint32_t *mbx_dump_map, *mbx_dump_cnt, *mbx_word_cnt, *mbx_mbox_cmd;
	char line_buf[LPFC_MBX_ACC_LBUF_SZ];
	int len = 0;
	uint32_t *pword;
	uint8_t *pbyte;
	uint32_t i, j;

	if (idiag.cmd.opcode != LPFC_IDIAG_CMD_MBXACC_DP)
		return;

	mbx_mbox_cmd = &idiag.cmd.data[IDIAG_MBXACC_MBCMD_INDX];
	mbx_dump_map = &idiag.cmd.data[IDIAG_MBXACC_DPMAP_INDX];
	mbx_dump_cnt = &idiag.cmd.data[IDIAG_MBXACC_DPCNT_INDX];
	mbx_word_cnt = &idiag.cmd.data[IDIAG_MBXACC_WDCNT_INDX];

	if (!(*mbx_dump_map & LPFC_MBX_DMP_MBX_ALL) ||
	    (*mbx_dump_cnt == 0) ||
	    (*mbx_word_cnt == 0))
		return;

	if ((*mbx_mbox_cmd != LPFC_MBX_ALL_CMD) &&
	    (*mbx_mbox_cmd != pmbox->mbxCommand))
		return;

	/* dump buffer content */
	if (*mbx_dump_map & LPFC_MBX_DMP_MBX_WORD) {
		pr_err("Mailbox command:0x%x dump by word:\n",
		       pmbox->mbxCommand);
		pword = (uint32_t *)pmbox;
		for (i = 0; i < *mbx_word_cnt; i++) {
			if (!(i % 8)) {
				if (i != 0)
					pr_err("%s\n", line_buf);
				len = 0;
				memset(line_buf, 0, LPFC_MBX_ACC_LBUF_SZ);
				len += scnprintf(line_buf+len,
						LPFC_MBX_ACC_LBUF_SZ-len,
						"%03d: ", i);
			}
			len += scnprintf(line_buf+len, LPFC_MBX_ACC_LBUF_SZ-len,
					"%08x ",
					((uint32_t)*pword) & 0xffffffff);
			pword++;
		}
		if ((i - 1) % 8)
			pr_err("%s\n", line_buf);
		pr_err("\n");
	}
	if (*mbx_dump_map & LPFC_MBX_DMP_MBX_BYTE) {
		pr_err("Mailbox command:0x%x dump by byte:\n",
		       pmbox->mbxCommand);
		pbyte = (uint8_t *)pmbox;
		for (i = 0; i < *mbx_word_cnt; i++) {
			if (!(i % 8)) {
				if (i != 0)
					pr_err("%s\n", line_buf);
				len = 0;
				memset(line_buf, 0, LPFC_MBX_ACC_LBUF_SZ);
				len += scnprintf(line_buf+len,
						LPFC_MBX_ACC_LBUF_SZ-len,
						"%03d: ", i);
			}
			for (j = 0; j < 4; j++) {
				len += scnprintf(line_buf+len,
						LPFC_MBX_ACC_LBUF_SZ-len,
						"%02x",
						((uint8_t)*pbyte) & 0xff);
				pbyte++;
			}
			len += scnprintf(line_buf+len,
					LPFC_MBX_ACC_LBUF_SZ-len, " ");
		}
		if ((i - 1) % 8)
			pr_err("%s\n", line_buf);
		pr_err("\n");
	}
	(*mbx_dump_cnt)--;

	/* Clean out command structure on reaching dump count */
	if (*mbx_dump_cnt == 0)
		memset(&idiag, 0, sizeof(idiag));
	return;
#endif
}

/**
 * lpfc_debugfs_initialize - Initialize debugfs for a vport
 * @vport: The vport pointer to initialize.
 *
 * Description:
 * When Debugfs is configured this routine sets up the lpfc debugfs file system.
 * If not already created, this routine will create the lpfc directory, and
 * lpfcX directory (for this HBA), and vportX directory for this vport. It will
 * also create each file used to access lpfc specific debugfs information.
 **/
inline void
lpfc_debugfs_initialize(struct lpfc_vport *vport)
{
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	struct lpfc_hba   *phba = vport->phba;
	char name[64];
	uint32_t num, i;
	bool pport_setup = false;

	if (!lpfc_debugfs_enable)
		return;

	/* Setup lpfc root directory */
	if (!lpfc_debugfs_root) {
		lpfc_debugfs_root = debugfs_create_dir("lpfc", NULL);
		atomic_set(&lpfc_debugfs_hba_count, 0);
	}
	if (!lpfc_debugfs_start_time)
		lpfc_debugfs_start_time = jiffies;

	/* Setup funcX directory for specific HBA PCI function */
	snprintf(name, sizeof(name), "fn%d", phba->brd_no);
	if (!phba->hba_debugfs_root) {
		pport_setup = true;
		phba->hba_debugfs_root =
			debugfs_create_dir(name, lpfc_debugfs_root);
		atomic_inc(&lpfc_debugfs_hba_count);
		atomic_set(&phba->debugfs_vport_count, 0);

		/* Multi-XRI pools */
		snprintf(name, sizeof(name), "multixripools");
		phba->debug_multixri_pools =
			debugfs_create_file(name, S_IFREG | 0644,
					    phba->hba_debugfs_root,
					    phba,
					    &lpfc_debugfs_op_multixripools);
		if (!phba->debug_multixri_pools) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "0527 Cannot create debugfs multixripools\n");
			goto debug_failed;
		}

		/* Setup hbqinfo */
		snprintf(name, sizeof(name), "hbqinfo");
		phba->debug_hbqinfo =
			debugfs_create_file(name, S_IFREG | 0644,
					    phba->hba_debugfs_root,
					    phba, &lpfc_debugfs_op_hbqinfo);

#ifdef LPFC_HDWQ_LOCK_STAT
		/* Setup lockstat */
		snprintf(name, sizeof(name), "lockstat");
		phba->debug_lockstat =
			debugfs_create_file(name, S_IFREG | 0644,
					    phba->hba_debugfs_root,
					    phba, &lpfc_debugfs_op_lockstat);
		if (!phba->debug_lockstat) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "4610 Cant create debugfs lockstat\n");
			goto debug_failed;
		}
#endif

		/* Setup dumpHBASlim */
		if (phba->sli_rev < LPFC_SLI_REV4) {
			snprintf(name, sizeof(name), "dumpHBASlim");
			phba->debug_dumpHBASlim =
				debugfs_create_file(name,
					S_IFREG|S_IRUGO|S_IWUSR,
					phba->hba_debugfs_root,
					phba, &lpfc_debugfs_op_dumpHBASlim);
		} else
			phba->debug_dumpHBASlim = NULL;

		/* Setup dumpHostSlim */
		if (phba->sli_rev < LPFC_SLI_REV4) {
			snprintf(name, sizeof(name), "dumpHostSlim");
			phba->debug_dumpHostSlim =
				debugfs_create_file(name,
					S_IFREG|S_IRUGO|S_IWUSR,
					phba->hba_debugfs_root,
					phba, &lpfc_debugfs_op_dumpHostSlim);
		} else
			phba->debug_dumpHostSlim = NULL;

		/* Setup dumpData */
		snprintf(name, sizeof(name), "dumpData");
		phba->debug_dumpData =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				 phba->hba_debugfs_root,
				 phba, &lpfc_debugfs_op_dumpData);

		/* Setup dumpDif */
		snprintf(name, sizeof(name), "dumpDif");
		phba->debug_dumpDif =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				 phba->hba_debugfs_root,
				 phba, &lpfc_debugfs_op_dumpDif);

		/* Setup DIF Error Injections */
		snprintf(name, sizeof(name), "InjErrLBA");
		phba->debug_InjErrLBA =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
			phba->hba_debugfs_root,
			phba, &lpfc_debugfs_op_dif_err);
		phba->lpfc_injerr_lba = LPFC_INJERR_LBA_OFF;

		snprintf(name, sizeof(name), "InjErrNPortID");
		phba->debug_InjErrNPortID =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
			phba->hba_debugfs_root,
			phba, &lpfc_debugfs_op_dif_err);

		snprintf(name, sizeof(name), "InjErrWWPN");
		phba->debug_InjErrWWPN =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
			phba->hba_debugfs_root,
			phba, &lpfc_debugfs_op_dif_err);

		snprintf(name, sizeof(name), "writeGuardInjErr");
		phba->debug_writeGuard =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
			phba->hba_debugfs_root,
			phba, &lpfc_debugfs_op_dif_err);

		snprintf(name, sizeof(name), "writeAppInjErr");
		phba->debug_writeApp =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
			phba->hba_debugfs_root,
			phba, &lpfc_debugfs_op_dif_err);

		snprintf(name, sizeof(name), "writeRefInjErr");
		phba->debug_writeRef =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
			phba->hba_debugfs_root,
			phba, &lpfc_debugfs_op_dif_err);

		snprintf(name, sizeof(name), "readGuardInjErr");
		phba->debug_readGuard =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
			phba->hba_debugfs_root,
			phba, &lpfc_debugfs_op_dif_err);

		snprintf(name, sizeof(name), "readAppInjErr");
		phba->debug_readApp =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
			phba->hba_debugfs_root,
			phba, &lpfc_debugfs_op_dif_err);

		snprintf(name, sizeof(name), "readRefInjErr");
		phba->debug_readRef =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
			phba->hba_debugfs_root,
			phba, &lpfc_debugfs_op_dif_err);

		/* Setup slow ring trace */
		if (lpfc_debugfs_max_slow_ring_trc) {
			num = lpfc_debugfs_max_slow_ring_trc - 1;
			if (num & lpfc_debugfs_max_slow_ring_trc) {
				/* Change to be a power of 2 */
				num = lpfc_debugfs_max_slow_ring_trc;
				i = 0;
				while (num > 1) {
					num = num >> 1;
					i++;
				}
				lpfc_debugfs_max_slow_ring_trc = (1 << i);
				pr_err("lpfc_debugfs_max_disc_trc changed to "
				       "%d\n", lpfc_debugfs_max_disc_trc);
			}
		}

		snprintf(name, sizeof(name), "slow_ring_trace");
		phba->debug_slow_ring_trc =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				 phba->hba_debugfs_root,
				 phba, &lpfc_debugfs_op_slow_ring_trc);
		if (!phba->slow_ring_trc) {
			phba->slow_ring_trc = kmalloc(
				(sizeof(struct lpfc_debugfs_trc) *
				lpfc_debugfs_max_slow_ring_trc),
				GFP_KERNEL);
			if (!phba->slow_ring_trc) {
				lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
						 "0416 Cannot create debugfs "
						 "slow_ring buffer\n");
				goto debug_failed;
			}
			atomic_set(&phba->slow_ring_trc_cnt, 0);
			memset(phba->slow_ring_trc, 0,
				(sizeof(struct lpfc_debugfs_trc) *
				lpfc_debugfs_max_slow_ring_trc));
		}

		snprintf(name, sizeof(name), "nvmeio_trc");
		phba->debug_nvmeio_trc =
			debugfs_create_file(name, 0644,
					    phba->hba_debugfs_root,
					    phba, &lpfc_debugfs_op_nvmeio_trc);

		atomic_set(&phba->nvmeio_trc_cnt, 0);
		if (lpfc_debugfs_max_nvmeio_trc) {
			num = lpfc_debugfs_max_nvmeio_trc - 1;
			if (num & lpfc_debugfs_max_disc_trc) {
				/* Change to be a power of 2 */
				num = lpfc_debugfs_max_nvmeio_trc;
				i = 0;
				while (num > 1) {
					num = num >> 1;
					i++;
				}
				lpfc_debugfs_max_nvmeio_trc = (1 << i);
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"0575 lpfc_debugfs_max_nvmeio_trc "
						"changed to %d\n",
						lpfc_debugfs_max_nvmeio_trc);
			}
			phba->nvmeio_trc_size = lpfc_debugfs_max_nvmeio_trc;

			/* Allocate trace buffer and initialize */
			phba->nvmeio_trc = kzalloc(
				(sizeof(struct lpfc_debugfs_nvmeio_trc) *
				phba->nvmeio_trc_size), GFP_KERNEL);

			if (!phba->nvmeio_trc) {
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"0576 Cannot create debugfs "
						"nvmeio_trc buffer\n");
				goto nvmeio_off;
			}
			phba->nvmeio_trc_on = 1;
			phba->nvmeio_trc_output_idx = 0;
			phba->nvmeio_trc = NULL;
		} else {
nvmeio_off:
			phba->nvmeio_trc_size = 0;
			phba->nvmeio_trc_on = 0;
			phba->nvmeio_trc_output_idx = 0;
			phba->nvmeio_trc = NULL;
		}
	}

	snprintf(name, sizeof(name), "vport%d", vport->vpi);
	if (!vport->vport_debugfs_root) {
		vport->vport_debugfs_root =
			debugfs_create_dir(name, phba->hba_debugfs_root);
		atomic_inc(&phba->debugfs_vport_count);
	}

	if (lpfc_debugfs_max_disc_trc) {
		num = lpfc_debugfs_max_disc_trc - 1;
		if (num & lpfc_debugfs_max_disc_trc) {
			/* Change to be a power of 2 */
			num = lpfc_debugfs_max_disc_trc;
			i = 0;
			while (num > 1) {
				num = num >> 1;
				i++;
			}
			lpfc_debugfs_max_disc_trc = (1 << i);
			pr_err("lpfc_debugfs_max_disc_trc changed to %d\n",
			       lpfc_debugfs_max_disc_trc);
		}
	}

	vport->disc_trc = kzalloc(
		(sizeof(struct lpfc_debugfs_trc) * lpfc_debugfs_max_disc_trc),
		GFP_KERNEL);

	if (!vport->disc_trc) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "0418 Cannot create debugfs disc trace "
				 "buffer\n");
		goto debug_failed;
	}
	atomic_set(&vport->disc_trc_cnt, 0);

	snprintf(name, sizeof(name), "discovery_trace");
	vport->debug_disc_trc =
		debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				 vport->vport_debugfs_root,
				 vport, &lpfc_debugfs_op_disc_trc);
	snprintf(name, sizeof(name), "nodelist");
	vport->debug_nodelist =
		debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				 vport->vport_debugfs_root,
				 vport, &lpfc_debugfs_op_nodelist);

	snprintf(name, sizeof(name), "nvmestat");
	vport->debug_nvmestat =
		debugfs_create_file(name, 0644,
				    vport->vport_debugfs_root,
				    vport, &lpfc_debugfs_op_nvmestat);

	snprintf(name, sizeof(name), "scsistat");
	vport->debug_scsistat =
		debugfs_create_file(name, 0644,
				    vport->vport_debugfs_root,
				    vport, &lpfc_debugfs_op_scsistat);
	if (!vport->debug_scsistat) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "4611 Cannot create debugfs scsistat\n");
		goto debug_failed;
	}

	snprintf(name, sizeof(name), "nvmektime");
	vport->debug_nvmektime =
		debugfs_create_file(name, 0644,
				    vport->vport_debugfs_root,
				    vport, &lpfc_debugfs_op_nvmektime);

	snprintf(name, sizeof(name), "cpucheck");
	vport->debug_cpucheck =
		debugfs_create_file(name, 0644,
				    vport->vport_debugfs_root,
				    vport, &lpfc_debugfs_op_cpucheck);

	/*
	 * The following section is for additional directories/files for the
	 * physical port.
	 */

	if (!pport_setup)
		goto debug_failed;

	/*
	 * iDiag debugfs root entry points for SLI4 device only
	 */
	if (phba->sli_rev < LPFC_SLI_REV4)
		goto debug_failed;

	snprintf(name, sizeof(name), "iDiag");
	if (!phba->idiag_root) {
		phba->idiag_root =
			debugfs_create_dir(name, phba->hba_debugfs_root);
		/* Initialize iDiag data structure */
		memset(&idiag, 0, sizeof(idiag));
	}

	/* iDiag read PCI config space */
	snprintf(name, sizeof(name), "pciCfg");
	if (!phba->idiag_pci_cfg) {
		phba->idiag_pci_cfg =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				phba->idiag_root, phba, &lpfc_idiag_op_pciCfg);
		idiag.offset.last_rd = 0;
	}

	/* iDiag PCI BAR access */
	snprintf(name, sizeof(name), "barAcc");
	if (!phba->idiag_bar_acc) {
		phba->idiag_bar_acc =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				phba->idiag_root, phba, &lpfc_idiag_op_barAcc);
		idiag.offset.last_rd = 0;
	}

	/* iDiag get PCI function queue information */
	snprintf(name, sizeof(name), "queInfo");
	if (!phba->idiag_que_info) {
		phba->idiag_que_info =
			debugfs_create_file(name, S_IFREG|S_IRUGO,
			phba->idiag_root, phba, &lpfc_idiag_op_queInfo);
	}

	/* iDiag access PCI function queue */
	snprintf(name, sizeof(name), "queAcc");
	if (!phba->idiag_que_acc) {
		phba->idiag_que_acc =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				phba->idiag_root, phba, &lpfc_idiag_op_queAcc);
	}

	/* iDiag access PCI function doorbell registers */
	snprintf(name, sizeof(name), "drbAcc");
	if (!phba->idiag_drb_acc) {
		phba->idiag_drb_acc =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				phba->idiag_root, phba, &lpfc_idiag_op_drbAcc);
	}

	/* iDiag access PCI function control registers */
	snprintf(name, sizeof(name), "ctlAcc");
	if (!phba->idiag_ctl_acc) {
		phba->idiag_ctl_acc =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				phba->idiag_root, phba, &lpfc_idiag_op_ctlAcc);
	}

	/* iDiag access mbox commands */
	snprintf(name, sizeof(name), "mbxAcc");
	if (!phba->idiag_mbx_acc) {
		phba->idiag_mbx_acc =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				phba->idiag_root, phba, &lpfc_idiag_op_mbxAcc);
	}

	/* iDiag extents access commands */
	if (phba->sli4_hba.extents_in_use) {
		snprintf(name, sizeof(name), "extAcc");
		if (!phba->idiag_ext_acc) {
			phba->idiag_ext_acc =
				debugfs_create_file(name,
						    S_IFREG|S_IRUGO|S_IWUSR,
						    phba->idiag_root, phba,
						    &lpfc_idiag_op_extAcc);
		}
	}

debug_failed:
	return;
#endif
}

/**
 * lpfc_debugfs_terminate -  Tear down debugfs infrastructure for this vport
 * @vport: The vport pointer to remove from debugfs.
 *
 * Description:
 * When Debugfs is configured this routine removes debugfs file system elements
 * that are specific to this vport. It also checks to see if there are any
 * users left for the debugfs directories associated with the HBA and driver. If
 * this is the last user of the HBA directory or driver directory then it will
 * remove those from the debugfs infrastructure as well.
 **/
inline void
lpfc_debugfs_terminate(struct lpfc_vport *vport)
{
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	struct lpfc_hba   *phba = vport->phba;

	kfree(vport->disc_trc);
	vport->disc_trc = NULL;

	debugfs_remove(vport->debug_disc_trc); /* discovery_trace */
	vport->debug_disc_trc = NULL;

	debugfs_remove(vport->debug_nodelist); /* nodelist */
	vport->debug_nodelist = NULL;

	debugfs_remove(vport->debug_nvmestat); /* nvmestat */
	vport->debug_nvmestat = NULL;

	debugfs_remove(vport->debug_scsistat); /* scsistat */
	vport->debug_scsistat = NULL;

	debugfs_remove(vport->debug_nvmektime); /* nvmektime */
	vport->debug_nvmektime = NULL;

	debugfs_remove(vport->debug_cpucheck); /* cpucheck */
	vport->debug_cpucheck = NULL;

	if (vport->vport_debugfs_root) {
		debugfs_remove(vport->vport_debugfs_root); /* vportX */
		vport->vport_debugfs_root = NULL;
		atomic_dec(&phba->debugfs_vport_count);
	}

	if (atomic_read(&phba->debugfs_vport_count) == 0) {

		debugfs_remove(phba->debug_multixri_pools); /* multixripools*/
		phba->debug_multixri_pools = NULL;

		debugfs_remove(phba->debug_hbqinfo); /* hbqinfo */
		phba->debug_hbqinfo = NULL;

#ifdef LPFC_HDWQ_LOCK_STAT
		debugfs_remove(phba->debug_lockstat); /* lockstat */
		phba->debug_lockstat = NULL;
#endif
		debugfs_remove(phba->debug_dumpHBASlim); /* HBASlim */
		phba->debug_dumpHBASlim = NULL;

		debugfs_remove(phba->debug_dumpHostSlim); /* HostSlim */
		phba->debug_dumpHostSlim = NULL;

		debugfs_remove(phba->debug_dumpData); /* dumpData */
		phba->debug_dumpData = NULL;

		debugfs_remove(phba->debug_dumpDif); /* dumpDif */
		phba->debug_dumpDif = NULL;

		debugfs_remove(phba->debug_InjErrLBA); /* InjErrLBA */
		phba->debug_InjErrLBA = NULL;

		debugfs_remove(phba->debug_InjErrNPortID);
		phba->debug_InjErrNPortID = NULL;

		debugfs_remove(phba->debug_InjErrWWPN); /* InjErrWWPN */
		phba->debug_InjErrWWPN = NULL;

		debugfs_remove(phba->debug_writeGuard); /* writeGuard */
		phba->debug_writeGuard = NULL;

		debugfs_remove(phba->debug_writeApp); /* writeApp */
		phba->debug_writeApp = NULL;

		debugfs_remove(phba->debug_writeRef); /* writeRef */
		phba->debug_writeRef = NULL;

		debugfs_remove(phba->debug_readGuard); /* readGuard */
		phba->debug_readGuard = NULL;

		debugfs_remove(phba->debug_readApp); /* readApp */
		phba->debug_readApp = NULL;

		debugfs_remove(phba->debug_readRef); /* readRef */
		phba->debug_readRef = NULL;

		kfree(phba->slow_ring_trc);
		phba->slow_ring_trc = NULL;

		/* slow_ring_trace */
		debugfs_remove(phba->debug_slow_ring_trc);
		phba->debug_slow_ring_trc = NULL;

		debugfs_remove(phba->debug_nvmeio_trc);
		phba->debug_nvmeio_trc = NULL;

		kfree(phba->nvmeio_trc);
		phba->nvmeio_trc = NULL;

		/*
		 * iDiag release
		 */
		if (phba->sli_rev == LPFC_SLI_REV4) {
			/* iDiag extAcc */
			debugfs_remove(phba->idiag_ext_acc);
			phba->idiag_ext_acc = NULL;

			/* iDiag mbxAcc */
			debugfs_remove(phba->idiag_mbx_acc);
			phba->idiag_mbx_acc = NULL;

			/* iDiag ctlAcc */
			debugfs_remove(phba->idiag_ctl_acc);
			phba->idiag_ctl_acc = NULL;

			/* iDiag drbAcc */
			debugfs_remove(phba->idiag_drb_acc);
			phba->idiag_drb_acc = NULL;

			/* iDiag queAcc */
			debugfs_remove(phba->idiag_que_acc);
			phba->idiag_que_acc = NULL;

			/* iDiag queInfo */
			debugfs_remove(phba->idiag_que_info);
			phba->idiag_que_info = NULL;

			/* iDiag barAcc */
			debugfs_remove(phba->idiag_bar_acc);
			phba->idiag_bar_acc = NULL;

			/* iDiag pciCfg */
			debugfs_remove(phba->idiag_pci_cfg);
			phba->idiag_pci_cfg = NULL;

			/* Finally remove the iDiag debugfs root */
			debugfs_remove(phba->idiag_root);
			phba->idiag_root = NULL;
		}

		if (phba->hba_debugfs_root) {
			debugfs_remove(phba->hba_debugfs_root); /* fnX */
			phba->hba_debugfs_root = NULL;
			atomic_dec(&lpfc_debugfs_hba_count);
		}

		if (atomic_read(&lpfc_debugfs_hba_count) == 0) {
			debugfs_remove(lpfc_debugfs_root); /* lpfc */
			lpfc_debugfs_root = NULL;
		}
	}
#endif
	return;
}

/*
 * Driver debug utility routines outside of debugfs. The debug utility
 * routines implemented here is intended to be used in the instrumented
 * debug driver for debugging host or port issues.
 */

/**
 * lpfc_debug_dump_all_queues - dump all the queues with a hba
 * @phba: Pointer to HBA context object.
 *
 * This function dumps entries of all the queues asociated with the @phba.
 **/
void
lpfc_debug_dump_all_queues(struct lpfc_hba *phba)
{
	int idx;

	/*
	 * Dump Work Queues (WQs)
	 */
	lpfc_debug_dump_wq(phba, DUMP_MBX, 0);
	lpfc_debug_dump_wq(phba, DUMP_ELS, 0);
	lpfc_debug_dump_wq(phba, DUMP_NVMELS, 0);

	for (idx = 0; idx < phba->cfg_hdw_queue; idx++)
		lpfc_debug_dump_wq(phba, DUMP_FCP, idx);

	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
		for (idx = 0; idx < phba->cfg_hdw_queue; idx++)
			lpfc_debug_dump_wq(phba, DUMP_NVME, idx);
	}

	lpfc_debug_dump_hdr_rq(phba);
	lpfc_debug_dump_dat_rq(phba);
	/*
	 * Dump Complete Queues (CQs)
	 */
	lpfc_debug_dump_cq(phba, DUMP_MBX, 0);
	lpfc_debug_dump_cq(phba, DUMP_ELS, 0);
	lpfc_debug_dump_cq(phba, DUMP_NVMELS, 0);

	for (idx = 0; idx < phba->cfg_hdw_queue; idx++)
		lpfc_debug_dump_cq(phba, DUMP_FCP, idx);

	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME) {
		for (idx = 0; idx < phba->cfg_hdw_queue; idx++)
			lpfc_debug_dump_cq(phba, DUMP_NVME, idx);
	}

	/*
	 * Dump Event Queues (EQs)
	 */
	for (idx = 0; idx < phba->cfg_hdw_queue; idx++)
		lpfc_debug_dump_hba_eq(phba, idx);
}
