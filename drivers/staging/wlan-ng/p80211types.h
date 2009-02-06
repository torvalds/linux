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

/*================================================================*/
/* System Includes */
/*================================================================*/

/*================================================================*/
/* Project Includes */
/*================================================================*/

#ifndef _WLAN_COMPAT_H
#include "wlan_compat.h"
#endif

/*================================================================*/
/* Constants */
/*================================================================*/

/*----------------------------------------------------------------*/
/* p80211 data type codes used for MIB items and message */
/* arguments. The various metadata structures provide additional */
/* information about these types. */

#define P80211_TYPE_OCTETSTR		1	/* pascal array of bytes */
#define P80211_TYPE_DISPLAYSTR		2	/* pascal array of bytes containing ascii */
#define P80211_TYPE_int			4	/* u32 min and max limited by 32 bits */
#define P80211_TYPE_ENUMint		5	/* u32 holding a numeric
						   code that can be mapped
						   to a textual name */
#define P80211_TYPE_UNKDATA		6	/* Data item containing an
						   unknown data type */
#define P80211_TYPE_intARRAY		7	/* Array of 32-bit integers. */
#define P80211_TYPE_BITARRAY		8	/* Array of bits. */
#define P80211_TYPE_MACARRAY		9	/* Array of MAC addresses. */

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
/* #define P80211_MSG_CAT_DOT11CFM		3 (doesn't exist at this time) */

#define P80211SEC_DOT11REQ		P80211_MSG_CAT_DOT11REQ
#define P80211SEC_DOT11IND		P80211_MSG_CAT_DOT11IND
/* #define P80211SEC_DOT11CFM		P80211_MSG_CAT_DOT11CFM  (doesn't exist at this time */



/*----------------------------------------------------------------*/
/* p80211 DID field codes that represent access type and */
/* is_table status. */

#define P80211DID_ACCESS_READ		0x10000000
#define P80211DID_ACCESS_WRITE		0x08000000
#define P80211DID_WRITEONLY		0x00000001
#define P80211DID_READONLY		0x00000002
#define P80211DID_READWRITE		0x00000003
#define P80211DID_ISTABLE_FALSE		0
#define P80211DID_ISTABLE_TRUE		1

/*----------------------------------------------------------------*/
/* p80211 enumeration constants.  The value to text mappings for */
/*  these is in p80211types.c.  These defines were generated */
/*  from the mappings. */

/* error codes for lookups */
#define P80211ENUM_BAD				0xffffffffUL
#define P80211ENUM_BADSTR			"P80211ENUM_BAD"

#define P80211ENUM_truth_false			0
#define P80211ENUM_truth_true			1
#define P80211ENUM_ifstate_disable		0
#define P80211ENUM_ifstate_fwload		1
#define P80211ENUM_ifstate_enable		2
#define P80211ENUM_powermgmt_active		1
#define P80211ENUM_powermgmt_powersave		2
#define P80211ENUM_bsstype_infrastructure	1
#define P80211ENUM_bsstype_independent		2
#define P80211ENUM_bsstype_any			3
#define P80211ENUM_authalg_opensystem		1
#define P80211ENUM_authalg_sharedkey		2
#define P80211ENUM_phytype_fhss			1
#define P80211ENUM_phytype_dsss			2
#define P80211ENUM_phytype_irbaseband		3
#define P80211ENUM_temptype_commercial		1
#define P80211ENUM_temptype_industrial		2
#define P80211ENUM_regdomain_fcc		16
#define P80211ENUM_regdomain_doc		32
#define P80211ENUM_regdomain_etsi		48
#define P80211ENUM_regdomain_spain		49
#define P80211ENUM_regdomain_france		50
#define P80211ENUM_regdomain_mkk		64
#define P80211ENUM_ccamode_edonly		1
#define P80211ENUM_ccamode_csonly		2
#define P80211ENUM_ccamode_edandcs		4
#define P80211ENUM_ccamode_cswithtimer		8
#define P80211ENUM_ccamode_hrcsanded		16
#define P80211ENUM_diversity_fixedlist		1
#define P80211ENUM_diversity_notsupported	2
#define P80211ENUM_diversity_dynamic		3
#define P80211ENUM_scantype_active		1
#define P80211ENUM_scantype_passive		2
#define P80211ENUM_scantype_both		3
#define P80211ENUM_resultcode_success		1
#define P80211ENUM_resultcode_invalid_parameters	2
#define P80211ENUM_resultcode_not_supported	3
#define P80211ENUM_resultcode_timeout		4
#define P80211ENUM_resultcode_too_many_req	5
#define P80211ENUM_resultcode_refused		6
#define P80211ENUM_resultcode_bss_already	7
#define P80211ENUM_resultcode_invalid_access	8
#define P80211ENUM_resultcode_invalid_mibattribute	9
#define P80211ENUM_resultcode_cant_set_readonly_mib	10
#define P80211ENUM_resultcode_implementation_failure	11
#define P80211ENUM_resultcode_cant_get_writeonly_mib	12
#define P80211ENUM_reason_unspec_reason		1
#define P80211ENUM_reason_auth_not_valid	2
#define P80211ENUM_reason_deauth_lv_ss		3
#define P80211ENUM_reason_inactivity		4
#define P80211ENUM_reason_ap_overload		5
#define P80211ENUM_reason_class23_err		6
#define P80211ENUM_reason_class3_err		7
#define P80211ENUM_reason_disas_lv_ss		8
#define P80211ENUM_reason_asoc_not_auth		9
#define P80211ENUM_status_successful		0
#define P80211ENUM_status_unspec_failure	1
#define P80211ENUM_status_unsup_cap		10
#define P80211ENUM_status_reasoc_no_asoc	11
#define P80211ENUM_status_fail_other		12
#define P80211ENUM_status_unspt_alg		13
#define P80211ENUM_status_auth_seq_fail		14
#define P80211ENUM_status_chlng_fail		15
#define P80211ENUM_status_auth_timeout		16
#define P80211ENUM_status_ap_full		17
#define P80211ENUM_status_unsup_rate		18
#define P80211ENUM_status_unsup_shortpreamble	19
#define P80211ENUM_status_unsup_pbcc		20
#define P80211ENUM_status_unsup_agility		21
#define P80211ENUM_msgitem_status_data_ok		0
#define P80211ENUM_msgitem_status_no_value		1
#define P80211ENUM_msgitem_status_invalid_itemname	2
#define P80211ENUM_msgitem_status_invalid_itemdata	3
#define P80211ENUM_msgitem_status_missing_itemdata	4
#define P80211ENUM_msgitem_status_incomplete_itemdata	5
#define P80211ENUM_msgitem_status_invalid_msg_did	6
#define P80211ENUM_msgitem_status_invalid_mib_did	7
#define P80211ENUM_msgitem_status_missing_conv_func	8
#define P80211ENUM_msgitem_status_string_too_long	9
#define P80211ENUM_msgitem_status_data_out_of_range	10
#define P80211ENUM_msgitem_status_string_too_short	11
#define P80211ENUM_msgitem_status_missing_valid_func	12
#define P80211ENUM_msgitem_status_unknown		13
#define P80211ENUM_msgitem_status_invalid_did		14
#define P80211ENUM_msgitem_status_missing_print_func	15

#define P80211ENUM_lnxroam_reason_unknown        0
#define P80211ENUM_lnxroam_reason_beacon         1
#define P80211ENUM_lnxroam_reason_signal         2
#define P80211ENUM_lnxroam_reason_txretry        3
#define P80211ENUM_lnxroam_reason_notjoined      4

#define P80211ENUM_p2preamble_long               0
#define P80211ENUM_p2preamble_short              2
#define P80211ENUM_p2preamble_mixed              3

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

#define P80211_SET_int(item, value) do { \
	(item).data   = (value); \
	(item).status = P80211ENUM_msgitem_status_data_ok; \
	} while(0)
/*----------------------------------------------------------------*/
/* string constants */

#define NOT_SET			"NOT_SET"
#define NOT_SUPPORTED		"NOT_SUPPORTED"
#define UNKNOWN_DATA		"UNKNOWN_DATA"


/*--------------------------------------------------------------------*/
/*  Metadata flags  */

/* MSM: Do these belong in p80211meta.h? I'm not sure. */

#define ISREQUIRED		(0x80000000UL)
#define ISREQUEST		(0x40000000UL)
#define ISCONFIRM		(0x20000000UL)


/*================================================================*/
/* Macros */

/*--------------------------------------------------------------------*/
/* The following macros are used to manipulate the 'flags' field in   */
/*  the metadata.  These are only used when the metadata is for       */
/*  command arguments to determine if the data item is required, and  */
/*  whether the metadata item is for a request command, confirm       */
/*  command or both.                                                  */
/*--------------------------------------------------------------------*/
/* MSM: Do these belong in p80211meta.h?  I'm not sure */

#define P80211ITEM_SETFLAGS(q, r, c)	( q | r | c )

#define P80211ITEM_ISREQUIRED(flags)	(((u32)(flags & ISREQUIRED)) >> 31 )
#define P80211ITEM_ISREQUEST(flags)	(((u32)(flags & ISREQUEST)) >> 30 )
#define P80211ITEM_ISCONFIRM(flags)	(((u32)(flags & ISCONFIRM)) >> 29 )

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

#define P80211DID_INVALID		0xffffffffUL
#define P80211DID_VALID			0x00000000UL

#define P80211DID_LSB_SECTION		(0)
#define P80211DID_LSB_GROUP		(6)
#define P80211DID_LSB_ITEM		(12)
#define P80211DID_LSB_INDEX		(18)
#define P80211DID_LSB_ISTABLE		(26)
#define P80211DID_LSB_ACCESS 		(27)

#define P80211DID_MASK_SECTION		(0x0000003fUL)
#define P80211DID_MASK_GROUP		(0x0000003fUL)
#define P80211DID_MASK_ITEM		(0x0000003fUL)
#define P80211DID_MASK_INDEX		(0x000000ffUL)
#define P80211DID_MASK_ISTABLE		(0x00000001UL)
#define P80211DID_MASK_ACCESS 		(0x00000003UL)


#define P80211DID_MK(a,m,l)	((((u32)(a)) & (m)) << (l))

#define P80211DID_MKSECTION(a)	P80211DID_MK(a, \
					P80211DID_MASK_SECTION, \
					P80211DID_LSB_SECTION )
#define P80211DID_MKGROUP(a)	P80211DID_MK(a, \
					P80211DID_MASK_GROUP, \
					P80211DID_LSB_GROUP )
#define P80211DID_MKITEM(a)	P80211DID_MK(a, \
					P80211DID_MASK_ITEM, \
					P80211DID_LSB_ITEM )
#define P80211DID_MKINDEX(a)	P80211DID_MK(a, \
					P80211DID_MASK_INDEX, \
					P80211DID_LSB_INDEX )
#define P80211DID_MKISTABLE(a)	P80211DID_MK(a, \
					P80211DID_MASK_ISTABLE, \
					P80211DID_LSB_ISTABLE )


#define P80211DID_MKID(s,g,i,n,t,a)	(P80211DID_MKSECTION(s) | \
						P80211DID_MKGROUP(g) | \
				 		P80211DID_MKITEM(i) | \
				 		P80211DID_MKINDEX(n) | \
						P80211DID_MKISTABLE(t) | \
						(a) )


#define P80211DID_GET(a,m,l)	((((u32)(a)) >> (l)) & (m))

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

/*================================================================*/
/* Types */

/*----------------------------------------------------------------*/
/* The following structure types are used for the represenation */
/*  of ENUMint type metadata. */

typedef struct p80211enumpair
{
	u32			val;
	char			*name;
} p80211enumpair_t;

typedef struct p80211enum
{
	int			nitems;
	p80211enumpair_t	*list;
} p80211enum_t;

/*----------------------------------------------------------------*/
/* The following structure types are used to store data items in */
/*  messages. */

/* Template pascal string */
typedef struct p80211pstr
{
	u8		len;
} __WLAN_ATTRIB_PACK__ p80211pstr_t;

typedef struct p80211pstrd
{
	u8		len;
	u8		data[0];
} __WLAN_ATTRIB_PACK__ p80211pstrd_t;

/* Maximum pascal string */
typedef struct p80211pstr255
{
	u8		len;
	u8		data[MAXLEN_PSTR255];
} __WLAN_ATTRIB_PACK__ p80211pstr255_t;

/* pascal string for macaddress and bssid */
typedef struct p80211pstr6
{
	u8		len;
	u8		data[MAXLEN_PSTR6];
} __WLAN_ATTRIB_PACK__ p80211pstr6_t;

/* pascal string for channel list */
typedef struct p80211pstr14
{
	u8		len;
	u8		data[MAXLEN_PSTR14];
} __WLAN_ATTRIB_PACK__ p80211pstr14_t;

/* pascal string for ssid */
typedef struct p80211pstr32
{
	u8		len;
	u8		data[MAXLEN_PSTR32];
} __WLAN_ATTRIB_PACK__ p80211pstr32_t;

/* MAC address array */
typedef struct p80211macarray
{
	u32		cnt;
	u8		data[1][MAXLEN_PSTR6];
} __WLAN_ATTRIB_PACK__ p80211macarray_t;

/* prototype template */
typedef struct p80211item
{
	u32		did;
	u16		status;
	u16		len;
} __WLAN_ATTRIB_PACK__ p80211item_t;

/* prototype template w/ data item */
typedef struct p80211itemd
{
	u32		did;
	u16		status;
	u16		len;
	u8		data[0];
} __WLAN_ATTRIB_PACK__ p80211itemd_t;

/* message data item for int, BOUNDEDINT, ENUMINT */
typedef struct p80211item_uint32
{
	u32		did;
	u16		status;
	u16		len;
	u32		data;
} __WLAN_ATTRIB_PACK__ p80211item_uint32_t;

/* message data item for OCTETSTR, DISPLAYSTR */
typedef struct p80211item_pstr6
{
	u32		did;
	u16		status;
	u16		len;
	p80211pstr6_t	data;
} __WLAN_ATTRIB_PACK__ p80211item_pstr6_t;

/* message data item for OCTETSTR, DISPLAYSTR */
typedef struct p80211item_pstr14
{
	u32			did;
	u16			status;
	u16			len;
	p80211pstr14_t		data;
} __WLAN_ATTRIB_PACK__ p80211item_pstr14_t;

/* message data item for OCTETSTR, DISPLAYSTR */
typedef struct p80211item_pstr32
{
	u32			did;
	u16			status;
	u16			len;
	p80211pstr32_t		data;
} __WLAN_ATTRIB_PACK__ p80211item_pstr32_t;

/* message data item for OCTETSTR, DISPLAYSTR */
typedef struct p80211item_pstr255
{
	u32			did;
	u16			status;
	u16			len;
	p80211pstr255_t		data;
} __WLAN_ATTRIB_PACK__ p80211item_pstr255_t;

/* message data item for UNK 392, namely mib items */
typedef struct  p80211item_unk392
{
	u32		did;
	u16		status;
	u16		len;
	u8		data[MAXLEN_MIBATTRIBUTE];
} __WLAN_ATTRIB_PACK__ p80211item_unk392_t;

/* message data item for UNK 1025, namely p2 pdas */
typedef struct  p80211item_unk1024
{
	u32		did;
	u16		status;
	u16		len;
	u8		data[1024];
}  __WLAN_ATTRIB_PACK__ p80211item_unk1024_t;

/* message data item for UNK 4096, namely p2 download chunks */
typedef struct  p80211item_unk4096
{
	u32		did;
	u16		status;
	u16		len;
	u8		data[4096];
}  __WLAN_ATTRIB_PACK__ p80211item_unk4096_t;

struct catlistitem;

/*----------------------------------------------------------------*/
/* The following structure type is used to represent all of the */
/*  metadata items.  Some components may choose to use more, */
/*  less or different metadata items. */

typedef void (*p80211_totext_t)( struct catlistitem *, u32 did, u8* itembuf, char *textbuf);
typedef void (*p80211_fromtext_t)( struct catlistitem *, u32 did, u8* itembuf, char *textbuf);
typedef u32 (*p80211_valid_t)( struct catlistitem *, u32 did, u8* itembuf);


/*================================================================*/
/* Extern Declarations */

/*----------------------------------------------------------------*/
/* Enumeration Lists */
/*  The following are the external declarations */
/*  for all enumerations  */

extern p80211enum_t MKENUMNAME(truth);
extern p80211enum_t MKENUMNAME(ifstate);
extern p80211enum_t MKENUMNAME(powermgmt);
extern p80211enum_t MKENUMNAME(bsstype);
extern p80211enum_t MKENUMNAME(authalg);
extern p80211enum_t MKENUMNAME(phytype);
extern p80211enum_t MKENUMNAME(temptype);
extern p80211enum_t MKENUMNAME(regdomain);
extern p80211enum_t MKENUMNAME(ccamode);
extern p80211enum_t MKENUMNAME(diversity);
extern p80211enum_t MKENUMNAME(scantype);
extern p80211enum_t MKENUMNAME(resultcode);
extern p80211enum_t MKENUMNAME(reason);
extern p80211enum_t MKENUMNAME(status);
extern p80211enum_t MKENUMNAME(msgcode);
extern p80211enum_t MKENUMNAME(msgitem_status);

extern p80211enum_t MKENUMNAME(lnxroam_reason);

extern p80211enum_t MKENUMNAME(p2preamble);

/*================================================================*/
/* Function Declarations */

/*----------------------------------------------------------------*/
/* The following declare some utility functions for use with the */
/*  p80211enum_t type. */

u32 p80211enum_text2int(p80211enum_t *ep, char *text);
u32 p80211enum_int2text(p80211enum_t *ep, u32 val, char *text);
void p80211_error2text(int err_code, char *err_str);

/*----------------------------------------------------------------*/
/* The following declare some utility functions for use with the */
/*  p80211item_t and p80211meta_t types. */

/*----------------------------------------------------------------*/
/* The following declare functions that perform validation and    */
/* text to binary conversions based on the metadata for interface */
/* and MIB data items.                                            */
/*----------------------------------------------------------------*/

/*-- DISPLAYSTR ------------------------------------------------------*/
/* pstr ==> cstr */
void p80211_totext_displaystr( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* cstr ==> pstr */
void p80211_fromtext_displaystr( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* function that checks validity of a displaystr binary value */
u32 p80211_isvalid_displaystr( struct catlistitem *metalist, u32 did, u8 *itembuf );

/*-- OCTETSTR --------------------------------------------------------*/
/* pstr ==> "xx:xx:...." */
void p80211_totext_octetstr( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* "xx:xx:...." ==> pstr */
void p80211_fromtext_octetstr( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* function that checks validity of an octetstr binary value */
u32 p80211_isvalid_octetstr( struct catlistitem *metalist, u32 did, u8 *itembuf );

/*-- int -------------------------------------------------------------*/
/* u32 ==> %d */
void p80211_totext_int( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* %d ==> u32 */
void p80211_fromtext_int( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* function that checks validity of an int's binary value (always successful) */
u32 p80211_isvalid_int( struct catlistitem *metalist, u32 did, u8 *itembuf );

/*-- ENUMint ---------------------------------------------------------*/
/* u32 ==> <valuename> */
void p80211_totext_enumint( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* <valuename> ==> u32 */
void p80211_fromtext_enumint( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* function that checks validity of an enum's binary value */
u32 p80211_isvalid_enumint( struct catlistitem *metalist, u32 did, u8 *itembuf );

/*-- intARRAY --------------------------------------------------------*/
/* u32[] => %d,%d,%d,... */
void p80211_totext_intarray( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* %d,%d,%d,... ==> u32[] */
void p80211_fromtext_intarray( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* function that checks validity of an integer array's value */
u32 p80211_isvalid_intarray( struct catlistitem *metalist, u32 did, u8 *itembuf );

/*-- BITARRAY --------------------------------------------------------*/
/* u32 ==> %d,%d,%d,... */
void p80211_totext_bitarray( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* %d,%d,%d,... ==> u32 */
void p80211_fromtext_bitarray( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* function that checks validity of a bit array's value */
u32 p80211_isvalid_bitarray( struct catlistitem *metalist, u32 did, u8 *itembuf );

/*-- MACARRAY --------------------------------------------------------*/
void p80211_totext_macarray( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

void p80211_fromtext_macarray( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* function that checks validity of a MAC address array's value */
u32 p80211_isvalid_macarray( struct catlistitem *metalist, u32 did, u8 *itembuf );

/*-- MIBATTRIUBTE ------------------------------------------------------*/
/* <mibvalue> ==> <textual representation identified in MIB metadata> */
void p80211_totext_getmibattribute( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );
void p80211_totext_setmibattribute( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );


/* <textual representation identified in MIB metadata> ==> <mibvalue> */
void p80211_fromtext_getmibattribute( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );
void p80211_fromtext_setmibattribute( struct catlistitem *metalist, u32 did, u8 *itembuf, char *textbuf );

/* function that checks validity of a mibitem's binary value */
u32 p80211_isvalid_getmibattribute( struct catlistitem *metalist, u32 did, u8 *itembuf );
u32 p80211_isvalid_setmibattribute( struct catlistitem *metalist, u32 did, u8 *itembuf );

#endif /* _P80211TYPES_H */

