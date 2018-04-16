/** @file crypt_new.h
 *
 *  @brief This file contains define for rc4 decrypt/encrypt
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
/*
 * AES API header file
 *
 */

#ifndef _CRYPT_NEW_H_
#define _CRYPT_NEW_H_

#include "crypt_new_rom.h"

/* forward decl */
typedef void (*MRVL_ENDECRYPT_FUNC_p) (MRVL_ENDECRYPT_t *enDeCrypt, int *pErr);
#if 0
#if (HW_IP_AEU_VERSION < 100000)

#ifdef WAPI_HW_SUPPORT
extern void MRVL_WapiEncrypt(MRVL_ENDECRYPT_t *crypt, int *pErr);
extern void MRVL_WapiDecrypt(MRVL_ENDECRYPT_t *crypt, int *pErr);

#define MRVL_WAPI_ENCRYPT          MRVL_WapiEncrypt
#define MRVL_WAPI_DECRYPT          MRVL_WapiDecrypt
#endif

#ifdef DIAG_AES_CCM
#define MRVL_AES_CCM_ENCRYPT      MRVL_AesCCMEncrypt
#define MRVL_AES_CCM_DECRYPT      MRVL_AesCCMDecrypt
#endif

#endif /*(HW_IP_AEU_VERSION < 100000) */
#endif
#define MRVL_AES_PRIMITIVE_ENCRYPT MRVL_AesPrimitiveEncrypt
#define MRVL_AES_PRIMITIVE_DECRYPT MRVL_AesPrimitiveDecrypt
#define MRVL_AES_WRAP_ENCRYPT      MRVL_AesWrapEncrypt
#define MRVL_AES_WRAP_DECRYPT      MRVL_AesWrapDecrypt

#ifdef RC4

#define MRVL_RC4_ENCRYPT       MRVL_Rc4Cryption
#define MRVL_RC4_DECRYPT       MRVL_Rc4Cryption

#endif /* RC4 */

typedef struct {
	MRVL_ENDECRYPT_e action;
	MRVL_ENDECRYPT_FUNC_p pActionFunc;

} MRVL_ENDECRYPT_SETUP_t;

extern MRVL_ENDECRYPT_SETUP_t mrvlEnDecryptSetup[2][6];

extern void cryptNewRomInit(void);

#endif /* _CRYPT_NEW_H_ */
