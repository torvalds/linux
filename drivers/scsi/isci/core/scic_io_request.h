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


#include "sci_status.h"
#include "intel_sas.h"

struct scic_sds_request;
struct scic_sds_remote_device;
struct scic_sds_controller;

/**
 * struct scic_io_parameters - This structure contains additional optional
 *    parameters for SSP IO requests.  These parameters are utilized with the
 *    scic_io_request_construct_advanced_ssp() method.
 *
 * Add Block-guard/DIF, TLR
 */
struct scic_io_parameters {
	/**
	 * This sub-structure contains SCSI specific features (for use with SSP
	 * IO requests).
	 */
	struct {
		/**
		 * Data Integrity Format (DIF) is also known as protection information
		 * or block-guard.  This sub-structure contains DIF specific feature
		 * information for SSP IO requests.
		 */
		struct {
			void *placeholder;
		} dif;

		/**
		 * Transport Layer Retries (TLR) is an SSP protocol specific feature.
		 * This sub-structure contains Transport Layer Retries (TLR) specific
		 * feature information for SSP IO requests.
		 */
		struct {
			void *placeholder;
		} tlr;

	} scsi;

};

/**
 * struct scic_passthru_request_callbacks - This structure contains the pointer
 *    to the callback functions for constructing the passthrough request common
 *    to SSP, SMP and STP. This structure must be set by the win sci layer
 *    before the passthrough build is called
 *
 *
 */
struct scic_passthru_request_callbacks {
	/**
	 * Function pointer to get the phy identifier for passthrough request.
	 */
	u32 (*scic_cb_passthru_get_phy_identifier)(void *, u8 *);
	/**
	 * Function pointer to get the port identifier for passthrough request.
	 */
	u32 (*scic_cb_passthru_get_port_identifier)(void *, u8 *);
	/**
	 * Function pointer to get the connection rate for passthrough request.
	 */
	u32 (*scic_cb_passthru_get_connection_rate)(void *, void *);
	/**
	 * Function pointer to get the destination sas address for passthrough request.
	 */
	void (*scic_cb_passthru_get_destination_sas_address)(void *, u8 **);
	/**
	 * Function pointer to get the transfer length for passthrough request.
	 */
	u32 (*scic_cb_passthru_get_transfer_length)(void *);
	/**
	 * Function pointer to get the data direction for passthrough request.
	 */
	u32 (*scic_cb_passthru_get_data_direction)(void *);

};

/**
 * struct scic_ssp_passthru_request_callbacks - This structure contains the
 *    pointer to the callback functions for constructing the passthrough
 *    request specific to SSP. This structure must be set by the win sci layer
 *    before the passthrough build is called
 *
 *
 */
struct scic_ssp_passthru_request_callbacks {
	/**
	 * Common callbacks for all Passthru requests
	 */
	struct scic_passthru_request_callbacks common_callbacks;
	/**
	 * Function pointer to get the lun for passthrough request.
	 */
	void (*scic_cb_ssp_passthru_get_lun)(void *, u8 **);
	/**
	 * Function pointer to get the cdb
	 */
	void (*scic_cb_ssp_passthru_get_cdb)(void *, u32 *, u8 **, u32 *, u8 **);
	/**
	 * Function pointer to get the task attribute for passthrough request.
	 */
	u32 (*scic_cb_ssp_passthru_get_task_attribute)(void *);
};

/**
 * struct scic_stp_passthru_request_callbacks - This structure contains the
 *    pointer to the callback functions for constructing the passthrough
 *    request specific to STP. This structure must be set by the win sci layer
 *    before the passthrough build is called
 *
 *
 */
struct scic_stp_passthru_request_callbacks {
	/**
	 * Common callbacks for all Passthru requests
	 */
	struct scic_passthru_request_callbacks common_callbacks;
	/**
	 * Function pointer to get the protocol for passthrough request.
	 */
	u8 (*scic_cb_stp_passthru_get_protocol)(void *);
	/**
	 * Function pointer to get the resgister fis
	 */
	void (*scic_cb_stp_passthru_get_register_fis)(void *, u8 **);
	/**
	 * Function pointer to get the MULTIPLE_COUNT (bits 5,6,7 in Byte 1 in the SAT-specific SCSI extenstion in ATA Pass-through (0x85))
	 */
	u8 (*scic_cb_stp_passthru_get_multiplecount)(void *);
	/**
	 * Function pointer to get the EXTEND (bit 0 in Byte 1 the SAT-specific SCSI extenstion in ATA Pass-through (0x85))
	 */
	u8 (*scic_cb_stp_passthru_get_extend)(void *);
	/**
	 * Function pointer to get the CK_COND (bit 5 in Byte 2 the SAT-specific SCSI extenstion in ATA Pass-through (0x85))
	 */
	u8 (*scic_cb_stp_passthru_get_ckcond)(void *);
	/**
	 * Function pointer to get the T_DIR (bit 3 in Byte 2 the SAT-specific SCSI extenstion in ATA Pass-through (0x85))
	 */
	u8 (*scic_cb_stp_passthru_get_tdir)(void *);
	/**
	 * Function pointer to get the BYTE_BLOCK (bit 2 in Byte 2 the SAT-specific SCSI extenstion in ATA Pass-through (0x85))
	 */
	u8 (*scic_cb_stp_passthru_get_byteblock)(void *);
	/**
	 * Function pointer to get the T_LENGTH (bits 0,1 in Byte 2 the SAT-specific SCSI extenstion in ATA Pass-through (0x85))
	 */
	u8 (*scic_cb_stp_passthru_get_tlength)(void *);

};

/**
 * struct scic_smp_passthru_request_callbacks - This structure contains the
 *    pointer to the callback functions for constructing the passthrough
 *    request specific to SMP. This structure must be set by the win sci layer
 *    before the passthrough build is called
 *
 *
 */
struct scic_smp_passthru_request_callbacks {
	/**
	 * Common callbacks for all Passthru requests
	 */
	struct scic_passthru_request_callbacks common_callbacks;

	/**
	 * Function pointer to get the length of the smp request and its length
	 */
	u32 (*scic_cb_smp_passthru_get_request)(void *, u8 **);
	/**
	 * Function pointer to get the frame type of the smp request
	 */
	u8 (*scic_cb_smp_passthru_get_frame_type)(void *);
	/**
	 * Function pointer to get the function in the the smp request
	 */
	u8 (*scic_cb_smp_passthru_get_function)(void *);

	/**
	 * Function pointer to get the "allocated response length" in the the smp request
	 */
	u8 (*scic_cb_smp_passthru_get_allocated_response_length)(void *);

};

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


/**
 * scic_io_request_get_object_size() - This method simply returns the size
 *    required to build an SCI based IO request object.
 *
 * Return the size of the SCI IO request object.
 */
u32 scic_io_request_get_object_size(
	void);

/**
 * scic_io_request_construct() - This method is called by the SCI user to
 *    construct all SCI Core IO requests.  Memory initialization and
 *    functionality common to all IO request types is performed in this method.
 * @scic_controller: the handle to the core controller object for which to
 *    build an IO request.
 * @scic_remote_device: the handle to the core remote device object for which
 *    to build an IO request.
 * @io_tag: This parameter specifies the IO tag to be associated with this
 *    request.  If SCI_CONTROLLER_INVALID_IO_TAG is passed, then a copy of the
 *    request is built internally.  The request will be copied into the actual
 *    controller request memory when the IO tag is allocated internally during
 *    the scic_controller_start_io() method.
 * @user_io_request_object: This parameter specifies the user IO request to be
 *    utilized during IO construction.  This IO pointer will become the
 *    associated object for the core IO request object.
 * @scic_io_request_memory: This parameter specifies the memory location to be
 *    utilized when building the core request.
 * @new_scic_io_request_handle: This parameter specifies a pointer to the
 *    handle the core will expect in further interactions with the core IO
 *    request object.
 *
 * The SCI core implementation will create an association between the user IO
 * request object and the core IO request object. Indicate if the controller
 * successfully built the IO request. SCI_SUCCESS This value is returned if the
 * IO request was successfully built.
 */
enum sci_status scic_io_request_construct(
	struct scic_sds_controller *scic_controller,
	struct scic_sds_remote_device *scic_remote_device,
	u16 io_tag,
	void *user_io_request_object,
	struct scic_sds_request *scic_io_request_memory,
	struct scic_sds_request **new_scic_io_request_handle);

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
 * request.  Please refer to the sci_object_set_association() routine for more
 * information.
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
 * request.  Please refer to the sci_object_set_association() routine for more
 * information.
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
 * request.  Please refer to the sci_object_set_association() routine for more
 * information.
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
 * scic_io_request_get_command_iu_address() - This method will return the
 *    address to the command information unit.
 * @scic_io_request: This parameter specifies the handle to the io request
 *    object to be built.
 *
 * The address of the SSP/SMP command information unit.
 */
void *scic_io_request_get_command_iu_address(
	struct scic_sds_request *scic_io_request);

/**
 * scic_io_request_get_response_iu_address() - This method will return the
 *    address to the response information unit.  For an SSP request this buffer
 *    is only valid if the IO request is completed with the status
 *    SCI_FAILURE_IO_RESPONSE_VALID.
 * @scic_io_request: This parameter specifies the handle to the io request
 *    object to be built.
 *
 * The address of the SSP/SMP response information unit.
 */
void *scic_io_request_get_response_iu_address(
	struct scic_sds_request *scic_io_request);

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
 * scic_stp_io_request_get_h2d_reg_address() - This method will return the
 *    address of the host to device register fis region for the io request
 *    object.
 * @scic_io_request: This parameter specifies the handle to the io request
 *    object from which to get the host to device register fis buffer.
 *
 * The address of the host to device register fis buffer in the io request
 * object. This function is only valid for SATA requests.
 */
void *scic_stp_io_request_get_h2d_reg_address(
	struct scic_sds_request *scic_io_request);

/**
 * scic_stp_io_request_get_d2h_reg_address() - This method will return the
 *    address of the device to host register fis region for the io request
 *    object.
 * @scic_io_request: This parameter specifies teh handle to the io request
 *    object from which to get the device to host register fis buffer.
 *
 * The address fo the device to host register fis ending the io request. This
 * function is only valid for SATA requests.
 */
void *scic_stp_io_request_get_d2h_reg_address(
	struct scic_sds_request *scic_io_request);


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

