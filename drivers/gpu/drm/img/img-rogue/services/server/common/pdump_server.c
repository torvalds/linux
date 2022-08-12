/*************************************************************************/ /*!
@File
@Title          Common Server PDump functions layer
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

#if defined(PDUMP)

#if defined(__linux__)
 #include <linux/version.h>
 #if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
  #include <linux/stdarg.h>
 #else
  #include <stdarg.h>
 #endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) */
#else
 #include <stdarg.h>
#endif /* __linux__ */

#include "pvrversion.h"
#include "allocmem.h"
#include "osfunc.h"
#include "pvrsrv.h"
#include "pvr_debug.h"
#include "srvkm.h"
#include "pdump_physmem.h"
#include "hash.h"
#include "connection_server.h"
#include "services_km.h"
#include <powervr/buffer_attribs.h>
#include "oskm_apphint.h"

/* pdump headers */
#include "tlstream.h"
#include "pdump_km.h"

#include "pdumpdesc.h"
#include "rgxpdump.h"

#include "tutilsdefs.h"
#include "tutils_km.h"
/* Allow temporary buffer size override */
#if !defined(PDUMP_TEMP_BUFFER_SIZE)
#define PDUMP_TEMP_BUFFER_SIZE (64 * 1024U)
#endif

#define	PTR_PLUS(t, p, x) ((t)(((IMG_CHAR *)(p)) + (x)))
#define	VPTR_PLUS(p, x) PTR_PLUS(void *, p, x)
#define	VPTR_INC(p, x) ((p) = VPTR_PLUS(p, x))
#define MAX_PDUMP_MMU_CONTEXTS	(32)

#define PRM_FILE_SIZE_MAX	0x7FDFFFFFU /*!< Default maximum file size to split output files, 2GB-2MB as fwrite limits it to 2GB-1 on 32bit systems */

#define MAX_PDUMP_WRITE_RETRIES	200	/*!< Max number of retries to dump pdump data in to respective buffers */

/* 'Magic' cookie used in this file only, where no psDeviceNode is available
 * but writing to the PDump log should be permitted
 */
#define PDUMP_MAGIC_COOKIE 0x9E0FF

static ATOMIC_T		g_sConnectionCount;

/*
 * Structure to store some essential attributes of a PDump stream buffer.
 */
typedef struct
{
	IMG_CHAR*  pszName;                     /*!< Name of the PDump TL Stream buffer */
	IMG_HANDLE hTL;                         /*!< Handle of created TL stream buffer */
	IMG_UINT32 ui32BufferSize;              /*!< The size of the buffer in bytes */
	IMG_UINT32 ui32BufferFullRetries;       /*!< The number of times the buffer got full */
	IMG_UINT32 ui32BufferFullAborts;        /*!< The number of times we failed to write data */
	IMG_UINT32 ui32HighestRetriesWatermark; /*!< Max number of retries try to dump pdump data */
	IMG_UINT32 ui32MaxAllowedWriteSize;     /*!< Max allowed write packet size */
} PDUMP_STREAM;

typedef struct
{
	PDUMP_STREAM sInitStream;   /*!< Driver initialisation PDump stream */
	PDUMP_STREAM sMainStream;   /*!< App framed PDump stream */
	PDUMP_STREAM sDeinitStream; /*!< Driver/HW de-initialisation PDump stream */
	PDUMP_STREAM sBlockStream;  /*!< Block mode PDump block data stream - currently its script only */
} PDUMP_CHANNEL;

typedef struct
{
	PDUMP_CHANNEL sCh;         /*!< Channel handles */
	IMG_UINT32    ui32FileIdx; /*!< File index gets incremented on script out file split */
} PDUMP_SCRIPT;

typedef struct
{
	IMG_UINT32    ui32Init;    /*!< Count of bytes written to the init phase stream */
	IMG_UINT32    ui32Main;    /*!< Count of bytes written to the main stream */
	IMG_UINT32    ui32Deinit;  /*!< Count of bytes written to the deinit stream */
	IMG_UINT32    ui32Block;   /*!< Count of bytes written to the block stream */
} PDUMP_CHANNEL_WOFFSETS;

typedef struct
{
	PDUMP_CHANNEL          sCh;             /*!< Channel handles */
	PDUMP_CHANNEL_WOFFSETS sWOff;           /*!< Channel file write offsets */
	IMG_UINT32             ui32FileIdx;     /*!< File index used when file size limit reached and a new file is started, parameter channel only */
	IMG_UINT32             ui32MaxFileSize; /*!< Maximum file size for parameter files */

	PDUMP_FILEOFFSET_T     uiZeroPageOffset; /*!< Offset of the zero page in the parameter file */
	size_t                 uiZeroPageSize;   /*!< Size of the zero page in the parameter file */
	IMG_CHAR               szZeroPageFilename[PDUMP_PARAM_MAX_FILE_NAME]; /*< PRM file name where the zero page was pdumped */
} PDUMP_PARAMETERS;

/* PDump lock to keep pdump write atomic.
 * Which will protect g_PDumpScript & g_PDumpParameters pdump
 * specific shared variable.
 */
static POS_LOCK g_hPDumpWriteLock;

static PDUMP_SCRIPT     g_PDumpScript    = { {
		{	PDUMP_SCRIPT_INIT_STREAM_NAME,   NULL,
			PDUMP_SCRIPT_INIT_STREAM_SIZE,   0, 0, 0 },
		{	PDUMP_SCRIPT_MAIN_STREAM_NAME,   NULL,
			PDUMP_SCRIPT_MAIN_STREAM_SIZE,   0, 0, 0 },
		{	PDUMP_SCRIPT_DEINIT_STREAM_NAME, NULL,
			PDUMP_SCRIPT_DEINIT_STREAM_SIZE, 0, 0, 0 },
		{	PDUMP_SCRIPT_BLOCK_STREAM_NAME,   NULL,
			PDUMP_SCRIPT_BLOCK_STREAM_SIZE,   0, 0, 0 },
		}, 0 };
static PDUMP_PARAMETERS g_PDumpParameters = { {
		{	PDUMP_PARAM_INIT_STREAM_NAME,   NULL,
			PDUMP_PARAM_INIT_STREAM_SIZE,   0, 0, 0 },
		{	PDUMP_PARAM_MAIN_STREAM_NAME,   NULL,
			PDUMP_PARAM_MAIN_STREAM_SIZE,   0, 0, 0 },
		{	PDUMP_PARAM_DEINIT_STREAM_NAME, NULL,
			PDUMP_PARAM_DEINIT_STREAM_SIZE, 0, 0, 0 },
		{	PDUMP_PARAM_BLOCK_STREAM_NAME,   NULL,
			PDUMP_PARAM_BLOCK_STREAM_SIZE,   0, 0, 0 },
		}, {0, 0, 0, 0}, 0, PRM_FILE_SIZE_MAX};


#if defined(PDUMP_DEBUG_OUTFILES)
/* counter increments each time debug write is called */
ATOMIC_T g_sEveryLineCounter;
#endif

// #define PDUMP_DEBUG_TRANSITION
#if defined(PDUMP_DEBUG_TRANSITION)
# define DEBUG_OUTFILES_COMMENT(dev, fmt, ...) (void)PDumpCommentWithFlags(dev, PDUMP_FLAGS_CONTINUOUS, fmt, __VA_ARGS__)
#else
# define DEBUG_OUTFILES_COMMENT(dev, fmt, ...)
#endif

#if defined(PDUMP_DEBUG) || defined(REFCOUNT_DEBUG)
# define PDUMP_REFCOUNT_PRINT(fmt, ...) PVRSRVDebugPrintf(PVR_DBG_WARNING, __FILE__, __LINE__, fmt, __VA_ARGS__)
#else
# define PDUMP_REFCOUNT_PRINT(fmt, ...)
#endif

/* Prototype for the test/debug state dump routine used in debugging */
#if defined(PDUMP_TRACE_STATE) || defined(PVR_TESTING_UTILS)
void PDumpCommonDumpState(void);
#endif


/*****************************************************************************/
/* PDump Control Module Definitions                                          */
/*****************************************************************************/

/*
 * struct _PDUMP_CAPTURE_RANGE_ is interpreted differently in different modes of PDump
 *
 * Non-Block mode:
 *    ui32Start     - Start frame number of range
 *    ui32End       - End frame number of range
 *    ui32Interval  - Frame sample rate interval
 *
 * Block mode:
 *    ui32Start     - If set to '0', first PDump-block will of minimal (i.e. PDUMP_BLOCKLEN_MIN)
 *                    length, else all blocks will be of block-length provided
 *
 *    ui32End       - By default this is set to PDUMP_FRAME_MAX so that Blocked PDump
 *                    will be captured indefinitely till stopped externally. On force capture
 *                    stop, this will be set to (ui32CurrentFrame + 1) to stop capture from
 *                    next frame onwards
 *
 *    ui32Interval  - This will be interpreted as PDump block-length provided
 **/
typedef struct _PDUMP_CAPTURE_RANGE_
{
	IMG_UINT32 ui32Start;
	IMG_UINT32 ui32End;
	IMG_UINT32 ui32Interval;
} PDUMP_CAPTURE_RANGE;

/* PDump Block mode specific controls */
typedef struct _PDUMP_BLOCK_CTRL_
{
	IMG_UINT32 ui32BlockLength;       /*!< PDump block length in term of number of frames per block */
	IMG_UINT32 ui32CurrentBlock;      /*!< Current block number */
} PDUMP_BLOCK_CTRL;

/*! PDump common module State Machine states */
typedef enum _PDUMP_SM_
{
	PDUMP_SM_UNINITIALISED,           /*!< Starting state */
	PDUMP_SM_INITIALISING,            /*!< Module is initialising */
	PDUMP_SM_READY,                   /*!< Module is initialised and ready */
	PDUMP_SM_READY_CLIENT_CONNECTED,  /*!< Module is ready and capture client connected */
	PDUMP_SM_FORCED_SUSPENDED,        /*!< Module forced error, PDumping suspended, this is to force driver reload before next capture */
	PDUMP_SM_ERROR_SUSPENDED,         /*!< Module fatal error, PDumping suspended semi-final state */
	PDUMP_SM_DEINITIALISED            /*!< Final state */
} PDUMP_SM;

/*! PDump control flags */
#define FLAG_IS_DRIVER_IN_INIT_PHASE 0x1  /*! Control flag that keeps track of State of driver initialisation phase */
#define FLAG_IS_IN_CAPTURE_RANGE     0x2  /*! Control flag that keeps track of Current capture status, is current frame in range */
#define FLAG_IS_IN_CAPTURE_INTERVAL  0x4  /*! Control flag that keeps track of Current capture status, is current frame in an interval where no capture takes place. */

#define CHECK_PDUMP_CONTROL_FLAG(PDUMP_CONTROL_FLAG) BITMASK_HAS(g_PDumpCtrl.ui32Flags, PDUMP_CONTROL_FLAG)
#define SET_PDUMP_CONTROL_FLAG(PDUMP_CONTROL_FLAG)   BITMASK_SET(g_PDumpCtrl.ui32Flags, PDUMP_CONTROL_FLAG)
#define UNSET_PDUMP_CONTROL_FLAG(PDUMP_CONTROL_FLAG) BITMASK_UNSET(g_PDumpCtrl.ui32Flags, PDUMP_CONTROL_FLAG)

/* No direct access to members from outside the control module - please */
typedef struct _PDUMP_CTRL_STATE_
{
	PDUMP_SM            eServiceState;      /*!< State of the pdump_common module */
	IMG_UINT32          ui32Flags;

	IMG_UINT32          ui32DefaultCapMode; /*!< Capture mode of the dump */
	PDUMP_CAPTURE_RANGE sCaptureRange;      /*|< The capture range for capture mode 'framed' */
	IMG_UINT32          ui32CurrentFrame;   /*!< Current frame number */

	PDUMP_BLOCK_CTRL    sBlockCtrl;         /*!< Pdump block mode ctrl data */

	POS_LOCK            hLock;              /*!< Exclusive lock to this structure */
	IMG_PID             InPowerTransitionPID;/*!< pid of thread requesting power transition */
} PDUMP_CTRL_STATE;

static PDUMP_CTRL_STATE g_PDumpCtrl =
{
	PDUMP_SM_UNINITIALISED,

	FLAG_IS_DRIVER_IN_INIT_PHASE,

	PDUMP_CAPMODE_UNSET,
	{
		PDUMP_FRAME_UNSET,
		PDUMP_FRAME_UNSET,
		0
	},
	0,

	{
		0,
		PDUMP_BLOCKNUM_INVALID,
	},

	NULL,
	0
};

static void PDumpAssertWriteLockHeld(void);

#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)

/*************************************************************************/ /*!
 @Function		PDumpCreateIncVarNameStr
 @Description	When 64 bit register access is split between two 32 bit
	accesses, it needs two PDump Internal variables to store register value.
	This function creates the string for the second PDump Internal variable
	for example if Passed Variable name is :SYSMEM:$1 this function will
	generate the string :SYSMEM:$2

 @Input	pszInternalVar	String for PDump internal variable in use

 @Return IMG_CHAR*  String for second PDump internal variable to be used
*/ /**************************************************************************/
static INLINE IMG_CHAR* PDumpCreateIncVarNameStr(const IMG_CHAR* pszInternalVar)
{
	IMG_CHAR *pszPDumpVarName;
	IMG_UINT32 ui32Size = (IMG_UINT32)OSStringLength(pszInternalVar);
	if (ui32Size == 0)
	{
		return NULL;
	}

	ui32Size++;
	pszPDumpVarName = (IMG_CHAR*)OSAllocMem((ui32Size) * sizeof(IMG_CHAR));
	if (pszPDumpVarName == NULL)
	{
		return NULL;
	}

	OSStringLCopy(pszPDumpVarName, pszInternalVar, ui32Size);
	/* Increase the number on the second variable */
	pszPDumpVarName[ui32Size-2] += 1;
	return pszPDumpVarName;
}

/*************************************************************************/ /*!
 @Function		PDumpFreeIncVarNameStr
 @Description	Free the string created by function PDumpCreateIncVarNameStr

 @Input	pszPDumpVarName String to free

 @Return	void
*/ /**************************************************************************/
static INLINE void PDumpFreeIncVarNameStr(IMG_CHAR* pszPDumpVarName)
{
	if (pszPDumpVarName != NULL)
	{
		OSFreeMem(pszPDumpVarName);
	}
}
#endif

static PVRSRV_ERROR PDumpCtrlInit(void)
{
	g_PDumpCtrl.eServiceState = PDUMP_SM_INITIALISING;

	/* Create lock for PDUMP_CTRL_STATE struct, which is shared between pdump client
	   and PDumping app. This lock will help us serialize calls from pdump client
	   and PDumping app */
	PVR_LOG_RETURN_IF_ERROR(OSLockCreate(&g_PDumpCtrl.hLock), "OSLockCreate");

	return PVRSRV_OK;
}

static void PDumpCtrlDeInit(void)
{
	if (g_PDumpCtrl.hLock)
	{
		OSLockDestroy(g_PDumpCtrl.hLock);
		g_PDumpCtrl.hLock = NULL;
	}
}

static INLINE void PDumpCtrlLockAcquire(void)
{
	OSLockAcquire(g_PDumpCtrl.hLock);
}

static INLINE void PDumpCtrlLockRelease(void)
{
	OSLockRelease(g_PDumpCtrl.hLock);
}

static INLINE PDUMP_SM PDumpCtrlGetModuleState(void)
{
	return g_PDumpCtrl.eServiceState;
}

PVRSRV_ERROR PDumpReady(void)
{
	switch (PDumpCtrlGetModuleState())
	{
	case PDUMP_SM_READY:
	case PDUMP_SM_READY_CLIENT_CONNECTED:
		return PVRSRV_OK;

	case PDUMP_SM_FORCED_SUSPENDED:
	case PDUMP_SM_ERROR_SUSPENDED:
		return PVRSRV_ERROR_PDUMP_NOT_ACTIVE;

	case PDUMP_SM_UNINITIALISED:
	case PDUMP_SM_INITIALISING:
	case PDUMP_SM_DEINITIALISED:
		return PVRSRV_ERROR_PDUMP_NOT_AVAILABLE;

	default:
		/* Bad state */
		PVR_ASSERT(1);
		return PVRSRV_ERROR_BAD_MAPPING;
	}
}


/******************************************************************************
	NOTE:
	The following PDumpCtrl*** functions require the PDUMP_CTRL_STATE lock be
	acquired BEFORE they are called. This is because the PDUMP_CTRL_STATE data
	is shared between the PDumping App and the PDump client, hence an exclusive
	access is required. The lock can be acquired and released by using the
	PDumpCtrlLockAcquire & PDumpCtrlLockRelease functions respectively.
******************************************************************************/

static void PDumpCtrlUpdateCaptureStatus(void)
{
	if (g_PDumpCtrl.ui32DefaultCapMode == PDUMP_CAPMODE_FRAMED)
	{
		if ((g_PDumpCtrl.ui32CurrentFrame >= g_PDumpCtrl.sCaptureRange.ui32Start) &&
			(g_PDumpCtrl.ui32CurrentFrame <= g_PDumpCtrl.sCaptureRange.ui32End))
		{
			if (((g_PDumpCtrl.ui32CurrentFrame - g_PDumpCtrl.sCaptureRange.ui32Start) % g_PDumpCtrl.sCaptureRange.ui32Interval) == 0)
			{
				SET_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_RANGE);
				UNSET_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_INTERVAL);
			}
			else
			{
				UNSET_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_RANGE);
				SET_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_INTERVAL);
			}
		}
		else
		{
			UNSET_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_RANGE);
			UNSET_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_INTERVAL);
		}
	}
	else if ((g_PDumpCtrl.ui32DefaultCapMode == PDUMP_CAPMODE_CONTINUOUS) || (g_PDumpCtrl.ui32DefaultCapMode == PDUMP_CAPMODE_BLOCKED))
	{
		SET_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_RANGE);
		UNSET_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_INTERVAL);
	}
	else if (g_PDumpCtrl.ui32DefaultCapMode == PDUMP_CAPMODE_UNSET)
	{
		UNSET_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_RANGE);
		UNSET_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_INTERVAL);
	}
	else
	{
		UNSET_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_RANGE);
		UNSET_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_INTERVAL);
		PVR_DPF((PVR_DBG_ERROR, "PDumpCtrlUpdateCaptureStatus: Unexpected capture mode (%x)", g_PDumpCtrl.ui32DefaultCapMode));
	}

}

static INLINE IMG_UINT32 PDumpCtrlCapModIsBlocked(void)
{
	return (g_PDumpCtrl.ui32DefaultCapMode == PDUMP_CAPMODE_BLOCKED);
}

static INLINE IMG_UINT32 PDumpCtrlMinimalFirstBlock(void)
{
	/* If ui32Start is set to zero, first block length will be set to minimum
	 * (i.e. PDUMP_BLOCKLEN_MIN), else it will be of same length as that of
	 * rest of the blocks (i.e. ui32BlockLength)
	 *
	 * Having shorter first block reduces playback time of final capture.
	 * */

	return (PDumpCtrlCapModIsBlocked() && (g_PDumpCtrl.sCaptureRange.ui32Start == 0));
}

static void PDumpCtrlSetBlock(IMG_UINT32 ui32BlockNum)
{
	g_PDumpCtrl.sBlockCtrl.ui32CurrentBlock = PDumpCtrlCapModIsBlocked()? ui32BlockNum : PDUMP_BLOCKNUM_INVALID;
}

static INLINE IMG_UINT32 PDumpCtrlGetBlock(void)
{
	return PDumpCtrlCapModIsBlocked()? g_PDumpCtrl.sBlockCtrl.ui32CurrentBlock : PDUMP_BLOCKNUM_INVALID;
}

static PVRSRV_ERROR PDumpCtrlForcedStop(void)
{
	/* In block-mode on forced stop request, capture will be stopped after (current_frame + 1)th frame number.
	 * This ensures that DumpAfterRender always be called on last frame before exiting the PDump capturing
	 * */
	g_PDumpCtrl.sCaptureRange.ui32End = g_PDumpCtrl.ui32CurrentFrame + 1;

	return PVRSRV_OK;
}

static INLINE IMG_BOOL PDumpCtrlIsCaptureForceStopped(void)
{
	return (PDumpCtrlCapModIsBlocked() && (g_PDumpCtrl.ui32CurrentFrame > g_PDumpCtrl.sCaptureRange.ui32End));
}

static void PDumpCtrlSetCurrentFrame(IMG_UINT32 ui32Frame)
{
	g_PDumpCtrl.ui32CurrentFrame = ui32Frame;

	PDumpCtrlUpdateCaptureStatus();

	/* Force PDump module to suspend PDumping on forced capture stop */
	if ((PDumpCtrlGetModuleState() != PDUMP_SM_FORCED_SUSPENDED) && PDumpCtrlIsCaptureForceStopped())
	{
		PVR_LOG(("PDump forced capture stop received. Suspend PDumping to force driver reload before next capture."));
		g_PDumpCtrl.eServiceState = PDUMP_SM_FORCED_SUSPENDED;
	}
#if defined(PDUMP_TRACE_STATE)
	PDumpCommonDumpState();
#endif
}

static void PDumpCtrlSetDefaultCaptureParams(IMG_UINT32 ui32Mode, IMG_UINT32 ui32Start, IMG_UINT32 ui32End, IMG_UINT32 ui32Interval)
{
	/* Set the capture range to that supplied by the PDump client tool
	 */
	g_PDumpCtrl.ui32DefaultCapMode = ui32Mode;
	g_PDumpCtrl.sCaptureRange.ui32Start = ui32Start;
	g_PDumpCtrl.sCaptureRange.ui32End = ui32End;
	g_PDumpCtrl.sCaptureRange.ui32Interval = ui32Interval;

	/* Set pdump block mode ctrl variables */
	g_PDumpCtrl.sBlockCtrl.ui32BlockLength = (ui32Mode == PDUMP_CAPMODE_BLOCKED)? ui32Interval : 0; /* ui32Interval is interpreted as block length */
	g_PDumpCtrl.sBlockCtrl.ui32CurrentBlock = PDUMP_BLOCKNUM_INVALID;

	/* Change module state to record capture client connected */
	if (g_PDumpCtrl.ui32DefaultCapMode == PDUMP_CAPMODE_UNSET)
		g_PDumpCtrl.eServiceState = PDUMP_SM_READY;
	else
		g_PDumpCtrl.eServiceState = PDUMP_SM_READY_CLIENT_CONNECTED;

	/* Reset the current frame on reset of the capture range, the helps to
	 * avoid inter-pdump start frame issues when the driver is not reloaded.
	 * No need to call PDumpCtrlUpdateCaptureStatus() direct as the set
	 * current frame call will.
	 */
	PDumpCtrlSetCurrentFrame(0);

}

static IMG_UINT32 PDumpCtrlGetCurrentFrame(void)
{
	return g_PDumpCtrl.ui32CurrentFrame;
}

static INLINE IMG_BOOL PDumpCtrlCaptureOn(void)
{
	return ((g_PDumpCtrl.eServiceState == PDUMP_SM_READY_CLIENT_CONNECTED) &&
			CHECK_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_RANGE)) ? IMG_TRUE : IMG_FALSE;
}

static INLINE IMG_BOOL PDumpCtrlCaptureInInterval(void)
{
	return ((g_PDumpCtrl.eServiceState == PDUMP_SM_READY_CLIENT_CONNECTED) &&
			CHECK_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_INTERVAL)) ? IMG_TRUE : IMG_FALSE;
}

static INLINE IMG_BOOL PDumpCtrlCaptureRangePast(void)
{
	return (g_PDumpCtrl.ui32CurrentFrame > g_PDumpCtrl.sCaptureRange.ui32End);
}

static IMG_BOOL PDumpCtrlIsLastCaptureFrame(void)
{
	if (g_PDumpCtrl.ui32DefaultCapMode == PDUMP_CAPMODE_FRAMED)
	{
		/* Is the next capture frame within the range end limit? */
		if ((g_PDumpCtrl.ui32CurrentFrame + g_PDumpCtrl.sCaptureRange.ui32Interval) > g_PDumpCtrl.sCaptureRange.ui32End)
		{
			return IMG_TRUE;
		}
	}
	else if (g_PDumpCtrl.ui32DefaultCapMode == PDUMP_CAPMODE_BLOCKED)
	{
		if (g_PDumpCtrl.ui32CurrentFrame == g_PDumpCtrl.sCaptureRange.ui32End)
		{
			return IMG_TRUE;
		}
	}
	/* Return false for all other conditions: framed mode but not last frame,
	 * continuous mode; unset mode.
	 */
	return IMG_FALSE;
}

static INLINE IMG_BOOL PDumpCtrlInitPhaseComplete(void)
{
	return !CHECK_PDUMP_CONTROL_FLAG(FLAG_IS_DRIVER_IN_INIT_PHASE);
}

static INLINE void PDumpCtrlSetInitPhaseComplete(IMG_BOOL bIsComplete)
{
	PDUMP_HERE_VAR;

	if (bIsComplete)
	{
		UNSET_PDUMP_CONTROL_FLAG(FLAG_IS_DRIVER_IN_INIT_PHASE);
		PDUMP_HEREA(102);
	}
	else
	{
		SET_PDUMP_CONTROL_FLAG(FLAG_IS_DRIVER_IN_INIT_PHASE);
		PDUMP_HEREA(103);
	}
}

static INLINE void PDumpCtrlPowerTransitionStart(void)
{
	g_PDumpCtrl.InPowerTransitionPID = OSGetCurrentProcessID();
}

static INLINE void PDumpCtrlPowerTransitionEnd(void)
{
	g_PDumpCtrl.InPowerTransitionPID = 0;
}

static INLINE IMG_PID PDumpCtrlInPowerTransitionPID(void)
{
	return g_PDumpCtrl.InPowerTransitionPID;
}

static INLINE IMG_BOOL PDumpCtrlInPowerTransition(void)
{
	IMG_BOOL bPDumpInPowerTransition = IMG_FALSE;
	if (PDumpCtrlInPowerTransitionPID())
	{
		bPDumpInPowerTransition = IMG_TRUE;
	}
	return bPDumpInPowerTransition;
}

static PVRSRV_ERROR PDumpCtrlGetState(IMG_UINT64 *ui64State)
{
	PDUMP_SM eState;

	*ui64State = 0;

	if (PDumpCtrlCaptureOn())
	{
		*ui64State |= PDUMP_STATE_CAPTURE_FRAME;
	}

	if (PDumpCtrlCaptureInInterval())
	{
		*ui64State |= PDUMP_STATE_CAPTURE_IN_INTERVAL;
	}

	eState = PDumpCtrlGetModuleState();

	if (eState == PDUMP_SM_READY_CLIENT_CONNECTED)
	{
		*ui64State |= PDUMP_STATE_CONNECTED;
	}

	if (eState == PDUMP_SM_ERROR_SUSPENDED)
	{
		*ui64State |= PDUMP_STATE_SUSPENDED;
	}

	return PVRSRV_OK;
}

/******************************************************************************
	End of PDumpCtrl*** functions
******************************************************************************/

/*
	Wrapper functions which need to be exposed in pdump_km.h for use in other
	pdump_*** modules safely. These functions call the specific PDumpCtrl layer
	function after acquiring the PDUMP_CTRL_STATE lock, hence making the calls
	from other modules hassle free by avoiding the acquire/release CtrlLock
	calls.
*/

static INLINE void PDumpModuleTransitionState(PDUMP_SM eNewState)
{
	PDumpCtrlLockAcquire();
	g_PDumpCtrl.eServiceState = eNewState;
	PDumpCtrlLockRelease();
}

void PDumpPowerTransitionStart(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	if (PDumpIsDevicePermitted(psDeviceNode))
	{
		PDumpCtrlLockAcquire();
		PDumpCtrlPowerTransitionStart();
		PDumpCtrlLockRelease();
	}
}

void PDumpPowerTransitionEnd(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	if (PDumpIsDevicePermitted(psDeviceNode))
	{
		PDumpCtrlLockAcquire();
		PDumpCtrlPowerTransitionEnd();
		PDumpCtrlLockRelease();
	}
}

static PVRSRV_ERROR PDumpGetCurrentBlockKM(IMG_UINT32 *pui32BlockNum)
{
	PDumpCtrlLockAcquire();
	*pui32BlockNum = PDumpCtrlGetBlock();
	PDumpCtrlLockRelease();

	return PVRSRV_OK;
}

static IMG_BOOL PDumpIsClientConnected(void)
{
	IMG_BOOL bPDumpClientConnected;

	PDumpCtrlLockAcquire();
	bPDumpClientConnected = (PDumpCtrlGetModuleState() == PDUMP_SM_READY_CLIENT_CONNECTED);
	PDumpCtrlLockRelease();

	return bPDumpClientConnected;
}

/* Prototype write allowed for exposure in PDumpCheckFlagsWrite */
static IMG_BOOL PDumpWriteAllowed(PVRSRV_DEVICE_NODE *psDeviceNode,
                                  IMG_UINT32 ui32Flags, IMG_UINT32* ui32ExitHere);

IMG_BOOL PDumpCheckFlagsWrite(PVRSRV_DEVICE_NODE *psDeviceNode,
                              IMG_UINT32 ui32Flags)
{
	return PDumpWriteAllowed(psDeviceNode, ui32Flags, NULL);
}

/*****************************************************************************/
/* PDump Common Write Layer just above common Transport Layer                */
/*****************************************************************************/


/*!
 * \name	_PDumpOSGetStreamOffset
 */
static IMG_BOOL _PDumpSetSplitMarker(IMG_HANDLE hStream, IMG_BOOL bRemoveOld)
{
	PVRSRV_ERROR eError;
	/* We have to indicate the reader that we wish to split. Insert an EOS packet in the TL stream */
	eError = TLStreamMarkEOS(hStream, bRemoveOld);

	/* If unsuccessful, return false */
	if (eError != PVRSRV_OK)
	{
		PVR_LOG_ERROR(eError, "TLStreamMarkEOS");

		return IMG_FALSE;
	}

	return IMG_TRUE;
}

IMG_BOOL PDumpIsDevicePermitted(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	if ((void*)psDeviceNode == (void*)PDUMP_MAGIC_COOKIE)
	{
		/* Always permit PDumping if passed 'magic' cookie */
		return IMG_TRUE;
	}

	if (psDeviceNode)
	{
		if ((psDeviceNode->sDevId.ui32InternalID > PVRSRV_MAX_DEVICES) ||
		    ((psPVRSRVData->ui32PDumpBoundDevice < PVRSRV_MAX_DEVICES) &&
		     (psDeviceNode->sDevId.ui32InternalID != psPVRSRVData->ui32PDumpBoundDevice)))
		{
			return IMG_FALSE;
		}
	}
	else
	{
		/* Assert if provided with a NULL psDeviceNode */
		OSDumpStack();
		PVR_ASSERT(psDeviceNode);
		return IMG_FALSE;
	}
	return IMG_TRUE;
}

/*
	Checks in this method were seeded from the original PDumpWriteILock()
	and DBGDrivWriteCM() and have grown since to ensure PDump output
	matches legacy output.
	Note: the order of the checks in this method is important as some
	writes have multiple pdump flags set!
 */
static IMG_BOOL PDumpWriteAllowed(PVRSRV_DEVICE_NODE *psDeviceNode,
                                  IMG_UINT32 ui32Flags, IMG_UINT32* ui32ExitHere)
{
	PDUMP_HERE_VAR;

	/* No writes if for a different device than the PDump-bound device
	 *  NB. psDeviceNode may be NULL if called during initialisation
	 */
	if (!PDumpIsDevicePermitted(psDeviceNode))
	{
		PDUMP_HERE(5);
		goto returnFalse;
	}

	/* PDUMP_FLAGS_CONTINUOUS and PDUMP_FLAGS_PERSISTENT can't come together. */
	PVR_ASSERT(IMG_FALSE == ((ui32Flags & PDUMP_FLAGS_CONTINUOUS) &&
		                     (ui32Flags & PDUMP_FLAGS_PERSISTENT)));

	/* Lock down the PDUMP_CTRL_STATE struct before calling the following
	   PDumpCtrl*** functions. This is to avoid updates to the Control data
	   while we are reading from it */
	PDumpCtrlLockAcquire();

	/* No writes if in framed mode and range pasted */
	if (PDumpCtrlCaptureRangePast())
	{
		PDUMP_HERE(10);
		goto unlockAndReturnFalse;
	}

	/* No writes while PDump is not ready or is suspended */
	if (PDumpReady() != PVRSRV_OK)
	{
		PDUMP_HERE(11);
		goto unlockAndReturnFalse;
	}

	/* Prevent PDumping during a power transition */
	if (PDumpCtrlInPowerTransition())
	{	/* except when it's flagged */
		if (ui32Flags & PDUMP_FLAGS_POWER)
		{
			PDUMP_HERE(20);
			goto unlockAndReturnTrue;
		}
		else if (PDumpCtrlInPowerTransitionPID() == OSGetCurrentProcessID())
		{
			PDUMP_HERE(16);
			goto unlockAndReturnFalse;
		}
	}

	/* Always allow dumping in init phase and when persistent flagged */
	if (ui32Flags & PDUMP_FLAGS_PERSISTENT)
	{
		PDUMP_HERE(12);
		goto unlockAndReturnTrue;
	}
	if (!PDumpCtrlInitPhaseComplete())
	{
		PDUMP_HERE(15);
		goto unlockAndReturnTrue;
	}

	/* The following checks are made when the driver has completed initialisation */
	/* No last/deinit statements allowed when not in initialisation phase */
	else /* init phase over */
	{
		if (ui32Flags & PDUMP_FLAGS_DEINIT)
		{
			PVR_ASSERT(0);
			PDUMP_HERE(17);
			goto unlockAndReturnFalse;
		}
	}

	/* If PDump client connected allow continuous flagged writes */
	if (PDUMP_IS_CONTINUOUS(ui32Flags))
	{
		if (PDumpCtrlGetModuleState() != PDUMP_SM_READY_CLIENT_CONNECTED) /* Is client connected? */
		{
			PDUMP_HERE(13);
			goto unlockAndReturnFalse;
		}
		PDUMP_HERE(14);
		goto unlockAndReturnTrue;
	}

	/* If in a capture interval but a write is still required.
	 * Force write out if FLAGS_INTERVAL has been set and we are in
	 * a capture interval */
	if (ui32Flags & PDUMP_FLAGS_INTERVAL)
	{
		if (PDumpCtrlCaptureInInterval()){
			PDUMP_HERE(21);
			goto unlockAndReturnTrue;
		}
	}

	/*
		If no flags are provided then it is FRAMED output and the frame
		range must be checked matching expected behaviour.
	 */
	if (!PDumpCtrlCaptureOn())
	{
		PDUMP_HERE(18);
		goto unlockAndReturnFalse;
	}

	PDUMP_HERE(19);

unlockAndReturnTrue:
	/* Allow the write to take place */

	PDumpCtrlLockRelease();
	return IMG_TRUE;

unlockAndReturnFalse:
	PDumpCtrlLockRelease();
returnFalse:
	if (ui32ExitHere != NULL)
	{
		*ui32ExitHere = here;
	}
	return IMG_FALSE;
}


/*************************************************************************/ /*!
 @Function		PDumpWriteToBuffer
 @Description	Write the supplied data to the PDump stream buffer and attempt
                to handle any buffer full conditions to ensure all the data
                requested to be written, is.

 @Input			psDeviceNode The device the PDump pertains to
 @Input			psStream	The address of the PDump stream buffer to write to
 @Input			pui8Data    Pointer to the data to be written
 @Input			ui32BCount	Number of bytes to write
 @Input			ui32Flags	PDump statement flags.

 @Return		IMG_UINT32  Actual number of bytes written, may be less than
                            ui32BCount when buffer full condition could not
                            be avoided.
*/ /**************************************************************************/
static IMG_UINT32 PDumpWriteToBuffer(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     PDUMP_STREAM* psStream,
                                     IMG_UINT8 *pui8Data,
                                     IMG_UINT32 ui32BCount,
                                     IMG_UINT32 ui32Flags)
{
	IMG_UINT32	ui32BytesToBeWritten;
	IMG_UINT32	ui32Off = 0;
	IMG_BYTE *pbyDataBuffer;
	IMG_UINT32 ui32BytesAvailable = 0;
	static IMG_UINT32 ui32TotalBytesWritten;
	PVRSRV_ERROR eError;
	IMG_UINT32 uiRetries = 0;

	/* Check PDump stream validity */
	if (psStream->hTL == NULL)
	{
		PVR_DPF((PVR_DBG_WARNING, "PDumpWriteToBuffer: PDump stream '%s' is invalid", psStream->pszName));
		return 0;
	}

	/* This API always called by holding pdump write lock
	 * to ensure atomic pdump write happened and
	 * if not holding pdump lock then assert.
	 */
	PDumpAssertWriteLockHeld();

	/* No need to check size of data to write as this is asserted
	 * higher up in the call stack as 1KB and 16KB for each channel
	 * respectively. */

	while (ui32BCount > 0)
	{
		ui32BytesToBeWritten = MIN ( ui32BCount, psStream->ui32MaxAllowedWriteSize );

		eError = TLStreamReserve2(psStream->hTL, &pbyDataBuffer, ui32BytesToBeWritten, 0, &ui32BytesAvailable, NULL);
		if (eError == PVRSRV_ERROR_STREAM_FULL)
		{
			psStream->ui32BufferFullRetries++;

			/*! Retry write2 only if available bytes is at least 1024 or more. */
			if (ui32BytesAvailable >= 0x400)
			{
				ui32BytesToBeWritten = ui32BytesAvailable;
				PVR_DPF((PVR_DBG_WARNING, "PDumpWriteToBuffer: TL buffer '%s' retrying write2=%u out of %u", psStream->pszName, ui32BytesToBeWritten, ui32BCount));
				eError = TLStreamReserve(psStream->hTL, &pbyDataBuffer, ui32BytesToBeWritten);
				/*! Not expected to get PVRSRV_ERROR_STREAM_FULL error and other error may get */
				PVR_ASSERT(eError != PVRSRV_ERROR_STREAM_FULL);
			}
			else
			{
				uiRetries++;
				PVR_DPF((PVR_DBG_WARNING, "PDumpWriteToBuffer: TL buffer '%s' full, rq=%u, av=%u, retrying write", psStream->pszName, ui32BCount, ui32BytesAvailable));

				/* Check if we are out of retries , if so then print warning */
				if (uiRetries >= MAX_PDUMP_WRITE_RETRIES)
				{
					PVR_DPF((PVR_DBG_ERROR,
					         "PDumpWriteToBuffer: PDump writes blocked to dump %d bytes, %s TLBuffers full for %d seconds, check system",
					         ui32BCount,
					         psStream->pszName,
					         ((200 * uiRetries)/1000)));

					if (uiRetries > psStream->ui32HighestRetriesWatermark)
					{
						psStream->ui32HighestRetriesWatermark = uiRetries;
					}

					psStream->ui32BufferFullAborts++;
					uiRetries = 0;

					/* As uiRetries exceed max write retries that means,
					 * something went wrong in system and thus suspend pdump.
					 */
					PDumpModuleTransitionState(PDUMP_SM_ERROR_SUSPENDED);
					return 0;
				}

				OSSleepms(100);
				continue;
			}
		}

		if (eError == PVRSRV_OK)
		{
			ui32TotalBytesWritten += ui32BytesToBeWritten;

			PVR_ASSERT(pbyDataBuffer != NULL);

			OSDeviceMemCopy((void*)pbyDataBuffer, pui8Data + ui32Off, ui32BytesToBeWritten);

			eError = TLStreamCommit(psStream->hTL, ui32BytesToBeWritten);
			if (PVRSRV_OK != eError)
			{
				return 0;
			}

			if (uiRetries > psStream->ui32HighestRetriesWatermark)
			{
				psStream->ui32HighestRetriesWatermark = uiRetries;
			}

			uiRetries = 0;
			ui32Off += ui32BytesToBeWritten;
			ui32BCount -= ui32BytesToBeWritten;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "PDumpWriteToBuffer: TLStreamReserve2(%s) unrecoverable error %s", psStream->pszName, PVRSRVGETERRORSTRING(eError)));
			/* Fatal -suspend PDump to prevent flooding kernel log buffer */
			PVR_LOG(("Unrecoverable error, PDump suspended!"));

			PDumpModuleTransitionState(PDUMP_SM_ERROR_SUSPENDED);
			return 0;
		}

		/*
		   if the capture range is unset
		   (which is detected via PDumpWriteAllowed())
		*/

		if (!PDumpWriteAllowed(psDeviceNode, ui32Flags, NULL))
		{
			psStream->ui32BufferFullAborts++;
			break;
		}
	}

	return ui32Off;
}

/*************************************************************************/ /*!
 @Function      PDumpWriteToChannel
 @Description   Write the supplied data to the PDump channel specified obeying
                flags to write to the necessary channel buffers.

 @Input         psDeviceNode The device the PDump pertains to
 @Input         psChannel   Address of the script or parameter channel object
 @Input/Output  psWOff      Address of the channel write offsets object to
                            update on successful writing
 @Input         pui8Data    Pointer to the data to be written
 @Input         ui32Size    Number of bytes to write
 @Input         ui32Flags   PDump statement flags, they may be clear (no flags)
                            or persistent flagged and they determine how the
                            which implies framed data, continuous flagged, data
                            is output. On the first test app run after driver
                            load, the Display Controller dumps a resource that
                            is persistent and this needs writing to both the
                            init (persistent) and main (continuous) channel
                            buffers to ensure the data is dumped in subsequent
                            test runs without reloading the driver.
                            In subsequent runs the PDump client 'freezes' the
                            init buffer so that only one dump of persistent
                            data for the "extended init phase" is captured to
                            the init buffer.
 @Return        IMG_BOOL    True when the data has been consumed, false otherwise
*/ /**************************************************************************/
static IMG_BOOL PDumpWriteToChannel(PVRSRV_DEVICE_NODE *psDeviceNode,
                                    PDUMP_CHANNEL* psChannel,
                                    PDUMP_CHANNEL_WOFFSETS* psWOff,
                                    IMG_UINT8* pui8Data,
                                    IMG_UINT32 ui32Size,
                                    IMG_UINT32 ui32Flags)
{
	IMG_UINT32 ui32BytesWritten = 0;
	PDUMP_HERE_VAR;

	PDUMP_HERE(210);

	/* At this point, PDumpWriteAllowed() has returned TRUE (or called from
	 * PDumpParameterChannelZeroedPageBlock() during driver init) we know the
	 * write must proceed because:
	 * - pdump is not suspended and
	 * - there is not an ongoing power transition or POWER override flag is set or
	 * - in driver init phase with ANY flag set or
	 * - post init with the pdump client connected and
	 * -   - PERSIST flag is present, xor
	 * -   - the CONTINUOUS flag is present, xor
	 * -   - in capture frame range
	 */
	PDumpAssertWriteLockHeld();

	/* Dump data to deinit buffer when flagged as deinit */
	if (ui32Flags & PDUMP_FLAGS_DEINIT)
	{
		PDUMP_HERE(211);
		ui32BytesWritten = PDumpWriteToBuffer(psDeviceNode,
		                                      &psChannel->sDeinitStream,
		                                      pui8Data, ui32Size, ui32Flags);
		if (ui32BytesWritten != ui32Size)
		{
			PVR_DPF((PVR_DBG_ERROR, "PDumpWriteToChannel: DEINIT Written length (%d) does not match data length (%d), PDump incomplete!", ui32BytesWritten, ui32Size));
			PDUMP_HERE(212);
			return IMG_FALSE;
		}

		if (psWOff)
		{
			psWOff->ui32Deinit += ui32Size;
		}

	}
	else
	{
		IMG_BOOL bDumpedToInitAlready = IMG_FALSE;
		IMG_BOOL bMainStreamData = IMG_FALSE;
		PDUMP_STREAM*  psStream = NULL;
		IMG_UINT32* pui32Offset = NULL;

		/* Always append persistent data to init phase so it's available on
		 * subsequent app runs, but also to the main stream if client connected */
		if (ui32Flags & PDUMP_FLAGS_PERSISTENT)
		{
			PDUMP_HERE(213);
			ui32BytesWritten = PDumpWriteToBuffer(psDeviceNode,
			                                      &psChannel->sInitStream,
			                                      pui8Data, ui32Size, ui32Flags);
			if (ui32BytesWritten != ui32Size)
			{
				PVR_DPF((PVR_DBG_ERROR, "PDumpWriteToChannel: PERSIST Written length (%d) does not match data length (%d), PDump incomplete!", ui32BytesWritten, ui32Size));
				PDUMP_HERE(214);
				return IMG_FALSE;
			}

			bDumpedToInitAlready = IMG_TRUE;
			if (psWOff)
			{
				psWOff->ui32Init += ui32Size;
			}

			/* Don't write continuous data if client not connected */
			if (PDumpCtrlGetModuleState() != PDUMP_SM_READY_CLIENT_CONNECTED)
			{
				return IMG_TRUE;
			}
		}

		/* Prepare to write the data to the main stream for
		 * persistent, continuous or framed data. Override and use init
		 * stream if driver still in init phase and we have not written
		 * to it yet.*/
		PDumpCtrlLockAcquire();
		if (!PDumpCtrlInitPhaseComplete() && !bDumpedToInitAlready)
		{
			PDUMP_HERE(215);
			psStream = &psChannel->sInitStream;
			if (psWOff)
			{
				pui32Offset = &psWOff->ui32Init;
			}
		}
		else
		{
			PDUMP_HERE(216);
			psStream = &psChannel->sMainStream;
			if (psWOff)
			{
				pui32Offset = &psWOff->ui32Main;
			}
			bMainStreamData = IMG_TRUE;

		}
		PDumpCtrlLockRelease();

		if (PDumpCtrlCapModIsBlocked() && bMainStreamData && !psWOff)
		{
			/* if PDUMP_FLAGS_BLKDATA flag is set in Blocked mode, Make copy of Main script stream data to Block script stream as well */
			if (ui32Flags & PDUMP_FLAGS_BLKDATA)
			{
				PDUMP_HERE(217);
				ui32BytesWritten = PDumpWriteToBuffer(psDeviceNode,
				                                      &psChannel->sBlockStream,
				                                      pui8Data, ui32Size, ui32Flags);
				if (ui32BytesWritten != ui32Size)
				{
					PVR_DPF((PVR_DBG_ERROR, "PDumpWriteToChannel: BLOCK Written length (%d) does not match data length (%d), PDump incomplete!", ui32BytesWritten, ui32Size));
					PDUMP_HERE(218);
					return IMG_FALSE;
				}
			}
		}

		/* Write the data to the stream */
		ui32BytesWritten = PDumpWriteToBuffer(psDeviceNode,
		                                      psStream, pui8Data,
		                                      ui32Size, ui32Flags);
		if (ui32BytesWritten != ui32Size)
		{
			PVR_DPF((PVR_DBG_ERROR, "PDumpWriteToChannel: MAIN Written length (%d) does not match data length (%d), PDump incomplete!", ui32BytesWritten, ui32Size));
			PDUMP_HERE(219);
			return IMG_FALSE;
		}

		if (pui32Offset)
		{
			*pui32Offset += ui32BytesWritten;
		}
	}

	return IMG_TRUE;
}

#if defined(PDUMP_DEBUG_OUTFILES)

static IMG_UINT32 _GenerateChecksum(void *pvData, size_t uiSize)
{
	IMG_UINT32 ui32Sum = 0;
	IMG_UINT32 *pui32Data = pvData;
	IMG_UINT8 *pui8Data = pvData;
	IMG_UINT32 i;
	IMG_UINT32 ui32LeftOver;

	for (i = 0; i < uiSize / sizeof(IMG_UINT32); i++)
	{
		ui32Sum += pui32Data[i];
	}

	ui32LeftOver = uiSize % sizeof(IMG_UINT32);

	while (ui32LeftOver)
	{
		ui32Sum += pui8Data[uiSize - ui32LeftOver];
		ui32LeftOver--;
	}

	return ui32Sum;
}

#endif

PVRSRV_ERROR PDumpWriteParameter(PVRSRV_DEVICE_NODE *psDeviceNode,
                                 IMG_UINT8 *pui8Data,
                                 IMG_UINT32 ui32Size,
                                 IMG_UINT32 ui32Flags,
                                 IMG_UINT32* pui32FileOffset,
                                 IMG_CHAR* aszFilenameStr)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_BOOL bPDumpCtrlInitPhaseComplete = IMG_FALSE;
	IMG_UINT32 here = 0;
	IMG_INT32 iCount;

	PDumpAssertWriteLockHeld();

	PVR_ASSERT(pui8Data && (ui32Size!=0));
	PVR_ASSERT(pui32FileOffset && aszFilenameStr);

	PDUMP_HERE(1);

	/* Check if write can proceed? */
	if (!PDumpWriteAllowed(psDeviceNode, ui32Flags, &here))
	{
		/* Abort write for the above reason but indicate what happened to
		 * caller to avoid disrupting the driver, caller should treat it as OK
		 * but skip any related PDump writes to the script file. */
		return PVRSRV_ERROR_PDUMP_NOT_ALLOWED;
	}

	PDUMP_HERE(2);

	PDumpCtrlLockAcquire();
	bPDumpCtrlInitPhaseComplete = PDumpCtrlInitPhaseComplete();
	PDumpCtrlLockRelease();

	if (!bPDumpCtrlInitPhaseComplete || (ui32Flags & PDUMP_FLAGS_PERSISTENT))
	{
		PDUMP_HERE(3);

		/* Init phase stream not expected to get above the file size max */
		PVR_ASSERT(g_PDumpParameters.sWOff.ui32Init < g_PDumpParameters.ui32MaxFileSize);

		/* Return the file write offset at which the parameter data was dumped */
		*pui32FileOffset = g_PDumpParameters.sWOff.ui32Init;
	}
	else
	{
		PDUMP_HERE(4);

		/* Do we need to signal the PDump client that a split is required? */
		if (g_PDumpParameters.sWOff.ui32Main + ui32Size > g_PDumpParameters.ui32MaxFileSize)
		{
			PDUMP_HERE(5);
			_PDumpSetSplitMarker(g_PDumpParameters.sCh.sMainStream.hTL, IMG_FALSE);
			g_PDumpParameters.ui32FileIdx++;
			g_PDumpParameters.sWOff.ui32Main = 0;
		}

		/* Return the file write offset at which the parameter data was dumped */
		*pui32FileOffset = g_PDumpParameters.sWOff.ui32Main;
	}

	/* Create the parameter file name, based on index, to be used in the script */
	if (g_PDumpParameters.ui32FileIdx == 0)
	{
		iCount = OSSNPrintf(aszFilenameStr, PDUMP_PARAM_MAX_FILE_NAME, PDUMP_PARAM_0_FILE_NAME);
	}
	else
	{
		PDUMP_HERE(6);
		iCount = OSSNPrintf(aszFilenameStr, PDUMP_PARAM_MAX_FILE_NAME, PDUMP_PARAM_N_FILE_NAME, g_PDumpParameters.ui32FileIdx);
	}

	PVR_LOG_GOTO_IF_FALSE(((iCount != -1) && (iCount < PDUMP_PARAM_MAX_FILE_NAME)), "OSSNPrintf", errExit);

	/* Write the parameter data to the parameter channel */
	eError = PVRSRV_ERROR_PDUMP_BUFFER_FULL;
	if (!PDumpWriteToChannel(psDeviceNode, &g_PDumpParameters.sCh,
	                         &g_PDumpParameters.sWOff, pui8Data,
	                         ui32Size, ui32Flags))
	{
		PDUMP_HERE(7);
		PVR_LOG_GOTO_IF_ERROR(eError, "PDumpWrite", errExit);
	}
#if defined(PDUMP_DEBUG_OUTFILES)
	else
	{
		IMG_UINT32 ui32Checksum;
		PDUMP_GET_SCRIPT_STRING();

		ui32Checksum = _GenerateChecksum(pui8Data, ui32Size);

		/* CHK CHKSUM SIZE PRMOFFSET PRMFILE */
		eError = PDumpSNPrintf(hScript, ui32MaxLen, "-- CHK 0x%08X 0x%08X 0x%08X %s",
									ui32Checksum,
									ui32Size,
									*pui32FileOffset,
									aszFilenameStr);
		PVR_GOTO_IF_ERROR(eError, errExit);

		PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
		PDUMP_RELEASE_SCRIPT_STRING();
	}
#endif

	return PVRSRV_OK;

errExit:
	return eError;
}


IMG_BOOL PDumpWriteScript(PVRSRV_DEVICE_NODE *psDeviceNode,
                          IMG_HANDLE hString, IMG_UINT32 ui32Flags)
{
	PDUMP_HERE_VAR;

	PVR_ASSERT(hString);

	PDumpAssertWriteLockHeld();

	PDUMP_HERE(201);

#if defined(DEBUG)
	/* Since buffer sizes and buffer writing/reading are a balancing act to
	 * avoid buffer full errors, check here our assumption on the maximum write size.
	 */
	{
		IMG_UINT32 ui32Size = (IMG_UINT32) OSStringLength((const IMG_CHAR *)hString);
		if (ui32Size > 0x400) // 1KB
		{
			PVR_DPF((PVR_DBG_ERROR, "PDUMP large script write %u bytes", ui32Size));
			OSDumpStack();
		}
	}
#endif

	if (!PDumpWriteAllowed(psDeviceNode, ui32Flags, NULL))
	{
		/* Abort write for the above reasons but indicated it was OK to
		 * caller to avoid disrupting the driver */
		return IMG_TRUE;
	}

	if (PDumpCtrlCapModIsBlocked())
	{
		if (ui32Flags & PDUMP_FLAGS_FORCESPLIT)
		{
			IMG_UINT32 ui32CurrentBlock;

			PDumpGetCurrentBlockKM(&ui32CurrentBlock);
			/* Keep Main stream script output files belongs to first and last block only */
			if (ui32CurrentBlock == 1)
			{
				/* To keep first(0th) block, do not remove old script file while
				 * splitting to second(1st) block (i.e. bRemoveOld=IMG_FALSE).
				 * */
				_PDumpSetSplitMarker(g_PDumpScript.sCh.sMainStream.hTL, IMG_FALSE);
			}
			else
			{
				/* Previous block's Main script output file will be removed
				 * before splitting to next
				 * */
				_PDumpSetSplitMarker(g_PDumpScript.sCh.sMainStream.hTL, IMG_TRUE);
			}

			/* Split Block stream output file
			 *
			 * We are keeping block script output files from all PDump blocks.
			 * */
			_PDumpSetSplitMarker(g_PDumpScript.sCh.sBlockStream.hTL, IMG_FALSE);
			g_PDumpScript.ui32FileIdx++;
		}
	}

	return PDumpWriteToChannel(psDeviceNode, &g_PDumpScript.sCh, NULL,
	                          (IMG_UINT8*) hString,
	                          (IMG_UINT32) OSStringLength((IMG_CHAR*) hString),
	                          ui32Flags);
}


/*****************************************************************************/


struct _PDUMP_CONNECTION_DATA_ {
	ATOMIC_T                  sRefCount;
	POS_LOCK                  hLock;                       /*!< Protects access to sListHead. */
	DLLIST_NODE               sListHead;
	IMG_UINT32                ui32LastSetFrameNumber;
	PDUMP_TRANSITION_EVENT    eLastEvent;                  /*!< Last processed transition event */
	PDUMP_TRANSITION_EVENT    eFailedEvent;                /*!< Failed transition event to retry */
	PFN_PDUMP_SYNCBLOCKS      pfnPDumpSyncBlocks;          /*!< Callback to PDump sync blocks */
	void                      *hSyncPrivData;              /*!< Sync private data */
};

static PDUMP_CONNECTION_DATA * _PDumpConnectionAcquire(PDUMP_CONNECTION_DATA *psPDumpConnectionData)
{
	IMG_INT iRefCount = OSAtomicIncrement(&psPDumpConnectionData->sRefCount);

	PDUMP_REFCOUNT_PRINT("%s: PDump connection %p, refcount = %d", __func__,
	                     psPDumpConnectionData, iRefCount);
	PVR_UNREFERENCED_PARAMETER(iRefCount);

	return psPDumpConnectionData;
}

static void _PDumpConnectionRelease(PDUMP_CONNECTION_DATA *psPDumpConnectionData)
{
	IMG_INT iRefCount = OSAtomicDecrement(&psPDumpConnectionData->sRefCount);
	if (iRefCount == 0)
	{
		OSLockDestroy(psPDumpConnectionData->hLock);
		PVR_ASSERT(dllist_is_empty(&psPDumpConnectionData->sListHead));
		OSFreeMem(psPDumpConnectionData);
	}

	PDUMP_REFCOUNT_PRINT("%s: PDump connection %p, refcount = %d", __func__,
	                     psPDumpConnectionData, iRefCount);
}

/******************************************************************************
 * Function Name  : PDumpInitStreams
 * Outputs        : None
 * Returns        :
 * Description    : Create the PDump streams
******************************************************************************/
static PVRSRV_ERROR PDumpInitStreams(PDUMP_CHANNEL* psParam, PDUMP_CHANNEL* psScript)
{

	PVRSRV_ERROR   eError;
	TL_STREAM_INFO sTLStreamInfo;

	/* TL - Create the streams */

	/**************************** Parameter stream ***************************/

	/* Parameter - Init */
	eError = TLStreamCreate(&psParam->sInitStream.hTL,
				psParam->sInitStream.pszName, psParam->sInitStream.ui32BufferSize,
				TL_OPMODE_DROP_NEWER | TL_FLAG_PERMANENT_NO_WRAP,
				NULL, NULL,
				NULL, NULL);
	PVR_LOG_GOTO_IF_ERROR(eError, "TLStreamCreate ParamInit", end);

	TLStreamInfo(psParam->sInitStream.hTL, &sTLStreamInfo);
	psParam->sInitStream.ui32MaxAllowedWriteSize = sTLStreamInfo.maxTLpacketSize;

	/* Parameter - Main */
	eError = TLStreamCreate(&psParam->sMainStream.hTL,
				psParam->sMainStream.pszName, psParam->sMainStream.ui32BufferSize,
				TL_OPMODE_DROP_NEWER ,
				NULL, NULL,
				NULL, NULL);
	PVR_LOG_GOTO_IF_ERROR(eError, "TLStreamCreate ParamMain", param_main_failed);

	TLStreamInfo(psParam->sMainStream.hTL, &sTLStreamInfo);
	psParam->sMainStream.ui32MaxAllowedWriteSize = sTLStreamInfo.maxTLpacketSize;

	/* Parameter - Deinit */
	eError = TLStreamCreate(&psParam->sDeinitStream.hTL,
				psParam->sDeinitStream.pszName,	psParam->sDeinitStream.ui32BufferSize,
				TL_OPMODE_DROP_NEWER | TL_FLAG_PERMANENT_NO_WRAP,
				NULL, NULL,
				NULL, NULL);
	PVR_LOG_GOTO_IF_ERROR(eError, "TLStreamCreate ParamDeinit", param_deinit_failed);

	TLStreamInfo(psParam->sDeinitStream.hTL, &sTLStreamInfo);
	psParam->sDeinitStream.ui32MaxAllowedWriteSize = sTLStreamInfo.maxTLpacketSize;

	/* Parameter - Block */
	/* As in current implementation Block script stream is just a filtered
	 * Main script stream using PDUMP_FLAGS_BLKDATA flag, no separate
	 * Parameter stream is needed. Block script will be referring to the
	 * same Parameters as that of Main script stream.
	 */

	/***************************** Script streams ****************************/

	/* Script - Init */
	eError = TLStreamCreate(&psScript->sInitStream.hTL,
				psScript->sInitStream.pszName, psScript->sInitStream.ui32BufferSize,
				TL_OPMODE_DROP_NEWER | TL_FLAG_PERMANENT_NO_WRAP,
				NULL, NULL,
				NULL, NULL);
	PVR_LOG_GOTO_IF_ERROR(eError, "TLStreamCreate ScriptInit", script_init_failed);

	TLStreamInfo(psScript->sInitStream.hTL, &sTLStreamInfo);
	psScript->sInitStream.ui32MaxAllowedWriteSize = sTLStreamInfo.maxTLpacketSize;

	/* Script - Main */
	eError = TLStreamCreate(&psScript->sMainStream.hTL,
				psScript->sMainStream.pszName, psScript->sMainStream.ui32BufferSize,
				TL_OPMODE_DROP_NEWER,
				NULL, NULL,
				NULL, NULL);
	PVR_LOG_GOTO_IF_ERROR(eError, "TLStreamCreate ScriptMain", script_main_failed);

	TLStreamInfo(psScript->sMainStream.hTL, &sTLStreamInfo);
	psScript->sMainStream.ui32MaxAllowedWriteSize = sTLStreamInfo.maxTLpacketSize;

	/* Script - Deinit */
	eError = TLStreamCreate(&psScript->sDeinitStream.hTL,
				psScript->sDeinitStream.pszName, psScript->sDeinitStream.ui32BufferSize,
				TL_OPMODE_DROP_NEWER | TL_FLAG_PERMANENT_NO_WRAP,
				NULL, NULL,
				NULL, NULL);
	PVR_LOG_GOTO_IF_ERROR(eError, "TLStreamCreate ScriptDeinit", script_deinit_failed);

	TLStreamInfo(psScript->sDeinitStream.hTL, &sTLStreamInfo);
	psScript->sDeinitStream.ui32MaxAllowedWriteSize = sTLStreamInfo.maxTLpacketSize;

	/* Script - Block */
	eError = TLStreamCreate(&psScript->sBlockStream.hTL,
				psScript->sBlockStream.pszName, psScript->sBlockStream.ui32BufferSize,
				TL_OPMODE_DROP_NEWER,
				NULL, NULL,
				NULL, NULL);
	PVR_LOG_GOTO_IF_ERROR(eError, "TLStreamCreate ScriptBlock", script_block_failed);

	TLStreamInfo(psScript->sBlockStream.hTL, &sTLStreamInfo);
	psScript->sBlockStream.ui32MaxAllowedWriteSize = sTLStreamInfo.maxTLpacketSize;

	return PVRSRV_OK;

script_block_failed:
	TLStreamClose(psScript->sDeinitStream.hTL);

script_deinit_failed:
	TLStreamClose(psScript->sMainStream.hTL);

script_main_failed:
	TLStreamClose(psScript->sInitStream.hTL);

script_init_failed:
	TLStreamClose(psParam->sDeinitStream.hTL);

param_deinit_failed:
	TLStreamClose(psParam->sMainStream.hTL);

param_main_failed:
	TLStreamClose(psParam->sInitStream.hTL);

end:
	return eError;
}
/******************************************************************************
 * Function Name  : PDumpDeInitStreams
 * Inputs         : psParam, psScript
 * Outputs        : None
 * Returns        : None
 * Description    : Deinitialises the PDump streams
******************************************************************************/
static void PDumpDeInitStreams(PDUMP_CHANNEL* psParam, PDUMP_CHANNEL* psScript)
{
	/* Script streams */
	TLStreamClose(psScript->sDeinitStream.hTL);
	TLStreamClose(psScript->sMainStream.hTL);
	TLStreamClose(psScript->sInitStream.hTL);
	TLStreamClose(psScript->sBlockStream.hTL);

	/* Parameter streams */
	TLStreamClose(psParam->sDeinitStream.hTL);
	TLStreamClose(psParam->sMainStream.hTL);
	TLStreamClose(psParam->sInitStream.hTL);

}

/******************************************************************************
 * Function Name  : PDumpParameterChannelZeroedPageBlock
 * Inputs         : psDeviceNode
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Set up the zero page block in the parameter stream
******************************************************************************/
static PVRSRV_ERROR PDumpParameterChannelZeroedPageBlock(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	IMG_UINT8 aui8Zero[32] = { 0 };
	size_t uiBytesToWrite;
	PVRSRV_ERROR eError;
	void *pvAppHintState = NULL;
	IMG_UINT32 ui32AppHintDefault = PVRSRV_APPHINT_GENERALNON4KHEAPPAGESIZE;
	IMG_UINT32 ui32GeneralNon4KHeapPageSize;

	OSCreateKMAppHintState(&pvAppHintState);
	OSGetKMAppHintUINT32(APPHINT_NO_DEVICE, pvAppHintState, GeneralNon4KHeapPageSize,
			&ui32AppHintDefault, &ui32GeneralNon4KHeapPageSize);
	OSFreeKMAppHintState(pvAppHintState);

	/* ZeroPageSize can't be smaller than page size */
	g_PDumpParameters.uiZeroPageSize = MAX(ui32GeneralNon4KHeapPageSize, OSGetPageSize());

	/* ensure the zero page size of a multiple of the zero source on the stack */
	PVR_ASSERT(g_PDumpParameters.uiZeroPageSize % sizeof(aui8Zero) == 0);

	/* the first write gets the parameter file name and stream offset,
	 * then subsequent writes do not need to know this as the data is
	 * contiguous in the stream
	 */
	PDUMP_LOCK(0);
	eError = PDumpWriteParameter(psDeviceNode, aui8Zero,
							sizeof(aui8Zero),
							0,
							&g_PDumpParameters.uiZeroPageOffset,
							g_PDumpParameters.szZeroPageFilename);

	/* Also treat PVRSRV_ERROR_PDUMP_NOT_ALLOWED as an error in this case
	 * as it should never happen since all writes during driver Init are
	 * allowed.
	*/
	PVR_GOTO_IF_ERROR(eError, err_write);

	uiBytesToWrite = g_PDumpParameters.uiZeroPageSize - sizeof(aui8Zero);

	while (uiBytesToWrite)
	{
		IMG_BOOL bOK;

		bOK = PDumpWriteToChannel(psDeviceNode,
								  &g_PDumpParameters.sCh,
								  &g_PDumpParameters.sWOff,
								  aui8Zero,
								  sizeof(aui8Zero), 0);

		if (!bOK)
		{
			PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_PDUMP_BUFFER_FULL, err_write);
		}

		uiBytesToWrite -= sizeof(aui8Zero);
	}

err_write:
	PDUMP_UNLOCK(0);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to initialise parameter stream zero block"));
	}

	return eError;
}

/******************************************************************************
 * Function Name  : PDumpGetParameterZeroPageInfo
 * Inputs         : None
 * Outputs        : puiZeroPageOffset: set to the offset of the zero page
 *                : puiZeroPageSize: set to the size of the zero page
 *                : ppszZeroPageFilename: set to a pointer to the PRM file name
 *                :                       containing the zero page
 * Returns        : None
 * Description    : Get information about the zero page
******************************************************************************/
void PDumpGetParameterZeroPageInfo(PDUMP_FILEOFFSET_T *puiZeroPageOffset,
					size_t *puiZeroPageSize,
					const IMG_CHAR **ppszZeroPageFilename)
{
		*puiZeroPageOffset = g_PDumpParameters.uiZeroPageOffset;
		*puiZeroPageSize = g_PDumpParameters.uiZeroPageSize;
		*ppszZeroPageFilename = g_PDumpParameters.szZeroPageFilename;
}


PVRSRV_ERROR PDumpInitCommon(void)
{
	PVRSRV_ERROR eError;
	PDUMP_HERE_VAR;

	PDUMP_HEREA(2010);

	/* Initialised with default initial value */
	OSAtomicWrite(&g_sConnectionCount, 0);
#if defined(PDUMP_DEBUG_OUTFILES)
	OSAtomicWrite(&g_sEveryLineCounter, 1);
#endif

	eError = OSLockCreate(&g_hPDumpWriteLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", errRet);

	/* Initialise PDump control module in common layer, also sets
	 * state to PDUMP_SM_INITIALISING.
	 */
	eError = PDumpCtrlInit();
	PVR_LOG_GOTO_IF_ERROR(eError, "PDumpCtrlInit", errRetLock);

	/* Call environment specific PDump initialisation Part 2*/
	eError = PDumpInitStreams(&g_PDumpParameters.sCh, &g_PDumpScript.sCh);
	PVR_LOG_GOTO_IF_ERROR(eError, "PDumpInitStreams", errRetCtrl);

	/* PDump now ready for write calls */
	PDumpModuleTransitionState(PDUMP_SM_READY);

	PDUMP_HEREA(2011);

	/* Test PDump initialised and ready by logging driver details */
	eError = PDumpCommentWithFlags((PVRSRV_DEVICE_NODE*)PDUMP_MAGIC_COOKIE,
	                               PDUMP_FLAGS_CONTINUOUS,
	                               "Driver Product Version: %s - %s (%s)",
	                               PVRVERSION_STRING, PVR_BUILD_DIR, PVR_BUILD_TYPE);
	PVR_LOG_GOTO_IF_ERROR(eError, "PDumpCommentWithFlags", errRetState);

	eError = PDumpCommentWithFlags((PVRSRV_DEVICE_NODE*)PDUMP_MAGIC_COOKIE,
	                               PDUMP_FLAGS_CONTINUOUS,
	                               "Start of Init Phase");
	PVR_LOG_GOTO_IF_ERROR(eError, "PDumpCommentWithFlags", errRetState);

	eError = PDumpParameterChannelZeroedPageBlock((PVRSRV_DEVICE_NODE*)PDUMP_MAGIC_COOKIE);
	PVR_LOG_GOTO_IF_ERROR(eError, "PDumpParameterChannelZeroedPageBlock", errRetState);

	PDUMP_HEREA(2012);
ret:
	return eError;


errRetState:
	PDumpModuleTransitionState(PDUMP_SM_UNINITIALISED);
	PDumpDeInitStreams(&g_PDumpParameters.sCh, &g_PDumpScript.sCh);
errRetCtrl:
	PDumpCtrlDeInit();
errRetLock:
	OSLockDestroy(g_hPDumpWriteLock);
	PDUMP_HEREA(2013);
errRet:
	goto ret;
}
void PDumpDeInitCommon(void)
{
	PDUMP_HERE_VAR;

	PDUMP_HEREA(2020);

	/* Suspend PDump as we want PDumpWriteAllowed to deliberately fail during PDump deinit */
	PDumpModuleTransitionState(PDUMP_SM_DEINITIALISED);

	/*Call environment specific PDump Deinitialisation */
	PDumpDeInitStreams(&g_PDumpParameters.sCh, &g_PDumpScript.sCh);

	/* DeInit the PDUMP_CTRL_STATE data */
	PDumpCtrlDeInit();

	/* take down the global PDump lock */
	OSLockDestroy(g_hPDumpWriteLock);
}

void PDumpStopInitPhase(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	IMG_UINT32 ui32PDumpBoundDevice = PVRSRVGetPVRSRVData()->ui32PDumpBoundDevice;

	/* Stop the init phase for the PDump-bound device only */
	if (psDeviceNode->sDevId.ui32InternalID == ui32PDumpBoundDevice)
	{
		/* output this comment to indicate init phase ending OSs */
		PDUMPCOMMENT(psDeviceNode, "Stop Init Phase");

		PDumpCtrlLockAcquire();
		PDumpCtrlSetInitPhaseComplete(IMG_TRUE);
		PDumpCtrlLockRelease();
	}
}

PVRSRV_ERROR PDumpIsLastCaptureFrameKM(IMG_BOOL *pbIsLastCaptureFrame)
{
	PDumpCtrlLockAcquire();
	*pbIsLastCaptureFrame = PDumpCtrlIsLastCaptureFrame();
	PDumpCtrlLockRelease();

	return PVRSRV_OK;
}



typedef struct _PDUMP_Transition_DATA_
{
	PFN_PDUMP_TRANSITION        pfnCallback;
	void                        *hPrivData;
	void                        *pvDevice;
	PDUMP_CONNECTION_DATA       *psPDumpConnectionData;
	DLLIST_NODE                 sNode;
} PDUMP_Transition_DATA;

PVRSRV_ERROR PDumpRegisterTransitionCallback(PDUMP_CONNECTION_DATA *psPDumpConnectionData,
											  PFN_PDUMP_TRANSITION pfnCallback,
											  void *hPrivData,
											  void *pvDevice,
											  void **ppvHandle)
{
	PDUMP_Transition_DATA *psData;
	PVRSRV_ERROR eError;

	psData = OSAllocMem(sizeof(*psData));
	PVR_GOTO_IF_NOMEM(psData, eError, fail_alloc);

	/* Setup the callback and add it to the list for this process */
	psData->pfnCallback = pfnCallback;
	psData->hPrivData = hPrivData;
	psData->pvDevice = pvDevice;

	OSLockAcquire(psPDumpConnectionData->hLock);
	dllist_add_to_head(&psPDumpConnectionData->sListHead, &psData->sNode);
	OSLockRelease(psPDumpConnectionData->hLock);

	/* Take a reference on the connection so it doesn't get freed too early */
	psData->psPDumpConnectionData =_PDumpConnectionAcquire(psPDumpConnectionData);
	*ppvHandle = psData;

	return PVRSRV_OK;

fail_alloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

void PDumpUnregisterTransitionCallback(void *pvHandle)
{
	PDUMP_Transition_DATA *psData = pvHandle;

	OSLockAcquire(psData->psPDumpConnectionData->hLock);
	dllist_remove_node(&psData->sNode);
	OSLockRelease(psData->psPDumpConnectionData->hLock);
	_PDumpConnectionRelease(psData->psPDumpConnectionData);
	OSFreeMem(psData);
}

typedef struct _PDUMP_Transition_DATA_FENCE_SYNC_
{
	PFN_PDUMP_TRANSITION_FENCE_SYNC         pfnCallback;
	void                                    *hPrivData;
} PDUMP_Transition_DATA_FENCE_SYNC;

PVRSRV_ERROR PDumpRegisterTransitionCallbackFenceSync(void *hPrivData,
							  PFN_PDUMP_TRANSITION_FENCE_SYNC pfnCallback, void **ppvHandle)
{
	PDUMP_Transition_DATA_FENCE_SYNC *psData;
	PVRSRV_ERROR eError;

	psData = OSAllocMem(sizeof(*psData));
	PVR_GOTO_IF_NOMEM(psData, eError, fail_alloc_exit);

	/* Setup the callback and add it to the list for this process */
	psData->pfnCallback = pfnCallback;
	psData->hPrivData = hPrivData;

	*ppvHandle = psData;
	return PVRSRV_OK;

fail_alloc_exit:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

void PDumpUnregisterTransitionCallbackFenceSync(void *pvHandle)
{
	PDUMP_Transition_DATA_FENCE_SYNC *psData = pvHandle;

	OSFreeMem(psData);
}

static PVRSRV_ERROR _PDumpTransition(PVRSRV_DEVICE_NODE *psDeviceNode,
	                                 PDUMP_CONNECTION_DATA *psPDumpConnectionData,
	                                 PDUMP_TRANSITION_EVENT eEvent,
	                                 IMG_UINT32 ui32PDumpFlags)
{
	DLLIST_NODE *psNode, *psNext;
	PVRSRV_ERROR eError;

	/* Only call the callbacks if we've really got new event */
	if ((eEvent != psPDumpConnectionData->eLastEvent) && (eEvent != PDUMP_TRANSITION_EVENT_NONE))
	{
		OSLockAcquire(psPDumpConnectionData->hLock);

		dllist_foreach_node(&psPDumpConnectionData->sListHead, psNode, psNext)
		{
			PDUMP_Transition_DATA *psData =
				IMG_CONTAINER_OF(psNode, PDUMP_Transition_DATA, sNode);

			eError = psData->pfnCallback(psData->hPrivData, psData->pvDevice, eEvent, ui32PDumpFlags);

			if (eError != PVRSRV_OK)
			{
				OSLockRelease(psPDumpConnectionData->hLock);
				psPDumpConnectionData->eFailedEvent = eEvent; /* Save failed event to retry */
				return eError;
			}
		}
		OSLockRelease(psPDumpConnectionData->hLock);

		/* PDump sync blocks:
		 *
		 * Client sync prims are managed in blocks.
		 *
		 * sync-blocks gets re-dumped each time we enter into capture range or
		 * enter into new PDump block. Ensure that live-FW thread and app-thread
		 * are synchronised before this.
		 *
		 * At playback time, script-thread and sim-FW threads needs to be
		 * synchronised before re-loading sync-blocks.
		 * */
		psPDumpConnectionData->pfnPDumpSyncBlocks(psDeviceNode, psPDumpConnectionData->hSyncPrivData, eEvent);

		if (psDeviceNode->hTransition)
		{
			PDUMP_Transition_DATA_FENCE_SYNC *psData = (PDUMP_Transition_DATA_FENCE_SYNC*)psDeviceNode->hTransition;
			psData->pfnCallback(psData->hPrivData, eEvent);
		}

		psPDumpConnectionData->eLastEvent = eEvent;
		psPDumpConnectionData->eFailedEvent = PDUMP_TRANSITION_EVENT_NONE; /* Clear failed event on success */
	}
	return PVRSRV_OK;
}

static PVRSRV_ERROR _PDumpBlockTransition(PVRSRV_DEVICE_NODE *psDeviceNode,
                                          PDUMP_CONNECTION_DATA *psPDumpConnectionData,
                                          PDUMP_TRANSITION_EVENT eEvent,
                                          IMG_UINT32 ui32PDumpFlags)
{

	/* Need to follow following sequence for Block transition:
	 *
	 * (1) _PDumpTransition with BLOCK_FINISHED event for current block
	 * (2) Split MAIN and Block script files
	 * (3) _PDumpTransition with BLOCK_STARTED event for new block
	 *
	 * */

	PVRSRV_ERROR        eError;
	IMG_UINT32          ui32CurrentBlock;
	IMG_UINT32          ui32Flags = (PDUMP_FLAGS_BLKDATA | PDUMP_FLAGS_CONTINUOUS); /* Internal Block mode specific PDump flags */

	PDumpGetCurrentBlockKM(&ui32CurrentBlock);

	if (eEvent == PDUMP_TRANSITION_EVENT_BLOCK_FINISHED)
	{
		/* (1) Current block has finished */
		eError = _PDumpTransition(psDeviceNode,
			                      psPDumpConnectionData,
			                      PDUMP_TRANSITION_EVENT_BLOCK_FINISHED,
			                      ui32PDumpFlags);
		PVR_RETURN_IF_ERROR(eError);

		(void) PDumpCommentWithFlags(psDeviceNode, ui32Flags,
		                             "}PDUMP_BLOCK_END_0x%08X",
		                             ui32CurrentBlock - 1); /* Add pdump-block end marker */

		/* (2) Split MAIN and BLOCK script out files on current pdump-block end */
		ui32Flags |= PDUMP_FLAGS_FORCESPLIT;

		(void) PDumpCommentWithFlags(psDeviceNode, ui32Flags,
		                             "PDUMP_BLOCK_START_0x%08X{",
		                             ui32CurrentBlock); /* Add pdump-block start marker */
	}

	/* (3) New block has started */
	return _PDumpTransition(psDeviceNode,
		                    psPDumpConnectionData,
		                    PDUMP_TRANSITION_EVENT_BLOCK_STARTED,
		                    ui32PDumpFlags);
}


PVRSRV_ERROR PDumpTransition(PVRSRV_DEVICE_NODE *psDeviceNode,
                             PDUMP_CONNECTION_DATA *psPDumpConnectionData,
                             PDUMP_TRANSITION_EVENT eEvent,
                             IMG_UINT32 ui32PDumpFlags)
{
	if ((eEvent == PDUMP_TRANSITION_EVENT_BLOCK_FINISHED) || (eEvent == PDUMP_TRANSITION_EVENT_BLOCK_STARTED))
	{
		/* Block mode transition events */
		PVR_ASSERT(PDumpCtrlCapModIsBlocked());
		return _PDumpBlockTransition(psDeviceNode, psPDumpConnectionData, eEvent, ui32PDumpFlags);
	}
	else
	{
		/* Non-block mode transition events */
		return _PDumpTransition(psDeviceNode, psPDumpConnectionData, eEvent, ui32PDumpFlags);
	}
}

static PVRSRV_ERROR PDumpIsCaptureFrame(IMG_BOOL *bInCaptureRange)
{
	IMG_UINT64 ui64State = 0;
	PVRSRV_ERROR eError;

	eError = PDumpCtrlGetState(&ui64State);

	*bInCaptureRange = (ui64State & PDUMP_STATE_CAPTURE_FRAME) ? IMG_TRUE : IMG_FALSE;

	return eError;
}

PVRSRV_ERROR PDumpGetStateKM(IMG_UINT64 *ui64State)
{
	PVRSRV_ERROR eError;

	PDumpCtrlLockAcquire();
	eError = PDumpCtrlGetState(ui64State);
	PDumpCtrlLockRelease();

	return eError;
}

/******************************************************************************
 * Function Name  : PDumpUpdateBlockCtrlStatus
 * Inputs         : ui32Frame - frame number
 * Outputs        : None
 * Returns        : IMG_TRUE if Block transition is required, else IMG_FALSE
 * Description    : Updates Block Ctrl status and checks if block transition
 *                  is required or not
******************************************************************************/
static INLINE IMG_BOOL PDumpUpdateBlockCtrlStatus(IMG_UINT32 ui32Frame)
{
	IMG_BOOL bForceBlockTransition;

	/* Default length of first block will be PDUMP_BLOCKLEN_MIN.
	 * User can force it to be same as block length provided (i.e. ui32BlockLength)
	 * through pdump client.
	 *
	 * Here is how blocks will be created.
	 *
	 * Assume,
	 * ui32BlockLength = 20
	 * PDUMP_BLOCKLEN_MIN = 10
	 *
	 * Then different pdump blocks will have following number of frames in it:
	 *
	 * if(!PDumpCtrlMinimalFirstBlock())
	 * {
	 *		//pdump -b<block len>
	 *		block 0 -> 00...09        -->minimal first block
	 *		block 1 -> 10...29
	 *		block 2 -> 30...49
	 *		block 3 -> 50...69
	 *		...
	 * }
	 * else
	 * {
	 *		//pdump -bf<block len>
	 *		block 0 -> 00...19
	 *		block 1 -> 20...39
	 *		block 2 -> 40...59
	 *		block 3 -> 60...79
	 *		...
	 * }
	 *
	 * */

	if (PDumpCtrlMinimalFirstBlock())
	{
		bForceBlockTransition = ((ui32Frame >= PDUMP_BLOCKLEN_MIN) && !((ui32Frame - PDUMP_BLOCKLEN_MIN) % g_PDumpCtrl.sBlockCtrl.ui32BlockLength)) || (ui32Frame == 0);
	}
	else
	{
		bForceBlockTransition = !(ui32Frame % g_PDumpCtrl.sBlockCtrl.ui32BlockLength);
	}

	if (bForceBlockTransition) /* Entering in new pdump-block */
	{
		/* Update block number
		 *
		 * Logic below is to maintain block number and frame number mappings
		 * in case of some applications where setFrame(0) gets called twice
		 * at the start.
		 * */
		PDumpCtrlLockAcquire();
		PDumpCtrlSetBlock((ui32Frame == 0)? 0 : (PDumpCtrlGetBlock() + 1));
		PDumpCtrlLockRelease();

		if (ui32Frame > 0) /* Do not do transition on first frame itself */
		{
			return IMG_TRUE; /* Transition */
		}
	}
	return IMG_FALSE; /* No transition */
}

PVRSRV_ERROR PDumpForceCaptureStopKM(CONNECTION_DATA *psConnection,
                                     PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	/* If call is not for the pdump-bound device, return immediately
	 * taking no action.
	 */
	if (!PDumpIsDevicePermitted(psDeviceNode))
	{
		return PVRSRV_OK;
	}

	if (!PDumpCtrlCapModIsBlocked())
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: This call is valid only in Block mode of PDump i.e. pdump -b<block-len>", __func__));
		return PVRSRV_ERROR_PDUMP_NOT_ALLOWED;
	}

	(void) PDumpCommentWithFlags(psDeviceNode,
	                             PDUMP_FLAGS_CONTINUOUS | PDUMP_FLAGS_BLKDATA,
	                             "PDdump forced STOP capture request received at frame %u",
	                             g_PDumpCtrl.ui32CurrentFrame);

	PDumpCtrlLockAcquire();
	eError = PDumpCtrlForcedStop();
	PDumpCtrlLockRelease();

	return eError;
}

static PVRSRV_ERROR _PDumpSetFrameKM(CONNECTION_DATA *psConnection,
                                     PVRSRV_DEVICE_NODE *psDeviceNode,
                                     IMG_UINT32 ui32Frame)
{
	PDUMP_CONNECTION_DATA *psPDumpConnectionData = psConnection->psPDumpConnectionData;
	PDUMP_TRANSITION_EVENT eTransitionEvent = PDUMP_TRANSITION_EVENT_NONE;
	IMG_BOOL bWasInCaptureRange = IMG_FALSE;
	IMG_BOOL bIsInCaptureRange = IMG_FALSE;
	PVRSRV_ERROR eError;

	/*
		Note:
		As we can't test to see if the new frame will be in capture range
		before we set the frame number and we don't want to roll back the
		frame number if we fail then we have to save the "transient" data
		which decides if we're entering or exiting capture range along
		with a failure boolean so we know what's required on a retry
	*/
	if (psPDumpConnectionData->ui32LastSetFrameNumber != ui32Frame)
	{
		(void) PDumpCommentWithFlags(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
		                             "Set pdump frame %u", ui32Frame);

		/*
			The boolean values below decide if the PDump transition
			should trigger because of the current context setting the
			frame number, hence the functions below should execute
			atomically and do not give a chance to some other context
			to transition
		*/
		PDumpCtrlLockAcquire();

		PDumpIsCaptureFrame(&bWasInCaptureRange);
		PDumpCtrlSetCurrentFrame(ui32Frame);
		PDumpIsCaptureFrame(&bIsInCaptureRange);

		PDumpCtrlLockRelease();

		psPDumpConnectionData->ui32LastSetFrameNumber = ui32Frame;

		/* Check for any transition event only if client is connected */
		if (PDumpIsClientConnected())
		{
			if (!bWasInCaptureRange && bIsInCaptureRange)
			{
				eTransitionEvent = PDUMP_TRANSITION_EVENT_RANGE_ENTERED;
			}
			else if (bWasInCaptureRange && !bIsInCaptureRange)
			{
				eTransitionEvent = PDUMP_TRANSITION_EVENT_RANGE_EXITED;
			}

			if (PDumpCtrlCapModIsBlocked())
			{
				/* Update block ctrl status and check for block transition */
				if (PDumpUpdateBlockCtrlStatus(ui32Frame))
				{
					PVR_ASSERT(eTransitionEvent == PDUMP_TRANSITION_EVENT_NONE); /* Something went wrong, can't handle two events at same time */
					eTransitionEvent = PDUMP_TRANSITION_EVENT_BLOCK_FINISHED;
				}
			}
		}
	}
	else if (psPDumpConnectionData->eFailedEvent != PDUMP_TRANSITION_EVENT_NONE)
	{
		/* Load the Transition data so we can try again */
		eTransitionEvent = psPDumpConnectionData->eFailedEvent;
	}
	else
	{
		/* New frame is the same as the last frame set and the last
		 * transition succeeded, no need to perform another transition.
		 */
		return PVRSRV_OK;
	}

	if (eTransitionEvent != PDUMP_TRANSITION_EVENT_NONE)
	{
		DEBUG_OUTFILES_COMMENT(psDeviceNode, "PDump transition event(%u)-begin frame %u (post)", eTransitionEvent, ui32Frame);
		eError = PDumpTransition(psDeviceNode, psPDumpConnectionData, eTransitionEvent, PDUMP_FLAGS_NONE);
		DEBUG_OUTFILES_COMMENT(psDeviceNode, "PDump transition event(%u)-complete frame %u (post)", eTransitionEvent, ui32Frame);
		PVR_RETURN_IF_ERROR(eError);
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpSetFrameKM(CONNECTION_DATA *psConnection,
                             PVRSRV_DEVICE_NODE *psDeviceNode,
                             IMG_UINT32 ui32Frame)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	/* If call is not for the pdump-bound device, return immediately
	 * taking no action.
	 */
	if (!PDumpIsDevicePermitted(psDeviceNode))
	{
		return PVRSRV_OK;
	}

#if defined(PDUMP_TRACE_STATE)
	PVR_DPF((PVR_DBG_WARNING, "PDumpSetFrameKM: ui32Frame( %d )", ui32Frame));
#endif

	DEBUG_OUTFILES_COMMENT(psDeviceNode, "(pre) Set pdump frame %u", ui32Frame);

	eError = _PDumpSetFrameKM(psConnection, psDeviceNode, ui32Frame);
	if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
	{
		PVR_LOG_ERROR(eError, "_PDumpSetFrameKM");
	}

	DEBUG_OUTFILES_COMMENT(psDeviceNode, "(post) Set pdump frame %u", ui32Frame);

	return eError;
}

PVRSRV_ERROR PDumpGetFrameKM(CONNECTION_DATA *psConnection,
                             PVRSRV_DEVICE_NODE * psDeviceNode,
                             IMG_UINT32* pui32Frame)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	/*
		It may be safe to avoid acquiring this lock here as all the other calls
		which read/modify current frame will wait on the PDump Control bridge
		lock first. Also, in no way as of now, does the PDumping app modify the
		current frame through a call which acquires the global bridge lock.
		Still, as a legacy we acquire and then read.
	*/
	PDumpCtrlLockAcquire();

	*pui32Frame = PDumpCtrlGetCurrentFrame();

	PDumpCtrlLockRelease();
	return eError;
}

PVRSRV_ERROR PDumpSetDefaultCaptureParamsKM(CONNECTION_DATA *psConnection,
                                            PVRSRV_DEVICE_NODE *psDeviceNode,
                                            IMG_UINT32 ui32Mode,
                                            IMG_UINT32 ui32Start,
                                            IMG_UINT32 ui32End,
                                            IMG_UINT32 ui32Interval,
                                            IMG_UINT32 ui32MaxParamFileSize)
{
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	/* NB. We choose not to check that the device is the pdump-bound
	 * device here, as this particular bridge call is made only from the pdump
	 * tool itself (which may only connect to the bound device).
	 */
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);

	eError = PDumpReady();
	PVR_LOG_RETURN_IF_ERROR(eError, "PDumpReady");

	/* Validate parameters */
	if ((ui32End < ui32Start) || (ui32Mode > PDUMP_CAPMODE_MAX))
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}
	else if (ui32Mode == PDUMP_CAPMODE_BLOCKED)
	{
		if ((ui32Interval < PDUMP_BLOCKLEN_MIN) || (ui32Interval > PDUMP_BLOCKLEN_MAX))
		{
			/* Force client to set ui32Interval (i.e. block length) in valid range */
			eError = PVRSRV_ERROR_PDUMP_INVALID_BLOCKLEN;
		}

		if (ui32End != PDUMP_FRAME_MAX)
		{
			/* Force client to set ui32End to PDUMP_FRAME_MAX */
			eError = PVRSRV_ERROR_INVALID_PARAMS;
		}
	}
	else if ((ui32Mode != PDUMP_CAPMODE_UNSET) && (ui32Interval < 1))
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_LOG_RETURN_IF_ERROR(eError, "PDumpSetDefaultCaptureParamsKM");

	/*
	   Acquire PDUMP_CTRL_STATE struct lock before modifications as a
	   PDumping app may be reading the state data for some checks
	*/
	PDumpCtrlLockAcquire();
	PDumpCtrlSetDefaultCaptureParams(ui32Mode, ui32Start, ui32End, ui32Interval);
	PDumpCtrlLockRelease();

	if (ui32MaxParamFileSize == 0)
	{
		g_PDumpParameters.ui32MaxFileSize = PRM_FILE_SIZE_MAX;
	}
	else
	{
		g_PDumpParameters.ui32MaxFileSize = ui32MaxParamFileSize;
	}
	return PVRSRV_OK;
}


/******************************************************************************
 * Function Name  : PDumpReg32
 * Inputs         : pszPDumpDevName, Register offset, and value to write
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a register write
******************************************************************************/
PVRSRV_ERROR PDumpReg32(PVRSRV_DEVICE_NODE *psDeviceNode,
						IMG_CHAR	*pszPDumpRegName,
						IMG_UINT32	ui32Reg,
						IMG_UINT32	ui32Data,
						IMG_UINT32	ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW :%s:0x%08X 0x%08X", pszPDumpRegName, ui32Reg, ui32Data);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpReg64
 * Inputs         : pszPDumpDevName, Register offset, and value to write
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a register write
******************************************************************************/
PVRSRV_ERROR PDumpReg64(PVRSRV_DEVICE_NODE *psDeviceNode,
						IMG_CHAR	*pszPDumpRegName,
						IMG_UINT32	ui32Reg,
						IMG_UINT64	ui64Data,
						IMG_UINT32	ui32Flags)
{
	PVRSRV_ERROR eErr;
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	IMG_UINT32 ui32UpperValue = (IMG_UINT32) (ui64Data >> 32);
	IMG_UINT32 ui32LowerValue = (IMG_UINT32) (ui64Data);
#endif

	PDUMP_GET_SCRIPT_STRING()

#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW :%s:0x%08X 0x%08X",
					pszPDumpRegName, ui32Reg, ui32LowerValue);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW :%s:0x%08X 0x%08X",
					pszPDumpRegName, ui32Reg + 4, ui32UpperValue);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return eErr;
	}
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);
#else
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW64 :%s:0x%08X 0x%010" IMG_UINT64_FMTSPECX, pszPDumpRegName, ui32Reg, ui64Data);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);
#endif
	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpRegLabelToReg64
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a register write
 *                  from a register label
******************************************************************************/
PVRSRV_ERROR PDumpRegLabelToReg64(PVRSRV_DEVICE_NODE *psDeviceNode,
                                  IMG_CHAR *pszPDumpRegName,
                                  IMG_UINT32 ui32RegDst,
                                  IMG_UINT32 ui32RegSrc,
                                  IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW64 :%s:0x%08X :%s:0x%08X", pszPDumpRegName, ui32RegDst, pszPDumpRegName, ui32RegSrc);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;

}

/******************************************************************************
 * Function Name  : PDumpRegLabelToMem32
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a memory write
 *                  from a register label
******************************************************************************/
PVRSRV_ERROR PDumpRegLabelToMem32(IMG_CHAR *pszPDumpRegName,
                                  IMG_UINT32 ui32Reg,
                                  PMR *psPMR,
                                  IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                                  IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	IMG_CHAR aszMemspaceName[PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH];
	IMG_CHAR aszSymbolicName[PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH];
	IMG_DEVMEM_OFFSET_T uiPDumpSymbolicOffset;
	IMG_DEVMEM_OFFSET_T uiNextSymName;
	PVRSRV_DEVICE_NODE *psDeviceNode;

	PDUMP_GET_SCRIPT_STRING()

	psDeviceNode = PMR_DeviceNode(psPMR);

	eErr = PMR_PDumpSymbolicAddr(psPMR,
	                             uiLogicalOffset,
	                             PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH,
	                             aszMemspaceName,
	                             PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH,
	                             aszSymbolicName,
	                             &uiPDumpSymbolicOffset,
	                             &uiNextSymName);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW :%s:%s:0x%"IMG_UINT64_FMTSPECX" :%s:0x%08X",aszMemspaceName, aszSymbolicName,
							uiPDumpSymbolicOffset, pszPDumpRegName, ui32Reg);


	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpRegLabelToMem64
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a memory write
 *                  from a register label
******************************************************************************/
PVRSRV_ERROR PDumpRegLabelToMem64(IMG_CHAR *pszPDumpRegName,
								  IMG_UINT32 ui32Reg,
								  PMR *psPMR,
								  IMG_DEVMEM_OFFSET_T uiLogicalOffset,
								  IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	IMG_CHAR aszMemspaceName[PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH];
	IMG_CHAR aszSymbolicName[PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH];
	IMG_DEVMEM_OFFSET_T uiPDumpSymbolicOffset;
	IMG_DEVMEM_OFFSET_T uiNextSymName;
	PVRSRV_DEVICE_NODE *psDeviceNode;

	PDUMP_GET_SCRIPT_STRING()

	psDeviceNode = PMR_DeviceNode(psPMR);

	eErr = PMR_PDumpSymbolicAddr(psPMR,
	                             uiLogicalOffset,
	                             PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH,
	                             aszMemspaceName,
	                             PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH,
	                             aszSymbolicName,
	                             &uiPDumpSymbolicOffset,
	                             &uiNextSymName);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW64 :%s:%s:0x%"IMG_UINT64_FMTSPECX" :%s:0x%08X",aszMemspaceName, aszSymbolicName,
							uiPDumpSymbolicOffset, pszPDumpRegName, ui32Reg);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;
}


/******************************************************************************
 * Function Name  : PDumpPhysHandleToInternalVar64
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents an internal var
                    write using a PDump pages handle
******************************************************************************/
PVRSRV_ERROR PDumpPhysHandleToInternalVar64(PVRSRV_DEVICE_NODE *psDeviceNode,
                                            IMG_CHAR *pszInternalVar,
                                            IMG_HANDLE hPdumpPages,
                                            IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	IMG_CHAR *pszSymbolicName;
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	IMG_CHAR *pszPDumpVarName;
#endif

	PDUMP_GET_SCRIPT_STRING()

	eErr = PDumpGetSymbolicAddr(hPdumpPages,
	                            &pszSymbolicName);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	eErr = PDumpSNPrintf(hScript, ui32MaxLen,
	                        "WRW %s %s:0x%llX",
	                        pszInternalVar, pszSymbolicName, 0llu);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);

#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	pszPDumpVarName = PDumpCreateIncVarNameStr(pszInternalVar);
	if (pszPDumpVarName == NULL)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW %s 0x%X", pszPDumpVarName, 0);

	PDumpFreeIncVarNameStr(pszPDumpVarName);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return eErr;
	}

	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);

#endif
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING();

	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpMemLabelToInternalVar64
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents an internal var
 *                  write using a memory label
******************************************************************************/
PVRSRV_ERROR PDumpMemLabelToInternalVar64(IMG_CHAR *pszInternalVar,
                                          PMR *psPMR,
                                          IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                                          IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	IMG_CHAR aszMemspaceName[PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH];
	IMG_CHAR aszSymbolicName[PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH];
	IMG_DEVMEM_OFFSET_T uiPDumpSymbolicOffset;
	IMG_DEVMEM_OFFSET_T uiNextSymName;
	PVRSRV_DEVICE_NODE *psDeviceNode;
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	IMG_CHAR *pszPDumpVarName;
#endif

	PDUMP_GET_SCRIPT_STRING()

	psDeviceNode = PMR_DeviceNode(psPMR);

	eErr = PMR_PDumpSymbolicAddr(psPMR,
	                             uiLogicalOffset,
	                             PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH,
	                             aszMemspaceName,
	                             PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH,
	                             aszSymbolicName,
	                             &uiPDumpSymbolicOffset,
	                             &uiNextSymName);


	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW %s :%s:%s:0x%"IMG_UINT64_FMTSPECX, pszInternalVar,
							aszMemspaceName, aszSymbolicName, uiPDumpSymbolicOffset);


	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);

#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	pszPDumpVarName = PDumpCreateIncVarNameStr(pszInternalVar);
	if (pszPDumpVarName == NULL)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW %s :%s:%s:0x%"IMG_UINT64_FMTSPECX, pszPDumpVarName,
							aszMemspaceName, aszSymbolicName, uiPDumpSymbolicOffset);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return eErr;
	}

	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "SHR %s %s 0x20", pszPDumpVarName, pszPDumpVarName);

	PDumpFreeIncVarNameStr(pszPDumpVarName);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return eErr;
	}

	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);

#endif
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpInternalVarToMemLabel
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a memory label
 *                  write using an internal var
******************************************************************************/
PVRSRV_ERROR PDumpInternalVarToMemLabel(PMR *psPMR,
                                        IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                                        IMG_CHAR *pszInternalVar,
                                        IMG_UINT32	ui32Flags)
{
	PVRSRV_ERROR eErr;
	IMG_CHAR aszMemspaceName[PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH];
	IMG_CHAR aszSymbolicName[PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH];
	IMG_DEVMEM_OFFSET_T uiPDumpSymbolicOffset;
	IMG_DEVMEM_OFFSET_T uiNextSymName;
	PVRSRV_DEVICE_NODE *psDeviceNode;
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	IMG_CHAR *pszPDumpVarName;
#endif

	PDUMP_GET_SCRIPT_STRING()

	psDeviceNode = PMR_DeviceNode(psPMR);

	eErr = PMR_PDumpSymbolicAddr(psPMR,
	                             uiLogicalOffset,
	                             PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH,
	                             aszMemspaceName,
	                             PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH,
	                             aszSymbolicName,
	                             &uiPDumpSymbolicOffset,
	                             &uiNextSymName);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW :%s:%s:0x%"IMG_UINT64_FMTSPECX" %s",
							aszMemspaceName, aszSymbolicName, uiPDumpSymbolicOffset, pszInternalVar);


	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return eErr;
	}

	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);

#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	pszPDumpVarName = PDumpCreateIncVarNameStr(pszInternalVar);
	if (pszPDumpVarName == NULL)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW %s 0x%X", pszPDumpVarName, 0);

	PDumpFreeIncVarNameStr(pszPDumpVarName);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return eErr;
	}

	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);

#endif
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	PDumpWriteRegORValueOp

 @Description

 Emits the PDump commands for the logical OR operation
 Var <- Var OR Value

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PDumpWriteVarORValueOp(PVRSRV_DEVICE_NODE *psDeviceNode,
                                    const IMG_CHAR *pszInternalVariable,
                                    const IMG_UINT64 ui64Value,
                                    const IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	IMG_CHAR *pszPDumpVarName;
	IMG_UINT32 ui32UpperValue = (IMG_UINT32) (ui64Value >> 32);
	IMG_UINT32 ui32LowerValue = (IMG_UINT32) (ui64Value);
#endif

	PDUMP_GET_SCRIPT_STRING();

	eErr = PDumpSNPrintf(hScript,
			ui32MaxLen,
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
			"OR %s %s 0x%X",
#else
			"OR %s %s 0x%"IMG_UINT64_FMTSPECX,
#endif
			pszInternalVariable,
			pszInternalVariable,
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
			ui32LowerValue
#else
			ui64Value
#endif
			);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	pszPDumpVarName = PDumpCreateIncVarNameStr(pszInternalVariable);
	if (pszPDumpVarName == NULL)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32PDumpFlags);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eErr = PDumpSNPrintf(hScript,
			ui32MaxLen,
			"OR %s %s 0x%X",
			pszPDumpVarName,
			pszPDumpVarName,
			ui32UpperValue);

	PDumpFreeIncVarNameStr(pszPDumpVarName);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32PDumpFlags);
		return eErr;
	}

	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);
#endif

	PDUMP_UNLOCK(ui32PDumpFlags);

	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	PDumpWriteVarORVarOp

 @Description

 Emits the PDump commands for the logical OR operation
 Var <- Var OR Var2

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PDumpWriteVarORVarOp(PVRSRV_DEVICE_NODE *psDeviceNode,
                                  const IMG_CHAR *pszInternalVar,
                                  const IMG_CHAR *pszInternalVar2,
                                  const IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;

	PDUMP_GET_SCRIPT_STRING();

	eErr = PDumpSNPrintf(hScript,
			ui32MaxLen,
			"OR %s %s %s",
			pszInternalVar,
			pszInternalVar,
			pszInternalVar2);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

	PDUMP_UNLOCK(ui32PDumpFlags);
	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	PDumpWriteVarANDVarOp

 @Description

 Emits the PDump commands for the logical AND operation
 Var <- Var AND Var2

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PDumpWriteVarANDVarOp(PVRSRV_DEVICE_NODE *psDeviceNode,
                                   const IMG_CHAR *pszInternalVar,
                                   const IMG_CHAR *pszInternalVar2,
                                   const IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;

	PDUMP_GET_SCRIPT_STRING();

	eErr = PDumpSNPrintf(hScript,
			ui32MaxLen,
			"AND %s %s %s",
			pszInternalVar,
			pszInternalVar,
			pszInternalVar2);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

	PDUMP_UNLOCK(ui32PDumpFlags);
	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;
}


/******************************************************************************
 * Function Name  : PDumpRegLabelToInternalVar
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which writes a register label into
 *                  an internal variable
******************************************************************************/
PVRSRV_ERROR PDumpRegLabelToInternalVar(PVRSRV_DEVICE_NODE *psDeviceNode,
                                        IMG_CHAR *pszPDumpRegName,
                                        IMG_UINT32 ui32Reg,
                                        IMG_CHAR *pszInternalVar,
                                        IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	IMG_CHAR *pszPDumpVarName;
#endif
	PDUMP_GET_SCRIPT_STRING()

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW %s :%s:0x%08X", pszInternalVar, pszPDumpRegName, ui32Reg);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);

#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	pszPDumpVarName = PDumpCreateIncVarNameStr(pszInternalVar);
	if (pszPDumpVarName == NULL)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW %s :%s:0x%08X", pszPDumpVarName, pszPDumpRegName, ui32Reg + 4);

	PDumpFreeIncVarNameStr(pszPDumpVarName);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return eErr;
	}

	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
#endif

	PDUMP_UNLOCK(ui32Flags);
	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;

}

/******************************************************************************
 * Function Name  : PDumpInternalVarToReg32
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a register write
 *                  from an internal variable
******************************************************************************/
PVRSRV_ERROR PDumpInternalVarToReg32(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     IMG_CHAR *pszPDumpRegName,
                                     IMG_UINT32 ui32Reg,
                                     IMG_CHAR *pszInternalVar,
                                     IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW :%s:0x%08X %s", pszPDumpRegName, ui32Reg, pszInternalVar);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpInternalVarToReg64
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a register write
 *                  from an internal variable
******************************************************************************/
PVRSRV_ERROR PDumpInternalVarToReg64(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     IMG_CHAR *pszPDumpRegName,
                                     IMG_UINT32 ui32Reg,
                                     IMG_CHAR *pszInternalVar,
                                     IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	IMG_CHAR *pszPDumpVarName;
#endif
	PDUMP_GET_SCRIPT_STRING()

#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW :%s:0x%08X %s", pszPDumpRegName, ui32Reg, pszInternalVar);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);

	pszPDumpVarName = PDumpCreateIncVarNameStr(pszInternalVar);
	if (pszPDumpVarName == NULL)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW :%s:0x%08X %s", pszPDumpRegName, ui32Reg + 4, pszPDumpVarName);

	PDumpFreeIncVarNameStr(pszPDumpVarName);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return eErr;
	}

	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

#else
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "WRW64 :%s:0x%08X %s", pszPDumpRegName, ui32Reg, pszInternalVar);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);
#endif

	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;
}



/******************************************************************************
 * Function Name  : PDumpMemLabelToMem32
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a memory write from
 *                  a memory label
******************************************************************************/
PVRSRV_ERROR PDumpMemLabelToMem32(PMR *psPMRSource,
                                  PMR *psPMRDest,
                                  IMG_DEVMEM_OFFSET_T uiLogicalOffsetSource,
                                  IMG_DEVMEM_OFFSET_T uiLogicalOffsetDest,
                                  IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	IMG_CHAR aszMemspaceNameSource[PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH];
	IMG_CHAR aszSymbolicNameSource[PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH];
	IMG_CHAR aszMemspaceNameDest[PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH];
	IMG_CHAR aszSymbolicNameDest[PHYSMEM_PDUMP_MEMSPNAME_SYMB_ADDR_MAX_LENGTH];
	IMG_DEVMEM_OFFSET_T uiPDumpSymbolicOffsetSource;
	IMG_DEVMEM_OFFSET_T uiPDumpSymbolicOffsetDest;
	IMG_DEVMEM_OFFSET_T uiNextSymNameSource;
	IMG_DEVMEM_OFFSET_T uiNextSymNameDest;
	PVRSRV_DEVICE_NODE *psDeviceNode;


	PDUMP_GET_SCRIPT_STRING()

	psDeviceNode = PMR_DeviceNode(psPMRSource);

	eErr = PMR_PDumpSymbolicAddr(psPMRSource,
	                             uiLogicalOffsetSource,
	                             PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH,
	                             aszMemspaceNameSource,
	                             PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH,
	                             aszSymbolicNameSource,
	                             &uiPDumpSymbolicOffsetSource,
	                             &uiNextSymNameSource);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	eErr = PMR_PDumpSymbolicAddr(psPMRDest,
	                             uiLogicalOffsetDest,
	                             PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH,
	                             aszMemspaceNameDest,
	                             PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH,
	                             aszSymbolicNameDest,
	                             &uiPDumpSymbolicOffsetDest,
	                             &uiNextSymNameDest);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	eErr = PDumpSNPrintf(hScript, ui32MaxLen,
	                     "WRW :%s:%s:0x%"IMG_UINT64_FMTSPECX" :%s:%s:0x%"IMG_UINT64_FMTSPECX,
	                     aszMemspaceNameDest, aszSymbolicNameDest,
	                     uiPDumpSymbolicOffsetDest, aszMemspaceNameSource,
	                     aszSymbolicNameSource, uiPDumpSymbolicOffsetSource);


	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}


	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpMemLabelToMem64
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a memory write from
 *                  a memory label
******************************************************************************/
PVRSRV_ERROR PDumpMemLabelToMem64(PMR *psPMRSource,
								  PMR *psPMRDest,
								  IMG_DEVMEM_OFFSET_T uiLogicalOffsetSource,
								  IMG_DEVMEM_OFFSET_T uiLogicalOffsetDest,
								  IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	IMG_CHAR aszMemspaceNameSource[PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH];
	IMG_CHAR aszSymbolicNameSource[PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH];
	IMG_CHAR aszMemspaceNameDest[PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH];
	IMG_CHAR aszSymbolicNameDest[PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH];
	IMG_DEVMEM_OFFSET_T uiPDumpSymbolicOffsetSource;
	IMG_DEVMEM_OFFSET_T uiPDumpSymbolicOffsetDest;
	IMG_DEVMEM_OFFSET_T uiNextSymNameSource;
	IMG_DEVMEM_OFFSET_T uiNextSymNameDest;
	PVRSRV_DEVICE_NODE *psDeviceNode;


	PDUMP_GET_SCRIPT_STRING()

	psDeviceNode = PMR_DeviceNode(psPMRSource);

	eErr = PMR_PDumpSymbolicAddr(psPMRSource,
	                             uiLogicalOffsetSource,
	                             PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH,
	                             aszMemspaceNameSource,
	                             PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH,
	                             aszSymbolicNameSource,
	                             &uiPDumpSymbolicOffsetSource,
	                             &uiNextSymNameSource);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	eErr = PMR_PDumpSymbolicAddr(psPMRDest,
	                             uiLogicalOffsetDest,
	                             PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH,
	                             aszMemspaceNameDest,
	                             PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH,
	                             aszSymbolicNameDest,
	                             &uiPDumpSymbolicOffsetDest,
	                             &uiNextSymNameDest);


	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	eErr = PDumpSNPrintf(hScript, ui32MaxLen,
	                     "WRW64 :%s:%s:0x%"IMG_UINT64_FMTSPECX" :%s:%s:0x%"IMG_UINT64_FMTSPECX,
	                     aszMemspaceNameDest, aszSymbolicNameDest,
	                     uiPDumpSymbolicOffsetDest, aszMemspaceNameSource,
	                     aszSymbolicNameSource, uiPDumpSymbolicOffsetSource);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}



/*!
*******************************************************************************

 @Function	PDumpWriteVarSHRValueOp

 @Description

 Emits the PDump commands for the logical SHR operation
 Var <-  Var SHR Value

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PDumpWriteVarSHRValueOp(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     const IMG_CHAR *pszInternalVariable,
                                     const IMG_UINT64 ui64Value,
                                     const IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	IMG_CHAR *pszPDumpVarName;
	IMG_UINT32 ui32UpperValue = (IMG_UINT32) (ui64Value >> 32);
	IMG_UINT32 ui32LowerValue = (IMG_UINT32) (ui64Value);
#endif

	PDUMP_GET_SCRIPT_STRING();

	eErr = PDumpSNPrintf(hScript,
			ui32MaxLen,
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
			"SHR %s %s 0x%X",
#else
			"SHR %s %s 0x%"IMG_UINT64_FMTSPECX,
#endif
			pszInternalVariable,
			pszInternalVariable,
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
			ui32LowerValue
#else
			ui64Value
#endif
			);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	pszPDumpVarName = PDumpCreateIncVarNameStr(pszInternalVariable);
	if (pszPDumpVarName == NULL)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32PDumpFlags);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eErr = PDumpSNPrintf(hScript,
			ui32MaxLen,
			"SHR %s %s 0x%X",
			pszPDumpVarName,
			pszPDumpVarName,
			ui32UpperValue);

	PDumpFreeIncVarNameStr(pszPDumpVarName);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32PDumpFlags);
		return eErr;
	}

	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);
#endif

	PDUMP_UNLOCK(ui32PDumpFlags);

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}


/*!
*******************************************************************************

 @Function	PDumpWriteRegANDValueOp

 @Description

 Emits the PDump commands for the logical AND operation
 Var <-  Var AND Value

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PDumpWriteVarANDValueOp(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     const IMG_CHAR *pszInternalVariable,
                                     const IMG_UINT64 ui64Value,
                                     const IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	IMG_CHAR *pszPDumpVarName;
	IMG_UINT32 ui32UpperValue = (IMG_UINT32) (ui64Value >> 32);
	IMG_UINT32 ui32LowerValue = (IMG_UINT32) (ui64Value);
#endif

	PDUMP_GET_SCRIPT_STRING();

	eErr = PDumpSNPrintf(hScript,
			ui32MaxLen,
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
			"AND %s %s 0x%X",
#else
			"AND %s %s 0x%"IMG_UINT64_FMTSPECX,
#endif
			pszInternalVariable,
			pszInternalVariable,
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
			ui32LowerValue
#else
			ui64Value
#endif
			);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	pszPDumpVarName = PDumpCreateIncVarNameStr(pszInternalVariable);
	if (pszPDumpVarName == NULL)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32PDumpFlags);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eErr = PDumpSNPrintf(hScript,
			ui32MaxLen,
			"AND %s %s 0x%X",
			pszPDumpVarName,
			pszPDumpVarName,
			ui32UpperValue);

	PDumpFreeIncVarNameStr(pszPDumpVarName);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32PDumpFlags);
		return eErr;
	}

	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);
#endif

	PDUMP_UNLOCK(ui32PDumpFlags);

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}


/******************************************************************************
 * Function Name  : PDumpSAW
 * Inputs         : pszDevSpaceName -- device space from which to output
 *                  ui32Offset -- offset value from register base
 *                  ui32NumSaveBytes -- number of bytes to output
 *                  pszOutfileName -- name of file to output to
 *                  ui32OutfileOffsetByte -- offset into output file to write
 *                  uiPDumpFlags -- flags to pass to PDumpOSWriteScript
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Dumps the contents of a register bank into a file
 *                  NB: ui32NumSaveBytes must be divisible by 4
******************************************************************************/
PVRSRV_ERROR PDumpSAW(PVRSRV_DEVICE_NODE *psDeviceNode,
                      IMG_CHAR           *pszDevSpaceName,
                      IMG_UINT32         ui32HPOffsetBytes,
                      IMG_UINT32         ui32NumSaveBytes,
                      IMG_CHAR           *pszOutfileName,
                      IMG_UINT32         ui32OutfileOffsetByte,
                      PDUMP_FLAGS_T      uiPDumpFlags)
{
	PVRSRV_ERROR eError;

	PDUMP_GET_SCRIPT_STRING()

	PVR_DPF((PVR_DBG_ERROR, "PDumpSAW"));

	eError = PDumpSNPrintf(hScript,
	                          ui32MaxLen,
	                          "SAW :%s:0x%x 0x%x 0x%x %s\n",
	                          pszDevSpaceName,
	                          ui32HPOffsetBytes,
	                          ui32NumSaveBytes / (IMG_UINT32)sizeof(IMG_UINT32),
	                          ui32OutfileOffsetByte,
	                          pszOutfileName);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PDumpSAW PDumpSNPrintf failed: eError=%u", eError));
		PDUMP_RELEASE_SCRIPT_STRING()
		return eError;
	}

	PDUMP_LOCK(uiPDumpFlags);
	if (! PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags))
	{
		PVR_DPF((PVR_DBG_ERROR, "PDumpSAW PDumpWriteScript failed!"));
	}
	PDUMP_UNLOCK(uiPDumpFlags);

	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;

}


/******************************************************************************
 * Function Name  : PDumpRegPolKM
 * Inputs         : Description of what this register read is trying to do
 *					pszPDumpDevName
 *					Register offset
 *					expected value
 *					mask for that value
 * Outputs        : None
 * Returns        : None
 * Description    : Create a PDUMP string which represents a register read
 *					with the expected value
******************************************************************************/
PVRSRV_ERROR PDumpRegPolKM(PVRSRV_DEVICE_NODE	*psDeviceNode,
						   IMG_CHAR				*pszPDumpRegName,
						   IMG_UINT32			ui32RegAddr,
						   IMG_UINT32			ui32RegValue,
						   IMG_UINT32			ui32Mask,
						   IMG_UINT32			ui32Flags,
						   PDUMP_POLL_OPERATOR	eOperator)
{
	/* Timings correct for Linux and XP */
	/* Timings should be passed in */
	#define POLL_DELAY			1000U
	#define POLL_COUNT_LONG		(2000000000U / POLL_DELAY)
	#define POLL_COUNT_SHORT	(1000000U / POLL_DELAY)

	PVRSRV_ERROR eErr;
	IMG_UINT32	ui32PollCount;

	PDUMP_GET_SCRIPT_STRING();

	ui32PollCount = POLL_COUNT_LONG;

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "POL :%s:0x%08X 0x%08X 0x%08X %d %u %d",
							pszPDumpRegName, ui32RegAddr, ui32RegValue,
							ui32Mask, eOperator, ui32PollCount, POLL_DELAY);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING()
	return PVRSRV_OK;
}

/*!
 * \name	PDumpOSVerifyLineEnding
 */
static void _PDumpVerifyLineEnding(IMG_HANDLE hBuffer, IMG_UINT32 ui32BufferSizeMax)
{
	IMG_UINT32 ui32Count;
	IMG_CHAR* pszBuf = hBuffer;

	/* strlen */
	ui32Count = OSStringNLength(pszBuf, ui32BufferSizeMax);

	/* Put \n sequence at the end if it isn't already there */
	if ((ui32Count >= 1) && (pszBuf[ui32Count-1] != '\n') && (ui32Count<ui32BufferSizeMax))
	{
		pszBuf[ui32Count] = '\n';
		ui32Count++;
		pszBuf[ui32Count] = '\0';
	}
}


/* Never call direct, needs caller to hold OS Lock.
 * Use PDumpCommentWithFlags() from within the server.
 * Clients call this via the bridge and PDumpCommentKM().
 */
static PVRSRV_ERROR _PDumpWriteComment(PVRSRV_DEVICE_NODE *psDeviceNode,
                                       IMG_CHAR *pszComment,
                                       IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
#if defined(PDUMP_DEBUG_OUTFILES)
	IMG_CHAR pszTemp[PVRSRV_PDUMP_MAX_COMMENT_SIZE+80];
	IMG_INT32 iCount;
#endif

	PDUMP_GET_SCRIPT_STRING();

	PVR_ASSERT(pszComment != NULL);

	if (OSStringNLength(pszComment, ui32MaxLen) == 0)
	{
		/* PDumpOSVerifyLineEnding silently fails if pszComment is too short to
		   actually hold the line endings that it's trying to enforce, so
		   short circuit it and force safety */
		pszComment = "\n";
	}
	else
	{
		/* Put line ending sequence at the end if it isn't already there */
		_PDumpVerifyLineEnding(pszComment, ui32MaxLen);
	}

#if defined(PDUMP_DEBUG_OUTFILES)
	/* Prefix comment with PID and line number */
	iCount = OSSNPrintf(pszTemp, PVRSRV_PDUMP_MAX_COMMENT_SIZE+80, "%u %u:%lu %s: %s",
		OSAtomicRead(&g_sEveryLineCounter),
		OSGetCurrentClientProcessIDKM(),
		(unsigned long)OSGetCurrentClientThreadIDKM(),
		OSGetCurrentClientProcessNameKM(),
		pszComment);
	if ((iCount < 0) || (iCount >= (PVRSRV_PDUMP_MAX_COMMENT_SIZE+80)))
	{
		eErr = PVRSRV_ERROR_PDUMP_BUF_OVERFLOW;
	}

	/* Append the comment to the script stream */
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "-- %s",
		pszTemp);
#else
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "-- %s",
		pszComment);
#endif
	if ((eErr != PVRSRV_OK) &&
		(eErr != PVRSRV_ERROR_PDUMP_BUF_OVERFLOW))
	{
		PVR_LOG_GOTO_IF_ERROR(eErr, "PDumpSNPrintf", ErrUnlock);
	}

	if (!PDumpWriteScript(psDeviceNode, hScript, ui32Flags))
	{
		if (PDUMP_IS_CONTINUOUS(ui32Flags))
		{
			eErr = PVRSRV_ERROR_PDUMP_BUFFER_FULL;
			PVR_LOG_GOTO_IF_ERROR(eErr, "PDumpWriteScript", ErrUnlock);
		}
		else
		{
			eErr = PVRSRV_ERROR_CMD_NOT_PROCESSED;
			PVR_LOG_GOTO_IF_ERROR(eErr, "PDumpWriteScript", ErrUnlock);
		}
	}

ErrUnlock:
	PDUMP_RELEASE_SCRIPT_STRING()
	return eErr;
}

/******************************************************************************
 * Function Name  : PDumpCommentKM
 * Inputs         : ui32CommentSize, pszComment, ui32Flags
 * Outputs        : None
 * Returns        : None
 * Description    : Dumps a pre-formatted comment, primarily called from the
 *                : bridge.
******************************************************************************/
PVRSRV_ERROR PDumpCommentKM(CONNECTION_DATA *psConnection,
                            PVRSRV_DEVICE_NODE *psDeviceNode,
                            IMG_UINT32 ui32CommentSize,
                            IMG_CHAR *pszComment,
                            IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(ui32CommentSize); /* Generated bridge code appends null char to pszComment. */

	PDUMP_LOCK(ui32Flags);

	eErr = _PDumpWriteComment(psDeviceNode, pszComment, ui32Flags);

	PDUMP_UNLOCK(ui32Flags);
	return eErr;
}

/******************************************************************************
 * Function Name  : PDumpCommentWithFlagsNoLockVA
 * Inputs         : ui32Flags - PDump flags
 *				  : pszFormat - format string for comment
 *				  : args      - pre-started va_list args for format string
 * Outputs        : None
 * Returns        : None
 * Description    : PDumps a comment, caller need to acquire pdump lock
 *                  explicitly before calling this function
******************************************************************************/
static PVRSRV_ERROR PDumpCommentWithFlagsNoLockVA(PVRSRV_DEVICE_NODE *psDeviceNode,
                                           IMG_UINT32 ui32Flags,
                                           const IMG_CHAR * pszFormat, va_list args)
{
	IMG_INT32 iCount;
	PVRSRV_ERROR eErr = PVRSRV_OK;
	PDUMP_GET_MSG_STRING();

	/* Construct the string */
	iCount = OSVSNPrintf(pszMsg, ui32MaxLen, pszFormat, args);
	PVR_LOG_GOTO_IF_FALSE(((iCount != -1) && (iCount < ui32MaxLen)), "OSVSNPrintf", exit);

	eErr = _PDumpWriteComment(psDeviceNode, pszMsg, ui32Flags);

exit:
	PDUMP_RELEASE_MSG_STRING();
	return eErr;
}

/******************************************************************************
 * Function Name  : PDumpCommentWithFlagsNoLock
 * Inputs         : ui32Flags - PDump flags
 *				  : pszFormat - format string for comment
 *				  : ... - args for format string
 * Outputs        : None
 * Returns        : None
 * Description    : PDumps a comment, caller need to acquire pdump lock
 *                  explicitly before calling this function.
******************************************************************************/
__printf(3, 4)
static PVRSRV_ERROR PDumpCommentWithFlagsNoLock(PVRSRV_DEVICE_NODE *psDeviceNode,
                                         IMG_UINT32 ui32Flags,
                                         IMG_CHAR *pszFormat, ...)
{
	PVRSRV_ERROR eErr = PVRSRV_OK;
	va_list args;

	va_start(args, pszFormat);
	PDumpCommentWithFlagsNoLockVA(psDeviceNode, ui32Flags, pszFormat, args);
	va_end(args);

	return eErr;
}

/******************************************************************************
 * Function Name  : PDumpCommentWithFlags
 * Inputs         : ui32Flags - PDump flags
 *				  : pszFormat - format string for comment
 *				  : ... - args for format string
 * Outputs        : None
 * Returns        : None
 * Description    : PDumps a comments
******************************************************************************/
PVRSRV_ERROR PDumpCommentWithFlags(PVRSRV_DEVICE_NODE *psDeviceNode,
                                   IMG_UINT32 ui32Flags,
                                   IMG_CHAR * pszFormat, ...)
{
	PVRSRV_ERROR eErr = PVRSRV_OK;
	va_list args;

	va_start(args, pszFormat);
	PDumpCommentWithFlagsVA(psDeviceNode, ui32Flags, pszFormat, args);
	va_end(args);

	return eErr;
}

/******************************************************************************
 * Function Name  : PDumpCommentWithFlagsVA
 * Inputs         : ui32Flags - PDump flags
 *				  : pszFormat - format string for comment
 *				  : args      - pre-started va_list args for format string
 * Outputs        : None
 * Returns        : None
 * Description    : PDumps a comments
******************************************************************************/
PVRSRV_ERROR PDumpCommentWithFlagsVA(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     IMG_UINT32 ui32Flags,
                                     const IMG_CHAR * pszFormat, va_list args)
{
	IMG_INT32 iCount;
	PVRSRV_ERROR eErr = PVRSRV_OK;
	PDUMP_GET_MSG_STRING();

	/* Construct the string */
	iCount = OSVSNPrintf(pszMsg, ui32MaxLen, pszFormat, args);
	PVR_LOG_GOTO_IF_FALSE(((iCount != -1) && (iCount < ui32MaxLen)), "OSVSNPrintf", exit);

	PDUMP_LOCK(ui32Flags);
	eErr = _PDumpWriteComment(psDeviceNode, pszMsg, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

exit:
	PDUMP_RELEASE_MSG_STRING();
	return eErr;
}

/******************************************************************************
 * Function Name  : PDumpCOMCommand
 * Inputs         : ui32PDumpFlags - PDump flags
 *			: pszPdumpStr - string for COM command
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : PDumps a COM command
******************************************************************************/
PVRSRV_ERROR PDumpCOMCommand(PVRSRV_DEVICE_NODE *psDeviceNode,
                             IMG_UINT32 ui32PDumpFlags,
                             const IMG_CHAR * pszPdumpStr)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "COM %s\n", pszPdumpStr);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);
	PDUMP_UNLOCK(ui32PDumpFlags);

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}

/*************************************************************************/ /*!
 * Function Name  : PDumpPanic
 * Inputs         : ui32PanicNo - Unique number for panic condition
 *				  : pszPanicMsg - Panic reason message limited to ~90 chars
 *				  : pszPPFunc   - Function name string where panic occurred
 *				  : ui32PPline  - Source line number where panic occurred
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : PDumps a panic assertion. Used when the host driver
 *                : detects a condition that will lead to an invalid PDump
 *                : script that cannot be played back off-line.
 */ /*************************************************************************/
PVRSRV_ERROR PDumpPanic(PVRSRV_DEVICE_NODE *psDeviceNode,
						IMG_UINT32      ui32PanicNo,
						IMG_CHAR*       pszPanicMsg,
						const IMG_CHAR* pszPPFunc,
						IMG_UINT32      ui32PPline)
{
	PVRSRV_ERROR   eError = PVRSRV_OK;
	PDUMP_FLAGS_T  uiPDumpFlags = PDUMP_FLAGS_CONTINUOUS;
	PDUMP_GET_SCRIPT_STRING();

	/* Log the panic condition to the live kern.log in both REL and DEB mode
	 * to aid user PDump troubleshooting. */
	PVR_LOG(("PDUMP PANIC %08x: %s", ui32PanicNo, pszPanicMsg));
	PVR_DPF((PVR_DBG_MESSAGE, "PDUMP PANIC start %s:%d", pszPPFunc, ui32PPline));

	/* Check the supplied panic reason string is within length limits */
	PVR_ASSERT(OSStringLength(pszPanicMsg)+sizeof("PANIC   ") < PVRSRV_PDUMP_MAX_COMMENT_SIZE-1);

	/* Obtain lock to keep the multi-line
	 * panic statement together in a single atomic write */
	PDUMP_BLKSTART(uiPDumpFlags);


	/* Write -- Panic start (Function:line) */
	eError = PDumpSNPrintf(hScript, ui32MaxLen, "-- Panic start (%s:%d)", pszPPFunc, ui32PPline);
	PVR_LOG_GOTO_IF_ERROR(eError, "PDumpSNPrintf", e1);
	(void)PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);

	/* Write COM messages */
	eError = PDumpCOMCommand(psDeviceNode, uiPDumpFlags,
				  "**** Script invalid and not compatible with off-line playback. ****");
	PVR_LOG_GOTO_IF_ERROR(eError, "PDumpCOMCommand", e1);

	eError = PDumpCOMCommand(psDeviceNode, uiPDumpFlags,
				  "**** Check test parameters and driver configuration, stop imminent. ****");
	PVR_LOG_GOTO_IF_ERROR(eError, "PDumpCOMCommand", e1);

	/* Write PANIC no msg command */
	eError = PDumpSNPrintf(hScript, ui32MaxLen, "PANIC %08x %s", ui32PanicNo, pszPanicMsg);
	PVR_LOG_GOTO_IF_ERROR(eError, "PDumpSNPrintf", e1);
	(void)PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);

	/* Write -- Panic end */
	eError = PDumpSNPrintf(hScript, ui32MaxLen, "-- Panic end");
	PVR_LOG_GOTO_IF_ERROR(eError, "PDumpSNPrintf", e1);
	(void)PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);

e1:
	PDUMP_BLKEND(uiPDumpFlags);

	PDUMP_RELEASE_SCRIPT_STRING();
	return eError;
}

/*************************************************************************/ /*!
 * Function Name  : PDumpCaptureError
 * Inputs         : ui32ErrorNo - Unique number for panic condition
 *                : pszErrorMsg - Panic reason message limited to ~90 chars
 *                : pszPPFunc   - Function name string where panic occurred
 *                : ui32PPline  - Source line number where panic occurred
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : PDumps an error string to the script file to interrupt
 *                : play back to inform user of a fatal issue that occurred
 *                : during PDump capture.
 */ /*************************************************************************/
PVRSRV_ERROR PDumpCaptureError(PVRSRV_DEVICE_NODE *psDeviceNode,
                               PVRSRV_ERROR       ui32ErrorNo,
                               IMG_CHAR*          pszErrorMsg,
                               const IMG_CHAR     *pszPPFunc,
                               IMG_UINT32         ui32PPline)
{
	IMG_CHAR*       pszFormatStr = "DRIVER_ERROR: %3d: %s";
	PDUMP_FLAGS_T   uiPDumpFlags = PDUMP_FLAGS_CONTINUOUS;

	/* Need to return an error using this macro */
	PDUMP_GET_SCRIPT_STRING();

	/* Check the supplied panic reason string is within length limits */
	PVR_ASSERT(OSStringLength(pszErrorMsg)+sizeof(pszFormatStr) < PVRSRV_PDUMP_MAX_COMMENT_SIZE-1);

	/* Write driver error message to the script file */
	(void) PDumpSNPrintf(hScript, ui32MaxLen, pszFormatStr, ui32ErrorNo, pszErrorMsg);

	/* Obtain lock to keep the multi-line
	 * panic statement together in a single atomic write */
	PDUMP_LOCK(uiPDumpFlags);
	(void) PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);
	PDUMP_UNLOCK(uiPDumpFlags);

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	PDumpImageDescriptor

 @Description

 Dumps an OutputImage command and its associated header info.

 @Input    psDeviceNode			: device
 @Input    ui32MMUContextID		: MMU context
 @Input    pszSABFileName		: filename string

 @Return   PVRSRV_ERROR			:

******************************************************************************/
PVRSRV_ERROR PDumpImageDescriptor(PVRSRV_DEVICE_NODE *psDeviceNode,
									IMG_UINT32 ui32MMUContextID,
									IMG_CHAR *pszSABFileName,
									IMG_DEV_VIRTADDR sData,
									IMG_UINT32 ui32DataSize,
									IMG_UINT32 ui32LogicalWidth,
									IMG_UINT32 ui32LogicalHeight,
									IMG_UINT32 ui32PhysicalWidth,
									IMG_UINT32 ui32PhysicalHeight,
									PDUMP_PIXEL_FORMAT ePixFmt,
									IMG_MEMLAYOUT eMemLayout,
									IMG_FB_COMPRESSION eFBCompression,
									const IMG_UINT32 *paui32FBCClearColour,
									PDUMP_FBC_SWIZZLE eFBCSwizzle,
									IMG_DEV_VIRTADDR sHeader,
									IMG_UINT32 ui32HeaderSize,
									IMG_UINT32 ui32PDumpFlags)
{
#if !defined(SUPPORT_RGX)
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(ui32MMUContextID);
	PVR_UNREFERENCED_PARAMETER(pszSABFileName);
	PVR_UNREFERENCED_PARAMETER(sData);
	PVR_UNREFERENCED_PARAMETER(ui32DataSize);
	PVR_UNREFERENCED_PARAMETER(ui32LogicalWidth);
	PVR_UNREFERENCED_PARAMETER(ui32LogicalHeight);
	PVR_UNREFERENCED_PARAMETER(ui32PhysicalWidth);
	PVR_UNREFERENCED_PARAMETER(ui32PhysicalHeight);
	PVR_UNREFERENCED_PARAMETER(ePixFmt);
	PVR_UNREFERENCED_PARAMETER(eMemLayout);
	PVR_UNREFERENCED_PARAMETER(eFBCompression);
	PVR_UNREFERENCED_PARAMETER(paui32FBCClearColour);
	PVR_UNREFERENCED_PARAMETER(eFBCSwizzle);
	PVR_UNREFERENCED_PARAMETER(sHeader);
	PVR_UNREFERENCED_PARAMETER(ui32HeaderSize);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);

	return PVRSRV_ERROR_NOT_IMPLEMENTED;
#else
	PVRSRV_ERROR  eErr = PVRSRV_OK;
	IMG_CHAR      *pszPDumpDevName = psDeviceNode->sDevId.pszPDumpDevName;
	IMG_BYTE      abyPDumpDesc[IMAGE_HEADER_SIZE];
	IMG_UINT32    ui32ParamOutPos, ui32SABOffset = 0;
	IMG_BOOL      bRawImageData = IMG_FALSE;

	PDUMP_GET_SCRIPT_AND_FILE_STRING();

	if (pszSABFileName == NULL)
	{
		eErr = PVRSRV_ERROR_INVALID_PARAMS;
		goto error_release_script;
	}

	/* Writing image descriptor to persistent buffer is not permitted */
	if (ui32PDumpFlags & PDUMP_FLAGS_PERSISTENT)
	{
		goto error_release_script;
	}

	/* Prepare OutputImage descriptor header */
	eErr = RGXPDumpPrepareOutputImageDescriptorHdr(psDeviceNode,
									ui32HeaderSize,
									ui32DataSize,
									ui32LogicalWidth,
									ui32LogicalHeight,
									ui32PhysicalWidth,
									ui32PhysicalHeight,
									ePixFmt,
									eMemLayout,
									eFBCompression,
									paui32FBCClearColour,
									eFBCSwizzle,
									&(abyPDumpDesc[0]));
	PVR_LOG_GOTO_IF_ERROR(eErr, "RGXPDumpPrepareOutputImageDescriptorHdr", error_release_script);

	PDUMP_LOCK(ui32PDumpFlags);

	PDumpCommentWithFlagsNoLock(psDeviceNode, ui32PDumpFlags, "Dump Image descriptor");

	bRawImageData =
		 (ePixFmt == PVRSRV_PDUMP_PIXEL_FORMAT_YUV8
	   || ePixFmt == PVRSRV_PDUMP_PIXEL_FORMAT_YUV_YV12
	   || ePixFmt == PVRSRV_PDUMP_PIXEL_FORMAT_422PL12YUV8
	   || ePixFmt == PVRSRV_PDUMP_PIXEL_FORMAT_420PL12YUV8
	   || ePixFmt == PVRSRV_PDUMP_PIXEL_FORMAT_422PL12YUV10
	   || ePixFmt == PVRSRV_PDUMP_PIXEL_FORMAT_420PL12YUV10
	   || ePixFmt == PVRSRV_PDUMP_PIXEL_FORMAT_VY0UY1_8888
	   || ePixFmt == PVRSRV_PDUMP_PIXEL_FORMAT_UY0VY1_8888
	   || ePixFmt == PVRSRV_PDUMP_PIXEL_FORMAT_Y0UY1V_8888
	   || ePixFmt == PVRSRV_PDUMP_PIXEL_FORMAT_Y0VY1U_8888);

#if defined(SUPPORT_VALIDATION) && defined(SUPPORT_FBCDC_SIGNATURE_CHECK)
	{
		PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;

		/*
		 * The render data may be corrupted, so write out the raw
		 * image buffer to avoid errors in the post-processing tools.
		 */
		bRawImageData |= (psDevInfo->ui32ValidationFlags & RGX_VAL_SIG_CHECK_ERR_EN);
	}
#endif

	if (bRawImageData)
	{
		IMG_UINT32 ui32ElementType;
		IMG_UINT32 ui32ElementCount;

		PDumpCommentWithFlagsNoLock(psDeviceNode, ui32PDumpFlags,
		                            "YUV data. Switching from OutputImage to SAB. Width=0x%08X Height=0x%08X",
		                            ui32LogicalWidth, ui32LogicalHeight);

		PDUMP_UNLOCK(ui32PDumpFlags);

		PDUMP_RELEASE_SCRIPT_AND_FILE_STRING();

		ui32ElementType = 0;
		ui32ElementCount = 0;

		/* Switch to CMD:OutputData with IBIN header. */
		return PDumpDataDescriptor(psDeviceNode,
								   ui32MMUContextID,
								   pszSABFileName,
								   sData,
								   ui32DataSize,
								   IBIN_HEADER_TYPE,
								   ui32ElementType,
								   ui32ElementCount,
								   ui32PDumpFlags);
	}

	/* Write OutputImage descriptor header to parameter file */
	eErr = PDumpWriteParameter(psDeviceNode,
							   abyPDumpDesc,
							   IMAGE_HEADER_SIZE,
							   ui32PDumpFlags,
							   &ui32ParamOutPos,
							   pszFileName);
	if (eErr != PVRSRV_OK)
	{
		if (eErr != PVRSRV_ERROR_PDUMP_NOT_ALLOWED)
		{
			PDUMP_ERROR(psDeviceNode, eErr,
			            "Failed to write device allocation to parameter file");
			PVR_LOG_ERROR(eErr, "PDumpWriteParameter");
		}
		else
		{
			/*
			 * Write to parameter file prevented under the flags and
			 * current state of the driver so skip write to script and return.
			 */
			eErr = PVRSRV_OK;
		}
		goto error;
	}

	eErr = PDumpSNPrintf(hScript,
							ui32MaxLenScript,
							"MALLOC :%s:BINHEADER 0x%08X 0x%08X\n",
							pszPDumpDevName,
							IMAGE_HEADER_SIZE,
							IMAGE_HEADER_SIZE);
	PVR_GOTO_IF_ERROR(eErr, error);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

	eErr = PDumpSNPrintf(hScript,
							ui32MaxLenScript,
							"LDB :%s:BINHEADER:0x00 0x%08x 0x%08x %s\n",
							pszPDumpDevName,
							IMAGE_HEADER_SIZE,
							ui32ParamOutPos,
							pszFileName);
	PVR_GOTO_IF_ERROR(eErr, error);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

	eErr = PDumpSNPrintf(hScript,
							ui32MaxLenScript,
							"SAB :%s:BINHEADER:0x00 0x%08X 0x00000000 %s.bin\n",
							pszPDumpDevName,
							IMAGE_HEADER_SIZE,
							pszSABFileName);
	PVR_GOTO_IF_ERROR(eErr, error);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

	ui32SABOffset += IMAGE_HEADER_SIZE;

	/*
	 * Write out the header section if image is FB compressed
	 */
	if (eFBCompression != IMG_FB_COMPRESSION_NONE)
	{
		eErr = PDumpSNPrintf(hScript,
								ui32MaxLenScript,
								"SAB :%s:v%x:0x%010"IMG_UINT64_FMTSPECX" 0x%08X 0x%08X %s.bin\n",
								pszPDumpDevName,
								ui32MMUContextID,
								(IMG_UINT64)sHeader.uiAddr,
								ui32HeaderSize,
								ui32SABOffset,
								pszSABFileName);
		PVR_GOTO_IF_ERROR(eErr, error);
		PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

		ui32SABOffset += ui32HeaderSize;
	}

	/*
	 * Now dump out the actual data associated with the surface
	 */
	eErr = PDumpSNPrintf(hScript,
							ui32MaxLenScript,
							"SAB :%s:v%x:0x%010"IMG_UINT64_FMTSPECX" 0x%08X 0x%08X %s.bin\n",
							pszPDumpDevName,
							ui32MMUContextID,
							(IMG_UINT64)sData.uiAddr,
							ui32DataSize,
							ui32SABOffset,
							pszSABFileName);

	PVR_GOTO_IF_ERROR(eErr, error);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

	/*
	 * The OutputImage command is required to trigger processing of the output
	 * data
	 */
	eErr = PDumpSNPrintf(hScript,
							ui32MaxLenScript,
							"CMD:OutputImage %s.bin\n",
							pszSABFileName);
	PVR_GOTO_IF_ERROR(eErr, error);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

	eErr = PDumpSNPrintf(hScript,
							ui32MaxLenScript,
							"FREE :%s:BINHEADER\n",
							pszPDumpDevName);
	PVR_GOTO_IF_ERROR(eErr, error);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

error:
	PDUMP_UNLOCK(ui32PDumpFlags);
error_release_script:
	PDUMP_RELEASE_SCRIPT_AND_FILE_STRING()
	return eErr;
#endif
}

/*!
*******************************************************************************

 @Function	PDumpDataDescriptor

 @Description

 Dumps an OutputData command and its associated header info.

 @Input    psDeviceNode         : device
 @Input    ui32MMUContextID     : MMU context
 @Input    pszSABFileName       : filename string
 @Input    sData                : GPU virtual address of data
 @Input    ui32HeaderType       : Header type
 @Input    ui32DataSize         : Data size
 @Input    ui32ElementType      : Element type being dumped
 @Input    ui32ElementCount     : Number of elements to be dumped
 @Input    ui32PDumpFlags       : PDump flags

 @Return   PVRSRV_ERROR         :

******************************************************************************/
PVRSRV_ERROR PDumpDataDescriptor(PVRSRV_DEVICE_NODE *psDeviceNode,
									IMG_UINT32 ui32MMUContextID,
									IMG_CHAR *pszSABFileName,
									IMG_DEV_VIRTADDR sData,
									IMG_UINT32 ui32DataSize,
									IMG_UINT32 ui32HeaderType,
									IMG_UINT32 ui32ElementType,
									IMG_UINT32 ui32ElementCount,
									IMG_UINT32 ui32PDumpFlags)
{
#if !defined(SUPPORT_RGX)
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(ui32MMUContextID);
	PVR_UNREFERENCED_PARAMETER(pszSABFileName);
	PVR_UNREFERENCED_PARAMETER(sData);
	PVR_UNREFERENCED_PARAMETER(ui32DataSize);
	PVR_UNREFERENCED_PARAMETER(ui32ElementType);
	PVR_UNREFERENCED_PARAMETER(ui32ElementCount);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);

	return PVRSRV_ERROR_NOT_IMPLEMENTED;
#else
	PVRSRV_ERROR   eErr = PVRSRV_OK;
	IMG_CHAR       *pszPDumpDevName = psDeviceNode->sDevId.pszPDumpDevName;
	IMG_BYTE       abyPDumpDesc[DATA_HEADER_SIZE];
	IMG_UINT32     ui32ParamOutPos, ui32SABOffset = 0;
	IMG_UINT32     ui32HeaderSize;

	PDUMP_GET_SCRIPT_AND_FILE_STRING();

	PVR_GOTO_IF_INVALID_PARAM(pszSABFileName, eErr, error_release_script);

	if (ui32HeaderType == DATA_HEADER_TYPE)
	{
		ui32HeaderSize = DATA_HEADER_SIZE;
	}
	else if (ui32HeaderType == IBIN_HEADER_TYPE)
	{
		ui32HeaderSize = IBIN_HEADER_SIZE;
	}
	else
	{
		PVR_GOTO_WITH_ERROR(eErr, PVRSRV_ERROR_INVALID_PARAMS, error_release_script);
	}

	/* Writing data descriptor to persistent buffer is not permitted */
	if (ui32PDumpFlags & PDUMP_FLAGS_PERSISTENT)
	{
		goto error_release_script;
	}

	/* Prepare OutputData descriptor header */
	eErr = RGXPDumpPrepareOutputDataDescriptorHdr(psDeviceNode,
									ui32HeaderType,
									ui32DataSize,
									ui32ElementType,
									ui32ElementCount,
									&(abyPDumpDesc[0]));
	PVR_LOG_GOTO_IF_ERROR(eErr, "RGXPDumpPrepareOutputDataDescriptorHdr", error_release_script);

	PDUMP_LOCK(ui32PDumpFlags);

	PDumpCommentWithFlagsNoLock(psDeviceNode, ui32PDumpFlags, "Dump Data descriptor");

	/* Write OutputImage command header to parameter file */
	eErr = PDumpWriteParameter(psDeviceNode,
							   abyPDumpDesc,
							   ui32HeaderSize,
							   ui32PDumpFlags,
							   &ui32ParamOutPos,
							   pszFileName);
	if (eErr != PVRSRV_OK)
	{
		if (eErr != PVRSRV_ERROR_PDUMP_NOT_ALLOWED)
		{
			PDUMP_ERROR(psDeviceNode, eErr,
			            "Failed to write device allocation to parameter file");
			PVR_LOG_ERROR(eErr, "PDumpWriteParameter");
		}
		else
		{
			/*
			 * Write to parameter file prevented under the flags and
			 * current state of the driver so skip write to script and return.
			 */
			eErr = PVRSRV_OK;
		}
		goto error;
	}

	eErr = PDumpSNPrintf(hScript,
							ui32MaxLenScript,
							"MALLOC :%s:BINHEADER 0x%08X 0x%08X\n",
							pszPDumpDevName,
							ui32HeaderSize,
							ui32HeaderSize);
	PVR_GOTO_IF_ERROR(eErr, error);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

	eErr = PDumpSNPrintf(hScript,
							ui32MaxLenScript,
							"LDB :%s:BINHEADER:0x00 0x%08x 0x%08x %s\n",
							pszPDumpDevName,
							ui32HeaderSize,
							ui32ParamOutPos,
							pszFileName);
	PVR_GOTO_IF_ERROR(eErr, error);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

	eErr = PDumpSNPrintf(hScript,
							ui32MaxLenScript,
							"SAB :%s:BINHEADER:0x00 0x%08X 0x00000000 %s.bin\n",
							pszPDumpDevName,
							ui32HeaderSize,
							pszSABFileName);
	PVR_GOTO_IF_ERROR(eErr, error);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

	ui32SABOffset += ui32HeaderSize;

	/*
	 * Now dump out the actual data associated
	 */
	eErr = PDumpSNPrintf(hScript,
							ui32MaxLenScript,
							"SAB :%s:v%x:0x%010"IMG_UINT64_FMTSPECX" 0x%08X 0x%08X %s.bin\n",
							pszPDumpDevName,
							ui32MMUContextID,
							(IMG_UINT64)sData.uiAddr,
							ui32DataSize,
							ui32SABOffset,
							pszSABFileName);

	PVR_GOTO_IF_ERROR(eErr, error);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

	/*
	 * The OutputData command is required to trigger processing of the output
	 * data
	 */
	eErr = PDumpSNPrintf(hScript,
							ui32MaxLenScript,
							"CMD:OutputData %s.bin\n",
							pszSABFileName);
	PVR_GOTO_IF_ERROR(eErr, error);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

	eErr = PDumpSNPrintf(hScript,
							ui32MaxLenScript,
							"FREE :%s:BINHEADER\n",
							pszPDumpDevName);
	PVR_GOTO_IF_ERROR(eErr, error);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);

error:
	PDUMP_UNLOCK(ui32PDumpFlags);
error_release_script:
	PDUMP_RELEASE_SCRIPT_AND_FILE_STRING()
	return eErr;
#endif
}

/*!
*******************************************************************************

 @Function	PDumpReadRegKM

 @Description

 Dumps a read from a device register to a file

 @Input    psConnection			: connection info
 @Input    pszFileName
 @Input    ui32FileOffset
 @Input    ui32Address
 @Input    ui32Size
 @Input    ui32PDumpFlags

 @Return   PVRSRV_ERROR			:

******************************************************************************/
PVRSRV_ERROR PDumpReadRegKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                            IMG_CHAR *pszPDumpRegName,
                            IMG_CHAR *pszFileName,
                            IMG_UINT32 ui32FileOffset,
                            IMG_UINT32 ui32Address,
                            IMG_UINT32 ui32Size,
                            IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	PVR_UNREFERENCED_PARAMETER(ui32Size);

	eErr = PDumpSNPrintf(hScript,
			ui32MaxLen,
			"SAB :%s:0x%08X 0x%08X %s",
			pszPDumpRegName,
			ui32Address,
			ui32FileOffset,
			pszFileName);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);
	PDUMP_UNLOCK(ui32PDumpFlags);

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpRegRead32ToInternalVar
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which reads register into an
 *                  internal variable
******************************************************************************/
PVRSRV_ERROR PDumpRegRead32ToInternalVar(PVRSRV_DEVICE_NODE *psDeviceNode,
							IMG_CHAR *pszPDumpRegName,
							IMG_UINT32 ui32Reg,
							IMG_CHAR *pszInternalVar,
							IMG_UINT32 ui32Flags)

{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()

	eErr = PDumpSNPrintf(hScript,
							ui32MaxLen,
							"RDW %s :%s:0x%08X",
							pszInternalVar,
							pszPDumpRegName,
							ui32Reg);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}

/******************************************************************************
 @name		PDumpRegRead32
 @brief		Dump 32-bit register read to script
 @param		pszPDumpDevName - pdump device name
 @param		ui32RegOffset - register offset
 @param		ui32Flags - pdump flags
 @return	Error
******************************************************************************/
PVRSRV_ERROR PDumpRegRead32(PVRSRV_DEVICE_NODE *psDeviceNode,
							IMG_CHAR *pszPDumpRegName,
							const IMG_UINT32 ui32RegOffset,
							IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "RDW :%s:0x%X",
							pszPDumpRegName,
							ui32RegOffset);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}

/******************************************************************************
 @name      PDumpRegRead64ToInternalVar
 @brief     Read 64-bit register into an internal variable
 @param     pszPDumpDevName - pdump device name
 @param     ui32RegOffset - register offset
 @param     ui32Flags - pdump flags
 @return    Error
******************************************************************************/
PVRSRV_ERROR PDumpRegRead64ToInternalVar(PVRSRV_DEVICE_NODE *psDeviceNode,
                            IMG_CHAR *pszPDumpRegName,
                            IMG_CHAR *pszInternalVar,
                            const IMG_UINT32 ui32RegOffset,
                            IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	IMG_CHAR *pszPDumpVarName;
#endif
	PDUMP_GET_SCRIPT_STRING();

#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "RDW %s :%s:0x%X",
	                     pszInternalVar,
	                     pszPDumpRegName,
	                     ui32RegOffset);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);

	pszPDumpVarName = PDumpCreateIncVarNameStr(pszInternalVar);
	if (pszPDumpVarName == NULL)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "RDW %s :%s:0x%X",
	                        pszPDumpVarName,
	                        pszPDumpRegName,
	                        ui32RegOffset + 4);

	PDumpFreeIncVarNameStr(pszPDumpVarName);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		PDUMP_UNLOCK(ui32Flags);
		return eErr;
	}

	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

#else
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "RDW64 %s :%s:0x%X",
	                        pszInternalVar,
	                        pszPDumpRegName,
	                        ui32RegOffset);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);
#endif

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}


/******************************************************************************
 @name		PDumpRegRead64
 @brief		Dump 64-bit register read to script
 @param		pszPDumpDevName - pdump device name
 @param		ui32RegOffset - register offset
 @param		ui32Flags - pdump flags
 @return	Error
******************************************************************************/
PVRSRV_ERROR PDumpRegRead64(PVRSRV_DEVICE_NODE *psDeviceNode,
							IMG_CHAR *pszPDumpRegName,
							const IMG_UINT32 ui32RegOffset,
							IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

#if defined(PDUMP_SPLIT_64BIT_REGISTER_ACCESS)
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "RDW :%s:0x%X",
							pszPDumpRegName, ui32RegOffset);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		return eErr;
	}
	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "RDW :%s:0x%X",
							pszPDumpRegName, ui32RegOffset + 4);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING()
		PDUMP_UNLOCK(ui32Flags);
		return eErr;
	}
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);
#else
	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "RDW64 :%s:0x%X",
							pszPDumpRegName,
							ui32RegOffset);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);
#endif

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}


/******************************************************************************
 FUNCTION	: PDumpWriteShiftedMaskedValue

 PURPOSE	: Emits the PDump commands for writing a masked shifted address
              into another location

 PARAMETERS	: PDump symbolic name and offset of target word
              PDump symbolic name and offset of source address
              right shift amount
              left shift amount
              mask

 RETURNS	: None
******************************************************************************/
PVRSRV_ERROR
PDumpWriteShiftedMaskedValue(PVRSRV_DEVICE_NODE *psDeviceNode,
                             const IMG_CHAR *pszDestRegspaceName,
                             const IMG_CHAR *pszDestSymbolicName,
                             IMG_DEVMEM_OFFSET_T uiDestOffset,
                             const IMG_CHAR *pszRefRegspaceName,
                             const IMG_CHAR *pszRefSymbolicName,
                             IMG_DEVMEM_OFFSET_T uiRefOffset,
                             IMG_UINT32 uiSHRAmount,
                             IMG_UINT32 uiSHLAmount,
                             IMG_UINT32 uiMask,
                             IMG_DEVMEM_SIZE_T uiWordSize,
                             IMG_UINT32 uiPDumpFlags)
{
	PVRSRV_ERROR         eError;

	/* Suffix of WRW command in PDump (i.e. WRW or WRW64) */
	const IMG_CHAR       *pszWrwSuffix;

	/* Internal PDump register used for interim calculation */
	const IMG_CHAR       *pszPDumpIntRegSpace;
	IMG_UINT32           uiPDumpIntRegNum;

	PDUMP_GET_SCRIPT_STRING();

	if ((uiWordSize != 4) && (uiWordSize != 8))
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return PVRSRV_ERROR_NOT_SUPPORTED;
	}

	pszWrwSuffix = (uiWordSize == 8) ? "64" : "";

	/* Should really "Acquire" a pdump register here */
	pszPDumpIntRegSpace = pszDestRegspaceName;
	uiPDumpIntRegNum = 1;

	PDUMP_LOCK(uiPDumpFlags);
	eError = PDumpSNPrintf(hScript,
	               ui32MaxLen,
	               /* Should this be "MOV" instead? */
	               "WRW :%s:$%d :%s:%s:" IMG_DEVMEM_OFFSET_FMTSPEC "\n",
	               /* dest */
	               pszPDumpIntRegSpace,
	               uiPDumpIntRegNum,
	               /* src */
	               pszRefRegspaceName,
	               pszRefSymbolicName,
	               uiRefOffset);
	PVR_GOTO_IF_ERROR(eError, ErrUnlock);

	PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);

	if (uiSHRAmount > 0)
	{
		eError = PDumpSNPrintf(hScript,
		               ui32MaxLen,
		               "SHR :%s:$%d :%s:$%d 0x%X\n",
		               /* dest */
		               pszPDumpIntRegSpace,
		               uiPDumpIntRegNum,
		               /* src A */
		               pszPDumpIntRegSpace,
		               uiPDumpIntRegNum,
		               /* src B */
		               uiSHRAmount);
		PVR_GOTO_IF_ERROR(eError, ErrUnlock);
		PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);
	}

	if (uiSHLAmount > 0)
	{
		eError = PDumpSNPrintf(hScript,
		               ui32MaxLen,
		               "SHL :%s:$%d :%s:$%d 0x%X\n",
		               /* dest */
		               pszPDumpIntRegSpace,
		               uiPDumpIntRegNum,
		               /* src A */
		               pszPDumpIntRegSpace,
		               uiPDumpIntRegNum,
		               /* src B */
		               uiSHLAmount);
		PVR_GOTO_IF_ERROR(eError, ErrUnlock);
		PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);
	}

	if (uiMask != (1ULL << (8*uiWordSize))-1)
	{
		eError = PDumpSNPrintf(hScript,
		               ui32MaxLen,
		               "AND :%s:$%d :%s:$%d 0x%X\n",
		               /* dest */
		               pszPDumpIntRegSpace,
		               uiPDumpIntRegNum,
		               /* src A */
		               pszPDumpIntRegSpace,
		               uiPDumpIntRegNum,
		               /* src B */
		               uiMask);
		PVR_GOTO_IF_ERROR(eError, ErrUnlock);
		PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);
	}

	eError = PDumpSNPrintf(hScript,
	               ui32MaxLen,
	               "WRW%s :%s:%s:" IMG_DEVMEM_OFFSET_FMTSPEC " :%s:$%d\n",
	               pszWrwSuffix,
	               /* dest */
	               pszDestRegspaceName,
	               pszDestSymbolicName,
	               uiDestOffset,
	               /* src */
	               pszPDumpIntRegSpace,
	               uiPDumpIntRegNum);
	PVR_GOTO_IF_ERROR(eError, ErrUnlock);
	PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);

ErrUnlock:
	PDUMP_UNLOCK(uiPDumpFlags);
	PDUMP_RELEASE_SCRIPT_STRING();

	return eError;
}


PVRSRV_ERROR
PDumpWriteSymbAddress(PVRSRV_DEVICE_NODE *psDeviceNode,
                      const IMG_CHAR *pszDestSpaceName,
                      IMG_DEVMEM_OFFSET_T uiDestOffset,
                      const IMG_CHAR *pszRefSymbolicName,
                      IMG_DEVMEM_OFFSET_T uiRefOffset,
                      const IMG_CHAR *pszPDumpDevName,
                      IMG_UINT32 ui32WordSize,
                      IMG_UINT32 ui32AlignShift,
                      IMG_UINT32 ui32Shift,
                      IMG_UINT32 uiPDumpFlags)
{
	const IMG_CHAR       *pszWrwSuffix = "";
	PVRSRV_ERROR         eError = PVRSRV_OK;

	PDUMP_GET_SCRIPT_STRING();

	if (ui32WordSize == 8)
	{
		pszWrwSuffix = "64";
	}

	PDUMP_LOCK(uiPDumpFlags);

	if (ui32AlignShift != ui32Shift)
	{
		/* Write physical address into a variable */
		eError = PDumpSNPrintf(hScript,
				       ui32MaxLen,
				       "WRW%s :%s:$1 %s:" IMG_DEVMEM_OFFSET_FMTSPEC "\n",
				       pszWrwSuffix,
				       /* dest */
				       pszPDumpDevName,
				       /* src */
				       pszRefSymbolicName,
				       uiRefOffset);
		PVR_GOTO_IF_ERROR(eError, symbAddress_error);
		PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);

		/* apply address alignment */
		eError = PDumpSNPrintf(hScript,
				       ui32MaxLen,
				       "SHR :%s:$1 :%s:$1 0x%X",
				       /* dest */
				       pszPDumpDevName,
				       /* src A */
				       pszPDumpDevName,
				       /* src B */
				       ui32AlignShift);
		PVR_GOTO_IF_ERROR(eError, symbAddress_error);
		PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);

		/* apply address shift */
		eError = PDumpSNPrintf(hScript,
				       ui32MaxLen,
				       "SHL :%s:$1 :%s:$1 0x%X",
				       /* dest */
				       pszPDumpDevName,
				       /* src A */
				       pszPDumpDevName,
				       /* src B */
				       ui32Shift);
		PVR_GOTO_IF_ERROR(eError, symbAddress_error);
		PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);


		/* write result to register */
		eError = PDumpSNPrintf(hScript,
				       ui32MaxLen,
				       "WRW%s :%s:0x%08X :%s:$1",
				       pszWrwSuffix,
				       pszDestSpaceName,
				       (IMG_UINT32)uiDestOffset,
				       pszPDumpDevName);
		PVR_GOTO_IF_ERROR(eError, symbAddress_error);
		PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);
	}
	else
	{
		eError = PDumpSNPrintf(hScript,
				       ui32MaxLen,
				       "WRW%s :%s:" IMG_DEVMEM_OFFSET_FMTSPEC " %s:" IMG_DEVMEM_OFFSET_FMTSPEC "\n",
				       pszWrwSuffix,
				       /* dest */
				       pszDestSpaceName,
				       uiDestOffset,
				       /* src */
				       pszRefSymbolicName,
				       uiRefOffset);
		PVR_GOTO_IF_ERROR(eError, symbAddress_error);
		PDumpWriteScript(psDeviceNode, hScript, uiPDumpFlags);
	}

symbAddress_error:
	PDUMP_UNLOCK(uiPDumpFlags);
	PDUMP_RELEASE_SCRIPT_STRING();

	return eError;
}

/******************************************************************************
 * Function Name  : PDumpIDLWithFlags
 * Inputs         : Idle time in clocks
 * Outputs        : None
 * Returns        : Error
 * Description    : Dump IDL command to script
******************************************************************************/
PVRSRV_ERROR PDumpIDLWithFlags(PVRSRV_DEVICE_NODE *psDeviceNode,
                               IMG_UINT32 ui32Clocks, IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "IDL %u", ui32Clocks);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}


/******************************************************************************
 * Function Name  : PDumpIDL
 * Inputs         : Idle time in clocks
 * Outputs        : None
 * Returns        : Error
 * Description    : Dump IDL command to script
******************************************************************************/
PVRSRV_ERROR PDumpIDL(PVRSRV_DEVICE_NODE *psDeviceNode,
                      IMG_UINT32 ui32Clocks)
{
	return PDumpIDLWithFlags(psDeviceNode, ui32Clocks, PDUMP_FLAGS_CONTINUOUS);
}

/******************************************************************************
 * Function Name  : PDumpRegBasedCBP
 * Inputs         : pszPDumpRegName, ui32RegOffset, ui32WPosVal, ui32PacketSize
 *                  ui32BufferSize, ui32Flags
 * Outputs        : None
 * Returns        : Error
 * Description    : Dump CBP command to script
******************************************************************************/
PVRSRV_ERROR PDumpRegBasedCBP(PVRSRV_DEVICE_NODE *psDeviceNode,
							  IMG_CHAR		*pszPDumpRegName,
							  IMG_UINT32	ui32RegOffset,
							  IMG_UINT32	ui32WPosVal,
							  IMG_UINT32	ui32PacketSize,
							  IMG_UINT32	ui32BufferSize,
							  IMG_UINT32	ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	eErr = PDumpSNPrintf(hScript,
			 ui32MaxLen,
			 "CBP :%s:0x%08X 0x%08X 0x%08X 0x%08X",
			 pszPDumpRegName,
			 ui32RegOffset,
			 ui32WPosVal,
			 ui32PacketSize,
			 ui32BufferSize);
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpTRG(PVRSRV_DEVICE_NODE *psDeviceNode,
                      IMG_CHAR *pszMemSpace,
                      IMG_UINT32 ui32MMUCtxID,
                      IMG_UINT32 ui32RegionID,
                      IMG_BOOL bEnable,
                      IMG_UINT64 ui64VAddr,
                      IMG_UINT64 ui64LenBytes,
                      IMG_UINT32 ui32XStride,
                      IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	if (bEnable)
	{
		eErr = PDumpSNPrintf(hScript, ui32MaxLen,
				     "TRG :%s:v%u %u 0x%08"IMG_UINT64_FMTSPECX" 0x%08"IMG_UINT64_FMTSPECX" %u",
				     pszMemSpace, ui32MMUCtxID, ui32RegionID,
				     ui64VAddr, ui64LenBytes, ui32XStride);
	}
	else
	{
		eErr = PDumpSNPrintf(hScript, ui32MaxLen,
				     "TRG :%s:v%u %u",
				     pszMemSpace, ui32MMUCtxID, ui32RegionID);

	}
	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32Flags);
	PDumpWriteScript(psDeviceNode, hScript, ui32Flags);
	PDUMP_UNLOCK(ui32Flags);

	PDUMP_RELEASE_SCRIPT_STRING();

	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpConnectionNotify
 * Description    : Called by the srvcore to tell PDump core that the
 *                  PDump capture and control client has connected
******************************************************************************/
void PDumpConnectionNotify(PVRSRV_DEVICE_NODE *psDeviceNode)
{
#if defined(TL_BUFFER_STATS)
	PVRSRV_ERROR		eErr;
#endif

	OSAtomicIncrement(&g_sConnectionCount);

	/* Reset the parameter file attributes */
	g_PDumpParameters.sWOff.ui32Main = g_PDumpParameters.sWOff.ui32Init;
	g_PDumpParameters.ui32FileIdx = 0;

	/* Reset the script file attributes */
	g_PDumpScript.ui32FileIdx = 0;

	/* The Main script & parameter buffers should be empty after the previous
	 * PDump capture if it completed correctly.
	 * When PDump client is not connected, writes are prevented to Main
	 * buffers in PDumpWriteAllowed() since no capture range, no client,
	 * no writes to Main buffers for continuous flagged and regular writes.
	 */
	if (!TLStreamOutOfData(g_PDumpParameters.sCh.sMainStream.hTL)) /* !empty */
	{
		PVR_DPF((PVR_DBG_ERROR, "PDump Main parameter buffer not empty, capture will be corrupt!"));
	}
	if (!TLStreamOutOfData(g_PDumpScript.sCh.sMainStream.hTL)) /* !empty */
	{
		PVR_DPF((PVR_DBG_ERROR, "PDump Main script buffer not empty, capture will be corrupt!"));
	}

#if defined(TL_BUFFER_STATS)
	eErr = TLStreamResetProducerByteCount(g_PDumpParameters.sCh.sMainStream.hTL, g_PDumpParameters.sWOff.ui32Init);
	PVR_LOG_IF_ERROR(eErr, "TLStreamResetByteCount Parameter Main");

	eErr = TLStreamResetProducerByteCount(g_PDumpScript.sCh.sMainStream.hTL, 0);
	PVR_LOG_IF_ERROR(eErr, "TLStreamResetByteCount Script Main");
#endif

	if (psDeviceNode->pfnPDumpInitDevice)
	{
		/* Reset pdump according to connected device */
		psDeviceNode->pfnPDumpInitDevice(psDeviceNode);
	}
}

/******************************************************************************
 * Function Name  : PDumpDisconnectionNotify
 * Description    : Called by the connection_server to tell PDump core that
 *                  the PDump capture and control client has disconnected
******************************************************************************/
void PDumpDisconnectionNotify(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eErr;

	if (PDumpCtrlCaptureOn())
	{
		PVR_LOG(("pdump killed, capture files may be invalid or incomplete!"));

		/* Disable capture in server, in case PDump client was killed and did
		 * not get a chance to reset the capture parameters.
		 * Will set module state back to READY.
		 */
		eErr = PDumpSetDefaultCaptureParamsKM(NULL, psDeviceNode, PDUMP_CAPMODE_UNSET,
						      PDUMP_FRAME_UNSET, PDUMP_FRAME_UNSET, 0, 0);
		PVR_LOG_IF_ERROR(eErr, "PDumpSetDefaultCaptureParamsKM");
	}
}

/******************************************************************************
 * Function Name  : PDumpRegCondStr
 * Inputs         : Description of what this register read is trying to do
 *					pszPDumpDevName
 *					Register offset
 *					expected value
 *					mask for that value
 * Outputs        : PDump conditional test for use with 'IF' and 'DOW'
 * Returns        : None
 * Description    : Create a PDUMP conditional test. The string is allocated
 *					on the heap and should be freed by the caller on success.
******************************************************************************/
PVRSRV_ERROR PDumpRegCondStr(IMG_CHAR            **ppszPDumpCond,
                             IMG_CHAR            *pszPDumpRegName,
                             IMG_UINT32          ui32RegAddr,
                             IMG_UINT32          ui32RegValue,
                             IMG_UINT32          ui32Mask,
                             IMG_UINT32          ui32Flags,
                             PDUMP_POLL_OPERATOR eOperator)
{
	IMG_UINT32	ui32PollCount;

	PDUMP_GET_MSG_STRING();

	ui32PollCount = POLL_COUNT_SHORT;

	if (0 == OSSNPrintf(pszMsg, ui32MaxLen, ":%s:0x%08X 0x%08X 0x%08X %d %u %d",
						pszPDumpRegName, ui32RegAddr, ui32RegValue,
						ui32Mask, eOperator, ui32PollCount, POLL_DELAY))
	{
		PDUMP_RELEASE_MSG_STRING()
		return PVRSRV_ERROR_INTERNAL_ERROR;
	}

	*ppszPDumpCond = pszMsg;

	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpInternalValCondStr
 * Inputs         : Description of what this register read is trying to do
 *					pszPDumpDevName
 *					Internal variable
 *					expected value
 *					mask for that value
 * Outputs        : PDump conditional test for use with 'IF' and 'DOW'
 * Returns        : None
 * Description    : Create a PDUMP conditional test. The string is allocated
 *					on the heap and should be freed by the caller on success.
******************************************************************************/
PVRSRV_ERROR PDumpInternalValCondStr(IMG_CHAR            **ppszPDumpCond,
                                     IMG_CHAR            *pszInternalVar,
                                     IMG_UINT32          ui32RegValue,
                                     IMG_UINT32          ui32Mask,
                                     IMG_UINT32          ui32Flags,
                                     PDUMP_POLL_OPERATOR eOperator)
{
	IMG_UINT32	ui32PollCount;

	PDUMP_GET_MSG_STRING();

	ui32PollCount = POLL_COUNT_SHORT;

	if (0 == OSSNPrintf(pszMsg, ui32MaxLen, "%s 0x%08X 0x%08X %d %u %d",
						pszInternalVar, ui32RegValue,
						ui32Mask, eOperator, ui32PollCount, POLL_DELAY))
	{
		PDUMP_RELEASE_MSG_STRING()
		return PVRSRV_ERROR_INTERNAL_ERROR;
	}

	*ppszPDumpCond = pszMsg;

	return PVRSRV_OK;
}


/******************************************************************************
 * Function Name  : PDumpIfKM
 * Inputs         : pszPDumpCond - string for condition
 * Outputs        : None
 * Returns        : None
 * Description    : Create a PDUMP string which represents IF command
					with condition.
******************************************************************************/
PVRSRV_ERROR PDumpIfKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                       IMG_CHAR *pszPDumpCond, IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "IF %s\n", pszPDumpCond);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);
	PDUMP_UNLOCK(ui32PDumpFlags);

	PDUMP_RELEASE_SCRIPT_STRING();
	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpElseKM
 * Inputs         : pszPDumpCond - string for condition
 * Outputs        : None
 * Returns        : None
 * Description    : Create a PDUMP string which represents ELSE command
					with condition.
******************************************************************************/
PVRSRV_ERROR PDumpElseKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                         IMG_CHAR *pszPDumpCond, IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "ELSE %s\n", pszPDumpCond);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);
	PDUMP_UNLOCK(ui32PDumpFlags);

	PDUMP_RELEASE_SCRIPT_STRING();

	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpFiKM
 * Inputs         : pszPDumpCond - string for condition
 * Outputs        : None
 * Returns        : None
 * Description    : Create a PDUMP string which represents FI command
					with condition.
******************************************************************************/
PVRSRV_ERROR PDumpFiKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                       IMG_CHAR *pszPDumpCond, IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "FI %s\n", pszPDumpCond);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);
	PDUMP_UNLOCK(ui32PDumpFlags);

	PDUMP_RELEASE_SCRIPT_STRING();

	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpStartDoLoopKM
 * Inputs         : None
 * Outputs        : None
 * Returns        : None
 * Description    : Create a PDUMP string which represents SDO command
					with condition.
******************************************************************************/
PVRSRV_ERROR PDumpStartDoLoopKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                                IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "SDO");

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);
	PDUMP_UNLOCK(ui32PDumpFlags);

	PDUMP_RELEASE_SCRIPT_STRING();

	return PVRSRV_OK;
}

/******************************************************************************
 * Function Name  : PDumpEndDoWhileLoopKM
 * Inputs         : pszPDumpWhileCond - string for loop condition
 * Outputs        : None
 * Returns        : None
 * Description    : Create a PDUMP string which represents DOW command
					with condition.
******************************************************************************/
PVRSRV_ERROR PDumpEndDoWhileLoopKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                                   IMG_CHAR *pszPDumpWhileCond,
                                   IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()

	eErr = PDumpSNPrintf(hScript, ui32MaxLen, "DOW %s\n", pszPDumpWhileCond);

	if (eErr != PVRSRV_OK)
	{
		PDUMP_RELEASE_SCRIPT_STRING();
		return eErr;
	}

	PDUMP_LOCK(ui32PDumpFlags);
	PDumpWriteScript(psDeviceNode, hScript, ui32PDumpFlags);
	PDUMP_UNLOCK(ui32PDumpFlags);

	PDUMP_RELEASE_SCRIPT_STRING();

	return PVRSRV_OK;
}


void PDumpLock(void)
{
	OSLockAcquire(g_hPDumpWriteLock);
}
void PDumpUnlock(void)
{
	OSLockRelease(g_hPDumpWriteLock);
}
static void PDumpAssertWriteLockHeld(void)
{
	/* It is expected to be g_hPDumpWriteLock is locked at this point. */
	PVR_ASSERT(OSLockIsLocked(g_hPDumpWriteLock));
}

#if defined(PDUMP_TRACE_STATE) || defined(PVR_TESTING_UTILS)
void PDumpCommonDumpState(void)
{
	PVR_LOG(("--- PDUMP COMMON: g_PDumpScript.sCh.*.hTL (In, Mn, De, Bk) ( %p, %p, %p, %p )",
			g_PDumpScript.sCh.sInitStream.hTL, g_PDumpScript.sCh.sMainStream.hTL, g_PDumpScript.sCh.sDeinitStream.hTL, g_PDumpScript.sCh.sBlockStream.hTL));
	PVR_LOG(("--- PDUMP COMMON: g_PDumpScript.sCh.*.ui32BufferFullRetries (In, Mn, De, Bk) ( %5d, %5d, %5d, %5d )",
			g_PDumpScript.sCh.sInitStream.ui32BufferFullRetries,
			g_PDumpScript.sCh.sMainStream.ui32BufferFullRetries,
			g_PDumpScript.sCh.sDeinitStream.ui32BufferFullRetries,
			g_PDumpScript.sCh.sBlockStream.ui32BufferFullRetries));
	PVR_LOG(("--- PDUMP COMMON: g_PDumpScript.sCh.*.ui32BufferFullAborts (In, Mn, De, Bk)  ( %5d, %5d, %5d, %5d )",
				g_PDumpScript.sCh.sInitStream.ui32BufferFullAborts,
				g_PDumpScript.sCh.sMainStream.ui32BufferFullAborts,
				g_PDumpScript.sCh.sDeinitStream.ui32BufferFullAborts,
				g_PDumpScript.sCh.sBlockStream.ui32BufferFullAborts));

	PVR_LOG(("--- PDUMP COMMON: g_PDumpScript.sCh.*.ui32HighestRetriesWatermark (In, Mn, De, Bk)  ( %5d, %5d, %5d, %5d )",
				g_PDumpScript.sCh.sInitStream.ui32HighestRetriesWatermark,
				g_PDumpScript.sCh.sMainStream.ui32HighestRetriesWatermark,
				g_PDumpScript.sCh.sDeinitStream.ui32HighestRetriesWatermark,
			    g_PDumpScript.sCh.sBlockStream.ui32HighestRetriesWatermark));
	PVR_LOG(("--- PDUMP COMMON: g_PDumpScript.ui32FileIdx( %d )", g_PDumpScript.ui32FileIdx));



	PVR_LOG(("--- PDUMP COMMON: g_PDumpParameters.sCh.*.hTL (In, Mn, De, Bk) ( %p, %p, %p, %p )",
			g_PDumpParameters.sCh.sInitStream.hTL, g_PDumpParameters.sCh.sMainStream.hTL, g_PDumpParameters.sCh.sDeinitStream.hTL, g_PDumpParameters.sCh.sBlockStream.hTL));
	PVR_LOG(("--- PDUMP COMMON: g_PDumpParameters.sCh.*.ui32BufferFullRetries (In, Mn, De, Bk) ( %5d, %5d, %5d, %5d )",
			g_PDumpParameters.sCh.sInitStream.ui32BufferFullRetries,
			g_PDumpParameters.sCh.sMainStream.ui32BufferFullRetries,
			g_PDumpParameters.sCh.sDeinitStream.ui32BufferFullRetries,
			g_PDumpParameters.sCh.sBlockStream.ui32BufferFullRetries));
	PVR_LOG(("--- PDUMP COMMON: g_PDumpParameters.sCh.*.ui32BufferFullAborts (In, Mn, De, Bk)  ( %5d, %5d, %5d, %5d )",
			g_PDumpParameters.sCh.sInitStream.ui32BufferFullAborts,
			g_PDumpParameters.sCh.sMainStream.ui32BufferFullAborts,
			g_PDumpParameters.sCh.sDeinitStream.ui32BufferFullAborts,
			g_PDumpParameters.sCh.sBlockStream.ui32BufferFullAborts));
	PVR_LOG(("--- PDUMP COMMON: g_PDumpParameters.sCh.*.ui32HighestRetriesWatermark (In, Mn, De, Bk)  ( %5d, %5d, %5d, %5d )",
				g_PDumpParameters.sCh.sInitStream.ui32HighestRetriesWatermark,
				g_PDumpParameters.sCh.sMainStream.ui32HighestRetriesWatermark,
				g_PDumpParameters.sCh.sDeinitStream.ui32HighestRetriesWatermark,
				g_PDumpParameters.sCh.sBlockStream.ui32HighestRetriesWatermark));


	PVR_LOG(("--- PDUMP COMMON: g_PDumpParameters.sWOff.* (In, Mn, De, Bk) ( %d, %d, %d, %d )",
			g_PDumpParameters.sWOff.ui32Init, g_PDumpParameters.sWOff.ui32Main, g_PDumpParameters.sWOff.ui32Deinit, g_PDumpParameters.sWOff.ui32Block));
	PVR_LOG(("--- PDUMP COMMON: g_PDumpParameters.ui32FileIdx( %d )", g_PDumpParameters.ui32FileIdx));

	PVR_LOG(("--- PDUMP COMMON: g_PDumpCtrl( %p ) eServiceState( %d ), IsDriverInInitPhase( %s ) ui32Flags( %x )",
			&g_PDumpCtrl, g_PDumpCtrl.eServiceState, CHECK_PDUMP_CONTROL_FLAG(FLAG_IS_DRIVER_IN_INIT_PHASE) ? "yes" : "no", g_PDumpCtrl.ui32Flags));
	PVR_LOG(("--- PDUMP COMMON: ui32DefaultCapMode( %d ) ui32CurrentFrame( %d )",
			g_PDumpCtrl.ui32DefaultCapMode, g_PDumpCtrl.ui32CurrentFrame));
	PVR_LOG(("--- PDUMP COMMON: sCaptureRange.ui32Start( %x ) sCaptureRange.ui32End( %x ) sCaptureRange.ui32Interval( %u )",
			g_PDumpCtrl.sCaptureRange.ui32Start, g_PDumpCtrl.sCaptureRange.ui32End, g_PDumpCtrl.sCaptureRange.ui32Interval));
	PVR_LOG(("--- PDUMP COMMON: IsInCaptureRange( %s ) IsInCaptureInterval( %s ) InPowerTransition( %d )",
			CHECK_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_RANGE) ? "yes" : "no",
			CHECK_PDUMP_CONTROL_FLAG(FLAG_IS_IN_CAPTURE_INTERVAL) ? "yes" : "no",
			PDumpCtrlInPowerTransition()));
	PVR_LOG(("--- PDUMP COMMON: sBlockCtrl.ui32BlockLength( %d ), sBlockCtrl.ui32CurrentBlock( %d )",
			g_PDumpCtrl.sBlockCtrl.ui32BlockLength, g_PDumpCtrl.sBlockCtrl.ui32CurrentBlock));
}
#endif /* defined(PDUMP_TRACE_STATE) || defined(PVR_TESTING_UTILS) */


PVRSRV_ERROR PDumpRegisterConnection(void *hSyncPrivData,
                                     PFN_PDUMP_SYNCBLOCKS pfnPDumpSyncBlocks,
                                     PDUMP_CONNECTION_DATA **ppsPDumpConnectionData)
{
	PDUMP_CONNECTION_DATA *psPDumpConnectionData;
	PVRSRV_ERROR eError;

	PVR_ASSERT(ppsPDumpConnectionData != NULL);
	PVR_ASSERT(pfnPDumpSyncBlocks != NULL);
	PVR_ASSERT(hSyncPrivData != NULL);

	psPDumpConnectionData = OSAllocMem(sizeof(*psPDumpConnectionData));
	PVR_GOTO_IF_NOMEM(psPDumpConnectionData, eError, fail_alloc);

	eError = OSLockCreate(&psPDumpConnectionData->hLock);
	PVR_GOTO_IF_ERROR(eError, fail_lockcreate);

	dllist_init(&psPDumpConnectionData->sListHead);
	OSAtomicWrite(&psPDumpConnectionData->sRefCount, 1);
	psPDumpConnectionData->ui32LastSetFrameNumber = PDUMP_FRAME_UNSET;
	psPDumpConnectionData->eLastEvent = PDUMP_TRANSITION_EVENT_NONE;
	psPDumpConnectionData->eFailedEvent = PDUMP_TRANSITION_EVENT_NONE;

	/*
	 * Although we don't take a ref count here, handle base destruction
	 * will ensure that any resource that might trigger us to do a Transition
	 * will have been freed before the sync blocks which are keeping the sync
	 * connection data alive.
	 */
	psPDumpConnectionData->hSyncPrivData = hSyncPrivData;
	psPDumpConnectionData->pfnPDumpSyncBlocks = pfnPDumpSyncBlocks;

	*ppsPDumpConnectionData = psPDumpConnectionData;

	return PVRSRV_OK;

fail_lockcreate:
	OSFreeMem(psPDumpConnectionData);
fail_alloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

void PDumpUnregisterConnection(PDUMP_CONNECTION_DATA *psPDumpConnectionData)
{
	_PDumpConnectionRelease(psPDumpConnectionData);
}


/*!
 * \name	PDumpSNPrintf
 */
PVRSRV_ERROR PDumpSNPrintf(IMG_HANDLE hBuf, IMG_UINT32 ui32ScriptSizeMax, IMG_CHAR* pszFormat, ...)
{
	IMG_CHAR* pszBuf = hBuf;
	IMG_INT32 n;
	va_list	vaArgs;

	va_start(vaArgs, pszFormat);

	n = OSVSNPrintf(pszBuf, ui32ScriptSizeMax, pszFormat, vaArgs);

	va_end(vaArgs);

	if (n>=(IMG_INT32)ui32ScriptSizeMax || n==-1)	/* glibc >= 2.1 or glibc 2.0 */
	{
		PVR_DPF((PVR_DBG_ERROR, "Buffer overflow detected, pdump output may be incomplete."));

		return PVRSRV_ERROR_PDUMP_BUF_OVERFLOW;
	}

#if defined(PDUMP_DEBUG_OUTFILES)
	OSAtomicIncrement(&g_sEveryLineCounter);
#endif

	/* Put line ending sequence at the end if it isn't already there */
	_PDumpVerifyLineEnding(pszBuf, ui32ScriptSizeMax);

	return PVRSRV_OK;
}

#endif /* defined(PDUMP) */
