/** @file keyMgmtAp_rom.h
 *
 *  @brief This file contains define for key management
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
#ifndef KEYMGMTAP_ROM_H__
#define KEYMGMTAP_ROM_H__
//Authenticator related data structures, function prototypes

#include "wltypes.h"
#include "IEEE_types.h"
#include "sha1.h"
#include "keyMgmtStaTypes.h"
#include "wl_macros.h"
#include "keyMgmtApTypes.h"
#include "rc4_rom.h"

#include "keyCommonDef.h"
#include "authenticator.h"
#include "keyMgmtApStaCommon.h"

/* This flags if the Secure flag in EAPOL key frame must be set */
#define SECURE_HANDSHAKE_FLAG 0x0080
/* Whether the EAPOL frame is for pairwise or group Key */
#define PAIRWISE_KEY_MSG    0x0800
/* Flags when WPA2 is enabled, not used for WPA */
#define WPA2_HANDSHAKE      0x8000

extern void GenerateGTK_internal(hostsa_private *priv, KeyData_t *grpKeyData,
				 UINT8 *nonce, UINT8 *StaMacAddr);

extern void PopulateKeyMsg(hostsa_private *priv,
			   EAPOL_KeyMsg_Tx_t *tx_eapol_ptr, Cipher_t *Cipher,
			   UINT16 Type, UINT32 replay_cnt[2], UINT8 *Nonce);

extern void prepareKDE(hostsa_private *priv, EAPOL_KeyMsg_Tx_t *tx_eapol_ptr,
		       KeyData_t *grKey, Cipher_t *cipher);

extern BOOLEAN Encrypt_keyData(hostsa_private *priv,
			       EAPOL_KeyMsg_Tx_t *tx_eapol_ptr,
			       UINT8 *EAPOL_Encr_Key, Cipher_t *cipher);

extern void ROM_InitGTK(hostsa_private *priv, KeyData_t *grpKeyData,
			UINT8 *nonce, UINT8 *StaMacAddr);

extern void KeyData_AddGTK(hostsa_private *priv, EAPOL_KeyMsg_Tx_t *pTxEAPOL,
			   KeyData_t *grKey, Cipher_t *cipher);

extern BOOLEAN KeyData_AddKey(hostsa_private *priv, EAPOL_KeyMsg_Tx_t *pTxEAPOL,
			      SecurityMode_t *pSecType,
			      KeyData_t *grKey, Cipher_t *cipher);

extern BOOLEAN KeyData_CopyWPAWP2(hostsa_private *priv,
				  EAPOL_KeyMsg_Tx_t *pTxEAPOL, void *pIe);

extern BOOLEAN KeyData_UpdateKeyMaterial(hostsa_private *priv,
					 EAPOL_KeyMsg_Tx_t *pTxEAPOL,
					 SecurityMode_t *pSecType, void *pWPA,
					 void *pWPA2);
extern void KeyMgmtAp_DerivePTK(hostsa_private *priv, UINT8 *pPMK, t_u8 *da,
				t_u8 *sa, UINT8 *ANonce, UINT8 *SNonce,
				UINT8 *EAPOL_MIC_Key, UINT8 *EAPOL_Encr_Key,
				KeyData_t *newPWKey, BOOLEAN use_kdf);

#endif //_KEYMGMTAP_ROM_H_
