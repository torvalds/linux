/* LZO1X-1(15) compression

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
#include "libbb.h"
#include "liblzo.h"

#define D_BITS          15
#define D_INDEX1(d,p)   d = DM(DMUL(0x21,DX3(p,5,5,6)) >> 5)
#define D_INDEX2(d,p)   d = (d & (D_MASK & 0x7ff)) ^ (D_HIGH | 0x1f)

#define DO_COMPRESS     lzo1x_1_15_compress

#include "lzo1x_c.c"
