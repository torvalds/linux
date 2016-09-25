/* p80211types.h
*
* Macros, constants, types, and funcs for p80211 data types
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
* This file declares some of the constants and types used in various
* parts of the linux-wlan system.
*
* Notes:
*   - Constant values are always in HOST byte order.
*
* All functions and statics declared here are implemented in p80211types.c
*   --------------------------------------------------------------------
*/

#ifndef _P80211TYPES_H
#define _P80211TYPES_H

/*----------------------------------------------------------------*/
/* The following constants are indexes into the Mib Category List */
/* and the Message Category List */

/* Mib Category List */
#define P80211_MIB_CAT_DOT11SMT		1
#define P80211_MIB_CAT_DOT11MAC		2
#define P80211_MIB_CAT_DOT11PHY		3

#define P80211SEC_DOT11SMT		P80211_MIB_CAT_DOT11SMT
#define P80211SEC_DOT11MAC		P80211_MIB_CAT_DOT11MAC
#define P80211SEC_DOT11PHY		P80211_MIB_CAT_DOT11PHY

/* Message Category List */
#define P80211_MSG_CAT_DOT11REQ		1
#define P80211_MSG_CAT_DOT11IND		2

/*----------------------------------------------------------------*/
/* p80211 enumeration constants.  The value to text mappings for */
/*  these is in p80211types.c.  These defines were generated */
/*  from the mappings. */

/* error codes for lookups */

#define P80211ENUM_truth_false			0
#define P80211ENUM_truth_true			1
#define P80211ENUM_ifstate_disable		0
#define P80211ENUM_ifstate_fwload		1
#define P80211ENUM_ifstate_enable		2
#define P80211ENUM_bsstype_infrastructure	1
#define P80211ENUM_bsstype_independent		2
#define P80211ENUM_bsstype_any			3
#define P80211ENUM_authalg_opensystem		1
#define P80211ENUM_authalg_sharedkey		2
#define P80211ENUM_scantype_active		1
#define P80211ENUM_resultcode_success		1
#define P80211ENUM_resultcode_invalid_parameters	2
#define P80211ENUM_resultcode_not_supported	3
#define P80211ENUM_resultcode_refused		6
#define P80211ENUM_resultcode_cant_set_readonly_mib	10
#define P80211ENUM_resultcode_implementation_failure	11
#define P80211ENUM_resultcode_cant_get_writeonly_mib	12
#define P80211ENUM_status_successful		0
#define P80211ENUM_status_unspec_failure	1
#define P80211ENUM_status_ap_full		17
#define P80211ENUM_msgitem_status_data_ok		0
#define P80211ENUM_msgitem_status_no_value		1

/*----------------------------------------------------------------*/
/* p80211 max length constants for the different pascal strings. */

#define MAXLEN_PSTR6		(6)	/* pascal array of 6 bytes */
#define MAXLEN_PSTR14		(14)	/* pascal array of 14 bytes */
#define MAXLEN_PSTR32		(32)	/* pascal array of 32 bytes */
#define MAXLEN_PSTR255		(255)	/* pascal array of 255 bytes */
#define MAXLEN_MIBATTRIBUTE	(392)	/* maximum mibattribute */
					/* where the size of the DATA itself */
					/* is a DID-LEN-DATA triple */
					/* with a max size of 4+4+384 */

/*----------------------------------------------------------------*/
/* The following macro creates a name for an enum */

#define MKENUMNAME(name) p80211enum_ ## name

/*----------------------------------------------------------------
* The following constants and macros are used to construct and
* deconstruct the Data ID codes.  The coding is as follows:
*
*     ...rwtnnnnnnnniiiiiiggggggssssss      s - Section
*                                           g - Group
*                                           i - Item
*                                           n - Index
*                                           t - Table flag
*                                           w - Write flag
*                                           r - Read flag
*                                           . - Unused
*/

#define P80211DID_LSB_SECTION		(0)
#define P80211DID_LSB_GROUP		(6)
#define P80211DID_LSB_ITEM		(12)
#define P80211DID_LSB_INDEX		(18)
#define P80211DID_LSB_ISTABLE		(26)
#define P80211DID_LSB_ACCESS		(27)

#define P80211DID_MASK_SECTION		(0x0000003fUL)
#define P80211DID_MASK_GROUP		(0x0000003fUL)
#define P80211DID_MASK_ITEM		(0x0000003fUL)
#define P80211DID_MASK_INDEX		(0x000000ffUL)
#define P80211DID_MASK_ISTABLE		(0x00000001UL)
#define P80211DID_MASK_ACCESS		(0x00000003UL)

#define P80211DID_MK(a, m, l)	((((u32)(a)) & (m)) << (l))

#define P80211DID_MKSECTION(a)	P80211DID_MK(a, \
					P80211DID_MASK_SECTION, \
					P80211DID_LSB_SECTION)
#define P80211DID_MKGROUP(a)	P80211DID_MK(a, \
					P80211DID_MASK_GROUP, \
					P80211DID_LSB_GROUP)
#define P80211DID_MKITEM(a)	P80211DID_MK(a, \
					P80211DID_MASK_ITEM, \
					P80211DID_LSB_ITEM)
#define P80211DID_MKINDEX(a)	P80211DID_MK(a, \
					P80211DID_MASK_INDEX, \
					P80211DID_LSB_INDEX)
#define P80211DID_MKISTABLE(a)	P80211DID_MK(a, \
					P80211DID_MASK_ISTABLE, \
					P80211DID_LSB_ISTABLE)

#define P80211DID_MKID(s, g, i, n, t, a)	(P80211DID_MKSECTION(s) | \
					P80211DID_MKGROUP(g) | \
					P80211DID_MKITEM(i) | \
					P80211DID_MKINDEX(n) | \
					P80211DID_MKISTABLE(t) | \
					(a))

#define P80211DID_GET(a, m, l)	((((u32)(a)) >> (l)) & (m))

#define P80211DID_SECTION(a)	P80211DID_GET(a, \
					P80211DID_MASK_SECTION, \
					P80211DID_LSB_SECTION)
#define P80211DID_GROUP(a)	P80211DID_GET(a, \
					P80211DID_MASK_GROUP, \
					P80211DID_LSB_GROUP)
#define P80211DID_ITEM(a)	P80211DID_GET(a, \
					P80211DID_MASK_ITEM, \
					P80211DID_LSB_ITEM)
#define P80211DID_INDEX(a)	P80211DID_GET(a, \
					P80211DID_MASK_INDEX, \
					P80211DID_LSB_INDEX)
#define P80211DID_ISTABLE(a)	P80211DID_GET(a, \
					P80211DID_MASK_ISTABLE, \
					P80211DID_LSB_ISTABLE)
#define P80211DID_ACCESS(a)	P80211DID_GET(a, \
					P80211DID_MASK_ACCESS, \
					P80211DID_LSB_ACCESS)

/*----------------------------------------------------------------*/
/* The following structure types are used for the representation */
/*  of ENUMint type metadata. */

struct p80211enumpair {
	u32 val;
	char *name;
};

struct p80211enum {
	int nitems;
	struct p80211enumpair *list;
};

/*----------------------------------------------------------------*/
/* The following structure types are used to store data items in */
/*  messages. */

/* Template pascal string */
struct p80211pstr {
	u8 len;
} __packed;

struct p80211pstrd {
	u8 len;
	u8 data[0];
} __packed;

/* Maximum pascal string */
struct p80211pstr255 {
	u8 len;
	u8 data[MAXLEN_PSTR255];
} __packed;

/* pascal string for macaddress and bssid */
struct p80211pstr6 {
	u8 len;
	u8 data[MAXLEN_PSTR6];
} __packed;

/* pascal string for channel list */
struct p80211pstr14 {
	u8 len;
	u8 data[MAXLEN_PSTR14];
} __packed;

/* pascal string for ssid */
struct p80211pstr32 {
	u8 len;
	u8 data[MAXLEN_PSTR32];
} __packed;

/* MAC address array */
struct p80211macarray {
	u32 cnt;
	u8 data[1][MAXLEN_PSTR6];
} __packed;

/* prototype template */
struct p80211item {
	u32 did;
	u16 status;
	u16 len;
} __packed;

/* prototype template w/ data item */
typedef struct p80211itemd {
	u32 did;
	u16 status;
	u16 len;
	u8 data[0];
} __packed p80211itemd_t;

/* message data item for int, BOUNDEDINT, ENUMINT */
typedef struct p80211item_uint32 {
	u32 did;
	u16 status;
	u16 len;
	u32 data;
} __packed p80211item_uint32_t;

/* message data item for OCTETSTR, DISPLAYSTR */
typedef struct p80211item_pstr6 {
	u32 did;
	u16 status;
	u16 len;
	struct p80211pstr6 data;
} __packed p80211item_pstr6_t;

/* message data item for OCTETSTR, DISPLAYSTR */
typedef struct p80211item_pstr14 {
	u32 did;
	u16 status;
	u16 len;
	struct p80211pstr14 data;
} __packed p80211item_pstr14_t;

/* message data item for OCTETSTR, DISPLAYSTR */
typedef struct p80211item_pstr32 {
	u32 did;
	u16 status;
	u16 len;
	struct p80211pstr32 data;
} __packed p80211item_pstr32_t;

/* message data item for OCTETSTR, DISPLAYSTR */
typedef struct p80211item_pstr255 {
	u32 did;
	u16 status;
	u16 len;
	struct p80211pstr255 data;
} __packed p80211item_pstr255_t;

/* message data item for UNK 392, namely mib items */
typedef struct p80211item_unk392 {
	u32 did;
	u16 status;
	u16 len;
	u8 data[MAXLEN_MIBATTRIBUTE];
} __packed p80211item_unk392_t;

/* message data item for UNK 1025, namely p2 pdas */
typedef struct p80211item_unk1024 {
	u32 did;
	u16 status;
	u16 len;
	u8 data[1024];
} __packed p80211item_unk1024_t;

/* message data item for UNK 4096, namely p2 download chunks */
typedef struct p80211item_unk4096 {
	u32 did;
	u16 status;
	u16 len;
	u8 data[4096];
} __packed p80211item_unk4096_t;

struct catlistitem;

/*----------------------------------------------------------------*/
/* The following structure type is used to represent all of the */
/*  metadata items.  Some components may choose to use more, */
/*  less or different metadata items. */

typedef void (*p80211_totext_t) (struct catlistitem *, u32 did, u8 *itembuf,
				 char *textbuf);
typedef void (*p80211_fromtext_t) (struct catlistitem *, u32 did, u8 *itembuf,
				   char *textbuf);
typedef u32(*p80211_valid_t) (struct catlistitem *, u32 did, u8 *itembuf);

/*----------------------------------------------------------------*/
/* Enumeration Lists */
/*  The following are the external declarations */
/*  for all enumerations  */

extern struct p80211enum MKENUMNAME(truth);
extern struct p80211enum MKENUMNAME(ifstate);
extern struct p80211enum MKENUMNAME(powermgmt);
extern struct p80211enum MKENUMNAME(bsstype);
extern struct p80211enum MKENUMNAME(authalg);
extern struct p80211enum MKENUMNAME(phytype);
extern struct p80211enum MKENUMNAME(temptype);
extern struct p80211enum MKENUMNAME(regdomain);
extern struct p80211enum MKENUMNAME(ccamode);
extern struct p80211enum MKENUMNAME(diversity);
extern struct p80211enum MKENUMNAME(scantype);
extern struct p80211enum MKENUMNAME(resultcode);
extern struct p80211enum MKENUMNAME(reason);
extern struct p80211enum MKENUMNAME(status);
extern struct p80211enum MKENUMNAME(msgcode);
extern struct p80211enum MKENUMNAME(msgitem_status);

extern struct p80211enum MKENUMNAME(lnxroam_reason);

extern struct p80211enum MKENUMNAME(p2preamble);

#endif /* _P80211TYPES_H */
