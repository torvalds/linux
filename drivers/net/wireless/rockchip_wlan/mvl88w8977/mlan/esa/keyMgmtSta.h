/** @file keyMgmtSta.h
 *
 *  @brief This file contains the defines for key management.
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
#ifndef _KEY_MGMT_STA_H_
#define _KEY_MGMT_STA_H_

#include "keyCommonDef.h"
#include "KeyApiStaDefs.h"
#include "IEEE_types.h"
#include "keyMgmtStaTypes.h"
#include "keyMgmtSta_rom.h"

#ifdef BTAMP
#include "btamp_config.h"
#define BTAMP_SUPPLICATNT_SESSIONS  AMPHCI_MAX_PHYSICAL_LINK_SUPPORTED
#else
#define BTAMP_SUPPLICATNT_SESSIONS  0
#endif

//longl test
#define MAX_SUPPLICANT_SESSIONS     (10)

void keyMgmtControlledPortOpen(phostsa_private priv);

extern BOOLEAN supplicantAkmIsWpaWpa2(phostsa_private priv, AkmSuite_t *pAkm);
extern BOOLEAN supplicantAkmIsWpaWpa2Psk(phostsa_private priv,
					 AkmSuite_t *pAkm);
extern BOOLEAN supplicantAkmUsesKdf(phostsa_private priv, AkmSuite_t *pAkm);
extern BOOLEAN supplicantGetPmkid(phostsa_private priv,
				  IEEEtypes_MacAddr_t *pBssid,
				  IEEEtypes_MacAddr_t *pSta, AkmSuite_t *pAkm,
				  UINT8 *pPMKID);

extern void keyMgmtSetIGtk(phostsa_private priv,
			   keyMgmtInfoSta_t *pKeyMgmtInfoSta,
			   IGtkKde_t *pIGtkKde, UINT8 iGtkKdeLen);

extern UINT8 *keyMgmtGetIGtk(phostsa_private priv);

extern void keyMgmtSta_RomInit(void);

#if 0
extern BufferDesc_t *GetTxEAPOLBuffer(struct cm_ConnectionInfo *connPtr,
				      EAPOL_KeyMsg_Tx_t **ppTxEapol,
				      BufferDesc_t * pBufDesc);
#endif
extern void allocSupplicantData(void *priv);
extern void freeSupplicantData(void *priv);
extern void supplicantInit(void *priv, supplicantData_t *suppData);
extern BOOLEAN keyMgmtProcessMsgExt(phostsa_private priv,
				    keyMgmtInfoSta_t *pKeyMgmtInfoSta,
				    EAPOL_KeyMsg_t *pKeyMsg);

extern void ProcessKeyMgmtDataSta(phostsa_private priv, mlan_buffer *pmbuf,
				  IEEEtypes_MacAddr_t *sa,
				  IEEEtypes_MacAddr_t *da);

#endif
