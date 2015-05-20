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
#include <stdarg.h>

#include "pvrversion.h"
#include "allocmem.h"
#include "osfunc.h"
#include "pvrsrv.h"
#include "pvr_debug.h"
#include "srvkm.h"
#include "pdump_physmem.h"
#include "hash.h"
#include "connection_server.h"
#include "sync_server.h"

/* pdump headers */
#include "dbgdrvif_srv5.h"
#include "pdump_osfunc.h"
#include "pdump_km.h"

/* Allow temporary buffer size override */
#if !defined(PDUMP_TEMP_BUFFER_SIZE)
#define PDUMP_TEMP_BUFFER_SIZE (64 * 1024U)
#endif

/* DEBUG */
#if 0
#define PDUMP_DBG(a)   PDumpOSDebugPrintf (a)
#else
#define PDUMP_DBG(a)
#endif


#define	PTR_PLUS(t, p, x) ((t)(((IMG_CHAR *)(p)) + (x)))
#define	VPTR_PLUS(p, x) PTR_PLUS(IMG_VOID *, p, x)
#define	VPTR_INC(p, x) ((p) = VPTR_PLUS(p, x))
#define MAX_PDUMP_MMU_CONTEXTS	(32)
static IMG_VOID *gpvTempBuffer = IMG_NULL;

#define PERSISTANT_MAGIC           ((IMG_UINTPTR_T) 0xe33ee33e)
#define PDUMP_PERSISTENT_HASH_SIZE 10

#define PDUMP_PRM_FILE_NAME_MAX	32         /*|< Size of parameter name used*/
#define PDUMP_PRM_FILE_SIZE_MAX	0x7FDFFFFF /*!< Default maximum file size to split output files, 2GB-2MB as fwrite limits it to 2GB-1 on 32bit systems */


static HASH_TABLE *g_psPersistentHash = IMG_NULL;

static IMG_BOOL		g_PDumpInitialised = IMG_FALSE;
static IMG_UINT32	g_ConnectionCount = 0;


typedef struct
{
	PDUMP_CHANNEL sCh;         /*!< Channel handles */
} PDUMP_SCRIPT;

typedef struct
{
	IMG_UINT32    ui32Init;    /*|< Count of bytes written to the init phase stream */
	IMG_UINT32    ui32Main;    /*!< Count of bytes written to the main stream */
	IMG_UINT32    ui32Deinit;  /*!< Count of bytes written to the deinit stream */
} PDUMP_CHANNEL_WOFFSETS;

typedef struct
{
	PDUMP_CHANNEL          sCh;             /*!< Channel handles */
	PDUMP_CHANNEL_WOFFSETS sWOff;           /*!< Channel file write offsets */
	IMG_UINT32             ui32FileIdx;     /*!< File index used when file size limit reached and a new file is started, parameter channel only */
	IMG_UINT32             ui32MaxFileSize; /*!< Maximum file size for parameter files */

	PDUMP_FILEOFFSET_T     uiZeroPageOffset; /*!< Offset of the zero page in the parameter file */
	IMG_SIZE_T             uiZeroPageSize; /*!< Size of the zero page in the parameter file */
	IMG_CHAR               szZeroPageFilename[PDUMP_PRM_FILE_NAME_MAX]; /*< PRM file name where the zero page was pdumped */
} PDUMP_PARAMETERS;

static PDUMP_SCRIPT     g_PDumpScript    = { { 0, 0, 0} };
static PDUMP_PARAMETERS g_PDumpParameters = { { 0, 0, 0}, {0, 0, 0}, 0, PDUMP_PRM_FILE_SIZE_MAX};


#if defined(PDUMP_DEBUG_OUTFILES)
/* counter increments each time debug write is called */
IMG_UINT32 g_ui32EveryLineCounter = 1U;
#endif

#if defined(PDUMP_DEBUG) || defined(REFCOUNT_DEBUG)
#define PDUMP_REFCOUNT_PRINT(fmt, ...) PVRSRVDebugPrintf(PVR_DBG_WARNING, __FILE__, __LINE__, fmt, __VA_ARGS__)
#else
#define PDUMP_REFCOUNT_PRINT(fmt, ...)
#endif

/* Prototype for the test/debug state dump rotuine used in debugging */
IMG_VOID PDumpCommonDumpState(IMG_BOOL bDumpOSLayerState);
#undef PDUMP_TRACE_STATE


/*****************************************************************************/
/*	PDump Control Module Definitions                                         */
/*****************************************************************************/

typedef struct _PDUMP_CAPTURE_RANGE_
{
	IMG_UINT32 ui32Start;       /*!< Start frame number of range */
	IMG_UINT32 ui32End;         /*!< Send frame number of range */
	IMG_UINT32 ui32Interval;    /*!< Frame sample rate interval */
} PDUMP_CAPTURE_RANGE;

/* No direct access to members from outside the control module - please */
typedef struct _PDUMP_CTRL_STATE_
{
	IMG_BOOL            bInitPhaseActive;   /*!< State of driver initialisation phase */
	IMG_UINT32          ui32Flags;          /*!< Unused */

	IMG_UINT32          ui32DefaultCapMode; /*!< Capture mode of the dump */
	PDUMP_CAPTURE_RANGE sDefaultRange;      /*|< The default capture range */
	IMG_UINT32          ui32CurrentFrame;   /*!< Current frame number */

	IMG_BOOL            bCaptureOn;         /*!< Current capture status, is current frame in range */
	IMG_BOOL            bSuspended;         /*!< Suspend flag set on unrecoverable error */
	IMG_BOOL            bInPowerTransition; /*!< Device power transition state */
} PDUMP_CTRL_STATE;

static PDUMP_CTRL_STATE g_PDumpCtrl =
{
	IMG_TRUE,
	0,

	0,              /*!< Value obtained from OS PDump layer during initialisation */
	{
		0xFFFFFFFF,
		0xFFFFFFFF,
		1
	},
	0,

	IMG_FALSE,
	IMG_FALSE,
	IMG_FALSE
};

IMG_VOID PDumpCtrlInit(IMG_UINT32 ui32InitCapMode)
{
	g_PDumpCtrl.ui32DefaultCapMode = ui32InitCapMode;
	PVR_ASSERT(g_PDumpCtrl.ui32DefaultCapMode != 0);
}

static IMG_VOID PDumpCtrlUpdateCaptureStatus(IMG_VOID)
{
	if (g_PDumpCtrl.ui32DefaultCapMode == DEBUG_CAPMODE_FRAMED)
	{
		if ((g_PDumpCtrl.ui32CurrentFrame >= g_PDumpCtrl.sDefaultRange.ui32Start) &&
			(g_PDumpCtrl.ui32CurrentFrame <= g_PDumpCtrl.sDefaultRange.ui32End) &&
			(((g_PDumpCtrl.ui32CurrentFrame - g_PDumpCtrl.sDefaultRange.ui32Start) % g_PDumpCtrl.sDefaultRange.ui32Interval) == 0))
		{
			g_PDumpCtrl.bCaptureOn = IMG_TRUE;
		}
		else
		{
			g_PDumpCtrl.bCaptureOn = IMG_FALSE;
		}
	}
	else if (g_PDumpCtrl.ui32DefaultCapMode == DEBUG_CAPMODE_CONTINUOUS)
	{
		g_PDumpCtrl.bCaptureOn = IMG_TRUE;
	}
	else
	{
		g_PDumpCtrl.bCaptureOn = IMG_FALSE;
		PVR_DPF((PVR_DBG_ERROR, "PDumpCtrlSetCurrentFrame: Unexpected capture mode (%x)", g_PDumpCtrl.ui32DefaultCapMode));
	}

}

IMG_VOID PDumpCtrlSetDefaultCaptureParams(IMG_UINT32 ui32Mode, IMG_UINT32 ui32Start, IMG_UINT32 ui32End, IMG_UINT32 ui32Interval)
{
	PVR_ASSERT(ui32Interval > 0);
	PVR_ASSERT(ui32End >= ui32Start);
	PVR_ASSERT((ui32Mode == DEBUG_CAPMODE_FRAMED) || (ui32Mode == DEBUG_CAPMODE_CONTINUOUS));

	/*
		Set the default capture range to that supplied by the PDump client tool
	 */
	g_PDumpCtrl.ui32DefaultCapMode = ui32Mode;
	g_PDumpCtrl.sDefaultRange.ui32Start = ui32Start;
	g_PDumpCtrl.sDefaultRange.ui32End = ui32End;
	g_PDumpCtrl.sDefaultRange.ui32Interval = ui32Interval;

	/*
		Reset the current frame on reset of the default capture range, helps
		avoid inter-pdump start frame issues when the driver is not reloaded.
	 */
	PDumpCtrlSetCurrentFrame(0);
}

INLINE IMG_BOOL PDumpCtrlCapModIsFramed(IMG_VOID)
{
	return g_PDumpCtrl.ui32DefaultCapMode == DEBUG_CAPMODE_FRAMED;
}

INLINE IMG_BOOL PDumpCtrlCapModIsContinuous(IMG_VOID)
{
	return g_PDumpCtrl.ui32DefaultCapMode == DEBUG_CAPMODE_CONTINUOUS;
}

IMG_UINT32 PDumpCtrlGetCurrentFrame(IMG_VOID)
{
	return g_PDumpCtrl.ui32CurrentFrame;
}

IMG_VOID PDumpCtrlSetCurrentFrame(IMG_UINT32 ui32Frame)
{
	g_PDumpCtrl.ui32CurrentFrame = ui32Frame;
	/* Mirror the value into the debug driver */
	PDumpOSSetFrame(ui32Frame);

	PDumpCtrlUpdateCaptureStatus();

#if defined(PDUMP_TRACE_STATE)	
	PDumpCommonDumpState(IMG_FALSE);
#endif
}

INLINE IMG_BOOL PDumpCtrlCaptureOn(IMG_VOID)
{
	return !g_PDumpCtrl.bSuspended && g_PDumpCtrl.bCaptureOn;
}

INLINE IMG_BOOL PDumpCtrlCaptureRangePast(IMG_VOID)
{
	return (g_PDumpCtrl.ui32CurrentFrame > g_PDumpCtrl.sDefaultRange.ui32End);
}

/* Used to imply if the PDump client is connected or not. */
INLINE IMG_BOOL PDumpCtrlCaptureRangeUnset(IMG_VOID)
{
	return ((g_PDumpCtrl.sDefaultRange.ui32Start == 0xFFFFFFFFU) &&
			(g_PDumpCtrl.sDefaultRange.ui32End == 0xFFFFFFFFU));
}

IMG_BOOL PDumpCtrIsLastCaptureFrame(IMG_VOID)
{
	if (g_PDumpCtrl.ui32DefaultCapMode == DEBUG_CAPMODE_FRAMED)
	{
		/* Is the next capture frame within the range end limit? */
		if ((g_PDumpCtrl.ui32CurrentFrame + g_PDumpCtrl.sDefaultRange.ui32Interval) > g_PDumpCtrl.sDefaultRange.ui32End)
		{
			return IMG_TRUE;
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "PDumpCtrIsLastCaptureFrame: Unexpected capture mode (%x)", g_PDumpCtrl.ui32DefaultCapMode));
	}

	/* Return false for continuous capture mode or when in framed mode */
	return IMG_FALSE;
}

INLINE IMG_BOOL PDumpCtrlInitPhaseComplete(IMG_VOID)
{
	return !g_PDumpCtrl.bInitPhaseActive;
}

INLINE IMG_VOID PDumpCtrlSetInitPhaseComplete(IMG_BOOL bIsComplete)
{
	if (bIsComplete)
	{
		g_PDumpCtrl.bInitPhaseActive = IMG_FALSE;
		PDUMP_HEREA(102);
	}
	else
	{
		g_PDumpCtrl.bInitPhaseActive = IMG_TRUE;
		PDUMP_HEREA(103);
	}
}

INLINE IMG_VOID PDumpCtrlSuspend(IMG_VOID)
{
	PDUMP_HEREA(104);
	g_PDumpCtrl.bSuspended = IMG_TRUE;
}

INLINE IMG_VOID PDumpCtrlResume(IMG_VOID)
{
	PDUMP_HEREA(105);
	g_PDumpCtrl.bSuspended = IMG_FALSE;
}

INLINE IMG_BOOL PDumpCtrlIsDumpSuspended(IMG_VOID)
{
	return g_PDumpCtrl.bSuspended;
}

IMG_VOID PDumpCtrlPowerTransitionStart(IMG_VOID)
{
	g_PDumpCtrl.bInPowerTransition = IMG_TRUE;
}

IMG_VOID PDumpCtrlPowerTransitionEnd(IMG_VOID)
{
	g_PDumpCtrl.bInPowerTransition = IMG_FALSE;
}

INLINE IMG_BOOL PDumpCtrlInPowerTransition(IMG_VOID)
{
	return g_PDumpCtrl.bInPowerTransition;
}


/*****************************************************************************/
/*	PDump Common Write Layer just above PDump OS Layer                       */
/*****************************************************************************/


/* 
	Checks in this method were seeded from the original PDumpWriteILock()
	and DBGDrivWriteCM() and have grown since to ensure PDump output
	matches legacy output.
	Note: the order of the checks in this method is important as some
	writes have multiple pdump flags set!
 */
static IMG_BOOL PDumpWriteAllowed(IMG_UINT32 ui32Flags)
{
	/* No writes if in framed mode and range pasted */
	if (PDumpCtrlCaptureRangePast())
	{
		PDUMP_HERE(10);
		return IMG_FALSE;
	}

	/* No writes while writing is suspended */
	if (PDumpCtrlIsDumpSuspended())
	{
		PDUMP_HERE(11);
		return IMG_FALSE;
	}

	/* Prevent PDumping during a power transition */
	if (PDumpCtrlInPowerTransition())
	{	/* except when it's flagged */
		if (ui32Flags & PDUMP_FLAGS_POWER)
		{
			PDUMP_HERE(20);
			return IMG_TRUE;
		}
		PDUMP_HERE(16);
		return IMG_FALSE;
	}

	/* Always allow dumping in init phase and when persistent flagged */
	if (ui32Flags & PDUMP_FLAGS_PERSISTENT)
	{
		PDUMP_HERE(12);
		return IMG_TRUE;
	}
	if (!PDumpCtrlInitPhaseComplete())
	{
		PDUMP_HERE(15);
		return IMG_TRUE;
	}

	/* The following checks are made when the driver has completed initialisation */

	/* If PDump client connected allow continuous flagged writes */
	if (ui32Flags & PDUMP_FLAGS_CONTINUOUS)
	{
		if (PDumpCtrlCaptureRangeUnset()) /* Is client connected? */
		{
			PDUMP_HERE(13);
			return IMG_FALSE;
		}
		PDUMP_HERE(14);
		return IMG_TRUE;
	}

	/* No last/deinit statements allowed when not in initialisation phase */
	if (ui32Flags & PDUMP_FLAGS_DEINIT)
	{
		if (PDumpCtrlInitPhaseComplete())
		{
			PDUMP_HERE(17);
			PVR_DPF((PVR_DBG_ERROR, "PDumpWriteAllowed: DEINIT flag used at the wrong time outside of initialisation!"));
			return IMG_FALSE;
		}
	}

	/* 
		If no flags are provided then it is FRAMED output and the frame
		range must be checked matching expected behaviour.
	 */
	if (PDumpCtrlCapModIsFramed() && !PDumpCtrlCaptureOn())
	{
		PDUMP_HERE(18);
		return IMG_FALSE;
	}

	PDUMP_HERE(19);

	/* Allow the write to take place */
	return IMG_TRUE;
}

#undef PDUMP_DEBUG_SCRIPT_LINES

#if defined(PDUMP_DEBUG_SCRIPT_LINES)
#define PDUMPOSDEBUGDRIVERWRITE(a,b,c,d) _PDumpOSDebugDriverWrite(a,b,c,d)
static IMG_UINT32 _PDumpOSDebugDriverWrite( IMG_HANDLE psStream,
									IMG_UINT8 *pui8Data,
									IMG_UINT32 ui32BCount,
									IMG_UINT32 ui32Flags)
{
	IMG_CHAR tmp1[80];
	IMG_CHAR* streamName = "unkn";

	if (g_PDumpScript.sCh.hDeinit == psStream)
		streamName = "dein";
	else if (g_PDumpScript.sCh.hInit == psStream)
		streamName = "init";
	else if (g_PDumpScript.sCh.hMain == psStream)
		streamName = "main";

	(void) PDumpOSSprintf(tmp1, 80, "-- %s, %x\n", streamName, ui32Flags);
	(void) PDumpOSDebugDriverWrite(psStream, tmp1, OSStringLength(tmp1));

	return PDumpOSDebugDriverWrite(psStream, pui8Data, ui32BCount);
}
#else
#define PDUMPOSDEBUGDRIVERWRITE(a,b,c,d) PDumpOSDebugDriverWrite(a,b,c)
#endif


/**************************************************************************/ /*!
 @Function		PDumpWriteToBuffer
 @Description	Write the supplied data to the PDump stream buffer and attempt
                to handle any buffer full conditions to ensure all the data
                requested to be written, is.

 @Input			psStream	The address of the PDump stream buffer to write to
 @Input			pui8Data    Pointer to the data to be written
 @Input			ui32BCount	Number of bytes to write
 @Input			ui32Flags	PDump statement flags.

 @Return 		IMG_UINT32  Actual number of bytes written, may be less than
 	 	 	 	 	 	 	ui32BCount when buffer full condition could not
 	 	 	 	 	 	 	be avoided.
*/ /***************************************************************************/
static IMG_UINT32 PDumpWriteToBuffer(IMG_HANDLE psStream, IMG_UINT8 *pui8Data,
		IMG_UINT32 ui32BCount, IMG_UINT32 ui32Flags)
{
	IMG_UINT32	ui32BytesWritten = 0;
	IMG_UINT32	ui32Off = 0;

	while (ui32BCount > 0)
	{
		ui32BytesWritten = PDUMPOSDEBUGDRIVERWRITE(psStream, &pui8Data[ui32Off], ui32BCount, ui32Flags);

		if (ui32BytesWritten == 0)
		{
			PVR_DPF((PVR_DBG_MESSAGE, "PDumpWriteToBuffer: Zero bytes written - release execution"));
			PDumpOSReleaseExecution();
		}

		if (ui32BytesWritten != 0xFFFFFFFFU)
		{
			if (ui32BCount != ui32BytesWritten)
			{
				PVR_DPF((PVR_DBG_MESSAGE, "PDumpWriteToBuffer: partial write of %d bytes of %d bytes", ui32BytesWritten, ui32BCount));
			}
			ui32Off += ui32BytesWritten;
			ui32BCount -= ui32BytesWritten;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "PDumpWriteToBuffer: Unrecoverable error received from the debug driver"));
			if( PDumpOSGetCtrlState(psStream, DBG_GET_STATE_FLAG_IS_READONLY) )
			{
				/* Fatal -suspend PDump to prevent flooding kernel log buffer */
				PVR_LOG(("PDump suspended, debug driver out of memory"));
				PDumpCtrlSuspend();
			}
			return 0;
		}
	}

	/* reset buffer counters */
	ui32BCount = ui32Off; ui32Off = 0; ui32BytesWritten = 0;

	return ui32BCount;
}


/**************************************************************************/ /*!
 @Function		PDumpWriteToChannel
 @Description	Write the supplied data to the PDump channel specified obeying
 	            flags to write to the necessary channel buffers.

 @Input			psChannel	The address of the script or parameter channel object
 @Input/Output	psWOff		The address of the channel write offsets object to
                            update on successful writing
 @Input			pui8Data    Pointer to the data to be written
 @Input			ui32Size	Number of bytes to write
 @Input			ui32Flags	PDump statement flags, they may be clear (no flags)
                            which implies framed data, continuous flagged,
                            persistent flagged, or continuous AND persistent
                            flagged and they determine how the data is output.
                            On the first test app run after driver load, the
                            Display Controller dumps a resource that is both
                            continuous and persistent and this needs writing to
                            both the init (persistent) and main (continuous)
                            channel buffers to ensure the data is dumped in
                            subsequent test runs without reloading the driver.
    						In subsequent runs the PDump client 'freezes' the
    						init buffer so that only one dump of persistent data
    						for the "extended init phase" is captured to the
    						init buffer.

 @Return 		IMG_BOOL    True when the data has been consumed, false otherwise
*/ /***************************************************************************/
static IMG_BOOL PDumpWriteToChannel(PDUMP_CHANNEL* psChannel, PDUMP_CHANNEL_WOFFSETS* psWOff,
		IMG_UINT8* pui8Data, IMG_UINT32 ui32Size, IMG_UINT32 ui32Flags)
{
	IMG_UINT32   ui32BytesWritten = 0;

	PDUMP_HERE(210);

	/* Dump data to deinit buffer when flagged as deinit */
	if (ui32Flags & PDUMP_FLAGS_DEINIT)
	{
		PDUMP_HERE(211);
		ui32BytesWritten = PDumpWriteToBuffer(psChannel->hDeinit, pui8Data, ui32Size, ui32Flags);
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
		IMG_HANDLE*  phStream = IMG_NULL;
		IMG_UINT32*  pui32Offset = IMG_NULL;

		/* Always append persistent data to init phase so it's available on
		 * subsequent app runs, but also to the main stream if client connected */
		if (ui32Flags & PDUMP_FLAGS_PERSISTENT)
		{
			PDUMP_HERE(213);
			ui32BytesWritten = PDumpWriteToBuffer(	psChannel->hInit, pui8Data, ui32Size, ui32Flags);
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
			if ((ui32Flags & PDUMP_FLAGS_CONTINUOUS) && PDumpCtrlCaptureRangeUnset())
			{
				return IMG_TRUE;
			}
		}

		/* Prepare to write the data to the main stream for
		 * persistent, continuous or framed data. Override and use init
		 * stream if driver still in init phase and we have not written 
		 * to it yet.*/
		if (!PDumpCtrlInitPhaseComplete() && !bDumpedToInitAlready)
		{
			PDUMP_HERE(215);
			phStream = &psChannel->hInit;
			if (psWOff)
			{
				pui32Offset = &psWOff->ui32Init;
			}
		}
		else
		{
			PDUMP_HERE(216);
			phStream = &psChannel->hMain;
			if (psWOff)
			{
				pui32Offset = &psWOff->ui32Main;
			}
		}

		/* Write the data to the stream */
		ui32BytesWritten = PDumpWriteToBuffer(*phStream, pui8Data, ui32Size, ui32Flags);
		if (ui32BytesWritten != ui32Size)
		{
			PVR_DPF((PVR_DBG_ERROR, "PDumpWriteToChannel: MAIN Written length (%d) does not match data length (%d), PDump incomplete!", ui32BytesWritten, ui32Size));
			PDUMP_HERE(217);
			return IMG_FALSE;
		}

		if (pui32Offset)
		{
			*pui32Offset += ui32BytesWritten;
		}
	}

	return IMG_TRUE;
}


PVRSRV_ERROR PDumpWriteParameter(IMG_UINT8 *pui8Data, IMG_UINT32 ui32Size, IMG_UINT32 ui32Flags,
		IMG_UINT32* pui32FileOffset, IMG_CHAR* aszFilenameStr)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT(pui8Data && (ui32Size!=0));
	PVR_ASSERT(pui32FileOffset && aszFilenameStr);

	PDUMP_HERE(1);

	if (!PDumpWriteAllowed(ui32Flags))
	{
		/* Abort write for the above reasons but indicated it was OK to
		 * caller to avoid disrupting the driver */
		return PVRSRV_OK;
	}

	PDUMP_HERE(2);

	if (!PDumpCtrlInitPhaseComplete() || (ui32Flags & PDUMP_FLAGS_PERSISTENT))
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
			PDumpOSSetSplitMarker(g_PDumpParameters.sCh.hMain, g_PDumpParameters.sWOff.ui32Main);
			g_PDumpParameters.ui32FileIdx++;
			g_PDumpParameters.sWOff.ui32Main = 0;
		}

		/* Return the file write offset at which the parameter data was dumped */
		*pui32FileOffset = g_PDumpParameters.sWOff.ui32Main;
	}

	/* Create the parameter file name, based on index, to be used in the script */
	if (g_PDumpParameters.ui32FileIdx == 0)
	{
		eError = PDumpOSSprintf(aszFilenameStr, PDUMP_PRM_FILE_NAME_MAX, PDUMP_PARAM_0_FILE_NAME);
	}
	else
	{
		PDUMP_HERE(6);
		eError = PDumpOSSprintf(aszFilenameStr, PDUMP_PRM_FILE_NAME_MAX, PDUMP_PARAM_N_FILE_NAME, g_PDumpParameters.ui32FileIdx);
	}
	PVR_LOGG_IF_ERROR(eError, "PDumpOSSprintf", errExit);

	/* Write the parameter data to the parameter channel */
	eError = PVRSRV_ERROR_PDUMP_BUFFER_FULL;
	if (!PDumpWriteToChannel(&g_PDumpParameters.sCh, &g_PDumpParameters.sWOff, pui8Data, ui32Size, ui32Flags))
	{
		PDUMP_HERE(7);
		PVR_LOGG_IF_ERROR(eError, "PDumpWrite", errExit);
	}

	return PVRSRV_OK;

errExit:
	return eError;
}


IMG_BOOL PDumpWriteScript(IMG_HANDLE hString, IMG_UINT32 ui32Flags)
{
	PVR_ASSERT(hString);

	PDUMP_HERE(201);

	if (!PDumpWriteAllowed(ui32Flags))
	{
		/* Abort write for the above reasons but indicated it was OK to
		 * caller to avoid disrupting the driver */
		return IMG_TRUE;
	}

	return PDumpWriteToChannel(&g_PDumpScript.sCh, IMG_NULL, (IMG_UINT8*) hString, (IMG_UINT32) OSStringLength((IMG_CHAR*) hString), ui32Flags);
}


/*****************************************************************************/






struct _PDUMP_CONNECTION_DATA_ {
	IMG_UINT32				ui32RefCount;
	POS_LOCK				hLock;
	DLLIST_NODE				sListHead;
	IMG_BOOL				bLastInto;
	IMG_UINT32				ui32LastSetFrameNumber;
	IMG_BOOL				bWasInCaptureRange;
	IMG_BOOL				bIsInCaptureRange;
	IMG_BOOL				bLastTransitionFailed;
	SYNC_CONNECTION_DATA	*psSyncConnectionData;
};

static PDUMP_CONNECTION_DATA * _PDumpConnectionAcquire(PDUMP_CONNECTION_DATA *psPDumpConnectionData)
{
	IMG_UINT32 ui32RefCount;

	OSLockAcquire(psPDumpConnectionData->hLock);
	ui32RefCount = ++psPDumpConnectionData->ui32RefCount;
	OSLockRelease(psPDumpConnectionData->hLock);

	PDUMP_REFCOUNT_PRINT("%s: PDump connection %p, refcount = %d",
						 __FUNCTION__, psPDumpConnectionData, ui32RefCount);

	return psPDumpConnectionData;
}

static IMG_VOID _PDumpConnectionRelease(PDUMP_CONNECTION_DATA *psPDumpConnectionData)
{
	IMG_UINT32 ui32RefCount;

	OSLockAcquire(psPDumpConnectionData->hLock);
	ui32RefCount = --psPDumpConnectionData->ui32RefCount;
	OSLockRelease(psPDumpConnectionData->hLock);

	if (ui32RefCount == 0)
	{
		OSLockDestroy(psPDumpConnectionData->hLock);
		PVR_ASSERT(dllist_is_empty(&psPDumpConnectionData->sListHead));
		OSFreeMem(psPDumpConnectionData);
	}

	PDUMP_REFCOUNT_PRINT("%s: PDump connection %p, refcount = %d",
						 __FUNCTION__, psPDumpConnectionData, ui32RefCount);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpIsPersistent)
#endif

IMG_BOOL PDumpIsPersistent(IMG_VOID)
{
	IMG_PID uiPID = OSGetCurrentProcessIDKM();
	IMG_UINTPTR_T puiRetrieve;

	puiRetrieve = HASH_Retrieve(g_psPersistentHash, uiPID);
	if (puiRetrieve != 0)
	{
		PVR_ASSERT(puiRetrieve == PERSISTANT_MAGIC);
		PDUMP_HEREA(110);
		return IMG_TRUE;
	}
	return IMG_FALSE;
}


/**************************************************************************
 * Function Name  : GetTempBuffer
 * Inputs         : None
 * Outputs        : None
 * Returns        : Temporary buffer address, or IMG_NULL
 * Description    : Get temporary buffer address.
**************************************************************************/
static IMG_VOID *GetTempBuffer(IMG_VOID)
{
	/*
	 * Allocate the temporary buffer, it it hasn't been allocated already.
	 * Return the address of the temporary buffer, or IMG_NULL if it
	 * couldn't be allocated.
	 * It is expected that the buffer will be allocated once, at driver
	 * load time, and left in place until the driver unloads.
	 */

	if (gpvTempBuffer == IMG_NULL)
	{
		gpvTempBuffer = OSAllocMem(PDUMP_TEMP_BUFFER_SIZE);
		if (gpvTempBuffer == IMG_NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "GetTempBuffer: OSAllocMem failed"));
		}
	}

	return gpvTempBuffer;
}

static IMG_VOID FreeTempBuffer(IMG_VOID)
{

	if (gpvTempBuffer != IMG_NULL)
	{
		OSFreeMem(gpvTempBuffer);
		gpvTempBuffer = IMG_NULL;
	}
}

/**************************************************************************
 * Function Name  : PDumpParameterChannelZeroedPageBlock
 * Inputs         : None
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Set up the zero page block in the parameter stream
**************************************************************************/
static PVRSRV_ERROR PDumpParameterChannelZeroedPageBlock(IMG_VOID)
{
	IMG_UINT8 aui8Zero[32] = { 0 };
	IMG_SIZE_T uiBytesToWrite;
	PVRSRV_ERROR eError;

	g_PDumpParameters.uiZeroPageSize = OSGetPageSize();

	/* ensure the zero page size of a multiple of the zero source on the stack */
	PVR_ASSERT(g_PDumpParameters.uiZeroPageSize % sizeof(aui8Zero) == 0);

	/* the first write gets the parameter file name and stream offset,
	 * then subsequent writes do not need to know this as the data is
	 * contiguous in the stream
	 */
	PDUMP_LOCK();
	eError = PDumpWriteParameter(aui8Zero,
							sizeof(aui8Zero),
							0,
							&g_PDumpParameters.uiZeroPageOffset,
							g_PDumpParameters.szZeroPageFilename);

	if(eError != PVRSRV_OK)
	{
		goto err_write;
	}

	uiBytesToWrite = g_PDumpParameters.uiZeroPageSize - sizeof(aui8Zero);

	while(uiBytesToWrite)
	{
		IMG_BOOL bOK;

		bOK = PDumpWriteToChannel(&g_PDumpParameters.sCh, &g_PDumpParameters.sWOff,
									aui8Zero,
									sizeof(aui8Zero), 0);

		if(!bOK)
		{
			eError = PVRSRV_ERROR_PDUMP_BUFFER_FULL;
			goto err_write;
		}

		uiBytesToWrite -= sizeof(aui8Zero);
	}

err_write:
	PDUMP_UNLOCK();

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to initialise parameter stream zero block"));
	}

	return eError;
}

/**************************************************************************
 * Function Name  : PDumpGetParameterZeroPageInfo
 * Inputs         : None
 * Outputs        : puiZeroPageOffset: will be set to the offset of the zero page
 *                : puiZeroPageSize: will be set to the size of the zero page
 *                : ppszZeroPageFilename: will be set to a pointer to the PRM file name
 *                :                       containing the zero page
 * Returns        : None
 * Description    : Get information about the zero page
**************************************************************************/
IMG_VOID PDumpGetParameterZeroPageInfo(PDUMP_FILEOFFSET_T *puiZeroPageOffset,
					IMG_SIZE_T *puiZeroPageSize,
					const IMG_CHAR **ppszZeroPageFilename)
{
		*puiZeroPageOffset = g_PDumpParameters.uiZeroPageOffset;
		*puiZeroPageSize = g_PDumpParameters.uiZeroPageSize;
		*ppszZeroPageFilename = g_PDumpParameters.szZeroPageFilename;
}

PVRSRV_ERROR PDumpInitCommon(IMG_VOID)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32InitCapMode = 0;
	IMG_CHAR* pszEnvComment = IMG_NULL;

	PDUMP_HEREA(2010);

	/* Allocate temporary buffer for copying from user space */
	(IMG_VOID) GetTempBuffer();

	eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	g_psPersistentHash = HASH_Create(PDUMP_PERSISTENT_HASH_SIZE);
	PVR_LOGG_IF_FALSE((g_psPersistentHash != IMG_NULL), "Failed to create persistent process hash", errExit);

	/* create the global PDump lock */
	eError = PDumpCreateLockKM();
	PVR_LOGG_IF_ERROR(eError, "PDumpCreateLockKM", errExit);

	/* Call environment specific PDump initialisation */
	eError = PDumpOSInit(&g_PDumpParameters.sCh, &g_PDumpScript.sCh, &ui32InitCapMode, &pszEnvComment);
	PVR_LOGG_IF_ERROR(eError, "PDumpOSInit", errExitLock);

	/* Initialise PDump control module in common layer */
	PDumpCtrlInit(ui32InitCapMode);

	/* Test PDump initialised and ready by logging driver details */
	eError = PDumpComment("Driver Product Name: %s", PVRSRVGetSystemName());
	PVR_LOGG_IF_ERROR(eError, "PDumpComment", errExitCtrl);
	eError = PDumpComment("Driver Product Version: %s - %s (%s)", PVRVERSION_STRING, PVR_BUILD_DIR, PVR_BUILD_TYPE);
	PVR_LOGG_IF_ERROR(eError, "PDumpComment", errExitCtrl);
	if (pszEnvComment != IMG_NULL)
	{
		eError = PDumpComment("%s", pszEnvComment);
		PVR_LOGG_IF_ERROR(eError, "PDumpComment", errExitCtrl);
	}
	eError = PDumpComment("Start of Init Phase");
	PVR_LOGG_IF_ERROR(eError, "PDumpComment", errExitCtrl);

	eError = PDumpParameterChannelZeroedPageBlock();
	PVR_LOGG_IF_ERROR(eError, "PDumpParameterChannelZeroedPageBlock", errExitCtrl);

	g_PDumpInitialised = IMG_TRUE;

	PDUMP_HEREA(2011);

	return PVRSRV_OK;

errExitCtrl:
	/* No PDumpCtrlDeInit at present */
	PDUMP_HEREA(2018);
	PDumpOSDeInit(&g_PDumpParameters.sCh, &g_PDumpScript.sCh);
errExitLock:
	PDUMP_HEREA(2019);
	PDumpDestroyLockKM();
errExit:
	return eError;
}

IMG_VOID PDumpDeInitCommon(IMG_VOID)
{
	PDUMP_HEREA(2020);

	g_PDumpInitialised = IMG_FALSE;

	/* Free temporary buffer */
	FreeTempBuffer();

	/* Call environment specific PDump Deinitialisation */
	PDumpOSDeInit(&g_PDumpParameters.sCh, &g_PDumpScript.sCh);

	/* take down the global PDump lock */
	PDumpDestroyLockKM();
}

IMG_BOOL PDumpReady(IMG_VOID)
{
	return g_PDumpInitialised;
}


PVRSRV_ERROR PDumpAddPersistantProcess(IMG_VOID)
{
	IMG_PID uiPID = OSGetCurrentProcessIDKM();
	IMG_UINTPTR_T puiRetrieve;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PDUMP_HEREA(121);

	puiRetrieve = HASH_Retrieve(g_psPersistentHash, uiPID);
	if (puiRetrieve == 0)
	{
		if (!HASH_Insert(g_psPersistentHash, uiPID, PERSISTANT_MAGIC))
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}
	else
	{
		PVR_ASSERT(puiRetrieve == PERSISTANT_MAGIC);
	}
	PDUMP_HEREA(122);

	return eError;
}

PVRSRV_ERROR PDumpStartInitPhaseKM(IMG_VOID)
{
	PDUMPCOMMENT("Start Init Phase");
	PDumpCtrlSetInitPhaseComplete(IMG_FALSE);
	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpStopInitPhaseKM(IMG_MODULE_ID eModuleID)
{
	/* Check with the OS we a running on */
	if (PDumpOSAllowInitPhaseToComplete(eModuleID))
	{
		PDUMPCOMMENT("Stop Init Phase");
		PDumpCtrlSetInitPhaseComplete(IMG_TRUE);
	}

	return PVRSRV_OK;
}

IMG_BOOL PDumpIsLastCaptureFrameKM(IMG_VOID)
{
	return PDumpCtrIsLastCaptureFrame();
}



typedef struct _PDUMP_Transition_DATA_ {
	PFN_PDUMP_TRANSITION	pfnCallback;
	IMG_PVOID				hPrivData;
	PDUMP_CONNECTION_DATA	*psPDumpConnectionData;
	DLLIST_NODE				sNode;
} PDUMP_Transition_DATA;

PVRSRV_ERROR PDumpRegisterTransitionCallback(PDUMP_CONNECTION_DATA *psPDumpConnectionData,
											  PFN_PDUMP_TRANSITION pfnCallback,
											  IMG_PVOID hPrivData,
											  IMG_PVOID *ppvHandle)
{
	PDUMP_Transition_DATA *psData;
	PVRSRV_ERROR eError;

	psData = OSAllocMem(sizeof(*psData));
	if (psData == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}

	/* Setup the callback and add it to the list for this process */
	psData->pfnCallback = pfnCallback;
	psData->hPrivData = hPrivData;
	dllist_add_to_head(&psPDumpConnectionData->sListHead, &psData->sNode);

	/* Take a reference on the connection so it doesn't get freed too early */
	psData->psPDumpConnectionData =_PDumpConnectionAcquire(psPDumpConnectionData);
	*ppvHandle = psData;

	return PVRSRV_OK;

fail_alloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

IMG_VOID PDumpUnregisterTransitionCallback(IMG_PVOID pvHandle)
{
	PDUMP_Transition_DATA *psData = pvHandle;

	dllist_remove_node(&psData->sNode);
	_PDumpConnectionRelease(psData->psPDumpConnectionData);
	OSFreeMem(psData);
}

typedef struct _PTCB_DATA_ {
	IMG_BOOL bInto;
	IMG_BOOL bContinuous;
	PVRSRV_ERROR eError;
} PTCB_DATA;

static IMG_BOOL _PDumpTransition(DLLIST_NODE *psNode, IMG_PVOID hData)
{
	PDUMP_Transition_DATA *psData = IMG_CONTAINER_OF(psNode, PDUMP_Transition_DATA, sNode);
	PTCB_DATA *psPTCBData = (PTCB_DATA *) hData;

	psPTCBData->eError = psData->pfnCallback(psData->hPrivData, psPTCBData->bInto, psPTCBData->bContinuous);
	if (psPTCBData->eError != PVRSRV_OK)
	{
		/* Got an error, break out of the loop */
		return IMG_FALSE;
	}

	return IMG_TRUE;
}

PVRSRV_ERROR PDumpTransition(PDUMP_CONNECTION_DATA *psPDumpConnectionData, IMG_BOOL bInto, IMG_BOOL bContinuous)
{
	PTCB_DATA sPTCBData;

	/* Only call the callbacks if we've really done a Transition */
	if (bInto != psPDumpConnectionData->bLastInto)
	{
		/* We're Transitioning either into or out of capture range */
		sPTCBData.bInto = bInto;
		sPTCBData.bContinuous = bContinuous;
		sPTCBData.eError = PVRSRV_OK;
		dllist_foreach_node(&psPDumpConnectionData->sListHead, _PDumpTransition, &sPTCBData);
		if (sPTCBData.eError != PVRSRV_OK)
		{
			/* We failed so bail out leaving the state as it is ready for the retry */
			return sPTCBData.eError;
		}

		if (bInto)
		{
			SyncConnectionPDumpSyncBlocks(psPDumpConnectionData->psSyncConnectionData);
		}
		psPDumpConnectionData->bLastInto = bInto;
	}
	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpIsCaptureFrameKM(IMG_BOOL *bIsCapturing)
{
	/* WDDM has an extended init phase to work around RA suballocs not
     * having a PDump MALLOC.
     * As a consequence, the WDDM init phase contains some FW command submissions.
     * These commands were discarded due to being outside a capture range. In order
	 * to correctly PDump these commands the init phase is considered to be a
	 * capture range.
	 * Note that PDumpCtrlInitPhasecCmplete was invented for
	 * this purpose. They are not otherwise required.
	 */
	if(!PDumpCtrlInitPhaseComplete())
	{
		*bIsCapturing = IMG_TRUE;
	}
	else
	{
		*bIsCapturing = PDumpCtrlCaptureOn();
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR _PDumpSetFrameKM(CONNECTION_DATA *psConnection, IMG_UINT32 ui32Frame)
{
	PDUMP_CONNECTION_DATA *psPDumpConnectionData = psConnection->psPDumpConnectionData;
	IMG_BOOL bWasInCaptureRange = IMG_FALSE;
	IMG_BOOL bIsInCaptureRange = IMG_FALSE;
	PVRSRV_ERROR eError;

	/*
		Note:
		As we can't test to see if the new frame will be in capture range
		before we set the frame number and we don't want to roll back
		the frame number if we fail then we have to save the "transient"
		data which decides if we're entering or exiting capture range
		along with a failure boolean so we know what to do on a retry
	*/
	if (psPDumpConnectionData->ui32LastSetFrameNumber != ui32Frame)
	{
		PDumpIsCaptureFrameKM(&bWasInCaptureRange);
		PDumpCtrlSetCurrentFrame(ui32Frame);
		PDumpIsCaptureFrameKM(&bIsInCaptureRange);
		psPDumpConnectionData->ui32LastSetFrameNumber = ui32Frame;

		/* Save the Transition data incase we fail the Transition */
		psPDumpConnectionData->bWasInCaptureRange = bWasInCaptureRange;
		psPDumpConnectionData->bIsInCaptureRange = bIsInCaptureRange;
	}
	else if (psPDumpConnectionData->bLastTransitionFailed)
	{
		/* Load the Transition data so we can try again */
		bWasInCaptureRange = psPDumpConnectionData->bWasInCaptureRange;
		bIsInCaptureRange = psPDumpConnectionData->bIsInCaptureRange;
	}
	else
	{
		/* New frame is the same as the last fame set and the last
		 * transition succeeded, no need to perform another transition.
		 */
		return PVRSRV_OK;
	}

	if (!bWasInCaptureRange && bIsInCaptureRange)
	{
		eError = PDumpTransition(psPDumpConnectionData, IMG_TRUE, IMG_FALSE);
		if (eError != PVRSRV_OK)
		{
			goto fail_Transition;
		}
	}
	else if (bWasInCaptureRange && !bIsInCaptureRange)
	{
		eError = PDumpTransition(psPDumpConnectionData, IMG_FALSE, IMG_FALSE);
		if (eError != PVRSRV_OK)
		{
			goto fail_Transition;
		}
	}
	else
	{
		/* Here both previous and current frames are in or out of range */
		/* Should never reach here due to the above goto success */
	}

	psPDumpConnectionData->bLastTransitionFailed = IMG_FALSE;
	return PVRSRV_OK;

fail_Transition:
	psPDumpConnectionData->bLastTransitionFailed = IMG_TRUE;
	return eError;
}

PVRSRV_ERROR PDumpSetFrameKM(CONNECTION_DATA *psConnection, IMG_UINT32 ui32Frame)
{
	PVRSRV_ERROR eError = PVRSRV_OK;	
	

#if defined(PDUMP_TRACE_STATE)
	PVR_DPF((PVR_DBG_WARNING, "PDumpSetFrameKM: ui32Frame( %d )", ui32Frame));
#endif

	/* Ignore errors as it is not fatal if the comments do not appear */
	(void) PDumpComment("Set pdump frame %u (pre)", ui32Frame);

	eError = _PDumpSetFrameKM(psConnection, ui32Frame);
	if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
	{
		PVR_LOG_ERROR(eError, "_PDumpSetFrameKM");
	}

	(void) PDumpComment("Set pdump frame %u (post)", ui32Frame);

	return eError;
}

PVRSRV_ERROR PDumpGetFrameKM(CONNECTION_DATA *psConnection, IMG_UINT32* pui32Frame)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	*pui32Frame = PDumpCtrlGetCurrentFrame();
	return eError;
}

PVRSRV_ERROR PDumpSetDefaultCaptureParamsKM(IMG_UINT32 ui32Mode,
                                           IMG_UINT32 ui32Start,
                                           IMG_UINT32 ui32End,
                                           IMG_UINT32 ui32Interval,
                                           IMG_UINT32 ui32MaxParamFileSize)
{
	PDumpCtrlSetDefaultCaptureParams(ui32Mode, ui32Start, ui32End, ui32Interval);
	if (ui32MaxParamFileSize == 0)
	{
		g_PDumpParameters.ui32MaxFileSize = PDUMP_PRM_FILE_SIZE_MAX;
	}
	else
	{
		g_PDumpParameters.ui32MaxFileSize = ui32MaxParamFileSize;
	}
	return PVRSRV_OK;
}


/**************************************************************************
 * Function Name  : PDumpReg32
 * Inputs         : pszPDumpDevName, Register offset, and value to write
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a register write
**************************************************************************/
PVRSRV_ERROR PDumpReg32(IMG_CHAR	*pszPDumpRegName,
						IMG_UINT32	ui32Reg,
						IMG_UINT32	ui32Data,
						IMG_UINT32	ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()
	PDUMP_DBG(("PDumpReg32"));

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "WRW :%s:0x%08X 0x%08X", pszPDumpRegName, ui32Reg, ui32Data);

	if (eErr != PVRSRV_OK)
	{
		return eErr;
	}

	PDUMP_LOCK();
	PDumpWriteScript(hScript, ui32Flags);
	PDUMP_UNLOCK();

	return PVRSRV_OK;
}


/**************************************************************************
 * Function Name  : PDumpReg64
 * Inputs         : pszPDumpDevName, Register offset, and value to write
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Create a PDUMP string, which represents a register write
**************************************************************************/
PVRSRV_ERROR PDumpReg64(IMG_CHAR	*pszPDumpRegName,
						IMG_UINT32	ui32Reg,
						IMG_UINT64	ui64Data,
						IMG_UINT32	ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()
	PDUMP_DBG(("PDumpRegKM"));

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "WRW64 :%s:0x%08X 0x%010llX", pszPDumpRegName, ui32Reg, ui64Data);

	if (eErr != PVRSRV_OK)
	{
		return eErr;
	}

	PDUMP_LOCK();
	PDumpWriteScript(hScript, ui32Flags);
	PDUMP_UNLOCK();

	return PVRSRV_OK;
}


/**************************************************************************
 * Function Name  : PDumpLDW
 * Inputs         : pcBuffer -- buffer to send to register bank
 *                  ui32NumLoadBytes -- number of bytes in pcBuffer
 *                  pszDevSpaceName -- devspace for register bank
 *                  ui32Offset -- value of offset control register
 *                  ui32PDumpFlags -- flags to pass to PDumpOSWriteScript
 * Outputs        : None
 * Returns        : PVRSRV_ERROR
 * Description    : Dumps the contents of pcBuffer to a .prm file and
 *                  writes an LDW directive to the pdump output.
 *                  NB: ui32NumLoadBytes must be divisible by 4
**************************************************************************/
PVRSRV_ERROR PDumpLDW(IMG_CHAR      *pcBuffer,
                      IMG_CHAR      *pszDevSpaceName,
                      IMG_UINT32    ui32OffsetBytes,
                      IMG_UINT32    ui32NumLoadBytes,
                      PDUMP_FLAGS_T uiPDumpFlags)
{
	PVRSRV_ERROR eError;
	IMG_CHAR aszParamStreamFilename[PMR_MAX_PARAMSTREAM_FILENAME_LENGTH_DEFAULT];
	IMG_UINT32 ui32ParamStreamFileOffset;

	PDUMP_GET_SCRIPT_STRING()

	eError = PDumpWriteBuffer((IMG_UINT8 *)pcBuffer,
	                          ui32NumLoadBytes,
	                          uiPDumpFlags,
	                          &aszParamStreamFilename[0],
	                          sizeof(aszParamStreamFilename),
	                          &ui32ParamStreamFileOffset);
	PVR_ASSERT(eError == PVRSRV_OK);


	uiPDumpFlags |= (PDumpIsPersistent()) ? PDUMP_FLAGS_PERSISTENT : 0;

	eError = PDumpOSBufprintf(hScript,
	                          ui32MaxLen,
	                          "LDW :%s:0x%x 0x%x 0x%x %s\n",
	                          pszDevSpaceName,
	                          ui32OffsetBytes,
	                          ui32NumLoadBytes / (IMG_UINT32)sizeof(IMG_UINT32),
	                          ui32ParamStreamFileOffset,
	                          aszParamStreamFilename);

	if(eError != PVRSRV_OK)
	{
		return eError;
	}

	PDUMP_LOCK();
	PDumpWriteScript(hScript, uiPDumpFlags);
	PDUMP_UNLOCK();

	return PVRSRV_OK;
}

/**************************************************************************
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
**************************************************************************/
PVRSRV_ERROR PDumpSAW(IMG_CHAR      *pszDevSpaceName,
                      IMG_UINT32    ui32HPOffsetBytes,
                      IMG_UINT32    ui32NumSaveBytes,
                      IMG_CHAR      *pszOutfileName,
                      IMG_UINT32    ui32OutfileOffsetByte,
                      PDUMP_FLAGS_T uiPDumpFlags)
{
	PVRSRV_ERROR eError;

	PDUMP_GET_SCRIPT_STRING()

	PVR_DPF((PVR_DBG_ERROR, "PDumpSAW\n"));

	uiPDumpFlags |= (PDumpIsPersistent()) ? PDUMP_FLAGS_PERSISTENT : 0;

	eError = PDumpOSBufprintf(hScript,
	                          ui32MaxLen,
	                          "SAW :%s:0x%x 0x%x 0x%x %s\n",
	                          pszDevSpaceName,
	                          ui32HPOffsetBytes,
	                          ui32NumSaveBytes / (IMG_UINT32)sizeof(IMG_UINT32),
	                          ui32OutfileOffsetByte,
	                          pszOutfileName);

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PDumpSAW PDumpOSBufprintf failed: eError=%u\n", eError));
		return eError;
	}

	PDUMP_LOCK();
	if(! PDumpWriteScript(hScript, uiPDumpFlags))
	{
		PVR_DPF((PVR_DBG_ERROR, "PDumpSAW PDumpWriteScript failed!\n"));
	}
	PDUMP_UNLOCK();

	return PVRSRV_OK;
	
}


/**************************************************************************
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
**************************************************************************/
PVRSRV_ERROR PDumpRegPolKM(IMG_CHAR				*pszPDumpRegName,
						   IMG_UINT32			ui32RegAddr, 
						   IMG_UINT32			ui32RegValue, 
						   IMG_UINT32			ui32Mask,
						   IMG_UINT32			ui32Flags,
						   PDUMP_POLL_OPERATOR	eOperator)
{
	/* Timings correct for linux and XP */
	/* Timings should be passed in */
	#define POLL_DELAY			1000U
	#define POLL_COUNT_LONG		(2000000000U / POLL_DELAY)
	#define POLL_COUNT_SHORT	(1000000U / POLL_DELAY)

	PVRSRV_ERROR eErr;
	IMG_UINT32	ui32PollCount;

	PDUMP_GET_SCRIPT_STRING();
	PDUMP_DBG(("PDumpRegPolKM"));
	if ( PDumpIsPersistent() )
	{
		/* Don't pdump-poll if the process is persistent */
		return PVRSRV_OK;
	}

	ui32PollCount = POLL_COUNT_LONG;

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "POL :%s:0x%08X 0x%08X 0x%08X %d %u %d",
							pszPDumpRegName, ui32RegAddr, ui32RegValue,
							ui32Mask, eOperator, ui32PollCount, POLL_DELAY);
	if(eErr != PVRSRV_OK)
	{
		return eErr;
	}

	PDUMP_LOCK();
	PDumpWriteScript(hScript, ui32Flags);
	PDUMP_UNLOCK();

	return PVRSRV_OK;
}


/**************************************************************************
 * Function Name  : PDumpCommentKM
 * Inputs         : pszComment, ui32Flags
 * Outputs        : None
 * Returns        : None
 * Description    : Dumps a comment
**************************************************************************/
PVRSRV_ERROR PDumpCommentKM(IMG_CHAR *pszComment, IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
#if defined(PDUMP_DEBUG_OUTFILES)
	IMG_CHAR pszTemp[256];
#endif
	PDUMP_GET_SCRIPT_STRING();
	PDUMP_DBG(("PDumpCommentKM"));

#if defined(PDUMP_DEBUG_OUTFILES)
	/* include comments in the "extended" init phase.
	 * default is to ignore them.
	 */
	ui32Flags |= ( PDumpIsPersistent() ) ? PDUMP_FLAGS_PERSISTENT : 0;
#endif

	if((pszComment == IMG_NULL) || (PDumpOSBuflen(pszComment, ui32MaxLen) == 0))
	{
		/* PDumpOSVerifyLineEnding silently fails if pszComment is too short to
		   actually hold the line endings that it's trying to enforce, so
		   short circuit it and force safety */
		pszComment = "\n";
	}
	else
	{
		/* Put line ending sequence at the end if it isn't already there */
		PDumpOSVerifyLineEnding(pszComment, ui32MaxLen);
	}

	PDUMP_LOCK();

#if defined(PDUMP_DEBUG_OUTFILES)
	/* Prefix comment with PID and line number */
	eErr = PDumpOSSprintf(pszTemp, 256, "%u %u:%lu %s: %s",
		g_ui32EveryLineCounter,
		OSGetCurrentProcessIDKM(),
		(unsigned long)OSGetCurrentThreadIDKM(),
		OSGetCurrentProcessNameKM(),
		pszComment);

	/* Append the comment to the script stream */
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "-- %s",
		pszTemp);
#else
	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "-- %s",
		pszComment);
#endif
	if( (eErr != PVRSRV_OK) &&
		(eErr != PVRSRV_ERROR_PDUMP_BUF_OVERFLOW))
	{
		PVR_LOGG_IF_ERROR(eErr, "PDumpOSBufprintf", ErrUnlock);
	}

	if (!PDumpWriteScript(hScript, ui32Flags))
	{
		if(ui32Flags & PDUMP_FLAGS_CONTINUOUS)
		{
			eErr = PVRSRV_ERROR_PDUMP_BUFFER_FULL;
			PVR_LOGG_IF_ERROR(eErr, "PDumpWriteScript", ErrUnlock);
		}
		else
		{
			eErr = PVRSRV_ERROR_CMD_NOT_PROCESSED;
			PVR_LOGG_IF_ERROR(eErr, "PDumpWriteScript", ErrUnlock);
		}
	}

ErrUnlock:
	PDUMP_UNLOCK();
	return eErr;
}

/**************************************************************************
 * Function Name  : PDumpCommentWithFlags
 * Inputs         : psPDev - PDev for PDump device
 *				  : pszFormat - format string for comment
 *				  : ... - args for format string
 * Outputs        : None
 * Returns        : None
 * Description    : PDumps a comments
**************************************************************************/
PVRSRV_ERROR PDumpCommentWithFlags(IMG_UINT32 ui32Flags, IMG_CHAR * pszFormat, ...)
{
	PVRSRV_ERROR eErr;
	PDUMP_va_list ap;
	PDUMP_GET_MSG_STRING();

	/* Construct the string */
	PDUMP_va_start(ap, pszFormat);
	eErr = PDumpOSVSprintf(pszMsg, ui32MaxLen, pszFormat, ap);
	PDUMP_va_end(ap);

	if(eErr != PVRSRV_OK)
	{
		return eErr;
	}
	return PDumpCommentKM(pszMsg, ui32Flags);
}

/**************************************************************************
 * Function Name  : PDumpComment
 * Inputs         : psPDev - PDev for PDump device
 *				  : pszFormat - format string for comment
 *				  : ... - args for format string
 * Outputs        : None
 * Returns        : None
 * Description    : PDumps a comments
**************************************************************************/
PVRSRV_ERROR PDumpComment(IMG_CHAR *pszFormat, ...)
{
	PVRSRV_ERROR eErr;
	PDUMP_va_list ap;
	PDUMP_GET_MSG_STRING();

	/* Construct the string */
	PDUMP_va_start(ap, pszFormat);
	eErr = PDumpOSVSprintf(pszMsg, ui32MaxLen, pszFormat, ap);
	PDUMP_va_end(ap);
	PVR_LOGR_IF_ERROR(eErr, "PDumpOSVSprintf");

	return PDumpCommentKM(pszMsg, PDUMP_FLAGS_CONTINUOUS);
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
PVRSRV_ERROR PDumpPanic(IMG_UINT32      ui32PanicNo,
						IMG_CHAR*       pszPanicMsg,
						const IMG_CHAR* pszPPFunc,
						IMG_UINT32      ui32PPline)
{
	PVRSRV_ERROR   eError = PVRSRV_OK;
	PDUMP_FLAGS_T  uiPDumpFlags = PDUMP_FLAGS_CONTINUOUS;
	IMG_CHAR       pszConsoleMsg[] =
"COM ***************************************************************************\n"
"COM Script invalid and not compatible with off-line playback. Check test \n"
"COM parameters and driver configuration, stop imminent.\n"
"COM ***************************************************************************\n";
	PDUMP_GET_SCRIPT_STRING();

	/* Log the panic condition to the live kern.log in both REL and DEB mode 
	 * to aid user PDump trouble shooting. */
	PVR_LOG(("PDUMP PANIC %08x: %s", ui32PanicNo, pszPanicMsg));
	PVR_DPF((PVR_DBG_MESSAGE, "PDUMP PANIC start %s:%d", pszPPFunc, ui32PPline));

	/* Check the supplied panic reason string is within length limits */
	PVR_ASSERT(OSStringLength(pszPanicMsg)+sizeof("PANIC   ") < PVRSRV_PDUMP_MAX_COMMENT_SIZE-1);

	/* Add persistent flag if required and obtain lock to keep the multi-line
	 * panic statement together in a single atomic write */
	uiPDumpFlags |= (PDumpIsPersistent()) ? PDUMP_FLAGS_PERSISTENT : 0;
	PDUMP_LOCK();

	/* Write -- Panic start (Function:line) */
	eError = PDumpOSBufprintf(hScript, ui32MaxLen, "-- Panic start (%s:%d)", pszPPFunc, ui32PPline);
	PVR_LOGG_IF_ERROR(eError, "PDumpOSBufprintf", e1);
	(IMG_VOID)PDumpWriteScript(hScript, uiPDumpFlags);

	/* Write COM <message> x4 */
	eError = PDumpOSBufprintf(hScript, ui32MaxLen, pszConsoleMsg);
	PVR_LOGG_IF_ERROR(eError, "PDumpOSBufprintf", e1);
	(IMG_VOID)PDumpWriteScript(hScript, uiPDumpFlags);

	/* Write PANIC no msg command */
	eError = PDumpOSBufprintf(hScript, ui32MaxLen, "PANIC %08x %s", ui32PanicNo, pszPanicMsg);
	PVR_LOGG_IF_ERROR(eError, "PDumpOSBufprintf", e1);
	(IMG_VOID)PDumpWriteScript(hScript, uiPDumpFlags);

	/* Write -- Panic end */
	eError = PDumpOSBufprintf(hScript, ui32MaxLen, "-- Panic end");
	PVR_LOGG_IF_ERROR(eError, "PDumpOSBufprintf", e1);
	(IMG_VOID)PDumpWriteScript(hScript, uiPDumpFlags);

e1:
	PDUMP_UNLOCK();

	return eError;
}

/*!
******************************************************************************

 @Function	PDumpBitmapKM

 @Description

 Dumps a bitmap from device memory to a file

 @Input    psDevId
 @Input    pszFileName
 @Input    ui32FileOffset
 @Input    ui32Width
 @Input    ui32Height
 @Input    ui32StrideInBytes
 @Input    sDevBaseAddr
 @Input    ui32Size
 @Input    ePixelFormat
 @Input    eMemFormat
 @Input    ui32PDumpFlags

 @Return   PVRSRV_ERROR			:

******************************************************************************/
PVRSRV_ERROR PDumpBitmapKM(	PVRSRV_DEVICE_NODE *psDeviceNode,
							IMG_CHAR *pszFileName,
							IMG_UINT32 ui32FileOffset,
							IMG_UINT32 ui32Width,
							IMG_UINT32 ui32Height,
							IMG_UINT32 ui32StrideInBytes,
							IMG_DEV_VIRTADDR sDevBaseAddr,
							IMG_UINT32 ui32MMUContextID,
							IMG_UINT32 ui32Size,
							PDUMP_PIXEL_FORMAT ePixelFormat,
							IMG_UINT32 ui32AddrMode,
							IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_DEVICE_IDENTIFIER *psDevId = &psDeviceNode->sDevId;
	PVRSRV_ERROR eErr=0;
	PDUMP_GET_SCRIPT_STRING();

	if ( PDumpIsPersistent() )
	{
		return PVRSRV_OK;
	}
	
	PDumpCommentWithFlags(ui32PDumpFlags, "Dump bitmap of render.");
	
	switch (ePixelFormat)
	{
		case PVRSRV_PDUMP_PIXEL_FORMAT_YUV8:
		{
			PDumpCommentWithFlags(ui32PDumpFlags, "YUV data. Switching from SII to SAB. Width=0x%08X Height=0x%08X Stride=0x%08X",
							 						ui32Width, ui32Height, ui32StrideInBytes);
							 						
			eErr = PDumpOSBufprintf(hScript,
									ui32MaxLen,
									"SAB :%s:v%x:0x%010llX 0x%08X 0x%08X %s.bin\n",
									psDevId->pszPDumpDevName,
									ui32MMUContextID,
									sDevBaseAddr.uiAddr,
									ui32Size,
									ui32FileOffset,
									pszFileName);
			
			if (eErr != PVRSRV_OK)
			{
				return eErr;
			}
			
			PDUMP_LOCK();
			PDumpWriteScript( hScript, ui32PDumpFlags);
			PDUMP_UNLOCK();		
			break;
		}
		case PVRSRV_PDUMP_PIXEL_FORMAT_420PL12YUV8: // YUV420 2 planes
		{
			const IMG_UINT32 ui32Plane0Size = ui32StrideInBytes*ui32Height;
			const IMG_UINT32 ui32Plane1Size = ui32Plane0Size>>1; // YUV420
			const IMG_UINT32 ui32Plane1FileOffset = ui32FileOffset + ui32Plane0Size;
			const IMG_UINT32 ui32Plane1MemOffset = ui32Plane0Size;
			
			PDumpCommentWithFlags(ui32PDumpFlags, "YUV420 2-plane. Width=0x%08X Height=0x%08X Stride=0x%08X",
							 						ui32Width, ui32Height, ui32StrideInBytes);
			eErr = PDumpOSBufprintf(hScript,
						ui32MaxLen,
						"SII %s %s.bin :%s:v%x:0x%010llX 0x%08X 0x%08X :%s:v%x:0x%010llX 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X",
						pszFileName,
						pszFileName,
						
						// Plane 0 (Y)
						psDevId->pszPDumpDevName,	// memsp
						ui32MMUContextID,			// Context id
						sDevBaseAddr.uiAddr,		// virtaddr
						ui32Plane0Size,				// size
						ui32FileOffset,				// fileoffset
						
						// Plane 1 (UV)
						psDevId->pszPDumpDevName,	// memsp
						ui32MMUContextID,			// Context id
						sDevBaseAddr.uiAddr+ui32Plane1MemOffset,	// virtaddr
						ui32Plane1Size,				// size
						ui32Plane1FileOffset,		// fileoffset
						
						ePixelFormat,
						ui32Width,
						ui32Height,
						ui32StrideInBytes,
						ui32AddrMode);
						
			if (eErr != PVRSRV_OK)
			{
				return eErr;
			}
			
			PDUMP_LOCK();
			PDumpWriteScript( hScript, ui32PDumpFlags);
			PDUMP_UNLOCK();
			break;
		}
		
		case PVRSRV_PDUMP_PIXEL_FORMAT_YUV_YV12: // YUV420 3 planes
		{
			const IMG_UINT32 ui32Plane0Size = ui32StrideInBytes*ui32Height;
			const IMG_UINT32 ui32Plane1Size = ui32Plane0Size>>2; // YUV420
			const IMG_UINT32 ui32Plane2Size = ui32Plane1Size;
			const IMG_UINT32 ui32Plane1FileOffset = ui32FileOffset + ui32Plane0Size;
			const IMG_UINT32 ui32Plane2FileOffset = ui32Plane1FileOffset + ui32Plane1Size;
			const IMG_UINT32 ui32Plane1MemOffset = ui32Plane0Size;
			const IMG_UINT32 ui32Plane2MemOffset = ui32Plane0Size+ui32Plane1Size;
	
			PDumpCommentWithFlags(ui32PDumpFlags, "YUV420 3-plane. Width=0x%08X Height=0x%08X Stride=0x%08X",
							 						ui32Width, ui32Height, ui32StrideInBytes);
			eErr = PDumpOSBufprintf(hScript,
						ui32MaxLen,
						"SII %s %s.bin :%s:v%x:0x%010llX 0x%08X 0x%08X :%s:v%x:0x%010llX 0x%08X 0x%08X :%s:v%x:0x%010llX 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X",
						pszFileName,
						pszFileName,
						
						// Plane 0 (Y)
						psDevId->pszPDumpDevName,	// memsp
						ui32MMUContextID,			// MMU context id
						sDevBaseAddr.uiAddr,		// virtaddr
						ui32Plane0Size,				// size
						ui32FileOffset,				// fileoffset
						
						// Plane 1 (U)
						psDevId->pszPDumpDevName,	// memsp
						ui32MMUContextID,			// MMU context id
						sDevBaseAddr.uiAddr+ui32Plane1MemOffset,	// virtaddr
						ui32Plane1Size,				// size
						ui32Plane1FileOffset,		// fileoffset
						
						// Plane 2 (V)
						psDevId->pszPDumpDevName,	// memsp
						ui32MMUContextID,			// MMU context id
						sDevBaseAddr.uiAddr+ui32Plane2MemOffset,	// virtaddr
						ui32Plane2Size,				// size
						ui32Plane2FileOffset,		// fileoffset
						
						ePixelFormat,
						ui32Width,
						ui32Height,
						ui32StrideInBytes,
						ui32AddrMode);
						
			if (eErr != PVRSRV_OK)
			{
				return eErr;
			}
			
			PDUMP_LOCK();
			PDumpWriteScript( hScript, ui32PDumpFlags);
			PDUMP_UNLOCK();
			break;
		}
		
		default: // Single plane formats
		{
			eErr = PDumpOSBufprintf(hScript,
						ui32MaxLen,
						"SII %s %s.bin :%s:v%x:0x%010llX 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X",
						pszFileName,
						pszFileName,
						psDevId->pszPDumpDevName,
						ui32MMUContextID,
						sDevBaseAddr.uiAddr,
						ui32Size,
						ui32FileOffset,
						ePixelFormat,
						ui32Width,
						ui32Height,
						ui32StrideInBytes,
						ui32AddrMode);
						
			if (eErr != PVRSRV_OK)
			{
				return eErr;
			}

			PDUMP_LOCK();
			PDumpWriteScript( hScript, ui32PDumpFlags);
			PDUMP_UNLOCK();
			break;
		}
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PDumpReadRegKM

 @Description

 Dumps a read from a device register to a file

 @Input    psConnection 		: connection info
 @Input    pszFileName
 @Input    ui32FileOffset
 @Input    ui32Address
 @Input    ui32Size
 @Input    ui32PDumpFlags

 @Return   PVRSRV_ERROR			:

******************************************************************************/
PVRSRV_ERROR PDumpReadRegKM		(	IMG_CHAR *pszPDumpRegName,
									IMG_CHAR *pszFileName,
									IMG_UINT32 ui32FileOffset,
									IMG_UINT32 ui32Address,
									IMG_UINT32 ui32Size,
									IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	PVR_UNREFERENCED_PARAMETER(ui32Size);

	eErr = PDumpOSBufprintf(hScript,
			ui32MaxLen,
			"SAB :%s:0x%08X 0x%08X %s",
			pszPDumpRegName,
			ui32Address,
			ui32FileOffset,
			pszFileName);
	if(eErr != PVRSRV_OK)
	{
		return eErr;
	}

	PDUMP_LOCK();
	PDumpWriteScript( hScript, ui32PDumpFlags);
	PDUMP_UNLOCK();

	return PVRSRV_OK;
}

/*****************************************************************************
 @name		PDumpRegRead32
 @brief		Dump 32-bit register read to script
 @param		pszPDumpDevName - pdump device name
 @param		ui32RegOffset - register offset
 @param		ui32Flags - pdump flags
 @return	Error
*****************************************************************************/
PVRSRV_ERROR PDumpRegRead32(IMG_CHAR *pszPDumpRegName,
							const IMG_UINT32 ui32RegOffset,
							IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "RDW :%s:0x%X",
							pszPDumpRegName, 
							ui32RegOffset);
	if(eErr != PVRSRV_OK)
	{
		return eErr;
	}
	PDUMP_LOCK();
	PDumpWriteScript(hScript, ui32Flags);
	PDUMP_UNLOCK();
	return PVRSRV_OK;
}

/*****************************************************************************
 @name		PDumpRegRead64
 @brief		Dump 64-bit register read to script
 @param		pszPDumpDevName - pdump device name
 @param		ui32RegOffset - register offset
 @param		ui32Flags - pdump flags
 @return	Error
*****************************************************************************/
PVRSRV_ERROR PDumpRegRead64(IMG_CHAR *pszPDumpRegName,
							const IMG_UINT32 ui32RegOffset,
							IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "RDW64 :%s:0x%X",
							pszPDumpRegName, 
							ui32RegOffset);
	if(eErr != PVRSRV_OK)
	{
		return eErr;
	}
	PDUMP_LOCK();
	PDumpWriteScript(hScript, ui32Flags);
	PDUMP_UNLOCK();
	return PVRSRV_OK;
}


/*****************************************************************************
 FUNCTION	: PDumpWriteShiftedMaskedValue

 PURPOSE	: Emits the PDump commands for writing a masked shifted address
              into another location

 PARAMETERS	: PDump symbolic name and offset of target word
              PDump symbolic name and offset of source address
              right shift amount
              left shift amount
              mask

 RETURNS	: None
*****************************************************************************/
PVRSRV_ERROR
PDumpWriteShiftedMaskedValue(const IMG_CHAR *pszDestRegspaceName,
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
        return PVRSRV_ERROR_NOT_SUPPORTED;
    }

    pszWrwSuffix = (uiWordSize == 8) ? "64" : "";

    /* Should really "Acquire" a pdump register here */
    pszPDumpIntRegSpace = pszDestRegspaceName;
    uiPDumpIntRegNum = 1;
        
    eError = PDumpOSBufprintf(hScript,
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
    if (eError != PVRSRV_OK)
    {
        goto ErrOut;
    }

    PDUMP_LOCK();
    PDumpWriteScript(hScript, uiPDumpFlags);

    if (uiSHRAmount > 0)
    {
        eError = PDumpOSBufprintf(hScript,
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
        if (eError != PVRSRV_OK)
        {
            goto ErrUnlock;
        }
        PDumpWriteScript(hScript, uiPDumpFlags);
    }
    
    if (uiSHLAmount > 0)
    {
        eError = PDumpOSBufprintf(hScript,
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
        if (eError != PVRSRV_OK)
        {
            goto ErrUnlock;
        }
        PDumpWriteScript(hScript, uiPDumpFlags);
    }
    
    if (uiMask != (1ULL << (8*uiWordSize))-1)
    {
        eError = PDumpOSBufprintf(hScript,
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
        if (eError != PVRSRV_OK)
        {
            goto ErrUnlock;
        }
        PDumpWriteScript(hScript, uiPDumpFlags);
    }

    eError = PDumpOSBufprintf(hScript,
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
    if(eError != PVRSRV_OK)
    {
        goto ErrUnlock;
    }
    PDumpWriteScript(hScript, uiPDumpFlags);

ErrUnlock:
	PDUMP_UNLOCK();
ErrOut:
	return eError;
}


PVRSRV_ERROR
PDumpWriteSymbAddress(const IMG_CHAR *pszDestSpaceName,
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

    PDUMP_LOCK();

    if (ui32AlignShift != ui32Shift)
    {
    	/* Write physical address into a variable */
    	eError = PDumpOSBufprintf(hScript,
    							ui32MaxLen,
    							"WRW%s :%s:$1 %s:" IMG_DEVMEM_OFFSET_FMTSPEC "\n",
    							pszWrwSuffix,
    							/* dest */
    							pszPDumpDevName,
    							/* src */
    							pszRefSymbolicName,
    							uiRefOffset);
		if (eError != PVRSRV_OK)
		{
			goto symbAddress_error;
		}
    	PDumpWriteScript(hScript, uiPDumpFlags);

    	/* apply address alignment  */
    	eError = PDumpOSBufprintf(hScript,
    							ui32MaxLen,
    							"SHR :%s:$1 :%s:$1 0x%X",
    							/* dest */
    							pszPDumpDevName,
    							/* src A */
    							pszPDumpDevName,
    							/* src B */
    							ui32AlignShift);
		if (eError != PVRSRV_OK)
		{
			goto symbAddress_error;
		}
    	PDumpWriteScript(hScript, uiPDumpFlags);

    	/* apply address shift  */
    	eError = PDumpOSBufprintf(hScript,
    							ui32MaxLen,
    							"SHL :%s:$1 :%s:$1 0x%X",
    							/* dest */
    							pszPDumpDevName,
    							/* src A */
    							pszPDumpDevName,
    							/* src B */
    							ui32Shift);
		if (eError != PVRSRV_OK)
		{
			goto symbAddress_error;
		}
    	PDumpWriteScript(hScript, uiPDumpFlags);


    	/* write result to register */
    	eError = PDumpOSBufprintf(hScript,
    							ui32MaxLen,
    							"WRW%s :%s:0x%08X :%s:$1",
    							pszWrwSuffix,
    							pszDestSpaceName,
    							(IMG_UINT32)uiDestOffset,
    							pszPDumpDevName);
		if (eError != PVRSRV_OK)
		{
			goto symbAddress_error;
		}
    	PDumpWriteScript(hScript, uiPDumpFlags);
    }
    else
    {
		eError = PDumpOSBufprintf(hScript,
								  ui32MaxLen,
								  "WRW%s :%s:" IMG_DEVMEM_OFFSET_FMTSPEC " %s:" IMG_DEVMEM_OFFSET_FMTSPEC "\n",
								  pszWrwSuffix,
								  /* dest */
								  pszDestSpaceName,
								  uiDestOffset,
								  /* src */
								  pszRefSymbolicName,
								  uiRefOffset);
		if (eError != PVRSRV_OK)
		{
			goto symbAddress_error;
		}
	    PDumpWriteScript(hScript, uiPDumpFlags);
    }

symbAddress_error:

    PDUMP_UNLOCK();

	return eError;
}

/**************************************************************************
 * Function Name  : PDumpIDLWithFlags
 * Inputs         : Idle time in clocks
 * Outputs        : None
 * Returns        : Error
 * Description    : Dump IDL command to script
**************************************************************************/
PVRSRV_ERROR PDumpIDLWithFlags(IMG_UINT32 ui32Clocks, IMG_UINT32 ui32Flags)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING();
	PDUMP_DBG(("PDumpIDLWithFlags"));

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "IDL %u", ui32Clocks);
	if(eErr != PVRSRV_OK)
	{
		return eErr;
	}
	PDUMP_LOCK();
	PDumpWriteScript(hScript, ui32Flags);
	PDUMP_UNLOCK();
	return PVRSRV_OK;
}


/**************************************************************************
 * Function Name  : PDumpIDL
 * Inputs         : Idle time in clocks
 * Outputs        : None
 * Returns        : Error
 * Description    : Dump IDL command to script
**************************************************************************/
PVRSRV_ERROR PDumpIDL(IMG_UINT32 ui32Clocks)
{
	return PDumpIDLWithFlags(ui32Clocks, PDUMP_FLAGS_CONTINUOUS);
}

/*****************************************************************************
 FUNCTION	: PDumpRegBasedCBP
    
 PURPOSE	: Dump CBP command to script

 PARAMETERS	:
			  
 RETURNS	: None
*****************************************************************************/
PVRSRV_ERROR PDumpRegBasedCBP(IMG_CHAR		*pszPDumpRegName,
							  IMG_UINT32	ui32RegOffset,
							  IMG_UINT32	ui32WPosVal,
							  IMG_UINT32	ui32PacketSize,
							  IMG_UINT32	ui32BufferSize,
							  IMG_UINT32	ui32Flags)
{
	PDUMP_GET_SCRIPT_STRING();

	PDumpOSBufprintf(hScript,
			 ui32MaxLen,
			 "CBP :%s:0x%08X 0x%08X 0x%08X 0x%08X",
			 pszPDumpRegName,
			 ui32RegOffset,
			 ui32WPosVal,
			 ui32PacketSize,
			 ui32BufferSize);
	PDUMP_LOCK();
	PDumpWriteScript(hScript, ui32Flags);
	PDUMP_UNLOCK();
	
	return PVRSRV_OK;		
}

PVRSRV_ERROR PDumpTRG(IMG_CHAR *pszMemSpace,
                      IMG_UINT32 ui32MMUCtxID,
                      IMG_UINT32 ui32RegionID,
                      IMG_BOOL bEnable,
                      IMG_UINT64 ui64VAddr,
                      IMG_UINT64 ui64LenBytes,
                      IMG_UINT32 ui32XStride,
                      IMG_UINT32 ui32Flags)
{
	PDUMP_GET_SCRIPT_STRING();

	if(bEnable)
	{
		PDumpOSBufprintf(hScript, ui32MaxLen,
		                 "TRG :%s:v%u %u 0x%08llX 0x%08llX %u",
		                 pszMemSpace, ui32MMUCtxID, ui32RegionID,
		                 ui64VAddr, ui64LenBytes, ui32XStride);
	}
	else
	{
		PDumpOSBufprintf(hScript, ui32MaxLen,
		                 "TRG :%s:v%u %u",
		                 pszMemSpace, ui32MMUCtxID, ui32RegionID);

	}

	PDUMP_LOCK();
	PDumpWriteScript(hScript, ui32Flags);
	PDUMP_UNLOCK();

	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : PDumpConnectionNotify
 * Description    : Called by the srvcore to tell PDump core that the
 *                  PDump capture and control client has connected
 **************************************************************************/
IMG_VOID PDumpConnectionNotify(IMG_VOID)
{
	PVRSRV_DATA			*psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEVICE_NODE	*psThis;

	if (PDumpOSAllowInitPhaseToComplete(IMG_PDUMPCTRL))
	{
		/* No 'Stop Init Phase' comment here as in this case as the PDump
		 * client can connect multiple times and we don't want the comment
		 * appearing multiple times in out files.
		 */
		PDumpCtrlSetInitPhaseComplete(IMG_TRUE);
	}

	g_ConnectionCount++;
	PVR_LOG(("PDump has connected (%u)", g_ConnectionCount));
	
	/* Reset the parameter file attributes */
	g_PDumpParameters.sWOff.ui32Main = g_PDumpParameters.sWOff.ui32Init;
	g_PDumpParameters.ui32FileIdx = 0;

	/* Loop over all known devices */
	psThis = psPVRSRVData->psDeviceNodeList;
	while (psThis)
	{
		if (psThis->pfnPDumpInitDevice)
		{
			/* Reset pdump according to connected device */
			psThis->pfnPDumpInitDevice(psThis);
		}
		psThis = psThis->psNext;
	}
}

/**************************************************************************
 * Function Name  : PDumpIfKM
 * Inputs         : pszPDumpCond - string for condition
 * Outputs        : None
 * Returns        : None
 * Description    : Create a PDUMP string which represents IF command 
					with condition.
**************************************************************************/
PVRSRV_ERROR PDumpIfKM(IMG_CHAR		*pszPDumpCond)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()
	PDUMP_DBG(("PDumpIfKM"));

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "IF %s\n", pszPDumpCond);

	if (eErr != PVRSRV_OK)
	{
		return eErr;
	}

	PDUMP_LOCK();
	PDumpWriteScript(hScript, PDUMP_FLAGS_CONTINUOUS);
	PDUMP_UNLOCK();

	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : PDumpElseKM
 * Inputs         : pszPDumpCond - string for condition
 * Outputs        : None
 * Returns        : None
 * Description    : Create a PDUMP string which represents ELSE command 
					with condition.
**************************************************************************/
PVRSRV_ERROR PDumpElseKM(IMG_CHAR		*pszPDumpCond)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()
	PDUMP_DBG(("PDumpElseKM"));

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "ELSE %s\n", pszPDumpCond);

	if (eErr != PVRSRV_OK)
	{
		return eErr;
	}

	PDUMP_LOCK();
	PDumpWriteScript(hScript, PDUMP_FLAGS_CONTINUOUS);
	PDUMP_UNLOCK();

	return PVRSRV_OK;
}

/**************************************************************************
 * Function Name  : PDumpFiKM
 * Inputs         : pszPDumpCond - string for condition
 * Outputs        : None
 * Returns        : None
 * Description    : Create a PDUMP string which represents FI command 
					with condition.
**************************************************************************/
PVRSRV_ERROR PDumpFiKM(IMG_CHAR		*pszPDumpCond)
{
	PVRSRV_ERROR eErr;
	PDUMP_GET_SCRIPT_STRING()
	PDUMP_DBG(("PDumpFiKM"));

	eErr = PDumpOSBufprintf(hScript, ui32MaxLen, "FI %s\n", pszPDumpCond);

	if (eErr != PVRSRV_OK)
	{
		return eErr;
	}

	PDUMP_LOCK();
	PDumpWriteScript(hScript, PDUMP_FLAGS_CONTINUOUS);
	PDUMP_UNLOCK();

	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpCreateLockKM(IMG_VOID)
{
	return PDumpOSCreateLock();
}

IMG_VOID PDumpDestroyLockKM(IMG_VOID)
{
	PDumpOSDestroyLock();
}

IMG_VOID PDumpLockKM(IMG_VOID)
{
	PDumpOSLock();
}

IMG_VOID PDumpUnlockKM(IMG_VOID)
{
	PDumpOSUnlock();
}

#if defined(PVR_TESTING_UTILS)
extern IMG_VOID PDumpOSDumpState(IMG_VOID);

#if !defined(LINUX)
IMG_VOID PDumpOSDumpState(IMG_BOOL bDumpOSLayerState)
{
	PVR_UNREFERENCED_PARAMETER(bDumpOSLayerState);
}
#endif

IMG_VOID PDumpCommonDumpState(IMG_BOOL bDumpOSLayerState)
{
	IMG_UINT32* ui32HashData = (IMG_UINT32*)g_psPersistentHash;

	PVR_LOG(("--- PDUMP COMMON: g_PDumpInitialised( %d )",
			g_PDumpInitialised) );
	PVR_LOG(("--- PDUMP COMMON: g_psPersistentHash( %p ) uSize( %d ) uCount( %d )",
			g_psPersistentHash, ui32HashData[0], ui32HashData[1]) );
	PVR_LOG(("--- PDUMP COMMON: g_PDumpScript.sCh.hInit( %p ) g_PDumpScript.sCh.hMain( %p ) g_PDumpScript.sCh.hDeinit( %p )",
			g_PDumpScript.sCh.hInit, g_PDumpScript.sCh.hMain, g_PDumpScript.sCh.hDeinit) );
	PVR_LOG(("--- PDUMP COMMON: g_PDumpParameters.sCh.hInit( %p ) g_PDumpParameters.sCh.hMain( %p ) g_PDumpParameters.sCh.hDeinit( %p )",
			g_PDumpParameters.sCh.hInit, g_PDumpParameters.sCh.hMain, g_PDumpParameters.sCh.hDeinit) );
	PVR_LOG(("--- PDUMP COMMON: g_PDumpParameters.sWOff.ui32Init( %d ) g_PDumpParameters.sWOff.ui32Main( %d ) g_PDumpParameters.sWOff.ui32Deinit( %d )",
			g_PDumpParameters.sWOff.ui32Init, g_PDumpParameters.sWOff.ui32Main, g_PDumpParameters.sWOff.ui32Deinit) );
	PVR_LOG(("--- PDUMP COMMON: g_PDumpParameters.ui32FileIdx( %d )",
			g_PDumpParameters.ui32FileIdx) );

	PVR_LOG(("--- PDUMP COMMON: g_PDumpCtrl( %p ) bInitPhaseActive( %d ) ui32Flags( %x )",
			&g_PDumpCtrl, g_PDumpCtrl.bInitPhaseActive, g_PDumpCtrl.ui32Flags) );
	PVR_LOG(("--- PDUMP COMMON: ui32DefaultCapMode( %d ) ui32CurrentFrame( %d )",
			g_PDumpCtrl.ui32DefaultCapMode, g_PDumpCtrl.ui32CurrentFrame) );
	PVR_LOG(("--- PDUMP COMMON: sDefaultRange.ui32Start( %d ) sDefaultRange.ui32End( %d ) sDefaultRange.ui32Interval( %d )",
			g_PDumpCtrl.sDefaultRange.ui32Start, g_PDumpCtrl.sDefaultRange.ui32End, g_PDumpCtrl.sDefaultRange.ui32Interval) );
	PVR_LOG(("--- PDUMP COMMON: bCaptureOn( %d ) bSuspended( %d ) bInPowerTransition( %d )",
			g_PDumpCtrl.bCaptureOn, g_PDumpCtrl.bSuspended, g_PDumpCtrl.bInPowerTransition) );

	if (bDumpOSLayerState)
	{
		PDumpOSDumpState();
	}
}
#endif


PVRSRV_ERROR PDumpRegisterConnection(SYNC_CONNECTION_DATA *psSyncConnectionData,
									 PDUMP_CONNECTION_DATA **ppsPDumpConnectionData)
{
	PDUMP_CONNECTION_DATA *psPDumpConnectionData;
	PVRSRV_ERROR eError;

	PVR_ASSERT(ppsPDumpConnectionData != IMG_NULL);

	psPDumpConnectionData = OSAllocMem(sizeof(*psPDumpConnectionData));
	if (psPDumpConnectionData == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}

	eError = OSLockCreate(&psPDumpConnectionData->hLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		goto fail_lockcreate;
	}

	dllist_init(&psPDumpConnectionData->sListHead);
	psPDumpConnectionData->ui32RefCount = 1;
	psPDumpConnectionData->bLastInto = IMG_FALSE;
	psPDumpConnectionData->ui32LastSetFrameNumber = 0xFFFFFFFFU;
	psPDumpConnectionData->bLastTransitionFailed = IMG_FALSE;
	/*
		Although we don't take a refcount here resman will ensure that
		any resource which might trigger use to do a Transition will
		have been freed before the sync blocks which are keeping the
		sync connection data alive
	*/
	psPDumpConnectionData->psSyncConnectionData = psSyncConnectionData;
	*ppsPDumpConnectionData = psPDumpConnectionData;

	return PVRSRV_OK;

fail_lockcreate:
	OSFreeMem(psPDumpConnectionData);
fail_alloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

IMG_VOID PDumpUnregisterConnection(PDUMP_CONNECTION_DATA *psPDumpConnectionData)
{
	_PDumpConnectionRelease(psPDumpConnectionData);
}

#else	/* defined(PDUMP) */
/* disable warning about empty module */
#ifdef	_WIN32
#pragma warning (disable:4206)
#endif
#endif	/* defined(PDUMP) */
/*****************************************************************************
 End of file (pdump_common.c)
*****************************************************************************/
