/** @file mlan_11n.h
 *
 *  @brief Interface for the 802.11n mlan_11n module implemented in mlan_11n.c
 *
 *  Driver interface functions and type declarations for the 11n module
 *    implemented in mlan_11n.c.
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
 *
 */

/********************************************************
Change log:
    12/01/2008: initial version
********************************************************/

#ifndef _MLAN_11N_H_
#define _MLAN_11N_H_

#include "mlan_11n_aggr.h"
#include "mlan_11n_rxreorder.h"
#include "mlan_wmm.h"

/** Print the 802.11n device capability */
void wlan_show_dot11ndevcap(pmlan_adapter pmadapter, t_u32 cap);
/** Print the 802.11n device MCS */
void wlan_show_devmcssupport(pmlan_adapter pmadapter, t_u8 support);
/** Handle the command response of a delete block ack request */
mlan_status wlan_ret_11n_delba(mlan_private *priv, HostCmd_DS_COMMAND *resp);
/** Handle the command response of an add block ack request */
mlan_status wlan_ret_11n_addba_req(mlan_private *priv,
				   HostCmd_DS_COMMAND *resp);
/** Handle the command response of 11ncfg command */
mlan_status wlan_ret_11n_cfg(IN pmlan_private pmpriv,
			     IN HostCmd_DS_COMMAND *resp,
			     IN mlan_ioctl_req *pioctl_buf);
/** Prepare 11ncfg command */
mlan_status wlan_cmd_11n_cfg(IN pmlan_private pmpriv,
			     IN HostCmd_DS_COMMAND *cmd, IN t_u16 cmd_action,
			     IN t_void *pdata_buf);
/** Prepare reject addba requst command */
mlan_status wlan_cmd_reject_addba_req(IN pmlan_private pmpriv,
				      IN HostCmd_DS_COMMAND *cmd,
				      IN t_u16 cmd_action,
				      IN t_void *pdata_buf);
/** Handle the command response of rejecting addba request */
mlan_status wlan_ret_reject_addba_req(IN pmlan_private pmpriv,
				      IN HostCmd_DS_COMMAND *resp,
				      IN mlan_ioctl_req *pioctl_buf);
/** Prepare TX BF configuration command */
mlan_status wlan_cmd_tx_bf_cfg(IN pmlan_private pmpriv,
			       IN HostCmd_DS_COMMAND *cmd,
			       IN t_u16 cmd_action, IN t_void *pdata_buf);
/** Handle the command response TX BF configuration */
mlan_status wlan_ret_tx_bf_cfg(IN pmlan_private pmpriv,
			       IN HostCmd_DS_COMMAND *resp,
			       IN mlan_ioctl_req *pioctl_buf);
#ifdef STA_SUPPORT
t_u8 wlan_11n_bandconfig_allowed(mlan_private *pmpriv, t_u8 bss_band);
/** Append the 802_11N tlv */
int wlan_cmd_append_11n_tlv(IN mlan_private *pmpriv,
			    IN BSSDescriptor_t *pbss_desc, OUT t_u8 **ppbuffer);
/** wlan fill HT cap tlv */
void wlan_fill_ht_cap_tlv(mlan_private *priv, MrvlIETypes_HTCap_t *pht_cap,
			  t_u8 band, t_u8 fill);
/** wlan fill HT cap IE */
void wlan_fill_ht_cap_ie(mlan_private *priv, IEEEtypes_HTCap_t *pht_cap,
			 t_u8 bands);
#endif /* STA_SUPPORT */
/** Miscellaneous configuration handler */
mlan_status wlan_11n_cfg_ioctl(IN pmlan_adapter pmadapter,
			       IN pmlan_ioctl_req pioctl_req);
/** Delete Tx BA stream table entry */
void wlan_11n_delete_txbastream_tbl_entry(mlan_private *priv,
					  TxBAStreamTbl *ptx_tbl);
/** Delete all Tx BA stream table entries */
void wlan_11n_deleteall_txbastream_tbl(mlan_private *priv);
/** Get Tx BA stream table */
TxBAStreamTbl *wlan_11n_get_txbastream_tbl(mlan_private *priv, int tid,
					   t_u8 *ra, int lock);
/** Create Tx BA stream table */
void wlan_11n_create_txbastream_tbl(mlan_private *priv, t_u8 *ra, int tid,
				    baStatus_e ba_status);
/** Send ADD BA request */
int wlan_send_addba(mlan_private *priv, int tid, t_u8 *peer_mac);
/** Send DEL BA request */
int wlan_send_delba(mlan_private *priv, pmlan_ioctl_req pioctl_req, int tid,
		    t_u8 *peer_mac, int initiator);
/** This function handles the command response of delete a block ack request*/
void wlan_11n_delete_bastream(mlan_private *priv, t_u8 *del_ba);
/** get rx reorder table */
int wlan_get_rxreorder_tbl(mlan_private *priv, rx_reorder_tbl *buf);
/** get tx ba stream table */
int wlan_get_txbastream_tbl(mlan_private *priv, tx_ba_stream_tbl *buf);
/** send delba */
void wlan_11n_delba(mlan_private *priv, int tid);
/** update amdpdu tx win size */
void wlan_update_ampdu_txwinsize(pmlan_adapter pmadapter);
/** Minimum number of AMSDU */
#define MIN_NUM_AMSDU 2
/** AMSDU Aggr control cmd resp */
mlan_status wlan_ret_amsdu_aggr_ctrl(pmlan_private pmpriv,
				     HostCmd_DS_COMMAND *resp,
				     mlan_ioctl_req *pioctl_buf);
void wlan_set_tx_pause_flag(mlan_private *priv, t_u8 flag);
/** reconfigure tx buf size */
mlan_status wlan_cmd_recfg_tx_buf(mlan_private *priv,
				  HostCmd_DS_COMMAND *cmd,
				  int cmd_action, void *pdata_buf);
/** AMSDU aggr control cmd */
mlan_status wlan_cmd_amsdu_aggr_ctrl(mlan_private *priv,
				     HostCmd_DS_COMMAND *cmd,
				     int cmd_action, void *pdata_buf);

t_u8 wlan_validate_chan_offset(IN mlan_private *pmpriv,
			       IN t_u8 band, IN t_u32 chan, IN t_u8 chan_bw);
/** get channel offset */
t_u8 wlan_get_second_channel_offset(int chan);

void wlan_update_11n_cap(mlan_private *pmpriv);

/** clean up txbastream_tbl */
void wlan_11n_cleanup_txbastream_tbl(mlan_private *priv, t_u8 *ra);
/**
 *  @brief This function checks whether a station has 11N enabled or not
 *
 *  @param priv     A pointer to mlan_private
 *  @param mac      station mac address
 *  @return         MTRUE or MFALSE
 */
static INLINE t_u8
is_station_11n_enabled(mlan_private *priv, t_u8 *mac)
{
	sta_node *sta_ptr = MNULL;
	sta_ptr = wlan_get_station_entry(priv, mac);
	if (sta_ptr)
		return (sta_ptr->is_11n_enabled) ? MTRUE : MFALSE;
	return MFALSE;
}

/**
 *  @brief This function get station max amsdu size
 *
 *  @param priv     A pointer to mlan_private
 *  @param mac      station mac address
 *  @return         max amsdu size statio supported
 */
static INLINE t_u16
get_station_max_amsdu_size(mlan_private *priv, t_u8 *mac)
{
	sta_node *sta_ptr = MNULL;
	sta_ptr = wlan_get_station_entry(priv, mac);
	if (sta_ptr)
		return sta_ptr->max_amsdu;
	return 0;
}

/**
 *  @brief This function checks whether a station allows AMPDU or not
 *
 *  @param priv     A pointer to mlan_private
 *  @param ptr      A pointer to RA list table
 *  @param tid      TID value for ptr
 *  @return         MTRUE or MFALSE
 */
static INLINE t_u8
is_station_ampdu_allowed(mlan_private *priv, raListTbl *ptr, int tid)
{
	sta_node *sta_ptr = MNULL;
	sta_ptr = wlan_get_station_entry(priv, ptr->ra);
	if (sta_ptr) {
		if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
			if (priv->sec_info.wapi_enabled &&
			    !sta_ptr->wapi_key_on)
				return MFALSE;
		}
		return (sta_ptr->ampdu_sta[tid] != BA_STREAM_NOT_ALLOWED)
			? MTRUE : MFALSE;
	}
	return MFALSE;
}

/**
 *  @brief This function disable station ampdu for specific tid
 *
 *  @param priv     A pointer to mlan_private
 *  @param tid     tid index
 *  @param ra      station mac address
 *  @return        N/A
 */
static INLINE void
disable_station_ampdu(mlan_private *priv, t_u8 tid, t_u8 *ra)
{
	sta_node *sta_ptr = MNULL;
	sta_ptr = wlan_get_station_entry(priv, ra);
	if (sta_ptr)
		sta_ptr->ampdu_sta[tid] = BA_STREAM_NOT_ALLOWED;
	return;
}

/**
 *  @brief This function reset station ampdu for specific id to user setting.
 *
 *  @param priv     A pointer to mlan_private
 *  @param tid     tid index
 *  @param ra      station mac address
 *  @return        N/A
 */
static INLINE void
reset_station_ampdu(mlan_private *priv, t_u8 tid, t_u8 *ra)
{
	sta_node *sta_ptr = MNULL;
	sta_ptr = wlan_get_station_entry(priv, ra);
	if (sta_ptr)
		sta_ptr->ampdu_sta[tid] = priv->aggr_prio_tbl[tid].ampdu_user;
	return;
}

/**
 *  @brief This function checks whether AMPDU is allowed or not
 *
 *  @param priv     A pointer to mlan_private
 *  @param ptr      A pointer to RA list table
 *  @param tid      TID value for ptr
 *
 *  @return         MTRUE or MFALSE
 */
static INLINE t_u8
wlan_is_ampdu_allowed(mlan_private *priv, raListTbl *ptr, int tid)
{
#ifdef UAP_SUPPORT
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP)
		return is_station_ampdu_allowed(priv, ptr, tid);
#endif /* UAP_SUPPORT */
	if (priv->sec_info.wapi_enabled && !priv->sec_info.wapi_key_on)
		return MFALSE;
	if (ptr->is_tdls_link)
		return is_station_ampdu_allowed(priv, ptr, tid);
	if (priv->adapter->tdls_status != TDLS_NOT_SETUP && !priv->txaggrctrl)
		return MFALSE;
	if (priv->bss_mode == MLAN_BSS_MODE_IBSS)
		return is_station_ampdu_allowed(priv, ptr, tid);
	return (priv->aggr_prio_tbl[tid].ampdu_ap != BA_STREAM_NOT_ALLOWED)
		? MTRUE : MFALSE;
}

/**
 *  @brief This function checks whether AMSDU is allowed or not
 *
 *  @param priv     A pointer to mlan_private
 *  @param ptr      A pointer to RA list table
 *  @param tid      TID value for ptr
 *
 *  @return         MTRUE or MFALSE
 */
static INLINE t_u8
wlan_is_amsdu_allowed(mlan_private *priv, raListTbl *ptr, int tid)
{
#ifdef UAP_SUPPORT
	sta_node *sta_ptr = MNULL;
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
		sta_ptr = wlan_get_station_entry(priv, ptr->ra);
		if (sta_ptr) {
			if (priv->sec_info.wapi_enabled &&
			    !sta_ptr->wapi_key_on)
				return MFALSE;
		}
	}
#endif /* UAP_SUPPORT */
#define TXRATE_BITMAP_INDEX_MCS0_7 2
	return ((priv->aggr_prio_tbl[tid].amsdu != BA_STREAM_NOT_ALLOWED)
		&&((priv->is_data_rate_auto)
		   || !((priv->bitmap_rates[TXRATE_BITMAP_INDEX_MCS0_7]) &
			0x03))) ? MTRUE : MFALSE;
}

/**
 *  @brief This function checks whether a BA stream is available or not
 *
 *  @param priv     A pointer to mlan_private
 *
 *  @return         MTRUE or MFALSE
 */
static INLINE t_u8
wlan_is_bastream_avail(mlan_private *priv)
{
	mlan_private *pmpriv = MNULL;
	t_u8 i = 0;
	t_u32 bastream_num = 0;
	t_u32 bastream_max = 0;
	for (i = 0; i < priv->adapter->priv_num; i++) {
		pmpriv = priv->adapter->priv[i];
		if (pmpriv)
			bastream_num +=
				wlan_wmm_list_len(priv->adapter,
						  (pmlan_list_head)&pmpriv->
						  tx_ba_stream_tbl_ptr);
	}
	bastream_max = ISSUPP_GETTXBASTREAM(priv->adapter->hw_dot_11n_dev_cap);
	if (bastream_max == 0)
		bastream_max = MLAN_MAX_TX_BASTREAM_DEFAULT;
	return (bastream_num < bastream_max) ? MTRUE : MFALSE;
}

/**
 *  @brief This function finds the stream to delete
 *
 *  @param priv     A pointer to mlan_private
 *  @param ptr      A pointer to RA list table
 *  @param ptr_tid  TID value of ptr
 *  @param ptid     A pointer to TID of stream to delete, if return MTRUE
 *  @param ra       RA of stream to delete, if return MTRUE
 *
 *  @return         MTRUE or MFALSE
 */
static INLINE t_u8
wlan_find_stream_to_delete(mlan_private *priv,
			   raListTbl *ptr, int ptr_tid, int *ptid, t_u8 *ra)
{
	int tid;
	t_u8 ret = MFALSE;
	TxBAStreamTbl *ptx_tbl;

	ENTER();

	ptx_tbl = (TxBAStreamTbl *)util_peek_list(priv->adapter->pmoal_handle,
						  &priv->tx_ba_stream_tbl_ptr,
						  MNULL, MNULL);
	if (!ptx_tbl) {
		LEAVE();
		return ret;
	}

	tid = priv->aggr_prio_tbl[ptr_tid].ampdu_user;

	while (ptx_tbl != (TxBAStreamTbl *)&priv->tx_ba_stream_tbl_ptr) {
		if (tid > priv->aggr_prio_tbl[ptx_tbl->tid].ampdu_user) {
			tid = priv->aggr_prio_tbl[ptx_tbl->tid].ampdu_user;
			*ptid = ptx_tbl->tid;
			memcpy(priv->adapter, ra, ptx_tbl->ra,
			       MLAN_MAC_ADDR_LENGTH);
			ret = MTRUE;
		}

		ptx_tbl = ptx_tbl->pnext;
	}
	LEAVE();
	return ret;
}

/**
 *  @brief This function checks whether 11n is supported
 *
 *  @param priv     A pointer to mlan_private
 *  @param ra       Address of the receiver STA
 *
 *  @return         MTRUE or MFALSE
 */
static int INLINE
wlan_is_11n_enabled(mlan_private *priv, t_u8 *ra)
{
	int ret = MFALSE;
	ENTER();
#ifdef UAP_SUPPORT
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
		if ((!(ra[0] & 0x01)) && (priv->is_11n_enabled))
			ret = is_station_11n_enabled(priv, ra);
	}
#endif /* UAP_SUPPORT */
#ifdef STA_SUPPORT
	if (priv->bss_mode == MLAN_BSS_MODE_IBSS) {
		if ((!(ra[0] & 0x01)) && (priv->adapter->adhoc_11n_enabled))
			ret = is_station_11n_enabled(priv, ra);
	}
#endif
	LEAVE();
	return ret;
}
#endif /* !_MLAN_11N_H_ */
