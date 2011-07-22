/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2007-2011 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
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

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"
#include "lpfc_version.h"
#include "lpfc_compat.h"
#include "lpfc_debugfs.h"

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
	char buffer[LPFC_DEBUG_TRC_ENTRY_SIZE];

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
		len +=  snprintf(buf+len, size-len, buffer,
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
		len +=  snprintf(buf+len, size-len, buffer,
			dtp->data1, dtp->data2, dtp->data3);
	}

	lpfc_debugfs_enable = enable;
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
	char buffer[LPFC_DEBUG_TRC_ENTRY_SIZE];


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
		len +=  snprintf(buf+len, size-len, buffer,
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
		len +=  snprintf(buf+len, size-len, buffer,
			dtp->data1, dtp->data2, dtp->data3);
	}

	lpfc_debugfs_enable = enable;
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
	int cnt, i, j, found, posted, low;
	uint32_t phys, raw_index, getidx;
	struct lpfc_hbq_init *hip;
	struct hbq_s *hbqs;
	struct lpfc_hbq_entry *hbqe;
	struct lpfc_dmabuf *d_buf;
	struct hbq_dmabuf *hbq_buf;

	if (phba->sli_rev != 3)
		return 0;
	cnt = LPFC_HBQINFO_SIZE;
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

	len +=  snprintf(buf+len, size-len, "HBQ %d Info\n", i);

	hbqs =  &phba->hbqs[i];
	posted = 0;
	list_for_each_entry(d_buf, &hbqs->hbq_buffer_list, list)
		posted++;

	hip =  lpfc_hbq_defs[i];
	len +=  snprintf(buf+len, size-len,
		"idx:%d prof:%d rn:%d bufcnt:%d icnt:%d acnt:%d posted %d\n",
		hip->hbq_index, hip->profile, hip->rn,
		hip->buffer_count, hip->init_count, hip->add_count, posted);

	raw_index = phba->hbq_get[i];
	getidx = le32_to_cpu(raw_index);
	len +=  snprintf(buf+len, size-len,
		"entrys:%d bufcnt:%d Put:%d nPut:%d localGet:%d hbaGet:%d\n",
		hbqs->entry_count, hbqs->buffer_count, hbqs->hbqPutIdx,
		hbqs->next_hbqPutIdx, hbqs->local_hbqGetIdx, getidx);

	hbqe = (struct lpfc_hbq_entry *) phba->hbqs[i].hbq_virt;
	for (j=0; j<hbqs->entry_count; j++) {
		len +=  snprintf(buf+len, size-len,
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
				len +=  snprintf(buf+len, size-len, "Unused\n");
				goto skipit;
			}
		}
		else {
			if ((j >= hbqs->hbqPutIdx) &&
				(j < (hbqs->entry_count+low))) {
				len +=  snprintf(buf+len, size-len, "Unused\n");
				goto skipit;
			}
		}

		/* Get the Buffer info for the posted buffer */
		list_for_each_entry(d_buf, &hbqs->hbq_buffer_list, list) {
			hbq_buf = container_of(d_buf, struct hbq_dmabuf, dbuf);
			phys = ((uint64_t)hbq_buf->dbuf.phys & 0xffffffff);
			if (phys == le32_to_cpu(hbqe->bde.addrLow)) {
				len +=  snprintf(buf+len, size-len,
					"Buf%d: %p %06x\n", i,
					hbq_buf->dbuf.virt, hbq_buf->tag);
				found = 1;
				break;
			}
			i++;
		}
		if (!found) {
			len +=  snprintf(buf+len, size-len, "No DMAinfo?\n");
		}
skipit:
		hbqe++;
		if (len > LPFC_HBQINFO_SIZE - 54)
			break;
	}
	spin_unlock_irq(&phba->hbalock);
	return len;
}

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
	char buffer[1024];

	off = 0;
	spin_lock_irq(&phba->hbalock);

	len +=  snprintf(buf+len, size-len, "HBA SLIM\n");
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
		len +=  snprintf(buf+len, size-len,
		"%08x: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		off, *ptr, *(ptr+1), *(ptr+2), *(ptr+3), *(ptr+4),
		*(ptr+5), *(ptr+6), *(ptr+7));
		ptr += 8;
		i -= (8 * sizeof(uint32_t));
		off += (8 * sizeof(uint32_t));
	}

	spin_unlock_irq(&phba->hbalock);
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

	len +=  snprintf(buf+len, size-len, "SLIM Mailbox\n");
	ptr = (uint32_t *)phba->slim2p.virt;
	i = sizeof(MAILBOX_t);
	while (i > 0) {
		len +=  snprintf(buf+len, size-len,
		"%08x: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		off, *ptr, *(ptr+1), *(ptr+2), *(ptr+3), *(ptr+4),
		*(ptr+5), *(ptr+6), *(ptr+7));
		ptr += 8;
		i -= (8 * sizeof(uint32_t));
		off += (8 * sizeof(uint32_t));
	}

	len +=  snprintf(buf+len, size-len, "SLIM PCB\n");
	ptr = (uint32_t *)phba->pcb;
	i = sizeof(PCB_t);
	while (i > 0) {
		len +=  snprintf(buf+len, size-len,
		"%08x: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		off, *ptr, *(ptr+1), *(ptr+2), *(ptr+3), *(ptr+4),
		*(ptr+5), *(ptr+6), *(ptr+7));
		ptr += 8;
		i -= (8 * sizeof(uint32_t));
		off += (8 * sizeof(uint32_t));
	}

	for (i = 0; i < 4; i++) {
		pgpp = &phba->port_gp[i];
		pring = &psli->ring[i];
		len +=  snprintf(buf+len, size-len,
				 "Ring %d: CMD GetInx:%d (Max:%d Next:%d "
				 "Local:%d flg:x%x)  RSP PutInx:%d Max:%d\n",
				 i, pgpp->cmdGetInx, pring->numCiocb,
				 pring->next_cmdidx, pring->local_getidx,
				 pring->flag, pgpp->rspPutInx, pring->numRiocb);
	}

	if (phba->sli_rev <= LPFC_SLI_REV3) {
		word0 = readl(phba->HAregaddr);
		word1 = readl(phba->CAregaddr);
		word2 = readl(phba->HSregaddr);
		word3 = readl(phba->HCregaddr);
		len +=  snprintf(buf+len, size-len, "HA:%08x CA:%08x HS:%08x "
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
	int cnt;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp;
	unsigned char *statep, *name;

	cnt = (LPFC_NODELIST_SIZE / LPFC_NODELIST_ENTRY_SIZE);

	spin_lock_irq(shost->host_lock);
	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		if (!cnt) {
			len +=  snprintf(buf+len, size-len,
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
		case NLP_STE_UNMAPPED_NODE:
			statep = "UNMAP ";
			break;
		case NLP_STE_MAPPED_NODE:
			statep = "MAPPED";
			break;
		case NLP_STE_NPR_NODE:
			statep = "NPR   ";
			break;
		default:
			statep = "UNKNOWN";
		}
		len +=  snprintf(buf+len, size-len, "%s DID:x%06x ",
			statep, ndlp->nlp_DID);
		name = (unsigned char *)&ndlp->nlp_portname;
		len +=  snprintf(buf+len, size-len,
			"WWPN %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x ",
			*name, *(name+1), *(name+2), *(name+3),
			*(name+4), *(name+5), *(name+6), *(name+7));
		name = (unsigned char *)&ndlp->nlp_nodename;
		len +=  snprintf(buf+len, size-len,
			"WWNN %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x ",
			*name, *(name+1), *(name+2), *(name+3),
			*(name+4), *(name+5), *(name+6), *(name+7));
		len +=  snprintf(buf+len, size-len, "RPI:%03d flag:x%08x ",
			ndlp->nlp_rpi, ndlp->nlp_flag);
		if (!ndlp->nlp_type)
			len +=  snprintf(buf+len, size-len, "UNKNOWN_TYPE ");
		if (ndlp->nlp_type & NLP_FC_NODE)
			len +=  snprintf(buf+len, size-len, "FC_NODE ");
		if (ndlp->nlp_type & NLP_FABRIC)
			len +=  snprintf(buf+len, size-len, "FABRIC ");
		if (ndlp->nlp_type & NLP_FCP_TARGET)
			len +=  snprintf(buf+len, size-len, "FCP_TGT sid:%d ",
				ndlp->nlp_sid);
		if (ndlp->nlp_type & NLP_FCP_INITIATOR)
			len +=  snprintf(buf+len, size-len, "FCP_INITIATOR ");
		len += snprintf(buf+len, size-len, "usgmap:%x ",
			ndlp->nlp_usg_map);
		len += snprintf(buf+len, size-len, "refcnt:%x",
			atomic_read(&ndlp->kref.refcount));
		len +=  snprintf(buf+len, size-len, "\n");
	}
	spin_unlock_irq(shost->host_lock);
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
 * This function returns zero if successful. On error it will return an negative
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
 * This function returns zero if successful. On error it will return an negative
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
 * This function returns zero if successful. On error it will return an negative
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
 * This function returns zero if successful. On error it will return an negative
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
 * This function returns zero if successful. On error it will return an negative
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
	printk(KERN_ERR "9059 BLKGRD:  %s: _dump_buf_data=0x%p\n",
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
	printk(KERN_ERR	"9060 BLKGRD: %s: _dump_buf_dif=0x%p file=%s\n",
		__func__, _dump_buf_dif, file->f_dentry->d_name.name);
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
 * This function returns zero if successful. On error it will return an negative
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
	struct lpfc_debug *debug;
	loff_t pos = -1;

	debug = file->private_data;

	switch (whence) {
	case 0:
		pos = off;
		break;
	case 1:
		pos = file->f_pos + off;
		break;
	case 2:
		pos = debug->len - off;
	}
	return (pos < 0 || pos > debug->len) ? -EINVAL : (file->f_pos = pos);
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
	int bsize, i;

	/* Protect copy from user */
	if (!access_ok(VERIFY_READ, buf, nbytes))
		return -EFAULT;

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
		where = idiag.cmd.data[0];
		count = idiag.cmd.data[1];
	} else
		return 0;

	/* Read single PCI config space register */
	switch (count) {
	case SIZE_U8: /* byte (8 bits) */
		pci_read_config_byte(pdev, where, &u8val);
		len += snprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
				"%03x: %02x\n", where, u8val);
		break;
	case SIZE_U16: /* word (16 bits) */
		pci_read_config_word(pdev, where, &u16val);
		len += snprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
				"%03x: %04x\n", where, u16val);
		break;
	case SIZE_U32: /* double word (32 bits) */
		pci_read_config_dword(pdev, where, &u32val);
		len += snprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
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
	len += snprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
			"%03x: ", offset_label);
	while (index > 0) {
		pci_read_config_dword(pdev, offset, &u32val);
		len += snprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
				"%08x ", u32val);
		offset += sizeof(uint32_t);
		index -= sizeof(uint32_t);
		if (!index)
			len += snprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
					"\n");
		else if (!(index % (8 * sizeof(uint32_t)))) {
			offset_label += (8 * sizeof(uint32_t));
			len += snprintf(pbuffer+len, LPFC_PCI_CFG_SIZE-len,
					"\n%03x: ", offset_label);
		}
	}

	/* Set up the offset for next portion of pci cfg read */
	idiag.offset.last_rd += LPFC_PCI_CFG_RD_SIZE;
	if (idiag.offset.last_rd >= LPFC_PCI_CFG_SIZE)
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
		where = idiag.cmd.data[0];
		count = idiag.cmd.data[1];
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
		where = idiag.cmd.data[0];
		count = idiag.cmd.data[1];
		value = idiag.cmd.data[2];
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
 * lpfc_idiag_queinfo_read - idiag debugfs read queue information
 * @file: The file pointer to read from.
 * @buf: The buffer to copy the data to.
 * @nbytes: The number of bytes to read.
 * @ppos: The position in the file to start reading from.
 *
 * Description:
 * This routine reads data from the @phba SLI4 PCI function queue information,
 * and copies to user @buf.
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
	int len = 0, fcp_qidx;
	char *pbuffer;

	if (!debug->buffer)
		debug->buffer = kmalloc(LPFC_QUE_INFO_GET_BUF_SIZE, GFP_KERNEL);
	if (!debug->buffer)
		return 0;
	pbuffer = debug->buffer;

	if (*ppos)
		return 0;

	/* Get slow-path event queue information */
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Slow-path EQ information:\n");
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"\tEQID[%02d], "
			"QE-COUNT[%04d], QE-SIZE[%04d], "
			"HOST-INDEX[%04d], PORT-INDEX[%04d]\n\n",
			phba->sli4_hba.sp_eq->queue_id,
			phba->sli4_hba.sp_eq->entry_count,
			phba->sli4_hba.sp_eq->entry_size,
			phba->sli4_hba.sp_eq->host_index,
			phba->sli4_hba.sp_eq->hba_index);

	/* Get fast-path event queue information */
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Fast-path EQ information:\n");
	for (fcp_qidx = 0; fcp_qidx < phba->cfg_fcp_eq_count; fcp_qidx++) {
		len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
				"\tEQID[%02d], "
				"QE-COUNT[%04d], QE-SIZE[%04d], "
				"HOST-INDEX[%04d], PORT-INDEX[%04d]\n",
				phba->sli4_hba.fp_eq[fcp_qidx]->queue_id,
				phba->sli4_hba.fp_eq[fcp_qidx]->entry_count,
				phba->sli4_hba.fp_eq[fcp_qidx]->entry_size,
				phba->sli4_hba.fp_eq[fcp_qidx]->host_index,
				phba->sli4_hba.fp_eq[fcp_qidx]->hba_index);
	}
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len, "\n");

	/* Get mailbox complete queue information */
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Slow-path MBX CQ information:\n");
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Associated EQID[%02d]:\n",
			phba->sli4_hba.mbx_cq->assoc_qid);
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"\tCQID[%02d], "
			"QE-COUNT[%04d], QE-SIZE[%04d], "
			"HOST-INDEX[%04d], PORT-INDEX[%04d]\n\n",
			phba->sli4_hba.mbx_cq->queue_id,
			phba->sli4_hba.mbx_cq->entry_count,
			phba->sli4_hba.mbx_cq->entry_size,
			phba->sli4_hba.mbx_cq->host_index,
			phba->sli4_hba.mbx_cq->hba_index);

	/* Get slow-path complete queue information */
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Slow-path ELS CQ information:\n");
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Associated EQID[%02d]:\n",
			phba->sli4_hba.els_cq->assoc_qid);
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"\tCQID [%02d], "
			"QE-COUNT[%04d], QE-SIZE[%04d], "
			"HOST-INDEX[%04d], PORT-INDEX[%04d]\n\n",
			phba->sli4_hba.els_cq->queue_id,
			phba->sli4_hba.els_cq->entry_count,
			phba->sli4_hba.els_cq->entry_size,
			phba->sli4_hba.els_cq->host_index,
			phba->sli4_hba.els_cq->hba_index);

	/* Get fast-path complete queue information */
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Fast-path FCP CQ information:\n");
	fcp_qidx = 0;
	do {
		len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
				"Associated EQID[%02d]:\n",
				phba->sli4_hba.fcp_cq[fcp_qidx]->assoc_qid);
		len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
				"\tCQID[%02d], "
				"QE-COUNT[%04d], QE-SIZE[%04d], "
				"HOST-INDEX[%04d], PORT-INDEX[%04d]\n",
				phba->sli4_hba.fcp_cq[fcp_qidx]->queue_id,
				phba->sli4_hba.fcp_cq[fcp_qidx]->entry_count,
				phba->sli4_hba.fcp_cq[fcp_qidx]->entry_size,
				phba->sli4_hba.fcp_cq[fcp_qidx]->host_index,
				phba->sli4_hba.fcp_cq[fcp_qidx]->hba_index);
	} while (++fcp_qidx < phba->cfg_fcp_eq_count);
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len, "\n");

	/* Get mailbox queue information */
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Slow-path MBX MQ information:\n");
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Associated CQID[%02d]:\n",
			phba->sli4_hba.mbx_wq->assoc_qid);
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"\tWQID[%02d], "
			"QE-COUNT[%04d], QE-SIZE[%04d], "
			"HOST-INDEX[%04d], PORT-INDEX[%04d]\n\n",
			phba->sli4_hba.mbx_wq->queue_id,
			phba->sli4_hba.mbx_wq->entry_count,
			phba->sli4_hba.mbx_wq->entry_size,
			phba->sli4_hba.mbx_wq->host_index,
			phba->sli4_hba.mbx_wq->hba_index);

	/* Get slow-path work queue information */
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Slow-path ELS WQ information:\n");
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Associated CQID[%02d]:\n",
			phba->sli4_hba.els_wq->assoc_qid);
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"\tWQID[%02d], "
			"QE-COUNT[%04d], QE-SIZE[%04d], "
			"HOST-INDEX[%04d], PORT-INDEX[%04d]\n\n",
			phba->sli4_hba.els_wq->queue_id,
			phba->sli4_hba.els_wq->entry_count,
			phba->sli4_hba.els_wq->entry_size,
			phba->sli4_hba.els_wq->host_index,
			phba->sli4_hba.els_wq->hba_index);

	/* Get fast-path work queue information */
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Fast-path FCP WQ information:\n");
	for (fcp_qidx = 0; fcp_qidx < phba->cfg_fcp_wq_count; fcp_qidx++) {
		len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
				"Associated CQID[%02d]:\n",
				phba->sli4_hba.fcp_wq[fcp_qidx]->assoc_qid);
		len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
				"\tWQID[%02d], "
				"QE-COUNT[%04d], WQE-SIZE[%04d], "
				"HOST-INDEX[%04d], PORT-INDEX[%04d]\n",
				phba->sli4_hba.fcp_wq[fcp_qidx]->queue_id,
				phba->sli4_hba.fcp_wq[fcp_qidx]->entry_count,
				phba->sli4_hba.fcp_wq[fcp_qidx]->entry_size,
				phba->sli4_hba.fcp_wq[fcp_qidx]->host_index,
				phba->sli4_hba.fcp_wq[fcp_qidx]->hba_index);
	}
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len, "\n");

	/* Get receive queue information */
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Slow-path RQ information:\n");
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"Associated CQID[%02d]:\n",
			phba->sli4_hba.hdr_rq->assoc_qid);
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"\tHQID[%02d], "
			"QE-COUNT[%04d], QE-SIZE[%04d], "
			"HOST-INDEX[%04d], PORT-INDEX[%04d]\n",
			phba->sli4_hba.hdr_rq->queue_id,
			phba->sli4_hba.hdr_rq->entry_count,
			phba->sli4_hba.hdr_rq->entry_size,
			phba->sli4_hba.hdr_rq->host_index,
			phba->sli4_hba.hdr_rq->hba_index);
	len += snprintf(pbuffer+len, LPFC_QUE_INFO_GET_BUF_SIZE-len,
			"\tDQID[%02d], "
			"QE-COUNT[%04d], QE-SIZE[%04d], "
			"HOST-INDEX[%04d], PORT-INDEX[%04d]\n",
			phba->sli4_hba.dat_rq->queue_id,
			phba->sli4_hba.dat_rq->entry_count,
			phba->sli4_hba.dat_rq->entry_size,
			phba->sli4_hba.dat_rq->host_index,
			phba->sli4_hba.dat_rq->hba_index);

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
	len += snprintf(pbuffer+len, LPFC_QUE_ACC_BUF_SIZE-len,
			"QE-INDEX[%04d]:\n", index);

	offset = 0;
	pentry = pque->qe[index].address;
	while (esize > 0) {
		len += snprintf(pbuffer+len, LPFC_QUE_ACC_BUF_SIZE-len,
				"%08x ", *pentry);
		pentry++;
		offset += sizeof(uint32_t);
		esize -= sizeof(uint32_t);
		if (esize > 0 && !(offset % (4 * sizeof(uint32_t))))
			len += snprintf(pbuffer+len,
					LPFC_QUE_ACC_BUF_SIZE-len, "\n");
	}
	len += snprintf(pbuffer+len, LPFC_QUE_ACC_BUF_SIZE-len, "\n");

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
		index = idiag.cmd.data[2];
		count = idiag.cmd.data[3];
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
	struct lpfc_queue *pque;
	int rc;

	/* This is a user write operation */
	debug->op = LPFC_IDIAG_OP_WR;

	rc = lpfc_idiag_cmd_get(buf, nbytes, &idiag.cmd);
	if (rc < 0)
		return rc;

	/* Get and sanity check on command feilds */
	quetp  = idiag.cmd.data[0];
	queid  = idiag.cmd.data[1];
	index  = idiag.cmd.data[2];
	count  = idiag.cmd.data[3];
	offset = idiag.cmd.data[4];
	value  = idiag.cmd.data[5];

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
		/* Slow-path event queue */
		if (phba->sli4_hba.sp_eq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.sp_eq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.sp_eq;
			goto pass_check;
		}
		/* Fast-path event queue */
		for (qidx = 0; qidx < phba->cfg_fcp_eq_count; qidx++) {
			if (phba->sli4_hba.fp_eq[qidx]->queue_id == queid) {
				/* Sanity check */
				rc = lpfc_idiag_que_param_check(
						phba->sli4_hba.fp_eq[qidx],
						index, count);
				if (rc)
					goto error_out;
				idiag.ptr_private = phba->sli4_hba.fp_eq[qidx];
				goto pass_check;
			}
		}
		goto error_out;
		break;
	case LPFC_IDIAG_CQ:
		/* MBX complete queue */
		if (phba->sli4_hba.mbx_cq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.mbx_cq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.mbx_cq;
			goto pass_check;
		}
		/* ELS complete queue */
		if (phba->sli4_hba.els_cq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.els_cq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.els_cq;
			goto pass_check;
		}
		/* FCP complete queue */
		qidx = 0;
		do {
			if (phba->sli4_hba.fcp_cq[qidx]->queue_id == queid) {
				/* Sanity check */
				rc = lpfc_idiag_que_param_check(
						phba->sli4_hba.fcp_cq[qidx],
						index, count);
				if (rc)
					goto error_out;
				idiag.ptr_private =
						phba->sli4_hba.fcp_cq[qidx];
				goto pass_check;
			}
		} while (++qidx < phba->cfg_fcp_eq_count);
		goto error_out;
		break;
	case LPFC_IDIAG_MQ:
		/* MBX work queue */
		if (phba->sli4_hba.mbx_wq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.mbx_wq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.mbx_wq;
			goto pass_check;
		}
		break;
	case LPFC_IDIAG_WQ:
		/* ELS work queue */
		if (phba->sli4_hba.els_wq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.els_wq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.els_wq;
			goto pass_check;
		}
		/* FCP work queue */
		for (qidx = 0; qidx < phba->cfg_fcp_wq_count; qidx++) {
			if (phba->sli4_hba.fcp_wq[qidx]->queue_id == queid) {
				/* Sanity check */
				rc = lpfc_idiag_que_param_check(
						phba->sli4_hba.fcp_wq[qidx],
						index, count);
				if (rc)
					goto error_out;
				idiag.ptr_private =
					phba->sli4_hba.fcp_wq[qidx];
				goto pass_check;
			}
		}
		goto error_out;
		break;
	case LPFC_IDIAG_RQ:
		/* HDR queue */
		if (phba->sli4_hba.hdr_rq->queue_id == queid) {
			/* Sanity check */
			rc = lpfc_idiag_que_param_check(
					phba->sli4_hba.hdr_rq, index, count);
			if (rc)
				goto error_out;
			idiag.ptr_private = phba->sli4_hba.hdr_rq;
			goto pass_check;
		}
		/* DAT queue */
		if (phba->sli4_hba.dat_rq->queue_id == queid) {
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
		pentry = pque->qe[index].address;
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
 * @len: The lenght of bytes to copied.
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
	case LPFC_DRB_EQCQ:
		len += snprintf(pbuffer+len, LPFC_DRB_ACC_BUF_SIZE-len,
				"EQCQ-DRB-REG: 0x%08x\n",
				readl(phba->sli4_hba.EQCQDBregaddr));
		break;
	case LPFC_DRB_MQ:
		len += snprintf(pbuffer+len, LPFC_DRB_ACC_BUF_SIZE-len,
				"MQ-DRB-REG:   0x%08x\n",
				readl(phba->sli4_hba.MQDBregaddr));
		break;
	case LPFC_DRB_WQ:
		len += snprintf(pbuffer+len, LPFC_DRB_ACC_BUF_SIZE-len,
				"WQ-DRB-REG:   0x%08x\n",
				readl(phba->sli4_hba.WQDBregaddr));
		break;
	case LPFC_DRB_RQ:
		len += snprintf(pbuffer+len, LPFC_DRB_ACC_BUF_SIZE-len,
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
		drb_reg_id = idiag.cmd.data[0];
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
	uint32_t drb_reg_id, value, reg_val;
	void __iomem *drb_reg;
	int rc;

	/* This is a user write operation */
	debug->op = LPFC_IDIAG_OP_WR;

	rc = lpfc_idiag_cmd_get(buf, nbytes, &idiag.cmd);
	if (rc < 0)
		return rc;

	/* Sanity check on command line arguments */
	drb_reg_id = idiag.cmd.data[0];
	value = idiag.cmd.data[1];

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
		case LPFC_DRB_EQCQ:
			drb_reg = phba->sli4_hba.EQCQDBregaddr;
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

#undef lpfc_debugfs_op_hbqinfo
static const struct file_operations lpfc_debugfs_op_hbqinfo = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_hbqinfo_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.release =      lpfc_debugfs_release,
};

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

#undef lpfc_idiag_op_queInfo
static const struct file_operations lpfc_idiag_op_queInfo = {
	.owner =        THIS_MODULE,
	.open =         lpfc_idiag_open,
	.read =         lpfc_idiag_queinfo_read,
	.release =      lpfc_idiag_release,
};

#undef lpfc_idiag_op_queacc
static const struct file_operations lpfc_idiag_op_queAcc = {
	.owner =        THIS_MODULE,
	.open =         lpfc_idiag_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_idiag_queacc_read,
	.write =        lpfc_idiag_queacc_write,
	.release =      lpfc_idiag_cmd_release,
};

#undef lpfc_idiag_op_drbacc
static const struct file_operations lpfc_idiag_op_drbAcc = {
	.owner =        THIS_MODULE,
	.open =         lpfc_idiag_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_idiag_drbacc_read,
	.write =        lpfc_idiag_drbacc_write,
	.release =      lpfc_idiag_cmd_release,
};

#endif

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

	if (!lpfc_debugfs_enable)
		return;

	/* Setup lpfc root directory */
	if (!lpfc_debugfs_root) {
		lpfc_debugfs_root = debugfs_create_dir("lpfc", NULL);
		atomic_set(&lpfc_debugfs_hba_count, 0);
		if (!lpfc_debugfs_root) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "0408 Cannot create debugfs root\n");
			goto debug_failed;
		}
	}
	if (!lpfc_debugfs_start_time)
		lpfc_debugfs_start_time = jiffies;

	/* Setup funcX directory for specific HBA PCI function */
	snprintf(name, sizeof(name), "fn%d", phba->brd_no);
	if (!phba->hba_debugfs_root) {
		phba->hba_debugfs_root =
			debugfs_create_dir(name, lpfc_debugfs_root);
		if (!phba->hba_debugfs_root) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "0412 Cannot create debugfs hba\n");
			goto debug_failed;
		}
		atomic_inc(&lpfc_debugfs_hba_count);
		atomic_set(&phba->debugfs_vport_count, 0);

		/* Setup hbqinfo */
		snprintf(name, sizeof(name), "hbqinfo");
		phba->debug_hbqinfo =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				 phba->hba_debugfs_root,
				 phba, &lpfc_debugfs_op_hbqinfo);
		if (!phba->debug_hbqinfo) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				"0411 Cannot create debugfs hbqinfo\n");
			goto debug_failed;
		}

		/* Setup dumpHBASlim */
		if (phba->sli_rev < LPFC_SLI_REV4) {
			snprintf(name, sizeof(name), "dumpHBASlim");
			phba->debug_dumpHBASlim =
				debugfs_create_file(name,
					S_IFREG|S_IRUGO|S_IWUSR,
					phba->hba_debugfs_root,
					phba, &lpfc_debugfs_op_dumpHBASlim);
			if (!phba->debug_dumpHBASlim) {
				lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
						 "0413 Cannot create debugfs "
						"dumpHBASlim\n");
				goto debug_failed;
			}
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
			if (!phba->debug_dumpHostSlim) {
				lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
						 "0414 Cannot create debugfs "
						 "dumpHostSlim\n");
				goto debug_failed;
			}
		} else
			phba->debug_dumpHBASlim = NULL;

		/* Setup dumpData */
		snprintf(name, sizeof(name), "dumpData");
		phba->debug_dumpData =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				 phba->hba_debugfs_root,
				 phba, &lpfc_debugfs_op_dumpData);
		if (!phba->debug_dumpData) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				"0800 Cannot create debugfs dumpData\n");
			goto debug_failed;
		}

		/* Setup dumpDif */
		snprintf(name, sizeof(name), "dumpDif");
		phba->debug_dumpDif =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				 phba->hba_debugfs_root,
				 phba, &lpfc_debugfs_op_dumpDif);
		if (!phba->debug_dumpDif) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				"0801 Cannot create debugfs dumpDif\n");
			goto debug_failed;
		}

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
				printk(KERN_ERR
				       "lpfc_debugfs_max_disc_trc changed to "
				       "%d\n", lpfc_debugfs_max_disc_trc);
			}
		}

		snprintf(name, sizeof(name), "slow_ring_trace");
		phba->debug_slow_ring_trc =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				 phba->hba_debugfs_root,
				 phba, &lpfc_debugfs_op_slow_ring_trc);
		if (!phba->debug_slow_ring_trc) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "0415 Cannot create debugfs "
					 "slow_ring_trace\n");
			goto debug_failed;
		}
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
	}

	snprintf(name, sizeof(name), "vport%d", vport->vpi);
	if (!vport->vport_debugfs_root) {
		vport->vport_debugfs_root =
			debugfs_create_dir(name, phba->hba_debugfs_root);
		if (!vport->vport_debugfs_root) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "0417 Can't create debugfs\n");
			goto debug_failed;
		}
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
			printk(KERN_ERR
			       "lpfc_debugfs_max_disc_trc changed to %d\n",
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
	if (!vport->debug_disc_trc) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "0419 Cannot create debugfs "
				 "discovery_trace\n");
		goto debug_failed;
	}
	snprintf(name, sizeof(name), "nodelist");
	vport->debug_nodelist =
		debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				 vport->vport_debugfs_root,
				 vport, &lpfc_debugfs_op_nodelist);
	if (!vport->debug_nodelist) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "0409 Can't create debugfs nodelist\n");
		goto debug_failed;
	}

	/*
	 * iDiag debugfs root entry points for SLI4 device only
	 */
	if (phba->sli_rev < LPFC_SLI_REV4)
		goto debug_failed;

	snprintf(name, sizeof(name), "iDiag");
	if (!phba->idiag_root) {
		phba->idiag_root =
			debugfs_create_dir(name, phba->hba_debugfs_root);
		if (!phba->idiag_root) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "2922 Can't create idiag debugfs\n");
			goto debug_failed;
		}
		/* Initialize iDiag data structure */
		memset(&idiag, 0, sizeof(idiag));
	}

	/* iDiag read PCI config space */
	snprintf(name, sizeof(name), "pciCfg");
	if (!phba->idiag_pci_cfg) {
		phba->idiag_pci_cfg =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				phba->idiag_root, phba, &lpfc_idiag_op_pciCfg);
		if (!phba->idiag_pci_cfg) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "2923 Can't create idiag debugfs\n");
			goto debug_failed;
		}
		idiag.offset.last_rd = 0;
	}

	/* iDiag get PCI function queue information */
	snprintf(name, sizeof(name), "queInfo");
	if (!phba->idiag_que_info) {
		phba->idiag_que_info =
			debugfs_create_file(name, S_IFREG|S_IRUGO,
			phba->idiag_root, phba, &lpfc_idiag_op_queInfo);
		if (!phba->idiag_que_info) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "2924 Can't create idiag debugfs\n");
			goto debug_failed;
		}
	}

	/* iDiag access PCI function queue */
	snprintf(name, sizeof(name), "queAcc");
	if (!phba->idiag_que_acc) {
		phba->idiag_que_acc =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				phba->idiag_root, phba, &lpfc_idiag_op_queAcc);
		if (!phba->idiag_que_acc) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "2926 Can't create idiag debugfs\n");
			goto debug_failed;
		}
	}

	/* iDiag access PCI function doorbell registers */
	snprintf(name, sizeof(name), "drbAcc");
	if (!phba->idiag_drb_acc) {
		phba->idiag_drb_acc =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				phba->idiag_root, phba, &lpfc_idiag_op_drbAcc);
		if (!phba->idiag_drb_acc) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "2927 Can't create idiag debugfs\n");
			goto debug_failed;
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

	if (vport->disc_trc) {
		kfree(vport->disc_trc);
		vport->disc_trc = NULL;
	}
	if (vport->debug_disc_trc) {
		debugfs_remove(vport->debug_disc_trc); /* discovery_trace */
		vport->debug_disc_trc = NULL;
	}
	if (vport->debug_nodelist) {
		debugfs_remove(vport->debug_nodelist); /* nodelist */
		vport->debug_nodelist = NULL;
	}

	if (vport->vport_debugfs_root) {
		debugfs_remove(vport->vport_debugfs_root); /* vportX */
		vport->vport_debugfs_root = NULL;
		atomic_dec(&phba->debugfs_vport_count);
	}
	if (atomic_read(&phba->debugfs_vport_count) == 0) {

		if (phba->debug_hbqinfo) {
			debugfs_remove(phba->debug_hbqinfo); /* hbqinfo */
			phba->debug_hbqinfo = NULL;
		}
		if (phba->debug_dumpHBASlim) {
			debugfs_remove(phba->debug_dumpHBASlim); /* HBASlim */
			phba->debug_dumpHBASlim = NULL;
		}
		if (phba->debug_dumpHostSlim) {
			debugfs_remove(phba->debug_dumpHostSlim); /* HostSlim */
			phba->debug_dumpHostSlim = NULL;
		}
		if (phba->debug_dumpData) {
			debugfs_remove(phba->debug_dumpData); /* dumpData */
			phba->debug_dumpData = NULL;
		}

		if (phba->debug_dumpDif) {
			debugfs_remove(phba->debug_dumpDif); /* dumpDif */
			phba->debug_dumpDif = NULL;
		}

		if (phba->slow_ring_trc) {
			kfree(phba->slow_ring_trc);
			phba->slow_ring_trc = NULL;
		}
		if (phba->debug_slow_ring_trc) {
			/* slow_ring_trace */
			debugfs_remove(phba->debug_slow_ring_trc);
			phba->debug_slow_ring_trc = NULL;
		}

		/*
		 * iDiag release
		 */
		if (phba->sli_rev == LPFC_SLI_REV4) {
			if (phba->idiag_drb_acc) {
				/* iDiag drbAcc */
				debugfs_remove(phba->idiag_drb_acc);
				phba->idiag_drb_acc = NULL;
			}
			if (phba->idiag_que_acc) {
				/* iDiag queAcc */
				debugfs_remove(phba->idiag_que_acc);
				phba->idiag_que_acc = NULL;
			}
			if (phba->idiag_que_info) {
				/* iDiag queInfo */
				debugfs_remove(phba->idiag_que_info);
				phba->idiag_que_info = NULL;
			}
			if (phba->idiag_pci_cfg) {
				/* iDiag pciCfg */
				debugfs_remove(phba->idiag_pci_cfg);
				phba->idiag_pci_cfg = NULL;
			}

			/* Finally remove the iDiag debugfs root */
			if (phba->idiag_root) {
				/* iDiag root */
				debugfs_remove(phba->idiag_root);
				phba->idiag_root = NULL;
			}
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
