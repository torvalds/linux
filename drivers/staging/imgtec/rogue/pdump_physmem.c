/*************************************************************************/ /*!
@File
@Title		Physmem PDump functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Common PDump (PMR specific) functions
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

#if defined(LINUX)
#include <linux/ctype.h>
#else
#include <ctype.h>
#endif

#include "img_types.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"

#include "pdump_physmem.h"
#include "pdump_osfunc.h"
#include "pdump_km.h"

#include "allocmem.h"
#include "osfunc.h"

/* #define MAX_PDUMP_MMU_CONTEXTS	(10) */
/* static IMG_UINT32 guiPDumpMMUContextAvailabilityMask = (1<<MAX_PDUMP_MMU_CONTEXTS)-1; */


struct _PDUMP_PHYSMEM_INFO_T_
{
    IMG_CHAR aszSymbolicAddress[PHYSMEM_PDUMP_MEMSPNAME_SYMB_ADDR_MAX_LENGTH];
    IMG_UINT64 ui64Size;
    IMG_UINT32 ui32Align;
    IMG_UINT32 ui32SerialNum;
};

static IMG_BOOL _IsAllowedSym(IMG_CHAR sym)
{
	/* Numbers, Characters or '_' are allowed */
	if (isalnum(sym) || sym == '_')
		return IMG_TRUE;
	else
		return IMG_FALSE;
}

static IMG_BOOL _IsLowerCaseSym(IMG_CHAR sym)
{
	if (sym >= 'a' && sym <= 'z')
		return IMG_TRUE;
	else
		return IMG_FALSE;
}

void PDumpMakeStringValid(IMG_CHAR *pszString,
                          IMG_UINT32 ui32StrLen)
{
	IMG_UINT32 i;
	for (i = 0; i < ui32StrLen; i++)
	{
		if (_IsAllowedSym(pszString[i]))
		{
			if (_IsLowerCaseSym(pszString[i]))
				pszString[i] = pszString[i]-32;
			else
				pszString[i] = pszString[i];
		}
		else
		{
			pszString[i] = '_';
		}
	}
}

/**************************************************************************
 * Function Name  : PDumpMalloc
 * Inputs         :
 * Outputs        :
 * Returns        : PVRSRV_ERROR
 * Description    :
**************************************************************************/
PVRSRV_ERROR PDumpMalloc(const IMG_CHAR *pszDevSpace,
                            const IMG_CHAR *pszSymbolicAddress,
                            IMG_UINT64 ui64Size,
                            IMG_DEVMEM_ALIGN_T uiAlign,
                            IMG_BOOL bInitialise,
                            IMG_UINT32 ui32InitValue,
                            IMG_BOOL bForcePersistent,
                            IMG_HANDLE *phHandlePtr)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32Flags = PDUMP_FLAGS_CONTINUOUS;

	PDUMP_PHYSMEM_INFO_T *psPDumpAllocationInfo;

	PDUMP_GET_SCRIPT_STRING()

    psPDumpAllocationInfo = OSAllocMem(sizeof*psPDumpAllocationInfo);
    PVR_ASSERT(psPDumpAllocationInfo != NULL);

	if (bForcePersistent)
	{
		ui32Flags |= PDUMP_FLAGS_PERSISTENT;
	}

	/*
		construct the symbolic address
	*/

    OSSNPrintf(psPDumpAllocationInfo->aszSymbolicAddress,
               sizeof(psPDumpAllocationInfo->aszSymbolicAddress)+sizeof(pszDevSpace),
               ":%s:%s",
               pszDevSpace,
               pszSymbolicAddress);

	/*
		Write to the MMU script stream indicating the memory allocation
	*/
	PDUMP_LOCK();
	if (bInitialise)
	{
		eError = PDumpOSBufprintf(hScript, ui32MaxLen, "CALLOC %s 0x%llX 0x%llX 0x%X\n",
								psPDumpAllocationInfo->aszSymbolicAddress,
								ui64Size,
								uiAlign,
								ui32InitValue);
	}
	else
	{
		eError = PDumpOSBufprintf(hScript, ui32MaxLen, "MALLOC %s 0x%llX 0x%llX\n",
								psPDumpAllocationInfo->aszSymbolicAddress,
								ui64Size,
								uiAlign);
	}

	if(eError != PVRSRV_OK)
	{
		OSFreeMem(psPDumpAllocationInfo);
		goto _return;
	}

	PDumpWriteScript(hScript, ui32Flags);

    psPDumpAllocationInfo->ui64Size = ui64Size;
    psPDumpAllocationInfo->ui32Align = TRUNCATE_64BITS_TO_32BITS(uiAlign);

    *phHandlePtr = (IMG_HANDLE)psPDumpAllocationInfo;

_return:
   	PDUMP_UNLOCK();
    return eError;
}


/**************************************************************************
 * Function Name  : PDumpFree
 * Inputs         :
 * Outputs        :
 * Returns        : PVRSRV_ERROR
 * Description    :
**************************************************************************/
PVRSRV_ERROR PDumpFree(IMG_HANDLE hPDumpAllocationInfoHandle)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32Flags = PDUMP_FLAGS_CONTINUOUS;

    PDUMP_PHYSMEM_INFO_T *psPDumpAllocationInfo;

	PDUMP_GET_SCRIPT_STRING()

    psPDumpAllocationInfo = (PDUMP_PHYSMEM_INFO_T *)hPDumpAllocationInfoHandle;

	/*
		Write to the MMU script stream indicating the memory free
	*/
	PDUMP_LOCK();
	eError = PDumpOSBufprintf(hScript, ui32MaxLen, "FREE %s\n",
                              psPDumpAllocationInfo->aszSymbolicAddress);
	if(eError != PVRSRV_OK)
	{
		goto _return;
	}

	PDumpWriteScript(hScript, ui32Flags);
    OSFreeMem(psPDumpAllocationInfo);

_return:
	PDUMP_UNLOCK();
	return eError;
}

PVRSRV_ERROR
PDumpPMRWRW32(const IMG_CHAR *pszDevSpace,
            const IMG_CHAR *pszSymbolicName,
            IMG_DEVMEM_OFFSET_T uiOffset,
            IMG_UINT32 ui32Value,
            PDUMP_FLAGS_T uiPDumpFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PDUMP_GET_SCRIPT_STRING()

	PDUMP_LOCK();
	eError = PDumpOSBufprintf(hScript,
                              ui32MaxLen,
                              "WRW :%s:%s:" IMG_DEVMEM_OFFSET_FMTSPEC " "
                              PMR_VALUE32_FMTSPEC " ",
                              pszDevSpace,
                              pszSymbolicName,
                              uiOffset,
                              ui32Value);
	if(eError != PVRSRV_OK)
	{
		goto _return;
	}

	PDumpWriteScript(hScript, uiPDumpFlags);

_return:
	PDUMP_UNLOCK();
	return eError;
}

PVRSRV_ERROR
PDumpPMRWRW64(const IMG_CHAR *pszDevSpace,
            const IMG_CHAR *pszSymbolicName,
            IMG_DEVMEM_OFFSET_T uiOffset,
            IMG_UINT64 ui64Value,
            PDUMP_FLAGS_T uiPDumpFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PDUMP_GET_SCRIPT_STRING()

	PDUMP_LOCK();
	eError = PDumpOSBufprintf(hScript,
                              ui32MaxLen,
                              "WRW64 :%s:%s:" IMG_DEVMEM_OFFSET_FMTSPEC " "
                              PMR_VALUE64_FMTSPEC " ",
                              pszDevSpace,
                              pszSymbolicName,
                              uiOffset,
                              ui64Value);
	if(eError != PVRSRV_OK)
	{
		goto _return;
	}

	PDumpWriteScript(hScript, uiPDumpFlags);

_return:
	PDUMP_UNLOCK();
	return eError;
}

PVRSRV_ERROR
PDumpPMRLDB(const IMG_CHAR *pszDevSpace,
            const IMG_CHAR *pszSymbolicName,
            IMG_DEVMEM_OFFSET_T uiOffset,
            IMG_DEVMEM_SIZE_T uiSize,
            const IMG_CHAR *pszFilename,
            IMG_UINT32 uiFileOffset,
            PDUMP_FLAGS_T uiPDumpFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PDUMP_GET_SCRIPT_STRING()

	PDUMP_LOCK();
	eError = PDumpOSBufprintf(hScript,
                              ui32MaxLen,
                              "LDB :%s:%s:" IMG_DEVMEM_OFFSET_FMTSPEC " "
                              IMG_DEVMEM_SIZE_FMTSPEC " "
                              PDUMP_FILEOFFSET_FMTSPEC " %s\n",
                              pszDevSpace,
                              pszSymbolicName,
                              uiOffset,
                              uiSize,
                              uiFileOffset,
                              pszFilename);
	if(eError != PVRSRV_OK)
	{
		goto _return;
	}

	PDumpWriteScript(hScript, uiPDumpFlags);

_return:
	PDUMP_UNLOCK();
	return eError;
}

PVRSRV_ERROR PDumpPMRSAB(const IMG_CHAR *pszDevSpace,
                         const IMG_CHAR *pszSymbolicName,
                         IMG_DEVMEM_OFFSET_T uiOffset,
                         IMG_DEVMEM_SIZE_T uiSize,
                         const IMG_CHAR *pszFileName,
                         IMG_UINT32 uiFileOffset)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 uiPDumpFlags;

	PDUMP_GET_SCRIPT_STRING()

	uiPDumpFlags = 0;

	PDUMP_LOCK();
	eError = PDumpOSBufprintf(hScript,
                              ui32MaxLen,
                              "SAB :%s:%s:" IMG_DEVMEM_OFFSET_FMTSPEC " "
                              IMG_DEVMEM_SIZE_FMTSPEC " "
                              "0x%08X %s.bin\n",
                              pszDevSpace,
                              pszSymbolicName,
                              uiOffset,
                              uiSize,
                              uiFileOffset,
                              pszFileName);
	if(eError != PVRSRV_OK)
	{
		goto _return;
	}

	PDumpWriteScript(hScript, uiPDumpFlags);

_return:
	PDUMP_UNLOCK();
	return eError;
}

PVRSRV_ERROR
PDumpPMRPOL(const IMG_CHAR *pszMemspaceName,
            const IMG_CHAR *pszSymbolicName,
            IMG_DEVMEM_OFFSET_T uiOffset,
            IMG_UINT32 ui32Value,
            IMG_UINT32 ui32Mask,
            PDUMP_POLL_OPERATOR eOperator,
            IMG_UINT32 uiCount,
            IMG_UINT32 uiDelay,
            PDUMP_FLAGS_T uiPDumpFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PDUMP_GET_SCRIPT_STRING()

	PDUMP_LOCK();
	eError = PDumpOSBufprintf(hScript,
                              ui32MaxLen,
                              "POL :%s:%s:" IMG_DEVMEM_OFFSET_FMTSPEC " "
                              "0x%08X 0x%08X %d %d %d\n",
                              pszMemspaceName,
                              pszSymbolicName,
                              uiOffset,
                              ui32Value,
                              ui32Mask,
                              eOperator,
                              uiCount,
                              uiDelay);
	if(eError != PVRSRV_OK)
	{
		goto _return;
	}

	PDumpWriteScript(hScript, uiPDumpFlags);

_return:
	PDUMP_UNLOCK();
	return eError;
}

PVRSRV_ERROR
PDumpPMRCBP(const IMG_CHAR *pszMemspaceName,
            const IMG_CHAR *pszSymbolicName,
            IMG_DEVMEM_OFFSET_T uiReadOffset,
            IMG_DEVMEM_OFFSET_T uiWriteOffset,
            IMG_DEVMEM_SIZE_T uiPacketSize,
            IMG_DEVMEM_SIZE_T uiBufferSize)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PDUMP_FLAGS_T uiPDumpFlags = 0;

	PDUMP_GET_SCRIPT_STRING()

	PDUMP_LOCK();
	eError = PDumpOSBufprintf(hScript,
                              ui32MaxLen,
                              "CBP :%s:%s:" IMG_DEVMEM_OFFSET_FMTSPEC " "
                              IMG_DEVMEM_OFFSET_FMTSPEC " " IMG_DEVMEM_SIZE_FMTSPEC " " IMG_DEVMEM_SIZE_FMTSPEC "\n",
                              pszMemspaceName,
                              pszSymbolicName,
                              uiReadOffset,
                              uiWriteOffset,
                              uiPacketSize,
                              uiBufferSize);

	if(eError != PVRSRV_OK)
	{
		goto _return;
	}

	PDumpWriteScript(hScript, uiPDumpFlags);

_return:
	PDUMP_UNLOCK();
	return eError;
}

PVRSRV_ERROR
PDumpWriteBuffer(IMG_UINT8 *pcBuffer,
                 size_t uiNumBytes,
                 PDUMP_FLAGS_T uiPDumpFlags,
                 IMG_CHAR *pszFilenameOut,
                 size_t uiFilenameBufSz,
                 PDUMP_FILEOFFSET_T *puiOffsetOut)
{
	PVRSRV_ERROR eError;
	PVR_UNREFERENCED_PARAMETER(uiFilenameBufSz);

	if (!PDumpReady())
	{
		return PVRSRV_ERROR_PDUMP_NOT_AVAILABLE;
	}

    PVR_ASSERT(uiNumBytes > 0);

	/* PRQA S 3415 1 */ /* side effects desired */
	if (PDumpIsDumpSuspended())
	{
		return PVRSRV_ERROR_PDUMP_NOT_ALLOWED;
	}

	PVR_ASSERT(uiFilenameBufSz <= PDUMP_PARAM_MAX_FILE_NAME);

	PDUMP_LOCK();

	eError = PDumpWriteParameter(pcBuffer, uiNumBytes, uiPDumpFlags, puiOffsetOut, pszFilenameOut);

	PDUMP_UNLOCK();

	if ((eError != PVRSRV_ERROR_PDUMP_NOT_ALLOWED) && (eError != PVRSRV_OK))
	{
		PVR_LOGR_IF_ERROR(eError, "PDumpWriteParameter");
	}
	/* else Write to parameter file Ok or Prevented under the flags and
	 * current state of the driver so skip further writes and let caller know.
	 */
	return eError;
}

#endif /* PDUMP */
