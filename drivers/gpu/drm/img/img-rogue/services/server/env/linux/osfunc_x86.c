/*************************************************************************/ /*!
@File
@Title          x86 specific OS functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Processor specific OS functions
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

#include <linux/smp.h>
#include <linux/uaccess.h>

#include "pvrsrv_error.h"
#include "img_types.h"
#include "img_defs.h"
#include "osfunc.h"
#include "pvr_debug.h"

static void x86_flush_cache_range(const void *pvStart, const void *pvEnd)
{
	IMG_BYTE *pbStart = (IMG_BYTE *)pvStart;
	IMG_BYTE *pbEnd = (IMG_BYTE *)pvEnd;
	IMG_BYTE *pbBase;

	pbEnd = (IMG_BYTE *)PVR_ALIGN((uintptr_t)pbEnd,
	                              (uintptr_t)boot_cpu_data.x86_clflush_size);

	mb();

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,168))
	__uaccess_begin();
#endif

	for (pbBase = pbStart; pbBase < pbEnd; pbBase += boot_cpu_data.x86_clflush_size)
	{
		clflush(pbBase);
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,168))
	__uaccess_end();
#endif

	mb();
}

void OSCPUCacheFlushRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
                            void *pvVirtStart,
                            void *pvVirtEnd,
                            IMG_CPU_PHYADDR sCPUPhysStart,
                            IMG_CPU_PHYADDR sCPUPhysEnd)
{
	PVR_UNREFERENCED_PARAMETER(psDevNode);
	PVR_UNREFERENCED_PARAMETER(sCPUPhysStart);
	PVR_UNREFERENCED_PARAMETER(sCPUPhysEnd);

	x86_flush_cache_range(pvVirtStart, pvVirtEnd);
}

void OSCPUCacheCleanRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
                            void *pvVirtStart,
                            void *pvVirtEnd,
                            IMG_CPU_PHYADDR sCPUPhysStart,
                            IMG_CPU_PHYADDR sCPUPhysEnd)
{
	PVR_UNREFERENCED_PARAMETER(psDevNode);
	PVR_UNREFERENCED_PARAMETER(sCPUPhysStart);
	PVR_UNREFERENCED_PARAMETER(sCPUPhysEnd);

	/* No clean feature on x86 */
	x86_flush_cache_range(pvVirtStart, pvVirtEnd);
}

void OSCPUCacheInvalidateRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
                                 void *pvVirtStart,
                                 void *pvVirtEnd,
                                 IMG_CPU_PHYADDR sCPUPhysStart,
                                 IMG_CPU_PHYADDR sCPUPhysEnd)
{
	PVR_UNREFERENCED_PARAMETER(psDevNode);
	PVR_UNREFERENCED_PARAMETER(sCPUPhysStart);
	PVR_UNREFERENCED_PARAMETER(sCPUPhysEnd);

	/* No invalidate-only support */
	x86_flush_cache_range(pvVirtStart, pvVirtEnd);
}

OS_CACHE_OP_ADDR_TYPE OSCPUCacheOpAddressType(void)
{
	return OS_CACHE_OP_ADDR_TYPE_VIRTUAL;
}

void OSUserModeAccessToPerfCountersEn(void)
{
	/* Not applicable to x86 architecture. */
}

IMG_BOOL OSIsWriteCombineUnalignedSafe(void)
{
	return IMG_TRUE;
}
