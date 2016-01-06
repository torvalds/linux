/*************************************************************************/ /*!
@File
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	OS-independent interface to helper functions for pdump
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

#include <stdarg.h>

#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvrsrv_device_types.h"


/* FIXME
 * Some OSes (WinXP,CE) allocate the string on the stack, but some
 * (Linux) use a global variable/lock instead.
 * Would be good to use the same across all OSes.
 *
 * A handle is returned which represents IMG_CHAR* type on all OSes.
 *
 * The allocated buffer length is also returned on OSes where it's
 * supported (e.g. Linux).
 */
#define MAX_PDUMP_STRING_LENGTH (256)
#if defined(WIN32)
#define PDUMP_GET_SCRIPT_STRING()	\
	IMG_CHAR pszScript[MAX_PDUMP_STRING_LENGTH];		\
	IMG_UINT32	ui32MaxLen = MAX_PDUMP_STRING_LENGTH-1;	\
	IMG_HANDLE	hScript = (IMG_HANDLE)pszScript;

#define PDUMP_GET_MSG_STRING()		\
	IMG_CHAR pszMsg[MAX_PDUMP_STRING_LENGTH];			\
	IMG_UINT32	ui32MaxLen = MAX_PDUMP_STRING_LENGTH-1;

#define PDUMP_GET_FILE_STRING()		\
	IMG_CHAR	pszFileName[MAX_PDUMP_STRING_LENGTH];	\
	IMG_UINT32	ui32MaxLen = MAX_PDUMP_STRING_LENGTH-1;

#define PDUMP_GET_SCRIPT_AND_FILE_STRING()		\
	IMG_CHAR 	pszScript[MAX_PDUMP_STRING_LENGTH];		\
	IMG_CHAR	pszFileName[MAX_PDUMP_STRING_LENGTH];	\
	IMG_UINT32	ui32MaxLenScript = MAX_PDUMP_STRING_LENGTH-1;	\
	IMG_UINT32	ui32MaxLenFileName = MAX_PDUMP_STRING_LENGTH-1;	\
	IMG_HANDLE	hScript = (IMG_HANDLE)pszScript;

#else	/* WIN32 */

#if defined(__QNXNTO__)

#define PDUMP_GET_SCRIPT_STRING()	\
	IMG_CHAR pszScript[MAX_PDUMP_STRING_LENGTH];		\
	IMG_UINT32	ui32MaxLen = MAX_PDUMP_STRING_LENGTH-1;	\
	IMG_HANDLE	hScript = (IMG_HANDLE)pszScript;

#define PDUMP_GET_MSG_STRING()		\
	IMG_CHAR pszMsg[MAX_PDUMP_STRING_LENGTH];			\
	IMG_UINT32	ui32MaxLen = MAX_PDUMP_STRING_LENGTH-1;

#define PDUMP_GET_FILE_STRING()		\
	IMG_CHAR	pszFileName[MAX_PDUMP_STRING_LENGTH];	\
	IMG_UINT32	ui32MaxLen = MAX_PDUMP_STRING_LENGTH-1;

#define PDUMP_GET_SCRIPT_AND_FILE_STRING()		\
	IMG_CHAR 	pszScript[MAX_PDUMP_STRING_LENGTH];		\
	IMG_CHAR	pszFileName[MAX_PDUMP_STRING_LENGTH];	\
	IMG_UINT32	ui32MaxLenScript = MAX_PDUMP_STRING_LENGTH-1;	\
	IMG_UINT32	ui32MaxLenFileName = MAX_PDUMP_STRING_LENGTH-1;	\
	IMG_HANDLE	hScript = (IMG_HANDLE)pszScript;

#else  /* __QNXNTO__ */

	/*
	 * Linux
	 */
#define PDUMP_GET_SCRIPT_STRING()				\
	IMG_HANDLE hScript;							\
	IMG_UINT32	ui32MaxLen;						\
	PVRSRV_ERROR eErrorPDump;						\
	eErrorPDump = PDumpOSGetScriptString(&hScript, &ui32MaxLen);\
	PVR_LOGR_IF_ERROR(eErrorPDump, "PDumpOSGetScriptString");

#define PDUMP_GET_MSG_STRING()					\
	IMG_CHAR *pszMsg;							\
	IMG_UINT32	ui32MaxLen;						\
	PVRSRV_ERROR eErrorPDump;						\
	eErrorPDump = PDumpOSGetMessageString(&pszMsg, &ui32MaxLen);\
	PVR_LOGR_IF_ERROR(eErrorPDump, "PDumpOSGetMessageString");

#define PDUMP_GET_FILE_STRING()				\
	IMG_CHAR *pszFileName;					\
	IMG_UINT32	ui32MaxLen;					\
	PVRSRV_ERROR eErrorPDump;				\
	eErrorPDump = PDumpOSGetFilenameString(&pszFileName, &ui32MaxLen);\
	PVR_LOGR_IF_ERROR(eErrorPDump, "PDumpOSGetFilenameString");

#define PDUMP_GET_SCRIPT_AND_FILE_STRING()		\
	IMG_HANDLE hScript;							\
	IMG_CHAR *pszFileName;						\
	IMG_UINT32	ui32MaxLenScript;				\
	IMG_UINT32	ui32MaxLenFileName;				\
	PVRSRV_ERROR eErrorPDump;						\
	eErrorPDump = PDumpOSGetScriptString(&hScript, &ui32MaxLenScript);\
	PVR_LOGR_IF_ERROR(eErrorPDump, "PDumpOSGetScriptString");\
	eErrorPDump = PDumpOSGetFilenameString(&pszFileName, &ui32MaxLenFileName);\
	PVR_LOGR_IF_ERROR(eErrorPDump, "PDumpOSGetFilenameString");

	/*!
	 * @name	PDumpOSGetScriptString
	 * @brief	Get the "script" buffer
	 * @param	phScript - buffer handle for pdump script
	 * @param	pui32MaxLen - max length of the script buffer
	 * 			FIXME: the max length should be internal to the OS-specific code
	 * @return	error (always PVRSRV_OK on some OSes)
	 */
	PVRSRV_ERROR PDumpOSGetScriptString(IMG_HANDLE *phScript, IMG_UINT32 *pui32MaxLen);

	/*!
	 * @name	PDumpOSGetMessageString
	 * @brief	Get the "message" buffer
	 * @param	pszMsg - buffer pointer for pdump messages
	 * @param	pui32MaxLen - max length of the message buffer
	 * 			FIXME: the max length should be internal to the OS-specific code
	 * @return	error (always PVRSRV_OK on some OSes)
	 */
	PVRSRV_ERROR PDumpOSGetMessageString(IMG_CHAR **ppszMsg, IMG_UINT32 *pui32MaxLen);

	/*!
	 * @name	PDumpOSGetFilenameString
	 * @brief	Get the "filename" buffer
	 * @param	ppszFile - buffer pointer for filename
	 * @param	pui32MaxLen - max length of the filename buffer
	 * 			FIXME: the max length should be internal to the OS-specific code
	 * @return	error (always PVRSRV_OK on some OSes)
	 */
	PVRSRV_ERROR PDumpOSGetFilenameString(IMG_CHAR **ppszFile, IMG_UINT32 *pui32MaxLen);

#endif /* __QNXNTO__ */
#endif /* WIN32 */


/*
 * PDump streams, channels, init and deinit routines (common to all OSes)
 */

typedef struct
{
	IMG_HANDLE hInit;        /*!< Driver initialisation PDump stream */
	IMG_HANDLE hMain;        /*!< App framed PDump stream */
	IMG_HANDLE hDeinit;      /*!< Driver/HW de-initialisation PDump stream */
} PDUMP_CHANNEL;

PVRSRV_ERROR PDumpOSInit(PDUMP_CHANNEL* psParam, PDUMP_CHANNEL* psScript,
		IMG_UINT32* pui32InitCapMode, IMG_CHAR** ppszEnvComment);

IMG_VOID PDumpOSDeInit(PDUMP_CHANNEL* psParam, PDUMP_CHANNEL* psScript);

/*!
 * @name	PDumpOSSetSplitMarker
 * @brief	Inform the PDump client to start a new file at the given marker.
 * @param	hStream - stream
 * @param   ui32Marker - byte file position
 */
IMG_BOOL PDumpOSSetSplitMarker(IMG_HANDLE hStream, IMG_UINT32 ui32Marker);

/*
	PDumpOSDebugDriverWrite - ENV layer write entry point from COMMON layer
	                          A call back down the PDump software layer
 */
IMG_UINT32 PDumpOSDebugDriverWrite(IMG_HANDLE psStream,
                                   IMG_UINT8 *pui8Data,
                                   IMG_UINT32 ui32BCount);

/*
 * Define macro for processing variable args list in OS-independent
 * manner. See e.g. PDumpComment().
 */
#define PDUMP_va_list	va_list
#define PDUMP_va_start	va_start
#define PDUMP_va_end	va_end


/*!
 * @name	PDumpOSBufprintf
 * @brief	Printf to OS-specific pdump state buffer
 * @param	hBuf - buffer handle to write into
 * @param	ui32ScriptSizeMax - maximum size of data to write (not supported on all OSes)
 * @param	pszFormat - format string
 */
PVRSRV_ERROR PDumpOSBufprintf(IMG_HANDLE hBuf, IMG_UINT32 ui32ScriptSizeMax, IMG_CHAR* pszFormat, ...) IMG_FORMAT_PRINTF(3, 4);

/*!
 * @name	PDumpOSDebugPrintf
 * @brief	Debug message during pdumping
 * @param	pszFormat - format string
 */
IMG_VOID PDumpOSDebugPrintf(IMG_CHAR* pszFormat, ...) IMG_FORMAT_PRINTF(1, 2);

/*
 * Write into a IMG_CHAR* on all OSes. Can be allocated on the stack or heap.
 */
/*!
 * @name	PDumpOSSprintf
 * @brief	Printf to IMG char array
 * @param	pszComment - char array to print into
 * @param	pszFormat - format string
 */
PVRSRV_ERROR PDumpOSSprintf(IMG_CHAR *pszComment, IMG_UINT32 ui32ScriptSizeMax, IMG_CHAR *pszFormat, ...) IMG_FORMAT_PRINTF(3, 4);

/*!
 * @name	PDumpOSVSprintf
 * @brief	Printf to IMG string using variable args (see stdarg.h). This is necessary
 * 			because the ... notation does not support nested function calls.
 * @param	pszMsg - char array to print into
 * @param	ui32ScriptSizeMax - maximum size of data to write (not supported on all OSes)
 * @param	pszFormat - format string
 * @param	vaArgs - variable args structure (from stdarg.h)
 */
PVRSRV_ERROR PDumpOSVSprintf(IMG_CHAR *pszMsg, IMG_UINT32 ui32ScriptSizeMax, IMG_CHAR* pszFormat, PDUMP_va_list vaArgs) IMG_FORMAT_PRINTF(3, 0);

/*!
 * @name	PDumpOSBuflen
 * @param	hBuffer - handle to buffer
 * @param	ui32BuffeRSizeMax - max size of buffer (chars)
 * @return	length of buffer, will always be <= ui32BufferSizeMax
 */
IMG_UINT32 PDumpOSBuflen(IMG_HANDLE hBuffer, IMG_UINT32 ui32BufferSizeMax);

/*!
 * @name	PDumpOSVerifyLineEnding
 * @brief	Put line ending sequence at the end if it isn't already there
 * @param	hBuffer - handle to buffer
 * @param	ui32BufferSizeMax - max size of buffer (chars)
 */
IMG_VOID PDumpOSVerifyLineEnding(IMG_HANDLE hBuffer, IMG_UINT32 ui32BufferSizeMax);

/*!
 * @name	PDumpOSCPUVAddrToDevPAddr
 * @brief	OS function to convert CPU virtual to device physical for dumping pages
 * @param	hOSMemHandle	mem allocation handle (used if kernel virtual mem space is limited, e.g. linux)
 * @param	ui32Offset		dword offset into allocation (for use with mem handle, e.g. linux)
 * @param	pui8LinAddr		CPU linear addr (usually a kernel virtual address)
 * @param	ui32PageSize	page size, used for assertion check
 * @return	psDevPAddr		device physical addr
 */
IMG_VOID PDumpOSCPUVAddrToDevPAddr(PVRSRV_DEVICE_TYPE eDeviceType,
        IMG_HANDLE hOSMemHandle,
		IMG_UINT32 ui32Offset,
		IMG_UINT8 *pui8LinAddr,
		IMG_UINT32 ui32PageSize,
		IMG_DEV_PHYADDR *psDevPAddr);

/*!
 * @name	PDumpOSCPUVAddrToPhysPages
 * @brief	OS function to convert CPU virtual to backing physical pages
 * @param	hOSMemHandle	mem allocation handle (used if kernel virtual mem space is limited, e.g. linux)
 * @param	ui32Offset		offset within mem allocation block
 * @param	pui8LinAddr		CPU linear addr
 * @param	ui32DataPageMask	mask for data page (= data page size -1)
 * @return	pui32PageOffset	CPU page offset (same as device page offset if page sizes equal)
 */
IMG_VOID PDumpOSCPUVAddrToPhysPages(IMG_HANDLE hOSMemHandle,
		IMG_UINT32 ui32Offset,
		IMG_PUINT8 pui8LinAddr,
		IMG_UINT32 ui32DataPageMask,
		IMG_UINT32 *pui32PageOffset);

/*!
 * @name	PDumpOSReleaseExecution
 * @brief	OS function to switch to another process, to clear pdump buffers
 */
IMG_VOID PDumpOSReleaseExecution(IMG_VOID);

/*!
 * @name	PDumpOSCreateLock
 * @brief	Create the global pdump lock
 */
PVRSRV_ERROR PDumpOSCreateLock(IMG_VOID);

/*!
 * @name	PDumpOSDestroyLock
 * @brief	Destroy the global pdump lock
 */
IMG_VOID PDumpOSDestroyLock(IMG_VOID);

/*!
 * @name	PDumpOSLock
 * @brief	Acquire the global pdump lock
 */
IMG_VOID PDumpOSLock(IMG_VOID);

/*!
 * @name	PDumpOSUnlock
 * @brief	Release the global pdump lock
 */
IMG_VOID PDumpOSUnlock(IMG_VOID);

/*!
 * @name	PDumpOSGetCtrlState
 * @brief	Retrieve some state from the debug driver or debug driver stream
 */
IMG_UINT32 PDumpOSGetCtrlState(IMG_HANDLE hDbgStream, IMG_UINT32 ui32StateID);

/*!
 * @name	PDumpOSSetFrame
 * @brief	Set the current frame value mirrored in the debug driver
 */
IMG_VOID PDumpOSSetFrame(IMG_UINT32 ui32Frame);

/*!
 * @name	PDumpOSAllowInitPhaseToComplete
 * @brief	Some platforms wish to control when the init phase is marked as
 *          complete depending on who is instructing it so.
 */
IMG_BOOL PDumpOSAllowInitPhaseToComplete(IMG_UINT32 eModuleID);


