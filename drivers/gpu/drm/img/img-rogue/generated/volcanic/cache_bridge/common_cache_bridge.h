/*******************************************************************************
@File
@Title          Common bridge header for cache
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for cache
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
*******************************************************************************/

#ifndef COMMON_CACHE_BRIDGE_H
#define COMMON_CACHE_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"

#include "cache_ops.h"

#define PVRSRV_BRIDGE_CACHE_CMD_FIRST			0
#define PVRSRV_BRIDGE_CACHE_CACHEOPQUEUE			PVRSRV_BRIDGE_CACHE_CMD_FIRST+0
#define PVRSRV_BRIDGE_CACHE_CACHEOPEXEC			PVRSRV_BRIDGE_CACHE_CMD_FIRST+1
#define PVRSRV_BRIDGE_CACHE_CACHEOPLOG			PVRSRV_BRIDGE_CACHE_CMD_FIRST+2
#define PVRSRV_BRIDGE_CACHE_CMD_LAST			(PVRSRV_BRIDGE_CACHE_CMD_FIRST+2)

/*******************************************
            CacheOpQueue
 *******************************************/

/* Bridge in structure for CacheOpQueue */
typedef struct PVRSRV_BRIDGE_IN_CACHEOPQUEUE_TAG
{
	PVRSRV_CACHE_OP *piuCacheOp;
	IMG_UINT64 *pui64Address;
	IMG_DEVMEM_OFFSET_T *puiOffset;
	IMG_DEVMEM_SIZE_T *puiSize;
	IMG_HANDLE *phPMR;
	IMG_UINT32 ui32NumCacheOps;
	IMG_UINT32 ui32OpTimeline;
} __packed PVRSRV_BRIDGE_IN_CACHEOPQUEUE;

/* Bridge out structure for CacheOpQueue */
typedef struct PVRSRV_BRIDGE_OUT_CACHEOPQUEUE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_CACHEOPQUEUE;

/*******************************************
            CacheOpExec
 *******************************************/

/* Bridge in structure for CacheOpExec */
typedef struct PVRSRV_BRIDGE_IN_CACHEOPEXEC_TAG
{
	IMG_UINT64 ui64Address;
	IMG_DEVMEM_OFFSET_T uiOffset;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_HANDLE hPMR;
	PVRSRV_CACHE_OP iuCacheOp;
} __packed PVRSRV_BRIDGE_IN_CACHEOPEXEC;

/* Bridge out structure for CacheOpExec */
typedef struct PVRSRV_BRIDGE_OUT_CACHEOPEXEC_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_CACHEOPEXEC;

/*******************************************
            CacheOpLog
 *******************************************/

/* Bridge in structure for CacheOpLog */
typedef struct PVRSRV_BRIDGE_IN_CACHEOPLOG_TAG
{
	IMG_INT64 i64EndTime;
	IMG_INT64 i64StartTime;
	IMG_UINT64 ui64Address;
	IMG_DEVMEM_OFFSET_T uiOffset;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_HANDLE hPMR;
	PVRSRV_CACHE_OP iuCacheOp;
} __packed PVRSRV_BRIDGE_IN_CACHEOPLOG;

/* Bridge out structure for CacheOpLog */
typedef struct PVRSRV_BRIDGE_OUT_CACHEOPLOG_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_CACHEOPLOG;

#endif /* COMMON_CACHE_BRIDGE_H */
