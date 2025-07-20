// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2012 Broadcom Corporation
 */

#ifndef CYW_FWIL_TYPES_H_
#define CYW_FWIL_TYPES_H_

#include <fwil_types.h>

enum brcmf_event_msgs_ext_command {
	CYW_EVENTMSGS_NONE	= 0,
	CYW_EVENTMSGS_SET_BIT	= 1,
	CYW_EVENTMSGS_RESET_BIT	= 2,
	CYW_EVENTMSGS_SET_MASK	= 3,
};

#define EVENTMSGS_VER 1
#define EVENTMSGS_EXT_STRUCT_SIZE	offsetof(struct eventmsgs_ext, mask[0])

/**
 * struct brcmf_eventmsgs_ext - structure used with "eventmsgs_ext" iovar.
 *
 * @ver: version.
 * @command: requested operation (see &enum event_msgs_ext_command).
 * @len: length of the @mask array.
 * @maxgetsize: indicates maximum mask size that may be returned by firmware
 *	upon iovar GET.
 * @mask: array where each bit represents firmware event.
 */
struct brcmf_eventmsgs_ext {
	u8	ver;
	u8	command;
	u8	len;
	u8	maxgetsize;
	u8	mask[] __counted_by(len);
};

#define BRCMF_EXTAUTH_START		1
#define BRCMF_EXTAUTH_ABORT		2
#define BRCMF_EXTAUTH_FAIL		3
#define BRCMF_EXTAUTH_SUCCESS		4

/**
 * struct brcmf_auth_req_status_le - external auth request and status update
 *
 * @flags: flags for external auth status
 * @peer_mac: peer MAC address
 * @ssid_len: length of ssid
 * @ssid: ssid characters
 * @pmkid: PMKSA identifier
 */
struct brcmf_auth_req_status_le {
	__le16 flags;
	u8 peer_mac[ETH_ALEN];
	__le32 ssid_len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 pmkid[WLAN_PMKID_LEN];
};

/**
 * struct brcmf_mf_params_le - management frame parameters for mgmt_frame iovar
 *
 * @version: version of the iovar
 * @dwell_time: dwell duration in ms
 * @len: length of frame data
 * @frame_control: frame control
 * @channel: channel
 * @da: peer MAC address
 * @bssid: BSS network identifier
 * @packet_id: packet identifier
 * @data: frame data
 */
struct brcmf_mf_params_le {
	__le32 version;
	__le32 dwell_time;
	__le16 len;
	__le16 frame_control;
	__le16 channel;
	u8 da[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	__le32 packet_id;
	u8 data[] __counted_by(len);
};

#endif /* CYW_FWIL_TYPES_H_ */

