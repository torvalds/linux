/*
 * Copyright 2002-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_AES_LOCL_H
# define HEADER_AES_LOCL_H

# include <openssl/e_os2.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>

# if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_AMD64) || defined(_M_X64))
#  define SWAP(x) (_lrotl(x, 8) & 0x00ff00ff | _lrotr(x, 8) & 0xff00ff00)
#  define GETU32(p) SWAP(*((u32 *)(p)))
#  define PUTU32(ct, st) { *((u32 *)(ct)) = SWAP((st)); }
# else
#  define GETU32(pt) (((u32)(pt)[0] << 24) ^ ((u32)(pt)[1] << 16) ^ ((u32)(pt)[2] <<  8) ^ ((u32)(pt)[3]))
#  define PUTU32(ct, st) { (ct)[0] = (u8)((st) >> 24); (ct)[1] = (u8)((st) >> 16); (ct)[2] = (u8)((st) >>  8); (ct)[3] = (u8)(st); }
# endif

# ifdef AES_LONG
typedef unsigned long u32;
# else
typedef unsigned int u32;
# endif
typedef unsigned short u16;
typedef unsigned char u8;

# define MAXKC   (256/32)
# define MAXKB   (256/8)
# define MAXNR   14

/* This controls loop-unrolling in aes_core.c */
# undef FULL_UNROLL

#endif                          /* !HEADER_AES_LOCL_H */
