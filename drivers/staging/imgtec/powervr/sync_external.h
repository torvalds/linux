/*************************************************************************/ /*!
@File
@Title          Services external synchronisation interface header
@Description    Defines synchronisation structures that are visible internally
                and externally
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        MIT

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/ /**************************************************************************/

#ifndef _SYNC_EXTERNAL_
#define _SYNC_EXTERNAL_

#if defined (__cplusplus)
extern "C" {
#endif

#include <powervr/mem_types.h>

/*!
 * Maximum byte length for a sync prim name
 */
#define SYNC_MAX_CLASS_NAME_LEN 32

/*!
 * Number of sync primitives in operations
 */
#define	PVRSRV_MAX_SYNC_PRIMS 32

typedef void* PVRSRV_CLIENT_SYNC_PRIM_HANDLE;
typedef void* SYNC_BRIDGE_HANDLE;
typedef struct SYNC_PRIM_CONTEXT *PSYNC_PRIM_CONTEXT;
typedef struct _SYNC_OP_COOKIE_ *PSYNC_OP_COOKIE;

/*!
 * Client sync prim definition holding a CPU accessible address
 *
 *   Structure: #PVRSRV_CLIENT_SYNC_PRIM
 *   Typedef: ::PVRSRV_CLIENT_SYNC_PRIM
 */
typedef struct PVRSRV_CLIENT_SYNC_PRIM
{
	volatile uint32_t	*pui32LinAddr;	/*!< User pointer to the primitive */
} PVRSRV_CLIENT_SYNC_PRIM;

/*!
 * Bundled information for a sync prim operation
 *
 *   Structure: #PVRSRV_CLIENT_SYNC_PRIM_OP
 *   Typedef: ::PVRSRV_CLIENT_SYNC_PRIM_OP
 */
typedef struct PVRSRV_CLIENT_SYNC_PRIM_OP
{
	#define PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK	(1 << 0)
	#define PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE	(1 << 1)
	#define PVRSRV_CLIENT_SYNC_PRIM_OP_UNFENCED_UPDATE (PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE | (1<<2))
	uint32_t                    ui32Flags;       /*!< Operation flags: PVRSRV_CLIENT_SYNC_PRIM_OP_XXX */
	PVRSRV_CLIENT_SYNC_PRIM    *psSync;          /*!< Pointer to the client sync primitive */
	uint32_t                    ui32FenceValue;  /*!< The Fence value (only used if PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK is set) */
	uint32_t                    ui32UpdateValue; /*!< The Update value (only used if PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE is set) */
} PVRSRV_CLIENT_SYNC_PRIM_OP;

#if defined (__cplusplus)
}
#endif

#endif /* _SYNC_EXTERNAL_ */
