/** @file rijndael.h
 *
 *  @brief This file contains the function optimised ANSI C code for the Rijndael cipher (now AES)
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

#ifndef __RIJNDAEL_H
#define __RIJNDAEL_H
#include "wltypes.h"

#define MAXKC   (256/32)
#define MAXKB   (256/8)
#define MAXNR   14
/*
typedef unsigned char   u8;
typedef unsigned short  u16;
typedef unsigned int    u32;
*/
/*  The structure for key information */
typedef struct {
	int decrypt;
	int Nr;			/* key-length-dependent number of rounds */
	UINT key[4 * (MAXNR + 1)];	/* encrypt or decrypt key schedule */
} rijndael_ctx;

void rijndael_set_key(rijndael_ctx *, UINT8 *, int, int);
void rijndael_decrypt(rijndael_ctx *, UINT8 *, UINT8 *);
void rijndael_encrypt(rijndael_ctx *, UINT8 *, UINT8 *);

#endif /* __RIJNDAEL_H */
