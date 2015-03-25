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
/* vi: set ts=8: */

/* This file contains a partial redefinition of the PowerVR Services 5
 * interface for use by components which are checkpatch clean. This
 * header is included by the unrefined, non-checkpatch clean headers
 * to ensure that prototype/typedef/macro changes break the build.
 */

#ifndef __SERVICES_KERNEL_CLIENT__
#define __SERVICES_KERNEL_CLIENT__

#include "debug_request_ids.h"
#include "pvrsrv_error.h"

#include <linux/types.h>

#ifndef __pvrsrv_defined_struct_enum__

/* pvrsrv_device_types.h */

enum PVRSRV_DEVICE_TYPE {
	PVRSRV_DEVICE_TYPE_RGX = 10,
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

enum PVRSRV_DEVICE_TYPE;
struct PVRSRV_CLIENT_SYNC_PRIM;
struct PVRSRV_CLIENT_SYNC_PRIM_OP;

#endif /* __pvrsrv_defined_struct_enum__ */

struct SYNC_PRIM_CONTEXT;

/* pvrsrv.h */

#define DEBUG_REQUEST_VERBOSITY_LOW    0
#define DEBUG_REQUEST_VERBOSITY_MEDIUM 1
#define DEBUG_REQUEST_VERBOSITY_HIGH   2
#define DEBUG_REQUEST_VERBOSITY_MAX    (DEBUG_REQUEST_VERBOSITY_HIGH)

typedef void (DUMPDEBUG_PRINTF_FUNC)(const char *fmt, ...) __printf(1, 2);

extern DUMPDEBUG_PRINTF_FUNC *g_pfnDumpDebugPrintf;

typedef void (*PFN_CMDCOMP_NOTIFY)(void *hCmdCompHandle);
enum PVRSRV_ERROR PVRSRVRegisterCmdCompleteNotify(void **phNotify,
	PFN_CMDCOMP_NOTIFY pfnCmdCompleteNotify, void *hPrivData);
enum PVRSRV_ERROR PVRSRVUnregisterCmdCompleteNotify(void *hNotify);

typedef void (*PFN_DBGREQ_NOTIFY) (void *hDebugRequestHandle,
	__u32 ui32VerbLevel);
enum PVRSRV_ERROR PVRSRVRegisterDbgRequestNotify(void **phNotify,
	PFN_DBGREQ_NOTIFY pfnDbgRequestNotify,
	__u32 ui32RequesterID, void *hDbgReqeustHandle);
enum PVRSRV_ERROR PVRSRVUnregisterDbgRequestNotify(void *hNotify);

enum PVRSRV_ERROR PVRSRVAcquireDeviceDataKM(__u32 ui32DevIndex,
	enum PVRSRV_DEVICE_TYPE eDeviceType, void **phDevCookie);
enum PVRSRV_ERROR PVRSRVReleaseDeviceDataKM(void *hDevCookie);
void PVRSRVCheckStatus(void *hCmdCompCallerHandle);
enum PVRSRV_ERROR AcquireGlobalEventObjectServer(void **phGlobalEventObject);
enum PVRSRV_ERROR ReleaseGlobalEventObjectServer(void *hGlobalEventObject);

/* sync.h */

enum PVRSRV_ERROR SyncPrimContextCreate(void *hBridge, void *hDeviceNode,
	struct SYNC_PRIM_CONTEXT **phSyncPrimContext);
void SyncPrimContextDestroy(struct SYNC_PRIM_CONTEXT *hSyncPrimContext);

enum PVRSRV_ERROR SyncPrimAlloc(struct SYNC_PRIM_CONTEXT *hSyncPrimContext,
	struct PVRSRV_CLIENT_SYNC_PRIM **ppsSync, const char * pszClassName);
void SyncPrimFree(struct PVRSRV_CLIENT_SYNC_PRIM *psSync);
__u32 SyncPrimGetFirmwareAddr(struct PVRSRV_CLIENT_SYNC_PRIM *psSync);

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

const char *PVRSRVGetErrorStringKM(enum PVRSRV_ERROR eError);

#endif /* __SERVICES_KERNEL_CLIENT__ */
