/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
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
 *************************************************************************

    Module Name:
    crypt_sha2.h

    Abstract:
    Miniport generic portion header file

    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
    Eddy        2008/11/24      Create SHA1
    Eddy        2008/07/23      Create SHA256
*/

#ifndef __CRYPT_SHA2_H__
#define __CRYPT_SHA2_H__

#ifdef CRYPT_TESTPLAN
#include "crypt_testplan.h"
#else
#include "rt_config.h"
#endif /* CRYPT_TESTPLAN */

/* Algorithm options */
#define SHA1_SUPPORT
#define SHA256_SUPPORT

#ifdef SHA1_SUPPORT
#define SHA1_BLOCK_SIZE    64 /* 512 bits = 64 bytes */
#define SHA1_DIGEST_SIZE   20 /* 160 bits = 20 bytes */
typedef struct _SHA1_CTX_STRUC {
    UINT32 HashValue[5];  /* 5 = (SHA1_DIGEST_SIZE / 32) */
    UINT64 MessageLen;    /* total size */
    UINT8  Block[SHA1_BLOCK_SIZE];
    UINT   BlockLen;
} SHA1_CTX_STRUC, *PSHA1_CTX_STRUC;

VOID SHA1_Init (
    IN  SHA1_CTX_STRUC *pSHA_CTX);
VOID SHA1_Hash (
    IN  SHA1_CTX_STRUC *pSHA_CTX);
VOID SHA1_Append (
    IN  SHA1_CTX_STRUC *pSHA_CTX,
    IN  const UINT8 Message[],
    IN  UINT MessageLen);
VOID SHA1_End (
    IN  SHA1_CTX_STRUC *pSHA_CTX,
    OUT UINT8 DigestMessage[]);
VOID RT_SHA1 (
    IN  const UINT8 Message[],
    IN  UINT MessageLen,
    OUT UINT8 DigestMessage[]);
#endif /* SHA1_SUPPORT */

#ifdef SHA256_SUPPORT
#define SHA256_BLOCK_SIZE   64 /* 512 bits = 64 bytes */
#define SHA256_DIGEST_SIZE  32 /* 256 bits = 32 bytes */
typedef struct _SHA256_CTX_STRUC {
    UINT32 HashValue[8];  /* 8 = (SHA256_DIGEST_SIZE / 32) */
    UINT64 MessageLen;    /* total size */
    UINT8  Block[SHA256_BLOCK_SIZE];
    UINT   BlockLen;
} SHA256_CTX_STRUC, *PSHA256_CTX_STRUC;

VOID SHA256_Init (
    IN  SHA256_CTX_STRUC *pSHA_CTX);
VOID SHA256_Hash (
    IN  SHA256_CTX_STRUC *pSHA_CTX);
VOID SHA256_Append (
    IN  SHA256_CTX_STRUC *pSHA_CTX,
    IN  const UINT8 Message[],
    IN  UINT MessageLen);
VOID SHA256_End (
    IN  SHA256_CTX_STRUC *pSHA_CTX,
    OUT UINT8 DigestMessage[]);
VOID RT_SHA256 (
    IN  const UINT8 Message[],
    IN  UINT MessageLen,
    OUT UINT8 DigestMessage[]);
#endif /* SHA256_SUPPORT */

#endif /* __CRYPT_SHA2_H__ */
