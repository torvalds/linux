/* This file is GENERATED AUTOMATICALLY.  DO NOT EDIT OR MODIFY.
* --------------------------------------------------------------------
*
* Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
* --------------------------------------------------------------------
*
* linux-wlan
*
*   The contents of this file are subject to the Mozilla Public
*   License Version 1.1 (the "License"); you may not use this file
*   except in compliance with the License. You may obtain a copy of
*   the License at http://www.mozilla.org/MPL/
*
*   Software distributed under the License is distributed on an "AS
*   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
*   implied. See the License for the specific language governing
*   rights and limitations under the License.
*
*   Alternatively, the contents of this file may be used under the
*   terms of the GNU Public License version 2 (the "GPL"), in which
*   case the provisions of the GPL are applicable instead of the
*   above.  If you wish to allow the use of your version of this file
*   only under the terms of the GPL and not to allow others to use
*   your version of this file under the MPL, indicate your decision
*   by deleting the provisions above and replace them with the notice
*   and other provisions required by the GPL.  If you do not delete
*   the provisions above, a recipient may use your version of this
*   file under either the MPL or the GPL.
*
* --------------------------------------------------------------------
*
* Inquiries regarding the linux-wlan Open Source project can be
* made directly to:
*
* AbsoluteValue Systems Inc.
* info@linux-wlan.com
* http://www.linux-wlan.com
*
* --------------------------------------------------------------------
*
* Portions of the development of this software were funded by
* Intersil Corporation as part of PRISM(R) chipset product development.
*
* --------------------------------------------------------------------
*/

#ifndef _P80211MKMETASTRUCT_H
#define _P80211MKMETASTRUCT_H

struct p80211msg_dot11req_mibget {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_unk392_t mibattribute;
	struct p80211item_uint32 resultcode;
} __packed;

struct p80211msg_dot11req_mibset {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_unk392_t mibattribute;
	struct p80211item_uint32 resultcode;
} __packed;

struct p80211msg_dot11req_scan {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	struct p80211item_uint32 bsstype;
	struct p80211item_pstr6 bssid;
	u8 pad_0C[1];
	struct p80211item_pstr32 ssid;
	u8 pad_1D[3];
	struct p80211item_uint32 scantype;
	struct p80211item_uint32 probedelay;
	struct p80211item_pstr14 channellist;
	u8 pad_2C[1];
	struct p80211item_uint32 minchanneltime;
	struct p80211item_uint32 maxchanneltime;
	struct p80211item_uint32 resultcode;
	struct p80211item_uint32 numbss;
	struct p80211item_uint32 append;
} __packed;

struct p80211msg_dot11req_scan_results {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	struct p80211item_uint32 bssindex;
	struct p80211item_uint32 resultcode;
	struct p80211item_uint32 signal;
	struct p80211item_uint32 noise;
	struct p80211item_pstr6 bssid;
	u8 pad_3C[1];
	struct p80211item_pstr32 ssid;
	u8 pad_4D[3];
	struct p80211item_uint32 bsstype;
	struct p80211item_uint32 beaconperiod;
	struct p80211item_uint32 dtimperiod;
	struct p80211item_uint32 timestamp;
	struct p80211item_uint32 localtime;
	struct p80211item_uint32 fhdwelltime;
	struct p80211item_uint32 fhhopset;
	struct p80211item_uint32 fhhoppattern;
	struct p80211item_uint32 fhhopindex;
	struct p80211item_uint32 dschannel;
	struct p80211item_uint32 cfpcount;
	struct p80211item_uint32 cfpperiod;
	struct p80211item_uint32 cfpmaxduration;
	struct p80211item_uint32 cfpdurremaining;
	struct p80211item_uint32 ibssatimwindow;
	struct p80211item_uint32 cfpollable;
	struct p80211item_uint32 cfpollreq;
	struct p80211item_uint32 privacy;
	struct p80211item_uint32 capinfo;
	struct p80211item_uint32 basicrate1;
	struct p80211item_uint32 basicrate2;
	struct p80211item_uint32 basicrate3;
	struct p80211item_uint32 basicrate4;
	struct p80211item_uint32 basicrate5;
	struct p80211item_uint32 basicrate6;
	struct p80211item_uint32 basicrate7;
	struct p80211item_uint32 basicrate8;
	struct p80211item_uint32 supprate1;
	struct p80211item_uint32 supprate2;
	struct p80211item_uint32 supprate3;
	struct p80211item_uint32 supprate4;
	struct p80211item_uint32 supprate5;
	struct p80211item_uint32 supprate6;
	struct p80211item_uint32 supprate7;
	struct p80211item_uint32 supprate8;
} __packed;

struct p80211msg_dot11req_start {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	struct p80211item_pstr32 ssid;
	u8 pad_12D[3];
	struct p80211item_uint32 bsstype;
	struct p80211item_uint32 beaconperiod;
	struct p80211item_uint32 dtimperiod;
	struct p80211item_uint32 cfpperiod;
	struct p80211item_uint32 cfpmaxduration;
	struct p80211item_uint32 fhdwelltime;
	struct p80211item_uint32 fhhopset;
	struct p80211item_uint32 fhhoppattern;
	struct p80211item_uint32 dschannel;
	struct p80211item_uint32 ibssatimwindow;
	struct p80211item_uint32 probedelay;
	struct p80211item_uint32 cfpollable;
	struct p80211item_uint32 cfpollreq;
	struct p80211item_uint32 basicrate1;
	struct p80211item_uint32 basicrate2;
	struct p80211item_uint32 basicrate3;
	struct p80211item_uint32 basicrate4;
	struct p80211item_uint32 basicrate5;
	struct p80211item_uint32 basicrate6;
	struct p80211item_uint32 basicrate7;
	struct p80211item_uint32 basicrate8;
	struct p80211item_uint32 operationalrate1;
	struct p80211item_uint32 operationalrate2;
	struct p80211item_uint32 operationalrate3;
	struct p80211item_uint32 operationalrate4;
	struct p80211item_uint32 operationalrate5;
	struct p80211item_uint32 operationalrate6;
	struct p80211item_uint32 operationalrate7;
	struct p80211item_uint32 operationalrate8;
	struct p80211item_uint32 resultcode;
} __packed;

struct p80211msg_lnxreq_ifstate {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	struct p80211item_uint32 ifstate;
	struct p80211item_uint32 resultcode;
} __packed;

struct p80211msg_lnxreq_wlansniff {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	struct p80211item_uint32 enable;
	struct p80211item_uint32 channel;
	struct p80211item_uint32 prismheader;
	struct p80211item_uint32 wlanheader;
	struct p80211item_uint32 keepwepflags;
	struct p80211item_uint32 stripfcs;
	struct p80211item_uint32 packet_trunc;
	struct p80211item_uint32 resultcode;
} __packed;

struct p80211msg_lnxreq_hostwep {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	struct p80211item_uint32 resultcode;
	struct p80211item_uint32 decrypt;
	struct p80211item_uint32 encrypt;
} __packed;

struct p80211msg_lnxreq_commsquality {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	struct p80211item_uint32 resultcode;
	struct p80211item_uint32 dbm;
	struct p80211item_uint32 link;
	struct p80211item_uint32 level;
	struct p80211item_uint32 noise;
	struct p80211item_uint32 txrate;
} __packed;

struct p80211msg_lnxreq_autojoin {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	struct p80211item_pstr32 ssid;
	u8 pad_19D[3];
	struct p80211item_uint32 authtype;
	struct p80211item_uint32 resultcode;
} __packed;

struct p80211msg_p2req_readpda {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_unk1024_t pda;
	struct p80211item_uint32 resultcode;
} __packed;

struct p80211msg_p2req_ramdl_state {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	struct p80211item_uint32 enable;
	struct p80211item_uint32 exeaddr;
	struct p80211item_uint32 resultcode;
} __packed;

struct p80211msg_p2req_ramdl_write {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	struct p80211item_uint32 addr;
	struct p80211item_uint32 len;
	p80211item_unk4096_t data;
	struct p80211item_uint32 resultcode;
} __packed;

struct p80211msg_p2req_flashdl_state {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	struct p80211item_uint32 enable;
	struct p80211item_uint32 resultcode;
} __packed;

struct p80211msg_p2req_flashdl_write {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	struct p80211item_uint32 addr;
	struct p80211item_uint32 len;
	p80211item_unk4096_t data;
	struct p80211item_uint32 resultcode;
} __packed;

#endif
