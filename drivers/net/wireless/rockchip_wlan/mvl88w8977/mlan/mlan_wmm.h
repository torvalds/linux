/** @file mlan_wmm.h
 *
 *  @brief This file contains related macros, enum, and struct
 *  of wmm functionalities
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

/****************************************************
Change log:
    10/24/2008: initial version
****************************************************/

#ifndef _MLAN_WMM_H_
#define _MLAN_WMM_H_

/**
 *  @brief This function gets the TID
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param ptr          A pointer to RA list table
 *
 *  @return             TID
 */
static INLINE int
wlan_get_tid(pmlan_adapter pmadapter, raListTbl *ptr)
{
	pmlan_buffer mbuf;

	ENTER();
	mbuf = (pmlan_buffer)util_peek_list(pmadapter->pmoal_handle,
					    &ptr->buf_head, MNULL, MNULL);
	LEAVE();

	return mbuf->priority;
}

/**
 *  @brief This function gets the length of a list
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param head         A pointer to mlan_list_head
 *
 *  @return             Length of list
 */
static INLINE int
wlan_wmm_list_len(pmlan_adapter pmadapter, pmlan_list_head head)
{
	pmlan_linked_list pos;
	int count = 0;

	ENTER();

	pos = head->pnext;

	while (pos != (pmlan_linked_list)head) {
		++count;
		pos = pos->pnext;
	}

	LEAVE();
	return count;
}

/**
 *  @brief This function requests a ralist lock
 *
 *  @param priv         A pointer to mlan_private structure
 *
 *  @return             N/A
 */
static INLINE t_void
wlan_request_ralist_lock(IN mlan_private *priv)
{
	mlan_adapter *pmadapter = priv->adapter;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;

	ENTER();

	/* Call MOAL spin lock callback function */
	pcb->moal_spin_lock(pmadapter->pmoal_handle,
			    priv->wmm.ra_list_spinlock);

	LEAVE();
	return;
}

/**
 *  @brief This function releases a lock on ralist
 *
 *  @param priv         A pointer to mlan_private structure
 *
 *  @return             N/A
 */
static INLINE t_void
wlan_release_ralist_lock(IN mlan_private *priv)
{
	mlan_adapter *pmadapter = priv->adapter;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;

	ENTER();

	/* Call MOAL spin unlock callback function */
	pcb->moal_spin_unlock(pmadapter->pmoal_handle,
			      priv->wmm.ra_list_spinlock);

	LEAVE();
	return;
}

/** Add buffer to WMM Tx queue */
void wlan_wmm_add_buf_txqueue(pmlan_adapter pmadapter, pmlan_buffer pmbuf);
/** Add to RA list */
void wlan_ralist_add(mlan_private *priv, t_u8 *ra);
/** Update the RA list */
int wlan_ralist_update(mlan_private *priv, t_u8 *old_ra, t_u8 *new_ra);

/** WMM status change command handler */
mlan_status wlan_cmd_wmm_status_change(pmlan_private priv);
/** Check if WMM lists are empty */
int wlan_wmm_lists_empty(pmlan_adapter pmadapter);
/** Process WMM transmission */
t_void wlan_wmm_process_tx(pmlan_adapter pmadapter);
/** Test to see if the ralist ptr is valid */
int wlan_is_ralist_valid(mlan_private *priv, raListTbl *ra_list, int tid);

raListTbl *wlan_wmm_get_ralist_node(pmlan_private priv, t_u8 tid,
				    t_u8 *ra_addr);
t_u8 wlan_get_random_ba_threshold(pmlan_adapter pmadapter);

/** Compute driver packet delay */
t_u8 wlan_wmm_compute_driver_packet_delay(pmlan_private priv,
					  const pmlan_buffer pmbuf);
/** Initialize WMM */
t_void wlan_wmm_init(pmlan_adapter pmadapter);
/** Initialize WMM paramter */
t_void wlan_init_wmm_param(pmlan_adapter pmadapter);
/** Setup WMM queues */
extern void wlan_wmm_setup_queues(pmlan_private priv);
/* Setup default queues */
void wlan_wmm_default_queue_priorities(pmlan_private priv);
/* process wmm_param_config command */
mlan_status wlan_cmd_wmm_param_config(IN pmlan_private pmpriv,
				      OUT HostCmd_DS_COMMAND *cmd,
				      IN t_u8 cmd_action, IN t_void *pdata_buf);

/* process wmm_param_config command response */
mlan_status wlan_ret_wmm_param_config(IN pmlan_private pmpriv,
				      const IN HostCmd_DS_COMMAND *resp,
				      OUT mlan_ioctl_req *pioctl_buf);

#ifdef STA_SUPPORT
/** Process WMM association request */
extern t_u32 wlan_wmm_process_association_req(pmlan_private priv,
					      t_u8 **ppAssocBuf,
					      IEEEtypes_WmmParameter_t *pWmmIE,
					      IEEEtypes_HTCap_t *pHTCap);
#endif /* STA_SUPPORT */

/** setup wmm queue priorities */
void wlan_wmm_setup_queue_priorities(pmlan_private priv,
				     IEEEtypes_WmmParameter_t *wmm_ie);

/* Get tid_down from tid */
int wlan_get_wmm_tid_down(mlan_private *priv, int tid);
/** Downgrade WMM priority queue */
void wlan_wmm_setup_ac_downgrade(pmlan_private priv);
/** select WMM queue */
t_u8 wlan_wmm_select_queue(mlan_private *pmpriv, t_u8 tid);
t_void wlan_wmm_delete_peer_ralist(pmlan_private priv, t_u8 *mac);

#ifdef STA_SUPPORT
/*
 *  Functions used in the cmd handling routine
 */
/** WMM ADDTS request command handler */
extern mlan_status wlan_cmd_wmm_addts_req(IN pmlan_private pmpriv,
					  OUT HostCmd_DS_COMMAND *cmd,
					  IN t_void *pdata_buf);
/** WMM DELTS request command handler */
extern mlan_status wlan_cmd_wmm_delts_req(IN pmlan_private pmpriv,
					  OUT HostCmd_DS_COMMAND *cmd,
					  IN t_void *pdata_buf);
/** WMM QUEUE_STATS command handler */
extern mlan_status wlan_cmd_wmm_queue_stats(IN pmlan_private pmpriv,
					    OUT HostCmd_DS_COMMAND *cmd,
					    IN t_void *pdata_buf);
/** WMM TS_STATUS command handler */
extern mlan_status wlan_cmd_wmm_ts_status(IN pmlan_private pmpriv,
					  OUT HostCmd_DS_COMMAND *cmd,
					  IN t_void *pdata_buf);

/*
 *  Functions used in the cmdresp handling routine
 */
/** WMM get status command response handler */
extern mlan_status wlan_ret_wmm_get_status(IN pmlan_private priv,
					   IN t_u8 *ptlv, IN int resp_len);
/** WMM ADDTS request command response handler */
extern mlan_status wlan_ret_wmm_addts_req(IN pmlan_private pmpriv,
					  const IN HostCmd_DS_COMMAND *resp,
					  OUT mlan_ioctl_req *pioctl_buf);
/** WMM DELTS request command response handler */
extern mlan_status wlan_ret_wmm_delts_req(IN pmlan_private pmpriv,
					  const IN HostCmd_DS_COMMAND *resp,
					  OUT mlan_ioctl_req *pioctl_buf);
/** WMM QUEUE_STATS command response handler */
extern mlan_status wlan_ret_wmm_queue_stats(IN pmlan_private pmpriv,
					    const IN HostCmd_DS_COMMAND *resp,
					    OUT mlan_ioctl_req *pioctl_buf);
/** WMM TS_STATUS command response handler */
extern mlan_status wlan_ret_wmm_ts_status(IN pmlan_private pmpriv,
					  IN HostCmd_DS_COMMAND *resp,
					  OUT mlan_ioctl_req *pioctl_buf);
#endif /* STA_SUPPORT */

/** WMM QUEUE_CONFIG command handler */
extern mlan_status wlan_cmd_wmm_queue_config(IN pmlan_private pmpriv,
					     OUT HostCmd_DS_COMMAND *cmd,
					     IN t_void *pdata_buf);

/** WMM QUEUE_CONFIG command response handler */
extern mlan_status wlan_ret_wmm_queue_config(IN pmlan_private pmpriv,
					     const IN HostCmd_DS_COMMAND *resp,
					     OUT mlan_ioctl_req *pioctl_buf);

mlan_status wlan_wmm_cfg_ioctl(IN pmlan_adapter pmadapter,
			       IN pmlan_ioctl_req pioctl_req);
#endif /* !_MLAN_WMM_H_ */
