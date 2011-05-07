/* Cypress West Bridge API header file (cyaserr.h)
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street
## Fifth Floor, Boston, MA  02110-1301, USA.
## ===========================
*/

#ifndef _INCLUDED_CYASERR_H_
#define _INCLUDED_CYASERR_H_

/*@@West Bridge Errors
  Summary
  This section lists the error codes for West Bridge.

*/

/* Summary
   The function completed successfully
*/
#define CY_AS_ERROR_SUCCESS	 (0)

/* Summary
   A function trying to acquire a resource was unable to do so.

   Description
   This code indicates that a resource that the API was trying to claim
   could not be claimed.

   See Also
   * CyAsMiscAcquireResource
   * CyAsStorageClaim
*/
#define CY_AS_ERROR_NOT_ACQUIRED  (1)

/* Summary
   A function trying to acquire a resource was unable to do so.

   Description
   The West Bridge API provides the capability to assign the storage media to
   either the West Bridge device or the USB port.  This error indicates the
   P port was trying to release a storage media and was not able to do
   so.  This generally means it was not owned by the P port processor.

   See Also
   * CyAsStorageRelease
*/
#define CY_AS_ERROR_NOT_RELEASED  (2)

/* Summary
   The West Bridge firmware is not loaded.

   Description
   Most of the API functions that are part of the West Bridge API rely on
   firmware running on the West Bridge device.  This error code is
   returned when one of these functions is called and the firmware has
   not yet been loaded.

   See Also
   * CyAsMiscGetFirmwareVersion
   * CyAsMiscReset
   * CyAsMiscAcquireResource
   * CyAsMiscReleaseResource
   * CyAsMiscSetTraceLevel
   * CyAsStorageStart
   * CyAsStorageStop
   * CyAsStorageRegisterCallback
   * CyAsStorageClaim
   * CyAsStorageRelease
   * CyAsStorageQueryMedia
   * CyAsStorageQueryDevice
   * CyAsStorageQueryUnit
   * CyAsStorageRead
   * CyAsStorageWrite
   * CyAsStorageReadAsync
   * CyAsStorageWriteAsync
*/
#define CY_AS_ERROR_NO_FIRMWARE (3)

/* Summary
   A timeout occurred waiting on a response from the West Bridge device

   Description
   When requests are made of the West Bridge device, a response is expected
   within a given timeframe.  If a response is not recevied within the
   given timeframe, a timeout error occurs.
*/
#define CY_AS_ERROR_TIMEOUT	 (4)

/* Summary
   A request to download firmware was made while not in the CONFIG mode

   Description
   Firmware is downloaded via the CyAsMiscDownloadFirmware() function.  This
   function can only be called while in the CONFIG mode.  This error indicates
   that the CyAsMiscDownloadFirmware() call was made while not in the CONFIG
   mode.

   See Also
   * CyAsMiscDownloadFirmware
*/
#define CY_AS_ERROR_NOT_IN_CONFIG_MODE	 (5)

/* Summary
   This error is returned if the firmware size specified is too invalid.

   Description
   If the size of the firmware to be downloaded into West Bridge is
   invalid, this error is issued.  Invalid firmware sizes are those
   greater than 24K or a size of zero.

   See Also
   * CyAsMiscDownloadFirmare
*/
#define CY_AS_ERROR_INVALID_SIZE  (6)

/* Summary
   This error is returned if a request is made to acquire a resource that has
   already been acquired.

   Description
   This error is returned if a request is made to acquire a resource that has
   already been acquired.

   See Also
   * CyAsMiscAcquireResource
   * CyAsMiscReleaseResource
*/
#define CY_AS_ERROR_RESOURCE_ALREADY_OWNED (7)

/* Summary
   This error is returned if a request is made to release a resource that has
   not previously been acquired.

   Description
   This error is returned if a request is made to release a resource that has
   not previously been acquired.

   See Also
   * CyAsMiscAcquireResource
   * CyAsMiscReleaseResource
*/
#define CY_AS_ERROR_RESOURCE_NOT_OWNED	 (8)

/* Summary
   This error is returned when a request is made for a media that
   does not exist

   Description
   This error is returned when a request is made that references
   a storage media that does not exist.  This error is returned
   when the storage media is not present in the current system,
   or if the media value given is not valid.

   See Also
   * CyAsMiscSetTraceLevel
   * CyAsStorageClaim
   * CyAsStorageRelease
   * CyAsStorageRead
   * CyAsStorageWrite
   * CyAsStorageReadAsync
   * CyAsStorageWriteAsync
*/
#define CY_AS_ERROR_NO_SUCH_MEDIA		  (9)

/* Summary
   This error is returned when a request is made for a device
   that does not exist

   Description
   This error is returned when a request is made that references a
   storage device that does not exist.  This error is returned when
   the device index is not present in the current system, or if the
   device index exceeds 15.

   See Also
   * CyAsMiscSetTraceLevel
   * CyAsStorageQueryDevice
   * CyAsStorageRead
   * CyAsStorageWrite
   * CyAsStorageReadAsync
   * CyAsStorageWriteAsync
*/
#define CY_AS_ERROR_NO_SUCH_DEVICE		 (10)

/* Summary
   This error is returned when a request is made for a unit that
   does not exist

   Description
   This error is returned when a request is made that references
   a storage unit that does not exist.  This error is returned
   when the unit index is not present in the current system, or
   if the unit index exceeds 255.

   See Also
   * CyAsMiscSetTraceLevel
   * CyAsStorageQueryDevice
   * CyAsStorageQueryUnit
   * CyAsStorageRead
   * CyAsStorageWrite
   * CyAsStorageReadAsync
   * CyAsStorageWriteAsync
*/
#define CY_AS_ERROR_NO_SUCH_UNIT  (11)

/* Summary
   This error is returned when a request is made for a block that
   does not exist

   Description
   This error is returned when a request is made that references
   a storage block that does not exist.  This error is returned
   when the block address reference an address beyond the end of
   the unit selected.

   See Also
   * CyAsStorageRead
   * CyAsStorageWrite
   * CyAsStorageReadAsync
   * CyAsStorageWriteAsync
*/
#define CY_AS_ERROR_INVALID_BLOCK		  (12)

/* Summary
   This error is returned when an invalid trace level is set.

   Description
   This error is returned when the trace level request is greater
   than three.

   See Also
   * CyAsMiscSetTraceLevel
*/
#define CY_AS_ERROR_INVALID_TRACE_LEVEL	 (13)

/* Summary
   This error is returned when West Bridge is already in the standby state
   and an attempt is made to put West Bridge into this state again.

   Description
   This error is returned when West Bridge is already in the standby state
   and an attempt is made to put West Bridge into this state again.

   See Also
   * CyAsMiscEnterStandby
*/
#define CY_AS_ERROR_ALREADY_STANDBY	 (14)

/* Summary
   This error is returned when the API needs to set a pin on the
   West Bridge device, but this is not supported by the underlying HAL
   layer.

   Description
   This error is returned when the API needs to set a pin on the
   West Bridge device, but this is not supported by the underlying HAL
   layer.

   See Also
   * CyAsMiscEnterStandby
   * CyAsMiscLeaveStandby
*/
#define CY_AS_ERROR_SETTING_WAKEUP_PIN	(15)

/* Summary
   This error is returned when a module is being started that has
   already been started.

   Description
   This error is returned when a module is being started and that module
   has already been started.  This error does not occur with the
   CyAsStorageStart() or CyAsUsbStart() functions as the storage and
   USB modules are reference counted.

   Note
   At the current time, this error is returned by module internal to
   the API but not returned by any of the API functions.
*/
#define CY_AS_ERROR_ALREADY_RUNNING	(16)

/* Summary
   This error is returned when a module is being stopped that has
   already been stopped.

   Description
   This error is returned when a module is being stopped and that module
   has already been stopped.  This error does not occur with the
   CyAsStorageStop() or CyAsUsbStop() functions as the storage and USB
   modules are reference counted.

   Note
   At the current time, this error is returned by module internal to
   the API but not returned by any of the API functions.
*/

#define CY_AS_ERROR_NOT_RUNNING (17)

/* Summary
   This error is returned when the caller tries to claim a media that
   has already been claimed.

   Description
   This error is returned when the caller tries to claim a media that
   has already been claimed.

   See Also
   * CyAsStorageClaim
*/
#define CY_AS_ERROR_MEDIA_ALREADY_CLAIMED  (18)

/* Summary
   This error is returned when the caller tries to release a media that has
   already been released.

   Description
   This error is returned when the caller tries to release a media that has
   already been released.

   See Also
   * CyAsStorageRelease
*/
#define CY_AS_ERROR_MEDIA_NOT_CLAIMED	  (19)

/* Summary
   This error is returned when canceling trying to cancel an asynchronous
   operation when an async operation is not pending.

   Description
   This error is returned when a call is made to a function to cancel an
   asynchronous operation and there is no asynchronous operation pending.

   See Also
   * CyAsStorageCancelAsync
   * CyAsUsbCancelAsync
*/
#define CY_AS_ERROR_NO_OPERATION_PENDING (20)

/* Summary
   This error is returned when an invalid endpoint number is provided to
   an API call.

   Description
   This error is returned when an invalid endpoint number is specified in
   an API call.  The endpoint number may be invalid because it is greater
   than 15, or because it was a reference to an endpoint that is invalid
   for West Bridge (2, 4, 6, or 8).

   See Also
   * CyAsUsbSetEndPointConfig
   * CyAsUsbGetEndPointConfig
   * CyAsUsbReadData
   * CyAsUsbWriteData
   * CyAsUsbReadDataAsync
   * CyAsUsbWriteDataAsync
   * CyAsUsbSetStall
   * CyAsUsbGetStall
*/
#define CY_AS_ERROR_INVALID_ENDPOINT (21)

/* Summary
   This error is returned when an invalid descriptor type
   is specified in an API call.

   Description
   This error is returned when an invalid descriptor type
   is specified in an API call.

   See Also
   * CyAsUsbSetDescriptor
   * CyAsUsbGetDescriptor
*/
#define CY_AS_ERROR_INVALID_DESCRIPTOR	 (22)

/* Summary
   This error is returned when an invalid descriptor index
   is specified in an API call.

   Description
   This error is returned when an invalid descriptor index
   is specified in an API call.

   See Also
   * CyAsUsbSetDescriptor
   * CyAsUsbGetDescriptor
*/
#define CY_AS_ERROR_BAD_INDEX (23)

/* Summary
   This error is returned if trying to set a USB descriptor
   when in the P port enumeration mode.

   Description
   This error is returned if trying to set a USB descriptor
   when in the P port enumeration mode.

   See Also
   * CyAsUsbSetDescriptor
   * CyAsUsbGetDescriptor
*/
#define CY_AS_ERROR_BAD_ENUMERATION_MODE (24)

/* Summary
   This error is returned when the endpoint configuration specified
   is not valid.

   Description
   This error is returned when the endpoint configuration specified
   is not valid.

   See Also
   * CyAsUsbSetDescriptor
   * CyAsUsbGetDescriptor
   * CyAsUsbCommitConfig
*/
#define CY_AS_ERROR_INVALID_CONFIGURATION  (25)

/* Summary
   This error is returned when the API cannot verify it is connected
   to an West Bridge device.

   Description
   When the API is initialized, the API tries to read the ID register from
   the West Bridge device.  The value from this ID register should match the
   value expected before communications with West Bridge are established.  This
   error means that the contents of the ID register cannot be verified.

   See Also
   * CyAsMiscConfigureDevice
*/
#define CY_AS_ERROR_NO_ANTIOCH			 (26)

/* Summary
   This error is returned when an API function is called and
   CyAsMiscConfigureDevice has not been called to configure West Bridge
   for the current environment.

   Description
   This error is returned when an API function is called and
   CyAsMiscConfigureDevice has not been called to configure West Bridge for
   the current environment.

   See Also
   * Almost all API function
*/
#define CY_AS_ERROR_NOT_CONFIGURED		 (27)

/* Summary
   This error is returned when West Bridge cannot allocate memory required for
   internal API operations.

   Description
   This error is returned when West Bridge cannot allocate memory required for
   internal API operations.

   See Also
   * Almost all API functoins
*/
#define CY_AS_ERROR_OUT_OF_MEMORY		  (28)

/* Summary
   This error is returned when a module is being started that has
   already been started.

   Description
   This error is returned when a module is being started and that module
   has already been started.  This error does not occur with the
   CyAsStorageStart() or CyAsUsbStart() functions as the storage and
   USB modules are reference counted.

   Note
   At the current time, this error is returned by module internal to the API but
   not returned by any of the API functions.
*/
#define CY_AS_ERROR_NESTED_SLEEP  (29)

/* Summary
   This error is returned when an operation is attempted on an endpoint that has
   been disabled.

   Description
   This error is returned when an operation is attempted on an endpoint that has
   been disabled.

   See Also
   * CyAsUsbReadData
   * CyAsUsbWriteData
   * CyAsUsbReadDataAsync
   * CyAsUsbWriteDataAsync
*/
#define CY_AS_ERROR_ENDPOINT_DISABLED	  (30)

/* Summary
   This error is returned when a call is made to an API function when
   the device is in standby.

   Description
   When the West Bridge device is in standby, the only two API functions that
   can be called are CyAsMiscInStandby() and CyAsMiscLeaveStandby().
   Calling any other API function will result in this error.

   See Also
*/
#define CY_AS_ERROR_IN_STANDBY			 (31)

/* Summary
   This error is returned when an API call is made with an invalid handle value.

   Description
   This error is returned when an API call is made with an invalid handle value.

   See Also
*/
#define CY_AS_ERROR_INVALID_HANDLE		 (32)

/* Summary
   This error is returned when an invalid response is returned from
   the West Bridge device.

   Description
   Many of the API calls result in requests made to the West Bridge
   device.  This error occurs when the response from West Bridge is
   invalid and generally indicates that the West Bridge device
   should be reset.

   See Also
*/
#define CY_AS_ERROR_INVALID_RESPONSE	(33)

/* Summary
   This error is returned from the callback function for any asynchronous
   read or write request that is canceled.

   Description
   When asynchronous requests are canceled, this error is passed to the
   callback function associated with the request to indicate that the
   request has been canceled

   See Also
   * CyAsStorageReadAsync
   * CyAsStorageWriteAsync
   * CyAsUsbReadDataAsync
   * CyAsUsbWriteDataAsync
   * CyAsStorageCancelAsync
   * CyAsUsbCancelAsync
*/
#define CY_AS_ERROR_CANCELED	(34)

/* Summary
   This error is returned when the call to create sleep channel fails
   in the HAL layer.

   Description
   This error is returned when the call to create sleep channel fails
   in the HAL layer.

   See Also
   * CyAsMiscConfigureDevice
*/
#define CY_AS_ERROR_CREATE_SLEEP_CHANNEL_FAILED	 (35)

/* Summary
   This error is returned when the call to CyAsMiscLeaveStandby
   is made and the device is not in standby.

   Description
   This error is returned when the call to CyAsMiscLeaveStandby
   is made and the device is not in standby.

   See Also
*/
#define CY_AS_ERROR_NOT_IN_STANDBY		 (36)

/* Summary
   This error is returned when the call to destroy sleep channel fails
   in the HAL layer.

   Description
   This error is returned when the call to destroy sleep channel fails
   in the HAL layer.

   See Also
   * CyAsMiscDestroyDevice
*/
#define CY_AS_ERROR_DESTROY_SLEEP_CHANNEL_FAILED (37)

/* Summary
   This error is returned when an invalid resource is specified to a call
   to CyAsMiscAcquireResource() or CyAsMiscReleaseResource()

   Description
   This error is returned when an invalid resource is specified to a call
   to CyAsMiscAcquireResource() or CyAsMiscReleaseResource()

   See Also
   * CyAsMiscAcquireResource
   * CyAsMiscReleaseResource
*/
#define CY_AS_ERROR_INVALID_RESOURCE (38)

/* Summary
   This error occurs when an operation is requested on an endpoint that has
   a currently pending async operation.

   Description
   There can only be a single asynchronous pending operation on a given
   endpoint and while the operation is pending on other operation can occur
   on the endpoint.  In addition, the device cannot enter standby while
   any asynchronous operations are pending.

   See Also
   * CyAsStorageReadAsync
   * CyAsStorageWriteAsync
   * CyAsUsbReadDataAsync
   * CyAsUsbWriteDataAsync
   * CyAsStorageRead
   * CyAsStorageWrite
   * CyAsUsbReadData
   * CyAsUsbWriteData
   * CyAsMiscEnterStandby
*/
#define CY_AS_ERROR_ASYNC_PENDING (39)

/* Summary
   This error is returned when a call to CyAsStorageCancelAsync() or
   CyAsUsbCancelAsync() is made when no asynchronous request is pending.

   Description
   This error is returned when a call to CyAsStorageCancelAsync() or
   CyAsUsbCancelAsync() is made when no asynchronous request is pending.

   See Also
   * CyAsStorageCancelAsync
   * CyAsUsbCancelAsync
*/
#define CY_AS_ERROR_ASYNC_NOT_PENDING	  (40)

/* Summary
   This error is returned when a request is made to put the West Bridge device
   into standby mode while the USB stack is still active.

   Description
   This error is returned when a request is made to put the West Bridge device
   into standby mode while the USB stack is still active.  You must call the
   function CyAsUsbStop() in order to shut down the USB stack in order to go
   into the standby mode.

   See Also
   * CyAsMiscEnterStandby
*/
#define CY_AS_ERROR_USB_RUNNING	 (41)

/* Summary
   A request for in the wrong direction was issued on an endpoint.

   Description
   This error is returned when a write is attempted on an OUT endpoint or
   a read is attempted on an IN endpoint.

   See Also
   * CyAsUsbReadData
   * CyAsUsbWriteData
   * CyAsUsbReadDataAsync
   * CyAsUsbWriteDataAsync
*/
#define CY_AS_ERROR_USB_BAD_DIRECTION	  (42)

/* Summary
   An invalid request was received

   Description
   This error is isused if an invalid request is issued.
*/
#define CY_AS_ERROR_INVALID_REQUEST	(43)

/* Summary
   An ACK request was requested while no setup packet was pending.

   Description
   This error is issued if CyAsUsbAckSetupPacket() is called when no
   setup packet is pending.
*/
#define CY_AS_ERROR_NO_SETUP_PACKET_PENDING	(44)

/* Summary
   A call was made to a API function that cannot be called from a callback.

   Description
   Only asynchronous functions can be called from within West Bridge callbacks.
   This error results when an invalid function is called from a callback.
*/
#define CY_AS_ERROR_INVALID_IN_CALLBACK	(45)

/* Summary
   A call was made to CyAsUsbSetEndPointConfig() before
   CyAsUsbSetPhysicalConfiguration() was called.

   Description
   When logical endpoints are configured, you must define the physical
   endpoint for the logical endpoint being configured.  Therefore
   CyAsUsbSetPhysicalConfiguration() must be called to define the
   physical endpoints before calling CyAsUsbSetEndPointConfig().
*/
#define CY_AS_ERROR_ENDPOINT_CONFIG_NOT_SET		(46)

/* Summary
   The physical endpoint referenced is not valid in the current physical
   configuration

   Description
   When logical endpoints are configured, you must define the physical
   endpoint for the logical endpoint being configured.  Given the
   current physical configuration, the physical endpoint referenced
   is not valid.
*/
#define CY_AS_ERROR_INVALID_PHYSICAL_ENDPOINT	(47)

/* Summary
   The data supplied to the CyAsMiscDownloadFirmware() call is not
   aligned on a  WORD (16 bit) boundary.

   Description
   Many systems have problems with the transfer of data a word at a
   time when the data is not word aligned.  For this reason, we
   require that the firmware image be aligned on a word boundary and
   be an even number of bytes.  This error is returned if these
   conditions are not met.
*/
#define CY_AS_ERROR_ALIGNMENT_ERROR	(48)

/* Summary
   A call was made to destroy the West Bridge device, but the USB
   stack or the storage stack was will running.

   Description
   Before calling CyAsMiscDestroyDevice to destroy an West Bridge
   device created via a call to CyAsMiscCreateDevice, the USB and
   STORAGE stacks much be stopped via calls to CyAsUsbStop and
   CyAsStorageStop.  This error indicates that one of these two
   stacks have not been stopped.
*/
#define CY_AS_ERROR_STILL_RUNNING	(49)

/* Summary
   A call was made to the API for a function that is not yet supported.

   Description
   There are calls that are not yet supported that may be called through
   the API.  This is done to maintain compatibility in the future with
   the API.  This error is returned if you are asking for a capability
   that does not yet exist.
*/
#define CY_AS_ERROR_NOT_YET_SUPPORTED	(50)

/* Summary
   A NULL callback was provided where a non-NULL callback was required

   Description
   When async IO function are called, a callback is required to indicate
   that the IO has completed.  This callback must be non-NULL.
*/
#define CY_AS_ERROR_NULL_CALLBACK (51)

/* Summary
   This error is returned when a request is made to put the West Bridge device
   into standby mode while the storage stack is still active.

   Description
   This error is returned when a request is made to put the West Bridge device
   into standby mode while the storage stack is still active.  You must call the
   function CyAsStorageStop() in order to shut down the storage stack in order
   to go into the standby mode.

   See Also
   * CyAsMiscEnterStandby
*/
#define CY_AS_ERROR_STORAGE_RUNNING	 (52)

/* Summary
   This error is returned when an operation is attempted that cannot be
   completed while the USB stack is connected to a USB host.

   Description
   This error is returned when an operation is attempted that cannot be
   completed while the USB stack is connected to a USB host.  In order
   to successfully complete the desired operation, CyAsUsbDisconnect()
   must be called to disconnect from the host.
*/
#define CY_AS_ERROR_USB_CONNECTED (53)

/* Summary
   This error is returned when a USB disconnect is attempted and the
   West Bridge device is not connected.

   Description
   This error is returned when a USB disconnect is attempted and the
   West Bridge device is not connected.
*/
#define CY_AS_ERROR_USB_NOT_CONNECTED	(54)

/* Summary
   This error is returned when an P2S storage operation attempted
   and data could not be read or written to the storage media.

   Description
   This error is returned when an P2S storage operation attempted
   and data could not be read or written to the storage media. If
   this error is recevied then a retry can be done.
*/
#define CY_AS_ERROR_MEDIA_ACCESS_FAILURE (55)

/* Summary
   This error is returned when an P2S storage operation attempted
   and the media is write protected.

   Description
   This error is returned when an P2S storage operation attempted
   and the media is write protected.
*/
#define CY_AS_ERROR_MEDIA_WRITE_PROTECTED (56)

/* Summary
   This error is returned when an attempt is made to cancel a request
   that has already been sent to the West Bridge.

   Description
   It is not possible to cancel an asynchronous storage read/write
   operation after the actual data transfer with the West Bridge
   has started. This error is returned if CyAsStorageCancelAsync
   is called to cancel such a request.
 */
#define CY_AS_ERROR_OPERATION_IN_TRANSIT (57)

/* Summary
   This error is returned when an invalid parameter is passed to
   one of the APIs.

   Description
   Some of the West Bridge APIs are applicable to only specific
   media types, devices etc. This error code is returned when a
   API is called with an invalid parameter type.
 */
#define CY_AS_ERROR_INVALID_PARAMETER	  (58)

/* Summary
   This error is returned if an API is not supported in the current setup.

   Description
   Some of the West Bridge APIs work only with specific device types
   or firmware images. This error is returned when such APIs are called
   when the current device or firmware does not support the invoked API
   function.
 */
#define CY_AS_ERROR_NOT_SUPPORTED		  (59)

/* Summary
   This error is returned when a call is made to one of the Storage or
   USB APIs while the device is in suspend mode.

   Description
   This error is returned when a call is made to one of the storage or
   USB APIs while the device is in suspend mode.
 */
#define CY_AS_ERROR_IN_SUSPEND			 (60)

/* Summary
   This error is returned when the call to CyAsMiscLeaveSuspend
   is made and the device is not in suspend mode.

   Description
   This error is returned when the call to CyAsMiscLeaveSuspend
   is made and the device is not in suspend mode.
 */
#define CY_AS_ERROR_NOT_IN_SUSPEND		 (61)

/* Summary
   This error is returned when a command that is disabled by USB is called.

   Description
   The remote wakeup capability should be exercised only if enabled by the
   USB host. This error is returned when the CyAsUsbSignalRemoteWakeup API
   is called when the feature has not been enabled by the USB host.
 */
#define CY_AS_ERROR_FEATURE_NOT_ENABLED	 (62)

/* Summary
   This error is returned when an Async storage read or write is called before a
   query device call is issued.

   Description
   In order for the SDK to properly set up a DMA the block size of a given media
   needs to be known. This is done by making a call to CyAsStorageQueryDevice.
   This call only needs to be made once per device. If this call is not issued
   before an Async read or write is issued this error code is returned.
   */
#define CY_AS_ERROR_QUERY_DEVICE_NEEDED	(63)

/* Summary
   This error is returned when a call is made to USB or STORAGE Start or
   Stop before a prior Start or Stop has finished.

   Description
   The USB and STORAGE start and stop functions can only be called if a
   prior start or stop function call has fully completed. This means when
   an async EX call is made you must wait until the callback for that call
   has been completed before calling  start or stop again.
   */
#define CY_AS_ERROR_STARTSTOP_PENDING	(64)

/* Summary
   This error is returned when a request is made for a bus that does not exist

   Description
   This error is returned when a request is made that references a bus
   number that does not exist.  This error is returned when the bus number
   is not present in the current system, or if the bus number given is not
   valid.

   See Also
   * CyAsMiscSetTraceLevel
   * CyAsStorageClaim
   * CyAsStorageRelease
   * CyAsStorageRead
   * CyAsStorageWrite
   * CyAsStorageReadAsync
   * CyAsStorageWriteAsync
*/
#define CY_AS_ERROR_NO_SUCH_BUS (65)

/* Summary
   This error is returned when the bus corresponding to a media type cannot
   be resolved.

   Description
   In some S-Port configurations, the same media type may be supported on
   multiple buses.  In this case, it is not possible to resolve the target
   address based on the media type.  This error indicates that only
   bus-based addressing is supported in a particular run-time
   configuration.

   See Also
   * CyAsMediaType
   * CyAsBusNumber_t
 */
#define CY_AS_ERROR_ADDRESS_RESOLUTION_ERROR (66)

/* Summary
   This error is returned when an invalid command is passed to the
   CyAsStorageSDIOSync() function.

   Description
   This error indiactes an unknown Command type was passed to the SDIO
   command handler function.
 */

#define CY_AS_ERROR_INVALID_COMMAND (67)


/* Summary
   This error is returned when an invalid function /uninitialized
   function is passed to an SDIO function.

   Description
   This error indiactes an unknown/uninitialized function number was
   passed to a SDIO function.
 */
#define CY_AS_ERROR_INVALID_FUNCTION (68)

/* Summary
   This error is returned when an invalid block size is passed to
   CyAsSdioSetBlocksize().

   Description
   This error is returned when an invalid block size (greater than
   maximum block size supported) is passed to CyAsSdioSetBlocksize().
 */

#define CY_AS_ERROR_INVALID_BLOCKSIZE	  (69)

/* Summary
   This error is returned when an tuple requested is not found.

   Description
   This error is returned when an tuple requested is not found.
 */
#define CY_AS_ERROR_TUPLE_NOT_FOUND	 (70)

/* Summary
   This error is returned when an extended IO operation to an SDIO function is
   Aborted.
   Description
   This error is returned when an extended IO operation to an SDIO function is
   Aborted.  */
#define CY_AS_ERROR_IO_ABORTED	 (71)

/* Summary
   This error is returned when an extended IO operation to an SDIO function is
   Suspended.
   Description
   This error is returned when an extended IO operation to an SDIO function is
   Suspended.	*/
#define CY_AS_ERROR_IO_SUSPENDED  (72)

/* Summary
   This error is returned when IO is attempted to a Suspended SDIO function.
   Description
   This error is returned when IO is attempted to a Suspended SDIO function. */
#define CY_AS_ERROR_FUNCTION_SUSPENDED	 (73)

/* Summary
   This error is returned if an MTP function is called before MTPStart
   has completed.
   Description
   This error is returned if an MTP function is called before MTPStart
   has completed.
*/
#define CY_AS_ERROR_MTP_NOT_STARTED	(74)

/* Summary
   This error is returned by API functions that are not valid in MTP
   mode (CyAsStorageClaim for example)
   Description
   This error is returned by API functions that are not valid in MTP
   mode (CyAsStorageClaim for example)
*/
#define CY_AS_ERROR_NOT_VALID_IN_MTP (75)

/* Summary
   This error is returned when an attempt is made to partition a
   storage device that is already partitioned.

   Description
   This error is returned when an attempt is made to partition a
   storage device that is already partitioned.
*/
#define CY_AS_ERROR_ALREADY_PARTITIONED	(76)

/* Summary
   This error is returned when a call is made to
   CyAsUsbSelectMSPartitions after CyAsUsbSetEnumConfig is called.

   Description
   This error is returned when a call is made to
   CyAsUsbSelectMSPartitions after CyAsUsbSetEnumConfig is called.
 */
#define CY_AS_ERROR_INVALID_CALL_SEQUENCE  (77)

/* Summary
   This error is returned when a StorageWrite opperation is attempted
   during an ongoing MTP transfer.
   Description
   This error is returned when a StorageWrite opperation is attempted
   during an ongoing MTP transfer. A MTP transfer is initiated by a
   call to CyAsMTPInitSendObject or CyAsMTPInitGetObject and is not
   finished until the CyAsMTPSendObjectComplete or
   CyAsMTPGetObjectComplete event is generated.
*/
#define CY_AS_ERROR_NOT_VALID_DURING_MTP (78)

/* Summary
   This error is returned when a StorageRead or StorageWrite is
   attempted while a UsbRead or UsbWrite on a Turbo endpoint (2 or 6) is
   pending, or visa versa.
   Description
   When there is a pending usb read or write on a turbo endpoint (2 or 6)
   a storage read or write call may not be performed. Similarly when there
   is a pending storage read or write a usb read or write may not be
   performed on a turbo endpoint (2 or 6).
*/
#define CY_AS_ERROR_STORAGE_EP_TURBO_EP_CONFLICT	 (79)

/* Summary
   This error is returned when processor requests to reserve greater
   number of zones than available for proc booting via lna firmware.

   Description
   Astoria does not allocate any nand zones for the processor in this case.
*/
#define CY_AS_ERROR_EXCEEDED_NUM_ZONES_AVAIL		 (80)

#endif						/* _INCLUDED_CYASERR_H_ */
