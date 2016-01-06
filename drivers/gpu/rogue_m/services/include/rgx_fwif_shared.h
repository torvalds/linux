/*************************************************************************/ /*!
@File
@Title          RGX firmware interface structures
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX firmware interface structures shared by both host client
                and host server
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

#if !defined (__RGX_FWIF_SHARED_H__)
#define __RGX_FWIF_SHARED_H__

#include "img_types.h"
#include "rgx_common.h"
#include "devicemem_typedefs.h"


/*!
 ******************************************************************************
 * Device state flags
 *****************************************************************************/
#define RGXKMIF_DEVICE_STATE_ZERO_FREELIST			(0x1 << 0)		/*!< Zeroing the physical pages of reconstructed free lists */
#define RGXKMIF_DEVICE_STATE_FTRACE_EN				(0x1 << 1)		/*!< Used to enable device FTrace thread to consume HWPerf data */
#define RGXKMIF_DEVICE_STATE_DISABLE_DW_LOGGING_EN	(0x1 << 2)		/*!< Used to disable the Devices Watchdog logging */

/* Required memory alignment for 64-bit variables accessible by Meta 
  (the gcc meta aligns 64-bit vars to 64-bit; therefore, mem shared between
   the host and meta that contains 64-bit vars has to maintain this aligment)*/
#define RGXFWIF_FWALLOC_ALIGN	sizeof(IMG_UINT64)

typedef struct _RGXFWIF_DEV_VIRTADDR_
{
	IMG_UINT32	ui32Addr;
} RGXFWIF_DEV_VIRTADDR;

typedef struct _RGXFWIF_DMA_ADDR_
{
	IMG_DEV_VIRTADDR        RGXFW_ALIGN psDevVirtAddr;

#if defined(RGX_FIRMWARE)
	IMG_PBYTE               pbyFWAddr;
#else
	RGXFWIF_DEV_VIRTADDR    pbyFWAddr;
#endif
} UNCACHED_ALIGN RGXFWIF_DMA_ADDR;

typedef IMG_UINT8	RGXFWIF_CCCB;

#if defined(RGX_FIRMWARE)
/* Compiling the actual firmware - use a fully typed pointer */
typedef RGXFWIF_CCCB					*PRGXFWIF_CCCB;
typedef struct _RGXFWIF_CCCB_CTL_		*PRGXFWIF_CCCB_CTL;
typedef struct _RGXFWIF_RENDER_TARGET_	*PRGXFWIF_RENDER_TARGET;
typedef struct _RGXFWIF_HWRTDATA_		*PRGXFWIF_HWRTDATA;
typedef struct _RGXFWIF_FREELIST_		*PRGXFWIF_FREELIST;
typedef struct _RGXFWIF_RAY_FRAME_DATA_	*PRGXFWIF_RAY_FRAME_DATA;
typedef struct _RGXFWIF_RPM_FREELIST_	*PRGXFWIF_RPM_FREELIST;
typedef struct _RGXFWIF_RTA_CTL_		*PRGXFWIF_RTA_CTL;
typedef IMG_UINT32						*PRGXFWIF_UFO_ADDR;
typedef struct _RGXFWIF_CLEANUP_CTL_	*PRGXFWIF_CLEANUP_CTL;
#else
/* Compiling the host driver - use a firmware device virtual pointer */
typedef RGXFWIF_DEV_VIRTADDR	PRGXFWIF_CCCB;
typedef RGXFWIF_DEV_VIRTADDR	PRGXFWIF_CCCB_CTL;
typedef RGXFWIF_DEV_VIRTADDR	PRGXFWIF_RENDER_TARGET;
typedef RGXFWIF_DEV_VIRTADDR	PRGXFWIF_HWRTDATA;
typedef RGXFWIF_DEV_VIRTADDR	PRGXFWIF_FREELIST;
typedef RGXFWIF_DEV_VIRTADDR	PRGXFWIF_RAY_FRAME_DATA;
typedef RGXFWIF_DEV_VIRTADDR	PRGXFWIF_RPM_FREELIST;
typedef RGXFWIF_DEV_VIRTADDR	PRGXFWIF_RTA_CTL;
typedef RGXFWIF_DEV_VIRTADDR	PRGXFWIF_UFO_ADDR;
typedef RGXFWIF_DEV_VIRTADDR	PRGXFWIF_CLEANUP_CTL;
#endif /* RGX_FIRMWARE */


/* FIXME PRGXFWIF_UFO_ADDR and RGXFWIF_UFO should move back into rgx_fwif_client.h */
typedef struct _RGXFWIF_UFO_
{
	PRGXFWIF_UFO_ADDR	puiAddrUFO;
	IMG_UINT32			ui32Value;
} RGXFWIF_UFO;


/*!
	Last reset reason for a context.
*/
typedef enum _RGXFWIF_CONTEXT_RESET_REASON_
{
	RGXFWIF_CONTEXT_RESET_REASON_NONE					= 0,	/*!< No reset reason recorded */
	RGXFWIF_CONTEXT_RESET_REASON_GUILTY_LOCKUP			= 1,	/*!< Caused a reset due to locking up */
	RGXFWIF_CONTEXT_RESET_REASON_INNOCENT_LOCKUP		= 2,	/*!< Affected by another context locking up */
	RGXFWIF_CONTEXT_RESET_REASON_GUILTY_OVERRUNING		= 3,	/*!< Overran the global deadline */
	RGXFWIF_CONTEXT_RESET_REASON_INNOCENT_OVERRUNING	= 4,	/*!< Affected by another context overrunning */
} RGXFWIF_CONTEXT_RESET_REASON;


/*!
	HWRTData state the render is in
*/
typedef enum
{
	RGXFWIF_RTDATA_STATE_NONE = 0,
	RGXFWIF_RTDATA_STATE_KICKTA,
	RGXFWIF_RTDATA_STATE_KICKTAFIRST,
	RGXFWIF_RTDATA_STATE_TAFINISHED,
	RGXFWIF_RTDATA_STATE_KICK3D,
	RGXFWIF_RTDATA_STATE_3DFINISHED,
	RGXFWIF_RTDATA_STATE_TAOUTOFMEM,
	RGXFWIF_RTDATA_STATE_PARTIALRENDERFINISHED,
	RGXFWIF_RTDATA_STATE_HWR					/*!< In case of HWR, we can't set the RTDATA state to NONE,
													 as this will cause any TA to become a first TA.
													 To ensure all related TA's are skipped, we use the HWR state */
} RGXFWIF_RTDATA_STATE;

typedef struct _RGXFWIF_CLEANUP_CTL_
{
	IMG_UINT32				ui32SubmittedCommands;	/*!< Number of commands received by the FW */
	IMG_UINT32				ui32ExecutedCommands;	/*!< Number of commands executed by the FW */
} UNCACHED_ALIGN RGXFWIF_CLEANUP_CTL;


/*!
 ******************************************************************************
 * Client CCB control for RGX
 *****************************************************************************/
typedef struct _RGXFWIF_CCCB_CTL_
{
	IMG_UINT32				ui32WriteOffset;	/*!< write offset into array of commands (MUST be aligned to 16 bytes!) */
	IMG_UINT32				ui32ReadOffset;		/*!< read offset into array of commands */
	IMG_UINT32				ui32DepOffset;		/*!< Dependency offset */
	IMG_UINT32				ui32WrapMask;		/*!< Offset wrapping mask (Total capacity of the CCB - 1) */
} UNCACHED_ALIGN RGXFWIF_CCCB_CTL;

typedef enum 
{
	RGXFW_LOCAL_FREELIST = 0,
	RGXFW_GLOBAL_FREELIST = 1,
#if defined(SUPPORT_MMU_FREELIST)
	RGXFW_MMU_FREELIST = 2,
#endif
	RGXFW_MAX_FREELISTS
} RGXFW_FREELIST_TYPE;

typedef struct _RGXFWIF_RTA_CTL_
{
	IMG_UINT32				ui32RenderTargetIndex;		//Render number
	IMG_UINT32				ui32CurrentRenderTarget;	//index in RTA
	IMG_UINT32				ui32ActiveRenderTargets;	//total active RTs
	IMG_UINT32				ui32CumulActiveRenderTargets;   //total active RTs from the first TA kick, for OOM
#if defined(RGX_FIRMWARE)
	IMG_UINT32				*paui32ValidRenderTargets;	//Array of valid RT indices
	IMG_UINT32              		*paui32NumRenders;  //Array of number of occurred partial renders per render target
#else
	RGXFWIF_DEV_VIRTADDR			paui32ValidRenderTargets;  //Array of valid RT indices
	RGXFWIF_DEV_VIRTADDR    		paui32NumRenders;  //Array of number of occurred partial renders per render target
#endif
	IMG_UINT16              		ui16MaxRTs;   //Number of render targets in the array
} UNCACHED_ALIGN RGXFWIF_RTA_CTL;

typedef struct _RGXFWIF_FREELIST_
{
	IMG_DEV_VIRTADDR	RGXFW_ALIGN psFreeListDevVAddr;
	IMG_UINT64			RGXFW_ALIGN ui64CurrentDevVAddr;
	IMG_UINT32			ui32CurrentStackTop;
	IMG_UINT32			ui32MaxPages;
	IMG_UINT32			ui32GrowPages;
	IMG_UINT32			ui32CurrentPages;
	IMG_UINT32			ui32AllocatedPageCount;
	IMG_UINT32			ui32AllocatedMMUPageCount;
	IMG_UINT32			ui32HWRCounter;
	IMG_UINT32			ui32FreeListID;
	IMG_BOOL			bGrowPending;
} UNCACHED_ALIGN RGXFWIF_FREELIST;

#if defined(RGX_FEATURE_RAY_TRACING)
typedef enum 
{
	RGXFW_RPM_SHF_FREELIST = 0,
	RGXFW_RPM_SHG_FREELIST = 1,
} RGXFW_RPM_FREELIST_TYPE;

#define		RGXFW_MAX_RPM_FREELISTS		(2)

typedef struct _RGXFWIF_RPM_FREELIST_
{
	IMG_DEV_VIRTADDR	RGXFW_ALIGN sFreeListDevVAddr;		/*!< device base address */
	IMG_DEV_VIRTADDR	RGXFW_ALIGN sRPMPageListDevVAddr;	/*!< device base address for RPM pages in-use */
	IMG_UINT32			sSyncAddr;				/*!< Free list sync object for OOM event */
	IMG_UINT32			ui32MaxPages;			/*!< maximum size */
	IMG_UINT32			ui32GrowPages;			/*!< grow size = maximum pages which may be added later */
	IMG_UINT32			ui32CurrentPages;		/*!< number of pages */
	IMG_UINT32			ui32ReadOffset;			/*!< head: where to read alloc'd pages */
	IMG_UINT32			ui32WriteOffset;		/*!< tail: where to write de-alloc'd pages */
	IMG_BOOL			bReadToggle;			/*!< toggle bit for circular buffer */
	IMG_BOOL			bWriteToggle;
	IMG_UINT32			ui32AllocatedPageCount; /*!< TODO: not sure yet if this is useful */
	IMG_UINT32			ui32HWRCounter;
	IMG_UINT32			ui32FreeListID;			/*!< unique ID per device, e.g. rolling counter */
	IMG_BOOL			bGrowPending;			/*!< FW is waiting for host to grow the freelist */
} UNCACHED_ALIGN RGXFWIF_RPM_FREELIST;

typedef struct _RGXFWIF_RAY_FRAME_DATA_
{
	/* state manager for shared state between vertex and ray processing */
	
	/* TODO: not sure if this will be useful, link it here for now */
	IMG_UINT32		sRPMFreeLists[RGXFW_MAX_RPM_FREELISTS];
	
	IMG_BOOL		bAbortOccurred;
	
	/* cleanup state.
	 * Both the SHG and RTU must complete or discard any outstanding work
	 * which references this frame data.
	 */
	RGXFWIF_CLEANUP_CTL		sCleanupStateSHG;
	RGXFWIF_CLEANUP_CTL		sCleanupStateRTU;
	IMG_UINT32				ui32CleanupStatus;
#define HWFRAMEDATA_SHG_CLEAN	(1 << 0)
#define HWFRAMEDATA_RTU_CLEAN	(1 << 1)

} UNCACHED_ALIGN RGXFWIF_RAY_FRAME_DATA;
#endif

typedef struct _RGXFWIF_RENDER_TARGET_
{
	IMG_DEV_VIRTADDR	RGXFW_ALIGN psVHeapTableDevVAddr; /*!< VHeap Data Store */
	IMG_BOOL			bTACachesNeedZeroing;			  /*!< Whether RTC and TPC caches (on mem) need to be zeroed on next first TA kick */

} UNCACHED_ALIGN RGXFWIF_RENDER_TARGET;


typedef struct _RGXFWIF_HWRTDATA_ 
{
	RGXFWIF_RTDATA_STATE	eState;

	IMG_UINT32				ui32NumPartialRenders; /*!< Number of partial renders. Used to setup ZLS bits correctly */
	IMG_DEV_VIRTADDR		RGXFW_ALIGN psPMMListDevVAddr; /*!< MList Data Store */

#if defined(RGX_FEATURE_SCALABLE_TE_ARCH)
	IMG_UINT64				RGXFW_ALIGN ui64VCECatBase[4];
	IMG_UINT64				RGXFW_ALIGN ui64VCELastCatBase[4];
	IMG_UINT64				RGXFW_ALIGN ui64TECatBase[4];
	IMG_UINT64				RGXFW_ALIGN ui64TELastCatBase[4];
#else
	IMG_UINT64				RGXFW_ALIGN ui64VCECatBase;
	IMG_UINT64				RGXFW_ALIGN ui64VCELastCatBase;
	IMG_UINT64				RGXFW_ALIGN ui64TECatBase;
	IMG_UINT64				RGXFW_ALIGN ui64TELastCatBase;
#endif
	IMG_UINT64				RGXFW_ALIGN ui64AlistCatBase;
	IMG_UINT64				RGXFW_ALIGN ui64AlistLastCatBase;

#if defined(SUPPORT_VFP)
	IMG_DEV_VIRTADDR		RGXFW_ALIGN sVFPPageTableAddr;
#endif
	IMG_UINT64				RGXFW_ALIGN ui64PMAListStackPointer;
	IMG_UINT32				ui32PMMListStackPointer;

	PRGXFWIF_FREELIST 		RGXFW_ALIGN apsFreeLists[RGXFW_MAX_FREELISTS]; 
	IMG_UINT32				aui32FreeListHWRSnapshot[RGXFW_MAX_FREELISTS];
	
	PRGXFWIF_RENDER_TARGET	psParentRenderTarget;

	RGXFWIF_CLEANUP_CTL		sTACleanupState;
	RGXFWIF_CLEANUP_CTL		s3DCleanupState;
	IMG_UINT32				ui32CleanupStatus;
#define HWRTDATA_TA_CLEAN	(1 << 0)
#define HWRTDATA_3D_CLEAN	(1 << 1)

	PRGXFWIF_RTA_CTL		psRTACtl;

	IMG_UINT32				bHasLastTA;
	IMG_BOOL				bPartialRendered;

	IMG_UINT32				ui32PPPScreen;
	IMG_UINT32				ui32PPPGridOffset;
	IMG_UINT64				RGXFW_ALIGN ui64PPPMultiSampleCtl;
	IMG_UINT32				ui32TPCStride;
	IMG_DEV_VIRTADDR		RGXFW_ALIGN sTailPtrsDevVAddr;
	IMG_UINT32				ui32TPCSize;
	IMG_UINT32				ui32TEScreen;
	IMG_UINT32				ui32MTileStride;
	IMG_UINT32				ui32TEAA;
	IMG_UINT32				ui32TEMTILE1;
	IMG_UINT32				ui32TEMTILE2;
	IMG_UINT32				ui32ISPMergeLowerX;
	IMG_UINT32				ui32ISPMergeLowerY;
	IMG_UINT32				ui32ISPMergeUpperX;
	IMG_UINT32				ui32ISPMergeUpperY;
	IMG_UINT32				ui32ISPMergeScaleX;
	IMG_UINT32				ui32ISPMergeScaleY;
} UNCACHED_ALIGN RGXFWIF_HWRTDATA;

typedef enum
{
	RGXFWIF_ZSBUFFER_UNBACKED = 0,
	RGXFWIF_ZSBUFFER_BACKED,
	RGXFWIF_ZSBUFFER_BACKING_PENDING,
	RGXFWIF_ZSBUFFER_UNBACKING_PENDING,
}RGXFWIF_ZSBUFFER_STATE;

typedef struct _RGXFWIF_ZSBUFFER_
{
	IMG_UINT32				ui32ZSBufferID;				/*!< Buffer ID*/
	IMG_BOOL				bOnDemand;					/*!< Needs On-demand ZS Buffer allocation */
	RGXFWIF_ZSBUFFER_STATE	eState;						/*!< Z/S-Buffer state */
	RGXFWIF_CLEANUP_CTL		sCleanupState;				/*!< Cleanup state */
} UNCACHED_ALIGN RGXFWIF_FWZSBUFFER;

/* Number of BIF tiling configurations / heaps */
#define RGXFWIF_NUM_BIF_TILING_CONFIGS 4

/*!
 *****************************************************************************
 * RGX Compatibility checks
 *****************************************************************************/
/* WARNING: RGXFWIF_COMPCHECKS_BVNC_V_LEN_MAX can be increased only and
		always equal to (N * sizeof(IMG_UINT32) - 1) */
#define RGXFWIF_COMPCHECKS_BVNC_V_LEN_MAX 3 /* WARNING: Do not change this macro without changing 
			accesses from dword to byte in function rgx_bvnc_packed() */

/* WARNING: Whenever the layout of RGXFWIF_COMPCHECKS_BVNC is a subject of change,
	following define should be increased by 1 to indicate to compatibility logic, 
	that layout has changed */
#define RGXFWIF_COMPCHECKS_LAYOUT_VERSION 1

typedef struct _RGXFWIF_COMPCHECKS_BVNC_
{
	IMG_UINT32	ui32LayoutVersion; /* WARNING: This field must be defined as first one in this structure */
	IMG_UINT32  ui32VLenMax;
	IMG_UINT32	ui32BNC;
	IMG_CHAR	aszV[RGXFWIF_COMPCHECKS_BVNC_V_LEN_MAX + 1];
} UNCACHED_ALIGN RGXFWIF_COMPCHECKS_BVNC;

#define RGXFWIF_COMPCHECKS_BVNC_DECLARE_AND_INIT(name) RGXFWIF_COMPCHECKS_BVNC name = { RGXFWIF_COMPCHECKS_LAYOUT_VERSION, RGXFWIF_COMPCHECKS_BVNC_V_LEN_MAX }
#define RGXFWIF_COMPCHECKS_BVNC_INIT(name) do { (name).ui32LayoutVersion = RGXFWIF_COMPCHECKS_LAYOUT_VERSION; \
												(name).ui32VLenMax = RGXFWIF_COMPCHECKS_BVNC_V_LEN_MAX; } while (0)

typedef struct _RGXFWIF_COMPCHECKS_
{
	RGXFWIF_COMPCHECKS_BVNC		sHWBVNC;			/*!< hardware BNC (from the RGX registers) */
	RGXFWIF_COMPCHECKS_BVNC		sFWBVNC;			/*!< firmware BNC */
	IMG_UINT32					ui32METAVersion;
	IMG_UINT32					ui32DDKVersion;		/*!< software DDK version */
	IMG_UINT32					ui32DDKBuild;		/*!< software DDK build no. */
	IMG_UINT32					ui32BuildOptions;	/*!< build options bit-field */
	IMG_BOOL					bUpdated;			/*!< Information is valid */
} UNCACHED_ALIGN RGXFWIF_COMPCHECKS;


#define GET_CCB_SPACE(WOff, ROff, CCBSize) \
	((((ROff) - (WOff)) + ((CCBSize) - 1)) & ((CCBSize) - 1))

#define UPDATE_CCB_OFFSET(Off, PacketSize, CCBSize) \
	(Off) = (((Off) + (PacketSize)) & ((CCBSize) - 1))

#define RESERVED_CCB_SPACE 		(sizeof(IMG_UINT32))


/* Defines relating to the per-context CCBs */
#define RGX_CCB_SIZE_LOG2			(16) /* 64kB */
#define RGX_CCB_ALLOCGRAN			(64)
#define RGX_CCB_TYPE_TASK			(1 << 31)
#define RGX_CCB_FWALLOC_ALIGN(size)	(((size) + (RGXFWIF_FWALLOC_ALIGN-1)) & ~(RGXFWIF_FWALLOC_ALIGN - 1))

/*!
 ******************************************************************************
 * Client CCB commands for RGX
 *****************************************************************************/
typedef enum _RGXFWIF_CCB_CMD_TYPE_
{
	RGXFWIF_CCB_CMD_TYPE_TA			= 201 | RGX_CCB_TYPE_TASK,
	RGXFWIF_CCB_CMD_TYPE_3D			= 202 | RGX_CCB_TYPE_TASK,
	RGXFWIF_CCB_CMD_TYPE_CDM		= 203 | RGX_CCB_TYPE_TASK,
	RGXFWIF_CCB_CMD_TYPE_TQ_3D		= 204 | RGX_CCB_TYPE_TASK,
	RGXFWIF_CCB_CMD_TYPE_TQ_2D		= 205 | RGX_CCB_TYPE_TASK,
	RGXFWIF_CCB_CMD_TYPE_3D_PR		= 206 | RGX_CCB_TYPE_TASK,
	RGXFWIF_CCB_CMD_TYPE_NULL		= 207 | RGX_CCB_TYPE_TASK,
	RGXFWIF_CCB_CMD_TYPE_SHG		= 208 | RGX_CCB_TYPE_TASK,
	RGXFWIF_CCB_CMD_TYPE_RTU		= 209 | RGX_CCB_TYPE_TASK,
	RGXFWIF_CCB_CMD_TYPE_RTU_FC		  = 210 | RGX_CCB_TYPE_TASK,
	RGXFWIF_CCB_CMD_TYPE_PRE_TIMESTAMP = 211 | RGX_CCB_TYPE_TASK,

/* Leave a gap between CCB specific commands and generic commands */
	RGXFWIF_CCB_CMD_TYPE_FENCE          = 212,
	RGXFWIF_CCB_CMD_TYPE_UPDATE         = 213,
	RGXFWIF_CCB_CMD_TYPE_RMW_UPDATE     = 214,
	RGXFWIF_CCB_CMD_TYPE_FENCE_PR       = 215,
	RGXFWIF_CCB_CMD_TYPE_PRIORITY       = 216,
/* Pre and Post timestamp commands are supposed to sandwich the DM cmd. The
   padding code with the CCB wrap upsets the FW if we don't have the task type
   bit cleared for POST_TIMESTAMPs. That's why we have 2 different cmd types.
*/
	RGXFWIF_CCB_CMD_TYPE_POST_TIMESTAMP = 217,
	RGXFWIF_CCB_CMD_TYPE_UNFENCED_UPDATE = 218,
	RGXFWIF_CCB_CMD_TYPE_UNFENCED_RMW_UPDATE = 219,
	
	RGXFWIF_CCB_CMD_TYPE_PADDING	= 220,
} RGXFWIF_CCB_CMD_TYPE;

typedef struct _RGXFWIF_CCB_CMD_HEADER_
{
	RGXFWIF_CCB_CMD_TYPE	eCmdType;
	IMG_UINT32				ui32CmdSize;
} RGXFWIF_CCB_CMD_HEADER;

typedef enum _RGXFWIF_PWR_EVT_
{
	RGXFWIF_PWR_EVT_PWR_ON,			/* Sidekick power event */
	RGXFWIF_PWR_EVT_DUST_CHANGE,		/* Rascal / dust power event */
	RGXFWIF_PWR_EVT_ALL			/* Applies to all power events. Keep as last element */
} RGXFWIF_PWR_EVT;

typedef struct _RGXFWIF_REG_CFG_REC_
{
	IMG_UINT64		ui64Addr;
	IMG_UINT64		ui64Value;
} RGXFWIF_REG_CFG_REC;


typedef struct _RGXFWIF_TIME_CORR_
{
	IMG_UINT64 RGXFW_ALIGN ui64OSTimeStamp;
	IMG_UINT64 RGXFW_ALIGN ui64CRTimeStamp;
	IMG_UINT32             ui32CoreClockSpeed;

	/* Utility variable used to convert CR timer deltas to OS timer deltas (nS),
	 * where the deltas are relative to the timestamps above:
	 * deltaOS = (deltaCR * K) >> decimal_shift, see full explanation below */
	IMG_UINT32             ui32CRDeltaToOSDeltaKNs;
} UNCACHED_ALIGN RGXFWIF_TIME_CORR;

typedef struct _RGXFWIF_TIMESTAMP_
{
	RGXFWIF_TIME_CORR      sTimeCorr;
	IMG_UINT64 RGXFW_ALIGN ui64Timestamp;
} UNCACHED_ALIGN RGXFWIF_TIMESTAMP;


/* These macros are used to help converting FW timestamps to the Host time domain.
 * On the FW the RGX_CR_TIMER counter is used to keep track of the time;
 * it increments by 1 every 256 GPU clock ticks, so the general formula
 * to perform the conversion is:
 *
 * [ GPU clock speed in Hz, if (scale == 10^9) then deltaOS is in nS,
 *   otherwise if (scale == 10^6) then deltaOS is in uS ]
 *
 *             deltaCR * 256                                   256 * scale
 *  deltaOS = --------------- * scale = deltaCR * K    [ K = --------------- ]
 *             GPUclockspeed                                  GPUclockspeed
 *
 * The actual K is multiplied by 2^20 (and deltaCR * K is divided by 2^20)
 * to get some better accuracy and to avoid returning 0 in the integer
 * division 256000000/GPUfreq if GPUfreq is greater than 256MHz.
 * This is the same as keeping K as a decimal number.
 *
 * The maximum deltaOS is slightly more than 5hrs for all GPU frequencies
 * (deltaCR * K is more or less a costant), and it's relative to
 * the base OS timestamp sampled as a part of the timer correlation data.
 * This base is refreshed on GPU power-on, DVFS transition and
 * periodic frequency calibration (executed every few seconds if the FW is
 * doing some work), so as long as the GPU is doing something and one of these
 * events is triggered then deltaCR * K will not overflow and deltaOS will be
 * correct.
 */

#define RGXFWIF_CRDELTA_TO_OSDELTA_ACCURACY_SHIFT  (20)

#define RGXFWIF_GET_CRDELTA_TO_OSDELTA_K_NS(clockfreq, remainder) \
	OSDivide64((256000000ULL << RGXFWIF_CRDELTA_TO_OSDELTA_ACCURACY_SHIFT), \
	           ((clockfreq) + 500) / 1000, \
	           &(remainder))

#define RGXFWIF_GET_DELTA_OSTIME_NS(deltaCR, K) \
	( ((deltaCR) * (K)) >> RGXFWIF_CRDELTA_TO_OSDELTA_ACCURACY_SHIFT)

#define RGXFWIF_GET_DELTA_OSTIME_US(deltacr, clockfreq, remainder) \
	OSDivide64r64((deltacr) * 256000, ((clockfreq) + 500) / 1000, &(remainder))

/* Use this macro to get a more realistic GPU core clock speed than
 * the one given by the upper layers (used when doing GPU frequency
 * calibration)
 */
#define RGXFWIF_GET_GPU_CLOCK_FREQUENCY_HZ(deltacr_us, deltaos_us, remainder) \
	OSDivide64((deltacr_us) * 256000000, (deltaos_us), &(remainder))

#endif /*  __RGX_FWIF_SHARED_H__ */

/******************************************************************************
 End of file (rgx_fwif_shared.h)
******************************************************************************/


