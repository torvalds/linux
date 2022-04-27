/*************************************************************************/ /*!
@File           rgxpdump.c
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
#include "pvrsrv.h"
#include "devicemem_pdump.h"
#include "rgxpdump.h"
#include "rgx_bvnc_defs_km.h"
#include "pdumpdesc.h"

/*
 * There are two different set of functions one for META/RISCV and one for MIPS
 * because the Pdump player does not implement the support for
 * the MIPS MMU yet. So for MIPS builds we cannot use DevmemPDumpSaveToFileVirtual,
 * we have to use DevmemPDumpSaveToFile instead.
 */
static PVRSRV_ERROR _FWDumpSignatureBufferKM(CONNECTION_DATA * psConnection,
                                             PVRSRV_DEVICE_NODE *psDeviceNode,
                                             IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	PDUMPIF("DISABLE_SIGNATURE_BUFFER_DUMP", ui32PDumpFlags);
	PDUMPELSE("DISABLE_SIGNATURE_BUFFER_DUMP", ui32PDumpFlags);

#if defined(SUPPORT_FIRMWARE_GCOV)
	/* Gcov */
	PDumpCommentWithFlags(ui32PDumpFlags, "** Gcov Buffer");
	DevmemPDumpSaveToFileVirtual(psDevInfo->psFirmwareGcovBufferMemDesc,
									 0,
									 psDevInfo->ui32FirmwareGcovSize,
									 "firmware_gcov.img",
									 0,
									 ui32PDumpFlags);
#endif
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

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, TDM_PDS_CHECKSUM))
	{
		/* TDM signatures */
		PDumpCommentWithFlags(ui32PDumpFlags, "** Dump TDM signatures and checksums Buffer");
		DevmemPDumpSaveToFileVirtual(psDevInfo->psRGXFWSigTDM2DChecksMemDesc,
									 0,
									 psDevInfo->ui32SigTDM2DChecksSize,
									 "out.tdmsig",
									 0,
									 ui32PDumpFlags);
	}

	PDUMPFI("DISABLE_SIGNATURE_BUFFER_DUMP", ui32PDumpFlags);

	return PVRSRV_OK;
}
static PVRSRV_ERROR _FWDumpTraceBufferKM(CONNECTION_DATA * psConnection,
										 PVRSRV_DEVICE_NODE	*psDeviceNode,
										 IMG_UINT32			ui32PDumpFlags)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	IMG_UINT32	ui32ThreadNum, ui32Size, ui32OutFileOffset;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);

	/* Dump trace buffers */
	PDumpCommentWithFlags(ui32PDumpFlags, "** Dump trace buffers");
	for (ui32ThreadNum = 0, ui32OutFileOffset = 0; ui32ThreadNum < RGXFW_THREAD_NUM; ui32ThreadNum++)
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

		/* next, dump size of trace buffer in DWords */
		ui32Size = sizeof(IMG_UINT32);
		DevmemPDumpSaveToFileVirtual(psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
								offsetof(RGXFWIF_TRACEBUF, ui32TraceBufSizeInDWords),
								ui32Size,
								"out.trace",
								ui32OutFileOffset,
								ui32PDumpFlags);
		ui32OutFileOffset += ui32Size;

		/* trace buffer */
		ui32Size = psDevInfo->psRGXFWIfTraceBufCtl->ui32TraceBufSizeInDWords * sizeof(IMG_UINT32);
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

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, TDM_PDS_CHECKSUM))
	{
		/* TDM signatures */
		PDumpCommentWithFlags(ui32PDumpFlags, "** Dump TDM signatures and checksums Buffer");
		DevmemPDumpSaveToFile(psDevInfo->psRGXFWSigTDM2DChecksMemDesc,
									 0,
									 psDevInfo->ui32SigTDM2DChecksSize,
									 "out.tdmsig",
									 0);
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR _MipsDumpTraceBufferKM(CONNECTION_DATA *psConnection,
                                           PVRSRV_DEVICE_NODE *psDeviceNode,
                                           IMG_UINT32 ui32PDumpFlags)
{
	IMG_UINT32		ui32ThreadNum, ui32Size, ui32OutFileOffset;
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);

	/* Dump trace buffers */
	PDumpCommentWithFlags(ui32PDumpFlags, "** Dump trace buffers");
	for (ui32ThreadNum = 0, ui32OutFileOffset = 0; ui32ThreadNum < RGXFW_THREAD_NUM; ui32ThreadNum++)
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

		/* next, dump size of trace buffer in DWords */
		ui32Size = sizeof(IMG_UINT32);
		DevmemPDumpSaveToFile(psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
								offsetof(RGXFWIF_TRACEBUF, ui32TraceBufSizeInDWords),
								ui32Size,
								"out.trace",
								ui32OutFileOffset);
		ui32OutFileOffset += ui32Size;

		/* trace buffer */
		ui32Size = psDevInfo->psRGXFWIfTraceBufCtl->ui32TraceBufSizeInDWords * sizeof(IMG_UINT32);
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

	return PVRSRV_OK;

}


/*
 * PVRSRVPDumpSignatureBufferKM
 */
PVRSRV_ERROR PVRSRVPDumpSignatureBufferKM(CONNECTION_DATA * psConnection,
                                          PVRSRV_DEVICE_NODE	*psDeviceNode,
                                          IMG_UINT32			ui32PDumpFlags)
{
	if ((psDeviceNode->pfnCheckDeviceFeature) &&
		 PVRSRV_IS_FEATURE_SUPPORTED(psDeviceNode, MIPS))
	{
		return _MipsDumpSignatureBufferKM(psConnection,
										  psDeviceNode,
										  ui32PDumpFlags);
	}
	else
	{
		return _FWDumpSignatureBufferKM(psConnection,
										psDeviceNode,
										ui32PDumpFlags);
	}
}


#if defined(SUPPORT_VALIDATION)
PVRSRV_ERROR PVRSRVPDumpComputeCRCSignatureCheckKM(CONNECTION_DATA * psConnection,
                                                   PVRSRV_DEVICE_NODE * psDeviceNode,
                                                   IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;
	PVRSRV_ERROR eError;

	if (!(RGX_IS_FEATURE_SUPPORTED(psDevInfo, COMPUTE)))
	{
		return PVRSRV_ERROR_NOT_SUPPORTED;
	}

	/*
	 * Add a PDUMP POLL on the KZ signature check status.
	 */
	if (psDevInfo->ui32ValidationFlags & RGX_VAL_KZ_SIG_CHECK_NOERR_EN)
	{
		PDUMPCOMMENT("Verify KZ Signature: match required");
		eError = PDUMPREGPOL(RGX_PDUMPREG_NAME,
		                     RGX_CR_SCRATCH11,
		                     1U,
		                     0xFFFFFFFF,
		                     ui32PDumpFlags,
		                     PDUMP_POLL_OPERATOR_EQUAL);
	}
	else if (psDevInfo->ui32ValidationFlags & RGX_VAL_KZ_SIG_CHECK_ERR_EN)
	{
		PDUMPCOMMENT("Verify KZ Signature: mismatch required");
		eError = PDUMPREGPOL(RGX_PDUMPREG_NAME,
		                     RGX_CR_SCRATCH11,
		                     2U,
		                     0xFFFFFFFF,
		                     ui32PDumpFlags,
		                     PDUMP_POLL_OPERATOR_EQUAL);
	}
	PVR_UNREFERENCED_PARAMETER(psConnection);

	return PVRSRV_OK;
}
#endif

PVRSRV_ERROR PVRSRVPDumpCRCSignatureCheckKM(CONNECTION_DATA * psConnection,
                                            PVRSRV_DEVICE_NODE * psDeviceNode,
                                            IMG_UINT32 ui32PDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);

	return PVRSRV_OK;
}


/*
 * PVRSRVPDumpValCheckPreCommand
 */
PVRSRV_ERROR PVRSRVPDumpValCheckPreCommandKM(CONNECTION_DATA * psConnection,
                                             PVRSRV_DEVICE_NODE * psDeviceNode,
                                             IMG_UINT32 ui32PDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
	PVR_UNREFERENCED_PARAMETER(psConnection);

	return PVRSRV_OK;
}

/*
 * PVRSRVPDumpValCheckPostCommand
 */
PVRSRV_ERROR PVRSRVPDumpValCheckPostCommandKM(CONNECTION_DATA * psConnection,
                                              PVRSRV_DEVICE_NODE * psDeviceNode,
                                              IMG_UINT32 ui32PDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
	PVR_UNREFERENCED_PARAMETER(psConnection);

	return PVRSRV_OK;
}


PVRSRV_ERROR PVRSRVPDumpTraceBufferKM(CONNECTION_DATA *psConnection,
                                      PVRSRV_DEVICE_NODE *psDeviceNode,
                                      IMG_UINT32 ui32PDumpFlags)
{
	if ((psDeviceNode->pfnCheckDeviceFeature) &&
		 PVRSRV_IS_FEATURE_SUPPORTED(psDeviceNode, MIPS))
	{
		return _MipsDumpTraceBufferKM(psConnection, psDeviceNode, ui32PDumpFlags);
	}
	else
	{
		return _FWDumpTraceBufferKM(psConnection, psDeviceNode, ui32PDumpFlags);
	}
}

PVRSRV_ERROR RGXPDumpPrepareOutputImageDescriptorHdr(PVRSRV_DEVICE_NODE *psDeviceNode,
									IMG_UINT32 ui32HeaderSize,
									IMG_UINT32 ui32DataSize,
									IMG_UINT32 ui32LogicalWidth,
									IMG_UINT32 ui32LogicalHeight,
									IMG_UINT32 ui32PhysicalWidth,
									IMG_UINT32 ui32PhysicalHeight,
									PDUMP_PIXEL_FORMAT ePixFmt,
									IMG_MEMLAYOUT eMemLayout,
									IMG_FB_COMPRESSION eFBCompression,
									const IMG_UINT32 *paui32FBCClearColour,
									PDUMP_FBC_SWIZZLE eFBCSwizzle,
									IMG_PBYTE pbyPDumpImageHdr)
{
	IMG_PUINT32 pui32Word;
	IMG_UINT32 ui32HeaderDataSize;

	/* Validate parameters */
	if (((IMAGE_HEADER_SIZE & ~(HEADER_WORD1_SIZE_CLRMSK >> HEADER_WORD1_SIZE_SHIFT)) != 0) ||
		((IMAGE_HEADER_VERSION & ~(HEADER_WORD1_VERSION_CLRMSK >> HEADER_WORD1_VERSION_SHIFT)) != 0))
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	pui32Word = IMG_OFFSET_ADDR(pbyPDumpImageHdr, 0);
	pui32Word[0] = (IMAGE_HEADER_TYPE << HEADER_WORD0_TYPE_SHIFT);
	pui32Word[1] = (IMAGE_HEADER_SIZE << HEADER_WORD1_SIZE_SHIFT) |
				   (IMAGE_HEADER_VERSION << HEADER_WORD1_VERSION_SHIFT);

	ui32HeaderDataSize = ui32DataSize;
	if (eFBCompression != IMG_FB_COMPRESSION_NONE)
	{
		ui32HeaderDataSize += ui32HeaderSize;
	}
	pui32Word[2] = ui32HeaderDataSize << HEADER_WORD2_DATA_SIZE_SHIFT;

	pui32Word[3] = ui32LogicalWidth << IMAGE_HEADER_WORD3_LOGICAL_WIDTH_SHIFT;
	pui32Word[4] = ui32LogicalHeight << IMAGE_HEADER_WORD4_LOGICAL_HEIGHT_SHIFT;

	pui32Word[5] = ePixFmt << IMAGE_HEADER_WORD5_FORMAT_SHIFT;

	pui32Word[6] = ui32PhysicalWidth << IMAGE_HEADER_WORD6_PHYSICAL_WIDTH_SHIFT;
	pui32Word[7] = ui32PhysicalHeight << IMAGE_HEADER_WORD7_PHYSICAL_HEIGHT_SHIFT;

	pui32Word[8] = IMAGE_HEADER_WORD8_STRIDE_POSITIVE | IMAGE_HEADER_WORD8_BIFTYPE_NONE;

	switch (eMemLayout)
	{
	case IMG_MEMLAYOUT_STRIDED:
		pui32Word[8] |= IMAGE_HEADER_WORD8_TWIDDLING_STRIDED;
		break;
	case IMG_MEMLAYOUT_TWIDDLED:
		pui32Word[8] |= IMAGE_HEADER_WORD8_TWIDDLING_NTWIDDLE;
		break;
	default:
		PVR_DPF((PVR_DBG_ERROR, "Unsupported memory layout - %d", eMemLayout));
		return PVRSRV_ERROR_UNSUPPORTED_MEMORY_LAYOUT;
	}

	pui32Word[9] = 0;
	if (eFBCompression != IMG_FB_COMPRESSION_NONE)
	{
		switch (PVRSRV_GET_DEVICE_FEATURE_VALUE(psDeviceNode, FBCDC_ALGORITHM))
		{
		case 1:
			pui32Word[9] |= IMAGE_HEADER_WORD9_FBCCOMPAT_BASE;
			break;
		case 2:
			pui32Word[9] |= IMAGE_HEADER_WORD9_FBCCOMPAT_V2;
			break;
		case 3:
			pui32Word[9] |= IMAGE_HEADER_WORD9_FBCCOMPAT_V3_0_LAYOUT2;
			break;
		case 4:
			pui32Word[9] |= IMAGE_HEADER_WORD9_FBCCOMPAT_V4;

			if (eFBCompression == IMG_FB_COMPRESSION_DIRECT_LOSSY50_8x8  ||
				eFBCompression == IMG_FB_COMPRESSION_DIRECT_LOSSY50_16x4 ||
				eFBCompression == IMG_FB_COMPRESSION_DIRECT_LOSSY50_32x2)
			{
				pui32Word[9] |= IMAGE_HEADER_WORD9_LOSSY_ON;
			}

			pui32Word[9] |= (eFBCSwizzle << IMAGE_HEADER_WORD9_SWIZZLE_SHIFT) & IMAGE_HEADER_WORD9_SWIZZLE_CLRMSK;

			break;
		case 50:
			pui32Word[9] |= IMAGE_HEADER_WORD9_FBCCOMPAT_TFBC;

			if (eFBCompression == IMG_FB_COMPRESSION_DIRECT_LOSSY25_8x8  ||
				eFBCompression == IMG_FB_COMPRESSION_DIRECT_LOSSY25_16x4 ||
				eFBCompression == IMG_FB_COMPRESSION_DIRECT_LOSSY25_32x2)
			{
				pui32Word[9] |= IMAGE_HEADER_WORD9_LOSSY_25;
			}

			if (eFBCompression == IMG_FB_COMPRESSION_DIRECT_LOSSY50_8x8  ||
				eFBCompression == IMG_FB_COMPRESSION_DIRECT_LOSSY50_16x4 ||
				eFBCompression == IMG_FB_COMPRESSION_DIRECT_LOSSY50_32x2)
			{
				pui32Word[9] |= IMAGE_HEADER_WORD9_LOSSY_50;
			}

			if (eFBCompression == IMG_FB_COMPRESSION_DIRECT_LOSSY75_8x8  ||
				eFBCompression == IMG_FB_COMPRESSION_DIRECT_LOSSY75_16x4 ||
				eFBCompression == IMG_FB_COMPRESSION_DIRECT_LOSSY75_32x2)
			{
				pui32Word[9] |= IMAGE_HEADER_WORD9_LOSSY_75;
			}

			break;
		default:
			PVR_DPF((PVR_DBG_ERROR, "Unsupported algorithm - %d",
					PVRSRV_GET_DEVICE_FEATURE_VALUE(psDeviceNode, FBCDC_ALGORITHM)));
			return PVRSRV_ERROR_NOT_ENABLED;
		}
	}

	switch (GET_FBCDC_BLOCK_TYPE(eFBCompression))
	{
	case IMG_FB_COMPRESSION_NONE:
		break;
	case IMG_FB_COMPRESSION_DIRECT_8x8:
		pui32Word[8] |= IMAGE_HEADER_WORD8_FBCTYPE_8X8;
		pui32Word[9] |= IMAGE_HEADER_WORD9_FBCDECOR_ENABLE;
		break;
	case IMG_FB_COMPRESSION_DIRECT_16x4:
		pui32Word[8] |= IMAGE_HEADER_WORD8_FBCTYPE_16x4;
		pui32Word[9] |= IMAGE_HEADER_WORD9_FBCDECOR_ENABLE;
		break;
	case IMG_FB_COMPRESSION_DIRECT_32x2:
		pui32Word[9] |= IMAGE_HEADER_WORD9_FBCDECOR_ENABLE;
		break;
	default:
		PVR_DPF((PVR_DBG_ERROR, "Unsupported compression mode - %d", eFBCompression));
		return PVRSRV_ERROR_UNSUPPORTED_FB_COMPRESSION_MODE;
	}

	pui32Word[10] = paui32FBCClearColour[0];
	pui32Word[11] = paui32FBCClearColour[1];
	pui32Word[12] = paui32FBCClearColour[2];
	pui32Word[13] = paui32FBCClearColour[3];

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXPDumpPrepareOutputDataDescriptorHdr(PVRSRV_DEVICE_NODE *psDeviceNode,
									IMG_UINT32 ui32HeaderType,
									IMG_UINT32 ui32DataSize,
									IMG_UINT32 ui32ElementType,
									IMG_UINT32 ui32ElementCount,
									IMG_PBYTE pbyPDumpDataHdr)
{
	IMG_PUINT32 pui32Word;

	/* Validate parameters */
	if (((DATA_HEADER_SIZE & ~(HEADER_WORD1_SIZE_CLRMSK >> HEADER_WORD1_SIZE_SHIFT)) != 0) ||
		((DATA_HEADER_VERSION & ~(HEADER_WORD1_VERSION_CLRMSK >> HEADER_WORD1_VERSION_SHIFT)) != 0))
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	pui32Word = IMG_OFFSET_ADDR(pbyPDumpDataHdr, 0);

	if (ui32HeaderType == DATA_HEADER_TYPE)
	{
		pui32Word[0] = (ui32HeaderType << HEADER_WORD0_TYPE_SHIFT);
		pui32Word[1] = (DATA_HEADER_SIZE << HEADER_WORD1_SIZE_SHIFT) |
			(DATA_HEADER_VERSION << HEADER_WORD1_VERSION_SHIFT);
		pui32Word[2] = ui32DataSize << HEADER_WORD2_DATA_SIZE_SHIFT;

		pui32Word[3] = ui32ElementType << DATA_HEADER_WORD3_ELEMENT_TYPE_SHIFT;
		pui32Word[4] = ui32ElementCount << DATA_HEADER_WORD4_ELEMENT_COUNT_SHIFT;
	}

	if (ui32HeaderType == IBIN_HEADER_TYPE)
	{
		pui32Word[0] = (ui32HeaderType << HEADER_WORD0_TYPE_SHIFT);
		pui32Word[1] = (IBIN_HEADER_SIZE << HEADER_WORD1_SIZE_SHIFT) |
			(IBIN_HEADER_VERSION << HEADER_WORD1_VERSION_SHIFT);
		pui32Word[2] = ui32DataSize << HEADER_WORD2_DATA_SIZE_SHIFT;
	}

	return PVRSRV_OK;
}
#endif /* PDUMP */

/******************************************************************************
 End of file (rgxpdump.c)
******************************************************************************/
