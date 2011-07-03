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
 * scic_cb_timer_create() - This callback method asks the user to create a
 *    timer and provide a handle for this timer for use in further timer
 *    interactions. The appropriate isci timer object function is called to
 *    create a timer object.
 * @timer_callback: This parameter specifies the callback method to be invoked
 *    whenever the timer expires.
 * @controller: This parameter specifies the controller with which this timer
 *    is to be associated.
 * @cookie: This parameter specifies a piece of information that the user must
 *    retain.  This cookie is to be supplied by the user anytime a timeout
 *    occurs for the created timer.
 *
 * This method returns a handle to a timer object created by the user.  The
 * handle will be utilized for all further interactions relating to this timer.
 */
void *scic_cb_timer_create(
	struct scic_sds_controller *controller,
	void (*timer_callback)(void *),
	void *cookie)
{
	struct isci_host *isci_host;
	struct isci_timer *timer = NULL;

	isci_host = (struct isci_host *)sci_object_get_association(controller);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_host = %p",
		__func__, isci_host);

	timer = isci_timer_create(&isci_host->timer_list_struct,
				  isci_host,
				  cookie,
				  timer_callback);

	dev_dbg(&isci_host->pdev->dev, "%s: timer = %p\n", __func__, timer);

	return (void *)timer;
}


/**
 * scic_cb_timer_start() - This callback method asks the user to start the
 *    supplied timer. The appropriate isci timer object function is called to
 *    start the timer.
 * @controller: This parameter specifies the controller with which this timer
 *    is to associated.
 * @timer: This parameter specifies the timer to be started.
 * @milliseconds: This parameter specifies the number of milliseconds for which
 *    to stall.  The operating system driver is allowed to round this value up
 *    where necessary.
 *
 */
void scic_cb_timer_start(
	struct scic_sds_controller *controller,
	void *timer,
	u32 milliseconds)
{
	struct isci_host *isci_host;

	isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_host = %p, timer = %p, milliseconds = %d\n",
		__func__, isci_host, timer, milliseconds);

	isci_timer_start((struct isci_timer *)timer, milliseconds);

}

/**
 * scic_cb_timer_stop() - This callback method asks the user to stop the
 *    supplied timer. The appropriate isci timer object function is called to
 *    stop the timer.
 * @controller: This parameter specifies the controller with which this timer
 *    is to associated.
 * @timer: This parameter specifies the timer to be stopped.
 *
 */
void scic_cb_timer_stop(
	struct scic_sds_controller *controller,
	void *timer)
{
	struct isci_host *isci_host;

	isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_host = %p, timer = %p\n",
		__func__, isci_host, timer);

	isci_timer_stop((struct isci_timer *)timer);
}

/**
 * scic_cb_controller_start_complete() - This user callback will inform the
 *    user that the controller has finished the start process. The associated
 *    isci host adapter's start_complete function is called.
 * @controller: This parameter specifies the controller that was started.
 * @completion_status: This parameter specifies the results of the start
 *    operation.  SCI_SUCCESS indicates successful completion.
 *
 */
void scic_cb_controller_start_complete(
	struct scic_sds_controller *controller,
	enum sci_status completion_status)
{
	struct isci_host *isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_host = %p\n", __func__, isci_host);

	isci_host_start_complete(isci_host, completion_status);
}

/**
 * scic_cb_controller_stop_complete() - This user callback will inform the user
 *    that the controller has finished the stop process. The associated isci
 *    host adapter's start_complete function is called.
 * @controller: This parameter specifies the controller that was stopped.
 * @completion_status: This parameter specifies the results of the stop
 *    operation.  SCI_SUCCESS indicates successful completion.
 *
 */
void scic_cb_controller_stop_complete(
	struct scic_sds_controller *controller,
	enum sci_status completion_status)
{
	struct isci_host *isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	dev_dbg(&isci_host->pdev->dev,
		"%s: status = 0x%x\n", __func__, completion_status);
	isci_host_stop_complete(isci_host, completion_status);
}

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
	struct scic_sds_request *scic_io_request,
	enum sci_io_status completion_status)
{
	struct isci_request *request;
	struct isci_host *isci_host;

	isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	request =
		(struct isci_request *)sci_object_get_association(
			scic_io_request
			);

	isci_request_io_request_complete(isci_host,
					 request,
					 completion_status);
}

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
	struct scic_sds_request *scic_task_request,
	enum sci_task_status completion_status)
{
	struct isci_request *request;
	struct isci_host *isci_host;

	isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	request =
		(struct isci_request *)sci_object_get_association(
			scic_task_request);

	isci_task_request_complete(isci_host, request, completion_status);
}

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
	enum sci_status completion_status)
{
	pr_warn("%s:************************************************\n",
		__func__);
}

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
	enum sci_status completion_status)
{
	struct isci_port *isci_port
		= (struct isci_port *)sci_object_get_association(port);

	isci_port_hard_reset_complete(isci_port, completion_status);
}

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
	struct scic_sds_port *port)
{
	struct isci_port *isci_port;
	struct isci_host *isci_host;

	isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	isci_port =
		(struct isci_port *)sci_object_get_association(port);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_port = %p\n", __func__, isci_port);

	isci_port_ready(isci_host, isci_port);
}

/**
 * scic_cb_port_not_ready() - This method informs the user that the port is now
 *    not in a ready (i.e. busy) state and can't be utilized to issue IOs.
 * @controller: This parameter represents the controller which contains the
 *    port.
 * @port: This parameter specifies the SCI port object for which the callback
 *    is being invoked.
 *
 */
void scic_cb_port_not_ready(
	struct scic_sds_controller *controller,
	struct scic_sds_port *port,
	u32 reason_code)
{
	struct isci_port *isci_port;
	struct isci_host *isci_host;

	isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	isci_port =
		(struct isci_port *)sci_object_get_association(port);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_port = %p\n", __func__, isci_port);

	isci_port_not_ready(isci_host, isci_port);
}

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
	struct scic_sds_phy *phy)
{
	pr_warn("%s:************************************************\n",
		__func__);
}

/**
 * scic_cb_port_bc_change_primitive_received() - This callback method informs
 *    the user that a broadcast change primitive was received.
 * @controller: This parameter represents the controller which contains the
 *    port.
 * @port: This parameter specifies the SCI port object for which the callback
 *    is being invoked.  For instances where the phy on which the primitive was
 *    received is not part of a port, this parameter will be
 *    SCI_INVALID_HANDLE_T.
 * @phy: This parameter specifies the phy on which the primitive was received.
 *
 */
void scic_cb_port_bc_change_primitive_received(
	struct scic_sds_controller *controller,
	struct scic_sds_port *port,
	struct scic_sds_phy *phy)
{
	struct isci_host *isci_host;

	isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	dev_dbg(&isci_host->pdev->dev,
		"%s: port = %p, phy = %p\n", __func__, port, phy);
	isci_port_bc_change_received(isci_host, port, phy);
}




/**
 * scic_cb_port_link_up() - This callback method informs the user that a phy
 *    has become operational and is capable of communicating with the remote
 *    end point.
 * @controller: This parameter represents the controller associated with the
 *    phy.
 * @port: This parameter specifies the port object for which the user callback
 *    is being invoked.  There may be conditions where this parameter can be
 *    SCI_INVALID_HANDLE
 * @phy: This parameter specifies the phy object for which the user callback is
 *    being invoked.
 *
 * none.
 */
void scic_cb_port_link_up(
	struct scic_sds_controller *controller,
	struct scic_sds_port *port,
	struct scic_sds_phy *phy)
{
	struct isci_host *isci_host;

	isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	dev_dbg(&isci_host->pdev->dev,
		"%s: phy = %p\n", __func__, phy);

	isci_port_link_up(isci_host, port, phy);
}

/**
 * scic_cb_port_link_down() - This callback method informs the user that a phy
 *    is no longer operational and is not capable of communicating with the
 *    remote end point.
 * @controller: This parameter represents the controller associated with the
 *    phy.
 * @port: This parameter specifies the port object for which the user callback
 *    is being invoked.  There may be conditions where this parameter can be
 *    SCI_INVALID_HANDLE
 * @phy: This parameter specifies the phy object for which the user callback is
 *    being invoked.
 *
 * none.
 */
void scic_cb_port_link_down(
	struct scic_sds_controller *controller,
	struct scic_sds_port *port,
	struct scic_sds_phy *phy)
{
	struct isci_host *isci_host;
	struct isci_phy *isci_phy;
	struct isci_port *isci_port;

	isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	isci_phy =
		(struct isci_phy *)sci_object_get_association(phy);

	isci_port =
		(struct isci_port *)sci_object_get_association(port);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_port = %p\n", __func__, isci_port);

	isci_port_link_down(isci_host, isci_phy, isci_port);
}

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
	enum sci_status completion_status)
{
	struct isci_host *isci_host;
	struct isci_remote_device *isci_device;

	isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	isci_device =
		(struct isci_remote_device *)sci_object_get_association(
			remote_device
			);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p\n", __func__, isci_device);

	isci_remote_device_start_complete(
		isci_host, isci_device, completion_status);

}

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
	enum sci_status completion_status)
{
	struct isci_host *isci_host;
	struct isci_remote_device *isci_device;

	isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	isci_device =
		(struct isci_remote_device *)sci_object_get_association(
			remote_device
			);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p\n", __func__, isci_device);

	isci_remote_device_stop_complete(
		isci_host, isci_device, completion_status);

}

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
	struct scic_sds_remote_device *remote_device)
{
	struct isci_remote_device *isci_device =
		(struct isci_remote_device *)
		sci_object_get_association(remote_device);

	dev_dbg(&isci_device->isci_port->isci_host->pdev->dev,
		"%s: isci_device = %p\n", __func__, isci_device);

	isci_remote_device_ready(isci_device);
}

/**
 * scic_cb_remote_device_not_ready() - This user callback method will inform
 *    the user that a remote device is no longer capable of handling IO
 *    requests (until a ready callback is invoked).
 * @controller: This parameter specifies the core controller associated with
 *    the completion callback.
 * @remote_device: This parameter specifies the remote device associated with
 *    the callback.
 * @reason_code: This parameter specifies the reason for the remote device
 *    going to a not ready state.
 *
 */
void scic_cb_remote_device_not_ready(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	u32 reason_code)
{
	struct isci_remote_device *isci_device =
		(struct isci_remote_device *)
		sci_object_get_association(remote_device);

	struct isci_host *isci_host;

	isci_host =
		(struct isci_host *)sci_object_get_association(controller);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p, reason_code = %x\n",
		__func__, isci_device, reason_code);

	isci_remote_device_not_ready(isci_device, reason_code);
}


