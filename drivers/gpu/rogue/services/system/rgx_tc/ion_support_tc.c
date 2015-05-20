/*************************************************************************/ /*!
@File           ion_support.c
@Title          Test chip ion support
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

#include PVR_ANDROID_ION_HEADER
#include <linux/slab.h>
#include <linux/version.h>
#include PVR_ANDROID_ION_PRIV_HEADER

#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvr_debug.h"
#include "ion_support.h"
#include "ion_sys.h"
#include "ion_sys_private.h"
#include "lma_heap_ion.h"

struct ion_platform_data gsTCIonConfig = {
	.nr = 1,
	.heaps =
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,39))
#else
		(struct ion_platform_heap [])
#endif
		{
			{
				/* This heap must be first. The base address and size are filled
				   in from data passed down by sysconfig.c. */
				.type = ION_HEAP_TYPE_CUSTOM,
				.name = "tc_local_mem",
				.id = ION_HEAP_TYPE_CUSTOM + 1,
				.base = 0,			/* filled in later */
				.size = 0,			/* filled in later */
			}
		}
};

struct ion_heap **gapsIonHeaps;
struct ion_device *gpsIonDev;
ION_TC_PRIVATE_DATA sPrivateData;

PVRSRV_ERROR IonInit(void *pvPrivateData)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	int i;

	sPrivateData = *(ION_TC_PRIVATE_DATA *)pvPrivateData;

	/* Fill in the heap base and size according to the private data. */
	gsTCIonConfig.heaps[0].base = sPrivateData.uiHeapBase;
	gsTCIonConfig.heaps[0].size = sPrivateData.uiHeapSize;

	gapsIonHeaps = kzalloc(sizeof(struct ion_heap *) * gsTCIonConfig.nr,
						   GFP_KERNEL);
	gpsIonDev = ion_device_create(NULL);
	if (IS_ERR_OR_NULL(gpsIonDev))
	{
		kfree(gapsIonHeaps);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	for (i = 0; i < gsTCIonConfig.nr; i++)
	{
		struct ion_platform_heap *psPlatHeapData = &gsTCIonConfig.heaps[i];

		switch (psPlatHeapData->type)
		{
			case ION_HEAP_TYPE_CUSTOM:
				/* Custom heap: this is used to mean a TC-specific heap,
				   which allocates from local memory. */
				gapsIonHeaps[i] = lma_heap_create(psPlatHeapData);
				break;
			default:
				/* For any other type of heap, hand this to ion to create as
				   appropriate. We don't necessarily need any of these -
				   this just gives us the flexibility to have another kind
				   of heap if necessary. */
				gapsIonHeaps[i] = ion_heap_create(psPlatHeapData);
				break;
		}

		if (IS_ERR_OR_NULL(gapsIonHeaps[i]))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create ion heap '%s'",
					 __func__, psPlatHeapData->name));
			goto err_destroy_heaps;
		}

		ion_device_add_heap(gpsIonDev, gapsIonHeaps[i]);
	}

out:
	return eError;

err_destroy_heaps:
	IonDeinit();
	goto out;
}

struct ion_device *IonDevAcquire(IMG_VOID)
{
	return gpsIonDev;
}

IMG_VOID IonDevRelease(struct ion_device *psIonDev)
{
	/* Nothing to do, sanity check the pointer we're passed back */
	PVR_ASSERT(psIonDev == gpsIonDev);
}

IMG_UINT32 IonPhysHeapID(IMG_VOID)
{
	return sPrivateData.ui32IonPhysHeapID;
}

#if defined(LMA)
IMG_DEV_PHYADDR IonCPUPhysToDevPhys(IMG_CPU_PHYADDR sCPUPhysAddr,
									IMG_UINT32 ui32Offset)
{
	return (IMG_DEV_PHYADDR){
		.uiAddr = sCPUPhysAddr.uiAddr + ui32Offset
			- sPrivateData.sPCIAddrRangeStart.uiAddr,
	};
}
#endif /* defined(LMA) */

IMG_VOID IonDeinit(void)
{
	int i;
	for (i = 0; i < gsTCIonConfig.nr; i++)
		if (gapsIonHeaps[i])
			ion_heap_destroy(gapsIonHeaps[i]);
	kfree(gapsIonHeaps);
	ion_device_destroy(gpsIonDev);
}
