/*************************************************************************/ /*!
@File
@Title          Parameter dump macro target routines
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
#if defined (PDUMP)

#include <asm/atomic.h>
#include <stdarg.h>

#include "pvrversion.h"
#include "pvr_debug.h"
#include "pvrsrv.h"
#include "osfunc.h"

#include "dbgdrvif_srv5.h"
#include "mm.h"
#include "allocmem.h"
#include "pdump_km.h"
#include "pdump_osfunc.h"

#include <linux/kernel.h> // sprintf
#include <linux/string.h> // strncpy, strlen
#include <linux/mutex.h>

#define PDUMP_DATAMASTER_PIXEL		(1)
#define PDUMP_DATAMASTER_EDM		(3)

static PDBGKM_SERVICE_TABLE gpfnDbgDrv = IMG_NULL;


typedef struct PDBG_PDUMP_STATE_TAG
{
	PDBG_STREAM psStream[PDUMP_NUM_CHANNELS];

	IMG_CHAR *pszMsg;
	IMG_CHAR *pszScript;
	IMG_CHAR *pszFile;

} PDBG_PDUMP_STATE;

static PDBG_PDUMP_STATE gsDBGPdumpState = {{IMG_NULL}, IMG_NULL, IMG_NULL, IMG_NULL};

#define SZ_MSG_SIZE_MAX			PVRSRV_PDUMP_MAX_COMMENT_SIZE-1
#define SZ_SCRIPT_SIZE_MAX		PVRSRV_PDUMP_MAX_COMMENT_SIZE-1
#define SZ_FILENAME_SIZE_MAX	PVRSRV_PDUMP_MAX_COMMENT_SIZE-1

static struct mutex gsPDumpMutex;

void DBGDrvGetServiceTable(void **fn_table);


/*!
 * \name	PDumpOSGetScriptString
 */
PVRSRV_ERROR PDumpOSGetScriptString(IMG_HANDLE *phScript,
									IMG_UINT32 *pui32MaxLen)
{
	*phScript = (IMG_HANDLE)gsDBGPdumpState.pszScript;
	*pui32MaxLen = SZ_SCRIPT_SIZE_MAX;
	if (!*phScript)
	{
		return PVRSRV_ERROR_PDUMP_NOT_ACTIVE;
	}
	return PVRSRV_OK;
}

/*!
 * \name	PDumpOSGetMessageString
 */
PVRSRV_ERROR PDumpOSGetMessageString(IMG_CHAR **ppszMsg,
									 IMG_UINT32 *pui32MaxLen)
{
	*ppszMsg = gsDBGPdumpState.pszMsg;
	*pui32MaxLen = SZ_MSG_SIZE_MAX;
	if (!*ppszMsg)
	{
		return PVRSRV_ERROR_PDUMP_NOT_ACTIVE;
	}
	return PVRSRV_OK;
}

/*!
 * \name	PDumpOSGetFilenameString
 */
PVRSRV_ERROR PDumpOSGetFilenameString(IMG_CHAR **ppszFile,
									 IMG_UINT32 *pui32MaxLen)
{
	*ppszFile = gsDBGPdumpState.pszFile;
	*pui32MaxLen = SZ_FILENAME_SIZE_MAX;
	if (!*ppszFile)
	{
		return PVRSRV_ERROR_PDUMP_NOT_ACTIVE;
	}
	return PVRSRV_OK;
}

/*!
 * \name	PDumpOSBufprintf
 */
PVRSRV_ERROR PDumpOSBufprintf(IMG_HANDLE hBuf, IMG_UINT32 ui32ScriptSizeMax, IMG_CHAR* pszFormat, ...)
{
	IMG_CHAR* pszBuf = hBuf;
	IMG_INT32 n;
	va_list	vaArgs;

	va_start(vaArgs, pszFormat);

	n = vsnprintf(pszBuf, ui32ScriptSizeMax, pszFormat, vaArgs);

	va_end(vaArgs);

	if (n>=(IMG_INT32)ui32ScriptSizeMax || n==-1)	/* glibc >= 2.1 or glibc 2.0 */
	{
		PVR_DPF((PVR_DBG_ERROR, "Buffer overflow detected, pdump output may be incomplete."));

		return PVRSRV_ERROR_PDUMP_BUF_OVERFLOW;
	}

#if defined(PDUMP_DEBUG_OUTFILES)
	g_ui32EveryLineCounter++;
#endif

	/* Put line ending sequence at the end if it isn't already there */
	PDumpOSVerifyLineEnding(pszBuf, ui32ScriptSizeMax);

	return PVRSRV_OK;
}

/*!
 * \name	PDumpOSVSprintf
 */
PVRSRV_ERROR PDumpOSVSprintf(IMG_CHAR *pszComment, IMG_UINT32 ui32ScriptSizeMax, IMG_CHAR* pszFormat, PDUMP_va_list vaArgs)
{
	IMG_INT32 n;

	n = vsnprintf(pszComment, ui32ScriptSizeMax, pszFormat, vaArgs);

	if (n>=(IMG_INT32)ui32ScriptSizeMax || n==-1)	/* glibc >= 2.1 or glibc 2.0 */
	{
		PVR_DPF((PVR_DBG_ERROR, "Buffer overflow detected, pdump output may be incomplete."));

		return PVRSRV_ERROR_PDUMP_BUF_OVERFLOW;
	}

	return PVRSRV_OK;
}

/*!
 * \name	PDumpOSDebugPrintf
 */
void PDumpOSDebugPrintf(IMG_CHAR* pszFormat, ...)
{
	PVR_UNREFERENCED_PARAMETER(pszFormat);

	
}

/*!
 * \name	PDumpOSSprintf
 */
PVRSRV_ERROR PDumpOSSprintf(IMG_CHAR *pszComment, IMG_UINT32 ui32ScriptSizeMax, IMG_CHAR *pszFormat, ...)
{
	IMG_INT32 n;
	va_list	vaArgs;

	va_start(vaArgs, pszFormat);

	n = vsnprintf(pszComment, ui32ScriptSizeMax, pszFormat, vaArgs);

	va_end(vaArgs);

	if (n>=(IMG_INT32)ui32ScriptSizeMax || n==-1)	/* glibc >= 2.1 or glibc 2.0 */
	{
		PVR_DPF((PVR_DBG_ERROR, "Buffer overflow detected, pdump output may be incomplete."));

		return PVRSRV_ERROR_PDUMP_BUF_OVERFLOW;
	}

	return PVRSRV_OK;
}

/*!
 * \name	PDumpOSBuflen
 */
IMG_UINT32 PDumpOSBuflen(IMG_HANDLE hBuffer, IMG_UINT32 ui32BufferSizeMax)
{
	IMG_CHAR* pszBuf = hBuffer;
	IMG_UINT32 ui32Count = 0;

	while ((pszBuf[ui32Count]!=0) && (ui32Count<ui32BufferSizeMax) )
	{
		ui32Count++;
	}
	return(ui32Count);
}

/*!
 * \name	PDumpOSVerifyLineEnding
 */
void PDumpOSVerifyLineEnding(IMG_HANDLE hBuffer, IMG_UINT32 ui32BufferSizeMax)
{
	IMG_UINT32 ui32Count;
	IMG_CHAR* pszBuf = hBuffer;

	/* strlen */
	ui32Count = PDumpOSBuflen(hBuffer, ui32BufferSizeMax);

	/* Put \n sequence at the end if it isn't already there */
	if ((ui32Count >= 1) && (pszBuf[ui32Count-1] != '\n') && (ui32Count<ui32BufferSizeMax))
	{
		pszBuf[ui32Count] = '\n';
		ui32Count++;
		pszBuf[ui32Count] = '\0';
	}
}



/*!
 * \name	PDumpOSGetStreamOffset
 */
IMG_BOOL PDumpOSSetSplitMarker(IMG_HANDLE hStream, IMG_UINT32 ui32Marker)
{
	PDBG_STREAM psStream = (PDBG_STREAM) hStream;

	PVR_ASSERT(gpfnDbgDrv);
	gpfnDbgDrv->pfnSetMarker(psStream, ui32Marker);
	return IMG_TRUE;
}

/*!
 *	\name	PDumpOSDebugDriverWrite
 */
IMG_UINT32 PDumpOSDebugDriverWrite( IMG_HANDLE psStream,
									IMG_UINT8 *pui8Data,
									IMG_UINT32 ui32BCount)
{
	PVR_ASSERT(gpfnDbgDrv != IMG_NULL);

	return gpfnDbgDrv->pfnDBGDrivWrite2(psStream, pui8Data, ui32BCount);
}

/*!
 *	\name	PDumpOSReleaseExecution
 */
void PDumpOSReleaseExecution(void)
{
	OSReleaseThreadQuanta();
}

/**************************************************************************
 * Function Name  : PDumpOSInit
 * Outputs        : None
 * Returns        :
 * Description    : Reset connection to vldbgdrv
 *					Then try to connect to PDUMP streams
**************************************************************************/
PVRSRV_ERROR PDumpOSInit(PDUMP_CHANNEL* psParam, PDUMP_CHANNEL* psScript,
		IMG_UINT32* pui32InitCapMode, IMG_CHAR** ppszEnvComment)
{
	PVRSRV_ERROR     eError;

	*pui32InitCapMode = DEBUG_CAPMODE_FRAMED;
	*ppszEnvComment = IMG_NULL;

	/* If we tried this earlier, then we might have connected to the driver
	 * But if pdump.exe was running then the stream connected would fail
	 */
	if (!gpfnDbgDrv)
	{
		DBGDrvGetServiceTable((void **)&gpfnDbgDrv);

		// If something failed then no point in trying to connect streams
		if (gpfnDbgDrv == IMG_NULL)
		{
			return PVRSRV_ERROR_PDUMP_NOT_AVAILABLE;
		}
		
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		if(!gsDBGPdumpState.pszFile)
		{
			gsDBGPdumpState.pszFile = OSAllocMem(SZ_FILENAME_SIZE_MAX);
			if (gsDBGPdumpState.pszFile == IMG_NULL)
			{
				goto init_failed;
			}
		}

		if(!gsDBGPdumpState.pszMsg)
		{
			gsDBGPdumpState.pszMsg = OSAllocMem(SZ_MSG_SIZE_MAX);
			if (gsDBGPdumpState.pszMsg == IMG_NULL)
			{
				goto init_failed;
			}
		}

		if(!gsDBGPdumpState.pszScript)
		{
			gsDBGPdumpState.pszScript = OSAllocMem(SZ_SCRIPT_SIZE_MAX);
			if (gsDBGPdumpState.pszScript == IMG_NULL)
			{
				goto init_failed;
			}
		}

		eError = PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
		if (!gpfnDbgDrv->pfnCreateStream(PDUMP_PARAM_CHANNEL_NAME, 0, 10, &psParam->hInit, &psParam->hMain, &psParam->hDeinit))
		{
			goto init_failed;
		}
		gsDBGPdumpState.psStream[PDUMP_CHANNEL_PARAM] = psParam->hMain;


		if (!gpfnDbgDrv->pfnCreateStream(PDUMP_SCRIPT_CHANNEL_NAME, 0, 10, &psScript->hInit, &psScript->hMain, &psScript->hDeinit))
		{
			goto init_failed;
		}
		gsDBGPdumpState.psStream[PDUMP_CHANNEL_SCRIPT] = psScript->hMain;
	}

	return PVRSRV_OK;

init_failed:
	PDumpOSDeInit(psParam, psScript);
	return eError;
}


void PDumpOSDeInit(PDUMP_CHANNEL* psParam, PDUMP_CHANNEL* psScript)
{
	gpfnDbgDrv->pfnDestroyStream(psScript->hInit, psScript->hMain, psScript->hDeinit);
	gpfnDbgDrv->pfnDestroyStream(psParam->hInit, psParam->hMain, psParam->hDeinit);

	if(gsDBGPdumpState.pszFile)
	{
		OSFreeMem(gsDBGPdumpState.pszFile);
		gsDBGPdumpState.pszFile = IMG_NULL;
	}

	if(gsDBGPdumpState.pszScript)
	{
		OSFreeMem(gsDBGPdumpState.pszScript);
		gsDBGPdumpState.pszScript = IMG_NULL;
	}

	if(gsDBGPdumpState.pszMsg)
	{
		OSFreeMem(gsDBGPdumpState.pszMsg);
		gsDBGPdumpState.pszMsg = IMG_NULL;
	}

	gpfnDbgDrv = IMG_NULL;
}

PVRSRV_ERROR PDumpOSCreateLock(void)
{
	mutex_init(&gsPDumpMutex);
	return PVRSRV_OK;
}

void PDumpOSDestroyLock(void)
{
	/* no destruction work to do, just assert
	 * the lock is not held */
	PVR_ASSERT(mutex_is_locked(&gsPDumpMutex) == 0);
}

void PDumpOSLock(void)
{
	mutex_lock(&gsPDumpMutex);
}

void PDumpOSUnlock(void)
{
	mutex_unlock(&gsPDumpMutex);
}

IMG_UINT32 PDumpOSGetCtrlState(IMG_HANDLE hDbgStream,
		IMG_UINT32 ui32StateID)
{
	return (gpfnDbgDrv->pfnGetCtrlState((PDBG_STREAM)hDbgStream, ui32StateID));
}

void PDumpOSSetFrame(IMG_UINT32 ui32Frame)
{
	gpfnDbgDrv->pfnSetFrame(ui32Frame);
	return;
}

IMG_BOOL PDumpOSAllowInitPhaseToComplete(IMG_UINT32 eModuleID)
{
 	return (eModuleID != IMG_PDUMPCTRL);
}

#if defined(PVR_TESTING_UTILS)
void PDumpOSDumpState(void);

void PDumpOSDumpState(void)
{
	PVR_LOG(("---- PDUMP LINUX: gpfnDbgDrv( %p )  gpfnDbgDrv.ui32Size( %d )",
			gpfnDbgDrv, gpfnDbgDrv->ui32Size));

	PVR_LOG(("---- PDUMP LINUX: gsDBGPdumpState( %p )",
			&gsDBGPdumpState));

	PVR_LOG(("---- PDUMP LINUX: gsDBGPdumpState.psStream[0]( %p )",
			gsDBGPdumpState.psStream[0]));

	(void) gpfnDbgDrv->pfnGetCtrlState(gsDBGPdumpState.psStream[0], 0xFE);

	PVR_LOG(("---- PDUMP LINUX: gsDBGPdumpState.psStream[1]( %p )",
			gsDBGPdumpState.psStream[1]));

	(void) gpfnDbgDrv->pfnGetCtrlState(gsDBGPdumpState.psStream[1], 0xFE);

	/* Now dump non-stream specific info */
	(void) gpfnDbgDrv->pfnGetCtrlState(gsDBGPdumpState.psStream[1], 0xFF);
}
#endif

#endif /* #if defined (PDUMP) */
/*****************************************************************************
 End of file (PDUMP.C)
*****************************************************************************/
