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

#ifndef _P80211MKMETADEF_H
#define _P80211MKMETADEF_H


#define DIDmsg_cat_dot11req \
			P80211DID_MKSECTION(1)
#define DIDmsg_dot11req_mibget \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(1))
#define DIDmsg_dot11req_mibget_mibattribute \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(1) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_dot11req_mibget_resultcode \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(1) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_dot11req_mibset \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(2))
#define DIDmsg_dot11req_mibset_mibattribute \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_dot11req_mibset_resultcode \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_dot11req_scan \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4))
#define DIDmsg_dot11req_scan_bsstype \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_dot11req_scan_bssid \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_dot11req_scan_ssid \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(3) | 0x00000000)
#define DIDmsg_dot11req_scan_scantype \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(4) | 0x00000000)
#define DIDmsg_dot11req_scan_probedelay \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(5) | 0x00000000)
#define DIDmsg_dot11req_scan_channellist \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(6) | 0x00000000)
#define DIDmsg_dot11req_scan_minchanneltime \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(7) | 0x00000000)
#define DIDmsg_dot11req_scan_maxchanneltime \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(8) | 0x00000000)
#define DIDmsg_dot11req_scan_resultcode \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(9) | 0x00000000)
#define DIDmsg_dot11req_scan_numbss \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(10) | 0x00000000)
#define DIDmsg_dot11req_scan_append \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(11) | 0x00000000)
#define DIDmsg_dot11req_scan_results \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5))
#define DIDmsg_dot11req_scan_results_bssindex \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_dot11req_scan_results_resultcode \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_dot11req_scan_results_signal \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(3) | 0x00000000)
#define DIDmsg_dot11req_scan_results_noise \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(4) | 0x00000000)
#define DIDmsg_dot11req_scan_results_bssid \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(5) | 0x00000000)
#define DIDmsg_dot11req_scan_results_ssid \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(6) | 0x00000000)
#define DIDmsg_dot11req_scan_results_bsstype \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(7) | 0x00000000)
#define DIDmsg_dot11req_scan_results_beaconperiod \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(8) | 0x00000000)
#define DIDmsg_dot11req_scan_results_dtimperiod \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(9) | 0x00000000)
#define DIDmsg_dot11req_scan_results_timestamp \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(10) | 0x00000000)
#define DIDmsg_dot11req_scan_results_localtime \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(11) | 0x00000000)
#define DIDmsg_dot11req_scan_results_fhdwelltime \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(12) | 0x00000000)
#define DIDmsg_dot11req_scan_results_fhhopset \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(13) | 0x00000000)
#define DIDmsg_dot11req_scan_results_fhhoppattern \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(14) | 0x00000000)
#define DIDmsg_dot11req_scan_results_fhhopindex \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(15) | 0x00000000)
#define DIDmsg_dot11req_scan_results_dschannel \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(16) | 0x00000000)
#define DIDmsg_dot11req_scan_results_cfpcount \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(17) | 0x00000000)
#define DIDmsg_dot11req_scan_results_cfpperiod \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(18) | 0x00000000)
#define DIDmsg_dot11req_scan_results_cfpmaxduration \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(19) | 0x00000000)
#define DIDmsg_dot11req_scan_results_cfpdurremaining \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(20) | 0x00000000)
#define DIDmsg_dot11req_scan_results_ibssatimwindow \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(21) | 0x00000000)
#define DIDmsg_dot11req_scan_results_cfpollable \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(22) | 0x00000000)
#define DIDmsg_dot11req_scan_results_cfpollreq \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(23) | 0x00000000)
#define DIDmsg_dot11req_scan_results_privacy \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(24) | 0x00000000)
#define DIDmsg_dot11req_scan_results_basicrate1 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(25) | 0x00000000)
#define DIDmsg_dot11req_scan_results_basicrate2 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(26) | 0x00000000)
#define DIDmsg_dot11req_scan_results_basicrate3 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(27) | 0x00000000)
#define DIDmsg_dot11req_scan_results_basicrate4 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(28) | 0x00000000)
#define DIDmsg_dot11req_scan_results_basicrate5 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(29) | 0x00000000)
#define DIDmsg_dot11req_scan_results_basicrate6 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(30) | 0x00000000)
#define DIDmsg_dot11req_scan_results_basicrate7 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(31) | 0x00000000)
#define DIDmsg_dot11req_scan_results_basicrate8 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(32) | 0x00000000)
#define DIDmsg_dot11req_scan_results_supprate1 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(33) | 0x00000000)
#define DIDmsg_dot11req_scan_results_supprate2 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(34) | 0x00000000)
#define DIDmsg_dot11req_scan_results_supprate3 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(35) | 0x00000000)
#define DIDmsg_dot11req_scan_results_supprate4 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(36) | 0x00000000)
#define DIDmsg_dot11req_scan_results_supprate5 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(37) | 0x00000000)
#define DIDmsg_dot11req_scan_results_supprate6 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(38) | 0x00000000)
#define DIDmsg_dot11req_scan_results_supprate7 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(39) | 0x00000000)
#define DIDmsg_dot11req_scan_results_supprate8 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(40) | 0x00000000)
#define DIDmsg_dot11req_start \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13))
#define DIDmsg_dot11req_start_ssid \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_dot11req_start_bsstype \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_dot11req_start_beaconperiod \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(3) | 0x00000000)
#define DIDmsg_dot11req_start_dtimperiod \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(4) | 0x00000000)
#define DIDmsg_dot11req_start_cfpperiod \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(5) | 0x00000000)
#define DIDmsg_dot11req_start_cfpmaxduration \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(6) | 0x00000000)
#define DIDmsg_dot11req_start_fhdwelltime \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(7) | 0x00000000)
#define DIDmsg_dot11req_start_fhhopset \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(8) | 0x00000000)
#define DIDmsg_dot11req_start_fhhoppattern \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(9) | 0x00000000)
#define DIDmsg_dot11req_start_dschannel \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(10) | 0x00000000)
#define DIDmsg_dot11req_start_ibssatimwindow \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(11) | 0x00000000)
#define DIDmsg_dot11req_start_probedelay \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(12) | 0x00000000)
#define DIDmsg_dot11req_start_cfpollable \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(13) | 0x00000000)
#define DIDmsg_dot11req_start_cfpollreq \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(14) | 0x00000000)
#define DIDmsg_dot11req_start_basicrate1 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(15) | 0x00000000)
#define DIDmsg_dot11req_start_basicrate2 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(16) | 0x00000000)
#define DIDmsg_dot11req_start_basicrate3 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(17) | 0x00000000)
#define DIDmsg_dot11req_start_basicrate4 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(18) | 0x00000000)
#define DIDmsg_dot11req_start_basicrate5 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(19) | 0x00000000)
#define DIDmsg_dot11req_start_basicrate6 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(20) | 0x00000000)
#define DIDmsg_dot11req_start_basicrate7 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(21) | 0x00000000)
#define DIDmsg_dot11req_start_basicrate8 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(22) | 0x00000000)
#define DIDmsg_dot11req_start_operationalrate1 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(23) | 0x00000000)
#define DIDmsg_dot11req_start_operationalrate2 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(24) | 0x00000000)
#define DIDmsg_dot11req_start_operationalrate3 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(25) | 0x00000000)
#define DIDmsg_dot11req_start_operationalrate4 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(26) | 0x00000000)
#define DIDmsg_dot11req_start_operationalrate5 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(27) | 0x00000000)
#define DIDmsg_dot11req_start_operationalrate6 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(28) | 0x00000000)
#define DIDmsg_dot11req_start_operationalrate7 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(29) | 0x00000000)
#define DIDmsg_dot11req_start_operationalrate8 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(30) | 0x00000000)
#define DIDmsg_dot11req_start_resultcode \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(31) | 0x00000000)
#define DIDmsg_cat_dot11ind \
			P80211DID_MKSECTION(2)
#define DIDmsg_dot11ind_authenticate \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(1))
#define DIDmsg_dot11ind_authenticate_peerstaaddress \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(1) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_dot11ind_authenticate_authenticationtype \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(1) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_dot11ind_deauthenticate \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(2))
#define DIDmsg_dot11ind_deauthenticate_peerstaaddress \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_dot11ind_deauthenticate_reasoncode \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_dot11ind_associate \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(3))
#define DIDmsg_dot11ind_associate_peerstaaddress \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(3) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_dot11ind_associate_aid \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(3) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_dot11ind_reassociate \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(4))
#define DIDmsg_dot11ind_reassociate_peerstaaddress \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_dot11ind_reassociate_aid \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_dot11ind_reassociate_oldapaddress \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(3) | 0x00000000)
#define DIDmsg_dot11ind_disassociate \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(5))
#define DIDmsg_dot11ind_disassociate_peerstaaddress \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_dot11ind_disassociate_reasoncode \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_cat_lnxreq \
			P80211DID_MKSECTION(3)
#define DIDmsg_lnxreq_ifstate \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(1))
#define DIDmsg_lnxreq_ifstate_ifstate \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(1) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_lnxreq_ifstate_resultcode \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(1) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_lnxreq_wlansniff \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(2))
#define DIDmsg_lnxreq_wlansniff_enable \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_lnxreq_wlansniff_channel \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_lnxreq_wlansniff_prismheader \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(3) | 0x00000000)
#define DIDmsg_lnxreq_wlansniff_wlanheader \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(4) | 0x00000000)
#define DIDmsg_lnxreq_wlansniff_keepwepflags \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(5) | 0x00000000)
#define DIDmsg_lnxreq_wlansniff_stripfcs \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(6) | 0x00000000)
#define DIDmsg_lnxreq_wlansniff_packet_trunc \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(7) | 0x00000000)
#define DIDmsg_lnxreq_wlansniff_resultcode \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(8) | 0x00000000)
#define DIDmsg_lnxreq_hostwep \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(3))
#define DIDmsg_lnxreq_hostwep_resultcode \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(3) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_lnxreq_hostwep_decrypt \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(3) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_lnxreq_hostwep_encrypt \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(3) | \
			P80211DID_MKITEM(3) | 0x00000000)
#define DIDmsg_lnxreq_commsquality \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(4))
#define DIDmsg_lnxreq_commsquality_resultcode \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_lnxreq_commsquality_dbm \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_lnxreq_commsquality_link \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(3) | 0x00000000)
#define DIDmsg_lnxreq_commsquality_level \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(4) | 0x00000000)
#define DIDmsg_lnxreq_commsquality_noise \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(5) | 0x00000000)
#define DIDmsg_lnxreq_autojoin \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(5))
#define DIDmsg_lnxreq_autojoin_ssid \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_lnxreq_autojoin_authtype \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_lnxreq_autojoin_resultcode \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(3) | 0x00000000)
#define DIDmsg_cat_p2req \
			P80211DID_MKSECTION(5)
#define DIDmsg_p2req_readpda \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(2))
#define DIDmsg_p2req_readpda_pda \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_p2req_readpda_resultcode \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_p2req_ramdl_state \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(11))
#define DIDmsg_p2req_ramdl_state_enable \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(11) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_p2req_ramdl_state_exeaddr \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(11) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_p2req_ramdl_state_resultcode \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(11) | \
			P80211DID_MKITEM(3) | 0x00000000)
#define DIDmsg_p2req_ramdl_write \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(12))
#define DIDmsg_p2req_ramdl_write_addr \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(12) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_p2req_ramdl_write_len \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(12) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_p2req_ramdl_write_data \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(12) | \
			P80211DID_MKITEM(3) | 0x00000000)
#define DIDmsg_p2req_ramdl_write_resultcode \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(12) | \
			P80211DID_MKITEM(4) | 0x00000000)
#define DIDmsg_p2req_flashdl_state \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(13))
#define DIDmsg_p2req_flashdl_state_enable \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_p2req_flashdl_state_resultcode \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(13) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_p2req_flashdl_write \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(14))
#define DIDmsg_p2req_flashdl_write_addr \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(14) | \
			P80211DID_MKITEM(1) | 0x00000000)
#define DIDmsg_p2req_flashdl_write_len \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(14) | \
			P80211DID_MKITEM(2) | 0x00000000)
#define DIDmsg_p2req_flashdl_write_data \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(14) | \
			P80211DID_MKITEM(3) | 0x00000000)
#define DIDmsg_p2req_flashdl_write_resultcode \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(14) | \
			P80211DID_MKITEM(4) | 0x00000000)
#define DIDmib_cat_dot11smt \
			P80211DID_MKSECTION(1)
#define DIDmib_dot11smt_dot11WEPDefaultKeysTable \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4))
#define DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey0 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(1) | 0x0c000000)
#define DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey1 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(2) | 0x0c000000)
#define DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey2 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(3) | 0x0c000000)
#define DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey3 \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(4) | \
			P80211DID_MKITEM(4) | 0x0c000000)
#define DIDmib_dot11smt_dot11PrivacyTable \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(6))
#define DIDmib_dot11smt_dot11PrivacyTable_dot11PrivacyInvoked \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(6) | \
			P80211DID_MKITEM(1) | 0x18000000)
#define DIDmib_dot11smt_dot11PrivacyTable_dot11WEPDefaultKeyID \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(6) | \
			P80211DID_MKITEM(2) | 0x18000000)
#define DIDmib_dot11smt_dot11PrivacyTable_dot11ExcludeUnencrypted \
			(P80211DID_MKSECTION(1) | \
			P80211DID_MKGROUP(6) | \
			P80211DID_MKITEM(4) | 0x18000000)
#define DIDmib_cat_dot11mac \
			P80211DID_MKSECTION(2)
#define DIDmib_dot11mac_dot11OperationTable \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(1))
#define DIDmib_dot11mac_dot11OperationTable_dot11MACAddress \
                        (P80211DID_MKSECTION(2) | \
                        P80211DID_MKGROUP(1) | \
                        P80211DID_MKITEM(1) | 0x18000000)
#define DIDmib_dot11mac_dot11OperationTable_dot11RTSThreshold \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(1) | \
			P80211DID_MKITEM(2) | 0x18000000)
#define DIDmib_dot11mac_dot11OperationTable_dot11ShortRetryLimit \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(1) | \
			P80211DID_MKITEM(3) | 0x10000000)
#define DIDmib_dot11mac_dot11OperationTable_dot11LongRetryLimit \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(1) | \
			P80211DID_MKITEM(4) | 0x10000000)
#define DIDmib_dot11mac_dot11OperationTable_dot11FragmentationThreshold \
			(P80211DID_MKSECTION(2) | \
			P80211DID_MKGROUP(1) | \
			P80211DID_MKITEM(5) | 0x18000000)
#define DIDmib_dot11mac_dot11OperationTable_dot11MaxTransmitMSDULifetime \
                       (P80211DID_MKSECTION(2) | \
                       P80211DID_MKGROUP(1) | \
                       P80211DID_MKITEM(6) | 0x10000000)
#define DIDmib_cat_dot11phy \
			P80211DID_MKSECTION(3)
#define DIDmib_dot11phy_dot11PhyOperationTable \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(1))
#define DIDmib_dot11phy_dot11PhyTxPowerTable_dot11CurrentTxPowerLevel \
                       (P80211DID_MKSECTION(3) | \
                       P80211DID_MKGROUP(3) | \
                       P80211DID_MKITEM(10) | 0x18000000)
#define DIDmib_dot11phy_dot11PhyDSSSTable \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(5))
#define DIDmib_dot11phy_dot11PhyDSSSTable_dot11CurrentChannel \
			(P80211DID_MKSECTION(3) | \
			P80211DID_MKGROUP(5) | \
			P80211DID_MKITEM(1) | 0x10000000)
#define DIDmib_cat_lnx \
			P80211DID_MKSECTION(4)
#define DIDmib_lnx_lnxConfigTable \
			(P80211DID_MKSECTION(4) | \
			P80211DID_MKGROUP(1))
#define DIDmib_lnx_lnxConfigTable_lnxRSNAIE \
			(P80211DID_MKSECTION(4) | \
			P80211DID_MKGROUP(1) | \
			P80211DID_MKITEM(1) | 0x18000000)
#define DIDmib_cat_p2 \
			P80211DID_MKSECTION(5)
#define DIDmib_p2_p2Static \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(2))
#define DIDmib_p2_p2Static_p2CnfPortType \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(2) | \
			P80211DID_MKITEM(1) | 0x18000000)
#define DIDmib_p2_p2MAC \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(6))
#define DIDmib_p2_p2MAC_p2CurrentTxRate \
			(P80211DID_MKSECTION(5) | \
			P80211DID_MKGROUP(6) | \
			P80211DID_MKITEM(12) | 0x10000000)
#endif
