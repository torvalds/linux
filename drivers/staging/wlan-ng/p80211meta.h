/* p80211meta.h
*
* Macros, constants, types, and funcs for p80211 metadata
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

#ifndef _P80211META_H
#define _P80211META_H

/*================================================================*/
/* Project Includes */

/*================================================================*/
/* Types */

/*----------------------------------------------------------------*/
/* The following structure types are used for the metadata */
/* representation of category list metadata, group list metadata, */
/* and data item metadata for both Mib and Messages. */

typedef struct p80211meta {
	char *name;		/* data item name */
	u32 did;		/* partial did */
	u32 flags;		/* set of various flag bits */
	u32 min;		/* min value of a BOUNDEDint */
	u32 max;		/* max value of a BOUNDEDint */

	u32 maxlen;		/* maxlen of a OCTETSTR or DISPLAYSTR */
	u32 minlen;		/* minlen of a OCTETSTR or DISPLAYSTR */
	p80211enum_t *enumptr;	/* ptr to the enum type for ENUMint */
	p80211_totext_t totextptr;	/* ptr to totext conversion function */
	p80211_fromtext_t fromtextptr;	/* ptr to totext conversion function */
	p80211_valid_t validfunptr;	/* ptr to totext conversion function */
} p80211meta_t;

typedef struct grplistitem {
	char *name;
	p80211meta_t *itemlist;
} grplistitem_t;

typedef struct catlistitem {
	char *name;
	grplistitem_t *grplist;
} catlistitem_t;

#endif /* _P80211META_H */
