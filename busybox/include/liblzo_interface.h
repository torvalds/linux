/*
   This file is part of the LZO real-time data compression library.

   Copyright (C) 1996..2008 Markus Franz Xaver Johannes Oberhumer
   All Rights Reserved.

   Markus F.X.J. Oberhumer <markus@oberhumer.com>
   http://www.oberhumer.com/opensource/lzo/

   The LZO library is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   The LZO library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the LZO library; see the file COPYING.
   If not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#define LZO1X
#undef LZO1Y

#undef assert
/*
static void die_at(int line)
{
	bb_error_msg_and_die("internal error at %d", line);
}
#define assert(v) if (!(v)) die_at(__LINE__)
*/
#define assert(v) ((void)0)

int lzo1x_1_compress(const uint8_t* src, unsigned src_len,
		uint8_t* dst, unsigned* dst_len,
		void* wrkmem);
int lzo1x_1_15_compress(const uint8_t* src, unsigned src_len,
		uint8_t* dst, unsigned* dst_len,
		void* wrkmem);
int lzo1x_999_compress_level(const uint8_t* in, unsigned in_len,
		uint8_t* out, unsigned* out_len,
		void* wrkmem,
		int compression_level);

/* decompression */
//int lzo1x_decompress(const uint8_t* src, unsigned src_len,
//		uint8_t* dst, unsigned* dst_len /*, void* wrkmem */);
/* safe decompression with overrun testing */
int lzo1x_decompress_safe(const uint8_t* src, unsigned src_len,
		uint8_t* dst, unsigned* dst_len	/*, void* wrkmem */);

#define LZO_E_OK                    0
#define LZO_E_ERROR                 (-1)
#define LZO_E_OUT_OF_MEMORY         (-2)    /* [not used right now] */
#define LZO_E_NOT_COMPRESSIBLE      (-3)    /* [not used right now] */
#define LZO_E_INPUT_OVERRUN         (-4)
#define LZO_E_OUTPUT_OVERRUN        (-5)
#define LZO_E_LOOKBEHIND_OVERRUN    (-6)
#define LZO_E_EOF_NOT_FOUND         (-7)
#define LZO_E_INPUT_NOT_CONSUMED    (-8)
#define LZO_E_NOT_YET_IMPLEMENTED   (-9)    /* [not used right now] */

/* lzo-2.03/include/lzo/lzoconf.h */
#define LZO_VERSION   0x2030
