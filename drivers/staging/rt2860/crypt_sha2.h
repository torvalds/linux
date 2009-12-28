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
 */

/****************************************************************************
    Module Name:
    SHA2

    Abstract:
    FIPS 180-2: Secure Hash Standard (SHS)

    Revision History:
    Who         When            What
    --------    ----------      ------------------------------------------
    Eddy        2008/11/24      Create SHA1
    Eddy        2008/07/23      Create SHA256
***************************************************************************/

#ifndef __CRYPT_SHA2_H__
#define __CRYPT_SHA2_H__

#ifdef CRYPT_TESTPLAN
#include "crypt_testplan.h"
#else
#include "rt_config.h"
#endif /* CRYPT_TESTPLAN */

/* Algorithm options */
#define SHA1_SUPPORT

#ifdef SHA1_SUPPORT
#define SHA1_BLOCK_SIZE    64	/* 512 bits = 64 bytes */
#define SHA1_DIGEST_SIZE   20	/* 160 bits = 20 bytes */
struct rt_sha1_ctx {
	u32 HashValue[5];	/* 5 = (SHA1_DIGEST_SIZE / 32) */
	u64 MessageLen;	/* total size */
	u8 Block[SHA1_BLOCK_SIZE];
	u32 BlockLen;
};

void RT_SHA1_Init(struct rt_sha1_ctx *pSHA_CTX);
void SHA1_Hash(struct rt_sha1_ctx *pSHA_CTX);
void SHA1_Append(struct rt_sha1_ctx *pSHA_CTX,
		 IN const u8 Message[], u32 MessageLen);
void SHA1_End(struct rt_sha1_ctx *pSHA_CTX, u8 DigestMessage[]);
void RT_SHA1(IN const u8 Message[],
	     u32 MessageLen, u8 DigestMessage[]);
#endif /* SHA1_SUPPORT */

#endif /* __CRYPT_SHA2_H__ */
