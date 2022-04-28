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
PVRSRV_ERROR DeinitPDUMPCTRLBridge(void);
PVRSRV_ERROR InitPDUMPBridge(void);
PVRSRV_ERROR DeinitPDUMPBridge(void);
PVRSRV_ERROR InitRGXPDUMPBridge(void);
PVRSRV_ERROR DeinitRGXPDUMPBridge(void);
#endif
#if defined(SUPPORT_DISPLAY_CLASS)
PVRSRV_ERROR InitDCBridge(void);
PVRSRV_ERROR DeinitDCBridge(void);
#endif
PVRSRV_ERROR InitMMBridge(void);
PVRSRV_ERROR DeinitMMBridge(void);
#if !defined(EXCLUDE_CMM_BRIDGE)
PVRSRV_ERROR InitCMMBridge(void);
PVRSRV_ERROR DeinitCMMBridge(void);
#endif
PVRSRV_ERROR InitPDUMPMMBridge(void);
PVRSRV_ERROR DeinitPDUMPMMBridge(void);
PVRSRV_ERROR InitSRVCOREBridge(void);
PVRSRV_ERROR DeinitSRVCOREBridge(void);
PVRSRV_ERROR InitSYNCBridge(void);
PVRSRV_ERROR DeinitSYNCBridge(void);
#if defined(SUPPORT_DMA_TRANSFER)
PVRSRV_ERROR InitDMABridge(void);
PVRSRV_ERROR DeinitDMABridge(void);
#endif

#if defined(SUPPORT_RGX)
PVRSRV_ERROR InitRGXTA3DBridge(void);
PVRSRV_ERROR DeinitRGXTA3DBridge(void);
#if defined(SUPPORT_RGXTQ_BRIDGE)
PVRSRV_ERROR InitRGXTQBridge(void);
PVRSRV_ERROR DeinitRGXTQBridge(void);
#endif /* defined(SUPPORT_RGXTQ_BRIDGE) */
PVRSRV_ERROR InitRGXTQ2Bridge(void);
PVRSRV_ERROR DeinitRGXTQ2Bridge(void);
PVRSRV_ERROR InitRGXCMPBridge(void);
PVRSRV_ERROR DeinitRGXCMPBridge(void);
#if defined(SUPPORT_USC_BREAKPOINT)
PVRSRV_ERROR InitRGXBREAKPOINTBridge(void);
PVRSRV_ERROR DeinitRGXBREAKPOINTBridge(void);
#endif
PVRSRV_ERROR InitRGXFWDBGBridge(void);
PVRSRV_ERROR DeinitRGXFWDBGBridge(void);
PVRSRV_ERROR InitRGXHWPERFBridge(void);
PVRSRV_ERROR DeinitRGXHWPERFBridge(void);
#if !defined(EXCLUDE_RGXREGCONFIG_BRIDGE)
PVRSRV_ERROR InitRGXREGCONFIGBridge(void);
PVRSRV_ERROR DeinitRGXREGCONFIGBridge(void);
#endif
PVRSRV_ERROR InitRGXKICKSYNCBridge(void);
PVRSRV_ERROR DeinitRGXKICKSYNCBridge(void);
PVRSRV_ERROR InitRGXSIGNALSBridge(void);
PVRSRV_ERROR DeinitRGXSIGNALSBridge(void);
#endif /* SUPPORT_RGX */
PVRSRV_ERROR InitCACHEBridge(void);
PVRSRV_ERROR DeinitCACHEBridge(void);
#if defined(SUPPORT_SECURE_EXPORT)
PVRSRV_ERROR InitSMMBridge(void);
PVRSRV_ERROR DeinitSMMBridge(void);
#endif
#if !defined(EXCLUDE_HTBUFFER_BRIDGE)
PVRSRV_ERROR InitHTBUFFERBridge(void);
PVRSRV_ERROR DeinitHTBUFFERBridge(void);
#endif
PVRSRV_ERROR InitPVRTLBridge(void);
PVRSRV_ERROR DeinitPVRTLBridge(void);
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
PVRSRV_ERROR InitRIBridge(void);
PVRSRV_ERROR DeinitRIBridge(void);
#endif
PVRSRV_ERROR InitDEVICEMEMHISTORYBridge(void);
PVRSRV_ERROR DeinitDEVICEMEMHISTORYBridge(void);
#if defined(SUPPORT_VALIDATION_BRIDGE)
PVRSRV_ERROR InitVALIDATIONBridge(void);
PVRSRV_ERROR DeinitVALIDATIONBridge(void);
#endif
#if defined(PVR_TESTING_UTILS)
PVRSRV_ERROR InitTUTILSBridge(void);
PVRSRV_ERROR DeinitTUTILSBridge(void);
#endif
PVRSRV_ERROR InitSYNCTRACKINGBridge(void);
PVRSRV_ERROR DeinitSYNCTRACKINGBridge(void);
#if defined(SUPPORT_WRAP_EXTMEM)
PVRSRV_ERROR InitMMEXTMEMBridge(void);
PVRSRV_ERROR DeinitMMEXTMEMBridge(void);
#endif
#if defined(SUPPORT_FALLBACK_FENCE_SYNC)
PVRSRV_ERROR InitSYNCFALLBACKBridge(void);
PVRSRV_ERROR DeinitSYNCFALLBACKBridge(void);
#endif
PVRSRV_ERROR InitRGXTIMERQUERYBridge(void);
PVRSRV_ERROR DeinitRGXTIMERQUERYBridge(void);
#if defined(SUPPORT_DI_BRG_IMPL)
PVRSRV_ERROR InitDIBridge(void);
PVRSRV_ERROR DeinitDIBridge(void);
#endif

PVRSRV_ERROR InitRGXRAYBridge(void);
PVRSRV_ERROR DeinitRGXRAYBridge(void);

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

PVRSRV_ERROR
ServerBridgeDeInit(void)
{
	PVRSRV_ERROR eError;

	eError = OSPlatformBridgeDeInit();
	PVR_LOG_IF_ERROR(eError, "OSPlatformBridgeDeInit");

#if defined(SUPPORT_DI_BRG_IMPL)
	eError = DeinitDIBridge();
	PVR_LOG_IF_ERROR(eError, "DeinitDIBridge");
#endif

#if defined(SUPPORT_FALLBACK_FENCE_SYNC)
	eError = DeinitSYNCFALLBACKBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitSYNCFALLBACKBridge");
#endif

#if defined(SUPPORT_WRAP_EXTMEM)
	eError = DeinitMMEXTMEMBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitMMEXTMEMBridge");
#endif

	eError = DeinitSRVCOREBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitSRVCOREBridge");

	eError = DeinitSYNCBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitSYNCBridge");

#if defined(PDUMP)
	eError = DeinitPDUMPCTRLBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitPDUMPCTRLBridge");
#endif

	eError = DeinitMMBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitMMBridge");

#if !defined(EXCLUDE_CMM_BRIDGE)
	eError = DeinitCMMBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitCMMBridge");
#endif

#if defined(PDUMP)
	eError = DeinitPDUMPMMBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitPDUMPMMBridge");

	eError = DeinitPDUMPBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitPDUMPBridge");
#endif

#if defined(PVR_TESTING_UTILS)
	eError = DeinitTUTILSBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitTUTILSBridge");
#endif

#if defined(SUPPORT_DISPLAY_CLASS)
	eError = DeinitDCBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitDCBridge");
#endif

	eError = DeinitCACHEBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitCACHEBridge");

#if defined(SUPPORT_SECURE_EXPORT)
	eError = DeinitSMMBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitSMMBridge");
#endif

#if !defined(EXCLUDE_HTBUFFER_BRIDGE)
	eError = DeinitHTBUFFERBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitHTBUFFERBridge");
#endif

	eError = DeinitPVRTLBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitPVRTLBridge");

#if defined(SUPPORT_VALIDATION_BRIDGE)
	eError = DeinitVALIDATIONBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitVALIDATIONBridge");
#endif

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	eError = DeinitRIBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRIBridge");
#endif

	eError = DeinitDEVICEMEMHISTORYBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitDEVICEMEMHISTORYBridge");

	eError = DeinitSYNCTRACKINGBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitSYNCTRACKINGBridge");

#if defined(SUPPORT_DMA_TRANSFER)
	eError = DeinitDMABridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitDMABridge");
#endif

#if defined(SUPPORT_RGX)

#if defined(SUPPORT_RGXTQ_BRIDGE)
	eError = DeinitRGXTQBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRGXTQBridge");
#endif /* defined(SUPPORT_RGXTQ_BRIDGE) */

	eError = DeinitRGXTA3DBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRGXTA3DBridge");

#if defined(SUPPORT_USC_BREAKPOINT)
	eError = DeinitRGXBREAKPOINTBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRGXBREAKPOINTBridge");
#endif

	eError = DeinitRGXFWDBGBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRGXFWDBGBridge");

#if defined(PDUMP)
	eError = DeinitRGXPDUMPBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRGXPDUMPBridge");
#endif

	eError = DeinitRGXHWPERFBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRGXHWPERFBridge");

#if !defined(EXCLUDE_RGXREGCONFIG_BRIDGE)
	eError = DeinitRGXREGCONFIGBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRGXREGCONFIGBridge");
#endif

	eError = DeinitRGXKICKSYNCBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRGXKICKSYNCBridge");

	eError = DeinitRGXTIMERQUERYBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRGXTIMERQUERYBridge");

#endif /* SUPPORT_RGX */

	return eError;
}

#if defined(SUPPORT_RGX)
PVRSRV_ERROR
DeviceDepBridgeInit(IMG_UINT64 ui64Features)
{
	PVRSRV_ERROR eError;

#if defined(RGX_FEATURE_COMPUTE_BIT_MASK)
	if (ui64Features & RGX_FEATURE_COMPUTE_BIT_MASK)
#endif
	{
		eError = InitRGXCMPBridge();
		PVR_LOG_RETURN_IF_ERROR(eError, "InitRGXCMPBridge");
	}

	if (ui64Features & RGX_FEATURE_SIGNAL_SNOOPING_BIT_MASK)
	{
		eError = InitRGXSIGNALSBridge();
		PVR_LOG_RETURN_IF_ERROR(eError, "InitRGXCMPBridge");
	}

#if defined(RGX_FEATURE_FASTRENDER_DM_BIT_MASK)
	if (ui64Features & RGX_FEATURE_FASTRENDER_DM_BIT_MASK)
#endif
	{
		eError = InitRGXTQ2Bridge();
		PVR_LOG_RETURN_IF_ERROR(eError, "InitRGXTQ2Bridge");
	}

#if defined(RGX_FEATURE_RAY_TRACING_ARCH_MAX_VALUE_IDX)
	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, RAY_TRACING_ARCH))
#endif
	{
#if defined(SUPPORT_RGXRAY_BRIDGE)
		eError = InitRGXRAYBridge();
		PVR_LOG_IF_ERROR(eError, "InitRGXRAYBridge");
#endif
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR
DeviceDepBridgeDeInit(IMG_UINT64 ui64Features)
{
	PVRSRV_ERROR eError;

#if defined(RGX_FEATURE_COMPUTE_BIT_MASK)
	if (ui64Features & RGX_FEATURE_COMPUTE_BIT_MASK)
#endif
	{
		eError = DeinitRGXCMPBridge();
		PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRGXCMPBridge");
	}

	if (ui64Features & RGX_FEATURE_SIGNAL_SNOOPING_BIT_MASK)
	{
		eError = DeinitRGXSIGNALSBridge();
		PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRGXSIGNALSBridge");
	}

#if defined(RGX_FEATURE_COMPUTE_BIT_MASK)
	if (ui64Features & RGX_FEATURE_FASTRENDER_DM_BIT_MASK)
#endif
	{
		eError = DeinitRGXTQ2Bridge();
		PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRGXTQ2Bridge");
	}

#if defined(RGX_FEATURE_RAY_TRACING_ARCH_MAX_VALUE_IDX)
	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, RAY_TRACING_ARCH))
#endif
	{
#if defined(SUPPORT_RGXRAY_BRIDGE)
		eError = DeinitRGXRAYBridge();
		PVR_LOG_RETURN_IF_ERROR(eError, "DeinitRGXRAYBridge");
#endif
	}

	return PVRSRV_OK;
}
#endif /* SUPPORT_RGX */
