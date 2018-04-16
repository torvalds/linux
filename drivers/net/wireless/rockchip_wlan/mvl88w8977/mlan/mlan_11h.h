/** @file mlan_11h.h
 *
 *  @brief This header file contains data structures and
 *  function declarations of 802.11h
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

/*************************************************************
Change Log:
    03/26/2009: initial creation
*************************************************************/

#ifndef _MLAN_11H_
#define _MLAN_11H_

/** 11H OID bitmasks */
#define ENABLE_11H_MASK         MBIT(0)
#define MASTER_RADAR_DET_MASK   MBIT(1)
#define SLAVE_RADAR_DET_MASK    MBIT(2)

/** DFS Master Radar Detect global enable */
#define DFS_MASTER_RADAR_DETECT_EN  (MTRUE)
/** DFS Slave Radar Detect global enable */
#define DFS_SLAVE_RADAR_DETECT_EN   (MFALSE)

#define CHANNEL_OFFSET_MASK      0x30
#define CHANNEL_BANDWIDTH_MASK   0x0C

/**
 *  11H APIs
 */

/* Is master radar detection enabled in firmware? */
extern t_bool wlan_11h_is_master_radar_det_active(mlan_private *priv);

/** Configure master radar detection.
 *  Need call wlan_11h_check_update_radar_det_state() after.
 */
extern mlan_status wlan_11h_config_master_radar_det(mlan_private *priv,
						    t_bool enable);

/** Configure slave radar detection.
 *  Need call wlan_11h_check_update_radar_det_state() after.
 */
extern mlan_status wlan_11h_config_slave_radar_det(mlan_private *priv,
						   t_bool enable);

/** Checks all interfaces and updates radar detect flags if necessary */
extern mlan_status wlan_11h_check_update_radar_det_state(mlan_private *pmpriv);

/** Return 1 if 11h is active in the firmware, 0 if it is inactive */
extern t_bool wlan_11h_is_active(mlan_private *priv);

/** Enable the tx interface and record the new transmit state */
extern void wlan_11h_tx_enable(mlan_private *priv);

/** Disable the tx interface and record the new transmit state */
extern void wlan_11h_tx_disable(mlan_private *priv);

/** Activate 11h extensions in the firmware */
extern mlan_status wlan_11h_activate(mlan_private *priv, t_void *pioctl_buf,
				     t_bool flag);

/** Initialize the 11h device structure */
extern void wlan_11h_init(mlan_adapter *pmadapter);

/** Cleanup for the 11h device structure */
extern void wlan_11h_cleanup(mlan_adapter *pmadapter);

/** Initialize the 11h interface structure */
extern void wlan_11h_priv_init(mlan_private *pmpriv);

/** Get an initial random channel to start an adhoc network on */
extern t_u8 wlan_11h_get_adhoc_start_channel(mlan_private *priv);

/** Get channel that has been closed via Channel Switch Announcement */
extern t_u8 wlan_11h_get_csa_closed_channel(mlan_private *priv);

/** Check if radar detection is required on the specified channel */
extern t_bool wlan_11h_radar_detect_required(mlan_private *priv, t_u8 channel);

/** Perform a standard availibility check on the specified channel */
extern t_s32 wlan_11h_issue_radar_detect(mlan_private *priv,
					 pmlan_ioctl_req pioctl_req,
					 t_u8 channel, Band_Config_t bandcfg);

/** Check previously issued radar report for a channel */
extern mlan_status wlan_11h_check_chan_report(mlan_private *priv, t_u8 chan);

/** Add any 11h TLVs necessary to complete an adhoc start command */
extern t_s32 wlan_11h_process_start(mlan_private *priv,
				    t_u8 **ppbuffer,
				    IEEEtypes_CapInfo_t *pcap_info,
				    t_u32 channel,
				    wlan_11h_bss_info_t *p11h_bss_info);

/** Add any 11h TLVs necessary to complete a join command (adhoc or infra) */
extern t_s32 wlan_11h_process_join(mlan_private *priv,
				   t_u8 **ppbuffer,
				   IEEEtypes_CapInfo_t *pcap_info,
				   t_u8 band,
				   t_u32 channel,
				   wlan_11h_bss_info_t *p11h_bss_info);

/** Complete the firmware command preparation for an 11h command function */
extern mlan_status wlan_11h_cmd_process(mlan_private *priv,
					HostCmd_DS_COMMAND *pcmd_ptr,
					const t_void *pinfo_buf);

/** Process the response of an 11h firmware command */
extern mlan_status wlan_11h_cmdresp_process(mlan_private *priv,
					    const HostCmd_DS_COMMAND *resp);

/** Receive IEs from scan processing and record any needed info for 11h */
extern mlan_status wlan_11h_process_bss_elem(mlan_adapter *pmadapter,
					     wlan_11h_bss_info_t *p11h_bss_info,
					     const t_u8 *pelement);

/** Handler for EVENT_CHANNEL_SWITCH_ANN */
extern mlan_status wlan_11h_handle_event_chanswann(mlan_private *priv);

/** Handler for EVENT_CHANNEL_REPORT_RDY */
extern mlan_status wlan_11h_handle_event_chanrpt_ready(mlan_private *priv,
						       mlan_event *pevent);

#ifdef DFS_TESTING_SUPPORT
/** Handler for DFS_TESTING IOCTL */
extern mlan_status wlan_11h_ioctl_dfs_testing(pmlan_adapter pmadapter,
					      pmlan_ioctl_req pioctl_req);
extern mlan_status wlan_11h_ioctl_get_channel_nop_info(pmlan_adapter pmadapter,
						       pmlan_ioctl_req
						       pioctl_req);
#endif

extern mlan_status

wlan_11h_ioctl_dfs_cancel_chan_report(mlan_private *priv,
				      pmlan_ioctl_req pioctl_req);
extern
mlan_status wlan_11h_ioctl_chan_switch_count(pmlan_adapter pmadapter,
					     pmlan_ioctl_req pioctl_req);

/** Check if channel is under a NOP duration (should not be used) */
extern t_bool wlan_11h_is_channel_under_nop(mlan_adapter *pmadapter,
					    t_u8 channel);

/** Check if RADAR_DETECTED handling is blocking data tx */
extern t_bool wlan_11h_radar_detected_tx_blocked(mlan_adapter *pmadapter);

/** Callback for RADAR_DETECTED (for UAP cmdresp) */
extern mlan_status wlan_11h_radar_detected_callback(t_void *priv);

/** BW_change event Handler for dfs_repeater */
void wlan_dfs_rep_bw_change(mlan_adapter *pmadapter);

/** disconnect event Handler for dfs_repeater */
void wlan_dfs_rep_disconnect(mlan_adapter *pmadapter);

/** Handler for RADAR_DETECTED */
extern mlan_status wlan_11h_radar_detected_handling(mlan_adapter *pmadapter,
						    mlan_private *priv);
/** DFS Event pre-processing */
extern mlan_status wlan_11h_dfs_event_preprocessing(mlan_adapter *pmadapter);

/** DFS switch to non-DFS channel */
extern mlan_status wlan_11h_switch_non_dfs_chan(mlan_private *priv, t_u8 *chan);

extern void wlan_11h_update_bandcfg(IN Band_Config_t *uap_band_cfg,
				    IN t_u8 new_channel);

/** function checks if interface is active. **/
extern t_bool wlan_is_intf_active(mlan_private *pmpriv);

#endif /*_MLAN_11H_ */
