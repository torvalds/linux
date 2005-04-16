#ifndef _ZFTAPE_COMPRESS_H
#define _ZFTAPE_COMPRESS_H
/*
 *      Copyright (c) 1994-1997 Claus-Justus Heine

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License as
 published by the Free Software Foundation; either version 2, or (at
 your option) any later version.
 
 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/compressor/zftape-compress.h,v $
 * $Revision: 1.1 $
 * $Date: 1997/10/05 19:12:32 $
 *
 * This file contains macros and definitions for zftape's
 * builtin compression code.
 *
 */

#include "../zftape/zftape-buffers.h"
#include "../zftape/zftape-vtbl.h"
#include "../compressor/lzrw3.h"

/* CMPR_WRK_MEM_SIZE gives the size of the compression wrk_mem */
/* I got these out of lzrw3.c */
#define U(X)            ((__u32) X)
#define SIZE_P_BYTE     (U(sizeof(__u8 *)))
#define ALIGNMENT_FUDGE (U(16))

#define CMPR_WRK_MEM_SIZE (U(4096)*(SIZE_P_BYTE) + ALIGNMENT_FUDGE)

/* the maximum number of bytes the size of the "compressed" data can
 * exceed the uncompressed data. As it is quite useless to compress
 * data twice it is sometimes the case that it is more efficient to
 * copy a block of data but to feed it to the "compression"
 * algorithm. In this case there are some flag bytes or the like
 * proceding the "compressed" data.  THAT MUST NOT BE THE CASE for the
 * algorithm we use for this driver. Instead, the high bit 15 of
 * compressed_size:
 *
 * compressed_size = ftape_compress()
 *
 * must be set in such a case.
 *
 * Nevertheless, it might also be as for lzrw3 that there is an
 * "intermediate" overrun that exceeds the amount of the compressed
 * data that is actually produced. During the algorithm we need in the
 * worst case MAX_CMP_GROUP bytes more than the input-size.
 */
#define MAX_CMP_GROUP (2+16*2) /* from lzrw3.c */

#define CMPR_OVERRUN      MAX_CMP_GROUP /* during compression */

/****************************************************/

#define     CMPR_BUFFER_SIZE (MAX_BLOCK_SIZE + CMPR_OVERRUN)

/* the compression map stores the byte offset compressed blocks within
 * the current volume for catridges with format code 2,3 and 5
 * (and old versions of zftape) and the offset measured in kilobytes for
 * format code 4 and 6. This gives us a possible max. size of a 
 * compressed volume of 1024*4GIG which should be enough.
 */
typedef __u32 CmprMap;

/* globals 
 */

/* exported functions
 */

#endif /* _ZFTAPE_COMPRESS_H */
