/*************************************************************************/ /*!
@File
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

#include "connection_server.h"
#include "osconnection_server.h"

#include "env_connection.h"
#include "allocmem.h"
#include "pvr_debug.h"

#if defined (SUPPORT_ION)
#include <linux/err.h>
#include PVR_ANDROID_ION_HEADER

/*
	The ion device (the base object for all requests)
	gets created by the system and we acquire it via
	linux specific functions provided by the system layer
*/
#include "ion_sys.h"
#endif

PVRSRV_ERROR OSConnectionPrivateDataInit(IMG_HANDLE *phOsPrivateData, IMG_PVOID pvOSData)
{
	ENV_CONNECTION_DATA *psEnvConnection;
#if defined(SUPPORT_ION)
	ENV_ION_CONNECTION_DATA *psIonConnection;
#endif

	*phOsPrivateData = OSAllocMem(sizeof(ENV_CONNECTION_DATA));

	if (*phOsPrivateData == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: OSAllocMem failed", __FUNCTION__));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psEnvConnection = (ENV_CONNECTION_DATA *)*phOsPrivateData;
	OSMemSet(psEnvConnection, 0, sizeof(*psEnvConnection));

	/* Save the pointer to our struct file */
	psEnvConnection->psFile = pvOSData;

#if defined(SUPPORT_ION)
	psIonConnection = (ENV_ION_CONNECTION_DATA *)OSAllocMem(sizeof(ENV_ION_CONNECTION_DATA));
	if (psIonConnection == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: OSAllocMem failed", __FUNCTION__));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet(psIonConnection, 0, sizeof(*psIonConnection));
	psEnvConnection->psIonData = psIonConnection;
	/*
		We can have more then one connection per process so we need more then
		the PID to have a unique name
	*/
	psEnvConnection->psIonData->psIonDev = IonDevAcquire();
	OSSNPrintf(psEnvConnection->psIonData->azIonClientName, ION_CLIENT_NAME_SIZE, "pvr_ion_client-%p-%d", *phOsPrivateData, OSGetCurrentProcessID());
	psEnvConnection->psIonData->psIonClient =
		ion_client_create(psEnvConnection->psIonData->psIonDev,
						  psEnvConnection->psIonData->azIonClientName);
 
	if (IS_ERR_OR_NULL(psEnvConnection->psIonData->psIonClient))
	{
		PVR_DPF((PVR_DBG_ERROR, "OSConnectionPrivateDataInit: Couldn't create "
								"ion client for per connection data"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	psEnvConnection->psIonData->ui32IonClientRefCount = 1;
#endif /* SUPPORT_ION */
	return PVRSRV_OK;
}

PVRSRV_ERROR OSConnectionPrivateDataDeInit(IMG_HANDLE hOsPrivateData)
{
	ENV_CONNECTION_DATA *psEnvConnection; 

	if (hOsPrivateData == IMG_NULL)
	{
		return PVRSRV_OK;
	}

	psEnvConnection = hOsPrivateData;

#if defined(SUPPORT_ION)
	EnvDataIonClientRelease(psEnvConnection->psIonData);
#endif

	OSFreeMem(hOsPrivateData);
	/*not nulling pointer, copy on stack*/

	return PVRSRV_OK;
}
