/** @file AssocAp_src_rom.h
 *
 *  @brief This file contains define check rsn/wpa ie
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
#ifndef ASSOCAP_SRV_ROM_H_
#define ASSOCAP_SRV_ROM_H_

#include "wltypes.h"
#include "IEEE_types.h"
#include "keyMgmtStaTypes.h"
#include "keyMgmtApTypes_rom.h"
#include "authenticator.h"

WL_STATUS assocSrvAp_checkRsnWpa(cm_Connection *connPtr,
				 apKeyMgmtInfoStaRom_t *pKeyMgmtInfo,
				 Cipher_t apWpaCipher,
				 Cipher_t apWpa2Cipher,
				 Cipher_t apMcstCipher,
				 UINT16 apAuthKey,
				 SecurityMode_t *pSecType,
				 IEEEtypes_RSNElement_t *pRsn,
				 IEEEtypes_WPAElement_t *pWpa,
				 BOOLEAN validate4WayHandshakeIE);

SINT32 assocSrvAp_CheckSecurity(cm_Connection *connPtr,
				IEEEtypes_WPSElement_t *pWps,
				IEEEtypes_RSNElement_t *pRsn,
				IEEEtypes_WPAElement_t *pWpa,
				IEEEtypes_WAPIElement_t *pWapi,
				IEEEtypes_StatusCode_t *pResult);
#endif
