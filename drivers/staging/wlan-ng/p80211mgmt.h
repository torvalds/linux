/* SPDX-License-Identifier: (GPL-2.0 OR MPL-1.1) */
/*
 *
 * Macros, types, and functions to handle 802.11 mgmt frames
 *
 * Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
 * --------------------------------------------------------------------
 *
 * linux-wlan
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
 *
 * This file declares the constants and types used in the interface
 * between a wlan driver and the user mode utilities.
 *
 * Notes:
 *  - Constant values are always in HOST byte order.  To assign
 *    values to multi-byte fields they _must_ be converted to
 *    ieee byte order.  To retrieve multi-byte values from incoming
 *    frames, they must be converted to host order.
 *
 *  - The len member of the frame structure does NOT!!! include
 *    the MAC CRC.  Therefore, the len field on rx'd frames should
 *    have 4 subtracted from it.
 *
 * All functions declared here are implemented in p80211.c
 *
 * The types, macros, and functions defined here are primarily
 * used for encoding and decoding management frames.  They are
 * designed to follow these patterns of use:
 *
 * DECODE:
 * 1) a frame of length len is received into buffer b
 * 2) using the hdr structure and macros, we determine the type
 * 3) an appropriate mgmt frame structure, mf, is allocated and zeroed
 * 4) mf.hdr = b
 *    mf.buf = b
 *    mf.len = len
 * 5) call mgmt_decode( mf )
 * 6) the frame field pointers in mf are now set.  Note that any
 *    multi-byte frame field values accessed using the frame field
 *    pointers are in ieee byte order and will have to be converted
 *    to host order.
 *
 * ENCODE:
 * 1) Library client allocates buffer space for maximum length
 *    frame of the desired type
 * 2) Library client allocates a mgmt frame structure, called mf,
 *    of the desired type
 * 3) Set the following:
 *    mf.type = <desired type>
 *    mf.buf = <allocated buffer address>
 * 4) call mgmt_encode( mf )
 * 5) all of the fixed field pointers and fixed length information element
 *    pointers in mf are now set to their respective locations in the
 *    allocated space (fortunately, all variable length information elements
 *    fall at the end of their respective frames).
 * 5a) The length field is set to include the last of the fixed and fixed
 *     length fields.  It may have to be updated for optional or variable
 *	length information elements.
 * 6) Optional and variable length information elements are special cases
 *    and must be handled individually by the client code.
 * --------------------------------------------------------------------
 */

#ifndef _P80211MGMT_H
#define _P80211MGMT_H

#ifndef _P80211HDR_H
#include "p80211hdr.h"
#endif

/*-- Information Element IDs --------------------*/
#define WLAN_EID_SSID		0
#define WLAN_EID_SUPP_RATES	1
#define WLAN_EID_FH_PARMS	2
#define WLAN_EID_DS_PARMS	3
#define WLAN_EID_CF_PARMS	4
#define WLAN_EID_TIM		5
#define WLAN_EID_IBSS_PARMS	6
/*-- values 7-15 reserved --*/
#define WLAN_EID_CHALLENGE	16
/*-- values 17-31 reserved for challenge text extension --*/
/*-- values 32-255 reserved --*/

/*-- Reason Codes -------------------------------*/
#define WLAN_MGMT_REASON_RSVD			0
#define WLAN_MGMT_REASON_UNSPEC			1
#define WLAN_MGMT_REASON_PRIOR_AUTH_INVALID	2
#define WLAN_MGMT_REASON_DEAUTH_LEAVING		3
#define WLAN_MGMT_REASON_DISASSOC_INACTIVE	4
#define WLAN_MGMT_REASON_DISASSOC_AP_BUSY	5
#define WLAN_MGMT_REASON_CLASS2_NONAUTH		6
#define WLAN_MGMT_REASON_CLASS3_NONASSOC	7
#define WLAN_MGMT_REASON_DISASSOC_STA_HASLEFT	8
#define WLAN_MGMT_REASON_CANT_ASSOC_NONAUTH	9

/*-- Status Codes -------------------------------*/
#define WLAN_MGMT_STATUS_SUCCESS		0
#define WLAN_MGMT_STATUS_UNSPEC_FAILURE		1
#define WLAN_MGMT_STATUS_CAPS_UNSUPPORTED	10
#define WLAN_MGMT_STATUS_REASSOC_NO_ASSOC	11
#define WLAN_MGMT_STATUS_ASSOC_DENIED_UNSPEC	12
#define WLAN_MGMT_STATUS_UNSUPPORTED_AUTHALG	13
#define WLAN_MGMT_STATUS_RX_AUTH_NOSEQ		14
#define WLAN_MGMT_STATUS_CHALLENGE_FAIL		15
#define WLAN_MGMT_STATUS_AUTH_TIMEOUT		16
#define WLAN_MGMT_STATUS_ASSOC_DENIED_BUSY	17
#define WLAN_MGMT_STATUS_ASSOC_DENIED_RATES	18
  /* p80211b additions */
#define WLAN_MGMT_STATUS_ASSOC_DENIED_NOSHORT	19
#define WLAN_MGMT_STATUS_ASSOC_DENIED_NOPBCC	20
#define WLAN_MGMT_STATUS_ASSOC_DENIED_NOAGILITY	21

/*-- Auth Algorithm Field ---------------------------*/
#define WLAN_AUTH_ALG_OPENSYSTEM		0
#define WLAN_AUTH_ALG_SHAREDKEY			1

/*-- Management Frame Field Offsets -------------*/
/* Note: Not all fields are listed because of variable lengths,   */
/*       see the code in p80211.c to see how we search for fields */
/* Note: These offsets are from the start of the frame data       */

#define WLAN_BEACON_OFF_TS			0
#define WLAN_BEACON_OFF_BCN_int			8
#define WLAN_BEACON_OFF_CAPINFO			10
#define WLAN_BEACON_OFF_SSID			12

#define WLAN_DISASSOC_OFF_REASON		0

#define WLAN_ASSOCREQ_OFF_CAP_INFO		0
#define WLAN_ASSOCREQ_OFF_LISTEN_int		2
#define WLAN_ASSOCREQ_OFF_SSID			4

#define WLAN_ASSOCRESP_OFF_CAP_INFO		0
#define WLAN_ASSOCRESP_OFF_STATUS		2
#define WLAN_ASSOCRESP_OFF_AID			4
#define WLAN_ASSOCRESP_OFF_SUPP_RATES		6

#define WLAN_REASSOCREQ_OFF_CAP_INFO		0
#define WLAN_REASSOCREQ_OFF_LISTEN_int		2
#define WLAN_REASSOCREQ_OFF_CURR_AP		4
#define WLAN_REASSOCREQ_OFF_SSID		10

#define WLAN_REASSOCRESP_OFF_CAP_INFO		0
#define WLAN_REASSOCRESP_OFF_STATUS		2
#define WLAN_REASSOCRESP_OFF_AID		4
#define WLAN_REASSOCRESP_OFF_SUPP_RATES		6

#define WLAN_PROBEREQ_OFF_SSID			0

#define WLAN_PROBERESP_OFF_TS			0
#define WLAN_PROBERESP_OFF_BCN_int		8
#define WLAN_PROBERESP_OFF_CAP_INFO		10
#define WLAN_PROBERESP_OFF_SSID			12

#define WLAN_AUTHEN_OFF_AUTH_ALG		0
#define WLAN_AUTHEN_OFF_AUTH_SEQ		2
#define WLAN_AUTHEN_OFF_STATUS			4
#define WLAN_AUTHEN_OFF_CHALLENGE		6

#define WLAN_DEAUTHEN_OFF_REASON		0

/*-- Capability Field ---------------------------*/
#define WLAN_GET_MGMT_CAP_INFO_ESS(n)		((n) & BIT(0))
#define WLAN_GET_MGMT_CAP_INFO_IBSS(n)		(((n) & BIT(1)) >> 1)
#define WLAN_GET_MGMT_CAP_INFO_CFPOLLABLE(n)	(((n) & BIT(2)) >> 2)
#define WLAN_GET_MGMT_CAP_INFO_CFPOLLREQ(n)	(((n) & BIT(3)) >> 3)
#define WLAN_GET_MGMT_CAP_INFO_PRIVACY(n)	(((n) & BIT(4)) >> 4)
  /* p80211b additions */
#define WLAN_GET_MGMT_CAP_INFO_SHORT(n)		(((n) & BIT(5)) >> 5)
#define WLAN_GET_MGMT_CAP_INFO_PBCC(n)		(((n) & BIT(6)) >> 6)
#define WLAN_GET_MGMT_CAP_INFO_AGILITY(n)	(((n) & BIT(7)) >> 7)

#define WLAN_SET_MGMT_CAP_INFO_ESS(n)		(n)
#define WLAN_SET_MGMT_CAP_INFO_IBSS(n)		((n) << 1)
#define WLAN_SET_MGMT_CAP_INFO_CFPOLLABLE(n)	((n) << 2)
#define WLAN_SET_MGMT_CAP_INFO_CFPOLLREQ(n)	((n) << 3)
#define WLAN_SET_MGMT_CAP_INFO_PRIVACY(n)	((n) << 4)
  /* p80211b additions */
#define WLAN_SET_MGMT_CAP_INFO_SHORT(n)		((n) << 5)
#define WLAN_SET_MGMT_CAP_INFO_PBCC(n)		((n) << 6)
#define WLAN_SET_MGMT_CAP_INFO_AGILITY(n)	((n) << 7)

#endif /* _P80211MGMT_H */
