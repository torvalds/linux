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
	p80211item_uint32_t resultcode;
} __attribute__ ((packed));

struct p80211msg_dot11req_mibset {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_unk392_t mibattribute;
	p80211item_uint32_t resultcode;
} __attribute__ ((packed));

struct p80211msg_dot11req_scan {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_uint32_t bsstype;
	p80211item_pstr6_t bssid;
	u8 pad_0C[1];
	p80211item_pstr32_t ssid;
	u8 pad_1D[3];
	p80211item_uint32_t scantype;
	p80211item_uint32_t probedelay;
	p80211item_pstr14_t channellist;
	u8 pad_2C[1];
	p80211item_uint32_t minchanneltime;
	p80211item_uint32_t maxchanneltime;
	p80211item_uint32_t resultcode;
	p80211item_uint32_t numbss;
	p80211item_uint32_t append;
} __attribute__ ((packed));

struct p80211msg_dot11req_scan_results {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_uint32_t bssindex;
	p80211item_uint32_t resultcode;
	p80211item_uint32_t signal;
	p80211item_uint32_t noise;
	p80211item_pstr6_t bssid;
	u8 pad_3C[1];
	p80211item_pstr32_t ssid;
	u8 pad_4D[3];
	p80211item_uint32_t bsstype;
	p80211item_uint32_t beaconperiod;
	p80211item_uint32_t dtimperiod;
	p80211item_uint32_t timestamp;
	p80211item_uint32_t localtime;
	p80211item_uint32_t fhdwelltime;
	p80211item_uint32_t fhhopset;
	p80211item_uint32_t fhhoppattern;
	p80211item_uint32_t fhhopindex;
	p80211item_uint32_t dschannel;
	p80211item_uint32_t cfpcount;
	p80211item_uint32_t cfpperiod;
	p80211item_uint32_t cfpmaxduration;
	p80211item_uint32_t cfpdurremaining;
	p80211item_uint32_t ibssatimwindow;
	p80211item_uint32_t cfpollable;
	p80211item_uint32_t cfpollreq;
	p80211item_uint32_t privacy;
	p80211item_uint32_t capinfo;
	p80211item_uint32_t basicrate1;
	p80211item_uint32_t basicrate2;
	p80211item_uint32_t basicrate3;
	p80211item_uint32_t basicrate4;
	p80211item_uint32_t basicrate5;
	p80211item_uint32_t basicrate6;
	p80211item_uint32_t basicrate7;
	p80211item_uint32_t basicrate8;
	p80211item_uint32_t supprate1;
	p80211item_uint32_t supprate2;
	p80211item_uint32_t supprate3;
	p80211item_uint32_t supprate4;
	p80211item_uint32_t supprate5;
	p80211item_uint32_t supprate6;
	p80211item_uint32_t supprate7;
	p80211item_uint32_t supprate8;
} __attribute__ ((packed));

struct p80211msg_dot11req_start {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_pstr32_t ssid;
	u8 pad_12D[3];
	p80211item_uint32_t bsstype;
	p80211item_uint32_t beaconperiod;
	p80211item_uint32_t dtimperiod;
	p80211item_uint32_t cfpperiod;
	p80211item_uint32_t cfpmaxduration;
	p80211item_uint32_t fhdwelltime;
	p80211item_uint32_t fhhopset;
	p80211item_uint32_t fhhoppattern;
	p80211item_uint32_t dschannel;
	p80211item_uint32_t ibssatimwindow;
	p80211item_uint32_t probedelay;
	p80211item_uint32_t cfpollable;
	p80211item_uint32_t cfpollreq;
	p80211item_uint32_t basicrate1;
	p80211item_uint32_t basicrate2;
	p80211item_uint32_t basicrate3;
	p80211item_uint32_t basicrate4;
	p80211item_uint32_t basicrate5;
	p80211item_uint32_t basicrate6;
	p80211item_uint32_t basicrate7;
	p80211item_uint32_t basicrate8;
	p80211item_uint32_t operationalrate1;
	p80211item_uint32_t operationalrate2;
	p80211item_uint32_t operationalrate3;
	p80211item_uint32_t operationalrate4;
	p80211item_uint32_t operationalrate5;
	p80211item_uint32_t operationalrate6;
	p80211item_uint32_t operationalrate7;
	p80211item_uint32_t operationalrate8;
	p80211item_uint32_t resultcode;
} __attribute__ ((packed));

struct p80211msg_lnxreq_ifstate {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_uint32_t ifstate;
	p80211item_uint32_t resultcode;
} __attribute__ ((packed));

struct p80211msg_lnxreq_wlansniff {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_uint32_t enable;
	p80211item_uint32_t channel;
	p80211item_uint32_t prismheader;
	p80211item_uint32_t wlanheader;
	p80211item_uint32_t keepwepflags;
	p80211item_uint32_t stripfcs;
	p80211item_uint32_t packet_trunc;
	p80211item_uint32_t resultcode;
} __attribute__ ((packed));

struct p80211msg_lnxreq_hostwep {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_uint32_t resultcode;
	p80211item_uint32_t decrypt;
	p80211item_uint32_t encrypt;
} __attribute__ ((packed));

struct p80211msg_lnxreq_commsquality {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_uint32_t resultcode;
	p80211item_uint32_t dbm;
	p80211item_uint32_t link;
	p80211item_uint32_t level;
	p80211item_uint32_t noise;
	p80211item_uint32_t txrate;
} __attribute__ ((packed));

struct p80211msg_lnxreq_autojoin {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_pstr32_t ssid;
	u8 pad_19D[3];
	p80211item_uint32_t authtype;
	p80211item_uint32_t resultcode;
} __attribute__ ((packed));

struct p80211msg_p2req_readpda {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_unk1024_t pda;
	p80211item_uint32_t resultcode;
} __attribute__ ((packed));

struct p80211msg_p2req_ramdl_state {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_uint32_t enable;
	p80211item_uint32_t exeaddr;
	p80211item_uint32_t resultcode;
} __attribute__ ((packed));

struct p80211msg_p2req_ramdl_write {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_uint32_t addr;
	p80211item_uint32_t len;
	p80211item_unk4096_t data;
	p80211item_uint32_t resultcode;
} __attribute__ ((packed));

struct p80211msg_p2req_flashdl_state {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_uint32_t enable;
	p80211item_uint32_t resultcode;
} __attribute__ ((packed));

struct p80211msg_p2req_flashdl_write {
	u32 msgcode;
	u32 msglen;
	u8 devname[WLAN_DEVNAMELEN_MAX];
	p80211item_uint32_t addr;
	p80211item_uint32_t len;
	p80211item_unk4096_t data;
	p80211item_uint32_t resultcode;
} __attribute__ ((packed));

#endif
