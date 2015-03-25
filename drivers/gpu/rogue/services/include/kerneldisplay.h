/*************************************************************************/ /*!
@File
@Title          Interface for 3rd party display class (DC) drivers
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    API between services and the 3rd party DC driver and vice versa
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

#if !defined (__KERNELDISPLAY_H__)
#define __KERNELDISPLAY_H__

#if defined (__cplusplus)
extern "C" {
#endif

#include "img_types.h"
#include "pvrsrv_surface.h"
#include "dc_external.h"
#include "dc_common.h"

/*************************************************************************/ /*!
@Function       GetInfo

@Description    Query the display controller for its information structure

@Input          hDeviceData             Device private data

@Output         psDisplayInfo           Display info structure

@Return         PVRSRV_OK if the query was successful
*/
/*****************************************************************************/
typedef IMG_VOID (*GetInfo)(IMG_HANDLE hDeviceData,
							DC_DISPLAY_INFO *psDisplayInfo);

/*************************************************************************/ /*!
@Function       PanelQueryCount

@Description    Query the display controller for how many panels are
                contented to it.

@Input          hDeviceData             Device private data

@Output         pui32NumPanels         Number of panels

@Return         PVRSRV_OK if the query was successful
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PanelQueryCount)(IMG_HANDLE hDeviceData,
										 IMG_UINT32 *ppui32NumPanels);

/*************************************************************************/ /*!
@Function       PanelQuery

@Description    Query the display controller for information on what panel(s)
                are connected to it and their properties.

@Input          hDeviceData             Device private data

@Input          ui32PanelsArraySize    Size of the format and dimension
                                        array size (i.e. number of panels
                                        that can be returned)

@Output         pui32NumPanels         Number of panels returned

@Output         pasFormat               Array of formats

@Output         pasDims                 Array of dimensions

@Return         PVRSRV_OK if the query was successful
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*PanelQuery)(IMG_HANDLE hDeviceData,
									IMG_UINT32 ui32PanelsArraySize,
									IMG_UINT32 *pui32NumPanels,
									PVRSRV_PANEL_INFO *pasPanelInfo);

/*************************************************************************/ /*!
@Function       FormatQuery

@Description    Query the display controller to see if it supports the specified
                format(s).

@Input          hDeviceData             Device private data

@Input          ui32NumFormats          Number of formats to check

@Input          pasFormat               Array of formats to check

@Output			pui32Supported          For each format, the number of display
                                        pipes that support that format

@Return         PVRSRV_OK if the query was successful
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*FormatQuery)(IMG_HANDLE hDeviceData,
									IMG_UINT32 ui32NumFormats,
									PVRSRV_SURFACE_FORMAT *pasFormat,
									IMG_UINT32 *pui32Supported);

/*************************************************************************/ /*!
@Function       DimQuery

@Description    Query the specificed display plane for the display dimensions
                it supports.

@Input          hDeviceData             Device private data

@Input          ui32NumDims             Number of dimensions to check

@Input          pasDim                  Array of dimentations to check

@Output         pui32Supported          For each dimension, the number of
                                        display pipes that support that
                                         dimension

@Return         PVRSRV_OK if the query was successful
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*DimQuery)(IMG_HANDLE hDeviceData,
								 IMG_UINT32 ui32NumDims,
								 PVRSRV_SURFACE_DIMS *psDim,
								 IMG_UINT32 *pui32Supported);


/*************************************************************************/ /*!
@Function       SetBlank

@Description    Enable/disable blanking of the screen

@Input          psConnection            Services connection

@Input          hDevice                 3rd party display class device

@Input          bEnabled                Enable/Disable the blanking

@Return         PVRSRV_OK on success
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*SetBlank)(IMG_HANDLE hDeviceData,
								 IMG_BOOL bEnabled);

/*************************************************************************/ /*!
@Function       SetVSyncReporting

@Description    Enable VSync reporting by trigger the global event object on
                every vsync happened.

@Input          psConnection            Services connection

@Input          hDevice                 3rd party display class device

@Input          bEnabled                Enable/Disable the reporting

@Return         PVRSRV_OK on success
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*SetVSyncReporting)(IMG_HANDLE hDeviceData,
										  IMG_BOOL bEnabled);

/*************************************************************************/ /*!
@Function       PVRSRVDCLastVSyncQuery

@Description    Query the time the last vsync happened.

@Input          psConnection            Services connection

@Input          hDevice                 3rd party display class device

@Output         pi64Timestamp           the requested timestamp

@Return         PVRSRV_OK if the query was successful
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*LastVSyncQuery)(IMG_HANDLE hDeviceData,
									   IMG_INT64 *pi64Timestamp);

typedef PVRSRV_ERROR (*BufferSystemAcquire)(IMG_HANDLE hDeviceData,
											IMG_DEVMEM_LOG2ALIGN_T *puiLog2PageSize,
											IMG_UINT32 *pui32PageCount,
											IMG_UINT32 *pui32PhysHeapID,
											IMG_UINT32 *pui32ByteStride,
											IMG_HANDLE *phSystemBuffer);

typedef	IMG_VOID (*BufferSystemRelease)(IMG_HANDLE hSystemBuffer);

/*************************************************************************/ /*!
@Function       ContextCreate

@Description    Create display context.

@Input          hDeviceData             Device private data

@Output         hDisplayContext         Created display context

@Return         PVRSRV_OK if the context was created
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*ContextCreate)(IMG_HANDLE hDeviceData,
									  IMG_HANDLE *hDisplayContext);

/*************************************************************************/ /*!
@Function       ContextConfigureCheck

@Description    Check to see if a configuration is valid for the display
                controller.

                Note: This function is optional

@Input          hDisplayContext         Display context

@Input          ui32PipeCount           Number of display pipes to configure

@Input          pasSurfAttrib           Array of surface attributes (one for
                                        each display plane)

@Input          ahBuffers               Array of buffers (one for
                                        each display plane)

@Return         PVRSRV_OK if the configuration is valid
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*ContextConfigureCheck)(IMG_HANDLE hDisplayContext,
											  IMG_UINT32 ui32PipeCount,
											  PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
											  IMG_HANDLE *ahBuffers);

/*************************************************************************/ /*!
@Function       ContextConfigure

@Description    Configuration the display pipeline.

@Input          hDisplayContext         Display context

@Input          ui32PipeCount           Number of display pipes to configure

@Input          pasSurfAttrib           Array of surface attributes (one for
                                        each display plane)

@Input          ahBuffers               Array of buffers (one for
                                        each display plane)

@Input          ui32DisplayPeriod		The number of VSync periods this
                                        configuration should be displayed for

@Input          hConfigData             Config handle which gets passed to
                                        DisplayConfigurationRetired when this
                                        configuration is retired

@Return         PVRSRV_OK if the configuration was successfully queued
*/
/*****************************************************************************/
typedef IMG_VOID (*ContextConfigure)(IMG_HANDLE hDisplayContext,
									 IMG_UINT32 ui32PipeCount,
									 PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
									 IMG_HANDLE *ahBuffers,
									 IMG_UINT32 ui32DisplayPeriod,
									 IMG_HANDLE hConfigData);

/*************************************************************************/ /*!
@Function       ContextDestroy

@Description    Destroy a display context.

@Input          hDisplayContext         Display context to destroy

@Return         None
*/
/*****************************************************************************/
typedef IMG_VOID (*ContextDestroy)(IMG_HANDLE hDisplayContext);

/*************************************************************************/ /*!
@Function       BufferAlloc

@Description    Allocate a display buffer. This is a request to the display
                controller to allocate a buffer from memory that is addressable
                by the display controller.

                Note: The actual allocation of display memory can be deferred
                until the first call to acquire, but the handle for the buffer
                still needs to be created and returned to the caller as well
                as some information about the buffer that's required upfront.

@Input          hDisplayContext         Display context this buffer will be
                                        used on

@Input          psSurfInfo              Attributes of the buffer

@Output         puiLog2PageSize         Log2 of the pagesize of the buffer

@Output         pui32PageCount          Number of pages in the buffer

@Output         pui32PhysHeapID         Physcial heap ID to use

@Output         pui32ByteStride         Stride (in bytes) of allocated buffer

@Output         phBuffer                Handle to allocated buffer

@Return         PVRSRV_OK if the buffer was successfully allocated
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*BufferAlloc)(IMG_HANDLE hDisplayContext,
									DC_BUFFER_CREATE_INFO *psSurfInfo,
									IMG_DEVMEM_LOG2ALIGN_T *puiLog2PageSize,
									IMG_UINT32 *pui32PageCount,
									IMG_UINT32 *pui32PhysHeapID,
									IMG_UINT32 *pui32ByteStride,
									IMG_HANDLE *phBuffer);

/*************************************************************************/ /*!
@Function       BufferImport

@Description    Import memory allocated from an external source to the display
                controller. The DC checks to see if the import is compatible
                and potentially sets up HW to map the imported buffer, although
                this isn't require to happen until the first call to DCBufferMap

                Note: This is optional

@Input          hDisplayContext         Display context this buffer will be
                                        used on

@Input          ui32NumPlanes           Number of planes

@Input          pahImport               Array of handles (one per colour channel)

@Input          psSurfAttrib            Surface attributes of the buffer

@Output         phBuffer                Handle to imported buffer

@Return         PVRSRV_OK if the buffer was successfully imported
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*BufferImport)(IMG_HANDLE hDisplayContext,
									 IMG_UINT32 ui32NumPlanes,
									 IMG_HANDLE **paphImport,
									 DC_BUFFER_IMPORT_INFO *psSurfAttrib,
									 IMG_HANDLE *phBuffer);

/*************************************************************************/ /*!
@Function       BufferAcquire

@Description    Acquire the buffer's physcial memory pages. If the buffer doesn't
                have any memory backing it yet then this will trigger the 3rd
                party driver to allocate it.

                Note: The page count isn't passed back in this function as
                services has already obtained it during BufferAlloc.

@Input          hBuffer                 Handle to the buffer

@Output         pasDevPAddr             Array of device physical page address
                                        of this buffer

@Output         pvLinAddr               CPU virtual address of buffer. This is
                                        optionial but if you have one you must
                                        return it otherwise return NULL.

@Return         PVRSRV_OK if the buffer was successfully acquired
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*BufferAcquire)(IMG_HANDLE hBuffer,
									  IMG_DEV_PHYADDR *pasDevPAddr,
									  IMG_PVOID *ppvLinAddr);

/*************************************************************************/ /*!
@Function       BufferRelease

@Description    Release the buffer's physcial memory pages.

@Input          hBuffer                 Handle to the buffer

@Return         None
*/
/*****************************************************************************/
typedef IMG_VOID (*BufferRelease)(IMG_HANDLE hBuffer);

/*************************************************************************/ /*!
@Function       BufferFree

@Description    Release a reference to the device buffer. If this was the last
                reference the 3rd party driver is entitled to free the backing
                memory.

@Input          hBuffer                 Buffer handle we're releasing

@Return         None
*/
/*****************************************************************************/
typedef IMG_VOID (*BufferFree)(IMG_HANDLE hBuffer);

/*************************************************************************/ /*!
@Function       BufferMap

@Description    Map the buffer into the display controller

                Note: This function is optional

@Input          hBuffer                 Buffer to map

@Return         PVRSRV_OK if the buffer was successfully mapped
*/
/*****************************************************************************/
typedef PVRSRV_ERROR (*BufferMap)(IMG_HANDLE hBuffer);

/*************************************************************************/ /*!
@Function       BufferUnmap

@Description    Unmap a buffer from the display controller

                Note: This function is optional

@Input          hBuffer                 Buffer to unmap

@Return         None
*/
/*****************************************************************************/
typedef IMG_VOID (*BufferUnmap)(IMG_HANDLE hBuffer);


/*
	Function table for server->display
*/
typedef struct _DC_DEVICE_FUNCTIONS_
{
	/*! Mandatory query functions */
	GetInfo						pfnGetInfo;
	PanelQueryCount				pfnPanelQueryCount;
	PanelQuery					pfnPanelQuery;
	FormatQuery					pfnFormatQuery;
	DimQuery					pfnDimQuery;

	/*! Optional blank/vsync function */
	SetBlank		            pfnSetBlank;
	SetVSyncReporting		    pfnSetVSyncReporting;
	LastVSyncQuery				pfnLastVSyncQuery;

	/*! Mandatory configure function */
	ContextCreate				pfnContextCreate;
	ContextDestroy				pfnContextDestroy;
	ContextConfigure			pfnContextConfigure;

	/*! Optional context function */
	ContextConfigureCheck		pfnContextConfigureCheck;

	/*! Mandatory buffer functions */
	BufferAlloc					pfnBufferAlloc;
	BufferAcquire				pfnBufferAcquire;
	BufferRelease				pfnBufferRelease;
	BufferFree					pfnBufferFree;

	/*! Optional buffer functions, pfnBufferMap and pfnBufferUnmap are paired
		functions, provide both or neither */
	BufferImport				pfnBufferImport;
	BufferMap					pfnBufferMap;
	BufferUnmap					pfnBufferUnmap;
	BufferSystemAcquire			pfnBufferSystemAcquire;
	BufferSystemRelease			pfnBufferSystemRelease;
} DC_DEVICE_FUNCTIONS;


/*
	functions exported by kernel services for use by 3rd party kernel display
	class device driver
*/

/*************************************************************************/ /*!
@Function       DCRegisterDevice

@Description    Register a display class device

@Input          psFuncTable             Callback function table

@Input          ui32MaxConfigsInFlight  The maximum number of configs that this
                                        display device can have in-flight.

@Input          hDeviceData             3rd party device handle, passed into
                                        DC callbacks

@Output         phSrvHandle             Services handle to pass back into
                                        UnregisterDCDevice

@Return         PVRSRV_OK if the display class driver was successfully registered
*/
/*****************************************************************************/
PVRSRV_ERROR DCRegisterDevice(DC_DEVICE_FUNCTIONS *psFuncTable,
							  IMG_UINT32 ui32MaxConfigsInFlight,
							  IMG_HANDLE hDeviceData,
							  IMG_HANDLE *phSrvHandle);

/*************************************************************************/ /*!
@Function       DCUnregisterDevice

@Description    Unregister a display class device

@Input          hDevice                Services device handle

@Return         None
*/
/*****************************************************************************/
IMG_VOID DCUnregisterDevice(IMG_HANDLE hSrvHandle);

/*************************************************************************/ /*!
@Function       DCDisplayConfigurationRetired

@Description    Called when a configuration as been retired due to a new
                configuration now being active.

@Input          hConfigData             ConfigData that is being retired

@Return         None
*/
/*****************************************************************************/
IMG_VOID DCDisplayConfigurationRetired(IMG_HANDLE hConfigData);

/*************************************************************************/ /*!
@Function       DCImportBufferAcquire

@Description    Acquire information about a buffer that was imported with
                BufferImport.

@Input          hImport                 Import buffer

@Input          uiLog2PageSize          Log 2 of the DC's page size

@Output         pui32PageCount          Size of the buffer in pages

@Output         ppasDevPAddr            Array of device physcial page address
                                        of this buffer

@Return         PVRSRV_OK if the import buffer was successfully acquired
*/
/*****************************************************************************/
PVRSRV_ERROR DCImportBufferAcquire(IMG_HANDLE hImport,
								   IMG_DEVMEM_LOG2ALIGN_T uiLog2PageSize,
								   IMG_UINT32 *pui32PageCount,
								   IMG_DEV_PHYADDR **ppasDevPAddr);

/*************************************************************************/ /*!
@Function       DCImportBufferRelease

@Description    Release an imported buffer.

@Input          hImport                 Import handle we're releasing

@Input          pasDevPAddr             Import data was returned from
                                        DCImportBufferAcquire

@Return         None
*/
/*****************************************************************************/
IMG_VOID DCImportBufferRelease(IMG_HANDLE hImport,
							   IMG_DEV_PHYADDR *pasDevPAddr);

#if defined (__cplusplus)
}
#endif

#endif/* #if !defined (__KERNELDISPLAY_H__) */

/******************************************************************************
 End of file (kerneldisplay.h)
******************************************************************************/
