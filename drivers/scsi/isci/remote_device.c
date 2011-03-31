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

#include "isci.h"
#include "scic_io_request.h"
#include "scic_remote_device.h"
#include "scic_phy.h"
#include "scic_port.h"
#include "port.h"
#include "remote_device.h"
#include "request.h"
#include "task.h"



/**
 * isci_remote_device_deconstruct() - This function frees an isci_remote_device.
 * @ihost: This parameter specifies the isci host object.
 * @idev: This parameter specifies the remote device to be freed.
 *
 */
static void isci_remote_device_deconstruct(struct isci_host *ihost, struct isci_remote_device *idev)
{
	dev_dbg(&ihost->pdev->dev,
		"%s: isci_device = %p\n", __func__, idev);

	/* There should not be any outstanding io's. All paths to
	 * here should go through isci_remote_device_nuke_requests.
	 * If we hit this condition, we will need a way to complete
	 * io requests in process */
	while (!list_empty(&idev->reqs_in_process)) {

		dev_err(&ihost->pdev->dev,
			"%s: ** request list not empty! **\n", __func__);
		BUG();
	}

	scic_remote_device_destruct(to_sci_dev(idev));
	idev->domain_dev->lldd_dev = NULL;
	idev->domain_dev = NULL;
	idev->isci_port = NULL;
	list_del_init(&idev->node);

	clear_bit(IDEV_START_PENDING, &idev->flags);
	clear_bit(IDEV_STOP_PENDING, &idev->flags);
	wake_up(&ihost->eventq);
}


/**
 * isci_remote_device_construct() - This function calls the scic remote device
 *    construct and start functions, it waits on the remote device start
 *    completion.
 * @port: This parameter specifies the isci port with the remote device.
 * @isci_device: This parameter specifies the isci remote device
 *
 * status from the scic calls, the caller to this function should clean up
 * resources as appropriate.
 */
static enum sci_status isci_remote_device_construct(
	struct isci_port *port,
	struct isci_remote_device *isci_device)
{
	enum sci_status status = SCI_SUCCESS;

	/* let the core do it's common constuction. */
	scic_remote_device_construct(port->sci_port_handle,
				     to_sci_dev(isci_device));

	/* let the core do it's device specific constuction. */
	if (isci_device->domain_dev->parent &&
	    (isci_device->domain_dev->parent->dev_type == EDGE_DEV)) {
		int i;

		/* struct smp_response_discover discover_response; */
		struct discover_resp discover_response;
		struct domain_device *parent =
			isci_device->domain_dev->parent;

		struct expander_device *parent_ex = &parent->ex_dev;

		for (i = 0; i < parent_ex->num_phys; i++) {

			struct ex_phy *phy = &parent_ex->ex_phy[i];

			if ((phy->phy_state == PHY_VACANT) ||
			    (phy->phy_state == PHY_NOT_PRESENT))
				continue;

			if (SAS_ADDR(phy->attached_sas_addr)
			    == SAS_ADDR(isci_device->domain_dev->sas_addr)) {

				discover_response.attached_dev_type
					= phy->attached_dev_type;
				discover_response.linkrate
					= phy->linkrate;
				discover_response.attached_sata_host
					= phy->attached_sata_host;
				discover_response.attached_sata_dev
					= phy->attached_sata_dev;
				discover_response.attached_sata_ps
					= phy->attached_sata_ps;
				discover_response.iproto
					= phy->attached_iproto >> 1;
				discover_response.tproto
					= phy->attached_tproto >> 1;
				memcpy(
					discover_response.attached_sas_addr,
					phy->attached_sas_addr,
					SAS_ADDR_SIZE
					);
				discover_response.attached_phy_id
					= phy->attached_phy_id;
				discover_response.change_count
					= phy->phy_change_count;
				discover_response.routing_attr
					= phy->routing_attr;
				discover_response.hmin_linkrate
					= phy->phy->minimum_linkrate_hw;
				discover_response.hmax_linkrate
					= phy->phy->maximum_linkrate_hw;
				discover_response.pmin_linkrate
					= phy->phy->minimum_linkrate;
				discover_response.pmax_linkrate
					= phy->phy->maximum_linkrate;
			}
		}


		dev_dbg(&port->isci_host->pdev->dev,
			"%s: parent->dev_type = EDGE_DEV\n",
			__func__);

		status = scic_remote_device_ea_construct(to_sci_dev(isci_device),
				(struct smp_response_discover *)&discover_response);

	} else
		status = scic_remote_device_da_construct(to_sci_dev(isci_device));


	if (status != SCI_SUCCESS) {
		dev_dbg(&port->isci_host->pdev->dev,
			"%s: scic_remote_device_da_construct failed - "
			"isci_device = %p\n",
			__func__,
			isci_device);

		return status;
	}

	sci_object_set_association(to_sci_dev(isci_device), isci_device);

	BUG_ON(port->isci_host == NULL);

	/* start the device. */
	status = scic_remote_device_start(to_sci_dev(isci_device),
					  ISCI_REMOTE_DEVICE_START_TIMEOUT);

	if (status != SCI_SUCCESS) {
		dev_warn(&port->isci_host->pdev->dev,
			 "%s: scic_remote_device_start failed\n",
			 __func__);
		return status;
	}

	return status;
}

void isci_remote_device_nuke_requests(struct isci_host *ihost, struct isci_remote_device *idev)
{
	DECLARE_COMPLETION_ONSTACK(aborted_task_completion);

	dev_dbg(&ihost->pdev->dev,
		"%s: idev = %p\n", __func__, idev);

	/* Cleanup all requests pending for this device. */
	isci_terminate_pending_requests(ihost, idev, terminating);

	dev_dbg(&ihost->pdev->dev,
		"%s: idev = %p, done\n", __func__, idev);
}

/**
 * This function builds the isci_remote_device when a libsas dev_found message
 *    is received.
 * @isci_host: This parameter specifies the isci host object.
 * @port: This parameter specifies the isci_port conected to this device.
 *
 * pointer to new isci_remote_device.
 */
static struct isci_remote_device *
isci_remote_device_alloc(struct isci_host *ihost, struct isci_port *iport)
{
	struct isci_remote_device *idev;
	int i;

	for (i = 0; i < SCI_MAX_REMOTE_DEVICES; i++) {
		idev = idev_by_id(ihost, i);
		if (!test_and_set_bit(IDEV_ALLOCATED, &idev->flags))
			break;
	}

	if (i >= SCI_MAX_REMOTE_DEVICES) {
		dev_warn(&ihost->pdev->dev, "%s: failed\n", __func__);
		return NULL;
	}

	BUG_ON(!list_empty(&idev->reqs_in_process));
	BUG_ON(!list_empty(&idev->node));
	isci_remote_device_change_state(idev, isci_freed);

	return idev;
}

/**
 * isci_remote_device_ready() - This function is called by the scic when the
 *    remote device is ready. We mark the isci device as ready and signal the
 *    waiting proccess.
 * @ihost: our valid isci_host
 * @idev: remote device
 *
 */
void isci_remote_device_ready(struct isci_host *ihost, struct isci_remote_device *idev)
{
	dev_dbg(&ihost->pdev->dev,
		"%s: idev = %p\n", __func__, idev);

	isci_remote_device_change_state(idev, isci_ready_for_io);
	if (test_and_clear_bit(IDEV_START_PENDING, &idev->flags))
		wake_up(&ihost->eventq);
}

/**
 * isci_remote_device_not_ready() - This function is called by the scic when
 *    the remote device is not ready. We mark the isci device as ready (not
 *    "ready_for_io") and signal the waiting proccess.
 * @isci_host: This parameter specifies the isci host object.
 * @isci_device: This parameter specifies the remote device
 *
 */
void isci_remote_device_not_ready(struct isci_host *ihost,
				  struct isci_remote_device *idev, u32 reason)
{
	dev_dbg(&ihost->pdev->dev,
		"%s: isci_device = %p\n", __func__, idev);

	if (reason == SCIC_REMOTE_DEVICE_NOT_READY_STOP_REQUESTED)
		isci_remote_device_change_state(idev, isci_stopping);
	else
		/* device ready is actually a "not ready for io" state. */
		isci_remote_device_change_state(idev, isci_ready);
}

/**
 * isci_remote_device_stop_complete() - This function is called by the scic
 *    when the remote device stop has completed. We mark the isci device as not
 *    ready and remove the isci remote device.
 * @isci_host: This parameter specifies the isci host object.
 * @isci_device: This parameter specifies the remote device.
 * @status: This parameter specifies status of the completion.
 *
 */
void isci_remote_device_stop_complete(
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device,
	enum sci_status status)
{
	dev_dbg(&isci_host->pdev->dev,
		"%s: complete isci_device = %p, status = 0x%x\n",
		__func__,
		isci_device,
		status);

	isci_remote_device_change_state(isci_device, isci_stopped);

	/* after stop, we can tear down resources. */
	isci_remote_device_deconstruct(isci_host, isci_device);

}

/**
 * isci_remote_device_start_complete() - This function is called by the scic
 *    when the remote device start has completed
 * @isci_host: This parameter specifies the isci host object.
 * @isci_device: This parameter specifies the remote device.
 * @status: This parameter specifies status of the completion.
 *
 */
void isci_remote_device_start_complete(
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device,
	enum sci_status status)
{


}


/**
 * isci_remote_device_stop() - This function is called internally to stop the
 *    remote device.
 * @isci_host: This parameter specifies the isci host object.
 * @isci_device: This parameter specifies the remote device.
 *
 * The status of the scic request to stop.
 */
enum sci_status isci_remote_device_stop(struct isci_host *ihost, struct isci_remote_device *idev)
{
	enum sci_status status;
	unsigned long flags;

	dev_dbg(&ihost->pdev->dev,
		"%s: isci_device = %p\n", __func__, idev);

	isci_remote_device_change_state(idev, isci_stopping);

	/* Kill all outstanding requests. */
	isci_remote_device_nuke_requests(ihost, idev);

	set_bit(IDEV_STOP_PENDING, &idev->flags);

	spin_lock_irqsave(&ihost->scic_lock, flags);
	status = scic_remote_device_stop(to_sci_dev(idev), 50);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	/* Wait for the stop complete callback. */
	if (status == SCI_SUCCESS) {
		wait_for_device_stop(ihost, idev);
		clear_bit(IDEV_ALLOCATED, &idev->flags);
	}

	dev_dbg(&ihost->pdev->dev,
		"%s: idev = %p - after completion wait\n",
		__func__, idev);

	return status;
}

/**
 * isci_remote_device_gone() - This function is called by libsas when a domain
 *    device is removed.
 * @domain_device: This parameter specifies the libsas domain device.
 *
 */
void isci_remote_device_gone(struct domain_device *dev)
{
	struct isci_host *ihost = dev_to_ihost(dev);
	struct isci_remote_device *idev = dev->lldd_dev;

	dev_dbg(&ihost->pdev->dev,
		"%s: domain_device = %p, isci_device = %p, isci_port = %p\n",
		__func__, dev, idev, idev->isci_port);

	isci_remote_device_stop(ihost, idev);
}


/**
 * isci_remote_device_found() - This function is called by libsas when a remote
 *    device is discovered. A remote device object is created and started. the
 *    function then sleeps until the sci core device started message is
 *    received.
 * @domain_device: This parameter specifies the libsas domain device.
 *
 * status, zero indicates success.
 */
int isci_remote_device_found(struct domain_device *domain_dev)
{
	struct isci_host *isci_host = dev_to_ihost(domain_dev);
	struct isci_port *isci_port;
	struct isci_phy *isci_phy;
	struct asd_sas_port *sas_port;
	struct asd_sas_phy *sas_phy;
	struct isci_remote_device *isci_device;
	enum sci_status status;

	dev_dbg(&isci_host->pdev->dev,
		"%s: domain_device = %p\n", __func__, domain_dev);

	wait_for_start(isci_host);

	sas_port = domain_dev->port;
	sas_phy = list_first_entry(&sas_port->phy_list, struct asd_sas_phy,
				   port_phy_el);
	isci_phy = to_isci_phy(sas_phy);
	isci_port = isci_phy->isci_port;

	/* we are being called for a device on this port,
	 * so it has to come up eventually
	 */
	wait_for_completion(&isci_port->start_complete);

	if ((isci_stopping == isci_port_get_state(isci_port)) ||
	    (isci_stopped == isci_port_get_state(isci_port)))
		return -ENODEV;

	isci_device = isci_remote_device_alloc(isci_host, isci_port);
	if (!isci_device)
		return -ENODEV;

	INIT_LIST_HEAD(&isci_device->node);
	domain_dev->lldd_dev = isci_device;
	isci_device->domain_dev = domain_dev;
	isci_device->isci_port = isci_port;
	isci_remote_device_change_state(isci_device, isci_starting);


	spin_lock_irq(&isci_host->scic_lock);
	list_add_tail(&isci_device->node, &isci_port->remote_dev_list);

	set_bit(IDEV_START_PENDING, &isci_device->flags);
	status = isci_remote_device_construct(isci_port, isci_device);
	spin_unlock_irq(&isci_host->scic_lock);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p\n",
		__func__, isci_device);

	if (status != SCI_SUCCESS) {

		spin_lock_irq(&isci_host->scic_lock);
		isci_remote_device_deconstruct(
			isci_host,
			isci_device
			);
		spin_unlock_irq(&isci_host->scic_lock);
		return -ENODEV;
	}

	/* wait for the device ready callback. */
	wait_for_device_start(isci_host, isci_device);

	return 0;
}
/**
 * isci_device_is_reset_pending() - This function will check if there is any
 *    pending reset condition on the device.
 * @request: This parameter is the isci_device object.
 *
 * true if there is a reset pending for the device.
 */
bool isci_device_is_reset_pending(
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device)
{
	struct isci_request *isci_request;
	struct isci_request *tmp_req;
	bool reset_is_pending = false;
	unsigned long flags;

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p\n", __func__, isci_device);

	spin_lock_irqsave(&isci_host->scic_lock, flags);

	/* Check for reset on all pending requests. */
	list_for_each_entry_safe(isci_request, tmp_req,
				 &isci_device->reqs_in_process, dev_node) {
		dev_dbg(&isci_host->pdev->dev,
			"%s: isci_device = %p request = %p\n",
			__func__, isci_device, isci_request);

		if (isci_request->ttype == io_task) {

			unsigned long flags;
			struct sas_task *task = isci_request_access_task(
				isci_request);

			spin_lock_irqsave(&task->task_state_lock, flags);
			if (task->task_state_flags & SAS_TASK_NEED_DEV_RESET)
				reset_is_pending = true;
			spin_unlock_irqrestore(&task->task_state_lock, flags);
		}
	}

	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p reset_is_pending = %d\n",
		__func__, isci_device, reset_is_pending);

	return reset_is_pending;
}

/**
 * isci_device_clear_reset_pending() - This function will clear if any pending
 *    reset condition flags on the device.
 * @request: This parameter is the isci_device object.
 *
 * true if there is a reset pending for the device.
 */
void isci_device_clear_reset_pending(struct isci_host *ihost, struct isci_remote_device *idev)
{
	struct isci_request *isci_request;
	struct isci_request *tmp_req;
	unsigned long flags = 0;

	dev_dbg(&ihost->pdev->dev, "%s: idev=%p, ihost=%p\n",
		 __func__, idev, ihost);

	spin_lock_irqsave(&ihost->scic_lock, flags);

	/* Clear reset pending on all pending requests. */
	list_for_each_entry_safe(isci_request, tmp_req,
				 &idev->reqs_in_process, dev_node) {
		dev_dbg(&ihost->pdev->dev, "%s: idev = %p request = %p\n",
			 __func__, idev, isci_request);

		if (isci_request->ttype == io_task) {

			unsigned long flags2;
			struct sas_task *task = isci_request_access_task(
				isci_request);

			spin_lock_irqsave(&task->task_state_lock, flags2);
			task->task_state_flags &= ~SAS_TASK_NEED_DEV_RESET;
			spin_unlock_irqrestore(&task->task_state_lock, flags2);
		}
	}
	spin_unlock_irqrestore(&ihost->scic_lock, flags);
}

/**
 * isci_remote_device_change_state() - This function gets the status of the
 *    remote_device object.
 * @isci_device: This parameter points to the isci_remote_device object
 *
 * status of the object as a isci_status enum.
 */
void isci_remote_device_change_state(
	struct isci_remote_device *isci_device,
	enum isci_status status)
{
	unsigned long flags;

	spin_lock_irqsave(&isci_device->state_lock, flags);
	isci_device->status = status;
	spin_unlock_irqrestore(&isci_device->state_lock, flags);
}
