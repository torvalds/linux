/*************************************************************************/ /*!
@File
@Title          OS functions header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    OS specific API definitions
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef OSFUNC_COMMON_H
/*! @cond Doxygen_Suppress */
#define OSFUNC_COMMON_H
/*! @endcond */

#if defined(__KERNEL__) && defined(__linux__)
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "img_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**************************************************************************/ /*!
@Function       DeviceMemSet
@Description    Set memory, whose mapping may be uncached, to a given value.
                Safe implementation for all architectures for uncached mapping,
                optimised for speed where supported by tool chains.
                In such cases, OSDeviceMemSet() is defined as a call to this
                function.
@Input          pvDest     void pointer to the memory to be set
@Input          ui8Value   byte containing the value to be set
@Input          ui32Size   the number of bytes to be set to the given value
@Return         None
 */ /**************************************************************************/
void DeviceMemSet(void *pvDest, IMG_UINT8 ui8Value, size_t ui32Size);

/**************************************************************************/ /*!
@Function       DeviceMemCopy
@Description    Copy values from one area of memory. Safe implementation for
                all architectures for uncached mapping, of either the source
                or destination, optimised for speed where supported by tool
                chains. In such cases, OSDeviceMemCopy() is defined as a call
                to this function.
@Input          pvDst      void pointer to the destination memory
@Input          pvSrc      void pointer to the source memory
@Input          ui32Size   the number of bytes to be copied
@Return         None
 */ /**************************************************************************/
void DeviceMemCopy(void *pvDst, const void *pvSrc, size_t ui32Size);

/**************************************************************************/ /*!
@Function       DeviceMemSetBytes
@Description    Potentially very slow (but safe) memset fallback for non-GNU C
                compilers for arm64/aarch64
@Input          pvDest     void pointer to the memory to be set
@Input          ui8Value   byte containing the value to be set
@Input          ui32Size   the number of bytes to be set to the given value
@Return         None
 */ /**************************************************************************/
void DeviceMemSetBytes(void *pvDest, IMG_UINT8 ui8Value, size_t ui32Size);

/**************************************************************************/ /*!
@Function       DeviceMemCopyBytes
@Description    Potentially very slow (but safe) memcpy fallback for non-GNU C
                compilers for arm64/aarch64
@Input          pvDst      void pointer to the destination memory
@Input          pvSrc      void pointer to the source memory
@Input          ui32Size   the number of bytes to be copied
@Return         None
 */ /**************************************************************************/
void DeviceMemCopyBytes(void *pvDst, const void *pvSrc, size_t ui32Size);

/**************************************************************************/ /*!
@Function       StringLCopy
@Description    Copy at most uDataSize-1 bytes from pszSrc to pszDest.
                If no null byte ('\0') is contained within the first uDataSize-1
                characters of the source string, the destination string will be
                truncated. If the length of the source string is less than uDataSize
                an additional NUL byte will be copied to the destination string
                to ensure that the string is NUL-terminated.
@Input          pszDest       char pointer to the destination string
@Input          pszSrc        const char pointer to the source string
@Input          uDataSize     the maximum number of bytes to be copied
@Return         Size of the source string
 */ /**************************************************************************/
size_t StringLCopy(IMG_CHAR *pszDest, const IMG_CHAR *pszSrc, size_t uDataSize);

#if defined(__arm64__) || defined(__aarch64__) || defined(PVRSRV_DEVMEM_TEST_SAFE_MEMSETCPY)
#if defined(__GNUC__)
/* Workarounds for assumptions made that memory will not be mapped uncached
 * in kernel or user address spaces on arm64 platforms (or other testing).
 */

#define OSDeviceMemSet(a,b,c)  DeviceMemSet((a), (b), (c))
#define OSDeviceMemCopy(a,b,c) DeviceMemCopy((a), (b), (c))

#else /* defined __GNUC__ */

#define OSDeviceMemSet(a,b,c)  DeviceMemSetBytes((a), (b), (c))
#define OSDeviceMemCopy(a,b,c) DeviceMemCopyBytes((a), (b), (c))

#endif /* defined __GNUC__ */

#else /* (defined(__arm64__) || defined(__aarch64__) || defined(PVRSRV_DEVMEM_TEST_SAFE_MEMSETCPY)) */

/* Everything else */

/**************************************************************************/ /*!
@Function       OSDeviceMemSet
@Description    Set memory, whose mapping may be uncached, to a given value.
                On some architectures, additional processing may be needed
                if the mapping is uncached.
@Input          a     void pointer to the memory to be set
@Input          b     byte containing the value to be set
@Input          c     the number of bytes to be set to the given value
@Return         Pointer to the destination memory.
 */ /**************************************************************************/
#define OSDeviceMemSet(a,b,c) memset((a), (b), (c))

/**************************************************************************/ /*!
@Function       OSDeviceMemCopy
@Description    Copy values from one area of memory, to another, when one
                or both mappings may be uncached.
                On some architectures, additional processing may be needed
                if mappings are uncached.
@Input          a     void pointer to the destination memory
@Input          b     void pointer to the source memory
@Input          c     the number of bytes to be copied
@Return         Pointer to the destination memory.
 */ /**************************************************************************/
#define OSDeviceMemCopy(a,b,c) memcpy((a), (b), (c))

#endif /* (defined(__arm64__) || defined(__aarch64__) || defined(PVRSRV_DEVMEM_TEST_SAFE_MEMSETCPY)) */

/**************************************************************************/ /*!
@Function       OSCachedMemSet
@Description    Set memory, where the mapping is known to be cached, to a
                given value. This function exists to allow an optimal memset
                to be performed when memory is known to be cached.
@Input          a     void pointer to the memory to be set
@Input          b     byte containing the value to be set
@Input          c     the number of bytes to be set to the given value
@Return         Pointer to the destination memory.
 */ /**************************************************************************/
#define OSCachedMemSet(a,b,c)  memset((a), (b), (c))

/**************************************************************************/ /*!
@Function       OSCachedMemCopy
@Description    Copy values from one area of memory, to another, when both
                mappings are known to be cached.
                This function exists to allow an optimal memcpy to be
                performed when memory is known to be cached.
@Input          a     void pointer to the destination memory
@Input          b     void pointer to the source memory
@Input          c     the number of bytes to be copied
@Return         Pointer to the destination memory.
 */ /**************************************************************************/
#define OSCachedMemCopy(a,b,c) memcpy((a), (b), (c))

#if defined(__KERNEL__)

/**************************************************************************/ /*!
@Function       OSCachedMemSetWMB
@Description    Set memory, where the mapping is known to be cached or
                write-combine, to a given value and issue a write memory barrier
                after. This
                function exists to allow an optimal memset to be performed when
                memory is known to be cached or write-combine.
@Input          a     void pointer to the memory to be set
@Input          b     byte containing the value to be set
@Input          c     the number of bytes to be set to the given value
@Return         Pointer to the destination memory.
 */ /**************************************************************************/
#define OSCachedMemSetWMB(a,b,c) \
	do { \
		memset((a), (b), (c)); \
		OSWriteMemoryBarrier(); \
	} while (0)

/**************************************************************************/ /*!
@Function       OSCachedMemCopy
@Description    Copy values from one area of memory, to another, when both
                mappings are known to be cached or write-combine and issue
                a write memory barrier after.
                This function exists to allow an optimal memcpy to be
                performed when memory is known to be cached or write-combine.
@Input          a     void pointer to the destination memory
@Input          b     void pointer to the source memory
@Input          c     the number of bytes to be copied
@Return         Pointer to the destination memory.
 */ /**************************************************************************/
#define OSCachedMemCopyWMB(a,b,c) \
	do { \
		memcpy((a), (b), (c)); \
		OSWriteMemoryBarrier(); \
	} while (0)

#endif /* defined(__KERNEL__) */

/**************************************************************************/ /*!
@Function       OSStringLCopy
@Description    Copy at most uDataSize-1 bytes from pszSrc to pszDest.
                If no null byte ('\0') is contained within the first uDataSize-1
                characters of the source string, the destination string will be
                truncated. If the length of the source string is less than uDataSize
                an additional NUL byte will be copied to the destination string
                to ensure that the string is NUL-terminated.
@Input          a     char pointer to the destination string
@Input          b     const char pointer to the source string
@Input          c     the maximum number of bytes to be copied
@Return         Size of the source string
 */ /**************************************************************************/
#if defined(__QNXNTO__) || (defined(__linux__) && defined(__KERNEL__) && !defined(DEBUG))
#define OSStringLCopy(a,b,c) strlcpy((a), (b), (c))
#else /* defined(__QNXNTO__) ... */
#define OSStringLCopy(a,b,c) StringLCopy((a), (b), (c))
#endif /* defined(__QNXNTO__) ... */

#ifdef __cplusplus
}
#endif

#endif /* OSFUNC_COMMON_H */

/******************************************************************************
 End of file (osfunc_common.h)
******************************************************************************/
