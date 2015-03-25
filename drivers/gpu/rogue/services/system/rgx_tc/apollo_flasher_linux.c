/*************************************************************************/ /*!
@File
@Title          Apollo flashing support
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Apollo flashing support for Linux
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

#include "allocmem.h"
#include "pvr_debugfs.h"
#include "apollo_flasher.h"

typedef struct _FLASH_DATA_
{
	IMG_UINT32			ui32ReadEntry;
	IMG_BOOL			bEnteredResetMode;

	struct dentry			*debugFSEntry;
	PFN_APOLLO_FLASH_INIT		pfnFlashInit;
	PFN_APOLLO_FLASH_WRITE		pfnFlashWrite;
	PFN_APOLLO_FLASH_GET_STATUS	pfnFlashGetStatus;

	IMG_VOID			*pvSysData;
} FLASH_DATA;

static ssize_t ApolloFlashWrite(const IMG_CHAR *pszData, size_t uiLength, loff_t uiOffset, IMG_VOID *pvData)
{
	FLASH_DATA *psFlashData = (FLASH_DATA *) pvData;
	IMG_UINT32 ui32FifoWriteSpace;
	FLASH_STATUS eProgramStatus;

	PVR_UNREFERENCED_PARAMETER(uiOffset);

	if (uiLength > 4)
	{
		return -EFBIG;
	}

	if (psFlashData->pfnFlashGetStatus(psFlashData->pvSysData, &ui32FifoWriteSpace, &eProgramStatus) != PVRSRV_OK)
	{
		return -EIO;
	}

	if (eProgramStatus == FLASH_STATUS_FAILED)
	{
		return -EIO;
	}

	if (!psFlashData->bEnteredResetMode)
	{
		if (eProgramStatus != FLASH_STATUS_WAITING)
		{
			return -EINVAL;
		}

		if (psFlashData->pfnFlashInit(psFlashData->pvSysData) != PVRSRV_OK)
		{
			return -EIO;
		}

		psFlashData->bEnteredResetMode = IMG_TRUE;
	}

	if (ui32FifoWriteSpace < 2)
	{
		return -ENOSPC;
	}
	else
	{
		if (psFlashData->pfnFlashWrite(psFlashData->pvSysData, (IMG_UINT32 *)pszData) != PVRSRV_OK)
		{
			return -EIO;
		}
	}

	return uiLength;
}

static const IMG_CHAR *ApolloFlashGetStatusString(FLASH_STATUS eFlashStatus)
{
	if (eFlashStatus == FLASH_STATUS_WAITING)
	{
		return "Waiting to flash!";
	}
	else if (eFlashStatus == FLASH_STATUS_IN_PROGRESS)
	{
		return "Flashing currently in progress!";
	}
	else if (eFlashStatus == FLASH_STATUS_FINISHED)
	{
		return "Flashing completed successfully!";
	}
	else if (eFlashStatus == FLASH_STATUS_FAILED)
	{
		return "Flashing failed!";
	}

	return "Error with the in kernel Apollo flashing module!";
}

static IMG_VOID *ApolloFlashReadStart(struct seq_file *psSeqFile, loff_t *puiPosition)
{
	FLASH_DATA *psFlashData = (FLASH_DATA *)psSeqFile->private;

	PVR_UNREFERENCED_PARAMETER(puiPosition);

	if (psFlashData->ui32ReadEntry == 1)
	{
		psFlashData->ui32ReadEntry = 0;
		return NULL;
	}

	return psSeqFile->private;
}

static IMG_VOID ApolloFlashReadStop(struct seq_file *psSeqFile, IMG_VOID *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psSeqFile);
	PVR_UNREFERENCED_PARAMETER(pvData);
}

static IMG_VOID *ApolloFlashReadNext(struct seq_file *psSeqFile,
				     IMG_VOID *pvData,
				     loff_t *puiPosition)
{
	PVR_UNREFERENCED_PARAMETER(psSeqFile);
	PVR_UNREFERENCED_PARAMETER(pvData);
	PVR_UNREFERENCED_PARAMETER(puiPosition);

	return NULL;
}

static int ApolloFlashReadShow(struct seq_file *psSeqFile, IMG_VOID *pvData)
{
	FLASH_DATA *psFlashData = (FLASH_DATA *)pvData;
	IMG_UINT32 ui32FifoStatus;
	IMG_UINT32 ui32ProgramStatus;

	if (psFlashData->pfnFlashGetStatus(psFlashData->pvSysData, &ui32FifoStatus, &ui32ProgramStatus) != PVRSRV_OK)
	{
		return -EIO;
	}

	psFlashData->ui32ReadEntry = 1;
	return seq_printf(psSeqFile, "%s\n", ApolloFlashGetStatusString(ui32ProgramStatus));
}

static struct seq_operations gsFlasherReadOps =
{
	.start = ApolloFlashReadStart,
	.stop = ApolloFlashReadStop,
	.next = ApolloFlashReadNext,
	.show = ApolloFlashReadShow,
};

PVRSRV_ERROR ApolloFlasherSetup(IMG_HANDLE *phFlasher,
				PFN_APOLLO_FLASH_INIT pfnFlashInit,
				PFN_APOLLO_FLASH_WRITE pfnFlashWrite,
				PFN_APOLLO_FLASH_GET_STATUS pfnFlashGetStatus,
				IMG_VOID *pvData)
{
	FLASH_DATA *psApolloFlashData;

	if (phFlasher == IMG_NULL ||
	    pfnFlashInit == IMG_NULL ||
	    pfnFlashWrite == IMG_NULL ||
	    pfnFlashGetStatus == IMG_NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psApolloFlashData = OSAllocMem(sizeof *psApolloFlashData);
	if (psApolloFlashData == IMG_NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psApolloFlashData->bEnteredResetMode = IMG_FALSE;
	psApolloFlashData->pfnFlashInit = pfnFlashInit;
	psApolloFlashData->pfnFlashWrite = pfnFlashWrite;
	psApolloFlashData->pfnFlashGetStatus = pfnFlashGetStatus;
	psApolloFlashData->pvSysData = pvData;

	if (PVRDebugFSCreateEntry("apollo_flash",
				  IMG_NULL,
				  &gsFlasherReadOps,
				  (PVRSRV_ENTRY_WRITE_FUNC *)ApolloFlashWrite,
				  (IMG_VOID *)psApolloFlashData,
				  &psApolloFlashData->debugFSEntry) < 0)
	{
		OSFreeMem(psApolloFlashData);

		return PVRSRV_ERROR_INIT_FAILURE;
	}

	*phFlasher = (IMG_HANDLE)psApolloFlashData;

	return PVRSRV_OK;
}

PVRSRV_ERROR ApolloFlasherCleanup(IMG_HANDLE hFlasher)
{
	FLASH_DATA *psFlashData = (FLASH_DATA *) hFlasher;

	if (psFlashData != IMG_NULL)
	{
		if (psFlashData->debugFSEntry != NULL)
		{
			PVRDebugFSRemoveEntry(psFlashData->debugFSEntry);
		}

		OSFreeMem(hFlasher);
	}

	return PVRSRV_OK;
}

