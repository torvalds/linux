/*  Cypress West Bridge API header file (cyanerr.h)
 ## Symbols for backward compatibility with previous releases of Antioch SDK.
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

#ifndef _INCLUDED_CYANERR_H_
#define _INCLUDED_CYANERR_H_

#include "cyaserr.h"

#ifndef __doxygen__

/*
 * Function completed successfully.
 */
#define CY_AN_ERROR_SUCCESS	(CY_AS_ERROR_SUCCESS)

/*
 * A function trying to acquire a resource was unable to do so.
 */
#define CY_AN_ERROR_NOT_ACQUIRED (CY_AS_ERROR_NOT_ACQUIRED)

/*
 * A function trying to acquire a resource was unable to do so.
 */
#define CY_AN_ERROR_NOT_RELEASED (CY_AS_ERROR_NOT_RELEASED)

/*
 * The West Bridge firmware is not loaded.
 */
#define CY_AN_ERROR_NO_FIRMWARE (CY_AS_ERROR_NO_FIRMWARE)

/*
 * A timeout occurred waiting on a response from the West Bridge device
 */
#define CY_AN_ERROR_TIMEOUT (CY_AS_ERROR_TIMEOUT)

/*
 * A request to download firmware was made while not in the CONFIG mode
 */
#define CY_AN_ERROR_NOT_IN_CONFIG_MODE (CY_AS_ERROR_NOT_IN_CONFIG_MODE)

/*
 * This error is returned if the firmware size specified is too invalid.
 */
#define CY_AN_ERROR_INVALID_SIZE (CY_AS_ERROR_INVALID_SIZE)

/*
 * This error is returned if a request is made to acquire a resource that has
 * already been acquired.
 */
#define CY_AN_ERROR_RESOURCE_ALREADY_OWNED (CY_AS_ERROR_RESOURCE_ALREADY_OWNED)

/*
 * This error is returned if a request is made to release a resource that has
 * not previously been acquired.
 */
#define CY_AN_ERROR_RESOURCE_NOT_OWNED (CY_AS_ERROR_RESOURCE_NOT_OWNED)

/*
 * This error is returned when a request is made for a media that does not
 * exist
 */
#define CY_AN_ERROR_NO_SUCH_MEDIA (CY_AS_ERROR_NO_SUCH_MEDIA)

/*
 * This error is returned when a request is made for a device that does
 * not exist
 */
#define CY_AN_ERROR_NO_SUCH_DEVICE (CY_AS_ERROR_NO_SUCH_DEVICE)

/*
 * This error is returned when a request is made for a unit that does
 * not exist
 */
#define CY_AN_ERROR_NO_SUCH_UNIT (CY_AS_ERROR_NO_SUCH_UNIT)

/*
 * This error is returned when a request is made for a block that does
 * not exist
 */
#define CY_AN_ERROR_INVALID_BLOCK (CY_AS_ERROR_INVALID_BLOCK)

/*
 * This error is returned when an invalid trace level is set.
 */
#define CY_AN_ERROR_INVALID_TRACE_LEVEL (CY_AS_ERROR_INVALID_TRACE_LEVEL)

/*
 * This error is returned when West Bridge is already in the standby state
 * and an attempt is made to put West Bridge into this state again.
 */
#define CY_AN_ERROR_ALREADY_STANDBY	(CY_AS_ERROR_ALREADY_STANDBY)

/*
 * This error is returned when the API needs to set a pin on the
 * West Bridge device, but this is not supported by the underlying HAL
 * layer.
 */
#define CY_AN_ERROR_SETTING_WAKEUP_PIN (CY_AS_ERROR_SETTING_WAKEUP_PIN)

/*
 * This error is returned when a module is being started that has
 * already been started.
 */
#define CY_AN_ERROR_ALREADY_RUNNING	 (CY_AS_ERROR_ALREADY_RUNNING)

/*
 * This error is returned when a module is being stopped that has
 * already been stopped.
 */
#define CY_AN_ERROR_NOT_RUNNING (CY_AS_ERROR_NOT_RUNNING)

/*
 * This error is returned when the caller tries to claim a media that has
 * already been claimed.
 */
#define CY_AN_ERROR_MEDIA_ALREADY_CLAIMED (CY_AS_ERROR_MEDIA_ALREADY_CLAIMED)

/*
 * This error is returned when the caller tries to release a media that
 * has already been released.
 */
#define CY_AN_ERROR_MEDIA_NOT_CLAIMED (CY_AS_ERROR_MEDIA_NOT_CLAIMED)

/*
 * This error is returned when canceling trying to cancel an asynchronous
 * operation when an async operation is not pending.
 */
#define CY_AN_ERROR_NO_OPERATION_PENDING (CY_AS_ERROR_NO_OPERATION_PENDING)

/*
 * This error is returned when an invalid endpoint number is provided
 * to an API call.
 */
#define CY_AN_ERROR_INVALID_ENDPOINT (CY_AS_ERROR_INVALID_ENDPOINT)

/*
 * This error is returned when an invalid descriptor type
 * is specified in an API call.
 */
#define CY_AN_ERROR_INVALID_DESCRIPTOR (CY_AS_ERROR_INVALID_DESCRIPTOR)

/*
 * This error is returned when an invalid descriptor index
 * is specified in an API call.
 */
#define CY_AN_ERROR_BAD_INDEX (CY_AS_ERROR_BAD_INDEX)

/*
 * This error is returned if trying to set a USB descriptor
 * when in the P port enumeration mode.
 */
#define CY_AN_ERROR_BAD_ENUMERATION_MODE (CY_AS_ERROR_BAD_ENUMERATION_MODE)

/*
 * This error is returned when the endpoint configuration specified
 * is not valid.
 */
#define CY_AN_ERROR_INVALID_CONFIGURATION (CY_AS_ERROR_INVALID_CONFIGURATION)

/*
 * This error is returned when the API cannot verify it is connected
 * to an West Bridge device.
 */
#define CY_AN_ERROR_NO_ANTIOCH (CY_AS_ERROR_NO_ANTIOCH)

/*
 * This error is returned when an API function is called and
 * CyAnMiscConfigureDevice has not been called to configure West
 * Bridge for the current environment.
 */
#define CY_AN_ERROR_NOT_CONFIGURED (CY_AS_ERROR_NOT_CONFIGURED)

/*
 * This error is returned when West Bridge cannot allocate memory required for
 * internal API operations.
 */
#define CY_AN_ERROR_OUT_OF_MEMORY (CY_AS_ERROR_OUT_OF_MEMORY)

/*
 * This error is returned when a module is being started that has
 * already been started.
 */
#define CY_AN_ERROR_NESTED_SLEEP (CY_AS_ERROR_NESTED_SLEEP)

/*
 * This error is returned when an operation is attempted on an endpoint that has
 * been disabled.
 */
#define CY_AN_ERROR_ENDPOINT_DISABLED (CY_AS_ERROR_ENDPOINT_DISABLED)

/*
 * This error is returned when a call is made to an API function when the device
 * is in standby.
 */
#define CY_AN_ERROR_IN_STANDBY (CY_AS_ERROR_IN_STANDBY)

/*
 * This error is returned when an API call is made with an invalid handle value.
 */
#define CY_AN_ERROR_INVALID_HANDLE (CY_AS_ERROR_INVALID_HANDLE)

/*
 * This error is returned when an invalid response is returned from the West
 * Bridge device.
 */
#define CY_AN_ERROR_INVALID_RESPONSE (CY_AS_ERROR_INVALID_RESPONSE)

/*
 * This error is returned from the callback function for any asynchronous
 * read or write request that is canceled.
 */
#define CY_AN_ERROR_CANCELED (CY_AS_ERROR_CANCELED)

/*
 * This error is returned when the call to create sleep channel fails
 * in the HAL layer.
 */
#define CY_AN_ERROR_CREATE_SLEEP_CHANNEL_FAILED \
	(CY_AS_ERROR_CREATE_SLEEP_CHANNEL_FAILED)

/*
 * This error is returned when the call to CyAnMiscLeaveStandby
 * is made and the device is not in standby.
 */
#define CY_AN_ERROR_NOT_IN_STANDBY (CY_AS_ERROR_NOT_IN_STANDBY)

/*
 * This error is returned when the call to destroy sleep channel fails
 * in the HAL layer.
 */
#define CY_AN_ERROR_DESTROY_SLEEP_CHANNEL_FAILED \
	(CY_AS_ERROR_DESTROY_SLEEP_CHANNEL_FAILED)

/*
 * This error is returned when an invalid resource is specified to a call
 * to CyAnMiscAcquireResource() or CyAnMiscReleaseResource()
 */
#define CY_AN_ERROR_INVALID_RESOURCE (CY_AS_ERROR_INVALID_RESOURCE)

/*
 * This error occurs when an operation is requested on an endpoint that has
 * a currently pending async operation.
 */
#define CY_AN_ERROR_ASYNC_PENDING (CY_AS_ERROR_ASYNC_PENDING)

/*
 * This error is returned when a call to CyAnStorageCancelAsync() or
 * CyAnUsbCancelAsync() is made when no asynchronous request is pending.
 */
#define CY_AN_ERROR_ASYNC_NOT_PENDING (CY_AS_ERROR_ASYNC_NOT_PENDING)

/*
 * This error is returned when a request is made to put the West Bridge device
 * into standby mode while the USB stack is still active.
 */
#define CY_AN_ERROR_USB_RUNNING (CY_AS_ERROR_USB_RUNNING)

/*
 * A request for in the wrong direction was issued on an endpoint.
 */
#define CY_AN_ERROR_USB_BAD_DIRECTION (CY_AS_ERROR_USB_BAD_DIRECTION)

/*
 * An invalid request was received
 */
#define CY_AN_ERROR_INVALID_REQUEST (CY_AS_ERROR_INVALID_REQUEST)

/*
 * An ACK request was requested while no setup packet was pending.
 */
#define CY_AN_ERROR_NO_SETUP_PACKET_PENDING	\
	(CY_AS_ERROR_NO_SETUP_PACKET_PENDING)

/*
 * A call was made to a API function that cannot be called from a callback.
 */
#define CY_AN_ERROR_INVALID_IN_CALLBACK	(CY_AS_ERROR_INVALID_IN_CALLBACK)

/*
 * A call was made to CyAnUsbSetEndPointConfig() before
 * CyAnUsbSetPhysicalConfiguration() was called.
 */
#define CY_AN_ERROR_ENDPOINT_CONFIG_NOT_SET	\
	(CY_AS_ERROR_ENDPOINT_CONFIG_NOT_SET)

/*
 * The physical endpoint referenced is not valid in the current
 * physical configuration
 */
#define CY_AN_ERROR_INVALID_PHYSICAL_ENDPOINT \
	(CY_AS_ERROR_INVALID_PHYSICAL_ENDPOINT)

/*
 * The data supplied to the CyAnMiscDownloadFirmware() call is not aligned on a
 * WORD (16 bit) boundary.
 */
#define CY_AN_ERROR_ALIGNMENT_ERROR	(CY_AS_ERROR_ALIGNMENT_ERROR)

/*
 * A call was made to destroy the West Bridge device, but the USB stack or the
 * storage stack was will running.
 */
#define CY_AN_ERROR_STILL_RUNNING (CY_AS_ERROR_STILL_RUNNING)

/*
 * A call was made to the API for a function that is not yet supported.
 */
#define CY_AN_ERROR_NOT_YET_SUPPORTED (CY_AS_ERROR_NOT_YET_SUPPORTED)

/*
 * A NULL callback was provided where a non-NULL callback was required
 */
#define CY_AN_ERROR_NULL_CALLBACK (CY_AS_ERROR_NULL_CALLBACK)

/*
 * This error is returned when a request is made to put the West Bridge device
 * into standby mode while the storage stack is still active.
 */
#define CY_AN_ERROR_STORAGE_RUNNING	(CY_AS_ERROR_STORAGE_RUNNING)

/*
 * This error is returned when an operation is attempted that cannot be
 * completed while the USB stack is connected to a USB host.
 */
#define CY_AN_ERROR_USB_CONNECTED (CY_AS_ERROR_USB_CONNECTED)

/*
 * This error is returned when a USB disconnect is attempted and the
 * West Bridge device is not connected.
 */
#define CY_AN_ERROR_USB_NOT_CONNECTED (CY_AS_ERROR_USB_NOT_CONNECTED)

/*
 * This error is returned when an P2S storage operation attempted and
 * data could not be read or written to the storage media.
 */
#define CY_AN_ERROR_MEDIA_ACCESS_FAILURE (CY_AS_ERROR_MEDIA_ACCESS_FAILURE)

/*
 * This error is returned when an P2S storage operation attempted and
 * the media is write protected.
 */
#define CY_AN_ERROR_MEDIA_WRITE_PROTECTED (CY_AS_ERROR_MEDIA_WRITE_PROTECTED)

/*
 * This error is returned when an attempt is made to cancel a request
 * that has already been sent to the West Bridge.
 */
#define CY_AN_ERROR_OPERATION_IN_TRANSIT (CY_AS_ERROR_OPERATION_IN_TRANSIT)

/*
 * This error is returned when an invalid parameter is passed to one of
 * the APIs.
 */
#define CY_AN_ERROR_INVALID_PARAMETER (CY_AS_ERROR_INVALID_PARAMETER)

/*
 * This error is returned if an API is not supported by the current
 * West Bridge device or the active firmware version.
 */
#define CY_AN_ERROR_NOT_SUPPORTED (CY_AS_ERROR_NOT_SUPPORTED)

/*
 * This error is returned when a call is made to one of the Storage or
 * USB APIs while the device is in suspend mode.
 */
#define CY_AN_ERROR_IN_SUSPEND (CY_AS_ERROR_IN_SUSPEND)

/*
 * This error is returned when the call to CyAnMiscLeaveSuspend
 * is made and the device is not in suspend mode.
 */
#define CY_AN_ERROR_NOT_IN_SUSPEND (CY_AS_ERROR_NOT_IN_SUSPEND)

/*
 * This error is returned when a command that is disabled by USB is called.
 */
#define CY_AN_ERROR_FEATURE_NOT_ENABLED (CY_AS_ERROR_FEATURE_NOT_ENABLED)

/*
 * This error is returned when an Async storage read or write is called before a
 * query device call is issued.
 */
#define CY_AN_ERROR_QUERY_DEVICE_NEEDED (CY_AS_ERROR_QUERY_DEVICE_NEEDED)

/*
 * This error is returned when a call is made to USB or STORAGE Start or
 * Stop before a prior Start or Stop has finished.
 */
#define CY_AN_ERROR_STARTSTOP_PENDING (CY_AS_ERROR_STARTSTOP_PENDING)

/*
 * This error is returned when a request is made for a bus that does not exist
 */
#define CY_AN_ERROR_NO_SUCH_BUS  (CY_AS_ERROR_NO_SUCH_BUS)

#endif /* __doxygen__ */

#endif /* _INCLUDED_CYANERR_H_ */
