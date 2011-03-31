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

#if !defined(_ISCI_REQUEST_H_)
#define _ISCI_REQUEST_H_

#include "isci.h"

/**
 * struct isci_request_status - This enum defines the possible states of an I/O
 *    request.
 *
 *
 */
enum isci_request_status {
	unallocated = 0x00,
	allocated   = 0x01,
	started     = 0x02,
	completed   = 0x03,
	aborting    = 0x04,
	aborted     = 0x05,
	terminating = 0x06,
	dead        = 0x07
};

enum task_type {
	io_task  = 0,
	tmf_task = 1
};

/**
 * struct isci_request - This class represents the request object used to track
 *    IO, smp and TMF request internal. It wraps the SCIC request object.
 *
 *
 */
struct isci_request {

	struct scic_sds_request *sci_request_handle;

	enum isci_request_status status;
	enum task_type ttype;
	unsigned short io_tag;
	bool complete_in_target;

	union ttype_ptr_union {
		struct sas_task *io_task_ptr;   /* When ttype==io_task  */
		struct isci_tmf *tmf_task_ptr;  /* When ttype==tmf_task */
	} ttype_ptr;
	struct isci_host *isci_host;
	struct isci_remote_device *isci_device;
	/* For use in the requests_to_{complete|abort} lists: */
	struct list_head completed_node;
	/* For use in the reqs_in_process list: */
	struct list_head dev_node;
	void *sci_request_mem_ptr;
	spinlock_t state_lock;
	dma_addr_t request_daddr;
	dma_addr_t zero_scatter_daddr;

	unsigned int num_sg_entries;                    /* returned by pci_alloc_sg */
	unsigned int request_alloc_size;                /* size of block from dma_pool_alloc */

	/** Note: "io_request_completion" is completed in two different ways
	 * depending on whether this is a TMF or regular request.
	 * - TMF requests are completed in the thread that started them;
	 * - regular requests are completed in the request completion callback
	 *   function.
	 * This difference in operation allows the aborter of a TMF request
	 * to be sure that once the TMF request completes, the I/O that the
	 * TMF was aborting is guaranteed to have completed.
	 */
	struct completion *io_request_completion;
};

/**
 * This function gets the status of the request object.
 * @request: This parameter points to the isci_request object
 *
 * status of the object as a isci_request_status enum.
 */
static inline
enum isci_request_status isci_request_get_state(
	struct isci_request *isci_request)
{
	BUG_ON(isci_request == NULL);

	/*probably a bad sign...	*/
	if (isci_request->status == unallocated)
		dev_warn(&isci_request->isci_host->pdev->dev,
			 "%s: isci_request->status == unallocated\n",
			 __func__);

	return isci_request->status;
}


/**
 * isci_request_change_state() - This function sets the status of the request
 *    object.
 * @request: This parameter points to the isci_request object
 * @status: This Parameter is the new status of the object
 *
 */
static inline enum isci_request_status isci_request_change_state(
	struct isci_request *isci_request,
	enum isci_request_status status)
{
	enum isci_request_status old_state;
	unsigned long flags;

	dev_dbg(&isci_request->isci_host->pdev->dev,
		"%s: isci_request = %p, state = 0x%x\n",
		__func__,
		isci_request,
		status);

	BUG_ON(isci_request == NULL);

	spin_lock_irqsave(&isci_request->state_lock, flags);
	old_state = isci_request->status;
	isci_request->status = status;
	spin_unlock_irqrestore(&isci_request->state_lock, flags);

	return old_state;
}

/**
 * isci_request_change_started_to_newstate() - This function sets the status of
 *    the request object.
 * @request: This parameter points to the isci_request object
 * @status: This Parameter is the new status of the object
 *
 * state previous to any change.
 */
static inline enum isci_request_status isci_request_change_started_to_newstate(
	struct isci_request *isci_request,
	struct completion *completion_ptr,
	enum isci_request_status newstate)
{
	enum isci_request_status old_state;
	unsigned long flags;

	BUG_ON(isci_request == NULL);

	spin_lock_irqsave(&isci_request->state_lock, flags);

	old_state = isci_request->status;

	if (old_state == started || old_state == aborting) {
		BUG_ON(isci_request->io_request_completion != NULL);

		isci_request->io_request_completion = completion_ptr;
		isci_request->status = newstate;
	}
	spin_unlock_irqrestore(&isci_request->state_lock, flags);

	dev_dbg(&isci_request->isci_host->pdev->dev,
		"%s: isci_request = %p, old_state = 0x%x\n",
		__func__,
		isci_request,
		old_state);

	return old_state;
}

/**
 * isci_request_change_started_to_aborted() - This function sets the status of
 *    the request object.
 * @request: This parameter points to the isci_request object
 * @completion_ptr: This parameter is saved as the kernel completion structure
 *    signalled when the old request completes.
 *
 * state previous to any change.
 */
static inline enum isci_request_status isci_request_change_started_to_aborted(
	struct isci_request *isci_request,
	struct completion *completion_ptr)
{
	return isci_request_change_started_to_newstate(
		       isci_request, completion_ptr, aborted
		       );
}
/**
 * isci_request_free() - This function frees the request object.
 * @isci_host: This parameter specifies the ISCI host object
 * @isci_request: This parameter points to the isci_request object
 *
 */
static inline void isci_request_free(
	struct isci_host *isci_host,
	struct isci_request *isci_request)
{
	BUG_ON(isci_request == NULL);

	/* release the dma memory if we fail. */
	dma_pool_free(isci_host->dma_pool, isci_request,
		      isci_request->request_daddr);
}


/* #define ISCI_REQUEST_VALIDATE_ACCESS
 */

#ifdef ISCI_REQUEST_VALIDATE_ACCESS

static inline
struct sas_task *isci_request_access_task(struct isci_request *isci_request)
{
	BUG_ON(isci_request->ttype != io_task);
	return isci_request->ttype_ptr.io_task_ptr;
}

static inline
struct isci_tmf *isci_request_access_tmf(struct isci_request *isci_request)
{
	BUG_ON(isci_request->ttype != tmf_task);
	return isci_request->ttype_ptr.tmf_task_ptr;
}

#else  /* not ISCI_REQUEST_VALIDATE_ACCESS */

#define isci_request_access_task(RequestPtr) \
	((RequestPtr)->ttype_ptr.io_task_ptr)

#define isci_request_access_tmf(RequestPtr)  \
	((RequestPtr)->ttype_ptr.tmf_task_ptr)

#endif /* not ISCI_REQUEST_VALIDATE_ACCESS */


int isci_request_alloc_tmf(
	struct isci_host *isci_host,
	struct isci_tmf *isci_tmf,
	struct isci_request **isci_request,
	struct isci_remote_device *isci_device,
	gfp_t gfp_flags);


int isci_request_execute(
	struct isci_host *isci_host,
	struct sas_task *task,
	struct isci_request **request,
	gfp_t gfp_flags);

/**
 * isci_request_unmap_sgl() - This function unmaps the DMA address of a given
 *    sgl
 * @request: This parameter points to the isci_request object
 * @*pdev: This Parameter is the pci_device struct for the controller
 *
 */
static inline void isci_request_unmap_sgl(
	struct isci_request *request,
	struct pci_dev *pdev)
{
	struct sas_task *task = isci_request_access_task(request);

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: request = %p, task = %p,\n"
		"task->data_dir = %d, is_sata = %d\n ",
		__func__,
		request,
		task,
		task->data_dir,
		sas_protocol_ata(task->task_proto));

	if ((task->data_dir != PCI_DMA_NONE) &&
	    !sas_protocol_ata(task->task_proto)) {
		if (task->num_scatter == 0)
			/* 0 indicates a single dma address */
			dma_unmap_single(
				&pdev->dev,
				request->zero_scatter_daddr,
				task->total_xfer_len,
				task->data_dir
				);

		else  /* unmap the sgl dma addresses */
			dma_unmap_sg(
				&pdev->dev,
				task->scatter,
				request->num_sg_entries,
				task->data_dir
				);
	}
}


void isci_request_io_request_complete(
	struct isci_host *isci_host,
	struct isci_request *request,
	enum sci_io_status completion_status);

u32 isci_request_io_request_get_transfer_length(
	struct isci_request *request);

enum dma_data_direction isci_request_io_request_get_data_direction(struct isci_request *req);

/**
 * isci_request_io_request_get_next_sge() - This function is called by the sci
 *    core to retrieve the next sge for a given request.
 * @request: This parameter is the isci_request object.
 * @current_sge_address: This parameter is the last sge retrieved by the sci
 *    core for this request.
 *
 * pointer to the next sge for specified request.
 */
static inline void *isci_request_io_request_get_next_sge(
	struct isci_request *request,
	void *current_sge_address)
{
	struct sas_task *task = isci_request_access_task(request);
	void *ret = NULL;

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: request = %p, "
		"current_sge_address = %p, "
		"num_scatter = %d\n",
		__func__,
		request,
		current_sge_address,
		task->num_scatter);

	if (!current_sge_address)	/* First time through.. */
		ret = task->scatter;    /* always task->scatter */
	else if (task->num_scatter == 0) /* Next element, if num_scatter == 0 */
		ret = NULL;              /* there is only one element. */
	else
		ret = sg_next(current_sge_address);     /* sg_next returns NULL
							 * for the last element
							 */

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: next sge address = %p\n",
		__func__,
		ret);

	return ret;
}



void *isci_request_ssp_io_request_get_cdb_address(
	struct isci_request *request);

u32 isci_request_ssp_io_request_get_cdb_length(
	struct isci_request *request);

u32  isci_request_ssp_io_request_get_lun(
	struct isci_request *request);

u32 isci_request_ssp_io_request_get_task_attribute(
	struct isci_request *request);

u32 isci_request_ssp_io_request_get_command_priority(
	struct isci_request *request);





void isci_terminate_pending_requests(
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device,
	enum isci_request_status new_request_state);




#endif /* !defined(_ISCI_REQUEST_H_) */
