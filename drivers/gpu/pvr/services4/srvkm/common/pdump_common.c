/*************************************************************************/ /*!
@Title          Common PDump functions
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

#if defined(PDUMP)
#include <stdarg.h>

#include "services_headers.h"
#include "perproc.h"

/* pdump headers */
#include "pdump_km.h"
#include "pdump_int.h"

/* Allow temporary buffer size override */
#if !defined(PDUMP_TEMP_BUFFER_SIZE)
#define PDUMP_TEMP_BUFFER_SIZE (64 * 1024U)
#endif

/* DEBUG */
#if 1
#define PDUMP_DBG(a)   PDumpOSDebugPrintf (a)
#else
#define PDUMP_DBG(a)
#endif


#define	PTR_PLUS(t, p, x) ((t)(((IMG_CHAR *)(p)) + (x)))
#define	VPTR_PLUS(p, x) PTR_PLUS(IMG_VOID *, p, x)
#define	VPTR_INC(p, x) ((p) = VPTR_PLUS(p, x))
#define MAX_PDUMP_MMU_CONTEXTS	(32)
static IMG_VOID *gpvTempBuffer = IMG_NULL;
static IMG_HANDLE ghTempBufferBlockAlloc;
static IMG_UINT16 gui16MMUContextUsage = 0;

#if defined(PDUMP_DEBUG_OUTFILES)
/* counter increments each time debug write is called */
IMG_UINT32 g_ui32EveryLineCounter = 1U;
#endif

#if defined(SUPPORT_PDUMP_MULTI_PROCESS)


IMG_BOOL _PDumpIsProcessActive(IMG_VOID)
{
	PVRSRV_PER_PROCESS_DATA* psPerProc = PVRSRVFindPerProcessData();
	if(psPerProc == IMG_NULL)
	{
		/* FIXME: kernel process logs some comments when kernel module is
		 * loaded, want to keep those.
		 */
		return IMG_TRUE;
	}
	return psPerProc->bPDumpActive;
}

#endif /* SUPPORT_PDUMP_MULTI_PROCESS */

#if defined(PDUMP_DEBUG_OUTFILES)
static INLINE
IMG_UINT32 _PDumpGetPID(IMG_VOID)
{
	PVRSRV_PER_PROCESS_DATA* psPerProc = PVRSRVFindPerProcessData();
	if(psPerProc == IMG_NULL)
	{
		/* Kernel PID */
		return 0;
	}
	return psPerProc->ui32PID;
}
#endif /* PDUMP_DEBUG_OUTFILES */

/**************************************************************************
 * Function Name  : GetTempBuffer
 * Inputs         : None
 * Outputs        : None
 * Returns        : Temporary buffer address, or IMG_NULL
 * Description    : Get temporary buffer address.
**************************************************************************/
static IMG_VOID *GetTempBuffer(IMG_VOID)
{
	/*
	 * Allocate the temporary buffer, it it hasn't been allocated already.
	 * Return the address of the temporary buffer, or IMG_NULL if it
	 * couldn't be allocated.
	 * It is expected that the buffer will be allocated once, at driver
	 * load time, and left in place until the driver unloads.
	 */

	if (gpvTempBuffer == IMG_NULL)
	{
		PVRSRV_ERROR eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					  PDUMP_TEMP_BUFFER_SIZE,
					  &gpvTempBuffer,
					  &ghTempBufferBlockAlloc,
					  "PDUMP Temporary Buffer");
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "GetTempBuffer: OSAllocMem failed: %d", eError));
		}
	}

	return gpvTempBuffer;
}

static IMG_VOID FreeTempBuffer(IMG_VOID)
{

	if (gpvTempBuffer != IMG_NULL)
	{
		PVRSRV_ERROR eError = OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
					  PDUMP_TEMP_BUFFER_SIZE,
					  gpvTempBuffer,
					  ghTempBufferBlockAlloc);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "FreeTempBuffer: OSFreeMem failed: %d", eError));
		}
		else
		{
			gpvTempBuffer = IMG_NULL;
		}
	}
}

IMG_VOID PDumpInitCommon(IMG_VOID)
{
	/* Allocate temporary buffer for copying from user space */
	(IMG_VOID) GetTempBuffer();

	/* Call environment specific PDump initialisation */
	PDumpInit();
}

IMG_VOID PDumpDeInitCommon(IMG_VOID)
{
	/* Free temporary buffer */
	FreeTempBuffer();

	/* Call environment specific PDump Deinitialisation */
	PDumpDeInit();
}

IMG_BOOL PDumpIsSuspended(IMG_VOID)
{
	return PDumpOSIsSuspended();
}

IMG_BOOL PDumpIsCaptureFrameKM(IMG_VOID)
{
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
	if( _PDumpIsProcessActive() )
	{
		return PDumpOSIsCaptureFrameKM();
	}
	return IMG_FALSE;
#else
	return PDumpOSIsCaptureFrameKM();
#endif
}

PVRSRV_ERROR PDumpSetFrameKM(IMG_UINT32 ui32Frame)
{
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
	if( _PDumpIsProcessActive() )
	{
		return PDumpOSSetFrameKM(ui32Frame);
	}
	return PVRSRV_OK;
#else
	return PDumpOSSetFrameKM(ui32Frame);
#endif
}

/**************************************************************************
 * Function Name  : PDumpRegWithFlagsKM
 * Inputs         : pszPDumpDevName, Register offset, and value to write
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a register write
**************************************************************************/
PVRSRV_ERROR PDumpRegWithFlagsKM(IMG_CHAR *pszPDumpRegName,
								IMG_UINT32 ui32Reg,
								IMG_UINT32 ui32Data,
								IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()

	PDUMP_LOCK();
	PDUMP_DBG(("PDumpRegWithFlagsKM"));

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "WRW :%s:0x%08X 0x%08X\r\n",
								pszPDumpRegName, ui32Reg, ui32Data);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : PDumpRegKM
 * Inputs         : Register offset, and value to write
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a register write
**************************************************************************/
PVRSRV_ERROR PDumpRegKM(IMG_CHAR *pszPDumpRegName,
						IMG_UINT32 ui32Reg,
						IMG_UINT32 ui32Data)
{
	return PDumpRegWithFlagsKM(pszPDumpRegName, ui32Reg, ui32Data, PDUMP_FLAGS_CONTINUOUS);
}

/**************************************************************************
 * Function Name  : PDumpRegPolWithFlagsKM
 * Inputs         : Description of what this register read is trying to do
 *					pszPDumpDevName
 *					Register offset
 *					expected value
 *					mask for that value
 * Outputs        : None
 * Returns        : None
 * Description    : Create a PDUMP string which represents a register read
 *					with the expected value
**************************************************************************/
PVRSRV_ERROR PDumpRegPolWithFlagsKM(IMG_CHAR *pszPDumpRegName,
									IMG_UINT32 ui32RegAddr, 
									IMG_UINT32 ui32RegValue, 
									IMG_UINT32 ui32Mask,
									IMG_UINT32 ui32Flags,
									PDUMP_POLL_OPERATOR	eOperator)
{
	/* Timings correct for linux and XP */
	#define POLL_DELAY			1000U
	#define POLL_COUNT_LONG		(2000000000U / POLL_DELAY)
	#define POLL_COUNT_SHORT	(1000000U / POLL_DELAY)

	PVRSRV_ERROR eErr;
	IMG_UINT32	ui32PollCount;
	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();
	PDUMP_DBG(("PDumpRegPolWithFlagsKM"));

	ui32PollCount = POLL_COUNT_LONG;

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "POL :%s:0x%08X 0x%08X 0x%08X %d %u %d\r\n",
							pszPDumpRegName, ui32RegAddr, ui32RegValue,
							ui32Mask, eOperator, ui32PollCount, POLL_DELAY);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}


/**************************************************************************
 * Function Name  : PDumpRegPol
 * Inputs         : Description of what this register read is trying to do
 *					pszPDumpDevName
 					Register offset
 *					expected value
 *					mask for that value
 * Outputs        : None
 * Returns        : None
 * Description    : Create a PDUMP string which represents a register read
 *					with the expected value
**************************************************************************/
PVRSRV_ERROR PDumpRegPolKM(IMG_CHAR *pszPDumpRegName, IMG_UINT32 ui32RegAddr, IMG_UINT32 ui32RegValue, IMG_UINT32 ui32Mask, PDUMP_POLL_OPERATOR	eOperator)
{
	return PDumpRegPolWithFlagsKM(pszPDumpRegName, ui32RegAddr, ui32RegValue, ui32Mask, PDUMP_FLAGS_CONTINUOUS, eOperator);
}

/**************************************************************************
 * Function Name  : PDumpMallocPages
 * Inputs         : psDevID, ui32DevVAddr, pvLinAddr, ui32NumBytes, hOSMemHandle
 *                : hUniqueTag
 * Outputs        : None
 * Returns        : None
 * Description    : Malloc memory pages

FIXME: This function assumes pvLinAddr is the address of the start of the 
block for this hOSMemHandle.
If this isn't true, the call to PDumpOSCPUVAddrToDevPAddr below will be 
incorrect.  (Consider using OSMemHandleToCPUPAddr() instead?)
The only caller at the moment is in buffer_manager.c, which does the right
thing.
**************************************************************************/
PVRSRV_ERROR PDumpMallocPages (PVRSRV_DEVICE_IDENTIFIER	*psDevID,
                           IMG_UINT32         ui32DevVAddr,
                           IMG_CPU_VIRTADDR   pvLinAddr,
                           IMG_HANDLE         hOSMemHandle,
                           IMG_UINT32         ui32NumBytes,
                           IMG_UINT32         ui32PageSize,
                           IMG_HANDLE         hUniqueTag,
                           IMG_UINT32		  ui32Flags)
{
	PVRSRV_ERROR eErr;
	IMG_PUINT8		pui8LinAddr;
    IMG_UINT32      ui32Offset;
	IMG_UINT32		ui32NumPages;
	IMG_DEV_PHYADDR	sDevPAddr;
	IMG_UINT32		ui32Page;
	IMG_UINT32		ui32PageSizeShift = 0;
	IMG_UINT32		ui32PageSizeTmp;
	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();

	/* However, lin addr is only required in non-linux OSes */
#if !defined(LINUX)
	PVR_ASSERT(((IMG_UINTPTR_T)pvLinAddr & HOST_PAGEMASK) == 0);
#endif

	PVR_ASSERT(((IMG_UINT32) ui32DevVAddr & HOST_PAGEMASK) == 0);
	PVR_ASSERT(((IMG_UINT32) ui32NumBytes & HOST_PAGEMASK) == 0);

	/*
	   Compute the amount to right-shift in order to divide by the page-size.
	   Required for 32-bit PAE kernels (thus phys addresses are 64-bits) where
	   64-bit division is unsupported.
	 */
	ui32PageSizeTmp = ui32PageSize;
	while (ui32PageSizeTmp >>= 1)
		ui32PageSizeShift++;

	/*
		Write a comment to the PDump2 script streams indicating the memory allocation
	*/
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "-- MALLOC :%s:VA_%08X 0x%08X %u (%d pages)\r\n",
			psDevID->pszPDumpDevName, ui32DevVAddr, ui32NumBytes, ui32PageSize, ui32NumBytes / ui32PageSize);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	/*
		Write to the MMU script stream indicating the memory allocation
	*/
	pui8LinAddr = (IMG_PUINT8) pvLinAddr;
	ui32Offset = 0;
	ui32NumPages = ui32NumBytes >> ui32PageSizeShift;
	while (ui32NumPages)
	{ 
		ui32NumPages--;

		/* See FIXME in function header. 
		 * Currently:  linux pdump uses OSMemHandle and Offset 
		 *             other OSes use the LinAddr. 
		 */
 		/* Calculate the device physical address for this page */
		PDumpOSCPUVAddrToDevPAddr(psDevID->eDeviceType,
				hOSMemHandle,
				ui32Offset,
				pui8LinAddr,
				ui32PageSize,
				&sDevPAddr);
		ui32Page = (IMG_UINT32)(sDevPAddr.uiAddr >> ui32PageSizeShift);
		/* increment kernel virtual address */
		pui8LinAddr	+= ui32PageSize;
		ui32Offset += ui32PageSize;

		sDevPAddr.uiAddr = ui32Page * ui32PageSize;

		eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "MALLOC :%s:PA_" UINTPTR_FMT DEVPADDR_FMT " %u %u 0x" DEVPADDR_FMT "\r\n",
												psDevID->pszPDumpDevName,
												(IMG_UINTPTR_T)hUniqueTag,
												sDevPAddr.uiAddr,
												ui32PageSize,
												ui32PageSize,
												sDevPAddr.uiAddr);
		if(eErr != PVRSRV_OK)
		{
			PDUMP_UNLOCK();
			return eErr;
		}
		PDumpOSWriteString2(hScript, ui32Flags);
	}

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}


/**************************************************************************
 * Function Name  : PDumpMallocPageTable
 * Inputs         : psDevId, pvLinAddr, ui32NumBytes, hUniqueTag
 * Outputs        : None
 * Returns        : None
 * Description    : Malloc memory page table
**************************************************************************/
PVRSRV_ERROR PDumpMallocPageTable (PVRSRV_DEVICE_IDENTIFIER	*psDevId,
								   IMG_HANDLE hOSMemHandle,
								   IMG_UINT32 ui32Offset,
                              	   IMG_CPU_VIRTADDR pvLinAddr,
								   IMG_UINT32 ui32PTSize,
								   IMG_UINT32 ui32Flags,
								   IMG_HANDLE hUniqueTag)
{
	PVRSRV_ERROR eErr;
	IMG_DEV_PHYADDR	sDevPAddr;
	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();
	PVR_ASSERT(((IMG_UINTPTR_T)pvLinAddr & (ui32PTSize - 1)) == 0);

	ui32Flags |= PDUMP_FLAGS_CONTINUOUS;

	/*
		Write a comment to the PDump2 script streams indicating the memory allocation
	*/
	eErr = PDumpOSBufprintf(hScript,
							ui32MaxLen,
							"-- MALLOC :%s:PAGE_TABLE 0x%08X %u\r\n",
							psDevId->pszPDumpDevName,
							ui32PTSize,
							ui32PTSize);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	/*
		Write to the MMU script stream indicating the memory allocation
	*/
	// FIXME: we'll never need more than a 4k page for a pagetable
	// fixing to 1 page for now.
	// note: when the mmu code supports packed pagetables the PTs
	// will be as small as 16bytes

	PDumpOSCPUVAddrToDevPAddr(psDevId->eDeviceType,
			hOSMemHandle, /* um - does this mean the pvLinAddr would be ignored?  Is that safe? */
			ui32Offset,
			(IMG_PUINT8) pvLinAddr,
			ui32PTSize,
			&sDevPAddr);

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "MALLOC :%s:PA_" UINTPTR_FMT DEVPADDR_FMT 
												 " 0x%X %u 0x" DEVPADDR_FMT "\r\n",
											psDevId->pszPDumpDevName,
											(IMG_UINTPTR_T)hUniqueTag,
											sDevPAddr.uiAddr,
											ui32PTSize,//size
											ui32PTSize,//alignment
											sDevPAddr.uiAddr);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : PDumpFreePages
 * Inputs         : psBMHeap, sDevVAddr, ui32NumBytes, hUniqueTag,
 					bInterLeaved
 * Outputs        : None
 * Returns        : None
 * Description    : Free memory pages
**************************************************************************/
PVRSRV_ERROR PDumpFreePages	(BM_HEAP 			*psBMHeap,
                         IMG_DEV_VIRTADDR  sDevVAddr,
                         IMG_UINT32        ui32NumBytes,
                         IMG_UINT32        ui32PageSize,
                         IMG_HANDLE        hUniqueTag,
						 IMG_BOOL		   bInterleaved,
						 IMG_BOOL		   bSparse,
						 IMG_UINT32		   ui32Flags)
{
	PVRSRV_ERROR eErr;
	IMG_UINT32 ui32NumPages, ui32PageCounter;
	IMG_DEV_PHYADDR	sDevPAddr;
	PVRSRV_DEVICE_NODE *psDeviceNode;
	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();
	PVR_ASSERT(((IMG_UINT32) sDevVAddr.uiAddr & (ui32PageSize - 1)) == 0);
	PVR_ASSERT(((IMG_UINT32) ui32NumBytes & (ui32PageSize - 1)) == 0);

	psDeviceNode = psBMHeap->pBMContext->psDeviceNode;

	/*
		Write a comment to the PDUMP2 script streams indicating the memory free
	*/
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "-- FREE :%s:VA_%08X\r\n", 
							psDeviceNode->sDevId.pszPDumpDevName, sDevVAddr.uiAddr);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}

	PDumpOSWriteString2(hScript, ui32Flags);

	/*
		Write to the MMU script stream indicating the memory free
	*/
	ui32NumPages = ui32NumBytes / ui32PageSize;
	for (ui32PageCounter = 0; ui32PageCounter < ui32NumPages; ui32PageCounter++)
	{
		if (!bInterleaved || (ui32PageCounter % 2) == 0)
		{
			sDevPAddr = psDeviceNode->pfnMMUGetPhysPageAddr(psBMHeap->pMMUHeap, sDevVAddr);

			/* With sparse mappings we expect spaces */
			if (bSparse && (sDevPAddr.uiAddr == 0))
			{
				continue;
			}

			PVR_ASSERT(sDevPAddr.uiAddr != 0);

			eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "FREE :%s:PA_" UINTPTR_FMT DEVPADDR_FMT "\r\n",
									psDeviceNode->sDevId.pszPDumpDevName, 
                                    (IMG_UINTPTR_T)hUniqueTag, 
                                    sDevPAddr.uiAddr);
			if(eErr != PVRSRV_OK)
			{
				PDUMP_UNLOCK();
				return eErr;
			}
			PDumpOSWriteString2(hScript, ui32Flags);
		}
		else
		{
			/* Gap pages in an interleaved allocation should be ignored. */
		}

		sDevVAddr.uiAddr += ui32PageSize;
	}

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : PDumpFreePageTable
 * Inputs         : psDevID, pvLinAddr, ui32NumBytes, hUniqueTag
 * Outputs        : None
 * Returns        : None
 * Description    : Free memory page table
**************************************************************************/
PVRSRV_ERROR PDumpFreePageTable	(PVRSRV_DEVICE_IDENTIFIER *psDevID,
								 IMG_HANDLE hOSMemHandle,
								 IMG_CPU_VIRTADDR   pvLinAddr,
								 IMG_UINT32         ui32PTSize,
								 IMG_UINT32			ui32Flags,
								 IMG_HANDLE         hUniqueTag)
{
	PVRSRV_ERROR eErr;
	IMG_DEV_PHYADDR	sDevPAddr;
	PDUMP_GET_SCRIPT_STRING();
	PVR_UNREFERENCED_PARAMETER(ui32PTSize);

	PDUMP_LOCK();

	/* override QAC warning about wrap around */
	PVR_ASSERT(((IMG_UINTPTR_T)pvLinAddr & (ui32PTSize-1UL)) == 0);	/* PRQA S 3382 */

	/*
		Write a comment to the PDUMP2 script streams indicating the memory free
	*/
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "-- FREE :%s:PAGE_TABLE\r\n", psDevID->pszPDumpDevName);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	/*
		Write to the MMU script stream indicating the memory free
	*/
	// FIXME: we'll never need more than a 4k page for a pagetable
	// fixing to 1 page for now.
	// note: when the mmu code supports packed pagetables the PTs
	// will be as small as 16bytes

	PDumpOSCPUVAddrToDevPAddr(psDevID->eDeviceType,
							  hOSMemHandle, /* um - does this mean the pvLinAddr would be ignored?  Is that safe? */
			0,
			(IMG_PUINT8) pvLinAddr,
			ui32PTSize,
			&sDevPAddr);

	{
		eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "FREE :%s:PA_" UINTPTR_FMT DEVPADDR_FMT "\r\n",
								psDevID->pszPDumpDevName,
								(IMG_UINTPTR_T)hUniqueTag,
								sDevPAddr.uiAddr);
		if(eErr != PVRSRV_OK)
		{
			PDUMP_UNLOCK();
			return eErr;
		}
		PDumpOSWriteString2(hScript, ui32Flags);
	}

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : PDumpPDRegWithFlags
 * Inputs         : psMMUAttrib
 *				  : ui32Reg
 *				  : ui32Data
 *				  : hUniqueTag
 * Outputs        : None
 * Returns        : None
 * Description    : Kernel Services internal pdump memory API
 *					Used for registers specifying physical addresses
 					e.g. MMU page directory register
**************************************************************************/
PVRSRV_ERROR PDumpPDRegWithFlags(PDUMP_MMU_ATTRIB *psMMUAttrib,
							IMG_UINT32 ui32Reg,
							 IMG_UINT32 ui32Data,
							 IMG_UINT32	ui32Flags,
							 IMG_HANDLE hUniqueTag)
{
	PVRSRV_ERROR eErr;
	IMG_CHAR *pszRegString;
	IMG_DEV_PHYADDR sDevPAddr;

	PDUMP_GET_SCRIPT_STRING()

	PDUMP_LOCK();
	if(psMMUAttrib->pszPDRegRegion != IMG_NULL)
	{	
		pszRegString = psMMUAttrib->pszPDRegRegion;
	}
	else
	{
		pszRegString = psMMUAttrib->sDevId.pszPDumpRegName;
	}

	/*
		Write to the MMU script stream indicating the physical page directory
	*/
#if defined(SGX_FEATURE_36BIT_MMU)
	sDevPAddr.uiAddr = ((ui32Data & psMMUAttrib->ui32PDEMask) << psMMUAttrib->ui32PDEAlignShift);

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen,
			 "WRW :%s:$1 :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X\r\n",
			 psMMUAttrib->sDevId.pszPDumpDevName,
			 psMMUAttrib->sDevId.pszPDumpDevName,
			 (IMG_UINTPTR_T)hUniqueTag,
			 sDevPAddr.uiAddr,
			 ui32Data & ~psMMUAttrib->ui32PDEMask);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "SHR :%s:$1 :%s:$1 0x4\r\n", 
			psMMUAttrib->sDevId.pszPDumpDevName,
			psMMUAttrib->sDevId.pszPDumpDevName);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen,
			 "WRW :%s:0x%08X: %s:$1\r\n",
			 pszRegString,
			 ui32Reg,
			 psMMUAttrib->sDevId.pszPDumpDevName);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);
#else
	sDevPAddr.uiAddr = ((ui32Data & psMMUAttrib->ui32PDEMask) << psMMUAttrib->ui32PDEAlignShift);

	eErr = PDumpOSBufprintf(hScript,
				ui32MaxLen,
				"WRW :%s:0x%08X :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X\r\n",
				pszRegString,
				ui32Reg,
				psMMUAttrib->sDevId.pszPDumpDevName,
				(IMG_UINTPTR_T)hUniqueTag,
				sDevPAddr.uiAddr,
				ui32Data & ~psMMUAttrib->ui32PDEMask);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);
#endif

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : PDumpPDReg
 * Inputs         : psMMUAttrib
 				  : ui32Reg
 *				  : ui32Data
 *				  : hUniqueTag
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Kernel Services internal pdump memory API
 *					Used for registers specifying physical addresses
 					e.g. MMU page directory register
**************************************************************************/
PVRSRV_ERROR PDumpPDReg	(PDUMP_MMU_ATTRIB *psMMUAttrib, 
					 IMG_UINT32 ui32Reg,
					 IMG_UINT32 ui32Data,
					 IMG_HANDLE hUniqueTag)
{
	return PDumpPDRegWithFlags(psMMUAttrib, ui32Reg, ui32Data, PDUMP_FLAGS_CONTINUOUS, hUniqueTag);
}

/**************************************************************************
 * Function Name  : PDumpMemPolKM
 * Inputs         : psMemInfo
 *				  : ui32Offset
 *				  : ui32Value
 *				  : ui32Mask
 *				  : eOperator
 *				  : ui32Flags
 *				  : hUniqueTag
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Implements Client pdump memory poll API
**************************************************************************/
PVRSRV_ERROR PDumpMemPolKM(PVRSRV_KERNEL_MEM_INFO		*psMemInfo,
						   IMG_UINT32			ui32Offset,
						   IMG_UINT32			ui32Value,
						   IMG_UINT32			ui32Mask,
						   PDUMP_POLL_OPERATOR	eOperator,
						   IMG_UINT32			ui32Flags,
						   IMG_HANDLE			hUniqueTag)
{
	#define MEMPOLL_DELAY		(1000)
	#define MEMPOLL_COUNT		(2000000000 / MEMPOLL_DELAY)

	PVRSRV_ERROR eErr;
	IMG_UINT32			ui32PageOffset;
	IMG_UINT8			*pui8LinAddr;
	IMG_DEV_PHYADDR		sDevPAddr;
	IMG_DEV_VIRTADDR	sDevVPageAddr;
	PDUMP_MMU_ATTRIB	*psMMUAttrib;

	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();
	if (PDumpOSIsSuspended())
	{
		PDUMP_UNLOCK();
		return PVRSRV_OK;
	}

	/* Check the offset and size don't exceed the bounds of the allocation */
	PVR_ASSERT((ui32Offset + sizeof(IMG_UINT32)) <= psMemInfo->uAllocSize);

	psMMUAttrib = ((BM_BUF*)psMemInfo->sMemBlk.hBuffer)->pMapping->pBMHeap->psMMUAttrib;

	/*
		Write a comment to the PDump2 script streams indicating the virtual memory pol
	*/
	eErr = PDumpOSBufprintf(hScript,
			 ui32MaxLen,
			 "-- POL :%s:VA_%08X 0x%08X 0x%08X %d %d %d\r\n",
			 psMMUAttrib->sDevId.pszPDumpDevName,
			 psMemInfo->sDevVAddr.uiAddr + ui32Offset,
			 ui32Value,
			 ui32Mask,
			 eOperator,
			 MEMPOLL_COUNT,
			 MEMPOLL_DELAY);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);


	pui8LinAddr = psMemInfo->pvLinAddrKM;

	/* Advance address by offset */
	pui8LinAddr += ui32Offset;

	/*
		query the buffer manager for the physical pages that back the
		virtual address
	*/
	PDumpOSCPUVAddrToPhysPages(psMemInfo->sMemBlk.hOSMemHandle,
			ui32Offset,
			pui8LinAddr,
			psMMUAttrib->ui32DataPageMask,
			&ui32PageOffset);

	/* calculate the DevV page address */
	sDevVPageAddr.uiAddr = psMemInfo->sDevVAddr.uiAddr + ui32Offset - ui32PageOffset;

	PVR_ASSERT((sDevVPageAddr.uiAddr & psMMUAttrib->ui32DataPageMask) == 0);

	/* get the physical page address based on the device virtual address */
	BM_GetPhysPageAddr(psMemInfo, sDevVPageAddr, &sDevPAddr);

	/* convert DevP page address to byte address */
	sDevPAddr.uiAddr += ui32PageOffset;

	eErr = PDumpOSBufprintf(hScript,
			 ui32MaxLen,
			 "POL :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X 0x%08X 0x%08X %d %d %d\r\n",
			 psMMUAttrib->sDevId.pszPDumpDevName,
			 (IMG_UINTPTR_T)hUniqueTag,
			 sDevPAddr.uiAddr & ~(psMMUAttrib->ui32DataPageMask),
			 (unsigned int)(sDevPAddr.uiAddr & (psMMUAttrib->ui32DataPageMask)),
			 ui32Value,
			 ui32Mask,
			 eOperator,
			 MEMPOLL_COUNT,
			 MEMPOLL_DELAY);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : _PDumpMemIntKM
 * Inputs         : psMemInfo
 *				  : ui32Offset
 *				  : ui32Bytes
 *				  : ui32Flags
 *				  : hUniqueTag
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Implements Client pdump mem API
**************************************************************************/
static PVRSRV_ERROR _PDumpMemIntKM(IMG_PVOID pvAltLinAddr,
								   PVRSRV_KERNEL_MEM_INFO *psMemInfo,
								   IMG_UINT32 ui32Offset,
								   IMG_UINT32 ui32Bytes,
								   IMG_UINT32 ui32Flags,
								   IMG_HANDLE hUniqueTag)
{
	PVRSRV_ERROR eErr;
	IMG_UINT32 ui32NumPages;
	IMG_UINT32 ui32PageByteOffset;
	IMG_UINT32 ui32BlockBytes;
	IMG_UINT8* pui8LinAddr;
	IMG_UINT8* pui8DataLinAddr = IMG_NULL;
	IMG_DEV_VIRTADDR sDevVPageAddr;
	IMG_DEV_VIRTADDR sDevVAddr;
	IMG_DEV_PHYADDR sDevPAddr;
	IMG_UINT32 ui32ParamOutPos;
	PDUMP_MMU_ATTRIB *psMMUAttrib;
	IMG_UINT32 ui32DataPageSize;
	PDUMP_GET_SCRIPT_AND_FILE_STRING();

	PDUMP_LOCK();
	/* PRQA S 3415 1 */ /* side effects desired */
	if (ui32Bytes == 0 || PDumpOSIsSuspended())
	{
		PDUMP_UNLOCK();
		return PVRSRV_OK;
	}

	psMMUAttrib = ((BM_BUF*)psMemInfo->sMemBlk.hBuffer)->pMapping->pBMHeap->psMMUAttrib;
	
	/*
		check the offset and size don't exceed the bounds of the allocation
	*/
	PVR_ASSERT((ui32Offset + ui32Bytes) <= psMemInfo->uAllocSize);

	if (!PDumpOSJTInitialised())
	{
		PDUMP_UNLOCK();
		return PVRSRV_ERROR_PDUMP_NOT_AVAILABLE;
	}

	/* setup memory addresses */
	if(pvAltLinAddr)
	{
		pui8DataLinAddr = pvAltLinAddr;
	}
	else if(psMemInfo->pvLinAddrKM)
	{
		pui8DataLinAddr = (IMG_UINT8 *)psMemInfo->pvLinAddrKM + ui32Offset;
	}
	pui8LinAddr = (IMG_UINT8 *)psMemInfo->pvLinAddrKM;
	sDevVAddr = psMemInfo->sDevVAddr;

	/* advance address by offset */
	sDevVAddr.uiAddr += ui32Offset;
	pui8LinAddr += ui32Offset;

	PVR_ASSERT(pui8DataLinAddr);

	PDumpOSCheckForSplitting(PDumpOSGetStream(PDUMP_STREAM_PARAM2), ui32Bytes, ui32Flags);

	ui32ParamOutPos = PDumpOSGetStreamOffset(PDUMP_STREAM_PARAM2);

	/*
		write the binary data up-front.
	*/
	if(!PDumpOSWriteString(PDumpOSGetStream(PDUMP_STREAM_PARAM2),
						pui8DataLinAddr,
						ui32Bytes,
						ui32Flags))
	{
		PDUMP_UNLOCK();
		return PVRSRV_ERROR_PDUMP_BUFFER_FULL;
	}

	if (PDumpOSGetParamFileNum() == 0)
	{
		eErr = PDumpOSSprintf(pszFileName, ui32MaxLenFileName, "%%0%%.prm");
	}
	else
	{
		eErr = PDumpOSSprintf(pszFileName, ui32MaxLenFileName, "%%0%%_%u.prm", PDumpOSGetParamFileNum());
	}
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}

	/*
		Write a comment to the PDump2 script streams indicating the virtual memory load
	*/
	eErr = PDumpOSBufprintf(hScript,
			 ui32MaxLenScript,
			 "-- LDB :%s:VA_" UINTPTR_FMT "%08X:0x%08X 0x%08X 0x%08X %s\r\n",
			 psMMUAttrib->sDevId.pszPDumpDevName,
			 (IMG_UINTPTR_T)hUniqueTag,
			 psMemInfo->sDevVAddr.uiAddr,
			 ui32Offset,
			 ui32Bytes,
			 ui32ParamOutPos,
			 pszFileName);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	/*
		query the buffer manager for the physical pages that back the
		virtual address
	*/
	PDumpOSCPUVAddrToPhysPages(psMemInfo->sMemBlk.hOSMemHandle,
			ui32Offset,
			pui8LinAddr,
			psMMUAttrib->ui32DataPageMask,
			&ui32PageByteOffset);
	ui32DataPageSize = psMMUAttrib->ui32DataPageMask + 1;
	ui32NumPages = (ui32PageByteOffset + ui32Bytes + psMMUAttrib->ui32DataPageMask) / ui32DataPageSize;

	while(ui32NumPages)
	{
		ui32NumPages--;
	
		/* calculate the DevV page address */
		sDevVPageAddr.uiAddr = sDevVAddr.uiAddr - ui32PageByteOffset;

		if (ui32DataPageSize <= PDUMP_TEMP_BUFFER_SIZE)
		{
			/* if a page fits within temp buffer, we should dump in page-aligned chunks. */
			PVR_ASSERT((sDevVPageAddr.uiAddr & psMMUAttrib->ui32DataPageMask) == 0);
		}

		/* get the physical page address based on the device virtual address */
		BM_GetPhysPageAddr(psMemInfo, sDevVPageAddr, &sDevPAddr);

		/* convert DevP page address to byte address */
		sDevPAddr.uiAddr += ui32PageByteOffset;

		/* how many bytes to dump from this page */
		if (ui32PageByteOffset + ui32Bytes > ui32DataPageSize)
		{
			/* dump up to the page boundary */
			ui32BlockBytes = ui32DataPageSize - ui32PageByteOffset;
		}
		else
		{
			/* dump what's left */
			ui32BlockBytes = ui32Bytes;
		}

		eErr = PDumpOSBufprintf(hScript,
					 ui32MaxLenScript,
					 "LDB :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X 0x%08X 0x%08X %s\r\n",
					 psMMUAttrib->sDevId.pszPDumpDevName,
					 (IMG_UINTPTR_T)hUniqueTag,
					 sDevPAddr.uiAddr & ~(psMMUAttrib->ui32DataPageMask),
					 (unsigned int)(sDevPAddr.uiAddr & (psMMUAttrib->ui32DataPageMask)),
					 ui32BlockBytes,
					 ui32ParamOutPos,
					 pszFileName);
		if(eErr != PVRSRV_OK)
		{
			PDUMP_UNLOCK();
			return eErr;
		}
		PDumpOSWriteString2(hScript, ui32Flags);

		/* update details for next page */

#if defined(SGX_FEATURE_VARIABLE_MMU_PAGE_SIZE)
		/* page may be larger than pdump temporary buffer */
		ui32PageByteOffset = (ui32PageByteOffset + ui32BlockBytes) % ui32DataPageSize;
#else
		/* page offset 0 after first page dump */
		ui32PageByteOffset = 0;
#endif
		/* bytes left over */
		ui32Bytes -= ui32BlockBytes;	/* PRQA S 3382 */ /* QAC missed MIN test */
		/* advance devVaddr */
		sDevVAddr.uiAddr += ui32BlockBytes;
		/* advance the cpuVaddr */
		pui8LinAddr += ui32BlockBytes;
		/* update the file write offset */
		ui32ParamOutPos += ui32BlockBytes;
	}

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : PDumpMemKM
 * Inputs         : psMemInfo
 *				  : ui32Offset
 *				  : ui32Bytes
 *				  : ui32Flags
 *				  : hUniqueTag
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Implements Client pdump mem API
**************************************************************************/
PVRSRV_ERROR PDumpMemKM(IMG_PVOID pvAltLinAddr,
						PVRSRV_KERNEL_MEM_INFO *psMemInfo,
						IMG_UINT32 ui32Offset,
						IMG_UINT32 ui32Bytes,
						IMG_UINT32 ui32Flags,
						IMG_HANDLE hUniqueTag)
{
	/*
		For now we don't support dumping sparse allocations that
		are from within the kernel, or are from UM but without a
		alternative linear address
	*/
	PVR_ASSERT((psMemInfo->ui32Flags & PVRSRV_MEM_SPARSE) == 0);

	if (psMemInfo->ui32Flags & PVRSRV_MEM_SPARSE)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	else
	{
		return _PDumpMemIntKM(pvAltLinAddr,
							  psMemInfo,
							  ui32Offset,
							  ui32Bytes,
							  ui32Flags,
							  hUniqueTag);
	}
}

PVRSRV_ERROR PDumpMemPDEntriesKM(PDUMP_MMU_ATTRIB *psMMUAttrib,
								 IMG_HANDLE hOSMemHandle,
								 IMG_CPU_VIRTADDR pvLinAddr,
								 IMG_UINT32 ui32Bytes,
								 IMG_UINT32 ui32Flags,
								 IMG_BOOL bInitialisePages,
								 IMG_HANDLE hUniqueTag1,
								 IMG_HANDLE hUniqueTag2)
{
	PDUMP_MMU_ATTRIB sMMUAttrib;
	
	/* Override the (variable) PT size since PDs are always 4K in size */
	sMMUAttrib = *psMMUAttrib;
	sMMUAttrib.ui32PTSize = (IMG_UINT32)HOST_PAGESIZE();
	return PDumpMemPTEntriesKM(	&sMMUAttrib,
								hOSMemHandle,
								pvLinAddr,
								ui32Bytes,
								ui32Flags,
								bInitialisePages,
								hUniqueTag1,
								hUniqueTag2);
}

/**************************************************************************
 * Function Name  : PDumpMemPTEntriesKM
 * Inputs         : psMMUAttrib - MMU attributes for pdump
 *				  : pvLinAddr - CPU address of PT base
 *				  : ui32Bytes - size
 *				  : ui32Flags - pdump flags
 *				  : bInitialisePages - whether to initialise pages from file
 *				  : hUniqueTag1 - ID for PT physical page
 *				  : hUniqueTag2 - ID for target physical page (if !bInitialisePages)
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Kernel Services internal pdump memory API
 *					Used for memory without DevVAddress mappings
 					e.g. MMU page tables
 					FIXME: This function doesn't support non-4k data pages,
 					e.g. dummy data page
**************************************************************************/
PVRSRV_ERROR PDumpMemPTEntriesKM(PDUMP_MMU_ATTRIB *psMMUAttrib,
								 IMG_HANDLE hOSMemHandle,
								 IMG_CPU_VIRTADDR pvLinAddr,
								 IMG_UINT32 ui32Bytes,
								 IMG_UINT32 ui32Flags,
								 IMG_BOOL bInitialisePages,
								 IMG_HANDLE hUniqueTag1,
								 IMG_HANDLE hUniqueTag2)
{
	PVRSRV_ERROR eErr;
	IMG_UINT32 ui32NumPages;
	IMG_UINT32 ui32PageOffset;
	IMG_UINT32 ui32BlockBytes;
	IMG_UINT8* pui8LinAddr;
	IMG_DEV_PHYADDR sDevPAddr;
	IMG_DEV_PHYADDR sDevPAddrTmp;
	IMG_CPU_PHYADDR sCpuPAddr;
	IMG_UINT32 ui32Offset;
	IMG_UINT32 ui32ParamOutPos;
	IMG_UINT32 ui32PageMask; /* mask for the physical page backing the PT */

#if !defined(SGX_FEATURE_36BIT_MMU)
	IMG_DEV_PHYADDR sDevPAddrTmp2;
#endif
	PDUMP_GET_SCRIPT_AND_FILE_STRING();

	PDUMP_LOCK();


	if (PDumpOSIsSuspended())
	{
		PDUMP_UNLOCK();
		return PVRSRV_OK;
	}

	if (!PDumpOSJTInitialised())
	{
		PDUMP_UNLOCK();
		return PVRSRV_ERROR_PDUMP_NOT_AVAILABLE;
	}

	if (!pvLinAddr)
	{
		PDUMP_UNLOCK();
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PDumpOSCheckForSplitting(PDumpOSGetStream(PDUMP_STREAM_PARAM2), ui32Bytes, ui32Flags);

	ui32ParamOutPos = PDumpOSGetStreamOffset(PDUMP_STREAM_PARAM2);

	if (bInitialisePages)
	{
		/*
			write the binary data up-front
			Use the 'continuous' memory stream
		*/
		if (!PDumpOSWriteString(PDumpOSGetStream(PDUMP_STREAM_PARAM2),
							pvLinAddr,
							ui32Bytes,
							ui32Flags | PDUMP_FLAGS_CONTINUOUS))
		{
			PDUMP_UNLOCK();
			return PVRSRV_ERROR_PDUMP_BUFFER_FULL;
		}

		if (PDumpOSGetParamFileNum() == 0)
		{
			eErr = PDumpOSSprintf(pszFileName, ui32MaxLenFileName, "%%0%%.prm");
		}
		else
		{
			eErr = PDumpOSSprintf(pszFileName, ui32MaxLenFileName, "%%0%%_%u.prm", PDumpOSGetParamFileNum());
		}
		if(eErr != PVRSRV_OK)
		{
			PDUMP_UNLOCK();
			return eErr;
		}
	}

	/*
		Mask for the physical page address backing the PT
		The PT size can be less than 4k with variable page size support
		The PD size is always 4k
		FIXME: This won't work for dumping the dummy data page
	*/
	ui32PageMask = psMMUAttrib->ui32PTSize - 1;

	/*
		Write to the MMU script stream indicating the physical page table entries
	*/
	/* physical pages that back the virtual address	*/
 	ui32PageOffset	= (IMG_UINT32)((IMG_UINTPTR_T)pvLinAddr & (psMMUAttrib->ui32PTSize - 1));
 	ui32NumPages	= (ui32PageOffset + ui32Bytes + psMMUAttrib->ui32PTSize - 1) / psMMUAttrib->ui32PTSize;
	pui8LinAddr		= (IMG_UINT8*) pvLinAddr;

	while (ui32NumPages)
	{
		ui32NumPages--;
		/* FIXME: if we used OSMemHandleToCPUPAddr() here, we might be
		   able to lose the lin addr arg.  At least one thing that
		   would need to be done here is to pass in an offset, as the
		   calling function doesn't necessarily give us the lin addr
		   of the start of the mem area.  Probably best to keep the
		   lin addr arg for now - but would be nice to remove the
		   redundancy */
		sCpuPAddr = OSMapLinToCPUPhys(hOSMemHandle, pui8LinAddr);
		sDevPAddr = SysCpuPAddrToDevPAddr(psMMUAttrib->sDevId.eDeviceType, sCpuPAddr);

		/* how many bytes to dump from this page */
		if (ui32PageOffset + ui32Bytes > psMMUAttrib->ui32PTSize)
		{
			/* dump up to the page boundary */
			ui32BlockBytes = psMMUAttrib->ui32PTSize - ui32PageOffset;
		}
		else
		{
			/* dump what's left */
			ui32BlockBytes = ui32Bytes;
		}

		/*
			Write a comment to the MMU script stream indicating the page table load
		*/
		
		if (bInitialisePages)
		{
			eErr = PDumpOSBufprintf(hScript,
					 ui32MaxLenScript,
					 "LDB :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X 0x%08X 0x%08X %s\r\n",
					 psMMUAttrib->sDevId.pszPDumpDevName,
					 (IMG_UINTPTR_T)hUniqueTag1,
					 sDevPAddr.uiAddr & ~ui32PageMask,
					 (unsigned int)(sDevPAddr.uiAddr & ui32PageMask),
					 ui32BlockBytes,
					 ui32ParamOutPos,
					 pszFileName);
			if(eErr != PVRSRV_OK)
			{
				PDUMP_UNLOCK();
				return eErr;
			}
			PDumpOSWriteString2(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);
		}
		else
		{
			for (ui32Offset = 0; ui32Offset < ui32BlockBytes; ui32Offset += sizeof(IMG_UINT32))
			{
				IMG_UINT32 ui32PTE = *((IMG_UINT32 *)(IMG_UINTPTR_T)(pui8LinAddr + ui32Offset)); /* PRQA S 3305 */ /* strict pointer */

				if ((ui32PTE & psMMUAttrib->ui32PDEMask) != 0)
				{
					/* PT entry points to non-null page */
#if defined(SGX_FEATURE_36BIT_MMU)
					sDevPAddrTmp.uiAddr = ((ui32PTE & psMMUAttrib->ui32PDEMask) << psMMUAttrib->ui32PTEAlignShift);

					eErr = PDumpOSBufprintf(hScript,
							ui32MaxLenScript,
							 "WRW :%s:$1 :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x0\r\n",
							 psMMUAttrib->sDevId.pszPDumpDevName,
							 psMMUAttrib->sDevId.pszPDumpDevName,
							 (IMG_UINTPTR_T)hUniqueTag2,
							 sDevPAddrTmp.uiAddr);
					if(eErr != PVRSRV_OK)
					{
						PDUMP_UNLOCK();
						return eErr;
					}
					PDumpOSWriteString2(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);
					eErr = PDumpOSBufprintf(hScript, ui32MaxLenScript, "SHR :%s:$1 :%s:$1 0x4\r\n",
								psMMUAttrib->sDevId.pszPDumpDevName,
								psMMUAttrib->sDevId.pszPDumpDevName);
					if(eErr != PVRSRV_OK)
					{
						PDUMP_UNLOCK();
						return eErr;
					}
					PDumpOSWriteString2(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);
					eErr = PDumpOSBufprintf(hScript, ui32MaxLenScript, "OR :%s:$1 :%s:$1 0x%08X\r\n",
								psMMUAttrib->sDevId.pszPDumpDevName,
								psMMUAttrib->sDevId.pszPDumpDevName,
								ui32PTE & ~psMMUAttrib->ui32PDEMask);
					if(eErr != PVRSRV_OK)
					{
						PDUMP_UNLOCK();
						return eErr;
					}
					PDumpOSWriteString2(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);
					sDevPAddrTmp.uiAddr = (sDevPAddr.uiAddr + ui32Offset) & ~ui32PageMask;

					eErr = PDumpOSBufprintf(hScript,
							ui32MaxLenScript,
							 "WRW :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X :%s:$1\r\n",
							 psMMUAttrib->sDevId.pszPDumpDevName,
							 (IMG_UINTPTR_T)hUniqueTag1,
							 sDevPAddrTmp.uiAddr,
							 (unsigned int)((sDevPAddr.uiAddr + ui32Offset) & ui32PageMask),
							 psMMUAttrib->sDevId.pszPDumpDevName);
					if(eErr != PVRSRV_OK)
					{
						PDUMP_UNLOCK();
						return eErr;
					}
					PDumpOSWriteString2(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);
#else
					sDevPAddrTmp.uiAddr  = (sDevPAddr.uiAddr + ui32Offset) & ~ui32PageMask;
					sDevPAddrTmp2.uiAddr = (ui32PTE & psMMUAttrib->ui32PDEMask) << psMMUAttrib->ui32PTEAlignShift;

					eErr = PDumpOSBufprintf(hScript,
							ui32MaxLenScript,
							 "WRW :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X\r\n",
							 psMMUAttrib->sDevId.pszPDumpDevName,
							 (IMG_UINTPTR_T)hUniqueTag1,
							 sDevPAddrTmp.uiAddr,
							 (unsigned int)((sDevPAddr.uiAddr + ui32Offset) & ui32PageMask),
							 psMMUAttrib->sDevId.pszPDumpDevName,
							 (IMG_UINTPTR_T)hUniqueTag2,
							 sDevPAddrTmp2.uiAddr,
							 (unsigned int)(ui32PTE & ~psMMUAttrib->ui32PDEMask));
					if(eErr != PVRSRV_OK)
					{
						PDUMP_UNLOCK();
						return eErr;
					}
					PDumpOSWriteString2(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);
#endif
				}
				else
				{
#if !defined(FIX_HW_BRN_31620)
					PVR_ASSERT((ui32PTE & psMMUAttrib->ui32PTEValid) == 0UL);
#endif
					sDevPAddrTmp.uiAddr = (sDevPAddr.uiAddr + ui32Offset) & ~ui32PageMask;

					eErr = PDumpOSBufprintf(hScript,
							ui32MaxLenScript,
							 "WRW :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X 0x%08X" UINTPTR_FMT "\r\n",
							 psMMUAttrib->sDevId.pszPDumpDevName,
							 (IMG_UINTPTR_T)hUniqueTag1,
							 sDevPAddrTmp.uiAddr,
							 (unsigned int)((sDevPAddr.uiAddr + ui32Offset) & ui32PageMask),
							 ui32PTE << psMMUAttrib->ui32PTEAlignShift,
							 (IMG_UINTPTR_T)hUniqueTag2);
					if(eErr != PVRSRV_OK)
					{
						PDUMP_UNLOCK();
						return eErr;
					}
					PDumpOSWriteString2(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);
				}
			}
		}

		/* update details for next page */

		/* page offset 0 after first page dump */
		ui32PageOffset = 0;
		/* bytes left over */
		ui32Bytes -= ui32BlockBytes;
		/* advance the cpuVaddr */
		pui8LinAddr += ui32BlockBytes;
		/* update the file write offset */
		ui32ParamOutPos += ui32BlockBytes;
	}

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpPDDevPAddrKM(PVRSRV_KERNEL_MEM_INFO *psMemInfo,
							   IMG_UINT32 ui32Offset,
							   IMG_DEV_PHYADDR sPDDevPAddr,
							   IMG_HANDLE hUniqueTag1,
							   IMG_HANDLE hUniqueTag2)
{
	PVRSRV_ERROR eErr;
	IMG_UINT32 ui32PageByteOffset;
	IMG_DEV_VIRTADDR sDevVAddr;
	IMG_DEV_VIRTADDR sDevVPageAddr;
	IMG_DEV_PHYADDR sDevPAddr;
	IMG_UINT32 ui32Flags = PDUMP_FLAGS_CONTINUOUS;
	IMG_UINT32 ui32ParamOutPos;
	PDUMP_MMU_ATTRIB *psMMUAttrib;
	IMG_UINT32 ui32PageMask; /* mask for the physical page backing the PT */
	IMG_DEV_PHYADDR sDevPAddrTmp;

	PDUMP_GET_SCRIPT_AND_FILE_STRING();

	PDUMP_LOCK();
	if (!PDumpOSJTInitialised())
	{
		PDUMP_UNLOCK();
		return PVRSRV_ERROR_PDUMP_NOT_AVAILABLE;
	}

	psMMUAttrib = ((BM_BUF*)psMemInfo->sMemBlk.hBuffer)->pMapping->pBMHeap->psMMUAttrib;
	ui32PageMask = psMMUAttrib->ui32PTSize - 1;

	ui32ParamOutPos = PDumpOSGetStreamOffset(PDUMP_STREAM_PARAM2);

	/* Write the PD phys addr to the param stream up front */
	if(!PDumpOSWriteString(PDumpOSGetStream(PDUMP_STREAM_PARAM2),
						(IMG_UINT8 *)&sPDDevPAddr,
						sizeof(IMG_DEV_PHYADDR),
						ui32Flags))
	{
		PDUMP_UNLOCK();
		return PVRSRV_ERROR_PDUMP_BUFFER_FULL;
	}

	if (PDumpOSGetParamFileNum() == 0)
	{
		eErr = PDumpOSSprintf(pszFileName, ui32MaxLenFileName, "%%0%%.prm");
	}
	else
	{
		eErr = PDumpOSSprintf(pszFileName, ui32MaxLenFileName, "%%0%%_%u.prm", PDumpOSGetParamFileNum());
	}
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}

	/* Write a comment indicating the PD phys addr write, so that the offsets
	 * into the param stream increase in correspondence with the number of bytes
	 * written. */
	sDevPAddrTmp.uiAddr = sPDDevPAddr.uiAddr & ~ui32PageMask;

	eErr = PDumpOSBufprintf(hScript,
			ui32MaxLenScript,
			"-- LDB :%s:PA_0x" UINTPTR_FMT DEVPADDR_FMT ":0x%08X 0x%08lX 0x%08X %s\r\n",
			psMMUAttrib->sDevId.pszPDumpDevName,
			(IMG_UINTPTR_T)hUniqueTag1,
			sDevPAddrTmp.uiAddr,
			(unsigned int)(sPDDevPAddr.uiAddr & ui32PageMask),
			sizeof(IMG_DEV_PHYADDR),
			ui32ParamOutPos,
			pszFileName);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	sDevVAddr = psMemInfo->sDevVAddr;
	ui32PageByteOffset = sDevVAddr.uiAddr & ui32PageMask;

	sDevVPageAddr.uiAddr = sDevVAddr.uiAddr - ui32PageByteOffset;
	PVR_ASSERT((sDevVPageAddr.uiAddr & 0xFFF) == 0);

	BM_GetPhysPageAddr(psMemInfo, sDevVPageAddr, &sDevPAddr);
	sDevPAddr.uiAddr += ui32PageByteOffset + ui32Offset;

#if defined(SGX_FEATURE_36BIT_MMU)
	sDevPAddrTmp.uiAddr = sPDDevPAddr.uiAddr & ~ui32PageMask;

	eErr = PDumpOSBufprintf(hScript,
				ui32MaxLenScript,
				 "WRW :%s:$1 :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X\r\n",
				 psMMUAttrib->sDevId.pszPDumpDevName,
				 psMMUAttrib->sDevId.pszPDumpDevName,
				 (IMG_UINTPTR_T)hUniqueTag2,
				 sDevPAddrTmp.uiAddr,
				 (unsigned int)(sPDDevPAddr.uiAddr & ui32PageMask));
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	eErr = PDumpOSBufprintf(hScript, ui32MaxLenScript, "SHR :%s:$1 :%s:$1 0x4\r\n",
				psMMUAttrib->sDevId.pszPDumpDevName,
				psMMUAttrib->sDevId.pszPDumpDevName);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}

	PDumpOSWriteString2(hScript, ui32Flags);
	sDevPAddrTmp.uiAddr = sDevPAddr.uiAddr & ~(psMMUAttrib->ui32DataPageMask);

	eErr = PDumpOSBufprintf(hScript,
				ui32MaxLenScript,
				 "WRW :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X :%s:$1\r\n",
				 psMMUAttrib->sDevId.pszPDumpDevName,
				 (IMG_UINTPTR_T)hUniqueTag1,
				 sDevPAddrTmp.uiAddr,
				 (unsigned int)((sDevPAddr.uiAddr) & (psMMUAttrib->ui32DataPageMask)),
				 psMMUAttrib->sDevId.pszPDumpDevName);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
#else
	eErr = PDumpOSBufprintf(hScript,
				ui32MaxLenScript,
				"WRW :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X \r\n",
				psMMUAttrib->sDevId.pszPDumpDevName,
				(IMG_UINTPTR_T)hUniqueTag1,
				sDevPAddr.uiAddr & ~ui32PageMask,
				(unsigned int)(sDevPAddr.uiAddr & ui32PageMask),
				psMMUAttrib->sDevId.pszPDumpDevName,
				(IMG_UINTPTR_T)hUniqueTag2,
				sPDDevPAddr.uiAddr & psMMUAttrib->ui32PDEMask,
				(unsigned int)(sPDDevPAddr.uiAddr & ~psMMUAttrib->ui32PDEMask));
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
#endif
	PDumpOSWriteString2(hScript, ui32Flags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : PDumpCommentKM
 * Inputs         : pszComment, ui32Flags
 * Outputs        : None
 * Returns        : None
 * Description    : Dumps a comment
**************************************************************************/
PVRSRV_ERROR PDumpCommentKM(IMG_CHAR *pszComment, IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	IMG_CHAR pszCommentPrefix[] = "-- "; /* prefix for comments */
#if defined(PDUMP_DEBUG_OUTFILES)
	IMG_CHAR pszTemp[256];
#endif
	IMG_UINT32 ui32LenCommentPrefix;
	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();
	PDUMP_DBG(("PDumpCommentKM"));

	/* Put \r \n sequence at the end if it isn't already there */
	PDumpOSVerifyLineEnding(pszComment, ui32MaxLen);

	/* Length of string excluding terminating NULL character */
	ui32LenCommentPrefix = PDumpOSBuflen(pszCommentPrefix, sizeof(pszCommentPrefix));

	/* Ensure output file is available for writing */
	/* FIXME: is this necessary? */
	if (!PDumpOSWriteString(PDumpOSGetStream(PDUMP_STREAM_SCRIPT2),
			  (IMG_UINT8*)pszCommentPrefix,
			  ui32LenCommentPrefix,
			  ui32Flags))
	{
#if defined(PDUMP_DEBUG_OUTFILES)
		if(ui32Flags & PDUMP_FLAGS_CONTINUOUS)
		{
			PVR_DPF((PVR_DBG_WARNING, "Incomplete comment, %d: %s (continuous set)",
					 g_ui32EveryLineCounter, pszComment));
			PDUMP_UNLOCK();
			return PVRSRV_ERROR_PDUMP_BUFFER_FULL;
		}
		else if(ui32Flags & PDUMP_FLAGS_PERSISTENT)
		{
			PVR_DPF((PVR_DBG_WARNING, "Incomplete comment, %d: %s (persistent set)",
					 g_ui32EveryLineCounter, pszComment));
			PDUMP_UNLOCK();
			return PVRSRV_ERROR_CMD_NOT_PROCESSED;
		}
		else
		{
			PVR_DPF((PVR_DBG_WARNING, "Incomplete comment, %d: %s",
					 g_ui32EveryLineCounter, pszComment));
			PDUMP_UNLOCK();
			return PVRSRV_ERROR_CMD_NOT_PROCESSED;
		}
#else
		PVR_DPF((PVR_DBG_WARNING, "Incomplete comment, %s",
					 pszComment));
		PDUMP_UNLOCK();
		return PVRSRV_ERROR_CMD_NOT_PROCESSED;
#endif
	}

#if defined(PDUMP_DEBUG_OUTFILES)
	/* Prefix comment with PID and line number */
	eErr = PDumpOSSprintf(pszTemp, 256, "%d-%d %s",
		_PDumpGetPID(),
		g_ui32EveryLineCounter,
		pszComment);

	/* Append the comment to the script stream */
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "%s",
		pszTemp);
#else
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "%s",
		pszComment);
#endif
	if( (eErr != PVRSRV_OK) &&
		(eErr != PVRSRV_ERROR_PDUMP_BUF_OVERFLOW))
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : PDumpCommentWithFlags
 * Inputs         : psPDev - PDev for PDump device
 *				  : pszFormat - format string for comment
 *				  : ... - args for format string
 * Outputs        : None
 * Returns        : None
 * Description    : PDumps a comments
**************************************************************************/
PVRSRV_ERROR PDumpCommentWithFlags(IMG_UINT32 ui32Flags, IMG_CHAR * pszFormat, ...)
{
	PVRSRV_ERROR eErr;
	PDUMP_va_list ap;
	PDUMP_GET_MSG_STRING();

	PDUMP_LOCK_MSG();
	/* Construct the string */
	PDUMP_va_start(ap, pszFormat);
	eErr = PDumpOSVSprintf(pszMsg, ui32MaxLen, pszFormat, ap);
	PDUMP_va_end(ap);

	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK_MSG();
		return eErr;
	}
	eErr = PDumpCommentKM(pszMsg, ui32Flags);
	PDUMP_UNLOCK_MSG();
	return eErr;
}

/**************************************************************************
 * Function Name  : PDumpComment
 * Inputs         : psPDev - PDev for PDump device
 *				  : pszFormat - format string for comment
 *				  : ... - args for format string
 * Outputs        : None
 * Returns        : None
 * Description    : PDumps a comments
**************************************************************************/
PVRSRV_ERROR PDumpComment(IMG_CHAR *pszFormat, ...)
{
	PVRSRV_ERROR eErr;
	PDUMP_va_list ap;
	PDUMP_GET_MSG_STRING();

	PDUMP_LOCK_MSG();
	/* Construct the string */
	PDUMP_va_start(ap, pszFormat);
	eErr = PDumpOSVSprintf(pszMsg, ui32MaxLen, pszFormat, ap);
	PDUMP_va_end(ap);

	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK_MSG();
		return eErr;
	}
	eErr = PDumpCommentKM(pszMsg, PDUMP_FLAGS_CONTINUOUS);
	PDUMP_UNLOCK_MSG();
	return eErr;
}

/**************************************************************************
 * Function Name  : PDumpDriverInfoKM
 * Inputs         : pszString, ui32Flags
 * Outputs        : None
 * Returns        : None
 * Description    : Dumps a comment
**************************************************************************/
PVRSRV_ERROR PDumpDriverInfoKM(IMG_CHAR *pszString, IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	IMG_UINT32	ui32MsgLen;
	PDUMP_GET_MSG_STRING();

	PDUMP_LOCK_MSG();
	/* Construct the string */
	eErr = PDumpOSSprintf(pszMsg, ui32MaxLen, "%s", pszString);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK_MSG();
		return eErr;
	}

	/* Put \r \n sequence at the end if it isn't already there */
	PDumpOSVerifyLineEnding(pszMsg, ui32MaxLen);
	ui32MsgLen = PDumpOSBuflen(pszMsg, ui32MaxLen);

	if	(!PDumpOSWriteString(PDumpOSGetStream(PDUMP_STREAM_DRIVERINFO),
						  (IMG_UINT8*)pszMsg,
						  ui32MsgLen,
						  ui32Flags))
	{
		if	(ui32Flags & PDUMP_FLAGS_CONTINUOUS)
		{
			PDUMP_UNLOCK_MSG();
			return PVRSRV_ERROR_PDUMP_BUFFER_FULL;
		}
		else
		{
			PDUMP_UNLOCK_MSG();
			return PVRSRV_ERROR_CMD_NOT_PROCESSED;
		}
	}

	PDUMP_UNLOCK_MSG();
	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PDumpBitmapKM

 @Description

 Dumps a bitmap from device memory to a file

 @Input    psDevId
 @Input    pszFileName
 @Input    ui32FileOffset
 @Input    ui32Width
 @Input    ui32Height
 @Input    ui32StrideInBytes
 @Input    sDevBaseAddr
 @Input    ui32Size
 @Input    ePixelFormat
 @Input    eMemFormat
 @Input    ui32PDumpFlags

 @Return   PVRSRV_ERROR			:

******************************************************************************/
PVRSRV_ERROR PDumpBitmapKM(	PVRSRV_DEVICE_NODE *psDeviceNode,
							IMG_CHAR *pszFileName,
							IMG_UINT32 ui32FileOffset,
							IMG_UINT32 ui32Width,
							IMG_UINT32 ui32Height,
							IMG_UINT32 ui32StrideInBytes,
							IMG_DEV_VIRTADDR sDevBaseAddr,
							IMG_HANDLE hDevMemContext,
							IMG_UINT32 ui32Size,
							PDUMP_PIXEL_FORMAT ePixelFormat,
							PDUMP_MEM_FORMAT eMemFormat,
							IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_DEVICE_IDENTIFIER *psDevId = &psDeviceNode->sDevId;
	IMG_UINT32 ui32MMUContextID;
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	PDumpCommentWithFlags(ui32PDumpFlags, "\r\n-- Dump bitmap of render\r\n");

	PDUMP_LOCK();
	/* find MMU context ID */
	ui32MMUContextID = psDeviceNode->pfnMMUGetContextID( hDevMemContext );

	eErr = PDumpOSBufprintf(hScript,
				ui32MaxLen,
				"SII %s %s.bin :%s:v%x:0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\r\n",
				pszFileName,
				pszFileName,
				psDevId->pszPDumpDevName,
				ui32MMUContextID,
				sDevBaseAddr.uiAddr,
				ui32Size,
				ui32FileOffset,
				ePixelFormat,
				ui32Width,
				ui32Height,
				ui32StrideInBytes,
				eMemFormat);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}

	PDumpOSWriteString2( hScript, ui32PDumpFlags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PDumpReadRegKM

 @Description

 Dumps a read from a device register to a file

 @Input    psConnection 		: connection info
 @Input    pszFileName
 @Input    ui32FileOffset
 @Input    ui32Address
 @Input    ui32Size
 @Input    ui32PDumpFlags

 @Return   PVRSRV_ERROR			:

******************************************************************************/
PVRSRV_ERROR PDumpReadRegKM		(	IMG_CHAR *pszPDumpRegName,
									IMG_CHAR *pszFileName,
									IMG_UINT32 ui32FileOffset,
									IMG_UINT32 ui32Address,
									IMG_UINT32 ui32Size,
									IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();
	PVR_UNREFERENCED_PARAMETER(ui32Size);

	PDUMP_LOCK();
	eErr = PDumpOSBufprintf(hScript,
			ui32MaxLen,
			"SAB :%s:0x%08X 0x%08X %s\r\n",
			pszPDumpRegName,
			ui32Address,
			ui32FileOffset,
			pszFileName);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}

	PDumpOSWriteString2( hScript, ui32PDumpFlags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/*****************************************************************************
 @name		PDumpTestNextFrame
 @brief		Tests whether the next frame will be pdumped
 @param		ui32CurrentFrame
 @return	bFrameDumped
*****************************************************************************/
IMG_BOOL PDumpTestNextFrame(IMG_UINT32 ui32CurrentFrame)
{
	IMG_BOOL	bFrameDumped;

	/*
		Try dumping a string
	*/
	(IMG_VOID) PDumpSetFrameKM(ui32CurrentFrame + 1);
	bFrameDumped = PDumpIsCaptureFrameKM();
	(IMG_VOID) PDumpSetFrameKM(ui32CurrentFrame);

	return bFrameDumped;
}

/*****************************************************************************
 @name		PDumpSignatureRegister
 @brief		Dumps a single signature register
 @param 	psDevId - device ID
 @param 	ui32Address	- The register address
 @param		ui32Size - The amount of data to be dumped in bytes
 @param		pui32FileOffset - Offset of dump in output file
 @param		ui32Flags - Flags
 @return	none
*****************************************************************************/
static PVRSRV_ERROR PDumpSignatureRegister	(PVRSRV_DEVICE_IDENTIFIER *psDevId,
									 IMG_CHAR	*pszFileName,
									 IMG_UINT32		ui32Address,
									 IMG_UINT32		ui32Size,
									 IMG_UINT32		*pui32FileOffset,
									 IMG_UINT32		ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	eErr = PDumpOSBufprintf(hScript,
			ui32MaxLen,
			"SAB :%s:0x%08X 0x%08X %s\r\n",
			psDevId->pszPDumpRegName,
			ui32Address,
			*pui32FileOffset,
			pszFileName);
	if(eErr != PVRSRV_OK)
	{
		return eErr;
	}

	PDumpOSWriteString2(hScript, ui32Flags);
	*pui32FileOffset += ui32Size;

	return PVRSRV_OK;
}

/*****************************************************************************
 @name		PDumpRegisterRange
 @brief		Dumps a list of signature registers to a file
 @param		psDevId - device ID
 @param		pszFileName - target filename for dump
 @param		pui32Registers - register list
 @param		ui32NumRegisters - number of regs to dump
 @param		pui32FileOffset - file offset
 @param		ui32Size - size of write in bytes
 @param		ui32Flags - pdump flags
 @return	none
 *****************************************************************************/
static IMG_VOID PDumpRegisterRange(PVRSRV_DEVICE_IDENTIFIER *psDevId,
									IMG_CHAR *pszFileName,
									IMG_UINT32 *pui32Registers,
									IMG_UINT32  ui32NumRegisters,
									IMG_UINT32 *pui32FileOffset,
									IMG_UINT32	ui32Size,
									IMG_UINT32	ui32Flags)
{
	IMG_UINT32 i;
	for (i = 0; i < ui32NumRegisters; i++)
	{
		PDumpSignatureRegister(psDevId, pszFileName, pui32Registers[i], ui32Size, pui32FileOffset, ui32Flags);
	}
}

/*****************************************************************************
 @name		PDump3DSignatureRegisters
 @brief		Dumps the signature registers for 3D modules...
 @param		psDevId - device ID info
 @param		pui32Registers - register list
 @param		ui32NumRegisters - number of regs to dump
 @return	Error
*****************************************************************************/
PVRSRV_ERROR PDump3DSignatureRegisters(PVRSRV_DEVICE_IDENTIFIER *psDevId,
										IMG_UINT32 ui32DumpFrameNum,
										IMG_BOOL bLastFrame,
										IMG_UINT32 *pui32Registers,
										IMG_UINT32 ui32NumRegisters)
{
	PVRSRV_ERROR eErr;
	IMG_UINT32	ui32FileOffset, ui32Flags;
	PDUMP_GET_FILE_STRING();

	ui32Flags = bLastFrame ? PDUMP_FLAGS_LASTFRAME : 0;
	ui32FileOffset = 0;

	PDumpCommentWithFlags(ui32Flags, "\r\n-- Dump 3D signature registers\r\n");
	eErr = PDumpOSSprintf(pszFileName, ui32MaxLen, "out%u_3d.sig", ui32DumpFrameNum);
	if(eErr != PVRSRV_OK)
	{
		return eErr;
	}

	/*
		Note:
		PDumpCommentWithFlags will take the lock so we defer the lock
		taking until here
	*/
	PDUMP_LOCK();
	PDumpRegisterRange(psDevId,
						pszFileName,
						pui32Registers,
						ui32NumRegisters,
						&ui32FileOffset,
						sizeof(IMG_UINT32),
						ui32Flags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/*****************************************************************************
 @name		PDumpTASignatureRegisters
 @brief		Dumps the TA signature registers
 @param		psDevId - device id info
 @param		ui32DumpFrameNum - frame number
 @param		ui32TAKickCount - TA kick counter
 @param		bLastFrame
 @param		pui32Registers - register list
 @param		ui32NumRegisters - number of regs to dump
 @return	Error
*****************************************************************************/
PVRSRV_ERROR PDumpTASignatureRegisters	(PVRSRV_DEVICE_IDENTIFIER *psDevId,
			 IMG_UINT32 ui32DumpFrameNum,
			 IMG_UINT32	ui32TAKickCount,
			 IMG_BOOL	bLastFrame,
			 IMG_UINT32 *pui32Registers,
			 IMG_UINT32 ui32NumRegisters)
{
	PVRSRV_ERROR eErr;
	IMG_UINT32	ui32FileOffset, ui32Flags;
	PDUMP_GET_FILE_STRING();

	ui32Flags = bLastFrame ? PDUMP_FLAGS_LASTFRAME : 0;
	ui32FileOffset = ui32TAKickCount * ui32NumRegisters * sizeof(IMG_UINT32);

	PDumpCommentWithFlags(ui32Flags, "\r\n-- Dump TA signature registers\r\n");
	eErr = PDumpOSSprintf(pszFileName, ui32MaxLen, "out%u_ta.sig", ui32DumpFrameNum);
	if(eErr != PVRSRV_OK)
	{
		return eErr;
	}

	/*
		Note:
		PDumpCommentWithFlags will take the lock so we defer the lock
		taking until here
	*/
	PDUMP_LOCK();
	PDumpRegisterRange(psDevId,
						pszFileName, 
						pui32Registers, 
						ui32NumRegisters, 
						&ui32FileOffset, 
						sizeof(IMG_UINT32), 
						ui32Flags);
	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/*****************************************************************************
 @name		PDumpCounterRegisters
 @brief		Dumps the performance counters
 @param		psDevId - device id info
 @param		ui32DumpFrameNum - frame number
 @param		bLastFrame
 @param		pui32Registers - register list
 @param		ui32NumRegisters - number of regs to dump
 @return	Error
*****************************************************************************/
PVRSRV_ERROR PDumpCounterRegisters (PVRSRV_DEVICE_IDENTIFIER *psDevId,
								IMG_UINT32 ui32DumpFrameNum,
								IMG_BOOL	bLastFrame,
								IMG_UINT32 *pui32Registers,
								IMG_UINT32 ui32NumRegisters)
{
	PVRSRV_ERROR eErr;
	IMG_UINT32	ui32FileOffset, ui32Flags;
	PDUMP_GET_FILE_STRING();

	ui32Flags = bLastFrame ? PDUMP_FLAGS_LASTFRAME : 0UL;
	ui32FileOffset = 0UL;

	PDumpCommentWithFlags(ui32Flags, "\r\n-- Dump counter registers\r\n");
	eErr = PDumpOSSprintf(pszFileName, ui32MaxLen, "out%u.perf", ui32DumpFrameNum);
	if(eErr != PVRSRV_OK)
	{
		return eErr;
	}
	/*
		Note:
		PDumpCommentWithFlags will take the lock so we defer the lock
		taking until here
	*/
	PDUMP_LOCK();
	PDumpRegisterRange(psDevId,
						pszFileName,
						pui32Registers,
						ui32NumRegisters,
						&ui32FileOffset,
						sizeof(IMG_UINT32),
						ui32Flags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/*****************************************************************************
 @name		PDumpRegRead
 @brief		Dump signature register read to script
 @param		pszPDumpDevName - pdump device name
 @param		ui32RegOffset - register offset
 @param		ui32Flags - pdump flags
 @return	Error
*****************************************************************************/
PVRSRV_ERROR PDumpRegRead(IMG_CHAR *pszPDumpRegName,
							const IMG_UINT32 ui32RegOffset,
							IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "RDW :%s:0x%X\r\n",
							pszPDumpRegName, 
							ui32RegOffset);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/*****************************************************************************
 @name		PDumpSaveMemKM
 @brief		Save device memory to a file
 @param		psDevId
 @param		pszFileName
 @param		ui32FileOffset
 @param		sDevBaseAddr
 @param		ui32Size
 @param		ui32PDumpFlags
 @return	Error
*****************************************************************************/
PVRSRV_ERROR PDumpSaveMemKM (PVRSRV_DEVICE_IDENTIFIER *psDevId,
							 IMG_CHAR			*pszFileName,
							 IMG_UINT32			ui32FileOffset,
							 IMG_DEV_VIRTADDR	sDevBaseAddr,
							 IMG_UINT32 		ui32Size,
							 IMG_UINT32			ui32MMUContextID,
							 IMG_UINT32 		ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();
	eErr = PDumpOSBufprintf(hScript,
							ui32MaxLen,
							"SAB :%s:v%x:0x%08X 0x%08X 0x%08X %s.bin\r\n",
							psDevId->pszPDumpDevName,
							ui32MMUContextID,
							sDevBaseAddr.uiAddr,
							ui32Size,
							ui32FileOffset,
							pszFileName);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}

	PDumpOSWriteString2(hScript, ui32PDumpFlags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/*****************************************************************************
 @name		PDumpCycleCountRegRead
 @brief		Dump counter register read to script
 @param		ui32RegOffset - register offset
 @param		bLastFrame
 @return	Error
*****************************************************************************/
PVRSRV_ERROR PDumpCycleCountRegRead(PVRSRV_DEVICE_IDENTIFIER *psDevId,
									const IMG_UINT32 ui32RegOffset,
									IMG_BOOL bLastFrame)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "RDW :%s:0x%X\r\n", 
							psDevId->pszPDumpRegName,
							ui32RegOffset);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, bLastFrame ? PDUMP_FLAGS_LASTFRAME : 0);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	PDumpSignatureBuffer

 @Description

 Dumps a signature registers buffer

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PDumpSignatureBuffer (PVRSRV_DEVICE_IDENTIFIER *psDevId,
								   IMG_CHAR			*pszFileName,
								   IMG_CHAR			*pszBufferType,
								   IMG_UINT32		ui32FileOffset,
								   IMG_DEV_VIRTADDR	sDevBaseAddr,
								   IMG_UINT32 		ui32Size,
								   IMG_UINT32		ui32MMUContextID,
								   IMG_UINT32 		ui32PDumpFlags)
{
	PDumpCommentWithFlags(ui32PDumpFlags, "\r\n-- Dump microkernel %s signature Buffer\r\n",
						  pszBufferType);
	PDumpCommentWithFlags(ui32PDumpFlags, "Buffer format (sizes in 32-bit words):\r\n");
	PDumpCommentWithFlags(ui32PDumpFlags, "\tNumber of signatures per sample (1)\r\n");
	PDumpCommentWithFlags(ui32PDumpFlags, "\tNumber of samples (1)\r\n");
	PDumpCommentWithFlags(ui32PDumpFlags, "\tSignature register offsets (1 * number of signatures)\r\n");
	PDumpCommentWithFlags(ui32PDumpFlags, "\tSignature sample values (number of samples * number of signatures)\r\n");
	PDumpCommentWithFlags(ui32PDumpFlags, "Note: If buffer is full, last sample is final state after test completed\r\n");
	return PDumpSaveMemKM(psDevId, pszFileName, ui32FileOffset, sDevBaseAddr, ui32Size,
						  ui32MMUContextID, ui32PDumpFlags);
}


/*!
******************************************************************************

 @Function	PDumpHWPerfCBKM

 @Description

 Dumps the HW Perf Circular Buffer

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PDumpHWPerfCBKM (PVRSRV_DEVICE_IDENTIFIER *psDevId,
							  IMG_CHAR			*pszFileName,
							  IMG_UINT32		ui32FileOffset,
							  IMG_DEV_VIRTADDR	sDevBaseAddr,
							  IMG_UINT32 		ui32Size,
							  IMG_UINT32		ui32MMUContextID,
							  IMG_UINT32 		ui32PDumpFlags)
{
	PDumpCommentWithFlags(ui32PDumpFlags, "\r\n-- Dump Hardware Performance Circular Buffer\r\n");
	return PDumpSaveMemKM(psDevId, pszFileName, ui32FileOffset, sDevBaseAddr, ui32Size,
						  ui32MMUContextID, ui32PDumpFlags);
}


/*****************************************************************************
 FUNCTION	: PDumpCBP

 PURPOSE	: Dump CBP command to script

 PARAMETERS	:

 RETURNS	: None
*****************************************************************************/
PVRSRV_ERROR PDumpCBP(PPVRSRV_KERNEL_MEM_INFO		psROffMemInfo,
			  IMG_UINT32					ui32ROffOffset,
			  IMG_UINT32					ui32WPosVal,
			  IMG_UINT32					ui32PacketSize,
			  IMG_UINT32					ui32BufferSize,
			  IMG_UINT32					ui32Flags,
			  IMG_HANDLE					hUniqueTag)
{
	PVRSRV_ERROR eErr;
	IMG_UINT32			ui32PageOffset;
	IMG_UINT8			*pui8LinAddr;
	IMG_DEV_VIRTADDR	sDevVAddr;
	IMG_DEV_PHYADDR		sDevPAddr;
	IMG_DEV_VIRTADDR 	sDevVPageAddr;
    //IMG_CPU_PHYADDR     CpuPAddr;
	PDUMP_MMU_ATTRIB *psMMUAttrib;
	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();
	psMMUAttrib = ((BM_BUF*)psROffMemInfo->sMemBlk.hBuffer)->pMapping->pBMHeap->psMMUAttrib;

	/* Check the offset and size don't exceed the bounds of the allocation */
	PVR_ASSERT((ui32ROffOffset + sizeof(IMG_UINT32)) <= psROffMemInfo->uAllocSize);

	pui8LinAddr = psROffMemInfo->pvLinAddrKM;
	sDevVAddr = psROffMemInfo->sDevVAddr;

	/* Advance addresses by offset */
	pui8LinAddr += ui32ROffOffset;
	sDevVAddr.uiAddr += ui32ROffOffset;

	/*
		query the buffer manager for the physical pages that back the
		virtual address
	*/
	PDumpOSCPUVAddrToPhysPages(psROffMemInfo->sMemBlk.hOSMemHandle,
			ui32ROffOffset,
			pui8LinAddr,
			psMMUAttrib->ui32DataPageMask,
			&ui32PageOffset);

	/* calculate the DevV page address */
	sDevVPageAddr.uiAddr = sDevVAddr.uiAddr - ui32PageOffset;

	PVR_ASSERT((sDevVPageAddr.uiAddr & 0xFFF) == 0);

	/* get the physical page address based on the device virtual address */
	BM_GetPhysPageAddr(psROffMemInfo, sDevVPageAddr, &sDevPAddr);

	/* convert DevP page address to byte address */
	sDevPAddr.uiAddr += ui32PageOffset;

	eErr = PDumpOSBufprintf(hScript,
			 ui32MaxLen,
			 "CBP :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X 0x%08X 0x%08X 0x%08X\r\n",
			 psMMUAttrib->sDevId.pszPDumpDevName,
			 (IMG_UINTPTR_T)hUniqueTag,
			 sDevPAddr.uiAddr & ~(psMMUAttrib->ui32DataPageMask),
			 (unsigned int)(sDevPAddr.uiAddr & (psMMUAttrib->ui32DataPageMask)),
			 ui32WPosVal,
			 ui32PacketSize,
			 ui32BufferSize);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}


/**************************************************************************
 * Function Name  : PDumpIDLWithFlags
 * Inputs         : Idle time in clocks
 * Outputs        : None
 * Returns        : Error
 * Description    : Dump IDL command to script
**************************************************************************/
PVRSRV_ERROR PDumpIDLWithFlags(IMG_UINT32 ui32Clocks, IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();
	PDUMP_DBG(("PDumpIDLWithFlags"));

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "IDL %u\r\n", ui32Clocks);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, ui32Flags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}


/**************************************************************************
 * Function Name  : PDumpIDL
 * Inputs         : Idle time in clocks
 * Outputs        : None
 * Returns        : Error
 * Description    : Dump IDL command to script
**************************************************************************/
PVRSRV_ERROR PDumpIDL(IMG_UINT32 ui32Clocks)
{
	return PDumpIDLWithFlags(ui32Clocks, PDUMP_FLAGS_CONTINUOUS);
}

/**************************************************************************
 * Function Name  : PDumpMemUM
 * Inputs         : pvAltLinAddrUM
 *				  : pvLinAddrUM
 *				  : psMemInfo
 *				  : ui32Offset
 *				  : ui32Bytes
 *				  : ui32Flags
 *				  : hUniqueTag
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Dump user mode memory
**************************************************************************/
PVRSRV_ERROR PDumpMemUM(PVRSRV_PER_PROCESS_DATA *psPerProc,
						IMG_PVOID pvAltLinAddrUM,
						IMG_PVOID pvLinAddrUM,
						PVRSRV_KERNEL_MEM_INFO *psMemInfo,
						IMG_UINT32 ui32Offset,
						IMG_UINT32 ui32Bytes,
						IMG_UINT32 ui32Flags,
						IMG_HANDLE hUniqueTag)
{
	IMG_VOID *pvAddrUM;
	IMG_VOID *pvAddrKM;
	PVRSRV_ERROR eError;

	if (psMemInfo->pvLinAddrKM != IMG_NULL && pvAltLinAddrUM == IMG_NULL)
	{
		/*
		 * There is a kernel virtual address for the memory that is
		 * being dumped, and no alternate user mode linear address.
		 */
		return PDumpMemKM(IMG_NULL,
					   psMemInfo,
					   ui32Offset,
					   ui32Bytes,
					   ui32Flags,
					   hUniqueTag);
	}

	pvAddrUM = (pvAltLinAddrUM != IMG_NULL) ? pvAltLinAddrUM : ((pvLinAddrUM != IMG_NULL) ? VPTR_PLUS(pvLinAddrUM, ui32Offset) : IMG_NULL);

	pvAddrKM = GetTempBuffer();

	/*
	 * The memory to be dumped needs to be copied in from
	 * the client.  Dump the memory, a buffer at a time.
	 */
	PVR_ASSERT(pvAddrUM != IMG_NULL && pvAddrKM != IMG_NULL);
	if (pvAddrUM == IMG_NULL || pvAddrKM == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PDumpMemUM: Nothing to dump"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32Bytes > PDUMP_TEMP_BUFFER_SIZE)
	{
		PDumpCommentWithFlags(ui32Flags, "Dumping 0x%08x bytes of memory, in blocks of 0x%08x bytes", ui32Bytes, (IMG_UINT32)PDUMP_TEMP_BUFFER_SIZE);
	}

	if (psMemInfo->ui32Flags & PVRSRV_MEM_SPARSE)
	{
		/*
			In case of sparse mappings we can't just copy the full range as not
			all pages are valid, instead we walk a page at a time only dumping
			if the a page exists at that address
		*/
		IMG_UINT32 ui32BytesRemain = ui32Bytes;
		IMG_UINT32 ui32InPageStart = ui32Offset & (~HOST_PAGEMASK);
		IMG_UINT32 ui32PageOffset = ui32Offset & (HOST_PAGEMASK);
		IMG_UINT32 ui32BytesToCopy = MIN(HOST_PAGESIZE() - ui32InPageStart, ui32BytesRemain);

		do
		{
			if (BM_MapPageAtOffset(BM_MappingHandleFromBuffer(psMemInfo->sMemBlk.hBuffer), ui32PageOffset))
			{
				eError = OSCopyFromUser(psPerProc,
							   pvAddrKM,
							   pvAddrUM,
							   ui32BytesToCopy);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR, "PDumpMemUM: OSCopyFromUser failed (%d)", eError));
					return eError;
				}

				/*
					At this point we know we're dumping a valid page so call
					the internal function
				*/
				eError = _PDumpMemIntKM(pvAddrKM,
										psMemInfo,
										ui32PageOffset + ui32InPageStart,
										ui32BytesToCopy,
										ui32Flags,
										hUniqueTag);
		
				if (eError != PVRSRV_OK)
				{
					/*
					 * If writing fails part way through, then some
					 * investigation is needed.
					 */
					if (ui32BytesToCopy != 0)
					{
						PVR_DPF((PVR_DBG_ERROR, "PDumpMemUM: PDumpMemKM failed (%d)", eError));
					}
					PVR_ASSERT(ui32BytesToCopy == 0);
					return eError;
				}
			}

			VPTR_INC(pvAddrUM, ui32BytesToCopy);
			ui32BytesRemain -= ui32BytesToCopy;
			ui32InPageStart = 0;
			ui32PageOffset += HOST_PAGESIZE();
		} while(ui32BytesRemain);
	}
	else
	{
		IMG_UINT32 ui32CurrentOffset = ui32Offset;
		IMG_UINT32 ui32BytesDumped;

		for (ui32BytesDumped = 0; ui32BytesDumped < ui32Bytes;)
		{
			IMG_UINT32 ui32BytesToDump = MIN(PDUMP_TEMP_BUFFER_SIZE, ui32Bytes - ui32BytesDumped);
	
			eError = OSCopyFromUser(psPerProc,
						   pvAddrKM,
						   pvAddrUM,
						   ui32BytesToDump);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "PDumpMemUM: OSCopyFromUser failed (%d)", eError));
				return eError;
			}
	
			eError = PDumpMemKM(pvAddrKM,
						   psMemInfo,
						   ui32CurrentOffset,
						   ui32BytesToDump,
						   ui32Flags,
						   hUniqueTag);
	
			if (eError != PVRSRV_OK)
			{
				/*
				 * If writing fails part way through, then some
				 * investigation is needed.
				 */
				if (ui32BytesDumped != 0)
				{
					PVR_DPF((PVR_DBG_ERROR, "PDumpMemUM: PDumpMemKM failed (%d)", eError));
				}
				PVR_ASSERT(ui32BytesDumped == 0);
				return eError;
			}
	
			VPTR_INC(pvAddrUM, ui32BytesToDump);
			ui32CurrentOffset += ui32BytesToDump;
			ui32BytesDumped += ui32BytesToDump;
		}
	}

	return PVRSRV_OK;
}


/**************************************************************************
 * Function Name  : _PdumpAllocMMUContext
 * Inputs         : pui32MMUContextID
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : pdump util to allocate MMU contexts
**************************************************************************/
static PVRSRV_ERROR _PdumpAllocMMUContext(IMG_UINT32 *pui32MMUContextID)
{
	IMG_UINT32 i;

	/* there are MAX_PDUMP_MMU_CONTEXTS contexts available, find one */
	for(i=0; i<MAX_PDUMP_MMU_CONTEXTS; i++)
	{
		if((gui16MMUContextUsage & (1U << i)) == 0)
		{
			/* mark in use */
			gui16MMUContextUsage |= 1U << i;
			*pui32MMUContextID = i;
			return PVRSRV_OK;
		}
	}

	PVR_DPF((PVR_DBG_ERROR, "_PdumpAllocMMUContext: no free MMU context ids"));

	return PVRSRV_ERROR_MMU_CONTEXT_NOT_FOUND;
}


/**************************************************************************
 * Function Name  : _PdumpFreeMMUContext
 * Inputs         : ui32MMUContextID
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : pdump util to free MMU contexts
**************************************************************************/
static PVRSRV_ERROR _PdumpFreeMMUContext(IMG_UINT32 ui32MMUContextID)
{
	if(ui32MMUContextID < MAX_PDUMP_MMU_CONTEXTS)
	{
		/* free the id */
		gui16MMUContextUsage &= ~(1U << ui32MMUContextID);
		return PVRSRV_OK;
	}

	PVR_DPF((PVR_DBG_ERROR, "_PdumpFreeMMUContext: MMU context ids invalid"));

	return PVRSRV_ERROR_MMU_CONTEXT_NOT_FOUND;
}


/**************************************************************************
 * Function Name  : PDumpSetMMUContext
 * Inputs         :
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Set MMU Context
**************************************************************************/
PVRSRV_ERROR PDumpSetMMUContext(PVRSRV_DEVICE_TYPE eDeviceType,
								IMG_CHAR *pszMemSpace,
								IMG_UINT32 *pui32MMUContextID,
								IMG_UINT32 ui32MMUType,
								IMG_HANDLE hUniqueTag1,
								IMG_HANDLE hOSMemHandle, 
								IMG_VOID *pvPDCPUAddr)
{
	IMG_UINT8 *pui8LinAddr = (IMG_UINT8 *)pvPDCPUAddr;
	IMG_CPU_PHYADDR sCpuPAddr;
	IMG_DEV_PHYADDR sDevPAddr;
	IMG_UINT32 ui32MMUContextID;
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();

	eErr = _PdumpAllocMMUContext(&ui32MMUContextID);
	if(eErr != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PDumpSetMMUContext: _PdumpAllocMMUContext failed: %d", eErr));
		PDUMP_UNLOCK();
		return eErr;
	}

	/* derive the DevPAddr */
	/* FIXME: if we used OSMemHandleToCPUPAddr() here, we could lose the lin addr arg */
	sCpuPAddr = OSMapLinToCPUPhys(hOSMemHandle, pui8LinAddr);
	sDevPAddr = SysCpuPAddrToDevPAddr(eDeviceType, sCpuPAddr);
	/* and round to 4k page */
	sDevPAddr.uiAddr &= ~((PVRSRV_4K_PAGE_SIZE) -1);

	eErr = PDumpOSBufprintf(hScript,
						ui32MaxLen, 
						"MMU :%s:v%d %d :%s:PA_" UINTPTR_FMT DEVPADDR_FMT "\r\n",
						pszMemSpace,
						ui32MMUContextID,
						ui32MMUType,
						pszMemSpace,
						(IMG_UINTPTR_T)hUniqueTag1,
						sDevPAddr.uiAddr);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}
	PDumpOSWriteString2(hScript, PDUMP_FLAGS_CONTINUOUS);

	/* return the MMU Context ID */
	*pui32MMUContextID = ui32MMUContextID;

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}


/**************************************************************************
 * Function Name  : PDumpClearMMUContext
 * Inputs         :
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Clear MMU Context
**************************************************************************/
PVRSRV_ERROR PDumpClearMMUContext(PVRSRV_DEVICE_TYPE eDeviceType,
								IMG_CHAR *pszMemSpace,
								IMG_UINT32 ui32MMUContextID,
								IMG_UINT32 ui32MMUType)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();
	PVR_UNREFERENCED_PARAMETER(eDeviceType);
	PVR_UNREFERENCED_PARAMETER(ui32MMUType);

	/* FIXME: Propagate error from PDumpComment once it's supported on
	 * all OSes and platforms
	 */
	PDumpComment("Clear MMU Context for memory space %s\r\n", pszMemSpace);

	/*
		Note:
		PDumpComment takes the lock so we can't take it until here
	*/
	PDUMP_LOCK();
	eErr = PDumpOSBufprintf(hScript,
						ui32MaxLen, 
						"MMU :%s:v%d\r\n",
						pszMemSpace,
						ui32MMUContextID);
	if(eErr != PVRSRV_OK)
	{
		PDUMP_UNLOCK();
		return eErr;
	}

	PDumpOSWriteString2(hScript, PDUMP_FLAGS_CONTINUOUS);

	eErr = _PdumpFreeMMUContext(ui32MMUContextID);
	if(eErr != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PDumpClearMMUContext: _PdumpFreeMMUContext failed: %d", eErr));
		PDUMP_UNLOCK();
		return eErr;
	}

	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/*****************************************************************************
 FUNCTION	: PDumpStoreMemToFile
    
 PURPOSE	: Dumps a given addr:size to a file

 PARAMETERS	:

 RETURNS	: 
*****************************************************************************/
PVRSRV_ERROR PDumpStoreMemToFile(PDUMP_MMU_ATTRIB *psMMUAttrib,
						         IMG_CHAR *pszFileName,
								 IMG_UINT32 ui32FileOffset, 
								 PVRSRV_KERNEL_MEM_INFO *psMemInfo,
								 IMG_UINT32 uiAddr, 
								 IMG_UINT32 ui32Size,
								 IMG_UINT32 ui32PDumpFlags,
								 IMG_HANDLE hUniqueTag)
{
	IMG_DEV_PHYADDR		sDevPAddr;
	IMG_DEV_VIRTADDR	sDevVPageAddr;
	IMG_UINT32			ui32PageOffset;

	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();
	/*
		query the buffer manager for the physical pages that back the
		virtual address
	*/
	ui32PageOffset = (IMG_UINT32)((IMG_UINTPTR_T)psMemInfo->pvLinAddrKM & psMMUAttrib->ui32DataPageMask);
	
	/* calculate the DevV page address */
	sDevVPageAddr.uiAddr = uiAddr - ui32PageOffset;
	
	/* get the physical page address based on the device virtual address */
	BM_GetPhysPageAddr(psMemInfo, sDevVPageAddr, &sDevPAddr);
	
	/* convert DevP page address to byte address */
	sDevPAddr.uiAddr += ui32PageOffset;

	PDumpOSBufprintf(hScript,
			 ui32MaxLen,
			 "SAB :%s:PA_" UINTPTR_FMT DEVPADDR_FMT ":0x%08X 0x%08X 0x%08X %s\r\n",
			 psMMUAttrib->sDevId.pszPDumpDevName,
			 (IMG_UINTPTR_T)hUniqueTag,
			 (sDevPAddr.uiAddr & ~psMMUAttrib->ui32DataPageMask),
			 (unsigned int)(sDevPAddr.uiAddr & psMMUAttrib->ui32DataPageMask),
			 ui32Size,
			 ui32FileOffset,
			 pszFileName);

	PDumpOSWriteString2(hScript, ui32PDumpFlags);
	
	PDUMP_UNLOCK();
	return PVRSRV_OK;	
}

/*****************************************************************************
 FUNCTION	: PDumpRegBasedCBP
    
 PURPOSE	: Dump CBP command to script

 PARAMETERS	:
			  
 RETURNS	: None
*****************************************************************************/
PVRSRV_ERROR PDumpRegBasedCBP(IMG_CHAR		*pszPDumpRegName,
							  IMG_UINT32	ui32RegOffset,
							  IMG_UINT32	ui32WPosVal,
							  IMG_UINT32	ui32PacketSize,
							  IMG_UINT32	ui32BufferSize,
							  IMG_UINT32	ui32Flags)
{
	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK();

	PDumpOSBufprintf(hScript,
			 ui32MaxLen,
			 "CBP :%s:0x%08X 0x%08X 0x%08X 0x%08X\r\n",
			 pszPDumpRegName,
			 ui32RegOffset,
			 ui32WPosVal,
			 ui32PacketSize,
			 ui32BufferSize);
	PDumpOSWriteString2(hScript, ui32Flags);

	PDUMP_UNLOCK();
	return PVRSRV_OK;		
}


/****************************************************
 * Non-uitron code here.
 * For example, code communicating with dbg driver.
 ***************************************************/
/* PRQA S 5087 1 */ /* include file needed here */
#include "syscommon.h"

/**************************************************************************
 * Function Name  : PDumpConnectionNotify
 * Description    : Called by the debugdrv to tell Services that pdump has
 * 					connected
 *					NOTE: No debugdrv on uitron.
 **************************************************************************/
IMG_EXPORT IMG_VOID PDumpConnectionNotify(IMG_VOID)
{
	SYS_DATA			*psSysData;
	PVRSRV_DEVICE_NODE	*psThis;
	PVR_DPF((PVR_DBG_WARNING, "PDump has connected."));
	
	/* Loop over all known devices */
	SysAcquireData(&psSysData);
	
	psThis = psSysData->psDeviceNodeList;
	while (psThis)
	{
		if (psThis->pfnPDumpInitDevice)
		{
			/* Reset pdump according to connected device */
			psThis->pfnPDumpInitDevice(psThis);
		}
		psThis = psThis->psNext;
	}
}

/*****************************************************************************
 * Function Name  : DbgWrite
 * Inputs         : psStream - debug stream to write to
 					pui8Data - buffer
 					ui32BCount - buffer length
 					ui32Flags - flags, e.g. continuous, LF
 * Outputs        : None
 * Returns        : Bytes written
 * Description    : Write a block of data to a debug stream
 *					NOTE: No debugdrv on uitron.
 *****************************************************************************/
IMG_UINT32 DbgWrite(PDBG_STREAM psStream, IMG_UINT8 *pui8Data, IMG_UINT32 ui32BCount, IMG_UINT32 ui32Flags)
{
	IMG_UINT32	ui32BytesWritten = 0;
	IMG_UINT32	ui32Off = 0;
	PDBG_STREAM_CONTROL psCtrl = psStream->psCtrl;

	/* Return immediately if marked as "never" */
	if ((ui32Flags & PDUMP_FLAGS_NEVER) != 0)
	{
		return ui32BCount;
	}
	
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
	/* Return if process is not marked for pdumping, unless it's persistent.
	 */
	if ( (_PDumpIsProcessActive() == IMG_FALSE ) &&
		 ((ui32Flags & PDUMP_FLAGS_PERSISTENT) == 0) && psCtrl->bInitPhaseComplete)
	{
		return ui32BCount;
	}
#endif

	/* Send persistent data first ...
	 * If we're still initialising the params will be captured to the
	 * init stream in the call to pfnDBGDrivWrite2 below.
	 */
	if ( ((ui32Flags & PDUMP_FLAGS_PERSISTENT) != 0) && (psCtrl->bInitPhaseComplete) )
	{
		while (ui32BCount > 0)
		{
			/*
				Params marked as persistent should be appended to the init phase.
				For example window system mem mapping of the primary surface.
			*/
			ui32BytesWritten = PDumpOSDebugDriverWrite(	psStream,
														PDUMP_WRITE_MODE_PERSISTENT,
														&pui8Data[ui32Off], ui32BCount, 1, 0);

			if (ui32BytesWritten == 0)
			{
				PVR_DPF((PVR_DBG_ERROR, "DbgWrite: Failed to send persistent data"));
				PDumpOSReleaseExecution();
			}

			if (ui32BytesWritten != 0xFFFFFFFFU)
			{
				ui32Off += ui32BytesWritten;
				ui32BCount -= ui32BytesWritten;
			}
			else
			{
				PVR_DPF((PVR_DBG_ERROR, "DbgWrite: Failed to send persistent data"));
				if( (psCtrl->ui32Flags & DEBUG_FLAGS_READONLY) != 0)
				{
					/* suspend pdump to prevent flooding kernel log buffer */
					PDumpSuspendKM();
				}
				return 0xFFFFFFFFU;
			}
		}
		
		/* reset buffer counters */
		ui32BCount = ui32Off; ui32Off = 0; ui32BytesWritten = 0;
	}

	while (((IMG_UINT32) ui32BCount > 0) && (ui32BytesWritten != 0xFFFFFFFFU))
	{
		/* If we're in the init phase we treat persisent as meaning continuous */
		if (((ui32Flags & PDUMP_FLAGS_CONTINUOUS) != 0) || ((ui32Flags & PDUMP_FLAGS_PERSISTENT) != 0))
		{
			/*
				If pdump client (or its equivalent) isn't running then throw continuous data away.
			*/
			if (((psCtrl->ui32CapMode & DEBUG_CAPMODE_FRAMED) != 0) &&
				 (psCtrl->ui32Start == 0xFFFFFFFFU) &&
				 (psCtrl->ui32End == 0xFFFFFFFFU) &&
				  psCtrl->bInitPhaseComplete)
			{
				ui32BytesWritten = ui32BCount;
			}
			else
			{
				ui32BytesWritten = PDumpOSDebugDriverWrite(	psStream, 
															PDUMP_WRITE_MODE_CONTINUOUS,
															&pui8Data[ui32Off], ui32BCount, 1, 0);
			}
		}
		else
		{
			if (ui32Flags & PDUMP_FLAGS_LASTFRAME)
			{
				IMG_UINT32	ui32DbgFlags;
	
				ui32DbgFlags = 0;
				if (ui32Flags & PDUMP_FLAGS_RESETLFBUFFER)
				{
					ui32DbgFlags |= WRITELF_FLAGS_RESETBUF;
				}
	
				ui32BytesWritten = PDumpOSDebugDriverWrite(	psStream,
															PDUMP_WRITE_MODE_LASTFRAME,
															&pui8Data[ui32Off], ui32BCount, 1, ui32DbgFlags);
			}
			else
			{
				ui32BytesWritten = PDumpOSDebugDriverWrite(	psStream, 
															PDUMP_WRITE_MODE_BINCM,
															&pui8Data[ui32Off], ui32BCount, 1, 0);
			}
		}

		/*
			If the debug driver's buffers are full so no data could be written then yield
			execution so pdump can run and empty them.
		*/
		if (ui32BytesWritten == 0)
		{
			if (ui32Flags & PDUMP_FLAGS_CONTINUOUS)
			{
				PVR_DPF((PVR_DBG_ERROR, "Buffer is full during writing of %s", &pui8Data[ui32Off]));
			}
			PDumpOSReleaseExecution();
		}

		if (ui32BytesWritten != 0xFFFFFFFFU)
		{
			ui32Off += ui32BytesWritten;
			ui32BCount -= ui32BytesWritten;
		}
		else
		{
			if (ui32Flags & PDUMP_FLAGS_CONTINUOUS)
			{
				PVR_DPF((PVR_DBG_ERROR, "Error during writing of %s", &pui8Data[ui32Off]));
			}
		}
		/* loop exits when i) all data is written, or ii) an unrecoverable error occurs */
	}

	return ui32BytesWritten;
}



#else	/* defined(PDUMP) */
/* disable warning about empty module */
#endif	/* defined(PDUMP) */
/*****************************************************************************
 End of file (pdump_common.c)
*****************************************************************************/
