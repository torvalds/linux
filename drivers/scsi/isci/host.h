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
#ifndef _SCI_HOST_H_
#define _SCI_HOST_H_

#include "scic_config_parameters.h"
#include "remote_device.h"
#include "phy.h"
#include "pool.h"
#include "sci_base_state_machine.h"
#include "remote_node_table.h"
#include "registers.h"
#include "scu_unsolicited_frame.h"
#include "unsolicited_frame_control.h"
#include "scic_sds_port_configuration_agent.h"

struct scic_sds_request;
struct scu_task_context;

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
 * This structure represents the SCU controller object.
 */
struct scic_sds_controller {
	/**
	 * This field contains the information for the base controller state
	 * machine.
	 */
	struct sci_base_state_machine state_machine;

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

struct isci_host {
	struct scic_sds_controller sci;
	union scic_oem_parameters oem_parameters;

	int id; /* unique within a given pci device */
	struct list_head timers;
	void *core_ctrl_memory;
	struct dma_pool *dma_pool;
	struct isci_phy phys[SCI_MAX_PHYS];
	struct isci_port ports[SCI_MAX_PORTS + 1]; /* includes dummy port */
	struct sas_ha_struct sas_ha;

	int can_queue;
	spinlock_t queue_lock;
	spinlock_t state_lock;

	struct pci_dev *pdev;

	enum isci_status status;
	#define IHOST_START_PENDING 0
	#define IHOST_STOP_PENDING 1
	unsigned long flags;
	wait_queue_head_t eventq;
	struct Scsi_Host *shost;
	struct tasklet_struct completion_tasklet;
	struct list_head requests_to_complete;
	struct list_head requests_to_errorback;
	spinlock_t scic_lock;

	struct isci_remote_device devices[SCI_MAX_REMOTE_DEVICES];
};

/**
 * enum scic_sds_controller_states - This enumeration depicts all the states
 *    for the common controller state machine.
 */
enum scic_sds_controller_states {
	/**
	 * Simply the initial state for the base controller state machine.
	 */
	SCI_BASE_CONTROLLER_STATE_INITIAL = 0,

	/**
	 * This state indicates that the controller is reset.  The memory for
	 * the controller is in it's initial state, but the controller requires
	 * initialization.
	 * This state is entered from the INITIAL state.
	 * This state is entered from the RESETTING state.
	 */
	SCI_BASE_CONTROLLER_STATE_RESET,

	/**
	 * This state is typically an action state that indicates the controller
	 * is in the process of initialization.  In this state no new IO operations
	 * are permitted.
	 * This state is entered from the RESET state.
	 */
	SCI_BASE_CONTROLLER_STATE_INITIALIZING,

	/**
	 * This state indicates that the controller has been successfully
	 * initialized.  In this state no new IO operations are permitted.
	 * This state is entered from the INITIALIZING state.
	 */
	SCI_BASE_CONTROLLER_STATE_INITIALIZED,

	/**
	 * This state indicates the the controller is in the process of becoming
	 * ready (i.e. starting).  In this state no new IO operations are permitted.
	 * This state is entered from the INITIALIZED state.
	 */
	SCI_BASE_CONTROLLER_STATE_STARTING,

	/**
	 * This state indicates the controller is now ready.  Thus, the user
	 * is able to perform IO operations on the controller.
	 * This state is entered from the STARTING state.
	 */
	SCI_BASE_CONTROLLER_STATE_READY,

	/**
	 * This state is typically an action state that indicates the controller
	 * is in the process of resetting.  Thus, the user is unable to perform
	 * IO operations on the controller.  A reset is considered destructive in
	 * most cases.
	 * This state is entered from the READY state.
	 * This state is entered from the FAILED state.
	 * This state is entered from the STOPPED state.
	 */
	SCI_BASE_CONTROLLER_STATE_RESETTING,

	/**
	 * This state indicates that the controller is in the process of stopping.
	 * In this state no new IO operations are permitted, but existing IO
	 * operations are allowed to complete.
	 * This state is entered from the READY state.
	 */
	SCI_BASE_CONTROLLER_STATE_STOPPING,

	/**
	 * This state indicates that the controller has successfully been stopped.
	 * In this state no new IO operations are permitted.
	 * This state is entered from the STOPPING state.
	 */
	SCI_BASE_CONTROLLER_STATE_STOPPED,

	/**
	 * This state indicates that the controller could not successfully be
	 * initialized.  In this state no new IO operations are permitted.
	 * This state is entered from the INITIALIZING state.
	 * This state is entered from the STARTING state.
	 * This state is entered from the STOPPING state.
	 * This state is entered from the RESETTING state.
	 */
	SCI_BASE_CONTROLLER_STATE_FAILED,

	SCI_BASE_CONTROLLER_MAX_STATES

};



/**
 * struct isci_pci_info - This class represents the pci function containing the
 *    controllers. Depending on PCI SKU, there could be up to 2 controllers in
 *    the PCI function.
 */
#define SCI_MAX_MSIX_INT (SCI_NUM_MSI_X_INT*SCI_MAX_CONTROLLERS)

struct isci_pci_info {
	struct msix_entry msix_entries[SCI_MAX_MSIX_INT];
	struct isci_host *hosts[SCI_MAX_CONTROLLERS];
	struct isci_orom *orom;
};

static inline struct isci_pci_info *to_pci_info(struct pci_dev *pdev)
{
	return pci_get_drvdata(pdev);
}

#define for_each_isci_host(id, ihost, pdev) \
	for (id = 0, ihost = to_pci_info(pdev)->hosts[id]; \
	     id < ARRAY_SIZE(to_pci_info(pdev)->hosts) && ihost; \
	     ihost = to_pci_info(pdev)->hosts[++id])

static inline enum isci_status isci_host_get_state(struct isci_host *isci_host)
{
	return isci_host->status;
}

static inline void isci_host_change_state(struct isci_host *isci_host,
					  enum isci_status status)
{
	unsigned long flags;

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_host = %p, state = 0x%x",
		__func__,
		isci_host,
		status);
	spin_lock_irqsave(&isci_host->state_lock, flags);
	isci_host->status = status;
	spin_unlock_irqrestore(&isci_host->state_lock, flags);

}

static inline int isci_host_can_queue(struct isci_host *isci_host, int num)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&isci_host->queue_lock, flags);
	if ((isci_host->can_queue - num) < 0) {
		dev_dbg(&isci_host->pdev->dev,
			"%s: isci_host->can_queue = %d\n",
			__func__,
			isci_host->can_queue);
		ret = -SAS_QUEUE_FULL;

	} else
		isci_host->can_queue -= num;

	spin_unlock_irqrestore(&isci_host->queue_lock, flags);

	return ret;
}

static inline void isci_host_can_dequeue(struct isci_host *isci_host, int num)
{
	unsigned long flags;

	spin_lock_irqsave(&isci_host->queue_lock, flags);
	isci_host->can_queue += num;
	spin_unlock_irqrestore(&isci_host->queue_lock, flags);
}

static inline void wait_for_start(struct isci_host *ihost)
{
	wait_event(ihost->eventq, !test_bit(IHOST_START_PENDING, &ihost->flags));
}

static inline void wait_for_stop(struct isci_host *ihost)
{
	wait_event(ihost->eventq, !test_bit(IHOST_STOP_PENDING, &ihost->flags));
}

static inline void wait_for_device_start(struct isci_host *ihost, struct isci_remote_device *idev)
{
	wait_event(ihost->eventq, !test_bit(IDEV_START_PENDING, &idev->flags));
}

static inline void wait_for_device_stop(struct isci_host *ihost, struct isci_remote_device *idev)
{
	wait_event(ihost->eventq, !test_bit(IDEV_STOP_PENDING, &idev->flags));
}

static inline struct isci_host *dev_to_ihost(struct domain_device *dev)
{
	return dev->port->ha->lldd_ha;
}

static inline struct isci_host *scic_to_ihost(struct scic_sds_controller *scic)
{
	/* XXX delete after merging scic_sds_contoller and isci_host */
	struct isci_host *ihost = container_of(scic, typeof(*ihost), sci);

	return ihost;
}

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

/* expander attached sata devices require 3 rnc slots */
static inline int scic_sds_remote_device_node_count(struct scic_sds_remote_device *sci_dev)
{
	struct domain_device *dev = sci_dev_to_domain(sci_dev);

	if ((dev->dev_type == SATA_DEV || (dev->tproto & SAS_PROTOCOL_STP)) &&
	    !sci_dev->is_direct_attached)
		return SCU_STP_REMOTE_NODE_COUNT;
	return SCU_SSP_REMOTE_NODE_COUNT;
}

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

static inline struct device *scic_to_dev(struct scic_sds_controller *scic)
{
	return &scic_to_ihost(scic)->pdev->dev;
}

static inline struct device *sciphy_to_dev(struct scic_sds_phy *sci_phy)
{
	struct isci_phy *iphy = sci_phy_to_iphy(sci_phy);

	if (!iphy || !iphy->isci_port || !iphy->isci_port->isci_host)
		return NULL;

	return &iphy->isci_port->isci_host->pdev->dev;
}

static inline struct device *sciport_to_dev(struct scic_sds_port *sci_port)
{
	struct isci_port *iport = sci_port_to_iport(sci_port);

	if (!iport || !iport->isci_host)
		return NULL;

	return &iport->isci_host->pdev->dev;
}

static inline struct device *scirdev_to_dev(struct scic_sds_remote_device *sci_dev)
{
	struct isci_remote_device *idev =
			container_of(sci_dev, typeof(*idev), sci);

	if (!idev || !idev->isci_port || !idev->isci_port->isci_host)
		return NULL;

	return &idev->isci_port->isci_host->pdev->dev;
}

enum {
	ISCI_SI_REVA0,
	ISCI_SI_REVA2,
	ISCI_SI_REVB0,
};

extern int isci_si_rev;

static inline bool is_a0(void)
{
	return isci_si_rev == ISCI_SI_REVA0;
}

static inline bool is_a2(void)
{
	return isci_si_rev == ISCI_SI_REVA2;
}

static inline bool is_b0(void)
{
	return isci_si_rev > ISCI_SI_REVA2;
}

void scic_sds_controller_post_request(struct scic_sds_controller *scic,
				      u32 request);
void scic_sds_controller_release_frame(struct scic_sds_controller *scic,
				       u32 frame_index);
void scic_sds_controller_copy_sata_response(void *response_buffer,
					    void *frame_header,
					    void *frame_buffer);
enum sci_status scic_sds_controller_allocate_remote_node_context(struct scic_sds_controller *scic,
								 struct scic_sds_remote_device *sci_dev,
								 u16 *node_id);
void scic_sds_controller_free_remote_node_context(
	struct scic_sds_controller *scic,
	struct scic_sds_remote_device *sci_dev,
	u16 node_id);
union scu_remote_node_context *scic_sds_controller_get_remote_node_context_buffer(
	struct scic_sds_controller *scic,
	u16 node_id);

struct scic_sds_request *scic_request_by_tag(struct scic_sds_controller *scic,
					     u16 io_tag);

struct scu_task_context *scic_sds_controller_get_task_context_buffer(
	struct scic_sds_controller *scic,
	u16 io_tag);

void scic_sds_controller_power_control_queue_insert(
	struct scic_sds_controller *scic,
	struct scic_sds_phy *sci_phy);

void scic_sds_controller_power_control_queue_remove(
	struct scic_sds_controller *scic,
	struct scic_sds_phy *sci_phy);

void scic_sds_controller_link_up(
	struct scic_sds_controller *scic,
	struct scic_sds_port *sci_port,
	struct scic_sds_phy *sci_phy);

void scic_sds_controller_link_down(
	struct scic_sds_controller *scic,
	struct scic_sds_port *sci_port,
	struct scic_sds_phy *sci_phy);

void scic_sds_controller_remote_device_stopped(
	struct scic_sds_controller *scic,
	struct scic_sds_remote_device *sci_dev);

void scic_sds_controller_copy_task_context(
	struct scic_sds_controller *scic,
	struct scic_sds_request *this_request);

void scic_sds_controller_register_setup(struct scic_sds_controller *scic);

enum sci_status scic_controller_continue_io(struct scic_sds_request *sci_req);
int isci_host_scan_finished(struct Scsi_Host *, unsigned long);
void isci_host_scan_start(struct Scsi_Host *);

int isci_host_init(struct isci_host *);

void isci_host_init_controller_names(
	struct isci_host *isci_host,
	unsigned int controller_idx);

void isci_host_deinit(
	struct isci_host *);

void isci_host_port_link_up(
	struct isci_host *,
	struct scic_sds_port *,
	struct scic_sds_phy *);
int isci_host_dev_found(struct domain_device *);

void isci_host_remote_device_start_complete(
	struct isci_host *,
	struct isci_remote_device *,
	enum sci_status);

void scic_controller_disable_interrupts(
	struct scic_sds_controller *scic);

enum sci_status scic_controller_start_io(
	struct scic_sds_controller *scic,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *io_request,
	u16 io_tag);

enum sci_task_status scic_controller_start_task(
	struct scic_sds_controller *scic,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *task_request,
	u16 io_tag);

enum sci_status scic_controller_terminate_request(
	struct scic_sds_controller *scic,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *request);

enum sci_status scic_controller_complete_io(
	struct scic_sds_controller *scic,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *io_request);

u16 scic_controller_allocate_io_tag(
	struct scic_sds_controller *scic);

enum sci_status scic_controller_free_io_tag(
	struct scic_sds_controller *scic,
	u16 io_tag);
#endif
