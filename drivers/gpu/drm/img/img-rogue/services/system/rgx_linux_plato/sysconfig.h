/*************************************************************************/ /*!
@File
@Title          System Description Header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This header provides system-specific declarations and macros
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

#if !defined(SYSCONFIG_H)
#define SYSCONFIG_H

#include "pvrsrv_device.h"
#include "rgxdevice.h"
#include "plato_drv.h"

#define SYS_RGX_ACTIVE_POWER_LATENCY_MS (10)
#define MAX_SYSTEMS 32

#if (PLATO_MEMORY_CONFIG == PLATO_MEMORY_LOCAL) || (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID)
static void PlatoLocalCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
					IMG_UINT32 ui32NumOfAddr,
					IMG_DEV_PHYADDR *psDevPAddr,
					IMG_CPU_PHYADDR *psCpuPAddr);

static void PlatoLocalDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
					IMG_UINT32 ui32NumOfAddr,
					IMG_CPU_PHYADDR *psCpuPAddr,
					IMG_DEV_PHYADDR *psDevPAddr);
#endif
#if (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HOST) || (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID)
static void PlatoSystemCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
					IMG_UINT32 ui32NumOfAddr,
					IMG_DEV_PHYADDR *psDevPAddr,
					IMG_CPU_PHYADDR *psCpuPAddr);

static void PlatoSystemDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
					IMG_UINT32 ui32NumOfAddr,
					IMG_CPU_PHYADDR *psCpuPAddr,
					IMG_DEV_PHYADDR *psDevPAddr);
#endif /* (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HOST) || (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID) */

#if (PLATO_MEMORY_CONFIG == PLATO_MEMORY_LOCAL) || (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID)
static PHYS_HEAP_FUNCTIONS gsLocalPhysHeapFuncs = {
	/* pfnCpuPAddrToDevPAddr */
	PlatoLocalCpuPAddrToDevPAddr,
	/* pfnDevPAddrToCpuPAddr */
	PlatoLocalDevPAddrToCpuPAddr,
};
#endif

#if (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HOST) || (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID)
static PHYS_HEAP_FUNCTIONS gsHostPhysHeapFuncs = {
	/* pfnCpuPAddrToDevPAddr */
	PlatoSystemCpuPAddrToDevPAddr,
	/* pfnDevPAddrToCpuPAddr */
	PlatoSystemDevPAddrToCpuPAddr,
};
#endif

#if (PLATO_MEMORY_CONFIG != PLATO_MEMORY_LOCAL) && \
	(PLATO_MEMORY_CONFIG != PLATO_MEMORY_HYBRID) && \
	(PLATO_MEMORY_CONFIG == PLATO_MEMORY_HOST)
#error "PLATO_MEMORY_CONFIG not valid"
#endif

/*****************************************************************************
 * system specific data structures
 *****************************************************************************/

#endif /* !defined(SYSCONFIG_H) */
