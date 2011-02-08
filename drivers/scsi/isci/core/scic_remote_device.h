/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SCIC_REMOTE_DEVICE_H_
#define _SCIC_REMOTE_DEVICE_H_

/**
 * This file contains all of the interface methods that can be called by an
 *    SCIC user on the device object.
 *
 *
 */


#include "sci_status.h"
#include "intel_sas.h"

struct scic_sds_port;
struct scic_sds_remote_device;

/**
 *
 *
 *
 */
enum scic_remote_device_not_ready_reason_code {
	SCIC_REMOTE_DEVICE_NOT_READY_START_REQUESTED,
	SCIC_REMOTE_DEVICE_NOT_READY_STOP_REQUESTED,
	SCIC_REMOTE_DEVICE_NOT_READY_SATA_REQUEST_STARTED,
	SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED,
	SCIC_REMOTE_DEVICE_NOT_READY_SMP_REQUEST_STARTED,

	SCIC_REMOTE_DEVICE_NOT_READY_REASON_CODE_MAX

};

/**
 * scic_remote_device_get_object_size() - This method simply returns the
 *    maximum memory space needed to store a remote device object.
 *
 * a positive integer value indicating the size (in bytes) of the remote device
 * object.
 */
u32 scic_remote_device_get_object_size(
	void);

struct scic_sds_port;
struct scic_sds_remote_device;
/**
 * scic_remote_device_construct() - This method will perform the construction
 *    common to all remote device objects.
 * @sci_port: SAS/SATA port through which this device is accessed.
 * @sci_dev: remote device to construct
 *
 * It isn't necessary to call scic_remote_device_destruct() for device objects
 * that have only called this method for construction. Once subsequent
 * construction methods have been invoked (e.g.
 * scic_remote_device_da_construct()), then destruction should occur. none
 */
void scic_remote_device_construct(struct scic_sds_port *sci_port,
				  struct scic_sds_remote_device *sci_dev);

/**
 * scic_remote_device_da_construct() - This method will construct a
 *    SCIC_REMOTE_DEVICE object for a direct attached (da) device.  The
 *    information (e.g. IAF, Signature FIS, etc.) necessary to build the device
 *    is known to the SCI Core since it is contained in the scic_phy object.
 * @remote_device: This parameter specifies the remote device to be destructed.
 *
 * The user must have previously called scic_remote_device_construct() Remote
 * device objects are a limited resource.  As such, they must be protected.
 * Thus calls to construct and destruct are mutually exclusive and
 * non-reentrant. Indicate if the remote device was successfully constructed.
 * SCI_SUCCESS Returned if the device was successfully constructed.
 * SCI_FAILURE_DEVICE_EXISTS Returned if the device has already been
 * constructed.  If it's an additional phy for the target, then call
 * scic_remote_device_da_add_phy(). SCI_FAILURE_UNSUPPORTED_PROTOCOL Returned
 * if the supplied parameters necessitate creation of a remote device for which
 * the protocol is not supported by the underlying controller hardware.
 * SCI_FAILURE_INSUFFICIENT_RESOURCES This value is returned if the core
 * controller associated with the supplied parameters is unable to support
 * additional remote devices.
 */
enum sci_status scic_remote_device_da_construct(
	struct scic_sds_remote_device *remote_device);

/**
 * scic_remote_device_ea_construct() - This method will construct an
 *    SCIC_REMOTE_DEVICE object for an expander attached (ea) device from an
 *    SMP Discover Response.
 * @remote_device: This parameter specifies the remote device to be destructed.
 * @discover_response: This parameter specifies the SMP Discovery Response to
 *    be used in device creation.
 *
 * The user must have previously called scic_remote_device_construct() Remote
 * device objects are a limited resource.  As such, they must be protected.
 * Thus calls to construct and destruct are mutually exclusive and
 * non-reentrant. Indicate if the remote device was successfully constructed.
 * SCI_SUCCESS Returned if the device was successfully constructed.
 * SCI_FAILURE_DEVICE_EXISTS Returned if the device has already been
 * constructed.  If it's an additional phy for the target, then call
 * scic_ea_remote_device_add_phy(). SCI_FAILURE_UNSUPPORTED_PROTOCOL Returned
 * if the supplied parameters necessitate creation of a remote device for which
 * the protocol is not supported by the underlying controller hardware.
 * SCI_FAILURE_INSUFFICIENT_RESOURCES This value is returned if the core
 * controller associated with the supplied parameters is unable to support
 * additional remote devices.
 */
enum sci_status scic_remote_device_ea_construct(
	struct scic_sds_remote_device *remote_device,
	struct smp_response_discover *discover_response);

/**
 * scic_remote_device_destruct() - This method is utilized to free up a core's
 *    remote device object.
 * @remote_device: This parameter specifies the remote device to be destructed.
 *
 * Remote device objects are a limited resource.  As such, they must be
 * protected.  Thus calls to construct and destruct are mutually exclusive and
 * non-reentrant. The return value shall indicate if the device was
 * successfully destructed or if some failure occurred. enum sci_status This value
 * is returned if the device is successfully destructed.
 * SCI_FAILURE_INVALID_REMOTE_DEVICE This value is returned if the supplied
 * device isn't valid (e.g. it's already been destoryed, the handle isn't
 * valid, etc.).
 */
enum sci_status scic_remote_device_destruct(
	struct scic_sds_remote_device *remote_device);





/**
 * scic_remote_device_start() - This method will start the supplied remote
 *    device.  This method enables normal IO requests to flow through to the
 *    remote device.
 * @remote_device: This parameter specifies the device to be started.
 * @timeout: This parameter specifies the number of milliseconds in which the
 *    start operation should complete.
 *
 * An indication of whether the device was successfully started. SCI_SUCCESS
 * This value is returned if the device was successfully started.
 * SCI_FAILURE_INVALID_PHY This value is returned if the user attempts to start
 * the device when there have been no phys added to it.
 */
enum sci_status scic_remote_device_start(
	struct scic_sds_remote_device *remote_device,
	u32 timeout);

/**
 * scic_remote_device_stop() - This method will stop both transmission and
 *    reception of link activity for the supplied remote device.  This method
 *    disables normal IO requests from flowing through to the remote device.
 * @remote_device: This parameter specifies the device to be stopped.
 * @timeout: This parameter specifies the number of milliseconds in which the
 *    stop operation should complete.
 *
 * An indication of whether the device was successfully stopped. SCI_SUCCESS
 * This value is returned if the transmission and reception for the device was
 * successfully stopped.
 */
enum sci_status scic_remote_device_stop(
	struct scic_sds_remote_device *remote_device,
	u32 timeout);

/**
 * scic_remote_device_reset() - This method will reset the device making it
 *    ready for operation. This method must be called anytime the device is
 *    reset either through a SMP phy control or a port hard reset request.
 * @remote_device: This parameter specifies the device to be reset.
 *
 * This method does not actually cause the device hardware to be reset. This
 * method resets the software object so that it will be operational after a
 * device hardware reset completes. An indication of whether the device reset
 * was accepted. SCI_SUCCESS This value is returned if the device reset is
 * started.
 */
enum sci_status scic_remote_device_reset(
	struct scic_sds_remote_device *remote_device);

/**
 * scic_remote_device_reset_complete() - This method informs the device object
 *    that the reset operation is complete and the device can resume operation
 *    again.
 * @remote_device: This parameter specifies the device which is to be informed
 *    of the reset complete operation.
 *
 * An indication that the device is resuming operation. SCI_SUCCESS the device
 * is resuming operation.
 */
enum sci_status scic_remote_device_reset_complete(
	struct scic_sds_remote_device *remote_device);



/**
 * scic_remote_device_get_connection_rate() - This method simply returns the
 *    link rate at which communications to the remote device occur.
 * @remote_device: This parameter specifies the device for which to get the
 *    connection rate.
 *
 * Return the link rate at which we transfer for the supplied remote device.
 */
enum sci_sas_link_rate scic_remote_device_get_connection_rate(
	struct scic_sds_remote_device *remote_device);

/**
 * scic_remote_device_get_protocols() - This method will indicate which
 *    protocols are supported by this remote device.
 * @remote_device: This parameter specifies the device for which to return the
 *    protocol.
 * @protocols: This parameter specifies the output values, from the remote
 *    device object, which indicate the protocols supported by the supplied
 *    remote_device.
 *
 * The type of protocols supported by this device.  The values are returned as
 * part of a bit mask in order to allow for multi-protocol support.
 */
void scic_remote_device_get_protocols(
	struct scic_sds_remote_device *remote_device,
	struct smp_discover_response_protocols *protocols);


#if !defined(DISABLE_ATAPI)
/**
 * scic_remote_device_is_atapi() -
 * @this_device: The device whose type is to be decided.
 *
 * This method first decide whether a device is a stp target, then decode the
 * signature fis of a DA STP device to tell whether it is a standard end disk
 * or an ATAPI device. bool Indicate a device is ATAPI device or not.
 */
bool scic_remote_device_is_atapi(
	struct scic_sds_remote_device *device_handle);
#else /* !defined(DISABLE_ATAPI) */
#define scic_remote_device_is_atapi(device_handle) false
#endif /* !defined(DISABLE_ATAPI) */


#endif  /* _SCIC_REMOTE_DEVICE_H_ */

