/* p80211msg.h
*
* Macros, constants, types, and funcs for req and ind messages
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

#ifndef _P80211MSG_H
#define _P80211MSG_H

/*================================================================*/
/* System Includes */

/*================================================================*/
/* Project Includes */

#ifndef _WLAN_COMPAT_H
#include "wlan_compat.h"
#endif

/*================================================================*/
/* Constants */

#define MSG_BUFF_LEN		4000
#define WLAN_DEVNAMELEN_MAX	16

/*================================================================*/
/* Macros */

/*================================================================*/
/* Types */

/*--------------------------------------------------------------------*/
/*----- Message Structure Types --------------------------------------*/

/*--------------------------------------------------------------------*/
/* Prototype msg type */

typedef struct p80211msg
{
	u32	msgcode;
	u32	msglen;
	u8	devname[WLAN_DEVNAMELEN_MAX];
} __WLAN_ATTRIB_PACK__ p80211msg_t;

typedef struct p80211msgd
{
	u32	msgcode;
	u32	msglen;
	u8	devname[WLAN_DEVNAMELEN_MAX];
	u8	args[0];
} __WLAN_ATTRIB_PACK__ p80211msgd_t;

/*================================================================*/
/* Extern Declarations */


/*================================================================*/
/* Function Declarations */

#endif  /* _P80211MSG_H */

