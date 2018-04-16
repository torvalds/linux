/** @file sha1.h
 *
 *  @brief This file contains the sha1 functions
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
#ifndef _SHA1_H_
#define _SHA1_H_

#include "wltypes.h"

enum {
	shaSuccess = 0,
	shaNull,		/* Null pointer parameter */
	shaInputTooLong,	/* input data too long */
	shaStateError		/* called Input after Result */
};

#define A_SHA_DIGEST_LEN 20

/*
 *  This structure will hold context information for the SHA-1
 *  hashing operation
 */
typedef struct {
	UINT32 Intermediate_Hash[A_SHA_DIGEST_LEN / 4];	/* Message Digest  */

	UINT32 Length_Low;	/* Message length in bits      */
	UINT32 Length_High;	/* Message length in bits      */

	UINT32 Scratch[16];	/* This is used to reduce the memory
				 ** requirements of the transform
				 **function
				 */
	UINT8 Message_Block[64];	/* 512-bit message blocks      */
	/* Index into message block array   */
	SINT16 Message_Block_Index;
	UINT8 Computed;		/* Is the digest computed?         */
	UINT8 Corrupted;	/* Is the message digest corrupted? */
} Mrvl_SHA1_CTX;

/*
 *  Function Prototypes
 */

extern int Mrvl_SHA1Init(Mrvl_SHA1_CTX *);
extern int Mrvl_SHA1Update(Mrvl_SHA1_CTX *, const UINT8 *, unsigned int);
extern int Mrvl_SHA1Final(void *priv, Mrvl_SHA1_CTX *,
			  UINT8 Message_Digest[A_SHA_DIGEST_LEN]);

extern void Mrvl_PRF(void *priv, unsigned char *key,
		     int key_len,
		     unsigned char *prefix,
		     int prefix_len,
		     unsigned char *data,
		     int data_len, unsigned char *output, int len);

extern void Mrvl_hmac_sha1(void *priv, unsigned char **ppText,
			   int *pTextLen,
			   int textNum,
			   unsigned char *key,
			   int key_len, unsigned char *output, int outputLen);

#endif
