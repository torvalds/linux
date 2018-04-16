/**
 * @file mlan_txrx.c
 *
 *  @brief This file contains the handling of TX/RX in MLAN
 *
 *  Copyright (C) 2009-2017, Marvell International Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 */

/*************************************************************
Change Log:
    05/11/2009: initial version
************************************************************/

#include "mlan.h"
#ifdef STA_SUPPORT
#include "mlan_join.h"
#endif
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_wmm.h"
#include "mlan_sdio.h"

/********************************************************
			Local Variables
********************************************************/

/********************************************************
			Global Variables
********************************************************/

/********************************************************
			Local Functions
********************************************************/

/********************************************************
			Global Functions
********************************************************/
/**
 *   @brief This function processes the received buffer
 *
 *   @param pmadapter A pointer to mlan_adapter
 *   @param pmbuf     A pointer to the received buffer
 *
 *   @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_handle_rx_packet(pmlan_adapter pmadapter, pmlan_buffer pmbuf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private priv = MNULL;
	RxPD *prx_pd;
#ifdef DEBUG_LEVEL1
	t_u32 sec = 0, usec = 0;
#endif

	ENTER();

	prx_pd = (RxPD *)(pmbuf->pbuf + pmbuf->data_offset);
	/* Get the BSS number from RxPD, get corresponding priv */
	priv = wlan_get_priv_by_id(pmadapter, prx_pd->bss_num & BSS_NUM_MASK,
				   prx_pd->bss_type);
	if (!priv)
		priv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	if (!priv) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	pmbuf->bss_index = priv->bss_index;
	PRINTM_GET_SYS_TIME(MDATA, &sec, &usec);
	PRINTM_NETINTF(MDATA, priv);
	PRINTM(MDATA, "%lu.%06lu : Data <= FW\n", sec, usec);
	ret = priv->ops.process_rx_packet(pmadapter, pmbuf);

done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function checks the conditions and sends packet to device
 *
 *  @param priv	   A pointer to mlan_private structure
 *  @param pmbuf   A pointer to the mlan_buffer for process
 *  @param tx_param A pointer to mlan_tx_param structure
 *
 *  @return         MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise failure
 */
mlan_status
wlan_process_tx(pmlan_private priv, pmlan_buffer pmbuf, mlan_tx_param *tx_param)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_adapter pmadapter = priv->adapter;
	t_u8 *head_ptr = MNULL;
#ifdef DEBUG_LEVEL1
	t_u32 sec = 0, usec = 0;
#endif
#ifdef STA_SUPPORT
	TxPD *plocal_tx_pd = MNULL;
#endif

	ENTER();
	head_ptr = (t_u8 *)priv->ops.process_txpd(priv, pmbuf);
	if (!head_ptr) {
		pmbuf->status_code = MLAN_ERROR_PKT_INVALID;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
#ifdef STA_SUPPORT
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_STA)
		plocal_tx_pd = (TxPD *)(head_ptr + priv->intf_hr_len);
#endif

	ret = wlan_sdio_host_to_card(pmadapter, MLAN_TYPE_DATA, pmbuf,
				     tx_param);
done:
	switch (ret) {
	case MLAN_STATUS_RESOURCE:
#ifdef STA_SUPPORT
		if ((GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_STA) &&
		    pmadapter->pps_uapsd_mode &&
		    (pmadapter->tx_lock_flag == MTRUE)) {
			pmadapter->tx_lock_flag = MFALSE;
			plocal_tx_pd->flags = 0;
		}
#endif
		PRINTM(MINFO, "MLAN_STATUS_RESOURCE is returned\n");
		break;
	case MLAN_STATUS_FAILURE:
		pmadapter->data_sent = MFALSE;
		PRINTM(MERROR, "Error: host_to_card failed: 0x%X\n", ret);
		pmadapter->dbg.num_tx_host_to_card_failure++;
		pmbuf->status_code = MLAN_ERROR_DATA_TX_FAIL;
		wlan_write_data_complete(pmadapter, pmbuf, ret);
		break;
	case MLAN_STATUS_PENDING:
		DBG_HEXDUMP(MDAT_D, "Tx", head_ptr + priv->intf_hr_len,
			    MIN(pmbuf->data_len + sizeof(TxPD),
				MAX_DATA_DUMP_LEN));
		break;
	case MLAN_STATUS_SUCCESS:
		DBG_HEXDUMP(MDAT_D, "Tx", head_ptr + priv->intf_hr_len,
			    MIN(pmbuf->data_len + sizeof(TxPD),
				MAX_DATA_DUMP_LEN));
		wlan_write_data_complete(pmadapter, pmbuf, ret);
		break;
	default:
		break;
	}

	if ((ret == MLAN_STATUS_SUCCESS) || (ret == MLAN_STATUS_PENDING)) {
		PRINTM_GET_SYS_TIME(MDATA, &sec, &usec);
		PRINTM_NETINTF(MDATA, priv);
		PRINTM(MDATA, "%lu.%06lu : Data => FW\n", sec, usec);
	}
	LEAVE();
	return ret;
}

/**
 *  @brief Packet send completion handling
 *
 *  @param pmadapter		A pointer to mlan_adapter structure
 *  @param pmbuf		A pointer to mlan_buffer structure
 *  @param status		Callback status
 *
 *  @return			MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_write_data_complete(IN pmlan_adapter pmadapter,
			 IN pmlan_buffer pmbuf, IN mlan_status status)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb;

	ENTER();

	MASSERT(pmadapter && pmbuf);
	if (!pmadapter || !pmbuf) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	pcb = &pmadapter->callbacks;

	if ((pmbuf->buf_type == MLAN_BUF_TYPE_DATA) ||
	    (pmbuf->buf_type == MLAN_BUF_TYPE_RAW_DATA)) {
		PRINTM(MINFO, "wlan_write_data_complete: DATA %p\n", pmbuf);
		if (pmbuf->flags & MLAN_BUF_FLAG_MOAL_TX_BUF) {
			/* pmbuf was allocated by MOAL */
			pcb->moal_send_packet_complete(pmadapter->pmoal_handle,
						       pmbuf, status);
		} else {
			/* pmbuf was allocated by MLAN */
			wlan_free_mlan_buffer(pmadapter, pmbuf);
		}
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Packet receive completion callback handler
 *
 *  @param pmadapter		A pointer to mlan_adapter structure
 *  @param pmbuf		A pointer to mlan_buffer structure
 *  @param status		Callback status
 *
 *  @return			MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_recv_packet_complete(IN pmlan_adapter pmadapter,
			  IN pmlan_buffer pmbuf, IN mlan_status status)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb;

	ENTER();

	MASSERT(pmadapter && pmbuf);
	if (!pmadapter || !pmbuf) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	pcb = &pmadapter->callbacks;
	MASSERT(pmbuf->bss_index < pmadapter->priv_num);

	if (pmbuf->pparent) {
	/** we will free the pparaent at the end of deaggr */
		wlan_free_mlan_buffer(pmadapter, pmbuf);
	} else {
		wlan_free_mlan_buffer(pmadapter, pmbuf);
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Add packet to Bypass TX queue
 *
 *  @param pmadapter  Pointer to the mlan_adapter driver data struct
 *  @param pmbuf      Pointer to the mlan_buffer data struct
 *
 *  @return         N/A
 */
t_void
wlan_add_buf_bypass_txqueue(mlan_adapter *pmadapter, pmlan_buffer pmbuf)
{
	pmlan_private priv = pmadapter->priv[pmbuf->bss_index];
	ENTER();

	if (pmbuf->buf_type != MLAN_BUF_TYPE_RAW_DATA)
		pmbuf->buf_type = MLAN_BUF_TYPE_DATA;
	pmadapter->callbacks.moal_spin_lock(pmadapter->pmoal_handle,
					    priv->bypass_txq.plock);
	pmadapter->bypass_pkt_count++;
	util_enqueue_list_tail(pmadapter->pmoal_handle, &priv->bypass_txq,
			       (pmlan_linked_list)pmbuf, MNULL, MNULL);
	pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
					      priv->bypass_txq.plock);
	LEAVE();
}

/**
 *  @brief Check if packets are available in Bypass TX queue
 *
 *  @param pmadapter  Pointer to the mlan_adapter driver data struct
 *
 *  @return         MFALSE if not empty; MTRUE if empty
 */
INLINE t_u8
wlan_bypass_tx_list_empty(mlan_adapter *pmadapter)
{
	return (pmadapter->bypass_pkt_count) ? MFALSE : MTRUE;
}

/**
 *  @brief Clean up the By-pass TX queue
 *
 *  @param priv     Pointer to the mlan_private data struct
 *
 *  @return      N/A
 */
t_void
wlan_cleanup_bypass_txq(mlan_private *priv)
{
	pmlan_buffer pmbuf;
	mlan_adapter *pmadapter = priv->adapter;
	ENTER();
	pmadapter->callbacks.moal_spin_lock(pmadapter->pmoal_handle,
					    priv->bypass_txq.plock);
	while ((pmbuf =
		(pmlan_buffer)util_peek_list(pmadapter->pmoal_handle,
					     &priv->bypass_txq, MNULL,
					     MNULL))) {
		util_unlink_list(pmadapter->pmoal_handle, &priv->bypass_txq,
				 (pmlan_linked_list)pmbuf, MNULL, MNULL);
		wlan_write_data_complete(pmadapter, pmbuf, MLAN_STATUS_FAILURE);
		pmadapter->bypass_pkt_count--;
	}
	pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
					      priv->bypass_txq.plock);
	LEAVE();
}

/**
 *  @brief Transmit the By-passed packet awaiting in by-pass queue
 *
 *  @param pmadapter Pointer to the mlan_adapter driver data struct
 *
 *  @return        N/A
 */
t_void
wlan_process_bypass_tx(pmlan_adapter pmadapter)
{
	pmlan_buffer pmbuf;
	mlan_tx_param tx_param;
	mlan_status status = MLAN_STATUS_SUCCESS;
	pmlan_private priv;
	int j = 0;
	ENTER();
	for (j = 0; j < pmadapter->priv_num; ++j) {
		priv = pmadapter->priv[j];
		if (priv) {
			pmbuf = (pmlan_buffer)util_dequeue_list(pmadapter->
								pmoal_handle,
								&priv->
								bypass_txq,
								pmadapter->
								callbacks.
								moal_spin_lock,
								pmadapter->
								callbacks.
								moal_spin_unlock);
			if (pmbuf) {
				PRINTM(MINFO, "Dequeuing bypassed packet %p\n",
				       pmbuf);
				/* XXX: nex_pkt_len ??? */
				tx_param.next_pkt_len = 0;
				status = wlan_process_tx(pmadapter->
							 priv[pmbuf->bss_index],
							 pmbuf, &tx_param);

				if (status == MLAN_STATUS_RESOURCE) {
					/* Queue the packet again so that it will be TX'ed later */
					util_enqueue_list_head(pmadapter->
							       pmoal_handle,
							       &priv->
							       bypass_txq,
							       (pmlan_linked_list)
							       pmbuf,
							       pmadapter->
							       callbacks.
							       moal_spin_lock,
							       pmadapter->
							       callbacks.
							       moal_spin_unlock);
				} else {
					pmadapter->callbacks.
						moal_spin_lock(pmadapter->
							       pmoal_handle,
							       priv->bypass_txq.
							       plock);
					pmadapter->bypass_pkt_count--;
					pmadapter->callbacks.
						moal_spin_unlock(pmadapter->
								 pmoal_handle,
								 priv->
								 bypass_txq.
								 plock);
				}
				break;
			} else {
				PRINTM(MINFO, "Nothing to send\n");
			}
		}
	}
	LEAVE();
}
