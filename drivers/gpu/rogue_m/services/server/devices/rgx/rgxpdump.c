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

/*
 * PVRSRVPDumpSignatureBufferKM
 */
PVRSRV_ERROR PVRSRVPDumpSignatureBufferKM(PVRSRV_DEVICE_NODE	*psDeviceNode,
										  IMG_UINT32			ui32PDumpFlags)
{	
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;

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

#if defined(RGX_FEATURE_RAY_TRACING)
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
#endif

	return PVRSRV_OK;
}

IMG_EXPORT
PVRSRV_ERROR PVRSRVPDumpTraceBufferKM(PVRSRV_DEVICE_NODE	*psDeviceNode,
									  IMG_UINT32			ui32PDumpFlags)
{	
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;

	/* Dump trace buffers */
	PDumpCommentWithFlags(ui32PDumpFlags, "** Dump trace buffers");
	DevmemPDumpSaveToFileVirtual(psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
								offsetof(RGXFWIF_TRACEBUF, sTraceBuf),
								RGXFW_THREAD_NUM * 
								   ( 1 * sizeof(IMG_UINT32) 
								    +RGXFW_TRACE_BUFFER_SIZE * sizeof(IMG_UINT32) 
								    +RGXFW_TRACE_BUFFER_ASSERT_SIZE * sizeof(IMG_CHAR)),
								"out.trace",
								0,
								ui32PDumpFlags);

	/* Dump hwperf buffer */
	PDumpCommentWithFlags(ui32PDumpFlags, "** Dump HWPerf Buffer");
	DevmemPDumpSaveToFileVirtual(psDevInfo->psRGXFWIfHWPerfBufMemDesc,
								 0,
								 psDevInfo->ui32RGXFWIfHWPerfBufSize,
								 "out.hwperf",
								 0,
								 ui32PDumpFlags);
	return PVRSRV_OK;
}

#endif /* PDUMP */

/******************************************************************************
 End of file (rgxpdump.c)
******************************************************************************/

