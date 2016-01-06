/*************************************************************************/ /*!
@File
@Title          RGX Timer queries
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX Timer queries functionality
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

#if ! defined (_RGX_TIMERQUERIES_H_)
#define _RGX_TIMERQUERIES_H_

#include "pvrsrv_error.h"
#include "img_types.h"
#include "device.h"
#include "rgxdevice.h"

/**************************************************************************/ /*!
@Function       PVRSRVRGXBeginTimerQuery
@Description    Opens a new timer query.

@Input          ui32QueryId an identifier between [ 0 and RGX_MAX_TIMER_QUERIES - 1 ]
@Return         PVRSRV_OK on success.
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVRGXBeginTimerQueryKM(PVRSRV_DEVICE_NODE * psDeviceNode,
                           IMG_UINT32         ui32QueryId);


/**************************************************************************/ /*!
@Function       PVRSRVRGXEndTimerQuery
@Description    Closes a timer query

                The lack of ui32QueryId argument expresses the fact that there can't
                be overlapping queries open.
@Return         PVRSRV_OK on success.
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVRGXEndTimerQueryKM(PVRSRV_DEVICE_NODE * psDeviceNode);



/**************************************************************************/ /*!
@Function       PVRSRVRGXQueryTimer
@Description    Queries the state of the specified timer

@Input          ui32QueryId an identifier between [ 0 and RGX_MAX_TIMER_QUERIES - 1 ]
@Out            pui64StartTime
@Out            pui64EndTime
@Return         PVRSRV_OK                         on success.
                PVRSRV_ERROR_RESOURCE_UNAVAILABLE if the device is still busy with
                                                  operations from the queried period
                other error code                  otherwise
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVRGXQueryTimerKM(PVRSRV_DEVICE_NODE * psDeviceNode,
                      IMG_UINT32         ui32QueryId,
                      IMG_UINT64         * pui64StartTime,
                      IMG_UINT64         * pui64EndTime);


/**************************************************************************/ /*!
@Function       PVRSRVRGXCurrentTime
@Description    Returns the current state of the timer used in timer queries
@Input          psDevData  Device data.
@Out            pui64Time
@Return         PVRSRV_OK on success.
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVRGXCurrentTime(PVRSRV_DEVICE_NODE * psDeviceNode,
                     IMG_UINT64         * pui64Time);


/******************************************************************************
 NON BRIDGED/EXPORTED interface
******************************************************************************/

/* write the timestamp cmd from the helper*/
IMG_VOID
RGXWriteTimestampCommand(IMG_PBYTE            * ppui8CmdPtr,
                         RGXFWIF_CCB_CMD_TYPE eCmdType,
                         RGXFWIF_DEV_VIRTADDR pTimestamp);

/* get the relevant data from the Kick to the helper*/
IMG_VOID
RGX_GetTimestampCmdHelper(PVRSRV_RGXDEV_INFO   * psDevInfo,
                          RGXFWIF_DEV_VIRTADDR * ppPreTimestamp,
                          RGXFWIF_DEV_VIRTADDR * ppPostTimestamp,
                          PRGXFWIF_UFO_ADDR    * ppUpdate);

#endif /* _RGX_TIMERQUERIES_H_ */

/******************************************************************************
 End of file (rgxtimerquery.h)
******************************************************************************/
