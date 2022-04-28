/*************************************************************************/ /*!
@File
@Title          Services Device Variable interface header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines the client side interface for device variables
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

#ifndef PVRSRV_DEVVAR_H
#define PVRSRV_DEVVAR_H

#include "img_types.h"
#include "img_defs.h"
#include "pvr_debug.h"
#include <powervr/pvrsrv_sync_ext.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define DEVVAR_MAX_NAME_LEN 32

typedef struct _PVRSRV_DEVVARCTX_ *PDEVVARCTX;
typedef struct _PVRSRV_DEVVAR_    *PDEVVAR;

typedef struct PVRSRV_DEV_VAR_UPDATE_TAG
{
	PDEVVAR					psDevVar;			/*!< Pointer to the dev var */
	IMG_UINT32				ui32UpdateValue;	/*!< the update value */
} PVRSRV_DEV_VAR_UPDATE;

/*************************************************************************/ /*!
@Function       PVRSRVDevVarContextCreate

@Description    Create a new device variable context

@Input          psDevConnection         Device to create the device
                                        variable context on

@Output         phDevVarContext         Handle to the created device
                                        variable context

@Return         PVRSRV_OK if the device variable context was successfully
                created
*/
/*****************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVDevVarContextCreate(const PVRSRV_DEV_CONNECTION *psDevConnection,
                          PDEVVARCTX                  *phDevVarContext);

/*************************************************************************/ /*!
@Function       PVRSRVDevVarContextDestroy

@Description    Destroy a device variable context

@Input          hDevVarContext          Handle to the device variable
                                        context to destroy

@Return         None
*/
/*****************************************************************************/
IMG_EXPORT void
PVRSRVDevVarContextDestroy(PDEVVARCTX hDevVarContext);

/*************************************************************************/ /*!
@Function       PVRSRVDevVarAlloc

@Description    Allocate a new device variable on the specified device
                variable context. The device variable's value is initialised
                with the value passed in ui32InitialValue.

@Input          hDevVarContext          Handle to the device variable
                                        context
@Input          ui32InitialValue        Value to initially assign to the
                                        new variable
@Input          pszDevVarName           Name assigned to the device variable
                                        (for debug purposes)

@Output         ppsDevVar               Created device variable

@Return         PVRSRV_OK if the device variable was successfully created
*/
/*****************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVDevVarAllocI(PDEVVARCTX hDevVarContext,
                   PDEVVAR *ppsDevVar,
                   IMG_UINT32 ui32InitialValue,
                   const IMG_CHAR *pszDevVarName
                   PVR_DBG_FILELINE_PARAM);
#define PVRSRVDevVarAlloc(hDevVarContext, ppsDevVar, ui32InitialValue, pszDevVarName) \
	PVRSRVDevVarAllocI( (hDevVarContext), (ppsDevVar), (ui32InitialValue), (pszDevVarName) \
	                    PVR_DBG_FILELINE )

/*************************************************************************/ /*!
@Function       PVRSRVDevVarFree

@Description    Free a device variable

@Input          psDevVar                The device variable to free

@Return         None
*/
/*****************************************************************************/
IMG_EXPORT void
PVRSRVDevVarFree(PDEVVAR psDevVar);

/*************************************************************************/ /*!
@Function       PVRSRVDevVarSet

@Description    Set the device variable to a value

@Input          psDevVar                The device variable to set

@Input          ui32Value               Value to set it to

@Return         None
*/
/*****************************************************************************/
IMG_EXPORT void
PVRSRVDevVarSet(PDEVVAR		psDevVar,
                IMG_UINT32	ui32Value);

/*************************************************************************/ /*!
@Function       PVRSRVDevVarGet

@Description    Get the current value of the device variable

@Input          psDevVar                The device variable to get the
                                        value of

@Return         Value of the variable
*/
/*****************************************************************************/
IMG_EXPORT IMG_UINT32
PVRSRVDevVarGet(PDEVVAR		psDevVar);

/*************************************************************************/ /*!
@Function       PVRSRVDevVarGetFirmwareAddr

@Description    Returns the address of the associated firmware value for a
                specified device integer (not exposed to client)

@Input          psDevVar               The device variable to resolve

@Return         The firmware address of the device variable
*/
/*****************************************************************************/
IMG_EXPORT IMG_UINT32
PVRSRVDevVarGetFirmwareAddr(PDEVVAR	psDevVar);

#if defined(PDUMP)
/*************************************************************************/ /*!
@Function       PVRSRVDevVarPDump

@Description    PDump the current value of the device variable

@Input          psDevVar                The device variable to PDump

@Return         None
*/
/*****************************************************************************/
IMG_EXPORT void
PVRSRVDevVarPDump(PDEVVAR psDevVar);

/*************************************************************************/ /*!
@Function       PVRSRVDevVarPDumpPol

@Description    Do a PDump poll of the device variable

@Input          psDevVar                The device variable to PDump

@Input          ui32Value               Value to Poll for

@Input          ui32Mask                PDump mask operator

@Input          ui32PDumpFlags          PDump flags

@Return         None
*/
/*****************************************************************************/
IMG_EXPORT void
PVRSRVDevVarPDumpPol(PDEVVAR psDevVar,
                     IMG_UINT32 ui32Value,
                     IMG_UINT32 ui32Mask,
                     PDUMP_POLL_OPERATOR eOperator,
                     IMG_UINT32 ui32PDumpFlags);

/*************************************************************************/ /*!
@Function       PVRSRVDevVarPDumpCBP

@Description    Do a PDump CB poll using the device variable

@Input          psDevVar                The device variable to PDump

@Input          uiWriteOffset           Current write offset of buffer

@Input          uiPacketSize            Size of the packet to write into CB

@Input          uiBufferSize            Size of the CB

@Return         None
*/
/*****************************************************************************/
IMG_EXPORT void
PVRSRVDevVarPDumpCBP(PDEVVAR psDevVar,
                     IMG_UINT64 uiWriteOffset,
                     IMG_UINT64 uiPacketSize,
                     IMG_UINT64 uiBufferSize);
#else /* PDUMP */

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVDevVarPDump)
#endif
static INLINE void
PVRSRVDevVarPDump(PDEVVAR psDevVar)
{
	PVR_UNREFERENCED_PARAMETER(psDevVar);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVDevVarPDumpPol)
#endif
static INLINE void
PVRSRVDevVarPDumpPol(PDEVVAR psDevVar,
                     IMG_UINT32 ui32Value,
                     IMG_UINT32 ui32Mask,
                     PDUMP_POLL_OPERATOR eOperator,
                     IMG_UINT32 ui32PDumpFlags)
{
	 PVR_UNREFERENCED_PARAMETER(psDevVar);
	 PVR_UNREFERENCED_PARAMETER(ui32Value);
	 PVR_UNREFERENCED_PARAMETER(ui32Mask);
	 PVR_UNREFERENCED_PARAMETER(eOperator);
	 PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVDevVarPDumpCBP)
#endif
static INLINE void
PVRSRVDevVarPDumpCBP(PDEVVAR psDevVar,
                     IMG_UINT64 uiWriteOffset,
                     IMG_UINT64 uiPacketSize,
                     IMG_UINT64 uiBufferSize)
{
	PVR_UNREFERENCED_PARAMETER(psDevVar);
	PVR_UNREFERENCED_PARAMETER(uiWriteOffset);
	PVR_UNREFERENCED_PARAMETER(uiPacketSize);
	PVR_UNREFERENCED_PARAMETER(uiBufferSize);
}
#endif /* PDUMP */

#if defined(__cplusplus)
}
#endif
#endif /* PVRSRV_DEVVAR_H */
