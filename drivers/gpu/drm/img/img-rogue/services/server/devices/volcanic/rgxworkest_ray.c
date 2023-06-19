/*************************************************************************/ /*!
@File           rgxworkest_ray.c
@Title          RGX Workload Estimation Functionality for ray datamaster
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Kernel mode workload estimation functionality for ray datamaster.
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

#include "rgxdevice.h"
#include "rgxworkest.h"
#include "rgxworkest_ray.h"
#include "rgxfwutils.h"
#include "rgxpdvfs.h"
#include "rgx_options.h"
#include "device.h"
#include "hash.h"
#include "pvr_debug.h"


static IMG_BOOL WorkEstHashCompareRay(size_t uKeySize, void *pKey1, void *pKey2)
{
	RGX_WORKLOAD *psWorkload1;
	RGX_WORKLOAD *psWorkload2;
	PVR_UNREFERENCED_PARAMETER(uKeySize);

	if (pKey1 && pKey2)
	{
		psWorkload1 = *((RGX_WORKLOAD **)pKey1);
		psWorkload2 = *((RGX_WORKLOAD **)pKey2);

		PVR_ASSERT(psWorkload1);
		PVR_ASSERT(psWorkload2);

		if (psWorkload1->sRay.ui32DispatchSize == psWorkload2->sRay.ui32DispatchSize &&
		    psWorkload1->sRay.ui32AccStructSize == psWorkload2->sRay.ui32AccStructSize)
		{
			/* This is added to allow this memory to be freed */
			*(uintptr_t*)pKey2 = *(uintptr_t*)pKey1;
			return IMG_TRUE;
		}
	}

	return IMG_FALSE;
}

static IMG_UINT32 WorkEstHashFuncRay(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen)
{
	RGX_WORKLOAD *psWorkload = *((RGX_WORKLOAD**)pKey);
	IMG_UINT32 ui32HashKey = 0;
	PVR_UNREFERENCED_PARAMETER(uHashTabLen);
	PVR_UNREFERENCED_PARAMETER(uKeySize);

	/* Hash key predicated on transfer src/dest attributes */
	ui32HashKey += _WorkEstDoHash(psWorkload->sRay.ui32DispatchSize);
	ui32HashKey += _WorkEstDoHash(psWorkload->sRay.ui32AccStructSize);

	return ui32HashKey;
}

void WorkEstInitRay(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData)
{
	_WorkEstInit(psDevInfo,
		&psWorkEstData->uWorkloadMatchingData.sRay.sDataRDM,
		(HASH_FUNC *)WorkEstHashFuncRay,
		(HASH_KEY_COMP *)WorkEstHashCompareRay);
}

void WorkEstDeInitRay(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData)
{
	_WorkEstDeInit(psDevInfo, &psWorkEstData->uWorkloadMatchingData.sRay.sDataRDM);
}
