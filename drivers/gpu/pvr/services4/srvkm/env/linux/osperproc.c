/*************************************************************************/ /*!
@Title          Linux specific per process data functions
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

#include "services_headers.h"
#include "osperproc.h"

#include "env_perproc.h"
#include "proc.h"

#if defined (SUPPORT_ION)
#include "ion.h"
extern struct ion_device *gpsIonDev;
#endif

extern IMG_UINT32 gui32ReleasePID;

PVRSRV_ERROR OSPerProcessPrivateDataInit(IMG_HANDLE *phOsPrivateData)
{
	PVRSRV_ERROR eError;
	IMG_HANDLE hBlockAlloc;
	PVRSRV_ENV_PER_PROCESS_DATA *psEnvPerProc;

	eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				sizeof(PVRSRV_ENV_PER_PROCESS_DATA),
				phOsPrivateData,
				&hBlockAlloc,
				"Environment per Process Data");

	if (eError != PVRSRV_OK)
	{
		*phOsPrivateData = IMG_NULL;

		PVR_DPF((PVR_DBG_ERROR, "%s: OSAllocMem failed (%d)", __FUNCTION__, eError));
		return eError;
	}

	psEnvPerProc = (PVRSRV_ENV_PER_PROCESS_DATA *)*phOsPrivateData;
	OSMemSet(psEnvPerProc, 0, sizeof(*psEnvPerProc));

	psEnvPerProc->hBlockAlloc = hBlockAlloc;

	/* Linux specific mmap processing */
	LinuxMMapPerProcessConnect(psEnvPerProc);

#if defined(SUPPORT_DRI_DRM) && defined(PVR_SECURE_DRM_AUTH_EXPORT)
	/* Linked list of PVRSRV_FILE_PRIVATE_DATA structures */
	INIT_LIST_HEAD(&psEnvPerProc->sDRMAuthListHead);
#endif

#if defined(SUPPORT_ION)
	OSSNPrintf(psEnvPerProc->azIonClientName, ION_CLIENT_NAME_SIZE, "pvr_ion_client-%d", OSGetCurrentProcessIDKM());
	psEnvPerProc->psIONClient =
		ion_client_create(gpsIonDev, -1,
						  psEnvPerProc->azIonClientName);
 
	if (IS_ERR_OR_NULL(psEnvPerProc->psIONClient))
	{
		PVR_DPF((PVR_DBG_ERROR, "OSPerProcessPrivateDataInit: Couldn't create "
								"ion client for per process data"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
#endif /* defined(SUPPORT_ION) */

	return PVRSRV_OK;
}

PVRSRV_ERROR OSPerProcessPrivateDataDeInit(IMG_HANDLE hOsPrivateData)
{
	PVRSRV_ERROR eError;
	PVRSRV_ENV_PER_PROCESS_DATA *psEnvPerProc;

	if (hOsPrivateData == IMG_NULL)
	{
		return PVRSRV_OK;
	}

	psEnvPerProc = (PVRSRV_ENV_PER_PROCESS_DATA *)hOsPrivateData;

	/* Linux specific mmap processing */
	LinuxMMapPerProcessDisconnect(psEnvPerProc);

	/* Remove per process /proc entries */
	RemovePerProcessProcDir(psEnvPerProc);

	eError = OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				sizeof(PVRSRV_ENV_PER_PROCESS_DATA),
				hOsPrivateData,
				psEnvPerProc->hBlockAlloc);
	/*not nulling pointer, copy on stack*/

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: OSFreeMem failed (%d)", __FUNCTION__, eError));
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR OSPerProcessSetHandleOptions(PVRSRV_HANDLE_BASE *psHandleBase)
{
	return LinuxMMapPerProcessHandleOptions(psHandleBase);
}

IMG_HANDLE LinuxTerminatingProcessPrivateData(IMG_VOID)
{
	if(!gui32ReleasePID)
		return NULL;
	return PVRSRVPerProcessPrivateData(gui32ReleasePID);
}
