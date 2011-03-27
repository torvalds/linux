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

#ifndef _SCIC_SDS_CONTROLLER_H_
#define _SCIC_SDS_CONTROLLER_H_

#include <linux/string.h>

/**
 * This file contains the structures, constants and prototypes used for the
 *    core controller object.
 *
 *
 */

#include "sci_pool.h"
#include "sci_controller_constants.h"
#include "sci_memory_descriptor_list.h"
#include "sci_base_controller.h"
#include "scic_config_parameters.h"
#include "scic_sds_port.h"
#include "scic_sds_phy.h"
#include "scic_sds_remote_node_table.h"
#include "scu_registers.h"
#include "scu_constants.h"
#include "scu_remote_node_context.h"
#include "scu_task_context.h"
#include "scu_unsolicited_frame.h"
#include "scic_sds_unsolicited_frame_control.h"
#include "scic_sds_port_configuration_agent.h"
#include "scic_sds_pci.h"

struct scic_sds_remote_device;
struct scic_sds_request;
struct scic_sds_controller;


#define SCU_COMPLETION_RAM_ALIGNMENT            (64)

/**
 * enum scic_sds_controller_memory_descriptors -
 *
 * This enumeration depects the types of MDEs that are going to be created for
 * the controller object.
 */
enum scic_sds_controller_memory_descriptors {
	/**
	 * Completion queue MDE entry
	 */
	SCU_MDE_COMPLETION_QUEUE,

	/**
	 * Remote node context MDE entry
	 */
	SCU_MDE_REMOTE_NODE_CONTEXT,

	/**
	 * Task context MDE entry
	 */
	SCU_MDE_TASK_CONTEXT,

	/**
	 * Unsolicited frame buffer MDE entrys this is the start of the unsolicited
	 * frame buffer entries.
	 */
	SCU_MDE_UF_BUFFER,

	SCU_MAX_MDES
};


/**
 * struct scic_power_control -
 *
 * This structure defines the fields for managing power control for direct
 * attached disk devices.
 */
struct scic_power_control {
	/**
	 * This field is set when the power control timer is running and cleared when
	 * it is not.
	 */
	bool timer_started;

	/**
	 * This field is the handle to the driver timer object.  This timer is used to
	 * control when the directed attached disks can consume power.
	 */
	void *timer;

	/**
	 * This field is used to keep track of how many phys are put into the
	 * requesters field.
	 */
	u8 phys_waiting;

	/**
	 * This field is used to keep track of how many phys have been granted to consume power
	 */
	u8 phys_granted_power;

	/**
	 * This field is an array of phys that we are waiting on. The phys are direct
	 * mapped into requesters via struct scic_sds_phy.phy_index
	 */
	struct scic_sds_phy *requesters[SCI_MAX_PHYS];

};

/**
 * struct scic_sds_controller -
 *
 * This structure represents the SCU contoller object.
 */
struct scic_sds_controller {
	/**
	 * The struct sci_base_controller is the parent object for the struct scic_sds_controller
	 * object.
	 */
	struct sci_base_controller parent;

	/**
	 * This field is the driver timer object handler used to time the controller
	 * object start and stop requests.
	 */
	void *timeout_timer;

	/**
	 * This field contains the user parameters to be utilized for this
	 * core controller object.
	 */
	union scic_user_parameters user_parameters;

	/**
	 * This field contains the OEM parameters to be utilized for this
	 * core controller object.
	 */
	union scic_oem_parameters oem_parameters;

	/**
	 * This field contains the port configuration agent for this controller.
	 */
	struct scic_sds_port_configuration_agent port_agent;

	/**
	 * This field is the array of port objects that are controlled by this
	 * controller object.  There is one dummy port object also contained within
	 * this controller object.
	 */
	struct scic_sds_port port_table[SCI_MAX_PORTS + 1];

	/**
	 * This field is the array of phy objects that are controlled by this
	 * controller object.
	 */
	struct scic_sds_phy phy_table[SCI_MAX_PHYS];

	/**
	 * This field is the array of device objects that are currently constructed
	 * for this controller object.  This table is used as a fast lookup of device
	 * objects that need to handle device completion notifications from the
	 * hardware. The table is RNi based.
	 */
	struct scic_sds_remote_device *device_table[SCI_MAX_REMOTE_DEVICES];

	/**
	 * This field is the array of IO request objects that are currently active for
	 * this controller object.  This table is used as a fast lookup of the io
	 * request object that need to handle completion queue notifications.  The
	 * table is TCi based.
	 */
	struct scic_sds_request *io_request_table[SCI_MAX_IO_REQUESTS];

	/**
	 * This field is the free RNi data structure
	 */
	struct scic_remote_node_table available_remote_nodes;

	/**
	 * This field is the TCi pool used to manage the task context index.
	 */
	SCI_POOL_CREATE(tci_pool, u16, SCI_MAX_IO_REQUESTS);

	/**
	 * This filed is the struct scic_power_control data used to controll when direct
	 * attached devices can consume power.
	 */
	struct scic_power_control power_control;

	/**
	 * This field is the array of sequence values for the IO Tag fields.  Even
	 * though only 4 bits of the field is used for the sequence the sequence is 16
	 * bits in size so the sequence can be bitwise or'd with the TCi to build the
	 * IO Tag value.
	 */
	u16 io_request_sequence[SCI_MAX_IO_REQUESTS];

	/**
	 * This field in the array of sequence values for the RNi.  These are used
	 * to control io request build to io request start operations.  The sequence
	 * value is recorded into an io request when it is built and is checked on
	 * the io request start operation to make sure that there was not a device
	 * hot plug between the build and start operation.
	 */
	u8 remote_device_sequence[SCI_MAX_REMOTE_DEVICES];

	/**
	 * This field is a pointer to the memory allocated by the driver for the task
	 * context table.  This data is shared between the hardware and software.
	 */
	struct scu_task_context *task_context_table;

	/**
	 * This field is a pointer to the memory allocated by the driver for the
	 * remote node context table.  This table is shared between the hardware and
	 * software.
	 */
	union scu_remote_node_context *remote_node_context_table;

	/**
	 * This field is the array of physical memory requiremets for this controller
	 * object.
	 */
	struct sci_physical_memory_descriptor memory_descriptors[SCU_MAX_MDES];

	/**
	 * This field is a pointer to the completion queue.  This memory is
	 * written to by the hardware and read by the software.
	 */
	u32 *completion_queue;

	/**
	 * This field is the software copy of the completion queue get pointer.  The
	 * controller object writes this value to the hardware after processing the
	 * completion entries.
	 */
	u32 completion_queue_get;

	/**
	 * This field is the minimum of the number of hardware supported port entries
	 * and the software requested port entries.
	 */
	u32 logical_port_entries;

	/**
	 * This field is the minimum number of hardware supported completion queue
	 * entries and the software requested completion queue entries.
	 */
	u32 completion_queue_entries;

	/**
	 * This field is the minimum number of hardware supported event entries and
	 * the software requested event entries.
	 */
	u32 completion_event_entries;

	/**
	 * This field is the minimum number of devices supported by the hardware and
	 * the number of devices requested by the software.
	 */
	u32 remote_node_entries;

	/**
	 * This field is the minimum number of IO requests supported by the hardware
	 * and the number of IO requests requested by the software.
	 */
	u32 task_context_entries;

	/**
	 * This object contains all of the unsolicited frame specific
	 * data utilized by the core controller.
	 */
	struct scic_sds_unsolicited_frame_control uf_control;

	/* Phy Startup Data */
	/**
	 * This field is the driver timer handle for controller phy request startup.
	 * On controller start the controller will start each PHY individually in
	 * order of phy index.
	 */
	void *phy_startup_timer;

	/**
	 * This field is set when the phy_startup_timer is running and is cleared when
	 * the phy_startup_timer is stopped.
	 */
	bool phy_startup_timer_pending;

	/**
	 * This field is the index of the next phy start.  It is initialized to 0 and
	 * increments for each phy index that is started.
	 */
	u32 next_phy_to_start;

	/**
	 * This field controlls the invalid link up notifications to the SCI_USER.  If
	 * an invalid_link_up notification is reported a bit for the PHY index is set
	 * so further notifications are not made.  Once the PHY object reports link up
	 * and is made part of a port then this bit for the PHY index is cleared.
	 */
	u8 invalid_phy_mask;

	/*
	 * This field saves the current interrupt coalescing number of the controller.
	 */
	u16 interrupt_coalesce_number;

	/*
	 * This field saves the current interrupt coalescing timeout value in microseconds.
	 */
	u32 interrupt_coalesce_timeout;

	/**
	 * This field is a pointer to the memory mapped register space for the
	 * struct smu_registers.
	 */
	struct smu_registers __iomem *smu_registers;

	/**
	 * This field is a pointer to the memory mapped register space for the
	 * struct scu_registers.
	 */
	struct scu_registers __iomem *scu_registers;

};

typedef void (*scic_sds_controller_phy_handler_t)(struct scic_sds_controller *,
						  struct scic_sds_port *,
						  struct scic_sds_phy *);

typedef void (*scic_sds_controller_device_handler_t)(struct scic_sds_controller *,
						  struct scic_sds_remote_device *);


/**
 * struct scic_sds_controller_state_handler -
 *
 * This structure contains the SDS core specific definition for the state
 * handlers.
 */
struct scic_sds_controller_state_handler {
	struct sci_base_controller_state_handler base;

	sci_base_controller_request_handler_t terminate_request;
	scic_sds_controller_phy_handler_t link_up;
	scic_sds_controller_phy_handler_t link_down;
	scic_sds_controller_device_handler_t device_stopped;
};

extern const struct scic_sds_controller_state_handler
	scic_sds_controller_state_handler_table[];

/**
 * INCREMENT_QUEUE_GET() -
 *
 * This macro will increment the specified index to and if the index wraps to 0
 * it will toggel the cycle bit.
 */
#define INCREMENT_QUEUE_GET(index, cycle, entry_count, bit_toggle) \
	{ \
		if ((index) + 1 == entry_count) {	\
			(index) = 0; \
			(cycle) = (cycle) ^ (bit_toggle); \
		} else { \
			index = index + 1; \
		} \
	}

/**
 * scic_sds_controller_get_port_configuration_agent() -
 *
 * This is a helper macro to get the port configuration agent from the
 * controller object.
 */
#define scic_sds_controller_get_port_configuration_agent(controller) \
	(&(controller)->port_agent)

/**
 * smu_register_write() -
 *
 * This macro writes to the smu_register for this controller
 */
#define smu_register_write(controller, reg, value) \
	scic_sds_pci_write_smu_dword((controller), &(reg), (value))

/**
 * smu_register_read() -
 *
 * This macro reads the smu_register for this controller
 */
#define smu_register_read(controller, reg) \
	scic_sds_pci_read_smu_dword((controller), &(reg))

/**
 * scu_register_write() -
 *
 * This mcaro writes the scu_register for this controller
 */
#define scu_register_write(controller, reg, value) \
	scic_sds_pci_write_scu_dword((controller), &(reg), (value))

/**
 * scu_register_read() -
 *
 * This macro reads the scu_register for this controller
 */
#define scu_register_read(controller, reg) \
	scic_sds_pci_read_scu_dword((controller), &(reg))

/**
 * scic_sds_controller_get_protocol_engine_group() -
 *
 * This macro returns the protocol engine group for this controller object.
 * Presently we only support protocol engine group 0 so just return that
 */
#define scic_sds_controller_get_protocol_engine_group(controller) 0

/**
 * scic_sds_io_tag_construct() -
 *
 * This macro constructs an IO tag from the sequence and index values.
 */
#define scic_sds_io_tag_construct(sequence, task_index)	\
	((sequence) << 12 | (task_index))

/**
 * scic_sds_io_tag_get_sequence() -
 *
 * This macro returns the IO sequence from the IO tag value.
 */
#define scic_sds_io_tag_get_sequence(io_tag) \
	(((io_tag) & 0xF000) >> 12)

/**
 * scic_sds_io_tag_get_index() -
 *
 * This macro returns the TCi from the io tag value
 */
#define scic_sds_io_tag_get_index(io_tag) \
	((io_tag) & 0x0FFF)

/**
 * scic_sds_io_sequence_increment() -
 *
 * This is a helper macro to increment the io sequence count. We may find in
 * the future that it will be faster to store the sequence count in such a way
 * as we dont perform the shift operation to build io tag values so therefore
 * need a way to incrment them correctly
 */
#define scic_sds_io_sequence_increment(value) \
	((value) = (((value) + 1) & 0x000F))

#define scic_sds_remote_device_node_count(device) \
	(\
		(\
			(device)->target_protocols.u.bits.attached_stp_target \
			&& ((device)->is_direct_attached != true) \
		) \
		? SCU_STP_REMOTE_NODE_COUNT : SCU_SSP_REMOTE_NODE_COUNT	\
	)

/**
 * scic_sds_controller_set_invalid_phy() -
 *
 * This macro will set the bit in the invalid phy mask for this controller
 * object.  This is used to control messages reported for invalid link up
 * notifications.
 */
#define scic_sds_controller_set_invalid_phy(controller, phy) \
	((controller)->invalid_phy_mask |= (1 << (phy)->phy_index))

/**
 * scic_sds_controller_clear_invalid_phy() -
 *
 * This macro will clear the bit in the invalid phy mask for this controller
 * object.  This is used to control messages reported for invalid link up
 * notifications.
 */
#define scic_sds_controller_clear_invalid_phy(controller, phy) \
	((controller)->invalid_phy_mask &= ~(1 << (phy)->phy_index))

void scic_sds_controller_post_request(
	struct scic_sds_controller *this_controller,
	u32 request);

void scic_sds_controller_release_frame(
	struct scic_sds_controller *this_controller,
	u32 frame_index);

void scic_sds_controller_copy_sata_response(
	void *response_buffer,
	void *frame_header,
	void *frame_buffer);

enum sci_status scic_sds_controller_allocate_remote_node_context(
	struct scic_sds_controller *this_controller,
	struct scic_sds_remote_device *the_device,
	u16 *node_id);

void scic_sds_controller_free_remote_node_context(
	struct scic_sds_controller *this_controller,
	struct scic_sds_remote_device *the_device,
	u16 node_id);

union scu_remote_node_context *scic_sds_controller_get_remote_node_context_buffer(
	struct scic_sds_controller *this_controller,
	u16 node_id);

struct scic_sds_request *scic_sds_controller_get_io_request_from_tag(
	struct scic_sds_controller *this_controller,
	u16 io_tag);


struct scu_task_context *scic_sds_controller_get_task_context_buffer(
	struct scic_sds_controller *this_controller,
	u16 io_tag);

void scic_sds_controller_power_control_queue_insert(
	struct scic_sds_controller *this_controller,
	struct scic_sds_phy *the_phy);

void scic_sds_controller_power_control_queue_remove(
	struct scic_sds_controller *this_controller,
	struct scic_sds_phy *the_phy);

void scic_sds_controller_link_up(
	struct scic_sds_controller *this_controller,
	struct scic_sds_port *the_port,
	struct scic_sds_phy *the_phy);

void scic_sds_controller_link_down(
	struct scic_sds_controller *this_controller,
	struct scic_sds_port *the_port,
	struct scic_sds_phy *the_phy);

void scic_sds_controller_remote_device_stopped(
	struct scic_sds_controller *this_controller,
	struct scic_sds_remote_device *the_device);

void scic_sds_controller_copy_task_context(
	struct scic_sds_controller *this_controller,
	struct scic_sds_request *this_request);

void scic_sds_controller_register_setup(
	struct scic_sds_controller *this_controller);

#endif /* _SCIC_SDS_CONTROLLER_H_ */
