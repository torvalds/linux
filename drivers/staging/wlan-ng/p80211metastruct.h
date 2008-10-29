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


typedef struct p80211msg_dot11req_mibget
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_unk392_t	mibattribute	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_dot11req_mibget_t;

typedef struct p80211msg_dot11req_mibset
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_unk392_t	mibattribute	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_dot11req_mibset_t;

typedef struct p80211msg_dot11req_scan
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	bsstype	;
	p80211item_pstr6_t	bssid	;
	u8	pad_0C[1]	;
	p80211item_pstr32_t	ssid	;
	u8	pad_1D[3]	;
	p80211item_uint32_t	scantype	;
	p80211item_uint32_t	probedelay	;
	p80211item_pstr14_t	channellist	;
	u8	pad_2C[1]	;
	p80211item_uint32_t	minchanneltime	;
	p80211item_uint32_t	maxchanneltime	;
	p80211item_uint32_t	resultcode	;
	p80211item_uint32_t	numbss	;
	p80211item_uint32_t	append	;
} __WLAN_ATTRIB_PACK__ p80211msg_dot11req_scan_t;

typedef struct p80211msg_dot11req_scan_results
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	bssindex	;
	p80211item_uint32_t	resultcode	;
	p80211item_uint32_t	signal	;
	p80211item_uint32_t	noise	;
	p80211item_pstr6_t	bssid	;
	u8	pad_3C[1]	;
	p80211item_pstr32_t	ssid	;
	u8	pad_4D[3]	;
	p80211item_uint32_t	bsstype	;
	p80211item_uint32_t	beaconperiod	;
	p80211item_uint32_t	dtimperiod	;
	p80211item_uint32_t	timestamp	;
	p80211item_uint32_t	localtime	;
	p80211item_uint32_t	fhdwelltime	;
	p80211item_uint32_t	fhhopset	;
	p80211item_uint32_t	fhhoppattern	;
	p80211item_uint32_t	fhhopindex	;
	p80211item_uint32_t	dschannel	;
	p80211item_uint32_t	cfpcount	;
	p80211item_uint32_t	cfpperiod	;
	p80211item_uint32_t	cfpmaxduration	;
	p80211item_uint32_t	cfpdurremaining	;
	p80211item_uint32_t	ibssatimwindow	;
	p80211item_uint32_t	cfpollable	;
	p80211item_uint32_t	cfpollreq	;
	p80211item_uint32_t	privacy	;
	p80211item_uint32_t	basicrate1	;
	p80211item_uint32_t	basicrate2	;
	p80211item_uint32_t	basicrate3	;
	p80211item_uint32_t	basicrate4	;
	p80211item_uint32_t	basicrate5	;
	p80211item_uint32_t	basicrate6	;
	p80211item_uint32_t	basicrate7	;
	p80211item_uint32_t	basicrate8	;
	p80211item_uint32_t	supprate1	;
	p80211item_uint32_t	supprate2	;
	p80211item_uint32_t	supprate3	;
	p80211item_uint32_t	supprate4	;
	p80211item_uint32_t	supprate5	;
	p80211item_uint32_t	supprate6	;
	p80211item_uint32_t	supprate7	;
	p80211item_uint32_t	supprate8	;
} __WLAN_ATTRIB_PACK__ p80211msg_dot11req_scan_results_t;

typedef struct p80211msg_dot11req_associate
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_pstr6_t	peerstaaddress	;
	u8	pad_8C[1]	;
	p80211item_uint32_t	associatefailuretimeout	;
	p80211item_uint32_t	cfpollable	;
	p80211item_uint32_t	cfpollreq	;
	p80211item_uint32_t	privacy	;
	p80211item_uint32_t	listeninterval	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_dot11req_associate_t;


typedef struct p80211msg_dot11req_reset
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	setdefaultmib	;
	p80211item_pstr6_t	macaddress	;
	u8	pad_11C[1]	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_dot11req_reset_t;

typedef struct p80211msg_dot11req_start
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_pstr32_t	ssid	;
	u8	pad_12D[3]	;
	p80211item_uint32_t	bsstype	;
	p80211item_uint32_t	beaconperiod	;
	p80211item_uint32_t	dtimperiod	;
	p80211item_uint32_t	cfpperiod	;
	p80211item_uint32_t	cfpmaxduration	;
	p80211item_uint32_t	fhdwelltime	;
	p80211item_uint32_t	fhhopset	;
	p80211item_uint32_t	fhhoppattern	;
	p80211item_uint32_t	dschannel	;
	p80211item_uint32_t	ibssatimwindow	;
	p80211item_uint32_t	probedelay	;
	p80211item_uint32_t	cfpollable	;
	p80211item_uint32_t	cfpollreq	;
	p80211item_uint32_t	basicrate1	;
	p80211item_uint32_t	basicrate2	;
	p80211item_uint32_t	basicrate3	;
	p80211item_uint32_t	basicrate4	;
	p80211item_uint32_t	basicrate5	;
	p80211item_uint32_t	basicrate6	;
	p80211item_uint32_t	basicrate7	;
	p80211item_uint32_t	basicrate8	;
	p80211item_uint32_t	operationalrate1	;
	p80211item_uint32_t	operationalrate2	;
	p80211item_uint32_t	operationalrate3	;
	p80211item_uint32_t	operationalrate4	;
	p80211item_uint32_t	operationalrate5	;
	p80211item_uint32_t	operationalrate6	;
	p80211item_uint32_t	operationalrate7	;
	p80211item_uint32_t	operationalrate8	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_dot11req_start_t;

typedef struct p80211msg_dot11ind_authenticate
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_pstr6_t	peerstaaddress	;
	u8	pad_13C[1]	;
	p80211item_uint32_t	authenticationtype	;
} __WLAN_ATTRIB_PACK__ p80211msg_dot11ind_authenticate_t;

typedef struct p80211msg_dot11ind_deauthenticate
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_pstr6_t	peerstaaddress	;
	u8	pad_14C[1]	;
	p80211item_uint32_t	reasoncode	;
} __WLAN_ATTRIB_PACK__ p80211msg_dot11ind_deauthenticate_t;

typedef struct p80211msg_dot11ind_associate
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_pstr6_t	peerstaaddress	;
	u8	pad_15C[1]	;
	p80211item_uint32_t	aid	;
} __WLAN_ATTRIB_PACK__ p80211msg_dot11ind_associate_t;

typedef struct p80211msg_dot11ind_reassociate
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_pstr6_t	peerstaaddress	;
	u8	pad_16C[1]	;
	p80211item_uint32_t	aid	;
	p80211item_pstr6_t	oldapaddress	;
	u8	pad_17C[1]	;
} __WLAN_ATTRIB_PACK__ p80211msg_dot11ind_reassociate_t;

typedef struct p80211msg_dot11ind_disassociate
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_pstr6_t	peerstaaddress	;
	u8	pad_18C[1]	;
	p80211item_uint32_t	reasoncode	;
} __WLAN_ATTRIB_PACK__ p80211msg_dot11ind_disassociate_t;

typedef struct p80211msg_lnxreq_ifstate
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	ifstate	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_lnxreq_ifstate_t;

typedef struct p80211msg_lnxreq_wlansniff
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	enable	;
	p80211item_uint32_t	channel	;
	p80211item_uint32_t	prismheader	;
	p80211item_uint32_t	wlanheader	;
	p80211item_uint32_t	keepwepflags	;
	p80211item_uint32_t	stripfcs	;
	p80211item_uint32_t	packet_trunc	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_lnxreq_wlansniff_t;

typedef struct p80211msg_lnxreq_hostwep
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	resultcode	;
	p80211item_uint32_t	decrypt	;
	p80211item_uint32_t	encrypt	;
} __WLAN_ATTRIB_PACK__ p80211msg_lnxreq_hostwep_t;

typedef struct p80211msg_lnxreq_commsquality
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	resultcode	;
	p80211item_uint32_t	dbm	;
	p80211item_uint32_t	link	;
	p80211item_uint32_t	level	;
	p80211item_uint32_t	noise	;
} __WLAN_ATTRIB_PACK__ p80211msg_lnxreq_commsquality_t;

typedef struct p80211msg_lnxreq_autojoin
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_pstr32_t	ssid	;
	u8	pad_19D[3]	;
	p80211item_uint32_t	authtype	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_lnxreq_autojoin_t;

typedef struct p80211msg_lnxind_wlansniffrm
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	hosttime	;
	p80211item_uint32_t	mactime	;
	p80211item_uint32_t	channel	;
	p80211item_uint32_t	rssi	;
	p80211item_uint32_t	sq	;
	p80211item_uint32_t	signal	;
	p80211item_uint32_t	noise	;
	p80211item_uint32_t	rate	;
	p80211item_uint32_t	istx	;
	p80211item_uint32_t	frmlen	;
} __WLAN_ATTRIB_PACK__ p80211msg_lnxind_wlansniffrm_t;

typedef struct p80211msg_lnxind_roam
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	reason	;
} __WLAN_ATTRIB_PACK__ p80211msg_lnxind_roam_t;

typedef struct p80211msg_p2req_join
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_pstr6_t	bssid	;
	u8	pad_20C[1]	;
	p80211item_uint32_t	basicrate1	;
	p80211item_uint32_t	basicrate2	;
	p80211item_uint32_t	basicrate3	;
	p80211item_uint32_t	basicrate4	;
	p80211item_uint32_t	basicrate5	;
	p80211item_uint32_t	basicrate6	;
	p80211item_uint32_t	basicrate7	;
	p80211item_uint32_t	basicrate8	;
	p80211item_uint32_t	operationalrate1	;
	p80211item_uint32_t	operationalrate2	;
	p80211item_uint32_t	operationalrate3	;
	p80211item_uint32_t	operationalrate4	;
	p80211item_uint32_t	operationalrate5	;
	p80211item_uint32_t	operationalrate6	;
	p80211item_uint32_t	operationalrate7	;
	p80211item_uint32_t	operationalrate8	;
	p80211item_pstr32_t	ssid	;
	u8	pad_21D[3]	;
	p80211item_uint32_t	channel	;
	p80211item_uint32_t	authtype	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_join_t;

typedef struct p80211msg_p2req_readpda
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_unk1024_t	pda	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_readpda_t;

typedef struct p80211msg_p2req_readcis
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_unk1024_t	cis	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_readcis_t;

typedef struct p80211msg_p2req_auxport_state
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	enable	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_auxport_state_t;

typedef struct p80211msg_p2req_auxport_read
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	addr	;
	p80211item_uint32_t	len	;
	p80211item_unk1024_t	data	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_auxport_read_t;

typedef struct p80211msg_p2req_auxport_write
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	addr	;
	p80211item_uint32_t	len	;
	p80211item_unk1024_t	data	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_auxport_write_t;

typedef struct p80211msg_p2req_low_level
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	command	;
	p80211item_uint32_t	param0	;
	p80211item_uint32_t	param1	;
	p80211item_uint32_t	param2	;
	p80211item_uint32_t	resp0	;
	p80211item_uint32_t	resp1	;
	p80211item_uint32_t	resp2	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_low_level_t;

typedef struct p80211msg_p2req_test_command
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	testcode	;
	p80211item_uint32_t	testparam	;
	p80211item_uint32_t	resultcode	;
	p80211item_uint32_t	status	;
	p80211item_uint32_t	resp0	;
	p80211item_uint32_t	resp1	;
	p80211item_uint32_t	resp2	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_test_command_t;

typedef struct p80211msg_p2req_mmi_read
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	addr	;
	p80211item_uint32_t	value	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_mmi_read_t;

typedef struct p80211msg_p2req_mmi_write
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	addr	;
	p80211item_uint32_t	data	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_mmi_write_t;

typedef struct p80211msg_p2req_ramdl_state
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	enable	;
	p80211item_uint32_t	exeaddr	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_ramdl_state_t;

typedef struct p80211msg_p2req_ramdl_write
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	addr	;
	p80211item_uint32_t	len	;
	p80211item_unk4096_t	data	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_ramdl_write_t;

typedef struct p80211msg_p2req_flashdl_state
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	enable	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_flashdl_state_t;

typedef struct p80211msg_p2req_flashdl_write
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	addr	;
	p80211item_uint32_t	len	;
	p80211item_unk4096_t	data	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_flashdl_write_t;

typedef struct p80211msg_p2req_mm_state
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	enable	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_mm_state_t;

typedef struct p80211msg_p2req_dump_state
{
	u32		msgcode	;
	u32		msglen	;
	u8		devname[WLAN_DEVNAMELEN_MAX]	;
	p80211item_uint32_t	level	;
	p80211item_uint32_t	resultcode	;
} __WLAN_ATTRIB_PACK__ p80211msg_p2req_dump_state_t;

#endif
