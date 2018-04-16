/** @file mlan_wmm.c
 *
 *  @brief This file contains functions for WMM.
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
    10/24/2008: initial version
********************************************************/

#include "mlan.h"
#ifdef STA_SUPPORT
#include "mlan_join.h"
#endif
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_wmm.h"
#include "mlan_11n.h"
#include "mlan_sdio.h"

/********************************************************
			Local Variables
********************************************************/

/** Maximum value FW can accept for driver delay in packet transmission */
#define DRV_PKT_DELAY_TO_FW_MAX   512

/*
 * Upper and Lower threshold for packet queuing in the driver

 *    - When the number of packets queued reaches the upper limit,
 *      the driver will stop the net queue in the app/kernel space.

 *    - When the number of packets drops beneath the lower limit after
 *      having reached the upper limit, the driver will restart the net
 *      queue.
 */

/** Lower threshold for packet queuing in the driver.
  * When the number of packets drops beneath the lower limit after having
  * reached the upper limit, the driver will restart the net queue.
  */
#define WMM_QUEUED_PACKET_LOWER_LIMIT   180

/** Upper threshold for packet queuing in the driver.
  * When the number of packets queued reaches the upper limit, the driver
  * will stop the net queue in the app/kernel space.
  */
#define WMM_QUEUED_PACKET_UPPER_LIMIT   200

/** Offset for TOS field in the IP header */
#define IPTOS_OFFSET 5

/** WMM information IE */
static const t_u8 wmm_info_ie[] = { WMM_IE, 0x07,
	0x00, 0x50, 0xf2, 0x02,
	0x00, 0x01, 0x00
};

/** Type enumeration of WMM AC_QUEUES */
typedef MLAN_PACK_START enum _wmm_ac_e {
	AC_BE,
	AC_BK,
	AC_VI,
	AC_VO
} MLAN_PACK_END wmm_ac_e;

/**
 * AC Priorities go from AC_BK to AC_VO.  The ACI enumeration for AC_BK (1)
 *   is higher than the enumeration for AC_BE (0); hence the needed
 *   mapping conversion for wmm AC to priority Queue Index
 */
static const t_u8 wmm_aci_to_qidx_map[] = { WMM_AC_BE,
	WMM_AC_BK,
	WMM_AC_VI,
	WMM_AC_VO
};

/**
 * This table will be used to store the tid values based on ACs.
 * It is initialized to default values per TID.
 */
t_u8 tos_to_tid[] = {
	/* TID        DSCP_P2   DSCP_P1  DSCP_P0   WMM_AC   */
	0x01,			/*    0         1        0       AC_BK   */
	0x02,			/*    0         0        0       AC_BK   */
	0x00,			/*    0         0        1       AC_BE   */
	0x03,			/*    0         1        1       AC_BE   */
	0x04,			/*    1         0        0       AC_VI   */
	0x05,			/*    1         0        1       AC_VI   */
	0x06,			/*    1         1        0       AC_VO   */
	0x07			/*    1         1        1       AC_VO   */
};

/**
 * This table inverses the tos_to_tid operation to get a priority
 * which is in sequential order, and can be compared.
 * Use this to compare the priority of two different TIDs.
 */
t_u8 tos_to_tid_inv[] = { 0x02,	/* from tos_to_tid[2] = 0 */
	0x00,			/* from tos_to_tid[0] = 1 */
	0x01,			/* from tos_to_tid[1] = 2 */
	0x03,
	0x04,
	0x05,
	0x06,
	0x07
};

/**
 * This table will provide the tid value for given ac. This table does not
 * change and will be used to copy back the default values to tos_to_tid in
 * case of disconnect.
 */
const t_u8 ac_to_tid[4][2] = { {1, 2}, {0, 3}, {4, 5}, {6, 7} };

/* Map of TOS UP values to WMM AC */
static const mlan_wmm_ac_e tos_to_ac[] = { WMM_AC_BE,
	WMM_AC_BK,
	WMM_AC_BK,
	WMM_AC_BE,
	WMM_AC_VI,
	WMM_AC_VI,
	WMM_AC_VO,
	WMM_AC_VO
};

raListTbl *wlan_wmm_get_ralist_node(pmlan_private priv, t_u8 tid,
				    t_u8 *ra_addr);

/********************************************************
			Local Functions
********************************************************/
#ifdef DEBUG_LEVEL2
/**
 *  @brief Debug print function to display the priority parameters for a WMM AC
 *
 *  @param pac_param	Pointer to the AC parameters to display
 *
 *  @return		N/A
 */
static void
wlan_wmm_ac_debug_print(const IEEEtypes_WmmAcParameters_t *pac_param)
{
	const char *ac_str[] = { "BK", "BE", "VI", "VO" };

	ENTER();

	PRINTM(MINFO, "WMM AC_%s: ACI=%d, ACM=%d, Aifsn=%d, "
	       "EcwMin=%d, EcwMax=%d, TxopLimit=%d\n",
	       ac_str[wmm_aci_to_qidx_map[pac_param->aci_aifsn.aci]],
	       pac_param->aci_aifsn.aci, pac_param->aci_aifsn.acm,
	       pac_param->aci_aifsn.aifsn, pac_param->ecw.ecw_min,
	       pac_param->ecw.ecw_max,
	       wlan_le16_to_cpu(pac_param->tx_op_limit));

	LEAVE();
}

/** Print the WMM AC for debug purpose */
#define PRINTM_AC(pac_param) wlan_wmm_ac_debug_print(pac_param)
#else
/** Print the WMM AC for debug purpose */
#define PRINTM_AC(pac_param)
#endif

/**
 *  @brief Allocate route address
 *
 *  @param pmadapter       Pointer to the mlan_adapter structure
 *  @param ra              Pointer to the route address
 *
 *  @return         ra_list
 */
static
raListTbl *
wlan_wmm_allocate_ralist_node(pmlan_adapter pmadapter, t_u8 *ra)
{
	raListTbl *ra_list = MNULL;

	ENTER();

	if (pmadapter->callbacks.
	    moal_malloc(pmadapter->pmoal_handle, sizeof(raListTbl),
			MLAN_MEM_DEF, (t_u8 **)&ra_list)) {
		PRINTM(MERROR, "Fail to allocate ra_list\n");
		goto done;
	}
	util_init_list((pmlan_linked_list)ra_list);
	util_init_list_head((t_void *)pmadapter->pmoal_handle,
			    &ra_list->buf_head, MFALSE,
			    pmadapter->callbacks.moal_init_lock);

	memcpy(pmadapter, ra_list->ra, ra, MLAN_MAC_ADDR_LENGTH);

	ra_list->del_ba_count = 0;
	ra_list->total_pkts = 0;
	ra_list->tx_pause = 0;
	PRINTM(MINFO, "RAList: Allocating buffers for TID %p\n", ra_list);
done:
	LEAVE();
	return ra_list;
}

/**
 *  @brief Add packet to TDLS pending TX queue
 *
 *  @param priv		  A pointer to mlan_private
 *  @param pmbuf      Pointer to the mlan_buffer data struct
 *
 *  @return           N/A
 */
static t_void
wlan_add_buf_tdls_txqueue(pmlan_private priv, pmlan_buffer pmbuf)
{
	mlan_adapter *pmadapter = priv->adapter;
	ENTER();
	util_enqueue_list_tail(pmadapter->pmoal_handle, &priv->tdls_pending_txq,
			       (pmlan_linked_list)pmbuf,
			       pmadapter->callbacks.moal_spin_lock,
			       pmadapter->callbacks.moal_spin_unlock);
	LEAVE();
}

/**
 *  @brief Clean up the tdls pending TX queue
 *
 *  @param priv		A pointer to mlan_private
 *
 *  @return      N/A
 */
static t_void
wlan_cleanup_tdls_txq(pmlan_private priv)
{
	pmlan_buffer pmbuf;
	mlan_adapter *pmadapter = priv->adapter;
	ENTER();

	pmadapter->callbacks.moal_spin_lock(pmadapter->pmoal_handle,
					    priv->tdls_pending_txq.plock);
	while ((pmbuf =
		(pmlan_buffer)util_peek_list(pmadapter->pmoal_handle,
					     &priv->tdls_pending_txq, MNULL,
					     MNULL))) {
		util_unlink_list(pmadapter->pmoal_handle,
				 &priv->tdls_pending_txq,
				 (pmlan_linked_list)pmbuf, MNULL, MNULL);
		wlan_write_data_complete(pmadapter, pmbuf, MLAN_STATUS_FAILURE);
	}
	pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
					      priv->tdls_pending_txq.plock);
	LEAVE();
}

/**
 * @brief Map ACs to TID
 *
 * @param priv             Pointer to the mlan_private driver data struct
 * @param queue_priority   Queue_priority structure
 *
 * @return 	   N/A
 */
static void
wlan_wmm_queue_priorities_tid(pmlan_private priv, t_u8 queue_priority[])
{
	int i;

	ENTER();

	for (i = 0; i < 4; ++i) {
		tos_to_tid[7 - (i * 2)] = ac_to_tid[queue_priority[i]][1];
		tos_to_tid[6 - (i * 2)] = ac_to_tid[queue_priority[i]][0];
	}

	for (i = 0; i < MAX_NUM_TID; i++)
		tos_to_tid_inv[tos_to_tid[i]] = (t_u8)i;

	/* in case priorities have changed, force highest priority so
	 * next packet will check from top to re-establish the highest
	 */
	util_scalar_write(priv->adapter->pmoal_handle,
			  &priv->wmm.highest_queued_prio,
			  HIGH_PRIO_TID,
			  priv->adapter->callbacks.moal_spin_lock,
			  priv->adapter->callbacks.moal_spin_unlock);

	LEAVE();
}

/**
 *  @brief Evaluate whether or not an AC is to be downgraded
 *
 *  @param priv     Pointer to the mlan_private driver data struct
 *  @param eval_ac  AC to evaluate for downgrading
 *
 *  @return WMM AC  The eval_ac traffic is to be sent on.
 */
static mlan_wmm_ac_e
wlan_wmm_eval_downgrade_ac(pmlan_private priv, mlan_wmm_ac_e eval_ac)
{
	int down_ac;
	mlan_wmm_ac_e ret_ac;
	WmmAcStatus_t *pac_status;

	ENTER();

	pac_status = &priv->wmm.ac_status[eval_ac];

	if (pac_status->disabled == MFALSE) {
		LEAVE();
		/* Okay to use this AC, its enabled */
		return eval_ac;
	}

	/* Setup a default return value of the lowest priority */
	ret_ac = WMM_AC_BK;

	/*
	 * Find the highest AC that is enabled and does not require admission
	 * control.  The spec disallows downgrading to an AC, which is enabled
	 * due to a completed admission control.  Unadmitted traffic is not
	 * to be sent on an AC with admitted traffic.
	 */
	for (down_ac = WMM_AC_BK; down_ac < eval_ac; down_ac++) {
		pac_status = &priv->wmm.ac_status[down_ac];

		if ((pac_status->disabled == MFALSE)
		    && (pac_status->flow_required == MFALSE))
			/* AC is enabled and does not require admission control */
			ret_ac = (mlan_wmm_ac_e)down_ac;
	}

	LEAVE();
	return ret_ac;
}

/**
 *  @brief Convert the IP TOS field to an WMM AC Queue assignment
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param tos       IP TOS field
 *
 *  @return     WMM AC Queue mapping of the IP TOS field
 */
static mlan_wmm_ac_e INLINE
wlan_wmm_convert_tos_to_ac(pmlan_adapter pmadapter, t_u32 tos)
{
	ENTER();

	if (tos >= NELEMENTS(tos_to_ac)) {
		LEAVE();
		return WMM_AC_BE;
	}

	LEAVE();
	return tos_to_ac[tos];
}

/**
 *  @brief  Evaluate a given TID and downgrade it to a lower TID if the
 *          WMM Parameter IE received from the AP indicates that the AP
 *          is disabled (due to call admission control (ACM bit). Mapping
 *          of TID to AC is taken care internally
 *
 *  @param priv		Pointer to the mlan_private data struct
 *  @param tid      tid to evaluate for downgrading
 *
 *  @return       Same tid as input if downgrading not required or
 *                the tid the traffic for the given tid should be downgraded to
 */
static t_u8 INLINE
wlan_wmm_downgrade_tid(pmlan_private priv, t_u32 tid)
{
	mlan_wmm_ac_e ac_down;
	pmlan_adapter pmadapter = priv->adapter;

	ENTER();

	ac_down =
		priv->wmm.
		ac_down_graded_vals[wlan_wmm_convert_tos_to_ac(pmadapter, tid)];
	LEAVE();
	/*
	 * Send the index to tid array, picking from the array will be
	 * taken care by dequeuing function
	 */
	if (tid == 1 || tid == 2)
		return ac_to_tid[ac_down][(tid + 1) % 2];
	else if (tid >= MAX_NUM_TID)
		return ac_to_tid[ac_down][0];
	else
		return ac_to_tid[ac_down][tid % 2];
}

/**
 *  @brief Delete packets in RA node
 *
 *  @param priv         Pointer to the mlan_private driver data struct
 *  @param ra_list      Pointer to raListTbl
 *
 *  @return             N/A
 */
static INLINE void
wlan_wmm_del_pkts_in_ralist_node(pmlan_private priv, raListTbl *ra_list)
{
	pmlan_buffer pmbuf;
	pmlan_adapter pmadapter = priv->adapter;

	ENTER();
	while ((pmbuf =
		(pmlan_buffer)util_peek_list(pmadapter->pmoal_handle,
					     &ra_list->buf_head, MNULL,
					     MNULL))) {
		util_unlink_list(pmadapter->pmoal_handle, &ra_list->buf_head,
				 (pmlan_linked_list)pmbuf, MNULL, MNULL);
		wlan_write_data_complete(pmadapter, pmbuf, MLAN_STATUS_FAILURE);
	}
	util_free_list_head((t_void *)pmadapter->pmoal_handle,
			    &ra_list->buf_head,
			    pmadapter->callbacks.moal_free_lock);

	LEAVE();
}

/**
 *  @brief Delete packets in RA list
 *
 *  @param priv			Pointer to the mlan_private driver data struct
 *  @param ra_list_head	ra list header
 *
 *  @return		N/A
 */
static INLINE void
wlan_wmm_del_pkts_in_ralist(pmlan_private priv, mlan_list_head *ra_list_head)
{
	raListTbl *ra_list;

	ENTER();

	ra_list =
		(raListTbl *)util_peek_list(priv->adapter->pmoal_handle,
					    ra_list_head, MNULL, MNULL);

	while (ra_list && ra_list != (raListTbl *)ra_list_head) {
		wlan_wmm_del_pkts_in_ralist_node(priv, ra_list);

		ra_list = ra_list->pnext;
	}

	LEAVE();
}

/**
 *  @brief Clean up the wmm queue
 *
 *  @param priv  Pointer to the mlan_private driver data struct
 *
 *  @return      N/A
 */
static void
wlan_wmm_cleanup_queues(pmlan_private priv)
{
	int i;

	ENTER();

	for (i = 0; i < MAX_NUM_TID; i++) {
		wlan_wmm_del_pkts_in_ralist(priv,
					    &priv->wmm.tid_tbl_ptr[i].ra_list);
		priv->wmm.pkts_queued[i] = 0;
		priv->wmm.pkts_paused[i] = 0;
	}
	util_scalar_write(priv->adapter->pmoal_handle,
			  &priv->wmm.tx_pkts_queued, 0, MNULL, MNULL);
	util_scalar_write(priv->adapter->pmoal_handle,
			  &priv->wmm.highest_queued_prio, HIGH_PRIO_TID, MNULL,
			  MNULL);

	LEAVE();
}

/**
 *  @brief Delete all route address from RA list
 *
 *  @param priv     Pointer to the mlan_private driver data struct
 *
 *  @return         N/A
 */
static void
wlan_wmm_delete_all_ralist(pmlan_private priv)
{
	raListTbl *ra_list;
	int i;
	pmlan_adapter pmadapter = priv->adapter;

	ENTER();

	for (i = 0; i < MAX_NUM_TID; ++i) {
		PRINTM(MINFO, "RAList: Freeing buffers for TID %d\n", i);
		while ((ra_list =
			(raListTbl *)util_peek_list(pmadapter->pmoal_handle,
						    &priv->wmm.tid_tbl_ptr[i].
						    ra_list, MNULL, MNULL))) {
			util_unlink_list(pmadapter->pmoal_handle,
					 &priv->wmm.tid_tbl_ptr[i].ra_list,
					 (pmlan_linked_list)ra_list, MNULL,
					 MNULL);

			pmadapter->callbacks.moal_mfree(pmadapter->pmoal_handle,
							(t_u8 *)ra_list);
		}

		util_init_list((pmlan_linked_list)
			       &priv->wmm.tid_tbl_ptr[i].ra_list);
		priv->wmm.tid_tbl_ptr[i].ra_list_curr = MNULL;
	}

	LEAVE();
}

/**
 *   @brief Get queue RA pointer
 *
 *   @param priv     Pointer to the mlan_private driver data struct
 *   @param tid      TID
 *   @param ra_addr  Pointer to the route address
 *
 *   @return         ra_list
 */
static raListTbl *
wlan_wmm_get_queue_raptr(pmlan_private priv, t_u8 tid, t_u8 *ra_addr)
{
	raListTbl *ra_list;
#if defined(UAP_SUPPORT)
	t_u8 bcast_addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
#endif

	ENTER();
	ra_list = wlan_wmm_get_ralist_node(priv, tid, ra_addr);
	if (ra_list) {
		LEAVE();
		return ra_list;
	}
#if defined(UAP_SUPPORT)
	if ((GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) &&
	    (0 !=
	     memcmp(priv->adapter, ra_addr, bcast_addr, sizeof(bcast_addr)))) {
		if (MNULL == wlan_get_station_entry(priv, ra_addr)) {
			PRINTM(MDATA, "Drop packets to unknown station\n");
			LEAVE();
			return MNULL;
		}
	}
#endif
	wlan_ralist_add(priv, ra_addr);

	ra_list = wlan_wmm_get_ralist_node(priv, tid, ra_addr);
	LEAVE();
	return ra_list;
}

#ifdef STA_SUPPORT
/**
 *  @brief Sends wmmac host event
 *
 *  @param priv      Pointer to the mlan_private driver data struct
 *  @param type_str  Type of host event
 *  @param src_addr  Pointer to the source Address
 *  @param tid       TID
 *  @param up        User priority
 *  @param status    Status code or Reason code
 *
 *  @return     N/A
 */
static void
wlan_send_wmmac_host_event(pmlan_private priv,
			   char *type_str,
			   t_u8 *src_addr, t_u8 tid, t_u8 up, t_u8 status)
{
	t_u8 event_buf[100];
	mlan_event *pevent;
	t_u8 *pout_buf;

	ENTER();

	/* Format one of the following two output strings:
	 **    - TSPEC:ADDTS_RSP:[<status code>]:TID=X:UP=Y
	 **    - TSPEC:DELTS_RX:[<reason code>]:TID=X:UP=Y
	 */
	pevent = (mlan_event *)event_buf;
	pout_buf = pevent->event_buf;

	memcpy(priv->adapter, pout_buf, (t_u8 *)"TSPEC:", 6);
	pout_buf += 6;

	memcpy(priv->adapter, pout_buf, (t_u8 *)type_str,
	       wlan_strlen(type_str));
	pout_buf += wlan_strlen(type_str);

	*pout_buf++ = ':';
	*pout_buf++ = '[';

	if (status >= 100) {
		*pout_buf++ = (status / 100) + '0';
		status = (status % 100);
	}

	if (status >= 10) {
		*pout_buf++ = (status / 10) + '0';
		status = (status % 10);
	}

	*pout_buf++ = status + '0';

	memcpy(priv->adapter, pout_buf, (t_u8 *)"]:TID", 5);
	pout_buf += 5;
	*pout_buf++ = tid + '0';

	memcpy(priv->adapter, pout_buf, (t_u8 *)":UP", 3);
	pout_buf += 3;
	*pout_buf++ = up + '0';

	*pout_buf = '\0';

	pevent->bss_index = priv->bss_index;
	pevent->event_id = MLAN_EVENT_ID_DRV_REPORT_STRING;
	pevent->event_len = wlan_strlen((const char *)(pevent->event_buf));

	wlan_recv_event(priv, MLAN_EVENT_ID_DRV_REPORT_STRING, pevent);
	LEAVE();
}
#endif /* STA_SUPPORT */

/**
 *  @brief This function gets the highest priority list pointer
 *
 *  @param pmadapter      A pointer to mlan_adapter
 *  @param priv           A pointer to mlan_private
 *  @param tid            A pointer to return tid
 *
 *  @return             raListTbl
 */
static raListTbl *
wlan_wmm_get_highest_priolist_ptr(pmlan_adapter pmadapter,
				  pmlan_private *priv, int *tid)
{
	pmlan_private priv_tmp;
	raListTbl *ptr, *head;
	mlan_bssprio_node *bssprio_node, *bssprio_head;
	tid_tbl_t *tid_ptr;
	int i, j;
	int next_prio = 0;
	int next_tid = 0;
	ENTER();

	PRINTM(MDAT_D, "POP\n");
	for (j = pmadapter->priv_num - 1; j >= 0; --j) {
		if (!(util_peek_list(pmadapter->pmoal_handle,
				     &pmadapter->bssprio_tbl[j].bssprio_head,
				     MNULL, MNULL)))
			continue;

		if (pmadapter->bssprio_tbl[j].bssprio_cur ==
		    (mlan_bssprio_node *)
		    &pmadapter->bssprio_tbl[j].bssprio_head) {
			pmadapter->bssprio_tbl[j].bssprio_cur =
				pmadapter->bssprio_tbl[j].bssprio_cur->pnext;
		}

		bssprio_head
			= bssprio_node = pmadapter->bssprio_tbl[j].bssprio_cur;

		do {
			priv_tmp = bssprio_node->priv;

			if ((priv_tmp->port_ctrl_mode == MTRUE)
			    && (priv_tmp->port_open == MFALSE)) {
				PRINTM(MINFO, "get_highest_prio_ptr(): "
				       "PORT_CLOSED Ignore pkts from BSS%d\n",
				       priv_tmp->bss_index);
				/* Ignore data pkts from a BSS if port is closed */
				goto next_intf;
			}
			if (priv_tmp->tx_pause == MTRUE) {
				PRINTM(MINFO, "get_highest_prio_ptr(): "
				       "TX PASUE Ignore pkts from BSS%d\n",
				       priv_tmp->bss_index);
				/* Ignore data pkts from a BSS if tx pause */
				goto next_intf;
			}

			pmadapter->callbacks.moal_spin_lock(pmadapter->
							    pmoal_handle,
							    priv_tmp->wmm.
							    ra_list_spinlock);

			for (i = util_scalar_read(pmadapter->pmoal_handle,
						  &priv_tmp->wmm.
						  highest_queued_prio, MNULL,
						  MNULL); i >= LOW_PRIO_TID;
			     --i) {

				tid_ptr =
					&(priv_tmp)->wmm.
					tid_tbl_ptr[tos_to_tid[i]];
				if (!util_peek_list
				    (pmadapter->pmoal_handle, &tid_ptr->ra_list,
				     MNULL, MNULL))
					continue;

				/*
				 * Always choose the next ra we transmitted
				 * last time, this way we pick the ra's in
				 * round robin fashion.
				 */
				head = ptr = tid_ptr->ra_list_curr->pnext;
				if (ptr == (raListTbl *)&tid_ptr->ra_list)
					head = ptr = ptr->pnext;

				do {
					if (!ptr->tx_pause &&
					    util_peek_list(pmadapter->
							   pmoal_handle,
							   &ptr->buf_head,
							   MNULL, MNULL)) {

						/* Because WMM only support BK/BE/VI/VO, we have 8 tid
						 * We should balance the traffic of the same AC */
						if (i % 2)
							next_prio = i - 1;
						else
							next_prio = i + 1;
						next_tid =
							tos_to_tid[next_prio];
						if (priv_tmp->wmm.
						    pkts_queued[next_tid] &&
						    (priv_tmp->wmm.
						     pkts_queued[next_tid] >
						     priv_tmp->wmm.
						     pkts_paused[next_tid]))
							util_scalar_write
								(pmadapter->
								 pmoal_handle,
								 &priv_tmp->wmm.
								 highest_queued_prio,
								 next_prio,
								 MNULL, MNULL);
						else
							/* if highest_queued_prio > i, set it to i */
							util_scalar_conditional_write
								(pmadapter->
								 pmoal_handle,
								 &priv_tmp->wmm.
								 highest_queued_prio,
								 MLAN_SCALAR_COND_GREATER_THAN,
								 i, i, MNULL,
								 MNULL);
						*priv = priv_tmp;
						*tid = tos_to_tid[i];
						/* hold priv->ra_list_spinlock to maintain ptr */
						PRINTM(MDAT_D,
						       "get highest prio ptr %p, tid %d\n",
						       ptr, *tid);
						LEAVE();
						return ptr;
					}

					ptr = ptr->pnext;
					if (ptr ==
					    (raListTbl *)&tid_ptr->ra_list)
						ptr = ptr->pnext;
				} while (ptr != head);
			}

			/* If priv still has packets queued, reset to HIGH_PRIO_TID */
			if (util_scalar_read(pmadapter->pmoal_handle,
					     &priv_tmp->wmm.tx_pkts_queued,
					     MNULL, MNULL))
				util_scalar_write(pmadapter->pmoal_handle,
						  &priv_tmp->wmm.
						  highest_queued_prio,
						  HIGH_PRIO_TID, MNULL, MNULL);
			else
				/* No packet at any TID for this priv.  Mark as such to skip
				 * checking TIDs for this priv (until pkt is added). */
				util_scalar_write(pmadapter->pmoal_handle,
						  &priv_tmp->wmm.
						  highest_queued_prio,
						  NO_PKT_PRIO_TID, MNULL,
						  MNULL);

			pmadapter->callbacks.moal_spin_unlock(pmadapter->
							      pmoal_handle,
							      priv_tmp->wmm.
							      ra_list_spinlock);

next_intf:
			bssprio_node = bssprio_node->pnext;
			if (bssprio_node == (mlan_bssprio_node *)
			    &pmadapter->bssprio_tbl[j].bssprio_head)
				bssprio_node = bssprio_node->pnext;
			pmadapter->bssprio_tbl[j].bssprio_cur = bssprio_node;
		} while (bssprio_node != bssprio_head);
	}

	LEAVE();
	return MNULL;
}

/**
 *  @brief This function gets the number of packets in the Tx queue
 *
 *  @param priv           A pointer to mlan_private
 *  @param ptr            A pointer to RA list table
 *  @param max_buf_size   Maximum buffer size
 *
 *  @return             Packet count
 */
static int
wlan_num_pkts_in_txq(mlan_private *priv, raListTbl *ptr, int max_buf_size)
{
	int count = 0, total_size = 0;
	pmlan_buffer pmbuf;

	ENTER();

	for (pmbuf = (pmlan_buffer)ptr->buf_head.pnext;
	     pmbuf != (pmlan_buffer)(&ptr->buf_head); pmbuf = pmbuf->pnext) {

		total_size += pmbuf->data_len;
		if (total_size < max_buf_size)
			++count;
		else
			break;
	}

	LEAVE();
	return count;
}

/**
 *  @brief This function sends a single packet
 *
 *  @param priv         A pointer to mlan_private
 *  @param ptr          A pointer to RA list table
 *  @param ptrindex     ptr's TID index
 *
 *  @return             N/A
 */
static void INLINE
wlan_send_single_packet(pmlan_private priv, raListTbl *ptr, int ptrindex)
{
	pmlan_buffer pmbuf;
	pmlan_buffer pmbuf_next;
	mlan_tx_param tx_param;
	pmlan_adapter pmadapter = priv->adapter;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	pmbuf = (pmlan_buffer)util_dequeue_list(pmadapter->pmoal_handle,
						&ptr->buf_head, MNULL, MNULL);
	if (pmbuf) {
		PRINTM(MINFO, "Dequeuing the packet %p %p\n", ptr, pmbuf);
		priv->wmm.pkts_queued[ptrindex]--;
		util_scalar_decrement(pmadapter->pmoal_handle,
				      &priv->wmm.tx_pkts_queued, MNULL, MNULL);
		ptr->total_pkts--;
		pmbuf_next =
			(pmlan_buffer)util_peek_list(pmadapter->pmoal_handle,
						     &ptr->buf_head, MNULL,
						     MNULL);
		pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
						      priv->wmm.
						      ra_list_spinlock);

		tx_param.next_pkt_len = ((pmbuf_next)
					 ? pmbuf_next->data_len +
					 sizeof(TxPD) : 0);
		status = wlan_process_tx(priv, pmbuf, &tx_param);

		if (status == MLAN_STATUS_RESOURCE) {
	    /** Queue the packet back at the head */
			PRINTM(MDAT_D, "Queuing pkt back to raList %p %p\n",
			       ptr, pmbuf);
			pmadapter->callbacks.moal_spin_lock(pmadapter->
							    pmoal_handle,
							    priv->wmm.
							    ra_list_spinlock);

			if (!wlan_is_ralist_valid(priv, ptr, ptrindex)) {
				pmadapter->callbacks.
					moal_spin_unlock(pmadapter->
							 pmoal_handle,
							 priv->wmm.
							 ra_list_spinlock);
				wlan_write_data_complete(pmadapter, pmbuf,
							 MLAN_STATUS_FAILURE);
				LEAVE();
				return;
			}
			priv->wmm.pkts_queued[ptrindex]++;
			util_scalar_increment(pmadapter->pmoal_handle,
					      &priv->wmm.tx_pkts_queued, MNULL,
					      MNULL);
			util_enqueue_list_head(pmadapter->pmoal_handle,
					       &ptr->buf_head,
					       (pmlan_linked_list)pmbuf, MNULL,
					       MNULL);

			ptr->total_pkts++;
			pmbuf->flags |= MLAN_BUF_FLAG_REQUEUED_PKT;
			pmadapter->callbacks.moal_spin_unlock(pmadapter->
							      pmoal_handle,
							      priv->wmm.
							      ra_list_spinlock);
		} else {
			pmadapter->callbacks.moal_spin_lock(pmadapter->
							    pmoal_handle,
							    priv->wmm.
							    ra_list_spinlock);
			if (wlan_is_ralist_valid(priv, ptr, ptrindex)) {
				priv->wmm.packets_out[ptrindex]++;
				priv->wmm.tid_tbl_ptr[ptrindex].ra_list_curr =
					ptr;
			}
			pmadapter->bssprio_tbl[priv->bss_priority].bssprio_cur =
				pmadapter->bssprio_tbl[priv->bss_priority].
				bssprio_cur->pnext;
			pmadapter->callbacks.moal_spin_unlock(pmadapter->
							      pmoal_handle,
							      priv->wmm.
							      ra_list_spinlock);
		}
	} else {
		pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
						      priv->wmm.
						      ra_list_spinlock);
		PRINTM(MINFO, "Nothing to send\n");
	}

	LEAVE();
}

/**
 *  @brief This function checks if this mlan_buffer is already processed.
 *
 *  @param priv     A pointer to mlan_private
 *  @param ptr      A pointer to RA list table
 *
 *  @return         MTRUE or MFALSE
 */
static int INLINE
wlan_is_ptr_processed(mlan_private *priv, raListTbl *ptr)
{
	pmlan_buffer pmbuf;

	pmbuf = (pmlan_buffer)util_peek_list(priv->adapter->pmoal_handle,
					     &ptr->buf_head, MNULL, MNULL);
	if (pmbuf && (pmbuf->flags & MLAN_BUF_FLAG_REQUEUED_PKT))
		return MTRUE;

	return MFALSE;
}

/**
 *  @brief This function sends a single packet that has been processed
 *
 *  @param priv         A pointer to mlan_private
 *  @param ptr          A pointer to RA list table
 *  @param ptrindex     ptr's TID index
 *
 *  @return             N/A
 */
static void INLINE
wlan_send_processed_packet(pmlan_private priv, raListTbl *ptr, int ptrindex)
{
	pmlan_buffer pmbuf_next = MNULL;
	mlan_tx_param tx_param;
	pmlan_buffer pmbuf;
	pmlan_adapter pmadapter = priv->adapter;
	mlan_status ret = MLAN_STATUS_FAILURE;

	pmbuf = (pmlan_buffer)util_dequeue_list(pmadapter->pmoal_handle,
						&ptr->buf_head, MNULL, MNULL);
	if (pmbuf) {
		pmbuf_next =
			(pmlan_buffer)util_peek_list(pmadapter->pmoal_handle,
						     &ptr->buf_head, MNULL,
						     MNULL);
		pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
						      priv->wmm.
						      ra_list_spinlock);
		tx_param.next_pkt_len =
			((pmbuf_next) ? pmbuf_next->data_len +
			 sizeof(TxPD) : 0);
		ret = wlan_sdio_host_to_card(pmadapter, MLAN_TYPE_DATA, pmbuf,
					     &tx_param);
		switch (ret) {
		case MLAN_STATUS_RESOURCE:
			PRINTM(MINFO, "MLAN_STATUS_RESOURCE is returned\n");
			pmadapter->callbacks.moal_spin_lock(pmadapter->
							    pmoal_handle,
							    priv->wmm.
							    ra_list_spinlock);

			if (!wlan_is_ralist_valid(priv, ptr, ptrindex)) {
				pmadapter->callbacks.
					moal_spin_unlock(pmadapter->
							 pmoal_handle,
							 priv->wmm.
							 ra_list_spinlock);
				wlan_write_data_complete(pmadapter, pmbuf,
							 MLAN_STATUS_FAILURE);
				LEAVE();
				return;
			}
			util_enqueue_list_head(pmadapter->pmoal_handle,
					       &ptr->buf_head,
					       (pmlan_linked_list)pmbuf,
					       MNULL, MNULL);

			pmbuf->flags |= MLAN_BUF_FLAG_REQUEUED_PKT;
			pmadapter->callbacks.moal_spin_unlock(pmadapter->
							      pmoal_handle,
							      priv->wmm.
							      ra_list_spinlock);
			break;
		case MLAN_STATUS_FAILURE:
			pmadapter->data_sent = MFALSE;
			PRINTM(MERROR, "Error: Failed to write data\n");
			pmadapter->dbg.num_tx_host_to_card_failure++;
			pmbuf->status_code = MLAN_ERROR_DATA_TX_FAIL;
			wlan_write_data_complete(pmadapter, pmbuf, ret);
			break;
		case MLAN_STATUS_PENDING:
			break;
		case MLAN_STATUS_SUCCESS:
			DBG_HEXDUMP(MDAT_D, "Tx",
				    pmbuf->pbuf + pmbuf->data_offset,
				    MIN(pmbuf->data_len + sizeof(TxPD),
					MAX_DATA_DUMP_LEN));
			wlan_write_data_complete(pmadapter, pmbuf, ret);
			break;
		default:
			break;
		}
		if (ret != MLAN_STATUS_RESOURCE) {
			pmadapter->callbacks.moal_spin_lock(pmadapter->
							    pmoal_handle,
							    priv->wmm.
							    ra_list_spinlock);
			if (wlan_is_ralist_valid(priv, ptr, ptrindex)) {
				priv->wmm.packets_out[ptrindex]++;
				priv->wmm.tid_tbl_ptr[ptrindex].ra_list_curr =
					ptr;
				ptr->total_pkts--;
			}
			pmadapter->bssprio_tbl[priv->bss_priority].bssprio_cur =
				pmadapter->bssprio_tbl[priv->bss_priority].
				bssprio_cur->pnext;
			priv->wmm.pkts_queued[ptrindex]--;
			util_scalar_decrement(pmadapter->pmoal_handle,
					      &priv->wmm.tx_pkts_queued,
					      MNULL, MNULL);
			pmadapter->callbacks.moal_spin_unlock(pmadapter->
							      pmoal_handle,
							      priv->wmm.
							      ra_list_spinlock);
		}
	} else {
		pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
						      priv->wmm.
						      ra_list_spinlock);
	}
}

/**
 *  @brief This function dequeues a packet
 *
 *  @param pmadapter  A pointer to mlan_adapter
 *
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static int
wlan_dequeue_tx_packet(pmlan_adapter pmadapter)
{
	raListTbl *ptr;
	pmlan_private priv = MNULL;
	int ptrindex = 0;
	t_u8 ra[MLAN_MAC_ADDR_LENGTH];
	int tid_del = 0;
	int tid = 0;

	ENTER();

	ptr = wlan_wmm_get_highest_priolist_ptr(pmadapter, &priv, &ptrindex);
	if (!ptr) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/*  Note:- Spinlock is locked in wlan_wmm_get_highest_priolist_ptr
	 *  when it returns a pointer (for the priv it returns),
	 *  and is unlocked in wlan_send_processed_packet,
	 *  wlan_send_single_packet or wlan_11n_aggregate_pkt.
	 *  The spinlock would be required for some parts of both of function.
	 *  But, the the bulk of these function will execute w/o spinlock.
	 *  Unlocking the spinlock inside these function will help us avoid
	 *  taking the spinlock again, check to see if the ptr is still
	 *  valid and then proceed. This is done purely to increase
	 *  execution time. */

	/* Note:- Also, anybody adding code which does not get into
	 * wlan_send_processed_packet, wlan_send_single_packet, or
	 * wlan_11n_aggregate_pkt should make sure ra_list_spinlock
	 * is freed. Otherwise there would be a lock up. */

	tid = wlan_get_tid(priv->adapter, ptr);
	if (tid >= MAX_NUM_TID)
		tid = wlan_wmm_downgrade_tid(priv, tid);

	if (wlan_is_ptr_processed(priv, ptr)) {
		wlan_send_processed_packet(priv, ptr, ptrindex);
		LEAVE();
		return MLAN_STATUS_SUCCESS;
	}

	if (!ptr->is_11n_enabled ||
	    (ptr->ba_status || ptr->del_ba_count >= DEL_BA_THRESHOLD)
#ifdef STA_SUPPORT
	    || priv->wps.session_enable
#endif /* STA_SUPPORT */
		) {
		if (ptr->is_11n_enabled && ptr->ba_status
		    && ptr->amsdu_in_ampdu
		    && wlan_is_amsdu_allowed(priv, ptr, tid)
		    && (wlan_num_pkts_in_txq(priv, ptr, pmadapter->tx_buf_size)
			>= MIN_NUM_AMSDU)) {
			wlan_11n_aggregate_pkt(priv, ptr, priv->intf_hr_len,
					       ptrindex);
		} else
			wlan_send_single_packet(priv, ptr, ptrindex);
	} else {
		if (wlan_is_ampdu_allowed(priv, ptr, tid) &&
		    (ptr->packet_count > ptr->ba_packet_threshold)) {
			if (wlan_is_bastream_avail(priv)) {
				PRINTM(MINFO,
				       "BA setup threshold %d reached. tid=%d\n",
				       ptr->packet_count, tid);
				if (!wlan_11n_get_txbastream_tbl
				    (priv, tid, ptr->ra, MFALSE)) {
					wlan_11n_create_txbastream_tbl(priv,
								       ptr->ra,
								       tid,
								       BA_STREAM_SETUP_INPROGRESS);
					wlan_send_addba(priv, tid, ptr->ra);
				}
			} else if (wlan_find_stream_to_delete(priv, ptr,
							      tid, &tid_del,
							      ra)) {
				PRINTM(MDAT_D, "tid_del=%d tid=%d\n", tid_del,
				       tid);
				if (!wlan_11n_get_txbastream_tbl
				    (priv, tid, ptr->ra, MFALSE)) {
					wlan_11n_create_txbastream_tbl(priv,
								       ptr->ra,
								       tid,
								       BA_STREAM_SETUP_INPROGRESS);
					wlan_send_delba(priv, MNULL, tid_del,
							ra, 1);
				}
			}
		}
		if (wlan_is_amsdu_allowed(priv, ptr, tid) &&
		    (wlan_num_pkts_in_txq(priv, ptr,
					  pmadapter->tx_buf_size) >=
		     MIN_NUM_AMSDU)) {
			wlan_11n_aggregate_pkt(priv, ptr, priv->intf_hr_len,
					       ptrindex);
		} else {
			wlan_send_single_packet(priv, ptr, ptrindex);
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief update tx_pause flag in ra_list
 *
 *  @param priv		  A pointer to mlan_private
 *  @param mac        peer mac address
 *  @param tx_pause   tx_pause flag (0/1)
 *
 *  @return           N/A
 */
t_void
wlan_update_ralist_tx_pause(pmlan_private priv, t_u8 *mac, t_u8 tx_pause)
{
	raListTbl *ra_list;
	int i;
	pmlan_adapter pmadapter = priv->adapter;
	t_u32 pkt_cnt = 0;
	t_u32 tx_pkts_queued = 0;
	ENTER();

	pmadapter->callbacks.moal_spin_lock(pmadapter->pmoal_handle,
					    priv->wmm.ra_list_spinlock);
	for (i = 0; i < MAX_NUM_TID; ++i) {
		ra_list = wlan_wmm_get_ralist_node(priv, i, mac);
		if (ra_list && ra_list->tx_pause != tx_pause) {
			pkt_cnt += ra_list->total_pkts;
			ra_list->tx_pause = tx_pause;
			if (tx_pause)
				priv->wmm.pkts_paused[i] += ra_list->total_pkts;
			else
				priv->wmm.pkts_paused[i] -= ra_list->total_pkts;

		}
	}
	if (pkt_cnt) {
		tx_pkts_queued = util_scalar_read(pmadapter->pmoal_handle,
						  &priv->wmm.tx_pkts_queued,
						  MNULL, MNULL);
		if (tx_pause)
			tx_pkts_queued -= pkt_cnt;
		else
			tx_pkts_queued += pkt_cnt;
		util_scalar_write(priv->adapter->pmoal_handle,
				  &priv->wmm.tx_pkts_queued, tx_pkts_queued,
				  MNULL, MNULL);
		util_scalar_write(priv->adapter->pmoal_handle,
				  &priv->wmm.highest_queued_prio, HIGH_PRIO_TID,
				  MNULL, MNULL);
	}
	pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
					      priv->wmm.ra_list_spinlock);
	LEAVE();
}

#ifdef STA_SUPPORT
/**
 *  @brief update tx_pause flag in none tdls ra_list
 *
 *  @param priv		  A pointer to mlan_private
 *  @param mac        peer mac address
 *  @param tx_pause   tx_pause flag (0/1)
 *
 *  @return           N/A
 */
t_void
wlan_update_non_tdls_ralist(mlan_private *priv, t_u8 *mac, t_u8 tx_pause)
{
	raListTbl *ra_list;
	int i;
	pmlan_adapter pmadapter = priv->adapter;
	t_u32 pkt_cnt = 0;
	t_u32 tx_pkts_queued = 0;
	ENTER();

	pmadapter->callbacks.moal_spin_lock(pmadapter->pmoal_handle,
					    priv->wmm.ra_list_spinlock);
	for (i = 0; i < MAX_NUM_TID; ++i) {
		ra_list =
			(raListTbl *)util_peek_list(priv->adapter->pmoal_handle,
						    &priv->wmm.tid_tbl_ptr[i].
						    ra_list, MNULL, MNULL);
		while (ra_list &&
		       (ra_list !=
			(raListTbl *)&priv->wmm.tid_tbl_ptr[i].ra_list)) {
			if (memcmp
			    (priv->adapter, ra_list->ra, mac,
			     MLAN_MAC_ADDR_LENGTH) &&
			    ra_list->tx_pause != tx_pause) {
				pkt_cnt += ra_list->total_pkts;
				ra_list->tx_pause = tx_pause;
				if (tx_pause)
					priv->wmm.pkts_paused[i] +=
						ra_list->total_pkts;
				else
					priv->wmm.pkts_paused[i] -=
						ra_list->total_pkts;
			}
			ra_list = ra_list->pnext;
		}
	}
	if (pkt_cnt) {
		tx_pkts_queued = util_scalar_read(pmadapter->pmoal_handle,
						  &priv->wmm.tx_pkts_queued,
						  MNULL, MNULL);
		if (tx_pause)
			tx_pkts_queued -= pkt_cnt;
		else
			tx_pkts_queued += pkt_cnt;
		util_scalar_write(priv->adapter->pmoal_handle,
				  &priv->wmm.tx_pkts_queued, tx_pkts_queued,
				  MNULL, MNULL);
		util_scalar_write(priv->adapter->pmoal_handle,
				  &priv->wmm.highest_queued_prio, HIGH_PRIO_TID,
				  MNULL, MNULL);
	}
	pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
					      priv->wmm.ra_list_spinlock);
	LEAVE();
	return;
}

/**
 *  @brief find tdls buffer from ralist
 *
 *  @param priv		  A pointer to mlan_private
 *  @param ralist     A pointer to ralistTbl
 *  @param mac        TDLS peer mac address
 *
 *  @return           pmlan_buffer or MNULL
 */
static pmlan_buffer
wlan_find_tdls_packets(mlan_private *priv, raListTbl *ra_list, t_u8 *mac)
{
	pmlan_buffer pmbuf = MNULL;
	mlan_adapter *pmadapter = priv->adapter;
	t_u8 ra[MLAN_MAC_ADDR_LENGTH];
	ENTER();
	pmbuf = (pmlan_buffer)util_peek_list(priv->adapter->pmoal_handle,
					     &ra_list->buf_head, MNULL, MNULL);
	if (!pmbuf) {
		LEAVE();
		return MNULL;
	}
	while (pmbuf != (pmlan_buffer)&ra_list->buf_head) {
		memcpy(pmadapter, ra, pmbuf->pbuf + pmbuf->data_offset,
		       MLAN_MAC_ADDR_LENGTH);
		if (!memcmp(priv->adapter, ra, mac, MLAN_MAC_ADDR_LENGTH)) {
			LEAVE();
			return pmbuf;
		}
		pmbuf = pmbuf->pnext;
	}
	LEAVE();
	return MNULL;
}

/**
 *  @brief find tdls buffer from tdls pending queue
 *
 *  @param priv		  A pointer to mlan_private
 *  @param mac        TDLS peer mac address
 *
 *  @return           pmlan_buffer or MNULL
 */
static pmlan_buffer
wlan_find_packets_tdls_txq(mlan_private *priv, t_u8 *mac)
{
	pmlan_buffer pmbuf = MNULL;
	mlan_adapter *pmadapter = priv->adapter;
	t_u8 ra[MLAN_MAC_ADDR_LENGTH];
	ENTER();
	pmbuf = (pmlan_buffer)util_peek_list(priv->adapter->pmoal_handle,
					     &priv->tdls_pending_txq,
					     MNULL, MNULL);
	if (!pmbuf) {
		LEAVE();
		return MNULL;
	}
	while (pmbuf != (pmlan_buffer)&priv->tdls_pending_txq) {
		memcpy(pmadapter, ra, pmbuf->pbuf + pmbuf->data_offset,
		       MLAN_MAC_ADDR_LENGTH);
		if (!memcmp(priv->adapter, ra, mac, MLAN_MAC_ADDR_LENGTH)) {
			LEAVE();
			return pmbuf;
		}
		pmbuf = pmbuf->pnext;
	}
	LEAVE();
	return MNULL;
}

/**
 *  @brief Remove TDLS ralist and move packets to AP's ralist
 *
 *  @param priv		  A pointer to mlan_private
 *  @param mac        TDLS peer mac address
 *
 *  @return           N/A
 */
static t_void
wlan_wmm_delete_tdls_ralist(pmlan_private priv, t_u8 *mac)
{
	raListTbl *ra_list;
	raListTbl *ra_list_ap = MNULL;
	int i;
	pmlan_adapter pmadapter = priv->adapter;
	pmlan_buffer pmbuf;
	ENTER();

	for (i = 0; i < MAX_NUM_TID; ++i) {
		ra_list = wlan_wmm_get_ralist_node(priv, i, mac);
		if (ra_list) {
			PRINTM(MDATA, "delete TDLS ralist %p\n", ra_list);
			ra_list_ap =
				(raListTbl *)util_peek_list(pmadapter->
							    pmoal_handle,
							    &priv->wmm.
							    tid_tbl_ptr[i].
							    ra_list, MNULL,
							    MNULL);
			while ((pmbuf =
				(pmlan_buffer)util_peek_list(pmadapter->
							     pmoal_handle,
							     &ra_list->buf_head,
							     MNULL, MNULL))) {
				util_unlink_list(pmadapter->pmoal_handle,
						 &ra_list->buf_head,
						 (pmlan_linked_list)pmbuf,
						 MNULL, MNULL);
				util_enqueue_list_tail(pmadapter->pmoal_handle,
						       &ra_list_ap->buf_head,
						       (pmlan_linked_list)pmbuf,
						       MNULL, MNULL);
				ra_list_ap->total_pkts++;
				ra_list_ap->packet_count++;
			}
			util_free_list_head((t_void *)pmadapter->pmoal_handle,
					    &ra_list->buf_head,
					    pmadapter->callbacks.
					    moal_free_lock);

			util_unlink_list(pmadapter->pmoal_handle,
					 &priv->wmm.tid_tbl_ptr[i].ra_list,
					 (pmlan_linked_list)ra_list, MNULL,
					 MNULL);
			pmadapter->callbacks.moal_mfree(pmadapter->pmoal_handle,
							(t_u8 *)ra_list);
			if (priv->wmm.tid_tbl_ptr[i].ra_list_curr == ra_list)
				priv->wmm.tid_tbl_ptr[i].ra_list_curr =
					ra_list_ap;
		}
	}

	LEAVE();

}
#endif /* STA_SUPPORT */
/********************************************************
			Global Functions
********************************************************/

/**
 *  @brief Get the threshold value for BA setup using system time.
 *
 *  @param pmadapter       Pointer to the mlan_adapter structure
 *
 *  @return         threshold value.
 */
t_u8
wlan_get_random_ba_threshold(pmlan_adapter pmadapter)
{
	t_u32 sec, usec;
	t_u8 ba_threshold = 0;

	ENTER();

	/* setup ba_packet_threshold here random number between
	   [BA_SETUP_PACKET_OFFSET, BA_SETUP_PACKET_OFFSET+BA_SETUP_MAX_PACKET_THRESHOLD-1] */

#define BA_SETUP_MAX_PACKET_THRESHOLD   16
#define BA_SETUP_PACKET_OFFSET          16

	pmadapter->callbacks.moal_get_system_time(pmadapter->pmoal_handle, &sec,
						  &usec);
	sec = (sec & 0xFFFF) + (sec >> 16);
	usec = (usec & 0xFFFF) + (usec >> 16);

	ba_threshold =
		(((sec << 16) + usec) % BA_SETUP_MAX_PACKET_THRESHOLD) +
		BA_SETUP_PACKET_OFFSET;
	PRINTM(MINFO, "setup BA after %d packets\n", ba_threshold);

	LEAVE();
	return ba_threshold;
}

/**
 *  @brief  This function cleans Tx/Rx queues
 *
 *  @param priv		A pointer to mlan_private
 *
 *  @return		N/A
 */
t_void
wlan_clean_txrx(pmlan_private priv)
{
	mlan_adapter *pmadapter = priv->adapter;
	t_u8 i = 0;

	ENTER();

	wlan_cleanup_bypass_txq(priv);

	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_STA) {
		wlan_cleanup_tdls_txq(priv);
	}
	wlan_11n_cleanup_reorder_tbl(priv);
	wlan_11n_deleteall_txbastream_tbl(priv);
	pmadapter->callbacks.moal_spin_lock(pmadapter->pmoal_handle,
					    priv->wmm.ra_list_spinlock);
	wlan_wmm_cleanup_queues(priv);
	wlan_wmm_delete_all_ralist(priv);
	memcpy(pmadapter, tos_to_tid, ac_to_tid, sizeof(tos_to_tid));
	for (i = 0; i < MAX_NUM_TID; i++)
		tos_to_tid_inv[tos_to_tid[i]] = (t_u8)i;
#if defined(UAP_SUPPORT)
	priv->num_drop_pkts = 0;
#endif
#ifdef SDIO_MULTI_PORT_TX_AGGR
	memset(pmadapter, pmadapter->mpa_tx_count, 0,
	       sizeof(pmadapter->mpa_tx_count));
	pmadapter->mpa_sent_no_ports = 0;
	pmadapter->mpa_sent_last_pkt = 0;
#endif
#ifdef SDIO_MULTI_PORT_RX_AGGR
	memset(pmadapter, pmadapter->mpa_rx_count, 0,
	       sizeof(pmadapter->mpa_rx_count));
#endif
	pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
					      priv->wmm.ra_list_spinlock);

	LEAVE();
}

/**
 *  @brief Set the WMM queue priorities to their default values
 *
 *  @param priv     Pointer to the mlan_private driver data struct
 *
 *  @return         N/A
 */
void
wlan_wmm_default_queue_priorities(pmlan_private priv)
{
	ENTER();

	/* Default queue priorities: VO->VI->BE->BK */
	priv->wmm.queue_priority[0] = WMM_AC_VO;
	priv->wmm.queue_priority[1] = WMM_AC_VI;
	priv->wmm.queue_priority[2] = WMM_AC_BE;
	priv->wmm.queue_priority[3] = WMM_AC_BK;

	LEAVE();
}

/**
 *  @brief Initialize WMM priority queues
 *
 *  @param priv     Pointer to the mlan_private driver data struct
 *  @param pwmm_ie  Pointer to the IEEEtypes_WmmParameter_t data struct
 *
 *  @return         N/A
 */
void
wlan_wmm_setup_queue_priorities(pmlan_private priv,
				IEEEtypes_WmmParameter_t *pwmm_ie)
{
	t_u16 cw_min, avg_back_off, tmp[4];
	t_u32 i, j, num_ac;
	t_u8 ac_idx;

	ENTER();

	if (!pwmm_ie || priv->wmm_enabled == MFALSE) {
		/* WMM is not enabled, just set the defaults and return */
		wlan_wmm_default_queue_priorities(priv);
		LEAVE();
		return;
	}
	memset(priv->adapter, tmp, 0, sizeof(tmp));

	HEXDUMP("WMM: setup_queue_priorities: param IE",
		(t_u8 *)pwmm_ie, sizeof(IEEEtypes_WmmParameter_t));

	PRINTM(MINFO, "WMM Parameter IE: version=%d, "
	       "qos_info Parameter Set Count=%d, Reserved=%#x\n",
	       pwmm_ie->vend_hdr.version, pwmm_ie->qos_info.para_set_count,
	       pwmm_ie->reserved);

	for (num_ac = 0; num_ac < NELEMENTS(pwmm_ie->ac_params); num_ac++) {
		cw_min = (1 << pwmm_ie->ac_params[num_ac].ecw.ecw_min) - 1;
		avg_back_off
			=
			(cw_min >> 1) +
			pwmm_ie->ac_params[num_ac].aci_aifsn.aifsn;

		ac_idx = wmm_aci_to_qidx_map[pwmm_ie->ac_params[num_ac].
					     aci_aifsn.aci];
		priv->wmm.queue_priority[ac_idx] = ac_idx;
		tmp[ac_idx] = avg_back_off;

		PRINTM(MCMND, "WMM: CWmax=%d CWmin=%d Avg Back-off=%d\n",
		       (1 << pwmm_ie->ac_params[num_ac].ecw.ecw_max) - 1,
		       cw_min, avg_back_off);
		PRINTM_AC(&pwmm_ie->ac_params[num_ac]);
	}

	HEXDUMP("WMM: avg_back_off", (t_u8 *)tmp, sizeof(tmp));
	HEXDUMP("WMM: queue_priority", priv->wmm.queue_priority,
		sizeof(priv->wmm.queue_priority));

	/* Bubble sort */
	for (i = 0; i < num_ac; i++) {
		for (j = 1; j < num_ac - i; j++) {
			if (tmp[j - 1] > tmp[j]) {
				SWAP_U16(tmp[j - 1], tmp[j]);
				SWAP_U8(priv->wmm.queue_priority[j - 1],
					priv->wmm.queue_priority[j]);
			} else if (tmp[j - 1] == tmp[j]) {
				if (priv->wmm.queue_priority[j - 1]
				    < priv->wmm.queue_priority[j]) {
					SWAP_U8(priv->wmm.queue_priority[j - 1],
						priv->wmm.queue_priority[j]);
				}
			}
		}
	}

	wlan_wmm_queue_priorities_tid(priv, priv->wmm.queue_priority);

	HEXDUMP("WMM: avg_back_off, sort", (t_u8 *)tmp, sizeof(tmp));
	DBG_HEXDUMP(MCMD_D, "WMM: queue_priority, sort",
		    priv->wmm.queue_priority, sizeof(priv->wmm.queue_priority));
	LEAVE();
}

/**
 *  @brief Downgrade WMM priority queue
 *
 *  @param priv     Pointer to the mlan_private driver data struct
 *
 *  @return         N/A
 */
void
wlan_wmm_setup_ac_downgrade(pmlan_private priv)
{
	int ac_val;

	ENTER();

	PRINTM(MINFO, "WMM: AC Priorities: BK(0), BE(1), VI(2), VO(3)\n");

	if (priv->wmm_enabled == MFALSE) {
		/* WMM is not enabled, default priorities */
		for (ac_val = WMM_AC_BK; ac_val <= WMM_AC_VO; ac_val++) {
			priv->wmm.ac_down_graded_vals[ac_val] =
				(mlan_wmm_ac_e)ac_val;
		}
	} else {
		for (ac_val = WMM_AC_BK; ac_val <= WMM_AC_VO; ac_val++) {
			priv->wmm.ac_down_graded_vals[ac_val]
				= wlan_wmm_eval_downgrade_ac(priv,
							     (mlan_wmm_ac_e)
							     ac_val);
			PRINTM(MINFO, "WMM: AC PRIO %d maps to %d\n", ac_val,
			       priv->wmm.ac_down_graded_vals[ac_val]);
		}
	}

	LEAVE();
}

/**
 *  @brief  Allocate and add a RA list for all TIDs with the given RA
 *
 *  @param priv  Pointer to the mlan_private driver data struct
 *  @param ra	 Address of the receiver STA (AP in case of infra)
 *
 *  @return      N/A
 */
void
wlan_ralist_add(mlan_private *priv, t_u8 *ra)
{
	int i;
	raListTbl *ra_list;
	pmlan_adapter pmadapter = priv->adapter;
	tdlsStatus_e status;

	ENTER();

	for (i = 0; i < MAX_NUM_TID; ++i) {
		ra_list = wlan_wmm_allocate_ralist_node(pmadapter, ra);
		PRINTM(MINFO, "Creating RA List %p for tid %d\n", ra_list, i);
		if (!ra_list)
			break;
		ra_list->max_amsdu = 0;
		ra_list->ba_status = BA_STREAM_NOT_SETUP;
		ra_list->amsdu_in_ampdu = MFALSE;
		if (queuing_ra_based(priv)) {
			ra_list->is_11n_enabled = wlan_is_11n_enabled(priv, ra);
			if (ra_list->is_11n_enabled)
				ra_list->max_amsdu =
					get_station_max_amsdu_size(priv, ra);
			ra_list->tx_pause = wlan_is_tx_pause(priv, ra);
		} else {
			ra_list->is_tdls_link = MFALSE;
			ra_list->tx_pause = MFALSE;
			status = wlan_get_tdls_link_status(priv, ra);
			if (MTRUE == wlan_is_tdls_link_setup(status)) {
				ra_list->is_11n_enabled =
					is_station_11n_enabled(priv, ra);
				if (ra_list->is_11n_enabled)
					ra_list->max_amsdu =
						get_station_max_amsdu_size(priv,
									   ra);
				ra_list->is_tdls_link = MTRUE;
			} else {
				ra_list->is_11n_enabled = IS_11N_ENABLED(priv);
				if (ra_list->is_11n_enabled)
					ra_list->max_amsdu = priv->max_amsdu;
			}
		}

		PRINTM_NETINTF(MDATA, priv);
		PRINTM(MDATA, "ralist %p: is_11n_enabled=%d max_amsdu=%d\n",
		       ra_list, ra_list->is_11n_enabled, ra_list->max_amsdu);

		if (ra_list->is_11n_enabled) {
			ra_list->packet_count = 0;
			ra_list->ba_packet_threshold =
				wlan_get_random_ba_threshold(pmadapter);
		}

		util_enqueue_list_tail(pmadapter->pmoal_handle,
				       &priv->wmm.tid_tbl_ptr[i].ra_list,
				       (pmlan_linked_list)ra_list, MNULL,
				       MNULL);

		if (!priv->wmm.tid_tbl_ptr[i].ra_list_curr)
			priv->wmm.tid_tbl_ptr[i].ra_list_curr = ra_list;
	}

	LEAVE();
}

/**
 *  @brief Initialize the WMM parameter.
 *
 *  @param pmadapter  Pointer to the mlan_adapter data structure
 *
 *  @return         N/A
 */
t_void
wlan_init_wmm_param(pmlan_adapter pmadapter)
{
	/* Reuse the same structure of WmmAcParameters_t for configuration purpose here.
	 * the definition of acm bit is changed to ucm (user configuration mode)
	 * FW will take the setting of aifsn,ecw_max,ecw_min, tx_op_limit
	 * only when ucm is set to 1. othewise the default setting/behavoir in
	 * firmware will be used.
	 */
	pmadapter->ac_params[AC_BE].aci_aifsn.acm = 0;
	pmadapter->ac_params[AC_BE].aci_aifsn.aci = AC_BE;
	pmadapter->ac_params[AC_BE].aci_aifsn.aifsn = 3;
	pmadapter->ac_params[AC_BE].ecw.ecw_max = 10;
	pmadapter->ac_params[AC_BE].ecw.ecw_min = 4;
	pmadapter->ac_params[AC_BE].tx_op_limit = 0;

	pmadapter->ac_params[AC_BK].aci_aifsn.acm = 0;
	pmadapter->ac_params[AC_BK].aci_aifsn.aci = AC_BK;
	pmadapter->ac_params[AC_BK].aci_aifsn.aifsn = 7;
	pmadapter->ac_params[AC_BK].ecw.ecw_max = 10;
	pmadapter->ac_params[AC_BK].ecw.ecw_min = 4;
	pmadapter->ac_params[AC_BK].tx_op_limit = 0;

	pmadapter->ac_params[AC_VI].aci_aifsn.acm = 0;
	pmadapter->ac_params[AC_VI].aci_aifsn.aci = AC_VI;
	pmadapter->ac_params[AC_VI].aci_aifsn.aifsn = 2;
	pmadapter->ac_params[AC_VI].ecw.ecw_max = 4;
	pmadapter->ac_params[AC_VI].ecw.ecw_min = 3;
	pmadapter->ac_params[AC_VI].tx_op_limit = 188;

	pmadapter->ac_params[AC_VO].aci_aifsn.acm = 0;
	pmadapter->ac_params[AC_VO].aci_aifsn.aci = AC_VO;
	pmadapter->ac_params[AC_VO].aci_aifsn.aifsn = 2;
	pmadapter->ac_params[AC_VO].ecw.ecw_max = 3;
	pmadapter->ac_params[AC_VO].ecw.ecw_min = 2;
	pmadapter->ac_params[AC_VO].tx_op_limit = 102;

}

/**
 *  @brief Initialize the WMM state information and the WMM data path queues.
 *
 *  @param pmadapter  Pointer to the mlan_adapter data structure
 *
 *  @return         N/A
 */
t_void
wlan_wmm_init(pmlan_adapter pmadapter)
{
	int i, j;
	pmlan_private priv;

	ENTER();

	for (j = 0; j < pmadapter->priv_num; ++j) {
		priv = pmadapter->priv[j];
		if (priv) {
			for (i = 0; i < MAX_NUM_TID; ++i) {
				priv->aggr_prio_tbl[i].amsdu =
					tos_to_tid_inv[i];
				priv->aggr_prio_tbl[i].ampdu_ap =
					priv->aggr_prio_tbl[i].ampdu_user =
					tos_to_tid_inv[i];
				priv->ibss_ampdu[i] =
					priv->aggr_prio_tbl[i].ampdu_user;
				priv->wmm.pkts_queued[i] = 0;
				priv->wmm.pkts_paused[i] = 0;
				priv->wmm.tid_tbl_ptr[i].ra_list_curr = MNULL;
			}
			priv->wmm.drv_pkt_delay_max = WMM_DRV_DELAY_MAX;

			priv->aggr_prio_tbl[6].amsdu = BA_STREAM_NOT_ALLOWED;
			priv->aggr_prio_tbl[7].amsdu = BA_STREAM_NOT_ALLOWED;
			priv->aggr_prio_tbl[6].ampdu_ap
				= priv->aggr_prio_tbl[6].ampdu_user =
				BA_STREAM_NOT_ALLOWED;
			priv->ibss_ampdu[6] = BA_STREAM_NOT_ALLOWED;

			priv->aggr_prio_tbl[7].ampdu_ap
				= priv->aggr_prio_tbl[7].ampdu_user =
				BA_STREAM_NOT_ALLOWED;
			priv->ibss_ampdu[7] = BA_STREAM_NOT_ALLOWED;

			priv->add_ba_param.timeout =
				MLAN_DEFAULT_BLOCK_ACK_TIMEOUT;
#ifdef STA_SUPPORT
			if (priv->bss_type == MLAN_BSS_TYPE_STA) {
				priv->add_ba_param.tx_win_size =
					MLAN_STA_AMPDU_DEF_TXWINSIZE;
				priv->add_ba_param.rx_win_size =
					MLAN_STA_AMPDU_DEF_RXWINSIZE;
			}
#endif
#ifdef WIFI_DIRECT_SUPPORT
			if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT) {
				priv->add_ba_param.tx_win_size =
					MLAN_WFD_AMPDU_DEF_TXRXWINSIZE;
				priv->add_ba_param.rx_win_size =
					MLAN_WFD_AMPDU_DEF_TXRXWINSIZE;
			}
#endif
			if (priv->bss_type == MLAN_BSS_TYPE_NAN) {
				priv->add_ba_param.tx_win_size =
					MLAN_NAN_AMPDU_DEF_TXRXWINSIZE;
				priv->add_ba_param.rx_win_size =
					MLAN_NAN_AMPDU_DEF_TXRXWINSIZE;
			}
#ifdef UAP_SUPPORT
			if (priv->bss_type == MLAN_BSS_TYPE_UAP) {
				priv->add_ba_param.tx_win_size =
					MLAN_UAP_AMPDU_DEF_TXWINSIZE;
				priv->add_ba_param.rx_win_size =
					MLAN_UAP_AMPDU_DEF_RXWINSIZE;
			}
#endif
			priv->user_rxwinsize = priv->add_ba_param.rx_win_size;
			priv->add_ba_param.tx_amsdu = MTRUE;
			priv->add_ba_param.rx_amsdu = MTRUE;
			memset(priv->adapter, priv->rx_seq, 0xff,
			       sizeof(priv->rx_seq));
			wlan_wmm_default_queue_priorities(priv);
		}
	}

	LEAVE();
}

/**
 *  @brief Setup the queue priorities and downgrade any queues as required
 *         by the WMM info.  Setups default values if WMM is not active
 *         for this association.
 *
 *  @param priv     Pointer to the mlan_private driver data struct
 *
 *  @return         N/A
 */
void
wlan_wmm_setup_queues(pmlan_private priv)
{
	ENTER();
	wlan_wmm_setup_queue_priorities(priv, MNULL);
	wlan_wmm_setup_ac_downgrade(priv);
	LEAVE();
}

#ifdef STA_SUPPORT
/**
 *  @brief  Send a command to firmware to retrieve the current WMM status
 *
 *  @param priv     Pointer to the mlan_private driver data struct
 *
 *  @return         MLAN_STATUS_SUCCESS; MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_wmm_status_change(pmlan_private priv)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	ret = wlan_prepare_cmd(priv, HostCmd_CMD_WMM_GET_STATUS, 0, 0, 0,
			       MNULL);
	LEAVE();
	return ret;
}
#endif

/**
 *  @brief Check if wmm TX queue is empty
 *
 *  @param pmadapter  Pointer to the mlan_adapter driver data struct
 *
 *  @return         MFALSE if not empty; MTRUE if empty
 */
int
wlan_wmm_lists_empty(pmlan_adapter pmadapter)
{
	int j;
	pmlan_private priv;

	ENTER();

	for (j = 0; j < pmadapter->priv_num; ++j) {
		priv = pmadapter->priv[j];
		if (priv) {
			if ((priv->port_ctrl_mode == MTRUE) &&
			    (priv->port_open == MFALSE)) {
				PRINTM(MINFO,
				       "wmm_lists_empty: PORT_CLOSED Ignore pkts from BSS%d\n",
				       j);
				continue;
			}
			if (priv->tx_pause)
				continue;

			if (util_scalar_read(pmadapter->pmoal_handle,
					     &priv->wmm.tx_pkts_queued,
					     pmadapter->callbacks.
					     moal_spin_lock,
					     pmadapter->callbacks.
					     moal_spin_unlock)) {
				LEAVE();
				return MFALSE;
			}
		}
	}

	LEAVE();
	return MTRUE;
}

/**
 *   @brief Get ralist node
 *
 *   @param priv     Pointer to the mlan_private driver data struct
 *   @param tid      TID
 *   @param ra_addr  Pointer to the route address
 *
 *   @return         ra_list or MNULL
 */
raListTbl *
wlan_wmm_get_ralist_node(pmlan_private priv, t_u8 tid, t_u8 *ra_addr)
{
	raListTbl *ra_list;
	ENTER();
	ra_list =
		(raListTbl *)util_peek_list(priv->adapter->pmoal_handle,
					    &priv->wmm.tid_tbl_ptr[tid].ra_list,
					    MNULL, MNULL);
	while (ra_list && (ra_list != (raListTbl *)
			   &priv->wmm.tid_tbl_ptr[tid].ra_list)) {
		if (!memcmp
		    (priv->adapter, ra_list->ra, ra_addr,
		     MLAN_MAC_ADDR_LENGTH)) {
			LEAVE();
			return ra_list;
		}
		ra_list = ra_list->pnext;
	}
	LEAVE();
	return MNULL;
}

/**
 *   @brief Check if RA list is valid or not
 *
 *   @param priv     Pointer to the mlan_private driver data struct
 *   @param ra_list  Pointer to raListTbl
 *   @param ptrindex TID pointer index
 *
 *   @return         MTRUE- valid. MFALSE- invalid.
 */
int
wlan_is_ralist_valid(mlan_private *priv, raListTbl *ra_list, int ptrindex)
{
	raListTbl *rlist;

	ENTER();

	rlist = (raListTbl *)util_peek_list(priv->adapter->pmoal_handle,
					    &priv->wmm.tid_tbl_ptr[ptrindex].
					    ra_list, MNULL, MNULL);

	while (rlist && (rlist != (raListTbl *)
			 &priv->wmm.tid_tbl_ptr[ptrindex].ra_list)) {
		if (rlist == ra_list) {
			LEAVE();
			return MTRUE;
		}

		rlist = rlist->pnext;
	}
	LEAVE();
	return MFALSE;
}

/**
 *  @brief  Update an existing raList with a new RA and 11n capability
 *
 *  @param priv     Pointer to the mlan_private driver data struct
 *  @param old_ra   Old receiver address
 *  @param new_ra   New receiver address
 *
 *  @return         integer count of updated nodes
 */
int
wlan_ralist_update(mlan_private *priv, t_u8 *old_ra, t_u8 *new_ra)
{
	t_u8 tid;
	int update_count;
	raListTbl *ra_list;

	ENTER();

	update_count = 0;

	for (tid = 0; tid < MAX_NUM_TID; ++tid) {

		ra_list = wlan_wmm_get_ralist_node(priv, tid, old_ra);

		if (ra_list) {
			update_count++;

			if (queuing_ra_based(priv))
				ra_list->is_11n_enabled =
					wlan_is_11n_enabled(priv, new_ra);
			else
				ra_list->is_11n_enabled = IS_11N_ENABLED(priv);
			ra_list->packet_count = 0;
			ra_list->ba_packet_threshold =
				wlan_get_random_ba_threshold(priv->adapter);
			ra_list->amsdu_in_ampdu = MFALSE;
			ra_list->ba_status = BA_STREAM_NOT_SETUP;
			PRINTM(MINFO,
			       "ralist_update: %p, %d, " MACSTR "-->" MACSTR
			       "\n", ra_list, ra_list->is_11n_enabled,
			       MAC2STR(ra_list->ra), MAC2STR(new_ra));

			memcpy(priv->adapter, ra_list->ra, new_ra,
			       MLAN_MAC_ADDR_LENGTH);
		}
	}

	LEAVE();
	return update_count;
}

/**
 *  @brief Add packet to WMM queue
 *
 *  @param pmadapter  Pointer to the mlan_adapter driver data struct
 *  @param pmbuf      Pointer to the mlan_buffer data struct
 *
 *  @return         N/A
 */
t_void
wlan_wmm_add_buf_txqueue(pmlan_adapter pmadapter, pmlan_buffer pmbuf)
{
	pmlan_private priv = pmadapter->priv[pmbuf->bss_index];
	t_u32 tid;
	raListTbl *ra_list;
	t_u8 ra[MLAN_MAC_ADDR_LENGTH], tid_down;
	tdlsStatus_e status;
#if defined(UAP_SUPPORT)
	sta_node *sta_ptr = MNULL;
#endif

	ENTER();

	pmbuf->buf_type = MLAN_BUF_TYPE_DATA;
	if (!priv->media_connected) {
		PRINTM_NETINTF(MWARN, priv);
		PRINTM(MWARN, "Drop packet %p in disconnect state\n", pmbuf);
		wlan_write_data_complete(pmadapter, pmbuf, MLAN_STATUS_FAILURE);
		LEAVE();
		return;
	}
	tid = pmbuf->priority;
	pmadapter->callbacks.moal_spin_lock(pmadapter->pmoal_handle,
					    priv->wmm.ra_list_spinlock);
	tid_down = wlan_wmm_downgrade_tid(priv, tid);

	/* In case of infra as we have already created the list during association
	   we just don't have to call get_queue_raptr, we will have only 1 raptr
	   for a tid in case of infra */
	if (!queuing_ra_based(priv)) {
		memcpy(pmadapter, ra, pmbuf->pbuf + pmbuf->data_offset,
		       MLAN_MAC_ADDR_LENGTH);
		status = wlan_get_tdls_link_status(priv, ra);
		if (MTRUE == wlan_is_tdls_link_setup(status)) {
			ra_list = wlan_wmm_get_queue_raptr(priv, tid_down, ra);
			pmbuf->flags |= MLAN_BUF_FLAG_TDLS;
		} else if (status == TDLS_SETUP_INPROGRESS) {
			wlan_add_buf_tdls_txqueue(priv, pmbuf);
			pmadapter->callbacks.moal_spin_unlock(pmadapter->
							      pmoal_handle,
							      priv->wmm.
							      ra_list_spinlock);
			LEAVE();
			return;
		} else
			ra_list =
				(raListTbl *)util_peek_list(pmadapter->
							    pmoal_handle,
							    &priv->wmm.
							    tid_tbl_ptr
							    [tid_down].ra_list,
							    MNULL, MNULL);
	} else {
		memcpy(pmadapter, ra, pmbuf->pbuf + pmbuf->data_offset,
		       MLAN_MAC_ADDR_LENGTH);
	/** put multicast/broadcast packet in the same ralist */
		if (ra[0] & 0x01)
			memset(pmadapter, ra, 0xff, sizeof(ra));
#if defined(UAP_SUPPORT)
		else if (priv->bss_type == MLAN_BSS_TYPE_UAP) {
			sta_ptr = wlan_get_station_entry(priv, ra);
			if (sta_ptr) {
				if (!sta_ptr->is_wmm_enabled) {
					tid_down =
						wlan_wmm_downgrade_tid(priv,
								       0xff);
				}
			}
		}
#endif
		ra_list = wlan_wmm_get_queue_raptr(priv, tid_down, ra);
	}

	if (!ra_list) {
		PRINTM_NETINTF(MWARN, priv);
		PRINTM(MWARN,
		       "Drop packet %p, ra_list=%p, media_connected=%d\n",
		       pmbuf, ra_list, priv->media_connected);
		pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
						      priv->wmm.
						      ra_list_spinlock);
		wlan_write_data_complete(pmadapter, pmbuf, MLAN_STATUS_FAILURE);
		LEAVE();
		return;
	}

	PRINTM_NETINTF(MDATA, priv);
	PRINTM(MDATA,
	       "Adding pkt %p (priority=%d, tid_down=%d) to ra_list %p\n",
	       pmbuf, pmbuf->priority, tid_down, ra_list);
	util_enqueue_list_tail(pmadapter->pmoal_handle, &ra_list->buf_head,
			       (pmlan_linked_list)pmbuf, MNULL, MNULL);

	ra_list->total_pkts++;
	ra_list->packet_count++;

	priv->wmm.pkts_queued[tid_down]++;
	if (ra_list->tx_pause) {
		priv->wmm.pkts_paused[tid_down]++;
	} else {
		util_scalar_increment(pmadapter->pmoal_handle,
				      &priv->wmm.tx_pkts_queued, MNULL, MNULL);
		/* if highest_queued_prio < prio(tid_down), set it to prio(tid_down) */
		util_scalar_conditional_write(pmadapter->pmoal_handle,
					      &priv->wmm.highest_queued_prio,
					      MLAN_SCALAR_COND_LESS_THAN,
					      tos_to_tid_inv[tid_down],
					      tos_to_tid_inv[tid_down],
					      MNULL, MNULL);
	}
	/* Record the current time the packet was queued; used to determine
	 *   the amount of time the packet was queued in the driver before it
	 *   was sent to the firmware.  The delay is then sent along with the
	 *   packet to the firmware for aggregate delay calculation for stats
	 *   and MSDU lifetime expiry.
	 */
	pmadapter->callbacks.moal_get_system_time(pmadapter->pmoal_handle,
						  &pmbuf->in_ts_sec,
						  &pmbuf->in_ts_usec);
	pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
					      priv->wmm.ra_list_spinlock);

	LEAVE();
}

#ifdef STA_SUPPORT
/**
 *  @brief Process the GET_WMM_STATUS command response from firmware
 *
 *  The GET_WMM_STATUS response may contain multiple TLVs for:
 *      - AC Queue status TLVs
 *      - Current WMM Parameter IE TLV
 *      - Admission Control action frame TLVs
 *
 *  This function parses the TLVs and then calls further functions
 *   to process any changes in the queue prioritize or state.
 *
 *  @param priv      Pointer to the mlan_private driver data struct
 *  @param ptlv      Pointer to the tlv block returned in the response.
 *  @param resp_len  Length of TLV block
 *
 *  @return MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_wmm_get_status(pmlan_private priv, t_u8 *ptlv, int resp_len)
{
	t_u8 *pcurrent = ptlv;
	t_u32 tlv_len;
	t_u8 send_wmm_event;
	MrvlIEtypes_Data_t *ptlv_hdr;
	MrvlIEtypes_WmmQueueStatus_t *ptlv_wmm_q_status;
	IEEEtypes_WmmParameter_t *pwmm_param_ie = MNULL;
	WmmAcStatus_t *pac_status;

	MrvlIETypes_ActionFrame_t *ptlv_action;
	IEEEtypes_Action_WMM_AddTsRsp_t *padd_ts_rsp;
	IEEEtypes_Action_WMM_DelTs_t *pdel_ts;

	ENTER();

	send_wmm_event = MFALSE;

	PRINTM(MINFO, "WMM: WMM_GET_STATUS cmdresp received: %d\n", resp_len);
	HEXDUMP("CMD_RESP: WMM_GET_STATUS", pcurrent, resp_len);

	while (resp_len >= sizeof(ptlv_hdr->header)) {
		ptlv_hdr = (MrvlIEtypes_Data_t *)pcurrent;
		tlv_len = wlan_le16_to_cpu(ptlv_hdr->header.len);
		if ((tlv_len + sizeof(ptlv_hdr->header)) > resp_len) {
			PRINTM(MERROR,
			       "WMM get status: Error in processing  TLV buffer\n");
			resp_len = 0;
			continue;
		}

		switch (wlan_le16_to_cpu(ptlv_hdr->header.type)) {
		case TLV_TYPE_WMMQSTATUS:
			ptlv_wmm_q_status =
				(MrvlIEtypes_WmmQueueStatus_t *)ptlv_hdr;
			PRINTM(MEVENT, "WMM_STATUS: QSTATUS TLV: %d\n",
			       ptlv_wmm_q_status->queue_index);

			PRINTM(MINFO,
			       "CMD_RESP: WMM_GET_STATUS: QSTATUS TLV: %d, %d, %d\n",
			       ptlv_wmm_q_status->queue_index,
			       ptlv_wmm_q_status->flow_required,
			       ptlv_wmm_q_status->disabled);

			pac_status =
				&priv->wmm.ac_status[ptlv_wmm_q_status->
						     queue_index];
			pac_status->disabled = ptlv_wmm_q_status->disabled;
			pac_status->flow_required =
				ptlv_wmm_q_status->flow_required;
			pac_status->flow_created =
				ptlv_wmm_q_status->flow_created;
			break;

		case TLV_TYPE_VENDOR_SPECIFIC_IE:	/* WMM_IE */
			/*
			 * Point the regular IEEE IE 2 bytes into the Marvell IE
			 *   and setup the IEEE IE type and length byte fields
			 */

			PRINTM(MEVENT, "WMM STATUS: WMM IE\n");

			HEXDUMP("WMM: WMM TLV:", (t_u8 *)ptlv_hdr, tlv_len + 4);

			pwmm_param_ie =
				(IEEEtypes_WmmParameter_t *)(pcurrent + 2);
			pwmm_param_ie->vend_hdr.len = (t_u8)tlv_len;
			pwmm_param_ie->vend_hdr.element_id = WMM_IE;

			PRINTM(MINFO,
			       "CMD_RESP: WMM_GET_STATUS: WMM Parameter Set: %d\n",
			       pwmm_param_ie->qos_info.para_set_count);

			memcpy(priv->adapter,
			       (t_u8 *)&priv->curr_bss_params.bss_descriptor.
			       wmm_ie, pwmm_param_ie,
			       MIN(sizeof(IEEEtypes_WmmParameter_t),
				   (pwmm_param_ie->vend_hdr.len + 2)));
			send_wmm_event = MTRUE;
			break;

		case TLV_TYPE_IEEE_ACTION_FRAME:
			PRINTM(MEVENT, "WMM_STATUS: IEEE Action Frame\n");
			ptlv_action = (MrvlIETypes_ActionFrame_t *)pcurrent;

			ptlv_action->actionFrame.wmmAc.tspecAct.category =
				wlan_le32_to_cpu(ptlv_action->actionFrame.wmmAc.
						 tspecAct.category);
			if (ptlv_action->actionFrame.wmmAc.tspecAct.category ==
			    IEEE_MGMT_ACTION_CATEGORY_WMM_TSPEC) {

				ptlv_action->actionFrame.wmmAc.tspecAct.action =
					wlan_le32_to_cpu(ptlv_action->
							 actionFrame.wmmAc.
							 tspecAct.action);
				switch (ptlv_action->actionFrame.wmmAc.tspecAct.
					action) {
				case TSPEC_ACTION_CODE_ADDTS_RSP:
					padd_ts_rsp =
						&ptlv_action->actionFrame.wmmAc.
						addTsRsp;
					wlan_send_wmmac_host_event(priv,
								   "ADDTS_RSP",
								   ptlv_action->
								   srcAddr,
								   padd_ts_rsp->
								   tspecIE.
								   TspecBody.
								   TSInfo.TID,
								   padd_ts_rsp->
								   tspecIE.
								   TspecBody.
								   TSInfo.
								   UserPri,
								   padd_ts_rsp->
								   statusCode);
					break;

				case TSPEC_ACTION_CODE_DELTS:
					pdel_ts =
						&ptlv_action->actionFrame.wmmAc.
						delTs;
					wlan_send_wmmac_host_event(priv,
								   "DELTS_RX",
								   ptlv_action->
								   srcAddr,
								   pdel_ts->
								   tspecIE.
								   TspecBody.
								   TSInfo.TID,
								   pdel_ts->
								   tspecIE.
								   TspecBody.
								   TSInfo.
								   UserPri,
								   pdel_ts->
								   reasonCode);
					break;

				case TSPEC_ACTION_CODE_ADDTS_REQ:
				default:
					break;
				}
			}
			break;

		default:
			break;
		}

		pcurrent += (tlv_len + sizeof(ptlv_hdr->header));
		resp_len -= (tlv_len + sizeof(ptlv_hdr->header));
	}

	wlan_wmm_setup_queue_priorities(priv, pwmm_param_ie);
	wlan_wmm_setup_ac_downgrade(priv);

	if (send_wmm_event) {
		wlan_recv_event(priv, MLAN_EVENT_ID_FW_WMM_CONFIG_CHANGE,
				MNULL);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Call back from the command module to allow insertion of a WMM TLV
 *
 *  If the BSS we are associating to supports WMM, add the required WMM
 *    Information IE to the association request command buffer in the form
 *    of a Marvell extended IEEE IE.
 *
 *  @param priv         Pointer to the mlan_private driver data struct
 *  @param ppassoc_buf  Output parameter: Pointer to the TLV output buffer,
 *                      modified on return to point after the appended WMM TLV
 *  @param pwmm_ie      Pointer to the WMM IE for the BSS we are joining
 *  @param pht_cap      Pointer to the HT IE for the BSS we are joining
 *
 *  @return Length of data appended to the association tlv buffer
 */
t_u32
wlan_wmm_process_association_req(pmlan_private priv,
				 t_u8 **ppassoc_buf,
				 IEEEtypes_WmmParameter_t *pwmm_ie,
				 IEEEtypes_HTCap_t *pht_cap)
{
	MrvlIEtypes_WmmParamSet_t *pwmm_tlv;
	t_u32 ret_len = 0;

	ENTER();

	/* Null checks */
	if (!ppassoc_buf) {
		LEAVE();
		return 0;
	}
	if (!(*ppassoc_buf)) {
		LEAVE();
		return 0;
	}

	if (!pwmm_ie) {
		LEAVE();
		return 0;
	}

	PRINTM(MINFO, "WMM: process assoc req: bss->wmmIe=0x%x\n",
	       pwmm_ie->vend_hdr.element_id);

	if ((priv->wmm_required
	     || (pht_cap && (pht_cap->ieee_hdr.element_id == HT_CAPABILITY)
		 && (priv->config_bands & BAND_GN
		     || priv->config_bands & BAND_AN))
	    )
	    && pwmm_ie->vend_hdr.element_id == WMM_IE) {
		pwmm_tlv = (MrvlIEtypes_WmmParamSet_t *)*ppassoc_buf;
		pwmm_tlv->header.type = (t_u16)wmm_info_ie[0];
		pwmm_tlv->header.type = wlan_cpu_to_le16(pwmm_tlv->header.type);
		pwmm_tlv->header.len = (t_u16)wmm_info_ie[1];
		memcpy(priv->adapter, pwmm_tlv->wmm_ie, &wmm_info_ie[2],
		       pwmm_tlv->header.len);
		if (pwmm_ie->qos_info.qos_uapsd)
			memcpy(priv->adapter,
			       (t_u8 *)(pwmm_tlv->wmm_ie +
					pwmm_tlv->header.len -
					sizeof(priv->wmm_qosinfo)),
			       &priv->wmm_qosinfo, sizeof(priv->wmm_qosinfo));

		ret_len = sizeof(pwmm_tlv->header) + pwmm_tlv->header.len;
		pwmm_tlv->header.len = wlan_cpu_to_le16(pwmm_tlv->header.len);

		HEXDUMP("ASSOC_CMD: WMM IE", (t_u8 *)pwmm_tlv, ret_len);
		*ppassoc_buf += ret_len;
	}

	LEAVE();
	return ret_len;
}
#endif /* STA_SUPPORT */

/**
 *   @brief Compute the time delay in the driver queues for a given packet.
 *
 *   When the packet is received at the OS/Driver interface, the current
 *     time is set in the packet structure.  The difference between the present
 *     time and that received time is computed in this function and limited
 *     based on pre-compiled limits in the driver.
 *
 *   @param priv   Ptr to the mlan_private driver data struct
 *   @param pmbuf  Ptr to the mlan_buffer which has been previously timestamped
 *
 *   @return  Time delay of the packet in 2ms units after having limit applied
 */
t_u8
wlan_wmm_compute_driver_packet_delay(pmlan_private priv,
				     const pmlan_buffer pmbuf)
{
	t_u8 ret_val = 0;
	t_u32 out_ts_sec, out_ts_usec;
	t_s32 queue_delay;

	ENTER();

	priv->adapter->callbacks.moal_get_system_time(priv->adapter->
						      pmoal_handle, &out_ts_sec,
						      &out_ts_usec);

	queue_delay = (t_s32)(out_ts_sec - pmbuf->in_ts_sec) * 1000;
	queue_delay += (t_s32)(out_ts_usec - pmbuf->in_ts_usec) / 1000;

	/*
	 * Queue delay is passed as a uint8 in units of 2ms (ms shifted
	 *  by 1). Min value (other than 0) is therefore 2ms, max is 510ms.
	 *
	 * Pass max value if queue_delay is beyond the uint8 range
	 */
	ret_val = (t_u8)(MIN(queue_delay, priv->wmm.drv_pkt_delay_max) >> 1);

	PRINTM(MINFO, "WMM: Pkt Delay: %d ms, %d ms sent to FW\n",
	       queue_delay, ret_val);

	LEAVE();
	return ret_val;
}

/**
 *  @brief Transmit the highest priority packet awaiting in the WMM Queues
 *
 *  @param pmadapter Pointer to the mlan_adapter driver data struct
 *
 *  @return        N/A
 */
void
wlan_wmm_process_tx(pmlan_adapter pmadapter)
{
	ENTER();

	do {
		if (wlan_dequeue_tx_packet(pmadapter))
			break;
		if (pmadapter->sdio_ireg & UP_LD_CMD_PORT_HOST_INT_STATUS) {
#ifdef SDIO_MULTI_PORT_TX_AGGR
			wlan_send_mp_aggr_buf(pmadapter);
#endif
			break;
		}

		/* Check if busy */
	} while (!pmadapter->data_sent && !pmadapter->tx_lock_flag
		 && !wlan_wmm_lists_empty(pmadapter));

	LEAVE();
	return;
}

/**
 *  @brief select wmm queue
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param tid          TID 0-7
 *
 *  @return             wmm_queue priority (0-3)
 */
t_u8
wlan_wmm_select_queue(mlan_private *pmpriv, t_u8 tid)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	t_u8 i;
	mlan_wmm_ac_e ac_down =
		pmpriv->wmm.
		ac_down_graded_vals[wlan_wmm_convert_tos_to_ac(pmadapter, tid)];

	ENTER();

	for (i = 0; i < 4; i++) {
		if (pmpriv->wmm.queue_priority[i] == ac_down) {
			LEAVE();
			return i;
		}
	}
	LEAVE();
	return 0;
}

/**
 *  @brief Delete tx packets in RA list
 *
 *  @param priv			Pointer to the mlan_private driver data struct
 *  @param ra_list_head	ra list header
 *  @param tid          tid
 *
 *  @return		N/A
 */
static INLINE t_u8
wlan_del_tx_pkts_in_ralist(pmlan_private priv,
			   mlan_list_head *ra_list_head, int tid)
{
	raListTbl *ra_list = MNULL;
	pmlan_adapter pmadapter = priv->adapter;
	pmlan_buffer pmbuf = MNULL;
	t_u8 ret = MFALSE;
	ENTER();
	ra_list =
		(raListTbl *)util_peek_list(priv->adapter->pmoal_handle,
					    ra_list_head, MNULL, MNULL);
	while (ra_list && ra_list != (raListTbl *)ra_list_head) {
		if (ra_list->total_pkts && (ra_list->tx_pause ||
					    (ra_list->total_pkts >
					     RX_LOW_THRESHOLD))) {
			pmbuf = (pmlan_buffer)util_dequeue_list(pmadapter->
								pmoal_handle,
								&ra_list->
								buf_head, MNULL,
								MNULL);
			if (pmbuf) {
				PRINTM(MDATA,
				       "Drop pkts: tid=%d tx_pause=%d pkts=%d "
				       MACSTR "\n", tid, ra_list->tx_pause,
				       ra_list->total_pkts,
				       MAC2STR(ra_list->ra));
				wlan_write_data_complete(pmadapter, pmbuf,
							 MLAN_STATUS_FAILURE);
				priv->wmm.pkts_queued[tid]--;
				priv->num_drop_pkts++;
				ra_list->total_pkts--;
				if (ra_list->tx_pause)
					priv->wmm.pkts_paused[tid]--;
				else
					util_scalar_decrement(pmadapter->
							      pmoal_handle,
							      &priv->wmm.
							      tx_pkts_queued,
							      MNULL, MNULL);
				ret = MTRUE;
				break;
			}
		}
		ra_list = ra_list->pnext;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Drop tx pkts
 *
 *  @param priv			Pointer to the mlan_private driver data struct
 *
 *  @return		N/A
 */
t_void
wlan_drop_tx_pkts(pmlan_private priv)
{
	int j;
	static int i;
	pmlan_adapter pmadapter = priv->adapter;
	pmadapter->callbacks.moal_spin_lock(pmadapter->pmoal_handle,
					    priv->wmm.ra_list_spinlock);
	for (j = 0; j < MAX_NUM_TID; j++, i++) {
		if (i == MAX_NUM_TID)
			i = 0;
		if (wlan_del_tx_pkts_in_ralist
		    (priv, &priv->wmm.tid_tbl_ptr[i].ra_list, i)) {
			i++;
			break;
		}
	}
	pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
					      priv->wmm.ra_list_spinlock);
	return;
}

/**
 *  @brief Remove peer ralist
 *
 *  @param priv		  A pointer to mlan_private
 *  @param mac        peer mac address
 *
 *  @return           N/A
 */
t_void
wlan_wmm_delete_peer_ralist(pmlan_private priv, t_u8 *mac)
{
	raListTbl *ra_list;
	int i;
	pmlan_adapter pmadapter = priv->adapter;
	t_u32 pkt_cnt = 0;
	t_u32 tx_pkts_queued = 0;

	ENTER();
	pmadapter->callbacks.moal_spin_lock(pmadapter->pmoal_handle,
					    priv->wmm.ra_list_spinlock);
	for (i = 0; i < MAX_NUM_TID; ++i) {
		ra_list = wlan_wmm_get_ralist_node(priv, i, mac);
		if (ra_list) {
			PRINTM(MINFO, "delete sta ralist %p\n", ra_list);
			priv->wmm.pkts_queued[i] -= ra_list->total_pkts;
			if (ra_list->tx_pause)
				priv->wmm.pkts_paused[i] -= ra_list->total_pkts;
			else
				pkt_cnt += ra_list->total_pkts;
			wlan_wmm_del_pkts_in_ralist_node(priv, ra_list);

			util_unlink_list(pmadapter->pmoal_handle,
					 &priv->wmm.tid_tbl_ptr[i].ra_list,
					 (pmlan_linked_list)ra_list, MNULL,
					 MNULL);
			pmadapter->callbacks.moal_mfree(pmadapter->pmoal_handle,
							(t_u8 *)ra_list);
			if (priv->wmm.tid_tbl_ptr[i].ra_list_curr == ra_list)
				priv->wmm.tid_tbl_ptr[i].ra_list_curr =
					(raListTbl *)&priv->wmm.tid_tbl_ptr[i].
					ra_list;
		}
	}
	if (pkt_cnt) {
		tx_pkts_queued = util_scalar_read(pmadapter->pmoal_handle,
						  &priv->wmm.tx_pkts_queued,
						  MNULL, MNULL);
		tx_pkts_queued -= pkt_cnt;
		util_scalar_write(priv->adapter->pmoal_handle,
				  &priv->wmm.tx_pkts_queued, tx_pkts_queued,
				  MNULL, MNULL);
		util_scalar_write(priv->adapter->pmoal_handle,
				  &priv->wmm.highest_queued_prio, HIGH_PRIO_TID,
				  MNULL, MNULL);
	}
	pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
					      priv->wmm.ra_list_spinlock);
	LEAVE();
}

#ifdef STA_SUPPORT
/**
 *  @brief Hold TDLS packets to tdls pending queue
 *
 *  @param priv		A pointer to mlan_private
 *  @param mac      station mac address
 *
 *  @return      N/A
 */
t_void
wlan_hold_tdls_packets(pmlan_private priv, t_u8 *mac)
{
	pmlan_buffer pmbuf;
	mlan_adapter *pmadapter = priv->adapter;
	raListTbl *ra_list = MNULL;
	t_u8 i;

	ENTER();
	pmadapter->callbacks.moal_spin_lock(pmadapter->pmoal_handle,
					    priv->wmm.ra_list_spinlock);
	PRINTM(MDATA, "wlan_hold_tdls_packets: " MACSTR "\n", MAC2STR(mac));
	for (i = 0; i < MAX_NUM_TID; ++i) {
		ra_list = (raListTbl *)util_peek_list(pmadapter->pmoal_handle,
						      &priv->wmm.tid_tbl_ptr[i].
						      ra_list, MNULL, MNULL);
		if (ra_list) {
			while ((pmbuf =
				wlan_find_tdls_packets(priv, ra_list, mac))) {
				util_unlink_list(pmadapter->pmoal_handle,
						 &ra_list->buf_head,
						 (pmlan_linked_list)pmbuf,
						 MNULL, MNULL);
				ra_list->total_pkts--;
				priv->wmm.pkts_queued[i]--;
				util_scalar_decrement(pmadapter->pmoal_handle,
						      &priv->wmm.tx_pkts_queued,
						      MNULL, MNULL);
				ra_list->packet_count--;
				wlan_add_buf_tdls_txqueue(priv, pmbuf);
				PRINTM(MDATA, "hold tdls packet=%p\n", pmbuf);
			}
		}
	}
	pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
					      priv->wmm.ra_list_spinlock);
	LEAVE();
}

/**
 *  @brief move TDLS packets back to ralist
 *
 *  @param priv		  A pointer to mlan_private
 *  @param mac        TDLS peer mac address
 *  @param status     tdlsStatus
 *
 *  @return           pmlan_buffer or MNULL
 */
t_void
wlan_restore_tdls_packets(pmlan_private priv, t_u8 *mac, tdlsStatus_e status)
{
	pmlan_buffer pmbuf;
	mlan_adapter *pmadapter = priv->adapter;
	raListTbl *ra_list = MNULL;
	t_u32 tid;
	t_u32 tid_down;

	ENTER();
	PRINTM(MDATA, "wlan_restore_tdls_packets: " MACSTR " status=%d\n",
	       MAC2STR(mac), status);

	pmadapter->callbacks.moal_spin_lock(pmadapter->pmoal_handle,
					    priv->wmm.ra_list_spinlock);

	while ((pmbuf = wlan_find_packets_tdls_txq(priv, mac))) {
		util_unlink_list(pmadapter->pmoal_handle,
				 &priv->tdls_pending_txq,
				 (pmlan_linked_list)pmbuf, MNULL, MNULL);
		tid = pmbuf->priority;
		tid_down = wlan_wmm_downgrade_tid(priv, tid);
		if (status == TDLS_SETUP_COMPLETE) {
			ra_list = wlan_wmm_get_queue_raptr(priv, tid_down, mac);
			pmbuf->flags |= MLAN_BUF_FLAG_TDLS;
		} else {
			ra_list =
				(raListTbl *)util_peek_list(pmadapter->
							    pmoal_handle,
							    &priv->wmm.
							    tid_tbl_ptr
							    [tid_down].ra_list,
							    MNULL, MNULL);
			pmbuf->flags &= ~MLAN_BUF_FLAG_TDLS;
		}
		if (!ra_list) {
			PRINTM_NETINTF(MWARN, priv);
			PRINTM(MWARN,
			       "Drop packet %p, ra_list=%p media_connected=%d\n",
			       pmbuf, ra_list, priv->media_connected);
			wlan_write_data_complete(pmadapter, pmbuf,
						 MLAN_STATUS_FAILURE);
			continue;
		}
		PRINTM_NETINTF(MDATA, priv);
		PRINTM(MDATA,
		       "ADD TDLS pkt %p (priority=%d) back to ra_list %p\n",
		       pmbuf, pmbuf->priority, ra_list);
		util_enqueue_list_tail(pmadapter->pmoal_handle,
				       &ra_list->buf_head,
				       (pmlan_linked_list)pmbuf, MNULL, MNULL);
		ra_list->total_pkts++;
		ra_list->packet_count++;
		priv->wmm.pkts_queued[tid_down]++;
		util_scalar_increment(pmadapter->pmoal_handle,
				      &priv->wmm.tx_pkts_queued, MNULL, MNULL);
		util_scalar_conditional_write(pmadapter->pmoal_handle,
					      &priv->wmm.highest_queued_prio,
					      MLAN_SCALAR_COND_LESS_THAN,
					      tos_to_tid_inv[tid_down],
					      tos_to_tid_inv[tid_down], MNULL,
					      MNULL);
	}
	if (status != TDLS_SETUP_COMPLETE)
		wlan_wmm_delete_tdls_ralist(priv, mac);
	pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
					      priv->wmm.ra_list_spinlock);
	LEAVE();
}

/**
 *  @brief This function prepares the command of ADDTS
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_wmm_addts_req(IN pmlan_private pmpriv,
		       OUT HostCmd_DS_COMMAND *cmd, IN t_void *pdata_buf)
{
	mlan_ds_wmm_addts *paddts = (mlan_ds_wmm_addts *)pdata_buf;
	HostCmd_DS_WMM_ADDTS_REQ *pcmd_addts = &cmd->params.add_ts;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_WMM_ADDTS_REQ);
	cmd->size = wlan_cpu_to_le16(sizeof(pcmd_addts->dialog_token)
				     + sizeof(pcmd_addts->timeout_ms)
				     + sizeof(pcmd_addts->command_result)
				     + sizeof(pcmd_addts->ieee_status_code)
				     + paddts->ie_data_len + S_DS_GEN);
	cmd->result = 0;

	pcmd_addts->timeout_ms = wlan_cpu_to_le32(paddts->timeout);
	pcmd_addts->dialog_token = paddts->dialog_tok;
	memcpy(pmpriv->adapter,
	       pcmd_addts->tspec_data,
	       paddts->ie_data, MIN(WMM_TSPEC_SIZE, paddts->ie_data_len));

	LEAVE();

	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of ADDTS
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_wmm_addts_req(IN pmlan_private pmpriv,
		       const IN HostCmd_DS_COMMAND *resp,
		       OUT mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_wmm_cfg *pwmm = MNULL;
	mlan_ds_wmm_addts *paddts = MNULL;
	const HostCmd_DS_WMM_ADDTS_REQ *presp_addts = &resp->params.add_ts;

	ENTER();

	if (pioctl_buf) {
		pwmm = (mlan_ds_wmm_cfg *)pioctl_buf->pbuf;
		paddts = (mlan_ds_wmm_addts *)&pwmm->param.addts;
		paddts->result = wlan_le32_to_cpu(presp_addts->command_result);
		paddts->dialog_tok = presp_addts->dialog_token;
		paddts->status_code = (t_u32)presp_addts->ieee_status_code;

		if (paddts->result == MLAN_CMD_RESULT_SUCCESS) {
			/* The tspecData field is potentially variable in size due to
			 * extra IEs that may have been in the ADDTS response action
			 * frame. Calculate the data length from the firmware command
			 * response.
			 */
			paddts->ie_data_len
				= (t_u8)(resp->size
					 - sizeof(presp_addts->command_result)
					 - sizeof(presp_addts->timeout_ms)
					 - sizeof(presp_addts->dialog_token)
					 - sizeof(presp_addts->ieee_status_code)
					 - S_DS_GEN);

			/* Copy the TSPEC data include any extra IEs after the TSPEC */
			memcpy(pmpriv->adapter,
			       paddts->ie_data,
			       presp_addts->tspec_data, paddts->ie_data_len);
		} else {
			paddts->ie_data_len = 0;
		}
		PRINTM(MINFO, "TSPEC: ADDTS ret = %d,%d sz=%d\n",
		       paddts->result, paddts->status_code,
		       paddts->ie_data_len);

		HEXDUMP("TSPEC: ADDTS data",
			paddts->ie_data, paddts->ie_data_len);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares the command of DELTS
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_wmm_delts_req(IN pmlan_private pmpriv,
		       OUT HostCmd_DS_COMMAND *cmd, IN t_void *pdata_buf)
{
	mlan_ds_wmm_delts *pdelts = (mlan_ds_wmm_delts *)pdata_buf;
	HostCmd_DS_WMM_DELTS_REQ *pcmd_delts = &cmd->params.del_ts;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_WMM_DELTS_REQ);
	cmd->size = wlan_cpu_to_le16(sizeof(pcmd_delts->dialog_token)
				     + sizeof(pcmd_delts->command_result)
				     + sizeof(pcmd_delts->ieee_reason_code)
				     + pdelts->ie_data_len + S_DS_GEN);
	cmd->result = 0;
	pcmd_delts->ieee_reason_code = (t_u8)pdelts->status_code;
	memcpy(pmpriv->adapter,
	       pcmd_delts->tspec_data,
	       pdelts->ie_data, MIN(WMM_TSPEC_SIZE, pdelts->ie_data_len));

	LEAVE();

	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of DELTS
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_wmm_delts_req(IN pmlan_private pmpriv,
		       const IN HostCmd_DS_COMMAND *resp,
		       OUT mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_wmm_cfg *pwmm;
	IEEEtypes_WMM_TSPEC_t *ptspec_ie;
	const HostCmd_DS_WMM_DELTS_REQ *presp_delts = &resp->params.del_ts;

	ENTER();

	if (pioctl_buf) {
		pwmm = (mlan_ds_wmm_cfg *)pioctl_buf->pbuf;
		pwmm->param.delts.result =
			wlan_le32_to_cpu(presp_delts->command_result);

		PRINTM(MINFO, "TSPEC: DELTS result = %d\n",
		       presp_delts->command_result);

		if (pwmm->param.delts.result == 0) {
			ptspec_ie =
				(IEEEtypes_WMM_TSPEC_t *)presp_delts->
				tspec_data;
			wlan_send_wmmac_host_event(pmpriv, "DELTS_TX", MNULL,
						   ptspec_ie->TspecBody.TSInfo.
						   TID,
						   ptspec_ie->TspecBody.TSInfo.
						   UserPri,
						   presp_delts->
						   ieee_reason_code);

		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares the command of WMM_QUEUE_STATS
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_wmm_queue_stats(IN pmlan_private pmpriv,
			 OUT HostCmd_DS_COMMAND *cmd, IN t_void *pdata_buf)
{
	mlan_ds_wmm_queue_stats *pqstats = (mlan_ds_wmm_queue_stats *)pdata_buf;
	HostCmd_DS_WMM_QUEUE_STATS *pcmd_qstats = &cmd->params.queue_stats;
	t_u8 id;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_WMM_QUEUE_STATS);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_WMM_QUEUE_STATS)
				     + S_DS_GEN);
	cmd->result = 0;

	pcmd_qstats->action = pqstats->action;
	pcmd_qstats->select_is_userpri = 1;
	pcmd_qstats->select_bin = pqstats->user_priority;
	pcmd_qstats->pkt_count = wlan_cpu_to_le16(pqstats->pkt_count);
	pcmd_qstats->pkt_loss = wlan_cpu_to_le16(pqstats->pkt_loss);
	pcmd_qstats->avg_queue_delay =
		wlan_cpu_to_le32(pqstats->avg_queue_delay);
	pcmd_qstats->avg_tx_delay = wlan_cpu_to_le32(pqstats->avg_tx_delay);
	pcmd_qstats->used_time = wlan_cpu_to_le16(pqstats->used_time);
	pcmd_qstats->policed_time = wlan_cpu_to_le16(pqstats->policed_time);
	for (id = 0; id < MLAN_WMM_STATS_PKTS_HIST_BINS; id++) {
		pcmd_qstats->delay_histogram[id] =
			wlan_cpu_to_le16(pqstats->delay_histogram[id]);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of WMM_QUEUE_STATS
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_wmm_queue_stats(IN pmlan_private pmpriv,
			 const IN HostCmd_DS_COMMAND *resp,
			 OUT mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_wmm_cfg *pwmm = MNULL;
	mlan_ds_wmm_queue_stats *pqstats = MNULL;
	const HostCmd_DS_WMM_QUEUE_STATS *presp_qstats =
		&resp->params.queue_stats;
	t_u8 id;

	ENTER();

	if (pioctl_buf) {
		pwmm = (mlan_ds_wmm_cfg *)pioctl_buf->pbuf;
		pqstats = (mlan_ds_wmm_queue_stats *)&pwmm->param.q_stats;

		pqstats->action = presp_qstats->action;
		pqstats->user_priority = presp_qstats->select_bin;
		pqstats->pkt_count = wlan_le16_to_cpu(presp_qstats->pkt_count);
		pqstats->pkt_loss = wlan_le16_to_cpu(presp_qstats->pkt_loss);
		pqstats->avg_queue_delay
			= wlan_le32_to_cpu(presp_qstats->avg_queue_delay);
		pqstats->avg_tx_delay
			= wlan_le32_to_cpu(presp_qstats->avg_tx_delay);
		pqstats->used_time = wlan_le16_to_cpu(presp_qstats->used_time);
		pqstats->policed_time
			= wlan_le16_to_cpu(presp_qstats->policed_time);
		for (id = 0; id < MLAN_WMM_STATS_PKTS_HIST_BINS; id++) {
			pqstats->delay_histogram[id]
				= wlan_le16_to_cpu(presp_qstats->
						   delay_histogram[id]);
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares the command of WMM_TS_STATUS
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_wmm_ts_status(IN pmlan_private pmpriv,
		       OUT HostCmd_DS_COMMAND *cmd, IN t_void *pdata_buf)
{
	mlan_ds_wmm_ts_status *pts_status = (mlan_ds_wmm_ts_status *)pdata_buf;
	HostCmd_DS_WMM_TS_STATUS *pcmd_ts_status = &cmd->params.ts_status;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_WMM_TS_STATUS);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_WMM_TS_STATUS)
				     + S_DS_GEN);
	cmd->result = 0;

	memcpy(pmpriv->adapter, (t_void *)pcmd_ts_status, (t_void *)pts_status,
	       sizeof(HostCmd_DS_WMM_TS_STATUS));

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of WMM_TS_STATUS
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_wmm_ts_status(IN pmlan_private pmpriv,
		       IN HostCmd_DS_COMMAND *resp,
		       OUT mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_wmm_cfg *pwmm = MNULL;
	HostCmd_DS_WMM_TS_STATUS *presp_ts_status = &resp->params.ts_status;

	ENTER();

	if (pioctl_buf) {
		pwmm = (mlan_ds_wmm_cfg *)pioctl_buf->pbuf;
		presp_ts_status->medium_time
			= wlan_le16_to_cpu(presp_ts_status->medium_time);
		memcpy(pmpriv->adapter,
		       (t_void *)&pwmm->param.ts_status,
		       (t_void *)presp_ts_status,
		       sizeof(mlan_ds_wmm_ts_status));
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Set/Get WMM status
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pioctl_req A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_wmm_ioctl_enable(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_wmm_cfg *wmm = MNULL;
	ENTER();
	wmm = (mlan_ds_wmm_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET)
		wmm->param.wmm_enable = (t_u32)pmpriv->wmm_required;
	else
		pmpriv->wmm_required = (t_u8)wmm->param.wmm_enable;
	pioctl_req->data_read_written = sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get WMM QoS configuration
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pioctl_req A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_wmm_ioctl_qos(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_wmm_cfg *wmm = MNULL;

	ENTER();

	wmm = (mlan_ds_wmm_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_GET)
		wmm->param.qos_cfg = pmpriv->wmm_qosinfo;
	else {
		pmpriv->wmm_qosinfo = wmm->param.qos_cfg;
		pmpriv->saved_wmm_qosinfo = wmm->param.qos_cfg;
	}

	pioctl_req->data_read_written = sizeof(t_u8) + MLAN_SUB_COMMAND_SIZE;

	LEAVE();
	return ret;
}

/**
 *  @brief Request for add a TSPEC
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_wmm_ioctl_addts_req(IN pmlan_adapter pmadapter,
			 IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_wmm_cfg *cfg = MNULL;

	ENTER();
	cfg = (mlan_ds_wmm_cfg *)pioctl_req->pbuf;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_WMM_ADDTS_REQ,
			       0, 0, (t_void *)pioctl_req,
			       (t_void *)&cfg->param.addts);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Request for delete a TSPEC
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_wmm_ioctl_delts_req(IN pmlan_adapter pmadapter,
			 IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_wmm_cfg *cfg = MNULL;

	ENTER();
	cfg = (mlan_ds_wmm_cfg *)pioctl_req->pbuf;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_WMM_DELTS_REQ,
			       0, 0, (t_void *)pioctl_req,
			       (t_void *)&cfg->param.delts);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief To get and start/stop queue stats on a WMM AC
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_wmm_ioctl_queue_stats(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_wmm_cfg *cfg = MNULL;

	ENTER();
	cfg = (mlan_ds_wmm_cfg *)pioctl_req->pbuf;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_WMM_QUEUE_STATS,
			       0, 0, (t_void *)pioctl_req,
			       (t_void *)&cfg->param.q_stats);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Get the status of the WMM AC queues
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_wmm_ioctl_queue_status(IN pmlan_adapter pmadapter,
			    IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_wmm_cfg *cfg = MNULL;
	mlan_ds_wmm_queue_status *pqstatus = MNULL;
	WmmAcStatus_t *pac_status = MNULL;
	mlan_wmm_ac_e ac_idx;

	ENTER();

	cfg = (mlan_ds_wmm_cfg *)pioctl_req->pbuf;
	pqstatus = (mlan_ds_wmm_queue_status *)&cfg->param.q_status;

	for (ac_idx = WMM_AC_BK; ac_idx <= WMM_AC_VO; ac_idx++) {
		pac_status = &pmpriv->wmm.ac_status[ac_idx];

		/* Firmware status */
		pqstatus->ac_status[ac_idx].flow_required =
			pac_status->flow_required;
		pqstatus->ac_status[ac_idx].flow_created =
			pac_status->flow_created;
		pqstatus->ac_status[ac_idx].disabled = pac_status->disabled;

		/* ACM bit reflected in firmware status (redundant) */
		pqstatus->ac_status[ac_idx].wmm_acm = pac_status->flow_required;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Get the status of the WMM Traffic Streams
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_wmm_ioctl_ts_status(IN pmlan_adapter pmadapter,
			 IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_wmm_cfg *cfg = MNULL;

	ENTER();

	cfg = (mlan_ds_wmm_cfg *)pioctl_req->pbuf;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_WMM_TS_STATUS,
			       0, 0, (t_void *)pioctl_req,
			       (t_void *)&cfg->param.ts_status);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}
#endif /* STA_SUPPORT */

/**
 *  @brief This function prepares the command of WMM_PARAM_CONFIG
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   cmd action.
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_wmm_param_config(IN pmlan_private pmpriv,
			  OUT HostCmd_DS_COMMAND *cmd,
			  IN t_u8 cmd_action, IN t_void *pdata_buf)
{
	wmm_ac_parameters_t *ac_params = (wmm_ac_parameters_t *)pdata_buf;
	HostCmd_DS_WMM_PARAM_CONFIG *pcmd_cfg = &cmd->params.param_config;
	t_u8 i = 0;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_WMM_PARAM_CONFIG);
	cmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_WMM_PARAM_CONFIG) +
				 S_DS_GEN);
	cmd->result = 0;

	pcmd_cfg->action = cmd_action;
	if (cmd_action == HostCmd_ACT_GEN_SET) {
		memcpy(pmpriv->adapter, pcmd_cfg->ac_params, ac_params,
		       sizeof(wmm_ac_parameters_t) * MAX_AC_QUEUES);
		for (i = 0; i < MAX_AC_QUEUES; i++) {
			pcmd_cfg->ac_params[i].tx_op_limit =
				wlan_cpu_to_le16(pcmd_cfg->ac_params[i].
						 tx_op_limit);
		}
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of WMM_PARAM_CONFIG
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_wmm_param_config(IN pmlan_private pmpriv,
			  const IN HostCmd_DS_COMMAND *resp,
			  OUT mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_wmm_cfg *pwmm = MNULL;
	HostCmd_DS_WMM_PARAM_CONFIG *pcfg =
		(HostCmd_DS_WMM_PARAM_CONFIG *) & resp->params.param_config;
	t_u8 i;

	ENTER();

	if (pioctl_buf) {
		pwmm = (mlan_ds_wmm_cfg *)pioctl_buf->pbuf;
		for (i = 0; i < MAX_AC_QUEUES; i++) {
			pcfg->ac_params[i].tx_op_limit =
				wlan_le16_to_cpu(pcfg->ac_params[i].
						 tx_op_limit);
		}
		memcpy(pmpriv->adapter, pwmm->param.ac_params, pcfg->ac_params,
		       sizeof(wmm_ac_parameters_t) * MAX_AC_QUEUES);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares the command of WMM_QUEUE_CONFIG
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_wmm_queue_config(IN pmlan_private pmpriv,
			  OUT HostCmd_DS_COMMAND *cmd, IN t_void *pdata_buf)
{
	mlan_ds_wmm_queue_config *pqcfg = (mlan_ds_wmm_queue_config *)pdata_buf;
	HostCmd_DS_WMM_QUEUE_CONFIG *pcmd_qcfg = &cmd->params.queue_config;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_WMM_QUEUE_CONFIG);
	cmd->size = wlan_cpu_to_le16(sizeof(pcmd_qcfg->action)
				     + sizeof(pcmd_qcfg->access_category)
				     + sizeof(pcmd_qcfg->msdu_lifetime_expiry)
				     + S_DS_GEN);
	cmd->result = 0;

	pcmd_qcfg->action = pqcfg->action;
	pcmd_qcfg->access_category = pqcfg->access_category;
	pcmd_qcfg->msdu_lifetime_expiry =
		wlan_cpu_to_le16(pqcfg->msdu_lifetime_expiry);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of WMM_QUEUE_CONFIG
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_wmm_queue_config(IN pmlan_private pmpriv,
			  const IN HostCmd_DS_COMMAND *resp,
			  OUT mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_wmm_cfg *pwmm = MNULL;
	const HostCmd_DS_WMM_QUEUE_CONFIG *presp_qcfg =
		&resp->params.queue_config;

	ENTER();

	if (pioctl_buf) {
		pwmm = (mlan_ds_wmm_cfg *)pioctl_buf->pbuf;
		pwmm->param.q_cfg.action = wlan_le32_to_cpu(presp_qcfg->action);
		pwmm->param.q_cfg.access_category =
			wlan_le32_to_cpu(presp_qcfg->access_category);
		pwmm->param.q_cfg.msdu_lifetime_expiry =
			wlan_le16_to_cpu(presp_qcfg->msdu_lifetime_expiry);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Set/Get a specified AC Queue's parameters
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_wmm_ioctl_queue_config(IN pmlan_adapter pmadapter,
			    IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_wmm_cfg *cfg = MNULL;

	ENTER();
	cfg = (mlan_ds_wmm_cfg *)pioctl_req->pbuf;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_WMM_QUEUE_CONFIG,
			       0, 0, (t_void *)pioctl_req,
			       (t_void *)&cfg->param.q_cfg);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief WMM configuration handler
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pioctl_req A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_wmm_cfg_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_wmm_cfg *wmm = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_wmm_cfg)) {
		PRINTM(MWARN, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_wmm_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	wmm = (mlan_ds_wmm_cfg *)pioctl_req->pbuf;
	switch (wmm->sub_command) {
#ifdef STA_SUPPORT
	case MLAN_OID_WMM_CFG_ENABLE:
		status = wlan_wmm_ioctl_enable(pmadapter, pioctl_req);
		break;
	case MLAN_OID_WMM_CFG_QOS:
		status = wlan_wmm_ioctl_qos(pmadapter, pioctl_req);
		break;
	case MLAN_OID_WMM_CFG_ADDTS:
		status = wlan_wmm_ioctl_addts_req(pmadapter, pioctl_req);
		break;
	case MLAN_OID_WMM_CFG_DELTS:
		status = wlan_wmm_ioctl_delts_req(pmadapter, pioctl_req);
		break;
	case MLAN_OID_WMM_CFG_QUEUE_STATS:
		status = wlan_wmm_ioctl_queue_stats(pmadapter, pioctl_req);
		break;
	case MLAN_OID_WMM_CFG_QUEUE_STATUS:
		status = wlan_wmm_ioctl_queue_status(pmadapter, pioctl_req);
		break;
	case MLAN_OID_WMM_CFG_TS_STATUS:
		status = wlan_wmm_ioctl_ts_status(pmadapter, pioctl_req);
		break;
#endif
	case MLAN_OID_WMM_CFG_QUEUE_CONFIG:
		status = wlan_wmm_ioctl_queue_config(pmadapter, pioctl_req);
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}
	LEAVE();
	return status;
}

/**
 *  @brief Get ralist info
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param buf          A pointer to ralist_info structure
 *  @return             number of ralist entry
 *
 */
int
wlan_get_ralist_info(mlan_private *priv, ralist_info *buf)
{
	ralist_info *plist = buf;
	mlan_list_head *ra_list_head = MNULL;
	raListTbl *ra_list;
	int i;
	int count = 0;
	for (i = 0; i < MAX_NUM_TID; i++) {
		ra_list_head = &priv->wmm.tid_tbl_ptr[i].ra_list;
		ra_list =
			(raListTbl *)util_peek_list(priv->adapter->pmoal_handle,
						    ra_list_head, MNULL, MNULL);
		while (ra_list && ra_list != (raListTbl *)ra_list_head) {
			if (ra_list->total_pkts) {
				plist->total_pkts = ra_list->total_pkts;
				plist->tid = i;
				plist->tx_pause = ra_list->tx_pause;
				memcpy(priv->adapter, plist->ra, ra_list->ra,
				       MLAN_MAC_ADDR_LENGTH);
				plist++;
				count++;
				if (count >= MLAN_MAX_RALIST_NUM)
					break;
			}
			ra_list = ra_list->pnext;
		}
	}
	LEAVE();
	return count;
}

/**
 *  @brief dump ralist info
 *
 *  @param priv         A pointer to mlan_private structure
 *
 *  @return             N/A
 *
 */
void
wlan_dump_ralist(mlan_private *priv)
{
	mlan_list_head *ra_list_head = MNULL;
	raListTbl *ra_list;
	mlan_adapter *pmadapter = priv->adapter;
	int i;
	t_u32 tx_pkts_queued;

	tx_pkts_queued =
		util_scalar_read(pmadapter->pmoal_handle,
				 &priv->wmm.tx_pkts_queued, MNULL, MNULL);
	PRINTM(MERROR, "bss_index = %d, tx_pkts_queued = %d\n", priv->bss_index,
	       tx_pkts_queued);
	if (!tx_pkts_queued)
		return;
	for (i = 0; i < MAX_NUM_TID; i++) {
		ra_list_head = &priv->wmm.tid_tbl_ptr[i].ra_list;
		ra_list =
			(raListTbl *)util_peek_list(priv->adapter->pmoal_handle,
						    ra_list_head, MNULL, MNULL);
		while (ra_list && ra_list != (raListTbl *)ra_list_head) {
			if (ra_list->total_pkts) {
				PRINTM(MERROR,
				       "ralist ra: %02x:%02x:%02x:%02x:%02x:%02x tid=%d pkts=%d pause=%d\n",
				       ra_list->ra[0], ra_list->ra[1],
				       ra_list->ra[2], ra_list->ra[3],
				       ra_list->ra[4], ra_list->ra[5], i,
				       ra_list->total_pkts, ra_list->tx_pause);
			}
			ra_list = ra_list->pnext;
		}
	}
	return;
}

/**
 *  @brief get tid down
 *
 *  @param priv         A pointer to mlan_private structure
 * 	@param tid 			tid
 *
 *  @return             tid_down
 *
 */
int
wlan_get_wmm_tid_down(mlan_private *priv, int tid)
{
	return wlan_wmm_downgrade_tid(priv, tid);
}
