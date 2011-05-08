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

#ifndef _SCIC_IO_REQUEST_H_
#define _SCIC_IO_REQUEST_H_

/**
 * This file contains the structures and interface methods that can be
 *    referenced and used by the SCI user for the SCI IO request object.
 *
 * Determine the failure situations and return values.
 */

#include <linux/kernel.h>
#include "sci_status.h"

struct scic_sds_request;
struct scic_sds_remote_device;
struct scic_sds_controller;

/**
 * This enumeration specifies the transport protocol utilized for the request.
 *
 *
 */
typedef enum {
	/**
	 * This enumeration constant indicates that no protocol has yet been
	 * set.
	 */
	SCIC_NO_PROTOCOL,

	/**
	 * This enumeration constant indicates that the protocol utilized
	 * is the Serial Management Protocol.
	 */
	SCIC_SMP_PROTOCOL,

	/**
	 * This enumeration constant indicates that the protocol utilized
	 * is the Serial SCSI Protocol.
	 */
	SCIC_SSP_PROTOCOL,

	/**
	 * This enumeration constant indicates that the protocol utilized
	 * is the Serial-ATA Tunneling Protocol.
	 */
	SCIC_STP_PROTOCOL

} SCIC_TRANSPORT_PROTOCOL;

enum sci_status scic_io_request_construct(
	struct scic_sds_controller *scic_controller,
	struct scic_sds_remote_device *scic_remote_device,
	u16 io_tag, struct scic_sds_request *sci_req);

/**
 * scic_io_request_construct_basic_ssp() - This method is called by the SCI
 *    user to build an SSP IO request.
 * @scic_io_request: This parameter specifies the handle to the io request
 *    object to be built.
 *
 * - The user must have previously called scic_io_request_construct() on the
 * supplied IO request. Indicate if the controller successfully built the IO
 * request. SCI_SUCCESS This value is returned if the IO request was
 * successfully built. SCI_FAILURE_UNSUPPORTED_PROTOCOL This value is returned
 * if the remote_device does not support the SSP protocol.
 * SCI_FAILURE_INVALID_ASSOCIATION This value is returned if the user did not
 * properly set the association between the SCIC IO request and the user's IO
 * request.
 */
enum sci_status scic_io_request_construct_basic_ssp(
	struct scic_sds_request *scic_io_request);





/**
 * scic_io_request_construct_basic_sata() - This method is called by the SCI
 *    Core user to build an STP IO request.
 * @scic_io_request: This parameter specifies the handle to the io request
 *    object to be built.
 *
 * - The user must have previously called scic_io_request_construct() on the
 * supplied IO request. Indicate if the controller successfully built the IO
 * request. SCI_SUCCESS This value is returned if the IO request was
 * successfully built. SCI_FAILURE_UNSUPPORTED_PROTOCOL This value is returned
 * if the remote_device does not support the STP protocol.
 * SCI_FAILURE_INVALID_ASSOCIATION This value is returned if the user did not
 * properly set the association between the SCIC IO request and the user's IO
 * request.
 */
enum sci_status scic_io_request_construct_basic_sata(
	struct scic_sds_request *scic_io_request);




/**
 * scic_io_request_construct_smp() - This method is called by the SCI user to
 *    build an SMP IO request.
 * @scic_io_request: This parameter specifies the handle to the io request
 *    object to be built.
 *
 * - The user must have previously called scic_io_request_construct() on the
 * supplied IO request. Indicate if the controller successfully built the IO
 * request. SCI_SUCCESS This value is returned if the IO request was
 * successfully built. SCI_FAILURE_UNSUPPORTED_PROTOCOL This value is returned
 * if the remote_device does not support the SMP protocol.
 * SCI_FAILURE_INVALID_ASSOCIATION This value is returned if the user did not
 * properly set the association between the SCIC IO request and the user's IO
 * request.
 */
enum sci_status scic_io_request_construct_smp(
	struct scic_sds_request *scic_io_request);



/**
 * scic_request_get_controller_status() - This method returns the controller
 *    specific IO/Task request status. These status values are unique to the
 *    specific controller being managed by the SCIC.
 * @io_request: the handle to the IO or task management request object for
 *    which to retrieve the status.
 *
 * This method returns a value indicating the controller specific request
 * status.
 */
u32 scic_request_get_controller_status(
	struct scic_sds_request *io_request);

/**
 * scic_io_request_get_io_tag() - This method will return the IO tag utilized
 *    by the IO request.
 * @scic_io_request: This parameter specifies the handle to the io request
 *    object for which to return the IO tag.
 *
 * An unsigned integer representing the IO tag being utilized.
 * SCI_CONTROLLER_INVALID_IO_TAG This value is returned if the IO does not
 * currently have an IO tag allocated to it. All return other values indicate a
 * legitimate tag.
 */
u16 scic_io_request_get_io_tag(
	struct scic_sds_request *scic_io_request);


/**
 * scic_stp_io_request_set_ncq_tag() - This method will assign an NCQ tag to
 *    the io request object.  The caller of this function must make sure that
 *    only valid NCQ tags are assigned to the io request object.
 * @scic_io_request: This parameter specifies the handle to the io request
 *    object to which to assign the ncq tag.
 * @ncq_tag: This parameter specifies the NCQ tag to be utilized for the
 *    supplied core IO request.  It is up to the user to make sure that this is
 *    a valid NCQ tag.
 *
 * none This function is only valid for SATA NCQ requests.
 */
void scic_stp_io_request_set_ncq_tag(
	struct scic_sds_request *scic_io_request,
	u16 ncq_tag);

/**
 * scic_io_request_get_number_of_bytes_transferred() - This method will return
 *    the number of bytes transferred from the SCU
 * @scic_io_request: This parameter specifies the handle to the io request
 *    whose data length was not eqaul to the data length specified in the
 *    request. When the driver gets an early io completion status from the
 *    hardware, this routine should be called to get the actual number of bytes
 *    transferred
 *
 * The return is the number of bytes transferred when the data legth is not
 * equal to the specified length in the io request
 */
u32 scic_io_request_get_number_of_bytes_transferred(
	struct scic_sds_request *scic_io_request);


#endif  /* _SCIC_IO_REQUEST_H_ */

