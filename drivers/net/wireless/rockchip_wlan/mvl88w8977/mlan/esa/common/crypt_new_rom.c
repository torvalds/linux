/** @file crypt_new_rom.c
 *
 *  @brief This file defines AES based functions.
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
 * AES based functions
 *
 * - AES Key Wrap algorithm (128-bit KEK) (RFC-3394)
 * - AES primitive algorithm
 * - AES CCM algorithm
 *
 * Date: 11/01/2005
 */
#include "wltypes.h"
#include "crypt_new_rom.h"
#include "rc4_rom.h"
#include "rijndael.h"

#include "hostsa_ext_def.h"
#include "authenticator.h"

static const UINT8 MRVL_DEFAULT_IV[8] = { 0xA6, 0xA6, 0xA6, 0xA6,
	0xA6, 0xA6, 0xA6, 0xA6
};

UINT8 aesResult[32];

/* MRVL_AES_MEMCMP
 *      : similiar to memcmp in std c lib
 * @dst     : dst ptr to be compared to
 * @src     : src ptr to be compared from
 * @len     : size of the comparison
 *
 * ASSUMPTION   : dst/src has to be valid data ptrs
 */
int
MRVL_AES_MEMCMP(UINT8 *dst, UINT8 *src, int len)
{
	int cnt = len;

	while (len--) {
		if (*dst++ == *src++) {
			cnt--;
		}
	}

	if (0 == cnt) {
		return 0;	/* dst == src */
	}

	return -1;		/* dst != src */
}

/* MRVL_AES_MEMSET
 *      : similiar to memset in std c lib
 * @dst     : dst starting pointer
 * @val     : val to be set
 * @size    : size of dst buffer to be set
 *
 * ASSUMPTION   : dst buffer must always have larger than the value of size
 */
void
MRVL_AES_MEMSET(UINT8 *dst, UINT8 val, int size)
{
	while (size--) {
		*dst++ = val;
	}
}

/* MRVL_AES_MEMCPY
 *      : similar to memcpy in std c lib
 * @dst     : dst buffer starting ptr
 * @src     : src buffer starting ptr
 * @size    : size of copy
 *
 * ASSUMPTION   :
 *      1: dst buffer must be larger than src + size of copy
 *      2: dst buffer isn't overlapping src buffer
 */
void
MRVL_AES_MEMCPY(UINT8 *dst, UINT8 *src, int size)
{
	if (dst < src) {
		while (size) {
			*dst++ = *src++;
			size--;
		}
	} else {
		while (size) {
			*(dst + size - 1) = *(src + size - 1);
			size--;
		}
	}
}

int
MRVL_AesInterCheck(UINT8 *inter, UINT8 *d)
{
	if (0 == MRVL_AES_MEMCMP(inter, d, 16)) {
		return 0;
	}

	return -1;
}

#ifdef WAR_ROM_BUG64609_SUPPORT_24_32_BYTES_KEY_LENGTH
extern const u8 Te4[256];
extern const u32 rcon[];
extern const u32 Td0[256];
extern const u32 Td1[256];
extern const u32 Td2[256];
extern const u32 Td3[256];
#define GETU32(pt) (((u32)(pt)[0] << 24) ^ ((u32)(pt)[1] << 16) ^ ((u32)(pt)[2] <<  8) ^ ((u32)(pt)[3]))

static int
rijndaelKeySetupEnc_2(u32 rk[ /*4*(Nr + 1) */ ], const u8 cipherKey[],
		      int keyBits)
{
	int i = 0;
	u32 temp;

	rk[0] = GETU32(cipherKey);
	rk[1] = GETU32(cipherKey + 4);
	rk[2] = GETU32(cipherKey + 8);
	rk[3] = GETU32(cipherKey + 12);
	if (keyBits == 128) {
		for (;;) {
			temp = rk[3];
			rk[4] = rk[0] ^
				(Te4[(temp >> 16) & 0xff] << 24) ^
				(Te4[(temp >> 8) & 0xff] << 16) ^
				(Te4[(temp) & 0xff] << 8) ^
				(Te4[(temp >> 24)]) ^ rcon[i];
			rk[5] = rk[1] ^ rk[4];
			rk[6] = rk[2] ^ rk[5];
			rk[7] = rk[3] ^ rk[6];
			if (++i == 10) {
				return 10;
			}
			rk += 4;
		}
	}

    /** Handle 24 bytes key length */
	rk[4] = GETU32(cipherKey + 16);
	rk[5] = GETU32(cipherKey + 20);
	if (keyBits == 192) {
		for (;;) {
			temp = rk[5];
			rk[6] = rk[0] ^
				(Te4[(temp >> 16) & 0xff] << 24) ^
				(Te4[(temp >> 8) & 0xff] << 16) ^
				(Te4[(temp) & 0xff] << 8) ^
				(Te4[(temp >> 24)]) ^ rcon[i];
			rk[7] = rk[1] ^ rk[6];
			rk[8] = rk[2] ^ rk[7];
			rk[9] = rk[3] ^ rk[8];
			if (++i == 8) {
				return 12;
			}
			rk[10] = rk[4] ^ rk[9];
			rk[11] = rk[5] ^ rk[10];
			rk += 6;
		}
	}

    /** Handle 32 bytes key length */
	rk[6] = GETU32(cipherKey + 24);
	rk[7] = GETU32(cipherKey + 28);
	if (keyBits == 256) {
		for (;;) {
			temp = rk[7];
			rk[8] = rk[0] ^
				(Te4[(temp >> 16) & 0xff] << 24) ^
				(Te4[(temp >> 8) & 0xff] << 16) ^
				(Te4[(temp) & 0xff] << 8) ^
				(Te4[(temp >> 24)]) ^ rcon[i];
			rk[9] = rk[1] ^ rk[8];
			rk[10] = rk[2] ^ rk[9];
			rk[11] = rk[3] ^ rk[10];
			if (++i == 7) {
				return 14;
			}
			temp = rk[11];
			rk[12] = rk[4] ^
				(Te4[(temp >> 24)] << 24) ^
				(Te4[(temp >> 16) & 0xff] << 16) ^
				(Te4[(temp >> 8) & 0xff] << 8) ^
				(Te4[(temp) & 0xff]);
			rk[13] = rk[5] ^ rk[12];
			rk[14] = rk[6] ^ rk[13];
			rk[15] = rk[7] ^ rk[14];
			rk += 8;
		}
	}
	return 0;
}

static int
rijndaelKeySetupDec_2(u32 rk[ /*4*(Nr + 1) */ ], const u8 cipherKey[],
		      int keyBits, int have_encrypt)
{
	int Nr, i, j;
	u32 temp;

	if (have_encrypt) {
		Nr = have_encrypt;
	} else {
		/* expand the cipher key: */
		Nr = rijndaelKeySetupEnc_2(rk, cipherKey, keyBits);
	}
	/* invert the order of the round keys: */
	for (i = 0, j = 4 * Nr; i < j; i += 4, j -= 4) {
		temp = rk[i];
		rk[i] = rk[j];
		rk[j] = temp;
		temp = rk[i + 1];
		rk[i + 1] = rk[j + 1];
		rk[j + 1] = temp;
		temp = rk[i + 2];
		rk[i + 2] = rk[j + 2];
		rk[j + 2] = temp;
		temp = rk[i + 3];
		rk[i + 3] = rk[j + 3];
		rk[j + 3] = temp;
	}
	/* apply the inverse MixColumn transform to all round keys but the first and the last: */
	for (i = 1; i < Nr; i++) {
		rk += 4;
		rk[0] = Td0[Te4[(rk[0] >> 24)]] ^
			Td1[Te4[(rk[0] >> 16) & 0xff]] ^
			Td2[Te4[(rk[0] >> 8) & 0xff]] ^
			Td3[Te4[(rk[0]) & 0xff]];
		rk[1] = Td0[Te4[(rk[1] >> 24)]] ^
			Td1[Te4[(rk[1] >> 16) & 0xff]] ^
			Td2[Te4[(rk[1] >> 8) & 0xff]] ^
			Td3[Te4[(rk[1]) & 0xff]];
		rk[2] = Td0[Te4[(rk[2] >> 24)]] ^
			Td1[Te4[(rk[2] >> 16) & 0xff]] ^
			Td2[Te4[(rk[2] >> 8) & 0xff]] ^
			Td3[Te4[(rk[2]) & 0xff]];
		rk[3] = Td0[Te4[(rk[3] >> 24)]] ^
			Td1[Te4[(rk[3] >> 16) & 0xff]] ^
			Td2[Te4[(rk[3] >> 8) & 0xff]] ^
			Td3[Te4[(rk[3]) & 0xff]];
	}
	return Nr;
}

static void
rijndael_set_key_2(rijndael_ctx *ctx, u8 *key, int bits, int encrypt)
{
	ctx->Nr = rijndaelKeySetupEnc_2(ctx->key, key, bits);
	if (encrypt) {
		ctx->decrypt = 0;
	} else {
		ctx->decrypt = 1;
		rijndaelKeySetupDec_2(ctx->key, key, bits, ctx->Nr);
							     /**bt_test :: TBD */
	}
}
#endif /** WAR_ROM_BUG64609_SUPPORT_24_32_BYTES_KEY_LENGTH */

/*
 * AesEncrypt   : aes primitive encryption
 *
 * @kek     : key encryption key
 * @kekLen  : kek len
 * @data    : data pointer
 *
 * ASSUMPTION   : both src and dst buffer has to be 16 bytes or 128-bit
 */
int
MRVL_AesEncrypt(UINT8 *kek, UINT8 kekLen, UINT8 *data, UINT8 *ret)
{
	//BufferDesc_t * pDesc = NULL;
	UINT8 buf[400] = { 0 };
	rijndael_ctx *ctx;
#if 0				//!defined(REMOVE_PATCH_HOOKS)
	int ptr_val;

	if (MRVL_AesEncrypt_hook(kek, kekLen, data, ret, &ptr_val)) {
		return ptr_val;
	}
#endif
#if 0
	/* Wait forever ensures a buffer */
	pDesc = (BufferDesc_t *) bml_AllocBuffer(ramHook_encrPoolConfig,
						 400, BML_WAIT_FOREVER);
#endif
	//ctx = (rijndael_ctx *)BML_DATA_PTR(pDesc);
	ctx = (rijndael_ctx *)buf;
#ifdef WAR_ROM_BUG64609_SUPPORT_24_32_BYTES_KEY_LENGTH
	rijndael_set_key_2(ctx, (UINT8 *)kek, kekLen * 64, 1);
#else
	rijndael_set_key(ctx, (UINT8 *)kek, kekLen * 64, 1);
#endif	   /** WAR_ROM_BUG64609_SUPPORT_24_32_BYTES_KEY_LENGTH */
	rijndael_encrypt(ctx, data, ret);
//    bml_FreeBuffer((UINT32)pDesc);

	return 0;
}

/****************************************************************
 * AES_WRAP : AES_WRAP is specified by RFC 3394 section 2.2.1
 *
 * Inputs   : plaintest, n 64-bit values {P1, P2, ..., Pn}, and
 *        Key, K (the KEK)
 * Outputs  : ciphertext, (n+1) 64-bit values {C0, ..., Cn}
 *
 * NOTE:  this function is ported over from WPA_SUPPLICANT
 ****************************************************************
 *
 * @kek     : key encryption key
 * @kekLen  : kek len, in unit of 64-bit, has to be 2, 3, or 4
 * @n       : length of the wrapped key in 64-bit
 *          unit; e.g.: 2 = 128-bit = 16 bytes
 * @plain   : plaintext key to be wrapped, has to be n * (64-bit)
 *          or n * 8 bytes
 * @cipher  : wrapped key, (n + 1) * 64-bit or (n+1) * 8 bytes
 */

/* debugging */

int
MRVL_AesWrap(UINT8 *kek, UINT8 kekLen, UINT32 n,
	     UINT8 *plain, UINT8 *keyIv, UINT8 *cipher)
{
	UINT8 a[8];
	UINT8 b[16];
	int i = 0;
	int j = 0;
	UINT8 *r = NULL;
#if 0				//!defined(REMOVE_PATCH_HOOKS)
	int ptr_val;

	if (MRVL_AesWrap_hook(kek, kekLen, n, plain, keyIv, cipher, &ptr_val)) {
		return ptr_val;
	}
#endif

	/* 0: before everything, check n value
	 */
	if (1 > n) {
		return -1;
	}

	r = cipher + 8;

	/* 1: initialize variables */
	MRVL_AES_MEMSET(b, 0x0, 16);
	if (keyIv) {
		MRVL_AES_MEMCPY(a, keyIv, 8);
	} else {
		MRVL_AES_MEMCPY(a, (UINT8 *)MRVL_DEFAULT_IV, 8);
	}
	MRVL_AES_MEMCPY(r, plain, (8 * n));

	/* 2: calculate intermediate values
	 * For j = 0 to 5
	 *  For i=1 to n
	 *      B = AES(K, A | R[i])
	 *      A = MSB(64, B) ^ t where t = (n*j)+i
	 *              R[i] = LSB(64, B)
	 */
	for (j = 0; j <= 5; j++) {
		r = cipher + 8;
		for (i = 1; i <= n; i++) {
			MRVL_AES_MEMCPY(b, a, 8);
			MRVL_AES_MEMCPY(b + 8, r, 8);
			MRVL_AesEncrypt(kek, kekLen, b, b);
			MRVL_AES_MEMCPY(a, b, 8);
			a[7] ^= n * j + i;
			MRVL_AES_MEMCPY(r, b + 8, 8);
			r += 8;
		}
	}

	MRVL_AES_MEMCPY(cipher, a, 8);

	/* 3: output the results
	 * these are already in @cipher
	 */

	return 0;
}

/****************************************************************
 * AES_UNWRAP   : AES_UNWRAP is specified by RFC 3394 section 2.2.2
 *
 * Inputs   : ciphertext, (n+1) 64-bit values {C0, ..., Cn}, and
 *        Key, K (the KEK)
 *
 * Outputs  : plaintest, n 64-bit values {P1, P2, ..., Pn} + first 8 bytes
 *        for KEYIV
 *
 *
 ****************************************************************
 *
 * @kek     : key encryption key
 * @kekLen  : kek len, in unit of 64-bit, has to be 2, 3, or 4
 * @n       : length of the wrapped key in 64-bit
 *          unit; e.g.: 2 = 128-bit = 16 bytes
 * @cipher  : wrapped data, (n + 1) * 64-bit or (n+1) * 8 bytes
 * @plain   : plaintext being unwrapped, has to be n * (64-bit)
 *          or n * 8 bytes + extra 8 bytes for KEYIV
 */
int
MRVL_AesUnWrap(UINT8 *kek, UINT8 kekLen, UINT32 n,
	       UINT8 *cipher, UINT8 *keyIv, UINT8 *plain)
{
	UINT8 b[16];
	int i = 0;
	int j = 0;
	UINT8 a[8];
	UINT8 *r = NULL;
	//BufferDesc_t * pDesc = NULL;
	UINT8 buf[400] = { 0 };

	rijndael_ctx *ctx;
#if 0				//!defined(REMOVE_PATCH_HOOKS)
	int ptr_val;

	if (MRVL_AesUnWrap_hook(kek, kekLen, n, cipher, keyIv, plain, &ptr_val)) {
		return ptr_val;
	}
#endif

	/* 0: before everything, check n value
	 */
	if (1 > n) {
		return -1;
	}

	/* 1: initialize variables */
	MRVL_AES_MEMSET(a, 0x0, 8);
	MRVL_AES_MEMSET(b, 0x0, 16);
	MRVL_AES_MEMCPY(a, cipher, 8);
	r = plain;
	MRVL_AES_MEMCPY(r, cipher + 8, 8 * n);
#if 0
	/* Wait forever ensures a buffer */
	pDesc = (BufferDesc_t *) bml_AllocBuffer(ramHook_encrPoolConfig,
						 400, BML_WAIT_FOREVER);
#endif
	//ctx = (rijndael_ctx *)BML_DATA_PTR(pDesc);
	ctx = (rijndael_ctx *)buf;
#ifdef WAR_ROM_BUG64609_SUPPORT_24_32_BYTES_KEY_LENGTH
	rijndael_set_key_2(ctx, (UINT8 *)kek, kekLen * 64, 0);
#else
	rijndael_set_key(ctx, (UINT8 *)kek, kekLen * 64, 0);
#endif	   /** WAR_ROM_BUG64609_SUPPORT_24_32_BYTES_KEY_LENGTH */

	/* 2: compute intermediate values
	 * For j = 5 to 0
	 *     For i = n to 1
	 *         B = AES-1(K, (A ^ t) | R[i]) where t = n*j+i
	 *         A = MSB(64, B)
	 *         R[i] = LSB(64, B)
	 */
	for (j = 5; j >= 0; j--) {
		r = plain + (n - 1) * 8;
		for (i = n; i >= 1; i--) {
			MRVL_AES_MEMCPY(b, a, 8);
			b[7] ^= n * j + i;
			MRVL_AES_MEMCPY(b + 8, r, 8);

			rijndael_decrypt(ctx, b, b);

			MRVL_AES_MEMCPY(a, b, 8);
			MRVL_AES_MEMCPY(r, b + 8, 8);
			r -= 8;
		}
	}

//    bml_FreeBuffer((UINT32)pDesc);

	/* 3: copy decrypted KeyIV to keyIv array */
	if (keyIv) {
		if (MRVL_AES_MEMCMP(keyIv, a, 8)) {
			return -1;
		}
	} else {
		if (MRVL_AES_MEMCMP((UINT8 *)MRVL_DEFAULT_IV, a, 8)) {
			return -1;
		}
	}

	return 0;
}

#if 0
/*****
 * AES CRYPTION HELPER FUNCTIONS
 *
 */
int
MRVL_AesValidateHostRequest(UINT32 *pBitMap, UINT8 *pCmdPtr,
			    UINT8 *pCryptData, SINT8 *AESwrapEnc)
{
	host_MRVL_AES_CRYPT_t *pLocal = NULL;
	MrvlIEParamSet_t *pLocalIEParam = NULL;
#if 0				//!defined(REMOVE_PATCH_HOOKS)
	int ptr_val;

	if (MRVL_AesValidateHostRequest_hook(pBitMap,
					     pCmdPtr,
					     pCryptData,
					     AESwrapEnc, &ptr_val)) {
		return ptr_val;
	}
#endif

	if (NULL == pBitMap || NULL == pCmdPtr) {
		return -1;
	}

	pLocal = (host_MRVL_AES_CRYPT_t *) pCmdPtr;
	pLocalIEParam = (MrvlIEParamSet_t *)&(pLocal->aesTlv);
	if ((0 != pLocal->action) && (0x1 != pLocal->action)) {
		*pBitMap |= MRVL_AES_NOT_EN_AND_DECRYPT;
	}

	if (0 == pLocal->action) {
		((MRVL_ENDECRYPT_t *)pCryptData)->enDeAction = CRYPT_DECRYPT;
	} else if (1 == pLocal->action) {
		((MRVL_ENDECRYPT_t *)pCryptData)->enDeAction = CRYPT_ENCRYPT;
	}
	switch (pLocal->algorithm) {
	case MRVL_CRYPTO_TEST_RC4:
		if ((1 > (pLocal->keyIVLen + pLocal->keyLen)) ||
		    (256 < (pLocal->keyIVLen + pLocal->keyLen))) {
			*pBitMap |= MRVL_AES_KEY_IV_INVALID_RC4;
		}
		break;
	case MRVL_CRYPTO_TEST_AES_ECB:
		if ((16 != pLocal->keyLen) &&
		    (24 != pLocal->keyLen) && (32 != pLocal->keyLen)) {
			*pBitMap |= MRVL_AES_KEY_SIZE_INVALID;
		}

		if ((16 != pLocalIEParam->Length)) {
			*pBitMap |= MRVL_AES_DATA_SIZE_INVALID;
		}
		break;
	case MRVL_CRYPTO_TEST_AES_WRAP:
		if (8 != pLocal->keyIVLen) {
			*pBitMap |= MRVL_AES_KEY_IV_INVALID_AES_WRAP;
		}

		if ((16 != pLocal->keyLen) &&
		    (24 != pLocal->keyLen) && (32 != pLocal->keyLen)) {
			*pBitMap |= MRVL_AES_KEY_SIZE_INVALID;
		}

		if ((1016 < pLocalIEParam->Length) ||
		    (8 > pLocalIEParam->Length)) {
			*pBitMap |= MRVL_AES_DATA_SIZE_INVALID;
		}

		if (1 == pLocal->action) {	/* Encryption */
			*AESwrapEnc = 8;
		} else if (0 == pLocal->action) {	/* Decryption */
			*AESwrapEnc = -8;
		}
		break;
#ifdef DIAG_AES_CCM
	case MRVL_CRYPTO_TEST_AEC_CCM:
		{
			host_MRVL_AES_CCM_CRYPT_t *pLocalCCM
				= (host_MRVL_AES_CCM_CRYPT_t *) pCmdPtr;

			pLocalIEParam =
				(MrvlIEParamSet_t *)&(pLocalCCM->aesTlv);

			/* key length should be 16 */
			if ((16 != pLocalCCM->keyLen) &&
			    (24 != pLocalCCM->keyLen) &&
			    (32 != pLocalCCM->keyLen)) {
				*pBitMap |= MRVL_AES_KEY_SIZE_INVALID;
			}

			/* nonce length 7 ~ 13 bytes */
			if ((pLocalCCM->nonceLen < 7) ||
			    (pLocalCCM->nonceLen > 13)) {
				*pBitMap |= MRVL_AES_NONCE_INVALID;
			}

			/* AAD length 0 ~ 30 bytes */
			if (pLocalCCM->aadLen > 30) {
				*pBitMap |= MRVL_AES_AAD_INVALID;
			}

			/* payload length 0 ~ 32 bytes */
			if (40 < pLocalIEParam->Length) {
				*pBitMap |= MRVL_AES_DATA_SIZE_INVALID;
			}
		}
		break;
#endif
#ifdef WAPI_HW_SUPPORT
	case MRVL_CRYPTO_TEST_WAPI:
		{
			host_MRVL_WAPI_CRYPT_t *pLocalWAPI
				= (host_MRVL_WAPI_CRYPT_t *) pCmdPtr;

			/* key length should be 16 */
			if (pLocalWAPI->keyLen != 32) {
				*pBitMap |= MRVL_AES_KEY_SIZE_INVALID;
			}

			/* nonce length 16 bytes */
			if (pLocalWAPI->nonceLen != 16) {
				*pBitMap |= MRVL_AES_NONCE_INVALID;
			}

			/* AAD length 32 or 48 bytes */
			if ((pLocalWAPI->aadLen != 32) &&
			    (pLocalWAPI->aadLen != 48)) {
				*pBitMap |= MRVL_AES_AAD_INVALID;
			}
		}
		break;
#endif
	default:
		*pBitMap |= MRVL_AES_ALGORITHM_INVALID;
		break;
	}

	/* put the buffer ptr to cryptdata */
	((MRVL_ENDECRYPT_t *)pCryptData)->pData = pCmdPtr;

	return 0;
}

/**************
 * WRAPPER to do AES primitive encryption
 */
void
MRVL_AesPrimitiveEncrypt(MRVL_ENDECRYPT_t *crypt, int *pErr)
{
	UINT8 *kek = NULL;
	UINT8 *data = NULL;
	UINT8 kekLen = 0;
	host_MRVL_AES_CRYPT_t *pLocal = NULL;
	MrvlIEAesCrypt_t *pLocalIEParam = NULL;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (MRVL_AesPrimitiveEncrypt_hook(crypt, pErr)) {
		return;
	}
#endif

	if ((NULL == pErr) || (NULL == crypt)) {
		*pErr = -1;
		return;
	}

	*pErr = 0;

	if ((CRYPT_ENCRYPT != crypt->enDeAction)) {
		*pErr = -1;
		return;
	}

	pLocal = (host_MRVL_AES_CRYPT_t *) (crypt->pData);
	pLocalIEParam = (MrvlIEAesCrypt_t *)&(pLocal->aesTlv);

	kek = (UINT8 *)(pLocal->key);
	kekLen = pLocal->keyLen;
	data = (UINT8 *)(pLocalIEParam->payload);

	if (-1 == MRVL_AesEncrypt(kek, kekLen / 8, data, data)) {
		*pErr = -1;
		return;
	}
}

void
MRVL_AesPrimitiveDecrypt(MRVL_ENDECRYPT_t *crypt, int *pErr)
{
	UINT8 *kek = NULL;
	UINT8 *data = NULL;
	UINT8 kekLen = 0;
	host_MRVL_AES_CRYPT_t *pLocal = NULL;
	MrvlIEAesCrypt_t *pLocalIEParam = NULL;
	BufferDesc_t *pDesc;
	rijndael_ctx *ctx;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (MRVL_AesPrimitiveDecrypt_hook(crypt, pErr)) {
		return;
	}
#endif

	if ((NULL == pErr) || (NULL == crypt)) {
		*pErr = -1;
		return;
	}

	if ((CRYPT_DECRYPT != crypt->enDeAction)) {
		*pErr = -1;
		return;
	}

	pLocal = (host_MRVL_AES_CRYPT_t *) (crypt->pData);
	pLocalIEParam = (MrvlIEAesCrypt_t *)&(pLocal->aesTlv);

	kek = (UINT8 *)(pLocal->key);
	kekLen = pLocal->keyLen;
	data = (UINT8 *)(pLocalIEParam->payload);
#if 0
	/* Wait forever ensures a buffer */
	pDesc = (BufferDesc_t *) bml_AllocBuffer(ramHook_encrPoolConfig,
						 400, BML_WAIT_FOREVER);
#endif
	ctx = (rijndael_ctx *)BML_DATA_PTR(pDesc);

#ifdef WAR_ROM_BUG64609_SUPPORT_24_32_BYTES_KEY_LENGTH
	rijndael_set_key_2(ctx, (UINT8 *)kek, kekLen * 8, 0);
#else
	rijndael_set_key(ctx, (UINT8 *)kek, kekLen * 8, 0);
#endif	   /** WAR_ROM_BUG64609_SUPPORT_24_32_BYTES_KEY_LENGTH */

	rijndael_decrypt(ctx, data, data);
	bml_FreeBuffer((UINT32)pDesc);

	*pErr = 0;

}

void
MRVL_AesWrapEncrypt(MRVL_ENDECRYPT_t *crypt, int *pErr)
{
	UINT8 *kek = NULL;
	UINT8 *data = NULL;
	UINT8 *keyIV = NULL;
	UINT8 kekLen = 0;
	UINT32 dataLen = 0;
	host_MRVL_AES_CRYPT_t *pLocal = NULL;
	MrvlIEAesCrypt_t *pLocalIEParam = NULL;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (MRVL_AesWrapEncrypt_hook(crypt, pErr)) {
		return;
	}
#endif

	if ((NULL == pErr) || (NULL == crypt)) {
		*pErr = -1;
		return;
	}

	*pErr = 0;

	if ((CRYPT_ENCRYPT != crypt->enDeAction)) {
		*pErr = -1;
		return;
	}

	pLocal = (host_MRVL_AES_CRYPT_t *) (crypt->pData);
	pLocalIEParam = (MrvlIEAesCrypt_t *)&(pLocal->aesTlv);

	kek = (UINT8 *)(pLocal->key);
	keyIV = (UINT8 *)(pLocal->keyIV);
	kekLen = pLocal->keyLen;
	data = (UINT8 *)(pLocalIEParam->payload);
	dataLen = pLocalIEParam->hdr.Length;
	/* need to add one more 8-bytes for return length */
	pLocalIEParam->hdr.Length = dataLen + 8;

	if (-1 ==
	    MRVL_AesWrap(kek, kekLen / 8, dataLen / 8, data, keyIV,
			 aesResult)) {
		*pErr = -2;
		return;
	}

	MRVL_AES_MEMCPY(data, aesResult, pLocalIEParam->hdr.Length);
}

void
MRVL_AesWrapDecrypt(MRVL_ENDECRYPT_t *crypt, int *pErr)
{
	UINT8 *kek = NULL;
	UINT8 *keyIV = NULL;
	UINT8 *data = NULL;
	UINT8 kekLen = 0;
	UINT32 dataLen = 0;
	host_MRVL_AES_CRYPT_t *pLocal = NULL;
	MrvlIEAesCrypt_t *pLocalIEParam = NULL;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (MRVL_AesWrapDecrypt_hook(crypt, pErr)) {
		return;
	}
#endif

	if ((NULL == pErr) || (NULL == crypt)) {
		*pErr = -1;
		return;
	}

	*pErr = 0;

	if ((CRYPT_DECRYPT != crypt->enDeAction)) {
		*pErr = -1;
		return;
	}

	pLocal = (host_MRVL_AES_CRYPT_t *) (crypt->pData);
	pLocalIEParam = (MrvlIEAesCrypt_t *)&(pLocal->aesTlv);

	kek = (UINT8 *)(pLocal->key);
	keyIV = (UINT8 *)(pLocal->keyIV);
	kekLen = pLocal->keyLen;
	data = (UINT8 *)(pLocalIEParam->payload);
	dataLen = pLocalIEParam->hdr.Length;

	if (-1 == MRVL_AesUnWrap(kek, kekLen / 8, dataLen / 8 - 1,
				 data, keyIV, aesResult)) {
		*pErr = -2;
		return;
	}

	dataLen -= 8;
	pLocalIEParam->hdr.Length = dataLen;
	MRVL_AES_MEMCPY(data, aesResult, dataLen);

}
#endif
#ifdef RC4

void
MRVL_Rc4Cryption(void *priv, MRVL_ENDECRYPT_t *crypt, int *pErr)
{
	host_MRVL_AES_CRYPT_t *pLocal = NULL;
	MrvlIEAesCrypt_t *pLocalIEParam = NULL;
	UINT8 *key = NULL;
	UINT8 *keyIV = NULL;
	UINT8 *data = NULL;
	UINT32 dataLen = 0;

	if ((NULL == pErr) || (NULL == crypt)) {
		*pErr = -1;
		return;
	}

	*pErr = 0;

	/* since RC4 encrypt/decrypt are the same */
	if ((CRYPT_DECRYPT != crypt->enDeAction) &&
	    (CRYPT_ENCRYPT != crypt->enDeAction)) {
		*pErr = -2;
		return;
	}

	pLocal = (host_MRVL_AES_CRYPT_t *) (crypt->pData);
	pLocalIEParam = (MrvlIEAesCrypt_t *)&(pLocal->aesTlv);

	key = (UINT8 *)(pLocal->key);
	data = (UINT8 *)(pLocalIEParam->payload);
	keyIV = (UINT8 *)(pLocal->keyIV);
	dataLen = pLocalIEParam->hdr.Length;

	RC4_Encrypt(priv, key, keyIV, pLocal->keyIVLen, data, dataLen, 0);

	return;
}
#endif /* RC4 */
