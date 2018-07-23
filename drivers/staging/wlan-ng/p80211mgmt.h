/* SPDX-License-Identifier: (GPL-2.0 OR MPL-1.1) */
/* p80211mgmt.h
 *
 * Macros, types, and functions to handle 802.11 mgmt frames
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

/*-- Information Element Types --------------------*/
/* prototype structure, all IEs start with these members */

struct wlan_ie {
	u8 eid;
	u8 len;
} __packed;

/*-- Service Set Identity (SSID)  -----------------*/
struct wlan_ie_ssid {
	u8 eid;
	u8 len;
	u8 ssid[1];		/* may be zero, ptrs may overlap */
} __packed;

/*-- Supported Rates  -----------------------------*/
struct wlan_ie_supp_rates {
	u8 eid;
	u8 len;
	u8 rates[1];		/* had better be at LEAST one! */
} __packed;

/*-- FH Parameter Set  ----------------------------*/
struct wlan_ie_fh_parms {
	u8 eid;
	u8 len;
	u16 dwell;
	u8 hopset;
	u8 hoppattern;
	u8 hopindex;
} __packed;

/*-- DS Parameter Set  ----------------------------*/
struct wlan_ie_ds_parms {
	u8 eid;
	u8 len;
	u8 curr_ch;
} __packed;

/*-- CF Parameter Set  ----------------------------*/

struct wlan_ie_cf_parms {
	u8 eid;
	u8 len;
	u8 cfp_cnt;
	u8 cfp_period;
	u16 cfp_maxdur;
	u16 cfp_durremaining;
} __packed;

/*-- TIM ------------------------------------------*/
struct wlan_ie_tim {
	u8 eid;
	u8 len;
	u8 dtim_cnt;
	u8 dtim_period;
	u8 bitmap_ctl;
	u8 virt_bm[1];
} __packed;

/*-- IBSS Parameter Set ---------------------------*/
struct wlan_ie_ibss_parms {
	u8 eid;
	u8 len;
	u16 atim_win;
} __packed;

/*-- Challenge Text  ------------------------------*/
struct wlan_ie_challenge {
	u8 eid;
	u8 len;
	u8 challenge[1];
} __packed;

/*-------------------------------------------------*/
/*  Frame Types  */

/* prototype structure, all mgmt frame types will start with these members */
struct wlan_fr_mgmt {
	u16 type;
	u16 len;		/* DOES NOT include CRC !!!! */
	u8 *buf;
	union p80211_hdr *hdr;
	/* used for target specific data, skb in Linux */
	void *priv;
	/*-- fixed fields -----------*/
	/*-- info elements ----------*/
};

/*-- Beacon ---------------------------------------*/
struct wlan_fr_beacon {
	u16 type;
	u16 len;
	u8 *buf;
	union p80211_hdr *hdr;
	/* used for target specific data, skb in Linux */
	void *priv;
	/*-- fixed fields -----------*/
	u64 *ts;
	u16 *bcn_int;
	u16 *cap_info;
	/*-- info elements ----------*/
	struct wlan_ie_ssid *ssid;
	struct wlan_ie_supp_rates *supp_rates;
	struct wlan_ie_fh_parms *fh_parms;
	struct wlan_ie_ds_parms *ds_parms;
	struct wlan_ie_cf_parms *cf_parms;
	struct wlan_ie_ibss_parms *ibss_parms;
	struct wlan_ie_tim *tim;

};

/*-- IBSS ATIM ------------------------------------*/
struct wlan_fr_ibssatim {
	u16 type;
	u16 len;
	u8 *buf;
	union p80211_hdr *hdr;
	/* used for target specific data, skb in Linux */
	void *priv;

	/*-- fixed fields -----------*/
	/*-- info elements ----------*/

	/* this frame type has a null body */

};

/*-- Disassociation -------------------------------*/
struct wlan_fr_disassoc {
	u16 type;
	u16 len;
	u8 *buf;
	union p80211_hdr *hdr;
	/* used for target specific data, skb in Linux */
	void *priv;
	/*-- fixed fields -----------*/
	u16 *reason;

	/*-- info elements ----------*/

};

/*-- Association Request --------------------------*/
struct wlan_fr_assocreq {
	u16 type;
	u16 len;
	u8 *buf;
	union p80211_hdr *hdr;
	/* used for target specific data, skb in Linux */
	void *priv;
	/*-- fixed fields -----------*/
	u16 *cap_info;
	u16 *listen_int;
	/*-- info elements ----------*/
	struct wlan_ie_ssid *ssid;
	struct wlan_ie_supp_rates *supp_rates;

};

/*-- Association Response -------------------------*/
struct wlan_fr_assocresp {
	u16 type;
	u16 len;
	u8 *buf;
	union p80211_hdr *hdr;
	/* used for target specific data, skb in Linux */
	void *priv;
	/*-- fixed fields -----------*/
	u16 *cap_info;
	u16 *status;
	u16 *aid;
	/*-- info elements ----------*/
	struct wlan_ie_supp_rates *supp_rates;

};

/*-- Reassociation Request ------------------------*/
struct wlan_fr_reassocreq {
	u16 type;
	u16 len;
	u8 *buf;
	union p80211_hdr *hdr;
	/* used for target specific data, skb in Linux */
	void *priv;
	/*-- fixed fields -----------*/
	u16 *cap_info;
	u16 *listen_int;
	u8 *curr_ap;
	/*-- info elements ----------*/
	struct wlan_ie_ssid *ssid;
	struct wlan_ie_supp_rates *supp_rates;

};

/*-- Reassociation Response -----------------------*/
struct wlan_fr_reassocresp {
	u16 type;
	u16 len;
	u8 *buf;
	union p80211_hdr *hdr;
	/* used for target specific data, skb in Linux */
	void *priv;
	/*-- fixed fields -----------*/
	u16 *cap_info;
	u16 *status;
	u16 *aid;
	/*-- info elements ----------*/
	struct wlan_ie_supp_rates *supp_rates;

};

/*-- Probe Request --------------------------------*/
struct wlan_fr_probereq {
	u16 type;
	u16 len;
	u8 *buf;
	union p80211_hdr *hdr;
	/* used for target specific data, skb in Linux */
	void *priv;
	/*-- fixed fields -----------*/
	/*-- info elements ----------*/
	struct wlan_ie_ssid *ssid;
	struct wlan_ie_supp_rates *supp_rates;

};

/*-- Probe Response -------------------------------*/
struct wlan_fr_proberesp {
	u16 type;
	u16 len;
	u8 *buf;
	union p80211_hdr *hdr;
	/* used for target specific data, skb in Linux */
	void *priv;
	/*-- fixed fields -----------*/
	u64 *ts;
	u16 *bcn_int;
	u16 *cap_info;
	/*-- info elements ----------*/
	struct wlan_ie_ssid *ssid;
	struct wlan_ie_supp_rates *supp_rates;
	struct wlan_ie_fh_parms *fh_parms;
	struct wlan_ie_ds_parms *ds_parms;
	struct wlan_ie_cf_parms *cf_parms;
	struct wlan_ie_ibss_parms *ibss_parms;
};

/*-- Authentication -------------------------------*/
struct wlan_fr_authen {
	u16 type;
	u16 len;
	u8 *buf;
	union p80211_hdr *hdr;
	/* used for target specific data, skb in Linux */
	void *priv;
	/*-- fixed fields -----------*/
	u16 *auth_alg;
	u16 *auth_seq;
	u16 *status;
	/*-- info elements ----------*/
	struct wlan_ie_challenge *challenge;

};

/*-- Deauthenication -----------------------------*/
struct wlan_fr_deauthen {
	u16 type;
	u16 len;
	u8 *buf;
	union p80211_hdr *hdr;
	/* used for target specific data, skb in Linux */
	void *priv;
	/*-- fixed fields -----------*/
	u16 *reason;

	/*-- info elements ----------*/

};

void wlan_mgmt_encode_beacon(struct wlan_fr_beacon *f);
void wlan_mgmt_decode_beacon(struct wlan_fr_beacon *f);
void wlan_mgmt_encode_disassoc(struct wlan_fr_disassoc *f);
void wlan_mgmt_decode_disassoc(struct wlan_fr_disassoc *f);
void wlan_mgmt_encode_assocreq(struct wlan_fr_assocreq *f);
void wlan_mgmt_decode_assocreq(struct wlan_fr_assocreq *f);
void wlan_mgmt_encode_assocresp(struct wlan_fr_assocresp *f);
void wlan_mgmt_decode_assocresp(struct wlan_fr_assocresp *f);
void wlan_mgmt_encode_reassocreq(struct wlan_fr_reassocreq *f);
void wlan_mgmt_decode_reassocreq(struct wlan_fr_reassocreq *f);
void wlan_mgmt_encode_reassocresp(struct wlan_fr_reassocresp *f);
void wlan_mgmt_decode_reassocresp(struct wlan_fr_reassocresp *f);
void wlan_mgmt_encode_probereq(struct wlan_fr_probereq *f);
void wlan_mgmt_decode_probereq(struct wlan_fr_probereq *f);
void wlan_mgmt_encode_proberesp(struct wlan_fr_proberesp *f);
void wlan_mgmt_decode_proberesp(struct wlan_fr_proberesp *f);
void wlan_mgmt_encode_authen(struct wlan_fr_authen *f);
void wlan_mgmt_decode_authen(struct wlan_fr_authen *f);
void wlan_mgmt_encode_deauthen(struct wlan_fr_deauthen *f);
void wlan_mgmt_decode_deauthen(struct wlan_fr_deauthen *f);

#endif /* _P80211MGMT_H */
