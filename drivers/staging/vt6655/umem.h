/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: umem.h
 *
 * Purpose: Define Memory macros
 *
 * Author: Tevin Chen
 *
 * Date: Mar 17, 1997
 *
 */


#ifndef __UMEM_H__
#define __UMEM_H__

#if !defined(__TTYPE_H__)
#include "ttype.h"
#endif



/*---------------------  Export Definitions -------------------------*/
// 4-byte memory tag
#define MEM_TAG 'mTEW'

// Macros used for memory allocation and deallocation.



#define ZERO_MEMORY(Destination,Length) {       \
            memset((PVOID)(Destination),        \
            0,                                  \
            (ULONG)(Length)                     \
            );                                  \
}

#define MEMvCopy(pvDest, pvSource, uCount) {    \
            memcpy((PVOID)(pvDest),             \
            (PVOID)(pvSource),                  \
            (ULONG)(uCount)                     \
            );                                  \
}


#define MEMEqualMemory(Destination,Source,Length)   (!memcmp((Destination),(Source),(Length)))
/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/




#endif // __UMEM_H__


