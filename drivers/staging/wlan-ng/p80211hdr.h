/* SPDX-License-Identifier: (GPL-2.0 OR MPL-1.1) */
/* p80211hdr.h
 *
 * Macros, types, and functions for handling 802.11 MAC headers
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
 *
 * This file declares the constants and types used in the interface
 * between a wlan driver and the user mode utilities.
 *
 * Note:
 *  - Constant values are always in HOST byte order.  To assign
 *    values to multi-byte fields they _must_ be converted to
 *    ieee byte order.  To retrieve multi-byte values from incoming
 *    frames, they must be converted to host order.
 *
 * All functions declared here are implemented in p80211.c
 * --------------------------------------------------------------------
 */

#ifndef _P80211HDR_H
#define _P80211HDR_H

#include <linux/if_ether.h>

/*--- Sizes -----------------------------------------------*/
#define WLAN_CRC_LEN			4
#define WLAN_BSSID_LEN			6
#define WLAN_HDR_A3_LEN			24
#define WLAN_HDR_A4_LEN			30
#define WLAN_SSID_MAXLEN		32
#define WLAN_DATA_MAXLEN		2312
#define WLAN_WEP_IV_LEN			4
#define WLAN_WEP_ICV_LEN		4

/*--- Frame Control Field -------------------------------------*/
/* Frame Types */
#define WLAN_FTYPE_MGMT			0x00
#define WLAN_FTYPE_CTL			0x01
#define WLAN_FTYPE_DATA			0x02

/* Frame subtypes */
/* Management */
#define WLAN_FSTYPE_ASSOCREQ		0x00
#define WLAN_FSTYPE_ASSOCRESP		0x01
#define WLAN_FSTYPE_REASSOCREQ		0x02
#define WLAN_FSTYPE_REASSOCRESP		0x03
#define WLAN_FSTYPE_PROBEREQ		0x04
#define WLAN_FSTYPE_PROBERESP		0x05
#define WLAN_FSTYPE_BEACON		0x08
#define WLAN_FSTYPE_ATIM		0x09
#define WLAN_FSTYPE_DISASSOC		0x0a
#define WLAN_FSTYPE_AUTHEN		0x0b
#define WLAN_FSTYPE_DEAUTHEN		0x0c

/* Control */
#define WLAN_FSTYPE_BLOCKACKREQ		0x8
#define WLAN_FSTYPE_BLOCKACK		0x9
#define WLAN_FSTYPE_PSPOLL		0x0a
#define WLAN_FSTYPE_RTS			0x0b
#define WLAN_FSTYPE_CTS			0x0c
#define WLAN_FSTYPE_ACK			0x0d
#define WLAN_FSTYPE_CFEND		0x0e
#define WLAN_FSTYPE_CFENDCFACK		0x0f

/* Data */
#define WLAN_FSTYPE_DATAONLY		0x00
#define WLAN_FSTYPE_DATA_CFACK		0x01
#define WLAN_FSTYPE_DATA_CFPOLL		0x02
#define WLAN_FSTYPE_DATA_CFACK_CFPOLL	0x03
#define WLAN_FSTYPE_NULL		0x04
#define WLAN_FSTYPE_CFACK		0x05
#define WLAN_FSTYPE_CFPOLL		0x06
#define WLAN_FSTYPE_CFACK_CFPOLL	0x07

/*--- FC Macros ----------------------------------------------*/
/* Macros to get/set the bitfields of the Frame Control Field */
/*  GET_FC_??? - takes the host byte-order value of an FC     */
/*               and retrieves the value of one of the        */
/*               bitfields and moves that value so its lsb is */
/*               in bit 0.                                    */
/*  SET_FC_??? - takes a host order value for one of the FC   */
/*               bitfields and moves it to the proper bit     */
/*               location for ORing into a host order FC.     */
/*               To send the FC produced from SET_FC_???,     */
/*               one must put the bytes in IEEE order.        */
/*  e.g.                                                      */
/*     printf("the frame subtype is %x",                      */
/*                 GET_FC_FTYPE( ieee2host( rx.fc )))         */
/*                                                            */
/*     tx.fc = host2ieee( SET_FC_FTYPE(WLAN_FTYP_CTL) |       */
/*                        SET_FC_FSTYPE(WLAN_FSTYPE_RTS) );   */
/*------------------------------------------------------------*/

#define WLAN_GET_FC_FTYPE(n)	((((u16)(n)) & GENMASK(3, 2)) >> 2)
#define WLAN_GET_FC_FSTYPE(n)	((((u16)(n)) & GENMASK(7, 4)) >> 4)
#define WLAN_GET_FC_TODS(n)	((((u16)(n)) & (BIT(8))) >> 8)
#define WLAN_GET_FC_FROMDS(n)	((((u16)(n)) & (BIT(9))) >> 9)
#define WLAN_GET_FC_ISWEP(n)	((((u16)(n)) & (BIT(14))) >> 14)

#define WLAN_SET_FC_FTYPE(n)	(((u16)(n)) << 2)
#define WLAN_SET_FC_FSTYPE(n)	(((u16)(n)) << 4)
#define WLAN_SET_FC_TODS(n)	(((u16)(n)) << 8)
#define WLAN_SET_FC_FROMDS(n)	(((u16)(n)) << 9)
#define WLAN_SET_FC_ISWEP(n)	(((u16)(n)) << 14)

#define DOT11_RATE5_ISBASIC_GET(r)     (((u8)(r)) & BIT(7))

/* Generic 802.11 Header types */

struct p80211_hdr {
	__le16	frame_control;
	u16	duration_id;
	u8	address1[ETH_ALEN];
	u8	address2[ETH_ALEN];
	u8	address3[ETH_ALEN];
	u16	sequence_control;
	u8	address4[ETH_ALEN];
} __packed;

/* Frame and header length macros */

static inline u16 wlan_ctl_framelen(u16 fstype)
{
	switch (fstype)	{
	case WLAN_FSTYPE_BLOCKACKREQ:
		return 24;
	case WLAN_FSTYPE_BLOCKACK:
		return 152;
	case WLAN_FSTYPE_PSPOLL:
	case WLAN_FSTYPE_RTS:
	case WLAN_FSTYPE_CFEND:
	case WLAN_FSTYPE_CFENDCFACK:
		return 20;
	case WLAN_FSTYPE_CTS:
	case WLAN_FSTYPE_ACK:
		return 14;
	default:
		return 4;
	}
}

#define WLAN_FCS_LEN			4

/* ftcl in HOST order */
static inline u16 p80211_headerlen(u16 fctl)
{
	u16 hdrlen = 0;

	switch (WLAN_GET_FC_FTYPE(fctl)) {
	case WLAN_FTYPE_MGMT:
		hdrlen = WLAN_HDR_A3_LEN;
		break;
	case WLAN_FTYPE_DATA:
		hdrlen = WLAN_HDR_A3_LEN;
		if (WLAN_GET_FC_TODS(fctl) && WLAN_GET_FC_FROMDS(fctl))
			hdrlen += ETH_ALEN;
		break;
	case WLAN_FTYPE_CTL:
		hdrlen = wlan_ctl_framelen(WLAN_GET_FC_FSTYPE(fctl)) -
		    WLAN_FCS_LEN;
		break;
	default:
		hdrlen = WLAN_HDR_A3_LEN;
	}

	return hdrlen;
}

#endif /* _P80211HDR_H */
