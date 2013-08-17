/*************************************************************************/ /*!
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

#if defined (SUPPORT_SGX) || defined (SUPPORT_VGX)
#if defined (PDUMP)

#include <asm/atomic.h>
#include <stdarg.h>
#if defined (SUPPORT_SGX)
#include "sgxdefs.h" /* Is this still needed? */
#endif
#include "services_headers.h"

#include "pvrversion.h"
#include "pvr_debug.h"

#include "dbgdrvif.h"
#if defined (SUPPORT_SGX)
#include "sgxmmu.h"/* Is this still needed? */
#endif
#include "mm.h"
#include "pdump_km.h"
#include "pdump_int.h"

#include <linux/kernel.h> // sprintf
#include <linux/string.h> // strncpy, strlen
#include <linux/mutex.h>

static IMG_BOOL PDumpWriteString2		(IMG_CHAR * pszString, IMG_UINT32 ui32Flags);
static IMG_BOOL PDumpWriteILock			(PDBG_STREAM psStream, IMG_UINT8 *pui8Data, IMG_UINT32 ui32Count, IMG_UINT32 ui32Flags);
static IMG_VOID DbgSetFrame				(PDBG_STREAM psStream, IMG_UINT32 ui32Frame);
static IMG_VOID DbgSetMarker			(PDBG_STREAM psStream, IMG_UINT32 ui32Marker);

#define PDUMP_DATAMASTER_PIXEL		(1)
#define PDUMP_DATAMASTER_EDM		(3)

/*
	Maximum file size to split output files
*/
#define MAX_FILE_SIZE	0x40000000

static atomic_t gsPDumpSuspended = ATOMIC_INIT(0);

static PDBGKM_SERVICE_TABLE gpfnDbgDrv = IMG_NULL;

DEFINE_MUTEX(sPDumpLock);
DEFINE_MUTEX(sPDumpMsgLock);

IMG_CHAR *pszStreamName[PDUMP_NUM_STREAMS] = {	"ParamStream2",
												"ScriptStream2",
												"DriverInfoStream"};
typedef struct PDBG_PDUMP_STATE_TAG
{
	PDBG_STREAM psStream[PDUMP_NUM_STREAMS];
	IMG_UINT32 ui32ParamFileNum;

	IMG_CHAR *pszMsg;
	IMG_CHAR *pszScript;
	IMG_CHAR *pszFile;

} PDBG_PDUMP_STATE;

static PDBG_PDUMP_STATE gsDBGPdumpState = {{IMG_NULL}, 0, IMG_NULL, IMG_NULL, IMG_NULL};

#define SZ_MSG_SIZE_MAX			PVRSRV_PDUMP_MAX_COMMENT_SIZE-1
#define SZ_SCRIPT_SIZE_MAX		PVRSRV_PDUMP_MAX_COMMENT_SIZE-1
#define SZ_FILENAME_SIZE_MAX	PVRSRV_PDUMP_MAX_COMMENT_SIZE-1




static inline IMG_BOOL PDumpSuspended(IMG_VOID)
{
	return (atomic_read(&gsPDumpSuspended) != 0) ? IMG_TRUE : IMG_FALSE;
}

/*!
 * \name	PDumpOSGetScriptString
 */
PVRSRV_ERROR PDumpOSGetScriptString(IMG_HANDLE *phScript,
									IMG_UINT32 *pui32MaxLen)
{
	*phScript = (IMG_HANDLE)gsDBGPdumpState.pszScript;
	*pui32MaxLen = SZ_SCRIPT_SIZE_MAX;
	if ((!*phScript) || PDumpSuspended())
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
	if ((!*ppszMsg) || PDumpSuspended())
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
	if ((!*ppszFile) || PDumpSuspended())
	{
		return PVRSRV_ERROR_PDUMP_NOT_ACTIVE;
	}
	return PVRSRV_OK;
}

/*!
 * \name	PDumpOSWriteString2
 */
IMG_BOOL PDumpOSWriteString2(IMG_HANDLE hScript, IMG_UINT32 ui32Flags)
{
	return PDumpWriteString2(hScript, ui32Flags);
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
IMG_VOID PDumpOSDebugPrintf(IMG_CHAR* pszFormat, ...)
{
	PVR_UNREFERENCED_PARAMETER(pszFormat);

	/* FIXME: Implement using services PVR_DBG or otherwise with kprintf */
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
IMG_VOID PDumpOSVerifyLineEnding(IMG_HANDLE hBuffer, IMG_UINT32 ui32BufferSizeMax)
{
	IMG_UINT32 ui32Count;
	IMG_CHAR* pszBuf = hBuffer;

	/* strlen */
	ui32Count = PDumpOSBuflen(hBuffer, ui32BufferSizeMax);

	/* Put \r \n sequence at the end if it isn't already there */
	if ((ui32Count >= 1) && (pszBuf[ui32Count-1] != '\n') && (ui32Count<ui32BufferSizeMax))
	{
		pszBuf[ui32Count] = '\n';
		ui32Count++;
		pszBuf[ui32Count] = '\0';
	}
	if ((ui32Count >= 2) && (pszBuf[ui32Count-2] != '\r') && (ui32Count<ui32BufferSizeMax))
	{
		pszBuf[ui32Count-1] = '\r';
		pszBuf[ui32Count] = '\n';
		ui32Count++;
		pszBuf[ui32Count] = '\0';
	}
}

/*!
 * \name	PDumpOSGetStream
 */
IMG_HANDLE PDumpOSGetStream(IMG_UINT32 ePDumpStream)
{
	return (IMG_HANDLE)gsDBGPdumpState.psStream[ePDumpStream];
}

/*!
 * \name	PDumpOSGetStreamOffset
 */
IMG_UINT32 PDumpOSGetStreamOffset(IMG_UINT32 ePDumpStream)
{
	PDBG_STREAM psStream = gsDBGPdumpState.psStream[ePDumpStream];
	return gpfnDbgDrv->pfnGetStreamOffset(psStream);
}

/*!
 * \name	PDumpOSGetParamFileNum
 */
IMG_UINT32 PDumpOSGetParamFileNum(IMG_VOID)
{
	return gsDBGPdumpState.ui32ParamFileNum;
}

/*!
 * \name	PDumpOSWriteString
 */
IMG_BOOL PDumpOSWriteString(IMG_HANDLE hStream,
		IMG_UINT8 *psui8Data,
		IMG_UINT32 ui32Size,
		IMG_UINT32 ui32Flags)
{
	PDBG_STREAM psStream = (PDBG_STREAM)hStream;
	return PDumpWriteILock(psStream,
					psui8Data,
					ui32Size,
					ui32Flags);
}

/*!
 * \name	PDumpOSCheckForSplitting
 */
IMG_VOID PDumpOSCheckForSplitting(IMG_HANDLE hStream, IMG_UINT32 ui32Size, IMG_UINT32 ui32Flags)
{
	/* File size limit not implemented for this OS.
	 */
	PVR_UNREFERENCED_PARAMETER(hStream);
	PVR_UNREFERENCED_PARAMETER(ui32Size);
	PVR_UNREFERENCED_PARAMETER(ui32Flags);
}

/*!
 * \name	PDumpOSJTInitialised
 */
IMG_BOOL PDumpOSJTInitialised(IMG_VOID)
{
	if(gpfnDbgDrv)
	{
		return IMG_TRUE;
	}
	return IMG_FALSE;
}

/*!
 * \name	PDumpOSIsSuspended
 */
inline IMG_BOOL PDumpOSIsSuspended(IMG_VOID)
{
	return (atomic_read(&gsPDumpSuspended) != 0) ? IMG_TRUE : IMG_FALSE;
}

/*!
 * \name	PDumpOSCPUVAddrToDevPAddr
 */
IMG_VOID PDumpOSCPUVAddrToDevPAddr(PVRSRV_DEVICE_TYPE eDeviceType,
        IMG_HANDLE hOSMemHandle,
		IMG_UINT32 ui32Offset,
		IMG_UINT8 *pui8LinAddr,
		IMG_UINT32 ui32PageSize,
		IMG_DEV_PHYADDR *psDevPAddr)
{
	IMG_CPU_PHYADDR	sCpuPAddr;

	PVR_UNREFERENCED_PARAMETER(pui8LinAddr);
	PVR_UNREFERENCED_PARAMETER(ui32PageSize);   /* for when no assert */

	/* Caller must now alway supply hOSMemHandle, even though we only (presently)
	   use it here in the linux implementation */
	   
	PVR_ASSERT (hOSMemHandle != IMG_NULL);
	
	sCpuPAddr = OSMemHandleToCpuPAddr(hOSMemHandle, ui32Offset);
	PVR_ASSERT((sCpuPAddr.uiAddr & (ui32PageSize - 1)) == 0);

	/* convert CPU physical addr to device physical */
	*psDevPAddr = SysCpuPAddrToDevPAddr(eDeviceType, sCpuPAddr);
}

/*!
 * \name	PDumpOSCPUVAddrToPhysPages
 */
IMG_VOID PDumpOSCPUVAddrToPhysPages(IMG_HANDLE hOSMemHandle,
		IMG_UINT32 ui32Offset,
		IMG_PUINT8 pui8LinAddr,
		IMG_UINTPTR_T ui32DataPageMask,
		IMG_UINT32 *pui32PageOffset)
{
	if(hOSMemHandle)
	{
		/*
		 * If a Services memory handle is provided then use it.
		 */
		IMG_CPU_PHYADDR     sCpuPAddr;

		PVR_UNREFERENCED_PARAMETER(pui8LinAddr);

		sCpuPAddr = OSMemHandleToCpuPAddr(hOSMemHandle, ui32Offset);
	    *pui32PageOffset = sCpuPAddr.uiAddr & ui32DataPageMask;
	}
	else
	{
		PVR_UNREFERENCED_PARAMETER(hOSMemHandle);
		PVR_UNREFERENCED_PARAMETER(ui32Offset);

		*pui32PageOffset = ((IMG_UINTPTR_T)pui8LinAddr & ui32DataPageMask);
	}
}

/*!
 *	\name	PDumpOSDebugDriverWrite
 */
IMG_UINT32 PDumpOSDebugDriverWrite( PDBG_STREAM psStream,
									PDUMP_DDWMODE eDbgDrvWriteMode,
									IMG_UINT8 *pui8Data,
									IMG_UINT32 ui32BCount,
									IMG_UINT32 ui32Level,
									IMG_UINT32 ui32DbgDrvFlags)
{
	switch(eDbgDrvWriteMode)
	{
		case PDUMP_WRITE_MODE_CONTINUOUS:
			PVR_UNREFERENCED_PARAMETER(ui32DbgDrvFlags);
			return gpfnDbgDrv->pfnDBGDrivWrite2(psStream, pui8Data, ui32BCount, ui32Level);
		case PDUMP_WRITE_MODE_LASTFRAME:
			return gpfnDbgDrv->pfnWriteLF(psStream, pui8Data, ui32BCount, ui32Level, ui32DbgDrvFlags);
		case PDUMP_WRITE_MODE_BINCM:
			PVR_UNREFERENCED_PARAMETER(ui32DbgDrvFlags);
			return gpfnDbgDrv->pfnWriteBINCM(psStream, pui8Data, ui32BCount, ui32Level);
		case PDUMP_WRITE_MODE_PERSISTENT:
			PVR_UNREFERENCED_PARAMETER(ui32DbgDrvFlags);
			return gpfnDbgDrv->pfnWritePersist(psStream, pui8Data, ui32BCount, ui32Level);
		default:
			PVR_UNREFERENCED_PARAMETER(ui32DbgDrvFlags);
			break;
	}
	return 0xFFFFFFFFU;
}

/*!
 *	\name	PDumpOSReleaseExecution
 */
IMG_VOID PDumpOSReleaseExecution(IMG_VOID)
{
	OSReleaseThreadQuanta();
}

/**************************************************************************
 * Function Name  : PDumpInit
 * Outputs        : None
 * Returns        :
 * Description    : Reset connection to vldbgdrv
 *					Then try to connect to PDUMP streams
**************************************************************************/
IMG_VOID PDumpInit(IMG_VOID)
{
	IMG_UINT32 i;
	DBGKM_CONNECT_NOTIFIER sConnectNotifier;

	/* If we tried this earlier, then we might have connected to the driver
	 * But if pdump.exe was running then the stream connected would fail
	 */
	if (!gpfnDbgDrv)
	{
		DBGDrvGetServiceTable(&gpfnDbgDrv);


		// If something failed then no point in trying to connect streams
		if (gpfnDbgDrv == IMG_NULL)
		{
			return;
		}
		
		/*
		 * Pass the connection notify callback
		 */
		sConnectNotifier.pfnConnectNotifier = &PDumpConnectionNotify;
		gpfnDbgDrv->pfnSetConnectNotifier(sConnectNotifier);

		if(!gsDBGPdumpState.pszFile)
		{
			if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_FILENAME_SIZE_MAX, (IMG_PVOID *)&gsDBGPdumpState.pszFile, 0,
				"Filename string") != PVRSRV_OK)
			{
				goto init_failed;
			}
		}

		if(!gsDBGPdumpState.pszMsg)
		{
			if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_MSG_SIZE_MAX, (IMG_PVOID *)&gsDBGPdumpState.pszMsg, 0,
				"Message string") != PVRSRV_OK)
			{
				goto init_failed;
			}
		}

		if(!gsDBGPdumpState.pszScript)
		{
			if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_SCRIPT_SIZE_MAX, (IMG_PVOID *)&gsDBGPdumpState.pszScript, 0,
				"Script string") != PVRSRV_OK)
			{
				goto init_failed;
			}
		}

		for(i=0; i < PDUMP_NUM_STREAMS; i++)
		{
			gsDBGPdumpState.psStream[i] = gpfnDbgDrv->pfnCreateStream(pszStreamName[i],
														DEBUG_CAPMODE_FRAMED,
														DEBUG_OUTMODE_STREAMENABLE,
														0,
														10);

			gpfnDbgDrv->pfnSetCaptureMode(gsDBGPdumpState.psStream[i],DEBUG_CAPMODE_FRAMED,0xFFFFFFFF, 0xFFFFFFFF, 1);
			gpfnDbgDrv->pfnSetFrame(gsDBGPdumpState.psStream[i],0);
		}

		PDUMPCOMMENT("Driver Product Name: %s", VS_PRODUCT_NAME);
		PDUMPCOMMENT("Driver Product Version: %s (%s)", PVRVERSION_STRING, PVRVERSION_FAMILY);
		PDUMPCOMMENT("Start of Init Phase");
	}

	return;

init_failed:

	if(gsDBGPdumpState.pszFile)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_FILENAME_SIZE_MAX, (IMG_PVOID) gsDBGPdumpState.pszFile, 0);
		gsDBGPdumpState.pszFile = IMG_NULL;
	}

	if(gsDBGPdumpState.pszScript)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_SCRIPT_SIZE_MAX, (IMG_PVOID) gsDBGPdumpState.pszScript, 0);
		gsDBGPdumpState.pszScript = IMG_NULL;
	}

	if(gsDBGPdumpState.pszMsg)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_MSG_SIZE_MAX, (IMG_PVOID) gsDBGPdumpState.pszMsg, 0);
		gsDBGPdumpState.pszMsg = IMG_NULL;
	}

	/*
	 * Remove the connection notify callback
	 */
	sConnectNotifier.pfnConnectNotifier = 0;
	gpfnDbgDrv->pfnSetConnectNotifier(sConnectNotifier);

	gpfnDbgDrv = IMG_NULL;
}


IMG_VOID PDumpDeInit(IMG_VOID)
{
	IMG_UINT32 i;
	DBGKM_CONNECT_NOTIFIER sConnectNotifier;

	for(i=0; i < PDUMP_NUM_STREAMS; i++)
	{
		gpfnDbgDrv->pfnDestroyStream(gsDBGPdumpState.psStream[i]);
	}

	if(gsDBGPdumpState.pszFile)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_FILENAME_SIZE_MAX, (IMG_PVOID) gsDBGPdumpState.pszFile, 0);
		gsDBGPdumpState.pszFile = IMG_NULL;
	}

	if(gsDBGPdumpState.pszScript)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_SCRIPT_SIZE_MAX, (IMG_PVOID) gsDBGPdumpState.pszScript, 0);
		gsDBGPdumpState.pszScript = IMG_NULL;
	}

	if(gsDBGPdumpState.pszMsg)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_MSG_SIZE_MAX, (IMG_PVOID) gsDBGPdumpState.pszMsg, 0);
		gsDBGPdumpState.pszMsg = IMG_NULL;
	}

	/*
	 * Remove the connection notify callback
	 */
	sConnectNotifier.pfnConnectNotifier = 0;
	gpfnDbgDrv->pfnSetConnectNotifier(sConnectNotifier);

	gpfnDbgDrv = IMG_NULL;
}

/**************************************************************************
 * Function Name  : PDumpStartInitPhaseKM
 * Inputs         : None
 * Outputs        : None
 * Returns        : None
 * Description    : Resume init phase state
**************************************************************************/
PVRSRV_ERROR PDumpStartInitPhaseKM(IMG_VOID)
{
	IMG_UINT32 i;

	if (gpfnDbgDrv)
	{
		PDUMPCOMMENT("Start Init Phase");
		for(i=0; i < PDUMP_NUM_STREAMS; i++)
		{
			gpfnDbgDrv->pfnStartInitPhase(gsDBGPdumpState.psStream[i]);
		}
	}
	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : PDumpStopInitPhaseKM
 * Inputs         : None
 * Outputs        : None
 * Returns        : None
 * Description    : End init phase state
**************************************************************************/
PVRSRV_ERROR PDumpStopInitPhaseKM(IMG_VOID)
{
	IMG_UINT32 i;

	if (gpfnDbgDrv)
	{
		PDUMPCOMMENT("Stop Init Phase");

		for(i=0; i < PDUMP_NUM_STREAMS; i++)
		{
			gpfnDbgDrv->pfnStopInitPhase(gsDBGPdumpState.psStream[i]);
		}
	}
	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : PDumpIsLastCaptureFrameKM
 * Inputs         : None
 * Outputs        : None
 * Returns        : True or false
 * Description    : Tests whether the current frame is being pdumped
**************************************************************************/
IMG_BOOL PDumpIsLastCaptureFrameKM(IMG_VOID)
{
	return gpfnDbgDrv->pfnIsLastCaptureFrame(gsDBGPdumpState.psStream[PDUMP_STREAM_SCRIPT2]);
}


/**************************************************************************
 * Function Name  : PDumpIsCaptureFrameKM
 * Inputs         : None
 * Outputs        : None
 * Returns        : True or false
 * Description    : Tests whether the current frame is being pdumped
**************************************************************************/
IMG_BOOL PDumpOSIsCaptureFrameKM(IMG_VOID)
{
	if (PDumpSuspended())
	{
		return IMG_FALSE;
	}
	return gpfnDbgDrv->pfnIsCaptureFrame(gsDBGPdumpState.psStream[PDUMP_STREAM_SCRIPT2], IMG_FALSE);
}

/**************************************************************************
 * Function Name  : PDumpSetFrameKM
 * Inputs         : None
 * Outputs        : None
 * Returns        : None
 * Description    : Sets a frame
**************************************************************************/
PVRSRV_ERROR PDumpOSSetFrameKM(IMG_UINT32 ui32Frame)
{
	IMG_UINT32	ui32Stream;

	for	(ui32Stream = 0; ui32Stream < PDUMP_NUM_STREAMS; ui32Stream++)
	{
		if	(gsDBGPdumpState.psStream[ui32Stream])
		{
			DbgSetFrame(gsDBGPdumpState.psStream[ui32Stream], ui32Frame);
		}
	}

	return PVRSRV_OK;
}


/*****************************************************************************
 FUNCTION	:	PDumpWriteString2

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_BOOL PDumpWriteString2(IMG_CHAR * pszString, IMG_UINT32 ui32Flags)
{
	return PDumpWriteILock(gsDBGPdumpState.psStream[PDUMP_STREAM_SCRIPT2], (IMG_UINT8 *) pszString, strlen(pszString), ui32Flags);
}


/*****************************************************************************
 FUNCTION	: PDumpWriteILock

 PURPOSE	: Writes, making sure it all goes...

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_BOOL PDumpWriteILock(PDBG_STREAM psStream, IMG_UINT8 *pui8Data, IMG_UINT32 ui32Count, IMG_UINT32 ui32Flags)
{
	IMG_UINT32 ui32Written = 0;
	if ((psStream == IMG_NULL) || PDumpSuspended() || ((ui32Flags & PDUMP_FLAGS_NEVER) != 0))
	{
		PVR_DPF((PVR_DBG_MESSAGE, "PDumpWriteILock: Failed to write 0x%x bytes to stream 0x%p", ui32Count, psStream));
		return IMG_TRUE;
	}


	/*
		Set the stream marker to split output files
	*/

	if (psStream == gsDBGPdumpState.psStream[PDUMP_STREAM_PARAM2])
	{
		IMG_UINT32 ui32ParamOutPos = gpfnDbgDrv->pfnGetStreamOffset(gsDBGPdumpState.psStream[PDUMP_STREAM_PARAM2]);

		if (ui32ParamOutPos + ui32Count > MAX_FILE_SIZE)
		{
			if ((gsDBGPdumpState.psStream[PDUMP_STREAM_SCRIPT2] && PDumpWriteString2("\r\n-- Splitting pdump output file\r\n\r\n", ui32Flags)))
			{
				DbgSetMarker(gsDBGPdumpState.psStream[PDUMP_STREAM_PARAM2], ui32ParamOutPos);
				gsDBGPdumpState.ui32ParamFileNum++;
			}
		}
	}

	ui32Written = DbgWrite(psStream, pui8Data, ui32Count, ui32Flags);

	if (ui32Written == 0xFFFFFFFF)
	{
		return IMG_FALSE;
	}

	return IMG_TRUE;
}

/*****************************************************************************
 FUNCTION	:	DbgSetFrame

 PURPOSE	:	Sets the frame in the stream

 PARAMETERS	:	psStream	- Stream pointer
				ui32Frame		- Frame number to set

 RETURNS	: 	None
*****************************************************************************/
static IMG_VOID DbgSetFrame(PDBG_STREAM psStream, IMG_UINT32 ui32Frame)
{
	gpfnDbgDrv->pfnSetFrame(psStream, ui32Frame);
}

/*****************************************************************************
 FUNCTION	:	DbgSetMarker

 PURPOSE	:	Sets the marker of the stream to split output files

 PARAMETERS	:	psStream	- Stream pointer
				ui32Marker	- Marker number to set

 RETURNS	: 	None
*****************************************************************************/
static IMG_VOID DbgSetMarker(PDBG_STREAM psStream, IMG_UINT32 ui32Marker)
{
	gpfnDbgDrv->pfnSetMarker(psStream, ui32Marker);
}

IMG_VOID PDumpSuspendKM(IMG_VOID)
{
	atomic_inc(&gsPDumpSuspended);
}

IMG_VOID PDumpResumeKM(IMG_VOID)
{
	atomic_dec(&gsPDumpSuspended);
}

/* Set to 1 if you want to debug PDump locking issues */
#define DEBUG_PDUMP_LOCKS 0

#if DEBUG_PDUMP_LOCKS
static IMG_UINT32 ui32Count=0;
static IMG_UINT32 aui32LockLine[2] = {0};
static IMG_UINT32 aui32UnlockLine[2] = {0};
static IMG_UINT32 ui32LockLineCount = 0;
static IMG_UINT32 ui32UnlockLineCount = 0;
#endif

IMG_VOID PDumpOSLock(IMG_UINT32 ui32Line)
{
#if DEBUG_PDUMP_LOCKS
	aui32LockLine[ui32LockLineCount++ % 2] = ui32Line;
	ui32Count++;
	if (ui32Count == 2)
	{
		IMG_UINT32 i;
		printk(KERN_ERR "Double lock\n");
		dump_stack();
		for (i=0;i<2;i++)
		{
			printk(KERN_ERR "Lock[%d] = %d, Unlock[%d] = %d\n", i, aui32LockLine[i],i, aui32UnlockLine[i]);
		}
	}
#endif
	mutex_lock(&sPDumpLock);
}

IMG_VOID PDumpOSUnlock(IMG_UINT32 ui32Line)
{
	mutex_unlock(&sPDumpLock);
#if DEBUG_PDUMP_LOCKS
	aui32UnlockLine[ui32UnlockLineCount++ % 2] = ui32Line;
	ui32Count--;
#endif
}

IMG_VOID PDumpOSLockMessageBuffer(IMG_VOID)
{
	mutex_lock(&sPDumpMsgLock);
}

IMG_VOID PDumpOSUnlockMessageBuffer(IMG_VOID)
{
	mutex_unlock(&sPDumpMsgLock);
}

#endif /* #if defined (PDUMP) */
#endif /* #if defined (SUPPORT_SGX) */
/*****************************************************************************
 End of file (PDUMP.C)
*****************************************************************************/
