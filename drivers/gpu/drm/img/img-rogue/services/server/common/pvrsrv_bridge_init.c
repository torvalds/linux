/*************************************************************************/ /*!
@File
@Title          PVR Common Bridge Init/Deinit Module (kernel side)
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements common PVR Bridge init/deinit code
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

#include "pvrsrv_bridge_init.h"
#include "srvcore.h"

/* These will go when full bridge gen comes in */
#if defined(PDUMP)
PVRSRV_ERROR InitPDUMPCTRLBridge(void);
void DeinitPDUMPCTRLBridge(void);
PVRSRV_ERROR InitPDUMPBridge(void);
void DeinitPDUMPBridge(void);
PVRSRV_ERROR InitRGXPDUMPBridge(void);
void DeinitRGXPDUMPBridge(void);
#endif
#if defined(SUPPORT_DISPLAY_CLASS)
PVRSRV_ERROR InitDCBridge(void);
void DeinitDCBridge(void);
#endif
PVRSRV_ERROR InitMMBridge(void);
void DeinitMMBridge(void);
#if !defined(EXCLUDE_CMM_BRIDGE)
PVRSRV_ERROR InitCMMBridge(void);
void DeinitCMMBridge(void);
#endif
PVRSRV_ERROR InitPDUMPMMBridge(void);
void DeinitPDUMPMMBridge(void);
PVRSRV_ERROR InitSRVCOREBridge(void);
void DeinitSRVCOREBridge(void);
PVRSRV_ERROR InitSYNCBridge(void);
void DeinitSYNCBridge(void);
#if defined(SUPPORT_DMA_TRANSFER)
PVRSRV_ERROR InitDMABridge(void);
void DeinitDMABridge(void);
#endif

#if defined(SUPPORT_RGX)
PVRSRV_ERROR InitRGXTA3DBridge(void);
void DeinitRGXTA3DBridge(void);
#if defined(SUPPORT_RGXTQ_BRIDGE)
PVRSRV_ERROR InitRGXTQBridge(void);
void DeinitRGXTQBridge(void);
#endif /* defined(SUPPORT_RGXTQ_BRIDGE) */

#if defined(SUPPORT_USC_BREAKPOINT)
PVRSRV_ERROR InitRGXBREAKPOINTBridge(void);
void DeinitRGXBREAKPOINTBridge(void);
#endif
PVRSRV_ERROR InitRGXFWDBGBridge(void);
void DeinitRGXFWDBGBridge(void);
PVRSRV_ERROR InitRGXHWPERFBridge(void);
void DeinitRGXHWPERFBridge(void);
#if !defined(EXCLUDE_RGXREGCONFIG_BRIDGE)
PVRSRV_ERROR InitRGXREGCONFIGBridge(void);
void DeinitRGXREGCONFIGBridge(void);
#endif
PVRSRV_ERROR InitRGXKICKSYNCBridge(void);
void DeinitRGXKICKSYNCBridge(void);
#endif /* SUPPORT_RGX */
PVRSRV_ERROR InitCACHEBridge(void);
void DeinitCACHEBridge(void);
#if defined(SUPPORT_SECURE_EXPORT)
PVRSRV_ERROR InitSMMBridge(void);
void DeinitSMMBridge(void);
#endif
#if !defined(EXCLUDE_HTBUFFER_BRIDGE)
PVRSRV_ERROR InitHTBUFFERBridge(void);
void DeinitHTBUFFERBridge(void);
#endif
PVRSRV_ERROR InitPVRTLBridge(void);
void DeinitPVRTLBridge(void);
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
PVRSRV_ERROR InitRIBridge(void);
void DeinitRIBridge(void);
#endif
PVRSRV_ERROR InitDEVICEMEMHISTORYBridge(void);
void DeinitDEVICEMEMHISTORYBridge(void);
#if defined(SUPPORT_VALIDATION_BRIDGE)
PVRSRV_ERROR InitVALIDATIONBridge(void);
void DeinitVALIDATIONBridge(void);
#endif
#if defined(PVR_TESTING_UTILS)
PVRSRV_ERROR InitTUTILSBridge(void);
void DeinitTUTILSBridge(void);
#endif
PVRSRV_ERROR InitSYNCTRACKINGBridge(void);
void DeinitSYNCTRACKINGBridge(void);
#if defined(SUPPORT_WRAP_EXTMEM)
PVRSRV_ERROR InitMMEXTMEMBridge(void);
void DeinitMMEXTMEMBridge(void);
#endif
#if defined(SUPPORT_FALLBACK_FENCE_SYNC)
PVRSRV_ERROR InitSYNCFALLBACKBridge(void);
void DeinitSYNCFALLBACKBridge(void);
#endif
PVRSRV_ERROR InitRGXTIMERQUERYBridge(void);
void DeinitRGXTIMERQUERYBridge(void);
#if defined(SUPPORT_DI_BRG_IMPL)
PVRSRV_ERROR InitDIBridge(void);
void DeinitDIBridge(void);
#endif

PVRSRV_ERROR
ServerBridgeInit(void)
{
	PVRSRV_ERROR eError;

	BridgeDispatchTableStartOffsetsInit();

	eError = InitSRVCOREBridge();
	PVR_LOG_IF_ERROR(eError, "InitSRVCOREBridge");

	eError = InitSYNCBridge();
	PVR_LOG_IF_ERROR(eError, "InitSYNCBridge");

#if defined(PDUMP)
	eError = InitPDUMPCTRLBridge();
	PVR_LOG_IF_ERROR(eError, "InitPDUMPCTRLBridge");
#endif

	eError = InitMMBridge();
	PVR_LOG_IF_ERROR(eError, "InitMMBridge");

#if !defined(EXCLUDE_CMM_BRIDGE)
	eError = InitCMMBridge();
	PVR_LOG_IF_ERROR(eError, "InitCMMBridge");
#endif

#if defined(PDUMP)
	eError = InitPDUMPMMBridge();
	PVR_LOG_IF_ERROR(eError, "InitPDUMPMMBridge");

	eError = InitPDUMPBridge();
	PVR_LOG_IF_ERROR(eError, "InitPDUMPBridge");
#endif

#if defined(SUPPORT_DISPLAY_CLASS)
	eError = InitDCBridge();
	PVR_LOG_IF_ERROR(eError, "InitDCBridge");
#endif

	eError = InitCACHEBridge();
	PVR_LOG_IF_ERROR(eError, "InitCACHEBridge");

#if defined(SUPPORT_SECURE_EXPORT)
	eError = InitSMMBridge();
	PVR_LOG_IF_ERROR(eError, "InitSMMBridge");
#endif

#if !defined(EXCLUDE_HTBUFFER_BRIDGE)
	eError = InitHTBUFFERBridge();
	PVR_LOG_IF_ERROR(eError, "InitHTBUFFERBridge");
#endif

	eError = InitPVRTLBridge();
	PVR_LOG_IF_ERROR(eError, "InitPVRTLBridge");

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	eError = InitRIBridge();
	PVR_LOG_IF_ERROR(eError, "InitRIBridge");
#endif

#if defined(SUPPORT_VALIDATION_BRIDGE)
	eError = InitVALIDATIONBridge();
	PVR_LOG_IF_ERROR(eError, "InitVALIDATIONBridge");
#endif

#if defined(PVR_TESTING_UTILS)
	eError = InitTUTILSBridge();
	PVR_LOG_IF_ERROR(eError, "InitTUTILSBridge");
#endif

	eError = InitDEVICEMEMHISTORYBridge();
	PVR_LOG_IF_ERROR(eError, "InitDEVICEMEMHISTORYBridge");

	eError = InitSYNCTRACKINGBridge();
	PVR_LOG_IF_ERROR(eError, "InitSYNCTRACKINGBridge");

#if defined(SUPPORT_DMA_TRANSFER)
	eError = InitDMABridge();
	PVR_LOG_IF_ERROR(eError, "InitDMABridge");
#endif

#if defined(SUPPORT_RGX)

#if defined(SUPPORT_RGXTQ_BRIDGE)
	eError = InitRGXTQBridge();
	PVR_LOG_IF_ERROR(eError, "InitRGXTQBridge");
#endif /* defined(SUPPORT_RGXTQ_BRIDGE) */

	eError = InitRGXTA3DBridge();
	PVR_LOG_IF_ERROR(eError, "InitRGXTA3DBridge");

	#if defined(SUPPORT_USC_BREAKPOINT)
	eError = InitRGXBREAKPOINTBridge();
	PVR_LOG_IF_ERROR(eError, "InitRGXBREAKPOINTBridge");
#endif

	eError = InitRGXFWDBGBridge();
	PVR_LOG_IF_ERROR(eError, "InitRGXFWDBGBridge");

#if defined(PDUMP)
	eError = InitRGXPDUMPBridge();
	PVR_LOG_IF_ERROR(eError, "InitRGXPDUMPBridge");
#endif

	eError = InitRGXHWPERFBridge();
	PVR_LOG_IF_ERROR(eError, "InitRGXHWPERFBridge");

#if !defined(EXCLUDE_RGXREGCONFIG_BRIDGE)
	eError = InitRGXREGCONFIGBridge();
	PVR_LOG_IF_ERROR(eError, "InitRGXREGCONFIGBridge");
#endif

	eError = InitRGXKICKSYNCBridge();
	PVR_LOG_IF_ERROR(eError, "InitRGXKICKSYNCBridge");

	eError = InitRGXTIMERQUERYBridge();
	PVR_LOG_IF_ERROR(eError, "InitRGXTIMERQUERYBridge");

#endif /* SUPPORT_RGX */

#if defined(SUPPORT_WRAP_EXTMEM)
	eError = InitMMEXTMEMBridge();
	PVR_LOG_IF_ERROR(eError, "InitMMEXTMEMBridge");
#endif

#if defined(SUPPORT_FALLBACK_FENCE_SYNC)
	eError = InitSYNCFALLBACKBridge();
	PVR_LOG_IF_ERROR(eError, "InitSYNCFALLBACKBridge");
#endif

#if defined(SUPPORT_DI_BRG_IMPL)
	eError = InitDIBridge();
	PVR_LOG_IF_ERROR(eError, "InitDIBridge");
#endif

	eError = OSPlatformBridgeInit();
	PVR_LOG_IF_ERROR(eError, "OSPlatformBridgeInit");

	return eError;
}

void ServerBridgeDeInit(void)
{
	OSPlatformBridgeDeInit();

#if defined(SUPPORT_DI_BRG_IMPL)
	DeinitDIBridge();
#endif

#if defined(SUPPORT_FALLBACK_FENCE_SYNC)
	DeinitSYNCFALLBACKBridge();
#endif

#if defined(SUPPORT_WRAP_EXTMEM)
	DeinitMMEXTMEMBridge();
#endif

	DeinitSRVCOREBridge();

	DeinitSYNCBridge();

#if defined(PDUMP)
	DeinitPDUMPCTRLBridge();
#endif

	DeinitMMBridge();

#if !defined(EXCLUDE_CMM_BRIDGE)
	DeinitCMMBridge();
#endif

#if defined(PDUMP)
	DeinitPDUMPMMBridge();

	DeinitPDUMPBridge();
#endif

#if defined(PVR_TESTING_UTILS)
	DeinitTUTILSBridge();
#endif

#if defined(SUPPORT_DISPLAY_CLASS)
	DeinitDCBridge();
#endif

	DeinitCACHEBridge();

#if defined(SUPPORT_SECURE_EXPORT)
	DeinitSMMBridge();
#endif

#if !defined(EXCLUDE_HTBUFFER_BRIDGE)
	DeinitHTBUFFERBridge();
#endif

	DeinitPVRTLBridge();

#if defined(SUPPORT_VALIDATION_BRIDGE)
	DeinitVALIDATIONBridge();
#endif

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	DeinitRIBridge();
#endif

	DeinitDEVICEMEMHISTORYBridge();

	DeinitSYNCTRACKINGBridge();

#if defined(SUPPORT_DMA_TRANSFER)
	DeinitDMABridge();
#endif

#if defined(SUPPORT_RGX)

#if defined(SUPPORT_RGXTQ_BRIDGE)
	DeinitRGXTQBridge();
#endif /* defined(SUPPORT_RGXTQ_BRIDGE) */

	DeinitRGXTA3DBridge();

#if defined(SUPPORT_USC_BREAKPOINT)
	DeinitRGXBREAKPOINTBridge();
#endif

	DeinitRGXFWDBGBridge();

#if defined(PDUMP)
	DeinitRGXPDUMPBridge();
#endif

	DeinitRGXHWPERFBridge();

#if !defined(EXCLUDE_RGXREGCONFIG_BRIDGE)
	DeinitRGXREGCONFIGBridge();
#endif

	DeinitRGXKICKSYNCBridge();

	DeinitRGXTIMERQUERYBridge();

#endif /* SUPPORT_RGX */
}
