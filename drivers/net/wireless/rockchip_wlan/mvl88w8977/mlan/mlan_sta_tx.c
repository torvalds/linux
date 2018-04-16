/** @file mlan_sta_tx.c
 *
 *  @brief This file contains the handling of data packet
 *  transmission in MLAN module.
 *
 *  Copyright (C) 2008-2017, Marvell International Ltd.
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

/********************************************************
Change log:
    10/21/2008: initial version
********************************************************/

#include "mlan.h"
#include "mlan_join.h"
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
		Global functions
********************************************************/
/**
 *  @brief This function fill the txpd for tx packet
 *
 *  @param priv    A pointer to mlan_private structure
 *  @param pmbuf   A pointer to the mlan_buffer for process
 *
 *  @return        headptr or MNULL
 */
t_void *
wlan_ops_sta_process_txpd(IN t_void *priv, IN pmlan_buffer pmbuf)
{
	mlan_private *pmpriv = (mlan_private *)priv;
	pmlan_adapter pmadapter = pmpriv->adapter;
	TxPD *plocal_tx_pd;
	t_u8 *head_ptr = MNULL;
	t_u32 pkt_type;
	t_u32 tx_control;

	ENTER();

	if (!pmbuf->data_len) {
		PRINTM(MERROR, "STA Tx Error: Invalid packet length: %d\n",
		       pmbuf->data_len);
		pmbuf->status_code = MLAN_ERROR_PKT_SIZE_INVALID;
		goto done;
	}
	if (pmbuf->buf_type == MLAN_BUF_TYPE_RAW_DATA) {
		memcpy(pmpriv->adapter, &pkt_type,
		       pmbuf->pbuf + pmbuf->data_offset, sizeof(pkt_type));
		memcpy(pmpriv->adapter, &tx_control,
		       pmbuf->pbuf + pmbuf->data_offset + sizeof(pkt_type),
		       sizeof(tx_control));
		pmbuf->data_offset += sizeof(pkt_type) + sizeof(tx_control);
		pmbuf->data_len -= sizeof(pkt_type) + sizeof(tx_control);
	}

	if (pmbuf->data_offset < (sizeof(TxPD) + pmpriv->intf_hr_len +
				  DMA_ALIGNMENT)) {
		PRINTM(MERROR,
		       "not enough space for TxPD: headroom=%d pkt_len=%d, required=%d\n",
		       pmbuf->data_offset, pmbuf->data_len,
		       sizeof(TxPD) + pmpriv->intf_hr_len + DMA_ALIGNMENT);
		pmbuf->status_code = MLAN_ERROR_PKT_SIZE_INVALID;
		goto done;
	}

	/* head_ptr should be aligned */
	head_ptr =
		pmbuf->pbuf + pmbuf->data_offset - sizeof(TxPD) -
		pmpriv->intf_hr_len;
	head_ptr = (t_u8 *)((t_ptr)head_ptr & ~((t_ptr)(DMA_ALIGNMENT - 1)));

	plocal_tx_pd = (TxPD *)(head_ptr + pmpriv->intf_hr_len);
	memset(pmadapter, plocal_tx_pd, 0, sizeof(TxPD));
	/* Set the BSS number to TxPD */
	plocal_tx_pd->bss_num = GET_BSS_NUM(pmpriv);
	plocal_tx_pd->bss_type = pmpriv->bss_type;
	plocal_tx_pd->tx_pkt_length = (t_u16)pmbuf->data_len;

	plocal_tx_pd->priority = (t_u8)pmbuf->priority;
	plocal_tx_pd->pkt_delay_2ms =
		wlan_wmm_compute_driver_packet_delay(pmpriv, pmbuf);

	if (plocal_tx_pd->priority <
	    NELEMENTS(pmpriv->wmm.user_pri_pkt_tx_ctrl))
		/*
		 * Set the priority specific tx_control field, setting of 0 will
		 *   cause the default value to be used later in this function
		 */
		plocal_tx_pd->tx_control
			=
			pmpriv->wmm.user_pri_pkt_tx_ctrl[plocal_tx_pd->
							 priority];
	if (pmadapter->pps_uapsd_mode) {
		if (MTRUE == wlan_check_last_packet_indication(pmpriv)) {
			pmadapter->tx_lock_flag = MTRUE;
			plocal_tx_pd->flags =
				MRVDRV_TxPD_POWER_MGMT_LAST_PACKET;
		}
	}
	if (pmbuf->flags & MLAN_BUF_FLAG_TDLS)
		plocal_tx_pd->flags |= MRVDRV_TxPD_FLAGS_TDLS_PACKET;
	/* Offset of actual data */
	plocal_tx_pd->tx_pkt_offset =
		(t_u16)((t_ptr)pmbuf->pbuf + pmbuf->data_offset -
			(t_ptr)plocal_tx_pd);

	if (!plocal_tx_pd->tx_control) {
		/* TxCtrl set by user or default */
		plocal_tx_pd->tx_control = pmpriv->pkt_tx_ctrl;
	}

	if (pmbuf->buf_type == MLAN_BUF_TYPE_RAW_DATA) {
		plocal_tx_pd->tx_pkt_type = (t_u16)pkt_type;
		plocal_tx_pd->tx_control = tx_control;
	}

	if (pmbuf->flags & MLAN_BUF_FLAG_TX_STATUS) {
		plocal_tx_pd->tx_control_1 |= pmbuf->tx_seq_num << 8;
		plocal_tx_pd->flags |= MRVDRV_TxPD_FLAGS_TX_PACKET_STATUS;
	}
	if (pmbuf->flags & MLAN_BUF_FLAG_TX_CTRL) {
		if (pmbuf->u.tx_info.data_rate) {
			plocal_tx_pd->tx_control |=
				(wlan_ieee_rateid_to_mrvl_rateid
				 (pmpriv, pmbuf->u.tx_info.data_rate,
				  MNULL) << 16);
			plocal_tx_pd->tx_control |= TXPD_TXRATE_ENABLE;
		}
		plocal_tx_pd->tx_control_1 |= pmbuf->u.tx_info.channel << 21;
		if (pmbuf->u.tx_info.bw) {
			plocal_tx_pd->tx_control_1 |= pmbuf->u.tx_info.bw << 16;
			plocal_tx_pd->tx_control_1 |= TXPD_BW_ENABLE;
		}
		if (pmbuf->u.tx_info.tx_power.tp.hostctl)
			plocal_tx_pd->tx_control |=
				pmbuf->u.tx_info.tx_power.val;
		if (pmbuf->u.tx_info.retry_limit) {
			plocal_tx_pd->tx_control |=
				pmbuf->u.tx_info.retry_limit << 8;
			plocal_tx_pd->tx_control |= TXPD_RETRY_ENABLE;
		}
	}
	endian_convert_TxPD(plocal_tx_pd);
	/* Adjust the data offset and length to include TxPD in pmbuf */
	pmbuf->data_len += pmbuf->data_offset;
	pmbuf->data_offset = (t_u32)(head_ptr - pmbuf->pbuf);
	pmbuf->data_len -= pmbuf->data_offset;

done:
	LEAVE();
	return head_ptr;
}

/**
 *  @brief This function tells firmware to send a NULL data packet.
 *
 *  @param priv     A pointer to mlan_private structure
 *  @param flags    Transmit Pkt Flags
 *
 *  @return         MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise failure
 */
mlan_status
wlan_send_null_packet(pmlan_private priv, t_u8 flags)
{
	pmlan_adapter pmadapter = MNULL;
	TxPD *ptx_pd;
/* sizeof(TxPD) + Interface specific header */
#define NULL_PACKET_HDR 256
	t_u32 data_len = NULL_PACKET_HDR;
	pmlan_buffer pmbuf = MNULL;
	t_u8 *ptr;
	mlan_status ret = MLAN_STATUS_SUCCESS;
#ifdef DEBUG_LEVEL1
	t_u32 sec = 0, usec = 0;
#endif

	ENTER();

	if (!priv) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	pmadapter = priv->adapter;

	if (pmadapter->surprise_removed == MTRUE) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	if (priv->media_connected == MFALSE) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	if (pmadapter->data_sent == MTRUE) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	pmbuf = wlan_alloc_mlan_buffer(pmadapter, data_len, 0,
				       MOAL_MALLOC_BUFFER);
	if (!pmbuf) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	memset(pmadapter, pmbuf->pbuf, 0, data_len);
	pmbuf->bss_index = priv->bss_index;
	pmbuf->buf_type = MLAN_BUF_TYPE_DATA;
	pmbuf->flags |= MLAN_BUF_FLAG_NULL_PKT;
	ptr = pmbuf->pbuf + pmbuf->data_offset;
	pmbuf->data_len = sizeof(TxPD) + priv->intf_hr_len;
	ptx_pd = (TxPD *)(ptr + priv->intf_hr_len);
	ptx_pd->tx_control = priv->pkt_tx_ctrl;
	ptx_pd->flags = flags;
	ptx_pd->priority = WMM_HIGHEST_PRIORITY;
	ptx_pd->tx_pkt_offset = sizeof(TxPD);
	/* Set the BSS number to TxPD */
	ptx_pd->bss_num = GET_BSS_NUM(priv);
	ptx_pd->bss_type = priv->bss_type;

	endian_convert_TxPD(ptx_pd);

	ret = wlan_sdio_host_to_card(pmadapter, MLAN_TYPE_DATA, pmbuf, MNULL);

	switch (ret) {
	case MLAN_STATUS_RESOURCE:
		wlan_free_mlan_buffer(pmadapter, pmbuf);
		PRINTM(MERROR, "STA Tx Error: Failed to send NULL packet!\n");
		pmadapter->dbg.num_tx_host_to_card_failure++;
		goto done;
	case MLAN_STATUS_FAILURE:
		pmadapter->data_sent = MFALSE;
		wlan_free_mlan_buffer(pmadapter, pmbuf);
		PRINTM(MERROR, "STA Tx Error: Failed to send NULL packet!\n");
		pmadapter->dbg.num_tx_host_to_card_failure++;
		goto done;
	case MLAN_STATUS_SUCCESS:
		wlan_free_mlan_buffer(pmadapter, pmbuf);
		PRINTM(MINFO, "STA Tx: Successfully send the NULL packet\n");
		pmadapter->tx_lock_flag = MTRUE;
		break;
	case MLAN_STATUS_PENDING:
		pmadapter->tx_lock_flag = MTRUE;
		break;
	default:
		break;
	}

	PRINTM_GET_SYS_TIME(MDATA, &sec, &usec);
	PRINTM_NETINTF(MDATA, priv);
	PRINTM(MDATA, "%lu.%06lu : Null data => FW\n", sec, usec);
	DBG_HEXDUMP(MDAT_D, "Null data", ptr, sizeof(TxPD) + priv->intf_hr_len);
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function checks if we need to send last packet indication.
 *
 *  @param priv    A pointer to mlan_private structure
 *
 *  @return        MTRUE or MFALSE
 */
t_u8
wlan_check_last_packet_indication(pmlan_private priv)
{
	pmlan_adapter pmadapter = priv->adapter;
	t_u8 ret = MFALSE;
	t_u8 prop_ps = MTRUE;

	ENTER();

	if (!pmadapter->sleep_period.period) {
		LEAVE();
		return ret;
	}
	if (wlan_bypass_tx_list_empty(pmadapter) &&
	    wlan_wmm_lists_empty(pmadapter)) {
		if (((priv->curr_bss_params.wmm_uapsd_enabled == MTRUE) &&
		     priv->wmm_qosinfo) || prop_ps)

			ret = MTRUE;
	}
	if (ret && !pmadapter->cmd_sent && !pmadapter->curr_cmd
	    && !wlan_is_cmd_pending(pmadapter)) {
		pmadapter->delay_null_pkt = MFALSE;
		ret = MTRUE;
	} else {
		ret = MFALSE;
		pmadapter->delay_null_pkt = MTRUE;
	}
	LEAVE();
	return ret;
}
