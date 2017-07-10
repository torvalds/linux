/*************************************************************************/ /*!
@File           device_connection.h
@Title          
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    
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

#if !defined(__DEVICE_CONNECTION_H__)
#define __DEVICE_CONNECTION_H__

#include "img_types.h"

#if defined(__KERNEL__)
typedef struct _PVRSRV_DEVICE_NODE_ *SHARED_DEV_CONNECTION;
#else
typedef IMG_HANDLE SHARED_DEV_CONNECTION;
#endif

/******************************************************************************
 * Device capability flags and masks
 *****************************************************************************/

/* Flag to be passed over the bridge during connection stating whether CPU cache coherent is available*/
#define PVRSRV_CACHE_COHERENT_SHIFT (0)
#define	PVRSRV_CACHE_COHERENT_DEVICE_FLAG (1U << PVRSRV_CACHE_COHERENT_SHIFT)
#define	PVRSRV_CACHE_COHERENT_CPU_FLAG (2U << PVRSRV_CACHE_COHERENT_SHIFT)
#define PVRSRV_CACHE_COHERENT_MASK (3U << PVRSRV_CACHE_COHERENT_SHIFT)

/* Flag to be passed over the bridge during connection stating whether CPU non-mappable memory is present */
#define PVRSRV_NONMAPPABLE_MEMORY_PRESENT_SHIFT (3)
#define PVRSRV_NONMAPPABLE_MEMORY_PRESENT_FLAG (1U << PVRSRV_NONMAPPABLE_MEMORY_PRESENT_SHIFT)

/* Flag to be passed over the bridge during connection stating SVM allocation availability */
#define PVRSRV_DEVMEM_SVM_ALLOC_SHIFT (4)
#define PVRSRV_DEVMEM_SVM_ALLOC_UNSUPPORTED (1U << PVRSRV_DEVMEM_SVM_ALLOC_SHIFT)
#define PVRSRV_DEVMEM_SVM_ALLOC_SUPPORTED (2U << PVRSRV_DEVMEM_SVM_ALLOC_SHIFT)
#define PVRSRV_DEVMEM_SVM_ALLOC_CANFAIL (4U << PVRSRV_DEVMEM_SVM_ALLOC_SHIFT)

#endif /* !defined(__DEVICE_CONNECTION_H__) */
