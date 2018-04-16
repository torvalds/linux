/** @file KeyMgmtApTypes.h
 *
 *  @brief This file contains the key management type for ap
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
#ifndef _KEYMGMTAPTYPES_H_
#define _KEYMGMTAPTYPES_H_

#include "wltypes.h"
#include "IEEE_types.h"
#include "keyMgmtStaTypes.h"
#include "keyMgmtApTypes_rom.h"
#include "keyCommonDef.h"

typedef enum {
	STA_ASSO_EVT,
	MSGRECVD_EVT,
	KEYMGMTTIMEOUT_EVT,
	GRPKEYTIMEOUT_EVT,
	UPDATEKEYS_EVT
} keyMgmt_HskEvent_e;

/* Fields till keyMgmtState are being accessed in rom code and
  * should be kept intact. Fields after keyMgmtState can be changed
  * safely.
  */
typedef struct {
	apKeyMgmtInfoStaRom_t rom;
	UINT8 numHskTries;
	UINT32 counterLo;
	UINT32 counterHi;
	UINT8 EAPOL_MIC_Key[EAPOL_MIC_KEY_SIZE];
	UINT8 EAPOL_Encr_Key[EAPOL_ENCR_KEY_SIZE];
	UINT8 EAPOLProtoVersion;
	UINT8 rsvd[3];
} apKeyMgmtInfoSta_t;

/*  Convert an Ascii character to a hex nibble
    e.g. Input is 'b' : Output will be 0xb
         Input is 'E' : Output will be 0xE
         Input is '8' : Output will be 0x8
    Assumption is that input is a-f or A-F or 0-9
*/
#define ASCII2HEX(Asc) (((Asc) >= 'a') ? (Asc - 'a' + 0xA)\
    : ( (Asc) >= 'A' ? ( (Asc) - 'A' + 0xA ) : ((Asc) - '0') ))

#define ETH_P_EAPOL 0x8E88

#endif //_KEYMGMTAPTYPES_H_
