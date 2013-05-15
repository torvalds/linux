/*****************************************************************************
**
** machine.h
**
** Copyright (c) Cambridge Electronic Design Limited 1991,1992,2010
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**
** Contact CED: Cambridge Electronic Design Limited, Science Park, Milton Road
**              Cambridge, CB6 0FE.
**              www.ced.co.uk
**              greg@ced.co.uk
**
** This file is included at the start of 'C' or 'C++' source file to define
** things for cross-platform/compiler interoperability. This used to deal with
** MSDOS/16-bit stuff, but this was all removed in Decemeber 2010. There are
** three things to consider: Windows, LINUX, mac OSX (BSD Unix) and 32 vs 64
** bit. At the time of writing (DEC 2010) there is a consensus on the following
** and their unsigned equivalents:
**
** type       bits
** char         8
** short       16
** int         32
** long long   64
**
** long is a problem as it is always 64 bits on linux/unix and is always 32 bits
** on windows.
** On windows, we define _IS_WINDOWS_ and one of WIN32 or WIN64.
** On linux we define LINUX
** On Max OSX we define MACOSX
**
*/

#ifndef __MACHINE_H__
#define __MACHINE_H__
#ifndef __KERNEL__
#include <float.h>
#include <limits.h>
#endif

/*
** The initial section is to identify the operating system
*/
#if (defined(__linux__) || defined(_linux) || defined(__linux)) && !defined(LINUX)
#define LINUX 1
#endif

#if (defined(__WIN32__) || defined(_WIN32)) && !defined(WIN32)
#define WIN32 1
#endif

#if defined(__APPLE__)
#define MACOSX
#endif

#if defined(_WIN64)
#undef WIN32
#undef WIN64
#define WIN64 1
#endif

#if defined(WIN32) || defined(WIN64)
#define _IS_WINDOWS_ 1
#endif

#if defined(LINUX) || defined(MAXOSX)
    #define FAR

    typedef int BOOL;       /*  To match Windows */
    typedef char * LPSTR;
    typedef const char * LPCSTR;
    typedef unsigned short WORD;
    typedef unsigned int  DWORD;
    typedef unsigned char  BYTE;
    typedef BYTE  BOOLEAN;
    typedef unsigned char UCHAR;
    #define __packed __attribute__((packed))
    typedef BYTE * LPBYTE;
    #define HIWORD(x) (WORD)(((x)>>16) & 0xffff)
    #define LOWORD(x) (WORD)((x) & 0xffff)
#endif

#ifdef _IS_WINDOWS_
#include <windows.h>
#define __packed
#endif

/*
** Sort out the DllExport and DllImport macros. The GCC compiler has its own
** syntax for this, though it also supports the MS specific __declspec() as
** a synonym.
*/
#ifdef GNUC
    #define DllExport __attribute__((dllexport))
    #define DllImport __attribute__((dllimport))
#endif

#ifndef DllExport
#ifdef _IS_WINDOWS_
    #define DllExport __declspec(dllexport)
    #define DllImport __declspec(dllimport)
#else
    #define DllExport
    #define DllImport
#endif
#endif /* _IS_WINDOWS_ */

    
#ifndef TRUE
   #define TRUE 1
   #define FALSE 0
#endif

#endif
