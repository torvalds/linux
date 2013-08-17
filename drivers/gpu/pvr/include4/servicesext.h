/*************************************************************************/ /*!
@Title          Services definitions required by external drivers
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provides services data structures, defines and prototypes
                required by external drivers.
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

#if !defined (__SERVICESEXT_H__)
#define __SERVICESEXT_H__

/*
 * Lock buffer read/write flags
 */
#define PVRSRV_LOCKFLG_READONLY     	(1)		/*!< The locking process will only read the locked surface */

/*!
 *****************************************************************************
 * Error values
 *
 * NOTE: If you change this, make sure you update the error texts in
 *   services4/include/pvrsrv_errors.h to match.
 *
 *****************************************************************************/
typedef enum _PVRSRV_ERROR_
{
	PVRSRV_OK = 0,
	PVRSRV_ERROR_OUT_OF_MEMORY,
	PVRSRV_ERROR_TOO_FEW_BUFFERS,
	PVRSRV_ERROR_INVALID_PARAMS,
	PVRSRV_ERROR_INIT_FAILURE,
	PVRSRV_ERROR_CANT_REGISTER_CALLBACK,
	PVRSRV_ERROR_INVALID_DEVICE,
	PVRSRV_ERROR_NOT_OWNER,
	PVRSRV_ERROR_BAD_MAPPING,
	PVRSRV_ERROR_TIMEOUT,
	PVRSRV_ERROR_FLIP_CHAIN_EXISTS,
	PVRSRV_ERROR_INVALID_SWAPINTERVAL,
	PVRSRV_ERROR_SCENE_INVALID,
	PVRSRV_ERROR_STREAM_ERROR,
	PVRSRV_ERROR_FAILED_DEPENDENCIES,
	PVRSRV_ERROR_CMD_NOT_PROCESSED,
	PVRSRV_ERROR_CMD_TOO_BIG,
	PVRSRV_ERROR_DEVICE_REGISTER_FAILED,
	PVRSRV_ERROR_TOOMANYBUFFERS,
	PVRSRV_ERROR_NOT_SUPPORTED,
	PVRSRV_ERROR_PROCESSING_BLOCKED,

	PVRSRV_ERROR_CANNOT_FLUSH_QUEUE,
	PVRSRV_ERROR_CANNOT_GET_QUEUE_SPACE,
	PVRSRV_ERROR_CANNOT_GET_RENDERDETAILS,
	PVRSRV_ERROR_RETRY,

	PVRSRV_ERROR_DDK_VERSION_MISMATCH,
	PVRSRV_ERROR_BUILD_MISMATCH,
	PVRSRV_ERROR_CORE_REVISION_MISMATCH,

	PVRSRV_ERROR_UPLOAD_TOO_BIG,

	PVRSRV_ERROR_INVALID_FLAGS,
	PVRSRV_ERROR_FAILED_TO_REGISTER_PROCESS,

	PVRSRV_ERROR_UNABLE_TO_LOAD_LIBRARY,
	PVRSRV_ERROR_UNABLE_GET_FUNC_ADDR,
	PVRSRV_ERROR_UNLOAD_LIBRARY_FAILED,

	PVRSRV_ERROR_BRIDGE_CALL_FAILED,
	PVRSRV_ERROR_IOCTL_CALL_FAILED,

    PVRSRV_ERROR_MMU_CONTEXT_NOT_FOUND,
	PVRSRV_ERROR_BUFFER_DEVICE_NOT_FOUND,
	PVRSRV_ERROR_BUFFER_DEVICE_ALREADY_PRESENT,

	PVRSRV_ERROR_PCI_DEVICE_NOT_FOUND,
	PVRSRV_ERROR_PCI_CALL_FAILED,
	PVRSRV_ERROR_PCI_REGION_TOO_SMALL,
	PVRSRV_ERROR_PCI_REGION_UNAVAILABLE,
	PVRSRV_ERROR_BAD_REGION_SIZE_MISMATCH,

	PVRSRV_ERROR_REGISTER_BASE_NOT_SET,

    PVRSRV_ERROR_BM_BAD_SHAREMEM_HANDLE,

	PVRSRV_ERROR_FAILED_TO_ALLOC_USER_MEM,
	PVRSRV_ERROR_FAILED_TO_ALLOC_VP_MEMORY,
	PVRSRV_ERROR_FAILED_TO_MAP_SHARED_PBDESC,
	PVRSRV_ERROR_FAILED_TO_GET_PHYS_ADDR,

	PVRSRV_ERROR_FAILED_TO_ALLOC_VIRT_MEMORY,
	PVRSRV_ERROR_FAILED_TO_COPY_VIRT_MEMORY,

	PVRSRV_ERROR_FAILED_TO_ALLOC_PAGES,
	PVRSRV_ERROR_FAILED_TO_FREE_PAGES,
	PVRSRV_ERROR_FAILED_TO_COPY_PAGES,
	PVRSRV_ERROR_UNABLE_TO_LOCK_PAGES,
	PVRSRV_ERROR_UNABLE_TO_UNLOCK_PAGES,
	PVRSRV_ERROR_STILL_MAPPED,
	PVRSRV_ERROR_MAPPING_NOT_FOUND,
	PVRSRV_ERROR_PHYS_ADDRESS_EXCEEDS_32BIT,
	PVRSRV_ERROR_FAILED_TO_MAP_PAGE_TABLE,

	PVRSRV_ERROR_INVALID_SEGMENT_BLOCK,
	PVRSRV_ERROR_INVALID_SGXDEVDATA,
	PVRSRV_ERROR_INVALID_DEVINFO,
	PVRSRV_ERROR_INVALID_MEMINFO,
	PVRSRV_ERROR_INVALID_MISCINFO,
	PVRSRV_ERROR_UNKNOWN_IOCTL,
	PVRSRV_ERROR_INVALID_CONTEXT,
	PVRSRV_ERROR_UNABLE_TO_DESTROY_CONTEXT,
	PVRSRV_ERROR_INVALID_HEAP,
	PVRSRV_ERROR_INVALID_KERNELINFO,
	PVRSRV_ERROR_UNKNOWN_POWER_STATE,
	PVRSRV_ERROR_INVALID_HANDLE_TYPE,
	PVRSRV_ERROR_INVALID_WRAP_TYPE,
	PVRSRV_ERROR_INVALID_PHYS_ADDR,
	PVRSRV_ERROR_INVALID_CPU_ADDR,
	PVRSRV_ERROR_INVALID_HEAPINFO,
	PVRSRV_ERROR_INVALID_PERPROC,
	PVRSRV_ERROR_FAILED_TO_RETRIEVE_HEAPINFO,
	PVRSRV_ERROR_INVALID_MAP_REQUEST,
	PVRSRV_ERROR_INVALID_UNMAP_REQUEST,
	PVRSRV_ERROR_UNABLE_TO_FIND_MAPPING_HEAP,
	PVRSRV_ERROR_MAPPING_STILL_IN_USE,

	PVRSRV_ERROR_EXCEEDED_HW_LIMITS,
	PVRSRV_ERROR_NO_STAGING_BUFFER_ALLOCATED,

	PVRSRV_ERROR_UNABLE_TO_CREATE_PERPROC_AREA,
	PVRSRV_ERROR_UNABLE_TO_CREATE_EVENT,
	PVRSRV_ERROR_UNABLE_TO_ENABLE_EVENT,
	PVRSRV_ERROR_UNABLE_TO_REGISTER_EVENT,
	PVRSRV_ERROR_UNABLE_TO_DESTROY_EVENT,
	PVRSRV_ERROR_UNABLE_TO_CREATE_THREAD,
	PVRSRV_ERROR_UNABLE_TO_CLOSE_THREAD,
	PVRSRV_ERROR_THREAD_READ_ERROR,
	PVRSRV_ERROR_UNABLE_TO_REGISTER_ISR_HANDLER,
	PVRSRV_ERROR_UNABLE_TO_INSTALL_ISR,
	PVRSRV_ERROR_UNABLE_TO_UNINSTALL_ISR,
	PVRSRV_ERROR_ISR_ALREADY_INSTALLED,
	PVRSRV_ERROR_ISR_NOT_INSTALLED,
	PVRSRV_ERROR_UNABLE_TO_INITIALISE_INTERRUPT,
	PVRSRV_ERROR_UNABLE_TO_RETRIEVE_INFO,
	PVRSRV_ERROR_UNABLE_TO_DO_BACKWARDS_BLIT,
	PVRSRV_ERROR_UNABLE_TO_CLOSE_SERVICES,
	PVRSRV_ERROR_UNABLE_TO_REGISTER_CONTEXT,
	PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE,
	PVRSRV_ERROR_UNABLE_TO_CLOSE_HANDLE,

	PVRSRV_ERROR_INVALID_CCB_COMMAND,

	PVRSRV_ERROR_UNABLE_TO_LOCK_RESOURCE,
	PVRSRV_ERROR_INVALID_LOCK_ID,
	PVRSRV_ERROR_RESOURCE_NOT_LOCKED,

	PVRSRV_ERROR_FLIP_FAILED,
	PVRSRV_ERROR_UNBLANK_DISPLAY_FAILED,

	PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE,

	PVRSRV_ERROR_CREATE_RENDER_CONTEXT_FAILED,
	PVRSRV_ERROR_UNKNOWN_PRIMARY_FRAG,
	PVRSRV_ERROR_UNEXPECTED_SECONDARY_FRAG,
	PVRSRV_ERROR_UNEXPECTED_PRIMARY_FRAG,

	PVRSRV_ERROR_UNABLE_TO_INSERT_FENCE_ID,

	PVRSRV_ERROR_BLIT_SETUP_FAILED,

	PVRSRV_ERROR_PDUMP_NOT_AVAILABLE,
	PVRSRV_ERROR_PDUMP_BUFFER_FULL,
	PVRSRV_ERROR_PDUMP_BUF_OVERFLOW,
	PVRSRV_ERROR_PDUMP_NOT_ACTIVE,
	PVRSRV_ERROR_INCOMPLETE_LINE_OVERLAPS_PAGES,

	PVRSRV_ERROR_MUTEX_DESTROY_FAILED,
	PVRSRV_ERROR_MUTEX_INTERRUPTIBLE_ERROR,

	PVRSRV_ERROR_INSUFFICIENT_SCRIPT_SPACE,
	PVRSRV_ERROR_INSUFFICIENT_SPACE_FOR_COMMAND,

	PVRSRV_ERROR_PROCESS_NOT_INITIALISED,
	PVRSRV_ERROR_PROCESS_NOT_FOUND,
	PVRSRV_ERROR_SRV_CONNECT_FAILED,
	PVRSRV_ERROR_SRV_DISCONNECT_FAILED,
	PVRSRV_ERROR_DEINT_PHASE_FAILED,
	PVRSRV_ERROR_INIT2_PHASE_FAILED,

	PVRSRV_ERROR_UNABLE_TO_FIND_RESOURCE,

	PVRSRV_ERROR_NO_DC_DEVICES_FOUND,
	PVRSRV_ERROR_UNABLE_TO_OPEN_DC_DEVICE,
	PVRSRV_ERROR_UNABLE_TO_REMOVE_DEVICE,
	PVRSRV_ERROR_NO_DEVICEDATA_FOUND,
	PVRSRV_ERROR_NO_DEVICENODE_FOUND,
	PVRSRV_ERROR_NO_CLIENTNODE_FOUND,
	PVRSRV_ERROR_FAILED_TO_PROCESS_QUEUE,

	PVRSRV_ERROR_UNABLE_TO_INIT_TASK,
	PVRSRV_ERROR_UNABLE_TO_SCHEDULE_TASK,
	PVRSRV_ERROR_UNABLE_TO_KILL_TASK,

	PVRSRV_ERROR_UNABLE_TO_ENABLE_TIMER,
	PVRSRV_ERROR_UNABLE_TO_DISABLE_TIMER,
	PVRSRV_ERROR_UNABLE_TO_REMOVE_TIMER,

	PVRSRV_ERROR_UNKNOWN_PIXEL_FORMAT,
	PVRSRV_ERROR_UNKNOWN_SCRIPT_OPERATION,

	PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE,
	PVRSRV_ERROR_HANDLE_NOT_ALLOCATED,
	PVRSRV_ERROR_HANDLE_TYPE_MISMATCH,
	PVRSRV_ERROR_UNABLE_TO_ADD_HANDLE,
	PVRSRV_ERROR_HANDLE_NOT_SHAREABLE,
	PVRSRV_ERROR_HANDLE_NOT_FOUND,
	PVRSRV_ERROR_INVALID_SUBHANDLE,
	PVRSRV_ERROR_HANDLE_BATCH_IN_USE,
	PVRSRV_ERROR_HANDLE_BATCH_COMMIT_FAILURE,

	PVRSRV_ERROR_UNABLE_TO_CREATE_HASH_TABLE,
	PVRSRV_ERROR_INSERT_HASH_TABLE_DATA_FAILED,

	PVRSRV_ERROR_UNSUPPORTED_BACKING_STORE,
	PVRSRV_ERROR_UNABLE_TO_DESTROY_BM_HEAP,

	PVRSRV_ERROR_UNKNOWN_INIT_SERVER_STATE,

	PVRSRV_ERROR_NO_FREE_DEVICEIDS_AVALIABLE,
	PVRSRV_ERROR_INVALID_DEVICEID,
	PVRSRV_ERROR_DEVICEID_NOT_FOUND,

	PVRSRV_ERROR_MEMORY_TEST_FAILED,
	PVRSRV_ERROR_CPUPADDR_TEST_FAILED,
	PVRSRV_ERROR_COPY_TEST_FAILED,

	PVRSRV_ERROR_SEMAPHORE_NOT_INITIALISED,

	PVRSRV_ERROR_UNABLE_TO_RELEASE_CLOCK,
	PVRSRV_ERROR_CLOCK_REQUEST_FAILED,
	PVRSRV_ERROR_DISABLE_CLOCK_FAILURE,
	PVRSRV_ERROR_UNABLE_TO_SET_CLOCK_RATE,
	PVRSRV_ERROR_UNABLE_TO_ROUND_CLOCK_RATE,
	PVRSRV_ERROR_UNABLE_TO_ENABLE_CLOCK,
	PVRSRV_ERROR_UNABLE_TO_GET_CLOCK,
	PVRSRV_ERROR_UNABLE_TO_GET_PARENT_CLOCK,
	PVRSRV_ERROR_UNABLE_TO_GET_SYSTEM_CLOCK,

	PVRSRV_ERROR_UNKNOWN_SGL_ERROR,

	PVRSRV_ERROR_SYSTEM_POWER_CHANGE_FAILURE,
	PVRSRV_ERROR_DEVICE_POWER_CHANGE_FAILURE,

	PVRSRV_ERROR_BAD_SYNC_STATE,

	PVRSRV_ERROR_CACHEOP_FAILED,

	PVRSRV_ERROR_CACHE_INVALIDATE_FAILED,

	PVRSRV_ERROR_FORCE_I32 = 0x7fffffff

} PVRSRV_ERROR;


/*!
 *****************************************************************************
 * List of known device classes.
 *****************************************************************************/
typedef enum _PVRSRV_DEVICE_CLASS_
{
	PVRSRV_DEVICE_CLASS_3D				= 0 ,
	PVRSRV_DEVICE_CLASS_DISPLAY			= 1 ,
	PVRSRV_DEVICE_CLASS_BUFFER			= 2 ,
	PVRSRV_DEVICE_CLASS_VIDEO			= 3 ,

	PVRSRV_DEVICE_CLASS_FORCE_I32 		= 0x7fffffff

} PVRSRV_DEVICE_CLASS;


/*!
 *****************************************************************************
 *	States for power management
 *****************************************************************************/
typedef enum _PVRSRV_SYS_POWER_STATE_
{
	PVRSRV_SYS_POWER_STATE_Unspecified		= -1,	/*!< Unspecified : Uninitialised */
	PVRSRV_SYS_POWER_STATE_D0				= 0,	/*!< On */
	PVRSRV_SYS_POWER_STATE_D1				= 1,	/*!< User Idle */
	PVRSRV_SYS_POWER_STATE_D2				= 2,	/*!< System Idle / sleep */
	PVRSRV_SYS_POWER_STATE_D3				= 3,	/*!< Suspend / Hibernate */
	PVRSRV_SYS_POWER_STATE_D4				= 4,	/*!< shutdown */

	PVRSRV_SYS_POWER_STATE_FORCE_I32 = 0x7fffffff

} PVRSRV_SYS_POWER_STATE, *PPVRSRV_SYS_POWER_STATE;


typedef enum _PVRSRV_DEV_POWER_STATE_
{
	PVRSRV_DEV_POWER_STATE_DEFAULT	= -1,	/*!< Default state for the device */
	PVRSRV_DEV_POWER_STATE_ON		= 0,	/*!< Running */
	PVRSRV_DEV_POWER_STATE_IDLE		= 1,	/*!< Powered but operation paused */
	PVRSRV_DEV_POWER_STATE_OFF		= 2,	/*!< Unpowered */

	PVRSRV_DEV_POWER_STATE_FORCE_I32 = 0x7fffffff

} PVRSRV_DEV_POWER_STATE, *PPVRSRV_DEV_POWER_STATE;	/* PRQA S 3205 */


/* Power transition handler prototypes */
typedef PVRSRV_ERROR (*PFN_PRE_POWER) (IMG_HANDLE				hDevHandle,
									   PVRSRV_DEV_POWER_STATE	eNewPowerState,
									   PVRSRV_DEV_POWER_STATE	eCurrentPowerState);
typedef PVRSRV_ERROR (*PFN_POST_POWER) (IMG_HANDLE				hDevHandle,
										PVRSRV_DEV_POWER_STATE	eNewPowerState,
										PVRSRV_DEV_POWER_STATE	eCurrentPowerState);

/* Clock speed handler prototypes */
typedef PVRSRV_ERROR (*PFN_PRE_CLOCKSPEED_CHANGE) (IMG_HANDLE				hDevHandle,
												   IMG_BOOL					bIdleDevice,
												   PVRSRV_DEV_POWER_STATE	eCurrentPowerState);
typedef PVRSRV_ERROR (*PFN_POST_CLOCKSPEED_CHANGE) (IMG_HANDLE				hDevHandle,
													IMG_BOOL				bIdleDevice,
													PVRSRV_DEV_POWER_STATE	eCurrentPowerState);


/*****************************************************************************
 * Enumeration of all possible pixel types. Where applicable, Ordering of name
 * is in reverse order of memory bytes (i.e. as a word in little endian).
 * e.g. A8R8G8B8 is in memory as 4 bytes in order: BB GG RR AA
 *
 * NOTE: When modifying this structure please update the client driver format
 *       tables located in %WORKROOT%/eurasia/codegen/pixfmts using the tool
 *       located in %WORKROOT%/eurasia/tools/intern/TextureFormatParser.
 *
 *****************************************************************************/
typedef enum _PVRSRV_PIXEL_FORMAT_ {
	/* Basic types */
	PVRSRV_PIXEL_FORMAT_UNKNOWN				=  0,
	PVRSRV_PIXEL_FORMAT_RGB565				=  1,
	PVRSRV_PIXEL_FORMAT_RGB555				=  2,
	PVRSRV_PIXEL_FORMAT_RGB888				=  3,	/*!< 24bit */
	PVRSRV_PIXEL_FORMAT_BGR888				=  4,	/*!< 24bit */
	PVRSRV_PIXEL_FORMAT_GREY_SCALE			=  8,
	PVRSRV_PIXEL_FORMAT_PAL12				= 13,
	PVRSRV_PIXEL_FORMAT_PAL8				= 14,
	PVRSRV_PIXEL_FORMAT_PAL4				= 15,
	PVRSRV_PIXEL_FORMAT_PAL2				= 16,
	PVRSRV_PIXEL_FORMAT_PAL1				= 17,
	PVRSRV_PIXEL_FORMAT_ARGB1555			= 18,
	PVRSRV_PIXEL_FORMAT_ARGB4444			= 19,
	PVRSRV_PIXEL_FORMAT_ARGB8888			= 20,
	PVRSRV_PIXEL_FORMAT_ABGR8888			= 21,
	PVRSRV_PIXEL_FORMAT_YV12				= 22,
	PVRSRV_PIXEL_FORMAT_I420				= 23,
    PVRSRV_PIXEL_FORMAT_IMC2				= 25,
	PVRSRV_PIXEL_FORMAT_XRGB8888			= 26,
	PVRSRV_PIXEL_FORMAT_XBGR8888			= 27,
	PVRSRV_PIXEL_FORMAT_BGRA8888			= 28,
	PVRSRV_PIXEL_FORMAT_XRGB4444			= 29,
	PVRSRV_PIXEL_FORMAT_ARGB8332			= 30,
	PVRSRV_PIXEL_FORMAT_A2RGB10				= 31,	/*!< 32bpp, 10 bits for R, G, B, 2 bits for A */
	PVRSRV_PIXEL_FORMAT_A2BGR10				= 32,	/*!< 32bpp, 10 bits for B, G, R, 2 bits for A */
	PVRSRV_PIXEL_FORMAT_P8					= 33,
	PVRSRV_PIXEL_FORMAT_L8					= 34,
	PVRSRV_PIXEL_FORMAT_A8L8				= 35,
	PVRSRV_PIXEL_FORMAT_A4L4				= 36,
	PVRSRV_PIXEL_FORMAT_L16					= 37,
	PVRSRV_PIXEL_FORMAT_L6V5U5				= 38,
	PVRSRV_PIXEL_FORMAT_V8U8				= 39,
	PVRSRV_PIXEL_FORMAT_V16U16				= 40,
	PVRSRV_PIXEL_FORMAT_QWVU8888			= 41,
	PVRSRV_PIXEL_FORMAT_XLVU8888			= 42,
	PVRSRV_PIXEL_FORMAT_QWVU16				= 43,
	PVRSRV_PIXEL_FORMAT_D16					= 44,
	PVRSRV_PIXEL_FORMAT_D24S8				= 45,
	PVRSRV_PIXEL_FORMAT_D24X8				= 46,

	/* Added to ensure TQ build */
	PVRSRV_PIXEL_FORMAT_ABGR16				= 47,
	PVRSRV_PIXEL_FORMAT_ABGR16F				= 48,
	PVRSRV_PIXEL_FORMAT_ABGR32				= 49,
	PVRSRV_PIXEL_FORMAT_ABGR32F				= 50,
	PVRSRV_PIXEL_FORMAT_B10GR11				= 51,
	PVRSRV_PIXEL_FORMAT_GR88				= 52,
	PVRSRV_PIXEL_FORMAT_BGR32				= 53,
	PVRSRV_PIXEL_FORMAT_GR32				= 54,
	PVRSRV_PIXEL_FORMAT_E5BGR9				= 55,

	/* reserved types */
	PVRSRV_PIXEL_FORMAT_RESERVED1			= 56,
	PVRSRV_PIXEL_FORMAT_RESERVED2			= 57,
	PVRSRV_PIXEL_FORMAT_RESERVED3			= 58,
	PVRSRV_PIXEL_FORMAT_RESERVED4			= 59,
	PVRSRV_PIXEL_FORMAT_RESERVED5			= 60,

	/* RGB space packed formats */
	PVRSRV_PIXEL_FORMAT_R8G8_B8G8			= 61,
	PVRSRV_PIXEL_FORMAT_G8R8_G8B8			= 62,

	/* YUV space planar formats */
	PVRSRV_PIXEL_FORMAT_NV11				= 63,
	PVRSRV_PIXEL_FORMAT_NV12				= 64,

	/* YUV space packed formats */
	PVRSRV_PIXEL_FORMAT_YUY2				= 65,
	PVRSRV_PIXEL_FORMAT_YUV420				= 66,
	PVRSRV_PIXEL_FORMAT_YUV444				= 67,
	PVRSRV_PIXEL_FORMAT_VUY444				= 68,
	PVRSRV_PIXEL_FORMAT_YUYV				= 69,
	PVRSRV_PIXEL_FORMAT_YVYU				= 70,
	PVRSRV_PIXEL_FORMAT_UYVY				= 71,
	PVRSRV_PIXEL_FORMAT_VYUY				= 72,

	PVRSRV_PIXEL_FORMAT_FOURCC_ORG_UYVY		= 73,	/*!< See http://www.fourcc.org/yuv.php#UYVY */
	PVRSRV_PIXEL_FORMAT_FOURCC_ORG_YUYV		= 74,	/*!< See http://www.fourcc.org/yuv.php#YUYV */
	PVRSRV_PIXEL_FORMAT_FOURCC_ORG_YVYU		= 75,	/*!< See http://www.fourcc.org/yuv.php#YVYU */
	PVRSRV_PIXEL_FORMAT_FOURCC_ORG_VYUY		= 76,	/*!< No fourcc.org link */
	PVRSRV_PIXEL_FORMAT_FOURCC_ORG_AYUV		= 77,	/*!< See http://www.fourcc.org/yuv.php#AYUV */

	/* 4 component, 32 bits per component types */
	PVRSRV_PIXEL_FORMAT_A32B32G32R32		= 78,	/*!< type unspecified */
	PVRSRV_PIXEL_FORMAT_A32B32G32R32F		= 79,	/*!< float type */
	PVRSRV_PIXEL_FORMAT_A32B32G32R32_UINT	= 80,	/*!< uint type */
	PVRSRV_PIXEL_FORMAT_A32B32G32R32_SINT	= 81,	/*!< sint type */

	/* 3 component, 32 bits per component types */
	PVRSRV_PIXEL_FORMAT_B32G32R32			= 82,	/*!< type unspecified */
	PVRSRV_PIXEL_FORMAT_B32G32R32F			= 83,	/*!< float data */
	PVRSRV_PIXEL_FORMAT_B32G32R32_UINT		= 84,	/*!< uint data */
	PVRSRV_PIXEL_FORMAT_B32G32R32_SINT		= 85,	/*!< signed int data */

	/* 2 component, 32 bits per component types */
	PVRSRV_PIXEL_FORMAT_G32R32				= 86,	/*!< type unspecified */
	PVRSRV_PIXEL_FORMAT_G32R32F				= 87,	/*!< float */
	PVRSRV_PIXEL_FORMAT_G32R32_UINT			= 88,	/*!< uint */
	PVRSRV_PIXEL_FORMAT_G32R32_SINT			= 89,	/*!< signed int */

	/* 1 component, 32 bits per component types */
	PVRSRV_PIXEL_FORMAT_D32F				= 90,	/*!< float depth */
	PVRSRV_PIXEL_FORMAT_R32					= 91,	/*!< type unspecified */
	PVRSRV_PIXEL_FORMAT_R32F				= 92,	/*!< float type */
	PVRSRV_PIXEL_FORMAT_R32_UINT			= 93,	/*!< unsigned int type */
	PVRSRV_PIXEL_FORMAT_R32_SINT			= 94,	/*!< signed int type */

	/* 4 component, 16 bits per component types */
	PVRSRV_PIXEL_FORMAT_A16B16G16R16		= 95,	/*!< type unspecified */
	PVRSRV_PIXEL_FORMAT_A16B16G16R16F		= 96,	/*!< type float */
	PVRSRV_PIXEL_FORMAT_A16B16G16R16_SINT	= 97,	/*!< signed ints */
	PVRSRV_PIXEL_FORMAT_A16B16G16R16_SNORM	= 98,	/*!< signed normalised int */
	PVRSRV_PIXEL_FORMAT_A16B16G16R16_UINT	= 99,	/*!< unsigned ints */
	PVRSRV_PIXEL_FORMAT_A16B16G16R16_UNORM	= 100,	/*!< normalised unsigned int */

	/* 2 component, 16 bits per component types */
	PVRSRV_PIXEL_FORMAT_G16R16				= 101,	/*!< unspecified type */
	PVRSRV_PIXEL_FORMAT_G16R16F				= 102,	/*!< float type */
	PVRSRV_PIXEL_FORMAT_G16R16_UINT			= 103,	/*!< unsigned int type */
	PVRSRV_PIXEL_FORMAT_G16R16_UNORM		= 104,	/*!< unsigned normalised */
	PVRSRV_PIXEL_FORMAT_G16R16_SINT			= 105,	/*!< signed int  */
	PVRSRV_PIXEL_FORMAT_G16R16_SNORM		= 106,	/*!< signed normalised */

	/* 1 component, 16 bits per component types */
	PVRSRV_PIXEL_FORMAT_R16					= 107,	/*!< type unspecified */
	PVRSRV_PIXEL_FORMAT_R16F				= 108,	/*!< float type */
	PVRSRV_PIXEL_FORMAT_R16_UINT			= 109,	/*!< unsigned int type */
	PVRSRV_PIXEL_FORMAT_R16_UNORM			= 110,	/*!< unsigned normalised int type */
	PVRSRV_PIXEL_FORMAT_R16_SINT			= 111,	/*!< signed int type */
	PVRSRV_PIXEL_FORMAT_R16_SNORM			= 112,	/*!< signed normalised int type */

	/* 4 component, 8 bits per component types */
	PVRSRV_PIXEL_FORMAT_X8R8G8B8			= 113,	/*!< type unspecified */
	PVRSRV_PIXEL_FORMAT_X8R8G8B8_UNORM		= 114,	/*!< normalised unsigned int */
	PVRSRV_PIXEL_FORMAT_X8R8G8B8_UNORM_SRGB	= 115,	/*!< normalised uint with sRGB */

	PVRSRV_PIXEL_FORMAT_A8R8G8B8			= 116,	/*!< type unspecified */
	PVRSRV_PIXEL_FORMAT_A8R8G8B8_UNORM		= 117,	/*!< normalised unsigned int */
	PVRSRV_PIXEL_FORMAT_A8R8G8B8_UNORM_SRGB	= 118,	/*!< normalised uint with sRGB */

	PVRSRV_PIXEL_FORMAT_A8B8G8R8			= 119,	/*!< type unspecified */
	PVRSRV_PIXEL_FORMAT_A8B8G8R8_UINT		= 120,	/*!< unsigned int */
	PVRSRV_PIXEL_FORMAT_A8B8G8R8_UNORM		= 121,	/*!< normalised unsigned int */
	PVRSRV_PIXEL_FORMAT_A8B8G8R8_UNORM_SRGB	= 122,	/*!< normalised unsigned int */
	PVRSRV_PIXEL_FORMAT_A8B8G8R8_SINT		= 123,	/*!< signed int */
	PVRSRV_PIXEL_FORMAT_A8B8G8R8_SNORM		= 124,	/*!< normalised signed int */

	/* 2 component, 8 bits per component types */
	PVRSRV_PIXEL_FORMAT_G8R8				= 125,	/*!< type unspecified */
	PVRSRV_PIXEL_FORMAT_G8R8_UINT			= 126,	/*!< unsigned int type */
	PVRSRV_PIXEL_FORMAT_G8R8_UNORM			= 127,	/*!< unsigned int normalised */
	PVRSRV_PIXEL_FORMAT_G8R8_SINT			= 128,	/*!< signed int type */
	PVRSRV_PIXEL_FORMAT_G8R8_SNORM			= 129,	/*!< signed int normalised */

	/* 1 component, 8 bits per component types */
	PVRSRV_PIXEL_FORMAT_A8					= 130,	/*!< type unspecified, alpha channel */
	PVRSRV_PIXEL_FORMAT_R8					= 131,	/*!< type unspecified */
	PVRSRV_PIXEL_FORMAT_R8_UINT				= 132,	/*!< unsigned int */
	PVRSRV_PIXEL_FORMAT_R8_UNORM			= 133,	/*!< unsigned normalised int */
	PVRSRV_PIXEL_FORMAT_R8_SINT				= 134,	/*!< signed int */
	PVRSRV_PIXEL_FORMAT_R8_SNORM			= 135,	/*!< signed normalised int */

	/* A2RGB10 types */
	PVRSRV_PIXEL_FORMAT_A2B10G10R10			= 136,	/*!< Type unspecified */
	PVRSRV_PIXEL_FORMAT_A2B10G10R10_UNORM	= 137,	/*!< normalised unsigned int */
	PVRSRV_PIXEL_FORMAT_A2B10G10R10_UINT	= 138,	/*!< unsigned int */

	/* F11F11F10 types */
	PVRSRV_PIXEL_FORMAT_B10G11R11			= 139,	/*!< type unspecified */
	PVRSRV_PIXEL_FORMAT_B10G11R11F			= 140,	/*!< float type */

	/* esoteric types */
	PVRSRV_PIXEL_FORMAT_X24G8R32			= 141,	/*!< 64 bit, type unspecified (Usually typed to D32S8 style) */
	PVRSRV_PIXEL_FORMAT_G8R24				= 142,	/*!< 32 bit, type unspecified (Usually typed to D24S8 style) */
	PVRSRV_PIXEL_FORMAT_X8R24				= 143,
	PVRSRV_PIXEL_FORMAT_E5B9G9R9			= 144,	/*!< 32 bit, shared exponent (RGBE). */
	PVRSRV_PIXEL_FORMAT_R1					= 145,	/*!< 1 bit monochrome */

	PVRSRV_PIXEL_FORMAT_RESERVED6			= 146,
	PVRSRV_PIXEL_FORMAT_RESERVED7			= 147,
	PVRSRV_PIXEL_FORMAT_RESERVED8			= 148,
	PVRSRV_PIXEL_FORMAT_RESERVED9			= 149,
	PVRSRV_PIXEL_FORMAT_RESERVED10			= 150,
	PVRSRV_PIXEL_FORMAT_RESERVED11			= 151,
	PVRSRV_PIXEL_FORMAT_RESERVED12			= 152,
	PVRSRV_PIXEL_FORMAT_RESERVED13			= 153,
	PVRSRV_PIXEL_FORMAT_RESERVED14			= 154,
	PVRSRV_PIXEL_FORMAT_RESERVED15			= 155,
	PVRSRV_PIXEL_FORMAT_RESERVED16			= 156,
	PVRSRV_PIXEL_FORMAT_RESERVED17			= 157,
	PVRSRV_PIXEL_FORMAT_RESERVED18			= 158,
	PVRSRV_PIXEL_FORMAT_RESERVED19			= 159,
	PVRSRV_PIXEL_FORMAT_RESERVED20			= 160,

	/* DXLegacy vertex types */
	PVRSRV_PIXEL_FORMAT_UBYTE4				= 161,	/*!< 4 channels, 1 byte per channel, normalised */
	PVRSRV_PIXEL_FORMAT_SHORT4				= 162,	/*!< 4 signed channels, 16 bits each, unnormalised */
	PVRSRV_PIXEL_FORMAT_SHORT4N				= 163,	/*!< 4 signed channels, 16 bits each, normalised */
	PVRSRV_PIXEL_FORMAT_USHORT4N			= 164,	/*!< 4 unsigned channels, 16 bits each, normalised */
	PVRSRV_PIXEL_FORMAT_SHORT2N				= 165,	/*!< 2 signed channels, 16 bits each, normalised */
	PVRSRV_PIXEL_FORMAT_SHORT2				= 166,	/*!< 2 signed channels, 16 bits each, unnormalised */
	PVRSRV_PIXEL_FORMAT_USHORT2N			= 167,	/*!< 2 unsigned channels, 16 bits each, normalised */
	PVRSRV_PIXEL_FORMAT_UDEC3				= 168,	/*!< 3 10-bit channels, unnormalised, unsigned*/
	PVRSRV_PIXEL_FORMAT_DEC3N				= 169,	/*!< 3 10-bit channels, signed normalised */
	PVRSRV_PIXEL_FORMAT_F16_2				= 170,	/*!< 2 F16 channels */
	PVRSRV_PIXEL_FORMAT_F16_4				= 171,	/*!< 4 F16 channels */

	/* misc float types */
	PVRSRV_PIXEL_FORMAT_L_F16				= 172,
	PVRSRV_PIXEL_FORMAT_L_F16_REP			= 173,
	PVRSRV_PIXEL_FORMAT_L_F16_A_F16			= 174,
	PVRSRV_PIXEL_FORMAT_A_F16				= 175,
	PVRSRV_PIXEL_FORMAT_B16G16R16F			= 176,

	PVRSRV_PIXEL_FORMAT_L_F32				= 177,
	PVRSRV_PIXEL_FORMAT_A_F32				= 178,
	PVRSRV_PIXEL_FORMAT_L_F32_A_F32			= 179,

	/* powervr types */
	PVRSRV_PIXEL_FORMAT_PVRTC2				= 180,
	PVRSRV_PIXEL_FORMAT_PVRTC4				= 181,
	PVRSRV_PIXEL_FORMAT_PVRTCII2			= 182,
	PVRSRV_PIXEL_FORMAT_PVRTCII4			= 183,
	PVRSRV_PIXEL_FORMAT_PVRTCIII			= 184,
	PVRSRV_PIXEL_FORMAT_PVRO8				= 185,
	PVRSRV_PIXEL_FORMAT_PVRO88				= 186,
	PVRSRV_PIXEL_FORMAT_PT1					= 187,
	PVRSRV_PIXEL_FORMAT_PT2					= 188,
	PVRSRV_PIXEL_FORMAT_PT4					= 189,
	PVRSRV_PIXEL_FORMAT_PT8					= 190,
	PVRSRV_PIXEL_FORMAT_PTW					= 191,
	PVRSRV_PIXEL_FORMAT_PTB					= 192,
	PVRSRV_PIXEL_FORMAT_MONO8				= 193,
	PVRSRV_PIXEL_FORMAT_MONO16				= 194,

	/* additional YUV types */
	PVRSRV_PIXEL_FORMAT_C0_YUYV				= 195,
	PVRSRV_PIXEL_FORMAT_C0_UYVY				= 196,
	PVRSRV_PIXEL_FORMAT_C0_YVYU				= 197,
	PVRSRV_PIXEL_FORMAT_C0_VYUY				= 198,
	PVRSRV_PIXEL_FORMAT_C1_YUYV				= 199,
	PVRSRV_PIXEL_FORMAT_C1_UYVY				= 200,
	PVRSRV_PIXEL_FORMAT_C1_YVYU				= 201,
	PVRSRV_PIXEL_FORMAT_C1_VYUY				= 202,

	/* planar YUV types */
	PVRSRV_PIXEL_FORMAT_C0_YUV420_2P_UV		= 203,
	PVRSRV_PIXEL_FORMAT_C0_YUV420_2P_VU		= 204,
	PVRSRV_PIXEL_FORMAT_C0_YUV420_3P		= 205,
	PVRSRV_PIXEL_FORMAT_C1_YUV420_2P_UV		= 206,
	PVRSRV_PIXEL_FORMAT_C1_YUV420_2P_VU		= 207,
	PVRSRV_PIXEL_FORMAT_C1_YUV420_3P		= 208,

	PVRSRV_PIXEL_FORMAT_A2B10G10R10F		= 209,
	PVRSRV_PIXEL_FORMAT_B8G8R8_SINT			= 210,
	PVRSRV_PIXEL_FORMAT_PVRF32SIGNMASK		= 211,
	
	PVRSRV_PIXEL_FORMAT_ABGR4444			= 212,	
	PVRSRV_PIXEL_FORMAT_ABGR1555			= 213,
	PVRSRV_PIXEL_FORMAT_BGR565				= 214,			

	/* 4k aligned planar YUV */
	PVRSRV_PIXEL_FORMAT_C0_4KYUV420_2P_UV	= 215,
	PVRSRV_PIXEL_FORMAT_C0_4KYUV420_2P_VU	= 216,
	PVRSRV_PIXEL_FORMAT_C1_4KYUV420_2P_UV	= 217,
	PVRSRV_PIXEL_FORMAT_C1_4KYUV420_2P_VU	= 218,
	PVRSRV_PIXEL_FORMAT_P208				= 219,			
	PVRSRV_PIXEL_FORMAT_A8P8				= 220,			

	PVRSRV_PIXEL_FORMAT_A4					= 221,
	PVRSRV_PIXEL_FORMAT_AYUV8888			= 222,
	PVRSRV_PIXEL_FORMAT_RAW256				= 223,
	PVRSRV_PIXEL_FORMAT_RAW512				= 224,
	PVRSRV_PIXEL_FORMAT_RAW1024				= 225,

	PVRSRV_PIXEL_FORMAT_FORCE_I32			= 0x7fffffff

} PVRSRV_PIXEL_FORMAT;

/*!
 *****************************************************************************
 * Enumeration of possible alpha types.
 *****************************************************************************/
typedef enum _PVRSRV_ALPHA_FORMAT_ {
	PVRSRV_ALPHA_FORMAT_UNKNOWN		=  0x00000000,
	PVRSRV_ALPHA_FORMAT_PRE			=  0x00000001,
	PVRSRV_ALPHA_FORMAT_NONPRE		=  0x00000002,
	PVRSRV_ALPHA_FORMAT_MASK		=  0x0000000F,
} PVRSRV_ALPHA_FORMAT;

/*!
 *****************************************************************************
 * Enumeration of possible alpha types.
 *****************************************************************************/
typedef enum _PVRSRV_COLOURSPACE_FORMAT_ {
	PVRSRV_COLOURSPACE_FORMAT_UNKNOWN		=  0x00000000,
	PVRSRV_COLOURSPACE_FORMAT_LINEAR		=  0x00010000,
	PVRSRV_COLOURSPACE_FORMAT_NONLINEAR		=  0x00020000,
	PVRSRV_COLOURSPACE_FORMAT_MASK			=  0x000F0000,
} PVRSRV_COLOURSPACE_FORMAT;


/*
 * Drawable orientation (in degrees clockwise).
 * Opposite sense from WSEGL.
 */
typedef enum _PVRSRV_ROTATION_ {
	PVRSRV_ROTATE_0		=	0,
	PVRSRV_ROTATE_90	=	1,
	PVRSRV_ROTATE_180	=	2,
	PVRSRV_ROTATE_270	=	3,
	PVRSRV_FLIP_Y

} PVRSRV_ROTATION;

/*!
 * Flags for DisplayClassCreateSwapChain.
 */
#define PVRSRV_CREATE_SWAPCHAIN_SHARED		(1<<0)
#define PVRSRV_CREATE_SWAPCHAIN_QUERY		(1<<1)
#define PVRSRV_CREATE_SWAPCHAIN_OEMOVERLAY	(1<<2)

/*!
 *****************************************************************************
 * Structure providing implementation details for serialisation and
 * synchronisation of operations. This is the fundamental unit on which operations
 * are synced, and would typically be included in any data structures that require
 * serialised accesses etc. e.g. MEM_INFO structures
 *
 *****************************************************************************/
/*
	Sync Data to be shared/mapped between user/kernel
*/
typedef struct _PVRSRV_SYNC_DATA_
{
	/* CPU accessible WriteOp Info */
	IMG_UINT32					ui32WriteOpsPending;
	volatile IMG_UINT32			ui32WriteOpsComplete;

	/* CPU accessible ReadOp Info */
	IMG_UINT32					ui32ReadOpsPending;
	volatile IMG_UINT32			ui32ReadOpsComplete;

	/* CPU accessible ReadOp2 Info */
	IMG_UINT32					ui32ReadOps2Pending;
	volatile IMG_UINT32			ui32ReadOps2Complete;

	/* pdump specific value */
	IMG_UINT32					ui32LastOpDumpVal;
	IMG_UINT32					ui32LastReadOpDumpVal;

	/* Last write oprtation on this sync */
	IMG_UINT64					ui64LastWrite;

} PVRSRV_SYNC_DATA;

/*
	Client Sync Info structure
*/
typedef struct _PVRSRV_CLIENT_SYNC_INFO_
{
	/* mapping of the kernel sync data */
	PVRSRV_SYNC_DATA		*psSyncData;

	/* Device accessible WriteOp Info */
	IMG_DEV_VIRTADDR		sWriteOpsCompleteDevVAddr;

	/* Device accessible ReadOp Info */
	IMG_DEV_VIRTADDR		sReadOpsCompleteDevVAddr;

	/* Device accessible ReadOp2 Info */
	IMG_DEV_VIRTADDR		sReadOps2CompleteDevVAddr;

	/* handle to client mapping data (OS specific) */
	IMG_HANDLE					hMappingInfo;

	/* handle to kernel sync info */
	IMG_HANDLE					hKernelSyncInfo;

} PVRSRV_CLIENT_SYNC_INFO, *PPVRSRV_CLIENT_SYNC_INFO;

/*!
 *****************************************************************************
 * Resource locking structure
 *****************************************************************************/
typedef struct PVRSRV_RESOURCE_TAG
{
	volatile IMG_UINT32 ui32Lock;
	IMG_UINT32 			ui32ID;
}PVRSRV_RESOURCE;
typedef PVRSRV_RESOURCE PVRSRV_RES_HANDLE;


/* command complete callback pfn prototype */
typedef IMG_VOID (*PFN_CMD_COMPLETE) (IMG_HANDLE);
typedef IMG_VOID (**PPFN_CMD_COMPLETE) (IMG_HANDLE);

/* private command handler prototype */
typedef IMG_BOOL (*PFN_CMD_PROC) (IMG_HANDLE, IMG_UINT32, IMG_VOID*);
typedef IMG_BOOL (**PPFN_CMD_PROC) (IMG_HANDLE, IMG_UINT32, IMG_VOID*);


/*
	rectangle structure required by Lock API
*/
typedef struct _IMG_RECT_
{
	IMG_INT32	x0;
	IMG_INT32	y0;
	IMG_INT32	x1;
	IMG_INT32	y1;
}IMG_RECT;

typedef struct _IMG_RECT_16_
{
	IMG_INT16	x0;
	IMG_INT16	y0;
	IMG_INT16	x1;
	IMG_INT16	y1;
}IMG_RECT_16;


/* common pfn between BC/DC */
typedef PVRSRV_ERROR (*PFN_GET_BUFFER_ADDR)(IMG_HANDLE,
											IMG_HANDLE,
											IMG_SYS_PHYADDR**,
											IMG_UINT32*,
											IMG_VOID**,
											IMG_HANDLE*,
											IMG_BOOL*,
											IMG_UINT32*);


/*
	Display dimension structure definition
*/
typedef struct DISPLAY_DIMS_TAG
{
	IMG_UINT32	ui32ByteStride;
	IMG_UINT32	ui32Width;
	IMG_UINT32	ui32Height;
} DISPLAY_DIMS;


/*
	Display format structure definition
*/
typedef struct DISPLAY_FORMAT_TAG
{
	/* pixel format type */
	PVRSRV_PIXEL_FORMAT		pixelformat;
} DISPLAY_FORMAT;

/*
	Display Surface Attributes structure definition
*/
typedef struct DISPLAY_SURF_ATTRIBUTES_TAG
{
	/* pixel format type */
	PVRSRV_PIXEL_FORMAT		pixelformat;
	/* dimensions information structure array */
	DISPLAY_DIMS			sDims;
} DISPLAY_SURF_ATTRIBUTES;


/*
	Display Mode information structure definition
*/
typedef struct DISPLAY_MODE_INFO_TAG
{
	/* pixel format type */
	PVRSRV_PIXEL_FORMAT		pixelformat;
	/* dimensions information structure array */
	DISPLAY_DIMS			sDims;
	/* refresh rate of the display */
	IMG_UINT32				ui32RefreshHZ;
	/* OEM specific flags */
	IMG_UINT32				ui32OEMFlags;
} DISPLAY_MODE_INFO;



#define MAX_DISPLAY_NAME_SIZE	(50) /* arbitrary choice! */

/*
	Display info structure definition
*/
typedef struct DISPLAY_INFO_TAG
{
	/* max swapchains supported */
	IMG_UINT32 ui32MaxSwapChains;
	/* max buffers in a swapchain */
	IMG_UINT32 ui32MaxSwapChainBuffers;
	/* min swap interval supported */
	IMG_UINT32 ui32MinSwapInterval;
	/* max swap interval supported */
	IMG_UINT32 ui32MaxSwapInterval;
	/* physical dimensions of the display required for DPI calc. */
	IMG_UINT32 ui32PhysicalWidthmm;
	IMG_UINT32 ui32PhysicalHeightmm;
	/* display name */
	IMG_CHAR	szDisplayName[MAX_DISPLAY_NAME_SIZE];
#if defined(SUPPORT_HW_CURSOR)
	/* cursor dimensions */
	IMG_UINT16	ui32CursorWidth;
	IMG_UINT16	ui32CursorHeight;
#endif
} DISPLAY_INFO;

typedef struct ACCESS_INFO_TAG
{
	IMG_UINT32		ui32Size;
	IMG_UINT32  	ui32FBPhysBaseAddress;
	IMG_UINT32		ui32FBMemAvailable;			/* size of usable FB memory */
	IMG_UINT32  	ui32SysPhysBaseAddress;
	IMG_UINT32		ui32SysSize;
	IMG_UINT32		ui32DevIRQ;
}ACCESS_INFO;



#if defined(PDUMP_SUSPEND_IS_PER_THREAD)
/** Present only on WinMobile 6.5 */

typedef struct {
	IMG_UINT32 threadId;
	IMG_INT    suspendCount;
} PVRSRV_THREAD_SUSPEND_COUNT;

#define PVRSRV_PDUMP_SUSPEND_Q_NAME "PVRSRVPDumpSuspendMsgQ"
#define PVRSRV_PDUMP_SUSPEND_Q_LENGTH 8

#endif /* defined(PDUMP_SUSPEND_IS_PER_THREAD) */


/*!
 *****************************************************************************
 * This structure is used for OS independent registry (profile) access
 *****************************************************************************/
typedef struct _PVRSRV_REGISTRY_INFO_
{
    IMG_UINT32		ui32DevCookie;
    IMG_PCHAR		pszKey;
    IMG_PCHAR		pszValue;
    IMG_PCHAR		pszBuf;
    IMG_UINT32		ui32BufSize;
} PVRSRV_REGISTRY_INFO, *PPVRSRV_REGISTRY_INFO;


PVRSRV_ERROR IMG_CALLCONV PVRSRVReadRegistryString (PPVRSRV_REGISTRY_INFO psRegInfo);
PVRSRV_ERROR IMG_CALLCONV PVRSRVWriteRegistryString (PPVRSRV_REGISTRY_INFO psRegInfo);


#define PVRSRV_BC_FLAGS_YUVCSC_CONFORMANT_RANGE	(0 << 0)
#define PVRSRV_BC_FLAGS_YUVCSC_FULL_RANGE		(1 << 0)

#define PVRSRV_BC_FLAGS_YUVCSC_BT601			(0 << 1)
#define PVRSRV_BC_FLAGS_YUVCSC_BT709			(1 << 1)

#define MAX_BUFFER_DEVICE_NAME_SIZE	(50) /* arbitrary choice! */

/* buffer information structure */
typedef struct BUFFER_INFO_TAG
{
	IMG_UINT32 			ui32BufferCount;
	IMG_UINT32			ui32BufferDeviceID;
	PVRSRV_PIXEL_FORMAT	pixelformat;
	IMG_UINT32			ui32ByteStride;
	IMG_UINT32			ui32Width;
	IMG_UINT32			ui32Height;
	IMG_UINT32			ui32Flags;
	IMG_CHAR			szDeviceName[MAX_BUFFER_DEVICE_NAME_SIZE];
} BUFFER_INFO;

typedef enum _OVERLAY_DEINTERLACE_MODE_
{
	WEAVE=0x0,
	BOB_ODD,
	BOB_EVEN,
	BOB_EVEN_NONINTERLEAVED
} OVERLAY_DEINTERLACE_MODE;

#endif /* __SERVICESEXT_H__ */
/*****************************************************************************
 End of file (servicesext.h)
*****************************************************************************/
