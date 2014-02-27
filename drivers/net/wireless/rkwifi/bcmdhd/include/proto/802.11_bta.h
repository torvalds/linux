/*
 * BT-AMP (BlueTooth Alternate Mac and Phy) 802.11 PAL (Protocol Adaptation Layer)
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: 802.11_bta.h 382882 2013-02-04 23:24:31Z $
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
