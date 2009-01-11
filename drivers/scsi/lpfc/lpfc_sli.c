/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2008 Emulex.  All rights reserved.           *
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
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_crtn.h"
#include "lpfc_logmsg.h"
#include "lpfc_compat.h"
#include "lpfc_debugfs.h"

/*
 * Define macro to log: Mailbox command x%x cannot issue Data
 * This allows multiple uses of lpfc_msgBlk0311
 * w/o perturbing log msg utility.
 */
#define LOG_MBOX_CANNOT_ISSUE_DATA(phba, pmbox, psli, flag) \
			lpfc_printf_log(phba, \
				KERN_INFO, \
				LOG_MBOX | LOG_SLI, \
				"(%d):0311 Mailbox command x%x cannot " \
				"issue Data: x%x x%x x%x\n", \
				pmbox->vport ? pmbox->vport->vpi : 0, \
				pmbox->mb.mbxCommand,		\
				phba->pport->port_state,	\
				psli->sli_flag,	\
				flag)


/* There are only four IOCB completion types. */
typedef enum _lpfc_iocb_type {
	LPFC_UNKNOWN_IOCB,
	LPFC_UNSOL_IOCB,
	LPFC_SOL_IOCB,
	LPFC_ABORT_IOCB
} lpfc_iocb_type;

/**
 * lpfc_cmd_iocb: Get next command iocb entry in the ring.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 *
 * This function returns pointer to next command iocb entry
 * in the command ring. The caller must hold hbalock to prevent
 * other threads consume the next command iocb.
 * SLI-2/SLI-3 provide different sized iocbs.
 **/
static inline IOCB_t *
lpfc_cmd_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring)
{
	return (IOCB_t *) (((char *) pring->cmdringaddr) +
			   pring->cmdidx * phba->iocb_cmd_size);
}

/**
 * lpfc_resp_iocb: Get next response iocb entry in the ring.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 *
 * This function returns pointer to next response iocb entry
 * in the response ring. The caller must hold hbalock to make sure
 * that no other thread consume the next response iocb.
 * SLI-2/SLI-3 provide different sized iocbs.
 **/
static inline IOCB_t *
lpfc_resp_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring)
{
	return (IOCB_t *) (((char *) pring->rspringaddr) +
			   pring->rspidx * phba->iocb_rsp_size);
}

/**
 * __lpfc_sli_get_iocbq: Allocates an iocb object from iocb pool.
 * @phba: Pointer to HBA context object.
 *
 * This function is called with hbalock held. This function
 * allocates a new driver iocb object from the iocb pool. If the
 * allocation is successful, it returns pointer to the newly
 * allocated iocb object else it returns NULL.
 **/
static struct lpfc_iocbq *
__lpfc_sli_get_iocbq(struct lpfc_hba *phba)
{
	struct list_head *lpfc_iocb_list = &phba->lpfc_iocb_list;
	struct lpfc_iocbq * iocbq = NULL;

	list_remove_head(lpfc_iocb_list, iocbq, struct lpfc_iocbq, list);
	return iocbq;
}

/**
 * lpfc_sli_get_iocbq: Allocates an iocb object from iocb pool.
 * @phba: Pointer to HBA context object.
 *
 * This function is called with no lock held. This function
 * allocates a new driver iocb object from the iocb pool. If the
 * allocation is successful, it returns pointer to the newly
 * allocated iocb object else it returns NULL.
 **/
struct lpfc_iocbq *
lpfc_sli_get_iocbq(struct lpfc_hba *phba)
{
	struct lpfc_iocbq * iocbq = NULL;
	unsigned long iflags;

	spin_lock_irqsave(&phba->hbalock, iflags);
	iocbq = __lpfc_sli_get_iocbq(phba);
	spin_unlock_irqrestore(&phba->hbalock, iflags);
	return iocbq;
}

/**
 * __lpfc_sli_release_iocbq: Release iocb to the iocb pool.
 * @phba: Pointer to HBA context object.
 * @iocbq: Pointer to driver iocb object.
 *
 * This function is called with hbalock held to release driver
 * iocb object to the iocb pool. The iotag in the iocb object
 * does not change for each use of the iocb object. This function
 * clears all other fields of the iocb object when it is freed.
 **/
static void
__lpfc_sli_release_iocbq(struct lpfc_hba *phba, struct lpfc_iocbq *iocbq)
{
	size_t start_clean = offsetof(struct lpfc_iocbq, iocb);

	/*
	 * Clean all volatile data fields, preserve iotag and node struct.
	 */
	memset((char*)iocbq + start_clean, 0, sizeof(*iocbq) - start_clean);
	list_add_tail(&iocbq->list, &phba->lpfc_iocb_list);
}

/**
 * lpfc_sli_release_iocbq: Release iocb to the iocb pool.
 * @phba: Pointer to HBA context object.
 * @iocbq: Pointer to driver iocb object.
 *
 * This function is called with no lock held to release the iocb to
 * iocb pool.
 **/
void
lpfc_sli_release_iocbq(struct lpfc_hba *phba, struct lpfc_iocbq *iocbq)
{
	unsigned long iflags;

	/*
	 * Clean all volatile data fields, preserve iotag and node struct.
	 */
	spin_lock_irqsave(&phba->hbalock, iflags);
	__lpfc_sli_release_iocbq(phba, iocbq);
	spin_unlock_irqrestore(&phba->hbalock, iflags);
}

/**
 * lpfc_sli_iocb_cmd_type: Get the iocb type.
 * @iocb_cmnd : iocb command code.
 *
 * This function is called by ring event handler function to get the iocb type.
 * This function translates the iocb command to an iocb command type used to
 * decide the final disposition of each completed IOCB.
 * The function returns
 * LPFC_UNKNOWN_IOCB if it is an unsupported iocb
 * LPFC_SOL_IOCB     if it is a solicited iocb completion
 * LPFC_ABORT_IOCB   if it is an abort iocb
 * LPFC_UNSOL_IOCB   if it is an unsolicited iocb
 *
 * The caller is not required to hold any lock.
 **/
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
	case CMD_FCP_TSEND_CX:
	case CMD_FCP_TRSP_CX:
	case CMD_FCP_TRECEIVE_CX:
	case CMD_FCP_AUTO_TRSP_CX:
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
	case CMD_FCP_TSEND64_CX:
	case CMD_FCP_TRSP64_CX:
	case CMD_FCP_TRECEIVE64_CX:
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
	case CMD_ASYNC_STATUS:
	case CMD_IOCB_RCV_SEQ64_CX:
	case CMD_IOCB_RCV_ELS64_CX:
	case CMD_IOCB_RCV_CONT64_CX:
	case CMD_IOCB_RET_XRI64_CX:
		type = LPFC_UNSOL_IOCB;
		break;
	case CMD_IOCB_XMIT_MSEQ64_CR:
	case CMD_IOCB_XMIT_MSEQ64_CX:
	case CMD_IOCB_RCV_SEQ_LIST64_CX:
	case CMD_IOCB_RCV_ELS_LIST64_CX:
	case CMD_IOCB_CLOSE_EXTENDED_CN:
	case CMD_IOCB_ABORT_EXTENDED_CN:
	case CMD_IOCB_RET_HBQE64_CN:
	case CMD_IOCB_FCP_IBIDIR64_CR:
	case CMD_IOCB_FCP_IBIDIR64_CX:
	case CMD_IOCB_FCP_ITASKMGT64_CX:
	case CMD_IOCB_LOGENTRY_CN:
	case CMD_IOCB_LOGENTRY_ASYNC_CN:
		printk("%s - Unhandled SLI-3 Command x%x\n",
				__func__, iocb_cmnd);
		type = LPFC_UNKNOWN_IOCB;
		break;
	default:
		type = LPFC_UNKNOWN_IOCB;
		break;
	}

	return type;
}

/**
 * lpfc_sli_ring_map: Issue config_ring mbox for all rings.
 * @phba: Pointer to HBA context object.
 *
 * This function is called from SLI initialization code
 * to configure every ring of the HBA's SLI interface. The
 * caller is not required to hold any lock. This function issues
 * a config_ring mailbox command for each ring.
 * This function returns zero if successful else returns a negative
 * error code.
 **/
static int
lpfc_sli_ring_map(struct lpfc_hba *phba)
{
	struct lpfc_sli *psli = &phba->sli;
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *pmbox;
	int i, rc, ret = 0;

	pmb = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb)
		return -ENOMEM;
	pmbox = &pmb->mb;
	phba->link_state = LPFC_INIT_MBX_CMDS;
	for (i = 0; i < psli->num_rings; i++) {
		lpfc_config_ring(phba, i, pmb);
		rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);
		if (rc != MBX_SUCCESS) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"0446 Adapter failed to init (%d), "
					"mbxCmd x%x CFG_RING, mbxStatus x%x, "
					"ring %d\n",
					rc, pmbox->mbxCommand,
					pmbox->mbxStatus, i);
			phba->link_state = LPFC_HBA_ERROR;
			ret = -ENXIO;
			break;
		}
	}
	mempool_free(pmb, phba->mbox_mem_pool);
	return ret;
}

/**
 * lpfc_sli_ringtxcmpl_put: Adds new iocb to the txcmplq.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @piocb: Pointer to the driver iocb object.
 *
 * This function is called with hbalock held. The function adds the
 * new iocb to txcmplq of the given ring. This function always returns
 * 0. If this function is called for ELS ring, this function checks if
 * there is a vport associated with the ELS command. This function also
 * starts els_tmofunc timer if this is an ELS command.
 **/
static int
lpfc_sli_ringtxcmpl_put(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
			struct lpfc_iocbq *piocb)
{
	list_add_tail(&piocb->list, &pring->txcmplq);
	pring->txcmplq_cnt++;
	if ((unlikely(pring->ringno == LPFC_ELS_RING)) &&
	   (piocb->iocb.ulpCommand != CMD_ABORT_XRI_CN) &&
	   (piocb->iocb.ulpCommand != CMD_CLOSE_XRI_CN)) {
		if (!piocb->vport)
			BUG();
		else
			mod_timer(&piocb->vport->els_tmofunc,
				  jiffies + HZ * (phba->fc_ratov << 1));
	}


	return 0;
}

/**
 * lpfc_sli_ringtx_get: Get first element of the txq.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 *
 * This function is called with hbalock held to get next
 * iocb in txq of the given ring. If there is any iocb in
 * the txq, the function returns first iocb in the list after
 * removing the iocb from the list, else it returns NULL.
 **/
static struct lpfc_iocbq *
lpfc_sli_ringtx_get(struct lpfc_hba *phba, struct lpfc_sli_ring *pring)
{
	struct lpfc_iocbq *cmd_iocb;

	list_remove_head((&pring->txq), cmd_iocb, struct lpfc_iocbq, list);
	if (cmd_iocb != NULL)
		pring->txq_cnt--;
	return cmd_iocb;
}

/**
 * lpfc_sli_next_iocb_slot: Get next iocb slot in the ring.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 *
 * This function is called with hbalock held and the caller must post the
 * iocb without releasing the lock. If the caller releases the lock,
 * iocb slot returned by the function is not guaranteed to be available.
 * The function returns pointer to the next available iocb slot if there
 * is available slot in the ring, else it returns NULL.
 * If the get index of the ring is ahead of the put index, the function
 * will post an error attention event to the worker thread to take the
 * HBA to offline state.
 **/
static IOCB_t *
lpfc_sli_next_iocb_slot (struct lpfc_hba *phba, struct lpfc_sli_ring *pring)
{
	struct lpfc_pgp *pgp = &phba->port_gp[pring->ringno];
	uint32_t  max_cmd_idx = pring->numCiocb;
	if ((pring->next_cmdidx == pring->cmdidx) &&
	   (++pring->next_cmdidx >= max_cmd_idx))
		pring->next_cmdidx = 0;

	if (unlikely(pring->local_getidx == pring->next_cmdidx)) {

		pring->local_getidx = le32_to_cpu(pgp->cmdGetInx);

		if (unlikely(pring->local_getidx >= max_cmd_idx)) {
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"0315 Ring %d issue: portCmdGet %d "
					"is bigger than cmd ring %d\n",
					pring->ringno,
					pring->local_getidx, max_cmd_idx);

			phba->link_state = LPFC_HBA_ERROR;
			/*
			 * All error attention handlers are posted to
			 * worker thread
			 */
			phba->work_ha |= HA_ERATT;
			phba->work_hs = HS_FFER3;

			lpfc_worker_wake_up(phba);

			return NULL;
		}

		if (pring->local_getidx == pring->next_cmdidx)
			return NULL;
	}

	return lpfc_cmd_iocb(phba, pring);
}

/**
 * lpfc_sli_next_iotag: Get an iotag for the iocb.
 * @phba: Pointer to HBA context object.
 * @iocbq: Pointer to driver iocb object.
 *
 * This function gets an iotag for the iocb. If there is no unused iotag and
 * the iocbq_lookup_len < 0xffff, this function allocates a bigger iotag_lookup
 * array and assigns a new iotag.
 * The function returns the allocated iotag if successful, else returns zero.
 * Zero is not a valid iotag.
 * The caller is not required to hold any lock.
 **/
uint16_t
lpfc_sli_next_iotag(struct lpfc_hba *phba, struct lpfc_iocbq *iocbq)
{
	struct lpfc_iocbq **new_arr;
	struct lpfc_iocbq **old_arr;
	size_t new_len;
	struct lpfc_sli *psli = &phba->sli;
	uint16_t iotag;

	spin_lock_irq(&phba->hbalock);
	iotag = psli->last_iotag;
	if(++iotag < psli->iocbq_lookup_len) {
		psli->last_iotag = iotag;
		psli->iocbq_lookup[iotag] = iocbq;
		spin_unlock_irq(&phba->hbalock);
		iocbq->iotag = iotag;
		return iotag;
	} else if (psli->iocbq_lookup_len < (0xffff
					   - LPFC_IOCBQ_LOOKUP_INCREMENT)) {
		new_len = psli->iocbq_lookup_len + LPFC_IOCBQ_LOOKUP_INCREMENT;
		spin_unlock_irq(&phba->hbalock);
		new_arr = kzalloc(new_len * sizeof (struct lpfc_iocbq *),
				  GFP_KERNEL);
		if (new_arr) {
			spin_lock_irq(&phba->hbalock);
			old_arr = psli->iocbq_lookup;
			if (new_len <= psli->iocbq_lookup_len) {
				/* highly unprobable case */
				kfree(new_arr);
				iotag = psli->last_iotag;
				if(++iotag < psli->iocbq_lookup_len) {
					psli->last_iotag = iotag;
					psli->iocbq_lookup[iotag] = iocbq;
					spin_unlock_irq(&phba->hbalock);
					iocbq->iotag = iotag;
					return iotag;
				}
				spin_unlock_irq(&phba->hbalock);
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
			spin_unlock_irq(&phba->hbalock);
			iocbq->iotag = iotag;
			kfree(old_arr);
			return iotag;
		}
	} else
		spin_unlock_irq(&phba->hbalock);

	lpfc_printf_log(phba, KERN_ERR,LOG_SLI,
			"0318 Failed to allocate IOTAG.last IOTAG is %d\n",
			psli->last_iotag);

	return 0;
}

/**
 * lpfc_sli_submit_iocb: Submit an iocb to the firmware.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @iocb: Pointer to iocb slot in the ring.
 * @nextiocb: Pointer to driver iocb object which need to be
 *            posted to firmware.
 *
 * This function is called with hbalock held to post a new iocb to
 * the firmware. This function copies the new iocb to ring iocb slot and
 * updates the ring pointers. It adds the new iocb to txcmplq if there is
 * a completion call back for this iocb else the function will free the
 * iocb object.
 **/
static void
lpfc_sli_submit_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		IOCB_t *iocb, struct lpfc_iocbq *nextiocb)
{
	/*
	 * Set up an iotag
	 */
	nextiocb->iocb.ulpIoTag = (nextiocb->iocb_cmpl) ? nextiocb->iotag : 0;


	if (pring->ringno == LPFC_ELS_RING) {
		lpfc_debugfs_slow_ring_trc(phba,
			"IOCB cmd ring:   wd4:x%08x wd6:x%08x wd7:x%08x",
			*(((uint32_t *) &nextiocb->iocb) + 4),
			*(((uint32_t *) &nextiocb->iocb) + 6),
			*(((uint32_t *) &nextiocb->iocb) + 7));
	}

	/*
	 * Issue iocb command to adapter
	 */
	lpfc_sli_pcimem_bcopy(&nextiocb->iocb, iocb, phba->iocb_cmd_size);
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
		__lpfc_sli_release_iocbq(phba, nextiocb);

	/*
	 * Let the HBA know what IOCB slot will be the next one the
	 * driver will put a command into.
	 */
	pring->cmdidx = pring->next_cmdidx;
	writel(pring->cmdidx, &phba->host_gp[pring->ringno].cmdPutInx);
}

/**
 * lpfc_sli_update_full_ring: Update the chip attention register.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 *
 * The caller is not required to hold any lock for calling this function.
 * This function updates the chip attention bits for the ring to inform firmware
 * that there are pending work to be done for this ring and requests an
 * interrupt when there is space available in the ring. This function is
 * called when the driver is unable to post more iocbs to the ring due
 * to unavailability of space in the ring.
 **/
static void
lpfc_sli_update_full_ring(struct lpfc_hba *phba, struct lpfc_sli_ring *pring)
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

/**
 * lpfc_sli_update_ring: Update chip attention register.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 *
 * This function updates the chip attention register bit for the
 * given ring to inform HBA that there is more work to be done
 * in this ring. The caller is not required to hold any lock.
 **/
static void
lpfc_sli_update_ring(struct lpfc_hba *phba, struct lpfc_sli_ring *pring)
{
	int ringno = pring->ringno;

	/*
	 * Tell the HBA that there is work to do in this ring.
	 */
	if (!(phba->sli3_options & LPFC_SLI3_CRP_ENABLED)) {
		wmb();
		writel(CA_R0ATT << (ringno * 4), phba->CAregaddr);
		readl(phba->CAregaddr); /* flush */
	}
}

/**
 * lpfc_sli_resume_iocb: Process iocbs in the txq.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 *
 * This function is called with hbalock held to post pending iocbs
 * in the txq to the firmware. This function is called when driver
 * detects space available in the ring.
 **/
static void
lpfc_sli_resume_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring)
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
	    lpfc_is_link_up(phba) &&
	    (pring->ringno != phba->sli.fcp_ring ||
	     phba->sli.sli_flag & LPFC_PROCESS_LA)) {

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

/**
 * lpfc_sli_next_hbq_slot: Get next hbq entry for the HBQ.
 * @phba: Pointer to HBA context object.
 * @hbqno: HBQ number.
 *
 * This function is called with hbalock held to get the next
 * available slot for the given HBQ. If there is free slot
 * available for the HBQ it will return pointer to the next available
 * HBQ entry else it will return NULL.
 **/
static struct lpfc_hbq_entry *
lpfc_sli_next_hbq_slot(struct lpfc_hba *phba, uint32_t hbqno)
{
	struct hbq_s *hbqp = &phba->hbqs[hbqno];

	if (hbqp->next_hbqPutIdx == hbqp->hbqPutIdx &&
	    ++hbqp->next_hbqPutIdx >= hbqp->entry_count)
		hbqp->next_hbqPutIdx = 0;

	if (unlikely(hbqp->local_hbqGetIdx == hbqp->next_hbqPutIdx)) {
		uint32_t raw_index = phba->hbq_get[hbqno];
		uint32_t getidx = le32_to_cpu(raw_index);

		hbqp->local_hbqGetIdx = getidx;

		if (unlikely(hbqp->local_hbqGetIdx >= hbqp->entry_count)) {
			lpfc_printf_log(phba, KERN_ERR,
					LOG_SLI | LOG_VPORT,
					"1802 HBQ %d: local_hbqGetIdx "
					"%u is > than hbqp->entry_count %u\n",
					hbqno, hbqp->local_hbqGetIdx,
					hbqp->entry_count);

			phba->link_state = LPFC_HBA_ERROR;
			return NULL;
		}

		if (hbqp->local_hbqGetIdx == hbqp->next_hbqPutIdx)
			return NULL;
	}

	return (struct lpfc_hbq_entry *) phba->hbqs[hbqno].hbq_virt +
			hbqp->hbqPutIdx;
}

/**
 * lpfc_sli_hbqbuf_free_all: Free all the hbq buffers.
 * @phba: Pointer to HBA context object.
 *
 * This function is called with no lock held to free all the
 * hbq buffers while uninitializing the SLI interface. It also
 * frees the HBQ buffers returned by the firmware but not yet
 * processed by the upper layers.
 **/
void
lpfc_sli_hbqbuf_free_all(struct lpfc_hba *phba)
{
	struct lpfc_dmabuf *dmabuf, *next_dmabuf;
	struct hbq_dmabuf *hbq_buf;
	unsigned long flags;
	int i, hbq_count;
	uint32_t hbqno;

	hbq_count = lpfc_sli_hbq_count();
	/* Return all memory used by all HBQs */
	spin_lock_irqsave(&phba->hbalock, flags);
	for (i = 0; i < hbq_count; ++i) {
		list_for_each_entry_safe(dmabuf, next_dmabuf,
				&phba->hbqs[i].hbq_buffer_list, list) {
			hbq_buf = container_of(dmabuf, struct hbq_dmabuf, dbuf);
			list_del(&hbq_buf->dbuf.list);
			(phba->hbqs[i].hbq_free_buffer)(phba, hbq_buf);
		}
		phba->hbqs[i].buffer_count = 0;
	}
	/* Return all HBQ buffer that are in-fly */
	list_for_each_entry_safe(dmabuf, next_dmabuf,
			&phba->hbqbuf_in_list, list) {
		hbq_buf = container_of(dmabuf, struct hbq_dmabuf, dbuf);
		list_del(&hbq_buf->dbuf.list);
		if (hbq_buf->tag == -1) {
			(phba->hbqs[LPFC_ELS_HBQ].hbq_free_buffer)
				(phba, hbq_buf);
		} else {
			hbqno = hbq_buf->tag >> 16;
			if (hbqno >= LPFC_MAX_HBQS)
				(phba->hbqs[LPFC_ELS_HBQ].hbq_free_buffer)
					(phba, hbq_buf);
			else
				(phba->hbqs[hbqno].hbq_free_buffer)(phba,
					hbq_buf);
		}
	}

	/* Mark the HBQs not in use */
	phba->hbq_in_use = 0;
	spin_unlock_irqrestore(&phba->hbalock, flags);
}

/**
 * lpfc_sli_hbq_to_firmware: Post the hbq buffer to firmware.
 * @phba: Pointer to HBA context object.
 * @hbqno: HBQ number.
 * @hbq_buf: Pointer to HBQ buffer.
 *
 * This function is called with the hbalock held to post a
 * hbq buffer to the firmware. If the function finds an empty
 * slot in the HBQ, it will post the buffer. The function will return
 * pointer to the hbq entry if it successfully post the buffer
 * else it will return NULL.
 **/
static struct lpfc_hbq_entry *
lpfc_sli_hbq_to_firmware(struct lpfc_hba *phba, uint32_t hbqno,
			 struct hbq_dmabuf *hbq_buf)
{
	struct lpfc_hbq_entry *hbqe;
	dma_addr_t physaddr = hbq_buf->dbuf.phys;

	/* Get next HBQ entry slot to use */
	hbqe = lpfc_sli_next_hbq_slot(phba, hbqno);
	if (hbqe) {
		struct hbq_s *hbqp = &phba->hbqs[hbqno];

		hbqe->bde.addrHigh = le32_to_cpu(putPaddrHigh(physaddr));
		hbqe->bde.addrLow  = le32_to_cpu(putPaddrLow(physaddr));
		hbqe->bde.tus.f.bdeSize = hbq_buf->size;
		hbqe->bde.tus.f.bdeFlags = 0;
		hbqe->bde.tus.w = le32_to_cpu(hbqe->bde.tus.w);
		hbqe->buffer_tag = le32_to_cpu(hbq_buf->tag);
				/* Sync SLIM */
		hbqp->hbqPutIdx = hbqp->next_hbqPutIdx;
		writel(hbqp->hbqPutIdx, phba->hbq_put + hbqno);
				/* flush */
		readl(phba->hbq_put + hbqno);
		list_add_tail(&hbq_buf->dbuf.list, &hbqp->hbq_buffer_list);
	}
	return hbqe;
}

/* HBQ for ELS and CT traffic. */
static struct lpfc_hbq_init lpfc_els_hbq = {
	.rn = 1,
	.entry_count = 200,
	.mask_count = 0,
	.profile = 0,
	.ring_mask = (1 << LPFC_ELS_RING),
	.buffer_count = 0,
	.init_count = 20,
	.add_count = 5,
};

/* HBQ for the extra ring if needed */
static struct lpfc_hbq_init lpfc_extra_hbq = {
	.rn = 1,
	.entry_count = 200,
	.mask_count = 0,
	.profile = 0,
	.ring_mask = (1 << LPFC_EXTRA_RING),
	.buffer_count = 0,
	.init_count = 0,
	.add_count = 5,
};

/* Array of HBQs */
struct lpfc_hbq_init *lpfc_hbq_defs[] = {
	&lpfc_els_hbq,
	&lpfc_extra_hbq,
};

/**
 * lpfc_sli_hbqbuf_fill_hbqs: Post more hbq buffers to HBQ.
 * @phba: Pointer to HBA context object.
 * @hbqno: HBQ number.
 * @count: Number of HBQ buffers to be posted.
 *
 * This function is called with no lock held to post more hbq buffers to the
 * given HBQ. The function returns the number of HBQ buffers successfully
 * posted.
 **/
static int
lpfc_sli_hbqbuf_fill_hbqs(struct lpfc_hba *phba, uint32_t hbqno, uint32_t count)
{
	uint32_t i, posted = 0;
	unsigned long flags;
	struct hbq_dmabuf *hbq_buffer;
	LIST_HEAD(hbq_buf_list);
	if (!phba->hbqs[hbqno].hbq_alloc_buffer)
		return 0;

	if ((phba->hbqs[hbqno].buffer_count + count) >
	    lpfc_hbq_defs[hbqno]->entry_count)
		count = lpfc_hbq_defs[hbqno]->entry_count -
					phba->hbqs[hbqno].buffer_count;
	if (!count)
		return 0;
	/* Allocate HBQ entries */
	for (i = 0; i < count; i++) {
		hbq_buffer = (phba->hbqs[hbqno].hbq_alloc_buffer)(phba);
		if (!hbq_buffer)
			break;
		list_add_tail(&hbq_buffer->dbuf.list, &hbq_buf_list);
	}
	/* Check whether HBQ is still in use */
	spin_lock_irqsave(&phba->hbalock, flags);
	if (!phba->hbq_in_use)
		goto err;
	while (!list_empty(&hbq_buf_list)) {
		list_remove_head(&hbq_buf_list, hbq_buffer, struct hbq_dmabuf,
				 dbuf.list);
		hbq_buffer->tag = (phba->hbqs[hbqno].buffer_count |
				      (hbqno << 16));
		if (lpfc_sli_hbq_to_firmware(phba, hbqno, hbq_buffer)) {
			phba->hbqs[hbqno].buffer_count++;
			posted++;
		} else
			(phba->hbqs[hbqno].hbq_free_buffer)(phba, hbq_buffer);
	}
	spin_unlock_irqrestore(&phba->hbalock, flags);
	return posted;
err:
	spin_unlock_irqrestore(&phba->hbalock, flags);
	while (!list_empty(&hbq_buf_list)) {
		list_remove_head(&hbq_buf_list, hbq_buffer, struct hbq_dmabuf,
				 dbuf.list);
		(phba->hbqs[hbqno].hbq_free_buffer)(phba, hbq_buffer);
	}
	return 0;
}

/**
 * lpfc_sli_hbqbuf_add_hbqs: Post more HBQ buffers to firmware.
 * @phba: Pointer to HBA context object.
 * @qno: HBQ number.
 *
 * This function posts more buffers to the HBQ. This function
 * is called with no lock held. The function returns the number of HBQ entries
 * successfully allocated.
 **/
int
lpfc_sli_hbqbuf_add_hbqs(struct lpfc_hba *phba, uint32_t qno)
{
	return(lpfc_sli_hbqbuf_fill_hbqs(phba, qno,
					 lpfc_hbq_defs[qno]->add_count));
}

/**
 * lpfc_sli_hbqbuf_init_hbqs: Post initial buffers to the HBQ.
 * @phba: Pointer to HBA context object.
 * @qno:  HBQ queue number.
 *
 * This function is called from SLI initialization code path with
 * no lock held to post initial HBQ buffers to firmware. The
 * function returns the number of HBQ entries successfully allocated.
 **/
static int
lpfc_sli_hbqbuf_init_hbqs(struct lpfc_hba *phba, uint32_t qno)
{
	return(lpfc_sli_hbqbuf_fill_hbqs(phba, qno,
					 lpfc_hbq_defs[qno]->init_count));
}

/**
 * lpfc_sli_hbqbuf_find: Find the hbq buffer associated with a tag.
 * @phba: Pointer to HBA context object.
 * @tag: Tag of the hbq buffer.
 *
 * This function is called with hbalock held. This function searches
 * for the hbq buffer associated with the given tag in the hbq buffer
 * list. If it finds the hbq buffer, it returns the hbq_buffer other wise
 * it returns NULL.
 **/
static struct hbq_dmabuf *
lpfc_sli_hbqbuf_find(struct lpfc_hba *phba, uint32_t tag)
{
	struct lpfc_dmabuf *d_buf;
	struct hbq_dmabuf *hbq_buf;
	uint32_t hbqno;

	hbqno = tag >> 16;
	if (hbqno >= LPFC_MAX_HBQS)
		return NULL;

	list_for_each_entry(d_buf, &phba->hbqs[hbqno].hbq_buffer_list, list) {
		hbq_buf = container_of(d_buf, struct hbq_dmabuf, dbuf);
		if (hbq_buf->tag == tag) {
			return hbq_buf;
		}
	}
	lpfc_printf_log(phba, KERN_ERR, LOG_SLI | LOG_VPORT,
			"1803 Bad hbq tag. Data: x%x x%x\n",
			tag, phba->hbqs[tag >> 16].buffer_count);
	return NULL;
}

/**
 * lpfc_sli_free_hbq: Give back the hbq buffer to firmware.
 * @phba: Pointer to HBA context object.
 * @hbq_buffer: Pointer to HBQ buffer.
 *
 * This function is called with hbalock. This function gives back
 * the hbq buffer to firmware. If the HBQ does not have space to
 * post the buffer, it will free the buffer.
 **/
void
lpfc_sli_free_hbq(struct lpfc_hba *phba, struct hbq_dmabuf *hbq_buffer)
{
	uint32_t hbqno;

	if (hbq_buffer) {
		hbqno = hbq_buffer->tag >> 16;
		if (!lpfc_sli_hbq_to_firmware(phba, hbqno, hbq_buffer)) {
			(phba->hbqs[hbqno].hbq_free_buffer)(phba, hbq_buffer);
		}
	}
}

/**
 * lpfc_sli_chk_mbx_command: Check if the mailbox is a legitimate mailbox.
 * @mbxCommand: mailbox command code.
 *
 * This function is called by the mailbox event handler function to verify
 * that the completed mailbox command is a legitimate mailbox command. If the
 * completed mailbox is not known to the function, it will return MBX_SHUTDOWN
 * and the mailbox event handler will take the HBA offline.
 **/
static int
lpfc_sli_chk_mbx_command(uint8_t mbxCommand)
{
	uint8_t ret;

	switch (mbxCommand) {
	case MBX_LOAD_SM:
	case MBX_READ_NV:
	case MBX_WRITE_NV:
	case MBX_WRITE_VPARMS:
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
	case MBX_SET_VARIABLE:
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
	case MBX_WRITE_WWN:
	case MBX_SET_DEBUG:
	case MBX_LOAD_EXP_ROM:
	case MBX_ASYNCEVT_ENABLE:
	case MBX_REG_VPI:
	case MBX_UNREG_VPI:
	case MBX_HEARTBEAT:
	case MBX_PORT_CAPABILITIES:
	case MBX_PORT_IOV_CONTROL:
		ret = mbxCommand;
		break;
	default:
		ret = MBX_SHUTDOWN;
		break;
	}
	return ret;
}

/**
 * lpfc_sli_wake_mbox_wait: Completion handler for mbox issued from
 *          lpfc_sli_issue_mbox_wait.
 * @phba: Pointer to HBA context object.
 * @pmboxq: Pointer to mailbox command.
 *
 * This is completion handler function for mailbox commands issued from
 * lpfc_sli_issue_mbox_wait function. This function is called by the
 * mailbox event handler function with no lock held. This function
 * will wake up thread waiting on the wait queue pointed by context1
 * of the mailbox.
 **/
static void
lpfc_sli_wake_mbox_wait(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmboxq)
{
	wait_queue_head_t *pdone_q;
	unsigned long drvr_flag;

	/*
	 * If pdone_q is empty, the driver thread gave up waiting and
	 * continued running.
	 */
	pmboxq->mbox_flag |= LPFC_MBX_WAKE;
	spin_lock_irqsave(&phba->hbalock, drvr_flag);
	pdone_q = (wait_queue_head_t *) pmboxq->context1;
	if (pdone_q)
		wake_up_interruptible(pdone_q);
	spin_unlock_irqrestore(&phba->hbalock, drvr_flag);
	return;
}


/**
 * lpfc_sli_def_mbox_cmpl: Default mailbox completion handler.
 * @phba: Pointer to HBA context object.
 * @pmb: Pointer to mailbox object.
 *
 * This function is the default mailbox completion handler. It
 * frees the memory resources associated with the completed mailbox
 * command. If the completed command is a REG_LOGIN mailbox command,
 * this function will issue a UREG_LOGIN to re-claim the RPI.
 **/
void
lpfc_sli_def_mbox_cmpl(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb)
{
	struct lpfc_dmabuf *mp;
	uint16_t rpi;
	int rc;

	mp = (struct lpfc_dmabuf *) (pmb->context1);

	if (mp) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
	}

	/*
	 * If a REG_LOGIN succeeded  after node is destroyed or node
	 * is in re-discovery driver need to cleanup the RPI.
	 */
	if (!(phba->pport->load_flag & FC_UNLOADING) &&
	    pmb->mb.mbxCommand == MBX_REG_LOGIN64 &&
	    !pmb->mb.mbxStatus) {

		rpi = pmb->mb.un.varWords[0];
		lpfc_unreg_login(phba, pmb->mb.un.varRegLogin.vpi, rpi, pmb);
		pmb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		rc = lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT);
		if (rc != MBX_NOT_FINISHED)
			return;
	}

	mempool_free(pmb, phba->mbox_mem_pool);
	return;
}

/**
 * lpfc_sli_handle_mb_event: Handle mailbox completions from firmware.
 * @phba: Pointer to HBA context object.
 *
 * This function is called with no lock held. This function processes all
 * the completed mailbox commands and gives it to upper layers. The interrupt
 * service routine processes mailbox completion interrupt and adds completed
 * mailbox commands to the mboxq_cmpl queue and signals the worker thread.
 * Worker thread call lpfc_sli_handle_mb_event, which will return the
 * completed mailbox commands in mboxq_cmpl queue to the upper layers. This
 * function returns the mailbox commands to the upper layer by calling the
 * completion handler function of each mailbox.
 **/
int
lpfc_sli_handle_mb_event(struct lpfc_hba *phba)
{
	MAILBOX_t *pmbox;
	LPFC_MBOXQ_t *pmb;
	int rc;
	LIST_HEAD(cmplq);

	phba->sli.slistat.mbox_event++;

	/* Get all completed mailboxe buffers into the cmplq */
	spin_lock_irq(&phba->hbalock);
	list_splice_init(&phba->sli.mboxq_cmpl, &cmplq);
	spin_unlock_irq(&phba->hbalock);

	/* Get a Mailbox buffer to setup mailbox commands for callback */
	do {
		list_remove_head(&cmplq, pmb, LPFC_MBOXQ_t, list);
		if (pmb == NULL)
			break;

		pmbox = &pmb->mb;

		if (pmbox->mbxCommand != MBX_HEARTBEAT) {
			if (pmb->vport) {
				lpfc_debugfs_disc_trc(pmb->vport,
					LPFC_DISC_TRC_MBOX_VPORT,
					"MBOX cmpl vport: cmd:x%x mb:x%x x%x",
					(uint32_t)pmbox->mbxCommand,
					pmbox->un.varWords[0],
					pmbox->un.varWords[1]);
			}
			else {
				lpfc_debugfs_disc_trc(phba->pport,
					LPFC_DISC_TRC_MBOX,
					"MBOX cmpl:       cmd:x%x mb:x%x x%x",
					(uint32_t)pmbox->mbxCommand,
					pmbox->un.varWords[0],
					pmbox->un.varWords[1]);
			}
		}

		/*
		 * It is a fatal error if unknown mbox command completion.
		 */
		if (lpfc_sli_chk_mbx_command(pmbox->mbxCommand) ==
		    MBX_SHUTDOWN) {
			/* Unknow mailbox command compl */
			lpfc_printf_log(phba, KERN_ERR, LOG_MBOX | LOG_SLI,
					"(%d):0323 Unknown Mailbox command "
					"%x Cmpl\n",
					pmb->vport ? pmb->vport->vpi : 0,
					pmbox->mbxCommand);
			phba->link_state = LPFC_HBA_ERROR;
			phba->work_hs = HS_FFER3;
			lpfc_handle_eratt(phba);
			continue;
		}

		if (pmbox->mbxStatus) {
			phba->sli.slistat.mbox_stat_err++;
			if (pmbox->mbxStatus == MBXERR_NO_RESOURCES) {
				/* Mbox cmd cmpl error - RETRYing */
				lpfc_printf_log(phba, KERN_INFO,
						LOG_MBOX | LOG_SLI,
						"(%d):0305 Mbox cmd cmpl "
						"error - RETRYing Data: x%x "
						"x%x x%x x%x\n",
						pmb->vport ? pmb->vport->vpi :0,
						pmbox->mbxCommand,
						pmbox->mbxStatus,
						pmbox->un.varWords[0],
						pmb->vport->port_state);
				pmbox->mbxStatus = 0;
				pmbox->mbxOwner = OWN_HOST;
				spin_lock_irq(&phba->hbalock);
				phba->sli.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
				spin_unlock_irq(&phba->hbalock);
				rc = lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT);
				if (rc == MBX_SUCCESS)
					continue;
			}
		}

		/* Mailbox cmd <cmd> Cmpl <cmpl> */
		lpfc_printf_log(phba, KERN_INFO, LOG_MBOX | LOG_SLI,
				"(%d):0307 Mailbox cmd x%x Cmpl x%p "
				"Data: x%x x%x x%x x%x x%x x%x x%x x%x x%x\n",
				pmb->vport ? pmb->vport->vpi : 0,
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

		if (pmb->mbox_cmpl)
			pmb->mbox_cmpl(phba,pmb);
	} while (1);
	return 0;
}

/**
 * lpfc_sli_get_buff: Get the buffer associated with the buffer tag.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @tag: buffer tag.
 *
 * This function is called with no lock held. When QUE_BUFTAG_BIT bit
 * is set in the tag the buffer is posted for a particular exchange,
 * the function will return the buffer without replacing the buffer.
 * If the buffer is for unsolicited ELS or CT traffic, this function
 * returns the buffer and also posts another buffer to the firmware.
 **/
static struct lpfc_dmabuf *
lpfc_sli_get_buff(struct lpfc_hba *phba,
		  struct lpfc_sli_ring *pring,
		  uint32_t tag)
{
	struct hbq_dmabuf *hbq_entry;

	if (tag & QUE_BUFTAG_BIT)
		return lpfc_sli_ring_taggedbuf_get(phba, pring, tag);
	hbq_entry = lpfc_sli_hbqbuf_find(phba, tag);
	if (!hbq_entry)
		return NULL;
	return &hbq_entry->dbuf;
}


/**
 * lpfc_sli_process_unsol_iocb: Unsolicited iocb handler.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @saveq: Pointer to the unsolicited iocb.
 *
 * This function is called with no lock held by the ring event handler
 * when there is an unsolicited iocb posted to the response ring by the
 * firmware. This function gets the buffer associated with the iocbs
 * and calls the event handler for the ring. This function handles both
 * qring buffers and hbq buffers.
 * When the function returns 1 the caller can free the iocb object otherwise
 * upper layer functions will free the iocb objects.
 **/
static int
lpfc_sli_process_unsol_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
			    struct lpfc_iocbq *saveq)
{
	IOCB_t           * irsp;
	WORD5            * w5p;
	uint32_t           Rctl, Type;
	uint32_t           match, i;
	struct lpfc_iocbq *iocbq;
	struct lpfc_dmabuf *dmzbuf;

	match = 0;
	irsp = &(saveq->iocb);

	if (irsp->ulpCommand == CMD_ASYNC_STATUS) {
		if (pring->lpfc_sli_rcv_async_status)
			pring->lpfc_sli_rcv_async_status(phba, pring, saveq);
		else
			lpfc_printf_log(phba,
					KERN_WARNING,
					LOG_SLI,
					"0316 Ring %d handler: unexpected "
					"ASYNC_STATUS iocb received evt_code "
					"0x%x\n",
					pring->ringno,
					irsp->un.asyncstat.evt_code);
		return 1;
	}

	if ((irsp->ulpCommand == CMD_IOCB_RET_XRI64_CX) &&
		(phba->sli3_options & LPFC_SLI3_HBQ_ENABLED)) {
		if (irsp->ulpBdeCount > 0) {
			dmzbuf = lpfc_sli_get_buff(phba, pring,
					irsp->un.ulpWord[3]);
			lpfc_in_buf_free(phba, dmzbuf);
		}

		if (irsp->ulpBdeCount > 1) {
			dmzbuf = lpfc_sli_get_buff(phba, pring,
					irsp->unsli3.sli3Words[3]);
			lpfc_in_buf_free(phba, dmzbuf);
		}

		if (irsp->ulpBdeCount > 2) {
			dmzbuf = lpfc_sli_get_buff(phba, pring,
				irsp->unsli3.sli3Words[7]);
			lpfc_in_buf_free(phba, dmzbuf);
		}

		return 1;
	}

	if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) {
		if (irsp->ulpBdeCount != 0) {
			saveq->context2 = lpfc_sli_get_buff(phba, pring,
						irsp->un.ulpWord[3]);
			if (!saveq->context2)
				lpfc_printf_log(phba,
					KERN_ERR,
					LOG_SLI,
					"0341 Ring %d Cannot find buffer for "
					"an unsolicited iocb. tag 0x%x\n",
					pring->ringno,
					irsp->un.ulpWord[3]);
		}
		if (irsp->ulpBdeCount == 2) {
			saveq->context3 = lpfc_sli_get_buff(phba, pring,
						irsp->unsli3.sli3Words[7]);
			if (!saveq->context3)
				lpfc_printf_log(phba,
					KERN_ERR,
					LOG_SLI,
					"0342 Ring %d Cannot find buffer for an"
					" unsolicited iocb. tag 0x%x\n",
					pring->ringno,
					irsp->unsli3.sli3Words[7]);
		}
		list_for_each_entry(iocbq, &saveq->list, list) {
			irsp = &(iocbq->iocb);
			if (irsp->ulpBdeCount != 0) {
				iocbq->context2 = lpfc_sli_get_buff(phba, pring,
							irsp->un.ulpWord[3]);
				if (!iocbq->context2)
					lpfc_printf_log(phba,
						KERN_ERR,
						LOG_SLI,
						"0343 Ring %d Cannot find "
						"buffer for an unsolicited iocb"
						". tag 0x%x\n", pring->ringno,
						irsp->un.ulpWord[3]);
			}
			if (irsp->ulpBdeCount == 2) {
				iocbq->context3 = lpfc_sli_get_buff(phba, pring,
						irsp->unsli3.sli3Words[7]);
				if (!iocbq->context3)
					lpfc_printf_log(phba,
						KERN_ERR,
						LOG_SLI,
						"0344 Ring %d Cannot find "
						"buffer for an unsolicited "
						"iocb. tag 0x%x\n",
						pring->ringno,
						irsp->unsli3.sli3Words[7]);
			}
		}
	}
	if (irsp->ulpBdeCount != 0 &&
	    (irsp->ulpCommand == CMD_IOCB_RCV_CONT64_CX ||
	     irsp->ulpStatus == IOSTAT_INTERMED_RSP)) {
		int found = 0;

		/* search continue save q for same XRI */
		list_for_each_entry(iocbq, &pring->iocb_continue_saveq, clist) {
			if (iocbq->iocb.ulpContext == saveq->iocb.ulpContext) {
				list_add_tail(&saveq->list, &iocbq->list);
				found = 1;
				break;
			}
		}
		if (!found)
			list_add_tail(&saveq->clist,
				      &pring->iocb_continue_saveq);
		if (saveq->iocb.ulpStatus != IOSTAT_INTERMED_RSP) {
			list_del_init(&iocbq->clist);
			saveq = iocbq;
			irsp = &(saveq->iocb);
		} else
			return 0;
	}
	if ((irsp->ulpCommand == CMD_RCV_ELS_REQ64_CX) ||
	    (irsp->ulpCommand == CMD_RCV_ELS_REQ_CX) ||
	    (irsp->ulpCommand == CMD_IOCB_RCV_ELS64_CX)) {
		Rctl = FC_ELS_REQ;
		Type = FC_ELS_DATA;
	} else {
		w5p = (WORD5 *)&(saveq->iocb.un.ulpWord[5]);
		Rctl = w5p->hcsw.Rctl;
		Type = w5p->hcsw.Type;

		/* Firmware Workaround */
		if ((Rctl == 0) && (pring->ringno == LPFC_ELS_RING) &&
			(irsp->ulpCommand == CMD_RCV_SEQUENCE64_CX ||
			 irsp->ulpCommand == CMD_IOCB_RCV_SEQ64_CX)) {
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
		for (i = 0; i < pring->num_mask; i++) {
			if ((pring->prt[i].rctl == Rctl)
			    && (pring->prt[i].type == Type)) {
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
		lpfc_printf_log(phba, KERN_WARNING, LOG_SLI,
				"0313 Ring %d handler: unexpected Rctl x%x "
				"Type x%x received\n",
				pring->ringno, Rctl, Type);
	}
	return 1;
}

/**
 * lpfc_sli_iocbq_lookup: Find command iocb for the given response iocb.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @prspiocb: Pointer to response iocb object.
 *
 * This function looks up the iocb_lookup table to get the command iocb
 * corresponding to the given response iocb using the iotag of the
 * response iocb. This function is called with the hbalock held.
 * This function returns the command iocb object if it finds the command
 * iocb else returns NULL.
 **/
static struct lpfc_iocbq *
lpfc_sli_iocbq_lookup(struct lpfc_hba *phba,
		      struct lpfc_sli_ring *pring,
		      struct lpfc_iocbq *prspiocb)
{
	struct lpfc_iocbq *cmd_iocb = NULL;
	uint16_t iotag;

	iotag = prspiocb->iocb.ulpIoTag;

	if (iotag != 0 && iotag <= phba->sli.last_iotag) {
		cmd_iocb = phba->sli.iocbq_lookup[iotag];
		list_del_init(&cmd_iocb->list);
		pring->txcmplq_cnt--;
		return cmd_iocb;
	}

	lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
			"0317 iotag x%x is out off "
			"range: max iotag x%x wd0 x%x\n",
			iotag, phba->sli.last_iotag,
			*(((uint32_t *) &prspiocb->iocb) + 7));
	return NULL;
}

/**
 * lpfc_sli_process_sol_iocb: process solicited iocb completion.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @saveq: Pointer to the response iocb to be processed.
 *
 * This function is called by the ring event handler for non-fcp
 * rings when there is a new response iocb in the response ring.
 * The caller is not required to hold any locks. This function
 * gets the command iocb associated with the response iocb and
 * calls the completion handler for the command iocb. If there
 * is no completion handler, the function will free the resources
 * associated with command iocb. If the response iocb is for
 * an already aborted command iocb, the status of the completion
 * is changed to IOSTAT_LOCAL_REJECT/IOERR_SLI_ABORTED.
 * This function always returns 1.
 **/
static int
lpfc_sli_process_sol_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
			  struct lpfc_iocbq *saveq)
{
	struct lpfc_iocbq *cmdiocbp;
	int rc = 1;
	unsigned long iflag;

	/* Based on the iotag field, get the cmd IOCB from the txcmplq */
	spin_lock_irqsave(&phba->hbalock, iflag);
	cmdiocbp = lpfc_sli_iocbq_lookup(phba, pring, saveq);
	spin_unlock_irqrestore(&phba->hbalock, iflag);

	if (cmdiocbp) {
		if (cmdiocbp->iocb_cmpl) {
			/*
			 * If an ELS command failed send an event to mgmt
			 * application.
			 */
			if (saveq->iocb.ulpStatus &&
			     (pring->ringno == LPFC_ELS_RING) &&
			     (cmdiocbp->iocb.ulpCommand ==
				CMD_ELS_REQUEST64_CR))
				lpfc_send_els_failure_event(phba,
					cmdiocbp, saveq);

			/*
			 * Post all ELS completions to the worker thread.
			 * All other are passed to the completion callback.
			 */
			if (pring->ringno == LPFC_ELS_RING) {
				if (cmdiocbp->iocb_flag & LPFC_DRIVER_ABORTED) {
					cmdiocbp->iocb_flag &=
						~LPFC_DRIVER_ABORTED;
					saveq->iocb.ulpStatus =
						IOSTAT_LOCAL_REJECT;
					saveq->iocb.un.ulpWord[4] =
						IOERR_SLI_ABORTED;

					/* Firmware could still be in progress
					 * of DMAing payload, so don't free data
					 * buffer till after a hbeat.
					 */
					saveq->iocb_flag |= LPFC_DELAY_MEM_FREE;
				}
			}
			(cmdiocbp->iocb_cmpl) (phba, cmdiocbp, saveq);
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
			lpfc_printf_vlog(cmdiocbp->vport, KERN_WARNING, LOG_SLI,
					 "0322 Ring %d handler: "
					 "unexpected completion IoTag x%x "
					 "Data: x%x x%x x%x x%x\n",
					 pring->ringno,
					 saveq->iocb.ulpIoTag,
					 saveq->iocb.ulpStatus,
					 saveq->iocb.un.ulpWord[4],
					 saveq->iocb.ulpCommand,
					 saveq->iocb.ulpContext);
		}
	}

	return rc;
}

/**
 * lpfc_sli_rsp_pointers_error: Response ring pointer error handler.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 *
 * This function is called from the iocb ring event handlers when
 * put pointer is ahead of the get pointer for a ring. This function signal
 * an error attention condition to the worker thread and the worker
 * thread will transition the HBA to offline state.
 **/
static void
lpfc_sli_rsp_pointers_error(struct lpfc_hba *phba, struct lpfc_sli_ring *pring)
{
	struct lpfc_pgp *pgp = &phba->port_gp[pring->ringno];
	/*
	 * Ring <ringno> handler: portRspPut <portRspPut> is bigger than
	 * rsp ring <portRspMax>
	 */
	lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
			"0312 Ring %d handler: portRspPut %d "
			"is bigger than rsp ring %d\n",
			pring->ringno, le32_to_cpu(pgp->rspPutInx),
			pring->numRiocb);

	phba->link_state = LPFC_HBA_ERROR;

	/*
	 * All error attention handlers are posted to
	 * worker thread
	 */
	phba->work_ha |= HA_ERATT;
	phba->work_hs = HS_FFER3;

	lpfc_worker_wake_up(phba);

	return;
}

/**
 * lpfc_poll_eratt: Error attention polling timer timeout handler.
 * @ptr: Pointer to address of HBA context object.
 *
 * This function is invoked by the Error Attention polling timer when the
 * timer times out. It will check the SLI Error Attention register for
 * possible attention events. If so, it will post an Error Attention event
 * and wake up worker thread to process it. Otherwise, it will set up the
 * Error Attention polling timer for the next poll.
 **/
void lpfc_poll_eratt(unsigned long ptr)
{
	struct lpfc_hba *phba;
	uint32_t eratt = 0;

	phba = (struct lpfc_hba *)ptr;

	/* Check chip HA register for error event */
	eratt = lpfc_sli_check_eratt(phba);

	if (eratt)
		/* Tell the worker thread there is work to do */
		lpfc_worker_wake_up(phba);
	else
		/* Restart the timer for next eratt poll */
		mod_timer(&phba->eratt_poll, jiffies +
					HZ * LPFC_ERATT_POLL_INTERVAL);
	return;
}

/**
 * lpfc_sli_poll_fcp_ring: Handle FCP ring completion in polling mode.
 * @phba: Pointer to HBA context object.
 *
 * This function is called from lpfc_queuecommand, lpfc_poll_timeout,
 * lpfc_abort_handler and lpfc_slave_configure when FCP_RING_POLLING
 * is enabled.
 *
 * The caller does not hold any lock.
 * The function processes each response iocb in the response ring until it
 * finds an iocb with LE bit set and chains all the iocbs upto the iocb with
 * LE bit set. The function will call the completion handler of the command iocb
 * if the response iocb indicates a completion for a command iocb or it is
 * an abort completion.
 **/
void lpfc_sli_poll_fcp_ring(struct lpfc_hba *phba)
{
	struct lpfc_sli      *psli  = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_FCP_RING];
	IOCB_t *irsp = NULL;
	IOCB_t *entry = NULL;
	struct lpfc_iocbq *cmdiocbq = NULL;
	struct lpfc_iocbq rspiocbq;
	struct lpfc_pgp *pgp = &phba->port_gp[pring->ringno];
	uint32_t status;
	uint32_t portRspPut, portRspMax;
	int type;
	uint32_t rsp_cmpl = 0;
	uint32_t ha_copy;
	unsigned long iflags;

	pring->stats.iocb_event++;

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
		entry = lpfc_resp_iocb(phba, pring);
		if (++pring->rspidx >= portRspMax)
			pring->rspidx = 0;

		lpfc_sli_pcimem_bcopy((uint32_t *) entry,
				      (uint32_t *) &rspiocbq.iocb,
				      phba->iocb_rsp_size);
		irsp = &rspiocbq.iocb;
		type = lpfc_sli_iocb_cmd_type(irsp->ulpCommand & CMD_IOCB_MASK);
		pring->stats.iocb_rsp++;
		rsp_cmpl++;

		if (unlikely(irsp->ulpStatus)) {
			/* Rsp ring <ringno> error: IOCB */
			lpfc_printf_log(phba, KERN_WARNING, LOG_SLI,
					"0326 Rsp Ring %d error: IOCB Data: "
					"x%x x%x x%x x%x x%x x%x x%x x%x\n",
					pring->ringno,
					irsp->un.ulpWord[0],
					irsp->un.ulpWord[1],
					irsp->un.ulpWord[2],
					irsp->un.ulpWord[3],
					irsp->un.ulpWord[4],
					irsp->un.ulpWord[5],
					*(uint32_t *)&irsp->un1,
					*((uint32_t *)&irsp->un1 + 1));
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
						"0314 IOCB cmd 0x%x "
						"processed. Skipping "
						"completion",
						irsp->ulpCommand);
				break;
			}

			spin_lock_irqsave(&phba->hbalock, iflags);
			cmdiocbq = lpfc_sli_iocbq_lookup(phba, pring,
							 &rspiocbq);
			spin_unlock_irqrestore(&phba->hbalock, iflags);
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
				dev_warn(&((phba->pcidev)->dev),
					 "lpfc%d: %s\n",
					 phba->brd_no, adaptermsg);
			} else {
				/* Unknown IOCB command */
				lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
						"0321 Unknown IOCB command "
						"Data: x%x, x%x x%x x%x x%x\n",
						type, irsp->ulpCommand,
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
		writel(pring->rspidx, &phba->host_gp[pring->ringno].rspGetInx);

		if (pring->rspidx == portRspPut)
			portRspPut = le32_to_cpu(pgp->rspPutInx);
	}

	ha_copy = readl(phba->HAregaddr);
	ha_copy >>= (LPFC_FCP_RING * 4);

	if ((rsp_cmpl > 0) && (ha_copy & HA_R0RE_REQ)) {
		spin_lock_irqsave(&phba->hbalock, iflags);
		pring->stats.iocb_rsp_full++;
		status = ((CA_R0ATT | CA_R0RE_RSP) << (LPFC_FCP_RING * 4));
		writel(status, phba->CAregaddr);
		readl(phba->CAregaddr);
		spin_unlock_irqrestore(&phba->hbalock, iflags);
	}
	if ((ha_copy & HA_R0CE_RSP) &&
	    (pring->flag & LPFC_CALL_RING_AVAILABLE)) {
		spin_lock_irqsave(&phba->hbalock, iflags);
		pring->flag &= ~LPFC_CALL_RING_AVAILABLE;
		pring->stats.iocb_cmd_empty++;

		/* Force update of the local copy of cmdGetInx */
		pring->local_getidx = le32_to_cpu(pgp->cmdGetInx);
		lpfc_sli_resume_iocb(phba, pring);

		if ((pring->lpfc_sli_cmd_available))
			(pring->lpfc_sli_cmd_available) (phba, pring);

		spin_unlock_irqrestore(&phba->hbalock, iflags);
	}

	return;
}

/**
 * lpfc_sli_handle_fast_ring_event: Handle ring events on FCP ring.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @mask: Host attention register mask for this ring.
 *
 * This function is called from the interrupt context when there is a ring
 * event for the fcp ring. The caller does not hold any lock.
 * The function processes each response iocb in the response ring until it
 * finds an iocb with LE bit set and chains all the iocbs upto the iocb with
 * LE bit set. The function will call the completion handler of the command iocb
 * if the response iocb indicates a completion for a command iocb or it is
 * an abort completion. The function will call lpfc_sli_process_unsol_iocb
 * function if this is an unsolicited iocb.
 * This routine presumes LPFC_FCP_RING handling and doesn't bother
 * to check it explicitly. This function always returns 1.
 **/
static int
lpfc_sli_handle_fast_ring_event(struct lpfc_hba *phba,
				struct lpfc_sli_ring *pring, uint32_t mask)
{
	struct lpfc_pgp *pgp = &phba->port_gp[pring->ringno];
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

	spin_lock_irqsave(&phba->hbalock, iflag);
	pring->stats.iocb_event++;

	/*
	 * The next available response entry should never exceed the maximum
	 * entries.  If it does, treat it as an adapter hardware error.
	 */
	portRspMax = pring->numRiocb;
	portRspPut = le32_to_cpu(pgp->rspPutInx);
	if (unlikely(portRspPut >= portRspMax)) {
		lpfc_sli_rsp_pointers_error(phba, pring);
		spin_unlock_irqrestore(&phba->hbalock, iflag);
		return 1;
	}

	rmb();
	while (pring->rspidx != portRspPut) {
		/*
		 * Fetch an entry off the ring and copy it into a local data
		 * structure.  The copy involves a byte-swap since the
		 * network byte order and pci byte orders are different.
		 */
		entry = lpfc_resp_iocb(phba, pring);
		phba->last_completion_time = jiffies;

		if (++pring->rspidx >= portRspMax)
			pring->rspidx = 0;

		lpfc_sli_pcimem_bcopy((uint32_t *) entry,
				      (uint32_t *) &rspiocbq.iocb,
				      phba->iocb_rsp_size);
		INIT_LIST_HEAD(&(rspiocbq.list));
		irsp = &rspiocbq.iocb;

		type = lpfc_sli_iocb_cmd_type(irsp->ulpCommand & CMD_IOCB_MASK);
		pring->stats.iocb_rsp++;
		rsp_cmpl++;

		if (unlikely(irsp->ulpStatus)) {
			/*
			 * If resource errors reported from HBA, reduce
			 * queuedepths of the SCSI device.
			 */
			if ((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
				(irsp->un.ulpWord[4] == IOERR_NO_RESOURCES)) {
				spin_unlock_irqrestore(&phba->hbalock, iflag);
				lpfc_rampdown_queue_depth(phba);
				spin_lock_irqsave(&phba->hbalock, iflag);
			}

			/* Rsp ring <ringno> error: IOCB */
			lpfc_printf_log(phba, KERN_WARNING, LOG_SLI,
					"0336 Rsp Ring %d error: IOCB Data: "
					"x%x x%x x%x x%x x%x x%x x%x x%x\n",
					pring->ringno,
					irsp->un.ulpWord[0],
					irsp->un.ulpWord[1],
					irsp->un.ulpWord[2],
					irsp->un.ulpWord[3],
					irsp->un.ulpWord[4],
					irsp->un.ulpWord[5],
					*(uint32_t *)&irsp->un1,
					*((uint32_t *)&irsp->un1 + 1));
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
						"0333 IOCB cmd 0x%x"
						" processed. Skipping"
						" completion\n",
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
					spin_unlock_irqrestore(&phba->hbalock,
							       iflag);
					(cmdiocbq->iocb_cmpl)(phba, cmdiocbq,
							      &rspiocbq);
					spin_lock_irqsave(&phba->hbalock,
							  iflag);
				}
			}
			break;
		case LPFC_UNSOL_IOCB:
			spin_unlock_irqrestore(&phba->hbalock, iflag);
			lpfc_sli_process_unsol_iocb(phba, pring, &rspiocbq);
			spin_lock_irqsave(&phba->hbalock, iflag);
			break;
		default:
			if (irsp->ulpCommand == CMD_ADAPTER_MSG) {
				char adaptermsg[LPFC_MAX_ADPTMSG];
				memset(adaptermsg, 0, LPFC_MAX_ADPTMSG);
				memcpy(&adaptermsg[0], (uint8_t *) irsp,
				       MAX_MSG_DATA);
				dev_warn(&((phba->pcidev)->dev),
					 "lpfc%d: %s\n",
					 phba->brd_no, adaptermsg);
			} else {
				/* Unknown IOCB command */
				lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
						"0334 Unknown IOCB command "
						"Data: x%x, x%x x%x x%x x%x\n",
						type, irsp->ulpCommand,
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
		writel(pring->rspidx, &phba->host_gp[pring->ringno].rspGetInx);

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

	spin_unlock_irqrestore(&phba->hbalock, iflag);
	return rc;
}

/**
 * lpfc_sli_handle_slow_ring_event: Handle ring events for non-FCP rings.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @mask: Host attention register mask for this ring.
 *
 * This function is called from the worker thread when there is a ring
 * event for non-fcp rings. The caller does not hold any lock .
 * The function processes each response iocb in the response ring until it
 * finds an iocb with LE bit set and chains all the iocbs upto the iocb with
 * LE bit set. The function will call lpfc_sli_process_sol_iocb function if the
 * response iocb indicates a completion of a command iocb. The function
 * will call lpfc_sli_process_unsol_iocb function if this is an unsolicited
 * iocb. The function frees the resources or calls the completion handler if
 * this iocb is an abort completion. The function returns 0 when the allocated
 * iocbs are not freed, otherwise returns 1.
 **/
int
lpfc_sli_handle_slow_ring_event(struct lpfc_hba *phba,
				struct lpfc_sli_ring *pring, uint32_t mask)
{
	struct lpfc_pgp *pgp;
	IOCB_t *entry;
	IOCB_t *irsp = NULL;
	struct lpfc_iocbq *rspiocbp = NULL;
	struct lpfc_iocbq *next_iocb;
	struct lpfc_iocbq *cmdiocbp;
	struct lpfc_iocbq *saveq;
	uint8_t iocb_cmd_type;
	lpfc_iocb_type type;
	uint32_t status, free_saveq;
	uint32_t portRspPut, portRspMax;
	int rc = 1;
	unsigned long iflag;

	pgp = &phba->port_gp[pring->ringno];
	spin_lock_irqsave(&phba->hbalock, iflag);
	pring->stats.iocb_event++;

	/*
	 * The next available response entry should never exceed the maximum
	 * entries.  If it does, treat it as an adapter hardware error.
	 */
	portRspMax = pring->numRiocb;
	portRspPut = le32_to_cpu(pgp->rspPutInx);
	if (portRspPut >= portRspMax) {
		/*
		 * Ring <ringno> handler: portRspPut <portRspPut> is bigger than
		 * rsp ring <portRspMax>
		 */
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"0303 Ring %d handler: portRspPut %d "
				"is bigger than rsp ring %d\n",
				pring->ringno, portRspPut, portRspMax);

		phba->link_state = LPFC_HBA_ERROR;
		spin_unlock_irqrestore(&phba->hbalock, iflag);

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
		entry = lpfc_resp_iocb(phba, pring);

		phba->last_completion_time = jiffies;
		rspiocbp = __lpfc_sli_get_iocbq(phba);
		if (rspiocbp == NULL) {
			printk(KERN_ERR "%s: out of buffers! Failing "
			       "completion.\n", __func__);
			break;
		}

		lpfc_sli_pcimem_bcopy(entry, &rspiocbp->iocb,
				      phba->iocb_rsp_size);
		irsp = &rspiocbp->iocb;

		if (++pring->rspidx >= portRspMax)
			pring->rspidx = 0;

		if (pring->ringno == LPFC_ELS_RING) {
			lpfc_debugfs_slow_ring_trc(phba,
			"IOCB rsp ring:   wd4:x%08x wd6:x%08x wd7:x%08x",
				*(((uint32_t *) irsp) + 4),
				*(((uint32_t *) irsp) + 6),
				*(((uint32_t *) irsp) + 7));
		}

		writel(pring->rspidx, &phba->host_gp[pring->ringno].rspGetInx);

		list_add_tail(&rspiocbp->list, &(pring->iocb_continueq));

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

			/*
			 * If resource errors reported from HBA, reduce
			 * queuedepths of the SCSI device.
			 */
			if ((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
			     (irsp->un.ulpWord[4] == IOERR_NO_RESOURCES)) {
				spin_unlock_irqrestore(&phba->hbalock, iflag);
				lpfc_rampdown_queue_depth(phba);
				spin_lock_irqsave(&phba->hbalock, iflag);
			}

			if (irsp->ulpStatus) {
				/* Rsp ring <ringno> error: IOCB */
				lpfc_printf_log(phba, KERN_WARNING, LOG_SLI,
						"0328 Rsp Ring %d error: "
						"IOCB Data: "
						"x%x x%x x%x x%x "
						"x%x x%x x%x x%x "
						"x%x x%x x%x x%x "
						"x%x x%x x%x x%x\n",
						pring->ringno,
						irsp->un.ulpWord[0],
						irsp->un.ulpWord[1],
						irsp->un.ulpWord[2],
						irsp->un.ulpWord[3],
						irsp->un.ulpWord[4],
						irsp->un.ulpWord[5],
						*(((uint32_t *) irsp) + 6),
						*(((uint32_t *) irsp) + 7),
						*(((uint32_t *) irsp) + 8),
						*(((uint32_t *) irsp) + 9),
						*(((uint32_t *) irsp) + 10),
						*(((uint32_t *) irsp) + 11),
						*(((uint32_t *) irsp) + 12),
						*(((uint32_t *) irsp) + 13),
						*(((uint32_t *) irsp) + 14),
						*(((uint32_t *) irsp) + 15));
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
				spin_unlock_irqrestore(&phba->hbalock, iflag);
				rc = lpfc_sli_process_sol_iocb(phba, pring,
							       saveq);
				spin_lock_irqsave(&phba->hbalock, iflag);
			} else if (type == LPFC_UNSOL_IOCB) {
				spin_unlock_irqrestore(&phba->hbalock, iflag);
				rc = lpfc_sli_process_unsol_iocb(phba, pring,
								 saveq);
				spin_lock_irqsave(&phba->hbalock, iflag);
				if (!rc)
					free_saveq = 0;
			} else if (type == LPFC_ABORT_IOCB) {
				if ((irsp->ulpCommand != CMD_XRI_ABORTED_CX) &&
				    ((cmdiocbp =
				      lpfc_sli_iocbq_lookup(phba, pring,
							    saveq)))) {
					/* Call the specified completion
					   routine */
					if (cmdiocbp->iocb_cmpl) {
						spin_unlock_irqrestore(
						       &phba->hbalock,
						       iflag);
						(cmdiocbp->iocb_cmpl) (phba,
							     cmdiocbp, saveq);
						spin_lock_irqsave(
							  &phba->hbalock,
							  iflag);
					} else
						__lpfc_sli_release_iocbq(phba,
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
						 "lpfc%d: %s\n",
						 phba->brd_no, adaptermsg);
				} else {
					/* Unknown IOCB command */
					lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
							"0335 Unknown IOCB "
							"command Data: x%x "
							"x%x x%x x%x\n",
							irsp->ulpCommand,
							irsp->ulpStatus,
							irsp->ulpIoTag,
							irsp->ulpContext);
				}
			}

			if (free_saveq) {
				list_for_each_entry_safe(rspiocbp, next_iocb,
							 &saveq->list, list) {
					list_del(&rspiocbp->list);
					__lpfc_sli_release_iocbq(phba,
								 rspiocbp);
				}
				__lpfc_sli_release_iocbq(phba, saveq);
			}
			rspiocbp = NULL;
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

	if ((rspiocbp != NULL) && (mask & HA_R0RE_REQ)) {
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

	spin_unlock_irqrestore(&phba->hbalock, iflag);
	return rc;
}

/**
 * lpfc_sli_abort_iocb_ring: Abort all iocbs in the ring.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 *
 * This function aborts all iocbs in the given ring and frees all the iocb
 * objects in txq. This function issues an abort iocb for all the iocb commands
 * in txcmplq. The iocbs in the txcmplq is not guaranteed to complete before
 * the return of this function. The caller is not required to hold any locks.
 **/
void
lpfc_sli_abort_iocb_ring(struct lpfc_hba *phba, struct lpfc_sli_ring *pring)
{
	LIST_HEAD(completions);
	struct lpfc_iocbq *iocb, *next_iocb;
	IOCB_t *cmd = NULL;

	if (pring->ringno == LPFC_ELS_RING) {
		lpfc_fabric_abort_hba(phba);
	}

	/* Error everything on txq and txcmplq
	 * First do the txq.
	 */
	spin_lock_irq(&phba->hbalock);
	list_splice_init(&pring->txq, &completions);
	pring->txq_cnt = 0;

	/* Next issue ABTS for everything on the txcmplq */
	list_for_each_entry_safe(iocb, next_iocb, &pring->txcmplq, list)
		lpfc_sli_issue_abort_iotag(phba, pring, iocb);

	spin_unlock_irq(&phba->hbalock);

	while (!list_empty(&completions)) {
		iocb = list_get_first(&completions, struct lpfc_iocbq, list);
		cmd = &iocb->iocb;
		list_del_init(&iocb->list);

		if (!iocb->iocb_cmpl)
			lpfc_sli_release_iocbq(phba, iocb);
		else {
			cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			(iocb->iocb_cmpl) (phba, iocb, iocb);
		}
	}
}

/**
 * lpfc_sli_flush_fcp_rings: flush all iocbs in the fcp ring.
 * @phba: Pointer to HBA context object.
 *
 * This function flushes all iocbs in the fcp ring and frees all the iocb
 * objects in txq and txcmplq. This function will not issue abort iocbs
 * for all the iocb commands in txcmplq, they will just be returned with
 * IOERR_SLI_DOWN. This function is invoked with EEH when device's PCI
 * slot has been permanently disabled.
 **/
void
lpfc_sli_flush_fcp_rings(struct lpfc_hba *phba)
{
	LIST_HEAD(txq);
	LIST_HEAD(txcmplq);
	struct lpfc_iocbq *iocb;
	IOCB_t *cmd = NULL;
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring  *pring;

	/* Currently, only one fcp ring */
	pring = &psli->ring[psli->fcp_ring];

	spin_lock_irq(&phba->hbalock);
	/* Retrieve everything on txq */
	list_splice_init(&pring->txq, &txq);
	pring->txq_cnt = 0;

	/* Retrieve everything on the txcmplq */
	list_splice_init(&pring->txcmplq, &txcmplq);
	pring->txcmplq_cnt = 0;
	spin_unlock_irq(&phba->hbalock);

	/* Flush the txq */
	while (!list_empty(&txq)) {
		iocb = list_get_first(&txq, struct lpfc_iocbq, list);
		cmd = &iocb->iocb;
		list_del_init(&iocb->list);

		if (!iocb->iocb_cmpl)
			lpfc_sli_release_iocbq(phba, iocb);
		else {
			cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			cmd->un.ulpWord[4] = IOERR_SLI_DOWN;
			(iocb->iocb_cmpl) (phba, iocb, iocb);
		}
	}

	/* Flush the txcmpq */
	while (!list_empty(&txcmplq)) {
		iocb = list_get_first(&txcmplq, struct lpfc_iocbq, list);
		cmd = &iocb->iocb;
		list_del_init(&iocb->list);

		if (!iocb->iocb_cmpl)
			lpfc_sli_release_iocbq(phba, iocb);
		else {
			cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			cmd->un.ulpWord[4] = IOERR_SLI_DOWN;
			(iocb->iocb_cmpl) (phba, iocb, iocb);
		}
	}
}

/**
 * lpfc_sli_brdready: Check for host status bits.
 * @phba: Pointer to HBA context object.
 * @mask: Bit mask to be checked.
 *
 * This function reads the host status register and compares
 * with the provided bit mask to check if HBA completed
 * the restart. This function will wait in a loop for the
 * HBA to complete restart. If the HBA does not restart within
 * 15 iterations, the function will reset the HBA again. The
 * function returns 1 when HBA fail to restart otherwise returns
 * zero.
 **/
int
lpfc_sli_brdready(struct lpfc_hba *phba, uint32_t mask)
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
				/* Do post */
			phba->pport->port_state = LPFC_VPORT_UNKNOWN;
			lpfc_sli_brdrestart(phba);
		}
		/* Read the HBA Host Status Register */
		status = readl(phba->HSregaddr);
	}

	/* Check to see if any errors occurred during init */
	if ((status & HS_FFERM) || (i >= 20)) {
		phba->link_state = LPFC_HBA_ERROR;
		retval = 1;
	}

	return retval;
}

#define BARRIER_TEST_PATTERN (0xdeadbeef)

/**
 * lpfc_reset_barrier: Make HBA ready for HBA reset.
 * @phba: Pointer to HBA context object.
 *
 * This function is called before resetting an HBA. This
 * function requests HBA to quiesce DMAs before a reset.
 **/
void lpfc_reset_barrier(struct lpfc_hba *phba)
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
	phba->link_flag |= LS_IGNORE_ERATT;

	if (readl(phba->HAregaddr) & HA_ERATT) {
		/* Clear Chip error bit */
		writel(HA_ERATT, phba->HAregaddr);
		phba->pport->stopped = 1;
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
		    phba->pport->stopped)
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
		phba->pport->stopped = 1;
	}

restore_hc:
	phba->link_flag &= ~LS_IGNORE_ERATT;
	writel(hc_copy, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */
}

/**
 * lpfc_sli_brdkill: Issue a kill_board mailbox command.
 * @phba: Pointer to HBA context object.
 *
 * This function issues a kill_board mailbox command and waits for
 * the error attention interrupt. This function is called for stopping
 * the firmware processing. The caller is not required to hold any
 * locks. This function calls lpfc_hba_down_post function to free
 * any pending commands after the kill. The function will return 1 when it
 * fails to kill the board else will return 0.
 **/
int
lpfc_sli_brdkill(struct lpfc_hba *phba)
{
	struct lpfc_sli *psli;
	LPFC_MBOXQ_t *pmb;
	uint32_t status;
	uint32_t ha_copy;
	int retval;
	int i = 0;

	psli = &phba->sli;

	/* Kill HBA */
	lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
			"0329 Kill HBA Data: x%x x%x\n",
			phba->pport->port_state, psli->sli_flag);

	pmb = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb)
		return 1;

	/* Disable the error attention */
	spin_lock_irq(&phba->hbalock);
	status = readl(phba->HCregaddr);
	status &= ~HC_ERINT_ENA;
	writel(status, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */
	phba->link_flag |= LS_IGNORE_ERATT;
	spin_unlock_irq(&phba->hbalock);

	lpfc_kill_board(phba, pmb);
	pmb->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
	retval = lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT);

	if (retval != MBX_SUCCESS) {
		if (retval != MBX_BUSY)
			mempool_free(pmb, phba->mbox_mem_pool);
		spin_lock_irq(&phba->hbalock);
		phba->link_flag &= ~LS_IGNORE_ERATT;
		spin_unlock_irq(&phba->hbalock);
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
		phba->pport->stopped = 1;
	}
	spin_lock_irq(&phba->hbalock);
	psli->sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
	phba->link_flag &= ~LS_IGNORE_ERATT;
	spin_unlock_irq(&phba->hbalock);

	psli->mbox_active = NULL;
	lpfc_hba_down_post(phba);
	phba->link_state = LPFC_HBA_ERROR;

	return ha_copy & HA_ERATT ? 0 : 1;
}

/**
 * lpfc_sli_brdreset: Reset the HBA.
 * @phba: Pointer to HBA context object.
 *
 * This function resets the HBA by writing HC_INITFF to the control
 * register. After the HBA resets, this function resets all the iocb ring
 * indices. This function disables PCI layer parity checking during
 * the reset.
 * This function returns 0 always.
 * The caller is not required to hold any locks.
 **/
int
lpfc_sli_brdreset(struct lpfc_hba *phba)
{
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	uint16_t cfg_value;
	int i;

	psli = &phba->sli;

	/* Reset HBA */
	lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
			"0325 Reset HBA Data: x%x x%x\n",
			phba->pport->port_state, psli->sli_flag);

	/* perform board reset */
	phba->fc_eventTag = 0;
	phba->pport->fc_myDID = 0;
	phba->pport->fc_prevDID = 0;

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

	phba->link_state = LPFC_WARM_START;
	return 0;
}

/**
 * lpfc_sli_brdrestart: Restart the HBA.
 * @phba: Pointer to HBA context object.
 *
 * This function is called in the SLI initialization code path to
 * restart the HBA. The caller is not required to hold any lock.
 * This function writes MBX_RESTART mailbox command to the SLIM and
 * resets the HBA. At the end of the function, it calls lpfc_hba_down_post
 * function to free any pending commands. The function enables
 * POST only during the first initialization. The function returns zero.
 * The function does not guarantee completion of MBX_RESTART mailbox
 * command before the return of this function.
 **/
int
lpfc_sli_brdrestart(struct lpfc_hba *phba)
{
	MAILBOX_t *mb;
	struct lpfc_sli *psli;
	volatile uint32_t word0;
	void __iomem *to_slim;

	spin_lock_irq(&phba->hbalock);

	psli = &phba->sli;

	/* Restart HBA */
	lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
			"0337 Restart HBA Data: x%x x%x\n",
			phba->pport->port_state, psli->sli_flag);

	word0 = 0;
	mb = (MAILBOX_t *) &word0;
	mb->mbxCommand = MBX_RESTART;
	mb->mbxHc = 1;

	lpfc_reset_barrier(phba);

	to_slim = phba->MBslimaddr;
	writel(*(uint32_t *) mb, to_slim);
	readl(to_slim); /* flush */

	/* Only skip post after fc_ffinit is completed */
	if (phba->pport->port_state)
		word0 = 1;	/* This is really setting up word1 */
	else
		word0 = 0;	/* This is really setting up word1 */
	to_slim = phba->MBslimaddr + sizeof (uint32_t);
	writel(*(uint32_t *) mb, to_slim);
	readl(to_slim); /* flush */

	lpfc_sli_brdreset(phba);
	phba->pport->stopped = 0;
	phba->link_state = LPFC_INIT_START;

	spin_unlock_irq(&phba->hbalock);

	memset(&psli->lnk_stat_offsets, 0, sizeof(psli->lnk_stat_offsets));
	psli->stats_start = get_seconds();

	/* Give the INITFF and Post time to settle. */
	mdelay(100);

	lpfc_hba_down_post(phba);

	return 0;
}

/**
 * lpfc_sli_chipset_init: Wait for the restart of the HBA after a restart.
 * @phba: Pointer to HBA context object.
 *
 * This function is called after a HBA restart to wait for successful
 * restart of the HBA. Successful restart of the HBA is indicated by
 * HS_FFRDY and HS_MBRDY bits. If the HBA fails to restart even after 15
 * iteration, the function will restart the HBA again. The function returns
 * zero if HBA successfully restarted else returns negative error code.
 **/
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
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"0436 Adapter failed to init, "
					"timeout, status reg x%x, "
					"FW Data: A8 x%x AC x%x\n", status,
					readl(phba->MBslimaddr + 0xa8),
					readl(phba->MBslimaddr + 0xac));
			phba->link_state = LPFC_HBA_ERROR;
			return -ETIMEDOUT;
		}

		/* Check to see if any errors occurred during init */
		if (status & HS_FFERM) {
			/* ERROR: During chipset initialization */
			/* Adapter failed to init, chipset, status reg
			   <status> */
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
					"0437 Adapter failed to init, "
					"chipset, status reg x%x, "
					"FW Data: A8 x%x AC x%x\n", status,
					readl(phba->MBslimaddr + 0xa8),
					readl(phba->MBslimaddr + 0xac));
			phba->link_state = LPFC_HBA_ERROR;
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
				/* Do post */
			phba->pport->port_state = LPFC_VPORT_UNKNOWN;
			lpfc_sli_brdrestart(phba);
		}
		/* Read the HBA Host Status Register */
		status = readl(phba->HSregaddr);
	}

	/* Check to see if any errors occurred during init */
	if (status & HS_FFERM) {
		/* ERROR: During chipset initialization */
		/* Adapter failed to init, chipset, status reg <status> */
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0438 Adapter failed to init, chipset, "
				"status reg x%x, "
				"FW Data: A8 x%x AC x%x\n", status,
				readl(phba->MBslimaddr + 0xa8),
				readl(phba->MBslimaddr + 0xac));
		phba->link_state = LPFC_HBA_ERROR;
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

/**
 * lpfc_sli_hbq_count: Get the number of HBQs to be configured.
 *
 * This function calculates and returns the number of HBQs required to be
 * configured.
 **/
int
lpfc_sli_hbq_count(void)
{
	return ARRAY_SIZE(lpfc_hbq_defs);
}

/**
 * lpfc_sli_hbq_entry_count: Calculate total number of hbq entries.
 *
 * This function adds the number of hbq entries in every HBQ to get
 * the total number of hbq entries required for the HBA and returns
 * the total count.
 **/
static int
lpfc_sli_hbq_entry_count(void)
{
	int  hbq_count = lpfc_sli_hbq_count();
	int  count = 0;
	int  i;

	for (i = 0; i < hbq_count; ++i)
		count += lpfc_hbq_defs[i]->entry_count;
	return count;
}

/**
 * lpfc_sli_hbq_size: Calculate memory required for all hbq entries.
 *
 * This function calculates amount of memory required for all hbq entries
 * to be configured and returns the total memory required.
 **/
int
lpfc_sli_hbq_size(void)
{
	return lpfc_sli_hbq_entry_count() * sizeof(struct lpfc_hbq_entry);
}

/**
 * lpfc_sli_hbq_setup: configure and initialize HBQs.
 * @phba: Pointer to HBA context object.
 *
 * This function is called during the SLI initialization to configure
 * all the HBQs and post buffers to the HBQ. The caller is not
 * required to hold any locks. This function will return zero if successful
 * else it will return negative error code.
 **/
static int
lpfc_sli_hbq_setup(struct lpfc_hba *phba)
{
	int  hbq_count = lpfc_sli_hbq_count();
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *pmbox;
	uint32_t hbqno;
	uint32_t hbq_entry_index;

				/* Get a Mailbox buffer to setup mailbox
				 * commands for HBA initialization
				 */
	pmb = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);

	if (!pmb)
		return -ENOMEM;

	pmbox = &pmb->mb;

	/* Initialize the struct lpfc_sli_hbq structure for each hbq */
	phba->link_state = LPFC_INIT_MBX_CMDS;
	phba->hbq_in_use = 1;

	hbq_entry_index = 0;
	for (hbqno = 0; hbqno < hbq_count; ++hbqno) {
		phba->hbqs[hbqno].next_hbqPutIdx = 0;
		phba->hbqs[hbqno].hbqPutIdx      = 0;
		phba->hbqs[hbqno].local_hbqGetIdx   = 0;
		phba->hbqs[hbqno].entry_count =
			lpfc_hbq_defs[hbqno]->entry_count;
		lpfc_config_hbq(phba, hbqno, lpfc_hbq_defs[hbqno],
			hbq_entry_index, pmb);
		hbq_entry_index += phba->hbqs[hbqno].entry_count;

		if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
			/* Adapter failed to init, mbxCmd <cmd> CFG_RING,
			   mbxStatus <status>, ring <num> */

			lpfc_printf_log(phba, KERN_ERR,
					LOG_SLI | LOG_VPORT,
					"1805 Adapter failed to init. "
					"Data: x%x x%x x%x\n",
					pmbox->mbxCommand,
					pmbox->mbxStatus, hbqno);

			phba->link_state = LPFC_HBA_ERROR;
			mempool_free(pmb, phba->mbox_mem_pool);
			return ENXIO;
		}
	}
	phba->hbq_count = hbq_count;

	mempool_free(pmb, phba->mbox_mem_pool);

	/* Initially populate or replenish the HBQs */
	for (hbqno = 0; hbqno < hbq_count; ++hbqno)
		lpfc_sli_hbqbuf_init_hbqs(phba, hbqno);
	return 0;
}

/**
 * lpfc_sli_config_port: Issue config port mailbox command.
 * @phba: Pointer to HBA context object.
 * @sli_mode: sli mode - 2/3
 *
 * This function is called by the sli intialization code path
 * to issue config_port mailbox command. This function restarts the
 * HBA firmware and issues a config_port mailbox command to configure
 * the SLI interface in the sli mode specified by sli_mode
 * variable. The caller is not required to hold any locks.
 * The function returns 0 if successful, else returns negative error
 * code.
 **/
int
lpfc_sli_config_port(struct lpfc_hba *phba, int sli_mode)
{
	LPFC_MBOXQ_t *pmb;
	uint32_t resetcount = 0, rc = 0, done = 0;

	pmb = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmb) {
		phba->link_state = LPFC_HBA_ERROR;
		return -ENOMEM;
	}

	phba->sli_rev = sli_mode;
	while (resetcount < 2 && !done) {
		spin_lock_irq(&phba->hbalock);
		phba->sli.sli_flag |= LPFC_SLI_MBOX_ACTIVE;
		spin_unlock_irq(&phba->hbalock);
		phba->pport->port_state = LPFC_VPORT_UNKNOWN;
		lpfc_sli_brdrestart(phba);
		rc = lpfc_sli_chipset_init(phba);
		if (rc)
			break;

		spin_lock_irq(&phba->hbalock);
		phba->sli.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
		spin_unlock_irq(&phba->hbalock);
		resetcount++;

		/* Call pre CONFIG_PORT mailbox command initialization.  A
		 * value of 0 means the call was successful.  Any other
		 * nonzero value is a failure, but if ERESTART is returned,
		 * the driver may reset the HBA and try again.
		 */
		rc = lpfc_config_port_prep(phba);
		if (rc == -ERESTART) {
			phba->link_state = LPFC_LINK_UNKNOWN;
			continue;
		} else if (rc)
			break;
		phba->link_state = LPFC_INIT_MBX_CMDS;
		lpfc_config_port(phba, pmb);
		rc = lpfc_sli_issue_mbox(phba, pmb, MBX_POLL);
		phba->sli3_options &= ~(LPFC_SLI3_NPIV_ENABLED |
					LPFC_SLI3_HBQ_ENABLED |
					LPFC_SLI3_CRP_ENABLED |
					LPFC_SLI3_INB_ENABLED |
					LPFC_SLI3_BG_ENABLED);
		if (rc != MBX_SUCCESS) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0442 Adapter failed to init, mbxCmd x%x "
				"CONFIG_PORT, mbxStatus x%x Data: x%x\n",
				pmb->mb.mbxCommand, pmb->mb.mbxStatus, 0);
			spin_lock_irq(&phba->hbalock);
			phba->sli.sli_flag &= ~LPFC_SLI2_ACTIVE;
			spin_unlock_irq(&phba->hbalock);
			rc = -ENXIO;
		} else
			done = 1;
	}
	if (!done) {
		rc = -EINVAL;
		goto do_prep_failed;
	}
	if (pmb->mb.un.varCfgPort.sli_mode == 3) {
		if (!pmb->mb.un.varCfgPort.cMA) {
			rc = -ENXIO;
			goto do_prep_failed;
		}
		if (phba->max_vpi && pmb->mb.un.varCfgPort.gmv) {
			phba->sli3_options |= LPFC_SLI3_NPIV_ENABLED;
			phba->max_vpi = pmb->mb.un.varCfgPort.max_vpi;
		} else
			phba->max_vpi = 0;
		if (pmb->mb.un.varCfgPort.gerbm)
			phba->sli3_options |= LPFC_SLI3_HBQ_ENABLED;
		if (pmb->mb.un.varCfgPort.gcrp)
			phba->sli3_options |= LPFC_SLI3_CRP_ENABLED;
		if (pmb->mb.un.varCfgPort.ginb) {
			phba->sli3_options |= LPFC_SLI3_INB_ENABLED;
			phba->hbq_get = phba->mbox->us.s3_inb_pgp.hbq_get;
			phba->port_gp = phba->mbox->us.s3_inb_pgp.port;
			phba->inb_ha_copy = &phba->mbox->us.s3_inb_pgp.ha_copy;
			phba->inb_counter = &phba->mbox->us.s3_inb_pgp.counter;
			phba->inb_last_counter =
					phba->mbox->us.s3_inb_pgp.counter;
		} else {
			phba->hbq_get = phba->mbox->us.s3_pgp.hbq_get;
			phba->port_gp = phba->mbox->us.s3_pgp.port;
			phba->inb_ha_copy = NULL;
			phba->inb_counter = NULL;
		}

		if (phba->cfg_enable_bg) {
			if (pmb->mb.un.varCfgPort.gbg)
				phba->sli3_options |= LPFC_SLI3_BG_ENABLED;
			else
				lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
						"0443 Adapter did not grant "
						"BlockGuard\n");
		}
	} else {
		phba->hbq_get = NULL;
		phba->port_gp = phba->mbox->us.s2.port;
		phba->inb_ha_copy = NULL;
		phba->inb_counter = NULL;
		phba->max_vpi = 0;
	}
do_prep_failed:
	mempool_free(pmb, phba->mbox_mem_pool);
	return rc;
}


/**
 * lpfc_sli_hba_setup: SLI intialization function.
 * @phba: Pointer to HBA context object.
 *
 * This function is the main SLI intialization function. This function
 * is called by the HBA intialization code, HBA reset code and HBA
 * error attention handler code. Caller is not required to hold any
 * locks. This function issues config_port mailbox command to configure
 * the SLI, setup iocb rings and HBQ rings. In the end the function
 * calls the config_port_post function to issue init_link mailbox
 * command and to start the discovery. The function will return zero
 * if successful, else it will return negative error code.
 **/
int
lpfc_sli_hba_setup(struct lpfc_hba *phba)
{
	uint32_t rc;
	int  mode = 3;

	switch (lpfc_sli_mode) {
	case 2:
		if (phba->cfg_enable_npiv) {
			lpfc_printf_log(phba, KERN_ERR, LOG_INIT | LOG_VPORT,
				"1824 NPIV enabled: Override lpfc_sli_mode "
				"parameter (%d) to auto (0).\n",
				lpfc_sli_mode);
			break;
		}
		mode = 2;
		break;
	case 0:
	case 3:
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT | LOG_VPORT,
				"1819 Unrecognized lpfc_sli_mode "
				"parameter: %d.\n", lpfc_sli_mode);

		break;
	}

	rc = lpfc_sli_config_port(phba, mode);

	if (rc && lpfc_sli_mode == 3)
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT | LOG_VPORT,
				"1820 Unable to select SLI-3.  "
				"Not supported by adapter.\n");
	if (rc && mode != 2)
		rc = lpfc_sli_config_port(phba, 2);
	if (rc)
		goto lpfc_sli_hba_setup_error;

	if (phba->sli_rev == 3) {
		phba->iocb_cmd_size = SLI3_IOCB_CMD_SIZE;
		phba->iocb_rsp_size = SLI3_IOCB_RSP_SIZE;
	} else {
		phba->iocb_cmd_size = SLI2_IOCB_CMD_SIZE;
		phba->iocb_rsp_size = SLI2_IOCB_RSP_SIZE;
		phba->sli3_options = 0;
	}

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"0444 Firmware in SLI %x mode. Max_vpi %d\n",
			phba->sli_rev, phba->max_vpi);
	rc = lpfc_sli_ring_map(phba);

	if (rc)
		goto lpfc_sli_hba_setup_error;

	/* Init HBQs */
	if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) {
		rc = lpfc_sli_hbq_setup(phba);
		if (rc)
			goto lpfc_sli_hba_setup_error;
	}

	phba->sli.sli_flag |= LPFC_PROCESS_LA;

	rc = lpfc_config_port_post(phba);
	if (rc)
		goto lpfc_sli_hba_setup_error;

	return rc;

lpfc_sli_hba_setup_error:
	phba->link_state = LPFC_HBA_ERROR;
	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"0445 Firmware initialization failed\n");
	return rc;
}


/**
 * lpfc_mbox_timeout: Timeout call back function for mbox timer.
 * @ptr: context object - pointer to hba structure.
 *
 * This is the callback function for mailbox timer. The mailbox
 * timer is armed when a new mailbox command is issued and the timer
 * is deleted when the mailbox complete. The function is called by
 * the kernel timer code when a mailbox does not complete within
 * expected time. This function wakes up the worker thread to
 * process the mailbox timeout and returns. All the processing is
 * done by the worker thread function lpfc_mbox_timeout_handler.
 **/
void
lpfc_mbox_timeout(unsigned long ptr)
{
	struct lpfc_hba  *phba = (struct lpfc_hba *) ptr;
	unsigned long iflag;
	uint32_t tmo_posted;

	spin_lock_irqsave(&phba->pport->work_port_lock, iflag);
	tmo_posted = phba->pport->work_port_events & WORKER_MBOX_TMO;
	if (!tmo_posted)
		phba->pport->work_port_events |= WORKER_MBOX_TMO;
	spin_unlock_irqrestore(&phba->pport->work_port_lock, iflag);

	if (!tmo_posted)
		lpfc_worker_wake_up(phba);
	return;
}


/**
 * lpfc_mbox_timeout_handler: Worker thread function to handle mailbox timeout.
 * @phba: Pointer to HBA context object.
 *
 * This function is called from worker thread when a mailbox command times out.
 * The caller is not required to hold any locks. This function will reset the
 * HBA and recover all the pending commands.
 **/
void
lpfc_mbox_timeout_handler(struct lpfc_hba *phba)
{
	LPFC_MBOXQ_t *pmbox = phba->sli.mbox_active;
	MAILBOX_t *mb = &pmbox->mb;
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring;

	/* Mbox cmd <mbxCommand> timeout */
	lpfc_printf_log(phba, KERN_ERR, LOG_MBOX | LOG_SLI,
			"0310 Mailbox command x%x timeout Data: x%x x%x x%p\n",
			mb->mbxCommand,
			phba->pport->port_state,
			phba->sli.sli_flag,
			phba->sli.mbox_active);

	/* Setting state unknown so lpfc_sli_abort_iocb_ring
	 * would get IOCB_ERROR from lpfc_sli_issue_iocb, allowing
	 * it to fail all oustanding SCSI IO.
	 */
	spin_lock_irq(&phba->pport->work_port_lock);
	phba->pport->work_port_events &= ~WORKER_MBOX_TMO;
	spin_unlock_irq(&phba->pport->work_port_lock);
	spin_lock_irq(&phba->hbalock);
	phba->link_state = LPFC_LINK_UNKNOWN;
	psli->sli_flag &= ~LPFC_SLI2_ACTIVE;
	spin_unlock_irq(&phba->hbalock);

	pring = &psli->ring[psli->fcp_ring];
	lpfc_sli_abort_iocb_ring(phba, pring);

	lpfc_printf_log(phba, KERN_ERR, LOG_MBOX | LOG_SLI,
			"0345 Resetting board due to mailbox timeout\n");
	/*
	 * lpfc_offline calls lpfc_sli_hba_down which will clean up
	 * on oustanding mailbox commands.
	 */
	/* If resets are disabled then set error state and return. */
	if (!phba->cfg_enable_hba_reset) {
		phba->link_state = LPFC_HBA_ERROR;
		return;
	}
	lpfc_offline_prep(phba);
	lpfc_offline(phba);
	lpfc_sli_brdrestart(phba);
	lpfc_online(phba);
	lpfc_unblock_mgmt_io(phba);
	return;
}

/**
 * lpfc_sli_issue_mbox: Issue a mailbox command to firmware.
 * @phba: Pointer to HBA context object.
 * @pmbox: Pointer to mailbox object.
 * @flag: Flag indicating how the mailbox need to be processed.
 *
 * This function is called by discovery code and HBA management code
 * to submit a mailbox command to firmware. This function gets the
 * hbalock to protect the data structures.
 * The mailbox command can be submitted in polling mode, in which case
 * this function will wait in a polling loop for the completion of the
 * mailbox.
 * If the mailbox is submitted in no_wait mode (not polling) the
 * function will submit the command and returns immediately without waiting
 * for the mailbox completion. The no_wait is supported only when HBA
 * is in SLI2/SLI3 mode - interrupts are enabled.
 * The SLI interface allows only one mailbox pending at a time. If the
 * mailbox is issued in polling mode and there is already a mailbox
 * pending, then the function will return an error. If the mailbox is issued
 * in NO_WAIT mode and there is a mailbox pending already, the function
 * will return MBX_BUSY after queuing the mailbox into mailbox queue.
 * The sli layer owns the mailbox object until the completion of mailbox
 * command if this function return MBX_BUSY or MBX_SUCCESS. For all other
 * return codes the caller owns the mailbox command after the return of
 * the function.
 **/
int
lpfc_sli_issue_mbox(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmbox, uint32_t flag)
{
	MAILBOX_t *mb;
	struct lpfc_sli *psli = &phba->sli;
	uint32_t status, evtctr;
	uint32_t ha_copy;
	int i;
	unsigned long timeout;
	unsigned long drvr_flag = 0;
	uint32_t word0, ldata;
	void __iomem *to_slim;
	int processing_queue = 0;

	spin_lock_irqsave(&phba->hbalock, drvr_flag);
	if (!pmbox) {
		/* processing mbox queue from intr_handler */
		processing_queue = 1;
		phba->sli.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
		pmbox = lpfc_mbox_get(phba);
		if (!pmbox) {
			spin_unlock_irqrestore(&phba->hbalock, drvr_flag);
			return MBX_SUCCESS;
		}
	}

	if (pmbox->mbox_cmpl && pmbox->mbox_cmpl != lpfc_sli_def_mbox_cmpl &&
		pmbox->mbox_cmpl != lpfc_sli_wake_mbox_wait) {
		if(!pmbox->vport) {
			spin_unlock_irqrestore(&phba->hbalock, drvr_flag);
			lpfc_printf_log(phba, KERN_ERR,
					LOG_MBOX | LOG_VPORT,
					"1806 Mbox x%x failed. No vport\n",
					pmbox->mb.mbxCommand);
			dump_stack();
			goto out_not_finished;
		}
	}

	/* If the PCI channel is in offline state, do not post mbox. */
	if (unlikely(pci_channel_offline(phba->pcidev))) {
		spin_unlock_irqrestore(&phba->hbalock, drvr_flag);
		goto out_not_finished;
	}

	psli = &phba->sli;

	mb = &pmbox->mb;
	status = MBX_SUCCESS;

	if (phba->link_state == LPFC_HBA_ERROR) {
		spin_unlock_irqrestore(&phba->hbalock, drvr_flag);

		/* Mbox command <mbxCommand> cannot issue */
		LOG_MBOX_CANNOT_ISSUE_DATA(phba, pmbox, psli, flag);
		goto out_not_finished;
	}

	if (mb->mbxCommand != MBX_KILL_BOARD && flag & MBX_NOWAIT &&
	    !(readl(phba->HCregaddr) & HC_MBINT_ENA)) {
		spin_unlock_irqrestore(&phba->hbalock, drvr_flag);
		LOG_MBOX_CANNOT_ISSUE_DATA(phba, pmbox, psli, flag);
		goto out_not_finished;
	}

	if (psli->sli_flag & LPFC_SLI_MBOX_ACTIVE) {
		/* Polling for a mbox command when another one is already active
		 * is not allowed in SLI. Also, the driver must have established
		 * SLI2 mode to queue and process multiple mbox commands.
		 */

		if (flag & MBX_POLL) {
			spin_unlock_irqrestore(&phba->hbalock, drvr_flag);

			/* Mbox command <mbxCommand> cannot issue */
			LOG_MBOX_CANNOT_ISSUE_DATA(phba, pmbox, psli, flag);
			goto out_not_finished;
		}

		if (!(psli->sli_flag & LPFC_SLI2_ACTIVE)) {
			spin_unlock_irqrestore(&phba->hbalock, drvr_flag);
			/* Mbox command <mbxCommand> cannot issue */
			LOG_MBOX_CANNOT_ISSUE_DATA(phba, pmbox, psli, flag);
			goto out_not_finished;
		}

		/* Another mailbox command is still being processed, queue this
		 * command to be processed later.
		 */
		lpfc_mbox_put(phba, pmbox);

		/* Mbox cmd issue - BUSY */
		lpfc_printf_log(phba, KERN_INFO, LOG_MBOX | LOG_SLI,
				"(%d):0308 Mbox cmd issue - BUSY Data: "
				"x%x x%x x%x x%x\n",
				pmbox->vport ? pmbox->vport->vpi : 0xffffff,
				mb->mbxCommand, phba->pport->port_state,
				psli->sli_flag, flag);

		psli->slistat.mbox_busy++;
		spin_unlock_irqrestore(&phba->hbalock, drvr_flag);

		if (pmbox->vport) {
			lpfc_debugfs_disc_trc(pmbox->vport,
				LPFC_DISC_TRC_MBOX_VPORT,
				"MBOX Bsy vport:  cmd:x%x mb:x%x x%x",
				(uint32_t)mb->mbxCommand,
				mb->un.varWords[0], mb->un.varWords[1]);
		}
		else {
			lpfc_debugfs_disc_trc(phba->pport,
				LPFC_DISC_TRC_MBOX,
				"MBOX Bsy:        cmd:x%x mb:x%x x%x",
				(uint32_t)mb->mbxCommand,
				mb->un.varWords[0], mb->un.varWords[1]);
		}

		return MBX_BUSY;
	}

	psli->sli_flag |= LPFC_SLI_MBOX_ACTIVE;

	/* If we are not polling, we MUST be in SLI2 mode */
	if (flag != MBX_POLL) {
		if (!(psli->sli_flag & LPFC_SLI2_ACTIVE) &&
		    (mb->mbxCommand != MBX_KILL_BOARD)) {
			psli->sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
			spin_unlock_irqrestore(&phba->hbalock, drvr_flag);
			/* Mbox command <mbxCommand> cannot issue */
			LOG_MBOX_CANNOT_ISSUE_DATA(phba, pmbox, psli, flag);
			goto out_not_finished;
		}
		/* timeout active mbox command */
		mod_timer(&psli->mbox_tmo, (jiffies +
			       (HZ * lpfc_mbox_tmo_val(phba, mb->mbxCommand))));
	}

	/* Mailbox cmd <cmd> issue */
	lpfc_printf_log(phba, KERN_INFO, LOG_MBOX | LOG_SLI,
			"(%d):0309 Mailbox cmd x%x issue Data: x%x x%x "
			"x%x\n",
			pmbox->vport ? pmbox->vport->vpi : 0,
			mb->mbxCommand, phba->pport->port_state,
			psli->sli_flag, flag);

	if (mb->mbxCommand != MBX_HEARTBEAT) {
		if (pmbox->vport) {
			lpfc_debugfs_disc_trc(pmbox->vport,
				LPFC_DISC_TRC_MBOX_VPORT,
				"MBOX Send vport: cmd:x%x mb:x%x x%x",
				(uint32_t)mb->mbxCommand,
				mb->un.varWords[0], mb->un.varWords[1]);
		}
		else {
			lpfc_debugfs_disc_trc(phba->pport,
				LPFC_DISC_TRC_MBOX,
				"MBOX Send:       cmd:x%x mb:x%x x%x",
				(uint32_t)mb->mbxCommand,
				mb->un.varWords[0], mb->un.varWords[1]);
		}
	}

	psli->slistat.mbox_cmd++;
	evtctr = psli->slistat.mbox_event;

	/* next set own bit for the adapter and copy over command word */
	mb->mbxOwner = OWN_CHIP;

	if (psli->sli_flag & LPFC_SLI2_ACTIVE) {
		/* First copy command data to host SLIM area */
		lpfc_sli_pcimem_bcopy(mb, phba->mbox, MAILBOX_CMD_SIZE);
	} else {
		if (mb->mbxCommand == MBX_CONFIG_PORT) {
			/* copy command data into host mbox for cmpl */
			lpfc_sli_pcimem_bcopy(mb, phba->mbox, MAILBOX_CMD_SIZE);
		}

		/* First copy mbox command data to HBA SLIM, skip past first
		   word */
		to_slim = phba->MBslimaddr + sizeof (uint32_t);
		lpfc_memcpy_to_slim(to_slim, &mb->un.varWords[0],
			    MAILBOX_CMD_SIZE - sizeof (uint32_t));

		/* Next copy over first word, with mbxOwner set */
		ldata = *((uint32_t *)mb);
		to_slim = phba->MBslimaddr;
		writel(ldata, to_slim);
		readl(to_slim); /* flush */

		if (mb->mbxCommand == MBX_CONFIG_PORT) {
			/* switch over to host mailbox */
			psli->sli_flag |= LPFC_SLI2_ACTIVE;
		}
	}

	wmb();

	switch (flag) {
	case MBX_NOWAIT:
		/* Set up reference to mailbox command */
		psli->mbox_active = pmbox;
		/* Interrupt board to do it */
		writel(CA_MBATT, phba->CAregaddr);
		readl(phba->CAregaddr); /* flush */
		/* Don't wait for it to finish, just return */
		break;

	case MBX_POLL:
		/* Set up null reference to mailbox command */
		psli->mbox_active = NULL;
		/* Interrupt board to do it */
		writel(CA_MBATT, phba->CAregaddr);
		readl(phba->CAregaddr); /* flush */

		if (psli->sli_flag & LPFC_SLI2_ACTIVE) {
			/* First read mbox status word */
			word0 = *((uint32_t *)phba->mbox);
			word0 = le32_to_cpu(word0);
		} else {
			/* First read mbox status word */
			word0 = readl(phba->MBslimaddr);
		}

		/* Read the HBA Host Attention Register */
		ha_copy = readl(phba->HAregaddr);
		timeout = msecs_to_jiffies(lpfc_mbox_tmo_val(phba,
							     mb->mbxCommand) *
					   1000) + jiffies;
		i = 0;
		/* Wait for command to complete */
		while (((word0 & OWN_CHIP) == OWN_CHIP) ||
		       (!(ha_copy & HA_MBATT) &&
			(phba->link_state > LPFC_WARM_START))) {
			if (time_after(jiffies, timeout)) {
				psli->sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
				spin_unlock_irqrestore(&phba->hbalock,
						       drvr_flag);
				goto out_not_finished;
			}

			/* Check if we took a mbox interrupt while we were
			   polling */
			if (((word0 & OWN_CHIP) != OWN_CHIP)
			    && (evtctr != psli->slistat.mbox_event))
				break;

			if (i++ > 10) {
				spin_unlock_irqrestore(&phba->hbalock,
						       drvr_flag);
				msleep(1);
				spin_lock_irqsave(&phba->hbalock, drvr_flag);
			}

			if (psli->sli_flag & LPFC_SLI2_ACTIVE) {
				/* First copy command data */
				word0 = *((uint32_t *)phba->mbox);
				word0 = le32_to_cpu(word0);
				if (mb->mbxCommand == MBX_CONFIG_PORT) {
					MAILBOX_t *slimmb;
					uint32_t slimword0;
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
			lpfc_sli_pcimem_bcopy(phba->mbox, mb, MAILBOX_CMD_SIZE);
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

	spin_unlock_irqrestore(&phba->hbalock, drvr_flag);
	return status;

out_not_finished:
	if (processing_queue) {
		pmbox->mb.mbxStatus = MBX_NOT_FINISHED;
		lpfc_mbox_cmpl_put(phba, pmbox);
	}
	return MBX_NOT_FINISHED;
}

/**
 * __lpfc_sli_ringtx_put: Add an iocb to the txq.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @piocb: Pointer to address of newly added command iocb.
 *
 * This function is called with hbalock held to add a command
 * iocb to the txq when SLI layer cannot submit the command iocb
 * to the ring.
 **/
static void
__lpfc_sli_ringtx_put(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		    struct lpfc_iocbq *piocb)
{
	/* Insert the caller's iocb in the txq tail for later processing. */
	list_add_tail(&piocb->list, &pring->txq);
	pring->txq_cnt++;
}

/**
 * lpfc_sli_next_iocb: Get the next iocb in the txq.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @piocb: Pointer to address of newly added command iocb.
 *
 * This function is called with hbalock held before a new
 * iocb is submitted to the firmware. This function checks
 * txq to flush the iocbs in txq to Firmware before
 * submitting new iocbs to the Firmware.
 * If there are iocbs in the txq which need to be submitted
 * to firmware, lpfc_sli_next_iocb returns the first element
 * of the txq after dequeuing it from txq.
 * If there is no iocb in the txq then the function will return
 * *piocb and *piocb is set to NULL. Caller needs to check
 * *piocb to find if there are more commands in the txq.
 **/
static struct lpfc_iocbq *
lpfc_sli_next_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		   struct lpfc_iocbq **piocb)
{
	struct lpfc_iocbq * nextiocb;

	nextiocb = lpfc_sli_ringtx_get(phba, pring);
	if (!nextiocb) {
		nextiocb = *piocb;
		*piocb = NULL;
	}

	return nextiocb;
}

/**
 * __lpfc_sli_issue_iocb: Lockless version of lpfc_sli_issue_iocb.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @piocb: Pointer to command iocb.
 * @flag: Flag indicating if this command can be put into txq.
 *
 * __lpfc_sli_issue_iocb is used by other functions in the driver
 * to issue an iocb command to the HBA. If the PCI slot is recovering
 * from error state or if HBA is resetting or if LPFC_STOP_IOCB_EVENT
 * flag is turned on, the function returns IOCB_ERROR.
 * When the link is down, this function allows only iocbs for
 * posting buffers.
 * This function finds next available slot in the command ring and
 * posts the command to the available slot and writes the port
 * attention register to request HBA start processing new iocb.
 * If there is no slot available in the ring and
 * flag & SLI_IOCB_RET_IOCB is set, the new iocb is added to the
 * txq, otherwise the function returns IOCB_BUSY.
 *
 * This function is called with hbalock held.
 * The function will return success after it successfully submit the
 * iocb to firmware or after adding to the txq.
 **/
static int
__lpfc_sli_issue_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		    struct lpfc_iocbq *piocb, uint32_t flag)
{
	struct lpfc_iocbq *nextiocb;
	IOCB_t *iocb;

	if (piocb->iocb_cmpl && (!piocb->vport) &&
	   (piocb->iocb.ulpCommand != CMD_ABORT_XRI_CN) &&
	   (piocb->iocb.ulpCommand != CMD_CLOSE_XRI_CN)) {
		lpfc_printf_log(phba, KERN_ERR,
				LOG_SLI | LOG_VPORT,
				"1807 IOCB x%x failed. No vport\n",
				piocb->iocb.ulpCommand);
		dump_stack();
		return IOCB_ERROR;
	}


	/* If the PCI channel is in offline state, do not post iocbs. */
	if (unlikely(pci_channel_offline(phba->pcidev)))
		return IOCB_ERROR;

	/*
	 * We should never get an IOCB if we are in a < LINK_DOWN state
	 */
	if (unlikely(phba->link_state < LPFC_LINK_DOWN))
		return IOCB_ERROR;

	/*
	 * Check to see if we are blocking IOCB processing because of a
	 * outstanding event.
	 */
	if (unlikely(pring->flag & LPFC_STOP_IOCB_EVENT))
		goto iocb_busy;

	if (unlikely(phba->link_state == LPFC_LINK_DOWN)) {
		/*
		 * Only CREATE_XRI, CLOSE_XRI, and QUE_RING_BUF
		 * can be issued if the link is not up.
		 */
		switch (piocb->iocb.ulpCommand) {
		case CMD_GEN_REQUEST64_CR:
		case CMD_GEN_REQUEST64_CX:
			if (!(phba->sli.sli_flag & LPFC_MENLO_MAINT) ||
				(piocb->iocb.un.genreq64.w5.hcsw.Rctl !=
					FC_FCP_CMND) ||
				(piocb->iocb.un.genreq64.w5.hcsw.Type !=
					MENLO_TRANSPORT_TYPE))

				goto iocb_busy;
			break;
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
		case CMD_CLOSE_XRI_CN:
		case CMD_CLOSE_XRI_CX:
			break;
		default:
			goto iocb_busy;
		}

	/*
	 * For FCP commands, we must be in a state where we can process link
	 * attention events.
	 */
	} else if (unlikely(pring->ringno == phba->sli.fcp_ring &&
			    !(phba->sli.sli_flag & LPFC_PROCESS_LA))) {
		goto iocb_busy;
	}

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
		__lpfc_sli_ringtx_put(phba, pring, piocb);
		return IOCB_SUCCESS;
	}

	return IOCB_BUSY;
}


/**
 * lpfc_sli_issue_iocb: Wrapper function for __lpfc_sli_issue_iocb.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @piocb: Pointer to command iocb.
 * @flag: Flag indicating if this command can be put into txq.
 *
 * lpfc_sli_issue_iocb is a wrapper around __lpfc_sli_issue_iocb
 * function. This function gets the hbalock and calls
 * __lpfc_sli_issue_iocb function and will return the error returned
 * by __lpfc_sli_issue_iocb function. This wrapper is used by
 * functions which do not hold hbalock.
 **/
int
lpfc_sli_issue_iocb(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		    struct lpfc_iocbq *piocb, uint32_t flag)
{
	unsigned long iflags;
	int rc;

	spin_lock_irqsave(&phba->hbalock, iflags);
	rc = __lpfc_sli_issue_iocb(phba, pring, piocb, flag);
	spin_unlock_irqrestore(&phba->hbalock, iflags);

	return rc;
}

/**
 * lpfc_extra_ring_setup: Extra ring setup function.
 * @phba: Pointer to HBA context object.
 *
 * This function is called while driver attaches with the
 * HBA to setup the extra ring. The extra ring is used
 * only when driver needs to support target mode functionality
 * or IP over FC functionalities.
 *
 * This function is called with no lock held.
 **/
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

/**
 * lpfc_sli_async_event_handler: ASYNC iocb handler function.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @iocbq: Pointer to iocb object.
 *
 * This function is called by the slow ring event handler
 * function when there is an ASYNC event iocb in the ring.
 * This function is called with no lock held.
 * Currently this function handles only temperature related
 * ASYNC events. The function decodes the temperature sensor
 * event message and posts events for the management applications.
 **/
static void
lpfc_sli_async_event_handler(struct lpfc_hba * phba,
	struct lpfc_sli_ring * pring, struct lpfc_iocbq * iocbq)
{
	IOCB_t *icmd;
	uint16_t evt_code;
	uint16_t temp;
	struct temp_event temp_event_data;
	struct Scsi_Host *shost;

	icmd = &iocbq->iocb;
	evt_code = icmd->un.asyncstat.evt_code;
	temp = icmd->ulpContext;

	if ((evt_code != ASYNC_TEMP_WARN) &&
		(evt_code != ASYNC_TEMP_SAFE)) {
		lpfc_printf_log(phba,
			KERN_ERR,
			LOG_SLI,
			"0346 Ring %d handler: unexpected ASYNC_STATUS"
			" evt_code 0x%x\n",
			pring->ringno,
			icmd->un.asyncstat.evt_code);
		return;
	}
	temp_event_data.data = (uint32_t)temp;
	temp_event_data.event_type = FC_REG_TEMPERATURE_EVENT;
	if (evt_code == ASYNC_TEMP_WARN) {
		temp_event_data.event_code = LPFC_THRESHOLD_TEMP;
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_TEMP,
				"0347 Adapter is very hot, please take "
				"corrective action. temperature : %d Celsius\n",
				temp);
	}
	if (evt_code == ASYNC_TEMP_SAFE) {
		temp_event_data.event_code = LPFC_NORMAL_TEMP;
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_TEMP,
				"0340 Adapter temperature is OK now. "
				"temperature : %d Celsius\n",
				temp);
	}

	/* Send temperature change event to applications */
	shost = lpfc_shost_from_vport(phba->pport);
	fc_host_post_vendor_event(shost, fc_get_event_number(),
		sizeof(temp_event_data), (char *) &temp_event_data,
		LPFC_NL_VENDOR_ID);

}


/**
 * lpfc_sli_setup: SLI ring setup function.
 * @phba: Pointer to HBA context object.
 *
 * lpfc_sli_setup sets up rings of the SLI interface with
 * number of iocbs per ring and iotags. This function is
 * called while driver attach to the HBA and before the
 * interrupts are enabled. So there is no need for locking.
 *
 * This function always returns 0.
 **/
int
lpfc_sli_setup(struct lpfc_hba *phba)
{
	int i, totiocbsize = 0;
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
			pring->sizeCiocb = (phba->sli_rev == 3) ?
							SLI3_IOCB_CMD_SIZE :
							SLI2_IOCB_CMD_SIZE;
			pring->sizeRiocb = (phba->sli_rev == 3) ?
							SLI3_IOCB_RSP_SIZE :
							SLI2_IOCB_RSP_SIZE;
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
			pring->sizeCiocb = (phba->sli_rev == 3) ?
							SLI3_IOCB_CMD_SIZE :
							SLI2_IOCB_CMD_SIZE;
			pring->sizeRiocb = (phba->sli_rev == 3) ?
							SLI3_IOCB_RSP_SIZE :
							SLI2_IOCB_RSP_SIZE;
			pring->iotag_max = phba->cfg_hba_queue_depth;
			pring->num_mask = 0;
			break;
		case LPFC_ELS_RING:	/* ring 2 - ELS / CT */
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R2_ENTRIES;
			pring->numRiocb = SLI2_IOCB_RSP_R2_ENTRIES;
			pring->sizeCiocb = (phba->sli_rev == 3) ?
							SLI3_IOCB_CMD_SIZE :
							SLI2_IOCB_CMD_SIZE;
			pring->sizeRiocb = (phba->sli_rev == 3) ?
							SLI3_IOCB_RSP_SIZE :
							SLI2_IOCB_RSP_SIZE;
			pring->fast_iotag = 0;
			pring->iotag_ctr = 0;
			pring->iotag_max = 4096;
			pring->lpfc_sli_rcv_async_status =
				lpfc_sli_async_event_handler;
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
		totiocbsize += (pring->numCiocb * pring->sizeCiocb) +
				(pring->numRiocb * pring->sizeRiocb);
	}
	if (totiocbsize > MAX_SLIM_IOCB_SIZE) {
		/* Too many cmd / rsp ring entries in SLI2 SLIM */
		printk(KERN_ERR "%d:0462 Too many cmd / rsp ring entries in "
		       "SLI2 SLIM Data: x%x x%lx\n",
		       phba->brd_no, totiocbsize,
		       (unsigned long) MAX_SLIM_IOCB_SIZE);
	}
	if (phba->cfg_multi_ring_support == 2)
		lpfc_extra_ring_setup(phba);

	return 0;
}

/**
 * lpfc_sli_queue_setup: Queue initialization function.
 * @phba: Pointer to HBA context object.
 *
 * lpfc_sli_queue_setup sets up mailbox queues and iocb queues for each
 * ring. This function also initializes ring indices of each ring.
 * This function is called during the initialization of the SLI
 * interface of an HBA.
 * This function is called with no lock held and always returns
 * 1.
 **/
int
lpfc_sli_queue_setup(struct lpfc_hba *phba)
{
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	int i;

	psli = &phba->sli;
	spin_lock_irq(&phba->hbalock);
	INIT_LIST_HEAD(&psli->mboxq);
	INIT_LIST_HEAD(&psli->mboxq_cmpl);
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
		INIT_LIST_HEAD(&pring->iocb_continue_saveq);
		INIT_LIST_HEAD(&pring->postbufq);
	}
	spin_unlock_irq(&phba->hbalock);
	return 1;
}

/**
 * lpfc_sli_host_down: Vport cleanup function.
 * @vport: Pointer to virtual port object.
 *
 * lpfc_sli_host_down is called to clean up the resources
 * associated with a vport before destroying virtual
 * port data structures.
 * This function does following operations:
 * - Free discovery resources associated with this virtual
 *   port.
 * - Free iocbs associated with this virtual port in
 *   the txq.
 * - Send abort for all iocb commands associated with this
 *   vport in txcmplq.
 *
 * This function is called with no lock held and always returns 1.
 **/
int
lpfc_sli_host_down(struct lpfc_vport *vport)
{
	LIST_HEAD(completions);
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *iocb, *next_iocb;
	int i;
	unsigned long flags = 0;
	uint16_t prev_pring_flag;

	lpfc_cleanup_discovery_resources(vport);

	spin_lock_irqsave(&phba->hbalock, flags);
	for (i = 0; i < psli->num_rings; i++) {
		pring = &psli->ring[i];
		prev_pring_flag = pring->flag;
		/* Only slow rings */
		if (pring->ringno == LPFC_ELS_RING) {
			pring->flag |= LPFC_DEFERRED_RING_EVENT;
			/* Set the lpfc data pending flag */
			set_bit(LPFC_DATA_READY, &phba->data_flags);
		}
		/*
		 * Error everything on the txq since these iocbs have not been
		 * given to the FW yet.
		 */
		list_for_each_entry_safe(iocb, next_iocb, &pring->txq, list) {
			if (iocb->vport != vport)
				continue;
			list_move_tail(&iocb->list, &completions);
			pring->txq_cnt--;
		}

		/* Next issue ABTS for everything on the txcmplq */
		list_for_each_entry_safe(iocb, next_iocb, &pring->txcmplq,
									list) {
			if (iocb->vport != vport)
				continue;
			lpfc_sli_issue_abort_iotag(phba, pring, iocb);
		}

		pring->flag = prev_pring_flag;
	}

	spin_unlock_irqrestore(&phba->hbalock, flags);

	while (!list_empty(&completions)) {
		list_remove_head(&completions, iocb, struct lpfc_iocbq, list);

		if (!iocb->iocb_cmpl)
			lpfc_sli_release_iocbq(phba, iocb);
		else {
			iocb->iocb.ulpStatus = IOSTAT_LOCAL_REJECT;
			iocb->iocb.un.ulpWord[4] = IOERR_SLI_DOWN;
			(iocb->iocb_cmpl) (phba, iocb, iocb);
		}
	}
	return 1;
}

/**
 * lpfc_sli_hba_down: Resource cleanup function for the HBA.
 * @phba: Pointer to HBA context object.
 *
 * This function cleans up all iocb, buffers, mailbox commands
 * while shutting down the HBA. This function is called with no
 * lock held and always returns 1.
 * This function does the following to cleanup driver resources:
 * - Free discovery resources for each virtual port
 * - Cleanup any pending fabric iocbs
 * - Iterate through the iocb txq and free each entry
 *   in the list.
 * - Free up any buffer posted to the HBA
 * - Free mailbox commands in the mailbox queue.
 **/
int
lpfc_sli_hba_down(struct lpfc_hba *phba)
{
	LIST_HEAD(completions);
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring;
	struct lpfc_dmabuf *buf_ptr;
	LPFC_MBOXQ_t *pmb;
	struct lpfc_iocbq *iocb;
	IOCB_t *cmd = NULL;
	int i;
	unsigned long flags = 0;

	lpfc_hba_down_prep(phba);

	lpfc_fabric_abort_hba(phba);

	spin_lock_irqsave(&phba->hbalock, flags);
	for (i = 0; i < psli->num_rings; i++) {
		pring = &psli->ring[i];
		/* Only slow rings */
		if (pring->ringno == LPFC_ELS_RING) {
			pring->flag |= LPFC_DEFERRED_RING_EVENT;
			/* Set the lpfc data pending flag */
			set_bit(LPFC_DATA_READY, &phba->data_flags);
		}

		/*
		 * Error everything on the txq since these iocbs have not been
		 * given to the FW yet.
		 */
		list_splice_init(&pring->txq, &completions);
		pring->txq_cnt = 0;

	}
	spin_unlock_irqrestore(&phba->hbalock, flags);

	while (!list_empty(&completions)) {
		list_remove_head(&completions, iocb, struct lpfc_iocbq, list);
		cmd = &iocb->iocb;

		if (!iocb->iocb_cmpl)
			lpfc_sli_release_iocbq(phba, iocb);
		else {
			cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			cmd->un.ulpWord[4] = IOERR_SLI_DOWN;
			(iocb->iocb_cmpl) (phba, iocb, iocb);
		}
	}

	spin_lock_irqsave(&phba->hbalock, flags);
	list_splice_init(&phba->elsbuf, &completions);
	phba->elsbuf_cnt = 0;
	phba->elsbuf_prev_cnt = 0;
	spin_unlock_irqrestore(&phba->hbalock, flags);

	while (!list_empty(&completions)) {
		list_remove_head(&completions, buf_ptr,
			struct lpfc_dmabuf, list);
		lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
		kfree(buf_ptr);
	}

	/* Return any active mbox cmds */
	del_timer_sync(&psli->mbox_tmo);
	spin_lock_irqsave(&phba->hbalock, flags);

	spin_lock(&phba->pport->work_port_lock);
	phba->pport->work_port_events &= ~WORKER_MBOX_TMO;
	spin_unlock(&phba->pport->work_port_lock);

	/* Return any pending or completed mbox cmds */
	list_splice_init(&phba->sli.mboxq, &completions);
	if (psli->mbox_active) {
		list_add_tail(&psli->mbox_active->list, &completions);
		psli->mbox_active = NULL;
		psli->sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
	}
	list_splice_init(&phba->sli.mboxq_cmpl, &completions);
	spin_unlock_irqrestore(&phba->hbalock, flags);

	while (!list_empty(&completions)) {
		list_remove_head(&completions, pmb, LPFC_MBOXQ_t, list);
		pmb->mb.mbxStatus = MBX_NOT_FINISHED;
		if (pmb->mbox_cmpl)
			pmb->mbox_cmpl(phba,pmb);
	}
	return 1;
}

/**
 * lpfc_sli_pcimem_bcopy: SLI memory copy function.
 * @srcp: Source memory pointer.
 * @destp: Destination memory pointer.
 * @cnt: Number of words required to be copied.
 *
 * This function is used for copying data between driver memory
 * and the SLI memory. This function also changes the endianness
 * of each word if native endianness is different from SLI
 * endianness. This function can be called with or without
 * lock.
 **/
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


/**
 * lpfc_sli_ringpostbuf_put: Function to add a buffer to postbufq.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @mp: Pointer to driver buffer object.
 *
 * This function is called with no lock held.
 * It always return zero after adding the buffer to the postbufq
 * buffer list.
 **/
int
lpfc_sli_ringpostbuf_put(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
			 struct lpfc_dmabuf *mp)
{
	/* Stick struct lpfc_dmabuf at end of postbufq so driver can look it up
	   later */
	spin_lock_irq(&phba->hbalock);
	list_add_tail(&mp->list, &pring->postbufq);
	pring->postbufq_cnt++;
	spin_unlock_irq(&phba->hbalock);
	return 0;
}

/**
 * lpfc_sli_get_buffer_tag: Tag allocation function for a buffer posted
 *          using CMD_QUE_XRI64_CX iocb.
 * @phba: Pointer to HBA context object.
 *
 * When HBQ is enabled, buffers are searched based on tags. This function
 * allocates a tag for buffer posted using CMD_QUE_XRI64_CX iocb. The
 * tag is bit wise or-ed with QUE_BUFTAG_BIT to make sure that the tag
 * does not conflict with tags of buffer posted for unsolicited events.
 * The function returns the allocated tag. The function is called with
 * no locks held.
 **/
uint32_t
lpfc_sli_get_buffer_tag(struct lpfc_hba *phba)
{
	spin_lock_irq(&phba->hbalock);
	phba->buffer_tag_count++;
	/*
	 * Always set the QUE_BUFTAG_BIT to distiguish between
	 * a tag assigned by HBQ.
	 */
	phba->buffer_tag_count |= QUE_BUFTAG_BIT;
	spin_unlock_irq(&phba->hbalock);
	return phba->buffer_tag_count;
}

/**
 * lpfc_sli_ring_taggedbuf_get: Search HBQ buffer associated with
 *              posted using CMD_QUE_XRI64_CX iocb.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @tag: Buffer tag.
 *
 * Buffers posted using CMD_QUE_XRI64_CX iocb are in pring->postbufq
 * list. After HBA DMA data to these buffers, CMD_IOCB_RET_XRI64_CX
 * iocb is posted to the response ring with the tag of the buffer.
 * This function searches the pring->postbufq list using the tag
 * to find buffer associated with CMD_IOCB_RET_XRI64_CX
 * iocb. If the buffer is found then lpfc_dmabuf object of the
 * buffer is returned to the caller else NULL is returned.
 * This function is called with no lock held.
 **/
struct lpfc_dmabuf *
lpfc_sli_ring_taggedbuf_get(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
			uint32_t tag)
{
	struct lpfc_dmabuf *mp, *next_mp;
	struct list_head *slp = &pring->postbufq;

	/* Search postbufq, from the begining, looking for a match on tag */
	spin_lock_irq(&phba->hbalock);
	list_for_each_entry_safe(mp, next_mp, &pring->postbufq, list) {
		if (mp->buffer_tag == tag) {
			list_del_init(&mp->list);
			pring->postbufq_cnt--;
			spin_unlock_irq(&phba->hbalock);
			return mp;
		}
	}

	spin_unlock_irq(&phba->hbalock);
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"0402 Cannot find virtual addr for buffer tag on "
			"ring %d Data x%lx x%p x%p x%x\n",
			pring->ringno, (unsigned long) tag,
			slp->next, slp->prev, pring->postbufq_cnt);

	return NULL;
}

/**
 * lpfc_sli_ringpostbuf_get: SLI2 buffer search function for
 *      unsolicited ct and els events.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @phys: DMA address of the buffer.
 *
 * This function searches the buffer list using the dma_address
 * of unsolicited event to find the driver's lpfc_dmabuf object
 * corresponding to the dma_address. The function returns the
 * lpfc_dmabuf object if a buffer is found else it returns NULL.
 * This function is called by the ct and els unsolicited event
 * handlers to get the buffer associated with the unsolicited
 * event.
 *
 * This function is called with no lock held.
 **/
struct lpfc_dmabuf *
lpfc_sli_ringpostbuf_get(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
			 dma_addr_t phys)
{
	struct lpfc_dmabuf *mp, *next_mp;
	struct list_head *slp = &pring->postbufq;

	/* Search postbufq, from the begining, looking for a match on phys */
	spin_lock_irq(&phba->hbalock);
	list_for_each_entry_safe(mp, next_mp, &pring->postbufq, list) {
		if (mp->phys == phys) {
			list_del_init(&mp->list);
			pring->postbufq_cnt--;
			spin_unlock_irq(&phba->hbalock);
			return mp;
		}
	}

	spin_unlock_irq(&phba->hbalock);
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"0410 Cannot find virtual addr for mapped buf on "
			"ring %d Data x%llx x%p x%p x%x\n",
			pring->ringno, (unsigned long long)phys,
			slp->next, slp->prev, pring->postbufq_cnt);
	return NULL;
}

/**
 * lpfc_sli_abort_els_cmpl: Completion handler for the els abort iocbs.
 * @phba: Pointer to HBA context object.
 * @cmdiocb: Pointer to driver command iocb object.
 * @rspiocb: Pointer to driver response iocb object.
 *
 * This function is the completion handler for the abort iocbs for
 * ELS commands. This function is called from the ELS ring event
 * handler with no lock held. This function frees memory resources
 * associated with the abort iocb.
 **/
static void
lpfc_sli_abort_els_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			struct lpfc_iocbq *rspiocb)
{
	IOCB_t *irsp = &rspiocb->iocb;
	uint16_t abort_iotag, abort_context;
	struct lpfc_iocbq *abort_iocb;
	struct lpfc_sli_ring *pring = &phba->sli.ring[LPFC_ELS_RING];

	abort_iocb = NULL;

	if (irsp->ulpStatus) {
		abort_context = cmdiocb->iocb.un.acxri.abortContextTag;
		abort_iotag = cmdiocb->iocb.un.acxri.abortIoTag;

		spin_lock_irq(&phba->hbalock);
		if (abort_iotag != 0 && abort_iotag <= phba->sli.last_iotag)
			abort_iocb = phba->sli.iocbq_lookup[abort_iotag];

		lpfc_printf_log(phba, KERN_INFO, LOG_ELS | LOG_SLI,
				"0327 Cannot abort els iocb %p "
				"with tag %x context %x, abort status %x, "
				"abort code %x\n",
				abort_iocb, abort_iotag, abort_context,
				irsp->ulpStatus, irsp->un.ulpWord[4]);

		/*
		 *  If the iocb is not found in Firmware queue the iocb
		 *  might have completed already. Do not free it again.
		 */
		if (irsp->ulpStatus == IOSTAT_LOCAL_REJECT) {
			spin_unlock_irq(&phba->hbalock);
			lpfc_sli_release_iocbq(phba, cmdiocb);
			return;
		}
		/*
		 * make sure we have the right iocbq before taking it
		 * off the txcmplq and try to call completion routine.
		 */
		if (!abort_iocb ||
		    abort_iocb->iocb.ulpContext != abort_context ||
		    (abort_iocb->iocb_flag & LPFC_DRIVER_ABORTED) == 0)
			spin_unlock_irq(&phba->hbalock);
		else {
			list_del_init(&abort_iocb->list);
			pring->txcmplq_cnt--;
			spin_unlock_irq(&phba->hbalock);

			/* Firmware could still be in progress of DMAing
			 * payload, so don't free data buffer till after
			 * a hbeat.
			 */
			abort_iocb->iocb_flag |= LPFC_DELAY_MEM_FREE;

			abort_iocb->iocb_flag &= ~LPFC_DRIVER_ABORTED;
			abort_iocb->iocb.ulpStatus = IOSTAT_LOCAL_REJECT;
			abort_iocb->iocb.un.ulpWord[4] = IOERR_SLI_ABORTED;
			(abort_iocb->iocb_cmpl)(phba, abort_iocb, abort_iocb);
		}
	}

	lpfc_sli_release_iocbq(phba, cmdiocb);
	return;
}

/**
 * lpfc_ignore_els_cmpl: Completion handler for aborted ELS command.
 * @phba: Pointer to HBA context object.
 * @cmdiocb: Pointer to driver command iocb object.
 * @rspiocb: Pointer to driver response iocb object.
 *
 * The function is called from SLI ring event handler with no
 * lock held. This function is the completion handler for ELS commands
 * which are aborted. The function frees memory resources used for
 * the aborted ELS commands.
 **/
static void
lpfc_ignore_els_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
		     struct lpfc_iocbq *rspiocb)
{
	IOCB_t *irsp = &rspiocb->iocb;

	/* ELS cmd tag <ulpIoTag> completes */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"0139 Ignoring ELS cmd tag x%x completion Data: "
			"x%x x%x x%x\n",
			irsp->ulpIoTag, irsp->ulpStatus,
			irsp->un.ulpWord[4], irsp->ulpTimeout);
	if (cmdiocb->iocb.ulpCommand == CMD_GEN_REQUEST64_CR)
		lpfc_ct_free_iocb(phba, cmdiocb);
	else
		lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

/**
 * lpfc_sli_issue_abort_iotag: Abort function for a command iocb.
 * @phba: Pointer to HBA context object.
 * @pring: Pointer to driver SLI ring object.
 * @cmdiocb: Pointer to driver command iocb object.
 *
 * This function issues an abort iocb for the provided command
 * iocb. This function is called with hbalock held.
 * The function returns 0 when it fails due to memory allocation
 * failure or when the command iocb is an abort request.
 **/
int
lpfc_sli_issue_abort_iotag(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
			   struct lpfc_iocbq *cmdiocb)
{
	struct lpfc_vport *vport = cmdiocb->vport;
	struct lpfc_iocbq *abtsiocbp;
	IOCB_t *icmd = NULL;
	IOCB_t *iabt = NULL;
	int retval = IOCB_ERROR;

	/*
	 * There are certain command types we don't want to abort.  And we
	 * don't want to abort commands that are already in the process of
	 * being aborted.
	 */
	icmd = &cmdiocb->iocb;
	if (icmd->ulpCommand == CMD_ABORT_XRI_CN ||
	    icmd->ulpCommand == CMD_CLOSE_XRI_CN ||
	    (cmdiocb->iocb_flag & LPFC_DRIVER_ABORTED) != 0)
		return 0;

	/* If we're unloading, don't abort iocb on the ELS ring, but change the
	 * callback so that nothing happens when it finishes.
	 */
	if ((vport->load_flag & FC_UNLOADING) &&
	    (pring->ringno == LPFC_ELS_RING)) {
		if (cmdiocb->iocb_flag & LPFC_IO_FABRIC)
			cmdiocb->fabric_iocb_cmpl = lpfc_ignore_els_cmpl;
		else
			cmdiocb->iocb_cmpl = lpfc_ignore_els_cmpl;
		goto abort_iotag_exit;
	}

	/* issue ABTS for this IOCB based on iotag */
	abtsiocbp = __lpfc_sli_get_iocbq(phba);
	if (abtsiocbp == NULL)
		return 0;

	/* This signals the response to set the correct status
	 * before calling the completion handler.
	 */
	cmdiocb->iocb_flag |= LPFC_DRIVER_ABORTED;

	iabt = &abtsiocbp->iocb;
	iabt->un.acxri.abortType = ABORT_TYPE_ABTS;
	iabt->un.acxri.abortContextTag = icmd->ulpContext;
	iabt->un.acxri.abortIoTag = icmd->ulpIoTag;
	iabt->ulpLe = 1;
	iabt->ulpClass = icmd->ulpClass;

	if (phba->link_state >= LPFC_LINK_UP)
		iabt->ulpCommand = CMD_ABORT_XRI_CN;
	else
		iabt->ulpCommand = CMD_CLOSE_XRI_CN;

	abtsiocbp->iocb_cmpl = lpfc_sli_abort_els_cmpl;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_SLI,
			 "0339 Abort xri x%x, original iotag x%x, "
			 "abort cmd iotag x%x\n",
			 iabt->un.acxri.abortContextTag,
			 iabt->un.acxri.abortIoTag, abtsiocbp->iotag);
	retval = __lpfc_sli_issue_iocb(phba, pring, abtsiocbp, 0);

	if (retval)
		__lpfc_sli_release_iocbq(phba, abtsiocbp);
abort_iotag_exit:
	/*
	 * Caller to this routine should check for IOCB_ERROR
	 * and handle it properly.  This routine no longer removes
	 * iocb off txcmplq and call compl in case of IOCB_ERROR.
	 */
	return retval;
}

/**
 * lpfc_sli_validate_fcp_iocb: Filtering function, used to find commands
 *                    associated with a vport/SCSI target/lun.
 * @iocbq: Pointer to driver iocb object.
 * @vport: Pointer to driver virtual port object.
 * @tgt_id: SCSI ID of the target.
 * @lun_id: LUN ID of the scsi device.
 * @ctx_cmd: LPFC_CTX_LUN/LPFC_CTX_TGT/LPFC_CTX_HOST
 *
 * This function acts as iocb filter for functions which abort or count
 * all FCP iocbs pending on a lun/SCSI target/SCSI host. It will return
 * 0 if the filtering criteria is met for the given iocb and will return
 * 1 if the filtering criteria is not met.
 * If ctx_cmd == LPFC_CTX_LUN, the function returns 0 only if the
 * given iocb is for the SCSI device specified by vport, tgt_id and
 * lun_id parameter.
 * If ctx_cmd == LPFC_CTX_TGT,  the function returns 0 only if the
 * given iocb is for the SCSI target specified by vport and tgt_id
 * parameters.
 * If ctx_cmd == LPFC_CTX_HOST, the function returns 0 only if the
 * given iocb is for the SCSI host associated with the given vport.
 * This function is called with no locks held.
 **/
static int
lpfc_sli_validate_fcp_iocb(struct lpfc_iocbq *iocbq, struct lpfc_vport *vport,
			   uint16_t tgt_id, uint64_t lun_id,
			   lpfc_ctx_cmd ctx_cmd)
{
	struct lpfc_scsi_buf *lpfc_cmd;
	int rc = 1;

	if (!(iocbq->iocb_flag &  LPFC_IO_FCP))
		return rc;

	if (iocbq->vport != vport)
		return rc;

	lpfc_cmd = container_of(iocbq, struct lpfc_scsi_buf, cur_iocbq);

	if (lpfc_cmd->pCmd == NULL)
		return rc;

	switch (ctx_cmd) {
	case LPFC_CTX_LUN:
		if ((lpfc_cmd->rdata->pnode) &&
		    (lpfc_cmd->rdata->pnode->nlp_sid == tgt_id) &&
		    (scsilun_to_int(&lpfc_cmd->fcp_cmnd->fcp_lun) == lun_id))
			rc = 0;
		break;
	case LPFC_CTX_TGT:
		if ((lpfc_cmd->rdata->pnode) &&
		    (lpfc_cmd->rdata->pnode->nlp_sid == tgt_id))
			rc = 0;
		break;
	case LPFC_CTX_HOST:
		rc = 0;
		break;
	default:
		printk(KERN_ERR "%s: Unknown context cmd type, value %d\n",
			__func__, ctx_cmd);
		break;
	}

	return rc;
}

/**
 * lpfc_sli_sum_iocb: Function to count the number of FCP iocbs pending.
 * @vport: Pointer to virtual port.
 * @tgt_id: SCSI ID of the target.
 * @lun_id: LUN ID of the scsi device.
 * @ctx_cmd: LPFC_CTX_LUN/LPFC_CTX_TGT/LPFC_CTX_HOST.
 *
 * This function returns number of FCP commands pending for the vport.
 * When ctx_cmd == LPFC_CTX_LUN, the function returns number of FCP
 * commands pending on the vport associated with SCSI device specified
 * by tgt_id and lun_id parameters.
 * When ctx_cmd == LPFC_CTX_TGT, the function returns number of FCP
 * commands pending on the vport associated with SCSI target specified
 * by tgt_id parameter.
 * When ctx_cmd == LPFC_CTX_HOST, the function returns number of FCP
 * commands pending on the vport.
 * This function returns the number of iocbs which satisfy the filter.
 * This function is called without any lock held.
 **/
int
lpfc_sli_sum_iocb(struct lpfc_vport *vport, uint16_t tgt_id, uint64_t lun_id,
		  lpfc_ctx_cmd ctx_cmd)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_iocbq *iocbq;
	int sum, i;

	for (i = 1, sum = 0; i <= phba->sli.last_iotag; i++) {
		iocbq = phba->sli.iocbq_lookup[i];

		if (lpfc_sli_validate_fcp_iocb (iocbq, vport, tgt_id, lun_id,
						ctx_cmd) == 0)
			sum++;
	}

	return sum;
}

/**
 * lpfc_sli_abort_fcp_cmpl: Completion handler function for an aborted
 *       FCP iocb.
 * @phba: Pointer to HBA context object
 * @cmdiocb: Pointer to command iocb object.
 * @rspiocb: Pointer to response iocb object.
 *
 * This function is called when an aborted FCP iocb completes. This
 * function is called by the ring event handler with no lock held.
 * This function frees the iocb.
 **/
void
lpfc_sli_abort_fcp_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			struct lpfc_iocbq *rspiocb)
{
	lpfc_sli_release_iocbq(phba, cmdiocb);
	return;
}

/**
 * lpfc_sli_abort_iocb: This function issue abort for all SCSI commands
 *          pending on a SCSI host(vport)/target/lun.
 * @vport: Pointer to virtual port.
 * @pring: Pointer to driver SLI ring object.
 * @tgt_id: SCSI ID of the target.
 * @lun_id: LUN ID of the scsi device.
 * @abort_cmd: LPFC_CTX_LUN/LPFC_CTX_TGT/LPFC_CTX_HOST.
 *
 * This function sends an abort command for every SCSI command
 * associated with the given virtual port pending on the ring
 * filtered by lpfc_sli_validate_fcp_iocb function.
 * When abort_cmd == LPFC_CTX_LUN, the function sends abort only to the
 * FCP iocbs associated with lun specified by tgt_id and lun_id
 * parameters
 * When abort_cmd == LPFC_CTX_TGT, the function sends abort only to the
 * FCP iocbs associated with SCSI target specified by tgt_id parameter.
 * When abort_cmd == LPFC_CTX_HOST, the function sends abort to all
 * FCP iocbs associated with virtual port.
 * This function returns number of iocbs it failed to abort.
 * This function is called with no locks held.
 **/
int
lpfc_sli_abort_iocb(struct lpfc_vport *vport, struct lpfc_sli_ring *pring,
		    uint16_t tgt_id, uint64_t lun_id, lpfc_ctx_cmd abort_cmd)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_iocbq *iocbq;
	struct lpfc_iocbq *abtsiocb;
	IOCB_t *cmd = NULL;
	int errcnt = 0, ret_val = 0;
	int i;

	for (i = 1; i <= phba->sli.last_iotag; i++) {
		iocbq = phba->sli.iocbq_lookup[i];

		if (lpfc_sli_validate_fcp_iocb(iocbq, vport, tgt_id, lun_id,
					       abort_cmd) != 0)
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
		abtsiocb->vport = phba->pport;

		if (lpfc_is_link_up(phba))
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

/**
 * lpfc_sli_wake_iocb_wait: iocb completion handler for iocb issued using
 *                          lpfc_sli_issue_iocb_wait.
 * @phba: Pointer to HBA context object.
 * @cmdiocbq: Pointer to command iocb.
 * @rspiocbq: Pointer to response iocb.
 *
 * This function is the completion handler for iocbs issued using
 * lpfc_sli_issue_iocb_wait function. This function is called by the
 * ring event handler function without any lock held. This function
 * can be called from both worker thread context and interrupt
 * context. This function also can be called from other thread which
 * cleans up the SLI layer objects.
 * This function copy the contents of the response iocb to the
 * response iocb memory object provided by the caller of
 * lpfc_sli_issue_iocb_wait and then wakes up the thread which
 * sleeps for the iocb completion.
 **/
static void
lpfc_sli_wake_iocb_wait(struct lpfc_hba *phba,
			struct lpfc_iocbq *cmdiocbq,
			struct lpfc_iocbq *rspiocbq)
{
	wait_queue_head_t *pdone_q;
	unsigned long iflags;

	spin_lock_irqsave(&phba->hbalock, iflags);
	cmdiocbq->iocb_flag |= LPFC_IO_WAKE;
	if (cmdiocbq->context2 && rspiocbq)
		memcpy(&((struct lpfc_iocbq *)cmdiocbq->context2)->iocb,
		       &rspiocbq->iocb, sizeof(IOCB_t));

	pdone_q = cmdiocbq->context_un.wait_queue;
	if (pdone_q)
		wake_up(pdone_q);
	spin_unlock_irqrestore(&phba->hbalock, iflags);
	return;
}

/**
 * lpfc_sli_issue_iocb_wait: Synchronous function to issue iocb commands.
 * @phba: Pointer to HBA context object..
 * @pring: Pointer to sli ring.
 * @piocb: Pointer to command iocb.
 * @prspiocbq: Pointer to response iocb.
 * @timeout: Timeout in number of seconds.
 *
 * This function issues the iocb to firmware and waits for the
 * iocb to complete. If the iocb command is not
 * completed within timeout seconds, it returns IOCB_TIMEDOUT.
 * Caller should not free the iocb resources if this function
 * returns IOCB_TIMEDOUT.
 * The function waits for the iocb completion using an
 * non-interruptible wait.
 * This function will sleep while waiting for iocb completion.
 * So, this function should not be called from any context which
 * does not allow sleeping. Due to the same reason, this function
 * cannot be called with interrupt disabled.
 * This function assumes that the iocb completions occur while
 * this function sleep. So, this function cannot be called from
 * the thread which process iocb completion for this ring.
 * This function clears the iocb_flag of the iocb object before
 * issuing the iocb and the iocb completion handler sets this
 * flag and wakes this thread when the iocb completes.
 * The contents of the response iocb will be copied to prspiocbq
 * by the completion handler when the command completes.
 * This function returns IOCB_SUCCESS when success.
 * This function is called with no lock held.
 **/
int
lpfc_sli_issue_iocb_wait(struct lpfc_hba *phba,
			 struct lpfc_sli_ring *pring,
			 struct lpfc_iocbq *piocb,
			 struct lpfc_iocbq *prspiocbq,
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
		timeleft = wait_event_timeout(done_q,
				piocb->iocb_flag & LPFC_IO_WAKE,
				timeout_req);

		if (piocb->iocb_flag & LPFC_IO_WAKE) {
			lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
					"0331 IOCB wake signaled\n");
		} else if (timeleft == 0) {
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"0338 IOCB wait timeout error - no "
					"wake response Data x%x\n", timeout);
			retval = IOCB_TIMEDOUT;
		} else {
			lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
					"0330 IOCB wake NOT set, "
					"Data x%x x%lx\n",
					timeout, (timeleft / jiffies));
			retval = IOCB_TIMEDOUT;
		}
	} else {
		lpfc_printf_log(phba, KERN_INFO, LOG_SLI,
				"0332 IOCB wait issue failed, Data x%x\n",
				retval);
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

/**
 * lpfc_sli_issue_mbox_wait: Synchronous function to issue mailbox.
 * @phba: Pointer to HBA context object.
 * @pmboxq: Pointer to driver mailbox object.
 * @timeout: Timeout in number of seconds.
 *
 * This function issues the mailbox to firmware and waits for the
 * mailbox command to complete. If the mailbox command is not
 * completed within timeout seconds, it returns MBX_TIMEOUT.
 * The function waits for the mailbox completion using an
 * interruptible wait. If the thread is woken up due to a
 * signal, MBX_TIMEOUT error is returned to the caller. Caller
 * should not free the mailbox resources, if this function returns
 * MBX_TIMEOUT.
 * This function will sleep while waiting for mailbox completion.
 * So, this function should not be called from any context which
 * does not allow sleeping. Due to the same reason, this function
 * cannot be called with interrupt disabled.
 * This function assumes that the mailbox completion occurs while
 * this function sleep. So, this function cannot be called from
 * the worker thread which processes mailbox completion.
 * This function is called in the context of HBA management
 * applications.
 * This function returns MBX_SUCCESS when successful.
 * This function is called with no lock held.
 **/
int
lpfc_sli_issue_mbox_wait(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmboxq,
			 uint32_t timeout)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(done_q);
	int retval;
	unsigned long flag;

	/* The caller must leave context1 empty. */
	if (pmboxq->context1)
		return MBX_NOT_FINISHED;

	pmboxq->mbox_flag &= ~LPFC_MBX_WAKE;
	/* setup wake call as IOCB callback */
	pmboxq->mbox_cmpl = lpfc_sli_wake_mbox_wait;
	/* setup context field to pass wait_queue pointer to wake function  */
	pmboxq->context1 = &done_q;

	/* now issue the command */
	retval = lpfc_sli_issue_mbox(phba, pmboxq, MBX_NOWAIT);

	if (retval == MBX_BUSY || retval == MBX_SUCCESS) {
		wait_event_interruptible_timeout(done_q,
				pmboxq->mbox_flag & LPFC_MBX_WAKE,
				timeout * HZ);

		spin_lock_irqsave(&phba->hbalock, flag);
		pmboxq->context1 = NULL;
		/*
		 * if LPFC_MBX_WAKE flag is set the mailbox is completed
		 * else do not free the resources.
		 */
		if (pmboxq->mbox_flag & LPFC_MBX_WAKE)
			retval = MBX_SUCCESS;
		else {
			retval = MBX_TIMEOUT;
			pmboxq->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
		}
		spin_unlock_irqrestore(&phba->hbalock, flag);
	}

	return retval;
}

/**
 * lpfc_sli_flush_mbox_queue: mailbox queue cleanup function.
 * @phba: Pointer to HBA context.
 *
 * This function is called to cleanup any pending mailbox
 * objects in the driver queue before bringing the HBA offline.
 * This function is called while resetting the HBA.
 * The function is called without any lock held. The function
 * takes hbalock to update SLI data structure.
 * This function returns 1 when there is an active mailbox
 * command pending else returns 0.
 **/
int
lpfc_sli_flush_mbox_queue(struct lpfc_hba * phba)
{
	struct lpfc_vport *vport = phba->pport;
	int i = 0;
	uint32_t ha_copy;

	while (phba->sli.sli_flag & LPFC_SLI_MBOX_ACTIVE && !vport->stopped) {
		if (i++ > LPFC_MBOX_TMO * 1000)
			return 1;

		/*
		 * Call lpfc_sli_handle_mb_event only if a mailbox cmd
		 * did finish. This way we won't get the misleading
		 * "Stray Mailbox Interrupt" message.
		 */
		spin_lock_irq(&phba->hbalock);
		ha_copy = phba->work_ha;
		phba->work_ha &= ~HA_MBATT;
		spin_unlock_irq(&phba->hbalock);

		if (ha_copy & HA_MBATT)
			if (lpfc_sli_handle_mb_event(phba) == 0)
				i = 0;

		msleep(1);
	}

	return (phba->sli.sli_flag & LPFC_SLI_MBOX_ACTIVE) ? 1 : 0;
}

/**
 * lpfc_sli_check_eratt: check error attention events
 * @phba: Pointer to HBA context.
 *
 * This function is called form timer soft interrupt context to check HBA's
 * error attention register bit for error attention events.
 *
 * This fucntion returns 1 when there is Error Attention in the Host Attention
 * Register and returns 0 otherwise.
 **/
int
lpfc_sli_check_eratt(struct lpfc_hba *phba)
{
	uint32_t ha_copy;

	/* If PCI channel is offline, don't process it */
	if (unlikely(pci_channel_offline(phba->pcidev)))
		return 0;

	/* If somebody is waiting to handle an eratt, don't process it
	 * here. The brdkill function will do this.
	 */
	if (phba->link_flag & LS_IGNORE_ERATT)
		return 0;

	/* Check if interrupt handler handles this ERATT */
	spin_lock_irq(&phba->hbalock);
	if (phba->hba_flag & HBA_ERATT_HANDLED) {
		/* Interrupt handler has handled ERATT */
		spin_unlock_irq(&phba->hbalock);
		return 0;
	}

	/* Read chip Host Attention (HA) register */
	ha_copy = readl(phba->HAregaddr);
	if (ha_copy & HA_ERATT) {
		/* Read host status register to retrieve error event */
		lpfc_sli_read_hs(phba);
		/* Set the driver HA work bitmap */
		phba->work_ha |= HA_ERATT;
		/* Indicate polling handles this ERATT */
		phba->hba_flag |= HBA_ERATT_HANDLED;
		spin_unlock_irq(&phba->hbalock);
		return 1;
	}
	spin_unlock_irq(&phba->hbalock);
	return 0;
}

/**
 * lpfc_sp_intr_handler: The slow-path interrupt handler of lpfc driver.
 * @irq: Interrupt number.
 * @dev_id: The device context pointer.
 *
 * This function is directly called from the PCI layer as an interrupt
 * service routine when the device is enabled with MSI-X multi-message
 * interrupt mode and there are slow-path events in the HBA. However,
 * when the device is enabled with either MSI or Pin-IRQ interrupt mode,
 * this function is called as part of the device-level interrupt handler.
 * When the PCI slot is in error recovery or the HBA is undergoing
 * initialization, the interrupt handler will not process the interrupt.
 * The link attention and ELS ring attention events are handled by the
 * worker thread. The interrupt handler signals the worker thread and
 * and returns for these events. This function is called without any
 * lock held. It gets the hbalock to access and update SLI data
 * structures.
 *
 * This function returns IRQ_HANDLED when interrupt is handled else it
 * returns IRQ_NONE.
 **/
irqreturn_t
lpfc_sp_intr_handler(int irq, void *dev_id)
{
	struct lpfc_hba  *phba;
	uint32_t ha_copy;
	uint32_t work_ha_copy;
	unsigned long status;
	unsigned long iflag;
	uint32_t control;

	MAILBOX_t *mbox, *pmbox;
	struct lpfc_vport *vport;
	struct lpfc_nodelist *ndlp;
	struct lpfc_dmabuf *mp;
	LPFC_MBOXQ_t *pmb;
	int rc;

	/*
	 * Get the driver's phba structure from the dev_id and
	 * assume the HBA is not interrupting.
	 */
	phba = (struct lpfc_hba *)dev_id;

	if (unlikely(!phba))
		return IRQ_NONE;

	/*
	 * Stuff needs to be attented to when this function is invoked as an
	 * individual interrupt handler in MSI-X multi-message interrupt mode
	 */
	if (phba->intr_type == MSIX) {
		/* If the pci channel is offline, ignore all the interrupts */
		if (unlikely(pci_channel_offline(phba->pcidev)))
			return IRQ_NONE;
		/* Update device-level interrupt statistics */
		phba->sli.slistat.sli_intr++;
		/* Ignore all interrupts during initialization. */
		if (unlikely(phba->link_state < LPFC_LINK_DOWN))
			return IRQ_NONE;
		/* Need to read HA REG for slow-path events */
		spin_lock_irqsave(&phba->hbalock, iflag);
		ha_copy = readl(phba->HAregaddr);
		/* If somebody is waiting to handle an eratt don't process it
		 * here. The brdkill function will do this.
		 */
		if (phba->link_flag & LS_IGNORE_ERATT)
			ha_copy &= ~HA_ERATT;
		/* Check the need for handling ERATT in interrupt handler */
		if (ha_copy & HA_ERATT) {
			if (phba->hba_flag & HBA_ERATT_HANDLED)
				/* ERATT polling has handled ERATT */
				ha_copy &= ~HA_ERATT;
			else
				/* Indicate interrupt handler handles ERATT */
				phba->hba_flag |= HBA_ERATT_HANDLED;
		}
		/* Clear up only attention source related to slow-path */
		writel((ha_copy & (HA_MBATT | HA_R2_CLR_MSK)),
			phba->HAregaddr);
		readl(phba->HAregaddr); /* flush */
		spin_unlock_irqrestore(&phba->hbalock, iflag);
	} else
		ha_copy = phba->ha_copy;

	work_ha_copy = ha_copy & phba->work_ha_mask;

	if (work_ha_copy) {
		if (work_ha_copy & HA_LATT) {
			if (phba->sli.sli_flag & LPFC_PROCESS_LA) {
				/*
				 * Turn off Link Attention interrupts
				 * until CLEAR_LA done
				 */
				spin_lock_irqsave(&phba->hbalock, iflag);
				phba->sli.sli_flag &= ~LPFC_PROCESS_LA;
				control = readl(phba->HCregaddr);
				control &= ~HC_LAINT_ENA;
				writel(control, phba->HCregaddr);
				readl(phba->HCregaddr); /* flush */
				spin_unlock_irqrestore(&phba->hbalock, iflag);
			}
			else
				work_ha_copy &= ~HA_LATT;
		}

		if (work_ha_copy & ~(HA_ERATT | HA_MBATT | HA_LATT)) {
			/*
			 * Turn off Slow Rings interrupts, LPFC_ELS_RING is
			 * the only slow ring.
			 */
			status = (work_ha_copy &
				(HA_RXMASK  << (4*LPFC_ELS_RING)));
			status >>= (4*LPFC_ELS_RING);
			if (status & HA_RXMASK) {
				spin_lock_irqsave(&phba->hbalock, iflag);
				control = readl(phba->HCregaddr);

				lpfc_debugfs_slow_ring_trc(phba,
				"ISR slow ring:   ctl:x%x stat:x%x isrcnt:x%x",
				control, status,
				(uint32_t)phba->sli.slistat.sli_intr);

				if (control & (HC_R0INT_ENA << LPFC_ELS_RING)) {
					lpfc_debugfs_slow_ring_trc(phba,
						"ISR Disable ring:"
						"pwork:x%x hawork:x%x wait:x%x",
						phba->work_ha, work_ha_copy,
						(uint32_t)((unsigned long)
						&phba->work_waitq));

					control &=
					    ~(HC_R0INT_ENA << LPFC_ELS_RING);
					writel(control, phba->HCregaddr);
					readl(phba->HCregaddr); /* flush */
				}
				else {
					lpfc_debugfs_slow_ring_trc(phba,
						"ISR slow ring:   pwork:"
						"x%x hawork:x%x wait:x%x",
						phba->work_ha, work_ha_copy,
						(uint32_t)((unsigned long)
						&phba->work_waitq));
				}
				spin_unlock_irqrestore(&phba->hbalock, iflag);
			}
		}
		spin_lock_irqsave(&phba->hbalock, iflag);
		if (work_ha_copy & HA_ERATT)
			lpfc_sli_read_hs(phba);
		if ((work_ha_copy & HA_MBATT) && (phba->sli.mbox_active)) {
			pmb = phba->sli.mbox_active;
			pmbox = &pmb->mb;
			mbox = phba->mbox;
			vport = pmb->vport;

			/* First check out the status word */
			lpfc_sli_pcimem_bcopy(mbox, pmbox, sizeof(uint32_t));
			if (pmbox->mbxOwner != OWN_HOST) {
				spin_unlock_irqrestore(&phba->hbalock, iflag);
				/*
				 * Stray Mailbox Interrupt, mbxCommand <cmd>
				 * mbxStatus <status>
				 */
				lpfc_printf_log(phba, KERN_ERR, LOG_MBOX |
						LOG_SLI,
						"(%d):0304 Stray Mailbox "
						"Interrupt mbxCommand x%x "
						"mbxStatus x%x\n",
						(vport ? vport->vpi : 0),
						pmbox->mbxCommand,
						pmbox->mbxStatus);
				/* clear mailbox attention bit */
				work_ha_copy &= ~HA_MBATT;
			} else {
				phba->sli.mbox_active = NULL;
				spin_unlock_irqrestore(&phba->hbalock, iflag);
				phba->last_completion_time = jiffies;
				del_timer(&phba->sli.mbox_tmo);
				if (pmb->mbox_cmpl) {
					lpfc_sli_pcimem_bcopy(mbox, pmbox,
							MAILBOX_CMD_SIZE);
				}
				if (pmb->mbox_flag & LPFC_MBX_IMED_UNREG) {
					pmb->mbox_flag &= ~LPFC_MBX_IMED_UNREG;

					lpfc_debugfs_disc_trc(vport,
						LPFC_DISC_TRC_MBOX_VPORT,
						"MBOX dflt rpi: : "
						"status:x%x rpi:x%x",
						(uint32_t)pmbox->mbxStatus,
						pmbox->un.varWords[0], 0);

					if (!pmbox->mbxStatus) {
						mp = (struct lpfc_dmabuf *)
							(pmb->context1);
						ndlp = (struct lpfc_nodelist *)
							pmb->context2;

						/* Reg_LOGIN of dflt RPI was
						 * successful. new lets get
						 * rid of the RPI using the
						 * same mbox buffer.
						 */
						lpfc_unreg_login(phba,
							vport->vpi,
							pmbox->un.varWords[0],
							pmb);
						pmb->mbox_cmpl =
							lpfc_mbx_cmpl_dflt_rpi;
						pmb->context1 = mp;
						pmb->context2 = ndlp;
						pmb->vport = vport;
						rc = lpfc_sli_issue_mbox(phba,
								pmb,
								MBX_NOWAIT);
						if (rc != MBX_BUSY)
							lpfc_printf_log(phba,
							KERN_ERR,
							LOG_MBOX | LOG_SLI,
							"0350 rc should have"
							"been MBX_BUSY");
						goto send_current_mbox;
					}
				}
				spin_lock_irqsave(
						&phba->pport->work_port_lock,
						iflag);
				phba->pport->work_port_events &=
					~WORKER_MBOX_TMO;
				spin_unlock_irqrestore(
						&phba->pport->work_port_lock,
						iflag);
				lpfc_mbox_cmpl_put(phba, pmb);
			}
		} else
			spin_unlock_irqrestore(&phba->hbalock, iflag);

		if ((work_ha_copy & HA_MBATT) &&
		    (phba->sli.mbox_active == NULL)) {
send_current_mbox:
			/* Process next mailbox command if there is one */
			do {
				rc = lpfc_sli_issue_mbox(phba, NULL,
							 MBX_NOWAIT);
			} while (rc == MBX_NOT_FINISHED);
			if (rc != MBX_SUCCESS)
				lpfc_printf_log(phba, KERN_ERR, LOG_MBOX |
						LOG_SLI, "0349 rc should be "
						"MBX_SUCCESS");
		}

		spin_lock_irqsave(&phba->hbalock, iflag);
		phba->work_ha |= work_ha_copy;
		spin_unlock_irqrestore(&phba->hbalock, iflag);
		lpfc_worker_wake_up(phba);
	}
	return IRQ_HANDLED;

} /* lpfc_sp_intr_handler */

/**
 * lpfc_fp_intr_handler: The fast-path interrupt handler of lpfc driver.
 * @irq: Interrupt number.
 * @dev_id: The device context pointer.
 *
 * This function is directly called from the PCI layer as an interrupt
 * service routine when the device is enabled with MSI-X multi-message
 * interrupt mode and there is a fast-path FCP IOCB ring event in the
 * HBA. However, when the device is enabled with either MSI or Pin-IRQ
 * interrupt mode, this function is called as part of the device-level
 * interrupt handler. When the PCI slot is in error recovery or the HBA
 * is undergoing initialization, the interrupt handler will not process
 * the interrupt. The SCSI FCP fast-path ring event are handled in the
 * intrrupt context. This function is called without any lock held. It
 * gets the hbalock to access and update SLI data structures.
 *
 * This function returns IRQ_HANDLED when interrupt is handled else it
 * returns IRQ_NONE.
 **/
irqreturn_t
lpfc_fp_intr_handler(int irq, void *dev_id)
{
	struct lpfc_hba  *phba;
	uint32_t ha_copy;
	unsigned long status;
	unsigned long iflag;

	/* Get the driver's phba structure from the dev_id and
	 * assume the HBA is not interrupting.
	 */
	phba = (struct lpfc_hba *) dev_id;

	if (unlikely(!phba))
		return IRQ_NONE;

	/*
	 * Stuff needs to be attented to when this function is invoked as an
	 * individual interrupt handler in MSI-X multi-message interrupt mode
	 */
	if (phba->intr_type == MSIX) {
		/* If pci channel is offline, ignore all the interrupts */
		if (unlikely(pci_channel_offline(phba->pcidev)))
			return IRQ_NONE;
		/* Update device-level interrupt statistics */
		phba->sli.slistat.sli_intr++;
		/* Ignore all interrupts during initialization. */
		if (unlikely(phba->link_state < LPFC_LINK_DOWN))
			return IRQ_NONE;
		/* Need to read HA REG for FCP ring and other ring events */
		ha_copy = readl(phba->HAregaddr);
		/* Clear up only attention source related to fast-path */
		spin_lock_irqsave(&phba->hbalock, iflag);
		writel((ha_copy & (HA_R0_CLR_MSK | HA_R1_CLR_MSK)),
			phba->HAregaddr);
		readl(phba->HAregaddr); /* flush */
		spin_unlock_irqrestore(&phba->hbalock, iflag);
	} else
		ha_copy = phba->ha_copy;

	/*
	 * Process all events on FCP ring. Take the optimized path for FCP IO.
	 */
	ha_copy &= ~(phba->work_ha_mask);

	status = (ha_copy & (HA_RXMASK << (4*LPFC_FCP_RING)));
	status >>= (4*LPFC_FCP_RING);
	if (status & HA_RXMASK)
		lpfc_sli_handle_fast_ring_event(phba,
						&phba->sli.ring[LPFC_FCP_RING],
						status);

	if (phba->cfg_multi_ring_support == 2) {
		/*
		 * Process all events on extra ring. Take the optimized path
		 * for extra ring IO.
		 */
		status = (ha_copy & (HA_RXMASK << (4*LPFC_EXTRA_RING)));
		status >>= (4*LPFC_EXTRA_RING);
		if (status & HA_RXMASK) {
			lpfc_sli_handle_fast_ring_event(phba,
					&phba->sli.ring[LPFC_EXTRA_RING],
					status);
		}
	}
	return IRQ_HANDLED;
}  /* lpfc_fp_intr_handler */

/**
 * lpfc_intr_handler: The device-level interrupt handler of lpfc driver.
 * @irq: Interrupt number.
 * @dev_id: The device context pointer.
 *
 * This function is the device-level interrupt handler called from the PCI
 * layer when either MSI or Pin-IRQ interrupt mode is enabled and there is
 * an event in the HBA which requires driver attention. This function
 * invokes the slow-path interrupt attention handling function and fast-path
 * interrupt attention handling function in turn to process the relevant
 * HBA attention events. This function is called without any lock held. It
 * gets the hbalock to access and update SLI data structures.
 *
 * This function returns IRQ_HANDLED when interrupt is handled, else it
 * returns IRQ_NONE.
 **/
irqreturn_t
lpfc_intr_handler(int irq, void *dev_id)
{
	struct lpfc_hba  *phba;
	irqreturn_t sp_irq_rc, fp_irq_rc;
	unsigned long status1, status2;

	/*
	 * Get the driver's phba structure from the dev_id and
	 * assume the HBA is not interrupting.
	 */
	phba = (struct lpfc_hba *) dev_id;

	if (unlikely(!phba))
		return IRQ_NONE;

	/* If the pci channel is offline, ignore all the interrupts. */
	if (unlikely(pci_channel_offline(phba->pcidev)))
		return IRQ_NONE;

	/* Update device level interrupt statistics */
	phba->sli.slistat.sli_intr++;

	/* Ignore all interrupts during initialization. */
	if (unlikely(phba->link_state < LPFC_LINK_DOWN))
		return IRQ_NONE;

	spin_lock(&phba->hbalock);
	phba->ha_copy = readl(phba->HAregaddr);
	if (unlikely(!phba->ha_copy)) {
		spin_unlock(&phba->hbalock);
		return IRQ_NONE;
	} else if (phba->ha_copy & HA_ERATT) {
		if (phba->hba_flag & HBA_ERATT_HANDLED)
			/* ERATT polling has handled ERATT */
			phba->ha_copy &= ~HA_ERATT;
		else
			/* Indicate interrupt handler handles ERATT */
			phba->hba_flag |= HBA_ERATT_HANDLED;
	}

	/* Clear attention sources except link and error attentions */
	writel((phba->ha_copy & ~(HA_LATT | HA_ERATT)), phba->HAregaddr);
	readl(phba->HAregaddr); /* flush */
	spin_unlock(&phba->hbalock);

	/*
	 * Invokes slow-path host attention interrupt handling as appropriate.
	 */

	/* status of events with mailbox and link attention */
	status1 = phba->ha_copy & (HA_MBATT | HA_LATT | HA_ERATT);

	/* status of events with ELS ring */
	status2 = (phba->ha_copy & (HA_RXMASK  << (4*LPFC_ELS_RING)));
	status2 >>= (4*LPFC_ELS_RING);

	if (status1 || (status2 & HA_RXMASK))
		sp_irq_rc = lpfc_sp_intr_handler(irq, dev_id);
	else
		sp_irq_rc = IRQ_NONE;

	/*
	 * Invoke fast-path host attention interrupt handling as appropriate.
	 */

	/* status of events with FCP ring */
	status1 = (phba->ha_copy & (HA_RXMASK << (4*LPFC_FCP_RING)));
	status1 >>= (4*LPFC_FCP_RING);

	/* status of events with extra ring */
	if (phba->cfg_multi_ring_support == 2) {
		status2 = (phba->ha_copy & (HA_RXMASK << (4*LPFC_EXTRA_RING)));
		status2 >>= (4*LPFC_EXTRA_RING);
	} else
		status2 = 0;

	if ((status1 & HA_RXMASK) || (status2 & HA_RXMASK))
		fp_irq_rc = lpfc_fp_intr_handler(irq, dev_id);
	else
		fp_irq_rc = IRQ_NONE;

	/* Return device-level interrupt handling status */
	return (sp_irq_rc == IRQ_HANDLED) ? sp_irq_rc : fp_irq_rc;
}  /* lpfc_intr_handler */
