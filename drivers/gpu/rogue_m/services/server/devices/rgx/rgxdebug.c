/*************************************************************************/ /*!
@File
@Title          Rgx debug information
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX debugging functions
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
//#define PVR_DPF_FUNCTION_TRACE_ON 1
#undef PVR_DPF_FUNCTION_TRACE_ON

#include "rgxdefs_km.h"
#include "rgxdevice.h"
#include "rgxmem.h"
#include "allocmem.h"
#include "osfunc.h"

#include "lists.h"

#include "rgxdebug.h"
#include "pvrversion.h"
#include "pvr_debug.h"
#include "srvkm.h"
#include "rgxutils.h"
#include "tlstream.h"
#include "rgxfwutils.h"
#include "pvrsrv.h"
#include "services.h"

#include "devicemem_pdump.h"

#include "rgx_fwif.h"
#include "pvrsrv.h"

#if defined(PVRSRV_ENABLE_FW_TRACE_DEBUGFS)
#include "rgx_fwif_sf.h"
#include "rgxfw_log_helper.h"
#endif

#include "rgxta3d.h"
#include "rgxcompute.h"
#include "rgxtransfer.h"
#if defined(RGX_FEATURE_RAY_TRACING)
#include "rgxray.h"
#endif
#if defined(SUPPORT_PAGE_FAULT_DEBUG)
#include "devicemem_history_server.h"
#endif


#define RGX_DEBUG_STR_SIZE	(150)

#define RGX_CR_BIF_CAT_BASE0                              (0x1200U)
#define RGX_CR_BIF_CAT_BASE1                              (0x1208U)

#define RGX_CR_BIF_CAT_BASEN(n) \
	RGX_CR_BIF_CAT_BASE0 + \
	((RGX_CR_BIF_CAT_BASE1 - RGX_CR_BIF_CAT_BASE0) * n)


#if defined(RGX_FEATURE_RAY_TRACING)
#define RGXDBG_BIF_IDS \
	X(BIF0)\
	X(BIF1)\
	X(TEXAS_BIF)\
	X(DPX_BIF)
#else
#define RGXDBG_BIF_IDS \
	X(BIF0)\
	X(BIF1)\
	X(TEXAS_BIF)
#endif

#define RGXDBG_SIDEBAND_TYPES \
	X(META)\
	X(TLA)\
	X(DMA)\
	X(VDMM)\
	X(CDM)\
	X(IPP)\
	X(PM)\
	X(TILING)\
	X(MCU)\
	X(PDS)\
	X(PBE)\
	X(VDMS)\
	X(IPF)\
	X(ISP)\
	X(TPF)\
	X(USCS)\
	X(PPP)\
	X(VCE)\
	X(TPF_CPF)\
	X(IPF_CPF)\
	X(FBCDC)

typedef enum
{
#define X(NAME) RGXDBG_##NAME,
	RGXDBG_BIF_IDS
#undef X
} RGXDBG_BIF_ID;

typedef enum
{
#define X(NAME) RGXDBG_##NAME,
	RGXDBG_SIDEBAND_TYPES
#undef X
} RGXDBG_SIDEBAND_TYPE;


IMG_CHAR* pszPowStateName [] = {
#define X(NAME)	#NAME,
	RGXFWIF_POW_STATES
#undef X
};

IMG_CHAR* pszBIFNames [] = {
#define X(NAME)	#NAME,
	RGXDBG_BIF_IDS
#undef X
};

extern IMG_UINT32 g_ui32HostSampleIRQCount;


IMG_UINT32 RGXReadWithSP(IMG_UINT32 ui32FWAddr)
{
	PVRSRV_DATA        *psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEVICE_NODE *psDeviceNode = psPVRSRVData->apsRegisteredDevNodes[0];
	PVRSRV_RGXDEV_INFO *psDevInfo    = psDeviceNode->pvDevice;
	IMG_UINT32         ui32Value     = 0;
	PVRSRV_ERROR       eError;

	eError = RGXReadMETAAddr(psDevInfo, ui32FWAddr, &ui32Value);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXReadWithSP error: %s", PVRSRVGetErrorStringKM(eError)));
	}

	return ui32Value;
}


#if !defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
/*!
*******************************************************************************

 @Function	_RGXDecodePMPC

 @Description

 Return the name for the PM managed Page Catalogues

 @Input ui32PC	 - Page Catalogue number

 @Return   IMG_VOID

******************************************************************************/
static IMG_CHAR* _RGXDecodePMPC(IMG_UINT32 ui32PC)
{
	IMG_CHAR* pszPMPC = " (-)";

	switch (ui32PC)
	{
		case 0x8: pszPMPC = " (PM-VCE0)"; break;
		case 0x9: pszPMPC = " (PM-TE0)"; break;
		case 0xA: pszPMPC = " (PM-ZLS0)"; break;
		case 0xB: pszPMPC = " (PM-ALIST0)"; break;
		case 0xC: pszPMPC = " (PM-VCE1)"; break;
		case 0xD: pszPMPC = " (PM-TE1)"; break;
		case 0xE: pszPMPC = " (PM-ZLS1)"; break;
		case 0xF: pszPMPC = " (PM-ALIST1)"; break;
	}

	return pszPMPC;
}

#if defined(RGX_FEATURE_RAY_TRACING)
/*!
*******************************************************************************

 @Function	_DPXDecodeBIFReqTags

 @Description

 Decode the BIF Tag ID and sideband data fields from DPX_CR_BIF_FAULT_BANK_REQ_STATUS regs

 @Input eBankID	 			- BIF identifier
 @Input ui32TagID           - Tag ID value
 @Input ui32TagSB           - Tag Sideband data
 @Output ppszTagID          - Decoded string from the Tag ID
 @Output ppszTagSB          - Decoded string from the Tag SB
 @Output pszScratchBuf      - Buffer provided to the function to generate the debug strings
 @Input ui32ScratchBufSize  - Size of the provided buffer

 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID _DPXDecodeBIFReqTags(RGXDBG_BIF_ID	eBankID,
									 IMG_UINT32		ui32TagID, 
									 IMG_UINT32		ui32TagSB, 
									 IMG_CHAR		**ppszTagID, 
									 IMG_CHAR		**ppszTagSB,
									 IMG_CHAR		*pszScratchBuf,
									 IMG_UINT32		ui32ScratchBufSize)
{
	/* default to unknown */
	IMG_CHAR *pszTagID = "-";
	IMG_CHAR *pszTagSB = "-";

	PVR_ASSERT(eBankID == RGXDBG_DPX_BIF);
	PVR_ASSERT(ppszTagID != IMG_NULL);

	PVR_UNREFERENCED_PARAMETER(ui32TagSB);
	PVR_UNREFERENCED_PARAMETER(pszScratchBuf);
	PVR_UNREFERENCED_PARAMETER(ui32ScratchBufSize);

	switch (ui32TagID)
	{
		case 0x0:
		{
			pszTagID = "MMU";
			break;
		}
		case 0x1:
		{
			pszTagID = "RS_READ";
			break;
		}
		case 0x2:
		{
			pszTagID = "RS_WRITE";
			break;
		}
		case 0x3:
		{
			pszTagID = "RQ";
			break;
		}
		case 0x4:
		{
			pszTagID = "PU";
			break;
		}
	} /* switch(TagID) */

	*ppszTagID = pszTagID;
	*ppszTagSB = pszTagSB;
}
#endif

/*!
*******************************************************************************

 @Function	_RGXDecodeBIFReqTags

 @Description

 Decode the BIF Tag ID and sideband data fields from BIF_FAULT_BANK_REQ_STATUS regs

 @Input eBankID	 			- BIF identifier
 @Input ui32TagID           - Tag ID value
 @Input ui32TagSB           - Tag Sideband data
 @Output ppszTagID          - Decoded string from the Tag ID
 @Output ppszTagSB          - Decoded string from the Tag SB
 @Output pszScratchBuf      - Buffer provided to the function to generate the debug strings
 @Input ui32ScratchBufSize  - Size of the provided buffer

 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID _RGXDecodeBIFReqTags(RGXDBG_BIF_ID	eBankID,
									 IMG_UINT32		ui32TagID, 
									 IMG_UINT32		ui32TagSB, 
									 IMG_CHAR		**ppszTagID, 
									 IMG_CHAR		**ppszTagSB,
									 IMG_CHAR		*pszScratchBuf,
									 IMG_UINT32		ui32ScratchBufSize)
{
	/* default to unknown */
	IMG_CHAR *pszTagID = "-";
	IMG_CHAR *pszTagSB = "-";

	PVR_ASSERT(ppszTagID != IMG_NULL);
	PVR_ASSERT(ppszTagSB != IMG_NULL);

#if defined(RGX_FEATURE_RAY_TRACING)
	if (eBankID == RGXDBG_DPX_BIF)
	{
		_DPXDecodeBIFReqTags(eBankID, ui32TagID, ui32TagSB, ppszTagID, ppszTagSB, pszScratchBuf, ui32ScratchBufSize);
		return;
	}
#endif
	
	switch (ui32TagID)
	{
		case 0x0:
		{
#if defined(RGX_FEATURE_RAY_TRACING)
			if (eBankID == RGXDBG_BIF0)
			{
				pszTagID = "VRDM";				
			}
			else
			{
				pszTagID = "MMU";
				switch (ui32TagSB)
				{
					case 0x0: pszTagSB = "Table"; break;
					case 0x1: pszTagSB = "Directory"; break;
					case 0x2: pszTagSB = "Catalogue"; break;
				}
			}
#else
			pszTagID = "MMU";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Table"; break;
				case 0x1: pszTagSB = "Directory"; break;
				case 0x2: pszTagSB = "Catalogue"; break;
			}
#endif
			break;
		}
		case 0x1:
		{
			pszTagID = "TLA";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Pixel data"; break;
				case 0x1: pszTagSB = "Command stream data"; break;
				case 0x2: pszTagSB = "Fence or flush"; break;
			}
			break;
		}
		case 0x2:
		{
#if defined(RGX_FEATURE_RAY_TRACING)
			if (eBankID == RGXDBG_BIF0)
			{
				pszTagID = "SHF";				
			}
			else
			{
				pszTagID = "HOST";
			}
#else
			pszTagID = "HOST";
#endif
			break;
		}
		case 0x3:
		{
#if defined(RGX_FEATURE_RAY_TRACING)
			if (eBankID == RGXDBG_BIF0)
			{
				pszTagID = "SHG";				
			}
			else
			{
				pszTagID = "META";
				switch (ui32TagSB)
				{
					case 0x0: pszTagSB = "DCache - Thread 0"; break;
					case 0x1: pszTagSB = "ICache - Thread 0"; break;
					case 0x2: pszTagSB = "JTag - Thread 0"; break;
					case 0x3: pszTagSB = "Slave bus - Thread 0"; break;
					case 0x4: pszTagSB = "DCache - Thread "; break;
					case 0x5: pszTagSB = "ICache - Thread 1"; break;
					case 0x6: pszTagSB = "JTag - Thread 1"; break;
					case 0x7: pszTagSB = "Slave bus - Thread 1"; break;
				}
			}
#else
			pszTagID = "META";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "DCache - Thread 0"; break;
				case 0x1: pszTagSB = "ICache - Thread 0"; break;
				case 0x2: pszTagSB = "JTag - Thread 0"; break;
				case 0x3: pszTagSB = "Slave bus - Thread 0"; break;
				case 0x4: pszTagSB = "DCache - Thread "; break;
				case 0x5: pszTagSB = "ICache - Thread 1"; break;
				case 0x6: pszTagSB = "JTag - Thread 1"; break;
				case 0x7: pszTagSB = "Slave bus - Thread 1"; break;
			}
#endif
			break;
		}
		case 0x4:
		{
			pszTagID = "USC";
			OSSNPrintf(pszScratchBuf, ui32ScratchBufSize,
			           "Cache line %d", (ui32TagSB & 0x3f));
			pszTagSB = pszScratchBuf;
			break;
		}
#if defined(RGX_FEATURE_CLUSTER_GROUPING)
		case 0x5:
		{
#if defined(RGX_FEATURE_RAY_TRACING)
			if (eBankID == RGXDBG_TEXAS_BIF)
			{
				pszTagID = "PBE";
			}
			else
			{
				pszTagID = "RPM";
			}
#else
			pszTagID = "PBE";
#endif
			break;
		}
		case 0x6:
		{
#if defined(RGX_FEATURE_RAY_TRACING)
			if (eBankID == RGXDBG_TEXAS_BIF)
			{
				pszTagID = "ISP";
				switch (ui32TagSB)
				{
					case 0x00: pszTagSB = "ZLS"; break;
					case 0x20: pszTagSB = "Occlusion Query"; break;
				}
			}
			else
			{
				pszTagID = "FBA";				
			}
#else
			pszTagID = "ISP";
			switch (ui32TagSB)
			{
				case 0x00: pszTagSB = "ZLS"; break;
				case 0x20: pszTagSB = "Occlusion Query"; break;
			}
#endif
			break;
		}
		case 0x7:
		{
			if (eBankID == RGXDBG_TEXAS_BIF)
			{
				pszTagID = "IPF";
				switch (ui32TagSB)
				{
					case 0x0: pszTagSB = "CPF"; break;
					case 0x1: pszTagSB = "DBSC"; break;
					case 0x2:
					case 0x4:
					case 0x6:
					case 0x8: pszTagSB = "Control Stream"; break;
					case 0x3:
					case 0x5:
					case 0x7:
					case 0x9: pszTagSB = "Primitive Block"; break;
				}
			}
			else
			{
				pszTagID = "IPP";
				switch (ui32TagSB)
				{
					case 0x0: pszTagSB = "Macrotile Header"; break;
					case 0x1: pszTagSB = "Region Header"; break;
				}
			}
			break;
		}
#else /* RGX_FEATURE_CLUSTER_GROUPING */
		case 0x5:
		{
			pszTagID = "PBE";
			break;
		}
		case 0x6:
		{
			pszTagID = "ISP";
			switch (ui32TagSB)
			{
				case 0x00: pszTagSB = "ZLS"; break;
				case 0x20: pszTagSB = "Occlusion Query"; break;
			}
			break;
		}
		case 0x7:
		{
			pszTagID = "IPF";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Macrotile Header"; break;
				case 0x1: pszTagSB = "Region Header"; break;
				case 0x2: pszTagSB = "DBSC"; break;
				case 0x3: pszTagSB = "CPF"; break;
				case 0x4: 
				case 0x6:
				case 0x8: pszTagSB = "Control Stream"; break;
				case 0x5: 
				case 0x7:
				case 0x9: pszTagSB = "Primitive Block"; break;
			}
			break;
		}
#endif /* RGX_FEATURE_CLUSTER_GROUPING */
		case 0x8:
		{
			pszTagID = "CDM";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Control Stream"; break;
				case 0x1: pszTagSB = "Indirect Data"; break;
				case 0x2: pszTagSB = "Event Write"; break;
				case 0x3: pszTagSB = "Context State"; break;
			}
			break;
		}
		case 0x9:
		{
			pszTagID = "VDM";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Control Stream"; break;
				case 0x1: pszTagSB = "PPP State"; break;
				case 0x2: pszTagSB = "Index Data"; break;
				case 0x4: pszTagSB = "Call Stack"; break;
				case 0x8: pszTagSB = "Context State"; break;
			}
			break;
		}
		case 0xA:
		{
			pszTagID = "PM";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "PMA_TAFSTACK"; break;
				case 0x1: pszTagSB = "PMA_TAMLIST"; break;
				case 0x2: pszTagSB = "PMA_3DFSTACK"; break;
				case 0x3: pszTagSB = "PMA_3DMLIST"; break;
				case 0x4: pszTagSB = "PMA_PMCTX0"; break;
				case 0x5: pszTagSB = "PMA_PMCTX1"; break;
				case 0x6: pszTagSB = "PMA_MAVP"; break;
				case 0x7: pszTagSB = "PMA_UFSTACK"; break;
				case 0x8: pszTagSB = "PMD_TAFSTACK"; break;
				case 0x9: pszTagSB = "PMD_TAMLIST"; break;
				case 0xA: pszTagSB = "PMD_3DFSTACK"; break;
				case 0xB: pszTagSB = "PMD_3DMLIST"; break;
				case 0xC: pszTagSB = "PMD_PMCTX0"; break;
				case 0xD: pszTagSB = "PMD_PMCTX1"; break;
				case 0xF: pszTagSB = "PMD_UFSTACK"; break;
				case 0x10: pszTagSB = "PMA_TAMMUSTACK"; break;
				case 0x11: pszTagSB = "PMA_3DMMUSTACK"; break;
				case 0x12: pszTagSB = "PMD_TAMMUSTACK"; break;
				case 0x13: pszTagSB = "PMD_3DMMUSTACK"; break;
				case 0x14: pszTagSB = "PMA_TAUFSTACK"; break;
				case 0x15: pszTagSB = "PMA_3DUFSTACK"; break;
				case 0x16: pszTagSB = "PMD_TAUFSTACK"; break;
				case 0x17: pszTagSB = "PMD_3DUFSTACK"; break;
				case 0x18: pszTagSB = "PMA_TAVFP"; break;
				case 0x19: pszTagSB = "PMD_3DVFP"; break;
				case 0x1A: pszTagSB = "PMD_TAVFP"; break;
			}
			break;
		}
		case 0xB:
		{
			pszTagID = "TA";
			switch (ui32TagSB)
			{
				case 0x1: pszTagSB = "VCE"; break;
				case 0x2: pszTagSB = "TPC"; break;
				case 0x3: pszTagSB = "TE Control Stream"; break;
				case 0x4: pszTagSB = "TE Region Header"; break;
				case 0x5: pszTagSB = "TE Render Target Cache"; break;
				case 0x6: pszTagSB = "TEAC Render Target Cache"; break;
				case 0x7: pszTagSB = "VCE Render Target Cache"; break;
				case 0x8: pszTagSB = "PPP Context State"; break;
			}
			break;
		}
		case 0xC:
		{
			pszTagID = "TPF";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "TPF0: Primitive Block"; break;
				case 0x1: pszTagSB = "TPF0: Depth Bias"; break;
				case 0x2: pszTagSB = "TPF0: Per Primitive IDs"; break;
				case 0x3: pszTagSB = "CPF - Tables"; break;
				case 0x4: pszTagSB = "TPF1: Primitive Block"; break;
				case 0x5: pszTagSB = "TPF1: Depth Bias"; break;
				case 0x6: pszTagSB = "TPF1: Per Primitive IDs"; break;
				case 0x7: pszTagSB = "CPF - Data: Pipe 0"; break;
				case 0x8: pszTagSB = "TPF2: Primitive Block"; break;
				case 0x9: pszTagSB = "TPF2: Depth Bias"; break;
				case 0xA: pszTagSB = "TPF2: Per Primitive IDs"; break;
				case 0xB: pszTagSB = "CPF - Data: Pipe 1"; break;
				case 0xC: pszTagSB = "TPF3: Primitive Block"; break;
				case 0xD: pszTagSB = "TPF3: Depth Bias"; break;
				case 0xE: pszTagSB = "TPF3: Per Primitive IDs"; break;
				case 0xF: pszTagSB = "CPF - Data: Pipe 2"; break;
			}
			break;
		}
		case 0xD:
		{
			pszTagID = "PDS";
			break;
		}
		case 0xE:
		{
			pszTagID = "MCU";
			{
				IMG_UINT32 ui32Burst = (ui32TagSB >> 5) & 0x7;
				IMG_UINT32 ui32GroupEnc = (ui32TagSB >> 2) & 0x7;
				IMG_UINT32 ui32Group = ui32TagSB & 0x3;

				IMG_CHAR* pszBurst = "";
				IMG_CHAR* pszGroupEnc = "";
				IMG_CHAR* pszGroup = "";

				switch (ui32Burst)
				{
					case 0x0:
					case 0x1: pszBurst = "128bit word within the Lower 256bits"; break;
					case 0x2:
					case 0x3: pszBurst = "128bit word within the Upper 256bits"; break;
					case 0x4: pszBurst = "Lower 256bits"; break;
					case 0x5: pszBurst = "Upper 256bits"; break;
					case 0x6: pszBurst = "512 bits"; break;
				}
				switch (ui32GroupEnc)
				{
					case 0x0: pszGroupEnc = "TPUA_USC"; break;
					case 0x1: pszGroupEnc = "TPUB_USC"; break;
					case 0x2: pszGroupEnc = "USCA_USC"; break;
					case 0x3: pszGroupEnc = "USCB_USC"; break;
					case 0x4: pszGroupEnc = "PDS_USC"; break;
#if (RGX_FEATURE_NUM_CLUSTERS < 6)
					case 0x5: pszGroupEnc = "PDSRW"; break;
#elif (RGX_FEATURE_NUM_CLUSTERS == 6)
					case 0x5: pszGroupEnc = "UPUC_USC"; break;
					case 0x6: pszGroupEnc = "TPUC_USC"; break;
					case 0x7: pszGroupEnc = "PDSRW"; break;
#endif
				}
				switch (ui32Group)
				{
					case 0x0: pszGroup = "Banks 0-3"; break;
					case 0x1: pszGroup = "Banks 4-7"; break;
					case 0x2: pszGroup = "Banks 8-11"; break;
					case 0x3: pszGroup = "Banks 12-15"; break;
				}

				OSSNPrintf(pszScratchBuf, ui32ScratchBufSize,
								"%s, %s, %s", pszBurst, pszGroupEnc, pszGroup);
				pszTagSB = pszScratchBuf;
			}
			break;
		}
		case 0xF:
		{
			pszTagID = "FB_CDC";
#if defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE)
			{
				IMG_UINT32 ui32Req   = (ui32TagSB >> 0) & 0xf;
				IMG_UINT32 ui32MCUSB = (ui32TagSB >> 4) & 0x3;
				IMG_CHAR* pszReqOrig = "";

				switch (ui32Req)
				{
					case 0x0: pszReqOrig = "FBC Request, originator ZLS"; break;
					case 0x1: pszReqOrig = "FBC Request, originator PBE"; break;
					case 0x2: pszReqOrig = "FBC Request, originator Host"; break;
					case 0x3: pszReqOrig = "FBC Request, originator TLA"; break;
					case 0x4: pszReqOrig = "FBDC Request, originator ZLS"; break;
					case 0x5: pszReqOrig = "FBDC Request, originator MCU"; break;
					case 0x6: pszReqOrig = "FBDC Request, originator Host"; break;
					case 0x7: pszReqOrig = "FBDC Request, originator TLA"; break;
					case 0x8: pszReqOrig = "FBC Request, originator ZLS Requester Fence"; break;
					case 0x9: pszReqOrig = "FBC Request, originator PBE Requester Fence"; break;
					case 0xa: pszReqOrig = "FBC Request, originator Host Requester Fence"; break;
					case 0xb: pszReqOrig = "FBC Request, originator TLA Requester Fence"; break;
					case 0xc: pszReqOrig = "Reserved"; break;
					case 0xd: pszReqOrig = "Reserved"; break;
					case 0xe: pszReqOrig = "FBDC Request, originator FBCDC(Host) Memory Fence"; break;
					case 0xf: pszReqOrig = "FBDC Request, originator FBCDC(TLA) Memory Fence"; break;
				}
				OSSNPrintf(pszScratchBuf, ui32ScratchBufSize,
				           "%s, MCU sideband 0x%X", pszReqOrig, ui32MCUSB);
				pszTagSB = pszScratchBuf;
			}
#else
			{
				IMG_UINT32 ui32Req   = (ui32TagSB >> 2) & 0x7;
				IMG_UINT32 ui32MCUSB = (ui32TagSB >> 0) & 0x3;
				IMG_CHAR* pszReqOrig = "";

				switch (ui32Req)
				{
					case 0x0: pszReqOrig = "FBC Request, originator ZLS";   break;
					case 0x1: pszReqOrig = "FBC Request, originator PBE";   break;
					case 0x2: pszReqOrig = "FBC Request, originator Host";  break;
					case 0x3: pszReqOrig = "FBC Request, originator TLA";   break;
					case 0x4: pszReqOrig = "FBDC Request, originator ZLS";  break;
					case 0x5: pszReqOrig = "FBDC Request, originator MCU";  break;
					case 0x6: pszReqOrig = "FBDC Request, originator Host"; break;
					case 0x7: pszReqOrig = "FBDC Request, originator TLA";  break;
				}
				OSSNPrintf(pszScratchBuf, ui32ScratchBufSize,
				           "%s, MCU sideband 0x%X", pszReqOrig, ui32MCUSB);
				pszTagSB = pszScratchBuf;
			}
#endif
			break;
		}
	} /* switch(TagID) */

	*ppszTagID = pszTagID;
	*ppszTagSB = pszTagSB;
}
#endif


#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
/*!
*******************************************************************************

 @Function	_RGXDecodeMMULevel

 @Description

 Return the name for the MMU level that faulted.

 @Input ui32MMULevel	 - MMU level

 @Return   IMG_CHAR* to the sting describing the MMU level that faulted.

******************************************************************************/
static IMG_CHAR* _RGXDecodeMMULevel(IMG_UINT32 ui32MMULevel)
{
	IMG_CHAR* pszMMULevel = "";

	switch (ui32MMULevel)
	{
		case 0x0: pszMMULevel = " (Page Table)"; break;
		case 0x1: pszMMULevel = " (Page Directory)"; break;
		case 0x2: pszMMULevel = " (Page Catalog)"; break;
		case 0x3: pszMMULevel = " (Cat Base)"; break;
	}

	return pszMMULevel;
}


/*!
*******************************************************************************

 @Function	_RGXDecodeMMUReqTags

 @Description

 Decodes the MMU Tag ID and Sideband data fields from RGX_CR_MMU_FAULT_META_STATUS and
 RGX_CR_MMU_FAULT_STATUS regs.

 @Input ui32TagID           - Tag ID value
 @Input ui32TagSB           - Tag Sideband data
 @Output ppszTagID          - Decoded string from the Tag ID
 @Output ppszTagSB          - Decoded string from the Tag SB
 @Output pszScratchBuf      - Buffer provided to the function to generate the debug strings
 @Input ui32ScratchBufSize  - Size of the provided buffer

 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID _RGXDecodeMMUReqTags(IMG_UINT32  ui32TagID, 
									 IMG_UINT32  ui32TagSB, 
                                     IMG_CHAR    **ppszTagID, 
									 IMG_CHAR    **ppszTagSB,
									 IMG_CHAR    *pszScratchBuf,
									 IMG_UINT32  ui32ScratchBufSize)
{
	IMG_INT32  i32SideBandType = -1;
	IMG_CHAR   *pszTagID = "-";
	IMG_CHAR   *pszTagSB = "-";

	PVR_ASSERT(ppszTagID != IMG_NULL);
	PVR_ASSERT(ppszTagSB != IMG_NULL);

	switch (ui32TagID)
	{
		case  0: pszTagID = "META (Jones)"; i32SideBandType = RGXDBG_META; break;
		case  1: pszTagID = "TLA (Jones)"; i32SideBandType = RGXDBG_TLA; break;
#if defined(RGX_FEATURE_META_DMA)
		case  2: pszTagID = "DMA (Jones)"; i32SideBandType = RGXDBG_DMA; break;
#endif
		case  3: pszTagID = "VDMM (Jones)"; i32SideBandType = RGXDBG_VDMM; break;
		case  4: pszTagID = "CDM (Jones)"; i32SideBandType = RGXDBG_CDM; break;
		case  5: pszTagID = "IPP (Jones)"; i32SideBandType = RGXDBG_IPP; break;
		case  6: pszTagID = "PM (Jones)"; i32SideBandType = RGXDBG_PM; break;
		case  7: pszTagID = "Tiling (Jones)"; i32SideBandType = RGXDBG_TILING; break;
		case  8: pszTagID = "MCU (Texas 0)"; i32SideBandType = RGXDBG_MCU; break;
#if defined(HW_ERN_47229)
		case  9: pszTagID = "PDS (Texas 0)"; i32SideBandType = RGXDBG_PDS; break;
		case 10: pszTagID = "PBE (Texas 0)"; i32SideBandType = RGXDBG_PBE break;
		case 11: pszTagID = "FBCDC (Texas 0)"; i32SideBandType = RGXDBG_FBCDC; break;
#elif defined(FIX_HW_BRN_50539)
		case  9: pszTagID = "PBE (Texas 0)"; i32SideBandType = RGXDBG_PBE break;
		case 10: pszTagID = "PDS (Texas 0)"; i32SideBandType = RGXDBG_PDS; break;
		case 11: pszTagID = "FBCDC (Texas 0)"; i32SideBandType = RGXDBG_FBCDC; break;
#else
		case  9: pszTagID = "PDS (Texas 0)"; i32SideBandType = RGXDBG_PDS; break;
		case 10: pszTagID = "PBE0 (Texas 0)"; i32SideBandType = RGXDBG_PBE; break;
		case 11: pszTagID = "PBE1 (Texas 0)"; i32SideBandType = RGXDBG_PBE; break;
#endif
		case 12: pszTagID = "VDMS (Black Pearl 0)"; i32SideBandType = RGXDBG_VDMS; break;
		case 13: pszTagID = "IPF (Black Pearl 0)"; i32SideBandType = RGXDBG_IPF; break;
		case 14: pszTagID = "ISP (Black Pearl 0)"; i32SideBandType = RGXDBG_ISP; break;
		case 15: pszTagID = "TPF (Black Pearl 0)"; i32SideBandType = RGXDBG_TPF; break;
		case 16: pszTagID = "USCS (Black Pearl 0)"; i32SideBandType = RGXDBG_USCS; break;
		case 17: pszTagID = "PPP (Black Pearl 0)"; i32SideBandType = RGXDBG_PPP; break;
#if defined(HW_ERN_47229)
		case 18: pszTagID = "VCE (Black Pearl 0)"; i32SideBandType = RGXDBG_VCE; break;
		case 19: pszTagID = "FBCDC (Black Pearl 0)"; i32SideBandType = RGXDBG_FBCDC; break;
#else
		case 18: pszTagID = "TPF_CPF (Black Pearl 0)"; i32SideBandType = RGXDBG_TPF_CPF; break;
		case 19: pszTagID = "IPF_CPF (Black Pearl 0)"; i32SideBandType = RGXDBG_IPF_CPF; break;
#endif
		case 20: pszTagID = "MCU (Texas 1)"; i32SideBandType = RGXDBG_MCU; break;
#if defined(HW_ERN_47229)
		case 21: pszTagID = "PDS (Texas 1)"; i32SideBandType = RGXDBG_PDS; break;
		case 22: pszTagID = "PBE (Texas 1)"; i32SideBandType = RGXDBG_PBE break;
		case 23: pszTagID = "FBCDC (Texas 1)"; i32SideBandType = RGXDBG_FBCDC; break;
#elif defined(FIX_HW_BRN_50539)
		case 21: pszTagID = "PBE (Texas 1)"; i32SideBandType = RGXDBG_PBE break;
		case 22: pszTagID = "PDS (Texas 1)"; i32SideBandType = RGXDBG_PDS; break;
		case 23: pszTagID = "FBCDC (Texas 1)"; i32SideBandType = RGXDBG_FBCDC; break;
#else
		case 21: pszTagID = "PDS (Texas 1)"; i32SideBandType = RGXDBG_PDS; break;
		case 22: pszTagID = "PBE0 (Texas 1)"; i32SideBandType = RGXDBG_PBE; break;
		case 23: pszTagID = "PBE1 (Texas 1)"; i32SideBandType = RGXDBG_PBE; break;
#endif
		case 24: pszTagID = "MCU (Texas 2)"; i32SideBandType = RGXDBG_MCU; break;
#if defined(HW_ERN_47229)
		case 25: pszTagID = "PDS (Texas 2)"; i32SideBandType = RGXDBG_PDS; break;
		case 26: pszTagID = "PBE (Texas 2)"; i32SideBandType = RGXDBG_PBE break;
		case 27: pszTagID = "FBCDC (Texas 2)"; i32SideBandType = RGXDBG_FBCDC; break;
#elif defined(FIX_HW_BRN_50539)
		case 25: pszTagID = "PBE (Texas 2)"; i32SideBandType = RGXDBG_PBE break;
		case 26: pszTagID = "PDS (Texas 2)"; i32SideBandType = RGXDBG_PDS; break;
		case 27: pszTagID = "FBCDC (Texas 2)"; i32SideBandType = RGXDBG_FBCDC; break;
#else
		case 25: pszTagID = "PDS (Texas 2)"; i32SideBandType = RGXDBG_PDS; break;
		case 26: pszTagID = "PBE0 (Texas 2)"; i32SideBandType = RGXDBG_PBE; break;
		case 27: pszTagID = "PBE1 (Texas 2)"; i32SideBandType = RGXDBG_PBE; break;
#endif
		case 28: pszTagID = "VDMS (Black Pearl 1)"; i32SideBandType = RGXDBG_VDMS; break;
		case 29: pszTagID = "IPF (Black Pearl 1)"; i32SideBandType = RGXDBG_IPF; break;
		case 30: pszTagID = "ISP (Black Pearl 1)"; i32SideBandType = RGXDBG_ISP; break;
		case 31: pszTagID = "TPF (Black Pearl 1)"; i32SideBandType = RGXDBG_TPF; break;
		case 32: pszTagID = "USCS (Black Pearl 1)"; i32SideBandType = RGXDBG_USCS; break;
		case 33: pszTagID = "PPP (Black Pearl 1)"; i32SideBandType = RGXDBG_PPP; break;
#if defined(HW_ERN_47229)
		case 34: pszTagID = "VCE (Black Pearl 1)"; i32SideBandType = RGXDBG_VCE; break;
		case 35: pszTagID = "FBCDC (Black Pearl 1)"; i32SideBandType = RGXDBG_FBCDC; break;
#else
		case 34: pszTagID = "TPF_CPF (Black Pearl 1)"; i32SideBandType = RGXDBG_TPF_CPF; break;
		case 35: pszTagID = "IPF_CPF (Black Pearl 1)"; i32SideBandType = RGXDBG_IPF_CPF; break;
#endif
		case 36: pszTagID = "MCU (Texas 3)"; i32SideBandType = RGXDBG_MCU; break;
#if defined(HW_ERN_47229)
		case 37: pszTagID = "PDS (Texas 3)"; i32SideBandType = RGXDBG_PDS; break;
		case 38: pszTagID = "PBE (Texas 3)"; i32SideBandType = RGXDBG_PBE break;
		case 39: pszTagID = "FBCDC (Texas 3)"; i32SideBandType = RGXDBG_FBCDC; break;
#elif defined(FIX_HW_BRN_50539)
		case 37: pszTagID = "PBE (Texas 3)"; i32SideBandType = RGXDBG_PBE break;
		case 38: pszTagID = "PDS (Texas 3)"; i32SideBandType = RGXDBG_PDS; break;
		case 39: pszTagID = "FBCDC (Texas 3)"; i32SideBandType = RGXDBG_FBCDC; break;
#else
		case 37: pszTagID = "PDS (Texas 3)"; i32SideBandType = RGXDBG_PDS; break;
		case 38: pszTagID = "PBE0 (Texas 3)"; i32SideBandType = RGXDBG_PBE; break;
		case 39: pszTagID = "PBE1 (Texas 3)"; i32SideBandType = RGXDBG_PBE; break;
#endif
		case 40: pszTagID = "MCU (Texas 4)"; i32SideBandType = RGXDBG_MCU; break;
#if defined(HW_ERN_47229)
		case 41: pszTagID = "PDS (Texas 4)"; i32SideBandType = RGXDBG_PDS; break;
		case 42: pszTagID = "PBE (Texas 4)"; i32SideBandType = RGXDBG_PBE break;
		case 43: pszTagID = "FBCDC (Texas 4)"; i32SideBandType = RGXDBG_FBCDC; break;
#elif defined(FIX_HW_BRN_50539)
		case 41: pszTagID = "PBE (Texas 4)"; i32SideBandType = RGXDBG_PBE break;
		case 42: pszTagID = "PDS (Texas 4)"; i32SideBandType = RGXDBG_PDS; break;
		case 43: pszTagID = "FBCDC (Texas 4)"; i32SideBandType = RGXDBG_FBCDC; break;
#else
		case 41: pszTagID = "PDS (Texas 4)"; i32SideBandType = RGXDBG_PDS; break;
		case 42: pszTagID = "PBE0 (Texas 4)"; i32SideBandType = RGXDBG_PBE; break;
		case 43: pszTagID = "PBE1 (Texas 4)"; i32SideBandType = RGXDBG_PBE; break;
#endif
		case 44: pszTagID = "VDMS (Black Pearl 2)"; i32SideBandType = RGXDBG_VDMS; break;
		case 45: pszTagID = "IPF (Black Pearl 2)"; i32SideBandType = RGXDBG_IPF; break;
		case 46: pszTagID = "ISP (Black Pearl 2)"; i32SideBandType = RGXDBG_ISP; break;
		case 47: pszTagID = "TPF (Black Pearl 2)"; i32SideBandType = RGXDBG_TPF; break;
		case 48: pszTagID = "USCS (Black Pearl 2)"; i32SideBandType = RGXDBG_USCS; break;
		case 49: pszTagID = "PPP (Black Pearl 2)"; i32SideBandType = RGXDBG_PPP; break;
#if defined(HW_ERN_47229)
		case 50: pszTagID = "VCE (Black Pearl 2)"; i32SideBandType = RGXDBG_VCE; break;
		case 51: pszTagID = "FBCDC (Black Pearl 2)"; i32SideBandType = RGXDBG_FBCDC; break;
#else
		case 50: pszTagID = "TPF_CPF (Black Pearl 2)"; i32SideBandType = RGXDBG_TPF_CPF; break;
		case 51: pszTagID = "IPF_CPF (Black Pearl 2)"; i32SideBandType = RGXDBG_IPF_CPF; break;
#endif
		case 52: pszTagID = "MCU (Texas 5)"; i32SideBandType = RGXDBG_MCU; break;
#if defined(HW_ERN_47229)
		case 53: pszTagID = "PDS (Texas 5)"; i32SideBandType = RGXDBG_PDS; break;
		case 54: pszTagID = "PBE (Texas 5)"; i32SideBandType = RGXDBG_PBE break;
		case 55: pszTagID = "FBCDC (Texas 5)"; i32SideBandType = RGXDBG_FBCDC; break;
#elif defined(FIX_HW_BRN_50539)
		case 53: pszTagID = "PBE (Texas 5)"; i32SideBandType = RGXDBG_PBE break;
		case 54: pszTagID = "PDS (Texas 5)"; i32SideBandType = RGXDBG_PDS; break;
		case 55: pszTagID = "FBCDC (Texas 5)"; i32SideBandType = RGXDBG_FBCDC; break;
#else
		case 53: pszTagID = "PDS (Texas 5)"; i32SideBandType = RGXDBG_PDS; break;
		case 54: pszTagID = "PBE0 (Texas 5)"; i32SideBandType = RGXDBG_PBE; break;
		case 55: pszTagID = "PBE1 (Texas 5)"; i32SideBandType = RGXDBG_PBE; break;
#endif
		case 56: pszTagID = "MCU (Texas 6)"; i32SideBandType = RGXDBG_MCU; break;
#if defined(HW_ERN_47229)
		case 57: pszTagID = "PDS (Texas 6)"; i32SideBandType = RGXDBG_PDS; break;
		case 58: pszTagID = "PBE (Texas 6)"; i32SideBandType = RGXDBG_PBE break;
		case 59: pszTagID = "FBCDC (Texas 6)"; i32SideBandType = RGXDBG_FBCDC; break;
#elif defined(FIX_HW_BRN_50539)
		case 57: pszTagID = "PBE (Texas 6)"; i32SideBandType = RGXDBG_PBE break;
		case 58: pszTagID = "PDS (Texas 6)"; i32SideBandType = RGXDBG_PDS; break;
		case 59: pszTagID = "FBCDC (Texas 6)"; i32SideBandType = RGXDBG_FBCDC; break;
#else
		case 57: pszTagID = "PDS (Texas 6)"; i32SideBandType = RGXDBG_PDS; break;
		case 58: pszTagID = "PBE0 (Texas 6)"; i32SideBandType = RGXDBG_PBE; break;
		case 59: pszTagID = "PBE1 (Texas 6)"; i32SideBandType = RGXDBG_PBE; break;
#endif
		case 60: pszTagID = "VDMS (Black Pearl 3)"; i32SideBandType = RGXDBG_VDMS; break;
		case 61: pszTagID = "IPF (Black Pearl 3)"; i32SideBandType = RGXDBG_IPF; break;
		case 62: pszTagID = "ISP (Black Pearl 3)"; i32SideBandType = RGXDBG_ISP; break;
		case 63: pszTagID = "TPF (Black Pearl 3)"; i32SideBandType = RGXDBG_TPF; break;
		case 64: pszTagID = "USCS (Black Pearl 3)"; i32SideBandType = RGXDBG_USCS; break;
		case 65: pszTagID = "PPP (Black Pearl 3)"; i32SideBandType = RGXDBG_PPP; break;
#if defined(HW_ERN_47229)
		case 66: pszTagID = "VCE (Black Pearl 3)"; i32SideBandType = RGXDBG_VCE; break;
		case 67: pszTagID = "FBCDC (Black Pearl 3)"; i32SideBandType = RGXDBG_FBCDC; break;
#else
		case 66: pszTagID = "TPF_CPF (Black Pearl 3)"; i32SideBandType = RGXDBG_TPF_CPF; break;
		case 67: pszTagID = "IPF_CPF (Black Pearl 3)"; i32SideBandType = RGXDBG_IPF_CPF; break;
#endif
		case 68: pszTagID = "MCU (Texas 7)"; i32SideBandType = RGXDBG_MCU; break;
#if defined(HW_ERN_47229)
		case 69: pszTagID = "PDS (Texas 7)"; i32SideBandType = RGXDBG_PDS; break;
		case 70: pszTagID = "PBE (Texas 7)"; i32SideBandType = RGXDBG_PBE break;
		case 71: pszTagID = "FBCDC (Texas 7)"; i32SideBandType = RGXDBG_FBCDC; break;
#elif defined(FIX_HW_BRN_50539)
		case 69: pszTagID = "PBE (Texas 7)"; i32SideBandType = RGXDBG_PBE break;
		case 70: pszTagID = "PDS (Texas 7)"; i32SideBandType = RGXDBG_PDS; break;
		case 71: pszTagID = "FBCDC (Texas 7)"; i32SideBandType = RGXDBG_FBCDC; break;
#else
		case 69: pszTagID = "PDS (Texas 7)"; i32SideBandType = RGXDBG_PDS; break;
		case 70: pszTagID = "PBE0 (Texas 7)"; i32SideBandType = RGXDBG_PBE; break;
		case 71: pszTagID = "PBE1 (Texas 7)"; i32SideBandType = RGXDBG_PBE; break;
#endif
	}
	
	switch (i32SideBandType)
	{
		case RGXDBG_META:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "DCache - Thread 0"; break;
				case 0x1: pszTagSB = "ICache - Thread 0"; break;
				case 0x2: pszTagSB = "JTag - Thread 0"; break;
				case 0x3: pszTagSB = "Slave bus - Thread 0"; break;
				case 0x4: pszTagSB = "DCache - Thread 1"; break;
				case 0x5: pszTagSB = "ICache - Thread 1"; break;
				case 0x6: pszTagSB = "JTag - Thread 1"; break;
				case 0x7: pszTagSB = "Slave bus - Thread 1"; break;
			}
			break;
		}

		case RGXDBG_TLA:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Pixel data"; break;
				case 0x1: pszTagSB = "Command stream data"; break;
				case 0x2: pszTagSB = "Fence or flush"; break;
			}
			break;
		}

		case RGXDBG_VDMM:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Control Stream - Read Only"; break;
				case 0x1: pszTagSB = "PPP State - Read Only"; break;
				case 0x2: pszTagSB = "Indices - Read Only"; break;
				case 0x4: pszTagSB = "Call Stack - Read/Write"; break;
				case 0x6: pszTagSB = "DrawIndirect - Read Only"; break;
				case 0xA: pszTagSB = "Context State - Write Only"; break;
			}
			break;
		}

		case RGXDBG_CDM:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Control Stream"; break;
				case 0x1: pszTagSB = "Indirect Data"; break;
				case 0x2: pszTagSB = "Event Write"; break;
				case 0x3: pszTagSB = "Context State"; break;
			}
			break;
		}

		case RGXDBG_IPP:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Macrotile Header"; break;
				case 0x1: pszTagSB = "Region Header"; break;
			}
			break;
		}

		case RGXDBG_PM:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "PMA_TAFSTACK"; break;
				case 0x1: pszTagSB = "PMA_TAMLIST"; break;
				case 0x2: pszTagSB = "PMA_3DFSTACK"; break;
				case 0x3: pszTagSB = "PMA_3DMLIST"; break;
				case 0x4: pszTagSB = "PMA_PMCTX0"; break;
				case 0x5: pszTagSB = "PMA_PMCTX1"; break;
				case 0x6: pszTagSB = "PMA_MAVP"; break;
				case 0x7: pszTagSB = "PMA_UFSTACK"; break;
				case 0x8: pszTagSB = "PMD_TAFSTACK"; break;
				case 0x9: pszTagSB = "PMD_TAMLIST"; break;
				case 0xA: pszTagSB = "PMD_3DFSTACK"; break;
				case 0xB: pszTagSB = "PMD_3DMLIST"; break;
				case 0xC: pszTagSB = "PMD_PMCTX0"; break;
				case 0xD: pszTagSB = "PMD_PMCTX1"; break;
				case 0xF: pszTagSB = "PMD_UFSTACK"; break;
				case 0x10: pszTagSB = "PMA_TAMMUSTACK"; break;
				case 0x11: pszTagSB = "PMA_3DMMUSTACK"; break;
				case 0x12: pszTagSB = "PMD_TAMMUSTACK"; break;
				case 0x13: pszTagSB = "PMD_3DMMUSTACK"; break;
				case 0x14: pszTagSB = "PMA_TAUFSTACK"; break;
				case 0x15: pszTagSB = "PMA_3DUFSTACK"; break;
				case 0x16: pszTagSB = "PMD_TAUFSTACK"; break;
				case 0x17: pszTagSB = "PMD_3DUFSTACK"; break;
				case 0x18: pszTagSB = "PMA_TAVFP"; break;
				case 0x19: pszTagSB = "PMD_3DVFP"; break;
				case 0x1A: pszTagSB = "PMD_TAVFP"; break;
			}
			break;
		}

		case RGXDBG_TILING:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "PSG Control Stream TP0"; break;
				case 0x1: pszTagSB = "TPC TP0"; break;
				case 0x2: pszTagSB = "VCE0"; break;
				case 0x3: pszTagSB = "VCE1"; break;
				case 0x4: pszTagSB = "PSG Control Stream TP1"; break;
				case 0x5: pszTagSB = "TPC TP1"; break;
				case 0x8: pszTagSB = "PSG Region Header TP0"; break;
				case 0xC: pszTagSB = "PSG Region Header TP1"; break;
			}
			break;
		}

		case RGXDBG_VDMS:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Context State - Write Only"; break;
			}
			break;
		}
		
		case RGXDBG_IPF:
		{
			switch (ui32TagSB)
			{
				case 0x00:
				case 0x20: pszTagSB = "CPF"; break;
				case 0x01: pszTagSB = "DBSC"; break;
				case 0x02:
				case 0x04:
				case 0x06:
				case 0x08:
				case 0x0A:
				case 0x0C:
				case 0x0E:
				case 0x10: pszTagSB = "Control Stream"; break;
				case 0x03:
				case 0x05:
				case 0x07:
				case 0x09:
				case 0x0B:
				case 0x0D:
				case 0x0F:
				case 0x11: pszTagSB = "Primitive Block"; break;
			}
			break;
		}

		case RGXDBG_ISP:
		{
			switch (ui32TagSB)
			{
				case 0x00: pszTagSB = "ZLS read/write"; break;
				case 0x20: pszTagSB = "Occlusion query read/write"; break;
			}
			break;
		}

		case RGXDBG_TPF:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "TPF0: Primitive Block"; break;
				case 0x1: pszTagSB = "TPF0: Depth Bias"; break;
				case 0x2: pszTagSB = "TPF0: Per Primitive IDs"; break;
				case 0x3: pszTagSB = "CPF - Tables"; break;
				case 0x4: pszTagSB = "TPF1: Primitive Block"; break;
				case 0x5: pszTagSB = "TPF1: Depth Bias"; break;
				case 0x6: pszTagSB = "TPF1: Per Primitive IDs"; break;
				case 0x7: pszTagSB = "CPF - Data: Pipe 0"; break;
				case 0x8: pszTagSB = "TPF2: Primitive Block"; break;
				case 0x9: pszTagSB = "TPF2: Depth Bias"; break;
				case 0xA: pszTagSB = "TPF2: Per Primitive IDs"; break;
				case 0xB: pszTagSB = "CPF - Data: Pipe 1"; break;
				case 0xC: pszTagSB = "TPF3: Primitive Block"; break;
				case 0xD: pszTagSB = "TPF3: Depth Bias"; break;
				case 0xE: pszTagSB = "TPF3: Per Primitive IDs"; break;
				case 0xF: pszTagSB = "CPF - Data: Pipe 2"; break;
			}
			break;
		}

		case RGXDBG_FBCDC:
		{
			IMG_UINT32 ui32Req   = (ui32TagSB >> 0) & 0xf;
			IMG_UINT32 ui32MCUSB = (ui32TagSB >> 4) & 0x3;
			IMG_CHAR* pszReqOrig = "";

			switch (ui32Req)
			{
				case 0x0: pszReqOrig = "FBC Request, originator ZLS";  break;
				case 0x1: pszReqOrig = "FBC Request, originator PBE"; break;
				case 0x2: pszReqOrig = "FBC Request, originator Host"; break;
				case 0x3: pszReqOrig = "FBC Request, originator TLA"; break;
				case 0x4: pszReqOrig = "FBDC Request, originator ZLS"; break;
				case 0x5: pszReqOrig = "FBDC Request, originator MCU"; break;
				case 0x6: pszReqOrig = "FBDC Request, originator Host"; break;
				case 0x7: pszReqOrig = "FBDC Request, originator TLA"; break;
				case 0x8: pszReqOrig = "FBC Request, originator ZLS Requester Fence"; break;
				case 0x9: pszReqOrig = "FBC Request, originator PBE Requester Fence"; break;
				case 0xa: pszReqOrig = "FBC Request, originator Host Requester Fence"; break;
				case 0xb: pszReqOrig = "FBC Request, originator TLA Requester Fence"; break;
				case 0xc: pszReqOrig = "Reserved"; break;
				case 0xd: pszReqOrig = "Reserved"; break;
				case 0xe: pszReqOrig = "FBDC Request, originator FBCDC(Host) Memory Fence"; break;
				case 0xf: pszReqOrig = "FBDC Request, originator FBCDC(TLA) Memory Fence"; break;
			}
			OSSNPrintf(pszScratchBuf, ui32ScratchBufSize,
			           "%s, MCU sideband 0x%X", pszReqOrig, ui32MCUSB);
			pszTagSB = pszScratchBuf;
			break;
		}

		case RGXDBG_MCU:
		{
			IMG_UINT32 ui32SetNumber = (ui32TagSB >> 5) & 0x7;
			IMG_UINT32 ui32WayNumber = (ui32TagSB >> 2) & 0x7;
			IMG_UINT32 ui32Group     = ui32TagSB & 0x3;

			IMG_CHAR* pszGroup = "";

			switch (ui32Group)
			{
				case 0x0: pszGroup = "Banks 0-1"; break;
				case 0x1: pszGroup = "Banks 2-3"; break;
				case 0x2: pszGroup = "Banks 4-5"; break;
				case 0x3: pszGroup = "Banks 6-7"; break;
			}

			OSSNPrintf(pszScratchBuf, ui32ScratchBufSize,
			           "Set=%d, Way=%d, %s", ui32SetNumber, ui32WayNumber, pszGroup);
			pszTagSB = pszScratchBuf;
			break;
		}

		default:
		{
			OSSNPrintf(pszScratchBuf, ui32ScratchBufSize, "SB=0x%02x", ui32TagSB);
			pszTagSB = pszScratchBuf;
			break;
		}
	}

	*ppszTagID = pszTagID;
	*ppszTagSB = pszTagSB;
}
#endif


#if !defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)

#if defined(SUPPORT_PAGE_FAULT_DEBUG)

typedef enum _DEVICEMEM_HISTORY_QUERY_INDEX_
{
	DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING,
	DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED,
	DEVICEMEM_HISTORY_QUERY_INDEX_NEXT,
	DEVICEMEM_HISTORY_QUERY_INDEX_COUNT,
} DEVICEMEM_HISTORY_QUERY_INDEX;

/*!
*******************************************************************************

 @Function	_PrintDevicememHistoryQueryResult

 @Description

 Print details of a single result from a DevicememHistory query

 @Input pfnDumpDebugPrintf       - Debug printf function
 @Input psResult                 - The DevicememHistory result to be printed
 @Input ui32Index                - The index of the result

 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID _PrintDevicememHistoryQueryResult(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
								DEVICEMEM_HISTORY_QUERY_OUT_RESULT *psResult,
								IMG_UINT32 ui32Index)
{
	IMG_UINT32 ui32Remainder;
	PVR_DUMPDEBUG_LOG(("  [%u] Name: %s Base address: " IMG_DEV_VIRTADDR_FMTSPEC
				" Size: " IMG_DEVMEM_SIZE_FMTSPEC
				" Allocated: %c Modified %llu us ago (abs time %llu us)",
									ui32Index,
									psResult->szString,
					(unsigned long long) psResult->sBaseDevVAddr.uiAddr,
					(unsigned long long) psResult->uiSize,
					psResult->bAllocated ? 'Y' : 'N',
					(unsigned long long) OSDivide64r64(psResult->ui64Age, 1000, &ui32Remainder),
					(unsigned long long) OSDivide64r64(psResult->ui64When, 1000, &ui32Remainder)));
}

/*!
*******************************************************************************

 @Function	_PrintDevicememHistoryQueryOut

 @Description

 Print details of all the results from a DevicememHistory query

 @Input pfnDumpDebugPrintf       - Debug printf function
 @Input psResult                 - The DevicememHistory result to be printed

 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID _PrintDevicememHistoryQueryOut(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf, DEVICEMEM_HISTORY_QUERY_OUT *psQueryOut)
{
	IMG_UINT32 i;

	if(psQueryOut->ui32NumResults == 0)
	{
		PVR_DUMPDEBUG_LOG(("  No results"));
	}
	else
	{
		for(i = 0; i < psQueryOut->ui32NumResults; i++)
		{
			_PrintDevicememHistoryQueryResult(pfnDumpDebugPrintf, &psQueryOut->sResults[i], i);
		}
	}
}

/* table of HW page size values and the equivalent */
static const unsigned int aui32HWPageSizeTable[][2] =
{
	{ 0, PVRSRV_4K_PAGE_SIZE },
	{ 1, PVRSRV_16K_PAGE_SIZE },
	{ 2, PVRSRV_64K_PAGE_SIZE },
	{ 3, PVRSRV_256K_PAGE_SIZE },
	{ 4, PVRSRV_1M_PAGE_SIZE },
	{ 5, PVRSRV_2M_PAGE_SIZE }
};

/*!
*******************************************************************************

 @Function	_PageSizeHWToBytes

 @Description

 Convert a HW page size value to its size in bytes

 @Input ui32PageSizeHW     - The HW page size value

 @Return   IMG_UINT32      The page size in bytes

******************************************************************************/
static IMG_UINT32 _PageSizeHWToBytes(IMG_UINT32 ui32PageSizeHW)
{
	PVR_ASSERT(ui32PageSizeHW <= 5);

	return aui32HWPageSizeTable[ui32PageSizeHW][1];
}

/*!
*******************************************************************************

 @Function	_GetDevicememHistoryData

 @Description

 Get the DevicememHistory results for the given PID and faulting device virtual address.
 The function will query DevicememHistory for information about the faulting page, as well
 as the page before and after.

 @Input uiPID              - The process ID to search for allocations belonging to
 @Input sFaultDevVAddr     - The device address to search for allocations at/before/after
 @Input asQueryOut         - Storage for the query results
 @Input ui32PageSizeBytes  - Faulted page size in bytes

 @Return IMG_BOOL          - IMG_TRUE if any results were found for this page fault

******************************************************************************/
static IMG_BOOL _GetDevicememHistoryData(IMG_PID uiPID, IMG_DEV_VIRTADDR sFaultDevVAddr,
							DEVICEMEM_HISTORY_QUERY_OUT asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_COUNT],
							IMG_UINT32 ui32PageSizeBytes)
{
	IMG_UINT32 i;
	DEVICEMEM_HISTORY_QUERY_IN sQueryIn;
	IMG_BOOL bAnyHits = IMG_FALSE;

	sQueryIn.uiPID = uiPID;

	/* query the DevicememHistory about the preceding / faulting / next page */

	for(i = DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING; i < DEVICEMEM_HISTORY_QUERY_INDEX_COUNT; i++)
	{
		IMG_BOOL bHits;

		switch(i)
		{
			case DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING:
				sQueryIn.sDevVAddr.uiAddr = (sFaultDevVAddr.uiAddr & ~(IMG_UINT64)(ui32PageSizeBytes - 1)) - 1;
				break;
			case DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED:
				sQueryIn.sDevVAddr = sFaultDevVAddr;
				break;
			case DEVICEMEM_HISTORY_QUERY_INDEX_NEXT:
				sQueryIn.sDevVAddr.uiAddr = (sFaultDevVAddr.uiAddr & ~(IMG_UINT64)(ui32PageSizeBytes - 1)) + ui32PageSizeBytes;
				break;
		}

		bHits = DevicememHistoryQuery(&sQueryIn, &asQueryOut[i]);

		if(bHits)
		{
			bAnyHits = IMG_TRUE;
		}
	}

	return bAnyHits;
}

/* stored data about one page fault */
typedef struct _FAULT_INFO_
{
	RGXMEM_PROCESS_INFO sProcessInfo;
	IMG_DEV_VIRTADDR sFaultDevVAddr;
	DEVICEMEM_HISTORY_QUERY_OUT asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_COUNT];
	/* the CR timer value at the time of the fault, recorded by the FW.
	 * used to differentiate different page faults
	 */
	IMG_UINT64 ui64CRTimer;
	/* time when this FAULT_INFO entry was added. used for timing
	 * reference against the map/unmap information
	 */
	IMG_UINT64 ui64When;
} FAULT_INFO;

/* history list of page faults.
 * Keeps the fist `n` page faults and the last `n` page faults, like the FW
 * HWR log
 */
typedef struct _FAULT_INFO_LOG_
{
	IMG_UINT32 ui32Head;
	IMG_UINT32 ui32NumWrites;
	/* the number of faults in this log need not correspond exactly to
	 * the HWINFO number of the FW, as the FW HWINFO log may contain
	 * non-page fault HWRs
	 */
	FAULT_INFO asFaults[RGXFWIF_HWINFO_MAX];
} FAULT_INFO_LOG;

static FAULT_INFO_LOG gsFaultInfoLog = { 0 };

/*!
*******************************************************************************

 @Function	_QueryFaultInfo

 @Description

 Searches the local list of previously analysed page faults to see if the given
 fault has already been analysed and if so, returns a pointer to the analysis
 objbect (FAULT_INFO *), otherwise returns NULL.

 @Input pfnDumpDebugPrintf       - The debug printf function
 @Input sFaultDevVAddr           - The faulting device virtual address
 @Input ui64CRTimer              - The CR timer value recorded by the FW at the time of the fault

 @Return   FAULT_INFO* Pointer to an existing fault analysis structure if found, otherwise IMG_NULL

******************************************************************************/
static FAULT_INFO *_QueryFaultInfo(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
								IMG_DEV_VIRTADDR sFaultDevVAddr,
								IMG_UINT64 ui64CRTimer)
{
	IMG_UINT32 i;

	for(i = 0; i < MIN(gsFaultInfoLog.ui32NumWrites, RGXFWIF_HWINFO_MAX); i++)
	{
		if((gsFaultInfoLog.asFaults[i].ui64CRTimer == ui64CRTimer) &&
			(gsFaultInfoLog.asFaults[i].sFaultDevVAddr.uiAddr == sFaultDevVAddr.uiAddr))
			{
				return &gsFaultInfoLog.asFaults[i];
			}
	}

	return IMG_NULL;
}

/*!
*******************************************************************************

 @Function	_AddFaultInfo

 @Description

 Add the given page fault information to the page fault log.


 @Input psProcessInfo        - Information about the user process which caused the page fault
 @Input sFaultDevVAddr       - The faulting device virtual address
 @Input ui64CRTimer          - The CR timer value recorded by the FW at the time of the fault
 @Input asQueryOut           - The DevicememHistory query information with the allocations relating to the fault

 @Return   FAULT_INFO* Pointer to the newly added record

******************************************************************************/
static FAULT_INFO *_AddFaultInfo(RGXMEM_PROCESS_INFO *psProcessInfo,
						IMG_DEV_VIRTADDR sFaultDevVAddr,
						IMG_UINT64 ui64CRTimer,
						DEVICEMEM_HISTORY_QUERY_OUT asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_COUNT])
{
	IMG_UINT32 ui32Head = gsFaultInfoLog.ui32Head;
	FAULT_INFO *psInfo = &gsFaultInfoLog.asFaults[ui32Head];

	if(gsFaultInfoLog.ui32Head < RGXFWIF_HWINFO_MAX - 1)
	{
		gsFaultInfoLog.ui32Head++;
	}
	else
	{
		/* wrap back to the first of the 'LAST' entries */
		gsFaultInfoLog.ui32Head = RGXFWIF_HWINFO_MAX_FIRST;
	}

	gsFaultInfoLog.ui32NumWrites++;

	psInfo->sProcessInfo = *psProcessInfo;
	psInfo->sFaultDevVAddr = sFaultDevVAddr;
	psInfo->ui64CRTimer = ui64CRTimer;
	psInfo->ui64When = OSClockus64();
	OSMemCopy(psInfo->asQueryOut, asQueryOut, sizeof(psInfo->asQueryOut));

	return psInfo;
}

/*!
*******************************************************************************

 @Function	_PrintFaultInfo

 @Description

 Print all the details of a page fault from a FAULT_INFO structure

 @Input pfnDumpDebugPrintf   - The debug printf function
 @Input psInfo               - The page fault occurrence to print
 @Input pui32Index           - (optional) index value to include in the print output

 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID _PrintFaultInfo(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
							FAULT_INFO *psInfo,
							const IMG_UINT32 *pui32Index)
{
	IMG_UINT32 i;

	if(pui32Index)
	{
		PVR_DUMPDEBUG_LOG(("(%u) Device memory history for page fault address 0x%010llX, CRTimer: 0x%016llX, "
							"PID: %u (%s, unregistered: %u) Abs Time: %llu us",
					*pui32Index,
					(unsigned long long) psInfo->sFaultDevVAddr.uiAddr,
					psInfo->ui64CRTimer,
					(unsigned int) psInfo->sProcessInfo.uiPID,
					psInfo->sProcessInfo.szProcessName,
					psInfo->sProcessInfo.bUnregistered,
					(unsigned long long) psInfo->ui64When));
	}
	else
	{
		PVR_DUMPDEBUG_LOG(("Device memory history for page fault address 0x%010llX, PID: %u (%s, unregistered: %u) Abs Time: %llu us",
					(unsigned long long) psInfo->sFaultDevVAddr.uiAddr,
					(unsigned int) psInfo->sProcessInfo.uiPID,
					psInfo->sProcessInfo.szProcessName,
					psInfo->sProcessInfo.bUnregistered,
					(unsigned long long) psInfo->ui64When));
	}

	for(i = DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING; i < DEVICEMEM_HISTORY_QUERY_INDEX_COUNT; i++)
	{
		const IMG_CHAR *pszWhich;

		switch(i)
		{
			case DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING:
				pszWhich = "Preceding page";
				break;
			case DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED:
				pszWhich = "Faulted page";
				break;
			case DEVICEMEM_HISTORY_QUERY_INDEX_NEXT:
				pszWhich = "Next page";
				break;
		}

		PVR_DUMPDEBUG_LOG(("%s:", pszWhich));
		_PrintDevicememHistoryQueryOut(pfnDumpDebugPrintf, &psInfo->asQueryOut[i]);
	}
}

#endif

/*!
*******************************************************************************

 @Function	_RGXDumpRGXBIFBank

 @Description

 Dump BIF Bank state in human readable form.

 @Input psDevInfo				- RGX device info
 @Input eBankID	 				- BIF identifier
 @Input ui64MMUStatus			- MMU Status register value
 @Input ui64ReqStatus			- BIF request Status register value
 @Input ui64PCAddress                   - Page catalogue base address of faulting access
 @Input ui64CRTimer                     - RGX CR timer value at time of page fault
 @Input bBIFSummary				- Flag to check whether the function is called
 	 	 	 	 	 	 	 	  as a part of the debug dump summary or
								  as a part of a HWR log
 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID _RGXDumpRGXBIFBank(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                   PVRSRV_RGXDEV_INFO	*psDevInfo,
                                   RGXDBG_BIF_ID 		eBankID,
                                   IMG_UINT64			ui64MMUStatus,
                                   IMG_UINT64			ui64ReqStatus,
                                   IMG_UINT64			ui64PCAddress,
                                   IMG_UINT64			ui64CRTimer,
                                   IMG_BOOL				bBIFSummary)
{

	if (ui64MMUStatus == 0x0)
	{
		PVR_DUMPDEBUG_LOG(("%s - OK", pszBIFNames[eBankID]));
	}
	else
	{
		IMG_DEV_VIRTADDR sFaultDevVAddr;
		IMG_DEV_PHYADDR sPCDevPAddr = { 0 };
#if defined(SUPPORT_PAGE_FAULT_DEBUG)
		IMG_BOOL bFound = IMG_FALSE;
		RGXMEM_PROCESS_INFO sProcessInfo;
		IMG_UINT32 ui32PageSizeBytes;
		FAULT_INFO *psInfo;
#endif
		/* Bank 0 & 1 share the same fields */
		PVR_DUMPDEBUG_LOG(("%s%s - FAULT:",
						  (bBIFSummary)?"":"    ",
						  pszBIFNames[eBankID]));

		/* MMU Status */
		{
			IMG_UINT32 ui32PC = 
				(ui64MMUStatus & ~RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_CAT_BASE_CLRMSK) >>
					RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_CAT_BASE_SHIFT;

			IMG_UINT32 ui32PageSize = 
				(ui64MMUStatus & ~RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_PAGE_SIZE_CLRMSK) >>
					RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_PAGE_SIZE_SHIFT;

			IMG_UINT32 ui32MMUDataType = 
				(ui64MMUStatus & ~RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_DATA_TYPE_CLRMSK) >>
					RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_DATA_TYPE_SHIFT;

			IMG_BOOL bROFault = (ui64MMUStatus & RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_FAULT_RO_EN) != 0;
			IMG_BOOL bProtFault = (ui64MMUStatus & RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_FAULT_PM_META_RO_EN) != 0;

#if defined(SUPPORT_PAGE_FAULT_DEBUG)
			ui32PageSizeBytes = _PageSizeHWToBytes(ui32PageSize);
#endif

			PVR_DUMPDEBUG_LOG(("%s  * MMU status (0x%016llX): PC = %d%s, Page Size = %d, MMU data type = %d%s%s.",
			                  (bBIFSummary)?"":"    ",
							  ui64MMUStatus,
			                  ui32PC,
			                  (ui32PC < 0x8)?"":_RGXDecodePMPC(ui32PC),
			                  ui32PageSize,
			                  ui32MMUDataType,
			                  (bROFault)?", Read Only fault":"",
			                  (bProtFault)?", PM/META protection fault":""));
		}

		/* Req Status */
		{
			IMG_CHAR *pszTagID;
			IMG_CHAR *pszTagSB;
			IMG_CHAR aszScratch[RGX_DEBUG_STR_SIZE];

			IMG_BOOL bRead = (ui64ReqStatus & RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_RNW_EN) != 0;
			IMG_UINT32 ui32TagSB = 
				(ui64ReqStatus & ~RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_TAG_SB_CLRMSK) >>
					RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_TAG_SB_SHIFT;
			IMG_UINT32 ui32TagID = 
				(ui64ReqStatus & ~RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_TAG_ID_CLRMSK) >>
							RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_TAG_ID_SHIFT;
			IMG_UINT64 ui64Addr = (ui64ReqStatus & ~RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_ADDRESS_CLRMSK);

#if defined(RGX_FEATURE_RAY_TRACING)
			/* RNW bit offset is different. The TAG_SB, TAG_ID and address fields are the same. */
			if (eBankID == RGXDBG_DPX_BIF)
			{
				bRead = (ui64ReqStatus & DPX_CR_BIF_FAULT_BANK_REQ_STATUS_RNW_EN) != 0;
			}
#endif
			_RGXDecodeBIFReqTags(eBankID, ui32TagID, ui32TagSB, &pszTagID, &pszTagSB, &aszScratch[0], RGX_DEBUG_STR_SIZE);

			PVR_DUMPDEBUG_LOG(("%s  * Request (0x%016llX): %s (%s), %s 0x%010llX.",
							  (bBIFSummary)?"":"    ",
							  ui64ReqStatus,
			                  pszTagID,
			                  pszTagSB,
			                  (bRead)?"Reading from":"Writing to",
			                  ui64Addr));
		}

		/* Check if the host thinks this fault is valid */

		sFaultDevVAddr.uiAddr = (ui64ReqStatus & ~RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_ADDRESS_CLRMSK);

		if(bBIFSummary)
		{
			IMG_UINT32 ui32PC = 
				(ui64MMUStatus & ~RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_CAT_BASE_CLRMSK) >>
					RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_CAT_BASE_SHIFT;
				
			/* Only the first 8 cat bases are application memory contexts which we can validate... */
			if (ui32PC < 8)
			{
				sPCDevPAddr.uiAddr = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_CAT_BASEN(ui32PC));
				PVR_DUMPDEBUG_LOG(("Acquired live PC address: 0x%016llX", sPCDevPAddr.uiAddr));
			}
			else
			{
				sPCDevPAddr.uiAddr = 0;
			}
		}
		else
		{
			PVR_DUMPDEBUG_LOG(("FW logged fault using PC Address: 0x%016llX", ui64PCAddress));
			sPCDevPAddr.uiAddr = ui64PCAddress;
		}

		if(bBIFSummary)
		{
			PVR_DUMPDEBUG_LOG(("Checking faulting address 0x%010llX", sFaultDevVAddr.uiAddr));
			RGXCheckFaultAddress(psDevInfo, &sFaultDevVAddr, &sPCDevPAddr);
		}

#if defined(SUPPORT_PAGE_FAULT_DEBUG)

		 /* look to see if we have already processed this fault.
		  * if so then use the previously acquired information.
		  */
		OSLockAcquire(psDevInfo->hDebugFaultInfoLock);
		psInfo = _QueryFaultInfo(pfnDumpDebugPrintf, sFaultDevVAddr, ui64CRTimer);

		if(psInfo == IMG_NULL)
		{
			if(sPCDevPAddr.uiAddr != RGXFWIF_INVALID_PC_PHYADDR)
			{
				/* look up the process details for the faulting page catalogue */
				bFound = RGXPCAddrToProcessInfo(psDevInfo, sPCDevPAddr, &sProcessInfo);

				if(bFound)
				{
					IMG_BOOL bHits;
					DEVICEMEM_HISTORY_QUERY_OUT asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_COUNT] = {{ 0 }};

					/* get any DevicememHistory data for the faulting address */
					bHits = _GetDevicememHistoryData(sProcessInfo.uiPID,
										sFaultDevVAddr,
										asQueryOut,
										ui32PageSizeBytes);

					if(bHits)
					{
						psInfo = _AddFaultInfo(&sProcessInfo,
											sFaultDevVAddr,
											ui64CRTimer,
											asQueryOut);
					}
				}
				else
				{
					PVR_DUMPDEBUG_LOG(("Could not find PID for PC 0x%016llX", sPCDevPAddr.uiAddr));
				}
			}
			else
			{
				PVR_DUMPDEBUG_LOG(("Page fault not applicable to Devmem History"));
			}
		}

		if(psInfo != IMG_NULL)
		{
			_PrintFaultInfo(pfnDumpDebugPrintf, psInfo, NULL);
		}

		OSLockRelease(psDevInfo->hDebugFaultInfoLock);
#endif

	}

}
#endif


#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
/*!
*******************************************************************************

 @Function	_RGXDumpRGXMMUFaultStatus

 @Description

 Dump MMU Fault status in human readable form.

 @Input psDevInfo				- RGX device info
 @Input ui64MMUStatus			- MMU Status register value
 @Input bSummary				- Flag to check whether the function is called
 	 	 	 	 	 	 	 	  as a part of the debug dump summary or
								  as a part of a HWR log
 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID _RGXDumpRGXMMUFaultStatus(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                          PVRSRV_RGXDEV_INFO    *psDevInfo,
                                          IMG_UINT64            ui64MMUStatus,
                                          IMG_BOOL              bSummary)
{
	if (ui64MMUStatus == 0x0)
	{
		PVR_DUMPDEBUG_LOG(("MMU (Core) - OK"));
	}
	else
	{
		IMG_UINT32 ui32PC        = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_CONTEXT_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_CONTEXT_SHIFT;
		IMG_UINT64 ui64Addr      = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_ADDRESS_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_ADDRESS_SHIFT;
		IMG_UINT32 ui32Requester = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_REQ_ID_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_REQ_ID_SHIFT;
		IMG_UINT32 ui32SideBand  = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_TAG_SB_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_TAG_SB_SHIFT;
		IMG_UINT32 ui32MMULevel  = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_LEVEL_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_LEVEL_SHIFT;
		IMG_BOOL bRead           = (ui64MMUStatus & RGX_CR_MMU_FAULT_STATUS_RNW_EN) != 0;
		IMG_BOOL bFault          = (ui64MMUStatus & RGX_CR_MMU_FAULT_STATUS_FAULT_EN) != 0;
		IMG_BOOL bROFault        = ((ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_TYPE_CLRMSK) >>
		                            RGX_CR_MMU_FAULT_STATUS_TYPE_SHIFT) == 0x2;
		IMG_BOOL bProtFault      = ((ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_TYPE_CLRMSK) >>
		                            RGX_CR_MMU_FAULT_STATUS_TYPE_SHIFT) == 0x3;
		IMG_CHAR aszScratch[RGX_DEBUG_STR_SIZE];
		IMG_CHAR *pszTagID;
		IMG_CHAR *pszTagSB;

		_RGXDecodeMMUReqTags(ui32Requester, ui32SideBand, &pszTagID, &pszTagSB, aszScratch, RGX_DEBUG_STR_SIZE);

		PVR_DUMPDEBUG_LOG(("%sMMU (Core) - FAULT:",  (bSummary)?"":"    "));
		PVR_DUMPDEBUG_LOG(("%s  * MMU status (0x%016llX): PC = %d, %s 0x%010llX, %s (%s)%s%s%s%s.",
						  (bSummary)?"":"    ",
						  ui64MMUStatus,
						  ui32PC,
		                  (bRead)?"Reading from":"Writing to",
						  ui64Addr,
						  pszTagID,
						  pszTagSB,
						  (bFault)?", Fault":"",
						  (bROFault)?", Read Only fault":"",
						  (bProtFault)?", PM/META protection fault":"",
						  _RGXDecodeMMULevel(ui32MMULevel)));
	}
}


/*!
*******************************************************************************

 @Function	_RGXDumpRGXMMUMetaFaultStatus

 @Description

 Dump MMU Meta Fault state in human readable form.

 @Input psDevInfo				- RGX device info
 @Input ui64MMUStatus			- MMU Status register value
 @Input bSummary				- Flag to check whether the function is called
 	 	 	 	 	 	 	 	  as a part of the debug dump summary or
								  as a part of a HWR log
 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID _RGXDumpRGXMMUMetaFaultStatus(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                              PVRSRV_RGXDEV_INFO    *psDevInfo,
                                              IMG_UINT64            ui64MMUStatus,
                                              IMG_BOOL              bSummary)
{
	if (ui64MMUStatus == 0x0)
	{
		PVR_DUMPDEBUG_LOG(("MMU (Meta) - OK"));
	}
	else
	{
		IMG_UINT32 ui32PC        = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_META_CONTEXT_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_META_CONTEXT_SHIFT;
		IMG_UINT64 ui64Addr      = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_META_ADDRESS_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_META_ADDRESS_SHIFT;
		IMG_UINT32 ui32SideBand  = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_META_TAG_SB_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_META_TAG_SB_SHIFT;
		IMG_UINT32 ui32Requester = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_META_REQ_ID_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_META_REQ_ID_SHIFT;
		IMG_UINT32 ui32MMULevel  = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_META_LEVEL_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_META_LEVEL_SHIFT;
		IMG_BOOL bRead           = (ui64MMUStatus & RGX_CR_MMU_FAULT_STATUS_META_RNW_EN) != 0;
		IMG_BOOL bFault          = (ui64MMUStatus & RGX_CR_MMU_FAULT_STATUS_META_FAULT_EN) != 0;
		IMG_BOOL bROFault        = ((ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_META_TYPE_CLRMSK) >>
		                            RGX_CR_MMU_FAULT_STATUS_META_TYPE_SHIFT) == 0x2;
		IMG_BOOL bProtFault      = ((ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_META_TYPE_CLRMSK) >>
		                            RGX_CR_MMU_FAULT_STATUS_META_TYPE_SHIFT) == 0x3;
		IMG_CHAR aszScratch[RGX_DEBUG_STR_SIZE];
		IMG_CHAR *pszTagID;
		IMG_CHAR *pszTagSB;

		_RGXDecodeMMUReqTags(ui32Requester, ui32SideBand, &pszTagID, &pszTagSB, aszScratch, RGX_DEBUG_STR_SIZE);

		PVR_DUMPDEBUG_LOG(("%sMMU (Meta) - FAULT:",  (bSummary)?"":"    "));
		PVR_DUMPDEBUG_LOG(("%s  * MMU status (0x%016llX): PC = %d, %s 0x%010llX, %s (%s)%s%s%s%s.",
						  (bSummary)?"":"    ",
						  ui64MMUStatus,
						  ui32PC,
		                  (bRead)?"Reading from":"Writing to",
						  ui64Addr,
						  pszTagID,
						  pszTagSB,
						  (bFault)?", Fault":"",
						  (bROFault)?", Read Only fault":"",
						  (bProtFault)?", PM/META protection fault":"",
						  _RGXDecodeMMULevel(ui32MMULevel)));
	}
}
#endif


/*!
*******************************************************************************

 @Function	_RGXDumpFWAssert

 @Description

 Dump FW assert strings when a thread asserts.

 @Input psRGXFWIfTraceBufCtl	- RGX FW trace buffer

 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID _RGXDumpFWAssert(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                 RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl)
{
	IMG_CHAR    *pszTraceAssertPath;
	IMG_CHAR    *pszTraceAssertInfo;
	IMG_INT32   ui32TraceAssertLine;
	IMG_UINT32  i;

	for (i = 0; i < RGXFW_THREAD_NUM; i++)
	{
		pszTraceAssertPath = psRGXFWIfTraceBufCtl->sTraceBuf[i].sAssertBuf.szPath;
		pszTraceAssertInfo = psRGXFWIfTraceBufCtl->sTraceBuf[i].sAssertBuf.szInfo;
		ui32TraceAssertLine = psRGXFWIfTraceBufCtl->sTraceBuf[i].sAssertBuf.ui32LineNum;

		/* print non null assert strings */
		if (*pszTraceAssertInfo)
		{
			PVR_DUMPDEBUG_LOG(("FW-T%d Assert: %s (%s:%d)", 
			                  i, pszTraceAssertInfo, pszTraceAssertPath, ui32TraceAssertLine));
		}
	}
}

static IMG_VOID _RGXDumpFWPoll(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                               RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl)
{
	IMG_UINT32 i;
	for (i = 0; i < RGXFW_THREAD_NUM; i++)
	{
		if (psRGXFWIfTraceBufCtl->aui32CrPollAddr[i])
		{
			PVR_DUMPDEBUG_LOG(("T%u polling %s (reg:0x%08X mask:0x%08X)",
			                  i,
			                  ((psRGXFWIfTraceBufCtl->aui32CrPollAddr[i] & RGXFW_POLL_TYPE_SET)?("set"):("unset")), 
			                  psRGXFWIfTraceBufCtl->aui32CrPollAddr[i] & ~RGXFW_POLL_TYPE_SET, 
			                  psRGXFWIfTraceBufCtl->aui32CrPollMask[i]));
		}
	}

}

static IMG_VOID _RGXDumpFWHWRInfo(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                  RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl, PVRSRV_RGXDEV_INFO *psDevInfo)
{
	IMG_BOOL        	bAnyLocked = IMG_FALSE;
	IMG_UINT32      	dm, i;
	IMG_UINT32      	ui32LineSize;
	IMG_CHAR	    	*pszLine, *pszTemp;
	const IMG_CHAR 		*apszDmNames[RGXFWIF_DM_MAX + 1] = { "GP(", "2D(", "TA(", "3D(", "CDM(",
#if defined(RGX_FEATURE_RAY_TRACING)
								 "RTU(", "SHG(",
#endif /* RGX_FEATURE_RAY_TRACING */
								 NULL };

	const IMG_CHAR 		*pszMsgHeader = "Number of HWR: ";
	IMG_CHAR 			*pszLockupType = "";
	RGXFWIF_HWRINFOBUF 	*psHWInfoBuf = psDevInfo->psRGXFWIfHWRInfoBuf;
	RGX_HWRINFO 		*psHWRInfo;
	IMG_UINT32      	ui32MsgHeaderSize = OSStringLength(pszMsgHeader);
	IMG_UINT32			ui32HWRRecoveryFlags;
	IMG_UINT32			ui32ReadIndex;

	for (dm = 0; dm < RGXFWIF_DM_MAX; dm++)
	{
		if (psRGXFWIfTraceBufCtl->aui16HwrDmLockedUpCount[dm]  ||
		    psRGXFWIfTraceBufCtl->aui16HwrDmOverranCount[dm])
		{
			bAnyLocked = IMG_TRUE;
			break;					
		}
	}

	if (!bAnyLocked && (psRGXFWIfTraceBufCtl->ui32HWRStateFlags & RGXFWIF_HWR_HARDWARE_OK))
	{
		/* No HWR situation, print nothing */
		return;
	}

	ui32LineSize = sizeof(IMG_CHAR) * (	ui32MsgHeaderSize + 
			(RGXFWIF_DM_MAX*(	4/*DM name + left parenthesis*/ + 
								5/*UINT16 max num of digits*/ + 
								1/*slash*/ + 
								5/*UINT16 max num of digits*/ + 
								3/*right parenthesis + comma + space*/)) + 
			7 + (RGXFWIF_DM_MAX*6)/* FALSE() + (UINT16 max num + comma) per DM */ +
			1/* \0 */);

	pszLine = OSAllocMem(ui32LineSize);
	if (pszLine == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"_RGXDumpRGXDebugSummary: Out of mem allocating line string (size: %d)", ui32LineSize));
		return;
	}

	OSStringCopy(pszLine,pszMsgHeader);
	pszTemp = pszLine + ui32MsgHeaderSize;

	for (dm = 0; (dm < RGXFWIF_DM_MAX) && (apszDmNames[dm] != IMG_NULL); dm++)
	{
		OSStringCopy(pszTemp,apszDmNames[dm]);
		pszTemp += OSStringLength(apszDmNames[dm]);
		pszTemp += OSSNPrintf(pszTemp, 
				5 + 1 + 5 + 1 + 5 + 1 + 1 + 1 + 1 /* UINT16 + slash + UINT16 + plus + UINT16 + right parenthesis + comma + space + \0 */,
				"%u/%u+%u), ",
				psRGXFWIfTraceBufCtl->aui16HwrDmRecoveredCount[dm],
				psRGXFWIfTraceBufCtl->aui16HwrDmLockedUpCount[dm],
				psRGXFWIfTraceBufCtl->aui16HwrDmOverranCount[dm]);
	}

	OSStringCopy(pszTemp, "FALSE(");
	pszTemp += 6;

	for (dm = 0; (dm < RGXFWIF_DM_MAX) && (apszDmNames[dm] != IMG_NULL); dm++)
	{
		pszTemp += OSSNPrintf(pszTemp, 
				5 + 1 + 1 /* UINT16 max num + comma + \0 */,
				(dm < RGXFWIF_DM_MAX-1 ? "%u," : "%u)"),
				psRGXFWIfTraceBufCtl->aui16HwrDmFalseDetectCount[dm]);
	}

	PVR_DUMPDEBUG_LOG((pszLine));

	OSFreeMem(pszLine);

	/* Print out per HWR info */
	for (dm = 0; (dm < RGXFWIF_DM_MAX) && (apszDmNames[dm] != IMG_NULL); dm++)
	{
		if (dm == RGXFWIF_DM_GP)
		{
			PVR_DUMPDEBUG_LOG(("DM %d (GP)", dm));
		}
		else
		{
			PVR_DUMPDEBUG_LOG(("DM %d (HWRflags 0x%08x)", dm, psRGXFWIfTraceBufCtl->aui32HWRRecoveryFlags[dm]));
		}

		ui32ReadIndex = 0;
		for(i = 0 ; i < RGXFWIF_HWINFO_MAX ; i++)
		{
			psHWRInfo = &psHWInfoBuf->sHWRInfo[ui32ReadIndex];

			if((psHWRInfo->eDM == dm) && (psHWRInfo->ui32HWRNumber != 0))
			{
				IMG_UINT64 ui64OSTimeStamp, ui64DeltaTime, ui64Seconds, ui64Nanoseconds;
				IMG_UINT32 ui32CRDeltaToOSDeltaKNs = psHWRInfo->sTimeCorr.ui32CRDeltaToOSDeltaKNs;
				IMG_UINT32 ui32Remainder;

				/* Get delta CR, convert to delta nS and add the result to the correlated OS timestamp */
				ui64DeltaTime = psHWRInfo->ui64CRTimer - psHWRInfo->sTimeCorr.ui64CRTimeStamp;
				ui64DeltaTime = RGXFWIF_GET_DELTA_OSTIME_NS(ui64DeltaTime, ui32CRDeltaToOSDeltaKNs);
				ui64OSTimeStamp = psHWRInfo->sTimeCorr.ui64OSTimeStamp + ui64DeltaTime;

				/* Split timestamp in seconds and nanoseconds */
				ui64Seconds = OSDivide64r64(ui64OSTimeStamp, 1000000000, &ui32Remainder);
				ui64Nanoseconds = ui64OSTimeStamp - (ui64Seconds * 1000000000ULL);

				ui32HWRRecoveryFlags = psHWRInfo->ui32HWRRecoveryFlags;
				if(ui32HWRRecoveryFlags & RGXFWIF_DM_STATE_GUILTY_LOCKUP) { pszLockupType = ", Guilty Lockup"; }
				else if (ui32HWRRecoveryFlags & RGXFWIF_DM_STATE_INNOCENT_LOCKUP) { pszLockupType = ", Innocent Lockup"; }
				else if (ui32HWRRecoveryFlags & RGXFWIF_DM_STATE_GUILTY_OVERRUNING) { pszLockupType = ", Guilty Overrun"; }
				else if (ui32HWRRecoveryFlags & RGXFWIF_DM_STATE_GUILTY_LOCKUP) { pszLockupType = ", Innocent Overrun"; }

				PVR_DUMPDEBUG_LOG(("  Recovery %d: PID = %d, frame = %d, HWRTData = 0x%08X,"
				                   " EventStatus = 0x%08X, CRTimer = 0x%012llX, OSTimer = %llu.%09llu%s",
				                   psHWRInfo->ui32HWRNumber,
				                   psHWRInfo->ui32PID,
				                   psHWRInfo->ui32FrameNum,
				                   psHWRInfo->ui32ActiveHWRTData,
				                   psHWRInfo->ui32EventStatus,
				                   psHWRInfo->ui64CRTimer,
				                   ui64Seconds,
				                   ui64Nanoseconds,
				                   pszLockupType));

				switch(psHWRInfo->eHWRType)
				{
#if !defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
					case RGX_HWRTYPE_BIF0FAULT:
					case RGX_HWRTYPE_BIF1FAULT:
					{
						_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, psDevInfo, RGXFWIF_HWRTYPE_BIF_BANK_GET(psHWRInfo->eHWRType),
										psHWRInfo->uHWRData.sBIFInfo.ui64BIFMMUStatus,
										psHWRInfo->uHWRData.sBIFInfo.ui64BIFReqStatus,
										psHWRInfo->uHWRData.sBIFInfo.ui64PCAddress,
										psHWRInfo->ui64CRTimer,
										IMG_FALSE);
					}
					break;
#if defined(RGX_FEATURE_CLUSTER_GROUPING)
					case RGX_HWRTYPE_TEXASBIF0FAULT:
					{
						_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, psDevInfo, RGXDBG_TEXAS_BIF,
										psHWRInfo->uHWRData.sBIFInfo.ui64BIFMMUStatus,
										psHWRInfo->uHWRData.sBIFInfo.ui64BIFReqStatus,
										psHWRInfo->uHWRData.sBIFInfo.ui64PCAddress,
										psHWRInfo->ui64CRTimer,
										IMG_FALSE);
					}
					break;
#endif
#if defined(RGX_FEATURE_RAY_TRACING)
					case RGX_HWRTYPE_DPXMMUFAULT:
					{
						_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, psDevInfo, RGXDBG_DPX_BIF,
										psHWRInfo->uHWRData.sBIFInfo.ui64BIFMMUStatus,
										psHWRInfo->uHWRData.sBIFInfo.ui64BIFReqStatus,
										psHWRInfo->uHWRData.sBIFInfo.ui64PCAddress,
										psHWRInfo->ui64CRTimer,
										IMG_FALSE);
					}
					break;
#endif
#else
					case RGX_HWRTYPE_MMUFAULT:
					{
						_RGXDumpRGXMMUFaultStatus(pfnDumpDebugPrintf, psDevInfo,
						                          psHWRInfo->uHWRData.sMMUInfo.ui64MMUStatus,
						                          IMG_FALSE);
					}
					break;

					case RGX_HWRTYPE_MMUMETAFAULT:
					{
						_RGXDumpRGXMMUMetaFaultStatus(pfnDumpDebugPrintf, psDevInfo,
						                              psHWRInfo->uHWRData.sMMUInfo.ui64MMUStatus,
						                              IMG_FALSE);
					}
					break;
#endif

					case RGX_HWRTYPE_POLLFAILURE:
					{
						PVR_DUMPDEBUG_LOG(("    T%u polling %s (reg:0x%08X mask:0x%08X)",
										  psHWRInfo->uHWRData.sPollInfo.ui32ThreadNum,
										  ((psHWRInfo->uHWRData.sPollInfo.ui32CrPollAddr & RGXFW_POLL_TYPE_SET)?("set"):("unset")),
										  psHWRInfo->uHWRData.sPollInfo.ui32CrPollAddr & ~RGXFW_POLL_TYPE_SET,
										  psHWRInfo->uHWRData.sPollInfo.ui32CrPollMask));
					}
					break;

					case RGX_HWRTYPE_OVERRUN:
					case RGX_HWRTYPE_UNKNOWNFAILURE:
					{
						/* Nothing to dump */
					}
					break;

					default:
					{
						PVR_ASSERT(IMG_FALSE);
					}
					break;
				}
			}

			if(ui32ReadIndex == RGXFWIF_HWINFO_MAX_FIRST - 1)
							ui32ReadIndex = psHWInfoBuf->ui32WriteIndex;
			else
				ui32ReadIndex = (ui32ReadIndex + 1) - (ui32ReadIndex / RGXFWIF_HWINFO_LAST_INDEX) * RGXFWIF_HWINFO_MAX_LAST;
		}
	}	
}

#if !defined(NO_HARDWARE)

/*!
*******************************************************************************

 @Function	_CheckForPendingPage

 @Description

 Check if the MMU indicates it is blocked on a pending page

 @Input psDevInfo	 - RGX device info

 @Return   IMG_BOOL      - IMG_TRUE if there is a pending page

******************************************************************************/
static INLINE IMG_BOOL _CheckForPendingPage(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	IMG_UINT32 ui32BIFMMUEntry;

	ui32BIFMMUEntry = OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_MMU_ENTRY);

	if(ui32BIFMMUEntry & RGX_CR_BIF_MMU_ENTRY_PENDING_EN)
	{
		return IMG_TRUE;
	}
	else
	{
		return IMG_FALSE;
	}
}

/*!
*******************************************************************************

 @Function	_GetPendingPageInfo

 @Description

 Get information about the pending page from the MMU status registers

 @Input psDevInfo	 - RGX device info
 @Output psDevVAddr      - The device virtual address of the pending MMU address translation
 @Output pui32CatBase    - The page catalog base
 @Output pui32DataType   - The MMU entry data type

 @Return   void

******************************************************************************/
static void _GetPendingPageInfo(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_DEV_VIRTADDR *psDevVAddr,
									IMG_UINT32 *pui32CatBase,
									IMG_UINT32 *pui32DataType)
{
	IMG_UINT64 ui64BIFMMUEntryStatus;

	ui64BIFMMUEntryStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_MMU_ENTRY_STATUS);

	psDevVAddr->uiAddr = (ui64BIFMMUEntryStatus & ~RGX_CR_BIF_MMU_ENTRY_STATUS_ADDRESS_CLRMSK);

	*pui32CatBase = (ui64BIFMMUEntryStatus & ~RGX_CR_BIF_MMU_ENTRY_STATUS_CAT_BASE_CLRMSK) >>
								RGX_CR_BIF_MMU_ENTRY_STATUS_CAT_BASE_SHIFT;

	*pui32DataType = (ui64BIFMMUEntryStatus & ~RGX_CR_BIF_MMU_ENTRY_STATUS_DATA_TYPE_CLRMSK) >>
								RGX_CR_BIF_MMU_ENTRY_STATUS_DATA_TYPE_SHIFT;
}

#endif

/*!
*******************************************************************************

 @Function	_RGXDumpRGXDebugSummary

 @Description

 Dump a summary in human readable form with the RGX state

 @Input psDevInfo	 - RGX device info

 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID _RGXDumpRGXDebugSummary(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                        PVRSRV_RGXDEV_INFO *psDevInfo, IMG_BOOL bRGXPoweredON)
{
	IMG_CHAR *pszState, *pszReason;
	RGXFWIF_TRACEBUF *psRGXFWIfTraceBuf = psDevInfo->psRGXFWIfTraceBuf;

#if defined(NO_HARDWARE)
	PVR_UNREFERENCED_PARAMETER(bRGXPoweredON);
#else
	if (bRGXPoweredON)
	{
#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
		IMG_UINT64	ui64RegValMMUStatus;

		ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_MMU_FAULT_STATUS);
		_RGXDumpRGXMMUFaultStatus(pfnDumpDebugPrintf, psDevInfo, ui64RegValMMUStatus, IMG_TRUE);

		ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_MMU_FAULT_STATUS_META);
		_RGXDumpRGXMMUMetaFaultStatus(pfnDumpDebugPrintf, psDevInfo, ui64RegValMMUStatus, IMG_TRUE);
#else
		IMG_UINT64	ui64RegValMMUStatus, ui64RegValREQStatus;

		ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_FAULT_BANK0_MMU_STATUS);
		ui64RegValREQStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_FAULT_BANK0_REQ_STATUS);

		_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, psDevInfo, RGXDBG_BIF0, ui64RegValMMUStatus, ui64RegValREQStatus, 0, 0, IMG_TRUE);

		ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_FAULT_BANK1_MMU_STATUS);
		ui64RegValREQStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_FAULT_BANK1_REQ_STATUS);

		_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, psDevInfo, RGXDBG_BIF1, ui64RegValMMUStatus, ui64RegValREQStatus, 0, 0, IMG_TRUE);

#if defined(RGX_FEATURE_CLUSTER_GROUPING)
#if defined(RGX_NUM_PHANTOMS)
		{
			IMG_UINT32  ui32Phantom;
			
			for (ui32Phantom = 0;  ui32Phantom < RGX_NUM_PHANTOMS;  ui32Phantom++)
			{
				/* This can't be done as it may interfere with the FW... */
				/*OSWriteHWReg64(RGX_CR_TEXAS_INDIRECT, ui32Phantom);*/
				
				ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_TEXAS_BIF_FAULT_BANK0_MMU_STATUS);
				ui64RegValREQStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_TEXAS_BIF_FAULT_BANK0_REQ_STATUS);

				_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, psDevInfo, RGXDBG_TEXAS, ui64RegValMMUStatus, ui64RegValREQStatus, 0, 0, IMG_TRUE);
			}
		}
#else
		ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_TEXAS_BIF_FAULT_BANK0_MMU_STATUS);
		ui64RegValREQStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_TEXAS_BIF_FAULT_BANK0_REQ_STATUS);

		_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, psDevInfo, RGXDBG_TEXAS_BIF, ui64RegValMMUStatus, ui64RegValREQStatus, 0, 0, IMG_TRUE);
#endif
#endif
#endif
#if defined(RGX_FEATURE_RAY_TRACING)
		ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, DPX_CR_BIF_FAULT_BANK_MMU_STATUS);
		ui64RegValREQStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, DPX_CR_BIF_FAULT_BANK_REQ_STATUS);

		_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, psDevInfo, RGXDBG_DPX_BIF, ui64RegValMMUStatus, ui64RegValREQStatus, 0, 0, IMG_TRUE);
#endif

		if(_CheckForPendingPage(psDevInfo))
		{
			IMG_UINT32 ui32CatBase;
			IMG_UINT32 ui32DataType;
			IMG_DEV_VIRTADDR sDevVAddr;

			PVR_DUMPDEBUG_LOG(("MMU Pending page: Yes"));

			_GetPendingPageInfo(psDevInfo, &sDevVAddr, &ui32CatBase, &ui32DataType);

			if(ui32CatBase >= 8)
			{
				PVR_DUMPDEBUG_LOG(("Cannot check address on PM cat base %u", ui32CatBase));
			}
			else
			{
				IMG_DEV_PHYADDR sPCDevPAddr;

				sPCDevPAddr.uiAddr = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_CAT_BASEN(ui32CatBase));

				PVR_DUMPDEBUG_LOG(("Checking device virtual address " IMG_DEV_VIRTADDR_FMTSPEC
							" on cat base %u. PC Addr = 0x%llX",
								(unsigned long long) sDevVAddr.uiAddr,
								ui32CatBase,
								(unsigned long long) sPCDevPAddr.uiAddr));
				RGXCheckFaultAddress(psDevInfo, &sDevVAddr, &sPCDevPAddr);
			}
		}
	}
#endif /* NO_HARDWARE */

	/* Firmware state */
	switch (psDevInfo->psDeviceNode->eHealthStatus)
	{
		case PVRSRV_DEVICE_HEALTH_STATUS_OK:  pszState = "OK";  break;
		case PVRSRV_DEVICE_HEALTH_STATUS_NOT_RESPONDING:  pszState = "NOT RESPONDING";  break;
		case PVRSRV_DEVICE_HEALTH_STATUS_DEAD:  pszState = "DEAD";  break;
		default:  pszState = "UNKNOWN";  break;
	}

	switch (psDevInfo->psDeviceNode->eHealthReason)
	{
		case PVRSRV_DEVICE_HEALTH_REASON_NONE:  pszReason = "";  break;
		case PVRSRV_DEVICE_HEALTH_REASON_ASSERTED:  pszReason = " - FW Assert";  break;
		case PVRSRV_DEVICE_HEALTH_REASON_POLL_FAILING:  pszReason = " - Poll failure";  break;
		case PVRSRV_DEVICE_HEALTH_REASON_TIMEOUTS:  pszReason = " - Global Event Object timeouts rising";  break;
		case PVRSRV_DEVICE_HEALTH_REASON_QUEUE_CORRUPT:  pszReason = " - KCCB offset invalid";  break;
		case PVRSRV_DEVICE_HEALTH_REASON_QUEUE_STALLED:  pszReason = " - KCCB stalled";  break;
		default:  pszReason = " - Unknown reason";  break;
	}

	if (psRGXFWIfTraceBuf == IMG_NULL)
	{
		PVR_DUMPDEBUG_LOG(("RGX FW State: %s%s", pszState, pszReason));

		/* can't dump any more information */
		return;
	}
	
	PVR_DUMPDEBUG_LOG(("RGX FW State: %s%s (HWRState 0x%08x)", pszState, pszReason, psRGXFWIfTraceBuf->ui32HWRStateFlags));
	PVR_DUMPDEBUG_LOG(("RGX FW Power State: %s (APM %s: %d ok, %d denied, %d other, %d total)", 
	                  pszPowStateName[psRGXFWIfTraceBuf->ePowState],
	                  (psDevInfo->pvAPMISRData)?"enabled":"disabled",
	                  psDevInfo->ui32ActivePMReqOk,
	                  psDevInfo->ui32ActivePMReqDenied,
	                  psDevInfo->ui32ActivePMReqTotal - psDevInfo->ui32ActivePMReqOk - psDevInfo->ui32ActivePMReqDenied,
	                  psDevInfo->ui32ActivePMReqTotal));


	_RGXDumpFWAssert(pfnDumpDebugPrintf, psRGXFWIfTraceBuf);

	_RGXDumpFWPoll(pfnDumpDebugPrintf, psRGXFWIfTraceBuf);

	_RGXDumpFWHWRInfo(pfnDumpDebugPrintf, psRGXFWIfTraceBuf, psDevInfo);

}

static IMG_VOID _RGXDumpMetaSPExtraDebugInfo(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                             PVRSRV_RGXDEV_INFO *psDevInfo)
{
/* List of extra META Slave Port debug registers */
#define RGX_META_SP_EXTRA_DEBUG \
			X(RGX_CR_META_SP_MSLVCTRL0) \
			X(RGX_CR_META_SP_MSLVCTRL1) \
			X(RGX_CR_META_SP_MSLVIRQSTATUS) \
			X(RGX_CR_META_SP_MSLVIRQENABLE) \
			X(RGX_CR_META_SP_MSLVIRQLEVEL)

	IMG_UINT32 ui32Idx, ui32RegIdx;
	IMG_UINT32 ui32RegVal;
	IMG_UINT32 ui32RegAddr;

	const IMG_UINT32 aui32DebugRegAddr [] = {
#define X(A) A,
		RGX_META_SP_EXTRA_DEBUG
#undef X
		};

	const IMG_CHAR* apszDebugRegName [] = {
#define X(A) #A,
	RGX_META_SP_EXTRA_DEBUG
#undef X
	};
	
	const IMG_UINT32 aui32Debug2RegAddr [] = {0xA28, 0x0A30, 0x0A38};

	PVR_DUMPDEBUG_LOG(("META Slave Port extra debug:"));

	/* dump first set of Slave Port debug registers */
	for (ui32Idx = 0; ui32Idx < sizeof(aui32DebugRegAddr)/sizeof(IMG_UINT32); ui32Idx++)
	{
		const IMG_CHAR* pszRegName = apszDebugRegName[ui32Idx];

		ui32RegAddr = aui32DebugRegAddr[ui32Idx];
		ui32RegVal = OSReadHWReg32(psDevInfo->pvRegsBaseKM, ui32RegAddr);
		PVR_DUMPDEBUG_LOG(("  * %s: 0x%8.8X", pszRegName, ui32RegVal));
	}

	/* dump second set of Slave Port debug registers */
	for (ui32Idx = 0; ui32Idx < 4; ui32Idx++)
	{
		OSWriteHWReg32(psDevInfo->pvRegsBaseKM, 0xA20, ui32Idx);
		ui32RegVal = OSReadHWReg32(psDevInfo->pvRegsBaseKM, 0xA20);
		PVR_DUMPDEBUG_LOG(("  * 0xA20[%d]: 0x%8.8X", ui32Idx, ui32RegVal));

	}

	for (ui32RegIdx = 0; ui32RegIdx < sizeof(aui32Debug2RegAddr)/sizeof(IMG_UINT32); ui32RegIdx++)
	{
		ui32RegAddr = aui32Debug2RegAddr[ui32RegIdx];
		for (ui32Idx = 0; ui32Idx < 2; ui32Idx++)
		{
			OSWriteHWReg32(psDevInfo->pvRegsBaseKM, ui32RegAddr, ui32Idx);
			ui32RegVal = OSReadHWReg32(psDevInfo->pvRegsBaseKM, ui32RegAddr);
			PVR_DUMPDEBUG_LOG(("  * 0x%X[%d]: 0x%8.8X", ui32RegAddr, ui32Idx, ui32RegVal));
		}
	}

}

/*
	RGXDumpDebugInfo
*/
IMG_VOID RGXDumpDebugInfo(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                          PVRSRV_RGXDEV_INFO	*psDevInfo)
{
	IMG_UINT32 i;

	for(i=0;i<=DEBUG_REQUEST_VERBOSITY_MAX;i++)
	{
		RGXDebugRequestProcess(pfnDumpDebugPrintf, psDevInfo, i);
	}
}


#if defined(PVRSRV_ENABLE_FW_TRACE_DEBUGFS)
/*
 *  Array of all the Firmware Trace log IDs used to convert the trace data.
 */
typedef struct _TRACEBUF_LOG_ {
	RGXFW_LOG_SFids	 eSFId;
	IMG_CHAR		 *pszName;
	IMG_CHAR		 *pszFmt;
	IMG_UINT32		 ui32ArgNum;
} TRACEBUF_LOG;

TRACEBUF_LOG aLogDefinitions[] = {
#define X(a, b, c, d, e) {RGXFW_LOG_CREATESFID(a,b,e), #c, d, e},
	RGXFW_LOG_SFIDLIST 
#undef X
};

#define NARGS_MASK ~(0xF<<16)
static IMG_BOOL _FirmwareTraceIntegrityCheck(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf)
{
	TRACEBUF_LOG  *psLogDef    = &aLogDefinitions[0];
	IMG_BOOL      bIntegrityOk = IMG_TRUE;

	/*
	 *  For every log ID, check the format string and number of arguments is valid.
	 */
	while (psLogDef->eSFId != RGXFW_SF_LAST)
	{
		IMG_UINT32    ui32Count;
		IMG_CHAR      *pszString;
		TRACEBUF_LOG  *psLogDef2;

		/*
		 * Check the number of arguments matches the number of '%' in the string and
		 * check that no string uses %s which is not supported as it requires a
		 * pointer to memory that is not going to be valid.
		 */
		pszString = psLogDef->pszFmt;
		ui32Count = 0;
		
		while (*pszString != '\0')
		{
			if (*pszString++ == '%')
			{
				ui32Count++;
				if (*pszString == 's')
				{
					bIntegrityOk = IMG_FALSE;
					PVR_DUMPDEBUG_LOG(("Integrity Check FAIL: %s has an unsupported type not recognized (fmt: %%%c). Please fix.",
									  psLogDef->pszName, *pszString));
				}
				else if (*pszString == '%')
				{
					/* Double % is a printable % sign and not a format string... */
					ui32Count--;
				}
			}
		}
		
		if (ui32Count != psLogDef->ui32ArgNum)
		{
			bIntegrityOk = IMG_FALSE;
			PVR_DUMPDEBUG_LOG(("Integrity Check FAIL: %s has %d arguments but only %d are specified. Please fix.",
			                  psLogDef->pszName, ui32Count, psLogDef->ui32ArgNum));
		}

		/* RGXDumpFirmwareTrace() has a hardcoded limit of supporting up to 20 arguments... */
		if (ui32Count > 20)
		{
			bIntegrityOk = IMG_FALSE;
			PVR_DUMPDEBUG_LOG(("Integrity Check FAIL: %s has %d arguments but a maximum of 20 are supported. Please fix.",
			                  psLogDef->pszName, ui32Count));
		}

		/* Check the id number is unique (don't take into account the number of arguments) */
		ui32Count = 0;
		psLogDef2 = &aLogDefinitions[0];

		while (psLogDef2->eSFId != RGXFW_SF_LAST)
		{
			if ((psLogDef->eSFId & NARGS_MASK) == (psLogDef2->eSFId & NARGS_MASK))
			{
				ui32Count++;
			}
			psLogDef2++;
		}

		if (ui32Count != 1)
		{
			bIntegrityOk = IMG_FALSE;
			PVR_DUMPDEBUG_LOG(("Integrity Check FAIL: %s id %x is not unique, there are %d more. Please fix.",
			                  psLogDef->pszName, psLogDef->eSFId, ui32Count - 1));
		}

		/* Move to the next log ID... */
		psLogDef++;
	}

	return bIntegrityOk;
}

IMG_VOID RGXDumpFirmwareTrace(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                              PVRSRV_RGXDEV_INFO  *psDevInfo)
{
	RGXFWIF_TRACEBUF  *psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBuf;
	static IMG_BOOL   bIntegrityCheckPassed = IMG_FALSE;

	/* Check that the firmware trace is correctly defined... */
	if (!bIntegrityCheckPassed)
	{
		bIntegrityCheckPassed = _FirmwareTraceIntegrityCheck(pfnDumpDebugPrintf);
		if (!bIntegrityCheckPassed)
		{
			return;
		}
	}

	/* Dump FW trace information... */
	if (psRGXFWIfTraceBufCtl != IMG_NULL)
	{
		IMG_CHAR    szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];
		IMG_UINT32  tid;
		
		/* Print the log type settings... */
		if (psRGXFWIfTraceBufCtl->ui32LogType & RGXFWIF_LOG_TYPE_GROUP_MASK)
		{
			PVR_DUMPDEBUG_LOG(("Debug log type: %s ( " RGXFWIF_LOG_ENABLED_GROUPS_LIST_PFSPEC ")",
							  ((psRGXFWIfTraceBufCtl->ui32LogType & RGXFWIF_LOG_TYPE_TRACE)?("trace"):("tbi")),
							  RGXFWIF_LOG_ENABLED_GROUPS_LIST(psRGXFWIfTraceBufCtl->ui32LogType)
							  ));
		}
		else
		{
			PVR_DUMPDEBUG_LOG(("Debug log type: none"));
		}

		/* Print the decoded log for each thread... */
		for (tid = 0;  tid < RGXFW_THREAD_NUM;  tid++) 
		{
			IMG_UINT32  *ui32TraceBuf = psRGXFWIfTraceBufCtl->sTraceBuf[tid].aui32TraceBuffer;
			IMG_UINT32  ui32TracePtr  = psRGXFWIfTraceBufCtl->sTraceBuf[tid].ui32TracePointer;
			IMG_UINT32  ui32Count     = 0;

			while (ui32Count < RGXFW_TRACE_BUFFER_SIZE)
			{
				IMG_UINT32  ui32Data, ui32DataToId;
				
				/* Find the first valid log ID, skipping whitespace... */
				do
				{
					ui32Data     = ui32TraceBuf[ui32TracePtr];
					ui32DataToId = idToStringID(ui32Data);

					/* If an unrecognized id is found check if it is valid, if it is tracebuf needs updating. */ 
					if (ui32DataToId == RGXFW_SF_LAST  &&  RGXFW_LOG_VALIDID(ui32Data))
					{
						PVR_DUMPDEBUG_LOG(("ERROR: Unrecognized id (%x). From here on the trace might be wrong!", ui32Data));
						return;
					}

					/* Update the trace pointer... */
					ui32TracePtr = (ui32TracePtr + 1) % RGXFW_TRACE_BUFFER_SIZE;
					ui32Count++;
				} while ((RGXFW_SF_LAST == ui32DataToId  ||  ui32DataToId >= RGXFW_SF_FIRST)  &&
				         ui32Count < RGXFW_TRACE_BUFFER_SIZE);

				if (ui32Count < RGXFW_TRACE_BUFFER_SIZE)
				{
					IMG_UINT64  ui64RGXTimer;
					
					/* If we hit the ASSERT message then this is the end of the log... */
					if (ui32Data == RGXFW_SF_MAIN_ASSERT_FAILED)
					{
						PVR_DUMPDEBUG_LOG(("ASSERTION %s failed at %s:%u",
										  psRGXFWIfTraceBufCtl->sTraceBuf[tid].sAssertBuf.szInfo,
										  psRGXFWIfTraceBufCtl->sTraceBuf[tid].sAssertBuf.szPath,
										  psRGXFWIfTraceBufCtl->sTraceBuf[tid].sAssertBuf.ui32LineNum));
						break;
					}

					/*
					 *  Print the trace string and provide up to 20 arguments which
					 *  printf function will be able to use. We have already checked
					 *  that no string uses more than this.
					 */
					OSStringCopy(szBuffer, "%llu:T%u-%s> ");
					OSStringCopy(&szBuffer[OSStringLength(szBuffer)], SFs[ui32DataToId].name);
					szBuffer[OSStringLength(szBuffer)-1] = '\0';
					ui64RGXTimer = (IMG_UINT64)(ui32TraceBuf[(ui32TracePtr +  0) % RGXFW_TRACE_BUFFER_SIZE]) << 32 |
					               (IMG_UINT64)(ui32TraceBuf[(ui32TracePtr +  1) % RGXFW_TRACE_BUFFER_SIZE]);
					PVR_DUMPDEBUG_LOG((szBuffer, ui64RGXTimer, tid, groups[RGXFW_SF_GID(ui32Data)],
									  ui32TraceBuf[(ui32TracePtr +  2) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr +  3) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr +  4) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr +  5) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr +  6) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr +  7) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr +  8) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr +  9) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr + 10) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr + 11) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr + 12) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr + 13) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr + 14) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr + 15) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr + 16) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr + 17) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr + 18) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr + 19) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr + 20) % RGXFW_TRACE_BUFFER_SIZE],
									  ui32TraceBuf[(ui32TracePtr + 21) % RGXFW_TRACE_BUFFER_SIZE]));

					/* Update the trace pointer... */
					ui32TracePtr = (ui32TracePtr + 2 + RGXFW_SF_PARAMNUM(ui32Data)) % RGXFW_TRACE_BUFFER_SIZE;
					ui32Count    = (ui32Count    + 2 + RGXFW_SF_PARAMNUM(ui32Data));
				}
			}
		}
	}
}
#endif


static IMG_CHAR* _RGXGetDebugDevPowerStateString(PVRSRV_DEV_POWER_STATE ePowerState)
{
	switch(ePowerState)
	{
		case PVRSRV_DEV_POWER_STATE_DEFAULT: return "DEFAULT";
		case PVRSRV_DEV_POWER_STATE_OFF: return "OFF";
		case PVRSRV_DEV_POWER_STATE_ON: return "ON";
		default: return "UNKNOWN";
	}
}

IMG_VOID RGXDebugRequestProcess(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                PVRSRV_RGXDEV_INFO	*psDevInfo,
                                IMG_UINT32			ui32VerbLevel)
{
	PVRSRV_ERROR eError = PVRSRVPowerLock();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,	"RGXDebugRequestProcess : failed to acquire lock, error:0x%x", eError));
		return;
	}

	switch (ui32VerbLevel)
	{
		case DEBUG_REQUEST_VERBOSITY_LOW :
		{
			IMG_UINT32              ui32DeviceIndex;
			PVRSRV_DEV_POWER_STATE  ePowerState;
			IMG_BOOL                bRGXPoweredON;

			ui32DeviceIndex = psDevInfo->psDeviceNode->sDevId.ui32DeviceIndex;

			eError = PVRSRVGetDevicePowerState(ui32DeviceIndex, &ePowerState);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "RGXDebugRequestProcess: Error retrieving RGX power state. No debug info dumped."));
				goto Exit;
			}

			bRGXPoweredON = (ePowerState == PVRSRV_DEV_POWER_STATE_ON);

			PVR_DUMPDEBUG_LOG(("------[ RGX summary ]------"));
			PVR_DUMPDEBUG_LOG(("RGX BVNC: %s", RGX_BVNC_KM));
			PVR_DUMPDEBUG_LOG(("RGX Power State: %s", _RGXGetDebugDevPowerStateString(ePowerState)));

			_RGXDumpRGXDebugSummary(pfnDumpDebugPrintf, psDevInfo, bRGXPoweredON);

			if (bRGXPoweredON)
			{

				PVR_DUMPDEBUG_LOG(("------[ RGX registers ]------"));
				PVR_DUMPDEBUG_LOG(("RGX Register Base Address (Linear):   0x%p", psDevInfo->pvRegsBaseKM));
				PVR_DUMPDEBUG_LOG(("RGX Register Base Address (Physical): 0x%08lX", (unsigned long)psDevInfo->sRegsPhysBase.uiAddr));

				/* Forcing bit 6 of MslvCtrl1 to 0 to avoid internal reg read going though the core */
				OSWriteHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_META_SP_MSLVCTRL1, 0x0);

				eError = RGXRunScript(psDevInfo, psDevInfo->psScripts->asDbgCommands, RGX_MAX_DEBUG_COMMANDS, PDUMP_FLAGS_CONTINUOUS, pfnDumpDebugPrintf);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_WARNING,"RGXDebugRequestProcess: RGXRunScript failed (%d) - Retry", eError));

					/* use thread1 for slave port accesses */
					OSWriteHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_META_SP_MSLVCTRL1, 0x1 << RGX_CR_META_SP_MSLVCTRL1_THREAD_SHIFT);

					eError = RGXRunScript(psDevInfo, psDevInfo->psScripts->asDbgCommands, RGX_MAX_DEBUG_COMMANDS, PDUMP_FLAGS_CONTINUOUS, pfnDumpDebugPrintf);
					if (eError != PVRSRV_OK)
					{
						PVR_DPF((PVR_DBG_ERROR,"RGXDebugRequestProcess: RGXRunScript retry failed (%d) - Dump Slave Port debug information", eError));
						_RGXDumpMetaSPExtraDebugInfo(pfnDumpDebugPrintf, psDevInfo);
					}

					/* use thread0 again */
					OSWriteHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_META_SP_MSLVCTRL1, 0x0 << RGX_CR_META_SP_MSLVCTRL1_THREAD_SHIFT);
				}
			}
			else
			{
				PVR_DUMPDEBUG_LOG((" (!) RGX power is down. No registers dumped"));
			}

			/* Dump out the kernel CCBs. */
			{
				RGXFWIF_DM	eKCCBType;
				
				for (eKCCBType = 0; eKCCBType < RGXFWIF_DM_MAX; eKCCBType++)
				{
					RGXFWIF_CCB_CTL	*psKCCBCtl = psDevInfo->apsKernelCCBCtl[eKCCBType];
		
					if (psKCCBCtl != IMG_NULL)
					{
						PVR_DUMPDEBUG_LOG(("RGX Kernel CCB %u WO:0x%X RO:0x%X",
						                  eKCCBType, psKCCBCtl->ui32WriteOffset, psKCCBCtl->ui32ReadOffset));
					}
				}
		 	}

			/* Dump out the firmware CCBs. */
			{
				RGXFWIF_DM	eFCCBType;
				
				for (eFCCBType = 0; eFCCBType < RGXFWIF_DM_MAX; eFCCBType++)
				{
					RGXFWIF_CCB_CTL	*psFCCBCtl = psDevInfo->apsFirmwareCCBCtl[eFCCBType];
		
					if (psFCCBCtl != IMG_NULL)
					{
						PVR_DUMPDEBUG_LOG(("RGX Firmware CCB %u WO:0x%X RO:0x%X",
						                  eFCCBType, psFCCBCtl->ui32WriteOffset, psFCCBCtl->ui32ReadOffset));
					}
				}
		 	}

		 	/* Dump the KCCB commands executed */
			{
				PVR_DUMPDEBUG_LOG(("RGX Kernel CCB commands executed = %d",
				                  psDevInfo->psRGXFWIfTraceBuf->ui32KCCBCmdsExecuted));
			}

		 	/* Dump the IRQ info */
			{
				PVR_DUMPDEBUG_LOG(("RGX FW IRQ count = %d, last sampled in MISR = %d",
				                  psDevInfo->psRGXFWIfTraceBuf->ui32InterruptCount,
				                  g_ui32HostSampleIRQCount));
			}

			/* Dump the FW config flags */
			{
				RGXFWIF_INIT		*psRGXFWInit;

				eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfInitMemDesc,
						(IMG_VOID **)&psRGXFWInit);

				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR,"RGXDebugRequestProcess: Failed to acquire kernel fw if ctl (%u)",
								eError));
					goto Exit;
				}

				PVR_DUMPDEBUG_LOG(("RGX FW config flags = 0x%X", psRGXFWInit->ui32ConfigFlags));

				DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfInitMemDesc);
			}

			break;

		}
		case DEBUG_REQUEST_VERBOSITY_MEDIUM :
		{
			IMG_INT tid;
			/* Dump FW trace information */
			if (psDevInfo->psRGXFWIfTraceBuf != IMG_NULL)
			{
				RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBuf;
		
				for ( tid = 0 ; tid < RGXFW_THREAD_NUM ; tid++) 
				{
					IMG_UINT32	i;
					IMG_BOOL	bPrevLineWasZero = IMG_FALSE;
					IMG_BOOL	bLineIsAllZeros = IMG_FALSE;
					IMG_UINT32	ui32CountLines = 0;
					IMG_UINT32	*pui32TraceBuffer;
					IMG_CHAR	*pszLine;
		
					pui32TraceBuffer = &psRGXFWIfTraceBufCtl->sTraceBuf[tid].aui32TraceBuffer[0];
		
					/* each element in the line is 8 characters plus a space.  The '+1' is because of the final trailing '\0'. */
					pszLine = OSAllocMem(9*RGXFW_TRACE_BUFFER_LINESIZE+1);
					if (pszLine == IMG_NULL)
					{
						PVR_DPF((PVR_DBG_ERROR,"RGXDebugRequestProcess: Out of mem allocating line string (size: %d)", 9*RGXFW_TRACE_BUFFER_LINESIZE));
						goto Exit;
					}
		
					/* Print the tracepointer */
					if (psRGXFWIfTraceBufCtl->ui32LogType & RGXFWIF_LOG_TYPE_GROUP_MASK)
					{
						PVR_DUMPDEBUG_LOG(("Debug log type: %s ( " RGXFWIF_LOG_ENABLED_GROUPS_LIST_PFSPEC ")",
						                  ((psRGXFWIfTraceBufCtl->ui32LogType & RGXFWIF_LOG_TYPE_TRACE)?("trace"):("tbi")),
						                  RGXFWIF_LOG_ENABLED_GROUPS_LIST(psRGXFWIfTraceBufCtl->ui32LogType)
						                  ));
					}
					else
					{
						PVR_DUMPDEBUG_LOG(("Debug log type: none"));
					}
					
					PVR_DUMPDEBUG_LOG(("------[ RGX FW thread %d trace START ]------", tid));
					PVR_DUMPDEBUG_LOG(("FWT[traceptr]: %X", psRGXFWIfTraceBufCtl->sTraceBuf[tid].ui32TracePointer));
					PVR_DUMPDEBUG_LOG(("FWT[tracebufsize]: %X", RGXFW_TRACE_BUFFER_SIZE));
		
					for (i = 0; i < RGXFW_TRACE_BUFFER_SIZE; i += RGXFW_TRACE_BUFFER_LINESIZE)
					{
						IMG_UINT32 k = 0;
						IMG_UINT32 ui32Line = 0x0;
						IMG_UINT32 ui32LineOffset = i*sizeof(IMG_UINT32);
						IMG_CHAR   *pszBuf = pszLine;
		
						for (k = 0; k < RGXFW_TRACE_BUFFER_LINESIZE; k++)
						{
							ui32Line |= pui32TraceBuffer[i + k];
		
							/* prepare the line to print it. The '+1' is because of the trailing '\0' added */
							OSSNPrintf(pszBuf, 9 + 1, " %08x", pui32TraceBuffer[i + k]);
							pszBuf += 9; /* write over the '\0' */
						}
		
						bLineIsAllZeros = (ui32Line == 0x0);
		
						if (bLineIsAllZeros)
						{
							if (bPrevLineWasZero)
							{
								ui32CountLines++;
							}
							else
							{
								bPrevLineWasZero = IMG_TRUE;
								ui32CountLines = 1;
								PVR_DUMPDEBUG_LOG(("FWT[%08x]: 00000000 ... 00000000", ui32LineOffset));
							}
						}
						else
						{
							if (bPrevLineWasZero  &&  ui32CountLines > 1)
							{
								PVR_DUMPDEBUG_LOG(("FWT[...]: %d lines were all zero", ui32CountLines));
							}
							bPrevLineWasZero = IMG_FALSE;

							PVR_DUMPDEBUG_LOG(("FWT[%08x]:%s", ui32LineOffset, pszLine));
						}
		
					}
					if (bPrevLineWasZero)
					{
						PVR_DUMPDEBUG_LOG(("FWT[END]: %d lines were all zero", ui32CountLines));
					}
		
					PVR_DUMPDEBUG_LOG(("------[ RGX FW thread %d trace END ]------", tid));
		
					OSFreeMem(pszLine);
				}
			}

			{
#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING) || defined(PVRSRV_ENABLE_FULL_CCB_DUMP)
				PVR_DUMPDEBUG_LOG(("------[ Full CCB Status ]------"));
#else
				PVR_DUMPDEBUG_LOG(("------[ Stalled FWCtxs ]------"));
#endif
				CheckForStalledTransferCtxt(psDevInfo, pfnDumpDebugPrintf);
				CheckForStalledRenderCtxt(psDevInfo, pfnDumpDebugPrintf);
				CheckForStalledComputeCtxt(psDevInfo, pfnDumpDebugPrintf);
#if defined(RGX_FEATURE_RAY_TRACING)
				CheckForStalledRayCtxt(psDevInfo, pfnDumpDebugPrintf);
#endif
			}
			break;
		}
		case DEBUG_REQUEST_VERBOSITY_HIGH:
		{
			PVRSRV_ERROR            eError;
			IMG_UINT32              ui32DeviceIndex;
			PVRSRV_DEV_POWER_STATE  ePowerState;
			IMG_BOOL                bRGXPoweredON;

			ui32DeviceIndex = psDevInfo->psDeviceNode->sDevId.ui32DeviceIndex;

			eError = PVRSRVGetDevicePowerState(ui32DeviceIndex, &ePowerState);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "RGXDebugRequestProcess: Error retrieving RGX power state. No debug info dumped."));
				return;
			}

			bRGXPoweredON = (ePowerState == PVRSRV_DEV_POWER_STATE_ON);

			PVR_DUMPDEBUG_LOG(("------[ Debug bus ]------"));

			_RGXDumpRGXDebugSummary(pfnDumpDebugPrintf, psDevInfo, bRGXPoweredON);

			if (bRGXPoweredON)
			{
				eError = RGXRunScript(psDevInfo, psDevInfo->psScripts->asDbgBusCommands, RGX_MAX_DBGBUS_COMMANDS, PDUMP_FLAGS_CONTINUOUS, pfnDumpDebugPrintf);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_WARNING,"RGXDebugRequestProcess: RGXRunScript failed (%s)", PVRSRVGetErrorStringKM(eError)));
				}
				break;
			}
		}
		default:
			break;
	}

Exit:
	PVRSRVPowerUnlock();
}

/*
	RGXPanic
*/
IMG_VOID RGXPanic(PVRSRV_RGXDEV_INFO	*psDevInfo)
{
	PVR_LOG(("RGX panic"));
	PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_MAX, IMG_NULL);
	OSPanic();
}

/*
	RGXQueryDMState
*/
PVRSRV_ERROR RGXQueryDMState(PVRSRV_RGXDEV_INFO *psDevInfo, RGXFWIF_DM eDM, RGXFWIF_DM_STATE *peState, RGXFWIF_DEV_VIRTADDR *psCommonContextDevVAddr)
{
	PVRSRV_ERROR	eError = PVRSRV_OK;
	RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBuf;

	if (eDM >= RGXFWIF_DM_MAX)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		PVR_DPF((PVR_DBG_ERROR,"RGXQueryDMState: eDM parameter is out of range (%u)",eError));
		return eError;
	}

	if (peState == IMG_NULL)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		PVR_DPF((PVR_DBG_ERROR,"RGXQueryDMState: peState is NULL (%u)",eError));
		return eError;
	}

	if (psCommonContextDevVAddr == IMG_NULL)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		PVR_DPF((PVR_DBG_ERROR,"RGXQueryDMState: psCommonContextDevVAddr is NULL (%u)",eError));
		return eError;
	}

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXQueryDMState: Failed (%d) to acquire address for trace buffer", eError));
		return eError;
	}

	if (psRGXFWIfTraceBufCtl->apsHwrDmFWCommonContext[eDM].ui32Addr)
	{
		*peState = RGXFWIF_DM_STATE_LOCKEDUP;
	}
	else
	{
		*peState = RGXFWIF_DM_STATE_NORMAL;
	}
	
	*psCommonContextDevVAddr = psRGXFWIfTraceBufCtl->apsHwrDmFWCommonContext[eDM];

	return eError;
}


/******************************************************************************
 End of file (rgxdebug.c)
******************************************************************************/
