/*************************************************************************/ /*!
@Title          Resource Manager
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
*/ /**************************************************************************/
#include "services_headers.h"
#include "resman.h"

#ifdef __linux__
#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#endif

#include <linux/sched.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
#include <linux/hardirq.h>
#else
#include <asm/hardirq.h>
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
#include <linux/mutex.h>
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#include <linux/semaphore.h>
#else
#include <asm/semaphore.h>
#endif
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
static DEFINE_MUTEX(lock);
#define	DOWN(m) mutex_lock(m)
#define	UP(m) mutex_unlock(m)
#else
static DECLARE_MUTEX(lock);
#define	DOWN(m) down(m)
#define	UP(m) up(m)
#endif

#define ACQUIRE_SYNC_OBJ  do {							\
		if (in_interrupt()) { 							\
			printk("ISR cannot take RESMAN mutex\n"); 	\
			BUG(); 										\
		} 												\
		else DOWN(&lock); 								\
} while (0)
#define RELEASE_SYNC_OBJ UP(&lock)

#else

#define ACQUIRE_SYNC_OBJ
#define RELEASE_SYNC_OBJ

#endif

#define RESMAN_SIGNATURE 0x12345678

/******************************************************************************
 * resman structures
 *****************************************************************************/

/* resman item structure */
typedef struct _RESMAN_ITEM_
{
#ifdef DEBUG
	IMG_UINT32				ui32Signature;
#endif
	struct _RESMAN_ITEM_	**ppsThis;	/*!< list navigation */
	struct _RESMAN_ITEM_	*psNext;	/*!< list navigation */

	IMG_UINT32				ui32Flags;	/*!< flags */
	IMG_UINT32				ui32ResType;/*!< res type */

	IMG_PVOID				pvParam;	/*!< param1 for callback */
	IMG_UINT32				ui32Param;	/*!< param2 for callback */

	RESMAN_FREE_FN			pfnFreeResource;/*!< resman item free callback */
} RESMAN_ITEM;


/* resman context structure */
typedef struct _RESMAN_CONTEXT_
{
#ifdef DEBUG
	IMG_UINT32					ui32Signature;
#endif
	struct	_RESMAN_CONTEXT_	**ppsThis;/*!< list navigation */
	struct	_RESMAN_CONTEXT_	*psNext;/*!< list navigation */

	PVRSRV_PER_PROCESS_DATA		*psPerProc; /* owner of resources */

	RESMAN_ITEM					*psResItemList;/*!< res item list for context */

} RESMAN_CONTEXT;


/* resman list structure */
typedef struct
{
	RESMAN_CONTEXT	*psContextList; /*!< resman context list */

} RESMAN_LIST, *PRESMAN_LIST;	/* PRQA S 3205 */


PRESMAN_LIST	gpsResList = IMG_NULL;

#include "lists.h"	/* PRQA S 5087 */ /* include lists.h required here */

static IMPLEMENT_LIST_ANY_VA(RESMAN_ITEM)
static IMPLEMENT_LIST_ANY_VA_2(RESMAN_ITEM, IMG_BOOL, IMG_FALSE)
static IMPLEMENT_LIST_INSERT(RESMAN_ITEM)
static IMPLEMENT_LIST_REMOVE(RESMAN_ITEM)
static IMPLEMENT_LIST_REVERSE(RESMAN_ITEM)

static IMPLEMENT_LIST_REMOVE(RESMAN_CONTEXT)
static IMPLEMENT_LIST_INSERT(RESMAN_CONTEXT)


#define PRINT_RESLIST(x, y, z)

/******************************************************** Forword references */

static PVRSRV_ERROR FreeResourceByPtr(RESMAN_ITEM *psItem, IMG_BOOL bExecuteCallback, IMG_BOOL bForceCleanup);

static PVRSRV_ERROR FreeResourceByCriteria(PRESMAN_CONTEXT	psContext,
										   IMG_UINT32		ui32SearchCriteria,
										   IMG_UINT32		ui32ResType,
										   IMG_PVOID		pvParam,
										   IMG_UINT32		ui32Param,
										   IMG_BOOL			bExecuteCallback);


#ifdef DEBUG
	static IMG_VOID ValidateResList(PRESMAN_LIST psResList);
	#define VALIDATERESLIST() ValidateResList(gpsResList)
#else
	#define VALIDATERESLIST()
#endif






/*!
******************************************************************************

 @Function	ResManInit

 @Description initialises the resman

 @Return   none

******************************************************************************/
PVRSRV_ERROR ResManInit(IMG_VOID)
{
	if (gpsResList == IMG_NULL)
	{
		/* If not already initialised */
		if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						sizeof(*gpsResList),
						(IMG_VOID **)&gpsResList, IMG_NULL,
						"Resource Manager List") != PVRSRV_OK)
		{
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		/* Init list, the linked list has dummy entries at both ends */
		gpsResList->psContextList = IMG_NULL;

		/* Check resource list */
		VALIDATERESLIST();
	}

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	ResManDeInit

 @Description de-initialises the resman

 @Return   none

******************************************************************************/
IMG_VOID ResManDeInit(IMG_VOID)
{
	if (gpsResList != IMG_NULL)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(*gpsResList), gpsResList, IMG_NULL);
		gpsResList = IMG_NULL;
	}
}


/*!
******************************************************************************

 @Function	PVRSRVResManConnect

 @Description Opens a connection to the Resource Manager

 @input 	hPerProc - Per-process data (if applicable)
 @output 	phResManContext - Resman context

 @Return    error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVResManConnect(IMG_HANDLE			hPerProc,
								 PRESMAN_CONTEXT	*phResManContext)
{
	PVRSRV_ERROR	eError;
	PRESMAN_CONTEXT	psResManContext;

	/*Acquire resource list sync object*/
	ACQUIRE_SYNC_OBJ;

	/*Check resource list*/
	VALIDATERESLIST();

	/* Allocate memory for the new context. */
	eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(*psResManContext),
						(IMG_VOID **)&psResManContext, IMG_NULL,
						"Resource Manager Context");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVResManConnect: ERROR allocating new RESMAN context struct"));

		/* Check resource list */
		VALIDATERESLIST();

		/* Release resource list sync object */
		RELEASE_SYNC_OBJ;

		return eError;
	}

#ifdef DEBUG
	psResManContext->ui32Signature = RESMAN_SIGNATURE;
#endif /* DEBUG */
	psResManContext->psResItemList	= IMG_NULL;
	psResManContext->psPerProc = hPerProc;

	/* Insert new context struct after the dummy first entry */
	List_RESMAN_CONTEXT_Insert(&gpsResList->psContextList, psResManContext);

	/* Check resource list */
	VALIDATERESLIST();

	/* Release resource list sync object */
	RELEASE_SYNC_OBJ;

	*phResManContext = psResManContext;

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	PVRSRVResManDisconnect

 @Description Closes a Resource Manager connection and frees all resources

 @input 	hResManContext - Resman context
 @input 	bKernelContext - IMG_TRUE for kernel contexts

 @Return	IMG_VOID

******************************************************************************/
IMG_VOID PVRSRVResManDisconnect(PRESMAN_CONTEXT psResManContext,
								IMG_BOOL		bKernelContext)
{
	/* Acquire resource list sync object */
	ACQUIRE_SYNC_OBJ;

	/* Check resource list */
	VALIDATERESLIST();

	/* Print and validate resource list */
	PRINT_RESLIST(gpsResList, psResManContext, IMG_TRUE);

	/* Free all auto-freed resources in order */

	if (!bKernelContext)
	{
		/* OS specific User-mode Mappings: */
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_OS_USERMODE_MAPPING, 0, 0, IMG_TRUE);

		/* VGX types: */
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_DMA_CLIENT_FIFO_DATA, 0, 0, IMG_TRUE);

		/* Event Object */
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_EVENT_OBJECT, 0, 0, IMG_TRUE);

		/* syncobject state (Read/Write Complete values) */
		/* Must be FIFO, so we reverse the list, twice */
		List_RESMAN_ITEM_Reverse(&psResManContext->psResItemList);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_MODIFY_SYNC_OPS, 0, 0, IMG_TRUE);
		List_RESMAN_ITEM_Reverse(&psResManContext->psResItemList);  // (could survive without this - all following items would be cleared up "fifo" too)

		/* SGX types: */
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_HW_RENDER_CONTEXT, 0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_HW_TRANSFER_CONTEXT, 0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_HW_2D_CONTEXT, 0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_TRANSFER_CONTEXT, 0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_SHARED_PB_DESC_CREATE_LOCK, 0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_SHARED_PB_DESC, 0, 0, IMG_TRUE);
		
		/* COMMON types: */
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_SYNC_INFO, 0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_DEVICECLASSMEM_MAPPING, 0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_DEVICEMEM_WRAP, 0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_DEVICEMEM_MAPPING, 0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_KERNEL_DEVICEMEM_ALLOCATION, 0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_DEVICEMEM_ALLOCATION, 0, 0, IMG_TRUE);
#if defined(SUPPORT_ION)
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_DEVICEMEM_ION, 0, 0, IMG_TRUE);
#endif
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_DEVICEMEM_CONTEXT, 0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_SHARED_MEM_INFO, 0, 0, IMG_TRUE);

		/* DISPLAY CLASS types: */
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_DISPLAYCLASS_SWAPCHAIN_REF, 0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_DISPLAYCLASS_DEVICE, 0, 0, IMG_TRUE);

		/* BUFFER CLASS types: */
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE, RESMAN_TYPE_BUFFERCLASS_DEVICE, 0, 0, IMG_TRUE);
	}

	/* Ensure that there are no resources left */
	PVR_ASSERT(psResManContext->psResItemList == IMG_NULL);

	/* Remove the context struct from the list */
	List_RESMAN_CONTEXT_Remove(psResManContext);

	/* Free the context struct */
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(RESMAN_CONTEXT), psResManContext, IMG_NULL);
	/*not nulling pointer, copy on stack*/


	/* Check resource list */
	VALIDATERESLIST();

	/* Print and validate resource list */
	PRINT_RESLIST(gpsResList, psResManContext, IMG_FALSE);

	/* Release resource list sync object */
	RELEASE_SYNC_OBJ;
}


/*!
******************************************************************************
 @Function	 ResManRegisterRes

 @Description    : Inform the resource manager that the given resource has
				   been alloacted and freeing of it will be the responsibility
				   of the resource manager

 @input 	psResManContext - resman context
 @input 	ui32ResType - identify what kind of resource it is
 @input 	pvParam - address of resource
 @input 	ui32Param - size of resource
 @input 	pfnFreeResource - pointer to function that frees this resource

 @Return   On success a pointer to an opaque data structure that represents
						the allocated resource, else NULL

**************************************************************************/
PRESMAN_ITEM ResManRegisterRes(PRESMAN_CONTEXT	psResManContext,
							   IMG_UINT32		ui32ResType,
							   IMG_PVOID		pvParam,
							   IMG_UINT32		ui32Param,
							   RESMAN_FREE_FN	pfnFreeResource)
{
	PRESMAN_ITEM	psNewResItem;

	PVR_ASSERT(psResManContext != IMG_NULL);
	PVR_ASSERT(ui32ResType != 0);

	if (psResManContext == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "ResManRegisterRes: invalid parameter - psResManContext"));
		return (PRESMAN_ITEM) IMG_NULL;
	}

	/* Acquire resource list sync object */
	ACQUIRE_SYNC_OBJ;

	/* Check resource list */
	VALIDATERESLIST();

	PVR_DPF((PVR_DBG_MESSAGE, "ResManRegisterRes: register resource "
			"Context 0x%p, ResType 0x%x, pvParam 0x%p, ui32Param 0x%x, "
			"FreeFunc %p",
			psResManContext,
			ui32ResType,
			pvParam,
			ui32Param,
			pfnFreeResource));

	/* Allocate memory for the new resource structure */
	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				   sizeof(RESMAN_ITEM), (IMG_VOID **)&psNewResItem,
				   IMG_NULL,
				   "Resource Manager Item") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "ResManRegisterRes: "
				"ERROR allocating new resource item"));

		/* Release resource list sync object */
		RELEASE_SYNC_OBJ;

		return((PRESMAN_ITEM)IMG_NULL);
	}

	/* Fill in details about this resource */
#ifdef DEBUG
	psNewResItem->ui32Signature		= RESMAN_SIGNATURE;
#endif /* DEBUG */
	psNewResItem->ui32ResType		= ui32ResType;
	psNewResItem->pvParam			= pvParam;
	psNewResItem->ui32Param			= ui32Param;
	psNewResItem->pfnFreeResource	= pfnFreeResource;
	psNewResItem->ui32Flags		    = 0;

	/* Insert new structure after dummy first entry */
	List_RESMAN_ITEM_Insert(&psResManContext->psResItemList, psNewResItem);

	/* Check resource list */
	VALIDATERESLIST();

	/* Release resource list sync object */
	RELEASE_SYNC_OBJ;

	return(psNewResItem);
}

/*!
******************************************************************************
 @Function	 	ResManFreeResByPtr

 @Description   frees a resource by matching on pointer type

 @inputs        psResItem - pointer to resource item to free
                bForceCleanup	- ignored uKernel re-sync

 @Return   		PVRSRV_ERROR
**************************************************************************/
PVRSRV_ERROR ResManFreeResByPtr(RESMAN_ITEM	*psResItem, IMG_BOOL bForceCleanup)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(psResItem != IMG_NULL);

	if (psResItem == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "ResManFreeResByPtr: NULL ptr - nothing to do"));
		return PVRSRV_OK;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "ResManFreeResByPtr: freeing resource at %p",
			psResItem));

	/*Acquire resource list sync object*/
	ACQUIRE_SYNC_OBJ;

	/*Check resource list*/
	VALIDATERESLIST();

	/*Free resource*/
	eError = FreeResourceByPtr(psResItem, IMG_TRUE, bForceCleanup);

	/*Check resource list*/
	VALIDATERESLIST();

	/*Release resource list sync object*/
	RELEASE_SYNC_OBJ;

	return(eError);
}


/*!
******************************************************************************
 @Function	 	ResManFreeResByCriteria

 @Description   frees a resource by matching on criteria

 @inputs	 	hResManContext - handle for resman context
 @inputs        ui32SearchCriteria - indicates which parameters should be
 				used in search for resources to free
 @inputs        ui32ResType - identify what kind of resource to free
 @inputs        pvParam - address of resource to be free
 @inputs        ui32Param - size of resource to be free

 @Return   		PVRSRV_ERROR
**************************************************************************/
PVRSRV_ERROR ResManFreeResByCriteria(PRESMAN_CONTEXT	psResManContext,
									 IMG_UINT32			ui32SearchCriteria,
									 IMG_UINT32			ui32ResType,
									 IMG_PVOID			pvParam,
									 IMG_UINT32			ui32Param)
{
	PVRSRV_ERROR	eError;

	PVR_ASSERT(psResManContext != IMG_NULL);

	/* Acquire resource list sync object */
	ACQUIRE_SYNC_OBJ;

	/* Check resource list */
	VALIDATERESLIST();

	PVR_DPF((PVR_DBG_MESSAGE, "ResManFreeResByCriteria: "
			"Context 0x%p, Criteria 0x%x, Type 0x%x, Addr 0x%p, Param 0x%x",
			psResManContext, ui32SearchCriteria, ui32ResType,
			pvParam, ui32Param));

	/* Free resources by criteria for this context */
	eError = FreeResourceByCriteria(psResManContext, ui32SearchCriteria,
									ui32ResType, pvParam, ui32Param,
									IMG_TRUE);

	/* Check resource list */
	VALIDATERESLIST();

	/* Release resource list sync object */
	RELEASE_SYNC_OBJ;

	return eError;
}


/*!
******************************************************************************
 @Function	 	ResManDissociateRes

 @Description   Moves a resource from one context to another.

 @inputs        psResItem - pointer to resource item to dissociate
 @inputs	 	psNewResManContext - new resman context for the resource

 @Return   		IMG_VOID
**************************************************************************/
PVRSRV_ERROR ResManDissociateRes(RESMAN_ITEM		*psResItem,
							 PRESMAN_CONTEXT	psNewResManContext)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT(psResItem != IMG_NULL);

	if (psResItem == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "ResManDissociateRes: invalid parameter - psResItem"));
		PVR_DBG_BREAK;
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

#ifdef DEBUG /* QAC fix */
	PVR_ASSERT(psResItem->ui32Signature == RESMAN_SIGNATURE);
#endif

	if (psNewResManContext != IMG_NULL)
	{
		/* Remove this item from its old resource list */
		List_RESMAN_ITEM_Remove(psResItem);

		/* Re-insert into new list */
		List_RESMAN_ITEM_Insert(&psNewResManContext->psResItemList, psResItem);

	}
	else
	{
		eError = FreeResourceByPtr(psResItem, IMG_FALSE, CLEANUP_WITH_POLL);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "ResManDissociateRes: failed to free resource by pointer"));
			return eError;
		}
	}

	return eError;
}

/*!
******************************************************************************
 @Function	 	ResManFindResourceByPtr_AnyVaCb

 @Description
 					Compares the resman item with a given pointer.

 @inputs	 	psCurItem - theThe item to check
 @inputs        va - Variable argument list with:
					 psItem - pointer to resource item to find

 @Return   		IMG_BOOL
**************************************************************************/
static IMG_BOOL ResManFindResourceByPtr_AnyVaCb(RESMAN_ITEM *psCurItem, va_list va)
{
	RESMAN_ITEM		*psItem;

	psItem = va_arg(va, RESMAN_ITEM*);

	return (IMG_BOOL)(psCurItem == psItem);
}


/*!
******************************************************************************
 @Function	 	ResManFindResourceByPtr

 @Description
 					Attempts to find a resource in the list for this context

 @inputs	 	hResManContext - handle for resman context
 @inputs        psItem - pointer to resource item to find

 @Return   		PVRSRV_ERROR
**************************************************************************/
IMG_INTERNAL PVRSRV_ERROR ResManFindResourceByPtr(PRESMAN_CONTEXT	psResManContext,
												  RESMAN_ITEM		*psItem)
{
/*	RESMAN_ITEM		*psCurItem;*/

	PVRSRV_ERROR	eResult;

	PVR_ASSERT(psResManContext != IMG_NULL);
	PVR_ASSERT(psItem != IMG_NULL);

	if ((psItem == IMG_NULL) || (psResManContext == IMG_NULL))
	{
		PVR_DPF((PVR_DBG_ERROR, "ResManFindResourceByPtr: invalid parameter"));
		PVR_DBG_BREAK;
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

#ifdef DEBUG	/* QAC fix */
	PVR_ASSERT(psItem->ui32Signature == RESMAN_SIGNATURE);
#endif

	/* Acquire resource list sync object */
	ACQUIRE_SYNC_OBJ;

	PVR_DPF((PVR_DBG_MESSAGE,
			"FindResourceByPtr: psItem=%p, psItem->psNext=%p",
			psItem, psItem->psNext));

	PVR_DPF((PVR_DBG_MESSAGE,
			"FindResourceByPtr: Resource Ctx 0x%p, Type 0x%x, Addr 0x%p, "
			"Param 0x%x, FnCall %p, Flags 0x%x",
			psResManContext,
			psItem->ui32ResType,
			psItem->pvParam,
			psItem->ui32Param,
			psItem->pfnFreeResource,
			psItem->ui32Flags));

	/* Search resource items starting at after the first dummy item */
	if(List_RESMAN_ITEM_IMG_BOOL_Any_va(psResManContext->psResItemList,
										&ResManFindResourceByPtr_AnyVaCb,
										psItem))
	{
		eResult = PVRSRV_OK;
	}
	else
	{
		eResult = PVRSRV_ERROR_NOT_OWNER;
	}

	/* Release resource list sync object */
	RELEASE_SYNC_OBJ;

/*	return PVRSRV_ERROR_NOT_OWNER;*/
	return eResult;
}

/*!
******************************************************************************
 @Function	 	FreeResourceByPtr

 @Description
 					Frees a resource and move it from the list
					NOTE : this function must be called with the resource
					list sync object held

 @inputs        psItem - pointer to resource item to free
 				bExecuteCallback - execute callback?
 				bForceCleanup - skips uKernel re-sync

 @Return   		PVRSRV_ERROR
**************************************************************************/
static PVRSRV_ERROR FreeResourceByPtr(RESMAN_ITEM	*psItem,
									  IMG_BOOL		bExecuteCallback,
									  IMG_BOOL		bForceCleanup)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT(psItem != IMG_NULL);

	if (psItem == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "FreeResourceByPtr: invalid parameter"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

#ifdef DEBUG	/* QAC fix */
	PVR_ASSERT(psItem->ui32Signature == RESMAN_SIGNATURE);
#endif

	PVR_DPF((PVR_DBG_MESSAGE,
			"FreeResourceByPtr: psItem=%p, psItem->psNext=%p",
			psItem, psItem->psNext));

	PVR_DPF((PVR_DBG_MESSAGE,
			"FreeResourceByPtr: Type 0x%x, Addr 0x%p, "
			"Param 0x%x, FnCall %p, Flags 0x%x",
			psItem->ui32ResType,
			psItem->pvParam, 
            psItem->ui32Param,
			psItem->pfnFreeResource, psItem->ui32Flags));

	/* Release resource list sync object just in case the free routine calls the resource manager */
	RELEASE_SYNC_OBJ;

	/* Call the freeing routine */
	if (bExecuteCallback)
	{
		eError = psItem->pfnFreeResource(psItem->pvParam, psItem->ui32Param, bForceCleanup);
	 	if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
		{
			PVR_DPF((PVR_DBG_ERROR, "FreeResourceByPtr: ERROR calling FreeResource function error 0x%x", eError));
			PVR_DPF((PVR_DBG_ERROR, "FreeResourceByPtr: ui32ResType %d", psItem->ui32ResType));
		}
	}

	/* Acquire resource list sync object */
	ACQUIRE_SYNC_OBJ;

	if (eError != PVRSRV_ERROR_RETRY)
	{
		/* Remove this item from the resource list */
		List_RESMAN_ITEM_Remove(psItem);

		/* Free memory for the resource item */
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(RESMAN_ITEM), psItem, IMG_NULL);
	}

	return(eError);
}

/*!
******************************************************************************
 @Function	 	FreeResourceByCriteria_AnyVaCb

 @Description
 					Matches a resource manager item with a given criteria.

 @inputs        psCuItem - the item to be matched
 @inputs		va - a variable argument list with:.
					ui32SearchCriteria - indicates which parameters should be used
					search for resources to free
					ui32ResType - identify what kind of resource to free
					pvParam - address of resource to be free
					ui32Param - size of resource to be free


 @Return   		psCurItem if matched, IMG_NULL otherwise.
**************************************************************************/
static IMG_VOID* FreeResourceByCriteria_AnyVaCb(RESMAN_ITEM *psCurItem, va_list va)
{
	IMG_UINT32 ui32SearchCriteria;
	IMG_UINT32 ui32ResType;
	IMG_PVOID pvParam;
	IMG_UINT32 ui32Param;

	ui32SearchCriteria = va_arg(va, IMG_UINT32);
	ui32ResType = va_arg(va, IMG_UINT32);
	pvParam = va_arg(va, IMG_PVOID);
	ui32Param = va_arg(va, IMG_UINT32);

	/*check that for all conditions are either disabled or eval to true*/
	if(
	/* Check resource type */
		(((ui32SearchCriteria & RESMAN_CRITERIA_RESTYPE) == 0UL) ||
		(psCurItem->ui32ResType == ui32ResType))
	&&
	/* Check address */
		(((ui32SearchCriteria & RESMAN_CRITERIA_PVOID_PARAM) == 0UL) ||
			 (psCurItem->pvParam == pvParam))
	&&
	/* Check size */
		(((ui32SearchCriteria & RESMAN_CRITERIA_UI32_PARAM) == 0UL) ||
			 (psCurItem->ui32Param == ui32Param))
		)
	{
		return psCurItem;
	}
	else
	{
		return IMG_NULL;
	}
}

/*!
******************************************************************************
 @Function	 	FreeResourceByCriteria

 @Description
 					Frees all resources that match the given criteria for the
					context.
					NOTE : this function must be called with the resource
					list sync object held

 @inputs        psResManContext - pointer to resman context
 @inputs        ui32SearchCriteria - indicates which parameters should be used
 @inputs        search for resources to free
 @inputs        ui32ResType - identify what kind of resource to free
 @inputs        pvParam - address of resource to be free
 @inputs        ui32Param - size of resource to be free
 @inputs        ui32AutoFreeLev - auto free level to free
 @inputs        bExecuteCallback - execute callback?

 @Return   		PVRSRV_ERROR
**************************************************************************/
static PVRSRV_ERROR FreeResourceByCriteria(PRESMAN_CONTEXT	psResManContext,
										   IMG_UINT32		ui32SearchCriteria,
										   IMG_UINT32		ui32ResType,
										   IMG_PVOID		pvParam,
										   IMG_UINT32		ui32Param,
										   IMG_BOOL			bExecuteCallback)
{
	PRESMAN_ITEM	psCurItem;
	PVRSRV_ERROR	eError = PVRSRV_OK;

	/* Search resource items starting at after the first dummy item */
	/*while we get a match and not an error*/
	while((psCurItem = (PRESMAN_ITEM)
				List_RESMAN_ITEM_Any_va(psResManContext->psResItemList,
										&FreeResourceByCriteria_AnyVaCb,
										ui32SearchCriteria,
										ui32ResType,
						 				pvParam,
						 				ui32Param)) != IMG_NULL
		  	&& eError == PVRSRV_OK)
	{
		do
		{
			eError = FreeResourceByPtr(psCurItem, bExecuteCallback, CLEANUP_WITH_POLL);
			if (eError == PVRSRV_ERROR_RETRY)
			{
				RELEASE_SYNC_OBJ;
				OSReleaseBridgeLock();
				/* Give a chance for other threads to come in and SGX to do more work */
				OSSleepms(MAX_CLEANUP_TIME_WAIT_US/1000);
				OSReacquireBridgeLock();
				ACQUIRE_SYNC_OBJ;
			}
		} while (eError == PVRSRV_ERROR_RETRY);
	}

	return eError;
}


#ifdef DEBUG
/*!
******************************************************************************
 @Function	 	ValidateResList

 @Description
 					Walks the resource list check the pointers
					NOTE : this function must be called with the resource
					list sync object held

 @Return   		none
**************************************************************************/
static IMG_VOID ValidateResList(PRESMAN_LIST psResList)
{
	PRESMAN_ITEM	psCurItem, *ppsThisItem;
	PRESMAN_CONTEXT	psCurContext, *ppsThisContext;

	/* check we're initialised */
	if (psResList == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "ValidateResList: resman not initialised yet"));
		return;
	}

	psCurContext = psResList->psContextList;
	ppsThisContext = &psResList->psContextList;

	/* Walk the context list */
	while(psCurContext != IMG_NULL)
	{
		/* Check current item */
		PVR_ASSERT(psCurContext->ui32Signature == RESMAN_SIGNATURE);
		if (psCurContext->ppsThis != ppsThisContext)
		{
			PVR_DPF((PVR_DBG_WARNING,
					"psCC=%p psCC->ppsThis=%p psCC->psNext=%p ppsTC=%p",
					psCurContext,
					psCurContext->ppsThis,
					psCurContext->psNext,
					ppsThisContext));
			PVR_ASSERT(psCurContext->ppsThis == ppsThisContext);
		}

		/* Walk the list for this context */
		psCurItem = psCurContext->psResItemList;
		ppsThisItem = &psCurContext->psResItemList;
		while(psCurItem != IMG_NULL)
		{
			/* Check current item */
			PVR_ASSERT(psCurItem->ui32Signature == RESMAN_SIGNATURE);
			if (psCurItem->ppsThis != ppsThisItem)
			{
				PVR_DPF((PVR_DBG_WARNING,
						"psCurItem=%p psCurItem->ppsThis=%p psCurItem->psNext=%p ppsThisItem=%p",
						psCurItem,
						psCurItem->ppsThis,
						psCurItem->psNext,
						ppsThisItem));
				PVR_ASSERT(psCurItem->ppsThis == ppsThisItem);
			}

			/* Move to next item */
			ppsThisItem = &psCurItem->psNext;
			psCurItem = psCurItem->psNext;
		}

		/* Move to next context */
		ppsThisContext = &psCurContext->psNext;
		psCurContext = psCurContext->psNext;
	}
}
#endif /* DEBUG */


/******************************************************************************
 End of file (resman.c)
******************************************************************************/
