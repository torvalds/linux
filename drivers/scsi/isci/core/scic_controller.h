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

#ifndef _SCIC_CONTROLLER_H_
#define _SCIC_CONTROLLER_H_

/**
 * This file contains all of the interface methods that can be called by an
 *    SCIC user on a controller object.
 *
 *
 */


#include "sci_status.h"
#include "sci_controller.h"
#include "scic_config_parameters.h"

struct scic_sds_request;
struct scic_sds_phy;
struct scic_sds_port;
struct scic_sds_remote_device;


enum sci_controller_mode {
	SCI_MODE_SPEED,		/* Optimized for performance */
	SCI_MODE_SIZE		/* Optimized for memory use */
};


/**
 * scic_controller_construct() - This method will attempt to construct a
 *    controller object utilizing the supplied parameter information.
 * @c: This parameter specifies the controller to be constructed.
 * @scu_base: mapped base address of the scu registers
 * @smu_base: mapped base address of the smu registers
 *
 * Indicate if the controller was successfully constructed or if it failed in
 * some way. SCI_SUCCESS This value is returned if the controller was
 * successfully constructed. SCI_WARNING_TIMER_CONFLICT This value is returned
 * if the interrupt coalescence timer may cause SAS compliance issues for SMP
 * Target mode response processing. SCI_FAILURE_UNSUPPORTED_CONTROLLER_TYPE
 * This value is returned if the controller does not support the supplied type.
 * SCI_FAILURE_UNSUPPORTED_INIT_DATA_VERSION This value is returned if the
 * controller does not support the supplied initialization data version.
 */
enum sci_status scic_controller_construct(struct scic_sds_controller *c,
					  void __iomem *scu_base,
					  void __iomem *smu_base);

/**
 * scic_controller_enable_interrupts() - This method will enable all controller
 *    interrupts.
 * @controller: This parameter specifies the controller for which to enable
 *    interrupts.
 *
 */
void scic_controller_enable_interrupts(
	struct scic_sds_controller *controller);

/**
 * scic_controller_disable_interrupts() - This method will disable all
 *    controller interrupts.
 * @controller: This parameter specifies the controller for which to disable
 *    interrupts.
 *
 */
void scic_controller_disable_interrupts(
	struct scic_sds_controller *controller);


/**
 * scic_controller_initialize() - This method will initialize the controller
 *    hardware managed by the supplied core controller object.  This method
 *    will bring the physical controller hardware out of reset and enable the
 *    core to determine the capabilities of the hardware being managed.  Thus,
 *    the core controller can determine it's exact physical (DMA capable)
 *    memory requirements.
 * @controller: This parameter specifies the controller to be initialized.
 *
 * The SCI Core user must have called scic_controller_construct() on the
 * supplied controller object previously. Indicate if the controller was
 * successfully initialized or if it failed in some way. SCI_SUCCESS This value
 * is returned if the controller hardware was successfully initialized.
 */
enum sci_status scic_controller_initialize(
	struct scic_sds_controller *controller);

/**
 * scic_controller_get_suggested_start_timeout() - This method returns the
 *    suggested scic_controller_start() timeout amount.  The user is free to
 *    use any timeout value, but this method provides the suggested minimum
 *    start timeout value.  The returned value is based upon empirical
 *    information determined as a result of interoperability testing.
 * @controller: the handle to the controller object for which to return the
 *    suggested start timeout.
 *
 * This method returns the number of milliseconds for the suggested start
 * operation timeout.
 */
u32 scic_controller_get_suggested_start_timeout(
	struct scic_sds_controller *controller);

/**
 * scic_controller_start() - This method will start the supplied core
 *    controller.  This method will start the staggered spin up operation.  The
 *    SCI User completion callback is called when the following conditions are
 *    met: -# the return status of this method is SCI_SUCCESS. -# after all of
 *    the phys have successfully started or been given the opportunity to start.
 * @controller: the handle to the controller object to start.
 * @timeout: This parameter specifies the number of milliseconds in which the
 *    start operation should complete.
 *
 * The SCI Core user must have filled in the physical memory descriptor
 * structure via the sci_controller_get_memory_descriptor_list() method. The
 * SCI Core user must have invoked the scic_controller_initialize() method
 * prior to invoking this method. The controller must be in the INITIALIZED or
 * STARTED state. Indicate if the controller start method succeeded or failed
 * in some way. SCI_SUCCESS if the start operation succeeded.
 * SCI_WARNING_ALREADY_IN_STATE if the controller is already in the STARTED
 * state. SCI_FAILURE_INVALID_STATE if the controller is not either in the
 * INITIALIZED or STARTED states. SCI_FAILURE_INVALID_MEMORY_DESCRIPTOR if
 * there are inconsistent or invalid values in the supplied
 * struct sci_physical_memory_descriptor array.
 */
enum sci_status scic_controller_start(
	struct scic_sds_controller *controller,
	u32 timeout);

/**
 * scic_controller_stop() - This method will stop an individual controller
 *    object.This method will invoke the associated user callback upon
 *    completion.  The completion callback is called when the following
 *    conditions are met: -# the method return status is SCI_SUCCESS. -# the
 *    controller has been quiesced. This method will ensure that all IO
 *    requests are quiesced, phys are stopped, and all additional operation by
 *    the hardware is halted.
 * @controller: the handle to the controller object to stop.
 * @timeout: This parameter specifies the number of milliseconds in which the
 *    stop operation should complete.
 *
 * The controller must be in the STARTED or STOPPED state. Indicate if the
 * controller stop method succeeded or failed in some way. SCI_SUCCESS if the
 * stop operation successfully began. SCI_WARNING_ALREADY_IN_STATE if the
 * controller is already in the STOPPED state. SCI_FAILURE_INVALID_STATE if the
 * controller is not either in the STARTED or STOPPED states.
 */
enum sci_status scic_controller_stop(
	struct scic_sds_controller *controller,
	u32 timeout);

/**
 * scic_controller_reset() - This method will reset the supplied core
 *    controller regardless of the state of said controller.  This operation is
 *    considered destructive.  In other words, all current operations are wiped
 *    out.  No IO completions for outstanding devices occur.  Outstanding IO
 *    requests are not aborted or completed at the actual remote device.
 * @controller: the handle to the controller object to reset.
 *
 * Indicate if the controller reset method succeeded or failed in some way.
 * SCI_SUCCESS if the reset operation successfully started. SCI_FATAL_ERROR if
 * the controller reset operation is unable to complete.
 */
enum sci_status scic_controller_reset(
	struct scic_sds_controller *controller);

/**
 * scic_controller_start_io() - This method is called by the SCI user to
 *    send/start an IO request. If the method invocation is successful, then
 *    the IO request has been queued to the hardware for processing.
 * @controller: the handle to the controller object for which to start an IO
 *    request.
 * @remote_device: the handle to the remote device object for which to start an
 *    IO request.
 * @io_request: the handle to the io request object to start.
 * @io_tag: This parameter specifies a previously allocated IO tag that the
 *    user desires to be utilized for this request. This parameter is optional.
 *     The user is allowed to supply SCI_CONTROLLER_INVALID_IO_TAG as the value
 *    for this parameter.
 *
 * - IO tags are a protected resource.  It is incumbent upon the SCI Core user
 * to ensure that each of the methods that may allocate or free available IO
 * tags are handled in a mutually exclusive manner.  This method is one of said
 * methods requiring proper critical code section protection (e.g. semaphore,
 * spin-lock, etc.). - For SATA, the user is required to manage NCQ tags.  As a
 * result, it is expected the user will have set the NCQ tag field in the host
 * to device register FIS prior to calling this method.  There is also a
 * requirement for the user to call scic_stp_io_set_ncq_tag() prior to invoking
 * the scic_controller_start_io() method. scic_controller_allocate_tag() for
 * more information on allocating a tag. Indicate if the controller
 * successfully started the IO request. SCI_IO_SUCCESS if the IO request was
 * successfully started. Determine the failure situations and return values.
 */
enum sci_io_status scic_controller_start_io(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *io_request,
	u16 io_tag);


/**
 * scic_controller_start_task() - This method is called by the SCIC user to
 *    send/start a framework task management request.
 * @controller: the handle to the controller object for which to start the task
 *    management request.
 * @remote_device: the handle to the remote device object for which to start
 *    the task management request.
 * @task_request: the handle to the task request object to start.
 * @io_tag: This parameter specifies a previously allocated IO tag that the
 *    user desires to be utilized for this request.  Note this not the io_tag
 *    of the request being managed.  It is to be utilized for the task request
 *    itself. This parameter is optional.  The user is allowed to supply
 *    SCI_CONTROLLER_INVALID_IO_TAG as the value for this parameter.
 *
 * - IO tags are a protected resource.  It is incumbent upon the SCI Core user
 * to ensure that each of the methods that may allocate or free available IO
 * tags are handled in a mutually exclusive manner.  This method is one of said
 * methods requiring proper critical code section protection (e.g. semaphore,
 * spin-lock, etc.). - The user must synchronize this task with completion
 * queue processing.  If they are not synchronized then it is possible for the
 * io requests that are being managed by the task request can complete before
 * starting the task request. scic_controller_allocate_tag() for more
 * information on allocating a tag. Indicate if the controller successfully
 * started the IO request. SCI_TASK_SUCCESS if the task request was
 * successfully started. SCI_TASK_FAILURE_REQUIRES_SCSI_ABORT This value is
 * returned if there is/are task(s) outstanding that require termination or
 * completion before this request can succeed.
 */
enum sci_task_status scic_controller_start_task(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *task_request,
	u16 io_tag);

/**
 * scic_controller_complete_task() - This method will perform core specific
 *    completion operations for task management request. After this method is
 *    invoked, the user should consider the task request as invalid until it is
 *    properly reused (i.e. re-constructed).
 * @controller: The handle to the controller object for which to complete the
 *    task management request.
 * @remote_device: The handle to the remote device object for which to complete
 *    the task management request.
 * @task_request: the handle to the task management request object to complete.
 *
 * Indicate if the controller successfully completed the task management
 * request. SCI_SUCCESS if the completion process was successful.
 */
enum sci_status scic_controller_complete_task(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *task_request);


/**
 * scic_controller_terminate_request() - This method is called by the SCI Core
 *    user to terminate an ongoing (i.e. started) core IO request.  This does
 *    not abort the IO request at the target, but rather removes the IO request
 *    from the host controller.
 * @controller: the handle to the controller object for which to terminate a
 *    request.
 * @remote_device: the handle to the remote device object for which to
 *    terminate a request.
 * @request: the handle to the io or task management request object to
 *    terminate.
 *
 * Indicate if the controller successfully began the terminate process for the
 * IO request. SCI_SUCCESS if the terminate process was successfully started
 * for the request. Determine the failure situations and return values.
 */
enum sci_status scic_controller_terminate_request(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *request);

/**
 * scic_controller_complete_io() - This method will perform core specific
 *    completion operations for an IO request.  After this method is invoked,
 *    the user should consider the IO request as invalid until it is properly
 *    reused (i.e. re-constructed).
 * @controller: The handle to the controller object for which to complete the
 *    IO request.
 * @remote_device: The handle to the remote device object for which to complete
 *    the IO request.
 * @io_request: the handle to the io request object to complete.
 *
 * - IO tags are a protected resource.  It is incumbent upon the SCI Core user
 * to ensure that each of the methods that may allocate or free available IO
 * tags are handled in a mutually exclusive manner.  This method is one of said
 * methods requiring proper critical code section protection (e.g. semaphore,
 * spin-lock, etc.). - If the IO tag for a request was allocated, by the SCI
 * Core user, using the scic_controller_allocate_io_tag() method, then it is
 * the responsibility of the caller to invoke the scic_controller_free_io_tag()
 * method to free the tag (i.e. this method will not free the IO tag). Indicate
 * if the controller successfully completed the IO request. SCI_SUCCESS if the
 * completion process was successful.
 */
enum sci_status scic_controller_complete_io(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *io_request);


/**
 * scic_controller_get_port_handle() - This method simply provides the user
 *    with a unique handle for a given SAS/SATA core port index.
 * @controller: This parameter represents the handle to the controller object
 *    from which to retrieve a port (SAS or SATA) handle.
 * @port_index: This parameter specifies the port index in the controller for
 *    which to retrieve the port handle. 0 <= port_index < maximum number of
 *    phys.
 * @port_handle: This parameter specifies the retrieved port handle to be
 *    provided to the caller.
 *
 * Indicate if the retrieval of the port handle was successful. SCI_SUCCESS
 * This value is returned if the retrieval was successful.
 * SCI_FAILURE_INVALID_PORT This value is returned if the supplied port id is
 * not in the supported range.
 */
enum sci_status scic_controller_get_port_handle(
	struct scic_sds_controller *controller,
	u8 port_index,
	struct scic_sds_port **port_handle);

/**
 * scic_controller_get_phy_handle() - This method simply provides the user with
 *    a unique handle for a given SAS/SATA phy index/identifier.
 * @controller: This parameter represents the handle to the controller object
 *    from which to retrieve a phy (SAS or SATA) handle.
 * @phy_index: This parameter specifies the phy index in the controller for
 *    which to retrieve the phy handle. 0 <= phy_index < maximum number of phys.
 * @phy_handle: This parameter specifies the retrieved phy handle to be
 *    provided to the caller.
 *
 * Indicate if the retrieval of the phy handle was successful. SCI_SUCCESS This
 * value is returned if the retrieval was successful. SCI_FAILURE_INVALID_PHY
 * This value is returned if the supplied phy id is not in the supported range.
 */
enum sci_status scic_controller_get_phy_handle(
	struct scic_sds_controller *controller,
	u8 phy_index,
	struct scic_sds_phy **phy_handle);

/**
 * scic_controller_allocate_io_tag() - This method will allocate a tag from the
 *    pool of free IO tags. Direct allocation of IO tags by the SCI Core user
 *    is optional. The scic_controller_start_io() method will allocate an IO
 *    tag if this method is not utilized and the tag is not supplied to the IO
 *    construct routine.  Direct allocation of IO tags may provide additional
 *    performance improvements in environments capable of supporting this usage
 *    model.  Additionally, direct allocation of IO tags also provides
 *    additional flexibility to the SCI Core user.  Specifically, the user may
 *    retain IO tags across the lives of multiple IO requests.
 * @controller: the handle to the controller object for which to allocate the
 *    tag.
 *
 * IO tags are a protected resource.  It is incumbent upon the SCI Core user to
 * ensure that each of the methods that may allocate or free available IO tags
 * are handled in a mutually exclusive manner.  This method is one of said
 * methods requiring proper critical code section protection (e.g. semaphore,
 * spin-lock, etc.). An unsigned integer representing an available IO tag.
 * SCI_CONTROLLER_INVALID_IO_TAG This value is returned if there are no
 * currently available tags to be allocated. All return other values indicate a
 * legitimate tag.
 */
u16 scic_controller_allocate_io_tag(
	struct scic_sds_controller *controller);

/**
 * scic_controller_free_io_tag() - This method will free an IO tag to the pool
 *    of free IO tags. This method provides the SCI Core user more flexibility
 *    with regards to IO tags.  The user may desire to keep an IO tag after an
 *    IO request has completed, because they plan on re-using the tag for a
 *    subsequent IO request.  This method is only legal if the tag was
 *    allocated via scic_controller_allocate_io_tag().
 * @controller: This parameter specifies the handle to the controller object
 *    for which to free/return the tag.
 * @io_tag: This parameter represents the tag to be freed to the pool of
 *    available tags.
 *
 * - IO tags are a protected resource.  It is incumbent upon the SCI Core user
 * to ensure that each of the methods that may allocate or free available IO
 * tags are handled in a mutually exclusive manner.  This method is one of said
 * methods requiring proper critical code section protection (e.g. semaphore,
 * spin-lock, etc.). - If the IO tag for a request was allocated, by the SCI
 * Core user, using the scic_controller_allocate_io_tag() method, then it is
 * the responsibility of the caller to invoke this method to free the tag. This
 * method returns an indication of whether the tag was successfully put back
 * (freed) to the pool of available tags. SCI_SUCCESS This return value
 * indicates the tag was successfully placed into the pool of available IO
 * tags. SCI_FAILURE_INVALID_IO_TAG This value is returned if the supplied tag
 * is not a valid IO tag value.
 */
enum sci_status scic_controller_free_io_tag(
	struct scic_sds_controller *controller,
	u16 io_tag);




/**
 * scic_controller_set_mode() - This method allows the user to configure the
 *    SCI core into either a performance mode or a memory savings mode.
 * @controller: This parameter represents the handle to the controller object
 *    for which to update the operating mode.
 * @mode: This parameter specifies the new mode for the controller.
 *
 * Indicate if the user successfully change the operating mode of the
 * controller. SCI_SUCCESS The user successfully updated the mode.
 */
enum sci_status scic_controller_set_mode(
	struct scic_sds_controller *controller,
	enum sci_controller_mode mode);


/**
 * scic_controller_set_interrupt_coalescence() - This method allows the user to
 *    configure the interrupt coalescence.
 * @controller: This parameter represents the handle to the controller object
 *    for which its interrupt coalesce register is overridden.
 * @coalesce_number: Used to control the number of entries in the Completion
 *    Queue before an interrupt is generated. If the number of entries exceed
 *    this number, an interrupt will be generated. The valid range of the input
 *    is [0, 256]. A setting of 0 results in coalescing being disabled.
 * @coalesce_timeout: Timeout value in microseconds. The valid range of the
 *    input is [0, 2700000] . A setting of 0 is allowed and results in no
 *    interrupt coalescing timeout.
 *
 * Indicate if the user successfully set the interrupt coalesce parameters.
 * SCI_SUCCESS The user successfully updated the interrutp coalescence.
 * SCI_FAILURE_INVALID_PARAMETER_VALUE The user input value is out of range.
 */
enum sci_status scic_controller_set_interrupt_coalescence(
	struct scic_sds_controller *controller,
	u32 coalesce_number,
	u32 coalesce_timeout);

struct device;
struct scic_sds_controller *scic_controller_alloc(struct device *dev);


#endif  /* _SCIC_CONTROLLER_H_ */

