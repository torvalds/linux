/*************************************************************************/ /*!
@Title          Services API Header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Exported services API details
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

#ifndef __SERVICES_H__
#define __SERVICES_H__

#if defined (__cplusplus)
extern "C" {
#endif

#include "img_defs.h"
#include "servicesext.h"
#include "pdumpdefs.h"


/* The comment below is the front page for code-generated doxygen documentation */
/*!
 ******************************************************************************
 @mainpage
 This document details the APIs and implementation of the Consumer Services.
 It is intended to be used in conjunction with the Consumer Services
 Software Architectural Specification and the Consumer Services Software
 Functional Specification.
 *****************************************************************************/

/******************************************************************************
 * 	#defines
 *****************************************************************************/

/* 4k page size definition */
#define PVRSRV_4K_PAGE_SIZE		4096UL

#define PVRSRV_MAX_CMD_SIZE		1024/*!< max size in bytes of a command */

#define PVRSRV_MAX_DEVICES		16	/*!< Largest supported number of devices on the system */

#define EVENTOBJNAME_MAXLENGTH (50)

/*
	Flags associated with memory allocation
	(bits 0-11)
*/
#define PVRSRV_MEM_READ						(1U<<0)
#define PVRSRV_MEM_WRITE					(1U<<1)
#define PVRSRV_MEM_CACHE_CONSISTENT			(1U<<2)
#define PVRSRV_MEM_NO_SYNCOBJ				(1U<<3)
#define PVRSRV_MEM_INTERLEAVED				(1U<<4)
#define PVRSRV_MEM_DUMMY					(1U<<5)
#define PVRSRV_MEM_EDM_PROTECT				(1U<<6)
#define PVRSRV_MEM_ZERO						(1U<<7)
#define PVRSRV_MEM_USER_SUPPLIED_DEVVADDR	(1U<<8)
#define PVRSRV_MEM_RAM_BACKED_ALLOCATION	(1U<<9)
#define PVRSRV_MEM_NO_RESMAN				(1U<<10)
#define PVRSRV_MEM_EXPORTED					(1U<<11)


/*
	Heap Attribute flags
	(bits 12-23)
*/
#define PVRSRV_HAP_CACHED					(1U<<12)
#define PVRSRV_HAP_UNCACHED					(1U<<13)
#define PVRSRV_HAP_WRITECOMBINE				(1U<<14)
#define PVRSRV_HAP_CACHETYPE_MASK			(PVRSRV_HAP_CACHED|PVRSRV_HAP_UNCACHED|PVRSRV_HAP_WRITECOMBINE)
#define PVRSRV_HAP_KERNEL_ONLY				(1U<<15)
#define PVRSRV_HAP_SINGLE_PROCESS			(1U<<16)
#define PVRSRV_HAP_MULTI_PROCESS			(1U<<17)
#define PVRSRV_HAP_FROM_EXISTING_PROCESS	(1U<<18)
#define PVRSRV_HAP_NO_CPU_VIRTUAL			(1U<<19)
#define PVRSRV_HAP_MAPTYPE_MASK				(PVRSRV_HAP_KERNEL_ONLY \
                                            |PVRSRV_HAP_SINGLE_PROCESS \
                                            |PVRSRV_HAP_MULTI_PROCESS \
                                            |PVRSRV_HAP_FROM_EXISTING_PROCESS \
                                            |PVRSRV_HAP_NO_CPU_VIRTUAL)

/*
	Allows user allocations to override heap attributes
	(Bits shared with heap flags)
*/
#define PVRSRV_MEM_CACHED					PVRSRV_HAP_CACHED
#define PVRSRV_MEM_UNCACHED					PVRSRV_HAP_UNCACHED
#define PVRSRV_MEM_WRITECOMBINE				PVRSRV_HAP_WRITECOMBINE

/*
	Backing store flags (defined internally)
	(bits 24-26)
*/
#define PVRSRV_MEM_BACKINGSTORE_FIELD_SHIFT	(24)

/*
	Per allocation/mapping flags
	(bits 27-30)
 */
#define PVRSRV_MAP_NOUSERVIRTUAL            (1UL<<27)
#define PVRSRV_MEM_XPROC  					(1U<<28)
#define PVRSRV_MEM_ION						(1U<<29)
#define PVRSRV_MEM_ALLOCATENONCACHEDMEM		(1UL<<30)

/*
	Internal allocation/mapping flags
	(bit 31)
*/
#define PVRSRV_MEM_SPARSE					(1U<<31)


/*
 * How much context we lose on a (power) mode change
 */
#define PVRSRV_NO_CONTEXT_LOSS					0		/*!< Do not lose state on power down */
#define PVRSRV_SEVERE_LOSS_OF_CONTEXT			1		/*!< lose state on power down */
#define PVRSRV_PRE_STATE_CHANGE_MASK			0x80	/*!< power state change mask */


/*
 * Device cookie defines
 */
#define PVRSRV_DEFAULT_DEV_COOKIE			(1)	 /*!< default device cookie */


/*
 * Misc Info. present flags
 */
#define PVRSRV_MISC_INFO_TIMER_PRESENT					(1U<<0)
#define PVRSRV_MISC_INFO_CLOCKGATE_PRESENT				(1U<<1)
#define PVRSRV_MISC_INFO_MEMSTATS_PRESENT				(1U<<2)
#define PVRSRV_MISC_INFO_GLOBALEVENTOBJECT_PRESENT		(1U<<3)
#define PVRSRV_MISC_INFO_DDKVERSION_PRESENT				(1U<<4)
#define PVRSRV_MISC_INFO_CPUCACHEOP_PRESENT				(1U<<5)
#define PVRSRV_MISC_INFO_FREEMEM_PRESENT				(1U<<6)
#define PVRSRV_MISC_INFO_GET_REF_COUNT_PRESENT			(1U<<7)
#define PVRSRV_MISC_INFO_GET_PAGE_SIZE_PRESENT			(1U<<8)
#define PVRSRV_MISC_INFO_FORCE_SWAP_TO_SYSTEM_PRESENT	(1U<<9)

#define PVRSRV_MISC_INFO_RESET_PRESENT					(1U<<31)

/* PDUMP defines */
#define PVRSRV_PDUMP_MAX_FILENAME_SIZE			20
#define PVRSRV_PDUMP_MAX_COMMENT_SIZE			200

/*
	S.LSI
	Flags for ION allocation for sharing MMIPs and IMG
*/
#define EXYNOS_MAX_YUV_PER_PLANE_ION			3
#define EXYNOS_ION_CLIP_FOR_HSTRIDE				0x1

/*
 * S.LSI 12.08.10
 * EXYNOS Misc Information flags.
 */
#define EXYNOS_MISC_INFO_VADDR_REMAP_AS_ION                (1U<<0)

/*
	Flags for PVRSRVChangeDeviceMemoryAttributes call.
*/
#define PVRSRV_CHANGEDEVMEM_ATTRIBS_CACHECOHERENT		0x00000001

/*
	Flags for PVRSRVMapExtMemory and PVRSRVUnmapExtMemory
	ALTERNATEVA		-	Used when mapping multiple virtual addresses to the same physical address. Set this flag on extra maps.
	PHYSCONTIG		-	Physical pages are contiguous (unused)
*/
#define PVRSRV_MAPEXTMEMORY_FLAGS_ALTERNATEVA			0x00000001
#define PVRSRV_MAPEXTMEMORY_FLAGS_PHYSCONTIG			0x00000002

/*
	Flags for PVRSRVModifySyncOps
	WO_INC		-	Used to increment "WriteOpsPending/complete of sync info"
	RO_INC		-	Used to increment "ReadOpsPending/complete of sync info"
*/
#define PVRSRV_MODIFYSYNCOPS_FLAGS_WO_INC			0x00000001
#define PVRSRV_MODIFYSYNCOPS_FLAGS_RO_INC			0x00000002

/*
	Flags for Services connection.
	Allows to define per-client policy for Services
*/
#define SRV_FLAGS_PERSIST		0x1
#define SRV_FLAGS_PDUMP_ACTIVE	0x2

/*
	Pdump flags which are accessible to Services clients
*/
#define PVRSRV_PDUMP_FLAGS_CONTINUOUS		0x1


/******************************************************************************
 * Enums
 *****************************************************************************/

/*!
 ******************************************************************************
 * List of known device types.
 *****************************************************************************/
typedef enum _PVRSRV_DEVICE_TYPE_
{
	PVRSRV_DEVICE_TYPE_UNKNOWN			= 0 ,
	PVRSRV_DEVICE_TYPE_MBX1				= 1 ,
	PVRSRV_DEVICE_TYPE_MBX1_LITE		= 2 ,

	PVRSRV_DEVICE_TYPE_M24VA			= 3,
	PVRSRV_DEVICE_TYPE_MVDA2			= 4,
	PVRSRV_DEVICE_TYPE_MVED1			= 5,
	PVRSRV_DEVICE_TYPE_MSVDX			= 6,

	PVRSRV_DEVICE_TYPE_SGX				= 7,

	PVRSRV_DEVICE_TYPE_VGX				= 8,

	/* 3rd party devices take ext type */
	PVRSRV_DEVICE_TYPE_EXT				= 9,

    PVRSRV_DEVICE_TYPE_LAST             = 9,

	PVRSRV_DEVICE_TYPE_FORCE_I32		= 0x7fffffff

} PVRSRV_DEVICE_TYPE;

#define HEAP_ID( _dev_ , _dev_heap_idx_ )	(  ((_dev_)<<24) | ((_dev_heap_idx_)&((1<<24)-1))  )
#define HEAP_IDX( _heap_id_ )				( (_heap_id_)&((1<<24) - 1 ) )
#define HEAP_DEV( _heap_id_ )				( (_heap_id_)>>24 )

/* common undefined heap ID define */
#define PVRSRV_UNDEFINED_HEAP_ID			(~0LU)

/*!
 ******************************************************************************
 * User Module type
 *****************************************************************************/
typedef enum
{
	IMG_EGL				= 0x00000001,
	IMG_OPENGLES1		= 0x00000002,
	IMG_OPENGLES2		= 0x00000003,
	IMG_D3DM			= 0x00000004,
	IMG_SRV_UM			= 0x00000005,
	IMG_OPENVG			= 0x00000006,
	IMG_SRVCLIENT		= 0x00000007,
	IMG_VISTAKMD		= 0x00000008,
	IMG_VISTA3DNODE		= 0x00000009,
	IMG_VISTAMVIDEONODE	= 0x0000000A,
	IMG_VISTAVPBNODE	= 0x0000000B,
	IMG_OPENGL			= 0x0000000C,
	IMG_D3D				= 0x0000000D,
#if defined(SUPPORT_GRAPHICS_HAL) || defined(SUPPORT_COMPOSER_HAL)
	IMG_ANDROID_HAL		= 0x0000000E,
#endif
#if defined(SUPPORT_OPENCL)
	IMG_OPENCL			= 0x0000000F,
#endif

} IMG_MODULE_ID;


#define APPHINT_MAX_STRING_SIZE	256

/*!
 ******************************************************************************
 * IMG data types
 *****************************************************************************/
typedef enum
{
	IMG_STRING_TYPE		= 1,
	IMG_FLOAT_TYPE		,
	IMG_UINT_TYPE		,
	IMG_INT_TYPE		,
	IMG_FLAG_TYPE
}IMG_DATA_TYPE;


/******************************************************************************
 * Structure definitions.
 *****************************************************************************/
/*
	S.LSI
	Structure for ION allocation for sharing MMIPs and IMG
*/
typedef struct _EXYNOS_ION_ALLOC_DATA_
{
	IMG_INT32 i32IonFd;
	IMG_UINT32 ui32Width;
	IMG_UINT32 ui32Height;
	IMG_UINT32 ui32WStride;
	IMG_UINT32 ui32HStride;
	IMG_UINT32 ui32BitsPerpixel;
	IMG_UINT32 ui32Flags;
} EXYNOS_ION_ALLOC_DATA;

/*!
 * Forward declaration
 */
typedef struct _PVRSRV_DEV_DATA_ *PPVRSRV_DEV_DATA;

/*!
 ******************************************************************************
 * Device identifier structure
 *****************************************************************************/
typedef struct _PVRSRV_DEVICE_IDENTIFIER_
{
	PVRSRV_DEVICE_TYPE		eDeviceType;		/*!< Identifies the type of the device */
	PVRSRV_DEVICE_CLASS		eDeviceClass;		/*!< Identifies more general class of device - display/3d/mpeg etc */
	IMG_UINT32				ui32DeviceIndex;	/*!< Index of the device within the system */
	IMG_CHAR				*pszPDumpDevName;	/*!< Pdump memory bank name */
	IMG_CHAR				*pszPDumpRegName;	/*!< Pdump register bank name */

} PVRSRV_DEVICE_IDENTIFIER;


/******************************************************************************
 * Client dev info
 ******************************************************************************
 */
typedef struct _PVRSRV_CLIENT_DEV_DATA_
{
	IMG_UINT32		ui32NumDevices;				/*!< Number of services-managed devices connected */
	PVRSRV_DEVICE_IDENTIFIER asDevID[PVRSRV_MAX_DEVICES];		/*!< Device identifiers */
	PVRSRV_ERROR	(*apfnDevConnect[PVRSRV_MAX_DEVICES])(PPVRSRV_DEV_DATA);	/*< device-specific connection callback */
	PVRSRV_ERROR	(*apfnDumpTrace[PVRSRV_MAX_DEVICES])(PPVRSRV_DEV_DATA);		/*!< device-specific debug trace callback */

} PVRSRV_CLIENT_DEV_DATA;


/*!
 ******************************************************************************
 * Kernel Services connection structure
 *****************************************************************************/
typedef struct _PVRSRV_CONNECTION_
{
	IMG_HANDLE hServices;					/*!< UM IOCTL handle */
	IMG_UINT32 ui32ProcessID;				/*!< Process ID for resource locking */
	PVRSRV_CLIENT_DEV_DATA	sClientDevData;	/*!< Client device data */
	IMG_UINT32 ui32SrvFlags;				/*!< Per-client Services flags */
}PVRSRV_CONNECTION;


/*!
 ******************************************************************************
 * This structure allows the user mode glue code to have an OS independent
 * set of prototypes.
 *****************************************************************************/
typedef struct _PVRSRV_DEV_DATA_
{
	IMG_CONST PVRSRV_CONNECTION	 *psConnection;	/*!< Services connection info */
	IMG_HANDLE			hDevCookie;				/*!< Dev cookie */

} PVRSRV_DEV_DATA;

/*!
 ******************************************************************************
 * address:value update structure
 *****************************************************************************/
typedef struct _PVRSRV_MEMUPDATE_
{
	IMG_UINT32			ui32UpdateAddr;		/*!< Address */
	IMG_UINT32			ui32UpdateVal;		/*!< value */
} PVRSRV_MEMUPDATE;

/*!
 ******************************************************************************
 * address:value register structure
 *****************************************************************************/
typedef struct _PVRSRV_HWREG_
{
	IMG_UINT32			ui32RegAddr;	/*!< Address */
	IMG_UINT32			ui32RegVal;		/*!< value */
} PVRSRV_HWREG;

/*!
 ******************************************************************************
 * Implementation details for memory handling
 *****************************************************************************/
typedef struct _PVRSRV_MEMBLK_
{
	IMG_DEV_VIRTADDR	sDevVirtAddr;			/*!< Address of the memory in the IMG MMUs address space */
	IMG_HANDLE			hOSMemHandle;			/*!< Stores the underlying memory allocation handle */
	IMG_HANDLE			hOSWrapMem;				/*!< FIXME: better way to solve this problem */
	IMG_HANDLE			hBuffer;				/*!< Stores the BM_HANDLE for the underlying memory management */
	IMG_HANDLE			hResItem;				/*!< handle to resource item for allocate */
	IMG_SYS_PHYADDR	 	*psIntSysPAddr;

} PVRSRV_MEMBLK;

/*!
 ******************************************************************************
 * Memory Management (externel interface)
 *****************************************************************************/
typedef struct _PVRSRV_KERNEL_MEM_INFO_ *PPVRSRV_KERNEL_MEM_INFO;

typedef struct _PVRSRV_CLIENT_MEM_INFO_
{
	/* CPU Virtual Address */
	IMG_PVOID				pvLinAddr;

	/* CPU Virtual Address (for kernel mode) */
	IMG_PVOID				pvLinAddrKM;

	/* Device Virtual Address */
	IMG_DEV_VIRTADDR		sDevVAddr;

	/* allocation flags */
	IMG_UINT32				ui32Flags;

	/* client allocation flags */
	IMG_UINT32				ui32ClientFlags;

	/* allocation size in bytes */
	IMG_SIZE_T				uAllocSize;


	/* ptr to associated client sync info - NULL if no sync */
	struct _PVRSRV_CLIENT_SYNC_INFO_	*psClientSyncInfo;

	/* handle to client mapping data (OS specific) */
	IMG_HANDLE							hMappingInfo;

	/* handle to kernel mem info */
	IMG_HANDLE							hKernelMemInfo;

	/* resman handle for UM mapping clean-up */
	IMG_HANDLE							hResItem;

#if defined(SUPPORT_MEMINFO_IDS)
	#if !defined(USE_CODE)
	/* Globally unique "stamp" for allocation (not re-used until wrap) */
	IMG_UINT64							ui64Stamp;
	#else /* !defined(USE_CODE) */
	IMG_UINT32							dummy1;
	IMG_UINT32							dummy2;
	#endif /* !defined(USE_CODE) */
#endif /* defined(SUPPORT_MEMINFO_IDS) */
#if defined(SUPPORT_ION)
	IMG_SIZE_T							uiIonBufferSize;
#endif /* defined(SUPPORT_ION) */
//S.LSI
#if defined(SUPPORT_ION)
	IMG_INT								iIonFds[EXYNOS_MAX_YUV_PER_PLANE_ION];
	IMG_INT								iNumIonFds;
#endif
	/*
		ptr to next mem info
		D3D uses psNext for mid-scene texture reload.
	*/
	struct _PVRSRV_CLIENT_MEM_INFO_		*psNext;

} PVRSRV_CLIENT_MEM_INFO, *PPVRSRV_CLIENT_MEM_INFO;


/*!
 ******************************************************************************
 * Memory Heap Information
 *****************************************************************************/
#define PVRSRV_MAX_CLIENT_HEAPS (32)
typedef struct _PVRSRV_HEAP_INFO_
{
	IMG_UINT32			ui32HeapID;
	IMG_HANDLE 			hDevMemHeap;
	IMG_DEV_VIRTADDR	sDevVAddrBase;
	IMG_UINT32			ui32HeapByteSize;
	IMG_UINT32			ui32Attribs;
	IMG_UINT32			ui32XTileStride;
}PVRSRV_HEAP_INFO;




/*
	Event Object information structure
*/
typedef struct _PVRSRV_EVENTOBJECT_
{
	/* globally unique name of the event object */
	IMG_CHAR	szName[EVENTOBJNAME_MAXLENGTH];
	/* kernel specific handle for the event object */
	IMG_HANDLE	hOSEventKM;

} PVRSRV_EVENTOBJECT;

/*
	Cache operation type
*/
typedef enum
{
	PVRSRV_MISC_INFO_CPUCACHEOP_NONE = 0,
	PVRSRV_MISC_INFO_CPUCACHEOP_CLEAN,
	PVRSRV_MISC_INFO_CPUCACHEOP_FLUSH,
	/* S.LSI 12.11.28 invalidate cache range add */
	PVRSRV_MISC_INFO_CPUCACHEOP_INVALIDATE
} PVRSRV_MISC_INFO_CPUCACHEOP_TYPE;

/*!
 ******************************************************************************
 * Structure to retrieve misc. information from services
 *****************************************************************************/
typedef struct _PVRSRV_MISC_INFO_
{
	IMG_UINT32	ui32StateRequest;		/*!< requested State Flags */
	IMG_UINT32	ui32StatePresent;		/*!< Present/Valid State Flags */

	/*!< SOC Timer register */
	IMG_VOID	*pvSOCTimerRegisterKM;
	IMG_VOID	*pvSOCTimerRegisterUM;
	IMG_HANDLE	hSOCTimerRegisterOSMemHandle;
	IMG_HANDLE	hSOCTimerRegisterMappingInfo;

	/*!< SOC Clock Gating registers */
	IMG_VOID	*pvSOCClockGateRegs;
	IMG_UINT32	ui32SOCClockGateRegsSize;

	/* Memory Stats/DDK version string depending on ui32StateRequest flags */
	IMG_CHAR	*pszMemoryStr;
	IMG_UINT32	ui32MemoryStrLen;

	/* global event object */
	PVRSRV_EVENTOBJECT	sGlobalEventObject;//FIXME: should be private to services
	IMG_HANDLE			hOSGlobalEvent;

	/* Note: add misc. items as required */
	IMG_UINT32	aui32DDKVersion[4];

	/*!< CPU cache flush controls: */
	struct
	{
		/*!< Defer the CPU cache op to the next HW op to be submitted (else flush now) */
		IMG_BOOL bDeferOp;

		/*!< Type of cache operation to perform */
		PVRSRV_MISC_INFO_CPUCACHEOP_TYPE eCacheOpType;

		/* This union is a bit unsightly. We need it because we'll use the psMemInfo
		 * directly in the srvclient PVRSRVGetMiscInfo code, and then convert it
		 * to a kernel meminfo if required. Try to not waste space.
		 */
		union
		{
			/*!< Input client meminfo (UM side) */
			PVRSRV_CLIENT_MEM_INFO *psClientMemInfo;

			/*!< Output kernel meminfo (Bridge+KM side) */
			struct _PVRSRV_KERNEL_MEM_INFO_ *psKernelMemInfo;
		} u;

		/*!< Offset in MemInfo to start cache op */
		IMG_VOID *pvBaseVAddr;

		/*!< Length of range to perform cache op  */
		IMG_UINT32	ui32Length;
	} sCacheOpCtl;

	/*!< Meminfo refcount controls: */
	struct
	{
		/* This union is a bit unsightly. We need it because we'll use the psMemInfo
		 * directly in the srvclient PVRSRVGetMiscInfo code, and then convert it
		 * to a kernel meminfo if required. Try to not waste space.
		 */
		union
		{
			/*!< Input client meminfo (UM side) */
			PVRSRV_CLIENT_MEM_INFO *psClientMemInfo;

			/*!< Output kernel meminfo (Bridge+KM side) */
			struct _PVRSRV_KERNEL_MEM_INFO_ *psKernelMemInfo;
		} u;

		/*!< Resulting refcount */
		IMG_UINT32 ui32RefCount;
	} sGetRefCountCtl;

	IMG_UINT32 ui32PageSize;
} PVRSRV_MISC_INFO;

/*!
 ******************************************************************************
 * Synchronisation token
 *****************************************************************************/
typedef struct _PVRSRV_SYNC_TOKEN_
{
	/* This token is supposed to be passed around as an opaque object
	   - caller should not rely on the internal fields staying the same.
	   The fields are hidden in sPrivate in order to reinforce this. */
	struct
	{
		IMG_HANDLE hKernelSyncInfo;
		IMG_UINT32 ui32ReadOpsPendingSnapshot;
		IMG_UINT32 ui32WriteOpsPendingSnapshot;
		IMG_UINT32 ui32ReadOps2PendingSnapshot;
	} sPrivate;
} PVRSRV_SYNC_TOKEN;


/******************************************************************************
 * PVR Client Event handling in Services
 *****************************************************************************/
typedef enum _PVRSRV_CLIENT_EVENT_
{
	PVRSRV_CLIENT_EVENT_HWTIMEOUT = 0,
} PVRSRV_CLIENT_EVENT;

typedef IMG_VOID (*PFN_QUEUE_COMMAND_COMPLETE)(IMG_HANDLE hCallbackData);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVClientEvent(IMG_CONST PVRSRV_CLIENT_EVENT eEvent,
											PVRSRV_DEV_DATA *psDevData,
											IMG_PVOID pvData);

/******************************************************************************
 * PVR Services API prototypes.
 *****************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVConnect(PVRSRV_CONNECTION **ppsConnection, IMG_UINT32 ui32SrvFlags);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDisconnect(IMG_CONST PVRSRV_CONNECTION *psConnection);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVEnumerateDevices(IMG_CONST PVRSRV_CONNECTION 			*psConnection,
													IMG_UINT32 					*puiNumDevices,
													PVRSRV_DEVICE_IDENTIFIER 	*puiDevIDs);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAcquireDeviceData(IMG_CONST PVRSRV_CONNECTION 	*psConnection,
													IMG_UINT32			uiDevIndex,
													PVRSRV_DEV_DATA		*psDevData,
													PVRSRV_DEVICE_TYPE	eDeviceType);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetMiscInfo (IMG_CONST PVRSRV_CONNECTION *psConnection, PVRSRV_MISC_INFO *psMiscInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVReleaseMiscInfo (IMG_CONST PVRSRV_CONNECTION *psConnection, PVRSRV_MISC_INFO *psMiscInfo);

IMG_IMPORT
PVRSRV_ERROR PVRSRVPollForValue ( const PVRSRV_CONNECTION *psConnection,
							IMG_HANDLE hOSEvent,
							volatile IMG_UINT32 *pui32LinMemAddr,
							IMG_UINT32 ui32Value,
							IMG_UINT32 ui32Mask,
							IMG_UINT32 ui32Waitus,
							IMG_UINT32 ui32Tries);

/* memory APIs */
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateDeviceMemContext(IMG_CONST PVRSRV_DEV_DATA *psDevData,
											IMG_HANDLE *phDevMemContext,
											IMG_UINT32 *pui32SharedHeapCount,
											PVRSRV_HEAP_INFO *psHeapInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyDeviceMemContext(IMG_CONST PVRSRV_DEV_DATA *psDevData,
											IMG_HANDLE 			hDevMemContext
	);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDeviceMemHeapInfo(IMG_CONST PVRSRV_DEV_DATA *psDevData,
											IMG_HANDLE hDevMemContext,
											IMG_UINT32 *pui32SharedHeapCount,
											PVRSRV_HEAP_INFO *psHeapInfo);

#if defined(PVRSRV_LOG_MEMORY_ALLOCS)
	#define PVRSRVAllocDeviceMem_log(psDevData, hDevMemHeap, ui32Attribs, ui32Size, ui32Alignment, ppsMemInfo, logStr) \
		(PVR_TRACE(("PVRSRVAllocDeviceMem(" #psDevData "," #hDevMemHeap "," #ui32Attribs "," #ui32Size "," #ui32Alignment "," #ppsMemInfo ")" \
			": " logStr " (size = 0x%lx)", ui32Size)), \
		PVRSRVAllocDeviceMem(psDevData, hDevMemHeap, ui32Attribs, ui32Size, ui32Alignment, ppsMemInfo))
#else
	#define PVRSRVAllocDeviceMem_log(psDevData, hDevMemHeap, ui32Attribs, ui32Size, ui32Alignment, ppsMemInfo, logStr) \
		PVRSRVAllocDeviceMem(psDevData, hDevMemHeap, ui32Attribs, ui32Size, ui32Alignment, ppsMemInfo)
#endif


IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAllocDeviceMem2(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
									IMG_HANDLE		hDevMemHeap,
									IMG_UINT32		ui32Attribs,
									IMG_SIZE_T		ui32Size,
									IMG_SIZE_T		ui32Alignment,
									IMG_PVOID		pvPrivData,
									IMG_UINT32		ui32PrivDataLength,
									PVRSRV_CLIENT_MEM_INFO	**ppsMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAllocDeviceMem(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
									IMG_HANDLE		hDevMemHeap,
									IMG_UINT32		ui32Attribs,
									IMG_SIZE_T		ui32Size,
									IMG_SIZE_T		ui32Alignment,
									PVRSRV_CLIENT_MEM_INFO	**ppsMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVFreeDeviceMem(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
								PVRSRV_CLIENT_MEM_INFO		*psMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVExportDeviceMem(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
												PVRSRV_CLIENT_MEM_INFO		*psMemInfo,
												IMG_HANDLE					*phMemInfo
	);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVReserveDeviceVirtualMem(IMG_CONST PVRSRV_DEV_DATA *psDevData,
											IMG_HANDLE			hDevMemHeap,
											IMG_DEV_VIRTADDR	*psDevVAddr,
											IMG_SIZE_T			ui32Size,
											IMG_SIZE_T			ui32Alignment,
											PVRSRV_CLIENT_MEM_INFO		**ppsMemInfo);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVFreeDeviceVirtualMem(IMG_CONST PVRSRV_DEV_DATA *psDevData,
													PVRSRV_CLIENT_MEM_INFO *psMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapDeviceMemory (IMG_CONST PVRSRV_DEV_DATA *psDevData,
									IMG_HANDLE hKernelMemInfo,
									IMG_HANDLE hDstDevMemHeap,
									PVRSRV_CLIENT_MEM_INFO **ppsDstMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapDeviceMemory (IMG_CONST PVRSRV_DEV_DATA *psDevData,
										PVRSRV_CLIENT_MEM_INFO *psMemInfo, IMG_BOOL bMaps);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapExtMemory (IMG_CONST PVRSRV_DEV_DATA	*psDevData,
									PVRSRV_CLIENT_MEM_INFO		*psMemInfo,
									IMG_SYS_PHYADDR				*psSysPAddr,
									IMG_UINT32					ui32Flags);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapExtMemory (IMG_CONST PVRSRV_DEV_DATA *psDevData,
									PVRSRV_CLIENT_MEM_INFO		*psMemInfo,
									IMG_UINT32					ui32Flags);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVWrapExtMemory(IMG_CONST PVRSRV_DEV_DATA *psDevData,
												IMG_HANDLE				hDevMemContext,
												IMG_SIZE_T 				ui32ByteSize,
												IMG_SIZE_T				ui32PageOffset,
												IMG_BOOL				bPhysContig,
												IMG_SYS_PHYADDR	 		*psSysPAddr,
												IMG_VOID 				*pvLinAddr,
												IMG_UINT32				ui32Flags,
												PVRSRV_CLIENT_MEM_INFO **ppsMemInfo);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnwrapExtMemory (IMG_CONST PVRSRV_DEV_DATA *psDevData,
												PVRSRV_CLIENT_MEM_INFO *psMemInfo);

PVRSRV_ERROR PVRSRVChangeDeviceMemoryAttributes(IMG_CONST PVRSRV_DEV_DATA			*psDevData,
												PVRSRV_CLIENT_MEM_INFO	*psClientMemInfo,
												IMG_UINT32				ui32Attribs);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapDeviceClassMemory (IMG_CONST PVRSRV_DEV_DATA *psDevData,
										IMG_HANDLE hDevMemContext,
										IMG_HANDLE hDeviceClassBuffer,
										PVRSRV_CLIENT_MEM_INFO **ppsMemInfo);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapDeviceClassMemory (IMG_CONST PVRSRV_DEV_DATA *psDevData,
										PVRSRV_CLIENT_MEM_INFO *psMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapPhysToUserSpace(IMG_CONST PVRSRV_DEV_DATA *psDevData,
									  IMG_SYS_PHYADDR sSysPhysAddr,
									  IMG_UINT32 uiSizeInBytes,
									  IMG_PVOID *ppvUserAddr,
									  IMG_UINT32 *puiActualSize,
									  IMG_PVOID *ppvProcess);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapPhysToUserSpace(IMG_CONST PVRSRV_DEV_DATA *psDevData,
										IMG_PVOID pvUserAddr,
										IMG_PVOID pvProcess);

#if defined(LINUX)
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVExportDeviceMem2(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
												 PVRSRV_CLIENT_MEM_INFO		*psMemInfo,
												 IMG_INT					*iFd);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapDeviceMemory2(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
												 IMG_INT					iFd,
												 IMG_HANDLE					hDstDevMemHeap,
												 PVRSRV_CLIENT_MEM_INFO		**ppsDstMemInfo,
												 IMG_BOOL					bMaps);
#endif /* defined(LINUX) */

#if defined(SUPPORT_ION)
PVRSRV_ERROR PVRSRVMapIonHandle(const PVRSRV_DEV_DATA *psDevData,
								 IMG_HANDLE hDevMemHeap,
								IMG_INT32 uiFD,
								IMG_UINT32 ui32ChunkCount,
								IMG_SIZE_T *pauiOffset,
								IMG_SIZE_T *pauiSize,
								IMG_UINT32 ui32Attribs,
								PVRSRV_CLIENT_MEM_INFO **ppsMemInfo);

PVRSRV_ERROR PVRSRVUnmapIonHandle(const PVRSRV_DEV_DATA *psDevData,
								  PVRSRV_CLIENT_MEM_INFO *psMemInfo);
#endif /* defined (SUPPORT_ION) */


IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAllocDeviceMemSparse(const PVRSRV_DEV_DATA *psDevData,
													IMG_HANDLE hDevMemHeap,
													IMG_UINT32 ui32Attribs,
													IMG_SIZE_T uAlignment,
													IMG_UINT32 ui32ChunkSize,
													IMG_UINT32 ui32NumVirtChunks,
													IMG_UINT32 ui32NumPhysChunks,
													IMG_BOOL *pabMapChunk,
													PVRSRV_CLIENT_MEM_INFO **ppsMemInfo);

/******************************************************************************
 * PVR Allocation Synchronisation Functionality...
 *****************************************************************************/

typedef enum _PVRSRV_SYNCVAL_MODE_
{
	PVRSRV_SYNCVAL_READ				= IMG_TRUE,
	PVRSRV_SYNCVAL_WRITE			= IMG_FALSE,

} PVRSRV_SYNCVAL_MODE, *PPVRSRV_SYNCVAL_MODE;

typedef IMG_UINT32 PVRSRV_SYNCVAL;

IMG_IMPORT PVRSRV_ERROR PVRSRVWaitForOpsComplete(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode, PVRSRV_SYNCVAL OpRequired);

IMG_IMPORT PVRSRV_ERROR PVRSRVWaitForAllOpsComplete(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode);

IMG_IMPORT IMG_BOOL PVRSRVTestOpsComplete(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode, PVRSRV_SYNCVAL OpRequired);

IMG_IMPORT IMG_BOOL PVRSRVTestAllOpsComplete(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode);

IMG_IMPORT IMG_BOOL PVRSRVTestOpsNotComplete(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode, PVRSRV_SYNCVAL OpRequired);

IMG_IMPORT IMG_BOOL PVRSRVTestAllOpsNotComplete(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode);

IMG_IMPORT PVRSRV_SYNCVAL PVRSRVGetPendingOpSyncVal(PPVRSRV_CLIENT_MEM_INFO psMemInfo,
	PVRSRV_SYNCVAL_MODE eMode);


/******************************************************************************
 * Common Device Class Enumeration
 *****************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVEnumerateDeviceClass(IMG_CONST PVRSRV_CONNECTION *psConnection,
													PVRSRV_DEVICE_CLASS DeviceClass,
													IMG_UINT32 *pui32DevCount,
													IMG_UINT32 *pui32DevID);

/******************************************************************************
 * Display Device Class API definition
 *****************************************************************************/
IMG_IMPORT
IMG_HANDLE IMG_CALLCONV PVRSRVOpenDCDevice(IMG_CONST PVRSRV_DEV_DATA *psDevData,
											IMG_UINT32 ui32DeviceID);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCloseDCDevice(IMG_CONST PVRSRV_CONNECTION	*psConnection, IMG_HANDLE hDevice);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVEnumDCFormats (IMG_HANDLE hDevice,
											IMG_UINT32		*pui32Count,
											DISPLAY_FORMAT	*psFormat);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVEnumDCDims (IMG_HANDLE hDevice,
										IMG_UINT32 		*pui32Count,
										DISPLAY_FORMAT	*psFormat,
										DISPLAY_DIMS	*psDims);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDCSystemBuffer(IMG_HANDLE hDevice,
										IMG_HANDLE *phBuffer
	);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDCInfo(IMG_HANDLE hDevice,
										DISPLAY_INFO* psDisplayInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateDCSwapChain (IMG_HANDLE				hDevice,
													IMG_UINT32				ui32Flags,
													DISPLAY_SURF_ATTRIBUTES	*psDstSurfAttrib,
													DISPLAY_SURF_ATTRIBUTES	*psSrcSurfAttrib,
													IMG_UINT32				ui32BufferCount,
													IMG_UINT32				ui32OEMFlags,
													IMG_UINT32				*pui32SwapChainID,
													IMG_HANDLE				*phSwapChain
	);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyDCSwapChain (IMG_HANDLE hDevice,
											IMG_HANDLE		hSwapChain
	);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSetDCDstRect (IMG_HANDLE hDevice,
										IMG_HANDLE	hSwapChain,
										IMG_RECT	*psDstRect);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSetDCSrcRect (IMG_HANDLE hDevice,
										IMG_HANDLE	hSwapChain,
										IMG_RECT	*psSrcRect);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSetDCDstColourKey (IMG_HANDLE hDevice,
											IMG_HANDLE	hSwapChain,
											IMG_UINT32	ui32CKColour);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSetDCSrcColourKey (IMG_HANDLE hDevice,
											IMG_HANDLE	hSwapChain,
											IMG_UINT32	ui32CKColour);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDCBuffers(IMG_HANDLE hDevice,
									IMG_HANDLE hSwapChain,
									IMG_HANDLE *phBuffer
	);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDCBuffers2(IMG_HANDLE hDevice,
											  IMG_HANDLE hSwapChain,
											  IMG_HANDLE *phBuffer,
											  IMG_SYS_PHYADDR *psPhyAddr);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSwapToDCBuffer (IMG_HANDLE hDevice,
										IMG_HANDLE hBuffer,
										IMG_UINT32 ui32ClipRectCount,
										IMG_RECT  *psClipRect,
										IMG_UINT32 ui32SwapInterval,
										IMG_HANDLE hPrivateTag
	);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSwapToDCBuffer2 (IMG_HANDLE hDevice,
										IMG_HANDLE hBuffer,
										IMG_UINT32 ui32SwapInterval,
										PVRSRV_CLIENT_MEM_INFO **ppsMemInfos,
										IMG_UINT32 ui32NumMemInfos,
										IMG_PVOID  pvPrivData,
										IMG_UINT32 ui32PrivDataLength);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSwapToDCSystem (IMG_HANDLE hDevice,
										IMG_HANDLE hSwapChain
	);

/******************************************************************************
 * Buffer Device Class API definition
 *****************************************************************************/
IMG_IMPORT
IMG_HANDLE IMG_CALLCONV PVRSRVOpenBCDevice(IMG_CONST PVRSRV_DEV_DATA *psDevData,
											IMG_UINT32 ui32DeviceID);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCloseBCDevice(IMG_CONST PVRSRV_CONNECTION *psConnection,
												IMG_HANDLE hDevice);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetBCBufferInfo(IMG_HANDLE hDevice,
												BUFFER_INFO	*psBuffer);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetBCBuffer(IMG_HANDLE hDevice,
												IMG_UINT32 ui32BufferIndex,
												IMG_HANDLE *phBuffer
	);


/******************************************************************************
 * PDUMP Function prototypes...
 *****************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpInit(IMG_CONST PVRSRV_CONNECTION *psConnection);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpStartInitPhase(IMG_CONST PVRSRV_CONNECTION *psConnection);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpStopInitPhase(IMG_CONST PVRSRV_CONNECTION *psConnection);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpMemPol(IMG_CONST PVRSRV_CONNECTION *psConnection,
										  PVRSRV_CLIENT_MEM_INFO *psMemInfo,
										  IMG_UINT32 ui32Offset,
										  IMG_UINT32 ui32Value,
										  IMG_UINT32 ui32Mask,
										  PDUMP_POLL_OPERATOR eOperator,
										  IMG_UINT32 ui32Flags);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpSyncPol(IMG_CONST PVRSRV_CONNECTION *psConnection,
											 PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo,
											 IMG_BOOL   bIsRead,
											 IMG_UINT32 ui32Value,
											 IMG_UINT32 ui32Mask);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpSyncPol2(IMG_CONST PVRSRV_CONNECTION *psConnection,
											 PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo,
											 IMG_BOOL bIsRead);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpMem(IMG_CONST PVRSRV_CONNECTION *psConnection,
									IMG_PVOID pvAltLinAddr,
									PVRSRV_CLIENT_MEM_INFO *psMemInfo,
									IMG_UINT32 ui32Offset,
									IMG_UINT32 ui32Bytes,
									IMG_UINT32 ui32Flags);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpSync(IMG_CONST PVRSRV_CONNECTION *psConnection,
										IMG_PVOID pvAltLinAddr,
										PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo,
										IMG_UINT32 ui32Offset,
										IMG_UINT32 ui32Bytes);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpReg(IMG_CONST PVRSRV_DEV_DATA *psDevData,
										 IMG_CHAR *pszRegRegion,
											IMG_UINT32 ui32RegAddr,
											IMG_UINT32 ui32RegValue,
											IMG_UINT32 ui32Flags);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpRegPolWithFlags(const PVRSRV_DEV_DATA *psDevData,
													 IMG_CHAR *pszRegRegion,
													 IMG_UINT32 ui32RegAddr,
													 IMG_UINT32 ui32RegValue,
													 IMG_UINT32 ui32Mask,
													 IMG_UINT32 ui32Flags);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpRegPol(const PVRSRV_DEV_DATA *psDevData,
											IMG_CHAR *pszRegRegion,
											IMG_UINT32 ui32RegAddr,
											IMG_UINT32 ui32RegValue,
											IMG_UINT32 ui32Mask);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpPDReg(IMG_CONST PVRSRV_CONNECTION *psConnection,
											IMG_UINT32 ui32RegAddr,
											IMG_UINT32 ui32RegValue);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpPDDevPAddr(IMG_CONST PVRSRV_CONNECTION *psConnection,
												PVRSRV_CLIENT_MEM_INFO *psMemInfo,
												IMG_UINT32 ui32Offset,
												IMG_DEV_PHYADDR sPDDevPAddr);

#if !defined(USE_CODE)
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpMemPages(IMG_CONST PVRSRV_DEV_DATA *psDevData,
														   IMG_HANDLE			hKernelMemInfo,
														   IMG_DEV_PHYADDR		*pPages,
														   IMG_UINT32			ui32NumPages,
												   		   IMG_DEV_VIRTADDR		sDevVAddr,
														   IMG_UINT32			ui32Start,
														   IMG_UINT32			ui32Length,
														   IMG_UINT32			ui32Flags);
#endif

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpSetFrame(IMG_CONST PVRSRV_CONNECTION *psConnection,
											  IMG_UINT32 ui32Frame);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpComment(IMG_CONST PVRSRV_CONNECTION *psConnection,
											 IMG_CONST IMG_CHAR *pszComment,
											 IMG_BOOL bContinuous);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpCommentf(IMG_CONST PVRSRV_CONNECTION *psConnection,
											  IMG_BOOL bContinuous,
											  IMG_CONST IMG_CHAR *pszFormat, ...)
#if !defined(USE_CODE)
											  IMG_FORMAT_PRINTF(3, 4)
#endif
;

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpCommentWithFlagsf(IMG_CONST PVRSRV_CONNECTION *psConnection,
													   IMG_UINT32 ui32Flags,
													   IMG_CONST IMG_CHAR *pszFormat, ...)
#if !defined(USE_CODE)
													   IMG_FORMAT_PRINTF(3, 4)
#endif
;

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpDriverInfo(IMG_CONST PVRSRV_CONNECTION *psConnection,
								 				IMG_CHAR *pszString,
												IMG_BOOL bContinuous);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpIsCapturing(IMG_CONST PVRSRV_CONNECTION *psConnection,
								 				IMG_BOOL *pbIsCapturing);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpBitmap(IMG_CONST PVRSRV_DEV_DATA *psDevData,
								 			IMG_CHAR *pszFileName,
											IMG_UINT32 ui32FileOffset,
											IMG_UINT32 ui32Width,
											IMG_UINT32 ui32Height,
											IMG_UINT32 ui32StrideInBytes,
											IMG_DEV_VIRTADDR sDevBaseAddr,
											IMG_HANDLE hDevMemContext,
											IMG_UINT32 ui32Size,
											PDUMP_PIXEL_FORMAT ePixelFormat,
											PDUMP_MEM_FORMAT eMemFormat,
											IMG_UINT32 ui32PDumpFlags);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpRegRead(IMG_CONST PVRSRV_DEV_DATA *psDevData,
											IMG_CONST IMG_CHAR *pszRegRegion,
								 			IMG_CONST IMG_CHAR *pszFileName,
											IMG_UINT32 ui32FileOffset,
											IMG_UINT32 ui32Address,
											IMG_UINT32 ui32Size,
											IMG_UINT32 ui32PDumpFlags);


IMG_IMPORT
IMG_BOOL IMG_CALLCONV PVRSRVPDumpIsCapturingTest(IMG_CONST PVRSRV_CONNECTION *psConnection);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpCycleCountRegRead(IMG_CONST PVRSRV_DEV_DATA *psDevData,
														IMG_UINT32 ui32RegOffset,
														IMG_BOOL bLastFrame);

IMG_IMPORT IMG_HANDLE	PVRSRVLoadLibrary(const IMG_CHAR *pszLibraryName);
IMG_IMPORT PVRSRV_ERROR	PVRSRVUnloadLibrary(IMG_HANDLE hExtDrv);
IMG_IMPORT PVRSRV_ERROR	PVRSRVGetLibFuncAddr(IMG_HANDLE hExtDrv, const IMG_CHAR *pszFunctionName, IMG_VOID **ppvFuncAddr);

IMG_IMPORT IMG_UINT32 PVRSRVClockus (void);
IMG_IMPORT IMG_VOID PVRSRVWaitus (IMG_UINT32 ui32Timeus);
IMG_IMPORT IMG_VOID PVRSRVReleaseThreadQuanta (void);
IMG_IMPORT IMG_UINT32 IMG_CALLCONV PVRSRVGetCurrentProcessID(void);
IMG_IMPORT IMG_CHAR * IMG_CALLCONV PVRSRVSetLocale(const IMG_CHAR *pszLocale);





IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVCreateAppHintState(IMG_MODULE_ID eModuleID,
														const IMG_CHAR *pszAppName,
														IMG_VOID **ppvState);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVFreeAppHintState(IMG_MODULE_ID eModuleID,
										 IMG_VOID *pvHintState);

IMG_IMPORT IMG_BOOL IMG_CALLCONV PVRSRVGetAppHint(IMG_VOID			*pvHintState,
												  const IMG_CHAR	*pszHintName,
												  IMG_DATA_TYPE		eDataType,
												  const IMG_VOID	*pvDefault,
												  IMG_VOID			*pvReturn);

/******************************************************************************
 * Memory API(s)
 *****************************************************************************/

/* Exported APIs */
IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVAllocUserModeMem (IMG_SIZE_T uiSize);
IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVCallocUserModeMem (IMG_SIZE_T uiSize);
IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVReallocUserModeMem (IMG_PVOID pvBase, IMG_SIZE_T uiNewSize);
IMG_IMPORT IMG_VOID  IMG_CALLCONV PVRSRVFreeUserModeMem (IMG_PVOID pvMem);
IMG_IMPORT IMG_VOID PVRSRVMemCopy(IMG_VOID *pvDst, const IMG_VOID *pvSrc, IMG_SIZE_T uiSize);
IMG_IMPORT IMG_VOID PVRSRVMemSet(IMG_VOID *pvDest, IMG_UINT8 ui8Value, IMG_SIZE_T uiSize);

struct _PVRSRV_MUTEX_OPAQUE_STRUCT_;
typedef	struct  _PVRSRV_MUTEX_OPAQUE_STRUCT_ *PVRSRV_MUTEX_HANDLE;


#if defined(PVR_DEBUG_MUTEXES)

IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateMutex(PVRSRV_MUTEX_HANDLE *phMutex,
													   IMG_CHAR pszMutexName[],
													   IMG_CHAR pszFilename[],
													   IMG_INT iLine);
IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyMutex(PVRSRV_MUTEX_HANDLE hMutex,
														IMG_CHAR pszMutexName[],
														IMG_CHAR pszFilename[],
														IMG_INT iLine);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVLockMutex(PVRSRV_MUTEX_HANDLE hMutex,
												 IMG_CHAR pszMutexName[],
												 IMG_CHAR pszFilename[],
												 IMG_INT iLine);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVUnlockMutex(PVRSRV_MUTEX_HANDLE hMutex,
												   IMG_CHAR pszMutexName[],
												   IMG_CHAR pszFilename[],
												   IMG_INT iLine);

#define PVRSRVCreateMutex(phMutex) PVRSRVCreateMutex(phMutex, #phMutex, __FILE__, __LINE__)
#define PVRSRVDestroyMutex(hMutex) PVRSRVDestroyMutex(hMutex, #hMutex, __FILE__, __LINE__)
#define PVRSRVLockMutex(hMutex) PVRSRVLockMutex(hMutex, #hMutex, __FILE__, __LINE__)
#define PVRSRVUnlockMutex(hMutex) PVRSRVUnlockMutex(hMutex, #hMutex, __FILE__, __LINE__)

#else /* defined(PVR_DEBUG_MUTEXES) */

IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateMutex(PVRSRV_MUTEX_HANDLE *phMutex);
IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyMutex(PVRSRV_MUTEX_HANDLE hMutex);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVLockMutex(PVRSRV_MUTEX_HANDLE hMutex);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVUnlockMutex(PVRSRV_MUTEX_HANDLE hMutex);

#endif /* defined(PVR_DEBUG_MUTEXES) */


struct _PVRSRV_RECMUTEX_OPAQUE_STRUCT_;
typedef	struct  _PVRSRV_RECMUTEX_OPAQUE_STRUCT_ *PVRSRV_RECMUTEX_HANDLE;


#if defined(PVR_DEBUG_MUTEXES)

IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateRecursiveMutex(PVRSRV_RECMUTEX_HANDLE *phMutex,
													   IMG_CHAR pszMutexName[],
													   IMG_CHAR pszFilename[],
													   IMG_INT iLine);
IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyRecursiveMutex(PVRSRV_RECMUTEX_HANDLE hMutex,
														IMG_CHAR pszMutexName[],
														IMG_CHAR pszFilename[],
														IMG_INT iLine);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVLockRecursiveMutex(PVRSRV_RECMUTEX_HANDLE hMutex,
												 IMG_CHAR pszMutexName[],
												 IMG_CHAR pszFilename[],
												 IMG_INT iLine);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVUnlockRecursiveMutex(PVRSRV_RECMUTEX_HANDLE hMutex,
												   IMG_CHAR pszMutexName[],
												   IMG_CHAR pszFilename[],
												   IMG_INT iLine);

#define PVRSRVCreateRecursiveMutex(phMutex) PVRSRVCreateRecursiveMutex(phMutex, #phMutex, __FILE__, __LINE__)
#define PVRSRVDestroyRecursiveMutex(hMutex) PVRSRVDestroyRecursiveMutex(hMutex, #hMutex, __FILE__, __LINE__)
#define PVRSRVLockRecursiveMutex(hMutex) PVRSRVLockRecursiveMutex(hMutex, #hMutex, __FILE__, __LINE__)
#define PVRSRVUnlockRecursiveMutex(hMutex) PVRSRVUnlockRecursiveMutex(hMutex, #hMutex, __FILE__, __LINE__)

#else /* defined(PVR_DEBUG_MUTEXES) */

IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateRecursiveMutex(PVRSRV_RECMUTEX_HANDLE *phMutex);
IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyRecursiveMutex(PVRSRV_RECMUTEX_HANDLE hMutex);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVLockRecursiveMutex(PVRSRV_RECMUTEX_HANDLE hMutex);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVUnlockRecursiveMutex(PVRSRV_RECMUTEX_HANDLE hMutex);

#endif /* defined(PVR_DEBUG_MUTEXES) */

/* Non-recursive coarse-grained mutex shared between all threads in a proccess */
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVLockProcessGlobalMutex(void);
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVUnlockProcessGlobalMutex(void);


struct _PVRSRV_SEMAPHORE_OPAQUE_STRUCT_;
typedef	struct  _PVRSRV_SEMAPHORE_OPAQUE_STRUCT_ *PVRSRV_SEMAPHORE_HANDLE;


  	#define IMG_SEMAPHORE_WAIT_INFINITE       ((IMG_UINT64)0xFFFFFFFFFFFFFFFFull)


#if !defined(USE_CODE)

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVCreateSemaphore)
#endif
static INLINE PVRSRV_ERROR PVRSRVCreateSemaphore(PVRSRV_SEMAPHORE_HANDLE *phSemaphore, IMG_INT iInitialCount)
{
	PVR_UNREFERENCED_PARAMETER(iInitialCount);
	*phSemaphore = 0;
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVDestroySemaphore)
#endif
static INLINE PVRSRV_ERROR PVRSRVDestroySemaphore(PVRSRV_SEMAPHORE_HANDLE hSemaphore)
{
	PVR_UNREFERENCED_PARAMETER(hSemaphore);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVWaitSemaphore)
#endif
static INLINE PVRSRV_ERROR PVRSRVWaitSemaphore(PVRSRV_SEMAPHORE_HANDLE hSemaphore, IMG_UINT64 ui64TimeoutMicroSeconds)
{
	PVR_UNREFERENCED_PARAMETER(hSemaphore);
	PVR_UNREFERENCED_PARAMETER(ui64TimeoutMicroSeconds);
	return PVRSRV_ERROR_INVALID_PARAMS;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPostSemaphore)
#endif
static INLINE IMG_VOID PVRSRVPostSemaphore(PVRSRV_SEMAPHORE_HANDLE hSemaphore, IMG_INT iPostCount)
{
	PVR_UNREFERENCED_PARAMETER(hSemaphore);
	PVR_UNREFERENCED_PARAMETER(iPostCount);
}

#endif /* !defined(USE_CODE) */


/* Non-exported APIs */
#if defined(DEBUG) && (defined(__linux__) || defined(__QNXNTO__) )
IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVAllocUserModeMemTracking(IMG_SIZE_T ui32Size, IMG_CHAR *pszFileName, IMG_UINT32 ui32LineNumber);

IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVCallocUserModeMemTracking(IMG_SIZE_T ui32Size, IMG_CHAR *pszFileName, IMG_UINT32 ui32LineNumber);

IMG_IMPORT IMG_VOID  IMG_CALLCONV PVRSRVFreeUserModeMemTracking(IMG_VOID *pvMem);

IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVReallocUserModeMemTracking(IMG_VOID *pvMem, IMG_SIZE_T ui32NewSize, 
													  IMG_CHAR *pszFileName, IMG_UINT32 ui32LineNumber);
#endif

/******************************************************************************
 * PVR Event Object API(s)
 *****************************************************************************/

IMG_IMPORT PVRSRV_ERROR PVRSRVEventObjectWait(const PVRSRV_CONNECTION *psConnection,
									IMG_HANDLE hOSEvent
	);

/*!
 ******************************************************************************

 @Function		PVRSRVCreateSyncInfoModObj

 @Description	Creates an empty Modification object to be later used by PVRSRVModifyPendingSyncOps

 ******************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateSyncInfoModObj(const PVRSRV_CONNECTION *psConnection,
													 IMG_HANDLE *phKernelSyncInfoModObj
	);

/*!
 ******************************************************************************

 @Function		PVRSRVDestroySyncInfoModObj

 @Description	Destroys a Modification object.  Must be empty.

 ******************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroySyncInfoModObj(const PVRSRV_CONNECTION *psConnection,
													  IMG_HANDLE hKernelSyncInfoModObj
	);



/*!
 ******************************************************************************

 @Function		PVRSRVModifyPendingSyncOps

 @Description	Returns PRE-INCREMENTED sync op values. Performs thread safe increment
				of sync ops values as specified by ui32ModifyFlags.

				PVRSRV_ERROR_RETRY is returned if the supplied modification object
                is not empty.  This is on the assumption that a different thread
				will imminently call PVRSRVModifyCompleteSyncOps.  This thread should
				sleep before retrying.  It should be regarded as an error if no such
				other thread exists.

				Note that this API has implied locking semantics, as follows:

				PVRSRVModifyPendingSyncOps() 
				        -  announces an operation on the buffer is "pending", and 
						   conceptually takes a ticket to represent your place in the queue.
						-  NB: ** exclusive access to the resource is  _NOT_ granted at this time **
				PVRSRVSyncOpsFlushToModObj()
				        -  ensures you have exclusive access to the resource (conceptually, a LOCK)
						-  the previously "pending" operation can now be regarded as "in progress"
				PVRSRVModifyCompleteSyncOps()
				        -  declares that the previously "in progress" operation is now complete. (UNLOCK)
				

 ******************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVModifyPendingSyncOps(const PVRSRV_CONNECTION *psConnection,
													  IMG_HANDLE hKernelSyncInfoModObj,
													  PVRSRV_CLIENT_SYNC_INFO *psSyncInfo,
													  IMG_UINT32 ui32ModifyFlags,
													  IMG_UINT32 *pui32ReadOpsPending,
													  IMG_UINT32 *pui32WriteOpsPending);

/*!
 ******************************************************************************

 @Function		PVRSRVModifyCompleteSyncOps

 @Description	Performs thread safe increment of sync ops values as specified
                by the ui32ModifyFlags that were given to PVRSRVModifyPendingSyncOps.
				The supplied Modification Object will become empty.

				Note that this API has implied locking semantics, as
				described above in PVRSRVModifyPendingSyncOps

 ******************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVModifyCompleteSyncOps(const PVRSRV_CONNECTION *psConnection,
													  IMG_HANDLE hKernelSyncInfoModObj
	);

/*!
 ******************************************************************************

 @Function	PVRSRVSyncOpsTakeToken

 @Description Takes a "deli-counter" style token for future use with
              PVRSRVSyncOpsFlushToToken().  In practice this means
              recording a snapshot of the current "pending" values.  A
              future PVRSRVSyncOpsFlushToToken() will ensure that all
              operations that were pending at the time of this
              PVRSRVSyncOpsTakeToken() call will be flushed.
              Operations may be subsequently queued after this call
              and would not be flushed.  The caller is required to
              provide storage for the token.  The token is disposable
              - i.e. the caller can simply let the token go out of
              scope without telling us... in particular, there is no
              obligation to call PVRSRVSyncOpsFlushToToken().
              Multiple tokens may be taken.  There is no implied
              locking with this API.

 ******************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSyncOpsTakeToken(const PVRSRV_CONNECTION *psConnection,
												 const PVRSRV_CLIENT_SYNC_INFO *psSyncInfo,
												 PVRSRV_SYNC_TOKEN *psSyncToken);
/*!
 ******************************************************************************

 @Function	PVRSRVSyncOpsFlushToToken

 @Description Tests whether the dependencies for a pending sync op modification
              have been satisfied.  If this function returns PVRSRV_OK, then the
              "complete" counts have caught up with the snapshot of the "pending"
              values taken when PVRSRVSyncOpsTakeToken() was called.
              In the event that the dependencies are not (yet) met,
			  this call will auto-retry if bWait is specified, otherwise, it will
			  return PVRSRV_ERROR_RETRY.  (Not really an "error")

			  (auto-retry behaviour not implemented)

 ******************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSyncOpsFlushToToken(const PVRSRV_CONNECTION *psConnection,
													const PVRSRV_CLIENT_SYNC_INFO *psSyncInfo,
													const PVRSRV_SYNC_TOKEN *psSyncToken,
													IMG_BOOL bWait);
/*!
 ******************************************************************************

 @Function	PVRSRVSyncOpsFlushToModObj

 @Description Tests whether the dependencies for a pending sync op modification
              have been satisfied.  If this function returns PVRSRV_OK, then the
              "complete" counts have caught up with the snapshot of the "pending"
              values taken when PVRSRVModifyPendingSyncOps() was called.
              PVRSRVModifyCompleteSyncOps() can then be called without risk of
			  stalling.  In the event that the dependencies are not (yet) met,
			  this call will auto-retry if bWait is specified, otherwise, it will
			  return PVRSRV_ERROR_RETRY.  (Not really an "error")

			  Note that this API has implied locking semantics, as
			  described above in PVRSRVModifyPendingSyncOps

 ******************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSyncOpsFlushToModObj(const PVRSRV_CONNECTION *psConnection,
													 IMG_HANDLE hKernelSyncInfoModObj,
													 IMG_BOOL bWait);

/*!
 ******************************************************************************

 @Function	PVRSRVSyncOpsFlushToDelta

 @Description Compares the number of outstanding operations (pending count minus
              complete count) with the limit specified.  If no more than ui32Delta
              operations are outstanding, this function returns PVRSRV_OK.
              In the event that there are too many outstanding operations,
			  this call will auto-retry if bWait is specified, otherwise, it will
			  return PVRSRV_ERROR_RETRY.  (Not really an "error")

 ******************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSyncOpsFlushToDelta(const PVRSRV_CONNECTION *psConnection,
													PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo,
													IMG_UINT32 ui32Delta,
													IMG_BOOL bWait);

/*!
 ******************************************************************************

 @Function	PVRSRVAllocSyncInfo

 @Description Creates a Sync Object.  Unlike the sync objects created
			  automatically with "PVRSRVAllocDeviceMem", the sync objects
			  returned by this function do _not_ have a UM mapping to the
			  sync data and they do _not_ have the device virtual address
			  of the "opscomplete" fields.  These data are to be deprecated.

 ******************************************************************************/

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAllocSyncInfo(IMG_CONST PVRSRV_DEV_DATA	*psDevData,
											  PVRSRV_CLIENT_SYNC_INFO **ppsSyncInfo);

/*!
 ******************************************************************************

 @Function	PVRSRVFreeSyncInfo

 @Description Destroys a Sync Object created via
              PVRSRVAllocSyncInfo.

 ******************************************************************************/

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVFreeSyncInfo(IMG_CONST PVRSRV_DEV_DATA *psDevData,
											 PVRSRV_CLIENT_SYNC_INFO *psSyncInfo);

/*!
 ******************************************************************************

 @Function		PVRSRVGetErrorString

 @Description	Returns a text string relating to the PVRSRV_ERROR enum.

 ******************************************************************************/
IMG_IMPORT
const IMG_CHAR *PVRSRVGetErrorString(PVRSRV_ERROR eError);


/*!
 ******************************************************************************

 @Function		PVRSRVCacheInvalidate

 @Description   Invalidate the CPU cache for a specified memory
                area. Note that PVRSRVGetMiscInfo provides similar cpu
                cache flush/invalidate functionality for some platforms.

 ******************************************************************************/

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCacheInvalidate(const PVRSRV_CONNECTION *psConnection,
                                                IMG_PVOID pvLinearAddress,
	                                            IMG_UINT32 ui32Size);

/******************************************************************************
 Time wrapping macro
******************************************************************************/
#define TIME_NOT_PASSED_UINT32(a,b,c)		(((a) - (b)) < (c))

#if defined (__cplusplus)
}
#endif
#endif /* __SERVICES_H__ */

/******************************************************************************
 End of file (services.h)
******************************************************************************/
