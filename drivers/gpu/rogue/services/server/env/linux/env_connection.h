/*************************************************************************/ /*!
@File
@Title          Server side connection management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Linux specific server side connection management
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

#if !defined(_ENV_CONNECTION_H_)
#define _ENV_CONNECTION_H_

#include <linux/list.h>

#include "handle.h"
#include "pvr_debug.h"

#if defined(SUPPORT_ION)
#include PVR_ANDROID_ION_HEADER
#include "ion_sys.h"
#include "allocmem.h"
#endif

#if defined(SUPPORT_DRM)
#include <drm/drmP.h>
#endif

#if defined(SUPPORT_ION)
#define ION_CLIENT_NAME_SIZE	50

typedef struct _ENV_ION_CONNECTION_DATA_
{
	IMG_CHAR azIonClientName[ION_CLIENT_NAME_SIZE];
	struct ion_device *psIonDev;
	struct ion_client *psIonClient;
	IMG_UINT32 ui32IonClientRefCount;
} ENV_ION_CONNECTION_DATA;
#endif

typedef struct _ENV_CONNECTION_DATA_
{
#if defined(SUPPORT_DRM)
	struct drm_file *psFile;
#else
	struct file *psFile;
#endif
#if defined(SUPPORT_ION)
	ENV_ION_CONNECTION_DATA *psIonData;
#endif
#if defined(SUPPORT_DRM)
	IMG_BOOL bAuthenticated;
#endif
} ENV_CONNECTION_DATA;

#if defined(SUPPORT_ION)
static inline struct ion_client *EnvDataIonClientAcquire(ENV_CONNECTION_DATA *psEnvData)
{
	PVR_ASSERT(psEnvData->psIonData != IMG_NULL);
	PVR_ASSERT(psEnvData->psIonData->psIonClient != IMG_NULL);
	PVR_ASSERT(psEnvData->psIonData->ui32IonClientRefCount > 0);
	psEnvData->psIonData->ui32IonClientRefCount++;
	return psEnvData->psIonData->psIonClient;
}

static inline void EnvDataIonClientRelease(ENV_ION_CONNECTION_DATA *psIonData)
{
	PVR_ASSERT(psIonData != IMG_NULL);
	PVR_ASSERT(psIonData->psIonClient != IMG_NULL);
	PVR_ASSERT(psIonData->ui32IonClientRefCount > 0);
	if (--psIonData->ui32IonClientRefCount == 0)
	{
		ion_client_destroy(psIonData->psIonClient);
		IonDevRelease(psIonData->psIonDev);
		OSFreeMem(psIonData);
		psIonData = IMG_NULL;
	}
}
#endif /* defined(SUPPORT_ION) */

#endif /* !defined(_ENV_CONNECTION_H_) */
