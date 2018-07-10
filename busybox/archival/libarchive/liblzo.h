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

#include "liblzo_interface.h"

/* lzo-2.03/src/config1x.h */
#define M2_MIN_LEN      3
#define M2_MAX_LEN      8
#define M3_MAX_LEN      33
#define M4_MAX_LEN      9
#define M1_MAX_OFFSET   0x0400
#define M2_MAX_OFFSET   0x0800
#define M3_MAX_OFFSET   0x4000
#define M4_MAX_OFFSET   0xbfff
#define M1_MARKER       0
#define M3_MARKER       32
#define M4_MARKER       16

#define MX_MAX_OFFSET   (M1_MAX_OFFSET + M2_MAX_OFFSET)
#define MIN_LOOKAHEAD   (M2_MAX_LEN + 1)

#define LZO_EOF_CODE

/* lzo-2.03/src/lzo_dict.h */
#define GINDEX(m_pos,m_off,dict,dindex,in)    m_pos = dict[dindex]
#define DX2(p,s1,s2) \
        (((((unsigned)((p)[2]) << (s2)) ^ (p)[1]) << (s1)) ^ (p)[0])
//#define DA3(p,s1,s2,s3) ((DA2((p)+1,s2,s3) << (s1)) + (p)[0])
//#define DS3(p,s1,s2,s3) ((DS2((p)+1,s2,s3) << (s1)) - (p)[0])
#define DX3(p,s1,s2,s3) ((DX2((p)+1,s2,s3) << (s1)) ^ (p)[0])

#define D_SIZE        (1U << D_BITS)
#define D_MASK        ((1U << D_BITS) - 1)
#define D_HIGH        ((D_MASK >> 1) + 1)

#define LZO_CHECK_MPOS_NON_DET(m_pos,m_off,in,ip,max_offset) \
    ( \
        m_pos = ip - (unsigned)(ip - m_pos), \
        ((uintptr_t)m_pos < (uintptr_t)in \
	|| (m_off = (unsigned)(ip - m_pos)) <= 0 \
	|| m_off > max_offset) \
    )

#define DENTRY(p,in)                      (p)
#define UPDATE_I(dict,drun,index,p,in)    dict[index] = DENTRY(p,in)

#define DMS(v,s)  ((unsigned) (((v) & (D_MASK >> (s))) << (s)))
#define DM(v)     ((unsigned) ((v) & D_MASK))
#define DMUL(a,b) ((unsigned) ((a) * (b)))

/* lzo-2.03/src/lzo_ptr.h */
#define pd(a,b)  ((unsigned)((a)-(b)))

#    define TEST_IP             (ip < ip_end)
#    define NEED_IP(x) \
            if ((unsigned)(ip_end - ip) < (unsigned)(x))  goto input_overrun
#    define TEST_IV(x)          if ((x) > (unsigned)0 - (511)) goto input_overrun

#    undef TEST_OP              /* don't need both of the tests here */
#    define TEST_OP             1
#    define NEED_OP(x) \
            if ((unsigned)(op_end - op) < (unsigned)(x))  goto output_overrun
#    define TEST_OV(x)          if ((x) > (unsigned)0 - (511)) goto output_overrun

#define HAVE_ANY_OP 1

//#if defined(LZO_TEST_OVERRUN_LOOKBEHIND)
#  define TEST_LB(m_pos)        if (m_pos < out || m_pos >= op) goto lookbehind_overrun
//#  define TEST_LBO(m_pos,o)     if (m_pos < out || m_pos >= op - (o)) goto lookbehind_overrun
//#else
//#  define TEST_LB(m_pos)        ((void) 0)
//#  define TEST_LBO(m_pos,o)     ((void) 0)
//#endif
