/*************************************************************************/ /*!
@File
@Title          Services external synchronisation interface header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines synchronisation structures that are visible internally
                and externally
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

#include "img_types.h"

#ifndef _SYNC_EXTERNAL_
#define _SYNC_EXTERNAL_

#define SYNC_MAX_CLASS_NAME_LEN 32

#define	PVRSRV_MAX_SYNC_PRIMS 32 /*!< number of sync primitives in operations */


typedef IMG_HANDLE SYNC_BRIDGE_HANDLE;
typedef struct SYNC_PRIM_CONTEXT *PSYNC_PRIM_CONTEXT;
typedef struct _SYNC_OP_COOKIE_ *PSYNC_OP_COOKIE;

typedef struct PVRSRV_CLIENT_SYNC_PRIM
{
	volatile IMG_UINT32	*pui32LinAddr;	/*!< User pointer to the primitive */
} PVRSRV_CLIENT_SYNC_PRIM;

typedef IMG_HANDLE PVRSRV_CLIENT_SYNC_PRIM_HANDLE;

typedef struct PVRSRV_CLIENT_SYNC_PRIM_OP
{
	IMG_UINT32 					ui32Flags;				/*!< Operation flags */
#define PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK	(1 << 0)
#define PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE	(1 << 1)
#define PVRSRV_CLIENT_SYNC_PRIM_OP_UNFENCED_UPDATE (PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE | (1<<2))
	PVRSRV_CLIENT_SYNC_PRIM		*psSync;				/*!< Pointer to the client sync */
	IMG_UINT32					ui32FenceValue;			/*!< The Fence value (only used if PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK is set) */
	IMG_UINT32					ui32UpdateValue;		/*!< The Update value (only used if PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE is set) */
} PVRSRV_CLIENT_SYNC_PRIM_OP;

#if defined(__KERNEL__) && defined(ANDROID) && !defined(__GENKSYMS__)
#define __pvrsrv_defined_struct_enum__
#include <services_kernel_client.h>
#endif

#endif /* _SYNC_EXTERNAL_ */
