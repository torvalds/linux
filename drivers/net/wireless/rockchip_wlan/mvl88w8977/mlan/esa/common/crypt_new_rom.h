/** @file crypt_new_rom.c
 *
 *  @brief This file contains the api for AES based functions
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
#ifndef _CRYPT_NEW_ROM_H_
#define _CRYPT_NEW_ROM_H_

#define MRVL_AES_CMD_REQUEST_TYPE_INVALID (1)
#define MRVL_AES_ALGORITHM_INVALID        (1<<1)
#define MRVL_AES_KEY_SIZE_INVALID         (1<<2)
#define MRVL_AES_KEY_IV_SIZE_INVALID      (1<<3)
#define MRVL_AES_ENCRYPT_DATA_OVER_127    (1<<4)
#define MRVL_AES_ENCRYPT_DATA_LESS_2      (1<<5)
#define MRVL_AES_NOT_EN_AND_DECRYPT       (1<<6)
#define MRVL_AES_KEY_IV_INVALID_AES       (1<<7)
#define MRVL_AES_KEY_IV_INVALID_AES_WRAP  (1<<8)
#define MRVL_AES_KEY_IV_INVALID_RC4       (1<<9)
#define MRVL_AES_DATA_SIZE_INVALID        (1<<10)

#define MRVL_AES_NONCE_INVALID            (1<<11)
#define MRVL_AES_AAD_INVALID              (1<<12)

#define host_AES_ENCRYPT  0x1
#define host_AES_DECRYPT  0x0

#define MRVL_CRYPTO_TEST_RC4        1
#define MRVL_CRYPTO_TEST_AES_ECB    2
#define MRVL_CRYPTO_TEST_AES_WRAP   3
#define MRVL_CRYPTO_TEST_AEC_CCM    4
#define MRVL_CRYPTO_TEST_WAPI       5

/* basic data structs to support AES feature */
/* enum for encrypt/decrypt */
typedef enum {
	CRYPT_DECRYPT = 0,
	CRYPT_ENCRYPT = 1,
	CRYPT_UNKNOWN
} MRVL_ENDECRYPT_e;

/* data strcut to hold action type and data to be processed */
typedef struct {
	UINT8 enDeAction;	/* encrypt or decrypt */
	UINT8 *pData;
} MRVL_ENDECRYPT_t;

#ifdef WAPI_HW_SUPPORT
extern void MRVL_WapiEncrypt(MRVL_ENDECRYPT_t *crypt, int *pErr);
extern void MRVL_WapiDecrypt(MRVL_ENDECRYPT_t *crypt, int *pErr);
#endif

extern BOOLEAN (*MRVL_AesPrimitiveEncrypt_hook) (MRVL_ENDECRYPT_t *crypt,
						 int *pErr);
extern void MRVL_AesPrimitiveEncrypt(MRVL_ENDECRYPT_t *crypt, int *pErr);

extern BOOLEAN (*MRVL_AesPrimitiveDecrypt_hook) (MRVL_ENDECRYPT_t *crypt,
						 int *pErr);
extern void MRVL_AesPrimitiveDecrypt(MRVL_ENDECRYPT_t *crypt, int *pErr);

extern BOOLEAN (*MRVL_AesWrapEncrypt_hook) (MRVL_ENDECRYPT_t *crypt, int *pErr);
extern void MRVL_AesWrapEncrypt(MRVL_ENDECRYPT_t *crypt, int *pErr);

extern BOOLEAN (*MRVL_AesWrapDecrypt_hook) (MRVL_ENDECRYPT_t *crypt, int *pErr);
extern void MRVL_AesWrapDecrypt(MRVL_ENDECRYPT_t *crypt, int *pErr);

#ifdef DIAG_AES_CCM
extern BOOLEAN (*MRVL_AesCCMPollingMode_hook) (MRVL_ENDECRYPT_t *crypt,
					       int *pErr, int decEnable);
extern void MRVL_AesCCMPollingMode(MRVL_ENDECRYPT_t *crypt, int *pErr,
				   int decEnable);
#endif

extern BOOLEAN (*MRVL_AesEncrypt_hook) (UINT8 *kek, UINT8 kekLen, UINT8 *data,
					UINT8 *ret, int *ptr_val);
extern int MRVL_AesEncrypt(UINT8 *kek, UINT8 kekLen, UINT8 *data, UINT8 *ret);

extern BOOLEAN (*MRVL_AesValidateHostRequest_hook) (UINT32 *pBitMap,
						    UINT8 *pCmdPtr,
						    UINT8 *pCryptData,
						    SINT8 *pAESWrapEnc,
						    int *ptr_val);
extern int MRVL_AesValidateHostRequest(UINT32 *pBitMap, UINT8 *pCmdPtr,
				       UINT8 *pCryptData, SINT8 *pAESWrapEnc);

#ifdef RC4
extern BOOLEAN (*MRVL_Rc4Cryption_hook) (MRVL_ENDECRYPT_t *crypt, int *pErr);
extern void MRVL_Rc4Cryption(void *priv, MRVL_ENDECRYPT_t *crypt, int *pErr);
#endif

extern BOOLEAN (*MRVL_AesWrap_hook) (UINT8 *kek, UINT8 kekLen, UINT32 n,
				     UINT8 *plain, UINT8 *keyIv, UINT8 *cipher,
				     int *ptr_val);
extern int MRVL_AesWrap(UINT8 *kek, UINT8 kekLen, UINT32 n, UINT8 *plain,
			UINT8 *keyIv, UINT8 *cipher);

extern BOOLEAN (*MRVL_AesUnWrap_hook) (UINT8 *kek, UINT8 kekLen, UINT32 n,
				       UINT8 *cipher, UINT8 *keyIv,
				       UINT8 *plain, int *ptr_val);
extern int MRVL_AesUnWrap(UINT8 *kek, UINT8 kekLen, UINT32 n, UINT8 *cipher,
			  UINT8 *keyIv, UINT8 *plain);

extern BOOLEAN (*MRVL_AesSetKey_hook) (const UINT8 *kek, UINT8 kekLen,
				       int *ptr_val);
extern int MRVL_AesSetKey(const UINT8 *kek, UINT8 kekLen);

/* AES related defines */
#define MRVL_AES_KEY_UPDATE_DONE    5
#define MRVL_AES_DONE           6
#define MRVL_AES_ENGINE_BITS_POS    12

#define MRVL_AES_KEYUPDATE_LOC      0x00000900
#define MRVL_AES_ENABLE_ENCR_LOC    0x00000500
#define MRVL_AES_ENABLE_DECR_LOC    0x00000700

/* convert 4 individual bytes into a 4 byte unsigned int */
#define MRVL_AES_GET_UINT32(x) ((x[3]<<24)|(x[2]<<16)|(x[1]<<8)|x[0])

/* convert 4 byte unsigned int back to a 4 individual bytes */
#define MRVL_AES_CONVERT_UINT32_UINT8(x,u)              \
    *u  = (UINT8)((((UINT32)x)&0x000000ff));            \
    *(u + 1)= (UINT8)((((UINT32)x)&0x0000ff00)>>8);     \
    *(u + 2)= (UINT8)((((UINT32)x)&0x00ff0000)>>16);    \
    *(u + 3)= (UINT8)((((UINT32)x)&0xff000000)>>24)     \


/* HW register read macros */
#define MRVL_AES_READ32(x) (WL_REGS32(x))
#define MRVL_AES_READ16(x) (WL_REGS16(x))
#define MRVL_AES_READ8(x)  (WL_REGS8(x) )

/* HW register write macros */
#define MRVL_AES_WRITE32(reg,val) (WL_WRITE_REGS32(reg,val))
#define MRVL_AES_WRITE16(reg,val) (WL_WRITE_REGS16(reg,val))
#define MRVL_AES_WRITE8(reg,val)  (WL_WRITE_REGS8(reg,val))

extern UINT32 (*ramHook_CryptNew_EnterCritical) (void);
extern void (*ramHook_CryptNew_ExitCritical) (UINT32 intSave);

extern int MRVL_AES_MEMCMP(UINT8 *dst, UINT8 *src, int len);
extern void MRVL_AES_MEMSET(UINT8 *dst, UINT8 val, int size);
extern void MRVL_AES_MEMCPY(UINT8 *dst, UINT8 *src, int size);
extern int MRVL_AesInterCheck(UINT8 *inter, UINT8 *d);
#ifdef DIAG_AES_CCM
extern void MRVL_AesCCMEncrypt(MRVL_ENDECRYPT_t *crypt, int *pErr);
extern void MRVL_AesCCMDecrypt(MRVL_ENDECRYPT_t *crypt, int *pErr);
#endif

#endif /* _CRYPT_NEW_ROM_H_ */
