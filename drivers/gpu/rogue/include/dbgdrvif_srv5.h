/*************************************************************************/ /*!
@File
@Title          Debug driver for Services 5
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

#ifndef _DBGDRVIF_SRV5_
#define _DBGDRVIF_SRV5_

#if defined(_MSC_VER) 
#pragma  warning(disable:4200)
#endif

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
#define DEBUG_CAPMODE_FRAMED			0x00000001UL /* Default capture mode, set when streams created */
#define DEBUG_CAPMODE_CONTINUOUS		0x00000002UL /* Only set in WDDM, streams created with it set to this mode */

#define DEBUG_FLAGS_USE_NONPAGED_MEM	0x00000001UL /* Only set in WDDM */
#define DEBUG_FLAGS_NO_BUF_EXPANDSION	0x00000002UL
#define DEBUG_FLAGS_READONLY			0x00000008UL
#define DEBUG_FLAGS_WRITEONLY			0x00000010UL
#define DEBUG_FLAGS_CIRCULAR			0x00000020UL

/*****************************************************************************
 IOCTL values.
*****************************************************************************/
/* IOCTL values defined here so that the windows based OS layer of PDump
   in the server can access the GetServiceTable method.
 */
#define DEBUG_SERVICE_IOCTL_BASE		0x800UL
#define DEBUG_SERVICE_GETSERVICETABLE	CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x01, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_GETSTREAM			CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x02, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_READ				CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x03, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_SETMARKER			CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x04, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_GETMARKER			CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x05, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_WAITFOREVENT		CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x06, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_GETFRAME			CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x07, METHOD_BUFFERED, FILE_ANY_ACCESS)
#if defined(__QNXNTO__)
#define DEBUG_SERVICE_CREATESTREAM		CTL_CODE(FILE_DEVICE_UNKNOWN, DEBUG_SERVICE_IOCTL_BASE + 0x08, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEBUG_SERVICE_MAX_API			8
#else
#define DEBUG_SERVICE_MAX_API			9
#endif


#if defined(_WIN32)
/*****************************************************************************
 Debug driver device name
*****************************************************************************/
#if defined (DBGDRV_MODULE_NAME)
#define REGISTRY_PATH_TO_DEBUG_DRIVER \
	L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\" DBGDRV_MODULE_NAME
#define DBGDRV_NT_DEVICE_NAME				L"\\Device\\" DBGDRV_MODULE_NAME
#define DBGDRV_NT_SYMLINK					L"\\DosDevices\\" DBGDRV_MODULE_NAME
#else
#error Debug driver name must be specified
/*
#define DBGDRV_NT_DEVICE_NAME				L"\\Device\\VLDbgDrv"
#define DBGDRV_NT_SYMLINK					L"\\DosDevices\\VLDBGDRV"
*/
#endif

/* symbolic link name */
#define DBGDRV_WIN32_DEVICE_NAME			"\\\\.\\VLDBGDRV"

#define DBGDRV_WINCE_DEVICE_NAME			L"DBD1:"
#endif

#ifdef __GNUC__
#define DBG_ALIGN(n) __attribute__ ((aligned (n)))
#else
#define DBG_ALIGN(n)
#endif

/* A pointer type which is at least 64 bits wide. The fixed width ensures
 * consistency in structures between 32 and 64-bit code.
 * The UM code (be it 32 or 64 bit) can simply write to the native pointer type (pvPtr).
 * 64-bit KM code must read ui32Ptr if in the case of a 32-bit client, otherwise it can
 * just read pvPtr if the client is also 64-bit
 *
 * ui64Ptr ensures the union is 64-bits wide in a 32-bit client.
 *
 * The union is explicitly 64-bit aligned as it was found gcc on x32 only
 * aligns it to 32-bit, as the ABI permits aligning 64-bit types to a 32-bit
 * boundary.
 */
typedef union
{
	/* native pointer type for UM to write to */
	IMG_VOID *pvPtr;
	/* the pointer written by a 32-bit client */
	IMG_UINT32 ui32Ptr;
	/* force the union width */
	IMG_UINT64 ui64Ptr;
} DBG_WIDEPTR DBG_ALIGN(8);

/* Helper macro for dbgdriv (KM) to get the pointer value from the WIDEPTR type,
 * depending on whether the client is 32 or 64-bit.
 *
 * note: double cast is required to avoid
 * 'cast to pointer from integer of different size' warning.
 * this is solved by first casting to an integer type.
 */

#if defined(CONFIG_COMPAT)
#define WIDEPTR_GET_PTR(p, bCompat) (bCompat ? \
					(IMG_VOID *) (IMG_UINTPTR_T) (p).ui32Ptr : \
					(p).pvPtr)
#else
#define WIDEPTR_GET_PTR(p, bCompat) (p).pvPtr
#endif

typedef enum _DBG_EVENT_
{
	DBG_EVENT_STREAM_DATA = 1
} DBG_EVENT;


/*****************************************************************************
 In/Out Structures
*****************************************************************************/
#if defined(__QNXNTO__)
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

typedef struct _DBG_OUT_CREATESTREAM_
{
	IMG_HANDLE phInit;
	IMG_HANDLE phMain;
	IMG_HANDLE phDeinit;
} DBG_OUT_CREATESTREAM, *PDBG_OUT_CREATESTREAM;
#endif

typedef struct _DBG_IN_FINDSTREAM_
{
	DBG_WIDEPTR pszName;
	IMG_BOOL bResetStream;
}DBG_IN_FINDSTREAM, *PDBG_IN_FINDSTREAM;

#define DEBUG_READ_BUFID_MAIN			0
#define DEBUG_READ_BUFID_INIT			1
#define DEBUG_READ_BUFID_DEINIT			2

typedef struct _DBG_IN_READ_
{
	DBG_WIDEPTR pui8OutBuffer;
	IMG_SID hStream;
	IMG_UINT32 ui32BufID;
	IMG_UINT32 ui32OutBufferSize;
} DBG_IN_READ, *PDBG_IN_READ;

typedef struct _DBG_OUT_READ_
{
	IMG_UINT32 ui32DataRead;
	IMG_UINT32 ui32SplitMarker;
} DBG_OUT_READ, *PDBG_OUT_READ;

typedef struct _DBG_IN_SETMARKER_
{
	IMG_SID hStream;
	IMG_UINT32 ui32Marker;
} DBG_IN_SETMARKER, *PDBG_IN_SETMARKER;

/*
	DBG STREAM abstract types
*/

typedef struct _DBG_STREAM_CONTROL_* PDBG_STREAM_CONTROL;
typedef struct _DBG_STREAM_* PDBG_STREAM;

/*
	Lookup identifiers for the GetState method in the KM service table.
 */
#define DBG_GET_STATE_FLAG_IS_READONLY    0x03


/*****************************************************************************
 Kernel mode service table
*****************************************************************************/
typedef struct _DBGKM_SERVICE_TABLE_
{
	IMG_UINT32 ui32Size;
	IMG_BOOL	(IMG_CALLCONV *pfnCreateStream)			(IMG_CHAR * pszName,IMG_UINT32 ui32Flags,IMG_UINT32 ui32Pages, IMG_HANDLE* phInit, IMG_HANDLE* phMain, IMG_HANDLE* phDeinit);
	IMG_VOID 	(IMG_CALLCONV *pfnDestroyStream)		(IMG_HANDLE hInit, IMG_HANDLE hMain, IMG_HANDLE hDeinit);
	IMG_UINT32	(IMG_CALLCONV *pfnDBGDrivWrite2)		(PDBG_STREAM psStream, IMG_UINT8 *pui8InBuf,IMG_UINT32 ui32InBuffSize);
	IMG_VOID 	(IMG_CALLCONV *pfnSetMarker)			(PDBG_STREAM psStream, IMG_UINT32 ui32Marker);
	IMG_VOID 	(IMG_CALLCONV *pfnWaitForEvent)			(DBG_EVENT eEvent);
	IMG_UINT32  (IMG_CALLCONV *pfnGetCtrlState)			(PDBG_STREAM psStream, IMG_UINT32 ui32StateID);
	IMG_VOID 	(IMG_CALLCONV *pfnSetFrame)				(IMG_UINT32 ui32Frame);
} DBGKM_SERVICE_TABLE, *PDBGKM_SERVICE_TABLE;

#if defined(_MSC_VER) 
#pragma  warning(default:4200)
#endif

#endif

/*****************************************************************************
 End of file
*****************************************************************************/
