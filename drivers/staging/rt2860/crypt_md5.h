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
    MD5

    Abstract:
    RFC1321: The MD5 Message-Digest Algorithm

    Revision History:
    Who         When            What
    --------    ----------      ------------------------------------------
    Eddy        2008/11/24      Create md5
***************************************************************************/

#ifndef __CRYPT_MD5_H__
#define __CRYPT_MD5_H__

#ifdef CRYPT_TESTPLAN
#include "crypt_testplan.h"
#else
#include "rt_config.h"
#endif /* CRYPT_TESTPLAN */

/* Algorithm options */
#define MD5_SUPPORT

#ifdef MD5_SUPPORT
#define MD5_BLOCK_SIZE    64	/* 512 bits = 64 bytes */
#define MD5_DIGEST_SIZE   16	/* 128 bits = 16 bytes */

struct rt_md5_ctx_struc {
	u32 HashValue[4];
	u64 MessageLen;
	u8 Block[MD5_BLOCK_SIZE];
	u32 BlockLen;
};

void MD5_Init(struct rt_md5_ctx_struc *pMD5_CTX);
void MD5_Hash(struct rt_md5_ctx_struc *pMD5_CTX);
void MD5_Append(struct rt_md5_ctx_struc *pMD5_CTX,
		IN const u8 Message[], u32 MessageLen);
void MD5_End(struct rt_md5_ctx_struc *pMD5_CTX, u8 DigestMessage[]);
void RT_MD5(IN const u8 Message[],
	    u32 MessageLen, u8 DigestMessage[]);
#endif /* MD5_SUPPORT */

#endif /* __CRYPT_MD5_H__ */
