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


/**
 * This file contains isci module object implementation.
 *
 *
 */

#include "isci.h"
#include "request.h"
#include "sata.h"
#include "task.h"


/**
 * scic_cb_stall_execution() - This method is called when the core requires the
 *    OS driver to stall execution.  This method is utilized during
 *    initialization or non-performance paths only.
 * @microseconds: This parameter specifies the number of microseconds for which
 *    to stall.  The operating system driver is allowed to round this value up
 *    where necessary.
 *
 * none.
 */
void scic_cb_stall_execution(
	u32 microseconds)
{
	udelay(microseconds);
}


/**
 * scic_cb_io_request_get_physical_address() - This callback method asks the
 *    user to provide the physical address for the supplied virtual address
 *    when building an io request object.
 * @controller: This parameter is the core controller object handle.
 * @io_request: This parameter is the io request object handle for which the
 *    physical address is being requested.
 *
 *
 */
void scic_cb_io_request_get_physical_address(
	struct scic_sds_controller *controller,
	struct scic_sds_request *io_request,
	void *virtual_address,
	dma_addr_t *physical_address)
{
	struct isci_request *request =
		(struct isci_request *)sci_object_get_association(io_request);

	char *requested_address = (char *)virtual_address;
	char *base_address = (char *)request;

	BUG_ON(requested_address < base_address);
	BUG_ON((requested_address - base_address) >=
			request->request_alloc_size);

	*physical_address = request->request_daddr +
		(requested_address - base_address);
}

/**
 * scic_cb_io_request_get_transfer_length() - This callback method asks the
 *    user to provide the number of bytes to be transfered as part of this
 *    request.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns the number of payload data bytes to be transfered for
 * this IO request.
 */
u32 scic_cb_io_request_get_transfer_length(
	void *scic_user_io_request)
{
	return isci_request_io_request_get_transfer_length(
		       scic_user_io_request
		       );
}


/**
 * scic_cb_io_request_get_data_direction() - This callback method asks the user
 *    to provide the data direction for this request.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns the value of SCI_IO_REQUEST_DATA_OUT or
 * SCI_IO_REQUEST_DATA_IN, or SCI_IO_REQUEST_NO_DATA.
 */
SCI_IO_REQUEST_DATA_DIRECTION scic_cb_io_request_get_data_direction(
	void *scic_user_io_request)
{
	return isci_request_io_request_get_data_direction(
		       scic_user_io_request
		       );
}


/**
 * scic_cb_io_request_get_next_sge() - This callback method asks the user to
 *    provide the address to where the next Scatter-Gather Element is located.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 * @current_sge_address: This parameter specifies the address for the current
 *    SGE (i.e. the one that has just processed).
 *
 * An address specifying the location for the next scatter gather element to be
 * processed.
 */
void scic_cb_io_request_get_next_sge(
	void *scic_user_io_request,
	void *current_sge_address,
	void **next_sge)
{
	*next_sge = isci_request_io_request_get_next_sge(
		scic_user_io_request,
		current_sge_address
		);
}

/**
 * scic_cb_sge_get_address_field() - This callback method asks the user to
 *    provide the contents of the "address" field in the Scatter-Gather Element.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 * @sge_address: This parameter specifies the address for the SGE from which to
 *    retrieve the address field.
 *
 * A physical address specifying the contents of the SGE's address field.
 */
dma_addr_t scic_cb_sge_get_address_field(
	void *scic_user_io_request,
	void *sge_address)
{
	return isci_request_sge_get_address_field(
		       scic_user_io_request,
		       sge_address
		       );
}

/**
 * scic_cb_sge_get_length_field() - This callback method asks the user to
 *    provide the contents of the "length" field in the Scatter-Gather Element.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 * @sge_address: This parameter specifies the address for the SGE from which to
 *    retrieve the address field.
 *
 * This method returns the length field specified inside the SGE referenced by
 * the sge_address parameter.
 */
u32 scic_cb_sge_get_length_field(
	void *scic_user_io_request,
	void *sge_address)
{
	return isci_request_sge_get_length_field(
		       scic_user_io_request,
		       sge_address
		       );
}

/**
 * scic_cb_ssp_io_request_get_cdb_address() - This callback method asks the
 *    user to provide the address for the command descriptor block (CDB)
 *    associated with this IO request.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns the virtual address of the CDB.
 */
void *scic_cb_ssp_io_request_get_cdb_address(
	void *scic_user_io_request)
{
	return isci_request_ssp_io_request_get_cdb_address(
		       scic_user_io_request
		       );
}

/**
 * scic_cb_ssp_io_request_get_cdb_length() - This callback method asks the user
 *    to provide the length of the command descriptor block (CDB) associated
 *    with this IO request.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns the length of the CDB.
 */
u32 scic_cb_ssp_io_request_get_cdb_length(
	void *scic_user_io_request)
{
	return isci_request_ssp_io_request_get_cdb_length(
		       scic_user_io_request
		       );
}

/**
 * scic_cb_ssp_io_request_get_lun() - This callback method asks the user to
 *    provide the Logical Unit (LUN) associated with this IO request.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns the LUN associated with this request. This should be u64?
 */
u32 scic_cb_ssp_io_request_get_lun(
	void *scic_user_io_request)
{
	return isci_request_ssp_io_request_get_lun(scic_user_io_request);
}

/**
 * scic_cb_ssp_io_request_get_task_attribute() - This callback method asks the
 *    user to provide the task attribute associated with this IO request.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns the task attribute associated with this IO request.
 */
u32 scic_cb_ssp_io_request_get_task_attribute(
	void *scic_user_io_request)
{
	return isci_request_ssp_io_request_get_task_attribute(
		       scic_user_io_request
		       );
}

/**
 * scic_cb_ssp_io_request_get_command_priority() - This callback method asks
 *    the user to provide the command priority associated with this IO request.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns the command priority associated with this IO request.
 */
u32 scic_cb_ssp_io_request_get_command_priority(
	void *scic_user_io_request)
{
	return isci_request_ssp_io_request_get_command_priority(
		       scic_user_io_request
		       );
}

/**
 * scic_cb_ssp_task_request_get_lun() - This method returns the Logical Unit to
 *    be utilized for this task management request.
 * @scic_user_task_request: This parameter points to the user's task request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns the LUN associated with this request. This should be u64?
 */
u32 scic_cb_ssp_task_request_get_lun(
	void *scic_user_task_request)
{
	return isci_task_ssp_request_get_lun(
		       (struct isci_request *)scic_user_task_request
		       );
}

/**
 * scic_cb_ssp_task_request_get_function() - This method returns the task
 *    management function to be utilized for this task request.
 * @scic_user_task_request: This parameter points to the user's task request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns an unsigned byte representing the task management
 * function to be performed.
 */
u8 scic_cb_ssp_task_request_get_function(
	void *scic_user_task_request)
{
	return isci_task_ssp_request_get_function(
		       (struct isci_request *)scic_user_task_request
		       );
}

/**
 * scic_cb_ssp_task_request_get_io_tag_to_manage() - This method returns the
 *    task management IO tag to be managed. Depending upon the task management
 *    function the value returned from this method may be ignored.
 * @scic_user_task_request: This parameter points to the user's task request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns an unsigned 16-bit word depicting the IO tag to be
 * managed.
 */
u16 scic_cb_ssp_task_request_get_io_tag_to_manage(
	void *scic_user_task_request)
{
	return isci_task_ssp_request_get_io_tag_to_manage(
		       (struct isci_request *)scic_user_task_request
		       );
}

/**
 * scic_cb_ssp_task_request_get_response_data_address() - This callback method
 *    asks the user to provide the virtual address of the response data buffer
 *    for the supplied IO request.
 * @scic_user_task_request: This parameter points to the user's task request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns the virtual address for the response data buffer
 * associated with this IO request.
 */
void *scic_cb_ssp_task_request_get_response_data_address(
	void *scic_user_task_request)
{
	return isci_task_ssp_request_get_response_data_address(
		       (struct isci_request *)scic_user_task_request
		       );
}

/**
 * scic_cb_ssp_task_request_get_response_data_length() - This callback method
 *    asks the user to provide the length of the response data buffer for the
 *    supplied IO request.
 * @scic_user_task_request: This parameter points to the user's task request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns the length of the response buffer data associated with
 * this IO request.
 */
u32 scic_cb_ssp_task_request_get_response_data_length(
	void *scic_user_task_request)
{
	return isci_task_ssp_request_get_response_data_length(
		       (struct isci_request *)scic_user_task_request
		       );
}

#if !defined(DISABLE_ATAPI)
/**
 * scic_cb_stp_packet_io_request_get_cdb_address() - This user callback asks
 *    the user to provide stp packet io's the CDB address.
 * @scic_user_io_request:
 *
 * The packet IO's cdb adress.
 */
void *scic_cb_stp_packet_io_request_get_cdb_address(
	void *scic_user_io_request)
{
	return isci_request_stp_packet_io_request_get_cdb_address(
		       scic_user_io_request
		       );
}


/**
 * scic_cb_stp_packet_io_request_get_cdb_length() - This user callback asks the
 *    user to provide stp packet io's the CDB length.
 * @scic_user_io_request:
 *
 * The packet IO's cdb length.
 */
u32 scic_cb_stp_packet_io_request_get_cdb_length(
	void *scic_user_io_request)
{
	return isci_request_stp_packet_io_request_get_cdb_length(
		       scic_user_io_request
		       );
}
#endif /* #if !defined(DISABLE_ATAPI) */


/**
 * scic_cb_io_request_do_copy_rx_frames() - This callback method asks the user
 *    if the received RX frame data is to be copied to the SGL or should be
 *    stored by the SCI core to be retrieved later with the
 *    scic_io_request_get_rx_frame().
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns true if the SCI core should copy the received frame data
 * to the SGL location or false if the SCI user wants to retrieve the frame
 * data at a later time.
 */
bool scic_cb_io_request_do_copy_rx_frames(
	void *scic_user_io_request)
{
	struct sas_task *task
		= isci_request_access_task(
		(struct isci_request *)scic_user_io_request
		);

	return (task->data_dir == DMA_NONE) ? false : true;
}

/**
 * scic_cb_get_virtual_address() - This callback method asks the user to
 *    provide the virtual address for the supplied physical address.
 * @controller: This parameter is the core controller object handle.
 * @physical_address: This parameter is the physical address which is to be
 *    returned as a virtual address.
 *
 * The method returns the virtual address for the supplied physical address.
 */
void *scic_cb_get_virtual_address(
	struct scic_sds_controller *controller,
	dma_addr_t physical_address)
{
	void *virt_addr = (void *)phys_to_virt(physical_address);

	return virt_addr;
}

/**
 * scic_cb_request_get_sat_protocol() - This callback method asks the user to
 *    return the SAT protocol definition for this IO request.  This method is
 *    only called by the SCI core if the request type constructed is SATA.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * This method returns one of the sat.h defined protocols for the given io
 * request.
 */
u8 scic_cb_request_get_sat_protocol(
	void *scic_user_io_request)
{
	return isci_sata_get_sat_protocol(
		       (struct isci_request *)scic_user_io_request
		       );
}
