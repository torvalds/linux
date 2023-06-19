/*************************************************************************/ /*!
@File
@Title          RGX API Header kernel mode
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Exported RGX API details
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

#ifndef RGXAPI_KM_H
#define RGXAPI_KM_H

#include "rgx_hwperf.h"


/******************************************************************************
 * RGX HW Performance Profiling Control API(s)
 *****************************************************************************/

/*! HWPerf device identification structure */
typedef struct _RGX_HWPERF_DEVICE_
{
	IMG_CHAR pszName[20];	/*!< Helps identify this device uniquely */
	IMG_HANDLE hDevData;	/*!< Handle for the server */

	struct _RGX_HWPERF_DEVICE_ *psNext;  /*!< Next device if any */
} RGX_HWPERF_DEVICE;

/*! HWPerf connection structure */
typedef struct
{
	RGX_HWPERF_DEVICE *psHWPerfDevList;  /*!< pointer to list of devices */
} RGX_HWPERF_CONNECTION;

/*************************************************************************/ /*!
@Function       RGXHWPerfLazyConnect
@Description    Obtain a HWPerf connection object to the RGX device(s). The
                connections to devices are not actually opened until
                HWPerfOpen() is called.

@Output         ppsHWPerfConnection Address of a HWPerf connection object
@Return         PVRSRV_ERROR        System error code
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfLazyConnect(RGX_HWPERF_CONNECTION** ppsHWPerfConnection);


/*************************************************************************/ /*!
@Function       RGXHWPerfOpen
@Description    Opens connection(s) to the RGX device(s). Valid handle to the
                connection object has to be provided which means the this
                function needs to be preceded by the call to
                RGXHWPerfLazyConnect() function.

@Input          psHWPerfConnection HWPerf connection object
@Return         PVRSRV_ERROR       System error code
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfOpen(RGX_HWPERF_CONNECTION* psHWPerfConnection);


/*************************************************************************/ /*!
@Function       RGXHWPerfConnect
@Description    Obtain a connection object to the RGX HWPerf module. Allocated
                connection object(s) reference opened connection(s). Calling
                this function is an equivalent of calling RGXHWPerfLazyConnect
                and RGXHWPerfOpen. This connect should be used when the caller
                will be retrieving event data.

@Output         ppsHWPerfConnection Address of HWPerf connection object
@Return         PVRSRV_ERROR        System error code
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfConnect(RGX_HWPERF_CONNECTION** ppsHWPerfConnection);


/*************************************************************************/ /*!
@Function       RGXHWPerfFreeConnection
@Description    Frees the HWPerf connection object

@Input          psHWPerfConnection Pointer to connection object as returned
                                    from RGXHWPerfLazyConnect()
@Return         PVRSRV_ERROR       System error code
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfFreeConnection(RGX_HWPERF_CONNECTION** psHWPerfConnection);


/*************************************************************************/ /*!
@Function       RGXHWPerfClose
@Description    Closes all the opened connection(s) to RGX device(s)

@Input          psHWPerfConnection Pointer to HWPerf connection object as
                                    returned from RGXHWPerfConnect() or
                                    RGXHWPerfOpen()
@Return         PVRSRV_ERROR       System error code
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfClose(RGX_HWPERF_CONNECTION *psHWPerfConnection);


/*************************************************************************/ /*!
@Function       RGXHWPerfDisconnect
@Description    Disconnect from the RGX device

@Input          ppsHWPerfConnection Pointer to HWPerf connection object as
                                     returned from RGXHWPerfConnect() or
                                     RGXHWPerfOpen(). Calling this function is
                                     an equivalent of calling RGXHWPerfClose()
                                     and RGXHWPerfFreeConnection().
@Return         PVRSRV_ERROR        System error code
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfDisconnect(RGX_HWPERF_CONNECTION** ppsHWPerfConnection);


/*************************************************************************/ /*!
@Function       RGXHWPerfControl
@Description    Enable or disable the generation of RGX HWPerf event packets.
                 See RGXCtrlHWPerf().

@Input          psHWPerfConnection Pointer to HWPerf connection object
@Input          eStreamId          ID of the HWPerf stream
@Input          bToggle            Switch to toggle or apply mask.
@Input          ui64Mask           Mask of events to control.
@Return         PVRSRV_ERROR       System error code
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfControl(
		RGX_HWPERF_CONNECTION *psHWPerfConnection,
		RGX_HWPERF_STREAM_ID eStreamId,
		IMG_BOOL             bToggle,
		IMG_UINT64           ui64Mask);


/*************************************************************************/ /*!
@Function       RGXHWPerfGetFilter
@Description    Reads HWPerf stream filter where stream is identified by the
                 given stream ID.

@Input          hDevData     Handle to connection/device object
@Input          eStreamId    ID of the HWPerf stream
@Output         ui64Filter   HWPerf filter value
@Return         PVRSRV_ERROR System error code
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfGetFilter(
		IMG_HANDLE  hDevData,
		RGX_HWPERF_STREAM_ID eStreamId,
		IMG_UINT64 *ui64Filter
);


/*************************************************************************/ /*!
@Function       RGXHWPerfConfigureCounters
@Description    Enable and configure the performance counter block for one or
                 more device layout modules.
                 See RGXConfigHWPerfCounters().

@Input          psHWPerfConnection Pointer to HWPerf connection object
@Input          ui32CtrlWord       One of <tt>RGX_HWPERF_CTRL_NOP</tt>,
                                    <tt>RGX_HWPERF_CTRL_GEOM_FULLrange</tt>,
                                    <tt>RGX_HWPERF_CTRL_COMP_FULLRANGE</tt>,
                                    <tt>RGX_HWPERF_CTRL_TDM_FULLRANGE</tt>
@Input          ui32NumBlocks      Number of elements in the array
@Input          asBlockConfigs     Address of the array of configuration blocks
@Return         PVRSRV_ERROR       System error code
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfConfigureCounters(
		RGX_HWPERF_CONNECTION *psHWPerfConnection,
		IMG_UINT32                 ui32CtrlWord,
		IMG_UINT32                 ui32NumBlocks,
		RGX_HWPERF_CONFIG_CNTBLK*  asBlockConfigs);


/*************************************************************************/ /*!
@Function       RGXHWPerfDisableCounters
@Description    Disable the performance counter block for one or more device
                 layout modules.

@Input          psHWPerfConnection Pointer to HWPerf connection object
@Input          ui32NumBlocks      Number of elements in the array
@Input          aeBlockIDs         An array of words with values taken from
                                    the RGX_HWPERF_CNTBLK_ID enumeration.
@Return         PVRSRV_ERROR       System error code
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfDisableCounters(
		RGX_HWPERF_CONNECTION *psHWPerfConnection,
		IMG_UINT32   ui32NumBlocks,
		IMG_UINT16*  aeBlockIDs);

/*************************************************************************/ /*!
@Function       RGXHWPerfEnableCounters
@Description    Enable the performance counter block for one or more device
                 layout modules.

@Input          psHWPerfConnection Pointer to HWPerf connection object
@Input          ui32NumBlocks      Number of elements in the array
@Input          aeBlockIDs         An array of words with values taken from the
                                    <tt>RGX_HWPERF_CNTBLK_ID</tt> enumeration.
@Return         PVRSRV_ERROR       System error code
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfEnableCounters(
		RGX_HWPERF_CONNECTION *psHWPerfConnection,
		IMG_UINT32   ui32NumBlocks,
		IMG_UINT16*  aeBlockIDs);

/******************************************************************************
 * RGX HW Performance Profiling Retrieval API(s)
 *
 * The client must ensure their use of this acquire/release API for a single
 * connection/stream must not be shared with multiple execution contexts e.g.
 * between a kernel thread and an ISR handler. It is the client's
 * responsibility to ensure this API is not interrupted by a high priority
 * thread/ISR
 *****************************************************************************/

/*************************************************************************/ /*!
@Function       RGXHWPerfAcquireEvents
@Description    When there is data available to read this call returns with OK
                 and the address and length of the data buffer the client can
                 safely read. This buffer may contain one or more event packets.
                 When there is no data to read, this call returns with OK and
                 sets *puiBufLen to 0 on exit.
                 Clients must pair this call with a RGXHWPerfReleaseEvents()
                 call.
                 Data returned in ppBuf will be in the form of a sequence of
                 HWPerf packets which should be traversed using the pointers,
                 structures and macros provided in rgx_hwperf.h

@Input          hDevData     Handle to connection/device object
@Input          eStreamId    ID of the HWPerf stream
@Output         ppBuf        Address of a pointer to a byte buffer. On exit it
                              contains the address of buffer to read from
@Output         pui32BufLen  Pointer to an integer. On exit it is the size of
                              the data to read from the buffer
@Return         PVRSRV_ERROR System error code
*/ /**************************************************************************/
PVRSRV_ERROR RGXHWPerfAcquireEvents(
		IMG_HANDLE  hDevData,
		RGX_HWPERF_STREAM_ID eStreamId,
		IMG_PBYTE*  ppBuf,
		IMG_UINT32* pui32BufLen);


/*************************************************************************/ /*!
@Function       RGXHWPerfReleaseEvents
@Description    Called after client has read the event data out of the buffer
                 retrieved from the Acquire Events call to release resources.
@Input          hDevData     Handle to connection/device object
@Input          eStreamId    ID of the HWPerf stream
@Return         PVRSRV_ERROR System error code
*/ /**************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR RGXHWPerfReleaseEvents(
		IMG_HANDLE hDevData,
		RGX_HWPERF_STREAM_ID eStreamId);


/*************************************************************************/ /*!
@Function       RGXHWPerfConvertCRTimeStamp
@Description    Converts the timestamp given by FW events to the common OS
                 timestamp. The first three inputs are obtained via a CLK_SYNC
                 event, ui64CRTimeStamp is the CR timestamp from the FW event
                 to be converted.
@Input          ui32ClkSpeed        Clock speed given by sync event
@Input          ui64CorrCRTimeStamp CR Timestamp given by sync event
@Input          ui64CorrOSTimeStamp Correlating OS Timestamp given by sync
                                     event
@Input          ui64CRTimeStamp     CR Timestamp to convert
@Return         IMG_UINT64          Calculated OS Timestamp
*/ /**************************************************************************/
IMG_UINT64 RGXHWPerfConvertCRTimeStamp(
		IMG_UINT32 ui32ClkSpeed,
		IMG_UINT64 ui64CorrCRTimeStamp,
		IMG_UINT64 ui64CorrOSTimeStamp,
		IMG_UINT64 ui64CRTimeStamp);

#endif /* RGXAPI_KM_H */

/******************************************************************************
 End of file (rgxapi_km.h)
******************************************************************************/
