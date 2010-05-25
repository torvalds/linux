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


/******* Common definitions and typedefs ***********************************/

#ifndef OUT
#define OUT
#endif

#ifndef TxInSleep
#define TxInSleep
#endif

typedef int             BOOL;

#if !defined(TRUE)
#define TRUE            1
#endif
#if !defined(FALSE)
#define FALSE           0
#endif

//2007-0809-01<Add>by MikeLiu
#ifndef  update_BssList
#define update_BssList
#endif



#ifndef WPA_SM_Transtatus
#define WPA_SM_Transtatus
#endif

#ifndef Calcu_LinkQual
#define Calcu_LinkQual
#endif

#ifndef Calcu_LinkQual
#define Calcu_LinkQual
#endif

/****** Simple typedefs  ***************************************************/

/* These lines assume that your compiler's longs are 32 bits and
 * shorts are 16 bits. It is already assumed that chars are 8 bits,
 * but it doesn't matter if they're signed or unsigned.
 */

typedef signed char             I8;     /* 8-bit signed integer */

typedef unsigned char           U8;     /* 8-bit unsigned integer */
typedef unsigned short          U16;    /* 16-bit unsigned integer */
typedef unsigned long           U32;    /* 32-bit unsigned integer */


typedef char            CHAR;
typedef signed short    SHORT;
typedef signed int      INT;
typedef signed long     LONG;

typedef unsigned char   UCHAR;
typedef unsigned short  USHORT;
typedef unsigned int    UINT;
typedef unsigned long   ULONG;
typedef unsigned long long	ULONGLONG; //64 bit



typedef unsigned char   BYTE;           //  8-bit
typedef unsigned short  WORD;           // 16-bit
typedef unsigned long   DWORD;          // 32-bit

// QWORD is for those situation that we want
// an 8-byte-aligned 8 byte long structure
// which is NOT really a floating point number.
typedef union tagUQuadWord {
    struct {
        DWORD   dwLowDword;
        DWORD   dwHighDword;
    } u;
    double      DoNotUseThisField;
} UQuadWord;
typedef UQuadWord       QWORD;          // 64-bit

/****** Common pointer types ***********************************************/

typedef unsigned long   ULONG_PTR;      // 32-bit
typedef unsigned long   DWORD_PTR;      // 32-bit

// boolean pointer
typedef unsigned int *   PUINT;

typedef BYTE *           PBYTE;

typedef WORD *           PWORD;

typedef DWORD *          PDWORD;

typedef QWORD *          PQWORD;

#endif // __TTYPE_H__
