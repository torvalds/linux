/*************************************************************************/ /*!
@File			rgxpdump.c
@Title          Device specific pdump routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device specific pdump functions
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

#if defined(PDUMP)

#include "devicemem_pdump.h"
#include "rgxpdump.h"
#include "rgx_bvnc_defs_km.h"

/*
 * There are two different set of functions one for META and one for MIPS
 * because the Pdump player does not implement the support for
 * the MIPS MMU yet. So for MIPS builds we cannot use DevmemPDumpSaveToFileVirtual,
 * we have to use DevmemPDumpSaveToFile instead.
 */
static PVRSRV_ERROR _MetaDumpSignatureBufferKM(CONNECTION_DATA * psConnection,
                                               PVRSRV_DEVICE_NODE *psDeviceNode,
                                               IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	/* TA signatures */
	PDumpCommentWithFlags(ui32PDumpFlags, "** Dump TA signatures and checksums Buffer");
	DevmemPDumpSaveToFileVirtual(psDevInfo->psRGXFWSigTAChecksMemDesc,
								 0,
								 psDevInfo->ui32SigTAChecksSize,
								 "out.tasig",
								 0,
								 ui32PDumpFlags);

	/* 3D signatures */
	PDumpCommentWithFlags(ui32PDumpFlags, "** Dump 3D signatures and checksums Buffer");
	DevmemPDumpSaveToFileVirtual(psDevInfo->psRGXFWSig3DChecksMemDesc,
								 0,
								 psDevInfo->ui32Sig3DChecksSize,
								 "out.3dsig",
								 0,
								 ui32PDumpFlags);

	if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_RAY_TRACING_BIT_MASK)
	{
		/* RT signatures */
		PDumpCommentWithFlags(ui32PDumpFlags, "** Dump RTU signatures and checksums Buffer");
		DevmemPDumpSaveToFileVirtual(psDevInfo->psRGXFWSigRTChecksMemDesc,
									 0,
									 psDevInfo->ui32SigRTChecksSize,
									 "out.rtsig",
									 0,
									 ui32PDumpFlags);
		/* SH signatures */
		PDumpCommentWithFlags(ui32PDumpFlags, "** Dump SHG signatures and checksums Buffer");
		DevmemPDumpSaveToFileVirtual(psDevInfo->psRGXFWSigSHChecksMemDesc,
									 0,
									 psDevInfo->ui32SigSHChecksSize,
									 "out.shsig",
									 0,
									 ui32PDumpFlags);
	}

	return PVRSRV_OK;
}
static PVRSRV_ERROR _MetaDumpTraceBufferKM(CONNECTION_DATA * psConnection,
									  PVRSRV_DEVICE_NODE	*psDeviceNode,
									  IMG_UINT32			ui32PDumpFlags)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	IMG_UINT32 		ui32ThreadNum, ui32Size, ui32OutFileOffset;

	PVR_UNREFERENCED_PARAMETER(psConnection);

#if defined(PVRSRV_GPUVIRT_GUESTDRV)
	PVR_UNREFERENCED_PARAMETER(ui32Size);
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	PVR_UNREFERENCED_PARAMETER(ui32ThreadNum);
	PVR_UNREFERENCED_PARAMETER(ui32OutFileOffset);
#else
	/* Dump trace buffers */
	PDumpCommentWithFlags(ui32PDumpFlags, "** Dump trace buffers");
	for(ui32ThreadNum = 0, ui32OutFileOffset = 0; ui32ThreadNum < RGXFW_THREAD_NUM; ui32ThreadNum++)
	{
		/*
		 * Some compilers cannot cope with the use of offsetof() below - the specific problem being the use of
		 * a non-const variable in the expression, which it needs to be const. Typical compiler error produced is
		 * "expression must have a constant value".
		 */
		const IMG_DEVMEM_OFFSET_T uiTraceBufThreadNumOff
		= (IMG_DEVMEM_OFFSET_T)(uintptr_t)&(((RGXFWIF_TRACEBUF *)0)->sTraceBuf[ui32ThreadNum]);

		/* ui32TracePointer tracepointer */
		ui32Size = sizeof(IMG_UINT32);
		DevmemPDumpSaveToFileVirtual(psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
								uiTraceBufThreadNumOff,
								ui32Size,
								"out.trace",
								ui32OutFileOffset,
								ui32PDumpFlags);
		ui32OutFileOffset += ui32Size;

		/* trace buffer */
		ui32Size = RGXFW_TRACE_BUFFER_SIZE * sizeof(IMG_UINT32);
		PVR_ASSERT(psDevInfo->psRGXFWIfTraceBufferMemDesc[ui32ThreadNum]);
		DevmemPDumpSaveToFileVirtual(psDevInfo->psRGXFWIfTraceBufferMemDesc[ui32ThreadNum],
								0, /* 0 offset in the trace buffer mem desc */
								ui32Size,
								"out.trace",
								ui32OutFileOffset,
								ui32PDumpFlags);
		ui32OutFileOffset += ui32Size;
		
		/* assert info buffer */
		ui32Size = RGXFW_TRACE_BUFFER_ASSERT_SIZE * sizeof(IMG_CHAR)
				+ RGXFW_TRACE_BUFFER_ASSERT_SIZE * sizeof(IMG_CHAR)
				+ sizeof(IMG_UINT32);
		DevmemPDumpSaveToFileVirtual(psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
								offsetof(RGXFWIF_TRACEBUF, sTraceBuf) /* move to first element of sTraceBuf */
									+ ui32ThreadNum * sizeof(RGXFWIF_TRACEBUF_SPACE) /* skip required number of sTraceBuf elements */
									+ offsetof(RGXFWIF_TRACEBUF_SPACE, sAssertBuf), /* offset into its sAssertBuf, to be pdumped */
								ui32Size,
								"out.trace",
								ui32OutFileOffset,
								ui32PDumpFlags);
		ui32OutFileOffset += ui32Size;
	}
	
	/* FW HWPerf buffer is always allocated when PDUMP is defined, irrespective of HWPerf events being enabled/disabled */
	PVR_ASSERT(psDevInfo->psRGXFWIfHWPerfBufMemDesc);

	/* Dump hwperf buffer */
	PDumpCommentWithFlags(ui32PDumpFlags, "** Dump HWPerf Buffer");
	DevmemPDumpSaveToFileVirtual(psDevInfo->psRGXFWIfHWPerfBufMemDesc,
								 0,
								 psDevInfo->ui32RGXFWIfHWPerfBufSize,
								 "out.hwperf",
								 0,
								 ui32PDumpFlags);
#endif

	return PVRSRV_OK;

}


static PVRSRV_ERROR _MipsDumpSignatureBufferKM(CONNECTION_DATA * psConnection,
                                               PVRSRV_DEVICE_NODE *psDeviceNode,
                                               IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	/* TA signatures */
	PDumpCommentWithFlags(ui32PDumpFlags, "** Dump TA signatures and checksums Buffer");

	DevmemPDumpSaveToFile(psDevInfo->psRGXFWSigTAChecksMemDesc,
								 0,
								 psDevInfo->ui32SigTAChecksSize,
								 "out.tasig",
								 0);

	/* 3D signatures */
	PDumpCommentWithFlags(ui32PDumpFlags, "** Dump 3D signatures and checksums Buffer");
	DevmemPDumpSaveToFile(psDevInfo->psRGXFWSig3DChecksMemDesc,
								 0,
								 psDevInfo->ui32Sig3DChecksSize,
								 "out.3dsig",
								 0);

	if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_RAY_TRACING_BIT_MASK)
	{
		/* RT signatures */
		PDumpCommentWithFlags(ui32PDumpFlags, "** Dump RTU signatures and checksums Buffer");
		DevmemPDumpSaveToFile(psDevInfo->psRGXFWSigRTChecksMemDesc,
									 0,
									 psDevInfo->ui32SigRTChecksSize,
									 "out.rtsig",
									 0);

		/* SH signatures */
		PDumpCommentWithFlags(ui32PDumpFlags, "** Dump SHG signatures and checksums Buffer");
		DevmemPDumpSaveToFile(psDevInfo->psRGXFWSigSHChecksMemDesc,
									 0,
									 psDevInfo->ui32SigSHChecksSize,
									 "out.shsig",
									 0);
	}

	return PVRSRV_OK;

}

static PVRSRV_ERROR _MipsDumpTraceBufferKM(CONNECTION_DATA *psConnection,
                                           PVRSRV_DEVICE_NODE *psDeviceNode,
                                           IMG_UINT32 ui32PDumpFlags)
{
#if defined(PVRSRV_GPUVIRT_GUESTDRV)
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
#else

	IMG_UINT32 		ui32ThreadNum, ui32Size, ui32OutFileOffset;
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	PVR_UNREFERENCED_PARAMETER(psConnection);
	/* Dump trace buffers */
	PDumpCommentWithFlags(ui32PDumpFlags, "** Dump trace buffers");
	for(ui32ThreadNum = 0, ui32OutFileOffset = 0; ui32ThreadNum < RGXFW_THREAD_NUM; ui32ThreadNum++)
	{
		/*
		 * Some compilers cannot cope with the use of offsetof() below - the specific problem being the use of
		 * a non-const variable in the expression, which it needs to be const. Typical compiler error produced is
		 * "expression must have a constant value".
		 */
		const IMG_DEVMEM_OFFSET_T uiTraceBufOff
		= (IMG_DEVMEM_OFFSET_T)(uintptr_t)&(((RGXFWIF_TRACEBUF *)0)->sTraceBuf[ui32ThreadNum]);

		/* Same again... */
		const IMG_DEVMEM_OFFSET_T uiTraceBufSpaceAssertBufOff
		= (IMG_DEVMEM_OFFSET_T)(uintptr_t)&(((RGXFWIF_TRACEBUF_SPACE *)0)->sAssertBuf);

		/* ui32TracePointer tracepointer */
		ui32Size = sizeof(IMG_UINT32);
		DevmemPDumpSaveToFile(psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
								uiTraceBufOff,
								ui32Size,
								"out.trace",
								ui32OutFileOffset);
		ui32OutFileOffset += ui32Size;

		/* trace buffer */
		ui32Size = RGXFW_TRACE_BUFFER_SIZE * sizeof(IMG_UINT32);
		PVR_ASSERT(psDevInfo->psRGXFWIfTraceBufferMemDesc[ui32ThreadNum]);
		DevmemPDumpSaveToFile(psDevInfo->psRGXFWIfTraceBufferMemDesc[ui32ThreadNum],
								0, /* 0 offset in the trace buffer mem desc */
								ui32Size,
								"out.trace",
								ui32OutFileOffset);
		ui32OutFileOffset += ui32Size;
		
		/* assert info buffer */
		ui32Size = RGXFW_TRACE_BUFFER_ASSERT_SIZE * sizeof(IMG_CHAR)
				+ RGXFW_TRACE_BUFFER_ASSERT_SIZE * sizeof(IMG_CHAR)
				+ sizeof(IMG_UINT32);
		DevmemPDumpSaveToFile(psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
								uiTraceBufOff + uiTraceBufSpaceAssertBufOff,
								ui32Size,
								"out.trace",
								ui32OutFileOffset);
		ui32OutFileOffset += ui32Size;
	}

	/* Dump hwperf buffer */
	PDumpCommentWithFlags(ui32PDumpFlags, "** Dump HWPerf Buffer");
	DevmemPDumpSaveToFile(psDevInfo->psRGXFWIfHWPerfBufMemDesc,
								 0,
								 psDevInfo->ui32RGXFWIfHWPerfBufSize,
								 "out.hwperf",
								 0);
#endif

	return PVRSRV_OK;

}


/*
 * PVRSRVPDumpSignatureBufferKM
 */
PVRSRV_ERROR PVRSRVPDumpSignatureBufferKM(CONNECTION_DATA * psConnection,
                                          PVRSRV_DEVICE_NODE	*psDeviceNode,
                                          IMG_UINT32			ui32PDumpFlags)
{	
	if( (psDeviceNode->pfnCheckDeviceFeature) && \
			psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_MIPS_BIT_MASK))
	{
		return _MipsDumpSignatureBufferKM(psConnection,
										  psDeviceNode,
										  ui32PDumpFlags);
	}
	else
	{
		return _MetaDumpSignatureBufferKM(psConnection,
											  psDeviceNode,
											  ui32PDumpFlags);
	}
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVPDumpTraceBufferKM(CONNECTION_DATA *psConnection,
                                      PVRSRV_DEVICE_NODE *psDeviceNode,
                                      IMG_UINT32 ui32PDumpFlags)
{	
	if( (psDeviceNode->pfnCheckDeviceFeature) && \
			psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_MIPS_BIT_MASK))
	{
		return _MipsDumpTraceBufferKM(psConnection, psDeviceNode, ui32PDumpFlags);
	}else
	{
		return _MetaDumpTraceBufferKM(psConnection, psDeviceNode, ui32PDumpFlags);
	}
}

#endif /* PDUMP */

/******************************************************************************
 End of file (rgxpdump.c)
******************************************************************************/

