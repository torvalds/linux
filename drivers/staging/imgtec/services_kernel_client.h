/* -*- mode: c; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* vi: set ts=8 sw=8 sts=8: */
/*************************************************************************/ /*!
@File           services_kernel_client.h
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

/* This file contains a partial redefinition of the PowerVR Services 5
 * interface for use by components which are checkpatch clean. This
 * header is included by the unrefined, non-checkpatch clean headers
 * to ensure that prototype/typedef/macro changes break the build.
 */

#ifndef __SERVICES_KERNEL_CLIENT__
#define __SERVICES_KERNEL_CLIENT__

#include "pvrsrv_error.h"

#include <linux/types.h>

#ifndef __pvrsrv_defined_struct_enum__

/* rgx_fwif_shared.h */

struct _RGXFWIF_DEV_VIRTADDR_ {
	__u32 ui32Addr;
};

/* sync_external.h */

struct PVRSRV_CLIENT_SYNC_PRIM {
	volatile __u32 *pui32LinAddr;
};

struct PVRSRV_CLIENT_SYNC_PRIM_OP {
	__u32 ui32Flags;
	struct pvrsrv_sync_prim *psSync;
	__u32 ui32FenceValue;
	__u32 ui32UpdateValue;
};

#else /* __pvrsrv_defined_struct_enum__ */

struct _RGXFWIF_DEV_VIRTADDR_;

struct PVRSRV_CLIENT_SYNC_PRIM;
struct PVRSRV_CLIENT_SYNC_PRIM_OP;

#endif /* __pvrsrv_defined_struct_enum__ */

struct _PMR_;
struct _PVRSRV_DEVICE_NODE_;
struct dma_buf;
struct SYNC_PRIM_CONTEXT;

/* pvr_notifier.h */

#ifndef _CMDCOMPNOTIFY_PFN_
typedef void (*PFN_CMDCOMP_NOTIFY)(void *hCmdCompHandle);
#define _CMDCOMPNOTIFY_PFN_
#endif
enum PVRSRV_ERROR PVRSRVRegisterCmdCompleteNotify(void **phNotify,
	PFN_CMDCOMP_NOTIFY pfnCmdCompleteNotify, void *hPrivData);
enum PVRSRV_ERROR PVRSRVUnregisterCmdCompleteNotify(void *hNotify);
void PVRSRVCheckStatus(void *hCmdCompCallerHandle);

#define DEBUG_REQUEST_DC               0
#define DEBUG_REQUEST_SERVERSYNC       1
#define DEBUG_REQUEST_SYS              2
#define DEBUG_REQUEST_ANDROIDSYNC      3
#define DEBUG_REQUEST_LINUXFENCE       4
#define DEBUG_REQUEST_SYNCCHECKPOINT   5
#define DEBUG_REQUEST_HTB              6
#define DEBUG_REQUEST_APPHINT          7

#define DEBUG_REQUEST_VERBOSITY_LOW    0
#define DEBUG_REQUEST_VERBOSITY_MEDIUM 1
#define DEBUG_REQUEST_VERBOSITY_HIGH   2
#define DEBUG_REQUEST_VERBOSITY_MAX    DEBUG_REQUEST_VERBOSITY_HIGH

#ifndef _DUMPDEBUG_PRINTF_FUNC_
typedef void (DUMPDEBUG_PRINTF_FUNC)(void *pvDumpDebugFile,
	const char *fmt, ...) __printf(2, 3);
#define _DUMPDEBUG_PRINTF_FUNC_
#endif

#ifndef _PFN_DBGREQ_NOTIFY_
typedef void (*PFN_DBGREQ_NOTIFY) (void *hDebugRequestHandle,
	__u32 ui32VerbLevel,
	DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
	void *pvDumpDebugFile);
#define _PFN_DBGREQ_NOTIFY_
#endif

enum PVRSRV_ERROR PVRSRVRegisterDbgRequestNotify(void **phNotify,
	struct _PVRSRV_DEVICE_NODE_ *psDevNode,
	PFN_DBGREQ_NOTIFY pfnDbgRequestNotify,
	__u32 ui32RequesterID,
	void *hDbgRequestHandle);
enum PVRSRV_ERROR PVRSRVUnregisterDbgRequestNotify(void *hNotify);

/* physmem_dmabuf.h */

struct dma_buf *PhysmemGetDmaBuf(struct _PMR_ *psPMR);

/* pvrsrv.h */

enum PVRSRV_ERROR PVRSRVAcquireGlobalEventObjectKM(void **phGlobalEventObject);
enum PVRSRV_ERROR PVRSRVReleaseGlobalEventObjectKM(void *hGlobalEventObject);

/* sync.h */

enum PVRSRV_ERROR SyncPrimContextCreate(
	struct _PVRSRV_DEVICE_NODE_ *psDevConnection,
	struct SYNC_PRIM_CONTEXT **phSyncPrimContext);
void SyncPrimContextDestroy(struct SYNC_PRIM_CONTEXT *hSyncPrimContext);

enum PVRSRV_ERROR SyncPrimAlloc(struct SYNC_PRIM_CONTEXT *hSyncPrimContext,
	struct PVRSRV_CLIENT_SYNC_PRIM **ppsSync, const char *pszClassName);
enum PVRSRV_ERROR SyncPrimFree(struct PVRSRV_CLIENT_SYNC_PRIM *psSync);
enum PVRSRV_ERROR SyncPrimGetFirmwareAddr(
	struct PVRSRV_CLIENT_SYNC_PRIM *psSync,
	__u32 *sync_addr);
enum PVRSRV_ERROR SyncPrimSet(struct PVRSRV_CLIENT_SYNC_PRIM *psSync,
	__u32 ui32Value);

/* pdump_km.h */

#ifdef PDUMP
enum PVRSRV_ERROR __printf(1, 2) PDumpComment(char *fmt, ...);
#else
static inline enum PVRSRV_ERROR __printf(1, 2) PDumpComment(char *fmt, ...)
{
	return PVRSRV_OK;
}
#endif

/* osfunc.h */

void OSAcquireBridgeLock(void);
void OSReleaseBridgeLock(void);
enum PVRSRV_ERROR OSEventObjectWait(void *hOSEventKM);
enum PVRSRV_ERROR OSEventObjectOpen(void *hEventObject, void **phOSEventKM);
enum PVRSRV_ERROR OSEventObjectClose(void *hOSEventKM);

/* srvkm.h */

enum PVRSRV_ERROR PVRSRVDeviceCreate(void *pvOSDevice,
	struct _PVRSRV_DEVICE_NODE_ **ppsDeviceNode);
enum PVRSRV_ERROR PVRSRVDeviceDestroy(
	struct _PVRSRV_DEVICE_NODE_ *psDeviceNode);
const char *PVRSRVGetErrorStringKM(enum PVRSRV_ERROR eError);

#endif /* __SERVICES_KERNEL_CLIENT__ */
