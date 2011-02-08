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

#ifndef _SCIC_USER_CALLBACK_H_
#define _SCIC_USER_CALLBACK_H_

/**
 * This file contains all of the interface methods/macros that must be
 *    implemented by an SCI Core user.
 *
 *
 */


#include "sci_status.h"
#include "scic_io_request.h"

struct scic_sds_request;
struct scic_sds_phy;
struct scic_sds_port;
struct scic_sds_remote_device;
struct scic_sds_controller;

/**
 * scic_cb_timer_create() - This callback method asks the user to create a
 *    timer and provide a handle for this timer for use in further timer
 *    interactions.
 * @controller: This parameter specifies the controller with which this timer
 *    is to be associated.
 * @timer_callback: This parameter specifies the callback method to be invoked
 *    whenever the timer expires.
 * @cookie: This parameter specifies a piece of information that the user must
 *    retain.  This cookie is to be supplied by the user anytime a timeout
 *    occurs for the created timer.
 *
 * The "timer_callback" method should be executed in a mutually exlusive manner
 * from the controller completion handler handler (refer to
 * scic_controller_get_handler_methods()). This method returns a handle to a
 * timer object created by the user.  The handle will be utilized for all
 * further interactions relating to this timer.
 */
void *scic_cb_timer_create(
	struct scic_sds_controller *controller,
	void (*timer_callback)(void *),
	void *cookie);


/**
 * scic_cb_timer_start() - This callback method asks the user to start the
 *    supplied timer.
 * @controller: This parameter specifies the controller with which this timer
 *    is to associated.
 * @timer: This parameter specifies the timer to be started.
 * @milliseconds: This parameter specifies the number of milliseconds for which
 *    to stall.  The operating system driver is allowed to round this value up
 *    where necessary.
 *
 * All timers in the system started by the SCI Core are one shot timers.
 * Therefore, the SCI user should make sure that it removes the timer from it's
 * list when a timer actually fires. Additionally, SCI Core user's should be
 * able to handle calls from the SCI Core to stop a timer that may already be
 * stopped. none
 */
void scic_cb_timer_start(
	struct scic_sds_controller *controller,
	void *timer,
	u32 milliseconds);

/**
 * scic_cb_timer_stop() - This callback method asks the user to stop the
 *    supplied timer.
 * @controller: This parameter specifies the controller with which this timer
 *    is to associated.
 * @timer: This parameter specifies the timer to be stopped.
 *
 */
void scic_cb_timer_stop(
	struct scic_sds_controller *controller,
	void *timer);

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
	u32 microseconds);

/**
 * scic_cb_controller_start_complete() - This user callback will inform the
 *    user that the controller has finished the start process.
 * @controller: This parameter specifies the controller that was started.
 * @completion_status: This parameter specifies the results of the start
 *    operation.  SCI_SUCCESS indicates successful completion.
 *
 */
void scic_cb_controller_start_complete(
	struct scic_sds_controller *controller,
	enum sci_status completion_status);

/**
 * scic_cb_controller_stop_complete() - This user callback will inform the user
 *    that the controller has finished the stop process.
 * @controller: This parameter specifies the controller that was stopped.
 * @completion_status: This parameter specifies the results of the stop
 *    operation.  SCI_SUCCESS indicates successful completion.
 *
 */
void scic_cb_controller_stop_complete(
	struct scic_sds_controller *controller,
	enum sci_status completion_status);

/**
 * scic_cb_io_request_complete() - This user callback will inform the user that
 *    an IO request has completed.
 * @controller: This parameter specifies the controller on which the IO is
 *    completing.
 * @remote_device: This parameter specifies the remote device on which this IO
 *    request is completing.
 * @io_request: This parameter specifies the IO request that has completed.
 * @completion_status: This parameter specifies the results of the IO request
 *    operation.  SCI_SUCCESS indicates successful completion.
 *
 */
void scic_cb_io_request_complete(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *io_request,
	enum sci_io_status completion_status);

/**
 * scic_cb_task_request_complete() - This user callback will inform the user
 *    that a task management request completed.
 * @controller: This parameter specifies the controller on which the task
 *    management request is completing.
 * @remote_device: This parameter specifies the remote device on which this
 *    task management request is completing.
 * @task_request: This parameter specifies the task management request that has
 *    completed.
 * @completion_status: This parameter specifies the results of the IO request
 *    operation.  SCI_SUCCESS indicates successful completion.
 *
 */
void scic_cb_task_request_complete(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *task_request,
	enum sci_task_status completion_status);

#ifndef SCI_GET_PHYSICAL_ADDRESS_OPTIMIZATION_ENABLED
/**
 * scic_cb_io_request_get_physical_address() - This callback method asks the
 *    user to provide the physical address for the supplied virtual address
 *    when building an io request object.
 * @controller: This parameter is the core controller object handle.
 * @io_request: This parameter is the io request object handle for which the
 *    physical address is being requested.
 * @virtual_address: This paramter is the virtual address which is to be
 *    returned as a physical address.
 * @physical_address: The physical address for the supplied virtual address.
 *
 * None.
 */
void scic_cb_io_request_get_physical_address(
	struct scic_sds_controller *controller,
	struct scic_sds_request *io_request,
	void *virtual_address,
	dma_addr_t *physical_address);
#endif /* SCI_GET_PHYSICAL_ADDRESS_OPTIMIZATION_ENABLED */

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
	void *scic_user_io_request);

/**
 * scic_cb_io_request_get_data_direction() - This callback method asks the user
 *    to provide the data direction for this request.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 */
enum dma_data_direction scic_cb_io_request_get_data_direction(void *req);

#ifndef SCI_SGL_OPTIMIZATION_ENABLED
/**
 * scic_cb_io_request_get_next_sge() - This callback method asks the user to
 *    provide the address to where the next Scatter-Gather Element is located.
 *    Details regarding usage: - Regarding the first SGE: the user should
 *    initialize an index, or a pointer, prior to construction of the request
 *    that will reference the very first scatter-gather element.  This is
 *    important since this method is called for every scatter-gather element,
 *    including the first element. - Regarding the last SGE: the user should
 *    return NULL from this method when this method is called and the SGL has
 *    exhausted all elements.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 * @current_sge_address: This parameter specifies the address for the current
 *    SGE (i.e. the one that has just processed).
 * @next_sge: An address specifying the location for the next scatter gather
 *    element to be processed.
 *
 * None
 */
void scic_cb_io_request_get_next_sge(
	void *scic_user_io_request,
	void *current_sge_address,
	void **next_sge);
#endif /* SCI_SGL_OPTIMIZATION_ENABLED */

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
	void *sge_address);

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
	void *sge_address);

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
	void *scic_user_io_request);

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
	void *scic_user_io_request);

/**
 * scic_cb_ssp_io_request_get_lun() - This callback method asks the user to
 *    provide the Logical Unit (LUN) associated with this IO request.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * The contents of the value returned from this callback are defined by the
 * protocol standard (e.g. T10 SAS specification).  Please refer to the
 * transport command information unit description in the associated standard.
 * This method returns the LUN associated with this request. This should be u64?
 */
u32 scic_cb_ssp_io_request_get_lun(
	void *scic_user_io_request);

/**
 * scic_cb_ssp_io_request_get_task_attribute() - This callback method asks the
 *    user to provide the task attribute associated with this IO request.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * The contents of the value returned from this callback are defined by the
 * protocol standard (e.g. T10 SAS specification).  Please refer to the
 * transport command information unit description in the associated standard.
 * This method returns the task attribute associated with this IO request.
 */
u32 scic_cb_ssp_io_request_get_task_attribute(
	void *scic_user_io_request);

/**
 * scic_cb_ssp_io_request_get_command_priority() - This callback method asks
 *    the user to provide the command priority associated with this IO request.
 * @scic_user_io_request: This parameter points to the user's IO request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * The contents of the value returned from this callback are defined by the
 * protocol standard (e.g. T10 SAS specification).  Please refer to the
 * transport command information unit description in the associated standard.
 * This method returns the command priority associated with this IO request.
 */
u32 scic_cb_ssp_io_request_get_command_priority(
	void *scic_user_io_request);

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
	void *scic_user_io_request);

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
	void *scic_user_io_request);


/**
 * scic_cb_ssp_task_request_get_lun() - This method returns the Logical Unit to
 *    be utilized for this task management request.
 * @scic_user_task_request: This parameter points to the user's task request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * The contents of the value returned from this callback are defined by the
 * protocol standard (e.g. T10 SAS specification).  Please refer to the
 * transport task information unit description in the associated standard. This
 * method returns the LUN associated with this request. This should be u64?
 */
u32 scic_cb_ssp_task_request_get_lun(
	void *scic_user_task_request);

/**
 * scic_cb_ssp_task_request_get_function() - This method returns the task
 *    management function to be utilized for this task request.
 * @scic_user_task_request: This parameter points to the user's task request
 *    object.  It is a cookie that allows the user to provide the necessary
 *    information for this callback.
 *
 * The contents of the value returned from this callback are defined by the
 * protocol standard (e.g. T10 SAS specification).  Please refer to the
 * transport task information unit description in the associated standard. This
 * method returns an unsigned byte representing the task management function to
 * be performed.
 */
u8 scic_cb_ssp_task_request_get_function(
	void *scic_user_task_request);

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
	void *scic_user_task_request);

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
	void *scic_user_task_request);

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
	void *scic_user_task_request);

/**
 * scic_cb_pci_get_bar() - In this method the user must return the base address
 *    register (BAR) value for the supplied base address register number.
 * @controller: The controller for which to retrieve the bar number.
 * @bar_number: This parameter depicts the BAR index/number to be read.
 *
 * Return a pointer value indicating the contents of the BAR. NULL indicates an
 * invalid BAR index/number was specified. All other values indicate a valid
 * VIRTUAL address from the BAR.
 */
void *scic_cb_pci_get_bar(
	struct scic_sds_controller *controller,
	u16 bar_number);

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
	dma_addr_t physical_address);

/**
 * scic_cb_port_stop_complete() - This method informs the user when a stop
 *    operation on the port has completed.
 * @controller: This parameter represents the controller which contains the
 *    port.
 * @port: This parameter specifies the SCI port object for which the callback
 *    is being invoked.
 * @completion_status: This parameter specifies the status for the operation
 *    being completed.
 *
 */
void scic_cb_port_stop_complete(
	struct scic_sds_controller *controller,
	struct scic_sds_port *port,
	enum sci_status completion_status);

/**
 * scic_cb_port_hard_reset_complete() - This method informs the user when a
 *    hard reset on the port has completed.  This hard reset could have been
 *    initiated by the user or by the remote port.
 * @controller: This parameter represents the controller which contains the
 *    port.
 * @port: This parameter specifies the SCI port object for which the callback
 *    is being invoked.
 * @completion_status: This parameter specifies the status for the operation
 *    being completed.
 *
 */
void scic_cb_port_hard_reset_complete(
	struct scic_sds_controller *controller,
	struct scic_sds_port *port,
	enum sci_status completion_status);

/**
 * scic_cb_port_ready() - This method informs the user that the port is now in
 *    a ready state and can be utilized to issue IOs.
 * @controller: This parameter represents the controller which contains the
 *    port.
 * @port: This parameter specifies the SCI port object for which the callback
 *    is being invoked.
 *
 */
void scic_cb_port_ready(
	struct scic_sds_controller *controller,
	struct scic_sds_port *port);

/**
 * scic_cb_port_not_ready() - This method informs the user that the port is now
 *    not in a ready (i.e. busy) state and can't be utilized to issue IOs.
 * @controller: This parameter represents the controller which contains the
 *    port.
 * @port: This parameter specifies the SCI port object for which the callback
 *    is being invoked.
 * @reason_code: This parameter specifies the reason for the port not ready
 *    callback.
 *
 */
void scic_cb_port_not_ready(
	struct scic_sds_controller *controller,
	struct scic_sds_port *port,
	u32 reason_code);

/**
 * scic_cb_port_invalid_link_up() - This method informs the SCI Core user that
 *    a phy/link became ready, but the phy is not allowed in the port.  In some
 *    situations the underlying hardware only allows for certain phy to port
 *    mappings.  If these mappings are violated, then this API is invoked.
 * @controller: This parameter represents the controller which contains the
 *    port.
 * @port: This parameter specifies the SCI port object for which the callback
 *    is being invoked.
 * @phy: This parameter specifies the phy that came ready, but the phy can't be
 *    a valid member of the port.
 *
 */
void scic_cb_port_invalid_link_up(
	struct scic_sds_controller *controller,
	struct scic_sds_port *port,
	struct scic_sds_phy *phy);

/**
 * scic_cb_port_bc_change_primitive_received() - This callback method informs
 *    the user that a broadcast change primitive was received.
 * @controller: This parameter represents the controller which contains the
 *    port.
 * @port: This parameter specifies the SCI port object for which the callback
 *    is being invoked.  For instances where the phy on which the primitive was
 *    received is not part of a port, this parameter will be
 *    NULL.
 * @phy: This parameter specifies the phy on which the primitive was received.
 *
 */
void scic_cb_port_bc_change_primitive_received(
	struct scic_sds_controller *controller,
	struct scic_sds_port *port,
	struct scic_sds_phy *phy);




/**
 * scic_cb_port_link_up() - This callback method informs the user that a phy
 *    has become operational and is capable of communicating with the remote
 *    end point.
 * @controller: This parameter represents the controller associated with the
 *    phy.
 * @port: This parameter specifies the port object for which the user callback
 *    is being invoked.  There may be conditions where this parameter can be
 *    NULL
 * @phy: This parameter specifies the phy object for which the user callback is
 *    being invoked.
 *
 */
void scic_cb_port_link_up(
	struct scic_sds_controller *controller,
	struct scic_sds_port *port,
	struct scic_sds_phy *phy);

/**
 * scic_cb_port_link_down() - This callback method informs the user that a phy
 *    is no longer operational and is not capable of communicating with the
 *    remote end point.
 * @controller: This parameter represents the controller associated with the
 *    phy.
 * @port: This parameter specifies the port object for which the user callback
 *    is being invoked.  There may be conditions where this parameter can be
 *    NULL
 * @phy: This parameter specifies the phy object for which the user callback is
 *    being invoked.
 *
 */
void scic_cb_port_link_down(
	struct scic_sds_controller *controller,
	struct scic_sds_port *port,
	struct scic_sds_phy *phy);

/**
 * scic_cb_remote_device_start_complete() - This user callback method will
 *    inform the user that a start operation has completed.
 * @controller: This parameter specifies the core controller associated with
 *    the completion callback.
 * @remote_device: This parameter specifies the remote device associated with
 *    the completion callback.
 * @completion_status: This parameter specifies the completion status for the
 *    operation.
 *
 */
void scic_cb_remote_device_start_complete(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	enum sci_status completion_status);

/**
 * scic_cb_remote_device_stop_complete() - This user callback method will
 *    inform the user that a stop operation has completed.
 * @controller: This parameter specifies the core controller associated with
 *    the completion callback.
 * @remote_device: This parameter specifies the remote device associated with
 *    the completion callback.
 * @completion_status: This parameter specifies the completion status for the
 *    operation.
 *
 */
void scic_cb_remote_device_stop_complete(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	enum sci_status completion_status);

/**
 * scic_cb_remote_device_ready() - This user callback method will inform the
 *    user that a remote device is now capable of handling IO requests.
 * @controller: This parameter specifies the core controller associated with
 *    the completion callback.
 * @remote_device: This parameter specifies the remote device associated with
 *    the callback.
 *
 */
void scic_cb_remote_device_ready(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device);

/**
 * scic_cb_remote_device_not_ready() - This user callback method will inform
 *    the user that a remote device is no longer capable of handling IO
 *    requests (until a ready callback is invoked).
 * @controller: This parameter specifies the core controller associated with
 *    the completion callback.
 * @remote_device: This parameter specifies the remote device associated with
 *    the callback.
 * @reason_code: This paramete specifies the reason the remote device is not
 *    ready.
 *
 */
void scic_cb_remote_device_not_ready(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	u32 reason_code);

#if !defined(DISABLE_ATAPI)
/**
 * scic_cb_stp_packet_io_request_get_cdb_address() - This user callback gets
 *    from stp packet io's user request the CDB address.
 * @scic_user_io_request:
 *
 * The cdb adress.
 */
void *scic_cb_stp_packet_io_request_get_cdb_address(
	void *scic_user_io_request);

/**
 * scic_cb_stp_packet_io_request_get_cdb_length() - This user callback gets
 *    from stp packet io's user request the CDB length.
 * @scic_user_io_request:
 *
 * The cdb length.
 */
u32 scic_cb_stp_packet_io_request_get_cdb_length(
	void *scic_user_io_request);
#else /* !defined(DISABLE_ATAPI) */
#define scic_cb_stp_packet_io_request_get_cdb_address(scic_user_io_request) NULL
#define scic_cb_stp_packet_io_request_get_cdb_length(scic_user_io_request) 0
#endif /* !defined(DISABLE_ATAPI) */


#endif  /* _SCIC_USER_CALLBACK_H_ */

