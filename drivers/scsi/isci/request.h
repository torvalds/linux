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

/**
 * isci_stp_request - extra request infrastructure to handle pio/atapi protocol
 * @pio_len - number of bytes requested at PIO setup
 * @status - pio setup ending status value to tell us if we need
 *	     to wait for another fis or if the transfer is complete.  Upon
 *           receipt of a d2h fis this will be the status field of that fis.
 * @sgl - track pio transfer progress as we iterate through the sgl
 * @device_cdb_len - atapi device advertises it's transfer constraints at setup
 */
struct isci_stp_request {
	u32 pio_len;
	u8 status;

	struct isci_stp_pio_sgl {
		int index;
		u8 set;
		u32 offset;
	} sgl;
	u32 device_cdb_len;
};

struct isci_request {
	enum isci_request_status status;
	#define IREQ_COMPLETE_IN_TARGET 0
	#define IREQ_TERMINATED 1
	#define IREQ_TMF 2
	#define IREQ_ACTIVE 3
	unsigned long flags;
	/* XXX kill ttype and ttype_ptr, allocate full sas_task */
	enum task_type ttype;
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
	unsigned int num_sg_entries;
	/* Note: "io_request_completion" is completed in two different ways
	 * depending on whether this is a TMF or regular request.
	 * - TMF requests are completed in the thread that started them;
	 * - regular requests are completed in the request completion callback
	 *   function.
	 * This difference in operation allows the aborter of a TMF request
	 * to be sure that once the TMF request completes, the I/O that the
	 * TMF was aborting is guaranteed to have completed.
	 *
	 * XXX kill io_request_completion
	 */
	struct completion *io_request_completion;
	struct sci_base_state_machine sm;
	struct isci_host *owning_controller;
	struct isci_remote_device *target_device;
	u16 io_tag;
	enum sci_request_protocol protocol;
	u32 scu_status; /* hardware result */
	u32 sci_status; /* upper layer disposition */
	u32 post_context;
	struct scu_task_context *tc;
	/* could be larger with sg chaining */
	#define SCU_SGL_SIZE ((SCI_MAX_SCATTER_GATHER_ELEMENTS + 1) / 2)
	struct scu_sgl_element_pair sg_table[SCU_SGL_SIZE] __attribute__ ((aligned(32)));
	/* This field is a pointer to the stored rx frame data.  It is used in
	 * STP internal requests and SMP response frames.  If this field is
	 * non-NULL the saved frame must be released on IO request completion.
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
			struct isci_stp_request req;
			struct host_to_dev_fis cmd;
			struct dev_to_host_fis rsp;
		} stp;
	};
};

static inline struct isci_request *to_ireq(struct isci_stp_request *stp_req)
{
	struct isci_request *ireq;

	ireq = container_of(stp_req, typeof(*ireq), stp.req);
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

enum sci_status sci_request_start(struct isci_request *ireq);
enum sci_status sci_io_request_terminate(struct isci_request *ireq);
enum sci_status
sci_io_request_event_handler(struct isci_request *ireq,
				  u32 event_code);
enum sci_status
sci_io_request_frame_handler(struct isci_request *ireq,
				  u32 frame_index);
enum sci_status
sci_task_request_terminate(struct isci_request *ireq);
extern enum sci_status
sci_request_complete(struct isci_request *ireq);
extern enum sci_status
sci_io_request_tc_completion(struct isci_request *ireq, u32 code);

/* XXX open code in caller */
static inline dma_addr_t
sci_io_request_get_dma_addr(struct isci_request *ireq, void *virt_addr)
{

	char *requested_addr = (char *)virt_addr;
	char *base_addr = (char *)ireq;

	BUG_ON(requested_addr < base_addr);
	BUG_ON((requested_addr - base_addr) >= sizeof(*ireq));

	return ireq->request_daddr + (requested_addr - base_addr);
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

#define isci_request_access_task(req) ((req)->ttype_ptr.io_task_ptr)

#define isci_request_access_tmf(req) ((req)->ttype_ptr.tmf_task_ptr)

struct isci_request *isci_tmf_request_from_tag(struct isci_host *ihost,
					       struct isci_tmf *isci_tmf,
					       u16 tag);
int isci_request_execute(struct isci_host *ihost, struct isci_remote_device *idev,
			 struct sas_task *task, u16 tag);
void isci_terminate_pending_requests(struct isci_host *ihost,
				     struct isci_remote_device *idev);
enum sci_status
sci_task_request_construct(struct isci_host *ihost,
			    struct isci_remote_device *idev,
			    u16 io_tag,
			    struct isci_request *ireq);
enum sci_status
sci_task_request_construct_ssp(struct isci_request *ireq);
enum sci_status
sci_task_request_construct_sata(struct isci_request *ireq);
void sci_smp_request_copy_response(struct isci_request *ireq);

static inline int isci_task_is_ncq_recovery(struct sas_task *task)
{
	return (sas_protocol_ata(task->task_proto) &&
		task->ata_task.fis.command == ATA_CMD_READ_LOG_EXT &&
		task->ata_task.fis.lbal == ATA_LOG_SATA_NCQ);

}

#endif /* !defined(_ISCI_REQUEST_H_) */
