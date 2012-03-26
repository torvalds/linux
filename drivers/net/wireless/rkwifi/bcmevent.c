/*
 * bcmevent read-only data shared by kernel or app layers
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 * $Id: bcmevent.c 275693 2011-08-04 19:59:34Z $
 */

#include <typedefs.h>
#include <bcmutils.h>
#include <proto/ethernet.h>
#include <proto/bcmeth.h>
#include <proto/bcmevent.h>

#if WLC_E_LAST != 85
#error "You need to add an entry to bcmevent_names[] for the new event"
#endif

const bcmevent_name_t bcmevent_names[] = {
	{ WLC_E_SET_SSID, "SET_SSID" },
	{ WLC_E_JOIN, "JOIN" },
	{ WLC_E_START, "START" },
	{ WLC_E_AUTH, "AUTH" },
	{ WLC_E_AUTH_IND, "AUTH_IND" },
	{ WLC_E_DEAUTH, "DEAUTH" },
	{ WLC_E_DEAUTH_IND, "DEAUTH_IND" },
	{ WLC_E_ASSOC, "ASSOC" },
	{ WLC_E_ASSOC_IND, "ASSOC_IND" },
	{ WLC_E_REASSOC, "REASSOC" },
	{ WLC_E_REASSOC_IND, "REASSOC_IND" },
	{ WLC_E_DISASSOC, "DISASSOC" },
	{ WLC_E_DISASSOC_IND, "DISASSOC_IND" },
	{ WLC_E_QUIET_START, "START_QUIET" },
	{ WLC_E_QUIET_END, "END_QUIET" },
	{ WLC_E_BEACON_RX, "BEACON_RX" },
	{ WLC_E_LINK, "LINK" },
	{ WLC_E_MIC_ERROR, "MIC_ERROR" },
	{ WLC_E_NDIS_LINK, "NDIS_LINK" },
	{ WLC_E_ROAM, "ROAM" },
	{ WLC_E_TXFAIL, "TXFAIL" },
	{ WLC_E_PMKID_CACHE, "PMKID_CACHE" },
	{ WLC_E_RETROGRADE_TSF, "RETROGRADE_TSF" },
	{ WLC_E_PRUNE, "PRUNE" },
	{ WLC_E_AUTOAUTH, "AUTOAUTH" },
	{ WLC_E_EAPOL_MSG, "EAPOL_MSG" },
	{ WLC_E_SCAN_COMPLETE, "SCAN_COMPLETE" },
	{ WLC_E_ADDTS_IND, "ADDTS_IND" },
	{ WLC_E_DELTS_IND, "DELTS_IND" },
	{ WLC_E_BCNSENT_IND, "BCNSENT_IND" },
	{ WLC_E_BCNRX_MSG, "BCNRX_MSG" },
	{ WLC_E_BCNLOST_MSG, "BCNLOST_IND" },
	{ WLC_E_ROAM_PREP, "ROAM_PREP" },
	{ WLC_E_PFN_NET_FOUND, "PFNFOUND_IND" },
	{ WLC_E_PFN_NET_LOST, "PFNLOST_IND" },
#if defined(IBSS_PEER_DISCOVERY_EVENT)
	{ WLC_E_IBSS_ASSOC, "IBSS_ASSOC" },
#endif /* defined(IBSS_PEER_DISCOVERY_EVENT) */
	{ WLC_E_RADIO, "RADIO" },
	{ WLC_E_PSM_WATCHDOG, "PSM_WATCHDOG" },
	{ WLC_E_PROBREQ_MSG, "PROBE_REQ_MSG" },
	{ WLC_E_SCAN_CONFIRM_IND, "SCAN_CONFIRM_IND" },
	{ WLC_E_PSK_SUP, "PSK_SUP" },
	{ WLC_E_COUNTRY_CODE_CHANGED, "CNTRYCODE_IND" },
	{ WLC_E_EXCEEDED_MEDIUM_TIME, "EXCEEDED_MEDIUM_TIME" },
	{ WLC_E_ICV_ERROR, "ICV_ERROR" },
	{ WLC_E_UNICAST_DECODE_ERROR, "UNICAST_DECODE_ERROR" },
	{ WLC_E_MULTICAST_DECODE_ERROR, "MULTICAST_DECODE_ERROR" },
	{ WLC_E_TRACE, "TRACE" },
	{ WLC_E_BTA_HCI_EVENT, "BTA_HCI_EVENT" },
	{ WLC_E_IF, "IF" },
#ifdef WLP2P
	{ WLC_E_P2P_DISC_LISTEN_COMPLETE, "WLC_E_P2P_DISC_LISTEN_COMPLETE" },
#endif
	{ WLC_E_RSSI, "RSSI" },
	{ WLC_E_PFN_SCAN_COMPLETE, "SCAN_COMPLETE" },
	{ WLC_E_EXTLOG_MSG, "EXTERNAL LOG MESSAGE" },
#ifdef WIFI_ACT_FRAME
	{ WLC_E_ACTION_FRAME, "ACTION_FRAME" },
	{ WLC_E_ACTION_FRAME_RX, "ACTION_FRAME_RX" },
	{ WLC_E_ACTION_FRAME_COMPLETE, "ACTION_FRAME_COMPLETE" },
#endif
	{ WLC_E_ESCAN_RESULT, "WLC_E_ESCAN_RESULT" },
	{ WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE, "WLC_E_AF_OFF_CHAN_COMPLETE" },
#ifdef WLP2P
	{ WLC_E_PROBRESP_MSG, "PROBE_RESP_MSG" },
	{ WLC_E_P2P_PROBREQ_MSG, "P2P PROBE_REQ_MSG" },
#endif
#ifdef PROP_TXSTATUS
	{ WLC_E_FIFO_CREDIT_MAP, "FIFO_CREDIT_MAP" },
#endif
	{ WLC_E_WAKE_EVENT, "WAKE_EVENT" },
	{ WLC_E_DCS_REQUEST, "DCS_REQUEST" },
	{ WLC_E_RM_COMPLETE, "RM_COMPLETE" },
#ifdef WLMEDIA_HTSF
	{ WLC_E_HTSFSYNC, "HTSF_SYNC_EVENT" },
#endif
	{ WLC_E_OVERLAY_REQ, "OVERLAY_REQ_EVENT" },
	{ WLC_E_CSA_COMPLETE_IND, "WLC_E_CSA_COMPLETE_IND" },
	{ WLC_E_EXCESS_PM_WAKE_EVENT, "EXCESS_PM_WAKE_EVENT" },
	{ WLC_E_PFN_SCAN_NONE, "PFN_SCAN_NONE" },
	{ WLC_E_PFN_SCAN_ALLGONE, "PFN_SCAN_ALLGONE" },
#ifdef SOFTAP
	{ WLC_E_GTK_PLUMBED, "GTK_PLUMBED" }
#endif
};


const int bcmevent_names_size = ARRAYSIZE(bcmevent_names);
