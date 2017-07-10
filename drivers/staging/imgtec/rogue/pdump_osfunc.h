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

	/**************************************************************************/ /*!
	@Function       PDumpOSGetScriptString
	@Description    Get the handle of the PDump "script" buffer.
	                This function is only called if PDUMP is defined.
	@Output         phScript           Handle of the PDump script buffer
	@Output         pui32MaxLen        max length the script buffer can be
	@Return         PVRSRV_OK on success, a failure code otherwise.
	*/ /**************************************************************************/
	PVRSRV_ERROR PDumpOSGetScriptString(IMG_HANDLE *phScript, IMG_UINT32 *pui32MaxLen);

	/**************************************************************************/ /*!
	@Function       PDumpOSGetMessageString
	@Description    Get the PDump "message" buffer.
	                This function is only called if PDUMP is defined.
	@Output         ppszMsg            Pointer to the PDump message buffer
	@Output         pui32MaxLen        max length the message buffer can be
	@Return         PVRSRV_OK on success, a failure code otherwise.
	*/ /**************************************************************************/
	PVRSRV_ERROR PDumpOSGetMessageString(IMG_CHAR **ppszMsg, IMG_UINT32 *pui32MaxLen);

	/**************************************************************************/ /*!
	@Function       PDumpOSGetFilenameString
	@Description    Get the PDump "filename" buffer.
	                This function is only called if PDUMP is defined.
	@Output         ppszFile           Pointer to the PDump filename buffer
	@Output         pui32MaxLen        max length the filename buffer can be
	@Return         PVRSRV_OK on success, a failure code otherwise.
	*/ /**************************************************************************/
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

/**************************************************************************/ /*!
@Function       PDumpOSInit
@Description    Reset the connection to vldbgdrv, then try to connect to
                PDump streams. This function is only called if PDUMP is
                defined.
@Input          psParam            PDump channel to be used for logging
                                   parameters
@Input          psScript           PDump channel to be used for logging
                                   commands / events
@Output         pui32InitCapMode   The initial PDump capture mode.
@Output         ppszEnvComment     Environment-specific comment that is
                                   output when writing to the PDump
                                   stream (this may be NULL).
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR PDumpOSInit(PDUMP_CHANNEL* psParam, PDUMP_CHANNEL* psScript,
		IMG_UINT32* pui32InitCapMode, IMG_CHAR** ppszEnvComment);

/**************************************************************************/ /*!
@Function       PDumpOSDeInit
@Description    Disconnect the PDump streams and close the connection to
                vldbgdrv. This function is only called if PDUMP is defined.
@Input          psParam            PDump parameter channel to be closed
@Input          psScript           PDump command channel to be closed
@Return         None
*/ /**************************************************************************/
void PDumpOSDeInit(PDUMP_CHANNEL* psParam, PDUMP_CHANNEL* psScript);

/**************************************************************************/ /*!
@Function       PDumpOSSetSplitMarker
@Description    Inform the PDump client to start a new file at the given
                marker. This function is only called if PDUMP is defined.
@Input          hStream            handle of PDump stream
@Input          ui32Marker         byte file position
@Return         IMG_TRUE
*/ /**************************************************************************/
IMG_BOOL PDumpOSSetSplitMarker(IMG_HANDLE hStream, IMG_UINT32 ui32Marker);

/**************************************************************************/ /*!
@Function       PDumpOSDebugDriverWrite
@Description    Writes a given number of bytes from the specified buffer
                to a PDump stream. This function is only called if PDUMP
                is defined.
@Input          psStream           handle of PDump stream to write into
@Input          pui8Data           buffer to write data from
@Input          ui32BCount         number of bytes to write
@Return         The number of bytes actually written (may be less than
                ui32BCount if there is insufficient space in the target
                PDump stream buffer)
*/ /**************************************************************************/
IMG_UINT32 PDumpOSDebugDriverWrite(IMG_HANDLE psStream,
                                   IMG_UINT8 *pui8Data,
                                   IMG_UINT32 ui32BCount);

/*
 * Define macro for processing variable args list in OS-independent
 * manner. See e.g. PDumpCommentWithFlags().
 */
#define PDUMP_va_list	va_list
#define PDUMP_va_start	va_start
#define PDUMP_va_end	va_end


/**************************************************************************/ /*!
@Function       PDumpOSBufprintf
@Description    Printf to OS-specific PDump state buffer. This function is
                only called if PDUMP is defined.
@Input          hBuf               handle of buffer to write into
@Input          ui32ScriptSizeMax  maximum size of data to write (chars)
@Input          pszFormat          format string
@Return         None
*/ /**************************************************************************/
PVRSRV_ERROR PDumpOSBufprintf(IMG_HANDLE hBuf, IMG_UINT32 ui32ScriptSizeMax, IMG_CHAR* pszFormat, ...) __printf(3, 4);

/**************************************************************************/ /*!
@Function       PDumpOSDebugPrintf
@Description    Debug message during PDumping. This function is only called
                if PDUMP is defined.
@Input          pszFormat            format string
@Return         None
*/ /**************************************************************************/
void PDumpOSDebugPrintf(IMG_CHAR* pszFormat, ...) __printf(1, 2);

/*
 * Write into a IMG_CHAR* on all OSes. Can be allocated on the stack or heap.
 */
/**************************************************************************/ /*!
@Function       PDumpOSSprintf
@Description    Printf to IMG char array. This function is only called if
                PDUMP is defined.
@Input          ui32ScriptSizeMax    maximum size of data to write (chars)
@Input          pszFormat            format string
@Output         pszComment           char array to print into
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR PDumpOSSprintf(IMG_CHAR *pszComment, IMG_UINT32 ui32ScriptSizeMax, IMG_CHAR *pszFormat, ...) __printf(3, 4);

/**************************************************************************/ /*!
@Function       PDumpOSVSprintf
@Description    Printf to IMG string using variable args (see stdarg.h).
                This is necessary because the '...' notation does not
                support nested function calls.
                This function is only called if PDUMP is defined.
@Input          ui32ScriptSizeMax    maximum size of data to write (chars)
@Input          pszFormat            format string
@Input          vaArgs               variable args structure (from stdarg.h)
@Output         pszMsg               char array to print into
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR PDumpOSVSprintf(IMG_CHAR *pszMsg, IMG_UINT32 ui32ScriptSizeMax, const IMG_CHAR* pszFormat, PDUMP_va_list vaArgs) __printf(3, 0);

/**************************************************************************/ /*!
@Function       PDumpOSBuflen
@Description    Returns the length of the specified buffer (in chars).
                This function is only called if PDUMP is defined.
@Input          hBuffer              handle to buffer
@Input          ui32BufferSizeMax    max size of buffer (chars)
@Return         The length of the buffer, will always be <= ui32BufferSizeMax
*/ /**************************************************************************/
IMG_UINT32 PDumpOSBuflen(IMG_HANDLE hBuffer, IMG_UINT32 ui32BufferSizeMax);

/**************************************************************************/ /*!
@Function       PDumpOSVerifyLineEnding
@Description    Put line ending sequence at the end if it isn't already
                there. This function is only called if PDUMP is defined.
@Input          hBuffer              handle to buffer
@Input          ui32BufferSizeMax    max size of buffer (chars)
@Return         None
*/ /**************************************************************************/
void PDumpOSVerifyLineEnding(IMG_HANDLE hBuffer, IMG_UINT32 ui32BufferSizeMax);

/**************************************************************************/ /*!
@Function       PDumpOSReleaseExecution
@Description    OS function to switch to another process, to clear PDump
                buffers.
                This function can simply wrap OSReleaseThreadQuanta.
                This function is only called if PDUMP is defined.
@Return         None
*/ /**************************************************************************/
void PDumpOSReleaseExecution(void);

/**************************************************************************/ /*!
@Function       PDumpOSCreateLock
@Description    Create the global pdump lock. This function is only called
                if PDUMP is defined.
@Return         None
*/ /**************************************************************************/
PVRSRV_ERROR PDumpOSCreateLock(void);

/**************************************************************************/ /*!
@Function       PDumpOSDestroyLock
@Description    Destroy the global pdump lock This function is only called
                if PDUMP is defined.
@Return         None
*/ /**************************************************************************/
void PDumpOSDestroyLock(void);

/**************************************************************************/ /*!
@Function       PDumpOSLock
@Description    Acquire the global pdump lock. This function is only called
                if PDUMP is defined.
@Return         None
*/ /**************************************************************************/
void PDumpOSLock(void);

/**************************************************************************/ /*!
@Function       PDumpOSUnlock
@Description    Release the global pdump lock. This function is only called
                if PDUMP is defined.
@Return         None
*/ /**************************************************************************/
void PDumpOSUnlock(void);

/*!
 * @name	PDumpOSGetCtrlState
 * @brief	Retrieve some state from the debug driver or debug driver stream
 */
IMG_UINT32 PDumpOSGetCtrlState(IMG_HANDLE hDbgStream, IMG_UINT32 ui32StateID);

/*!
 * @name	PDumpOSSetFrame
 * @brief	Set the current frame value mirrored in the debug driver
 */
void PDumpOSSetFrame(IMG_UINT32 ui32Frame);

/*!
 * @name	PDumpOSAllowInitPhaseToComplete
 * @brief	Some platforms wish to control when the init phase is marked as
 *          complete depending on who is instructing it so.
 */
IMG_BOOL PDumpOSAllowInitPhaseToComplete(IMG_BOOL bPDumpClient, IMG_BOOL bInitClient);


