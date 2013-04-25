/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#ifndef __CRYPT_AES_H__
#define __CRYPT_AES_H__

#include "rt_config.h"


/* AES definition & structure */
#define AES_STATE_ROWS 4     /* Block size: 4*4*8 = 128 bits */
#define AES_STATE_COLUMNS 4
#define AES_BLOCK_SIZES AES_STATE_ROWS*AES_STATE_COLUMNS
#define AES_KEY_ROWS 4
#define AES_KEY_COLUMNS 8    /*Key length: 4*{4,6,8}*8 = 128, 192, 256 bits */
#define AES_KEY128_LENGTH 16
#define AES_KEY192_LENGTH 24
#define AES_KEY256_LENGTH 32
#define AES_CBC_IV_LENGTH 16

typedef struct {
    UINT8 State[AES_STATE_ROWS][AES_STATE_COLUMNS];
    UINT8 KeyWordExpansion[AES_KEY_ROWS][AES_KEY_ROWS*((AES_KEY256_LENGTH >> 2) + 6 + 1)];
} AES_CTX_STRUC, *PAES_CTX_STRUC;


/* AES operations */
VOID RT_AES_KeyExpansion (
    IN UINT8 Key[],
    IN UINT KeyLength,
    INOUT AES_CTX_STRUC *paes_ctx);

VOID RT_AES_Encrypt (
    IN UINT8 PlainBlock[],
    IN UINT PlainBlockSize,
    IN UINT8 Key[],
    IN UINT KeyLength,
    OUT UINT8 CipherBlock[],
    INOUT UINT *CipherBlockSize);

VOID RT_AES_Decrypt (
    IN UINT8 CipherBlock[],
    IN UINT CipherBlockSize,
    IN UINT8 Key[],
    IN UINT KeyLength,
    OUT UINT8 PlainBlock[],
    INOUT UINT *PlainBlockSize);

/* AES Counter with CBC-MAC operations */
VOID AES_CCM_MAC (
    IN UINT8 Payload[],
    IN UINT  PayloadLength,
    IN UINT8 Key[],
    IN UINT  KeyLength,
    IN UINT8 Nonce[],
    IN UINT  NonceLength,
    IN UINT8 AAD[],
    IN UINT  AADLength,
    IN UINT  MACLength,
    OUT UINT8 MACText[]);

INT AES_CCM_Encrypt (
    IN UINT8 PlainText[],
    IN UINT  PlainTextLength,
    IN UINT8 Key[],
    IN UINT  KeyLength,
    IN UINT8 Nonce[],
    IN UINT  NonceLength,
    IN UINT8 AAD[],
    IN UINT  AADLength,
    IN UINT  MACLength,
    OUT UINT8 CipherText[],
    INOUT UINT *CipherTextLength);

INT AES_CCM_Decrypt (
    IN UINT8 CipherText[],
    IN UINT  CipherTextLength,
    IN UINT8 Key[],
    IN UINT  KeyLength,
    IN UINT8 Nonce[],
    IN UINT  NonceLength,
    IN UINT8 AAD[],
    IN UINT  AADLength,
    IN UINT  MACLength,
    OUT UINT8 PlainText[],
    INOUT UINT *PlainTextLength);

/* AES-CMAC operations */
VOID AES_CMAC_GenerateSubKey (
    IN UINT8 Key[],
    IN UINT KeyLength,
    OUT UINT8 SubKey1[],
    OUT UINT8 SubKey2[]);

VOID AES_CMAC (
    IN UINT8 PlainText[],
    IN UINT PlainTextLength,
    IN UINT8 Key[],
    IN UINT KeyLength,
    OUT UINT8 MACText[],
    INOUT UINT *MACTextLength);



/* AES-CBC operations */
VOID AES_CBC_Encrypt (
    IN UINT8 PlainText[],
    IN UINT PlainTextLength,
    IN UINT8 Key[],
    IN UINT KeyLength,
    IN UINT8 IV[],
    IN UINT IVLength,
    OUT UINT8 CipherText[],
    INOUT UINT *CipherTextLength);

VOID AES_CBC_Decrypt (
    IN UINT8 CipherText[],
    IN UINT CipherTextLength,
    IN UINT8 Key[],
    IN UINT KeyLength,
    IN UINT8 IV[],
    IN UINT IVLength,
    OUT UINT8 PlainText[],
    INOUT UINT *PlainTextLength);

/* AES key wrap operations */
INT AES_Key_Wrap (
    IN UINT8 PlainText[],
    IN UINT  PlainTextLength,
    IN UINT8 Key[],
    IN UINT  KeyLength,
    OUT UINT8 CipherText[],
    OUT UINT *CipherTextLength);
        
INT AES_Key_Unwrap (
    IN UINT8 CipherText[],
    IN UINT  CipherTextLength,
    IN UINT8 Key[],
    IN UINT  KeyLength,
    OUT UINT8 PlainText [],
    OUT UINT *PlainTextLength);


#endif /* __CRYPT_AES_H__ */

