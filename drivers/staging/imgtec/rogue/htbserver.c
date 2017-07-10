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
#include "img_types.h"
#include "pvrsrv_error.h"
#include "osfunc.h"
#include "allocmem.h"
#include "pvr_notifier.h"
#include "pvrsrv.h"
#include "pvrsrv_apphint.h"

/* size of circular buffer controlling the maximum number of concurrent PIDs logged */
#define HTB_MAX_NUM_PID 8

/* number of times to try rewriting a log entry */
#define HTB_LOG_RETRY_COUNT 5

/*************************************************************************/ /*!
  Host Trace Buffer control information structure
*/ /**************************************************************************/
typedef struct
{
	IMG_CHAR *pszBufferName;        /*!< Name to use for the trace buffer,
                                         this will be required to request
                                         trace data from TL.
                                         Once set this may not be changed */

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

} HTB_CTRL_INFO;


/*************************************************************************/ /*!
*/ /**************************************************************************/
static const IMG_UINT32 MapFlags[] =
{
	0,                          /* HTB_OPMODE_UNDEF = 0 */
	TL_FLAG_RESERVE_DROP_NEWER, /* HTB_OPMODE_DROPLATEST */
	0,                          /* HTB_OPMODE_DROPOLDEST */
	TL_FLAG_RESERVE_BLOCK       /* HTB_OPMODE_BLOCK */
};

static_assert(0 == HTB_OPMODE_UNDEF,      "Unexpected value for HTB_OPMODE_UNDEF");
static_assert(1 == HTB_OPMODE_DROPLATEST, "Unexpected value for HTB_OPMODE_DROPLATEST");
static_assert(2 == HTB_OPMODE_DROPOLDEST, "Unexpected value for HTB_OPMODE_DROPOLDEST");
static_assert(3 == HTB_OPMODE_BLOCK,      "Unexpected value for HTB_OPMODE_BLOCK");

static const IMG_UINT32 g_ui32TLBaseFlags = 0; //TL_FLAG_NO_SIGNAL_ON_COMMIT

/* Minimum TL buffer size,
 * large enough for around 60 worst case messages or 200 average messages
 */
#define HTB_TL_BUFFER_SIZE_MIN	(0x10000)


static HTB_CTRL_INFO g_sCtrl  = {0};
static IMG_BOOL g_bConfigured = IMG_FALSE;
static IMG_HANDLE g_hTLStream = NULL;


/************************************************************************/ /*!
 @Function      _LookupFlags
 @Description   Convert HTBuffer Operation mode to TLStream flags

 @Input         eModeHTBuffer   Operation Mode

 @Return        IMG_UINT32      TLStream FLags
*/ /**************************************************************************/
static IMG_UINT32
_LookupFlags( HTB_OPMODE_CTRL eMode )
{
	return (eMode < (sizeof(MapFlags)/sizeof(MapFlags[0])))? MapFlags[eMode]: 0;
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

	if (ui32VerbLevel == DEBUG_REQUEST_VERBOSITY_HIGH)
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
	PVRSRV_ERROR eError = PVRSRV_OK;

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
	if (g_hTLStream)
	{
		TLStreamClose( g_hTLStream );
		g_hTLStream = NULL;
	}

	if (g_sCtrl.pszBufferName)
	{
		OSFreeMem( g_sCtrl.pszBufferName );
		g_sCtrl.pszBufferName = NULL;
	}

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

/*************************************************************************/ /*!
 @Function      HTBConfigureKM
 @Description   Configure or update the configuration of the Host Trace Buffer

 @Input         ui32NameSize    Size of the pszName string

 @Input         pszName         Name to use for the underlying data buffer

 @Input         ui32BufferSize  Size of the underlying data buffer

 @Return        eError          Internal services call returned eError error
                                number
*/ /**************************************************************************/
PVRSRV_ERROR
HTBConfigureKM(
		IMG_UINT32 ui32NameSize,
		const IMG_CHAR * pszName,
		const IMG_UINT32 ui32BufferSize
)
{
	if ( !g_sCtrl.pszBufferName )
	{
		g_sCtrl.ui32BufferSize = (ui32BufferSize < HTB_TL_BUFFER_SIZE_MIN)? HTB_TL_BUFFER_SIZE_MIN: ui32BufferSize;
		ui32NameSize = (ui32NameSize > PRVSRVTL_MAX_STREAM_NAME_SIZE)? PRVSRVTL_MAX_STREAM_NAME_SIZE: ui32NameSize;
		g_sCtrl.pszBufferName = OSAllocMem(ui32NameSize * sizeof(IMG_CHAR));
		OSStringNCopy(g_sCtrl.pszBufferName, pszName, ui32NameSize);
		g_sCtrl.pszBufferName[ui32NameSize-1] = 0;

		/* initialise rest of state */
		g_sCtrl.eOpMode = HTB_OPMODE_DROPLATEST;
		g_sCtrl.ui32LogLevel = 0;
		g_sCtrl.ui32PIDCount = 0;
		g_sCtrl.ui32PIDHead = 0;
		g_sCtrl.eLogMode = HTB_LOGMODE_ALLPID;
		g_sCtrl.bLogDropSignalled = IMG_FALSE;

		PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_EnableHTBLogGroup,
		                                    _HTBReadLogGroup,
		                                    _HTBSetLogGroup,
		                                    NULL,
		                                    NULL);
		PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_HTBOperationMode,
		                                    _HTBReadOpMode,
		                                    _HTBSetOpMode,
		                                    NULL,
		                                    NULL);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "HTBConfigureKM: Reconfiguration is not supported\n"));
	}

	return PVRSRV_OK;
}


static void
_OnTLReaderOpenCallback( void *pvArg )
{
	if ( g_hTLStream )
	{
		PVRSRV_ERROR eError;
		IMG_UINT32 ui32Time = OSClockus();
		eError = HTBLog(0, 0, ui32Time, HTB_SF_CTRL_FWSYNC_SCALE,
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
	IMG_UINT32 ui32Time = OSClockus();

	if ( !g_bConfigured && g_sCtrl.pszBufferName && ui32NumFlagGroups )
	{
		eError = TLStreamCreate(
				&g_hTLStream,
				g_sCtrl.pszBufferName,
				g_sCtrl.ui32BufferSize,
				_LookupFlags(HTB_OPMODE_DROPLATEST) | g_ui32TLBaseFlags,
				_OnTLReaderOpenCallback, NULL, NULL, NULL );
		PVR_LOGR_IF_ERROR( eError, "TLStreamCreate");
		g_bConfigured = IMG_TRUE;
	}

	if ( HTB_OPMODE_UNDEF != eOpMode && g_sCtrl.eOpMode != eOpMode)
	{
		g_sCtrl.eOpMode = eOpMode;
		eError = TLStreamReconfigure(g_hTLStream, _LookupFlags(g_sCtrl.eOpMode | g_ui32TLBaseFlags));
		while ( PVRSRV_ERROR_NOT_READY == eError && ui32RetryCount-- )
		{
			OSReleaseThreadQuanta();
			eError = TLStreamReconfigure(g_hTLStream, _LookupFlags(g_sCtrl.eOpMode | g_ui32TLBaseFlags));
		}
		PVR_LOGR_IF_ERROR( eError, "TLStreamReconfigure");
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
	eError = HTBLog(0, 0, ui32Time, HTB_SF_CTRL_OPMODE, g_sCtrl.eOpMode);
	PVR_LOG_IF_ERROR( eError, "HTBLog");
	eError = HTBLog(0, 0, ui32Time, HTB_SF_CTRL_ENABLE_GROUP, g_auiHTBGroupEnable[0]);
	PVR_LOG_IF_ERROR( eError, "HTBLog");
	eError = HTBLog(0, 0, ui32Time, HTB_SF_CTRL_LOG_LEVEL, g_sCtrl.ui32LogLevel);
	PVR_LOG_IF_ERROR( eError, "HTBLog");
	eError = HTBLog(0, 0, ui32Time, HTB_SF_CTRL_LOGMODE, g_sCtrl.eLogMode);
	PVR_LOG_IF_ERROR( eError, "HTBLog");
	for (i = 0; i < g_sCtrl.ui32PIDCount; i++)
	{
		eError = HTBLog(0, 0, ui32Time, HTB_SF_CTRL_ENABLE_PID, g_sCtrl.aui32EnablePID[i]);
		PVR_LOG_IF_ERROR( eError, "HTBLog");
	}

	if (0 != g_sCtrl.ui32SyncMarker && 0 != g_sCtrl.ui32SyncCalcClkSpd)
	{
		eError = HTBLog(0, 0, ui32Time, HTB_SF_CTRL_FWSYNC_MARK_RPT,
				g_sCtrl.ui32SyncMarker);
		PVR_LOG_IF_ERROR( eError, "HTBLog");
		eError = HTBLog(0, 0, ui32Time, HTB_SF_CTRL_FWSYNC_SCALE_RPT, 
				((IMG_UINT32)((g_sCtrl.ui64SyncOSTS>>32)&0xffffffff)), ((IMG_UINT32)(g_sCtrl.ui64SyncOSTS&0xffffffff)),
				((IMG_UINT32)((g_sCtrl.ui64SyncCRTS>>32)&0xffffffff)), ((IMG_UINT32)(g_sCtrl.ui64SyncCRTS&0xffffffff)),
				g_sCtrl.ui32SyncCalcClkSpd);
		PVR_LOG_IF_ERROR( eError, "HTBLog");
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
		IMG_UINT32 ui32Time = OSClockus();
		eError = HTBLog(0, 0, ui32Time, HTB_SF_CTRL_FWSYNC_MARK, ui32Marker);
		if (PVRSRV_OK != eError)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s() failed (%s) in %s()", "HTBLog", PVRSRVGETERRORSTRING(eError), __func__));
		}
		if (0 != g_sCtrl.ui32SyncCalcClkSpd)
		{
			eError = HTBLog(0, 0, ui32Time, HTB_SF_CTRL_FWSYNC_SCALE,
					((IMG_UINT32)((g_sCtrl.ui64SyncOSTS>>32)&0xffffffff)), ((IMG_UINT32)(g_sCtrl.ui64SyncOSTS&0xffffffff)),
					((IMG_UINT32)((g_sCtrl.ui64SyncCRTS>>32)&0xffffffff)), ((IMG_UINT32)(g_sCtrl.ui64SyncCRTS&0xffffffff)),
					g_sCtrl.ui32SyncCalcClkSpd);
			if (PVRSRV_OK != eError)
			{
				PVR_DPF((PVR_DBG_WARNING, "%s() failed (%s) in %s()", "HTBLog", PVRSRVGETERRORSTRING(eError), __func__));
			}
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
	if ( g_hTLStream && bLogValues)
	{
		PVRSRV_ERROR eError;
		IMG_UINT32 ui32Time = OSClockus();
		eError = HTBLog(0, 0, ui32Time, HTB_SF_CTRL_FWSYNC_SCALE,
				((IMG_UINT32)((ui64OSTS>>32)&0xffffffff)), ((IMG_UINT32)(ui64OSTS&0xffffffff)),
				((IMG_UINT32)((ui64CRTS>>32)&0xffffffff)), ((IMG_UINT32)(ui64CRTS&0xffffffff)),
				ui32CalcClkSpd);
		PVR_DPF((PVR_DBG_WARNING, "%s() failed (%s) in %s()", "HTBLog", PVRSRVGETERRORSTRING(eError), __func__));
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

 @Input			ui32TimeStamp	The timestamp to be associated with this log event

 @Input         SF    			The log event ID

 @Input			...				Log parameters

 @Return        PVRSRV_OK       Success.

*/ /**************************************************************************/
PVRSRV_ERROR
HTBLogKM(
		IMG_UINT32 PID,
		IMG_UINT32 ui32TimeStamp,
		HTB_LOG_SFids SF,
		IMG_UINT32 ui32NumArgs,
		IMG_UINT32 * aui32Args
)
{
	/* format of messages is: SF:PID:TIME:[PARn]*
	 * 32-bit timestamp (us) gives about 1h before looping
	 * Buffer allocated on the stack so don't need a semaphore to guard it
	 */
	IMG_UINT32 aui32MessageBuffer[HTB_LOG_HEADER_SIZE+HTB_LOG_MAX_PARAMS];

	PVRSRV_ERROR eError = PVRSRV_ERROR_NOT_ENABLED;
	IMG_UINT32 ui32RetryCount = HTB_LOG_RETRY_COUNT;
	IMG_UINT32 * pui32Message = aui32MessageBuffer;
	IMG_UINT32 ui32MessageSize = 4 * (HTB_LOG_HEADER_SIZE+ui32NumArgs);

	if ( g_hTLStream
			&& ( 0 == PID || ~0 == PID || HTB_LOGMODE_ALLPID == g_sCtrl.eLogMode || _ValidPID(PID) )
/*			&& ( g_sCtrl.ui32GroupEnable & (0x1 << HTB_SF_GID(SF)) ) */
/*			&& ( g_sCtrl.ui32LogLevel >= HTB_SF_LVL(SF) ) */
			)
	{
		*pui32Message++ = SF;
		*pui32Message++ = PID;
		*pui32Message++ = ui32TimeStamp;
		while ( ui32NumArgs )
		{
			ui32NumArgs--;
			pui32Message[ui32NumArgs] = aui32Args[ui32NumArgs];
		}

		eError = TLStreamWrite( g_hTLStream, (IMG_UINT8*)aui32MessageBuffer, ui32MessageSize );
		while ( PVRSRV_ERROR_NOT_READY == eError && ui32RetryCount-- )
		{
			OSReleaseThreadQuanta();
			eError = TLStreamWrite( g_hTLStream, (IMG_UINT8*)aui32MessageBuffer, ui32MessageSize );
		}

		if ( PVRSRV_OK == eError )
		{
			g_sCtrl.bLogDropSignalled = IMG_FALSE;
		}
		else if ( PVRSRV_ERROR_STREAM_RESERVE_TOO_BIG != eError || !g_sCtrl.bLogDropSignalled )
		{
			PVR_DPF((PVR_DBG_WARNING, "%s() failed (%s) in %s()", "TLStreamWrite", PVRSRVGETERRORSTRING(eError), __func__));
		}
		if ( PVRSRV_ERROR_STREAM_RESERVE_TOO_BIG == eError )
		{
			g_sCtrl.bLogDropSignalled = IMG_TRUE;
		}
	}

	return eError;
}

/* EOF */


