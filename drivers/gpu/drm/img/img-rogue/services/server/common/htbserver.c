/*************************************************************************/ /*!
@File           htbserver.c
@Title          Host Trace Buffer server implementation.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Host Trace Buffer provides a mechanism to log Host events to a
                buffer in a similar way to the Firmware Trace mechanism.
                Host Trace Buffer logs data using a Transport Layer buffer.
                The Transport Layer and pvrtld tool provides the mechanism to
                retrieve the trace data.
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

#include "htbserver.h"
#include "htbuffer.h"
#include "htbuffer_types.h"
#include "tlstream.h"
#include "pvrsrv_tlcommon.h"
#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"
#include "osfunc.h"
#include "allocmem.h"
#include "pvr_notifier.h"
#include "pvrsrv.h"
#include "pvrsrv_apphint.h"
#include "oskm_apphint.h"

/* size of circular buffer controlling the maximum number of concurrent PIDs logged */
#define HTB_MAX_NUM_PID 8

/* number of times to try rewriting a log entry */
#define HTB_LOG_RETRY_COUNT 5

/*************************************************************************/ /*!
  Host Trace Buffer control information structure
*/ /**************************************************************************/
typedef struct
{
	IMG_UINT32 ui32BufferSize;      /*!< Requested buffer size in bytes
                                         Once set this may not be changed */

	HTB_OPMODE_CTRL eOpMode;        /*!< Control what trace data is dropped if
                                         the buffer is full.
                                         Once set this may not be changed */

/*	IMG_UINT32 ui32GroupEnable; */  /*!< Flags word controlling groups to be
                                         logged */

	IMG_UINT32 ui32LogLevel;        /*!< Log level to control messages logged */

	IMG_UINT32 aui32EnablePID[HTB_MAX_NUM_PID]; /*!< PIDs to enable logging for
                                                     a specific set of processes */

	IMG_UINT32 ui32PIDCount;        /*!< Current number of PIDs being logged */

	IMG_UINT32 ui32PIDHead;         /*!< Head of the PID circular buffer */

	HTB_LOGMODE_CTRL eLogMode;      /*!< Logging mode control */

	IMG_BOOL bLogDropSignalled;     /*!< Flag indicating if a log message has
                                         been signalled as dropped */

	/* synchronisation parameters */
	IMG_UINT64 ui64SyncOSTS;
	IMG_UINT64 ui64SyncCRTS;
	IMG_UINT32 ui32SyncCalcClkSpd;
	IMG_UINT32 ui32SyncMarker;

	IMG_BOOL bInitDone;             /* Set by HTBInit, reset by HTBDeInit */

	POS_SPINLOCK hRepeatMarkerLock;     /*!< Spinlock used in HTBLogKM to protect global variables
	                                     (ByteCount, OSTS, CRTS ClkSpeed)
	                                     from becoming inconsistent due to calls from
	                                     both KM and UM */

	IMG_UINT32 ui32ByteCount; /* Byte count used for triggering repeat sync point */
	/* static variables containing details of previous sync point */
	IMG_UINT64 ui64OSTS;
	IMG_UINT64 ui64CRTS;
	IMG_UINT32 ui32ClkSpeed;

} HTB_CTRL_INFO;


/*************************************************************************/ /*!
*/ /**************************************************************************/
static const IMG_UINT32 MapFlags[] =
{
	0,                    /* HTB_OPMODE_UNDEF = 0 */
	TL_OPMODE_DROP_NEWER, /* HTB_OPMODE_DROPLATEST */
	TL_OPMODE_DROP_OLDEST,/* HTB_OPMODE_DROPOLDEST */
	TL_OPMODE_BLOCK       /* HTB_OPMODE_BLOCK */
};

static_assert(0 == HTB_OPMODE_UNDEF,      "Unexpected value for HTB_OPMODE_UNDEF");
static_assert(1 == HTB_OPMODE_DROPLATEST, "Unexpected value for HTB_OPMODE_DROPLATEST");
static_assert(2 == HTB_OPMODE_DROPOLDEST, "Unexpected value for HTB_OPMODE_DROPOLDEST");
static_assert(3 == HTB_OPMODE_BLOCK,      "Unexpected value for HTB_OPMODE_BLOCK");

static_assert(1 == TL_OPMODE_DROP_NEWER,  "Unexpected value for TL_OPMODE_DROP_NEWER");
static_assert(2 == TL_OPMODE_DROP_OLDEST, "Unexpected value for TL_OPMODE_DROP_OLDEST");
static_assert(3 == TL_OPMODE_BLOCK,       "Unexpected value for TL_OPMODE_BLOCK");

static const IMG_UINT32 g_ui32TLBaseFlags; //TL_FLAG_NO_SIGNAL_ON_COMMIT

/* Minimum TL buffer size.
 * Large enough for around 60 worst case messages or 200 average messages
 */
#define HTB_TL_BUFFER_SIZE_MIN	(0x10000)

/* Minimum concentration of HTB packets in a TL Stream is 60%
 * If we just put the HTB header in the TL stream (12 bytes), the TL overhead
 * is 8 bytes for its own header, so for the smallest possible (and most
 * inefficient) packet we have 3/5 of the buffer used for actual HTB data.
 * This shift is used as a guaranteed estimation on when to produce a repeat
 * packet. By shifting the size of the buffer by 1 we effectively /2 this
 * under the 60% boundary chance we may have overwritten the marker and thus
 * guaranteed to always have a marker in the stream */
#define HTB_MARKER_PREDICTION_THRESHOLD(val) (val >> 1)

static HTB_CTRL_INFO g_sCtrl;
static IMG_BOOL g_bConfigured = IMG_FALSE;
static IMG_HANDLE g_hTLStream;


/************************************************************************/ /*!
 @Function      _LookupFlags
 @Description   Convert HTBuffer Operation mode to TLStream flags

 @Input         eModeHTBuffer   Operation Mode

 @Return        IMG_UINT32      TLStream FLags
*/ /**************************************************************************/
static IMG_UINT32
_LookupFlags( HTB_OPMODE_CTRL eMode )
{
	return (eMode < ARRAY_SIZE(MapFlags)) ? MapFlags[eMode] : 0;
}


/************************************************************************/ /*!
 @Function      _HTBLogDebugInfo
 @Description   Debug dump handler used to dump the state of the HTB module.
                Called for each verbosity level during a debug dump. Function
                only prints state when called for High verbosity.

 @Input         hDebugRequestHandle See PFN_DBGREQ_NOTIFY

 @Input         ui32VerbLevel       See PFN_DBGREQ_NOTIFY

 @Input         pfnDumpDebugPrintf  See PFN_DBGREQ_NOTIFY

 @Input         pvDumpDebugFile     See PFN_DBGREQ_NOTIFY

*/ /**************************************************************************/
static void _HTBLogDebugInfo(
		PVRSRV_DBGREQ_HANDLE hDebugRequestHandle,
		IMG_UINT32 ui32VerbLevel,
		DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
		void *pvDumpDebugFile
)
{
	PVR_UNREFERENCED_PARAMETER(hDebugRequestHandle);

	if (DD_VERB_LVL_ENABLED(ui32VerbLevel, DEBUG_REQUEST_VERBOSITY_HIGH))
	{

		if (g_bConfigured)
		{
			IMG_INT i;

			PVR_DUMPDEBUG_LOG("------[ HTB Log state: On ]------");

			PVR_DUMPDEBUG_LOG("HTB Log mode: %d", g_sCtrl.eLogMode);
			PVR_DUMPDEBUG_LOG("HTB Log level: %d", g_sCtrl.ui32LogLevel);
			PVR_DUMPDEBUG_LOG("HTB Buffer Opmode: %d", g_sCtrl.eOpMode);

			for (i=0; i < HTB_FLAG_NUM_EL; i++)
			{
				PVR_DUMPDEBUG_LOG("HTB Log group %d: %x", i, g_auiHTBGroupEnable[i]);
			}
		}
		else
		{
			PVR_DUMPDEBUG_LOG("------[ HTB Log state: Off ]------");
		}
	}
}

/************************************************************************/ /*!
 @Function      HTBDeviceCreate
 @Description   Initialisation actions for HTB at device creation.

 @Input         psDeviceNode    Reference to the device node in context

 @Return        eError          Internal services call returned eError error
                                number
*/ /**************************************************************************/
PVRSRV_ERROR
HTBDeviceCreate(
		PVRSRV_DEVICE_NODE *psDeviceNode
)
{
	PVRSRV_ERROR eError;

	eError = PVRSRVRegisterDbgRequestNotify(&psDeviceNode->hHtbDbgReqNotify,
			 psDeviceNode, &_HTBLogDebugInfo, DEBUG_REQUEST_HTB, NULL);
	PVR_LOG_IF_ERROR(eError, "PVRSRVRegisterDbgRequestNotify");

	return eError;
}

/************************************************************************/ /*!
 @Function      HTBIDeviceDestroy
 @Description   De-initialisation actions for HTB at device destruction.

 @Input         psDeviceNode    Reference to the device node in context

*/ /**************************************************************************/
void
HTBDeviceDestroy(
		PVRSRV_DEVICE_NODE *psDeviceNode
)
{
	if (psDeviceNode->hHtbDbgReqNotify)
	{
		/* No much we can do if it fails, driver unloading */
		(void)PVRSRVUnregisterDbgRequestNotify(psDeviceNode->hHtbDbgReqNotify);
		psDeviceNode->hHtbDbgReqNotify = NULL;
	}
}

static IMG_UINT32 g_ui32HTBufferSize = HTB_TL_BUFFER_SIZE_MIN;

/*
 * AppHint access routine forward definitions
 */
static PVRSRV_ERROR _HTBSetLogGroup(const PVRSRV_DEVICE_NODE *, const void *,
                                    IMG_UINT32);
static PVRSRV_ERROR _HTBReadLogGroup(const PVRSRV_DEVICE_NODE *, const void *,
                                    IMG_UINT32 *);

static PVRSRV_ERROR	_HTBSetOpMode(const PVRSRV_DEVICE_NODE *, const void *,
                                   IMG_UINT32);
static PVRSRV_ERROR _HTBReadOpMode(const PVRSRV_DEVICE_NODE *, const void *,
                                    IMG_UINT32 *);

static void _OnTLReaderOpenCallback(void *);

/************************************************************************/ /*!
 @Function      HTBInit
 @Description   Allocate and initialise the Host Trace Buffer
                The buffer size may be changed by specifying
                HTBufferSizeInKB=xxxx

 @Return        eError          Internal services call returned eError error
                                number
*/ /**************************************************************************/
PVRSRV_ERROR
HTBInit(void)
{
	void			*pvAppHintState = NULL;
	IMG_UINT32		ui32AppHintDefault;
	IMG_UINT32		ui32BufBytes;
	PVRSRV_ERROR	eError;

	if (g_sCtrl.bInitDone)
	{
		PVR_DPF((PVR_DBG_ERROR, "HTBInit: Driver already initialised"));
		return PVRSRV_ERROR_ALREADY_EXISTS;
	}

	/*
	 * Buffer Size can be configured by specifying a value in the AppHint
	 * This will only take effect at module load time so there is no query
	 * or setting mechanism available.
	 */
	PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_HTBufferSizeInKB,
										NULL,
										NULL,
	                                    APPHINT_OF_DRIVER_NO_DEVICE,
	                                    NULL);

	PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_EnableHTBLogGroup,
	                                    _HTBReadLogGroup,
	                                    _HTBSetLogGroup,
	                                    APPHINT_OF_DRIVER_NO_DEVICE,
	                                    NULL);
	PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_HTBOperationMode,
	                                    _HTBReadOpMode,
	                                    _HTBSetOpMode,
	                                    APPHINT_OF_DRIVER_NO_DEVICE,
	                                    NULL);

	/*
	 * Now get whatever values have been configured for our AppHints
	 */
	OSCreateKMAppHintState(&pvAppHintState);
	ui32AppHintDefault = HTB_TL_BUFFER_SIZE_MIN / 1024;
	OSGetKMAppHintUINT32(pvAppHintState, HTBufferSizeInKB,
						 &ui32AppHintDefault, &g_ui32HTBufferSize);
	OSFreeKMAppHintState(pvAppHintState);

	ui32BufBytes = g_ui32HTBufferSize * 1024;

	/* initialise rest of state */
	g_sCtrl.ui32BufferSize =
		(ui32BufBytes < HTB_TL_BUFFER_SIZE_MIN)
		? HTB_TL_BUFFER_SIZE_MIN
		: ui32BufBytes;
	g_sCtrl.eOpMode = HTB_OPMODE_DROPOLDEST;
	g_sCtrl.ui32LogLevel = 0;
	g_sCtrl.ui32PIDCount = 0;
	g_sCtrl.ui32PIDHead = 0;
	g_sCtrl.eLogMode = HTB_LOGMODE_ALLPID;
	g_sCtrl.bLogDropSignalled = IMG_FALSE;

	eError = OSSpinLockCreate(&g_sCtrl.hRepeatMarkerLock);
	PVR_LOG_RETURN_IF_ERROR(eError, "OSSpinLockCreate");

	g_sCtrl.bInitDone = IMG_TRUE;

	/* Log the current driver parameter setting for the HTBufferSizeInKB.
	 * We do this here as there is no other infrastructure for obtaining
	 * the value.
	 */
	if (g_ui32HTBufferSize != ui32AppHintDefault)
	{
		PVR_LOG(("Increasing HTBufferSize to %uKB", g_ui32HTBufferSize));
	}

	return PVRSRV_OK;
}

/************************************************************************/ /*!
 @Function      HTBDeInit
 @Description   Close the Host Trace Buffer and free all resources. Must
                perform a no-op if already de-initialised.

 @Return        eError          Internal services call returned eError error
                                number
*/ /**************************************************************************/
PVRSRV_ERROR
HTBDeInit( void )
{
	if (!g_sCtrl.bInitDone)
		return PVRSRV_OK;

	if (g_hTLStream)
	{
		TLStreamClose( g_hTLStream );
		g_hTLStream = NULL;
	}

	if (g_sCtrl.hRepeatMarkerLock != NULL)
	{
		OSSpinLockDestroy(g_sCtrl.hRepeatMarkerLock);
		g_sCtrl.hRepeatMarkerLock = NULL;
	}

	g_sCtrl.bInitDone = IMG_FALSE;
	return PVRSRV_OK;
}


/*************************************************************************/ /*!
 AppHint interface functions
*/ /**************************************************************************/
static
PVRSRV_ERROR _HTBSetLogGroup(const PVRSRV_DEVICE_NODE *psDeviceNode,
                             const void *psPrivate,
                             IMG_UINT32 ui32Value)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(psPrivate);

	return HTBControlKM(1, &ui32Value, 0, 0,
	                    HTB_LOGMODE_UNDEF, HTB_OPMODE_UNDEF);
}

static
PVRSRV_ERROR _HTBReadLogGroup(const PVRSRV_DEVICE_NODE *psDeviceNode,
                              const void *psPrivate,
                              IMG_UINT32 *pui32Value)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(psPrivate);

	*pui32Value = g_auiHTBGroupEnable[0];
	return PVRSRV_OK;
}

static
PVRSRV_ERROR _HTBSetOpMode(const PVRSRV_DEVICE_NODE *psDeviceNode,
                           const void *psPrivate,
                           IMG_UINT32 ui32Value)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(psPrivate);

	return HTBControlKM(0, NULL, 0, 0, HTB_LOGMODE_UNDEF, ui32Value);
}

static
PVRSRV_ERROR _HTBReadOpMode(const PVRSRV_DEVICE_NODE *psDeviceNode,
                            const void *psPrivate,
                            IMG_UINT32 *pui32Value)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(psPrivate);

	*pui32Value = (IMG_UINT32)g_sCtrl.eOpMode;
	return PVRSRV_OK;
}


static void
_OnTLReaderOpenCallback( void *pvArg )
{
	if ( g_hTLStream )
	{
		IMG_UINT64 ui64Time;
		OSClockMonotonicns64(&ui64Time);
		(void) HTBLog((IMG_HANDLE) NULL, 0, 0, ui64Time, HTB_SF_CTRL_FWSYNC_MARK_SCALE,
		              g_sCtrl.ui32SyncMarker,
		              ((IMG_UINT32)((g_sCtrl.ui64SyncOSTS>>32)&0xffffffff)),
		              ((IMG_UINT32)(g_sCtrl.ui64SyncOSTS&0xffffffff)),
		              ((IMG_UINT32)((g_sCtrl.ui64SyncCRTS>>32)&0xffffffff)),
		              ((IMG_UINT32)(g_sCtrl.ui64SyncCRTS&0xffffffff)),
		              g_sCtrl.ui32SyncCalcClkSpd);
	}

	PVR_UNREFERENCED_PARAMETER(pvArg);
}


/*************************************************************************/ /*!
 @Function      HTBControlKM
 @Description   Update the configuration of the Host Trace Buffer

 @Input         ui32NumFlagGroups Number of group enable flags words

 @Input         aui32GroupEnable  Flags words controlling groups to be logged

 @Input         ui32LogLevel    Log level to record

 @Input         ui32EnablePID   PID to enable logging for a specific process

 @Input         eLogMode        Enable logging for all or specific processes,

 @Input         eOpMode         Control the behaviour of the data buffer

 @Return        eError          Internal services call returned eError error
                                number
*/ /**************************************************************************/
PVRSRV_ERROR
HTBControlKM(
	const IMG_UINT32 ui32NumFlagGroups,
	const IMG_UINT32 * aui32GroupEnable,
	const IMG_UINT32 ui32LogLevel,
	const IMG_UINT32 ui32EnablePID,
	const HTB_LOGMODE_CTRL eLogMode,
	const HTB_OPMODE_CTRL eOpMode
)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32RetryCount = HTB_LOG_RETRY_COUNT;
	IMG_UINT32 i;
	IMG_UINT64 ui64Time;
	OSClockMonotonicns64(&ui64Time);

	if ( !g_bConfigured && ui32NumFlagGroups )
	{
		eError = TLStreamCreate(
				&g_hTLStream,
				HTB_STREAM_NAME,
				g_sCtrl.ui32BufferSize,
				_LookupFlags(HTB_OPMODE_DROPOLDEST) | g_ui32TLBaseFlags,
				_OnTLReaderOpenCallback, NULL, NULL, NULL);
		PVR_LOG_RETURN_IF_ERROR(eError, "TLStreamCreate");
		g_bConfigured = IMG_TRUE;
	}

	if (HTB_OPMODE_UNDEF != eOpMode && g_sCtrl.eOpMode != eOpMode)
	{
		g_sCtrl.eOpMode = eOpMode;
		eError = TLStreamReconfigure(g_hTLStream, _LookupFlags(g_sCtrl.eOpMode | g_ui32TLBaseFlags));
		while ( PVRSRV_ERROR_NOT_READY == eError && ui32RetryCount-- )
		{
			OSReleaseThreadQuanta();
			eError = TLStreamReconfigure(g_hTLStream, _LookupFlags(g_sCtrl.eOpMode | g_ui32TLBaseFlags));
		}
		PVR_LOG_RETURN_IF_ERROR(eError, "TLStreamReconfigure");
	}

	if ( ui32EnablePID )
	{
		g_sCtrl.aui32EnablePID[g_sCtrl.ui32PIDHead] = ui32EnablePID;
		g_sCtrl.ui32PIDHead++;
		g_sCtrl.ui32PIDHead %= HTB_MAX_NUM_PID;
		g_sCtrl.ui32PIDCount++;
		if ( g_sCtrl.ui32PIDCount > HTB_MAX_NUM_PID )
		{
			g_sCtrl.ui32PIDCount = HTB_MAX_NUM_PID;
		}
	}

	/* HTB_LOGMODE_ALLPID overrides ui32EnablePID */
	if ( HTB_LOGMODE_ALLPID == eLogMode )
	{
		OSCachedMemSet(g_sCtrl.aui32EnablePID, 0, sizeof(g_sCtrl.aui32EnablePID));
		g_sCtrl.ui32PIDCount = 0;
		g_sCtrl.ui32PIDHead = 0;
	}
	if ( HTB_LOGMODE_UNDEF != eLogMode )
	{
		g_sCtrl.eLogMode = eLogMode;
	}

	if ( ui32NumFlagGroups )
	{
		for (i = 0; i < HTB_FLAG_NUM_EL && i < ui32NumFlagGroups; i++)
		{
			g_auiHTBGroupEnable[i] = aui32GroupEnable[i];
		}
		for (; i < HTB_FLAG_NUM_EL; i++)
		{
			g_auiHTBGroupEnable[i] = 0;
		}
	}

	if ( ui32LogLevel )
	{
		g_sCtrl.ui32LogLevel = ui32LogLevel;
	}

	/* Dump the current configuration state */
	eError = HTBLog((IMG_HANDLE) NULL, 0, 0, ui64Time, HTB_SF_CTRL_OPMODE, g_sCtrl.eOpMode);
	PVR_LOG_IF_ERROR(eError, "HTBLog");
	eError = HTBLog((IMG_HANDLE) NULL, 0, 0, ui64Time, HTB_SF_CTRL_ENABLE_GROUP, g_auiHTBGroupEnable[0]);
	PVR_LOG_IF_ERROR(eError, "HTBLog");
	eError = HTBLog((IMG_HANDLE) NULL, 0, 0, ui64Time, HTB_SF_CTRL_LOG_LEVEL, g_sCtrl.ui32LogLevel);
	PVR_LOG_IF_ERROR(eError, "HTBLog");
	eError = HTBLog((IMG_HANDLE) NULL, 0, 0, ui64Time, HTB_SF_CTRL_LOGMODE, g_sCtrl.eLogMode);
	PVR_LOG_IF_ERROR(eError, "HTBLog");
	for (i = 0; i < g_sCtrl.ui32PIDCount; i++)
	{
		eError = HTBLog((IMG_HANDLE) NULL, 0, 0, ui64Time, HTB_SF_CTRL_ENABLE_PID, g_sCtrl.aui32EnablePID[i]);
		PVR_LOG_IF_ERROR(eError, "HTBLog");
	}
	/* Else should never be hit as we set the spd when the power state is updated */
	if (0 != g_sCtrl.ui32SyncMarker && 0 != g_sCtrl.ui32SyncCalcClkSpd)
	{
		eError = HTBLog((IMG_HANDLE) NULL, 0, 0, ui64Time, HTB_SF_CTRL_FWSYNC_MARK_SCALE,
				g_sCtrl.ui32SyncMarker,
				((IMG_UINT32)((g_sCtrl.ui64SyncOSTS>>32)&0xffffffff)), ((IMG_UINT32)(g_sCtrl.ui64SyncOSTS&0xffffffff)),
				((IMG_UINT32)((g_sCtrl.ui64SyncCRTS>>32)&0xffffffff)), ((IMG_UINT32)(g_sCtrl.ui64SyncCRTS&0xffffffff)),
				g_sCtrl.ui32SyncCalcClkSpd);
		PVR_LOG_IF_ERROR(eError, "HTBLog");
	}

	return eError;
}

/*************************************************************************/ /*!
*/ /**************************************************************************/
static IMG_BOOL
_ValidPID( IMG_UINT32 PID )
{
	IMG_UINT32 i;

	for (i = 0; i < g_sCtrl.ui32PIDCount; i++)
	{
		if ( g_sCtrl.aui32EnablePID[i] == PID )
		{
			return IMG_TRUE;
		}
	}
	return IMG_FALSE;
}


/*************************************************************************/ /*!
 @Function      HTBSyncPartitionMarker
 @Description   Write an HTB sync partition marker to the HTB log

 @Input         ui33Marker      Marker value

*/ /**************************************************************************/
void
HTBSyncPartitionMarker(
	const IMG_UINT32 ui32Marker
)
{
	g_sCtrl.ui32SyncMarker = ui32Marker;
	if ( g_hTLStream )
	{
		PVRSRV_ERROR eError;
		IMG_UINT64 ui64Time;
		OSClockMonotonicns64(&ui64Time);

		/* Else should never be hit as we set the spd when the power state is updated */
		if (0 != g_sCtrl.ui32SyncCalcClkSpd)
		{
			eError = HTBLog((IMG_HANDLE) NULL, 0, 0, ui64Time, HTB_SF_CTRL_FWSYNC_MARK_SCALE,
					ui32Marker,
					((IMG_UINT32)((g_sCtrl.ui64SyncOSTS>>32)&0xffffffff)), ((IMG_UINT32)(g_sCtrl.ui64SyncOSTS&0xffffffff)),
					((IMG_UINT32)((g_sCtrl.ui64SyncCRTS>>32)&0xffffffff)), ((IMG_UINT32)(g_sCtrl.ui64SyncCRTS&0xffffffff)),
					g_sCtrl.ui32SyncCalcClkSpd);
			PVR_WARN_IF_ERROR(eError, "HTBLog");
		}
	}
}

/*************************************************************************/ /*!
 @Function      HTBSyncPartitionMarkerRepeat
 @Description   Write a HTB sync partition marker to the HTB log, given
                the previous values to repeat.

 @Input         ui33Marker      Marker value
 @Input         ui64SyncOSTS    previous OSTS
 @Input         ui64SyncCRTS    previous CRTS
 @Input         ui32ClkSpeed    previous Clock speed

*/ /**************************************************************************/
void
HTBSyncPartitionMarkerRepeat(
	const IMG_UINT32 ui32Marker,
	const IMG_UINT64 ui64SyncOSTS,
	const IMG_UINT64 ui64SyncCRTS,
	const IMG_UINT32 ui32ClkSpeed
)
{
	if ( g_hTLStream )
	{
		PVRSRV_ERROR eError;
		IMG_UINT64 ui64Time;
		OSClockMonotonicns64(&ui64Time);

		/* Else should never be hit as we set the spd when the power state is updated */
		if (0 != ui32ClkSpeed)
		{
			eError = HTBLog((IMG_HANDLE) NULL, 0, 0, ui64Time, HTB_SF_CTRL_FWSYNC_MARK_SCALE,
					ui32Marker,
					((IMG_UINT32)((ui64SyncOSTS>>32)&0xffffffffU)), ((IMG_UINT32)(ui64SyncOSTS&0xffffffffU)),
					((IMG_UINT32)((ui64SyncCRTS>>32)&0xffffffffU)), ((IMG_UINT32)(ui64SyncCRTS&0xffffffffU)),
					ui32ClkSpeed);
			PVR_WARN_IF_ERROR(eError, "HTBLog");
		}
	}
}

/*************************************************************************/ /*!
 @Function      HTBSyncScale
 @Description   Write FW-Host synchronisation data to the HTB log when clocks
                change or are re-calibrated

 @Input         bLogValues      IMG_TRUE if value should be immediately written
                                out to the log

 @Input         ui32OSTS        OS Timestamp

 @Input         ui32CRTS        Rogue timestamp

 @Input         ui32CalcClkSpd  Calculated clock speed

*/ /**************************************************************************/
void
HTBSyncScale(
	const IMG_BOOL bLogValues,
	const IMG_UINT64 ui64OSTS,
	const IMG_UINT64 ui64CRTS,
	const IMG_UINT32 ui32CalcClkSpd
)
{
	g_sCtrl.ui64SyncOSTS = ui64OSTS;
	g_sCtrl.ui64SyncCRTS = ui64CRTS;
	g_sCtrl.ui32SyncCalcClkSpd = ui32CalcClkSpd;
	if (g_hTLStream && bLogValues)
	{
		PVRSRV_ERROR eError;
		IMG_UINT64 ui64Time;
		OSClockMonotonicns64(&ui64Time);
		eError = HTBLog((IMG_HANDLE) NULL, 0, 0, ui64Time, HTB_SF_CTRL_FWSYNC_MARK_SCALE,
				g_sCtrl.ui32SyncMarker,
				((IMG_UINT32)((ui64OSTS>>32)&0xffffffff)), ((IMG_UINT32)(ui64OSTS&0xffffffff)),
				((IMG_UINT32)((ui64CRTS>>32)&0xffffffff)), ((IMG_UINT32)(ui64CRTS&0xffffffff)),
				ui32CalcClkSpd);
		/*
		 * Don't spam the log with non-failure cases
		 */
		PVR_WARN_IF_ERROR(eError, "HTBLog");
	}
}


/*************************************************************************/ /*!
 @Function      HTBLogKM
 @Description   Record a Host Trace Buffer log event

 @Input         PID             The PID of the process the event is associated
                                with. This is provided as an argument rather
                                than querying internally so that events associated
                                with a particular process, but performed by
                                another can be logged correctly.

 @Input         ui64TimeStamp   The timestamp to be associated with this log event

 @Input         SF              The log event ID

 @Input         ...             Log parameters

 @Return        PVRSRV_OK       Success.

*/ /**************************************************************************/
PVRSRV_ERROR
HTBLogKM(
		IMG_UINT32 PID,
		IMG_UINT32 TID,
		IMG_UINT64 ui64TimeStamp,
		HTB_LOG_SFids SF,
		IMG_UINT32 ui32NumArgs,
		IMG_UINT32 * aui32Args
)
{
	OS_SPINLOCK_FLAGS uiSpinLockFlags;
	IMG_UINT32 ui32ReturnFlags = 0;

	/* Local snapshot variables of global counters */
	IMG_UINT64 ui64OSTSSnap;
	IMG_UINT64 ui64CRTSSnap;
	IMG_UINT32 ui32ClkSpeedSnap;

	/* format of messages is: SF:PID:TID:TIMEPT1:TIMEPT2:[PARn]*
	 * Buffer is on the stack so we don't need a semaphore to guard it
	 */
	IMG_UINT32 aui32MessageBuffer[HTB_LOG_HEADER_SIZE+HTB_LOG_MAX_PARAMS];

	/* Min HTB size is HTB_TL_BUFFER_SIZE_MIN : 10000 bytes and Max message/
	 * packet size is 4*(HTB_LOG_HEADER_SIZE+HTB_LOG_MAX_PARAMS) = 80 bytes,
	 * hence with these constraints this design is unlikely to get
	 * PVRSRV_ERROR_TLPACKET_SIZE_LIMIT_EXCEEDED error
	 */
	PVRSRV_ERROR eError = PVRSRV_ERROR_NOT_ENABLED;
	IMG_UINT32 ui32RetryCount = HTB_LOG_RETRY_COUNT;
	IMG_UINT32 * pui32Message = aui32MessageBuffer;
	IMG_UINT32 ui32MessageSize = 4 * (HTB_LOG_HEADER_SIZE+ui32NumArgs);

	PVR_LOG_GOTO_IF_INVALID_PARAM(aui32Args != NULL, eError, ReturnError);
	PVR_LOG_GOTO_IF_INVALID_PARAM(ui32NumArgs == HTB_SF_PARAMNUM(SF), eError, ReturnError);
	PVR_LOG_GOTO_IF_INVALID_PARAM(ui32NumArgs <= HTB_LOG_MAX_PARAMS, eError, ReturnError);

	if ( g_hTLStream
			&& ( 0 == PID || ~0 == PID || HTB_LOGMODE_ALLPID == g_sCtrl.eLogMode || _ValidPID(PID) )
/*			&& ( g_sCtrl.ui32GroupEnable & (0x1 << HTB_SF_GID(SF)) ) */
/*			&& ( g_sCtrl.ui32LogLevel >= HTB_SF_LVL(SF) ) */
			)
	{
		*pui32Message++ = SF;
		*pui32Message++ = PID;
		*pui32Message++ = TID;
		*pui32Message++ = ((IMG_UINT32)((ui64TimeStamp>>32)&0xffffffff));
		*pui32Message++ = ((IMG_UINT32)(ui64TimeStamp&0xffffffff));
		while ( ui32NumArgs )
		{
			ui32NumArgs--;
			pui32Message[ui32NumArgs] = aui32Args[ui32NumArgs];
		}

		eError = TLStreamWriteRetFlags( g_hTLStream, (IMG_UINT8*)aui32MessageBuffer, ui32MessageSize, &ui32ReturnFlags );
		while ( PVRSRV_ERROR_NOT_READY == eError && ui32RetryCount-- )
		{
			OSReleaseThreadQuanta();
			eError = TLStreamWriteRetFlags( g_hTLStream, (IMG_UINT8*)aui32MessageBuffer, ui32MessageSize, &ui32ReturnFlags );
		}

		if ( PVRSRV_OK == eError )
		{
			g_sCtrl.bLogDropSignalled = IMG_FALSE;
		}
		else if ( PVRSRV_ERROR_STREAM_FULL != eError || !g_sCtrl.bLogDropSignalled )
		{
			PVR_DPF((PVR_DBG_WARNING, "%s() failed (%s) in %s()", "TLStreamWrite", PVRSRVGETERRORSTRING(eError), __func__));
		}
		if ( PVRSRV_ERROR_STREAM_FULL == eError )
		{
			g_sCtrl.bLogDropSignalled = IMG_TRUE;
		}

	}

	if (SF == HTB_SF_CTRL_FWSYNC_MARK_SCALE)
	{
		OSSpinLockAcquire(g_sCtrl.hRepeatMarkerLock, uiSpinLockFlags);

		/* If a marker is being placed reset byte count from last marker */
		g_sCtrl.ui32ByteCount = 0;
		g_sCtrl.ui64OSTS = (IMG_UINT64)aui32Args[HTB_ARG_OSTS_PT1] << 32 | aui32Args[HTB_ARG_OSTS_PT2];
		g_sCtrl.ui64CRTS = (IMG_UINT64)aui32Args[HTB_ARG_CRTS_PT1] << 32 | aui32Args[HTB_ARG_CRTS_PT2];
		g_sCtrl.ui32ClkSpeed = aui32Args[HTB_ARG_CLKSPD];

		OSSpinLockRelease(g_sCtrl.hRepeatMarkerLock, uiSpinLockFlags);
	}
	else
	{
		OSSpinLockAcquire(g_sCtrl.hRepeatMarkerLock, uiSpinLockFlags);
		/* Increase global count */
		g_sCtrl.ui32ByteCount += ui32MessageSize;

		/* Check if packet has overwritten last marker/rpt &&
		   If the packet count is over half the size of the buffer */
		if (ui32ReturnFlags & TL_FLAG_OVERWRITE_DETECTED &&
				 g_sCtrl.ui32ByteCount > HTB_MARKER_PREDICTION_THRESHOLD(g_sCtrl.ui32BufferSize))
		{
			/* Take snapshot of global variables */
			ui64OSTSSnap = g_sCtrl.ui64OSTS;
			ui64CRTSSnap = g_sCtrl.ui64CRTS;
			ui32ClkSpeedSnap = g_sCtrl.ui32ClkSpeed;
			/* Reset global variable counter */
			g_sCtrl.ui32ByteCount = 0;
			OSSpinLockRelease(g_sCtrl.hRepeatMarkerLock, uiSpinLockFlags);

			/* Produce a repeat marker */
			HTBSyncPartitionMarkerRepeat(g_sCtrl.ui32SyncMarker, ui64OSTSSnap, ui64CRTSSnap, ui32ClkSpeedSnap);
		}
		else
		{
			OSSpinLockRelease(g_sCtrl.hRepeatMarkerLock, uiSpinLockFlags);
		}
	}

ReturnError:
	return eError;
}

/*************************************************************************/ /*!
 @Function      HTBIsConfigured
 @Description   Determine if HTB stream has been configured

 @Input         none

 @Return        IMG_FALSE       Stream has not been configured
                IMG_TRUE        Stream has been configured

*/ /**************************************************************************/
IMG_BOOL
HTBIsConfigured(void)
{
	return g_bConfigured;
}
/* EOF */
