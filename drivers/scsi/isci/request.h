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

#ifndef _ISCI_REQUEST_H_
#define _ISCI_REQUEST_H_

#include "isci.h"
#include "host.h"
#include "scu_task_context.h"

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

enum sci_request_protocol {
	SCIC_NO_PROTOCOL,
	SCIC_SMP_PROTOCOL,
	SCIC_SSP_PROTOCOL,
	SCIC_STP_PROTOCOL
}; /* XXX remove me, use sas_task.{dev|task_proto} instead */;

struct scic_sds_stp_request {
	union {
		u32 ncq;

		u32 udma;

		struct scic_sds_stp_pio_request {
			/*
			 * Total transfer for the entire PIO request recorded
			 * at request constuction time.
			 *
			 * @todo Should we just decrement this value for each
			 * byte of data transitted or received to elemenate
			 * the current_transfer_bytes field?
			 */
			u32 total_transfer_bytes;

			/*
			 * Total number of bytes received/transmitted in data
			 * frames since the start of the IO request.  At the
			 * end of the IO request this should equal the
			 * total_transfer_bytes.
			 */
			u32 current_transfer_bytes;

			/*
			 * The number of bytes requested in the in the PIO
			 * setup.
			 */
			u32 pio_transfer_bytes;

			/*
			 * PIO Setup ending status value to tell us if we need
			 * to wait for another FIS or if the transfer is
			 * complete. On the receipt of a D2H FIS this will be
			 * the status field of that FIS.
			 */
			u8 ending_status;

			/*
			 * On receipt of a D2H FIS this will be the ending
			 * error field if the ending_status has the
			 * SATA_STATUS_ERR bit set.
			 */
			u8 ending_error;

			struct scic_sds_request_pio_sgl {
				struct scu_sgl_element_pair *sgl_pair;
				u8 sgl_set;
				u32 sgl_offset;
			} request_current;
		} pio;

		struct {
			/*
			 * The number of bytes requested in the PIO setup
			 * before CDB data frame.
			 */
			u32 device_preferred_cdb_length;
		} packet;
	} type;
};

struct scic_sds_request {
	/*
	 * This field contains the information for the base request state
	 * machine.
	 */
	struct sci_base_state_machine sm;

	/*
	 * This field simply points to the controller to which this IO request
	 * is associated.
	 */
	struct scic_sds_controller *owning_controller;

	/*
	 * This field simply points to the remote device to which this IO
	 * request is associated.
	 */
	struct scic_sds_remote_device *target_device;

	/*
	 * This field is utilized to determine if the SCI user is managing
	 * the IO tag for this request or if the core is managing it.
	 */
	bool was_tag_assigned_by_user;

	/*
	 * This field indicates the IO tag for this request.  The IO tag is
	 * comprised of the task_index and a sequence count. The sequence count
	 * is utilized to help identify tasks from one life to another.
	 */
	u16 io_tag;

	/*
	 * This field specifies the protocol being utilized for this
	 * IO request.
	 */
	enum sci_request_protocol protocol;

	/*
	 * This field indicates the completion status taken from the SCUs
	 * completion code.  It indicates the completion result for the SCU
	 * hardware.
	 */
	u32 scu_status;

	/*
	 * This field indicates the completion status returned to the SCI user.
	 * It indicates the users view of the io request completion.
	 */
	u32 sci_status;

	/*
	 * This field contains the value to be utilized when posting
	 * (e.g. Post_TC, * Post_TC_Abort) this request to the silicon.
	 */
	u32 post_context;

	struct scu_task_context *task_context_buffer;
	struct scu_task_context tc ____cacheline_aligned;

	/* could be larger with sg chaining */
	#define SCU_SGL_SIZE ((SCI_MAX_SCATTER_GATHER_ELEMENTS + 1) / 2)
	struct scu_sgl_element_pair sg_table[SCU_SGL_SIZE] __attribute__ ((aligned(32)));

	/*
	 * This field indicates if this request is a task management request or
	 * normal IO request.
	 */
	bool is_task_management_request;

	/*
	 * This field is a pointer to the stored rx frame data.  It is used in
	 * STP internal requests and SMP response frames.  If this field is
	 * non-NULL the saved frame must be released on IO request completion.
	 *
	 * @todo In the future do we want to keep a list of RX frame buffers?
	 */
	u32 saved_rx_frame_index;

	union {
		struct {
			union {
				struct ssp_cmd_iu cmd;
				struct ssp_task_iu tmf;
			};
			union {
				struct ssp_response_iu rsp;
				u8 rsp_buf[SSP_RESP_IU_MAX_SIZE];
			};
		} ssp;

		struct {
			struct smp_req cmd;
			struct smp_resp rsp;
		} smp;

		struct {
			struct scic_sds_stp_request req;
			struct host_to_dev_fis cmd;
			struct dev_to_host_fis rsp;
		} stp;
	};
};

static inline struct scic_sds_request *to_sci_req(struct scic_sds_stp_request *stp_req)
{
	struct scic_sds_request *sci_req;

	sci_req = container_of(stp_req, typeof(*sci_req), stp.req);
	return sci_req;
}

struct isci_request {
	enum isci_request_status status;
	enum task_type ttype;
	unsigned short io_tag;
	bool complete_in_target;
	bool terminated;

	union ttype_ptr_union {
		struct sas_task *io_task_ptr;   /* When ttype==io_task  */
		struct isci_tmf *tmf_task_ptr;  /* When ttype==tmf_task */
	} ttype_ptr;
	struct isci_host *isci_host;
	/* For use in the requests_to_{complete|abort} lists: */
	struct list_head completed_node;
	/* For use in the reqs_in_process list: */
	struct list_head dev_node;
	spinlock_t state_lock;
	dma_addr_t request_daddr;
	dma_addr_t zero_scatter_daddr;

	unsigned int num_sg_entries;			/* returned by pci_alloc_sg */

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
	struct scic_sds_request sci;
};

static inline struct isci_request *sci_req_to_ireq(struct scic_sds_request *sci_req)
{
	struct isci_request *ireq = container_of(sci_req, typeof(*ireq), sci);

	return ireq;
}

/**
 * enum sci_base_request_states - This enumeration depicts all the states for
 *    the common request state machine.
 *
 *
 */
enum sci_base_request_states {
	/*
	 * Simply the initial state for the base request state machine.
	 */
	SCI_REQ_INIT,

	/*
	 * This state indicates that the request has been constructed.
	 * This state is entered from the INITIAL state.
	 */
	SCI_REQ_CONSTRUCTED,

	/*
	 * This state indicates that the request has been started. This state
	 * is entered from the CONSTRUCTED state.
	 */
	SCI_REQ_STARTED,

	SCI_REQ_STP_UDMA_WAIT_TC_COMP,
	SCI_REQ_STP_UDMA_WAIT_D2H,

	SCI_REQ_STP_NON_DATA_WAIT_H2D,
	SCI_REQ_STP_NON_DATA_WAIT_D2H,

	SCI_REQ_STP_SOFT_RESET_WAIT_H2D_ASSERTED,
	SCI_REQ_STP_SOFT_RESET_WAIT_H2D_DIAG,
	SCI_REQ_STP_SOFT_RESET_WAIT_D2H,

	/*
	 * While in this state the IO request object is waiting for the TC
	 * completion notification for the H2D Register FIS
	 */
	SCI_REQ_STP_PIO_WAIT_H2D,

	/*
	 * While in this state the IO request object is waiting for either a
	 * PIO Setup FIS or a D2H register FIS.  The type of frame received is
	 * based on the result of the prior frame and line conditions.
	 */
	SCI_REQ_STP_PIO_WAIT_FRAME,

	/*
	 * While in this state the IO request object is waiting for a DATA
	 * frame from the device.
	 */
	SCI_REQ_STP_PIO_DATA_IN,

	/*
	 * While in this state the IO request object is waiting to transmit
	 * the next data frame to the device.
	 */
	SCI_REQ_STP_PIO_DATA_OUT,

	/*
	 * The AWAIT_TC_COMPLETION sub-state indicates that the started raw
	 * task management request is waiting for the transmission of the
	 * initial frame (i.e. command, task, etc.).
	 */
	SCI_REQ_TASK_WAIT_TC_COMP,

	/*
	 * This sub-state indicates that the started task management request
	 * is waiting for the reception of an unsolicited frame
	 * (i.e. response IU).
	 */
	SCI_REQ_TASK_WAIT_TC_RESP,

	/*
	 * This sub-state indicates that the started task management request
	 * is waiting for the reception of an unsolicited frame
	 * (i.e. response IU).
	 */
	SCI_REQ_SMP_WAIT_RESP,

	/*
	 * The AWAIT_TC_COMPLETION sub-state indicates that the started SMP
	 * request is waiting for the transmission of the initial frame
	 * (i.e. command, task, etc.).
	 */
	SCI_REQ_SMP_WAIT_TC_COMP,

	/*
	 * This state indicates that the request has completed.
	 * This state is entered from the STARTED state. This state is entered
	 * from the ABORTING state.
	 */
	SCI_REQ_COMPLETED,

	/*
	 * This state indicates that the request is in the process of being
	 * terminated/aborted.
	 * This state is entered from the CONSTRUCTED state.
	 * This state is entered from the STARTED state.
	 */
	SCI_REQ_ABORTING,

	/*
	 * Simply the final state for the base request state machine.
	 */
	SCI_REQ_FINAL,
};

/**
 * scic_sds_request_get_controller() -
 *
 * This macro will return the controller for this io request object
 */
#define scic_sds_request_get_controller(sci_req) \
	((sci_req)->owning_controller)

/**
 * scic_sds_request_get_device() -
 *
 * This macro will return the device for this io request object
 */
#define scic_sds_request_get_device(sci_req) \
	((sci_req)->target_device)

/**
 * scic_sds_request_get_port() -
 *
 * This macro will return the port for this io request object
 */
#define scic_sds_request_get_port(sci_req)	\
	scic_sds_remote_device_get_port(scic_sds_request_get_device(sci_req))

/**
 * scic_sds_request_get_post_context() -
 *
 * This macro returns the constructed post context result for the io request.
 */
#define scic_sds_request_get_post_context(sci_req)	\
	((sci_req)->post_context)

/**
 * scic_sds_request_get_task_context() -
 *
 * This is a helper macro to return the os handle for this request object.
 */
#define scic_sds_request_get_task_context(request) \
	((request)->task_context_buffer)

/**
 * scic_sds_request_set_status() -
 *
 * This macro will set the scu hardware status and sci request completion
 * status for an io request.
 */
#define scic_sds_request_set_status(request, scu_status_code, sci_status_code) \
	{ \
		(request)->scu_status = (scu_status_code); \
		(request)->sci_status = (sci_status_code); \
	}

/**
 * SCU_SGL_ZERO() -
 *
 * This macro zeros the hardware SGL element data
 */
#define SCU_SGL_ZERO(scu_sge) \
	{ \
		(scu_sge).length = 0; \
		(scu_sge).address_lower = 0; \
		(scu_sge).address_upper = 0; \
		(scu_sge).address_modifier = 0;	\
	}

/**
 * SCU_SGL_COPY() -
 *
 * This macro copys the SGL Element data from the host os to the hardware SGL
 * elment data
 */
#define SCU_SGL_COPY(scu_sge, os_sge) \
	{ \
		(scu_sge).length = sg_dma_len(sg); \
		(scu_sge).address_upper = \
			upper_32_bits(sg_dma_address(sg)); \
		(scu_sge).address_lower = \
			lower_32_bits(sg_dma_address(sg)); \
		(scu_sge).address_modifier = 0;	\
	}

enum sci_status scic_sds_request_start(struct scic_sds_request *sci_req);
enum sci_status scic_sds_io_request_terminate(struct scic_sds_request *sci_req);
enum sci_status
scic_sds_io_request_event_handler(struct scic_sds_request *sci_req,
				  u32 event_code);
enum sci_status
scic_sds_io_request_frame_handler(struct scic_sds_request *sci_req,
				  u32 frame_index);
enum sci_status
scic_sds_task_request_terminate(struct scic_sds_request *sci_req);
extern enum sci_status
scic_sds_request_complete(struct scic_sds_request *sci_req);
extern enum sci_status
scic_sds_io_request_tc_completion(struct scic_sds_request *sci_req, u32 code);

/* XXX open code in caller */
static inline void *scic_request_get_virt_addr(struct scic_sds_request *sci_req,
					       dma_addr_t phys_addr)
{
	struct isci_request *ireq = sci_req_to_ireq(sci_req);
	dma_addr_t offset;

	BUG_ON(phys_addr < ireq->request_daddr);

	offset = phys_addr - ireq->request_daddr;

	BUG_ON(offset >= sizeof(*ireq));

	return (char *)ireq + offset;
}

/* XXX open code in caller */
static inline dma_addr_t
scic_io_request_get_dma_addr(struct scic_sds_request *sci_req, void *virt_addr)
{
	struct isci_request *ireq = sci_req_to_ireq(sci_req);

	char *requested_addr = (char *)virt_addr;
	char *base_addr = (char *)ireq;

	BUG_ON(requested_addr < base_addr);
	BUG_ON((requested_addr - base_addr) >= sizeof(*ireq));

	return ireq->request_daddr + (requested_addr - base_addr);
}

/**
 * This function gets the status of the request object.
 * @request: This parameter points to the isci_request object
 *
 * status of the object as a isci_request_status enum.
 */
static inline enum isci_request_status
isci_request_get_state(struct isci_request *isci_request)
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
static inline enum isci_request_status
isci_request_change_state(struct isci_request *isci_request,
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
static inline enum isci_request_status
isci_request_change_started_to_newstate(struct isci_request *isci_request,
					struct completion *completion_ptr,
					enum isci_request_status newstate)
{
	enum isci_request_status old_state;
	unsigned long flags;

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
static inline enum isci_request_status
isci_request_change_started_to_aborted(struct isci_request *isci_request,
				       struct completion *completion_ptr)
{
	return isci_request_change_started_to_newstate(isci_request,
						       completion_ptr,
						       aborted);
}
/**
 * isci_request_free() - This function frees the request object.
 * @isci_host: This parameter specifies the ISCI host object
 * @isci_request: This parameter points to the isci_request object
 *
 */
static inline void isci_request_free(struct isci_host *isci_host,
				     struct isci_request *isci_request)
{
	if (!isci_request)
		return;

	/* release the dma memory if we fail. */
	dma_pool_free(isci_host->dma_pool,
		      isci_request,
		      isci_request->request_daddr);
}

#define isci_request_access_task(req) ((req)->ttype_ptr.io_task_ptr)

#define isci_request_access_tmf(req) ((req)->ttype_ptr.tmf_task_ptr)

struct isci_request *isci_request_alloc_tmf(struct isci_host *ihost,
					    struct isci_tmf *isci_tmf,
					    gfp_t gfp_flags);

int isci_request_execute(struct isci_host *ihost, struct isci_remote_device *idev,
			 struct sas_task *task, gfp_t gfp_flags);

/**
 * isci_request_unmap_sgl() - This function unmaps the DMA address of a given
 *    sgl
 * @request: This parameter points to the isci_request object
 * @*pdev: This Parameter is the pci_device struct for the controller
 *
 */
static inline void
isci_request_unmap_sgl(struct isci_request *request, struct pci_dev *pdev)
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

/**
 * isci_request_io_request_get_next_sge() - This function is called by the sci
 *    core to retrieve the next sge for a given request.
 * @request: This parameter is the isci_request object.
 * @current_sge_address: This parameter is the last sge retrieved by the sci
 *    core for this request.
 *
 * pointer to the next sge for specified request.
 */
static inline void *
isci_request_io_request_get_next_sge(struct isci_request *request,
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

void
isci_terminate_pending_requests(struct isci_host *ihost,
				struct isci_remote_device *idev);
enum sci_status
scic_task_request_construct(struct scic_sds_controller *scic,
			    struct scic_sds_remote_device *sci_dev,
			    u16 io_tag,
			    struct scic_sds_request *sci_req);
enum sci_status
scic_task_request_construct_ssp(struct scic_sds_request *sci_req);
enum sci_status
scic_task_request_construct_sata(struct scic_sds_request *sci_req);
void
scic_stp_io_request_set_ncq_tag(struct scic_sds_request *sci_req, u16 ncq_tag);
void scic_sds_smp_request_copy_response(struct scic_sds_request *sci_req);
#endif /* !defined(_ISCI_REQUEST_H_) */
