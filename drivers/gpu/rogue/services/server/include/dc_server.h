/**************************************************************************/ /*!
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
*/ /***************************************************************************/

#ifndef _DC_SERVER_H_
#define _DC_SERVER_H_

#include "img_types.h"
#include "pvrsrv_error.h"
#include "sync_external.h"
#include "pvrsrv_surface.h"
#include "pmr.h"
#include "kerneldisplay.h"
#include "sync_server.h"

typedef struct _DC_DEVICE_ DC_DEVICE;
typedef struct _DC_DISPLAY_CONTEXT_ DC_DISPLAY_CONTEXT;
typedef struct _DC_BUFFER_ DC_BUFFER;
typedef DC_BUFFER* DC_PIN_HANDLE;

PVRSRV_ERROR DCDevicesQueryCount(IMG_UINT32 *pui32DeviceCount);

PVRSRV_ERROR DCDevicesEnumerate(IMG_UINT32 ui32DeviceArraySize,
								IMG_UINT32 *pui32DeviceCount,
								IMG_UINT32 *paui32DeviceIndex);

PVRSRV_ERROR DCDeviceAcquire(IMG_UINT32 ui32DeviceIndex,
							 DC_DEVICE **ppsDevice);

PVRSRV_ERROR DCDeviceRelease(DC_DEVICE *psDevice);

PVRSRV_ERROR DCGetInfo(DC_DEVICE *psDevice,
					   DC_DISPLAY_INFO *psDisplayInfo);

PVRSRV_ERROR DCPanelQueryCount(DC_DEVICE *psDevice,
								IMG_UINT32 *pui32NumPanels);

PVRSRV_ERROR DCPanelQuery(DC_DEVICE *psDevice,
						   IMG_UINT32 ui32PanelsArraySize,
						   IMG_UINT32 *pui32NumPanels,
						   PVRSRV_PANEL_INFO *pasPanelInfo);

PVRSRV_ERROR DCFormatQuery(DC_DEVICE *psDevice,
							 IMG_UINT32 ui32FormatArraySize,
							 PVRSRV_SURFACE_FORMAT *pasFormat,
							 IMG_UINT32 *pui32Supported);

PVRSRV_ERROR DCDimQuery(DC_DEVICE *psDevice,
						  IMG_UINT32 ui32DimSize,
						  PVRSRV_SURFACE_DIMS *pasDim,
						  IMG_UINT32 *pui32Supported);

PVRSRV_ERROR DCSetBlank(DC_DEVICE *psDevice,
						IMG_BOOL bEnabled);

PVRSRV_ERROR DCSetVSyncReporting(DC_DEVICE *psDevice,
								 IMG_BOOL bEnabled);

PVRSRV_ERROR DCLastVSyncQuery(DC_DEVICE *psDevice,
							  IMG_INT64 *pi64Timestamp);

PVRSRV_ERROR DCSystemBufferAcquire(DC_DEVICE *psDevice,
								   IMG_UINT32 *pui32ByteStride,
								   DC_BUFFER **ppsBuffer);

PVRSRV_ERROR DCSystemBufferRelease(DC_BUFFER *psBuffer);

PVRSRV_ERROR DCDisplayContextCreate(DC_DEVICE *psDevice,
									DC_DISPLAY_CONTEXT **ppsDisplayContext);

PVRSRV_ERROR DCDisplayContextFlush(IMG_VOID);

PVRSRV_ERROR DCDisplayContextConfigureCheck(DC_DISPLAY_CONTEXT *psDisplayContext,
											IMG_UINT32 ui32PipeCount,
											PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
											DC_BUFFER **papsBuffers);

PVRSRV_ERROR DCDisplayContextConfigure(DC_DISPLAY_CONTEXT *psDisplayContext,
									   IMG_UINT32 ui32PipeCount,
									   PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
									   DC_BUFFER **papsBuffers,
									   IMG_UINT32 ui32SyncOpCount,
									   SERVER_SYNC_PRIMITIVE **papsSync,
									   IMG_BOOL *pabUpdate,
									   IMG_UINT32 ui32DisplayPeriod,
									   IMG_UINT32 ui32MaxDepth,
									   IMG_INT32 i32AcquireFenceFd,
									   IMG_INT32 *pi32ReleaseFenceFd);

PVRSRV_ERROR DCDisplayContextDestroy(DC_DISPLAY_CONTEXT *psDisplayContext);

PVRSRV_ERROR DCBufferAlloc(DC_DISPLAY_CONTEXT *psDisplayContext,
						   DC_BUFFER_CREATE_INFO *psSurfInfo,
						   IMG_UINT32 *pui32ByteStride,
						   DC_BUFFER **ppsBuffer);

PVRSRV_ERROR DCBufferFree(DC_BUFFER *psBuffer);

PVRSRV_ERROR DCBufferImport(DC_DISPLAY_CONTEXT *psDisplayContext,
							IMG_UINT32 ui32NumPlanes,
							PMR **papsImport,
						    DC_BUFFER_IMPORT_INFO *psSurfAttrib,
						    DC_BUFFER **ppsBuffer);

PVRSRV_ERROR DCBufferUnimport(DC_BUFFER *psBuffer);

PVRSRV_ERROR DCBufferAcquire(DC_BUFFER *psBuffer,
							 PMR **psPMR);

PVRSRV_ERROR DCBufferRelease(PMR *psPMR);

PVRSRV_ERROR DCBufferPin(DC_BUFFER *psBuffer, DC_PIN_HANDLE *phPin);

PVRSRV_ERROR DCBufferUnpin(DC_PIN_HANDLE hPin);

PVRSRV_ERROR DCInit(IMG_VOID);
PVRSRV_ERROR DCDeInit(IMG_VOID);

#endif /*_DC_SERVER_H_  */
