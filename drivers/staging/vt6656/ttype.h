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
 * File: ttype.h
 *
 * Purpose: define basic common types and macros
 *
 * Author: Tevin Chen
 *
 * Date: May 21, 1996
 *
 */

#ifndef __TTYPE_H__
#define __TTYPE_H__

#include <linux/types.h>

/******* Common definitions and typedefs ***********************************/

typedef int             BOOL;

#if !defined(TRUE)
#define TRUE            1
#endif
#if !defined(FALSE)
#define FALSE           0
#endif

/****** Simple typedefs  ***************************************************/

typedef u8 BYTE;
typedef u16 WORD;
typedef u32 DWORD;

// QWORD is for those situation that we want
// an 8-byte-aligned 8 byte long structure
// which is NOT really a floating point number.
typedef union tagUQuadWord {
    struct {
	u32 dwLowDword;
	u32 dwHighDword;
    } u;
    double      DoNotUseThisField;
} UQuadWord;
typedef UQuadWord       QWORD;          // 64-bit

/****** Common pointer types ***********************************************/

typedef u32 ULONG_PTR;
typedef u32 DWORD_PTR;

// boolean pointer

typedef BYTE *           PBYTE;

typedef WORD *           PWORD;

typedef DWORD *          PDWORD;

typedef QWORD *          PQWORD;

#endif /* __TTYPE_H__ */
