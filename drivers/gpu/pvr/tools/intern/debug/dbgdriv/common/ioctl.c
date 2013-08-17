/*************************************************************************/ /*!
@Title          IOCTL implementations for debug device.
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


#ifdef LINUX
#include <asm/uaccess.h>
#include "pvr_uaccess.h"
#endif /* LINUX */

#include "img_types.h"
#include "dbgdrvif.h"
#include "dbgdriv.h"
#include "hotkey.h"
#include "dbgdriv_ioctl.h"


/*****************************************************************************
 Code
*****************************************************************************/

/*****************************************************************************
 FUNCTION	:	DBGDrivCreateStream

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivCreateStream(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_CREATESTREAM psIn;
	IMG_VOID * *ppvOut;
	#ifdef LINUX
	static IMG_CHAR name[32];
	#endif

	psIn = (PDBG_IN_CREATESTREAM) pvInBuffer;
	ppvOut = (IMG_VOID * *) pvOutBuffer;

	#ifdef LINUX

	if(pvr_copy_from_user(name, psIn->u.pszName, 32) != 0)
	{
		return IMG_FALSE;
	}

	*ppvOut = ExtDBGDrivCreateStream(name, psIn->ui32CapMode, psIn->ui32OutMode, 0, psIn->ui32Pages);

	#else
	*ppvOut = ExtDBGDrivCreateStream(psIn->u.pszName, psIn->ui32CapMode, psIn->ui32OutMode, DEBUG_FLAGS_NO_BUF_EXPANDSION, psIn->ui32Pages);
	#endif


	return(IMG_TRUE);
}

/*****************************************************************************
 FUNCTION	:	DBGDrivDestroyStream

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivDestroyStream(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_STREAM *ppsStream;
	PDBG_STREAM  psStream;

	ppsStream = (PDBG_STREAM *) pvInBuffer;
	psStream  = (PDBG_STREAM) *ppsStream;

	PVR_UNREFERENCED_PARAMETER(	pvOutBuffer);

	ExtDBGDrivDestroyStream(psStream);

	return(IMG_TRUE);
}

/*****************************************************************************
 FUNCTION	:	DBGDrivGetStream

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivGetStream(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_FINDSTREAM psParams;
	IMG_SID *	phStream;

	psParams	= (PDBG_IN_FINDSTREAM)pvInBuffer;
	phStream	= (IMG_SID *)pvOutBuffer;

	*phStream = PStream2SID(ExtDBGDrivFindStream(psParams->u.pszName, psParams->bResetStream));

	return(IMG_TRUE);
}

/*****************************************************************************
 FUNCTION	:	DBGDrivWriteString

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivWriteString(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_WRITESTRING psParams;
	IMG_UINT32 *pui32OutLen;
	PDBG_STREAM psStream;

	psParams = (PDBG_IN_WRITESTRING) pvInBuffer;
	pui32OutLen = (IMG_UINT32 *) pvOutBuffer;

	psStream = SID2PStream(psParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		*pui32OutLen = ExtDBGDrivWriteString(psStream,psParams->u.pszString,psParams->ui32Level);
		return(IMG_TRUE);
	}
	else
	{
		/* invalid SID */
		*pui32OutLen = 0;
		return(IMG_FALSE);
	}
}

/*****************************************************************************
 FUNCTION	:	DBGDrivWriteStringCM

 PURPOSE	:	Same as DBGDrivWriteString, but takes notice of capture mode.

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivWriteStringCM(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_WRITESTRING psParams;
	IMG_UINT32 *pui32OutLen;
	PDBG_STREAM psStream;

	psParams = (PDBG_IN_WRITESTRING) pvInBuffer;
	pui32OutLen = (IMG_UINT32 *) pvOutBuffer;

	psStream = SID2PStream(psParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		*pui32OutLen = ExtDBGDrivWriteStringCM(psStream,psParams->u.pszString,psParams->ui32Level);
		return(IMG_TRUE);
	}
	else
	{
		/* invalid SID */
		*pui32OutLen = 0;
		return(IMG_FALSE);
	}
}

/*****************************************************************************
 FUNCTION	:	DBGDrivReadString

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivReadString(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 * pui32OutLen;
	PDBG_IN_READSTRING	psParams;
	PDBG_STREAM  psStream;

	psParams = (PDBG_IN_READSTRING) pvInBuffer;
	pui32OutLen = (IMG_UINT32 *) pvOutBuffer;

	psStream = SID2PStream(psParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		*pui32OutLen = ExtDBGDrivReadString(psStream,
											psParams->u.pszString,psParams->ui32StringLen);
		return(IMG_TRUE);
	}
	else
	{
		/* invalid SID */
		*pui32OutLen = 0;
		return(IMG_FALSE);
	}
}

/*****************************************************************************
 FUNCTION	:	DBGDrivWrite

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivWrite(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *	pui32BytesCopied;
	PDBG_IN_WRITE	psInParams;
	PDBG_STREAM		psStream;

	psInParams = (PDBG_IN_WRITE) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;

	psStream = SID2PStream(psInParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		*pui32BytesCopied = ExtDBGDrivWrite(psStream,
										psInParams->u.pui8InBuffer,
										psInParams->ui32TransferSize,
										psInParams->ui32Level);
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
 FUNCTION	:	DBGDrivWrite2

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivWrite2(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *	pui32BytesCopied;
	PDBG_IN_WRITE	psInParams;
	PDBG_STREAM 	psStream;

	psInParams = (PDBG_IN_WRITE) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;

	psStream = SID2PStream(psInParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		*pui32BytesCopied = ExtDBGDrivWrite2(psStream,
										 psInParams->u.pui8InBuffer,
										 psInParams->ui32TransferSize,
										 psInParams->ui32Level);
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
 FUNCTION	:	DBGDrivWriteCM

 PURPOSE	:	Same as DBGDIOCDrivWrite2, but takes notice of capture mode.

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivWriteCM(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *	pui32BytesCopied;
	PDBG_IN_WRITE	psInParams;
	PDBG_STREAM		psStream;

	psInParams = (PDBG_IN_WRITE) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;

	psStream = SID2PStream(psInParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		*pui32BytesCopied = ExtDBGDrivWriteCM(psStream,
										  psInParams->u.pui8InBuffer,
										  psInParams->ui32TransferSize,
										  psInParams->ui32Level);
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
 FUNCTION	:	DBGDrivRead

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivRead(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *	pui32BytesCopied;
	PDBG_IN_READ	psInParams;
	PDBG_STREAM		psStream;

	psInParams = (PDBG_IN_READ) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;

	psStream = SID2PStream(psInParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		*pui32BytesCopied = ExtDBGDrivRead(psStream,
									   psInParams->bReadInitBuffer,
									   psInParams->ui32OutBufferSize,
									   psInParams->u.pui8OutBuffer);
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
 FUNCTION	:	DBGDIOCDrivSetCaptureMode

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivSetCaptureMode(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_SETDEBUGMODE	psParams;
	PDBG_STREAM				psStream;

	psParams = (PDBG_IN_SETDEBUGMODE) pvInBuffer;
	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);

	psStream = SID2PStream(psParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		ExtDBGDrivSetCaptureMode(psStream,
							 psParams->ui32Mode,
							 psParams->ui32Start,
							 psParams->ui32End,
							 psParams->ui32SampleRate);
		return(IMG_TRUE);
	}
	else
	{
		/* invalid SID */
		return(IMG_FALSE);
	}
}

/*****************************************************************************
 FUNCTION	:	DBGDIOCDrivSetOutMode

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivSetOutMode(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_SETDEBUGOUTMODE psParams;
	PDBG_STREAM				psStream;

	psParams = (PDBG_IN_SETDEBUGOUTMODE) pvInBuffer;
	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);

	psStream = SID2PStream(psParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		ExtDBGDrivSetOutputMode(psStream,psParams->ui32Mode);
		return(IMG_TRUE);
	}
	else
	{
		/* invalid SID */
		return(IMG_FALSE);
	}
}

/*****************************************************************************
 FUNCTION	:	DBGDIOCDrivSetDebugLevel

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivSetDebugLevel(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_SETDEBUGLEVEL psParams;
	PDBG_STREAM           psStream;

	psParams = (PDBG_IN_SETDEBUGLEVEL) pvInBuffer;
	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);

	psStream = SID2PStream(psParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		ExtDBGDrivSetDebugLevel(psStream,psParams->ui32Level);
		return(IMG_TRUE);
	}
	else
	{
		/* invalid SID */
		return(IMG_FALSE);
	}
}

/*****************************************************************************
 FUNCTION	:	DBGDrivSetFrame

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivSetFrame(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_SETFRAME	psParams;
	PDBG_STREAM			psStream;

	psParams = (PDBG_IN_SETFRAME) pvInBuffer;
	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);

	psStream = SID2PStream(psParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		ExtDBGDrivSetFrame(psStream,psParams->ui32Frame);
		return(IMG_TRUE);
	}
	else
	{
		/* invalid SID */
		return(IMG_FALSE);
	}
}

/*****************************************************************************
 FUNCTION	:	DBGDrivGetFrame

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivGetFrame(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_STREAM  psStream;
	IMG_UINT32  *pui32Current;

	pui32Current = (IMG_UINT32 *) pvOutBuffer;
	psStream = SID2PStream(*(IMG_SID *)pvInBuffer);
	
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		*pui32Current = ExtDBGDrivGetFrame(psStream);
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
 FUNCTION	:	DBGDIOCDrivIsCaptureFrame

 PURPOSE	:	Determines if this frame is a capture frame

 PARAMETERS	:

 RETURNS	:	IMG_TRUE if current frame is to be captured
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivIsCaptureFrame(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_ISCAPTUREFRAME psParams;
	IMG_UINT32 *			pui32Current;
	PDBG_STREAM				psStream;

	psParams = (PDBG_IN_ISCAPTUREFRAME) pvInBuffer;
	pui32Current = (IMG_UINT32 *) pvOutBuffer;

	psStream = SID2PStream(psParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		*pui32Current = ExtDBGDrivIsCaptureFrame(psStream,
											 psParams->bCheckPreviousFrame);
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
 FUNCTION	:	DBGDrivOverrideMode

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivOverrideMode(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_OVERRIDEMODE	psParams;
	PDBG_STREAM				psStream;

	psParams = (PDBG_IN_OVERRIDEMODE) pvInBuffer;
	PVR_UNREFERENCED_PARAMETER(	pvOutBuffer);

	psStream = SID2PStream(psParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		ExtDBGDrivOverrideMode(psStream,psParams->ui32Mode);
		return(IMG_TRUE);
	}
	else
	{
		/* invalid SID */
		return(IMG_FALSE);
	}
}

/*****************************************************************************
 FUNCTION	:	DBGDrivDefaultMode

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivDefaultMode(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_STREAM  psStream;

	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);

	psStream = SID2PStream(*(IMG_SID *)pvInBuffer);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		ExtDBGDrivDefaultMode(psStream);
		return(IMG_TRUE);
	}
	else
	{
		/* invalid SID */
		return(IMG_FALSE);
	}
}

/*****************************************************************************
 FUNCTION	: DBGDIOCDrivSetMarker

 PURPOSE	: Sets the marker in the stream to split output files

 PARAMETERS	: pvInBuffer, pvOutBuffer

 RETURNS	: success
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivSetMarker(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_SETMARKER	psParams;
	PDBG_STREAM			psStream;

	psParams = (PDBG_IN_SETMARKER) pvInBuffer;
	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);

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
static IMG_UINT32 DBGDIOCDrivGetMarker(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_STREAM  psStream;
	IMG_UINT32  *pui32Current;

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
 FUNCTION	:	DBGDrivGetServiceTable

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivGetServiceTable(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_PVOID *	ppvOut;

	PVR_UNREFERENCED_PARAMETER(pvInBuffer);
	ppvOut = (IMG_PVOID *) pvOutBuffer;

	*ppvOut = DBGDrivGetServiceTable();

    return(IMG_TRUE);
}

/*****************************************************************************
 FUNCTION	:	DBGDIOCDrivWriteLF

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivWriteLF(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_WRITE_LF psInParams;
	IMG_UINT32      *pui32BytesCopied;
	PDBG_STREAM      psStream;

	psInParams = (PDBG_IN_WRITE_LF) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;

	psStream = SID2PStream(psInParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		*pui32BytesCopied = ExtDBGDrivWriteLF(psStream,
										  psInParams->u.pui8InBuffer,
										  psInParams->ui32BufferSize,
										  psInParams->ui32Level,
										  psInParams->ui32Flags);
		return(IMG_TRUE);
	}
	else
	{
		/* invalid SID */
		return(IMG_FALSE);
	}
}

/*****************************************************************************
 FUNCTION	:	DBGDIOCDrivReadLF

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivReadLF(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *	pui32BytesCopied;
	PDBG_IN_READ	psInParams;
	PDBG_STREAM		psStream;

	psInParams = (PDBG_IN_READ) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;

	psStream = SID2PStream(psInParams->hStream);
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		*pui32BytesCopied = ExtDBGDrivReadLF(psStream,
										 psInParams->ui32OutBufferSize,
										 psInParams->u.pui8OutBuffer);
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
 FUNCTION	:	DBGDIOCDrivWaitForEvent

 PURPOSE	:

 PARAMETERS	:

 RETURNS	:
*****************************************************************************/
static IMG_UINT32 DBGDIOCDrivWaitForEvent(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	DBG_EVENT eEvent = (DBG_EVENT)(*(IMG_UINT32 *)pvInBuffer);

	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);

	ExtDBGDrivWaitForEvent(eEvent);

	return(IMG_TRUE);
}

/*
	VxD DIOC interface jump table.
*/
IMG_UINT32 (*g_DBGDrivProc[25])(IMG_VOID *, IMG_VOID *) =
{
	DBGDIOCDrivCreateStream,
	DBGDIOCDrivDestroyStream,
	DBGDIOCDrivGetStream,
	DBGDIOCDrivWriteString,
	DBGDIOCDrivReadString,
	DBGDIOCDrivWrite,
	DBGDIOCDrivRead,
	DBGDIOCDrivSetCaptureMode,
	DBGDIOCDrivSetOutMode,
	DBGDIOCDrivSetDebugLevel,
	DBGDIOCDrivSetFrame,
	DBGDIOCDrivGetFrame,
	DBGDIOCDrivOverrideMode,
	DBGDIOCDrivDefaultMode,
	DBGDIOCDrivGetServiceTable,
	DBGDIOCDrivWrite2,
	DBGDIOCDrivWriteStringCM,
	DBGDIOCDrivWriteCM,
	DBGDIOCDrivSetMarker,
	DBGDIOCDrivGetMarker,
	DBGDIOCDrivIsCaptureFrame,
	DBGDIOCDrivWriteLF,
	DBGDIOCDrivReadLF,
	DBGDIOCDrivWaitForEvent
};

/*****************************************************************************
 End of file (IOCTL.C)
*****************************************************************************/
