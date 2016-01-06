/*************************************************************************/ /*!
@File
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

#if defined(_WIN32)
#pragma  warning(disable:4201)
#pragma  warning(disable:4214)
#pragma  warning(disable:4115)
#pragma  warning(disable:4514)

#include <ntddk.h>
#include <windef.h>

#endif /* _WIN32 */

#ifdef LINUX
#include <asm/uaccess.h>
#include "pvr_uaccess.h"
#endif /* LINUX */

#include "img_types.h"
#include "dbgdrvif_srv5.h"
#include "dbgdriv.h"
#include "dbgdriv_ioctl.h"
#include "hostfunc.h"

#ifdef _WIN32
#pragma  warning(default:4214)
#pragma  warning(default:4115)
#endif /* _WIN32 */

/*****************************************************************************
 Code
*****************************************************************************/

/*****************************************************************************
 FUNCTION	:	DBGDIOCDrivGetServiceTable

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivGetServiceTable(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer, IMG_BOOL bCompat)
{
	IMG_PVOID *	ppvOut;

	PVR_UNREFERENCED_PARAMETER(pvInBuffer);
	PVR_UNREFERENCED_PARAMETER(bCompat);
	ppvOut = (IMG_PVOID *) pvOutBuffer;

	*ppvOut = DBGDrivGetServiceTable();

    return(IMG_TRUE);
}

#if defined(__QNXNTO__)
/*****************************************************************************
 FUNCTION	:	DBGIODrivCreateStream

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivCreateStream(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer, IMG_BOOL bCompat)
{
	PDBG_IN_CREATESTREAM psIn;
	PDBG_OUT_CREATESTREAM psOut;

	PVR_UNREFERENCED_PARAMETER(bCompat);

	psIn = (PDBG_IN_CREATESTREAM) pvInBuffer;
	psOut = (PDBG_OUT_CREATESTREAM) pvOutBuffer;

	return (ExtDBGDrivCreateStream(psIn->u.pszName, DEBUG_FLAGS_NO_BUF_EXPANDSION, psIn->ui32Pages, &psOut->phInit, &psOut->phMain, &psOut->phDeinit));
}
#endif

/*****************************************************************************
 FUNCTION	:	DBGDIOCDrivGetStream

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivGetStream(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer, IMG_BOOL bCompat)
{
	PDBG_IN_FINDSTREAM psParams;
	IMG_SID *	phStream;

	psParams	= (PDBG_IN_FINDSTREAM)pvInBuffer;
	phStream	= (IMG_SID *)pvOutBuffer;

	*phStream = PStream2SID(ExtDBGDrivFindStream(WIDEPTR_GET_PTR(psParams->pszName, bCompat), psParams->bResetStream));

	return(IMG_TRUE);
}

/*****************************************************************************
 FUNCTION	:	DBGDIOCDrivRead

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivRead(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer, IMG_BOOL bCompat)
{
	IMG_UINT32 *	pui32BytesCopied;
	PDBG_IN_READ	psInParams;
	PDBG_STREAM		psStream;
	IMG_UINT8	*pui8ReadBuffer;

	psInParams = (PDBG_IN_READ) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;
	pui8ReadBuffer = WIDEPTR_GET_PTR(psInParams->pui8OutBuffer, bCompat);

	psStream = SID2PStream(psInParams->hStream);

	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		*pui32BytesCopied = ExtDBGDrivRead(psStream,
									   psInParams->ui32BufID,
									   psInParams->ui32OutBufferSize,
									   pui8ReadBuffer);
		return(IMG_TRUE);
	}
	else
	{
		/* invalid SID */
		*pui32BytesCopied = 0;
		return(IMG_FALSE);
	}
}

/*****************************************************************************
 FUNCTION	: DBGDIOCDrivSetMarker

 PURPOSE	: Sets the marker in the stream to split output files

 PARAMETERS	: pvInBuffer, pvOutBuffer

 RETURNS	: success
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivSetMarker(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer, IMG_BOOL bCompat)
{
	PDBG_IN_SETMARKER	psParams;
	PDBG_STREAM			psStream;

	psParams = (PDBG_IN_SETMARKER) pvInBuffer;
	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);
	PVR_UNREFERENCED_PARAMETER(bCompat);

	psStream = SID2PStream(psParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		ExtDBGDrivSetMarker(psStream, psParams->ui32Marker);
		return(IMG_TRUE);
	}
	else
	{
		/* invalid SID */
		return(IMG_FALSE);
	}
}

/*****************************************************************************
 FUNCTION	: DBGDIOCDrivGetMarker

 PURPOSE	: Gets the marker in the stream to split output files

 PARAMETERS	: pvInBuffer, pvOutBuffer

 RETURNS	: success
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivGetMarker(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer, IMG_BOOL bCompat)
{
	PDBG_STREAM  psStream;
	IMG_UINT32  *pui32Current;

	PVR_UNREFERENCED_PARAMETER(bCompat);

	pui32Current = (IMG_UINT32 *) pvOutBuffer;

	psStream = SID2PStream(*(IMG_SID *)pvInBuffer);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		*pui32Current = ExtDBGDrivGetMarker(psStream);
		return(IMG_TRUE);
	}
	else
	{
		/* invalid SID */
		*pui32Current = 0;
		return(IMG_FALSE);
	}
}


/*****************************************************************************
 FUNCTION	:	DBGDIOCDrivWaitForEvent

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivWaitForEvent(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer, IMG_BOOL bCompat)
{
	DBG_EVENT eEvent = (DBG_EVENT)(*(IMG_UINT32 *)pvInBuffer);

	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);
	PVR_UNREFERENCED_PARAMETER(bCompat);

	ExtDBGDrivWaitForEvent(eEvent);

	return(IMG_TRUE);
}


/*****************************************************************************
 FUNCTION	: DBGDIOCDrivGetFrame

 PURPOSE	: Gets the marker in the stream to split output files

 PARAMETERS	: pvInBuffer, pvOutBuffer

 RETURNS	: success
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivGetFrame(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer, IMG_BOOL bCompat)
{
	IMG_UINT32  *pui32Current;

	PVR_UNREFERENCED_PARAMETER(pvInBuffer);
	PVR_UNREFERENCED_PARAMETER(bCompat);

	pui32Current = (IMG_UINT32 *) pvOutBuffer;

	*pui32Current = ExtDBGDrivGetFrame();

	return(IMG_TRUE);
}

/*
	ioctl interface jump table.
	Accessed from the UM debug driver client
*/
IMG_UINT32 (*g_DBGDrivProc[DEBUG_SERVICE_MAX_API])(IMG_VOID *, IMG_VOID *, IMG_BOOL) =
{
	DBGDIOCDrivGetServiceTable, /* WDDM only for KMD to retrieve address from DBGDRV, Not used by umdbgdrvlnx */
	DBGDIOCDrivGetStream,
	DBGDIOCDrivRead,
	DBGDIOCDrivSetMarker,
	DBGDIOCDrivGetMarker,
	DBGDIOCDrivWaitForEvent,
	DBGDIOCDrivGetFrame,
#if defined(__QNXNTO__)
	DBGDIOCDrivCreateStream
#endif
};

/*****************************************************************************
 End of file (IOCTL.C)
*****************************************************************************/
