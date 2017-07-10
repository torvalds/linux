/*************************************************************************/ /*!
@File
@Title          Initialisation server os-dependent functionality definitions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Glue header for os-dependent API calls
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

#ifndef __SRVINIT_OSFUNC_H__
#define __SRVINIT_OSFUNC_H__

#if defined (__cplusplus)
extern "C" {
#endif

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/kernel.h>
#include <linux/string.h> //strlen, strcopy
#include <linux/ctype.h> //toupper
#else
#include <stddef.h>
#include <ctype.h> //toupper
#include <string.h> //strlen, strcopy
#include <stdio.h>
#endif

#if (defined(__linux__) && defined(__KERNEL__)) || (defined(INTEGRITY_OS) && defined(SUPPORT_KERNEL_SRVINIT)) || defined(__QNXNTO__)
#include "osfunc.h"
static inline void SRVINITDeviceMemCopy(void *pvDst, const void *pvSrc, size_t uiSize)
{
	OSDeviceMemCopy(pvDst, pvSrc, uiSize);
}

static inline void SRVINITDeviceMemSet(void *pvDest, IMG_UINT8 ui8Value, size_t uiSize)
{
	OSDeviceMemSet(pvDest, ui8Value, uiSize);
}
#else
#include "services.h"
static inline void SRVINITDeviceMemCopy(void *pvDst, const void *pvSrc, size_t uiSize)
{
	PVRSRVDeviceMemCopy(pvDst, pvSrc, uiSize);
}

static inline void SRVINITDeviceMemSet(void *pvDest, IMG_UINT8 ui8Value, size_t uiSize)
{
	PVRSRVDeviceMemSet(pvDest, ui8Value, uiSize);
}
#endif

#if defined (__cplusplus)
}
#endif

#endif /* __SRVINIT_OSFUNC_H__ */
