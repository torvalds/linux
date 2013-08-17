/*************************************************************************/ /*!
@Title          Debug driver
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Debug Driver Interface
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

#ifndef _DBGDRVIF_
#define _DBGDRVIF_


#if defined(__linux__)

#define FILE_DEVICE_UNKNOWN             0
#define METHOD_BUFFERED                 0
#define FILE_ANY_ACCESS                 0

#define CTL_CODE( DeviceType, Function, Method, Access ) (Function) 
#define MAKEIOCTLINDEX(i)	((i) & 0xFFF)

#else

#include "ioctldef.h"

#endif

/*****************************************************************************
 Stream mode stuff.
*****************************************************************************/
#define DEBUG_CAPMODE_FRAMED			0x00000001UL
#define DEBUG_CAPMODE_CONTINUOUS		0x00000002UL
#define DEBUG_CAPMODE_HOTKEY			0x00000004UL

#define DEBUG_OUTMODE_STANDARDDBG		0x00000001UL
#define DEBUG_OUTMODE_MONO				0x00000002UL
#define DEBUG_OUTMODE_STREAMENABLE		0x00000004UL
#define DEBUG_OUTMODE_ASYNC				0x00000008UL
#define DEBUG_OUTMODE_SGXVGA            0x00000010UL

#define DEBUG_FLAGS_USE_NONPAGED_MEM	0x00000001UL
#define DEBUG_FLAGS_NO_BUF_EXPANDSION	0x00000002UL
#define DEBUG_FLAGS_ENABLESAMPLE		0x00000004UL
#define DEBUG_FLAGS_READONLY			0x00000008UL
#define DEBUG_FLAGS_WRITEONLY			0x00000010UL

#define DEBUG_FLAGS_TEXTSTREAM			0x80000000UL

/*****************************************************************************
 Debug level control. Only bothered with the first 12 levels, I suspect you
 get the idea...
*****************************************************************************/
#define DEBUG_LEVEL_0					0x00000001UL
#define DEBUG_LEVEL_1					0x00000003UL
#define DEBUG_LEVEL_2					0x00000007UL
#define DEBUG_LEVEL_3					0x0000000FUL
#define DEBUG_LEVEL_4					0x0000001FUL
#define DEBUG_LEVEL_5					0x0000003FUL
#define DEBUG_LEVEL_6					0x0000007FUL
#define DEBUG_LEVEL_7					0x000000FFUL
#define DEBUG_LEVEL_8					0x000001FFUL
#define DEBUG_LEVEL_9					0x000003FFUL
#define DEBUG_LEVEL_10					0x000007FFUL
#define DEBUG_LEVEL_11					0x00000FFFUL

#define DEBUG_LEVEL_SEL0				0x00000001UL
#define DEBUG_LEVEL_SEL1				0x00000002UL
#define DEBUG_LEVEL_SEL2				0x00000004UL
#define DEBUG_LEVEL_SEL3				0x00000008UL
#define DEBUG_LEVEL_SEL4				0x00000010UL
#define DEBUG_LEVEL_SEL5				0x00000020UL
#define DEBUG_LEVEL_SEL6				0x00000040UL
#define DEBUG_LEVEL_SEL7				0x00000080UL
#define DEBUG_LEVEL_SEL8				0x00000100UL
#define DEBUG_LEVEL_SEL9				0x00000200UL
#define DEBUG_LEVEL_SEL10				0x00000400UL
#define DEBUG_LEVEL_SEL11				0x00000800UL

/*****************************************************************************
 IOCTL values.
*****************************************************************************/
#define DEBUG_SERVICE_IOCTL_BASE		0x800UL
#define DEBUG_SERVICE_CREATESTREAM		CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x01, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_DESTROYSTREAM		CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x02, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_GETSTREAM			CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x03, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_WRITESTRING		CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x04, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_READSTRING		CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x05, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_WRITE				CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x06, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_READ				CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x07, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_SETDEBUGMODE		CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x08, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_SETDEBUGOUTMODE	CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x09, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_SETDEBUGLEVEL		CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x0A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_SETFRAME			CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x0B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_GETFRAME			CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x0C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_OVERRIDEMODE		CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x0D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_DEFAULTMODE		CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x0E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_GETSERVICETABLE	CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x0F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_WRITE2			CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x10, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_WRITESTRINGCM		CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x11, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_WRITECM			CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x12, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_SETMARKER			CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x13, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_GETMARKER			CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x14, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_ISCAPTUREFRAME	CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x15, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_WRITELF			CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x16, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_READLF			CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x17, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_WAITFOREVENT		CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x18, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_SETCONNNOTIFY		CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x19, METHOD_BUFFERED, FILE_ANY_ACCESS)


typedef enum _DBG_EVENT_
{
	DBG_EVENT_STREAM_DATA = 1
} DBG_EVENT;


/*****************************************************************************
 In/Out Structures
*****************************************************************************/
typedef struct _DBG_IN_CREATESTREAM_
{
	union
	{
		IMG_CHAR *pszName;
		IMG_UINT64 ui64Name;
	} u;
	IMG_UINT32 ui32Pages;
	IMG_UINT32 ui32CapMode;
	IMG_UINT32 ui32OutMode;
}DBG_IN_CREATESTREAM, *PDBG_IN_CREATESTREAM;

typedef struct _DBG_IN_FINDSTREAM_
{
	union
	{
		IMG_CHAR *pszName;
		IMG_UINT64 ui64Name;
	}u;
	IMG_BOOL bResetStream;
}DBG_IN_FINDSTREAM, *PDBG_IN_FINDSTREAM;

typedef struct _DBG_IN_WRITESTRING_
{
	union
	{
		IMG_CHAR *pszString;
		IMG_UINT64 ui64String;
	} u;
	IMG_SID hStream;
	IMG_UINT32 ui32Level;
}DBG_IN_WRITESTRING, *PDBG_IN_WRITESTRING;

typedef struct _DBG_IN_READSTRING_
{
	union
	{
		IMG_CHAR *pszString;
		IMG_UINT64 ui64String;
	} u;
	IMG_SID hStream;
	IMG_UINT32 ui32StringLen;
} DBG_IN_READSTRING, *PDBG_IN_READSTRING;

typedef struct _DBG_IN_SETDEBUGMODE_
{
	IMG_SID hStream;
	IMG_UINT32 ui32Mode;
	IMG_UINT32 ui32Start;
	IMG_UINT32 ui32End;
	IMG_UINT32 ui32SampleRate;
} DBG_IN_SETDEBUGMODE, *PDBG_IN_SETDEBUGMODE;

typedef struct _DBG_IN_SETDEBUGOUTMODE_
{
	IMG_SID hStream;
	IMG_UINT32 ui32Mode;
} DBG_IN_SETDEBUGOUTMODE, *PDBG_IN_SETDEBUGOUTMODE;

typedef struct _DBG_IN_SETDEBUGLEVEL_
{
	IMG_SID hStream;
	IMG_UINT32 ui32Level;
} DBG_IN_SETDEBUGLEVEL, *PDBG_IN_SETDEBUGLEVEL;

typedef struct _DBG_IN_SETFRAME_
{
	IMG_SID hStream;
	IMG_UINT32 ui32Frame;
} DBG_IN_SETFRAME, *PDBG_IN_SETFRAME;

typedef struct _DBG_IN_WRITE_
{
	union
	{
		IMG_UINT8 *pui8InBuffer;
		IMG_UINT64 ui64InBuffer;
	} u;
	IMG_SID hStream;
	IMG_UINT32 ui32Level;
	IMG_UINT32 ui32TransferSize;
} DBG_IN_WRITE, *PDBG_IN_WRITE;

typedef struct _DBG_IN_READ_
{
	union
	{
		IMG_UINT8 *pui8OutBuffer;
		IMG_UINT64 ui64OutBuffer;
	} u;
	IMG_SID hStream;
	IMG_BOOL bReadInitBuffer;
	IMG_UINT32 ui32OutBufferSize;
} DBG_IN_READ, *PDBG_IN_READ;

typedef struct _DBG_IN_OVERRIDEMODE_
{
	IMG_SID hStream;
	IMG_UINT32 ui32Mode;
} DBG_IN_OVERRIDEMODE, *PDBG_IN_OVERRIDEMODE;

typedef struct _DBG_IN_ISCAPTUREFRAME_
{
	IMG_SID hStream;
	IMG_BOOL bCheckPreviousFrame;
} DBG_IN_ISCAPTUREFRAME, *PDBG_IN_ISCAPTUREFRAME;

typedef struct _DBG_IN_SETMARKER_
{
	IMG_SID hStream;
	IMG_UINT32 ui32Marker;
} DBG_IN_SETMARKER, *PDBG_IN_SETMARKER;

typedef struct _DBG_IN_WRITE_LF_
{
	union
	{
		IMG_UINT8 *pui8InBuffer;
		IMG_UINT64 ui64InBuffer;
	} u;
	IMG_UINT32 ui32Flags;
	IMG_SID    hStream;
	IMG_UINT32 ui32Level;
	IMG_UINT32 ui32BufferSize;
} DBG_IN_WRITE_LF, *PDBG_IN_WRITE_LF;

/*
	Flags for above struct
*/
#define WRITELF_FLAGS_RESETBUF		0x00000001UL

/*
	Common control structure (don't duplicate control in main stream
	and init phase stream).
*/
typedef struct _DBG_STREAM_CONTROL_
{
	IMG_BOOL   bInitPhaseComplete;		/*!< init phase has finished */
	IMG_UINT32 ui32Flags;			/*!< flags (see DEBUG_FLAGS above) */

	IMG_UINT32 ui32CapMode;			/*!< capturing mode framed/hot key */
	IMG_UINT32 ui32OutMode;			/*!< output mode, e.g. files */
	IMG_UINT32 ui32DebugLevel;
	IMG_UINT32 ui32DefaultMode;
	IMG_UINT32 ui32Start;			/*!< first capture frame */
	IMG_UINT32 ui32End;				/*!< last frame */
	IMG_UINT32 ui32Current;			/*!< current frame */
	IMG_UINT32 ui32SampleRate;		/*!< capture frequency */
	IMG_UINT32 ui32Reserved;
} DBG_STREAM_CONTROL, *PDBG_STREAM_CONTROL;
/*
	Per-buffer control structure.
*/
typedef struct _DBG_STREAM_
{
	struct _DBG_STREAM_ *psNext;
	struct _DBG_STREAM_ *psInitStream;
	DBG_STREAM_CONTROL *psCtrl;
	IMG_BOOL   bCircularAllowed;
	IMG_PVOID  pvBase;
	IMG_UINT32 ui32Size;
	IMG_UINT32 ui32RPtr;
	IMG_UINT32 ui32WPtr;
	IMG_UINT32 ui32DataWritten;
	IMG_UINT32 ui32Marker;			/*!< marker for file splitting */
	IMG_UINT32 ui32InitPhaseWOff;	/*!< snapshot offset for init phase end for follow-on pdump */
	IMG_CHAR szName[30];		/* Give this a size, some compilers don't like [] */
} DBG_STREAM,*PDBG_STREAM;

/*
 * Allows dbgdrv to notify services when events happen, e.g. pdump.exe starts.
 * (better than resetting psDevInfo->psKernelCCBInfo->ui32CCBDumpWOff = 0
 * in SGXGetClientInfoKM.)
 */
typedef struct _DBGKM_CONNECT_NOTIFIER_
{
	IMG_VOID (IMG_CALLCONV *pfnConnectNotifier)		(IMG_VOID);
} DBGKM_CONNECT_NOTIFIER, *PDBGKM_CONNECT_NOTIFIER;

/*****************************************************************************
 Kernel mode service table
*****************************************************************************/
typedef struct _DBGKM_SERVICE_TABLE_
{
	IMG_UINT32 ui32Size;
	IMG_VOID * 	(IMG_CALLCONV *pfnCreateStream)			(IMG_CHAR * pszName,IMG_UINT32 ui32CapMode,IMG_UINT32 ui32OutMode,IMG_UINT32 ui32Flags,IMG_UINT32 ui32Pages);
	IMG_VOID 	(IMG_CALLCONV *pfnDestroyStream)		(PDBG_STREAM psStream);
	IMG_VOID * 	(IMG_CALLCONV *pfnFindStream) 			(IMG_CHAR * pszName, IMG_BOOL bResetInitBuffer);
	IMG_UINT32 	(IMG_CALLCONV *pfnWriteString) 			(PDBG_STREAM psStream,IMG_CHAR * pszString,IMG_UINT32 ui32Level);
	IMG_UINT32 	(IMG_CALLCONV *pfnReadString)			(PDBG_STREAM psStream,IMG_CHAR * pszString,IMG_UINT32 ui32Limit);
	IMG_UINT32 	(IMG_CALLCONV *pfnWriteBIN)				(PDBG_STREAM psStream,IMG_UINT8 *pui8InBuf,IMG_UINT32 ui32InBuffSize,IMG_UINT32 ui32Level);
	IMG_UINT32 	(IMG_CALLCONV *pfnReadBIN)				(PDBG_STREAM psStream,IMG_BOOL bReadInitBuffer, IMG_UINT32 ui32OutBufferSize,IMG_UINT8 *pui8OutBuf);
	IMG_VOID 	(IMG_CALLCONV *pfnSetCaptureMode)		(PDBG_STREAM psStream,IMG_UINT32 ui32CapMode,IMG_UINT32 ui32Start,IMG_UINT32 ui32Stop,IMG_UINT32 ui32SampleRate);
	IMG_VOID 	(IMG_CALLCONV *pfnSetOutputMode)		(PDBG_STREAM psStream,IMG_UINT32 ui32OutMode);
	IMG_VOID 	(IMG_CALLCONV *pfnSetDebugLevel)		(PDBG_STREAM psStream,IMG_UINT32 ui32DebugLevel);
	IMG_VOID 	(IMG_CALLCONV *pfnSetFrame)				(PDBG_STREAM psStream,IMG_UINT32 ui32Frame);
	IMG_UINT32 	(IMG_CALLCONV *pfnGetFrame)				(PDBG_STREAM psStream);
	IMG_VOID 	(IMG_CALLCONV *pfnOverrideMode)			(PDBG_STREAM psStream,IMG_UINT32 ui32Mode);
	IMG_VOID 	(IMG_CALLCONV *pfnDefaultMode)			(PDBG_STREAM psStream);
	IMG_UINT32	(IMG_CALLCONV *pfnDBGDrivWrite2)		(PDBG_STREAM psStream,IMG_UINT8 *pui8InBuf,IMG_UINT32 ui32InBuffSize,IMG_UINT32 ui32Level);
	IMG_UINT32 	(IMG_CALLCONV *pfnWriteStringCM)		(PDBG_STREAM psStream,IMG_CHAR * pszString,IMG_UINT32 ui32Level);
	IMG_UINT32	(IMG_CALLCONV *pfnWriteBINCM)			(PDBG_STREAM psStream,IMG_UINT8 *pui8InBuf,IMG_UINT32 ui32InBuffSize,IMG_UINT32 ui32Level);
	IMG_VOID 	(IMG_CALLCONV *pfnSetMarker)			(PDBG_STREAM psStream,IMG_UINT32 ui32Marker);
	IMG_UINT32 	(IMG_CALLCONV *pfnGetMarker)			(PDBG_STREAM psStream);
	IMG_VOID 	(IMG_CALLCONV *pfnStartInitPhase)		(PDBG_STREAM psStream);
	IMG_VOID 	(IMG_CALLCONV *pfnStopInitPhase)		(PDBG_STREAM psStream);
	IMG_BOOL 	(IMG_CALLCONV *pfnIsCaptureFrame)		(PDBG_STREAM psStream, IMG_BOOL bCheckPreviousFrame);
	IMG_UINT32 	(IMG_CALLCONV *pfnWriteLF)				(PDBG_STREAM psStream, IMG_UINT8 *pui8InBuf, IMG_UINT32 ui32InBuffSize, IMG_UINT32 ui32Level, IMG_UINT32 ui32Flags);
	IMG_UINT32 	(IMG_CALLCONV *pfnReadLF)				(PDBG_STREAM psStream, IMG_UINT32 ui32OutBuffSize, IMG_UINT8 *pui8OutBuf);
	IMG_UINT32 	(IMG_CALLCONV *pfnGetStreamOffset)		(PDBG_STREAM psStream);
	IMG_VOID	(IMG_CALLCONV *pfnSetStreamOffset)		(PDBG_STREAM psStream, IMG_UINT32 ui32StreamOffset);
	IMG_BOOL 	(IMG_CALLCONV *pfnIsLastCaptureFrame)	(PDBG_STREAM psStream);
	IMG_VOID 	(IMG_CALLCONV *pfnWaitForEvent)			(DBG_EVENT eEvent);
	IMG_VOID 	(IMG_CALLCONV *pfnSetConnectNotifier)	(DBGKM_CONNECT_NOTIFIER fn_notifier);
	IMG_UINT32 	(IMG_CALLCONV *pfnWritePersist)			(PDBG_STREAM psStream,IMG_UINT8 *pui8InBuf,IMG_UINT32 ui32InBuffSize,IMG_UINT32 ui32Level);
} DBGKM_SERVICE_TABLE, *PDBGKM_SERVICE_TABLE;

#if defined(__linux__)
/*****************************************************************************
 Function to export service table from debug driver to the PDUMP component.
*****************************************************************************/
IMG_VOID DBGDrvGetServiceTable(DBGKM_SERVICE_TABLE **fn_table);
#endif


#endif
/*****************************************************************************
 End of file (DBGDRVIF.H)
*****************************************************************************/
