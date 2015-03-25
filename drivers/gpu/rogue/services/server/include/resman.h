/**************************************************************************/ /*!
@File
@Title          Resource Manager API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provide resource management
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

#ifndef __RESMAN_H__
#define __RESMAN_H__

#if defined (__cplusplus)
extern "C" {
#endif

#include "servicesext.h"

/******************************************************************************
 * resman definitions 
 *****************************************************************************/

enum {
	/* SGX: */
	RESMAN_TYPE_SHARED_PB_DESC = 1,					/*!< Parameter buffer kernel stubs */
	RESMAN_TYPE_SHARED_PB_DESC_CREATE_LOCK,			/*!< Shared parameter buffer creation lock */

	/* MSVDX: TBD */
	
	/* DISPLAY CLASS: */
	RESMAN_TYPE_DISPLAYCLASS_SWAPCHAIN_REF,			/*!< Display Class Swapchain Reference Resource */
	RESMAN_TYPE_DISPLAYCLASS_DEVICE,				/*!< Display Class Device Resource */

	/* BUFFER CLASS: */
	RESMAN_TYPE_BUFFERCLASS_DEVICE,					/*!< Buffer Class Device Resource */
	
	/* OS specific User mode Mappings: */
	RESMAN_TYPE_OS_USERMODE_MAPPING,				/*!< OS specific User mode mappings */
	
	/* COMMON: */
	RESMAN_TYPE_DC_DEVICE,
	RESMAN_TYPE_DC_DISPLAY_CONTEXT,
	RESMAN_TYPE_DC_PIN_HANDLE,
	RESMAN_TYPE_DC_BUFFER,
	RESMAN_TYPE_DEVMEM_MEM_EXPORT,
    RESMAN_TYPE_PMR,
    RESMAN_TYPE_PMR_EXPORT,
	RESMAN_TYPE_PMR_PAGELIST,						/*!< Device Memory page list Resource */
	RESMAN_TYPE_DEVICEMEM2_CONTEXT,					/*!< Device Memory Context Resource */
	RESMAN_TYPE_DEVICEMEM2_CONTEXT_EXPORT,			/*!< Device Memory Context export Resource */
	RESMAN_TYPE_DEVICEMEM2_HEAP,					/*!< Device Memory Heap Resource */
    RESMAN_TYPE_DEVICEMEM2_RESERVATION,             /*!< Device Memory Reservation Resource */
    RESMAN_TYPE_DEVICEMEM2_MAPPING,                 /*!< Device Memory Mapping Resource */
	RESMAN_TYPE_DEVICEMEM_CONTEXT,					/*!< Device Memory Context Resource */
	RESMAN_TYPE_DEVICECLASSMEM_MAPPING,				/*!< Device Memory Mapping Resource */
	RESMAN_TYPE_DEVICEMEM_MAPPING,					/*!< Device Memory Mapping Resource */
	RESMAN_TYPE_DEVICEMEM_WRAP,						/*!< Device Memory Wrap Resource */
	RESMAN_TYPE_DEVICEMEM_ALLOCATION,				/*!< Device Memory Allocation Resource */
	RESMAN_TYPE_EVENT_OBJECT,						/*!< Event Object */
    RESMAN_TYPE_SHARED_MEM_INFO,                    /*!< Shared system memory meminfo */
    RESMAN_TYPE_MODIFY_SYNC_OPS,					/*!< Syncobject synchronisation Resource*/
    RESMAN_TYPE_SYNC_INFO,					        /*!< Syncobject Resource*/
	PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,				/*!< Private Data Resource*/
	RESMAN_TYPE_SYNC_RECORD_HANDLE,					/*!< Sync record handle */
	RESMAN_TYPE_SYNC_PRIMITIVE,						/*!< Sync primitive resource */
	RESMAN_TYPE_SYNC_PRIMITIVE_BLOCK,				/*!< Sync primitive block resource */
	RESMAN_TYPE_SERVER_SYNC_PRIMITIVE,				/*!< Server sync primitive resource */
	RESMAN_TYPE_SERVER_SYNC_EXPORT,					/*!< Server sync export resource */
	RESMAN_TYPE_SERVER_OP_COOKIE,					/*!< Server  operation cookie resource */
	RESMAN_TYPE_SHARED_EVENT_OBJECT,				/*!< Shared event object resource */

	/* RGX: */
	RESMAN_TYPE_RGX_SERVER_RENDER_CONTEXT,			/*!< RGX Render Context Resource */
	RESMAN_TYPE_RGX_SERVER_TQ_CONTEXT,				/*!< RGX Transfer Queue Context Resource */
	RESMAN_TYPE_RGX_SERVER_COMPUTE_CONTEXT,			/*!< RGX Compute Context Resource */
	RESMAN_TYPE_RGX_SERVER_RAY_CONTEXT,				/*!< RGX Ray Context Resource */
	RESMAN_TYPE_RGX_MEMORY_BLOCK,					/*!< RGX Freelist Memory Block Resource */
	RESMAN_TYPE_RGX_FWIF_HWRTDATA,					/*! < FW HWRTDATA structure */
	RESMAN_TYPE_RGX_FWIF_RENDERTARGET,					/*! < FW RENDER_TARGET structure */
	RESMAN_TYPE_RGX_FWIF_ZSBUFFER,					/*!< FW ZS-Buffer structure */
	RESMAN_TYPE_RGX_POPULATION,						/*!< ZS-Buffer population structure */
	RESMAN_TYPE_RGX_FWIF_FREELIST,					/*! < FW FREELIST structure */

	/* KERNEL: */
	RESMAN_TYPE_KERNEL_DEVICEMEM_ALLOCATION,		/*!< Device Memory Allocation Resource */

	/* TRANSPORT LAYER: */
	RESMAN_TYPE_TL_STREAM_DESC,						/*!< Transport Layer stream descriptor resource */

	/* RI: */
	RESMAN_TYPE_RI_HANDLE							/*!< RI resource */
};

#define RESMAN_CRITERIA_ALL				0x00000000	/*!< match by criteria all */
#define RESMAN_CRITERIA_RESTYPE			0x00000001	/*!< match by criteria type */
#define RESMAN_CRITERIA_PVOID_PARAM		0x00000002	/*!< match by criteria param1 */

/* Set the maximum time the freeing of the resources can keep the lock */
#define RESMAN_DEFERRED_CLEANUP_TIMESLICE_NS 3000*1000 /* 3ms */

typedef PVRSRV_ERROR (*RESMAN_FREE_FN)(IMG_PVOID pvParam); 

typedef struct _RESMAN_ITEM_ *PRESMAN_ITEM;
typedef struct _RESMAN_CONTEXT_ *PRESMAN_CONTEXT;
typedef struct _RESMAN_DEFER_CONTEXTS_LIST_ *PRESMAN_DEFER_CONTEXTS_LIST;

/******************************************************************************
 * resman functions 
 *****************************************************************************/

PVRSRV_ERROR ResManInit(IMG_VOID);
IMG_VOID ResManDeInit(IMG_VOID);

PRESMAN_ITEM ResManRegisterRes(PRESMAN_CONTEXT	hResManContext,
							   IMG_UINT32		ui32ResType, 
							   IMG_PVOID		pvParam, 
							   RESMAN_FREE_FN	pfnFreeResource);

PVRSRV_ERROR ResManFreeResByPtr(PRESMAN_ITEM	psResItem);

/*!
******************************************************************************
 @Function	 	ResManFindPrivateDataByPtr

 @Description   finds the private date for a resource by matching on pointer type

 @inputs        psResItem - pointer to resource item

 @Return   		PVRSRV_ERROR
**************************************************************************/
extern PVRSRV_ERROR
ResManFindPrivateDataByPtr(
                           PRESMAN_ITEM psResItem,
                           IMG_PVOID *ppvParam1
                           );

PVRSRV_ERROR ResManDissociateRes(PRESMAN_ITEM		psResItem,
							 PRESMAN_CONTEXT	psNewResManContext);

PVRSRV_ERROR ResManFindResourceByPtr(PRESMAN_CONTEXT	hResManContext,
									 PRESMAN_ITEM		psItem);

PVRSRV_ERROR PVRSRVResManConnect(PRESMAN_DEFER_CONTEXTS_LIST hDeferContext,
								 PRESMAN_CONTEXT *phResManContext);

IMG_VOID PVRSRVResManDisconnect(PRESMAN_CONTEXT hResManContext);

PVRSRV_ERROR PVRSRVResManCreateDeferContext(IMG_HANDLE hEventObj,
										    PRESMAN_DEFER_CONTEXTS_LIST *phDeferContext);

IMG_BOOL PVRSRVResManFlushDeferContext(PRESMAN_DEFER_CONTEXTS_LIST hDeferContext,
                                       IMG_UINT64 ui64TimesliceLimit);

IMG_VOID PVRSRVResManDestroyDeferContext(PRESMAN_DEFER_CONTEXTS_LIST hDeferContext);

#if defined (__cplusplus)
}
#endif

#endif /* __RESMAN_H__ */

/******************************************************************************
 End of file (resman.h)
******************************************************************************/

