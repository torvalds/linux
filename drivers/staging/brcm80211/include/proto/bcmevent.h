/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _BCMEVENT_H_
#define _BCMEVENT_H_

#include <typedefs.h>
#include <packed_section_start.h>

#define BCM_EVENT_MSG_VERSION		1
#define BCM_MSG_IFNAME_MAX		16

#define WLC_EVENT_MSG_LINK		0x01
#define WLC_EVENT_MSG_FLUSHTXQ		0x02
#define WLC_EVENT_MSG_GROUP		0x04

typedef BWL_PRE_PACKED_STRUCT struct {
	uint16 version;
	uint16 flags;
	uint32 event_type;
	uint32 status;
	uint32 reason;
	uint32 auth_type;
	uint32 datalen;
	struct ether_addr addr;
	char ifname[BCM_MSG_IFNAME_MAX];
} BWL_POST_PACKED_STRUCT wl_event_msg_t;

#ifdef BRCM_FULLMAC
typedef BWL_PRE_PACKED_STRUCT struct bcm_event {
	struct ether_header eth;
	bcmeth_hdr_t		bcm_hdr;
	wl_event_msg_t		event;
} BWL_POST_PACKED_STRUCT bcm_event_t;
#endif
#define BCM_MSG_LEN	(sizeof(bcm_event_t) - sizeof(bcmeth_hdr_t) - \
	sizeof(struct ether_header))

#define WLC_E_SET_SSID		0
#define WLC_E_JOIN		1
#define WLC_E_START		2
#define WLC_E_AUTH		3
#define WLC_E_AUTH_IND		4
#define WLC_E_DEAUTH		5
#define WLC_E_DEAUTH_IND	6
#define WLC_E_ASSOC		7
#define WLC_E_ASSOC_IND		8
#define WLC_E_REASSOC		9
#define WLC_E_REASSOC_IND	10
#define WLC_E_DISASSOC		11
#define WLC_E_DISASSOC_IND	12
#define WLC_E_QUIET_START	13
#define WLC_E_QUIET_END		14
#define WLC_E_BEACON_RX		15
#define WLC_E_LINK		16
#define WLC_E_MIC_ERROR		17
#define WLC_E_NDIS_LINK		18
#define WLC_E_ROAM		19
#define WLC_E_TXFAIL		20
#define WLC_E_PMKID_CACHE	21
#define WLC_E_RETROGRADE_TSF	22
#define WLC_E_PRUNE		23
#define WLC_E_AUTOAUTH		24
#define WLC_E_EAPOL_MSG		25
#define WLC_E_SCAN_COMPLETE	26
#define WLC_E_ADDTS_IND		27
#define WLC_E_DELTS_IND		28
#define WLC_E_BCNSENT_IND	29
#define WLC_E_BCNRX_MSG		30
#define WLC_E_BCNLOST_MSG	31
#define WLC_E_ROAM_PREP		32
#define WLC_E_PFN_NET_FOUND	33
#define WLC_E_PFN_NET_LOST	34
#define WLC_E_RESET_COMPLETE	35
#define WLC_E_JOIN_START	36
#define WLC_E_ROAM_START	37
#define WLC_E_ASSOC_START	38
#define WLC_E_IBSS_ASSOC	39
#define WLC_E_RADIO		40
#define WLC_E_PSM_WATCHDOG	41
#define WLC_E_PROBREQ_MSG       44
#define WLC_E_SCAN_CONFIRM_IND  45
#define WLC_E_PSK_SUP		46
#define WLC_E_COUNTRY_CODE_CHANGED 47
#define	WLC_E_EXCEEDED_MEDIUM_TIME 48
#define WLC_E_ICV_ERROR		49
#define WLC_E_UNICAST_DECODE_ERROR 50
#define WLC_E_MULTICAST_DECODE_ERROR 51
#define WLC_E_TRACE		52
#define WLC_E_IF		54
#define WLC_E_RSSI		56
#define WLC_E_PFN_SCAN_COMPLETE	57
#define WLC_E_EXTLOG_MSG	58
#define WLC_E_ACTION_FRAME      59
#define WLC_E_ACTION_FRAME_COMPLETE 60
#define WLC_E_PRE_ASSOC_IND	61
#define WLC_E_PRE_REASSOC_IND	62
#define WLC_E_CHANNEL_ADOPTED	63
#define WLC_E_AP_STARTED	64
#define WLC_E_DFS_AP_STOP	65
#define WLC_E_DFS_AP_RESUME	66
#define WLC_E_RESERVED1		67
#define WLC_E_RESERVED2		68
#define WLC_E_ESCAN_RESULT 	69
#define WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE 	70
#define WLC_E_DCS_REQUEST 73

#define WLC_E_FIFO_CREDIT_MAP	74

#define WLC_E_LAST		75

typedef struct {
	uint event;
	const char *name;
} bcmevent_name_t;

extern const bcmevent_name_t bcmevent_names[];
extern const int bcmevent_names_size;

#define WLC_E_STATUS_SUCCESS		0
#define WLC_E_STATUS_FAIL		1
#define WLC_E_STATUS_TIMEOUT		2
#define WLC_E_STATUS_NO_NETWORKS	3
#define WLC_E_STATUS_ABORT		4
#define WLC_E_STATUS_NO_ACK		5
#define WLC_E_STATUS_UNSOLICITED	6
#define WLC_E_STATUS_ATTEMPT		7
#define WLC_E_STATUS_PARTIAL		8
#define WLC_E_STATUS_NEWSCAN		9
#define WLC_E_STATUS_NEWASSOC		10
#define WLC_E_STATUS_11HQUIET		11
#define WLC_E_STATUS_SUPPRESS		12
#define WLC_E_STATUS_NOCHANS		13
#define WLC_E_STATUS_CS_ABORT		15
#define WLC_E_STATUS_ERROR		16

#define WLC_E_REASON_INITIAL_ASSOC	0
#define WLC_E_REASON_LOW_RSSI		1
#define WLC_E_REASON_DEAUTH		2
#define WLC_E_REASON_DISASSOC		3
#define WLC_E_REASON_BCNS_LOST		4
#define WLC_E_REASON_MINTXRATE		9
#define WLC_E_REASON_TXFAIL		10

#define WLC_E_REASON_FAST_ROAM_FAILED	5
#define WLC_E_REASON_DIRECTED_ROAM	6
#define WLC_E_REASON_TSPEC_REJECTED	7
#define WLC_E_REASON_BETTER_AP		8

#define WLC_E_PRUNE_ENCR_MISMATCH	1
#define WLC_E_PRUNE_BCAST_BSSID		2
#define WLC_E_PRUNE_MAC_DENY		3
#define WLC_E_PRUNE_MAC_NA		4
#define WLC_E_PRUNE_REG_PASSV		5
#define WLC_E_PRUNE_SPCT_MGMT		6
#define WLC_E_PRUNE_RADAR		7
#define WLC_E_RSN_MISMATCH		8
#define WLC_E_PRUNE_NO_COMMON_RATES	9
#define WLC_E_PRUNE_BASIC_RATES		10
#define WLC_E_PRUNE_CIPHER_NA		12
#define WLC_E_PRUNE_KNOWN_STA		13
#define WLC_E_PRUNE_WDS_PEER		15
#define WLC_E_PRUNE_QBSS_LOAD		16
#define WLC_E_PRUNE_HOME_AP		17

#define WLC_E_SUP_OTHER			0
#define WLC_E_SUP_DECRYPT_KEY_DATA	1
#define WLC_E_SUP_BAD_UCAST_WEP128	2
#define WLC_E_SUP_BAD_UCAST_WEP40	3
#define WLC_E_SUP_UNSUP_KEY_LEN		4
#define WLC_E_SUP_PW_KEY_CIPHER		5
#define WLC_E_SUP_MSG3_TOO_MANY_IE	6
#define WLC_E_SUP_MSG3_IE_MISMATCH	7
#define WLC_E_SUP_NO_INSTALL_FLAG	8
#define WLC_E_SUP_MSG3_NO_GTK		9
#define WLC_E_SUP_GRP_KEY_CIPHER	10
#define WLC_E_SUP_GRP_MSG1_NO_GTK	11
#define WLC_E_SUP_GTK_DECRYPT_FAIL	12
#define WLC_E_SUP_SEND_FAIL		13
#define WLC_E_SUP_DEAUTH		14

typedef struct wl_event_data_if {
	uint8 ifidx;
	uint8 opcode;
	uint8 reserved;
	uint8 bssidx;
	uint8 role;
} wl_event_data_if_t;

#define WLC_E_IF_ADD		1
#define WLC_E_IF_DEL		2
#define WLC_E_IF_CHANGE		3

#define WLC_E_IF_ROLE_STA		0
#define WLC_E_IF_ROLE_AP		1
#define WLC_E_IF_ROLE_WDS		2

#define WLC_E_LINK_BCN_LOSS	1
#define WLC_E_LINK_DISASSOC	2
#define WLC_E_LINK_ASSOC_REC	3
#define WLC_E_LINK_BSSCFG_DIS	4

#include <packed_section_end.h>

#endif				/* _BCMEVENT_H_ */
