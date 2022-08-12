/**************************************************************************/ /*!
@File
@Title          Server side connection management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    API for OS specific callbacks from server side connection
                management
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
*/ /***************************************************************************/
#ifndef OSCONNECTION_SERVER_H
#define OSCONNECTION_SERVER_H

#include "handle.h"
#include "osfunc.h"

/*! Function not implemented definition. */
#define OSCONNECTION_SERVER_NOT_IMPLEMENTED 0
/*! Assert used for OSCONNECTION_SERVER_NOT_IMPLEMENTED. */
#define OSCONNECTION_SERVER_NOT_IMPLEMENTED_ASSERT() PVR_ASSERT(OSCONNECTION_SERVER_NOT_IMPLEMENTED)

#if defined(__linux__) || defined(__QNXNTO__) || defined(INTEGRITY_OS)
PVRSRV_ERROR OSConnectionPrivateDataInit(IMG_HANDLE *phOsPrivateData, void *pvOSData);
PVRSRV_ERROR OSConnectionPrivateDataDeInit(IMG_HANDLE hOsPrivateData);

PVRSRV_ERROR OSConnectionSetHandleOptions(PVRSRV_HANDLE_BASE *psHandleBase);

PVRSRV_DEVICE_NODE* OSGetDevNode(CONNECTION_DATA *psConnection);

#else	/* defined(__linux__) || defined(__QNXNTO__) || defined(INTEGRITY_OS) */
#ifdef INLINE_IS_PRAGMA
#pragma inline(OSConnectionPrivateDataInit)
#endif
/*************************************************************************/ /*!
@Function       OSConnectionPrivateDataInit
@Description    Allocates and initialises any OS-specific private data
                relating to a connection.
                Called from PVRSRVCommonConnectionConnect().
@Input          pvOSData            pointer to any OS private data
@Output         phOsPrivateData     handle to the created connection
                                    private data
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
static INLINE PVRSRV_ERROR OSConnectionPrivateDataInit(IMG_HANDLE *phOsPrivateData, void *pvOSData)
{
	PVR_UNREFERENCED_PARAMETER(phOsPrivateData);
	PVR_UNREFERENCED_PARAMETER(pvOSData);

	OSCONNECTION_SERVER_NOT_IMPLEMENTED_ASSERT();

	return PVRSRV_ERROR_NOT_IMPLEMENTED;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSConnectionPrivateDataDeInit)
#endif
/*************************************************************************/ /*!
@Function       OSConnectionPrivateDataDeInit
@Description    Frees previously allocated OS-specific private data
                relating to a connection.
@Input          hOsPrivateData      handle to the connection private data
                                    to be freed
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
static INLINE PVRSRV_ERROR OSConnectionPrivateDataDeInit(IMG_HANDLE hOsPrivateData)
{
	PVR_UNREFERENCED_PARAMETER(hOsPrivateData);

	OSCONNECTION_SERVER_NOT_IMPLEMENTED_ASSERT();

	return PVRSRV_ERROR_NOT_IMPLEMENTED;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSConnectionSetHandleOptions)
#endif
static INLINE PVRSRV_ERROR OSConnectionSetHandleOptions(PVRSRV_HANDLE_BASE *psHandleBase)
{
	PVR_UNREFERENCED_PARAMETER(psHandleBase);

	OSCONNECTION_SERVER_NOT_IMPLEMENTED_ASSERT();

	return PVRSRV_ERROR_NOT_IMPLEMENTED;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(OSGetDevNode)
#endif
static INLINE PVRSRV_DEVICE_NODE* OSGetDevNode(CONNECTION_DATA *psConnection)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);

	OSCONNECTION_SERVER_NOT_IMPLEMENTED_ASSERT();

	return NULL;
}
#endif	/* defined(__linux__) || defined(__QNXNTO__) || defined(INTEGRITY_OS) */


#endif /* OSCONNECTION_SERVER_H */
