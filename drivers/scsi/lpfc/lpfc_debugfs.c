/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2007 Emulex.  All rights reserved.                *
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
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"
#include "lpfc_version.h"
#include "lpfc_compat.h"
#include "lpfc_debugfs.h"

#ifdef CONFIG_LPFC_DEBUG_FS
/* debugfs interface
 *
 * To access this interface the user should:
 * # mkdir /debug
 * # mount -t debugfs none /debug
 *
 * The lpfc debugfs directory hierachy is:
 * lpfc/lpfcX/vportY
 * where X is the lpfc hba unique_id
 * where Y is the vport VPI on that hba
 *
 * Debugging services available per vport:
 * discovery_trace
 * This is an ACSII readable file that contains a trace of the last
 * lpfc_debugfs_max_disc_trc events that happened on a specific vport.
 * See lpfc_debugfs.h for different categories of
 * discovery events. To enable the discovery trace, the following
 * module parameters must be set:
 * lpfc_debugfs_enable=1         Turns on lpfc debugfs filesystem support
 * lpfc_debugfs_max_disc_trc=X   Where X is the event trace depth for
 *                               EACH vport. X MUST also be a power of 2.
 * lpfc_debugfs_mask_disc_trc=Y  Where Y is an event mask as defined in
 *                               lpfc_debugfs.h .
 */
static int lpfc_debugfs_enable = 1;
module_param(lpfc_debugfs_enable, int, 0);
MODULE_PARM_DESC(lpfc_debugfs_enable, "Enable debugfs services");

/* This MUST be a power of 2 */
static int lpfc_debugfs_max_disc_trc;
module_param(lpfc_debugfs_max_disc_trc, int, 0);
MODULE_PARM_DESC(lpfc_debugfs_max_disc_trc,
	"Set debugfs discovery trace depth");

/* This MUST be a power of 2 */
static int lpfc_debugfs_max_slow_ring_trc;
module_param(lpfc_debugfs_max_slow_ring_trc, int, 0);
MODULE_PARM_DESC(lpfc_debugfs_max_slow_ring_trc,
	"Set debugfs slow ring trace depth");

int lpfc_debugfs_mask_disc_trc;
module_param(lpfc_debugfs_mask_disc_trc, int, 0);
MODULE_PARM_DESC(lpfc_debugfs_mask_disc_trc,
	"Set debugfs discovery trace mask");

#include <linux/debugfs.h>

/* size of output line, for discovery_trace and slow_ring_trace */
#define LPFC_DEBUG_TRC_ENTRY_SIZE 100

/* nodelist output buffer size */
#define LPFC_NODELIST_SIZE 8192
#define LPFC_NODELIST_ENTRY_SIZE 120

/* dumpHBASlim output buffer size */
#define LPFC_DUMPHBASLIM_SIZE 4096

/* dumpHostSlim output buffer size */
#define LPFC_DUMPHOSTSLIM_SIZE 4096

/* hbqinfo output buffer size */
#define LPFC_HBQINFO_SIZE 8192

struct lpfc_debug {
	char *buffer;
	int  len;
};

static atomic_t lpfc_debugfs_seq_trc_cnt = ATOMIC_INIT(0);
static unsigned long lpfc_debugfs_start_time = 0L;

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
		((uint8_t *)phba->MBslimaddr) + lpfc_debugfs_last_hba_slim_off,
		1024);

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
	ptr = (uint32_t *)phba->slim2p;
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
	ptr = (uint32_t *)&phba->slim2p->pcb;
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

	pgpp = (struct lpfc_pgp *)&phba->slim2p->mbx.us.s3_pgp.port;
	pring = &psli->ring[0];
	len +=  snprintf(buf+len, size-len,
		"Ring 0: CMD GetInx:%d (Max:%d Next:%d Local:%d flg:x%x)  "
		"RSP PutInx:%d Max:%d\n",
		pgpp->cmdGetInx, pring->numCiocb,
		pring->next_cmdidx, pring->local_getidx, pring->flag,
		pgpp->rspPutInx, pring->numRiocb);
	pgpp++;

	pring = &psli->ring[1];
	len +=  snprintf(buf+len, size-len,
		"Ring 1: CMD GetInx:%d (Max:%d Next:%d Local:%d flg:x%x)  "
		"RSP PutInx:%d Max:%d\n",
		pgpp->cmdGetInx, pring->numCiocb,
		pring->next_cmdidx, pring->local_getidx, pring->flag,
		pgpp->rspPutInx, pring->numRiocb);
	pgpp++;

	pring = &psli->ring[2];
	len +=  snprintf(buf+len, size-len,
		"Ring 2: CMD GetInx:%d (Max:%d Next:%d Local:%d flg:x%x)  "
		"RSP PutInx:%d Max:%d\n",
		pgpp->cmdGetInx, pring->numCiocb,
		pring->next_cmdidx, pring->local_getidx, pring->flag,
		pgpp->rspPutInx, pring->numRiocb);
	pgpp++;

	pring = &psli->ring[3];
	len +=  snprintf(buf+len, size-len,
		"Ring 3: CMD GetInx:%d (Max:%d Next:%d Local:%d flg:x%x)  "
		"RSP PutInx:%d Max:%d\n",
		pgpp->cmdGetInx, pring->numCiocb,
		pring->next_cmdidx, pring->local_getidx, pring->flag,
		pgpp->rspPutInx, pring->numRiocb);


	ptr = (uint32_t *)&phba->slim2p->mbx.us.s3_pgp.hbq_get;
	word0 = readl(phba->HAregaddr);
	word1 = readl(phba->CAregaddr);
	word2 = readl(phba->HSregaddr);
	word3 = readl(phba->HCregaddr);
	len +=  snprintf(buf+len, size-len, "HA:%08x CA:%08x HS:%08x HC:%08x\n",
	word0, word1, word2, word3);
	spin_unlock_irq(&phba->hbalock);
	return len;
}

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


inline void
lpfc_debugfs_disc_trc(struct lpfc_vport *vport, int mask, char *fmt,
	uint32_t data1, uint32_t data2, uint32_t data3)
{
#ifdef CONFIG_LPFC_DEBUG_FS
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

inline void
lpfc_debugfs_slow_ring_trc(struct lpfc_hba *phba, char *fmt,
	uint32_t data1, uint32_t data2, uint32_t data3)
{
#ifdef CONFIG_LPFC_DEBUG_FS
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

#ifdef CONFIG_LPFC_DEBUG_FS
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

	/* Round to page boundry */
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

	/* Round to page boundry */
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

static int
lpfc_debugfs_hbqinfo_open(struct inode *inode, struct file *file)
{
	struct lpfc_hba *phba = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundry */
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

static int
lpfc_debugfs_dumpHBASlim_open(struct inode *inode, struct file *file)
{
	struct lpfc_hba *phba = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundry */
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

static int
lpfc_debugfs_dumpHostSlim_open(struct inode *inode, struct file *file)
{
	struct lpfc_hba *phba = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundry */
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
lpfc_debugfs_nodelist_open(struct inode *inode, struct file *file)
{
	struct lpfc_vport *vport = inode->i_private;
	struct lpfc_debug *debug;
	int rc = -ENOMEM;

	debug = kmalloc(sizeof(*debug), GFP_KERNEL);
	if (!debug)
		goto out;

	/* Round to page boundry */
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

static ssize_t
lpfc_debugfs_read(struct file *file, char __user *buf,
		  size_t nbytes, loff_t *ppos)
{
	struct lpfc_debug *debug = file->private_data;
	return simple_read_from_buffer(buf, nbytes, ppos, debug->buffer,
				       debug->len);
}

static int
lpfc_debugfs_release(struct inode *inode, struct file *file)
{
	struct lpfc_debug *debug = file->private_data;

	kfree(debug->buffer);
	kfree(debug);

	return 0;
}

#undef lpfc_debugfs_op_disc_trc
static struct file_operations lpfc_debugfs_op_disc_trc = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_disc_trc_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_nodelist
static struct file_operations lpfc_debugfs_op_nodelist = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_nodelist_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_hbqinfo
static struct file_operations lpfc_debugfs_op_hbqinfo = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_hbqinfo_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_dumpHBASlim
static struct file_operations lpfc_debugfs_op_dumpHBASlim = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_dumpHBASlim_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_dumpHostSlim
static struct file_operations lpfc_debugfs_op_dumpHostSlim = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_dumpHostSlim_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.release =      lpfc_debugfs_release,
};

#undef lpfc_debugfs_op_slow_ring_trc
static struct file_operations lpfc_debugfs_op_slow_ring_trc = {
	.owner =        THIS_MODULE,
	.open =         lpfc_debugfs_slow_ring_trc_open,
	.llseek =       lpfc_debugfs_lseek,
	.read =         lpfc_debugfs_read,
	.release =      lpfc_debugfs_release,
};

static struct dentry *lpfc_debugfs_root = NULL;
static atomic_t lpfc_debugfs_hba_count;
#endif

inline void
lpfc_debugfs_initialize(struct lpfc_vport *vport)
{
#ifdef CONFIG_LPFC_DEBUG_FS
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
					 "0409 Cannot create debugfs root\n");
			goto debug_failed;
		}
	}
	if (!lpfc_debugfs_start_time)
		lpfc_debugfs_start_time = jiffies;

	/* Setup lpfcX directory for specific HBA */
	snprintf(name, sizeof(name), "lpfc%d", phba->brd_no);
	if (!phba->hba_debugfs_root) {
		phba->hba_debugfs_root =
			debugfs_create_dir(name, lpfc_debugfs_root);
		if (!phba->hba_debugfs_root) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "0409 Cannot create debugfs hba\n");
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
				"0409 Cannot create debugfs hbqinfo\n");
			goto debug_failed;
		}

		/* Setup dumpHBASlim */
		snprintf(name, sizeof(name), "dumpHBASlim");
		phba->debug_dumpHBASlim =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				 phba->hba_debugfs_root,
				 phba, &lpfc_debugfs_op_dumpHBASlim);
		if (!phba->debug_dumpHBASlim) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				"0409 Cannot create debugfs dumpHBASlim\n");
			goto debug_failed;
		}

		/* Setup dumpHostSlim */
		snprintf(name, sizeof(name), "dumpHostSlim");
		phba->debug_dumpHostSlim =
			debugfs_create_file(name, S_IFREG|S_IRUGO|S_IWUSR,
				 phba->hba_debugfs_root,
				 phba, &lpfc_debugfs_op_dumpHostSlim);
		if (!phba->debug_dumpHostSlim) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				"0409 Cannot create debugfs dumpHostSlim\n");
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
					 "0409 Cannot create debugfs "
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
						 "0409 Cannot create debugfs "
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
					 "0409 Cant create debugfs");
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
				 "0409 Cannot create debugfs disc trace "
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
				 "0409 Cannot create debugfs "
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
				 "0409 Cant create debugfs nodelist");
		goto debug_failed;
	}
debug_failed:
	return;
#endif
}


inline void
lpfc_debugfs_terminate(struct lpfc_vport *vport)
{
#ifdef CONFIG_LPFC_DEBUG_FS
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
		if (phba->slow_ring_trc) {
			kfree(phba->slow_ring_trc);
			phba->slow_ring_trc = NULL;
		}
		if (phba->debug_slow_ring_trc) {
			/* slow_ring_trace */
			debugfs_remove(phba->debug_slow_ring_trc);
			phba->debug_slow_ring_trc = NULL;
		}

		if (phba->hba_debugfs_root) {
			debugfs_remove(phba->hba_debugfs_root); /* lpfcX */
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


