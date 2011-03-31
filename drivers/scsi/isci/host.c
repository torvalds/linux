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
#include "scic_port.h"

#include "port.h"
#include "request.h"
#include "host.h"
#include "probe_roms.h"

irqreturn_t isci_msix_isr(int vec, void *data)
{
	struct isci_host *ihost = data;
	struct scic_sds_controller *scic = ihost->core_controller;

	if (scic_sds_controller_isr(scic))
		tasklet_schedule(&ihost->completion_tasklet);

	return IRQ_HANDLED;
}

irqreturn_t isci_intx_isr(int vec, void *data)
{
	struct pci_dev *pdev = data;
	struct isci_host *ihost;
	irqreturn_t ret = IRQ_NONE;
	int i;

	for_each_isci_host(i, ihost, pdev) {
		struct scic_sds_controller *scic = ihost->core_controller;

		if (scic_sds_controller_isr(scic)) {
			tasklet_schedule(&ihost->completion_tasklet);
			ret = IRQ_HANDLED;
		} else if (scic_sds_controller_error_isr(scic)) {
			spin_lock(&ihost->scic_lock);
			scic_sds_controller_error_handler(scic);
			spin_unlock(&ihost->scic_lock);
			ret = IRQ_HANDLED;
		}
	}

	return ret;
}

irqreturn_t isci_error_isr(int vec, void *data)
{
	struct isci_host *ihost = data;
	struct scic_sds_controller *scic = ihost->core_controller;

	if (scic_sds_controller_error_isr(scic))
		scic_sds_controller_error_handler(scic);

	return IRQ_HANDLED;
}

/**
 * isci_host_start_complete() - This function is called by the core library,
 *    through the ISCI Module, to indicate controller start status.
 * @isci_host: This parameter specifies the ISCI host object
 * @completion_status: This parameter specifies the completion status from the
 *    core library.
 *
 */
void isci_host_start_complete(struct isci_host *ihost, enum sci_status completion_status)
{
	if (completion_status != SCI_SUCCESS)
		dev_info(&ihost->pdev->dev,
			"controller start timed out, continuing...\n");
	isci_host_change_state(ihost, isci_ready);
	clear_bit(IHOST_START_PENDING, &ihost->flags);
	wake_up(&ihost->eventq);
}

int isci_host_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	struct isci_host *ihost = SHOST_TO_SAS_HA(shost)->lldd_ha;

	if (test_bit(IHOST_START_PENDING, &ihost->flags))
		return 0;

	/* todo: use sas_flush_discovery once it is upstream */
	scsi_flush_work(shost);

	scsi_flush_work(shost);

	dev_dbg(&ihost->pdev->dev,
		"%s: ihost->status = %d, time = %ld\n",
		 __func__, isci_host_get_state(ihost), time);

	return 1;

}

void isci_host_scan_start(struct Scsi_Host *shost)
{
	struct isci_host *ihost = SHOST_TO_SAS_HA(shost)->lldd_ha;
	struct scic_sds_controller *scic = ihost->core_controller;
	unsigned long tmo = scic_controller_get_suggested_start_timeout(scic);

	set_bit(IHOST_START_PENDING, &ihost->flags);

	spin_lock_irq(&ihost->scic_lock);
	scic_controller_start(scic, tmo);
	scic_controller_enable_interrupts(scic);
	spin_unlock_irq(&ihost->scic_lock);
}

void isci_host_stop_complete(struct isci_host *ihost, enum sci_status completion_status)
{
	isci_host_change_state(ihost, isci_stopped);
	scic_controller_disable_interrupts(ihost->core_controller);
	clear_bit(IHOST_STOP_PENDING, &ihost->flags);
	wake_up(&ihost->eventq);
}

static struct coherent_memory_info *isci_host_alloc_mdl_struct(
	struct isci_host *isci_host,
	u32 size)
{
	struct coherent_memory_info *mdl_struct;
	void *uncached_address = NULL;


	mdl_struct = devm_kzalloc(&isci_host->pdev->dev,
				  sizeof(*mdl_struct),
				  GFP_KERNEL);
	if (!mdl_struct)
		return NULL;

	INIT_LIST_HEAD(&mdl_struct->node);

	uncached_address = dmam_alloc_coherent(&isci_host->pdev->dev,
					       size,
					       &mdl_struct->dma_handle,
					       GFP_KERNEL);
	if (!uncached_address)
		return NULL;

	/* memset the whole memory area. */
	memset((char *)uncached_address, 0, size);
	mdl_struct->vaddr = uncached_address;
	mdl_struct->size = (size_t)size;

	return mdl_struct;
}

static void isci_host_build_mde(
	struct sci_physical_memory_descriptor *mde_struct,
	struct coherent_memory_info *mdl_struct)
{
	unsigned long address = 0;
	dma_addr_t dma_addr = 0;

	address = (unsigned long)mdl_struct->vaddr;
	dma_addr = mdl_struct->dma_handle;

	/* to satisfy the alignment. */
	if ((address % mde_struct->constant_memory_alignment) != 0) {
		int align_offset
			= (mde_struct->constant_memory_alignment
			   - (address % mde_struct->constant_memory_alignment));
		address += align_offset;
		dma_addr += align_offset;
	}

	mde_struct->virtual_address = (void *)address;
	mde_struct->physical_address = dma_addr;
	mdl_struct->mde = mde_struct;
}

static int isci_host_mdl_allocate_coherent(
	struct isci_host *isci_host)
{
	struct sci_physical_memory_descriptor *current_mde;
	struct coherent_memory_info *mdl_struct;
	u32 size = 0;

	struct sci_base_memory_descriptor_list *mdl_handle
		= sci_controller_get_memory_descriptor_list_handle(
		isci_host->core_controller);

	sci_mdl_first_entry(mdl_handle);

	current_mde = sci_mdl_get_current_entry(mdl_handle);

	while (current_mde != NULL) {

		size = (current_mde->constant_memory_size
			+ current_mde->constant_memory_alignment);

		mdl_struct = isci_host_alloc_mdl_struct(isci_host, size);
		if (!mdl_struct)
			return -ENOMEM;

		list_add_tail(&mdl_struct->node, &isci_host->mdl_struct_list);

		isci_host_build_mde(current_mde, mdl_struct);

		sci_mdl_next_entry(mdl_handle);
		current_mde = sci_mdl_get_current_entry(mdl_handle);
	}

	return 0;
}


/**
 * isci_host_completion_routine() - This function is the delayed service
 *    routine that calls the sci core library's completion handler. It's
 *    scheduled as a tasklet from the interrupt service routine when interrupts
 *    in use, or set as the timeout function in polled mode.
 * @data: This parameter specifies the ISCI host object
 *
 */
static void isci_host_completion_routine(unsigned long data)
{
	struct isci_host *isci_host = (struct isci_host *)data;
	struct list_head    completed_request_list;
	struct list_head    errored_request_list;
	struct list_head    *current_position;
	struct list_head    *next_position;
	struct isci_request *request;
	struct isci_request *next_request;
	struct sas_task     *task;

	INIT_LIST_HEAD(&completed_request_list);
	INIT_LIST_HEAD(&errored_request_list);

	spin_lock_irq(&isci_host->scic_lock);

	scic_sds_controller_completion_handler(isci_host->core_controller);

	/* Take the lists of completed I/Os from the host. */

	list_splice_init(&isci_host->requests_to_complete,
			 &completed_request_list);

	/* Take the list of errored I/Os from the host. */
	list_splice_init(&isci_host->requests_to_errorback,
			 &errored_request_list);

	spin_unlock_irq(&isci_host->scic_lock);

	/* Process any completions in the lists. */
	list_for_each_safe(current_position, next_position,
			   &completed_request_list) {

		request = list_entry(current_position, struct isci_request,
				     completed_node);
		task = isci_request_access_task(request);

		/* Normal notification (task_done) */
		dev_dbg(&isci_host->pdev->dev,
			"%s: Normal - request/task = %p/%p\n",
			__func__,
			request,
			task);

		/* Return the task to libsas */
		if (task != NULL) {

			task->lldd_task = NULL;
			if (!(task->task_state_flags & SAS_TASK_STATE_ABORTED)) {

				/* If the task is already in the abort path,
				* the task_done callback cannot be called.
				*/
				task->task_done(task);
			}
		}
		/* Free the request object. */
		isci_request_free(isci_host, request);
	}
	list_for_each_entry_safe(request, next_request, &errored_request_list,
				 completed_node) {

		task = isci_request_access_task(request);

		/* Use sas_task_abort */
		dev_warn(&isci_host->pdev->dev,
			 "%s: Error - request/task = %p/%p\n",
			 __func__,
			 request,
			 task);

		if (task != NULL) {

			/* Put the task into the abort path if it's not there
			 * already.
			 */
			if (!(task->task_state_flags & SAS_TASK_STATE_ABORTED))
				sas_task_abort(task);

		} else {
			/* This is a case where the request has completed with a
			 * status such that it needed further target servicing,
			 * but the sas_task reference has already been removed
			 * from the request.  Since it was errored, it was not
			 * being aborted, so there is nothing to do except free
			 * it.
			 */

			spin_lock_irq(&isci_host->scic_lock);
			/* Remove the request from the remote device's list
			* of pending requests.
			*/
			list_del_init(&request->dev_node);
			spin_unlock_irq(&isci_host->scic_lock);

			/* Free the request object. */
			isci_request_free(isci_host, request);
		}
	}

}

void isci_host_deinit(struct isci_host *ihost)
{
	struct scic_sds_controller *scic = ihost->core_controller;
	int i;

	isci_host_change_state(ihost, isci_stopping);
	for (i = 0; i < SCI_MAX_PORTS; i++) {
		struct isci_port *port = &ihost->isci_ports[i];
		struct isci_remote_device *idev, *d;

		list_for_each_entry_safe(idev, d, &port->remote_dev_list, node) {
			isci_remote_device_change_state(idev, isci_stopping);
			isci_remote_device_stop(ihost, idev);
		}
	}

	set_bit(IHOST_STOP_PENDING, &ihost->flags);

	spin_lock_irq(&ihost->scic_lock);
	scic_controller_stop(scic, SCIC_CONTROLLER_STOP_TIMEOUT);
	spin_unlock_irq(&ihost->scic_lock);

	wait_for_stop(ihost);
	scic_controller_reset(scic);
	isci_timer_list_destroy(ihost);
}

static void __iomem *scu_base(struct isci_host *isci_host)
{
	struct pci_dev *pdev = isci_host->pdev;
	int id = isci_host->id;

	return pcim_iomap_table(pdev)[SCI_SCU_BAR * 2] + SCI_SCU_BAR_SIZE * id;
}

static void __iomem *smu_base(struct isci_host *isci_host)
{
	struct pci_dev *pdev = isci_host->pdev;
	int id = isci_host->id;

	return pcim_iomap_table(pdev)[SCI_SMU_BAR * 2] + SCI_SMU_BAR_SIZE * id;
}

static void isci_user_parameters_get(
		struct isci_host *isci_host,
		union scic_user_parameters *scic_user_params)
{
	struct scic_sds_user_parameters *u = &scic_user_params->sds1;
	int i;

	for (i = 0; i < SCI_MAX_PHYS; i++) {
		struct sci_phy_user_params *u_phy = &u->phys[i];

		u_phy->max_speed_generation = phy_gen;

		/* we are not exporting these for now */
		u_phy->align_insertion_frequency = 0x7f;
		u_phy->in_connection_align_insertion_frequency = 0xff;
		u_phy->notify_enable_spin_up_insertion_frequency = 0x33;
	}

	u->stp_inactivity_timeout = stp_inactive_to;
	u->ssp_inactivity_timeout = ssp_inactive_to;
	u->stp_max_occupancy_timeout = stp_max_occ_to;
	u->ssp_max_occupancy_timeout = ssp_max_occ_to;
	u->no_outbound_task_timeout = no_outbound_task_to;
	u->max_number_concurrent_device_spin_up = max_concurr_spinup;
}

int isci_host_init(struct isci_host *isci_host)
{
	int err = 0, i;
	enum sci_status status;
	struct scic_sds_controller *controller;
	union scic_oem_parameters oem;
	union scic_user_parameters scic_user_params;
	struct isci_pci_info *pci_info = to_pci_info(isci_host->pdev);

	isci_timer_list_construct(isci_host);

	controller = scic_controller_alloc(&isci_host->pdev->dev);

	if (!controller) {
		dev_err(&isci_host->pdev->dev,
			"%s: failed (%d)\n",
			__func__,
			err);
		return -ENOMEM;
	}

	isci_host->core_controller = controller;
	sci_object_set_association(isci_host->core_controller, isci_host);
	spin_lock_init(&isci_host->state_lock);
	spin_lock_init(&isci_host->scic_lock);
	spin_lock_init(&isci_host->queue_lock);
	init_waitqueue_head(&isci_host->eventq);

	isci_host_change_state(isci_host, isci_starting);
	isci_host->can_queue = ISCI_CAN_QUEUE_VAL;

	status = scic_controller_construct(controller, scu_base(isci_host),
					   smu_base(isci_host));

	if (status != SCI_SUCCESS) {
		dev_err(&isci_host->pdev->dev,
			"%s: scic_controller_construct failed - status = %x\n",
			__func__,
			status);
		return -ENODEV;
	}

	isci_host->sas_ha.dev = &isci_host->pdev->dev;
	isci_host->sas_ha.lldd_ha = isci_host;

	/*
	 * grab initial values stored in the controller object for OEM and USER
	 * parameters
	 */
	isci_user_parameters_get(isci_host, &scic_user_params);
	status = scic_user_parameters_set(isci_host->core_controller,
					  &scic_user_params);
	if (status != SCI_SUCCESS) {
		dev_warn(&isci_host->pdev->dev,
			 "%s: scic_user_parameters_set failed\n",
			 __func__);
		return -ENODEV;
	}

	scic_oem_parameters_get(controller, &oem);

	/* grab any OEM parameters specified in orom */
	if (pci_info->orom) {
		status = isci_parse_oem_parameters(&oem,
						   pci_info->orom,
						   isci_host->id);
		if (status != SCI_SUCCESS) {
			dev_warn(&isci_host->pdev->dev,
				 "parsing firmware oem parameters failed\n");
			return -EINVAL;
		}
	}

	status = scic_oem_parameters_set(isci_host->core_controller, &oem);
	if (status != SCI_SUCCESS) {
		dev_warn(&isci_host->pdev->dev,
				"%s: scic_oem_parameters_set failed\n",
				__func__);
		return -ENODEV;
	}

	tasklet_init(&isci_host->completion_tasklet,
		     isci_host_completion_routine, (unsigned long)isci_host);

	INIT_LIST_HEAD(&(isci_host->mdl_struct_list));

	INIT_LIST_HEAD(&isci_host->requests_to_complete);
	INIT_LIST_HEAD(&isci_host->requests_to_errorback);

	spin_lock_irq(&isci_host->scic_lock);
	status = scic_controller_initialize(isci_host->core_controller);
	spin_unlock_irq(&isci_host->scic_lock);
	if (status != SCI_SUCCESS) {
		dev_warn(&isci_host->pdev->dev,
			 "%s: scic_controller_initialize failed -"
			 " status = 0x%x\n",
			 __func__, status);
		return -ENODEV;
	}

	/* populate mdl with dma memory. scu_mdl_allocate_coherent() */
	err = isci_host_mdl_allocate_coherent(isci_host);
	if (err)
		return err;

	/*
	 * keep the pool alloc size around, will use it for a bounds checking
	 * when trying to convert virtual addresses to physical addresses
	 */
	isci_host->dma_pool_alloc_size = sizeof(struct isci_request) +
					 scic_io_request_get_object_size();
	isci_host->dma_pool = dmam_pool_create(DRV_NAME, &isci_host->pdev->dev,
					       isci_host->dma_pool_alloc_size,
					       SLAB_HWCACHE_ALIGN, 0);

	if (!isci_host->dma_pool)
		return -ENOMEM;

	for (i = 0; i < SCI_MAX_PORTS; i++)
		isci_port_init(&isci_host->isci_ports[i], isci_host, i);

	for (i = 0; i < SCI_MAX_PHYS; i++)
		isci_phy_init(&isci_host->phys[i], isci_host, i);

	for (i = 0; i < SCI_MAX_REMOTE_DEVICES; i++) {
		struct isci_remote_device *idev = idev_by_id(isci_host, i);

		INIT_LIST_HEAD(&idev->reqs_in_process);
		INIT_LIST_HEAD(&idev->node);
		spin_lock_init(&idev->state_lock);
	}

	return 0;
}
