/*************************************************************************/ /*!
@File           htbserver.h
@Title          Host Trace Buffer server implementation.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved

@Description    Host Trace Buffer provides a mechanism to log Host events to a
                buffer in a similar way to the Firmware Trace mechanism.
                Host Trace Buffer logs data using a Transport Layer buffer.
                The Transport Layer and pvrtld tool provides the mechanism to
                retrieve the trace data.

                A Host Trace can be merged with a corresponding Firmware Trace.
                This is achieved by inserting synchronisation data into both
                traces and post processing to merge them.

                The FW Trace will contain a "Sync Partition Marker". This is
                updated every time the RGX is brought out of reset (RGX clock
                timestamps reset at this point) and is repeated when the FW
                Trace buffer wraps to ensure there is always at least 1
                partition marker in the Firmware Trace buffer whenever it is
                read.

                The Host Trace will contain corresponding "Sync Partition
                Markers" - #HTBSyncPartitionMarker(). Each partition is then
                subdivided into "Sync Scale" sections - #HTBSyncScale(). The
                "Sync Scale" data allows the timestamps from the two traces to
                be correlated. The "Sync Scale" data is updated as part of the
                standard RGX time correlation code (rgxtimecorr.c) and is
                updated periodically including on power and clock changes.

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

#ifndef __HTBSERVER_H__
#define __HTBSERVER_H__

#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvrsrv.h"
#include "htbuffer.h"


/************************************************************************/ /*!
 @Function      HTBIDeviceCreate
 @Description   Initialisation actions for HTB at device creation.

 @Input         psDeviceNode    Reference to the device node in context

 @Return        eError          Internal services call returned eError error
                                number
*/ /**************************************************************************/
PVRSRV_ERROR
HTBDeviceCreate(
		PVRSRV_DEVICE_NODE *psDeviceNode
);


/************************************************************************/ /*!
 @Function      HTBIDeviceDestroy
 @Description   De-initialisation actions for HTB at device destruction.

 @Input         psDeviceNode    Reference to the device node in context

*/ /**************************************************************************/
void
HTBDeviceDestroy(
		PVRSRV_DEVICE_NODE *psDeviceNode
);


/************************************************************************/ /*!
 @Function      HTBDeInit
 @Description   Close the Host Trace Buffer and free all resources

 @Return        eError          Internal services call returned eError error
                                number
*/ /**************************************************************************/
PVRSRV_ERROR
HTBDeInit( void );


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
);


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
);


/*************************************************************************/ /*!
 @Function      HTBSyncPartitionMarker
 @Description   Write an HTB sync partition marker to the HTB log

 @Input         ui33Marker      Marker value

*/ /**************************************************************************/
void
HTBSyncPartitionMarker(
	const IMG_UINT32 ui32Marker
);


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
);


/*************************************************************************/ /*!
 @Function      HTBLogKM
 @Description   Record a Host Trace Buffer log event

 @Input         PID             The PID of the process the event is associated
                                with. This is provided as an argument rather
                                than querying internally so that events associated
                                with a particular process, but performed by
                                another can be logged correctly.

 @Input         ui32TimeStamp   The timestamp to be associated with this log event

 @Input         SF              The log event ID

 @Input         ...             Log parameters

 @Return        PVRSRV_OK       Success.

*/ /**************************************************************************/
PVRSRV_ERROR
HTBLogKM(
		IMG_UINT32 PID,
		IMG_UINT32 ui32TimeStamp,
		HTB_LOG_SFids SF,
		IMG_UINT32 ui32NumArgs,
		IMG_UINT32 * aui32Args
);


#endif /* __HTBSERVER_H__ */

/* EOF */

