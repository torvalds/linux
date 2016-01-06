/*************************************************************************/ /*!
@File
@Title		MMU PDump functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Common PDump (MMU specific) functions
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

#if defined (PDUMP)

#include "img_types.h"
#include "pdump_mmu.h"
#include "pdump_osfunc.h"
#include "pdump_km.h"
#include "osfunc.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"

#define MAX_PDUMP_MMU_CONTEXTS	(10)
static IMG_UINT32 guiPDumpMMUContextAvailabilityMask = (1<<MAX_PDUMP_MMU_CONTEXTS)-1;

/* arbitrary buffer length here. */
#define MAX_SYMBOLIC_ADDRESS_LENGTH 40

#define MMUPX_FMT(X) ((X<3) ? ((X<2) ?  "MMUPT_\0" : "MMUPD_\0") : "MMUPC_\0")


/* Array used to look-up debug strings from MMU_LEVEL */
static IMG_CHAR ai8MMULevelStringLookup[MMU_LEVEL_LAST][15] =
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
                      IMG_VOID *pvBytes,
                      IMG_UINT32 ui32Flags)
{
    static const IMG_CHAR *pvBeyondLastPointer;
    static const IMG_CHAR *pvBasePointer;
    static IMG_UINT32 ui32BeyondLastOffset;
    static IMG_UINT32 ui32BaseOffset;
    static IMG_UINT32 uiAccumulatedBytes = 0;
	IMG_UINT32 ui32ParamOutPos;
    PVRSRV_ERROR eErr = PVRSRV_OK;

	PDUMP_GET_SCRIPT_AND_FILE_STRING();
	PVR_UNREFERENCED_PARAMETER(ui32MaxLenFileName);

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
        eErr = PDumpWriteParameter((IMG_UINT8 *)(IMG_UINTPTR_T)pvBasePointer,
                               uiAccumulatedBytes, ui32Flags,
                               &ui32ParamOutPos, pszFileName);
    	PVR_LOGG_IF_ERROR(eErr, "PDumpWriteParameter", ErrOut);

        eErr = PDumpOSBufprintf(hScript, ui32MaxLenScript,
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
    	PVR_LOGG_IF_ERROR(eErr, "PDumpOSBufprintf", ErrOut);

        PDumpWriteScript(hScript, ui32Flags);

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
    return eErr;
}


/**************************************************************************
 * Function Name  : PDumpMMUMalloc
 * Inputs         :
 * Outputs        : 
 * Returns        : PVRSRV_ERROR
 * Description    : 
**************************************************************************/
PVRSRV_ERROR PDumpMMUMalloc(const IMG_CHAR			*pszPDumpDevName,
							MMU_LEVEL 				eMMULevel,
							IMG_DEV_PHYADDR			*psDevPAddr,
							IMG_UINT32				ui32Size,
							IMG_UINT32				ui32Align)
{
	PVRSRV_ERROR eErr;
	IMG_UINT32 ui32Flags = PDUMP_FLAGS_CONTINUOUS;
	IMG_UINT64 ui64SymbolicAddr;
	IMG_CHAR *pszMMUPX;

	PDUMP_GET_SCRIPT_STRING();

	ui32Flags |= ( PDumpIsPersistent() ) ? PDUMP_FLAGS_PERSISTENT : 0;

	if (eMMULevel >= MMU_LEVEL_LAST)
	{
		eErr = PVRSRV_ERROR_INVALID_PARAMS;
		goto ErrOut;
	}

	/*
		Write a comment to the PDump2 script streams indicating the memory allocation
	*/
	eErr = PDumpOSBufprintf(hScript,
							ui32MaxLen,
							"-- MALLOC :%s:%s Size=0x%08X Alignment=0x%08X DevPAddr=0x%08llX",
							pszPDumpDevName,
							ai8MMULevelStringLookup[eMMULevel],
							ui32Size,
							ui32Align,
							psDevPAddr->uiAddr);
	if(eErr != PVRSRV_OK)
	{
		goto ErrOut;
	}

	PDumpOSLock();
	PDumpWriteScript(hScript, ui32Flags);

	/*
		construct the symbolic address
	*/
	ui64SymbolicAddr = (IMG_UINT64)psDevPAddr->uiAddr;

	/*
		Write to the MMU script stream indicating the memory allocation
	*/
	pszMMUPX = MMUPX_FMT(eMMULevel);
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "MALLOC :%s:%s%016llX 0x%X 0x%X",
											pszPDumpDevName,
											pszMMUPX,
											ui64SymbolicAddr,
											ui32Size,
											ui32Align
											/* don't need this sDevPAddr.uiAddr*/);
	if(eErr != PVRSRV_OK)
	{
		goto ErrUnlock;
	}
	PDumpWriteScript(hScript, ui32Flags);

ErrUnlock:
	PDumpOSUnlock();
ErrOut:
	return eErr;
}


/**************************************************************************
 * Function Name  : PDumpMMUFree
 * Inputs         :
 * Outputs        : 
 * Returns        : PVRSRV_ERROR
 * Description    : 
**************************************************************************/
PVRSRV_ERROR PDumpMMUFree(const IMG_CHAR				*pszPDumpDevName,
							MMU_LEVEL 					eMMULevel,
							IMG_DEV_PHYADDR				*psDevPAddr)
{
	PVRSRV_ERROR eErr;
	IMG_UINT64 ui64SymbolicAddr;
	IMG_UINT32 ui32Flags = PDUMP_FLAGS_CONTINUOUS;
	IMG_CHAR *pszMMUPX;

	PDUMP_GET_SCRIPT_STRING();

	ui32Flags |= ( PDumpIsPersistent() ) ? PDUMP_FLAGS_PERSISTENT : 0;

	if (eMMULevel >= MMU_LEVEL_LAST)
	{
		eErr = PVRSRV_ERROR_INVALID_PARAMS;
		goto ErrOut;
	}

	/*
		Write a comment to the PDUMP2 script streams indicating the memory free
	*/
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "-- FREE :%s:%s", 
							pszPDumpDevName, ai8MMULevelStringLookup[eMMULevel]);
	if(eErr != PVRSRV_OK)
	{
		goto ErrOut;
	}

	PDumpOSLock();
	PDumpWriteScript(hScript, ui32Flags);

	/*
		construct the symbolic address
	*/
	ui64SymbolicAddr = (IMG_UINT64)psDevPAddr->uiAddr;

	/*
		Write to the MMU script stream indicating the memory free
	*/
	pszMMUPX = MMUPX_FMT(eMMULevel);
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "FREE :%s:%s%016llX",
							pszPDumpDevName,
							pszMMUPX,
							ui64SymbolicAddr);
	if(eErr != PVRSRV_OK)
	{
		goto ErrUnlock;
	}
	PDumpWriteScript(hScript, ui32Flags);

ErrUnlock:
	PDumpOSUnlock();
ErrOut:
	return eErr;
}


/**************************************************************************
 * Function Name  : PDumpMMUMalloc2
 * Inputs         :
 * Outputs        : 
 * Returns        : PVRSRV_ERROR
 * Description    : 
**************************************************************************/
PVRSRV_ERROR PDumpMMUMalloc2(const IMG_CHAR			*pszPDumpDevName,
							const IMG_CHAR			*pszTableType,/* PAGE_CATALOGUE, PAGE_DIRECTORY, PAGE_TABLE */
                             const IMG_CHAR *pszSymbolicAddr,
                             IMG_UINT32				ui32Size,
                             IMG_UINT32				ui32Align)
{
	PVRSRV_ERROR eErr;
	IMG_UINT32 ui32Flags = PDUMP_FLAGS_CONTINUOUS;

	PDUMP_GET_SCRIPT_STRING();

	ui32Flags |= ( PDumpIsPersistent() ) ? PDUMP_FLAGS_PERSISTENT : 0;

	/*
		Write a comment to the PDump2 script streams indicating the memory allocation
	*/
	eErr = PDumpOSBufprintf(hScript,
							ui32MaxLen,
							"-- MALLOC :%s:%s Size=0x%08X Alignment=0x%08X\n",
							pszPDumpDevName,
							pszTableType,
							ui32Size,
							ui32Align);
	if(eErr != PVRSRV_OK)
	{
		goto ErrOut;
	}

	PDumpOSLock();
	PDumpWriteScript(hScript, ui32Flags);

	/*
		Write to the MMU script stream indicating the memory allocation
	*/
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "MALLOC :%s:%s 0x%X 0x%X\n",
											pszPDumpDevName,
											pszSymbolicAddr,
											ui32Size,
											ui32Align
											/* don't need this sDevPAddr.uiAddr*/);
	if(eErr != PVRSRV_OK)
	{
		goto ErrUnlock;
	}
	PDumpWriteScript(hScript, ui32Flags);

ErrUnlock:
	PDumpOSUnlock();
ErrOut:
	return eErr;
}


/**************************************************************************
 * Function Name  : PDumpMMUFree2
 * Inputs         :
 * Outputs        : 
 * Returns        : PVRSRV_ERROR
 * Description    : 
**************************************************************************/
PVRSRV_ERROR PDumpMMUFree2(const IMG_CHAR				*pszPDumpDevName,
							const IMG_CHAR				*pszTableType,/* PAGE_CATALOGUE, PAGE_DIRECTORY, PAGE_TABLE */
                           const IMG_CHAR *pszSymbolicAddr)
{
	PVRSRV_ERROR eErr;
	IMG_UINT32 ui32Flags = PDUMP_FLAGS_CONTINUOUS;

	PDUMP_GET_SCRIPT_STRING();

	ui32Flags |= ( PDumpIsPersistent() ) ? PDUMP_FLAGS_PERSISTENT : 0;

	/*
		Write a comment to the PDUMP2 script streams indicating the memory free
	*/
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "-- FREE :%s:%s\n", 
							pszPDumpDevName, pszTableType);
	if(eErr != PVRSRV_OK)
	{
		goto ErrOut;
	}

	PDumpOSLock();
	PDumpWriteScript(hScript, ui32Flags);

	/*
		Write to the MMU script stream indicating the memory free
	*/
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "FREE :%s:%s\n",
                            pszPDumpDevName,
							pszSymbolicAddr);
	if(eErr != PVRSRV_OK)
	{
		goto ErrUnlock;
	}
	PDumpWriteScript(hScript, ui32Flags);

ErrUnlock:
	PDumpOSUnlock();
ErrOut:
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
                                   IMG_VOID *pvPxMem,
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
                                   IMG_UINT32 ui32Flags)
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
    IMG_CHAR   *pszWrwSuffix = 0;
    IMG_VOID *pvRawBytes = 0;
    IMG_CHAR aszPxSymbolicAddr[MAX_SYMBOLIC_ADDRESS_LENGTH];
    IMG_UINT64 ui64PxE64;
    IMG_UINT64 ui64Protflags64;
    IMG_CHAR *pszMMUPX;

	PDUMP_GET_SCRIPT_STRING();

	ui32Flags |= ( PDumpIsPersistent() ) ? PDUMP_FLAGS_PERSISTENT : 0;

	if (!PDumpReady())
	{
		eErr = PVRSRV_ERROR_PDUMP_NOT_AVAILABLE;
		goto ErrOut;
	}


	if (PDumpIsDumpSuspended())
	{
		eErr = PVRSRV_OK;
		goto ErrOut;
	}

    if (pvPxMem == IMG_NULL)
    {
        PVR_DPF((PVR_DBG_ERROR, "PDUMPMMUDUMPPxENTRIES: PxMem is Null"));
        eErr = PVRSRV_ERROR_INVALID_PARAMS;
        goto ErrOut;
    }


	/*
		create the symbolic address of the Px
	*/
	ui64PxSymAddr = sPxDevPAddr.uiAddr;

	pszMMUPX = MMUPX_FMT(eMMULevel);
    OSSNPrintf(aszPxSymbolicAddr,
               MAX_SYMBOLIC_ADDRESS_LENGTH,
               ":%s:%s%016llX",
               pszPDumpDevName,
               pszMMUPX,
               ui64PxSymAddr);

    PDumpOSLock();

	/*
		traverse PxEs, dumping entries
	*/
	for(uiPxEIdx = uiFirstEntry;
        uiPxEIdx < uiFirstEntry + uiNumEntries;
        uiPxEIdx++)
	{
		/* Calc the symbolic address offset of the PxE location
		   This is what we have to add to the table address to get to a certain entry */
		ui32SymAddrOffset = (uiPxEIdx*uiBytesPerEntry);

		/* Calc the symbolic address of the PxE value and HW protflags */
		/* just read it here */
		switch(uiBytesPerEntry)
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

        bPxEValid = (ui64Protflags64 & 1) ? IMG_TRUE : IMG_FALSE;

        if(bPxEValid)
        {
            _ContiguousPDumpBytes(aszPxSymbolicAddr, ui32SymAddrOffset, IMG_TRUE,
                                  0, 0,
                                  ui32Flags | PDUMP_FLAGS_CONTINUOUS);

            iShiftAmount = (IMG_INT32)(uiLog2Align - uiAddrShift);

            /* First put the symbolic representation of the actual
               address of the entry into a pdump internal register */
            /* MOV seemed cleaner here, since (a) it's 64-bit; (b) the
               target is not memory.  However, MOV cannot do the
               "reference" of the symbolic address.  Apparently WRW is
               correct. */

			if (pszSymbolicAddr == IMG_NULL)
			{
				pszSymbolicAddr = "none";
			}

            if (eMMULevel == MMU_LEVEL_1)
            {
             	if (iShiftAmount == 0)
			    {
             		eErr = PDumpOSBufprintf(hScript,
											ui32MaxLen,
											"WRW%s :%s:%s%016llX:0x%08X :%s:%s:0x%llx | 0x%llX\n",
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
                	eErr = PDumpOSBufprintf(hScript,
					                        ui32MaxLen,
					                        "WRW :%s:$1 :%s:%s:0x%llx\n",
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
            	pszMMUPX = MMUPX_FMT(eMMULevel-1);
            	eErr = PDumpOSBufprintf(hScript,
                                    ui32MaxLen,
                                    "WRW :%s:$1 :%s:%s%016llX:0x0",
                                    /* dest */
                                    pszPDumpDevName,
                                    /* src */
                                    pszPDumpDevName,
                                    pszMMUPX,
                                    ui64PxEValueSymAddr);
            	pszMMUPX = MMUPX_FMT(eMMULevel);
            }
            if (eErr != PVRSRV_OK)
            {
                goto ErrUnlock;
            }
            PDumpWriteScript(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);

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
                   than the position in the PxE would suggest.  */
                eErr = PDumpOSBufprintf(hScript,
                                        ui32MaxLen,
                                        "SHR :%s:$1 :%s:$1 0x%X",
                                        /* dest */
                                        pszPDumpDevName,
                                        /* src A */
                                        pszPDumpDevName,
                                        /* src B */
                                        iShiftAmount);
                if (eErr != PVRSRV_OK)
                {
                    goto ErrUnlock;
                }
                PDumpWriteScript(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);
            }
            else if (iShiftAmount < 0)
            {
                /* Page X Address is specified in units smaller
                   than the position in the PxE would suggest.  */
                eErr = PDumpOSBufprintf(hScript,
                                        ui32MaxLen,
                                        "SHL :%s:$1 :%s:$1 0x%X",
                                        /* dest */
                                        pszPDumpDevName,
                                        /* src A */
                                        pszPDumpDevName,
                                        /* src B */
                                        -iShiftAmount);
                if (eErr != PVRSRV_OK)
                {
                    goto ErrUnlock;
                }
                PDumpWriteScript(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);
            }

            if (eMMULevel == MMU_LEVEL_1)
            {
            	if( iShiftAmount != 0)
            	{
					eErr = PDumpOSBufprintf(hScript,
											ui32MaxLen,
											"WRW%s :%s:%s%016llX:0x%08X :%s:$1  | 0x%llX",
											pszWrwSuffix,
											/* dest */
											pszPDumpDevName,
											pszMMUPX,
											ui64PxSymAddr,
											ui32SymAddrOffset,
											/* src */
											pszPDumpDevName,
											/* ORing prot flags */
											ui64Protflags64);
					if(eErr != PVRSRV_OK)
					{
						goto ErrUnlock;
					}
					PDumpWriteScript(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);
            	}
             }
            else
            {
            	/* Now we can "or" in the protection flags */
            	eErr = PDumpOSBufprintf(hScript,
                                    	ui32MaxLen,
                                    	"OR :%s:$1 :%s:$1 0x%llX",
                                    	/* dest */
                                    	pszPDumpDevName,
                                    	/* src A */
                                    	pszPDumpDevName,
                                    	/* src B */
                                        ui64Protflags64);
            	if (eErr != PVRSRV_OK)
            	{
                	goto ErrUnlock;
            	}
                PDumpWriteScript(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);

                /* Finally, we write the register into the actual PxE */
            	eErr = PDumpOSBufprintf(hScript,
                                        ui32MaxLen,
                                        "WRW%s :%s:%s%016llX:0x%08X :%s:$1",
                                        pszWrwSuffix,
                                        /* dest */
                                        pszPDumpDevName,
                                        pszMMUPX,
                                        ui64PxSymAddr,
                                        ui32SymAddrOffset,
                                        /* src */
                                        pszPDumpDevName);
				if(eErr != PVRSRV_OK)
				{
					goto ErrUnlock;
				}
				PDumpWriteScript(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);
        	}
        }
        else
        {
            /* If the entry was "invalid", simply write the actual
               value found to the memory location */
            eErr = _ContiguousPDumpBytes(aszPxSymbolicAddr, ui32SymAddrOffset, IMG_FALSE,
                                         uiBytesPerEntry, pvRawBytes,
                                         ui32Flags | PDUMP_FLAGS_CONTINUOUS);
            if (eErr != PVRSRV_OK)
            {
                goto ErrUnlock;
            }
        }
	}

    /* flush out any partly accumulated stuff for LDB */
    _ContiguousPDumpBytes(aszPxSymbolicAddr, ui32SymAddrOffset, IMG_TRUE,
                          0, 0,
                          ui32Flags | PDUMP_FLAGS_CONTINUOUS);

ErrUnlock:
	PDumpOSUnlock();
ErrOut:
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
	for(i=0; i<MAX_PDUMP_MMU_CONTEXTS; i++)
	{
		if((guiPDumpMMUContextAvailabilityMask & (1U << i)))
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
	if(ui32MMUContextID < MAX_PDUMP_MMU_CONTEXTS)
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
 * Function Name  : PDumpSetMMUContext
 * Inputs         :
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Set MMU Context
**************************************************************************/
PVRSRV_ERROR PDumpMMUAllocMMUContext(const IMG_CHAR *pszPDumpMemSpaceName,
                                     IMG_DEV_PHYADDR sPCDevPAddr,
                                     PDUMP_MMU_TYPE eMMUType,
                                     IMG_UINT32 *pui32MMUContextID)
{
    IMG_UINT64 ui64PCSymAddr;
    IMG_CHAR *pszMMUPX;

	IMG_UINT32 ui32MMUContextID;
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	eErr = _PdumpAllocMMUContext(&ui32MMUContextID);
	if(eErr != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PDumpSetMMUContext: _PdumpAllocMMUContext failed: %d", eErr));
        PVR_DBG_BREAK;
		goto ErrOut;
	}

	/*
		create the symbolic address of the PC
    */
	ui64PCSymAddr = sPCDevPAddr.uiAddr;

	pszMMUPX = MMUPX_FMT(3);
	eErr = PDumpOSBufprintf(hScript,
                            ui32MaxLen, 
                            "MMU :%s:v%d %d :%s:%s%016llX",
                            /* mmu context */
                            pszPDumpMemSpaceName,
                            ui32MMUContextID,
                            /* mmu type */
                            eMMUType,
                            /* PC base address */
                            pszPDumpMemSpaceName,
                            pszMMUPX,
                            ui64PCSymAddr);
	if(eErr != PVRSRV_OK)
	{
        PVR_DBG_BREAK;
		goto ErrOut;
	}

	PDumpOSLock();
	PDumpWriteScript(hScript, PDUMP_FLAGS_CONTINUOUS);
    PDumpOSUnlock();

	/* return the MMU Context ID */
	*pui32MMUContextID = ui32MMUContextID;

ErrOut:
	return eErr;
}


/**************************************************************************
 * Function Name  : PDumpClearMMUContext
 * Inputs         :
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Clear MMU Context
**************************************************************************/
PVRSRV_ERROR PDumpMMUFreeMMUContext(const IMG_CHAR *pszPDumpMemSpaceName,
                                    IMG_UINT32 ui32MMUContextID)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	eErr = PDumpOSBufprintf(hScript,
                            ui32MaxLen,
                            "-- Clear MMU Context for memory space %s", pszPDumpMemSpaceName);
	if(eErr != PVRSRV_OK)
	{
		goto ErrOut;
	}

	PDumpOSLock();
	PDumpWriteScript(hScript, PDUMP_FLAGS_CONTINUOUS);

	eErr = PDumpOSBufprintf(hScript,
                            ui32MaxLen, 
                            "MMU :%s:v%d",
                            pszPDumpMemSpaceName,
                            ui32MMUContextID);
	if(eErr != PVRSRV_OK)
	{
		goto ErrUnlock;
	}

	PDumpWriteScript(hScript, PDUMP_FLAGS_CONTINUOUS);

	eErr = _PdumpFreeMMUContext(ui32MMUContextID);
	if(eErr != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PDumpClearMMUContext: _PdumpFreeMMUContext failed: %d", eErr));
		goto ErrUnlock;
	}

ErrUnlock:
	PDumpOSUnlock();
ErrOut:
	return eErr;
}


/**************************************************************************
 * Function Name  : PDumpMMUActivateCatalog
 * Inputs         :
 * Outputs        : 
 * Returns        : PVRSRV_ERROR
 * Description    : 
**************************************************************************/
PVRSRV_ERROR PDumpMMUActivateCatalog(const IMG_CHAR *pszPDumpRegSpaceName,
                                     const IMG_CHAR *pszPDumpRegName,
                                     IMG_UINT32 uiRegAddr,
                                     const IMG_CHAR *pszPDumpPCSymbolicName)
{
	IMG_UINT32 ui32Flags = PDUMP_FLAGS_CONTINUOUS;
	PVRSRV_ERROR eErr;

	PDUMP_GET_SCRIPT_STRING();

	ui32Flags |= ( PDumpIsPersistent() ) ? PDUMP_FLAGS_PERSISTENT : 0;

	if (!PDumpReady())
	{
		return PVRSRV_ERROR_PDUMP_NOT_AVAILABLE;
	}


	if (PDumpIsDumpSuspended())
	{
		return PVRSRV_OK;
	}

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen,
							"-- Write Page Catalogue Address to %s",
							pszPDumpRegName);
	if(eErr != PVRSRV_OK)
	{
		goto ErrOut;
	}

	PDumpOSLock();
	PDumpWriteScript(hScript, ui32Flags);

    eErr = PDumpOSBufprintf(hScript,
                            ui32MaxLen,
                            "WRW :%s:0x%04X %s:0",
                            /* dest */
                            pszPDumpRegSpaceName,
                            uiRegAddr,
                            /* src */
                            pszPDumpPCSymbolicName);
    if (eErr != PVRSRV_OK)
    {
        goto ErrUnlock;
    }
    PDumpWriteScript(hScript, ui32Flags | PDUMP_FLAGS_CONTINUOUS);

ErrUnlock:
	PDumpOSUnlock();
ErrOut:
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
    PVRSRV_ERROR eError;

    //							"SAB :%s:v%x:0x%010llX 0x%08X 0x%08X %s.bin",

	PDUMP_GET_SCRIPT_STRING();

	ui32PDumpFlags |= ( PDumpIsPersistent() ) ? PDUMP_FLAGS_PERSISTENT : 0;

	if (!PDumpReady())
	{
		eError = PVRSRV_ERROR_PDUMP_NOT_AVAILABLE;
		goto ErrOut;
	}


	if (PDumpIsDumpSuspended())
	{
		eError = PVRSRV_OK;
		goto ErrOut;
	}

    eError = PDumpOSBufprintf(hScript,
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
    PVR_ASSERT(eError == PVRSRV_OK);
    PDumpOSLock();
    PDumpWriteScript(hScript, ui32PDumpFlags);
    PDumpOSUnlock();

ErrOut:
    return eError;
}

#endif /* #if defined (PDUMP) */

