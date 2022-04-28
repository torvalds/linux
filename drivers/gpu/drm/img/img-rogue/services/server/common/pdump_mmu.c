/*************************************************************************/ /*!
@File
@Title          MMU PDump functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Common PDump (MMU specific) functions
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
*/ /***************************************************************************/

#if defined(PDUMP)

#include "img_types.h"
#include "img_defs.h"
#include "pdump_mmu.h"
#include "pdump_km.h"
#include "pdump_physmem.h"
#include "osfunc.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "allocmem.h"

#define MAX_PDUMP_MMU_CONTEXTS	(10)
static IMG_UINT32 guiPDumpMMUContextAvailabilityMask = (1<<MAX_PDUMP_MMU_CONTEXTS)-1;


#define MMUPX_FMT(X) ((X<3) ? ((X<2) ?  "MMUPT_\0" : "MMUPD_\0") : "MMUPC_\0")
#define MIPSMMUPX_FMT(X) ((X<3) ? ((X<2) ?  "MIPSMMUPT_\0" : "MIPSMMUPD_\0") : "MIPSMMUPC_\0")


/* Array used to look-up debug strings from MMU_LEVEL */
static const IMG_CHAR * const apszMMULevelStringLookup[MMU_LEVEL_LAST] =
{
	"MMU_LEVEL_0",
	"PAGE_TABLE",
	"PAGE_DIRECTORY",
	"PAGE_CATALOGUE",
};

static PVRSRV_ERROR
_ContiguousPDumpBytes(const IMG_CHAR *pszSymbolicName,
		      IMG_UINT32 ui32SymAddrOffset,
		      IMG_BOOL bFlush,
		      IMG_UINT32 uiNumBytes,
		      void *pvBytes,
		      IMG_UINT32 ui32Flags)
{
	static const IMG_CHAR *pvBeyondLastPointer;
	static const IMG_CHAR *pvBasePointer;
	static IMG_UINT32 ui32BeyondLastOffset;
	static IMG_UINT32 ui32BaseOffset;
	static IMG_UINT32 uiAccumulatedBytes;
	IMG_UINT32 ui32ParamOutPos;
	PVRSRV_ERROR eErr = PVRSRV_OK;

	PDUMP_GET_SCRIPT_AND_FILE_STRING();

	/* Caller has PDUMP_LOCK */

	if (!bFlush && uiAccumulatedBytes > 0)
	{
		/* do some tests for contiguity.  If it fails, we flush anyway */
		if (pvBeyondLastPointer != pvBytes ||
		    ui32SymAddrOffset != ui32BeyondLastOffset
		    /* NB: ought to check that symbolic name agrees too, but
		       we know this always to be the case in the current use-case */
			)
		{
			bFlush = IMG_TRUE;
		}
	}

	/* Flush if necessary */
	if (bFlush && uiAccumulatedBytes > 0)
	{
		eErr = PDumpWriteParameter((IMG_UINT8 *)(uintptr_t)pvBasePointer,
					   uiAccumulatedBytes, ui32Flags,
					   &ui32ParamOutPos, pszFileName);
		if (eErr == PVRSRV_OK)
		{
			eErr = PDumpSNPrintf(hScript, ui32MaxLenScript,
					     "LDB %s:0x%X 0x%X 0x%X %s",
					     /* dest */
					     pszSymbolicName,
					     ui32BaseOffset,
					     /* size */
					     uiAccumulatedBytes,
					     /* file offset */
					     ui32ParamOutPos,
					     /* filename */
					     pszFileName);
			PVR_LOG_GOTO_IF_ERROR(eErr, "PDumpSNPrintf", ErrOut);

			PDumpWriteScript(hScript, ui32Flags);
		}
		else if (eErr != PVRSRV_ERROR_PDUMP_NOT_ALLOWED)
		{
			PVR_LOG_GOTO_IF_ERROR(eErr, "PDumpWriteParameter", ErrOut);
		}
		else
		{
			/* else Write to parameter file prevented under the flags and
			 * current state of the driver so skip write to script and error IF.
			 * this is normal e.g. no in capture range for example.
			 */
			eErr = PVRSRV_OK;
		}

		uiAccumulatedBytes = 0;
	}

	/* Initialise offsets and pointers if necessary */
	if (uiAccumulatedBytes == 0)
	{
		ui32BaseOffset = ui32BeyondLastOffset = ui32SymAddrOffset;
		pvBeyondLastPointer = pvBasePointer = (const IMG_CHAR *)pvBytes;
	}

	/* Accumulate some bytes */
	ui32BeyondLastOffset += uiNumBytes;
	pvBeyondLastPointer += uiNumBytes;
	uiAccumulatedBytes += uiNumBytes;

ErrOut:
	PDUMP_RELEASE_SCRIPT_AND_FILE_STRING();
	return eErr;
}

/**************************************************************************
 * Function Name  : PDumpMMUMalloc
 * Inputs         :
 * Outputs        :
 * Returns        : PVRSRV_ERROR
 * Description    :
**************************************************************************/
PVRSRV_ERROR PDumpMMUMalloc(const IMG_CHAR *pszPDumpDevName,
			    MMU_LEVEL eMMULevel,
			    IMG_DEV_PHYADDR *psDevPAddr,
			    IMG_UINT32 ui32Size,
			    IMG_UINT32 ui32Align,
			    PDUMP_MMU_TYPE eMMUType)
{
	PVRSRV_ERROR eErr = PVRSRV_OK;
	IMG_UINT32 ui32Flags = PDUMP_FLAGS_CONTINUOUS | PDUMP_FLAGS_BLKDATA;
	IMG_UINT64 ui64SymbolicAddr;
	IMG_CHAR *pszMMUPX;

	PDUMP_GET_SCRIPT_STRING();

	PVR_GOTO_IF_INVALID_PARAM(eMMULevel < MMU_LEVEL_LAST, eErr, ErrOut);

	PDUMP_LOCK(ui32Flags);

	/*
	  Write a comment to the PDump2 script streams indicating the memory allocation
	*/
	eErr = PDumpSNPrintf(hScript,
			     ui32MaxLen,
			     "-- CALLOC :%s:%s Size=0x%08X Alignment=0x%08X 0x0 DevPAddr=0x%08"IMG_UINT64_FMTSPECX,
			     pszPDumpDevName,
			     apszMMULevelStringLookup[eMMULevel],
			     ui32Size,
			     ui32Align,
			     psDevPAddr->uiAddr);
	PVR_GOTO_IF_ERROR(eErr, ErrUnlock);

	PDumpWriteScript(hScript, ui32Flags);

	/*
	  construct the symbolic address
	*/
	ui64SymbolicAddr = (IMG_UINT64)psDevPAddr->uiAddr;

	/*
	  Write to the MMU script stream indicating the memory allocation
	*/
	if (eMMUType == PDUMP_MMU_TYPE_MIPS_MICROAPTIV)
	{
		pszMMUPX = MIPSMMUPX_FMT(eMMULevel);
	}
	else
	{
		pszMMUPX = MMUPX_FMT(eMMULevel);
	}
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "CALLOC :%s:%s%016"IMG_UINT64_FMTSPECX" 0x%X 0x%X 0x0",
			     pszPDumpDevName,
			     pszMMUPX,
			     ui64SymbolicAddr,
			     ui32Size,
			     ui32Align
			     /* don't need this sDevPAddr.uiAddr*/);
	PVR_GOTO_IF_ERROR(eErr, ErrUnlock);
	PDumpWriteScript(hScript, ui32Flags);

ErrUnlock:
	PDUMP_UNLOCK(ui32Flags);
ErrOut:
	PDUMP_RELEASE_SCRIPT_STRING();
	return eErr;
}

/**************************************************************************
 * Function Name  : PDumpMMUFree
 * Inputs         :
 * Outputs        :
 * Returns        : PVRSRV_ERROR
 * Description    :
**************************************************************************/
PVRSRV_ERROR PDumpMMUFree(const IMG_CHAR *pszPDumpDevName,
			  MMU_LEVEL eMMULevel,
			  IMG_DEV_PHYADDR *psDevPAddr,
			  PDUMP_MMU_TYPE eMMUType)
{
	PVRSRV_ERROR eErr = PVRSRV_OK;
	IMG_UINT64 ui64SymbolicAddr;
	IMG_UINT32 ui32Flags = PDUMP_FLAGS_CONTINUOUS | PDUMP_FLAGS_BLKDATA;
	IMG_CHAR *pszMMUPX;

	PDUMP_GET_SCRIPT_STRING();

	PVR_GOTO_IF_INVALID_PARAM(eMMULevel < MMU_LEVEL_LAST, eErr, ErrOut);

	PDUMP_LOCK(ui32Flags);
	/*
	  Write a comment to the PDUMP2 script streams indicating the memory free
	*/
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "-- FREE :%s:%s",
			     pszPDumpDevName, apszMMULevelStringLookup[eMMULevel]);
	PVR_GOTO_IF_ERROR(eErr, ErrUnlock);

	PDumpWriteScript(hScript, ui32Flags);

	/*
	  construct the symbolic address
	*/
	ui64SymbolicAddr = (IMG_UINT64)psDevPAddr->uiAddr;

	/*
	  Write to the MMU script stream indicating the memory free
	*/
	if (eMMUType == PDUMP_MMU_TYPE_MIPS_MICROAPTIV)
	{
		pszMMUPX = MIPSMMUPX_FMT(eMMULevel);
	}
	else
	{
		pszMMUPX = MMUPX_FMT(eMMULevel);
	}
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "FREE :%s:%s%016"IMG_UINT64_FMTSPECX,
			     pszPDumpDevName,
			     pszMMUPX,
			     ui64SymbolicAddr);
	PVR_GOTO_IF_ERROR(eErr, ErrUnlock);
	PDumpWriteScript(hScript, ui32Flags);

ErrUnlock:
	PDUMP_UNLOCK(ui32Flags);
ErrOut:
	PDUMP_RELEASE_SCRIPT_STRING();
	return eErr;
}

/*******************************************************************************************************
 * Function Name  : PDumpPTBaseObjectToMem64
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a memory write from the baseobject
 *					for MIPS MMU device type
********************************************************************************************************/
PVRSRV_ERROR PDumpPTBaseObjectToMem64(const IMG_CHAR *pszPDumpDevName,
				      PMR *psPMRDest,
				      IMG_DEVMEM_OFFSET_T uiLogicalOffsetSource,
				      IMG_DEVMEM_OFFSET_T uiLogicalOffsetDest,
				      IMG_UINT32 ui32Flags,
				      MMU_LEVEL eMMULevel,
				      IMG_UINT64 ui64PxSymAddr,
				      IMG_UINT64 ui64PxOffset)
{

	IMG_CHAR aszMemspaceNameDest[PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH];
	IMG_CHAR aszSymbolicNameDest[PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH];
	IMG_DEVMEM_OFFSET_T uiPDumpSymbolicOffsetDest;
	IMG_DEVMEM_OFFSET_T uiNextSymNameDest;
	PVRSRV_ERROR eErr = PVRSRV_OK;

	PDUMP_GET_SCRIPT_STRING()

	eErr = PMR_PDumpSymbolicAddr(psPMRDest,
				     uiLogicalOffsetDest,
				     PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH,
				     aszMemspaceNameDest,
				     PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH,
				     aszSymbolicNameDest,
				     &uiPDumpSymbolicOffsetDest,
				     &uiNextSymNameDest);

	PVR_GOTO_IF_ERROR(eErr, Err);

	eErr = PDumpSNPrintf(hScript, ui32MaxLen,
						 "WRW64 :%s:%s:0x%"IMG_UINT64_FMTSPECX" :%s:%s%016"IMG_UINT64_FMTSPECX":0x%"IMG_UINT64_FMTSPECX,
						 aszMemspaceNameDest, aszSymbolicNameDest, uiPDumpSymbolicOffsetDest,
						 pszPDumpDevName, MIPSMMUPX_FMT(eMMULevel), ui64PxSymAddr, ui64PxOffset);

	PVR_GOTO_IF_ERROR(eErr, Err);

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

Err:
	PDUMP_RELEASE_SCRIPT_STRING();
	return eErr;
}

/**************************************************************************
 * Function Name  : PDumpMMUDumpPxEntries
 * Inputs         :
 * Outputs        :
 * Returns        : PVRSRV_ERROR
 * Description    :
**************************************************************************/
PVRSRV_ERROR PDumpMMUDumpPxEntries(MMU_LEVEL eMMULevel,
				   const IMG_CHAR *pszPDumpDevName,
				   void *pvPxMem,
				   IMG_DEV_PHYADDR sPxDevPAddr,
				   IMG_UINT32 uiFirstEntry,
				   IMG_UINT32 uiNumEntries,
				   const IMG_CHAR *pszMemspaceName,
				   const IMG_CHAR *pszSymbolicAddr,
				   IMG_UINT64 uiSymbolicAddrOffset,
				   IMG_UINT32 uiBytesPerEntry,
				   IMG_UINT32 uiLog2Align,
				   IMG_UINT32 uiAddrShift,
				   IMG_UINT64 uiAddrMask,
				   IMG_UINT64 uiPxEProtMask,
				   IMG_UINT64 uiDataValidEnable,
				   IMG_UINT32 ui32Flags,
				   PDUMP_MMU_TYPE eMMUType)
{
	PVRSRV_ERROR eErr = PVRSRV_OK;
	IMG_UINT64 ui64PxSymAddr;
	IMG_UINT64 ui64PxEValueSymAddr;
	IMG_UINT32 ui32SymAddrOffset = 0;
	IMG_UINT32 *pui32PxMem;
	IMG_UINT64 *pui64PxMem;
	IMG_BOOL   bPxEValid;
	IMG_UINT32 uiPxEIdx;
	IMG_INT32  iShiftAmount;
	IMG_CHAR   *pszWrwSuffix = NULL;
	void *pvRawBytes = NULL;
	IMG_CHAR aszPxSymbolicAddr[PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH];
	IMG_UINT64 ui64PxE64;
	IMG_UINT64 ui64Protflags64;
	IMG_CHAR *pszMMUPX;

	PDUMP_GET_SCRIPT_STRING();
	ui32Flags |= (PDUMP_FLAGS_BLKDATA | PDUMP_FLAGS_CONTINUOUS);

	eErr = PDumpReady();
	if (eErr != PVRSRV_OK)
	{
		/* Mask suspension from caller as this is terminal & logged */
		eErr = (eErr == PVRSRV_ERROR_PDUMP_NOT_ACTIVE) ? PVRSRV_OK : eErr;
		goto ErrOut;
	}

	PVR_GOTO_IF_INVALID_PARAM(pvPxMem, eErr, ErrOut);

	/*
	  create the symbolic address of the Px
	*/
	ui64PxSymAddr = sPxDevPAddr.uiAddr;

	if (eMMUType == PDUMP_MMU_TYPE_MIPS_MICROAPTIV)
	{
		pszMMUPX = MIPSMMUPX_FMT(eMMULevel);
	}
	else
	{
		pszMMUPX = MMUPX_FMT(eMMULevel);
	}

	OSSNPrintf(aszPxSymbolicAddr,
	           PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH,
	           ":%s:%s%016"IMG_UINT64_FMTSPECX,
	           pszPDumpDevName,
	           pszMMUPX,
	           ui64PxSymAddr);

	PDUMP_LOCK(ui32Flags);

	/*
	  traverse PxEs, dumping entries
	*/
	for (uiPxEIdx = uiFirstEntry;
	     uiPxEIdx < uiFirstEntry + uiNumEntries;
	     uiPxEIdx++)
	{
		/* Calc the symbolic address offset of the PxE location
		   This is what we have to add to the table address to get to a certain entry */
		ui32SymAddrOffset = (uiPxEIdx*uiBytesPerEntry);

		/* Calc the symbolic address of the PxE value and HW protflags */
		/* just read it here */
		switch (uiBytesPerEntry)
		{
			case 4:
			{
				pui32PxMem = pvPxMem;
				ui64PxE64 = pui32PxMem[uiPxEIdx];
				pszWrwSuffix = "";
				pvRawBytes = &pui32PxMem[uiPxEIdx];
				break;
			}
			case 8:
			{
				pui64PxMem = pvPxMem;
				ui64PxE64 = pui64PxMem[uiPxEIdx];
				pszWrwSuffix = "64";
				pvRawBytes = &pui64PxMem[uiPxEIdx];
				break;
			}
			default:
			{
				PVR_DPF((PVR_DBG_ERROR, "PDumpMMUPxEntries: error"));
				ui64PxE64 = 0;
				//!!error
				break;
			}
		}

		ui64PxEValueSymAddr = (ui64PxE64 & uiAddrMask) >> uiAddrShift << uiLog2Align;
		ui64Protflags64 = ui64PxE64 & uiPxEProtMask;
		bPxEValid = (ui64Protflags64 & uiDataValidEnable) ? IMG_TRUE : IMG_FALSE;

		if (!bPxEValid)
		{
			/* If the entry was "invalid", simply write the actual
			   value found to the memory location */
			eErr = _ContiguousPDumpBytes(aszPxSymbolicAddr, ui32SymAddrOffset, IMG_FALSE,
			                             uiBytesPerEntry, pvRawBytes,
			                             ui32Flags);
			if (eErr == PVRSRV_OK)
			{
				goto done;
			}
			else
			{
				goto ErrUnlock;
			}
		}

		_ContiguousPDumpBytes(aszPxSymbolicAddr, ui32SymAddrOffset, IMG_TRUE,
		                      0, NULL,
		                      ui32Flags);

		iShiftAmount = (IMG_INT32)(uiLog2Align - uiAddrShift);

		/* First put the symbolic representation of the actual
		   address of the entry into a pdump internal register */
		/* MOV seemed cleaner here, since (a) it's 64-bit; (b) the
		   target is not memory.  However, MOV cannot do the
		   "reference" of the symbolic address.  Apparently WRW is
		   correct. */

		if (pszSymbolicAddr == NULL)
		{
			pszSymbolicAddr = "none";
		}

		if (eMMULevel == MMU_LEVEL_1)
		{
			if (iShiftAmount == 0)
			{
				eErr = PDumpSNPrintf(hScript,
				                     ui32MaxLen,
				                     "WRW%s :%s:%s%016"IMG_UINT64_FMTSPECX":0x%08X :%s:%s:0x%"IMG_UINT64_FMTSPECX" | 0x%"IMG_UINT64_FMTSPECX"\n",
				                     pszWrwSuffix,
				                     /* dest */
				                     pszPDumpDevName,
				                     pszMMUPX,
				                     ui64PxSymAddr,
				                     ui32SymAddrOffset,
				                     /* src */
				                     pszMemspaceName,
				                     pszSymbolicAddr,
				                     uiSymbolicAddrOffset,
				                     /* ORing prot flags */
				                     ui64Protflags64);
			}
			else
			{
				eErr = PDumpSNPrintf(hScript,
				                     ui32MaxLen,
				                    "WRW :%s:$1 :%s:%s:0x%"IMG_UINT64_FMTSPECX"\n",
				                     /* dest */
				                     pszPDumpDevName,
				                     /* src */
				                     pszMemspaceName,
				                     pszSymbolicAddr,
				                     uiSymbolicAddrOffset);
			}
		}
		else
		{
			if (eMMUType == PDUMP_MMU_TYPE_MIPS_MICROAPTIV)
			{
				pszMMUPX = MIPSMMUPX_FMT(eMMULevel - 1);
			}
			else
			{
				pszMMUPX = MMUPX_FMT(eMMULevel - 1);
			}
			eErr = PDumpSNPrintf(hScript,
			                     ui32MaxLen,
			                     "WRW :%s:$1 :%s:%s%016"IMG_UINT64_FMTSPECX":0x0",
			                     /* dest */
			                     pszPDumpDevName,
			                     /* src */
			                     pszPDumpDevName,
			                     pszMMUPX,
			                     ui64PxEValueSymAddr);
			if (eMMUType == PDUMP_MMU_TYPE_MIPS_MICROAPTIV)
			{
				pszMMUPX = MIPSMMUPX_FMT(eMMULevel);
			}
			else
			{
				pszMMUPX = MMUPX_FMT(eMMULevel);
			}
		}

		PVR_GOTO_IF_ERROR(eErr, ErrUnlock);
		PDumpWriteScript(hScript, ui32Flags);

		/* Now shift it to the right place, if necessary: */
		/* Now shift that value down, by the "Align shift"
		   amount, to get it into units (ought to assert that
		   we get an integer - i.e. we don't shift any bits
		   off the bottom, don't know how to do PDUMP
		   assertions yet) and then back up by the right
		   amount to get it into the position of the field.
		   This is optimised into a single shift right by the
		   difference between the two. */
		if (iShiftAmount > 0)
		{
			/* Page X Address is specified in units larger
			   than the position in the PxE would suggest. */
			eErr = PDumpSNPrintf(hScript,
			                     ui32MaxLen,
			                     "SHR :%s:$1 :%s:$1 0x%X",
			                     /* dest */
			                     pszPDumpDevName,
			                     /* src A */
			                     pszPDumpDevName,
			                     /* src B */
			                     iShiftAmount);
			PVR_GOTO_IF_ERROR(eErr, ErrUnlock);
			PDumpWriteScript(hScript, ui32Flags);
		}
		else if (iShiftAmount < 0)
		{
			/* Page X Address is specified in units smaller
			   than the position in the PxE would suggest. */
			eErr = PDumpSNPrintf(hScript,
			                     ui32MaxLen,
			                     "SHL :%s:$1 :%s:$1 0x%X",
			                     /* dest */
			                     pszPDumpDevName,
			                     /* src A */
			                     pszPDumpDevName,
			                     /* src B */
			                     -iShiftAmount);
			PVR_GOTO_IF_ERROR(eErr, ErrUnlock);
			PDumpWriteScript(hScript, ui32Flags);
		}

		if (eMMULevel == MMU_LEVEL_1)
		{
			if (iShiftAmount != 0)
			{
				/* Now we can "or" in the protection flags */
				eErr = PDumpSNPrintf(hScript,
				                     ui32MaxLen,
				                     "OR :%s:$1 :%s:$1 0x%"IMG_UINT64_FMTSPECX,
				                     /* dest */
				                     pszPDumpDevName,
				                     /* src A */
				                     pszPDumpDevName,
				                     /* src B */
				                     ui64Protflags64);
				PVR_GOTO_IF_ERROR(eErr, ErrUnlock);
				PDumpWriteScript(hScript, ui32Flags);
				eErr = PDumpSNPrintf(hScript,
				                     ui32MaxLen,
				                     "WRW%s :%s:%s%016"IMG_UINT64_FMTSPECX":0x%08X :%s:$1",
				                     pszWrwSuffix,
				                     /* dest */
				                     pszPDumpDevName,
				                     pszMMUPX,
				                     ui64PxSymAddr,
				                     ui32SymAddrOffset,
				                     /* src */
				                     pszPDumpDevName);
				PVR_GOTO_IF_ERROR(eErr, ErrUnlock);
				PDumpWriteScript(hScript, ui32Flags);
			}
		}
		else
		{
			/* Now we can "or" in the protection flags */
			eErr = PDumpSNPrintf(hScript,
			                     ui32MaxLen,
			                     "OR :%s:$1 :%s:$1 0x%"IMG_UINT64_FMTSPECX,
			                     /* dest */
			                     pszPDumpDevName,
			                     /* src A */
			                     pszPDumpDevName,
			                     /* src B */
			                     ui64Protflags64);
			PVR_GOTO_IF_ERROR(eErr, ErrUnlock);
			PDumpWriteScript(hScript, ui32Flags);

			/* Finally, we write the register into the actual PxE */
			eErr = PDumpSNPrintf(hScript,
			                     ui32MaxLen,
			                     "WRW%s :%s:%s%016"IMG_UINT64_FMTSPECX":0x%08X :%s:$1",
			                     pszWrwSuffix,
			                     /* dest */
			                     pszPDumpDevName,
			                     pszMMUPX,
			                     ui64PxSymAddr,
			                     ui32SymAddrOffset,
			                     /* src */
			                     pszPDumpDevName);
			PVR_GOTO_IF_ERROR(eErr, ErrUnlock);
			PDumpWriteScript(hScript, ui32Flags);
		}
	}

done:
	/* flush out any partly accumulated stuff for LDB */
	_ContiguousPDumpBytes(aszPxSymbolicAddr, ui32SymAddrOffset, IMG_TRUE,
	                      0, NULL,
	                      ui32Flags);

ErrUnlock:
	PDUMP_UNLOCK(ui32Flags);
ErrOut:
	PDUMP_RELEASE_SCRIPT_STRING();
	return eErr;
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
	for (i=0; i<MAX_PDUMP_MMU_CONTEXTS; i++)
	{
		if ((guiPDumpMMUContextAvailabilityMask & (1U << i)))
		{
			/* mark in use */
			guiPDumpMMUContextAvailabilityMask &= ~(1U << i);
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
	if (ui32MMUContextID < MAX_PDUMP_MMU_CONTEXTS)
	{
		/* free the id */
		PVR_ASSERT (!(guiPDumpMMUContextAvailabilityMask & (1U << ui32MMUContextID)));
		guiPDumpMMUContextAvailabilityMask |= (1U << ui32MMUContextID);
		return PVRSRV_OK;
	}

	PVR_DPF((PVR_DBG_ERROR, "_PdumpFreeMMUContext: MMU context ids invalid"));

	return PVRSRV_ERROR_MMU_CONTEXT_NOT_FOUND;
}

/**************************************************************************
 * Function Name  : PDumpMMUAllocMMUContext
 * Inputs         :
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Alloc MMU Context
**************************************************************************/
PVRSRV_ERROR PDumpMMUAllocMMUContext(const IMG_CHAR *pszPDumpMemSpaceName,
				     IMG_DEV_PHYADDR sPCDevPAddr,
				     PDUMP_MMU_TYPE eMMUType,
				     IMG_UINT32 *pui32MMUContextID,
				     IMG_UINT32 ui32PDumpFlags)
{
	IMG_UINT64 ui64PCSymAddr;
	IMG_CHAR *pszMMUPX;
	IMG_UINT32 ui32MMUContextID;
	PVRSRV_ERROR eErr = PVRSRV_OK;
	PDUMP_GET_SCRIPT_STRING();

	eErr = _PdumpAllocMMUContext(&ui32MMUContextID);
	if (eErr != PVRSRV_OK)
	{
		PVR_LOG_ERROR(eErr, "_PdumpAllocMMUContext");
		PVR_DBG_BREAK;
		goto ErrOut;
	}

	/*
	  create the symbolic address of the PC
	*/
	ui64PCSymAddr = sPCDevPAddr.uiAddr;

	if (eMMUType == PDUMP_MMU_TYPE_MIPS_MICROAPTIV)
	{
		pszMMUPX = MIPSMMUPX_FMT(1);
		/* Giving it a mock value until the Pdump player implements
		   the support for the MIPS microAptiv MMU*/
		eMMUType = PDUMP_MMU_TYPE_VARPAGE_40BIT;
	}
	else
	{
		pszMMUPX = MMUPX_FMT(3);
	}

	eErr = PDumpSNPrintf(hScript,
			     ui32MaxLen,
			     "MMU :%s:v%d %d :%s:%s%016"IMG_UINT64_FMTSPECX,
			     /* mmu context */
			     pszPDumpMemSpaceName,
			     ui32MMUContextID,
			     /* mmu type */
			     eMMUType,
			     /* PC base address */
			     pszPDumpMemSpaceName,
			     pszMMUPX,
			     ui64PCSymAddr);
	if (eErr != PVRSRV_OK)
	{
		PVR_DBG_BREAK;
		goto ErrOut;
	}

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(hScript, ui32PDumpFlags);
	PDUMP_UNLOCK(ui32PDumpFlags);

	/* return the MMU Context ID */
	*pui32MMUContextID = ui32MMUContextID;

ErrOut:
	PDUMP_RELEASE_SCRIPT_STRING();
	return eErr;
}

/**************************************************************************
 * Function Name  : PDumpMMUFreeMMUContext
 * Inputs         :
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Free MMU Context
**************************************************************************/
PVRSRV_ERROR PDumpMMUFreeMMUContext(const IMG_CHAR *pszPDumpMemSpaceName,
				    IMG_UINT32 ui32MMUContextID,
				    IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr = PVRSRV_OK;
	PDUMP_GET_SCRIPT_STRING();

	PDUMP_LOCK(ui32PDumpFlags);
	eErr = PDumpSNPrintf(hScript,
			     ui32MaxLen,
			     "-- Clear MMU Context for memory space %s", pszPDumpMemSpaceName);
	PVR_GOTO_IF_ERROR(eErr, ErrUnlock);

	PDumpWriteScript(hScript, ui32PDumpFlags);

	eErr = PDumpSNPrintf(hScript,
	                     ui32MaxLen,
	                     "MMU :%s:v%d",
	                     pszPDumpMemSpaceName,
	                     ui32MMUContextID);

	PVR_GOTO_IF_ERROR(eErr, ErrUnlock);

	PDumpWriteScript(hScript, ui32PDumpFlags);

	eErr = _PdumpFreeMMUContext(ui32MMUContextID);
	PVR_LOG_GOTO_IF_ERROR(eErr, "_PdumpFreeMMUContext", ErrUnlock);

ErrUnlock:
	PDUMP_UNLOCK(ui32PDumpFlags);
	PDUMP_RELEASE_SCRIPT_STRING();
	return eErr;
}

PVRSRV_ERROR
PDumpMMUSAB(const IMG_CHAR *pszPDumpMemNamespace,
	    IMG_UINT32 uiPDumpMMUCtx,
	    IMG_DEV_VIRTADDR sDevAddrStart,
	    IMG_DEVMEM_SIZE_T uiSize,
	    const IMG_CHAR *pszFilename,
	    IMG_UINT32 uiFileOffset,
	    IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr = PVRSRV_OK;

	// "SAB :%s:v%x:0x%010llX 0x%08X 0x%08X %s.bin",

	PDUMP_GET_SCRIPT_STRING();

	eErr = PDumpReady();
	if (eErr != PVRSRV_OK)
	{
		/* Mask suspension from caller as this is terminal & logged */
		eErr = (eErr == PVRSRV_ERROR_PDUMP_NOT_ACTIVE) ? PVRSRV_OK : eErr;
		goto ErrOut;
	}

	eErr = PDumpSNPrintf(hScript,
			     ui32MaxLen,
			     "SAB :%s:v%x:" IMG_DEV_VIRTADDR_FMTSPEC " "
			     IMG_DEVMEM_SIZE_FMTSPEC " "
			     "0x%x %s.bin\n",
			     pszPDumpMemNamespace,
			     uiPDumpMMUCtx,
			     sDevAddrStart.uiAddr,
			     uiSize,
			     uiFileOffset,
			     pszFilename);
	PVR_GOTO_IF_ERROR(eErr, ErrOut);

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(hScript, ui32PDumpFlags);
	PDUMP_UNLOCK(ui32PDumpFlags);

ErrOut:
	PDUMP_RELEASE_SCRIPT_STRING();
	return eErr;
}

#endif /* #if defined(PDUMP) */
