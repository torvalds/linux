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

#ifndef __ISCI_H__
#define __ISCI_H__

#include <linux/interrupt.h>
#include <linux/types.h>

#define DRV_NAME "isci"
#define SCI_PCI_BAR_COUNT 2
#define SCI_NUM_MSI_X_INT 2
#define SCI_SMU_BAR       0
#define SCI_SMU_BAR_SIZE  (16*1024)
#define SCI_SCU_BAR       1
#define SCI_SCU_BAR_SIZE  (4*1024*1024)
#define SCI_IO_SPACE_BAR0 2
#define SCI_IO_SPACE_BAR1 3
#define ISCI_CAN_QUEUE_VAL 250 /* < SCI_MAX_IO_REQUESTS ? */
#define SCIC_CONTROLLER_STOP_TIMEOUT 5000

#define SCI_CONTROLLER_INVALID_IO_TAG 0xFFFF

#define SCI_MAX_PHYS  (4UL)
#define SCI_MAX_PORTS SCI_MAX_PHYS
#define SCI_MAX_SMP_PHYS  (384) /* not silicon constrained */
#define SCI_MAX_REMOTE_DEVICES (256UL)
#define SCI_MAX_IO_REQUESTS (256UL)
#define SCI_MAX_SEQ (16)
#define SCI_MAX_MSIX_MESSAGES  (2)
#define SCI_MAX_SCATTER_GATHER_ELEMENTS 130 /* not silicon constrained */
#define SCI_MAX_CONTROLLERS 2
#define SCI_MAX_DOMAINS  SCI_MAX_PORTS

#define SCU_MAX_CRITICAL_NOTIFICATIONS    (384)
#define SCU_MAX_EVENTS_SHIFT		  (7)
#define SCU_MAX_EVENTS                    (1 << SCU_MAX_EVENTS_SHIFT)
#define SCU_MAX_UNSOLICITED_FRAMES        (128)
#define SCU_MAX_COMPLETION_QUEUE_SCRATCH  (128)
#define SCU_MAX_COMPLETION_QUEUE_ENTRIES  (SCU_MAX_CRITICAL_NOTIFICATIONS \
					   + SCU_MAX_EVENTS \
					   + SCU_MAX_UNSOLICITED_FRAMES	\
					   + SCI_MAX_IO_REQUESTS \
					   + SCU_MAX_COMPLETION_QUEUE_SCRATCH)
#define SCU_MAX_COMPLETION_QUEUE_SHIFT	  (ilog2(SCU_MAX_COMPLETION_QUEUE_ENTRIES))

#define SCU_ABSOLUTE_MAX_UNSOLICITED_FRAMES (4096)
#define SCU_UNSOLICITED_FRAME_BUFFER_SIZE   (1024U)
#define SCU_INVALID_FRAME_INDEX             (0xFFFF)

#define SCU_IO_REQUEST_MAX_SGE_SIZE         (0x00FFFFFF)
#define SCU_IO_REQUEST_MAX_TRANSFER_LENGTH  (0x00FFFFFF)

static inline void check_sizes(void)
{
	BUILD_BUG_ON_NOT_POWER_OF_2(SCU_MAX_EVENTS);
	BUILD_BUG_ON(SCU_MAX_UNSOLICITED_FRAMES <= 8);
	BUILD_BUG_ON_NOT_POWER_OF_2(SCU_MAX_UNSOLICITED_FRAMES);
	BUILD_BUG_ON_NOT_POWER_OF_2(SCU_MAX_COMPLETION_QUEUE_ENTRIES);
	BUILD_BUG_ON(SCU_MAX_UNSOLICITED_FRAMES > SCU_ABSOLUTE_MAX_UNSOLICITED_FRAMES);
	BUILD_BUG_ON_NOT_POWER_OF_2(SCI_MAX_IO_REQUESTS);
	BUILD_BUG_ON_NOT_POWER_OF_2(SCI_MAX_SEQ);
}

/**
 * enum sci_status - This is the general return status enumeration for non-IO,
 *    non-task management related SCI interface methods.
 *
 *
 */
enum sci_status {
	/**
	 * This member indicates successful completion.
	 */
	SCI_SUCCESS = 0,

	/**
	 * This value indicates that the calling method completed successfully,
	 * but that the IO may have completed before having it's start method
	 * invoked.  This occurs during SAT translation for requests that do
	 * not require an IO to the target or for any other requests that may
	 * be completed without having to submit IO.
	 */
	SCI_SUCCESS_IO_COMPLETE_BEFORE_START,

	/**
	 *  This Value indicates that the SCU hardware returned an early response
	 *  because the io request specified more data than is returned by the
	 *  target device (mode pages, inquiry data, etc.). The completion routine
	 *  will handle this case to get the actual number of bytes transferred.
	 */
	SCI_SUCCESS_IO_DONE_EARLY,

	/**
	 * This member indicates that the object for which a state change is
	 * being requested is already in said state.
	 */
	SCI_WARNING_ALREADY_IN_STATE,

	/**
	 * This member indicates interrupt coalescence timer may cause SAS
	 * specification compliance issues (i.e. SMP target mode response
	 * frames must be returned within 1.9 milliseconds).
	 */
	SCI_WARNING_TIMER_CONFLICT,

	/**
	 * This field indicates a sequence of action is not completed yet. Mostly,
	 * this status is used when multiple ATA commands are needed in a SATI translation.
	 */
	SCI_WARNING_SEQUENCE_INCOMPLETE,

	/**
	 * This member indicates that there was a general failure.
	 */
	SCI_FAILURE,

	/**
	 * This member indicates that the SCI implementation is unable to complete
	 * an operation due to a critical flaw the prevents any further operation
	 * (i.e. an invalid pointer).
	 */
	SCI_FATAL_ERROR,

	/**
	 * This member indicates the calling function failed, because the state
	 * of the controller is in a state that prevents successful completion.
	 */
	SCI_FAILURE_INVALID_STATE,

	/**
	 * This member indicates the calling function failed, because there is
	 * insufficient resources/memory to complete the request.
	 */
	SCI_FAILURE_INSUFFICIENT_RESOURCES,

	/**
	 * This member indicates the calling function failed, because the
	 * controller object required for the operation can't be located.
	 */
	SCI_FAILURE_CONTROLLER_NOT_FOUND,

	/**
	 * This member indicates the calling function failed, because the
	 * discovered controller type is not supported by the library.
	 */
	SCI_FAILURE_UNSUPPORTED_CONTROLLER_TYPE,

	/**
	 * This member indicates the calling function failed, because the
	 * requested initialization data version isn't supported.
	 */
	SCI_FAILURE_UNSUPPORTED_INIT_DATA_VERSION,

	/**
	 * This member indicates the calling function failed, because the
	 * requested configuration of SAS Phys into SAS Ports is not supported.
	 */
	SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION,

	/**
	 * This member indicates the calling function failed, because the
	 * requested protocol is not supported by the remote device, port,
	 * or controller.
	 */
	SCI_FAILURE_UNSUPPORTED_PROTOCOL,

	/**
	 * This member indicates the calling function failed, because the
	 * requested information type is not supported by the SCI implementation.
	 */
	SCI_FAILURE_UNSUPPORTED_INFORMATION_TYPE,

	/**
	 * This member indicates the calling function failed, because the
	 * device already exists.
	 */
	SCI_FAILURE_DEVICE_EXISTS,

	/**
	 * This member indicates the calling function failed, because adding
	 * a phy to the object is not possible.
	 */
	SCI_FAILURE_ADDING_PHY_UNSUPPORTED,

	/**
	 * This member indicates the calling function failed, because the
	 * requested information type is not supported by the SCI implementation.
	 */
	SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD,

	/**
	 * This member indicates the calling function failed, because the SCI
	 * implementation does not support the supplied time limit.
	 */
	SCI_FAILURE_UNSUPPORTED_TIME_LIMIT,

	/**
	 * This member indicates the calling method failed, because the SCI
	 * implementation does not contain the specified Phy.
	 */
	SCI_FAILURE_INVALID_PHY,

	/**
	 * This member indicates the calling method failed, because the SCI
	 * implementation does not contain the specified Port.
	 */
	SCI_FAILURE_INVALID_PORT,

	/**
	 * This member indicates the calling method was partly successful
	 * The port was reset but not all phys in port are operational
	 */
	SCI_FAILURE_RESET_PORT_PARTIAL_SUCCESS,

	/**
	 * This member indicates that calling method failed
	 * The port reset did not complete because none of the phys are operational
	 */
	SCI_FAILURE_RESET_PORT_FAILURE,

	/**
	 * This member indicates the calling method failed, because the SCI
	 * implementation does not contain the specified remote device.
	 */
	SCI_FAILURE_INVALID_REMOTE_DEVICE,

	/**
	 * This member indicates the calling method failed, because the remote
	 * device is in a bad state and requires a reset.
	 */
	SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED,

	/**
	 * This member indicates the calling method failed, because the SCI
	 * implementation does not contain or support the specified IO tag.
	 */
	SCI_FAILURE_INVALID_IO_TAG,

	/**
	 * This member indicates that the operation failed and the user should
	 * check the response data associated with the IO.
	 */
	SCI_FAILURE_IO_RESPONSE_VALID,

	/**
	 * This member indicates that the operation failed, the failure is
	 * controller implementation specific, and the response data associated
	 * with the request is not valid.  You can query for the controller
	 * specific error information via sci_controller_get_request_status()
	 */
	SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR,

	/**
	 * This member indicated that the operation failed because the
	 * user requested this IO to be terminated.
	 */
	SCI_FAILURE_IO_TERMINATED,

	/**
	 * This member indicates that the operation failed and the associated
	 * request requires a SCSI abort task to be sent to the target.
	 */
	SCI_FAILURE_IO_REQUIRES_SCSI_ABORT,

	/**
	 * This member indicates that the operation failed because the supplied
	 * device could not be located.
	 */
	SCI_FAILURE_DEVICE_NOT_FOUND,

	/**
	 * This member indicates that the operation failed because the
	 * objects association is required and is not correctly set.
	 */
	SCI_FAILURE_INVALID_ASSOCIATION,

	/**
	 * This member indicates that the operation failed, because a timeout
	 * occurred.
	 */
	SCI_FAILURE_TIMEOUT,

	/**
	 * This member indicates that the operation failed, because the user
	 * specified a value that is either invalid or not supported.
	 */
	SCI_FAILURE_INVALID_PARAMETER_VALUE,

	/**
	 * This value indicates that the operation failed, because the number
	 * of messages (MSI-X) is not supported.
	 */
	SCI_FAILURE_UNSUPPORTED_MESSAGE_COUNT,

	/**
	 * This value indicates that the method failed due to a lack of
	 * available NCQ tags.
	 */
	SCI_FAILURE_NO_NCQ_TAG_AVAILABLE,

	/**
	 * This value indicates that a protocol violation has occurred on the
	 * link.
	 */
	SCI_FAILURE_PROTOCOL_VIOLATION,

	/**
	 * This value indicates a failure condition that retry may help to clear.
	 */
	SCI_FAILURE_RETRY_REQUIRED,

	/**
	 * This field indicates the retry limit was reached when a retry is attempted
	 */
	SCI_FAILURE_RETRY_LIMIT_REACHED,

	/**
	 * This member indicates the calling method was partly successful.
	 * Mostly, this status is used when a LUN_RESET issued to an expander attached
	 * STP device in READY NCQ substate needs to have RNC suspended/resumed
	 * before posting TC.
	 */
	SCI_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS,

	/**
	 * This field indicates an illegal phy connection based on the routing attribute
	 * of both expander phy attached to each other.
	 */
	SCI_FAILURE_ILLEGAL_ROUTING_ATTRIBUTE_CONFIGURATION,

	/**
	 * This field indicates a CONFIG ROUTE INFO command has a response with function result
	 * INDEX DOES NOT EXIST, usually means exceeding max route index.
	 */
	SCI_FAILURE_EXCEED_MAX_ROUTE_INDEX,

	/**
	 * This value indicates that an unsupported PCI device ID has been
	 * specified.  This indicates that attempts to invoke
	 * sci_library_allocate_controller() will fail.
	 */
	SCI_FAILURE_UNSUPPORTED_PCI_DEVICE_ID

};

/**
 * enum sci_io_status - This enumeration depicts all of the possible IO
 *    completion status values.  Each value in this enumeration maps directly
 *    to a value in the enum sci_status enumeration.  Please refer to that
 *    enumeration for detailed comments concerning what the status represents.
 *
 * Add the API to retrieve the SCU status from the core. Check to see that the
 * following status are properly handled: - SCI_IO_FAILURE_UNSUPPORTED_PROTOCOL
 * - SCI_IO_FAILURE_INVALID_IO_TAG
 */
enum sci_io_status {
	SCI_IO_SUCCESS                         = SCI_SUCCESS,
	SCI_IO_FAILURE                         = SCI_FAILURE,
	SCI_IO_SUCCESS_COMPLETE_BEFORE_START   = SCI_SUCCESS_IO_COMPLETE_BEFORE_START,
	SCI_IO_SUCCESS_IO_DONE_EARLY           = SCI_SUCCESS_IO_DONE_EARLY,
	SCI_IO_FAILURE_INVALID_STATE           = SCI_FAILURE_INVALID_STATE,
	SCI_IO_FAILURE_INSUFFICIENT_RESOURCES  = SCI_FAILURE_INSUFFICIENT_RESOURCES,
	SCI_IO_FAILURE_UNSUPPORTED_PROTOCOL    = SCI_FAILURE_UNSUPPORTED_PROTOCOL,
	SCI_IO_FAILURE_RESPONSE_VALID          = SCI_FAILURE_IO_RESPONSE_VALID,
	SCI_IO_FAILURE_CONTROLLER_SPECIFIC_ERR = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR,
	SCI_IO_FAILURE_TERMINATED              = SCI_FAILURE_IO_TERMINATED,
	SCI_IO_FAILURE_REQUIRES_SCSI_ABORT     = SCI_FAILURE_IO_REQUIRES_SCSI_ABORT,
	SCI_IO_FAILURE_INVALID_PARAMETER_VALUE = SCI_FAILURE_INVALID_PARAMETER_VALUE,
	SCI_IO_FAILURE_NO_NCQ_TAG_AVAILABLE    = SCI_FAILURE_NO_NCQ_TAG_AVAILABLE,
	SCI_IO_FAILURE_PROTOCOL_VIOLATION      = SCI_FAILURE_PROTOCOL_VIOLATION,

	SCI_IO_FAILURE_REMOTE_DEVICE_RESET_REQUIRED = SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED,

	SCI_IO_FAILURE_RETRY_REQUIRED      = SCI_FAILURE_RETRY_REQUIRED,
	SCI_IO_FAILURE_RETRY_LIMIT_REACHED = SCI_FAILURE_RETRY_LIMIT_REACHED,
	SCI_IO_FAILURE_INVALID_REMOTE_DEVICE = SCI_FAILURE_INVALID_REMOTE_DEVICE
};

/**
 * enum sci_task_status - This enumeration depicts all of the possible task
 *    completion status values.  Each value in this enumeration maps directly
 *    to a value in the enum sci_status enumeration.  Please refer to that
 *    enumeration for detailed comments concerning what the status represents.
 *
 * Check to see that the following status are properly handled:
 */
enum sci_task_status {
	SCI_TASK_SUCCESS                         = SCI_SUCCESS,
	SCI_TASK_FAILURE                         = SCI_FAILURE,
	SCI_TASK_FAILURE_INVALID_STATE           = SCI_FAILURE_INVALID_STATE,
	SCI_TASK_FAILURE_INSUFFICIENT_RESOURCES  = SCI_FAILURE_INSUFFICIENT_RESOURCES,
	SCI_TASK_FAILURE_UNSUPPORTED_PROTOCOL    = SCI_FAILURE_UNSUPPORTED_PROTOCOL,
	SCI_TASK_FAILURE_INVALID_TAG             = SCI_FAILURE_INVALID_IO_TAG,
	SCI_TASK_FAILURE_RESPONSE_VALID          = SCI_FAILURE_IO_RESPONSE_VALID,
	SCI_TASK_FAILURE_CONTROLLER_SPECIFIC_ERR = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR,
	SCI_TASK_FAILURE_TERMINATED              = SCI_FAILURE_IO_TERMINATED,
	SCI_TASK_FAILURE_INVALID_PARAMETER_VALUE = SCI_FAILURE_INVALID_PARAMETER_VALUE,

	SCI_TASK_FAILURE_REMOTE_DEVICE_RESET_REQUIRED = SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED,
	SCI_TASK_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS = SCI_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS

};

/**
 * sci_swab32_cpy - convert between scsi and scu-hardware byte format
 * @dest: receive the 4-byte endian swapped version of src
 * @src: word aligned source buffer
 *
 * scu hardware handles SSP/SMP control, response, and unidentified
 * frames in "big endian dword" order.  Regardless of host endian this
 * is always a swab32()-per-dword conversion of the standard definition,
 * i.e. single byte fields swapped and multi-byte fields in little-
 * endian
 */
static inline void sci_swab32_cpy(void *_dest, void *_src, ssize_t word_cnt)
{
	u32 *dest = _dest, *src = _src;

	while (--word_cnt >= 0)
		dest[word_cnt] = swab32(src[word_cnt]);
}

extern unsigned char no_outbound_task_to;
extern u16 ssp_max_occ_to;
extern u16 stp_max_occ_to;
extern u16 ssp_inactive_to;
extern u16 stp_inactive_to;
extern unsigned char phy_gen;
extern unsigned char max_concurr_spinup;
extern uint cable_selection_override;

irqreturn_t isci_msix_isr(int vec, void *data);
irqreturn_t isci_intx_isr(int vec, void *data);
irqreturn_t isci_error_isr(int vec, void *data);

/*
 * Each timer is associated with a cancellation flag that is set when
 * del_timer() is called and checked in the timer callback function. This
 * is needed since del_timer_sync() cannot be called with sci_lock held.
 * For deinit however, del_timer_sync() is used without holding the lock.
 */
struct sci_timer {
	struct timer_list	timer;
	bool			cancel;
};

static inline
void sci_init_timer(struct sci_timer *tmr, void (*fn)(unsigned long))
{
	tmr->timer.function = fn;
	tmr->timer.data = (unsigned long) tmr;
	tmr->cancel = 0;
	init_timer(&tmr->timer);
}

static inline void sci_mod_timer(struct sci_timer *tmr, unsigned long msec)
{
	tmr->cancel = 0;
	mod_timer(&tmr->timer, jiffies + msecs_to_jiffies(msec));
}

static inline void sci_del_timer(struct sci_timer *tmr)
{
	tmr->cancel = 1;
	del_timer(&tmr->timer);
}

struct sci_base_state_machine {
	const struct sci_base_state *state_table;
	u32 initial_state_id;
	u32 current_state_id;
	u32 previous_state_id;
};

typedef void (*sci_state_transition_t)(struct sci_base_state_machine *sm);

struct sci_base_state {
	sci_state_transition_t enter_state;	/* Called on state entry */
	sci_state_transition_t exit_state;	/* Called on state exit */
};

extern void sci_init_sm(struct sci_base_state_machine *sm,
			const struct sci_base_state *state_table,
			u32 initial_state);
extern void sci_change_state(struct sci_base_state_machine *sm, u32 next_state);
#endif  /* __ISCI_H__ */
