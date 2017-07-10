/*************************************************************************/ /*!
@File
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

#ifndef __PCI_SUPPORT_H__
#define __PCI_SUPPORT_H__

#include "img_types.h"
#include "pvrsrv_error.h"

#if defined(LINUX)
#include <linux/pci.h>
#define TO_PCI_COOKIE(dev) to_pci_dev((struct device *)(dev))
#else
#define TO_PCI_COOKIE(dev) (dev)
#endif

typedef enum _HOST_PCI_INIT_FLAGS_
{
	HOST_PCI_INIT_FLAG_BUS_MASTER	= 0x00000001,
	HOST_PCI_INIT_FLAG_MSI		= 0x00000002,
	HOST_PCI_INIT_FLAG_FORCE_I32 	= 0x7fffffff
} HOST_PCI_INIT_FLAGS;

struct _PVRSRV_PCI_DEV_OPAQUE_STRUCT_;
typedef struct _PVRSRV_PCI_DEV_OPAQUE_STRUCT_ *PVRSRV_PCI_DEV_HANDLE;

PVRSRV_PCI_DEV_HANDLE OSPCIAcquireDev(IMG_UINT16 ui16VendorID, IMG_UINT16 ui16DeviceID, HOST_PCI_INIT_FLAGS eFlags);
PVRSRV_PCI_DEV_HANDLE OSPCISetDev(void *pvPCICookie, HOST_PCI_INIT_FLAGS eFlags);
PVRSRV_ERROR OSPCIReleaseDev(PVRSRV_PCI_DEV_HANDLE hPVRPCI);
PVRSRV_ERROR OSPCIDevID(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT16 *pui16DeviceID);
PVRSRV_ERROR OSPCIIRQ(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 *pui32IRQ);
IMG_UINT64 OSPCIAddrRangeLen(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index);
IMG_UINT64 OSPCIAddrRangeStart(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index);
IMG_UINT64 OSPCIAddrRangeEnd(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index);
PVRSRV_ERROR OSPCIRequestAddrRange(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index);
PVRSRV_ERROR OSPCIReleaseAddrRange(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index);
PVRSRV_ERROR OSPCIRequestAddrRegion(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index, IMG_UINT64 uiOffset, IMG_UINT64 uiLength);
PVRSRV_ERROR OSPCIReleaseAddrRegion(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index, IMG_UINT64 uiOffset, IMG_UINT64 uiLength);
PVRSRV_ERROR OSPCISuspendDev(PVRSRV_PCI_DEV_HANDLE hPVRPCI);
PVRSRV_ERROR OSPCIResumeDev(PVRSRV_PCI_DEV_HANDLE hPVRPCI);

#if defined(CONFIG_MTRR)
PVRSRV_ERROR OSPCIClearResourceMTRRs(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index);
#else
static inline PVRSRV_ERROR OSPCIClearResourceMTRRs(PVRSRV_PCI_DEV_HANDLE hPVRPCI, IMG_UINT32 ui32Index)
{
	PVR_UNREFERENCED_PARAMETER(hPVRPCI);
	PVR_UNREFERENCED_PARAMETER(ui32Index);
	return PVRSRV_OK;
}
#endif

#endif /* __PCI_SUPPORT_H__ */
