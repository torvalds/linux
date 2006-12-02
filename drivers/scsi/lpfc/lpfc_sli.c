/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2006 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 * Portions Copyright (C) 2004-2005 Christoph Hellwig              *
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
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_crtn.h"
#include "lpfc_logmsg.h"
#include "lpfc_compat.h"

/*
 * Define macro to log: Mailbox command x%x cannot issue Data
 * This allows multiple uses of lpfc_msgBlk0311
 * w/o perturbing log msg utility.
 */
#define LOG_MBOX_CANNOT_ISSUE_DATA( phba, mb, psli, flag) \
			lpfc_printf_log(phba, \
				KERN_INFO, \
				LOG_MBOX | LOG_SLI, \
				"%d:0311 Mailbox command x%x cannot issue " \
				"Data: x%x x%x x%x\n", \
				phba->brd_no, \
				mb->mbxCommand,		\
				phba->hba_state,	\
				psli->sli_flag,	\
				flag);


/* There are only four IOCB completion types. */
typedef enum _lpfc_iocb_type {
	LPFC_UNKNOWN_IOCB,
	LPFC_UNSOL_IOCB,
	LPFC_SOL_IOCB,
	LPFC_ABORT_IOCB
} lpfc_iocb_type;

struct lpfc_iocbq *
lpfc_sli_get_iocbq(struct lpfc_hba * phba)
{
	struct list_head *lpfc_iocb_list = &phba->lpfc_iocb_list;
	struct lpfc_iocbq * iocbq = NULL;

	list_remove_head(lpfc_iocb_list, iocbq, struct lpfc_iocbq, list);
	return iocbq;
}

void
lpfc_sli_release_iocbq(struct lpfc_hba * phba, struct lpfc_iocbq * iocbq)
{
	size_t start_clean = (size_t)(&((struct lpfc_iocbq *)NULL)->iocb);

	/*
	 * Clean all volatile data fields, preserve iotag and node struct.
	 */
	memset((char*)iocbq + start_clean, 0, sizeof(*iocbq) - start_clean);
	list_add_tail(&iocbq->list, &phba->lpfc_iocb_list);
}

/*
 * Translate the iocb command to an iocb command type used to decide the final
 * disposition of each completed IOCB.
 */
static lpfc_iocb_type
lpfc_sli_iocb_cmd_type(uint8_t iocb_cmnd)
{
	lpfc_iocb_type type = LPFC_UNKNOWN_IOCB;

	if (iocb_cmnd > CMD_MAX_IOCB_CMD)
		return 0;

	switch (iocb_cmnd) {
	case CMD_XMIT_SEQUENCE_CR:
	case CMD_XMIT_SEQUENCE_CX:
	case CMD_XMIT_BCAST_CN:
	case CMD_XMIT_BCAST_CX:
	case CMD_ELS_REQUEST_CR:
	case CMD_ELS_REQUEST_CX:
	case CMD_CREATE_XRI_CR:
	case CMD_CREATE_XRI_CX:
	case CMD_GET_RPI_CN:
	case CMD_XMIT_ELS_RSP_CX:
	case CMD_GET_RPI_CR:
	case CMD_FCP_IWRITE_CR:
	case CMD_FCP_IWRITE_CX:
	case CMD_FCP_IREAD_CR:
	case CMD_FCP_IREAD_CX:
	case CMD_FCP_ICMND_CR:
	case CMD_FCP_ICMND_CX:
	case CMD_ADAPTER_MSG:
	case CMD_ADAPTER_DUMP:
	case CMD_XMIT_SEQUENCE64_CR:
	case CMD_XMIT_SEQUENCE64_CX:
	case CMD_XMIT_BCAST64_CN:
	case CMD_XMIT_BCAST64_CX:
	case CMD_ELS_REQUEST64_CR:
	case CMD_ELS_REQUEST64_CX:
	case CMD_FCP_IWRITE64_CR:
	case CMD_FCP_IWRITE64_CX:
	case CMD_FCP_IREAD64_CR:
	case CMD_FCP_IREAD64_CX:
	case CMD_FCP_ICMND64_CR:
	case CMD_FCP_ICMND64_CX:
	case CMD_GEN_REQUEST64_CR:
	case CMD_GEN_REQUEST64_CX:
	case CMD_XMIT_ELS_RSP64_CX:
		type = LPFC_SOL_IOCB;
		break;
	case CMD_ABORT_XRI_CN:
	case CMD_ABORT_XRI_CX:
	case CMD_CLOSE_XRI_CN:
	case CMD_CLOSE_XRI_CX:
	case CMD_XRI_ABORTED_CX:
	case CMD_ABORT_MXRI64_CN:
		type = LPFC_ABORT_IOCB;
		break;
	case CMD_RCV_SEQUENCE_CX:
	case CMD_RCV_ELS_REQ_CX:
	case CMD_RCV_SEQUENCE64_CX:
	case CMD_RCV_ELS_REQ64_CX:
		type = LPFC_UNSOL_IOCB;
		break;
	default:
		type = LPFC_UNKNOWN_IOCB;
		break;
	}

	return type;
}

static int
lpfc_sli_ring_map(struct lpfc_hba * phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_sli *psli = &phba->sli;
	MAILBOX_t *pmbox = &pmb->mb;
	int i, rc;

	for (i = 0; i < psli->num_rings; i++) {
		phba->hba_state = LPFC_INIT_MBX_CMDS;
		lpfc_config_ring(phba, i, pmb);
		rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);
		if (rc != MBX_SUCCESS) {
			lpfc_printf_log(phba,
					KERN_ERR,
					LOG_INIT,
					"%d:0446 Adapter failed to init, "
					"mbxCmd x%x CFG_RING, mbxStatus x%x, "
					"ring %d\n",
					phba->brd_no,
					pmbox->mbxCommand,
					pmbox->mbxStatus,
					i);
			phba->hba_state = LPFC_HBA_ERROR;
			return -ENXIO;
		}
	}
	return 0;
}

static int
lpfc_sli_ringtxcmpl_put(struct lpfc_hba * phba,
			struct lpfc_sli_ring * pring, struct lpfc_iocbq * piocb)
{
	list_add_tail(&piocb->list, &pring->txcmplq);
	pring->txcmplq_cnt++;
	if (unlikely(pring->ringno == LPFC_ELS_RING))
		mod_timer(&phba->els_tmofunc,
					jiffies + HZ * (phba->fc_ratov << 1));

	return (0);
}

static struct lpfc_iocbq *
lpfc_sli_ringtx_get(struct lpfc_hba * phba, struct lpfc_sli_ring * pring)
{
	struct list_head *dlp;
	struct lpfc_iocbq *cmd_iocb;

	dlp = &pring->txq;
	cmd_iocb = NULL;
	list_remove_head((&pring->txq), cmd_iocb,
			 struct lpfc_iocbq,
			 list);
	if (cmd_iocb) {
		/* If the first ptr is not equal to the list header,
		 * deque the IOCBQ_t and return it.
		 */
		pring->txq_cnt--;
	}
	return (cmd_iocb);
}

static IOCB_t *
lpfc_sli_next_iocb_slot (struct lpfc_hba *phba, struct lpfc_sli_ring *pring)
{
	struct lpfc_pgp *pgp = &phba->slim2p->mbx.us.s2.port[pring->ringno];
	uint32_t  max_cmd_idx = pring->numCiocb;
	IOCB_t *iocb = NULL;

	if ((pring->next_cmdidx == pring->cmdidx) &&
	   (++pring->next_cmdidx >= max_cmd_idx))
		pring->next_cmdidx = 0;

	if (unlikely(pring->local_getidx == pring->next_cmdidx)) {

		pring->local_getidx = le32_to_cpu(pgp->cmdGetInx);

		if (unlikely(pring->local_getidx >= max_cmd_idx)) {
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"%d:0315 Ring %d issue: portCmdGet %d "
					"is bigger then cmd ring %d\n",
					phba->brd_no, pring->ringno,
					pring->local_getidx, max_cmd_idx);

			phba->hba_state = LPFC_HBA_ERROR;
			/*
			 * All error attention handlers are posted to
			 * worker thread
			 */
			phba->work_ha |= HA_ERATT;
			phba->work_hs = HS_FFER3;
			if (phba->work_wait)
				wake_up(phba->work_wait);

			return NULL;
		}

		if (pring->local_getidx == pring->next_cmdidx)
			return NULL;
	}

	iocb = IOCB_ENTRY(pring->cmdringaddr, pring->cmdidx);

	return iocb;
}

uint16_t
lpfc_sli_next_iotag(struct lpfc_hba * phba, struct lpfc_iocbq * iocbq)
{
	struct lpfc_iocbq ** new_arr;
	struct lpfc_iocbq ** old_arr;
	size_t new_len;
	struct lpfc_sli *psli = &phba->sli;
	uint16_t iotag;

	spin_lock_irq(phba->host->host_lock);
	iotag = psli->last_iotag;
	if(++iotag < psli->iocbq_lookup_len) {
		psli->last_iotag = iotag;
		psli->iocbq_lookup[iotag] = iocbq;
		spin_unlock_irq(phba->host->host_lock);
		iocbq->iotag = iotag;
		return iotag;
	}
	else if (psli->iocbq_lookup_len < (0xffff
					   - LPFC_IOCBQ_LOOKUP_INCREMENT)) {
		new_len = psli->iocbq_lookup_len + LPFC_IOCBQ_LOOKUP_INCREMENT;
		spin_unlock_irq(phba->host->host_lock);
		new_arr = kmalloc(new_len * sizeof (struct lpfc_iocbq *),
				  GFP_KERNEL);
		if (new_arr) {
			memset((char *)new_arr, 0,
			       new_len * sizeof (struct lpfc_iocbq *));
			spin_lock_irq(phba->host->host_lock);
			old_arr = psli->iocbq_lookup;
			if (new_len <= psli->iocbq_lookup_len) {
				/* highly unprobable case */
				kfree(new_arr);
				iotag = psli->last_iotag;
				if(++iotag < psli->iocbq_lookup_len) {
					psli->last_iotag = iotag;
					psli->iocbq_lookup[iotag] = iocbq;
					spin_unlock_irq(phba->host->host_lock);
					iocbq->iotag = iotag;
					return iotag;
				}
				spin_unlock_irq(phba->host->host_lock);
				return 0;
			}
			if (psli->iocbq_lookup)
				memcpy(new_arr, old_arr,
				       ((psli->last_iotag  + 1) *
	 				sizeof (struct lpfc_iocbq *)));
			psli->iocbq_lookup = new_arr;
			psli->iocbq_lookup_len = new_len;
			psli->last_iotag = iotag;
			psli->iocbq_lookup[iotag] = iocbq;
			spin_unlock_irq(phba->host->host_lock);
			iocbq->iotag = iotag;
			kfree(old_arr);
			return iotag;
		}
	} else
		spin_unlock_irq(phba->host->host_lock);

	lpfc_printf_log(phba, KERN_ERR,LOG_SLI,
			"%d:0318 Failed to allocate IOTAG.last IOTAG is %d\n",
			phba->brd_no, psli->last_iotag);

	return 0;
}

static void
lpfc_sli_submit_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		IOCB_t *iocb, struct lpfc_iocbq *nextiocb)
{
	/*
	 * Set up an iotag
	 */
	nextiocb->iocb.ulpIoTag = (nextiocb->iocb_cmpl) ? nextiocb->iotag : 0;

	/*
	 * Issue iocb command to adapter
	 */
	lpfc_sli_pcimem_bcopy(&nextiocb->iocb, iocb, sizeof (IOCB_t));
	wmb();
	pring->stats.iocb_cmd++;

	/*
	 * If there is no completion routine to call, we can release the
	 * IOCB buffer back right now. For IOCBs, like QUE_RING_BUF,
	 * that have no rsp ring completion, iocb_cmpl MUST be NULL.
	 */
	if (nextiocb->iocb_cmpl)
		lpfc_sli_ringtxcmpl_put(phba, pring, nextiocb);
	else
		lpfc_sli_release_iocbq(phba, nextiocb);

	/*
	 * Let the HBA know what IOCB slot will be the next one the
	 * driver will put a command into.
	 */
	pring->cmdidx = pring->next_cmdidx;
	writel(pring->cmdidx, phba->MBslimaddr
	       + (SLIMOFF + (pring->ringno * 2)) * 4);
}

static void
lpfc_sli_update_full_ring(struct lpfc_hba * phba,
			  struct lpfc_sli_ring *pring)
{
	int ringno = pring->ringno;

	pring->flag |= LPFC_CALL_RING_AVAILABLE;

	wmb();

	/*
	 * Set ring 'ringno' to SET R0CE_REQ in Chip Att register.
	 * The HBA will tell us when an IOCB entry is available.
	 */
	writel((CA_R0ATT|CA_R0CE_REQ) << (ringno*4), phba->CAregaddr);
	readl(phba->CAregaddr); /* flush */

	pring->stats.iocb_cmd_full++;
}

static void
lpfc_sli_update_ring(struct lpfc_hba * phba,
		     struct lpfc_sli_ring *pring)
{
	int ringno = pring->ringno;

	/*
	 * Tell the HBA that there is work to do in this ring.
	 */
	wmb();
	writel(CA_R0ATT << (ringno * 4), phba->CAregaddr);
	readl(phba->CAregaddr); /* flush */
}

static void
lpfc_sli_resume_iocb(struct lpfc_hba * phba, struct lpfc_sli_ring * pring)
{
	IOCB_t *iocb;
	struct lpfc_iocbq *nextiocb;

	/*
	 * Check to see if:
	 *  (a) there is anything on the txq to send
	 *  (b) link is up
	 *  (c) link attention events can be processed (fcp ring only)
	 *  (d) IOCB processing is not blocked by the outstanding mbox command.
	 */
	if (pring->txq_cnt &&
	    (phba->hba_state > LPFC_LINK_DOWN) &&
	    (pring->ringno != phba->sli.fcp_ring ||
	     phba->sli.sli_flag & LPFC_PROCESS_LA) &&
	    !(pring->flag & LPFC_STOP_IOCB_MBX)) {

		while ((iocb = lpfc_sli_next_iocb_slot(phba, pring)) &&
		       (nextiocb = lpfc_sli_ringtx_get(phba, pring)))
			lpfc_sli_submit_iocb(phba, pring, iocb, nextiocb);

		if (iocb)
			lpfc_sli_update_ring(phba, pring);
		else
			lpfc_sli_update_full_ring(phba, pring);
	}

	return;
}

/* lpfc_sli_turn_on_ring is only called by lpfc_sli_handle_mb_event below */
static void
lpfc_sli_turn_on_ring(struct lpfc_hba * phba, int ringno)
{
	struct lpfc_pgp *pgp = &phba->slim2p->mbx.us.s2.port[ringno];

	/* If the ring is active, flag it */
	if (phba->sli.ring[ringno].cmdringaddr) {
		if (phba->sli.ring[ringno].flag & LPFC_STOP_IOCB_MBX) {
			phba->sli.ring[ringno].flag &= ~LPFC_STOP_IOCB_MBX;
			/*
			 * Force update of the local copy of cmdGetInx
			 */
			phba->sli.ring[ringno].local_getidx
				= le32_to_cpu(pgp->cmdGetInx);
			spin_lock_irq(phba->host->host_lock);
			lpfc_sli_resume_iocb(phba, &phba->sli.ring[ringno]);
			spin_unlock_irq(phba->host->host_lock);
		}
	}
}

static int
lpfc_sli_chk_mbx_command(uint8_t mbxCommand)
{
	uint8_t ret;

	switch (mbxCommand) {
	case MBX_LOAD_SM:
	case MBX_READ_NV:
	case MBX_WRITE_NV:
	case MBX_RUN_BIU_DIAG:
	case MBX_INIT_LINK:
	case MBX_DOWN_LINK:
	case MBX_CONFIG_LINK:
	case MBX_CONFIG_RING:
	case MBX_RESET_RING:
	case MBX_READ_CONFIG:
	case MBX_READ_RCONFIG:
	case MBX_READ_SPARM:
	case MBX_READ_STATUS:
	case MBX_READ_RPI:
	case MBX_READ_XRI:
	case MBX_READ_REV:
	case MBX_READ_LNK_STAT:
	case MBX_REG_LOGIN:
	case MBX_UNREG_LOGIN:
	case MBX_READ_LA:
	case MBX_CLEAR_LA:
	case MBX_DUMP_MEMORY:
	case MBX_DUMP_CONTEXT:
	case MBX_RUN_DIAGS:
	case MBX_RESTART:
	case MBX_UPDATE_CFG:
	case MBX_DOWN_LOAD:
	case MBX_DEL_LD_ENTRY:
	case MBX_RUN_PROGRAM:
	case MBX_SET_MASK:
	case MBX_SET_SLIM:
	case MBX_UNREG_D_ID:
	case MBX_KILL_BOARD:
	case MBX_CONFIG_FARP:
	case MBX_BEACON:
	case MBX_LOAD_AREA:
	case MBX_RUN_BIU_DIAG64:
	case MBX_CONFIG_PORT:
	case MBX_READ_SPARM64:
	case MBX_READ_RPI64:
	case MBX_REG_LOGIN64:
	case MBX_READ_LA64:
	case MBX_FLASH_WR_ULA:
	case MBX_SET_DEBUG:
	case MBX_LOAD_EXP_ROM:
		ret = mbxCommand;
		break;
	default:
		ret = MBX_SHUTDOWN;
		break;
	}
	return (ret);
}
static void
lpfc_sli_wake_mbox_wait(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmboxq)
{
	wait_queue_head_t *pdone_q;

	/*
	 * If pdone_q is empty, the driver thread gave up waiting and
	 * continued running.
	 */
	pdone_q = (wait_queue_head_t *) pmboxq->context1;
	if (pdone_q)
		wake_up_interruptible(pdone_q);
	return;
}

void
lpfc_sli_def_mbox_cmpl(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_dmabuf *mp;
	mp = (struct lpfc_dmabuf *) (pmb->context1);
	if (mp) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
	}
	mempool_free( pmb, phba->mbox_mem_pool);
	return;
}

int
lpfc_sli_handle_mb_event(struct lpfc_hba * phba)
{
	MAILBOX_t *mbox;
	MAILBOX_t *pmbox;
	LPFC_MBOXQ_t *pmb;
	struct lpfc_sli *psli;
	int i, rc;
	uint32_t process_next;

	psli = &phba->sli;
	/* We should only get here if we are in SLI2 mode */
	if (!(phba->sli.sli_flag & LPFC_SLI2_ACTIVE)) {
		return (1);
	}

	phba->sli.slistat.mbox_event++;

	/* Get a Mailbox buffer to setup mailbox commands for callback */
	if ((pmb = phba->sli.mbox_active)) {
		pmbox = &pmb->mb;
		mbox = &phba->slim2p->mbx;

		/* First check out the status word */
		lpfc_sli_pcimem_bcopy(mbox, pmbox, sizeof (uint32_t));

		/* Sanity check to ensure the host owns the mailbox */
		if (pmbox->mbxOwner != OWN_HOST) {
			/* Lets try for a while */
			for (i = 0; i < 10240; i++) {
				/* First copy command data */
				lpfc_sli_pcimem_bcopy(mbox, pmbox,
							sizeof (uint32_t));
				if (pmbox->mbxOwner == OWN_HOST)
					goto mbout;
			}
			/* Stray Mailbox Interrupt, mbxCommand <cmd> mbxStatus
			   <status> */
			lpfc_printf_log(phba,
					KERN_WARNING,
					LOG_MBOX | LOG_SLI,
					"%d:0304 Stray Mailbox Interrupt "
					"mbxCommand x%x mbxStatus x%x\n",
					phba->brd_no,
					pmbox->mbxCommand,
					pmbox->mbxStatus);

			spin_lock_irq(phba->host->host_lock);
			phba->sli.sli_flag |= LPFC_SLI_MBOX_ACTIVE;
			spin_unlock_irq(phba->host->host_lock);
			return (1);
		}

	      mbout:
		del_timer_sync(&phba->sli.mbox_tmo);
		phba->work_hba_events &= ~WORKER_MBOX_TMO;

		/*
		 * It is a fatal error if unknown mbox command completion.
		 */
		if (lpfc_sli_chk_mbx_command(pmbox->mbxCommand) ==
		    MBX_SHUTDOWN) {

			/* Unknow mailbox command compl */
			lpfc_printf_log(phba,
				KERN_ERR,
				LOG_MBOX | LOG_SLI,
				"%d:0323 Unknown Mailbox command %x Cmpl\n",
				phba->brd_no,
				pmbox->mbxCommand);
			phba->hba_state = LPFC_HBA_ERROR;
			phba->work_hs = HS_FFER3;
			lpfc_handle_eratt(phba);
			return (0);
		}

		phba->sli.mbox_active = NULL;
		if (pmbox->mbxStatus) {
			phba->sli.slistat.mbox_stat_err++;
			if (pmbox->mbxStatus == MBXERR_NO_RESOURCES) {
				/* Mbox cmd cmpl error - RETRYing */
				lpfc_printf_log(phba,
					KERN_INFO,
					LOG_MBOX | LOG_SLI,
					"%d:0305 Mbox cmd cmpl error - "
					"RETRYing Data: x%x x%x x%x x%x\n",
					phba->brd_no,
					pmbox->mbxCommand,
					pmbox->mbxStatus,
					pmbox->un.varWords[0],
					phba->hba_state);
				pmbox->mbxStatus = 0;
				pmbox->mbxOwner = OWN_HOST;
				spin_lock_irq(phba->host->host_lock);
				phba->sli.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
				spin_unlock_irq(phba->host->host_lock);
				rc = lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT);
				if (rc == MBX_SUCCESS)
					return (0);
			}
		}

		/* Mailbox cmd <cmd> Cmpl <cmpl> */
		lpfc_printf_log(phba,
				KERN_INFO,
				LOG_MBOX | LOG_SLI,
				"%d:0307 Mailbox cmd x%x Cmpl x%p "
				"Data: x%x x%x x%x x%x x%x x%x x%x x%x x%x\n",
				phba->brd_no,
				pmbox->mbxCommand,
				pmb->mbox_cmpl,
				*((uint32_t *) pmbox),
				pmbox->un.varWords[0],
				pmbox->un.varWords[1],
				pmbox->un.varWords[2],
				pmbox->un.varWords[3],
				pmbox->un.varWords[4],
				pmbox->un.varWords[5],
				pmbox->un.varWords[6],
				pmbox->un.varWords[7]);

		if (pmb->mbox_cmpl) {
			lpfc_sli_pcimem_bcopy(mbox, pmbox, MAILBOX_CMD_SIZE);
			pmb->mbox_cmpl(phba,pmb);
		}
	}


	do {
		process_next = 0;	/* by default don't loop */
		spin_lock_irq(phba->host->host_lock);
		phba->sli.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;

		/* Process next mailbox command if there is one */
		if ((pmb = lpfc_mbox_get(phba))) {
			spin_unlock_irq(phba->host->host_lock);
			rc = lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT);
			if (rc == MBX_NOT_FINISHED) {
				pmb->mb.mbxStatus = MBX_NOT_FINISHED;
				pmb->mbox_cmpl(phba,pmb);
				process_next = 1;
				continue;	/* loop back */
			}
		} else {
			spin_unlock_irq(phba->host->host_lock);
			/* Turn on IOCB processing */
			for (i = 0; i < phba->sli.num_rings; i++) {
				lpfc_sli_turn_on_ring(phba, i);
			}

			/* Free any lpfc_dmabuf's waiting for mbox cmd cmpls */
			while (!list_empty(&phba->freebufList)) {
				struct lpfc_dmabuf *mp;

				mp = NULL;
				list_remove_head((&phba->freebufList),
						 mp,
						 struct lpfc_dmabuf,
						 list);
				if (mp) {
					lpfc_mbuf_free(phba, mp->virt,
						       mp->phys);
					kfree(mp);
				}
			}
		}

	} while (process_next);

	return (0);
}
static int
lpfc_sli_process_unsol_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
			    struct lpfc_iocbq *saveq)
{
	IOCB_t           * irsp;
	WORD5            * w5p;
	uint32_t           Rctl, Type;
	uint32_t           match, i;

	match = 0;
	irsp = &(saveq->iocb);
	if ((irsp->ulpCommand == CMD_RCV_ELS_REQ64_CX)
	    || (irsp->ulpCommand == CMD_RCV_ELS_REQ_CX)) {
		Rctl = FC_ELS_REQ;
		Type = FC_ELS_DATA;
	} else {
		w5p =
		    (WORD5 *) & (saveq->iocb.un.
				 ulpWord[5]);
		Rctl = w5p->hcsw.Rctl;
		Type = w5p->hcsw.Type;

		/* Firmware Workaround */
		if ((Rctl == 0) && (pring->ringno == LPFC_ELS_RING) &&
			(irsp->ulpCommand == CMD_RCV_SEQUENCE64_CX)) {
			Rctl = FC_ELS_REQ;
			Type = FC_ELS_DATA;
			w5p->hcsw.Rctl = Rctl;
			w5p->hcsw.Type = Type;
		}
	}
	/* unSolicited Responses */
	if (pring->prt[0].profile) {
		if (pring->prt[0].lpfc_sli_rcv_unsol_event)
			(pring->prt[0].lpfc_sli_rcv_unsol_event) (phba, pring,
									saveq);
		match = 1;
	} else {
		/* We must search, based on rctl / type
		   for the right routine */
		for (i = 0; i < pring->num_mask;
		     i++) {
			if ((pring->prt[i].rctl ==
			     Rctl)
			    && (pring->prt[i].
				type == Type)) {
				if (pring->prt[i].lpfc_sli_rcv_unsol_event)
					(pring->prt[i].lpfc_sli_rcv_unsol_event)
							(phba, pring, saveq);
				match = 1;
				break;
			}
		}
	}
	if (match == 0) {
		/* Unexpected Rctl / Type received */
		/* Ring <ringno> handler: unexpected
		   Rctl <Rctl> Type <Type> received */
		lpfc_printf_log(phba,
				KERN_WARNING,
				LOG_SLI,
				"%d:0313 Ring %d handler: unexpected Rctl x%x "
				"Type x%x received \n",
				phba->brd_no,
				pring->ringno,
				Rctl,
				Type);
	}
	return(1);
}

static struct lpfc_iocbq *
lpfc_sli_iocbq_lookup(struct lpfc_hba * phba,
		      struct lpfc_sli_ring * pring,
		      struct lpfc_iocbq * prspiocb)
{
	struct lpfc_iocbq *cmd_iocb = NULL;
	uint16_t iotag;

	iotag = prspiocb->iocb.ulpIoTag;

	if (iotag != 0 && iotag <= phba->sli.last_iotag) {
		cmd_iocb = phba->sli.iocbq_lookup[iotag];
		list_del(&cmd_iocb->list);
		pring->txcmplq_cnt--;
		return cmd_iocb;
	}

	lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
			"%d:0317 iotag x%x is out off "
			"range: max iotag x%x wd0 x%x\n",
			phba->brd_no, iotag,
			phba->sli.last_iotag,
			*(((uint32_t *) &prspiocb->iocb) + 7));
	return NULL;
}

static int
lpfc_sli_process_sol_iocb(struct lpfc_hba * phba, struct lpfc_sli_ring * pring,
			  struct lpfc_iocbq *saveq)
{
	struct lpfc_iocbq * cmdiocbp;
	int rc = 1;
	unsigned long iflag;

	/* Based on the iotag field, get the cmd IOCB from the txcmplq */
	spin_lock_irqsave(phba->host->host_lock, iflag);
	cmdiocbp = lpfc_sli_iocbq_lookup(phba, pring, saveq);
	if (cmdiocbp) {
		if (cmdiocbp->iocb_cmpl) {
			/*
			 * Post all ELS completions to the worker thread.
			 * All other are passed to the completion callback.
			 */
			if (pring->ringno == LPFC_ELS_RING) {
				spin_unlock_irqrestore(phba->host->host_lock,
						       iflag);
				(cmdiocbp->iocb_cmpl) (phba, cmdiocbp, saveq);
				spin_lock_irqsave(phba->host->host_lock, iflag);
			}
			else {
				spin_unlock_irqrestore(phba->host->host_lock,
						       iflag);
				(cmdiocbp->iocb_cmpl) (phba, cmdiocbp, saveq);
				spin_lock_irqsave(phba->host->host_lock, iflag);
			}
		} else
			lpfc_sli_release_iocbq(phba, cmdiocbp);
	} else {
		/*
		 * Unknown initiating command based on the response iotag.
		 * This could be the case on the ELS ring because of
		 * lpfc_els_abort().
		 */
		if (pring->ringno != LPFC_ELS_RING) {
			/*
			 * Ring <ringno> handler: unexpected completion IoTag
			 * <IoTag>
			 */
			lpfc_printf_log(phba,
				KERN_WARNING,
				LOG_SLI,
				"%d:0322 Ring %d handler: unexpected "
				"completion IoTag x%x Data: x%x x%x x%x x%x\n",
				phba->brd_no,
				pring->ringno,
				saveq->iocb.ulpIoTag,
				saveq->iocb.ulpStatus,
				saveq->iocb.un.ulpWord[4],
				saveq->iocb.ulpCommand,
				saveq->iocb.ulpContext);
		}
	}

	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return rc;
}

static void lpfc_sli_rsp_pointers_error(struct lpfc_hba * phba,
					struct lpfc_sli_ring * pring)
{
	struct lpfc_pgp *pgp = &phba->slim2p->mbx.us.s2.port[pring->ringno];
	/*
	 * Ring <ringno> handler: portRspPut <portRspPut> is bigger then
	 * rsp ring <portRspMax>
	 */
	lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
			"%d:0312 Ring %d handler: portRspPut %d "
			"is bigger then rsp ring %d\n",
			phba->brd_no, pring->ringno,
			le32_to_cpu(pgp->rspPutInx),
			pring->numRiocb);

	phba->hba_state = LPFC_HBA_ERROR;

	/*
	 * All error attention handlers are posted to
	 * worker thread
	 */
	phba->work_ha |= HA_ERATT;
	phba->work_hs = HS_FFER3;
	if (phba->work_wait)
		wake_up(phba->work_wait);

	return;
}

void lpfc_sli_poll_fcp_ring(struct lpfc_hba * phba)
{
	struct lpfc_sli      * psli   = &phba->sli;
	struct lpfc_sli_ring * pring = &psli->ring[LPFC_FCP_RING];
	IOCB_t *irsp = NULL;
	IOCB_t *entry = NULL;
	struct lpfc_iocbq *cmdiocbq = NULL;
	struct lpfc_iocbq rspiocbq;
	struct lpfc_pgp *pgp;
	uint32_t status;
	uint32_t portRspPut, portRspMax;
	int type;
	uint32_t rsp_cmpl = 0;
	void __iomem *to_slim;
	uint32_t ha_copy;

	pring->stats.iocb_event++;

	/* The driver assumes SLI-2 mode */
	pgp =  &phba->slim2p->mbx.us.s2.port[pring->ringno];

	/*
	 * The next available response entry should never exceed the maximum
	 * entries.  If it does, treat it as an adapter hardware error.
	 */
	portRspMax = pring->numRiocb;
	portRspPut = le32_to_cpu(pgp->rspPutInx);
	if (unlikely(portRspPut >= portRspMax)) {
		lpfc_sli_rsp_pointers_error(phba, pring);
		return;
	}

	rmb();
	while (pring->rspidx != portRspPut) {

		entry = IOCB_ENTRY(pring->rspringaddr, pring->rspidx);

		if (++pring->rspidx >= portRspMax)
			pring->rspidx = 0;

		lpfc_sli_pcimem_bcopy((uint32_t *) entry,
				      (uint32_t *) &rspiocbq.iocb,
				      sizeof (IOCB_t));
		irsp = &rspiocbq.iocb;
		type = lpfc_sli_iocb_cmd_type(irsp->ulpCommand & CMD_IOCB_MASK);
		pring->stats.iocb_rsp++;
		rsp_cmpl++;

		if (unlikely(irsp->ulpStatus)) {
			/* Rsp ring <ringno> error: IOCB */
			lpfc_printf_log(phba, KERN_WARNING, LOG_SLI,
					"%d:0326 Rsp Ring %d error: IOCB Data: "
					"x%x x%x x%x x%x x%x x%x x%x x%x\n",
					phba->brd_no, pring->ringno,
					irsp->un.ulpWord[0],
					irsp->un.ulpWord[1],
					irsp->un.ulpWord[2],
					irsp->un.ulpWord[3],
					irsp->un.ulpWord[4],
					irsp->un.ulpWord[5],
					*(((uint32_t *) irsp) + 6),
					*(((uint32_t *) irsp) + 7));
		}

		switch (type) {
		case LPFC_ABORT_IOCB:
		case LPFC_SOL_IOCB:
			/*
			 * Idle exchange closed via ABTS from port.  No iocb
			 * resources need to be recovered.
			 */
			if (unlikely(irsp->ulpCommand == CMD_XRI_ABORTED_CX)) {
				lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
						"%d:0314 IOCB cmd 0x%x"
						" processed. Skipping"
						" completion", phba->brd_no,
						irsp->ulpCommand);
				break;
			}

			cmdiocbq = lpfc_sli_iocbq_lookup(phba, pring,
							 &rspiocbq);
			if ((cmdiocbq) && (cmdiocbq->iocb_cmpl)) {
				(cmdiocbq->iocb_cmpl)(phba, cmdiocbq,
						      &rspiocbq);
			}
			break;
		default:
			if (irsp->ulpCommand == CMD_ADAPTER_MSG) {
				char adaptermsg[LPFC_MAX_ADPTMSG];
				memset(adaptermsg, 0, LPFC_MAX_ADPTMSG);
				memcpy(&adaptermsg[0], (uint8_t *) irsp,
				       MAX_MSG_DATA);
				dev_warn(&((phba->pcidev)->dev), "lpfc%d: %s",
					 phba->brd_no, adaptermsg);
			} else {
				/* Unknown IOCB command */
				lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
						"%d:0321 Unknown IOCB command "
						"Data: x%x, x%x x%x x%x x%x\n",
						phba->brd_no, type,
						irsp->ulpCommand,
						irsp->ulpStatus,
						irsp->ulpIoTag,
						irsp->ulpContext);
			}
			break;
		}

		/*
		 * The response IOCB has been processed.  Update the ring
		 * pointer in SLIM.  If the port response put pointer has not
		 * been updated, sync the pgp->rspPutInx and fetch the new port
		 * response put pointer.
		 */
		to_slim = phba->MBslimaddr +
			(SLIMOFF + (pring->ringno * 2) + 1) * 4;
		writeb(pring->rspidx, to_slim);

		if (pring->rspidx == portRspPut)
			portRspPut = le32_to_cpu(pgp->rspPutInx);
	}

	ha_copy = readl(phba->HAregaddr);
	ha_copy >>= (LPFC_FCP_RING * 4);

	if ((rsp_cmpl > 0) && (ha_copy & HA_R0RE_REQ)) {
		pring->stats.iocb_rsp_full++;
		status = ((CA_R0ATT | CA_R0RE_RSP) << (LPFC_FCP_RING * 4));
		writel(status, phba->CAregaddr);
		readl(phba->CAregaddr);
	}
	if ((ha_copy & HA_R0CE_RSP) &&
	    (pring->flag & LPFC_CALL_RING_AVAILABLE)) {
		pring->flag &= ~LPFC_CALL_RING_AVAILABLE;
		pring->stats.iocb_cmd_empty++;

		/* Force update of the local copy of cmdGetInx */
		pring->local_getidx = le32_to_cpu(pgp->cmdGetInx);
		lpfc_sli_resume_iocb(phba, pring);

		if ((pring->lpfc_sli_cmd_available))
			(pring->lpfc_sli_cmd_available) (phba, pring);

	}

	return;
}

/*
 * This routine presumes LPFC_FCP_RING handling and doesn't bother
 * to check it explicitly.
 */
static int
lpfc_sli_handle_fast_ring_event(struct lpfc_hba * phba,
				struct lpfc_sli_ring * pring, uint32_t mask)
{
 	struct lpfc_pgp *pgp = &phba->slim2p->mbx.us.s2.port[pring->ringno];
	IOCB_t *irsp = NULL;
	IOCB_t *entry = NULL;
	struct lpfc_iocbq *cmdiocbq = NULL;
	struct lpfc_iocbq rspiocbq;
	uint32_t status;
	uint32_t portRspPut, portRspMax;
	int rc = 1;
	lpfc_iocb_type type;
	unsigned long iflag;
	uint32_t rsp_cmpl = 0;
	void __iomem  *to_slim;

	spin_lock_irqsave(phba->host->host_lock, iflag);
	pring->stats.iocb_event++;

	/*
	 * The next available response entry should never exceed the maximum
	 * entries.  If it does, treat it as an adapter hardware error.
	 */
	portRspMax = pring->numRiocb;
	portRspPut = le32_to_cpu(pgp->rspPutInx);
	if (unlikely(portRspPut >= portRspMax)) {
		lpfc_sli_rsp_pointers_error(phba, pring);
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		return 1;
	}

	rmb();
	while (pring->rspidx != portRspPut) {
		/*
		 * Fetch an entry off the ring and copy it into a local data
		 * structure.  The copy involves a byte-swap since the
		 * network byte order and pci byte orders are different.
		 */
		entry = IOCB_ENTRY(pring->rspringaddr, pring->rspidx);

		if (++pring->rspidx >= portRspMax)
			pring->rspidx = 0;

		lpfc_sli_pcimem_bcopy((uint32_t *) entry,
				      (uint32_t *) &rspiocbq.iocb,
				      sizeof (IOCB_t));
		INIT_LIST_HEAD(&(rspiocbq.list));
		irsp = &rspiocbq.iocb;

		type = lpfc_sli_iocb_cmd_type(irsp->ulpCommand & CMD_IOCB_MASK);
		pring->stats.iocb_rsp++;
		rsp_cmpl++;

		if (unlikely(irsp->ulpStatus)) {
			/* Rsp ring <ringno> error: IOCB */
			lpfc_printf_log(phba, KERN_WARNING, LOG_SLI,
				"%d:0336 Rsp Ring %d error: IOCB Data: "
				"x%x x%x x%x x%x x%x x%x x%x x%x\n",
				phba->brd_no, pring->ringno,
				irsp->un.ulpWord[0], irsp->un.ulpWord[1],
				irsp->un.ulpWord[2], irsp->un.ulpWord[3],
				irsp->un.ulpWord[4], irsp->un.ulpWord[5],
				*(((uint32_t *) irsp) + 6),
				*(((uint32_t *) irsp) + 7));
		}

		switch (type) {
		case LPFC_ABORT_IOCB:
		case LPFC_SOL_IOCB:
			/*
			 * Idle exchange closed via ABTS from port.  No iocb
			 * resources need to be recovered.
			 */
			if (unlikely(irsp->ulpCommand == CMD_XRI_ABORTED_CX)) {
				lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
						"%d:0333 IOCB cmd 0x%x"
						" processed. Skipping"
						" completion\n", phba->brd_no,
						irsp->ulpCommand);
				break;
			}

			cmdiocbq = lpfc_sli_iocbq_lookup(phba, pring,
							 &rspiocbq);
			if ((cmdiocbq) && (cmdiocbq->iocb_cmpl)) {
				if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
					(cmdiocbq->iocb_cmpl)(phba, cmdiocbq,
							      &rspiocbq);
				} else {
					spin_unlock_irqrestore(
						phba->host->host_lock, iflag);
					(cmdiocbq->iocb_cmpl)(phba, cmdiocbq,
							      &rspiocbq);
					spin_lock_irqsave(phba->host->host_lock,
							  iflag);
				}
			}
			break;
		case LPFC_UNSOL_IOCB:
			spin_unlock_irqrestore(phba->host->host_lock, iflag);
			lpfc_sli_process_unsol_iocb(phba, pring, &rspiocbq);
			spin_lock_irqsave(phba->host->host_lock, iflag);
			break;
		default:
			if (irsp->ulpCommand == CMD_ADAPTER_MSG) {
				char adaptermsg[LPFC_MAX_ADPTMSG];
				memset(adaptermsg, 0, LPFC_MAX_ADPTMSG);
				memcpy(&adaptermsg[0], (uint8_t *) irsp,
				       MAX_MSG_DATA);
				dev_warn(&((phba->pcidev)->dev), "lpfc%d: %s",
					 phba->brd_no, adaptermsg);
			} else {
				/* Unknown IOCB command */
				lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"%d:0334 Unknown IOCB command "
					"Data: x%x, x%x x%x x%x x%x\n",
					phba->brd_no, type, irsp->ulpCommand,
					irsp->ulpStatus, irsp->ulpIoTag,
					irsp->ulpContext);
			}
			break;
		}

		/*
		 * The response IOCB has been processed.  Update the ring
		 * pointer in SLIM.  If the port response put pointer has not
		 * been updated, sync the pgp->rspPutInx and fetch the new port
		 * response put pointer.
		 */
		to_slim = phba->MBslimaddr +
			(SLIMOFF + (pring->ringno * 2) + 1) * 4;
		writel(pring->rspidx, to_slim);

		if (pring->rspidx == portRspPut)
			portRspPut = le32_to_cpu(pgp->rspPutInx);
	}

	if ((rsp_cmpl > 0) && (mask & HA_R0RE_REQ)) {
		pring->stats.iocb_rsp_full++;
		status = ((CA_R0ATT | CA_R0RE_RSP) << (pring->ringno * 4));
		writel(status, phba->CAregaddr);
		readl(phba->CAregaddr);
	}
	if ((mask & HA_R0CE_RSP) && (pring->flag & LPFC_CALL_RING_AVAILABLE)) {
		pring->flag &= ~LPFC_CALL_RING_AVAILABLE;
		pring->stats.iocb_cmd_empty++;

		/* Force update of the local copy of cmdGetInx */
		pring->local_getidx = le32_to_cpu(pgp->cmdGetInx);
		lpfc_sli_resume_iocb(phba, pring);

		if ((pring->lpfc_sli_cmd_available))
			(pring->lpfc_sli_cmd_available) (phba, pring);

	}

	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return rc;
}


int
lpfc_sli_handle_slow_ring_event(struct lpfc_hba * phba,
			   struct lpfc_sli_ring * pring, uint32_t mask)
{
	IOCB_t *entry;
	IOCB_t *irsp = NULL;
	struct lpfc_iocbq *rspiocbp = NULL;
	struct lpfc_iocbq *next_iocb;
	struct lpfc_iocbq *cmdiocbp;
	struct lpfc_iocbq *saveq;
	struct lpfc_pgp *pgp = &phba->slim2p->mbx.us.s2.port[pring->ringno];
	uint8_t iocb_cmd_type;
	lpfc_iocb_type type;
	uint32_t status, free_saveq;
	uint32_t portRspPut, portRspMax;
	int rc = 1;
	unsigned long iflag;
	void __iomem  *to_slim;

	spin_lock_irqsave(phba->host->host_lock, iflag);
	pring->stats.iocb_event++;

	/*
	 * The next available response entry should never exceed the maximum
	 * entries.  If it does, treat it as an adapter hardware error.
	 */
	portRspMax = pring->numRiocb;
	portRspPut = le32_to_cpu(pgp->rspPutInx);
	if (portRspPut >= portRspMax) {
		/*
		 * Ring <ringno> handler: portRspPut <portRspPut> is bigger then
		 * rsp ring <portRspMax>
		 */
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_SLI,
				"%d:0303 Ring %d handler: portRspPut %d "
				"is bigger then rsp ring %d\n",
				phba->brd_no,
				pring->ringno, portRspPut, portRspMax);

		phba->hba_state = LPFC_HBA_ERROR;
		spin_unlock_irqrestore(phba->host->host_lock, iflag);

		phba->work_hs = HS_FFER3;
		lpfc_handle_eratt(phba);

		return 1;
	}

	rmb();
	while (pring->rspidx != portRspPut) {
		/*
		 * Build a completion list and call the appropriate handler.
		 * The process is to get the next available response iocb, get
		 * a free iocb from the list, copy the response data into the
		 * free iocb, insert to the continuation list, and update the
		 * next response index to slim.  This process makes response
		 * iocb's in the ring available to DMA as fast as possible but
		 * pays a penalty for a copy operation.  Since the iocb is
		 * only 32 bytes, this penalty is considered small relative to
		 * the PCI reads for register values and a slim write.  When
		 * the ulpLe field is set, the entire Command has been
		 * received.
		 */
		entry = IOCB_ENTRY(pring->rspringaddr, pring->rspidx);
		rspiocbp = lpfc_sli_get_iocbq(phba);
		if (rspiocbp == NULL) {
			printk(KERN_ERR "%s: out of buffers! Failing "
			       "completion.\n", __FUNCTION__);
			break;
		}

		lpfc_sli_pcimem_bcopy(entry, &rspiocbp->iocb, sizeof (IOCB_t));
		irsp = &rspiocbp->iocb;

		if (++pring->rspidx >= portRspMax)
			pring->rspidx = 0;

		to_slim = phba->MBslimaddr + (SLIMOFF + (pring->ringno * 2)
					      + 1) * 4;
		writel(pring->rspidx, to_slim);

		if (list_empty(&(pring->iocb_continueq))) {
			list_add(&rspiocbp->list, &(pring->iocb_continueq));
		} else {
			list_add_tail(&rspiocbp->list,
				      &(pring->iocb_continueq));
		}

		pring->iocb_continueq_cnt++;
		if (irsp->ulpLe) {
			/*
			 * By default, the driver expects to free all resources
			 * associated with this iocb completion.
			 */
			free_saveq = 1;
			saveq = list_get_first(&pring->iocb_continueq,
					       struct lpfc_iocbq, list);
			irsp = &(saveq->iocb);
			list_del_init(&pring->iocb_continueq);
			pring->iocb_continueq_cnt = 0;

			pring->stats.iocb_rsp++;

			if (irsp->ulpStatus) {
				/* Rsp ring <ringno> error: IOCB */
				lpfc_printf_log(phba,
					KERN_WARNING,
					LOG_SLI,
					"%d:0328 Rsp Ring %d error: IOCB Data: "
					"x%x x%x x%x x%x x%x x%x x%x x%x\n",
					phba->brd_no,
					pring->ringno,
					irsp->un.ulpWord[0],
					irsp->un.ulpWord[1],
					irsp->un.ulpWord[2],
					irsp->un.ulpWord[3],
					irsp->un.ulpWord[4],
					irsp->un.ulpWord[5],
					*(((uint32_t *) irsp) + 6),
					*(((uint32_t *) irsp) + 7));
			}

			/*
			 * Fetch the IOCB command type and call the correct
			 * completion routine.  Solicited and Unsolicited
			 * IOCBs on the ELS ring get freed back to the
			 * lpfc_iocb_list by the discovery kernel thread.
			 */
			iocb_cmd_type = irsp->ulpCommand & CMD_IOCB_MASK;
			type = lpfc_sli_iocb_cmd_type(iocb_cmd_type);
			if (type == LPFC_SOL_IOCB) {
				spin_unlock_irqrestore(phba->host->host_lock,
						       iflag);
				rc = lpfc_sli_process_sol_iocb(phba, pring,
					saveq);
				spin_lock_irqsave(phba->host->host_lock, iflag);
			} else if (type == LPFC_UNSOL_IOCB) {
				spin_unlock_irqrestore(phba->host->host_lock,
						       iflag);
				rc = lpfc_sli_process_unsol_iocb(phba, pring,
					saveq);
				spin_lock_irqsave(phba->host->host_lock, iflag);
			} else if (type == LPFC_ABORT_IOCB) {
				if ((irsp->ulpCommand != CMD_XRI_ABORTED_CX) &&
				    ((cmdiocbp =
				      lpfc_sli_iocbq_lookup(phba, pring,
							    saveq)))) {
					/* Call the specified completion
					   routine */
					if (cmdiocbp->iocb_cmpl) {
						spin_unlock_irqrestore(
						       phba->host->host_lock,
						       iflag);
						(cmdiocbp->iocb_cmpl) (phba,
							     cmdiocbp, saveq);
						spin_lock_irqsave(
							  phba->host->host_lock,
							  iflag);
					} else
						lpfc_sli_release_iocbq(phba,
								      cmdiocbp);
				}
			} else if (type == LPFC_UNKNOWN_IOCB) {
				if (irsp->ulpCommand == CMD_ADAPTER_MSG) {

					char adaptermsg[LPFC_MAX_ADPTMSG];

					memset(adaptermsg, 0,
					       LPFC_MAX_ADPTMSG);
					memcpy(&adaptermsg[0], (uint8_t *) irsp,
					       MAX_MSG_DATA);
					dev_warn(&((phba->pcidev)->dev),
						 "lpfc%d: %s",
						 phba->brd_no, adaptermsg);
				} else {
					/* Unknown IOCB command */
					lpfc_printf_log(phba,
						KERN_ERR,
						LOG_SLI,
						"%d:0335 Unknown IOCB command "
						"Data: x%x x%x x%x x%x\n",
						phba->brd_no,
						irsp->ulpCommand,
						irsp->ulpStatus,
						irsp->ulpIoTag,
						irsp->ulpContext);
				}
			}

			if (free_saveq) {
				if (!list_empty(&saveq->list)) {
					list_for_each_entry_safe(rspiocbp,
								 next_iocb,
								 &saveq->list,
								 list) {
						list_del(&rspiocbp->list);
						lpfc_sli_release_iocbq(phba,
								     rspiocbp);
					}
				}
				lpfc_sli_release_iocbq(phba, saveq);
			}
		}

		/*
		 * If the port response put pointer has not been updated, sync
		 * the pgp->rspPutInx in the MAILBOX_tand fetch the new port
		 * response put pointer.
		 */
		if (pring->rspidx == portRspPut) {
			portRspPut = le32_to_cpu(pgp->rspPutInx);
		}
	} /* while (pring->rspidx != portRspPut) */

	if ((rspiocbp != 0) && (mask & HA_R0RE_REQ)) {
		/* At least one response entry has been freed */
		pring->stats.iocb_rsp_full++;
		/* SET RxRE_RSP in Chip Att register */
		status = ((CA_R0ATT | CA_R0RE_RSP) << (pring->ringno * 4));
		writel(status, phba->CAregaddr);
		readl(phba->CAregaddr); /* flush */
	}
	if ((mask & HA_R0CE_RSP) && (pring->flag & LPFC_CALL_RING_AVAILABLE)) {
		pring->flag &= ~LPFC_CALL_RING_AVAILABLE;
		pring->stats.iocb_cmd_empty++;

		/* Force update of the local copy of cmdGetInx */
		pring->local_getidx = le32_to_cpu(pgp->cmdGetInx);
		lpfc_sli_resume_iocb(phba, pring);

		if ((pring->lpfc_sli_cmd_available))
			(pring->lpfc_sli_cmd_available) (phba, pring);

	}

	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return rc;
}

int
lpfc_sli_abort_iocb_ring(struct lpfc_hba *phba, struct lpfc_sli_ring *pring)
{
	struct lpfc_iocbq *iocb, *next_iocb;
	IOCB_t *icmd = NULL, *cmd = NULL;
	int errcnt;

	errcnt = 0;

	/* Error everything on txq and txcmplq
	 * First do the txq.
	 */
	spin_lock_irq(phba->host->host_lock);
	list_for_each_entry_safe(iocb, next_iocb, &pring->txq, list) {
		list_del_init(&iocb->list);
		if (iocb->iocb_cmpl) {
			icmd = &iocb->iocb;
			icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			spin_unlock_irq(phba->host->host_lock);
			(iocb->iocb_cmpl) (phba, iocb, iocb);
			spin_lock_irq(phba->host->host_lock);
		} else
			lpfc_sli_release_iocbq(phba, iocb);
	}
	pring->txq_cnt = 0;
	INIT_LIST_HEAD(&(pring->txq));

	/* Next issue ABTS for everything on the txcmplq */
	list_for_each_entry_safe(iocb, next_iocb, &pring->txcmplq, list) {
		cmd = &iocb->iocb;

		/*
		 * Imediate abort of IOCB, deque and call compl
		 */

		list_del_init(&iocb->list);
		pring->txcmplq_cnt--;

		if (iocb->iocb_cmpl) {
			cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			spin_unlock_irq(phba->host->host_lock);
			(iocb->iocb_cmpl) (phba, iocb, iocb);
			spin_lock_irq(phba->host->host_lock);
		} else
			lpfc_sli_release_iocbq(phba, iocb);
	}

	INIT_LIST_HEAD(&pring->txcmplq);
	pring->txcmplq_cnt = 0;
	spin_unlock_irq(phba->host->host_lock);

	return errcnt;
}

int
lpfc_sli_brdready(struct lpfc_hba * phba, uint32_t mask)
{
	uint32_t status;
	int i = 0;
	int retval = 0;

	/* Read the HBA Host Status Register */
	status = readl(phba->HSregaddr);

	/*
	 * Check status register every 100ms for 5 retries, then every
	 * 500ms for 5, then every 2.5 sec for 5, then reset board and
	 * every 2.5 sec for 4.
	 * Break our of the loop if errors occurred during init.
	 */
	while (((status & mask) != mask) &&
	       !(status & HS_FFERM) &&
	       i++ < 20) {

		if (i <= 5)
			msleep(10);
		else if (i <= 10)
			msleep(500);
		else
			msleep(2500);

		if (i == 15) {
			phba->hba_state = LPFC_STATE_UNKNOWN; /* Do post */
			lpfc_sli_brdrestart(phba);
		}
		/* Read the HBA Host Status Register */
		status = readl(phba->HSregaddr);
	}

	/* Check to see if any errors occurred during init */
	if ((status & HS_FFERM) || (i >= 20)) {
		phba->hba_state = LPFC_HBA_ERROR;
		retval = 1;
	}

	return retval;
}

#define BARRIER_TEST_PATTERN (0xdeadbeef)

void lpfc_reset_barrier(struct lpfc_hba * phba)
{
	uint32_t __iomem *resp_buf;
	uint32_t __iomem *mbox_buf;
	volatile uint32_t mbox;
	uint32_t hc_copy;
	int  i;
	uint8_t hdrtype;

	pci_read_config_byte(phba->pcidev, PCI_HEADER_TYPE, &hdrtype);
	if (hdrtype != 0x80 ||
	    (FC_JEDEC_ID(phba->vpd.rev.biuRev) != HELIOS_JEDEC_ID &&
	     FC_JEDEC_ID(phba->vpd.rev.biuRev) != THOR_JEDEC_ID))
		return;

	/*
	 * Tell the other part of the chip to suspend temporarily all
	 * its DMA activity.
	 */
	resp_buf = phba->MBslimaddr;

	/* Disable the error attention */
	hc_copy = readl(phba->HCregaddr);
	writel((hc_copy & ~HC_ERINT_ENA), phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	if (readl(phba->HAregaddr) & HA_ERATT) {
		/* Clear Chip error bit */
		writel(HA_ERATT, phba->HAregaddr);
		phba->stopped = 1;
	}

	mbox = 0;
	((MAILBOX_t *)&mbox)->mbxCommand = MBX_KILL_BOARD;
	((MAILBOX_t *)&mbox)->mbxOwner = OWN_CHIP;

	writel(BARRIER_TEST_PATTERN, (resp_buf + 1));
	mbox_buf = phba->MBslimaddr;
	writel(mbox, mbox_buf);

	for (i = 0;
	     readl(resp_buf + 1) != ~(BARRIER_TEST_PATTERN) && i < 50; i++)
		mdelay(1);

	if (readl(resp_buf + 1) != ~(BARRIER_TEST_PATTERN)) {
		if (phba->sli.sli_flag & LPFC_SLI2_ACTIVE ||
		    phba->stopped)
			goto restore_hc;
		else
			goto clear_errat;
	}

	((MAILBOX_t *)&mbox)->mbxOwner = OWN_HOST;
	for (i = 0; readl(resp_buf) != mbox &&  i < 500; i++)
		mdelay(1);

clear_errat:

	while (!(readl(phba->HAregaddr) & HA_ERATT) && ++i < 500)
		mdelay(1);

	if (readl(phba->HAregaddr) & HA_ERATT) {
		writel(HA_ERATT, phba->HAregaddr);
		phba->stopped = 1;
	}

restore_hc:
	writel(hc_copy, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */
}

int
lpfc_sli_brdkill(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli;
	LPFC_MBOXQ_t *pmb;
	uint32_t status;
	uint32_t ha_copy;
	int retval;
	int i = 0;

	psli = &phba->sli;

	/* Kill HBA */
	lpfc_printf_log(phba,
		KERN_INFO,
		LOG_SLI,
		"%d:0329 Kill HBA Data: x%x x%x\n",
		phba->brd_no,
		phba->hba_state,
		psli->sli_flag);

	if ((pmb = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool,
						  GFP_KERNEL)) == 0)
		return 1;

	/* Disable the error attention */
	spin_lock_irq(phba->host->host_lock);
	status = readl(phba->HCregaddr);
	status &= ~HC_ERINT_ENA;
	writel(status, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */
	spin_unlock_irq(phba->host->host_lock);

	lpfc_kill_board(phba, pmb);
	pmb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
	retval = lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT);

	if (retval != MBX_SUCCESS) {
		if (retval != MBX_BUSY)
			mempool_free(pmb, phba->mbox_mem_pool);
		return 1;
	}

	psli->sli_flag &= ~LPFC_SLI2_ACTIVE;

	mempool_free(pmb, phba->mbox_mem_pool);

	/* There is no completion for a KILL_BOARD mbox cmd. Check for an error
	 * attention every 100ms for 3 seconds. If we don't get ERATT after
	 * 3 seconds we still set HBA_ERROR state because the status of the
	 * board is now undefined.
	 */
	ha_copy = readl(phba->HAregaddr);

	while ((i++ < 30) && !(ha_copy & HA_ERATT)) {
		mdelay(100);
		ha_copy = readl(phba->HAregaddr);
	}

	del_timer_sync(&psli->mbox_tmo);
	if (ha_copy & HA_ERATT) {
		writel(HA_ERATT, phba->HAregaddr);
		phba->stopped = 1;
	}
	spin_lock_irq(phba->host->host_lock);
	psli->sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
	spin_unlock_irq(phba->host->host_lock);

	psli->mbox_active = NULL;
	lpfc_hba_down_post(phba);
	phba->hba_state = LPFC_HBA_ERROR;

	return (ha_copy & HA_ERATT ? 0 : 1);
}

int
lpfc_sli_brdreset(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	uint16_t cfg_value;
	int i;

	psli = &phba->sli;

	/* Reset HBA */
	lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
			"%d:0325 Reset HBA Data: x%x x%x\n", phba->brd_no,
			phba->hba_state, psli->sli_flag);

	/* perform board reset */
	phba->fc_eventTag = 0;
	phba->fc_myDID = 0;
	phba->fc_prevDID = 0;

	/* Turn off parity checking and serr during the physical reset */
	pci_read_config_word(phba->pcidev, PCI_COMMAND, &cfg_value);
	pci_write_config_word(phba->pcidev, PCI_COMMAND,
			      (cfg_value &
			       ~(PCI_COMMAND_PARITY | PCI_COMMAND_SERR)));

	psli->sli_flag &= ~(LPFC_SLI2_ACTIVE | LPFC_PROCESS_LA);
	/* Now toggle INITFF bit in the Host Control Register */
	writel(HC_INITFF, phba->HCregaddr);
	mdelay(1);
	readl(phba->HCregaddr); /* flush */
	writel(0, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	/* Restore PCI cmd register */
	pci_write_config_word(phba->pcidev, PCI_COMMAND, cfg_value);

	/* Initialize relevant SLI info */
	for (i = 0; i < psli->num_rings; i++) {
		pring = &psli->ring[i];
		pring->flag = 0;
		pring->rspidx = 0;
		pring->next_cmdidx  = 0;
		pring->local_getidx = 0;
		pring->cmdidx = 0;
		pring->missbufcnt = 0;
	}

	phba->hba_state = LPFC_WARM_START;
	return 0;
}

int
lpfc_sli_brdrestart(struct lpfc_hba * phba)
{
	MAILBOX_t *mb;
	struct lpfc_sli *psli;
	uint16_t skip_post;
	volatile uint32_t word0;
	void __iomem *to_slim;

	spin_lock_irq(phba->host->host_lock);

	psli = &phba->sli;

	/* Restart HBA */
	lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
			"%d:0337 Restart HBA Data: x%x x%x\n", phba->brd_no,
			phba->hba_state, psli->sli_flag);

	word0 = 0;
	mb = (MAILBOX_t *) &word0;
	mb->mbxCommand = MBX_RESTART;
	mb->mbxHc = 1;

	lpfc_reset_barrier(phba);

	to_slim = phba->MBslimaddr;
	writel(*(uint32_t *) mb, to_slim);
	readl(to_slim); /* flush */

	/* Only skip post after fc_ffinit is completed */
	if (phba->hba_state) {
		skip_post = 1;
		word0 = 1;	/* This is really setting up word1 */
	} else {
		skip_post = 0;
		word0 = 0;	/* This is really setting up word1 */
	}
	to_slim = phba->MBslimaddr + sizeof (uint32_t);
	writel(*(uint32_t *) mb, to_slim);
	readl(to_slim); /* flush */

	lpfc_sli_brdreset(phba);
	phba->stopped = 0;
	phba->hba_state = LPFC_INIT_START;

	spin_unlock_irq(phba->host->host_lock);

	memset(&psli->lnk_stat_offsets, 0, sizeof(psli->lnk_stat_offsets));
	psli->stats_start = get_seconds();

	if (skip_post)
		mdelay(100);
	else
		mdelay(2000);

	lpfc_hba_down_post(phba);

	return 0;
}

static int
lpfc_sli_chipset_init(struct lpfc_hba *phba)
{
	uint32_t status, i = 0;

	/* Read the HBA Host Status Register */
	status = readl(phba->HSregaddr);

	/* Check status register to see what current state is */
	i = 0;
	while ((status & (HS_FFRDY | HS_MBRDY)) != (HS_FFRDY | HS_MBRDY)) {

		/* Check every 100ms for 5 retries, then every 500ms for 5, then
		 * every 2.5 sec for 5, then reset board and every 2.5 sec for
		 * 4.
		 */
		if (i++ >= 20) {
			/* Adapter failed to init, timeout, status reg
			   <status> */
			lpfc_printf_log(phba,
					KERN_ERR,
					LOG_INIT,
					"%d:0436 Adapter failed to init, "
					"timeout, status reg x%x\n",
					phba->brd_no,
					status);
			phba->hba_state = LPFC_HBA_ERROR;
			return -ETIMEDOUT;
		}

		/* Check to see if any errors occurred during init */
		if (status & HS_FFERM) {
			/* ERROR: During chipset initialization */
			/* Adapter failed to init, chipset, status reg
			   <status> */
			lpfc_printf_log(phba,
					KERN_ERR,
					LOG_INIT,
					"%d:0437 Adapter failed to init, "
					"chipset, status reg x%x\n",
					phba->brd_no,
					status);
			phba->hba_state = LPFC_HBA_ERROR;
			return -EIO;
		}

		if (i <= 5) {
			msleep(10);
		} else if (i <= 10) {
			msleep(500);
		} else {
			msleep(2500);
		}

		if (i == 15) {
			phba->hba_state = LPFC_STATE_UNKNOWN; /* Do post */
			lpfc_sli_brdrestart(phba);
		}
		/* Read the HBA Host Status Register */
		status = readl(phba->HSregaddr);
	}

	/* Check to see if any errors occurred during init */
	if (status & HS_FFERM) {
		/* ERROR: During chipset initialization */
		/* Adapter failed to init, chipset, status reg <status> */
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"%d:0438 Adapter failed to init, chipset, "
				"status reg x%x\n",
				phba->brd_no,
				status);
		phba->hba_state = LPFC_HBA_ERROR;
		return -EIO;
	}

	/* Clear all interrupt enable conditions */
	writel(0, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	/* setup host attn register */
	writel(0xffffffff, phba->HAregaddr);
	readl(phba->HAregaddr); /* flush */
	return 0;
}

int
lpfc_sli_hba_setup(struct lpfc_hba * phba)
{
	LPFC_MBOXQ_t *pmb;
	uint32_t resetcount = 0, rc = 0, done = 0;

	pmb = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		phba->hba_state = LPFC_HBA_ERROR;
		return -ENOMEM;
	}

	while (resetcount < 2 && !done) {
		spin_lock_irq(phba->host->host_lock);
		phba->sli.sli_flag |= LPFC_SLI_MBOX_ACTIVE;
		spin_unlock_irq(phba->host->host_lock);
		phba->hba_state = LPFC_STATE_UNKNOWN;
		lpfc_sli_brdrestart(phba);
		msleep(2500);
		rc = lpfc_sli_chipset_init(phba);
		if (rc)
			break;

		spin_lock_irq(phba->host->host_lock);
		phba->sli.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
		spin_unlock_irq(phba->host->host_lock);
		resetcount++;

	/* Call pre CONFIG_PORT mailbox command initialization.  A value of 0
	 * means the call was successful.  Any other nonzero value is a failure,
	 * but if ERESTART is returned, the driver may reset the HBA and try
	 * again.
	 */
		rc = lpfc_config_port_prep(phba);
		if (rc == -ERESTART) {
			phba->hba_state = 0;
			continue;
		} else if (rc) {
			break;
		}

		phba->hba_state = LPFC_INIT_MBX_CMDS;
		lpfc_config_port(phba, pmb);
		rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);
		if (rc == MBX_SUCCESS)
			done = 1;
		else {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"%d:0442 Adapter failed to init, mbxCmd x%x "
				"CONFIG_PORT, mbxStatus x%x Data: x%x\n",
				phba->brd_no, pmb->mb.mbxCommand,
				pmb->mb.mbxStatus, 0);
			phba->sli.sli_flag &= ~LPFC_SLI2_ACTIVE;
		}
	}
	if (!done)
		goto lpfc_sli_hba_setup_error;

	rc = lpfc_sli_ring_map(phba, pmb);

	if (rc)
		goto lpfc_sli_hba_setup_error;

	phba->sli.sli_flag |= LPFC_PROCESS_LA;

	rc = lpfc_config_port_post(phba);
	if (rc)
		goto lpfc_sli_hba_setup_error;

	goto lpfc_sli_hba_setup_exit;
lpfc_sli_hba_setup_error:
	phba->hba_state = LPFC_HBA_ERROR;
lpfc_sli_hba_setup_exit:
	mempool_free(pmb, phba->mbox_mem_pool);
	return rc;
}

static void
lpfc_mbox_abort(struct lpfc_hba * phba)
{
	LPFC_MBOXQ_t *pmbox;
	MAILBOX_t *mb;

	if (phba->sli.mbox_active) {
		del_timer_sync(&phba->sli.mbox_tmo);
		phba->work_hba_events &= ~WORKER_MBOX_TMO;
		pmbox = phba->sli.mbox_active;
		mb = &pmbox->mb;
		phba->sli.mbox_active = NULL;
		if (pmbox->mbox_cmpl) {
			mb->mbxStatus = MBX_NOT_FINISHED;
			(pmbox->mbox_cmpl) (phba, pmbox);
		}
		phba->sli.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
	}

	/* Abort all the non active mailbox commands. */
	spin_lock_irq(phba->host->host_lock);
	pmbox = lpfc_mbox_get(phba);
	while (pmbox) {
		mb = &pmbox->mb;
		if (pmbox->mbox_cmpl) {
			mb->mbxStatus = MBX_NOT_FINISHED;
			spin_unlock_irq(phba->host->host_lock);
			(pmbox->mbox_cmpl) (phba, pmbox);
			spin_lock_irq(phba->host->host_lock);
		}
		pmbox = lpfc_mbox_get(phba);
	}
	spin_unlock_irq(phba->host->host_lock);
	return;
}

/*! lpfc_mbox_timeout
 *
 * \pre
 * \post
 * \param hba Pointer to per struct lpfc_hba structure
 * \param l1  Pointer to the driver's mailbox queue.
 * \return
 *   void
 *
 * \b Description:
 *
 * This routine handles mailbox timeout events at timer interrupt context.
 */
void
lpfc_mbox_timeout(unsigned long ptr)
{
	struct lpfc_hba *phba;
	unsigned long iflag;

	phba = (struct lpfc_hba *)ptr;
	spin_lock_irqsave(phba->host->host_lock, iflag);
	if (!(phba->work_hba_events & WORKER_MBOX_TMO)) {
		phba->work_hba_events |= WORKER_MBOX_TMO;
		if (phba->work_wait)
			wake_up(phba->work_wait);
	}
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
}

void
lpfc_mbox_timeout_handler(struct lpfc_hba *phba)
{
	LPFC_MBOXQ_t *pmbox;
	MAILBOX_t *mb;

	spin_lock_irq(phba->host->host_lock);
	if (!(phba->work_hba_events & WORKER_MBOX_TMO)) {
		spin_unlock_irq(phba->host->host_lock);
		return;
	}

	phba->work_hba_events &= ~WORKER_MBOX_TMO;

	pmbox = phba->sli.mbox_active;
	mb = &pmbox->mb;

	/* Mbox cmd <mbxCommand> timeout */
	lpfc_printf_log(phba,
		KERN_ERR,
		LOG_MBOX | LOG_SLI,
		"%d:0310 Mailbox command x%x timeout Data: x%x x%x x%p\n",
		phba->brd_no,
		mb->mbxCommand,
		phba->hba_state,
		phba->sli.sli_flag,
		phba->sli.mbox_active);

	phba->sli.mbox_active = NULL;
	if (pmbox->mbox_cmpl) {
		mb->mbxStatus = MBX_NOT_FINISHED;
		spin_unlock_irq(phba->host->host_lock);
		(pmbox->mbox_cmpl) (phba, pmbox);
		spin_lock_irq(phba->host->host_lock);
	}
	phba->sli.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;

	spin_unlock_irq(phba->host->host_lock);
	lpfc_mbox_abort(phba);
	return;
}

int
lpfc_sli_issue_mbox(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmbox, uint32_t flag)
{
	MAILBOX_t *mb;
	struct lpfc_sli *psli;
	uint32_t status, evtctr;
	uint32_t ha_copy;
	int i;
	unsigned long drvr_flag = 0;
	volatile uint32_t word0, ldata;
	void __iomem *to_slim;

	psli = &phba->sli;

	spin_lock_irqsave(phba->host->host_lock, drvr_flag);


	mb = &pmbox->mb;
	status = MBX_SUCCESS;

	if (phba->hba_state == LPFC_HBA_ERROR) {
		spin_unlock_irqrestore(phba->host->host_lock, drvr_flag);

		/* Mbox command <mbxCommand> cannot issue */
		LOG_MBOX_CANNOT_ISSUE_DATA( phba, mb, psli, flag)
		return (MBX_NOT_FINISHED);
	}

	if (mb->mbxCommand != MBX_KILL_BOARD && flag & MBX_NOWAIT &&
	    !(readl(phba->HCregaddr) & HC_MBINT_ENA)) {
		spin_unlock_irqrestore(phba->host->host_lock, drvr_flag);
		LOG_MBOX_CANNOT_ISSUE_DATA( phba, mb, psli, flag)
		return (MBX_NOT_FINISHED);
	}

	if (psli->sli_flag & LPFC_SLI_MBOX_ACTIVE) {
		/* Polling for a mbox command when another one is already active
		 * is not allowed in SLI. Also, the driver must have established
		 * SLI2 mode to queue and process multiple mbox commands.
		 */

		if (flag & MBX_POLL) {
			spin_unlock_irqrestore(phba->host->host_lock,
					       drvr_flag);

			/* Mbox command <mbxCommand> cannot issue */
			LOG_MBOX_CANNOT_ISSUE_DATA( phba, mb, psli, flag)
			return (MBX_NOT_FINISHED);
		}

		if (!(psli->sli_flag & LPFC_SLI2_ACTIVE)) {
			spin_unlock_irqrestore(phba->host->host_lock,
					       drvr_flag);
			/* Mbox command <mbxCommand> cannot issue */
			LOG_MBOX_CANNOT_ISSUE_DATA( phba, mb, psli, flag)
			return (MBX_NOT_FINISHED);
		}

		/* Handle STOP IOCB processing flag. This is only meaningful
		 * if we are not polling for mbox completion.
		 */
		if (flag & MBX_STOP_IOCB) {
			flag &= ~MBX_STOP_IOCB;
			/* Now flag each ring */
			for (i = 0; i < psli->num_rings; i++) {
				/* If the ring is active, flag it */
				if (psli->ring[i].cmdringaddr) {
					psli->ring[i].flag |=
					    LPFC_STOP_IOCB_MBX;
				}
			}
		}

		/* Another mailbox command is still being processed, queue this
		 * command to be processed later.
		 */
		lpfc_mbox_put(phba, pmbox);

		/* Mbox cmd issue - BUSY */
		lpfc_printf_log(phba,
			KERN_INFO,
			LOG_MBOX | LOG_SLI,
			"%d:0308 Mbox cmd issue - BUSY Data: x%x x%x x%x x%x\n",
			phba->brd_no,
			mb->mbxCommand,
			phba->hba_state,
			psli->sli_flag,
			flag);

		psli->slistat.mbox_busy++;
		spin_unlock_irqrestore(phba->host->host_lock,
				       drvr_flag);

		return (MBX_BUSY);
	}

	/* Handle STOP IOCB processing flag. This is only meaningful
	 * if we are not polling for mbox completion.
	 */
	if (flag & MBX_STOP_IOCB) {
		flag &= ~MBX_STOP_IOCB;
		if (flag == MBX_NOWAIT) {
			/* Now flag each ring */
			for (i = 0; i < psli->num_rings; i++) {
				/* If the ring is active, flag it */
				if (psli->ring[i].cmdringaddr) {
					psli->ring[i].flag |=
					    LPFC_STOP_IOCB_MBX;
				}
			}
		}
	}

	psli->sli_flag |= LPFC_SLI_MBOX_ACTIVE;

	/* If we are not polling, we MUST be in SLI2 mode */
	if (flag != MBX_POLL) {
		if (!(psli->sli_flag & LPFC_SLI2_ACTIVE) &&
		    (mb->mbxCommand != MBX_KILL_BOARD)) {
			psli->sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
			spin_unlock_irqrestore(phba->host->host_lock,
					       drvr_flag);
			/* Mbox command <mbxCommand> cannot issue */
			LOG_MBOX_CANNOT_ISSUE_DATA( phba, mb, psli, flag);
			return (MBX_NOT_FINISHED);
		}
		/* timeout active mbox command */
		mod_timer(&psli->mbox_tmo, (jiffies +
			       (HZ * lpfc_mbox_tmo_val(phba, mb->mbxCommand))));
	}

	/* Mailbox cmd <cmd> issue */
	lpfc_printf_log(phba,
		KERN_INFO,
		LOG_MBOX | LOG_SLI,
		"%d:0309 Mailbox cmd x%x issue Data: x%x x%x x%x\n",
		phba->brd_no,
		mb->mbxCommand,
		phba->hba_state,
		psli->sli_flag,
		flag);

	psli->slistat.mbox_cmd++;
	evtctr = psli->slistat.mbox_event;

	/* next set own bit for the adapter and copy over command word */
	mb->mbxOwner = OWN_CHIP;

	if (psli->sli_flag & LPFC_SLI2_ACTIVE) {
		/* First copy command data to host SLIM area */
		lpfc_sli_pcimem_bcopy(mb, &phba->slim2p->mbx, MAILBOX_CMD_SIZE);
	} else {
		if (mb->mbxCommand == MBX_CONFIG_PORT) {
			/* copy command data into host mbox for cmpl */
			lpfc_sli_pcimem_bcopy(mb, &phba->slim2p->mbx,
					MAILBOX_CMD_SIZE);
		}

		/* First copy mbox command data to HBA SLIM, skip past first
		   word */
		to_slim = phba->MBslimaddr + sizeof (uint32_t);
		lpfc_memcpy_to_slim(to_slim, &mb->un.varWords[0],
			    MAILBOX_CMD_SIZE - sizeof (uint32_t));

		/* Next copy over first word, with mbxOwner set */
		ldata = *((volatile uint32_t *)mb);
		to_slim = phba->MBslimaddr;
		writel(ldata, to_slim);
		readl(to_slim); /* flush */

		if (mb->mbxCommand == MBX_CONFIG_PORT) {
			/* switch over to host mailbox */
			psli->sli_flag |= LPFC_SLI2_ACTIVE;
		}
	}

	wmb();
	/* interrupt board to doit right away */
	writel(CA_MBATT, phba->CAregaddr);
	readl(phba->CAregaddr); /* flush */

	switch (flag) {
	case MBX_NOWAIT:
		/* Don't wait for it to finish, just return */
		psli->mbox_active = pmbox;
		break;

	case MBX_POLL:
		psli->mbox_active = NULL;
		if (psli->sli_flag & LPFC_SLI2_ACTIVE) {
			/* First read mbox status word */
			word0 = *((volatile uint32_t *)&phba->slim2p->mbx);
			word0 = le32_to_cpu(word0);
		} else {
			/* First read mbox status word */
			word0 = readl(phba->MBslimaddr);
		}

		/* Read the HBA Host Attention Register */
		ha_copy = readl(phba->HAregaddr);

		i = lpfc_mbox_tmo_val(phba, mb->mbxCommand);
		i *= 1000; /* Convert to ms */

		/* Wait for command to complete */
		while (((word0 & OWN_CHIP) == OWN_CHIP) ||
		       (!(ha_copy & HA_MBATT) &&
			(phba->hba_state > LPFC_WARM_START))) {
			if (i-- <= 0) {
				psli->sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
				spin_unlock_irqrestore(phba->host->host_lock,
						       drvr_flag);
				return (MBX_NOT_FINISHED);
			}

			/* Check if we took a mbox interrupt while we were
			   polling */
			if (((word0 & OWN_CHIP) != OWN_CHIP)
			    && (evtctr != psli->slistat.mbox_event))
				break;

			spin_unlock_irqrestore(phba->host->host_lock,
					       drvr_flag);

			/* Can be in interrupt context, do not sleep */
			/* (or might be called with interrupts disabled) */
			mdelay(1);

			spin_lock_irqsave(phba->host->host_lock, drvr_flag);

			if (psli->sli_flag & LPFC_SLI2_ACTIVE) {
				/* First copy command data */
				word0 = *((volatile uint32_t *)
						&phba->slim2p->mbx);
				word0 = le32_to_cpu(word0);
				if (mb->mbxCommand == MBX_CONFIG_PORT) {
					MAILBOX_t *slimmb;
					volatile uint32_t slimword0;
					/* Check real SLIM for any errors */
					slimword0 = readl(phba->MBslimaddr);
					slimmb = (MAILBOX_t *) & slimword0;
					if (((slimword0 & OWN_CHIP) != OWN_CHIP)
					    && slimmb->mbxStatus) {
						psli->sli_flag &=
						    ~LPFC_SLI2_ACTIVE;
						word0 = slimword0;
					}
				}
			} else {
				/* First copy command data */
				word0 = readl(phba->MBslimaddr);
			}
			/* Read the HBA Host Attention Register */
			ha_copy = readl(phba->HAregaddr);
		}

		if (psli->sli_flag & LPFC_SLI2_ACTIVE) {
			/* copy results back to user */
			lpfc_sli_pcimem_bcopy(&phba->slim2p->mbx, mb,
					MAILBOX_CMD_SIZE);
		} else {
			/* First copy command data */
			lpfc_memcpy_from_slim(mb, phba->MBslimaddr,
							MAILBOX_CMD_SIZE);
			if ((mb->mbxCommand == MBX_DUMP_MEMORY) &&
				pmbox->context2) {
				lpfc_memcpy_from_slim((void *)pmbox->context2,
				      phba->MBslimaddr + DMP_RSP_OFFSET,
						      mb->un.varDmp.word_cnt);
			}
		}

		writel(HA_MBATT, phba->HAregaddr);
		readl(phba->HAregaddr); /* flush */

		psli->sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
		status = mb->mbxStatus;
	}

	spin_unlock_irqrestore(phba->host->host_lock, drvr_flag);
	return (status);
}

static int
lpfc_sli_ringtx_put(struct lpfc_hba * phba, struct lpfc_sli_ring * pring,
		    struct lpfc_iocbq * piocb)
{
	/* Insert the caller's iocb in the txq tail for later processing. */
	list_add_tail(&piocb->list, &pring->txq);
	pring->txq_cnt++;
	return (0);
}

static struct lpfc_iocbq *
lpfc_sli_next_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		   struct lpfc_iocbq ** piocb)
{
	struct lpfc_iocbq * nextiocb;

	nextiocb = lpfc_sli_ringtx_get(phba, pring);
	if (!nextiocb) {
		nextiocb = *piocb;
		*piocb = NULL;
	}

	return nextiocb;
}

int
lpfc_sli_issue_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		    struct lpfc_iocbq *piocb, uint32_t flag)
{
	struct lpfc_iocbq *nextiocb;
	IOCB_t *iocb;

	/*
	 * We should never get an IOCB if we are in a < LINK_DOWN state
	 */
	if (unlikely(phba->hba_state < LPFC_LINK_DOWN))
		return IOCB_ERROR;

	/*
	 * Check to see if we are blocking IOCB processing because of a
	 * outstanding mbox command.
	 */
	if (unlikely(pring->flag & LPFC_STOP_IOCB_MBX))
		goto iocb_busy;

	if (unlikely(phba->hba_state == LPFC_LINK_DOWN)) {
		/*
		 * Only CREATE_XRI, CLOSE_XRI, ABORT_XRI, and QUE_RING_BUF
		 * can be issued if the link is not up.
		 */
		switch (piocb->iocb.ulpCommand) {
		case CMD_QUE_RING_BUF_CN:
		case CMD_QUE_RING_BUF64_CN:
			/*
			 * For IOCBs, like QUE_RING_BUF, that have no rsp ring
			 * completion, iocb_cmpl MUST be 0.
			 */
			if (piocb->iocb_cmpl)
				piocb->iocb_cmpl = NULL;
			/*FALLTHROUGH*/
		case CMD_CREATE_XRI_CR:
			break;
		default:
			goto iocb_busy;
		}

	/*
	 * For FCP commands, we must be in a state where we can process link
	 * attention events.
	 */
	} else if (unlikely(pring->ringno == phba->sli.fcp_ring &&
		   !(phba->sli.sli_flag & LPFC_PROCESS_LA)))
		goto iocb_busy;

	while ((iocb = lpfc_sli_next_iocb_slot(phba, pring)) &&
	       (nextiocb = lpfc_sli_next_iocb(phba, pring, &piocb)))
		lpfc_sli_submit_iocb(phba, pring, iocb, nextiocb);

	if (iocb)
		lpfc_sli_update_ring(phba, pring);
	else
		lpfc_sli_update_full_ring(phba, pring);

	if (!piocb)
		return IOCB_SUCCESS;

	goto out_busy;

 iocb_busy:
	pring->stats.iocb_cmd_delay++;

 out_busy:

	if (!(flag & SLI_IOCB_RET_IOCB)) {
		lpfc_sli_ringtx_put(phba, pring, piocb);
		return IOCB_SUCCESS;
	}

	return IOCB_BUSY;
}

static int
lpfc_extra_ring_setup( struct lpfc_hba *phba)
{
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;

	psli = &phba->sli;

	/* Adjust cmd/rsp ring iocb entries more evenly */

	/* Take some away from the FCP ring */
	pring = &psli->ring[psli->fcp_ring];
	pring->numCiocb -= SLI2_IOCB_CMD_R1XTRA_ENTRIES;
	pring->numRiocb -= SLI2_IOCB_RSP_R1XTRA_ENTRIES;
	pring->numCiocb -= SLI2_IOCB_CMD_R3XTRA_ENTRIES;
	pring->numRiocb -= SLI2_IOCB_RSP_R3XTRA_ENTRIES;

	/* and give them to the extra ring */
	pring = &psli->ring[psli->extra_ring];

	pring->numCiocb += SLI2_IOCB_CMD_R1XTRA_ENTRIES;
	pring->numRiocb += SLI2_IOCB_RSP_R1XTRA_ENTRIES;
	pring->numCiocb += SLI2_IOCB_CMD_R3XTRA_ENTRIES;
	pring->numRiocb += SLI2_IOCB_RSP_R3XTRA_ENTRIES;

	/* Setup default profile for this ring */
	pring->iotag_max = 4096;
	pring->num_mask = 1;
	pring->prt[0].profile = 0;      /* Mask 0 */
	pring->prt[0].rctl = phba->cfg_multi_ring_rctl;
	pring->prt[0].type = phba->cfg_multi_ring_type;
	pring->prt[0].lpfc_sli_rcv_unsol_event = NULL;
	return 0;
}

int
lpfc_sli_setup(struct lpfc_hba *phba)
{
	int i, totiocb = 0;
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring;

	psli->num_rings = MAX_CONFIGURED_RINGS;
	psli->sli_flag = 0;
	psli->fcp_ring = LPFC_FCP_RING;
	psli->next_ring = LPFC_FCP_NEXT_RING;
	psli->extra_ring = LPFC_EXTRA_RING;

	psli->iocbq_lookup = NULL;
	psli->iocbq_lookup_len = 0;
	psli->last_iotag = 0;

	for (i = 0; i < psli->num_rings; i++) {
		pring = &psli->ring[i];
		switch (i) {
		case LPFC_FCP_RING:	/* ring 0 - FCP */
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R0_ENTRIES;
			pring->numRiocb = SLI2_IOCB_RSP_R0_ENTRIES;
			pring->numCiocb += SLI2_IOCB_CMD_R1XTRA_ENTRIES;
			pring->numRiocb += SLI2_IOCB_RSP_R1XTRA_ENTRIES;
			pring->numCiocb += SLI2_IOCB_CMD_R3XTRA_ENTRIES;
			pring->numRiocb += SLI2_IOCB_RSP_R3XTRA_ENTRIES;
			pring->iotag_ctr = 0;
			pring->iotag_max =
			    (phba->cfg_hba_queue_depth * 2);
			pring->fast_iotag = pring->iotag_max;
			pring->num_mask = 0;
			break;
		case LPFC_EXTRA_RING:	/* ring 1 - EXTRA */
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R1_ENTRIES;
			pring->numRiocb = SLI2_IOCB_RSP_R1_ENTRIES;
			pring->num_mask = 0;
			break;
		case LPFC_ELS_RING:	/* ring 2 - ELS / CT */
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R2_ENTRIES;
			pring->numRiocb = SLI2_IOCB_RSP_R2_ENTRIES;
			pring->fast_iotag = 0;
			pring->iotag_ctr = 0;
			pring->iotag_max = 4096;
			pring->num_mask = 4;
			pring->prt[0].profile = 0;	/* Mask 0 */
			pring->prt[0].rctl = FC_ELS_REQ;
			pring->prt[0].type = FC_ELS_DATA;
			pring->prt[0].lpfc_sli_rcv_unsol_event =
			    lpfc_els_unsol_event;
			pring->prt[1].profile = 0;	/* Mask 1 */
			pring->prt[1].rctl = FC_ELS_RSP;
			pring->prt[1].type = FC_ELS_DATA;
			pring->prt[1].lpfc_sli_rcv_unsol_event =
			    lpfc_els_unsol_event;
			pring->prt[2].profile = 0;	/* Mask 2 */
			/* NameServer Inquiry */
			pring->prt[2].rctl = FC_UNSOL_CTL;
			/* NameServer */
			pring->prt[2].type = FC_COMMON_TRANSPORT_ULP;
			pring->prt[2].lpfc_sli_rcv_unsol_event =
			    lpfc_ct_unsol_event;
			pring->prt[3].profile = 0;	/* Mask 3 */
			/* NameServer response */
			pring->prt[3].rctl = FC_SOL_CTL;
			/* NameServer */
			pring->prt[3].type = FC_COMMON_TRANSPORT_ULP;
			pring->prt[3].lpfc_sli_rcv_unsol_event =
			    lpfc_ct_unsol_event;
			break;
		}
		totiocb += (pring->numCiocb + pring->numRiocb);
	}
	if (totiocb > MAX_SLI2_IOCB) {
		/* Too many cmd / rsp ring entries in SLI2 SLIM */
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"%d:0462 Too many cmd / rsp ring entries in "
				"SLI2 SLIM Data: x%x x%x\n",
				phba->brd_no, totiocb, MAX_SLI2_IOCB);
	}
	if (phba->cfg_multi_ring_support == 2)
		lpfc_extra_ring_setup(phba);

	return 0;
}

int
lpfc_sli_queue_setup(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	int i;

	psli = &phba->sli;
	spin_lock_irq(phba->host->host_lock);
	INIT_LIST_HEAD(&psli->mboxq);
	/* Initialize list headers for txq and txcmplq as double linked lists */
	for (i = 0; i < psli->num_rings; i++) {
		pring = &psli->ring[i];
		pring->ringno = i;
		pring->next_cmdidx  = 0;
		pring->local_getidx = 0;
		pring->cmdidx = 0;
		INIT_LIST_HEAD(&pring->txq);
		INIT_LIST_HEAD(&pring->txcmplq);
		INIT_LIST_HEAD(&pring->iocb_continueq);
		INIT_LIST_HEAD(&pring->postbufq);
	}
	spin_unlock_irq(phba->host->host_lock);
	return (1);
}

int
lpfc_sli_hba_down(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	LPFC_MBOXQ_t *pmb;
	struct lpfc_iocbq *iocb, *next_iocb;
	IOCB_t *icmd = NULL;
	int i;
	unsigned long flags = 0;

	psli = &phba->sli;
	lpfc_hba_down_prep(phba);

	spin_lock_irqsave(phba->host->host_lock, flags);

	for (i = 0; i < psli->num_rings; i++) {
		pring = &psli->ring[i];
		pring->flag |= LPFC_DEFERRED_RING_EVENT;

		/*
		 * Error everything on the txq since these iocbs have not been
		 * given to the FW yet.
		 */
		pring->txq_cnt = 0;

		list_for_each_entry_safe(iocb, next_iocb, &pring->txq, list) {
			list_del_init(&iocb->list);
			if (iocb->iocb_cmpl) {
				icmd = &iocb->iocb;
				icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
				icmd->un.ulpWord[4] = IOERR_SLI_DOWN;
				spin_unlock_irqrestore(phba->host->host_lock,
						       flags);
				(iocb->iocb_cmpl) (phba, iocb, iocb);
				spin_lock_irqsave(phba->host->host_lock, flags);
			} else
				lpfc_sli_release_iocbq(phba, iocb);
		}

		INIT_LIST_HEAD(&(pring->txq));

	}

	spin_unlock_irqrestore(phba->host->host_lock, flags);

	/* Return any active mbox cmds */
	del_timer_sync(&psli->mbox_tmo);
	spin_lock_irqsave(phba->host->host_lock, flags);
	phba->work_hba_events &= ~WORKER_MBOX_TMO;
	if (psli->mbox_active) {
		pmb = psli->mbox_active;
		pmb->mb.mbxStatus = MBX_NOT_FINISHED;
		if (pmb->mbox_cmpl) {
			spin_unlock_irqrestore(phba->host->host_lock, flags);
			pmb->mbox_cmpl(phba,pmb);
			spin_lock_irqsave(phba->host->host_lock, flags);
		}
	}
	psli->sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
	psli->mbox_active = NULL;

	/* Return any pending mbox cmds */
	while ((pmb = lpfc_mbox_get(phba)) != NULL) {
		pmb->mb.mbxStatus = MBX_NOT_FINISHED;
		if (pmb->mbox_cmpl) {
			spin_unlock_irqrestore(phba->host->host_lock, flags);
			pmb->mbox_cmpl(phba,pmb);
			spin_lock_irqsave(phba->host->host_lock, flags);
		}
	}

	INIT_LIST_HEAD(&psli->mboxq);

	spin_unlock_irqrestore(phba->host->host_lock, flags);

	return 1;
}

void
lpfc_sli_pcimem_bcopy(void *srcp, void *destp, uint32_t cnt)
{
	uint32_t *src = srcp;
	uint32_t *dest = destp;
	uint32_t ldata;
	int i;

	for (i = 0; i < (int)cnt; i += sizeof (uint32_t)) {
		ldata = *src;
		ldata = le32_to_cpu(ldata);
		*dest = ldata;
		src++;
		dest++;
	}
}

int
lpfc_sli_ringpostbuf_put(struct lpfc_hba * phba, struct lpfc_sli_ring * pring,
			 struct lpfc_dmabuf * mp)
{
	/* Stick struct lpfc_dmabuf at end of postbufq so driver can look it up
	   later */
	list_add_tail(&mp->list, &pring->postbufq);

	pring->postbufq_cnt++;
	return 0;
}


struct lpfc_dmabuf *
lpfc_sli_ringpostbuf_get(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
			 dma_addr_t phys)
{
	struct lpfc_dmabuf *mp, *next_mp;
	struct list_head *slp = &pring->postbufq;

	/* Search postbufq, from the begining, looking for a match on phys */
	list_for_each_entry_safe(mp, next_mp, &pring->postbufq, list) {
		if (mp->phys == phys) {
			list_del_init(&mp->list);
			pring->postbufq_cnt--;
			return mp;
		}
	}

	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"%d:0410 Cannot find virtual addr for mapped buf on "
			"ring %d Data x%llx x%p x%p x%x\n",
			phba->brd_no, pring->ringno, (unsigned long long)phys,
			slp->next, slp->prev, pring->postbufq_cnt);
	return NULL;
}

static void
lpfc_sli_abort_elsreq_cmpl(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
			   struct lpfc_iocbq * rspiocb)
{
	struct lpfc_dmabuf *buf_ptr, *buf_ptr1;
	/* Free the resources associated with the ELS_REQUEST64 IOCB the driver
	 * just aborted.
	 * In this case, context2  = cmd,  context2->next = rsp, context3 = bpl
	 */
	if (cmdiocb->context2) {
		buf_ptr1 = (struct lpfc_dmabuf *) cmdiocb->context2;

		/* Free the response IOCB before completing the abort
		   command.  */
		buf_ptr = NULL;
		list_remove_head((&buf_ptr1->list), buf_ptr,
				 struct lpfc_dmabuf, list);
		if (buf_ptr) {
			lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
			kfree(buf_ptr);
		}
		lpfc_mbuf_free(phba, buf_ptr1->virt, buf_ptr1->phys);
		kfree(buf_ptr1);
	}

	if (cmdiocb->context3) {
		buf_ptr = (struct lpfc_dmabuf *) cmdiocb->context3;
		lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
		kfree(buf_ptr);
	}

	lpfc_sli_release_iocbq(phba, cmdiocb);
	return;
}

int
lpfc_sli_issue_abort_iotag32(struct lpfc_hba * phba,
			     struct lpfc_sli_ring * pring,
			     struct lpfc_iocbq * cmdiocb)
{
	struct lpfc_iocbq *abtsiocbp;
	IOCB_t *icmd = NULL;
	IOCB_t *iabt = NULL;

	/* issue ABTS for this IOCB based on iotag */
	abtsiocbp = lpfc_sli_get_iocbq(phba);
	if (abtsiocbp == NULL)
		return 0;

	iabt = &abtsiocbp->iocb;
	icmd = &cmdiocb->iocb;
	switch (icmd->ulpCommand) {
	case CMD_ELS_REQUEST64_CR:
		/* Even though we abort the ELS command, the firmware may access
		 * the BPL or other resources before it processes our
		 * ABORT_MXRI64. Thus we must delay reusing the cmdiocb
		 * resources till the actual abort request completes.
		 */
		abtsiocbp->context1 = (void *)((unsigned long)icmd->ulpCommand);
		abtsiocbp->context2 = cmdiocb->context2;
		abtsiocbp->context3 = cmdiocb->context3;
		cmdiocb->context2 = NULL;
		cmdiocb->context3 = NULL;
		abtsiocbp->iocb_cmpl = lpfc_sli_abort_elsreq_cmpl;
		break;
	default:
		lpfc_sli_release_iocbq(phba, abtsiocbp);
		return 0;
	}

	iabt->un.amxri.abortType = ABORT_TYPE_ABTS;
	iabt->un.amxri.iotag32 = icmd->un.elsreq64.bdl.ulpIoTag32;

	iabt->ulpLe = 1;
	iabt->ulpClass = CLASS3;
	iabt->ulpCommand = CMD_ABORT_MXRI64_CN;

	if (lpfc_sli_issue_iocb(phba, pring, abtsiocbp, 0) == IOCB_ERROR) {
		lpfc_sli_release_iocbq(phba, abtsiocbp);
		return 0;
	}

	return 1;
}

static int
lpfc_sli_validate_fcp_iocb(struct lpfc_iocbq *iocbq, uint16_t tgt_id,
			   uint64_t lun_id, uint32_t ctx,
			   lpfc_ctx_cmd ctx_cmd)
{
	struct lpfc_scsi_buf *lpfc_cmd;
	struct scsi_cmnd *cmnd;
	int rc = 1;

	if (!(iocbq->iocb_flag &  LPFC_IO_FCP))
		return rc;

	lpfc_cmd = container_of(iocbq, struct lpfc_scsi_buf, cur_iocbq);
	cmnd = lpfc_cmd->pCmd;

	if (cmnd == NULL)
		return rc;

	switch (ctx_cmd) {
	case LPFC_CTX_LUN:
		if ((cmnd->device->id == tgt_id) &&
		    (cmnd->device->lun == lun_id))
			rc = 0;
		break;
	case LPFC_CTX_TGT:
		if (cmnd->device->id == tgt_id)
			rc = 0;
		break;
	case LPFC_CTX_CTX:
		if (iocbq->iocb.ulpContext == ctx)
			rc = 0;
		break;
	case LPFC_CTX_HOST:
		rc = 0;
		break;
	default:
		printk(KERN_ERR "%s: Unknown context cmd type, value %d\n",
			__FUNCTION__, ctx_cmd);
		break;
	}

	return rc;
}

int
lpfc_sli_sum_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		uint16_t tgt_id, uint64_t lun_id, lpfc_ctx_cmd ctx_cmd)
{
	struct lpfc_iocbq *iocbq;
	int sum, i;

	for (i = 1, sum = 0; i <= phba->sli.last_iotag; i++) {
		iocbq = phba->sli.iocbq_lookup[i];

		if (lpfc_sli_validate_fcp_iocb (iocbq, tgt_id, lun_id,
						0, ctx_cmd) == 0)
			sum++;
	}

	return sum;
}

void
lpfc_sli_abort_fcp_cmpl(struct lpfc_hba * phba, struct lpfc_iocbq * cmdiocb,
			   struct lpfc_iocbq * rspiocb)
{
	spin_lock_irq(phba->host->host_lock);
	lpfc_sli_release_iocbq(phba, cmdiocb);
	spin_unlock_irq(phba->host->host_lock);
	return;
}

int
lpfc_sli_abort_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		    uint16_t tgt_id, uint64_t lun_id, uint32_t ctx,
		    lpfc_ctx_cmd abort_cmd)
{
	struct lpfc_iocbq *iocbq;
	struct lpfc_iocbq *abtsiocb;
	IOCB_t *cmd = NULL;
	int errcnt = 0, ret_val = 0;
	int i;

	for (i = 1; i <= phba->sli.last_iotag; i++) {
		iocbq = phba->sli.iocbq_lookup[i];

		if (lpfc_sli_validate_fcp_iocb (iocbq, tgt_id, lun_id,
						0, abort_cmd) != 0)
			continue;

		/* issue ABTS for this IOCB based on iotag */
		abtsiocb = lpfc_sli_get_iocbq(phba);
		if (abtsiocb == NULL) {
			errcnt++;
			continue;
		}

		cmd = &iocbq->iocb;
		abtsiocb->iocb.un.acxri.abortType = ABORT_TYPE_ABTS;
		abtsiocb->iocb.un.acxri.abortContextTag = cmd->ulpContext;
		abtsiocb->iocb.un.acxri.abortIoTag = cmd->ulpIoTag;
		abtsiocb->iocb.ulpLe = 1;
		abtsiocb->iocb.ulpClass = cmd->ulpClass;

		if (phba->hba_state >= LPFC_LINK_UP)
			abtsiocb->iocb.ulpCommand = CMD_ABORT_XRI_CN;
		else
			abtsiocb->iocb.ulpCommand = CMD_CLOSE_XRI_CN;

		/* Setup callback routine and issue the command. */
		abtsiocb->iocb_cmpl = lpfc_sli_abort_fcp_cmpl;
		ret_val = lpfc_sli_issue_iocb(phba, pring, abtsiocb, 0);
		if (ret_val == IOCB_ERROR) {
			lpfc_sli_release_iocbq(phba, abtsiocb);
			errcnt++;
			continue;
		}
	}

	return errcnt;
}

static void
lpfc_sli_wake_iocb_wait(struct lpfc_hba *phba,
			struct lpfc_iocbq *cmdiocbq,
			struct lpfc_iocbq *rspiocbq)
{
	wait_queue_head_t *pdone_q;
	unsigned long iflags;

	spin_lock_irqsave(phba->host->host_lock, iflags);
	cmdiocbq->iocb_flag |= LPFC_IO_WAKE;
	if (cmdiocbq->context2 && rspiocbq)
		memcpy(&((struct lpfc_iocbq *)cmdiocbq->context2)->iocb,
		       &rspiocbq->iocb, sizeof(IOCB_t));

	pdone_q = cmdiocbq->context_un.wait_queue;
	spin_unlock_irqrestore(phba->host->host_lock, iflags);
	if (pdone_q)
		wake_up(pdone_q);
	return;
}

/*
 * Issue the caller's iocb and wait for its completion, but no longer than the
 * caller's timeout.  Note that iocb_flags is cleared before the
 * lpfc_sli_issue_call since the wake routine sets a unique value and by
 * definition this is a wait function.
 */
int
lpfc_sli_issue_iocb_wait(struct lpfc_hba * phba,
			 struct lpfc_sli_ring * pring,
			 struct lpfc_iocbq * piocb,
			 struct lpfc_iocbq * prspiocbq,
			 uint32_t timeout)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(done_q);
	long timeleft, timeout_req = 0;
	int retval = IOCB_SUCCESS;
	uint32_t creg_val;

	/*
	 * If the caller has provided a response iocbq buffer, then context2
	 * is NULL or its an error.
	 */
	if (prspiocbq) {
		if (piocb->context2)
			return IOCB_ERROR;
		piocb->context2 = prspiocbq;
	}

	piocb->iocb_cmpl = lpfc_sli_wake_iocb_wait;
	piocb->context_un.wait_queue = &done_q;
	piocb->iocb_flag &= ~LPFC_IO_WAKE;

	if (phba->cfg_poll & DISABLE_FCP_RING_INT) {
		creg_val = readl(phba->HCregaddr);
		creg_val |= (HC_R0INT_ENA << LPFC_FCP_RING);
		writel(creg_val, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
	}

	retval = lpfc_sli_issue_iocb(phba, pring, piocb, 0);
	if (retval == IOCB_SUCCESS) {
		timeout_req = timeout * HZ;
		spin_unlock_irq(phba->host->host_lock);
		timeleft = wait_event_timeout(done_q,
				piocb->iocb_flag & LPFC_IO_WAKE,
				timeout_req);
		spin_lock_irq(phba->host->host_lock);

		if (timeleft == 0) {
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"%d:0338 IOCB wait timeout error - no "
					"wake response Data x%x\n",
					phba->brd_no, timeout);
			retval = IOCB_TIMEDOUT;
		} else if (!(piocb->iocb_flag & LPFC_IO_WAKE)) {
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"%d:0330 IOCB wake NOT set, "
					"Data x%x x%lx\n", phba->brd_no,
					timeout, (timeleft / jiffies));
			retval = IOCB_TIMEDOUT;
		} else {
			lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
					"%d:0331 IOCB wake signaled\n",
					phba->brd_no);
		}
	} else {
		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"%d:0332 IOCB wait issue failed, Data x%x\n",
				phba->brd_no, retval);
		retval = IOCB_ERROR;
	}

	if (phba->cfg_poll & DISABLE_FCP_RING_INT) {
		creg_val = readl(phba->HCregaddr);
		creg_val &= ~(HC_R0INT_ENA << LPFC_FCP_RING);
		writel(creg_val, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
	}

	if (prspiocbq)
		piocb->context2 = NULL;

	piocb->context_un.wait_queue = NULL;
	piocb->iocb_cmpl = NULL;
	return retval;
}

int
lpfc_sli_issue_mbox_wait(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmboxq,
			 uint32_t timeout)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(done_q);
	DECLARE_WAITQUEUE(wq_entry, current);
	uint32_t timeleft = 0;
	int retval;

	/* The caller must leave context1 empty. */
	if (pmboxq->context1 != 0) {
		return (MBX_NOT_FINISHED);
	}

	/* setup wake call as IOCB callback */
	pmboxq->mbox_cmpl = lpfc_sli_wake_mbox_wait;
	/* setup context field to pass wait_queue pointer to wake function  */
	pmboxq->context1 = &done_q;

	/* start to sleep before we wait, to avoid races */
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&done_q, &wq_entry);

	/* now issue the command */
	retval = lpfc_sli_issue_mbox(phba, pmboxq, MBX_NOWAIT);

	if (retval == MBX_BUSY || retval == MBX_SUCCESS) {
		timeleft = schedule_timeout(timeout * HZ);
		pmboxq->context1 = NULL;
		/* if schedule_timeout returns 0, we timed out and were not
		   woken up */
		if ((timeleft == 0) || signal_pending(current))
			retval = MBX_TIMEOUT;
		else
			retval = MBX_SUCCESS;
	}


	set_current_state(TASK_RUNNING);
	remove_wait_queue(&done_q, &wq_entry);
	return retval;
}

int
lpfc_sli_flush_mbox_queue(struct lpfc_hba * phba)
{
	int i = 0;

	while (phba->sli.sli_flag & LPFC_SLI_MBOX_ACTIVE && !phba->stopped) {
		if (i++ > LPFC_MBOX_TMO * 1000)
			return 1;

		if (lpfc_sli_handle_mb_event(phba) == 0)
			i = 0;

		msleep(1);
	}

	return (phba->sli.sli_flag & LPFC_SLI_MBOX_ACTIVE) ? 1 : 0;
}

irqreturn_t
lpfc_intr_handler(int irq, void *dev_id)
{
	struct lpfc_hba *phba;
	uint32_t ha_copy;
	uint32_t work_ha_copy;
	unsigned long status;
	int i;
	uint32_t control;

	/*
	 * Get the driver's phba structure from the dev_id and
	 * assume the HBA is not interrupting.
	 */
	phba = (struct lpfc_hba *) dev_id;

	if (unlikely(!phba))
		return IRQ_NONE;

	phba->sli.slistat.sli_intr++;

	/*
	 * Call the HBA to see if it is interrupting.  If not, don't claim
	 * the interrupt
	 */

	/* Ignore all interrupts during initialization. */
	if (unlikely(phba->hba_state < LPFC_LINK_DOWN))
		return IRQ_NONE;

	/*
	 * Read host attention register to determine interrupt source
	 * Clear Attention Sources, except Error Attention (to
	 * preserve status) and Link Attention
	 */
	spin_lock(phba->host->host_lock);
	ha_copy = readl(phba->HAregaddr);
	writel((ha_copy & ~(HA_LATT | HA_ERATT)), phba->HAregaddr);
	readl(phba->HAregaddr); /* flush */
	spin_unlock(phba->host->host_lock);

	if (unlikely(!ha_copy))
		return IRQ_NONE;

	work_ha_copy = ha_copy & phba->work_ha_mask;

	if (unlikely(work_ha_copy)) {
		if (work_ha_copy & HA_LATT) {
			if (phba->sli.sli_flag & LPFC_PROCESS_LA) {
				/*
				 * Turn off Link Attention interrupts
				 * until CLEAR_LA done
				 */
				spin_lock(phba->host->host_lock);
				phba->sli.sli_flag &= ~LPFC_PROCESS_LA;
				control = readl(phba->HCregaddr);
				control &= ~HC_LAINT_ENA;
				writel(control, phba->HCregaddr);
				readl(phba->HCregaddr); /* flush */
				spin_unlock(phba->host->host_lock);
			}
			else
				work_ha_copy &= ~HA_LATT;
		}

		if (work_ha_copy & ~(HA_ERATT|HA_MBATT|HA_LATT)) {
			for (i = 0; i < phba->sli.num_rings; i++) {
				if (work_ha_copy & (HA_RXATT << (4*i))) {
					/*
					 * Turn off Slow Rings interrupts
					 */
					spin_lock(phba->host->host_lock);
					control = readl(phba->HCregaddr);
					control &= ~(HC_R0INT_ENA << i);
					writel(control, phba->HCregaddr);
					readl(phba->HCregaddr); /* flush */
					spin_unlock(phba->host->host_lock);
				}
			}
		}

		if (work_ha_copy & HA_ERATT) {
			phba->hba_state = LPFC_HBA_ERROR;
			/*
			 * There was a link/board error.  Read the
			 * status register to retrieve the error event
			 * and process it.
			 */
			phba->sli.slistat.err_attn_event++;
			/* Save status info */
			phba->work_hs = readl(phba->HSregaddr);
			phba->work_status[0] = readl(phba->MBslimaddr + 0xa8);
			phba->work_status[1] = readl(phba->MBslimaddr + 0xac);

			/* Clear Chip error bit */
			writel(HA_ERATT, phba->HAregaddr);
			readl(phba->HAregaddr); /* flush */
			phba->stopped = 1;
		}

		spin_lock(phba->host->host_lock);
		phba->work_ha |= work_ha_copy;
		if (phba->work_wait)
			wake_up(phba->work_wait);
		spin_unlock(phba->host->host_lock);
	}

	ha_copy &= ~(phba->work_ha_mask);

	/*
	 * Process all events on FCP ring.  Take the optimized path for
	 * FCP IO.  Any other IO is slow path and is handled by
	 * the worker thread.
	 */
	status = (ha_copy & (HA_RXMASK  << (4*LPFC_FCP_RING)));
	status >>= (4*LPFC_FCP_RING);
	if (status & HA_RXATT)
		lpfc_sli_handle_fast_ring_event(phba,
						&phba->sli.ring[LPFC_FCP_RING],
						status);

	if (phba->cfg_multi_ring_support == 2) {
		/*
		 * Process all events on extra ring.  Take the optimized path
		 * for extra ring IO.  Any other IO is slow path and is handled
		 * by the worker thread.
		 */
		status = (ha_copy & (HA_RXMASK  << (4*LPFC_EXTRA_RING)));
		status >>= (4*LPFC_EXTRA_RING);
		if (status & HA_RXATT) {
			lpfc_sli_handle_fast_ring_event(phba,
					&phba->sli.ring[LPFC_EXTRA_RING],
					status);
		}
	}
	return IRQ_HANDLED;

} /* lpfc_intr_handler */
