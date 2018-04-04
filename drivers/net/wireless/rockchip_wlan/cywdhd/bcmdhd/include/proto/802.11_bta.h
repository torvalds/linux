/*
 * BT-AMP (BlueTooth Alternate Mac and Phy) 802.11 PAL (Protocol Adaptation Layer)
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
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
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: 802.11_bta.h 518342 2014-12-01 23:21:41Z $
*/

#ifndef _802_11_BTA_H_
#define _802_11_BTA_H_

#define BT_SIG_SNAP_MPROT		"\xAA\xAA\x03\x00\x19\x58"

/* BT-AMP 802.11 PAL Protocols */
#define BTA_PROT_L2CAP				1
#define	BTA_PROT_ACTIVITY_REPORT		2
#define BTA_PROT_SECURITY			3
#define BTA_PROT_LINK_SUPERVISION_REQUEST	4
#define BTA_PROT_LINK_SUPERVISION_REPLY		5

/* BT-AMP 802.11 PAL AMP_ASSOC Type IDs */
#define BTA_TYPE_ID_MAC_ADDRESS			1
#define BTA_TYPE_ID_PREFERRED_CHANNELS		2
#define BTA_TYPE_ID_CONNECTED_CHANNELS		3
#define BTA_TYPE_ID_CAPABILITIES		4
#define BTA_TYPE_ID_VERSION			5
#endif /* _802_11_bta_h_ */
