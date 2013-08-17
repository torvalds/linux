/*************************************************************************/ /*!
@Title          Memory debugging routines.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Adds extra memory to the allocations to trace the memory bounds
                and other runtime information.
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

#ifndef MEM_DEBUG_C
#define MEM_DEBUG_C

#if defined(PVRSRV_DEBUG_OS_MEMORY)

#include "img_types.h"
#include "services_headers.h"

#if defined (__cplusplus)
extern "C"
{
#endif

#define STOP_ON_ERROR 0

	/*
	 Allocated Memory Layout:
	 
	 ---------                     \
	 Status    [OSMEM_DEBUG_INFO]   |- TEST_BUFFER_PADDING_STATUS
	 ---------                     <
	 [0xBB]*   [raw bytes]          |- ui32Size 
	 ---------                     <
	 [0xB2]*   [raw bytes]          |- TEST_BUFFER_PADDING_AFTER
	 ---------                     /
	*/

	IMG_BOOL MemCheck(const IMG_PVOID pvAddr, const IMG_UINT8 ui8Pattern, IMG_SIZE_T uSize)
	{
		IMG_UINT8 *pui8Addr;
		for (pui8Addr = (IMG_UINT8*)pvAddr; uSize > 0; uSize--, pui8Addr++)
		{
			if (*pui8Addr != ui8Pattern)
			{
				return IMG_FALSE;
			}
		}
		return IMG_TRUE;
	}

	/*
	This function expects the pointer to the user data, not the debug data.
	*/
	IMG_VOID OSCheckMemDebug(IMG_PVOID pvCpuVAddr, IMG_SIZE_T uSize, const IMG_CHAR *pszFileName, const IMG_UINT32 uLine)
	{
		OSMEM_DEBUG_INFO const *psInfo = (OSMEM_DEBUG_INFO *)((IMG_UINTPTR_T)pvCpuVAddr - TEST_BUFFER_PADDING_STATUS);

		/* invalid pointer */
		if (pvCpuVAddr == IMG_NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "Pointer 0x%p : null pointer"
					 " - referenced %s:%d - allocated %s:%d",
					 pvCpuVAddr,
					 pszFileName, uLine,
					 psInfo->sFileName, psInfo->uLineNo));
			while (STOP_ON_ERROR);
		}

		/* align */
		if (((IMG_UINT32)pvCpuVAddr&3) != 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "Pointer 0x%p : invalid alignment"
					 " - referenced %s:%d - allocated %s:%d",
					 pvCpuVAddr,
					 pszFileName, uLine,
					 psInfo->sFileName, psInfo->uLineNo));
			while (STOP_ON_ERROR);
		}

		/*check guard region before*/
		if (!MemCheck((IMG_PVOID)psInfo->sGuardRegionBefore, 0xB1, sizeof(psInfo->sGuardRegionBefore)))
		{
			PVR_DPF((PVR_DBG_ERROR, "Pointer 0x%p : guard region before overwritten"
					 " - referenced %s:%d - allocated %s:%d",
					 pvCpuVAddr,
					 pszFileName, uLine,
					 psInfo->sFileName, psInfo->uLineNo));
			while (STOP_ON_ERROR);
		}

		/*check size*/
		if (uSize != psInfo->uSize)
		{
			PVR_DPF((PVR_DBG_WARNING, 
					"Pointer 0x%p : supplied size was different to stored size (0x%x != 0x%X) - referenced %s:%d - allocated %s:%d",
					 pvCpuVAddr, uSize, psInfo->uSize,
					 pszFileName, uLine,
					 psInfo->sFileName, psInfo->uLineNo));
			while (STOP_ON_ERROR);
		}

		/*check size parity*/
		if ((0x01234567 ^ psInfo->uSizeParityCheck) != psInfo->uSize)
		{
			PVR_DPF((PVR_DBG_WARNING, 
					"Pointer 0x%p : stored size parity error (0x%X != 0x%X) \
					  - referenced %s:%d - allocated %s:%d",
					 pvCpuVAddr, psInfo->uSize, 0x01234567 ^ psInfo->uSizeParityCheck,
					 pszFileName, uLine,
					 psInfo->sFileName, psInfo->uLineNo));
			while (STOP_ON_ERROR);
		}
		else
		{
			/*the stored size is ok, so we use it instead the supplied uSize*/
			uSize = psInfo->uSize;
		}

		/*check padding after*/
		if (uSize)
		{
			if (!MemCheck((IMG_VOID*)((IMG_UINTPTR_T)pvCpuVAddr + uSize), 0xB2, TEST_BUFFER_PADDING_AFTER))
			{
				PVR_DPF((PVR_DBG_ERROR, "Pointer 0x%p : guard region after overwritten"
						 " - referenced from %s:%d - allocated from %s:%d",
						 pvCpuVAddr,
						 pszFileName, uLine,
						 psInfo->sFileName, psInfo->uLineNo));
			}
		}

		/* allocated... */
		if (psInfo->eValid != isAllocated)
		{
			PVR_DPF((PVR_DBG_ERROR, "Pointer 0x%p : not allocated (freed? %d)"
					 " - referenced %s:%d - freed %s:%d",
					 pvCpuVAddr, psInfo->eValid == isFree,
					 pszFileName, uLine,
					 psInfo->sFileName, psInfo->uLineNo));
			while (STOP_ON_ERROR);
		}
	}

	IMG_VOID debug_strcpy(IMG_CHAR *pDest, const IMG_CHAR *pSrc)
	{
		IMG_SIZE_T i = 0;

		for (; i < 128; i++) /*changed to 128 to match the filename array size*/
		{
			*pDest = *pSrc;
			if (*pSrc == '\0') break;
			pDest++;
			pSrc++;
		}
	}

	PVRSRV_ERROR OSAllocMem_Debug_Wrapper(IMG_UINT32 ui32Flags,
										  IMG_UINT32 ui32Size,
										  IMG_PVOID *ppvCpuVAddr,
										  IMG_HANDLE *phBlockAlloc,
										  IMG_CHAR *pszFilename,
										  IMG_UINT32 ui32Line)
	{
		OSMEM_DEBUG_INFO *psInfo;

		PVRSRV_ERROR eError;

		eError = OSAllocMem_Debug_Linux_Memory_Allocations(ui32Flags,
				 ui32Size + TEST_BUFFER_PADDING,
				 ppvCpuVAddr,
				 phBlockAlloc,
				 pszFilename,
				 ui32Line);

		if (eError != PVRSRV_OK)
		{
			return eError;
		}

		OSMemSet((IMG_CHAR *)(*ppvCpuVAddr) + TEST_BUFFER_PADDING_STATUS, 0xBB, ui32Size);
		OSMemSet((IMG_CHAR *)(*ppvCpuVAddr) + ui32Size + TEST_BUFFER_PADDING_STATUS, 0xB2, TEST_BUFFER_PADDING_AFTER);

		/*fill the dbg info struct*/
		psInfo = (OSMEM_DEBUG_INFO *)(*ppvCpuVAddr);

		OSMemSet(psInfo->sGuardRegionBefore, 0xB1, sizeof(psInfo->sGuardRegionBefore));
		debug_strcpy(psInfo->sFileName, pszFilename);
		psInfo->uLineNo = ui32Line;
		psInfo->eValid = isAllocated;
		psInfo->uSize = ui32Size;
		psInfo->uSizeParityCheck = 0x01234567 ^ ui32Size;

		/*point to the user data section*/
		*ppvCpuVAddr = (IMG_PVOID) ((IMG_UINTPTR_T)*ppvCpuVAddr)+TEST_BUFFER_PADDING_STATUS;

#ifdef PVRSRV_LOG_MEMORY_ALLOCS
		/*this is here to simplify the surounding logging macro, that is a expression
		maybe the macro should be an expression */
		PVR_TRACE(("Allocated pointer (after debug info): 0x%p from %s:%d", *ppvCpuVAddr, pszFilename, ui32Line));
#endif

		return PVRSRV_OK;
	}

	PVRSRV_ERROR OSFreeMem_Debug_Wrapper(IMG_UINT32 ui32Flags,
										 IMG_UINT32 ui32Size,
										 IMG_PVOID pvCpuVAddr,
										 IMG_HANDLE hBlockAlloc,
										 IMG_CHAR *pszFilename,
										 IMG_UINT32 ui32Line)
	{
		OSMEM_DEBUG_INFO *psInfo;

		/*check dbginfo (arg pointing to user memory)*/
		OSCheckMemDebug(pvCpuVAddr, ui32Size, pszFilename, ui32Line);

		/*mark memory as freed*/
		OSMemSet(pvCpuVAddr, 0xBF, ui32Size + TEST_BUFFER_PADDING_AFTER);

		/*point to the starting address of the total allocated memory*/
		psInfo = (OSMEM_DEBUG_INFO *)((IMG_UINTPTR_T) pvCpuVAddr - TEST_BUFFER_PADDING_STATUS);

		/*update dbg info struct*/
		psInfo->uSize = 0;
		psInfo->uSizeParityCheck = 0;
		psInfo->eValid = isFree;
		psInfo->uLineNo = ui32Line;
		debug_strcpy(psInfo->sFileName, pszFilename);

		return OSFreeMem_Debug_Linux_Memory_Allocations(ui32Flags, ui32Size + TEST_BUFFER_PADDING, psInfo, hBlockAlloc, pszFilename, ui32Line);
	}

#if defined (__cplusplus)

}
#endif

#endif /* PVRSRV_DEBUG_OS_MEMORY */

#endif /* MEM_DEBUG_C */
