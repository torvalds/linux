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

#ifndef _SCIC_TASK_REQUEST_H_
#define _SCIC_TASK_REQUEST_H_

/**
 * This file contains the structures and interface methods that can be
 *    referenced and used by the SCI user for to utilize task management
 *    requests.
 *
 *
 */


#include "sci_status.h"

struct scic_sds_request;
struct scic_sds_remote_device;
struct scic_sds_controller;


/**
 * scic_task_request_construct() - This method is called by the SCI user to
 *    construct all SCI Core task management requests, regardless of protocol.
 *    Memory initialization and functionality common to all task request types
 *    is performed in this method.
 * @scic_controller: the handle to the core controller object for which to
 *    build the task managmement request.
 * @scic_remote_device: the handle to the core remote device object for which
 *    to build the task management request. passed, then a copy of the request
 *    is built internally.  The request will be copied into the actual
 *    controller request memory when the task is allocated internally during
 *    the scic_controller_start_task() method.
 * @io_tag: This parameter specifies the IO tag to be associated with this
 *    request.  If SCI_CONTROLLER_INVALID_IO_TAG is passed, then a copy of the
 *    request is built internally.  The request will be copied into the actual
 *    controller request memory when the IO tag is allocated internally during
 *    the scic_controller_start_io() method.
 * @user_task_request_object: This parameter specifies the user task request to
 *    be utilized during construction.  This task pointer will become the
 *    associated object for the core task request object.
 * @scic_task_request_memory: This parameter specifies the memory location to
 *    be utilized when building the core request.
 * @new_scic_task_request_handle: This parameter specifies a pointer to the
 *    handle the core will expect in further interactions with the core task
 *    request object.
 *
 * The SCI core implementation will create an association between the user task
 * request object and the core task request object. Indicate if the controller
 * successfully built the task request. SCI_SUCCESS This value is returned if
 * the task request was successfully built.
 */
enum sci_status scic_task_request_construct(
	struct scic_sds_controller *scic_controller,
	struct scic_sds_remote_device *scic_remote_device,
	u16 io_tag,
	void *user_task_request_object,
	void *scic_task_request_memory,
	struct scic_sds_request **new_scic_task_request_handle);

/**
 * scic_task_request_construct_ssp() - This method is called by the SCI user to
 *    construct all SCI Core SSP task management requests.  Memory
 *    initialization and functionality common to all task request types is
 *    performed in this method.
 * @scic_task_request: This parameter specifies the handle to the core task
 *    request object for which to construct a SATA specific task management
 *    request.
 *
 * Indicate if the controller successfully built the task request. SCI_SUCCESS
 * This value is returned if the task request was successfully built.
 */
enum sci_status scic_task_request_construct_ssp(
	struct scic_sds_request *scic_task_request);

/**
 * scic_task_request_construct_sata() - This method is called by the SCI user
 *    to construct all SCI Core SATA task management requests.  Memory
 *    initialization and functionality common to all task request types is
 *    performed in this method.
 * @scic_task_request_handle: This parameter specifies the handle to the core
 *    task request object for which to construct a SATA specific task
 *    management request.
 *
 * Indicate if the controller successfully built the task request. SCI_SUCCESS
 * This value is returned if the task request was successfully built.
 */
enum sci_status scic_task_request_construct_sata(
	struct scic_sds_request *scic_task_request_handle);



#endif  /* _SCIC_TASK_REQUEST_H_ */

