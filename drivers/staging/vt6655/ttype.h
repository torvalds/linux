/*
 * File: ttype.h
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

#ifndef VOID
#define VOID            void
#endif

#ifndef CONST
#define CONST           const
#endif

#ifndef STATIC
#define STATIC          static
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#ifndef TxInSleep
#define TxInSleep
#endif
#if! defined(__CPU8051)
typedef int             BOOL;
#else   // __CPU8051
#define BOOL            int
#endif  // __CPU8051

#if !defined(TRUE)
#define TRUE            1
#endif
#if !defined(FALSE)
#define FALSE           0
#endif


#if !defined(SUCCESS)
#define SUCCESS         0
#endif
#if !defined(FAILED)
#define FAILED          -1
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

#if! defined(__CPU8051)

/* These lines assume that your compiler's longs are 32 bits and
 * shorts are 16 bits. It is already assumed that chars are 8 bits,
 * but it doesn't matter if they're signed or unsigned.
 */

typedef signed char             I8;     /* 8-bit signed integer */
typedef signed short            I16;    /* 16-bit signed integer */
typedef signed long             I32;    /* 32-bit signed integer */

typedef unsigned char           U8;     /* 8-bit unsigned integer */
typedef unsigned short          U16;    /* 16-bit unsigned integer */
typedef unsigned long           U32;    /* 32-bit unsigned integer */


#if defined(__WIN32)
typedef signed __int64          I64;    /* 64-bit signed integer */
typedef unsigned __int64        U64;    /* 64-bit unsigned integer */
#endif // __WIN32


typedef char            CHAR;
typedef signed short    SHORT;
typedef signed int      INT;
typedef signed long     LONG;

typedef unsigned char   UCHAR;
typedef unsigned short  USHORT;
typedef unsigned int    UINT;
typedef unsigned long   ULONG;
typedef unsigned long long	ULONGLONG; //64 bit
typedef unsigned long long	ULONGULONG;



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



#ifndef _TCHAR_DEFINED
typedef char            TCHAR;
typedef char*           PTCHAR;
typedef unsigned char   TBYTE;
typedef unsigned char*  PTBYTE;
#define _TCHAR_DEFINED
#endif

#else   // __CPU8051

#define U8              unsigned char
#define U16             unsigned short
#define U32             unsigned long

#define USHORT          unsigned short
#define UINT            unsigned int

#define BYTE            unsigned char
#define WORD            unsigned short
#define DWORD           unsigned long


#endif  // __CPU8051


// maybe this should be defined in <limits.h>
#define U8_MAX          0xFFU
#define U16_MAX         0xFFFFU
#define U32_MAX         0xFFFFFFFFUL

#define BYTE_MAX        0xFFU
#define WORD_MAX        0xFFFFU
#define DWORD_MAX       0xFFFFFFFFUL




/******* 32-bit vs. 16-bit definitions and typedefs ************************/

#if !defined(NULL)
#ifdef __cplusplus
#define NULL            0
#else
#define NULL            ((void *)0)
#endif // __cplusplus
#endif // !NULL




#if defined(__WIN32) || defined(__CPU8051)

#if !defined(FAR)
#define FAR
#endif
#if !defined(NEAR)
#define NEAR
#endif
#if !defined(DEF)
#define DEF
#endif
#if !defined(CALLBACK)
#define CALLBACK
#endif

#else  // !__WIN32__

#if !defined(FAR)
#define FAR
#endif
#if !defined(NEAR)
#define NEAR
#endif
#if !defined(DEF)
// default pointer type is FAR, if you want near pointer just redefine it to NEAR
#define DEF
#endif
#if !defined(CALLBACK)
#define CALLBACK
#endif

#endif // !__WIN32__




/****** Common pointer types ***********************************************/

#if! defined(__CPU8051)

typedef signed char DEF*        PI8;
typedef signed short DEF*       PI16;
typedef signed long DEF*        PI32;

typedef unsigned char DEF*      PU8;
typedef unsigned short DEF*     PU16;
typedef unsigned long DEF*      PU32;

#if defined(__WIN32)
typedef signed __int64 DEF*     PI64;
typedef unsigned __int64 DEF*   PU64;
#endif // __WIN32

#if !defined(_WIN64)
typedef unsigned long   ULONG_PTR;      // 32-bit
typedef unsigned long   DWORD_PTR;      // 32-bit
#endif // _WIN64


// boolean pointer
typedef int DEF*            PBOOL;
typedef int NEAR*           NPBOOL;
typedef int FAR*            LPBOOL;

typedef int DEF*            PINT;
typedef int NEAR*           NPINT;
typedef int FAR*            LPINT;
typedef const int DEF*      PCINT;
typedef const int NEAR*     NPCINT;
typedef const int FAR*      LPCINT;

typedef unsigned int DEF*           PUINT;
typedef const unsigned int DEF*     PCUINT;

typedef long DEF*           PLONG;
typedef long NEAR*          NPLONG;
typedef long FAR*           LPLONG;
//typedef const long DEF*     PCLONG;
typedef const long NEAR*    NPCLONG;
typedef const long FAR*     LPCLONG;

typedef BYTE DEF*           PBYTE;
typedef BYTE NEAR*          NPBYTE;
typedef BYTE FAR*           LPBYTE;
typedef const BYTE DEF*     PCBYTE;
typedef const BYTE NEAR*    NPCBYTE;
typedef const BYTE FAR*     LPCBYTE;

typedef WORD DEF*           PWORD;
typedef WORD NEAR*          NPWORD;
typedef WORD FAR*           LPWORD;
typedef const WORD DEF*     PCWORD;
typedef const WORD NEAR*    NPCWORD;
typedef const WORD FAR*     LPCWORD;

typedef DWORD DEF*          PDWORD;
typedef DWORD NEAR*         NPDWORD;
typedef DWORD FAR*          LPDWORD;
typedef const DWORD DEF*    PCDWORD;
typedef const DWORD NEAR*   NPCDWORD;
typedef const DWORD FAR*    LPCDWORD;

typedef QWORD DEF*          PQWORD;
typedef QWORD NEAR*         NPQWORD;
typedef QWORD FAR*          LPQWORD;
typedef const QWORD DEF*    PCQWORD;
typedef const QWORD NEAR*   NPCQWORD;
typedef const QWORD FAR*    LPCQWORD;

typedef void DEF*           PVOID;
typedef void NEAR*          NPVOID;
typedef void FAR*           LPVOID;

// handle declaration
#ifdef STRICT
typedef void *HANDLE;
#else
typedef PVOID HANDLE;
#endif

//
// ANSI (Single-byte Character) types
//
typedef char DEF*           PCH;
typedef char NEAR*          NPCH;
typedef char FAR*           LPCH;
typedef const char DEF*     PCCH;
typedef const char NEAR*    NPCCH;
typedef const char FAR*     LPCCH;

typedef char DEF*           PSTR;
typedef char NEAR*          NPSTR;
typedef char FAR*           LPSTR;
typedef const char DEF*     PCSTR;
typedef const char NEAR*    NPCSTR;
typedef const char FAR*     LPCSTR;

#endif  // !__CPU8051




/****** Misc definitions, types ********************************************/

// parameter prefix
#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif


// unreferenced parameter macro to avoid warning message in MS C
#if defined(__TURBOC__)

//you should use "#pragma argsused" to avoid warning message in Borland C
#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(x)
#endif

#else

#ifndef UNREFERENCED_PARAMETER
//#define UNREFERENCED_PARAMETER(x) x
#define UNREFERENCED_PARAMETER(x)
#endif

#endif


// in-line assembly prefix
#if defined(__TURBOC__)
#define ASM             asm
#else  // !__TURBOC__
#define ASM             _asm
#endif // !__TURBOC__




#endif // __TTYPE_H__


